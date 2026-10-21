// SPDX-License-Identifier: MIT
// What the built-in controls hand the window's focus ring pass (Element::focus_ring_rect()) when
// their ring sits a set distance from a part of them.
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>

namespace ui::detail
{

// The rect draw_focus_ring() rings so that the Fluent rings' outer edge lands `outward` outside
// `bounds` on each side (draw_focus_ring_at()'s seed). The other focus visuals ring `bounds`.
[[nodiscard]] inline Rect focus_ring_seed(const Rect& bounds, const Thickness& outward, const FocusRingStyle& style)
{
    if (style.kind != FocusVisual::FluentRings)
    {
        return bounds;
    }
    return bounds.inset(Edges{.left   = style.margin - outward.left,
                              .top    = style.margin - outward.top,
                              .right  = style.margin - outward.right,
                              .bottom = style.margin - outward.bottom});
}

// A check box's, radio button's or toggle switch's ring: one ring around the box (or dot, or
// switch) and the label together, 7 out at the sides and 3 above and below. Stretched across its
// slot, the control still rings its content only.
[[nodiscard]] inline Rect toggle_content_ring(const Rect& bounds, Size desired_content, const Theme& theme)
{
    const Rect content{bounds.left(), bounds.top(), (std::min)(bounds.width, desired_content.width), bounds.height};
    return focus_ring_seed(content, Symmetric{.horizontal = 7.0f, .vertical = 3.0f}, theme.focus_ring_style());
}

} // namespace ui::detail
