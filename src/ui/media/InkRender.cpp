// SPDX-License-Identifier: MIT
#include "InkRender.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/media/InkCanvas.hpp"

#include <algorithm>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ui::detail
{

namespace
{
constexpr float InkFlattenStep      = 2.0f;  // DIPs between center-line samples on a smoothed curve
constexpr int   InkRoundNibSides    = 16;    // a round nib as a polygon, for the swept form
constexpr float InkHighlighterAlpha = 0.5f;  // what the pen flyout's preview uses for a highlighter
constexpr float InkSamePointEpsilon = 0.01f; // repeated samples are dropped before smoothing

float ink_distance(Point a, Point b)
{
    return std::hypot(b.x - a.x, b.y - a.y);
}

float ink_cross(Point o, Point a, Point b)
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// The points with consecutive duplicates removed (a stroke may sit still for a while).
std::vector<InkPoint> ink_distinct_points(const std::vector<InkPoint>& points)
{
    std::vector<InkPoint> out;
    out.reserve(points.size());
    for (const InkPoint& p : points)
    {
        if (out.empty() || ink_distance(out.back().position, p.position) > InkSamePointEpsilon)
        {
            out.push_back(p);
        }
    }
    return out;
}

// Catmull-Rom through the points as cubic Béziers: segment i runs from p[i] to p[i+1].
struct InkCubic
{
    Point p0, c1, c2, p3;
};

std::vector<InkCubic> ink_catmull_rom(const std::vector<InkPoint>& pts)
{
    std::vector<InkCubic> out;
    const std::size_t     n = pts.size();
    for (std::size_t i = 0; i + 1 < n; ++i)
    {
        const Point p0 = pts[i == 0 ? 0 : i - 1].position;
        const Point p1 = pts[i].position;
        const Point p2 = pts[i + 1].position;
        const Point p3 = pts[std::min(i + 2, n - 1)].position;
        out.push_back(InkCubic{p1,
                               Point{p1.x + (p2.x - p0.x) / 6.0f, p1.y + (p2.y - p0.y) / 6.0f},
                               Point{p2.x - (p3.x - p1.x) / 6.0f, p2.y - (p3.y - p1.y) / 6.0f},
                               p2});
    }
    return out;
}

Point ink_cubic_at(const InkCubic& c, float t)
{
    const float u = 1.0f - t;
    const float a = u * u * u;
    const float b = 3.0f * u * u * t;
    const float d = 3.0f * u * t * t;
    const float e = t * t * t;
    return Point{a * c.p0.x + b * c.c1.x + d * c.c2.x + e * c.p3.x, a * c.p0.y + b * c.c1.y + d * c.c2.y + e * c.p3.y};
}

bool ink_uniform_pressure(const InkStroke& stroke)
{
    if (stroke.attributes.ignore_pressure)
    {
        return true;
    }
    const float first = stroke.points.front().pressure;
    return std::ranges::all_of(stroke.points, [first](const InkPoint& p) { return p.pressure == first; });
}

// The nib at `center`, scaled, as a convex polygon.
void ink_nib_polygon(const InkDrawingAttributes& attributes, Point center, float scale, std::vector<Point>& out)
{
    const float hw = std::max(0.25f, attributes.size.width * 0.5f * scale);
    const float hh = std::max(0.25f, attributes.size.height * 0.5f * scale);
    if (attributes.pen_tip == InkPenTip::Rectangle)
    {
        out.push_back(Point{center.x - hw, center.y - hh});
        out.push_back(Point{center.x + hw, center.y - hh});
        out.push_back(Point{center.x + hw, center.y + hh});
        out.push_back(Point{center.x - hw, center.y + hh});
        return;
    }
    for (int i = 0; i < InkRoundNibSides; ++i)
    {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(InkRoundNibSides);
        out.push_back(Point{center.x + hw * std::cos(angle), center.y + hh * std::sin(angle)});
    }
}

// Andrew's monotone chain. Every hull comes out wound the same way, which is what lets a set of
// them overlap into one shape under the nonzero rule.
std::vector<Point> ink_convex_hull(std::vector<Point> pts)
{
    std::ranges::sort(pts, [](Point a, Point b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    if (pts.size() < 3)
    {
        return pts;
    }
    std::vector<Point> hull(pts.size() * 2);
    std::size_t        k = 0;
    for (const Point& p : pts)
    {
        while (k >= 2 && ink_cross(hull[k - 2], hull[k - 1], p) <= 0.0f)
        {
            --k;
        }
        hull[k++] = p;
    }
    const std::size_t lower = k + 1;
    for (std::size_t i = pts.size() - 1; i > 0; --i)
    {
        const Point& p = pts[i - 1];
        while (k >= lower && ink_cross(hull[k - 2], hull[k - 1], p) <= 0.0f)
        {
            --k;
        }
        hull[k++] = p;
    }
    hull.resize(k - 1);
    return hull;
}

void ink_add_polygon(PathGeometry& geometry, const std::vector<Point>& polygon)
{
    if (polygon.size() < 3)
    {
        return;
    }
    PathFigure figure{.start = polygon.front(), .segments = {}, .closed = true};
    figure.segments.reserve(polygon.size() - 1);
    for (std::size_t i = 1; i < polygon.size(); ++i)
    {
        figure.segments.push_back(PathSegment{.kind = PathSegmentKind::Line, .p1 = polygon[i]});
    }
    geometry.figures.push_back(std::move(figure));
}

float ink_point_segment_distance(Point p, Point a, Point b)
{
    const float dx  = b.x - a.x;
    const float dy  = b.y - a.y;
    const float len = dx * dx + dy * dy;
    float       t   = len > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len : 0.0f;
    t               = std::clamp(t, 0.0f, 1.0f);
    return ink_distance(p, Point{a.x + t * dx, a.y + t * dy});
}

float ink_segment_distance(Point a, Point b, Point c, Point d)
{
    const float d1 = ink_cross(a, b, c);
    const float d2 = ink_cross(a, b, d);
    const float d3 = ink_cross(c, d, a);
    const float d4 = ink_cross(c, d, b);
    if (((d1 > 0.0f && d2 < 0.0f) || (d1 < 0.0f && d2 > 0.0f)) && ((d3 > 0.0f && d4 < 0.0f) || (d3 < 0.0f && d4 > 0.0f)))
    {
        return 0.0f; // they cross
    }
    return std::min({ink_point_segment_distance(a, c, d),
                     ink_point_segment_distance(b, c, d),
                     ink_point_segment_distance(c, a, b),
                     ink_point_segment_distance(d, a, b)});
}
} // namespace

float ink_pressure_scale(const InkDrawingAttributes& attributes, float pressure)
{
    if (attributes.ignore_pressure)
    {
        return 1.0f;
    }
    return 0.25f + 1.5f * std::clamp(pressure, 0.0f, 1.0f);
}

Color ink_paint_color(const InkDrawingAttributes& attributes)
{
    Color c = attributes.color;
    if (attributes.draw_as_highlighter)
    {
        c.a *= InkHighlighterAlpha;
    }
    else if (attributes.kind == InkDrawingAttributesKind::Pencil)
    {
        c.a *= std::clamp(attributes.pencil_opacity, 0.0f, 1.0f);
    }
    return c;
}

std::vector<InkPoint> ink_centerline(const InkStroke& stroke)
{
    const std::vector<InkPoint> pts = ink_distinct_points(stroke.points);
    if (pts.size() < 3 || !stroke.attributes.fit_to_curve)
    {
        return pts;
    }
    std::vector<InkPoint>       out{pts.front()};
    const std::vector<InkCubic> cubics = ink_catmull_rom(pts);
    for (std::size_t i = 0; i < cubics.size(); ++i)
    {
        const float chord = ink_distance(cubics[i].p0, cubics[i].p3);
        const int   steps = std::max(1, static_cast<int>(std::ceil(chord / InkFlattenStep)));
        for (int s = 1; s <= steps; ++s)
        {
            const float t = static_cast<float>(s) / static_cast<float>(steps);
            out.push_back(
                InkPoint{ink_cubic_at(cubics[i], t), pts[i].pressure + (pts[i + 1].pressure - pts[i].pressure) * t});
        }
    }
    return out;
}

std::optional<DrawGeometry> ink_stroke_drawing(const InkStroke& stroke)
{
    if (stroke.points.empty())
    {
        return std::nullopt;
    }
    const InkDrawingAttributes& a     = stroke.attributes;
    const Color                 paint = ink_paint_color(a);
    const bool round_even = a.pen_tip == InkPenTip::Circle && a.size.width == a.size.height && ink_uniform_pressure(stroke);

    if (round_even)
    {
        const std::vector<InkPoint> pts   = ink_distinct_points(stroke.points);
        const float                 width = std::max(0.5f, a.size.width * ink_pressure_scale(a, pts.front().pressure));
        if (pts.size() == 1)
        {
            return DrawGeometry{.geometry = PathGeometry::ellipse(pts.front().position, width * 0.5f, width * 0.5f),
                                .fill     = Brush{paint}};
        }
        PathFigure figure{.start = pts.front().position, .segments = {}, .filled = false};
        if (a.fit_to_curve && pts.size() >= 3)
        {
            for (const InkCubic& c : ink_catmull_rom(pts))
            {
                figure.segments.push_back(PathSegment{.kind = PathSegmentKind::Cubic, .p1 = c.c1, .p2 = c.c2, .p3 = c.p3});
            }
        }
        else
        {
            for (std::size_t i = 1; i < pts.size(); ++i)
            {
                figure.segments.push_back(PathSegment{.kind = PathSegmentKind::Line, .p1 = pts[i].position});
            }
        }
        PathGeometry geometry;
        geometry.figures.push_back(std::move(figure));
        return DrawGeometry{.geometry     = std::move(geometry),
                            .stroke       = Brush{paint},
                            .stroke_style = StrokeStyle{.thickness = width,
                                                        .start_cap = PenLineCap::Round,
                                                        .end_cap   = PenLineCap::Round,
                                                        .line_join = PenLineJoin::Round}};
    }

    // The nib swept along the center line: the hull of the nib at each pair of neighboring
    // samples, all of them one filled shape.
    const std::vector<InkPoint> line = ink_centerline(stroke);
    PathGeometry                geometry;
    geometry.fill_rule = FillRule::Nonzero;
    std::vector<Point> pair;
    if (line.size() == 1)
    {
        ink_nib_polygon(a, line.front().position, ink_pressure_scale(a, line.front().pressure), pair);
        ink_add_polygon(geometry, ink_convex_hull(std::move(pair)));
    }
    for (std::size_t i = 0; i + 1 < line.size(); ++i)
    {
        pair.clear();
        ink_nib_polygon(a, line[i].position, ink_pressure_scale(a, line[i].pressure), pair);
        ink_nib_polygon(a, line[i + 1].position, ink_pressure_scale(a, line[i + 1].pressure), pair);
        ink_add_polygon(geometry, ink_convex_hull(pair));
    }
    return DrawGeometry{.geometry = std::move(geometry), .fill = Brush{paint}};
}

bool ink_stroke_touches(const InkStroke& stroke, Point a, Point b, float tolerance)
{
    const std::vector<InkPoint> line = ink_centerline(stroke);
    if (line.empty())
    {
        return false;
    }
    const InkDrawingAttributes& attrs = stroke.attributes;
    const float                 nib   = std::max(attrs.size.width, attrs.size.height) * 0.5f;
    if (line.size() == 1)
    {
        return ink_point_segment_distance(line.front().position, a, b) <=
               nib * ink_pressure_scale(attrs, line.front().pressure) + tolerance;
    }
    for (std::size_t i = 0; i + 1 < line.size(); ++i)
    {
        const float reach = nib * std::max(ink_pressure_scale(attrs, line[i].pressure),
                                           ink_pressure_scale(attrs, line[i + 1].pressure));
        if (ink_segment_distance(a, b, line[i].position, line[i + 1].position) <= reach + tolerance)
        {
            return true;
        }
    }
    return false;
}

std::optional<std::vector<InkStroke>> ink_stroke_cut(const InkStroke& stroke, Point a, Point b, float radius)
{
    if (stroke.points.empty())
    {
        return std::nullopt;
    }
    // The samples, at most half the radius apart, so a cut lands close to the eraser's edge.
    const float           step = std::max(0.5f, radius * 0.5f);
    std::vector<InkPoint> dense;
    dense.reserve(stroke.points.size());
    for (std::size_t i = 0; i < stroke.points.size(); ++i)
    {
        const InkPoint& p = stroke.points[i];
        if (i > 0)
        {
            const InkPoint& q      = stroke.points[i - 1];
            const float     length = std::hypot(p.position.x - q.position.x, p.position.y - q.position.y);
            const auto      pieces = static_cast<int>(std::ceil(length / step));
            for (int k = 1; k < pieces; ++k)
            {
                const float t = static_cast<float>(k) / static_cast<float>(pieces);
                dense.push_back(InkPoint{Point{q.position.x + (p.position.x - q.position.x) * t,
                                               q.position.y + (p.position.y - q.position.y) * t},
                                         q.pressure + (p.pressure - q.pressure) * t});
            }
        }
        dense.push_back(p);
    }

    std::vector<InkStroke> runs;
    InkStroke              run{.attributes = stroke.attributes};
    bool                   cut       = false;
    const auto             close_run = [&]
    {
        if (run.points.size() > 1)
        {
            runs.push_back(std::move(run));
        }
        run = InkStroke{.attributes = stroke.attributes};
    };
    for (const InkPoint& p : dense)
    {
        if (ink_point_segment_distance(p.position, a, b) <= radius)
        {
            cut = true;
            close_run();
        }
        else
        {
            run.points.push_back(p);
        }
    }
    if (!cut)
    {
        return std::nullopt;
    }
    close_run();
    return runs;
}

namespace
{
// Pencil ink: the stroke itself at this share of its opacity, under specks at the full opacity.
constexpr float InkPencilBaseAlpha = 0.55f;

// A well-mixed 64-bit hash (splitmix64's finalizer), for grain that does not depend on draw order.
std::uint64_t ink_grain_hash(std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// The grain of pencil ink: small specks scattered across the nib along the center line. Each
// sample places its specks from a hash of where it lies, so a stroke keeps its grain whenever it
// is drawn again, and the part drawn so far keeps it while the stroke grows.
std::optional<DrawGeometry> ink_pencil_grain(const InkStroke& stroke)
{
    // The center line, stepped every 1.5 DIPs: a straight run is only its two ends otherwise.
    const std::vector<InkPoint> center = ink_centerline(stroke);
    if (center.empty())
    {
        return std::nullopt;
    }
    std::vector<InkPoint> line{center.front()};
    for (std::size_t i = 1; i < center.size(); ++i)
    {
        const InkPoint& from  = center[i - 1];
        const InkPoint& to    = center[i];
        const float     span  = std::hypot(to.position.x - from.position.x, to.position.y - from.position.y);
        const int       steps = (std::max)(1, static_cast<int>(std::ceil(span / 1.5f)));
        for (int k = 1; k <= steps; ++k)
        {
            const float t = static_cast<float>(k) / static_cast<float>(steps);
            line.push_back(InkPoint{Point{from.position.x + (to.position.x - from.position.x) * t,
                                          from.position.y + (to.position.y - from.position.y) * t},
                                    from.pressure + (to.pressure - from.pressure) * t});
        }
    }
    const InkDrawingAttributes& a = stroke.attributes;
    PathGeometry                grain;
    grain.fill_rule = FillRule::Nonzero;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const Point p        = line[i].position;
        const Point prev     = line[i == 0 ? 0 : i - 1].position;
        const Point next     = line[std::min(i + 1, line.size() - 1)].position;
        float       tx       = next.x - prev.x;
        float       ty       = next.y - prev.y;
        const float len      = std::hypot(tx, ty);
        tx                   = len > 0.0f ? tx / len : 1.0f;
        ty                   = len > 0.0f ? ty / len : 0.0f;
        const float   half   = (std::max)(a.size.width, a.size.height) * 0.5f * ink_pressure_scale(a, line[i].pressure);
        const auto    qx     = static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(p.x * 4.0f)));
        const auto    qy     = static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(p.y * 4.0f)));
        std::uint64_t seed   = ink_grain_hash(qx * 0x100000001B3ull ^ qy);
        const int     specks = (std::max)(1, static_cast<int>(half * 0.9f));
        for (int s = 0; s < specks; ++s)
        {
            const auto unit = [&seed]
            {
                seed = ink_grain_hash(seed);
                return static_cast<float>(seed >> 40) / static_cast<float>(1ull << 24);
            };
            const float across = (unit() * 2.0f - 1.0f) * half * 0.9f;
            const float along  = unit() * 2.0f - 1.0f;
            const float size   = 0.35f + unit() * 0.5f;
            const Point c{p.x - ty * across + tx * along, p.y + tx * across + ty * along};
            grain.figures.push_back(
                PathFigure{.start    = Point{c.x - size, c.y},
                           .segments = {{.p1 = Point{c.x, c.y - size}},
                                        {.p1 = Point{c.x + size, c.y}},
                                        {.p1 = Point{c.x, c.y + size}}},
                           .closed   = true});
        }
    }
    return DrawGeometry{.geometry = std::move(grain), .fill = Brush{ink_paint_color(a)}};
}
} // namespace

std::vector<DrawGeometry> ink_stroke_drawings(const InkStroke& stroke)
{
    std::optional<DrawGeometry> base = ink_stroke_drawing(stroke);
    if (!base)
    {
        return {};
    }
    if (stroke.attributes.kind != InkDrawingAttributesKind::Pencil)
    {
        return {std::move(*base)};
    }
    Color lighter = ink_paint_color(stroke.attributes);
    lighter.a *= InkPencilBaseAlpha;
    (base->stroke ? base->stroke : base->fill) = Brush{lighter};
    std::vector<DrawGeometry> out{std::move(*base)};
    if (std::optional<DrawGeometry> grain = ink_pencil_grain(stroke))
    {
        out.push_back(std::move(*grain));
    }
    return out;
}

std::vector<DrawGeometry> ink_selected_drawing(const InkStroke& stroke)
{
    std::optional<DrawGeometry> outer = ink_stroke_drawing(stroke);
    if (!outer)
    {
        return {};
    }
    // The stroke's own shape in white over the same shape grown by a rim in its color, so even a
    // hairline reads as hollow.
    constexpr float Rim   = 1.0f;
    DrawGeometry    inner = *outer;
    if (outer->stroke)
    {
        // A stroked path: in white at its width, over the path 2 rims wider.
        inner.stroke                  = Brush{colors::White};
        outer->stroke_style.thickness = inner.stroke_style.thickness + 2.0f * Rim;
    }
    else
    {
        // A filled shape: filled white, over the shape outlined 2 rims wide (half of it outside).
        inner.fill          = Brush{colors::White};
        outer->stroke       = outer->fill;
        outer->stroke_style = StrokeStyle{.thickness = 2.0f * Rim, .line_join = PenLineJoin::Round};
    }
    return {std::move(*outer), std::move(inner)};
}

bool ink_point_in_polygon(Point p, std::span<const Point> outline)
{
    if (outline.size() < 3)
    {
        return false;
    }
    bool inside = false;
    for (std::size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++)
    {
        const Point a = outline[i];
        const Point b = outline[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
        {
            inside = !inside;
        }
    }
    return inside;
}

} // namespace ui::detail
