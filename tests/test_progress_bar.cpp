// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/status/ProgressBar.hpp"
#include "winui/ui/status/ProgressRing.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE("A default-constructed ProgressBar is determinate at 0 over [0,100]", "[progress-bar]")
{
    ProgressBarControl bar;
    REQUIRE_THAT(bar.min(), WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(bar.max(), WithinAbs(100.0, 1e-6));
    REQUIRE_THAT(bar.value(), WithinAbs(0.0, 1e-6));
    REQUIRE_FALSE(bar.is_indeterminate());
}

TEST_CASE("Construction clamps an out-of-range initial value into [min,max]", "[progress-bar]")
{
    ProgressBarControl under{-5.0, 0.0, 10.0};
    REQUIRE_THAT(under.value(), WithinAbs(0.0, 1e-6));

    ProgressBarControl over{50.0, 0.0, 10.0};
    REQUIRE_THAT(over.value(), WithinAbs(10.0, 1e-6));
}

TEST_CASE("set_value clamps, no-ops on an actual non-change, and fires the callback otherwise", "[progress-bar]")
{
    ProgressBarControl bar{0.0, 0.0, 10.0};

    int    calls = 0;
    double last  = -1.0;
    bar.on_value_changed(
        [&](double v)
        {
            ++calls;
            last = v;
        });

    bar.set_value(0.0); // already there: no-op
    REQUIRE(calls == 0);

    bar.set_value(7.0);
    REQUIRE_THAT(bar.value(), WithinAbs(7.0, 1e-6));
    REQUIRE(calls == 1);
    REQUIRE_THAT(last, WithinAbs(7.0, 1e-6));

    bar.set_value(1000.0); // clamps to 10
    REQUIRE_THAT(bar.value(), WithinAbs(10.0, 1e-6));
    REQUIRE(calls == 2);
}

TEST_CASE("set_range re-clamps the current value, firing the callback only if that moves it", "[progress-bar]")
{
    ProgressBarControl bar{7.0, 0.0, 10.0};

    int calls = 0;
    bar.on_value_changed([&](double) { ++calls; });

    bar.set_range(0.0, 20.0); // 7 still valid: no reclamp needed
    REQUIRE(calls == 0);
    REQUIRE_THAT(bar.value(), WithinAbs(7.0, 1e-6));

    bar.set_range(0.0, 5.0); // clamps 7 -> 5
    REQUIRE(calls == 1);
    REQUIRE_THAT(bar.value(), WithinAbs(5.0, 1e-6));
}

TEST_CASE("Value and indeterminate toggling never change measured size", "[progress-bar][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ProgressBarControl bar{0.0, 0.0, 100.0};
    const Size         first = bar.measure({500.0f, Unbounded}, m, theme);
    REQUIRE_THAT(first.width, WithinAbs(200.0, 1e-3)); // nominal default length, unconstrained
    REQUIRE_THAT(first.height, WithinAbs(3.0, 1e-3));  // the minimum height

    bar.set_value(80.0);
    const Size second = bar.measure({500.0f, Unbounded}, m, theme);
    REQUIRE_THAT(first.width, WithinAbs(static_cast<double>(second.width), 1e-3));
    REQUIRE_THAT(first.height, WithinAbs(static_cast<double>(second.height), 1e-3));

    bar.set_indeterminate(true);
    const Size third = bar.measure({500.0f, Unbounded}, m, theme);
    REQUIRE_THAT(first.width, WithinAbs(static_cast<double>(third.width), 1e-3));
    REQUIRE_THAT(first.height, WithinAbs(static_cast<double>(third.height), 1e-3));
}

TEST_CASE("Determinate render draws the track always, plus an indicator only once value > min", "[progress-bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ProgressBarControl bar{0.0, 0.0, 100.0};
    const Size         desired = bar.measure({200.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    TestRenderer r0;
    bar.render(r0, theme);
    REQUIRE(r0.count_of<FillRoundedRect>() == 1); // track only, at value == min

    bar.set_value(50.0);
    TestRenderer r1;
    bar.render(r1, theme);
    REQUIRE(r1.count_of<FillRoundedRect>() == 2); // track + indicator
    const auto& track     = std::get<FillRoundedRect>(r1.commands[0]);
    const auto& indicator = std::get<FillRoundedRect>(r1.commands[1]);
    REQUIRE_THAT(track.bounds.width, WithinAbs(static_cast<double>(desired.width), 1e-3));
    REQUIRE_THAT(indicator.bounds.width, WithinAbs(static_cast<double>(desired.width) * 0.5, 1e-3));
    REQUIRE_THAT(indicator.bounds.x, WithinAbs(0.0, 1e-3)); // grows from the left edge

    bar.set_value(100.0);
    TestRenderer r2;
    bar.render(r2, theme);
    REQUIRE(r2.count_of<FillRoundedRect>() == 2);
    const auto& full = std::get<FillRoundedRect>(r2.commands[1]);
    REQUIRE_THAT(full.bounds.width, WithinAbs(static_cast<double>(desired.width), 1e-3));
}

TEST_CASE("Indeterminate render draws exactly two clipped sliding indicators, ignoring value", "[progress-bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ProgressBarControl bar{50.0, 0.0, 100.0}; // value is irrelevant once indeterminate
    bar.set_indeterminate(true);
    const Size desired = bar.measure({200.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    bar.render(r, theme);
    REQUIRE(r.count() == 4);
    REQUIRE(r.count_of<PushClip>() == 1);
    REQUIRE(r.count_of<PopClip>() == 1);
    REQUIRE(r.count_of<FillRoundedRect>() == 2);
    REQUIRE(std::holds_alternative<PushClip>(r.commands.front()));
    REQUIRE(std::holds_alternative<PopClip>(r.commands.back()));

    // At phase 0, indicator 1 starts fully off-screen to the left (-40% of the track width).
    const auto& indicator1 = std::get<FillRoundedRect>(r.commands[1]);
    REQUIRE_THAT(indicator1.bounds.x, WithinAbs(static_cast<double>(-desired.width * 0.4f), 1e-3));
    REQUIRE_THAT(indicator1.bounds.width, WithinAbs(static_cast<double>(desired.width) * 0.4, 1e-3));
}

TEST_CASE("on_tick advances the indeterminate sweep and wraps after a full 2s period; determinate ignores it",
          "[progress-bar][tick]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ProgressBarControl bar{0.0, 0.0, 100.0};
    bar.set_indeterminate(true);
    const Size desired = bar.measure({200.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    bar.tick(750ms); // halfway through indicator 1's 1.5s sweep segment
    TestRenderer mid;
    bar.render(mid, theme);
    const auto& mid1 = std::get<FillRoundedRect>(mid.commands[1]);
    // Halfway between -40% and +120% is +40% of the track width.
    REQUIRE_THAT(mid1.bounds.x, WithinAbs(static_cast<double>(desired.width) * 0.4, 1e-3));

    bar.tick(1250ms); // 750 + 1250 = 2000ms total: wraps the period back to exactly phase 0
    TestRenderer wrapped;
    bar.render(wrapped, theme);
    const auto& wrapped1 = std::get<FillRoundedRect>(wrapped.commands[1]);
    REQUIRE_THAT(wrapped1.bounds.x, WithinAbs(static_cast<double>(-desired.width * 0.4f), 1e-3));

    ProgressBarControl det{0.0, 0.0, 100.0};
    det.arrange({0, 0, 200.0f, 3.0f});
    ui::testing::clear_dirty(det);
    det.tick(500ms);
    REQUIRE_FALSE(det.is_dirty());
}

TEST_CASE("set_show_paused/set_show_error no-op on an actual non-change and mark dirty otherwise", "[progress-bar]")
{
    ProgressBarControl bar;
    ui::testing::clear_dirty(bar);

    bar.set_show_paused(false); // already false
    REQUIRE_FALSE(bar.is_dirty());
    bar.set_show_paused(true);
    REQUIRE(bar.is_dirty());
    REQUIRE(bar.show_paused());

    ui::testing::clear_dirty(bar);
    bar.set_show_error(false); // already false
    REQUIRE_FALSE(bar.is_dirty());
    bar.set_show_error(true);
    REQUIRE(bar.is_dirty());
    REQUIRE(bar.show_error());
}

TEST_CASE("Determinate render tints the indicator with the paused/error color, error outranking paused",
          "[progress-bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ProgressBarControl bar{50.0, 0.0, 100.0};
    const Size         desired = bar.measure({200.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    bar.set_show_paused(true);
    TestRenderer paused;
    bar.render(paused, theme);
    const auto& paused_indicator = std::get<FillRoundedRect>(paused.commands[1]);
    REQUIRE(paused_indicator.color == theme.palette.semantic_fill_caution);

    bar.set_show_error(true); // error outranks an already-set paused
    TestRenderer error;
    bar.render(error, theme);
    const auto& error_indicator = std::get<FillRoundedRect>(error.commands[1]);
    REQUIRE(error_indicator.color == theme.palette.semantic_fill_critical);
}

TEST_CASE("Indeterminate paused/error freezes into a single full-width tinted bar and halts the sweep clock",
          "[progress-bar][render][tick]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ProgressBarControl bar{0.0, 0.0, 100.0};
    bar.set_indeterminate(true);
    bar.set_show_error(true);
    const Size desired = bar.measure({200.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    bar.render(r, theme);
    REQUIRE(r.count() == 1); // one static bar, no clip/second rectangle
    const auto& bar_cmd = std::get<FillRoundedRect>(r.commands[0]);
    REQUIRE(bar_cmd.color == theme.palette.semantic_fill_critical);
    REQUIRE_THAT(bar_cmd.bounds.width, WithinAbs(static_cast<double>(desired.width), 1e-3));

    ui::testing::clear_dirty(bar);
    bar.tick(500ms);
    REQUIRE_FALSE(bar.is_dirty()); // the sweep phase clock doesn't advance while frozen
}

TEST_CASE("Turning indeterminate back on always restarts the sweep from phase 0", "[progress-bar][tick]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ProgressBarControl bar{0.0, 0.0, 100.0};
    bar.set_indeterminate(true);
    const Size desired = bar.measure({200.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    bar.tick(900ms);
    bar.set_indeterminate(false);
    bar.set_indeterminate(true);

    TestRenderer r;
    bar.render(r, theme);
    const auto& indicator1 = std::get<FillRoundedRect>(r.commands[1]);
    REQUIRE_THAT(indicator1.bounds.x, WithinAbs(static_cast<double>(-desired.width * 0.4f), 1e-3));
}

namespace
{
const Theme& aero()
{
    static const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    return theme;
}

std::vector<const FillRoundedRect*> fills_of(const TestRenderer& r, Color color)
{
    std::vector<const FillRoundedRect*> out;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&command); fill != nullptr && fill->color == color)
        {
            out.push_back(fill);
        }
    }
    return out;
}

// The glint: the one gradient fill whose stops peak at #60FFFFFF.
const FillRoundedRectGradient* glint_of(const TestRenderer& r, Color color = Color::from_argb(0x60FFFFFF))
{
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRectGradient>(&command);
            fill != nullptr && fill->brush.stops.size() == 4 && fill->brush.stops[1].color == color)
        {
            return fill;
        }
    }
    return nullptr;
}

ProgressBarControl laid_out_aero_bar(double value)
{
    ProgressBarControl         bar{value};
    const HeadlessTextMeasurer m;
    (void)bar.measure({202.0f, 100.0f}, m, aero());
    bar.arrange({0.0f, 0.0f, 202.0f, 15.0f});
    return bar;
}
} // namespace

TEST_CASE("Aero's progress bar is the era's 15 DIP glossy bar", "[progress-bar][layout][aero]")
{
    const HeadlessTextMeasurer m;
    ProgressBarControl         bar{50.0};
    CHECK(bar.measure({300.0f, 100.0f}, m, aero()).height == 15.0f);
    CHECK(bar.measure({300.0f, 100.0f}, m, Theme::light()).height == 3.0f);

    ProgressBarControl half = laid_out_aero_bar(50.0);
    TestRenderer       r;
    half.render(r, aero());
    // The indicator fills half the track inside the 1 DIP frame, in Aero's green under its wash.
    const auto indicator = fills_of(r, Color::from_argb(0xFF01D328));
    REQUIRE(indicator.size() == 1);
    CHECK(indicator[0]->bounds == Rect{1.0f, 1.0f, 100.0f, 13.0f});
    CHECK(fills_of(r, Color::from_argb(0x80B5FFA9)).size() == 1);
    // The frame closes it.
    REQUIRE(std::holds_alternative<DrawGeometry>(r.commands.back()));
    CHECK(std::get<DrawGeometry>(r.commands.back()).stroke.has_value());
}

TEST_CASE("Aero's glint crosses the indicator at 200 DIPs a second, then rests a second", "[progress-bar][tick][aero]")
{
    ProgressBarControl bar = laid_out_aero_bar(50.0); // 100 DIPs filled: 300 DIPs of pass, 1.5 s
    ui::testing::clear_dirty(bar);
    bar.tick(500ms);
    CHECK(bar.is_dirty());
    TestRenderer mid;
    bar.render(mid, aero());
    const FillRoundedRectGradient* glint = glint_of(mid);
    REQUIRE(glint != nullptr);
    CHECK_THAT(glint->bounds.x, WithinAbs(1.0f, 1e-3)); // a third of the way, from 100 DIPs out

    bar.tick(1600ms); // 2.1 s: resting until 2.5 s
    TestRenderer rest;
    bar.render(rest, aero());
    CHECK(glint_of(rest) == nullptr);

    // A paused bar takes the caution color, no wash and no glint, and stops asking for frames.
    bar.set_show_paused(true);
    ui::testing::clear_dirty(bar);
    bar.tick(100ms);
    CHECK_FALSE(bar.is_dirty());
    TestRenderer paused;
    bar.render(paused, aero());
    CHECK(fills_of(paused, aero().severity.caution.fill).size() == 1);
    CHECK(fills_of(paused, Color::from_argb(0x80B5FFA9)).empty());
}

TEST_CASE("An indeterminate Aero bar runs a green band over the empty track", "[progress-bar][render][aero]")
{
    ProgressBarControl bar = laid_out_aero_bar(0.0);
    bar.set_indeterminate(true);
    bar.tick(250ms);
    TestRenderer r;
    bar.render(r, aero());
    CHECK(fills_of(r, Color::from_argb(0xFF01D328)).empty());
    CHECK(glint_of(r) == nullptr);
    CHECK(glint_of(r, Color::from_argb(0xFF01D328)) != nullptr);
}

namespace
{
const Theme& classic_theme(ThemeVariant variant)
{
    static const Theme win9x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    static const Theme win3x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{});
    return variant == ThemeVariant::Windows3x ? win3x : win9x;
}

ProgressBarControl laid_out_classic_bar(ThemeVariant variant, double value)
{
    ProgressBarControl         bar{value};
    const HeadlessTextMeasurer m;
    (void)bar.measure({202.0f, 100.0f}, m, classic_theme(variant));
    bar.arrange({0.0f, 0.0f, 202.0f, 16.0f});
    return bar;
}

// The fills `size` big, left to right as drawn.
std::vector<Rect> fills_sized(const TestRenderer& r, Size size)
{
    std::vector<Rect> out;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&command);
            fill != nullptr && fill->bounds.width == size.width && fill->bounds.height == size.height)
        {
            out.push_back(fill->bounds);
        }
    }
    return out;
}
} // namespace

TEST_CASE("The Windows 9x progress bar is a sunken well of highlight chunks", "[progress-bar][render][classic]")
{
    const Theme&               theme = classic_theme(ThemeVariant::Windows9x);
    const HeadlessTextMeasurer m;
    ProgressBarControl         probe{50.0};
    CHECK(probe.measure({300.0f, 100.0f}, m, theme).height == 16.0f);

    // Inside the 1 DIP edge and a DIP of face, 198 x 12: chunks 8 wide on a 10 DIP pitch. Half
    // is 99 DIPs, and the started tenth chunk is drawn whole.
    ProgressBarControl bar = laid_out_classic_bar(ThemeVariant::Windows9x, 50.0);
    TestRenderer       r;
    bar.render(r, theme);
    const std::vector<Rect> chunks = fills_sized(r, {8.0f, 12.0f});
    REQUIRE(chunks.size() == 10);
    CHECK(chunks.front() == Rect{2.0f, 2.0f, 8.0f, 12.0f});
    CHECK(chunks.back() == Rect{92.0f, 2.0f, 8.0f, 12.0f});
    // The static edge: shadow along the top and left, highlight along the bottom and right.
    const std::vector<Rect> rows = fills_sized(r, {202.0f, 1.0f});
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].y == 0.0f);
    CHECK(rows[1].y == 15.0f);

    // Full, twenty chunks fill the well, the last against its right edge.
    bar.set_value(100.0);
    TestRenderer full;
    bar.render(full, theme);
    CHECK(fills_sized(full, {8.0f, 12.0f}).size() == 20);
    CHECK(fills_sized(full, {8.0f, 12.0f}).back().x == 192.0f);

    // Four DIPs narrower, the last chunk is cut at the well's right edge.
    bar.arrange({0.0f, 0.0f, 198.0f, 16.0f});
    TestRenderer cut;
    bar.render(cut, theme);
    CHECK(fills_sized(cut, {8.0f, 12.0f}).size() == 19);
    CHECK(fills_sized(cut, {4.0f, 12.0f}).size() == 1);
}

TEST_CASE("An indeterminate Windows 9x bar sweeps five chunks on the chunk grid", "[progress-bar][tick][classic]")
{
    const Theme&       theme = classic_theme(ThemeVariant::Windows9x);
    ProgressBarControl bar   = laid_out_classic_bar(ThemeVariant::Windows9x, 0.0);
    bar.set_indeterminate(true);
    ui::testing::clear_dirty(bar);
    bar.tick(1000ms); // halfway through 198 + 50 DIPs of travel: the band ends at 124, snapped to 120
    CHECK(bar.is_dirty());
    TestRenderer r;
    bar.render(r, theme);
    const std::vector<Rect> chunks = fills_sized(r, {8.0f, 12.0f});
    REQUIRE(chunks.size() == 5);
    CHECK(chunks.front().x == 72.0f);
    CHECK(chunks.back().x == 112.0f);
}

TEST_CASE("The Windows 3.x progress bar is one block inside a 1 DIP frame", "[progress-bar][render][classic]")
{
    const Theme&       theme = classic_theme(ThemeVariant::Windows3x);
    ProgressBarControl bar   = laid_out_classic_bar(ThemeVariant::Windows3x, 50.0);
    TestRenderer       r;
    bar.render(r, theme);
    CHECK(fills_sized(r, {100.0f, 14.0f}).size() == 1);
    CHECK(fills_sized(r, {100.0f, 14.0f}).front() == Rect{1.0f, 1.0f, 100.0f, 14.0f});
    CHECK(fills_sized(r, {8.0f, 14.0f}).empty());

    // Paused, the block takes the caution color.
    bar.set_show_paused(true);
    TestRenderer paused;
    bar.render(paused, theme);
    const auto block = fills_of(paused, theme.colors().caution);
    REQUIRE(block.size() == 1);
    CHECK(block[0]->bounds == Rect{1.0f, 1.0f, 100.0f, 14.0f});
}

TEST_CASE("The early-Windows progress bar is an ink block inside a field's frame", "[progress-bar][render][classic]")
{
    const Theme        theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x}, OsThemeSnapshot{});
    ProgressBarControl bar{50.0};
    const HeadlessTextMeasurer m;
    CHECK(bar.measure({202.0f, 100.0f}, m, theme).height == 16.0f);
    bar.arrange({0.0f, 0.0f, 202.0f, 16.0f});
    TestRenderer r;
    bar.render(r, theme);
    // The window-colored well, its black rule, and half of it in black.
    CHECK(fills_of(r, colors::White).size() == 1);
    const auto block = fills_sized(r, {100.0f, 14.0f});
    REQUIRE(block.size() == 1);
    CHECK(block.front() == Rect{1.0f, 1.0f, 100.0f, 14.0f});
    CHECK(fills_of(r, colors::Black).size() == 5); // four edges and the block
}

TEST_CASE("A progress spec without a value is indeterminate, and a bound value can switch it",
          "[progress_bar][progress_ring][spec]")
{
    Property<std::optional<double>> progress{};
    ui::Control<ui::ProgressBar>    bar;
    ui::Control<ui::ProgressRing>   ring;
    std::unique_ptr<Element>        bar_tree = ui::build(ui::ProgressBar{.value = ui::bind(progress), .control = bar});
    std::unique_ptr<Element> ring_tree = ui::build(ui::ProgressRing{.value = ui::bind(progress), .control = ring});
    CHECK(bar->is_indeterminate());
    CHECK(ring->is_indeterminate());

    progress = 40.0;
    CHECK_FALSE(bar->is_indeterminate());
    CHECK_FALSE(ring->is_indeterminate());
    CHECK(bar->value() == 40.0);
    CHECK(ring->value() == 40.0);

    progress = std::nullopt;
    CHECK(bar->is_indeterminate());
    CHECK(ring->is_indeterminate());

    Property<double>             plain{25.0};
    ui::Control<ui::ProgressBar> fixed;
    std::unique_ptr<Element>     fixed_tree = ui::build(ui::ProgressBar{.value = plain, .control = fixed});
    CHECK_FALSE(fixed->is_indeterminate());
    CHECK(fixed->value() == 25.0);
}
