// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <optional>
#include <vector>

#include <cstddef>

namespace ui::detail
{

class ItemSelection
{
public:
    void reset(std::size_t count)
    {
        bits_.assign(count, false);
        anchor_.reset();
    }
    [[nodiscard]] std::size_t size() const
    {
        return bits_.size();
    }
    [[nodiscard]] bool is_selected(std::size_t index) const
    {
        return index < bits_.size() && bits_[index];
    }
    /// Returns whether anything changed.
    bool set(std::size_t index, bool selected)
    {
        if (index >= bits_.size() || bits_[index] == selected)
        {
            return false;
        }
        bits_[index] = selected;
        return true;
    }
    bool clear()
    {
        bool changed = false;
        for (std::size_t i = 0; i < bits_.size(); ++i)
        {
            changed = set(i, false) || changed;
        }
        return changed;
    }
    bool select_all()
    {
        bool changed = false;
        for (std::size_t i = 0; i < bits_.size(); ++i)
        {
            changed = set(i, true) || changed;
        }
        return changed;
    }
    bool invert()
    {
        for (std::size_t i = 0; i < bits_.size(); ++i)
        {
            bits_[i] = !bits_[i];
        }
        return !bits_.empty();
    }
    /// Select (or deselect) every item from `a` to `b`, either order.
    bool set_range(std::size_t a, std::size_t b, bool selected)
    {
        bool changed = false;
        for (std::size_t i = std::min(a, b); i <= std::max(a, b) && i < bits_.size(); ++i)
        {
            changed = set(i, selected) || changed;
        }
        return changed;
    }
    [[nodiscard]] bool all_selected() const
    {
        return !bits_.empty() && std::all_of(bits_.begin(), bits_.end(), [](bool b) { return b; });
    }
    [[nodiscard]] std::size_t selected_count() const
    {
        return static_cast<std::size_t>(std::count(bits_.begin(), bits_.end(), true));
    }
    [[nodiscard]] std::vector<std::size_t> selected() const
    {
        std::vector<std::size_t> out;
        for (std::size_t i = 0; i < bits_.size(); ++i)
        {
            if (bits_[i])
            {
                out.push_back(i);
            }
        }
        return out;
    }
    [[nodiscard]] std::optional<std::size_t> first_selected() const
    {
        for (std::size_t i = 0; i < bits_.size(); ++i)
        {
            if (bits_[i])
            {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] const std::optional<std::size_t>& anchor() const
    {
        return anchor_;
    }
    void set_anchor(std::optional<std::size_t> anchor)
    {
        anchor_ = anchor;
    }

    void on_inserted(std::size_t index, std::size_t count)
    {
        index = std::min(index, bits_.size());
        bits_.insert(bits_.begin() + static_cast<std::ptrdiff_t>(index), count, false);
        if (anchor_ && *anchor_ >= index)
        {
            *anchor_ += count;
        }
    }
    /// Returns whether a selected item went.
    bool on_removed(std::size_t index, std::size_t count)
    {
        if (index >= bits_.size())
        {
            return false;
        }
        const std::size_t end     = std::min(bits_.size(), index + count);
        const bool        changed = std::any_of(bits_.begin() + static_cast<std::ptrdiff_t>(index),
                                                bits_.begin() + static_cast<std::ptrdiff_t>(end),
                                                [](bool b) { return b; });
        bits_.erase(bits_.begin() + static_cast<std::ptrdiff_t>(index), bits_.begin() + static_cast<std::ptrdiff_t>(end));
        if (anchor_)
        {
            if (*anchor_ >= end)
            {
                *anchor_ -= end - index;
            }
            else if (*anchor_ >= index)
            {
                anchor_.reset();
            }
        }
        return changed;
    }
    /// One item moved from `from` to `to`; its selection goes with it.
    void on_moved(std::size_t from, std::size_t to)
    {
        if (from >= bits_.size() || to >= bits_.size())
        {
            return;
        }
        const bool selected = bits_[from];
        bits_.erase(bits_.begin() + static_cast<std::ptrdiff_t>(from));
        bits_.insert(bits_.begin() + static_cast<std::ptrdiff_t>(to), selected);
        if (anchor_ && *anchor_ == from)
        {
            anchor_ = to;
        }
    }

private:
    std::vector<bool>          bits_;
    std::optional<std::size_t> anchor_;
};

} // namespace ui::detail
