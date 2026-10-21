// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Property.hpp"

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace ui
{

/// The names and patterns a locale writes dates and times with, as the date and time controls
/// use them. The defaults are the locale-independent ones: English names, Sunday first and US
/// patterns.
///
/// A pattern uses the Windows date and time pattern letters, the way the Region settings write
/// them:
///
/// - `d` and `dd` are the day, `dd` with a leading zero.
/// - `ddd` and `dddd` are the abbreviated and the full day name.
/// - `M` and `MM` are the month number, `MM` with a leading zero.
/// - `MMM` and `MMMM` are the abbreviated and the full month name.
/// - `y` and `yy` are the last two digits of the year, `y` without a leading zero.
/// - `yyyy` is the full year.
/// - `h` and `hh` are the hour on a 12-hour clock, `H` and `HH` on a 24-hour clock, doubled
///   with a leading zero.
/// - `m` and `mm` are the minute.
/// - `t` is the first letter of the AM or PM designator, `tt` the whole designator.
/// - `'text'` is written as it stands, and `''` is one apostrophe.
///
/// Every other character is written as it is.
///
/// Example:
/// @code
/// // A delivery date in German, written the same whatever the user's own locale is.
/// ui::DateFormatSymbols german = ui::DateFormatSymbols::invariant();
/// german.month_names = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August",
///                       "September", "Oktober", "November", "Dezember"};
/// const std::chrono::year_month_day day{std::chrono::year{2026}, std::chrono::October, std::chrono::day{19}};
/// window.set_content(ui::Text{.text = "Lieferung am " + ui::format_date(day, "d. MMMM yyyy", german)}); // "19. Oktober 2026"
/// @endcode
struct DateFormatSymbols
{
    /// The month names, January first.
    std::array<std::string, 12>
        month_names{"January",
                    "February",
                    "March",
                    "April",
                    "May",
                    "June",
                    "July",
                    "August",
                    "September",
                    "October",
                    "November",
                    "December"};
    /// The abbreviated month names, January first.
    std::array<std::string, 12>
        abbreviated_month_names{"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    /// The day names, Sunday first (the index is `std::chrono::weekday::c_encoding()`).
    std::array<std::string, 7> day_names{"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    /// The abbreviated day names, Sunday first.
    std::array<std::string, 7> abbreviated_day_names{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    /// The shortest day names, Sunday first: a calendar's column headers.
    std::array<std::string, 7> shortest_day_names{"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    /// The day a week starts on, the first column of a calendar.
    std::chrono::weekday first_day_of_week = std::chrono::Sunday;
    /// The short date: "M/d/yyyy" (en-US), "dd.MM.yyyy" (de-DE), "yyyy-MM-dd" (sv-SE).
    std::string short_date_pattern = "M/d/yyyy";
    /// The long date: "dddd, MMMM d, yyyy" (en-US), "dddd, d. MMMM yyyy" (de-DE).
    std::string long_date_pattern = "dddd, MMMM d, yyyy";
    /// A month with its year, a calendar's heading: "MMMM yyyy" (en-US).
    std::string year_month_pattern = "MMMM yyyy";
    /// The short time: "h:mm tt" (en-US), "HH:mm" (de-DE).
    std::string short_time_pattern = "h:mm tt";
    /// The designators a 12-hour time carries: "AM" and "PM" (en-US), "vorm." and "nachm." (de-AT).
    std::string am_designator = "AM";
    std::string pm_designator = "PM"; ///< See `am_designator`.

    /// Whether `short_time_pattern` uses the 12-hour clock (an `h` outside quotes).
    [[nodiscard]] bool twelve_hour_clock() const;

    /// The locale-independent symbols, the default.
    [[nodiscard]] static DateFormatSymbols invariant()
    {
        return {};
    }

    /// Member-wise comparison; set_regional_date_format() uses it to skip no-op changes.
    friend bool operator==(const DateFormatSymbols&, const DateFormatSymbols&) = default;
};

/// The regional date format: what the date and time controls use. Invariant until a host sets
/// it. Process-wide; UI thread only.
[[nodiscard]] const DateFormatSymbols& regional_date_format();
/// Sets the regional date format. The window host sets it from the user's locale and the Region
/// settings. Raises regional_date_format_changed() when it differs from the current one.
void set_regional_date_format(DateFormatSymbols symbols);
/// Raised when the regional date format changes: the date and time controls write their text
/// again. An app that caches formatted dates subscribes here and keeps the Subscription.
[[nodiscard]] const Signal& regional_date_format_changed();

/// `date` written with `pattern` (see DateFormatSymbols). An invalid date writes nothing.
[[nodiscard]] std::string format_date(std::chrono::year_month_day date,
                                      std::string_view            pattern,
                                      const DateFormatSymbols&    symbols = regional_date_format());
/// `time` (a time of day, from midnight) written with `pattern` (see DateFormatSymbols). Whole
/// days in `time` are dropped.
[[nodiscard]] std::string format_time(std::chrono::minutes     time,
                                      std::string_view         pattern,
                                      const DateFormatSymbols& symbols = regional_date_format());

/// The date today on the local clock. Read from the system clock on every call.
[[nodiscard]] std::chrono::year_month_day today();

} // namespace ui
