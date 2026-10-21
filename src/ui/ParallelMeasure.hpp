// SPDX-License-Identifier: MIT
#pragma once

#include "LayoutSnapshot.hpp"
#include "winui/ui/Element.hpp"

#include <optional>
#include <unordered_map>

#include <cstddef>
#include <cstdint>

namespace ui::detail
{

// What measure_batch() needs while a parallel layout pass runs on this thread.
struct ParallelMeasureState
{
    Scheduler*  scheduler    = nullptr; // nullptr until a batch needs it: then options' or layout_scheduler()
    Scheduler*  requested    = nullptr; // ParallelLayout::scheduler
    std::size_t min_elements = 0;
    // Subtree sizes of the containers seen this pass (0: not parallel-safe). Leaves are not stored.
    std::unordered_map<const Element*, std::uint32_t> costs;
};

// Turns measure_batch() parallel for the measure() calls this thread makes while it lives, as
// `options` allows. Window::layout() opens one around its content's measure. Off options make it
// a no-op; the pool is only touched once a batch qualifies.
class ParallelMeasureScope
{
public:
    explicit ParallelMeasureScope(const ParallelLayout& options);
    ~ParallelMeasureScope();
    ParallelMeasureScope(const ParallelMeasureScope&)            = delete;
    ParallelMeasureScope& operator=(const ParallelMeasureScope&) = delete;

private:
    std::optional<ParallelMeasureState> state_; // engaged while on
    ParallelMeasureState*               previous_ = nullptr;
};

} // namespace ui::detail
