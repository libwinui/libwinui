// SPDX-License-Identifier: MIT
#include "winui/ui/themes/EarlyPalette.hpp"

namespace ui
{

// EarlyPalette's fields are the system colors COLOR_SCROLLBAR (0) through COLOR_APPWORKSPACE (12)
// in index order, each field named after its index. Windows 1.x has the first ten; 2.x adds
// COLOR_ACTIVEBORDER, COLOR_INACTIVEBORDER and COLOR_APPWORKSPACE.

// The Windows 2.03 color row (the same for EGA, VGA and 1024x768), in `COLOR_*` index order.
EarlyPalette EarlyPalette::windows_2x_color()
{
    EarlyPalette p;
    p.scrollbar        = Color::from_argb(0xFF3F3F3F);
    p.background       = Color::from_argb(0xFF00FFFF);
    p.active_caption   = Color::from_argb(0xFF0000FF);
    p.inactive_caption = Color::from_argb(0xFFFFFFFF);
    p.menu             = Color::from_argb(0xFF00FFFF);
    p.window           = Color::from_argb(0xFFFFFFFF);
    p.window_frame     = Color::from_argb(0xFF000000);
    p.menu_text        = Color::from_argb(0xFF000000);
    p.window_text      = Color::from_argb(0xFF000000);
    p.caption_text     = Color::from_argb(0xFFFFFFFF);
    p.active_border    = Color::from_argb(0xFF7F7F7F);
    p.inactive_border  = Color::from_argb(0xFF7F7F7F);
    p.app_workspace    = Color::from_argb(0xFF0080FF);
    return p;
}

// The Windows 2.03 black-and-white row (the same for all five adapters). Differs from the color
// row in four slots: `BACKGROUND`, `ACTIVECAPTION`, `MENU` and `APPWORKSPACE`.
EarlyPalette EarlyPalette::windows_2x_mono()
{
    EarlyPalette p   = windows_2x_color();
    p.background     = Color::from_argb(0xFF7F7F7F);
    p.menu           = Color::from_argb(0xFFFFFFFF);
    p.active_caption = Color::from_argb(0xFF000000);
    p.app_workspace  = Color::from_argb(0xFFBFBFBF);
    return p;
}

// The default colors of the four Windows 1.01 mono display drivers, all identical. The three
// 2.x-only slots do not exist in 1.01.
EarlyPalette EarlyPalette::windows_1x_mono()
{
    EarlyPalette p;
    p.scrollbar        = Color::from_argb(0xFFC0C0C0);
    p.background       = Color::from_argb(0xFF808080);
    p.active_caption   = Color::from_argb(0xFF000000);
    p.inactive_caption = Color::from_argb(0xFF808080);
    p.menu             = Color::from_argb(0xFFFFFFFF);
    p.window           = Color::from_argb(0xFFFFFFFF);
    p.window_frame     = Color::from_argb(0xFF000000);
    p.menu_text        = Color::from_argb(0xFF000000);
    p.window_text      = Color::from_argb(0xFF000000);
    p.caption_text     = Color::from_argb(0xFFFFFFFF);
    return p;
}

// The Windows 1.01 EGA colors, the same at high and low resolution, and the same as the alpha/beta
// 1 colors Windows 2.03 still carries.
EarlyPalette EarlyPalette::windows_1x_color()
{
    EarlyPalette p     = windows_1x_mono();
    p.scrollbar        = Color::from_argb(0xFFFF0000);
    p.background       = Color::from_argb(0xFF00FF40);
    p.active_caption   = Color::from_argb(0xFF0000FF);
    p.inactive_caption = Color::from_argb(0xFF4040C8);
    p.menu             = Color::from_argb(0xFFFFFF00);
    return p;
}

} // namespace ui
