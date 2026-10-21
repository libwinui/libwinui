// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"

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
using Catch::Matchers::WithinAbs;

namespace
{
// 10px text: 5.5px a character, 13.5px a line.
constexpr float Advance = 5.5f;
constexpr float Line    = 13.5f;

TextStyle ten_px()
{
    return TextStyle{10.0f, TextAlign::Leading, TextAlign::Center, "Segoe UI"};
}

TextLayoutRun run(std::string text, std::optional<Color> color = std::nullopt, std::optional<std::size_t> tag = std::nullopt)
{
    return TextLayoutRun{std::move(text), ten_px(), color, tag};
}

TextLayoutOptions width(float w, TextWrapping wrapping = TextWrapping::Wrap, TextAlignment align = TextAlignment::Left)
{
    TextLayoutOptions o;
    o.max_width  = w;
    o.wrapping   = wrapping;
    o.alignment  = align;
    o.base_style = ten_px();
    return o;
}
} // namespace

TEST_CASE("TextLayout keeps unbroken text on one line and joins paragraphs with a newline", "[text][layout]")
{
    TextLayout layout;
    layout.set_content({{{run("Hello "), run("world")}}, {{run("Second")}}});
    const HeadlessTextMeasurer m;
    layout.layout(width(0.0f), m);

    REQUIRE(layout.text() == "Hello world\nSecond");
    REQUIRE(layout.lines().size() == 2);
    REQUIRE_THAT(layout.size().width, WithinAbs(11 * Advance, 0.01));
    REQUIRE_THAT(layout.size().height, WithinAbs(2 * Line, 0.01));
    REQUIRE(layout.lines()[1].begin == 12);
    REQUIRE(layout.fragments().size() == 3); // one per run and line
}

TEST_CASE("TextLayout breaks after spaces, across runs, and hangs the trailing space", "[text][layout]")
{
    TextLayout layout;
    // "aaaa bb" + "bb cccc": the word "bbbb" spans two runs and must not be split between them.
    layout.set_content({{{run("aaaa bb"), run("bb cccc")}}});
    const HeadlessTextMeasurer m;
    layout.layout(width(9 * Advance), m);

    REQUIRE(layout.lines().size() == 2);
    REQUIRE(layout.lines()[0].begin == 0);
    REQUIRE(layout.lines()[0].end == 10); // "aaaa bbbb ": the space hangs
    REQUIRE_THAT(layout.lines()[0].width, WithinAbs(9 * Advance, 0.01));
    REQUIRE(layout.lines()[1].begin == 10);
    REQUIRE_THAT(layout.lines()[1].top, WithinAbs(Line, 0.01));
}

TEST_CASE("TextLayout splits an overlong word under Wrap and lets it overflow under WrapWholeWords", "[text][layout]")
{
    const HeadlessTextMeasurer m;
    TextLayout                 layout;
    layout.set_content({{{run("abcdefghij")}}});

    layout.layout(width(4 * Advance, TextWrapping::Wrap), m);
    REQUIRE(layout.lines().size() == 3); // abcd / efgh / ij

    layout.layout(width(4 * Advance, TextWrapping::WrapWholeWords), m);
    REQUIRE(layout.lines().size() == 1);
    REQUIRE_THAT(layout.size().width, WithinAbs(10 * Advance, 0.01));
}

TEST_CASE("TextLayout treats a newline inside a run as a line break", "[text][layout]")
{
    const HeadlessTextMeasurer m;
    TextLayout                 layout;
    layout.set_content({{{run("one\ntwo\n")}}});
    layout.layout(width(0.0f), m);

    REQUIRE(layout.lines().size() == 3); // the trailing break opens an empty line
    REQUIRE(layout.lines()[1].begin == 4);
    REQUIRE(layout.lines()[1].end == 7);
    REQUIRE(layout.lines()[2].begin == 8);
    REQUIRE(layout.lines()[2].begin == layout.lines()[2].end);
}

TEST_CASE("TextLayout aligns lines, and Justify spreads all but the last", "[text][layout]")
{
    const HeadlessTextMeasurer m;
    TextLayout                 layout;
    layout.set_content({{{run("aa bb cc dd")}}});

    layout.layout(width(20 * Advance, TextWrapping::Wrap, TextAlignment::Right), m);
    REQUIRE_THAT(layout.lines()[0].left, WithinAbs(9 * Advance, 0.01));

    layout.layout(width(20 * Advance, TextWrapping::Wrap, TextAlignment::Center), m);
    REQUIRE_THAT(layout.lines()[0].left, WithinAbs(4.5f * Advance, 0.01));

    // 8 characters a line: "aa bb cc" / "dd". The first line's two gaps take the 2 spare
    // characters' worth of room between them; the last line stays left-aligned.
    layout.layout(width(10 * Advance, TextWrapping::Wrap, TextAlignment::Justify), m);
    REQUIRE(layout.lines().size() == 2);
    const auto& f = layout.fragments();
    REQUIRE(layout.lines()[0].fragment_end - layout.lines()[0].fragment_begin == 3); // a word each
    REQUIRE_THAT(f[2].rect.x + 2 * Advance, WithinAbs(10 * Advance, 0.01));          // "cc" ends at the edge
    REQUIRE_THAT(f[layout.lines()[1].fragment_begin].rect.x, WithinAbs(0.0f, 0.01));
}

TEST_CASE("TextLayout paragraph margins offset lines and add to the height", "[text][layout]")
{
    const HeadlessTextMeasurer m;
    TextLayout                 layout;
    layout.set_content({{{run("a")}, Edges{.bottom = 4}}, {{run("b")}, Edges{.left = 10}}});
    layout.layout(width(0.0f), m);

    REQUIRE_THAT(layout.lines()[1].top, WithinAbs(Line + 4, 0.01));
    REQUIRE_THAT(layout.lines()[1].left, WithinAbs(10, 0.01));
    REQUIRE_THAT(layout.size().height, WithinAbs(2 * Line + 4, 0.01));
}

TEST_CASE("TextLayout hit tests points to offsets and offsets to carets", "[text][layout]")
{
    const HeadlessTextMeasurer m;
    TextLayout                 layout;
    layout.set_content({{{run("abcd efgh")}}});
    layout.layout(width(5 * Advance), m); // "abcd " / "efgh"

    REQUIRE(layout.hit_test({0.4f * Advance, 1.0f}) == 0);
    REQUIRE(layout.hit_test({1.6f * Advance, 1.0f}) == 2);
    REQUIRE(layout.hit_test({100.0f, 1.0f}) == 4); // past a wrapped line's end: before its space
    REQUIRE(layout.hit_test({2.0f * Advance, Line + 1.0f}) == 7);
    REQUIRE(layout.hit_test({0.0f, 100.0f}) == 5); // below everything: the last line

    const Rect caret = layout.caret_rect(7);
    REQUIRE_THAT(caret.x, WithinAbs(2 * Advance, 0.01));
    REQUIRE_THAT(caret.y, WithinAbs(Line, 0.01));
    // The wrap point itself is drawn at the start of the next line.
    REQUIRE_THAT(layout.caret_rect(5).y, WithinAbs(Line, 0.01));

    const auto rects = layout.range_rects(2, 7);
    REQUIRE(rects.size() == 2);
    REQUIRE_THAT(rects[0].x, WithinAbs(2 * Advance, 0.01));
    REQUIRE_THAT(rects[1].width, WithinAbs(2 * Advance, 0.01));
}

TEST_CASE("TextLayout reports tags and fitting lines, and lays out from a later offset", "[text][layout]")
{
    const HeadlessTextMeasurer m;
    TextLayout                 layout;
    layout.set_content({{{run("aaaa "), run("link", std::nullopt, 7), run(" bbbb cccc dddd")}}});
    layout.layout(width(10 * Advance), m);

    REQUIRE(layout.tag_at({6 * Advance, 1.0f}) == 7);
    REQUIRE(layout.tag_at({1 * Advance, 1.0f}) == std::nullopt);

    REQUIRE(layout.lines().size() == 3);
    REQUIRE(layout.fitting_lines(2 * Line + 1) == 2);
    const std::size_t rest = layout.lines()[1].next;

    TextLayout overflow;
    overflow.set_content(layout.content());
    overflow.layout(width(10 * Advance), m, rest);
    REQUIRE(overflow.lines().size() == 1);
    REQUIRE(overflow.lines()[0].begin == rest);
    REQUIRE_THAT(overflow.lines()[0].top, WithinAbs(0.0f, 0.01));
}

TEST_CASE("TextLayout draws highlights under the text and recolors the selected part", "[text][layout]")
{
    const HeadlessTextMeasurer m;
    TextLayout                 layout;
    layout.set_content({{{run("hello world")}}});
    layout.layout(width(0.0f), m);

    TestRenderer      renderer;
    TextLayout::Paint paint;
    paint.foreground = colors::Black;
    paint.selection  = TextHighlight{6, 11, colors::White, Color::from_argb(0xFF0000FF)};
    layout.render(renderer, {10.0f, 20.0f}, paint);

    // background, the text, then the clip + recolored text + pop.
    REQUIRE(renderer.count() == 5);
    const auto* bg = std::get_if<FillRoundedRect>(&renderer.commands[0]);
    REQUIRE(bg);
    REQUIRE_THAT(bg->bounds.x, WithinAbs(10 + 6 * Advance, 0.01));
    const auto* text = std::get_if<DrawTextRun>(&renderer.commands[1]);
    REQUIRE(text);
    REQUIRE(text->text == "hello world");
    REQUIRE(text->color == colors::Black);
    const auto* recolor = std::get_if<DrawTextRun>(&renderer.commands[3]);
    REQUIRE(recolor);
    REQUIRE(recolor->color == Color::from_argb(0xFF0000FF));
}

TEST_CASE("TextLayout keeps its measurements when only colors change", "[text][layout]")
{
    TextLayout layout;
    REQUIRE(layout.set_content({{{run("a")}}}));
    REQUIRE_FALSE(layout.set_content({{{run("a", colors::White)}}}));
    REQUIRE(layout.set_content({{{run("b")}}}));
}

TEST_CASE("A copied TextLayout reads its own runs", "[text][layout]")
{
    const HeadlessTextMeasurer m;
    std::optional<TextLayout>  source{std::in_place};
    source->set_content({{{run("Hello "), run("world")}}});
    source->layout(width(0.0f), m);

    TextLayout copy = *source;
    TextLayout assigned;
    assigned = *source;
    source.reset(); // the runs the copies point at must be their own

    for (const TextLayout* layout : {&copy, &assigned})
    {
        REQUIRE(layout->run_count() == 2);
        REQUIRE(&layout->run(0) == &layout->content()[0].runs[0]);
        REQUIRE(layout->run(1).text == "world");
        REQUIRE(layout->text() == "Hello world");
        REQUIRE(layout->lines().size() == 1);
    }
    copy.layout(width(6 * Advance), m); // still lays out: "Hello " / "world"
    REQUIRE(copy.lines().size() == 2);
}
