// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"

#include <optional>
#include <utility>

namespace ui
{

/// The colors of an early Windows scheme: ten in Windows 1.x, thirteen in Windows 2.x. The three
/// 2.x-only colors are `std::optional` and empty in a Windows 1.x palette. Where the theme needs
/// a border color and a 1.x palette has none, it uses `window_frame`.
///
/// Every value in the four factories below is the one that Windows release shipped.
///
/// A palette is the input to Theme::early(), which derives the whole early-Windows theme from it.
/// Replace the whole palette instead of single colors. The four palettes are four distinct
/// looks, and mixing colors between them gives a scheme no Windows release had.
///
/// Example:
/// @code
/// // The default Windows 2.03 color look for the whole window.
/// window.pin_theme(ui::Theme::early(ui::EarlyPalette::windows_2x_color()));
///
/// // Windows 1.01 on a monochrome display. Its row has no border slots, so the window border
/// // is drawn in `window_frame`.
/// window.pin_theme(ui::Theme::early(ui::EarlyPalette::windows_1x_mono()));
/// @endcode
struct EarlyPalette
{
    Color scrollbar;        ///< The scroll bar's trough. The thumb is drawn separately.
    Color background;       ///< The desktop.
    Color active_caption;   ///< The active window's caption.
    Color inactive_caption; ///< An inactive window's caption.
    Color menu;             ///< The menu bar and menus.
    Color window;           ///< The page, dialogs included.
    Color window_frame;     ///< Every 1px frame, the era's only depth cue.
    Color menu_text;        ///< Menu text.
    Color window_text;      ///< Text on `window`.
    Color caption_text;     ///< Caption title text.

    std::optional<Color> active_border{};   ///< The active window's border (Windows 2.x).
    std::optional<Color> inactive_border{}; ///< An inactive window's border (Windows 2.x).
    std::optional<Color> app_workspace{};   ///< The background behind MDI child windows (Windows 2.x).

    /// Color-for-color equality, including whether each 2.x-only color is present.
    friend bool operator==(const EarlyPalette&, const EarlyPalette&) = default;

    /// Windows 2.03 on a color display: the default, and the one to pick if only one
    /// early-Windows palette is wanted. Windows 2.03 uses it on every color adapter. A cyan
    /// desktop and cyan menu bar, and a blue caption with white text over a white page.
    [[nodiscard]] static EarlyPalette windows_2x_color();

    /// Windows 2.03 on a monochrome display, the same on every monochrome adapter.
    [[nodiscard]] static EarlyPalette windows_2x_mono();

    /// Windows 1.01 on a monochrome display: the safest single choice for a Windows 1.x look.
    /// All of that release's monochrome display drivers use these colors.
    ///
    /// The three 2.x-only colors stay `nullopt`.
    [[nodiscard]] static EarlyPalette windows_1x_mono();

    /// Windows 1.01 on a color display, as its EGA drivers ship it: a red scroll-bar trough on a
    /// white window under a blue caption.
    ///
    /// Photos of the Windows 1.01 retail box show yellow-green troughs and pale cyan windows
    /// instead, likely a scheme picked for the shot.
    [[nodiscard]] static EarlyPalette windows_1x_color();
};

/// Reverse video, the era's only way to show a selected state, which is why the palettes above
/// have no selection color. A selected run swaps its foreground and background. Returns the
/// swapped pair as `{background, foreground}`.
///
/// The swap keeps the palette's own colors. Inverting each pixel instead would turn the 2.03
/// color palette's cyan menu red. On monochrome palettes both give the same result.
///
/// Example:
/// @code
/// const ui::EarlyPalette p         = ui::EarlyPalette::windows_2x_color();
/// const ui::Rect         item_rect = {0, 0, 80, 20};
/// const auto [fill, text] = ui::early_reverse_video(p.menu, p.menu_text); // a selected menu item
/// renderer.fill_rounded_rect(item_rect, 0.0f, fill);
/// renderer.draw_text("Open...", item_rect, text, theme.text_style);
/// @endcode
[[nodiscard]] constexpr std::pair<Color, Color> early_reverse_video(const Color& background, const Color& foreground)
{
    return {foreground, background};
}

/// The era's gray as one flat color. The era has no disabled-text color and grays disabled items
/// with a 50 % checkerboard of ink and paper. `Renderer` has no pattern fill, so this returns the
/// checkerboard's average, the midpoint of the two colors. Black on white gives `#808080`, the
/// gray the Windows 1.01 monochrome palette uses for the desktop and inactive captions.
///
/// Only the text grays out. A disabled push button keeps its frame, corners and border and grays
/// its label.
///
/// Example:
/// @code
/// const ui::EarlyPalette p          = ui::EarlyPalette::windows_1x_mono();
/// const ui::Color        gray       = ui::early_dither_gray(p.window_text, p.window); // ~`#808080`
/// const ui::Rect         label_rect = {0, 0, 80, 20};
/// const bool             enabled    = false;
/// renderer.draw_text("Cancel", label_rect, enabled ? p.window_text : gray, theme.text_style);
/// @endcode
[[nodiscard]] constexpr Color early_dither_gray(const Color& ink, const Color& paper)
{
    return lerp(ink, paper, 0.5f);
}

} // namespace ui
