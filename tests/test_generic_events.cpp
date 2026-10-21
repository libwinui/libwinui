// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/containers/Border.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

TEST_CASE("every spec raises pointer entered and exited", "[spec][events]")
{
    HeadlessUI ui      = create_headless();
    int        entered = 0;
    int        exited  = 0;
    ui.window().set_content(
        ui::Text{.text                 = "Hover me",
                 .width                = 100,
                 .height               = 30,
                 .horizontal_alignment = Alignment::Leading,
                 .vertical_alignment   = Alignment::Leading,
                 .on_pointer_entered   = [&] { ++entered; },
                 .on_pointer_exited    = [&] { ++exited; }});
    ui.layout({400, 300});

    ui::testing::seams(ui.window()).handle_pointer({Point{10.0f, 10.0f}, PointerButton::None, PointerAction::Move});
    CHECK(entered == 1);
    ui::testing::seams(ui.window()).handle_pointer({Point{300.0f, 200.0f}, PointerButton::None, PointerAction::Move});
    CHECK(exited == 1);
}

TEST_CASE("focus events fire as focus enters and leaves a control or anything inside it", "[spec][events][focus]")
{
    HeadlessUI               ui = create_headless();
    std::vector<std::string> log;
    ui::Control<ui::Button>  first;
    ui::Control<ui::Button>  second;
    ui::Control<ui::TextBox> name;

    ui.window().set_content(ui::VerticalStack{
        .content = {
            ui::Border{
                .on_got_focus  = [&] { log.push_back("card got"); },
                .on_lost_focus = [&] { log.push_back("card lost"); },
                .content = ui::VerticalStack{.content =
                                                 {
                                                     ui::Button{.text          = "One",
                                                                .on_got_focus  = [&] { log.push_back("one got"); },
                                                                .on_lost_focus = [&] { log.push_back("one lost"); },
                                                                .control       = first},
                                                     ui::Button{.text = "Two", .control = second},
                                                 }},
            },
            ui::TextBox{.on_got_focus  = [&] { log.push_back("name got"); },
                        .on_lost_focus = [&] { log.push_back("name lost"); },
                        .control       = name},
            ui::Text{.text = "never focused", .on_got_focus = [&] { log.push_back("text got"); }},
        }});
    ui.layout({400, 600});
    Window& win = ui.window();

    win.focus_element(first);
    CHECK(log == std::vector<std::string>{"one got", "card got"});

    // Within the card: the card hears nothing.
    log.clear();
    win.focus_element(second);
    CHECK(log == std::vector<std::string>{"one lost"});

    // Out of the card into the box.
    log.clear();
    win.focus_element(name);
    CHECK(log == std::vector<std::string>{"card lost", "name got"});

    // A handler sees the control already focused.
    bool focused_in_handler = false;
    name->on_got_focus([&] { focused_in_handler = name->focused(); });
    win.focus_element(second);
    win.focus_element(name);
    CHECK(focused_in_handler);
}
