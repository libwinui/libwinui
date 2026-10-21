// SPDX-License-Identifier: MIT
#include "winui/ui/themes/MetroPalette.hpp"

#include "winui/core/Color.hpp"

namespace ui
{

MetroPalette MetroPalette::metro_dark()
{
    MetroPalette p;
    p.page_background      = Color::from_argb(0xFF1D1D1D);
    p.foreground           = Color::from_argb(0xDEFFFFFF);
    p.foreground_secondary = Color::from_argb(0x99FFFFFF);
    p.foreground_pressed   = Color::from_argb(0x66FFFFFF);

    p.button_background          = colors::Transparent;
    p.button_border              = Color::from_argb(0xFFFFFFFF);
    p.button_foreground          = Color::from_argb(0xFFFFFFFF);
    p.button_hover_background    = Color::from_argb(0x21FFFFFF);
    p.button_pressed_background  = Color::from_argb(0xFFFFFFFF);
    p.button_pressed_foreground  = Color::from_argb(0xFF000000);
    p.button_disabled_background = colors::Transparent;
    p.button_disabled_border     = Color::from_argb(0x66FFFFFF);
    p.button_disabled_foreground = Color::from_argb(0x66FFFFFF);

    p.toggle_box_background = Color::from_argb(0xCCFFFFFF);
    p.toggle_box_border     = Color::from_argb(0xCCFFFFFF);
    p.toggle_glyph          = Color::from_argb(0xFF000000);

    p.field_background   = colors::White;
    p.field_text         = colors::Black;
    p.field_border       = Color::from_argb(0xFFFFFFFF);
    p.menu_background    = colors::White;
    p.menu_text          = Color::from_argb(0xFF000000);
    p.tooltip_background = colors::White;
    p.tooltip_text       = Color::from_argb(0xFF666666);
    p.tooltip_border     = Color::from_argb(0xFF808080);
    p.hyperlink          = Color::from_argb(0xFF9C72FF);
    p.separator          = Color::from_argb(0xFF7A7A7A);

    p.accent         = Color::from_argb(0xFF4617B4);
    p.accent_hover   = Color::from_argb(0xFF5F37BE);
    p.accent_pressed = Color::from_argb(0xFF7E4FEC); // a pressed toggle switch, dark

    // Theme-invariant in 8.1, but set in both factories so each stays a transcription of its
    // own dictionary.
    p.menu_highlight      = Color::from_argb(0xFFE5E5E5);
    p.menu_highlight_text = Color::from_argb(0xFF000000);
    p.scrollbar_track     = Color::from_argb(0x59D5D5D5);
    p.scrollbar_thumb     = Color::from_argb(0xFFCDCDCD);
    p.scrollbar_arrow     = Color::from_argb(0x99000000);

    p.button_border_width    = 2.0f;
    p.indicator_border_width = 0.0f;
    return p;
}

MetroPalette MetroPalette::metro_light()
{
    MetroPalette p;
    p.page_background      = Color::from_argb(0xFFFFFFFF);
    p.foreground           = Color::from_argb(0xDE000000);
    p.foreground_secondary = Color::from_argb(0x99000000);
    p.foreground_pressed   = Color::from_argb(0x66000000);

    p.button_background          = Color::from_argb(0xB3B6B6B6);
    p.button_border              = Color::from_argb(0x33000000); // drawn at thickness 0, so unused
    p.button_foreground          = Color::from_argb(0xFF000000);
    p.button_hover_background    = Color::from_argb(0xD1CDCDCD);
    p.button_pressed_background  = Color::from_argb(0xFF000000);
    p.button_pressed_foreground  = Color::from_argb(0xFFFFFFFF);
    p.button_disabled_background = Color::from_argb(0x66CACACA);
    p.button_disabled_border     = Color::from_argb(0x1A000000);
    p.button_disabled_foreground = Color::from_argb(0x66000000);

    p.toggle_box_background = Color::from_argb(0xCCFFFFFF);
    p.toggle_box_border     = Color::from_argb(0x45000000);
    p.toggle_glyph          = Color::from_argb(0xFF000000);

    p.field_background   = colors::White;
    p.field_text         = colors::Black;
    p.field_border       = Color::from_argb(0xA3000000);
    p.menu_background    = colors::White;
    p.menu_text          = Color::from_argb(0xFF000000);
    p.tooltip_background = colors::White;
    p.tooltip_text       = Color::from_argb(0xFF666666);
    p.tooltip_border     = Color::from_argb(0xFF808080);
    p.hyperlink          = Color::from_argb(0xFF4F1ACB);
    p.separator          = Color::from_argb(0xFF7A7A7A);

    p.accent         = Color::from_argb(0xFF4617B4);
    p.accent_hover   = Color::from_argb(0xFF5F37BE);
    p.accent_pressed = Color::from_argb(0xFF7241E4); // a pressed toggle switch, light

    p.menu_highlight      = Color::from_argb(0xFFE5E5E5);
    p.menu_highlight_text = Color::from_argb(0xFF000000);
    p.scrollbar_track     = Color::from_argb(0x59D5D5D5);
    p.scrollbar_thumb     = Color::from_argb(0xFFCDCDCD);
    p.scrollbar_arrow     = Color::from_argb(0x99000000);

    p.button_border_width    = 0.0f;
    p.indicator_border_width = 2.0f;
    return p;
}

} // namespace ui
