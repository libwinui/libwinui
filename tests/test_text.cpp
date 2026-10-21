// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestClipboard.hpp"
#include "winui/testing/TestContextMenuHost.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Inlines.hpp"
#include "winui/ui/text/RichText.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"

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
using Catch::Matchers::WithinAbs;

namespace
{
// Theme::light()'s body text is 14px: 7.7px a character, 18.9px a line.
constexpr float Advance    = 14.0f * HeadlessTextMeasurer::AdvanceRatio;
constexpr float LineHeight = 20.0f; // Body is 14/20: the type ramp sets its line height

// Controls keep the measurer for hit testing and drawing, as every host's measurer outlives them.
const HeadlessTextMeasurer m;

template <typename E>
Size lay_out(E& e, Rect rect)
{
    const Size size = e.measure({rect.width, rect.height}, m, Theme::default_light());
    e.arrange(rect);
    return size;
}

PointerEvent pointer(PointerAction action, Point at, int clicks = 1)
{
    PointerEvent e;
    e.action      = action;
    e.button      = action == PointerAction::Move ? PointerButton::None : PointerButton::Left;
    e.position    = at;
    e.click_count = clicks;
    return e;
}

std::vector<const DrawTextRun*> texts(const TestRenderer& r)
{
    std::vector<const DrawTextRun*> out;
    for (const auto& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            out.push_back(t);
        }
    }
    return out;
}
} // namespace

TEST_CASE("A TextControl's inlines draw as runs of their own formatting", "[text][textblock]")
{
    TextControl block;
    block.set_inlines({
        {.text = "Times", .font_family = "Times New Roman", .color = Color::from_argb(0xFF808080)},
        line_break(),
        {.text = "Text can be "},
        {.text = "bold", .weight = FontWeight::Bold},
        {.text = ", "},
        {.text = "italic", .italic = true},
        {.text = " or "},
        {.text = "underlined", .underline = true},
    });
    REQUIRE(block.text() == "Times\nText can be bold, italic or underlined");

    lay_out(block, {0, 0, 1000, 200});
    TestRenderer r;
    block.render(r, Theme::light());
    const auto runs = texts(r);
    REQUIRE(runs.size() == 7);
    REQUIRE(runs[0]->style.font_family == "Times New Roman");
    REQUIRE(runs[0]->color == Color::from_argb(0xFF808080));
    REQUIRE_THAT(runs[1]->bounds.y, WithinAbs(LineHeight, 0.01)); // after the line break
    REQUIRE(runs[2]->style.weight == FontWeight::Bold);
    REQUIRE(runs[4]->style.italic);
    REQUIRE(runs[6]->style.underline);
    REQUIRE(runs[6]->color == Theme::light().colors().text);
}

TEST_CASE("CharacterSpacing widens every character", "[text][textblock]")
{
    TextControl block{"abcd"};
    block.set_character_spacing(200);
    const Size size = block.measure({Unbounded, Unbounded}, m, Theme::default_light());
    block.arrange({0, 0, 0, 0});
    REQUIRE_THAT(size.width, WithinAbs(4 * (Advance + 0.2f * 14.0f), 0.01));

    TestRenderer r;
    block.render(r, Theme::light());
    REQUIRE(texts(r)[0]->style.character_spacing == 200);
}

TEST_CASE("TextAlignment places a plain TextControl's text and justifies a wrapped one", "[text][textblock]")
{
    TextControl block{"abc"};
    block.set_text_alignment(TextAlignment::Right);
    lay_out(block, {0, 0, 100, 30});
    TestRenderer r;
    block.render(r, Theme::light());
    REQUIRE(texts(r)[0]->style.horizontal == TextAlign::Trailing);

    TextControl justified{"aa bb cc dd"};
    justified.set_wrapping(TextWrapping::Wrap);
    justified.set_text_alignment(TextAlignment::Justify);
    lay_out(justified, {0, 0, 10 * Advance, 100});
    TestRenderer rj;
    justified.render(rj, Theme::light());
    const auto runs = texts(rj);
    REQUIRE(runs.size() == 4); // a word each: three spread over the first line, "dd" alone
    REQUIRE_THAT(runs[2]->bounds.x + 2 * Advance, WithinAbs(10 * Advance, 0.01));
}

TEST_CASE("A selectable TextControl selects by drag, double click and Ctrl+A, and copies", "[text][textblock]")
{
    TextControl block{"hello brave world"};
    REQUIRE_FALSE(block.focusable());
    block.set_selection_enabled(true);
    REQUIRE(block.focusable());
    lay_out(block, {10, 10, 400, 30});

    block.handle_pointer(pointer(PointerAction::Down, {10 + 1 * Advance, 15}));
    block.handle_pointer(pointer(PointerAction::Move, {10 + 5 * Advance, 15}));
    block.handle_pointer(pointer(PointerAction::Up, {10 + 5 * Advance, 15}));
    REQUIRE(block.selection() == std::pair<std::size_t, std::size_t>{1, 5});
    REQUIRE(block.selected_text() == "ello");

    block.handle_pointer(pointer(PointerAction::Down, {10 + 8 * Advance, 15}, 2));
    REQUIRE(block.selected_text() == "brave");

    TestClipboard clipboard;
    ui::testing::set_clipboard(&clipboard);
    ui::testing::set_focused(block, true);
    block.handle_key(KeyEvent{Key::A, KeyAction::Down, {.ctrl = true}});
    block.handle_key(KeyEvent{Key::C, KeyAction::Down, {.ctrl = true}});
    REQUIRE(clipboard.text() == "hello brave world");

    // Drawn only while focused: the highlight, then the text, then the recolored selection.
    TestRenderer r;
    block.render(r, Theme::light());
    REQUIRE(std::get<FillRoundedRect>(r.commands[0]).color == Theme::light().colors().selection);
    ui::testing::set_focused(block, false);
    TestRenderer unfocused;
    block.render(unfocused, Theme::light());
    REQUIRE(unfocused.count_of<FillRoundedRect>() == 0);
}

TEST_CASE("SelectionHighlightColor paints the selection, and the context menu copies it", "[text][textblock]")
{
    TextControl block{"orange text"};
    block.set_selection_enabled(true);
    block.set_selection_highlight_color(Color::from_argb(0xFFFF8C00));
    lay_out(block, {0, 0, 400, 30});
    block.select(0, 6);
    ui::testing::set_focused(block, true);

    TestRenderer r;
    block.render(r, Theme::light());
    REQUIRE(std::get<FillRoundedRect>(r.commands[0]).color == Color::from_argb(0xFFFF8C00));

    TestClipboard       clipboard;
    TestContextMenuHost menu;
    ui::testing::set_clipboard(&clipboard);
    menu.pick("Copy");
    REQUIRE(block.handle_context_menu(Point{5, 5}, menu));
    REQUIRE(clipboard.text() == "orange");
}

TEST_CASE("A hyperlink inline is underlined in the link color and invoked by a click", "[text][textblock]")
{
    int         clicks = 0;
    TextControl block;
    block.set_inlines({{.text = "see "}, {.text = "link", .on_click = [&] { ++clicks; }}});
    lay_out(block, {0, 0, 400, 30});

    const Theme  theme = Theme::light();
    TestRenderer r;
    block.render(r, theme);
    const auto runs = texts(r);
    REQUIRE(runs[1]->style.underline);
    REQUIRE(runs[1]->color == theme.colors().link);

    const Point on_link{5.5f * Advance, 5};
    block.handle_pointer(pointer(PointerAction::Down, on_link));
    block.handle_pointer(pointer(PointerAction::Up, on_link));
    REQUIRE(clicks == 1);
    // A release somewhere else does not click.
    block.handle_pointer(pointer(PointerAction::Down, on_link));
    block.handle_pointer(pointer(PointerAction::Up, {1, 5}));
    REQUIRE(clicks == 1);
}

TEST_CASE("RichText lays out paragraphs with margins and wraps by default", "[text][richtext]")
{
    RichTextControl rich;
    rich.set_blocks({{.inlines = {{.text = "aaaa bbbb"}}, .margin = Edges{.bottom = 4}}, {.inlines = {{.text = "cc"}}}});
    REQUIRE(rich.wrapping() == TextWrapping::Wrap);
    REQUIRE(rich.selection_enabled());
    REQUIRE(rich.plain_text() == "aaaa bbbb\ncc");

    const Size size = lay_out(rich, {0, 0, 5 * Advance, 200});
    REQUIRE_THAT(size.height, WithinAbs(3 * LineHeight + 4, 0.01)); // "aaaa " / "bbbb" / 4 / "cc"
}

TEST_CASE("RichText hands what does not fit to its overflow containers", "[text][richtext]")
{
    // Three 5-character columns (a word a line), each two lines tall; the last one, with nowhere
    // further to go, shows everything left.
    auto rich   = std::make_unique<RichTextControl>();
    auto first  = std::make_unique<RichTextOverflowControl>();
    auto second = std::make_unique<RichTextOverflowControl>();
    rich->set_blocks({{.inlines = {{.text = "aaaa bbbb cccc dddd eeee ffff gggg"}}}});
    rich->set_overflow_target(first.get());
    first->set_overflow_target(second.get());
    REQUIRE(second->content_source() == rich.get());

    const float col = 5 * Advance;
    lay_out(*rich, {0, 0, col, 2 * LineHeight});
    lay_out(*first, {col, 0, col, 2 * LineHeight});
    lay_out(*second, {2 * col, 0, col, 2 * LineHeight});

    REQUIRE(rich->has_overflow_content());
    REQUIRE(first->content_start() == 10); // "cccc ..."
    REQUIRE(first->has_overflow_content());
    REQUIRE(second->content_start() == 20);
    REQUIRE_FALSE(second->has_overflow_content());

    TestRenderer r;
    second->render(r, Theme::light());
    const auto runs = texts(r);
    REQUIRE(runs.size() == 3);
    REQUIRE(runs[0]->text == "eeee ");
    REQUIRE_THAT(runs[0]->bounds.x, WithinAbs(2 * col, 0.01));

    // A selection made in an overflow is the RichText's.
    first->handle_pointer(pointer(PointerAction::Down, {col + 1, 2}, 2));
    REQUIRE(rich->selected_text() == "cccc");

    rich.reset();
    REQUIRE(first->content_source() == nullptr);
    REQUIRE(second->content_source() == nullptr);
}

TEST_CASE("RichText's text highlighters paint their ranges", "[text][richtext]")
{
    RichTextControl rich;
    rich.set_blocks({{.inlines = {{.text = "Lorem ipsum dolor"}}}});
    rich.set_text_highlighters(
        {TextHighlighter{.ranges = {{6, 5}}, .background = Color::from_argb(0xFFFFFF00), .foreground = std::nullopt}});
    lay_out(rich, {0, 0, 400, 100});

    TestRenderer r;
    rich.render(r, Theme::light());
    const auto& bg = std::get<FillRoundedRect>(r.commands[0]);
    REQUIRE(bg.color == Color::from_argb(0xFFFFFF00));
    REQUIRE_THAT(bg.bounds.x, WithinAbs(6 * Advance, 0.01));
    REQUIRE_THAT(bg.bounds.width, WithinAbs(5 * Advance, 0.01));
}

TEST_CASE("ui::Text and ui::RichText carry the new text properties", "[text][spec]")
{
    ui::Control<ui::Text> block;
    auto                  node = ui::build(
        ui::Text{.text                      = "x",
                 .selection_enabled         = true,
                 .text_alignment            = TextAlignment::Center,
                 .character_spacing         = 100,
                 .selection_highlight_color = Color::from_argb(0xFF008000),
                 .control                   = block});
    REQUIRE(block->text_alignment() == TextAlignment::Center);
    REQUIRE(block->character_spacing() == 100);
    REQUIRE(block->selection_enabled());
    REQUIRE(block->selection_highlight_color() == Color::from_argb(0xFF008000));

    ui::Control<ui::RichText> rich;
    auto                      r = ui::build(ui::RichText{.blocks            = {{.inlines = {{.text = "a"}}}},
                                                         .selection_enabled = false,
                                                         .text_alignment    = TextAlignment::Justify,
                                                         .control           = rich});
    REQUIRE(rich->plain_text() == "a");
    REQUIRE(rich->text_alignment() == TextAlignment::Justify);
    REQUIRE_FALSE(rich->selection_enabled());
}

TEST_CASE("TextTrimming cuts a TextControl to its box and reports it", "[text][textblock]")
{
    // Trimming, and whether the text is trimmed with its change notification.
    TextControl block{"Hello wonderful world"};
    block.set_trimming(TextTrimming::CharacterEllipsis);
    std::vector<bool> changes;
    block.on_text_trimmed_changed([&](bool trimmed) { changes.push_back(trimmed); });

    // Twelve characters' room: "Hello wonde" and the ellipsis.
    lay_out(block, Rect{0.0f, 0.0f, Advance * 12.0f, LineHeight});
    REQUIRE(block.is_text_trimmed());
    REQUIRE(changes == std::vector<bool>{true});
    TestRenderer r;
    block.render(r);
    REQUIRE(texts(r).size() == 1);
    REQUIRE(texts(r)[0]->text == "Hello wonde…");

    // WordEllipsis stops at the end of the last whole word.
    block.set_trimming(TextTrimming::WordEllipsis);
    lay_out(block, Rect{0.0f, 0.0f, Advance * 12.0f, LineHeight});
    TestRenderer word;
    block.render(word);
    REQUIRE(texts(word)[0]->text == "Hello…");

    lay_out(block, Rect{0.0f, 0.0f, Advance * 30.0f, LineHeight});
    REQUIRE_FALSE(block.is_text_trimmed());
    REQUIRE(changes == std::vector<bool>{true, false});
    TestRenderer whole;
    block.render(whole);
    REQUIRE(texts(whole)[0]->text == "Hello wonderful world");

    // Clip draws the whole text, clipped to the box.
    block.set_trimming(TextTrimming::Clip);
    lay_out(block, Rect{0.0f, 0.0f, Advance * 12.0f, LineHeight});
    REQUIRE(block.is_text_trimmed());
    TestRenderer clip;
    block.render(clip);
    REQUIRE(texts(clip)[0]->text == "Hello wonderful world");
}

TEST_CASE("A TextControl made selectable after its first layout selects through the window", "[text][textblock]")
{
    HeadlessUI            ui = create_headless();
    Property<bool>        selectable{false};
    ui::Control<ui::Text> block;
    ui.window().set_content(ui::Scroll{
        .content = ui::VerticalStack{.content = {ui::Text{.text              = "hello brave world",
                                                          .selection_enabled = bind(selectable),
                                                          .control           = block}}},
    });
    ui.layout({400, 300});

    selectable = true;
    ui.layout({400, 300});
    REQUIRE(block->focusable());

    const Rect   b = block->bounds();
    const float  y = b.y + b.height / 2;
    PointerEvent down{{b.x + 1 * Advance, y}, PointerButton::Left, PointerAction::Down};
    down.click_count = 1;
    ui::testing::seams(ui.window()).handle_pointer(down);
    ui::testing::seams(ui.window()).handle_pointer({{b.x + 5 * Advance, y}, PointerButton::None, PointerAction::Move});
    ui::testing::seams(ui.window()).handle_pointer({{b.x + 5 * Advance, y}, PointerButton::Left, PointerAction::Up});
    ui.layout({400, 300});

    REQUIRE(ui.window().focused() == block);
    REQUIRE(block->selected_text() == "ello");
    TestRenderer r;
    ui.window().render(r);
    REQUIRE(r.count_of<FillRoundedRect>() >= 1);
}

TEST_CASE("Text and RichText draw a theme-resource selection highlight in the theme's color", "[text][theme]")
{
    const Theme          light    = Theme::light();
    const Theme          dark     = Theme::fluent(Palette::fluent_dark());
    constexpr ThemeColor resource = [](const Theme& t) { return t.palette.text_fill_secondary; };
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

    ui::Control<ui::Text> block;
    auto                  text = ui::build(
        ui::Text{.text = "orange text", .selection_enabled = true, .selection_highlight_color = resource, .control = block});
    REQUIRE(block->selection_highlight_resource() == resource);
    REQUIRE_FALSE(block->selection_highlight_color().has_value());
    lay_out(*block, {0, 0, 400, 30});
    block->select(0, 6);
    ui::testing::set_focused(*block, true);
    TestRenderer block_light;
    block->render(block_light, light);
    CHECK(has_fill(block_light, resource(light)));
    TestRenderer block_dark;
    block->render(block_dark, dark);
    CHECK(has_fill(block_dark, resource(dark)));

    ui::Control<ui::RichText> rich;
    auto                      paragraphs = ui::build(ui::RichText{.blocks = {{.inlines = {{.text = "Lorem ipsum"}}}},
                                                                  .selection_highlight_color = resource,
                                                                  .control                   = rich});
    REQUIRE(rich->selection_highlight_resource() == resource);
    lay_out(*rich, {0, 0, 400, 100});
    rich->select(0, 5);
    ui::testing::set_focused(*rich, true);
    TestRenderer rich_light;
    rich->render(rich_light, light);
    CHECK(has_fill(rich_light, resource(light)));
    TestRenderer rich_dark;
    rich->render(rich_dark, dark);
    CHECK(has_fill(rich_dark, resource(dark)));

    rich->set_selection_highlight_color(colors::Black); // a fixed color replaces the resource
    CHECK(rich->selection_highlight_resource() == nullptr);
    CHECK(rich->selection_highlight_color() == colors::Black);
}

TEST_CASE("Automation sees each hyperlink inline as a Hyperlink that Invoke follows", "[text][textblock][automation]")
{
    int         clicks = 0;
    TextControl block;
    block.set_inlines({{.text = "see "}, {.text = "link", .on_click = [&] { ++clicks; }}, {.text = " here"}});
    lay_out(block, {10, 20, 400, 30});

    REQUIRE(describe_automation(block) ==
            "Text \"see link here\"\n"
            "  Hyperlink \"link\" [Invoke]\n");
    const std::vector<Element*> links = automation_children(block);
    REQUIRE(automation_parent(*links[0]) == &block);
    // Over the link's own text: four characters in from the left, past "see ".
    CHECK_THAT(links[0]->bounds().x, WithinAbs(10.0f + 4.0f * Advance, 0.5));
    CHECK_THAT(links[0]->bounds().width, WithinAbs(4.0f * Advance, 0.5));
    CHECK(links[0]->bounds().y >= 20.0f);

    REQUIRE(ui::automation_providers(*links[0]).invoke->invoke());
    CHECK(clicks == 1);

    block.set_inlines({{.text = "plain"}});
    lay_out(block, {10, 20, 400, 30});
    CHECK(automation_children(block).empty());
}
