// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/status/InfoBadge.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <optional>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE("A default-constructed InfoBadge has no value and shows a dot", "[info-badge]")
{
    InfoBadgeControl badge;
    REQUIRE_FALSE(badge.value().has_value());
}

TEST_CASE("Constructing with a value stores it", "[info-badge]")
{
    InfoBadgeControl badge{7};
    REQUIRE(badge.value() == 7);
}

TEST_CASE("Construction and set_value clamp a negative number to 0; nullopt clears", "[info-badge]")
{
    InfoBadgeControl badge{-99};
    REQUIRE(badge.value() == 0);

    badge.set_value(-5);
    REQUIRE(badge.value() == 0);

    badge.set_value(std::nullopt);
    REQUIRE_FALSE(badge.value().has_value());
}

TEST_CASE("set_value no-ops on an actual non-change", "[info-badge]")
{
    InfoBadgeControl badge{3};
    ui::testing::clear_dirty(badge);
    badge.set_value(3);
    REQUIRE_FALSE(badge.is_dirty());

    badge.set_value(4);
    REQUIRE(badge.is_dirty());
    REQUIRE(badge.value() == 4);
}

TEST_CASE("Dot mode measures to the minimum 4x4 pill", "[info-badge][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBadgeControl badge;
    const Size       desired = badge.measure({100.0f, Unbounded}, m, theme);
    REQUIRE_THAT(desired.width, WithinAbs(4.0, 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(4.0, 1e-3));
}

TEST_CASE("Value mode grows with digit count and is at least as tall as Dot mode", "[info-badge][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBadgeControl one{1};
    const Size       one_size = one.measure({200.0f, Unbounded}, m, theme);

    InfoBadgeControl many{123456};
    const Size       many_size = many.measure({200.0f, Unbounded}, m, theme);

    REQUIRE(many_size.width > one_size.width);
    REQUIRE(one_size.height >= 4.0f);
    REQUIRE(one_size.height <= 16.0f); // the badge's maximum height
}

TEST_CASE("Dot mode renders exactly one filled pill and no text", "[info-badge][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBadgeControl badge;
    const Size       desired = badge.measure({100.0f, Unbounded}, m, theme);
    badge.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    badge.render(r, theme);
    REQUIRE(r.count() == 1);
    REQUIRE(r.count_of<FillRoundedRect>() == 1);
    const auto& fill = std::get<FillRoundedRect>(r.commands[0]);
    REQUIRE_THAT(fill.radius, WithinAbs(static_cast<double>(fill.bounds.height) * 0.5, 1e-3));
}

TEST_CASE("Value mode renders a filled pill plus the value's own text", "[info-badge][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBadgeControl badge{42};
    const Size       desired = badge.measure({100.0f, Unbounded}, m, theme);
    badge.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    badge.render(r, theme);
    REQUIRE(r.count() == 2);
    REQUIRE(r.count_of<FillRoundedRect>() == 1);
    REQUIRE(r.count_of<DrawTextRun>() == 1);
    const auto& text = std::get<DrawTextRun>(r.commands[1]);
    REQUIRE(text.text == "42");
}

TEST_CASE("A one-digit value is a circle, never narrower than tall", "[info-badge][layout]")
{
    HeadlessTextMeasurer m;
    InfoBadgeControl     badge{5};
    const Size           desired = badge.measure({100.0f, Unbounded}, m, Theme::default_light());
    REQUIRE_THAT(desired.width, WithinAbs(desired.height, 1e-3)); // never narrower than tall
}

TEST_CASE("An icon badge is 16x16 and draws its glyph in the centered 8px ScaleToFit", "[info-badge][layout][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBadgeControl badge;
    badge.set_icon(glyphs::Cancel);
    const Size desired = badge.measure({100.0f, Unbounded}, m, theme);
    REQUIRE_THAT(desired.width, WithinAbs(16.0, 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(16.0, 1e-3));
    badge.arrange({10.0f, 20.0f, desired.width, desired.height});

    TestRenderer r;
    badge.render(r, theme);
    REQUIRE(r.count_of<FillRoundedRect>() == 1);
    REQUIRE(r.count_of<DrawTextRun>() == 1);
    const auto& glyph = std::get<DrawTextRun>(r.commands[1]);
    REQUIRE(glyph.text == encode_utf8(glyphs::Cancel.codepoint));
    REQUIRE_THAT(glyph.bounds.x, WithinAbs(14.0, 1e-3));
    REQUIRE_THAT(glyph.bounds.y, WithinAbs(24.0, 1e-3));
    REQUIRE_THAT(glyph.bounds.width, WithinAbs(8.0, 1e-3));
    REQUIRE_THAT(glyph.style.font_size, WithinAbs(8.0, 1e-3));
}

TEST_CASE("A value wins over an icon", "[info-badge][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBadgeControl badge{3};
    badge.set_icon(glyphs::Cancel);
    const Size desired = badge.measure({100.0f, Unbounded}, m, theme);
    badge.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    badge.render(r, theme);
    REQUIRE(std::get<DrawTextRun>(r.commands[1]).text == "3");
}

TEST_CASE("Severity picks the style family's background, and a local background beats it", "[info-badge][render]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    const auto fill_of = [&](InfoBadgeControl& badge)
    {
        const Size desired = badge.measure({100.0f, Unbounded}, m, theme);
        badge.arrange({0, 0, desired.width, desired.height});
        TestRenderer r;
        badge.render(r, theme);
        return std::get<FillRoundedRect>(r.commands[0]).color;
    };

    InfoBadgeControl badge;
    REQUIRE(fill_of(badge) == theme.accent_button_normal.background); // the default badge
    badge.set_severity(InfoBadgeSeverity::Attention);
    REQUIRE(fill_of(badge) == theme.severity.informational.fill);
    badge.set_severity(InfoBadgeSeverity::Informational);
    REQUIRE(fill_of(badge) == theme.severity.neutral);
    badge.set_severity(InfoBadgeSeverity::Success);
    REQUIRE(fill_of(badge) == theme.severity.success.fill);
    badge.set_severity(InfoBadgeSeverity::Caution);
    REQUIRE(fill_of(badge) == theme.severity.caution.fill);
    badge.set_severity(InfoBadgeSeverity::Critical);
    REQUIRE(fill_of(badge) == theme.severity.critical.fill);

    badge.set_background(Color::from_argb(0xFFC42B1C));
    REQUIRE(fill_of(badge) == Color::from_argb(0xFFC42B1C));
    badge.set_background(std::nullopt);
    REQUIRE(fill_of(badge) == theme.severity.critical.fill);
}

TEST_CASE("Fluent's Informational badge is the solid neutral fill in both modes", "[info-badge][theme]")
{
    REQUIRE(Theme::light().severity.neutral == Color::from_argb(0xFF8A8A8A));
    REQUIRE(Theme::fluent(Palette::fluent_dark()).severity.neutral == Color::from_argb(0xFF9D9D9D));
}

TEST_CASE("severity_glyph returns each severity's icon", "[info-badge]")
{
    REQUIRE(InfoBadgeControl::severity_glyph(InfoBadgeSeverity::Attention).codepoint == 0xEA38);
    REQUIRE(InfoBadgeControl::severity_glyph(InfoBadgeSeverity::Informational).codepoint == 0xF13F);
    REQUIRE(InfoBadgeControl::severity_glyph(InfoBadgeSeverity::Success).codepoint == 0xE10B);  // Symbol.Accept
    REQUIRE(InfoBadgeControl::severity_glyph(InfoBadgeSeverity::Caution).codepoint == 0xE171);  // Symbol.Important
    REQUIRE(InfoBadgeControl::severity_glyph(InfoBadgeSeverity::Critical).codepoint == 0xE10A); // Symbol.Cancel
}

TEST_CASE("A theme-resource background fills the badge and follows the theme", "[info-badge][render][theme]")
{
    HeadlessTextMeasurer m;
    const Theme          light    = Theme::light();
    const Theme          dark     = Theme::fluent(Palette::fluent_dark());
    constexpr ThemeColor resource = [](const Theme& t) { return t.palette.text_fill_secondary; };
    REQUIRE_FALSE(resource(light) == resource(dark));

    ui::Control<ui::InfoBadge> badge;
    auto                       built = ui::build(ui::InfoBadge{.value = 3, .background = resource, .control = badge});
    REQUIRE(badge->background_resource() == resource);
    REQUIRE_FALSE(badge->background().has_value());

    const auto fill_of = [&](const Theme& theme)
    {
        const Size desired = badge->measure({100.0f, Unbounded}, m, theme);
        badge->arrange({0, 0, desired.width, desired.height});
        TestRenderer r;
        badge->render(r, theme);
        return std::get<FillRoundedRect>(r.commands[0]).color;
    };
    CHECK(fill_of(light) == resource(light));
    CHECK(fill_of(dark) == resource(dark));

    badge->set_background(Color::from_argb(0xFFC42B1C));
    CHECK(badge->background_resource() == nullptr);
    CHECK(fill_of(dark) == Color::from_argb(0xFFC42B1C));
}
