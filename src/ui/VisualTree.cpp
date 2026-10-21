// SPDX-License-Identifier: MIT
#include "winui/ui/VisualTree.hpp"

#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/VisualId.hpp"

#include <cassert>
#include <cstddef>

namespace ui
{

void VisualTree::clear()
{
    id_.clear();
    parent_.clear();
    first_child_.clear();
    next_sibling_.clear();
    last_child_.clear();
    bounds_.clear();
    flags_.clear();
    slot_of_.clear();
}

std::size_t VisualTree::add_node(VisualId id, VisualId parent, const Rect& bounds, DirtyFlags flags_at_build)
{
    const std::size_t slot = id_.size();

    id_.push_back(id);
    parent_.push_back(parent);
    first_child_.push_back(InvalidVisualId);
    next_sibling_.push_back(InvalidVisualId);
    last_child_.push_back(InvalidVisualId);
    bounds_.push_back(bounds);
    flags_.push_back(flags_at_build);
    slot_of_[id] = slot;

    if (parent != InvalidVisualId)
    {
        const auto it = slot_of_.find(parent);
        assert(it != slot_of_.end() && "VisualTree::add_node: parent not yet in the tree");
        const std::size_t parent_slot = it->second;

        if (first_child_[parent_slot] == InvalidVisualId)
        {
            first_child_[parent_slot] = id;
        }
        else
        {
            const std::size_t last_slot = slot_of_.at(last_child_[parent_slot]);
            next_sibling_[last_slot]    = id;
        }
        last_child_[parent_slot] = id;
    }

    return slot;
}

bool VisualTree::find(VisualId id, std::size_t& out_slot) const
{
    const auto it = slot_of_.find(id);
    if (it == slot_of_.end())
    {
        return false;
    }
    out_slot = it->second;
    return true;
}

} // namespace ui
