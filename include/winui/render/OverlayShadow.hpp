// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"

#include <algorithm>

#include <cmath>

namespace ui
{

/// The elevation (Z, in DIPs) of each Fluent surface class, which shadow_recipe() turns into a
/// shadow. A caller with its own Z (NavigationShell's pane is 32, Tabs control's overlay 16) can pass
/// a raw float instead.
namespace elevation_z
{
inline constexpr float Layer   = 1.0f;   ///< A layer surface; casts no shadow.
inline constexpr float Control = 2.0f;   ///< A control at rest/hover (pressed drops to 1); casts none.
inline constexpr float Card    = 8.0f;   ///< A card.
inline constexpr float ToolTip = 16.0f;  ///< A tooltip.
inline constexpr float Flyout  = 32.0f;  ///< A flyout, menu or other light-dismiss popup.
inline constexpr float Dialog  = 128.0f; ///< A dialog (and a window); the recipe clamps here.
} // namespace elevation_z

/// The two superimposed black shadows that make up a drop shadow: an omnidirectional `ambient`
/// and a downward-offset `directional`. Both are pure black, and only their alpha varies. Blur
/// and offsets are in DIPs, alphas 0..1. Anything below Z 4 casts nothing, which is why a
/// Fluent button has no shadow.
///
/// Example:
/// @code
/// // A card-like Border at Z = 8, drawn under its own background.
/// const ui::Rect         bounds{16, 16, 240, 120};
/// const ui::Color        card_background = ui::colors::White;
/// const ui::ShadowRecipe recipe = ui::ShadowRecipe::for_elevation(ui::elevation_z::Card, theme.dark);
/// if (recipe.casts())
/// {
///     ui::draw_drop_shadow(renderer, bounds, 8.0f, recipe);
/// }
/// renderer.fill_rounded_rect(bounds, 8.0f, card_background);
/// @endcode
struct ShadowRecipe
{
    float ambient_blur      = 0.0f; ///< Blur radius of the ambient shadow.
    float ambient_y         = 0.0f; ///< Downward offset of the ambient shadow.
    float ambient_alpha     = 0.0f; ///< Opacity of the ambient shadow; 0 draws none.
    float directional_blur  = 0.0f; ///< Blur radius of the directional (key-light) shadow.
    float directional_y     = 0.0f; ///< Downward offset of the directional shadow.
    float directional_alpha = 0.0f; ///< Opacity of the directional shadow; 0 draws none.

    /// Whether either shadow is visible at all.
    [[nodiscard]] constexpr bool casts() const
    {
        return ambient_alpha > 0.0f || directional_alpha > 0.0f;
    }

    /// The drop-shadow recipe for elevation `elevation`, with the alphas for a light or
    /// `dark` theme. A dark theme's alphas are roughly double. Up to Z 32 there is no ambient
    /// shadow. In a light theme the directional alpha stops growing at Z 16, so a ToolTip and a
    /// Flyout have the same opacity and differ only in blur and offset. A dark theme uses a flat
    /// 0.26 up to Z 32.
    [[nodiscard]] static ShadowRecipe for_elevation(float elevation, bool dark)
    {
        const float e = (std::min)(64.0f, elevation * 0.5f);
        if (e < 2.0f)
        {
            return ShadowRecipe{}; // no shadow: Control (2) and Layer (1) land here
        }

        ShadowRecipe r;
        r.directional_blur = e;
        r.directional_y    = e * 0.5f;
        if (e <= 16.0f)
        {
            r.ambient_blur      = 2.0f;
            r.ambient_alpha     = 0.0f;
            r.directional_alpha = dark ? 0.26f : (std::min)(e / 100.0f + 0.06f, 0.14f);
        }
        else
        {
            r.ambient_blur      = e / 3.0f;
            r.ambient_y         = 2.0f;
            r.ambient_alpha     = dark ? 0.37f : 0.15f;
            r.directional_alpha = dark ? 0.37f : 0.19f;
        }
        return r;
    }
};

namespace detail
{

/// How opaque a Gaussian-blurred black rectangle is at `t` blur-radii outside its own edge.
///
/// A blurred step edge is at half the shadow's nominal opacity exactly on the caster's
/// boundary, where the Gaussian straddles it. It rises to full opacity only well inside (where the
/// caster's own fill hides it) and decays outward on a Gaussian tail. Rings that reach full alpha
/// at the boundary and ramp down linearly would draw a shadow about twice as dark, spread over the
/// whole blur radius as a flat band.
///
/// `sigma = blur / 3`, the usual convention that the blur radius is the Gaussian's support. A
/// flyout's 16 DIP blur then stays visible for 10 DIPs: there this returns 0.030, which times a
/// dark flyout's 0.26 is 2/255, where a shadow stops being visible.
[[nodiscard]] inline float blurred_edge_coverage(float t)
{
    // 0.5 * erfc(t / (sigma_in_blur_radii * sqrt2)), sigma = 1/3 of a blur radius.
    constexpr float InvSigmaRootTwo = 3.0f / 1.41421356f;
    return 0.5f * std::erfc(t * InvSigmaRootTwo);
}

/// One blurred black rounded rect, faked as `Layers` nested rounded-rect fills, since `Renderer`
/// has no blur primitive. The outermost spreads a full blur radius, each inner one a step
/// tighter. Each ring's own alpha is solved so that the composite of every ring covering a
/// given distance lands on `alpha * blurred_edge_coverage(that distance)`: ring j sits under rings
/// 1..j-1, so it has to supply only what they left, `(target - so_far) / (1 - so_far)`. Drawn
/// outermost-first, so the caster's own fill, painted afterwards, covers the stack's center.
///
/// Rings whose solved alpha would round to nothing at 8 bits are skipped. With a Gaussian tail
/// the outermost two or three always are.
inline void draw_blurred_rect(Renderer& renderer, const Rect& bounds, float radius, float blur, float y_offset, float alpha)
{
    constexpr int Layers = 8;
    if (alpha <= 0.0f || blur <= 0.0f)
    {
        return;
    }
    const float peak   = (std::min)(alpha, 1.0f);
    float       so_far = 0.0f; // composite opacity of the rings drawn so far
    for (int j = 1; j <= Layers; ++j)
    {
        // Ring j spans (outer_t - step, outer_t] blur radii out; aim at its midpoint.
        const float step    = 1.0f / static_cast<float>(Layers);
        const float outer_t = static_cast<float>(Layers - j + 1) * step;
        const float target  = peak * blurred_edge_coverage(outer_t - step * 0.5f);
        const float ring    = (target - so_far) / (1.0f - so_far);
        so_far              = target;
        if (ring * 255.0f < 0.5f) // invisible once quantized, so skip the fill
        {
            continue;
        }
        const float spread = blur * outer_t;
        Rect        r      = bounds.inflate(spread);
        r.y += y_offset;
        renderer.fill_rounded_rect(r, radius + spread, colors::Black.with_alpha(ring));
    }
}

} // namespace detail

/// Draws `recipe`'s two shadows behind a surface occupying `bounds` (a rounded rect at `radius`).
/// Call before drawing the surface's own background so its fill paints over the shadow's center.
///
/// Renderer has no blur, so each shadow is a stack of up to eight nested fill_rounded_rect()
/// calls whose alphas composite to a Gaussian edge. The shadow reaches up to one blur radius past
/// `bounds`, so leave room there or don't clip it. A recipe that doesn't cast draws nothing.
///
/// Example:
/// @code
/// const ui::Rect bounds{16, 16, 200, 32};
/// ui::draw_drop_shadow(renderer, bounds, theme.overlay_corner_radius,
///                      ui::ShadowRecipe::for_elevation(ui::elevation_z::ToolTip, theme.dark));
/// @endcode
inline void draw_drop_shadow(Renderer& renderer, const Rect& bounds, float radius, const ShadowRecipe& recipe)
{
    detail::draw_blurred_rect(renderer, bounds, radius, recipe.ambient_blur, recipe.ambient_y, recipe.ambient_alpha);
    detail::draw_blurred_rect(renderer, bounds, radius, recipe.directional_blur, recipe.directional_y, recipe.directional_alpha);
}

/// Aero's popup drop shadow, separate from the Fluent recipe above. Aero has no elevation scale
/// and casts no shadow under a button, card or panel. It casts this one only, under a menu,
/// submenu, tooltip, combo box drop-down or toolbar overflow.
///
/// The shape: the popup's own rect moved 5 px right and down and filled `#71000000`. The whole
/// shadow lies inside that rect: a solid center 5 px in from each side, wrapped in a 5 px
/// fall-off band, with round corners at `radius + 5`. Because the rect moves by exactly the band
/// width, the top and left bands land under the popup, and a user sees a 5 px shadow down the
/// right and along the bottom. A caller reserves a 5 px strip on the right and bottom of the
/// popup for it.
///
/// Example:
/// @code
/// // Under a Vista menu popup, before its own background and border.
/// const ui::Rect popup_bounds{0, 0, 180, 120};
/// ui::draw_aero_popup_shadow(renderer, popup_bounds, 0.0f);
/// renderer.fill_rounded_rect(popup_bounds, 0.0f, theme.surfaces.menu);
/// @endcode
inline void draw_aero_popup_shadow(Renderer& renderer, const Rect& bounds, float radius)
{
    constexpr float Depth     = 5.0f;
    constexpr float BaseAlpha = 0x71 / 255.0f;
    // The classic drop shadow's alpha multiplier at 0.5 to 5 px out from the body edge. Walked
    // outermost-first and composited like draw_blurred_rect(), each ring adds what is left.
    struct Band
    {
        float distance;
        float falloff;
    };
    constexpr Band Bands[] = {{5.0f, 0.0f}, {4.5f, 0.02654f}, {3.5f, 0.12389f}, {2.5f, 0.38053f}, {1.5f, 0.74336f}, {0.5f, 1.0f}};

    // The band is the inner 5 px of `displaced`, not a halo outside it: every ring is measured out
    // from the displaced rect deflated by the depth, so the shadow reaches 5 px past the popup, not 10.
    const Rect displaced{bounds.x + Depth, bounds.y + Depth, bounds.width, bounds.height};
    const Rect core = displaced.inset(Thickness{Depth});
    // The corner radius is clamped to under half the core's shorter side, and each ring then adds
    // its own distance to it.
    const float core_radius = (std::max)(0.0f, (std::min)(radius, 0.5f * (std::min)(core.width, core.height)));
    float       so_far      = 0.0f; // composite opacity of the bands drawn so far
    for (const Band& band : Bands)
    {
        const float target = BaseAlpha * band.falloff;
        const float ring   = (target - so_far) / (1.0f - so_far);
        so_far             = target;
        if (ring * 255.0f < 0.5f) // invisible once quantized; the outermost band always is
        {
            continue;
        }
        renderer.fill_rounded_rect(core.inflate(band.distance), core_radius + band.distance, colors::Black.with_alpha(ring));
    }
}

/// Draws a popup's drop shadow. A light-dismiss surface (Flyout, MenuFlyout, the ComboBox popup)
/// sits at elevation 32, the default `z`. A tooltip is the one popup that sits a step lower, at
/// `elevation_z::ToolTip`. `dark` picks the dark-theme alphas; pass `theme.dark`.
///
/// Example:
/// @code
/// const ui::Rect flyout_bounds{0, 0, 240, 160};
/// ui::draw_overlay_shadow(renderer, flyout_bounds, theme.overlay_corner_radius, theme.dark);
/// renderer.fill_rounded_rect(flyout_bounds, theme.overlay_corner_radius, theme.surfaces.menu);
/// @endcode
inline void draw_overlay_shadow(Renderer& renderer, const Rect& bounds, float radius, bool dark, float z = elevation_z::Flyout)
{
    draw_drop_shadow(renderer, bounds, radius, ShadowRecipe::for_elevation(z, dark));
}

} // namespace ui
