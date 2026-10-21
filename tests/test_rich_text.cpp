// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/text/RichText.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

namespace
{
constexpr float FontSize = 14.0f;
constexpr float CharW    = FontSize * HeadlessTextMeasurer::AdvanceRatio;
constexpr float LineH    = 20.0f; // Body is 14/20: the type ramp sets its line height
} // namespace

TEST_CASE("Empty RichText measures to zero size and renders nothing", "[richtext]")
{
    RichTextControl      rt;
    HeadlessTextMeasurer measurer;
    const Size           size = rt.measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    REQUIRE(size.width == 0.0f);
    REQUIRE(size.height == 0.0f);

    rt.arrange({0.0f, 0.0f, 0.0f, 0.0f});
    TestRenderer renderer;
    rt.render(renderer, Theme::light());
    REQUIRE(renderer.count() == 0);
}

namespace
{
constexpr float ParagraphGap = 4.0f;

// One paragraph per entry, each but the last ParagraphGap above the next.
std::vector<Paragraph> paragraphs(std::vector<std::vector<Inline>> lines)
{
    std::vector<Paragraph> blocks;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        Paragraph p{.inlines = std::move(lines[i])};
        if (i + 1 < lines.size())
        {
            p.margin.bottom = ParagraphGap;
        }
        blocks.push_back(std::move(p));
    }
    return blocks;
}
} // namespace

TEST_CASE("measure() sums per-paragraph run widths and stacks paragraph heights with margins", "[richtext]")
{
    RichTextControl rt;
    rt.set_blocks(paragraphs({
        {Inline{.text = "Hello "}, Inline{.text = "world", .weight = FontWeight::Bold}}, // 11 chars total
        {Inline{.text = "Second line"}},                                                 // 11 chars
    }));

    HeadlessTextMeasurer measurer;
    const Size           size = rt.measure({Unbounded, Unbounded}, measurer, Theme::default_light());

    // HeadlessTextMeasurer widens bold runs (weight_advance_factor), so the first paragraph is
    // wider than the second although both have 11 chars.
    const float line0_width = 6.0f * CharW + 5.0f * CharW * HeadlessTextMeasurer::weight_advance_factor(FontWeight::Bold);
    const float line1_width     = 11.0f * CharW;
    const float expected_width  = std::max(line0_width, line1_width);
    const float expected_height = 2.0f * LineH + ParagraphGap;
    REQUIRE(size.width == expected_width);
    REQUIRE(size.height == expected_height);
}

TEST_CASE("render() emits one DrawTextRun per run, positioned left-to-right and stacked by paragraph", "[richtext]")
{
    RichTextControl rt;
    rt.set_blocks(paragraphs({
        {Inline{.text = "AB"}, Inline{.text = "CD"}},
        {Inline{.text = "EF"}},
    }));

    HeadlessTextMeasurer measurer;
    const Size           size = rt.measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    rt.arrange({10.0f, 20.0f, size.width, size.height});

    TestRenderer renderer;
    rt.render(renderer, Theme::light());
    REQUIRE(renderer.count() == 3);

    const auto& r0 = std::get<DrawTextRun>(renderer.commands[0]);
    const auto& r1 = std::get<DrawTextRun>(renderer.commands[1]);
    const auto& r2 = std::get<DrawTextRun>(renderer.commands[2]);

    REQUIRE(r0.text == "AB");
    REQUIRE(r0.bounds.left() == 10.0f);
    REQUIRE(r0.bounds.top() == 20.0f);

    REQUIRE(r1.text == "CD");
    REQUIRE(r1.bounds.left() == 10.0f + 2.0f * CharW); // right after "AB"
    REQUIRE(r1.bounds.top() == 20.0f);                 // same line as "AB"

    REQUIRE(r2.text == "EF");
    REQUIRE(r2.bounds.left() == 10.0f); // second paragraph starts back at the left edge
    REQUIRE(r2.bounds.top() == 20.0f + LineH + ParagraphGap);
}

TEST_CASE("Per-run weight and italic reach the emitted DrawTextRun's TextStyle", "[richtext]")
{
    RichTextControl rt;
    rt.set_blocks(paragraphs({{Inline{.text = "Plain"},
                               Inline{.text = "Bold", .weight = FontWeight::Bold},
                               Inline{.text = "Italic", .italic = true}}}));

    HeadlessTextMeasurer measurer;
    const Size           size = rt.measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    rt.arrange({0.0f, 0.0f, size.width, size.height});

    TestRenderer renderer;
    rt.render(renderer, Theme::light());
    REQUIRE(renderer.count() == 3);

    REQUIRE(std::get<DrawTextRun>(renderer.commands[0]).style.weight == FontWeight::Regular);
    REQUIRE_FALSE(std::get<DrawTextRun>(renderer.commands[0]).style.italic);

    REQUIRE(std::get<DrawTextRun>(renderer.commands[1]).style.weight == FontWeight::Bold);
    REQUIRE_FALSE(std::get<DrawTextRun>(renderer.commands[1]).style.italic);

    REQUIRE(std::get<DrawTextRun>(renderer.commands[2]).style.weight == FontWeight::Regular);
    REQUIRE(std::get<DrawTextRun>(renderer.commands[2]).style.italic);
}

TEST_CASE("A run's color falls back to theme.colors().text when unset", "[richtext]")
{
    RichTextControl rt;
    rt.set_blocks(paragraphs({{Inline{.text = "Default"}, Inline{.text = "Red", .color = Color::from_argb(0xFFFF0000)}}}));

    HeadlessTextMeasurer measurer;
    const Size           size = rt.measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    rt.arrange({0.0f, 0.0f, size.width, size.height});

    Theme        theme = Theme::light();
    TestRenderer renderer;
    rt.render(renderer, theme);
    REQUIRE(renderer.count() == 2);

    REQUIRE(std::get<DrawTextRun>(renderer.commands[0]).color == theme.colors().text);
    REQUIRE(std::get<DrawTextRun>(renderer.commands[1]).color == Color::from_argb(0xFFFF0000));
}
