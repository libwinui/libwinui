// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <optional>

namespace ui
{

/// Strokes a control's border so the whole ring lands inside `bounds`.
///
/// A stroke covers half its width on each side of its path, so a border centered on a control's
/// rect puts half of itself outside it. A clipping parent, such as `SwipeControl`, cuts that outer
/// half on all four sides, and under an aliased, pixel-snapped theme the remaining half pixel
/// rounds away. A border drawn inside never extends past its layout slot, like the edges of
/// draw_bevel_border() and draw_win3x_border().
///
/// The path moves in by half a stroke, and the radius with it, so the ring's outer edge stays on
/// the rect and corner the caller asked for. `width` is in DIPs.
///
/// Example:
/// @code
/// // A 2px outline whose outer edge sits exactly on bounds, corner radius 4.
/// const ui::Rect bounds{0, 0, 120, 32};
/// ui::stroke_rounded_rect_inside(renderer, bounds, 4.0f, theme.colors().text, 2.0f);
/// @endcode
inline void stroke_rounded_rect_inside(Renderer& renderer, const Rect& bounds, float corner_radius, const Color& color, float width)
{
    const float half = width * 0.5f;
    renderer.stroke_rounded_rect(bounds.inset(Thickness{half}), (std::max)(0.0f, corner_radius - half), color, width);
}

/// The gradient counterpart of stroke_rounded_rect_inside(), for Fluent's elevation border.
/// `extent_px` keeps its meaning, because the renderer anchors it at the edge of the path it
/// strokes, which moves in with it. See StrokeRoundedRectGradient for its sign.
inline void stroke_rounded_rect_gradient_inside(
    Renderer&                  renderer,
    const Rect&                bounds,
    float                      corner_radius,
    const LinearGradientBrush& brush,
    float                      extent_px,
    float                      width)
{
    const float half = width * 0.5f;
    renderer.stroke_rounded_rect_gradient(bounds.inset(Thickness{half}),
                                          (std::max)(0.0f, corner_radius - half),
                                          brush,
                                          extent_px,
                                          width);
}

/// Draws `visual`'s background fill and border around `bounds` at `corner_radius`, in this order:
///   1. The background: `visual.background_gradient` if set (XP and Vista chrome), else a flat
///      `visual.background` fill.
///   2. `visual.inner_shadow` if set (Aero's pressed state): two 2px bands fading out from the
///      top and left edges, over the fill and under the border. No other theme sets it.
///   3. `visual.inset_well` if set (Aero's check box): a square rim and a square fill inside the
///      well, both diagonal gradients, under the border.
///   4. The border, at most one of:
///      a. A bevel when `visual.border_style` is Raised or Sunken (the 3.x through 2000 chrome):
///         draw_win3x_border() when the visual has a `bevel_frame`, else draw_bevel_border().
///         Nothing is drawn after a bevel.
///      b. Otherwise `visual.border_gradient`, if set and `use_border_gradient` is true: Fluent's
///         elevation border on a button at rest or under the pointer. A check box's checked
///         visual carries one too, and the check box passes `use_border_gradient = false` to
///         leave it undrawn.
///      c. Otherwise a flat stroke of `visual.border`, when `draw_flat_border_fallback` or
///         `visual.draw_border` is true. Buttons pass false, so their pressed, disabled and
///         subtle states stay borderless, and a button visual that wants an outline sets
///         `draw_border` itself, as Metro's dark button does. A check box passes true, so its
///         box always shows an outline when there's no bevel.
///      b and c are stroked inside `bounds` (see stroke_rounded_rect_inside()).
///   5. `visual.inner_border` if set (Aero, Luna): an `inner_border_width` gradient ring stroked
///      inside the rect the border leaves, at the radius less the border. It is the innermost
///      layer, so it comes last.
///
/// Every border is drawn at `visual.resolved_border_width()`: a visual's own
/// `ButtonVisual::border_width` overrides the theme-wide `border_width` passed here, so one theme
/// can give a button a 2px outline and a check box none at the same time.
///
/// Text and content are the caller's to draw afterwards; the window draws the focus ring.
///
/// Example:
/// @code
/// class MyButton : public ui::Element
/// {
///     bool        pressed_ = false;
///     std::string label_   = "OK";
///
///     void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
///     {
///         const ui::Theme&        theme = context.theme;
///         const ui::ButtonVisual& v     = pressed_ ? theme.button_pressed : theme.button_normal;
///         ui::draw_control_chrome(renderer, bounds(), theme.resolved_button_corner_radius(), v,
///                                 theme.border_width);
///         renderer.draw_text(label_, bounds(), v.text, ui::TextStyle{});
///     }
/// };
/// @endcode
void draw_control_chrome(Renderer&           renderer,
                         const Rect&         bounds,
                         float               corner_radius,
                         const ButtonVisual& visual,
                         float               border_width,
                         bool                draw_flat_border_fallback = false,
                         bool                use_border_gradient       = true);

/// An expand, collapse or drop-down mark in `area`: the icon font's `glyph` at `glyph_size` (0
/// for the glyph's own size) in `theme.icon_font_family`, or, under a theme whose
/// `disclosure_visual` is ThemeDisclosureVisual::ClassicArrow, the era's solid 7 x 4 arrow
/// pointing `classic` (draw_classic_arrow()). Every control with such a mark draws it through
/// this call, so a theme changes all of them at once.
///
/// Example:
/// @code
/// // An Expander's header mark: a chevron, or a solid arrow in the classic packs.
/// const bool     expanded = true;
/// const ui::Rect chevron_rect{200, 8, 16, 16};
/// ui::draw_disclosure(renderer, theme, expanded ? ui::glyphs::ChevronUp : ui::glyphs::ChevronDown,
///                     expanded ? ui::ArrowDirection::Up : ui::ArrowDirection::Down, chevron_rect,
///                     theme.colors().text, 12.0f);
/// @endcode
void draw_disclosure(Renderer&        renderer,
                     const Theme&     theme,
                     const IconGlyph& glyph,
                     ArrowDirection   classic,
                     const Rect&      area,
                     const Color&     color,
                     float            glyph_size = 0.0f);

/// A popup's surface: `theme.overlay_material` (in-app acrylic) when the theme has one, drawn
/// through Renderer::draw_geometry() with `fallback` as the color a renderer that cannot blur
/// paints, and a plain `fallback` fill otherwise. `fallback` is the surface's solid color, usually
/// `theme.surfaces.menu`. Acrylic blurs what was drawn before it, so fill the surface first, then
/// draw its content.
///
/// Example:
/// @code
/// // A custom flyout's background.
/// const ui::Rect bounds{0, 0, 240, 160};
/// ui::fill_overlay_surface(renderer, theme, bounds, theme.overlay_corner_radius, theme.surfaces.menu);
/// @endcode
void fill_overlay_surface(Renderer& renderer, const Theme& theme, const Rect& bounds, float corner_radius, const Color& fallback);

/// A bordered box's background and border, as a Border or a panel draws them: the
/// background fills the rect inside the border, at the corner radius less the border, and the
/// border is laid inside `bounds`.
/// Either color may be absent, which draws nothing for that layer.
///
/// A uniform `thickness` strokes one rounded ring. A non-uniform one fills the four edges as
/// straight bands, so a rounded corner between two different widths comes out square.
///
/// Example:
/// @code
/// // What ui::BorderControl::render_override() does with its own properties.
/// const ui::Rect bounds{0, 0, 200, 120};
/// ui::draw_border(renderer, bounds, ui::Color::from_rgba(243, 243, 243),
///                    ui::Color::from_rgba(0, 0, 0, 40), ui::Thickness{1}, 8.0f);
/// @endcode
void draw_border(Renderer&                   renderer,
                 const Rect&                 bounds,
                 const std::optional<Color>& background,
                 const std::optional<Color>& border_brush,
                 const Thickness&            thickness,
                 float                       corner_radius);

} // namespace ui
