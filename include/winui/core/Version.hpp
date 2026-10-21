// SPDX-License-Identifier: MIT
#pragma once

// The numbers are written here by hand and must match project(VERSION) in CMakeLists.txt:
// configuring fails when they differ.

/// Major version of the headers being compiled against, for the preprocessor.
#define LIBWINUI_VERSION_MAJOR 0
/// Minor version of the headers being compiled against, for the preprocessor.
#define LIBWINUI_VERSION_MINOR 1
/// Patch version of the headers being compiled against, for the preprocessor.
#define LIBWINUI_VERSION_PATCH 0

/// Marks a name kept only for compatibility: `[[deprecated(msg)]]`, or nothing when
/// LIBWINUI_NO_DEPRECATION_WARNINGS is defined before the first libwinui include. `msg` names the
/// replacement.
#ifdef LIBWINUI_NO_DEPRECATION_WARNINGS
#define LIBWINUI_DEPRECATED(msg)
#else
#define LIBWINUI_DEPRECATED(msg) [[deprecated(msg)]]
#endif

// Internal: LIBWINUI_DEPRECATED for a data member. GCC (14 included) reports a deprecated member in
// every implicitly defined constructor and destructor of its class, that is wherever the class
// is created, copied or destroyed, so there it marks nothing.
#if defined(__GNUC__) && !defined(__clang__)
#define LIBWINUI_DEPRECATED_MEMBER(msg)
#else
#define LIBWINUI_DEPRECATED_MEMBER(msg) LIBWINUI_DEPRECATED(msg)
#endif

// Internal: around the few places the library itself still has to touch a deprecated name (the
// compatibility path behind it), so that use does not warn.
#if defined(_MSC_VER) && !defined(__clang__)
#define LIBWINUI_ALLOW_DEPRECATED_BEGIN _Pragma("warning(push)") _Pragma("warning(disable : 4996)")
#define LIBWINUI_ALLOW_DEPRECATED_END   _Pragma("warning(pop)")
#else
#define LIBWINUI_ALLOW_DEPRECATED_BEGIN \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define LIBWINUI_ALLOW_DEPRECATED_END _Pragma("GCC diagnostic pop")
#endif

namespace ui
{

/// Major version of the headers being compiled against.
inline constexpr int VersionMajor = LIBWINUI_VERSION_MAJOR;
/// Minor version of the headers being compiled against.
inline constexpr int VersionMinor = LIBWINUI_VERSION_MINOR;
/// Patch version of the headers being compiled against.
inline constexpr int VersionPatch = LIBWINUI_VERSION_PATCH;

/// The version of the linked library as "major.minor.patch", e.g. "0.1.0". Compare it with
/// VersionMajor, VersionMinor and VersionPatch to detect a header/binary mismatch with the shared
/// library. There is no ABI promise: a shared library only works with the headers of its own
/// version, built by the same compiler with the same options. The string is static; never free it.
[[nodiscard]] const char* version_string();

} // namespace ui
