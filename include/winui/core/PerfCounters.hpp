// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace ui::detail
{

// What the library counts as it works, for the testing tier's perf_counters()
// (winui/testing/PerfCounters.hpp). Every counter is process-wide and only ever grows; a test
// takes the difference between two snapshots. Counting is a relaxed atomic add, so layout
// workers count their measures too.
enum class PerfCounter : std::uint8_t
{
    LayoutPasses,       // Window::layout() calls that measured and arranged the tree
    FocusOrderBuilds,   // Window rebuilt its tab ring
    VisualTreeBuilds,   // Window rebuilt its visual tree snapshot
    Measures,           // Element::measure() calls that reached measure_override()
    Arranges,           // Element::arrange() calls that reached arrange_override()
    Renders,            // Element::render() calls that reached render_override()
    TextMeasurements,   // TextMeasurer::measure() and measure_wrapped() calls
    TextFormatsCreated, // renderer text formats created
    TextLayoutsCreated, // renderer text layouts created
    BrushesCreated,     // renderer brushes created
    GeometriesCreated,  // renderer geometries created
    StrokeStylesCreated,
    BitmapsCreated, // renderer bitmaps created
    Frames,         // Window::render() calls
    Ticks,          // Window::tick() calls
    ElementTicks,   // Element::tick() calls
    TimerWakeups,   // timer wakeups a window host received
    Count
};

// Adds `n` to `counter`.
void count(PerfCounter counter, std::uint64_t n = 1) noexcept;

// The current value of `counter`.
[[nodiscard]] std::uint64_t counter_value(PerfCounter counter) noexcept;

// Points on the way to an app's first frame, each noted once, the first time it is reached.
enum class Milestone : std::uint8_t
{
    FirstWindow, // the first native window exists
    FirstFrame,  // the first frame was presented
    FirstIdle,   // the message loop ran out of messages for the first time
    Count
};

// Notes `milestone` now, unless it was noted before.
void note_milestone(Milestone milestone) noexcept;

// When `milestone` was noted, in std::chrono::system_clock ticks since its epoch; 0 if never.
[[nodiscard]] std::int64_t milestone_time(Milestone milestone) noexcept;

} // namespace ui::detail
