// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

class Scheduler;

namespace detail
{
// A job queued on a Scheduler. Move-only; wait() blocks until it has run. Dropping one does not
// cancel the job, and an exception the job throws is swallowed, so jobs must not throw. Internal:
// the library splits measuring into these, and background work reaches a pool through
// ui::on_background().
class Job
{
public:
    Job() = default;

    // Blocks until the job has run (it already has in Scheduler::Inline mode). No-op on an empty
    // or moved-from Job.
    void wait()
    {
        if (future_.valid())
        {
            future_.wait();
        }
    }

private:
    friend Job submit(Scheduler& scheduler, Callback<void()> work);
    explicit Job(std::future<void> future) : future_(std::move(future))
    {
    }
    std::future<void> future_;
};

// Queues `work` for the next free worker (FIFO). In Inline mode `work` has already run when this
// returns. Safe to call from any thread.
[[nodiscard]] Job submit(Scheduler& scheduler, Callback<void()> work);
} // namespace detail

/// A pool of worker threads for background work and parallel layout.
///
/// Construct one to give a piece of work its own threads, and hand it to `ui::on_background(pool)`
/// or to `ParallelLayout::scheduler`. Work on a pool must not touch the UI tree. The Inline mode
/// runs everything synchronously on the calling thread, for deterministic tests. The destructor
/// runs every job still queued, then joins the workers.
///
/// Example:
/// @code
/// // Two threads of its own, so an index never waits behind other background work.
/// ui::Scheduler indexers{2};
///
/// // `status` is app state that outlives the window, shown by a Text bound to it.
/// ui::FireAndForget index(ui::Scheduler& indexers, ui::Window& window, ui::Property<std::string>& status,
///                         std::filesystem::path folder)
/// {
///     const ui::WindowRef home = window.ref();
///     co_await ui::on_background(indexers); // on one of the pool's threads
///     const auto files = std::distance(std::filesystem::recursive_directory_iterator{folder}, {});
///     co_await ui::on_ui_thread(home);
///     status = std::format("{} files", files);
/// }
/// @endcode
class Scheduler
{
public:
    /// Tag selecting the deterministic mode for tests: work runs synchronously, in the order it
    /// was queued, on the calling thread, with no worker threads and no interleaving. Construct
    /// with `Scheduler{Scheduler::Inline{}}`.
    struct Inline
    {
    };

    /// Real worker threads. thread_count == 0 auto-detects: max(1, hardware_concurrency()).
    explicit Scheduler(std::size_t thread_count = 0);
    /// The deterministic mode (see Inline): no threads.
    explicit Scheduler(Inline);
    /// Runs every job still queued, then joins the worker threads.
    ~Scheduler();

    /// Not copyable: it owns threads.
    Scheduler(const Scheduler&) = delete;
    /// Not copyable: it owns threads.
    Scheduler& operator=(const Scheduler&) = delete;

    /// Real worker thread count (0 in inline mode).
    [[nodiscard]] std::size_t thread_count() const
    {
        return threads_.size();
    }
    /// How many jobs can run at once: thread_count(), and 1 in inline mode. Parallel layout
    /// splits its measuring into this many chunks.
    [[nodiscard]] std::size_t worker_count() const
    {
        return threads_.empty() ? 1 : threads_.size();
    }

private:
    friend detail::Job detail::submit(Scheduler& scheduler, Callback<void()> work);
    void               worker_loop();

    // Declared last so the threads are joined before the queue they use is destroyed (members
    // are destroyed in reverse declaration order).
    std::mutex                   mutex_;
    std::condition_variable      cv_;
    std::queue<Callback<void()>> queue_;
    bool                         stop_ = false;
    std::vector<std::jthread>    threads_; // empty in inline mode
};

} // namespace ui
