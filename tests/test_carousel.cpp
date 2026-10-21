// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/ui/navigation/Carousel.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
std::unique_ptr<CarouselControl> make_flip_view(int items, Orientation orientation = Orientation::Horizontal)
{
    auto view = std::make_unique<CarouselControl>();
    view->set_orientation(orientation);
    for (int i = 0; i < items; ++i)
    {
        view->add_item(std::make_unique<TextControl>("Item " + std::to_string(i)));
    }
    HeadlessTextMeasurer m;
    (void)view->measure({400, 270}, m, Theme::default_light());
    view->arrange({0, 0, 400, 270});
    return view;
}

PointerEvent at(Point p, PointerAction action, PointerButton button = PointerButton::None)
{
    return {p, button, action};
}
} // namespace

TEST_CASE("Every item fills the view, laid out along the flip axis", "[flip-view][layout]")
{
    auto view = make_flip_view(3);
    REQUIRE(view->selected_index() == 0);
    REQUIRE(view->item(0)->bounds() == Rect{0, 0, 400, 270});
    REQUIRE(view->item(1)->bounds() == Rect{400, 0, 400, 270});

    auto vertical = make_flip_view(3, Orientation::Vertical);
    REQUIRE(vertical->item(1)->bounds() == Rect{0, 270, 400, 270});

    view->set_use_animations(false);
    view->set_selected_index(2);
    REQUIRE(view->item(2)->bounds() == Rect{0, 0, 400, 270});

    CarouselControl empty;
    REQUIRE(empty.selected_index() == std::nullopt);
    REQUIRE_FALSE(empty.focusable());
}

TEST_CASE("A one-item flip slides; a longer jump lands at once", "[flip-view][animation]")
{
    auto                       view = make_flip_view(4);
    std::optional<std::size_t> last;
    view->on_selection_changed([&](std::size_t index) { last = index; });

    view->set_selected_index(1);
    REQUIRE(last == 1);
    REQUIRE(view->item(1)->bounds().x == 400.0f); // not moved yet
    view->tick(100ms);
    REQUIRE(view->item(1)->bounds().x > 0.0f);
    REQUIRE(view->item(1)->bounds().x < 400.0f);
    view->tick(300ms);
    REQUIRE(view->item(1)->bounds().x == 0.0f);

    view->set_selected_index(3);
    REQUIRE(view->item(3)->bounds().x == 0.0f);
}

TEST_CASE("The buttons show on mouse movement, never past an end, and hide after three seconds", "[flip-view][buttons]")
{
    auto view = make_flip_view(3);
    view->set_use_animations(false);
    REQUIRE_FALSE(view->next_button_visible());

    view->handle_pointer(at({200, 135}, PointerAction::Move));
    REQUIRE(view->next_button_visible());
    REQUIRE_FALSE(view->previous_button_visible()); // nothing before the first item
    REQUIRE(view->next_button_bounds() == Rect{383, 116, 16, 38});

    view->tick(2900ms);
    REQUIRE(view->next_button_visible());
    view->tick(200ms);
    REQUIRE_FALSE(view->next_button_visible());

    // Over a button, they stay.
    view->handle_pointer(at({200, 135}, PointerAction::Move));
    view->handle_pointer(at(view->next_button_bounds().center(), PointerAction::Move));
    view->tick(5000ms);
    REQUIRE(view->next_button_visible());
}

TEST_CASE("A button flips on release over it", "[flip-view][buttons][input]")
{
    auto view = make_flip_view(3);
    view->set_use_animations(false);
    view->handle_pointer(at({200, 135}, PointerAction::Move));
    const Point next = view->next_button_bounds().center();
    view->handle_pointer(at(next, PointerAction::Down, PointerButton::Left));
    REQUIRE(view->selected_index() == 0);
    view->handle_pointer(at(next, PointerAction::Up, PointerButton::Left));
    REQUIRE(view->selected_index() == 1);

    view->handle_pointer(at(view->previous_button_bounds().center(), PointerAction::Move));
    view->handle_pointer(at(view->previous_button_bounds().center(), PointerAction::Down, PointerButton::Left));
    view->handle_pointer(at(view->previous_button_bounds().center(), PointerAction::Up, PointerButton::Left));
    REQUIRE(view->selected_index() == 0);
}

TEST_CASE("The wheel flips once per turn, swallowing a quick repeat in the same direction", "[flip-view][wheel]")
{
    auto view = make_flip_view(4);
    view->set_use_animations(false);
    const PointerEvent down{{200, 135}, PointerButton::None, PointerAction::Wheel, 48.0f};
    const PointerEvent up{{200, 135}, PointerButton::None, PointerAction::Wheel, -48.0f};

    REQUIRE(view->handle_wheel(down));
    REQUIRE(view->selected_index() == 1);
    view->tick(50ms);
    REQUIRE(view->handle_wheel(down)); // within 200 ms: swallowed
    REQUIRE(view->selected_index() == 1);
    view->tick(50ms);
    REQUIRE(view->handle_wheel(up)); // a change of direction flips at once
    REQUIRE(view->selected_index() == 0);

    // At an end the wheel is left unhandled, so it can chain to a parent.
    view->tick(500ms);
    REQUIRE_FALSE(view->handle_wheel(up));
}

TEST_CASE("Keys along the flip axis step, Home/End jump, PageUp/PageDown step", "[flip-view][keyboard]")
{
    auto view = make_flip_view(5);
    view->set_use_animations(false);
    ui::testing::set_focused(*view, true);
    REQUIRE_FALSE(view->handle_key({Key::ArrowDown, KeyAction::Down})); // across the axis: left to focus
    REQUIRE(view->selected_index() == 0);

    REQUIRE(view->handle_key({Key::ArrowRight, KeyAction::Down}));
    REQUIRE(view->selected_index() == 1);
    view->handle_key({Key::PageDown, KeyAction::Down});
    REQUIRE(view->selected_index() == 2);
    view->handle_key({Key::End, KeyAction::Down});
    REQUIRE(view->selected_index() == 4);
    view->handle_key({Key::ArrowRight, KeyAction::Down});
    REQUIRE(view->selected_index() == 4);
    view->handle_key({Key::Home, KeyAction::Down});
    REQUIRE(view->selected_index() == 0);
}

TEST_CASE("ui::Carousel builds its items and selection", "[flip-view][spec]")
{
    ui::Control<ui::Carousel> ref;
    auto                      node = ui::build(
        ui::Carousel{.selected_index = 1, .control = ref, .content = {ui::Text{.text = "one"}, ui::Text{.text = "two"}}});
    REQUIRE(ref != nullptr);
    REQUIRE(ref->item_count() == 2);
    REQUIRE(ref->selected_index() == 1);
}
