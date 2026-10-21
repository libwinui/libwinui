// SPDX-License-Identifier: MIT
// Compositor layers: the Window hands an element's continuous motion to the compositor, takes it
// back when the compositor cannot show it, and draws it itself where there is no compositor.
#include "render/Compositor.hpp"
#include "testing/TestCompositor.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/dialogs/ToolTip.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Spacer.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/status/InfoBar.hpp"
#include "winui/ui/status/ProgressBar.hpp"
#include "winui/ui/status/ProgressRing.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <chrono>
#include <memory>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;

namespace
{
constexpr Size Client{400.0f, 300.0f};

// What a host does for one frame: lay out, work out the damage (which places the layers), draw.
TestRenderer frame(HeadlessUI& ui)
{
    ui.layout(Client);
    (void)ui.window().take_damage();
    TestRenderer renderer;
    ui.window().render(renderer);
    return renderer;
}

template <typename T>
std::vector<T> all_of(const TestCompositor& compositor)
{
    std::vector<T> out;
    for (const CompositionCommand& command : compositor.commands())
    {
        if (const auto* c = std::get_if<T>(&command))
        {
            out.push_back(*c);
        }
    }
    return out;
}

// A window showing `element` at the top of a stack, with a compositor.
struct Scene
{
    HeadlessUI     ui;
    TestCompositor compositor;
    StackControl*  stack = nullptr;

    template <typename T>
    T* show(std::unique_ptr<T> element)
    {
        stack  = ui.window().set_content(std::make_unique<StackControl>(Orientation::Vertical));
        T* raw = stack->add(std::move(element));
        seams(ui.window()).set_compositor(&compositor);
        return raw;
    }
};

std::vector<DrawGeometry> arcs_in(const std::vector<RenderCommand>& commands)
{
    std::vector<DrawGeometry> arcs;
    for (const RenderCommand& command : commands)
    {
        if (const auto* arc = std::get_if<DrawGeometry>(&command))
        {
            arcs.push_back(*arc);
        }
    }
    return arcs;
}

Point arc_start(const TestRenderer& renderer)
{
    const std::vector<DrawGeometry> arcs = arcs_in(renderer.commands);
    REQUIRE(arcs.size() == 1);
    return arcs.front().geometry.figures.at(0).start;
}

// Ticks a few times and says whether any tick asked for a frame.
bool ticks_repaint(HeadlessUI& ui)
{
    bool repaint = false;
    for (int i = 0; i < 5; ++i)
    {
        repaint = ui.tick(16ms) || repaint;
    }
    return repaint;
}
} // namespace

TEST_CASE("LayerAnimation::value_at holds, interpolates and repeats", "[layers]")
{
    const LayerAnimation slide{.property   = LayerProperty::OffsetX,
                               .key_frames = {KeyFrame::linear(0ms, 0.0f), KeyFrame::linear(100ms, 10.0f)},
                               .duration   = 200ms};
    CHECK(slide.value_at(0ms) == 0.0f);
    CHECK(slide.value_at(50ms) == Catch::Approx(5.0f));
    CHECK(slide.value_at(150ms) == 10.0f); // holds after the last frame, to the end of the cycle
    CHECK(slide.value_at(250ms) == Catch::Approx(5.0f));

    LayerAnimation once = slide;
    once.repeat         = false;
    CHECK(once.value_at(250ms) == 10.0f);

    LayerAnimation later = slide;
    later.phase          = 50ms;
    CHECK(later.value_at(0ms) == Catch::Approx(5.0f));
}

TEST_CASE("An indeterminate ProgressRing turns on a compositor layer and leaves the frame alone",
          "[layers][progress-ring]")
{
    Scene                scene;
    ProgressRingControl* ring = scene.show(std::make_unique<ProgressRingControl>());

    const TestRenderer drawn = frame(scene.ui);
    REQUIRE(ui::testing::ElementAccess::layers_composed(*ring));
    CHECK(drawn.count_of<DrawGeometry>() == 0); // no track by default, and the arc is the layer's

    const auto syncs  = all_of<SyncLayer>(scene.compositor);
    const auto paints = all_of<PaintLayer>(scene.compositor);
    const auto anims  = all_of<AnimateLayer>(scene.compositor);
    REQUIRE(syncs.size() == 1);
    REQUIRE(paints.size() == 1);
    REQUIRE(anims.size() == 1);
    const Rect square = syncs[0].placement.bounds; // the ring's square, centered in its bounds
    CHECK(square.height == ring->bounds().height);
    CHECK(square.center().x == Catch::Approx(ring->bounds().center().x));
    CHECK(paints[0].id == syncs[0].id);
    CHECK(arcs_in(paints[0].commands).size() == 1); // the three-quarter arc

    const LayerAnimation& spin = anims[0].animation;
    CHECK(spin.property == LayerProperty::Rotation);
    CHECK(spin.repeat);
    CHECK(spin.cycle() == 1500ms);
    CHECK(spin.value_at(375ms) == Catch::Approx(90.0f));
    CHECK(spin.center.x == Catch::Approx(square.width * 0.5f));

    // Nothing changes: no new commands, no repaint, no ticks wanted.
    const std::size_t before = scene.compositor.count();
    (void)frame(scene.ui);
    CHECK(scene.compositor.count() == before);
    CHECK_FALSE(ticks_repaint(scene.ui));
    CHECK_FALSE(scene.ui.window().wants_ticks());
}

TEST_CASE("The layer's arc is the arc the frame draws at the start of a turn", "[layers][progress-ring]")
{
    Scene                scene;
    ProgressRingControl* ring = scene.show(std::make_unique<ProgressRingControl>());
    (void)frame(scene.ui);
    const auto paints = all_of<PaintLayer>(scene.compositor);
    REQUIRE(paints.size() == 1);
    const Rect square = all_of<SyncLayer>(scene.compositor).at(0).placement.bounds;

    // The same ring, drawn by the frame at phase 0.
    ProgressRingControl alone;
    alone.arrange(ring->bounds());
    TestRenderer frame_drawn;
    alone.render(frame_drawn);
    REQUIRE(frame_drawn.count_of<DrawGeometry>() == 1);
    const std::vector<DrawGeometry> layer_arcs = arcs_in(paints[0].commands);
    REQUIRE(layer_arcs.size() == 1);
    CHECK(layer_arcs.front() == std::get<DrawGeometry>(frame_drawn.commands.front()));
    // Drawn moved to the layer's top-left.
    const PushTransform* moved = nullptr;
    for (const RenderCommand& command : paints[0].commands)
    {
        moved = moved != nullptr ? moved : std::get_if<PushTransform>(&command);
    }
    REQUIRE(moved != nullptr);
    CHECK(moved->offset.x == -square.x);
    CHECK(moved->offset.y == -square.y);
}

TEST_CASE("Without a compositor that shows layers, the ring keeps turning on the UI thread", "[layers][progress-ring]")
{
    SECTION("no compositor")
    {
        HeadlessUI           ui;
        auto*                stack = ui.window().set_content(std::make_unique<StackControl>(Orientation::Vertical));
        ProgressRingControl* ring  = stack->add(std::make_unique<ProgressRingControl>());
        const TestRenderer   drawn = frame(ui);
        CHECK_FALSE(ui::testing::ElementAccess::layers_composed(*ring));
        CHECK(drawn.count_of<DrawGeometry>() == 1);
        CHECK(ui.tick(16ms));
    }
    SECTION("a compositor without layers")
    {
        Scene scene;
        scene.compositor.set_supports_layers(false);
        ProgressRingControl* ring  = scene.show(std::make_unique<ProgressRingControl>());
        const TestRenderer   drawn = frame(scene.ui);
        CHECK_FALSE(ui::testing::ElementAccess::layers_composed(*ring));
        CHECK(drawn.count_of<DrawGeometry>() == 1);
        CHECK(scene.compositor.count_of<SyncLayer>() == 0);
        CHECK(scene.ui.tick(16ms));
    }
    SECTION("animations off")
    {
        Scene                scene;
        ProgressRingControl* ring  = scene.show(std::make_unique<ProgressRingControl>());
        Theme                theme = Theme::light();
        theme.animations_enabled   = false;
        scene.ui.window().pin_theme(theme);
        const TestRenderer drawn = frame(scene.ui);
        CHECK_FALSE(ui::testing::ElementAccess::layers_composed(*ring));
        CHECK(drawn.count_of<DrawGeometry>() == 1);
        CHECK(scene.compositor.count_of<SyncLayer>() == 0);
    }
}

TEST_CASE("The ring's layer goes when it is inactive, collapsed or removed", "[layers][progress-ring]")
{
    Scene                scene;
    ProgressRingControl* ring = scene.show(std::make_unique<ProgressRingControl>());
    (void)frame(scene.ui);
    const VisualId layer = all_of<SyncLayer>(scene.compositor).at(0).id;

    const auto removed = [&] { return all_of<RemoveLayer>(scene.compositor).size(); };

    SECTION("inactive")
    {
        ring->set_active(false);
        (void)frame(scene.ui);
        REQUIRE(removed() == 1);
        CHECK(all_of<RemoveLayer>(scene.compositor)[0].id == layer);
        CHECK_FALSE(scene.ui.tick(16ms));

        ring->set_active(true); // back on, on a fresh layer
        (void)frame(scene.ui);
        CHECK(ui::testing::ElementAccess::layers_composed(*ring));
        CHECK(all_of<SyncLayer>(scene.compositor).size() == 2);
    }
    SECTION("determinate")
    {
        ring->set_indeterminate(false);
        (void)frame(scene.ui);
        CHECK(removed() == 1);
    }
    SECTION("collapsed")
    {
        ring->set_visibility(Visibility::Collapsed);
        (void)frame(scene.ui);
        CHECK(removed() == 1);
        CHECK_FALSE(ticks_repaint(scene.ui)); // a collapsed ring keeps still

        ring->set_visibility(Visibility::Visible);
        (void)frame(scene.ui);
        CHECK(all_of<SyncLayer>(scene.compositor).size() == 2);
    }
    SECTION("removed")
    {
        scene.stack->clear();
        (void)frame(scene.ui);
        CHECK(removed() == 1);
        (void)frame(scene.ui); // nothing left to remove
        CHECK(removed() == 1);
    }
}

TEST_CASE("A dialog over the ring hands the arc back to the frame, where it continues", "[layers][progress-ring]")
{
    Scene                scene;
    ProgressRingControl* ring = scene.show(std::make_unique<ProgressRingControl>());
    (void)frame(scene.ui);
    REQUIRE(ui::testing::ElementAccess::layers_composed(*ring));

    scene.compositor.advance(std::chrono::milliseconds{500});
    (void)scene.ui.window().show_modal(std::make_unique<TextControl>("Busy"));
    const TestRenderer drawn = frame(scene.ui);
    CHECK_FALSE(ui::testing::ElementAccess::layers_composed(*ring));
    CHECK(all_of<RemoveLayer>(scene.compositor).size() == 1);
    CHECK(drawn.count_of<DrawGeometry>() == 1);
    CHECK(scene.ui.tick(16ms)); // the UI thread turns it now

    // A third of a turn on: the compositor's 500 ms and the tick's 16.
    const TestRenderer turned = frame(scene.ui);
    const Point        first  = arc_start(turned);
    const Point        c      = ring->bounds().center();
    const float        angle  = std::atan2(first.y - c.y, first.x - c.x) * 180.0f / 3.14159265f;
    CHECK(angle == Catch::Approx(-90.0f + 360.0f * 516.0f / 1500.0f).margin(0.1));

    // Closed again: back on the compositor, from where the frame left it.
    scene.ui.window().close_modal();
    (void)frame(scene.ui);
    CHECK(ui::testing::ElementAccess::layers_composed(*ring));
    const auto anims = all_of<AnimateLayer>(scene.compositor);
    REQUIRE(anims.size() == 2);
    CHECK(anims[1].animation.phase.count() == Catch::Approx(516.0));
}

TEST_CASE("A ring scrolled out of view runs nowhere, and comes back when scrolled to", "[layers][progress-ring]")
{
    HeadlessUI     ui;
    TestCompositor compositor;
    auto*          view   = ui.window().set_content(std::make_unique<ScrollControl>());
    auto*          stack  = view->set_child(std::make_unique<StackControl>(Orientation::Vertical));
    auto*          spacer = stack->add(std::make_unique<SpacerControl>());
    spacer->set_height(1000.0f);
    ProgressRingControl* ring = stack->add(std::make_unique<ProgressRingControl>());
    seams(ui.window()).set_compositor(&compositor);

    (void)frame(ui);
    CHECK(compositor.count_of<SyncLayer>() == 0);
    CHECK_FALSE(ticks_repaint(ui));

    view->scroll_to(0.0f, 1000.0f, {.animation_mode = ScrollAnimationMode::Disabled});
    (void)frame(ui);
    CHECK(ui::testing::ElementAccess::layers_composed(*ring));
    REQUIRE(compositor.count_of<SyncLayer>() == 1);
    const detail::LayerPlacement placement = all_of<SyncLayer>(compositor)[0].placement;
    CHECK(placement.bounds.y < Client.height);
    REQUIRE(placement.clip.has_value());
    CHECK(placement.clip->bottom() <= Client.height);
}

namespace
{
// A square that tracks whether it is on screen and keeps what it heard.
class ScreenWatcher final : public Element
{
public:
    ScreenWatcher()
    {
        set_screen_tracking(true);
    }
    std::vector<bool> heard;
    void              on_screen_changed(bool on_screen) override
    {
        heard.push_back(on_screen);
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {40.0f, 40.0f};
    }
    void render_override(Renderer& renderer, const RenderContext&) const override
    {
        renderer.fill_rounded_rect(bounds(), 0.0f, colors::Black);
    }
};
} // namespace

// A hovered button's wash is a compositor visual over the whole frame, so a tooltip opening over
// the button would show the wash on top of the tip.
TEST_CASE("A popup over a hovered button takes the button's wash from the compositor into the frame", "[layers][button]")
{
    Scene          scene;
    auto           tip    = std::make_unique<ToolTipControl>();
    ButtonControl* button = tip->set_anchor(std::make_unique<ButtonControl>("Show a simple ToolTip."));
    button->set_margin(Edges{.top = 100.0f}); // room above, where the tip opens
    tip->set_content(std::make_unique<TextControl>("Simple ToolTip"));
    ToolTipControl* shown = scene.show(std::move(tip));
    (void)frame(scene.ui);

    const VisualId wash   = button->visual_id();
    const Point    center = button->bounds().center();
    seams(scene.ui.window()).handle_pointer({center, PointerButton::None, PointerAction::Move});
    (void)frame(scene.ui);
    const auto faded_in = all_of<AnimateOpacity>(scene.compositor);
    REQUIRE_FALSE(faded_in.empty());
    CHECK(faded_in.back().id == wash);
    CHECK(faded_in.back().to > 0.0f);
    CHECK(all_of<RemoveVisual>(scene.compositor).empty());

    const auto wash_drawn = [&](const TestRenderer& drawn)
    {
        for (const RenderCommand& command : drawn.commands)
        {
            const auto* fill = std::get_if<FillRoundedRect>(&command);
            if (fill != nullptr && fill->bounds == button->bounds() &&
                fill->color == colors::White.with_alpha(faded_in.back().to))
            {
                return true;
            }
        }
        return false;
    };

    // Opened from low on the button, the tip above the pointer reaches down over the button's top.
    shown->show_at(Point{center.x, button->bounds().bottom() - 4.0f});
    (void)frame(scene.ui); // the measuring pass finds the tip over the button
    const TestRenderer covered = frame(scene.ui);
    CHECK(detail::TreeAccess::visuals_covered(*button));
    const auto removed = all_of<RemoveVisual>(scene.compositor);
    REQUIRE(removed.size() == 1);
    CHECK(removed.front().id == wash);
    CHECK(wash_drawn(covered));

    shown->close();
    (void)frame(scene.ui);
    const TestRenderer uncovered = frame(scene.ui);
    CHECK_FALSE(detail::TreeAccess::visuals_covered(*button));
    CHECK_FALSE(wash_drawn(uncovered));
    const auto faded_again = all_of<AnimateOpacity>(scene.compositor);
    REQUIRE(faded_again.size() == faded_in.size() + 1);
    CHECK(faded_again.back().id == wash);
    CHECK(faded_again.back().from == 0.0f);
    CHECK(faded_again.back().to == faded_in.back().to);
}

TEST_CASE("A focus ring around a hovered button leaves its wash on the compositor", "[layers][button]")
{
    Scene          scene;
    ButtonControl* button = scene.show(std::make_unique<ButtonControl>("Focused"));
    (void)frame(scene.ui);
    seams(scene.ui.window()).handle_key({Key::Tab, KeyAction::Down}); // keyboard focus, so the ring shows
    REQUIRE(button->is_focused());
    seams(scene.ui.window()).handle_pointer({button->bounds().center(), PointerButton::None, PointerAction::Move});
    (void)frame(scene.ui);
    (void)frame(scene.ui);
    CHECK_FALSE(detail::TreeAccess::visuals_covered(*button));
    CHECK(all_of<RemoveVisual>(scene.compositor).empty());
}

TEST_CASE("An element tracking the screen hears when it leaves the view and comes back", "[layers][on-screen]")
{
    HeadlessUI ui;
    ui.show(Client);
    auto* view   = ui.window().set_content(std::make_unique<ScrollControl>());
    auto* stack  = view->set_child(std::make_unique<StackControl>(Orientation::Vertical));
    auto* spacer = stack->add(std::make_unique<SpacerControl>());
    spacer->set_height(1000.0f);
    ScreenWatcher* watcher = stack->add(std::make_unique<ScreenWatcher>());
    auto*          still   = stack->add(std::make_unique<SpacerControl>());
    REQUIRE(watcher->on_screen()); // no frame has said otherwise yet

    (void)frame(ui);
    CHECK_FALSE(watcher->on_screen());
    CHECK(watcher->heard == std::vector<bool>{false});
    CHECK(still->on_screen()); // an element that does not track is never told

    (void)frame(ui);
    CHECK(watcher->heard.size() == 1); // told of changes only

    view->scroll_to(0.0f, 1000.0f, {.animation_mode = ScrollAnimationMode::Disabled});
    (void)frame(ui);
    CHECK(watcher->on_screen());
    CHECK(watcher->heard == std::vector<bool>{false, true});

    watcher->set_visibility(Visibility::Collapsed); // not drawn at all
    (void)frame(ui);
    CHECK_FALSE(watcher->on_screen());
    CHECK(watcher->heard == std::vector<bool>{false, true, false});
}

TEST_CASE("An indeterminate ProgressBar slides its two bars on compositor layers", "[layers][progress-bar]")
{
    Scene               scene;
    ProgressBarControl* bar = scene.show(std::make_unique<ProgressBarControl>());
    bar->set_indeterminate(true);
    const TestRenderer drawn = frame(scene.ui);
    REQUIRE(ui::testing::ElementAccess::layers_composed(*bar));
    CHECK(drawn.count_of<FillRoundedRect>() == 0);
    CHECK_FALSE(ticks_repaint(scene.ui));

    const auto syncs = all_of<SyncLayer>(scene.compositor);
    const auto anims = all_of<AnimateLayer>(scene.compositor);
    REQUIRE(syncs.size() == 2);
    REQUIRE(anims.size() == 2);
    const Rect b = bar->bounds();
    for (const SyncLayer& sync : syncs)
    {
        REQUIRE(sync.placement.clip.has_value());
        CHECK(*sync.placement.clip == b);
        CHECK(sync.placement.bounds.x == b.x);
    }

    // The key frames put each bar where the frame draws it, at any moment of the cycle.
    ProgressBarControl fallback;
    fallback.set_indeterminate(true);
    fallback.arrange(b);
    double elapsed = 0.0;
    for (const double step : {0.0, 120.0, 400.0, 260.0, 500.0, 333.0, 600.0})
    {
        fallback.tick(Duration{step});
        elapsed += step;
        TestRenderer renderer;
        fallback.render(renderer);
        std::vector<Rect> bars;
        for (const RenderCommand& command : renderer.commands)
        {
            if (const auto* fill = std::get_if<FillRoundedRect>(&command))
            {
                bars.push_back(fill->bounds);
            }
        }
        REQUIRE(bars.size() == 2);
        for (std::size_t i = 0; i < 2; ++i)
        {
            CHECK(syncs[i].placement.bounds.width == Catch::Approx(bars[i].width));
            CHECK(b.x + anims[i].animation.value_at(Duration{elapsed}) == Catch::Approx(bars[i].x).margin(1e-3));
        }
    }

    SECTION("paused, the frame draws the frozen bar")
    {
        bar->set_show_paused(true);
        const TestRenderer paused = frame(scene.ui);
        CHECK_FALSE(ui::testing::ElementAccess::layers_composed(*bar));
        CHECK(all_of<RemoveLayer>(scene.compositor).size() == 2);
        CHECK(paused.count_of<FillRoundedRect>() == 1);
    }
}

TEST_CASE("A hovered button its parent stops drawing loses its wash, and gets it back once drawn and hovered",
          "[layers][button]")
{
    Scene           scene;
    InfoBarControl* bar = scene.show(
        std::make_unique<InfoBarControl>("Title", "Message", InfoBarSeverity::Informational, true));
    (void)frame(scene.ui);
    ButtonControl* close = nullptr;
    bar->for_each_child([&close](Element& child) { close = dynamic_cast<ButtonControl*>(&child); });
    REQUIRE(close != nullptr);

    const Point at = bar->close_button_bounds().center();
    seams(scene.ui.window()).handle_pointer({at, PointerButton::None, PointerAction::Move});
    (void)frame(scene.ui);
    REQUIRE_FALSE(all_of<AnimateOpacity>(scene.compositor).empty());
    REQUIRE(all_of<RemoveVisual>(scene.compositor).empty());

    // The click closes the bar under the pointer, which has not moved: the button is still hovered.
    seams(scene.ui.window()).handle_pointer({at, PointerButton::Left, PointerAction::Down});
    seams(scene.ui.window()).handle_pointer({at, PointerButton::Left, PointerAction::Up});
    REQUIRE_FALSE(bar->is_open());
    (void)frame(scene.ui);
    const auto removed = all_of<RemoveVisual>(scene.compositor);
    REQUIRE(removed.size() == 1);
    CHECK(removed.front().id == close->visual_id());

    bar->set_open(true);
    (void)frame(scene.ui);
    (void)frame(scene.ui);
    CHECK_FALSE(detail::TreeAccess::visuals_covered(*close));
    seams(scene.ui.window()).handle_pointer({at, PointerButton::None, PointerAction::Move});
    (void)frame(scene.ui);
    const auto faded = all_of<AnimateOpacity>(scene.compositor);
    REQUIRE_FALSE(faded.empty());
    CHECK(faded.back().id == close->visual_id());
    CHECK(faded.back().to > 0.0f);
}

TEST_CASE("A hovered button that is destroyed loses its wash", "[layers][button]")
{
    Scene          scene;
    ButtonControl* button = scene.show(std::make_unique<ButtonControl>("Remove me"));
    (void)frame(scene.ui);
    const VisualId wash = button->visual_id();
    seams(scene.ui.window()).handle_pointer({button->bounds().center(), PointerButton::None, PointerAction::Move});
    (void)frame(scene.ui);
    REQUIRE(all_of<RemoveVisual>(scene.compositor).empty());

    (void)scene.stack->remove(button);
    (void)frame(scene.ui);
    const auto removed = all_of<RemoveVisual>(scene.compositor);
    REQUIRE(removed.size() == 1);
    CHECK(removed.front().id == wash);
}
