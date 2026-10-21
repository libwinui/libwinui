// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/core/Scheduler.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/ParallelLayout.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <span>
#include <vector>

#include <cstddef>

namespace ui
{

// The parallel measuring machinery behind ParallelLayout (LayoutSnapshot.cpp): private, used by
// Window and the built-in panels.

// A read-only batch of independent elements plus everything needed to measure them, captured once
// so every worker of measure_parallel() sees the same inputs.
//
// The snapshot owns nothing: the elements, measurer and theme must outlive the measure_parallel()
// call, and the elements must not be mutated (or measured elsewhere) while it runs. The results are
// returned, not written back; committing them to the tree is the caller's job.
//
// Example:
// @code
// // In a container's measure_override(): many independent children, measured on worker threads.
// std::vector<ui::Element*> elements;
// for (const auto& child : children_)
// {
//     elements.push_back(child.get());
// }
//
// ui::Scheduler         scheduler{4}; // or ui::Scheduler::Inline{} to stay on this thread
// ui::LayoutSnapshot    snapshot{elements, {available.width, 0.0f}, &measurer, &theme};
// std::vector<ui::Size> sizes = ui::measure_parallel(snapshot, scheduler); // sizes[i] is elements[i]'s
// @endcode
struct LayoutSnapshot
{
    // The elements to measure (non-owning); results come back in this order.
    std::span<Element* const> elements;
    // The available size passed to every Element::measure(), DIPs.
    Size available;
    // The text measurer every element measures with; must be safe to use from several threads.
    const TextMeasurer* measurer;
    // The theme every element measures against.
    const Theme* theme;
};

// Measures every element in `snapshot`, split into up to scheduler.worker_count()
// contiguous chunks and dispatched as one job per chunk. Blocks until every chunk has
// completed, then returns the desired sizes in the same order as `snapshot.elements`. Each element
// is measured exactly as Element::measure() would sequentially; an empty batch returns an empty
// vector. With an Inline scheduler the chunks run on the calling thread, in order.
[[nodiscard]] std::vector<Size> measure_parallel(const LayoutSnapshot& snapshot, Scheduler& scheduler);

// The process-wide pool parallel layout uses unless ParallelLayout::scheduler names another one:
// one worker per hardware thread but one (the UI thread measures a share itself), at most 7.
// Created on first use and never destroyed, so its threads exist only in a process that had a
// batch large enough to use it.
[[nodiscard]] Scheduler& layout_scheduler();

// One child measure in a measure_batch(): the element, the size it is offered, and, after the
// call, the size it asked for.
struct MeasureRequest
{
    // The child to measure (non-owning).
    Element* element = nullptr;
    // Passed to Element::measure().
    Size available{};
    // What Element::measure() returned; written by measure_batch().
    Size desired{};
};

// Whether measure_batch() may use worker threads right now: true only on the thread running a
// Window::layout() measure with parallel layout on, and never on a worker. A container checks it
// before collecting a batch, so its sequential path costs nothing extra.
[[nodiscard]] bool parallel_measure_active();

// Measures every request, with the same results, bit for bit, as
// `r.desired = r.element->measure(r.available, measurer, theme)` for each in order. When
// parallel_measure_active() and the batch is large enough, the requests whose subtrees are all
// Element::parallel_measure_safe() are measured on worker threads, split by subtree size; the
// rest are measured on the calling thread first. A container uses it for children it measures
// independently of each other, never for a sibling whose offer depends on another's result.
//
// Example:
// @code
// ui::Size measure_override(ui::Size available, const ui::MeasureContext& context) const override
// {
//     ui::Size total{};
//     if (ui::parallel_measure_active())
//     {
//         std::vector<ui::MeasureRequest> batch;
//         for (const auto& child : children_)
//         {
//             batch.push_back({child.get(), {available.width, ui::Unbounded}});
//         }
//         ui::measure_batch(batch, context);
//         for (const ui::MeasureRequest& r : batch)
//         {
//             total = {std::max(total.width, r.desired.width), total.height + r.desired.height};
//         }
//         return total;
//     }
//     for (const auto& child : children_)
//     {
//         const ui::Size d = child->measure({available.width, ui::Unbounded}, context);
//         total            = {std::max(total.width, d.width), total.height + d.height};
//     }
//     return total;
// }
// @endcode
void measure_batch(std::span<MeasureRequest> batch, const MeasureContext& context);

} // namespace ui
