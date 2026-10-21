// SPDX-License-Identifier: MIT
#include "winui/ui/Coroutine.hpp"

#include "Resumption.hpp"
#include "winui/core/Scheduler.hpp"
#include "winui/ui/WindowRef.hpp"

#include <coroutine>
#include <utility>

namespace ui
{

void UiThreadHop::await_suspend(std::coroutine_handle<> coroutine) const
{
    // The coroutine may be resumed, finish and free this awaiter before post() returns, or be
    // destroyed inside it: nothing of *this is touched after the call. Through the WindowRef, so a
    // destroyed Window drops it rather than being called. A copy, as the frame holding window_ may
    // be gone while post() still runs.
    const WindowRef target = window_;
    (void)target.post(detail::Resumption{coroutine});
}

void BackgroundHop::await_suspend(std::coroutine_handle<> coroutine) const
{
    // Scheduler runs every job it accepts, so a plain handle is enough here.
    (void)detail::submit(*scheduler_, [coroutine] { coroutine.resume(); });
}

Scheduler& background_scheduler()
{
    // Leaked on purpose: joining workers from a static destructor at exit can deadlock (a DLL
    // under the loader lock), and a coroutine still waiting then has nothing to come back to.
    static Scheduler* const scheduler = new Scheduler{};
    return *scheduler;
}

} // namespace ui
