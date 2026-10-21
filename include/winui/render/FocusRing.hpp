// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"

namespace ui
{

/// Draws a 1px dotted rectangle around `bounds`. The caller insets `bounds` from the control's
/// edges first, leaving a few pixels between the bevel or fill and the dots. Dots are
/// `dash_length`-long strokes of `color` separated by an equal gap, starting `phase` into the
/// first gap. The default, 1px dashes at phase 0, alternates every pixel like the classic focus
/// rect. Metro's dashed pair (draw_focus_ring()) draws this twice, one dash apart, so one
/// color's dashes land exactly in the other's gaps. Each dash is a Renderer::stroke_line().
///
/// Example:
/// @code
/// // The classic focus rect, 4px inside a Windows 95 button.
/// const ui::Rect bounds{0, 0, 75, 23};
/// ui::draw_dotted_rect(renderer, bounds.inset(ui::Thickness{4}), ui::colors::Black);
/// @endcode
void draw_dotted_rect(Renderer& renderer, const Rect& bounds, const Color& color, float dash_length = 1.0f, float phase = 0.0f);

/// Which focus visual a theme draws: Fluent's two solid rings, a classic single-color dotted
/// rect, or Metro's interleaved black+white dashed pair.
enum class FocusVisual
{
    FluentRings,     ///< Two concentric solid rings outside the control (Modern/HC).
    ClassicDotted,   ///< One dotted rect inset inside the control (95 through 7).
    MetroDashedPair, ///< Two interleaved dashed rects inset inside the control (Windows 8).
};

/// The focus visual's parameters as plain data. Theme::focus_ring_style() builds one from the
/// active theme.
///
/// Example:
/// @code
/// const ui::Rect     bounds{0, 0, 300, 40};
/// ui::FocusRingStyle style = theme.focus_ring_style();
/// // Inside `bounds`: the rings' outer edge on its edge, as the window rings a list row.
/// ui::draw_focus_ring(renderer, bounds.inset(ui::Thickness{style.outward_extent()}), theme.corner_radius, style);
///
/// // Or hand-built, e.g. a high-contrast yellow ring:
/// const ui::FocusRingStyle hc{
///     .kind        = ui::FocusVisual::FluentRings,
///     .outer_color = ui::Color::from_rgba(255, 255, 0),
///     .inner_color = ui::colors::Black,
/// };
/// @endcode
struct FocusRingStyle
{
    FocusVisual kind = FocusVisual::FluentRings; ///< Which visual draw_focus_ring() draws.
    Color       outer_color;                     ///< Outer ring, or the phase-0 dashes / the dots.
    Color       inner_color;                     ///< Inner ring, or the offset dashes (Metro).
    float       outer_width = 2.0f;              ///< Outer ring width in DIPs (FluentRings).
    float       inner_width = 1.0f;              ///< Inner ring width in DIPs (FluentRings).
    /// How far the two-ring band reaches outward: where the outer ring's outer edge lands, not
    /// the center of its stroke.
    float margin = 3.0f;
    /// MetroDashedPair only: draw_dotted_rect()'s own dash_length. Metro uses a 1px dash on a
    /// 1px gap.
    float dash_length = 1.0f;

    /// How far outward draw_focus_ring() paints beyond the rect it's given, for this style's
    /// kind: `margin` for the Fluent rings, and zero for ClassicDotted and MetroDashedPair, which
    /// draw inside the given rect (see draw_focus_ring()). Insetting a rect by it puts the ring
    /// inside the rect in every era (Element::focus_ring_inside()).
    [[nodiscard]] constexpr float outward_extent() const
    {
        return kind == FocusVisual::FluentRings ? margin : 0.0f;
    }
};

/// Draws a focus visual at its own distance from each side of `bounds`. For the Fluent rings,
/// the outer ring's outer edge lands `outward` outside `bounds` on each side, inside for a
/// negative side. `FocusRingStyle::margin`, 3 on every side, is the usual distance, and the few
/// controls that differ pass their own. Any other kind of focus visual is drawn around `bounds`,
/// as draw_focus_ring() draws it.
///
/// Example:
/// @code
/// // A check box rings box and label as one unit, 7 out at the sides and 3 above and below.
/// const ui::FocusRingStyle style = theme.focus_ring_style();
/// const ui::Rect           content_rect{0, 0, 120, 20};
/// ui::draw_focus_ring_at(renderer, content_rect, ui::Symmetric{.horizontal = 7.0f, .vertical = 3.0f}, 4.0f, style);
/// @endcode
void draw_focus_ring_at(Renderer& renderer, const Rect& bounds, const Thickness& outward, float radius, const FocusRingStyle& style);

/// Draws the Fluent two-ring focus visual, whatever `style.kind` says: a `style.outer_width` ring
/// in `style.outer_color` whose outer edge lands `style.margin` outside `bounds`, and a
/// `style.inner_width` ring in `style.inner_color` touching it on the inside. Both follow the
/// control's corner `radius`, adjusted for each ring's distance, so the band stays concentric
/// with the control's rounding. Use it instead of draw_focus_ring() in a control whose classic
/// dotted rect needs other bounds than its rings, such as CheckBox.
///
/// Example:
/// @code
/// // CheckBox: the modern ring hugs the box, while the classic dotted rect wraps the label.
/// const ui::FocusRingStyle style = theme.focus_ring_style();
/// const ui::Rect           box_rect{0, 0, 20, 20};
/// const ui::Rect           label_rect{28, 0, 80, 20};
/// if (style.kind == ui::FocusVisual::FluentRings)
/// {
///     ui::draw_focus_ring_rings(renderer, box_rect, 4.0f, style);
/// }
/// else
/// {
///     ui::draw_focus_ring(renderer, label_rect, 0.0f, style);
/// }
/// @endcode
void draw_focus_ring_rings(Renderer& renderer, const Rect& bounds, float radius, const FocusRingStyle& style);

/// Draws the focus visual `style.kind` calls for around `bounds`, at the control's corner
/// `radius`:
///   ClassicDotted: a dotted rect inset 4px (draw_dotted_rect()).
///   MetroDashedPair: the same inset rect drawn twice, `outer_color` at phase 0 and
///     `inner_color` at phase `style.dash_length`, so one color's dashes fill the other's gaps.
///   FluentRings: draw_focus_ring_rings().
/// Every focusable control except CheckBox uses the same `bounds` for all three, so this is the
/// one call most render_override() implementations need.
///
/// Example:
/// @code
/// class MyControl : public ui::Element
/// {
///     void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
///     {
///         // ... the control's own drawing ...
///         if (is_focused() && focus_visible())
///         {
///             const ui::Theme& theme = context.theme;
///             ui::draw_focus_ring(renderer, bounds(), theme.corner_radius, theme.focus_ring_style());
///         }
///     }
/// };
/// @endcode
void draw_focus_ring(Renderer& renderer, const Rect& bounds, float radius, const FocusRingStyle& style);

} // namespace ui
