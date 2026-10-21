// SPDX-License-Identifier: MIT
#include "winui/ui/MessageBox.hpp"

#include "../platform/Backend.hpp"

namespace ui
{

namespace
{
// UI-thread state, like the clipboard override (Clipboard.cpp).
MessageBoxHost* g_installed_message_box_host = nullptr;

MessageBoxResult show(const Window* owner, const MessageBoxOptions& options)
{
    if (g_installed_message_box_host != nullptr)
    {
        return g_installed_message_box_host->show(owner, options);
    }
    return detail::platform_message_box(owner, options);
}
} // namespace

MessageBoxResult message_box(const Window& owner, const MessageBoxOptions& options)
{
    return show(&owner, options);
}

MessageBoxResult message_box(const MessageBoxOptions& options)
{
    return show(nullptr, options);
}

void detail::install_message_box_host(MessageBoxHost* host)
{
    g_installed_message_box_host = host;
}

MessageBoxHost* detail::installed_message_box_host()
{
    return g_installed_message_box_host;
}

} // namespace ui
