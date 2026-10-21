// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/containers/Border.hpp"
#include "winui/ui/containers/GroupBox.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace ui;
using namespace ui::testing;

namespace
{
Theme era(ThemeVariant variant)
{
    return resolve_theme(ThemeRequest{.variant = variant}, OsThemeSnapshot{});
}

// A group box holding a 100 x 40 block, laid out at the origin at its desired size.
struct Laid
{
    std::unique_ptr<GroupBoxControl> box;
    BorderControl*                   block = nullptr;
    Size                             desired{};
};

Laid lay_out(const Theme& theme, std::string header)
{
    Laid l;
    l.box   = std::make_unique<GroupBoxControl>(std::move(header));
    l.block = l.box->set_child(std::make_unique<BorderControl>());
    l.block->set_width(100.0f);
    l.block->set_height(40.0f);
    HeadlessTextMeasurer m;
    l.desired = l.box->measure({400.0f, Unbounded}, m, theme);
    l.box->arrange({0.0f, 0.0f, l.desired.width, l.desired.height});
    return l;
}

float line_height(const TextStyle& style)
{
    HeadlessTextMeasurer m;
    return m.measure("Header", style).height;
}

const DrawTextRun* header_run(const TestRenderer& r)
{
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* run = std::get_if<DrawTextRun>(&command); run != nullptr && run->text == "Header")
        {
            return run;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("Under Fluent the header sits above a card that pads the content by 16", "[group-box][layout]")
{
    const Theme     theme  = era(ThemeVariant::Fluent2);
    const TextStyle strong = theme.type.for_role(TextRole::BodyStrong);
    const float     h      = line_height(strong);
    Laid            l      = lay_out(theme, "Header");

    CHECK(l.desired.width == 132.0f);
    CHECK(l.desired.height == h + 10.0f + 16.0f + 40.0f + 16.0f);
    CHECK(l.block->bounds().x == 16.0f);
    CHECK(l.block->bounds().y == h + 10.0f + 16.0f);

    TestRenderer r;
    l.box->render(r, theme);
    const DrawTextRun* run = header_run(r);
    REQUIRE(run != nullptr);
    CHECK(run->color == theme.colors().text);
    CHECK(run->bounds.y == 0.0f);
    bool card = false;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&command);
            fill != nullptr && fill->color == theme.palette.card_background_default)
        {
            card = fill->bounds.y > h && fill->radius == 7.0f; // the overlay corner radius inside the stroke
        }
    }
    CHECK(card);
    CHECK(r.count_of<PushClip>() == 0);
}

TEST_CASE("Under Fluent a group box without a header is the card alone", "[group-box][layout]")
{
    const Theme theme = era(ThemeVariant::Fluent2);
    Laid        l     = lay_out(theme, "");
    CHECK(l.desired.height == 72.0f);
    CHECK(l.block->bounds().y == 16.0f);
}

TEST_CASE("Windows 9x etches the frame and breaks it behind the header", "[group-box][render][retro]")
{
    const Theme theme = era(ThemeVariant::Windows9x);
    const float h     = line_height(theme.text_style);
    Laid        l     = lay_out(theme, "Header");
    CHECK(l.block->bounds().x == 7.0f);
    CHECK(l.block->bounds().y == std::max(18.0f, h + 2.0f));

    TestRenderer r;
    l.box->render(r, theme);
    // Three clipped passes (left of the gap, right of it, below it), each a light and a dark ring.
    CHECK(r.count_of<PushClip>() == 3);
    CHECK(r.count_of<StrokeRoundedRect>() == 6);
    const DrawTextRun* run = header_run(r);
    REQUIRE(run != nullptr);
    CHECK(run->bounds.x == 7.0f);
    CHECK(run->color == theme.colors().text);
}

TEST_CASE("Luna's header is blue in a single-line frame", "[group-box][render][luna]")
{
    const Theme theme = era(ThemeVariant::Luna);
    Laid        l     = lay_out(theme, "Header");
    CHECK(l.block->bounds().x == 9.0f);

    TestRenderer r;
    l.box->render(r, theme);
    CHECK(r.count_of<StrokeRoundedRect>() == 3);
    const DrawTextRun* run = header_run(r);
    REQUIRE(run != nullptr);
    CHECK(run->color == Color::from_argb(0xFF0046D5));
    CHECK(run->bounds.x == 12.0f);
}

TEST_CASE("An era group box without a header draws its frame whole from the top", "[group-box][render][aero]")
{
    const Theme  theme = era(ThemeVariant::Aero);
    Laid         l     = lay_out(theme, "");
    TestRenderer r;
    l.box->render(r, theme);
    CHECK(r.count_of<PushClip>() == 0);
    CHECK(r.count_of<StrokeRoundedRect>() == 1);
    CHECK(r.count_of<DrawTextRun>() == 0);
    CHECK(l.block->bounds().y == 11.0f);
}

TEST_CASE("A narrow group box still measures wide enough for its header", "[group-box][layout]")
{
    const Theme          theme = era(ThemeVariant::Windows3x);
    GroupBoxControl      box{"A rather long group box header"};
    HeadlessTextMeasurer m;
    const float          w = m.measure(box.header(), theme.text_style).width;
    CHECK(box.measure({400.0f, Unbounded}, m, theme).width == w + 7.0f + 1.0f + 1.0f);
}

TEST_CASE("The GroupBox spec binds its header, which names the group", "[group-box][spec]")
{
    Property<std::string>    header{"Appearance"};
    ui::Control<GroupBox>    handle;
    std::unique_ptr<Element> built = GroupBox{.header = bind(header), .control = handle}.build();
    REQUIRE(handle != nullptr);
    CHECK(handle->header() == "Appearance");
    CHECK(handle->automation_name_fallback() == "Appearance");
    header = "Behavior";
    CHECK(handle->header() == "Behavior");
}
