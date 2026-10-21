// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/CubicBezier.hpp"
#include "winui/render/Describe.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/scrolling/ScrollBarOverlay.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
// A fixed-size block of content, remembering the width it was offered.
class Block final : public Element
{
public:
    explicit Block(Size size) : size_(size)
    {
    }

    mutable Size offered{};

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
    {
        offered = available;
        return size_;
    }
    void render_override(Renderer& renderer, const RenderContext&) const override
    {
        renderer.fill_rounded_rect(bounds(), 0.0f, colors::Black);
    }


private:
    Size size_;
};

struct View
{
    std::unique_ptr<ScrollControl> view  = std::make_unique<ScrollControl>();
    Block*                         block = nullptr;
};

View make_view(Size                     content,
               Rect                     viewport    = {0, 0, 300, 200},
               ScrollContentOrientation orientation = ScrollContentOrientation::Vertical)
{
    View v;
    v.view->set_content_orientation(orientation);
    v.block = v.view->set_child(std::make_unique<Block>(content));
    v.block->set_horizontal_alignment(Alignment::Leading);
    v.block->set_vertical_alignment(Alignment::Leading);
    HeadlessTextMeasurer m;
    (void)v.view->measure(viewport.size(), m, Theme::default_light());
    v.view->arrange(viewport);
    return v;
}

PointerEvent wheel(Point p, float delta)
{
    return {p, PointerButton::None, PointerAction::Wheel, delta};
}
} // namespace

TEST_CASE("content_orientation decides which axes the content is measured unbounded along", "[scroll-view][layout]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    const auto offered = [&](ScrollContentOrientation orientation)
    {
        ScrollControl view;
        view.set_content_orientation(orientation);
        Block* block = view.set_child(std::make_unique<Block>(Size{50, 50}));
        (void)view.measure({300, 200}, m, theme);
        return block->offered;
    };
    // Measure: the viewport's width for Vertical and None, its height for Horizontal
    // and None, unbounded along the rest.
    REQUIRE(offered(ScrollContentOrientation::Vertical) == Size{300, Unbounded});
    REQUIRE(offered(ScrollContentOrientation::Horizontal) == Size{Unbounded, 200});
    REQUIRE(offered(ScrollContentOrientation::None) == Size{300, 200});
    REQUIRE(offered(ScrollContentOrientation::Both) == Size{Unbounded, Unbounded});
}

TEST_CASE("Content wider than the viewport scrolls horizontally too", "[scroll-view][layout]")
{
    View v = make_view({800, 500}, {0, 0, 300, 200}, ScrollContentOrientation::Both);
    REQUIRE_THAT(v.view->extent_width(), WithinAbs(800.0, 1e-3));
    REQUIRE_THAT(v.view->scrollable_width(), WithinAbs(500.0, 1e-3));
    REQUIRE_THAT(v.view->scrollable_height(), WithinAbs(300.0, 1e-3));
    REQUIRE(v.view->computed_horizontal_scroll_mode() == ScrollMode::Enabled);

    v.view->scroll_to(120, 40, {.animation_mode = ScrollAnimationMode::Disabled});
    REQUIRE_THAT(v.view->horizontal_offset(), WithinAbs(120.0, 1e-3));
    REQUIRE_THAT(v.block->bounds().x, WithinAbs(-120.0, 1e-3));
    REQUIRE_THAT(v.block->bounds().y, WithinAbs(-40.0, 1e-3));

    v.view->scroll_to(10000, -5, {.animation_mode = ScrollAnimationMode::Disabled});
    REQUIRE_THAT(v.view->horizontal_offset(), WithinAbs(500.0, 1e-3));
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("A Stretch child is stretched to the viewport; a smaller aligned one sits by its alignment",
          "[scroll-view][layout]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    ScrollControl stretched;
    Block*        a = stretched.set_child(std::make_unique<Block>(Size{50, 50}));
    (void)stretched.measure({300, 200}, m, theme);
    stretched.arrange({0, 0, 300, 200});
    REQUIRE(a->bounds() == Rect{0, 0, 300, 200}); // a Stretch child grows to the viewport

    ScrollControl centered;
    Block*        b = centered.set_child(std::make_unique<Block>(Size{50, 50}));
    b->set_horizontal_alignment(Alignment::Center);
    b->set_vertical_alignment(Alignment::Trailing);
    (void)centered.measure({300, 200}, m, theme);
    centered.arrange({0, 0, 300, 200});
    // Center rests in the middle, Bottom against the far edge.
    REQUIRE(b->bounds() == Rect{125, 150, 50, 50});
}

TEST_CASE("scroll_to animates over its distance-based duration and then completes", "[scroll-view][animation]")
{
    View v = make_view({300, 1000});

    std::vector<int> completed;
    v.view->on_scroll_completed([&](int id) { completed.push_back(id); });
    int views = 0;
    v.view->on_view_changed([&] { ++views; });

    const int id = v.view->scroll_to(0, 100);
    REQUIRE(v.view->state() == ScrollInteractionState::Animating);
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(0.0, 1e-3)); // nothing moved yet

    // 100 DIPs at 5 ms each: 500 ms.
    v.view->tick(250ms);
    const float halfway = v.view->vertical_offset();
    REQUIRE(halfway > 0.0f);
    REQUIRE(halfway < 100.0f);
    REQUIRE(completed.empty());

    v.view->tick(300ms);
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(100.0, 1e-3));
    REQUIRE(completed == std::vector<int>{id});
    REQUIRE(v.view->state() == ScrollInteractionState::Idle);
    REQUIRE(views >= 2);
}

TEST_CASE("A disabled animation scrolls at once and still completes", "[scroll-view][animation]")
{
    View             v = make_view({300, 1000});
    std::vector<int> completed;
    v.view->on_scroll_completed([&](int id) { completed.push_back(id); });

    const int id = v.view->scroll_by(0, 60, {.animation_mode = ScrollAnimationMode::Disabled});
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(60.0, 1e-3));
    REQUIRE(completed == std::vector<int>{id});
}

TEST_CASE("ScrollAnimationStarting can replace the animation's duration and key frames", "[scroll-view][animation]")
{
    View v = make_view({300, 1000});
    v.view->on_scroll_animation_starting(
        [](ScrollAnimationStarting& args)
        {
            REQUIRE(args.animation.key_frames.size() == 1);
            REQUIRE_THAT(args.animation.duration.count(), WithinAbs(1000.0, 1e-3)); // 400 DIPs, capped at 1000 ms
            REQUIRE(args.end_position == Point{0, 400});
            // A teleport: hold the start, jump to the target half way, arrive linearly.
            args.animation.duration   = 200ms;
            args.animation.key_frames = {
                {0.5f, {0, 0}, {easing::Linear, 0}},
                {0.5f, {0, 300}, {easing::Linear, 1}},
                {1.0f, {0, 400}, {easing::Linear, 0}},
            };
        });
    v.view->scroll_to(0, 400);
    v.view->tick(50ms);
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(0.0, 1e-3));
    v.view->tick(100ms); // 150 of 200 ms: half way between 300 and 400
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(350.0, 1e-2));
    v.view->tick(100ms);
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(400.0, 1e-3));
}

TEST_CASE("A newer view change interrupts the running one, which still completes", "[scroll-view][animation]")
{
    View             v = make_view({300, 1000});
    std::vector<int> completed;
    v.view->on_scroll_completed([&](int id) { completed.push_back(id); });

    const int first = v.view->scroll_to(0, 300);
    v.view->tick(100ms);
    const int second = v.view->scroll_to(0, 0, {.animation_mode = ScrollAnimationMode::Disabled});
    REQUIRE(completed == std::vector<int>{first, second});
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("zoom_to keeps the point under its center and scales the content by the factor", "[scroll-view][zoom]")
{
    View v = make_view({300, 200}, {0, 0, 300, 200}, ScrollContentOrientation::None);
    v.view->zoom_to(2.0f, std::nullopt, {.animation_mode = ScrollAnimationMode::Disabled});

    REQUIRE_THAT(v.view->zoom_factor(), WithinAbs(2.0, 1e-3));
    // offset' = z'/z * (offset + c) - c, about the viewport's center (150, 100).
    REQUIRE_THAT(v.view->horizontal_offset(), WithinAbs(150.0, 1e-3));
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(100.0, 1e-3));
    REQUIRE_THAT(v.view->scrollable_width(), WithinAbs(300.0, 1e-3));

    // The content stays laid out at its own size and is drawn through a transform.
    TestRenderer r;
    v.view->render(r, Theme::light());
    REQUIRE(r.count_of<PushTransform>() == 1);
    REQUIRE(r.count_of<PopTransform>() == 1);
    const auto& push = std::get<PushTransform>(r.commands[1]);
    REQUIRE_THAT(push.scale, WithinAbs(2.0, 1e-3));
    REQUIRE(describe(r.commands).find("PushTransform scale=2") != std::string::npos);
    // The block's top-left (content space) lands at offset -150,-100 once scaled.
    const Rect b = v.block->bounds();
    REQUIRE_THAT(b.x * push.scale + push.offset.x, WithinAbs(-150.0, 1e-3));
    REQUIRE_THAT(b.y * push.scale + push.offset.y, WithinAbs(-100.0, 1e-3));

    v.view->zoom_to(50.0f, std::nullopt, {.animation_mode = ScrollAnimationMode::Disabled});
    REQUIRE_THAT(v.view->zoom_factor(), WithinAbs(10.0, 1e-3));
    v.view->zoom_to(0.01f, std::nullopt, {.animation_mode = ScrollAnimationMode::Disabled});
    REQUIRE_THAT(v.view->zoom_factor(), WithinAbs(0.1, 1e-3));
}

TEST_CASE("An animated zoom takes its step-based duration", "[scroll-view][zoom][animation]")
{
    View             v = make_view({300, 200}, {0, 0, 300, 200}, ScrollContentOrientation::None);
    std::vector<int> zoomed;
    v.view->on_zoom_completed([&](int id) { zoomed.push_back(id); });

    const int id = v.view->zoom_to(4.0f); // three whole steps at 250 ms
    v.view->tick(400ms);
    REQUIRE(v.view->zoom_factor() > 1.0f);
    REQUIRE(v.view->zoom_factor() < 4.0f);
    REQUIRE(zoomed.empty());
    v.view->tick(400ms);
    REQUIRE_THAT(v.view->zoom_factor(), WithinAbs(4.0, 1e-3));
    REQUIRE(zoomed == std::vector<int>{id});
}

TEST_CASE("A request made before the first layout waits for it", "[scroll-view][zoom]")
{
    HeadlessTextMeasurer m;
    ScrollControl        view;
    view.set_content_orientation(ScrollContentOrientation::None);
    view.set_child(std::make_unique<Block>(Size{300, 200}));
    view.zoom_to(2.0f, std::nullopt, {.animation_mode = ScrollAnimationMode::Disabled});
    REQUIRE_THAT(view.zoom_factor(), WithinAbs(1.0, 1e-3)); // no viewport yet, so no center

    (void)view.measure({300, 200}, m, Theme::default_light());
    view.arrange({0, 0, 300, 200});
    REQUIRE_THAT(view.zoom_factor(), WithinAbs(2.0, 1e-3));
    REQUIRE_THAT(view.horizontal_offset(), WithinAbs(150.0, 1e-3));
}

TEST_CASE("Pointer input reaches zoomed content where it is drawn", "[scroll-view][zoom][input]")
{
    HeadlessTextMeasurer m;
    ScrollControl        view;
    view.set_content_orientation(ScrollContentOrientation::None);
    auto* button = view.set_child(std::make_unique<ButtonControl>("Zoomed"));
    button->set_horizontal_alignment(Alignment::Leading);
    button->set_vertical_alignment(Alignment::Leading);
    int clicks = 0;
    button->on_click([&] { ++clicks; });
    (void)view.measure({300, 200}, m, Theme::default_light());
    view.arrange({0, 0, 300, 200});
    view.zoom_to(2.0f, Point{0, 0}, {.animation_mode = ScrollAnimationMode::Disabled});

    // The button's own (content-space) center is drawn at twice its distance from the origin.
    const Point drawn{button->bounds().center().x * 2.0f, button->bounds().center().y * 2.0f};
    view.handle_pointer({drawn, PointerButton::Left, PointerAction::Down});
    view.handle_pointer({drawn, PointerButton::Left, PointerAction::Up});
    REQUIRE(clicks == 1);
}

TEST_CASE("Ctrl+wheel zooms about the pointer only while the zoom mode is Enabled", "[scroll-view][zoom][input]")
{
    View v = make_view({300, 200}, {0, 0, 300, 200}, ScrollContentOrientation::None);

    PointerEvent zoom_in   = wheel({150, 100}, -48.0f);
    zoom_in.modifiers.ctrl = true;
    REQUIRE_FALSE(v.view->handle_wheel(zoom_in)); // Disabled: left to whatever is around it
    REQUIRE_THAT(v.view->zoom_factor(), WithinAbs(1.0, 1e-3));

    v.view->set_zoom_mode(ScrollZoomMode::Enabled);
    REQUIRE(v.view->handle_wheel(zoom_in));
    REQUIRE_THAT(v.view->zoom_factor(), WithinAbs(1.1, 1e-3));
}

TEST_CASE("Scroll modes gate user input but not programmatic scrolls", "[scroll-view][input]")
{
    View v = make_view({300, 1000});
    REQUIRE(v.view->computed_vertical_scroll_mode() == ScrollMode::Enabled); // Auto, and scrollable

    v.view->set_vertical_scroll_mode(ScrollMode::Disabled);
    REQUIRE_FALSE(v.view->handle_wheel(wheel({10, 10}, 40.0f))); // chains outward
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(0.0, 1e-3));

    v.view->scroll_to(0, 50, {.animation_mode = ScrollAnimationMode::Disabled});
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(50.0, 1e-3));

    // Auto with nothing to scroll is Disabled, unless zooming is on.
    View small = make_view({100, 100});
    REQUIRE(small.view->computed_vertical_scroll_mode() == ScrollMode::Disabled);
    small.view->set_zoom_mode(ScrollZoomMode::Enabled);
    REQUIRE(small.view->computed_vertical_scroll_mode() == ScrollMode::Enabled);
}

TEST_CASE("Shift+wheel and a horizontal wheel scroll sideways", "[scroll-view][input]")
{
    View v = make_view({800, 1000}, {0, 0, 300, 200}, ScrollContentOrientation::Both);

    PointerEvent shifted    = wheel({10, 10}, 40.0f);
    shifted.modifiers.shift = true;
    REQUIRE(v.view->handle_wheel(shifted));
    REQUIRE_THAT(v.view->horizontal_offset(), WithinAbs(40.0, 1e-3));

    PointerEvent tilt     = wheel({10, 10}, 30.0f);
    tilt.horizontal_wheel = true;
    REQUIRE(v.view->handle_wheel(tilt));
    REQUIRE_THAT(v.view->horizontal_offset(), WithinAbs(70.0, 1e-3));
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(0.0, 1e-3));

    // A plain wheel over content that only scrolls sideways scrolls sideways.
    View wide = make_view({800, 100}, {0, 0, 300, 200}, ScrollContentOrientation::Both);
    REQUIRE(wide.view->handle_wheel(wheel({10, 10}, 25.0f)));
    REQUIRE_THAT(wide.view->horizontal_offset(), WithinAbs(25.0, 1e-3));
}

TEST_CASE("add_scroll_velocity coasts: constant with no decay, decaying to the minimum otherwise",
          "[scroll-view][inertia]")
{
    View             v = make_view({300, 5000});
    std::vector<int> completed;
    v.view->on_scroll_completed([&](int id) { completed.push_back(id); });

    const int constant = v.view->add_scroll_velocity({0, 100}, Point{0, 0});
    REQUIRE(v.view->state() == ScrollInteractionState::Inertia);
    v.view->tick(500ms);
    v.view->tick(500ms);
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(100.0, 1e-2));
    REQUIRE(completed.empty()); // still coasting

    v.view->add_scroll_velocity({0, 100}, Point{0, 0});
    REQUIRE(completed == std::vector<int>{constant});
    v.view->tick(1000ms);
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(300.0, 1e-2));

    // At or under the minimum velocity nothing moves, and the request completes at once.
    v.view->scroll_by(0, 0, {.animation_mode = ScrollAnimationMode::Disabled}); // stops the coasting
    const float here = v.view->vertical_offset();
    completed.clear();
    const int slow = v.view->add_scroll_velocity({0, 30}, Point{0, 0});
    REQUIRE(completed == std::vector<int>{slow});
    v.view->tick(1000ms);
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(here, 1e-3));

    // Decaying (the default 0.95): the distance to the minimum velocity is (v0 - 30) / -ln(0.05).
    View decaying = make_view({300, 5000});
    decaying.view->add_scroll_velocity({0, 200});
    for (int i = 0; i < 100; ++i)
    {
        decaying.view->tick(16ms);
    }
    REQUIRE_THAT(decaying.view->vertical_offset(), WithinAbs((200.0 - 30.0) / 2.995732, 0.05));
    REQUIRE(decaying.view->state() == ScrollInteractionState::Idle);
}

TEST_CASE("Inertia stops at an edge", "[scroll-view][inertia]")
{
    View v = make_view({300, 400}); // 200 to scroll
    v.view->add_scroll_velocity({0, 1000}, Point{0, 0});
    v.view->tick(1000ms);
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(200.0, 1e-3));
    REQUIRE(v.view->state() == ScrollInteractionState::Idle);
}

TEST_CASE("A tab-stop Scroll scrolls with the keyboard", "[scroll-view][keyboard]")
{
    View v = make_view({300, 2000});
    REQUIRE_FALSE(v.view->focusable());
    v.view->set_focusable(true);
    REQUIRE(v.view->focusable());
    ui::testing::set_focused(*v.view, true);

    const auto settle = [&]
    {
        for (int i = 0; i < 400; ++i)
        {
            v.view->tick(16ms);
        }
    };

    // Down: 15% of the 200px viewport, as a velocity.
    REQUIRE_FALSE(v.view->handle_key({Key::ArrowUp, KeyAction::Down})); // nothing above: left to focus
    REQUIRE(v.view->handle_key({Key::ArrowDown, KeyAction::Down}));
    settle();
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(30.0, 0.1));

    v.view->handle_key({Key::PageDown, KeyAction::Down});
    settle();
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(230.0, 0.2));

    v.view->handle_key({Key::End, KeyAction::Down});
    settle();
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(1800.0, 1e-3));
    v.view->handle_key({Key::Home, KeyAction::Down});
    settle();
    REQUIRE_THAT(v.view->vertical_offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("ScrollBarVisibility: Auto follows scrollability, Visible always, Hidden never", "[scroll-view][scrollbar]")
{
    View v = make_view({800, 1000}, {0, 0, 300, 200}, ScrollContentOrientation::Both);
    REQUIRE(v.view->computed_vertical_scroll_bar_visible());
    REQUIRE(v.view->computed_horizontal_scroll_bar_visible());

    v.view->set_vertical_scroll_bar_visibility(ScrollBarVisibility::Hidden);
    REQUIRE_FALSE(v.view->computed_vertical_scroll_bar_visible());

    View small = make_view({100, 100});
    REQUIRE_FALSE(small.view->computed_vertical_scroll_bar_visible());
    small.view->set_vertical_scroll_bar_visibility(ScrollBarVisibility::Visible);
    REQUIRE(small.view->computed_vertical_scroll_bar_visible());
    TestRenderer r;
    small.view->render(r, Theme::light());
    // The Visible bar draws its thumb across the whole track even with nothing to scroll.
    REQUIRE(std::holds_alternative<FillRoundedRect>(r.commands.back()));
}

TEST_CASE("Both bars stop short of each other's corner", "[scroll-view][scrollbar]")
{
    View         v = make_view({800, 1000}, {0, 0, 300, 200}, ScrollContentOrientation::Both);
    TestRenderer r;
    v.view->render(r, Theme::light());
    // The last two fills are the two thumbs: the vertical one ends 12px above the bottom, the
    // horizontal one 12px before the right edge.
    const auto& vertical_thumb   = std::get<FillRoundedRect>(r.commands[r.commands.size() - 2]);
    const auto& horizontal_thumb = std::get<FillRoundedRect>(r.commands.back());
    REQUIRE(vertical_thumb.bounds.bottom() <= 200.0f - 12.0f);
    REQUIRE(horizontal_thumb.bounds.right() <= 300.0f - 12.0f);
    REQUIRE(horizontal_thumb.bounds.bottom() == 200.0f);
    REQUIRE(horizontal_thumb.bounds.width > horizontal_thumb.bounds.height);
}

TEST_CASE("A horizontal ScrollBarOverlay is the vertical one transposed", "[scroll-view][scrollbar]")
{
    ScrollBarOverlay bar{Orientation::Horizontal};
    const Rect       viewport{0, 0, 300, 100};
    REQUIRE(bar.scrollable(viewport, 600.0f));
    REQUIRE_FALSE(bar.scrollable(viewport, 300.0f));
    REQUIRE(bar.band_bounds(viewport) == Rect{0, 88, 300, 12});
    REQUIRE(bar.up_button_bounds(viewport) == Rect{0, 88, 12, 12});
    REQUIRE(bar.down_button_bounds(viewport) == Rect{288, 88, 12, 12});

    float      offset = 0.0f;
    const Rect thumb  = bar.thumb_bounds(viewport, 600.0f, offset);
    REQUIRE(thumb.bottom() == 100.0f);
    REQUIRE(bar.handle_pointer({thumb.center(), PointerButton::Left, PointerAction::Down}, viewport, 600.0f, offset));
    REQUIRE(bar.handle_pointer({{thumb.center().x + 50.0f, thumb.center().y}, PointerButton::None, PointerAction::Move},
                               viewport,
                               600.0f,
                               offset));
    REQUIRE(offset > 0.0f);
}

namespace
{
PointerEvent move_to(Point p)
{
    return {p, PointerButton::None, PointerAction::Move};
}

// The thumbs' alphas, vertical then horizontal: the last two fills when both are collapsed.
std::pair<float, float> thumb_alphas(const ScrollControl& view)
{
    TestRenderer r;
    view.render(r, Theme::light());
    return {std::get<FillRoundedRect>(r.commands[r.commands.size() - 2]).color.a,
            std::get<FillRoundedRect>(r.commands.back()).color.a};
}

void idle(ScrollControl& view, double ms)
{
    for (double t = 0.0; t < ms; t += 50.0)
    {
        view.tick(50ms);
    }
}
} // namespace

TEST_CASE("Hovering either bar keeps both, and the hide resumes once neither is hovered", "[scroll-view][scrollbar]")
{
    View v = make_view({800, 1000}, {0, 0, 300, 200}, ScrollContentOrientation::Both);

    v.view->handle_pointer(move_to({294, 50})); // the vertical band
    idle(*v.view, 3000.0);
    REQUIRE(thumb_alphas(*v.view).second > 0.0f);

    v.view->handle_pointer(move_to({100, 50})); // off both bands
    idle(*v.view, 3000.0);
    REQUIRE(thumb_alphas(*v.view) == std::pair{0.0f, 0.0f});
}

TEST_CASE("A pointer jumping from one bar straight onto the other un-hovers the first", "[scroll-view][scrollbar]")
{
    View v = make_view({800, 1000}, {0, 0, 300, 200}, ScrollContentOrientation::Both);

    v.view->handle_pointer(move_to({100, 194})); // the horizontal band
    v.view->handle_pointer(move_to({294, 50}));  // the vertical band, skipping the corner
    v.view->handle_pointer(move_to({100, 50}));  // the content
    idle(*v.view, 3000.0);
    // Neither bar is left expanded, so both hide after the delay.
    REQUIRE(thumb_alphas(*v.view) == std::pair{0.0f, 0.0f});
}

TEST_CASE("Along a constrained axis content is clipped to the viewport unless its own size asks for more",
          "[scroll-view][layout]")
{
    // Wanting 800 is not enough: the desired size is clipped to the 300 offered.
    View wants = make_view({800, 500});
    REQUIRE_THAT(wants.view->extent_width(), WithinAbs(300.0, 1e-3));
    REQUIRE_THAT(wants.view->scrollable_width(), WithinAbs(0.0, 1e-3));

    // An explicit width or min_width widens the offer, and it scrolls.
    HeadlessTextMeasurer m;
    ScrollControl        view;
    Block*               block = view.set_child(std::make_unique<Block>(Size{800, 500}));
    block->set_min_width(600.0f);
    (void)view.measure({300, 200}, m, Theme::default_light());
    view.arrange({0, 0, 300, 200});
    REQUIRE(block->offered.width == 600.0f);
    REQUIRE_THAT(view.extent_width(), WithinAbs(600.0, 1e-3));
    REQUIRE_THAT(view.scrollable_width(), WithinAbs(300.0, 1e-3));
}

namespace
{
void click(HeadlessUI& ui, Point p)
{
    ui::testing::seams(ui.window()).handle_pointer({p, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({p, PointerButton::Left, PointerAction::Up});
}
} // namespace

TEST_CASE("A focusable Scroll gives a press on a control inside it to that control", "[scroll_view][focus]")
{
    HeadlessUI     ui;
    auto           scroller = std::make_unique<ScrollControl>();
    ScrollControl* view     = scroller.get();
    view->set_focusable(true);
    ui::ButtonControl* button = view->set_child(std::make_unique<ui::ButtonControl>("Inside"));
    button->set_horizontal_alignment(Alignment::Leading); // leaves the scroller's own space free
    button->set_vertical_alignment(Alignment::Leading);
    ui.window().set_content(std::move(scroller));
    ui.layout({400.0f, 300.0f});

    // Both are stops that cover the press; the one inside is the one under the pointer.
    REQUIRE(view->takes_focus_at(button->bounds().center()));
    click(ui, button->bounds().center());
    CHECK(ui.window().focused() == button);

    click(ui, Point{view->bounds().right() - 30.0f, view->bounds().bottom() - 30.0f});
    CHECK(ui.window().focused() == view);
}

// ---- the era's permanent scroll bar (Aero) ----

namespace
{
View make_era_view(Size content, Rect viewport, ScrollContentOrientation orientation = ScrollContentOrientation::Vertical)
{
    View v;
    v.view->set_content_orientation(orientation);
    v.block = v.view->set_child(std::make_unique<Block>(content));
    v.block->set_horizontal_alignment(Alignment::Leading);
    v.block->set_vertical_alignment(Alignment::Leading);
    HeadlessTextMeasurer m;
    (void)v.view->measure(viewport.size(), m, resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{}));
    v.view->arrange(viewport);
    return v;
}

std::vector<const FillPolygon*> arrows(const TestRenderer& renderer)
{
    std::vector<const FillPolygon*> out;
    for (const RenderCommand& command : renderer.commands)
    {
        if (const auto* polygon = std::get_if<FillPolygon>(&command); polygon != nullptr && polygon->points.size() == 3)
        {
            out.push_back(polygon);
        }
    }
    return out;
}
} // namespace

TEST_CASE("Aero's scroll bar takes a 17 DIP gutter from the viewport while it shows", "[scroll-view][layout][aero]")
{
    // Taller than the view: the vertical bar shows and the content is measured 17 narrower.
    View tall = make_era_view({200, 1000}, {0, 0, 300, 200});
    CHECK(tall.view->viewport_width() == 283.0f);
    CHECK(tall.view->viewport_height() == 200.0f);
    CHECK(tall.block->offered.width == 283.0f);
    CHECK(tall.view->scrollable_height() == 800.0f);

    // Content that fits takes no gutter.
    View fits = make_era_view({200, 100}, {0, 0, 300, 200});
    CHECK(fits.view->viewport_width() == 300.0f);

    // The overlay bar never narrows the view.
    View fluent = make_view({200, 1000}, {0, 0, 300, 200});
    CHECK(fluent.view->viewport_width() == 300.0f);
}

TEST_CASE("One era bar's gutter can call for the other", "[scroll-view][layout][aero]")
{
    // 290 wide fits 300 but not the 283 the vertical bar leaves, so both bars show and meet in
    // a corner cell.
    View both = make_era_view({290, 1000}, {0, 0, 300, 200}, ScrollContentOrientation::Both);
    CHECK(both.view->viewport_width() == 283.0f);
    CHECK(both.view->viewport_height() == 183.0f);

    TestRenderer renderer;
    both.view->render(renderer, resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{}));
    bool corner = false;
    for (const RenderCommand& command : renderer.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&command))
        {
            corner = corner || fill->bounds == Rect{283, 183, 17, 17};
        }
    }
    CHECK(corner);
}

TEST_CASE("Aero's arrows are bare glyphs until the pointer is over the bar", "[scroll-view][render][aero]")
{
    const Theme aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    View        v    = make_era_view({200, 1000}, {0, 0, 300, 200});

    TestRenderer idle;
    v.view->render(idle, aero);
    const auto idle_arrows = arrows(idle);
    REQUIRE(idle_arrows.size() == 2);
    CHECK(idle_arrows[0]->fill == Color::from_argb(0xFF707070));
    // The thumb has its chrome at rest: the gradient face and the #959595 outline.
    bool thumb_outline = false;
    for (const RenderCommand& command : idle.commands)
    {
        if (const auto* stroke = std::get_if<StrokeRoundedRect>(&command))
        {
            thumb_outline = thumb_outline || stroke->color == Color::from_argb(0xFF959595);
        }
    }
    CHECK(thumb_outline);

    // Over the track, below the thumb: the arrows turn dark and gain the outline too.
    v.view->handle_pointer({Point{292, 150}, PointerButton::None, PointerAction::Move});
    TestRenderer hovered;
    v.view->render(hovered, aero);
    const auto lit = arrows(hovered);
    REQUIRE(lit.size() == 2);
    CHECK(lit[0]->fill == Color::from_argb(0xFF212121));

    // Over the down arrow: its hover look.
    v.view->handle_pointer({Point{292, 195}, PointerButton::None, PointerAction::Move});
    TestRenderer on_arrow;
    v.view->render(on_arrow, aero);
    const auto hot = arrows(on_arrow);
    REQUIRE(hot.size() == 2);
    CHECK(hot[1]->fill == Color::from_argb(0xFF0D2A3A));
}

TEST_CASE("Aero's bar neither narrows nor fades when idle", "[scroll-view][animation][aero]")
{
    const Theme aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    View        v    = make_era_view({200, 1000}, {0, 0, 300, 200});
    for (int i = 0; i < 10; ++i)
    {
        v.view->tick(500ms);
    }
    TestRenderer renderer;
    v.view->render(renderer, aero);
    CHECK(arrows(renderer).size() == 2);
    // A track click pages by the 200 DIP viewport.
    v.view->handle_pointer({Point{292, 150}, PointerButton::Left, PointerAction::Down});
    v.view->handle_pointer({Point{292, 150}, PointerButton::Left, PointerAction::Up});
    CHECK(v.view->vertical_offset() > 0.0f);
}

TEST_CASE("Content measured to the viewport's width takes no horizontal gutter beside a vertical one",
          "[scroll-view][layout][aero]")
{
    // As wide as the view: a vertical view measures it again 17 narrower, so it never overflows.
    View tall = make_era_view({300, 1000}, {0, 0, 300, 200});
    CHECK(tall.view->viewport_width() == 283.0f);
    CHECK(tall.view->viewport_height() == 200.0f);
}

TEST_CASE("Zooming in gives Aero's bars their gutters", "[scroll-view][layout][aero]")
{
    const Theme aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    View        v    = make_era_view({300, 200}, {0, 0, 300, 200}, ScrollContentOrientation::None);
    CHECK(v.view->viewport_width() == 300.0f);
    CHECK(v.view->viewport_height() == 200.0f);

    ui::testing::clear_dirty(*v.view);
    (void)v.view->zoom_to(2.0f, std::nullopt, ScrollZoomOptions{.animation_mode = ScrollAnimationMode::Disabled});
    CHECK(v.view->is_dirty(DirtyFlags::Layout));
    HeadlessTextMeasurer m;
    (void)v.view->measure({300, 200}, m, aero);
    v.view->arrange({0, 0, 300, 200});
    CHECK(v.view->viewport_width() == 283.0f);
    CHECK(v.view->viewport_height() == 183.0f);
}

namespace
{
View make_view_in(const Theme& theme, Size content, Rect viewport)
{
    View v;
    v.view->set_content_orientation(ScrollContentOrientation::Vertical);
    v.block = v.view->set_child(std::make_unique<Block>(content));
    v.block->set_horizontal_alignment(Alignment::Leading);
    v.block->set_vertical_alignment(Alignment::Leading);
    HeadlessTextMeasurer m;
    (void)v.view->measure(viewport.size(), m, theme);
    v.view->arrange(viewport);
    return v;
}

Theme era(ThemeVariant variant)
{
    return resolve_theme(ThemeRequest{.variant = variant}, OsThemeSnapshot{});
}

bool has_fill(const TestRenderer& r, const Rect& bounds)
{
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&command); fill != nullptr && fill->bounds == bounds)
        {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("Luna's scroll bar has a 17 DIP gutter and chevron arrows", "[scroll-view][render][luna]")
{
    const Theme luna = era(ThemeVariant::Luna);
    View        v    = make_view_in(luna, {200, 1000}, {0, 0, 300, 200});
    CHECK(v.view->viewport_width() == 283.0f);
    TestRenderer r;
    v.view->render(r, luna);
    int chevrons = 0;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* polygon = std::get_if<FillPolygon>(&command); polygon != nullptr && polygon->points.size() == 6)
        {
            chevrons += polygon->fill == Color::from_argb(0xFF4D6185) ? 1 : 0;
        }
    }
    CHECK(chevrons == 2);
}

TEST_CASE("The Windows 9x bar is 16 DIPs on a dithered shaft that inverts under a held page click",
          "[scroll-view][render][retro]")
{
    const Theme classic = era(ThemeVariant::Windows9x);
    View        v       = make_view_in(classic, {200, 1000}, {0, 0, 300, 200});
    CHECK(v.view->viewport_width() == 284.0f);
    const auto patterns = [&]
    {
        TestRenderer r;
        v.view->render(r, classic);
        return r.count_of<FillRectPattern>();
    };
    CHECK(patterns() == 1);
    v.view->handle_pointer({Point{292, 150}, PointerButton::Left, PointerAction::Down});
    CHECK(patterns() == 2); // the held half, inverted
    v.view->handle_pointer({Point{292, 150}, PointerButton::Left, PointerAction::Up});
    CHECK(patterns() == 1);
}

TEST_CASE("Windows 3.x and 1.x/2.x scroll boxes keep their size whatever the content", "[scroll-view][render][retro]")
{
    // A 17 DIP square box under 3.x, 18 tall under 1.x/2.x, right under the up arrow.
    const Theme  win3x = era(ThemeVariant::Windows3x);
    View         three = make_view_in(win3x, {200, 1000}, {0, 0, 300, 200});
    TestRenderer r3;
    three.view->render(r3, win3x);
    CHECK(has_fill(r3, Rect{283, 17, 17, 17}));

    const Theme  old = era(ThemeVariant::Windows1x);
    View         two = make_view_in(old, {200, 5000}, {0, 0, 300, 200});
    TestRenderer r2;
    two.view->render(r2, old);
    CHECK(has_fill(r2, Rect{283, 17, 17, 18}));
}

namespace
{
// Content that acts on its own presses, as a button does.
class Presser final : public Element
{
public:
    int presses = 0;

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {600, 500};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
    bool pointer_override(const PointerEvent& e) override
    {
        if (e.action == PointerAction::Down)
        {
            ++presses;
        }
        return e.action == PointerAction::Down || e.action == PointerAction::Up;
    }
};

void drag(Element& target, Point from, Point to)
{
    target.handle_pointer({from, PointerButton::Left, PointerAction::Down});
    target.handle_pointer({Point{(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f}, PointerButton::None, PointerAction::Move});
    target.handle_pointer({to, PointerButton::None, PointerAction::Move});
    target.handle_pointer({to, PointerButton::Left, PointerAction::Up});
}
} // namespace

TEST_CASE("pan_on_drag moves the view with a drag of the content", "[scroll-view][pan]")
{
    View v = make_view({600, 500}, {0, 0, 300, 200}, ScrollContentOrientation::Both);
    (void)v.view->scroll_to(100.0f, 100.0f, {.animation_mode = ScrollAnimationMode::Disabled});

    // Off by default: the drag does nothing.
    drag(*v.view, {150, 100}, {100, 60});
    CHECK(v.view->horizontal_offset() == 100.0f);
    CHECK(v.view->vertical_offset() == 100.0f);
    CHECK(v.view->cursor_at({150, 100}) == std::nullopt);

    v.view->set_pan_on_drag(true);
    CHECK(v.view->cursor_at({150, 100}) == CursorShape::SizeAll);
    // The content follows the pointer: dragging it up and left scrolls down and right.
    drag(*v.view, {150, 100}, {100, 60});
    CHECK(v.view->horizontal_offset() == 150.0f);
    CHECK(v.view->vertical_offset() == 140.0f);
    CHECK(v.view->state() == ScrollInteractionState::Idle);

    // The offsets stay in the scrollable range.
    drag(*v.view, {150, 100}, {290, 190});
    CHECK(v.view->horizontal_offset() == 10.0f);
    CHECK(v.view->vertical_offset() == 50.0f);
    drag(*v.view, {150, 100}, {290, 190});
    CHECK(v.view->horizontal_offset() == 0.0f);
    CHECK(v.view->vertical_offset() == 0.0f);
}

TEST_CASE("pan_on_drag moves only the axes the user may scroll", "[scroll-view][pan]")
{
    View v = make_view({600, 500}, {0, 0, 300, 200}, ScrollContentOrientation::Both);
    v.view->set_pan_on_drag(true);
    v.view->set_horizontal_scroll_mode(ScrollMode::Disabled);
    drag(*v.view, {150, 100}, {100, 60});
    CHECK(v.view->horizontal_offset() == 0.0f);
    CHECK(v.view->vertical_offset() == 40.0f);

    // Content that fits pans nothing, and the press is left to whatever is around the view.
    View small = make_view({200, 100}, {0, 0, 300, 200}, ScrollContentOrientation::Both);
    small.view->set_pan_on_drag(true);
    CHECK(small.view->cursor_at({100, 50}) == std::nullopt);
    CHECK_FALSE(small.view->handle_pointer({Point{100, 50}, PointerButton::Left, PointerAction::Down}));
}

TEST_CASE("pan_on_drag leaves a press the content handles to the content", "[scroll-view][pan]")
{
    auto     view    = std::make_unique<ScrollControl>();
    Presser* presser = view->set_child(std::make_unique<Presser>());
    presser->set_horizontal_alignment(Alignment::Leading);
    presser->set_vertical_alignment(Alignment::Leading);
    view->set_content_orientation(ScrollContentOrientation::Both);
    view->set_pan_on_drag(true);
    HeadlessTextMeasurer m;
    (void)view->measure({300, 200}, m, Theme::default_light());
    view->arrange({0, 0, 300, 200});

    drag(*view, {150, 100}, {100, 60});
    CHECK(presser->presses == 1);
    CHECK(view->horizontal_offset() == 0.0f);
    CHECK(view->vertical_offset() == 0.0f);
}

TEST_CASE("pan_on_drag moves the view by the pointer's travel when zoomed", "[scroll-view][pan][zoom]")
{
    View v = make_view({600, 500}, {0, 0, 300, 200}, ScrollContentOrientation::Both);
    v.view->set_pan_on_drag(true);
    (void)v.view->zoom_to(2.0f, Point{0, 0}, ScrollZoomOptions{.animation_mode = ScrollAnimationMode::Disabled});
    const float h = v.view->horizontal_offset();
    const float y = v.view->vertical_offset();
    drag(*v.view, {150, 100}, {100, 60});
    CHECK_THAT(v.view->horizontal_offset(), WithinAbs(h + 50.0f, 1e-3));
    CHECK_THAT(v.view->vertical_offset(), WithinAbs(y + 40.0f, 1e-3));
}
