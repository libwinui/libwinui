// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../../ui/WindowHost.hpp"
#include "winui/render/Bitmap.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>

namespace ui::win32
{

/// A new icon of `size_px` square, owned by the caller (DestroyIcon), or nullptr for an empty
/// WindowIcon or on failure. A resource icon is loaded from the executable and needs neither factory.
[[nodiscard]] HICON create_icon(const WindowIcon& icon, int size_px, ID2D1Factory* d2d, IDWriteFactory* dwrite);

// Explorer's own taskbar setting, not "Apps use light theme": the two are separate switches. A
// glyph with no foreground of its own is drawn to read on it.
[[nodiscard]] bool taskbar_uses_light_theme();

// The executable's first icon resource at `size_px`, the one Explorer shows for the file; nullptr
// when it has none. What a window without an icon of its own shows.
[[nodiscard]] HICON load_executable_icon(int size_px);

/// The pixels of `icon` at its own size, straight alpha; empty for nullptr or a monochrome icon.
/// An icon without an alpha channel takes its transparency from the mask.
[[nodiscard]] Bitmap icon_pixels(HICON icon);

} // namespace ui::win32

#endif // _WIN32
