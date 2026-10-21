// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/render/Describe.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
// 10px text: HeadlessTextMeasurer advances 5.5px per character and stacks 13.5px lines.
constexpr float Advance = 5.5f;
constexpr float Line    = 13.5f;

TextStyle ten_px(TextWrapping wrapping)
{
    TextStyle style{10.0f, TextAlign::Leading, TextAlign::Leading, "Segoe UI"};
    style.wrapping = wrapping;
    return style;
}
} // namespace

TEST_CASE("The headless measurer breaks between words at the offered width", "[text][wrap]")
{
    const HeadlessTextMeasurer m;

    // 50px holds nine characters: "aaaa bbbb" fits, " cccc" goes to a second line.
    const Size wrapped = m.measure_wrapped("aaaa bbbb cccc", ten_px(TextWrapping::Wrap), 50.0f);
    CHECK_THAT(wrapped.width, WithinAbs(9.0f * Advance, 1e-3));
    CHECK_THAT(wrapped.height, WithinAbs(2.0f * Line, 1e-3));

    // Text that fits, a NoWrap style or an unbounded width measure exactly as measure() does.
    const Size one_line = m.measure("aaaa bbbb cccc", ten_px(TextWrapping::Wrap));
    CHECK(m.measure_wrapped("aaaa bbbb cccc", ten_px(TextWrapping::Wrap), 500.0f) == one_line);
    CHECK(m.measure_wrapped("aaaa bbbb cccc", ten_px(TextWrapping::NoWrap), 50.0f) == one_line);
    CHECK(m.measure_wrapped("aaaa bbbb cccc", ten_px(TextWrapping::Wrap), 0.0f) == one_line);
}

TEST_CASE("Wrap splits a word longer than the line; WrapWholeWords lets it overflow", "[text][wrap]")
{
    const HeadlessTextMeasurer m;
    const std::string          word = "abcdefghijklmnopqrst"; // 20 characters, 110px

    const Size split = m.measure_wrapped(word, ten_px(TextWrapping::Wrap), 50.0f);
    CHECK_THAT(split.width, WithinAbs(9.0f * Advance, 1e-3));
    CHECK_THAT(split.height, WithinAbs(3.0f * Line, 1e-3)); // 9 + 9 + 2

    const Size whole = m.measure_wrapped(word, ten_px(TextWrapping::WrapWholeWords), 50.0f);
    CHECK_THAT(whole.width, WithinAbs(20.0f * Advance, 1e-3));
    CHECK_THAT(whole.height, WithinAbs(Line, 1e-3));

    // A line break always starts a new line, even when everything would fit on one.
    const Size lines = m.measure_wrapped("ab\ncd", ten_px(TextWrapping::Wrap), 500.0f);
    CHECK_THAT(lines.width, WithinAbs(2.0f * Advance, 1e-3));
    CHECK_THAT(lines.height, WithinAbs(2.0f * Line, 1e-3));
}

TEST_CASE("A wrapping TextControl grows taller at the width its parent offers", "[text][wrap][layout]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();

    auto         stack = std::make_unique<StackControl>(Orientation::Vertical);
    TextControl* label = stack->add(std::make_unique<TextControl>("aaaa bbbb cccc"));
    label->set_text_style(ten_px(TextWrapping::NoWrap));
    label->set_wrapping(TextWrapping::Wrap);
    CHECK(label->wrapping() == TextWrapping::Wrap);

    (void)stack->measure({50.0f, 300.0f}, measurer, theme);
    stack->arrange({0.0f, 0.0f, 50.0f, 300.0f});
    CHECK_THAT(label->bounds().height, WithinAbs(2.0f * Line, 1e-3));

    // The run carries the wrapping to the renderer, and the semantic dump says so.
    TestRenderer renderer;
    label->render(renderer, theme);
    REQUIRE(renderer.count_of<DrawTextRun>() == 1);
    CHECK(std::get<DrawTextRun>(renderer.commands[0]).style.wrapping == TextWrapping::Wrap);
    CHECK(describe(renderer.commands).find(" wrap=wrap ") != std::string::npos);

    label->set_wrapping(TextWrapping::NoWrap);
    (void)stack->measure({50.0f, 300.0f}, measurer, theme);
    stack->arrange({0.0f, 0.0f, 50.0f, 300.0f});
    CHECK_THAT(label->bounds().height, WithinAbs(Line, 1e-3));
}

TEST_CASE("A wrapping TextControl stays on one line when its width is unbounded", "[text][wrap][layout]")
{
    HeadlessTextMeasurer measurer;
    TextControl          label{"aaaa bbbb cccc"};
    label.set_text_style(ten_px(TextWrapping::Wrap));
    const Size desired = label.measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    CHECK_THAT(desired.width, WithinAbs(14.0f * Advance, 1e-3));
    CHECK_THAT(desired.height, WithinAbs(Line, 1e-3));
}

TEST_CASE("A Button wraps a TextControl inside it to the width it is given", "[text][wrap][layout]")
{
    // "Wrapping Buttons with large content": wrapping text
    // as a Button's content.
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();
    ButtonControl        button;
    TextControl*         label = button.set_content(std::make_unique<TextControl>("aaaa bbbb cccc"));
    label->set_text_style(ten_px(TextWrapping::NoWrap));
    label->set_wrapping(TextWrapping::Wrap);

    const float padding = theme.padding.horizontal();
    const Size  narrow  = button.measure({50.0f + padding, Unbounded}, measurer, theme);
    const Size  wide    = button.measure({500.0f, Unbounded}, measurer, theme);
    CHECK(narrow.height > wide.height);
}
