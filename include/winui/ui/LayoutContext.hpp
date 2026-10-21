// SPDX-License-Identifier: MIT
// What a layout pass hands Element::measure_override() and arrange_override(), and a render pass
// render_override().
#pragma once

#include "winui/render/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

namespace ui
{

/// What a measure pass hands each element's measure_override(), and what a container hands on to
/// its children's measure(). Both must outlive the next measure() (see Element::measure()).
///
/// Example:
/// @code
/// class PaddedControl : public ui::Element
/// {
/// protected:
///     ui::Size measure_override(ui::Size available, const ui::MeasureContext& context) const override
///     {
///         const ui::Size inner = child_->measure(available, context);
///         return {inner.width + 16.0f, inner.height + 16.0f};
///     }
///
/// private:
///     std::unique_ptr<ui::Element> child_;
/// };
/// @endcode
struct MeasureContext
{
    const TextMeasurer& measurer; ///< Sizes text the way the renderer will draw it.
    const Theme&        theme;    ///< The theme in effect here (an ElementTheme scope already applied).

    /// The same context under another theme, for a container that hands its children one of
    /// its own.
    [[nodiscard]] MeasureContext with_theme(const Theme& other) const
    {
        return {measurer, other};
    }
};

/// What Element::arrange() hands arrange_override(): the measurer and theme the element was last
/// measured with, so a container that measures again while arranging (a virtualizing list
/// realizing rows) has them.
struct ArrangeContext
{
    const TextMeasurer& measurer; ///< The one the last measure() used.
    const Theme&        theme;    ///< The theme in effect here, as the last measure() saw it.

    /// The measure context for measuring children again from arrange_override().
    [[nodiscard]] MeasureContext measure_context() const
    {
        return {measurer, theme};
    }
};

/// What a render pass hands each element's render_override() beside the renderer, and what a
/// container hands on to its children's render(). It holds what drawing reads besides the
/// renderer; more fields (a DPI scale, the dirty rect) may be added later without breaking an
/// override.
///
/// Example:
/// @code
/// class CardControl : public ui::Element
/// {
/// protected:
///     void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
///     {
///         renderer.fill_rounded_rect(bounds(), context.theme.corner_radius, fill_);
///         child_->render(renderer, context);
///     }
///
/// private:
///     ui::Color                    fill_;
///     std::unique_ptr<ui::Element> child_;
/// };
/// @endcode
struct RenderContext
{
    const Theme& theme; ///< The theme in effect here (an ElementTheme scope already applied).

    /// The same context under another theme, for a container that draws its children with one
    /// of its own.
    [[nodiscard]] RenderContext with_theme(const Theme& other) const
    {
        return {other};
    }
};

} // namespace ui
