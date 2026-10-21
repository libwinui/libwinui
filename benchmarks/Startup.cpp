// SPDX-License-Identifier: MIT
// winui_startup: how long an app takes to its first window, first frame and first idle message
// loop, its private bytes once idle and the frames and layout passes it took, printed as one line
// of JSON. It opens a real window and closes it again right after the first idle.
//
//     winui_startup minimal   the first tutorial's window: a label and two buttons
//     winui_startup gallery   the gallery's first page in its window
//
// Run it twice: the first run after a reboot (or after the binaries were rebuilt) is the cold
// start, the second the warm one.
#include "gallery.hpp"

#include <winui/testing.hpp>
#include <winui/winui.hpp>

#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <cstdio>

namespace
{

std::string ms(const std::optional<ui::Duration>& value)
{
    return value ? std::format("{:.1f}", value->count()) : std::string{"null"};
}

// Closes `window` on the first tick after the first idle, and prints what was measured.
void report_when_idle(ui::Window& window, std::string_view scenario)
{
    window.on_tick(
        [&window, scenario, reported = false](ui::Duration) mutable
        {
            const ui::testing::StartupMilestones m      = ui::testing::startup_milestones();
            const ui::testing::PerfCounters      counts = ui::testing::perf_counters();
            if (reported || !m.first_idle)
            {
                return;
            }
            reported = true;
            std::printf(
                "{\"scenario\": \"%.*s\", \"first_window_ms\": %s, \"first_frame_ms\": %s, \"first_idle_ms\": %s, "
                "\"private_bytes\": %llu, \"frames\": %llu, \"layout_passes\": %llu}\n",
                static_cast<int>(scenario.size()),
                scenario.data(),
                ms(m.first_window).c_str(),
                ms(m.first_frame).c_str(),
                ms(m.first_idle).c_str(),
                static_cast<unsigned long long>(ui::testing::private_bytes()),
                static_cast<unsigned long long>(counts.frames),
                static_cast<unsigned long long>(counts.layout_passes));
            std::fflush(stdout);
            window.close();
        });
}

int minimal()
{
    ui::Property<int> clicks{0};
    ui::Window        window{{.title = "My first window", .size = {480, 320}}};
    window.set_content(ui::VerticalStack{
        .spacing              = 12.0f,
        .horizontal_alignment = ui::Alignment::Center,
        .vertical_alignment   = ui::Alignment::Center,
        .content =
            {
                ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })},
                ui::Button{.text = "Click me", .on_click = [&] { ++clicks; }},
                ui::Button{.text = "Reset", .on_click = [&] { clicks = 0; }},
            },
    });
    report_when_idle(window, "minimal");
    return ui::run(window);
}

int gallery_page()
{
    ui::Window                window{{.title    = "libwinui Control Gallery",
                                      .size    = {1180, 820},
                                      .backdrop = ui::Backdrop::Mica,
                                      .style    = {.frame = ui::Frame::Themed}}};
    ui::Property<std::string> status{""};
    ui::ThemeRequest          theme_request{};
    gallery::Context          context{status, window, theme_request, [](std::string_view) {}};
    const gallery::Page&      page = gallery::groups().front().pages.front();
    window.set_content(ui::Scroll{.content = page.make(context)});
    report_when_idle(window, "gallery");
    return ui::run(window);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string_view scenario = argc > 1 ? argv[1] : "minimal";
    if (scenario == "minimal")
    {
        return minimal();
    }
    if (scenario == "gallery")
    {
        return gallery_page();
    }
    std::fprintf(stderr, "usage: winui_startup [minimal|gallery]\n");
    return 2;
}
