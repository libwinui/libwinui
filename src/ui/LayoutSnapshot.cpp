// SPDX-License-Identifier: MIT
#include "LayoutSnapshot.hpp"

#include "AnimationsScope.hpp"
#include "ParallelMeasure.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Scheduler.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <span>
#include <thread>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ui
{

std::vector<Size> measure_parallel(const LayoutSnapshot& snapshot, Scheduler& scheduler)
{
    const std::size_t total = snapshot.elements.size();
    std::vector<Size> results(total);
    if (total == 0)
    {
        return results;
    }

    const std::size_t chunks     = std::min(scheduler.worker_count(), total);
    const std::size_t chunk_size = (total + chunks - 1) / chunks;

    std::vector<detail::Job> tasks;
    tasks.reserve(chunks);
    for (std::size_t start = 0; start < total; start += chunk_size)
    {
        const std::size_t end = std::min(start + chunk_size, total);
        // Each job writes only its own [start, end) slice of `results`, so nothing needs locking
        // beyond the final wait().
        tasks.push_back(detail::submit(scheduler,
                                       [&snapshot, &results, start, end]
                                       {
                                           for (std::size_t i = start; i < end; ++i)
                                           {
                                               results[i] = snapshot.elements[i]->measure(snapshot.available,
                                                                                          *snapshot.measurer,
                                                                                          *snapshot.theme);
                                           }
                                       }));
    }
    for (detail::Job& task : tasks)
    {
        task.wait();
    }
    return results;
}

Scheduler& layout_scheduler()
{
    // Leaked on purpose: joining workers from a static destructor runs after main() (under the
    // loader lock in a DLL build), and an idle pool has nothing to flush.
    static Scheduler* const pool = []
    {
        const std::size_t hardware = std::thread::hardware_concurrency();
        const std::size_t workers  = std::clamp<std::size_t>(hardware > 1 ? hardware - 1 : 1, 1, 7);
        return new Scheduler{workers}; // NOLINT(cppcoreguidelines-owning-memory)
    }();
    return *pool;
}

namespace
{

thread_local detail::ParallelMeasureState* current_state = nullptr;

constexpr std::uint32_t Unsafe = 0;

// The number of elements in `element`'s subtree, or Unsafe when any of them may not be measured
// on a worker. A collapsed element measures nothing below it, so it is one safe element.
std::uint32_t subtree_cost(const Element& element, detail::ParallelMeasureState& state)
{
    if (element.visibility() == Visibility::Collapsed)
    {
        return 1;
    }
    if (!element.parallel_measure_safe())
    {
        return Unsafe;
    }
    if (const auto it = state.costs.find(&element); it != state.costs.end())
    {
        return it->second;
    }
    std::uint32_t cost         = 1;
    bool          has_children = false;
    element.for_each_child(
        [&](Element& child)
        {
            has_children = true;
            if (cost == Unsafe)
            {
                return;
            }
            const std::uint32_t c = subtree_cost(child, state);
            cost                  = c == Unsafe ? Unsafe : cost + c;
        });
    if (has_children)
    {
        state.costs.emplace(&element, cost);
    }
    return cost;
}

void measure_in_order(std::span<MeasureRequest> batch, const MeasureContext& context)
{
    for (MeasureRequest& r : batch)
    {
        r.desired = r.element->measure(r.available, context);
    }
}

// Clears this thread's state while it measures a share of a batch itself, so a container inside
// that share measures sequentially, as on the workers.
class SuspendParallel
{
public:
    SuspendParallel() : saved_(current_state)
    {
        current_state = nullptr;
    }
    ~SuspendParallel()
    {
        current_state = saved_;
    }
    SuspendParallel(const SuspendParallel&)            = delete;
    SuspendParallel& operator=(const SuspendParallel&) = delete;

private:
    detail::ParallelMeasureState* saved_;
};

} // namespace

bool parallel_measure_active()
{
    return current_state != nullptr;
}

void measure_batch(std::span<MeasureRequest> batch, const MeasureContext& context)
{
    detail::ParallelMeasureState* const state = current_state;
    if (state == nullptr || batch.size() < 2)
    {
        measure_in_order(batch, context);
        return;
    }

    std::vector<std::uint32_t> costs(batch.size());
    std::size_t                total   = 0;
    std::size_t                largest = 0;
    std::size_t                safe    = 0;
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
        costs[i] = subtree_cost(*batch[i].element, *state);
        if (costs[i] != Unsafe)
        {
            total += costs[i];
            largest = std::max<std::size_t>(largest, costs[i]);
            ++safe;
        }
    }
    if (safe < 2 || total < state->min_elements)
    {
        measure_in_order(batch, context);
        return;
    }

    if (state->scheduler == nullptr)
    {
        state->scheduler = state->requested != nullptr ? state->requested : &layout_scheduler();
    }
    Scheduler&        scheduler = *state->scheduler;
    const std::size_t threads   = scheduler.thread_count() + 1; // the workers, and this thread
    // Few siblings, one of which is a large batch by itself: splitting here would leave threads
    // idle, while measuring in order lets that child split its own, wider, set of children.
    if (safe < threads * 2 && largest >= state->min_elements)
    {
        measure_in_order(batch, context);
        return;
    }

    // What may not leave this thread goes first, in order, before any worker starts. A safe
    // element's measure reaches nothing outside its subtree; an unsafe one that changes a safe
    // sibling (a binding it fires) has that sibling measured with the change, as the pass its
    // invalidation asks for would.
    std::vector<std::size_t> parallel;
    parallel.reserve(safe);
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
        if (costs[i] == Unsafe)
        {
            MeasureRequest& r = batch[i];
            r.desired         = r.element->measure(r.available, context);
        }
        else
        {
            parallel.push_back(i);
        }
    }

    // Contiguous chunks of about equal subtree size, one per thread.
    const std::size_t        chunks = std::min(threads, parallel.size());
    std::vector<std::size_t> bounds{0};
    bounds.reserve(chunks + 1);
    std::size_t running = 0;
    for (std::size_t k = 0; k < parallel.size() && bounds.size() < chunks; ++k)
    {
        running += costs[parallel[k]];
        if (running * chunks >= total * bounds.size())
        {
            bounds.push_back(k + 1);
        }
    }
    bounds.push_back(parallel.size());

    const bool                      animations = animations_enabled();
    std::vector<std::exception_ptr> failures(bounds.size() - 1);
    const auto                      run_chunk = [&](std::size_t chunk)
    {
        try
        {
            const detail::AnimationsScope scope{animations};
            for (std::size_t k = bounds[chunk]; k < bounds[chunk + 1]; ++k)
            {
                MeasureRequest& r = batch[parallel[k]];
                r.desired         = r.element->measure(r.available, context);
            }
        } catch (...)
        {
            failures[chunk] = std::current_exception();
        }
    };

    std::vector<detail::Job> tasks;
    tasks.reserve(bounds.size() - 2);
    for (std::size_t chunk = 1; chunk + 1 < bounds.size(); ++chunk)
    {
        tasks.push_back(detail::submit(scheduler, [&run_chunk, chunk] { run_chunk(chunk); }));
    }
    {
        const SuspendParallel suspend;
        run_chunk(0);
    }
    for (detail::Job& task : tasks)
    {
        task.wait();
    }
    for (const std::exception_ptr& failure : failures)
    {
        if (failure)
        {
            std::rethrow_exception(failure);
        }
    }
}

namespace detail
{

ParallelMeasureScope::ParallelMeasureScope(const ParallelLayout& options) : previous_(current_state)
{
    if (!options.enabled)
    {
        current_state = nullptr;
        return;
    }
    state_.emplace();
    state_->requested    = options.scheduler;
    state_->min_elements = std::max<std::size_t>(options.min_elements, 1);
    current_state        = &*state_;
}

ParallelMeasureScope::~ParallelMeasureScope()
{
    current_state = previous_;
}

} // namespace detail

} // namespace ui
