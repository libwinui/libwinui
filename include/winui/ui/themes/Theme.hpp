// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Duration.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/CubicBezier.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Paint.hpp"
#include "winui/ui/themes/Palette.hpp"

#include <functional>
#include <memory>
#include <optional>

#include <cstdint>

namespace ui
{

struct ClassicPalette;
struct EarlyPalette;
struct Mdl2Palette;
struct MetroPalette;

/// The interaction state a control derives from its input flags. Focus is not one of them. The
/// focus visual is a separate ring a control draws on top of whichever of these four states it
/// is in, so a focused and hovered control keeps its hover look.
enum class ButtonState
{
    Normal,   ///< At rest.
    Hover,    ///< The pointer is over the control.
    Pressed,  ///< Held down by the pointer or the keyboard.
    Disabled, ///< The control is disabled; wins over the other three.
};

/// A recessed well inside an indicator's border, as in Aero's check box and radio button. Their
/// 13x13 glyphs have five layers, and the border and the flat fill are only the outer two. `rim`
/// fills the shape `inset` in from the outer edge and `fill` the shape a pixel further in, so the
/// rim shows as a 1px ring around the fill. With the default inset the rim covers
/// `(2.5, 2.5, w-5, h-5)` and the fill `(3, 3, w-6, h-6)`. Both layers are filled, since their
/// gradients run diagonally and only fills follow `GradientAxis`. `draw_control_chrome` draws the
/// well square, for the check box, and RadioButton draws the same two layers round.
struct InsetWell
{
    /// The outer layer, filling the shape `inset` in from the indicator's edge.
    LinearGradientBrush rim;
    /// The inner layer. Empty where the era skips it: a disabled Aero box keeps the rim and drops
    /// the fill.
    std::optional<LinearGradientBrush> fill{};
    /// DIPs from the indicator's outer edge to the rim. The fill starts one pixel further in.
    float inset = 2.0f;

    friend bool operator==(const InsetWell&, const InsetWell&) = default;
};

/// How a button-shaped surface looks in one interaction state: its fill, border and label color,
/// plus the optional era layers (bevels, gradients, inner rings, wells) that
/// `draw_control_chrome` draws.
///
/// Theme holds one per state per style (`button_normal`, `accent_button_hover`, ...), and the
/// built-in toggles, sliders and swipe items use the same type. A plain
/// `ButtonVisual{background, border, text}` leaves every optional layer off and draws a flat
/// rounded surface. The other fields are for themes that reproduce an era.
///
/// Example:
/// @code
/// ui::Theme theme = ui::Theme::light();
/// theme.button_hover = ui::ButtonVisual{ui::Color::from_argb(0xFFE0E0E0), // background
///                                          ui::Color::from_argb(0xFF808080), // border
///                                          ui::colors::Black};               // label
/// theme.button_hover.border_width = 2.0f; // this state only; the rest keep Theme::border_width
/// window.pin_theme(std::move(theme));
/// @endcode
struct ButtonVisual
{
    /// The flat fill. Also set as the fallback color when `background_gradient` is set.
    Color background;
    /// The outline color, for a surface that draws a flat border.
    Color border;
    /// The label, glyph or mark drawn on the surface.
    Color text;

    /// For era themes only. Every field below defaults to off, so an aggregate-initialized
    /// ButtonVisual{bg, border, text} draws a plain flat surface.
    ///
    /// Flat and Rounded, the default, draw no separate border, and `background` is the whole
    /// fill. Raised and Sunken draw a classic 3-D edge around the fill with render/Bevel.hpp's
    /// draw_bevel_border(), from the `bevel_*` colors below, in place of the border and gradient
    /// fields.
    BorderStyle border_style = BorderStyle::Rounded;
    /// The four bevel rings. `bevel_light` is the outer top/left ring, `bevel_highlight` the inner
    /// top/left ring, `bevel_shadow` the inner bottom/right ring and `bevel_dark_shadow` the outer
    /// bottom/right ring, the hard dark edge at a raised button's bottom-right corner. Windows 95
    /// and 98 use five distinct colors with the face: `#FFFFFF`, `#DFDFDF`, `#C0C0C0`, `#808080`
    /// and `#000000`. Windows 2000 makes the light ring the face color and softens the dark ring
    /// to `#404040`, so the same edge has visibly lower contrast.
    Color bevel_light       = colors::White;
    Color bevel_highlight   = colors::White;
    Color bevel_shadow      = Color::from_argb(0xFF808080);
    Color bevel_dark_shadow = colors::Black;
    /// The push-button arrangement, which swaps the two top/left rings (render/Bevel.hpp). Set
    /// only on push-button visuals. A window border, field well or check-box well keeps the plain
    /// arrangement, since the era draws both.
    bool bevel_soft = false;
    /// How many of the two bevel rings to draw. The era's hot toolbar button is a single edge
    /// with no second ring, a third raised shape next to the window border and the push button
    /// (render/Bevel.hpp's `rings`).
    int bevel_rings = 2;
    /// Windows 3.x only. When set, the edge is drawn by `draw_win3x_border` instead of
    /// `draw_bevel_border`: a 1px frame in this color on all four sides, with a 1px
    /// `bevel_highlight` on the top and left and a 2px `bevel_shadow` on the right and bottom
    /// inside it. The pressed state drops the bottom and right edge entirely. `bevel_light` and
    /// `bevel_dark_shadow` are not used here, since Windows 3.x has no outer bevel rings. Unset
    /// everywhere else.
    std::optional<Color> bevel_frame{};
    /// A top-to-bottom gradient fill in place of `background`, for Luna and Aero. `background`
    /// stays set alongside it as a flat fallback for code that only needs the button's rough
    /// color.
    std::optional<LinearGradientBrush> background_gradient{};

    /// Aero's inner highlight ring, the fourth layer of its button: a second 1px stroke one pixel
    /// inside the outer border, with a radius one less. The outer border sits at
    /// `(0.5, 0.5, w-1, h-1)` with radius 2.75 and this ring at `(1.5, 1.5, w-3, h-3)` with radius
    /// 1.75. It makes an Aero surface look like a lit bevel, and its color marks three states:
    /// white `#FAFFFFFF` to `#85FFFFFF` at rest and disabled, absent when pressed (the press
    /// animation fades it out first), and solid cyan `#F900CCFF` on the default button, the era's
    /// only default-button cue. The gradient stretches over the ring's own height, where
    /// `border_gradient` below spans a fixed number of pixels.
    std::optional<LinearGradientBrush> inner_border{};
    /// How thick that ring is. Aero's is one pixel, the default. Luna's is two: the amber glow
    /// of a hot button sits 2 px thick inside its 1px `#003C74` border, and the check box's ring
    /// is 2 px too, inset by 1 px. A 1px ring would look like a thin outline instead of a glow.
    float inner_border_width = 1.0f;
    /// Aero's pressed inner shadow: two 2px bands inside the top and left edges, fading from this
    /// color to transparent, drawn over the fill and under the border. Aero shows a press by
    /// shading the button's inside, and the label stays where it is.
    std::optional<Color> inner_shadow{};
    /// Aero's check box and radio button only (see `InsetWell`): the rim and fill drawn inside the
    /// well, over `background` and under the border. Unset everywhere else.
    std::optional<InsetWell> inset_well{};

    /// Fluent's elevation border: a gradient stroke drawn in place of a flat `border` when set,
    /// which makes the control look slightly raised. Theme::fluent() sets it only on the neutral
    /// and accent buttons at rest and on hover. Every other ButtonVisual leaves it unset: pressed
    /// and disabled states, subtle buttons, toggle indicators, and the era and high-contrast
    /// themes. The gradient spans `border_gradient_extent_px` device pixels whatever the
    /// control's height. A positive span starts at the top edge, and a negative one starts at
    /// the bottom edge with the stops reversed (see StrokeRoundedRectGradient).
    std::optional<LinearGradientBrush> border_gradient{};
    float                              border_gradient_extent_px = 3.0f;

    /// The same raised look on the mark drawn over the indicator. The gradient runs from top to
    /// bottom over the mark's own height, with `Palette::control_stroke_default` at 50 % and
    /// `Palette::control_stroke_secondary` at 70 %, in light and dark alike. Unlike
    /// `border_gradient` it stretches with the mark: the upper half is the faint color and the
    /// lower 30 % the stronger one, whatever the mark's size. It is stroked 1 pixel inside the
    /// mark. Only RadioButton reads it, for its dot, and only `Theme::fluent()`'s checked ring
    /// sets it.
    std::optional<LinearGradientBrush> mark_border_gradient{};

    /// Aero's radio dot: a radial fill lit from the top left and sized to the dot, with a flat
    /// `mark_stroke` ring 1px inside its edge, in place of the flat `text` disc. RadioButton only,
    /// and only where set. When it is set on an unchecked state the dot is drawn anyway, which is
    /// how the era previews a press. The gradient dot is drawn in render() only, and never
    /// animated as a one-color layer.
    std::optional<RadialGradientBrush> mark_gradient{};
    std::optional<Color>               mark_stroke{};

    /// The classic engraved look of a disabled label (see TextEffect). The default draws the label
    /// once, in plain color.
    TextEffect text_effect{};

    /// This visual's own border width, for themes such as Windows 8.1, whose dark mode has a 2px
    /// Button outline and a 0px CheckBox outline, and light mode the opposite. Unset, the default,
    /// uses `Theme::border_width` (see resolved_border_width()).
    OptionalFloat border_width{};
    /// Draws a flat stroke in `border` at the resolved width even when the caller of
    /// draw_control_chrome() did not ask for one through `draw_flat_border_fallback`. Metro's
    /// dark Button is only this stroke, a 2px outline with no fill. Defaults to false.
    bool draw_border = false;

    /// Returns this visual's `border_width` if it set one, otherwise `theme_default`, which is
    /// `Theme::border_width`. A control that draws its border itself, without
    /// draw_control_chrome(), calls this to get the same width draw_control_chrome() would use.
    [[nodiscard]] float resolved_border_width(float theme_default) const
    {
        return border_width.value_or(theme_default);
    }

    friend bool operator==(const ButtonVisual&, const ButtonVisual&) = default;
};

/// The era-specific surface colors the built-in controls draw with: field borders, menus,
/// tooltips, link states, separators and scroll bars. A custom control reads the role colors in
/// `Theme::colors()` first and uses these only to match a built-in control's exact look.
///
/// Example:
/// @code
/// // A custom menu row drawn the way the theme's own menus draw theirs:
/// class MenuRowControl : public ui::Element
/// {
/// protected:
///     void render_override(ui::Renderer& r, const ui::RenderContext& context) const override
///     {
///         const ui::Surfaces& s = context.theme.surfaces;
///         r.fill_rounded_rect(bounds(), 0.0f, hot_ ? s.menu_highlight : s.menu);
///         r.draw_text(label_, bounds(), hot_ ? s.menu_highlight_text : s.menu_text, context.theme.text_style);
///     }
///
/// private:
///     std::string label_;
///     bool        hot_ = false;
/// };
/// @endcode
struct Surfaces
{
    Color field_border;        ///< A text field's border or bevel color, a sunken well or a 1px stroke.
    Color menu;                ///< The menu, menu bar, toolbar and tab strip background.
    Color menu_text;           ///< Menu item text.
    Color menu_highlight;      ///< The fill of a menu item under the pointer or selected.
    Color menu_highlight_text; ///< The text of a menu item under the pointer or selected.
    /// The popup's own border, when it differs from the field border. `nullopt` uses
    /// `field_border`. Metro needs it: its popup border is black while its text field border is
    /// white, and the field border would draw a white popup with a white frame.
    std::optional<Color> menu_border;
    /// A menu item's fill while pressed, and its secondary text: the shortcut, the submenu
    /// chevron and the check mark at rest. `nullopt` uses `menu_highlight` and `menu_text`, for
    /// an era whose menus have no separate pressed look and draw shortcuts in the item's color.
    std::optional<Color> menu_pressed;
    std::optional<Color> menu_secondary_text;
    Color                tooltip;      ///< The tooltip background.
    Color                tooltip_text; ///< Tooltip text.
    /// The tooltip's own 1 px frame, when it differs from the field border. `nullopt` uses
    /// `field_border`, as `menu_border` does. Luna needs it: its tooltip keeps the classic look,
    /// so the frame is black instead of the text field's `#7F9DB9`.
    std::optional<Color> tooltip_border;
    /// TeachingTip's body and tail while it does not close on an outside click. A tip that does
    /// fills with `menu`, the same in-app acrylic a Flyout uses. `nullopt` is `menu`, for eras
    /// that predate the control.
    std::optional<Color> teaching_tip;
    /// The 1px frame of the two surfaces that float over the page without being a popup list:
    /// Dialog and TeachingTip. `nullopt` keeps each one's era frame, the field border for a
    /// dialog and the popup border for a tip.
    std::optional<Color> surface_stroke;
    /// DialogControl's title-and-content area, laid over the dialog's own background, and the
    /// rule under it that sets the button area apart. `nullopt` draws neither, which gives the
    /// one flat face of every earlier era's dialog.
    std::optional<Color> dialog_top_overlay;
    std::optional<Color> dialog_separator;
    /// SidePane's default pane background: `#F2F2F2` in light, `#171717` in dark, and the window
    /// color in high contrast. `nullopt` is `menu`, the era's chrome face, for eras that predate
    /// the control.
    std::optional<Color> split_view_pane;
    /// NavigationShell's content area fill and the border around it. `nullopt` draws neither,
    /// and the page shows through, as in every earlier era.
    std::optional<Color> nav_content;
    std::optional<Color> nav_content_stroke;
    /// A link's text under the pointer, pressed and disabled, where the era has its own.
    /// `nullopt` uses `ThemeColors::link` for hover and press and the subtle button's disabled
    /// text, the right look for every era without a hyperlink button of its own.
    std::optional<Color> hyperlink_hover;
    std::optional<Color> hyperlink_pressed;
    std::optional<Color> hyperlink_disabled;
    Color                separator_light; ///< The etched groove's light line.
    Color                separator_dark;  ///< The etched groove's dark line.
    Color                scrollbar_track; ///< The scroll bar's track.
    Color                scrollbar_thumb; ///< The scroll bar's thumb.
    Color                scrollbar_arrow; ///< The scroll bar's arrow glyphs.

    friend bool operator==(const Surfaces&, const Surfaces&) = default;
};

/// The theme's colors by role, resolved for the era and mode the theme draws. Every theme fills
/// every field, high contrast included, so a custom control can read these without checking
/// `Theme::variant` first. `Theme::palette` (Fluent only), `surfaces` and the button visuals hold
/// the era-specific detail behind them.
///
/// A theme's colors are read-only (`Theme::colors()`), since they are what the theme resolved
/// to. To change them, pass a ThemeColors to `Theme::fluent(colors)` or
/// `Theme::classic(colors, ...)`, which derive a whole theme from it, or set a `ThemeRequest`
/// (`accent_override`, mode, variant) on the window. The defaults are the Fluent light theme's
/// colors, so `ThemeColors{.accent = ...}` describes a light theme with that accent.
///
/// New roles are added at the end. A role is never removed or renamed.
///
/// Example:
/// @code
/// class SwatchControl : public ui::Element
/// {
/// protected:
///     void render_override(ui::Renderer& r, const ui::RenderContext& context) const override
///     {
///         const ui::ThemeColors& c      = context.theme.colors();
///         const float            radius = context.theme.corner_radius;
///         r.fill_rounded_rect(bounds(), radius, is_enabled() ? c.accent : c.control_fill_disabled);
///         r.stroke_rounded_rect(bounds(), radius, c.control_stroke, 1.0f);
///         r.draw_text(label_, bounds(), c.text_on_accent, context.theme.text_style);
///     }
///
/// private:
///     std::string label_;
/// };
/// @endcode
struct ThemeColors
{
    // ---- text ----
    /// Body text on `background`, `card` and `overlay`. TextControl's default color.
    Color text = Color::from_argb(0xE4000000);
    /// Less prominent text: captions, descriptions, a placeholder. The same as `text` in an era
    /// without a secondary text color.
    Color text_secondary = Color::from_argb(0x9E000000);
    /// The text of a disabled control.
    Color text_disabled = Color::from_argb(0x5C000000);
    /// Text and glyphs drawn on an `accent` fill.
    Color text_on_accent = colors::White;
    /// A link's text. `Surfaces` holds its hover, pressed and disabled states.
    Color link = Color::from_argb(0xFF004275);

    // ---- surfaces ----
    /// The window's own background, which the host clears the window to: the page color in the
    /// Fluent eras, the dialog face in the classic ones.
    Color background = Color::from_argb(0xFFF3F3F3);
    /// A raised panel or card on `background`. The same as `background` in an era without
    /// cards.
    Color card = Color::from_argb(0xB3FFFFFF);
    /// A popup's surface: menus, flyouts, drop-down lists. Under in-app acrylic this is the
    /// solid color the material stands in for.
    Color overlay = Color::from_argb(0xFFF9F9F9);
    /// An editable or scrollable surface, such as a TextBox or a list's rows.
    Color field = Color::from_argb(0xB3FFFFFF);
    /// Text on `field`.
    Color field_text = Color::from_argb(0xE4000000);
    /// The scrim drawn over the window behind an open dialog. 30 % black in every Fluent theme.
    Color scrim = Color::from_argb(0x4D000000);

    // ---- accent ----
    /// The accent fill at rest, on hover and pressed: what an accent Button fills with.
    Color accent         = Color::from_argb(0xFF005A9E);
    Color accent_hover   = Color::from_argb(0xFF005A9E).with_alpha(0.9f);
    Color accent_pressed = Color::from_argb(0xFF005A9E).with_alpha(0.8f);

    // ---- controls ----
    /// A control's fill at rest, on hover, pressed and disabled: what a neutral Button fills
    /// with.
    Color control_fill          = Color::from_argb(0xB3FFFFFF);
    Color control_fill_hover    = Color::from_argb(0x80F9F9F9);
    Color control_fill_pressed  = Color::from_argb(0x4DF9F9F9);
    Color control_fill_disabled = Color::from_argb(0x4DF9F9F9);
    /// A control's outline: a neutral Button's border.
    Color control_stroke = Color::from_argb(0x0F000000);
    /// The stronger outline of an unchecked box, a slider track or a field's border.
    Color control_stroke_strong = Color::from_argb(0x72000000);
    /// A divider line between groups of content. The Fluent eras draw it as one flat stroke. The
    /// classic eras draw an etched groove from `Surfaces::separator_light` and `separator_dark`
    /// instead, as `Theme::divider_visual` says.
    Color divider = Color::from_argb(0x0F000000);

    // ---- selection and focus ----
    /// A selected item's fill and its text.
    Color selection      = Color::from_argb(0xFF005A9E);
    Color selection_text = colors::White;
    /// The keyboard focus visual: the outer ring (also the classic eras' dotted rectangle) and
    /// the inner ring inside it.
    Color focus       = Color::from_argb(0xE4000000);
    Color focus_inner = Color::from_argb(0xB3FFFFFF);

    // ---- status ----
    /// The strong severity fills: InfoBar's icon disc, an error message, a warning mark.
    Color critical      = Color::from_argb(0xFFC42B1C);
    Color caution       = Color::from_argb(0xFF9D5D00);
    Color success       = Color::from_argb(0xFF0F7B0F);
    Color informational = Color::from_argb(0xFF005A9E);

    friend bool operator==(const ThemeColors&, const ThemeColors&) = default;
};

/// How the corners of the system-drawn window frame are rounded. An era theme sets this so the
/// frame matches the content, so a Windows 95 theme asks for square corners. Windows 11 only.
enum class CornerPreference
{
    Default,    ///< The system decides: round on Windows 11.
    DoNotRound, ///< Square corners.
    Round,      ///< The standard window radius.
    RoundSmall, ///< The smaller menu and flyout radius.
};

/// Which of the four answers a FrameColor holds.
enum class FrameColorKind
{
    Follow,        ///< No opinion; the next layer decides (see follow()).
    SystemDefault, ///< The system's own color.
    None,          ///< Draw nothing. For the border only.
    Custom,        ///< The color in operator*().
};

/// A color for a part of the system-drawn window frame. It holds one of four answers, where
/// `std::optional<Color>` could hold only two:
///
///   follow()         - no opinion, the default. In ChromeHints it leaves the system's color.
///                      In ChromeOverrides it uses the theme's.
///   system_default() - the system's own color, even over a theme that sets one.
///   none()           - draw nothing. Windows 11 and the border only. A caption color of
///                      none() is treated as system_default().
///   a Color          - that color. The conversion is implicit, so
///                      `chrome.caption_color = palette.selection` works.
///
/// Example:
/// @code
/// ui::ChromeOverrides chrome;
/// chrome.caption_color      = ui::Color::from_argb(0xFF1E1E1E); // a custom color
/// chrome.caption_text_color = ui::FrameColor::system_default(); // the OS's, over the theme's
/// chrome.border_color       = ui::FrameColor::none();           // no border line (Windows 11)
/// window.set_chrome(chrome);
/// @endcode
class FrameColor
{
public:
    /// follow(): no opinion.
    constexpr FrameColor() = default;
    /// A custom color; implicit so a plain Color can be assigned.
    constexpr FrameColor(Color color) :
        kind_(FrameColorKind::Custom),
        color_(color) // NOLINT(google-explicit-constructor)
    {
    }

    /// No opinion: ChromeHints leaves the OS default, ChromeOverrides uses the theme's.
    [[nodiscard]] static constexpr FrameColor follow()
    {
        return FrameColor{};
    }
    /// Forces the OS's own color, even over a theme that sets one.
    [[nodiscard]] static constexpr FrameColor system_default()
    {
        return FrameColor{FrameColorKind::SystemDefault};
    }
    /// Draws nothing. Windows 11 and the border only; a caption of none() acts as system_default().
    [[nodiscard]] static constexpr FrameColor none()
    {
        return FrameColor{FrameColorKind::None};
    }

    /// Which answer this holds.
    [[nodiscard]] constexpr FrameColorKind kind() const
    {
        return kind_;
    }
    /// True only for a custom color, the one case where `*` means anything.
    [[nodiscard]] constexpr bool has_value() const
    {
        return kind_ == FrameColorKind::Custom;
    }
    /// The custom color; meaningful only when has_value().
    [[nodiscard]] constexpr const Color& operator*() const
    {
        return color_;
    }
    /// Back to follow().
    constexpr void reset()
    {
        *this = FrameColor{};
    }

    friend constexpr bool operator==(const FrameColor&, const FrameColor&) = default;

private:
    constexpr explicit FrameColor(FrameColorKind kind) : kind_(kind)
    {
    }

    FrameColorKind kind_ = FrameColorKind::Follow;
    Color          color_{};
};

/// What a theme sets on the system-drawn window frame (`Theme::chrome`): corner rounding, a dark
/// title bar, the caption, border and title text colors, and whether a system backdrop is
/// allowed. Each window applies it with its own ChromeOverrides on top (merge_chrome()), and
/// again on every theme change. The defaults leave the system's look alone.
///
/// Example:
/// @code
/// ui::Theme theme                 = ui::Theme::light();
/// theme.chrome.corner_preference      = ui::CornerPreference::DoNotRound;
/// theme.chrome.caption_color          = ui::Color::from_argb(0xFF000080);
/// theme.chrome.caption_text_color     = ui::colors::White;
/// theme.chrome.inactive_caption_color = ui::Color::from_argb(0xFF808080);
/// window.pin_theme(std::move(theme)); // the host re-applies the frame attributes
/// @endcode
struct ChromeHints
{
    /// How the frame's corners are rounded. Windows 11 only.
    CornerPreference corner_preference = CornerPreference::Default;
    /// Whether the title bar is dark. It also tells every other part of the window that the
    /// system draws, such as the native context menu, that the theme is dark, so a menu never
    /// shows light under a dark title bar.
    bool       dark_titlebar = false;
    FrameColor caption_color{};      ///< The title bar background.
    FrameColor border_color{};       ///< The window border.
    FrameColor caption_text_color{}; ///< The title text.

    /// Whether the page may show a system backdrop material (Mica) instead of an opaque
    /// `colors.background` fill, that is, whether a window that asked for `Backdrop::Mica` leaves
    /// its client area transparent under this theme.
    ///
    /// Only Fluent 2 leaves this true. Mica came with Windows 11, after every earlier era, and
    /// Windows 10 Fluent has only acrylic. Every other theme and high contrast turn it off. Over
    /// Mica a page paints only where its controls paint, so text in a band the app leaves bare,
    /// such as a header or a status strip, lands on the wallpaper's tint.
    bool allows_system_backdrop = true;

    /// The title bar background and text colors while the window is inactive. `follow()` keeps
    /// the active colors in both states.
    FrameColor inactive_caption_color{};
    FrameColor inactive_caption_text_color{};

    friend bool operator==(const ChromeHints&, const ChromeHints&) = default;
};

/// One window's own settings for its frame, layered over the theme's ChromeHints. Every field
/// left unset follows the theme. The window's override wins over the theme, and the theme over
/// the system default. The override is applied again on every theme change, so a theme switch
/// never undoes it.
///
/// Corner preference works on Windows 11 only, since Windows 10 frames are always square, and a
/// maximized or snapped window is never rounded. `RoundSmall` is the menu and flyout radius, the
/// right pick for a `Frame::None` popup that should look like a flyout.
///
/// `allows_system_backdrop` has no override. Whether a page may go translucent is up to the
/// theme (see ChromeHints).
///
/// Example:
/// @code
/// // Square corners on a Fluent window, and no border line at all:
/// window.set_chrome({.corner_preference = ui::CornerPreference::DoNotRound,
///                     .border_color      = ui::FrameColor::none()});
/// @endcode
struct ChromeOverrides
{
    /// Overrides ChromeHints::corner_preference when set.
    std::optional<CornerPreference> corner_preference{};
    /// Overrides ChromeHints::dark_titlebar when set.
    std::optional<bool> dark_titlebar{};
    /// The caption, border and caption text colors; follow() keeps the theme's.
    FrameColor caption_color{};
    FrameColor border_color{};
    FrameColor caption_text_color{};
    /// The caption while the window is inactive (ChromeHints' fields of the same name).
    FrameColor inactive_caption_color{};
    FrameColor inactive_caption_text_color{};

    friend bool operator==(const ChromeOverrides&, const ChromeOverrides&) = default;
};

/// The frame settings a window applies: `overrides` layered over `theme`.
[[nodiscard]] inline ChromeHints merge_chrome(const ChromeHints& theme, const ChromeOverrides& overrides)
{
    ChromeHints merged = theme;
    if (overrides.corner_preference)
    {
        merged.corner_preference = *overrides.corner_preference;
    }
    if (overrides.dark_titlebar)
    {
        merged.dark_titlebar = *overrides.dark_titlebar;
    }
    const auto pick = [](const FrameColor& over, const FrameColor& base)
    { return over.kind() == FrameColorKind::Follow ? base : over; };
    merged.caption_color               = pick(overrides.caption_color, theme.caption_color);
    merged.border_color                = pick(overrides.border_color, theme.border_color);
    merged.caption_text_color          = pick(overrides.caption_text_color, theme.caption_text_color);
    merged.inactive_caption_color      = pick(overrides.inactive_caption_color, theme.inactive_caption_color);
    merged.inactive_caption_text_color = pick(overrides.inactive_caption_text_color, theme.inactive_caption_text_color);
    return merged;
}

/// The accent color and the three lighter and three darker shades Windows derives from it, read
/// from the system's color settings. Fluent controls fill accent surfaces with one of the
/// shades instead of the accent itself. A filled accent control at rest uses `light2` in the dark
/// theme and `dark1` in the light theme, so the surface stands out against the page.
/// The defaults are the shades of Windows' default accent, so a default-constructed
/// OsThemeSnapshot resolves to the colors Windows 11 shows with it: light fills with `dark1`
/// `#005A9E`, dark with `light2` `#76B9ED`.
struct AccentRamp
{
    /// The accent color itself, which the other fields are tints and shades of.
    Color base   = Color::from_argb(0xFF0078D4);
    Color light1 = Color::from_argb(0xFF429CE3); ///< The first lighter shade.
    Color light2 = Color::from_argb(0xFF76B9ED); ///< The second lighter shade, the dark theme's accent fill.
    Color light3 = Color::from_argb(0xFFA6D8FF); ///< The lightest shade.
    Color dark1  = Color::from_argb(0xFF005A9E); ///< The first darker shade, the light theme's accent fill.
    Color dark2  = Color::from_argb(0xFF004275); ///< The second darker shade.
    Color dark3  = Color::from_argb(0xFF002642); ///< The darkest shade.

    friend bool operator==(const AccentRamp&, const AccentRamp&) = default;
};

/// How dividers and separators are drawn, Toolbar's separators included: the classic two-line
/// etched groove from `surfaces.separator_light` and `separator_dark`, the default, or Fluent's
/// flat single line in `colors.divider`. Only `Theme::fluent()` picks the flat line. Every other
/// theme keeps the groove.
enum class ThemeDividerVisual
{
    EtchedGroove, ///< Two 1px lines, `surfaces.separator_light` / `separator_dark`.
    FlatStroke,   ///< One 1px line in `colors.divider`.
};

/// How a control marks that something opens, drops down or expands: Expander, TreeView,
/// NavigationShell, Breadcrumb, DropdownButton, SplitButton, NumberBox's spin buttons and a
/// menu's submenu arrow. Fluent and every theme after Windows 2000 draw the icon font's chevrons.
/// The classic themes draw solid arrows and plus/minus boxes.
enum class ThemeDisclosureVisual
{
    /// The icon font's line chevrons (`glyphs::ChevronDown` and the rest).
    IconChevron,
    /// The classic solid arrows, 7 x 4 at 96 DPI (`draw_classic_arrow()`), as on scroll bars and
    /// submenus, and a tree view's 9 x 9 plus/minus box (`draw_classic_tree_button()`).
    ClassicArrow,
};

/// Which drop shadow an open popup casts (render/OverlayShadow.hpp). Fluent's two-light shadow
/// follows from an elevation scale, where every kind of surface has a height. Aero casts no
/// shadow under a button, card or panel. Its only shadow is an offset block with a 5 px fall-off
/// under a menu, tooltip or drop-down. Defaults to the Fluent shadow.
enum class ThemePopupShadow
{
    FluentTwoLight, ///< Fluent's soft two-light elevation shadow.
    AeroDropShadow, ///< An offset block with a 5 px fall-off, as Luna and Aero draw it.
    /// No shadow at all. Menus, drop-downs and dialogs are plain rectangles on the surface below
    /// them.
    None,
};

/// How a menu (MenuFlyout, the menu bar's menus, a context menu) and a drop-down list
/// (ComboBox) come into view. Every era animates these differently:
///   - Fluent 2, Fluent, MDL2 and Metro (the defaults): a menu slides out from its own edge over
///     250 ms and fades out over 83 ms. A drop-down list appears at once.
///   - Aero and Luna follow the system's animation settings. A menu fades in when menu fading
///     is on, slides in when it is off, and does neither when menu animation is off. A
///     drop-down list slides open when list box animation is on. Both run 200 ms, and a menu
///     closes at once. resolve_theme() reads the settings from OsThemeSnapshot.
///   - Windows 9x, 3.x and 1.x/2.x: no animation.
enum class ThemePopupAnimation
{
    Slide, ///< Slides out from its own edge.
    Fade,  ///< Fades in.
    None,  ///< Appears at once.
};

/// The design language (era) the whole UI is drawn in, chosen independently of the Windows
/// version the app runs on. ThemeVariant::System, the default, draws the look of the running
/// Windows: Fluent 2 on Windows 11 and Fluent before it. Fluent2 is the primary target.
///
/// `System`, `Fluent2` and `Fluent` are always available. The era themes (`Mdl2` to `Windows1x`)
/// are linked into an app only once it calls enable_era_themes(). Before that, asking for one
/// draws the System look.
///
/// Dark and light are not variants. They are set by ThemeMode, which by default follows the
/// system. `Fluent2`, `Fluent`, `Metro` and `Mdl2` have a dark and a light palette and honor
/// ThemeMode and ThemeRequest::accent_override. Every other variant has one fixed, light
/// palette and ignores both (theme_variant_supports_dark_mode()).
///
/// Under high contrast every variant draws its own era's high-contrast look. Windows 9x, 3.x and
/// 1.x take the contrast colors as one more color scheme, Luna and Aero fall back to the Classic
/// look as XP and 7 did, and Metro, MDL2 and Fluent keep their shapes in contrast colors. The
/// colors are the system's contrast theme while one is on, and the era's own contrast scheme
/// when an app forces ThemeMode::HighContrast without one.
///
/// Example:
/// @code
/// int run_retro()
/// {
///     ui::Window retro{{
///         .title = "Retro",
///         .theme = ui::ThemeRequest{.variant = ui::ThemeVariant::Windows9x}, // light-only, ignores ThemeMode
///     }};
///     return ui::run(retro);
/// }
/// @endcode
enum class ThemeVariant
{
    /// The look of the running Windows: Fluent2 on Windows 11 and Fluent on Windows 10 and
    /// earlier (OsThemeSnapshot::system_variant). The default. A resolved Theme never reports it.
    /// Its Theme::variant is the variant System stood for.
    System,
    /// Fluent 2, Windows 11's design, whatever Windows the app runs on. The primary target, with
    /// resolve_high_contrast() under high contrast.
    Fluent2,
    /// Fluent 1: the original Fluent Design System as it shipped on Windows 10, from 1709 (2017)
    /// through early 2021. Follows dark and light mode and the accent, like Fluent2. Windows 11 is
    /// Fluent2.
    ///
    /// See resolve_fluent1() for what this variant changes.
    Fluent,
    /// MDL2 ("Microsoft Design Language 2"): Windows 10's original flat, square look with the
    /// accent as its only color, as it shipped from 1507 (2015-07-29) through 1703 (2017-04-11),
    /// before Fluent. No material, no elevation, and no rounded corners except the Slider thumb
    /// and the ToggleSwitch track.
    ///
    /// Follows dark and light mode and the accent, like Fluent2, Fluent and Metro. Its colors
    /// come from the 24 base colors of an Mdl2Palette, in a light and a dark set.
    ///
    /// The name is a press convention. No Microsoft document of the era uses it.
    Mdl2,
    /// MDL ("Metro Design Language"): Windows 8 and 8.1 merged into one entry. It follows
    /// Windows 8.1, with its dark mode and user accent.
    Metro,
    /// Aero: Windows Vista and 7 merged into one entry, defaulting to Windows 7's column.
    ///
    /// The two releases barely differ. The only change a control shows is disabled text, which
    /// went from `#808080` to `#6D6D6D`. Aero has no control accent, since the user's glass color
    /// tinted the frame, taskbar and Start menu but never a control. This variant ignores both
    /// ThemeMode's light and dark and ThemeRequest::accent_override.
    Aero,
    /// Luna: Windows XP's default visual style.
    Luna,
    /// Windows 9x: Windows 95, 98, and 2000 merged into one entry. Defaults to the Windows 95 and
    /// 98 "Windows Standard" scheme (`ClassicPalette::windows_standard()`).
    /// ThemeRequest::classic_scheme swaps in any other ClassicPalette, such as
    /// `ClassicPalette::windows_standard_2000()`.
    Windows9x,
    /// Windows 3.x: before hot tracking, Active Desktop and the four-color bevel of Windows 95.
    Windows3x,
    /// Early Windows: Windows 1.01 (1985-11) through 2.11 (1989-03), the design language
    /// Windows 3.0 replaced in May 1990.
    ///
    /// It has no sense of depth at all: no bevel, no gray `#C0C0C0` face, no light source, no
    /// highlight and shadow pair and no drop shadow. All of its chrome is black and white, and
    /// the only depth cue is a one-pixel frame line. It has ten colors, thirteen from 2.0, with
    /// no button face, selection or disabled-text color among them. That is why its colors are an
    /// `EarlyPalette` instead of a `ClassicPalette`, and its theme comes from `Theme::early()`
    /// instead of `Theme::classic()`.
    ///
    /// What sets "Windows 1" apart from "Windows 2" happens at 2.03 in December 1987, when
    /// windows stopped being tiled. This one variant covers both and defaults to the Windows 2.03
    /// color palette. `EarlyPalette::windows_1x_mono()` and the other two palettes are one
    /// ThemeRequest::early_scheme away. The four palettes are separate looks, so pick a version
    /// first, then a display type, and do not mix them.
    Windows1x,
};

namespace detail
{
struct ControlChrome;
struct ControlChromeAccess;

// A number no other Theme value has had, never 0.
[[nodiscard]] std::uint64_t next_theme_generation() noexcept;

// Theme::generation()'s storage: every Theme value, a copy or a moved-to one included, gets a
// number of its own, so a cache keyed on it never mistakes an edited copy for its source.
class ThemeGeneration
{
public:
    ThemeGeneration() noexcept : value_(next_theme_generation())
    {
    }
    ThemeGeneration(const ThemeGeneration&) noexcept : value_(next_theme_generation())
    {
    }
    ThemeGeneration& operator=(const ThemeGeneration&) noexcept
    {
        value_ = next_theme_generation();
        return *this;
    }
    ~ThemeGeneration() = default;
    [[nodiscard]] std::uint64_t value() const
    {
        return value_;
    }

private:
    std::uint64_t value_;
};
} // namespace detail

/// Everything a theme defines: the palette and accent shades, corner radii, padding and border
/// width, the surfaces, text styles and type ramp, the focus visual, motion, popup behavior,
/// window frame settings, and the three button sets custom controls draw with
/// (`draw_control_chrome`).
///
/// A default-constructed Theme is not usable: button_normal, accent_button_* and the other
/// button visuals have no meaningful default. Always get a Theme from Theme::light() or
/// Theme::fluent(palette), also for a class member's default
/// (`Theme theme_ = Theme::light();`, never `Theme theme_{};`).
///
/// A Window owns the active theme (Window::pin_theme()) and hands it to every element's measure
/// and render. An element with a requested theme draws its subtree with `with_dark_mode`. The
/// fields are plain data, so an app may take a resolved theme and change single values (metrics,
/// type, motion) before applying it. The role colors are the exception: `colors()` is
/// read-only, and a theme with other colors comes from fluent(const ThemeColors&) or
/// classic(const ThemeColors&, ...). How each built-in control draws itself in an era (its
/// toggle, slider, field and row-selection looks) is internal to the theme and not public. A
/// copy of a theme carries it along unchanged.
///
/// Example:
/// @code
/// // A dark Fluent theme with a wider focus ring, applied to a window:
/// ui::Theme theme           = ui::Theme::fluent(ui::Palette::fluent_dark());
/// theme.focus_ring_outer_width = 3.0f;
/// window.pin_theme(std::move(theme));
/// @endcode
struct Theme
{
    /// The design language the theme draws in, the one its factory built it for. Under high
    /// contrast it is the variant whose contrast look this is, so check `high_contrast` too. A
    /// custom control reads it to ask "is this Fluent?" before reading `palette`.
    ///
    /// Example:
    /// @code
    /// const bool fluent = (theme.variant == ui::ThemeVariant::Fluent2 || theme.variant == ui::ThemeVariant::Fluent) &&
    ///                     !theme.high_contrast;
    /// const ui::Color fill = fluent ? theme.palette.card_background_default : theme.colors().field;
    /// @endcode
    ThemeVariant variant = ThemeVariant::Fluent2;
    /// Whether the theme is a high-contrast one, from a system contrast theme or
    /// ThemeMode::HighContrast. Its colors are then the contrast scheme's, and `palette` is left
    /// default even under the Fluent variants.
    bool high_contrast = false;
    /// Whether the theme draws light content on a dark background: the dark mode of a variant
    /// that has both (Fluent 2, Fluent, Mdl2, Metro), or a dark contrast scheme. Every theme sets
    /// it, and the fixed-palette eras are light. A custom control reads it to pick a per-mode
    /// value, such as a shadow's alpha or a band color, under any variant. `palette.dark` is set
    /// only by Theme::fluent().
    bool dark = false;

    /// The colors by role, filled for every era and mode (see ThemeColors), and what a custom
    /// control reads first. Read-only. A different set of colors is a different theme, made by
    /// `fluent(const ThemeColors&)` or `classic(const ThemeColors&, ...)`.
    [[nodiscard]] const ThemeColors& colors() const noexcept
    {
        return colors_;
    }

    /// A copy of this theme for drawing a container's content. Content in the default text color
    /// draws in `text`, and content that reads the background, such as a control that blends into
    /// its surface, sees `background` when one is given. A container whose surface differs from
    /// the page, such as a card, a popup or an accent-filled box, draws its children with it.
    ///
    /// Example:
    /// @code
    /// // An accent-filled box whose text follows the accent:
    /// class AccentBoxControl : public ui::Element
    /// {
    /// protected:
    ///     void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
    ///     {
    ///         const ui::Theme& theme = context.theme;
    ///         renderer.fill_rounded_rect(bounds(), 0.0f, theme.colors().accent);
    ///         child_->render(renderer, context.with_theme(theme.with_content_colors(theme.colors().text_on_accent)));
    ///     }
    ///
    /// private:
    ///     std::unique_ptr<ui::Element> child_;
    /// };
    /// @endcode
    [[nodiscard]] Theme with_content_colors(Color text, std::optional<Color> background = std::nullopt) const;

    /// The Fluent palette the theme was derived from. Only Theme::fluent() sets it, so it is
    /// filled for the Fluent variants outside high contrast. Every other theme leaves it default,
    /// so check `variant` before reading it.
    Palette palette;
    /// The accent shades the theme was resolved from, the Windows defaults when the theme was not
    /// resolved from the system's settings. Controls and apps that need a lighter or darker shade
    /// of the accent read it here.
    AccentRamp accent_ramp{};

    // ---- light and dark (Element::set_requested_theme()) ----
    /// The same theme resolved light (false) or dark (true), which an element with a requested
    /// theme (Element::set_requested_theme()) draws its subtree with. Set by resolve_theme() for
    /// the variants that have both. Empty for the fixed-palette eras and under high contrast,
    /// where a requested theme is ignored. It resolves the request again, so later edits to this
    /// theme are not in its result. An element with a requested theme carries the edits that are
    /// not colors (metrics, type, motion) over on top of it.
    std::function<Theme(bool dark)> with_dark_mode{};
    /// A number unique to this Theme value, never 0. Each theme built, copied or moved into gets
    /// a new one, so a cache of measurements or derived themes can tell a new or edited theme
    /// from the one it came from. Editing a field in place keeps the number. Hand the edited
    /// theme over as a new value (Window::pin_theme()) for caches to see the change.
    [[nodiscard]] std::uint64_t generation() const
    {
        return generation_.value();
    }

    // ---- global metrics ----
    float     corner_radius         = 4.0f; ///< Control corner radius in DIPs: buttons, fields, toggles.
    float     overlay_corner_radius = 8.0f; ///< Popup corner radius in DIPs: flyouts, menus, dialogs.
    Thickness padding = Edges{.left = 11.0f, .top = 5.0f, .right = 11.0f, .bottom = 6.0f}; ///< Button content padding
    float     border_width = 1.0f; ///< Default control border width; see ButtonVisual::border_width.

    /// Control density: the sizes compact density overrides. Each is nullopt in a standard
    /// theme, which leaves the control at its own standard size. ControlSizing::compact() holds
    /// the compact values, and ui/containers/DensityScope.hpp's DensityScope applies them to a
    /// subtree.
    struct ControlSizing
    {
        OptionalFloat            text_control_min_height{}; ///< Text field minimum height (standard: 32)
        std::optional<Thickness> text_control_padding{};    ///< Text field padding (standard: the field's `padding`)
        OptionalFloat            combo_box_min_height{};    ///< ComboBox minimum height
        std::optional<Thickness> combo_box_padding{};       ///< ComboBox padding (standard: its `padding`)

        /// Compact density: text field minimum height 24 and padding 2,2,6,1, ComboBox minimum
        /// height 24 and padding 12,1,0,3.
        [[nodiscard]] static ControlSizing compact()
        {
            ControlSizing s;
            s.text_control_min_height = 24.0f;
            s.text_control_padding    = Edges{.left = 2.0f, .top = 2.0f, .right = 6.0f, .bottom = 1.0f};
            s.combo_box_min_height    = 24.0f;
            s.combo_box_padding       = Edges{.left = 12.0f, .top = 1.0f, .bottom = 3.0f};
            return s;
        }

        friend bool operator==(const ControlSizing&, const ControlSizing&) = default;
    };
    /// Control density overrides; all unset (standard density) by default.
    ControlSizing sizing{};

    /// The two-ring focus visual (render/FocusRing.hpp's draw_focus_ring()): a 2px outer ring in
    /// `colors.focus` with a 1px `colors.focus_inner` ring right inside it. The pair fills the
    /// `focus_ring_margin` pixels outside the control's bounds. `focus_ring_margin` is how far
    /// out the outer ring's outer edge lands, half a stroke beyond its center line.
    float focus_ring_outer_width = 2.0f; ///< outer ring stroke, in DIPs
    float focus_ring_inner_width = 1.0f; ///< inner ring stroke, in DIPs
    float focus_ring_margin      = 3.0f; ///< how far the two rings reach outside the control

    /// TextBox's misspelling squiggle. Defaults to the Fluent light theme's critical red, the red
    /// every Windows spell checker uses, which the era themes keep. Theme::fluent() takes the
    /// palette's semantic_fill_critical, a lighter red in dark themes. High contrast uses the
    /// window text color, the only one sure to show on the field background.
    Color spelling_error = Color::from_argb(0xFFC42B1C);

    /// How a disabled control's text is drawn, on top of `colors.text_disabled`: the text beside a
    /// CheckBox, RadioButton or ToggleSwitch, and a disabled TextControl. Windows 95 through 2000
    /// draw disabled text the way they draw a disabled push button's label, engraved: a highlight
    /// copy offset by a pixel under a shadow copy. So `Theme::classic()` sets the same `Emboss` it
    /// gives `button_disabled`.
    ///
    /// Text only. A disabled check mark or radio dot stays a flat gray glyph, so the indicators
    /// read their own `text` and never this. Windows 3.x draws disabled text in plain gray, as
    /// every non-classic theme does.
    TextEffect disabled_text_effect{};

    /// Severity colors: InfoBar's icon disc and body, and ProgressBar's paused and error
    /// indicator. Read these instead of `palette`, which only the Fluent themes fill.
    ///
    /// The defaults are the Fluent light colors, so a theme that sets none still draws readable
    /// bars. `Theme::fluent()` fills them from the palette, `resolve_high_contrast()` from the
    /// system's contrast colors, and an era theme sets its own where the era has them.
    struct SeverityVisual
    {
        Color fill;       ///< the strong mark: InfoBar's icon disc, ProgressBar's indicator
        Color background; ///< the surface behind the whole bar

        friend bool operator==(const SeverityVisual&, const SeverityVisual&) = default;
    };
    /// The four InfoBar severities plus the shared neutral fill, glyph and stroke colors.
    struct Severities
    {
        /// Drawn in the accent itself. `Theme::fluent()` fills it from `accent_button_normal`.
        SeverityVisual informational{Color::from_argb(0xFF0078D4), Color::from_argb(0x80F6F6F6)};
        /// The success, caution and critical severities.
        SeverityVisual success{Color::from_argb(0xFF0F7B0F), Color::from_argb(0xFFDFF6DD)};
        SeverityVisual caution{Color::from_argb(0xFF9D5D00), Color::from_argb(0xFFFFF4CE)};
        SeverityVisual critical{Color::from_argb(0xFFC42B1C), Color::from_argb(0xFFFDE7E9)};
        /// The gray of InfoBadge's `Informational` styles. A fill only, since no InfoBar severity
        /// is neutral.
        Color neutral = Color::from_argb(0xFF8A8A8A);
        /// The glyph drawn on one of the `fill` discs, and the bar's own 1px outline.
        Color text_on_fill = colors::White;
        Color stroke       = Color::from_argb(0x0F000000);

        friend bool operator==(const Severities&, const Severities&) = default;
    };
    /// InfoBar, InfoBadge and ProgressBar severity colors.
    Severities severity{};

    /// The hover and press highlight fade of every control that can be toggled.
    Duration anim_faster{83.0};
    /// For offset and scale moves: ToggleSwitch's knob slide, RadioButton's dot resize, and
    /// Slider's inner-circle grow/shrink into Normal/Disabled.
    Duration anim_fast{167.0};
    /// Slider's inner thumb circle growing into Hover/Pressed. The duration is keyed by the
    /// state being entered, so leaving Hover runs at anim_fast.
    Duration anim_normal{250.0};
    /// The easing curve for every offset/scale motion above, at either duration.
    CubicBezier anim_fast_out_slow_in = easing::ControlFastOutSlowIn;
    /// Whether controls animate at all, resolved from ThemeRequest::animations and the system setting.
    /// When false, resolve_theme() also zeroes the durations above and `popup_motion`, and
    /// Window ticks its tree with animations_enabled() false, so every animation the framework
    /// plays lands on its end state at once.
    bool animations_enabled = true;

    /// The theme's base UI font: button labels, most control text, and menus unless
    /// `chrome_text_style` is set. TextControl reads `type` instead.
    TextStyle text_style{};

    /// The font menus and menu bars draw in, for an era whose chrome font differs from its
    /// content font. nullopt uses `text_style`. Windows 3.x sets it: its chrome is System 10pt
    /// bold, and its dialogs MS Sans Serif 8pt.
    std::optional<TextStyle> chrome_text_style{};

    /// `chrome_text_style` if this era has one, `text_style` otherwise.
    [[nodiscard]] const TextStyle& menu_text_style() const
    {
        return chrome_text_style ? *chrome_text_style : text_style;
    }

    /// The type ramp `TextControl::set_role()` resolves against. Every theme fills `body` from
    /// `text_style`. Metro, MDL2 and the Fluent themes give the other roles their own size and weight.
    struct TypeRamp
    {
        /// One style per TextRole. A theme that sets only `body` leaves the rest at defaults.
        TextStyle header, subheader, title, subtitle, body, caption;
        /// BodyStrong's own style, where an era has one, as Fluent 2 does. Unset, the role is
        /// `body` in SemiBold (TextControl::resolved_style()).
        std::optional<TextStyle> body_strong{};

        /// The style for a TextRole (BodyStrong falls back to `body`; see `body_strong`).
        [[nodiscard]] const TextStyle& for_role(TextRole role) const
        {
            switch (role)
            {
                case TextRole::BodyStrong:
                    return body_strong ? *body_strong : body;
                case TextRole::Header:
                    return header;
                case TextRole::Subheader:
                    return subheader;
                case TextRole::Title:
                    return title;
                case TextRole::Subtitle:
                    return subtitle;
                case TextRole::Caption:
                    return caption;
                case TextRole::Body:
                    break;
            }
            return body;
        }

        /// Per-role foreground, for an era whose type ramp carries color as well as size and
        /// weight. Aero sets it: its main instruction is `#003399` Segoe UI 12pt and links are
        /// `#0066CC`. Every entry is unset for every other theme, so a role paints in
        /// `ThemeColors::text`.
        struct RoleColors
        {
            /// One optional color per role; unset paints in `ThemeColors::text`.
            std::optional<Color> header, subheader, title, subtitle, body, caption;

            friend bool operator==(const RoleColors&, const RoleColors&) = default;
        };
        /// Per-role foregrounds; all unset outside Aero.
        RoleColors color{};

        /// The color for a TextRole (BodyStrong shares `body`'s); unset means `colors.text`.
        [[nodiscard]] const std::optional<Color>& color_for_role(TextRole role) const
        {
            switch (role)
            {
                case TextRole::BodyStrong:
                    return color.body;
                case TextRole::Header:
                    return color.header;
                case TextRole::Subheader:
                    return color.subheader;
                case TextRole::Title:
                    return color.title;
                case TextRole::Subtitle:
                    return color.subtitle;
                case TextRole::Caption:
                    return color.caption;
                case TextRole::Body:
                    break;
            }
            return color.body;
        }
    };
    /// The type ramp TextControl resolves its role() against.
    TypeRamp type{};

    /// Page layout metrics: the grid step, the page inset and header, and the item spacing.
    /// Metro sets its 20px grid and 120/140/50 page layout here; the defaults suit every other
    /// theme.
    struct PageMetrics
    {
        float     grid_unit = 4.0f;           ///< the layout grid step, in DIPs
        Thickness content_margin{12.0f};      ///< page content inset, in DIPs
        float     header_height      = 0.0f;  ///< 0 == no header band
        float     item_spacing       = 4.0f;  ///< gap between stacked items, in DIPs
        float     command_bar_height = 48.0f; ///< a command bar's height, in DIPs
    };
    /// Page silhouette metrics.
    PageMetrics page{};

    /// How the era's shapes and text are rasterized. Window::render() applies it first, every
    /// frame. The default is antialiased shapes, grayscale text and no pixel snapping. Only the
    /// classic, Luna and Aero themes set something else.
    SetRasterization rasterization{};

    /// How keyboard focus is shown: the Fluent two rings (the default), the classic eras' black
    /// dotted rectangle inside the control (Windows 95 through 7, drawn in `colors.focus`), or
    /// Metro's dashed pair.
    FocusVisual focus_visual = FocusVisual::FluentRings;

    /// Which divider geometry separators draw; EtchedGroove unless Theme::fluent().
    ThemeDividerVisual divider_visual = ThemeDividerVisual::EtchedGroove;

    /// Which expand, collapse and drop-down marks controls draw; ClassicArrow in Theme::classic().
    ThemeDisclosureVisual disclosure_visual = ThemeDisclosureVisual::IconChevron;

    /// The shadow an open popup casts.
    ThemePopupShadow popup_shadow = ThemePopupShadow::FluentTwoLight;

    /// How menus and drop-down lists open and close.
    struct PopupMotion
    {
        ThemePopupAnimation menu = ThemePopupAnimation::Slide; ///< how a menu opens
        ThemePopupAnimation list = ThemePopupAnimation::None;  ///< how a drop-down list opens
        Duration            open{250.0};                       ///< the opening animation's length
        Duration            close{83.0};                       ///< a menu's fade-out; 0 closes at once

        friend bool operator==(const PopupMotion&, const PopupMotion&) = default;
    };
    /// The era's popup open/close animations.
    PopupMotion popup_motion{};

    /// The material that flyouts, drop-down lists, menus and the other popups that close on an
    /// outside click fill with: in-app acrylic, which blurs what is drawn behind it. Only Fluent 2
    /// sets it (`Palette::acrylic_in_app_default()`), and only while the system allows
    /// transparency (`OsThemeSnapshot::transparency_enabled`) and no contrast theme is on. Unset,
    /// those surfaces fill with their solid color, `surfaces.menu`. fill_overlay_surface() makes
    /// that choice.
    std::optional<AcrylicBrush> overlay_material{};

    /// The symbol font a control's glyphs are drawn from, passed as the `font_family` of
    /// `render/IconGlyph.hpp`'s `draw_icon`. Windows 10 Fluent uses `Segoe MDL2 Assets`, and
    /// Windows 11 `Segoe Fluent Icons`, a redesign with the same code points. Each theme names its
    /// font.
    FontName icon_font_family = "Segoe Fluent Icons";

    /// Button's own corner radius, for an era whose push button is rounded differently from its
    /// other controls. Unset, the default, follows `corner_radius`. Windows 3.x sets it, since
    /// its push button is the era's one rounded control.
    OptionalFloat button_corner_radius{};

    /// `button_corner_radius` if set, `corner_radius` otherwise.
    [[nodiscard]] float resolved_button_corner_radius() const
    {
        return button_corner_radius.value_or(corner_radius);
    }

    /// The era-specific surface colors every theme fills (see Surfaces): field borders, menus,
    /// toolbars, tabs, tooltips, link states and scroll bars.
    Surfaces surfaces{};

    /// Settings for the system-drawn window frame (see ChromeHints). The defaults leave the
    /// system's corner rounding, title bar mode and frame colors alone.
    ChromeHints chrome{};

    /// Neutral Button (the default ButtonVariant), the look of every Button unless it opts into
    /// ButtonVariant::Accent/Subtle.
    ButtonVisual button_normal, button_hover, button_pressed, button_disabled;
    /// ButtonVariant::Accent: a filled accent surface, at most one per view.
    ButtonVisual accent_button_normal, accent_button_hover, accent_button_pressed, accent_button_disabled;
    /// ButtonVariant::Subtle: borderless, with only a wash on hover and press. Toolbar, command
    /// bar and menu buttons use it.
    ButtonVisual subtle_button_normal, subtle_button_hover, subtle_button_pressed, subtle_button_disabled;

    /// The neutral Button's visual (`button_*`) for an interaction state.
    [[nodiscard]] const ButtonVisual& for_state(ButtonState s) const
    {
        switch (s)
        {
            case ButtonState::Hover:
                return button_hover;
            case ButtonState::Pressed:
                return button_pressed;
            case ButtonState::Disabled:
                return button_disabled;
            case ButtonState::Normal:
                break;
        }
        return button_normal;
    }

    /// The borderless `subtle_button_*` visual for an interaction state. Menu bar, command bar
    /// and toolbar buttons use it, the menu bar's items included. A Button the app places itself
    /// uses for_state() unless it opts into ButtonVariant::Subtle.
    [[nodiscard]] const ButtonVisual& subtle_for_state(ButtonState s) const
    {
        switch (s)
        {
            case ButtonState::Hover:
                return subtle_button_hover;
            case ButtonState::Pressed:
                return subtle_button_pressed;
            case ButtonState::Disabled:
                return subtle_button_disabled;
            case ButtonState::Normal:
                break;
        }
        return subtle_button_normal;
    }

    /// The focus visual settings that render/FocusRing.hpp's draw_focus_ring() and
    /// draw_focus_ring_rings() take, built from this theme's fields.
    [[nodiscard]] FocusRingStyle focus_ring_style() const
    {
        return FocusRingStyle{focus_visual,
                              colors_.focus,
                              colors_.focus_inner,
                              focus_ring_outer_width,
                              focus_ring_inner_width,
                              focus_ring_margin};
    }

    /// A Fluent theme derived from `palette`: every value above and the built-in controls' own
    /// looks.
    [[nodiscard]] static Theme fluent(const Palette& palette);

    /// A Fluent theme derived from a full set of role colors. The roles that differ from the
    /// Fluent light colors, or the dark ones when `roles.background` is dark, replace the palette
    /// colors they come from, and everything else follows from them as in fluent(const Palette&).
    /// Change `accent` alone and the selection, the accent button's hover and pressed fills and
    /// the informational severity follow it. `colors()` of the result reads back what was passed
    /// for every role that was changed.
    ///
    /// Example:
    /// @code
    /// ui::ThemeColors colors = ui::Theme::fluent(ui::Palette::fluent_dark()).colors();
    /// colors.accent          = ui::Color::from_argb(0xFF8764B8);
    /// window.pin_theme(ui::Theme::fluent(colors));
    /// @endcode
    [[nodiscard]] static Theme fluent(const ThemeColors& roles);

    /// A classic (Windows 95, 98, 2000) theme derived from `palette`, the classic counterpart of
    /// fluent(). Every classic theme, a custom scheme included, is one `ClassicPalette` plus
    /// this function, with no hand-assembled `ButtonVisual` per state. `hot_track` lights a
    /// button's face under the pointer, as Windows 98 does and 95 does not. `font_family` is the
    /// UI font, at 11 px. Name a TrueType font: bitmap fonts can't be loaded, so `MS Sans Serif`
    /// or `System` quietly gives Segoe UI. The Windows 9x and 3.x themes pass
    /// `Microsoft Sans Serif` and then set it at 8 pt on a 13 px line.
    /// `win3x_edge` switches every edge to the Windows 3.x construction in place of the 9x
    /// four-ring bevel. Windows 3.x has only face, shadow and highlight colors for buttons, but
    /// draws a four-color, uneven edge: a 1px black frame, a 1px white highlight on the top and
    /// left and a 2px gray shadow on the right and bottom (`render/Bevel.hpp`'s
    /// `draw_win3x_border`). It also rounds the push button, flattens the check box to a black
    /// and white outline, marks it with an X instead of a tick, and drops the engraved disabled
    /// label.
    [[nodiscard]] static Theme classic(const ClassicPalette& palette,
                                       bool                  hot_track,
                                       const char*           font_family,
                                       bool                  win3x_edge = false);

    /// A classic theme derived from a full set of role colors, the classic counterpart of
    /// fluent(const ThemeColors&). The roles that differ from the Windows Standard scheme's colors
    /// replace the scheme color they come from: `background` and `control_fill` replace the face,
    /// and with it the bevel, `text` the face text, and `field` and `field_text` the window
    /// colors. `selection`, `selection_text`, `text_disabled`, `link` and `overlay` (the menu)
    /// replace their own. The other roles follow from those, as in
    /// classic(const ClassicPalette&, ...).
    ///
    /// Example:
    /// @code
    /// ui::ThemeColors colors = ui::Theme::classic(ui::ClassicPalette::windows_standard(), true, "Tahoma").colors();
    /// colors.selection       = ui::Color::from_rgb(0x008080);
    /// window.pin_theme(ui::Theme::classic(colors, true, "Tahoma"));
    /// @endcode
    [[nodiscard]] static Theme classic(const ThemeColors& roles, bool hot_track, const char* font_family, bool win3x_edge = false);

    /// A Metro (Windows 8.1) theme derived from `palette`, like fluent() and classic(). Metro has
    /// a dark and a light mode and a user accent, and `dark` says which mode the theme is. Pass
    /// `MetroPalette::metro_dark()` or `metro_light()` with the matching `dark`.
    [[nodiscard]] static Theme metro(const MetroPalette& palette, bool dark);

    /// An MDL2 (Windows 10, 1507 to 1703) theme derived from `palette`, like fluent(), classic()
    /// and metro(). Like Metro, MDL2 has a dark and a light mode, and `dark` says which, together
    /// with the matching `Mdl2Palette::mdl2_dark()` or `mdl2_light()`. `accent` is the system's
    /// current accent, which this era uses on the controls.
    [[nodiscard]] static Theme mdl2(const Mdl2Palette& palette, bool dark, const Color& accent);

    /// An early-Windows (1.x and 2.x) theme derived from `palette`, like fluent(), classic(),
    /// metro() and mdl2(), and the oldest of the five.
    ///
    /// This era has no sense of depth and no button face, selection or disabled-text color, so
    /// its colors are an `EarlyPalette`. Its whole drawing model is six rules: a 1px frame, a
    /// flat fill, reverse video, black-and-white glyphs, fixed-pitch text without antialiasing,
    /// and a double 1px frame for a dialog.
    [[nodiscard]] static Theme early(const EarlyPalette& palette);

    /// Windows 11 Fluent, light: `fluent(Palette::fluent_light())`. The default theme a Window
    /// starts with, and the one to reach for in member defaults.
    [[nodiscard]] static Theme light()
    {
        return fluent(Palette::fluent_light());
    }

    /// One light() theme that lives as long as the program, the default argument where a control
    /// is measured or drawn on its own. Some containers keep a pointer to the theme they were
    /// last measured with, so a default made of a temporary would dangle.
    [[nodiscard]] static const Theme& default_light()
    {
        static const Theme theme = light();
        return theme;
    }

private:
    friend struct detail::ControlChromeAccess;

    ThemeColors colors_{};
    // The built-in controls' own records (src/ui/themes/ControlChrome.hpp), shared by every copy
    // of this theme until one of them is edited; null reads as a default ControlChrome.
    std::shared_ptr<const detail::ControlChrome> control_chrome_{};
    detail::ThemeGeneration                      generation_{};
};


/// A theme resource for every color of ThemeColors, read from the theme each time it is used, so
/// it follows light, dark and high contrast. `.with_alpha()` gives one at another opacity.
///
/// Example:
/// @code
/// ui::Border{.background = ui::theme_colors::Accent.with_alpha(0.7f)};
/// ui::Text{.text = "Saved", .foreground = ui::theme_colors::Success};
/// @endcode
namespace theme_colors
{
/// ThemeColors::text.
inline constexpr ThemeColor Text = [](const Theme& t) { return t.colors().text; };
/// ThemeColors::text_secondary.
inline constexpr ThemeColor TextSecondary = [](const Theme& t) { return t.colors().text_secondary; };
/// ThemeColors::text_disabled.
inline constexpr ThemeColor TextDisabled = [](const Theme& t) { return t.colors().text_disabled; };
/// ThemeColors::text_on_accent.
inline constexpr ThemeColor TextOnAccent = [](const Theme& t) { return t.colors().text_on_accent; };
/// ThemeColors::link.
inline constexpr ThemeColor Link = [](const Theme& t) { return t.colors().link; };
/// ThemeColors::background.
inline constexpr ThemeColor Background = [](const Theme& t) { return t.colors().background; };
/// ThemeColors::card.
inline constexpr ThemeColor Card = [](const Theme& t) { return t.colors().card; };
/// ThemeColors::overlay.
inline constexpr ThemeColor Overlay = [](const Theme& t) { return t.colors().overlay; };
/// ThemeColors::field.
inline constexpr ThemeColor Field = [](const Theme& t) { return t.colors().field; };
/// ThemeColors::field_text.
inline constexpr ThemeColor FieldText = [](const Theme& t) { return t.colors().field_text; };
/// ThemeColors::scrim.
inline constexpr ThemeColor Scrim = [](const Theme& t) { return t.colors().scrim; };
/// ThemeColors::accent.
inline constexpr ThemeColor Accent = [](const Theme& t) { return t.colors().accent; };
/// ThemeColors::accent_hover.
inline constexpr ThemeColor AccentHover = [](const Theme& t) { return t.colors().accent_hover; };
/// ThemeColors::accent_pressed.
inline constexpr ThemeColor AccentPressed = [](const Theme& t) { return t.colors().accent_pressed; };
/// ThemeColors::control_fill.
inline constexpr ThemeColor ControlFill = [](const Theme& t) { return t.colors().control_fill; };
/// ThemeColors::control_fill_hover.
inline constexpr ThemeColor ControlFillHover = [](const Theme& t) { return t.colors().control_fill_hover; };
/// ThemeColors::control_fill_pressed.
inline constexpr ThemeColor ControlFillPressed = [](const Theme& t) { return t.colors().control_fill_pressed; };
/// ThemeColors::control_fill_disabled.
inline constexpr ThemeColor ControlFillDisabled = [](const Theme& t) { return t.colors().control_fill_disabled; };
/// ThemeColors::control_stroke.
inline constexpr ThemeColor ControlStroke = [](const Theme& t) { return t.colors().control_stroke; };
/// ThemeColors::control_stroke_strong.
inline constexpr ThemeColor ControlStrokeStrong = [](const Theme& t) { return t.colors().control_stroke_strong; };
/// ThemeColors::divider.
inline constexpr ThemeColor Divider = [](const Theme& t) { return t.colors().divider; };
/// ThemeColors::selection.
inline constexpr ThemeColor Selection = [](const Theme& t) { return t.colors().selection; };
/// ThemeColors::selection_text.
inline constexpr ThemeColor SelectionText = [](const Theme& t) { return t.colors().selection_text; };
/// ThemeColors::focus.
inline constexpr ThemeColor Focus = [](const Theme& t) { return t.colors().focus; };
/// ThemeColors::focus_inner.
inline constexpr ThemeColor FocusInner = [](const Theme& t) { return t.colors().focus_inner; };
/// ThemeColors::critical.
inline constexpr ThemeColor Critical = [](const Theme& t) { return t.colors().critical; };
/// ThemeColors::caution.
inline constexpr ThemeColor Caution = [](const Theme& t) { return t.colors().caution; };
/// ThemeColors::success.
inline constexpr ThemeColor Success = [](const Theme& t) { return t.colors().success; };
/// ThemeColors::informational.
inline constexpr ThemeColor Informational = [](const Theme& t) { return t.colors().informational; };
} // namespace theme_colors

} // namespace ui
