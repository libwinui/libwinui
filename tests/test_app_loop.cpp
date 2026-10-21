// SPDX-License-Identifier: MIT
// The app loop against the real backend: ui::run() borrows a Window the caller owns. Every window
// here is created hidden, so nothing appears on screen.
#include "winui/core/Geometry.hpp"
#include "winui/testing/PerfCounters.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/App.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/text/Text.hpp"

#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#ifdef _WIN32

using namespace ui;

TEST_CASE("An idle window stops waking the process up", "[app][win32][perf-count]")
{
    Window window{{.title = "idle", .size = {320, 200}, .visible = false}};
    window.set_content(ui::Text{.text = "Nothing moves here"});

    // Read from another thread, since a post() would itself be something to do. The counters are
    // atomics, readable from anywhere.
    std::uint64_t before_quiet = 0;
    std::uint64_t after_quiet  = 0;
    std::thread   watcher{[&]
                          {
                            std::this_thread::sleep_for(std::chrono::milliseconds{3600}); // past the idle grace
                            before_quiet = ui::testing::perf_counters().timer_wakeups;
                            std::this_thread::sleep_for(std::chrono::milliseconds{1000});
                            after_quiet = ui::testing::perf_counters().timer_wakeups;
                            window.post([] { ui::quit(0); });
                          }};
    REQUIRE(ui::run(window) == 0);
    watcher.join();
    CHECK(after_quiet == before_quiet);
}

TEST_CASE("run() borrows the window: it is created, runs until quit() and stays a valid object", "[app][win32]")
{
    Window window{{.title = "test", .size = {320, 200}, .visible = false}};
    window.set_content(ui::Text{.text = "Hello"});
    REQUIRE(ui::testing::seams(window).window_host() == nullptr);

    int  closed       = 0;
    bool had_host     = false;
    bool was_laid_out = false;
    window.on_closed([&] { ++closed; });
    window.post(
        [&]
        {
            had_host     = ui::testing::seams(window).window_host() != nullptr;
            was_laid_out = window.content() != nullptr && window.content()->bounds().width > 0.0f;
            ui::quit(7);
        });

    REQUIRE(ui::run(window) == 7);
    CHECK(had_host);
    CHECK(was_laid_out);
    CHECK(closed == 1);

    // The loop closed the OS window; the Window and its tree are still the caller's.
    CHECK(ui::testing::seams(window).window_host() == nullptr);
    REQUIRE(window.content() != nullptr);
    CHECK(window.title() == "test");
}

TEST_CASE("a closed window can be run again", "[app][win32]")
{
    Window window{{.visible = false}};
    int    closed = 0;
    window.on_closed([&] { ++closed; });

    window.post([] { ui::quit(1); });
    REQUIRE(ui::run(window) == 1);

    // post() drops its work while the window is closed.
    bool ran = false;
    window.post([&] { ran = true; });
    window.on_tick([](Duration) { ui::quit(2); });
    REQUIRE(ui::run(window) == 2);
    CHECK_FALSE(ran);
    CHECK(closed == 2);
}

TEST_CASE("a window destroyed while its OS window exists takes it down and runs on_closed()", "[app][win32]")
{
    Window main_window{{.visible = false}};
    int    closed = 0;
    main_window.post(
        [&]
        {
            {
                // Far off screen and never activated: show() is what creates a second window.
                Window other{{.size = {100, 100}, .position = Point{-30000.0f, -30000.0f}}};
                other.on_closed([&] { ++closed; });
                other.show_inactive();
                CHECK(ui::testing::seams(other).window_host() != nullptr);
            }
            ui::quit(3);
        });
    REQUIRE(ui::run(main_window) == 3);
    CHECK(closed == 1);
}

#endif // _WIN32
