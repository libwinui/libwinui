// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"

#include <cstdint>

namespace ui
{

class Window;
class UiThreadHop;

/// A weak handle to a Window, for a worker that may outlive it. Take it with Window::ref() while
/// the Window is known to exist (on the UI thread, before the work starts); post() through it is
/// safe from any thread at any time, also after the Window was destroyed: the work is dropped then.
///
/// It names one Window, not an address. A new Window built where a destroyed one was (a dialog in a
/// re-created `std::optional<ui::Window>`) is a different window, and work posted through a handle
/// to the old one is dropped instead of reaching the new one. Copyable and cheap: an address and
/// a serial number, compared but never dereferenced. A default-constructed one names no window.
///
/// Example:
/// @code
/// // `preview` lives as long as the window, so the posted work may write it.
/// void load_preview(const std::filesystem::path& path, ui::Property<ui::Bitmap>& preview)
/// {
///     std::thread([home = window.ref(), path, &preview] {
///         ui::Bitmap image = ui::Bitmap::read_png(path);
///         home.post([&preview, image = std::move(image)]() mutable { preview.set(std::move(image)); });
///     }).detach();
/// }
/// @endcode
class WindowRef
{
public:
    /// Names no window: post() drops everything.
    WindowRef() noexcept = default;

    /// Window::post() through the handle: queues `work` for the window's next tick() and returns
    /// true, or, when that window is closed or destroyed, destroys `work` without running it and
    /// returns false. Callable from any thread. Dropped work is destroyed on the calling thread
    /// once no lock is held, so its destructor may post again.
    bool post(Callback<void()> work) const;

    /// The Window while it exists, null once it was destroyed (or for a default-constructed
    /// handle). On the window's own thread only: anywhere else the window may be gone by the time
    /// the pointer is used.
    [[nodiscard]] Window* get() const;

    /// Whether both name the same Window.
    friend bool operator==(const WindowRef&, const WindowRef&) noexcept = default;

private:
    friend class Window;
    friend class UiThreadHop;
    WindowRef(const Window* window, std::uint64_t serial) noexcept : window_(window), serial_(serial)
    {
    }

    const Window* window_ = nullptr;
    std::uint64_t serial_ = 0;
};

} // namespace ui
