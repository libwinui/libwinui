// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Describe.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/SpellChecker.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestClipboard.hpp"
#include "winui/testing/TestContextMenuHost.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/TestSpellChecker.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/AutoSuggestBox.hpp"
#include "winui/ui/text/NumberBox.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <utility>
#include <variant>
#ifdef _WIN32
#include "platform/windows/Win32SpellChecker.hpp"
#include "platform/windows/Win32ThemeSource.hpp"
#include "winui/testing/OffscreenCapture.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <thread>
#include <windows.h>
#undef near // <windows.h>'s 16-bit pointer-qualifier macros collide with near() below
#undef far
#endif

#include "winui/testing/ElementAccess.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/text/Text.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;

namespace
{
constexpr float FontSize = 14.0f; // Theme::light()'s default text_style.font_size
constexpr float CharW    = FontSize * HeadlessTextMeasurer::AdvanceRatio;
constexpr float TextLeft = 100.0f + 11.0f; // make_box()'s arrange x + Theme::light().padding.left

// The measurer is static because TextBox caches the pointer it was measured with.
TextBoxControl make_box(std::string text = "")
{
    static HeadlessTextMeasurer measurer;
    TextBoxControl              tb{std::move(text)};
    const Size                  size = tb.measure({200.0f, Unbounded}, measurer, Theme::default_light());
    // Arranged across the width it was offered, as a Stretch slot does: a TextBox is only as wide
    // as its text by itself, and focusing it adds the delete button's column.
    tb.arrange({100.0f, 100.0f, 200.0f, size.height});
    ui::testing::set_focused(tb, true);
    return tb;
}

// A point over character `index` (a little into it, so it hit-tests to `index`).
Point at_char(std::size_t index)
{
    return {TextLeft + (static_cast<float>(index) + 0.2f) * CharW, 110.0f};
}

void type(TextBoxControl& tb, std::string_view text)
{
    // One code point a keystroke, as WM_CHAR delivers them.
    for (std::size_t i = 0; i < text.size(); i = next_code_point(text, i))
    {
        tb.handle_text(std::string_view{text}.substr(i, next_code_point(text, i) - i));
    }
}

KeyEvent key_down(Key k, bool ctrl = false)
{
    KeyEvent e{k, KeyAction::Down};
    e.modifiers.ctrl = ctrl;
    return e;
}

std::vector<StrokeWavyLine> squiggles(const TextBoxControl& tb)
{
    TestRenderer r;
    tb.render(r, Theme::light());
    std::vector<StrokeWavyLine> lines;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* line = std::get_if<StrokeWavyLine>(&c))
        {
            lines.push_back(*line);
        }
    }
    return lines;
}

bool near(float a, float b)
{
    return std::abs(a - b) < 0.01f;
}

// A checked box: synced to `checker`, first check already run.
void sync_and_check(TextBoxControl& tb, SpellChecker& checker)
{
    tb.sync_spell_checker(checker);
    tb.tick(0ms);
}
} // namespace

// ---- the render primitive ---------------------------------------------------------

TEST_CASE("StrokeWavyLine is recorded verbatim and described", "[render][spellcheck]")
{
    const Color  red = Color::from_argb(0xFFC42B1C);
    TestRenderer r;
    r.stroke_wavy_line({1.0f, 2.0f}, {30.0f, 2.0f}, red, 1.0f, 1.5f, 4.0f);

    REQUIRE(r.count() == 1);
    REQUIRE(std::get<StrokeWavyLine>(r.commands[0]) == StrokeWavyLine{{1.0f, 2.0f}, {30.0f, 2.0f}, red, 1.0f, 1.5f, 4.0f});

    const std::string text = describe(r.commands);
    REQUIRE(text.starts_with("StrokeWavyLine from=("));
    REQUIRE(text.find(" amplitude=") != std::string::npos);
    REQUIRE(text.find(" wavelength=") != std::string::npos);
}

// ---- the headless double ------------------------------------------------------------

TEST_CASE("TestSpellChecker flags configured words; add/ignore/autocorrect change its answer", "[spellcheck]")
{
    TestSpellChecker checker;
    checker.add_misspelling("Ths", {"This"});

    auto errors = checker.check("Ths is it");
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0] == SpellingError{0, 3, SpellingAction::Suggest, {}});
    REQUIRE(checker.suggest("Ths") == std::vector<std::string>{"This"});

    const auto before = checker.generation();
    checker.ignore_for_session("Ths");
    REQUIRE(checker.generation() > before);
    REQUIRE(checker.check("Ths").empty());

    checker.add_autocorrection("teh", "the");
    errors = checker.check("I saw teh dog");
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0] == SpellingError{6, 3, SpellingAction::Replace, "the"});
}

// ---- TextBox: checking and squiggles --------------------------------------------------

TEST_CASE("A TextBox without a spell checker never squiggles", "[textbox][spellcheck]")
{
    TextBoxControl tb = make_box("Ths is a tset");
    tb.tick(1000ms);
    REQUIRE(tb.spelling_errors().empty());
    REQUIRE(squiggles(tb).empty());
}

TEST_CASE("A synced TextBox checks on its next tick and squiggles each misspelled word's own span",
          "[textbox][spellcheck]")
{
    TestSpellChecker checker;
    checker.add_misspelling("Ths", {"This"});
    checker.add_misspelling("tset", {"test"});
    TextBoxControl tb = make_box("Ths is a tset");

    tb.sync_spell_checker(checker);
    REQUIRE(tb.spelling_errors().empty()); // never inside the sync (i.e. inside focus handling) itself
    tb.tick(0ms);
    REQUIRE(tb.spelling_errors().size() == 2);

    const auto lines = squiggles(tb);
    REQUIRE(lines.size() == 2);
    REQUIRE(near(lines[0].from.x, TextLeft));
    REQUIRE(near(lines[0].to.x, TextLeft + 3 * CharW));
    REQUIRE(near(lines[1].from.x, TextLeft + 9 * CharW));
    REQUIRE(near(lines[1].to.x, TextLeft + 13 * CharW));
    REQUIRE(lines[0].from.y == lines[0].to.y);
    REQUIRE(lines[0].color == Theme::light().spelling_error);
}

TEST_CASE("Nothing is squiggled with highlighting switched off, no dictionary, or the field opted out",
          "[textbox][spellcheck]")
{
    TestSpellChecker checker;
    checker.add_misspelling("Ths");
    TextBoxControl tb = make_box("Ths is it");

    SECTION("Highlight misspelled words: off")
    {
        checker.set_highlight_enabled(false);
        sync_and_check(tb, checker);
        REQUIRE(squiggles(tb).empty());
    }
    SECTION("no dictionary for the current language")
    {
        checker.set_available(false);
        sync_and_check(tb, checker);
        REQUIRE(squiggles(tb).empty());
    }
    SECTION("set_spell_check(false) never even asks")
    {
        tb.set_spell_check(false);
        sync_and_check(tb, checker);
        type(tb, " Ths ");
        tb.tick(Duration{TextBoxControl::SpellCheckDelayMs});
        REQUIRE(checker.check_count() == 0);
        REQUIRE(squiggles(tb).empty());
    }
}

TEST_CASE("Flipping a setting re-checks on the next tick via the checker's generation", "[textbox][spellcheck]")
{
    TestSpellChecker checker;
    checker.add_misspelling("Ths");
    TextBoxControl tb = make_box("Ths is it");
    sync_and_check(tb, checker);
    REQUIRE(squiggles(tb).size() == 1);

    checker.set_highlight_enabled(false);
    tb.tick(0ms);
    REQUIRE(squiggles(tb).empty());

    checker.set_highlight_enabled(true);
    tb.tick(0ms);
    REQUIRE(squiggles(tb).size() == 1);
}

TEST_CASE("The word being typed isn't squiggled; finishing it squiggles it at once", "[textbox][spellcheck]")
{
    TestSpellChecker checker;
    checker.add_misspelling("Ths");
    TextBoxControl tb = make_box();
    sync_and_check(tb, checker);

    type(tb, "Ths");
    tb.tick(Duration{TextBoxControl::SpellCheckDelayMs}); // the debounced full check has run...
    REQUIRE(tb.spelling_errors().size() == 1);
    REQUIRE(squiggles(tb).empty()); // ...but the word is still being typed

    const int checks = checker.check_count();
    tb.handle_text(" ");
    REQUIRE(checker.check_count() == checks + 1); // just the finished word, synchronously
    REQUIRE(squiggles(tb).size() == 1);
}

TEST_CASE("An apostrophe doesn't finish a word early", "[textbox][spellcheck]")
{
    TestSpellChecker checker;
    checker.add_misspelling("didn");
    TextBoxControl tb = make_box();
    sync_and_check(tb, checker);

    type(tb, "didn't ");
    REQUIRE(squiggles(tb).empty());
}

TEST_CASE("Edits drop the squiggles they touch immediately and re-check after the debounce delay",
          "[textbox][spellcheck]")
{
    TestSpellChecker checker;
    checker.add_misspelling("Ths");
    checker.add_misspelling("tset");
    TextBoxControl tb = make_box("Ths is a tset");
    sync_and_check(tb, checker);
    REQUIRE(tb.spelling_errors().size() == 2);

    tb.handle_key(key_down(Key::Home));
    tb.handle_text("x"); // "xThs is a tset": touches "Ths", shifts "tset" by one
    REQUIRE(tb.spelling_errors().size() == 1);
    REQUIRE(tb.spelling_errors()[0].start == 10);

    const int checks = checker.check_count();
    tb.tick(Duration{TextBoxControl::SpellCheckDelayMs - 1.0});
    REQUIRE(checker.check_count() == checks);
    tb.tick(1ms);
    REQUIRE(checker.check_count() == checks + 1);
    REQUIRE(tb.spelling_errors().size() == 1); // "xThs" is fine as far as this dictionary knows
}

TEST_CASE("A pending check still runs after focus moves on", "[textbox][spellcheck]")
{
    TestSpellChecker checker;
    checker.add_misspelling("Ths");
    TextBoxControl tb = make_box();
    sync_and_check(tb, checker);

    type(tb, "Ths");
    ui::testing::set_focused(tb, false);
    tb.tick(Duration{TextBoxControl::SpellCheckDelayMs});
    REQUIRE(squiggles(tb).size() == 1); // blur finished the word, so it shows
}

// ---- TextBox: autocorrect -------------------------------------------------------------

TEST_CASE("Autocorrect replaces a registered word as it's finished; Ctrl+Z puts it back for good",
          "[textbox][spellcheck]")
{
    TestSpellChecker checker;
    checker.add_autocorrection("teh", "the");
    TextBoxControl tb = make_box();
    sync_and_check(tb, checker);
    std::vector<std::string> changes;
    tb.on_text_changed([&](const std::string& text) { changes.push_back(text); });

    type(tb, "teh ");
    REQUIRE(tb.text() == "the ");
    REQUIRE(tb.caret() == 4);
    REQUIRE(changes.size() == 4); // one notification per keystroke, the correcting one included
    REQUIRE(changes.back() == "the ");

    tb.handle_key(key_down(Key::Unknown, true)); // Ctrl pressed on its own, on the way to Ctrl+Z
    tb.handle_key(key_down(Key::Z, true));
    REQUIRE(tb.text() == "teh ");
    REQUIRE(tb.caret() == 4);
    REQUIRE(squiggles(tb).size() == 1); // reverted, but still misspelled

    // Finishing the same word again doesn't re-correct it.
    tb.handle_key(key_down(Key::Backspace));
    tb.handle_text(" ");
    REQUIRE(tb.text() == "teh ");
}

TEST_CASE("Ctrl+Z only reverts an autocorrection as the very next action", "[textbox][spellcheck]")
{
    TestSpellChecker checker;
    checker.add_autocorrection("teh", "the");
    TextBoxControl tb = make_box();
    sync_and_check(tb, checker);

    type(tb, "teh x");
    tb.handle_key(key_down(Key::Z, true)); // undoes the "x"; the autocorrection stays
    REQUIRE(tb.text() == "the ");
}

TEST_CASE("With autocorrect switched off the word is only squiggled", "[textbox][spellcheck]")
{
    TestSpellChecker checker;
    checker.add_autocorrection("teh", "the");
    checker.set_autocorrect_enabled(false);
    TextBoxControl tb = make_box();
    sync_and_check(tb, checker);

    type(tb, "teh ");
    REQUIRE(tb.text() == "teh ");
    REQUIRE(squiggles(tb).size() == 1);
}

// ---- TextBox: context menu -------------------------------------------------------------

TEST_CASE("Right-clicking a squiggle lists its suggestions first; picking one replaces the word",
          "[textbox][spellcheck][context_menu]")
{
    TestSpellChecker checker;
    checker.add_misspelling("Ths", {"This", "Thus"});
    TextBoxControl tb = make_box("Ths is it");
    sync_and_check(tb, checker);

    TestContextMenuHost host;
    host.pick("This");
    REQUIRE(tb.handle_context_menu(at_char(1), host));
    REQUIRE(host.last_texts() ==
            std::vector<std::string>{"This",
                                     "Thus",
                                     "",
                                     "Add to dictionary",
                                     "Ignore",
                                     "",
                                     "Cut",
                                     "Copy",
                                     "Paste",
                                     "",
                                     "Select All"});
    REQUIRE(near(host.last_position().x, at_char(1).x));
    REQUIRE(tb.text() == "This is it");
    REQUIRE(tb.caret() == 4);
    REQUIRE(tb.spelling_errors().empty());
}

TEST_CASE("An autocorrection's replacement leads the suggestion list; no suggestions shows a grayed note",
          "[textbox][spellcheck][context_menu]")
{
    TestSpellChecker    checker;
    TestContextMenuHost host;

    SECTION("Replace")
    {
        checker.add_autocorrection("teh", "the");
        TextBoxControl tb = make_box("teh cat");
        sync_and_check(tb, checker);
        (void)tb.handle_context_menu(at_char(1), host);
        REQUIRE(host.last_texts().front() == "the");
    }
    SECTION("no suggestions")
    {
        checker.add_misspelling("qzx");
        TextBoxControl tb = make_box("qzx");
        sync_and_check(tb, checker);
        (void)tb.handle_context_menu(at_char(1), host);
        REQUIRE(host.last_texts().front() == "(No spelling suggestions)");
        REQUIRE(host.enabled("(No spelling suggestions)") == false);
    }
}

TEST_CASE("Add to dictionary and Ignore reach the checker and clear the squiggle", "[textbox][spellcheck][context_menu]")
{
    TestSpellChecker checker;
    checker.add_misspelling("Ths");
    TextBoxControl tb = make_box("Ths is Ths");
    sync_and_check(tb, checker);
    REQUIRE(tb.spelling_errors().size() == 2);

    TestContextMenuHost host;
    SECTION("Add to dictionary")
    {
        host.pick("Add to dictionary");
        (void)tb.handle_context_menu(at_char(1), host);
        REQUIRE(checker.added() == std::vector<std::string>{"Ths"});
    }
    SECTION("Ignore")
    {
        host.pick("Ignore");
        (void)tb.handle_context_menu(at_char(1), host);
        REQUIRE(checker.ignored() == std::vector<std::string>{"Ths"});
    }
    REQUIRE(tb.spelling_errors().empty()); // both occurrences, at once
    tb.tick(0ms);                          // and the generation-triggered re-check agrees
    REQUIRE(tb.spelling_errors().empty());
}

TEST_CASE("Adding a word from one field clears it from another on that field's next tick",
          "[textbox][spellcheck][context_menu]")
{
    TestSpellChecker checker;
    checker.add_misspelling("Ths");
    TextBoxControl a = make_box("Ths");
    TextBoxControl b = make_box("Ths");
    sync_and_check(a, checker);
    sync_and_check(b, checker);
    ui::testing::set_focused(b, false);

    TestContextMenuHost host;
    host.pick("Add to dictionary");
    (void)a.handle_context_menu(at_char(1), host);

    REQUIRE(b.spelling_errors().size() == 1);
    b.tick(0ms);
    REQUIRE(b.spelling_errors().empty());
}

TEST_CASE("Off a squiggle the menu is just the edit commands, enabled by selection and clipboard",
          "[textbox][context_menu]")
{
    TextBoxControl tb = make_box("Hello world");
    TestClipboard  clipboard;
    ui::testing::set_clipboard(&clipboard);
    TestContextMenuHost host;

    REQUIRE(tb.handle_context_menu(at_char(2), host));
    REQUIRE(host.last_texts() == std::vector<std::string>{"Cut", "Copy", "Paste", "", "Select All"});
    REQUIRE(host.enabled("Cut") == false);
    REQUIRE(host.enabled("Copy") == false);
    REQUIRE(host.enabled("Paste") == false); // nothing on the clipboard
    REQUIRE(host.enabled("Select All") == true);
    REQUIRE(tb.caret() == 2); // the right-click moved the caret

    // Inside a selection the right-click keeps it, so Copy acts on it.
    clipboard.set_text("x");
    tb.handle_key(key_down(Key::A, true));
    host.pick("Copy");
    (void)tb.handle_context_menu(at_char(2), host);
    REQUIRE(host.enabled("Cut") == true);
    REQUIRE(host.enabled("Paste") == true);
    REQUIRE(clipboard.text() == "Hello world");
}

TEST_CASE("A keyboard-invoked menu anchors under the caret", "[textbox][context_menu]")
{
    TextBoxControl      tb = make_box("Hello");
    TestContextMenuHost host;
    REQUIRE(tb.handle_context_menu(std::nullopt, host));
    REQUIRE(near(host.last_position().x, TextLeft + 5 * CharW));
    REQUIRE(near(host.last_position().y, tb.bounds().bottom()));
}

// ---- composites forward the context menu, not the spell checker -----------------------------

TEST_CASE(
    "ComboBox/NumberBox/AutoSuggestBox forward the context menu to their internal TextBox, "
    "with no spelling section",
    "[context_menu]")
{
    static HeadlessTextMeasurer measurer;
    TestContextMenuHost         host;

    SECTION("ComboBox")
    {
        ComboBoxControl cb{{"a", "b"}};
        cb.set_editable(true);
        cb.set_text("Hello");
        (void)cb.measure({200.0f, Unbounded}, measurer, Theme::default_light());
        cb.arrange({100.0f, 100.0f, 160.0f, 32.0f});
        ui::testing::set_focused(cb, true);

        REQUIRE(cb.handle_context_menu(std::nullopt, host));
        REQUIRE(host.last_texts() == std::vector<std::string>{"Cut", "Copy", "Paste", "", "Select All"});
    }
    SECTION("NumberBox")
    {
        NumberBoxControl nb{42.0};
        (void)nb.measure({200.0f, Unbounded}, measurer, Theme::default_light());
        nb.arrange({100.0f, 100.0f, 160.0f, 32.0f});
        ui::testing::set_focused(nb, true);

        REQUIRE(nb.handle_context_menu(std::nullopt, host));
        REQUIRE(host.last_texts() == std::vector<std::string>{"Cut", "Copy", "Paste", "", "Select All"});
    }
    SECTION("AutoSuggestBox")
    {
        AutoSuggestBoxControl asb{"Hello"};
        (void)asb.measure({200.0f, Unbounded}, measurer, Theme::default_light());
        asb.arrange({100.0f, 100.0f, 160.0f, 32.0f});
        ui::testing::set_focused(asb, true);

        REQUIRE(asb.handle_context_menu(std::nullopt, host));
        REQUIRE(host.last_texts() == std::vector<std::string>{"Cut", "Copy", "Paste", "", "Select All"});
    }
}

// ---- Window routing ----------------------------------------------------------------------

TEST_CASE("Window routes a right-click to the focusable element under it, spell checker synced",
          "[window][spellcheck][context_menu]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    auto                 stack  = std::make_unique<StackControl>(Orientation::Vertical);
    TextBoxControl*      first  = stack->add(std::make_unique<TextBoxControl>("Ths"));
    TextBoxControl*      second = stack->add(std::make_unique<TextBoxControl>("Ths"));
    window.set_stretch_content(false); // laid out for the control at its desired size, centered
    window.set_content(std::move(stack));

    TestSpellChecker checker;
    checker.add_misspelling("Ths", {"This"});
    TestContextMenuHost host;

    SECTION("no host: nothing happens")
    {
        seams(window).set_spell_checker(&checker);
        window.layout({400.0f, 300.0f}, measurer);
        REQUIRE_FALSE(ui::testing::seams(window).handle_context_menu(std::nullopt));
    }
    SECTION("with a host")
    {
        seams(window).set_spell_checker(&checker);
        seams(window).set_context_menu_host(&host);
        window.layout({400.0f, 300.0f}, measurer);

        const Rect b = second->bounds();
        REQUIRE(ui::testing::seams(window).handle_context_menu(Point{b.left() + 11.0f + CharW, b.top() + b.height * 0.5f}));
        REQUIRE(window.focused() == second);
        REQUIRE(host.show_count() == 1);
        REQUIRE(host.last_texts().front() == "This"); // checked on demand, no tick needed
        REQUIRE(first->spelling_errors().empty());    // never focused, never checked

        // Right-clicking empty space asks nobody.
        REQUIRE_FALSE(ui::testing::seams(window).handle_context_menu(Point{1.0f, 1.0f}));
        REQUIRE(host.show_count() == 1);
    }
}

TEST_CASE("Window asks its spell checker source once, when a text field first needs a checker",
          "[window][spellcheck][context_menu]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    auto                 stack = std::make_unique<StackControl>(Orientation::Vertical);
    TextBoxControl*      box   = stack->add(std::make_unique<TextBoxControl>("Ths"));
    window.set_stretch_content(false);
    window.set_content(std::move(stack));

    TestSpellChecker checker;
    checker.add_misspelling("Ths", {"This"});
    TestContextMenuHost host;
    int                 asked = 0;
    seams(window).set_spell_checker_source(
        [&]() -> SpellChecker*
        {
            ++asked;
            return &checker;
        });
    seams(window).set_context_menu_host(&host);
    window.layout({400.0f, 300.0f}, measurer);
    REQUIRE(asked == 0); // nothing has needed one yet

    const Rect b = box->bounds();
    REQUIRE(seams(window).handle_context_menu(Point{b.left() + 11.0f + CharW, b.top() + b.height * 0.5f}));
    REQUIRE(host.last_texts().front() == "This");
    REQUIRE(seams(window).handle_context_menu(Point{b.left() + 11.0f + CharW, b.top() + b.height * 0.5f}));
    REQUIRE(asked == 1);
}

namespace
{
// The gallery's section card (examples/gallery/main.cpp's Panel), minus the painting.
class Card final : public ContentHost
{
public:
    explicit Card(std::unique_ptr<Element> child)
    {
        set_child(std::move(child));
    }
};

// The gallery's TextBox section, nested the same way: Scroll, Vertical, section Vertical, Card,
// stacked Vertical, {TextBox, Text, TextBox}. Returns the spell-checked field.
TextBoxControl* build_gallery_tree(Window& window)
{
    ui::Control<ui::TextBox> checked;
    window.set_content(ui::Scroll{
        .content = ui::VerticalStack{
            .spacing  = 20.0f,
            .content  = {
                ui::Text{.text = "status"},
                ui::VerticalStack{
                    .spacing  = 6.0f,
                    .content  = {
                        ui::Text{.text = "TextBox"},
                        std::make_unique<Card>(ui::build(ui::VerticalStack{
                            .spacing     = 10.0f,
                            .child_alignment = Alignment::Stretch,
                            .content = {
                                ui::TextBox{.text = "Ada Lovelace", .spell_check = false},
                                ui::Text{.text = "Spell checked — click in, then right-click a squiggled word:"},
                                ui::TextBox{.text = "Ths sentense has a fewe speling erors", .control = checked},
                            }})),
                    }},
            }}});
    return checked;
}

// Click into `tb` through the Window, the way WM_LBUTTONDOWN/UP do, then let one tick pass.
void click_and_tick(Window& window, TextBoxControl& tb, const TextMeasurer& measurer)
{
    const Rect   b = tb.bounds();
    PointerEvent down;
    down.position    = Point{b.left() + 20.0f, b.top() + b.height * 0.5f};
    down.button      = PointerButton::Left;
    down.action      = PointerAction::Down;
    down.click_count = 1;
    ui::testing::seams(window).handle_pointer(down);
    PointerEvent up = down;
    up.action       = PointerAction::Up;
    ui::testing::seams(window).handle_pointer(up);
    window.layout({480.0f, 640.0f}, measurer);
    window.tick(16ms);
}
} // namespace

TEST_CASE("A spell-checked field nested like the gallery's squiggles after a click and a tick", "[window][spellcheck]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    TextBoxControl*      tb = build_gallery_tree(window);
    REQUIRE(tb != nullptr);

    TestSpellChecker checker;
    checker.add_misspelling("Ths", {"This"});
    seams(window).set_spell_checker(&checker);
    window.layout({480.0f, 640.0f}, measurer);

    click_and_tick(window, *tb, measurer);
    REQUIRE(window.focused() == tb);
    CHECK(tb->spelling_errors().size() == 1);

    TestRenderer r;
    window.render(r);
    CHECK(r.count_of<StrokeWavyLine>() == 1);
}

// ---- Windows: the real checker and the Direct2D squiggle -------------------------------------

#ifdef _WIN32

namespace
{
// An environment variable, empty when unset. getenv() is deprecated under MSVC's CRT.
std::string environment(const char* name)
{
#ifdef _MSC_VER
    char*       raw  = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr)
    {
        return {};
    }
    std::string value{raw};
    std::free(raw);
    return value;
#else
    const char* raw = std::getenv(name);
    return raw != nullptr ? std::string{raw} : std::string{};
#endif
}
} // namespace

// A GUI thread is usually STA by the first check (TSF and OLE set it up that way), so the
// checker's own MTA CoInitializeEx gets RPC_E_CHANGED_MODE and has to cope.
TEST_CASE("Win32SpellChecker works on a thread already in a single-threaded apartment", "[win32][spellcheck]")
{
    HRESULT     co        = E_FAIL;
    bool        available = false;
    std::size_t errors    = 0;
    std::thread thread(
        [&]
        {
            co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            {
                win32::Win32SpellChecker checker{"en-US"};
                available = checker.available();
                if (available)
                {
                    errors = checker.check("Ths sentense").size();
                }
            }
            if (SUCCEEDED(co))
            {
                CoUninitialize();
            }
        });
    thread.join();
    REQUIRE(co == S_OK);
    CHECK(available);
    CHECK(errors == 2);
}

// Settings > Time & language > Typing writes its switches as DWORDs under TabletTip\1.7, 0 for
// off; one never touched is missing and on. Read here on its own, so the test holds whichever way
// the machine has them.
TEST_CASE("Win32SpellChecker follows the Typing switches the Settings app writes", "[win32][spellcheck]")
{
    const auto switch_on = [](const wchar_t* name)
    {
        DWORD value = 0;
        DWORD size  = sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER,
                            L"Software\\Microsoft\\TabletTip\\1.7",
                            name,
                            RRF_RT_REG_DWORD,
                            nullptr,
                            &value,
                            &size) != ERROR_SUCCESS ||
               value != 0;
    };
    win32::Win32SpellChecker checker{"en-US"};
    CHECK(checker.autocorrect_enabled() == switch_on(L"EnableAutocorrection"));
    CHECK(checker.highlight_enabled() == switch_on(L"EnableSpellchecking"));
}

// The checker win32::run() makes: no pinned language, so it follows the keyboard layout.
TEST_CASE("The keyboard-language Win32SpellChecker squiggles the gallery-shaped field", "[win32][spellcheck]")
{
    win32::Win32SpellChecker checker;
    const std::string        language = checker.language();
    INFO("keyboard language: " << std::string(language.begin(), language.end()));
    if (!checker.available())
    {
        SKIP("no spell checker for the current keyboard language");
    }
    REQUIRE(checker.highlight_enabled());
    REQUIRE_FALSE(checker.check("Ths sentense").empty());

    HeadlessTextMeasurer measurer;
    Window               window;
    TextBoxControl*      tb = build_gallery_tree(window);
    seams(window).set_spell_checker(&checker);
    window.layout({480.0f, 640.0f}, measurer);

    click_and_tick(window, *tb, measurer);
    REQUIRE(window.focused() == tb);
    CHECK(tb->spelling_errors().size() == 5);

    TestRenderer r;
    window.render(r);
    CHECK(r.count_of<StrokeWavyLine>() == 5);
}

// Never calls add_to_dictionary() or ISpellChecker::AutoCorrect(): both write into the user's own
// Windows dictionary. ignore_for_session() and add_autocorrection() stay in-process.
TEST_CASE("Win32SpellChecker checks, suggests and ignores against the installed en-US dictionary", "[win32][spellcheck]")
{
    win32::Win32SpellChecker checker{"en-US"};
    if (!checker.available())
    {
        SKIP("no en-US spell checker installed on this machine");
    }
    REQUIRE(checker.language() == "en-US");

    const auto errors = checker.check("Ths is a tset.");
    REQUIRE(errors.size() == 2);
    REQUIRE(errors[0].start == 0);
    REQUIRE(errors[0].length == 3);
    REQUIRE(errors[1].start == 9);
    REQUIRE(errors[1].length == 4);
    REQUIRE(checker.check("This is a test.").empty());

    const auto suggestions = checker.suggest("Ths");
    REQUIRE(std::find(suggestions.begin(), suggestions.end(), "This") != suggestions.end());

    const auto before = checker.generation();
    checker.ignore_for_session("asdkjqwe");
    REQUIRE(checker.generation() > before);
    REQUIRE(checker.check("asdkjqwe is nonsense").empty());
}

TEST_CASE("Win32SpellChecker turns table typos into autocorrections, matching their case", "[win32][spellcheck]")
{
    win32::Win32SpellChecker checker{"en-US"};
    if (!checker.available())
    {
        SKIP("no en-US spell checker installed on this machine");
    }

    auto errors = checker.check("Recieve it.");
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0].action == SpellingAction::Replace);
    REQUIRE(errors[0].replacement == "Receive");

    checker.add_autocorrection("wrok", "work");
    errors = checker.check("at wrok");
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0].action == SpellingAction::Replace);
    REQUIRE(errors[0].replacement == "work");

    win32::Win32SpellChecker plain{"en-US", /*builtin_autocorrections=*/false};
    errors = plain.check("recieve");
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0].action == SpellingAction::Suggest);
}

TEST_CASE("Win32SpellChecker for a language without a dictionary is simply unavailable", "[win32][spellcheck]")
{
    win32::Win32SpellChecker checker{"xx-XX"};
    REQUIRE_FALSE(checker.available());
    REQUIRE(checker.check("Ths").empty());
    REQUIRE(checker.suggest("Ths").empty());
}

TEST_CASE("D2DRenderer draws the squiggle under the misspelled word in the theme's color", "[win32][pixel][spellcheck]")
{
    ui::testing::OffscreenCapture offscreen;
    REQUIRE(offscreen.ok());

    Window          window;
    TextBoxControl* tb = window.set_content(std::make_unique<TextBoxControl>("Ths is fine"));
    const Size      client{240.0f, 40.0f};
    window.layout(client, offscreen.measurer());

    TestSpellChecker checker;
    checker.add_misspelling("Ths");
    tb->sync_spell_checker(checker);
    tb->tick(0ms);
    REQUIRE(tb->spelling_errors().size() == 1);

    const Bitmap image = offscreen.capture(window, client);
    REQUIRE_FALSE(image.empty());

    // Only the squiggle has red-dominant pixels (text is gray or black, the field near-white).
    const float text_left  = tb->bounds().left() + Theme::light().padding.left;
    const float word_right = text_left + offscreen.measurer().measure("Ths", Theme::light().text_style).width;
    int         red_inside = 0;
    int         red_beyond = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const Color c = image.get(x, y);
            if (c.r - c.g > 0.25f && c.r - c.b > 0.25f)
            {
                const float fx = static_cast<float>(x);
                (fx >= text_left - 1.0f && fx <= word_right + 1.0f ? red_inside : red_beyond) += 1;
            }
        }
    }
    REQUIRE(red_inside > 3);
    REQUIRE(red_beyond == 0);
}

// The gallery end to end minus the HWND. Set LIBWINUI_SPELL_PNG to a path to also get the frame
// as a PNG.
TEST_CASE("The gallery-shaped field shows red squiggles through the real checker and Direct2D",
          "[win32][pixel][spellcheck]")
{
    ui::testing::OffscreenCapture offscreen;
    REQUIRE(offscreen.ok());
    win32::Win32SpellChecker checker;
    if (!checker.available())
    {
        SKIP("no spell checker for the current keyboard language");
    }

    const OsThemeSnapshot os = win32::Win32ThemeSource{}.snapshot();
    Window                window;
    window.pin_theme(
        resolve_theme({.mode = os.dark ? ThemeMode::Dark : ThemeMode::Light, .variant = ThemeVariant::Fluent2}, os));
    TextBoxControl* tb = build_gallery_tree(window);
    seams(window).set_spell_checker(&checker);
    const Size client{480.0f, 640.0f};
    window.layout(client, offscreen.measurer());

    click_and_tick(window, *tb, offscreen.measurer());
    REQUIRE(window.focused() == tb);
    REQUIRE(tb->spelling_errors().size() == 5);

    const Bitmap image = offscreen.capture(window, client, window.theme().colors().background);
    REQUIRE_FALSE(image.empty());
    if (const std::string png = environment("LIBWINUI_SPELL_PNG"); !png.empty())
    {
        image.write_png(png);
    }

    const Rect b = tb->bounds();
    INFO("field bounds " << b.x << "," << b.y << " " << b.width << "x" << b.height);
    int red = 0;
    for (int y = static_cast<int>(b.top()); y < static_cast<int>(b.bottom()) && y < image.height(); ++y)
    {
        for (int x = std::max(0, static_cast<int>(b.left())); x < static_cast<int>(b.right()) && x < image.width(); ++x)
        {
            const Color c = image.get(x, y);
            if (c.r - c.g > 0.25f && c.r - c.b > 0.25f)
            {
                ++red;
            }
        }
    }
    CHECK(red > 20);
}

#endif // _WIN32
