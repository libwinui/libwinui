// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Paint.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <concepts>
#include <optional>
#include <type_traits>

#include <cstddef>

namespace ui
{

/// A control's `Background`: either a fixed color, a theme resource (ThemeColor), or nothing,
/// plus the optional BrushTransition that animates every change of what is drawn.
///
/// The building block behind `set_background()` on Border, Grid, Panel, ForEach and the
/// list views: the owning control stores one, calls resolve() from its render pass and
/// advance() from its animation tick. A change is noticed in resolve(), the one place the theme
/// is known, whether it came from set(), a new resource, or the same resource under a new theme,
/// and the blend starts from the color on screen at that moment.
///
/// Example:
/// @code
/// class Swatch final : public ui::Element
/// {
/// public:
///     Swatch()
///     {
///         background_.set(ui::theme_colors::Background);
///         background_.set_transition(ui::BrushTransition{.duration = 150ms});
///     }
///     void set_color(ui::Color color)
///     {
///         background_.set(color); // blends from the old color at the next render
///         mark_dirty(ui::DirtyFlags::Visual);
///     }
///
/// protected:
///     ui::Size measure_override(ui::Size, const ui::MeasureContext&) const override
///     {
///         return ui::Size{40.0f, 40.0f};
///     }
///     void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
///     {
///         if (const std::optional<ui::Color> fill = background_.resolve(context.theme))
///         {
///             renderer.fill_rounded_rect(bounds(), 0.0f, *fill);
///         }
///     }
///     void tick_override(ui::Duration delta) override
///     {
///         if (background_.advance(delta)) // true while the blend runs
///         {
///             mark_dirty(ui::DirtyFlags::Visual);
///         }
///     }
///
/// private:
///     ui::BackgroundBrush background_;
/// };
/// @endcode
class BackgroundBrush
{
public:
    /// The fixed color; nullopt (a null brush) when there is none or a resource is set.
    [[nodiscard]] const std::optional<Color>& color() const
    {
        return color_;
    }
    /// The theme resource, or nullptr when a fixed color (or nothing) is set.
    [[nodiscard]] ThemeColor resource() const
    {
        return resource_;
    }
    /// Switches to a fixed color (nullopt: draw nothing) and clears any theme resource. The
    /// transition, if any, starts at the next resolve().
    void set(std::optional<Color> color)
    {
        color_    = color;
        resource_ = nullptr;
    }
    /// Switches to a fixed color and clears any theme resource.
    void set(Color color)
    {
        set(std::optional<Color>{color});
    }
    /// Switches to a theme resource, re-read on every resolve(), and clears any fixed color.
    void set(ThemeColor theme_color)
    {
        color_    = std::nullopt;
        resource_ = theme_color;
    }
    /// Switches to what `paint` holds: its color (a brush's flat color, see Paint), its
    /// resource, or nothing.
    void set(const Paint& paint)
    {
        color_    = paint.color();
        resource_ = paint.resource();
    }

    /// The blend between colors; nullopt (the default) makes every change immediate.
    [[nodiscard]] const std::optional<BrushTransition>& transition() const
    {
        return transition_;
    }
    /// Takes effect from the next change; a blend already running keeps going.
    void set_transition(std::optional<BrushTransition> transition)
    {
        transition_ = transition;
    }

    /// What to draw under `theme` now; nullopt draws nothing. A change since the last call starts
    /// the transition from the color that was on screen. Only color-to-color changes animate:
    /// a background appearing or disappearing is immediate.
    [[nodiscard]] std::optional<Color> resolve(const Theme& theme) const
    {
        const std::optional<Color> target = resource_ != nullptr ? std::optional<Color>{resource_(theme)} : color_;
        if (drawn_ && target != shown_)
        {
            animator_.retarget(animator_.current(shown_), target, theme.animations_enabled ? transition_ : std::nullopt);
        }
        drawn_ = true;
        shown_ = target;
        return animator_.current(target);
    }
    /// Advances a running transition by `delta`; true while it runs, so the owner
    /// keeps marking itself DirtyFlags::Visual. Call from the owner's tick_override().
    bool advance(Duration delta)
    {
        return animator_.advance(delta);
    }

private:
    std::optional<Color>           color_;
    ThemeColor                     resource_ = nullptr;
    std::optional<BrushTransition> transition_;
    mutable ColorAnimator          animator_;
    mutable std::optional<Color>   shown_; // the target drawn last time
    mutable bool                   drawn_ = false;
};

} // namespace ui
