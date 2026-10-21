// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/render/Brush.hpp"

namespace ui
{

/// The Fluent colors a Theme is derived from, one field per role: text, control fills,
/// strokes, accent, surfaces and status colors.
///
/// Theme::fluent() derives every control's colors from a Palette. fluent_light() and
/// fluent_dark() give the Windows 11 colors, and fluent1_light() and fluent1_dark() the
/// Windows 10 ones. A custom palette starts from one of them and changes fields. A Palette is
/// plain data and cheap to copy.
///
/// Example:
/// @code
/// ui::Palette palette = ui::Palette::fluent_dark();
/// palette.accent_fill_default = ui::Color::from_argb(0xFF8764B8); // a purple accent
/// palette.accent_text_primary = ui::Color::from_argb(0xFFD4C3F0);
///
/// ui::Theme theme = ui::Theme::fluent(palette);
/// window.pin_theme(std::move(theme)); // accent buttons, toggles and links now use the purple
/// @endcode
struct Palette
{
    /// Whether this is a dark palette. A few shapes differ between light and dark, and
    /// `Theme::fluent()` reads them from this flag. The main one is the neutral elevation border,
    /// whose gradient is flipped vertically in light and not in dark. Set by the factories, and
    /// false in a default-constructed Palette.
    bool dark = false;

    // ---- Text ----
    /// Body text and labels at rest.
    Color text_fill_primary;
    /// Captions, placeholders, secondary labels and glyphs.
    Color text_fill_secondary;
    /// The faintest readable text, below secondary.
    Color text_fill_tertiary;
    /// The label of a disabled control.
    Color text_fill_disabled;
    /// Text and glyphs on an accent fill, such as an accent button's label or a checked box's
    /// check mark. White in light, black in dark.
    Color text_on_accent_primary;
    /// An accent button's label while pressed.
    Color text_on_accent_secondary;
    /// The label of a disabled accent button.
    Color text_on_accent_disabled;

    // ---- Control fills (rest, hover, pressed, disabled) ----
    /// A standard control (Button, TextBox, ComboBox) at rest.
    Color control_fill_default;
    /// The same control under the pointer.
    Color control_fill_secondary;
    /// The same control pressed.
    Color control_fill_tertiary;
    /// A fourth control fill, a stronger wash than the pressed one.
    Color control_fill_quaternary;
    /// A disabled standard control.
    Color control_fill_disabled;
    /// A focused text field (TextBox, PasswordBox, RichEditBox, AutoSuggestBox, NumberBox). The
    /// one opaque fill of this set in light, so the field turns from a translucent wash into a
    /// solid surface on focus. Light's is plain white, which differs from
    /// `solid_background_tertiary`'s `#F9F9F9`.
    Color control_fill_input_active;

    /// The fill of an unchecked CheckBox, RadioButton or ToggleSwitch indicator at rest, a
    /// slightly recessed surface distinct from `control_fill_*`. The three `control_alt_fill_*`
    /// fields after it are hover, pressed and disabled. Three values are easy to guess wrong:
    ///   * Dark's rest fill is translucent black (`#19000000`), so a dark unchecked box at rest
    ///     is darker than the page.
    ///   * In light, rest is the fainter 2 % wash and hover the 6 % one.
    ///   * The disabled fill is transparent in both themes, so a disabled box shows only its
    ///     stroke.
    Color control_alt_fill_secondary;
    /// The unchecked indicator under the pointer.
    Color control_alt_fill_tertiary;
    /// The unchecked indicator pressed.
    Color control_alt_fill_quaternary;
    /// The unchecked indicator while disabled, transparent in both Windows 11 themes.
    Color control_alt_fill_disabled;

    // ---- Subtle fills (borderless buttons, list/menu items) ----
    /// A subtle control, such as a borderless button or a list or menu row, at rest.
    Color subtle_fill_transparent;
    /// A subtle control under the pointer.
    Color subtle_fill_secondary;
    /// A subtle control pressed.
    Color subtle_fill_tertiary;

    /// A control sitting directly on the window's Mica backdrop, such as a tab of a Tabs
    /// control, under the pointer. It replaces the `subtle_fill_*` colors there. The pressed
    /// fill is a lighter, mostly opaque layer (light 0xB3FFFFFF, dark 0x733A3A3A), so a tab
    /// brightens on press. At rest the control is transparent, which needs no field.
    Color on_backdrop_hover;
    /// That control pressed.
    Color on_backdrop_pressed;

    // ---- Accent fills ----
    /// Accent buttons, checked toggles and selection indicators. The value is the default
    /// accent. A Theme resolved from the OS replaces it with the user's accent.
    Color accent_fill_default;
    /// An accent-filled control while disabled, a neutral wash.
    Color accent_fill_disabled;

    // ---- Accent text (a hyperlink button's four foregrounds) ----
    /// Link text at rest. The four `accent_text_*` fields cover rest, hover, pressed and
    /// disabled link text. They use other shades of the accent than the fill above: in light,
    /// rest, hover and pressed are `AccentRamp::dark2`, `dark3` and `dark1`, and in dark they are
    /// `light3`, `light3` and `light2`. Windows 10 has no such shades, so fluent1_light() and
    /// fluent1_dark() fill these with its own link colors.
    Color accent_text_primary;
    /// Link text under the pointer.
    Color accent_text_secondary;
    /// Link text pressed.
    Color accent_text_tertiary;
    /// A disabled link's text.
    Color accent_text_disabled;

    // ---- Strokes ----
    /// The hairline border of a standard control, one stop of its elevation-border gradient.
    Color control_stroke_default;
    /// The other stop of a standard control's elevation-border gradient.
    Color control_stroke_secondary;
    /// The first stop of an accent control's elevation-border gradient. This pair differs from
    /// the neutral `control_stroke_default` and `control_stroke_secondary`. The second stop is a
    /// 40 % black hairline in light, against the neutral 16 %, and this one is translucent white
    /// in both themes, where the neutral one flips between black and white.
    Color control_stroke_on_accent_default;
    /// The second stop of that gradient, the darker edge of an accent control.
    Color control_stroke_on_accent_secondary;
    /// The border of an unchecked CheckBox, RadioButton or ToggleSwitch. It is much bolder than
    /// `control_stroke_default`, so an unchecked toggle still looks interactive.
    Color control_strong_stroke_default;
    /// That border on a disabled control.
    Color control_strong_stroke_disabled;
    /// The outer 2px ring of the keyboard focus visual.
    Color focus_stroke_outer;
    /// The inner 1px ring of the focus visual, the outer ring's inverse.
    Color focus_stroke_inner;
    /// The border of card-shaped surfaces such as InfoBar: `#0F000000` in light, `#19000000` in dark.
    Color card_stroke_default;
    /// A flat 1px rule between grouped items, as in menu and Toolbar separators. Light
    /// 0x0F000000, dark 0x15FFFFFF.
    Color divider_stroke_default;
    /// The 1px frame of a surface that floats over the page, such as TeachingTip. The same
    /// `#66757575` in light and dark.
    Color surface_stroke_default = Color::from_argb(0x66757575);
    /// The top area of a DialogControl, opaque white in light and a 5 % white wash in dark.
    /// Transparent in the Windows 10 palettes.
    Color layer_fill_alt = colors::Transparent;
    /// NavigationShell's content area, `#80FFFFFF` in light and `#4C3A3A3A` in dark. Transparent in
    /// the Windows 10 palettes.
    Color layer_fill_default = colors::Transparent;
    /// An opaque control fill, such as the inner ring of a selected TileViewItem. `#FFFFFF` in
    /// light, `#454545` in dark.
    Color control_solid_fill_default = colors::White;
    /// A control drawn over a picture, such as the unchecked multi-select box of a TileViewItem.
    /// This field and the three after it are rest, hover, pressed and disabled: light
    /// `#C9FFFFFF`, `#F3F3F3`, `#EBEBEB` and `#00FFFFFF`, dark `#B31C1C1C`, `#1A1A1A`, `#131313` and `#1E1E1E`.
    Color control_on_image_fill_default = Color::from_argb(0xC9FFFFFF);
    /// The on-image fill under the pointer.
    Color control_on_image_fill_secondary = Color::from_argb(0xFFF3F3F3);
    /// The on-image fill pressed.
    Color control_on_image_fill_tertiary = Color::from_argb(0xFFEBEBEB);
    /// The on-image fill while disabled.
    Color control_on_image_fill_disabled = Color::from_argb(0x00FFFFFF);
    /// A hovered TileViewItem's 1px outline, `#37000000` in both themes.
    Color control_stroke_on_accent_tertiary = Color::from_argb(0x37000000);

    // ---- Backgrounds / surfaces ----
    /// The opaque window and page background.
    Color solid_background_base;
    /// A slightly recessed opaque surface.
    Color solid_background_secondary;
    /// A slightly raised opaque surface.
    Color solid_background_tertiary;
    /// The solid color of the in-app acrylic material: `#2C2C2C` in dark, `#F9F9F9` in light.
    /// Popups that close on an outside click, such as menus, flyouts, the ComboBox list and
    /// ToolTip, draw in acrylic. This color replaces the acrylic whenever it is off: with
    /// transparency effects disabled, in battery saver, on a low-end GPU or in high contrast.
    /// Both values match the Windows 11 system popup menu. In dark it differs from
    /// `solid_background_tertiary` (`#282828`).
    Color acrylic_fallback_default;
    /// The in-app acrylic material as an AcrylicBrush. Light has tint `#FCFCFC` at opacity 0 and
    /// luminosity opacity 0.85, dark has `#2C2C2C` at 0.15 and 0.96, and both fall back to
    /// `acrylic_fallback_default`. Drawn with Renderer::draw_geometry(), it blurs what is
    /// behind it.
    [[nodiscard]] AcrylicBrush acrylic_in_app_default() const
    {
        AcrylicBrush brush;
        brush.tint_color              = dark ? Color::from_argb(0xFF2C2C2C) : Color::from_argb(0xFFFCFCFC);
        brush.tint_opacity            = dark ? 0.15f : 0.0f;
        brush.tint_luminosity_opacity = dark ? 0.96f : 0.85f;
        brush.fallback_color          = acrylic_fallback_default;
        return brush;
    }
    /// The fill of a card surface, such as an Expander's header, paired with
    /// `card_stroke_default`. 0xB3FFFFFF in light, 0x0DFFFFFF in dark, where
    /// `control_fill_default` is 0x0FFFFFFF.
    Color card_background_default;
    /// A card's recessed inner region, such as an expanded Expander's content panel.
    Color card_background_secondary;

    // ---- Semantic / status fills (InfoBar/InfoBadge) ----
    /// The success icon of an InfoBar and a success InfoBadge. The `semantic_fill_*` colors are
    /// strong marks for icons and badges. The `semantic_background_*` colors are the much
    /// fainter washes that fill an InfoBar's body. The informational severity has no fill color
    /// because its icon uses the accent, so InfoBar reads `accent_fill_default` for it.
    Color semantic_fill_success;
    /// The warning icon of an InfoBar and a caution InfoBadge.
    Color semantic_fill_caution;
    /// The error icon of an InfoBar and a critical InfoBadge.
    Color semantic_fill_critical;
    /// The opaque gray of an informational InfoBadge, `#8A8A8A` in light, `#9D9D9D` in dark.
    Color semantic_fill_solid_neutral;
    /// An informational InfoBar's body.
    Color semantic_background_informational;
    /// A success InfoBar's body.
    Color semantic_background_success;
    /// A warning InfoBar's body.
    Color semantic_background_caution;
    /// An error InfoBar's body.
    Color semantic_background_critical;

    // ---- Tabular row/header surfaces ----
    /// The header band of tabular controls such as TableView. A translucent wash of about
    /// 15 % alpha, above `control_stroke_default`'s 6 to 7 %, so it stands out over row banding.
    Color tabular_header_background;
    /// The gridlines between rows and cells of tabular controls, stronger than
    /// `divider_stroke_default` so they stay visible at 1px over alternating rows.
    Color tabular_grid_line;
    /// Every second row of a tabular control with banding on. It has the same values as
    /// `subtle_fill_secondary`.
    Color tabular_row_alternating;

    /// The Windows 11 light colors. The accent fields hold shades of the default accent
    /// (`#005A9E` for the fill). A theme resolved from the OS replaces them with the user's accent.
    [[nodiscard]] static Palette fluent_light();

    /// The Windows 11 dark colors, with the default accent's light shades (`#76B9ED` for the
    /// fill) in the accent fields.
    [[nodiscard]] static Palette fluent_dark();

    // ---- Fluent 1 (Windows 10, 2017-2021) ------------------------------------------------

    /// The Windows 10 light colors in the same roles: a pure white page, 20 % black button
    /// fills that lighten on hover, unfilled toggles, and the accent `#0078D4`.
    [[nodiscard]] static Palette fluent1_light();

    /// The Windows 10 dark colors. Its translucent text and fill colors are the exact inverse of
    /// fluent1_light()'s. Its opaque grays are not: they have their own order per theme, so a
    /// gray that is darker than another in light can be lighter in dark.
    [[nodiscard]] static Palette fluent1_dark();
};

} // namespace ui
