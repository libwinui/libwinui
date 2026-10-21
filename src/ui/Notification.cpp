// SPDX-License-Identifier: MIT
#include "winui/ui/Notification.hpp"

#include "../platform/Backend.hpp"

#include <utility>

namespace ui
{

namespace
{
// UI-thread state, like the clipboard override (Clipboard.cpp).
Notifier* g_installed_notifier = nullptr;
} // namespace

void notify(Notification notification)
{
    Notifier& notifier = g_installed_notifier != nullptr ? *g_installed_notifier : detail::platform_notifier();
    notifier.notify(std::move(notification));
}

void detail::install_notifier(Notifier* notifier)
{
    g_installed_notifier = notifier;
}

Notifier* detail::installed_notifier()
{
    return g_installed_notifier;
}

} // namespace ui
