// SPDX-License-Identifier: MIT
#include "winui/core/Version.hpp"

#include <format>
#include <string>

namespace ui
{

const char* version_string()
{
    // Made from the constants this file was compiled with, so the two cannot drift apart.
    static const std::string text = std::format("{}.{}.{}", VersionMajor, VersionMinor, VersionPatch);
    return text.c_str();
}

} // namespace ui
