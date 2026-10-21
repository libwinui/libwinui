// SPDX-License-Identifier: MIT
#include "winui/core/DateFormat.hpp"

#include "winui/core/Property.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

#include <ctime>

namespace ui
{

namespace
{
struct RegionalDateFormat
{
    DateFormatSymbols symbols;
    Signal            changed;
};

RegionalDateFormat& regional_dates()
{
    static RegionalDateFormat format;
    return format;
}

// How many times `letter` repeats from `pattern[i]`.
std::size_t run_length(std::string_view pattern, std::size_t i)
{
    std::size_t n = 1;
    while (i + n < pattern.size() && pattern[i + n] == pattern[i])
    {
        ++n;
    }
    return n;
}

void append_number(std::string& out, long long value, std::size_t min_digits)
{
    std::string digits = std::to_string(value < 0 ? -value : value);
    if (digits.size() < min_digits)
    {
        digits.insert(0, min_digits - digits.size(), '0');
    }
    if (value < 0)
    {
        out += '-';
    }
    out += digits;
}

// Writes a quoted literal starting at `pattern[i]` (the opening quote) and returns the index
// after the closing one. A doubled quote is one apostrophe, inside or outside a literal.
std::size_t append_literal(std::string& out, std::string_view pattern, std::size_t i)
{
    if (i + 1 < pattern.size() && pattern[i + 1] == '\'')
    {
        out += '\'';
        return i + 2;
    }
    ++i;
    while (i < pattern.size())
    {
        if (pattern[i] == '\'')
        {
            if (i + 1 < pattern.size() && pattern[i + 1] == '\'')
            {
                out += '\'';
                i += 2;
                continue;
            }
            return i + 1;
        }
        out += pattern[i++];
    }
    return i;
}

// The leading character of a UTF-8 string, for the `t` designator letter.
std::string first_character(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }
    std::size_t n = 1;
    while (n < text.size() && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
    {
        ++n;
    }
    return text.substr(0, n);
}
} // namespace

bool DateFormatSymbols::twelve_hour_clock() const
{
    bool quoted = false;
    for (const char c : short_time_pattern)
    {
        if (c == '\'')
        {
            quoted = !quoted;
        }
        else if (!quoted && c == 'h')
        {
            return true;
        }
        else if (!quoted && c == 'H')
        {
            return false;
        }
    }
    return false;
}

const DateFormatSymbols& regional_date_format()
{
    return regional_dates().symbols;
}

void set_regional_date_format(DateFormatSymbols symbols)
{
    if (symbols == regional_dates().symbols)
    {
        return;
    }
    regional_dates().symbols = std::move(symbols);
    regional_dates().changed.raise();
}

const Signal& regional_date_format_changed()
{
    return regional_dates().changed;
}

std::string format_date(std::chrono::year_month_day date, std::string_view pattern, const DateFormatSymbols& symbols)
{
    if (!date.ok())
    {
        return {};
    }
    const int      year    = static_cast<int>(date.year());
    const unsigned month   = static_cast<unsigned>(date.month());
    const unsigned day     = static_cast<unsigned>(date.day());
    const unsigned weekday = std::chrono::weekday{std::chrono::sys_days{date}}.c_encoding();
    std::string    out;
    for (std::size_t i = 0; i < pattern.size();)
    {
        const char c = pattern[i];
        if (c == '\'')
        {
            i = append_literal(out, pattern, i);
            continue;
        }
        const std::size_t n = (c == 'd' || c == 'M' || c == 'y') ? run_length(pattern, i) : 1;
        switch (c)
        {
            case 'd':
                if (n >= 4)
                {
                    out += symbols.day_names[weekday];
                }
                else if (n == 3)
                {
                    out += symbols.abbreviated_day_names[weekday];
                }
                else
                {
                    append_number(out, day, n);
                }
                break;
            case 'M':
                if (n >= 4)
                {
                    out += symbols.month_names[month - 1];
                }
                else if (n == 3)
                {
                    out += symbols.abbreviated_month_names[month - 1];
                }
                else
                {
                    append_number(out, month, n);
                }
                break;
            case 'y':
                if (n <= 2)
                {
                    append_number(out, ((year % 100) + 100) % 100, n);
                }
                else
                {
                    append_number(out, year, n);
                }
                break;
            default:
                out += c;
                break;
        }
        i += n;
    }
    return out;
}

std::string format_time(std::chrono::minutes time, std::string_view pattern, const DateFormatSymbols& symbols)
{
    const long long    minutes_of_day = ((time.count() % 1440) + 1440) % 1440;
    const long long    hour           = minutes_of_day / 60;
    const long long    minute         = minutes_of_day % 60;
    const std::string& designator     = hour < 12 ? symbols.am_designator : symbols.pm_designator;
    std::string        out;
    for (std::size_t i = 0; i < pattern.size();)
    {
        const char c = pattern[i];
        if (c == '\'')
        {
            i = append_literal(out, pattern, i);
            continue;
        }
        const bool        letter = c == 'h' || c == 'H' || c == 'm' || c == 't' || c == 's';
        const std::size_t n      = letter ? run_length(pattern, i) : 1;
        switch (c)
        {
            case 'h':
                append_number(out, hour % 12 == 0 ? 12 : hour % 12, std::min<std::size_t>(n, 2));
                break;
            case 'H':
                append_number(out, hour, std::min<std::size_t>(n, 2));
                break;
            case 'm':
                append_number(out, minute, std::min<std::size_t>(n, 2));
                break;
            case 's':
                append_number(out, 0, std::min<std::size_t>(n, 2));
                break;
            case 't':
                out += n >= 2 ? designator : first_character(designator);
                break;
            default:
                out += c;
                break;
        }
        i += n;
    }
    return out;
}

// The C runtime's local time: MSVC's std::chrono::current_zone() needs the ICU that only ships
// from Windows 10 1903, and the library runs on 1703.
std::chrono::year_month_day today()
{
    const std::time_t now = std::time(nullptr);
    std::tm           local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return std::chrono::year_month_day{std::chrono::year{local.tm_year + 1900},
                                       std::chrono::month{static_cast<unsigned>(local.tm_mon + 1)},
                                       std::chrono::day{static_cast<unsigned>(local.tm_mday)}};
}

} // namespace ui
