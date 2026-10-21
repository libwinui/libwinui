// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/DragDrop.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/collections/ListViewBase.hpp"
#include "winui/ui/collections/TileView.hpp"
#include "winui/ui/containers/Border.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <any>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;
using namespace std::chrono_literals;

namespace
{

class Tile final : public Element
{
public:
    explicit Tile(Size size) : size_(size)
    {
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
    Size size_;
};

std::vector<std::string> names(std::size_t n)
{
    std::vector<std::string> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        out.push_back("Item " + std::to_string(i));
    }
    return out;
}

void layout(Element& e, Size size)
{
    // Static: the list re-measures from arrange() and render() with the ones it was measured with.
    static HeadlessTextMeasurer m;
    (void)e.measure(size, m, Theme::default_light());
    e.arrange(Rect{0.0f, 0.0f, size.width, size.height});
}

Rect item_rect(const ListViewBase& list, std::size_t index)
{
    const Element* e = list.repeater()->try_get_element(index);
    return e != nullptr ? e->bounds() : Rect{};
}

void click(ListViewBase& list, std::size_t index, bool ctrl = false, bool shift = false, int clicks = 1)
{
    const Point  p = item_rect(list, index).center();
    PointerEvent d{p, PointerButton::Left, PointerAction::Down};
    d.modifiers.ctrl  = ctrl;
    d.modifiers.shift = shift;
    d.click_count     = clicks;
    list.handle_pointer(d);
    PointerEvent u{p, PointerButton::Left, PointerAction::Up};
    u.modifiers.ctrl  = ctrl;
    u.modifiers.shift = shift;
    list.handle_pointer(u);
}

} // namespace

TEST_CASE("A templated ListView sizes each row to its content, at least the 40 DIP minimum", "[list-view-base]")
{
    ListViewControl list;
    list.set_item_template([](std::size_t i) { return std::make_unique<Tile>(Size{100.0f, i == 1 ? 60.0f : 10.0f}); });
    list.set_item_count(4);
    layout(list, {300.0f, 400.0f});
    CHECK_THAT(item_rect(list, 0).height, WithinAbs(40.0, 1e-3)); // the minimum
    CHECK_THAT(item_rect(list, 1).height, WithinAbs(60.0, 1e-3));
    CHECK_THAT(item_rect(list, 2).y - item_rect(list, 1).y, WithinAbs(60.0, 1e-3));
    // The content sits 16 in (the row's 16,0,12,0 padding), centered vertically, stretched across.
    std::vector<Element*> content;
    list.repeater()->try_get_element(0)->automation_children(content);
    REQUIRE(content.size() == 1);
    const Rect row = item_rect(list, 0);
    CHECK_THAT(content[0]->bounds().x - row.x, WithinAbs(16.0, 1e-3));
    CHECK_THAT(content[0]->bounds().y - row.y, WithinAbs(15.0, 1e-3));
    CHECK_THAT(content[0]->bounds().width, WithinAbs(row.width - 28.0f, 1e-3));
}

TEST_CASE("A templated item is named by its content for UI Automation", "[list-view-base][automation]")
{
    TileViewControl grid;
    grid.set_item_template(
        [](std::size_t i)
        {
            return ui::Spec{
                ui::VerticalStack{
                    .accessibility = {.name = i == 0 ? "First page" : ""},
                    .content = {ui::Border{.width = 96, .height = 60}, ui::Text{.text = "Page " + std::to_string(i + 1)}},
                }}
                .take();
        });
    grid.set_item_count(2);
    layout(grid, {420.0f, 400.0f});
    CHECK(grid.repeater()->try_get_element(0)->automation_name() == "First page"); // the template's own name
    CHECK(grid.repeater()->try_get_element(1)->automation_name() == "Page 2");     // else the first text in it
}

TEST_CASE("A button in a row takes its own click, and a click beside it selects the row", "[list-view-base][selection]")
{
    ListViewControl list;
    int             clicks = 0;
    list.set_item_template(
        [&clicks](std::size_t)
        {
            auto button = std::make_unique<ButtonControl>("Go");
            button->on_click([&clicks] { ++clicks; });
            return button;
        });
    list.set_item_count(3);
    layout(list, {300.0f, 400.0f});
    std::vector<Element*> content;
    list.repeater()->try_get_element(1)->automation_children(content);
    REQUIRE(content.size() == 1);

    const auto press = [&list](Point p)
    {
        list.handle_pointer({p, PointerButton::Left, PointerAction::Down});
        list.handle_pointer({p, PointerButton::Left, PointerAction::Up});
    };
    press(content[0]->bounds().center());
    CHECK(clicks == 1);
    CHECK(list.selected_items().empty());
    CHECK(list.current_item() == 1); // the keyboard carries on from the row

    press(Point{item_rect(list, 1).x + 4.0f, item_rect(list, 1).center().y}); // the row's own padding
    CHECK(clicks == 1);
    CHECK(list.selected_items() == std::vector<std::size_t>{1});
}

TEST_CASE("set_current_item moves the focus cue and leaves the selection", "[list-view-base][selection]")
{
    ListViewControl list;
    list.set_selection_mode(SelectionMode::Multiple);
    list.set_items(names(6));
    layout(list, {300.0f, 400.0f});
    list.set_selected_items({1, 3});

    list.set_current_item(3);
    CHECK(list.current_item() == 3);
    CHECK(list.selected_items() == std::vector<std::size_t>{1, 3});
    list.set_current_item(6); // past the end
    CHECK(list.current_item() == 3);

    ui::testing::set_focused(list, true);
    list.handle_key(KeyEvent{Key::ArrowDown, KeyAction::Down});
    CHECK(list.current_item() == 4); // the arrows go on from there
}

TEST_CASE("Automation invokes an item a click acts on", "[list-view-base][automation]")
{
    TileViewControl grid;
    grid.set_selection_mode(SelectionMode::None);
    grid.set_items(names(4));
    layout(grid, {420.0f, 400.0f});
    const auto providers = [&grid](std::size_t i)
    { return ui::automation_providers(*grid.repeater()->try_get_element(i)); };
    CHECK(providers(1).invoke == nullptr); // a click does nothing to it

    std::vector<std::size_t> clicked;
    std::vector<std::size_t> invoked;
    grid.on_item_click([&clicked](std::size_t i) { clicked.push_back(i); });
    grid.set_item_click_enabled(true);
    grid.on_item_invoked([&invoked](std::size_t i) { invoked.push_back(i); });
    grid.set_item_invoked_enabled(true);
    REQUIRE(providers(2).invoke != nullptr);
    REQUIRE(providers(2).invoke->invoke());
    CHECK(clicked == std::vector<std::size_t>{2});
    CHECK(invoked == std::vector<std::size_t>{2});
    CHECK(grid.current_item() == 2);

    grid.set_enabled(false);
    CHECK_FALSE(providers(2).invoke->invoke());
}

TEST_CASE("Automation adds items to a multiple selection and takes them out", "[list-view-base][automation]")
{
    TileViewControl grid;
    grid.set_selection_mode(SelectionMode::Multiple);
    grid.set_items(names(4));
    layout(grid, {420.0f, 400.0f});
    const auto provider = [&grid](std::size_t i)
    { return ui::automation_providers(*grid.repeater()->try_get_element(i)).selection_item; };

    REQUIRE(provider(0)->add_to_selection());
    REQUIRE(provider(2)->add_to_selection());
    CHECK(grid.selected_items() == std::vector<std::size_t>{0, 2});
    REQUIRE(provider(0)->remove_from_selection());
    CHECK(grid.selected_items() == std::vector<std::size_t>{2});

    grid.set_selection_mode(SelectionMode::Single);
    REQUIRE(provider(1)->add_to_selection());
    CHECK(grid.selected_items() == std::vector<std::size_t>{1}); // one at a time
}

TEST_CASE("Multiple selection toggles on click and moves the content over for the check boxes",
          "[list-view-base][selection]")
{
    ListViewControl list;
    list.set_selection_mode(SelectionMode::Multiple);
    list.set_items(names(6));
    layout(list, {300.0f, 400.0f});
    CHECK(list.selected_items().empty()); // only Single mode selects the first of a new list of strings
    click(list, 1);
    click(list, 3);
    CHECK(list.selected_items() == std::vector<std::size_t>{1, 3});
    click(list, 1);
    CHECK(list.selected_items() == std::vector<std::size_t>{3});
    click(list, 5, false, /*shift=*/true); // 1..5 to the anchor's state (1, unselected): 3 goes too
    CHECK(list.selected_items().empty());
    click(list, 2);
    click(list, 4, false, /*shift=*/true); // 2..4 to the anchor's state (selected)
    CHECK(list.selected_items() == std::vector<std::size_t>{2, 3, 4});

    // The check box: 20x20, 14 in from the left, centered.
    TestRenderer r;
    list.render(r);
    const Rect row   = item_rect(list, 3);
    bool       found = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&c);
            fill != nullptr && fill->bounds.width == 20.0f && fill->bounds.y > row.y && fill->bounds.y < row.bottom())
        {
            found = true;
            CHECK_THAT(fill->bounds.x, WithinAbs(row.x + 14.0f, 1e-3));
        }
    }
    CHECK(found);
    // The text moved 28 over (16 + 28 = 44 in).
    bool text = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t != nullptr && t->text == "Item 3")
        {
            text = true;
            CHECK_THAT(t->bounds.x, WithinAbs(row.x + 44.0f, 1e-3));
        }
    }
    CHECK(text);
}

TEST_CASE("Extended selection and Ctrl+A", "[list-view-base][selection]")
{
    ListViewControl list;
    list.set_selection_mode(SelectionMode::Extended);
    list.set_items(names(8));
    layout(list, {300.0f, 400.0f});
    click(list, 2);
    click(list, 4, /*ctrl=*/true);
    CHECK(list.selected_items() == std::vector<std::size_t>{2, 4});
    click(list, 6, false, /*shift=*/true);
    CHECK(list.selected_items() == std::vector<std::size_t>{4, 5, 6});
    ui::testing::set_focused(list, true);
    list.handle_key(KeyEvent{Key::A, KeyAction::Down, {.ctrl = true}});
    CHECK(list.selected_items().size() == 8);
    list.handle_key(KeyEvent{Key::A, KeyAction::Down, {.ctrl = true}}); // all selected: none
    CHECK(list.selected_items().empty());
}

TEST_CASE("on_item_click fires on a click and the item is selected as well", "[list-view-base][click]")
{
    ListViewControl list;
    list.set_items(names(5));
    list.set_item_click_enabled(true);
    std::vector<std::size_t> clicked;
    list.on_item_click([&](std::size_t i) { clicked.push_back(i); });
    layout(list, {300.0f, 400.0f});
    click(list, 3);
    CHECK(clicked == std::vector<std::size_t>{3});
    CHECK(list.selected_index() == 3);
    list.set_selection_mode(SelectionMode::None);
    click(list, 1);
    CHECK(clicked.back() == 1);
    CHECK(list.selected_index() == std::nullopt);
}

TEST_CASE("on_item_invoked is a double-click or Enter, and under SelectionMode::None a click or Space",
          "[list-view-base][invoke]")
{
    ListViewControl list;
    list.set_items(names(5));
    std::vector<std::size_t> invoked;
    list.on_item_invoked([&](std::size_t i) { invoked.push_back(i); });
    layout(list, {300.0f, 400.0f});
    click(list, 1, false, false, 2);
    CHECK(invoked.empty()); // not enabled

    list.set_item_invoked_enabled(true);
    click(list, 1);
    CHECK(invoked.empty()); // a single click selects
    CHECK(list.selected_index() == 1);
    click(list, 1, false, false, 2);
    CHECK(invoked == std::vector<std::size_t>{1});
    ui::testing::set_focused(list, true);
    list.handle_key(KeyEvent{Key::Enter, KeyAction::Down});
    CHECK(invoked.size() == 2);
    list.handle_key(KeyEvent{Key::Space, KeyAction::Down}); // Space selects
    CHECK(invoked.size() == 2);

    list.set_selection_mode(SelectionMode::None);
    click(list, 3);
    CHECK(invoked.back() == 3);
    list.handle_key(KeyEvent{Key::Space, KeyAction::Down});
    CHECK(invoked.size() == 4);
}

TEST_CASE("Grouped items get headers, and the current group's header pins to the top", "[list-view-base][groups]")
{
    ListViewControl list;
    list.set_items(names(30));
    list.set_item_height(40.0f);
    std::vector<std::string> keys;
    for (const char* key : {"A", "B", "C"})
    {
        keys.insert(keys.end(), 10, key);
    }
    list.set_group_keys(keys);
    layout(list, {300.0f, 300.0f});
    // Flat: header A at 0, items 0..9, header B, ...
    const Element* header_a = list.repeater()->try_get_element(0);
    REQUIRE(header_a != nullptr);
    CHECK(header_a->bounds().height >= 44.0f); // a group header's minimum height
    const Element* item0 = list.repeater()->try_get_element(1);
    CHECK(item0->bounds().y > header_a->bounds().y);

    // Scroll into group A's items: its header is pinned at the top.
    list.scroll_view()->scroll_to(list.scroll_view()->horizontal_offset(),
                                  200.0f,
                                  {.animation_mode = ui::ScrollAnimationMode::Disabled});
    layout(list, {300.0f, 300.0f});
    TestRenderer r;
    list.render(r);
    // The natural header scrolled away above the viewport; a copy of it is drawn at the top.
    bool pinned = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c);
            t != nullptr && t->text == "A" && t->bounds.y >= list.scroll_view()->bounds().y &&
            t->bounds.y < list.scroll_view()->bounds().y + 20.0f)
        {
            pinned = true;
        }
    }
    CHECK(pinned);
}

TEST_CASE("scroll_into_view: Default scrolls the least, Leading puts the item at the top", "[list-view-base][scroll]")
{
    ListViewControl list;
    list.set_items(names(100));
    list.set_item_height(20.0f);
    layout(list, {300.0f, 200.0f});
    const float viewport = list.scroll_view()->bounds().height;
    list.scroll_into_view(50);
    CHECK_THAT(list.scroll_offset(), WithinAbs(1020.0 - viewport, 1e-2));
    list.scroll_into_view(52); // below: to its bottom again
    CHECK_THAT(list.scroll_offset(), WithinAbs(1060.0 - viewport, 1e-2));
    list.scroll_into_view(30, ScrollIntoViewAlignment::Leading);
    CHECK_THAT(list.scroll_offset(), WithinAbs(600.0, 1e-2));

    // The relative scroll position: the first item in view and its offset, and back.
    list.scroll_view()->scroll_to(list.scroll_view()->horizontal_offset(),
                                  615.0f,
                                  {.animation_mode = ui::ScrollAnimationMode::Disabled});
    const std::string saved = list.relative_scroll_position([&](std::size_t i) { return list.items()[i]; });
    CHECK(saved == "Item 30|15.000000");
    list.scroll_view()->scroll_to(list.scroll_view()->horizontal_offset(),
                                  0.0f,
                                  {.animation_mode = ui::ScrollAnimationMode::Disabled});
    CHECK(list.set_relative_scroll_position(saved,
                                            [&](const std::string& key) -> std::optional<std::size_t>
                                            {
                                                const auto it = std::find(list.items().begin(), list.items().end(), key);
                                                return it == list.items().end()
                                                           ? std::optional<std::size_t>{}
                                                           : static_cast<std::size_t>(it - list.items().begin());
                                            }));
    CHECK_THAT(list.scroll_offset(), WithinAbs(615.0, 1e-2));
}

TEST_CASE("KeepLastItemInView follows a list growing at its end", "[list-view-base][scroll]")
{
    ListViewControl list;
    list.set_items_updating_scroll_mode(ItemsUpdatingScrollMode::KeepLastItemInView);
    list.set_item_height(40.0f);
    list.set_items(names(20));
    layout(list, {300.0f, 200.0f});
    const float at_end = list.scroll_view()->scrollable_height();
    CHECK_THAT(list.scroll_offset(), WithinAbs(at_end, 1e-2)); // a new collection opens at its end
    list.add_item("New");
    layout(list, {300.0f, 200.0f});
    CHECK_THAT(list.scroll_offset(), WithinAbs(list.scroll_view()->scrollable_height(), 1e-2));
    CHECK(list.scroll_view()->scrollable_height() > at_end);
}

TEST_CASE("Making room in a TileView moves the last item of a line to the start of the next",
          "[list-view-base][grid-view][drag]")
{
    TileViewControl grid;
    grid.set_item_template([](std::size_t) { return std::make_unique<Tile>(Size{96.0f, 60.0f}); });
    grid.set_item_count(8);
    grid.set_can_reorder_items(true);
    grid.set_allow_drop(true);
    grid.on_drag_over([] { return DragResponse{.operation = DataPackageOperation::Copy}; });
    layout(grid, {420.0f, 400.0f}); // cells of 100 x 64, four to a line: two full lines

    // A file from outside, over the start of item 1: items 1 to 7 move on one cell.
    DataPackage package;
    package.files = {"page.pdf"};
    grid.drag_over(DragEventArgs{package, Point{item_rect(grid, 1).x + 5.0f, item_rect(grid, 1).center().y}});

    const auto moved = [&](std::size_t item) { return grid.repeater()->try_get_element(item)->translation(); };
    // Along a line, into the neighbor's cell.
    CHECK_THAT(moved(1).x, WithinAbs(100.0, 1e-3));
    CHECK_THAT(moved(1).y, WithinAbs(0.0, 1e-3));
    // The last of a line, to the start of the next.
    CHECK_THAT(moved(3).x, WithinAbs(-300.0, 1e-3));
    CHECK_THAT(moved(3).y, WithinAbs(64.0, 1e-3));
    // The last item has no neighbor to take the cell from, and its cell starts a third line.
    CHECK_THAT(moved(7).x, WithinAbs(-300.0, 1e-3));
    CHECK_THAT(moved(7).y, WithinAbs(64.0, 1e-3));
}

TEST_CASE("A drop in a TileView goes by line first, and below the last line at the end",
          "[list-view-base][grid-view][drag]")
{
    TileViewControl grid;
    grid.set_item_template([](std::size_t) { return std::make_unique<Tile>(Size{96.0f, 60.0f}); });
    grid.set_item_count(8);
    layout(grid, {420.0f, 400.0f}); // four cells to a line: two full lines

    const Rect five = item_rect(grid, 5);
    // Level with a line, the middle of each cell along it decides.
    CHECK(grid.insertion_index_at(Point{five.x + 5.0f, five.center().y}) == 5);
    CHECK(grid.insertion_index_at(Point{five.right() - 5.0f, five.center().y}) == 6);
    // Past the end of a full line: after its last item.
    const Rect three = item_rect(grid, 3);
    CHECK(grid.insertion_index_at(Point{three.right() + 10.0f, three.center().y}) == 4);
    // Well below the last line, under item 5: the end, not after item 5.
    CHECK(grid.insertion_index_at(Point{five.center().x, five.bottom() + 60.0f}) == 8);

    // A last line with room left: level with it, past its last item is the end too.
    grid.set_item_count(6);
    layout(grid, {420.0f, 400.0f});
    CHECK(grid.insertion_index_at(Point{item_rect(grid, 3).center().x, five.center().y}) == 6);
    CHECK(grid.insertion_index_at(Point{five.center().x, five.bottom() + 60.0f}) == 6);
}

TEST_CASE("Items a drag moved aside slide back when it leaves, and jump into place on a drop",
          "[list-view-base][grid-view][drag]")
{
    TileViewControl grid;
    grid.set_item_template([](std::size_t) { return std::make_unique<Tile>(Size{96.0f, 60.0f}); });
    grid.set_item_count(8);
    grid.set_can_reorder_items(true);
    grid.set_allow_drop(true);
    grid.on_drag_over([] { return DragResponse{.operation = DataPackageOperation::Copy}; });
    layout(grid, {420.0f, 400.0f});

    DataPackage package;
    package.files = {"page.pdf"};
    const DragEventArgs over{package, Point{item_rect(grid, 1).x + 5.0f, item_rect(grid, 1).center().y}};
    const auto          item = [&](std::size_t i) { return grid.repeater()->try_get_element(i); };

    // Moving on to another drop target: the items slide back from the gap.
    grid.drag_over(over);
    grid.drag_leave(over);
    CHECK(item(1)->translation().x == 0.0f);
    CHECK(item(1)->translation_transition().has_value());

    // A drop: the items take their new cells at once.
    grid.drag_over(over);
    grid.drop(over);
    CHECK(item(1)->translation().x == 0.0f);
    CHECK_FALSE(item(1)->translation_transition().has_value());
}

TEST_CASE("A TileView lays its items out in cells the first item's size", "[list-view-base][grid-view]")
{
    TileViewControl grid;
    grid.set_item_template([](std::size_t) { return std::make_unique<Tile>(Size{96.0f, 60.0f}); });
    grid.set_item_count(10);
    layout(grid, {420.0f, 400.0f});
    // A cell is the item plus a tile's 0,0,4,4 margin: 100 x 64; four across in 420.
    CHECK_THAT(item_rect(grid, 1).x - item_rect(grid, 0).x, WithinAbs(100.0, 1e-3));
    CHECK_THAT(item_rect(grid, 4).y - item_rect(grid, 0).y, WithinAbs(64.0, 1e-3));
    CHECK_THAT(item_rect(grid, 4).x, WithinAbs(item_rect(grid, 0).x, 1e-3));
    grid.set_maximum_rows_or_columns(3);
    layout(grid, {420.0f, 400.0f});
    CHECK_THAT(item_rect(grid, 3).y - item_rect(grid, 0).y, WithinAbs(64.0, 1e-3));

    // Selected: the 2px accent frame over the content.
    grid.set_selected_index(0);
    TestRenderer r;
    grid.render(r);
    const Theme theme = Theme::light();
    bool        frame = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* s = std::get_if<StrokeRoundedRect>(&c); s != nullptr && s->stroke_width == 2.0f)
        {
            frame = s->color == theme.accent_button_normal.background;
        }
    }
    CHECK(frame);
}

TEST_CASE("A TileView sizes a cell holding a Grid to the Grid's content", "[list-view-base][grid-view]")
{
    TileViewControl grid;
    grid.set_item_template(
        [](std::size_t)
        {
            return ui::Spec{ui::VerticalStack{
                                .content =
                                    {
                                        ui::Grid{.content = {ui::Cell{.content = ui::Border{.width = 96, .height = 60}},
                                                             ui::Cell{.content = ui::Border{
                                                                          .horizontal_alignment = Alignment::Leading,
                                                                          .vertical_alignment   = Alignment::Leading,
                                                                          .content              = ui::Text{.text = "3"},
                                                                      }}}},
                                        ui::Text{.text = "Page", .width = 96},
                                    },
                            }}
                .take();
        });
    grid.set_item_count(10);
    layout(grid, {420.0f, 400.0f});
    CHECK_THAT(item_rect(grid, 0).width, WithinAbs(96.0, 1e-3));
    CHECK_THAT(item_rect(grid, 1).x - item_rect(grid, 0).x, WithinAbs(100.0, 1e-3)); // four across
}

TEST_CASE("Dragging an item within a list reorders it", "[list-view-base][drag]")
{
    HeadlessUI       ui   = create_headless();
    auto             own  = std::make_unique<ListViewControl>();
    ListViewControl* list = own.get();
    list->set_items({"A", "B", "C", "D", "E"});
    list->set_item_height(40.0f);
    list->set_can_drag_items(true);
    list->set_can_reorder_items(true);
    list->set_allow_drop(true);
    list->set_height(300.0f);
    list->set_width(200.0f);
    std::vector<std::pair<std::size_t, std::size_t>> reordered;
    list->on_items_reordered([&](const ItemsReorderedArgs& args) { reordered.emplace_back(args.from, args.to); });
    ui.window().set_content(std::move(own));
    ui.layout({400.0f, 400.0f});

    Window&     w    = ui.window();
    const Point from = item_rect(*list, 0).center();
    ui::testing::seams(w).handle_pointer({from, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(w).handle_pointer({{from.x, from.y + 10.0f}, PointerButton::None, PointerAction::Move});
    REQUIRE(DragDrop::active());
    // Past the middle of D: it goes after D.
    const Rect d = item_rect(*list, 3);
    ui::testing::seams(w).handle_pointer({{from.x, d.center().y + 5.0f}, PointerButton::None, PointerAction::Move});
    ui::testing::seams(w).handle_pointer({{from.x, d.center().y + 5.0f}, PointerButton::Left, PointerAction::Up});
    CHECK_FALSE(DragDrop::active());
    CHECK(list->items() == std::vector<std::string>{"B", "C", "D", "A", "E"});
    CHECK(reordered == std::vector<std::pair<std::size_t, std::size_t>>{{0, 3}});
}

TEST_CASE("A drag held near the end of a list scrolls it, and stops at the end", "[list-view-base][drag]")
{
    HeadlessUI       ui   = create_headless();
    auto             own  = std::make_unique<ListViewControl>();
    ListViewControl* list = own.get();
    list->set_items(names(30));
    list->set_item_height(40.0f);
    list->set_can_drag_items(true);
    list->set_can_reorder_items(true);
    list->set_allow_drop(true);
    list->set_height(300.0f);
    list->set_width(200.0f);
    ui.window().set_content(std::move(own));
    ui.layout({400.0f, 400.0f});

    Window&     w    = ui.window();
    const Point from = item_rect(*list, 0).center();
    ui::testing::seams(w).handle_pointer({from, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(w).handle_pointer({{from.x, from.y + 10.0f}, PointerButton::None, PointerAction::Move});
    REQUIRE(DragDrop::active());

    // Held still in the middle: nothing scrolls.
    w.tick(100ms);
    CHECK(list->scroll_offset() == 0.0f);

    // Held still at the bottom rim: it scrolls a little in each tick, without another move.
    const float bottom = list->bounds().bottom() - 2.0f;
    ui::testing::seams(w).handle_pointer({{from.x, bottom}, PointerButton::None, PointerAction::Move});
    w.tick(100ms);
    const float once = list->scroll_offset();
    CHECK(once > 0.0f);
    w.tick(100ms);
    CHECK(list->scroll_offset() > once);
    for (int i = 0; i < 40; ++i)
    {
        w.tick(100ms);
        ui.layout({400.0f, 400.0f});
    }
    const float end = list->scroll_offset();
    w.tick(100ms);
    CHECK(list->scroll_offset() == end); // at the end

    // Canceled, it stops.
    (void)ui::testing::seams(w).handle_key({Key::Escape, KeyAction::Down});
    CHECK_FALSE(DragDrop::active());
    ui::testing::seams(w).handle_pointer({{from.x, list->bounds().top() + 2.0f}, PointerButton::None, PointerAction::Move});
    w.tick(100ms);
    CHECK(list->scroll_offset() == end);
}

TEST_CASE("A drag between two lists reaches the target's drop handler", "[list-view-base][drag]")
{
    HeadlessUI ui    = create_headless();
    auto       stack = std::make_unique<StackControl>(Orientation::Horizontal);
    auto*      left  = stack->add(std::make_unique<ListViewControl>());
    auto*      right = stack->add(std::make_unique<ListViewControl>());
    for (ListViewControl* l : {left, right})
    {
        l->set_item_height(40.0f);
        l->set_width(150.0f);
        l->set_height(200.0f);
        l->set_can_drag_items(true);
        l->set_allow_drop(true);
    }
    left->set_items({"One", "Two"});
    right->set_items({"Three"});
    right->on_drag_over([] { return DragResponse{.operation = DataPackageOperation::Move}; });
    right->on_drop(
        [&](const DragEventArgs& e)
        {
            const auto* payload = std::any_cast<ListViewDragPayload>(&e.data.data);
            REQUIRE(payload != nullptr);
            REQUIRE(payload->source == left);
            const std::size_t at = right->insertion_index_at(e.position);
            right->insert_item(at, left->items()[payload->items.front()]);
            left->remove_item(payload->items.front());
        });
    std::optional<DataPackageOperation> completed;
    left->on_drag_items_completed([&](const DragItemsCompletedArgs& args) { completed = args.result; });
    ui.window().set_content(std::move(stack));
    ui.layout({400.0f, 300.0f});

    Window&     w    = ui.window();
    const Point from = item_rect(*left, 1).center();
    ui::testing::seams(w).handle_pointer({from, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(w).handle_pointer({{from.x + 10.0f, from.y}, PointerButton::None, PointerAction::Move});
    const Point to{item_rect(*right, 0).center().x, item_rect(*right, 0).y + 5.0f}; // before "Three"
    ui::testing::seams(w).handle_pointer({to, PointerButton::None, PointerAction::Move});
    ui::testing::seams(w).handle_pointer({to, PointerButton::Left, PointerAction::Up});
    CHECK(left->items() == std::vector<std::string>{"One"});
    CHECK(right->items() == std::vector<std::string>{"Two", "Three"});
    REQUIRE(completed.has_value());
    CHECK(*completed == DataPackageOperation::Move);
}

TEST_CASE("Escape cancels a drag, and the source hears it ended with nothing done", "[list-view-base][drag]")
{
    HeadlessUI       ui   = create_headless();
    auto             own  = std::make_unique<ListViewControl>();
    ListViewControl* list = own.get();
    list->set_items({"A", "B"});
    list->set_item_height(40.0f);
    list->set_can_drag_items(true);
    list->set_height(200.0f);
    std::optional<DataPackageOperation> completed;
    list->on_drag_items_completed([&](const DragItemsCompletedArgs& args) { completed = args.result; });
    ui.window().set_content(std::move(own));
    ui.layout({300.0f, 300.0f});
    Window&     w    = ui.window();
    const Point from = item_rect(*list, 0).center();
    ui::testing::seams(w).handle_pointer({from, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(w).handle_pointer({{from.x, from.y + 20.0f}, PointerButton::None, PointerAction::Move});
    REQUIRE(DragDrop::active());
    ui::testing::seams(w).handle_key(KeyEvent{Key::Escape, KeyAction::Down});
    CHECK_FALSE(DragDrop::active());
    REQUIRE(completed.has_value());
    CHECK(*completed == DataPackageOperation::None);
    CHECK(list->items() == std::vector<std::string>{"A", "B"});
}

TEST_CASE("Group keys gather items in first-appearance order without sorting", "[list-view-base][groups]")
{
    ListViewControl list;
    list.set_items({"apple", "banana", "avocado", "cherry", "blueberry"});
    list.set_group_keys({"a", "b", "a", "c", "b"});
    layout(list, {300.0f, 400.0f});

    REQUIRE(list.group_count() == 3);
    CHECK(list.group_key(0) == "a");
    CHECK(list.group_key(1) == "b");
    CHECK(list.group_key(2) == "c");
    // On screen: header a, apple, avocado, header b, banana, blueberry, header c, cherry.
    const auto shown = [&](std::size_t flat)
    {
        const Element* e = list.repeater()->try_get_element(flat);
        REQUIRE(e != nullptr);
        return list.index_from_element(*e);
    };
    CHECK(shown(0) == std::nullopt);
    CHECK(shown(1) == 0u);
    CHECK(shown(2) == 2u);
    CHECK(shown(3) == std::nullopt);
    CHECK(shown(4) == 1u);
    CHECK(shown(5) == 4u);
    CHECK(shown(7) == 3u);
    CHECK(item_rect(list, 1).y < item_rect(list, 4).y);

    // The arrow keys follow the screen, not the collection.
    list.set_selected_index(2); // avocado
    ui::testing::set_focused(list, true);
    list.handle_key(KeyEvent{Key::ArrowDown, KeyAction::Down});
    CHECK(list.selected_index() == 1); // banana, under the b header
    list.handle_key(KeyEvent{Key::End, KeyAction::Down});
    CHECK(list.selected_index() == 3); // cherry is last on screen
    list.handle_key(KeyEvent{Key::Home, KeyAction::Down});
    CHECK(list.selected_index() == 0);

    // A Shift range runs over what is on screen between the two: from avocado (the anchor) down
    // to blueberry takes banana with it, not cherry.
    list.set_selection_mode(SelectionMode::Extended);
    list.set_selected_index(2);                                 // the anchor; the keys are on apple
    list.handle_key(KeyEvent{Key::ArrowDown, KeyAction::Down}); // avocado
    list.handle_key(KeyEvent{Key::ArrowDown, KeyAction::Down, {.shift = true}});
    list.handle_key(KeyEvent{Key::ArrowDown, KeyAction::Down, {.shift = true}});
    CHECK(list.selected_items() == std::vector<std::size_t>{1, 2, 4});

    list.set_group_keys({});
    CHECK(list.group_count() == 0);
}

TEST_CASE("A grouped TileView puts each header on a line of its own", "[list-view-base][groups][tile-view]")
{
    TileViewControl tiles;
    tiles.set_items({"a1", "b1", "a2", "a3", "b2"});
    tiles.set_item_width(100.0f);
    tiles.set_item_height_cell(60.0f);
    tiles.set_group_keys({"a", "b", "a", "a", "b"});
    layout(tiles, {250.0f, 600.0f}); // two cells to a line

    // header a, a1, a2, a3, header b, b1, b2
    const Element* header_a = tiles.repeater()->try_get_element(0);
    REQUIRE(header_a != nullptr);
    const Element* a1 = tiles.repeater()->try_get_element(1);
    const Element* a2 = tiles.repeater()->try_get_element(2);
    const Element* a3 = tiles.repeater()->try_get_element(3);
    const Element* b1 = tiles.repeater()->try_get_element(5);
    REQUIRE((a1 != nullptr && a2 != nullptr && a3 != nullptr && b1 != nullptr));
    CHECK(a1->bounds().y > header_a->bounds().y);
    CHECK(a2->bounds().y == a1->bounds().y); // a1 and a2 share the first line
    CHECK(a2->bounds().x > a1->bounds().x);
    CHECK(a3->bounds().y > a1->bounds().y); // a3 wraps
    CHECK(a3->bounds().x == a1->bounds().x);
    // Group b starts below group a, with its header between them.
    const Element* header_b = tiles.repeater()->try_get_element(4);
    REQUIRE(header_b != nullptr);
    CHECK(header_b->bounds().y > a3->bounds().y);
    CHECK(b1->bounds().y > header_b->bounds().y);
    CHECK(b1->bounds().x == a1->bounds().x);
}
