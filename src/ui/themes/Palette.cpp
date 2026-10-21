// SPDX-License-Identifier: MIT
#include "winui/ui/themes/Palette.hpp"

#include "winui/core/Color.hpp"

namespace ui
{

Palette Palette::fluent_light()
{
    Palette p;
    p.dark                     = false;
    p.text_fill_primary        = Color::from_argb(0xE4000000);
    p.text_fill_secondary      = Color::from_argb(0x9E000000);
    p.text_fill_tertiary       = Color::from_argb(0x72000000);
    p.text_fill_disabled       = Color::from_argb(0x5C000000);
    p.text_on_accent_primary   = colors::White;
    p.text_on_accent_secondary = Color::from_argb(0xB3FFFFFF);
    p.text_on_accent_disabled  = colors::White;

    p.control_fill_default      = Color::from_argb(0xB3FFFFFF);
    p.control_fill_secondary    = Color::from_argb(0x80F9F9F9);
    p.control_fill_tertiary     = Color::from_argb(0x4DF9F9F9);
    p.control_fill_quaternary   = Color::from_argb(0xC2F3F3F3);
    p.control_fill_disabled     = Color::from_argb(0x4DF9F9F9);
    p.control_fill_input_active = colors::White;

    p.control_alt_fill_secondary  = Color::from_argb(0x06000000);
    p.control_alt_fill_tertiary   = Color::from_argb(0x0F000000);
    p.control_alt_fill_quaternary = Color::from_argb(0x18000000);
    p.control_alt_fill_disabled   = colors::Transparent;

    p.subtle_fill_transparent = colors::Transparent;
    p.subtle_fill_secondary   = Color::from_argb(0x09000000);
    p.subtle_fill_tertiary    = Color::from_argb(0x06000000);

    p.on_backdrop_hover   = Color::from_argb(0x0A000000);
    p.on_backdrop_pressed = Color::from_argb(0xB3FFFFFF);

    p.accent_fill_default   = Color::from_argb(0xFF005A9E);
    p.accent_fill_disabled  = Color::from_argb(0x37000000);
    p.accent_text_primary   = Color::from_argb(0xFF004275);
    p.accent_text_secondary = Color::from_argb(0xFF002642);
    p.accent_text_tertiary  = Color::from_argb(0xFF005A9E);
    p.accent_text_disabled  = Color::from_argb(0x5C000000);

    p.control_stroke_default             = Color::from_argb(0x0F000000);
    p.control_stroke_secondary           = Color::from_argb(0x29000000);
    p.control_stroke_on_accent_default   = Color::from_argb(0x14FFFFFF);
    p.control_stroke_on_accent_secondary = Color::from_argb(0x66000000);
    p.control_strong_stroke_default      = Color::from_argb(0x72000000);
    p.control_strong_stroke_disabled     = Color::from_argb(0x37000000);
    p.focus_stroke_outer                 = Color::from_argb(0xE4000000);
    p.focus_stroke_inner                 = Color::from_argb(0xB3FFFFFF);
    p.card_stroke_default                = Color::from_argb(0x0F000000);
    p.divider_stroke_default             = Color::from_argb(0x0F000000);
    p.layer_fill_alt                     = colors::White;
    p.layer_fill_default                 = Color::from_argb(0x80FFFFFF);

    p.solid_background_base      = Color::from_argb(0xFFF3F3F3);
    p.solid_background_secondary = Color::from_argb(0xFFEEEEEE);
    p.solid_background_tertiary  = Color::from_argb(0xFFF9F9F9);
    p.acrylic_fallback_default   = Color::from_argb(0xFFF9F9F9);

    p.card_background_default   = Color::from_argb(0xB3FFFFFF);
    p.card_background_secondary = Color::from_argb(0x80F6F6F6);

    p.semantic_fill_success             = Color::from_argb(0xFF0F7B0F);
    p.semantic_fill_caution             = Color::from_argb(0xFF9D5D00);
    p.semantic_fill_critical            = Color::from_argb(0xFFC42B1C);
    p.semantic_fill_solid_neutral       = Color::from_argb(0xFF8A8A8A);
    p.semantic_background_informational = Color::from_argb(0x80F6F6F6);
    p.semantic_background_success       = Color::from_argb(0xFFDFF6DD);
    p.semantic_background_caution       = Color::from_argb(0xFFFFF4CE);
    p.semantic_background_critical      = Color::from_argb(0xFFFDE7E9);

    p.tabular_header_background = Color::from_argb(0x26000000);
    p.tabular_grid_line         = Color::from_argb(0x29000000);
    p.tabular_row_alternating   = Color::from_argb(0x09000000);
    return p;
}

Palette Palette::fluent_dark()
{
    Palette p;
    p.dark                     = true;
    p.text_fill_primary        = colors::White;
    p.text_fill_secondary      = Color::from_argb(0xC5FFFFFF);
    p.text_fill_tertiary       = Color::from_argb(0x87FFFFFF);
    p.text_fill_disabled       = Color::from_argb(0x5DFFFFFF);
    p.text_on_accent_primary   = colors::Black;
    p.text_on_accent_secondary = Color::from_argb(0x80000000);
    p.text_on_accent_disabled  = Color::from_argb(0x87FFFFFF);

    p.control_fill_default      = Color::from_argb(0x0FFFFFFF);
    p.control_fill_secondary    = Color::from_argb(0x15FFFFFF);
    p.control_fill_tertiary     = Color::from_argb(0x08FFFFFF);
    p.control_fill_quaternary   = Color::from_argb(0x0FFFFFFF);
    p.control_fill_disabled     = Color::from_argb(0x0BFFFFFF);
    p.control_fill_input_active = Color::from_argb(0xB31E1E1E);

    p.control_alt_fill_secondary  = Color::from_argb(0x19000000);
    p.control_alt_fill_tertiary   = Color::from_argb(0x0BFFFFFF);
    p.control_alt_fill_quaternary = Color::from_argb(0x12FFFFFF);
    p.control_alt_fill_disabled   = colors::Transparent;

    p.subtle_fill_transparent = colors::Transparent;
    p.subtle_fill_secondary   = Color::from_argb(0x0FFFFFFF);
    p.subtle_fill_tertiary    = Color::from_argb(0x0AFFFFFF);

    p.on_backdrop_hover   = Color::from_argb(0x0FFFFFFF);
    p.on_backdrop_pressed = Color::from_argb(0x733A3A3A);

    p.accent_fill_default   = Color::from_argb(0xFF76B9ED);
    p.accent_fill_disabled  = Color::from_argb(0x28FFFFFF);
    p.accent_text_primary   = Color::from_argb(0xFFA6D8FF);
    p.accent_text_secondary = Color::from_argb(0xFFA6D8FF);
    p.accent_text_tertiary  = Color::from_argb(0xFF76B9ED);
    p.accent_text_disabled  = Color::from_argb(0x5DFFFFFF);

    p.control_stroke_default             = Color::from_argb(0x12FFFFFF);
    p.control_stroke_secondary           = Color::from_argb(0x18FFFFFF);
    p.control_stroke_on_accent_default   = Color::from_argb(0x14FFFFFF);
    p.control_stroke_on_accent_secondary = Color::from_argb(0x23000000);
    p.control_strong_stroke_default      = Color::from_argb(0x8BFFFFFF);
    p.control_strong_stroke_disabled     = Color::from_argb(0x28FFFFFF);
    p.focus_stroke_outer                 = colors::White;
    p.focus_stroke_inner                 = Color::from_argb(0xB3000000);
    p.card_stroke_default                = Color::from_argb(0x19000000);
    p.divider_stroke_default             = Color::from_argb(0x15FFFFFF);
    p.layer_fill_alt                     = Color::from_argb(0x0DFFFFFF);
    p.layer_fill_default                 = Color::from_argb(0x4C3A3A3A);

    p.control_solid_fill_default      = Color::from_argb(0xFF454545);
    p.control_on_image_fill_default   = Color::from_argb(0xB31C1C1C);
    p.control_on_image_fill_secondary = Color::from_argb(0xFF1A1A1A);
    p.control_on_image_fill_tertiary  = Color::from_argb(0xFF131313);
    p.control_on_image_fill_disabled  = Color::from_argb(0xFF1E1E1E);

    p.solid_background_base      = Color::from_argb(0xFF202020);
    p.solid_background_secondary = Color::from_argb(0xFF1C1C1C);
    p.solid_background_tertiary  = Color::from_argb(0xFF282828);
    p.acrylic_fallback_default   = Color::from_argb(0xFF2C2C2C);

    p.card_background_default   = Color::from_argb(0x0DFFFFFF);
    p.card_background_secondary = Color::from_argb(0x08FFFFFF);

    p.semantic_fill_success             = Color::from_argb(0xFF6CCB5F);
    p.semantic_fill_caution             = Color::from_argb(0xFFFCE100);
    p.semantic_fill_critical            = Color::from_argb(0xFFFF99A4);
    p.semantic_fill_solid_neutral       = Color::from_argb(0xFF9D9D9D);
    p.semantic_background_informational = Color::from_argb(0x08FFFFFF);
    p.semantic_background_success       = Color::from_argb(0xFF393D1B);
    p.semantic_background_caution       = Color::from_argb(0xFF433519);
    p.semantic_background_critical      = Color::from_argb(0xFF442726);

    p.tabular_header_background = Color::from_argb(0x26FFFFFF);
    p.tabular_grid_line         = Color::from_argb(0x29FFFFFF);
    p.tabular_row_alternating   = Color::from_argb(0x0FFFFFFF);
    return p;
}

Palette Palette::fluent1_light()
{
    Palette p;
    p.dark = false;

    // The era has no separate tertiary text rung: every disabled foreground is the 40 % base
    // color, the same as tertiary here.
    p.text_fill_primary        = colors::Black;
    p.text_fill_secondary      = Color::from_argb(0x99000000);
    p.text_fill_tertiary       = Color::from_argb(0x66000000);
    p.text_fill_disabled       = Color::from_argb(0x66000000);
    p.text_on_accent_primary   = colors::White;
    p.text_on_accent_secondary = colors::White;
    p.text_on_accent_disabled  = Color::from_argb(0x66000000);

    // Hover is lighter than rest in this era. 0x19 stands in for the era's 0.1 opacity over
    // opaque black, which composites at 25.5/255.
    p.control_fill_default      = Color::from_argb(0x33000000);
    p.control_fill_secondary    = Color::from_argb(0x19000000);
    p.control_fill_tertiary     = Color::from_argb(0x66000000);
    p.control_fill_quaternary   = Color::from_argb(0x33000000); // no era counterpart, and no reader
    p.control_fill_disabled     = Color::from_argb(0x33000000);
    p.control_fill_input_active = colors::White;

    p.control_alt_fill_secondary  = colors::Transparent;
    p.control_alt_fill_tertiary   = colors::Transparent;
    p.control_alt_fill_quaternary = Color::from_argb(0x66000000);
    p.control_alt_fill_disabled   = colors::Transparent;

    p.subtle_fill_transparent = colors::Transparent;
    p.subtle_fill_secondary   = Color::from_argb(0x19000000);
    p.subtle_fill_tertiary    = Color::from_argb(0x33000000);

    // No Mica and no layer-on-Mica fills in this era, so Tabs reuses the list ladder.
    p.on_backdrop_hover   = Color::from_argb(0x19000000);
    p.on_backdrop_pressed = Color::from_argb(0x33000000);

    // A disabled accent button goes neutral (the 20 % base color) instead of dimming the accent.
    p.accent_fill_default  = Color::from_argb(0xFF0078D4);
    p.accent_fill_disabled = Color::from_argb(0x33000000);
    // A Windows 10 link grays out under the pointer instead of stepping along an accent ramp.
    p.accent_text_primary   = Color::from_argb(0xFF0078D4);
    p.accent_text_secondary = Color::from_argb(0x99000000);
    p.accent_text_tertiary  = Color::from_argb(0x66000000);
    p.accent_text_disabled  = Color::from_argb(0x66000000);

    // Fluent 1's text field border, a rung bolder than the system's own.
    // No button draws a border in this era, so these serve fields and separators only.
    p.control_stroke_default   = Color::from_argb(0x99000000);
    p.control_stroke_secondary = Color::from_argb(0xCC000000);
    // The accent button's border is transparent in every state.
    p.control_stroke_on_accent_default   = colors::Transparent;
    p.control_stroke_on_accent_secondary = colors::Transparent;
    // Hover and press walk this to 80 % and 100 %. resolve_fluent1() applies that ladder,
    // since one shared `ToggleVisual` cannot hold it.
    p.control_strong_stroke_default  = Color::from_argb(0x99000000);
    p.control_strong_stroke_disabled = Color::from_argb(0x66000000);
    p.focus_stroke_outer             = colors::Black;
    p.focus_stroke_inner             = Color::from_argb(0x99FFFFFF);
    // The popup border: black in both themes, a dark line under a dark flyout.
    p.card_stroke_default    = Color::from_argb(0x24000000);
    p.divider_stroke_default = Color::from_argb(0x66000000);

    p.solid_background_base      = colors::White;
    p.solid_background_secondary = Color::from_argb(0xFFE6E6E6);
    p.solid_background_tertiary  = Color::from_argb(0xFFF2F2F2);
    p.acrylic_fallback_default   = Color::from_argb(0xFFF2F2F2);
    // Derived, since Expander postdates the era: the medium-low chrome color for the card face,
    // the low chrome color for the recessed panel.
    p.card_background_default   = Color::from_argb(0xFFF2F2F2);
    p.card_background_secondary = Color::from_argb(0xFFF2F2F2);

    // The era's error text color. The rest is inherited from Fluent 2: InfoBar and InfoBadge postdate
    // the era, and its ramp has no success or caution hue.
    p.semantic_fill_critical            = Color::from_argb(0xFFC50500);
    p.semantic_fill_solid_neutral       = Color::from_argb(0xFF8A8A8A);
    p.semantic_fill_success             = Color::from_argb(0xFF0F7B0F);
    p.semantic_fill_caution             = Color::from_argb(0xFF9D5D00);
    p.semantic_background_informational = Color::from_argb(0x80F6F6F6);
    p.semantic_background_success       = Color::from_argb(0xFFDFF6DD);
    p.semantic_background_caution       = Color::from_argb(0xFFFFF4CE);
    p.semantic_background_critical      = Color::from_argb(0xFFFDE7E9);

    // Derived, since TableView postdates the era. The header takes the list ladder's 20 % press
    // rung, the gridline its 10 % hover rung.
    p.tabular_header_background = Color::from_argb(0x33000000);
    p.tabular_grid_line         = Color::from_argb(0x19000000);
    // Its secondary subtle fill, as on Windows 11.
    p.tabular_row_alternating = Color::from_argb(0x19000000);
    return p;
}

Palette Palette::fluent1_dark()
{
    Palette p;
    p.dark = true;

    p.text_fill_primary   = colors::White;
    p.text_fill_secondary = Color::from_argb(0x99FFFFFF);
    p.text_fill_tertiary  = Color::from_argb(0x66FFFFFF);
    p.text_fill_disabled  = Color::from_argb(0x66FFFFFF);
    // The era's chrome white is white in both themes, so text on accent stays white.
    p.text_on_accent_primary   = colors::White;
    p.text_on_accent_secondary = colors::White;
    p.text_on_accent_disabled  = Color::from_argb(0x66FFFFFF);

    p.control_fill_default    = Color::from_argb(0x33FFFFFF);
    p.control_fill_secondary  = Color::from_argb(0x19FFFFFF);
    p.control_fill_tertiary   = Color::from_argb(0x66FFFFFF);
    p.control_fill_quaternary = Color::from_argb(0x33FFFFFF);
    p.control_fill_disabled   = Color::from_argb(0x33FFFFFF);
    // The era's own value. The system's makes a focused field white in both themes, which
    // would put white text on a white field here.
    p.control_fill_input_active = colors::Black;

    p.control_alt_fill_secondary  = colors::Transparent;
    p.control_alt_fill_tertiary   = colors::Transparent;
    p.control_alt_fill_quaternary = Color::from_argb(0x66FFFFFF);
    p.control_alt_fill_disabled   = colors::Transparent;

    p.subtle_fill_transparent = colors::Transparent;
    p.subtle_fill_secondary   = Color::from_argb(0x19FFFFFF);
    p.subtle_fill_tertiary    = Color::from_argb(0x33FFFFFF);

    p.on_backdrop_hover   = Color::from_argb(0x19FFFFFF);
    p.on_backdrop_pressed = Color::from_argb(0x33FFFFFF);

    p.accent_fill_default   = Color::from_argb(0xFF0078D4);
    p.accent_fill_disabled  = Color::from_argb(0x33FFFFFF);
    p.accent_text_primary   = Color::from_argb(0xFF0078D4);
    p.accent_text_secondary = Color::from_argb(0x99FFFFFF);
    p.accent_text_tertiary  = Color::from_argb(0x66FFFFFF);
    p.accent_text_disabled  = Color::from_argb(0x66FFFFFF);

    p.control_stroke_default             = Color::from_argb(0x99FFFFFF);
    p.control_stroke_secondary           = Color::from_argb(0xCCFFFFFF);
    p.control_stroke_on_accent_default   = colors::Transparent;
    p.control_stroke_on_accent_secondary = colors::Transparent;
    p.control_strong_stroke_default      = Color::from_argb(0x99FFFFFF);
    p.control_strong_stroke_disabled     = Color::from_argb(0x66FFFFFF);
    p.focus_stroke_outer                 = colors::White;
    p.focus_stroke_inner                 = Color::from_argb(0x99000000);
    // Still black in Dark: the one stroke that does not flip with the theme.
    p.card_stroke_default    = Color::from_argb(0x5C000000);
    p.divider_stroke_default = Color::from_argb(0x66FFFFFF);
    // Windows 10 has no solid control fill or on-image fill ramp; the
    // Fluent 2 Dark values stand in so a dark list's multi-select box is not white.
    p.control_solid_fill_default      = Color::from_argb(0xFF454545);
    p.control_on_image_fill_default   = Color::from_argb(0xB31C1C1C);
    p.control_on_image_fill_secondary = Color::from_argb(0xFF1A1A1A);
    p.control_on_image_fill_tertiary  = Color::from_argb(0xFF131313);
    p.control_on_image_fill_disabled  = Color::from_argb(0xFF1E1E1E);

    p.solid_background_base      = colors::Black;
    p.solid_background_secondary = Color::from_argb(0xFF1F1F1F);
    p.solid_background_tertiary  = Color::from_argb(0xFF2B2B2B);
    p.acrylic_fallback_default   = Color::from_argb(0xFF2B2B2B);
    p.card_background_default    = Color::from_argb(0xFF2B2B2B);
    p.card_background_secondary  = Color::from_argb(0xFF171717);

    // The era's error text color is yellow in Dark. The rest is inherited from Fluent 2 Dark.
    p.semantic_fill_critical            = Color::from_argb(0xFFFFF000);
    p.semantic_fill_solid_neutral       = Color::from_argb(0xFF9D9D9D);
    p.semantic_fill_success             = Color::from_argb(0xFF6CCB5F);
    p.semantic_fill_caution             = Color::from_argb(0xFFFCE100);
    p.semantic_background_informational = Color::from_argb(0x08FFFFFF);
    p.semantic_background_success       = Color::from_argb(0xFF393D1B);
    p.semantic_background_caution       = Color::from_argb(0xFF433519);
    p.semantic_background_critical      = Color::from_argb(0xFF442726);

    p.tabular_header_background = Color::from_argb(0x33FFFFFF);
    p.tabular_grid_line         = Color::from_argb(0x19FFFFFF);
    // Its secondary subtle fill, as on Windows 11.
    p.tabular_row_alternating = Color::from_argb(0x19FFFFFF);
    return p;
}

} // namespace ui
