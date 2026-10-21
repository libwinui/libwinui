// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/ui/themes/ClassicPalette.hpp"
#include "winui/ui/themes/EarlyPalette.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <optional>

namespace ui
{

/// Which light, dark or contrast appearance a ThemeRequest asks for.
///
/// Only the variants for which theme_variant_supports_dark_mode() is true tell Light and Dark
/// apart. HighContrast applies to every variant.
enum class ThemeMode
{
    System, ///< Follow the system's light or dark setting as it changes (the default).
    Light,  ///< Always light, whatever the system setting.
    Dark,   ///< Always dark, whatever the system setting.
    /// The system's contrast theme while one is on, otherwise the variant's own contrast colors
    /// (see resolve_theme()).
    HighContrast,
};

/// Whether controls animate: follow the system's "Animation effects" setting, or force it
/// either way.
///
/// With animations off, every transition the framework plays itself lands on its end state at
/// once: control state tweens, transitions, implicit and brush transitions, popup motion,
/// animated icons, animated scrolling and connected animations. Timers that are not animations
/// (tooltip delay, caret blink, key repeat) keep running, and an indeterminate ProgressBar or
/// ProgressRing keeps moving, since that motion is what tells the user work is going on.
///
/// Example:
/// @code
/// // A "Reduce motion" switch in an app's settings page:
/// void set_reduce_motion(bool reduce_motion)
/// {
///     ui::ThemeRequest request = window.theme_request();
///     request.animations       = reduce_motion ? ui::AnimationPreference::Off : ui::AnimationPreference::System;
///     window.set_theme(request);
/// }
/// @endcode
enum class AnimationPreference
{
    System, ///< Follow OsThemeSnapshot::animations_enabled as it changes (the default).
    On,     ///< Always animate, whatever the system setting.
    Off,    ///< Never animate, whatever the system setting.
};

/// The colors of the system's contrast theme, used as they are. The defaults are black on
/// white, so a default-constructed OsThemeSnapshot is usable without setting every field.
///
/// These are the eight colors a user picks in a contrast theme, plus a border color.
///
/// Example:
/// @code
/// ui::OsThemeSnapshot os;
/// os.high_contrast = true;
/// os.hc            = ui::HighContrastPalette::windows10_high_contrast_black();
///
/// // A contrast theme is on: the window draws with the palette the user picked.
/// window.pin_theme(ui::resolve_theme(ui::ThemeRequest{}, os));
/// @endcode
struct HighContrastPalette
{
    Color window_background  = colors::White;                ///< The page and text field background.
    Color window_text        = colors::Black;                ///< Text on `window_background`.
    Color control_background = colors::White;                ///< Button and control faces.
    Color control_text       = colors::Black;                ///< Text on `control_background`.
    Color control_border     = colors::Black;                ///< Control borders and frames.
    Color accent             = Color::from_argb(0xFF0078D4); ///< Selected items and selected text.
    Color accent_text        = colors::White;                ///< Text on `accent`.
    Color disabled_text      = Color::from_argb(0xFF6D6D6D); ///< Disabled text.
    Color hyperlink          = Color::from_argb(0xFF0066CC); ///< Hyperlinks.

    /// Field-wise equality, so a window can tell whether the contrast colors changed.
    friend bool operator==(const HighContrastPalette&, const HighContrastPalette&) = default;

    /// Aquatic, the contrast theme Windows 11 turns on when Left Alt+Left Shift+Print Screen is
    /// pressed and no contrast theme was chosen before. The border color is not one a user picks,
    /// so it takes `control_text`, the color contrast themes draw control borders in.
    ///
    /// Fluent 2 uses these colors when an app forces `ThemeMode::HighContrast` while the system
    /// has no contrast theme on.
    [[nodiscard]] static constexpr HighContrastPalette aquatic()
    {
        HighContrastPalette p;
        p.window_background  = Color::from_argb(0xFF202020);
        p.window_text        = Color::from_argb(0xFFFFFFFF);
        p.control_background = Color::from_argb(0xFF202020);
        p.control_text       = Color::from_argb(0xFFFFFFFF);
        p.control_border     = Color::from_argb(0xFFFFFFFF);
        p.accent             = Color::from_argb(0xFF8EE3F0);
        p.accent_text        = Color::from_argb(0xFF263B50);
        p.disabled_text      = Color::from_argb(0xFFA6A6A6);
        p.hyperlink          = Color::from_argb(0xFF75E9FC);
        return p;
    }

    /// High Contrast Black as Windows 8 through 10 ship it, designed for a 14:1 contrast ratio.
    /// The border takes `control_text`, as in `aquatic()`. Metro, MDL2 and Fluent use it when
    /// high contrast is forced.
    [[nodiscard]] static constexpr HighContrastPalette windows10_high_contrast_black()
    {
        HighContrastPalette p;
        p.window_background  = colors::Black;
        p.window_text        = colors::White;
        p.control_background = colors::Black;
        p.control_text       = colors::White;
        p.control_border     = colors::White;
        p.accent             = Color::from_argb(0xFF1AEBFF);
        p.accent_text        = colors::Black;
        p.disabled_text      = Color::from_argb(0xFF3FF23F);
        p.hyperlink          = Color::from_argb(0xFFFFFF00);
        return p;
    }
};

/// The system's theme settings, before any app override is applied.
///
/// The library reads one from the system's settings. A default-constructed snapshot is light,
/// with Windows' default blue accent and no high contrast, which resolves to the colors
/// Windows 11 shows out of the box.
///
/// Example:
/// @code
/// // Preview both modes side by side, whatever the OS is set to.
/// const ui::Theme light = ui::resolve_theme(ui::ThemeRequest{}, ui::OsThemeSnapshot{.dark = false});
/// const ui::Theme dark  = ui::resolve_theme(ui::ThemeRequest{}, ui::OsThemeSnapshot{.dark = true});
///
/// ui::Window light_preview{{.title = "Light"}};
/// ui::Window dark_preview{{.title = "Dark"}};
/// light_preview.pin_theme(light);
/// dark_preview.pin_theme(dark);
/// @endcode
struct OsThemeSnapshot
{
    /// The system's app mode: true when "Choose your app mode" is set to Dark.
    bool dark = false;
    /// The user's accent color.
    Color accent = Color::from_argb(0xFF0078D4);
    /// The system's lighter and darker shades of `accent`. The defaults are the shades of
    /// Windows' default blue.
    AccentRamp accent_ramp{};
    /// Whether a contrast theme is on. When true, resolve_theme() draws with `hc` whatever the
    /// ThemeRequest says.
    bool high_contrast = false;
    /// The contrast theme's colors. Only read while `high_contrast` is true.
    HighContrastPalette hc{};
    /// The system's menu animation setting. With `menu_fade` and `combo_box_animation`, it sets
    /// how popups open in the Luna and Aero themes (`Theme::popup_motion`). All three are on by
    /// default. This one and `combo_box_animation` are off while the system turns UI effects off.
    bool menu_animation = true;
    /// See `menu_animation`: whether menus fade in (true) or slide in.
    bool menu_fade = true;
    /// See `menu_animation`: whether combo box drop-downs slide open.
    bool combo_box_animation = true;
    /// The system's "Animation effects" setting. ThemeRequest::animations follows it by default.
    bool animations_enabled = true;
    /// Whether the system draws transparency: "Transparency effects" is on in Settings, and
    /// neither Energy Saver nor a remote session turns it off. Fluent 2's in-app acrylic
    /// (Theme::overlay_material) follows it.
    bool transparency_enabled = true;
    /// What ThemeVariant::System draws: Fluent2 on Windows 11 (build 22000 and later), Fluent on
    /// any earlier Windows.
    ThemeVariant system_variant = ThemeVariant::Fluent2;

    /// Field-wise equality. A window resolves its Theme again only when the snapshot changed.
    friend bool operator==(const OsThemeSnapshot&, const OsThemeSnapshot&) = default;
};

/// What an application asks for. Every field left at its default means "follow the system."
///
/// Pass one as `ui::WindowOptions::theme`, or later to Window::set_theme(), and the window
/// resolves the Theme from it, and again whenever the system settings change. resolve_theme()
/// makes a fixed Theme from one.
///
/// Example:
/// @code
/// int run_app()
/// {
///     ui::ThemeRequest request;
///     request.mode            = ui::ThemeMode::Dark;
///     request.accent_override = ui::Color::from_argb(0xFFE81123); // brand red instead of the OS accent
///
///     // The system decides everything the request leaves at its default:
///     ui::Window app{{.title = "App", .theme = request}};
///
///     // Later, follow the system's light or dark setting again:
///     request.mode = ui::ThemeMode::System;
///     app.set_theme(request);
///     return ui::run(app);
/// }
/// @endcode
struct ThemeRequest
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// Light, Dark, HighContrast, or System (the default) to follow the system as it changes.
    ThemeMode mode = ThemeMode::System;
    /// A brand accent to use in place of OsThemeSnapshot::accent. Its lighter and darker shades
    /// are derived from it. Ignored by the light-only variants and under high contrast.
    std::optional<Color> accent_override{};
    /// ThemeVariant::System, the default, follows `mode`, as do Fluent2, Fluent, Metro and Mdl2.
    /// Every other variant opts into a fixed, light-only palette true to its era, which ignores
    /// `mode` and `accent_override` since the era has no dark mode. The exception is a `mode` of
    /// HighContrast, which swaps in the era's contrast colors and keeps the era's shapes.
    /// theme_variant_supports_dark_mode() tells the two sets apart.
    ThemeVariant variant = ThemeVariant::System;

    /// A `ClassicPalette` that replaces the era's default colors when `variant` is Windows9x or
    /// Windows3x. "Windows 9x with the Eggplant scheme" is one palette away, since those eras'
    /// themes are derived from a ClassicPalette alone. Ignored by every other variant, Luna and
    /// Aero included.
    std::optional<ClassicPalette> classic_scheme{};

    /// The palette for `variant` Windows1x: Windows 1.01 or Windows 2.03, each in a color and a
    /// monochrome version. The variant defaults to `EarlyPalette::windows_2x_color()`, and this
    /// picks any of the other three. Ignored by every other variant.
    std::optional<EarlyPalette> early_scheme{};

    /// Whether controls animate (Theme::animations_enabled): System (the default) follows
    /// OsThemeSnapshot::animations_enabled, On and Off override it. Applies to every variant.
    AnimationPreference animations = AnimationPreference::System;

    friend bool operator==(const ThemeRequest&, const ThemeRequest&) = default;
};

/// Resolves an app's request against the system's settings into a Theme.
/// High contrast, when it is on because the system reports it or the app asked for
/// ThemeMode::HighContrast, wins over every other color input (`mode`, `accent_override`,
/// `classic_scheme`, `early_scheme`), since apps don't keep their brand colors under high
/// contrast. `variant` still picks the era the contrast colors are drawn in. The system's contrast
/// theme (`os.hc`) is the user's choice and always supplies the colors. A forced high contrast
/// with no contrast theme on takes the variant's own contrast colors: Aquatic for Fluent 2,
/// Windows 10's High Contrast Black for Fluent, MDL2 and Metro, the XP and Windows 98 High
/// Contrast Black schemes for Luna, Aero and Windows 9x, Windows 3.1's Monochrome scheme, and the
/// matching monochrome palette for Early Windows.
///
/// Example:
/// @code
/// ui::OsThemeSnapshot os;
/// os.dark = true;
///
/// // The app asks for light, so the system being dark doesn't change the result.
/// const ui::Theme theme = ui::resolve_theme(ui::ThemeRequest{.mode = ui::ThemeMode::Light}, os);
/// window.pin_theme(theme);
/// @endcode
[[nodiscard]] Theme resolve_theme(const ThemeRequest& request, const OsThemeSnapshot& os);

/// Links the era themes into the app, ThemeVariant::Mdl2 to ThemeVariant::Windows1x, with their
/// window frames. Until it is called they are left out of the executable, and a ThemeRequest
/// asking for one draws ThemeVariant::System instead. Call it once, before the first window.
/// Calling it again does nothing.
///
/// Example:
/// @code
/// int main()
/// {
///     ui::enable_era_themes();
///     ui::Window window{{.title = "Retro", .theme = ui::ThemeRequest{.variant = ui::ThemeVariant::Luna}}};
///     return ui::run(window);
/// }
/// @endcode
void enable_era_themes();

/// Whether enable_era_themes() was called, so the era variants draw as themselves.
[[nodiscard]] bool era_themes_enabled();

/// Whether `variant` tells ThemeRequest::mode's Dark and Light apart: true for System, Fluent2,
/// Fluent, Metro and Mdl2. Every other variant has one fixed, light palette and looks the same
/// whatever `mode` says, except ThemeMode::HighContrast, which still replaces its colors. A UI
/// with a variant picker and a dark/light switch uses it to disable the switch when it would
/// have no effect.
[[nodiscard]] bool theme_variant_supports_dark_mode(ThemeVariant variant);

/// What the operating system's theme is right now: light or dark, the accent color, high
/// contrast. A window that follows the system already tracks this by itself; ask for it to
/// pre-select a theme picker to match, or to resolve a theme before a window exists. A platform
/// with no backend returns the defaults.
///
/// Example:
/// @code
/// ui::Property<bool> dark{ui::os_theme().dark};
/// @endcode
[[nodiscard]] OsThemeSnapshot os_theme();

} // namespace ui
