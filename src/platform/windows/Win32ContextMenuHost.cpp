// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "Win32ContextMenuHost.hpp"

#include "DarkMode.hpp"
#include "Win32String.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/ContextMenuHost.hpp"

#include <optional>
#include <string>
#include <vector>
#include <windows.h>

#include <cmath>
#include <cstddef>

namespace ui::win32
{
namespace
{

// '&' marks a mnemonic in menu text; a literal one (a suggestion like "R&D") is "&&".
[[nodiscard]] std::wstring escape_mnemonics(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t ch : text)
    {
        out += ch;
        if (ch == L'&')
        {
            out += L'&';
        }
    }
    return out;
}

} // namespace

std::optional<std::size_t> Win32ContextMenuHost::show(Point position, const std::vector<ContextMenuItem>& items)
{
    if (items.empty())
    {
        return std::nullopt;
    }
    // Before the HMENU exists: uxtheme caches the menu theme, so the app mode only reaches menus
    // opened after it. Without immersive app mode the menu stays light.
    set_app_dark_mode(dark_);
    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const ContextMenuItem& item = items[i];
        if (item.is_separator)
        {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        // Command ids are index + 1: TrackPopupMenuEx's TPM_RETURNCMD reports 0 for "dismissed".
        const std::wstring text = escape_mnemonics(widen(item.text));
        AppendMenuW(menu, MF_STRING | (item.enabled ? 0u : MF_GRAYED), static_cast<UINT_PTR>(i + 1), text.c_str());
    }

    const float scale = static_cast<float>(dpi_) / 96.0f;
    POINT pt{static_cast<LONG>(std::lround(position.x * scale)), static_cast<LONG>(std::lround(position.y * scale))};
    ClientToScreen(hwnd_, &pt);

    // TPM_RIGHTBUTTON: the right-click that opened the menu is often still being released.
    const UINT flags = TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY;
    const BOOL cmd   = TrackPopupMenuEx(menu, flags, pt.x, pt.y, hwnd_, nullptr);
    DestroyMenu(menu);

    if (cmd <= 0 || static_cast<std::size_t>(cmd) > items.size())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(cmd) - 1;
}

} // namespace ui::win32

#endif // _WIN32
