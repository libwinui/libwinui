// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/ControlHeader.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace ui
{

namespace detail
{
class PickerEngine;
} // namespace detail

/// Which clock a TimePicker shows.
enum class TimePickerClock
{
    TwelveHour,     ///< 12 hours and an AM/PM column.
    TwentyFourHour, ///< 0 to 23 hours.
};

/// A field that shows a time of day in its parts (hour, minute and, on a 12-hour clock, AM or
/// PM) and opens a flyout of spinning columns to pick one.
///
/// It works like DatePicker: the flyout opens on the time, or on now while there is none, and
/// Enter or the check mark keeps the choice. `minute_increment` sets the minutes offered: 15
/// offers 00, 15, 30 and 45, and a time set in between is rounded down to one of them. The clock
/// follows the short time pattern of regional_date_format() unless the picker sets its own.
///
/// `ui::TimePicker` is the declarative form.
///
/// Example:
/// @code
/// ui::TimePickerControl picker;
/// picker.set_header("Alarm");
/// picker.set_minute_increment(5);
/// picker.set_selected_time(std::chrono::hours{7} + std::chrono::minutes{30});
/// @endcode
class TimePickerControl final : public Element, private InvokeProvider, private ValueProvider
{
public:
    /// A picker with no time, on the regional clock, offering every minute.
    TimePickerControl();
    ~TimePickerControl() override;
    TimePickerControl(const TimePickerControl&)            = delete;
    TimePickerControl& operator=(const TimePickerControl&) = delete;

    /// The picked time of day, from midnight; unset until one is picked.
    [[nodiscard]] std::optional<std::chrono::minutes> selected_time() const
    {
        return time_;
    }
    /// Sets the time: whole days are dropped and the minutes rounded down to the increment.
    /// Raises the change callback when it differs.
    void set_selected_time(std::optional<std::chrono::minutes> time);
    /// Called after the time changed, from the flyout or a setter, with the new time.
    void on_selected_time_changed(Callback<void(std::optional<std::chrono::minutes>)> handler)
    {
        on_selected_time_changed_ = std::move(handler);
    }

    /// The clock shown: the regional one until set.
    [[nodiscard]] TimePickerClock clock() const;
    /// See clock().
    void set_clock(std::optional<TimePickerClock> clock);
    /// The minutes offered step by this many, 1 to 59; 0 offers only the full hour.
    [[nodiscard]] int minute_increment() const
    {
        return increment_;
    }
    /// See minute_increment(). A value outside 0 to 59 is ignored.
    void set_minute_increment(int increment);
    /// The label above the field. Empty shows none.
    void set_header(std::string text);

    /// Whether the flyout is open.
    [[nodiscard]] bool is_open() const;
    /// Opens or closes the flyout.
    void set_open(bool open);

    // ---- Element ----
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Button;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return header_.text();
    }
    void for_each_child(ChildVisitor visit) const override;
    void collect_focusables(std::vector<Element*>& out) override;
    void automation_children(std::vector<Element*>& out) const override;

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    bool               pointer_override(const PointerEvent& e) override;
    bool               key_override(const KeyEvent& e) override;
    bool               text_override(const TextInputEvent& e) override;
    void               tick_override(Duration delta) override;
    void               enabled_changed() override;
    void               focus_changed(bool focused) override;

private:
    class Model;
    friend class Model;
    friend struct TimePicker;

    [[nodiscard]] std::chrono::minutes coerce(std::chrono::minutes time) const;

    std::optional<std::chrono::minutes>                 time_;
    std::optional<TimePickerClock>                      clock_;
    int                                                 increment_ = 1;
    ControlHeader                                       header_{4.0f};
    std::unique_ptr<Model>                              model_;
    std::unique_ptr<detail::PickerEngine>               engine_;
    Callback<void(std::optional<std::chrono::minutes>)> on_selected_time_changed_;
    Subscription                                        format_changed_;
    Subscription                                        strings_changed_;

    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.invoke = this;
        out.value  = this;
    }
    bool invoke() override
    {
        if (!is_enabled())
        {
            return false;
        }
        set_open(!is_open());
        return true;
    }
    [[nodiscard]] std::string text_value() const override;
    [[nodiscard]] bool        text_value_read_only() const override
    {
        return true;
    }
    bool set_text_value(std::string_view) override
    {
        return false;
    }
};

/// The declarative form of TimePickerControl: a time of day picked from spinning columns.
///
/// Example:
/// @code
/// ui::Property<std::optional<std::chrono::minutes>> arrival;
///
/// window.set_content(ui::TimePicker{
///     .header           = "Arrival time",
///     .selected_time    = arrival,
///     .minute_increment = 15,
/// });
/// @endcode
struct TimePicker
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<TimePicker>` points at.
    using control_type = TimePickerControl;

    // ---- properties ----
    /// The label above the field. Empty shows none.
    Binding<std::string> header{};
    /// The picked time of day, from midnight; unset until one is picked.
    TwoWayField<std::optional<std::chrono::minutes>> selected_time{};
    /// The clock shown; unset follows regional_date_format().
    Binding<std::optional<TimePickerClock>> clock{};
    /// The step of the minutes offered, 1 to 59; 0 offers only the full hour.
    Binding<int> minute_increment{1};

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
    /// The new time (unset when cleared), from the flyout. A bound value doesn't run it.
    Callback<void(std::optional<std::chrono::minutes>)> on_selected_time_changed{};
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control; ui::Handled stops it there (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<TimePicker> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::TimePickerControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
