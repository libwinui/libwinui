// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Scheduler.hpp"

#include <cstddef>

namespace ui
{

/// How Window::layout() may spread measuring over worker threads (Window::set_parallel_layout()).
///
/// Only a batch of siblings whose whole subtrees answer Element::parallel_measure_safe() goes to
/// the workers, and only when it is large enough to pay for the hand-off. Everything else is
/// measured on the UI thread. The sizes are the same, bit for bit, either way.
///
/// Example:
/// @code
/// ui::Window window{{.title = "Report"}};
/// window.set_parallel_layout({.enabled = false}); // everything on the UI thread
/// @endcode
struct ParallelLayout
{
    /// Whether to measure on worker threads at all. On by default. Below `min_elements` nothing
    /// changes, and above it a form with real text measurement lays out about three times faster
    /// (the docs' layout page has the numbers).
    bool enabled = true;
    /// The smallest batch worth sending to workers, counted in elements across the siblings'
    /// subtrees. A smaller one is measured on the calling thread.
    std::size_t min_elements = 256;
    /// The pool to measure on, borrowed. It must outlive every layout pass that uses it. nullptr
    /// uses the process-wide layout pool, created on first use, with one worker fewer than there
    /// are hardware threads and at most 7, since the UI thread measures a share itself. A
    /// `Scheduler{Scheduler::Inline{}}` measures everything on the calling thread, through the
    /// same batching code.
    Scheduler* scheduler = nullptr;
};

} // namespace ui
