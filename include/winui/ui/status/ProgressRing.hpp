// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
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

/// Circular progress, by default an indeterminate spinner.
///
/// Indeterminate (the default), a three-quarter arc rotates continuously. Determinate, an accent
/// arc sweeps clockwise from the top over the fraction of
/// [`min()`, `max()`] that `value()` has reached. The ring is 32 DIPs across by default (at
/// least 16) with a 4 DIP stroke, and draws no track unless a `background()` is set.
/// `set_active(false)` hides it entirely. It takes no input and has no disabled state.
/// `ui::ProgressRing` is the declarative form.
///
/// Example:
/// @code
/// ui::Property<bool> working{true};
///
/// window.set_content(ui::HorizontalStack{
///     .spacing  = 12,
///     .content  = {
///         ui::ProgressRing{.active = working, .width = 32.0f},
///         ui::Button{.text = "Stop", .on_click = [&] { working = false; }},
///     },
/// });
///
/// // A determinate gauge with a visible track:
/// auto ring = std::make_unique<ui::ProgressRingControl>(/*value=*/40.0);
/// ring->set_indeterminate(false);
/// ring->set_background(ui::Color::from_argb(0x33808080));
/// ring->set_value(75.0);
/// @endcode
class ProgressRingControl final : public Element, private RangeValueProvider
{

    // ---- compositor layers ----
    /// One layer while active and indeterminate: the arc, which the compositor turns.
    [[nodiscard]] std::size_t layer_count() const override;
    /// The square the ring is drawn in, centered in bounds().
    [[nodiscard]] detail::LayerPlacement layer_placement(std::size_t index) const override;
    /// The three-quarter arc at the start of its turn, from the top clockwise.
    void render_layer(std::size_t index, Renderer& renderer, const Theme& theme) const override;
    /// A clockwise turn of the arc every 1.5 s around the ring's center, from where the rotation is.
    void animate_layers(detail::Compositor& compositor, std::span<const VisualId> layers, Duration elapsed) override;
    /// Moves the rotation on by the time the compositor turned it.
    void layers_stopped(Duration elapsed) override;

public:
    /// An active, indeterminate ring; `max` below `min` is raised to `min` and `value` is
    /// clamped into the range. Sets a 16x16 DIP minimum size.
    explicit ProgressRingControl(double value = 0.0, double min = 0.0, double max = 100.0);

    // ---- range/value ----
    /// The value with no arc; 0 by default.
    [[nodiscard]] double(min)() const
    {
        return min_;
    }
    /// The value drawing the full circle; 100 by default.
    [[nodiscard]] double(max)() const
    {
        return max_;
    }
    /// Sets both ends (`max` below `min` becomes `min`) and re-clamps `value()`, firing
    /// `on_value_changed()` if that moves it. Always marks the ring for redraw.
    void set_range(double min, double max);

    /// The value, always within [`min()`, `max()`]; 0 by default. Drawn only while determinate.
    [[nodiscard]] double value() const
    {
        return value_;
    }
    /// Clamps into the range. Does nothing, and fires no callback, when the clamped value is
    /// unchanged. Otherwise redraws and fires `on_value_changed()`.
    void set_value(double value);

    // ---- indeterminate / active ----
    /// Whether it spins endlessly instead of showing a share; true by default.
    [[nodiscard]] bool is_indeterminate() const
    {
        return indeterminate_;
    }
    /// Turning it on restarts the rotation from the top. Marks the ring for redraw.
    void set_indeterminate(bool indeterminate);

    /// Whether the ring shows; true by default. An inactive ring draws nothing but keeps its layout size.
    [[nodiscard]] bool is_active() const
    {
        return active_;
    }
    /// Shows or hides the ring. Redraws only.
    void set_active(bool active);

    /// The fixed fill of the track circle. nullopt (the default) draws no track, unless a theme
    /// resource is set.
    [[nodiscard]] const std::optional<Color>& background() const
    {
        return background_;
    }
    /// The track fill as a theme resource, or nullptr.
    [[nodiscard]] ThemeColor background_resource() const
    {
        return background_resource_;
    }
    /// Sets the fixed track fill and clears a theme-resource one. Marks the ring for redraw when
    /// the color changes.
    void set_background(std::optional<Color> background);
    /// Sets the track fill to a theme resource, re-read under every theme, and clears a fixed one.
    void set_background(ThemeColor resource);

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
    /// ProgressBar, with a range value that UI Automation can read but not set.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::ProgressBar;
    }

protected:
    /// 32 x 32 DIPs, capped at a bounded `available` size.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;

    // ---- render ----
    /// Draws the optional background circle, then the arc as one stroked path with round ends.
    void render_override(Renderer& renderer, const RenderContext& context) const override;

    // ---- time ----
    /// Advances the indeterminate arc's rotation (a 1.5 s turn) and redraws while active and
    /// indeterminate. Ignored otherwise, and while the arc is animated without redrawing.
    void tick_override(Duration delta) override;


private:
    [[nodiscard]] float value_fraction() const;
    [[nodiscard]] Color arc_color(const Theme& theme) const;

    double min_   = 0.0;
    double max_   = 100.0;
    double value_ = 0.0;

    bool                 indeterminate_ = true;
    bool                 active_        = true;
    std::optional<Color> background_{};
    ThemeColor           background_resource_ = nullptr;

    double phase_ms_ = 0.0;

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

/// The circular counterpart of ui::ProgressBar. Builds a ui::ProgressRingControl.
///
/// Example:
/// @code
/// ui::Property<bool> working{false};
///
/// window.set_content(ui::HorizontalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::ProgressRing{.active = working, .width = 32.0f, .height = 32.0f},
///         ui::ProgressRing{.value = 70.0, .width = 32.0f, .height = 32.0f},
///         ui::ToggleSwitch{.header = "Working"},
///     },
/// });
/// @endcode
struct ProgressRing
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ProgressRing>` points at.
    using control_type = ProgressRingControl;

    // ---- properties ----
    /// Value, clamped to [`min`, `max`]: the arc. Unset (the default) is the indeterminate
    /// spinner, for work whose length is unknown.
    Binding<std::optional<double>> value{};
    /// The value with no arc.
    Binding<double> min{0.0};
    /// The value drawing the full circle. Below `min`, it is taken as `min`.
    Binding<double> max{100.0};
    /// False draws nothing but keeps the ring's layout size.
    Binding<bool> active{true};
    /// A full track ring under the arc: a fixed color or a theme resource, which follows theme
    /// switches. Unset draws no track.
    Binding<Paint> background{};

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
    ControlOut<ProgressRing> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::ProgressRingControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
