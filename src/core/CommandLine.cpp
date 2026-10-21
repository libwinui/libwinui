// SPDX-License-Identifier: MIT
#include "winui/core/CommandLine.hpp"

#include "winui/core/Utf8.hpp"

#include <string>
#include <vector>

#include <cstddef>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <shellapi.h> // needs windows.h ahead of it
// clang-format on
#else
#include <fstream>
#include <iterator>
#endif

namespace ui
{

std::vector<std::string> command_line()
{
    std::vector<std::string> args;
#ifdef _WIN32
    int     count = 0;
    LPWSTR* argv  = CommandLineToArgvW(GetCommandLineW(), &count);
    if (argv == nullptr)
    {
        return args;
    }
    args.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        // wchar_t is UTF-16 on Windows, so the argument is char16_t in all but name.
        args.push_back(from_utf16(reinterpret_cast<const char16_t*>(argv[i])));
    }
    LocalFree(static_cast<HLOCAL>(argv));
#else
    // NUL-separated, with a NUL after the last argument too.
    std::ifstream     file{"/proc/self/cmdline", std::ios::binary};
    const std::string raw{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::size_t       start = 0;
    while (start < raw.size())
    {
        std::size_t end = raw.find('\0', start);
        if (end == std::string::npos)
        {
            end = raw.size();
        }
        args.push_back(sanitize_utf8(std::string_view{raw}.substr(start, end - start)));
        start = end + 1;
    }
#endif
    return args;
}


} // namespace ui
