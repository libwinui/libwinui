// SPDX-License-Identifier: MIT
#pragma once

#include "../../render/Border.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <cstddef>

namespace ui::detail
{

// A text field's minimum height, and the spelling squiggle's geometry.
inline constexpr float FieldMinHeight      = 32.0f;
inline constexpr float SquiggleStrokeWidth = 1.0f;
inline constexpr float SquiggleAmplitude   = 1.0f;
inline constexpr float SquiggleWavelength  = 4.0f;

[[nodiscard]] inline Thickness text_padding(const Theme& theme)
{
    return theme.sizing.text_control_padding.value_or(theme.padding);
}

// Where line `index` visibly ends: a soft-wrapped line's trailing space is not part of it.
[[nodiscard]] inline std::size_t visible_line_end(const TextLayout& layout, std::size_t index)
{
    const TextLayout::Line& line = layout.lines()[index];
    if (!line.hard_break && line.end > line.begin && layout.text()[line.end - 1] == ' ')
    {
        return line.end - 1;
    }
    return line.end;
}

// `invalid` (the control's `error` is set) draws the border, and the focus underline, in the
// critical color.
inline void draw_text_control_chrome(
    Renderer&    renderer,
    const Theme& theme,
    const Rect&  bounds,
    bool         enabled,
    bool         focused,
    bool         hovered,
    bool         invalid = false)
{
    // Fluent's text field fills: `focused` (`control_fill_input_active`) is opaque where
    // rest and hover are translucent. Non-Fluent themes leave `field_fills` unset.
    Color background = theme.colors().field;
    if (detail::controls(theme).field_fills)
    {
        background = !enabled  ? detail::controls(theme).field_fills->disabled
                     : focused ? detail::controls(theme).field_fills->focused
                     : hovered ? detail::controls(theme).field_fills->hover
                               : background;
    }

    const float radius = detail::resolved_field_corner_radius(theme);
    renderer.fill_rounded_rect(bounds, radius, background);
    if (!enabled)
    {
        // Disabled is the one state with a flat (non-gradient) stroke: `control_stroke_default`,
        // matching button_disabled.border.
        stroke_rounded_rect_inside(renderer, bounds, radius, theme.button_disabled.border, theme.border_width);
    }
    else if (detail::controls(theme).field_border_states)
    {
        // Aero: the whole border is the state (ControlChrome::FieldBorderStates), focus over hover, and
        // being the focus cue it leaves no underline to draw below.
        const detail::ControlChrome::FieldBorderStates& states = *detail::controls(theme).field_border_states;
        const LinearGradientBrush& brush = focused ? states.focused : hovered ? states.hover : states.rest;
        stroke_rounded_rect_gradient_inside(renderer, bounds, radius, brush, states.extent_px, theme.border_width);
        if (invalid)
        {
            stroke_rounded_rect_inside(renderer, bounds, radius, theme.colors().critical, theme.border_width);
        }
        return;
    }
    else if (detail::controls(theme).field_border_gradient)
    {
        // The same elevation border draws at rest and on hover (hover
        // recolors only the fill), so `hovered` plays no part here, unlike Button.
        stroke_rounded_rect_gradient_inside(renderer,
                                            bounds,
                                            radius,
                                            *detail::controls(theme).field_border_gradient,
                                            detail::controls(theme).field_border_gradient_extent_px,
                                            theme.border_width);
    }
    else
    {
        // Hover recolors the flat border only where a theme sets `field_border_hover`: Windows 10
        // steps it 60 % -> 80 %, Windows 11 aliases it to the rest brush. Focus wins over hover.
        const Color border = (hovered && !focused && detail::controls(theme).field_border_hover)
                                 ? *detail::controls(theme).field_border_hover
                                 : theme.surfaces.field_border;
        stroke_rounded_rect_inside(renderer, bounds, radius, border, theme.border_width);
    }
    if (invalid && enabled)
    {
        stroke_rounded_rect_inside(renderer, bounds, radius, theme.colors().critical, theme.border_width);
    }
    if (focused && enabled)
    {
        // The accent underline is `accent_fill_default`,
        // not `focus_ring`. Which sides thicken is `ControlChrome::field_focus_border_thickness`.
        draw_thickness_border(renderer,
                              bounds,
                              detail::controls(theme).field_focus_border_thickness,
                              invalid ? theme.colors().critical
                                      : detail::controls(theme).field_focus_underline.value_or(theme.colors().focus));
    }
}

} // namespace ui::detail
