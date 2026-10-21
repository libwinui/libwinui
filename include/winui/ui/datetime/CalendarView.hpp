// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <cstddef>

namespace ui
{

struct Theme;

class MenuFlyoutControl;
class ComboBoxControl;

namespace detail
{
class CalendarPart;
class CalendarCellAutomation;
class CalendarYearSpin;
class PickerEngine;
} // namespace detail

/// How many dates a CalendarView lets the user select.
enum class CalendarViewSelectionMode
{
    None,     ///< Days can't be selected; the calendar only shows dates.
    Single,   ///< One date at a time: a click replaces the selection (the default).
    Multiple, ///< Any number of dates: a click adds or removes one.
};

/// What a CalendarView shows.
enum class CalendarViewDisplayMode
{
    Month,  ///< The days of one month, six weeks at a time (the default).
    Year,   ///< The months of one year.
    Decade, ///< The years of one decade.
};

/// A month calendar: the days of one month in a grid of weeks, with the month's name above them
/// and buttons to page through months.
///
/// Clicking a day selects it, and clicking a selected day removes it from the selection.
/// Clicking the heading zooms out to the months of the year and then to the years of the decade,
/// and clicking a month or a year zooms back in. The days of the neighboring months fill the
/// first and last weeks, in a dimmed color when `out_of_scope_enabled` is true.
///
/// The names of the months and the days and the first day of the week come from
/// regional_date_format() unless the calendar sets its own.
///
/// Keyboard: the arrow keys move between days, Home and End go to the first and the last day of
/// the month, PageUp and PageDown move by a month, Ctrl+Up zooms out and Ctrl+Down zooms in, and
/// Enter or Space select the focused day or zoom into the focused month or year. The heading,
/// the two paging buttons and the grid are four tab stops.
///
/// `ui::CalendarView` is the declarative form.
///
/// Example:
/// @code
/// ui::CalendarViewControl calendar;
/// calendar.set_selection_mode(ui::CalendarViewSelectionMode::Multiple);
/// calendar.on_selected_dates_changed([](const std::vector<std::chrono::year_month_day>& dates)
///                                    { std::printf("%zu days picked\n", dates.size()); });
/// @endcode
class CalendarViewControl final :
    public Element,
    private GridProvider,
    private TableProvider,
    private SelectionProvider,
    private ValueProvider
{
public:
    /// A calendar on today's month, in single selection, with nothing selected.
    CalendarViewControl();
    /// Destroys the parts.
    ~CalendarViewControl() override;
    CalendarViewControl(const CalendarViewControl&)            = delete;
    CalendarViewControl& operator=(const CalendarViewControl&) = delete;

    // ---- selection ----
    /// The selected dates, in the order they were selected.
    [[nodiscard]] const std::vector<std::chrono::year_month_day>& selected_dates() const
    {
        return selected_;
    }
    /// Replaces the selection. Dates outside [min_date(), max_date()], blackout dates and
    /// repeats are dropped, and single selection keeps the first. Raises the change callback when
    /// the selection differs.
    void set_selected_dates(std::vector<std::chrono::year_month_day> dates);
    /// Called after the selection changed, from input or a setter, with the new selection.
    void on_selected_dates_changed(Callback<void(const std::vector<std::chrono::year_month_day>&)> handler)
    {
        on_selected_dates_changed_ = std::move(handler);
    }
    /// How many dates can be selected. Switching to None clears the selection and to Single
    /// keeps the first date.
    [[nodiscard]] CalendarViewSelectionMode selection_mode() const
    {
        return selection_mode_;
    }
    /// See selection_mode().
    void set_selection_mode(CalendarViewSelectionMode mode);

    // ---- range ----
    /// The first date the calendar shows: unset is January 1, 100 years before today.
    [[nodiscard]] std::chrono::year_month_day min_date() const;
    /// The last date the calendar shows: unset is December 31, 100 years after today.
    [[nodiscard]] std::chrono::year_month_day max_date() const;
    /// Sets the range; unset follows today. A selected date outside it is dropped.
    void set_date_range(std::optional<std::chrono::year_month_day> min, std::optional<std::chrono::year_month_day> max);

    // ---- what is shown ----
    /// Month, Year or Decade.
    [[nodiscard]] CalendarViewDisplayMode display_mode() const
    {
        return display_mode_;
    }
    /// Zooms to `mode`, animated where the theme animates.
    void set_display_mode(CalendarViewDisplayMode mode);
    /// Called after the display mode changed, from input or a setter.
    void on_display_mode_changed(Callback<void(CalendarViewDisplayMode)> handler)
    {
        on_display_mode_changed_ = std::move(handler);
    }
    /// A date in the month (or year, or decade) the calendar shows.
    [[nodiscard]] std::chrono::year_month_day display_date() const
    {
        return display_;
    }
    /// Shows the month, year or decade `date` is in, clamped to the range.
    void set_display_date(std::chrono::year_month_day date);

    /// The day the weeks start on; unset follows regional_date_format().
    [[nodiscard]] std::chrono::weekday first_day_of_week() const;
    /// See first_day_of_week().
    void set_first_day_of_week(std::optional<std::chrono::weekday> day);
    /// Whether today is drawn as a filled circle. True by default.
    [[nodiscard]] bool today_highlighted() const
    {
        return today_highlighted_;
    }
    /// See today_highlighted().
    void set_today_highlighted(bool highlighted);
    /// Whether the days of the neighboring months are dimmed. True by default.
    [[nodiscard]] bool out_of_scope_enabled() const
    {
        return out_of_scope_enabled_;
    }
    /// See out_of_scope_enabled().
    void set_out_of_scope_enabled(bool enabled);
    /// Whether the first day of each month carries the month's abbreviated name above its number
    /// (and January, in the year view, the year). False by default.
    [[nodiscard]] bool group_label_visible() const
    {
        return group_label_visible_;
    }
    /// See group_label_visible().
    void set_group_label_visible(bool visible);
    /// Marks dates that can't be selected: they are drawn struck through and skipped by the
    /// selection. Unset blocks none.
    void set_blackout(Callback<bool(std::chrono::year_month_day)> is_blackout);
    /// Whether `date` is a blackout date.
    [[nodiscard]] bool is_blackout(std::chrono::year_month_day date) const;

    /// The day drawn as today: unset (the default) is ui::today(). A test or a demo pins it.
    void set_today(std::optional<std::chrono::year_month_day> day);
    /// The day drawn as today.
    [[nodiscard]] std::chrono::year_month_day today() const;

    /// The day the keyboard is on in the month view: the selected day, or today, until an arrow
    /// key moves it.
    [[nodiscard]] std::chrono::year_month_day focused_date() const
    {
        return focus_date_;
    }

    // ---- Element ----
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Calendar;
    }
    [[nodiscard]] Rect  focus_ring_rect(const Theme& theme) const override;
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override;
    [[nodiscard]] bool  focus_ring_inside() const override
    {
        return true;
    }
    void for_each_child(ChildVisitor visit) const override;
    /// The heading, the two paging buttons, then the grid, as four tab stops.
    void collect_focusables(std::vector<Element*>& out) override;
    void automation_children(std::vector<Element*>& out) const override;

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    bool               pointer_override(const PointerEvent& e) override;
    bool               key_override(const KeyEvent& e) override;
    void               tick_override(Duration delta) override;
    void               enabled_changed() override;

private:
    friend class detail::CalendarPart;
    friend class detail::CalendarCellAutomation;
    friend class detail::CalendarYearSpin;
    friend class CalendarDatePickerControl;
    friend struct CalendarView;
    friend struct CalendarDatePicker;
    friend class detail::PickerEngine;

    // The look a theme draws the calendar in; measure_override() keeps the last one.
    struct Look;
    [[nodiscard]] static Look make_look(const Theme& theme);

    // One cell of the grid shown now.
    struct Cell
    {
        std::chrono::year_month_day date{};
        Rect                        bounds{};
        bool                        in_scope   = true;
        bool                        in_range   = true;
        bool                        group_head = false; // the 1st of a month, or January
    };

    [[nodiscard]] std::chrono::year_month_day clamp(std::chrono::year_month_day date) const;
    [[nodiscard]] std::vector<Cell> cells(CalendarViewDisplayMode mode, std::chrono::year_month_day scope, const Rect& grid) const;
    [[nodiscard]] Rect        grid_rect() const;
    [[nodiscard]] std::string heading(CalendarViewDisplayMode mode, std::chrono::year_month_day scope) const;
    [[nodiscard]] std::string cell_text(const Cell& cell, CalendarViewDisplayMode mode) const;
    [[nodiscard]] std::string cell_name(const Cell& cell, CalendarViewDisplayMode mode) const;
    [[nodiscard]] bool        is_selected(std::chrono::year_month_day date) const;
    [[nodiscard]] bool        same_scope(std::chrono::year_month_day a,
                                         std::chrono::year_month_day b,
                                         CalendarViewDisplayMode     mode) const;
    [[nodiscard]] bool        has_more_before() const;
    [[nodiscard]] bool        has_more_after() const;
    [[nodiscard]] std::optional<std::size_t> cell_at(Point p) const;
    [[nodiscard]] std::optional<std::size_t> focused_cell() const;
    [[nodiscard]] Rect                       footer_rect() const;
    void                                     open_month_menu(Point at);
    void                                     render_comctl(Renderer& renderer, const Theme& theme) const;
    void                                     render_applet(Renderer& renderer, const Theme& theme) const;
    [[nodiscard]] bool                       applet() const;
    void                                     sync_applet_parts();
    void                                     set_display_year(int year);
    void                                     show_month(std::chrono::year_month_day first);

    void toggle(std::chrono::year_month_day date);
    void activate_cell(const Cell& cell);
    void page(int direction);
    void zoom(bool out);
    void move_focus(std::chrono::year_month_day date);
    void show_scope(std::chrono::year_month_day date, int slide_direction);
    void notify_selection(std::vector<std::chrono::year_month_day> previous);
    void cached_cells_dirty();
    void ensure_cells() const;

    // Look and metrics, measured by measure_override() for the theme it last saw.
    std::unique_ptr<Look> look_;

    std::vector<std::chrono::year_month_day>   selected_;
    CalendarViewSelectionMode                  selection_mode_ = CalendarViewSelectionMode::Single;
    std::optional<std::chrono::year_month_day> min_;
    std::optional<std::chrono::year_month_day> max_;
    CalendarViewDisplayMode                    display_mode_ = CalendarViewDisplayMode::Month;
    std::chrono::year_month_day                display_{};
    std::chrono::year_month_day                focus_date_{};
    std::optional<std::chrono::weekday>        first_day_;
    std::optional<std::chrono::year_month_day> today_;
    bool                                       today_highlighted_      = true;
    bool                                       out_of_scope_enabled_   = true;
    bool                                       group_label_visible_    = false;
    float                                      corner_radius_override_ = -1.0f; // CalendarDatePicker's flyout
    bool popup_frame_ = false; // CalendarDatePicker's popup: the classic calendar gets a 1 px frame
    Callback<bool(std::chrono::year_month_day)>                     blackout_;
    Callback<void(const std::vector<std::chrono::year_month_day>&)> on_selected_dates_changed_;
    Callback<void(CalendarViewDisplayMode)>                         on_display_mode_changed_;
    Subscription                                                    format_changed_;

    // Pointer state over the grid.
    std::optional<std::size_t> hover_cell_;
    std::optional<std::size_t> pressed_cell_;
    bool                       footer_pressed_ = false;

    // The parts: the heading and the paging buttons.
    std::unique_ptr<detail::CalendarPart> header_;
    std::unique_ptr<detail::CalendarPart> previous_;
    std::unique_ptr<detail::CalendarPart> next_;
    // The classic eras' month menu on the title, made on first use.
    std::unique_ptr<MenuFlyoutControl> month_menu_;
    // Windows 9x's month list and year spin box over the grid, made when that look first lays out.
    std::unique_ptr<ComboBoxControl>          month_list_;
    std::unique_ptr<detail::CalendarYearSpin> year_spin_;
    bool                                      syncing_parts_ = false;

    // The cells of the scope shown now, rebuilt when it, the range or the layout changes.
    mutable std::vector<Cell> cells_;
    mutable bool              cells_valid_ = false;
    // Automation nodes per cell index, made on first use and kept.
    mutable std::vector<std::unique_ptr<detail::CalendarCellAutomation>> cell_nodes_;
    mutable std::array<std::unique_ptr<Element>, 7>                      weekday_nodes_{};

    // Animation: a page slide (prev/next) or a zoom (view switch), with the scope it leaves.
    enum class Transition
    {
        None,
        Slide,
        Zoom,
    };
    Transition                  transition_      = Transition::None;
    double                      transition_ms_   = 0.0;
    int                         slide_direction_ = 0; // +1 the new page comes from below
    bool                        zoom_out_        = false;
    CalendarViewDisplayMode     from_mode_       = CalendarViewDisplayMode::Month;
    std::chrono::year_month_day from_scope_{};

    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.grid      = this;
        out.table     = this;
        out.selection = this;
        out.value     = this;
    }
    [[nodiscard]] std::size_t grid_row_count() const override;
    [[nodiscard]] std::size_t grid_column_count() const override;
    [[nodiscard]] Element*    grid_item(std::size_t row, std::size_t column) const override;
    void                      table_column_headers(std::vector<Element*>& out) const override;
    void                      selected_items(std::vector<Element*>& out) const override;
    [[nodiscard]] bool        can_select_multiple() const override
    {
        return selection_mode_ == CalendarViewSelectionMode::Multiple;
    }
    [[nodiscard]] bool selection_required() const override
    {
        return false;
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

/// The declarative form of CalendarViewControl: a month calendar.
///
/// Example:
/// @code
/// ui::Property<std::vector<std::chrono::year_month_day>> picked;
///
/// window.set_content(ui::CalendarView{
///     .selected_dates      = picked,
///     .group_label_visible = true,
///     .is_blackout         = [](std::chrono::year_month_day date)
///     {
///         const std::chrono::weekday day{std::chrono::sys_days{date}};
///         return day == std::chrono::Saturday || day == std::chrono::Sunday; // weekdays only
///     },
/// });
/// @endcode
struct CalendarView
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<CalendarView>` points at.
    using control_type = CalendarViewControl;

    // ---- properties ----
    /// The selected dates (CalendarViewControl::set_selected_dates()).
    TwoWayField<std::vector<std::chrono::year_month_day>> selected_dates{};
    /// The first date shown; unset is January 1, 100 years before today.
    Binding<std::optional<std::chrono::year_month_day>> min_date{};
    /// The last date shown; unset is December 31, 100 years after today.
    Binding<std::optional<std::chrono::year_month_day>> max_date{};
    /// How many dates can be selected.
    Binding<CalendarViewSelectionMode> selection_mode{CalendarViewSelectionMode::Single};
    /// Month, Year or Decade.
    TwoWayField<CalendarViewDisplayMode> display_mode{CalendarViewDisplayMode::Month};
    /// The month (or year, or decade) shown; unset is today's.
    Binding<std::optional<std::chrono::year_month_day>> display_date{};
    /// The day the weeks start on; unset follows regional_date_format().
    Binding<std::optional<std::chrono::weekday>> first_day_of_week{};
    /// Whether today is drawn as a filled circle.
    Binding<bool> today_highlighted{true};
    /// Whether the days of the neighboring months are dimmed.
    Binding<bool> out_of_scope_enabled{true};
    /// Whether the first day of each month carries the month's name.
    Binding<bool> group_label_visible{false};

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
    /// The new selection, from input. A bound value doesn't run it.
    Callback<void(const std::vector<std::chrono::year_month_day>&)> on_selected_dates_changed{};
    /// The new display mode, from input. A bound value doesn't run it.
    Callback<void(CalendarViewDisplayMode)> on_display_mode_changed{};
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
    /// A wheel turn over the control, before the control pages; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<CalendarView> control{}; ///< Out: the handle that receives the built control.

    // ---- templates ----
    /// Marks dates that can't be selected; unset blocks none.
    Callback<bool(std::chrono::year_month_day)> is_blackout{};

    /// Consumes the spec and returns the built ui::CalendarViewControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
