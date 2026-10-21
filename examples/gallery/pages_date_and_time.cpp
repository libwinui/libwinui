// SPDX-License-Identifier: MIT
#include "gallery.hpp"

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <cstddef>
#include <ctime>

namespace gallery
{

namespace
{

// One date per line, in the user's own short format.
std::string list_dates(const std::vector<std::chrono::year_month_day>& dates)
{
    std::string text;
    for (const std::chrono::year_month_day& date : dates)
    {
        text += (text.empty() ? "" : "\n") + ui::format_date(date, ui::regional_date_format().short_date_pattern);
    }
    return text.empty() ? std::string{"No date selected."} : text;
}

// ---- CalendarView ----

struct CalendarViewState
{
    ui::Property<std::vector<std::chrono::year_month_day>> dates{};
    ui::Property<bool>                                     group_labels{true};
    ui::Property<bool>                                     out_of_scope{true};
    ui::Property<std::optional<std::size_t>>               mode{1};
};

ui::Spec calendar_view_page(Context&)
{
    return with_state<CalendarViewState>(
        [](CalendarViewState& s)
        {
            ui::Spec options = ui::VerticalStack{
                .spacing         = 8.0f,
                .child_alignment = ui::Alignment::Stretch,
                .width           = 220.0f,
                .content =
                    {
                        ui::CheckBox{.text = "Group labels", .checked = s.group_labels},
                        ui::CheckBox{.text = "Dim other months' days", .checked = s.out_of_scope},
                        ui::ComboBox{.header         = "Selection mode",
                                     .items          = {"None", "Single", "Multiple"},
                                     .selected_index = s.mode},
                    },
            };
            return examples({
                example({
                    .title = "A basic calendar view.",
                    .code  = R"cpp(
                        ui::Property<std::vector<std::chrono::year_month_day>> dates{};
                        ui::Property<bool> group_labels{true};
                        ui::Property<bool> out_of_scope{true};

                        ui::CalendarView{
                            .selected_dates       = dates,
                            .selection_mode       = ui::CalendarViewSelectionMode::Single,
                            .out_of_scope_enabled = out_of_scope,
                            .group_label_visible  = group_labels,
                            .vertical_alignment   = ui::Alignment::Leading,
                        }
                    )cpp",
                    .output = ui::Text{.text = ui::bind(s.dates, list_dates), .wrapping = ui::TextWrapping::WrapWholeWords},
                    .options = std::move(options),
                    .controls =
                        {
                            ui::CalendarView{
                                .selected_dates       = s.dates,
                                .selection_mode       = ui::bind(s.mode,
                                                                 [](const std::optional<std::size_t>& m)
                                                                 {
                                                               return m == 0u ? ui::CalendarViewSelectionMode::None
                                                                      : m == 2u ? ui::CalendarViewSelectionMode::Multiple
                                                                                : ui::CalendarViewSelectionMode::Single;
                                                                 }),
                                .out_of_scope_enabled = s.out_of_scope,
                                .group_label_visible  = s.group_labels,
                                .vertical_alignment   = ui::Alignment::Leading,
                            },
                        },
                }),
            });
        });
}


// ---- CalendarDatePicker ----

struct CalendarDatePickerState
{
    ui::Property<std::optional<std::chrono::year_month_day>> date{};
};

ui::Spec calendar_date_picker_page(Context&)
{
    return with_state<CalendarDatePickerState>(
        [](CalendarDatePickerState& s)
        {
            return examples({
                example({
                    .title = "CalendarDatePicker with a header and placeholder text.",
                    .code  = R"cpp(
                        ui::Property<std::optional<std::chrono::year_month_day>> date{};

                        ui::CalendarDatePicker{
                            .header           = "Calendar",
                            .placeholder_text = "Pick a date",
                            .date             = date,
                        }
                    )cpp",
                    .output = ui::Text{.text = ui::bind(s.date,
                                                        [](const std::optional<std::chrono::year_month_day>& d)
                                                        { return d ? list_dates({*d}) : std::string{"No date picked."}; })},
                    .controls = {ui::CalendarDatePicker{.header = "Calendar", .placeholder_text = "Pick a date", .date = s.date}},
                }),
            });
        });
}


// ---- DatePicker ----

struct DatePickerState
{
    ui::Property<std::optional<std::chrono::year_month_day>> date{};
    ui::Property<std::optional<std::chrono::year_month_day>> later{};
};

std::string show_date(const std::optional<std::chrono::year_month_day>& date)
{
    return date ? list_dates({*date}) : std::string{"No date picked."};
}

ui::Spec date_picker_page(Context&)
{
    return with_state<DatePickerState>(
        [](DatePickerState& s)
        {
            // Two months from today, in a range of this year and the next five.
            const std::chrono::year_month_day today = ui::today();
            const std::chrono::year_month ym = std::chrono::year_month{today.year(), today.month()} + std::chrono::months{2};
            if (!s.later.get())
            {
                s.later = std::chrono::year_month_day{ym.year(), ym.month(), std::chrono::day{1}};
            }
            const int year = static_cast<int>(today.year());
            return examples({
                example({
                    .title    = "A simple DatePicker with a header.",
                    .code     = R"cpp(
                        ui::Property<std::optional<std::chrono::year_month_day>> date{};

                        ui::DatePicker{.header = "Pick a date", .selected_date = date}
                    )cpp",
                    .output   = ui::Text{.text = ui::bind(s.date, show_date)},
                    .controls = {ui::DatePicker{.header = "Pick a date", .selected_date = s.date}},
                }),
                example({
                    .title  = "A DatePicker with the year hidden.",
                    .code   = R"cpp(
                        ui::Property<std::optional<std::chrono::year_month_day>> date{};
                        const int year = static_cast<int>(ui::today().year());

                        ui::DatePicker{
                            .selected_date = date,
                            .min_year      = year,
                            .max_year      = year + 5,
                            .year_visible  = false,
                        }
                    )cpp",
                    .output = ui::Text{.text = ui::bind(s.later, show_date)},
                    .controls = {ui::DatePicker{.selected_date = s.later, .min_year = year, .max_year = year + 5, .year_visible = false}},
                }),
            });
        });
}

// ---- TimePicker ----

struct TimePickerState
{
    ui::Property<std::optional<std::chrono::minutes>> simple{};
    ui::Property<std::optional<std::chrono::minutes>> arrival{};
    ui::Property<std::optional<std::chrono::minutes>> twelve{};
    ui::Property<std::optional<std::chrono::minutes>> twenty_four{};
};

std::string show_time(const std::optional<std::chrono::minutes>& time)
{
    return time ? ui::format_time(*time, ui::regional_date_format().short_time_pattern)
                : std::string{"No time picked."};
}

ui::Spec time_picker_page(Context&)
{
    return with_state<TimePickerState>(
        [](TimePickerState& s)
        {
            if (!s.twelve.get())
            {
                const std::time_t now = std::time(nullptr);
                std::tm           local{};
                localtime_s(&local, &now);
                s.twelve      = std::chrono::minutes{local.tm_hour * 60 + local.tm_min};
                s.twenty_four = s.twelve.get();
            }
            ui::Spec clocks = ui::HorizontalStack{
                .spacing = 8.0f,
                .content =
                    {
                        ui::TimePicker{.header = "12-hour clock", .selected_time = s.twelve, .clock = ui::TimePickerClock::TwelveHour},
                        ui::TimePicker{.header        = "24-hour clock",
                                       .selected_time = s.twenty_four,
                                       .clock         = ui::TimePickerClock::TwentyFourHour},
                    },
            };
            return examples({
                example({
                    .title    = "A simple TimePicker that uses the clock format for the user's region.",
                    .code     = R"cpp(
                        ui::Property<std::optional<std::chrono::minutes>> time{};

                        ui::TimePicker{.selected_time = time}
                    )cpp",
                    .output   = ui::Text{.text = ui::bind(s.simple, show_time)},
                    .controls = {ui::TimePicker{.selected_time = s.simple}},
                }),
                example({
                    .title  = "A TimePicker with a header and minute increments specified.",
                    .code   = R"cpp(
                        ui::Property<std::optional<std::chrono::minutes>> arrival{};

                        ui::TimePicker{.header = "Arrival time", .selected_time = arrival, .minute_increment = 15}
                    )cpp",
                    .output = ui::Text{.text = ui::bind(s.arrival, show_time)},
                    .controls = {ui::TimePicker{.header = "Arrival time", .selected_time = s.arrival, .minute_increment = 15}},
                }),
                example({
                    .title    = "Use clock to override the regional clock format.",
                    .code     = R"cpp(
                        ui::Property<std::optional<std::chrono::minutes>> twelve{};
                        ui::Property<std::optional<std::chrono::minutes>> twenty_four{};

                        ui::HorizontalStack{
                            .spacing = 8.0f,
                            .content =
                                {
                                    ui::TimePicker{.header        = "12-hour clock",
                                                   .selected_time = twelve,
                                                   .clock         = ui::TimePickerClock::TwelveHour},
                                    ui::TimePicker{.header        = "24-hour clock",
                                                   .selected_time = twenty_four,
                                                   .clock         = ui::TimePickerClock::TwentyFourHour},
                                },
                        }
                    )cpp",
                    .controls = {std::move(clocks)},
                }),
            });
        });
}

} // namespace

std::span<const Page> date_and_time_pages()
{
    static const auto Pages = std::to_array<Page>({
        {"CalendarDatePicker", "A date field that opens a calendar to pick the date from.", calendar_date_picker_page},
        {"CalendarView", "A calendar for picking dates from a month, a year or a decade.", calendar_view_page},
        {"DatePicker", "A date picked from spinning columns for the day, the month and the year.", date_picker_page},
        {"TimePicker", "A time of day picked from spinning columns for the hour, the minute and AM or PM.", time_picker_page},
    });
    return Pages;
}

} // namespace gallery
