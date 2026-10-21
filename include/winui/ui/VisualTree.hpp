// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/VisualId.hpp"

#include <unordered_map>
#include <vector>

#include <cstddef>

namespace ui
{

/// A flat, data-oriented snapshot of the laid-out tree's shape: one slot per Element, with
/// parent / first-child / next-sibling links by stable VisualId, bounds and dirty flags held in
/// parallel arrays.
///
/// Window::layout() clears and rebuilds it on every pass that re-lays out, through
/// Element::build_visual_tree(), and hands it out read-only via Window::visual_tree(). It is an
/// inspection surface (tests, tooling), not a way to drive controls: changes go through the
/// Element tree. Slots are indices in pre-order and are only valid until the next rebuild; a
/// VisualId stays the same across rebuilds.
///
/// Example:
/// @code
/// // Every element of a laid-out window, flat, for a tool that inspects the tree:
/// const ui::VisualTree& tree = window.visual_tree();
/// for (std::size_t slot = 0; slot < tree.size(); ++slot)
/// {
///     const ui::Rect& bounds = tree.bounds_at(slot);
///     std::println("{} under {}: {}x{}", tree.id_at(slot), tree.parent_at(slot), bounds.width, bounds.height);
/// }
///
/// // And one element by its id, here a button built with `.control = save_button`:
/// ui::Control<ui::Button> save_button;
/// std::size_t             slot = 0;
/// if (save_button && tree.find(save_button->visual_id(), slot))
/// {
///     std::println("Save is at {}, {}", tree.bounds_at(slot).x, tree.bounds_at(slot).y);
/// }
/// @endcode
class VisualTree
{
public:
    /// Drops every node; the tree is empty (size() == 0) afterwards.
    void clear();

    /// Append a node under `parent` (InvalidVisualId for the root) and return its slot.
    /// `id` must be unique within the tree and, if `parent` is not InvalidVisualId, `parent`
    /// must already be in the tree (asserted). Nodes are expected in pre-order (parent before
    /// children), which is how Element::build_visual_tree walks the logical tree; a new child
    /// becomes the last in its parent's sibling chain. `bounds` is in window DIPs and
    /// `flags_at_build` records what was still dirty when the snapshot was taken.
    std::size_t add_node(VisualId id, VisualId parent, const Rect& bounds, DirtyFlags flags_at_build = DirtyFlags::None);

    /// The number of nodes; valid slots are `0 .. size() - 1`, in insertion (pre-)order.
    [[nodiscard]] std::size_t size() const
    {
        return id_.size();
    }

    /// The stable VisualId of the node in `slot`. The `*_at()` accessors do not bounds-check.
    [[nodiscard]] VisualId id_at(std::size_t slot) const
    {
        return id_[slot];
    }
    /// The parent's id, or InvalidVisualId for the root.
    [[nodiscard]] VisualId parent_at(std::size_t slot) const
    {
        return parent_[slot];
    }
    /// The first child's id, or InvalidVisualId for a leaf.
    [[nodiscard]] VisualId first_child_at(std::size_t slot) const
    {
        return first_child_[slot];
    }
    /// The next child of the same parent, in add order, or InvalidVisualId for the last one.
    [[nodiscard]] VisualId next_sibling_at(std::size_t slot) const
    {
        return next_sibling_[slot];
    }
    /// The node's arranged bounds at snapshot time, in window DIPs.
    [[nodiscard]] const Rect& bounds_at(std::size_t slot) const
    {
        return bounds_[slot];
    }
    /// The node's dirty flags at snapshot time (Window::layout() builds before clearing them).
    [[nodiscard]] DirtyFlags flags_at(std::size_t slot) const
    {
        return flags_[slot];
    }

    /// Look up a node's slot by its stable id in O(1). Returns false, leaving `out_slot`
    /// untouched, when no node has that id.
    [[nodiscard]] bool find(VisualId id, std::size_t& out_slot) const;

private:
    std::vector<VisualId>   id_;
    std::vector<VisualId>   parent_;
    std::vector<VisualId>   first_child_;
    std::vector<VisualId>   next_sibling_;
    std::vector<VisualId>   last_child_; // bookkeeping only, to append siblings in O(1)
    std::vector<Rect>       bounds_;
    std::vector<DirtyFlags> flags_;

    std::unordered_map<VisualId, std::size_t> slot_of_;
};

} // namespace ui
