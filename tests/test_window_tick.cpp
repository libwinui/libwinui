// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;

TEST_CASE("Window::on_tick() sees every tick's elapsed time", "[window][tick]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_content(ui.text("Idle"));

    std::vector<double> seen;
    ui.window().on_tick([&seen](Duration delta) { seen.push_back(delta.count()); });
    ui.window().tick(16ms);
    ui.window().tick(100ms);
    REQUIRE(seen == std::vector<double>{16.0, 100.0});

    ui.window().on_tick({}); // an empty handler removes it
    ui.window().tick(16ms);
    REQUIRE(seen.size() == 2);
}

TEST_CASE("A change made from the tick hook asks for a repaint", "[window][tick]")
{
    HeadlessUI   ui    = create_headless();
    auto         owned = ui.text("0");
    TextControl* label = owned.get();
    ui.window().set_content(std::move(owned));
    ui.layout({400, 300});
    REQUIRE_FALSE(ui.window().tick(16ms));

    int frames = 0;
    ui.window().on_tick([&](Duration) { label->set_text(std::to_string(++frames)); });
    REQUIRE(ui.window().tick(16ms));
    REQUIRE(label->text() == "1");
}

TEST_CASE("Posted work runs on the next tick, in order, before the tick hook", "[window][tick]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_content(ui.text("Idle"));

    std::vector<std::string> order;
    ui.window().on_tick([&order](Duration) { order.push_back("hook"); });
    ui.window().post([&order] { order.push_back("first"); });
    ui.window().post([&order] { order.push_back("second"); });
    REQUIRE(order.empty()); // nothing runs until the UI thread ticks
    REQUIRE(ui.host().wake_requests == 2);

    ui.window().tick(16ms);
    REQUIRE(order == std::vector<std::string>{"first", "second", "hook"});

    // Work that posts more work queues it for the tick after, rather than running it now.
    order.clear();
    ui.window().post([&] { ui.window().post([&order] { order.push_back("later"); }); });
    ui.window().tick(16ms);
    REQUIRE(order == std::vector<std::string>{"hook"});
    ui.window().tick(16ms);
    REQUIRE(order == std::vector<std::string>{"hook", "later", "hook"});
}

TEST_CASE("post() can be called from another thread", "[window][tick]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_content(ui.text("Idle"));

    // The worker owns something move-only, the case Callback exists for.
    int         result = 0;
    auto        value  = std::make_unique<int>(42);
    std::thread worker([&ui, &result, value = std::move(value)]() mutable
                       { ui.window().post([&result, value = std::move(value)] { result = *value; }); });
    worker.join();
    REQUIRE(ui.host().wake_requests == 1);
    REQUIRE(result == 0);

    ui.window().tick(16ms);
    REQUIRE(result == 42);
}

TEST_CASE("on_closing() can keep the window open", "[window][close]")
{
    HeadlessUI ui = create_headless();
    REQUIRE(ui::testing::seams(ui.window()).handle_close_request()); // no handler: every close goes through

    bool dirty = true;
    ui.window().on_closing([&dirty] { return dirty ? ui::Cancel : ui::Proceed; });
    REQUIRE_FALSE(ui::testing::seams(ui.window()).handle_close_request());
    dirty = false;
    REQUIRE(ui::testing::seams(ui.window()).handle_close_request());
}

TEST_CASE("on_closed() is where workers stop, and post() drops work from then on", "[window][close]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_content(ui.text("Idle"));

    // A worker that posts until it is told to stop, like a decode pool.
    std::atomic<bool> stop{false};
    std::atomic<int>  posted{0};
    int               ran = 0;
    std::thread       worker(
        [&]
        {
            while (!stop)
            {
                ui.window().post([&ran] { ++ran; });
                ++posted;
                std::this_thread::yield();
            }
            ui.window().post([&ran] { ++ran; }); // one on the way out, after on_closed() began
        });
    while (posted < 3)
    {
        std::this_thread::yield();
    }

    bool closed = false;
    ui.window().on_closed(
        [&]
        {
            closed = true;
            stop   = true;
            worker.join();
        });
    ui::testing::seams(ui.window()).handle_closed();
    REQUIRE(closed);
    REQUIRE_FALSE(worker.joinable());

    // Nothing queued before or after survives the close.
    ui.window().post([&ran] { ++ran; });
    ui.window().tick(16ms);
    REQUIRE(ran == 0);
}


TEST_CASE("after() runs once on the first tick at or past its time", "[window][tick][timer]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_content(ui.text("Idle"));

    int          runs  = 0;
    Subscription timer = ui.window().after(100ms, [&] { ++runs; });
    REQUIRE(timer.alive());
    ui.tick(60ms);
    REQUIRE(runs == 0);
    ui.tick(60ms);
    REQUIRE(runs == 1);
    ui.tick(500ms);
    REQUIRE(runs == 1);
}

TEST_CASE("post_after() runs once with nothing to keep", "[window][tick][timer]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_content(ui.text("Idle"));

    int runs = 0;
    ui.window().post_after(100ms, [&] { ++runs; });
    REQUIRE(ui.window().next_wake() == 100ms);
    ui.tick(60ms);
    REQUIRE(runs == 0);
    ui.tick(60ms);
    REQUIRE(runs == 1);
    ui.tick(500ms);
    REQUIRE(runs == 1);
    REQUIRE_FALSE(ui.window().next_wake().has_value());
}

TEST_CASE("post_after() is dropped when the window closes first", "[window][tick][timer]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_content(ui.text("Idle"));

    int          dropped = 0;
    int          kept    = 0;
    Subscription timer   = ui.window().after(50ms, [&] { ++kept; });
    ui.window().post_after(50ms, [&] { ++dropped; });
    ui::testing::seams(ui.window()).handle_closed();
    ui.window().post_after(10ms, [&] { ++dropped; }); // on a closed window
    ui.tick(100ms);
    REQUIRE(dropped == 0);
    REQUIRE(kept == 1); // an after() timer belongs to its Subscription
}

TEST_CASE("Dropping the Subscription cancels a timer", "[window][tick][timer]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_content(ui.text("Idle"));

    int runs = 0;
    {
        Subscription timer = ui.window().after(10ms, [&] { ++runs; });
    }
    Subscription reset = ui.window().every(10ms, [&] { ++runs; });
    reset.reset();
    ui.tick(50ms);
    REQUIRE(runs == 0);
}

TEST_CASE("every() repeats, and a late tick runs it once", "[window][tick][timer]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_content(ui.text("Idle"));

    int          runs  = 0;
    Subscription timer = ui.window().every(1s, [&] { ++runs; });
    ui.tick(999ms);
    REQUIRE(runs == 0);
    ui.tick(1ms);
    REQUIRE(runs == 1);
    ui.tick(1000ms);
    REQUIRE(runs == 2);
    ui.tick(3500ms); // three intervals late: one run, the next a second later
    REQUIRE(runs == 3);
    ui.tick(999ms);
    REQUIRE(runs == 3);
    ui.tick(1ms);
    REQUIRE(runs == 4);
}

TEST_CASE("A pending timer asks for one tick when it is due, not for ticks all the while", "[window][tick][timer]")
{
    HeadlessUI ui = create_headless();
    ui.show({400, 300});
    ui.window().set_content(ui.text("Idle"));
    ui.layout({400, 300});
    (void)ui.tick(16ms);
    REQUIRE_FALSE(ui.window().wants_ticks());
    REQUIRE_FALSE(ui.window().next_wake().has_value());

    const int    wakes = ui.host().wake_requests;
    Subscription timer = ui.window().after(5s, [] {});
    REQUIRE(ui.host().wake_requests == wakes + 1); // an idle host is woken to ask when to tick
    REQUIRE(ui.window().next_wake() == 5000ms);
    (void)ui.tick(1000ms);
    REQUIRE_FALSE(ui.window().wants_ticks());
    REQUIRE(ui.window().next_wake() == 4000ms);
    Subscription soon = ui.window().every(250ms, [] {});
    REQUIRE(ui.window().next_wake() == 250ms); // the earliest one
    soon.reset();
    REQUIRE(ui.window().next_wake() == 4000ms); // a canceled timer no longer counts
    (void)ui.tick(3000ms);
    REQUIRE(ui.window().next_wake() == 1000ms);
    (void)ui.tick(1000ms);
    REQUIRE_FALSE(ui.window().next_wake().has_value()); // it ran, so nothing is pending
}

namespace
{
// Waits for a moment `due_ms` into the window's time, asking for one tick then.
class WakeAt final : public Element
{
public:
    explicit WakeAt(double due_ms) : due_ms_(due_ms)
    {
    }
    double clock_ms = 0.0;
    int    woken    = 0;

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
    void tick_override(Duration delta) override
    {
        clock_ms += delta.count();
        if (clock_ms >= due_ms_)
        {
            ++woken;
            return;
        }
        request_tick_in(Duration{due_ms_ - clock_ms});
    }


private:
    double due_ms_;
};
} // namespace

TEST_CASE("An element's request_tick_in() wakes the window once, without keeping it busy", "[window][tick][timer]")
{
    HeadlessUI ui = create_headless();
    ui.show({400, 300});
    auto* waiting = ui.window().set_content(std::make_unique<WakeAt>(1000.0));
    ui.layout({400, 300});
    (void)ui.tick(16ms);
    REQUIRE_FALSE(ui.window().wants_ticks());
    REQUIRE(ui.window().next_wake() == 984ms);

    (void)ui.tick(484ms); // something else woke it early: the element asks again
    REQUIRE_FALSE(ui.window().wants_ticks());
    REQUIRE(ui.window().next_wake() == 500ms);

    Subscription timer = ui.window().after(std::chrono::milliseconds{200}, [] {});
    REQUIRE(ui.window().next_wake() == 200ms); // the earlier of the two

    timer.reset();
    (void)ui.tick(500ms);
    REQUIRE(waiting->woken == 1);
    REQUIRE_FALSE(ui.window().next_wake().has_value()); // it asked for nothing more
    REQUIRE_FALSE(ui.window().wants_ticks());
}

TEST_CASE("A minimized window does not wake for an element", "[window][tick][timer]")
{
    HeadlessUI ui = create_headless();
    ui.show({400, 300}, WindowState::Minimized);
    (void)ui.window().set_content(std::make_unique<WakeAt>(1000.0));
    (void)ui.tick(16ms);
    REQUIRE_FALSE(ui.window().next_wake().has_value());
}

namespace
{
// Changes how it draws every `period_ms`, and sleeps in between: a caret's blink.
class Blink final : public Element
{
public:
    explicit Blink(double period_ms) : period_ms_(period_ms)
    {
    }
    int blinks = 0;

    void poke()
    {
        mark_dirty(DirtyFlags::Visual);
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
    void tick_override(Duration delta) override
    {
        clock_ms_ += delta.count();
        if (clock_ms_ >= period_ms_)
        {
            clock_ms_ -= period_ms_;
            ++blinks;
            mark_dirty(DirtyFlags::Visual);
        }
        request_tick_in(Duration{period_ms_ - clock_ms_});
    }


private:
    double period_ms_;
    double clock_ms_ = 0.0;
};

// Changes on every tick without saying when it is next due: an animation it runs itself.
class Spin final : public Element
{
public:
    bool   spinning = true;
    double wake_ms  = 0.0;

protected: // also waits for something this far ahead, when above 0
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
    void tick_override(Duration) override
    {
        if (spinning)
        {
            mark_dirty(DirtyFlags::Visual);
        }
        if (wake_ms > 0.0)
        {
            request_tick_in(Duration{wake_ms});
        }
    }
};
} // namespace

TEST_CASE("A change only timed steps made is drawn without keeping the window busy", "[window][tick][timer]")
{
    HeadlessUI ui = create_headless();
    ui.show({400, 300});
    auto* blink = ui.window().set_content(std::make_unique<Blink>(500.0));
    ui.layout({400, 300});
    (void)ui.tick(16ms);
    REQUIRE_FALSE(ui.window().wants_ticks());
    REQUIRE(ui.window().next_wake() == 484ms);

    REQUIRE(ui.tick(484ms)); // the blink asks for a repaint
    REQUIRE(blink->blinks == 1);
    REQUIRE_FALSE(ui.window().wants_ticks());
    REQUIRE(ui.window().next_wake() == 500ms);

    // A tick before the frame finds the blink's change still undrawn: still no reason to be busy.
    REQUIRE(ui.tick(100ms));
    REQUIRE_FALSE(ui.window().wants_ticks());
    REQUIRE(ui.window().next_wake() == 400ms);

    // Anything else that marks the tree is, drawn or not: here a setter between two ticks.
    blink->poke();
    (void)ui.tick(16ms);
    REQUIRE(ui.window().wants_ticks());
    ui.layout({400, 300});
    (void)ui.tick(16ms);
    REQUIRE_FALSE(ui.window().wants_ticks());
}

TEST_CASE("A timed step does not hide another element's animation in the same tick", "[window][tick][timer]")
{
    HeadlessUI ui    = create_headless();
    auto       stack = std::make_unique<StackControl>(Orientation::Vertical);
    auto*      blink = stack->add(std::make_unique<Blink>(100.0));
    auto*      spin  = stack->add(std::make_unique<Spin>());
    ui.window().set_content(std::move(stack));
    ui.show({400, 300});
    ui.layout({400, 300});

    // Both change in this tick, the spinner without a wake: busy.
    (void)ui.tick(100ms);
    REQUIRE(blink->blinks == 1);
    REQUIRE(ui.window().wants_ticks());

    spin->spinning = false;
    ui.layout({400, 300});
    (void)ui.tick(100ms);
    REQUIRE(blink->blinks == 2);
    REQUIRE_FALSE(ui.window().wants_ticks());

    // An animation of the element's own visual layer keeps the window busy too, wake or not.
    blink->start_animation(AnimatedProperty::Opacity, DoubleAnimation{.from = 1.0f, .to = 0.0f, .duration = 300ms});
    ui.layout({400, 300});
    (void)ui.tick(50ms);
    REQUIRE(ui.window().wants_ticks());
    (void)ui.tick(50ms); // blinks here as well
    REQUIRE(blink->blinks == 3);
    REQUIRE(ui.window().wants_ticks());
}

TEST_CASE("An animation that also waits for something later keeps the window busy", "[window][tick][timer]")
{
    HeadlessUI ui = create_headless();
    ui.show({400, 300});
    auto* spin    = ui.window().set_content(std::make_unique<Spin>());
    spin->wake_ms = 2000.0;
    ui.layout({400, 300});
    for (int i = 0; i < 5; ++i)
    {
        (void)ui.tick(100ms);
        REQUIRE(ui.window().wants_ticks());
        REQUIRE(ui.window().next_wake() == 2000ms);
    }
}

TEST_CASE("A timer started from posted work counts from that tick, not from the one before", "[window][tick][timer]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_content(ui.text("Idle"));
    (void)ui.tick(16ms);

    // An idle window gets no ticks, so the next one carries all the time since the last.
    int          runs = 0;
    Subscription timer;
    ui.window().post([&] { timer = ui.window().after(250ms, [&] { ++runs; }); });
    (void)ui.tick(Duration{10'000.0});
    REQUIRE(runs == 0);
    REQUIRE(ui.window().next_wake() == 250ms);
    (void)ui.tick(250ms);
    REQUIRE(runs == 1);
}

TEST_CASE("A timer's handler may cancel itself or start another timer", "[window][tick][timer]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_content(ui.text("Idle"));

    int          runs = 0;
    Subscription self;
    self = ui.window().every(10ms,
                             [&]
                             {
                                 ++runs;
                                 self.reset(); // cancels its own timer while it runs
                             });
    Subscription chained;
    Subscription first = ui.window().after(10ms, [&] { chained = ui.window().after(10ms, [&] { runs += 10; }); });
    ui.tick(10ms);
    REQUIRE(runs == 1);
    REQUIRE_FALSE(self.alive());
    ui.tick(10ms);
    REQUIRE(runs == 11);
}

TEST_CASE("A timer's Subscription outlives its window safely", "[window][tick][timer]")
{
    Subscription timer;
    {
        HeadlessUI ui = create_headless();
        timer         = ui.window().after(10ms, [] {});
        REQUIRE(timer.alive());
    }
    REQUIRE_FALSE(timer.alive());
    timer.reset();
}

TEST_CASE("is_open() is true while the window is open and turns false as it closes", "[window][close]")
{
    HeadlessUI ui = create_headless();
    REQUIRE(ui.window().is_open().get());

    std::vector<bool> seen;
    Subscription      watch = ui.window().is_open().observe([&](const bool& open) { seen.push_back(open); });
    bool              open_in_on_closed = true;
    ui.window().on_closed([&] { open_in_on_closed = ui.window().is_open().get(); });
    seams(ui.window()).handle_closed();
    CHECK(seen == std::vector<bool>{false});
    CHECK_FALSE(open_in_on_closed);

    seams(ui.window()).handle_opened(); // shown again
    CHECK(ui.window().is_open().get());
}

TEST_CASE("force_close() closes without asking on_closing(), once", "[window][close]")
{
    HeadlessUI ui    = create_headless();
    int        asked = 0;
    ui.window().on_closing(
        [&]
        {
            ++asked;
            return ui::Cancel; // "Save changes?" is showing instead
        });

    ui.window().close();
    CHECK(ui.host().close_requests == 1);
    CHECK_FALSE(seams(ui.window()).handle_close_request());
    CHECK(asked == 1);

    ui.window().force_close();
    CHECK(ui.host().close_requests == 2);
    CHECK(seams(ui.window()).handle_close_request());
    CHECK(asked == 1);

    // Only that one close: the next one asks again.
    CHECK_FALSE(seams(ui.window()).handle_close_request());
    CHECK(asked == 2);
}

TEST_CASE("WindowRef::get() is the window while it exists and null after", "[window][close]")
{
    WindowRef ref;
    {
        HeadlessUI ui = create_headless();
        ref           = ui.window().ref();
        CHECK(ref.get() == &ui.window());
    }
    CHECK(ref.get() == nullptr);
    CHECK(WindowRef{}.get() == nullptr);
}
