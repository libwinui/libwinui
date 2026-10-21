// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Duration.hpp"

#include <optional>

#include <cstdint>

namespace ui::testing
{

/// How much work the library has done so far in this process, counted as it works: layout passes,
/// measures, text layouts created, ticks and so on. Every count only grows, so a test compares two
/// snapshots, most easily through PerfCountScope. Counts don't flake the way times do, so a test
/// can check that hovering a button measures nothing, and fail when it does.
///
/// Example:
/// @code
/// ui::testing::HeadlessUI ui = ui::testing::create_headless();
/// ui.window().set_content(ui.button("Save"));
/// ui.layout({800, 600});
/// const ui::testing::PerfCountScope scope;
/// ui.layout({800, 600}); // nothing changed
/// REQUIRE(scope.elapsed().measures == 0);
/// @endcode
struct PerfCounters
{
    /// Window::layout() calls that measured and arranged the tree.
    std::uint64_t layout_passes = 0;
    /// Times a window rebuilt its tab ring.
    std::uint64_t focus_order_builds = 0;
    /// Times a window rebuilt its VisualTree snapshot.
    std::uint64_t visual_tree_builds = 0;
    /// Element::measure() calls that reached measure_override().
    std::uint64_t measures = 0;
    /// Element::arrange() calls that reached arrange_override().
    std::uint64_t arranges = 0;
    /// Element::render() calls that reached render_override().
    std::uint64_t renders = 0;
    /// Calls into a TextMeasurer, the headless one or the renderer's.
    std::uint64_t text_measurements = 0;
    /// Text formats the renderer created.
    std::uint64_t text_formats_created = 0;
    /// Text layouts the renderer created.
    std::uint64_t text_layouts_created = 0;
    /// Brushes the renderer created.
    std::uint64_t brushes_created = 0;
    /// Geometries the renderer created.
    std::uint64_t geometries_created = 0;
    /// Stroke styles the renderer created.
    std::uint64_t stroke_styles_created = 0;
    /// Bitmaps the renderer created.
    std::uint64_t bitmaps_created = 0;
    /// Window::render() calls.
    std::uint64_t frames = 0;
    /// Window::tick() calls.
    std::uint64_t ticks = 0;
    /// Element::tick() calls, one per element ticked.
    std::uint64_t element_ticks = 0;
    /// Times a window host woke up for a timer.
    std::uint64_t timer_wakeups = 0;

    /// The counts in `a` less those in `b`.
    friend PerfCounters operator-(const PerfCounters& a, const PerfCounters& b);
};

/// The counts since the process started.
[[nodiscard]] PerfCounters perf_counters();

/// How long the process took to reach each point on the way to its first frame, counted from
/// process creation. Unset for a point not reached yet, and on a platform the library has no
/// backend for.
struct StartupMilestones
{
    /// The first native window was created.
    std::optional<Duration> first_window{};
    /// The first frame was presented.
    std::optional<Duration> first_frame{};
    /// The message loop ran out of messages for the first time: the app is up and waiting.
    std::optional<Duration> first_idle{};
};

/// The startup milestones reached so far.
[[nodiscard]] StartupMilestones startup_milestones();

/// The process's private bytes (memory committed for it alone), or 0 where the platform doesn't
/// report them.
[[nodiscard]] std::uint64_t private_bytes();

/// Takes a snapshot when it is made; elapsed() is the work done since.
class PerfCountScope
{
public:
    PerfCountScope() : start_(perf_counters())
    {
    }

    /// The counts since this scope was made (or last reset()).
    [[nodiscard]] PerfCounters elapsed() const
    {
        return perf_counters() - start_;
    }

    /// Starts counting again from now.
    void reset()
    {
        start_ = perf_counters();
    }

private:
    PerfCounters start_;
};

} // namespace ui::testing
