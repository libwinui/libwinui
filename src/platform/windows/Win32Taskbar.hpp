// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../../ui/WindowHost.hpp"

#include <shobjidl.h>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

namespace ui::win32
{

// Calls about the button fail until the shell sends "TaskbarButtonCreated" (again after an
// Explorer restart), so every value is kept and replayed from button_created().
class Win32Taskbar
{
public:
    /// First command id a thumbnail button's WM_COMMAND carries; button `i` is FirstButtonId + i.
    static constexpr UINT FirstButtonId = 0x4C00;

    explicit Win32Taskbar(HWND hwnd);
    ~Win32Taskbar();
    Win32Taskbar(const Win32Taskbar&)            = delete;
    Win32Taskbar& operator=(const Win32Taskbar&) = delete;

    /// The "TaskbarButtonCreated" message arrived: (re)connect and replay everything.
    void button_created();

    void set_progress(const TaskbarProgress& progress);
    /// Takes ownership of `icon` (nullptr: no overlay).
    void set_overlay(HICON icon, std::wstring_view description);
    /// Takes ownership of every icon in `icons`, one per button (entries may be nullptr).
    void set_thumbnail_buttons(const std::vector<ThumbnailButton>& buttons, std::vector<HICON> icons);
    /// Works without a button: it tells the shell a full-screen window is not a stuck one.
    void mark_fullscreen(bool fullscreen);

private:
    bool connect();
    void push_progress();
    void push_overlay();
    void push_thumbnail_buttons();
    void destroy_thumbnail_icons();

    HWND                                  hwnd_;
    Microsoft::WRL::ComPtr<ITaskbarList3> list_;
    bool                                  com_owned_     = false;
    bool                                  connect_tried_ = false;
    bool                                  button_ready_  = false;

    TaskbarProgress progress_{};
    HICON           overlay_ = nullptr;
    std::wstring    overlay_description_;

    struct Button
    {
        HICON        icon = nullptr;
        std::wstring tool_tip;
        bool         enabled = true;
        bool         visible = true;
    };
    std::vector<Button> buttons_;
    bool                buttons_added_ = false; // ThumbBarAddButtons done for the current button
    bool                fullscreen_    = false;
};

} // namespace ui::win32

#endif // _WIN32
