// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Scheduler.hpp"
#include "winui/ui/WindowRef.hpp"

#include <coroutine>
#include <exception>

namespace ui
{

class Window;

// ---------------------------------------------------------------------------------------
// FireAndForget: the coroutine return type for UI work nobody waits on.
// ---------------------------------------------------------------------------------------
/// The return type of a coroutine that is started and not waited on, such as a click handler that
/// loads something on a worker and shows it when it is done. Write the work top to bottom and hop
/// between threads with `co_await ui::on_background()` and `co_await ui::on_ui_thread(home)`.
///
/// The coroutine runs on the calling thread up to its first co_await, then wherever the awaited
/// hop resumes it. Its frame frees itself when it finishes, or when a hop drops it (see
/// on_ui_thread()). An exception that escapes it calls std::terminate(), since nothing is left
/// to catch it, so catch exceptions inside the coroutine. The handle it returns holds nothing.
///
/// Example:
/// @code
/// // `status` is app state that outlives the window, shown by a `ui::bind(status)`.
/// ui::FireAndForget load(ui::Window& window, ui::Property<std::string>& status, std::string path)
/// {
///     status = "Loading...";
///     const ui::WindowRef home = window.ref();
///     co_await ui::on_background(); // from here on a worker thread: no tree or property access
///     std::string text = read_file(path);
///     co_await ui::on_ui_thread(home); // back on the window's thread, inside tick()
///     status = std::move(text);
/// }
/// @endcode
class FireAndForget
{
public:
    /// What the compiler needs to make a coroutine of this type.
    struct promise_type
    {
        /// The (empty) handle the call returns.
        FireAndForget get_return_object() const noexcept
        {
            return {};
        }
        /// Runs at once, on the calling thread.
        std::suspend_never initial_suspend() const noexcept
        {
            return {};
        }
        /// The frame frees itself at the end.
        std::suspend_never final_suspend() const noexcept
        {
            return {};
        }
        /// Nothing to hand back.
        void return_void() const noexcept
        {
        }
        /// An exception escaping the coroutine ends the program; see the class comment.
        [[noreturn]] void unhandled_exception() const noexcept
        {
            std::terminate();
        }
    };
};

/// The awaitable on_ui_thread() returns: posts the resumption to the window through WindowRef.
/// Only ever awaited, never named.
class UiThreadHop
{
public:
    /// That Window, while it exists.
    explicit UiThreadHop(WindowRef window) noexcept : window_(window)
    {
    }
    /// Never ready: the coroutine always suspends.
    [[nodiscard]] bool await_ready() const noexcept
    {
        return false; // always through post(), also from the UI thread itself
    }
    /// Hands `coroutine` over to be resumed there.
    void await_suspend(std::coroutine_handle<> coroutine) const;
    /// Nothing to return.
    void await_resume() const noexcept
    {
    }

private:
    WindowRef window_;
};

/// The awaitable on_background() returns: submits the resumption to a scheduler. Only ever
/// awaited, never named.
class BackgroundHop
{
public:
    /// Resumes on one of `scheduler`'s workers.
    explicit BackgroundHop(Scheduler& scheduler) noexcept : scheduler_(&scheduler)
    {
    }
    /// Never ready: the coroutine always suspends.
    [[nodiscard]] bool await_ready() const noexcept
    {
        return false;
    }
    /// Hands `coroutine` over to be resumed there.
    void await_suspend(std::coroutine_handle<> coroutine) const;
    /// Nothing to return.
    void await_resume() const noexcept
    {
    }

private:
    Scheduler* scheduler_;
};

/// `co_await ui::on_ui_thread(home)` resumes the coroutine on the UI thread of the Window that
/// `home` was taken from. Take `home` with Window::ref() on the UI thread, before the work starts.
/// The resumption goes through Window::post(), so it runs inside the next tick(), ahead of
/// on_tick(). It never resumes at once, even when the coroutine is already on the UI thread.
/// Callable from any thread, also after the Window was destroyed, and it never reaches a new
/// Window built at the old one's address.
///
/// When the window is closed or destroyed before that tick, the work is dropped and the coroutine
/// never resumes. Its frame is destroyed instead, which runs the destructors of its locals, so
/// nothing leaks. The frame is destroyed on the thread that dropped the work:
/// - the awaiting thread, when the window was already closed or destroyed
/// - the UI thread, when the window closes while the work is queued
/// - the destroying thread, when the Window is destroyed while the work is queued
///
/// A coroutine that has to clean up after itself does it in a destructor, such as an RAII guard,
/// since code after the co_await may never run. Anything that awaits this coroutine in turn never
/// resumes either.
///
/// Example:
/// @code
/// ui::FireAndForget refresh(ui::Window& window, ui::Property<std::string>& status)
/// {
///     const ui::WindowRef home = window.ref(); // still on the UI thread
///     co_await ui::on_background();
///     std::string text = fetch_status();
///     co_await ui::on_ui_thread(home);
///     status = std::move(text); // the property's observers run on the UI thread
/// }
/// @endcode
[[nodiscard]] inline UiThreadHop on_ui_thread(WindowRef window) noexcept
{
    return UiThreadHop{window};
}

/// The Scheduler on_background() uses when given none. It has one worker per hardware thread, is
/// created on first use and is never destroyed. Its workers end with the process, so a coroutine
/// waiting on it at exit is not resumed. A coroutine that hops back to a destroyed Window is
/// dropped (see on_ui_thread()).
[[nodiscard]] Scheduler& background_scheduler();

/// `co_await ui::on_background(scheduler)` resumes the coroutine on one of `scheduler`'s
/// workers. Code running there must not touch the UI tree. In Scheduler::Inline mode it resumes at
/// once on the calling thread. Every job queued on a Scheduler runs, also when the scheduler is
/// destroyed, so a coroutine waiting here is always resumed.
///
/// Example:
/// @code
/// ui::Scheduler pool{2};
/// auto work = [&]() -> ui::FireAndForget {
///     co_await ui::on_background(pool);
///     crunch();
/// };
/// work();
/// @endcode
[[nodiscard]] inline BackgroundHop on_background(Scheduler& scheduler) noexcept
{
    return BackgroundHop{scheduler};
}

/// `co_await ui::on_background()` resumes the coroutine on a worker of background_scheduler().
/// It is meant for work that keeps a CPU busy. A long blocking wait, such as on a socket or a
/// sleep, holds a worker the whole time, so give that its own Scheduler or thread.
///
/// The default pool is never joined, so work can still run on it after main() returned and the
/// Window it hops back to is gone. on_ui_thread() then drops the coroutine. Anything else the
/// coroutine refers to has to outlive it, or must not be touched after that hop.
[[nodiscard]] inline BackgroundHop on_background()
{
    return BackgroundHop{background_scheduler()};
}

} // namespace ui
