// SPDX-License-Identifier: MIT
// FireAndForget coroutines hopping between a window's UI thread and the background.
#include "winui/core/Scheduler.hpp"
#include "winui/core/ThreadCheck.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Coroutine.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/Dialog.hpp"
#include "winui/ui/text/Text.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

using namespace ui;

namespace
{
std::atomic<int> thread_failures{0};

void count_failure(const ThreadCheckFailure&)
{
    ++thread_failures;
}

// Sets a flag and records the thread when the coroutine frame holding it is destroyed.
struct FrameGuard
{
    std::atomic<bool>* destroyed;
    std::thread::id*   on;
    FrameGuard(std::atomic<bool>* d, std::thread::id* t) : destroyed(d), on(t)
    {
    }
    FrameGuard(const FrameGuard&)            = delete;
    FrameGuard& operator=(const FrameGuard&) = delete;
    ~FrameGuard()
    {
        *on        = std::this_thread::get_id();
        *destroyed = true;
    }
};

// Ticks the window until `done` or a few seconds have passed.
void tick_until(testing::HeadlessUI& ui, const std::atomic<bool>& done)
{
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!done && std::chrono::steady_clock::now() < give_up)
    {
        ui.tick(1ms);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
}
} // namespace

TEST_CASE("on_ui_thread() resumes inside the next tick(), never at once", "[coroutine]")
{
    auto            ui    = testing::create_headless();
    Window&         w     = ui.window();
    const WindowRef home  = w.ref();
    int             stage = 0;

    auto work = [&]() -> FireAndForget
    {
        stage = 1;
        co_await on_ui_thread(home); // already on the UI thread: still goes through post()
        stage = 2;
    };
    work();
    CHECK(stage == 1);

    int stage_in_tick_handler = -1;
    w.on_tick([&](Duration) { stage_in_tick_handler = stage; });
    ui.tick(16ms);
    CHECK(stage == 2);
    CHECK(stage_in_tick_handler == 2); // posted work runs ahead of on_tick()
}

TEST_CASE("a coroutine waiting for a window that closes is destroyed, not resumed", "[coroutine]")
{
    auto            ui   = testing::create_headless();
    Window&         w    = ui.window();
    const WindowRef home = w.ref();

    std::atomic<bool> destroyed{false};
    std::thread::id   destroyed_on;
    bool              resumed = false;
    auto              work    = [&]() -> FireAndForget
    {
        FrameGuard guard{&destroyed, &destroyed_on};
        co_await on_ui_thread(home);
        resumed = true;
    };

    work();
    CHECK_FALSE(destroyed);
    ui::testing::seams(w).handle_closed(); // the host closed the OS window: queued work is dropped
    CHECK(destroyed);
    CHECK(destroyed_on == std::this_thread::get_id());
    ui.tick(16ms);
    CHECK_FALSE(resumed);

    // Awaiting a window that is closed already: dropped at once, by the awaiting thread.
    destroyed = false;
    work();
    CHECK(destroyed);
    CHECK_FALSE(resumed);
}

TEST_CASE("a coroutine dropped on a worker is destroyed on that worker", "[coroutine]")
{
    auto            ui   = testing::create_headless();
    Window&         w    = ui.window();
    const WindowRef home = w.ref();
    ui::testing::seams(w).handle_closed();

    Scheduler         pool{1};
    std::atomic<bool> destroyed{false};
    std::thread::id   destroyed_on;
    auto              work = [&]() -> FireAndForget
    {
        FrameGuard guard{&destroyed, &destroyed_on};
        co_await on_background(pool);
        co_await on_ui_thread(home);
    };
    work();
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!destroyed && std::chrono::steady_clock::now() < give_up)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE(destroyed);
    CHECK(destroyed_on != std::this_thread::get_id());
}

TEST_CASE("on_background() moves to another thread and on_ui_thread() comes back to the tree", "[coroutine]")
{
    thread_failures                   = 0;
    const ThreadCheckHandler previous = set_thread_check_handler(&count_failure);

    auto            ui   = testing::create_headless();
    Window&         w    = ui.window();
    const WindowRef home = w.ref();
    TextControl*    text = w.set_content(std::make_unique<TextControl>("waiting"));

    std::thread::id   background;
    std::thread::id   back;
    std::atomic<bool> done{false};
    auto              work = [&]() -> FireAndForget
    {
        co_await on_background();
        background         = std::this_thread::get_id();
        std::string result = "done";
        co_await on_ui_thread(home);
        back = std::this_thread::get_id();
        text->set_text(result); // checked: the tree's own thread
        done = true;
    };
    work();
    tick_until(ui, done);

    set_thread_check_handler(previous);
    REQUIRE(done);
    CHECK(background != std::this_thread::get_id());
    CHECK(back == std::this_thread::get_id());
    CHECK(text->text() == "done");
    CHECK(thread_failures == 0);
}

TEST_CASE("a coroutine hopping back to a destroyed Window is destroyed on the worker, not resumed", "[coroutine]")
{
    // main() returning while the default pool still counts: the Window is gone before the hop.
    std::optional<Window> window{std::in_place};
    const WindowRef       home = window->ref();
    Scheduler             pool{1};
    std::atomic<bool>     release{false};
    std::atomic<bool>     destroyed{false};
    std::thread::id       destroyed_on;
    std::atomic<bool>     resumed{false};
    auto                  work = [&]() -> FireAndForget
    {
        FrameGuard guard{&destroyed, &destroyed_on};
        co_await on_background(pool);
        while (!release)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        co_await on_ui_thread(home); // the Window is destroyed by now
        resumed = true;
    };
    work();
    window.reset();
    release            = true;
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!destroyed && std::chrono::steady_clock::now() < give_up)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE(destroyed);
    CHECK(destroyed_on != std::this_thread::get_id());
    CHECK_FALSE(resumed);
}

TEST_CASE("WindowRef::post() queues while the Window lives and drops the work once it is destroyed", "[coroutine]")
{
    std::atomic<bool> destroyed{false};
    std::thread::id   destroyed_on;
    bool              ran = false;

    auto            ui   = testing::create_headless();
    const WindowRef home = ui.window().ref();
    CHECK(home.post([&] { ran = true; }));
    ui.tick(16ms);
    CHECK(ran);

    std::optional<Window> window{std::in_place};
    const WindowRef       gone = window->ref();
    window.reset();

    ran                          = false;
    bool                  posted = true;
    std::thread           worker{[&]
                                 {
                           auto guard = std::make_shared<FrameGuard>(&destroyed, &destroyed_on);
                           posted     = gone.post([&ran, g = std::move(guard)] { ran = true; });
                                 }};
    const std::thread::id worker_id = worker.get_id();
    worker.join();
    CHECK_FALSE(posted);
    CHECK(destroyed); // the work was destroyed, not leaked
    CHECK(destroyed_on == worker_id);
    CHECK_FALSE(ran);

    CHECK_FALSE(WindowRef{}.post([] {})); // names no window
}

TEST_CASE("posting from a worker while the Window is destroyed either queues or drops, never crashes", "[coroutine]")
{
    for (int round = 0; round < 20; ++round)
    {
        auto              window = std::make_unique<Window>();
        const WindowRef   home   = window->ref();
        std::atomic<int>  alive{0}; // posted work not yet destroyed
        std::atomic<bool> started{false};
        std::thread       worker{[&]
                                 {
                               struct Counted
                               {
                                   std::atomic<int>* alive;
                                   explicit Counted(std::atomic<int>* a) : alive(a)
                                   {
                                       ++*alive;
                                   }
                                   Counted(const Counted&)            = delete;
                                   Counted& operator=(const Counted&) = delete;
                                   ~Counted()
                                   {
                                       --*alive;
                                   }
                               };
                               started = true;
                               while (home.post([c = std::make_shared<Counted>(&alive)] {}))
                               {
                               }
                                 }};
        while (!started)
        {
            std::this_thread::yield();
        }
        window.reset(); // destroys whatever was queued; the worker's next post is dropped
        worker.join();
        CHECK(alive == 0);
    }
}

TEST_CASE("a WindowRef does not reach a new Window built at the old one's address", "[coroutine]")
{
    std::optional<Window> window{std::in_place};
    const Window*         first_address = &*window;
    const WindowRef       old           = window->ref();
    window.reset();
    window.emplace(); // a re-created dialog: same storage
    REQUIRE(&*window == first_address);
    CHECK(window->ref() != old);

    bool ran = false;
    CHECK_FALSE(old.post([&] { ran = true; }));

    std::atomic<bool> destroyed{false};
    std::thread::id   destroyed_on;
    bool              resumed = false;
    auto              work    = [&]() -> FireAndForget
    {
        FrameGuard guard{&destroyed, &destroyed_on};
        co_await on_ui_thread(old);
        resumed = true;
    };
    work();
    CHECK(destroyed); // dropped at once, not queued on the new window
    CHECK(destroyed_on == std::this_thread::get_id());
    CHECK_FALSE(ran);
    CHECK_FALSE(resumed);
}

TEST_CASE("on_background() on an inline scheduler resumes at once on the calling thread", "[coroutine]")
{
    Scheduler       inline_pool{Scheduler::Inline{}};
    std::thread::id resumed_on;
    auto            work = [&]() -> FireAndForget
    {
        co_await on_background(inline_pool);
        resumed_on = std::this_thread::get_id();
    };
    work();
    CHECK(resumed_on == std::this_thread::get_id());
}

namespace
{
void click(Window& w, const Element* button)
{
    const Point center = button->bounds().center();
    ui::testing::seams(w).handle_pointer({center, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(w).handle_pointer({center, PointerButton::Left, PointerAction::Up});
}
} // namespace

TEST_CASE("show_dialog() resumes with the answer on the next tick, after the spec's own handlers",
          "[coroutine][content_dialog]")
{
    auto    ui = testing::create_headless();
    Window& w  = ui.window();
    ui.layout({800, 600});

    std::vector<std::string> log;
    bool                     busy = true;
    Control<Dialog>          handle;
    auto                     work = [&]() -> FireAndForget
    {
        const DialogResult
            answer = co_await show_dialog(w,
                                          Dialog{
                                              .title   = "Delete?",
                                              .primary = {.text = "Delete"},
                                              .close   = {.text = "Cancel"},
                                              .on_closing =
                                                  [&]
                                              {
                                                  log.emplace_back("closing");
                                                  return busy ? ui::Cancel : ui::Proceed;
                                              },
                                              .on_closed = [&](DialogResult) { log.emplace_back("closed"); },
                                              .control   = handle,
                                          });
        log.emplace_back(answer == DialogResult::Primary ? "primary" : "other");
    };
    work();
    REQUIRE(w.has_modal());
    ui.layout({800, 600});
    DialogControl* d = handle;

    click(w, d->primary_button()); // on_closing cancels: the coroutine keeps waiting
    ui.tick(16ms);
    CHECK(w.has_modal());
    CHECK(log == std::vector<std::string>{"closing"});

    busy = false;
    click(w, d->primary_button());
    CHECK_FALSE(w.has_modal());
    CHECK(log == std::vector<std::string>{"closing", "closing", "closed"}); // not resumed at once
    ui.tick(16ms);
    CHECK(log == std::vector<std::string>{"closing", "closing", "closed", "primary"});
}

TEST_CASE("a coroutine whose dialog goes unanswered is destroyed, not resumed", "[coroutine][content_dialog]")
{
    std::atomic<bool> destroyed{false};
    std::thread::id   destroyed_on;
    bool              resumed = false;
    auto              work    = [&](Window& w) -> FireAndForget
    {
        FrameGuard guard{&destroyed, &destroyed_on};
        (void)co_await show_dialog(w, Dialog{.title = "Question", .close = {.text = "Close"}});
        resumed = true;
    };

    SECTION("another modal replaces it")
    {
        auto ui = testing::create_headless();
        work(ui.window());
        CHECK_FALSE(destroyed);
        ui.window().show_modal(Dialog{.title = "Next", .close = {.text = "Close"}});
        ui.tick(16ms);
        CHECK(destroyed);
    }
    SECTION("the window is destroyed with it open")
    {
        std::optional<testing::HeadlessUI> ui;
        ui.emplace();
        work(ui->window());
        CHECK_FALSE(destroyed);
        ui.reset();
        CHECK(destroyed);
    }
    SECTION("the window closes before the answer resumes it")
    {
        auto ui = testing::create_headless();
        work(ui.window());
        ui.window().close_modal(); // answered with None: the resume is queued
        CHECK_FALSE(destroyed);
        ui::testing::seams(ui.window()).handle_closed();
        CHECK(destroyed);
        ui.tick(16ms);
    }
    CHECK_FALSE(resumed);
}
