// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/VisualStates.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/collections/VirtualListControl.hpp"
#include "winui/ui/navigation/NavigationShell.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/scrolling/ScrollBarOverlay.hpp"
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
#include <cmath>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
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
} // namespace

TEST_CASE("Scroll asks for its content's size, up to the space offered", "[scroll]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ScrollControl view;
    view.set_child(make_tall_stack(50));
    const Size content = make_tall_stack(50)->measure({Unbounded, Unbounded}, m, theme);
    REQUIRE(content.width < 300.0f);
    REQUIRE(content.height > 200.0f);

    // Too tall: the offered height. Narrower than offered: the content's width.
    const Size desired = view.measure({300, 200}, m, theme);
    CHECK_THAT(desired.width, WithinAbs(content.width, 1e-3));
    CHECK_THAT(desired.height, WithinAbs(200.0, 1e-3));

    ScrollControl short_view;
    short_view.set_child(make_tall_stack(2));
    const Size short_content = make_tall_stack(2)->measure({Unbounded, Unbounded}, m, theme);
    const Size short_desired = short_view.measure({300, 200}, m, theme);
    CHECK_THAT(short_desired.height, WithinAbs(short_content.height, 1e-3));

    // Arrange still stretches the Scroll over its slot.
    short_view.arrange(Rect{0, 0, 300, 200});
    CHECK_THAT(short_view.bounds().height, WithinAbs(200.0, 1e-3));
}

TEST_CASE("Scroll clamps scroll offset to the content range", "[scroll]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ScrollControl view;
    StackControl* stack = view.set_child(make_tall_stack(20));

    (void)view.measure({300, 200}, m, theme);
    view.arrange({0, 0, 300, 200});

    view.scroll_to(view.horizontal_offset(), 1'000'000.0f, {.animation_mode = ui::ScrollAnimationMode::Disabled});
    REQUIRE(view.vertical_offset() > 0.0f);

    const auto& last = stack->children().back();
    REQUIRE(last->bounds().bottom() <= 200.5f);

    view.scroll_to(view.horizontal_offset(), -50.0f, {.animation_mode = ui::ScrollAnimationMode::Disabled});
    REQUIRE_THAT(view.vertical_offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Scroll clips render output between a PushClip/PopClip pair", "[scroll][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ScrollControl view;
    view.set_child(make_tall_stack(10));
    (void)view.measure({300, 100}, m, theme);
    view.arrange({0, 0, 300, 100});

    TestRenderer r;
    view.render(r, theme);

    REQUIRE(r.count_of<PushClip>() == 1);
    REQUIRE(r.count_of<PopClip>() == 1);
    REQUIRE(std::holds_alternative<PushClip>(r.commands.front()));
    REQUIRE(std::get<PushClip>(r.commands.front()).bounds == Rect{0, 0, 300, 100});
    // The overlay scrollbar's thumb paints after the clip pair, unclipped, on top of content.
    REQUIRE(r.commands.size() >= 2);
    REQUIRE(std::holds_alternative<PopClip>(r.commands[r.commands.size() - 2]));
    REQUIRE(std::holds_alternative<FillRoundedRect>(r.commands.back()));
}

TEST_CASE("Mouse wheel over a Scroll scrolls its content", "[scroll][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ScrollControl view;
    view.set_child(make_tall_stack(20));
    (void)view.measure({300, 100}, m, theme);
    view.arrange({0, 0, 300, 100});
    REQUIRE_THAT(view.vertical_offset(), WithinAbs(0.0, 1e-3));

    view.handle_pointer({{10, 10}, PointerButton::None, PointerAction::Wheel, 40.0f});
    REQUIRE_THAT(view.vertical_offset(), WithinAbs(40.0, 1e-3));

    view.handle_pointer({{10, 500}, PointerButton::None, PointerAction::Wheel, 40.0f});
    REQUIRE_THAT(view.vertical_offset(), WithinAbs(40.0, 1e-3));
}

namespace
{
// A Scroll re-arranges its child to full content height on every reflow, so a directly
// nested one loses its viewport after one outer scroll. Each test uses a fresh tree per turn.
struct NestedScrolls
{
    std::unique_ptr<ScrollControl> outer;
    ScrollControl*                 inner;
};

NestedScrolls make_nested_scroll_views(const TextMeasurer& m, const Theme& theme)
{
    NestedScrolls nested;
    nested.outer = std::make_unique<ScrollControl>();
    nested.inner = nested.outer->set_child(std::make_unique<ScrollControl>());
    nested.inner->set_child(make_tall_stack(20));

    (void)nested.outer->measure({300.0f, 300.0f}, m, theme);
    nested.outer->arrange({0, 0, 300, 300});
    (void)nested.inner->measure({300.0f, 100.0f}, m, theme);
    nested.inner->arrange({0, 20, 300, 100}); // narrower viewport, inside outer's own bounds
    return nested;
}
} // namespace

TEST_CASE("Wheel over a nested Scroll reaches it, not its ancestor", "[scroll][input][wheel-routing]")
{
    HeadlessTextMeasurer m;
    Theme                theme  = Theme::light();
    NestedScrolls        nested = make_nested_scroll_views(m, theme);

    REQUIRE(nested.outer->handle_wheel({{10, 30}, PointerButton::None, PointerAction::Wheel, 40.0f}));
    REQUIRE_THAT(nested.inner->vertical_offset(), WithinAbs(40.0, 1e-3));
    REQUIRE_THAT(nested.outer->vertical_offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Wheel outside a nested Scroll's bounds falls back to its ancestor", "[scroll][input][wheel-routing]")
{
    HeadlessTextMeasurer m;
    Theme                theme  = Theme::light();
    NestedScrolls        nested = make_nested_scroll_views(m, theme);

    // (10, 200) is inside the outer's bounds but outside the inner's {0,20,300,100}.
    REQUIRE(nested.outer->handle_wheel({{10, 200}, PointerButton::None, PointerAction::Wheel, 40.0f}));
    REQUIRE_THAT(nested.outer->vertical_offset(), WithinAbs(40.0, 1e-3));
    REQUIRE_THAT(nested.inner->vertical_offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("A press on content scrolled out of the viewport is not delivered", "[scroll][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ScrollControl view;
    StackControl* stack = view.set_child(make_tall_stack(20));
    (void)view.measure({300, 100}, m, theme);
    view.arrange({0, 0, 300, 100});

    auto* last   = static_cast<ButtonControl*>(stack->children().back().get());
    int   clicks = 0;
    last->on_click([&] { ++clicks; });

    REQUIRE(last->bounds().top() > 100.0f);

    const Point p = last->bounds().center();
    view.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    view.handle_pointer({p, PointerButton::Left, PointerAction::Up});
    REQUIRE(clicks == 0);
}

TEST_CASE("VirtualList realizes only the visible items out of a large item count", "[virtual-list]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    VirtualListControl list;
    list.set_item_count(100'000);
    list.set_item_height(20.0f);
    list.set_item_factory([](std::size_t i) { return std::make_unique<TextControl>("Item " + std::to_string(i)); });

    (void)list.measure({300, 105}, m, theme); // 105px viewport / 20px items -> ~6 visible
    list.arrange({0, 0, 300, 105});

    REQUIRE(list.visible_count() < 10);
    REQUIRE(list.visible_count() > 0);
    REQUIRE(list.first_visible_index() == 0);

    REQUIRE_THAT(list.visible_item(0)->bounds().top(), WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(list.visible_item(1)->bounds().top(), WithinAbs(20.0, 1e-3));
}

TEST_CASE("Scrolling a VirtualList swaps which items are realized", "[virtual-list][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    VirtualListControl list;
    list.set_item_count(1000);
    list.set_item_height(25.0f);
    list.set_item_factory([](std::size_t i) { return std::make_unique<TextControl>("Item " + std::to_string(i)); });
    (void)list.measure({300, 100}, m, theme);
    list.arrange({0, 0, 300, 100});
    REQUIRE(list.first_visible_index() == 0);

    list.handle_pointer({{10, 10}, PointerButton::None, PointerAction::Wheel, 25.0f * 7});
    REQUIRE(list.first_visible_index() == 7);

    list.handle_pointer({{10, 500}, PointerButton::None, PointerAction::Wheel, 1000.0f});
    REQUIRE(list.first_visible_index() == 7);
}

TEST_CASE("A row whose press reflows its VirtualList outlives its handler", "[virtual-list][input]")
{
    // A collapsing group header reflows the list from its own click handler. The rows being
    // dispatched to must survive that, and the rows realized by the reflow must not see the press.
    struct Log
    {
        std::vector<std::size_t> pressed;
        std::vector<std::size_t> destroyed;
    };
    class Row final : public Element
    {
    public:
        Row(std::size_t index, Log& log, VirtualListControl*& list) : index_(index), log_(log), list_(list)
        {
        }
        ~Row() override
        {
            log_.destroyed.push_back(index_);
        }

    protected:
        [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
        {
            return {};
        }
        void render_override(Renderer&, const RenderContext&) const override
        {
        }
        bool pointer_override(const PointerEvent& e) override
        {
            if (e.action != PointerAction::Down)
            {
                return false;
            }
            log_.pressed.push_back(index_);
            if (index_ == 0)
            {
                list_->set_scroll_offset(25.0f * 7);
                log_.pressed.push_back(index_ + 1000); // this row is still alive here
            }
            return false;
        }


    private:
        std::size_t          index_;
        Log&                 log_;
        VirtualListControl*& list_;
    };

    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    Log                  log;
    VirtualListControl   list;
    VirtualListControl*  self = &list;
    list.set_item_count(1000);
    list.set_item_height(25.0f);
    list.set_item_factory([&](std::size_t i) { return std::make_unique<Row>(i, log, self); });
    (void)list.measure({300, 100}, m, theme);
    list.arrange({0, 0, 300, 100});
    log.destroyed.clear();

    list.handle_pointer({{10, 10}, PointerButton::Left, PointerAction::Down});

    CHECK(log.pressed == std::vector<std::size_t>{0, 1000});
    CHECK(list.first_visible_index() == 7);
    CHECK(std::ranges::find(log.destroyed, std::size_t{0}) != log.destroyed.end()); // retired once the press unwound
}

TEST_CASE("Scrolling a VirtualList past the focused item never leaves a dangling focus", "[virtual-list][input][focus]")
{
    // VirtualList's wheel handler destroys item Elements outside Window::layout(). A focused one
    // must not leave Window::focused_ dangling, or the next WM_KILLFOCUS blur() crashes.
    HeadlessTextMeasurer m;

    auto                list_owned = std::make_unique<VirtualListControl>();
    VirtualListControl* list       = list_owned.get();
    list->set_item_count(1000);
    list->set_item_height(25.0f);
    list->set_item_factory([](std::size_t i) { return std::make_unique<ButtonControl>("Item " + std::to_string(i)); });

    Window window;
    window.set_content(std::move(list_owned));
    window.layout({300, 100}, m);

    window.focus_content();
    REQUIRE(window.focused() != nullptr);
    REQUIRE(list->visible_count() > 0);
    REQUIRE(window.focused() == list->visible_item(0));

    ui::testing::seams(window).handle_pointer({{10, 10}, PointerButton::None, PointerAction::Wheel, 25.0f * 50});

    if (Element* focused = window.focused())
    {
        bool still_live = false;
        for (std::size_t i = 0; i < list->visible_count(); ++i)
        {
            if (list->visible_item(i) == focused)
            {
                still_live = true;
                break;
            }
        }
        REQUIRE(still_live);
    }

    window.blur();
    REQUIRE(window.focused() == nullptr);
}

namespace
{
// A row as tall as it is told, whatever room it is offered.
class TallRow final : public Element
{
public:
    explicit TallRow(float height) : height_(height)
    {
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
    {
        return {std::isfinite(available.width) ? available.width : 0.0f, height_};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }


private:
    float height_;
};
} // namespace

TEST_CASE("A VirtualList can size each row to its content, at least item_height", "[virtual-list]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    VirtualListControl list;
    list.set_item_count(1000);
    list.set_item_height(20.0f);
    list.set_size_items_to_content(true);
    list.set_item_factory([](std::size_t i) { return std::make_unique<TallRow>(i % 3 == 0 ? 60.0f : 10.0f); });
    (void)list.measure({300, 100}, m, theme);
    list.arrange({0, 0, 300, 100});

    // 60, then 20 for a row that asks for 10: three rows fill the 100 DIPs.
    REQUIRE(list.visible_count() == 3);
    REQUIRE(list.visible_item(0)->bounds() == Rect{0, 0, 300, 60});
    REQUIRE(list.visible_item(1)->bounds() == Rect{0, 60, 300, 20});
    REQUIRE(list.visible_item(2)->bounds() == Rect{0, 80, 300, 20});
    REQUIRE_THAT(list.item_top(3), WithinAbs(100.0, 1e-3));
    REQUIRE_THAT(list.item_extent(0), WithinAbs(60.0, 1e-3));
    REQUIRE_THAT(list.item_extent(500), WithinAbs(20.0, 1e-3)); // not seen yet: the estimate
    REQUIRE_THAT(list.content_height(), WithinAbs(100.0 + 997.0 * 20.0, 1e-3));
    REQUIRE(list.index_at(85.0f) == 2);
    REQUIRE(list.index_at(1e9f) == 1000);

    // Scrolled 70 DIPs down, row 1 starts the view, 10 DIPs above it.
    list.set_scroll_offset(70.0f);
    REQUIRE(list.first_visible_index() == 1);
    REQUIRE_THAT(list.visible_item(0)->bounds().top(), WithinAbs(-10.0, 1e-3));
    // Row 3 was measured on the way: 60, not the estimate.
    REQUIRE_THAT(list.item_extent(3), WithinAbs(60.0, 1e-3));

    // Off again, every row is item_height() tall.
    list.set_size_items_to_content(false);
    REQUIRE_THAT(list.content_height(), WithinAbs(1000.0 * 20.0, 1e-3));
}

TEST_CASE("A VirtualList measures its unbounded desired height as item_count * item_height", "[virtual-list]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    VirtualListControl list;
    list.set_item_count(500);
    list.set_item_height(24.0f);

    const Size desired = list.measure({300, Unbounded}, m, theme); // unbounded height
    REQUIRE_THAT(desired.height, WithinAbs(500.0 * 24.0, 1e-3));
}

TEST_CASE("ScrollBarOverlay reports scrollable only when content exceeds the viewport", "[scroll][scrollbar]")
{
    const Rect viewport{0, 0, 300, 100};
    REQUIRE_FALSE(ScrollBarOverlay::is_scrollable(viewport, 100.0f));
    REQUIRE_FALSE(ScrollBarOverlay::is_scrollable(viewport, 50.0f));
    REQUIRE(ScrollBarOverlay::is_scrollable(viewport, 101.0f));
}

TEST_CASE("ScrollBarOverlay thumb hugs the edge, clamps to a minimum length, and tracks offset", "[scroll][scrollbar]")
{
    ScrollBarOverlay bar;
    const Rect       viewport{0, 0, 300, 200};

    // Content is 10x the viewport: a proportional thumb would be sized off the track between
    // the two step buttons (200 - 2*12 = 176px), clamped up to the 30px minimum instead.
    const Rect thumb_at_top = bar.thumb_bounds(viewport, 2000.0f, 0.0f);
    REQUIRE_THAT(thumb_at_top.height, WithinAbs(30.0, 1e-3));
    // The track starts below the always-reserved, ScrollBarSize-tall up button.
    REQUIRE_THAT(thumb_at_top.top(), WithinAbs(12.0, 1e-3));
    // Collapsed (not hovered/dragging): thin (2px) and flush with the viewport's edge.
    REQUIRE_THAT(thumb_at_top.width, WithinAbs(2.0, 1e-3));
    REQUIRE_THAT(thumb_at_top.right(), WithinAbs(300.0, 1e-3));

    // At the maximum offset (2000 - 200), the thumb's bottom sits at the down button's top
    // edge (200 - 12), not the viewport's own bottom.
    const Rect thumb_at_max = bar.thumb_bounds(viewport, 2000.0f, 1800.0f);
    REQUIRE_THAT(thumb_at_max.bottom(), WithinAbs(188.0, 1e-3));
}

TEST_CASE("ScrollBarOverlay thickens on hover but stays flush with the edge, collapses when the pointer leaves",
          "[scroll][scrollbar]")
{
    ScrollBarOverlay bar;
    const Rect       viewport{0, 0, 300, 200};
    REQUIRE_FALSE(bar.expanded());

    float offset = 0.0f;
    REQUIRE(bar.handle_pointer({{296, 10}, PointerButton::None, PointerAction::Move}, viewport, 2000.0f, offset));
    REQUIRE(bar.expanded());

    const Rect thumb = bar.thumb_bounds(viewport, 2000.0f, 0.0f);
    REQUIRE_THAT(thumb.width, WithinAbs(12.0, 1e-3));
    // No inset, or the thumb would misalign against the step buttons.
    REQUIRE_THAT(thumb.right(), WithinAbs(300.0, 1e-3));

    bar.handle_pointer({{10, 10}, PointerButton::None, PointerAction::Move}, viewport, 2000.0f, offset);
    REQUIRE_FALSE(bar.expanded());
}

TEST_CASE("ScrollBarOverlay dragging the thumb scrolls proportionally to the drag distance",
          "[scroll][scrollbar][input]")
{
    ScrollBarOverlay bar;
    const Rect       viewport{0, 0, 300, 200};
    const float      content_length = 2000.0f; // max offset = 1800

    float       offset = 0.0f;
    const Rect  thumb  = bar.thumb_bounds(viewport, content_length, offset); // 30px tall, anchored below the up button
    const Point on_thumb = thumb.center();

    REQUIRE(bar.handle_pointer({on_thumb, PointerButton::Left, PointerAction::Down}, viewport, content_length, offset));

    // The track excludes both step buttons, so dragging halfway down it scrolls halfway to max.
    const float track = viewport.height - 2.0f * thumb.top() - thumb.height;
    const Point dragged{on_thumb.x, on_thumb.y + track / 2.0f};
    REQUIRE(bar.handle_pointer({dragged, PointerButton::None, PointerAction::Move}, viewport, content_length, offset));
    REQUIRE_THAT(offset, WithinAbs(900.0, 1.0));

    REQUIRE(bar.handle_pointer({dragged, PointerButton::None, PointerAction::Up}, viewport, content_length, offset));
}

TEST_CASE("ScrollBarOverlay clicking the track pages toward the click, away from the thumb",
          "[scroll][scrollbar][input]")
{
    ScrollBarOverlay bar;
    const Rect       viewport{0, 0, 300, 200};
    const float      content_length = 2000.0f;

    float      offset = 500.0f;
    const Rect thumb  = bar.thumb_bounds(viewport, content_length, offset);
    REQUIRE(thumb.top() > 0.0f); // sanity: the thumb has moved down from the top

    // Click below the thumb pages toward Maximum by one viewport height.
    const Point below_thumb{thumb.center().x, thumb.bottom() + 5.0f};
    REQUIRE(bar.handle_pointer({below_thumb, PointerButton::Left, PointerAction::Down}, viewport, content_length, offset));
    REQUIRE_THAT(offset, WithinAbs(700.0, 1e-3));
}

TEST_CASE("ScrollBarOverlay's up/down step buttons scroll by a fixed step and clamp at the ends",
          "[scroll][scrollbar][input]")
{
    ScrollBarOverlay bar;
    const Rect       viewport{0, 0, 300, 200};
    const float      content_length = 2000.0f;                // max offset = 1800
    const float      step           = viewport.height / 8.0f; // this class's own default step ratio

    const Rect up   = bar.up_button_bounds(viewport);
    const Rect down = bar.down_button_bounds(viewport);
    REQUIRE_THAT(up.top(), WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(up.height, WithinAbs(12.0, 1e-3)); // ScrollBarSize
    REQUIRE_THAT(down.bottom(), WithinAbs(200.0, 1e-3));
    REQUIRE_THAT(down.height, WithinAbs(12.0, 1e-3));

    float offset = 500.0f;
    REQUIRE(bar.handle_pointer({up.center(), PointerButton::Left, PointerAction::Down}, viewport, content_length, offset));
    REQUIRE_THAT(offset, WithinAbs(500.0 - step, 1e-3));

    REQUIRE(bar.handle_pointer({down.center(), PointerButton::Left, PointerAction::Down}, viewport, content_length, offset));
    REQUIRE_THAT(offset, WithinAbs(500.0, 1e-3)); // back to where it started

    // Clamped at the top: a step larger than the remaining distance to zero doesn't go negative.
    offset = 10.0f;
    REQUIRE(bar.handle_pointer({up.center(), PointerButton::Left, PointerAction::Down}, viewport, content_length, offset));
    REQUIRE_THAT(offset, WithinAbs(0.0, 1e-3));

    // Clamped at the bottom: doesn't overshoot Maximum either.
    offset = 1790.0f;
    REQUIRE(bar.handle_pointer({down.center(), PointerButton::Left, PointerAction::Down}, viewport, content_length, offset));
    REQUIRE_THAT(offset, WithinAbs(1800.0, 1e-3));
}

TEST_CASE("ScrollBarOverlay draws its step buttons only once expanded, using ScrollUp/ScrollDown glyphs",
          "[scroll][scrollbar][render]")
{
    ScrollBarOverlay bar;
    Theme            theme = Theme::light();
    const Rect       viewport{0, 0, 300, 200};
    const float      content_length = 2000.0f;
    constexpr float  Offset         = 0.0f;

    {
        // Collapsed: only the thumb paints, no track, no button icons.
        TestRenderer r;
        bar.render(r, theme, viewport, content_length, Offset);
        REQUIRE(r.count_of<DrawTextRun>() == 0);
    }

    float offset_unused = 0.0f;
    bar.handle_pointer({{296, 100}, PointerButton::None, PointerAction::Move}, viewport, content_length, offset_unused);
    REQUIRE_FALSE(bar.update(0.6s)); // past 0.4 delay + 0.167 duration: settled, fully expanded

    {
        TestRenderer r;
        bar.render(r, theme, viewport, content_length, Offset);
        REQUIRE(r.count_of<DrawTextRun>() == 2); // ScrollUp + ScrollDown glyphs

        // The arrow uses theme.surfaces.scrollbar_thumb, and fading scales its alpha instead of replacing it.
        bool found_glyph = false;
        for (const auto& command : r.commands)
        {
            if (std::holds_alternative<DrawTextRun>(command))
            {
                const auto& run = std::get<DrawTextRun>(command);
                REQUIRE(run.color.r == theme.surfaces.scrollbar_thumb.r);
                REQUIRE(run.color.g == theme.surfaces.scrollbar_thumb.g);
                REQUIRE(run.color.b == theme.surfaces.scrollbar_thumb.b);
                REQUIRE_THAT(run.color.a, WithinAbs(theme.surfaces.scrollbar_thumb.a, 1e-3));
                found_glyph = true;
            }
        }
        REQUIRE(found_glyph);
    }
}

TEST_CASE("ScrollBarOverlay eases the thumb between collapsed and expanded over 167ms, after a delay",
          "[scroll][scrollbar][animation]")
{
    ScrollBarOverlay bar;
    Theme            theme = Theme::light();
    const Rect       viewport{0, 0, 300, 200};
    const float      content_length = 2000.0f;
    constexpr float  Offset         = 0.0f;

    auto render_thumb = [&]
    {
        TestRenderer r;
        bar.render(r, theme, viewport, content_length, Offset);
        REQUIRE(r.count_of<FillRoundedRect>() >= 1);
        return std::get<FillRoundedRect>(r.commands.back()); // thumb always paints last
    };

    REQUIRE_THAT(render_thumb().bounds.width, WithinAbs(2.0, 1e-3));

    float offset_unused = 0.0f;
    REQUIRE(
        bar.handle_pointer({{296, 10}, PointerButton::None, PointerAction::Move}, viewport, content_length, offset_unused));
    REQUIRE(bar.expanded());

    // Still within the 400ms begin-delay: no visual movement yet.
    REQUIRE(bar.update(0.2s));
    REQUIRE_THAT(render_thumb().bounds.width, WithinAbs(2.0, 1e-3));

    // Partway through the 167ms ease: strictly between collapsed and expanded.
    REQUIRE(bar.update(0.25s)); // state_time_ = 0.45s -> 0.05s into the expand
    const float mid_width = render_thumb().bounds.width;
    REQUIRE(mid_width > 2.0f);
    REQUIRE(mid_width < 12.0f);

    // Past begin-delay + duration (0.4 + 0.167): settled at fully expanded, no more redraws needed.
    REQUIRE_FALSE(bar.update(0.5s));
    const FillRoundedRect expanded_thumb = render_thumb();
    REQUIRE_THAT(expanded_thumb.bounds.width, WithinAbs(12.0, 1e-3));
    REQUIRE_THAT(expanded_thumb.radius, WithinAbs(3.0, 1e-3)); // the scroll bar's corner radius

    // Leaving collapses back down the same way, after its own (500ms) delay.
    bar.handle_pointer({{10, 10}, PointerButton::None, PointerAction::Move}, viewport, content_length, offset_unused);
    REQUIRE_FALSE(bar.expanded());
    REQUIRE(bar.update(0.3s)); // still inside the 500ms contract delay: no movement yet
    REQUIRE_THAT(render_thumb().bounds.width, WithinAbs(12.0, 1e-3));
    REQUIRE_FALSE(bar.update(0.4s)); // 0.3+0.4 = 0.7s > 0.5s delay + 0.167s duration: settled
    REQUIRE_THAT(render_thumb().bounds.width, WithinAbs(2.0, 1e-3));
}

TEST_CASE("ScrollBarOverlay fades to fully hidden after 2s idle, and reappears the instant activity resumes",
          "[scroll][scrollbar][animation]")
{
    ScrollBarOverlay bar;
    Theme            theme = Theme::light();
    const Rect       viewport{0, 0, 300, 200};
    const float      content_length = 2000.0f;
    constexpr float  Offset         = 0.0f;

    auto thumb_alpha = [&]
    {
        TestRenderer r;
        bar.render(r, theme, viewport, content_length, Offset);
        return std::get<FillRoundedRect>(r.commands.back()).color.a;
    };

    const float full_alpha = thumb_alpha();
    REQUIRE(full_alpha > 0.0f);

    // Just short of the 2s idle-hide delay: still fully visible, nothing animating.
    REQUIRE_FALSE(bar.update(1.9s));
    REQUIRE_THAT(thumb_alpha(), WithinAbs(static_cast<double>(full_alpha), 1e-3));
    REQUIRE_FALSE(bar.update(0.05s)); // 1.95s idle: still short of the delay
    REQUIRE_THAT(thumb_alpha(), WithinAbs(static_cast<double>(full_alpha), 1e-3));

    // Crosses the 2s delay this tick; the 83ms presence fade only starts now, so a 60ms step
    // lands strictly mid-fade rather than overshooting it in one tick.
    REQUIRE(bar.update(0.06s)); // 2.01s idle
    const float mid_alpha = thumb_alpha();
    REQUIRE(mid_alpha < full_alpha);
    REQUIRE(mid_alpha > 0.0f);

    REQUIRE_FALSE(bar.update(0.05s));
    REQUIRE_THAT(thumb_alpha(), WithinAbs(0.0, 1e-3));

    // notify_activity() is what Scroll and VirtualList call from their own wheel handling.
    bar.notify_activity();
    REQUIRE(bar.update(0.05s)); // mid fade-in: not yet fully visible
    const float reappearing_alpha = thumb_alpha();
    REQUIRE(reappearing_alpha > 0.0f);
    REQUIRE(reappearing_alpha < full_alpha);
    REQUIRE_FALSE(bar.update(0.05s)); // settled, fully visible again
    REQUIRE_THAT(thumb_alpha(), WithinAbs(static_cast<double>(full_alpha), 1e-3));
}

TEST_CASE("ScrollBarOverlay says how long until it fades while it waits", "[scroll][scrollbar][tick]")
{
    ScrollBarOverlay bar;
    REQUIRE(bar.fade_wait() == 2001ms); // whole milliseconds and one more
    REQUIRE_FALSE(bar.update(0.5s));
    REQUIRE(bar.fade_wait() == 1501ms);
    REQUIRE_FALSE(bar.update(1.49s));
    REQUIRE(bar.update(0.02s)); // fading: no wait
    REQUIRE_FALSE(bar.fade_wait().has_value());
    REQUIRE_FALSE(bar.update(0.2s)); // faded
    REQUIRE_FALSE(bar.fade_wait().has_value());
    bar.notify_activity();
    REQUIRE_FALSE(bar.fade_wait().has_value()); // still coming back
    REQUIRE(bar.update(0.05s));
    REQUIRE_FALSE(bar.update(0.05s));
    REQUIRE(bar.fade_wait().has_value());
}

TEST_CASE("ScrollBarOverlay never idle-hides while hovered or dragging", "[scroll][scrollbar][animation]")
{
    ScrollBarOverlay bar;
    Theme            theme = Theme::light();
    const Rect       viewport{0, 0, 300, 200};
    const float      content_length = 2000.0f;
    float            offset         = 0.0f;

    REQUIRE(bar.handle_pointer({{296, 10}, PointerButton::None, PointerAction::Move}, viewport, content_length, offset));
    REQUIRE(bar.expanded());

    // Well past the 2s idle-hide delay, but still hovered every tick.
    for (int i = 0; i < 5; ++i)
    {
        (void)bar.update(1.0s);
        bar.handle_pointer({{296, 10}, PointerButton::None, PointerAction::Move}, viewport, content_length, offset);
    }

    TestRenderer r;
    bar.render(r, theme, viewport, content_length, offset);
    REQUIRE(std::get<FillRoundedRect>(r.commands.back()).color.a > 0.0f);
}

TEST_CASE("Scroll draws no overlay scrollbar when content fits the viewport", "[scroll][scrollbar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ScrollControl view;
    view.set_child(std::make_unique<TextControl>("short"));
    (void)view.measure({300, 200}, m, theme);
    view.arrange({0, 0, 300, 200});

    TestRenderer r;
    view.render(r, theme);
    REQUIRE(r.count_of<FillRoundedRect>() == 0); // nothing to scroll, nothing drawn
}

TEST_CASE(
    "Scroll::set_show_scrollbar(false) suppresses the overlay's paint and hit-test even when "
    "content overflows",
    "[scroll][scrollbar][render][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ScrollControl view;
    view.set_child(make_tall_stack(20));
    (void)view.measure({300, 100}, m, theme);
    view.arrange({0, 0, 300, 100});

    REQUIRE(view.show_scrollbar()); // default on
    view.set_show_scrollbar(false);
    REQUIRE_FALSE(view.show_scrollbar());

    // The Buttons paint their own FillRoundedRect chrome, so check what follows the PopClip.
    TestRenderer r;
    view.render(r, theme);
    REQUIRE(std::holds_alternative<PopClip>(r.commands.back())); // no thumb painted after it

    // While hidden the overlay is never consulted, so a drag in its band claims nothing.
    view.handle_pointer({{295, 50}, PointerButton::Left, PointerAction::Down});
    view.handle_pointer({{295, 90}, PointerButton::None, PointerAction::Move});
    REQUIRE_THAT(view.vertical_offset(), WithinAbs(0.0, 1e-3));
    view.handle_pointer({{295, 90}, PointerButton::Left, PointerAction::Up});

    view.set_show_scrollbar(true);
    REQUIRE(view.show_scrollbar());
    TestRenderer r2;
    view.render(r2, theme);
    REQUIRE(std::holds_alternative<FillRoundedRect>(r2.commands.back())); // restored, the thumb again

    // The same drag now scrolls, so the suppression above was real.
    view.handle_pointer({{295, 50}, PointerButton::Left, PointerAction::Down});
    view.handle_pointer({{295, 90}, PointerButton::None, PointerAction::Move});
    REQUIRE(view.vertical_offset() > 0.0f);
}

TEST_CASE("Hovering a Scroll's scrollbar band claims Move instead of reaching content underneath",
          "[scroll][scrollbar][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ScrollControl view;
    StackControl* stack = view.set_child(make_tall_stack(20));
    (void)view.measure({300, 100}, m, theme);
    view.arrange({0, 0, 300, 100});

    auto* first = static_cast<ButtonControl*>(stack->children().front().get());

    // Move over the scrollbar band (the rightmost 12px) must not hover the button behind it.
    view.handle_pointer({{295, 10}, PointerButton::None, PointerAction::Move});
    REQUIRE(ui::testing::visual_state(*first) == ButtonState::Normal); // not Hover: hot() is private, this is its public proxy
}

TEST_CASE("VirtualList render wraps visible items in a PushClip/PopClip pair", "[virtual-list][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    VirtualListControl list;
    list.set_item_count(50);
    list.set_item_height(20.0f);
    list.set_item_factory([](std::size_t i) { return std::make_unique<TextControl>("Item " + std::to_string(i)); });
    (void)list.measure({300, 60}, m, theme);
    list.arrange({0, 0, 300, 60});

    TestRenderer r;
    list.render(r, theme);
    REQUIRE(std::holds_alternative<PushClip>(r.commands.front()));
    REQUIRE(r.count_of<DrawTextRun>() == list.visible_count());
    // 50 items * 20px overflow the 60px viewport, so the thumb paints after the clip pair.
    REQUIRE(r.commands.size() >= 2);
    REQUIRE(std::holds_alternative<PopClip>(r.commands[r.commands.size() - 2]));
    REQUIRE(std::holds_alternative<FillRoundedRect>(r.commands.back()));
}

namespace
{
// The gallery's shape: a NavigationShell whose content is a page Scroll, with a smaller
// Scroll pinned to a fixed Grid cell on that page.
struct NavPage
{
    std::unique_ptr<NavigationShellControl> nav;
    ScrollControl*                          page;  // the content region's own scroll
    ScrollControl*                          inner; // the scrollable control sitting on the page
};

NavPage make_nav_page(const TextMeasurer& m, const Theme& theme)
{
    NavPage built;
    built.nav = std::make_unique<NavigationShellControl>();
    built.nav->set_pane_width(100.0f);
    // The gallery shell's configuration: a Left pane with no back or toggle button.
    built.nav->set_pane_display_mode(NavigationShellPaneDisplayMode::Left);
    built.nav->set_back_button_visible(AutoVisibility::Collapsed);
    built.nav->set_pane_toggle_button_visible(false);
    built.nav->set_settings_visible(false);

    // Enough rows that the pane itself scrolls, so the pane's and the content's wheel handling
    // must stay out of each other's way.
    std::vector<NavEntry> items;
    for (int i = 0; i < 20; ++i)
    {
        NavEntry entry{};
        entry.text = "Row";
        items.push_back(std::move(entry));
    }
    built.nav->set_items(std::move(items));

    auto cell = std::make_unique<GridControl>();
    cell->add_column(GridLength::dip(200.0f));
    cell->add_row(GridLength::dip(100.0f));
    auto inner = std::make_unique<ScrollControl>();
    inner->set_child(make_tall_stack(20));
    built.inner = cell->add(std::move(inner), 0, 0);

    auto page = std::make_unique<StackControl>(Orientation::Vertical);
    page->add(std::move(cell));
    page->add(make_tall_stack(20));

    auto scroll = std::make_unique<ScrollControl>();
    scroll->set_child(std::move(page));
    built.page = built.nav->set_content(std::move(scroll));

    (void)built.nav->measure({500.0f, 300.0f}, m, theme);
    built.nav->arrange({0, 0, 500, 300});
    return built;
}
} // namespace

TEST_CASE("Wheel over a NavigationShell's page reaches a scrollable on it, not the page's own scroll",
          "[scroll][input][wheel-routing][navigation-view]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    NavPage              built = make_nav_page(m, theme);

    // NavigationShell must forward handle_wheel(). Element's default goes through handle_pointer(),
    // where the page's Scroll self-scrolls and no control on the page gets the wheel.
    REQUIRE(built.nav->handle_wheel({built.inner->bounds().center(), PointerButton::None, PointerAction::Wheel, 40.0f}));
    REQUIRE_THAT(built.inner->vertical_offset(), WithinAbs(40.0, 1e-3));
    REQUIRE_THAT(built.page->vertical_offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Wheel over a NavigationShell's page but outside any control scrolls the page",
          "[scroll][input][wheel-routing][navigation-view]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    NavPage              built = make_nav_page(m, theme);

    const Point below_the_cell{built.inner->bounds().center().x, built.inner->bounds().bottom() + 50.0f};
    REQUIRE(built.nav->handle_wheel({below_the_cell, PointerButton::None, PointerAction::Wheel, 40.0f}));
    REQUIRE_THAT(built.page->vertical_offset(), WithinAbs(40.0, 1e-3));
    REQUIRE_THAT(built.inner->vertical_offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Wheel over a NavigationShell's pane scrolls the pane, never the page beside it",
          "[scroll][input][wheel-routing][navigation-view]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    NavPage              built = make_nav_page(m, theme);

    REQUIRE(built.nav->handle_wheel({{50.0f, 150.0f}, PointerButton::None, PointerAction::Wheel, 40.0f}));
    REQUIRE(built.nav->pane_scroll_offset() > 0.0f);
    REQUIRE_THAT(built.page->vertical_offset(), WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(built.inner->vertical_offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Wheel over a ListView scrolls the list rather than the page around it",
          "[scroll][input][wheel-routing][list-view]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    auto cell = std::make_unique<GridControl>();
    cell->add_column(GridLength::dip(200.0f));
    cell->add_row(GridLength::dip(100.0f));
    auto list = std::make_unique<ListViewControl>();
    list->set_items({"one", "two", "three", "four", "five", "six", "seven", "eight"});
    ListViewControl* list_view = cell->add(std::move(list), 0, 0);

    auto page = std::make_unique<StackControl>(Orientation::Vertical);
    page->add(std::move(cell));
    page->add(make_tall_stack(20));

    ScrollControl outer;
    outer.set_child(std::move(page));
    (void)outer.measure({400.0f, 300.0f}, m, theme);
    outer.arrange({0, 0, 400, 300});

    // ListView must forward handle_wheel(), or its VirtualList scrolls but reports the turn as not
    // consumed, and the Scroll around it scrolls the page too.
    REQUIRE(outer.handle_wheel({list_view->bounds().center(), PointerButton::None, PointerAction::Wheel, 40.0f}));
    REQUIRE(list_view->scroll_offset() > 0.0f);
    REQUIRE_THAT(outer.vertical_offset(), WithinAbs(0.0, 1e-3));
}

// ScrollChainMode defaults to Auto: a scroller already at the end a wheel turn points
// toward hands the turn on to the one around it.
TEST_CASE("A wheel turn past a nested Scroll's end chains to the page around it",
          "[scroll][input][wheel-routing][chaining]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    NavPage              built = make_nav_page(m, theme);

    built.inner->scroll_to(built.inner->horizontal_offset(),
                           1'000'000.0f,
                           {.animation_mode = ui::ScrollAnimationMode::Disabled}); // at its bottom
    const float inner_end = built.inner->vertical_offset();
    REQUIRE(inner_end > 0.0f);

    REQUIRE(built.nav->handle_wheel({built.inner->bounds().center(), PointerButton::None, PointerAction::Wheel, 40.0f}));
    REQUIRE_THAT(built.page->vertical_offset(), WithinAbs(40.0, 1e-3));
    REQUIRE_THAT(built.inner->vertical_offset(), WithinAbs(inner_end, 1e-3));

    // Toward its top it still has room, so it takes the turn itself.
    REQUIRE(built.nav->handle_wheel({built.inner->bounds().center(), PointerButton::None, PointerAction::Wheel, -40.0f}));
    REQUIRE_THAT(built.inner->vertical_offset(), WithinAbs(inner_end - 40.0f, 1e-3));
    REQUIRE_THAT(built.page->vertical_offset(), WithinAbs(40.0, 1e-3));
}

TEST_CASE("ScrollChainMode::Never keeps a wheel turn at the end", "[scroll][input][wheel-routing][chaining]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    NavPage              built = make_nav_page(m, theme);

    REQUIRE(built.inner->vertical_scroll_chain_mode() == ScrollChainMode::Auto);
    built.inner->set_vertical_scroll_chain_mode(ScrollChainMode::Never);
    built.inner->scroll_to(built.inner->horizontal_offset(),
                           1'000'000.0f,
                           {.animation_mode = ui::ScrollAnimationMode::Disabled});

    REQUIRE(built.nav->handle_wheel({built.inner->bounds().center(), PointerButton::None, PointerAction::Wheel, 40.0f}));
    REQUIRE_THAT(built.page->vertical_offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("A ListView scrolled to its end chains the wheel to the page",
          "[scroll][input][wheel-routing][chaining][list-view]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    auto cell = std::make_unique<GridControl>();
    cell->add_column(GridLength::dip(200.0f));
    cell->add_row(GridLength::dip(100.0f));
    auto list = std::make_unique<ListViewControl>();
    list->set_items({"one", "two", "three", "four", "five", "six", "seven", "eight"});
    ListViewControl* list_view = cell->add(std::move(list), 0, 0);

    auto page = std::make_unique<StackControl>(Orientation::Vertical);
    page->add(std::move(cell));
    page->add(make_tall_stack(20));

    ScrollControl outer;
    outer.set_child(std::move(page));
    (void)outer.measure({400.0f, 300.0f}, m, theme);
    outer.arrange({0, 0, 400, 300});

    const Point over_list = list_view->bounds().center();
    REQUIRE(outer.handle_wheel({over_list, PointerButton::None, PointerAction::Wheel, 10'000.0f})); // to its end
    const float list_end = list_view->scroll_offset();
    REQUIRE(list_end > 0.0f);
    REQUIRE_THAT(outer.vertical_offset(), WithinAbs(0.0, 1e-3));

    REQUIRE(outer.handle_wheel({over_list, PointerButton::None, PointerAction::Wheel, 40.0f}));
    REQUIRE_THAT(outer.vertical_offset(), WithinAbs(40.0, 1e-3));
    REQUIRE_THAT(list_view->scroll_offset(), WithinAbs(list_end, 1e-3));
}

TEST_CASE("A VirtualList factory returning nullptr ends the realized rows there", "[virtual-list]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    // The source shrank to three items before the count caught up.
    const std::vector<std::string> names{"a", "b", "c"};
    VirtualListControl             list;
    list.set_item_count(10);
    list.set_item_height(20.0f);
    list.set_item_factory([&names](std::size_t i) -> std::unique_ptr<Element>
                          { return i < names.size() ? std::make_unique<TextControl>(names[i]) : nullptr; });

    (void)list.measure({300, 200}, m, theme);
    list.arrange({0, 0, 300, 200});

    CHECK(list.visible_count() == 3);
}
