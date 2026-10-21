// SPDX-License-Identifier: MIT
#pragma once

#ifdef _WIN32
// <windows.h>'s own tag behind HWND, so the alias below is that very type.
struct HWND__; // NOLINT(bugprone-reserved-identifier)
#endif

namespace ui
{

/// The operating system's own handle of a window or a hosted control, for the calls libwinui
/// does not wrap. On Windows it is the same type as `HWND`, declared here without `<windows.h>`,
/// so a real HWND passes in and out unchanged; elsewhere it is an opaque pointer. Null while the
/// window does not exist.
///
/// Example:
/// <!-- not compiled: needs <windows.h>, which the check cannot include inside its namespace -->
/// @code
/// if (const ui::NativeHandle hwnd = window.native_handle())
/// {
///     FlashWindow(hwnd, TRUE); // any Win32 call libwinui doesn't wrap
/// }
/// @endcode
#ifdef _WIN32
using NativeHandle = HWND__*;
#else
using NativeHandle = void*;
#endif

} // namespace ui
