// SPDX-License-Identifier: MIT
#include "winui/core/PerfCounters.hpp"

#include <array>
#include <atomic>
#include <chrono>

#include <cstddef>

namespace ui::detail
{

namespace
{
constinit std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(PerfCounter::Count)> counters{};
constinit std::array<std::atomic<std::int64_t>, static_cast<std::size_t>(Milestone::Count)>    milestones{};
} // namespace

void count(PerfCounter counter, std::uint64_t n) noexcept
{
    counters[static_cast<std::size_t>(counter)].fetch_add(n, std::memory_order_relaxed);
}

std::uint64_t counter_value(PerfCounter counter) noexcept
{
    return counters[static_cast<std::size_t>(counter)].load(std::memory_order_relaxed);
}

void note_milestone(Milestone milestone) noexcept
{
    std::int64_t unset = 0;
    const auto   now   = static_cast<std::int64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    (void)milestones[static_cast<std::size_t>(milestone)].compare_exchange_strong(unset, now, std::memory_order_relaxed);
}

std::int64_t milestone_time(Milestone milestone) noexcept
{
    return milestones[static_cast<std::size_t>(milestone)].load(std::memory_order_relaxed);
}

} // namespace ui::detail
