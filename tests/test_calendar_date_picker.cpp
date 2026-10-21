// SPDX-License-Identifier: MIT
#include "winui/core/DateFormat.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/datetime/CalendarDatePicker.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;
using std::chrono::year_month_day;

namespace
{
constexpr year_month_day ymd(int y, unsigned m, unsigned d)
{
    return year_month_day{std::chrono::year{y}, std::chrono::month{m}, std::chrono::day{d}};
}

KeyEvent key(Key k, ModifierKeys modifiers = {})
{
    KeyEvent e{k, KeyAction::Down};
    e.modifiers = modifiers;
    return e;
}

// Restores the regional format a test set.
struct RegionalDateGuard
{
    DateFormatSymbols saved = regional_date_format();
    RegionalDateGuard()
    {
        set_regional_date_format(DateFormatSymbols::invariant());
    }
    ~RegionalDateGuard()
    {
        set_regional_date_format(saved);
    }
};
} // namespace

TEST_CASE("A new CalendarDatePicker shows its placeholder", "[calendar-date-picker]")
{
    RegionalDateGuard         guard;
    CalendarDatePickerControl picker;
    CHECK_FALSE(picker.date());
    CHECK(picker.shown_text() == "Pick a date");
    picker.set_placeholder_text("When?");
    CHECK(picker.shown_text() == "When?");
    picker.set_date(ymd(2026, 10, 7));
    CHECK(picker.shown_text() == "10/7/2026");
    picker.set_date_format("dddd, d MMMM");
    CHECK(picker.shown_text() == "Wednesday, 7 October");
}

TEST_CASE("Picking a day in the calendar sets the date and closes it", "[calendar-date-picker]")
{
    RegionalDateGuard                       guard;
    HeadlessUI                              ui;
    Control<CalendarDatePicker>             picker;
    Property<std::optional<year_month_day>> date;
    std::vector<bool>                       opened;
    ui.window().set_content(CalendarDatePicker{
        .header    = "Arrival",
        .date      = date,
        .on_opened = [&] { opened.push_back(true); },
        .on_closed = [&] { opened.push_back(false); },
        .control   = picker,
    });
    ui.layout({400.0f, 500.0f});

    picker->handle_key(key(Key::Enter));
    REQUIRE(picker->is_calendar_open());
    CHECK(picker->calendar().display_date().month() == ui::today().month()); // opens on today

    picker->calendar().set_selected_dates({ymd(2026, 10, 9)}); // what a click on the 9th does
    CHECK_FALSE(picker->is_calendar_open());
    CHECK(date.get() == ymd(2026, 10, 9));
    CHECK(opened == std::vector{true, false});

    SECTION("it opens on the date, and clearing the date keeps it open")
    {
        picker->handle_key(key(Key::ArrowDown, ModifierKeys{.alt = true}));
        REQUIRE(picker->is_calendar_open());
        CHECK(picker->calendar().selected_dates() == std::vector{ymd(2026, 10, 9)});
        CHECK(picker->calendar().display_date().month() == std::chrono::October);
        picker->calendar().set_selected_dates({});
        CHECK(picker->is_calendar_open());
        CHECK_FALSE(date.get());
    }
}

TEST_CASE("The date is clamped to the calendar's range", "[calendar-date-picker]")
{
    CalendarDatePickerControl picker;
    picker.calendar().set_date_range(ymd(2026, 1, 1), ymd(2026, 12, 31));
    picker.set_date(ymd(2030, 5, 5));
    CHECK(picker.date() == ymd(2026, 12, 31));
}

TEST_CASE("A disabled picker doesn't open", "[calendar-date-picker]")
{
    CalendarDatePickerControl picker;
    picker.set_enabled(false);
    picker.set_calendar_open(true);
    CHECK_FALSE(picker.is_calendar_open());
}

TEST_CASE("The picker is a Button with its date as the value", "[calendar-date-picker][automation]")
{
    RegionalDateGuard         guard;
    CalendarDatePickerControl picker;
    picker.set_header("Arrival");
    picker.set_date(ymd(2026, 10, 7));
    const std::string tree = describe_automation(picker);
    CHECK(tree.find("Button \"Arrival\"") == 0);
    CHECK(tree.find("Value=\"10/7/2026\"") != std::string::npos);
}
