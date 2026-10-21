// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "winui/core/Callback.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ui::win32
{

// A hidden message-only window on the UI thread, for the OS services that call back from another
// thread or another process: a toast's click, a second instance's command line. Made by the
// first call, on the thread that makes it, and kept for the life of the process.
class MessageWindow
{
public:
    // The window, made on this thread by the first call; that thread must be the UI thread.
    static MessageWindow& get();

    // The window class, for finding the window of another process by its title.
    [[nodiscard]] static const wchar_t* class_name();

    [[nodiscard]] HWND hwnd() const
    {
        return hwnd_;
    }

    // Runs `work` on the window's thread the next time its loop pumps messages. Callable from any
    // thread; dropped when the window could not be made.
    void post(Callback<void()> work) const;

    // What the window does with WM_COPYDATA; it answers FALSE when unset. Runs on the window's
    // thread while the sender waits, so it should copy the data and post the rest.
    void on_copy_data(Callback<bool(const COPYDATASTRUCT&)> handler);

    MessageWindow(const MessageWindow&)            = delete;
    MessageWindow& operator=(const MessageWindow&) = delete;

private:
    MessageWindow();
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND                                  hwnd_ = nullptr;
    Callback<bool(const COPYDATASTRUCT&)> copy_data_{};
};

} // namespace ui::win32

#endif // _WIN32
