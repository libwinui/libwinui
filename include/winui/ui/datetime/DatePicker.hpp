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

/// A field that shows a date in its parts (day, month and year, in the order the locale writes
/// them) and opens a flyout of three spinning columns to pick one.
///
/// The flyout opens on the date, or on today while there is none. A click, Enter, Space or
/// Alt+Down opens it. In the flyout the arrow keys, the wheel or a click on a row turn a column,
/// Left and Right move between the columns, and Enter or the check mark button keeps the
/// choice. Escape, a click outside or the cross drops it. Turning the month or the year keeps
/// the day where it fits: January 31 becomes February 28.
///
/// `ui::DatePicker` is the declarative form.
///
/// Example:
/// @code
/// ui::DatePickerControl picker;
/// picker.set_header("Birthday");
/// picker.set_year_range(1900, 2026);
/// picker.on_selected_date_changed([](std::optional<std::chrono::year_month_day> date)
///                                 { std::printf("%s\n", date ? "set" : "cleared"); });
/// @endcode
class DatePickerControl final : public Element, private InvokeProvider, private ValueProvider
{
public:
    /// A picker with no date, its years 100 back and ahead of today.
    DatePickerControl();
    ~DatePickerControl() override;
    DatePickerControl(const DatePickerControl&)            = delete;
    DatePickerControl& operator=(const DatePickerControl&) = delete;

    /// The picked date; unset until one is picked.
    [[nodiscard]] std::optional<std::chrono::year_month_day> selected_date() const
    {
        return date_;
    }
    /// Sets the date, clamped to the years. Raises the change callback when it differs.
    void set_selected_date(std::optional<std::chrono::year_month_day> date);
    /// Called after the date changed, from the flyout or a setter, with the new date.
    void on_selected_date_changed(Callback<void(std::optional<std::chrono::year_month_day>)> handler)
    {
        on_selected_date_changed_ = std::move(handler);
    }

    /// The first year offered; 100 years before today until set.
    [[nodiscard]] int min_year() const;
    /// The last year offered; 100 years after today until set.
    [[nodiscard]] int max_year() const;
    /// Sets the years offered; unset follows today. A date outside them is clamped.
    void set_year_range(std::optional<int> min, std::optional<int> max);

    /// Which parts the field and the flyout show; all three by default.
    void set_parts_visible(bool day, bool month, bool year);
    /// The label above the field. Empty shows none.
    void set_header(std::string text);

    /// Whether the flyout is open.
    [[nodiscard]] bool is_open() const;
    /// Opens or closes the picker's drop-down.
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
    friend struct DatePicker;

    [[nodiscard]] std::chrono::year_month_day clamp(std::chrono::year_month_day date) const;

    std::optional<std::chrono::year_month_day>                 date_;
    std::optional<int>                                         min_year_;
    std::optional<int>                                         max_year_;
    bool                                                       day_visible_   = true;
    bool                                                       month_visible_ = true;
    bool                                                       year_visible_  = true;
    ControlHeader                                              header_{4.0f};
    std::unique_ptr<Model>                                     model_;
    std::unique_ptr<detail::PickerEngine>                      engine_;
    Callback<void(std::optional<std::chrono::year_month_day>)> on_selected_date_changed_;
    Subscription                                               format_changed_;
    Subscription                                               strings_changed_;

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

/// The declarative form of DatePickerControl: a date in its parts, picked from spinning columns.
///
/// Example:
/// @code
/// ui::Property<std::optional<std::chrono::year_month_day>> birthday;
///
/// window.set_content(ui::DatePicker{
///     .header        = "Birthday",
///     .selected_date = birthday,
///     .min_year      = 1900,
/// });
/// @endcode
struct DatePicker
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<DatePicker>` points at.
    using control_type = DatePickerControl;

    // ---- properties ----
    /// The label above the field. Empty shows none.
    Binding<std::string> header{};
    /// The picked date; unset until one is picked.
    TwoWayField<std::optional<std::chrono::year_month_day>> selected_date{};
    /// The first year offered; unset is 100 years before today.
    Binding<std::optional<int>> min_year{};
    /// The last year offered; unset is 100 years after today.
    Binding<std::optional<int>> max_year{};
    /// Whether the day shows.
    Binding<bool> day_visible{true};
    /// Whether the month shows.
    Binding<bool> month_visible{true};
    /// Whether the year shows.
    Binding<bool> year_visible{true};

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
    /// The new date (unset when cleared), from the flyout. A bound value doesn't run it.
    Callback<void(std::optional<std::chrono::year_month_day>)> on_selected_date_changed{};
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
    ControlOut<DatePicker> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::DatePickerControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
