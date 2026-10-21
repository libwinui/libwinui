// SPDX-License-Identifier: MIT
#include "winui/core/DateFormat.hpp"
#include "winui/core/Property.hpp"

#include <chrono>
#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace std::chrono_literals;

namespace
{
constexpr std::chrono::year_month_day Monday19{std::chrono::year{2026}, std::chrono::October, std::chrono::day{19}};
constexpr std::chrono::year_month_day March5{std::chrono::year{2007}, std::chrono::March, std::chrono::day{5}};

// Restores the regional format a test changed.
struct RegionalDateGuard
{
    DateFormatSymbols saved = regional_date_format();
    ~RegionalDateGuard()
    {
        set_regional_date_format(saved);
    }
};
} // namespace

TEST_CASE("format_date writes the Windows pattern letters", "[date-format]")
{
    const DateFormatSymbols en = DateFormatSymbols::invariant();
    CHECK(format_date(Monday19, "M/d/yyyy", en) == "10/19/2026");
    CHECK(format_date(March5, "M/d/yyyy", en) == "3/5/2007");
    CHECK(format_date(March5, "MM/dd/yy", en) == "03/05/07");
    CHECK(format_date(March5, "y", en) == "7");
    CHECK(format_date(Monday19, "dddd, MMMM d, yyyy", en) == "Monday, October 19, 2026");
    CHECK(format_date(Monday19, "ddd d MMM", en) == "Mon 19 Oct");
    CHECK(format_date(Monday19, "MMMM yyyy", en) == "October 2026");
}

TEST_CASE("format_date writes quoted text as it stands", "[date-format]")
{
    const DateFormatSymbols en = DateFormatSymbols::invariant();
    CHECK(format_date(Monday19, "d 'de' MMMM", en) == "19 de October");
    CHECK(format_date(Monday19, "'day' d", en) == "day 19");
    CHECK(format_date(Monday19, "d''MM", en) == "19'10");
    CHECK(format_date(Monday19, "'it''s' d", en) == "it's 19");
    CHECK(format_date(Monday19, "yyyy.MM.dd.", en) == "2026.10.19.");
}

TEST_CASE("format_date takes the names from the symbols it is given", "[date-format]")
{
    DateFormatSymbols de = DateFormatSymbols::invariant();
    de.month_names[9]    = "Oktober";
    de.day_names[1]      = "Montag";
    const std::chrono::year_month_day bad{std::chrono::year{2026}, std::chrono::February, std::chrono::day{30}};
    CHECK(format_date(Monday19, "dddd, d. MMMM yyyy", de) == "Montag, 19. Oktober 2026");
    CHECK(format_date(bad, "d.M.yyyy", de).empty());
}

TEST_CASE("format_time writes 12 and 24 hour clocks", "[date-format]")
{
    const DateFormatSymbols en = DateFormatSymbols::invariant();
    CHECK(format_time(0min, "h:mm tt", en) == "12:00 AM");
    CHECK(format_time(9h + 5min, "h:mm tt", en) == "9:05 AM");
    CHECK(format_time(12h, "h:mm tt", en) == "12:00 PM");
    CHECK(format_time(23h + 59min, "hh:mm t", en) == "11:59 P");
    CHECK(format_time(9h + 5min, "HH:mm", en) == "09:05");
    CHECK(format_time(21h + 30min, "H:mm", en) == "21:30");
    CHECK(format_time(25h, "H:mm", en) == "1:00"); // whole days are dropped
}

TEST_CASE("A time pattern tells which clock it uses", "[date-format]")
{
    DateFormatSymbols symbols = DateFormatSymbols::invariant();
    CHECK(symbols.twelve_hour_clock());
    symbols.short_time_pattern = "HH:mm";
    CHECK_FALSE(symbols.twelve_hour_clock());
    symbols.short_time_pattern = "'h' HH:mm"; // a quoted h is text
    CHECK_FALSE(symbols.twelve_hour_clock());
}

TEST_CASE("The regional date format notifies only on a change", "[date-format]")
{
    RegionalDateGuard guard;
    set_regional_date_format(DateFormatSymbols::invariant());
    int          raised = 0;
    Subscription sub    = regional_date_format_changed().observe([&] { ++raised; });

    set_regional_date_format(DateFormatSymbols::invariant());
    CHECK(raised == 0);

    DateFormatSymbols monday_first  = DateFormatSymbols::invariant();
    monday_first.first_day_of_week  = std::chrono::Monday;
    monday_first.short_date_pattern = "dd.MM.yyyy";
    set_regional_date_format(monday_first);
    CHECK(raised == 1);
    CHECK(regional_date_format().first_day_of_week == std::chrono::Monday);
    CHECK(format_date(Monday19, regional_date_format().short_date_pattern) == "19.10.2026");
}

TEST_CASE("today() is a valid date", "[date-format]")
{
    CHECK(today().ok());
}
