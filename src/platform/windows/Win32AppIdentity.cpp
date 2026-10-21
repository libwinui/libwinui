// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Win32AppIdentity.hpp"

// clang-format off
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
// clang-format on

#include <system_error>

namespace ui::win32
{

std::filesystem::path module_path()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size())
        {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring explicit_app_user_model_id()
{
    std::wstring id;
    PWSTR        explicit_id = nullptr;
    if (SUCCEEDED(GetCurrentProcessExplicitAppUserModelID(&explicit_id)) && explicit_id != nullptr)
    {
        id = explicit_id;
    }
    CoTaskMemFree(explicit_id);
    return id;
}

std::filesystem::path cache_folder(const wchar_t* name)
{
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
    {
        CoTaskMemFree(local);
        return {};
    }
    std::filesystem::path folder = std::filesystem::path{local} / L"libwinui" / name;
    CoTaskMemFree(local);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    return error ? std::filesystem::path{} : folder;
}

} // namespace ui::win32

#endif // _WIN32
