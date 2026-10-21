// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestClipboard.hpp"
#include "winui/testing/TestContextMenuHost.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/TestSpellChecker.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/RichEditBox.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;

namespace
{
KeyEvent key(Key k, bool ctrl = false, bool shift = false)
{
    KeyEvent e{k, KeyAction::Down};
    e.modifiers.ctrl  = ctrl;
    e.modifiers.shift = shift;
    return e;
}

void type(RichEditBoxControl& box, std::string_view text)
{
    for (std::size_t i = 0; i < text.size(); i = next_code_point(text, i))
    {
        if (text[i] == '\n')
        {
            box.handle_key(key(Key::Enter));
        }
        else
        {
            box.handle_text(text.substr(i, next_code_point(text, i) - i));
        }
    }
}

// RichEditBox keeps a pointer to the measurer, so the measurer must outlive the box.
std::unique_ptr<RichEditBoxControl> make_box(std::string text = "")
{
    static HeadlessTextMeasurer measurer;
    auto                        box  = std::make_unique<RichEditBoxControl>(std::move(text));
    const Size                  size = box->measure({300.0f, Unbounded}, measurer, Theme::default_light());
    box->arrange({10.0f, 10.0f, 300.0f, std::max(size.height, 120.0f)});
    ui::testing::set_focused(*box, true);
    return box;
}

RichTextFormat bold()
{
    RichTextFormat f;
    f.bold = true;
    return f;
}
} // namespace

TEST_CASE("RichEditBox defaults to RichEditBox's own: multi-line, wrapping, empty", "[richeditbox]")
{
    RichEditBoxControl box;
    CHECK(box.accepts_return());
    CHECK(box.wrapping() == TextWrapping::Wrap);
    CHECK(box.text().empty());
    CHECK(box.spans().empty());
    ui::testing::set_focused(box, true);
    CHECK(box.handle_key({Key::Enter, KeyAction::Up})); // a line break, never the default button
    ui::testing::set_focused(box, false);
    CHECK(box.automation_role() == AutomationRole::Edit);

    HeadlessTextMeasurer measurer;
    CHECK(box.measure({300.0f, Unbounded}, measurer, Theme::default_light()).height >= 32.0f);
}

TEST_CASE("RichEditBox: typing, Enter and Backspace edit a document of paragraphs", "[richeditbox]")
{
    auto box = make_box();
    type(*box, "ab\ncd");
    CHECK(box->text() == "ab\ncd");
    CHECK(box->caret() == 5);

    box->handle_key(key(Key::Backspace));
    CHECK(box->text() == "ab\nc");
    box->handle_key(key(Key::Home));
    CHECK(box->caret() == 3);
    box->handle_key(key(Key::Backspace));
    CHECK(box->text() == "abc");

    box->set_accepts_return(false);
    box->handle_key(key(Key::Enter));
    CHECK(box->text() == "abc");
}

TEST_CASE("RichEditBox: set_text normalizes line ends and raises no TextChanged", "[richeditbox]")
{
    auto box     = make_box();
    int  changes = 0;
    box->on_text_changed([&] { ++changes; });
    box->set_text("one\r\ntwo\rthree");
    CHECK(box->text() == "one\ntwo\nthree");
    CHECK(changes == 0);
    CHECK_FALSE(box->can_undo());

    type(*box, "!");
    CHECK(changes == 1);
}

TEST_CASE("RichEditBox: Ctrl+B at a bare caret formats what is typed next", "[richeditbox]")
{
    auto box = make_box();
    type(*box, "plain ");
    box->handle_key(key(Key::B, true));
    CHECK(box->selection_format().bold == true);
    type(*box, "bold");
    box->handle_key(key(Key::B, true));
    type(*box, " again");

    const std::vector<RichTextSpan> expected{
        {"plain ", {}},
        {"bold", bold()},
        {" again", {}},
    };
    CHECK(box->spans() == expected);
}

TEST_CASE("RichEditBox: toggling a mixed selection turns it on, then off", "[richeditbox]")
{
    auto box = make_box("hello world");
    box->select(0, 5);
    box->toggle_bold();
    CHECK(box->range_format(0, 5).bold == true);

    box->select(0, 11);
    CHECK_FALSE(box->selection_format().bold.has_value()); // mixed
    box->toggle_bold();
    CHECK(box->range_format(0, 11).bold == true);
    box->toggle_bold();
    CHECK(box->range_format(0, 11).bold == false);
    CHECK(box->spans().size() == 1);
}

TEST_CASE("RichEditBox: colors set on a range read back, control_default puts back the control's own", "[richeditbox]")
{
    auto        box = make_box("abcdef");
    const Color red = Color::from_argb(0xFFFF0000);
    box->set_range_format(2, 4, {.foreground = red, .background = Color::from_argb(0xFFFFFF00)});
    CHECK(box->range_format(2, 4).foreground == red);
    CHECK_FALSE(box->range_format(0, 2).foreground.has_value());
    CHECK(has_flag(box->range_format(0, 2).control_default, RichTextFormatField::Foreground));
    CHECK_FALSE(box->range_format(0, 4).foreground.has_value());
    CHECK_FALSE(has_flag(box->range_format(0, 4).control_default, RichTextFormatField::Foreground));

    box->set_range_format(0, 6, {.control_default = RichTextFormatField::Foreground | RichTextFormatField::Background});
    CHECK(box->spans() == std::vector<RichTextSpan>{{"abcdef", {}}});

    box->set_range_format(0, 3, {.font_family = "Arial", .font_size = 20.0f});
    CHECK(box->range_format(0, 3).font_family == "Arial");
    CHECK(box->range_format(3, 6).control_default == RichTextFormatField::All);
    box->set_range_format(0, 6, {.control_default = RichTextFormatField::FontFamily | RichTextFormatField::FontSize});
    CHECK(box->spans() == std::vector<RichTextSpan>{{"abcdef", {}}});
    CHECK_FALSE(box->can_undo()); // range formatting is not an undo step
}

TEST_CASE("RichEditBox: typed text takes the format before the caret", "[richeditbox]")
{
    auto box = make_box();
    box->set_spans({{"ab", bold()}, {"cd", {}}});
    box->select(2, 2);
    type(*box, "X");
    CHECK(box->range_format(2, 3).bold == true);
    box->select(5, 5);
    type(*box, "Y");
    CHECK(box->range_format(5, 6).bold == false);
}

TEST_CASE("RichEditBox: find_all is case-insensitive and does not overlap", "[richeditbox]")
{
    auto                                                   box = make_box("Aaa aA");
    const std::vector<std::pair<std::size_t, std::size_t>> expected{{0, 2}, {4, 6}};
    CHECK(box->find_all("aa") == expected);
    CHECK(box->find_all("").empty());
    CHECK(box->find_all("zz").empty());
}

TEST_CASE("RichEditBox: typing coalesces into one undo step until the caret moves", "[richeditbox]")
{
    auto box = make_box();
    type(*box, "hello");
    box->handle_key(key(Key::Z, true));
    CHECK(box->text().empty());
    box->handle_key(key(Key::Y, true));
    CHECK(box->text() == "hello");

    box->handle_key(key(Key::ArrowLeft));
    type(*box, "X");
    CHECK(box->text() == "hellXo");
    box->handle_key(key(Key::Z, true));
    CHECK(box->text() == "hello");
    box->handle_key(key(Key::Z, true, true)); // Ctrl+Shift+Z redoes
    CHECK(box->text() == "hellXo");
}

TEST_CASE("RichEditBox: formatting the selection is an undo step", "[richeditbox]")
{
    auto box = make_box("word");
    box->select_all();
    int changes = 0;
    box->on_text_changed([&] { ++changes; });
    box->handle_key(key(Key::I, true));
    CHECK(box->range_format(0, 4).italic == true);
    CHECK(changes == 1);
    box->handle_key(key(Key::Z, true));
    CHECK(box->range_format(0, 4).italic == false);
    CHECK(changes == 2);
}

TEST_CASE("RichEditBox: DisabledFormattingShortcuts leaves the shortcut alone", "[richeditbox]")
{
    auto box = make_box("word");
    box->select_all();
    box->set_disabled_formatting_shortcuts(DisabledFormattingShortcuts::Bold);
    KeyEvent underline_up = key(Key::U, true);
    underline_up.action   = KeyAction::Up;
    CHECK(box->handle_key(underline_up));
    CHECK_FALSE(box->handle_key(key(Key::B, true))); // left to an app-wide shortcut
    CHECK(box->range_format(0, 4).bold == false);
    box->handle_key(key(Key::U, true));
    CHECK(box->range_format(0, 4).underline == true);
}

TEST_CASE("RichEditBox: Ctrl+Left/Right jump by words, Ctrl+Backspace deletes one", "[richeditbox]")
{
    auto box = make_box("one two three");
    box->handle_key(key(Key::ArrowLeft, true));
    CHECK(box->caret() == 8);
    box->handle_key(key(Key::ArrowLeft, true));
    CHECK(box->caret() == 4);
    box->handle_key(key(Key::ArrowRight, true));
    CHECK(box->caret() == 8);
    box->handle_key(key(Key::Backspace, true));
    CHECK(box->text() == "one three");
}

TEST_CASE("RichEditBox: MaxLength and CharacterCasing apply to what the user types", "[richeditbox]")
{
    auto box = make_box();
    box->set_max_length(3);
    box->set_character_casing(CharacterCasing::Upper);
    type(*box, "abcdef");
    CHECK(box->text() == "ABC");
}

TEST_CASE("RichEditBox: read-only selects but never edits", "[richeditbox]")
{
    auto box = make_box("fixed");
    box->set_read_only(true);
    type(*box, "x");
    box->handle_key(key(Key::Backspace));
    box->handle_key(key(Key::A, true));
    box->handle_key(key(Key::B, true));
    CHECK(box->text() == "fixed");
    CHECK(box->selection() == std::pair<std::size_t, std::size_t>{0, 5});
    CHECK(box->range_format(0, 5).bold == false);
    CHECK_FALSE(box->handle_key(key(Key::Enter)));
    CHECK(box->text() == "fixed");
}

TEST_CASE("RichEditBox: copy and paste within the process keeps the formatting", "[richeditbox]")
{
    TestClipboard clipboard;
    auto          box = make_box();
    ui::testing::set_clipboard(&clipboard);
    box->set_spans({{"ab", bold()}, {"cd", {}}});
    box->select(1, 3);
    box->handle_key(key(Key::C, true));
    CHECK(clipboard.text() == "bc");

    box->select(4, 4);
    box->handle_key(key(Key::V, true));
    const std::vector<RichTextSpan> expected{{"ab", bold()}, {"cd", {}}, {"b", bold()}, {"c", {}}};
    CHECK(box->spans() == expected);

    // Text from anywhere else comes in plain, in the format at the caret.
    clipboard.set_text("x\r\ny");
    box->select(0, 0);
    box->handle_key(key(Key::V, true));
    CHECK(box->text() == "x\nyabcdbc");
    CHECK(box->range_format(0, 3).bold == true);
}

TEST_CASE("RichEditBox: cut removes the selection as one undo step", "[richeditbox]")
{
    TestClipboard clipboard;
    auto          box = make_box("hello world");
    ui::testing::set_clipboard(&clipboard);
    box->select(5, 11);
    box->handle_key(key(Key::X, true));
    CHECK(box->text() == "hello");
    CHECK(clipboard.text() == " world");
    box->handle_key(key(Key::Z, true));
    CHECK(box->text() == "hello world");
}

TEST_CASE("RichEditBox: RTF written by save_rtf() loads back to the same document", "[richeditbox][rtf]")
{
    auto           box = make_box();
    RichTextFormat italic_red;
    italic_red.italic     = true;
    italic_red.foreground = Color::from_argb(0xFFFF0000);
    RichTextFormat marked;
    marked.underline     = true;
    marked.strikethrough = true;
    marked.background    = Color::from_argb(0xFFFFFF00);
    marked.font_family   = "Consolas";
    marked.font_size     = 20.0f;
    const std::vector<RichTextSpan> document{
        {"Plain {braces} \\ back", {}},
        {"bold\n", bold()},
        {"caf\u00E9 \u20AC\ttab", italic_red},
        {"marked", marked},
    };
    box->set_spans(document);
    const std::string rtf = box->save_rtf();
    CHECK(rtf.starts_with("{\\rtf1"));

    auto copy = make_box();
    copy->load_rtf(rtf);
    CHECK(copy->spans() == document);
}

TEST_CASE("RichEditBox: load_rtf reads a WordPad-style file and skips what it does not know", "[richeditbox][rtf]")
{
    const std::string rtf =
        "{\\rtf1\\ansi\\ansicpg1252\\deff0\\nouicompat{\\fonttbl{\\f0\\fnil\\fcharset0 Calibri;}"
        "{\\f1\\fswiss Arial;}}\r\n"
        "{\\colortbl ;\\red0\\green0\\blue255;}\r\n"
        "{\\*\\generator Riched20 10.0.22621}\\viewkind4\\uc1 \r\n"
        "\\pard\\sa200\\sl276\\slmult1\\f0\\fs22\\lang9 Caf\\'e9 \\b bold\\b0  "
        "\\cf1\\f1 blue\\cf0\\f0  \\u8364?\\par\r\n"
        "{\\info{\\author nobody}}second\\par\r\n}";
    auto box = make_box();
    box->load_rtf(rtf);
    CHECK(box->text() == "Caf\u00E9 bold blue \u20AC\nsecond");
    // Byte offsets: the é takes two.
    CHECK(box->range_format(6, 10).bold == true);
    CHECK(box->range_format(11, 15).foreground == Color::from_argb(0xFF0000FF));
    CHECK(box->range_format(11, 15).font_family == "Arial");
    CHECK(box->range_format(0, 5).font_family == "Calibri");
    CHECK(box->range_format(0, 5).font_size.value_or(0.0f) > 14.0f); // 11pt
}

TEST_CASE("RichEditBox: load_rtf takes anything that is not RTF as plain text", "[richeditbox][rtf]")
{
    auto box = make_box();
    box->load_rtf("just text");
    CHECK(box->text() == "just text");
    CHECK(box->spans() == std::vector<RichTextSpan>{{"just text", {}}});
}

TEST_CASE("RichEditBox: the context menu offers the commands that apply, then the app's", "[richeditbox]")
{
    TestClipboard       clipboard;
    TestContextMenuHost menu;
    auto                box = make_box("text");
    ui::testing::set_clipboard(&clipboard);
    bool shared = false;
    box->add_context_menu_item("Share", [&] { shared = true; });

    box->handle_context_menu(std::nullopt, menu);
    CHECK(menu.last_texts() == std::vector<std::string>{"Bold", "Italic", "Underline", "", "Select All", "", "Share"});

    box->select_all();
    int changes = 0;
    box->on_text_changed([&] { ++changes; });
    menu.pick("Bold");
    box->handle_context_menu(std::nullopt, menu);
    CHECK(menu.last_texts() ==
          std::vector<std::string>{"Cut", "Copy", "Bold", "Italic", "Underline", "", "Select All", "", "Share"});
    CHECK(box->range_format(0, 4).bold == true);
    CHECK(changes == 1);

    menu.pick("Share");
    box->handle_context_menu(std::nullopt, menu);
    CHECK(shared);
    CHECK(menu.enabled("Undo") == true);

    box->set_read_only(true);
    box->handle_context_menu(std::nullopt, menu);
    CHECK(menu.last_texts() == std::vector<std::string>{"Copy", "", "Select All", "", "Share"});
}

TEST_CASE("RichEditBox: SelectionChanged follows the caret; TextChanged only the user's edits", "[richeditbox]")
{
    auto box        = make_box("abc");
    int  selections = 0;
    int  changes    = 0;
    box->on_selection_changed([&] { ++selections; });
    box->on_text_changed([&] { ++changes; });
    box->handle_key(key(Key::ArrowLeft));
    CHECK(selections == 1);
    box->handle_key(key(Key::ArrowLeft, false, true));
    CHECK(selections == 2);
    box->set_range_format(0, 3, {.bold = true});
    box->set_text("new");
    CHECK(changes == 0);
}

TEST_CASE("RichEditBox renders each format run in its own style and color", "[richeditbox]")
{
    auto           box = make_box();
    RichTextFormat green;
    green.foreground = Color::from_argb(0xFF008000);
    box->set_spans({{"Bold ", bold()}, {"green", green}});
    ui::testing::set_focused(*box, false);

    TestRenderer renderer;
    box->render(renderer, Theme::light());
    std::vector<DrawTextRun> runs;
    for (const RenderCommand& command : renderer.commands)
    {
        if (const auto* run = std::get_if<DrawTextRun>(&command))
        {
            runs.push_back(*run);
        }
    }
    REQUIRE(runs.size() == 2);
    CHECK(runs[0].text.starts_with("Bold"));
    CHECK(runs[0].style.weight == FontWeight::Bold);
    CHECK(runs[1].text == "green");
    CHECK(runs[1].color == Color::from_argb(0xFF008000));
    CHECK(runs[1].style.weight == FontWeight::Regular);
}

TEST_CASE("RichEditBox squiggles misspellings and offers suggestions", "[richeditbox]")
{
    TestSpellChecker checker;
    checker.add_misspelling("wrld", {"world"});
    TestContextMenuHost menu;
    auto                box = make_box("hello wrld");
    box->sync_spell_checker(checker);
    box->tick(0ms);
    REQUIRE(box->spelling_errors().size() == 1);

    TestRenderer renderer;
    box->render(renderer, Theme::light());
    bool squiggled = false;
    for (const RenderCommand& command : renderer.commands)
    {
        squiggled = squiggled || std::holds_alternative<StrokeWavyLine>(command);
    }
    CHECK(squiggled);

    box->select(8, 8);
    menu.pick("world");
    box->handle_context_menu(std::nullopt, menu);
    CHECK(box->text() == "hello world");
}

TEST_CASE("ui::RichEditBox builds a ui::RichEditBox", "[richeditbox][spec]")
{
    ui::Control<ui::RichEditBox> ref;
    auto                         node = ui::build(
        ui::RichEditBox{.placeholder_text              = "Write here",
                        .text                          = "hi",
                        .accepts_return                = false,
                        .disabled_formatting_shortcuts = DisabledFormattingShortcuts::All,
                        .text_alignment                = TextAlignment::Center,
                        .control                       = ref});
    REQUIRE(ref != nullptr);
    CHECK(ref == node.get());
    CHECK(ref->text() == "hi");
    CHECK(ref->placeholder_text() == "Write here");
    CHECK_FALSE(ref->accepts_return());
    CHECK(ref->text_alignment() == TextAlignment::Center);
    CHECK(ref->disabled_formatting_shortcuts() == DisabledFormattingShortcuts::All);
}

TEST_CASE("RichEditBox in a window: a click focuses it and typing edits it", "[richeditbox]")
{
    HeadlessUI                   ui;
    ui::Control<ui::RichEditBox> box;
    ui.window().set_content(ui::VerticalStack{.content = {ui::RichEditBox{.width = 200.0f, .control = box}}});
    ui.layout({400.0f, 300.0f});
    REQUIRE(box != nullptr);
    const Rect  b = box->bounds();
    const Point inside{b.x + 10.0f, b.y + b.height / 2.0f};
    ui::testing::seams(ui.window()).handle_pointer({inside, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({inside, PointerButton::Left, PointerAction::Up});
    CHECK(ui.window().focused() == box);
    ui::testing::seams(ui.window()).handle_text("h");
    ui::testing::seams(ui.window()).handle_text("i");
    CHECK(box->text() == "hi");
}

TEST_CASE("RichEditBox in a window draws its caret when empty and its text once typed", "[richeditbox]")
{
    HeadlessUI                   ui;
    ui::Control<ui::RichEditBox> box;
    ui.window().set_content(ui::VerticalStack{.content = {ui::RichEditBox{.control = box}}});
    ui.layout({400.0f, 300.0f});
    const Point inside{box->bounds().x + 10.0f, box->bounds().y + 10.0f};
    ui::testing::seams(ui.window()).handle_pointer({inside, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({inside, PointerButton::Left, PointerAction::Up});
    const auto dump = [&]
    {
        TestRenderer r;
        box->render(r, Theme::light());
        std::string texts;
        int         lines = 0;
        for (const RenderCommand& c : r.commands)
        {
            if (const auto* t = std::get_if<DrawTextRun>(&c))
            {
                texts += t->text;
            }
            lines += std::holds_alternative<StrokeLine>(c) ? 1 : 0;
        }
        return std::pair{texts, lines};
    };
    CHECK(dump().second >= 1);
    ui::testing::seams(ui.window()).handle_text("h");
    ui.layout({400.0f, 300.0f});
    CHECK(dump().first == "h");
    INFO("bounds " << box->bounds().width << "x" << box->bounds().height);
    CHECK(box->bounds().width > 50.0f);
}

TEST_CASE("RichEditBox: a click where the caret already is keeps the format picked for the next letter", "[richeditbox]")
{
    HeadlessUI                   ui;
    ui::Control<ui::RichEditBox> box;
    ui.window().set_content(ui::VerticalStack{.content = {ui::RichEditBox{.width = 300.0f, .control = box}}});
    ui.layout({400.0f, 300.0f});
    ui.window().focus_element(box);
    ui::testing::seams(ui.window()).handle_text("a");
    box->set_selection_format({.foreground = Color::from_argb(0xFFFF0000)}); // a toolbar pick, caret at 1
    ui.layout({400.0f, 300.0f});
    const Point end{box->bounds().right() - 5.0f, box->bounds().y + box->bounds().height / 2.0f};
    ui::testing::seams(ui.window()).handle_pointer({end, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({end, PointerButton::Left, PointerAction::Up});
    REQUIRE(box->caret() == 1);
    ui::testing::seams(ui.window()).handle_text("b");
    CHECK(box->range_format(1, 2).foreground == Color::from_argb(0xFFFF0000));

    box->handle_key(key(Key::Home)); // a real move drops it
    box->set_selection_format({.foreground = Color::from_argb(0xFF0000FF)});
    box->handle_key(key(Key::End));
    ui::testing::seams(ui.window()).handle_text("c");
    CHECK(box->range_format(2, 3).foreground == Color::from_argb(0xFFFF0000));
}

TEST_CASE("RichEditBox's bound text is two-way and keeps the formatting", "[richeditbox][spec]")
{
    HeadlessUI                   ui;
    Property<std::string>        notes{"x"};
    int                          changes = 0;
    ui::Control<ui::RichEditBox> box;
    ui.window().set_content(ui::VerticalStack{
        .content = {ui::RichEditBox{
            .text            = ui::bind(notes),
            .width           = 200.0f,
            .on_text_changed = [&] { ++changes; },
            .control         = box,
        }}});
    ui.layout({400.0f, 300.0f});
    REQUIRE(box->text() == "x");

    // The user types: the property follows, and the spec's own handler still runs.
    const Point inside{box->bounds().x + 10.0f, box->bounds().y + box->bounds().height / 2.0f};
    seams(ui.window()).handle_pointer({inside, PointerButton::Left, PointerAction::Down});
    seams(ui.window()).handle_pointer({inside, PointerButton::Left, PointerAction::Up});
    seams(ui.window()).handle_text("y");
    CHECK(notes.get() == box->text());
    CHECK(notes.get().size() == 2);
    CHECK(changes == 1);

    // The app writes: the box follows.
    notes = "replaced";
    CHECK(box->text() == "replaced");

    // The same text again changes nothing, so formatting applied by the user stays.
    box->load_rtf(R"({\rtf1\ansi {\b bold}\par})");
    const std::string rtf = box->save_rtf();
    notes                 = box->text();
    CHECK(box->save_rtf() == rtf);
}

TEST_CASE("RichEditBox draws a theme-resource selection highlight in the theme's color", "[richeditbox][theme]")
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

    ui::Control<ui::RichEditBox> box;
    auto built = ui::build(ui::RichEditBox{.text = "themed", .selection_highlight_color = resource, .control = box});
    REQUIRE(box->selection_highlight_resource() == resource);
    const Size size = box->measure({300.0f, Unbounded}, measurer, light);
    box->arrange({10.0f, 10.0f, 300.0f, std::max(size.height, 120.0f)});
    ui::testing::set_focused(*box, true);
    box->select_all();

    TestRenderer in_light;
    box->render(in_light, light);
    CHECK(has_fill(in_light, resource(light)));
    TestRenderer in_dark;
    box->render(in_dark, dark);
    CHECK(has_fill(in_dark, resource(dark)));
}

TEST_CASE("A RichEditBox draws a scroll bar only while its text overflows, and the bar scrolls it",
          "[richeditbox][scroll]")
{
    static HeadlessTextMeasurer measurer;
    const auto                  make = [](int lines)
    {
        std::string text;
        for (int i = 0; i < lines; ++i)
        {
            text += (i == 0 ? "" : "\n") + std::string("line ") + std::to_string(i);
        }
        auto box = std::make_unique<RichEditBoxControl>(std::move(text));
        (void)box->measure({200.0f, 200.0f}, measurer, Theme::default_light());
        box->arrange({0.0f, 0.0f, 200.0f, 200.0f});
        ui::testing::set_focused(*box, true);
        return box;
    };
    const auto   fills     = [](const TestRenderer& r) { return r.count_of<FillRoundedRect>(); };
    auto         short_box = make(2);
    auto         tall_box  = make(100);
    TestRenderer short_render;
    TestRenderer tall_render;
    short_box->render(short_render, Theme::default_light());
    tall_box->render(tall_render, Theme::default_light());
    CHECK(fills(tall_render) > fills(short_render)); // the thumb

    // A click low on the bar's track pages the view down.
    tall_box->select(0, 0);
    tall_box->render(tall_render, Theme::default_light());
    const std::optional<Rect> top = tall_box->caret_bounds();
    REQUIRE(top.has_value());
    const Point track{tall_box->bounds().right() - 2.0f, tall_box->bounds().bottom() - 40.0f};
    tall_box->handle_pointer(PointerEvent{track, PointerButton::None, PointerAction::Move});
    for (int i = 0; i < 10; ++i)
    {
        tall_box->tick(std::chrono::milliseconds{50});
    }
    tall_box->handle_pointer(PointerEvent{track, PointerButton::Left, PointerAction::Down});
    tall_box->handle_pointer(PointerEvent{track, PointerButton::Left, PointerAction::Up});
    tall_box->render(tall_render, Theme::default_light());
    const std::optional<Rect> paged = tall_box->caret_bounds();
    REQUIRE(paged.has_value());
    CHECK(paged->top() < top->top());
    CHECK(tall_box->selection().first == 0); // the bar never moved the caret
}
