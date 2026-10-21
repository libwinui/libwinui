// SPDX-License-Identifier: MIT
// winui_idle: what each gallery page costs while it sits on screen untouched. It opens a window
// like the gallery's, shows the pages one after another, waits out the host's idle grace, then
// measures for a few seconds: timer wake-ups, ticks, frames and process CPU time, one line of JSON
// per page. A page that keeps drawing frames has an animation that never stops.
//
//     winui_idle [settle seconds] [measure seconds] [page name filter]
//
// The first line is the window with no page, the baseline.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "gallery.hpp"

#include <winui/testing.hpp>
#include <winui/winui.hpp>

#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <windows.h>

#include <cstdio>

namespace
{

using Clock = std::chrono::steady_clock;

double cpu_ms()
{
    FILETIME created{}, exited{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    const auto value = [](const FILETIME& t)
    { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return static_cast<double>(value(kernel) + value(user)) / 10'000.0;
}

// CPU cycles the whole process has used: exact, where GetProcessTimes() moves in 15.6 ms steps.
unsigned long long cpu_cycles()
{
    ULONG64 cycles = 0;
    QueryProcessCycleTime(GetCurrentProcess(), &cycles);
    return cycles;
}

struct Sample
{
    ui::testing::PerfCounters counts{};
    double                    cpu    = 0.0;
    unsigned long long        cycles = 0;
    Clock::time_point         time{};

    static Sample now()
    {
        return {ui::testing::perf_counters(), cpu_ms(), cpu_cycles(), Clock::now()};
    }
};

void report(std::string_view group, std::string_view page, const Sample& from, const Sample& to)
{
    const double                    seconds = std::chrono::duration<double>(to.time - from.time).count();
    const ui::testing::PerfCounters counts  = to.counts - from.counts;
    const double                    cpu     = to.cpu - from.cpu;
    std::printf("%s\n",
                std::format("{{\"group\": \"{}\", \"page\": \"{}\", \"seconds\": {:.2f}, \"timer_wakeups\": {}, "
                            "\"ticks\": {}, \"frames\": {}, \"cpu_ms\": {:.1f}, \"cpu_percent\": {:.2f}, "
                            "\"mcycles_per_s\": {:.1f}}}",
                            group,
                            page,
                            seconds,
                            counts.timer_wakeups,
                            counts.ticks,
                            counts.frames,
                            cpu,
                            cpu / (seconds * 10.0),
                            static_cast<double>(to.cycles - from.cycles) / seconds / 1e6)
                    .c_str());
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv)
{
    const double           settle = argc > 1 ? std::stod(argv[1]) : 4.0;
    const double           span   = argc > 2 ? std::stod(argv[2]) : 3.0;
    const std::string_view filter = argc > 3 ? argv[3] : "";

    ui::Window                window{{.title    = "libwinui Control Gallery",
                                      .size    = {1180, 820},
                                      .backdrop = ui::Backdrop::Mica,
                                      .style    = {.frame = ui::Frame::Themed}}};
    ui::Property<std::string> status{""};
    ui::ThemeRequest          theme_request{};
    gallery::Context          context{status, window, theme_request, [](std::string_view) {}};

    struct Entry
    {
        std::string_view     group;
        const gallery::Page* page;
    };
    std::vector<Entry> entries{{"", nullptr}};
    for (const gallery::Group& group : gallery::groups())
    {
        for (const gallery::Page& page : group.pages)
        {
            if (std::string_view{page.name}.find(filter) != std::string_view::npos)
            {
                entries.push_back({group.title, &page});
            }
        }
    }

    window.set_content(ui::Text{.text = ""});
    std::thread controller(
        [&]
        {
            const auto wait = [](double seconds) { std::this_thread::sleep_for(std::chrono::duration<double>(seconds)); };
            wait(1.0); // the first frame
            for (const Entry& entry : entries)
            {
                window.post(
                    [&, entry]
                    {
                        if (entry.page)
                        {
                            window.set_content(ui::Scroll{.content = entry.page->make(context)});
                        }
                    });
                wait(settle);
                auto from = std::make_shared<Sample>();
                window.post([from] { *from = Sample::now(); });
                wait(span);
                window.post(
                    [from, entry]
                    { report(entry.group, entry.page ? entry.page->name : "(empty window)", *from, Sample::now()); });
            }
            window.post([&] { window.close(); });
        });
    const int result = ui::run(window);
    controller.join();
    return result;
}
