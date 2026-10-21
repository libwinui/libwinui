// SPDX-License-Identifier: MIT
// Pen and touch data for the mouse messages Windows makes of a pen or a finger.
//
// The backend keeps taking its input from WM_*BUTTON* and WM_MOUSEMOVE: capture, hover, the wheel,
// non-client hit testing, drag and drop and every popup already run on them, and a window that
// left WM_POINTER* to DefWindowProc gets those messages for a pen and a finger too. What the
// mouse messages lose is said here: the WM_POINTER* message before each one is read for the
// pen's pressure, tilt and buttons and then still handed to DefWindowProc, and the mouse message
// itself is told apart by the signature Windows puts in its GetMessageExtraInfo().
#pragma once
#ifdef _WIN32

#include "winui/core/Input.hpp"

#include <algorithm>
#include <optional>

#include <cstdint>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ui::win32
{

// A mouse message made from pen or touch input carries 0xFF5157xx in its extra info; bit 0x80
// of the low byte is set for touch.
inline constexpr std::uintptr_t PenTouchSignatureMask = 0xFFFFFF00;
inline constexpr std::uintptr_t PenTouchSignature     = 0xFF515700;
inline constexpr std::uintptr_t TouchSignatureBit     = 0x80;

// The pen and touch data of one pointer frame, what a PointerEvent gets on top of the mouse
// message's position and button.
struct PointerSample
{
    PointerDeviceType device        = PointerDeviceType::Mouse;
    float             pressure      = 0.5f;
    float             tilt_x        = 0.0f;
    float             tilt_y        = 0.0f;
    bool              eraser        = false;
    bool              barrel_button = false;
};

// Which device a mouse message came from, by its GetMessageExtraInfo() value.
[[nodiscard]] constexpr PointerDeviceType device_from_extra_info(std::uintptr_t extra)
{
    if ((extra & PenTouchSignatureMask) != PenTouchSignature)
    {
        return PointerDeviceType::Mouse;
    }
    return (extra & TouchSignatureBit) != 0 ? PointerDeviceType::Touch : PointerDeviceType::Pen;
}

// Windows reports pressure as 0..1024; a device that reports none keeps the mouse's 0.5.
[[nodiscard]] constexpr float pressure_from_raw(UINT32 raw)
{
    return std::clamp(static_cast<float>(raw) / 1024.0f, 0.0f, 1.0f);
}

[[nodiscard]] inline PointerSample pen_sample(const POINTER_PEN_INFO& info)
{
    PointerSample s;
    s.device = PointerDeviceType::Pen;
    if ((info.penMask & PEN_MASK_PRESSURE) != 0)
    {
        s.pressure = pressure_from_raw(info.pressure);
    }
    if ((info.penMask & PEN_MASK_TILT_X) != 0)
    {
        s.tilt_x = static_cast<float>(std::clamp<INT32>(info.tiltX, -90, 90));
    }
    if ((info.penMask & PEN_MASK_TILT_Y) != 0)
    {
        s.tilt_y = static_cast<float>(std::clamp<INT32>(info.tiltY, -90, 90));
    }
    s.eraser        = (info.penFlags & (PEN_FLAG_INVERTED | PEN_FLAG_ERASER)) != 0;
    s.barrel_button = (info.penFlags & PEN_FLAG_BARREL) != 0;
    return s;
}

[[nodiscard]] inline PointerSample touch_sample(const POINTER_TOUCH_INFO& info)
{
    PointerSample s;
    s.device = PointerDeviceType::Touch;
    if ((info.touchMask & TOUCH_MASK_PRESSURE) != 0)
    {
        s.pressure = pressure_from_raw(info.pressure);
    }
    return s;
}

// The pen or touch data of the pointer a WM_POINTER* message is about; nothing for a mouse
// pointer or one the system no longer knows.
[[nodiscard]] inline std::optional<PointerSample> query_pointer_sample(UINT32 pointer_id)
{
    POINTER_INPUT_TYPE type = PT_POINTER;
    if (!GetPointerType(pointer_id, &type))
    {
        return std::nullopt;
    }
    if (type == PT_PEN)
    {
        POINTER_PEN_INFO info{};
        if (GetPointerPenInfo(pointer_id, &info))
        {
            return pen_sample(info);
        }
    }
    else if (type == PT_TOUCH)
    {
        POINTER_TOUCH_INFO info{};
        if (GetPointerTouchInfo(pointer_id, &info))
        {
            return touch_sample(info);
        }
    }
    return std::nullopt;
}

// Fills `e`'s device fields for a mouse message whose extra info was `extra`: the last pen or
// touch frame when it came from that device, the mouse's defaults otherwise.
inline void apply_pointer_source(PointerEvent& e, std::uintptr_t extra, const std::optional<PointerSample>& last)
{
    PointerSample s;
    s.device = device_from_extra_info(extra);
    if (s.device != PointerDeviceType::Mouse && last && last->device == s.device)
    {
        s = *last;
    }
    e.device        = s.device;
    e.pressure      = s.pressure;
    e.tilt_x        = s.tilt_x;
    e.tilt_y        = s.tilt_y;
    e.eraser        = s.eraser;
    e.barrel_button = s.barrel_button;
}

} // namespace ui::win32

#endif
