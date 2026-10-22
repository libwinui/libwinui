// SPDX-License-Identifier: MIT
// libwinui tutorial: background work and the UI thread.
#include <winui/winui.hpp>

#include <coroutine>
#include <exception>
#include <format>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>

namespace
{
// --8<-- [start:slow_work]
constexpr int Limit = 10'000'000;

// Counts the primes below `limit` by trial division, which takes a few seconds on purpose. It
// touches nothing but its arguments, so any thread may run it, and it gives up when asked to.
int count_primes(int limit, std::stop_token stop = {})
{
    int count = 0;
    for (int n = 2; n < limit && !stop.stop_requested(); ++n)
    {
        bool prime = true;
        for (int d = 2; d * d <= n; ++d)
        {
            if (n % d == 0)
            {
                prime = false;
                break;
            }
        }
        count += prime ? 1 : 0;
    }
    return count;
}
// --8<-- [end:slow_work]

// --8<-- [start:state]
struct AppState
{
    ui::Property<std::string>         status{"Idle"};
    ui::Property<bool>                busy{false};
    ui::ObservableVector<std::string> log{};
    std::stop_source                  stop{}; // asked to stop when the window closes
};
// --8<-- [end:state]

// --8<-- [start:coroutine]
ui::FireAndForget count_with_coroutine(ui::Window& window, AppState& state)
{
    // On the UI thread, like any handler, up to the first co_await.
    state.busy                 = true;
    state.status               = "Counting...";
    const std::stop_token stop = state.stop.get_token();
    const ui::WindowRef   home = window.ref(); // where to come back to

    co_await ui::on_background();
    // A worker thread from here on: no element, no Property, no Window except post().
    std::string result;
    try
    {
        result = std::format("{} primes below {}", count_primes(Limit, stop), Limit);
    } catch (const std::exception& error)
    {
        result = std::format("Counting failed: {}", error.what());
    }

    co_await ui::on_ui_thread(home);
    // Back on the UI thread. If the window closed or was destroyed in the meantime, this line is
    // never reached.
    state.busy   = false;
    state.status = result;
    state.log.push_back("Coroutine: " + result);
}
// --8<-- [end:coroutine]

} // namespace

int main()
{
    AppState state;

    ui::Window window{{.title = "Background work", .size = {480, 360}}};

    // Declared after the window, so it is destroyed before it: the jthread asks its thread to
    // stop and joins it while the window still exists.
    // --8<-- [start:worker]
    std::jthread worker;
    // --8<-- [end:worker]
    ui::WindowRef log_window; // the log window while it is open

    // --8<-- [start:on_closed]
    window.on_closed(
        [&]
        {
            state.stop.request_stop(); // the coroutine's count gives up early
            worker.request_stop();
            if (worker.joinable())
            {
                worker.join(); // what it posts from now on is dropped
            }
        });
    // --8<-- [end:on_closed]

    // --8<-- [start:frozen]
    auto count_on_ui_thread = [&]
    {
        state.status     = "Counting...";       // never drawn: no frame until we return
        const int primes = count_primes(Limit); // the window is frozen for the whole count
        state.status     = std::format("{} primes below {}", primes, Limit);
        state.log.push_back("UI thread: " + state.status.get());
    };
    // --8<-- [end:frozen]

    // --8<-- [start:thread]
    auto count_with_thread = [&]
    {
        state.busy   = true;
        state.status = "Counting...";
        // Assigning over a finished jthread joins it first. The buttons are disabled while busy,
        // so the old one is done by now.
        worker = std::jthread{[&window, &state](std::stop_token stop)
                              {
                                  const int primes = count_primes(Limit, stop);
                                  window.post(
                                      [&state, primes]
                                      {
                                          state.busy   = false;
                                          state.status = std::format("{} primes below {}", primes, Limit);
                                          state.log.push_back("Thread: " + state.status.get());
                                      });
                              }};
    };
    // --8<-- [end:thread]

    // --8<-- [start:log_window]
    auto show_log = [&]
    {
        if (ui::Window* open = log_window.get())
        {
            open->activate();
            return;
        }
        auto log = std::make_unique<ui::Window>(ui::WindowOptions{.title = "Log", .size = {360, 320}});
        log->set_content(ui::Scroll{
            .content =
                ui::ForEach<std::string>{
                    .items = state.log,
                    .item  = [](const std::string& line) -> ui::Spec { return ui::Text{.text = line}; },
                },
        });
        // The loop owns it from here and destroys it once it is closed.
        log_window = ui::open_window(std::move(log)).ref();
    };
    // --8<-- [end:log_window]

    // --8<-- [start:tree]
    // One binding for every button: each one built from it follows `busy` on its own.
    const auto idle = ui::bind(state.busy, [](bool busy) { return !busy; });
    window.set_content(ui::VerticalStack{
        .spacing = 12,
        .margin  = 24,
        .content =
            {
                ui::HorizontalStack{
                    .spacing         = 12,
                    .child_alignment = ui::Alignment::Center,
                    .content =
                        {
                            ui::ProgressRing{.active = state.busy, .width = 24, .height = 24},
                            ui::Text{.text = state.status},
                        },
                },
                ui::Button{.text = "Count on the UI thread", .enabled = idle, .on_click = count_on_ui_thread},
                ui::Button{
                    .text     = "Count with a coroutine",
                    .enabled  = idle,
                    .on_click = [&] { count_with_coroutine(window, state); },
                },
                ui::Button{.text = "Count with a thread", .enabled = idle, .on_click = count_with_thread},
                ui::Button{.text = "Show log", .on_click = show_log},
            },
    });
    // --8<-- [end:tree]

    return ui::run(window);
}
