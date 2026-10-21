// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/collections/TileView.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
std::vector<std::string> make_items(std::size_t n)
{
    std::vector<std::string> items;
    items.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        items.push_back("Item " + std::to_string(i));
    }
    return items;
}

KeyEvent key_down(Key k)
{
    return {k, KeyAction::Down};
}
} // namespace

TEST_CASE("ListView measures a bounded height as given when the items overflow it", "[list-view]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ListViewControl view;
    view.set_items(make_items(50));
    view.set_item_height(20.0f);

    const Size desired = view.measure({300, 200}, m, theme);
    CHECK(desired.width <= 300.0f);
    CHECK_THAT(desired.height, WithinAbs(200.0, 1e-3));
}

TEST_CASE("ListView with a few items asks for their height only", "[list-view]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ListViewControl view;
    view.set_items(make_items(3));
    view.set_item_height(20.0f);

    const Size desired = view.measure({300, 200}, m, theme);
    CHECK_THAT(desired.height, WithinAbs(60.0, 1e-3));
}

TEST_CASE("ListView measures unbounded height as item_count * item_height", "[list-view]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ListViewControl view;
    view.set_items(make_items(30));
    view.set_item_height(20.0f);

    const Size desired = view.measure({300, Unbounded}, m, theme);
    REQUIRE_THAT(desired.height, WithinAbs(30.0 * 20.0, 1e-3));
}

TEST_CASE("set_items selects nothing, and clears a selection with one change event", "[list-view]")
{
    ListViewControl view;
    view.set_items(make_items(10));
    REQUIRE(view.selected_index() == std::nullopt);

    std::vector<std::optional<std::size_t>> changes;
    view.on_selection_changed([&](std::optional<std::size_t> i) { changes.push_back(i); });
    view.set_selected_index(3);
    view.set_items(make_items(4));
    CHECK(view.selected_index() == std::nullopt);
    CHECK(changes == std::vector<std::optional<std::size_t>>{3, std::nullopt});

    view.set_items({}); // nothing was selected: no event
    CHECK(changes.size() == 2);
}

TEST_CASE("set_selected_index clamps and fires on_selection_changed only on an actual change", "[list-view]")
{
    ListViewControl view;
    view.set_items(make_items(10));
    view.set_selected_index(0);

    int                        calls = 0;
    std::optional<std::size_t> last;
    view.on_selection_changed(
        [&](std::optional<std::size_t> i)
        {
            ++calls;
            last = i;
        });

    view.set_selected_index(0); // already selected: no-op
    REQUIRE(calls == 0);

    view.set_selected_index(5);
    REQUIRE(calls == 1);
    REQUIRE(last == 5);
    REQUIRE(view.selected_index() == 5);

    view.set_selected_index(1000); // clamped to the last item
    REQUIRE(view.selected_index() == 9);
    REQUIRE(calls == 2);
}

TEST_CASE("None deselects instead of clamping", "[list-view]")
{
    ListViewControl view;
    view.set_items(make_items(10));
    view.set_selected_index(4);

    int                        calls = 0;
    std::optional<std::size_t> last{0};
    view.on_selection_changed(
        [&](std::optional<std::size_t> i)
        {
            ++calls;
            last = i;
        });

    view.set_selected_index(std::nullopt);
    REQUIRE(view.selected_index() == std::nullopt);
    REQUIRE(calls == 1);
    REQUIRE(last == std::nullopt);

    view.set_selected_index(std::nullopt); // already deselected: no-op
    REQUIRE(calls == 1);

    view.set_selected_index(2); // and selecting again still works
    REQUIRE(view.selected_index() == 2);
    REQUIRE(calls == 2);
}

TEST_CASE("a spec ListView with no .selected_index selects nothing", "[list-view][spec]")
{
    // build() applies .selected_index after set_items() seeded 0, so the None default selects nothing.
    ui::Control<ui::ListView<std::string>> raw;
    std::unique_ptr<Element> node = ui::build(ui::ListView<std::string>{.items = {"a", "b", "c"}, .control = raw});
    REQUIRE(raw->selected_index() == std::nullopt);
}

TEST_CASE("a two-way .selected_index follows the selected item as the collection changes around it",
          "[list-view][spec][binding]")
{
    ObservableVector<std::string>          names{"a", "b", "c", "d"};
    Property<std::optional<std::size_t>>   selected{2};
    int                                    changes = 0;
    ui::Control<ui::ListView<std::string>> view;
    std::unique_ptr<Element>               node = ui::build(
        ui::ListView<std::string>{.items                = ui::bind(names),
                                  .selected_index       = ui::bind(selected),
                                  .on_selection_changed = [&] { ++changes; },
                                  .control              = view});
    REQUIRE(view->selected_index() == 2);

    names.insert(0, "z"); // c is now at 3
    CHECK(view->selected_index() == 3);
    CHECK(selected.get() == 3);

    names.erase(0); // back to 2
    CHECK(view->selected_index() == 2);
    CHECK(selected.get() == 2);

    names.move(2, 0); // c to the front
    CHECK(view->selected_index() == 0);
    CHECK(selected.get() == 0);

    names.move(3, 1); // d in front of a: c stays at 0
    CHECK(selected.get() == 0);
    CHECK(changes == 0); // the same item stayed selected throughout: no selection change
}

TEST_CASE("Arrow keys move the selection one at a time, clamped (not wrapping)", "[list-view][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ListViewControl view;
    view.set_items(make_items(5));
    view.set_selected_index(0);
    view.set_item_height(20.0f);
    (void)view.measure({200, 100}, m, theme);
    view.arrange({0, 0, 200, 100});
    ui::testing::set_focused(view, true);

    view.handle_key(key_down(Key::ArrowUp)); // already at 0: clamps, stays 0
    REQUIRE(view.selected_index() == 0);

    view.handle_key(key_down(Key::ArrowDown));
    REQUIRE(view.selected_index() == 1);

    view.handle_key(key_down(Key::End));
    REQUIRE(view.selected_index() == 4);

    view.handle_key(key_down(Key::ArrowDown)); // clamped at the end
    REQUIRE(view.selected_index() == 4);

    view.handle_key(key_down(Key::Home));
    REQUIRE(view.selected_index() == 0);
}

TEST_CASE("Disabled or unfocused ListView ignores key input", "[list-view][input]")
{
    ListViewControl view;
    view.set_items(make_items(5));
    view.set_selected_index(0);

    view.handle_key(key_down(Key::ArrowDown)); // not focused
    REQUIRE(view.selected_index() == 0);

    ui::testing::set_focused(view, true);
    view.set_enabled(false);
    view.handle_key(key_down(Key::ArrowDown));
    REQUIRE(view.selected_index() == 0);
}

TEST_CASE("Selecting an off-screen item scrolls it into view", "[list-view][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ListViewControl view;
    view.set_items(make_items(100));
    view.set_item_height(20.0f);
    (void)view.measure({200, 100}, m, theme); // ~100px viewport / 20px rows -> ~5 visible
    view.arrange({0, 0, 200, 100});
    REQUIRE_THAT(view.scroll_offset(), WithinAbs(0.0, 1e-3));

    const float viewport_height = 100.0f;

    view.set_selected_index(50);
    // Item 50 occupies [1000, 1020); the viewport must have scrolled so its bottom is
    // visible, i.e. offset == 1020 - viewport_height.
    REQUIRE_THAT(view.scroll_offset(), WithinAbs(1020.0 - viewport_height, 1e-3));

    view.set_selected_index(0);
    REQUIRE_THAT(view.scroll_offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Clicking a realized row selects it", "[list-view][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ListViewControl view;
    view.set_items(make_items(20));
    view.set_item_height(20.0f);
    (void)view.measure({200, 100}, m, theme);
    view.arrange({0, 0, 200, 100});

    int calls = 0;
    view.on_selection_changed([&](std::optional<std::size_t>) { ++calls; });

    // Row 2 occupies y in [40, 60).
    const Point p{10.0f, 45.0f};
    view.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    view.handle_pointer({p, PointerButton::Left, PointerAction::Up});

    REQUIRE(view.selected_index() == 2);
    REQUIRE(calls == 1);
}

TEST_CASE("A release with no matching press on the same row selects nothing", "[list-view][input]")
{
    // A row captures the pointer on press: a bare release is someone else's click ending over the
    // list and must not move the selection.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ListViewControl view;
    view.set_items(make_items(20));
    view.set_item_height(20.0f);
    (void)view.measure({200, 100}, m, theme);
    view.arrange({0, 0, 200, 100});
    view.set_selected_index(0);

    int calls = 0;
    view.on_selection_changed([&](std::optional<std::size_t>) { ++calls; });

    const Point p{10.0f, 45.0f}; // row 2
    view.handle_pointer({p, PointerButton::Left, PointerAction::Up});
    CHECK(view.selected_index() == 0);
    CHECK(calls == 0);

    view.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    view.handle_pointer({{10.0f, 85.0f}, PointerButton::Left, PointerAction::Up});
    CHECK(view.selected_index() == 0);
    CHECK(calls == 0);
}

TEST_CASE("ListView only realizes (and renders) the visible rows out of a large item count", "[list-view][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ListViewControl view;
    view.set_items(make_items(100'000));
    view.set_item_height(20.0f);
    (void)view.measure({200, 105}, m, theme); // ~6 visible rows
    view.arrange({0, 0, 200, 105});

    TestRenderer r;
    view.render(r, theme);

    // One DrawTextRun per realized row: the ~6 in view, ForEach's cache below and one past the
    // edge. A few dozen at most.
    REQUIRE(r.count_of<DrawTextRun>() > 0);
    REQUIRE(r.count_of<DrawTextRun>() < 20);
}

TEST_CASE("A ListView with no items is not focusable", "[list-view][focus]")
{
    ListViewControl view;
    REQUIRE_FALSE(view.focusable());

    view.set_items(make_items(3));
    REQUIRE(view.focusable());

    view.set_enabled(false);
    REQUIRE_FALSE(view.focusable());
}

TEST_CASE(
    "Default item height (ListViewItemMinHeight) is honored without an explicit "
    "set_item_height call",
    "[list-view][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ListViewControl view;
    view.set_items(make_items(10));

    const Size desired = view.measure({200, Unbounded}, m, theme);
    REQUIRE_THAT(desired.height, WithinAbs(10.0 * 40.0, 1e-3));
}

TEST_CASE("A selected row draws the accent selection pill, not a full-row highlight fill", "[list-view][render]")
{
    // Selection is an accent pill over a faint wash.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    REQUIRE(ui::detail::controls(theme).list_item_subtle_selection);

    ListViewControl view;
    view.set_items(make_items(3));
    view.set_selected_index(0);
    view.set_item_height(20.0f);
    // Tall enough not to scroll, or the overlay scroll bar adds FillRoundedRect commands.
    (void)view.measure({200, 200}, m, theme);
    view.arrange({0, 0, 200, 200});

    TestRenderer r;
    view.render(r, theme);

    // Row 0 draws its background chip and the accent pill. Rows 1 and 2 draw nothing, because
    // the transparent subtle fill has zero alpha.
    REQUIRE(r.count_of<FillRoundedRect>() == 2);
    bool found_pill = false;
    for (const auto& cmd : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&cmd))
        {
            if (fill->bounds.width == 3.0f && fill->bounds.height == 16.0f)
            {
                found_pill = true;
            }
        }
    }
    REQUIRE(found_pill);
}

TEST_CASE("A right-click on a row asks the menu anchor in it although the list has focus", "[list-view][context]")
{
    ui::testing::HeadlessUI                ui = ui::testing::create_headless();
    std::string                            asked;
    ui::Control<ui::ListView<std::string>> view;
    ui.window().set_content(ui::build(
        ui::ListView<std::string>{.items   = {"a", "b", "c"},
                                  .width   = 200,
                                  .height  = 300,
                                  .control = view,
                                  .item    = [&asked](const std::string& name) -> ui::Spec
                                  {
                                      // The row is a menu's anchor, as a context flyout on an item template is.
                                      auto         menu = std::make_unique<MenuFlyoutControl>();
                                      ui::Element* row  = menu->set_anchor(ui::build(ui::Text{.text = name}));
                                      row->on_context_requested([&asked, name](std::optional<Point>) { asked = name; });
                                      return menu;
                                  }}));
    ui.layout({400, 400});

    const Rect  list = view->bounds();
    const Point at{list.left() + 20.0f, list.top() + 12.0f};
    REQUIRE(ui::testing::seams(ui.window()).handle_context_menu(at));
    CHECK(asked == "a");
}

TEST_CASE("ui::ListView and ui::TileView regroup by key as the collection changes", "[spec][collections]")
{
    struct Fruit
    {
        std::string name;
        std::string color;
    };
    ObservableVector<Fruit>          fruit{{"apple", "red"}, {"banana", "yellow"}, {"cherry", "red"}};
    ui::Control<ui::ListView<Fruit>> list;
    ui::Control<ui::TileView<Fruit>> tiles;
    std::vector<std::string>         headers;
    std::unique_ptr<Element>         tree = ui::build(ui::ListView{
        .items                 = ui::bind(fruit),
        .group_by              = [](const Fruit& f) { return f.color; },
        .control               = list,
        .item                  = [](const Fruit& f) -> ui::Spec { return ui::Text{.text = f.name}; },
        .group_header_template = [&](const std::string& key) -> ui::Spec
        {
            headers.push_back(key);
            return ui::Text{.text = "Color " + key};
        },
    });
    std::unique_ptr<Element>         grid = ui::build(ui::TileView{
        .items    = ui::bind(fruit),
        .group_by = [](const Fruit& f) { return f.color; },
        .control  = tiles,
        .item     = [](const Fruit& f) -> ui::Spec { return ui::Text{.text = f.name}; },
    });
    CHECK(list->group_count() == 2);
    CHECK(list->group_key(0) == "red");
    CHECK(tiles->group_count() == 2);

    fruit.push_back({"lime", "green"});
    CHECK(list->group_count() == 3);
    CHECK(list->group_key(2) == "green");
    CHECK(tiles->group_key(2) == "green");
    fruit.erase(1); // the only yellow one
    CHECK(list->group_count() == 2);
    CHECK(list->group_key(1) == "green");

    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light(); // arrange() reads the theme measure() was given
    (void)tree->measure({300.0f, 400.0f}, measurer, theme);
    tree->arrange(Rect{0.0f, 0.0f, 300.0f, 400.0f});
    CHECK(std::ranges::find(headers, "red") != headers.end()); // the template gets the key
}
