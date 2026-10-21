// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>
#include <windows.h>

// UTF-8 to and from the UTF-16 the Win32 "W" functions take. libwinui's API is UTF-8 throughout;
// an app that calls Win32 itself converts at that boundary, as these controls do.
namespace native_controls
{

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

} // namespace native_controls
