// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"

namespace ui
{

/// The Metro (Windows 8.1) base colors and border widths, as the Windows 8.1 light and dark
/// themes define them.
///
/// `Palette` is the Fluent counterpart and `ClassicPalette` the classic one. `Theme::metro()`
/// derives every control's colors from this flat set of values. Start from `metro_dark()` or
/// `metro_light()`. A default-constructed palette has every color zero apart from `accent`.
/// The era derives no accent shades: `accent`, `accent_hover` and `accent_pressed` are set
/// separately, so a new accent means setting all three.
///
/// Example:
/// @code
/// ui::MetroPalette palette = ui::MetroPalette::metro_dark();
/// palette.accent         = ui::Color::from_argb(0xFF008A00); // app accent instead of violet
/// palette.accent_hover   = ui::Color::from_argb(0xFF1FA01F);
/// palette.accent_pressed = ui::Color::from_argb(0xFF006E00);
///
/// ui::Theme theme = ui::Theme::metro(palette, /*dark=*/true);
/// window.pin_theme(std::move(theme)); // every control picks up the new colors
/// @endcode
struct MetroPalette
{
    // ---- Page-level ----
    Color page_background;      ///< Page fill.
    Color foreground;           ///< Primary text (0xDE alpha).
    Color foreground_secondary; ///< Secondary text (0x99 alpha).
    Color foreground_pressed;   ///< Pressed-state text (0x66 alpha).

    // ---- Button ----
    /// Rest fill of a `ui::ButtonControl`. Transparent in dark, where the button is an outline,
    /// and a gray wash in light.
    Color button_background;
    /// Outline color, drawn `button_border_width` DIPs thick. Light draws no outline.
    Color button_border;
    Color button_foreground;       ///< Label color at rest and on hover.
    Color button_hover_background; ///< Fill while the pointer is over the button.
    /// Fill while pressed. The button inverts: white in dark, black in light.
    Color button_pressed_background;
    Color button_pressed_foreground;  ///< Label color while pressed, the inverse of the fill.
    Color button_disabled_background; ///< Fill of a disabled button.
    Color button_disabled_border;     ///< Outline of a disabled button.
    Color button_disabled_foreground; ///< Label of a disabled button.

    // ---- CheckBox/RadioButton/ToggleSwitch (the toggle box/ring) ----
    Color toggle_box_background; ///< Fill of the CheckBox box and the RadioButton ring.
    /// Outline of the box and ring, drawn `indicator_border_width` DIPs thick.
    Color toggle_box_border;
    Color toggle_glyph; ///< The check mark and dot color, drawn on `toggle_box_background`.

    // ---- Field/surface roles ----
    /// Text-field fill. White in both themes, since text boxes stay light in the dark theme.
    Color field_background;
    Color field_text;   ///< Text color inside a text field.
    Color field_border; ///< Text-field outline.
    /// Menu and flyout fill. White in both themes, since every popup uses the light colors.
    Color menu_background;
    Color menu_text; ///< Menu item text color.
    /// A hovered menu item's fill, a flat light-gray wash with black text. The accent marks
    /// selection in lists and combo boxes, and a menu item is never selected.
    Color menu_highlight;
    Color menu_highlight_text; ///< Text color of the hovered menu item on `menu_highlight`.
    Color tooltip_background;  ///< Tooltip fill (white in both themes).
    Color tooltip_text;        ///< Tooltip text color.
    Color tooltip_border;      ///< Tooltip frame, 2 DIP wide.
    Color hyperlink;           ///< Hyperlink text color, a lighter violet in the dark theme.
    Color separator;           ///< Menu and list separator line color.

    // ---- scroll bar: the same in both themes ----
    /// Scroll bar track fill. The scroll bar is the same light gray in both themes.
    Color scrollbar_track;
    Color scrollbar_thumb; ///< Scroll bar thumb fill, the same in both themes.
    Color scrollbar_arrow; ///< Scroll bar arrow glyph color, the same in both themes.

    // ---- Accent, used as is (no derived shades) ----
    /// The accent, used as is for selection, checked toggles and progress. Defaults to the
    /// Windows 8.1 violet `#FF4617B4`.
    Color accent = Color::from_argb(0xFF4617B4);
    Color accent_hover;   ///< Accent under the pointer, set separately from `accent`.
    Color accent_pressed; ///< Accent while pressed, set separately from `accent`.

    /// Button outline thickness in DIPs. The dark theme's button is a 2 DIP outline with no
    /// fill, and the light theme's is a borderless flat fill (0).
    float button_border_width = 2.0f;
    /// CheckBox and RadioButton outline thickness in DIPs, the inverse of the button's: 0 in
    /// dark, 2 in light.
    float indicator_border_width = 0.0f;

    /// The Windows 8.1 dark theme, its default: a `#FF1D1D1D` page, outline buttons and white menus.
    [[nodiscard]] static MetroPalette metro_dark();

    /// The Windows 8.1 light theme: a white page, borderless gray-filled buttons, and 2 DIP
    /// CheckBox and RadioButton outlines.
    [[nodiscard]] static MetroPalette metro_light();

    /// Member-wise equality over every field.
    friend bool operator==(const MetroPalette&, const MetroPalette&) = default;
};

} // namespace ui
