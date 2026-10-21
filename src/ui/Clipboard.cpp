// SPDX-License-Identifier: MIT
#include "winui/ui/Clipboard.hpp"

#include "../platform/Backend.hpp"

namespace ui
{

namespace
{
// UI-thread state: the clipboard is used from the UI thread only.
Clipboard* g_installed_clipboard = nullptr;
} // namespace

Clipboard& clipboard()
{
    return g_installed_clipboard != nullptr ? *g_installed_clipboard : detail::platform_clipboard();
}

void detail::install_clipboard(Clipboard* clipboard)
{
    g_installed_clipboard = clipboard;
}

Clipboard* detail::installed_clipboard()
{
    return g_installed_clipboard;
}

} // namespace ui
