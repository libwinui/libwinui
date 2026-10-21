// SPDX-License-Identifier: MIT
#include "winui/ui/themes/Mdl2Palette.hpp"

#include "winui/core/Color.hpp"

namespace ui
{

Mdl2Palette Mdl2Palette::mdl2_light()
{
    Mdl2Palette p;
    p.page_100               = Color::from_argb(0xFFFFFFFF);
    p.page_20                = Color::from_argb(0x33FFFFFF);
    p.page_60                = Color::from_argb(0x99FFFFFF);
    p.page_80                = Color::from_argb(0xCCFFFFFF);
    p.page_40                = Color::from_argb(0x66FFFFFF);
    p.text_100               = Color::from_argb(0xFF000000);
    p.text_20                = Color::from_argb(0x33000000);
    p.text_60                = Color::from_argb(0x99000000);
    p.text_80                = Color::from_argb(0xCC000000);
    p.text_40                = Color::from_argb(0x66000000);
    p.chrome_contrast        = Color::from_argb(0xFF171717);
    p.black_100              = Color::from_argb(0xFF000000);
    p.black_20               = Color::from_argb(0x33000000);
    p.black_40               = Color::from_argb(0x66000000);
    p.black_80               = Color::from_argb(0xCC000000);
    p.chrome_disabled_faint  = Color::from_argb(0xFFCCCCCC);
    p.chrome_disabled_strong = Color::from_argb(0xFF7A7A7A);
    p.chrome_strong          = Color::from_argb(0xFFCCCCCC);
    p.chrome_faint           = Color::from_argb(0xFFF2F2F2);
    p.chrome_mid             = Color::from_argb(0xFFE6E6E6);
    p.chrome_flyout          = Color::from_argb(0xFFF2F2F2);
    p.white_100              = Color::from_argb(0xFFFFFFFF);
    p.list_hover             = Color::from_argb(0x19000000);
    p.list_pressed           = Color::from_argb(0x33000000);
    p.page_background        = Color::from_argb(0xFFFFFFFF);
    return p;
}

Mdl2Palette Mdl2Palette::mdl2_dark()
{
    Mdl2Palette p;
    p.page_100               = Color::from_argb(0xFF000000);
    p.page_20                = Color::from_argb(0x33000000);
    p.page_60                = Color::from_argb(0x99000000);
    p.page_80                = Color::from_argb(0xCC000000);
    p.page_40                = Color::from_argb(0x66000000);
    p.text_100               = Color::from_argb(0xFFFFFFFF);
    p.text_20                = Color::from_argb(0x33FFFFFF);
    p.text_60                = Color::from_argb(0x99FFFFFF);
    p.text_80                = Color::from_argb(0xCCFFFFFF);
    p.text_40                = Color::from_argb(0x66FFFFFF);
    p.chrome_contrast        = Color::from_argb(0xFFF2F2F2);
    p.black_100              = Color::from_argb(0xFF000000);
    p.black_20               = Color::from_argb(0x33000000);
    p.black_40               = Color::from_argb(0x66000000);
    p.black_80               = Color::from_argb(0xCC000000);
    p.chrome_disabled_faint  = Color::from_argb(0xFF333333);
    p.chrome_disabled_strong = Color::from_argb(0xFF858585);
    p.chrome_strong          = Color::from_argb(0xFF767676);
    p.chrome_faint           = Color::from_argb(0xFF171717);
    p.chrome_mid             = Color::from_argb(0xFF1F1F1F);
    p.chrome_flyout          = Color::from_argb(0xFF2B2B2B);
    p.white_100              = Color::from_argb(0xFFFFFFFF);
    p.list_hover             = Color::from_argb(0x19FFFFFF);
    p.list_pressed           = Color::from_argb(0x33FFFFFF);
    p.page_background        = Color::from_argb(0xFF000000);
    return p;
}

} // namespace ui
