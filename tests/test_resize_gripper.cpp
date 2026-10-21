// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panes/ResizeGripper.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE("ResizeGripper measures to a fixed square regardless of available space", "[resize_gripper][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ResizeGripperControl gripper;
    const Size           desired = gripper.measure({500, 400}, m, theme);
    REQUIRE_THAT(desired.width, WithinAbs(gripper.size(), 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(gripper.size(), 1e-3));

    const Size unbounded = gripper.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE_THAT(unbounded.width, WithinAbs(gripper.size(), 1e-3));
    REQUIRE_THAT(unbounded.height, WithinAbs(gripper.size(), 1e-3));

    gripper.set_size(24.0f);
    const Size resized = gripper.measure({500, 400}, m, theme);
    REQUIRE_THAT(resized.width, WithinAbs(24.0, 1e-3));
    REQUIRE_THAT(resized.height, WithinAbs(24.0, 1e-3));
}

TEST_CASE("ResizeGripper is not focusable", "[resize_gripper][focus]")
{
    ResizeGripperControl gripper;
    REQUIRE_FALSE(gripper.focusable());

    std::vector<Element*> focusables;
    gripper.collect_focusables(focusables);
    REQUIRE(focusables.empty());
}

TEST_CASE("ResizeGripper reports hover state from Move events", "[resize_gripper][input]")
{
    ResizeGripperControl gripper;
    gripper.arrange({0, 0, 16, 16});
    REQUIRE_FALSE(gripper.hovered());

    gripper.handle_pointer({{8, 8}, PointerButton::None, PointerAction::Move});
    REQUIRE(gripper.hovered());

    gripper.handle_pointer({{}, PointerButton::None, PointerAction::Leave});
    REQUIRE_FALSE(gripper.hovered());
}

TEST_CASE("ResizeGripper fires incremental two-axis deltas while dragging", "[resize_gripper][input]")
{
    ResizeGripperControl gripper;
    gripper.arrange({100, 100, 16, 16});

    std::vector<std::pair<float, float>> deltas;
    gripper.on_drag_delta([&](Point delta) { deltas.emplace_back(delta.x, delta.y); });

    gripper.handle_pointer({{105, 105}, PointerButton::Left, PointerAction::Down});
    REQUIRE(gripper.dragging());

    gripper.handle_pointer({{120, 112}, PointerButton::None, PointerAction::Move});
    gripper.handle_pointer({{135, 130}, PointerButton::None, PointerAction::Move});

    REQUIRE(deltas.size() == 2);
    REQUIRE_THAT(deltas[0].first, WithinAbs(15.0, 1e-3));  // 120 - 105
    REQUIRE_THAT(deltas[0].second, WithinAbs(7.0, 1e-3));  // 112 - 105
    REQUIRE_THAT(deltas[1].first, WithinAbs(15.0, 1e-3));  // 135 - 120
    REQUIRE_THAT(deltas[1].second, WithinAbs(18.0, 1e-3)); // 130 - 112

    gripper.handle_pointer({{135, 130}, PointerButton::Left, PointerAction::Up});
    REQUIRE_FALSE(gripper.dragging());

    gripper.handle_pointer({{200, 200}, PointerButton::None, PointerAction::Move});
    REQUIRE(deltas.size() == 2);
}

TEST_CASE("A drag started outside the gripper's bounds does not start dragging", "[resize_gripper][input]")
{
    ResizeGripperControl gripper;
    gripper.arrange({0, 0, 16, 16});

    gripper.handle_pointer({{500, 500}, PointerButton::Left, PointerAction::Down});
    REQUIRE_FALSE(gripper.dragging());
}

TEST_CASE("ResizeGripper renders six dots and nothing else", "[resize_gripper][render]")
{
    ResizeGripperControl gripper;
    gripper.arrange({0, 0, 16, 16});

    TestRenderer renderer;
    Theme        theme = Theme::light();
    gripper.render(renderer, theme);

    REQUIRE(renderer.count_of<FillEllipse>() == 6);
    REQUIRE(renderer.count() == 6);
}
