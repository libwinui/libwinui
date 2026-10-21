// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/collections/ForEach.hpp"
#include "winui/ui/layouts/Layout.hpp"
#include "winui/ui/layouts/LinedFlowLayout.hpp"
#include "winui/ui/layouts/StackLayout.hpp"
#include "winui/ui/layouts/TileLayout.hpp"
#include "winui/ui/layouts/WrapLayout.hpp"
#include "winui/ui/panels/LayoutPanel.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <limits>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{

// A fixed-size item that remembers which item it is.
class Item final : public Element
{
public:
    Item(std::size_t index, Size size) : index_(index), size_(size)
    {
    }
    [[nodiscard]] std::size_t index() const
    {
        return index_;
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return size_;
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }


private:
    std::size_t index_;
    Size        size_;
};

struct Fixture
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();
    ScrollControl        view;
    ForEachControl*      repeater = nullptr;

    explicit Fixture(std::size_t count, Size item = {100.0f, 20.0f})
    {
        repeater = view.set_child(std::make_unique<ForEachControl>());
        repeater->set_item_template([item](std::size_t i) { return std::make_unique<Item>(i, item); });
        repeater->set_item_count(count);
    }
    void layout(Size size = {200.0f, 100.0f})
    {
        (void)view.measure(size, measurer, theme);
        view.arrange(Rect{0.0f, 0.0f, size.width, size.height});
    }
    [[nodiscard]] Item* item(std::size_t index) const
    {
        return dynamic_cast<Item*>(repeater->try_get_element(index));
    }
};

} // namespace

TEST_CASE("ForEach with a StackLayout realizes the viewport plus a viewport each side", "[for-each]")
{
    Fixture f(1000);
    f.layout();
    // 100 tall, 20 per item: the viewport shows 5, the cache adds 100 below (5 more) and one item
    // past the edge is always generated.
    const std::vector<std::size_t> realized = f.repeater->realized_indices();
    REQUIRE_FALSE(realized.empty());
    CHECK(realized.front() == 0);
    CHECK(realized.back() >= 9);
    CHECK(realized.back() <= 12);
    CHECK_THAT(f.repeater->bounds().height, WithinAbs(20000.0, 1e-3));
    REQUIRE(f.item(3) != nullptr);
    CHECK_THAT(f.item(3)->bounds().y, WithinAbs(60.0, 1e-3));
    // Stretched across (a stack's items fill the cross axis).
    CHECK_THAT(f.item(3)->bounds().width, WithinAbs(200.0, 1e-3));
}

TEST_CASE("Scrolling a ForEach realizes the items in view and clears the rest", "[for-each]")
{
    Fixture     f(1000);
    std::size_t prepared = 0;
    std::size_t cleared  = 0;
    f.repeater->on_element_prepared([&] { ++prepared; });
    f.repeater->on_element_clearing([&](Element&) { ++cleared; });
    f.layout();
    f.view.scroll_to(f.view.horizontal_offset(), 10000.0f, {.animation_mode = ui::ScrollAnimationMode::Disabled}); // item 500 at the top
    const std::vector<std::size_t> realized = f.repeater->realized_indices();
    REQUIRE_FALSE(realized.empty());
    CHECK(realized.front() >= 490);
    CHECK(realized.front() <= 496);
    CHECK(realized.back() <= 512);
    CHECK(f.repeater->try_get_element(0) == nullptr);
    CHECK(cleared > 0);
    REQUIRE(f.item(500) != nullptr);
    CHECK_THAT(f.item(500)->bounds().y, WithinAbs(0.0, 1e-3)); // at the top of the viewport
    CHECK(f.repeater->realized_count() == prepared - cleared);
}

TEST_CASE("A zero cache length realizes only what shows", "[for-each]")
{
    Fixture f(1000);
    f.repeater->set_vertical_cache_length(0.0f);
    f.layout();
    CHECK(f.repeater->realized_indices().back() <= 6);
}

TEST_CASE("ForEach keeps realized elements through inserts and removes", "[for-each]")
{
    Fixture f(20);
    f.layout();
    Element* third = f.repeater->try_get_element(3);
    REQUIRE(third != nullptr);
    std::vector<std::pair<std::size_t, std::size_t>> moves;
    f.repeater->on_element_index_changed(
        [&](const ElementIndexChangedArgs& args) { moves.emplace_back(args.old_index, args.new_index); });

    f.repeater->notify_inserted(1, 2);
    CHECK(f.repeater->item_count() == 22);
    CHECK(f.repeater->try_get_element(5) == third);
    CHECK(f.repeater->element_index(*third) == 5);
    CHECK_FALSE(moves.empty());

    f.repeater->notify_removed(0, 1);
    CHECK(f.repeater->try_get_element(4) == third);
    f.layout();
    CHECK_THAT(third->bounds().y, WithinAbs(80.0, 1e-3));
}

TEST_CASE("An item template that inserts into the collection leaves the index consistent", "[for-each]")
{
    Fixture         f(20);
    bool            edited   = false;
    std::size_t     prepared = 0;
    std::size_t     cleared  = 0;
    ForEachControl* repeater = f.repeater;
    repeater->set_item_template(
        [repeater, &edited](std::size_t i)
        {
            if (i == 2 && !edited)
            {
                edited = true;
                repeater->notify_inserted(0); // items 0 and 1 move to 1 and 2
            }
            return std::make_unique<Item>(i, Size{100.0f, 20.0f});
        });
    repeater->on_element_prepared([&] { ++prepared; });
    repeater->on_element_clearing([&](Element&) { ++cleared; });
    f.layout();
    f.layout();

    CHECK(edited);
    CHECK(repeater->item_count() == 21);
    CHECK(repeater->realized_count() == prepared - cleared); // nothing announced that was then dropped
    for (const std::size_t index : repeater->realized_indices())
    {
        Element* element = repeater->try_get_element(index);
        REQUIRE(element != nullptr);
        CHECK(repeater->element_index(*element) == index);
    }
}

TEST_CASE("get_or_create_element realizes a far item where the layout puts it", "[for-each]")
{
    Fixture f(1000);
    f.layout();
    Element* far = f.repeater->get_or_create_element(700);
    REQUIRE(far != nullptr);
    CHECK_THAT(far->bounds().y - f.repeater->bounds().y, WithinAbs(700.0 * 20.0, 1e-2));
    far->start_bring_into_view();
    CHECK_THAT(f.view.vertical_offset(), WithinAbs(700.0 * 20.0 - 80.0, 1e-2));
}

TEST_CASE("TileLayout sizes every cell from item 0 and justifies the lines", "[items-repeater][uniform-grid]")
{
    Fixture f(10, Size{40.0f, 30.0f});
    auto*   grid = f.repeater->set_layout(std::make_unique<TileLayoutEngine>());
    grid->set_min_column_spacing(8.0f);
    grid->set_min_row_spacing(4.0f);
    f.layout(Size{150.0f, 400.0f});
    // (150 + 8) / 48 = 3 per line; 4 lines.
    CHECK_THAT(f.repeater->layout_extent().height, WithinAbs(4.0 * 34.0 - 4.0, 1e-3));
    REQUIRE(f.item(4) != nullptr);
    CHECK_THAT(f.item(4)->bounds().x, WithinAbs(48.0, 1e-3));
    CHECK_THAT(f.item(4)->bounds().y, WithinAbs(34.0, 1e-3));

    grid->set_maximum_rows_or_columns(2);
    f.layout(Size{150.0f, 400.0f});
    CHECK_THAT(f.item(3)->bounds().x, WithinAbs(48.0, 1e-3));
    CHECK_THAT(f.item(3)->bounds().y, WithinAbs(34.0, 1e-3));

    grid->set_maximum_rows_or_columns(0);
    grid->set_items_justification(ItemsJustification::Center);
    f.layout(Size{150.0f, 400.0f});
    // A line of 3 is 136 wide: 14 over, 7 each side.
    CHECK_THAT(f.item(0)->bounds().x, WithinAbs(7.0, 1e-3));

    grid->set_items_justification(ItemsJustification::Start);
    grid->set_items_stretch(TileLayoutItemsStretch::Fill);
    f.layout(Size{150.0f, 400.0f});
    // 3 cells of 40 + 2 gaps of 8 = 136; 14 over, 4 each (integer division).
    CHECK_THAT(f.item(0)->bounds().width, WithinAbs(44.0, 1e-3));
}

TEST_CASE("WrapLayout wraps items of different widths and aligns each line", "[items-repeater][flow-layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();
    ScrollControl        view;
    auto*                repeater = view.set_child(std::make_unique<ForEachControl>());
    const float          widths[] = {60.0f, 80.0f, 50.0f, 90.0f, 40.0f};
    repeater->set_item_template([&widths](std::size_t i) { return std::make_unique<Item>(i, Size{widths[i], 20.0f}); });
    repeater->set_item_count(5);
    auto* flow = repeater->set_layout(std::make_unique<WrapLayoutEngine>());
    flow->set_min_item_spacing(10.0f);
    flow->set_line_spacing(5.0f);
    (void)view.measure({200.0f, 300.0f}, measurer, theme);
    view.arrange({0.0f, 0.0f, 200.0f, 300.0f});
    // Line 1: 60 + 10 + 80 = 150 (+10+50 = 210 does not fit); line 2: 50, 90, (40 does not fit: 50+10+90+10+40 = 200 fits).
    const auto x = [&](std::size_t i) { return repeater->try_get_element(i)->bounds().x; };
    const auto y = [&](std::size_t i) { return repeater->try_get_element(i)->bounds().y; };
    CHECK_THAT(x(1), WithinAbs(70.0, 1e-3));
    CHECK_THAT(y(2), WithinAbs(25.0, 1e-3));
    CHECK_THAT(x(3), WithinAbs(60.0, 1e-3));
    CHECK_THAT(x(4), WithinAbs(160.0, 1e-3));

    flow->set_line_alignment(ItemsJustification::End);
    (void)view.measure({200.0f, 300.0f}, measurer, theme);
    view.arrange({0.0f, 0.0f, 200.0f, 300.0f});
    CHECK_THAT(x(1), WithinAbs(120.0, 1e-3)); // 50 over on the first line
}

TEST_CASE("LinedFlowLayout fills lines at one height, stretching all but the last",
          "[items-repeater][lined-flow-layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();
    ScrollControl        view;
    auto*                repeater = view.set_child(std::make_unique<ForEachControl>());
    // Aspect 1.5 at any height: 150 wide on 100-high lines.
    class Photo final : public Element
    {
    public:

    protected:
        [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
        {
            const float h = std::isfinite(available.height) ? available.height : 100.0f;
            const float w = std::isfinite(available.width) ? available.width : h * 1.5f;
            return {w, h};
        }
        void render_override(Renderer&, const RenderContext&) const override
        {
        }
    };
    repeater->set_item_template([](std::size_t) { return std::make_unique<Photo>(); });
    repeater->set_item_count(7);
    auto* lined = repeater->set_layout(std::make_unique<LinedFlowLayoutEngine>());
    lined->set_line_height(100.0f);
    lined->set_min_item_spacing(10.0f);
    lined->set_line_spacing(5.0f);
    lined->set_items_stretch(LinedFlowLayoutItemsStretch::Fill);
    (void)view.measure({400.0f, 600.0f}, measurer, theme);
    view.arrange({0.0f, 0.0f, 400.0f, 600.0f});
    // Two items make 310 (expand 380/300 = 1.27); three make 470 (shrink 380/450 = 0.84): shrink is
    // nearer 1, so lines of three, each item 380/3 wide.
    CHECK(lined->line_count() == 3);
    const Rect second = repeater->try_get_element(1)->bounds();
    CHECK_THAT(second.width, WithinAbs(380.0 / 3.0, 1e-2));
    CHECK_THAT(second.height, WithinAbs(100.0, 1e-3));
    const Rect last = repeater->try_get_element(6)->bounds();
    CHECK_THAT(last.y, WithinAbs(210.0, 1e-3));
    CHECK_THAT(last.width, WithinAbs(150.0, 1e-3)); // the last line is not stretched
}

TEST_CASE("StackLayout in a LayoutPanel lays every child out", "[items-repeater][layout-panel]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();
    LayoutPanelControl   panel;
    auto*                stack = panel.set_layout(std::make_unique<StackLayoutEngine>());
    stack->set_spacing(4.0f);
    for (std::size_t i = 0; i < 50; ++i)
    {
        panel.add(std::make_unique<Item>(i, Size{30.0f, 10.0f}));
    }
    const Size desired = panel.measure({Unbounded, Unbounded}, measurer, theme);
    CHECK_THAT(desired.height, WithinAbs(50.0 * 14.0 - 4.0, 1e-3));
    panel.arrange({0.0f, 0.0f, desired.width, desired.height});
    CHECK_THAT(panel.children()[49]->bounds().y, WithinAbs(49.0 * 14.0, 1e-3));
}

TEST_CASE("A focused element stays realized when it scrolls out of the window", "[items-repeater][focus]")
{
    HeadlessUI      ui       = create_headless();
    auto            view     = std::make_unique<ScrollControl>();
    ForEachControl* repeater = view->set_child(std::make_unique<ForEachControl>());
    repeater->set_item_template([&ui](std::size_t) { return ui.button("Row"); });
    repeater->set_item_count(500);
    ScrollControl* scroll = view.get();
    scroll->set_height(100.0f);
    ui.window().set_content(std::move(view));
    ui.layout({200.0f, 100.0f});

    Element* first = repeater->try_get_element(0);
    REQUIRE(first != nullptr);
    ui.window().focus_content();
    REQUIRE(ui.window().focused() == first);
    CHECK(first->has_focus_within());
    CHECK(repeater->has_focus_within());

    // Far away: the focused row is kept (the pinned element), the others around it go.
    scroll->scroll_to(scroll->horizontal_offset(), 5000.0f, {.animation_mode = ui::ScrollAnimationMode::Disabled});
    ui.layout({200.0f, 100.0f});
    CHECK(repeater->try_get_element(0) == first);
    CHECK(repeater->try_get_element(1) == nullptr);
}

TEST_CASE("A non-virtualizing layout in a ForEach realizes every item", "[for-each]")
{
    class Column final : public NonVirtualizingLayout
    {
    public:
        Size measure(NonVirtualizingLayoutContext& context, Size available) override
        {
            float h = 0.0f;
            for (std::size_t i = 0; i < context.item_count(); ++i)
            {
                h += context.measure(i, Size{available.width, std::numeric_limits<float>::infinity()}).height;
            }
            return Size{available.width, h};
        }
        Size arrange(NonVirtualizingLayoutContext& context, Size final_size) override
        {
            float y = 0.0f;
            for (std::size_t i = 0; i < context.item_count(); ++i)
            {
                const Size d = context.desired_size(i);
                context.arrange(i, Rect{0.0f, y, final_size.width, d.height});
                y += d.height;
            }
            return final_size;
        }
    };
    Fixture f(40);
    f.repeater->set_layout(std::make_unique<Column>());
    f.layout();
    CHECK(f.repeater->realized_count() == 40);
    CHECK_THAT(f.item(39)->bounds().y, WithinAbs(39.0 * 20.0, 1e-3));
}

TEST_CASE("A theme-resource background fills a ForEach and follows the theme", "[items-repeater][theme]")
{
    HeadlessTextMeasurer m;
    const Theme          light    = Theme::light();
    const Theme          dark     = Theme::fluent(Palette::fluent_dark());
    constexpr ThemeColor resource = [](const Theme& t) { return t.palette.text_fill_secondary; };
    REQUIRE_FALSE(resource(light) == resource(dark));

    ui::Control<ui::ForEach<int>> repeater;
    auto built = ui::build(ui::ForEach<int>{.items = std::vector<int>{}, .background = resource, .control = repeater});
    (void)repeater->measure({100.0f, 100.0f}, m, light);
    repeater->arrange({0, 0, 100, 100});

    const auto fill_of = [&](const Theme& theme)
    {
        TestRenderer r;
        repeater->render(r, theme);
        REQUIRE(r.count_of<FillRoundedRect>() >= 1);
        return std::get<FillRoundedRect>(r.commands[0]).color;
    };
    CHECK(fill_of(light) == resource(light));
    CHECK(fill_of(dark) == resource(dark));
}
