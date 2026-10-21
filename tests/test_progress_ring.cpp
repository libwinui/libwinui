// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/status/ProgressRing.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <numbers>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace std::chrono_literals;

namespace
{
// The one arc a render drew, as a stroked path.
const ui::DrawGeometry& only_arc(const ui::testing::TestRenderer& renderer)
{
    REQUIRE(renderer.count_of<ui::DrawGeometry>() == 1);
    for (const auto& command : renderer.commands)
    {
        if (const auto* arc = std::get_if<ui::DrawGeometry>(&command))
        {
            return *arc;
        }
    }
    return std::get<ui::DrawGeometry>(renderer.commands.front());
}

ui::Color arc_color(const ui::DrawGeometry& arc)
{
    return std::get<ui::Color>(arc.stroke.value());
}
} // namespace

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE("A default-constructed ProgressRing is active, indeterminate, and at 0 over [0,100]", "[progress-ring]")
{
    ProgressRingControl ring;
    REQUIRE_THAT(ring.min(), WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(ring.max(), WithinAbs(100.0, 1e-6));
    REQUIRE_THAT(ring.value(), WithinAbs(0.0, 1e-6));
    REQUIRE(ring.is_indeterminate());
    REQUIRE(ring.is_active());
}

TEST_CASE("Construction clamps an out-of-range initial value into [min,max]", "[progress-ring]")
{
    ProgressRingControl under{-5.0, 0.0, 10.0};
    REQUIRE_THAT(under.value(), WithinAbs(0.0, 1e-6));

    ProgressRingControl over{50.0, 0.0, 10.0};
    REQUIRE_THAT(over.value(), WithinAbs(10.0, 1e-6));
}

TEST_CASE("set_value clamps, no-ops on an actual non-change, and fires the callback otherwise", "[progress-ring]")
{
    ProgressRingControl ring{0.0, 0.0, 10.0};

    int    calls = 0;
    double last  = -1.0;
    ring.on_value_changed(
        [&](double v)
        {
            ++calls;
            last = v;
        });

    ring.set_value(0.0); // already there: no-op
    REQUIRE(calls == 0);

    ring.set_value(7.0);
    REQUIRE_THAT(ring.value(), WithinAbs(7.0, 1e-6));
    REQUIRE(calls == 1);
    REQUIRE_THAT(last, WithinAbs(7.0, 1e-6));

    ring.set_value(1000.0); // clamps to 10
    REQUIRE_THAT(ring.value(), WithinAbs(10.0, 1e-6));
    REQUIRE(calls == 2);
}

TEST_CASE("set_range re-clamps the current value, firing the callback only if that moves it", "[progress-ring]")
{
    ProgressRingControl ring{7.0, 0.0, 10.0};

    int calls = 0;
    ring.on_value_changed([&](double) { ++calls; });

    ring.set_range(0.0, 20.0); // 7 still valid: no reclamp needed
    REQUIRE(calls == 0);

    ring.set_range(0.0, 5.0); // clamps 7 -> 5
    REQUIRE(calls == 1);
    REQUIRE_THAT(ring.value(), WithinAbs(5.0, 1e-6));
}

TEST_CASE("measure_override defaults to a fixed 32x32 and shrinks to fit a tighter available size",
          "[progress-ring][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ProgressRingControl ring;
    const Size          unconstrained = ring.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE_THAT(unconstrained.width, WithinAbs(32.0, 1e-3));
    REQUIRE_THAT(unconstrained.height, WithinAbs(32.0, 1e-3));

    const Size constrained = ring.measure({20.0f, 20.0f}, m, theme);
    REQUIRE_THAT(constrained.width, WithinAbs(20.0, 1e-3));
    REQUIRE_THAT(constrained.height, WithinAbs(20.0, 1e-3));

    // A minimum width and height of 16 (the ProgressRing style) floors even a tighter
    // available size.
    const Size floored = ring.measure({5.0f, 5.0f}, m, theme);
    REQUIRE_THAT(floored.width, WithinAbs(16.0, 1e-3));
    REQUIRE_THAT(floored.height, WithinAbs(16.0, 1e-3));

    ring.set_value(50.0);
    ring.set_indeterminate(false);
    const Size after = ring.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE_THAT(after.width, WithinAbs(32.0, 1e-3));
    REQUIRE_THAT(after.height, WithinAbs(32.0, 1e-3));
}

TEST_CASE("An inactive ring draws nothing at all, in either mode", "[progress-ring][render]")
{
    ProgressRingControl ring{50.0, 0.0, 100.0};
    ring.set_indeterminate(false);
    ring.set_active(false);
    ring.arrange({0, 0, 32.0f, 32.0f});

    TestRenderer r;
    ring.render(r, Theme::light());
    REQUIRE(r.count() == 0);
}

TEST_CASE("Determinate render draws no arc at value == min, and a full-circle arc at value == max",
          "[progress-ring][render]")
{
    Theme theme = Theme::light();

    ProgressRingControl zero{0.0, 0.0, 100.0};
    zero.set_indeterminate(false);
    zero.arrange({0, 0, 32.0f, 32.0f});
    TestRenderer r0;
    zero.render(r0, theme);
    REQUIRE(r0.count() == 0);

    ProgressRingControl full{100.0, 0.0, 100.0};
    full.set_indeterminate(false);
    full.arrange({0, 0, 32.0f, 32.0f});
    TestRenderer r1;
    full.render(r1, theme);
    const DrawGeometry& arc = only_arc(r1);
    REQUIRE(arc.geometry.figures.size() == 1);
    const PathFigure& figure = arc.geometry.figures.front();
    CHECK(figure.closed); // a whole 2*pi sweep, with no ends to cap
    CHECK_FALSE(figure.filled);
    CHECK_FALSE(arc.fill.has_value());
    CHECK(arc.stroke_style.thickness == 4.0f);

    // The arc starts at 12 o'clock: directly above the ring's center, at
    // radius = diameter/2 - strokeWidth/2 = 16 - 2 = 14.
    REQUIRE_THAT(figure.start.x, WithinAbs(16.0, 1e-2));
    REQUIRE_THAT(figure.start.y, WithinAbs(2.0, 1e-2));
}

TEST_CASE("The determinate arc is one smooth path with round ends", "[progress-ring][render][regression]")
{
    ProgressRingControl ring{30.0, 0.0, 100.0};
    ring.set_indeterminate(false);
    ring.arrange({0, 0, 32.0f, 32.0f});
    TestRenderer r;
    ring.render(r, Theme::light());

    const DrawGeometry& arc = only_arc(r);
    CHECK(r.count_of<StrokeLine>() == 0);
    CHECK(arc.stroke_style.start_cap == PenLineCap::Round);
    CHECK(arc.stroke_style.end_cap == PenLineCap::Round);
    const PathFigure& figure = arc.geometry.figures.at(0);
    CHECK_FALSE(figure.closed);

    // Every Bezier segment, sampled along its length, stays on the circle of radius 14.
    const Point center{16.0f, 16.0f};
    Point       from = figure.start;
    for (const PathSegment& segment : figure.segments)
    {
        REQUIRE(segment.kind == PathSegmentKind::Cubic);
        for (float t = 0.0f; t <= 1.0f; t += 0.125f)
        {
            const float u = 1.0f - t;
            const float x = u * u * u * from.x + 3 * u * u * t * segment.p1.x + 3 * u * t * t * segment.p2.x +
                            t * t * t * segment.p3.x;
            const float y = u * u * u * from.y + 3 * u * u * t * segment.p1.y + 3 * u * t * t * segment.p2.y +
                            t * t * t * segment.p3.y;
            CHECK_THAT(std::hypot(x - center.x, y - center.y), WithinAbs(14.0, 0.01));
        }
        from = segment.p3;
    }

    // 30% of a turn clockwise from 12 o'clock.
    const float end = -std::numbers::pi_v<float> * 0.5f + 2.0f * std::numbers::pi_v<float> * 0.3f;
    CHECK_THAT(from.x, WithinAbs(16.0 + 14.0 * std::cos(end), 1e-3));
    CHECK_THAT(from.y, WithinAbs(16.0 + 14.0 * std::sin(end), 1e-3));
}

TEST_CASE("Determinate render at half value ends the arc at 6 o'clock", "[progress-ring][render]")
{
    Theme theme = Theme::light();

    ProgressRingControl half{50.0, 0.0, 100.0};
    half.set_indeterminate(false);
    half.arrange({0, 0, 32.0f, 32.0f});

    TestRenderer r;
    half.render(r, theme);
    const PathFigure& figure = only_arc(r).geometry.figures.at(0);
    REQUIRE_FALSE(figure.segments.empty());
    const Point end = figure.segments.back().end();
    CHECK_THAT(end.x, WithinAbs(16.0, 1e-3));
    CHECK_THAT(end.y, WithinAbs(30.0, 1e-3));
}

TEST_CASE("Indeterminate render always draws the same fixed-length sweep, ignoring value", "[progress-ring][render]")
{
    Theme theme = Theme::light();

    ProgressRingControl ring{99.0, 0.0, 100.0}; // value is irrelevant once indeterminate
    ring.arrange({0, 0, 32.0f, 32.0f});

    TestRenderer r;
    ring.render(r, theme);
    const PathFigure& figure = only_arc(r).geometry.figures.at(0);
    REQUIRE_FALSE(figure.segments.empty());
    const Point end = figure.segments.back().end(); // three quarters on from 12 o'clock: 9 o'clock
    CHECK_THAT(end.x, WithinAbs(2.0, 1e-3));
    CHECK_THAT(end.y, WithinAbs(16.0, 1e-3));
}

TEST_CASE("on_tick rotates the indeterminate sweep's start angle and wraps after a full period", "[progress-ring][tick]")
{
    Theme theme = Theme::light();

    ProgressRingControl ring;
    ring.arrange({0, 0, 32.0f, 32.0f});

    TestRenderer at_zero;
    ring.render(at_zero, theme);
    const Point start0 = only_arc(at_zero).geometry.figures.at(0).start;

    ring.tick(750ms); // a quarter into the 1500ms spin period -> quarter-turn rotation
    TestRenderer at_quarter;
    ring.render(at_quarter, theme);
    const Point start_quarter = only_arc(at_quarter).geometry.figures.at(0).start;
    const bool  same_point = std::abs(start0.x - start_quarter.x) < 1e-3 && std::abs(start0.y - start_quarter.y) < 1e-3;
    REQUIRE_FALSE(same_point);

    ring.tick(750ms); // 750 + 750 = 1500ms total: wraps the period back to exactly phase 0
    TestRenderer wrapped;
    ring.render(wrapped, theme);
    const Point start_wrapped = only_arc(wrapped).geometry.figures.at(0).start;
    REQUIRE_THAT(start_wrapped.x, WithinAbs(static_cast<double>(start0.x), 1e-2));
    REQUIRE_THAT(start_wrapped.y, WithinAbs(static_cast<double>(start0.y), 1e-2));
}

TEST_CASE("on_tick is a no-op while inactive or determinate", "[progress-ring][tick]")
{
    ProgressRingControl inactive;
    inactive.set_active(false);
    inactive.arrange({0, 0, 32.0f, 32.0f});
    ui::testing::clear_dirty(inactive);
    inactive.tick(500ms);
    REQUIRE_FALSE(inactive.is_dirty());

    ProgressRingControl determinate;
    determinate.set_indeterminate(false);
    determinate.arrange({0, 0, 32.0f, 32.0f});
    ui::testing::clear_dirty(determinate);
    determinate.tick(500ms);
    REQUIRE_FALSE(determinate.is_dirty());
}

// Windows1x's accent button is an outline on a COLOR_WINDOW face, so the ring reads
// ControlChrome::progress_fill first and falls back to the accent only when a theme has none.
TEST_CASE("A ProgressRing draws an arc that is visible against its own page, under every theme",
          "[progress-ring][render][regression]")
{
    for (ThemeVariant v :
         {ThemeVariant::Fluent2,
          ThemeVariant::Fluent,
          ThemeVariant::Mdl2,
          ThemeVariant::Metro,
          ThemeVariant::Aero,
          ThemeVariant::Luna,
          ThemeVariant::Windows9x,
          ThemeVariant::Windows3x,
          ThemeVariant::Windows1x})
    {
        CAPTURE(static_cast<int>(v));
        const Theme         theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        ProgressRingControl ring;
        ring.arrange({0, 0, 32.0f, 32.0f});
        TestRenderer renderer;
        ring.render(renderer, theme);

        CHECK(arc_color(only_arc(renderer)) != theme.colors().background);
    }
}

TEST_CASE("A Background draws the full track ring under the arc; none by default", "[progress-ring][render]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    ProgressRingControl ring{0.0};
    ring.set_indeterminate(false);
    const Size desired = ring.measure({Unbounded, Unbounded}, m, theme);
    ring.arrange({0.0f, 0.0f, desired.width, desired.height});

    TestRenderer bare;
    ring.render(bare, theme);
    REQUIRE(bare.count() == 0); // value == min: no arc, and the default background is transparent

    const Color light_gray = Color::from_argb(0xFFD3D3D3);
    ring.set_background(light_gray);
    ring.set_value(50.0);
    TestRenderer r;
    ring.render(r, theme);
    REQUIRE(r.count() == 2); // the full circle, then half an arc on top
    const auto& track = std::get<StrokeEllipse>(r.commands.front());
    CHECK(track.color == light_gray);
    CHECK(track.radius_x == 14.0f);
    CHECK(track.stroke_width == 4.0f);
    CHECK_FALSE(arc_color(only_arc(r)) == light_gray);

    ring.set_active(false);
    TestRenderer inactive;
    ring.render(inactive, theme);
    REQUIRE(inactive.count() == 0);
}

TEST_CASE("A theme-resource background draws the track and follows the theme", "[progress-ring][render][theme]")
{
    HeadlessTextMeasurer m;
    const Theme          light    = Theme::light();
    const Theme          dark     = Theme::fluent(Palette::fluent_dark());
    constexpr ThemeColor resource = [](const Theme& t) { return t.palette.text_fill_secondary; };
    REQUIRE_FALSE(resource(light) == resource(dark));

    ui::Control<ui::ProgressRing> ring;
    auto built = ui::build(ui::ProgressRing{.value = 50.0, .background = resource, .control = ring});
    REQUIRE(ring->background_resource() == resource);
    const Size desired = ring->measure({Unbounded, Unbounded}, m, light);
    ring->arrange({0.0f, 0.0f, desired.width, desired.height});

    TestRenderer in_light;
    ring->render(in_light, light);
    CHECK(std::get<StrokeEllipse>(in_light.commands.front()).color == resource(light));
    TestRenderer in_dark;
    ring->render(in_dark, dark);
    CHECK(std::get<StrokeEllipse>(in_dark.commands.front()).color == resource(dark));

    ring->set_background(std::nullopt);
    TestRenderer bare;
    ring->render(bare, dark);
    CHECK(bare.count() == 1); // the half arc alone
    CHECK(bare.count_of<DrawGeometry>() == 1);
}
