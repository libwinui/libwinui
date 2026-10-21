// SPDX-License-Identifier: MIT
#include "winui/render/Chrome.hpp"

#include "../ui/themes/ControlChrome.hpp"
#include "DropdownChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <cstddef>

namespace ui
{

void fill_overlay_surface(Renderer& renderer, const Theme& theme, const Rect& bounds, float corner_radius, const Color& fallback)
{
    if (!theme.overlay_material)
    {
        renderer.fill_rounded_rect(bounds, corner_radius, fallback);
        return;
    }
    AcrylicBrush brush   = *theme.overlay_material;
    brush.fallback_color = fallback;
    renderer.draw_geometry(
        DrawGeometry{.geometry = PathGeometry::rectangle(bounds, corner_radius, corner_radius), .fill = Brush{brush}});
}

void draw_control_chrome(Renderer&           renderer,
                         const Rect&         bounds,
                         float               corner_radius,
                         const ButtonVisual& visual,
                         float               border_width,
                         bool                draw_flat_border_fallback,
                         bool                use_border_gradient)
{
    if (visual.background_gradient)
    {
        renderer.fill_rounded_rect_gradient(bounds, corner_radius, *visual.background_gradient);
    }
    else
    {
        renderer.fill_rounded_rect(bounds, corner_radius, visual.background);
    }

    // Aero's pressed inner shadow: two 2px bands, left and top, fading
    // to transparent. The corner where they cross is double-covered, as in the era's screenshots.
    if (visual.inner_shadow)
    {
        constexpr float ShadowExtent = 2.0f;
        const Color     from         = *visual.inner_shadow;
        const Color     to           = from.with_alpha(0.0f);
        const Rect      inner        = bounds.inset(Thickness{1.0f});
        const float     w            = std::min(ShadowExtent, inner.width);
        const float     h            = std::min(ShadowExtent, inner.height);
        if (w > 0.0f && h > 0.0f)
        {
            renderer.fill_rounded_rect_gradient(Rect{inner.x, inner.y, w, inner.height},
                                                0.0f,
                                                LinearGradientBrush{{GradientStop{0.0f, from}, GradientStop{1.0f, to}},
                                                                    GradientAxis::Horizontal});
            renderer.fill_rounded_rect_gradient(Rect{inner.x, inner.y, inner.width, h},
                                                0.0f,
                                                LinearGradientBrush{{GradientStop{0.0f, from}, GradientStop{1.0f, to}},
                                                                    GradientAxis::Vertical});
        }
    }

    // Aero's check-box well (Theme.hpp's `InsetWell`). Square whatever `corner_radius` is: both
    // layers are plain rectangles.
    if (visual.inset_well)
    {
        const InsetWell& well = *visual.inset_well;
        const Rect       rim  = bounds.inset(Thickness{well.inset});
        if (rim.width > 0.0f && rim.height > 0.0f)
        {
            renderer.fill_rounded_rect_gradient(rim, 0.0f, well.rim);
            const Rect fill = rim.inset(Thickness{1.0f});
            if (well.fill && fill.width > 0.0f && fill.height > 0.0f)
            {
                renderer.fill_rounded_rect_gradient(fill, 0.0f, *well.fill);
            }
        }
    }

    const float effective_border_width = visual.resolved_border_width(border_width);

    const bool is_bevel = visual.border_style == BorderStyle::Raised || visual.border_style == BorderStyle::Sunken;
    if (is_bevel && visual.bevel_frame)
    {
        // Windows 3.x's own edge grammar, which the four-ring DrawEdge shape below cannot express. It
        // rounds its frame itself, so it takes `corner_radius`.
        draw_win3x_border(renderer,
                          bounds,
                          visual.border_style,
                          *visual.bevel_frame,
                          visual.bevel_highlight,
                          visual.bevel_shadow,
                          effective_border_width,
                          corner_radius);
        return;
    }
    if (is_bevel)
    {
        draw_bevel_border(renderer,
                          bounds,
                          visual.border_style,
                          visual.bevel_light,
                          visual.bevel_highlight,
                          visual.bevel_shadow,
                          visual.bevel_dark_shadow,
                          effective_border_width,
                          BevelSides::All,
                          visual.bevel_soft,
                          visual.bevel_rings);
        return;
    }

    if (use_border_gradient && visual.border_gradient)
    {
        stroke_rounded_rect_gradient_inside(renderer,
                                            bounds,
                                            corner_radius,
                                            *visual.border_gradient,
                                            visual.border_gradient_extent_px,
                                            effective_border_width);
    }
    else if (draw_flat_border_fallback || visual.draw_border)
    {
        stroke_rounded_rect_inside(renderer, bounds, corner_radius, visual.border, effective_border_width);
    }

    // The inner highlight ring goes last, inside the rect the border leaves: Aero's 1px ring centers
    // 1.5px in, at (1.5, 1.5, w-3, h-3). The gradient runs top to bottom over the path's height.
    if (visual.inner_border)
    {
        const Rect  inside = bounds.inset(Thickness{effective_border_width});
        const float half   = visual.inner_border_width * 0.5f;
        const Rect  path   = inside.inset(Thickness{half});
        if (path.width > 0.0f && path.height > 0.0f)
        {
            stroke_rounded_rect_gradient_inside(renderer,
                                                inside,
                                                std::max(0.0f, corner_radius - effective_border_width),
                                                *visual.inner_border,
                                                path.height,
                                                visual.inner_border_width);
        }
    }
}

void draw_dropdown_arrow(Renderer& renderer, const detail::ControlChrome::DropdownChrome& chrome, const Rect& column, bool enabled)
{
    // Centered with the margin taken out of the column first (Vista's left 3 and top 1 move the
    // center right by 1.5 and down by 0.5).
    const Rect  inner = column.inset(chrome.arrow_margin);
    const float cx    = inner.left() + inner.width * 0.5f;
    const float cy    = inner.top() + inner.height * 0.5f;
    const float hw    = chrome.arrow_size.width * 0.5f;
    const float hh    = chrome.arrow_size.height * 0.5f;
    const Color fill  = enabled ? chrome.arrow : chrome.arrow_disabled;
    if (chrome.field_with_button)
    {
        // DFCS_SCROLLCOMBOBOX: the scroll bar's down arrow, per device pixel so its sides stay
        // diagonal at every scale.
        draw_classic_arrow(renderer, inner, ArrowDirection::Down, fill, chrome.arrow_size.height);
        return;
    }
    renderer.fill_polygon(FillPolygon{{Point{cx - hw, cy - hh}, Point{cx + hw, cy - hh}, Point{cx, cy + hh}}, fill});
}

void draw_disclosure(Renderer&        renderer,
                     const Theme&     theme,
                     const IconGlyph& glyph,
                     ArrowDirection   classic,
                     const Rect&      area,
                     const Color&     color,
                     float            glyph_size)
{
    if (theme.disclosure_visual == ThemeDisclosureVisual::ClassicArrow)
    {
        draw_classic_arrow(renderer, area, classic, color);
        return;
    }
    draw_icon(renderer, glyph, area, color, glyph_size, theme.icon_font_family);
}

void draw_dropdown_list(Renderer&                                    renderer,
                        const Theme&                                 theme,
                        const detail::ControlChrome::DropdownChrome& chrome,
                        const Rect&                                  bounds,
                        const std::vector<std::string>&              items,
                        float                                        row_height,
                        std::size_t                                  highlighted)
{
    renderer.fill_rounded_rect(bounds, 0.0f, chrome.list_background);
    TextStyle style  = theme.text_style;
    style.horizontal = TextAlign::Leading;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const Rect row{bounds.left(), bounds.top() + row_height * static_cast<float>(i), bounds.width, row_height};
        const bool lit = i == highlighted;
        if (lit)
        {
            renderer.fill_rounded_rect(row.inset(Symmetric{.horizontal = 1.0f, .vertical = 0.0f}), 0.0f, chrome.list_highlight);
        }
        const Rect text{row.left() + 1.0f + chrome.list_item_padding.left,
                        row.top(),
                        std::max(0.0f, row.width - 2.0f - chrome.list_item_padding.horizontal()),
                        row.height};
        renderer.draw_text(items[i], text, lit ? chrome.list_highlight_text : theme.colors().field_text, style);
    }
    stroke_rounded_rect_inside(renderer, bounds, 0.0f, chrome.list_border, 1.0f);
}

void draw_border(Renderer&                   renderer,
                 const Rect&                 bounds,
                 const std::optional<Color>& background,
                 const std::optional<Color>& border_brush,
                 const Thickness&            thickness,
                 float                       corner_radius)
{
    const bool has_border = border_brush.has_value() && border_brush->a > 0.0f &&
                            (thickness.left > 0.0f || thickness.top > 0.0f || thickness.right > 0.0f ||
                             thickness.bottom > 0.0f);
    if (background && background->a > 0.0f)
    {
        const Rect inner = has_border ? bounds.inset(thickness) : bounds;
        if (inner.width > 0.0f && inner.height > 0.0f)
        {
            const float inset = has_border ? std::max({thickness.left, thickness.top, thickness.right, thickness.bottom})
                                           : 0.0f;
            renderer.fill_rounded_rect(inner, std::max(0.0f, corner_radius - inset), *background);
        }
    }
    if (!has_border)
    {
        return;
    }
    const bool uniform = thickness.left == thickness.top && thickness.top == thickness.right &&
                         thickness.right == thickness.bottom;
    if (uniform)
    {
        stroke_rounded_rect_inside(renderer, bounds, corner_radius, *border_brush, thickness.left);
        return;
    }
    // Top and bottom run the full width; the sides fill what is left between them.
    const Color c = *border_brush;
    if (thickness.top > 0.0f)
    {
        renderer.fill_rounded_rect(Rect{bounds.x, bounds.y, bounds.width, thickness.top}, 0.0f, c);
    }
    if (thickness.bottom > 0.0f)
    {
        renderer.fill_rounded_rect(Rect{bounds.x, bounds.bottom() - thickness.bottom, bounds.width, thickness.bottom}, 0.0f, c);
    }
    const float side_top    = bounds.y + thickness.top;
    const float side_height = std::max(0.0f, bounds.height - thickness.vertical());
    if (thickness.left > 0.0f && side_height > 0.0f)
    {
        renderer.fill_rounded_rect(Rect{bounds.x, side_top, thickness.left, side_height}, 0.0f, c);
    }
    if (thickness.right > 0.0f && side_height > 0.0f)
    {
        renderer.fill_rounded_rect(Rect{bounds.right() - thickness.right, side_top, thickness.right, side_height}, 0.0f, c);
    }
}

void draw_classic_combo_field(Renderer&                                    renderer,
                              const Theme&                                 theme,
                              const detail::ControlChrome::DropdownChrome& era,
                              const ClassicComboState&                     state)
{
    const float edge = theme.border_width;

    renderer.fill_rounded_rect(state.box, 0.0f, state.enabled ? theme.colors().field : theme.colors().background);
    stroke_rounded_rect_inside(renderer, state.box, 0.0f, theme.surfaces.field_border, edge);

    const Rect          button{state.box.right() - edge - era.arrow_column,
                               state.box.top() + edge,
                               era.arrow_column,
                               std::max(0.0f, state.box.height - 2.0f * edge)};
    const bool          pressed = state.enabled && state.expanded;
    const ButtonVisual& face    = !state.enabled ? theme.button_disabled
                                  : pressed      ? theme.button_pressed
                                                 : theme.button_normal;
    draw_control_chrome(renderer, button, 0.0f, face, theme.border_width);
    const Point shift = pressed ? detail::controls(theme).pressed_content_offset : Point{0.0f, 0.0f};
    const Rect  arrow{button.x + shift.x, button.y + shift.y, button.width, button.height};
    draw_dropdown_arrow(renderer, era, arrow, state.enabled);

    // The text sits in the field left of the button; while focused the item is a selected run,
    // one pixel in from the edge, as the edit-less `CBS_DROPDOWNLIST` paints it.
    const Rect field{state.box.left() + edge,
                     state.box.top() + edge,
                     std::max(0.0f, button.left() - state.box.left() - edge),
                     button.height};
    const bool highlighted = state.focused && state.enabled && !state.expanded;
    if (highlighted)
    {
        renderer.fill_rounded_rect(field.inset(Thickness{1.0f}), 0.0f, theme.colors().selection);
    }
    const Rect label_rect{state.box.left() + era.text_inset,
                          state.box.top(),
                          std::max(0.0f, button.left() - state.box.left() - era.text_inset),
                          state.box.height};
    TextStyle  label_style = theme.text_style;
    label_style.horizontal = TextAlign::Leading;
    const Color text       = !state.enabled ? theme.colors().text_disabled
                             : highlighted  ? theme.colors().selection_text
                                            : theme.colors().field_text;
    if (!state.placeholder)
    {
        renderer.draw_text(state.text, label_rect, text, label_style);
    }
    else if (!state.text.empty())
    {
        renderer.draw_text(state.text, label_rect, state.enabled ? text.with_alpha(0.6f) : text, label_style);
    }
}


} // namespace ui
