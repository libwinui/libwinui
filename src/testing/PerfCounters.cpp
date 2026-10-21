// SPDX-License-Identifier: MIT
#include "winui/testing/PerfCounters.hpp"

#include "winui/core/PerfCounters.hpp"

#include <chrono>

#ifdef _WIN32
#include <psapi.h>
#include <windows.h>
#endif

namespace ui::testing
{

namespace
{
std::uint64_t value(detail::PerfCounter counter)
{
    return detail::counter_value(counter);
}
} // namespace

PerfCounters operator-(const PerfCounters& a, const PerfCounters& b)
{
    return {
        .layout_passes         = a.layout_passes - b.layout_passes,
        .focus_order_builds    = a.focus_order_builds - b.focus_order_builds,
        .visual_tree_builds    = a.visual_tree_builds - b.visual_tree_builds,
        .measures              = a.measures - b.measures,
        .arranges              = a.arranges - b.arranges,
        .renders               = a.renders - b.renders,
        .text_measurements     = a.text_measurements - b.text_measurements,
        .text_formats_created  = a.text_formats_created - b.text_formats_created,
        .text_layouts_created  = a.text_layouts_created - b.text_layouts_created,
        .brushes_created       = a.brushes_created - b.brushes_created,
        .geometries_created    = a.geometries_created - b.geometries_created,
        .stroke_styles_created = a.stroke_styles_created - b.stroke_styles_created,
        .bitmaps_created       = a.bitmaps_created - b.bitmaps_created,
        .frames                = a.frames - b.frames,
        .ticks                 = a.ticks - b.ticks,
        .element_ticks         = a.element_ticks - b.element_ticks,
        .timer_wakeups         = a.timer_wakeups - b.timer_wakeups,
    };
}

PerfCounters perf_counters()
{
    using detail::PerfCounter;
    return {
        .layout_passes         = value(PerfCounter::LayoutPasses),
        .focus_order_builds    = value(PerfCounter::FocusOrderBuilds),
        .visual_tree_builds    = value(PerfCounter::VisualTreeBuilds),
        .measures              = value(PerfCounter::Measures),
        .arranges              = value(PerfCounter::Arranges),
        .renders               = value(PerfCounter::Renders),
        .text_measurements     = value(PerfCounter::TextMeasurements),
        .text_formats_created  = value(PerfCounter::TextFormatsCreated),
        .text_layouts_created  = value(PerfCounter::TextLayoutsCreated),
        .brushes_created       = value(PerfCounter::BrushesCreated),
        .geometries_created    = value(PerfCounter::GeometriesCreated),
        .stroke_styles_created = value(PerfCounter::StrokeStylesCreated),
        .bitmaps_created       = value(PerfCounter::BitmapsCreated),
        .frames                = value(PerfCounter::Frames),
        .ticks                 = value(PerfCounter::Ticks),
        .element_ticks         = value(PerfCounter::ElementTicks),
        .timer_wakeups         = value(PerfCounter::TimerWakeups),
    };
}

StartupMilestones startup_milestones()
{
#ifdef _WIN32
    FILETIME created{};
    FILETIME unused{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &unused, &unused, &unused))
    {
        return {};
    }
    // FILETIME counts 100 ns from 1601, system_clock from 1970.
    constexpr std::int64_t Epoch1970 = 116'444'736'000'000'000;
    const std::int64_t     filetime = (static_cast<std::int64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    const auto start = std::chrono::system_clock::time_point{std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>{filetime - Epoch1970})};
    const auto since_start = [&](detail::Milestone milestone) -> std::optional<Duration>
    {
        const std::int64_t at = detail::milestone_time(milestone);
        if (at == 0)
        {
            return std::nullopt;
        }
        const std::chrono::system_clock::time_point when{std::chrono::system_clock::duration{at}};
        return std::chrono::duration_cast<Duration>(when - start);
    };
    return {
        .first_window = since_start(detail::Milestone::FirstWindow),
        .first_frame  = since_start(detail::Milestone::FirstFrame),
        .first_idle   = since_start(detail::Milestone::FirstIdle),
    };
#else
    return {};
#endif
}

std::uint64_t private_bytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
    {
        return counters.PrivateUsage;
    }
#endif
    return 0;
}

} // namespace ui::testing
