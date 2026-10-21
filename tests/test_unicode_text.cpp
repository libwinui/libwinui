// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Describe.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextAnalyzer.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/text/PasswordBox.hpp"
#include "winui/ui/text/RichEditBox.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <string_view>
#include <utility>
#include <variant>

#include <cstddef>

#ifdef _WIN32
#include "platform/windows/Win32SpellChecker.hpp"
#include "platform/windows/Win32TextAnalyzer.hpp"
#endif

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

namespace
{

// U+0065 U+0301: an e and a combining acute, one cluster of two code points.
const std::string Acute = "e\u0301";
// The family ZWJ sequence: man, ZWJ, woman, ZWJ, girl. Five code points, 18 bytes.
const std::string Family = "\U0001F468\u200D\U0001F469\u200D\U0001F467";
// A grinning face: one code point past the BMP (a surrogate pair in UTF-16).
const std::string Grin = "\U0001F600";
// A flag: two regional indicators.
const std::string SwissFlag = "\U0001F1E8\U0001F1ED";

std::vector<std::size_t> clusters(std::string_view text)
{
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < text.size();)
    {
        i = text_analyzer().next_cluster(text, i);
        out.push_back(i);
    }
    return out;
}

TextBoxControl make_box(std::string text)
{
    static HeadlessTextMeasurer measurer;
    TextBoxControl              box{std::move(text)};
    const Size                  size = box.measure({400.0f, Unbounded}, measurer, Theme::default_light());
    box.arrange({0.0f, 0.0f, size.width, size.height});
    ui::testing::set_focused(box, true);
    return box;
}

void press(Element& e, Key key)
{
    e.handle_key({key, KeyAction::Down});
    e.handle_key({key, KeyAction::Up});
}

void type(Element& e, std::string_view text)
{
    for (std::size_t i = 0; i < text.size(); i = next_code_point(text, i))
    {
        e.handle_text(text.substr(i, next_code_point(text, i) - i));
    }
}

} // namespace

// ---- the built-in TextAnalyzer --------------------------------------------------------------

TEST_CASE("Clusters keep combining marks, ZWJ sequences and flags together", "[utf8][text-analyzer]")
{
    const std::string text   = "a" + Acute + Family + Grin + SwissFlag + "z";
    const std::size_t a      = 1;
    const std::size_t acute  = a + Acute.size();
    const std::size_t family = acute + Family.size();
    const std::size_t grin   = family + Grin.size();
    const std::size_t flag   = grin + SwissFlag.size();
    REQUIRE(clusters(text) == std::vector<std::size_t>{a, acute, family, grin, flag, text.size()});

    // Backwards lands on the same boundaries.
    std::vector<std::size_t> back;
    for (std::size_t i = text.size(); i > 0;)
    {
        i = text_analyzer().previous_cluster(text, i);
        back.push_back(i);
    }
    REQUIRE(back == std::vector<std::size_t>{flag, grin, family, acute, a, 0});
}

TEST_CASE("The built-in analyzer cases Latin, Greek and Cyrillic and classifies words", "[utf8][text-analyzer]")
{
    const TextAnalyzer builtin;
    REQUIRE(builtin.to_upper("stra\u00DFe \u00E4 \u03B1 \u0434") == "STRA\u00DFE \u00C4 \u0391 \u0414");
    REQUIRE(builtin.to_lower("\u00C4\u0178\u0141") == "\u00E4\u00FF\u0142");
    REQUIRE(builtin.is_word_char(U'\x00E9'));
    REQUIRE(builtin.is_word_char(U'\x6F22'));
    REQUIRE(builtin.is_word_char(U'\x0301'));
    REQUIRE_FALSE(builtin.is_word_char(U'\x2014')); // an em dash
    REQUIRE_FALSE(builtin.is_word_char(U'\x3000')); // an ideographic space
    REQUIRE_FALSE(builtin.is_word_char(U' '));
}

TEST_CASE("word_run takes accented words whole", "[utf8][text-analyzer]")
{
    const std::string text = "Caf\u00E9 au lait";
    // "Café" is five bytes; a double-click on its é selects all of it.
    REQUIRE(word_run(text, 3, false) == std::pair<std::size_t, std::size_t>{0, 5});
    REQUIRE(word_run(text, 5, false) == std::pair<std::size_t, std::size_t>{5, 6});
}

// ---- TextBox ---------------------------------------------------------------------------------

TEST_CASE("TextBox moves the caret and deletes by cluster", "[utf8][textbox]")
{
    const std::string text = "a" + Grin + Family + Acute + "b";
    TextBoxControl    box  = make_box(text);
    REQUIRE(box.caret() == text.size());

    press(box, Key::ArrowLeft);
    REQUIRE(box.caret() == text.size() - 1);
    press(box, Key::ArrowLeft); // over e + combining acute
    REQUIRE(box.caret() == 1 + Grin.size() + Family.size());
    press(box, Key::ArrowLeft); // over the whole family
    REQUIRE(box.caret() == 1 + Grin.size());
    press(box, Key::ArrowLeft); // over the surrogate pair's code point
    REQUIRE(box.caret() == 1);
    press(box, Key::ArrowRight);
    REQUIRE(box.caret() == 1 + Grin.size());

    press(box, Key::Delete); // the family, all five code points
    REQUIRE(box.text() == "a" + Grin + Acute + "b");
    press(box, Key::Backspace); // the grinning face, never half of it
    REQUIRE(box.text() == "a" + Acute + "b");
    REQUIRE(box.caret() == 1);
    REQUIRE(is_valid_utf8(box.text()));
}

TEST_CASE("TextBox takes typed text past the BMP and keeps it valid", "[utf8][textbox]")
{
    TextBoxControl box = make_box("");
    type(box, "D\u00FCrrenhauser " + Grin);
    REQUIRE(box.text() == "D\u00FCrrenhauser " + Grin);
    REQUIRE(box.caret() == box.text().size());
}

TEST_CASE("TextBox MaxLength counts code points and never splits a cluster", "[utf8][textbox]")
{
    TextBoxControl box = make_box("");
    box.set_max_length(3);
    type(box, "\u00E4\u00F6");
    REQUIRE(box.text() == "\u00E4\u00F6"); // two code points, four bytes: still room for one
    box.handle_text(Acute);                // two code points: does not fit whole, so none of it
    REQUIRE(box.text() == "\u00E4\u00F6");
    box.handle_text(Grin);
    REQUIRE(box.text() == "\u00E4\u00F6" + Grin);
}

TEST_CASE("TextBox CharacterCasing maps what is typed past ASCII", "[utf8][textbox]")
{
    TextBoxControl box = make_box("");
    box.set_character_casing(CharacterCasing::Upper);
    type(box, "caf\u00E9");
    REQUIRE(box.text() == "CAF\u00C9");
}

TEST_CASE("A password is masked one bullet per code point", "[utf8][passwordbox]")
{
    static HeadlessTextMeasurer measurer;
    PasswordBoxControl          box;
    const Size                  size = box.measure({300.0f, Unbounded}, measurer, Theme::default_light());
    box.arrange({0.0f, 0.0f, 300.0f, size.height});
    ui::testing::set_focused(box, true);
    type(box, "\u00E4" + Grin);
    REQUIRE(box.password() == "\u00E4" + Grin);
    REQUIRE(ui::automation_providers(box).value->text_value() == "\u25CF\u25CF");

    TestRenderer r;
    box.render(r, Theme::light());
    bool masked = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            masked = masked || t->text == "\u25CF\u25CF";
        }
    }
    REQUIRE(masked);
}

// ---- RichEditBox and RTF ---------------------------------------------------------------------

TEST_CASE("RichEditBox steps over clusters and finds text case-insensitively past ASCII", "[utf8][richeditbox]")
{
    static HeadlessTextMeasurer measurer;
    RichEditBoxControl          box{"x" + Family + "y"};
    const Size                  size = box.measure({300.0f, Unbounded}, measurer, Theme::default_light());
    box.arrange({0.0f, 0.0f, 300.0f, std::max(size.height, 100.0f)});
    ui::testing::set_focused(box, true);
    box.select(1, 1);
    press(box, Key::ArrowRight);
    REQUIRE(box.caret() == 1 + Family.size());
    press(box, Key::Backspace);
    REQUIRE(box.text() == "xy");

    RichEditBoxControl caf{"CAF\u00C9 and caf\u00E9"};
    const auto         found = caf.find_all("caf\u00E9");
    REQUIRE(found == std::vector<std::pair<std::size_t, std::size_t>>{{0, 5}, {10, 15}});
}

TEST_CASE("RTF writes a character past the BMP as a surrogate pair and reads it back", "[utf8][rtf]")
{
    RichEditBoxControl box{"a" + Grin + "\u00E9"};
    const std::string  rtf = box.save_rtf();
    REQUIRE(rtf.find("\\u-10179?\\u-8704?") != std::string::npos); // D83D DE00, signed
    REQUIRE(rtf.find("\\u233?") != std::string::npos);

    RichEditBoxControl back;
    back.load_rtf(rtf);
    REQUIRE(back.text() == "a" + Grin + "\u00E9");
}

// ---- measurement, layout, the semantic dump ---------------------------------------------------

TEST_CASE("The headless measurer and TextLayout count code points, not bytes", "[utf8][layout]")
{
    const HeadlessTextMeasurer measurer;
    const TextStyle            style{};
    REQUIRE(measurer.measure("\u00E4\u00F6\u00FC", style).width == measurer.measure("aou", style).width);

    // A word too long for its line splits between code points, never inside one.
    TextLayout layout;
    layout.set_content({TextLayoutParagraph{{TextLayoutRun{"\u00E4\u00E4\u00E4\u00E4\u00E4\u00E4\u00E4\u00E4", style}}}});
    TextLayoutOptions options;
    options.max_width = measurer.measure("\u00E4\u00E4\u00E4", style).width + 1.0f;
    options.wrapping  = TextWrapping::Wrap;
    layout.layout(options, measurer);
    REQUIRE(layout.lines().size() == 3);
    for (const TextLayout::Fragment& f : layout.fragments())
    {
        REQUIRE(is_valid_utf8(f.text));
    }
    REQUIRE(layout.fragments().front().text == "\u00E4\u00E4\u00E4");
    REQUIRE(layout.hit_test(Point{options.max_width, 1.0f}) == 6); // after the third ä
}

TEST_CASE("The semantic dump escapes per code point", "[utf8][describe]")
{
    TestRenderer r;
    r.draw_text("\u00E4" + Grin, Rect{0, 0, 10, 10}, colors::Black, TextStyle{});
    const std::string dump = describe(r.commands);
    REQUIRE(dump.find("\\u00E4\\U0001F600") != std::string::npos);
}

// ---- the UTF-16 seams --------------------------------------------------------------------------

TEST_CASE("UTF-8 and UTF-16 offsets map onto each other", "[utf8]")
{
    const std::string text = "a\u00E4" + Grin + "b"; // bytes 1 + 2 + 4 + 1, units 1 + 1 + 2 + 1
    REQUIRE(utf16_length(text) == 5);
    REQUIRE(utf16_length(std::string_view{text}.substr(0, 3)) == 2);
    REQUIRE(utf8_offset_from_utf16(text, 2) == 3);
    REQUIRE(utf8_offset_from_utf16(text, 3) == 3); // inside the pair: before it
    REQUIRE(utf8_offset_from_utf16(text, 4) == 7);
    REQUIRE(utf8_offset_from_utf16(text, 99) == text.size());
}

TEST_CASE("WM_CHAR's surrogate halves are paired into one code point", "[utf8]")
{
    Utf16Decoder decoder;
    REQUIRE(decoder.feed(u'a') == "a");
    REQUIRE(decoder.feed(char16_t{0xD83D}).empty()); // waits for its partner
    REQUIRE(decoder.feed(char16_t{0xDE00}) == Grin);

    // A high half followed by something else, and a low half alone, become U+FFFD.
    REQUIRE(decoder.feed(char16_t{0xD83D}).empty());
    REQUIRE(decoder.feed(u'b') == "\uFFFDb");
    REQUIRE(decoder.feed(char16_t{0xDE00}) == "\uFFFD");
    REQUIRE(decoder.feed(char16_t{0xD83D}).empty());
    REQUIRE(decoder.feed(char16_t{0xD83D}) == "\uFFFD");
    REQUIRE(decoder.feed(char16_t{0xDE00}) == Grin);
}

#ifdef _WIN32

// ---- the Windows TextAnalyzer ------------------------------------------------------------------

TEST_CASE("Win32TextAnalyzer answers from the system ICU", "[utf8][text-analyzer][win32]")
{
    const win32::Win32TextAnalyzer analyzer;
    if (!analyzer.has_icu())
    {
        SKIP("no icu.dll on this Windows (before 1903)");
    }
    const std::string        text = "a" + Acute + Family + SwissFlag + "\uAC01"; // and a Hangul syllable
    std::vector<std::size_t> stops;
    for (std::size_t i = 0; i < text.size();)
    {
        i = analyzer.next_cluster(text, i);
        stops.push_back(i);
    }
    const std::size_t acute  = 1 + Acute.size();
    const std::size_t family = acute + Family.size();
    const std::size_t flag   = family + SwissFlag.size();
    REQUIRE(stops == std::vector<std::size_t>{1, acute, family, flag, text.size()});
    REQUIRE(analyzer.previous_cluster(text, family) == acute);

    REQUIRE(analyzer.to_upper("stra\u00DFe") == "STRASSE"); // full case mapping
    REQUIRE(analyzer.to_lower("\u00C4RGER") == "\u00E4rger");
    REQUIRE(analyzer.is_word_char(U'\x6F22'));
    REQUIRE(analyzer.is_word_char(U'\x0301'));
    REQUIRE_FALSE(analyzer.is_word_char(U'\x2014'));
}

TEST_CASE("Win32SpellChecker reports byte offsets past non-ASCII text", "[utf8][spellcheck][win32]")
{
    win32::Win32SpellChecker checker{"en-US"};
    if (!checker.available())
    {
        SKIP("no en-US spell checker installed on this machine");
    }
    // "Caf\u00E9 " is six bytes but five UTF-16 units; the grin is four bytes but two units.
    const std::string text   = "Caf\u00E9 " + Grin + " tset";
    const auto        errors = checker.check(text);
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0].start == text.size() - 4);
    REQUIRE(errors[0].length == 4);
}

#endif
