// SPDX-License-Identifier: MIT
// winui_timers: what Window::after() and every() cost on a real, otherwise idle window, printed as
// one line of JSON: timer wake-ups, ticks, elements ticked, frames, process CPU time, and how often
// and how punctually the callback ran.
//
//     winui_timers <scenario> [seconds] [minimal|buttons|textbox|tooltip|flipview|gif|player|player_once]
//
//     idle             no timer at all, the baseline
//     pending          one after(1 hour) that never fires inside the run
//     every_20ms       every(20ms) with an empty callback (also every_100ms, every_1s)
//     label_20ms       every(20ms) writing a bound label, so every firing draws a frame
//     after_250ms      after(250ms), re-armed from its own callback
//
// "buttons" puts 1000 buttons in the window, to show what each tick costs in a large tree. The other
// trees show what a control's own waits cost on an idle window, with the "idle" scenario:
//
//     textbox      a focused TextBox, its caret blinking
//     tooltip      a text with a tool tip, the pointer moved onto it as the measurement starts
//     flipview     a Carousel, the pointer moved onto it as the measurement starts (its buttons)
//     gif          an on-screen animated image of 10 frames of 100 ms
//     player       a looped AnimatedVisualPlayer scrolled out of view
//     player_once  one 4 s play of an AnimatedVisualPlayer out of view, started with the measurement
// A thread starts the measurement once the window has gone idle and stops it `seconds` later,
// through post(), so the probe's own bookkeeping adds no timer.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winui/testing.hpp>
#include <winui/winui.hpp>

#include <algorithm>
#include <chrono>
#include <format>
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

double since_ms(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

// A circle growing over `length` milliseconds.
struct Pulse final : ui::AnimatedVisualSource
{
    double length = 1000.0;

    [[nodiscard]] ui::Size natural_size() const override
    {
        return {48.0f, 48.0f};
    }
    [[nodiscard]] ui::Duration duration() const override
    {
        return ui::Duration{length};
    }
    void render_frame(ui::Renderer& r, const ui::Rect& b, double progress) const override
    {
        const float radius = static_cast<float>(progress) * b.width / 2.0f;
        r.fill_ellipse(b.center(), radius, radius, ui::colors::Black);
    }
};

ui::AnimatedImage ten_frames()
{
    ui::AnimatedImage image;
    for (int i = 0; i < 10; ++i)
    {
        image.frames.emplace_back(16, 16, i % 2 == 0 ? ui::colors::White : ui::colors::Black);
        image.delays.push_back(ui::Duration{100.0});
    }
    return image;
}

struct Run
{
    std::string_view                 scenario;
    double                           seconds   = 10.0;
    ui::Window*                      window    = nullptr;
    ui::Property<int>*               label     = nullptr;
    ui::Element*                     target    = nullptr; // what the pointer moves onto, or focus goes to
    bool                             focus     = false;
    ui::AnimatedVisualPlayerControl* play_once = nullptr;
    ui::Subscription                 timer;
    ui::testing::PerfCounters        start_counts{};
    double                           start_cpu    = 0.0;
    unsigned long long               start_cycles = 0;
    Clock::time_point                start_time{};
    Clock::time_point                armed{};     // when the current after() was armed
    double                           delay = 0.0; // its delay
    std::vector<double>              samples;     // intervals between firings, or lateness of after()
    Clock::time_point                last_fire{};
    int                              fires = 0;

    void fired()
    {
        const Clock::time_point now = Clock::now();
        if (fires > 0)
        {
            samples.push_back(since_ms(last_fire, now));
        }
        last_fire = now;
        ++fires;
    }

    void arm_after()
    {
        armed = Clock::now();
        timer = window->after(ui::Duration{delay},
                              [this]
                              {
                                  samples.push_back(since_ms(armed, Clock::now()) - delay);
                                  ++fires;
                                  arm_after();
                              });
    }

    void start()
    {
        start_counts = ui::testing::perf_counters();
        start_cpu    = cpu_ms();
        start_cycles = cpu_cycles();
        start_time   = Clock::now();
        using std::chrono::milliseconds;
        if (target != nullptr && focus)
        {
            (void)window->focus_element(target);
        }
        else if (target != nullptr)
        {
            ui::testing::seams(*window).handle_pointer(
                {target->bounds().center(), ui::PointerButton::None, ui::PointerAction::Move});
        }
        if (play_once != nullptr)
        {
            play_once->play(0.0, 1.0, false);
        }
        if (scenario == "pending")
        {
            timer = window->after(std::chrono::hours{1}, [] {});
        }
        else if (scenario == "every_20ms" || scenario == "every_100ms" || scenario == "every_1s")
        {
            const milliseconds interval = scenario == "every_20ms"    ? milliseconds{20}
                                          : scenario == "every_100ms" ? milliseconds{100}
                                                                      : milliseconds{1000};
            timer                       = window->every(interval, [this] { fired(); });
        }
        else if (scenario == "label_20ms")
        {
            timer = window->every(milliseconds{20},
                                  [this]
                                  {
                                      fired();
                                      ++*label;
                                  });
        }
        else if (scenario == "after_250ms")
        {
            delay = 250.0;
            arm_after();
        }
    }

    void stop()
    {
        const double                    elapsed = since_ms(start_time, Clock::now());
        const double                    cpu     = cpu_ms() - start_cpu;
        const double                    mcycles = static_cast<double>(cpu_cycles() - start_cycles) / 1e6;
        const ui::testing::PerfCounters counts  = ui::testing::perf_counters() - start_counts;
        timer                                   = {};
        double mean = 0.0, low = 0.0, high = 0.0;
        if (!samples.empty())
        {
            for (double s : samples)
            {
                mean += s;
            }
            mean /= static_cast<double>(samples.size());
            low  = *std::ranges::min_element(samples);
            high = *std::ranges::max_element(samples);
        }
        const bool  lateness = scenario.starts_with("after");
        const char* what     = lateness ? "late" : "interval";
        std::printf("%s\n",
                    std::format("{{\"scenario\": \"{}\", \"seconds\": {:.1f}, \"timer_wakeups\": {}, \"ticks\": {}, "
                                "\"element_ticks\": {}, \"frames\": {}, \"cpu_ms\": {:.1f}, \"mcycles_per_s\": {:.1f}, "
                                "\"fires\": {}, "
                                "\"{}_mean_ms\": {:.1f}, \"{}_min_ms\": {:.1f}, \"{}_max_ms\": {:.1f}}}",
                                scenario,
                                elapsed / 1000.0,
                                counts.timer_wakeups,
                                counts.ticks,
                                counts.element_ticks,
                                counts.frames,
                                cpu,
                                mcycles / (elapsed / 1000.0),
                                fires,
                                what,
                                mean,
                                what,
                                low,
                                what,
                                high)
                        .c_str());
        std::fflush(stdout);
        window->close();
    }
};

} // namespace

int main(int argc, char** argv)
{
    Run run;
    run.scenario                = argc > 1 ? argv[1] : "idle";
    run.seconds                 = argc > 2 ? std::stod(argv[2]) : 10.0;
    const std::string_view tree = argc > 3 ? argv[3] : "minimal";

    Pulse             pulse; // outlives the window, which shows it
    ui::Property<int> count{0};
    ui::Window        window{{.title = "winui_timers", .size = {480, 320}}};
    run.window = &window;
    run.label  = &count;
    if (tree == "textbox")
    {
        ui::Control<ui::TextBox> box;
        window.set_content(ui::TextBox{.text = "Focused", .control = box});
        run.target = box;
        run.focus  = true;
    }
    else if (tree == "tooltip")
    {
        ui::Control<ui::Text> text;
        window.set_content(ui::Text{.text = "Hover me", .tool_tip = "A tip", .control = text});
        run.target = text;
    }
    else if (tree == "flipview")
    {
        auto flip = std::make_unique<ui::CarouselControl>();
        for (int i = 0; i < 3; ++i)
        {
            flip->add_item(std::make_unique<ui::TextControl>(std::format("Item {}", i)));
        }
        run.target = flip.get();
        window.set_content(std::move(flip));
    }
    else if (tree == "gif")
    {
        window.set_content(ui::Image{.source = ten_frames(), .stretch = ui::Stretch::None});
    }
    else if (tree == "player_visible")
    {
        pulse.length = 1000.0;
        window.set_content(ui::AnimatedVisualPlayer{.source = &pulse, .auto_play = true, .width = 96.0f, .height = 96.0f});
    }
    else if (tree == "player" || tree == "player_once")
    {
        pulse.length = tree == "player" ? 1000.0 : 4000.0;
        ui::Control<ui::AnimatedVisualPlayer> player;
        ui::Spec                              below = ui::VerticalStack{
            .content = {ui::Spacer{.height = 2000.0f},
                        ui::AnimatedVisualPlayer{.source    = &pulse,
                                                 .auto_play = tree == "player",
                                                 .width     = 96.0f,
                                                 .height    = 96.0f,
                                                 .control   = player}}};
        window.set_content(ui::Scroll{.content = std::move(below)});
        if (tree == "player_once")
        {
            run.play_once = player;
        }
    }
    else if (tree == "buttons")
    {
        ui::SpecList buttons;
        for (int i = 0; i < 1000; ++i)
        {
            buttons.push_back(ui::Button{.text = std::format("Button {}", i)});
        }
        window.set_content(ui::Scroll{
            .content = ui::VerticalStack{
                .content = {ui::Text{.text = ui::bind(count, [](int n) { return std::format("{}", n); })},
                            ui::VerticalStack{.content = std::move(buttons)}}}});
    }
    else
    {
        window.set_content(ui::Text{.text = ui::bind(count, [](int n) { return std::format("Count: {}", n); })});
    }

    // Startup keeps the tick timer running for a few seconds after the last busy tick; wait that out.
    std::thread controller(
        [&]
        {
            std::this_thread::sleep_for(std::chrono::seconds{5});
            window.post([&] { run.start(); });
            std::this_thread::sleep_for(std::chrono::duration<double>(run.seconds));
            window.post([&] { run.stop(); });
        });
    const int result = ui::run(window);
    controller.join();
    return result;
}
