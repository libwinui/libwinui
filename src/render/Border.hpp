// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"

namespace ui
{

/// Draws independent left/top/right/bottom edges of `color` around `bounds`. A zero-
/// thickness edge draws nothing, so multiple calls with different (color, thickness) pairs
/// layer correctly — e.g. a uniform 1px neutral border, then a bottom-only 2px accent
/// overlay on top, without either erasing the other.
///
/// Each edge is a square-cornered Renderer::fill_rounded_rect() inside `bounds` (the edges do
/// not grow outwards), and corners are square-cut even when the content below is rounded.
/// Thicknesses are in DIPs.
///
/// Example:
/// @code
/// // TextBox-style focus: a 1px neutral outline with a 2px accent underline over it.
/// ui::draw_thickness_border(renderer, bounds, ui::Thickness{1},
///                              ui::Color::from_rgba(0, 0, 0, 40));
/// ui::draw_thickness_border(renderer, bounds, ui::Edges{.bottom = 2},
///                              ui::Color::from_rgba(0, 95, 184));
/// @endcode
inline void draw_thickness_border(Renderer& renderer, const Rect& bounds, const Thickness& thickness, const Color& color)
{
    if (thickness.top > 0.0f)
    {
        renderer.fill_rounded_rect(Rect{bounds.left(), bounds.top(), bounds.width, thickness.top}, 0.0f, color);
    }
    if (thickness.bottom > 0.0f)
    {
        renderer.fill_rounded_rect(Rect{bounds.left(), bounds.bottom() - thickness.bottom, bounds.width, thickness.bottom},
                                   0.0f,
                                   color);
    }
    if (thickness.left > 0.0f)
    {
        renderer.fill_rounded_rect(Rect{bounds.left(), bounds.top(), thickness.left, bounds.height}, 0.0f, color);
    }
    if (thickness.right > 0.0f)
    {
        renderer.fill_rounded_rect(Rect{bounds.right() - thickness.right, bounds.top(), thickness.right, bounds.height},
                                   0.0f,
                                   color);
    }
}

} // namespace ui
