// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"

#include <algorithm>
#include <initializer_list>
#include <span>

#include <cmath>

namespace ui::lottie
{

// ---- easing --------------------------------------------------------------------------------

struct Easing
{
    enum class Kind
    {
        Linear,
        Cubic,
        HoldThenStep,
        StepThenHold,
    };
    Kind  kind = Kind::Linear;
    float x1   = 0.0f;
    float y1   = 0.0f;
    float x2   = 1.0f;
    float y2   = 1.0f;
};

inline constexpr Easing Linear{Easing::Kind::Linear};
inline constexpr Easing HoldThenStep{Easing::Kind::HoldThenStep};
inline constexpr Easing StepThenHold{Easing::Kind::StepThenHold};

/// A cubic Bezier easing through the control points (x1, y1) and (x2, y2).
[[nodiscard]] constexpr Easing cubic(float x1, float y1, float x2, float y2)
{
    return Easing{Easing::Kind::Cubic, x1, y1, x2, y2};
}

/// The eased fraction for `t` in [0,1].
[[nodiscard]] inline float ease(const Easing& e, float t)
{
    switch (e.kind)
    {
        case Easing::Kind::Linear:
            return t;
        case Easing::Kind::HoldThenStep:
            return t >= 1.0f ? 1.0f : 0.0f;
        case Easing::Kind::StepThenHold:
            return t > 0.0f ? 1.0f : 0.0f;
        case Easing::Kind::Cubic:
            break;
    }
    // Solve x(s) = t for the curve parameter by bisection (x is monotonic for x1, x2 in [0,1]),
    // then return y(s).
    const auto bez = [](float a, float b, float s)
    {
        const float u = 1.0f - s;
        return 3.0f * u * u * s * a + 3.0f * u * s * s * b + s * s * s;
    };
    float lo = 0.0f;
    float hi = 1.0f;
    float s  = t;
    for (int i = 0; i < 40; ++i)
    {
        s             = 0.5f * (lo + hi);
        const float x = bez(e.x1, e.x2, s);
        if (x < t)
        {
            lo = s;
        }
        else
        {
            hi = s;
        }
    }
    return bez(e.y1, e.y2, s);
}

// ---- key frames ----------------------------------------------------------------------------

template <typename T>
struct Key
{
    float  progress = 0.0f;
    T      value{};
    Easing easing = Linear;
};

[[nodiscard]] inline float lerp_value(float a, float b, float t)
{
    return a + (b - a) * t;
}
[[nodiscard]] inline Point lerp_value(Point a, Point b, float t)
{
    return Point{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

/// A scalar or 2D vector key-frame animation at `progress`, starting from the property's `initial`
/// value.
template <typename T>
[[nodiscard]] T animate(std::span<const Key<T>> keys, float progress, T initial)
{
    if (keys.empty())
    {
        return initial;
    }
    if (progress < keys.front().progress)
    {
        const float span = keys.front().progress;
        const float t    = span > 0.0f ? progress / span : 1.0f;
        return lerp_value(initial, keys.front().value, ease(keys.front().easing, std::clamp(t, 0.0f, 1.0f)));
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i)
    {
        const Key<T>& a = keys[i];
        const Key<T>& b = keys[i + 1];
        if (progress >= a.progress && progress < b.progress)
        {
            const float t = (progress - a.progress) / (b.progress - a.progress);
            return lerp_value(a.value, b.value, ease(b.easing, t));
        }
    }
    return keys.back().value;
}

template <typename T>
[[nodiscard]] T animate(std::initializer_list<Key<T>> keys, float progress, T initial)
{
    return animate<T>(std::span<const Key<T>>(keys.begin(), keys.size()), progress, initial);
}

/// A boolean key-frame animation, such as a layer's visibility: each key's value holds from its
/// progress on.
[[nodiscard]] inline bool animate_bool(std::initializer_list<Key<bool>> keys, float progress, bool initial)
{
    bool value = initial;
    for (const Key<bool>& k : keys)
    {
        if (progress >= k.progress)
        {
            value = k.value;
        }
    }
    return value;
}

/// A 2D vector key that is either a plain value or a cubic path over the scalar `param` (index into the params span given to animate_path()).
struct PathKey
{
    float  progress = 0.0f;
    Point  value{};
    Easing easing  = Linear;
    bool   is_path = false;
    Point  p0{}, p1{}, p2{}, p3{};
    int    param = 0;
};

[[nodiscard]] inline PathKey value_key(float progress, Point value, Easing easing = Linear)
{
    return PathKey{progress, value, easing};
}
[[nodiscard]] inline PathKey path_key(float progress, Point p0, Point p1, Point p2, Point p3, int param, Easing easing)
{
    PathKey k{progress, {}, easing, true, p0, p1, p2, p3, param};
    return k;
}

[[nodiscard]] inline Point cubic_point(Point p0, Point p1, Point p2, Point p3, float t)
{
    const float u = 1.0f - t;
    return Point{u * u * u * p0.x + 3.0f * u * u * t * p1.x + 3.0f * u * t * t * p2.x + t * t * t * p3.x,
                 u * u * u * p0.y + 3.0f * u * u * t * p1.y + 3.0f * u * t * t * p2.y + t * t * t * p3.y};
}

/// A 2D vector key-frame animation whose keys may be paths.
[[nodiscard]] inline Point animate_path(std::initializer_list<PathKey> keys,
                                        float                          progress,
                                        Point                          initial,
                                        std::span<const float>         params)
{
    const auto value = [&](const PathKey& k)
    {
        if (!k.is_path)
        {
            return k.value;
        }
        const float t = k.param >= 0 && static_cast<std::size_t>(k.param) < params.size()
                            ? params[static_cast<std::size_t>(k.param)]
                            : 0.0f;
        return cubic_point(k.p0, k.p1, k.p2, k.p3, t);
    };
    if (keys.size() == 0)
    {
        return initial;
    }
    const PathKey* first = keys.begin();
    if (progress < first->progress)
    {
        const float span = first->progress;
        const float t    = span > 0.0f ? progress / span : 1.0f;
        return lerp_value(initial, value(*first), ease(first->easing, std::clamp(t, 0.0f, 1.0f)));
    }
    for (const PathKey* a = keys.begin(); a + 1 < keys.end(); ++a)
    {
        const PathKey* b = a + 1;
        if (progress >= a->progress && progress < b->progress)
        {
            const float t = (progress - a->progress) / (b->progress - a->progress);
            return lerp_value(value(*a), value(*b), ease(b->easing, t));
        }
    }
    return value(*(keys.end() - 1));
}

// ---- transforms ----------------------------------------------------------------------------

struct ShapeTransform
{
    Point  center{};
    Point  scale{1.0f, 1.0f};
    float  rotation = 0.0f; // degrees, clockwise
    Matrix matrix{};
    Point  offset{};

    [[nodiscard]] Matrix to_matrix() const
    {
        Matrix m = Matrix::translation(-center.x, -center.y) * Matrix::scale(scale.x, scale.y);
        if (rotation != 0.0f)
        {
            m = m * Matrix::rotation(rotation);
        }
        return m * Matrix::translation(center.x, center.y) * matrix * Matrix::translation(offset.x, offset.y);
    }
};

// ---- drawing -------------------------------------------------------------------------------

/// A path geometry built figure by figure, the way an ID2D1GeometrySink takes it (BeginFigure,
/// AddLine, AddBezier, EndFigure).
class PathBuilder
{
public:
    explicit PathBuilder(FillRule rule = FillRule::Nonzero)
    {
        geometry_.fill_rule = rule;
    }
    PathBuilder& begin(float x, float y)
    {
        geometry_.figures.push_back(PathFigure{Point{x, y}, {}, false, true});
        return *this;
    }
    PathBuilder& line(float x, float y)
    {
        geometry_.figures.back().segments.push_back(PathSegment{PathSegmentKind::Line, Point{x, y}});
        return *this;
    }
    PathBuilder& bezier(float x1, float y1, float x2, float y2, float x3, float y3)
    {
        geometry_.figures.back().segments.push_back(
            PathSegment{PathSegmentKind::Cubic, Point{x1, y1}, Point{x2, y2}, Point{x3, y3}});
        return *this;
    }
    PathBuilder& end(bool closed)
    {
        geometry_.figures.back().closed = closed;
        return *this;
    }
    [[nodiscard]] PathGeometry build() const
    {
        return geometry_;
    }

private:
    PathGeometry geometry_;
};

/// Draws a visual of `natural` size scaled uniformly into `dest` (the caller already fitted
/// `dest` to the natural aspect ratio), everything in `foreground`.
class Painter
{
public:
    Painter(Renderer& renderer, const Rect& dest, Size natural, Color foreground) :
        renderer_(renderer),
        foreground_(foreground),
        to_dest_(Matrix::scale(natural.width > 0.0f ? dest.width / natural.width : 1.0f,
                               natural.height > 0.0f ? dest.height / natural.height : 1.0f) *
                 Matrix::translation(dest.x, dest.y))
    {
    }

    /// A filled sprite shape: `geometry` in the shape's own coordinates, `m` its full transform
    /// up to the visual's root.
    void fill(const PathGeometry& geometry, const Matrix& m) const
    {
        DrawGeometry cmd;
        cmd.geometry = geometry.transformed(m * to_dest_);
        cmd.fill     = foreground_;
        renderer_.draw_geometry(cmd);
    }

    /// A stroked sprite shape. The stroke is in the shape's coordinates, so the thickness
    /// scales with the transform (sqrt of the determinant, the transforms here being uniform).
    void stroke(const PathGeometry& geometry,
                const Matrix&       m,
                float               thickness,
                PenLineCap          cap         = PenLineCap::Flat,
                PenLineJoin         join        = PenLineJoin::Miter,
                float               miter_limit = 1.0f) const
    {
        const Matrix total = m * to_dest_;
        const float  scale = std::sqrt(std::abs(total.m11 * total.m22 - total.m12 * total.m21));
        DrawGeometry cmd;
        cmd.geometry                 = geometry.transformed(total);
        cmd.stroke                   = foreground_;
        cmd.stroke_style.thickness   = thickness * scale;
        cmd.stroke_style.start_cap   = cap;
        cmd.stroke_style.end_cap     = cap;
        cmd.stroke_style.dash_cap    = cap;
        cmd.stroke_style.line_join   = join;
        cmd.stroke_style.miter_limit = miter_limit; // a shape's miter limit defaults to 1
        renderer_.draw_geometry(cmd);
    }

    /// A layer's opacity below 1, around what it draws.
    void push_opacity(float opacity) const
    {
        renderer_.push_opacity_layer(opacity);
    }
    void pop_opacity() const
    {
        renderer_.pop_opacity_layer();
    }

    /// The layer's clip over its natural square, in destination coordinates.
    void push_clip(const Rect& natural_rect) const
    {
        const Point a = to_dest_.apply(Point{natural_rect.x, natural_rect.y});
        const Point b = to_dest_.apply(Point{natural_rect.right(), natural_rect.bottom()});
        renderer_.push_clip(Rect{a.x, a.y, b.x - a.x, b.y - a.y});
    }
    void pop_clip() const
    {
        renderer_.pop_clip();
    }

private:
    Renderer& renderer_;
    Color     foreground_;
    Matrix    to_dest_;
};

} // namespace ui::lottie
