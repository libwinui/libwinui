// SPDX-License-Identifier: MIT
#include "winui/core/Scheduler.hpp"

#include <atomic>
#include <numeric>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace ui;

TEST_CASE("A queued job runs the work and wait() blocks until it has completed", "[scheduler]")
{
    Scheduler        scheduler;
    std::atomic<int> value{0};

    detail::Job task = detail::submit(scheduler, [&] { value.store(42, std::memory_order_relaxed); });
    task.wait();

    REQUIRE(value.load(std::memory_order_relaxed) == 42);
}

TEST_CASE("Many independent jobs all run exactly once", "[scheduler]")
{
    Scheduler        scheduler{4};
    constexpr int    Jobs = 500;
    std::atomic<int> counter{0};

    std::vector<detail::Job> tasks;
    tasks.reserve(Jobs);
    for (int i = 0; i < Jobs; ++i)
    {
        tasks.push_back(detail::submit(scheduler, [&] { counter.fetch_add(1, std::memory_order_relaxed); }));
    }
    for (detail::Job& t : tasks)
    {
        t.wait();
    }

    REQUIRE(counter.load() == Jobs);
}

TEST_CASE("A default Scheduler has at least one worker thread", "[scheduler]")
{
    Scheduler scheduler;
    REQUIRE(scheduler.thread_count() >= 1);
    REQUIRE(scheduler.worker_count() == scheduler.thread_count());
}

TEST_CASE("Scheduler::Inline runs work synchronously, in submission order, with no worker threads",
          "[scheduler][deterministic]")
{
    Scheduler scheduler{Scheduler::Inline{}};
    REQUIRE(scheduler.thread_count() == 0);
    REQUIRE(scheduler.worker_count() == 1); // still at least one chunk's worth

    // No lock needed: in inline mode detail::submit() runs the work before returning, on the
    // calling thread, so this vector is never touched concurrently.
    std::vector<int> order;
    for (int i = 0; i < 10; ++i)
    {
        detail::Job t = detail::submit(scheduler, [&order, i] { order.push_back(i); });
        t.wait();
        REQUIRE(order.back() == i); // already ran by the time detail::submit() returned
    }

    std::vector<int> expected(10);
    std::iota(expected.begin(), expected.end(), 0);
    REQUIRE(order == expected);
}
