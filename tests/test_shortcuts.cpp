// SPDX-License-Identifier: MIT
// ui::Shortcuts (shortcuts for one area) and Window::add_command() (a command's shortcut, window-wide).
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/containers/Shortcuts.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/TextBox.hpp"

#include <memory>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

namespace
{
bool press(Window& window, KeyCombo keys)
{
    return seams(window).handle_key(KeyEvent{.key = keys.key, .modifiers = keys.modifiers});
}
} // namespace

TEST_CASE("ui::Shortcuts fires while focus is inside its content, and only then", "[shortcut][spec]")
{
    HeadlessTextMeasurer     measurer;
    Window                   window;
    int                      sent = 0;
    ui::Control<ui::TextBox> inside;
    ui::Control<ui::Button>  outside;
    window.set_content(ui::VerticalStack{
        .content =
            {
                ui::Shortcuts{
                    .items   = {ui::Shortcut{.keys = Ctrl + Key::Enter, .on_pressed = [&] { ++sent; }}},
                    .content = ui::TextBox{.control = inside},
                },
                ui::Button{.text = "Elsewhere", .control = outside},
            },
    });
    window.layout({400.0f, 300.0f}, measurer);

    window.focus_element(outside);
    press(window, Ctrl + Key::Enter); // the button may take it, the shortcut does not
    CHECK(sent == 0);

    window.focus_element(inside);
    CHECK(press(window, Ctrl + Key::Enter));
    CHECK(sent == 1);
}

TEST_CASE("A shortcut that returns NotHandled lets the key go on to the area around it", "[shortcut][spec]")
{
    HeadlessTextMeasurer     measurer;
    Window                   window;
    int                      inner = 0;
    int                      outer = 0;
    ui::Control<ui::TextBox> box;
    window.set_content(ui::Shortcuts{
        .items = {ui::Shortcut{.keys = Key::F2, .on_pressed = [&] { ++outer; }}},
        .content =
            ui::Shortcuts{
                .items   = {ui::Shortcut{.keys = Key::F2,
                                         .on_pressed =
                                             [&]
                                         {
                                           ++inner;
                                           return inner > 1 ? ui::NotHandled : ui::Handled;
                                         }}},
                .content = ui::TextBox{.control = box},
            },
    });
    window.layout({400.0f, 300.0f}, measurer);
    window.focus_element(box);

    press(window, Key::F2);
    CHECK((inner == 1 && outer == 0));
    press(window, Key::F2);
    CHECK((inner == 2 && outer == 1));
}

TEST_CASE("Window::add_command fires a command's shortcut wherever focus is, while enabled and registered",
          "[shortcut][command]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    Property<bool>       can_reload{true};
    int                  reloads = 0;
    Command reload{.text = "Reload", .shortcut = Key::F5, .enabled = can_reload, .execute = [&] { ++reloads; }};
    window.set_content(ui::VerticalStack{.content = {ui::Button{.text = "Unrelated"}}});
    window.layout({400.0f, 300.0f}, measurer);

    CHECK_FALSE(press(window, Key::F5)); // not registered yet
    Subscription registered = window.add_command(reload);
    CHECK(press(window, Key::F5));
    CHECK(reloads == 1);

    can_reload = false;
    CHECK_FALSE(press(window, Key::F5));
    can_reload = true;
    registered.reset();
    CHECK_FALSE(press(window, Key::F5));
    CHECK(reloads == 1);
}
