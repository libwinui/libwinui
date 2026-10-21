// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestClipboard.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/PasswordBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <string>
#include <string_view>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;

namespace
{
const HeadlessTextMeasurer m;

void lay_out(PasswordBoxControl& box, float width = 300.0f)
{
    const Size size = box.measure({width, Unbounded}, m, Theme::default_light());
    box.arrange({0.0f, 0.0f, width, size.height});
}

void type(PasswordBoxControl& box, const std::string& text)
{
    // One code point a keystroke, as WM_CHAR delivers them.
    for (std::size_t i = 0; i < text.size(); i = next_code_point(text, i))
    {
        box.handle_text(std::string_view{text}.substr(i, next_code_point(text, i) - i));
    }
}

bool draws_text(PasswordBoxControl& box, const std::string& text)
{
    TestRenderer r;
    box.render(r, Theme::light());
    for (const auto& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t && t->text == text)
        {
            return true;
        }
    }
    return false;
}

PointerEvent at(PointerAction action, Point p)
{
    return PointerEvent{p, PointerButton::Left, action};
}
} // namespace

TEST_CASE("A PasswordBox masks what is typed and never copies it", "[passwordbox]")
{
    PasswordBoxControl box;
    lay_out(box);
    ui::testing::set_focused(box, true);
    type(box, "hunter2");
    REQUIRE(box.password() == "hunter2");
    const std::string seven_dots = "\u25CF\u25CF\u25CF\u25CF\u25CF\u25CF\u25CF"; // one per code point
    REQUIRE(draws_text(box, seven_dots));
    REQUIRE_FALSE(draws_text(box, "hunter2"));
    REQUIRE(ui::automation_providers(box).value->text_value() == seven_dots);

    TestClipboard clipboard;
    ui::testing::set_clipboard(&clipboard);
    box.handle_key(KeyEvent{Key::A, KeyAction::Down, {.ctrl = true}});
    box.handle_key(KeyEvent{Key::C, KeyAction::Down, {.ctrl = true}});
    REQUIRE_FALSE(clipboard.text().has_value());

    box.set_password_char('#');
    REQUIRE(draws_text(box, "#######"));
}

TEST_CASE("The RevealButton peeks while held, and only at a password typed since focus", "[passwordbox]")
{
    PasswordBoxControl box;
    box.set_password("preset");
    lay_out(box);
    ui::testing::set_focused(box, true);
    REQUIRE_FALSE(box.reveal_button_visible()); // already there when focus arrived

    box.handle_key(KeyEvent{Key::A, KeyAction::Down, {.ctrl = true}});
    box.handle_key(KeyEvent{Key::Delete, KeyAction::Down});
    REQUIRE(box.password().empty());
    type(box, "new");
    REQUIRE(box.reveal_button_visible());

    const Point on_button = box.reveal_button_rect().center();
    box.handle_pointer(at(PointerAction::Down, on_button));
    REQUIRE(box.password_revealed());
    REQUIRE(draws_text(box, "new"));
    box.handle_pointer(at(PointerAction::Up, on_button));
    REQUIRE_FALSE(box.password_revealed());

    ui::testing::set_focused(box, false);
    ui::testing::set_focused(box, true);
    REQUIRE_FALSE(box.reveal_button_visible());
}

TEST_CASE("PasswordBoxRevealMode Visible shows the text, Hidden never offers the button", "[passwordbox]")
{
    PasswordBoxControl box;
    lay_out(box);
    ui::testing::set_focused(box, true);
    type(box, "abc");

    box.set_reveal_mode(PasswordBoxRevealMode::Visible);
    REQUIRE(box.password_revealed());
    REQUIRE(draws_text(box, "abc"));
    REQUIRE_FALSE(box.reveal_button_visible());

    box.set_reveal_mode(PasswordBoxRevealMode::Hidden);
    REQUIRE_FALSE(box.password_revealed());
    REQUIRE_FALSE(box.reveal_button_visible());
}

TEST_CASE("PasswordChanged fires on edits and may replace the password", "[passwordbox]")
{
    PasswordBoxControl box;
    std::string        seen;
    box.on_password_changed(
        [&](const std::string& password)
        {
            seen = password;
            if (password == "Password")
            {
                box.set_password("");
            }
        });
    lay_out(box);
    ui::testing::set_focused(box, true);
    type(box, "Password");
    REQUIRE(seen == "Password");
    REQUIRE(box.password().empty());
    REQUIRE_FALSE(box.reveal_button_visible());
}

TEST_CASE("ui::PasswordBox builds with its header, placeholder and character", "[passwordbox][spec]")
{
    ui::Control<ui::PasswordBox> box;
    auto                         node = ui::build(ui::PasswordBox{.header           = "Password",
                                                                  .placeholder_text = "Enter your password",
                                                                  .password_char    = '#',
                                                                  .reveal_mode      = PasswordBoxRevealMode::Hidden,
                                                                  .control          = box});
    REQUIRE(box->header() == "Password");
    REQUIRE(box->placeholder_text() == "Enter your password");
    REQUIRE(box->password_char() == '#');
    REQUIRE(box->reveal_mode() == PasswordBoxRevealMode::Hidden);
}

TEST_CASE("PasswordBox's on_submit fires on Enter and only then claims the key", "[password_box][spec]")
{
    int                          submitted = 0;
    ui::Control<ui::PasswordBox> plain;
    ui::Control<ui::PasswordBox> submitting;
    auto                         a = ui::build(ui::PasswordBox{.control = plain});
    auto b = ui::build(ui::PasswordBox{.on_submit = [&] { ++submitted; }, .control = submitting});

    ui::testing::set_focused(*plain, true);
    CHECK_FALSE(plain->handle_key(KeyEvent{Key::Enter, KeyAction::Down})); // Enter reaches the default button

    ui::testing::set_focused(*submitting, true);
    CHECK(submitting->handle_key(KeyEvent{Key::Enter, KeyAction::Down}));
    CHECK(submitted == 0); // on release, as a button
    submitting->handle_key(KeyEvent{Key::Enter, KeyAction::Up});
    CHECK(submitted == 1);
}
