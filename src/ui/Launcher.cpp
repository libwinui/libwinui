// SPDX-License-Identifier: MIT
#include "winui/ui/Launcher.hpp"

#include "../platform/Backend.hpp"

#include <filesystem>
#include <string>

namespace ui
{

namespace
{
// UI-thread state, like the clipboard override (Clipboard.cpp).
Launcher* g_installed_launcher = nullptr;

Launcher& current_launcher()
{
    return g_installed_launcher != nullptr ? *g_installed_launcher : detail::platform_launcher();
}
} // namespace

void launch_uri(const std::string& uri)
{
    if (!uri.empty())
    {
        current_launcher().launch_uri(uri);
    }
}

void launch_file(const std::filesystem::path& file)
{
    if (!file.empty())
    {
        current_launcher().launch_file(file);
    }
}

void detail::install_launcher(Launcher* launcher)
{
    g_installed_launcher = launcher;
}

Launcher* detail::installed_launcher()
{
    return g_installed_launcher;
}

} // namespace ui
