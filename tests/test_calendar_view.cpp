// SPDX-License-Identifier: MIT
#include "winui/core/DateFormat.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/datetime/CalendarView.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <chrono>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;
using namespace std::chrono_literals;
using std::chrono::year_month_day;

namespace
{
constexpr year_month_day ymd(int y, unsigned m, unsigned d)
{
    return year_month_day{std::chrono::year{y}, std::chrono::month{m}, std::chrono::day{d}};
}

// October 19, 2026 is a Monday; October 1 is a Thursday.
constexpr year_month_day Today = ymd(2026, 10, 19);

KeyEvent key(Key k, ModifierKeys modifiers = {})
{
    KeyEvent e{k, KeyAction::Down};
    e.modifiers = modifiers;
    return e;
}

// A calendar pinned to Today, Sunday first, laid out at the origin.
void lay_out(CalendarViewControl& calendar, const Theme& theme = Theme::light())
{
    calendar.set_today(Today);
    calendar.set_first_day_of_week(std::chrono::Sunday);
    calendar.set_display_date(Today);
    HeadlessTextMeasurer m;
    const Size           size = calendar.measure({1000.0f, 1000.0f}, m, theme);
    calendar.arrange({0.0f, 0.0f, size.width, size.height});
}

// The center of the month view's cell for `date` (Sunday first, October 2026 shown): the grid
// starts at September 27.
Point day_point(year_month_day date)
{
    const auto index = (std::chrono::sys_days{date} - std::chrono::sys_days{ymd(2026, 9, 27)}).count();
    // Fluent 2: the grid is 294 wide at x 3, its rows 42 tall from y 95.
    return Point{3.0f + 42.0f * static_cast<float>(index % 7) + 21.0f,
                 95.0f + 42.0f * static_cast<float>(index / 7) + 21.0f};
}

void click(CalendarViewControl& calendar, Point p)
{
    calendar.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    calendar.handle_pointer({p, PointerButton::Left, PointerAction::Up});
}
} // namespace

TEST_CASE("A new CalendarView shows today's month in single selection", "[calendar-view]")
{
    CalendarViewControl calendar;
    CHECK(calendar.selection_mode() == CalendarViewSelectionMode::Single);
    CHECK(calendar.selected_dates().empty());
    CHECK(calendar.display_mode() == CalendarViewDisplayMode::Month);
    CHECK(calendar.today_highlighted());
    CHECK(calendar.out_of_scope_enabled());
    CHECK_FALSE(calendar.group_label_visible());
    const year_month_day now = ui::today();
    CHECK(calendar.display_date() == now);
    CHECK(calendar.min_date() == ymd(static_cast<int>(now.year()) - 100, 1, 1));
    CHECK(calendar.max_date() == ymd(static_cast<int>(now.year()) + 100, 12, 31));
}

TEST_CASE("The Fluent 2 calendar is 300 by 350", "[calendar-view][layout]")
{
    CalendarViewControl  calendar;
    HeadlessTextMeasurer m;
    const Size           size = calendar.measure({1000.0f, 1000.0f}, m, Theme::light());
    CHECK(size.width == 300.0f);
    CHECK(size.height == 350.0f);
}

TEST_CASE("Clicking days selects and deselects them", "[calendar-view][input]")
{
    CalendarViewControl calendar;
    lay_out(calendar);
    std::vector<std::vector<year_month_day>> seen;
    calendar.on_selected_dates_changed([&](const std::vector<year_month_day>& dates) { seen.push_back(dates); });

    click(calendar, day_point(ymd(2026, 10, 7)));
    REQUIRE(calendar.selected_dates() == std::vector{ymd(2026, 10, 7)});

    SECTION("single selection replaces the date, and a second click removes it")
    {
        click(calendar, day_point(ymd(2026, 10, 9)));
        CHECK(calendar.selected_dates() == std::vector{ymd(2026, 10, 9)});
        click(calendar, day_point(ymd(2026, 10, 9)));
        CHECK(calendar.selected_dates().empty());
        CHECK(seen.size() == 3);
    }
    SECTION("multiple selection adds dates")
    {
        calendar.set_selection_mode(CalendarViewSelectionMode::Multiple);
        click(calendar, day_point(ymd(2026, 10, 9)));
        click(calendar, day_point(ymd(2026, 10, 12)));
        CHECK(calendar.selected_dates() == std::vector{ymd(2026, 10, 7), ymd(2026, 10, 9), ymd(2026, 10, 12)});
        calendar.set_selection_mode(CalendarViewSelectionMode::Single); // keeps the first
        CHECK(calendar.selected_dates() == std::vector{ymd(2026, 10, 7)});
    }
    SECTION("no selection mode clears it and ignores clicks")
    {
        calendar.set_selection_mode(CalendarViewSelectionMode::None);
        CHECK(calendar.selected_dates().empty());
        click(calendar, day_point(ymd(2026, 10, 9)));
        CHECK(calendar.selected_dates().empty());
    }
}

TEST_CASE("Blackout and out-of-range dates can't be selected", "[calendar-view]")
{
    CalendarViewControl calendar;
    calendar.set_selection_mode(CalendarViewSelectionMode::Multiple);
    calendar.set_blackout(
        [](year_month_day date) { return std::chrono::weekday{std::chrono::sys_days{date}} == std::chrono::Sunday; });
    lay_out(calendar);
    calendar.set_date_range(ymd(2026, 10, 5), ymd(2026, 10, 30));

    calendar.set_selected_dates({ymd(2026, 10, 18), ymd(2026, 10, 1), ymd(2026, 10, 20), ymd(2026, 10, 20)});
    CHECK(calendar.selected_dates() == std::vector{ymd(2026, 10, 20)}); // a Sunday, too early, then a repeat

    click(calendar, day_point(ymd(2026, 10, 11))); // a Sunday
    click(calendar, day_point(ymd(2026, 10, 2)));  // before the range
    CHECK(calendar.selected_dates() == std::vector{ymd(2026, 10, 20)});
    CHECK(calendar.is_blackout(ymd(2026, 10, 25)));
}

TEST_CASE("The month grid starts on the week holding the 1st", "[calendar-view]")
{
    CalendarViewControl calendar;
    lay_out(calendar);
    // Sunday first: October 1, 2026 is a Thursday, so the grid starts on September 27, and a
    // click on the first cell picks it.
    click(calendar, Point{3.0f + 21.0f, 95.0f + 21.0f});
    CHECK(calendar.selected_dates() == std::vector{ymd(2026, 9, 27)});
    CHECK(calendar.display_date().month() == std::chrono::September); // clicking another month's day shows it

    calendar.set_first_day_of_week(std::chrono::Monday);
    calendar.set_display_date(Today);
    click(calendar, Point{3.0f + 21.0f, 95.0f + 21.0f});
    CHECK(calendar.selected_dates() == std::vector{ymd(2026, 9, 28)});
}

TEST_CASE("The keyboard moves the focused day and pages months", "[calendar-view][keyboard]")
{
    CalendarViewControl calendar;
    lay_out(calendar);
    calendar.set_display_date(ymd(2026, 1, 31));
    calendar.handle_key(key(Key::Home));
    CHECK(calendar.focused_date() == ymd(2026, 1, 1));
    calendar.handle_key(key(Key::End));
    CHECK(calendar.focused_date() == ymd(2026, 1, 31));

    calendar.handle_key(key(Key::PageDown)); // January 31 clamps to the end of February
    CHECK(calendar.focused_date() == ymd(2026, 2, 28));
    CHECK(calendar.display_date().month() == std::chrono::February);

    calendar.handle_key(key(Key::ArrowRight)); // past the end of the month shows the next one
    CHECK(calendar.focused_date() == ymd(2026, 3, 1));
    CHECK(calendar.display_date().month() == std::chrono::March);
    calendar.handle_key(key(Key::ArrowUp));
    CHECK(calendar.focused_date() == ymd(2026, 2, 22));

    calendar.handle_key(key(Key::Space));
    CHECK(calendar.selected_dates() == std::vector{ymd(2026, 2, 22)});
}

TEST_CASE("Ctrl+Up zooms out to the year and decade, Enter zooms back in", "[calendar-view][keyboard]")
{
    CalendarViewControl calendar;
    lay_out(calendar);
    std::vector<CalendarViewDisplayMode> modes;
    calendar.on_display_mode_changed([&](CalendarViewDisplayMode mode) { modes.push_back(mode); });

    calendar.handle_key(key(Key::ArrowUp, ModifierKeys{.ctrl = true}));
    CHECK(calendar.display_mode() == CalendarViewDisplayMode::Year);
    calendar.handle_key(key(Key::ArrowUp, ModifierKeys{.ctrl = true}));
    CHECK(calendar.display_mode() == CalendarViewDisplayMode::Decade);
    calendar.handle_key(key(Key::ArrowUp, ModifierKeys{.ctrl = true})); // no further
    CHECK(modes.size() == 2);

    calendar.handle_key(key(Key::ArrowRight)); // the next year
    calendar.handle_key(key(Key::Enter));
    CHECK(calendar.display_mode() == CalendarViewDisplayMode::Year);
    CHECK(calendar.display_date().year() == std::chrono::year{2027});
    calendar.handle_key(key(Key::ArrowDown, ModifierKeys{.ctrl = true}));
    CHECK(calendar.display_mode() == CalendarViewDisplayMode::Month);
}

TEST_CASE("Paging stops at the ends of the range", "[calendar-view]")
{
    CalendarViewControl calendar;
    lay_out(calendar);
    calendar.set_date_range(ymd(2026, 9, 10), ymd(2026, 11, 20));
    calendar.handle_key(key(Key::PageDown));
    calendar.handle_key(key(Key::PageDown));
    calendar.handle_key(key(Key::PageDown));
    CHECK(calendar.display_date().month() == std::chrono::November);
    calendar.set_display_date(ymd(2020, 1, 1)); // clamped
    CHECK(calendar.display_date() == ymd(2026, 9, 10));
}

TEST_CASE("Today is a filled accent circle, a selected day an accent ring", "[calendar-view][render]")
{
    CalendarViewControl calendar;
    lay_out(calendar);
    calendar.set_selected_dates({ymd(2026, 10, 7)});
    const Theme& theme = Theme::light();
    TestRenderer r;
    calendar.render(r, theme);

    int today_fills = 0;
    int rings       = 0;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&command);
            fill && fill->color == theme.colors().accent && fill->radius == 20.0f)
        {
            ++today_fills;
        }
        if (const auto* stroke = std::get_if<StrokeRoundedRect>(&command); stroke && stroke->color == theme.colors().accent)
        {
            ++rings;
        }
    }
    CHECK(today_fills == 1);
    CHECK(rings == 1);
}

TEST_CASE("The calendar describes itself to automation", "[calendar-view][automation]")
{
    CalendarViewControl calendar;
    lay_out(calendar);
    const std::string tree = describe_automation(calendar);
    CHECK(tree.find("Calendar") == 0);
    CHECK(tree.find("Button \"October 2026\"") != std::string::npos);
    CHECK(tree.find("Button \"Previous\"") != std::string::npos);
    CHECK(tree.find("HeaderItem \"Sunday\"") != std::string::npos);
    CHECK(tree.find("DataItem \"19, today\"") != std::string::npos);
}

TEST_CASE("A CalendarView spec binds its selection both ways", "[calendar-view][spec]")
{
    HeadlessUI                            ui;
    Property<std::vector<year_month_day>> picked{std::vector{ymd(2026, 10, 7)}};
    Control<CalendarView>                 calendar;
    ui.window().set_content(CalendarView{.selected_dates = picked, .control = calendar});
    ui.layout({400.0f, 400.0f});

    CHECK(calendar->selected_dates() == std::vector{ymd(2026, 10, 7)});
    CHECK(calendar->display_date().month() == std::chrono::October); // shows the selected date

    calendar->set_selected_dates({ymd(2026, 10, 9)});
    CHECK(picked.get() == std::vector{ymd(2026, 10, 9)});

    picked = std::vector{ymd(2026, 10, 12)};
    CHECK(calendar->selected_dates() == std::vector{ymd(2026, 10, 12)});
}

TEST_CASE("Each era draws its own month calendar", "[calendar-view][layout][era]")
{
    const auto size_in = [](ThemeVariant variant)
    {
        CalendarViewControl  calendar;
        HeadlessTextMeasurer m;
        return calendar.measure({1000.0f, 1000.0f}, m, resolve_theme(ThemeRequest{.variant = variant}, OsThemeSnapshot{}));
    };
    CHECK(size_in(ThemeVariant::Fluent2) == Size{300.0f, 350.0f});
    CHECK(size_in(ThemeVariant::Fluent) == Size{296.0f, 332.0f}); // Windows 10's square cells
    CHECK(size_in(ThemeVariant::Mdl2) == Size{296.0f, 332.0f});
    CHECK(size_in(ThemeVariant::Aero) == Size{225.0f, 160.0f}); // the Vista design
    CHECK(size_in(ThemeVariant::Metro) == Size{225.0f, 160.0f});
    CHECK(size_in(ThemeVariant::Windows9x) == Size{164.0f, 162.0f}); // the Date/Time control panel's
    CHECK(size_in(ThemeVariant::Windows3x) == Size{176.0f, 153.0f}); // comctl32's, 25 px cells
    CHECK(size_in(ThemeVariant::Luna) == Size{169.0f, 153.0f});      // the same with Tahoma's 24
}

TEST_CASE("Windows 9x's calendar pages with its month list and year spin box", "[calendar-view][era]")
{
    const Theme         theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    CalendarViewControl calendar;
    lay_out(calendar, theme);
    calendar.set_display_date(ymd(2026, 4, 1));

    ComboBoxControl* months = nullptr;
    Element*         years  = nullptr;
    calendar.for_each_child(
        [&](Element& child)
        {
            if (auto* combo = dynamic_cast<ComboBoxControl*>(&child))
            {
                months = combo;
            }
            else if (child.automation_role() == AutomationRole::Edit)
            {
                years = &child;
            }
        });
    REQUIRE(months != nullptr);
    REQUIRE(years != nullptr);
    CHECK(months->items().size() == 12);
    CHECK(months->selected_index() == 3u); // April

    months->set_selected_index(0u);
    CHECK(calendar.display_date().month() == std::chrono::January);
    CHECK(calendar.display_date().year() == std::chrono::year{2026});

    // Up and Down step the year, four digits type one.
    years->handle_key(key(Key::ArrowUp));
    CHECK(calendar.display_date().year() == std::chrono::year{2027});
    years->handle_text(TextInputEvent{"1"});
    years->handle_text(TextInputEvent{"9"});
    years->handle_text(TextInputEvent{"9"});
    years->handle_text(TextInputEvent{"8"});
    CHECK(calendar.display_date().year() == std::chrono::year{1998});
    CHECK(calendar.display_date().month() == std::chrono::January);
    CHECK(months->selected_index() == 0u);

    // The heading and paging buttons are gone; the tab stops are the list, the year and the grid.
    std::vector<Element*> stops;
    calendar.collect_focusables(stops);
    REQUIRE(stops.size() == 3);
    CHECK(stops[0] == months);
    CHECK(stops[1] == years);
    CHECK(stops[2] == &calendar);

    // There is no year view to zoom to.
    calendar.set_display_mode(CalendarViewDisplayMode::Year);
    CHECK(calendar.display_mode() == CalendarViewDisplayMode::Month);
}

TEST_CASE("Windows 9x's calendar shows only the month's own days", "[calendar-view][era]")
{
    const Theme         theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    CalendarViewControl calendar;
    lay_out(calendar, theme);
    calendar.set_display_date(ymd(2026, 4, 1)); // April 2026 starts on a Wednesday

    // The first row's Sunday is March 29: a blank cell, which a click doesn't pick.
    const Point sunday{2.0f + 11.0f, 52.0f + 9.0f};
    calendar.handle_pointer({sunday, PointerButton::Left, PointerAction::Down});
    calendar.handle_pointer({sunday, PointerButton::Left, PointerAction::Up});
    CHECK(calendar.selected_dates().empty());

    TestRenderer r;
    calendar.render(r, theme);
    bool drew_march = false;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* run = std::get_if<DrawTextRun>(&command); run && run->text == "29" && run->bounds.y < 70.0f)
        {
            drew_march = true;
        }
    }
    CHECK_FALSE(drew_march);
}

TEST_CASE("The classic month calendar's footer goes to today", "[calendar-view][era]")
{
    const Theme         theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{});
    CalendarViewControl calendar;
    lay_out(calendar, theme);
    calendar.set_display_date(ymd(2025, 3, 1));
    const Point footer{60.0f, 135.0f + 9.0f};
    calendar.handle_pointer({footer, PointerButton::Left, PointerAction::Down});
    calendar.handle_pointer({footer, PointerButton::Left, PointerAction::Up});
    CHECK(calendar.display_date().month() == std::chrono::October);
    CHECK(calendar.selected_dates() == std::vector{Today});

    // Ctrl+PageDown moves a year, as in comctl32 5.81.
    calendar.handle_key(key(Key::PageDown, ModifierKeys{.ctrl = true}));
    CHECK(calendar.focused_date() == ymd(2027, 10, 19));
}
