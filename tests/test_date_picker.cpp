// SPDX-License-Identifier: MIT
#include "winui/core/DateFormat.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/datetime/DatePicker.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

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

// The invariant format: "M/d/yyyy", so the parts run month, day, year.
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

// A window with one DatePicker under `variant`, focused.
struct PickerWindow
{
    RegionalDateGuard                       guard;
    HeadlessUI                              ui;
    Control<DatePicker>                     picker;
    Property<std::optional<year_month_day>> date;

    explicit PickerWindow(ThemeVariant                  variant = ThemeVariant::Fluent2,
                          std::optional<year_month_day> initial = std::nullopt) :
        date(initial)
    {
        ui.window().set_theme(ThemeRequest{.variant = variant, .animations = AnimationPreference::Off});
        ui.window().set_content(
            DatePicker{.header = "Birthday", .selected_date = date, .min_year = 2000, .max_year = 2030, .control = picker});
        ui.layout({600.0f, 700.0f});
        REQUIRE(picker->focus());
    }
    void press(Key k, ModifierKeys modifiers = {})
    {
        seams(ui.window()).handle_key(key(k, modifiers));
        ui.layout({600.0f, 700.0f});
    }
};
} // namespace

TEST_CASE("A new DatePicker has no date and a century each way", "[date-picker]")
{
    DatePickerControl picker;
    CHECK_FALSE(picker.selected_date());
    const int year = static_cast<int>(ui::today().year());
    CHECK(picker.min_year() == year - 100);
    CHECK(picker.max_year() == year + 100);
    picker.set_year_range(2000, 2010);
    picker.set_selected_date(ymd(2020, 5, 5));
    CHECK(picker.selected_date() == ymd(2010, 12, 31));
}

TEST_CASE("The flyout's columns pick a date, kept on Enter", "[date-picker][flyout]")
{
    PickerWindow w(ThemeVariant::Fluent2, ymd(2026, 1, 31));
    w.press(Key::Enter);
    REQUIRE(w.picker->is_open());

    // Focus starts in the month column: down to February keeps the day where it fits.
    w.press(Key::ArrowDown);
    w.press(Key::Enter);
    CHECK_FALSE(w.picker->is_open());
    CHECK(w.date.get() == ymd(2026, 2, 28));

    SECTION("Escape drops the choice")
    {
        w.press(Key::Enter);
        w.press(Key::ArrowDown);
        w.press(Key::Escape);
        CHECK_FALSE(w.picker->is_open());
        CHECK(w.date.get() == ymd(2026, 2, 28));
    }
    SECTION("Right moves to the day column")
    {
        w.press(Key::Enter);
        w.press(Key::ArrowRight);
        w.press(Key::ArrowUp);
        w.press(Key::Enter);
        CHECK(w.date.get() == ymd(2026, 2, 27));
    }
}

TEST_CASE("comctl32's field edits the date in place", "[date-picker][field]")
{
    PickerWindow w(ThemeVariant::Windows9x, ymd(2026, 10, 7));
    // The month is first in "M/d/yyyy": Up turns it, Right moves to the day.
    w.press(Key::ArrowUp);
    CHECK(w.date.get() == ymd(2026, 11, 7));
    w.press(Key::ArrowRight);
    w.press(Key::ArrowDown);
    CHECK(w.date.get() == ymd(2026, 11, 6));
    w.press(Key::End);
    CHECK(w.date.get() == ymd(2026, 11, 30));

    // Typed digits fill the part.
    seams(w.ui.window()).handle_text("1");
    seams(w.ui.window()).handle_text("2");
    CHECK(w.date.get() == ymd(2026, 11, 12));

    // F4 opens the era's calendar.
    w.press(Key::F4);
    CHECK(w.picker->is_open());
}

TEST_CASE("Windows 8.1's picker is three drop-down lists", "[date-picker][combo]")
{
    PickerWindow                  w(ThemeVariant::Metro, ymd(2026, 10, 7));
    std::vector<ComboBoxControl*> combos;
    w.picker->for_each_child(
        [&](Element& child)
        {
            if (auto* combo = dynamic_cast<ComboBoxControl*>(&child))
            {
                combos.push_back(combo);
            }
        });
    REQUIRE(combos.size() == 3);
    CHECK(combos[0]->items().size() == 12); // the months
    combos[1]->set_selected_index(19);      // the 20th
    CHECK(w.date.get() == ymd(2026, 10, 20));
}

TEST_CASE("The DatePicker is a Button with the date as its value", "[date-picker][automation]")
{
    RegionalDateGuard guard;
    DatePickerControl picker;
    picker.set_header("Birthday");
    picker.set_selected_date(ymd(2026, 10, 7));
    const std::string tree = describe_automation(picker);
    CHECK(tree.find("Button \"Birthday\"") == 0);
    CHECK(tree.find("Value=\"Wednesday, October 7, 2026\"") != std::string::npos);
}
