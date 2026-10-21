// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/CubicBezier.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/containers/Border.hpp"
#include "winui/ui/dialogs/ToolTip.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/media/Shape.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{

PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}

std::unique_ptr<RectangleControl> square(float size)
{
    auto r = std::make_unique<RectangleControl>();
    r->set_width(size);
    r->set_height(size);
    r->set_fill(Brush{colors::Black});
    r->set_horizontal_alignment(Alignment::Leading);
    r->set_vertical_alignment(Alignment::Leading);
    return r;
}

template <typename T>
std::size_t count_of(const TestRenderer& r)
{
    std::size_t n = 0;
    for (const RenderCommand& c : r.commands)
    {
        n += std::holds_alternative<T>(c) ? 1 : 0;
    }
    return n;
}

template <typename T>
const T* first_of(const TestRenderer& r)
{
    for (const RenderCommand& c : r.commands)
    {
        if (const T* t = std::get_if<T>(&c))
        {
            return t;
        }
    }
    return nullptr;
}

struct Host
{
    HeadlessTextMeasurer measurer;
    Window               window;

    template <typename T>
    T* show(std::unique_ptr<T> content)
    {
        T* observer = window.set_content(std::move(content));
        layout();
        return observer;
    }
    void layout()
    {
        window.layout(Size{400.0f, 300.0f}, measurer);
    }
    void run(double ms, double step = 10.0)
    {
        for (double t = 0.0; t < ms - 1e-9; t += step)
        {
            window.tick(Duration{step});
        }
        layout();
    }
};

} // namespace

// ---- easing ----

TEST_CASE("easing functions follow their curves and modes", "[animation]")
{
    // EaseOut is the default mode.
    CHECK_THAT(ease(QuadraticEase{}, 0.5f), WithinAbs(0.75f, 1e-6));
    CHECK_THAT(ease(CubicEase{EasingMode::EaseIn}, 0.5f), WithinAbs(0.125f, 1e-6));
    CHECK_THAT(ease(QuarticEase{EasingMode::EaseIn}, 0.5f), WithinAbs(0.0625f, 1e-6));
    CHECK_THAT(ease(QuinticEase{EasingMode::EaseIn}, 0.5f), WithinAbs(0.03125f, 1e-6));
    // EaseInOut: the first half is EaseIn at double speed, halved.
    CHECK_THAT(ease(CubicEase{EasingMode::EaseInOut}, 0.25f), WithinAbs(0.0625f, 1e-6));
    CHECK_THAT(ease(CubicEase{EasingMode::EaseInOut}, 0.75f), WithinAbs(0.9375f, 1e-6));
    CHECK_THAT(ease(PowerEase{3.0f, EasingMode::EaseIn}, 0.5f), WithinAbs(0.125f, 1e-6));
    CHECK_THAT(ease(SineEase{EasingMode::EaseIn}, 0.5f), WithinAbs(1.0f - std::sin(0.25f * 3.14159265f), 1e-5));
    CHECK_THAT(ease(CircleEase{EasingMode::EaseIn}, 0.6f), WithinAbs(0.2f, 1e-5));
    // ExponentialEase: an exponent of 0 is linear.
    CHECK_THAT(ease(ExponentialEase{0.0f, EasingMode::EaseIn}, 0.3f), WithinAbs(0.3f, 1e-6));
    CHECK_THAT(ease(ExponentialEase{4.5f, EasingMode::EaseIn}, 0.5f),
               WithinAbs((std::exp(2.25f) - 1.0f) / (std::exp(4.5f) - 1.0f), 1e-5));
    // Back pulls back below zero on the way in.
    CHECK(ease(BackEase{1.0f, EasingMode::EaseIn}, 0.3f) < 0.0f);

    const std::vector<EasingFunction> all{
        CubicBezier{0.8f, 0.0f, 0.2f, 1.0f},
        BackEase{},
        BounceEase{},
        CircleEase{},
        CubicEase{},
        ElasticEase{},
        ExponentialEase{},
        PowerEase{},
        QuadraticEase{},
        QuarticEase{},
        QuinticEase{},
        SineEase{},
        BounceEase{3, 2.0f, EasingMode::EaseInOut},
        ElasticEase{3, 3.0f, EasingMode::EaseIn},
    };
    for (const EasingFunction& f : all)
    {
        CHECK_THAT(ease(f, 0.0f), WithinAbs(0.0f, 1e-4));
        CHECK_THAT(ease(f, 1.0f), WithinAbs(1.0f, 1e-4));
    }
}

TEST_CASE("key frames sample like DoubleAnimationUsingKeyFrames", "[animation]")
{
    const std::vector<KeyFrame> frames{KeyFrame::linear(100ms, 10.0f),
                                       KeyFrame::discrete(200ms, 20.0f),
                                       KeyFrame::spline(300ms, 30.0f, CubicBezier{0.0f, 0.0f, 1.0f, 1.0f})};
    // The first segment starts from the base value.
    CHECK_THAT(sample_key_frames(frames, 0.0f, 50ms), WithinAbs(5.0f, 1e-5));
    CHECK_THAT(sample_key_frames(frames, 0.0f, 100ms), WithinAbs(10.0f, 1e-5));
    // A discrete frame holds the previous value until its own time.
    CHECK_THAT(sample_key_frames(frames, 0.0f, 199ms), WithinAbs(10.0f, 1e-5));
    CHECK_THAT(sample_key_frames(frames, 0.0f, 200ms), WithinAbs(20.0f, 1e-5));
    CHECK_THAT(sample_key_frames(frames, 0.0f, 250ms), WithinAbs(25.0f, 1e-4));
    // After the last frame its value holds.
    CHECK_THAT(sample_key_frames(frames, 0.0f, 900ms), WithinAbs(30.0f, 1e-5));
    CHECK(key_frames_duration(frames) == 300ms);
}

// ---- the render/input wrappers ----

TEST_CASE("an element at rest renders exactly its own commands", "[animation]")
{
    ButtonControl plain{"Plain"};
    TestRenderer  a;
    plain.render(a);

    ButtonControl moved{"Plain"};
    moved.set_opacity(1.0f);
    moved.set_translation(Vector3{});
    TestRenderer b;
    moved.render(b);
    CHECK(a.commands.size() == b.commands.size());
    CHECK(count_of<PushTransform>(b) == 0);
    CHECK(count_of<PushOpacityLayer>(b) == 0);
}

TEST_CASE("opacity, translation, scale and rotation wrap the element's commands", "[animation]")
{
    Host              host;
    RectangleControl* rect = host.show(square(50.0f));
    rect->set_opacity(0.5f);
    rect->set_translation(Vector3{10.0f, 20.0f, 0.0f});

    TestRenderer r;
    rect->render(r);
    const auto* layer = first_of<PushOpacityLayer>(r);
    REQUIRE(layer != nullptr);
    CHECK_THAT(layer->opacity, WithinAbs(0.5f, 1e-6));
    const auto* transform = first_of<PushTransform>(r);
    REQUIRE(transform != nullptr);
    CHECK_THAT(transform->offset.x, WithinAbs(10.0f, 1e-4));
    CHECK_THAT(transform->offset.y, WithinAbs(20.0f, 1e-4));
    CHECK(count_of<PopTransform>(r) == 1);
    CHECK(count_of<PopOpacityLayer>(r) == 1);

    // A scale of 2 about the center keeps the center where it was.
    rect->set_translation(Vector3{});
    rect->set_center_point(Vector3{25.0f, 25.0f, 0.0f});
    rect->set_scale(Vector3{2.0f, 2.0f, 1.0f});
    const Matrix m      = rect->visual_transform();
    const Point  center = rect->bounds().center();
    CHECK_THAT(m.apply(center).x, WithinAbs(center.x, 1e-4));
    CHECK_THAT(m.apply(center).y, WithinAbs(center.y, 1e-4));
    CHECK_THAT(m.apply(rect->bounds().origin()).x, WithinAbs(rect->bounds().x - 25.0f, 1e-4));

    // A quarter turn clockwise about the center moves the top-left corner to the top-right.
    rect->set_scale(Vector3{1.0f, 1.0f, 1.0f});
    rect->set_rotation(90.0f);
    const Point corner = rect->visual_transform().apply(rect->bounds().origin());
    CHECK_THAT(corner.x, WithinAbs(rect->bounds().right(), 1e-3));
    CHECK_THAT(corner.y, WithinAbs(rect->bounds().y, 1e-3));
    TestRenderer rotated;
    rect->render(rotated);
    const auto* skew = first_of<PushTransform>(rotated);
    REQUIRE(skew != nullptr);
    CHECK((skew->m12 != 0.0f || skew->m21 != 0.0f));

    // Fully transparent draws nothing at all.
    rect->set_opacity(0.0f);
    TestRenderer none;
    rect->render(none);
    CHECK(none.commands.empty());
}

TEST_CASE("pointer input reaches a moved element where it is drawn", "[animation]")
{
    Host           host;
    auto           stack  = std::make_unique<StackControl>(Orientation::Vertical);
    ButtonControl* button = stack->add(std::make_unique<ButtonControl>("Moved"));
    button->set_horizontal_alignment(Alignment::Leading);
    int clicks = 0;
    button->on_click([&] { ++clicks; });
    host.show(std::move(stack));

    const Rect laid_out = button->bounds();
    button->set_translation(Vector3{200.0f, 0.0f, 0.0f});
    host.layout();

    const Point old_center = laid_out.center();
    ui::testing::seams(host.window).handle_pointer(down(old_center));
    ui::testing::seams(host.window).handle_pointer(up(old_center));
    CHECK(clicks == 0);

    const Point new_center{old_center.x + 200.0f, old_center.y};
    ui::testing::seams(host.window).handle_pointer(down(new_center));
    ui::testing::seams(host.window).handle_pointer(up(new_center));
    CHECK(clicks == 1);

    button->set_hit_test_visible(false);
    ui::testing::seams(host.window).handle_pointer(down(new_center));
    ui::testing::seams(host.window).handle_pointer(up(new_center));
    CHECK(clicks == 1);
}

TEST_CASE("a Collapsed element takes no room, draws nothing and leaves the tab ring", "[animation]")
{
    Host           host;
    auto           stack  = std::make_unique<StackControl>(Orientation::Vertical);
    ButtonControl* first  = stack->add(std::make_unique<ButtonControl>("First"));
    ButtonControl* second = stack->add(std::make_unique<ButtonControl>("Second"));
    host.show(std::move(stack));
    const float second_top = second->bounds().y;

    first->set_visibility(Visibility::Collapsed);
    host.layout();
    CHECK(first->bounds().width == 0.0f);
    CHECK(second->bounds().y < second_top);

    TestRenderer r;
    first->render(r);
    CHECK(r.commands.empty());

    host.window.focus_content();
    CHECK(host.window.focused() == second);
    host.window.focus_next();
    CHECK(host.window.focused() == second);
    CHECK_FALSE(first->is_effectively_visible());
}

// ---- implicit transitions ----

TEST_CASE("a ScalarTransition animates opacity along (0.8,0)-(0.2,1) over 300 ms", "[animation]")
{
    Host              host;
    RectangleControl* rect = host.show(square(50.0f));
    rect->set_opacity_transition(ScalarTransition{});
    rect->set_opacity(0.5f);

    // The property is the new value at once; what is drawn catches up.
    CHECK(rect->opacity() == 0.5f);
    CHECK_THAT(rect->current_opacity(), WithinAbs(1.0f, 1e-6));
    CHECK(rect->is_animating(AnimatedProperty::Opacity));
    host.run(150.0);
    // The curve is symmetric, so halfway through time is halfway through the change.
    CHECK_THAT(rect->current_opacity(), WithinAbs(0.75f, 1e-3));
    host.run(160.0);
    CHECK_THAT(rect->current_opacity(), WithinAbs(0.5f, 1e-6));
    CHECK_FALSE(rect->is_animating(AnimatedProperty::Opacity));

    rect->set_opacity_transition(std::nullopt);
    rect->set_opacity(1.0f);
    CHECK(rect->current_opacity() == 1.0f);
}

TEST_CASE("a Vector3Transition animates only its components; the rest snap", "[animation]")
{
    Host              host;
    RectangleControl* rect = host.show(square(50.0f));
    rect->set_translation_transition(Vector3Transition{300ms, Vector3Components::X});
    rect->set_translation(Vector3{100.0f, 100.0f, 0.0f});
    const Vector3 shown = rect->current_translation();
    CHECK(shown.x == 0.0f);
    CHECK(shown.y == 100.0f);
    host.run(300.0);
    CHECK_THAT(rect->current_translation().x, WithinAbs(100.0f, 1e-4));
}

TEST_CASE("a transition set before layout does not animate the initial value", "[animation]")
{
    RectangleControl rect;
    rect.set_opacity_transition(ScalarTransition{});
    rect.set_opacity(0.2f);
    CHECK(rect.current_opacity() == 0.2f);
}

// ---- start_animation ----

TEST_CASE("a DoubleAnimation eases from the current value and holds its end", "[animation]")
{
    Host              host;
    RectangleControl* rect      = host.show(square(50.0f));
    int               completed = 0;
    rect->start_animation(AnimatedProperty::TranslationX,
                          DoubleAnimation{.to           = 200.0f,
                                          .duration     = 500ms,
                                          .easing       = CircleEase{EasingMode::EaseInOut},
                                          .on_completed = [&] { ++completed; }});
    host.run(250.0);
    CHECK_THAT(rect->current_translation().x, WithinAbs(100.0f, 0.5f));
    host.run(260.0);
    CHECK(completed == 1);
    CHECK_THAT(rect->translation().x, WithinAbs(200.0f, 1e-4));
    CHECK_FALSE(rect->is_animating(AnimatedProperty::Translation));

    // Back again: from where it is to 0, 150 ms, an accelerating exponential.
    rect->start_animation(AnimatedProperty::TranslationX,
                          DoubleAnimation{.to = 0.0f, .duration = 150ms, .easing = ExponentialEase{4.5f, EasingMode::EaseIn}});
    host.run(75.0);
    CHECK(rect->current_translation().x > 150.0f); // barely moved yet
    host.run(80.0);
    CHECK_THAT(rect->translation().x, WithinAbs(0.0f, 1e-4));
}

TEST_CASE("a DoubleAnimation honors BeginTime, AutoReverse, RepeatBehavior and FillBehavior", "[animation]")
{
    Host              host;
    RectangleControl* rect = host.show(square(50.0f));
    rect->start_animation(AnimatedProperty::Opacity,
                          DoubleAnimation{.from         = 1.0f,
                                          .to           = 0.0f,
                                          .duration     = 100ms,
                                          .begin_time   = 100ms,
                                          .auto_reverse = true,
                                          .repeat_count = 2.0,
                                          .fill         = FillBehavior::Stop});
    host.run(50.0);
    CHECK(rect->current_opacity() == 1.0f); // not begun
    host.run(100.0);                        // 50 ms in: halfway down
    CHECK_THAT(rect->current_opacity(), WithinAbs(0.5f, 1e-3));
    host.run(100.0); // 150 ms in: halfway back up
    CHECK_THAT(rect->current_opacity(), WithinAbs(0.5f, 1e-3));
    host.run(300.0); // done; Stop hands the property back
    CHECK(rect->current_opacity() == 1.0f);
    CHECK(rect->opacity() == 1.0f);
}

TEST_CASE("a spring settles on its final value and hands its velocity to the next", "[animation]")
{
    Host              host;
    RectangleControl* rect      = host.show(square(50.0f));
    int               completed = 0;
    rect->start_animation(AnimatedProperty::Scale,
                          SpringAnimation{.final_value   = Vector3{1.5f, 1.5f, 1.5f},
                                          .damping_ratio = 0.6f,
                                          .period        = 50ms,
                                          .on_completed  = [&] { ++completed; }});
    host.run(30.0);
    const float mid = rect->current_scale().x;
    CHECK(mid > 1.0f);
    // An underdamped spring overshoots.
    float peak = 0.0f;
    for (int i = 0; i < 20; ++i)
    {
        host.run(5.0, 5.0);
        peak = std::max(peak, rect->current_scale().x);
    }
    CHECK(peak > 1.5f);
    host.run(2000.0);
    CHECK(completed == 1);
    CHECK_THAT(rect->scale().x, WithinAbs(1.5f, 1e-2));

    // Retargeting mid-flight continues from the current value without a jump.
    rect->start_animation(AnimatedProperty::Scale, SpringAnimation{.final_value = Vector3{1.0f, 1.0f, 1.0f}});
    host.run(20.0);
    const float before = rect->current_scale().x;
    rect->start_animation(AnimatedProperty::Scale, SpringAnimation{.final_value = Vector3{1.5f, 1.5f, 1.5f}});
    CHECK_THAT(rect->current_scale().x, WithinAbs(before, 1e-5));
}

TEST_CASE("an expression animation reads another element's animated values every frame", "[animation]")
{
    Host              host;
    auto              stack   = std::make_unique<StackControl>(Orientation::Vertical);
    RectangleControl* source  = stack->add(square(50.0f));
    RectangleControl* ellipse = stack->add(square(50.0f));
    host.show(std::move(stack));

    ellipse->start_animation(AnimatedProperty::Scale,
                             ExpressionAnimation{.expression = Callback<Vector3()>{
                                                     [source]
                                                     {
                                                         const Vector3 s = source->current_scale();
                                                         return Vector3{1.0f / s.x, 1.0f / s.y, 1.0f};
                                                     }}});
    source->set_scale(Vector3{2.0f, 2.0f, 1.0f});
    CHECK_THAT(ellipse->current_scale().x, WithinAbs(0.5f, 1e-6));
    // A scalar expression drives every component of a Vector3 property.
    ellipse->start_animation(AnimatedProperty::Translation,
                             ExpressionAnimation{.expression = Callback<float()>{[] { return 7.0f; }}});
    CHECK(ellipse->current_translation().y == 7.0f);
    ellipse->stop_animation(AnimatedProperty::Translation);
    CHECK(ellipse->current_translation().y == 0.0f);
}

// ---- transitions ----

TEST_CASE("ChildrenTransitions' EntranceTransition plays when a child is first laid out", "[animation]")
{
    Host host;
    auto stack = std::make_unique<StackControl>(Orientation::Horizontal);
    stack->set_children_transitions({EntranceTransition{.stagger = true}});
    RectangleControl* a = stack->add(square(50.0f));
    RectangleControl* b = stack->add(square(50.0f));
    host.show(std::move(stack));

    // ENTERPAGE: from 28 DIPs below, transparent.
    REQUIRE(a->transition_running());
    CHECK(a->transition_target().opacity == 0.0f);
    CHECK_THAT(a->transition_target().translate.y, WithinAbs(28.0f, 1e-4));
    // Staggered 35 ms apart, the first one already 35 ms late.
    host.run(60.0);
    CHECK(a->transition_target().opacity > 0.0f);
    CHECK(b->transition_target().opacity == 0.0f);
    host.run(700.0);
    CHECK_FALSE(a->transition_running());
    CHECK_FALSE(b->transition_running());
    CHECK(a->transition_target().is_identity());

    host.layout();
    CHECK_FALSE(a->transition_running());
}

TEST_CASE("RepositionTransition glides an element that layout moved", "[animation]")
{
    Host host;
    auto grid = std::make_unique<GridControl>();
    grid->add_row(GridLength::auto_());
    for (int i = 0; i < 3; ++i)
    {
        grid->add_column(GridLength::auto_());
    }
    grid->set_horizontal_alignment(Alignment::Leading);
    grid->set_vertical_alignment(Alignment::Leading);
    grid->add(square(75.0f), 0, 0);
    RectangleControl* middle = grid->add(square(75.0f), 0, 1);
    RectangleControl* right  = grid->add(square(75.0f), 0, 2);
    right->set_transitions({RepositionTransition{.stagger = false}});
    host.show(std::move(grid));
    CHECK_FALSE(right->transition_running());
    const float before = right->actual_offset().x;

    middle->set_visibility(Visibility::Collapsed);
    host.layout();
    // Relative to its parent: the window centers the grid, which shrank too.
    const float moved = before - right->actual_offset().x;
    CHECK(moved == 75.0f);
    // Drawn where it was, then gliding over 367 ms.
    REQUIRE(right->transition_running());
    CHECK_THAT(right->transition_target().translate.x, WithinAbs(moved, 1e-3));
    host.run(180.0);
    CHECK(right->transition_target().translate.x < moved);
    CHECK(right->transition_target().translate.x > 0.0f);
    host.run(200.0);
    CHECK_FALSE(right->transition_running());
}

TEST_CASE("run_transition samples its tracks and resets the target when done", "[animation]")
{
    Host              host;
    RectangleControl* rect = host.show(square(50.0f));
    int               done = 0;
    rect->run_transition(transition_storyboards::popup_in(Point{0.0f, 40.0f}), [&] { ++done; });
    CHECK(rect->transition_target().opacity == 0.0f);
    CHECK_THAT(rect->transition_target().translate.y, WithinAbs(40.0f, 1e-4));
    host.run(120.0); // SHOWPOPUP's fade: linear from 83 to 166 ms
    CHECK(rect->transition_target().opacity > 0.0f);
    CHECK(rect->transition_target().opacity < 1.0f);
    host.run(300.0);
    CHECK(done == 1);
    CHECK(rect->transition_target().is_identity());
}

TEST_CASE("the navigation storyboards match the transitions", "[animation]")
{
    using transition_storyboards::NavigationTrigger;
    // Entrance: the new page waits 150 ms, then rises 140 DIPs over 300.
    const TransitionStoryboard to = transition_storyboards::navigation(RisePageTransition{}, NavigationTrigger::NavigatingTo);
    CHECK(to.duration() == 450ms);
    // NoPageTransition: nothing.
    CHECK(transition_storyboards::navigation(NoPageTransition{}, NavigationTrigger::NavigatingTo).empty());
    // ZoomPageTransition in: 783 ms of scale.
    CHECK(transition_storyboards::navigation(ZoomPageTransition{}, NavigationTrigger::NavigatingTo).duration() == 783ms);
    // Slide from the right: the new page comes in from +200.
    const TransitionStoryboard slide = transition_storyboards::navigation(SlidePageTransition{PageSlideDirection::FromRight},
                                                                          NavigationTrigger::NavigatingTo);
    bool found = false;
    for (const TransitionTrack& t : slide.tracks)
    {
        if (t.channel == TransitionChannel::TranslateX)
        {
            CHECK_THAT(sample_key_frames(t.key_frames, 0.0f, 150ms), WithinAbs(200.0f, 1e-4));
            found = true;
        }
    }
    CHECK(found);
}

// ---- brush transitions ----

TEST_CASE("a BrushTransition blends a Border's background linearly over 150 ms", "[animation]")
{
    Host           host;
    auto           border_owner = std::make_unique<BorderControl>();
    BorderControl* border       = border_owner.get();
    border->set_width(50.0f);
    border->set_height(50.0f);
    border->set_background(colors::Black);
    border->set_background_transition(BrushTransition{});
    host.show(std::move(border_owner));

    const auto fill_of = [&]
    {
        TestRenderer r;
        border->render(r);
        const auto* fill = first_of<FillRoundedRect>(r);
        REQUIRE(fill != nullptr);
        return fill->color;
    };
    CHECK(fill_of() == colors::Black); // on screen
    border->set_background(colors::White);
    CHECK(fill_of() == colors::Black); // the blend starts from there
    host.run(70.0, 10.0);
    host.window.tick(5ms);
    CHECK_THAT(fill_of().r, WithinAbs(0.5f, 1e-3));
    host.run(100.0);
    CHECK(fill_of() == colors::White);
}

TEST_CASE("RequestedTheme draws a subtree in the other mode, and a theme resource follows it", "[animation]")
{
    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light}, OsThemeSnapshot{});
    REQUIRE(light.with_dark_mode);
    REQUIRE_FALSE(light.palette.dark);

    Host host;
    host.window.pin_theme(light);
    auto         owner = std::make_unique<GridControl>();
    GridControl* grid  = owner.get();
    grid->add_row(GridLength::dip(50.0f));
    grid->add_column(GridLength::dip(50.0f));
    grid->set_background([](const Theme& t) { return t.palette.solid_background_base; });
    grid->set_background_transition(BrushTransition{});
    host.show(std::move(owner));

    const auto fill_of = [&]
    {
        TestRenderer r;
        grid->render(r, host.window.theme());
        const auto* fill = first_of<FillRoundedRect>(r);
        REQUIRE(fill != nullptr);
        return fill->color;
    };
    const Color light_base = light.palette.solid_background_base;
    const Color dark_base  = light.with_dark_mode(true).palette.solid_background_base;
    CHECK(fill_of() == light_base);

    // Dark: the resource re-resolves, and the transition blends to it.
    grid->set_requested_theme(ElementTheme::Dark);
    CHECK(fill_of() == light_base);
    host.run(200.0);
    CHECK(fill_of() == dark_base);

    // The fixed-palette eras have no other mode: nothing changes.
    const Theme luna = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Luna},
                                     OsThemeSnapshot{});
    CHECK_FALSE(luna.with_dark_mode);
}

// ---- the animations switch ----

TEST_CASE("the animations switch follows the OS unless the request forces it", "[animation][theme]")
{
    const OsThemeSnapshot os_on{};
    OsThemeSnapshot       os_off;
    os_off.animations_enabled = false;

    const Theme normal = resolve_theme(ThemeRequest{}, os_on);
    CHECK(normal.animations_enabled);
    CHECK(normal.anim_fast.count() == 167);

    // System follows the OS; with it off, every duration controls read from the theme is zero.
    const Theme system_off = resolve_theme(ThemeRequest{}, os_off);
    CHECK_FALSE(system_off.animations_enabled);
    CHECK(system_off.anim_faster.count() == 0);
    CHECK(system_off.anim_fast.count() == 0);
    CHECK(system_off.anim_normal.count() == 0);
    CHECK(system_off.popup_motion.menu == ThemePopupAnimation::None);
    CHECK(system_off.popup_motion.open.count() == 0);
    CHECK(system_off.popup_motion.close.count() == 0);
    // ... and so does the other mode a RequestedTheme draws with.
    REQUIRE(system_off.with_dark_mode);
    CHECK_FALSE(system_off.with_dark_mode(true).animations_enabled);

    // On and Off win over the OS either way.
    const Theme forced_on = resolve_theme(ThemeRequest{.animations = AnimationPreference::On}, os_off);
    CHECK(forced_on.animations_enabled);
    CHECK(forced_on.anim_fast.count() == 167);
    CHECK_FALSE(resolve_theme(ThemeRequest{.animations = AnimationPreference::Off}, os_on).animations_enabled);

    // USER32's popup effects on Aero, too.
    const Theme aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero, .animations = AnimationPreference::Off},
                                     os_on);
    CHECK(aero.popup_motion.menu == ThemePopupAnimation::None);
    CHECK(aero.popup_motion.list == ThemePopupAnimation::None);

    // A window follows the OS as it reports it, and a pinned theme with the switch off is zeroed.
    Window window;
    seams(window).report_os_theme(os_off);
    CHECK_FALSE(window.theme().animations_enabled);
    window.set_theme(ThemeRequest{.animations = AnimationPreference::On});
    CHECK(window.theme().animations_enabled);
    Theme pinned              = Theme::light();
    pinned.animations_enabled = false;
    window.pin_theme(pinned);
    CHECK(window.theme().anim_fast.count() == 0);
}

TEST_CASE("with animations off, the visual layer lands on its end state", "[animation]")
{
    Host host;
    host.window.set_theme(ThemeRequest{.animations = AnimationPreference::Off});
    RectangleControl* rect = host.show(square(50.0f));

    SECTION("an implicit transition lands on the next tick")
    {
        rect->set_opacity_transition(ScalarTransition{});
        rect->set_opacity(0.5f);
        host.window.tick(10ms);
        CHECK(rect->current_opacity() == 0.5f);
        CHECK_FALSE(rect->is_animating(AnimatedProperty::Opacity));
    }
    SECTION("an animation completes and runs its completion handler")
    {
        int completed = 0;
        rect->start_animation(AnimatedProperty::TranslationX,
                              DoubleAnimation{.to = 200.0f, .duration = 500ms, .on_completed = [&] { ++completed; }});
        host.window.tick(10ms);
        CHECK(completed == 1);
        CHECK(rect->translation().x == 200.0f);
        CHECK_FALSE(rect->is_animating(AnimatedProperty::Translation));
    }
    SECTION("a spring settles without overshooting, however loose")
    {
        int settled = 0;
        rect->start_animation(AnimatedProperty::TranslationY,
                              SpringAnimation{.final_value   = Vector3{0.0f, 80.0f, 0.0f},
                                              .damping_ratio = 0.0f,
                                              .on_completed  = [&] { ++settled; }});
        host.window.tick(10ms);
        CHECK(settled == 1);
        CHECK(rect->translation().y == 80.0f);
    }
    SECTION("an animation that never ends keeps its own clock")
    {
        rect->start_animation(AnimatedProperty::Opacity,
                              DoubleAnimation{.from         = 0.0f,
                                              .to           = 1.0f,
                                              .duration     = 100ms,
                                              .repeat_count = std::numeric_limits<double>::infinity()});
        host.window.tick(50ms);
        CHECK(rect->is_animating(AnimatedProperty::Opacity));
        CHECK_THAT(rect->current_opacity(), WithinAbs(0.5f, 1e-4));
    }
    SECTION("a storyboard completes on the next tick")
    {
        int done = 0;
        rect->run_transition(transition_storyboards::content_out(), [&] { ++done; });
        host.window.tick(10ms);
        CHECK(done == 1);
        CHECK(rect->transition_target().is_identity());

        // Started while the window is dispatching, it is drawn at its end from the start.
        host.window.post(
            [&]
            {
                rect->run_transition(transition_storyboards::content_out(), [&] { ++done; });
                CHECK(rect->transition_target().opacity == 0.0f);
            });
        host.window.tick(0ms);
        CHECK(done == 2);
    }
}

TEST_CASE("with animations off, transitions do not play", "[animation]")
{
    Host host;
    host.window.set_theme(ThemeRequest{.animations = AnimationPreference::Off});
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->set_children_transitions({EntranceTransition{}, AddRemoveTransition{}});
    RectangleControl* first = stack->add(square(50.0f));
    stack->add(square(50.0f));
    StackControl* panel = host.show(std::move(stack));

    // No entrance on Load.
    CHECK_FALSE(first->transition_running());

    // A removed child leaves no ghost behind once the window has ticked.
    const auto commands = [&]
    {
        TestRenderer r;
        panel->render(r, host.window.theme());
        return r.count();
    };
    const std::size_t both = commands();
    REQUIRE(panel->remove(first));
    host.window.tick(10ms);
    host.layout();
    CHECK(commands() < both);
}

TEST_CASE("with animations off, a control's own tween lands on the next tick", "[animation]")
{
    // Aero's ToggleSwitch slides its push-button knob on a clock of its own.
    Host host;
    host.window.set_theme(ThemeRequest{.variant = ThemeVariant::Aero, .animations = AnimationPreference::Off});
    auto owned = std::make_unique<ToggleSwitchControl>();
    owned->set_horizontal_alignment(Alignment::Leading);
    owned->set_vertical_alignment(Alignment::Leading);
    ToggleSwitchControl* toggle = host.show(std::move(owned));
    const Theme&         theme  = host.window.theme();

    const auto knob_x = [&]
    {
        TestRenderer r;
        toggle->render(r, theme);
        for (const RenderCommand& c : r.commands)
        {
            if (const auto* g = std::get_if<FillRoundedRectGradient>(&c);
                g && g->brush == *theme.button_normal.background_gradient)
            {
                return g->bounds.x - toggle->bounds().x;
            }
        }
        FAIL("no knob drawn");
        return 0.0f;
    };
    host.window.tick(10ms);
    CHECK(knob_x() == 3.0f);
    toggle->set_checked(true);
    host.window.tick(10ms);
    CHECK(knob_x() == 21.0f);
}

TEST_CASE("with animations off, timers that are not animations keep their time", "[animation]")
{
    Host host;
    host.window.set_theme(ThemeRequest{.animations = AnimationPreference::Off});
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);

    auto            tip_owned = std::make_unique<ToolTipControl>();
    ToolTipControl* tip       = tip_owned.get();
    TextControl*    anchor    = tip->set_anchor(std::make_unique<TextControl>("Save"));
    tip->set_content(std::make_unique<TextControl>("Saves the document"));
    stack->add(std::move(tip_owned));
    TextBoxControl* box = stack->add(std::make_unique<TextBoxControl>("Hi"));
    host.show(std::move(stack));

    // The tooltip still waits out its 800 ms.
    seams(host.window).handle_pointer({anchor->bounds().center(), PointerButton::None, PointerAction::Move});
    host.window.tick(500ms);
    CHECK_FALSE(tip->is_open());
    host.window.tick(500ms);
    CHECK(tip->is_open());

    // The caret still blinks.
    ui::testing::set_focused(*box, true);
    REQUIRE(box->caret_visible());
    host.window.tick(Duration{TextBoxControl::CaretBlinkIntervalMs});
    CHECK_FALSE(box->caret_visible());
}
