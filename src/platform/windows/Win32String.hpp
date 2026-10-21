// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>
#include <windows.h>

namespace ui::win32
{

/// `text` (UTF-8) as UTF-16 for a ...W call.
[[nodiscard]] inline std::wstring widen(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    const int    size = static_cast<int>(text.size());
    const int    n    = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), size, out.data(), n);
    return out;
}

/// `text` (UTF-16 from Windows) as UTF-8.
[[nodiscard]] inline std::string narrow(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }
    const int   size = static_cast<int>(text.size());
    const int   n    = WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), size, out.data(), n, nullptr, nullptr);
    return out;
}

/// A NUL-terminated UTF-16 string from Windows, as UTF-8; empty for nullptr.
[[nodiscard]] inline std::string narrow(const wchar_t* text)
{
    return text != nullptr ? narrow(std::wstring_view{text}) : std::string{};
}

} // namespace ui::win32
