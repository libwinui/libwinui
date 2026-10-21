// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/DragDrop.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/TreeView.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
PointerEvent move(Point p)
{
    return {p, PointerButton::None, PointerAction::Move};
}
PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}

// A -> {A1 (leaf), A2 -> {A2a (leaf)}}, B (leaf). A starts expanded, A2 starts collapsed,
// so the initial visible rows are [A, A1, A2, B] (A2a hidden until A2 expands).
std::vector<TreeNode> make_tree()
{
    TreeNode a2a{.text = "A2a", .children = {}, .expanded = false};
    TreeNode a2{.text = "A2", .children = {a2a}, .expanded = false};
    TreeNode a1{.text = "A1", .children = {}, .expanded = false};
    TreeNode a{.text = "A", .children = {a1, a2}, .expanded = true};
    TreeNode b{.text = "B", .children = {}, .expanded = false};
    return {a, b};
}

TreeViewControl make_tree_view()
{
    TreeViewControl tv;
    tv.set_roots(make_tree());
    HeadlessTextMeasurer measurer;
    const Size           desired = tv.measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    tv.arrange({100.0f, 100.0f, desired.width, desired.height});
    return tv;
}

// A point inside row `index`'s chevron gutter, given that row's known depth.
Point chevron_point(const TreeViewControl& tv, std::size_t index, std::size_t depth)
{
    const Rect  b      = tv.bounds();
    const float indent = static_cast<float>(depth) * tv.indent_width();
    return Point{b.left() + indent + tv.row_height() * 0.5f,
                 b.top() + tv.row_height() * (static_cast<float>(index) + 0.5f)};
}

// A point inside row `index`'s label, past any indent or chevron gutter.
Point label_point(const TreeViewControl& tv, std::size_t index)
{
    const Rect b = tv.bounds();
    return Point{b.right() - 5.0f, b.top() + tv.row_height() * (static_cast<float>(index) + 0.5f)};
}

const Point Outside{-500.0f, -500.0f};
} // namespace

TEST_CASE("An empty TreeView has no visible rows and is not focusable", "[tree_view]")
{
    TreeViewControl tv;
    REQUIRE(tv.visible_row_count() == 0);
    REQUIRE_FALSE(tv.focusable());
}

TEST_CASE("Flattening descends into an expanded node's children but not a collapsed one's", "[tree_view]")
{
    TreeViewControl tv = make_tree_view();
    // A2 starts collapsed, so A2a is hidden: [A, A1, A2, B].
    REQUIRE(tv.visible_row_count() == 4);
    REQUIRE(tv.visible_node_at(0)->text == "A");
    REQUIRE(tv.visible_node_at(1)->text == "A1");
    REQUIRE(tv.visible_node_at(2)->text == "A2");
    REQUIRE(tv.visible_node_at(3)->text == "B");
    REQUIRE(tv.visible_node_at(4) == nullptr);
}

TEST_CASE("measure() height reflects visible rows, not total node count", "[tree_view]")
{
    TreeViewControl      tv;
    Theme                theme = Theme::light();
    HeadlessTextMeasurer measurer;
    tv.set_roots(make_tree());
    const Size desired = tv.measure({Unbounded, Unbounded}, measurer, theme);
    REQUIRE_THAT(desired.height, WithinAbs(tv.row_height() * 4.0f, 1e-3));
}

TEST_CASE("ArrowDown/ArrowUp step through the visible list, skipping collapsed subtrees", "[tree_view]")
{
    TreeViewControl tv = make_tree_view();
    ui::testing::set_focused(tv, true);

    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // no selection yet -> first row, "A"
    REQUIRE(tv.selected()->text == "A");

    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // "A1"
    REQUIRE(tv.selected()->text == "A1");

    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // "A2" (collapsed, A2a not visible)
    REQUIRE(tv.selected()->text == "A2");

    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // "B"
    REQUIRE(tv.selected()->text == "B");

    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // already last, stays clamped
    REQUIRE(tv.selected()->text == "B");

    tv.handle_key({Key::ArrowUp, KeyAction::Down});
    REQUIRE(tv.selected()->text == "A2");
}

TEST_CASE("ArrowRight expands a collapsed node, then moves into its first child", "[tree_view]")
{
    TreeViewControl tv = make_tree_view();
    ui::testing::set_focused(tv, true);
    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // A
    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // A1
    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // A2 (collapsed)
    REQUIRE(tv.selected()->text == "A2");

    tv.handle_key({Key::ArrowRight, KeyAction::Down}); // expands A2, selection stays on A2
    REQUIRE(tv.selected()->text == "A2");
    REQUIRE(tv.visible_row_count() == 5); // A2a now visible

    tv.handle_key({Key::ArrowRight, KeyAction::Down}); // already expanded -> move into first child
    REQUIRE(tv.selected()->text == "A2a");
}

TEST_CASE("ArrowLeft collapses an expanded node; otherwise moves to the parent", "[tree_view]")
{
    TreeViewControl tv = make_tree_view();
    ui::testing::set_focused(tv, true);
    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // A (expanded)
    REQUIRE(tv.selected()->text == "A");

    tv.handle_key({Key::ArrowLeft, KeyAction::Down}); // collapses A
    REQUIRE_FALSE(tv.is_expanded(tv.roots().front()));
    REQUIRE(tv.selected()->text == "A");  // selection unchanged
    REQUIRE(tv.visible_row_count() == 2); // just [A, B] now

    tv.handle_key({Key::ArrowLeft, KeyAction::Down}); // A is a root with no parent: no-op
    REQUIRE(tv.selected()->text == "A");
}

TEST_CASE("ArrowLeft on a leaf moves selection to its parent", "[tree_view]")
{
    TreeViewControl tv = make_tree_view();
    ui::testing::set_focused(tv, true);
    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // A
    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // A1 (leaf)
    REQUIRE(tv.selected()->text == "A1");

    tv.handle_key({Key::ArrowLeft, KeyAction::Down}); // A1 has no children -> move to parent "A"
    REQUIRE(tv.selected()->text == "A");
}

TEST_CASE("Space toggles expansion without changing selection", "[tree_view]")
{
    TreeViewControl tv = make_tree_view();
    ui::testing::set_focused(tv, true);
    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // A
    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // A1
    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // A2 (collapsed)
    REQUIRE(tv.selected()->text == "A2");

    tv.handle_key({Key::Space, KeyAction::Down});
    REQUIRE(tv.is_expanded(tv.roots().front().children[1]));
    REQUIRE(tv.selected()->text == "A2"); // selection didn't move

    tv.handle_key({Key::Space, KeyAction::Down});
    REQUIRE_FALSE(tv.is_expanded(tv.roots().front().children[1]));
}

TEST_CASE("A chevron click toggles expansion without selecting; a label click selects without expanding", "[tree_view]")
{
    TreeViewControl tv = make_tree_view();

    // Row 2 is "A2" (depth 1, collapsed, has children).
    tv.handle_pointer(down(chevron_point(tv, 2, 1)));
    tv.handle_pointer(up(chevron_point(tv, 2, 1)));
    REQUIRE(tv.is_expanded(tv.roots().front().children[1]));
    REQUIRE(tv.selected() == nullptr); // chevron click never selected anything

    // Row 1 is "A1" (leaf, no chevron), so a label click selects it.
    tv.handle_pointer(down(label_point(tv, 1)));
    tv.handle_pointer(up(label_point(tv, 1)));
    REQUIRE(tv.selected() != nullptr);
    REQUIRE(tv.selected()->text == "A1");
    // Clicking A1's label must not have touched any expanded flag.
    REQUIRE(tv.is_expanded(tv.roots().front()));
}

TEST_CASE("A click selects and raises on_item_click; a double-click or Enter opens", "[tree_view]")
{
    TreeViewControl          tv = make_tree_view();
    std::vector<std::string> log;
    tv.on_item_click([&](const TreeNode& node) { log.push_back("click " + node.text); });
    tv.on_selection_changed([&](const TreeNode* node) { log.push_back("select " + node->text); });
    tv.on_item_invoked([&](const TreeNode& node) { log.push_back("open " + node.text); });

    tv.handle_pointer(down(label_point(tv, 1)));
    tv.handle_pointer(up(label_point(tv, 1)));
    CHECK(log == std::vector<std::string>{"click A1", "select A1"});

    log.clear();
    PointerEvent second = down(label_point(tv, 1));
    second.click_count  = 2;
    tv.handle_pointer(second);
    tv.handle_pointer(up(label_point(tv, 1)));
    CHECK(log == std::vector<std::string>{"click A1", "open A1"});

    log.clear();
    ui::testing::set_focused(tv, true);
    tv.handle_key({Key::Enter, KeyAction::Down});
    CHECK(log == std::vector<std::string>{"open A1"});
}

TEST_CASE("Without a selection a single click opens the row", "[tree_view]")
{
    TreeViewControl tv = make_tree_view();
    tv.set_selection_mode(TreeViewSelectionMode::None);
    int invoked = 0;
    tv.on_item_invoked([&](const TreeNode&) { ++invoked; });
    tv.handle_pointer(down(label_point(tv, 3)));
    tv.handle_pointer(up(label_point(tv, 3)));
    CHECK(invoked == 1);
    CHECK(tv.selected() == nullptr);
}

TEST_CASE("on_selection_changed fires only on an actual change", "[tree_view]")
{
    TreeViewControl tv = make_tree_view();
    ui::testing::set_focused(tv, true);
    int fired = 0;
    tv.on_selection_changed([&](const TreeNode*) { ++fired; });

    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // no selection -> "A"
    REQUIRE(fired == 1);

    tv.handle_key({Key::ArrowLeft, KeyAction::Down}); // collapses A, selection stays "A": no callback
    REQUIRE(fired == 1);

    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // "A" -> "B" (A now collapsed)
    REQUIRE(fired == 2);
}

TEST_CASE("on_selection_changed reports the selected node, or nullptr once nothing is", "[tree_view]")
{
    TreeViewControl tv = make_tree_view();
    tv.set_selection_mode(TreeViewSelectionMode::Multiple);
    ui::testing::set_focused(tv, true);
    std::vector<const TreeNode*> reported;
    tv.on_selection_changed([&](const TreeNode* node) { reported.push_back(node); });

    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // focus on "A"; Multiple does not select with it
    REQUIRE(reported.empty());
    tv.handle_key({Key::Space, KeyAction::Down}); // checks "A" and its subtree
    REQUIRE(reported.size() == 1);
    REQUIRE(reported.back() != nullptr);
    CHECK(reported.back() == tv.selected());

    tv.handle_key({Key::Space, KeyAction::Down}); // unchecks it again
    REQUIRE(reported.size() == 2);
    CHECK(reported.back() == nullptr);
}

TEST_CASE("Selection survives an unrelated sibling's expand/collapse (tracked by pointer, not index)", "[tree_view]")
{
    TreeViewControl tv = make_tree_view();

    tv.handle_pointer(down(label_point(tv, 1)));
    tv.handle_pointer(up(label_point(tv, 1)));
    REQUIRE(tv.selected()->text == "A1");

    // Expanding an unrelated sibling shifts every row below it. Selection is tracked by node
    // pointer, not by row index, so A1 stays selected.
    tv.handle_pointer(down(chevron_point(tv, 2, 1)));
    tv.handle_pointer(up(chevron_point(tv, 2, 1)));
    REQUIRE(tv.visible_row_count() == 5);

    REQUIRE(tv.selected() != nullptr);
    REQUIRE(tv.selected()->text == "A1");
}

TEST_CASE("Disabled TreeView ignores input and cannot be focused", "[tree_view][state]")
{
    TreeViewControl tv = make_tree_view();
    tv.set_enabled(false);

    tv.handle_pointer(down(label_point(tv, 1)));
    tv.handle_pointer(up(label_point(tv, 1)));
    REQUIRE(tv.selected() == nullptr);
    REQUIRE_FALSE(tv.focusable());
}

TEST_CASE("Release outside the tree cancels a pending row interaction cleanly", "[tree_view][state]")
{
    TreeViewControl tv = make_tree_view();
    tv.handle_pointer(down(label_point(tv, 0)));
    tv.handle_pointer(move(Outside));
    tv.handle_pointer(up(Outside));
    // Release happened outside bounds(), so nothing should have been selected.
    REQUIRE(tv.selected() == nullptr);
}

// ---- Multiple selection, templates, events and drag and drop ----

namespace
{
// The check box's cell starts 10 in from the indentation.
Point check_point(const TreeViewControl& tv, std::size_t index, std::size_t depth)
{
    const Rect  b      = tv.bounds();
    const float indent = static_cast<float>(depth) * tv.indent_width();
    return Point{b.left() + indent + 10.0f + 10.0f, b.top() + tv.row_height() * (static_cast<float>(index) + 0.5f)};
}

void relayout(TreeViewControl& tv)
{
    HeadlessTextMeasurer measurer;
    const Size           desired = tv.measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    tv.arrange({100.0f, 100.0f, std::max(desired.width, 300.0f), desired.height});
}
} // namespace

TEST_CASE("Multiple selection: a check box selects a subtree and a partly selected parent is indeterminate",
          "[tree_view][multi]")
{
    TreeViewControl tv;
    tv.set_selection_mode(TreeViewSelectionMode::Multiple);
    tv.set_roots(make_tree());
    relayout(tv);
    int invoked = 0;
    tv.on_item_invoked([&](const TreeNode&) { ++invoked; });

    // A click on the row (not the box) selects nothing, and a double-click opens it.
    tv.handle_pointer(down(label_point(tv, 1)));
    tv.handle_pointer(up(label_point(tv, 1)));
    CHECK(invoked == 0);
    PointerEvent second = down(label_point(tv, 1));
    second.click_count  = 2;
    tv.handle_pointer(second);
    tv.handle_pointer(up(label_point(tv, 1)));
    CHECK(invoked == 1);
    CHECK(tv.selected_nodes().empty());

    // A1's box: A1 selected, A partly (A2 still unselected).
    tv.handle_pointer(down(check_point(tv, 1, 1)));
    tv.handle_pointer(up(check_point(tv, 1, 1)));
    CHECK(tv.selection_state(tv.roots()[0].children[0]) == TreeNodeSelectionState::Selected);
    CHECK(tv.selection_state(tv.roots()[0]) == TreeNodeSelectionState::Partial);

    // A's box selects its whole subtree (from partial to selected).
    tv.handle_pointer(down(check_point(tv, 0, 0)));
    tv.handle_pointer(up(check_point(tv, 0, 0)));
    CHECK(tv.selection_state(tv.roots()[0]) == TreeNodeSelectionState::Selected);
    CHECK(tv.selection_state(tv.roots()[0].children[1].children[0]) == TreeNodeSelectionState::Selected); // A2a, collapsed, too
    CHECK(tv.selected_nodes().size() == 4); // A, A1, A2, A2a

    // Space on the focused row (A, whose box was clicked last) toggles it back; the arrows move the
    // focus alone.
    ui::testing::set_focused(tv, true);
    tv.handle_key({Key::Space, KeyAction::Down});
    CHECK(tv.selected_nodes().empty());
    tv.handle_key({Key::ArrowDown, KeyAction::Down}); // A1
    CHECK(tv.selected_nodes().empty());
    tv.handle_key({Key::Space, KeyAction::Down});
    CHECK(tv.selected_nodes().size() == 1);
}

TEST_CASE("Expanding asks for the children of a node that has none yet, and opens with the tree it gets", "[tree_view]")
{
    TreeViewControl tv;
    tv.set_roots({TreeNode{.text = "Lazy", .has_children = true}});
    relayout(tv);
    int expanding = 0;
    tv.on_expanding(
        [&](const TreeNode& node)
        {
            ++expanding;
            REQUIRE(node.text == "Lazy");
            // A new tree, with the children: the node is found again by its key and opens.
            tv.set_roots({TreeNode{.text = "Lazy", .children = {TreeNode{.text = "Loaded"}}}});
        });
    tv.handle_pointer(down(chevron_point(tv, 0, 0)));
    tv.handle_pointer(up(chevron_point(tv, 0, 0)));
    CHECK(expanding == 1);
    CHECK(tv.visible_row_count() == 2);
    CHECK(tv.visible_node_at(1)->text == "Loaded");
}

TEST_CASE("An item template draws each row's content after the chevron", "[tree_view]")
{
    class Glyph final : public Element
    {
    public:

    protected:
        [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
        {
            return {50.0f, 16.0f};
        }
        void render_override(Renderer&, const RenderContext&) const override
        {
        }
    };
    TreeViewControl          tv;
    std::vector<std::string> made;
    tv.set_item_template(
        [&](const TreeNode& node)
        {
            made.push_back(node.text);
            return std::make_unique<Glyph>();
        });
    tv.set_roots(make_tree());
    relayout(tv);
    CHECK(made.size() == 4);
    // Each row's content sits under its tree item.
    std::vector<Element*>                     content;
    const std::function<void(const Element&)> walk = [&](const Element& node)
    {
        for (Element* child : automation_children(node))
        {
            if (child->automation_role() == AutomationRole::TreeItem)
            {
                walk(*child);
            }
            else
            {
                content.push_back(child);
            }
        }
    };
    walk(tv);
    REQUIRE(content.size() == 4);
    for (Element* e : content)
    {
        CHECK(e->bounds().x >= tv.bounds().x + 40.0f); // past the chevron column
    }
}

TEST_CASE("A TreeView control starts with dragging and dropping off, like the spec", "[tree_view][drag]")
{
    TreeViewControl tv;
    CHECK_FALSE(tv.can_drag_items());
    CHECK_FALSE(tv.allow_drop());

    ui::Control<ui::TreeView<TreeNode>> handle;
    std::unique_ptr<Element> node = ui::build(ui::TreeView<TreeNode>{.items = make_tree(), .control = handle});
    CHECK(handle->can_drag_items() == tv.can_drag_items());
    CHECK(handle->allow_drop() == tv.allow_drop());

    // A press and a move on a row start no drag.
    HeadlessUI       ui  = create_headless();
    auto             own = std::make_unique<TreeViewControl>();
    TreeViewControl* raw = own.get();
    raw->set_roots(make_tree());
    ui.window().set_content(std::move(own));
    ui.layout({400.0f, 400.0f});
    const Point from = label_point(*raw, 3);
    ui::testing::seams(ui.window()).handle_pointer(down(from));
    ui::testing::seams(ui.window()).handle_pointer(move({from.x, from.y - 10.0f}));
    CHECK_FALSE(DragDrop::active());
    ui::testing::seams(ui.window()).handle_pointer(up(from));
}

TEST_CASE("Dragging a node onto another makes it the last child; onto its own subtree is refused", "[tree_view][drag]")
{
    HeadlessUI       ui  = create_headless();
    auto             own = std::make_unique<TreeViewControl>();
    TreeViewControl* tv  = own.get();
    tv->set_can_drag_items(true);
    tv->set_allow_drop(true);
    tv->set_roots(make_tree());
    ui.window().set_content(std::move(own));
    ui.layout({400.0f, 400.0f});
    Window& w = ui.window();

    // Drag B (row 3) into the middle of A1 (row 1).
    const Point from = label_point(*tv, 3);
    ui::testing::seams(w).handle_pointer(down(from));
    ui::testing::seams(w).handle_pointer(move({from.x, from.y - 10.0f}));
    REQUIRE(DragDrop::active());
    const Point onto = label_point(*tv, 1);
    ui::testing::seams(w).handle_pointer(move(onto));
    ui::testing::seams(w).handle_pointer(up(onto));
    CHECK_FALSE(DragDrop::active());
    REQUIRE(tv->roots().size() == 1);
    REQUIRE(tv->roots()[0].children[0].children.size() == 1);
    CHECK(tv->roots()[0].children[0].children[0].text == "B");
    CHECK_FALSE(tv->is_expanded(tv->roots()[0].children[0])); // a drop into a collapsed node leaves it collapsed

    // A into A1 (its own child) is refused.
    ui.layout({400.0f, 400.0f});
    const Point a = label_point(*tv, 0);
    ui::testing::seams(w).handle_pointer(down(a));
    ui::testing::seams(w).handle_pointer(move({a.x, a.y + 10.0f}));
    const Point child = label_point(*tv, 1);
    ui::testing::seams(w).handle_pointer(move(child));
    ui::testing::seams(w).handle_pointer(up(child));
    CHECK(tv->roots().size() == 1);
    CHECK(tv->roots()[0].text == "A");
}

TEST_CASE("Dragging a node to the top quarter of another reorders it before", "[tree_view][drag]")
{
    HeadlessUI       ui  = create_headless();
    auto             own = std::make_unique<TreeViewControl>();
    TreeViewControl* tv  = own.get();
    tv->set_can_drag_items(true);
    tv->set_allow_drop(true);
    tv->set_roots(make_tree());
    ui.window().set_content(std::move(own));
    ui.layout({400.0f, 400.0f});
    Window&     w = ui.window();
    std::string completed;
    tv->on_drag_items_completed(
        [&](const TreeViewDragItemsCompletedArgs& args)
        { completed = args.node.text + (args.new_parent != nullptr ? " in " + args.new_parent->text : ""); });

    const Point from = label_point(*tv, 3); // B
    ui::testing::seams(w).handle_pointer(down(from));
    ui::testing::seams(w).handle_pointer(move({from.x, from.y - 10.0f}));
    const Rect  a1{tv->bounds().x, tv->bounds().y + tv->row_height(), tv->bounds().width, tv->row_height()};
    const Point before{a1.right() - 5.0f, a1.y + 2.0f};
    ui::testing::seams(w).handle_pointer(move(before));
    ui::testing::seams(w).handle_pointer(up(before));
    REQUIRE(tv->roots()[0].children.size() == 3);
    CHECK(tv->roots()[0].children[0].text == "B");
    CHECK(tv->roots()[0].children[1].text == "A1");
    CHECK(completed == "B in A");
}

namespace
{
// A headless window holding one TreeView, and a drag of a row to a point at `fraction` of another
// row's height.
struct DragTree
{
    HeadlessUI       ui = create_headless();
    TreeViewControl* tv = nullptr;

    explicit DragTree(std::vector<TreeNode> roots)
    {
        auto own = std::make_unique<TreeViewControl>();
        tv       = own.get();
        tv->set_can_drag_items(true);
        tv->set_allow_drop(true);
        tv->set_roots(std::move(roots));
        ui.window().set_content(std::move(own));
        ui.layout({400.0f, 400.0f});
    }
    [[nodiscard]] Point at(std::size_t row, float fraction) const
    {
        return Point{tv->bounds().right() - 5.0f,
                     tv->bounds().top() + tv->row_height() * (static_cast<float>(row) + fraction)};
    }
    // Picks up `from` and moves over `to`, leaving the drag in flight.
    void hover(std::size_t from, Point to)
    {
        const Point start = label_point(*tv, from);
        ui::testing::seams(ui.window()).handle_pointer(down(start));
        ui::testing::seams(ui.window()).handle_pointer(move({start.x, start.y + (from == 0 ? 10.0f : -10.0f)}));
        REQUIRE(DragDrop::active());
        ui::testing::seams(ui.window()).handle_pointer(move(to));
    }
    void drag(std::size_t from, Point to)
    {
        hover(from, to);
        ui::testing::seams(ui.window()).handle_pointer(up(to));
        ui.layout({400.0f, 400.0f});
    }
};

std::vector<TreeNode> with_c()
{
    std::vector<TreeNode> roots = make_tree(); // A {A1, A2 {A2a}}, B
    roots.push_back(TreeNode{.text = "C"});
    return roots;
}
} // namespace

TEST_CASE("TreeView drop zones: the middle 60% of a row drops in, the outer 20%s reorder", "[tree_view][drag]")
{
    SECTION("at 22% of A1 B goes into it")
    {
        DragTree t{make_tree()};
        t.drag(3, t.at(1, 0.22f));
        REQUIRE(t.tv->roots().size() == 1);
        CHECK(t.tv->roots()[0].children[0].children.size() == 1);
    }
    SECTION("at 18% of A1 B goes before it")
    {
        DragTree t{make_tree()};
        t.drag(3, t.at(1, 0.18f));
        REQUIRE(t.tv->roots()[0].children.size() == 3);
        CHECK(t.tv->roots()[0].children[0].text == "B");
    }
    SECTION("at 82% of A1 B goes after it")
    {
        DragTree t{make_tree()};
        t.drag(3, t.at(1, 0.82f));
        REQUIRE(t.tv->roots()[0].children.size() == 3);
        CHECK(t.tv->roots()[0].children[1].text == "B");
    }
}

TEST_CASE("TreeView reorder: one gap, two outcomes, by the way the node was dragged", "[tree_view][drag]")
{
    // The gap between A2 (the last child of A) and B (top level).
    SECTION("dragged down into it, A1 goes after A2, at A2's level")
    {
        DragTree t{with_c()};
        t.drag(1, t.at(3, 0.1f));
        REQUIRE(t.tv->roots().size() == 3);
        REQUIRE(t.tv->roots()[0].children.size() == 2);
        CHECK(t.tv->roots()[0].children[0].text == "A2");
        CHECK(t.tv->roots()[0].children[1].text == "A1");
    }
    SECTION("dragged up into it, C goes before B, at B's level")
    {
        DragTree t{with_c()};
        t.drag(4, t.at(2, 0.9f));
        REQUIRE(t.tv->roots().size() == 3);
        CHECK(t.tv->roots()[1].text == "C");
        CHECK(t.tv->roots()[2].text == "B");
        CHECK(t.tv->roots()[0].children.size() == 2);
    }
}

TEST_CASE("TreeView reorder: after an expanded row is its first child; the gaps by the dragged row move nothing",
          "[tree_view][drag]")
{
    SECTION("X dragged down below expanded A becomes A's first child")
    {
        std::vector<TreeNode> roots{TreeNode{.text = "X"},
                                    TreeNode{.text = "A", .children = {TreeNode{.text = "A1"}}, .expanded = true}};
        DragTree              t{std::move(roots)};
        t.drag(0, t.at(1, 0.9f));
        REQUIRE(t.tv->roots().size() == 1);
        REQUIRE(t.tv->roots()[0].children.size() == 2);
        CHECK(t.tv->roots()[0].children[0].text == "X");
    }
    SECTION("A1 dropped in the gap right under itself stays put, and the drag still completes")
    {
        DragTree    t{make_tree()};
        std::string completed;
        t.tv->on_drag_items_completed(
            [&](const TreeViewDragItemsCompletedArgs& args)
            { completed = args.node.text + (args.new_parent != nullptr ? " in " + args.new_parent->text : ""); });
        t.drag(1, t.at(2, 0.1f));
        REQUIRE(t.tv->roots()[0].children.size() == 2);
        CHECK(t.tv->roots()[0].children[0].text == "A1");
        CHECK(t.tv->roots()[0].children[1].text == "A2");
        CHECK(completed == "A1 in A");
    }
}

TEST_CASE("TreeView hover-expand: a second anywhere on a collapsed row with children opens it", "[tree_view][drag]")
{
    DragTree t{make_tree()};
    t.hover(3, t.at(2, 0.9f)); // B over A2's bottom strip: a reorder zone, still A2's row
    t.ui.window().tick(600ms);
    CHECK_FALSE(t.tv->is_expanded(t.tv->roots()[0].children[1]));
    t.ui.window().tick(500ms);
    CHECK(t.tv->is_expanded(t.tv->roots()[0].children[1]));
    ui::testing::seams(t.ui.window()).handle_pointer(up(t.at(2, 0.9f)));
}

TEST_CASE("TreeView reorder in Multiple selection may not land on a selected node's slot", "[tree_view][drag]")
{
    DragTree t{make_tree()};
    t.tv->set_selection_mode(TreeViewSelectionMode::Multiple);
    t.ui.layout({400.0f, 400.0f});
    t.tv->handle_pointer(down(check_point(*t.tv, 1, 1))); // A1's box
    t.tv->handle_pointer(up(check_point(*t.tv, 1, 1)));
    REQUIRE(t.tv->selected_nodes().size() == 1);
    t.drag(3, t.at(1, 0.1f)); // B up to the gap above A1: the slot is A1's
    CHECK(t.tv->roots().size() == 2);
    CHECK(t.tv->roots()[0].children.size() == 2);
}

// TVS_HASBUTTONS: a 9 x 9 GRAYTEXT box on the field color, a plus while collapsed and a minus
// once expanded, where Fluent draws a chevron glyph.
TEST_CASE("A classic TreeView marks expandable rows with the plus/minus box", "[treeview][render][retro]")
{
    const Theme     theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    TreeViewControl tv;
    tv.set_roots(make_tree());
    HeadlessTextMeasurer measurer;
    const Size           desired = tv.measure({Unbounded, Unbounded}, measurer, theme);
    tv.arrange({0.0f, 0.0f, desired.width, desired.height});
    TestRenderer renderer;
    tv.render(renderer, theme);

    // A (expanded) and A2 (collapsed) have children: two boxes, one plus and one minus.
    std::size_t boxes = 0;
    std::size_t bars  = 0;
    for (const auto& cmd : renderer.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&cmd))
        {
            if (f->color == theme.colors().text_disabled && f->bounds.width == 9.0f && f->bounds.height == 9.0f)
            {
                ++boxes;
            }
            if (f->color == theme.colors().field_text && (f->bounds.width == 5.0f || f->bounds.height == 5.0f))
            {
                ++bars;
            }
        }
        if (const auto* t = std::get_if<DrawTextRun>(&cmd))
        {
            CHECK(static_cast<unsigned char>(t->text.front()) < 0x80); // labels only, no icon-font chevron
        }
    }
    CHECK(boxes == 2);
    CHECK(bars == 3);
}

TEST_CASE("A new tree keeps what the user expanded and selected, by key", "[tree_view][keys]")
{
    TreeViewControl tv;
    tv.set_roots({TreeNode{.text = "Docs", .key = "d", .children = {TreeNode{.text = "Report", .key = "r"}}},
                  TreeNode{.text = "Music", .key = "m"}});
    relayout(tv);
    std::vector<std::string> seen;
    tv.on_selection_changed([&](const TreeNode* n) { seen.push_back(n != nullptr ? n->key : "(none)"); });

    tv.set_expanded("d", true);
    tv.select("r");
    REQUIRE(seen == std::vector<std::string>{"r"});

    // Renamed and reordered, same keys: still open, still selected, and nothing to report.
    tv.set_roots({TreeNode{.text = "Music", .key = "m"},
                  TreeNode{.text = "Documents", .key = "d", .children = {TreeNode{.text = "Report.docx", .key = "r"}}}});
    CHECK(tv.is_expanded(*tv.find("d")));
    REQUIRE(tv.selected() != nullptr);
    CHECK(tv.selected()->text == "Report.docx");
    CHECK(seen.size() == 1);

    // The selected key gone: no longer selected, and that is a change.
    tv.set_roots({TreeNode{.text = "Music", .key = "m"}});
    CHECK(tv.selected() == nullptr);
    CHECK(seen.back() == "(none)");

    // A node without a key is known by its path of texts.
    tv.set_roots({TreeNode{.text = "A", .children = {TreeNode{.text = "B"}}}});
    REQUIRE(tv.find("A/B") != nullptr);
    CHECK(tv.key_of(tv.roots()[0].children[0]) == "A/B");
}

namespace
{
struct Folder
{
    std::string         path;
    std::string         name;
    bool                has_subfolders = false;
    std::vector<Folder> subfolders{};

    friend bool operator==(const Folder&, const Folder&) = default;
};
} // namespace

TEST_CASE("ui::TreeView<T> shows the app's items, which stay the model", "[tree_view][spec]")
{
    STATIC_REQUIRE(std::is_same_v<decltype(ui::TreeView{.items = {TreeNode{.text = "A"}}}), ui::TreeView<TreeNode>>);

    Property<std::vector<Folder>>     folders{std::vector<Folder>{
        Folder{.path = "C:", .name = "C:", .has_subfolders = true},
        Folder{.path = "D:", .name = "D:", .subfolders = {Folder{.path = "D:/Music", .name = "Music"}}},
    }};
    std::vector<std::string>          picked;
    int                               loads = 0;
    ui::Control<ui::TreeView<Folder>> handle;
    std::unique_ptr<Element>          tree = ui::build(ui::TreeView<Folder>{
        .items                = folders,
        .children             = &Folder::subfolders,
        .key                  = &Folder::path,
        .has_children         = &Folder::has_subfolders,
        .on_selection_changed = [&](std::optional<Folder> f) { picked.push_back(f ? f->name : "(none)"); },
        .on_expanding =
            [&](const Folder& f)
        {
            ++loads;
            if (f.path == "C:" && f.subfolders.empty())
            {
                folders.update([](std::vector<Folder>& v)
                               { v[0].subfolders = {Folder{.path = "C:/Windows", .name = "Windows"}}; });
            }
        },
        .control = handle,
        .item    = [](const Folder& f) -> ui::Spec { return ui::Text{.text = f.name}; },
    });
    TreeViewControl&                  tv   = *handle;
    relayout(tv);
    REQUIRE(tv.visible_row_count() == 2);

    // Lazy: C: shows a chevron, loads into the property on expand, and opens with what it got.
    tv.set_expanded("C:", true);
    CHECK(loads == 1);
    relayout(tv);
    REQUIRE(tv.visible_row_count() == 3);
    CHECK(tv.key_of(*tv.visible_node_at(1)) == "C:/Windows");

    // The handler gets a copy of the item.
    tv.select("C:/Windows");
    REQUIRE(picked.size() == 1);
    CHECK(picked[0] == "Windows");

    // A rename in the data keeps the row open and selected: the key is the same.
    folders.update([](std::vector<Folder>& v) { v[0].name = "System"; });
    relayout(tv);
    CHECK(tv.visible_row_count() == 3);
    CHECK(tv.key_of(*tv.selected()) == "C:/Windows");
}

TEST_CASE("A drag in a ui::TreeView<T> with a member .children writes the rearranged items back",
          "[tree_view][drag][spec]")
{
    Property<std::vector<Folder>>     folders{std::vector<Folder>{Folder{.path = "a", .name = "A"},
                                                                  Folder{.path = "b", .name = "B"},
                                                                  Folder{.path = "c", .name = "C"}}};
    std::string                       moved;
    ui::Control<ui::TreeView<Folder>> handle;
    HeadlessUI                        ui = create_headless();
    ui.window().set_content(ui::TreeView<Folder>{
        .items                   = folders,
        .children                = &Folder::subfolders,
        .key                     = &Folder::path,
        .can_drag_items          = true,
        .allow_drop              = true,
        .on_drag_items_completed = [&](const TreeViewMovedArgs<Folder>& args)
        { moved = args.item.name + (args.new_parent != nullptr ? " into " + args.new_parent->name : ""); },
        .control = handle,
        .item    = [](const Folder& f) -> ui::Spec { return ui::Text{.text = f.name}; },
    });
    ui.layout({400.0f, 400.0f});
    TreeViewControl& tv = *handle;
    const auto       at = [&](std::size_t row, float fraction)
    {
        return Point{tv.bounds().right() - 5.0f,
                     tv.bounds().top() + tv.row_height() * (static_cast<float>(row) + fraction)};
    };

    // C dropped into the middle of A: C becomes A's child, in the property too.
    const Point start = at(2, 0.5f);
    ui::testing::seams(ui.window()).handle_pointer(down(start));
    ui::testing::seams(ui.window())
        .handle_pointer(PointerEvent{{start.x, start.y - 10.0f}, PointerButton::Left, PointerAction::Move});
    REQUIRE(DragDrop::active());
    ui::testing::seams(ui.window()).handle_pointer(PointerEvent{at(0, 0.5f), PointerButton::Left, PointerAction::Move});
    ui::testing::seams(ui.window()).handle_pointer(up(at(0, 0.5f)));

    CHECK(moved == "C into A");
    REQUIRE(folders->size() == 2);
    REQUIRE(folders->at(0).subfolders.size() == 1);
    CHECK(folders->at(0).subfolders[0].name == "C");
}

TEST_CASE("Automation sees each visible row as a tree item that selects, expands and opens", "[tree_view][automation]")
{
    TreeViewControl tv = make_tree_view();
    std::string     invoked;
    tv.on_item_invoked([&](const TreeNode& node) { invoked = node.text; });

    REQUIRE(describe_automation(tv) ==
            "Tree [Value=\"\" (ReadOnly)]\n"
            "  TreeItem \"A\" [Invoke SelectionItem=Unselected ExpandCollapse=Expanded]\n"
            "    TreeItem \"A1\" [Invoke SelectionItem=Unselected]\n"
            "    TreeItem \"A2\" [Invoke SelectionItem=Unselected ExpandCollapse=Collapsed]\n"
            "  TreeItem \"B\" [Invoke SelectionItem=Unselected]\n");

    // Each node's parent is the one that lists it, and each sits over its row.
    const std::vector<Element*> top     = automation_children(tv);
    const std::vector<Element*> under_a = automation_children(*top[0]);
    REQUIRE(top.size() == 2);
    REQUIRE(under_a.size() == 2);
    REQUIRE(automation_parent(*top[0]) == &tv);
    REQUIRE(automation_parent(*under_a[1]) == top[0]);
    const float row = tv.row_height();
    REQUIRE(under_a[1]->bounds() == Rect{tv.bounds().x, tv.bounds().y + 2.0f * row, tv.bounds().width, row});

    // Selecting, expanding and invoking go through the patterns.
    REQUIRE(ui::automation_providers(*under_a[0]).selection_item->select_item());
    REQUIRE(tv.selected() != nullptr);
    CHECK(tv.selected()->text == "A1");
    CHECK(ui::automation_providers(*under_a[0]).selection_item->item_selected());

    REQUIRE(ui::automation_providers(*under_a[1]).expand_collapse->set_expanded_state(true));
    relayout(tv);
    const std::vector<Element*> under_a2 = automation_children(*under_a[1]);
    REQUIRE(under_a2.size() == 1);
    CHECK(under_a2[0]->automation_name() == "A2a");
    CHECK(automation_children(tv)[1]->bounds().y == tv.bounds().y + 4.0f * row); // B moved down a row

    REQUIRE(ui::automation_providers(*top[1]).invoke->invoke());
    CHECK(invoked == "B");
    CHECK(tv.selected()->text == "B"); // opened as Enter does, selected first

    REQUIRE(ui::automation_providers(*top[0]).expand_collapse->set_expanded_state(false));
    CHECK(automation_children(*top[0]).empty());

    // A node whose row went away reads as empty and offers nothing.
    std::vector<TreeNode> roots = tv.roots();
    roots.pop_back();
    tv.set_roots(std::move(roots));
    CHECK(automation_children(tv).size() == 1);
    CHECK(top[1]->automation_name().empty());
    CHECK(ui::automation_providers(*top[1]).selection_item == nullptr);
}

TEST_CASE("Automation adds tree items to a multiple selection and takes them out", "[tree_view][automation]")
{
    TreeViewControl tv = make_tree_view();
    tv.set_selection_mode(TreeViewSelectionMode::Multiple);
    relayout(tv);
    const std::vector<Element*> top     = automation_children(tv);
    const std::vector<Element*> under_a = automation_children(*top[0]);
    CHECK(ui::automation_providers(*top[0]).invoke == nullptr); // nothing to open without a handler

    REQUIRE(ui::automation_providers(*under_a[0]).selection_item->add_to_selection());
    REQUIRE(ui::automation_providers(*top[1]).selection_item->add_to_selection());
    CHECK(tv.selected_nodes().size() == 2);
    REQUIRE(ui::automation_providers(*under_a[0]).selection_item->remove_from_selection());
    REQUIRE(tv.selected_nodes().size() == 1);
    CHECK(tv.selected_nodes()[0]->text == "B");

    tv.set_selection_mode(TreeViewSelectionMode::None);
    CHECK(ui::automation_providers(*top[1]).selection_item == nullptr);
}
