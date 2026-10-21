// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Win32Launcher.hpp"

#include "../Backend.hpp"
#include "Win32Apartment.hpp"
#include "Win32String.hpp"

// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

#include <filesystem>
#include <string>
#include <system_error>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

namespace ui::win32
{

void Win32Launcher::launch_uri(const std::string& uri)
{
    keep_mta_alive();
    try
    {
        using winrt::Windows::Foundation::Uri;
        using winrt::Windows::System::Launcher;
        (void)Launcher::LaunchUriAsync(Uri{widen(uri)});
    } catch (const winrt::hresult_error&) // NOLINT(bugprone-empty-catch)
    {
        // Uri's constructor rejects a malformed string, and launch_uri() has no failure to report.
    }
}

void Win32Launcher::launch_file(const std::filesystem::path& file)
{
    std::error_code error;
    if (!std::filesystem::exists(file, error))
    {
        return;
    }
    // ShellExecuteEx's "open" verb is the file manager's double-click. SEE_MASK_ASYNCOK returns
    // without waiting for the shell's DDE conversation, so the UI thread does not stall.
    const std::wstring path = file.wstring();
    SHELLEXECUTEINFOW  info{};
    info.cbSize = sizeof(info);
    info.fMask  = SEE_MASK_ASYNCOK;
    info.lpVerb = L"open";
    info.lpFile = path.c_str();
    info.nShow  = SW_SHOWNORMAL;
    (void)ShellExecuteExW(&info);
}

} // namespace ui::win32

namespace ui::detail
{

Launcher& platform_launcher()
{
    static win32::Win32Launcher launcher;
    return launcher;
}

} // namespace ui::detail

#endif // _WIN32
