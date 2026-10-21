// SPDX-License-Identifier: MIT
#include "winui/ui/JumpList.hpp"

#include "../platform/Backend.hpp"

#include <utility>

namespace ui
{

namespace
{
// UI-thread state, like the notifier override (Notification.cpp).
JumpListHost* g_installed_jump_list_host = nullptr;

JumpListHost& jump_list_host()
{
    return g_installed_jump_list_host != nullptr ? *g_installed_jump_list_host : detail::platform_jump_list_host();
}
} // namespace

void set_jump_list(std::vector<JumpTask> tasks)
{
    jump_list_host().set_tasks(std::move(tasks));
}

void add_recent_file(const std::filesystem::path& path)
{
    jump_list_host().add_recent_file(path);
}

void detail::install_jump_list_host(JumpListHost* host)
{
    g_installed_jump_list_host = host;
}

JumpListHost* detail::installed_jump_list_host()
{
    return g_installed_jump_list_host;
}

} // namespace ui
