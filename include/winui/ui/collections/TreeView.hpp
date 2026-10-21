// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/DragDrop.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/ItemsModel.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <concepts>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace ui
{

/// A node of a tree: plain data, not an Element. The ready-made item of a ui::TreeView, and
/// what a TreeViewControl is given.
///
/// A tree keeps which nodes are expanded and selected by key. A new tree with the same keys, after
/// a reload, a rename or children added on expand, keeps them. A node without a key is known by
/// the path of texts from its root ("Documents/Report.docx").
///
/// Example:
/// @code
/// ui::TreeView{
///     .items = {
///         {.text = "Documents", .children = {{.text = "Report.docx"}, {.text = "Budget.xlsx"}}, .expanded = true},
///         {.text = "Music", .has_children = true}, // children added on expand
///     },
/// }
/// @endcode
struct TreeNode
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The row's label, and the name screen readers read for the node. Not displayed when an item
    /// template is set.
    std::string text{};
    /// What the tree knows the node by. Empty uses the path of texts from its root.
    std::string key{};
    /// The child nodes, in display order, owned by value.
    std::vector<TreeNode> children{};
    /// Whether the children show when the tree first sees this node's key. After that the user's
    /// expanding and collapsing count.
    bool expanded = false;
    /// Whether to show the chevron before the children exist. An on_expanding handler adds them.
    bool has_children = false;

    /// Same text, key, children and flags.
    friend bool operator==(const TreeNode&, const TreeNode&) = default;
};

/// How many nodes a TreeView lets the user select.
enum class TreeViewSelectionMode
{
    None,     ///< No selection. Clicks only invoke.
    Single,   ///< One node, selected by a click or the arrow keys. The default.
    Multiple, ///< Any number, through a check box per row or Space. A click only invokes.
};

/// What a TreeViewControl's `on_drag_items_completed` gets: the node that moved, its new parent
/// and its position there.
struct TreeViewDragItemsCompletedArgs
{
    const TreeNode& node;                 ///< The moved node, at its new place.
    const TreeNode* new_parent = nullptr; ///< Its new parent; nullptr for a root.
    std::size_t     index      = 0;       ///< Its position among the new parent's children (or the roots).
};

/// A node's selection state in Multiple mode, as its check box shows it.
enum class TreeNodeSelectionState
{
    Unselected, ///< Neither the node nor any descendant leaf is selected.
    Partial,    ///< Some of the node's descendants are selected, not all. The check box shows indeterminate.
    Selected,   ///< The node, or every descendant leaf, is selected.
};

/// A hierarchy of expandable rows, with single or multiple (check box) selection, templated rows,
/// and rearranging by drag and drop.
///
/// The nodes are plain TreeNode data, set with set_roots(). The rows are the visible nodes in
/// order, and the tree draws each row's chrome itself: backplate, selection indicator,
/// indentation and chevron. Which nodes are expanded and selected is kept by each node's key, so
/// set_roots() with a changed tree keeps them. The tree is a single Tab stop. Up and Down move
/// between rows. Left and Right collapse, expand, or step to the parent or the first child. Enter
/// invokes. With set_can_drag_items() and set_allow_drop() on, a row can be dragged onto another
/// row to become its last child, or into the gap between rows to reorder.
/// on_drag_items_completed() reports the move. The tree is not virtualized. Every visible row is
/// realized, so keep very large trees collapsed.
///
/// Example:
/// @code
/// auto tree = std::make_unique<ui::TreeViewControl>();
/// tree->set_roots({{.text = "Documents", .children = {{.text = "Report.docx"}}, .expanded = true},
///                  {.text = "Network", .has_children = true}});
/// tree->on_expanding([raw = tree.get()](const ui::TreeNode& node) {
///     if (node.text == "Network" && node.children.empty())
///     {
///         std::vector<ui::TreeNode> roots = raw->roots();
///         roots[1].children.push_back({.text = "Server01"}); // load lazily
///         raw->set_roots(std::move(roots));                 // the node goes on expanding
///     }
/// });
/// // Enter or double-click:
/// tree->on_item_invoked([](const ui::TreeNode& node) { std::println("open {}", node.text); });
/// @endcode
class TreeViewControl final : public Element, private ValueProvider
{
public:
    /// Makes a row's content for a node and may choose a different look per node. Called during
    /// measure for each visible row. The result is kept per node until the tree changes.
    using ItemTemplate = Callback<std::unique_ptr<Element>(const TreeNode&)>;

    /// An empty, enabled tree in Single selection mode, with dragging and dropping off.
    TreeViewControl();

    // ---- content ----
    /// Replaces the tree. Nodes whose key was there before stay expanded and selected as they
    /// were. A new key takes its node's `expanded`. A selected node whose key is gone is
    /// deselected, which raises on_selection_changed(). Safe to call from on_expanding(), which is
    /// how children added on expand arrive. Invalidates every `TreeNode*` into the old tree and
    /// every row content the template made, and lays the tree out again.
    void set_roots(std::vector<TreeNode> roots);
    /// The top-level nodes, each owning its subtree, as last set or rearranged by a drag.
    [[nodiscard]] const std::vector<TreeNode>& roots() const
    {
        return roots_;
    }
    /// The node with `key`, or nullptr. A node without a key is found by its path of texts.
    [[nodiscard]] const TreeNode* find(std::string_view key) const;
    /// A node's effective key: its `key`, or the path of texts from its root.
    [[nodiscard]] const std::string& key_of(const TreeNode& node) const;

    /// Sets the row content template. nullptr, the default, shows each node's text. Rebuilds every
    /// row's content on the next layout.
    void set_item_template(ItemTemplate item_template);

    // ---- expansion ----
    /// Whether `node`'s children show.
    [[nodiscard]] bool is_expanded(const TreeNode& node) const;
    /// Expands or collapses the node with `key`, as the chevron does, raising on_expanding() first.
    void set_expanded(std::string_view key, bool expanded);

    // ---- selection ----
    /// The selection mode; Single by default.
    [[nodiscard]] TreeViewSelectionMode selection_mode() const
    {
        return selection_mode_;
    }
    /// Switches the mode and adapts the selection. None clears it, Single keeps only the first
    /// selected node, and Multiple shows the check boxes. Lays the tree out again.
    void set_selection_mode(TreeViewSelectionMode mode);
    /// The selected node, the first one in Multiple mode, or nullptr.
    [[nodiscard]] const TreeNode* selected() const;
    /// Every fully selected node, in tree order.
    [[nodiscard]] std::vector<const TreeNode*> selected_nodes() const;
    /// Selects the node with `key` alone, in Single mode. An unknown key clears the selection.
    void select(std::string_view key);
    /// A node's state in Multiple mode. A leaf's is whether it is selected. A parent's follows its
    /// children: Selected when all are, Unselected when none are, else Partial.
    [[nodiscard]] TreeNodeSelectionState selection_state(const TreeNode& node) const;
    /// Selects every node in Multiple mode, and does nothing in the other modes. Fires
    /// on_selection_changed() once.
    void select_all();
    /// Fired whenever the selection changes, by a click, the arrow keys, a check box, or a selected
    /// key gone from set_roots(). Called with selected(): the selected node, the first one in
    /// Multiple mode, or nullptr when nothing is selected. Replaces any previous handler.
    void on_selection_changed(Callback<void(const TreeNode*)> handler)
    {
        on_selection_changed_ = std::move(handler);
    }

    // ---- events ----
    /// Called with the node on a single click on its row, before the click selects it. Not
    /// fired for a click on the chevron or check box.
    void on_item_click(Callback<void(const TreeNode&)> handler)
    {
        on_item_click_ = std::move(handler);
    }
    /// Called when a row is opened: Enter or a double-click, and under
    /// TreeViewSelectionMode::None also a single click. Raised after the click selects. Not
    /// fired for a click on the chevron or check box.
    void on_item_invoked(Callback<void(const TreeNode&)> handler)
    {
        on_item_invoked_ = std::move(handler);
    }
    /// A node is opening. Add the children of a node with `has_children` here, by setting a tree
    /// that has them (set_roots()). Fires before the node expands. If the node still has no
    /// children and no `has_children` afterwards, it stays collapsed.
    void on_expanding(Callback<void(const TreeNode&)> handler)
    {
        on_expanding_ = std::move(handler);
    }
    /// Fired after a node has closed.
    void on_collapsed(Callback<void(const TreeNode&)> handler)
    {
        on_collapsed_ = std::move(handler);
    }

    // ---- drag and drop ----
    /// Whether rows can be dragged out to rearrange the tree; false by default. Element's
    /// allow-drop flag, also off by default, decides whether the tree accepts drops. Rearranging
    /// the tree needs both.
    [[nodiscard]] bool can_drag_items() const
    {
        return can_drag_items_;
    }
    /// Turns dragging rows on or off. Takes effect on the next press.
    void set_can_drag_items(bool can)
    {
        can_drag_items_ = can;
    }
    /// Called with the node that moved, at its new place, its new parent (nullptr for a root) and
    /// its position there. roots() already holds the rearranged tree.
    void on_drag_items_completed(Callback<void(const TreeViewDragItemsCompletedArgs&)> handler)
    {
        on_drag_items_completed_ = std::move(handler);
    }

    /// The row height of the last measure, in DIPs, for callers that compute rows. 0 before the
    /// first measure.
    [[nodiscard]] float row_height() const
    {
        return row_height_;
    }
    /// Indentation per nesting level, in DIPs; 16 by default.
    [[nodiscard]] float indent_width() const
    {
        return indent_width_;
    }
    /// Changes the per-level indentation and lays the tree out again.
    void set_indent_width(float width);

    /// How many rows are visible, that is not inside a collapsed ancestor.
    [[nodiscard]] std::size_t visible_row_count() const;
    /// The node drawn at visible row `index`, counted from 0 at the top, or nullptr when out of
    /// range. Valid until the tree is next rearranged.
    [[nodiscard]] const TreeNode* visible_node_at(std::size_t index) const;

    // ---- focus ----
    /// Whether the tree holds keyboard focus and draws the focus ring.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// A Tab stop only while enabled and non-empty.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled() && !roots_.empty();
    }
    /// Sets the context menu of a node. It is asked for on a right-click on its row, or on
    /// Shift+F10 or the Menu key while the node is the current one. The node is selected first,
    /// unless it already is or the selection mode is not Single. An empty list opens no menu.
    /// Replaces any previous one.
    void set_item_context_menu(Callback<std::vector<MenuItem>(const TreeNode&)> menu)
    {
        item_context_menu_ = std::move(menu);
    }
    /// Offers a right-click first to the templated content of the row under `position`, then to
    /// the tree's own context flyout, and then opens the node's context menu.
    bool raise_context_requested(std::optional<Point> position) override;
    /// The node context menu while it shows, so the window finds its popup.
    void for_each_child(ChildVisitor visit) const override
    {
        if (item_menu_ && detail::TreeAccess::popup_request(*item_menu_) != nullptr)
        {
            visit(*item_menu_);
        }
    }

    // ---- drag and drop ----
    void         drag_enter(const DragEventArgs& e) override;
    DragResponse drag_over(const DragEventArgs& e) override;
    void         drag_leave(const DragEventArgs& e) override;
    void         drop(const DragEventArgs& e) override;
    void         drag_completed(DataPackageOperation result) override;
    /// One accessibility item per root row. Each lists its content and its visible children.
    void automation_children(std::vector<Element*>& out) const override;
    /// A row's accessibility item is the parent of its content and of its child rows' items.
    [[nodiscard]] Element* automation_parent_of(const Element& child) const override;
    /// The row the keyboard is on, so a screen reader reads it as the arrow keys move.
    [[nodiscard]] Element* automation_focus() const override;

    // ---- automation ----
    /// Reported to screen readers as a Tree.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Tree;
    }

protected:
    // ---- layout ----
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    bool pointer_override(const PointerEvent& e) override;

    // ---- render ----
    void render_override(Renderer& renderer, const RenderContext& context) const override;

    // ---- tree plumbing (the rows' template content) ----
    /// Ticks the templated row content and drives the hover-to-expand timer during a drag.
    void tick_override(Duration delta) override;
    void clear_dirty_override(DirtyFlags flags) override;

private:
    friend class TreeViewItemAutomation;

    // The accessibility item of `node`, made on first use and kept for the tree's life, by key.
    [[nodiscard]] Element* item_automation(const TreeNode& node) const;
    // Enter invokes the current row (on_item_invoked()).
    [[nodiscard]] bool takes_enter_key() const
    {
        return true;
    }
    // All four arrows navigate the tree and do not move focus.
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowUp || key == Key::ArrowDown || key == Key::ArrowLeft || key == Key::ArrowRight;
    }
    // One flattened, visible row. `node` points into roots_; `path` is its place in the tree.
    struct VisibleRow
    {
        TreeNode*                node         = nullptr;
        std::size_t              depth        = 0;
        bool                     has_children = false;
        std::vector<std::size_t> path;
    };
    // Where a drop would put the dragged node, relative to a row: into it, before it, after it (its
    // first child when it is expanded), or nowhere (the gap next to the dragged row itself).
    enum class DropZone
    {
        Before,
        Into,
        After,
        Stay,
    };
    // What the tree keeps about a node, by its key: the user's doing, not the app's data.
    struct NodeState
    {
        bool expanded = false;
        bool selected = false;
    };

    void                                     flatten_into(std::vector<TreeNode>&    nodes,
                                                          std::size_t               depth,
                                                          std::vector<std::size_t>& path,
                                                          std::vector<VisibleRow>&  out) const;
    [[nodiscard]] std::vector<VisibleRow>    flatten() const;
    [[nodiscard]] Rect                       row_rect(std::size_t visible_index) const;
    [[nodiscard]] std::size_t                row_at(Point p) const;
    [[nodiscard]] TreeNode*                  node_at(const std::vector<std::size_t>& path);
    [[nodiscard]] std::optional<std::size_t> row_of(const TreeNode* node) const;
    // The visible row the focus ring goes inside: the cursor in Multiple and None, the selection otherwise.
    [[nodiscard]] std::optional<std::size_t> ring_row() const;
    [[nodiscard]] float                      check_left(std::size_t depth) const;
    [[nodiscard]] float                      chevron_left(std::size_t depth) const;
    [[nodiscard]] float                      content_left(std::size_t depth) const;

    // ---- the state kept by key ----
    void                     index_keys(); // keys_ for the current roots_
    [[nodiscard]] NodeState& state_of(const TreeNode& node);
    [[nodiscard]] bool       is_selected(const TreeNode& node) const;
    void                     set_subtree(const TreeNode& node, bool selected);
    void                     clear_selection();
    void                     sync_parents(const std::vector<TreeNode>& nodes);
    void collect_selected(const std::vector<TreeNode>& nodes, std::vector<const TreeNode*>& out) const;

    void select_single(TreeNode* node);
    void toggle_multi(TreeNode* node);
    void set_node_expanded(TreeNode* node, bool expanded);
    void toggle_expanded(TreeNode* node);
    void structure_changed();
    void selection_may_have_changed(const TreeNode* before);

    // ---- drag ----
    void                                  start_drag(std::size_t row, Point pointer);
    [[nodiscard]] std::optional<DropZone> drop_zone(Point p, std::size_t dragged, std::size_t& row) const;
    // The visible row of the node at `path` (the dragged one), or NoIndex.
    [[nodiscard]] std::size_t dragged_row(const std::vector<std::size_t>& path) const;
    [[nodiscard]] bool        can_drop(const std::vector<std::size_t>& source, std::size_t row, DropZone zone) const;

    std::vector<TreeNode>                                 roots_;
    std::unordered_map<std::string, NodeState>            states_; // by effective key
    std::unordered_map<const TreeNode*, std::string>      keys_;   // each node's effective key
    std::unordered_map<std::string_view, const TreeNode*> by_key_; // views into keys_
    TreeViewSelectionMode                                 selection_mode_ = TreeViewSelectionMode::Single;
    TreeNode*                                             current_ = nullptr; // keyboard focus (Multiple mode's cursor)
    std::size_t                                           hovered_row_ = detail::NoIndex;
    std::size_t                                           pressed_row_ = detail::NoIndex;
    int           press_clicks_ = 1; // the press's click count: 2 is a double-click
    std::uint64_t tree_version_ = 0; // bumped whenever node addresses may change
    Point         press_point_{};
    bool          can_drag_items_ = false;

    mutable ItemTemplate                                                  item_template_; // called from measure
    Callback<std::vector<MenuItem>(const TreeNode&)>                      item_context_menu_;
    std::unique_ptr<MenuFlyoutControl>                                    item_menu_; // made on the first node menu
    mutable std::unordered_map<const TreeNode*, std::unique_ptr<Element>> contents_;
    // Never pruned: a client may still hold one whose key went away, which then reads as empty.
    mutable std::unordered_map<std::string, std::unique_ptr<Element>> item_automation_;

    // The drag this tree started, and where a drop over it would go.
    std::optional<std::vector<std::size_t>> dragging_;
    std::optional<std::size_t>              drop_row_;
    DropZone                                drop_zone_      = DropZone::Into;
    std::size_t                             drag_hover_row_ = detail::NoIndex; // the row under a drag, for hover-expand
    double                                  hover_expand_ms_ = 0.0;

    float                       indent_width_  = 16.0f;
    mutable float               row_height_    = 0.0f;
    mutable float               chevron_width_ = 0.0f;
    mutable const TextMeasurer* measurer_      = nullptr;
    mutable const Theme*        theme_         = nullptr;
    mutable bool                multi_layout_  = false;

    Callback<void(const TreeNode*)>                       on_selection_changed_;
    Callback<void(const TreeNode&)>                       on_item_click_;
    Callback<void(const TreeNode&)>                       on_item_invoked_;
    Callback<void(const TreeNode&)>                       on_expanding_;
    Callback<void(const TreeNode&)>                       on_collapsed_;
    Callback<void(const TreeViewDragItemsCompletedArgs&)> on_drag_items_completed_;

protected:
    void enabled_changed() override;
    void focus_changed(bool focused) override;
    bool key_override(const KeyEvent& e) override;

protected:
    /// Inside the row the keyboard is on. Around the whole tree when there is none.
    [[nodiscard]] Rect  focus_ring_rect(const Theme& theme) const override;
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override;
    [[nodiscard]] bool  focus_ring_inside() const override;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.value = this;
    }
    // The selected node's text, or empty.
    [[nodiscard]] std::string text_value() const override
    {
        const TreeNode* node = selected();
        return node != nullptr ? node->text : std::string{};
    }
    [[nodiscard]] bool text_value_read_only() const override
    {
        return true;
    }
    bool set_text_value(std::string_view /*text*/) override
    {
        return false;
    }
};

/// What a ui::TreeView's `on_drag_items_completed` gets: the item that moved, its new parent
/// (nullptr for a root) and its position among the new parent's children. Valid during the call.
template <typename T>
struct TreeViewMovedArgs
{
    const T&    item;                 ///< The moved item.
    const T*    new_parent = nullptr; ///< Its new parent; nullptr for a root.
    std::size_t index      = 0;       ///< Its position among the new parent's children (or the roots).
};

namespace detail
{
// How a ui::TreeView reads an item's children: a member, or a function returning them.
template <typename T>
class TreeChildren
{
public:
    TreeChildren() = default;
    TreeChildren(std::vector<T> T::* member) : member_(member) // NOLINT(google-explicit-constructor)
    {
    }
    template <typename F>
        requires(!std::same_as<std::decay_t<F>, TreeChildren>) && std::invocable<const F&, const T&> &&
                std::convertible_to<std::invoke_result_t<const F&, const T&>, std::vector<T>>
    TreeChildren(F fn) : // NOLINT(google-explicit-constructor)
        fn_([fn = std::move(fn)](const T& item) { return std::vector<T>(std::invoke(fn, item)); })
    {
    }
    [[nodiscard]] explicit operator bool() const
    {
        return member_ != nullptr || static_cast<bool>(fn_);
    }
    // The member, when that is what was given: the tree can then write a drag back.
    [[nodiscard]] std::vector<T> T::* member() const
    {
        return member_;
    }
    // `item`'s children, kept alive in `storage` when a function made them.
    [[nodiscard]] const std::vector<T>& of(const T& item, std::deque<std::vector<T>>& storage) const
    {
        if (member_ != nullptr)
        {
            return item.*member_;
        }
        storage.push_back(fn_ ? fn_(item) : std::vector<T>{});
        return storage.back();
    }

private:
    std::vector<T> T::*                     member_ = nullptr;
    std::function<std::vector<T>(const T&)> fn_;
};

// How a ui::TreeView reads an item's key: a member or a function giving a string or a number.
template <typename T>
class TreeKey
{
public:
    TreeKey() = default;
    template <typename F>
        requires(!std::same_as<std::decay_t<F>, TreeKey>) && std::invocable<const F&, const T&> &&
                (std::convertible_to<std::invoke_result_t<const F&, const T&>, std::string> ||
                 std::integral<std::remove_cvref_t<std::invoke_result_t<const F&, const T&>>>)
    TreeKey(F fn) : // NOLINT(google-explicit-constructor)
        fn_(
            [fn = std::move(fn)](const T& item) -> std::string
            {
                if constexpr (std::convertible_to<std::invoke_result_t<const F&, const T&>, std::string>)
                {
                    return std::string(std::invoke(fn, item));
                }
                else
                {
                    return std::to_string(std::invoke(fn, item));
                }
            })
    {
    }
    [[nodiscard]] explicit operator bool() const
    {
        return static_cast<bool>(fn_);
    }
    [[nodiscard]] std::string of(const T& item) const
    {
        return fn_(item);
    }

private:
    std::function<std::string(const T&)> fn_;
};

// A ui::TreeView's items as the TreeNodes its control shows, with each node's item by key.
template <typename T>
struct TreeMirror
{
    std::vector<T>                            items;
    std::deque<std::vector<T>>                made; // children a function returned
    std::unordered_map<std::string, const T*> by_key;
    TreeChildren<T>                           children;
    TreeKey<T>                                key;
    std::function<bool(const T&)>             has_children;
    std::function<bool(const T&)>             expanded;
    typename Binding<std::vector<T>>::Write   write; // a two-way property, for drags

    [[nodiscard]] const T* find(const std::string& key_text) const
    {
        const auto it = by_key.find(key_text);
        return it != by_key.end() ? it->second : nullptr;
    }

    [[nodiscard]] std::vector<TreeNode> mirror(std::vector<T> next)
    {
        items = std::move(next);
        made.clear();
        by_key.clear();
        std::vector<TreeNode> out;
        mirror_level(items, std::string{}, out);
        return out;
    }

    void mirror_level(const std::vector<T>& level, const std::string& parent_path, std::vector<TreeNode>& out)
    {
        out.reserve(level.size());
        for (std::size_t i = 0; i < level.size(); ++i)
        {
            const T&          item = level[i];
            const std::string path = parent_path.empty() ? std::to_string(i) : parent_path + "/" + std::to_string(i);
            std::string       k    = key ? key.of(item) : path;
            by_key[k]              = &item;
            TreeNode node{.key          = std::move(k),
                          .expanded     = expanded && expanded(item),
                          .has_children = has_children && has_children(item)};
            if (children)
            {
                mirror_level(children.of(item, made), path, node.children);
            }
            out.push_back(std::move(node));
        }
    }

    // The items again from the control's tree, after a drag moved a node: only with a member
    // `.children`, which says where the children go.
    [[nodiscard]] std::vector<T> rebuild(const std::vector<TreeNode>& nodes) const
    {
        std::vector<T> out;
        out.reserve(nodes.size());
        for (const TreeNode& node : nodes)
        {
            const T* item = find(node.key);
            if (item == nullptr)
            {
                continue;
            }
            T copy = *item;
            if (children.member() != nullptr)
            {
                copy.*children.member() = rebuild(node.children);
            }
            out.push_back(std::move(copy));
        }
        return out;
    }
};
} // namespace detail

/// A hierarchy of expandable rows over the app's data. Builds a ui::TreeViewControl.
///
/// `.items` are the top-level items, and `.children` says where an item's children are: a member
/// (`&Folder::subfolders`) or a function returning them. The data is the model. The tree shows
/// it and keeps only which rows are expanded and selected, by `.key`, so that state survives a
/// changed collection, such as a reload, a rename or children loaded on expand. A function
/// `.children` can make an item's children as they are needed, and `.has_children` shows the
/// chevron before they exist. `on_expanding` loads them into the bound property, and the row
/// opens.
///
/// With a member `.children` and a bound `ui::Property`, a drag (with `can_drag_items` and
/// `allow_drop`) writes the rearranged items back.
///
/// The ready-made item is ui::TreeNode, which needs no `.children`, `.key` or `.item`. An item
/// type of its own needs `.item`, the row's content. Name the item type as for the other views,
/// `ui::TreeView<Folder>`. A braced list of ui::TreeNode works with a plain `ui::TreeView`.
///
/// Example:
/// @code
/// struct Folder
/// {
///     std::string         path;
///     std::string         name;
///     bool                has_subfolders = false;
///     std::vector<Folder> subfolders;
///     friend bool operator==(const Folder&, const Folder&) = default;
/// };
/// ui::Property<std::vector<Folder>> folders{{{"C:/", "Local Disk (C:)", true}, {"D:/", "Data (D:)", true}}};
///
/// window.set_content(ui::TreeView<Folder>{
///     .items        = folders,
///     .children     = &Folder::subfolders,
///     .key          = &Folder::path,
///     .has_children = &Folder::has_subfolders,
///     .on_expanding = [&](const Folder& f) {
///         std::vector<Folder> drives = folders.get(); // load the drive's folders lazily
///         for (Folder& drive : drives)
///             if (drive.path == f.path && drive.subfolders.empty())
///                 drive.subfolders = {{f.path + "Users", "Users"}, {f.path + "Windows", "Windows"}};
///         folders = drives;
///     },
///     .item         = [](const Folder& f) -> ui::Spec { return ui::Text{.text = f.name}; },
/// });
/// @endcode
template <typename T = TreeNode>
struct TreeView
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<TreeView>` points at.
    using control_type = TreeViewControl;

    // ---- properties ----
    /// The top-level items: a `ui::Property<std::vector<T>>` the tree follows, or a plain vector.
    Binding<std::vector<T>> items{};
    /// Where an item's children are: a member pointer, or a function returning them. Not needed
    /// for ui::TreeNode.
    detail::TreeChildren<std::type_identity_t<T>> children{};
    /// What identifies an item: a member pointer or a function giving a string or a number. Keys
    /// must be unique in the tree. Left out, an item is known by its position.
    detail::TreeKey<std::type_identity_t<T>> key{};
    /// Whether an item shows the chevron before its children exist. Load them in `on_expanding`.
    std::function<bool(const std::type_identity_t<T>&)> has_children{};
    /// Whether an item's children show when the tree first sees its key. After that the user's
    /// expanding and collapsing count. Left out, every item starts collapsed.
    std::function<bool(const std::type_identity_t<T>&)> expanded{};
    /// Indentation per nesting level, DIPs.
    Binding<float> indent_width{16.0f};
    /// None, Single (the default) or Multiple (check boxes).
    Binding<TreeViewSelectionMode> selection_mode{TreeViewSelectionMode::Single};
    /// Whether items can be dragged to another place in the tree; off by default.
    Binding<bool> can_drag_items{false};
    Binding<bool> allow_drop{false}; ///< Whether the tree accepts drops; off by default.

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The selection changed, by a click, the arrow keys, a check box, or a selected item gone from
    /// `.items`. Called with a copy of the selected item, the first one in Multiple mode, or unset
    /// when nothing is selected (see ui::SelectedItem).
    Callback<void(SelectedItem<std::type_identity_t<T>>)> on_selection_changed{};
    /// A row was clicked, before the click selects it.
    Callback<void(const std::type_identity_t<T>&)> on_item_click{};
    /// A row was opened by Enter or a double-click, and under TreeViewSelectionMode::None also by
    /// a single click.
    Callback<void(const std::type_identity_t<T>&)> on_item_invoked{};
    /// An item is opening. Load its children into the bound property here.
    Callback<void(const std::type_identity_t<T>&)> on_expanding{};
    /// An item has closed.
    Callback<void(const std::type_identity_t<T>&)> on_collapsed{};
    /// A drag moved an item. Called with the item, its new parent and its position there.
    Callback<void(const TreeViewMovedArgs<std::type_identity_t<T>>&)> on_drag_items_completed{};
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<TreeView> control{}; ///< Out: the handle that receives the built control.

    // ---- templates ----
    /// A row's content for an item. Left out, a ui::TreeNode row shows its text.
    Callback<Spec(const std::type_identity_t<T>&)> item{};
    /// The context menu of an item: a callable taking the item and returning the menu's entries.
    /// Called on a right-click on its row, or on Shift+F10 or the Menu key while it is current.
    /// The item is selected first. An empty list opens no menu. The reference is valid during the
    /// call only, so an entry's `on_click` captures what it needs by value.
    Callback<std::vector<MenuItem>(const std::type_identity_t<T>&)> item_context_menu{};

    /// Consumes the spec and returns the built ui::TreeViewControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

template <typename T>
std::unique_ptr<Element> TreeView<T>::build() &&
{
    static_assert(std::copy_constructible<T>, "a ui::TreeView keeps a copy of its items, so they must be copyable");
    auto             node = std::make_unique<TreeViewControl>();
    TreeViewControl* raw  = node.get();
    apply(indent_width, *node, [raw](const float& v) { raw->set_indent_width(v); });
    apply(selection_mode, *node, [raw](const TreeViewSelectionMode& v) { raw->set_selection_mode(v); });
    apply(enabled, *node, [raw](const bool& value) { raw->set_enabled(value); });
    apply(can_drag_items, *node, [raw](const bool& v) { raw->set_can_drag_items(v); });
    apply(allow_drop, *node, [raw](const bool& v) { raw->set_allow_drop(v); });

    if constexpr (std::same_as<T, TreeNode>)
    {
        // The nodes are the tree's own: no mirror to keep, the control's nodes are the items.
        if (items.reactive())
        {
            auto write = items.take_writer();
            if (write && on_drag_items_completed)
            {
                on_drag_items_completed =
                    [write, handler = std::move(on_drag_items_completed), raw](const TreeViewMovedArgs<TreeNode>& args) mutable
                {
                    write(raw->roots());
                    handler(args);
                };
            }
            else if (write)
            {
                on_drag_items_completed = [write, raw](const TreeViewMovedArgs<TreeNode>&) { write(raw->roots()); };
            }
        }
        apply(items, *node, [raw](const std::vector<TreeNode>& v) { raw->set_roots(v); });
        if (item)
        {
            node->set_item_template([t = std::move(item)](const TreeNode& n) mutable { return t(n).build(); });
        }
        if (on_selection_changed)
        {
            node->on_selection_changed([h = std::move(on_selection_changed)](const TreeNode* n) mutable
                                       { h(n != nullptr ? std::optional<TreeNode>{*n} : std::nullopt); });
        }
        if (on_item_click)
        {
            node->on_item_click(std::move(on_item_click));
        }
        if (on_item_invoked)
        {
            node->on_item_invoked(std::move(on_item_invoked));
        }
        if (on_expanding)
        {
            node->on_expanding(std::move(on_expanding));
        }
        if (on_collapsed)
        {
            node->on_collapsed(std::move(on_collapsed));
        }
        if (item_context_menu)
        {
            node->set_item_context_menu(std::move(item_context_menu));
        }
        if (on_drag_items_completed)
        {
            node->on_drag_items_completed(
                [h = std::move(on_drag_items_completed)](const TreeViewDragItemsCompletedArgs& args) mutable
                {
                    h(TreeViewMovedArgs<TreeNode>{.item = args.node, .new_parent = args.new_parent, .index = args.index});
                });
        }
    }
    else
    {
        assert(item && "a ui::TreeView of an item type of its own needs .item");
        auto mirror          = std::make_shared<detail::TreeMirror<T>>();
        mirror->children     = std::move(children);
        mirror->key          = std::move(key);
        mirror->has_children = std::move(has_children);
        mirror->expanded     = std::move(expanded);
        if (items.reactive() && mirror->children.member() != nullptr)
        {
            mirror->write = items.take_writer();
        }
        apply(items, *node, [raw, mirror](const std::vector<T>& v) { raw->set_roots(mirror->mirror(v)); });

        node->set_item_template(
            [mirror, t = std::move(item)](const TreeNode& n) mutable -> std::unique_ptr<Element>
            {
                const T* found = mirror->find(n.key);
                return found != nullptr ? t(*found).build() : nullptr;
            });
        // Each handler gets the item of the node, found by its key.
        const auto forward = [mirror](Callback<void(const T&)> handler) -> Callback<void(const TreeNode&)>
        {
            if (!handler)
            {
                return {};
            }
            return [mirror, h = std::move(handler)](const TreeNode& n) mutable
            {
                if (const T* found = mirror->find(n.key))
                {
                    h(*found);
                }
            };
        };
        if (auto h = forward(std::move(on_item_click)))
        {
            node->on_item_click(std::move(h));
        }
        if (auto h = forward(std::move(on_item_invoked)))
        {
            node->on_item_invoked(std::move(h));
        }
        if (auto h = forward(std::move(on_expanding)))
        {
            node->on_expanding(std::move(h));
        }
        if (auto h = forward(std::move(on_collapsed)))
        {
            node->on_collapsed(std::move(h));
        }
        if (on_selection_changed)
        {
            node->on_selection_changed(
                [mirror, h = std::move(on_selection_changed)](const TreeNode* n) mutable
                {
                    const T* found = n != nullptr ? mirror->find(n->key) : nullptr;
                    if constexpr (std::copy_constructible<T>)
                    {
                        h(found != nullptr ? std::optional<T>{*found} : std::nullopt);
                    }
                });
        }
        if (item_context_menu)
        {
            node->set_item_context_menu(
                [mirror, menu = std::move(item_context_menu)](const TreeNode& n) mutable -> std::vector<MenuItem>
                {
                    const T* found = mirror->find(n.key);
                    return found != nullptr ? menu(*found) : std::vector<MenuItem>{};
                });
        }
        if (on_drag_items_completed || mirror->write)
        {
            node->on_drag_items_completed(
                [mirror, raw, h = std::move(on_drag_items_completed)](const TreeViewDragItemsCompletedArgs& args) mutable
                {
                    const T* moved  = mirror->find(args.node.key);
                    const T* parent = args.new_parent != nullptr ? mirror->find(args.new_parent->key) : nullptr;
                    if (h && moved != nullptr)
                    {
                        h(TreeViewMovedArgs<T>{.item = *moved, .new_parent = parent, .index = args.index});
                    }
                    if (mirror->write)
                    {
                        mirror->write(mirror->rebuild(raw->roots())); // comes back as the same tree
                    }
                });
        }
    }

    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
