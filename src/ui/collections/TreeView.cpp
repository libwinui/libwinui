// SPDX-License-Identifier: MIT
#include "winui/ui/collections/TreeView.hpp"

#include "../../render/ControlGlyphs.hpp"
#include "../menus/ContextMenuPopup.hpp"
#include "../themes/ControlChrome.hpp"
#include "ItemChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/RenderRecording.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/DragDrop.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
constexpr float TreeViewHoverOverlayAlpha = 0.10f;

// The chevron's column: a 12px glyph box with 14 DIP of padding left and right.
constexpr float TreeViewChevronColumnWidth = 40.0f;
constexpr float ChevronGlyphBox            = 12.0f;
constexpr float ChevronPadding             = 14.0f;
// Multiple selection mode: the check box's
// 32px cell 10 in from the indentation, the chevron's left padding dropped.
constexpr float CheckMargin        = 10.0f;
constexpr float CheckCell          = 32.0f;
constexpr float TreeViewCheckGlyph = 12.0f;
// SM_CXDRAG's default, and how long a drag hovers on a collapsed node before it opens (1 s).
constexpr float  TreeViewDragThreshold = 4.0f;
constexpr double HoverExpandMs         = 1000.0;
constexpr float  DropLineWidth         = 2.0f;

// The backplate is inset from the row's slot.
[[nodiscard]] Rect row_chip(const Rect& row)
{
    constexpr float InsetX = 4.0f;
    constexpr float InsetY = 2.0f;
    return Rect{row.left() + InsetX,
                row.top() + InsetY,
                std::max(0.0f, row.width - 2.0f * InsetX),
                std::max(0.0f, row.height - 2.0f * InsetY)};
}

// The selection indicator: a 3x16 accent pill, corner radius 2, at the chip's left.
void draw_row_pill(Renderer& renderer, const Rect& chip, const Color& fill)
{
    constexpr float Width  = 3.0f;
    constexpr float Height = 16.0f;
    constexpr float Radius = 2.0f;
    const Rect      pill{chip.left(), chip.top() + (chip.height - Height) * 0.5f, Width, Height};
    renderer.fill_rounded_rect(pill, Radius, fill);
}

// What a TreeView drag carries: the tree, and where in it the dragged node was.
struct TreeDragPayload
{
    const TreeViewControl*   source = nullptr;
    std::vector<std::size_t> path;
};

bool is_prefix(const std::vector<std::size_t>& prefix, const std::vector<std::size_t>& path)
{
    return prefix.size() <= path.size() && std::equal(prefix.begin(), prefix.end(), path.begin());
}

TreeNode* find_parent(std::vector<TreeNode>& nodes, const TreeNode* target)
{
    for (TreeNode& node : nodes)
    {
        for (TreeNode& child : node.children)
        {
            if (&child == target)
            {
                return &node;
            }
        }
        if (TreeNode* found = find_parent(node.children, target))
        {
            return found;
        }
    }
    return nullptr;
}

// The first accessible name in `root`'s automation subtree, depth first.
std::string first_automation_name(const Element& root)
{
    std::string name = root.automation_name();
    if (!name.empty())
    {
        return name;
    }
    for (const Element* child : automation_children(root))
    {
        name = first_automation_name(*child);
        if (!name.empty())
        {
            return name;
        }
    }
    return {};
}

} // namespace

// ---- automation nodes ----
// A row is drawn by the tree and has no Element, so each node gets one of these when a client
// first asks for it. It finds its node by key on every call, so it stays valid across set_roots().
class TreeViewItemAutomation final : public Element, private SelectionItemProvider, private ExpandCollapseProvider, private InvokeProvider
{
public:
    TreeViewItemAutomation(TreeViewControl* owner, std::string key) : owner_(owner), key_(std::move(key))
    {
        set_parent_of(*this, owner);
    }

    void place(const Rect& rect)
    {
        detail::LayoutAccess::set_bounds(*this, rect);
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::TreeItem;
    }
    // The row's content, then the nodes of its children while it is expanded.
    void automation_children(std::vector<Element*>& out) const override
    {
        const TreeNode* n = node();
        if (n == nullptr)
        {
            return;
        }
        if (Element* content = this->content(*n))
        {
            out.push_back(content);
        }
        if (owner_->is_expanded(*n))
        {
            for (const TreeNode& child : n->children)
            {
                out.push_back(owner_->item_automation(child));
            }
        }
    }

protected:
    // The node's text; a templated row reads as the first name its content gives.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        const TreeNode* n = node();
        if (n == nullptr)
        {
            return {};
        }
        if (!n->text.empty())
        {
            return n->text;
        }
        const Element* shown = content(*n);
        return shown != nullptr ? first_automation_name(*shown) : std::string{};
    }

    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

    void automation_providers(AutomationProviders& out) override
    {
        const TreeNode* n = node();
        if (n == nullptr)
        {
            return;
        }
        if (owner_->selection_mode_ != TreeViewSelectionMode::None)
        {
            out.selection_item = this;
        }
        if (!n->children.empty() || n->has_children)
        {
            out.expand_collapse = this;
        }
        if (owner_->on_item_invoked_)
        {
            out.invoke = this;
        }
    }

private:
    [[nodiscard]] TreeNode* node() const
    {
        return const_cast<TreeNode*>(owner_->find(key_)); // NOLINT(cppcoreguidelines-pro-type-const-cast): the tree's own node
    }
    [[nodiscard]] Element* content(const TreeNode& n) const
    {
        const auto it = owner_->contents_.find(&n);
        return it != owner_->contents_.end() ? it->second.get() : nullptr;
    }
    [[nodiscard]] bool usable() const
    {
        return owner_->is_enabled() && owner_->selection_mode_ != TreeViewSelectionMode::None && node() != nullptr;
    }
    void selection_changed()
    {
        owner_->mark_dirty(DirtyFlags::Visual);
        if (owner_->on_selection_changed_)
        {
            owner_->on_selection_changed_(owner_->selected());
        }
    }

    // ---- SelectionItem ----
    [[nodiscard]] bool item_selected() const override
    {
        const TreeNode* n = node();
        return n != nullptr && owner_->is_selected(*n);
    }
    // Selects this node alone, as a click does.
    bool select_item() override
    {
        if (!usable())
        {
            return false;
        }
        if (owner_->selection_mode_ == TreeViewSelectionMode::Single)
        {
            owner_->select_single(node());
            return true;
        }
        owner_->clear_selection();
        owner_->set_subtree(*node(), true);
        owner_->sync_parents(owner_->roots_);
        owner_->current_ = node();
        selection_changed();
        return true;
    }
    bool add_to_selection() override
    {
        if (!usable())
        {
            return false;
        }
        if (owner_->selection_mode_ == TreeViewSelectionMode::Single)
        {
            return select_item();
        }
        if (owner_->selection_state(*node()) != TreeNodeSelectionState::Selected)
        {
            owner_->toggle_multi(node());
        }
        return true;
    }
    bool remove_from_selection() override
    {
        if (!usable())
        {
            return false;
        }
        TreeNode* n = node();
        if (owner_->selection_state(*n) == TreeNodeSelectionState::Unselected)
        {
            return true;
        }
        owner_->set_subtree(*n, false);
        if (owner_->selection_mode_ == TreeViewSelectionMode::Multiple)
        {
            owner_->sync_parents(owner_->roots_);
        }
        selection_changed();
        return true;
    }

    // ---- ExpandCollapse ----
    [[nodiscard]] bool expanded_state() const override
    {
        const TreeNode* n = node();
        return n != nullptr && owner_->is_expanded(*n);
    }
    bool set_expanded_state(bool expanded) override
    {
        if (!owner_->is_enabled() || node() == nullptr)
        {
            return false;
        }
        owner_->set_node_expanded(node(), expanded);
        return true;
    }

    // ---- Invoke ----
    // Opens the node as Enter does: it is selected first.
    bool invoke() override
    {
        if (!owner_->is_enabled() || node() == nullptr || !owner_->on_item_invoked_)
        {
            return false;
        }
        if (owner_->selection_mode_ == TreeViewSelectionMode::Single)
        {
            owner_->select_single(node()); // its handler may set a new tree
        }
        if (const TreeNode* n = node(); n != nullptr && owner_->on_item_invoked_)
        {
            owner_->on_item_invoked_(*n);
        }
        return true;
    }

    TreeViewControl* owner_;
    std::string      key_;
};

TreeViewControl::TreeViewControl() = default;

Element* TreeViewControl::item_automation(const TreeNode& node) const
{
    const std::string& key  = key_of(node);
    auto&              slot = item_automation_[key];
    if (!slot)
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): a cache
        auto made = std::make_unique<TreeViewItemAutomation>(const_cast<TreeViewControl*>(this), key);
        if (const std::optional<std::size_t> row = row_of(&node))
        {
            made->place(row_rect(*row));
        }
        slot = std::move(made);
    }
    return slot.get();
}

void TreeViewControl::set_roots(std::vector<TreeNode> roots)
{
    const TreeNode*   before_node = selected();
    const std::string before      = before_node != nullptr ? key_of(*before_node) : std::string{};
    const bool        had         = before_node != nullptr;
    roots_                        = std::move(roots);
    structure_changed();
    // A key not seen before takes its node's own `expanded`; a key that is gone is forgotten.
    std::unordered_map<std::string, NodeState> kept;
    for (const auto& [node, key] : keys_)
    {
        const auto it = states_.find(key);
        kept.emplace(key, it != states_.end() ? it->second : NodeState{.expanded = node->expanded, .selected = false});
    }
    states_ = std::move(kept);
    if (selection_mode_ == TreeViewSelectionMode::Multiple)
    {
        sync_parents(roots_);
    }
    const TreeNode* after = selected();
    if ((after != nullptr) != had || (after != nullptr && key_of(*after) != before))
    {
        if (on_selection_changed_)
        {
            on_selection_changed_(after);
        }
    }
}

void TreeViewControl::structure_changed()
{
    current_     = nullptr;
    hovered_row_ = detail::NoIndex;
    pressed_row_ = detail::NoIndex;
    ++tree_version_;
    contents_.clear(); // node addresses may have moved
    index_keys();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

// ---- the state kept by key ----

void TreeViewControl::index_keys()
{
    keys_.clear();
    by_key_.clear();
    std::vector<std::pair<std::vector<TreeNode>*, std::string>> pending{{&roots_, std::string{}}};
    while (!pending.empty())
    {
        auto [level, prefix] = std::move(pending.back());
        pending.pop_back();
        for (TreeNode& node : *level)
        {
            std::string path       = prefix.empty() ? node.text : prefix + "/" + node.text;
            std::string key        = node.key.empty() ? path : node.key;
            const auto [it, added] = keys_.emplace(&node, std::move(key));
            by_key_.emplace(it->second, &node); // the first node with a key wins
            pending.emplace_back(&node.children, std::move(path));
        }
    }
}

const std::string& TreeViewControl::key_of(const TreeNode& node) const
{
    static const std::string none;
    const auto               it = keys_.find(&node);
    return it != keys_.end() ? it->second : none;
}

const TreeNode* TreeViewControl::find(std::string_view key) const
{
    const auto it = by_key_.find(key);
    return it != by_key_.end() ? it->second : nullptr;
}

TreeViewControl::NodeState& TreeViewControl::state_of(const TreeNode& node)
{
    const std::string& key = key_of(node);
    const auto         it  = states_.find(key);
    if (it != states_.end())
    {
        return it->second;
    }
    return states_.emplace(key, NodeState{.expanded = node.expanded, .selected = false}).first->second;
}

bool TreeViewControl::is_expanded(const TreeNode& node) const
{
    const auto it = states_.find(key_of(node));
    return it != states_.end() ? it->second.expanded : node.expanded;
}

bool TreeViewControl::is_selected(const TreeNode& node) const
{
    const auto it = states_.find(key_of(node));
    return it != states_.end() && it->second.selected;
}

void TreeViewControl::set_subtree(const TreeNode& node, bool selected)
{
    state_of(node).selected = selected;
    for (const TreeNode& child : node.children)
    {
        set_subtree(child, selected);
    }
}

void TreeViewControl::clear_selection()
{
    for (auto& [key, state] : states_)
    {
        state.selected = false;
    }
}

// A parent's flag follows its children, bottom up.
void TreeViewControl::sync_parents(const std::vector<TreeNode>& nodes)
{
    for (const TreeNode& n : nodes)
    {
        if (!n.children.empty())
        {
            sync_parents(n.children);
            state_of(n).selected = selection_state(n) == TreeNodeSelectionState::Selected;
        }
    }
}

void TreeViewControl::collect_selected(const std::vector<TreeNode>& nodes, std::vector<const TreeNode*>& out) const
{
    for (const TreeNode& n : nodes)
    {
        if (is_selected(n))
        {
            out.push_back(&n);
        }
        collect_selected(n.children, out);
    }
}

void TreeViewControl::set_item_template(ItemTemplate item_template)
{
    item_template_ = std::move(item_template);
    contents_.clear();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TreeViewControl::set_selection_mode(TreeViewSelectionMode mode)
{
    if (selection_mode_ == mode)
    {
        return;
    }
    selection_mode_ = mode;
    if (mode == TreeViewSelectionMode::None)
    {
        clear_selection();
    }
    else if (mode == TreeViewSelectionMode::Single)
    {
        // One node at most: the first.
        const TreeNode* keep = selected();
        clear_selection();
        if (keep != nullptr)
        {
            state_of(*keep).selected = true;
        }
    }
    else
    {
        sync_parents(roots_);
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TreeViewControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_row_ = detail::NoIndex;
        pressed_row_ = detail::NoIndex;
    }
    mark_dirty(DirtyFlags::Visual);
}

void TreeViewControl::focus_changed(bool /*focused*/)
{
    mark_dirty(DirtyFlags::Visual);
}

void TreeViewControl::set_indent_width(float width)
{
    indent_width_ = width;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

// ---- rows ----

void TreeViewControl::flatten_into(std::vector<TreeNode>&    nodes,
                                   std::size_t               depth,
                                   std::vector<std::size_t>& path,
                                   std::vector<VisibleRow>&  out) const
{
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        TreeNode& node = nodes[i];
        path.push_back(i);
        out.push_back(VisibleRow{&node, depth, !node.children.empty() || node.has_children, path});
        if (is_expanded(node) && !node.children.empty())
        {
            flatten_into(node.children, depth + 1, path, out);
        }
        path.pop_back();
    }
}

std::vector<TreeViewControl::VisibleRow> TreeViewControl::flatten() const
{
    std::vector<VisibleRow>  rows;
    std::vector<std::size_t> path;
    // The pointers are only written through by the non-const members; measure()/render() read.
    flatten_into(const_cast<std::vector<TreeNode>&>(roots_), 0, path, rows);
    return rows;
}

std::size_t TreeViewControl::visible_row_count() const
{
    return flatten().size();
}

const TreeNode* TreeViewControl::visible_node_at(std::size_t index) const
{
    const auto rows = flatten();
    return index < rows.size() ? rows[index].node : nullptr;
}

Rect TreeViewControl::row_rect(std::size_t visible_index) const
{
    return Rect{bounds().left(), bounds().top() + row_height_ * static_cast<float>(visible_index), bounds().width, row_height_};
}

std::size_t TreeViewControl::row_at(Point p) const
{
    if (!bounds().contains(p) || row_height_ <= 0.0f)
    {
        return detail::NoIndex;
    }
    const auto index = static_cast<std::size_t>((p.y - bounds().top()) / row_height_);
    return index < visible_row_count() ? index : detail::NoIndex;
}

TreeNode* TreeViewControl::node_at(const std::vector<std::size_t>& path)
{
    std::vector<TreeNode>* level = &roots_;
    TreeNode*              node  = nullptr;
    for (std::size_t i : path)
    {
        if (i >= level->size())
        {
            return nullptr;
        }
        node  = &(*level)[i];
        level = &node->children;
    }
    return node;
}

std::optional<std::size_t> TreeViewControl::row_of(const TreeNode* node) const
{
    const auto rows = flatten();
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].node == node)
        {
            return i;
        }
    }
    return std::nullopt;
}

float TreeViewControl::check_left(std::size_t depth) const
{
    return bounds().left() + static_cast<float>(depth) * indent_width_ + CheckMargin;
}

float TreeViewControl::chevron_left(std::size_t depth) const
{
    const float indent = bounds().left() + static_cast<float>(depth) * indent_width_;
    return multi_layout_ ? indent + CheckMargin + CheckCell : indent;
}

float TreeViewControl::content_left(std::size_t depth) const
{
    const float chevron = multi_layout_ ? ChevronGlyphBox + ChevronPadding : chevron_width_;
    return chevron_left(depth) + chevron;
}

// ---- selection ----

TreeNodeSelectionState TreeViewControl::selection_state(const TreeNode& node) const
{
    if (node.children.empty())
    {
        return is_selected(node) ? TreeNodeSelectionState::Selected : TreeNodeSelectionState::Unselected;
    }
    bool any_selected   = false;
    bool any_unselected = false;
    for (const TreeNode& child : node.children)
    {
        switch (selection_state(child))
        {
            case TreeNodeSelectionState::Selected:
                any_selected = true;
                break;
            case TreeNodeSelectionState::Unselected:
                any_unselected = true;
                break;
            case TreeNodeSelectionState::Partial:
                return TreeNodeSelectionState::Partial;
        }
    }
    if (any_selected && any_unselected)
    {
        return TreeNodeSelectionState::Partial;
    }
    return any_selected ? TreeNodeSelectionState::Selected : TreeNodeSelectionState::Unselected;
}

const TreeNode* TreeViewControl::selected() const
{
    std::vector<const TreeNode*> nodes;
    collect_selected(roots_, nodes);
    return nodes.empty() ? nullptr : nodes.front();
}

std::vector<const TreeNode*> TreeViewControl::selected_nodes() const
{
    std::vector<const TreeNode*> nodes;
    collect_selected(roots_, nodes);
    return nodes;
}

void TreeViewControl::select_all()
{
    if (selection_mode_ != TreeViewSelectionMode::Multiple)
    {
        return;
    }
    for (const TreeNode& root : roots_)
    {
        set_subtree(root, true);
    }
    mark_dirty(DirtyFlags::Visual);
    if (on_selection_changed_ && !roots_.empty())
    {
        on_selection_changed_(selected());
    }
}

void TreeViewControl::select_single(TreeNode* node)
{
    if (node == nullptr || selection_mode_ != TreeViewSelectionMode::Single)
    {
        return;
    }
    if (is_selected(*node) && selected() == node)
    {
        current_ = node;
        return;
    }
    clear_selection();
    state_of(*node).selected = true;
    current_                 = node;
    mark_dirty(DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(selected());
    }
}

void TreeViewControl::toggle_multi(TreeNode* node)
{
    if (node == nullptr || selection_mode_ != TreeViewSelectionMode::Multiple)
    {
        return;
    }
    // A node and its whole subtree take the new state (from partial, selected); the ancestors follow.
    const bool select = selection_state(*node) != TreeNodeSelectionState::Selected;
    set_subtree(*node, select);
    sync_parents(roots_);
    current_ = node;
    mark_dirty(DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(selected());
    }
}

void TreeViewControl::set_expanded(std::string_view key, bool expanded)
{
    set_node_expanded(const_cast<TreeNode*>(find(key)), expanded);
}

void TreeViewControl::set_node_expanded(TreeNode* node, bool expanded)
{
    if (node == nullptr || is_expanded(*node) == expanded)
    {
        return;
    }
    if (expanded)
    {
        if (on_expanding_)
        {
            // The handler may set a new tree with the node's children in it: find it again by key.
            const std::string   key     = key_of(*node);
            const std::uint64_t version = tree_version_;
            on_expanding_(*node);
            if (tree_version_ != version)
            {
                node = const_cast<TreeNode*>(find(key));
                if (node == nullptr)
                {
                    return;
                }
            }
        }
        if (node->children.empty() && !node->has_children)
        {
            return;
        }
        state_of(*node).expanded = true;
        if (selection_mode_ == TreeViewSelectionMode::Multiple && is_selected(*node))
        {
            set_subtree(*node, true); // children added under a selected node take its state
        }
    }
    else
    {
        state_of(*node).expanded = false;
        if (on_collapsed_)
        {
            on_collapsed_(*node);
        }
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TreeViewControl::toggle_expanded(TreeNode* node)
{
    if (node == nullptr || (node->children.empty() && !node->has_children))
    {
        return;
    }
    set_node_expanded(node, !is_expanded(*node));
}

void TreeViewControl::select(std::string_view key)
{
    if (selection_mode_ != TreeViewSelectionMode::Single)
    {
        return;
    }
    if (TreeNode* node = const_cast<TreeNode*>(find(key)))
    {
        select_single(node);
        return;
    }
    if (selected() != nullptr)
    {
        clear_selection();
        mark_dirty(DirtyFlags::Visual);
        if (on_selection_changed_)
        {
            on_selection_changed_(nullptr);
        }
    }
}

// ---- layout ----

Size TreeViewControl::measure_override(Size available, const MeasureContext& context) const
{
    measurer_     = &context.measurer;
    theme_        = &context.theme;
    multi_layout_ = selection_mode_ == TreeViewSelectionMode::Multiple;
    if (detail::controls(context.theme).list_item_subtle_selection)
    {
        // A 20 DIP floor on the row's content, plus 3 + 5 of padding.
        const float content_height = std::max(context.measurer.measure("", context.theme.text_style).height, 20.0f);
        row_height_                = content_height + 8.0f;
    }
    else
    {
        row_height_ = context.measurer.measure("", context.theme.text_style).height + context.theme.padding.vertical();
    }
    chevron_width_ = detail::controls(context.theme).list_item_subtle_selection ? TreeViewChevronColumnWidth : row_height_;
    const float trailing = detail::controls(context.theme).list_item_subtle_selection
                               ? 0.0f
                               : context.theme.padding.horizontal();

    const auto rows = flatten();
    // A template's content can be taller than a text row; the rows keep one height, the tallest.
    float       max_width = 0.0f;
    const float lead      = multi_layout_ ? CheckMargin + CheckCell + ChevronGlyphBox + ChevronPadding : chevron_width_;
    for (const auto& row : rows)
    {
        const float indent = static_cast<float>(row.depth) * indent_width_;
        float       content_w;
        if (item_template_)
        {
            auto& content = contents_[row.node];
            if (!content)
            {
                content = item_template_(*row.node);
                if (content)
                {
                    set_parent_of(*content, const_cast<TreeViewControl*>(this));
                }
            }
            const Size d = content ? content->measure(Size{Unbounded, Unbounded}, context) : Size{};
            content_w    = d.width;
            row_height_ = std::max(row_height_,
                                   d.height + (detail::controls(context.theme).list_item_subtle_selection ? 8.0f : 0.0f));
        }
        else
        {
            content_w = context.measurer.measure(row.node->text, context.theme.text_style).width;
        }
        max_width = std::max(max_width, indent + lead + content_w + trailing);
    }

    // Content made for nodes that are no longer showing goes.
    if (item_template_ && contents_.size() > rows.size())
    {
        std::erase_if(contents_,
                      [&rows](const auto& entry)
                      {
                          return std::none_of(rows.begin(),
                                              rows.end(),
                                              [&](const VisibleRow& r) { return r.node == entry.first; });
                      });
    }

    Size desired{max_width, row_height_ * static_cast<float>(rows.size())};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void TreeViewControl::arrange_override(const Rect&, const ArrangeContext&)
{
    const auto rows = flatten();
    if (!item_automation_.empty())
    {
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (const auto it = item_automation_.find(key_of(*rows[i].node)); it != item_automation_.end())
            {
                static_cast<TreeViewItemAutomation&>(*it->second).place(row_rect(i));
            }
        }
    }
    if (!item_template_)
    {
        return;
    }
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        const auto it = contents_.find(rows[i].node);
        if (it == contents_.end() || !it->second)
        {
            continue;
        }
        const Rect  r = row_rect(i);
        const float x = content_left(rows[i].depth);
        it->second->arrange(Rect{x, r.y, std::max(0.0f, r.right() - x), r.height});
    }
}

// ---- input ----

bool TreeViewControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
        {
            const std::size_t row = row_at(e.position);
            if (row != hovered_row_)
            {
                hovered_row_ = row;
                mark_dirty(DirtyFlags::Visual);
            }
            if (pressed_row_ != detail::NoIndex && can_drag_items_ &&
                (std::abs(e.position.x - press_point_.x) > TreeViewDragThreshold ||
                 std::abs(e.position.y - press_point_.y) > TreeViewDragThreshold))
            {
                const std::size_t from = pressed_row_;
                pressed_row_           = detail::NoIndex;
                start_drag(from, e.position);
            }
            break;
        }
        case PointerAction::Down:
            if (e.button == PointerButton::Left)
            {
                pressed_row_  = row_at(e.position);
                press_clicks_ = e.click_count;
                press_point_  = e.position;
                mark_dirty(DirtyFlags::Visual);
                handled = pressed_row_ != detail::NoIndex;
            }
            break;
        case PointerAction::Up:
        {
            // A drag this tree started that no window took over ends here, with nothing done.
            if (DragDrop::pending())
            {
                DragDrop::cancel();
                dragging_.reset();
            }
            const std::size_t pressed = pressed_row_;
            pressed_row_              = detail::NoIndex;
            handled                   = pressed != detail::NoIndex;
            mark_dirty(DirtyFlags::Visual);
            if (e.button != PointerButton::Left)
            {
                break;
            }
            const std::size_t row = row_at(e.position);
            if (row == detail::NoIndex || row != pressed)
            {
                break;
            }
            const auto        rows = flatten();
            const VisibleRow& r    = rows[row];
            const float       cx   = chevron_left(r.depth);
            const float       cw   = multi_layout_ ? ChevronGlyphBox + ChevronPadding : chevron_width_;
            if (r.has_children && e.position.x >= cx && e.position.x < cx + cw)
            {
                toggle_expanded(r.node); // a press on the chevron toggles at once
                break;
            }
            if (multi_layout_)
            {
                const float left = check_left(r.depth);
                if (e.position.x >= left && e.position.x < left + CheckCell)
                {
                    toggle_multi(r.node); // the check box
                    break;
                }
            }
            TreeNode* const     node    = r.node;
            const std::uint64_t version = tree_version_;
            if (on_item_click_)
            {
                on_item_click_(*node);
                if (tree_version_ != version)
                {
                    break; // the handler replaced the tree: the node is gone
                }
            }
            if (multi_layout_)
            {
                current_ = node; // a click on the row moves the cursor; its check box selects
            }
            else
            {
                select_single(node);
            }
            const bool opens = press_clicks_ >= 2 || selection_mode_ == TreeViewSelectionMode::None;
            if (opens && on_item_invoked_ && tree_version_ == version)
            {
                on_item_invoked_(*node);
            }
            break;
        }
        case PointerAction::Leave:
            hovered_row_ = detail::NoIndex;
            pressed_row_ = detail::NoIndex;
            mark_dirty(DirtyFlags::Visual);
            break;
        default:
            break;
    }
    return handled;
}

bool TreeViewControl::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused() || roots_.empty() || e.action != KeyAction::Down)
    {
        return handled;
    }
    const auto rows = flatten();
    if (rows.empty())
    {
        return handled;
    }
    const bool multi = selection_mode_ == TreeViewSelectionMode::Multiple;
    TreeNode* focus = multi || selection_mode_ == TreeViewSelectionMode::None ? current_ : const_cast<TreeNode*>(selected());
    std::size_t current = detail::NoIndex;
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].node == focus)
        {
            current = i;
            break;
        }
    }
    // Arrows move the selection in Single mode, the focus alone otherwise.
    const auto move_to = [&](TreeNode* node)
    {
        if (selection_mode_ == TreeViewSelectionMode::Single)
        {
            select_single(node);
        }
        else
        {
            current_ = node;
            mark_dirty(DirtyFlags::Visual);
        }
    };
    switch (e.key)
    {
        case Key::ArrowDown:
            if (current == detail::NoIndex)
            {
                move_to(rows.front().node);
            }
            else if (current + 1 < rows.size())
            {
                move_to(rows[current + 1].node);
            }
            break;
        case Key::ArrowUp:
            if (current == detail::NoIndex)
            {
                move_to(rows.front().node);
            }
            else if (current > 0)
            {
                move_to(rows[current - 1].node);
            }
            break;
        case Key::ArrowRight:
            if (current != detail::NoIndex)
            {
                const VisibleRow& row = rows[current];
                if (row.has_children && !is_expanded(*row.node))
                {
                    set_node_expanded(row.node, true);
                }
                else if (is_expanded(*row.node) && !row.node->children.empty())
                {
                    move_to(&row.node->children.front());
                }
            }
            break;
        case Key::ArrowLeft:
            if (current != detail::NoIndex)
            {
                const VisibleRow& row = rows[current];
                if (row.has_children && is_expanded(*row.node))
                {
                    set_node_expanded(row.node, false);
                }
                else if (TreeNode* parent = find_parent(roots_, row.node))
                {
                    move_to(parent);
                }
            }
            break;
        case Key::Space:
            if (current != detail::NoIndex)
            {
                // Multiple: Space toggles the check box. Otherwise the expansion, as this
                // tree's Space always has.
                if (multi)
                {
                    toggle_multi(rows[current].node);
                }
                else
                {
                    toggle_expanded(rows[current].node);
                }
            }
            break;
        case Key::Enter:
            if (current != detail::NoIndex && on_item_invoked_)
            {
                on_item_invoked_(*rows[current].node);
            }
            break;
        case Key::A:
            if (e.modifiers.ctrl && multi)
            {
                select_all();
            }
            break;
        default:
            break;
    }
    return handled;
}

bool TreeViewControl::raise_context_requested(std::optional<Point> position)
{
    const auto  rows = flatten();
    std::size_t row  = detail::NoIndex;
    if (position)
    {
        row = row_at(*position);
    }
    else if (current_ != nullptr)
    {
        row = row_of(current_).value_or(detail::NoIndex);
    }
    if (position && item_template_ && row != detail::NoIndex && row < rows.size())
    {
        const auto it = contents_.find(rows[row].node);
        if (it != contents_.end() && it->second && raise_context_requested_in(*it->second, position))
        {
            return true;
        }
    }
    if (!item_context_menu_ || row == detail::NoIndex || row >= rows.size())
    {
        return Element::raise_context_requested(position);
    }
    TreeNode*             node    = rows[row].node;
    std::vector<MenuItem> entries = item_context_menu_(*node);
    if (entries.empty())
    {
        return Element::raise_context_requested(position);
    }
    if (!is_selected(*node))
    {
        select_single(node);
    }
    current_ = node;
    mark_dirty(DirtyFlags::Visual);
    detail::show_context_menu(item_menu_, *this, std::move(entries), position.value_or(row_rect(row).center()));
    return true;
}

// ---- drag and drop ----

void TreeViewControl::start_drag(std::size_t row, Point pointer)
{
    if (DragDrop::active())
    {
        return;
    }
    const auto rows = flatten();
    if (row >= rows.size())
    {
        return;
    }
    const VisibleRow& r = rows[row];
    DataPackage       data;
    data.text                = r.node->text;
    data.requested_operation = DataPackageOperation::Move;
    data.data                = TreeDragPayload{this, r.path};

    // A single node drags collapsed.
    set_node_expanded(r.node, false);
    RenderRecording recording;
    const Rect      rect = row_rect(row);
    if (theme_ != nullptr)
    {
        const Rect chip = row_chip(rect);
        recording.fill_rounded_rect(chip,
                                    theme_->corner_radius,
                                    detail::subtle_row_background(*theme_, ButtonState::Hover, false));
        TextStyle style  = theme_->text_style;
        style.horizontal = TextAlign::Leading;
        const float x    = content_left(r.depth);
        recording.draw_text(r.node->text,
                            Rect{x, rect.y, std::max(0.0f, rect.right() - x), rect.height},
                            theme_->colors().text,
                            style);
    }
    dragging_ = r.path;
    DragDrop::start(*this, std::move(data), std::move(recording), rect, pointer);
}

std::optional<TreeViewControl::DropZone> TreeViewControl::drop_zone(Point p, std::size_t dragged, std::size_t& row) const
{
    const std::size_t under = row_at(p);
    if (under == detail::NoIndex)
    {
        return std::nullopt;
    }
    // The middle of the row, 0.2 to 0.8 of its height, drops into it.
    const Rect  r = row_rect(under);
    const float t = (p.y - r.y) / std::max(1.0f, r.height);
    if (t > 0.2f && t < 0.8f)
    {
        row = under;
        return DropZone::Into;
    }
    // Otherwise the gap above or below the row, turned into an empty slot:
    // dragged down, after the row above the gap; dragged up, before the row below it.
    const std::size_t count = visible_row_count();
    const std::size_t gap   = t <= 0.5f ? under : under + 1;
    if (gap == 0)
    {
        row = 0; // the top of the list
    }
    else if (gap >= count)
    {
        row = count - 1; // after the last row
    }
    else if (dragged < gap)
    {
        row = gap - 1;
    }
    else
    {
        row = gap;
    }
    if (row == dragged)
    {
        return DropZone::Stay;
    }
    // After the slot's node when the dragged one is above it.
    return dragged < row ? DropZone::After : DropZone::Before;
}

std::size_t TreeViewControl::dragged_row(const std::vector<std::size_t>& path) const
{
    const auto rows = flatten();
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].path == path)
        {
            return i;
        }
    }
    return detail::NoIndex;
}

bool TreeViewControl::can_drop(const std::vector<std::size_t>& source, std::size_t row, DropZone zone) const
{
    const auto rows = flatten();
    if (row >= rows.size())
    {
        return false;
    }
    if (zone == DropZone::Stay)
    {
        return true; // accepted, and moves nothing
    }
    // Not onto itself, nor anywhere in its own subtree.
    if (is_prefix(source, rows[row].path))
    {
        return false;
    }
    // A reorder in Multiple selection may not land on a selected node's slot.
    return zone == DropZone::Into || selection_mode_ != TreeViewSelectionMode::Multiple || !is_selected(*rows[row].node);
}

void TreeViewControl::drag_enter(const DragEventArgs& e)
{
    Element::drag_enter(e);
}

DragResponse TreeViewControl::drag_over(const DragEventArgs& e)
{
    const auto* payload = std::any_cast<TreeDragPayload>(&e.data.data);
    if (payload == nullptr || payload->source != this)
    {
        return Element::drag_over(e); // someone else's drag: the app's handlers
    }
    std::size_t                   row  = detail::NoIndex;
    const std::optional<DropZone> zone = drop_zone(e.position, dragged_row(payload->path), row);
    const bool                    ok   = zone && can_drop(payload->path, row, *zone);
    drop_row_                          = ok && *zone != DropZone::Stay ? std::optional<std::size_t>{row} : std::nullopt;
    if (zone)
    {
        drop_zone_ = *zone;
    }
    // The hover-expand timer runs while the pointer stays on one row (a drag entering starts it,
    // one leaving the row stops it).
    const std::size_t under = row_at(e.position);
    if (under != drag_hover_row_)
    {
        drag_hover_row_  = under;
        hover_expand_ms_ = 0.0;
    }
    mark_dirty(DirtyFlags::Visual);
    return DragResponse{.operation = ok ? DataPackageOperation::Move : DataPackageOperation::None};
}

void TreeViewControl::drag_leave(const DragEventArgs& e)
{
    drop_row_.reset();
    drag_hover_row_  = detail::NoIndex;
    hover_expand_ms_ = 0.0;
    mark_dirty(DirtyFlags::Visual);
    Element::drag_leave(e);
}

void TreeViewControl::drop(const DragEventArgs& e)
{
    const auto* payload = std::any_cast<TreeDragPayload>(&e.data.data);
    if (payload == nullptr || payload->source != this)
    {
        Element::drop(e);
        return;
    }
    std::size_t                   row  = detail::NoIndex;
    const std::optional<DropZone> zone = drop_zone(e.position, dragged_row(payload->path), row);
    drop_row_.reset();
    drag_hover_row_ = detail::NoIndex;
    if (!zone || !can_drop(payload->path, row, *zone) || *zone == DropZone::Stay)
    {
        return; // over its own row: dropped where it was
    }
    const auto               rows   = flatten();
    std::vector<std::size_t> target = rows[row].path;
    std::vector<std::size_t> source = payload->path;
    // Read before the node is taken out: that moves its siblings, and the state is found by address.
    const bool target_open = is_expanded(*rows[row].node);

    // Take the node out, then fix the target's path for the gap it left.
    std::vector<TreeNode>* source_level = &roots_;
    for (std::size_t k = 0; k + 1 < source.size(); ++k)
    {
        source_level = &(*source_level)[source[k]].children;
    }
    TreeNode moved = std::move((*source_level)[source.back()]);
    source_level->erase(source_level->begin() + static_cast<std::ptrdiff_t>(source.back()));
    const std::size_t depth = source.size() - 1;
    if (target.size() > depth &&
        std::equal(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(depth), target.begin()) &&
        target[depth] > source.back())
    {
        --target[depth];
    }

    TreeNode*              target_node = node_at(target);
    std::vector<TreeNode>* level       = nullptr;
    std::size_t            index       = 0;
    TreeNode*              new_parent  = nullptr;
    if (*zone == DropZone::Into)
    {
        // Appended as the target's last child. A collapsed target stays
        // collapsed: only a second's hover opens it.
        level      = &target_node->children;
        index      = level->size();
        new_parent = target_node;
    }
    else if (*zone == DropZone::After && target_open)
    {
        // After an expanded node is its first child.
        level      = &target_node->children;
        index      = 0;
        new_parent = target_node;
    }
    else
    {
        std::vector<std::size_t> parent_path(target.begin(), target.end() - 1);
        new_parent = parent_path.empty() ? nullptr : node_at(parent_path);
        level      = new_parent != nullptr ? &new_parent->children : &roots_;
        index      = target.back() + (*zone == DropZone::After ? 1 : 0);
    }
    const std::vector<std::size_t> parent_path = [&]
    {
        // Find the parent again after the insert (its address may move with its siblings).
        std::vector<std::size_t> p;
        if (new_parent != nullptr)
        {
            const auto flat = flatten();
            for (const VisibleRow& r : flat)
            {
                if (r.node == new_parent)
                {
                    p = r.path;
                }
            }
        }
        return p;
    }();
    index = std::min(index, level->size());
    level->insert(level->begin() + static_cast<std::ptrdiff_t>(index), std::move(moved));
    structure_changed();
    if (selection_mode_ == TreeViewSelectionMode::Multiple)
    {
        sync_parents(roots_);
    }

    // Where it went, for on_drag_items_completed.
    std::vector<std::size_t> landed = parent_path;
    landed.push_back(index);
    dragging_ = landed;
}

void TreeViewControl::drag_completed(DataPackageOperation result)
{
    drop_row_.reset();
    drag_hover_row_  = detail::NoIndex;
    hover_expand_ms_ = 0.0;
    mark_dirty(DirtyFlags::Visual);
    if (dragging_ && result != DataPackageOperation::None && on_drag_items_completed_)
    {
        const std::vector<std::size_t> path = *dragging_;
        if (const TreeNode* node = node_at(path))
        {
            const std::vector<std::size_t> parent(path.begin(), path.end() - 1);
            on_drag_items_completed_(
                TreeViewDragItemsCompletedArgs{.node       = *node,
                                               .new_parent = parent.empty() ? nullptr : node_at(parent),
                                               .index      = path.back()});
        }
    }
    dragging_.reset();
}

// ---- render ----

void TreeViewControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    theme_             = &theme;
    renderer.push_clip(bounds());
    const bool multi = selection_mode_ == TreeViewSelectionMode::Multiple;
    const auto rows  = flatten();
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        const VisibleRow&            row       = rows[i];
        const Rect                   r         = row_rect(i);
        const TreeNodeSelectionState state_sel = multi ? selection_state(*row.node)
                                                       : (is_selected(*row.node) ? TreeNodeSelectionState::Selected
                                                                                 : TreeNodeSelectionState::Unselected);
        const bool                   selected  = state_sel == TreeNodeSelectionState::Selected;
        const bool        hovered = i == hovered_row_ || (drop_row_ && *drop_row_ == i && drop_zone_ == DropZone::Into);
        const bool        pressed = i == pressed_row_;
        const ButtonState state   = !is_enabled() ? ButtonState::Disabled
                                                  : (pressed ? ButtonState::Pressed
                                                             : (hovered ? ButtonState::Hover : ButtonState::Normal));

        Color text_color;
        if (detail::controls(theme).list_item_subtle_selection)
        {
            const Rect  chip = row_chip(r);
            const Color bg = detail::subtle_row_background(theme,
                                                           state == ButtonState::Disabled ? ButtonState::Normal : state,
                                                           selected);
            if (bg.a > 0.0f)
            {
                renderer.fill_rounded_rect(chip, theme.corner_radius, bg);
            }
            // The indicator belongs to Single selection (Multiple selects through the check boxes).
            if (selected && !multi && !detail::controls(theme).row_selection_fills)
            {
                draw_row_pill(renderer, chip, theme.colors().accent);
            }
            text_color = state == ButtonState::Pressed ? theme.palette.text_fill_secondary : theme.colors().text;
        }
        else if (selected)
        {
            renderer.fill_rounded_rect(r, 0.0f, theme.colors().selection);
            text_color = theme.colors().selection_text;
        }
        else
        {
            if (hovered)
            {
                renderer.fill_rounded_rect(r, 0.0f, theme.colors().control_fill_hover.with_alpha(TreeViewHoverOverlayAlpha));
            }
            text_color = theme.colors().text;
        }
        if (!is_enabled())
        {
            text_color = theme.colors().text_disabled;
        }

        if (multi)
        {
            const Rect                box{check_left(row.depth),
                                          r.y + (r.height - detail::ItemCheckBoxSize) * 0.5f,
                                          detail::ItemCheckBoxSize,
                                          detail::ItemCheckBoxSize};
            const std::optional<bool> checked = state_sel == TreeNodeSelectionState::Partial
                                                    ? std::nullopt
                                                    : std::optional<bool>{selected};
            detail::draw_item_check_box_tristate(renderer, theme, box, checked, state, TreeViewCheckGlyph);
        }
        if (row.has_children)
        {
            const float cw = multi ? ChevronGlyphBox : chevron_width_;
            const Rect  chevron_rect{chevron_left(row.depth), r.top(), cw, r.height};
            if (theme.disclosure_visual == ThemeDisclosureVisual::ClassicArrow)
            {
                // TVS_HASBUTTONS: a GRAYTEXT box on the field color with the sign in WINDOWTEXT,
                // drawn beside the row's highlight rather than in its colors.
                draw_classic_tree_button(renderer,
                                         chevron_rect,
                                         is_expanded(*row.node),
                                         theme.colors().text_disabled,
                                         theme.colors().field,
                                         is_enabled() ? theme.colors().field_text : theme.colors().text_disabled);
            }
            else
            {
                draw_icon(renderer,
                          is_expanded(*row.node) ? glyphs::TreeChevronExpanded : glyphs::TreeChevronCollapsed,
                          chevron_rect,
                          text_color,
                          0.0f,
                          theme.icon_font_family);
            }
        }

        const auto content = contents_.find(row.node);
        if (item_template_ && content != contents_.end() && content->second)
        {
            content->second->render(renderer, context);
        }
        else
        {
            const float left_gap  = detail::controls(theme).list_item_subtle_selection ? 0.0f : theme.padding.left;
            const float right_gap = detail::controls(theme).list_item_subtle_selection ? 0.0f : theme.padding.right;
            const float text_x    = content_left(row.depth) + left_gap;
            TextStyle   style     = theme.text_style;
            style.horizontal      = TextAlign::Leading;
            const Rect text_rect{text_x, r.top(), std::max(0.0f, r.right() - text_x - right_gap), r.height};
            renderer.draw_text(row.node->text, text_rect, text_color, style);
        }

        // Where a reorder would put the dragged node: an accent line along the row's edge, indented
        // to the level the node lands at (one deeper after an expanded row: its first child).
        if (drop_row_ && *drop_row_ == i && drop_zone_ != DropZone::Into)
        {
            const bool  first_child = drop_zone_ == DropZone::After && is_expanded(*row.node);
            const float y           = drop_zone_ == DropZone::Before ? r.top() : r.bottom();
            const float x           = content_left(row.depth + (first_child ? 1 : 0));
            renderer.fill_rounded_rect(Rect{x, y - DropLineWidth * 0.5f, std::max(0.0f, r.right() - x - 4.0f), DropLineWidth},
                                       1.0f,
                                       theme.colors().accent);
        }
    }
    renderer.pop_clip();
}

std::optional<std::size_t> TreeViewControl::ring_row() const
{
    const bool      multi = selection_mode_ == TreeViewSelectionMode::Multiple;
    const TreeNode* focus = multi || selection_mode_ == TreeViewSelectionMode::None ? current_ : selected();
    return row_of(focus);
}

Rect TreeViewControl::focus_ring_rect(const Theme& /*theme*/) const
{
    const std::optional<std::size_t> row = ring_row();
    return row ? row_rect(*row) : bounds();
}

float TreeViewControl::focus_ring_corner_radius(const Theme& theme) const
{
    return ring_row() ? theme.corner_radius : 0.0f;
}

bool TreeViewControl::focus_ring_inside() const
{
    return ring_row().has_value();
}

// ---- plumbing ----

void TreeViewControl::tick_override(Duration delta)
{
    // A drag resting on a collapsed node with children, not the dragged one, opens it after a
    // second.
    if (drag_hover_row_ != detail::NoIndex && dragging_)
    {
        hover_expand_ms_ += delta.count();
        if (hover_expand_ms_ >= HoverExpandMs)
        {
            hover_expand_ms_ = 0.0;
            const auto rows  = flatten();
            if (drag_hover_row_ < rows.size() && rows[drag_hover_row_].has_children &&
                !is_expanded(*rows[drag_hover_row_].node) && rows[drag_hover_row_].path != *dragging_)
            {
                set_node_expanded(rows[drag_hover_row_].node, true);
            }
        }
    }
    for (auto& [node, content] : contents_)
    {
        if (content)
        {
            content->tick(delta);
        }
    }
    if (item_menu_ && detail::TreeAccess::popup_request(*item_menu_) == nullptr) // listed as a child, and ticked, while it shows
    {
        item_menu_->tick(delta);
    }
}

void TreeViewControl::clear_dirty_override(DirtyFlags flags)
{
    if (item_menu_)
    {
        clear_dirty_of(*item_menu_, flags); // listed as a child only while it shows
    }
    for (auto& [node, content] : contents_)
    {
        if (content)
        {
            clear_dirty_of(*content, flags);
        }
    }
}

void TreeViewControl::automation_children(std::vector<Element*>& out) const
{
    for (const TreeNode& root : roots_)
    {
        out.push_back(item_automation(root));
    }
    if (item_menu_ && detail::TreeAccess::popup_request(*item_menu_) != nullptr)
    {
        out.push_back(item_menu_.get()); // the open node menu
    }
}

Element* TreeViewControl::automation_focus() const
{
    const bool      single = selection_mode_ == TreeViewSelectionMode::Single;
    const TreeNode* node   = single && selected() != nullptr ? selected() : current_;
    return node != nullptr && row_of(node) ? item_automation(*node) : nullptr;
}

Element* TreeViewControl::automation_parent_of(const Element& child) const
{
    for (const auto& [node, content] : contents_)
    {
        if (content.get() == &child)
        {
            return item_automation(*node);
        }
    }
    for (const auto& [key, item] : item_automation_)
    {
        if (item.get() == &child)
        {
            const TreeNode* node = find(key);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): find_parent only reads
            const TreeNode* parent = node != nullptr ? find_parent(const_cast<std::vector<TreeNode>&>(roots_), node) : nullptr;
            return parent != nullptr ? item_automation(*parent) : nullptr;
        }
    }
    return nullptr;
}

} // namespace ui
