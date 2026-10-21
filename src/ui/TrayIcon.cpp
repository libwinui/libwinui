// SPDX-License-Identifier: MIT
// The portable half of the tray icon: the flyout's WindowOptions preset and its placement. The
// icon itself is the backend's (src/platform/windows/Tray.cpp).
#include "winui/ui/TrayIcon.hpp"

#include "winui/core/Geometry.hpp"
#include "winui/ui/WindowOptions.hpp"
#include "winui/ui/WindowStyle.hpp"

#include <algorithm>
#include <utility>

#include <cmath>

namespace ui
{

WindowOptions tray_flyout_options(TrayFlyoutOptions options)
{
    WindowOptions window;
    window.title    = std::move(options.title);
    window.size     = {options.width, 200.0f}; // the height follows the content at every show
    window.theme    = options.theme;
    window.backdrop = options.backdrop;
    // A bare popup with Windows 11 corners and rim and no shadow, like the shell's own flyouts.
    // Asking for a shadow makes it Borderless: an overlapped window DWM shadows and rounds itself.
    window.style.frame               = options.shadow ? Frame::Borderless : Frame::None;
    window.style.round_popup_corners = true;
    window.style.resizable           = false;
    window.style.minimize_button     = false;
    window.style.maximize_button     = false;
    window.style.always_on_top       = true;
    window.style.tool_window         = true;
    // Without the system menu: DWM would draw a tool window's close button over the backdrop.
    window.style.caption_buttons = false;
    window.spell_check           = std::move(options.spell_check);
    window.visible               = false;
    window.close_hides           = true; // Alt+F4 and Window::close() dismiss it
    return window;
}

Point place_tray_flyout(const Rect& anchor, const Rect& work_area, Size size, float margin)
{
    const float work_right    = work_area.x + work_area.width;
    const float work_bottom   = work_area.y + work_area.height;
    const float anchor_right  = anchor.x + anchor.width;
    const float anchor_bottom = anchor.y + anchor.height;

    // Along the taskbar: centered on the icon, kept clear of both corners. One too big for the
    // work area sticks to the leading corner.
    const auto along = [margin](float center, float extent, float low, float high)
    { return std::max(low + margin, std::min(center - extent * 0.5f, high - margin - extent)); };
    const float center_x = anchor.x + anchor.width * 0.5f;
    const float center_y = anchor.y + anchor.height * 0.5f;

    if (anchor_bottom <= work_area.y)
    {
        return {along(center_x, size.width, work_area.x, work_right), work_area.y + margin};
    }
    if (anchor_right <= work_area.x)
    {
        return {work_area.x + margin, along(center_y, size.height, work_area.y, work_bottom)};
    }
    if (anchor.x >= work_right)
    {
        return {work_right - margin - size.width, along(center_y, size.height, work_area.y, work_bottom)};
    }
    // A bottom taskbar, or an anchor inside the work area (the overflow flyout, a shown auto-hide
    // taskbar): above the anchor, not over it.
    const float bottom = std::min(work_bottom, anchor.y);
    return {along(center_x, size.width, work_area.x, work_right),
            std::max(work_area.y + margin, bottom - margin - size.height)};
}

} // namespace ui
