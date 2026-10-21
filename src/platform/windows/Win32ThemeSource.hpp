// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "winui/ui/themes/ThemeSource.hpp"

namespace ui::win32
{

/// Reads AppsUseLightTheme (registry — no WinRT equivalent for classic desktop apps),
/// the accent color and its tonal ramp (Windows.UI.ViewManagement.UISettings, falling back to
/// the registry/DwmGetColorizationColor only if WinRT itself is unreachable), and
/// SPI_GETHIGHCONTRAST + GetSysColor (high-contrast palette) live, each call. No caching:
/// the Win32 host calls snapshot() only on WM_CREATE and the theme-change messages, so a
/// per-call query is cheap enough not to warrant one.
///
/// Example:
/// @code
/// // Pre-select a dark-mode toggle to match the OS before ui::run() applies the theme.
/// const ui::OsThemeSnapshot os = ui::win32::Win32ThemeSource{}.snapshot();
/// ui::Property<bool> dark{os.dark};
///
/// ui::Window window;
/// window.pin_theme(ui::resolve_theme(ui::ThemeRequest{}, os));
/// window.set_content(ui::ToggleSwitch{.header = "Dark mode", .checked = ui::bind(dark)});
/// return ui::run(window);
/// @endcode
class Win32ThemeSource final
{
public:
    /// The OS theme state right now: app mode, accent and its ramp, and the contrast theme.
    /// Queries the OS on every call; usable before any window exists.
    [[nodiscard]] OsThemeSnapshot snapshot() const;
};

} // namespace ui::win32

#endif // _WIN32
