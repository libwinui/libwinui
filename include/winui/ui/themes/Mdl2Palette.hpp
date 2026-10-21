// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"

namespace ui
{

/// The 24 base colors of the MDL2 theme. The `page_*` colors are the page color and the `text_*`
/// colors the text color, each named after its opacity: 20, 40, 60, 80 or 100 %. The `list_*`
/// colors are the text color at 10 and 20 %. The two sets swap between light and dark.
/// Keep them translucent. A disabled or secondary color is the text color blended over whatever
/// is behind it, and an opaque gray mixed against the page looks wrong over a selection or an
/// image. Most `chrome` colors are opaque grays that differ between the themes, which gives
/// MDL2 chrome its weight without an accent.
///
/// Theme::mdl2() derives every control color from one of these palettes plus the accent.
///
/// Example:
/// @code
/// // White text on a black page, with a blue accent.
/// const ui::Mdl2Palette palette = ui::Mdl2Palette::mdl2_dark();
/// window.pin_theme(ui::Theme::mdl2(palette, /*dark=*/true, ui::Color::from_argb(0xFF0078D7)));
///
/// // Secondary text in the text color at 60 %, blended over whatever is behind it.
/// window.set_content(ui::VerticalStack{
///     .content = {
///         ui::Text{.text = "Now playing"},
///         ui::Text{.text = "Track 3 of 12", .foreground = palette.text_60},
///     },
/// });
/// @endcode
struct Mdl2Palette
{
    /// The page color at full opacity: white in light, black in dark.
    Color page_100;
    /// The page color at 20 %.
    Color page_20;
    /// The page color at 60 %, the inner focus ring.
    Color page_60;
    /// The page color at 80 %.
    Color page_80;
    /// The page color at 40 %.
    Color page_40;
    /// The text color at full opacity: black in light, white in dark.
    Color text_100;
    /// The text color at 20 %, a button's fill at rest.
    Color text_20;
    /// The text color at 60 %, secondary text and borders.
    Color text_60;
    /// The text color at 80 %, the border of an unchecked check box or radio button.
    Color text_80;
    /// The text color at 40 %, disabled text and a pressed button's fill.
    Color text_40;
    /// An opaque near-black in light (`#171717`) and near-white in dark (`#F2F2F2`).
    Color chrome_contrast;
    /// Opaque black in both themes.
    Color black_100;
    /// Black at 20 % in both themes.
    Color black_20;
    /// Black at 40 % in both themes.
    Color black_40;
    /// Black at 80 % in both themes.
    Color black_80;
    /// An opaque gray for disabled chrome such as a disabled slider: `#CCCCCC` in light, `#333333` in dark.
    Color chrome_disabled_faint;
    /// The other disabled-chrome gray, also a text field's border: `#7A7A7A` in light, `#858585` in dark.
    Color chrome_disabled_strong;
    /// The strongest opaque chrome gray, the window border and a pressed slider thumb: `#CCCCCC` in
    /// light, `#767676` in dark.
    Color chrome_strong;
    /// The faintest opaque chrome surface: `#F2F2F2` in light, `#171717` in dark.
    Color chrome_faint;
    /// A mid opaque chrome surface, the title bar: `#E6E6E6` in light, `#1F1F1F` in dark.
    Color chrome_mid;
    /// Flyout, menu and tooltip backgrounds: `#F2F2F2` in light, `#2B2B2B` in dark.
    Color chrome_flyout;
    /// Opaque white in both themes, text on an accent fill.
    Color white_100;
    /// The text color at 10 %, a list row or menu item under the pointer.
    Color list_hover;
    /// The text color at 20 %, a list row pressed.
    Color list_pressed;

    /// The page background: `#FFFFFFFF` in light and `#FF000000` in dark. A field of its own,
    /// although it equals `page_100`.
    Color page_background;

    /// The light palette: black text colors over a white page, and light-gray chrome.
    [[nodiscard]] static Mdl2Palette mdl2_light();

    /// The dark palette: white text colors over a black page. It is the era's default theme. The
    /// era's guidance is light for productivity apps, dark for media apps, and the system
    /// setting if neither fits.
    [[nodiscard]] static Mdl2Palette mdl2_dark();

    /// Field-by-field equality.
    friend bool operator==(const Mdl2Palette&, const Mdl2Palette&) = default;
};

} // namespace ui
