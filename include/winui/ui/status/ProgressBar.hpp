// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ui
{

/// A thin horizontal bar showing determinate progress, or an endless sweep while the duration
/// is unknown.
///
/// Determinate, the indicator covers the fraction of [`min()`, `max()`] that `value()` has
/// reached and jumps to each new value without animating. Indeterminate, it sweeps across in a
/// loop. `show_paused()` / `show_error()` tint the indicator in the caution / critical color
/// (error wins) and freeze an indeterminate sweep into one full-width bar. The bar is 200 DIPs
/// wide unless its slot is narrower, and as tall as the theme draws it. It takes no input and has
/// no disabled state.
///
/// `ui::ProgressBar` is the declarative form.
///
/// Example:
/// @code
/// ui::Property<double> progress{0.0};
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 12,
///     .content  = {
///         ui::ProgressBar{.value = progress, .width = 200.0f,
///                                .accessibility = {.name = "Download progress"}},
///         ui::ProgressBar{.width = 200.0f},
///         ui::Button{.text = "Step", .on_click = [&] { progress += 10.0; }},
///     },
/// });
///
/// // Imperatively:
/// auto bar = std::make_unique<ui::ProgressBarControl>(/*value=*/0.0, /*min=*/0.0, /*max=*/1.0);
/// bar->on_value_changed([](double v) { /* v is already clamped to [0, 1] */ });
/// bar->set_value(0.25);
/// bar->set_show_paused(true); // amber indicator
/// @endcode
class ProgressBarControl final : public Element, private RangeValueProvider
{

    // ---- compositor layers ----
    /// Two layers while the theme's two-bar sweep runs (indeterminate, neither paused nor in
    /// error): the two bars, which the compositor slides across.
    [[nodiscard]] std::size_t layer_count() const override;
    /// Bar `index` at the bar's left end, clipped to the bar.
    [[nodiscard]] detail::LayerPlacement layer_placement(std::size_t index) const override;
    /// Bar `index`, a rounded accent rect 40% (the first) or 60% (the second) of the bar's width.
    void render_layer(std::size_t index, Renderer& renderer, const Theme& theme) const override;
    /// The 2-second sweep of both bars, from where it is.
    void animate_layers(detail::Compositor& compositor, std::span<const VisualId> layers, Duration elapsed) override;
    /// Moves the sweep on by the time the compositor ran it.
    void layers_stopped(Duration elapsed) override;

public:
    /// A determinate bar; `max` below `min` is raised to `min`, and `value` is clamped into the range.
    explicit ProgressBarControl(double value = 0.0, double min = 0.0, double max = 100.0);

    // ---- range/value ----
    /// The value at which the bar is empty; 0 by default.
    [[nodiscard]] double(min)() const
    {
        return min_;
    }
    /// The value at which the bar is full; 100 by default.
    [[nodiscard]] double(max)() const
    {
        return max_;
    }
    /// Sets both ends (`max` below `min` becomes `min`) and re-clamps `value()`, firing
    /// `on_value_changed()` if that moves it. Always marks the bar for redraw.
    void set_range(double min, double max);

    /// The value, always within [`min()`, `max()`]; 0 by default. Ignored while indeterminate.
    [[nodiscard]] double value() const
    {
        return value_;
    }
    /// Clamps into the range (to `min()` for an empty range). Does nothing, and fires no callback,
    /// when the clamped value is unchanged. Otherwise redraws and fires `on_value_changed()`.
    void set_value(double value);

    // ---- indeterminate ----
    /// Whether it shows an endless sweep instead of a filled share; false by default.
    [[nodiscard]] bool is_indeterminate() const
    {
        return indeterminate_;
    }
    /// Redraws only. Turning the sweep on always restarts its phase from the beginning.
    void set_indeterminate(bool indeterminate);

    // ---- paused/error override ----
    /// The indicator in the caution color; false by default.
    [[nodiscard]] bool show_paused() const
    {
        return show_paused_;
    }
    /// Marks the bar for redraw.
    void set_show_paused(bool show_paused);
    /// The indicator in the critical color, winning over `show_paused()`; false by
    /// default.
    [[nodiscard]] bool show_error() const
    {
        return show_error_;
    }
    /// Marks the bar for redraw.
    void set_show_error(bool show_error);

    /// Fired with the new, clamped value whenever `value()` changes. Replaces any
    /// previous handler.
    void on_value_changed(Callback<void(double)> handler)
    {
        on_value_changed_ = std::move(handler);
    }

    // ---- layout ----
    /// True: measuring reads nothing shared.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

    // ---- automation ----
    /// ProgressBar, with a range value that UI Automation can read but not set, since the bar
    /// takes no input.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::ProgressBar;
    }

protected:
    /// 200 DIPs wide and the theme's bar height tall, the width capped at a bounded `available`
    /// width.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;

    // ---- render ----
    /// Draws the track, then the determinate indicator or the two clipped sweep bars.
    void render_override(Renderer& renderer, const RenderContext& context) const override;

    // ---- time ----
    /// Advances the theme's progress animation: the indeterminate sweep while neither paused nor
    /// in error, and a glint over the filled part in themes that draw one.
    void tick_override(Duration delta) override;


private:
    [[nodiscard]] float value_fraction() const;
    // Aero's bar: track, indicator, glint and frame.
    void render_glossy(Renderer& renderer, const Theme& theme) const;
    // The classic eras' bar: a framed well with the indicator in chunks or one block.
    void render_framed(Renderer& renderer, const Theme& theme) const;
    // Whether the glint is crossing the bar: normal progress, or an indeterminate bar.
    [[nodiscard]] bool glinting() const;

    double min_   = 0.0;
    double max_   = 100.0;
    double value_ = 0.0;

    bool   indeterminate_ = false;
    bool   show_paused_   = false;
    bool   show_error_    = false;
    double phase_ms_      = 0.0;
    double glow_ms_       = 0.0; // the glint's place in its pass and rest
    // measure(): whether the theme draws the glossy or the framed bar, which tick_override()
    // cannot ask.
    mutable bool glossy_ = false;
    mutable bool framed_ = false;

    Callback<void(double)> on_value_changed_;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.range_value = this;
    }
    [[nodiscard]] double range_value() const override
    {
        return value_;
    }
    [[nodiscard]] double range_minimum() const override
    {
        return min_;
    }
    [[nodiscard]] double range_maximum() const override
    {
        return max_;
    }
    [[nodiscard]] double range_small_change() const override
    {
        return 0.0;
    }
    [[nodiscard]] bool range_read_only() const override
    {
        return false;
    }
    bool set_range_value(double /*value*/) override
    {
        return false;
    }
};

/// A determinate or indeterminate horizontal progress bar. Builds a ui::ProgressBarControl.
///
/// Example:
/// @code
/// ui::Property<double> progress{0.0};
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::ProgressBar{.value = progress, .width = 200.0f},
///         ui::ProgressBar{.width = 200.0f}, // no value: an indeterminate sweep
///         ui::Button{.text = "Step", .on_click = [&] { progress += 10.0; }},
///     },
/// });
/// @endcode
struct ProgressBar
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ProgressBar>` points at.
    using control_type = ProgressBarControl;

    // ---- properties ----
    /// Value, clamped to [`min`, `max`]; the filled share of the bar. Unset (the default) is an
    /// endless sweep, for work whose length is unknown.
    Binding<std::optional<double>> value{};
    /// The value of the empty bar.
    Binding<double> min{0.0};
    /// The value of the full bar. Below `min`, it is taken as `min`.
    Binding<double> max{100.0};
    /// The indicator in the caution color, and an indeterminate sweep
    /// frozen into a full bar.
    Binding<bool> show_paused{false};
    /// As `show_paused`, in the critical color.
    Binding<bool> show_error{false};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named Style; explicit fields below win over it.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Fixed width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Fixed height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Minimum width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Minimum height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Maximum width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Maximum height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment within the parent's slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment within the parent's slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The new (clamped) value, whenever a bound `value` moves it.
    Callback<void(double)> on_value_changed{};
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<ProgressBar> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::ProgressBarControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
