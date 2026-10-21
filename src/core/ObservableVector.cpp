// SPDX-License-Identifier: MIT
#include "winui/core/ObservableVector.hpp"

#include <algorithm>
#include <functional>
#include <span>
#include <vector>

#include <cstddef>

namespace ui::detail
{

void keyed_changes(std::size_t                             old_size,
                   std::span<const std::size_t>            source,
                   const std::vector<bool>&                changed,
                   Callback<void(const CollectionChange&)> sink)
{
    constexpr std::size_t npos = static_cast<std::size_t>(-1);
    // Each move searches the working order once; beyond this many steps a reset is cheaper.
    constexpr std::size_t MoveBudget = 20'000'000;

    // Which old items are still there, and where each survivor sits among the survivors.
    std::vector<bool> survives(old_size, false);
    for (const std::size_t old_index : source)
    {
        if (old_index != npos)
        {
            survives[old_index] = true;
        }
    }
    std::vector<std::size_t> rank(old_size, npos);
    std::size_t              survivors = 0;
    for (std::size_t i = 0; i < old_size; ++i)
    {
        if (survives[i])
        {
            rank[i] = survivors++;
        }
    }

    // The survivors in their new order, each named by its place in the old order.
    std::vector<std::size_t> wanted;
    wanted.reserve(survivors);
    for (const std::size_t old_index : source)
    {
        if (old_index != npos)
        {
            wanted.push_back(rank[old_index]);
        }
    }

    // The longest run of survivors already in relative order stays put; every other one moves once.
    std::vector<bool> stays(survivors, false);
    {
        std::vector<std::size_t> tails; // per length, the position in `wanted` ending the best run
        std::vector<std::size_t> previous(survivors, npos);
        for (std::size_t i = 0; i < survivors; ++i)
        {
            const auto at = std::lower_bound(tails.begin(),
                                             tails.end(),
                                             wanted[i],
                                             [&wanted](std::size_t tail, std::size_t value)
                                             { return wanted[tail] < value; });
            if (at != tails.begin())
            {
                previous[i] = *(at - 1);
            }
            if (at == tails.end())
            {
                tails.push_back(i);
            }
            else
            {
                *at = i;
            }
        }
        for (std::size_t i = tails.empty() ? npos : tails.back(); i != npos; i = previous[i])
        {
            stays[i] = true;
        }
    }
    const std::size_t moves = survivors - static_cast<std::size_t>(std::count(stays.begin(), stays.end(), true));
    if (moves != 0 && moves > MoveBudget / survivors)
    {
        sink({CollectionChangeKind::Reset, 0, 0, 0, source.size()});
        return;
    }

    // Removals, from the end so that the positions before them hold.
    std::size_t size = old_size;
    for (std::size_t i = old_size; i > 0;)
    {
        if (survives[i - 1])
        {
            --i;
            continue;
        }
        const std::size_t end = i;
        while (i > 0 && !survives[i - 1])
        {
            --i;
        }
        size -= end - i;
        sink({CollectionChangeKind::Removed, i, end - i, 0, size});
    }

    // Moves, from the end: each row that does not stay goes in front of its successor, which is
    // already in place. `order` is the rows as the observer has them now, named by their target slot.
    if (moves != 0)
    {
        std::vector<std::size_t> order(survivors);
        for (std::size_t slot = 0; slot < survivors; ++slot)
        {
            order[wanted[slot]] = slot;
        }
        for (std::size_t slot = survivors; slot-- > 0;)
        {
            if (stays[slot])
            {
                continue;
            }
            const auto        here = std::find(order.begin(), order.end(), slot);
            const std::size_t from = static_cast<std::size_t>(here - order.begin());
            order.erase(here);
            const auto next      = slot + 1 < survivors ? std::find(order.begin(), order.end(), slot + 1) : order.end();
            const std::size_t to = static_cast<std::size_t>(next - order.begin());
            order.insert(next, slot);
            if (from != to)
            {
                sink({CollectionChangeKind::Moved, from, 1, to, size});
            }
        }
    }

    // Insertions and rebuilt rows, in ascending order: the positions are final ones.
    for (std::size_t i = 0; i < source.size();)
    {
        if (source[i] != npos)
        {
            ++i;
            continue;
        }
        const std::size_t first = i;
        while (i < source.size() && source[i] == npos)
        {
            ++i;
        }
        size += i - first;
        sink({CollectionChangeKind::Inserted, first, i - first, 0, size});
    }
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        if (source[i] != npos && changed[i])
        {
            sink({CollectionChangeKind::Replaced, i, 1, 0, size});
        }
    }
}

} // namespace ui::detail
