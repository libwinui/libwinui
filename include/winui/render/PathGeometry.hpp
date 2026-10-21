// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ui
{

/// Which parts of a self-intersecting or multi-figure geometry are inside.
/// `EvenOdd` (the default, markup `F0`) alternates at every crossing; `Nonzero` (`F1`) counts
/// winding direction.
enum class FillRule
{
    EvenOdd, ///< Inside when a ray crosses the outline an odd number of times.
    Nonzero, ///< Inside when the outline winds around the point at all.
};

/// An affine transform in row-vector layout: `x' = x*m11 + y*m21 + dx`,
/// `y' = x*m12 + y*m22 + dy`. `a * b` applies `a` first.
///
/// The default-constructed matrix is the identity. Renderer::push_transform() takes one, and
/// PathGeometry::transformed() runs a geometry's points through one.
///
/// Example:
/// @code
/// // Scale by 2, then move 10 DIPs right: (5,5) lands at (20,10).
/// const ui::Matrix m = ui::Matrix::scale(2.0f, 2.0f) * ui::Matrix::translation(10.0f, 0.0f);
/// const ui::Point  p = m.apply(ui::Point{5.0f, 5.0f});
/// const auto          back = m.inverted(); // nullopt only for a zero scale
/// renderer.push_transform(m);
/// // ... draw ...
/// renderer.pop_transform();
/// @endcode
struct Matrix
{
    float m11 = 1.0f; ///< x scale (and rotation cosine).
    float m12 = 0.0f; ///< How much x feeds y' (rotation / skew).
    float m21 = 0.0f; ///< How much y feeds x' (rotation / skew).
    float m22 = 1.0f; ///< y scale (and rotation cosine).
    float dx  = 0.0f; ///< Horizontal translation, in DIPs.
    float dy  = 0.0f; ///< Vertical translation, in DIPs.

    /// A pure translation by (`x`, `y`) DIPs.
    [[nodiscard]] static constexpr Matrix translation(float x, float y)
    {
        return Matrix{1.0f, 0.0f, 0.0f, 1.0f, x, y};
    }
    /// A scale about the origin, `sx` horizontally and `sy` vertically.
    [[nodiscard]] static constexpr Matrix scale(float sx, float sy)
    {
        return Matrix{sx, 0.0f, 0.0f, sy, 0.0f, 0.0f};
    }
    /// A rotation by `degrees` clockwise (y down), about the origin.
    [[nodiscard]] static Matrix rotation(float degrees);

    /// `p` run through the transform.
    [[nodiscard]] constexpr Point apply(Point p) const
    {
        return Point{p.x * m11 + p.y * m21 + dx, p.x * m12 + p.y * m22 + dy};
    }
    /// True for the exact identity (no tolerance), which lets a caller skip a push_transform().
    [[nodiscard]] constexpr bool is_identity() const
    {
        return m11 == 1.0f && m12 == 0.0f && m21 == 0.0f && m22 == 1.0f && dx == 0.0f && dy == 0.0f;
    }
    /// The inverse, or nullopt for a singular matrix (a zero scale), which maps every point onto
    /// a line and so has no way back. Pointer input hitting a scaled-to-nothing element gets this.
    [[nodiscard]] constexpr std::optional<Matrix> inverted() const
    {
        const float det = m11 * m22 - m12 * m21;
        if (det == 0.0f)
        {
            return std::nullopt;
        }
        const float inv = 1.0f / det;
        return Matrix{m22 * inv, -m12 * inv, -m21 * inv, m11 * inv, (m21 * dy - m22 * dx) * inv, (m12 * dx - m11 * dy) * inv};
    }
    /// The axis-aligned bounds of `r` once transformed.
    [[nodiscard]] Rect transform_bounds(const Rect& r) const;

    /// Composition: the result applies `a` first, then `b`.
    friend constexpr Matrix operator*(const Matrix& a, const Matrix& b)
    {
        return Matrix{a.m11 * b.m11 + a.m12 * b.m21,
                      a.m11 * b.m12 + a.m12 * b.m22,
                      a.m21 * b.m11 + a.m22 * b.m21,
                      a.m21 * b.m12 + a.m22 * b.m22,
                      a.dx * b.m11 + a.dy * b.m21 + b.dx,
                      a.dx * b.m12 + a.dy * b.m22 + b.dy};
    }
    friend constexpr bool operator==(const Matrix&, const Matrix&) = default;
};

/// Which curve this segment is, and so which of the points it uses.
enum class PathSegmentKind
{
    Line,      ///< Straight to `p1`.
    Quadratic, ///< Control point `p1`, end `p2`.
    Cubic,     ///< Control points `p1`, `p2`, end `p3`.
};

/// One piece of a figure, starting where the previous one ended. A `Line` uses `p1` as its end
/// point; a `Quadratic` has one control point `p1` and ends at `p2`; a `Cubic` has control points
/// `p1` and `p2` and ends at `p3`.
///
/// Arcs never appear here: the PathGeometry builders and PathGeometry::parse() convert them to cubics.
///
/// Example:
/// @code
/// ui::PathFigure figure{.start = ui::Point{0, 0}};
/// figure.segments.push_back({.kind = ui::PathSegmentKind::Line, .p1 = ui::Point{10, 0}});
/// figure.segments.push_back({.kind = ui::PathSegmentKind::Quadratic,
///                            .p1   = ui::Point{15, 5},
///                            .p2   = ui::Point{10, 10}});
/// @endcode
struct PathSegment
{
    PathSegmentKind kind = PathSegmentKind::Line; ///< The segment type; decides which points are read.
    Point           p1{};                         ///< The end point of a Line, else the first control point.
    Point           p2{};                         ///< The end of a Quadratic, the second control point of a Cubic.
    Point           p3{};                         ///< The end of a Cubic; unused otherwise.

    /// The point the segment ends at, whichever member that is for its `kind`.
    [[nodiscard]] Point end() const
    {
        return kind == PathSegmentKind::Line ? p1 : kind == PathSegmentKind::Quadratic ? p2 : p3;
    }

    friend bool operator==(const PathSegment&, const PathSegment&) = default;
};

/// One figure of a geometry: a start point, its segments, whether the last point joins the first
/// and whether the inside is painted.
///
/// Example:
/// @code
/// ui::PathGeometry triangle;
/// triangle.figures.push_back(ui::PathFigure{
///     .start    = ui::Point{0, 10},
///     .segments = {{.p1 = ui::Point{5, 0}}, {.p1 = ui::Point{10, 10}}},
///     .closed   = true,
/// });
/// @endcode
struct PathFigure
{
    Point                    start{};        ///< Where the first segment begins, in DIPs.
    std::vector<PathSegment> segments{};     ///< Each segment continues from the previous end point.
    bool                     closed = false; ///< Join the last point back to `start` when stroking.
    bool                     filled = true;  ///< Whether the figure's inside is painted by a fill.

    friend bool operator==(const PathFigure&, const PathFigure&) = default;
};

/// A set of figures and the fill rule between them: what a Path, a path icon or any other Shape draws.
///
/// A default-constructed PathGeometry is empty and draws nothing. Build one with the static builders
/// or parse path markup, then hand it to a ui::PathControl, to
/// `ui::Path{.data = ...}`, to IconSource::path(), or straight to Renderer::draw_geometry().
///
/// Example:
/// @code
/// const auto arrow = ui::PathGeometry::parse("F1 M 20,20L 24,10L 24,24L 5,24"); // nullopt if malformed
/// const ui::PathGeometry pill   = ui::PathGeometry::rectangle(ui::Rect{0, 0, 40, 16}, 8.0f, 8.0f);
/// const ui::PathGeometry both[] = {*arrow, pill};
/// const ui::PathGeometry group  = ui::PathGeometry::group(both, ui::FillRule::Nonzero);
///
/// window.set_content(ui::Path{
///     .data   = group,
///     .fill   = ui::colors::Black,
///     .stroke = ui::Color::from_rgba(0, 120, 215),
/// });
/// @endcode
class PathGeometry
{
public:
    /// The figures, drawn in order; all share `fill_rule`.
    std::vector<PathFigure> figures;
    /// How overlapping figures decide what is inside (markup `F0`/`F1`).
    FillRule fill_rule = FillRule::EvenOdd;

    /// A line: one open figure from `from` to `to`.
    [[nodiscard]] static PathGeometry line(Point from, Point to);
    /// A rectangle, also a Rectangle shape's outline: a closed figure, its corners
    /// rounded by elliptical arcs of `radius_x` by `radius_y` when either is positive (each
    /// clamped to half the rect), sixteen points in all.
    [[nodiscard]] static PathGeometry rectangle(const Rect& rect, float radius_x = 0.0f, float radius_y = 0.0f);
    /// An ellipse: a closed figure of four quarter arcs.
    [[nodiscard]] static PathGeometry ellipse(Point center, float radius_x, float radius_y);
    /// A Polyline (`closed` false) or Polygon (`closed` true): straight segments through
    /// `points`. Fewer than two points is empty.
    [[nodiscard]] static PathGeometry polyline(std::span<const Point> points, bool closed, FillRule rule = FillRule::EvenOdd);
    /// A group: every member's figures under one fill rule.
    [[nodiscard]] static PathGeometry group(std::span<const PathGeometry> members, FillRule rule = FillRule::EvenOdd);
    /// Parses path markup such as `F1 M 0,10 L 5,0 L 10,10 Z`. Returns nullopt when the markup
    /// doesn't parse. An empty string is an empty geometry.
    [[nodiscard]] static std::optional<PathGeometry> parse(std::string_view markup);

    /// True when there are no figures (the geometry draws nothing).
    [[nodiscard]] bool empty() const
    {
        return figures.empty();
    }

    /// The tight bounding box of every figure (Bézier extrema, not control points). An empty
    /// geometry has an empty rect at the origin.
    [[nodiscard]] Rect bounds() const;

    /// Every point run through `m`. Béziers stay Béziers: an affine transform of the control
    /// points is the transform of the curve.
    [[nodiscard]] PathGeometry transformed(const Matrix& m) const;

    /// Whether `p` is inside the filled area under the fill rule, with curves flattened finely
    /// enough for hit testing.
    [[nodiscard]] bool fill_contains(Point p) const;

    friend bool operator==(const PathGeometry&, const PathGeometry&) = default;
};

} // namespace ui
