// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Rating.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
KeyEvent key_down(Key k)
{
    return {k, KeyAction::Down};
}

Size measure_and_arrange(RatingControl& rating)
{
    HeadlessTextMeasurer m;
    const Size           desired = rating.measure({1000.0f, Unbounded}, m, Theme::default_light());
    rating.arrange({0, 0, desired.width, desired.height});
    return desired;
}

float star_x(int i, int max_rating, float width)
{
    return width * (static_cast<float>(i) + 0.5f) / static_cast<float>(max_rating);
}
} // namespace

TEST_CASE("A default-constructed RatingControl has 5 stars and no value", "[rating-control]")
{
    RatingControl rating;
    REQUIRE(rating.max_rating() == 5);
    REQUIRE_FALSE(rating.value().has_value());
    REQUIRE_FALSE(rating.placeholder_value().has_value());
    REQUIRE(rating.is_clear_enabled());
    REQUIRE(rating.initial_set_value() == 1);
    REQUIRE(rating.enabled());
    REQUIRE(rating.focusable());
}

TEST_CASE("Construction clamps max_rating to >= 1 and coerces the initial value", "[rating-control]")
{
    RatingControl zero_max{0, 3.0};
    REQUIRE(zero_max.max_rating() == 1);
    REQUIRE(zero_max.value() == 1.0); // clamped down to max_rating

    RatingControl negative{5, -2.0}; // up to 1 rounds up to 1.0
    REQUIRE(negative.value() == 1.0);

    RatingControl fractional{5, 0.4};
    REQUIRE(fractional.value() == 1.0);
}

TEST_CASE("set_value's coercion matches the class comment; no-op if unchanged", "[rating-control]")
{
    RatingControl rating{5};

    int                   calls = 0;
    std::optional<double> last;
    rating.on_value_changed(
        [&](std::optional<double> v)
        {
            ++calls;
            last = v;
        });

    rating.set_value(3.0);
    REQUIRE(rating.value() == 3.0);
    REQUIRE(calls == 1);
    REQUIRE(last == 3.0);

    rating.set_value(3.0); // unchanged: no-op
    REQUIRE(calls == 1);

    rating.set_value(10.0); // clamps down to max_rating
    REQUIRE(rating.value() == 5.0);
    REQUIRE(calls == 2);

    rating.set_value(0.7); // (0,1] rounds up to 1.0
    REQUIRE(rating.value() == 1.0);
    REQUIRE(calls == 3);

    rating.set_value(std::nullopt); // clears
    REQUIRE_FALSE(rating.value().has_value());
    REQUIRE_FALSE(last.has_value());
    REQUIRE(calls == 4);

    rating.set_value(std::nullopt); // already cleared: no-op
    REQUIRE(calls == 4);

    rating.set_value(-5.0); // up to 1 rounds up to 1.0
    REQUIRE(rating.value() == 1.0);
    REQUIRE(calls == 5);
}

TEST_CASE("set_placeholder_value preserves 0 and fractional values below 1, unlike set_value", "[rating-control]")
{
    RatingControl rating{5};

    rating.set_placeholder_value(0.0);
    REQUIRE(rating.placeholder_value() == 0.0);

    rating.set_placeholder_value(3.7);
    REQUIRE(rating.placeholder_value() == 3.7);

    rating.set_placeholder_value(10.0); // clamps down to max_rating
    REQUIRE(rating.placeholder_value() == 5.0);

    rating.set_placeholder_value(-1.0); // clamps up to 0
    REQUIRE(rating.placeholder_value() == 0.0);

    rating.set_placeholder_value(std::nullopt); // clears
    REQUIRE_FALSE(rating.placeholder_value().has_value());
}

TEST_CASE("set_max_rating re-clamps value() and placeholder_value(), firing on_value_changed only if that moves it",
          "[rating-control]")
{
    RatingControl rating{10, 8.0};
    rating.set_placeholder_value(6.0);

    int calls = 0;
    rating.on_value_changed([&](std::optional<double>) { ++calls; });

    rating.set_max_rating(9); // 8 still valid: no reclamp needed
    REQUIRE(calls == 0);
    REQUIRE(rating.value() == 8.0);

    rating.set_max_rating(5); // clamps value 8 -> 5, placeholder 6 -> 5
    REQUIRE(rating.value() == 5.0);
    REQUIRE(rating.placeholder_value() == 5.0);
    REQUIRE(calls == 1);
}

TEST_CASE("A click commits only on release, not on down or move (unlike Slider)", "[rating-control][input]")
{
    RatingControl rating{5};
    const Size    size = measure_and_arrange(rating);

    int calls = 0;
    rating.on_value_changed([&](std::optional<double>) { ++calls; });

    const Point third{star_x(2, 5, size.width), size.height * 0.5f};
    rating.handle_pointer({third, PointerButton::Left, PointerAction::Down});
    REQUIRE(calls == 0); // preview only, not yet committed
    REQUIRE_FALSE(rating.value().has_value());

    rating.handle_pointer({third, PointerButton::Left, PointerAction::Move});
    REQUIRE(calls == 0);

    rating.handle_pointer({third, PointerButton::Left, PointerAction::Up});
    REQUIRE(calls == 1);
    REQUIRE(rating.value() == 3.0); // star index 2 -> 3rd star -> rating 3
}

TEST_CASE("Dragging updates the preview past the control's own bounds; releasing off the left edge clears",
          "[rating-control][input]")
{
    RatingControl rating{5, 3.0}; // start with a committed rating so a clear is observable
    const Size    size = measure_and_arrange(rating);

    const Point start{star_x(3, 5, size.width), size.height * 0.5f};
    rating.handle_pointer({start, PointerButton::Left, PointerAction::Down});

    // The preview keeps updating through pointer capture after the pointer leaves.
    const Point off_left{-50.0f, size.height * 0.5f};
    rating.handle_pointer({off_left, PointerButton::Left, PointerAction::Move});
    REQUIRE(rating.value() == 3.0); // still not committed

    rating.handle_pointer({off_left, PointerButton::Left, PointerAction::Up});
    REQUIRE_FALSE(rating.value().has_value()); // released at/below 0 with is_clear_enabled(): clears
}

TEST_CASE("Re-picking the star that already represents the current rating clears it, but only via mouse",
          "[rating-control][input]")
{
    RatingControl rating{5, 3.0};
    const Size    size = measure_and_arrange(rating);

    const Point third{star_x(2, 5, size.width), size.height * 0.5f};
    rating.handle_pointer({third, PointerButton::Left, PointerAction::Down});
    rating.handle_pointer({third, PointerButton::Left, PointerAction::Up});
    REQUIRE_FALSE(rating.value().has_value()); // clicking the already-set 3rd star clears

    // is_clear_enabled(false): clicking the current rating instead floors at 1, never clears.
    RatingControl no_clear{5, 3.0};
    no_clear.set_clear_enabled(false);
    const Size  no_clear_size = measure_and_arrange(no_clear);
    const Point start{star_x(3, 5, no_clear_size.width), no_clear_size.height * 0.5f};
    const Point off_left{-50.0f, no_clear_size.height * 0.5f}; // dragged off-edge, released at/below 0
    no_clear.handle_pointer({start, PointerButton::Left, PointerAction::Down});
    no_clear.handle_pointer({off_left, PointerButton::Left, PointerAction::Move});
    no_clear.handle_pointer({off_left, PointerButton::Left, PointerAction::Up});
    REQUIRE(no_clear.value() == 1.0);
}

TEST_CASE("Arrow keys step the rating; Up/Right increase and Down/Left decrease, only while focused",
          "[rating-control][input]")
{
    RatingControl rating{5};

    rating.handle_key(key_down(Key::ArrowRight)); // not focused: ignored
    REQUIRE_FALSE(rating.value().has_value());

    ui::testing::set_focused(rating, true);
    rating.handle_key(key_down(Key::ArrowRight)); // unset -> initial_set_value()
    REQUIRE(rating.value() == 1.0);

    rating.handle_key(key_down(Key::ArrowRight));
    REQUIRE(rating.value() == 2.0);

    rating.handle_key(key_down(Key::ArrowLeft));
    REQUIRE(rating.value() == 1.0);

    rating.handle_key(key_down(Key::ArrowDown)); // Down decreases too; already at 1 -> clears
    REQUIRE_FALSE(rating.value().has_value());
}

TEST_CASE("Increasing via the keyboard at an already-maxed rating holds steady instead of clearing",
          "[rating-control][input]")
{
    RatingControl rating{5, 5.0};
    ui::testing::set_focused(rating, true);

    rating.handle_key(key_down(Key::ArrowRight));
    REQUIRE(rating.value() == 5.0); // held, not cleared, unlike a mouse re-pick at the max
}

TEST_CASE("A custom initial_set_value is what a keyboard step from unset lands on", "[rating-control]")
{
    RatingControl rating{10};
    rating.set_initial_set_value(4);
    ui::testing::set_focused(rating, true);

    rating.handle_key(key_down(Key::ArrowRight));
    REQUIRE(rating.value() == 4.0);
}

TEST_CASE("Home/End jump to 0 (clear) and max_rating() while focused", "[rating-control][input]")
{
    RatingControl rating{5, 3.0};
    ui::testing::set_focused(rating, true);

    rating.handle_key(key_down(Key::End));
    REQUIRE(rating.value() == 5.0);
    rating.handle_key(key_down(Key::Home));
    REQUIRE_FALSE(rating.value().has_value());
}

TEST_CASE("Disabled RatingControl ignores pointer and keyboard input and is not focusable", "[rating-control][state]")
{
    RatingControl rating{5, 3.0};
    const Size    size = measure_and_arrange(rating);
    ui::testing::set_focused(rating, true);
    rating.set_enabled(false);

    REQUIRE_FALSE(rating.focusable());

    const Point last{star_x(4, 5, size.width), size.height * 0.5f};
    rating.handle_pointer({last, PointerButton::Left, PointerAction::Down});
    rating.handle_pointer({last, PointerButton::Left, PointerAction::Up});
    rating.handle_key(key_down(Key::ArrowRight));

    REQUIRE(rating.value() == 3.0); // unchanged
}

TEST_CASE("Render draws one outline star per max_rating and one foreground star per filled star",
          "[rating-control][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    RatingControl rating{5, 3.0};
    measure_and_arrange(rating);

    TestRenderer r;
    rating.render(r, theme);
    REQUIRE(r.count_of<DrawTextRun>() == 5 + 3); // 5 outlines + 3 filled

    RatingControl empty{5};
    measure_and_arrange(empty);
    TestRenderer r2;
    empty.render(r2, theme);
    REQUIRE(r2.count_of<DrawTextRun>() == 5); // outlines only, nothing filled

    RatingControl fractional{5};
    fractional.set_value(3.4); // a programmatically-set fractional value
    measure_and_arrange(fractional);
    TestRenderer r3;
    fractional.render(r3, theme);
    REQUIRE(r3.count_of<DrawTextRun>() == 5 + 4); // 5 outlines + 3 full + 1 clipped partial
    REQUIRE(r3.count_of<PushClip>() == 1);
    REQUIRE(r3.count_of<PopClip>() == 1);
}

TEST_CASE("A read-only RatingControl ignores input but keeps its tab stop and its colors", "[rating-control][state]")
{
    RatingControl rating{5, 3.0};
    const Size    size = measure_and_arrange(rating);
    ui::testing::set_focused(rating, true);
    rating.set_read_only(true);

    // Unlike disabled: still reachable by Tab, as the handlers only skip input.
    REQUIRE(rating.focusable());

    const Point last{star_x(4, 5, size.width), size.height * 0.5f};
    rating.handle_pointer({last, PointerButton::Left, PointerAction::Move});
    rating.handle_pointer({last, PointerButton::Left, PointerAction::Down});
    rating.handle_pointer({last, PointerButton::Left, PointerAction::Up});
    rating.handle_key(key_down(Key::ArrowRight));
    rating.handle_key(key_down(Key::Home));
    CHECK(rating.value() == 3.0);
    CHECK_FALSE(rating.hovered());
    CHECK_FALSE(ui::automation_providers(rating).range_value->set_range_value(5.0));
    CHECK(ui::automation_providers(rating).range_value->range_read_only());

    // The app can still set it; read-only stops the user.
    rating.set_value(4.0);
    CHECK(rating.value() == 4.0);

    // Same draw as an ordinary rating of the same value, down to the colors; disabled would gray it.
    RatingControl plain{5, 4.0};
    measure_and_arrange(plain);
    TestRenderer read_only_draw;
    ui::testing::set_focused(rating, false);
    rating.render(read_only_draw, Theme::light());
    TestRenderer plain_draw;
    plain.render(plain_draw, Theme::light());
    CHECK(read_only_draw.commands == plain_draw.commands);
}

TEST_CASE("Turning read-only on drops a hover preview already showing", "[rating-control][state]")
{
    RatingControl rating{5};
    const Size    size = measure_and_arrange(rating);
    rating.handle_pointer({{star_x(3, 5, size.width), size.height * 0.5f}, PointerButton::None, PointerAction::Move});

    TestRenderer previewing;
    rating.render(previewing, Theme::light());
    REQUIRE(previewing.count_of<DrawTextRun>() == 5 + 4);

    rating.set_read_only(true);
    TestRenderer settled;
    rating.render(settled, Theme::light());
    CHECK(settled.count_of<DrawTextRun>() == 5);
}

TEST_CASE("A two-way value the rating coerces is written back, at build and when already coerced",
          "[rating][spec][binding]")
{
    Property<std::optional<double>> v{9.0};
    int                             handled = 0;
    ui::Control<ui::Rating>         rating;
    std::unique_ptr<Element>        node = ui::build(
        ui::Rating{.value            = ui::bind(v),
                   .max_rating       = 5,
                   .on_value_changed = [&](std::optional<double>) { ++handled; },
                   .control          = rating});
    CHECK(rating->value() == 5.0);
    CHECK(v.get() == 5.0);
    CHECK(handled == 0);

    v = 7.0; // already 5 on the control
    CHECK(v.get() == 5.0);
    v = 0.5; // coerced up to one star
    CHECK(rating->value() == 1.0);
    CHECK(v.get() == 1.0);
}

TEST_CASE("A Rating's header sits above the stars and names it", "[rating][spec]")
{
    HeadlessTextMeasurer     m;
    const Theme              theme = Theme::default_light();
    Property<std::string>    label{"Food"};
    ui::Control<ui::Rating>  rating;
    std::unique_ptr<Element> node = ui::build(ui::Rating{.header = ui::bind(label), .control = rating});
    const Size               with = node->measure({1000.0f, Unbounded}, m, theme);
    node->arrange({0.0f, 0.0f, with.width, with.height});
    CHECK(rating->automation_name() == "Food");
    CHECK(rating->bounds().top() > 0.0f); // the stars start below the header

    label = "Service"; // follows the binding, screen reader name included
    CHECK(rating->header() == "Service");
    CHECK(rating->automation_name() == "Service");

    label              = "";
    const Size without = node->measure({1000.0f, Unbounded}, m, theme);
    CHECK(without.height < with.height);
}
