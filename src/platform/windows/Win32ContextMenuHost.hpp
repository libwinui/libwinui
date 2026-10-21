// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "winui/core/NativeHandle.hpp"
#include "winui/render/ContextMenuHost.hpp"

namespace ui::win32
{

/// The Win32 ContextMenuHost: shows a native USER32 popup menu at a point in the window and
/// returns what the user picked.
///
/// ui::run() creates one per window and installs it with Window::set_context_menu_host();
/// construct one yourself only in a custom host. show() is modal (the menu's own loop) and
/// returns once the menu closes; the window keeps repainting meanwhile. The menu uses the system
/// menu colors — only dark or light, via set_dark() — never the Theme's.
///
/// Example:
/// @code
/// ui::win32::Win32ContextMenuHost menu{hwnd, GetDpiForWindow(hwnd)};
/// testing::seams(window).set_context_menu_host(&menu); // non-owning; `menu` must outlive the window's use
///
/// menu.set_dark(app_is_dark); // re-pushed before every show()
/// const std::optional<std::size_t> picked = menu.show(ui::Point{40, 20}, {
///     ui::ContextMenuItem{"Cut"},
///     ui::ContextMenuItem{"Copy"},
///     ui::ContextMenuItem::separator(),
///     ui::ContextMenuItem{"Paste", /*enabled=*/false},
/// });
/// if (picked == 1) { /* Copy */ }
/// @endcode
class Win32ContextMenuHost final : public ContextMenuHost
{
public:
    /// A host for menus over `hwnd` (non-owning), whose current DPI is `dpi`.
    Win32ContextMenuHost(NativeHandle hwnd, unsigned dpi) : hwnd_(hwnd), dpi_(dpi)
    {
    }

    /// WM_DPICHANGED — show()'s DIP→pixel conversion must track the window's current DPI.
    void set_dpi(unsigned dpi)
    {
        dpi_ = dpi;
    }

    /// Whether the *next* menu should be drawn dark. Cheap to set repeatedly (see
    /// set_app_dark_mode()); the caller re-pushes the window's current theme before every
    /// show(), since Window::pin_theme() can flip it at any time.
    void set_dark(bool dark)
    {
        dark_ = dark;
    }

    /// Shows `items` with the menu's top-left at `position` (window-client DIPs) and blocks until
    /// it closes. Returns the index into `items` of the entry picked, or nullopt when dismissed.
    [[nodiscard]] std::optional<std::size_t> show(Point position, const std::vector<ContextMenuItem>& items) override;

private:
    NativeHandle hwnd_ = nullptr;
    unsigned     dpi_  = 96;
    bool         dark_ = false;
};

} // namespace ui::win32

#endif // _WIN32
