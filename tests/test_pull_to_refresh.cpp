// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/PullToRefresh.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
PointerEvent move(Point p)
{
    return {p, PointerButton::None, PointerAction::Move};
}
PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}

std::unique_ptr<StackControl> make_tall_stack(int n)
{
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->set_spacing(0.0f);
    for (int i = 0; i < n; ++i)
    {
        stack->add(std::make_unique<ButtonControl>("Item"));
    }
    return stack;
}

struct Fixture
{
    PullToRefreshControl ptr;
    ButtonControl*       content = nullptr;
    int                  clicks  = 0;

    Fixture()
    {
        content = ptr.set_content(std::make_unique<ButtonControl>("Row"));
        content->on_click([this] { ++clicks; });

        HeadlessTextMeasurer measurer;
        (void)ptr.measure({200.0f, 120.0f}, measurer, Theme::default_light());
        ptr.arrange({0, 0, 200, 120});
    }
};
} // namespace

TEST_CASE("A new PullToRefresh is not focusable, has no content, and starts Idle", "[pull-to-refresh]")
{
    PullToRefreshControl ptr;
    REQUIRE_FALSE(ptr.focusable());
    REQUIRE(ptr.content() == nullptr);
    REQUIRE(ptr.state() == PullToRefreshState::Idle);
    REQUIRE(ptr.pull_distance() == 0.0f);
}

TEST_CASE("arrange() alone leaves content at its unshifted base position", "[pull-to-refresh][layout]")
{
    Fixture f;
    REQUIRE_THAT(f.content->bounds().top(), WithinAbs(0.0f, 1e-3));
}

TEST_CASE("Dragging down past the drag threshold enters Interacting, clamped to the visualizer's size",
          "[pull-to-refresh][input]")
{
    Fixture f;

    f.ptr.handle_pointer(down({50, 10}));
    f.ptr.handle_pointer(move({50, 30})); // dy = 20, past the drag threshold
    REQUIRE(f.ptr.state() == PullToRefreshState::Interacting);
    REQUIRE_THAT(f.ptr.pull_distance(), WithinAbs(20.0, 1e-3));

    f.ptr.handle_pointer(move({50, 400})); // dy = 390, clamped to the visualizer's 100
    REQUIRE(f.ptr.state() == PullToRefreshState::Pending);
    REQUIRE_THAT(f.ptr.pull_distance(), WithinAbs(100.0, 1e-3));
    REQUIRE_THAT(f.ptr.interaction_ratio(), WithinAbs(1.0, 1e-4));
}

TEST_CASE(
    "A drag past 80% of the visualizer enters Pending, matching real RefreshVisualizer's "
    "own m_executionRatio",
    "[pull-to-refresh][input]")
{
    // The indicator commits to Pending once the pull passes the execution ratio
    // (0.8), not at the full pull distance.
    Fixture f;

    f.ptr.handle_pointer(down({50, 10}));
    f.ptr.handle_pointer(move({50, 89})); // dy = 79 < 0.8*100 = 80 -> still Interacting
    REQUIRE(f.ptr.state() == PullToRefreshState::Interacting);
    REQUIRE_THAT(f.ptr.pull_distance(), WithinAbs(79.0, 1e-3));

    f.ptr.handle_pointer(move({50, 92})); // dy = 82 > 80 -> Pending, short of the full 100
    REQUIRE(f.ptr.state() == PullToRefreshState::Pending);
    REQUIRE_THAT(f.ptr.pull_distance(), WithinAbs(82.0, 1e-3));
}

TEST_CASE("A mostly-horizontal drag does not steal the gesture - content still receives its click",
          "[pull-to-refresh][input]")
{
    Fixture f;

    f.ptr.handle_pointer(down({50, 10}));
    f.ptr.handle_pointer(move({90, 14})); // dx = 40 > dy = 4: horizontal, not a pull
    f.ptr.handle_pointer(up({90, 14}));

    REQUIRE(f.ptr.state() == PullToRefreshState::Idle);
    REQUIRE(f.clicks == 1); // ordinary click passthrough since the drag never committed
}

TEST_CASE("Releasing short of the execution threshold springs back to Idle", "[pull-to-refresh][input]")
{
    Fixture f;

    f.ptr.handle_pointer(down({50, 10}));
    f.ptr.handle_pointer(move({50, 40})); // dy = 30 < 80
    REQUIRE(f.ptr.state() == PullToRefreshState::Interacting);

    f.ptr.handle_pointer(up({50, 40}));
    REQUIRE(f.ptr.state() == PullToRefreshState::Idle);
    REQUIRE_THAT(f.ptr.pull_distance(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Releasing past the execution threshold enters Refreshing and fires on_refresh_requested once",
          "[pull-to-refresh][input]")
{
    Fixture f;
    int     requested = 0;
    f.ptr.on_refresh_requested([&] { ++requested; });

    f.ptr.handle_pointer(down({50, 10}));
    f.ptr.handle_pointer(move({50, 200})); // dy = 190, clamped to 100 -> Pending
    REQUIRE(f.ptr.state() == PullToRefreshState::Pending);

    f.ptr.handle_pointer(up({50, 200}));
    REQUIRE(f.ptr.state() == PullToRefreshState::Refreshing);
    // The content settles at the execution ratio of the visualizer.
    REQUIRE_THAT(f.ptr.pull_distance(), WithinAbs(80.0, 1e-3));
    REQUIRE(requested == 1);
}

TEST_CASE("request_refresh()/complete_refresh() form a two-call contract", "[pull-to-refresh]")
{
    Fixture f;
    int     requested = 0;
    f.ptr.on_refresh_requested([&] { ++requested; });

    f.ptr.complete_refresh(); // no-op while not Refreshing
    REQUIRE(f.ptr.state() == PullToRefreshState::Idle);

    f.ptr.request_refresh();
    REQUIRE(f.ptr.state() == PullToRefreshState::Refreshing);
    REQUIRE_THAT(f.ptr.pull_distance(), WithinAbs(80.0, 1e-3));
    REQUIRE(requested == 1);

    f.ptr.request_refresh(); // no-op while already Refreshing, does not re-fire
    REQUIRE(requested == 1);

    f.ptr.complete_refresh();
    REQUIRE(f.ptr.state() == PullToRefreshState::Idle);
    REQUIRE_THAT(f.ptr.pull_distance(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("on_refresh_state_changed fires once per actual transition", "[pull-to-refresh]")
{
    Fixture                         f;
    std::vector<PullToRefreshState> transitions;
    f.ptr.on_refresh_state_changed(
        [&](const PullToRefreshStateChangedArgs& args) { transitions.push_back(args.new_state); });

    f.ptr.handle_pointer(down({50, 10}));
    f.ptr.handle_pointer(move({50, 30}));  // -> Interacting
    f.ptr.handle_pointer(move({50, 200})); // -> Pending (clamped)
    f.ptr.handle_pointer(up({50, 200}));   // -> Refreshing
    f.ptr.complete_refresh();              // -> Idle

    const std::vector<PullToRefreshState> expected{
        PullToRefreshState::Interacting,
        PullToRefreshState::Pending,
        PullToRefreshState::Refreshing,
        PullToRefreshState::Idle,
    };
    REQUIRE(transitions == expected);
}

TEST_CASE("The pull gesture is gated on Scroll content already being scrolled to its top", "[pull-to-refresh][input]")
{
    HeadlessTextMeasurer measurer;

    PullToRefreshControl ptr;
    ScrollControl*       scroll = ptr.set_content(std::make_unique<ScrollControl>());
    scroll->set_child(make_tall_stack(20));

    (void)ptr.measure({200.0f, 120.0f}, measurer, Theme::default_light());
    ptr.arrange({0, 0, 200, 120});

    scroll->scroll_to(scroll->horizontal_offset(),
                      1'000'000.0f,
                      {.animation_mode = ui::ScrollAnimationMode::Disabled}); // clamped to the max, well past the top
    REQUIRE(scroll->vertical_offset() > 0.0f);

    ptr.handle_pointer(down({50, 10}));
    ptr.handle_pointer(move({50, 60})); // would be a pull, but content isn't at its top
    REQUIRE(ptr.state() == PullToRefreshState::Idle);
    REQUIRE_THAT(ptr.pull_distance(), WithinAbs(0.0, 1e-3));
    ptr.handle_pointer(up({50, 60}));

    scroll->scroll_to(scroll->horizontal_offset(), 0.0f, {.animation_mode = ui::ScrollAnimationMode::Disabled}); // back at the top
    ptr.handle_pointer(down({50, 10}));
    ptr.handle_pointer(move({50, 60})); // dy = 50, now allowed
    REQUIRE(ptr.state() != PullToRefreshState::Idle);
    REQUIRE(ptr.pull_distance() > 0.0f);
}

TEST_CASE("render() clips content always, plus the indicator strip only while pull_distance > 0",
          "[pull-to-refresh][render]")
{
    Fixture f;

    {
        TestRenderer r;
        f.ptr.render(r, Theme::light());
        REQUIRE(r.count_of<PushClip>() == 1); // content only, nothing revealed yet
        REQUIRE(r.count_of<PopClip>() == 1);
    }

    f.ptr.handle_pointer(down({50, 10}));
    f.ptr.handle_pointer(move({50, 50})); // dy = 40, revealed

    TestRenderer r;
    f.ptr.render(r, Theme::light());
    REQUIRE(r.count_of<PushClip>() == 2); // indicator strip + content
    REQUIRE(r.count_of<PopClip>() == 2);
    REQUIRE(std::get<PushClip>(r.commands.front()).bounds == Rect{0, 0, 200, 40});
}

TEST_CASE("The visualizer's content turns with the pull and spins while refreshing", "[pull-to-refresh][visualizer]")
{
    Fixture  f;
    Element* icon = f.ptr.visualizer_content();
    REQUIRE(icon != nullptr); // the Refresh symbol by default

    f.ptr.handle_pointer(down({50, 10}));
    f.ptr.handle_pointer(move({50, 50})); // 40 of 100: half-way to the execution ratio
    f.ptr.tick(16ms);
    CHECK_THAT(icon->current_rotation(), WithinAbs(180.0, 0.5));
    CHECK_THAT(icon->current_opacity(), WithinAbs(0.4, 1e-3));

    f.ptr.handle_pointer(move({50, 200})); // Pending: full opacity, pulsing
    f.ptr.tick(150ms);
    CHECK_THAT(icon->current_opacity(), WithinAbs(1.0, 1e-3));
    CHECK(icon->current_scale().x > 1.4f);

    f.ptr.handle_pointer(up({50, 200})); // Refreshing: a turn every 500 ms
    f.ptr.tick(125ms);
    CHECK_THAT(icon->current_rotation(), WithinAbs(90.0, 1.0));
}

TEST_CASE("RefreshStateChanged carries the old and new state, and a handler may stop the rotation",
          "[pull-to-refresh][visualizer]")
{
    Fixture f;
    auto*   sun = f.ptr.set_visualizer_content(std::make_unique<ButtonControl>("*"));
    std::vector<std::pair<PullToRefreshState, PullToRefreshState>> changes;
    f.ptr.on_refresh_state_changed(
        [&](const PullToRefreshStateChangedArgs& args)
        {
            changes.emplace_back(args.old_state, args.new_state);
            sun->stop_animation(AnimatedProperty::Rotation); // the gallery's custom icon does not turn
        });
    f.ptr.request_refresh();
    f.ptr.tick(125ms);
    REQUIRE(changes.size() == 1);
    CHECK(changes.front().first == PullToRefreshState::Idle);
    CHECK(changes.front().second == PullToRefreshState::Refreshing);
    CHECK_THAT(sun->current_rotation(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("content always joins focusables regardless of the current pull distance", "[pull-to-refresh][focus]")
{
    HeadlessTextMeasurer measurer;

    auto                  ptr_owned = std::make_unique<PullToRefreshControl>();
    PullToRefreshControl* ptr       = ptr_owned.get();
    ButtonControl*        inner     = ptr->set_content(std::make_unique<ButtonControl>("Inner"));

    Window window;
    window.set_content(std::move(ptr_owned));
    window.layout({200, 120}, measurer);

    std::vector<Element*> focusables;
    window.content()->collect_focusables(focusables);
    REQUIRE(std::find(focusables.begin(), focusables.end(), static_cast<Element*>(inner)) != focusables.end());
}

TEST_CASE("BottomToTop pulls the content up from the bottom, revealing the visualizer there",
          "[pull-to-refresh][direction]")
{
    Fixture f;
    f.ptr.set_pull_direction(RefreshPullDirection::BottomToTop);
    const float base_top = f.content->bounds().top();

    f.ptr.handle_pointer(down({50, 110}));
    f.ptr.handle_pointer(move({50, 130})); // downward: the wrong way, nothing happens
    REQUIRE(f.ptr.state() == PullToRefreshState::Idle);
    f.ptr.handle_pointer(up({50, 130}));

    f.ptr.handle_pointer(down({50, 110}));
    f.ptr.handle_pointer(move({50, 70})); // 40 up
    REQUIRE(f.ptr.state() == PullToRefreshState::Interacting);
    REQUIRE_THAT(f.ptr.pull_distance(), WithinAbs(40.0, 1e-3));

    TestRenderer r;
    f.ptr.render(r, Theme::light());
    REQUIRE(std::get<PushClip>(r.commands.front()).bounds == Rect{0, 80, 200, 40}); // the bottom strip
    REQUIRE_THAT(f.content->bounds().top(), WithinAbs(base_top - 40.0f, 1e-3));

    f.ptr.handle_pointer(move({50, 20}));
    f.ptr.handle_pointer(up({50, 20}));
    REQUIRE(f.ptr.state() == PullToRefreshState::Refreshing);
    f.ptr.tick(200ms);
    REQUIRE_THAT(f.content->bounds().top(), WithinAbs(base_top - 80.0f, 1e-3));
}

TEST_CASE("LeftToRight and RightToLeft pull sideways, and a vertical drag does not start them",
          "[pull-to-refresh][direction]")
{
    Fixture f;
    f.ptr.set_pull_direction(RefreshPullDirection::LeftToRight);
    const float base_left = f.content->bounds().left();

    f.ptr.handle_pointer(down({20, 10}));
    f.ptr.handle_pointer(move({20, 60})); // down: across the pull
    REQUIRE(f.ptr.state() == PullToRefreshState::Idle);
    f.ptr.handle_pointer(up({20, 60}));

    f.ptr.handle_pointer(down({20, 10}));
    f.ptr.handle_pointer(move({70, 12})); // 50 to the right
    REQUIRE_THAT(f.ptr.pull_distance(), WithinAbs(50.0, 1e-3));
    TestRenderer r;
    f.ptr.render(r, Theme::light()); // a pull marks Visual: rendering moves the content
    REQUIRE(std::get<PushClip>(r.commands.front()).bounds == Rect{0, 0, 50, 120}); // the left strip
    REQUIRE_THAT(f.content->bounds().left(), WithinAbs(base_left + 50.0f, 1e-3));
    f.ptr.handle_pointer(up({70, 12}));
    f.ptr.tick(200ms);

    f.ptr.set_pull_direction(RefreshPullDirection::RightToLeft);
    f.ptr.handle_pointer(down({180, 10}));
    f.ptr.handle_pointer(move({130, 12})); // 50 to the left
    REQUIRE_THAT(f.ptr.pull_distance(), WithinAbs(50.0, 1e-3));
    TestRenderer rr;
    f.ptr.render(rr, Theme::light());
    REQUIRE(std::get<PushClip>(rr.commands.front()).bounds == Rect{150, 0, 50, 120}); // the right strip
}

TEST_CASE("The indicator starts turned along a sideways pull, unless Orientation says otherwise",
          "[pull-to-refresh][direction][visualizer]")
{
    Fixture  f;
    Element* icon = f.ptr.visualizer_content();
    f.ptr.set_pull_direction(RefreshPullDirection::LeftToRight);
    f.ptr.handle_pointer(down({20, 10}));
    f.ptr.handle_pointer(move({30, 10})); // 10 of 100: an eighth of the way to the execution ratio
    f.ptr.tick(16ms);
    CHECK_THAT(icon->current_rotation(), WithinAbs(-90.0 + 45.0, 0.5));
    f.ptr.handle_pointer(up({30, 10}));
    f.ptr.tick(200ms);

    f.ptr.set_pull_direction(RefreshPullDirection::RightToLeft);
    f.ptr.tick(16ms);
    CHECK_THAT(icon->current_rotation(), WithinAbs(90.0, 0.5)); // Idle: the starting angle

    f.ptr.set_visualizer_orientation(RefreshVisualizerOrientation::Normal);
    f.ptr.tick(16ms);
    CHECK_THAT(icon->current_rotation(), WithinAbs(0.0, 0.5));
    f.ptr.set_visualizer_orientation(RefreshVisualizerOrientation::Rotate270DegreesCounterclockwise);
    f.ptr.tick(16ms);
    CHECK_THAT(icon->current_rotation(), WithinAbs(-90.0, 0.5));
}

TEST_CASE("BottomToTop is gated on the content being scrolled to its end", "[pull-to-refresh][direction][input]")
{
    HeadlessTextMeasurer measurer;
    PullToRefreshControl ptr;
    ptr.set_pull_direction(RefreshPullDirection::BottomToTop);
    ScrollControl* scroll = ptr.set_content(std::make_unique<ScrollControl>());
    scroll->set_child(make_tall_stack(20));
    (void)ptr.measure({200.0f, 120.0f}, measurer, Theme::default_light());
    ptr.arrange({0, 0, 200, 120});

    ptr.handle_pointer(down({50, 110}));
    ptr.handle_pointer(move({50, 60})); // at the top: a BottomToTop pull may not start
    REQUIRE(ptr.state() == PullToRefreshState::Idle);
    ptr.handle_pointer(up({50, 60}));

    scroll->scroll_to(scroll->horizontal_offset(), 1'000'000.0f, {.animation_mode = ui::ScrollAnimationMode::Disabled}); // clamped to the end
    ptr.handle_pointer(down({50, 110}));
    ptr.handle_pointer(move({50, 60}));
    REQUIRE(ptr.state() != PullToRefreshState::Idle);
}
