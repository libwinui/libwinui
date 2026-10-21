// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/TestRenderer.hpp"

#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace ui;
using namespace ui::testing;

TEST_CASE("draw_dotted_rect emits multiple short StrokeLine dashes, not one solid stroke", "[focus_rect]")
{
    TestRenderer renderer;
    draw_dotted_rect(renderer, Rect{10.0f, 10.0f, 60.0f, 20.0f}, colors::Black, 2.0f);

    REQUIRE(renderer.count() > 4); // far more than the 4 edges of a solid rectangle
    for (const auto& cmd : renderer.commands)
    {
        const auto& line   = std::get<StrokeLine>(cmd);
        const float length = std::abs(line.to.x - line.from.x) + std::abs(line.to.y - line.from.y);
        REQUIRE(length <= 2.0f); // no dash longer than dash_length
        REQUIRE(line.color == colors::Black);
    }
}

TEST_CASE("draw_dotted_rect degenerates gracefully for a zero-size rect", "[focus_rect]")
{
    TestRenderer renderer;
    draw_dotted_rect(renderer, Rect{0.0f, 0.0f, 0.0f, 0.0f}, colors::Black, 2.0f);
    REQUIRE(renderer.count() == 0);
}

TEST_CASE("A larger rect produces more dashes than a smaller one", "[focus_rect]")
{
    TestRenderer small_renderer;
    draw_dotted_rect(small_renderer, Rect{0.0f, 0.0f, 20.0f, 10.0f}, colors::Black, 2.0f);
    TestRenderer large_renderer;
    draw_dotted_rect(large_renderer, Rect{0.0f, 0.0f, 200.0f, 100.0f}, colors::Black, 2.0f);

    REQUIRE(large_renderer.count() > small_renderer.count());
}

// ---- The Fluent two-ring focus visual ----------------------------------------------------

TEST_CASE("draw_focus_ring_rings emits exactly two concentric StrokeRoundedRect rings", "[focus_rect]")
{
    TestRenderer         renderer;
    const Rect           bounds{10.0f, 10.0f, 100.0f, 40.0f};
    const FocusRingStyle style{FocusVisual::FluentRings,
                               colors::Black,
                               colors::White,
                               /*outer_width=*/2.0f,
                               /*inner_width=*/1.0f,
                               /*margin=*/3.0f};

    draw_focus_ring_rings(renderer, bounds, 4.0f, style);

    REQUIRE(renderer.count() == 2);
    const auto& outer = std::get<StrokeRoundedRect>(renderer.commands[0]);
    const auto& inner = std::get<StrokeRoundedRect>(renderer.commands[1]);

    // Strokes are centered on the path, so each ring's path sits half its width inside its band:
    // outer covers 1..3 out, inner 0..1, and the two touch like nested Borders.
    CHECK(outer.color == colors::Black);
    CHECK(outer.stroke_width == 2.0f);
    CHECK(outer.bounds == bounds.inflate(2.0f)); // margin(3) - outer_width(2)/2

    CHECK(inner.color == colors::White);
    CHECK(inner.stroke_width == 1.0f);
    CHECK(inner.bounds == bounds.inflate(0.5f)); // margin(3) - outer_width(2) - inner_width(1)/2

    CHECK(inner.radius < outer.radius);
}

TEST_CASE("draw_focus_ring dispatches to the classic dotted rect when style.kind is ClassicDotted", "[focus_rect]")
{
    TestRenderer         renderer;
    const FocusRingStyle style{FocusVisual::ClassicDotted, colors::Black, colors::White, 2.0f, 1.0f, 3.0f};

    draw_focus_ring(renderer, Rect{10.0f, 10.0f, 100.0f, 40.0f}, 4.0f, style);

    REQUIRE(renderer.count() > 0);
    for (const auto& cmd : renderer.commands)
    {
        REQUIRE(std::holds_alternative<StrokeLine>(cmd));
    }
}

TEST_CASE("draw_focus_ring dispatches to the two-ring visual when style.kind is FluentRings", "[focus_rect]")
{
    TestRenderer         renderer;
    const FocusRingStyle style{FocusVisual::FluentRings, colors::Black, colors::White, 2.0f, 1.0f, 3.0f};

    draw_focus_ring(renderer, Rect{10.0f, 10.0f, 100.0f, 40.0f}, 4.0f, style);

    REQUIRE(renderer.count() == 2);
    for (const auto& cmd : renderer.commands)
    {
        REQUIRE(std::holds_alternative<StrokeRoundedRect>(cmd));
    }
}

TEST_CASE("draw_focus_ring dispatches to an interleaved black+white dashed pair for MetroDashedPair", "[focus_rect]")
{
    TestRenderer renderer;
    const FocusRingStyle style{FocusVisual::MetroDashedPair, colors::White, colors::Black, 2.0f, 1.0f, 3.0f, /*dash_length=*/1.0f};

    draw_focus_ring(renderer, Rect{10.0f, 10.0f, 100.0f, 40.0f}, 4.0f, style);

    // Both colors appear, one pass per color, offset by one dash.
    REQUIRE(renderer.count() > 0);
    bool saw_outer = false, saw_inner = false;
    for (const auto& cmd : renderer.commands)
    {
        const auto& line = std::get<StrokeLine>(cmd);
        saw_outer |= (line.color == colors::White);
        saw_inner |= (line.color == colors::Black);
    }
    REQUIRE(saw_outer);
    REQUIRE(saw_inner);
}

TEST_CASE("draw_focus_ring_at puts the ring's outer edge where each side asks", "[focus_rect]")
{
    TestRenderer         renderer;
    const Rect           bounds{10.0f, 10.0f, 100.0f, 40.0f};
    const FocusRingStyle style{FocusVisual::FluentRings, colors::Black, colors::White, 2.0f, 1.0f, 3.0f};

    // 7 out at the sides, 3 above and below: the outer ring is 2 wide, so its path is 1 inside that.
    draw_focus_ring_at(renderer, bounds, Symmetric{.horizontal = 7.0f, .vertical = 3.0f}, 4.0f, style);
    REQUIRE(renderer.count() == 2);
    const auto& outer = std::get<StrokeRoundedRect>(renderer.commands[0]);
    CHECK(outer.bounds == Rect{4.0f, 8.0f, 112.0f, 44.0f});

    // The usual margin all round is what draw_focus_ring() draws.
    TestRenderer usual;
    TestRenderer plain;
    draw_focus_ring_at(usual, bounds, Thickness{3.0f}, 4.0f, style);
    draw_focus_ring(plain, bounds, 4.0f, style);
    CHECK(usual.commands == plain.commands);

    // Another kind of focus visual has no margin to move: it is drawn as it always is.
    FocusRingStyle dotted = style;
    dotted.kind           = FocusVisual::ClassicDotted;
    TestRenderer moved;
    TestRenderer fixed;
    draw_focus_ring_at(moved, bounds, Symmetric{.horizontal = 7.0f, .vertical = 3.0f}, 0.0f, dotted);
    draw_focus_ring(fixed, bounds, 0.0f, dotted);
    CHECK(moved.commands == fixed.commands);
}
