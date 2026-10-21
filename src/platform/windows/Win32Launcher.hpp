// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "winui/ui/Launcher.hpp"

namespace ui::win32
{

class Win32Launcher final : public Launcher
{
public:
    /// Fire and forget: the IAsyncOperation is not waited on, so the UI thread never blocks on
    /// the shell. A string that is not a URI, or one no app is registered for, is dropped.
    void launch_uri(const std::string& uri) override;
    /// ShellExecuteEx's "open" verb, without waiting for the app to start. A missing file is
    /// dropped; a type with no app registered gets the shell's "open with" prompt.
    void launch_file(const std::filesystem::path& file) override;
};

} // namespace ui::win32

#endif // _WIN32
