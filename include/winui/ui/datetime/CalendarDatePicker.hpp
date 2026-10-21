// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/ControlHeader.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/datetime/CalendarView.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace ui
{

/// A field that shows a date and opens a CalendarView under itself to pick one.
///
/// The field shows the date in the short date pattern of regional_date_format() (or its own
/// `date_format`), and the placeholder text while no date is picked. A click, Enter, Space,
/// Alt+Down or F4 opens the calendar. Picking a day sets the date and closes it, clicking the
/// picked day again clears the date and keeps it open, and Escape or a click outside closes it
/// without a change. calendar() is the CalendarView in the popup, for its range, first day of the
/// week and blackout dates.
///
/// `ui::CalendarDatePicker` is the declarative form.
///
/// Example:
/// @code
/// ui::CalendarDatePickerControl picker;
/// picker.set_header("Arrival");
/// picker.calendar().set_date_range(ui::today(), std::nullopt); // nothing in the past
/// picker.on_date_changed([](std::optional<std::chrono::year_month_day> date)
///                        { std::printf("%s\n", date ? "picked" : "cleared"); });
/// @endcode
class CalendarDatePickerControl final : public Element, private InvokeProvider, private ValueProvider
{
public:
    /// An empty picker showing its placeholder.
    CalendarDatePickerControl();
    ~CalendarDatePickerControl() override;
    CalendarDatePickerControl(const CalendarDatePickerControl&)            = delete;
    CalendarDatePickerControl& operator=(const CalendarDatePickerControl&) = delete;

    /// The picked date; unset until one is picked.
    [[nodiscard]] std::optional<std::chrono::year_month_day> date() const
    {
        return date_;
    }
    /// Sets the date, clamped to the calendar's range. Raises the change callback when it differs.
    void set_date(std::optional<std::chrono::year_month_day> date);
    /// Called after the date changed, from the calendar or a setter, with the new date.
    void on_date_changed(Callback<void(std::optional<std::chrono::year_month_day>)> handler)
    {
        on_date_changed_ = std::move(handler);
    }

    /// The label above the field. Empty shows none.
    void set_header(std::string text);
    /// The hint below the field. Empty shows none.
    void set_description(std::string text);
    /// What the field shows while no date is picked; unset is "Pick a date" (ui::strings()).
    void set_placeholder_text(std::optional<std::string> text);
    /// The pattern the date is written with (DateFormatSymbols); unset is the short date.
    void set_date_format(std::optional<std::string> pattern);
    /// The text the field shows now: the date, or the placeholder.
    [[nodiscard]] std::string shown_text() const;

    /// Whether the calendar is open.
    [[nodiscard]] bool is_calendar_open() const
    {
        return open_;
    }
    /// Opens or closes the calendar.
    void set_calendar_open(bool open);
    /// Called after the calendar opened (true) or closed (false).
    void on_calendar_open_changed(Callback<void(bool)> handler)
    {
        on_open_changed_ = std::move(handler);
    }

    /// The calendar the popup shows. It stays a CalendarView in single selection.
    [[nodiscard]] CalendarViewControl& calendar()
    {
        return *calendar_;
    }

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
    void automation_children(std::vector<Element*>& out) const override;

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    bool               pointer_override(const PointerEvent& e) override;
    bool               key_override(const KeyEvent& e) override;
    void               enabled_changed() override;
    void               tick_override(Duration delta) override;

private:
    void sync_popup();

    std::optional<std::chrono::year_month_day>                 date_;
    std::optional<std::string>                                 placeholder_;
    std::optional<std::string>                                 format_;
    bool                                                       open_    = false;
    bool                                                       hovered_ = false;
    bool                                                       pressed_ = false;
    bool                                                       syncing_ = false;
    ControlHeader                                              header_{8.0f};
    ControlDescription                                         description_;
    std::unique_ptr<CalendarViewControl>                       calendar_;
    Callback<void(std::optional<std::chrono::year_month_day>)> on_date_changed_;
    Callback<void(bool)>                                       on_open_changed_;
    Subscription                                               format_changed_;
    Subscription                                               strings_changed_;
    mutable float                                              glyph_column_ = 32.0f; // set by measure_override()

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
        set_calendar_open(!open_);
        return true;
    }
    [[nodiscard]] std::string text_value() const override
    {
        return shown_text();
    }
    [[nodiscard]] bool text_value_read_only() const override
    {
        return true;
    }
    bool set_text_value(std::string_view) override
    {
        return false;
    }
};

/// The declarative form of CalendarDatePickerControl: a date field with a calendar.
///
/// Example:
/// @code
/// ui::Property<std::optional<std::chrono::year_month_day>> arrival;
///
/// window.set_content(ui::CalendarDatePicker{
///     .header           = "Arrival",
///     .placeholder_text = "Pick a date",
///     .date             = arrival,
///     .min_date         = ui::today(),
/// });
/// @endcode
struct CalendarDatePicker
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<CalendarDatePicker>` points at.
    using control_type = CalendarDatePickerControl;

    // ---- properties ----
    /// The label above the field. Empty shows none.
    Binding<std::string> header{};
    /// The hint below the field. Empty shows none.
    Binding<std::string> description{};
    /// What the field shows while no date is picked; unset is "Pick a date".
    Binding<std::optional<std::string>> placeholder_text{};
    /// The picked date; unset until one is picked.
    TwoWayField<std::optional<std::chrono::year_month_day>> date{};
    /// The first date the calendar offers; unset is January 1, 100 years before today.
    Binding<std::optional<std::chrono::year_month_day>> min_date{};
    /// The last date the calendar offers; unset is December 31, 100 years after today.
    Binding<std::optional<std::chrono::year_month_day>> max_date{};
    /// The pattern the field writes the date with (DateFormatSymbols); unset is the short date.
    Binding<std::optional<std::string>> date_format{};
    /// The day the calendar's weeks start on; unset follows regional_date_format().
    Binding<std::optional<std::chrono::weekday>> first_day_of_week{};
    /// Whether the calendar draws today highlighted.
    Binding<bool> today_highlighted{true};
    /// Whether the calendar dims the neighboring months' days.
    Binding<bool> out_of_scope_enabled{true};
    /// Whether the calendar names the month on its first day.
    Binding<bool> group_label_visible{false};
    /// Whether the calendar is open.
    TwoWayField<bool> is_calendar_open{false};

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
    /// The new date (unset when cleared), picked in the calendar. A bound value doesn't run it.
    Callback<void(std::optional<std::chrono::year_month_day>)> on_date_changed{};
    /// The calendar opened.
    Action on_opened{};
    /// The calendar closed.
    Action on_closed{};
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
    ControlOut<CalendarDatePicker> control{}; ///< Out: the handle that receives the built control.

    // ---- templates ----
    /// Marks dates the calendar won't let be picked; unset blocks none.
    Callback<bool(std::chrono::year_month_day)> is_blackout{};

    /// Consumes the spec and returns the built ui::CalendarDatePickerControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
