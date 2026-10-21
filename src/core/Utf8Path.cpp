// SPDX-License-Identifier: MIT
#include "winui/core/Utf8Path.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace ui
{

std::filesystem::path path_from_utf8(std::string_view text)
{
    const std::string clean = sanitize_utf8(text);
    return std::filesystem::path{std::u8string{clean.begin(), clean.end()}};
}

std::string path_to_utf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string{u8.begin(), u8.end()};
}

} // namespace ui
