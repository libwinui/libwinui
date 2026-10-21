// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Win32NativeHost.hpp"

#include "Win32String.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/ui/hosting/NativeControl.hpp"

#include <optional>
#include <windows.h>

#include <cmath>

namespace ui::win32
{

int Win32NativeHost::to_px(float dips) const
{
    return static_cast<int>(std::lround(dips * static_cast<float>(dpi_) / 96.0f));
}

void Win32NativeHost::ensure_created(VisualId id, const NativeControlDesc& desc)
{
    if (controls_.contains(id))
    {
        return;
    }
    // WS_CHILD and WS_VISIBLE are always set: this host only creates child controls.
    HWND hwnd = CreateWindowExW(static_cast<DWORD>(desc.ex_style),
                                widen(desc.class_name).c_str(),
                                widen(desc.initial_text).c_str(),
                                static_cast<DWORD>(desc.style) | WS_CHILD | WS_VISIBLE,
                                0,
                                0,
                                0,
                                0,
                                parent_,
                                nullptr,
                                GetModuleHandleW(nullptr),
                                nullptr);
    if (hwnd)
    {
        controls_.emplace(id, hwnd);
    }
}

void Win32NativeHost::set_bounds(VisualId id, const Rect& bounds)
{
    auto it = controls_.find(id);
    if (it == controls_.end())
    {
        return;
    }
    MoveWindow(it->second, to_px(bounds.x), to_px(bounds.y), to_px(bounds.width), to_px(bounds.height), TRUE);

    repaint(it->second);
}

void Win32NativeHost::repaint(HWND hwnd)
{
    // Now: while an animation runs the host window is invalidated after every message, so the
    // child's synthesized WM_PAINT can starve and a scrolled control keeps stale pixels. The frame
    // too: a client edge and a multi-line edit's scroll bars are non-client, and only hovering
    // would redraw them otherwise.
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void Win32NativeHost::set_visible(VisualId id, bool visible)
{
    auto it = controls_.find(id);
    if (it == controls_.end())
    {
        return;
    }
    ShowWindow(it->second, visible ? SW_SHOW : SW_HIDE);
}

void Win32NativeHost::apply_region(HWND hwnd, int origin_x_px, int origin_y_px, const std::optional<Rect>& visible, bool redraw) const
{
    if (!visible)
    {
        SetWindowRgn(hwnd, nullptr, redraw ? TRUE : FALSE);
        return;
    }
    // A window region is in the window's own coordinates. Each edge is rounded the way
    // set_bounds() rounds the window's, so the region meets the viewport edge exactly.
    HRGN region = CreateRectRgn(to_px(visible->x) - origin_x_px,
                                to_px(visible->y) - origin_y_px,
                                to_px(visible->x + visible->width) - origin_x_px,
                                to_px(visible->y + visible->height) - origin_y_px);
    if (region && !SetWindowRgn(hwnd, region, redraw ? TRUE : FALSE))
    {
        DeleteObject(region); // the system owns it only once SetWindowRgn succeeds
    }
}

void Win32NativeHost::set_clip(VisualId id, const std::optional<Rect>& visible)
{
    auto it = controls_.find(id);
    if (it == controls_.end())
    {
        return;
    }
    RECT placed{};
    GetWindowRect(it->second, &placed);
    MapWindowPoints(HWND_DESKTOP, parent_, reinterpret_cast<POINT*>(&placed), 2);
    apply_region(it->second, placed.left, placed.top, visible, false);
    repaint(it->second);
}

void Win32NativeHost::place(VisualId id, const Rect& bounds, const std::optional<Rect>& visible)
{
    auto it = controls_.find(id);
    if (it == controls_.end())
    {
        return;
    }
    // The region first, without its own repaint, so set_bounds()'s synchronous repaint already
    // paints inside it and a control scrolling out never flashes its old region.
    apply_region(it->second, to_px(bounds.x), to_px(bounds.y), visible, false);
    set_bounds(id, bounds);
}

Win32NativeHost::~Win32NativeHost()
{
    for (const auto& [hwnd, owner] : owners_)
    {
        if (owner != nullptr)
        {
            notify_host_lost(*owner);
        }
    }
}

void Win32NativeHost::destroy(VisualId id)
{
    auto it = controls_.find(id);
    if (it == controls_.end())
    {
        return;
    }
    owners_.erase(it->second);
    DestroyWindow(it->second);
    controls_.erase(it);
}

void Win32NativeHost::set_owner(VisualId id, NativeControl* owner)
{
    auto it = controls_.find(id);
    if (it == controls_.end())
    {
        return; // CreateWindowExW failed; nothing to route to
    }
    owners_[it->second] = owner;
}

NativeControl* Win32NativeHost::owner_for(HWND hwnd) const
{
    auto it = owners_.find(hwnd);
    return it == owners_.end() ? nullptr : it->second;
}

NativeHandle Win32NativeHost::native_handle(VisualId id) const
{
    auto it = controls_.find(id);
    return it == controls_.end() ? nullptr : it->second;
}

} // namespace ui::win32

#endif // _WIN32
