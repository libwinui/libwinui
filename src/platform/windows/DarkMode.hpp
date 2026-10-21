// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

namespace ui::win32
{

// Returns false when this Windows has no immersive app mode. Only the next menu picks the
// change up. UI thread only: the applied-mode cache is an unsynchronized static.
bool set_app_dark_mode(bool dark);

// The Windows 10 or 11 build number (22000 and up is Windows 11), or 0 on an earlier Windows.
[[nodiscard]] unsigned long os_build();

} // namespace ui::win32

#endif // _WIN32
