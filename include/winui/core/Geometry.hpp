// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"

#include <algorithm>
#include <limits>

namespace ui
{

/// The extent of an unbounded axis in a measure offer (Element::measure). An axis that has run
/// out of room is 0, which is a real bound, never "unbounded".
inline constexpr float Unbounded = std::numeric_limits<float>::infinity();

/// The axis a control runs along: a stack's direction, a slider's track, a pager's row of pips.
enum class Orientation
{
    Horizontal, ///< left to right
    Vertical,   ///< top to bottom
};

/// A 2D position in device-independent pixels (DIPs, 1/96 inch), y pointing down.
///
/// Used for pointer positions, rect origins and path vertices; coordinates are relative to
/// whatever the API says (usually the window client area).
///
/// Example:
/// @code
/// // In an element of your own: is the pointer over the close glyph?
/// class CardControl : public ui::Element
/// {
/// protected:
///     bool pointer_override(const ui::PointerEvent& e) override
///     {
///         const ui::Point corner{bounds().right() - 24.0f, bounds().y};
///         close_hot_ = ui::Rect{corner.x, corner.y, 24.0f, 24.0f}.contains(e.position);
///         return false;
///     }
///
/// private:
///     bool close_hot_ = false;
/// };
/// @endcode
struct Point
{
    float x = 0.0f; ///< Horizontal coordinate, DIPs.
    float y = 0.0f; ///< Vertical coordinate, DIPs (down is positive).

    /// Exact member-wise comparison.
    friend constexpr bool operator==(const Point&, const Point&) = default;
};

/// A width/height pair in DIPs: what Element::measure() returns and takes as `available`.
///
/// In a measure offer, ui::Unbounded (+infinity) on an axis means unbounded; 0 is no room.
///
/// Example:
/// @code
/// ui::ButtonControl button{"Save"};
/// const ui::Size    desired = button.measure(ui::Size{300, ui::Unbounded}, measurer, theme);
/// REQUIRE_FALSE(desired.empty());
/// @endcode
struct Size
{
    float width  = 0.0f; ///< Horizontal extent, DIPs.
    float height = 0.0f; ///< Vertical extent, DIPs.

    /// True if either extent is zero or negative, i.e. the size covers no area.
    [[nodiscard]] constexpr bool empty() const
    {
        return width <= 0.0f || height <= 0.0f;
    }

    /// Exact member-wise comparison.
    friend constexpr bool operator==(const Size&, const Size&) = default;
};

/// A 3D vector: what an element's translation, scale and center point are.
/// The renderer is 2D, so `z` is carried and animated but never drawn.
///
/// Example:
/// @code
/// auto element = std::make_unique<ui::ButtonControl>("Save");
/// element->set_center_point(ui::Vector3{50, 20, 0});
/// element->set_scale(ui::Vector3{1.1f, 1.1f, 1.0f});
/// element->set_translation(element->translation() + ui::Vector3{0, -4, 0});
/// @endcode
struct Vector3
{
    float x = 0.0f; ///< X component (DIPs for a translation/center, a factor for a scale).
    float y = 0.0f; ///< Y component.
    float z = 0.0f; ///< Z component; carried and animated but not rendered.

    /// Component-wise sum.
    friend constexpr Vector3 operator+(const Vector3& a, const Vector3& b)
    {
        return Vector3{a.x + b.x, a.y + b.y, a.z + b.z};
    }
    /// Component-wise difference.
    friend constexpr Vector3 operator-(const Vector3& a, const Vector3& b)
    {
        return Vector3{a.x - b.x, a.y - b.y, a.z - b.z};
    }
    /// Scales every component by `s`.
    friend constexpr Vector3 operator*(const Vector3& v, float s)
    {
        return Vector3{v.x * s, v.y * s, v.z * s};
    }
    /// Exact member-wise comparison.
    friend constexpr bool operator==(const Vector3&, const Vector3&) = default;
};

/// Some edges of a ui::Thickness by name, the others 0: `.margin = ui::Edges{.left = 32}`.
///
/// Converts to a Thickness wherever one is taken.
struct Edges
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    float                                             left   = 0.0f; ///< Left edge, DIPs.
    float                                             top    = 0.0f; ///< Top edge, DIPs.
    float                                             right  = 0.0f; ///< Right edge, DIPs.
    float                                             bottom = 0.0f; ///< Bottom edge, DIPs.
};

/// A ui::Thickness with one value on the left and right and another on the top and bottom:
/// `.padding = ui::Symmetric{.horizontal = 16, .vertical = 8}`.
///
/// Converts to a Thickness wherever one is taken.
struct Symmetric
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    float                                             horizontal = 0.0f; ///< The left and the right edge, DIPs.
    float                                             vertical   = 0.0f; ///< The top and the bottom edge, DIPs.
};

/// A per-edge thickness in DIPs, used for margins, paddings and border widths.
///
/// A number is the same value on every edge (`.margin = 8`). Other thicknesses name their edges,
/// through ui::Edges (`.margin = ui::Edges{.left = 32}`) or ui::Symmetric
/// (`.padding = ui::Symmetric{.horizontal = 16, .vertical = 8}`), so the code says which edge
/// gets which value.
///
/// Example:
/// @code
/// window.set_content(ui::VerticalStack{
///     .margin   = ui::Symmetric{.horizontal = 24, .vertical = 12},
///     .content  = {ui::Text{.text = "Indented", .margin = ui::Edges{.left = 16}}},
/// });
/// @endcode
struct Thickness
{
    float left   = 0.0f; ///< Left edge, DIPs.
    float top    = 0.0f; ///< Top edge, DIPs.
    float right  = 0.0f; ///< Right edge, DIPs.
    float bottom = 0.0f; ///< Bottom edge, DIPs.

    /// All four edges 0.
    constexpr Thickness() = default;
    /// The same value on all four edges, so a field takes `.margin = 8`.
    constexpr Thickness(float uniform) : // NOLINT(google-explicit-constructor)
        left(uniform),
        top(uniform),
        right(uniform),
        bottom(uniform)
    {
    }
    /// The edges `edges` names; the others 0.
    constexpr Thickness(const Edges& edges) : // NOLINT(google-explicit-constructor)
        left(edges.left),
        top(edges.top),
        right(edges.right),
        bottom(edges.bottom)
    {
    }
    /// `horizontal` on the left and right, `vertical` on the top and bottom.
    constexpr Thickness(const Symmetric& edges) : // NOLINT(google-explicit-constructor)
        left(edges.horizontal),
        top(edges.vertical),
        right(edges.horizontal),
        bottom(edges.vertical)
    {
    }

    /// left + right: the width this thickness takes out of a rect.
    [[nodiscard]] constexpr float horizontal() const
    {
        return left + right;
    }
    /// top + bottom: the height this thickness takes out of a rect.
    [[nodiscard]] constexpr float vertical() const
    {
        return top + bottom;
    }

    /// Exact member-wise comparison.
    friend constexpr bool operator==(const Thickness&, const Thickness&) = default;
};

/// Axis-aligned rectangle: (x, y) is the top-left, extents are width/height.
///
/// All values are DIPs. The layout rect handed to Element::arrange() is a Rect; a rect with a
/// non-positive extent is empty().
///
/// Example:
/// @code
/// const ui::Rect bounds{0, 0, 200, 32};
/// const ui::Rect content = bounds.inset(ui::Edges{.left = 12, .top = 5, .right = 12, .bottom = 6});
/// const ui::Rect focus   = bounds.inflate(3.0f); // focus visual outside the border
/// const ui::Rect visible = ui::intersection(content, ui::Rect{0, 0, 100, 100}); // {12, 5, 88, 21}
/// @endcode
struct Rect
{
    float x      = 0.0f; ///< Left edge, DIPs.
    float y      = 0.0f; ///< Top edge, DIPs.
    float width  = 0.0f; ///< Horizontal extent, DIPs.
    float height = 0.0f; ///< Vertical extent, DIPs.

    /// The empty rect at the origin.
    constexpr Rect() = default;
    /// From top-left corner and extents.
    constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), width(w_), height(h_)
    {
    }
    /// From top-left corner and a Size.
    constexpr Rect(Point origin, Size size) : x(origin.x), y(origin.y), width(size.width), height(size.height)
    {
    }

    /// Same as `x`.
    [[nodiscard]] constexpr float left() const
    {
        return x;
    }
    /// Same as `y`.
    [[nodiscard]] constexpr float top() const
    {
        return y;
    }
    /// x + width: the first x coordinate outside the rect.
    [[nodiscard]] constexpr float right() const
    {
        return x + width;
    }
    /// y + height: the first y coordinate outside the rect.
    [[nodiscard]] constexpr float bottom() const
    {
        return y + height;
    }

    /// The top-left corner.
    [[nodiscard]] constexpr Point origin() const
    {
        return {x, y};
    }
    /// The extents as a Size.
    [[nodiscard]] constexpr Size size() const
    {
        return {width, height};
    }
    /// The midpoint of the rect.
    [[nodiscard]] constexpr Point center() const
    {
        return {x + width * 0.5f, y + height * 0.5f};
    }

    /// True if either extent is zero or negative.
    [[nodiscard]] constexpr bool empty() const
    {
        return width <= 0.0f || height <= 0.0f;
    }

    /// Point-in-rect test. Left/top edges are inclusive, right/bottom exclusive.
    [[nodiscard]] constexpr bool contains(Point p) const
    {
        return p.x >= left() && p.x < right() && p.y >= top() && p.y < bottom();
    }

    /// True if the two rects share some area. Rects that only share an edge do not overlap, and an
    /// empty rect overlaps nothing.
    [[nodiscard]] constexpr bool intersects(const Rect& o) const
    {
        return !empty() && !o.empty() && left() < o.right() && right() > o.left() && top() < o.bottom() &&
               bottom() > o.top();
    }

    /// Shrink inward by a thickness (padding). Never produces negative extents.
    [[nodiscard]] constexpr Rect inset(const Thickness& t) const
    {
        const float w = (std::max)(0.0f, width - t.horizontal());
        const float h = (std::max)(0.0f, height - t.vertical());
        return {x + t.left, y + t.top, w, h};
    }

    /// Grow outward uniformly by `amount` on every edge. A negative amount shrinks, unclamped.
    [[nodiscard]] constexpr Rect inflate(float amount) const
    {
        return {x - amount, y - amount, width + 2.0f * amount, height + 2.0f * amount};
    }

    /// Exact member-wise comparison.
    friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

/// Intersection of two rectangles (empty rect if they do not overlap).
[[nodiscard]] constexpr Rect intersection(const Rect& a, const Rect& b)
{
    const float l   = (std::max)(a.left(), b.left());
    const float t   = (std::max)(a.top(), b.top());
    const float r   = (std::min)(a.right(), b.right());
    const float btm = (std::min)(a.bottom(), b.bottom());
    if (r <= l || btm <= t)
    {
        return {};
    }
    return {l, t, r - l, btm - t};
}

} // namespace ui
