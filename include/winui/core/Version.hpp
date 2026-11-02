// SPDX-License-Identifier: MIT
#pragma once

#include "winui/Macros.hpp"

#ifndef LIBWINUI_IMPORTED

namespace ui
{

/// Major version of the headers being compiled against.
LIBWINUI_EXPORT inline constexpr int VersionMajor = LIBWINUI_VERSION_MAJOR;
/// Minor version of the headers being compiled against.
LIBWINUI_EXPORT inline constexpr int VersionMinor = LIBWINUI_VERSION_MINOR;
/// Patch version of the headers being compiled against.
LIBWINUI_EXPORT inline constexpr int VersionPatch = LIBWINUI_VERSION_PATCH;

/// The version of the linked library as "major.minor.patch", e.g. "0.1.0". Compare it with
/// VersionMajor, VersionMinor and VersionPatch to detect a header/binary mismatch with the shared
/// library. There is no ABI promise: a shared library only works with the headers of its own
/// version, built by the same compiler with the same options. The string is static; never free it.
LIBWINUI_EXPORT [[nodiscard]] const char* version_string();

} // namespace ui

#endif // LIBWINUI_IMPORTED
