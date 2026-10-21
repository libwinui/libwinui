// SPDX-License-Identifier: MIT
#include "winui/render/PathGeometry.hpp"

#include "winui/core/Geometry.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <cmath>
#include <cstdlib>

namespace ui
{

namespace
{

// The mini-language is ASCII; a byte of a multi-byte sequence is none of these.
bool is_ascii_alpha(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool is_ascii_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

bool is_ascii_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

char ascii_upper(char ch)
{
    return ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
}

// The distance, as a fraction of the radius, from an arc's end point to its Bézier control point
// for a quarter circle.
constexpr float ArcAsBezier = 0.5522847498f;

// ---- bounds ----------------------------------------------------------------------------

struct Extent
{
    float min_x = std::numeric_limits<float>::infinity();
    float min_y = std::numeric_limits<float>::infinity();
    float max_x = -std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();

    void add(Point p)
    {
        min_x = std::min(min_x, p.x);
        min_y = std::min(min_y, p.y);
        max_x = std::max(max_x, p.x);
        max_y = std::max(max_y, p.y);
    }
    [[nodiscard]] bool valid() const
    {
        return min_x <= max_x && min_y <= max_y;
    }
};

Point cubic_at(Point p0, Point p1, Point p2, Point p3, float t)
{
    const float u = 1.0f - t;
    const float a = u * u * u;
    const float b = 3.0f * u * u * t;
    const float c = 3.0f * u * t * t;
    const float d = t * t * t;
    return Point{a * p0.x + b * p1.x + c * p2.x + d * p3.x, a * p0.y + b * p1.y + c * p2.y + d * p3.y};
}

Point quadratic_at(Point p0, Point p1, Point p2, float t)
{
    const float u = 1.0f - t;
    return Point{u * u * p0.x + 2.0f * u * t * p1.x + t * t * p2.x, u * u * p0.y + 2.0f * u * t * p1.y + t * t * p2.y};
}

// The parameters in (0,1) where one coordinate of a cubic has a zero derivative: the roots of
// a*t^2 + b*t + c with a = -p0 + 3p1 - 3p2 + p3, b = 2(p0 - 2p1 + p2), c = p1 - p0.
void cubic_extrema(float p0, float p1, float p2, float p3, std::array<float, 2>& out, int& count)
{
    count             = 0;
    const double a    = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
    const double b    = 2.0 * (p0 - 2.0 * p1 + p2);
    const double c    = p1 - p0;
    auto         keep = [&](double t)
    {
        if (t > 0.0 && t < 1.0 && count < 2)
        {
            out[count++] = static_cast<float>(t);
        }
    };
    if (std::abs(a) < 1e-12)
    {
        if (std::abs(b) > 1e-12)
        {
            keep(-c / b);
        }
        return;
    }
    const double disc = b * b - 4.0 * a * c;
    if (disc < 0.0)
    {
        return;
    }
    const double root = std::sqrt(disc);
    keep((-b + root) / (2.0 * a));
    keep((-b - root) / (2.0 * a));
}

void add_segment_bounds(Extent& e, Point from, const PathSegment& s)
{
    switch (s.kind)
    {
        case PathSegmentKind::Line:
            e.add(s.p1);
            break;
        case PathSegmentKind::Quadratic:
        {
            e.add(s.p2);
            const float dx = from.x - 2.0f * s.p1.x + s.p2.x;
            const float dy = from.y - 2.0f * s.p1.y + s.p2.y;
            if (std::abs(dx) > 1e-9f)
            {
                const float t = (from.x - s.p1.x) / dx;
                if (t > 0.0f && t < 1.0f)
                {
                    e.add(quadratic_at(from, s.p1, s.p2, t));
                }
            }
            if (std::abs(dy) > 1e-9f)
            {
                const float t = (from.y - s.p1.y) / dy;
                if (t > 0.0f && t < 1.0f)
                {
                    e.add(quadratic_at(from, s.p1, s.p2, t));
                }
            }
            break;
        }
        case PathSegmentKind::Cubic:
        {
            e.add(s.p3);
            std::array<float, 2> ts{};
            int                  n = 0;
            cubic_extrema(from.x, s.p1.x, s.p2.x, s.p3.x, ts, n);
            for (int i = 0; i < n; ++i)
            {
                e.add(cubic_at(from, s.p1, s.p2, s.p3, ts[static_cast<std::size_t>(i)]));
            }
            cubic_extrema(from.y, s.p1.y, s.p2.y, s.p3.y, ts, n);
            for (int i = 0; i < n; ++i)
            {
                e.add(cubic_at(from, s.p1, s.p2, s.p3, ts[static_cast<std::size_t>(i)]));
            }
            break;
        }
    }
}

// ---- arcs ------------------------------------------------------------------------------

// SVG endpoint arc as cubic Béziers of at most a quarter turn each: center conversion per the
// SVG implementation notes F.6.5, radii scaled up per F.6.6, then k = 4/3 tan(delta/4) per piece.
void append_arc(std::vector<PathSegment>& out, Point from, float rx, float ry, float rotation, bool large_arc, bool sweep, Point to)
{
    if (from == to)
    {
        return;
    }
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx <= 0.0f || ry <= 0.0f)
    {
        out.push_back(PathSegment{PathSegmentKind::Line, to});
        return;
    }
    const double phi = static_cast<double>(rotation) * std::numbers::pi / 180.0;
    const double cp  = std::cos(phi);
    const double sp  = std::sin(phi);
    const double hx  = (from.x - to.x) * 0.5;
    const double hy  = (from.y - to.y) * 0.5;
    const double x1  = cp * hx + sp * hy;
    const double y1  = -sp * hx + cp * hy;
    double       a   = rx;
    double       b   = ry;
    const double lam = (x1 * x1) / (a * a) + (y1 * y1) / (b * b);
    if (lam > 1.0)
    {
        a *= std::sqrt(lam);
        b *= std::sqrt(lam);
    }
    const double num = a * a * b * b - a * a * y1 * y1 - b * b * x1 * x1;
    const double den = a * a * y1 * y1 + b * b * x1 * x1;
    double       co  = den > 0.0 ? std::sqrt(std::max(0.0, num / den)) : 0.0;
    if (large_arc == sweep)
    {
        co = -co;
    }
    const double cxp = co * a * y1 / b;
    const double cyp = -co * b * x1 / a;
    const double cx  = cp * cxp - sp * cyp + (from.x + to.x) * 0.5;
    const double cy  = sp * cxp + cp * cyp + (from.y + to.y) * 0.5;

    auto angle = [](double ux, double uy, double vx, double vy)
    {
        const double dot = ux * vx + uy * vy;
        const double len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
        double       ang = std::acos(std::clamp(len > 0.0 ? dot / len : 1.0, -1.0, 1.0));
        if (ux * vy - uy * vx < 0.0)
        {
            ang = -ang;
        }
        return ang;
    };
    const double theta1 = angle(1.0, 0.0, (x1 - cxp) / a, (y1 - cyp) / b);
    double       delta  = angle((x1 - cxp) / a, (y1 - cyp) / b, (-x1 - cxp) / a, (-y1 - cyp) / b);
    if (!sweep && delta > 0.0)
    {
        delta -= 2.0 * std::numbers::pi;
    }
    else if (sweep && delta < 0.0)
    {
        delta += 2.0 * std::numbers::pi;
    }

    const int    pieces = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / (std::numbers::pi * 0.5) - 1e-9)));
    const double step   = delta / pieces;
    const double k      = 4.0 / 3.0 * std::tan(step / 4.0);
    auto         point  = [&](double t)
    {
        const double ex = a * std::cos(t);
        const double ey = b * std::sin(t);
        return Point{static_cast<float>(cp * ex - sp * ey + cx), static_cast<float>(sp * ex + cp * ey + cy)};
    };
    auto derivative = [&](double t)
    {
        const double ex = -a * std::sin(t);
        const double ey = b * std::cos(t);
        return Point{static_cast<float>(cp * ex - sp * ey), static_cast<float>(sp * ex + cp * ey)};
    };
    double t0 = theta1;
    for (int i = 0; i < pieces; ++i)
    {
        const double t1 = t0 + step;
        const Point  a0 = point(t0);
        const Point  a1 = i + 1 == pieces ? to : point(t1);
        const Point  d0 = derivative(t0);
        const Point  d1 = derivative(t1);
        const float  kf = static_cast<float>(k);
        out.push_back(PathSegment{PathSegmentKind::Cubic,
                                  Point{a0.x + kf * d0.x, a0.y + kf * d0.y},
                                  Point{a1.x - kf * d1.x, a1.y - kf * d1.y},
                                  a1});
        t0 = t1;
    }
}

// ---- markup ----------------------------------------------------------------------------

class MarkupParser
{
public:
    explicit MarkupParser(std::string_view text) : text_(text)
    {
    }

    std::optional<PathGeometry> parse()
    {
        PathGeometry g;
        skip_space();
        if (pos_ + 1 < text_.size() && (text_[pos_] == 'F' || text_[pos_] == 'f') &&
            (text_[pos_ + 1] == '0' || text_[pos_ + 1] == '1'))
        {
            g.fill_rule = text_[pos_ + 1] == '1' ? FillRule::Nonzero : FillRule::EvenOdd;
            pos_ += 2;
        }

        char  command = 0;
        Point current{};
        Point figure_start{};
        Point last_control{};   // reflected by S / T
        char  last_command = 0; // what produced last_control
        bool  have_figure  = false;

        auto figure = [&]() -> PathFigure&
        {
            if (!have_figure)
            {
                g.figures.push_back(PathFigure{current, {}, false, true});
                figure_start = current;
                have_figure  = true;
            }
            return g.figures.back();
        };

        while (true)
        {
            skip_space();
            if (pos_ >= text_.size())
            {
                break;
            }
            const char ch = text_[pos_];
            if (is_ascii_alpha(ch))
            {
                command = ch;
                ++pos_;
            }
            else if (command == 0 || command == 'z' || command == 'Z')
            {
                return std::nullopt; // operands with no command to repeat
            }

            const bool rel = (command >= 'a' && command <= 'z');
            const auto off = [&](Point p) { return rel ? Point{p.x + current.x, p.y + current.y} : p; };
            switch (ascii_upper(command))
            {
                case 'M':
                {
                    Point p{};
                    if (!pair(p))
                    {
                        return std::nullopt;
                    }
                    current     = off(p);
                    have_figure = false;
                    figure();
                    last_command = 'M';
                    // Further pairs after a move are lines.
                    command = rel ? 'l' : 'L';
                    break;
                }
                case 'L':
                {
                    Point p{};
                    if (!pair(p))
                    {
                        return std::nullopt;
                    }
                    const Point to = off(p);
                    figure().segments.push_back(PathSegment{PathSegmentKind::Line, to});
                    current      = to;
                    last_command = 'L';
                    break;
                }
                case 'H':
                case 'V':
                {
                    float v = 0.0f;
                    if (!number(v))
                    {
                        return std::nullopt;
                    }
                    Point to = current;
                    if (ascii_upper(command) == 'H')
                    {
                        to.x = rel ? current.x + v : v;
                    }
                    else
                    {
                        to.y = rel ? current.y + v : v;
                    }
                    figure().segments.push_back(PathSegment{PathSegmentKind::Line, to});
                    current      = to;
                    last_command = 'L';
                    break;
                }
                case 'C':
                {
                    Point c1{};
                    Point c2{};
                    Point p{};
                    if (!pair(c1) || !pair(c2) || !pair(p))
                    {
                        return std::nullopt;
                    }
                    const Point a  = off(c1);
                    const Point b  = off(c2);
                    const Point to = off(p);
                    figure().segments.push_back(PathSegment{PathSegmentKind::Cubic, a, b, to});
                    current      = to;
                    last_control = b;
                    last_command = 'C';
                    break;
                }
                case 'S':
                {
                    Point c2{};
                    Point p{};
                    if (!pair(c2) || !pair(p))
                    {
                        return std::nullopt;
                    }
                    const Point a  = last_command == 'C'
                                         ? Point{2.0f * current.x - last_control.x, 2.0f * current.y - last_control.y}
                                         : current;
                    const Point b  = off(c2);
                    const Point to = off(p);
                    figure().segments.push_back(PathSegment{PathSegmentKind::Cubic, a, b, to});
                    current      = to;
                    last_control = b;
                    last_command = 'C';
                    break;
                }
                case 'Q':
                {
                    Point c1{};
                    Point p{};
                    if (!pair(c1) || !pair(p))
                    {
                        return std::nullopt;
                    }
                    const Point a  = off(c1);
                    const Point to = off(p);
                    figure().segments.push_back(PathSegment{PathSegmentKind::Quadratic, a, to});
                    current      = to;
                    last_control = a;
                    last_command = 'Q';
                    break;
                }
                case 'T':
                {
                    Point p{};
                    if (!pair(p))
                    {
                        return std::nullopt;
                    }
                    const Point a  = last_command == 'Q'
                                         ? Point{2.0f * current.x - last_control.x, 2.0f * current.y - last_control.y}
                                         : current;
                    const Point to = off(p);
                    figure().segments.push_back(PathSegment{PathSegmentKind::Quadratic, a, to});
                    current      = to;
                    last_control = a;
                    last_command = 'Q';
                    break;
                }
                case 'A':
                {
                    float rx    = 0.0f;
                    float ry    = 0.0f;
                    float rot   = 0.0f;
                    float large = 0.0f;
                    float sweep = 0.0f;
                    Point p{};
                    if (!number(rx) || !number(ry) || !number(rot) || !number(large) || !number(sweep) || !pair(p))
                    {
                        return std::nullopt;
                    }
                    const Point to = off(p);
                    append_arc(figure().segments, current, rx, ry, rot, large != 0.0f, sweep != 0.0f, to);
                    current      = to;
                    last_command = 'A';
                    break;
                }
                case 'Z':
                {
                    if (have_figure)
                    {
                        g.figures.back().closed = true;
                    }
                    current      = figure_start;
                    have_figure  = false;
                    last_command = 'Z';
                    break;
                }
                default:
                    return std::nullopt;
            }
        }
        return g;
    }

private:
    void skip_space()
    {
        while (pos_ < text_.size() && (is_ascii_space(text_[pos_]) || text_[pos_] == ','))
        {
            ++pos_;
        }
    }

    bool number(float& out)
    {
        skip_space();
        const std::size_t begin = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-'))
        {
            ++pos_;
        }
        bool digits = false;
        while (pos_ < text_.size() && is_ascii_digit(text_[pos_]))
        {
            ++pos_;
            digits = true;
        }
        if (pos_ < text_.size() && text_[pos_] == '.')
        {
            ++pos_;
            while (pos_ < text_.size() && is_ascii_digit(text_[pos_]))
            {
                ++pos_;
                digits = true;
            }
        }
        if (!digits)
        {
            pos_ = begin;
            return false;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E'))
        {
            std::size_t save = pos_++;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-'))
            {
                ++pos_;
            }
            bool exp_digits = false;
            while (pos_ < text_.size() && is_ascii_digit(text_[pos_]))
            {
                ++pos_;
                exp_digits = true;
            }
            if (!exp_digits)
            {
                pos_ = save;
            }
        }
        // from_chars rather than strtof: the decimal point is '.' whatever the C locale says.
        const std::string_view token = std::string_view{text_}.substr(begin, pos_ - begin);
        const char*            first = token.data() + (token.starts_with('+') ? 1 : 0);
        std::from_chars(first, token.data() + token.size(), out);
        return true;
    }

    bool pair(Point& out)
    {
        return number(out.x) && number(out.y);
    }

    std::string_view text_;
    std::size_t      pos_ = 0;
};

// ---- flattening (hit testing) ----------------------------------------------------------

std::vector<Point> flatten(const PathFigure& f)
{
    constexpr int      Steps = 16;
    std::vector<Point> pts{f.start};
    Point              from = f.start;
    for (const PathSegment& s : f.segments)
    {
        switch (s.kind)
        {
            case PathSegmentKind::Line:
                pts.push_back(s.p1);
                break;
            case PathSegmentKind::Quadratic:
                for (int i = 1; i <= Steps; ++i)
                {
                    pts.push_back(quadratic_at(from, s.p1, s.p2, static_cast<float>(i) / Steps));
                }
                break;
            case PathSegmentKind::Cubic:
                for (int i = 1; i <= Steps; ++i)
                {
                    pts.push_back(cubic_at(from, s.p1, s.p2, s.p3, static_cast<float>(i) / Steps));
                }
                break;
        }
        from = s.end();
    }
    return pts;
}

} // namespace

Rect Matrix::transform_bounds(const Rect& r) const
{
    const Point corners[] = {apply({r.left(), r.top()}),
                             apply({r.right(), r.top()}),
                             apply({r.left(), r.bottom()}),
                             apply({r.right(), r.bottom()})};
    float       left      = corners[0].x;
    float       top       = corners[0].y;
    float       right     = corners[0].x;
    float       bottom    = corners[0].y;
    for (const Point& p : corners)
    {
        left   = std::min(left, p.x);
        top    = std::min(top, p.y);
        right  = std::max(right, p.x);
        bottom = std::max(bottom, p.y);
    }
    return Rect{left, top, right - left, bottom - top};
}

Matrix Matrix::rotation(float degrees)
{
    const float r = degrees * std::numbers::pi_v<float> / 180.0f;
    const float c = std::cos(r);
    const float s = std::sin(r);
    return Matrix{c, s, -s, c, 0.0f, 0.0f};
}

PathGeometry PathGeometry::line(Point from, Point to)
{
    PathGeometry g;
    g.figures.push_back(PathFigure{from, {PathSegment{PathSegmentKind::Line, to}}, false, true});
    return g;
}

PathGeometry PathGeometry::rectangle(const Rect& rc, float radius_x, float radius_y)
{
    PathGeometry g;
    if (radius_x > 0.0f || radius_y > 0.0f)
    {
        const float rx     = std::min(std::abs(radius_x), rc.width / 2.0f);
        const float ry     = std::min(std::abs(radius_y), rc.height / 2.0f);
        const float bx     = (1.0f - ArcAsBezier) * rx;
        const float by     = (1.0f - ArcAsBezier) * ry;
        const float right  = rc.x + rc.width;
        const float bottom = rc.y + rc.height;

        std::array<Point, 16> p{};
        p[1].x = p[0].x = p[15].x = p[14].x = rc.x;
        p[2].x = p[13].x = rc.x + bx;
        p[3].x = p[12].x = rc.x + rx;
        p[4].x = p[11].x = right - rx;
        p[5].x = p[10].x = right - bx;
        p[6].x = p[7].x = p[8].x = p[9].x = right;

        p[2].y = p[3].y = p[4].y = p[5].y = rc.y;
        p[1].y = p[6].y = rc.y + by;
        p[0].y = p[7].y = rc.y + ry;
        p[15].y = p[8].y = bottom - ry;
        p[14].y = p[9].y = bottom - by;
        p[13].y = p[12].y = p[11].y = p[10].y = bottom;

        PathFigure f{p[0], {}, true, true};
        f.segments.push_back(PathSegment{PathSegmentKind::Cubic, p[1], p[2], p[3]});
        f.segments.push_back(PathSegment{PathSegmentKind::Line, p[4]});
        f.segments.push_back(PathSegment{PathSegmentKind::Cubic, p[5], p[6], p[7]});
        f.segments.push_back(PathSegment{PathSegmentKind::Line, p[8]});
        f.segments.push_back(PathSegment{PathSegmentKind::Cubic, p[9], p[10], p[11]});
        f.segments.push_back(PathSegment{PathSegmentKind::Line, p[12]});
        f.segments.push_back(PathSegment{PathSegmentKind::Cubic, p[13], p[14], p[15]});
        g.figures.push_back(std::move(f));
        return g;
    }
    PathFigure f{Point{rc.x, rc.y}, {}, true, true};
    f.segments.push_back(PathSegment{PathSegmentKind::Line, Point{rc.x + rc.width, rc.y}});
    f.segments.push_back(PathSegment{PathSegmentKind::Line, Point{rc.x + rc.width, rc.y + rc.height}});
    f.segments.push_back(PathSegment{PathSegmentKind::Line, Point{rc.x, rc.y + rc.height}});
    g.figures.push_back(std::move(f));
    return g;
}

PathGeometry PathGeometry::ellipse(Point c, float rx, float ry)
{
    PathGeometry g;
    const float  kx = ArcAsBezier * rx;
    const float  ky = ArcAsBezier * ry;
    PathFigure   f{Point{c.x + rx, c.y}, {}, true, true};
    f.segments.push_back(
        PathSegment{PathSegmentKind::Cubic, Point{c.x + rx, c.y + ky}, Point{c.x + kx, c.y + ry}, Point{c.x, c.y + ry}});
    f.segments.push_back(
        PathSegment{PathSegmentKind::Cubic, Point{c.x - kx, c.y + ry}, Point{c.x - rx, c.y + ky}, Point{c.x - rx, c.y}});
    f.segments.push_back(
        PathSegment{PathSegmentKind::Cubic, Point{c.x - rx, c.y - ky}, Point{c.x - kx, c.y - ry}, Point{c.x, c.y - ry}});
    f.segments.push_back(
        PathSegment{PathSegmentKind::Cubic, Point{c.x + kx, c.y - ry}, Point{c.x + rx, c.y - ky}, Point{c.x + rx, c.y}});
    g.figures.push_back(std::move(f));
    return g;
}

PathGeometry PathGeometry::polyline(std::span<const Point> points, bool closed, FillRule rule)
{
    PathGeometry g;
    g.fill_rule = rule;
    if (points.size() < 2)
    {
        return g;
    }
    PathFigure f{points.front(), {}, closed, true};
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        f.segments.push_back(PathSegment{PathSegmentKind::Line, points[i]});
    }
    g.figures.push_back(std::move(f));
    return g;
}

PathGeometry PathGeometry::group(std::span<const PathGeometry> members, FillRule rule)
{
    PathGeometry g;
    g.fill_rule = rule;
    for (const PathGeometry& m : members)
    {
        g.figures.insert(g.figures.end(), m.figures.begin(), m.figures.end());
    }
    return g;
}

std::optional<PathGeometry> PathGeometry::parse(std::string_view markup)
{
    return MarkupParser{markup}.parse();
}

Rect PathGeometry::bounds() const
{
    Extent e;
    for (const PathFigure& f : figures)
    {
        e.add(f.start);
        Point from = f.start;
        for (const PathSegment& s : f.segments)
        {
            add_segment_bounds(e, from, s);
            from = s.end();
        }
    }
    if (!e.valid())
    {
        return Rect{};
    }
    return Rect{e.min_x, e.min_y, e.max_x - e.min_x, e.max_y - e.min_y};
}

PathGeometry PathGeometry::transformed(const Matrix& m) const
{
    PathGeometry g = *this;
    for (PathFigure& f : g.figures)
    {
        f.start = m.apply(f.start);
        for (PathSegment& s : f.segments)
        {
            s.p1 = m.apply(s.p1);
            s.p2 = m.apply(s.p2);
            s.p3 = m.apply(s.p3);
        }
    }
    return g;
}

bool PathGeometry::fill_contains(Point p) const
{
    int crossings = 0; // even-odd
    int winding   = 0; // nonzero
    for (const PathFigure& f : figures)
    {
        if (!f.filled)
        {
            continue;
        }
        const std::vector<Point> pts = flatten(f);
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            const Point a = pts[i];
            const Point b = pts[(i + 1) % pts.size()]; // every filled figure closes for filling
            if ((a.y <= p.y) != (b.y <= p.y))
            {
                const float x = a.x + (p.y - a.y) / (b.y - a.y) * (b.x - a.x);
                if (p.x < x)
                {
                    ++crossings;
                    winding += b.y > a.y ? 1 : -1;
                }
            }
        }
    }
    return fill_rule == FillRule::EvenOdd ? (crossings % 2) == 1 : winding != 0;
}

} // namespace ui
