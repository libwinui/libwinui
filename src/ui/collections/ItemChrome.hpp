// SPDX-License-Identifier: MIT
#pragma once

#include "../themes/ControlChrome.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <optional>

namespace ui::detail
{

inline constexpr float ItemCheckBoxSize = 20.0f;

inline void draw_item_check_box(
    Renderer&    renderer,
    const Theme& theme,
    const Rect&  box,
    bool         checked,
    ButtonState  state,
    bool         on_image,
    float        glyph_size)
{
    const bool   fluent = detail::controls(theme).list_item_subtle_selection;
    ButtonVisual v      = detail::controls(theme).check_box_box.for_state(state, checked);
    if (fluent && on_image && !checked)
    {
        switch (state)
        {
            case ButtonState::Hover:
                v.background = theme.palette.control_on_image_fill_secondary;
                break;
            case ButtonState::Pressed:
                v.background = theme.palette.control_on_image_fill_tertiary;
                break;
            case ButtonState::Disabled:
                v.background = theme.palette.control_on_image_fill_disabled;
                break;
            case ButtonState::Normal:
                v.background = theme.palette.control_on_image_fill_default;
                break;
        }
    }
    const float radius = fluent ? 3.0f
                                : (detail::controls(theme).toggle_box_radius >= 0.0f
                                       ? detail::controls(theme).toggle_box_radius
                                       : (theme.corner_radius > 0.0f ? 3.0f : 0.0f));
    draw_control_chrome(renderer, box, radius, v, theme.border_width, /*draw_flat_border_fallback=*/true, /*use_border_gradient=*/false);
    if (checked)
    {
        draw_icon(renderer, detail::controls(theme).check_glyph, box, v.text, glyph_size, theme.icon_font_family);
    }
}

/// A tree's check box, which can also be indeterminate (a node whose children are partly selected):
/// the CheckBox's own indeterminate visual (`ControlChrome::check_box_indeterminate`) in that state.
inline void draw_item_check_box_tristate(
    Renderer&           renderer,
    const Theme&        theme,
    const Rect&         box,
    std::optional<bool> checked,
    ButtonState         state,
    float               glyph_size)
{
    if (checked)
    {
        draw_item_check_box(renderer, theme, box, *checked, state, /*on_image=*/false, glyph_size);
        return;
    }
    const IndeterminateVisuals& set = detail::controls(theme).check_box_indeterminate;
    const IndeterminateVisual&  iv  = set.for_state(state);
    const float radius = detail::controls(theme).list_item_subtle_selection ? 3.0f : (theme.corner_radius > 0.0f ? 3.0f : 0.0f);
    draw_control_chrome(renderer,
                        box,
                        radius,
                        iv.box,
                        theme.border_width,
                        /*draw_flat_border_fallback=*/true,
                        /*use_border_gradient=*/false);
    if (iv.dither)
    {
        const Rect inside = box.inset(Thickness{theme.border_width});
        renderer.fill_rect_pattern(inside, Pattern::Checkerboard50, *iv.dither, iv.box.background);
    }
    switch (set.kind)
    {
        case IndeterminateMark::Glyph:
            draw_icon(renderer, set.glyph, box, iv.box.text, glyph_size, theme.icon_font_family);
            break;
        case IndeterminateMark::Square:
        {
            const float side = std::min(set.square_size, box.width);
            const Rect  square{box.x + (box.width - side) * 0.5f, box.y + (box.height - side) * 0.5f, side, side};
            renderer.fill_rounded_rect(square, 0.0f, iv.box.text);
            break;
        }
        case IndeterminateMark::None:
            break;
    }
}

} // namespace ui::detail
