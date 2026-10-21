// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include <windows.h>

namespace ui::win32
{

// GetProcAddress's FARPROC as the type the export really has, or null. The cast goes through
// void (*)(), the one function pointer type the cast-function-type warnings treat as generic.
template <typename Fn>
[[nodiscard]] Fn proc_address(HMODULE module, const char* name)
{
    return reinterpret_cast<Fn>(reinterpret_cast<void (*)()>(GetProcAddress(module, name)));
}

} // namespace ui::win32

#endif
