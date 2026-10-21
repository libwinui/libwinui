// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/collections/TableView.hpp"
#include "winui/ui/collections/TreeView.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/menus/ContextMenu.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"

#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

namespace
{
void press(HeadlessUI& ui, Key key)
{
    seams(ui.window()).handle_key(KeyEvent{key, KeyAction::Down});
    seams(ui.window()).handle_key(KeyEvent{key, KeyAction::Up});
    ui.layout({600, 400});
}

void click(HeadlessUI& ui, Point p)
{
    seams(ui.window()).handle_pointer({p, PointerButton::Left, PointerAction::Down});
    seams(ui.window()).handle_pointer({p, PointerButton::Left, PointerAction::Up});
    ui.layout({600, 400});
}

// A right-click at `at`, then a layout so an opened menu is placed and focused.
bool right_click(HeadlessUI& ui, std::optional<Point> at)
{
    const bool taken = seams(ui.window()).handle_context_menu(at);
    ui.layout({600, 400});
    return taken;
}

struct Row
{
    std::string name;
    friend bool operator==(const Row&, const Row&) = default;
};
} // namespace

TEST_CASE("A ContextMenu opens at the pointer on a right-click, also over content that takes no focus", "[context-menu]")
{
    HeadlessUI                   ui;
    std::string                  picked;
    ui::Control<ui::ContextMenu> menu;
    ui::Control<ui::Text>        label;
    ui.window().set_content(ui::VerticalStack{
        .content = {ui::ContextMenu{
            .items   = {{.text = "Copy", .on_click = [&] { picked = "copy"; }}, {.text = "Paste"}},
            .control = menu,
            .content = ui::Text{.text = "A picture", .height = 40.0f, .control = label},
        }},
    });
    ui.layout({600, 400});

    const Point at = label->bounds().center();
    REQUIRE(right_click(ui, at));
    REQUIRE(menu->is_open());
    CHECK(ui::testing::popup_content(menu->menu())->bounds().left() == at.x);
    CHECK(ui::testing::popup_content(menu->menu())->bounds().top() == at.y);

    press(ui, Key::Enter); // the first item has focus as the menu opens
    CHECK(picked == "copy");
    CHECK_FALSE(menu->is_open());
}

TEST_CASE("A ContextMenu opens for Shift+F10 over the focused element, and a left-click does not open it",
          "[context-menu]")
{
    HeadlessUI                   ui;
    int                          clicks = 0;
    ui::Control<ui::ContextMenu> menu;
    ui::Control<ui::Button>      button;
    ui.window().set_content(ui::VerticalStack{
        .content = {ui::ContextMenu{
            .items   = {{.text = "Rename"}},
            .control = menu,
            .content = ui::Button{.text = "File", .on_click = [&] { ++clicks; }, .control = button},
        }},
    });
    ui.layout({600, 400});

    click(ui, button->bounds().center());
    CHECK(clicks == 1);
    CHECK_FALSE(menu->is_open());

    REQUIRE(ui.window().focused() == button.get());
    REQUIRE(right_click(ui, std::nullopt));
    REQUIRE(menu->is_open());
    CHECK(ui::testing::popup_content(menu->menu())->bounds().left() == button->bounds().center().x);
}

TEST_CASE("A ContextMenu with no items opens nothing, and a handler inside it comes first", "[context-menu]")
{
    HeadlessUI                   ui;
    ui::Control<ui::ContextMenu> empty;
    ui::Control<ui::ContextMenu> outer;
    ui::Control<ui::Text>        plain;
    ui::Control<ui::Text>        handled;
    bool                         asked = false;
    ui.window().set_content(ui::VerticalStack{
        .content =
            {
                ui::ContextMenu{.control = empty, .content = ui::Text{.text = "plain", .height = 40.0f, .control = plain}},
                ui::ContextMenu{.items   = {{.text = "Outer"}},
                                .control = outer,
                                .content = ui::Text{.text = "inner", .height = 40.0f, .control = handled}},
            },
    });
    ui.layout({600, 400});
    handled->on_context_requested([&](std::optional<Point>) { asked = true; });

    CHECK_FALSE(right_click(ui, plain->bounds().center()));
    CHECK_FALSE(empty->is_open());

    REQUIRE(right_click(ui, handled->bounds().center()));
    CHECK(asked);
    CHECK_FALSE(outer->is_open());
}

TEST_CASE("A ListView's item_context_menu gets the item and selects it", "[context-menu][list-view]")
{
    HeadlessUI                             ui;
    std::string                            asked;
    std::string                            deleted;
    ui::Control<ui::ListView<std::string>> view;
    ui.window().set_content(ui::ListView<std::string>{
        .items             = {"a", "b", "c"},
        .height            = 300.0f,
        .control           = view,
        .item_context_menu = [&](const std::string& name) -> std::vector<MenuItem>
        {
            asked = name;
            if (name == "c")
            {
                return {}; // no menu for this one
            }
            return {{.text = "Delete", .on_click = [&deleted, name] { deleted = name; }}};
        },
    });
    ui.layout({600, 400});
    const Rect list = view->bounds();

    REQUIRE(right_click(ui, Point{list.left() + 20.0f, list.top() + 60.0f})); // the second row
    CHECK(asked == "b");
    CHECK(view->selected_index() == 1);
    press(ui, Key::Enter);
    CHECK(deleted == "b");

    // The current item for the keyboard.
    REQUIRE(right_click(ui, std::nullopt));
    press(ui, Key::Enter);
    CHECK(deleted == "b");

    CHECK_FALSE(right_click(ui, Point{list.left() + 20.0f, list.top() + 100.0f})); // "c" has none
    CHECK(asked == "c");
}

TEST_CASE("A TreeView's item_context_menu gets the node and selects it", "[context-menu][tree-view]")
{
    HeadlessUI                          ui;
    std::string                         ran;
    ui::Control<ui::TreeView<TreeNode>> tree;
    ui.window().set_content(ui::TreeView<TreeNode>{
        .items             = std::vector<TreeNode>{TreeNode{.text = "Documents"}, TreeNode{.text = "Pictures"}},
        .height            = 300.0f,
        .control           = tree,
        .item_context_menu = [&](const TreeNode& node) -> std::vector<MenuItem>
        { return {{.text = "Open", .on_click = [&ran, text = node.text] { ran = text; }}}; },
    });
    ui.layout({600, 400});
    REQUIRE(tree->row_height() > 0.0f);
    const Rect bounds = tree->bounds();

    REQUIRE(right_click(ui, Point{bounds.left() + 40.0f, bounds.top() + tree->row_height() * 1.5f}));
    REQUIRE(tree->selected() != nullptr);
    CHECK(tree->selected()->text == "Pictures");
    press(ui, Key::Enter);
    CHECK(ran == "Pictures");
}

TEST_CASE("A TableView's item_context_menu gets the row's item and selects it", "[context-menu][data-grid]")
{
    HeadlessUI                      ui;
    std::string                     ran;
    ui::Control<ui::TableView<Row>> table;
    ui.window().set_content(ui::TableView<Row>{
        .items             = std::vector<Row>{{"alpha"}, {"beta"}, {"gamma"}},
        .columns           = {ui::TableColumn<Row>{.header = "Name", .text = [](const Row& r) { return r.name; }}},
        .row_height        = 20.0f,
        .height            = 300.0f,
        .control           = table,
        .item_context_menu = [&](const Row& row) -> std::vector<MenuItem>
        { return {{.text = "Open", .on_click = [&ran, name = row.name] { ran = name; }}}; },
    });
    ui.layout({600, 400});
    const Rect body = table->bounds();

    // The header is 40 tall; the second row starts 20 below it.
    REQUIRE(right_click(ui, Point{body.left() + 30.0f, body.top() + 40.0f + 30.0f + 3.0f}));
    CHECK(table->selected_row() == 1);
    press(ui, Key::Enter);
    CHECK(ran == "beta");
}
