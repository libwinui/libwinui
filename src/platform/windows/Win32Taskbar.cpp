// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "Win32Taskbar.hpp"

#include "../../ui/WindowHost.hpp"
#include "Win32String.hpp"

#include <algorithm>
#include <objbase.h> // CoInitializeEx/CoCreateInstance; <windows.h> skips it under WIN32_LEAN_AND_MEAN
#include <string_view>
#include <vector>

#include <cmath>
#include <cwchar>

namespace ui::win32
{

Win32Taskbar::Win32Taskbar(HWND hwnd) : hwnd_(hwnd)
{
}

Win32Taskbar::~Win32Taskbar()
{
    list_.Reset();
    if (overlay_ != nullptr)
    {
        DestroyIcon(overlay_);
    }
    destroy_thumbnail_icons();
    if (com_owned_)
    {
        CoUninitialize();
    }
}

bool Win32Taskbar::connect()
{
    if (!connect_tried_)
    {
        connect_tried_ = true;
        // Normally already in the MTA through Win32ThemeSource; only uninitialize what this started.
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com_owned_       = co == S_OK || co == S_FALSE;
        if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&list_))) &&
            FAILED(list_->HrInit()))
        {
            list_.Reset();
        }
    }
    return list_ != nullptr;
}

void Win32Taskbar::button_created()
{
    if (!connect())
    {
        return;
    }
    button_ready_  = true;
    buttons_added_ = false; // a new button (Explorer restarted) has no toolbar yet
    push_progress();
    push_overlay();
    push_thumbnail_buttons();
    if (fullscreen_)
    {
        list_->MarkFullscreenWindow(hwnd_, TRUE);
    }
}

void Win32Taskbar::set_progress(const TaskbarProgress& progress)
{
    progress_ = progress;
    push_progress();
}

void Win32Taskbar::push_progress()
{
    if (!button_ready_)
    {
        return;
    }
    TBPFLAG flag = TBPF_NOPROGRESS;
    switch (progress_.state)
    {
        case TaskbarProgressState::None:
            flag = TBPF_NOPROGRESS;
            break;
        case TaskbarProgressState::Indeterminate:
            flag = TBPF_INDETERMINATE;
            break;
        case TaskbarProgressState::Normal:
            flag = TBPF_NORMAL;
            break;
        case TaskbarProgressState::Paused:
            flag = TBPF_PAUSED;
            break;
        case TaskbarProgressState::Error:
            flag = TBPF_ERROR;
            break;
    }
    list_->SetProgressState(hwnd_, flag);
    if (flag != TBPF_NOPROGRESS && flag != TBPF_INDETERMINATE)
    {
        // SetProgressValue also switches an indeterminate bar back to normal, so it is only
        // sent for the states that show a value.
        constexpr ULONGLONG Total = 10000;
        const double        value = std::clamp(progress_.value, 0.0, 1.0);
        list_->SetProgressValue(hwnd_, static_cast<ULONGLONG>(std::llround(value * static_cast<double>(Total))), Total);
        list_->SetProgressState(hwnd_, flag);
    }
}

void Win32Taskbar::set_overlay(HICON icon, std::wstring_view description)
{
    if (overlay_ != nullptr)
    {
        DestroyIcon(overlay_);
    }
    overlay_             = icon;
    overlay_description_ = std::wstring{description};
    push_overlay();
}

void Win32Taskbar::push_overlay()
{
    if (button_ready_)
    {
        // The shell copies the icon, so ours can stay owned here for the next replay.
        list_->SetOverlayIcon(hwnd_, overlay_, overlay_ != nullptr ? overlay_description_.c_str() : nullptr);
    }
}

void Win32Taskbar::set_thumbnail_buttons(const std::vector<ThumbnailButton>& buttons, std::vector<HICON> icons)
{
    icons.resize(buttons.size(), nullptr);
    if (buttons_.empty())
    {
        // The set is fixed by the first call (ThumbBarAddButtons cannot be undone), capped at the
        // seven the shell allows.
        buttons_.resize(std::min<std::size_t>(buttons.size(), 7));
    }
    for (std::size_t i = 0; i < icons.size(); ++i)
    {
        if (i >= buttons_.size())
        {
            if (icons[i] != nullptr)
            {
                DestroyIcon(icons[i]); // beyond the fixed set: nowhere to show it
            }
            continue;
        }
        Button& button = buttons_[i];
        if (button.icon != nullptr)
        {
            DestroyIcon(button.icon);
        }
        button.icon     = icons[i];
        button.tool_tip = widen(buttons[i].display_tool_tip());
        button.enabled  = buttons[i].is_enabled();
        button.visible  = buttons[i].visible.initial();
    }
    push_thumbnail_buttons();
}

void Win32Taskbar::push_thumbnail_buttons()
{
    if (!button_ready_ || buttons_.empty())
    {
        return;
    }
    std::vector<THUMBBUTTON> native(buttons_.size());
    for (std::size_t i = 0; i < buttons_.size(); ++i)
    {
        THUMBBUTTON& b = native[i];
        b.dwMask       = THB_ICON | THB_TOOLTIP | THB_FLAGS;
        b.iId          = FirstButtonId + static_cast<UINT>(i);
        b.hIcon        = buttons_[i].icon;
        wcsncpy_s(b.szTip, buttons_[i].tool_tip.c_str(), _TRUNCATE);
        b.dwFlags = static_cast<THUMBBUTTONFLAGS>(
            (buttons_[i].enabled ? THBF_ENABLED : THBF_DISABLED) | (buttons_[i].visible ? 0 : THBF_HIDDEN));
    }
    const UINT count = static_cast<UINT>(native.size());
    if (!buttons_added_)
    {
        buttons_added_ = SUCCEEDED(list_->ThumbBarAddButtons(hwnd_, count, native.data()));
    }
    else
    {
        list_->ThumbBarUpdateButtons(hwnd_, count, native.data());
    }
}

void Win32Taskbar::mark_fullscreen(bool fullscreen)
{
    fullscreen_ = fullscreen;
    if (connect())
    {
        list_->MarkFullscreenWindow(hwnd_, fullscreen ? TRUE : FALSE);
    }
}

void Win32Taskbar::destroy_thumbnail_icons()
{
    for (Button& button : buttons_)
    {
        if (button.icon != nullptr)
        {
            DestroyIcon(button.icon);
            button.icon = nullptr;
        }
    }
}

} // namespace ui::win32

#endif // _WIN32
