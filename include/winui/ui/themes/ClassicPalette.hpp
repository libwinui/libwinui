// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"

#include <optional>

namespace ui
{

/// The classic 3-D bevel ramp: the four rings draw_bevel_border() draws around a raised or
/// sunken classic control.
///
/// Usually derived from a face color with classic_bevel_ramp_from_face(). A scheme that states
/// its own ramp, such as High Contrast Black, sets ClassicPalette::bevel instead.
///
/// Example:
/// @code
/// // In a custom control's render_override(): a sunken well on Windows 95's gray, whose ramp is
/// // `#DFDFDF`, white, `#808080` and black.
/// class WellControl : public ui::Element
/// {
/// protected:
///     void render_override(ui::Renderer& renderer, const ui::RenderContext&) const override
///     {
///         const ui::ClassicBevelRamp ramp = ui::classic_bevel_ramp_from_face(ui::Color::from_argb(0xFFC0C0C0));
///         ui::draw_bevel_border(renderer, bounds(), ui::BorderStyle::Sunken, ramp.light, ramp.highlight,
///                               ramp.shadow, ramp.dark_shadow, 1.0f);
///     }
/// };
/// @endcode
struct ClassicBevelRamp
{
    Color light;       ///< The outer top/left ring, a step lighter than the face.
    Color highlight;   ///< The inner top/left ring, the brightest edge (white on the standard grays).
    Color shadow;      ///< The inner bottom/right ring, a mid gray on the standard grays.
    Color dark_shadow; ///< The outer bottom/right ring, the darkest edge.

    /// Member-wise equality.
    friend bool operator==(const ClassicBevelRamp&, const ClassicBevelRamp&) = default;
};

/// A classic scheme's title bars and window borders: what a window under `Frame::Themed` draws its
/// caption and frame with. Windows 98 added the gradient pair; a scheme without one has both ends
/// of each caption the same color.
///
/// Example:
/// @code
/// // A header strip in Windows 2000's active caption colors: white on navy `#0A246A`, the
/// // left end of a gradient whose right end is `#A6CAF0`.
/// const ui::ClassicPalette  scheme  = ui::ClassicPalette::windows_standard_2000();
/// const ui::ClassicCaption& caption = *scheme.caption;
/// window.set_content(ui::Border{
///     .background = caption.active,
///     .padding    = ui::Symmetric{.horizontal = 8.0f, .vertical = 4.0f},
///     .content    = ui::Text{.text = "Display Properties", .foreground = caption.text},
/// });
/// @endcode
struct ClassicCaption
{
    Color active;            ///< The active window's caption, at its left end.
    Color gradient_active;   ///< The active caption's right end.
    Color inactive;          ///< An inactive window's caption, at its left end.
    Color gradient_inactive; ///< The inactive caption's right end.
    Color text;              ///< The active window's title text.
    Color inactive_text;     ///< An inactive window's title text.
    Color active_border;     ///< The active window's sizing frame.
    Color inactive_border;   ///< An inactive window's sizing frame.
    /// The scheme's caption button cell, in pixels at 96 DPI: 18 in Windows Standard, 20 in High
    /// Contrast Black. The caption is a pixel taller than this, and a caption button is 2 pixels
    /// narrower and 4 shorter.
    float button_cell = 18.0f;

    /// Member-wise equality.
    friend bool operator==(const ClassicCaption&, const ClassicCaption&) = default;
};

/// Lightens `c` the way the classic 3-D ramp is derived from a face color. The color's
/// luminosity moves toward white by up to half the remaining distance, and `percent`, clamped
/// to [0,1], scales that move. Hue, saturation and alpha are kept.
///
/// Example:
/// @code
/// const ui::Color face  = ui::Color::from_rgba(128, 64, 128); // a custom face
/// const ui::Color light = ui::classic_lighter(face, 0.5f);    // the bevel's outer light ring
/// const ui::Color dark  = ui::classic_darker(face, 1.0f);     // the bevel's outer dark ring
/// @endcode
[[nodiscard]] Color classic_lighter(const Color& c, float percent = 1.0f);
/// Darkens `c` the same way classic_lighter() lightens it. The luminosity moves toward black by
/// up to a third, scaled by `percent`, which is clamped to [0,1]. Hue, saturation and alpha are
/// kept.
[[nodiscard]] Color classic_darker(const Color& c, float percent = 1.0f);

/// Derives the whole classic 3-D ramp from one face color. `light` is
/// `classic_lighter(face, 0.5)`, `highlight` is `classic_lighter(face, 1.0)`, `shadow` is
/// `classic_darker(face, 0.5)` and `dark_shadow` is `classic_darker(face, 1.0)`.
///
/// When `face` is exactly one of the two standard grays, the ramp that era ships is returned
/// as is, because neither can be derived:
///   - `0xC0C0C0` (Windows 95 and 98 SE): `0xDFDFDF`, white, `0x808080` and black. Its light
///     ring sits between the face and the highlight.
///   - `0xD4D0C8` (Windows 2000, and XP's Classic scheme): the face itself, white, `0x808080`
///     and `0x404040`. The light ring equals the face, and the dark ring is softer than black.
///
/// Any other face color, as in a custom scheme, gets the computed ramp.
///
/// Example:
/// @code
/// // In a custom control's render_override(): a raised teal panel. Teal is no canonical gray, so its
/// // ramp is computed, with `highlight` equal to classic_lighter(face, 1.0f).
/// class TealPanelControl : public ui::Element
/// {
/// protected:
///     void render_override(ui::Renderer& renderer, const ui::RenderContext&) const override
///     {
///         const ui::Color            face = ui::Color::from_rgba(0, 128, 128);
///         const ui::ClassicBevelRamp ramp = ui::classic_bevel_ramp_from_face(face);
///         renderer.fill_rounded_rect(bounds(), 0.0f, face);
///         ui::draw_bevel_border(renderer, bounds(), ui::BorderStyle::Raised, ramp.light, ramp.highlight,
///                               ramp.shadow, ramp.dark_shadow, 1.0f);
///     }
/// };
///
/// // Windows 2000's gray gets its published ramp instead: `#D4D0C8` (the face itself), white,
/// // `#808080` and `#404040`.
/// const ui::ClassicBevelRamp win2000 = ui::classic_bevel_ramp_from_face(ui::Color::from_argb(0xFFD4D0C8));
/// @endcode
[[nodiscard]] ClassicBevelRamp classic_bevel_ramp_from_face(const Color& face);

/// The classic era's color scheme: the colors a classic 3-D look is drawn with. Palette is the
/// Fluent counterpart.
///
/// Theme::classic() derives a whole Theme, every control's visuals included, from one of these.
/// A Windows 95, 98 or 2000 look, Windows Classic on XP, or a custom scheme is one literal plus
/// that derivation, with no hand-assembled `ButtonVisual`s. `face_text`, `window_text` and
/// `menu_text` are separate fields, so a scheme with a dark face, such as classic "Eggplant",
/// can use light text.
///
/// Example:
/// @code
/// // A shipped scheme:
/// window.pin_theme(ui::Theme::classic(ui::ClassicPalette::windows_standard(),
///                                         /*hot_track=*/false, "Microsoft Sans Serif"));
///
/// // A user-authored one, derived from its face and then touched up:
/// ui::ClassicPalette scheme = ui::ClassicPalette::from_face(ui::Color::from_rgba(128, 128, 64));
/// scheme.selection = ui::Color::from_rgba(128, 0, 128);
/// window.pin_theme(ui::Theme::classic(scheme, /*hot_track=*/true, "Microsoft Sans Serif"));
/// @endcode
struct ClassicPalette
{
    Color face;      ///< Control and dialog surfaces, and the color the bevel ramp is derived from.
    Color face_text; ///< Text on `face`.

    Color window;       ///< The surface of edit fields and list boxes.
    Color window_text;  ///< Text on `window`.
    Color window_frame; ///< 1px hard frames.

    Color selection;      ///< Selected items and selected text.
    Color selection_text; ///< Text on `selection`.
    Color gray_text;      ///< Disabled text.

    Color menu;      ///< The menu and menu bar background.
    Color menu_text; ///< Menu item text.

    Color info_bk;   ///< The tooltip background, the pale yellow `#FFFFE1` in the standard schemes.
    Color info_text; ///< Tooltip text.
    Color hot_light; ///< Hyperlinks.

    /// The bevel ramp, when the scheme states its own. nullopt, the default, derives it from
    /// `face` with classic_bevel_ramp_from_face(). High-contrast schemes need this: High Contrast
    /// Black pairs a black face with a white outer dark ring and gray inner rings, which no
    /// lightening or darkening of black produces.
    std::optional<ClassicBevelRamp> bevel{};

    /// The caption and border colors, when the scheme states them. nullopt, the default, derives
    /// them as Windows 95 and 98's Windows Standard has them. The active caption is `selection`
    /// and an inactive one is the bevel ramp's `shadow`, both without a gradient. The active title
    /// is `selection_text`, an inactive title is `face`, and both borders are `face`.
    std::optional<ClassicCaption> caption{};

    /// Member-wise equality, including `bevel` and `caption`.
    friend bool operator==(const ClassicPalette&, const ClassicPalette&) = default;

    /// The default "Windows Standard" scheme as Windows 95 and Windows 98 SE ship it: a
    /// `0xC0C0C0` face and a navy `0x000080` selection. The two versions use the same colors.
    [[nodiscard]] static ClassicPalette windows_standard();

    /// "Windows Standard" as Windows 2000 and XP's Classic scheme ship it: the warmer `0xD4D0C8`
    /// face, a `0x0A246A` selection, a `0x000080` hyperlink, the flatter bevel ramp and a
    /// gradient caption. Combining parts of it with windows_standard() gives a look no Windows
    /// version had.
    [[nodiscard]] static ClassicPalette windows_standard_2000();

    /// High Contrast Black as Windows 98 SE ships it. White on black, a purple `#800080`
    /// selection, green `#00FF00` disabled text, a pinkish `#D0B0B8` outer light bevel ring and
    /// pure-blue `#0000FF` hyperlinks. The last two look odd but are the shipped values.
    [[nodiscard]] static ClassicPalette high_contrast_black_98();

    /// High Contrast Black as Windows 2000 and XP ship it, the scheme both switch to when high
    /// contrast is turned on. It is the Windows 98 scheme with a white outer light bevel ring,
    /// purple hyperlinks and white-on-black tooltips.
    [[nodiscard]] static ClassicPalette high_contrast_black_2000();

    /// Monochrome, the Windows 3.1 scheme closest to high contrast: black on white, silver
    /// buttons, a black selection and title bar. Windows 3.1 has no tooltip or hyperlink color,
    /// so tooltips and hyperlinks use the window colors. Pass it to
    /// `Theme::classic(..., /*win3x_edge=*/true)` for the Windows 3.x edges.
    [[nodiscard]] static ClassicPalette monochrome_3x();

    /// A custom scheme derived from `face`. The bevel comes from classic_bevel_ramp_from_face(),
    /// and the rest takes the colors every classic scheme shares: a white window, a navy
    /// selection, black text and the pale yellow tooltip. "Windows 95 with the Eggplant scheme" is
    /// `ClassicPalette::from_face(eggplant_face)`.
    [[nodiscard]] static ClassicPalette from_face(const Color& face);
};

} // namespace ui
