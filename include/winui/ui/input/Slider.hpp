// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/ControlHeader.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ui
{

/// Where tick marks are drawn relative to the track. No placement draws ticks until
/// tick_frequency() is above zero, except under the classic look. The classic look only tells
/// None (no ticks) from the rest, which all draw its one row of ticks on the far side (see
/// set_tick_frequency()).
enum class SliderTickPlacement
{
    None,        ///< no ticks
    TopLeft,     ///< above a horizontal track, left of a vertical one
    BottomRight, ///< below a horizontal track, right of a vertical one
    Outside,     ///< both of the above
    Inline,      ///< notches cut across the track itself (the default)
};

/// What a pointer drag lands on.
enum class SliderSnapsTo
{
    StepValues, ///< multiples of step_frequency()
    Ticks,      ///< multiples of tick_frequency(); the arrow keys move by it too
};

/// A thumb dragged along a track to pick a value in [min(), max()].
///
/// A click on the track jumps the thumb there and starts a drag; the drag snaps to
/// step_frequency() (or the ticks, see set_snaps_to()). The arrow keys move by step(), Home and
/// End jump to the ends, and on_value_changed() fires live on every change. An optional header
/// sits above the track. The declarative form is ui::Slider.
///
/// Example:
/// @code
/// auto  panel  = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* label  = panel->add(std::make_unique<ui::TextControl>("50"));
/// auto* volume = panel->add(std::make_unique<ui::SliderControl>(ui::Orientation::Horizontal,
///                                                           0.0f, 100.0f, 50.0f));
/// volume->set_header("Volume");
/// volume->set_step(5.0f);
/// volume->set_tick_frequency(10.0f);
/// volume->set_tick_placement(ui::SliderTickPlacement::BottomRight);
/// volume->on_value_changed([label](double v) { label->set_text(std::format("{:.0f}", v)); });
/// window.set_content(std::move(panel));
/// @endcode
class SliderControl final : public Element, private RangeValueProvider
{

    // ---- composition ----
    /// Starts easing the thumb's inner circle toward its rest, hover or pressed size. Does
    /// nothing under the retro looks.
    void sync_composition_override(detail::Compositor& compositor, const Theme& theme) override;
    /// Advances the inner circle's resize animation.
    void tick_override(Duration delta) override;

public:
    /// A slider over [min, max] starting at `value` (clamped into the range). Raises nothing.
    explicit SliderControl(Orientation orientation = Orientation::Horizontal,
                           double      min         = 0.0,
                           double      max         = 100.0,
                           double      value       = 0.0);

    /// The orientation given at construction; there is no setter.
    [[nodiscard]] Orientation orientation() const
    {
        return orientation_;
    }

    /// The header text; empty when none is shown.
    [[nodiscard]] const std::string& header() const
    {
        return header_.text();
    }
    /// The label above the slider. Empty draws none.
    void set_header(std::string header)
    {
        if (header_.set_text(std::move(header)))
        {
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }

    /// The value at the start of the track.
    [[nodiscard]] double(min)() const
    {
        return min_;
    }
    /// The value at the end of the track; never below min().
    [[nodiscard]] double(max)() const
    {
        return max_;
    }
    /// Sets the range and clamps the current value into it, firing on_value_changed if that
    /// moves it. A `max` below `min` is taken as `min`.
    void set_range(double min, double max);

    /// Value, always within [min(), max()].
    [[nodiscard]] double value() const
    {
        return value_;
    }
    /// Clamps to [min, max]. Fires on_value_changed only if the clamped value differs from the
    /// current one.
    void set_value(double value);

    /// The arrow-key increment, 1 by default. Also UI Automation's small change.
    [[nodiscard]] double step() const
    {
        return step_;
    }
    /// The amount each arrow-key press moves the value by. Zero disables keyboard stepping
    /// (Home/End still jump to min/max). Negative is 0.
    void set_step(double step)
    {
        step_ = (std::max)(0.0, step);
    }

    /// The tick interval in value units; 0 (the default) means none is set.
    [[nodiscard]] double tick_frequency() const
    {
        return tick_frequency_;
    }
    /// How often a tick mark is drawn along the track, in value units. Ticks sit at whole
    /// multiples of it counted from min(), one at min() and one per full interval after, so a
    /// range the frequency does not divide gets no tick at max(). Ticks closer than 20 DIPs are
    /// thinned to every n-th one.
    ///
    /// At zero there are no ticks, though some themes still mark the two ends of the range.
    /// Negative is 0. Redraws only.
    void set_tick_frequency(double frequency)
    {
        tick_frequency_ = (std::max)(0.0, frequency);
        mark_dirty(DirtyFlags::Visual);
    }

    /// Where the ticks go relative to the track; Inline by default.
    [[nodiscard]] SliderTickPlacement tick_placement() const
    {
        return tick_placement_;
    }
    /// Redraws only.
    void set_tick_placement(SliderTickPlacement placement)
    {
        tick_placement_ = placement;
        mark_dirty(DirtyFlags::Visual);
    }

    /// The drag snapping interval, 1 by default.
    [[nodiscard]] double step_frequency() const
    {
        return step_frequency_;
    }
    /// The interval a pointer click or drag snaps to under SliderSnapsTo::StepValues.
    /// It is 1 by default, so a drag lands on whole values. Give a range narrower than a few
    /// units a finer step. Zero makes the drag continuous. Snapping lands on whole multiples of
    /// the interval counted from 0, not from min(), clamped into the range. Keyboard stepping
    /// uses step() and is unaffected. set_value() never snaps.
    void set_step_frequency(double frequency)
    {
        step_frequency_ = (std::max)(0.0, frequency);
    }

    /// What a drag snaps to; StepValues by default.
    [[nodiscard]] SliderSnapsTo snaps_to() const
    {
        return snaps_to_;
    }
    /// SliderSnapsTo::Ticks snaps a drag to tick_frequency() instead and makes the arrow keys move by
    /// it, ignoring step(). With no tick frequency set it behaves as StepValues.
    void set_snaps_to(SliderSnapsTo snaps_to)
    {
        snaps_to_ = snaps_to;
    }

    /// Reverses the direction: min at the right of a horizontal slider and at the top of a
    /// vertical one, with the value fill, the ticks, a click, a drag and the arrow keys all
    /// following (Left and Down then increase, Right and Up decrease). False by default.
    [[nodiscard]] bool direction_reversed() const
    {
        return direction_reversed_;
    }
    /// Redraws; the value itself does not change.
    void set_direction_reversed(bool reversed)
    {
        direction_reversed_ = reversed;
        mark_dirty(DirtyFlags::Visual);
    }


    /// Draws the whole track with `gradient`, in place of the neutral track and the accent fill
    /// up to the thumb. `checkerboard` draws a 50% checkerboard (Pattern::Checkerboard50) behind
    /// the gradient first, so a channel whose stops carry partial alpha looks translucent.
    /// std::nullopt restores the default track. The retro (95/98/2000) bevel track ignores the
    /// gradient.
    void set_track_gradient(std::optional<LinearGradientBrush> gradient, bool checkerboard = false)
    {
        track_gradient_              = std::move(gradient);
        track_gradient_checkerboard_ = checkerboard;
        mark_dirty(DirtyFlags::Visual);
    }

    /// Fired whenever `value` changes, from a drag, a click-to-jump, or a keyboard step
    /// alike. A drag fires on every change, not only on release.
    void on_value_changed(Callback<void(double)> handler)
    {
        on_value_changed_ = std::move(handler);
    }

    // ---- layout ----
    /// True: measuring reads only the slider's own state, the measurer and the theme.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

    // ---- focus ----
    /// Whether the slider holds keyboard focus (the focus ring is drawn).
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// A tab stop while enabled.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }

    /// Whether the pointer is over the slider (false while disabled).
    [[nodiscard]] bool hovered() const
    {
        return hovered_;
    }
    /// Whether a pointer drag is in progress.
    [[nodiscard]] bool dragging() const
    {
        return dragging_;
    }

    // ---- derived visual state ----

protected:
    friend struct testing::VisualStates; // the tests read the state a control draws in
    /// The state the thumb is drawn in: Disabled, then Pressed while dragging, then Hover, else
    /// Normal.
    [[nodiscard]] ButtonState visual_state() const;
    /// The header, when set, is stacked above the track (ControlHeader). The track asks for 200
    /// DIPs along its axis (less if less is offered) and at least 32 across it.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places the header; the track takes the rest of the bounds.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    /// Press on the track jumps the value there and starts a drag; moves while dragging update
    /// the value (snapped per snaps_to()); release ends the drag.
    bool pointer_override(const PointerEvent& e) override;

    // ---- render ----
    /// Draws the header, the track (or set_track_gradient()'s gradient), the value fill, the
    /// ticks and the thumb, or the classic track and thumb under a retro theme.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


public:
    /// Enabled and dragging.
    [[nodiscard]] bool is_pressed() const;

    // ---- automation ----
    /// Slider. The header names it when set. Without a header, set accessibility().name, as
    /// for an icon-only Button.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Slider;
    }
    /// The header text.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return header_.text();
    }

private:
    // Up/Right increase and Down/Left decrease, regardless of orientation (reversed by
    // direction_reversed()). Claimed so Window's directional focus navigation leaves them
    // alone (Element::key_override).
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowUp || key == Key::ArrowDown || key == Key::ArrowLeft || key == Key::ArrowRight;
    }
    [[nodiscard]] bool horizontal() const
    {
        return orientation_ == Orientation::Horizontal;
    }
    [[nodiscard]] float  value_fraction() const;
    [[nodiscard]] double position_to_value(Point p) const;
    [[nodiscard]] double snapped_position_value(Point p) const;
    // The multiple of `interval` nearest `value`, clamped to the range.
    [[nodiscard]] double closest_step(double interval, double value) const;
    [[nodiscard]] Point  thumb_center() const;
    [[nodiscard]] Rect   track_bounds(const Theme& theme) const;
    [[nodiscard]] Rect   filled_bounds(const Rect& track, Point thumb_c) const;
    [[nodiscard]] float  thumb_diameter(const Theme& theme, ButtonState state) const;
    // Whether the focus visual rings the whole track container (the concentric-circle thumb under
    // an outward ring) rather than the thumb, and that container: the track's row, the length of
    // the control.
    [[nodiscard]] bool rings_whole_control(const Theme& theme) const;
    [[nodiscard]] Rect focus_container(const Theme& theme) const;
    void               render_classic(Renderer& renderer, const Theme& theme) const;
    // `gap` is where the outside tick rows start from the track's edge, `length` how long a
    // tick is. Fluent uses 4 and 4, Aero its thumb-row edge and 4 for ButtonBlock.
    void render_ticks(Renderer&    renderer,
                      const Theme& theme,
                      const Rect&  track,
                      const Color& outside_fill,
                      float        gap    = 4.0f,
                      float        length = 4.0f) const;
    // Points the block toward the ticks when they sit on one side only (`ControlChrome::slider_block`).
    void render_block(Renderer& renderer, const Theme& theme) const;

    Orientation   orientation_;
    ControlHeader header_{4.0f}; // 4 DIPs between the header and the track
    double        min_ = 0.0, max_ = 100.0;
    double        value_ = 0.0;
    double        step_  = 1.0;

    bool hovered_  = false;
    bool dragging_ = false;

    double              tick_frequency_     = 0.0; // see set_tick_frequency(): what 0 means depends on the look
    SliderTickPlacement tick_placement_     = SliderTickPlacement::Inline;
    double              step_frequency_     = 1.0; // the default; 0 makes a drag continuous
    SliderSnapsTo       snaps_to_           = SliderSnapsTo::StepValues;
    bool                direction_reversed_ = false;

    // From the left of a horizontal track and the bottom of a vertical one. The identity
    // unless the direction is reversed.
    [[nodiscard]] float travel_fraction(float value_fraction) const
    {
        return direction_reversed_ ? 1.0f - value_fraction : value_fraction;
    }

    // Cached by measure_override() because handle_pointer() gets no Theme. Nonzero only for the
    // classic look, whose thumb travels inside its channel instead of across the full width.
    mutable float travel_inset_ = 0.0f;

    std::optional<LinearGradientBrush> track_gradient_;
    bool                               track_gradient_checkerboard_ = false;

    // The thumb's inner circle size, eased between the state sizes; its first target only records.
    detail::RectTween inner_tween_;

    Callback<void(double)> on_value_changed_;

protected:
    void enabled_changed() override;
    /// Called by Window as focus moves; redraws.
    void focus_changed(bool focused) override;
    /// Arrow keys step by step() (or the tick frequency under SliderSnapsTo::Ticks), Home/End jump to
    /// min()/max().
    bool key_override(const KeyEvent& e) override;

protected:
    /// Around the thumb, the track's row or the whole control, as the theme places it. None where
    /// focus shows on the thumb's face.
    [[nodiscard]] Rect  focus_ring_rect(const Theme& theme) const override;
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.range_value = this;
    }
    // value().
    [[nodiscard]] double range_value() const override
    {
        return value_;
    }
    // min().
    [[nodiscard]] double range_minimum() const override
    {
        return min_;
    }
    // max().
    [[nodiscard]] double range_maximum() const override
    {
        return max_;
    }
    // step().
    [[nodiscard]] double range_small_change() const override
    {
        return step_;
    }
    // UI Automation's SetValue: set_value() (clamped, raising on_value_changed()); refused while
    // disabled.
    bool set_range_value(double value) override
    {
        if (!is_enabled())
        {
            return false;
        }
        set_value(value);
        return true;
    }
    [[nodiscard]] bool range_read_only() const override
    {
        return false;
    }
};

/// A draggable thumb over a continuous range. Builds a ui::SliderControl.
///
/// `orientation`, the range and the initial value go to the control's constructor, so the value
/// is clamped against the right range from the start and seeding it raises nothing.
///
/// Example:
/// @code
/// ui::Property<double> volume{40.0};
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::Slider{.header           = "Volume",
///                    .value            = volume,
///                    .max              = 100.0,
///                    .step             = 5.0,
///                    .tick_frequency   = 10.0,
///                    .tick_placement   = ui::SliderTickPlacement::BottomRight,
///                    .width            = 240.0f},
///         ui::Text{.text = ui::bind(volume, [](double v) { return std::format("{:.0f}%", v); })},
///     },
/// });
/// @endcode
struct Slider
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Slider>` points at.
    using control_type = SliderControl;

    // ---- properties ----
    /// The label above the control. Empty draws none.
    Binding<std::string> header{};
    /// Value, clamped to [`min`, `max`]. `ui::bind(prop)` is two-way: dragging writes the property.
    TwoWayField<double> value{0.0};
    /// Minimum, the value at the start of the track. A bound range that moves past the value
    /// clamps it, and a two-way `value` follows.
    Binding<double> min{0.0};
    /// Maximum, the value at the end of the track. Below `min`, it is taken as `min`.
    Binding<double> max{100.0};
    /// What an arrow key adds or takes away; 0 turns arrow stepping off.
    Binding<double> step{1.0};
    /// See ui::SliderControl::set_tick_frequency: 0 draws no ticks, except under the classic look.
    Binding<double> tick_frequency{0.0};
    /// Where the ticks go relative to the track; Inline (across it) by default.
    Binding<SliderTickPlacement> tick_placement{SliderTickPlacement::Inline};
    /// What a drag snaps to: 1 by default, 0 for a continuous drag. `step` stays the arrow-key
    /// increment.
    Binding<double> step_frequency{1.0};
    /// A drag lands on `step_frequency` multiples, or on ticks (which the arrow keys
    /// then move by too).
    Binding<SliderSnapsTo> snaps_to{SliderSnapsTo::StepValues};
    // Everything about the value (range, step, ticks, snapping) comes first, as one group, and a
    // new field of that kind goes at its end, right above this line.
    /// Horizontal or Vertical; fixed at build time (the control has no setter for it).
    Orientation orientation = Orientation::Horizontal;
    /// Min at the right (horizontal) or the top (vertical).
    Binding<bool> direction_reversed{false};

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
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The new value, live on every change during a drag, click or key press. A bound value doesn't
    /// run it.
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
    ControlOut<Slider> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::SliderControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
