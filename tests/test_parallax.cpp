// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/scrolling/Parallax.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
// A 100x100 Scroll over 1000 DIPs of content (so 900 DIPs of scrolling), laid out and
// scrolled to `offset`.
std::unique_ptr<ScrollControl> make_source(float offset)
{
    HeadlessTextMeasurer measurer;
    auto                 scroll  = std::make_unique<ScrollControl>();
    auto                 content = std::make_unique<TextControl>("tall content");
    content->set_min_height(1000.0f);
    scroll->set_child(std::move(content));
    (void)scroll->measure({100.0f, 100.0f}, measurer, Theme::default_light());
    scroll->arrange({0, 0, 100.0f, 100.0f});
    scroll->scroll_to(scroll->horizontal_offset(), offset, {.animation_mode = ui::ScrollAnimationMode::Disabled});
    return scroll;
}

struct View
{
    HeadlessTextMeasurer measurer;
    ParallaxControl      view;
    ButtonControl*       child = nullptr;

    View()
    {
        child = view.set_child(std::make_unique<ButtonControl>("Content"));
    }
    void layout()
    {
        (void)view.measure({200.0f, 50.0f}, measurer, Theme::default_light());
        view.arrange({0, 20, 200, 50});
    }
    float shift_y() const
    {
        return child->current_translation().y;
    }
};
} // namespace

TEST_CASE("A new Parallax is not focusable and has no child or source", "[parallax-view]")
{
    ParallaxControl view;
    CHECK_FALSE(view.focusable());
    CHECK(view.child() == nullptr);
    CHECK(view.source() == nullptr);
    CHECK(view.vertical_source_offset_kind() == ParallaxSourceOffsetKind::Relative);
    CHECK(view.max_vertical_shift_ratio() == 1.0f);
    CHECK(view.is_vertical_shift_clamped());
}

TEST_CASE("The child is laid out taller by the shift, and wider by the same ratio when it stretches",
          "[parallax-view][layout]")
{
    View v;
    v.view.set_vertical_shift(100.0f);
    v.layout();
    // Arrange: height 50 + 100, and a Stretch child's width grown by (150 / its height).
    CHECK_THAT(v.child->bounds().height, WithinAbs(150.0f, 1e-3));
    CHECK(v.child->bounds().width > 200.0f);

    View plain;
    plain.layout();
    CHECK(plain.child->bounds().height <= 50.0f);
}

TEST_CASE("Without a source the child stays put", "[parallax-view][render]")
{
    View v;
    v.view.set_vertical_shift(100.0f);
    v.layout();
    CHECK(v.shift_y() == 0.0f);
}

TEST_CASE("render() clips the child to the view's own bounds", "[parallax-view][render]")
{
    View v;
    v.view.set_vertical_shift(100.0f);
    auto source = make_source(450.0f);
    v.view.set_source(source.get());
    v.layout();
    TestRenderer r;
    v.view.render(r);
    REQUIRE(std::holds_alternative<PushClip>(r.commands.front()));
    CHECK(std::get<PushClip>(r.commands.front()).bounds == Rect{0, 20, 200, 50});
    CHECK(std::holds_alternative<PopClip>(r.commands.back()));
}

TEST_CASE("Absolute offsets: clamped, 0 before the start, a ramp, -shift past the end", "[parallax-view]")
{
    View v;
    v.view.set_vertical_shift(100.0f);
    v.view.set_vertical_source_offset_kind(ParallaxSourceOffsetKind::Absolute);
    v.view.set_vertical_source_end_offset(200.0f);
    v.layout();

    auto at_start = make_source(0.0f);
    v.view.set_source(at_start.get());
    CHECK_THAT(v.shift_y(), WithinAbs(0.0f, 1e-3));
    auto mid = make_source(100.0f);
    v.view.set_source(mid.get());
    CHECK_THAT(v.shift_y(), WithinAbs(-50.0f, 1e-3));
    auto past = make_source(300.0f);
    v.view.set_source(past.get());
    CHECK_THAT(v.shift_y(), WithinAbs(-100.0f, 1e-3));

    v.view.set_vertical_shift(-100.0f);
    v.view.set_source(at_start.get());
    CHECK_THAT(v.shift_y(), WithinAbs(-100.0f, 1e-3));
    v.view.set_source(past.get());
    CHECK_THAT(v.shift_y(), WithinAbs(0.0f, 1e-3));
}

TEST_CASE("Absolute offsets unclamped extrapolate; the max ratio caps the rate", "[parallax-view]")
{
    View v;
    v.view.set_vertical_shift(100.0f);
    v.view.set_vertical_source_offset_kind(ParallaxSourceOffsetKind::Absolute);
    v.view.set_vertical_source_end_offset(50.0f);
    v.view.set_vertical_shift_clamped(false);
    v.layout();
    auto source = make_source(200.0f);
    v.view.set_source(source.get());
    CHECK_THAT(v.shift_y(), WithinAbs(-200.0f, 1e-3)); // -min(1, 100/50) * 200

    v.view.set_vertical_shift_clamped(true);
    v.view.set_vertical_source_end_offset(100.0f);
    v.view.set_max_vertical_shift_ratio(0.5f);
    CHECK_THAT(v.shift_y(), WithinAbs(-50.0f, 1e-3)); // ratio-capped, not shift-capped
}

TEST_CASE("Relative offsets outside a Scroll span its range widened by a viewport each side", "[parallax-view]")
{
    // A Scroll pulls a whole viewport (100) past either end:
    // start = -100, end = 900 + 100.
    View v;
    v.view.set_vertical_shift(500.0f);
    v.layout();
    auto top = make_source(0.0f);
    v.view.set_source(top.get());
    CHECK_THAT(v.shift_y(), WithinAbs(-(500.0f / 1100.0f) * 100.0f, 1e-3)); // -min(1, 500/1100) * (0 + 100)
    auto bottom = make_source(900.0f);
    v.view.set_source(bottom.get());
    CHECK_THAT(v.shift_y(), WithinAbs(-(500.0f / 1100.0f) * 1000.0f, 1e-2));
}

TEST_CASE("A ListView source pulls a tenth of its viewport past either end", "[parallax-view]")
{
    HeadlessTextMeasurer     measurer;
    ListViewControl          list;
    std::vector<std::string> items;
    items.reserve(30);
    for (int i = 0; i < 30; ++i)
    {
        items.push_back("Item");
    }
    list.set_items(items);
    list.set_item_height(40.0f);
    (void)list.measure({200.0f, 400.0f}, measurer, Theme::default_light());
    list.arrange({0, 0, 200, 400});
    REQUIRE(list.scroll_info().has_value());
    const Element::ScrollInfo info = *list.scroll_info();
    CHECK(info.extent.height == 1200.0f);
    CHECK_THAT(info.out_of_bounds_pan.height, WithinAbs(info.viewport.height * 0.1f, 1e-4));

    View v;
    v.view.set_vertical_shift(500.0f);
    v.layout();
    v.view.set_source(&list);
    // start = -pan, end = (1200 - viewport) + pan: at offset 0, -(500 / (end - start)) * pan.
    const float pan   = info.out_of_bounds_pan.height;
    const float range = (1200.0f - info.viewport.height) + 2.0f * pan;
    CHECK_THAT(v.shift_y(), WithinAbs(-(500.0f / range) * pan, 1e-3));
}

TEST_CASE("A source destroyed first stops the parallax instead of dangling", "[parallax-view]")
{
    View v;
    v.view.set_vertical_shift(100.0f);
    v.view.set_vertical_source_offset_kind(ParallaxSourceOffsetKind::Absolute);
    v.view.set_vertical_source_end_offset(100.0f);
    v.layout();
    auto source = make_source(50.0f);
    v.view.set_source(source.get());
    CHECK(v.shift_y() != 0.0f);
    source.reset();
    CHECK(v.view.source() == nullptr);
    CHECK(v.shift_y() == 0.0f);
}

TEST_CASE("The child keeps focus and pointer input, hit where it is drawn", "[parallax-view][input]")
{
    View v;
    v.view.set_vertical_shift(50.0f);
    v.view.set_vertical_source_offset_kind(ParallaxSourceOffsetKind::Absolute);
    v.view.set_vertical_source_end_offset(100.0f);
    int clicks = 0;
    v.child->on_click([&] { ++clicks; });
    v.layout();

    std::vector<Element*> focusables;
    v.view.collect_focusables(focusables);
    CHECK(std::find(focusables.begin(), focusables.end(), static_cast<Element*>(v.child)) != focusables.end());

    auto source = make_source(100.0f); // shifted up by the whole 50
    v.view.set_source(source.get());
    const Rect  laid = v.child->bounds();
    const Point target{laid.x + 20.0f, laid.y + 60.0f}; // inside the drawn button
    v.view.handle_pointer({Point{target.x, target.y - 50.0f}, PointerButton::Left, PointerAction::Down});
    v.view.handle_pointer({Point{target.x, target.y - 50.0f}, PointerButton::Left, PointerAction::Up});
    CHECK(clicks == 1);
}
