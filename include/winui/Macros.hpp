// SPDX-License-Identifier: MIT
#pragma once

// libwinui's preprocessor side: macros only, no declarations. Every public header includes it
// first, and a file that imports the winui module includes it for the macros, which a module
// doesn't export.

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

// Internal: an empty member that takes no room (core/Designated.hpp). MSVC ignores the standard
// attribute to keep its ABI, and clang-cl matches that ABI (and warns that it ignores it), so both
// get nothing: the layout is the same whichever of the two compiles a translation unit.
#if defined(_MSC_VER)
#define LIBWINUI_NO_UNIQUE_ADDRESS
#else
#define LIBWINUI_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

// The thread-affinity checks (core/ThreadCheck.hpp): 1 compiles them in, 0 compiles them out.
// Unset, it follows the build type: on without NDEBUG, off with it. The CMake option
// LIBWINUI_THREAD_CHECKS (AUTO, ON, OFF) sets it for the library and everything that links it. It
// never changes a class layout: the owning thread ids are stored either way (one std::thread::id
// per Window, Element and observed source) and only the checks are switched, so a consumer built
// with a different NDEBUG than the library still agrees with it on every object it shares.
#ifndef LIBWINUI_THREAD_CHECKS
#ifdef NDEBUG
#define LIBWINUI_THREAD_CHECKS 0
#else
#define LIBWINUI_THREAD_CHECKS 1
#endif
#endif

// Internal: `export` on each public declaration inside the winui module (modules/winui.cppm, which
// defines LIBWINUI_MODULE before it includes anything), nothing in a header build.
#ifdef LIBWINUI_MODULE
#define LIBWINUI_EXPORT export
#else
#define LIBWINUI_EXPORT
#endif
