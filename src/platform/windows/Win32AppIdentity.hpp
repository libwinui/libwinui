// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include <filesystem>
#include <string>

namespace ui::win32
{

// The running executable's full path.
[[nodiscard]] std::filesystem::path module_path();

// The AppUserModelID the app set for itself (SetCurrentProcessExplicitAppUserModelID), or empty
// when it set none and the shell derives one from the executable. Toasts and the jump list both
// use it when it is there, so they belong to the same taskbar button.
[[nodiscard]] std::wstring explicit_app_user_model_id();

// %LOCALAPPDATA%\libwinui\<name>, created if missing; empty when that failed.
[[nodiscard]] std::filesystem::path cache_folder(const wchar_t* name);

} // namespace ui::win32

#endif // _WIN32
