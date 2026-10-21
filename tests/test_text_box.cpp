// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestClipboard.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;

namespace
{
PointerEvent move(Point p)
{
    return {p, PointerButton::None, PointerAction::Move};
}
PointerEvent down(Point p, int click_count = 1)
{
    PointerEvent e{p, PointerButton::Left, PointerAction::Down};
    e.click_count = click_count;
    return e;
}
PointerEvent shift_down(Point p)
{
    PointerEvent e{p, PointerButton::Left, PointerAction::Down};
    e.modifiers.shift = true;
    return e;
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}
KeyEvent key_down(Key k)
{
    return {k, KeyAction::Down};
}
KeyEvent key_up(Key k)
{
    return {k, KeyAction::Up};
}
KeyEvent shift_key_down(Key k)
{
    KeyEvent e{k, KeyAction::Down};
    e.modifiers.shift = true;
    return e;
}
KeyEvent ctrl_key_down(Key k)
{
    KeyEvent e{k, KeyAction::Down};
    e.modifiers.ctrl = true;
    return e;
}

constexpr float FontSize = 14.0f; // Theme::light()'s default text_style.font_size
constexpr float CharW    = FontSize * HeadlessTextMeasurer::AdvanceRatio;
constexpr float LeftPad  = 11.0f; // Theme::light()'s default padding.left

// Static because TextBox caches the measurer pointer it was measured with, for render() and
// handle_pointer().
TextBoxControl make_text_box(std::string text = "Hello")
{
    static HeadlessTextMeasurer measurer;
    TextBoxControl              tb{std::move(text)};
    const Size                  size = tb.measure({200.0f, Unbounded}, measurer, Theme::default_light());
    tb.arrange({100.0f, 100.0f, size.width, size.height});
    ui::testing::set_focused(tb, true);
    return tb;
}

// A quarter character into the target's span: hit_test() compares exact prefix widths, and a
// point right on a boundary can round to the wrong side.
Point caret_point(const TextBoxControl& tb, std::size_t index)
{
    const Rect b = tb.bounds();
    return Point{b.left() + LeftPad + (static_cast<float>(index) + 0.25f) * CharW, b.center().y};
}
} // namespace

TEST_CASE("New TextBox holds its constructed text with the caret at the end", "[textbox]")
{
    TextBoxControl tb = make_text_box("Hello");
    REQUIRE(tb.text() == "Hello");
    REQUIRE(tb.caret() == 5);
    REQUIRE_FALSE(tb.has_selection());
    REQUIRE(tb.enabled());
}

TEST_CASE("Typing inserts at the caret and fires on_text_changed", "[textbox][edit]")
{
    TextBoxControl           tb = make_text_box("Hlo");
    std::vector<std::string> observed;
    tb.on_text_changed([&](const std::string& t) { observed.push_back(t); });

    tb.handle_pointer(down(caret_point(tb, 1))); // caret after 'H'
    tb.handle_pointer(up(caret_point(tb, 1)));
    tb.handle_text("e");
    tb.handle_text("l");

    REQUIRE(tb.text() == "Hello");
    REQUIRE(tb.caret() == 3);
    REQUIRE(observed == (std::vector<std::string>{"Helo", "Hello"}));
}

TEST_CASE("handle_text ignores control characters below 0x20", "[textbox][edit]")
{
    TextBoxControl tb = make_text_box("Hi");
    tb.handle_text("\x08"); // Backspace's char code, handled via handle_key instead
    tb.handle_text("\x0D"); // Enter's char code
    REQUIRE(tb.text() == "Hi");
}

TEST_CASE("Backspace deletes the character before the caret", "[textbox][edit]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_pointer(down(caret_point(tb, 3)));
    tb.handle_pointer(up(caret_point(tb, 3)));
    tb.handle_key(key_down(Key::Backspace));

    REQUIRE(tb.text() == "Helo");
    REQUIRE(tb.caret() == 2);
}

TEST_CASE("Delete removes the character after the caret", "[textbox][edit]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_pointer(down(caret_point(tb, 1)));
    tb.handle_pointer(up(caret_point(tb, 1)));
    tb.handle_key(key_down(Key::Delete));

    REQUIRE(tb.text() == "Hllo");
    REQUIRE(tb.caret() == 1);
}

TEST_CASE("Backspace/Delete at the boundaries are no-ops", "[textbox][edit]")
{
    TextBoxControl tb = make_text_box("Hi");
    tb.handle_pointer(down(caret_point(tb, 0)));
    tb.handle_pointer(up(caret_point(tb, 0)));
    tb.handle_key(key_down(Key::Backspace));
    REQUIRE(tb.text() == "Hi");

    tb.handle_pointer(down(caret_point(tb, 2)));
    tb.handle_pointer(up(caret_point(tb, 2)));
    tb.handle_key(key_down(Key::Delete));
    REQUIRE(tb.text() == "Hi");
}

TEST_CASE("Home/End jump the caret to the start/end", "[textbox][keyboard]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_key(key_down(Key::Home));
    REQUIRE(tb.caret() == 0);
    tb.handle_key(key_down(Key::End));
    REQUIRE(tb.caret() == 5);
}

TEST_CASE("Left/Right move the caret by one and collapse any selection", "[textbox][keyboard]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_key(key_down(Key::Home));
    tb.handle_key(key_down(Key::ArrowRight));
    tb.handle_key(key_down(Key::ArrowRight));
    REQUIRE(tb.caret() == 2);

    tb.handle_key(key_down(Key::ArrowLeft));
    REQUIRE(tb.caret() == 1);
    REQUIRE_FALSE(tb.has_selection());
}

TEST_CASE("ArrowLeft/Right clamp at the text boundaries", "[textbox][keyboard]")
{
    TextBoxControl tb = make_text_box("Hi");
    tb.handle_key(key_down(Key::Home));
    tb.handle_key(key_down(Key::ArrowLeft));
    REQUIRE(tb.caret() == 0);

    tb.handle_key(key_down(Key::End));
    tb.handle_key(key_down(Key::ArrowRight));
    REQUIRE(tb.caret() == 2);
}

TEST_CASE("Clicking positions the caret via the measurer", "[textbox][pointer]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_pointer(down(caret_point(tb, 3)));
    tb.handle_pointer(up(caret_point(tb, 3)));
    REQUIRE(tb.caret() == 3);
    REQUIRE_FALSE(tb.has_selection());
}

TEST_CASE("Dragging after a click extends the selection", "[textbox][pointer]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_pointer(down(caret_point(tb, 1)));
    tb.handle_pointer(move(caret_point(tb, 4)));
    tb.handle_pointer(up(caret_point(tb, 4)));

    REQUIRE(tb.has_selection());
    REQUIRE(tb.selection_range() == std::pair<std::size_t, std::size_t>{1, 4});
    REQUIRE(tb.caret() == 4);
}

TEST_CASE("Backspace over a selection deletes the whole range", "[textbox][edit]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_pointer(down(caret_point(tb, 1)));
    tb.handle_pointer(move(caret_point(tb, 4)));
    tb.handle_pointer(up(caret_point(tb, 4)));

    tb.handle_key(key_down(Key::Backspace));
    REQUIRE(tb.text() == "Ho");
    REQUIRE(tb.caret() == 1);
    REQUIRE_FALSE(tb.has_selection());
}

TEST_CASE("Typing over a selection replaces it", "[textbox][edit]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_pointer(down(caret_point(tb, 1)));
    tb.handle_pointer(move(caret_point(tb, 4)));
    tb.handle_pointer(up(caret_point(tb, 4)));

    tb.handle_text("i");
    REQUIRE(tb.text() == "Hio");
    REQUIRE(tb.caret() == 2);
}

TEST_CASE("Enter fires on_submit on release, not press", "[textbox][command]")
{
    TextBoxControl tb    = make_text_box("query");
    int            fired = 0;
    tb.on_submit([&] { ++fired; });

    tb.handle_key(key_down(Key::Enter));
    REQUIRE(fired == 0);
    tb.handle_key(key_up(Key::Enter));
    REQUIRE(fired == 1);
}

TEST_CASE("Disabled TextBox ignores pointer, keyboard and char input", "[textbox][state]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.set_enabled(false);

    tb.handle_pointer(down(caret_point(tb, 1)));
    tb.handle_pointer(up(caret_point(tb, 1)));
    tb.handle_key(key_down(Key::Backspace));
    tb.handle_text("x");

    REQUIRE(tb.text() == "Hello");
    REQUIRE_FALSE(tb.enabled());
}

TEST_CASE("Losing focus keeps the text and caret, only key-press state resets", "[textbox][state]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_key(key_down(Key::Enter)); // start a keyboard press
    ui::testing::set_focused(tb, false);

    REQUIRE(tb.text() == "Hello"); // not lost
    int fired = 0;
    tb.on_submit([&] { ++fired; });
    tb.handle_key(key_up(Key::Enter)); // arrives after focus loss: ignored
    REQUIRE(fired == 0);
}

TEST_CASE("set_text moves the caret to the end and does not fire on_text_changed", "[textbox]")
{
    TextBoxControl tb    = make_text_box("Hello");
    int            fired = 0;
    tb.on_text_changed([&](const std::string&) { ++fired; });

    tb.set_text("New value");
    REQUIRE(tb.text() == "New value");
    REQUIRE(tb.caret() == 9);
    REQUIRE(fired == 0);
}

TEST_CASE("Shift+Left/Right/Home/End extend the selection instead of collapsing it", "[textbox][keyboard][selection]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_key(key_down(Key::Home));
    tb.handle_key(key_down(Key::ArrowRight));
    tb.handle_key(key_down(Key::ArrowRight)); // caret/anchor both at 2, no selection yet

    tb.handle_key(shift_key_down(Key::ArrowRight));
    REQUIRE(tb.caret() == 3);
    REQUIRE(tb.selection_range() == std::pair<std::size_t, std::size_t>{2, 3});

    tb.handle_key(shift_key_down(Key::End)); // anchor (2) untouched, caret extends to the end
    REQUIRE(tb.caret() == 5);
    REQUIRE(tb.selection_range() == std::pair<std::size_t, std::size_t>{2, 5});

    tb.handle_key(shift_key_down(Key::Home)); // anchor still 2, caret now on the other side of it
    REQUIRE(tb.caret() == 0);
    REQUIRE(tb.selection_range() == std::pair<std::size_t, std::size_t>{0, 2});
}

TEST_CASE("Plain Left/Right with an active selection collapses to its near boundary", "[textbox][keyboard][selection]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_pointer(down(caret_point(tb, 1)));
    tb.handle_pointer(move(caret_point(tb, 4)));
    tb.handle_pointer(up(caret_point(tb, 4)));
    REQUIRE(tb.selection_range() == std::pair<std::size_t, std::size_t>{1, 4});

    tb.handle_key(key_down(Key::ArrowLeft));
    REQUIRE(tb.caret() == 1);
    REQUIRE_FALSE(tb.has_selection());

    tb.handle_pointer(down(caret_point(tb, 1)));
    tb.handle_pointer(move(caret_point(tb, 4)));
    tb.handle_pointer(up(caret_point(tb, 4)));
    tb.handle_key(key_down(Key::ArrowRight));
    REQUIRE(tb.caret() == 4);
    REQUIRE_FALSE(tb.has_selection());
}

TEST_CASE("Shift+click extends the selection from the existing anchor", "[textbox][pointer][selection]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_pointer(down(caret_point(tb, 1)));
    tb.handle_pointer(up(caret_point(tb, 1)));
    REQUIRE_FALSE(tb.has_selection());

    tb.handle_pointer(shift_down(caret_point(tb, 4)));
    REQUIRE(tb.selection_range() == std::pair<std::size_t, std::size_t>{1, 4});
    REQUIRE(tb.caret() == 4);
}

TEST_CASE("Double-click selects the word under the pointer", "[textbox][pointer][selection]")
{
    TextBoxControl tb = make_text_box("Hello world");
    tb.handle_pointer(down(caret_point(tb, 8), 2)); // inside "world"
    tb.handle_pointer(up(caret_point(tb, 8)));

    REQUIRE(tb.selection_range() == std::pair<std::size_t, std::size_t>{6, 11});
    REQUIRE(tb.caret() == 11);
}

TEST_CASE("Triple-click selects the whole text", "[textbox][pointer][selection]")
{
    TextBoxControl tb = make_text_box("Hello world");
    tb.handle_pointer(down(caret_point(tb, 8), 3));
    tb.handle_pointer(up(caret_point(tb, 8)));

    REQUIRE(tb.selection_range() == std::pair<std::size_t, std::size_t>{0, 11});
    REQUIRE(tb.caret() == 11);
}

TEST_CASE("Ctrl+A selects the whole text", "[textbox][keyboard][selection]")
{
    TextBoxControl tb = make_text_box("Hello world");
    tb.handle_key(key_down(Key::Home)); // move off the end-of-construction caret first
    tb.handle_key(ctrl_key_down(Key::A));

    REQUIRE(tb.selection_range() == std::pair<std::size_t, std::size_t>{0, 11});
    REQUIRE(tb.caret() == 11);
}

TEST_CASE("Plain A (no Ctrl) does not select anything", "[textbox][keyboard][selection]")
{
    TextBoxControl tb = make_text_box("Hi");
    tb.handle_key(key_down(Key::Home));
    tb.handle_key(key_down(Key::A)); // no ctrl: falls through to WM_CHAR for typing

    REQUIRE_FALSE(tb.has_selection());
    REQUIRE(tb.caret() == 0);
}

TEST_CASE("Ctrl+C copies the selection without changing the text", "[textbox][keyboard][clipboard]")
{
    TextBoxControl tb = make_text_box("Hello world");
    TestClipboard  clipboard;
    ui::testing::set_clipboard(&clipboard);

    tb.handle_pointer(down(caret_point(tb, 0)));
    tb.handle_pointer(move(caret_point(tb, 5)));
    tb.handle_pointer(up(caret_point(tb, 5)));
    tb.handle_key(ctrl_key_down(Key::C));

    REQUIRE(clipboard.text() == "Hello");
    REQUIRE(tb.text() == "Hello world"); // copy never mutates
}

TEST_CASE("Ctrl+X cuts the selection into the clipboard", "[textbox][keyboard][clipboard]")
{
    TextBoxControl tb = make_text_box("Hello world");
    TestClipboard  clipboard;
    ui::testing::set_clipboard(&clipboard);

    tb.handle_pointer(down(caret_point(tb, 0)));
    tb.handle_pointer(move(caret_point(tb, 5)));
    tb.handle_pointer(up(caret_point(tb, 5)));
    tb.handle_key(ctrl_key_down(Key::X));

    REQUIRE(clipboard.text() == "Hello");
    REQUIRE(tb.text() == " world");
    REQUIRE(tb.caret() == 0);
}

TEST_CASE("Ctrl+C/X with nothing selected is a no-op", "[textbox][keyboard][clipboard]")
{
    TextBoxControl tb = make_text_box("Hi");
    TestClipboard  clipboard;
    ui::testing::set_clipboard(&clipboard);

    tb.handle_key(key_down(Key::Home));
    tb.handle_key(ctrl_key_down(Key::C));
    tb.handle_key(ctrl_key_down(Key::X));

    REQUIRE_FALSE(clipboard.text().has_value());
    REQUIRE(tb.text() == "Hi");
}

TEST_CASE("Ctrl+V pastes the clipboard's text at the caret, replacing any selection", "[textbox][keyboard][clipboard]")
{
    TextBoxControl tb = make_text_box("Hello world");
    TestClipboard  clipboard;
    clipboard.set_text("there");
    ui::testing::set_clipboard(&clipboard);

    tb.handle_pointer(down(caret_point(tb, 0)));
    tb.handle_pointer(move(caret_point(tb, 5)));
    tb.handle_pointer(up(caret_point(tb, 5)));
    tb.handle_key(ctrl_key_down(Key::V));

    REQUIRE(tb.text() == "there world");
    REQUIRE(tb.caret() == 5);
    REQUIRE_FALSE(tb.has_selection());
}

TEST_CASE("Ctrl+V strips embedded newlines/control characters from the pasted text", "[textbox][keyboard][clipboard]")
{
    TextBoxControl tb = make_text_box("");
    TestClipboard  clipboard;
    clipboard.set_text("foo\r\nbar");
    ui::testing::set_clipboard(&clipboard);

    tb.handle_key(ctrl_key_down(Key::V));

    REQUIRE(tb.text() == "foobar");
}

TEST_CASE("Ctrl+C/X/V without a synced clipboard are no-ops", "[textbox][keyboard][clipboard]")
{
    TextBoxControl tb = make_text_box("Hello world");
    tb.handle_pointer(down(caret_point(tb, 0)));
    tb.handle_pointer(move(caret_point(tb, 5)));
    tb.handle_pointer(up(caret_point(tb, 5)));

    tb.handle_key(ctrl_key_down(Key::C));
    tb.handle_key(ctrl_key_down(Key::X));
    tb.handle_key(ctrl_key_down(Key::V));

    REQUIRE(tb.text() == "Hello world");
}

TEST_CASE("The caret blinks on a fixed cycle while focused", "[textbox][caret][tick]")
{
    TextBoxControl tb = make_text_box("Hi");
    REQUIRE(tb.caret_visible()); // starts visible on focus gain

    tb.tick(Duration{TextBoxControl::CaretBlinkIntervalMs}); // exactly one interval: toggles off
    REQUIRE_FALSE(tb.caret_visible());

    tb.tick(Duration{TextBoxControl::CaretBlinkIntervalMs}); // and back on
    REQUIRE(tb.caret_visible());
}

TEST_CASE("Typing resets the caret to visible mid-blink", "[textbox][caret][tick]")
{
    TextBoxControl tb = make_text_box("Hi");
    tb.tick(Duration{TextBoxControl::CaretBlinkIntervalMs});
    REQUIRE_FALSE(tb.caret_visible());

    tb.handle_text("!");
    REQUIRE(tb.caret_visible());
}

TEST_CASE("The caret does not blink while unfocused", "[textbox][caret][tick]")
{
    TextBoxControl tb = make_text_box("Hi");
    ui::testing::set_focused(tb, false);

    tb.tick(Duration{TextBoxControl::CaretBlinkIntervalMs * 3.0});
    REQUIRE(tb.caret_visible()); // on_tick() no-ops entirely while unfocused
}

// ---------------------------------------------------------------------------------------
// test_theme.cpp pins the token values, these pin that render() reads the right ones. The
// underline and selection wash must not read `focus_ring`, which belongs to the focus ring.
// ---------------------------------------------------------------------------------------
namespace
{
// The fill/stroke pair TextBoxControl::render() emits for its own chrome, before any text.
std::vector<Color> chrome_fills(const TextBoxControl& box, const Theme& theme)
{
    TestRenderer renderer;
    box.render(renderer, theme);
    std::vector<Color> out;
    for (const RenderCommand& c : renderer.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c))
        {
            out.push_back(f->color);
        }
    }
    return out;
}
} // namespace

TEST_CASE("A focused Fluent TextBox fills opaque and underlines in the accent", "[text_box][render]")
{
    const Palette palette = Palette::fluent_light();
    const Theme   theme   = Theme::fluent(palette);

    TextBoxControl box;
    box.arrange(Rect{0.0f, 0.0f, 200.0f, 32.0f});

    // Rest: the translucent default control fill.
    REQUIRE_FALSE(chrome_fills(box, theme).empty());
    CHECK(chrome_fills(box, theme).front() == palette.control_fill_default);

    ui::testing::set_focused(box, true);
    const std::vector<Color> focused = chrome_fills(box, theme);
    REQUIRE_FALSE(focused.empty());
    // The focused fill is opaque, so the page stops showing through the field.
    CHECK(focused.front() == palette.control_fill_input_active);
    CHECK(focused.front().a == 1.0f);

    // The 2epx bottom edge is the accent, not the focus ring's outer color (which in Light is a
    // near-black and would read as a hard black underline).
    TestRenderer renderer;
    box.render(renderer, theme);
    bool found_accent_underline = false;
    for (const RenderCommand& c : renderer.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c))
        {
            found_accent_underline = found_accent_underline || f->color == palette.accent_fill_default;
            // The focus ring's outer color, a near-black in Light.
            CHECK_FALSE(f->color == theme.colors().focus);
        }
    }
    CHECK(found_accent_underline);

    box.set_enabled(false);
    const std::vector<Color> disabled = chrome_fills(box, theme);
    REQUIRE_FALSE(disabled.empty());
    CHECK(disabled.front() == palette.control_fill_disabled);
}

TEST_CASE("A TextBox selection is an accent highlight under the selection text color", "[text_box][render]")
{
    const Palette palette = Palette::fluent_light();
    const Theme   theme   = Theme::fluent(palette);

    TextBoxControl box = make_text_box("hello");
    box.handle_key(ctrl_key_down(Key::A));
    REQUIRE(box.has_selection());

    TestRenderer renderer;
    box.render(renderer, theme);
    bool found = false;
    for (const RenderCommand& c : renderer.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c))
        {
            found = found || f->color == theme.colors().selection;
            CHECK_FALSE(f->color == theme.colors().focus);
        }
    }
    CHECK(found);
    // The selected text is drawn again, clipped to the highlight, in the selection text color.
    REQUIRE(renderer.count_of<PushClip>() == 1);
    bool recolored = false;
    for (const RenderCommand& c : renderer.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            recolored = recolored || t->color == theme.colors().selection_text;
        }
    }
    CHECK(recolored);
}

// ---- Multi-line, read-only, fonts, the delete button, password masking ----

namespace
{
TextBoxControl make_multiline(std::string text, TextWrapping wrapping = TextWrapping::NoWrap)
{
    static HeadlessTextMeasurer measurer;
    TextBoxControl              tb{std::move(text)};
    tb.set_accepts_return(true);
    tb.set_wrapping(wrapping);
    const Size size = tb.measure({200.0f, Unbounded}, measurer, Theme::default_light());
    tb.arrange({0.0f, 0.0f, size.width, size.height});
    ui::testing::set_focused(tb, true);
    return tb;
}
} // namespace

TEST_CASE("accepts_return makes Enter a line break and the box a line taller", "[textbox][multiline]")
{
    static HeadlessTextMeasurer measurer;
    TextBoxControl              tb       = make_multiline("one");
    const float                 one_line = tb.measure({200.0f, Unbounded}, measurer, Theme::default_light()).height;
    REQUIRE(tb.handle_key(key_up(Key::ArrowUp)));
    REQUIRE(tb.handle_key(key_up(Key::Enter)));

    bool submitted = false;
    tb.on_submit([&] { submitted = true; });
    tb.handle_key(key_down(Key::Enter));
    tb.handle_key(key_up(Key::Enter));
    tb.handle_text("t");
    REQUIRE(tb.text() == "one\nt");
    REQUIRE_FALSE(submitted);

    // One line sits inside the 32 DIP minimum height; from two on, each adds its height.
    const float two_lines = tb.measure({200.0f, Unbounded}, measurer, Theme::default_light()).height;
    REQUIRE(two_lines > one_line);
    tb.handle_key(key_down(Key::Enter));
    const float three_lines = tb.measure({200.0f, Unbounded}, measurer, Theme::default_light()).height;
    REQUIRE_THAT(three_lines - two_lines, Catch::Matchers::WithinAbs(FontSize * HeadlessTextMeasurer::LineRatio, 0.01));
}

TEST_CASE("Up and Down move between lines, keeping the column", "[textbox][multiline]")
{
    TextBoxControl tb = make_multiline("abcdef\nxy\nlmnopq");
    tb.handle_key(ctrl_key_down(Key::Home));
    REQUIRE(tb.caret() == 0);
    for (int i = 0; i < 4; ++i)
    {
        tb.handle_key(key_down(Key::ArrowRight));
    }
    tb.handle_key(key_down(Key::ArrowDown)); // "xy" is shorter: its end
    REQUIRE(tb.caret() == 9);
    tb.handle_key(key_down(Key::ArrowDown)); // back to column 4 on the long line
    REQUIRE(tb.caret() == 14);
    tb.handle_key(key_down(Key::Home));
    REQUIRE(tb.caret() == 10);
    tb.handle_key(key_down(Key::End));
    REQUIRE(tb.caret() == 16);
    tb.handle_key(key_down(Key::ArrowUp));
    tb.handle_key(key_down(Key::ArrowUp));
    REQUIRE(tb.caret() == 6);
}

TEST_CASE("A wrapping TextBox breaks at its width and pastes line breaks only when it accepts returns",
          "[textbox][multiline]")
{
    static HeadlessTextMeasurer measurer;
    TextBoxControl              tb{"aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk"};
    tb.set_wrapping(TextWrapping::Wrap);
    const Size size = tb.measure({120.0f, Unbounded}, measurer, Theme::default_light());
    REQUIRE(size.height > 3 * FontSize * HeadlessTextMeasurer::LineRatio);

    TestClipboard  clipboard;
    TextBoxControl single = make_text_box("");
    ui::testing::set_clipboard(&clipboard);
    clipboard.set_text("a\r\nb");
    single.handle_key(ctrl_key_down(Key::V));
    REQUIRE(single.text() == "ab");

    TextBoxControl multi = make_multiline("");
    ui::testing::set_clipboard(&clipboard);
    multi.handle_key(ctrl_key_down(Key::V));
    REQUIRE(multi.text() == "a\nb");
}

TEST_CASE("A read-only TextBox selects and copies but never edits or shows a caret", "[textbox]")
{
    TextBoxControl tb = make_text_box("fixed");
    TestClipboard  clipboard;
    ui::testing::set_clipboard(&clipboard);
    tb.set_read_only(true);
    REQUIRE(ui::automation_providers(tb).value->text_value_read_only());

    tb.handle_text("x");
    tb.handle_key(key_down(Key::Backspace));
    tb.handle_key(ctrl_key_down(Key::A));
    tb.handle_key(ctrl_key_down(Key::X));
    REQUIRE(tb.text() == "fixed");
    tb.handle_key(ctrl_key_down(Key::C));
    REQUIRE(clipboard.text() == "fixed");
    REQUIRE_FALSE(tb.delete_button_visible());

    tb.handle_key(key_down(Key::End));
    TestRenderer renderer;
    tb.render(renderer, Theme::light());
    REQUIRE(renderer.count_of<StrokeLine>() == 0); // no caret
}

TEST_CASE("The delete button shows on a focused, non-empty, single-line box and clears it", "[textbox]")
{
    TextBoxControl tb = make_text_box("clear me");
    REQUIRE(tb.delete_button_visible());
    std::string last = "unset";
    tb.on_text_changed([&](const std::string& text) { last = text; });

    const Point on_button = tb.delete_button_rect().center();
    tb.handle_pointer(down(on_button));
    tb.handle_pointer(up(on_button));
    REQUIRE(tb.text().empty());
    REQUIRE(last.empty());
    REQUIRE_FALSE(tb.delete_button_visible()); // nothing left to clear

    TextBoxControl unfocused = make_text_box("x");
    ui::testing::set_focused(unfocused, false);
    REQUIRE_FALSE(unfocused.delete_button_visible());
    TextBoxControl multi = make_multiline("x");
    REQUIRE_FALSE(multi.delete_button_visible());
}

TEST_CASE("The desktop eras draw no delete button", "[textbox]")
{
    static HeadlessTextMeasurer measurer;
    for (const ThemeVariant variant :
         {ThemeVariant::Aero, ThemeVariant::Luna, ThemeVariant::Windows9x, ThemeVariant::Windows3x, ThemeVariant::Windows1x})
    {
        const Theme    theme = resolve_theme(ThemeRequest{.variant = variant}, OsThemeSnapshot{});
        TextBoxControl tb{"clear me"};
        const Size     size = tb.measure({200.0f, Unbounded}, measurer, theme);
        tb.arrange({0.0f, 0.0f, size.width, size.height});
        ui::testing::set_focused(tb, true);
        CHECK_FALSE(tb.delete_button_visible());
    }
}

TEST_CASE("Font properties, the foreground and selection_highlight_color reach the drawing", "[textbox]")
{
    static HeadlessTextMeasurer measurer;
    TextBoxControl              tb{"styled"};
    tb.set_font_family("Arial");
    tb.set_font_size(24.0f);
    tb.set_italic(true);
    tb.set_character_spacing(200);
    tb.set_foreground(Color::from_argb(0xFF5178BE));
    tb.set_selection_highlight_color(Color::from_argb(0xFF008000));
    const Size size = tb.measure({300.0f, Unbounded}, measurer, Theme::default_light());
    REQUIRE(size.height > 24.0f * HeadlessTextMeasurer::LineRatio);
    tb.arrange({0, 0, size.width, size.height});
    ui::testing::set_focused(tb, true);
    tb.handle_key(ctrl_key_down(Key::A));

    TestRenderer renderer;
    tb.render(renderer, Theme::light());
    bool styled = false;
    bool green  = false;
    for (const RenderCommand& c : renderer.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t && t->text == "styled")
        {
            styled = styled || (t->style.font_family == "Arial" && t->style.font_size == 24.0f && t->style.italic &&
                                t->style.character_spacing == 200 && t->color == Color::from_argb(0xFF5178BE));
        }
        if (const auto* f = std::get_if<FillRoundedRect>(&c))
        {
            green = green || f->color == Color::from_argb(0xFF008000);
        }
    }
    REQUIRE(styled);
    REQUIRE(green);
}

TEST_CASE("A password character masks the text and keeps it off the clipboard", "[textbox]")
{
    TextBoxControl tb = make_text_box("secret");
    TestClipboard  clipboard;
    ui::testing::set_clipboard(&clipboard);
    tb.set_password_char(U'\x25CF');
    tb.handle_key(ctrl_key_down(Key::A));
    tb.handle_key(ctrl_key_down(Key::C));
    REQUIRE_FALSE(clipboard.text().has_value());

    TestRenderer renderer;
    tb.render(renderer, Theme::light());
    bool masked = false;
    for (const RenderCommand& c : renderer.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            REQUIRE(t->text != "secret");
            masked = masked || t->text == "\u25CF\u25CF\u25CF\u25CF\u25CF\u25CF";
        }
    }
    REQUIRE(masked);
}

// ---- max_length and character_casing ----

TEST_CASE("max_length stops typing at the limit and cuts a paste to what fits", "[textbox][edit][max-length]")
{
    TextBoxControl tb = make_text_box("abc");
    tb.set_max_length(5);
    tb.handle_key(key_down(Key::End));
    tb.handle_text("d");
    tb.handle_text("e");
    tb.handle_text("f"); // dropped: the box is full
    REQUIRE(tb.text() == "abcde");

    // Replacing a selection frees its room first (TxGetMaxLength counts what the edit leaves).
    TestClipboard clipboard;
    clipboard.set_text("XYZW");
    ui::testing::set_clipboard(&clipboard);
    tb.handle_pointer(down(caret_point(tb, 3)));
    tb.handle_pointer(move(caret_point(tb, 5)));
    tb.handle_pointer(up(caret_point(tb, 5)));
    tb.handle_key(ctrl_key_down(Key::V));
    REQUIRE(tb.text() == "abcXY");
}

TEST_CASE("max_length leaves text set from code alone, and 0 means no limit", "[textbox][edit][max-length]")
{
    TextBoxControl tb = make_text_box("");
    tb.set_max_length(3);
    tb.set_text("longer than three");
    REQUIRE(tb.text() == "longer than three");
    tb.handle_text("!"); // still over the limit: nothing more goes in
    REQUIRE(tb.text() == "longer than three");

    tb.set_max_length(0); // no limit
    REQUIRE(tb.max_length() == 0);
    tb.handle_text("!");
    REQUIRE(tb.text() == "longer than three!");
}

TEST_CASE("max_length counts a line break", "[textbox][edit][max-length]")
{
    TextBoxControl tb = make_text_box("ab");
    tb.set_accepts_return(true);
    tb.set_max_length(2);
    tb.handle_key(key_down(Key::End));
    tb.handle_key(key_down(Key::Enter));
    REQUIRE(tb.text() == "ab");
}

TEST_CASE("CharacterCasing cases what is typed and pasted from then on", "[textbox][edit][casing]")
{
    // Upper turns "a" into "A", Lower then turns
    // "B" into "b", Normal leaves both, and nothing already typed changes case.
    TextBoxControl tb = make_text_box("");
    tb.set_character_casing(CharacterCasing::Upper);
    tb.handle_text("a");
    REQUIRE(tb.text() == "A");
    tb.set_character_casing(CharacterCasing::Lower);
    tb.handle_text("B");
    REQUIRE(tb.text() == "Ab");
    tb.set_character_casing(CharacterCasing::Normal);
    tb.handle_text("a");
    tb.handle_text("B");
    REQUIRE(tb.text() == "AbaB");

    TestClipboard clipboard;
    clipboard.set_text("MiXeD");
    ui::testing::set_clipboard(&clipboard);
    tb.set_character_casing(CharacterCasing::Upper);
    tb.handle_key(ctrl_key_down(Key::V));
    REQUIRE(tb.text() == "AbaBMIXED");
}

TEST_CASE("An Aero text field's whole border is a per-state gradient, darkest on top, at radius 1", "[textbox][aero]")
{
    // Gray at rest, a lighter blue on hover, the focus blue when focused, and no underline: the
    // border is the cue.
    const Theme    theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    TextBoxControl tb{"text"};
    tb.arrange({0.0f, 0.0f, 120.0f, 23.0f});

    const auto border_top = [&]
    {
        TestRenderer renderer;
        tb.render(renderer, theme);
        std::vector<const StrokeRoundedRectGradient*> strokes;
        for (const RenderCommand& c : renderer.commands)
        {
            if (const auto* s = std::get_if<StrokeRoundedRectGradient>(&c))
            {
                strokes.push_back(s);
            }
        }
        REQUIRE(strokes.size() == 1);
        CHECK(strokes[0]->radius == 0.5f); // 1, less the half pen the stroke moves inside by
        CHECK(strokes[0]->extent_px == 20.0f);
        return strokes[0]->brush.stops.front().color;
    };

    CHECK(border_top() == Color::from_argb(0xFFABADB3));
    tb.handle_pointer(move({10.0f, 10.0f}));
    CHECK(border_top() == Color::from_argb(0xFF5794BF));
    ui::testing::set_focused(tb, true);
    CHECK(border_top() == Color::from_argb(0xFF3D7BAD)); // focus wins over hover
}

TEST_CASE("Setting a TextBox's own text again keeps the caret", "[textbox]")
{
    TextBoxControl tb = make_text_box("Hello");
    tb.handle_pointer(down(caret_point(tb, 2)));
    tb.handle_pointer(up(caret_point(tb, 2)));
    REQUIRE(tb.caret() == 2);

    // What a two-way binding does on every keystroke.
    tb.set_text("Hello");
    CHECK(tb.caret() == 2);

    tb.set_text("Help");
    CHECK(tb.caret() == 4);
}

TEST_CASE("A TextBox taller than its text draws a caret one line tall, centered", "[textbox]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    TextBoxControl       box{"Hello"};
    box.set_height(120.0f);
    ui::testing::set_focused(box, true);
    (void)box.measure({200.0f, 120.0f}, m, theme);
    box.arrange({0.0f, 0.0f, 200.0f, 120.0f});

    TestRenderer renderer;
    box.render(renderer, theme);
    const StrokeLine* caret = nullptr;
    for (const RenderCommand& command : renderer.commands)
    {
        if (const auto* line = std::get_if<StrokeLine>(&command); line && line->from.x == line->to.x)
        {
            caret = line;
        }
    }
    REQUIRE(caret != nullptr);
    const float line_height = m.measure("Ag", box.text_style(theme)).height;
    CHECK_THAT(caret->to.y - caret->from.y, Catch::Matchers::WithinAbs(line_height, 0.01));
    CHECK_THAT((caret->from.y + caret->to.y) * 0.5f, Catch::Matchers::WithinAbs(60.0f, 2.0));
}

TEST_CASE("The window reports the focused text box's caret for the input method", "[textbox][ime]")
{
    ui::testing::HeadlessUI ui    = ui::testing::create_headless();
    auto                    owned = std::make_unique<TextBoxControl>("ab");
    TextBoxControl*         box   = owned.get();
    ui.window().set_content(std::move(owned));
    ui.layout({300, 100});
    CHECK_FALSE(ui.window().caret_bounds().has_value()); // nothing focused yet

    ui.window().focus_content();
    const std::optional<Rect> at_end = ui.window().caret_bounds();
    REQUIRE(at_end.has_value());
    CHECK(box->bounds().contains(at_end->center()));

    ui::testing::seams(ui.window()).handle_text("cd");
    const std::optional<Rect> later = ui.window().caret_bounds();
    REQUIRE(later.has_value());
    CHECK(later->x > at_end->x);
    CHECK_THAT(later->y, Catch::Matchers::WithinAbs(at_end->y, 0.01));

    box->set_read_only(true);
    CHECK_FALSE(ui.window().caret_bounds().has_value());
}

TEST_CASE("A theme-resource selection highlight follows the theme", "[textbox][theme]")
{
    static HeadlessTextMeasurer measurer;
    const Theme                 light    = Theme::light();
    const Theme                 dark     = Theme::fluent(Palette::fluent_dark());
    constexpr ThemeColor        resource = [](const Theme& t) { return t.palette.text_fill_secondary; };
    REQUIRE_FALSE(resource(light) == resource(dark));
    const auto has_fill = [](const TestRenderer& r, Color color)
    {
        for (const RenderCommand& c : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&c); f != nullptr && f->color == color)
            {
                return true;
            }
        }
        return false;
    };

    ui::Control<ui::TextBox> box;
    auto built = ui::build(ui::TextBox{.text = "themed", .selection_highlight_color = resource, .control = box});
    REQUIRE(box->selection_highlight_resource() == resource);
    const Size size = box->measure({300.0f, Unbounded}, measurer, light);
    box->arrange({0, 0, size.width, size.height});
    ui::testing::set_focused(*box, true);
    box->handle_key(ctrl_key_down(Key::A));

    TestRenderer in_light;
    box->render(in_light, light);
    CHECK(has_fill(in_light, resource(light)));
    TestRenderer in_dark;
    box->render(in_dark, dark);
    CHECK(has_fill(in_dark, resource(dark)));
}

// ---- undo, selection from code, the scroll bar ----

namespace
{
KeyEvent ctrl_shift_key_down(Key k)
{
    KeyEvent e{k, KeyAction::Down};
    e.modifiers.ctrl  = true;
    e.modifiers.shift = true;
    return e;
}

// A multi-line box of `lines` numbered lines, 200 DIPs tall, so most of them are out of view.
TextBoxControl make_tall_box(int lines)
{
    static HeadlessTextMeasurer measurer;
    std::string                 text;
    for (int i = 0; i < lines; ++i)
    {
        text += (i == 0 ? "" : "\n") + std::string("line ") + std::to_string(i);
    }
    TextBoxControl tb{std::move(text)};
    tb.set_accepts_return(true);
    (void)tb.measure({200.0f, 200.0f}, measurer, Theme::default_light());
    tb.arrange({0.0f, 0.0f, 200.0f, 200.0f});
    ui::testing::set_focused(tb, true);
    return tb;
}
} // namespace

TEST_CASE("Ctrl+Z undoes typing a word at a time, and Ctrl+Y and Ctrl+Shift+Z redo it", "[textbox][undo]")
{
    TextBoxControl           tb = make_text_box("");
    std::vector<std::string> observed;
    tb.on_text_changed([&](const std::string& t) { observed.push_back(t); });
    for (const char* c : {"o", "n", "e", " ", "t", "w", "o"})
    {
        tb.handle_text(c);
    }
    REQUIRE(tb.text() == "one two");
    REQUIRE(tb.can_undo());
    REQUIRE_FALSE(tb.can_redo());

    observed.clear();
    tb.handle_key(ctrl_key_down(Key::Z));
    CHECK(tb.text() == "one");
    CHECK(tb.caret() == 3);
    tb.handle_key(ctrl_key_down(Key::Z));
    CHECK(tb.text().empty());
    CHECK_FALSE(tb.can_undo());
    CHECK(observed == std::vector<std::string>{"one", ""});

    tb.handle_key(ctrl_key_down(Key::Y));
    CHECK(tb.text() == "one");
    tb.handle_key(ctrl_shift_key_down(Key::Z));
    CHECK(tb.text() == "one two");
    CHECK(tb.caret() == 7);
    CHECK_FALSE(tb.can_redo());

    // A new edit drops what could be redone.
    tb.undo();
    tb.handle_text("!");
    CHECK(tb.text() == "one!");
    CHECK_FALSE(tb.can_redo());
}

TEST_CASE("Moving the caret starts a new undo step, and a run of Backspace undoes at once", "[textbox][undo]")
{
    TextBoxControl tb = make_text_box("");
    tb.handle_text("a");
    tb.handle_text("b");
    tb.handle_key(key_down(Key::ArrowLeft));
    tb.handle_text("x");
    REQUIRE(tb.text() == "axb");
    tb.undo();
    CHECK(tb.text() == "ab");
    tb.undo();
    CHECK(tb.text().empty());

    TextBoxControl erased = make_text_box("Hello");
    erased.handle_key(key_down(Key::Backspace));
    erased.handle_key(key_down(Key::Backspace));
    erased.handle_key(key_down(Key::Backspace));
    REQUIRE(erased.text() == "He");
    erased.undo();
    CHECK(erased.text() == "Hello");
    CHECK(erased.caret() == 5);
}

TEST_CASE("Undoing the deletion of a selection selects it again", "[textbox][undo]")
{
    TextBoxControl tb = make_text_box("Hello world");
    tb.select(6, 11);
    tb.handle_key(key_down(Key::Delete));
    REQUIRE(tb.text() == "Hello ");
    tb.undo();
    CHECK(tb.text() == "Hello world");
    CHECK(tb.selection_range() == std::pair<std::size_t, std::size_t>{6, 11});
    CHECK(tb.selected_text() == "world");
}

TEST_CASE("set_text starts a new history; a read-only box ignores Ctrl+Z", "[textbox][undo]")
{
    TextBoxControl tb = make_text_box("");
    tb.handle_text("a");
    tb.set_text("loaded");
    CHECK_FALSE(tb.can_undo());
    tb.handle_key(ctrl_key_down(Key::Z));
    CHECK(tb.text() == "loaded");

    tb.handle_text("!");
    tb.set_read_only(true);
    tb.handle_key(ctrl_key_down(Key::Z));
    CHECK(tb.text() == "loaded!");
}

TEST_CASE("select, selected_text and replace_selection edit from code, and can be undone", "[textbox][selection]")
{
    TextBoxControl           tb = make_text_box("make it bold");
    std::vector<std::string> observed;
    tb.on_text_changed([&](const std::string& t) { observed.push_back(t); });

    tb.select(8, 12);
    REQUIRE(tb.selected_text() == "bold");
    tb.replace_selection("**bold**");
    CHECK(tb.text() == "make it **bold**");
    CHECK(tb.caret() == 16);
    CHECK_FALSE(tb.has_selection());
    CHECK(observed == std::vector<std::string>{"make it **bold**"});

    tb.undo();
    CHECK(tb.text() == "make it bold");
    CHECK(tb.selected_text() == "bold");

    // Offsets past the end are clamped; a backwards selection keeps its direction.
    tb.select(100, 5);
    CHECK(tb.caret() == 5);
    CHECK(tb.selected_text() == "it bold");
    tb.select_all();
    CHECK(tb.selected_text() == "make it bold");
    // Inside a multi-byte character, an offset moves back to its start.
    TextBoxControl utf8 = make_text_box("a\xC3\xA9z");
    utf8.select(2, 2);
    CHECK(utf8.caret() == 1);
}

TEST_CASE("on_selection_changed reports caret and selection moves, and nothing else", "[textbox][selection]")
{
    TextBoxControl tb    = make_text_box("Hello");
    int            moves = 0;
    tb.on_selection_changed([&] { ++moves; });

    tb.handle_key(key_down(Key::ArrowLeft));
    CHECK(moves == 1);
    tb.handle_key(shift_key_down(Key::Home));
    CHECK(moves == 2);
    tb.select(1, 1);
    CHECK(moves == 3);
    tb.select(1, 1); // unchanged
    CHECK(moves == 3);
    tb.handle_text("x");
    CHECK(moves == 4);
    tb.undo(); // one report for the undo, though it edits and moves the caret
    CHECK(moves == 5);

    // The spec wires it up.
    int                      spec_moves = 0;
    ui::Control<ui::TextBox> box;
    std::unique_ptr<Element>
        built = ui::TextBox{.text = "ab", .on_selection_changed = [&] { ++spec_moves; }, .control = box}.build();
    box->select(0, 1);
    CHECK(spec_moves == 1);
}

TEST_CASE("A multi-line box follows the caret after it moves, and lets the wheel scroll away",
          "[textbox][multiline][scroll]")
{
    TextBoxControl tb = make_tall_box(100);
    TestRenderer   renderer;

    // Ctrl+End: the caret on the last line, scrolled into view.
    tb.handle_key(ctrl_key_down(Key::Home));
    tb.render(renderer, Theme::default_light());
    tb.handle_key(ctrl_key_down(Key::End));
    tb.render(renderer, Theme::default_light());
    std::optional<Rect> caret = tb.caret_bounds();
    REQUIRE(caret.has_value());
    CHECK(caret->bottom() <= tb.bounds().bottom());
    CHECK(caret->top() >= tb.bounds().top());

    // The wheel moves the view, and the next render keeps it there although the box is focused.
    const float before = caret->top();
    REQUIRE(tb.handle_wheel(PointerEvent{tb.bounds().center(), PointerButton::None, PointerAction::Wheel, -200.0f}));
    tb.render(renderer, Theme::default_light());
    tb.render(renderer, Theme::default_light());
    caret = tb.caret_bounds();
    REQUIRE(caret.has_value());
    CHECK(caret->top() > before + 100.0f); // scrolled up: the caret is far below the view now

    // select() brings its caret into view, focused or not.
    ui::testing::set_focused(tb, false);
    tb.select(0, 0);
    tb.render(renderer, Theme::default_light());
    ui::testing::set_focused(tb, true);
    caret = tb.caret_bounds();
    REQUIRE(caret.has_value());
    CHECK(caret->top() >= tb.bounds().top());
}

TEST_CASE("A multi-line box draws a scroll bar only while its text overflows, and the bar scrolls it",
          "[textbox][multiline][scroll]")
{
    const auto     fills     = [](const TestRenderer& r) { return r.count_of<FillRoundedRect>(); };
    TextBoxControl short_box = make_tall_box(2);
    TextBoxControl tall_box  = make_tall_box(100);
    TestRenderer   short_render;
    TestRenderer   tall_render;
    short_box.render(short_render, Theme::default_light());
    tall_box.render(tall_render, Theme::default_light());
    CHECK(fills(tall_render) > fills(short_render)); // the thumb

    // A click low on the bar's track pages the view down.
    tall_box.select(0, 0);
    tall_box.render(tall_render, Theme::default_light());
    const std::optional<Rect> top = tall_box.caret_bounds();
    REQUIRE(top.has_value());
    const Point track{tall_box.bounds().right() - 2.0f, tall_box.bounds().bottom() - 40.0f};
    tall_box.handle_pointer(PointerEvent{track, PointerButton::None, PointerAction::Move});
    for (int i = 0; i < 10; ++i)
    {
        tall_box.tick(std::chrono::milliseconds{50});
    }
    tall_box.handle_pointer(PointerEvent{track, PointerButton::Left, PointerAction::Down});
    tall_box.handle_pointer(PointerEvent{track, PointerButton::Left, PointerAction::Up});
    tall_box.render(tall_render, Theme::default_light());
    const std::optional<Rect> paged = tall_box.caret_bounds();
    REQUIRE(paged.has_value());
    CHECK(paged->top() < top->top());
    CHECK(tall_box.caret() == 0); // the bar never moved the caret
}

TEST_CASE("An unfocused box shows its selection only in its unfocused selection color", "[textbox][selection]")
{
    const Color mark     = Color::from_rgb(255, 230, 0);
    const auto  has_fill = [&](const TestRenderer& r)
    {
        for (const RenderCommand& c : r.commands)
        {
            if (const auto* fill = std::get_if<FillRoundedRect>(&c); fill && fill->color == mark)
            {
                return true;
            }
        }
        return false;
    };
    TextBoxControl tb = make_text_box("find me here");
    tb.select(5, 7);
    ui::testing::set_focused(tb, false);

    TestRenderer hidden;
    tb.render(hidden, Theme::default_light());
    CHECK_FALSE(has_fill(hidden));

    tb.set_unfocused_selection_highlight_color(mark);
    TestRenderer shown;
    tb.render(shown, Theme::default_light());
    CHECK(has_fill(shown));

    // The spec field sets it.
    ui::Control<ui::TextBox> box;
    std::unique_ptr<Element> built = ui::TextBox{.unfocused_selection_highlight_color = mark, .control = box}.build();
    CHECK(box->unfocused_selection_highlight_color().color() == mark);
}

namespace
{
// A multi-line box `width` wide and 100 tall, laid out and focused.
TextBoxControl make_code_box(std::string text, float width = 300.0f)
{
    static HeadlessTextMeasurer measurer;
    TextBoxControl              tb{std::move(text)};
    tb.set_accepts_return(true);
    (void)tb.measure({width, 100.0f}, measurer, Theme::default_light());
    tb.arrange({0.0f, 0.0f, width, 100.0f});
    ui::testing::set_focused(tb, true);
    return tb;
}

void relayout_code_box(TextBoxControl& tb, float width = 300.0f)
{
    static HeadlessTextMeasurer measurer;
    (void)tb.measure({width, 100.0f}, measurer, Theme::default_light());
    tb.arrange({0.0f, 0.0f, width, 100.0f});
}

std::vector<const DrawTextRun*> drawn_texts(const TestRenderer& r)
{
    std::vector<const DrawTextRun*> out;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            out.push_back(t);
        }
    }
    return out;
}

const DrawTextRun* drawn(const TestRenderer& r, std::string_view text)
{
    for (const DrawTextRun* t : drawn_texts(r))
    {
        if (t->text == text)
        {
            return t;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("Color spans draw their text in their color and stay on it through edits", "[textbox][code]")
{
    const Color    keyword = Color::from_rgb(200, 0, 0);
    TextBoxControl tb      = make_code_box("int x = 1;");
    tb.set_color_spans({{.begin = 0, .end = 3, .color = keyword}});

    TestRenderer before;
    tb.render(before, Theme::default_light());
    const DrawTextRun* word = drawn(before, "int");
    REQUIRE(word != nullptr);
    CHECK(word->color == keyword);
    REQUIRE(drawn(before, " x = 1;") != nullptr); // the rest in the text color
    CHECK(drawn(before, " x = 1;")->color != keyword);

    // Typing before the span moves it; typing inside it stretches it; removing its text drops it.
    tb.select(0, 0);
    tb.handle_text("  ");
    CHECK(tb.color_spans() == std::vector<TextColorSpan>{{.begin = 2, .end = 5, .color = keyword}});
    tb.select(4, 4);
    tb.handle_text("n");
    CHECK(tb.color_spans() == std::vector<TextColorSpan>{{.begin = 2, .end = 6, .color = keyword}});
    tb.select(2, 6);
    tb.handle_key(key_down(Key::Delete));
    CHECK(tb.color_spans().empty());

    // Spans come back sorted, overlaps cut, and set_text() drops them.
    tb.set_color_spans({{.begin = 3, .end = 6, .color = keyword}, {.begin = 0, .end = 4, .color = keyword}});
    CHECK(tb.color_spans() ==
          std::vector<TextColorSpan>{{.begin = 0, .end = 4, .color = keyword}, {.begin = 4, .end = 6, .color = keyword}});
    tb.set_text("other");
    CHECK(tb.color_spans().empty());
}

TEST_CASE("on_edit reports every change with what was removed and inserted", "[textbox][code]")
{
    struct Seen
    {
        std::size_t position;
        std::string removed;
        std::string inserted;
        bool        operator==(const Seen&) const = default;
    };
    std::vector<Seen> seen;
    TextBoxControl    tb = make_code_box("abc");
    tb.on_edit([&](const TextBoxEditArgs& e)
               { seen.push_back({e.position, std::string{e.removed}, std::string{e.inserted}}); });

    tb.select(1, 2);
    tb.handle_text("X");
    tb.handle_key(ctrl_key_down(Key::Z));
    tb.set_text("new");
    CHECK(seen == std::vector<Seen>{{1, "b", "X"}, {1, "X", "b"}, {0, "abc", "new"}});
}

TEST_CASE("A box that accepts tab takes Tab from the window and inserts a tab character", "[textbox][code][focus]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    auto                 stack = std::make_unique<StackControl>(Orientation::Vertical);
    auto*                code  = stack->add(std::make_unique<TextBoxControl>());
    auto*                next  = stack->add(std::make_unique<TextBoxControl>());
    code->set_accepts_return(true);
    window.set_content(std::move(stack));
    window.layout({300.0f, 300.0f}, measurer);
    (void)window.focus(*code);

    ui::testing::seams(window).handle_key(key_down(Key::Tab));
    CHECK(window.focused() == next); // off by default: Tab moves focus

    code->set_accepts_tab(true);
    (void)window.focus(*code);
    ui::testing::seams(window).handle_key(key_down(Key::Tab));
    CHECK(window.focused() == code);
    CHECK(code->text() == "\t");
    ui::testing::seams(window).handle_key(KeyEvent{Key::Tab, KeyAction::Down, {.shift = true}});
    CHECK(window.focused() == code); // Shift+Tab is taken too, and does nothing
    CHECK(code->text() == "\t");

    code->set_read_only(true);
    ui::testing::seams(window).handle_key(key_down(Key::Tab));
    CHECK(window.focused() == next); // only while it can be edited
}

TEST_CASE("Line numbers sit left of the text, one per line of the text", "[textbox][code]")
{
    TextBoxControl tb = make_code_box("one\ntwo\nthree");
    TestRenderer   plain;
    tb.render(plain, Theme::default_light());
    const float plain_x = drawn(plain, "one")->bounds.x;

    tb.set_line_numbers(true);
    relayout_code_box(tb);
    TestRenderer numbered;
    tb.render(numbered, Theme::default_light());
    REQUIRE(drawn(numbered, "1") != nullptr);
    REQUIRE(drawn(numbered, "3") != nullptr);
    CHECK(drawn(numbered, "4") == nullptr);
    CHECK(drawn(numbered, "1")->bounds.right() < drawn(numbered, "one")->bounds.x);
    CHECK(drawn(numbered, "one")->bounds.x > plain_x);
    CHECK(drawn(numbered, "2")->bounds.y == drawn(numbered, "two")->bounds.y);
}

TEST_CASE("A long text draws only the lines in view", "[textbox][code]")
{
    std::string text;
    for (int i = 0; i < 200; ++i)
    {
        text += "line " + std::to_string(i) + "\n";
    }
    TextBoxControl tb = make_code_box(text);
    tb.select(0, 0);
    TestRenderer r;
    tb.render(r, Theme::default_light());
    CHECK(drawn(r, "line 0") != nullptr);
    CHECK(drawn(r, "line 50") == nullptr);
    CHECK(drawn_texts(r).size() < 10);
}

TEST_CASE("A NoWrap box scrolls sideways to the caret, by Shift+wheel and by its bar", "[textbox][code]")
{
    const std::string long_line(200, 'w');
    TextBoxControl    tb = make_code_box(long_line + "\nshort");
    tb.select(200, 200); // the end of the long line
    TestRenderer at_end;
    tb.render(at_end, Theme::default_light());
    const DrawTextRun* line = drawn(at_end, long_line);
    REQUIRE(line != nullptr);
    CHECK(line->bounds.x < LeftPad); // scrolled left to show the caret
    const std::optional<Rect> caret = tb.caret_bounds();
    REQUIRE(caret);
    CHECK(caret->x < 300.0f);
    CHECK(caret->x > LeftPad);

    // Shift+wheel back to the start.
    PointerEvent wheel{Point{50.0f, 50.0f}, PointerButton::None, PointerAction::Wheel};
    wheel.wheel_delta     = -100000.0f;
    wheel.modifiers.shift = true;
    REQUIRE(tb.handle_wheel(wheel));
    TestRenderer at_start;
    tb.render(at_start, Theme::default_light());
    CHECK(drawn(at_start, long_line)->bounds.x == LeftPad);
    // A click lands on the character under it, scroll included.
    CHECK(tb.cursor_at(Point{50.0f, 20.0f}) == CursorShape::IBeam);

    // A wrapping box never scrolls sideways.
    tb.set_wrapping(TextWrapping::Wrap);
    relayout_code_box(tb);
    CHECK_FALSE(tb.handle_wheel(wheel));
}

TEST_CASE("The code fields build from the spec", "[textbox][code][spec]")
{
    const Color              red   = Color::from_rgb(255, 0, 0);
    int                      edits = 0;
    ui::Control<ui::TextBox> box;
    std::unique_ptr<Element>
        built = ui::TextBox{.text           = "int",
                            .accepts_return = true,
                            .accepts_tab    = true,
                            .color_spans    = std::vector<TextColorSpan>{{.begin = 0, .end = 3, .color = red}},
                            .line_numbers   = true,
                            .on_edit        = [&](const TextBoxEditArgs&) { ++edits; },
                            .control        = box}
                    .build();
    CHECK(box->accepts_tab());
    CHECK(box->line_numbers());
    CHECK(box->color_spans().size() == 1);
    box->replace_selection("x");
    CHECK(edits == 1);
}

TEST_CASE("Ctrl+Left and Ctrl+Right jump words, Ctrl+Backspace and Ctrl+Delete take them", "[textbox][keyboard]")
{
    TextBoxControl tb = make_text_box("int value = 42;");
    tb.select(0, 0);
    tb.handle_key(ctrl_key_down(Key::ArrowRight));
    CHECK(tb.caret() == 4); // the start of "value"
    tb.handle_key(ctrl_key_down(Key::ArrowRight));
    CHECK(tb.caret() == 10); // "="
    tb.handle_key(ctrl_key_down(Key::ArrowLeft));
    CHECK(tb.caret() == 4);

    tb.select(9, 9); // after "value"
    tb.handle_key(ctrl_key_down(Key::Backspace));
    CHECK(tb.text() == "int  = 42;");
    tb.select(0, 0);
    tb.handle_key(ctrl_key_down(Key::Delete));
    CHECK(tb.text() == "= 42;");
}

TEST_CASE("Page Down moves the caret a view's height and the view with it", "[textbox][keyboard]")
{
    TextBoxControl tb = make_tall_box(100); // 200 tall
    tb.select(0, 0);
    TestRenderer r;
    tb.render(r, Theme::default_light());
    const std::size_t before = tb.caret();
    tb.handle_key(key_down(Key::PageDown));
    TestRenderer after;
    tb.render(after, Theme::default_light());
    const std::optional<Rect> caret = tb.caret_bounds();
    REQUIRE(caret);
    CHECK(tb.caret() > before);
    const std::size_t line = static_cast<std::size_t>(
        std::count(tb.text().begin(), tb.text().begin() + static_cast<std::ptrdiff_t>(tb.caret()), '\n'));
    CHECK(line >= 6);        // a view's height of lines, not one
    CHECK(caret->y < 40.0f); // still near the top of the view, which scrolled down with it
    tb.handle_key(key_down(Key::PageUp));
    CHECK(tb.caret() == 0);
}
