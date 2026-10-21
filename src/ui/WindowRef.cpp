// SPDX-License-Identifier: MIT
#include "winui/ui/WindowRef.hpp"

#include "WindowRegistry.hpp"
#include "winui/ui/Window.hpp"

#include <mutex>
#include <unordered_map>
#include <utility>

#include <cstdint>

namespace ui
{

namespace
{
// Every live Window by address, with the serial it was given. Posting through a WindowRef looks
// the window up and queues its work under this lock, and a Window's destructor unregisters under it
// before anything of the Window is torn down, so the two cannot interleave.
struct WindowRegistry
{
    std::mutex                                       mutex;
    std::unordered_map<const Window*, std::uint64_t> live;
    std::uint64_t                                    last_serial = 0;
};

WindowRegistry& live_windows()
{
    // Leaked on purpose: a worker of the leaked background_scheduler() may post after main()
    // returned, while static destructors run.
    static WindowRegistry* const instance = new WindowRegistry{};
    return *instance;
}
} // namespace

namespace detail
{

std::uint64_t register_window(const Window* window)
{
    WindowRegistry&       r = live_windows();
    const std::lock_guard lock{r.mutex};
    const std::uint64_t   serial = ++r.last_serial;
    r.live[window]               = serial;
    return serial;
}

void unregister_window(const Window* window)
{
    WindowRegistry&       r = live_windows();
    const std::lock_guard lock{r.mutex};
    r.live.erase(window);
}

} // namespace detail

Window* WindowRef::get() const
{
    WindowRegistry&       r = live_windows();
    const std::lock_guard lock{r.mutex};
    const auto            it = r.live.find(window_);
    if (window_ == nullptr || it == r.live.end() || (serial_ != 0 && it->second != serial_))
    {
        return nullptr;
    }
    return const_cast<Window*>(window_); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

bool WindowRef::post(Callback<void()> work) const
{
    // `work` is a parameter, so a refused one is destroyed after the lock below is released.
    WindowRegistry&       r = live_windows();
    const std::lock_guard lock{r.mutex};
    const auto            it = r.live.find(window_);
    if (window_ == nullptr || it == r.live.end() || (serial_ != 0 && it->second != serial_))
    {
        return false;
    }
    // Alive and staying so while the lock is held: its destructor waits for it.
    return const_cast<Window*>(window_)->enqueue(work); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

} // namespace ui
