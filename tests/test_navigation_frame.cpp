// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/RenderRecording.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/ConnectedAnimation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/dialogs/Popup.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/media/Shape.hpp"
#include "winui/ui/navigation/NavigationFrame.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Text.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{

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
        window.layout(Size{600.0f, 400.0f}, measurer);
    }
    void run(double ms, double step = 10.0)
    {
        for (double t = 0.0; t < ms - 1e-9; t += step)
        {
            window.tick(Duration{step});
            layout();
        }
    }
};

std::unique_ptr<Element> page(const char* text)
{
    return std::make_unique<TextControl>(text);
}

std::unique_ptr<RectangleControl> square(float x, float y, float size)
{
    auto r = std::make_unique<RectangleControl>();
    r->set_width(size);
    r->set_height(size);
    r->set_fill(Brush{colors::Black});
    r->set_horizontal_alignment(Alignment::Leading);
    r->set_vertical_alignment(Alignment::Leading);
    r->set_margin(Edges{.left = x, .top = y});
    return r;
}

} // namespace

// ---- NavigationFrame ----

TEST_CASE("NavigationFrame keeps a back stack and rebuilds pages from their factories", "[navigation_frame]")
{
    Host                    host;
    NavigationFrameControl* frame = host.show(std::make_unique<NavigationFrameControl>());
    int                     built = 0;
    CHECK(frame->navigate(
        [&]
        {
            ++built;
            return page("One");
        }));
    CHECK(frame->content() != nullptr);
    CHECK_FALSE(frame->can_go_back());
    CHECK(frame->navigate([] { return page("Two"); }));
    CHECK(frame->can_go_back());
    CHECK(frame->back_stack_depth() == 1);

    NavigationMode mode = NavigationMode::New;
    frame->on_navigated([&](const NavigationEventArgs& e) { mode = e.mode; });
    CHECK(frame->go_back());
    CHECK(mode == NavigationMode::Back);
    CHECK(built == 2); // not cached: built again
    CHECK(frame->can_go_forward());
    CHECK(frame->go_forward());
    CHECK(mode == NavigationMode::Forward);
    CHECK_FALSE(frame->go_forward());

    frame->go_back();
    frame->navigate([] { return page("Three"); });
    CHECK_FALSE(frame->can_go_forward());
}

TEST_CASE("NavigationFrame caches a page navigated away from with NavigationCacheMode::Enabled", "[navigation_frame]")
{
    Host                    host;
    NavigationFrameControl* frame = host.show(std::make_unique<NavigationFrameControl>());
    frame->navigate([] { return page("List"); }, std::nullopt, NavigationCacheMode::Enabled);
    Element* list = frame->content();
    frame->navigate([] { return page("Detail"); });
    frame->go_back();
    CHECK(frame->content() == list);
}

TEST_CASE("A NavigationTransition animates both pages; without one navigation is immediate", "[navigation_frame]")
{
    Host                    host;
    NavigationFrameControl* frame = host.show(std::make_unique<NavigationFrameControl>());
    frame->navigate([] { return page("One"); });
    CHECK_FALSE(frame->content()->transition_running());

    frame->set_content_transitions({NavigationTransition{}});
    frame->navigate([] { return page("Two"); });
    host.layout();
    REQUIRE(frame->leaving() != nullptr);
    CHECK(frame->content()->transition_target().opacity == 0.0f);
    CHECK_THAT(frame->content()->transition_target().translate.y, WithinAbs(140.0f, 1e-4));
    CHECK_FALSE(frame->content()->is_hit_test_visible());
    host.run(200.0);
    CHECK(frame->leaving() == nullptr);
    CHECK(frame->content()->transition_target().opacity == 1.0f);
    host.run(300.0);
    CHECK(frame->content()->transition_target().is_identity());
    CHECK(frame->content()->is_hit_test_visible());

    frame->navigate([] { return page("Three"); }, NoPageTransition{});
    CHECK(frame->leaving() == nullptr);
    CHECK_FALSE(frame->content()->transition_running());

    frame->navigate([] { return page("Four"); }, ZoomPageTransition{});
    CHECK_THAT(frame->content()->transition_target().scale_x, WithinAbs(0.94f, 1e-4));
}

// ---- ListView ----

TEST_CASE("AddRemoveTransition animates rows added to and removed from a ListView", "[navigation_frame]")
{
    Host             host;
    auto             owner = std::make_unique<ListViewControl>();
    ListViewControl* list  = owner.get();
    list->set_items({"Item 0", "Item 1", "Item 2"});
    list->set_item_transitions({AddRemoveTransition{}});
    list->set_height(200.0f);
    host.show(std::move(owner));

    list->insert_item(0, "New");
    host.layout();
    CHECK(list->items().front() == "New");
    TestRenderer r;
    list->render(r);
    bool found_transform = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* t = std::get_if<PushTransform>(&c))
        {
            if (t->offset.y < 0.0f)
            {
                found_transform = true;
            }
        }
    }
    CHECK(found_transform);
    host.run(900.0);

    list->set_selected_index(1);
    list->remove_item(0);
    CHECK(list->selected_index() == 0); // stayed on the same item
    host.layout();
    TestRenderer during;
    list->render(during);
    bool ghost = false;
    for (const RenderCommand& c : during.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t != nullptr && t->text == "New")
        {
            ghost = true;
        }
    }
    CHECK(ghost);
    host.run(700.0);
    TestRenderer after;
    list->render(after);
    for (const RenderCommand& c : after.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            CHECK(t->text != "New");
        }
    }
}

TEST_CASE("ContentTransition fades out the old rows and slides the new ones in", "[navigation_frame]")
{
    Host             host;
    auto             owner = std::make_unique<ListViewControl>();
    ListViewControl* list  = owner.get();
    list->set_items({"Item 0", "Item 1"});
    list->set_item_transitions({ContentTransition{}});
    host.show(std::move(owner));
    host.run(600.0);

    list->set_items({"Updated content 0", "Updated content 1"});
    host.layout();
    TestRenderer r;
    list->render(r);
    bool old_row = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t != nullptr && t->text == "Item 0")
        {
            old_row = true;
        }
    }
    CHECK(old_row);
    host.run(600.0);
    TestRenderer done;
    list->render(done);
    CHECK(std::none_of(done.commands.begin(),
                       done.commands.end(),
                       [](const RenderCommand& c)
                       {
                           const auto* t = std::get_if<DrawTextRun>(&c);
                           return t != nullptr && t->text == "Item 0";
                       }));
}

// ---- Popup ----

TEST_CASE("PopupTransition slides a popup's child in and keeps it up until it has faded out", "[navigation_frame]")
{
    Host           host;
    auto           stack = std::make_unique<StackControl>(Orientation::Vertical);
    PopupControl*  popup = stack->add(std::make_unique<PopupControl>());
    ButtonControl* child = popup->set_child(std::make_unique<ButtonControl>("Close"));
    popup->set_child_transitions({PopupTransition{}});
    host.show(std::move(stack));

    popup->set_open(true);
    host.layout();
    CHECK_THAT(child->transition_target().translate.y, WithinAbs(40.0f, 1e-4));
    CHECK(child->transition_target().opacity == 0.0f);
    host.run(400.0);
    CHECK(child->transition_target().is_identity());

    popup->set_open(false);
    CHECK(popup->closing());
    CHECK(ui::testing::popup_open(*popup));
    CHECK_FALSE(child->is_hit_test_visible());
    host.run(100.0);
    CHECK_FALSE(popup->closing());
    CHECK_FALSE(ui::testing::popup_open(*popup));
}

// ---- RenderRecording / ElementWatch ----

TEST_CASE("RenderRecording replays what it recorded, any number of times", "[navigation_frame]")
{
    ButtonControl   button{"Snapshot"};
    RenderRecording recording;
    button.render(recording);
    TestRenderer direct;
    button.render(direct);
    TestRenderer replayed;
    recording.replay(replayed);
    recording.replay(replayed);
    CHECK(recording.size() == direct.commands.size());
    CHECK(replayed.commands.size() == direct.commands.size() * 2);
    CHECK(replayed.commands.front() == direct.commands.front());
}

TEST_CASE("ElementWatch goes null with its element", "[navigation_frame]")
{
    auto         element = std::make_unique<ButtonControl>("Watched");
    ElementWatch watch{element.get()};
    CHECK(watch.get() == element.get());
    element.reset();
    CHECK(watch.get() == nullptr);

    auto other = std::make_unique<ButtonControl>("Other");
    {
        ElementWatch temporary{other.get()};
    }
    other.reset(); // must not touch the destroyed watch
    SUCCEED();
}

// ---- ConnectedAnimationService ----

TEST_CASE("A connected animation flies a snapshot from the source to the destination", "[navigation_frame]")
{
    Host              host;
    auto              stack  = std::make_unique<StackControl>(Orientation::Vertical);
    RectangleControl* source = stack->add(square(10.0f, 10.0f, 50.0f));
    RectangleControl* target = stack->add(square(200.0f, 20.0f, 100.0f));
    host.show(std::move(stack));
    ConnectedAnimationService& service = host.window.connected_animation_service();

    ConnectedAnimation& animation = service.prepare_to_animate("hero", *source);
    CHECK(service.animation("hero") == &animation);
    CHECK(animation.state() == ConnectedAnimationState::Prepared);
    int completed = 0;
    animation.on_completed([&] { ++completed; });
    animation.set_flight_path(ShortFlightPath{});
    CHECK(animation.try_start(*target));
    CHECK_FALSE(animation.try_start(*target)); // only once
    host.layout();
    CHECK(animation.state() == ConnectedAnimationState::Running);
    CHECK(target->transition_target().opacity == 0.0f);
    CHECK(animation.destination_rect() == target->bounds());

    TestRenderer mid;
    host.window.render(mid);
    std::size_t transforms = 0;
    for (const RenderCommand& c : mid.commands)
    {
        transforms += std::holds_alternative<PushTransform>(c) ? 1 : 0;
    }
    CHECK(transforms >= 1);

    host.run(250.0); // Direct: 200 ms
    CHECK(completed == 1);
    CHECK(service.animation("hero") == nullptr);
    CHECK(target->transition_target().is_identity());
    CHECK_FALSE(service.active());
}

TEST_CASE("A connected animation nobody starts times out after two seconds", "[navigation_frame]")
{
    Host              host;
    auto              stack  = std::make_unique<StackControl>(Orientation::Vertical);
    RectangleControl* source = stack->add(square(0.0f, 0.0f, 50.0f));
    host.show(std::move(stack));
    ConnectedAnimationService& service = host.window.connected_animation_service();
    service.prepare_to_animate("lost", *source);
    host.run(1900.0, 100.0);
    CHECK(service.animation("lost") != nullptr);
    host.run(200.0, 100.0);
    CHECK(service.animation("lost") == nullptr);
}

TEST_CASE("A connected animation whose destination is destroyed before layout is dropped", "[navigation_frame]")
{
    Host              host;
    auto              stack  = std::make_unique<StackControl>(Orientation::Vertical);
    RectangleControl* source = stack->add(square(0.0f, 0.0f, 50.0f));
    host.show(std::move(stack));
    ConnectedAnimationService& service     = host.window.connected_animation_service();
    auto                       destination = std::make_unique<RectangleControl>();
    service.prepare_to_animate("gone", *source).try_start(*destination);
    destination.reset();
    host.layout();
    CHECK(service.animation("gone") == nullptr);
}
