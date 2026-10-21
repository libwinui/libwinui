// SPDX-License-Identifier: MIT
#include "winui/core/DateFormat.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/datetime/TimePicker.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace ui;
using namespace ui::testing;
using namespace std::chrono_literals;

namespace
{
KeyEvent key(Key k)
{
    return KeyEvent{k, KeyAction::Down};
}

struct RegionalDateGuard
{
    DateFormatSymbols saved = regional_date_format();
    RegionalDateGuard()
    {
        set_regional_date_format(DateFormatSymbols::invariant()); // "h:mm tt"
    }
    ~RegionalDateGuard()
    {
        set_regional_date_format(saved);
    }
};
} // namespace

TEST_CASE("A TimePicker rounds down to its minute increment", "[time-picker]")
{
    RegionalDateGuard guard;
    TimePickerControl picker;
    CHECK(picker.clock() == TimePickerClock::TwelveHour); // the invariant "h:mm tt"
    picker.set_minute_increment(15);
    picker.set_selected_time(7h + 38min);
    CHECK(picker.selected_time() == 7h + 30min);
    picker.set_selected_time(25h + 5min); // whole days are dropped
    CHECK(picker.selected_time() == 1h);
    picker.set_minute_increment(0);
    picker.set_selected_time(9h + 59min);
    CHECK(picker.selected_time() == 9h);
    picker.set_minute_increment(75); // ignored
    CHECK(picker.minute_increment() == 0);
}

TEST_CASE("The flyout picks hour, minute and AM or PM", "[time-picker][flyout]")
{
    RegionalDateGuard                             guard;
    HeadlessUI                                    ui;
    Control<TimePicker>                           picker;
    Property<std::optional<std::chrono::minutes>> time{9h + 15min};
    ui.window().set_theme(ThemeRequest{.variant = ThemeVariant::Fluent2, .animations = AnimationPreference::Off});
    ui.window().set_content(TimePicker{.selected_time = time, .minute_increment = 15, .control = picker});
    ui.layout({600.0f, 700.0f});
    REQUIRE(picker->focus());

    // Each key is followed by a layout, as a frame follows input: the flyout takes focus there.
    const auto press = [&](Key k)
    {
        seams(ui.window()).handle_key(key(k));
        ui.layout({600.0f, 700.0f});
    };
    press(Key::Enter);
    REQUIRE(picker->is_open());
    press(Key::ArrowDown); // the hour: 10
    press(Key::ArrowRight);
    press(Key::ArrowDown); // the minute: 30
    press(Key::ArrowRight);
    press(Key::ArrowDown); // PM
    press(Key::Enter);
    CHECK(time.get() == 22h + 30min);
}

TEST_CASE("The flyout's columns slide to the new value while animations run", "[time-picker][flyout][animation]")
{
    RegionalDateGuard                             guard;
    HeadlessUI                                    ui;
    Control<TimePicker>                           picker;
    Property<std::optional<std::chrono::minutes>> time{9h + 15min};
    ui.window().set_theme(ThemeRequest{.variant = ThemeVariant::Fluent2, .animations = AnimationPreference::On});
    ui.window().set_content(TimePicker{.selected_time = time, .control = picker});
    ui.layout({600.0f, 700.0f});
    REQUIRE(picker->focus());
    seams(ui.window()).handle_key(key(Key::Enter));
    ui.layout({600.0f, 700.0f});
    REQUIRE(picker->is_open());
    seams(ui.window()).handle_key(key(Key::ArrowDown)); // the hour: 10
    for (int i = 0; i < 60; ++i)
    {
        (void)ui.window().tick(16ms);
    }

    // The band is the accent fill; the hour drawn on it is the one the band's text box holds.
    const Theme& theme = ui.window().theme();
    TestRenderer r;
    Element*     flyout = popup_content(*picker);
    REQUIRE(flyout != nullptr);
    flyout->render(r, theme);
    std::optional<Rect> band;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&command); fill && fill->color == theme.colors().accent)
        {
            band = fill->bounds;
        }
    }
    REQUIRE(band);
    std::vector<std::string> on_band;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* run = std::get_if<DrawTextRun>(&command);
            run && run->color == theme.colors().text_on_accent && std::abs(run->bounds.y - (band->y + 3.0f)) < 0.5f)
        {
            on_band.push_back(run->text);
        }
    }
    REQUIRE_FALSE(on_band.empty());
    CHECK(on_band.front() == "10");
}

TEST_CASE("The flyout is as wide as its columns' widest items", "[time-picker][flyout]")
{
    RegionalDateGuard   guard;
    HeadlessUI          ui;
    Control<TimePicker> picker;
    ui.window().set_theme(ThemeRequest{.variant = ThemeVariant::Fluent2, .animations = AnimationPreference::Off});
    ui.window().set_content(TimePicker{.horizontal_alignment = Alignment::Leading, .control = picker});
    ui.layout({600.0f, 700.0f});
    // Three centered columns ("hour", "minute", "AM"/"PM") and their padding: well under the old 242.
    CHECK(picker->bounds().width < 200.0f);
    CHECK(picker->bounds().width > 100.0f);
}

TEST_CASE("A 24-hour TimePicker has no AM/PM part", "[time-picker]")
{
    RegionalDateGuard                             guard;
    HeadlessUI                                    ui;
    Control<TimePicker>                           picker;
    Property<std::optional<std::chrono::minutes>> time{23h};
    ui.window().set_theme(ThemeRequest{.variant = ThemeVariant::Windows9x});
    ui.window().set_content(TimePicker{.selected_time = time, .clock = TimePickerClock::TwentyFourHour, .control = picker});
    ui.layout({600.0f, 700.0f});
    REQUIRE(picker->focus());
    // comctl32's field: Up turns the hour, wrapping past 23.
    seams(ui.window()).handle_key(key(Key::ArrowUp));
    CHECK(time.get() == 0min);
    seams(ui.window()).handle_key(key(Key::ArrowRight));
    seams(ui.window()).handle_key(key(Key::ArrowDown));
    CHECK(time.get() == 59min);
}
