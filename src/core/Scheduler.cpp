// SPDX-License-Identifier: MIT
#include "winui/core/Scheduler.hpp"

#include <algorithm>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <cstddef>

namespace ui
{

Scheduler::Scheduler(std::size_t thread_count)
{
    if (thread_count == 0)
    {
        thread_count = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    }

    threads_.reserve(thread_count);
    try
    {
        for (std::size_t i = 0; i < thread_count; ++i)
        {
            threads_.emplace_back(&Scheduler::worker_loop, this);
        }
    } catch (...)
    {
        // The workers already started wait on stop_, and unwinding joins them.
        {
            const std::lock_guard lock{mutex_};
            stop_ = true;
        }
        cv_.notify_all();
        throw;
    }
}

Scheduler::Scheduler(Inline)
{
}

Scheduler::~Scheduler()
{
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    // threads_ (std::jthread) join automatically as it is destroyed, below.
}

namespace detail
{
Job submit(Scheduler& scheduler, Callback<void()> work)
{
    std::packaged_task<void()> task{std::move(work)};
    std::future<void>          future = task.get_future();

    if (scheduler.threads_.empty())
    {
        // Deterministic/inline mode: run now, on the calling thread.
        task();
        return Job{std::move(future)};
    }

    {
        std::lock_guard lock(scheduler.mutex_);
        scheduler.queue_.emplace(std::move(task));
    }
    scheduler.cv_.notify_one();
    return Job{std::move(future)};
}
} // namespace detail

void Scheduler::worker_loop()
{
    for (;;)
    {
        Callback<void()> work;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty())
            {
                return;
            }
            work = std::move(queue_.front());
            queue_.pop();
        }
        work();
    }
}

} // namespace ui
