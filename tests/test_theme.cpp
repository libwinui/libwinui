// SPDX-License-Identifier: MIT
#include "render/ControlGlyphs.hpp"
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/CubicBezier.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/OverlayShadow.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/themes/ClassicPalette.hpp"
#include "winui/ui/themes/EarlyPalette.hpp"
#include "winui/ui/themes/Mdl2Palette.hpp"
#include "winui/ui/themes/MetroPalette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <chrono>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>

using namespace ui;
using namespace ui::testing;

TEST_CASE("resolve_theme fills a light-theme accent button with the dark1 ramp rung and white text", "[theme]")
{
    OsThemeSnapshot os;
    os.dark              = false;
    os.accent_ramp.dark1 = Color::from_argb(0xFF00548C); // a rung distinct from every other field

    const Theme theme = resolve_theme(ThemeRequest{}, os);

    CHECK(theme.colors().background == Palette::fluent_light().solid_background_base);
    CHECK(theme.colors().text == Palette::fluent_light().text_fill_primary);
    CHECK(theme.accent_button_normal.background == os.accent_ramp.dark1);
    CHECK(theme.accent_button_normal.text == colors::White);
}

TEST_CASE("resolve_theme fills a dark-theme accent button with the light2 ramp rung and black text", "[theme]")
{
    OsThemeSnapshot os;
    os.dark               = true;
    os.accent_ramp.light2 = Color::from_argb(0xFF6CB4EE); // a rung distinct from every other field

    const Theme theme = resolve_theme(ThemeRequest{}, os);

    CHECK(theme.colors().background == Color::from_argb(0xFF202020));
    CHECK(theme.colors().text == colors::White);
    CHECK(theme.accent_button_normal.background == os.accent_ramp.light2);
    CHECK(theme.accent_button_normal.text == colors::Black);
}

TEST_CASE("Button defaults to the neutral control fill, not the accent ramp", "[theme]")
{
    // Theme::button_* comes from the Palette's control_fill_* ramp and must not depend on the
    // live accent color. Only the accent button reads the accent.
    OsThemeSnapshot os;
    os.dark              = false;
    os.accent_ramp.dark1 = Color::from_argb(0xFF00548C);

    const Theme theme = resolve_theme(ThemeRequest{}, os);

    CHECK(theme.button_normal.background == Palette::fluent_light().control_fill_default);
    CHECK(theme.button_normal.background != theme.accent_button_normal.background);
}

TEST_CASE("Subtle button is transparent at rest and only washes on hover/press", "[theme]")
{
    const Theme theme = resolve_theme(ThemeRequest{}, OsThemeSnapshot{});

    CHECK(theme.subtle_button_normal.background == colors::Transparent);
    CHECK(theme.subtle_button_hover.background != colors::Transparent);
    CHECK(theme.subtle_button_pressed.background != colors::Transparent);
}

TEST_CASE("hover and pressed reduce the accent fill's opacity rather than switching ramp rungs", "[theme]")
{
    OsThemeSnapshot os;
    os.dark              = false;
    os.accent_ramp.dark1 = Color::from_argb(0xFF00548C);

    const Theme theme = resolve_theme(ThemeRequest{}, os);

    CHECK(theme.accent_button_hover.background == os.accent_ramp.dark1.with_alpha(0.9f));
    CHECK(theme.accent_button_pressed.background == os.accent_ramp.dark1.with_alpha(0.8f));
    // The border stays the opaque fill on hover. Pressed and disabled accent buttons have no border.
    CHECK(theme.accent_button_hover.border == os.accent_ramp.dark1);
    CHECK(theme.accent_button_pressed.border == colors::Transparent);
}

TEST_CASE("resolve_theme follows the system light/dark setting by default", "[theme]")
{
    ThemeRequest request; // ThemeMode::System

    const Theme light = resolve_theme(request, OsThemeSnapshot{.dark = false});
    const Theme dark  = resolve_theme(request, OsThemeSnapshot{.dark = true});

    CHECK(light.colors().background != dark.colors().background);
}

TEST_CASE("ThemeRequest::mode overrides the OS setting", "[theme]")
{
    OsThemeSnapshot os;
    os.dark = true; // OS says dark...

    ThemeRequest request;
    request.mode = ThemeMode::Light; // ...but the app pins light.

    const Theme theme = resolve_theme(request, os);
    CHECK(theme.colors().background == Palette::fluent_light().solid_background_base);
}

TEST_CASE("ThemeRequest::accent_override derives its own ramp instead of using the OS one", "[theme]")
{
    OsThemeSnapshot os;
    os.dark              = false;
    os.accent_ramp.dark1 = Color::from_argb(0xFF00FF00); // sentinel: must NOT show up in the result

    ThemeRequest request;
    request.accent_override = Color::from_argb(0xFFFF0000);

    const Theme theme = resolve_theme(request, os);

    CHECK(theme.accent_button_normal.background != os.accent_ramp.dark1);
    // Same derivation resolve_theme falls back to: darken the override by 15% for dark1.
    CHECK(theme.accent_button_normal.background == lerp(Color::from_argb(0xFFFF0000), colors::Black, 0.15f));
}

TEST_CASE("high contrast is sourced verbatim from the OS palette", "[theme]")
{
    OsThemeSnapshot os;
    os.high_contrast         = true;
    os.hc.window_background  = Color::from_argb(0xFF000000);
    os.hc.window_text        = Color::from_argb(0xFFFFFFFF);
    os.hc.control_background = Color::from_argb(0xFF000000);
    os.hc.control_text       = Color::from_argb(0xFFFFFF00);
    os.hc.control_border     = Color::from_argb(0xFFFFFF00);
    os.hc.accent             = Color::from_argb(0xFF1AEBFF);
    os.hc.accent_text        = Color::from_argb(0xFF000000);

    const Theme theme = resolve_theme(ThemeRequest{}, os);

    CHECK(theme.colors().background == os.hc.window_background);
    CHECK(theme.colors().text == os.hc.window_text);
    CHECK(theme.button_normal.text == os.hc.control_text);
    CHECK(theme.button_hover.background == os.hc.accent);
    CHECK(theme.accent_button_normal.background == os.hc.accent);
    CHECK(theme.colors().focus == os.hc.accent);
}

TEST_CASE("high contrast wins even when the app explicitly requests light or dark", "[theme]")
{
    OsThemeSnapshot os;
    os.high_contrast        = true;
    os.hc.window_background = Color::from_argb(0xFF000000);

    ThemeRequest request;
    request.mode = ThemeMode::Light;

    const Theme theme = resolve_theme(request, os);
    CHECK(theme.colors().background == os.hc.window_background);
}

TEST_CASE("ThemeMode::HighContrast can be forced without the OS reporting HC", "[theme]")
{
    OsThemeSnapshot os; // os.high_contrast left false
    // Outside a contrast theme these are the ordinary system colors, not a contrast palette.
    os.hc.window_background = Color::from_argb(0xFF123456);

    ThemeRequest request;
    request.mode = ThemeMode::HighContrast;

    const Theme theme = resolve_theme(request, os);
    CHECK(theme.colors().background == HighContrastPalette::aquatic().window_background);
    CHECK(theme.button_hover.background == HighContrastPalette::aquatic().accent);
}

namespace
{

// Nine distinct values, so a check can tell which slot a color came from.
[[nodiscard]] HighContrastPalette distinct_hc_palette()
{
    HighContrastPalette hc;
    hc.window_background  = Color::from_argb(0xFF000000);
    hc.window_text        = Color::from_argb(0xFFFFFFFF);
    hc.control_background = Color::from_argb(0xFF101010);
    hc.control_text       = Color::from_argb(0xFFFFFF00);
    hc.control_border     = Color::from_argb(0xFF00FF00);
    hc.accent             = Color::from_argb(0xFF1AEBFF);
    hc.accent_text        = Color::from_argb(0xFF202020);
    hc.disabled_text      = Color::from_argb(0xFF3FF23F);
    hc.hyperlink          = Color::from_argb(0xFFFF80FF);
    return hc;
}

[[nodiscard]] Theme resolve_hc(ThemeVariant variant, const HighContrastPalette& hc)
{
    return resolve_theme(ThemeRequest{.variant = variant}, OsThemeSnapshot{.high_contrast = true, .hc = hc});
}

constexpr ThemeVariant AllVariants[] =
    {ThemeVariant::Fluent2,
     ThemeVariant::Fluent,
     ThemeVariant::Mdl2,
     ThemeVariant::Metro,
     ThemeVariant::Aero,
     ThemeVariant::Luna,
     ThemeVariant::Windows9x,
     ThemeVariant::Windows3x,
     ThemeVariant::Windows1x};

} // namespace

TEST_CASE("high contrast keeps no era color in any variant", "[theme][high-contrast]")
{
    const HighContrastPalette hc   = distinct_hc_palette();
    auto                      from = [&](const Color& c)
    {
        return c == hc.window_background || c == hc.window_text || c == hc.control_background || c == hc.control_text ||
               c == hc.control_border || c == hc.accent || c == hc.accent_text || c == hc.disabled_text ||
               c == hc.hyperlink;
    };

    for (ThemeVariant v : AllVariants)
    {
        CAPTURE(static_cast<int>(v));
        const Theme theme = resolve_hc(v, hc);
        CHECK(from(theme.colors().background));
        CHECK(from(theme.colors().text));
        CHECK(from(theme.colors().field));
        CHECK(from(theme.colors().field_text));
        CHECK(from(theme.colors().selection));
        CHECK(from(theme.colors().selection_text));
        CHECK(from(theme.surfaces.menu));
        CHECK(from(theme.surfaces.menu_text));
        CHECK(from(theme.surfaces.tooltip));
        CHECK(from(theme.surfaces.tooltip_text));
        CHECK(from(theme.colors().link));
        CHECK(from(theme.button_normal.background));
        CHECK(from(theme.button_normal.text));
        CHECK(from(theme.accent_button_normal.background));
        CHECK(from(ui::detail::controls(theme).check_box_box.normal.on.text));
        CHECK(from(theme.severity.critical.fill));
        CHECK(from(theme.severity.caution.background));
        CHECK(from(theme.spelling_error));
        CHECK_FALSE(theme.chrome.allows_system_backdrop);
    }
}

// A plain border color only strokes when the visual asks for it, so an HC button that forgot to
// ask was a ButtonFace fill on a Window page of the same color.
TEST_CASE("high contrast buttons draw their outline, and Fluent 2 keeps its type ramp", "[theme][high-contrast]")
{
    const HighContrastPalette hc = distinct_hc_palette();
    for (ThemeVariant v : {ThemeVariant::Fluent2, ThemeVariant::Fluent, ThemeVariant::Mdl2, ThemeVariant::Metro})
    {
        CAPTURE(static_cast<int>(v));
        const Theme theme = resolve_hc(v, hc);
        CHECK(theme.button_normal.draw_border);
        CHECK(theme.button_disabled.draw_border);
        CHECK(theme.button_normal.border != theme.button_normal.background);
    }

    // Subtle, and the hyperlink button through it: a transparent fill and no border.
    const Theme fluent2 = resolve_hc(ThemeVariant::Fluent2, hc);
    CHECK_FALSE(fluent2.subtle_button_normal.draw_border);
    CHECK(fluent2.subtle_button_normal.background == colors::Transparent);
    CHECK(fluent2.subtle_button_hover.background == hc.control_background);

    const Theme dark   = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Fluent2},
                                       OsThemeSnapshot{});
    const Theme forced = resolve_theme(ThemeRequest{.mode = ThemeMode::HighContrast, .variant = ThemeVariant::Fluent2},
                                       OsThemeSnapshot{});
    CHECK(forced.type.title.font_size == dark.type.title.font_size);
    CHECK(forced.type.header.font_size == dark.type.header.font_size);
    CHECK(forced.type.title.font_size > forced.type.body.font_size);
    CHECK(fluent2.type.subtitle.font_size == dark.type.subtitle.font_size);
}

TEST_CASE("high contrast keeps each variant's own construction", "[theme][high-contrast]")
{
    const HighContrastPalette hc = distinct_hc_palette();

    // Fluent 2 is the resolver high contrast always used.
    const Theme fluent2 = resolve_hc(ThemeVariant::Fluent2, hc);
    CHECK(fluent2.corner_radius == 4.0f); // the radii are kept under a contrast theme
    CHECK(fluent2.button_hover.background == hc.accent);
    CHECK(fluent2.colors().link == hc.hyperlink);

    // Fluent 1: the same colors, with Windows 10's 2/4 rounding, font and indicator geometry.
    const Theme fluent1 = resolve_hc(ThemeVariant::Fluent, hc);
    CHECK(fluent1.corner_radius == 2.0f);
    CHECK(fluent1.overlay_corner_radius == 4.0f);
    CHECK(std::string_view{fluent1.icon_font_family} == "Segoe MDL2 Assets");
    CHECK(fluent1.chrome.corner_preference == CornerPreference::DoNotRound);
    CHECK(fluent1.button_hover.background == fluent2.button_hover.background);

    // MDL2: square 2 px outlines (the rest border is transparent, ButtonText in HC),
    // and hover recolors the outline and label, not the face.
    const Theme mdl2 = resolve_hc(ThemeVariant::Mdl2, hc);
    CHECK(mdl2.corner_radius == 0.0f);
    CHECK(mdl2.border_width == 2.0f);
    auto outlined = [](const Color& fill, const Color& border, const Color& text)
    {
        ButtonVisual v{fill, border, text};
        v.draw_border = true;
        return v;
    };
    CHECK(mdl2.button_normal == outlined(hc.control_background, hc.control_text, hc.control_text));
    CHECK(mdl2.button_hover == outlined(hc.control_background, hc.accent, hc.accent));
    CHECK(ui::detail::controls(mdl2).slider_thumb.shape == SliderThumbShape::RoundedRect);

    // Metro: 8.1's high contrast colors. An outlined button that inverts when pressed, and a
    // check box that fills with Highlight on hover.
    const Theme metro = resolve_hc(ThemeVariant::Metro, hc);
    CHECK(metro.focus_visual == FocusVisual::MetroDashedPair);
    CHECK(metro.button_normal.draw_border);
    CHECK(metro.button_normal.border_width == 2.0f);
    CHECK(metro.button_pressed.background == hc.control_text);
    CHECK(metro.button_pressed.text == hc.control_background);
    CHECK(ui::detail::controls(metro).check_box_box.hover.on.background == hc.accent);
    CHECK(ui::detail::controls(metro).check_box_box.hover.on.text == hc.accent_text);
    CHECK(ui::detail::controls(metro).check_box_box.normal.on.border_width == 2.0f);

    // Windows 9x/3.x: the HC palette is one more color scheme, bevels and all.
    const Theme win9x = resolve_hc(ThemeVariant::Windows9x, hc);
    CHECK(win9x.button_normal.border_style == BorderStyle::Raised);
    CHECK(win9x.colors().background == hc.control_background);
    CHECK(win9x.colors().field == hc.window_background);
    CHECK(win9x.chrome.caption_color == hc.accent);
    CHECK(win9x.chrome.caption_text_color == hc.accent_text);
    const Theme win3x = resolve_hc(ThemeVariant::Windows3x, hc);
    CHECK(win3x.button_normal.bevel_frame == hc.control_border);

    // Luna and Aero: XP and 7 switched to Classic under high contrast, in their own UI font.
    const Theme luna = resolve_hc(ThemeVariant::Luna, hc);
    CHECK(luna.button_normal.border_style == BorderStyle::Raised);
    CHECK(std::string_view{luna.text_style.font_family} == "Tahoma");
    CHECK(luna.text_style.font_size == 11.0f);
    const Theme aero = resolve_hc(ThemeVariant::Aero, hc);
    CHECK(aero.button_normal.border_style == BorderStyle::Raised);
    CHECK(std::string_view{aero.text_style.font_family} == "Segoe UI");
    CHECK(aero.text_style.font_size == 12.0f);
    CHECK_FALSE(aero.button_normal.background_gradient.has_value());

    // Windows 1.x/2.x: a 1px frame and reverse video, no bevel.
    const Theme early = resolve_hc(ThemeVariant::Windows1x, hc);
    CHECK(early.button_normal.border_style == BorderStyle::Flat);
    CHECK(early.colors().selection == hc.window_text);
    CHECK(early.colors().selection_text == hc.window_background);
}

TEST_CASE("high contrast classic bevels reproduce High Contrast Black", "[theme][high-contrast]")
{
    // The High Contrast Black scheme: black face, white 3DLIGHT/3DDKSHADOW, #C0C0C0/#808080 inner rings.
    HighContrastPalette hc;
    hc.control_background = colors::Black;
    hc.control_text       = colors::White;

    const ButtonVisual button = resolve_hc(ThemeVariant::Windows9x, hc).button_normal;
    CHECK(button.bevel_light == colors::White);
    CHECK(button.bevel_dark_shadow == colors::White);
    CHECK(button.bevel_highlight.r == Catch::Approx(0xC0 / 255.0f).margin(1.5 / 255.0));
    CHECK(button.bevel_shadow.r == Catch::Approx(0x80 / 255.0f).margin(1.5 / 255.0));
}

TEST_CASE("high contrast ignores the color requests it replaces", "[theme][high-contrast]")
{
    const HighContrastPalette hc = distinct_hc_palette();
    const OsThemeSnapshot     os{.high_contrast = true, .hc = hc};

    const Theme scheme = resolve_theme(ThemeRequest{.variant        = ThemeVariant::Windows9x,
                                                    .classic_scheme = ClassicPalette::windows_standard_2000()},
                                       os);
    CHECK(scheme.colors().background == hc.control_background);

    const Theme accent = resolve_theme(ThemeRequest{.mode            = ThemeMode::Dark,
                                                    .accent_override = Color::from_argb(0xFFFF0000),
                                                    .variant         = ThemeVariant::Mdl2},
                                       os);
    CHECK(accent.colors().selection == hc.accent);
    CHECK(accent.colors().background == hc.window_background);
}

TEST_CASE("forced high contrast takes each era's own contrast scheme", "[theme][high-contrast]")
{
    // The OS reports no contrast theme, so `os.hc` is an ordinary scheme no variant may read.
    OsThemeSnapshot os;
    os.hc.window_background = os.hc.control_background = Color::from_argb(0xFF123456);
    os.hc.accent                                       = Color::from_argb(0xFF654321);
    auto forced = [&](ThemeVariant v, std::optional<EarlyPalette> early = std::nullopt)
    { return resolve_theme(ThemeRequest{.mode = ThemeMode::HighContrast, .variant = v, .early_scheme = early}, os); };

    const HighContrastPalette aquatic = HighContrastPalette::aquatic();
    CHECK(forced(ThemeVariant::Fluent2).colors().background == aquatic.window_background);
    CHECK(forced(ThemeVariant::Fluent2).button_hover.background == aquatic.accent);

    // Windows 8 through 10's High Contrast Black: cyan selection, yellow links.
    const HighContrastPalette black10 = HighContrastPalette::windows10_high_contrast_black();
    for (ThemeVariant v : {ThemeVariant::Fluent, ThemeVariant::Mdl2, ThemeVariant::Metro})
    {
        CAPTURE(static_cast<int>(v));
        const Theme theme = forced(v);
        CHECK(theme.colors().background == black10.window_background);
        CHECK(theme.colors().selection == black10.accent);
        CHECK(theme.colors().link == black10.hyperlink);
    }

    // Luna and Aero: Classic in XP's High Contrast Black, bevel ring for ring.
    const ClassicPalette black2000 = ClassicPalette::high_contrast_black_2000();
    for (ThemeVariant v : {ThemeVariant::Luna, ThemeVariant::Aero})
    {
        CAPTURE(static_cast<int>(v));
        const Theme theme = forced(v);
        CHECK(theme.colors().background == colors::Black);
        CHECK(theme.colors().selection == Color::from_argb(0xFF800080));
        CHECK(theme.colors().text_disabled == Color::from_argb(0xFF00FF00));
        CHECK(theme.chrome.caption_color == Color::from_argb(0xFF800080));
        CHECK(theme.button_normal.bevel_light == black2000.bevel->light);
        CHECK(theme.button_normal.bevel_highlight == black2000.bevel->highlight);
        CHECK(theme.button_normal.bevel_shadow == black2000.bevel->shadow);
        CHECK(theme.button_normal.bevel_dark_shadow == black2000.bevel->dark_shadow);
    }

    // Windows 98's own High Contrast Black, its pink 3DLIGHT included.
    const Theme win9x = forced(ThemeVariant::Windows9x);
    CHECK(win9x.button_normal.border_style == BorderStyle::Raised);
    CHECK(win9x.button_normal.bevel_light == Color::from_argb(0xFFD0B0B8));
    CHECK(win9x.colors().link == Color::from_argb(0xFF0000FF));
    CHECK(std::string_view{win9x.text_style.font_family} == "Microsoft Sans Serif");

    // Windows 3.1 had no contrast theme; its Monochrome scheme is light, with silver buttons.
    const Theme win3x = forced(ThemeVariant::Windows3x);
    CHECK(win3x.colors().background == Color::from_argb(0xFFC0C0C0));
    CHECK(win3x.colors().field == colors::White);
    CHECK(win3x.colors().selection == colors::Black);
    CHECK(win3x.colors().selection_text == colors::White);

    // Early Windows: the monochrome-display row of whichever version was asked for.
    auto same_early = [](const Theme& a, const Theme& b)
    {
        CHECK(a.colors().background == b.colors().background);
        CHECK(a.surfaces.menu == b.surfaces.menu);
        CHECK(a.surfaces.scrollbar_track == b.surfaces.scrollbar_track);
        CHECK(a.chrome.caption_color == b.chrome.caption_color);
        CHECK(a.button_normal == b.button_normal);
    };
    auto early = [](const EarlyPalette& p)
    { return resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x, .early_scheme = p}, OsThemeSnapshot{}); };
    same_early(forced(ThemeVariant::Windows1x), early(EarlyPalette::windows_2x_mono()));
    same_early(forced(ThemeVariant::Windows1x, EarlyPalette::windows_1x_color()), early(EarlyPalette::windows_1x_mono()));

    // No two of the modern eras' schemes and the classic ones collapse into one another.
    CHECK(forced(ThemeVariant::Fluent2).colors().selection != forced(ThemeVariant::Fluent).colors().selection);
    CHECK(forced(ThemeVariant::Fluent).colors().selection != forced(ThemeVariant::Luna).colors().selection);
    CHECK(forced(ThemeVariant::Luna).colors().selection != win3x.colors().selection);
}

TEST_CASE("the OS's contrast theme wins over the era's own scheme", "[theme][high-contrast]")
{
    const HighContrastPalette hc = distinct_hc_palette();
    for (ThemeVariant v : AllVariants)
    {
        CAPTURE(static_cast<int>(v));
        const Theme theme = resolve_theme(ThemeRequest{.mode = ThemeMode::HighContrast, .variant = v},
                                          OsThemeSnapshot{.high_contrast = true, .hc = hc});
        const Theme os    = resolve_hc(v, hc);
        CHECK(theme.colors().background == os.colors().background);
        CHECK(theme.colors().selection == os.colors().selection);
        CHECK(theme.colors().link == os.colors().link);
        CHECK(theme.button_normal == os.button_normal);
        CHECK(theme.accent_button_normal == os.accent_button_normal);
    }
}

TEST_CASE("the era contrast schemes carry their install media's values", "[theme][high-contrast]")
{
    const ClassicPalette black98 = ClassicPalette::high_contrast_black_98();
    CHECK(black98.face == colors::Black);
    CHECK(black98.face_text == colors::White);
    CHECK(black98.window_frame == colors::White);
    CHECK(black98.selection == Color::from_argb(0xFF800080));
    CHECK(black98.gray_text == Color::from_argb(0xFF00FF00));
    CHECK(black98.info_bk == colors::White);
    CHECK(black98.hot_light == Color::from_argb(0xFF0000FF));
    CHECK(black98.bevel == ClassicBevelRamp{Color::from_argb(0xFFD0B0B8),
                                            Color::from_argb(0xFFC0C0C0),
                                            Color::from_argb(0xFF808080),
                                            colors::White});

    const ClassicPalette black2000 = ClassicPalette::high_contrast_black_2000();
    CHECK(black2000.selection == Color::from_argb(0xFF800080));
    CHECK(black2000.info_bk == colors::Black);
    CHECK(black2000.info_text == colors::White);
    CHECK(black2000.hot_light == Color::from_argb(0xFF800080));
    CHECK(black2000.bevel->light == colors::White);

    const ClassicPalette mono = ClassicPalette::monochrome_3x();
    CHECK(mono.face == Color::from_argb(0xFFC0C0C0));
    CHECK(mono.window == colors::White);
    CHECK(mono.menu == colors::White);
    CHECK(mono.selection == colors::Black);
    CHECK(mono.gray_text == Color::from_argb(0xFF808080));

    const HighContrastPalette black10 = HighContrastPalette::windows10_high_contrast_black();
    CHECK(black10.accent == Color::from_argb(0xFF1AEBFF));
    CHECK(black10.accent_text == colors::Black);
    CHECK(black10.disabled_text == Color::from_argb(0xFF3FF23F));
    CHECK(black10.hyperlink == Color::from_argb(0xFFFFFF00));
}

TEST_CASE("HighContrastPalette::aquatic carries Windows 11's default contrast theme", "[theme][high-contrast]")
{
    const HighContrastPalette aquatic = HighContrastPalette::aquatic();
    CHECK(aquatic.window_background == Color::from_argb(0xFF202020));
    CHECK(aquatic.window_text == colors::White);
    CHECK(aquatic.accent == Color::from_argb(0xFF8EE3F0));
    CHECK(aquatic.accent_text == Color::from_argb(0xFF263B50));
    CHECK(aquatic.hyperlink == Color::from_argb(0xFF75E9FC));
    CHECK(aquatic.disabled_text == Color::from_argb(0xFFA6A6A6));
}

// ---- Palette ----------------------------------------------------------------------------

TEST_CASE("Palette::fluent_light/dark spot-check a few tokens against the shipped values", "[palette]")
{
    const Palette light = Palette::fluent_light();
    const Palette dark  = Palette::fluent_dark();

    // The solid base background.
    CHECK(light.solid_background_base == Color::from_argb(0xFFF3F3F3));
    CHECK(dark.solid_background_base == Color::from_argb(0xFF202020));

    // The default control fill.
    CHECK(light.control_fill_default == Color::from_argb(0xB3FFFFFF));
    CHECK(dark.control_fill_default == Color::from_argb(0x0FFFFFFF));

    // Primary text: light is 89%-alpha black, not pure black.
    CHECK(light.text_fill_primary == Color::from_argb(0xE4000000));
    CHECK(dark.text_fill_primary == colors::White);

    // The focus ring.
    CHECK(light.focus_stroke_outer == Color::from_argb(0xE4000000));
    CHECK(dark.focus_stroke_outer == colors::White);

    // The acrylic fallback. A near-miss of solid_background_tertiary
    // (4/255 apart in Dark), so it is pinned against being merged into that token.
    CHECK(light.acrylic_fallback_default == Color::from_argb(0xFFF9F9F9));
    CHECK(dark.acrylic_fallback_default == Color::from_argb(0xFF2C2C2C));
    CHECK(light.acrylic_fallback_default == light.solid_background_tertiary);
    CHECK(dark.acrylic_fallback_default != dark.solid_background_tertiary);
}

TEST_CASE("Fluent overlays fill with the acrylic fallback, not the page color", "[palette][theme]")
{
    // Flyouts, menus, tooltips and the ComboBox popup are acrylic, so they take its
    // fallback color. DialogControl stays on the solid base background.
    auto luma = [](const Color& c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; };
    for (const Palette& palette : {Palette::fluent_light(), Palette::fluent_dark()})
    {
        const Theme theme = Theme::fluent(palette);
        CHECK(theme.surfaces.menu == palette.acrylic_fallback_default);
        CHECK(theme.surfaces.tooltip == palette.acrylic_fallback_default);
        CHECK(theme.colors().background == palette.solid_background_base);
        // An overlay is lighter than the page in both themes.
        CHECK(luma(theme.surfaces.menu) > luma(theme.colors().background));
        CHECK(luma(theme.surfaces.tooltip) > luma(theme.colors().background));
    }
}

// ---- The Fluent 2 contour gradient ------------------------------------------------------

TEST_CASE("The neutral elevation border flips direction per theme; the accent one is flipped in both",
          "[palette][theme][elevation]")
{
    // The neutral elevation border is flipped in Light only, the accent
    // one in both.
    const Theme light = Theme::fluent(Palette::fluent_light());
    const Theme dark  = Theme::fluent(Palette::fluent_dark());

    REQUIRE(light.button_normal.border_gradient.has_value());
    REQUIRE(dark.button_normal.border_gradient.has_value());
    CHECK(light.button_normal.border_gradient_extent_px < 0.0f); // flipped: bottom-anchored
    CHECK(dark.button_normal.border_gradient_extent_px > 0.0f);  // unflipped: top-anchored
    CHECK(light.button_hover.border_gradient_extent_px == light.button_normal.border_gradient_extent_px);
    CHECK(dark.button_hover.border_gradient_extent_px == dark.button_normal.border_gradient_extent_px);

    CHECK(light.accent_button_normal.border_gradient_extent_px < 0.0f);
    CHECK(dark.accent_button_normal.border_gradient_extent_px < 0.0f);

    // 3 device pixels in absolute units: a bounding-box mapping would wash the whole border.
    CHECK(std::abs(light.button_normal.border_gradient_extent_px) == 3.0f);
    CHECK(std::abs(dark.accent_button_normal.border_gradient_extent_px) == 3.0f);
}

TEST_CASE("The accent elevation border uses the on-accent stroke pair, not the neutral one",
          "[palette][theme][elevation]")
{
    // The accent elevation border fades the secondary on-accent stroke into the
    // default one. In Light the two "Secondary" stops are 40%-black vs 16%-black.
    for (const Palette& palette : {Palette::fluent_light(), Palette::fluent_dark()})
    {
        const Theme theme = Theme::fluent(palette);
        REQUIRE(theme.accent_button_normal.border_gradient.has_value());
        REQUIRE(theme.button_normal.border_gradient.has_value());
        const auto& accent  = theme.accent_button_normal.border_gradient->stops;
        const auto& neutral = theme.button_normal.border_gradient->stops;
        REQUIRE(accent.size() == 2);
        CHECK(accent[0].offset == 0.33f);
        CHECK(accent[1].offset == 1.0f);
        CHECK(accent[0].color == palette.control_stroke_on_accent_secondary);
        CHECK(accent[1].color == palette.control_stroke_on_accent_default);
        CHECK(accent[0].color != neutral[0].color);
        CHECK(accent[1].color != neutral[1].color);
    }
    // The default on-accent stroke is white-alpha in both themes, unlike every neutral stroke.
    CHECK(Palette::fluent_light().control_stroke_on_accent_default ==
          Palette::fluent_dark().control_stroke_on_accent_default);
}

TEST_CASE("Pressed drops the contour gradient for a flat stroke, and nothing else moves", "[theme][elevation]")
{
    // Elevation 2 -> 1 is below the shadow threshold, so only the contour flattens.
    for (const Palette& palette : {Palette::fluent_light(), Palette::fluent_dark()})
    {
        const Theme theme = Theme::fluent(palette);
        CHECK_FALSE(theme.button_pressed.border_gradient.has_value());
        CHECK_FALSE(theme.button_disabled.border_gradient.has_value());
        CHECK(theme.button_pressed.border == palette.control_stroke_default);
        CHECK(theme.button_disabled.border == palette.control_stroke_default);
    }
}

// ---- Tokens corrected against the shipped values ----------------------------------------

TEST_CASE("The alt control fills are the shipped values, not an approximation of them", "[palette]")
{
    const Palette light = Palette::fluent_light();
    const Palette dark  = Palette::fluent_dark();

    CHECK(light.control_alt_fill_secondary == Color::from_argb(0x06000000));
    CHECK(light.control_alt_fill_tertiary == Color::from_argb(0x0F000000));
    CHECK(light.control_alt_fill_quaternary == Color::from_argb(0x18000000));
    CHECK(light.control_alt_fill_disabled == colors::Transparent);

    // Dark's rest rung is black-alpha, its interaction rungs white-alpha.
    CHECK(dark.control_alt_fill_secondary == Color::from_argb(0x19000000));
    CHECK(dark.control_alt_fill_tertiary == Color::from_argb(0x0BFFFFFF));
    CHECK(dark.control_alt_fill_quaternary == Color::from_argb(0x12FFFFFF));
    CHECK(dark.control_alt_fill_disabled == colors::Transparent);
}

TEST_CASE("An unchecked indicator's stroke lightens on press", "[theme]")
{
    // The Pressed unchecked stroke is
    // the strong stroke's disabled color, not its default one.
    for (const Palette& palette : {Palette::fluent_light(), Palette::fluent_dark()})
    {
        const Theme theme = Theme::fluent(palette);
        for (const ToggleVisuals* tv : {&ui::detail::controls(theme).check_box_box, &ui::detail::controls(theme).radio_ring})
        {
            CHECK(tv->for_state(ButtonState::Normal, false).border == palette.control_strong_stroke_default);
            CHECK(tv->for_state(ButtonState::Hover, false).border == palette.control_strong_stroke_default);
            CHECK(tv->for_state(ButtonState::Pressed, false).border == palette.control_strong_stroke_disabled);
            CHECK(tv->for_state(ButtonState::Disabled, false).border == palette.control_strong_stroke_disabled);
        }
        // ...and ToggleSwitch does not: its pressed off stroke stays
        // the default strong stroke.
        CHECK(ui::detail::controls(theme).toggle_track.for_state(ButtonState::Pressed, false).border ==
              palette.control_strong_stroke_default);
        CHECK(ui::detail::controls(theme).toggle_track.for_state(ButtonState::Disabled, false).border ==
              palette.control_strong_stroke_disabled);
        CHECK(ui::detail::controls(theme).toggle_track.pressed.off.background ==
              ui::detail::controls(theme).check_box_box.pressed.off.background);
    }
}

TEST_CASE("The modal scrim is 30 percent black, identical in both themes", "[theme]")
{
    CHECK(Theme::fluent(Palette::fluent_light()).colors().scrim == Color::from_argb(0x4D000000));
    CHECK(Theme::fluent(Palette::fluent_dark()).colors().scrim == Color::from_argb(0x4D000000));
}

TEST_CASE("A default AccentRamp is the shipped Windows default ramp, not the base accent repeated", "[theme][palette]")
{
    // The fallback ramp. Filled accent controls read Dark1
    // in light theme and Light2 in dark, neither of which is the base #0078D4.
    const AccentRamp ramp;
    CHECK(ramp.dark1 == Color::from_argb(0xFF005A9E));
    CHECK(ramp.light2 == Color::from_argb(0xFF76B9ED));

    OsThemeSnapshot os;
    os.dark = false;
    CHECK(resolve_theme(ThemeRequest{}, os).accent_button_normal.background == Color::from_argb(0xFF005A9E));
    os.dark = true;
    CHECK(resolve_theme(ThemeRequest{}, os).accent_button_normal.background == Color::from_argb(0xFF76B9ED));

    // The static tables agree with the ramp, so Theme::light() is the same blue as resolve_theme().
    CHECK(Palette::fluent_light().accent_fill_default == ramp.dark1);
    CHECK(Palette::fluent_dark().accent_fill_default == ramp.light2);
}

// ---- Shadows ----------------------------------------------------------------------------

TEST_CASE("Nothing below elevation 4 casts a shadow", "[theme][elevation]")
{
    for (const bool dark : {false, true})
    {
        CHECK_FALSE(ShadowRecipe::for_elevation(elevation_z::Control, dark).casts());
        CHECK_FALSE(ShadowRecipe::for_elevation(elevation_z::Layer, dark).casts());
        CHECK(ShadowRecipe::for_elevation(elevation_z::Card, dark).casts());
    }
}

TEST_CASE("Dark-theme shadows are stronger than light at every elevation that casts one", "[theme][elevation]")
{
    for (const float z : {elevation_z::Card, elevation_z::ToolTip, elevation_z::Flyout, elevation_z::Dialog})
    {
        const ShadowRecipe light = ShadowRecipe::for_elevation(z, /*dark=*/false);
        const ShadowRecipe dark  = ShadowRecipe::for_elevation(z, /*dark=*/true);
        CHECK(dark.directional_alpha > light.directional_alpha);
        // Blur and offset are theme-independent, only alpha differs.
        CHECK(dark.directional_blur == light.directional_blur);
        CHECK(dark.directional_y == light.directional_y);
    }
}

TEST_CASE("The faked blur is half-strength at the caster's edge and a tail beyond it", "[theme][elevation]")
{
    // A Gaussian straddles its edge, so the shadow is at half its opacity on the caster's
    // boundary. Replays the emitted fills to get the composite opacity at a distance.
    TestRenderer       renderer;
    const Rect         bounds{100.0f, 100.0f, 200.0f, 80.0f};
    const ShadowRecipe flyout = ShadowRecipe::for_elevation(elevation_z::Flyout, /*dark=*/true);
    draw_drop_shadow(renderer, bounds, 8.0f, flyout);
    REQUIRE_FALSE(renderer.commands.empty());

    // Sample at the left edge's vertical center, where the y-offset plays no part.
    auto composite_at = [&](float distance_outside)
    {
        const float x   = bounds.left() - distance_outside;
        const float y   = bounds.y + bounds.height * 0.5f;
        float       acc = 0.0f;
        for (const auto& cmd : renderer.commands)
        {
            const auto* fill = std::get_if<FillRoundedRect>(&cmd);
            REQUIRE(fill != nullptr); // the stack is fills and nothing else
            const Rect& r = fill->bounds;
            if (x >= r.left() && x <= r.right() && y >= r.top() && y <= r.bottom())
            {
                acc = acc + fill->color.a * (1.0f - acc); // source-over
            }
        }
        return acc;
    };

    // About half the recipe's alpha at the boundary. A range, because each ring is flat and aims
    // at its midpoint, so the innermost one carries the true value a pixel out.
    CHECK(composite_at(0.0f) > flyout.directional_alpha * 0.35f);
    CHECK(composite_at(0.0f) < flyout.directional_alpha * 0.55f);
    // Decays outward and is spent well before the blur radius, which is why the visible
    // inset for a flyout (10 epx) is smaller than its computed one (16 epx).
    CHECK(composite_at(4.0f) < composite_at(0.0f));
    CHECK(composite_at(10.0f) < composite_at(4.0f));
    CHECK(composite_at(10.0f) < 0.02f);
    CHECK(composite_at(flyout.directional_blur + 1.0f) == 0.0f);
}

TEST_CASE("The light-theme low branch saturates at elevation 8; dark is flat across it", "[theme][elevation]")
{
    // min(E/100 + 0.06, 0.14) caps at E = 8, so a ToolTip (E = 8) and a Flyout (E = 16) share a
    // light-theme opacity.
    const ShadowRecipe tooltip_l = ShadowRecipe::for_elevation(elevation_z::ToolTip, false);
    const ShadowRecipe flyout_l  = ShadowRecipe::for_elevation(elevation_z::Flyout, false);
    CHECK(tooltip_l.directional_alpha == 0.14f);
    CHECK(flyout_l.directional_alpha == 0.14f);
    CHECK(flyout_l.directional_blur > tooltip_l.directional_blur);

    CHECK(ShadowRecipe::for_elevation(elevation_z::Card, false).directional_alpha == Catch::Approx(0.10f));
    for (const float z : {elevation_z::Card, elevation_z::ToolTip, elevation_z::Flyout})
    {
        CHECK(ShadowRecipe::for_elevation(z, /*dark=*/true).directional_alpha == 0.26f);
        // Ambient light is suppressed entirely below elevation 16.
        CHECK(ShadowRecipe::for_elevation(z, /*dark=*/true).ambient_alpha == 0.0f);
    }

    // The high branch (Dialog/Window, E clamped to 64) switches the ambient light back on.
    const ShadowRecipe dialog = ShadowRecipe::for_elevation(elevation_z::Dialog, false);
    CHECK(dialog.directional_blur == 64.0f); // clamped
    CHECK(dialog.directional_y == 32.0f);
    CHECK(dialog.ambient_alpha == 0.15f);
    CHECK(dialog.directional_alpha == 0.19f);
}

// ---- Retro theme packs ------------------------------------------------------------------

TEST_CASE("Windows9x is the Windows 95/98 \"Windows Standard\" row, whole", "[theme][retro]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});

    CHECK(theme.corner_radius == 0.0f);
    // Windows 95 and 98 SE are byte-identical on every color slot; `#D4D0C8` is Windows 2000,
    // `ClassicPalette::windows_standard_2000()`.
    CHECK(theme.colors().background == Color::from_argb(0xFFC0C0C0));
    CHECK(theme.button_normal.background == Color::from_argb(0xFFC0C0C0));
    CHECK(theme.colors().selection == Color::from_argb(0xFF000080));
    CHECK(theme.button_normal.border_style == BorderStyle::Raised);
    CHECK(theme.button_pressed.border_style == BorderStyle::Sunken);
    CHECK_FALSE(theme.button_normal.background_gradient.has_value());
    // A command button has no hover state in this era. Hot-tracking is for toolbar and menu buttons.
    CHECK(theme.button_hover == theme.button_normal);
    // The real GetSysColor() defaults for "Windows Standard": a five-stop ramp, with 3DLIGHT a
    // real step between face and highlight.
    CHECK(theme.button_normal.bevel_light == Color::from_argb(0xFFDFDFDF));
    CHECK(theme.button_normal.bevel_highlight == colors::White);
    CHECK(theme.button_normal.bevel_shadow == Color::from_argb(0xFF808080));
    CHECK(theme.button_normal.bevel_dark_shadow == colors::Black);
    // A classic push button is compact, much tighter than Fluent's 11/5.
    CHECK(theme.padding.left < 11.0f);
    CHECK(theme.padding.top < 5.0f);
    CHECK(ui::detail::controls(theme).toggle_box_size == 13.0f); // the real classic checkbox/radio size
    CHECK(theme.focus_visual == FocusVisual::ClassicDotted);     // dotted inset rect, not an outward glow ring
}

// ---- Luna -------------------------------------------------------------------------------
// Values match XP SP3's Luna Normal Blue: its color hints, or its bitmaps' pixels.
TEST_CASE("Luna's push button is a near-white gradient inside a #003C74 border", "[theme][retro][luna]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    // One arc of ~3px, measured off the border's own (1,4)->(4,1) run.
    CHECK(theme.corner_radius == 3.0f);
    REQUIRE(theme.button_normal.background_gradient.has_value());
    // Near-white, not blue: white to the `#D6D0C5` turn-down two rows above the bottom border.
    // An `#EFF5FB`->`#C4DFFB` face is Aero's.
    CHECK(theme.button_normal.background_gradient->stops.front().color == colors::White);
    CHECK(theme.button_normal.background_gradient->stops.back().color == Color::from_argb(0xFFD6D0C5));
    // No bevel: Luna shades pressed state with a darker gradient.
    CHECK(theme.button_normal.border_style != BorderStyle::Raised);
    CHECK(theme.button_normal.border_style != BorderStyle::Sunken);
    // `BorderColorHint = 0 60 116`. `draw_border` must be set or ButtonControl::render() strokes nothing.
    CHECK(theme.button_normal.border == Color::from_argb(0xFF003C74));
    CHECK(theme.button_normal.draw_border);
}

TEST_CASE("Hovering a Luna button adds an amber ring and does not touch its face", "[theme][retro][luna]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    // The hot cell is pixel-identical to the normal one from row 4 to row 17. Hover is only a
    // 2px amber ring at inset 1, inside the border.
    REQUIRE(theme.button_hover.background_gradient.has_value());
    CHECK(theme.button_hover.background_gradient->stops == theme.button_normal.background_gradient->stops);
    REQUIRE(theme.button_hover.inner_border.has_value());
    CHECK(theme.button_hover.inner_border_width == 2.0f);
    CHECK(theme.button_hover.inner_border->stops.front().color == Color::from_argb(0xFFFFF0CF));
    CHECK(theme.button_hover.inner_border->stops.back().color == Color::from_argb(0xFFE59700));
    // Pressed drops the ring and inverts the light direction: dark at the top.
    CHECK_FALSE(theme.button_pressed.inner_border.has_value());
    REQUIRE(theme.button_pressed.background_gradient.has_value());
    CHECK(theme.button_pressed.background_gradient->stops.front().color == Color::from_argb(0xFFD1CCC1));
    // Disabled is a flat recolor: `#F5F4EA` face, `#C9C7BA` border, `#A1A192` label. No emboss.
    CHECK_FALSE(theme.button_disabled.background_gradient.has_value());
    CHECK(theme.button_disabled.background == Color::from_argb(0xFFF5F4EA));
    CHECK(theme.button_disabled.border == Color::from_argb(0xFFC9C7BA));
    CHECK(theme.button_disabled.text == Color::from_argb(0xFFA1A192));
    // `ButtonVariant::Accent` maps onto PBS_DEFAULTED: a blue inner glow at the hot ring's geometry.
    REQUIRE(theme.accent_button_normal.inner_border.has_value());
    CHECK(theme.accent_button_normal.inner_border->stops.front().color == Color::from_argb(0xFFCEE7FF));
    CHECK(theme.accent_button_normal.inner_border->stops.back().color == Color::from_argb(0xFF6982EE));
}

TEST_CASE("A Luna toolbar button is invisible at rest and warm near-white when hot", "[theme][retro][luna]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    // `TOOLBARBUTTONS` cells 1 and 4 are 100% magenta, so normal and disabled paint nothing.
    CHECK(theme.subtle_button_normal.background == colors::Transparent);
    CHECK(theme.subtle_button_disabled.background == colors::Transparent);
    REQUIRE(theme.subtle_button_hover.background_gradient.has_value());
    // Warm near-white, not the push button's amber, and not Office 2003's blue toolbar wash.
    CHECK(theme.subtle_button_hover.background_gradient->stops.front().color == Color::from_argb(0xFFFEFEFD));
    CHECK(theme.subtle_button_hover.border == Color::from_argb(0xFFCECEC3));
    CHECK_FALSE(theme.subtle_button_hover.inner_border.has_value());
}

TEST_CASE("Luna's check box is a square white well with a green tick", "[theme][retro][luna]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    const ButtonVisual& off = ui::detail::controls(theme).check_box_box.normal.off;
    const ButtonVisual& on  = ui::detail::controls(theme).check_box_box.normal.on;

    // `#1C5180`, not the push button's `#003C74`: the theme hints `29 82 129` against `0 60 116`.
    CHECK(off.border == Color::from_argb(0xFF1C5180));
    CHECK(on.border == Color::from_argb(0xFF1C5180));
    CHECK(off.border != theme.button_normal.border);
    // `FillColorHint = 33 161 33`, the literal tick color in the art. Green, scheme-invariant.
    CHECK(on.text == Color::from_argb(0xFF21A121));
    // The well is a 45 degree ramp, `#DCDCD7` top-left to `#FFFFFF` bottom-right.
    REQUIRE(on.background_gradient.has_value());
    CHECK(on.background_gradient->axis == GradientAxis::Diagonal);
    CHECK(on.background_gradient->stops.front().color == Color::from_argb(0xFFDCDCD7));
    CHECK(on.background_gradient->stops.back().color == colors::White);
    // Square, while the button is rounded: the art's border covers every corner pixel.
    CHECK(ui::detail::controls(theme).toggle_box_radius == 0.0f);
    CHECK(theme.corner_radius > 0.0f);
    // The radio dot is a different green from the tick, and it does not darken on press.
    CHECK(ui::detail::controls(theme).radio_ring.normal.on.text == Color::from_argb(0xFF38B935));
    CHECK(ui::detail::controls(theme).radio_ring.pressed.on.text == Color::from_argb(0xFF38B935));
    CHECK(ui::detail::controls(theme).check_box_box.pressed.on.text == Color::from_argb(0xFF1C861A));
    CHECK(ui::detail::controls(theme).radio_ring.disabled.on.text == Color::from_argb(0xFFC0BFB5));
    CHECK(ui::detail::controls(theme).radio_ring.normal.on.background_gradient == on.background_gradient);
    CHECK(ui::detail::controls(theme).radio_dot.rest == 5.0f); // a 5px disc centered at (6,6) of the 13px bullet
    // Hot adds the amber ring and also flattens the well.
    REQUIRE(ui::detail::controls(theme).check_box_box.hover.off.inner_border.has_value());
    CHECK(ui::detail::controls(theme).check_box_box.hover.off.inner_border_width == 2.0f);
    CHECK(ui::detail::controls(theme).check_box_box.hover.off.background == Color::from_argb(0xFFE7E7E3));
    // Pressed suppresses the ring, as on the push button.
    CHECK_FALSE(ui::detail::controls(theme).check_box_box.pressed.off.inner_border.has_value());
    // Disabled: one `#CAC8BB` for both the outline and the mark.
    CHECK(ui::detail::controls(theme).check_box_box.disabled.on.border == Color::from_argb(0xFFCAC8BB));
    CHECK(ui::detail::controls(theme).check_box_box.disabled.on.text == Color::from_argb(0xFFCAC8BB));
    // The ToggleSwitch keeps the check box's tokens: its knob is the tick green when on.
    CHECK(ui::detail::controls(theme).toggle_track == ui::detail::controls(theme).check_box_box);
}

TEST_CASE("Luna's ToggleSwitch is not a capsule, and its TrackBar is not a disc", "[theme][retro][luna]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    // XP has no switch. Luna's radii top out at 4, so no capsule: 0.15 of the 20px track is the
    // push button's 3px arc.
    CHECK(ui::detail::controls(theme).toggle_switch_track_radius_ratio *
              ui::detail::controls(theme).toggle_switch_track_size.height ==
          3.0f);

    // An 11 x 21 bevelled block whose bands carry the state.
    CHECK(ui::detail::controls(theme).slider_thumb.shape == SliderThumbShape::RoundedRect);
    CHECK(ui::detail::controls(theme).slider_thumb.outer == 21.0f); // the extent across the track
    CHECK(ui::detail::controls(theme).slider_thumb.rest == 11.0f);  // and along it: all five cells are one size,
    CHECK(ui::detail::controls(theme).slider_thumb.hover == 11.0f); // so the thumb never resizes
    CHECK(ui::detail::controls(theme).slider_thumb.pressed == 11.0f);
    CHECK(ui::detail::controls(theme).slider_thumb.radius == 1.0f); // a single cut corner pixel, not 1.5
    REQUIRE(ui::detail::controls(theme).slider_chrome.has_value());
    // `[TrackBar.Thumb] AccentColorHint = 71 196 70` is the rest band to within a unit; hot is amber.
    CHECK(ui::detail::controls(theme).slider_chrome->rest.near_end == Color::from_argb(0xFF48CB46));
    CHECK(ui::detail::controls(theme).slider_chrome->hover.near_end == Color::from_argb(0xFFFAC158));
    // `[TrackBar.Thumb] BorderColorHint = 181 196 205` is the lit edge. Lit from the upper left.
    CHECK(ui::detail::controls(theme).slider_chrome->thumb_edge_light == Color::from_argb(0xFFB5C4CD));
    CHECK(ui::detail::controls(theme).slider_chrome->thumb_edge_dark == Color::from_argb(0xFF778892));
    // The channel is sunken: `[TrackBar.Track] BorderColorHint` top and left, white below and right.
    CHECK(ui::detail::controls(theme).slider_chrome->track_edge_dark == Color::from_argb(0xFF9D9C99));
    CHECK(ui::detail::controls(theme).slider_chrome->track_edge_light == colors::White);
    // `[TrackBar.Track] imagecount = 1`: one uniform channel, no filled half.
    CHECK_FALSE(ui::detail::controls(theme).slider_value_fill);
    // Every Fluent-lineage era fills it. Aero does not either: its groove has no filled part.
    for (ThemeVariant v : {ThemeVariant::Fluent2, ThemeVariant::Fluent, ThemeVariant::Mdl2})
    {
        const Theme other = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        CHECK(ui::detail::controls(other).slider_value_fill);
        CHECK_FALSE(ui::detail::controls(other).slider_chrome.has_value());
    }
}

TEST_CASE("Luna's surfaces come from [SysMetrics], not from its button", "[theme][retro][luna]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    // `Btnface = 236 233 216`, `Highlight = 49 106 197`, `HighlightText = 255 255 255`.
    CHECK(theme.colors().background == Color::from_argb(0xFFECE9D8));
    CHECK(theme.colors().selection == Color::from_argb(0xFF316AC5));
    CHECK(theme.colors().selection_text == colors::White);
    // The field edge is `#7F9DB9`, `[edit] BorderColor`, not the button's outline blue.
    CHECK(theme.surfaces.field_border == Color::from_argb(0xFF7F9DB9));
    CHECK(theme.surfaces.field_border != theme.button_normal.border);
    // The popup surface is `Menu = 255 255 255`; `#ECE9D8` is `MenuBar`. The highlight is the
    // selection blue, not Office's amber.
    CHECK(theme.surfaces.menu == colors::White);
    CHECK(theme.surfaces.menu_highlight == Color::from_argb(0xFF316AC5));
    CHECK(theme.surfaces.menu_highlight_text == colors::White);
    REQUIRE(theme.surfaces.menu_border.has_value());
    CHECK(*theme.surfaces.menu_border == Color::from_argb(0xFF808080));
    // The themed disabled gray is `TextColor = 161 161 146`; `COLOR_GRAYTEXT` `#ACA899` is unthemed.
    CHECK(theme.colors().text_disabled == Color::from_argb(0xFFA1A192));
    // The track's FillColorHint is the thumb's, copy-pasted, and contradicts its own art.
    CHECK(theme.surfaces.scrollbar_track == Color::from_argb(0xFFF9F8F5));
    CHECK(theme.surfaces.scrollbar_thumb == Color::from_argb(0xFFC6D5FD));
    CHECK(theme.surfaces.scrollbar_arrow == Color::from_argb(0xFF4D6185));
    // Luna's separator is a 1px `#CAC6AF`. `divider` must be set or SplitterControl::render() draws black.
    CHECK(theme.divider_visual == ThemeDividerVisual::FlatStroke);
    CHECK(theme.colors().divider == Color::from_argb(0xFFCAC6AF));
}

TEST_CASE("Luna states switch instantly, and its popups are square", "[theme][retro][luna]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    // XP's theme schema carries no times, and GetThemeTransitionDuration is Vista-only.
    CHECK(theme.anim_faster == std::chrono::milliseconds{0});
    // The themed tooltip's radius is 0 and menus are USER32-drawn with a 1px frame.
    CHECK(theme.overlay_corner_radius == 0.0f);
    // Luna and Aero cast the same drop shadow, so the two share a path here.
    CHECK(theme.popup_shadow == ThemePopupShadow::AeroDropShadow);
    // A focused Luna text box shows no chrome change at all, only the caret.
    CHECK(ui::detail::controls(theme).field_focus_border_thickness == Thickness{});
    // No accent pill: the three schemes are hand-authored, and the amber hot state is fixed.
    CHECK_FALSE(ui::detail::controls(theme).accent_selection_indicator);
    // Tahoma 8pt via `MS Shell Dlg 2`, about 11px.
    CHECK(std::string_view{theme.text_style.font_family} == "Tahoma");
    CHECK(theme.text_style.font_size == 11.0f);
    // `ActiveCaption = 0 84 227`. `#0A246A` is Windows 2000's.
    REQUIRE(theme.chrome.caption_color.has_value());
    CHECK(*theme.chrome.caption_color == Color::from_argb(0xFF0054E3));
}

// ---- Aero -------------------------------------------------------------------------------
// Colors come from the Aero visual style.

TEST_CASE("Aero's resting button is opaque neutral gray with a hard 50 % split, not a translucent blue wash",
          "[theme][retro][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    // The button face: four stops, two of them coincident.
    REQUIRE(theme.button_normal.background_gradient.has_value());
    const auto& stops = theme.button_normal.background_gradient->stops;
    REQUIRE(stops.size() == 4);
    CHECK(stops[0].color == Color::from_argb(0xFFF3F3F3));
    CHECK(stops[1].color == Color::from_argb(0xFFEBEBEB));
    CHECK(stops[2].color == Color::from_argb(0xFFDDDDDD));
    CHECK(stops[3].color == Color::from_argb(0xFFCDCDCD));
    // The hard split, not a smooth ramp, is what makes the surface read as glossy.
    CHECK(stops[1].offset == 0.5f);
    CHECK(stops[2].offset == 0.5f);
    // Gray means r == g == b, and opaque means alpha 1.
    for (const GradientStop& s : stops)
    {
        CHECK(s.color.r == s.color.g);
        CHECK(s.color.g == s.color.b);
        CHECK(s.color.a == 1.0f);
    }
    CHECK(theme.button_normal.border == Color::from_argb(0xFF707070));
}

TEST_CASE("Aero's button states move blue in, and the pressed state drops the ring and shades its inside",
          "[theme][retro][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    // Hover overlay and border.
    REQUIRE(theme.button_hover.background_gradient.has_value());
    CHECK(theme.button_hover.background_gradient->stops.front().color == Color::from_argb(0xFFEAF6FD));
    CHECK(theme.button_hover.background_gradient->stops.back().color == Color::from_argb(0xFFA7D9F5));
    CHECK(theme.button_hover.border == Color::from_argb(0xFF3C7FB1));

    // Pressed deepens the blue and darkens the border.
    REQUIRE(theme.button_pressed.background_gradient.has_value());
    CHECK(theme.button_pressed.background_gradient->stops.front().color == Color::from_argb(0xFFC2E4F6));
    CHECK(theme.button_pressed.background_gradient->stops.back().color == Color::from_argb(0xFF90CBEB));
    CHECK(theme.button_pressed.border == Color::from_argb(0xFF2C628B));

    // The white inner ring shows at rest, hover and disabled; the
    // press animation fades it out.
    CHECK(theme.button_normal.inner_border.has_value());
    CHECK(theme.button_hover.inner_border.has_value());
    CHECK_FALSE(theme.button_pressed.inner_border.has_value());
    CHECK(theme.button_disabled.inner_border.has_value());

    // ...and pressing shades the inside instead of moving the label.
    REQUIRE(theme.button_pressed.inner_shadow.has_value());
    CHECK(*theme.button_pressed.inner_shadow == Color::from_argb(0x80333333));
    CHECK_FALSE(theme.button_normal.inner_shadow.has_value());

    // Disabled: a flat `#F4F4F4` face, `#ADB2B5` border, `#ADADAD` label, no gradient.
    CHECK_FALSE(theme.button_disabled.background_gradient.has_value());
    CHECK(theme.button_disabled.background == Color::from_argb(0xFFF4F4F4));
    CHECK(theme.button_disabled.border == Color::from_argb(0xFFADB2B5));
    CHECK(theme.button_disabled.text == Color::from_argb(0xFFADADAD));
}

TEST_CASE("Aero's default button is the normal face with a solid cyan inner ring, not a filled accent surface",
          "[theme][retro][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    // The default button's inner ring is `#F900CCFF`; the Win32 `Defaulted` AccentColorHint is
    // `#04CCFF`, the same color to within the alpha. The face underneath is the Normal one.
    CHECK(theme.accent_button_normal.background_gradient == theme.button_normal.background_gradient);
    REQUIRE(theme.accent_button_normal.inner_border.has_value());
    const Color ring = theme.accent_button_normal.inner_border->stops.front().color;
    CHECK(ring == Color::from_rgba(0x00, 0xCC, 0xFF, 0xF9));
    // The ring is what tells it apart from the plain button.
    CHECK(theme.accent_button_normal.inner_border != theme.button_normal.inner_border);
}

TEST_CASE("Aero's check box stays a light well with a dark glyph - the era has no filled toggle", "[theme][retro][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    // The well is `#F4F4F4` inside `#8E8F8F`, checked or not, and checking adds a
    // `#31347C` glyph. CheckBoxControl::render() takes the visual's `text` as the glyph color.
    CHECK(ui::detail::controls(theme).check_box_box.normal.on.background ==
          ui::detail::controls(theme).check_box_box.normal.off.background);
    CHECK(ui::detail::controls(theme).check_box_box.normal.on.background == Color::from_argb(0xFFF4F4F4));
    CHECK(ui::detail::controls(theme).check_box_box.normal.on.border == Color::from_argb(0xFF8E8F8F));
    CHECK(ui::detail::controls(theme).check_box_box.normal.on.text == Color::from_argb(0xFF31347C));
    CHECK(ui::detail::controls(theme).check_box_box.normal.off.text == colors::Transparent);

    // Hover and press tint the inside of the well with the push button's border blues.
    CHECK(ui::detail::controls(theme).check_box_box.hover.on.background == Color::from_argb(0xFFDEF9FA));
    CHECK(ui::detail::controls(theme).check_box_box.hover.on.border == theme.button_hover.border);
    CHECK(ui::detail::controls(theme).check_box_box.pressed.on.background == Color::from_argb(0xFFC2E4F6));
    CHECK(ui::detail::controls(theme).check_box_box.pressed.on.border == theme.button_pressed.border);
    // The radio is the same well with the same navy mark as a dot.
    CHECK(ui::detail::controls(theme).radio_ring.normal.on.background ==
          ui::detail::controls(theme).check_box_box.normal.on.background);
    CHECK(ui::detail::controls(theme).radio_ring.normal.on.border ==
          ui::detail::controls(theme).check_box_box.normal.on.border);
    CHECK(ui::detail::controls(theme).radio_ring.normal.on.text == ui::detail::controls(theme).check_box_box.normal.on.text);

    // The box is recessed inside the well: a diagonal rim and 7x7 fill per state, square corners.
    // The radio has its own rim stops.
    CHECK(ui::detail::controls(theme).toggle_box_radius == 0.0f);
    REQUIRE(ui::detail::controls(theme).check_box_box.normal.off.inset_well.has_value());
    CHECK(ui::detail::controls(theme).check_box_box.normal.off.inset_well ==
          ui::detail::controls(theme).check_box_box.normal.on.inset_well);
    CHECK(ui::detail::controls(theme).check_box_box.hover.off.inset_well->fill->stops.front().color ==
          Color::from_argb(0xFFB1DFFD));
    CHECK(ui::detail::controls(theme).check_box_box.pressed.off.inset_well->fill->stops.front().color ==
          Color::from_argb(0xFF7FBADC));
    CHECK_FALSE(ui::detail::controls(theme).check_box_box.disabled.off.inset_well->fill.has_value());
    REQUIRE(ui::detail::controls(theme).radio_ring.normal.off.inset_well.has_value());
    CHECK(ui::detail::controls(theme).radio_ring.normal.off.inset_well->rim.stops.front().color ==
          Color::from_argb(0xFFB3B8BD));
    CHECK(ui::detail::controls(theme).radio_ring.normal.off.inset_well->fill ==
          ui::detail::controls(theme).check_box_box.normal.off.inset_well->fill);

    // The checked radio dot is a radial bead in a `#193B55` ring, and a press previews it.
    REQUIRE(ui::detail::controls(theme).radio_ring.normal.on.mark_gradient.has_value());
    CHECK(ui::detail::controls(theme).radio_ring.normal.on.mark_gradient->stops.back().color ==
          Color::from_argb(0xFF0B82C7));
    CHECK(ui::detail::controls(theme).radio_ring.normal.on.mark_gradient->center == Point{0.25f, 0.25f});
    CHECK(ui::detail::controls(theme).radio_ring.normal.on.mark_stroke == Color::from_argb(0xFF193B55));
    CHECK(ui::detail::controls(theme).radio_ring.pressed.off.mark_gradient ==
          ui::detail::controls(theme).radio_ring.pressed.on.mark_gradient);
    CHECK_FALSE(ui::detail::controls(theme).radio_ring.normal.off.mark_gradient.has_value());
    CHECK(ui::detail::controls(theme).radio_dot.rest == 7.0f);
}

TEST_CASE("Aero's system colors are the Aero style's, with Windows 7's GrayText", "[theme][retro][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    CHECK(theme.colors().background == Color::from_argb(0xFFF0F0F0)); // `Btnface`, not `#F0F5FB`
    CHECK(theme.colors().field == colors::White);                     // `Window`
    CHECK(theme.colors().selection == Color::from_argb(0xFF3399FF));  // `Highlight`, identical in Vista and 7
    CHECK(theme.colors().selection_text == colors::White);
    CHECK(theme.colors().link == Color::from_argb(0xFF0066CC));          // `HyperLinkText` == `HotTracking`
    CHECK(theme.colors().text_disabled == Color::from_argb(0xFF6D6D6D)); // `GrayText`, Windows 7's column
    CHECK(theme.surfaces.field_border == Color::from_argb(0xFFABADB3));  // `EditStyle` BorderColorHint
    CHECK(theme.surfaces.tooltip_text == Color::from_argb(0xFF575757));
    // `#FFFFE1` is the Classic tooltip, `COLOR_INFOBK`. The themed one is white-to-pale.
    CHECK(theme.surfaces.tooltip != Color::from_argb(0xFFFFFFE1));
    // Default-constructs to opaque black if a resolver forgets it, and `Splitter` reads it always.
    CHECK(theme.colors().divider != colors::Black);
}

TEST_CASE("Aero's scrollbar thumb is gray at rest, not the hover blue", "[theme][retro][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    // The thumb's rest fill is the button face's hard-split neutral ramp; blue is hover.
    const Color thumb = theme.surfaces.scrollbar_thumb;
    CHECK(thumb.r == thumb.g);
    CHECK(std::abs(thumb.b - thumb.r) < 0.02f);
    CHECK(theme.surfaces.scrollbar_track == Color::from_argb(0xFFF4F4F4));
}

TEST_CASE("Aero's type ramp carries color as well as size - the main instruction is #003399", "[theme][retro][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    // Segoe UI 9 pt at 96 dpi is 12 epx; the main instruction is the same face at 12 pt = 16 epx.
    CHECK(theme.text_style.font_family == "Segoe UI");
    CHECK(theme.text_style.font_size == 12.0f);
    CHECK(theme.type.body.font_size == 12.0f);
    CHECK(theme.type.header.font_size == 16.0f);

    const Color main_instruction = Color::from_argb(0xFF003399);
    REQUIRE(theme.type.color_for_role(TextRole::Header).has_value());
    CHECK(*theme.type.color_for_role(TextRole::Header) == main_instruction);
    CHECK(*theme.type.color_for_role(TextRole::Subheader) == main_instruction);
    // Body text is plain black. The ramp colors the instruction rungs only.
    CHECK_FALSE(theme.type.color_for_role(TextRole::Body).has_value());
}

TEST_CASE("Every other theme leaves the per-role text color unset, so a role still paints in colors.text", "[theme]")
{
    for (ThemeVariant v :
         {ThemeVariant::Fluent2,
          ThemeVariant::Fluent,
          ThemeVariant::Metro,
          ThemeVariant::Mdl2,
          ThemeVariant::Luna,
          ThemeVariant::Windows9x,
          ThemeVariant::Windows3x,
          ThemeVariant::Windows1x})
    {
        const Theme theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        CHECK_FALSE(theme.type.color_for_role(TextRole::Header).has_value());
        CHECK_FALSE(theme.type.color_for_role(TextRole::Body).has_value());
    }
}

TEST_CASE("Aero rounds at 2.75 and casts the CS_DROPSHADOW popup shadow rather than Fluent's", "[theme][retro][aero]")
{
    const Theme aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    CHECK(aero.corner_radius == 2.75f); // the button's outer radius; the ring falls out at 1.75
    CHECK(aero.popup_shadow == ThemePopupShadow::AeroDropShadow);

    // Aero has no elevation scale; the two-light Fluent recipe is a different mechanism.
    for (ThemeVariant v : {ThemeVariant::Fluent2, ThemeVariant::Fluent})
    {
        CHECK(resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{}).popup_shadow == ThemePopupShadow::FluentTwoLight);
    }
}

TEST_CASE("Aero's popup shadow is an offset hard-edged block, not a soft omnidirectional glow", "[theme][retro][aero]")
{
    const Rect   popup{100.0f, 100.0f, 200.0f, 80.0f};
    TestRenderer renderer;
    draw_aero_popup_shadow(renderer, popup, 2.0f);

    REQUIRE_FALSE(renderer.commands.empty());
    // Every band is displaced +5/+5, so the shadow sits below and to the right of the popup,
    // unlike Fluent's centered blur.
    for (std::size_t i = 0; i < renderer.commands.size(); ++i)
    {
        const auto* fill = std::get_if<FillRoundedRect>(&renderer.commands[i]);
        REQUIRE(fill != nullptr);
        const float cx = fill->bounds.left() + fill->bounds.width * 0.5f;
        const float cy = fill->bounds.top() + fill->bounds.height * 0.5f;
        CHECK(cx == Catch::Approx(popup.left() + popup.width * 0.5f + 5.0f));
        CHECK(cy == Catch::Approx(popup.top() + popup.height * 0.5f + 5.0f));
        CHECK(fill->color.r == 0.0f); // `#71000000`: pure black, alpha only
        CHECK(fill->color.g == 0.0f);
        CHECK(fill->color.b == 0.0f);
    }
    // `shadowBounds` is the popup rect displaced +5,+5 and the whole shadow lives
    // inside it, in the strip the callers' `Margin="0,0,5,5"` reserves.
    const auto* outermost = std::get_if<FillRoundedRect>(&renderer.commands[0]);
    REQUIRE(outermost != nullptr);
    CHECK(outermost->bounds.right() <= popup.right() + 5.0f);
    CHECK(outermost->bounds.bottom() <= popup.bottom() + 5.0f);
    CHECK(outermost->bounds.right() > popup.right() + 4.0f); // ...and it does reach that far
    // Nothing spills past the popup's top or left edge: an offset block, not a halo.
    CHECK(outermost->bounds.left() >= popup.left());
    CHECK(outermost->bounds.top() >= popup.top());
    // The innermost ring is the solid center, the shadow bounds deflated by the depth on each side
    // (:105-108): 10 px narrower than the popup, plus the half-pixel first stop.
    const auto* innermost = std::get_if<FillRoundedRect>(&renderer.commands.back());
    REQUIRE(innermost != nullptr);
    CHECK(innermost->bounds.width == Catch::Approx(popup.width - 9.0f));
}

TEST_CASE("An Aero button actually strokes its border - a Rounded visual needs draw_border to show one",
          "[theme][retro][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    // `ButtonControl::render()` passes `draw_flat_border_fallback = false`, so a `Rounded` visual with no
    // bevel or border gradient draws no outline unless it opts in.
    for (const ButtonVisual& v : {theme.button_normal, theme.button_hover, theme.button_pressed, theme.button_disabled})
    {
        CHECK(v.border_style == BorderStyle::Rounded);
        CHECK(v.draw_border);
    }

    ButtonControl button{"OK"};
    button.arrange(Rect{0.0f, 0.0f, 80.0f, 23.0f});
    TestRenderer renderer;
    button.render(renderer, theme);

    bool stroked_border = false;
    bool stroked_ring   = false;
    for (std::size_t i = 0; i < renderer.commands.size(); ++i)
    {
        if (const auto* s = std::get_if<StrokeRoundedRect>(&renderer.commands[i]))
        {
            stroked_border |= s->color == Color::from_argb(0xFF707070);
        }
        if (std::holds_alternative<StrokeRoundedRectGradient>(renderer.commands[i]))
        {
            stroked_ring = true;
        }
    }
    CHECK(stroked_border);
    CHECK(stroked_ring); // the white inner highlight, one pixel in at one less radius
}

TEST_CASE("A focused Aero text field turns its border blue all the way round and shows no dotted rectangle",
          "[theme][retro][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    // `EPSN_FOCUSED` is `#3D7BAD` at the top, and a text box draws no focus rectangle:
    // the blue border is the cue.
    REQUIRE(ui::detail::controls(theme).field_focus_underline.has_value());
    CHECK(*ui::detail::controls(theme).field_focus_underline == Color::from_argb(0xFF3D7BAD));
    // Uniform, unlike Fluent 2's bottom-only `0,0,0,2` underline.
    CHECK(ui::detail::controls(theme).field_focus_border_thickness == Thickness{1.0f});
    // Push buttons keep the dotted rectangle on top of the current state; a field must not show one.
    CHECK(theme.focus_visual == FocusVisual::ClassicDotted);
    CHECK(theme.colors().focus == colors::Black);
}

TEST_CASE("Aero marks a selected row with its own highlight, never a sliding accent bar", "[theme][retro][aero]")
{
    const Theme aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    // `traits_of()` gives Aero no accent, so the accent selection bar would draw in the pale button
    // face. Selection is the flat highlight the pane already paints.
    CHECK_FALSE(ui::detail::controls(aero).accent_selection_indicator);

    CHECK(aero.colors().selection == Color::from_argb(0xFF3399FF));
    CHECK(aero.colors().selection_text == colors::White);

    // Fluent 2 and Fluent 1 both draw one, a different bar each (see `nav_indicator`).
    CHECK(ui::detail::controls(resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{}))
              .accent_selection_indicator);
    CHECK(ui::detail::controls(resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{}))
              .accent_selection_indicator);
}

// ---- Non-client chrome hints ------------------------------------------------------------

TEST_CASE("Only Fluent 2 lets DWM put a system backdrop behind the page", "[theme][chrome][backdrop]")
{
    // Mica ships in 2021, after every earlier era. A pack that leaves the client area
    // transparent gets its bare bands tinted by the wallpaper.
    CHECK(resolve_theme(ThemeRequest{}, OsThemeSnapshot{}).chrome.allows_system_backdrop);
    CHECK(resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{.dark = true}).chrome.allows_system_backdrop);

    for (ThemeVariant v :
         {ThemeVariant::Fluent,
          ThemeVariant::Mdl2,
          ThemeVariant::Metro,
          ThemeVariant::Aero,
          ThemeVariant::Luna,
          ThemeVariant::Windows9x,
          ThemeVariant::Windows3x,
          ThemeVariant::Windows1x})
    {
        CAPTURE(static_cast<int>(v));
        CHECK_FALSE(resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{}).chrome.allows_system_backdrop);
    }

    // A contrast ratio against `colors.background` means nothing over the wallpaper, so high
    // contrast declines even when Fluent 2 is asked for.
    CHECK_FALSE(resolve_theme(ThemeRequest{.mode = ThemeMode::HighContrast, .variant = ThemeVariant::Fluent2},
                              OsThemeSnapshot{.high_contrast = true})
                    .chrome.allows_system_backdrop);
}


TEST_CASE("Fluent2 chrome hints follow dark mode but leave corners on DWM's own default", "[theme][chrome]")
{
    const Theme light = resolve_theme(ThemeRequest{}, OsThemeSnapshot{.dark = false});
    const Theme dark  = resolve_theme(ThemeRequest{}, OsThemeSnapshot{.dark = true});

    CHECK_FALSE(light.chrome.dark_titlebar);
    CHECK(dark.chrome.dark_titlebar);
    CHECK(light.chrome.corner_preference == CornerPreference::Default);
}

TEST_CASE("Windows9x asks DWM for square corners and its own classic navy caption", "[theme][chrome][retro]")
{
    const Theme win9x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    CHECK(win9x.chrome.corner_preference == CornerPreference::DoNotRound);
    REQUIRE(win9x.chrome.caption_color.has_value());
    CHECK(*win9x.chrome.caption_color == Color::from_argb(0xFF000080));
    CHECK_FALSE(win9x.chrome.dark_titlebar);
}

TEST_CASE("Luna/Aero/MDL/MDL2 all ask DWM for square corners, matching their real pre-Win11 desktop windows",
          "[theme][chrome][retro]")
{
    for (ThemeVariant v : {ThemeVariant::Luna, ThemeVariant::Aero, ThemeVariant::Metro, ThemeVariant::Mdl2})
    {
        const Theme theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        CHECK(theme.chrome.corner_preference == CornerPreference::DoNotRound);
    }
}

TEST_CASE("Fluent asks DWM for square corners; Fluent2 keeps the rounded default", "[theme][chrome]")
{
    const Theme fluent1 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{});
    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});
    CHECK(fluent1.chrome.corner_preference == CornerPreference::DoNotRound);
    CHECK(fluent2.chrome.corner_preference == CornerPreference::Default);
}

// ---- The extended era timeline ----------------------------------------------------------

TEST_CASE("Era-specific animation timings: 9x/MDL2 are instant, Aero cross-fades over 300 ms", "[theme][retro]")
{
    using namespace std::chrono_literals;
    const Theme win9x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    const Theme mdl2  = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});
    const Theme aero  = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    CHECK(win9x.anim_faster == 0ms);
    CHECK(mdl2.anim_faster == 0ms);
    // No era source states `TMT_TRANSITIONDURATIONS`, and the 225/150 ms a live probe returns is
    // Windows 11 data. A 0.3 s hover-in is the best-sourced value.
    CHECK(aero.anim_faster == 300ms);
}

// ---- Windows 3.x ------------------------------------------------------------------------
// Values come from the 3.1 Resource Kit's `VGA.DR_` bitmaps and the shipped `VGASYS.FO_` /
// `SSERIFE.FO_` fonts.

TEST_CASE("Windows3x draws the era's own asymmetric edge, not the 9x bevel with stops removed", "[theme][retro][win3x]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{});

    // The four colors `OBM_UPARROW` is painted in, and the ButtonFace/ButtonHilight/ButtonText
    // of all 22 shipped color schemes.
    CHECK(theme.button_normal.border_style == BorderStyle::Raised);
    CHECK(theme.button_normal.background == Color::from_argb(0xFFC0C0C0));
    CHECK(theme.button_normal.bevel_highlight == colors::White);
    CHECK(theme.button_normal.bevel_shadow == Color::from_argb(0xFF808080));
    // The 1px black frame outside the highlight has no slot in the 9x ramp, so this era routes to
    // `draw_win3x_border`, not `draw_bevel_border`.
    REQUIRE(theme.button_normal.bevel_frame.has_value());
    CHECK(*theme.button_normal.bevel_frame == colors::Black);

    // Pressed: dark top/left only, no bottom/right edge, content offset +1,+1.
    CHECK(theme.button_pressed.border_style == BorderStyle::Sunken);
    REQUIRE(theme.button_pressed.bevel_frame.has_value());
    CHECK(ui::detail::controls(theme).pressed_content_offset == Point{1.0f, 1.0f});

    // Rounded push-button corners: `OBM_BTNCORNERS`' 6x6 ring cuts one pixel off each corner. The
    // only rounded thing in the era.
    CHECK(theme.corner_radius == 0.0f);
    CHECK(theme.resolved_button_corner_radius() == 1.0f);

    // No hover state anywhere in the era; no animation; no gradient; square window corners.
    using namespace std::chrono_literals;
    CHECK(theme.button_hover == theme.button_normal);
    CHECK(theme.anim_fast == 0ms);
    CHECK(theme.anim_faster == 0ms);
    CHECK_FALSE(theme.button_normal.background_gradient.has_value());
    CHECK(theme.chrome.corner_preference == CornerPreference::DoNotRound);

    // Aliased text and geometry, snapped to device pixels: 1bpp art on a 16-color display.
    CHECK(theme.rasterization.geometry == GeometryAA::Aliased);
    CHECK(theme.rasterization.text == TextAA::Aliased);
    CHECK(theme.rasterization.snap_to_device_pixels);
}

// Render-level, because a token-level check does not notice a control taking a different code
// path; `bevel_frame` is what routes `draw_control_chrome` at the era's edge.
TEST_CASE("a Windows3x Button renders the era's edge, and a Windows9x one does not", "[theme][retro][win3x]")
{
    const Theme win3x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{});
    const Theme win9x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});

    auto stream = [](const Theme& theme)
    {
        ButtonControl button{"OK"};
        button.arrange(Rect{0.0f, 0.0f, 75.0f, 23.0f}); // the era's own canonical button size
        TestRenderer renderer;
        button.render(renderer, theme);
        return renderer;
    };

    const TestRenderer three = stream(win3x);
    // Fill, the 1px frame, then 2px shadow right and bottom and 1px highlight top and left. The 9x
    // path emits eight edge lines and no frame.
    CHECK(three.count_of<StrokeRoundedRect>() == 1);
    CHECK(three.count_of<StrokeLine>() == 4);

    const TestRenderer nine = stream(win9x);
    CHECK(nine.count_of<StrokeRoundedRect>() == 0);
    CHECK(nine.count_of<StrokeLine>() == 8);

    // One white pixel of highlight against two gray ones of shadow.
    float highlight_width = 0.0f, shadow_width = 0.0f;
    for (const auto& cmd : three.commands)
    {
        if (!std::holds_alternative<StrokeLine>(cmd))
        {
            continue;
        }
        const auto& line                                               = std::get<StrokeLine>(cmd);
        (line.color == colors::White ? highlight_width : shadow_width) = line.stroke_width;
    }
    CHECK(highlight_width == 1.0f);
    CHECK(shadow_width == 2.0f);
}

// Render-level: each bug here was a control reading the wrong thing out of a theme that looked
// correct token by token.
TEST_CASE("a Windows3x ToggleSwitch is a classic well, not a Fluent pill", "[theme][retro][win3x]")
{
    auto stream = [](const Theme& theme, bool on)
    {
        ToggleSwitchControl toggle{on};
        toggle.arrange(Rect{0.0f, 0.0f, 154.0f, 20.0f});
        TestRenderer renderer;
        toggle.render(renderer, theme);
        return renderer;
    };

    const Theme win3x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{});
    CHECK(ui::detail::controls(win3x).toggle_switch_shape == ToggleSwitchShape::ClassicWell);

    // `ToggleSwitch` must not infer "retro" from the check box's `border_style`: flattening the 3.x
    // check box once dropped the switch into the Fluent capsule branch.
    const TestRenderer three = stream(win3x, /*on=*/false);
    for (const auto& cmd : three.commands)
    {
        if (std::holds_alternative<FillRoundedRect>(cmd))
        {
            CHECK(std::get<FillRoundedRect>(cmd).radius == 0.0f);
        }
    }
    // And it draws the era's own edge, frame included, not the 9x ramp.
    CHECK(three.count_of<StrokeRoundedRect>() == 1);
    CHECK(three.count_of<StrokeLine>() == 4);

    // Sunken when on: the era's latched-button cue, a single gray top/left edge inside the frame.
    const TestRenderer on = stream(win3x, /*on=*/true);
    CHECK(on.count_of<StrokeLine>() == 2);

    // Windows 9x takes the same shape, and its indicator is still bevelled.
    const Theme win9x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    CHECK(ui::detail::controls(win9x).toggle_switch_shape == ToggleSwitchShape::ClassicWell);
    const TestRenderer nine = stream(win9x, /*on=*/false);
    CHECK(nine.count_of<StrokeRoundedRect>() == 0); // no frame ring in this era
    CHECK(nine.count_of<StrokeLine>() == 8);        // the full four-ring DrawEdge shape

    // Fluent still gets its pill, so the token changed nothing outside the classic packs.
    const Theme fluent = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});
    CHECK(ui::detail::controls(fluent).toggle_switch_shape == ToggleSwitchShape::FluentPill);
    CHECK(stream(fluent, /*on=*/false).count_of<StrokeLine>() == 0);
}

TEST_CASE("Windows3x draws no accent selection indicator", "[theme][retro][win3x]")
{
    // The era has no control accent, so the NavigationShell bar would draw in the pale button face.
    // Selection is already carried by `COLOR_HIGHLIGHT`.
    const Theme win3x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{});
    CHECK_FALSE(ui::detail::controls(win3x).accent_selection_indicator);
    CHECK_FALSE(ui::detail::controls(resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{}))
                    .accent_selection_indicator);
}

TEST_CASE("Windows3x has two UI fonts, and the chrome one is bold", "[theme][retro][win3x]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{});

    // `SSERIFE.FON`: MS Sans Serif 8pt, 13px cell, weight 400. The era's dialog templates name it
    // as "Helv", which `[FontSubstitutes]` maps to MS Sans Serif. DirectWrite cannot load the
    // bitmap face, so its TrueType successor stands in at 8 pt on the strike's 13 px line.
    CHECK(theme.text_style.font_family == "Microsoft Sans Serif");
    CHECK(theme.text_style.font_size == Catch::Approx(32.0f / 3.0f));
    CHECK(theme.text_style.line_height == 13.0f);
    CHECK(theme.text_style.weight == FontWeight::Regular);

    // `VGASYS.FON`: System 10pt, 16px cell, 13px ascent, `dfWeight` 700. Menus and title bars read
    // bold because the stock face is bold. Another bitmap face, set in the same stand-in, bold, at
    // the size whose advances match it.
    REQUIRE(theme.chrome_text_style.has_value());
    CHECK(theme.menu_text_style().font_family == "Microsoft Sans Serif");
    CHECK(theme.menu_text_style().weight == FontWeight::Bold);
    CHECK(theme.menu_text_style().line_height == 16.0f);
    CHECK(theme.menu_text_style().font_size > theme.text_style.font_size);
}

TEST_CASE("Windows3x checks with an X and its check box carries no bevel", "[theme][retro][win3x]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{});

    // `OBM_CHECKBOXES` is a 1 bpp grid of flat 13x13 outline glyphs. A monochrome glyph gets no
    // ramp, which is why a 3.1 check box has no bevel and a 95 one does.
    for (const ToggleVisuals* tv : {&ui::detail::controls(theme).check_box_box, &ui::detail::controls(theme).radio_ring})
    {
        CHECK(tv->normal.off.border_style == BorderStyle::Flat);
        CHECK(tv->normal.on.border_style == BorderStyle::Flat);
        CHECK(tv->normal.off.background == colors::White); // COLOR_WINDOW
        CHECK(tv->normal.off.border == colors::Black);     // COLOR_WINDOWFRAME
        CHECK(tv->normal.on.text == colors::Black);        // the X and the disc are 1bpp ink alike
        CHECK_FALSE(tv->normal.on.bevel_frame.has_value());
    }
    // The ToggleSwitch is the one that needs a real edge, and it has its own.
    CHECK(ui::detail::controls(theme).toggle_track.normal.off.bevel_frame == colors::Black);
    CHECK(ui::detail::controls(theme).toggle_box_size == 13.0f);
    // The X, not the tick. The tick is `OBM_CHECK`, the menu mark; Windows 95 merged the two.
    CHECK(ui::detail::controls(theme).check_glyph.codepoint == glyphs::Cancel.codepoint);
    CHECK(ui::detail::controls(theme).check_glyph.codepoint != glyphs::Accept.codepoint);

    // A disabled label is `COLOR_GRAYTEXT` only. The engraved look (`OBM_UPARROWI`) is for color
    // chrome glyphs, and there is no `DrawState` in this era. Windows 9x keeps it.
    CHECK(theme.button_disabled.text_effect.kind == TextEffectKind::None);
    CHECK(theme.button_disabled.text == Color::from_argb(0xFF808080));

    const Theme win9x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    CHECK(win9x.button_disabled.text_effect.kind == TextEffectKind::Emboss);
    CHECK(ui::detail::controls(win9x).check_glyph.codepoint == glyphs::Accept.codepoint);
    CHECK(ui::detail::controls(win9x).check_box_box.normal.off.border_style == BorderStyle::Sunken);
    CHECK_FALSE(win9x.button_normal.bevel_frame.has_value());
}

// ---- Early Windows: 1.01 through 2.11 ---------------------------------------------------

TEST_CASE("Windows1x has no elevation model at all - no bevel, no face, no ramp", "[theme][retro][winold]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x}, OsThemeSnapshot{});

    // No bevel anywhere: every `ButtonVisual` is `BorderStyle::Flat`. The era ships no button
    // artwork, and every `OBM_*` chrome bitmap in the 1.01 drivers is 1 bpp.
    for (const ButtonVisual* v :
         {&theme.button_normal,
          &theme.button_hover,
          &theme.button_pressed,
          &theme.button_disabled,
          &ui::detail::controls(theme).check_box_box.normal.off,
          &ui::detail::controls(theme).check_box_box.normal.on,
          &ui::detail::controls(theme).check_box_box.disabled.off,
          &ui::detail::controls(theme).radio_ring.normal.on,
          &ui::detail::controls(theme).toggle_track.normal.on})
    {
        CHECK(v->border_style == BorderStyle::Flat);
        CHECK_FALSE(v->bevel_frame.has_value()); // Windows 3.x's frame ring; this era has no ring at all
        CHECK_FALSE(v->background_gradient.has_value());
        CHECK_FALSE(v->border_gradient.has_value());
        CHECK_FALSE(v->inner_border.has_value());
        CHECK_FALSE(v->inner_shadow.has_value());
        CHECK(v->text_effect.kind == TextEffectKind::None);
    }

    // Frames are one pixel of `COLOR_WINDOWFRAME`.
    CHECK(theme.border_width == 1.0f);
    CHECK(theme.button_normal.border == colors::Black);
    CHECK(theme.button_normal.draw_border);

    // Fills are flat in every state: no gradient, no dither, no alpha.
    CHECK(theme.button_normal.background.a == 1.0f);
    CHECK(theme.button_pressed.background.a == 1.0f);

    // No `COLOR_BTNFACE` and no silver page, so a dialog is `COLOR_WINDOW` like everything else.
    CHECK(theme.colors().background == colors::White);
    const Theme win3x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{});
    CHECK(win3x.colors().background == Color::from_argb(0xFFC0C0C0));
    CHECK(theme.colors().background != win3x.colors().background);
}

TEST_CASE("Windows1x selects by reverse video, because there is no highlight color", "[theme][retro][winold]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x}, OsThemeSnapshot{});

    // `COLOR_HIGHLIGHT` arrives with Windows 3.0, so selection swaps foreground and background.
    CHECK(theme.colors().selection == theme.colors().field_text);
    CHECK(theme.colors().selection_text == theme.colors().field);
    CHECK(theme.surfaces.menu_highlight == theme.surfaces.menu_text);
    CHECK(theme.surfaces.menu_highlight_text == theme.surfaces.menu);

    // Windows 9x has a real `COLOR_HIGHLIGHT` navy, neither of the two colors around it.
    const Theme win9x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    CHECK(win9x.colors().selection == Color::from_argb(0xFF000080));
    CHECK(win9x.colors().selection != win9x.colors().field_text);

    // No `COLOR_GRAYTEXT` either. The 50 % ink-on-paper checkerboard cannot be pattern-filled, so
    // it averages to `#808080`, the gray the 1.01 table uses for `COLOR_BACKGROUND`.
    CHECK(theme.colors().text_disabled == lerp(colors::Black, colors::White, 0.5f));
    CHECK(theme.colors().text_disabled.r == 0.5f);
    CHECK(theme.colors().text_disabled != theme.colors().text);
    CHECK(theme.colors().text_disabled != theme.colors().background);
    // Gray the label, not the button: the frame and its corners are untouched.
    CHECK(theme.button_disabled.text == theme.colors().text_disabled);
    CHECK(theme.button_disabled.border == theme.button_normal.border);
    CHECK(theme.button_disabled.background == theme.button_normal.background);
}

TEST_CASE("Windows1x asks for a monospaced face at one size, with no chrome font and no ramp", "[theme][retro][winold]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x}, OsThemeSnapshot{});

    // `HIFONTS.FON` resource 28: `System`, 8x12 cell, ascent 10, external leading 2, `dfWeight`
    // 400, fixed pitch (`dfAvgWidth == dfMaxWidth == 8`).
    CHECK(theme.text_style.font_size == 14.0f);
    CHECK(theme.text_style.line_height == 14.0f);
    // Regular, not bold: Windows 3.x's `System` is weight 700, this one is 400.
    CHECK(theme.text_style.weight == FontWeight::Regular);
    CHECK(resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{}).menu_text_style().weight ==
          FontWeight::Bold);

    // One UI face. The chrome/dialog split is a Windows 3.0 arrival.
    CHECK_FALSE(theme.chrome_text_style.has_value());
    CHECK(theme.menu_text_style().font_family == theme.text_style.font_family);

    // No type ramp: the style guide states no rule on type, and a display ships one face.
    for (const TextStyle* s :
         {&theme.type.header,
          &theme.type.subheader,
          &theme.type.title,
          &theme.type.subtitle,
          &theme.type.body,
          &theme.type.caption})
    {
        CHECK(s->font_size == theme.text_style.font_size);
        CHECK(s->weight == theme.text_style.weight);
        CHECK(s->font_family == theme.text_style.font_family);
    }

    // Aliased geometry and text: every UI face in the era is a 1-bit glyph strip.
    CHECK(theme.rasterization.geometry == GeometryAA::Aliased);
    CHECK(theme.rasterization.text == TextAA::Aliased);
    CHECK(theme.rasterization.snap_to_device_pixels);
}

TEST_CASE("A Windows1x push button is a rounded outline, and an X checks its check box", "[theme][retro][winold]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x}, OsThemeSnapshot{});

    // Push buttons are rounded outlines on the window's own background. `OBM_BTNCORNERS` glyph A
    // is a 6x6 hollow ring with one pixel cut off each corner, unchanged up to Windows 3.0.
    CHECK(theme.button_corner_radius == 1.0f);
    CHECK(theme.resolved_button_corner_radius() == 1.0f);
    CHECK(theme.corner_radius == 0.0f); // and it is the era's *only* rounded surface
    CHECK(ui::detail::controls(theme).toggle_box_radius == 0.0f);
    CHECK(theme.button_normal.background == theme.colors().background); // no face of its own

    // The checked check box is an X, corner to corner, in the 1.01 artwork. The tick is
    // `OBM_CHECK`, the menu mark.
    CHECK(ui::detail::controls(theme).check_glyph.codepoint == glyphs::Cancel.codepoint);
    // `OBM_CHECKBOXES` cells are 14 x 14 at EGA high resolution; the radio disc is about 8 px in
    // a 14 px ring. Windows 95's 13x13 is a later re-cut.
    CHECK(ui::detail::controls(theme).toggle_box_size == 14.0f);
    CHECK(ui::detail::controls(theme).radio_dot.rest == 8.0f);
    CHECK(ui::detail::controls(theme).radio_dot.hover == ui::detail::controls(theme).radio_dot.rest);
    CHECK(ui::detail::controls(theme).radio_dot.pressed == ui::detail::controls(theme).radio_dot.rest);

    // A push button is 14 DU tall, and Ok/Cancel is 32 x 14 DU: 64 x 21 px
    // at the 8x12 font's dialog unit.
    CHECK(theme.padding == Edges{.left = 8.0f, .top = 3.0f, .right = 8.0f, .bottom = 4.0f});
}

TEST_CASE("Windows1x has no hover state and nothing animates", "[theme][retro][winold]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x}, OsThemeSnapshot{});

    // No hover: the era's books never mention one, `BN_*` has no pointer-enter code, and with
    // `InvertRect` as the only state operation it would look like pressed.
    CHECK(theme.button_hover == theme.button_normal);
    CHECK(theme.accent_button_hover == theme.accent_button_normal);
    CHECK(theme.subtle_button_hover == theme.subtle_button_normal);
    for (const ToggleVisuals* tv : {&ui::detail::controls(theme).check_box_box,
                                    &ui::detail::controls(theme).radio_ring,
                                    &ui::detail::controls(theme).toggle_track})
    {
        CHECK(tv->hover == tv->normal);
        CHECK(tv->pressed == tv->normal);
    }

    // No accent color, so `ButtonVariant::Accent` is `BS_DEFPUSHBUTTON`: `OBM_BTNCORNERS` glyph B,
    // an 8x8 ring at 2px stroke. Same fill, same color, twice the stroke.
    CHECK(theme.accent_button_normal.background == theme.button_normal.background);
    CHECK(theme.accent_button_normal.border == theme.button_normal.border);
    CHECK(theme.accent_button_normal.text == theme.button_normal.text);
    CHECK(theme.accent_button_normal.resolved_border_width(theme.border_width) == 2.0f);
    CHECK(theme.button_normal.resolved_border_width(theme.border_width) == 1.0f);
    // No toolbar to be subtle against, so the subtle family reuses the plain button.
    CHECK(theme.subtle_button_normal == theme.button_normal);
    CHECK_FALSE(ui::detail::controls(theme).accent_selection_indicator);

    CHECK(theme.anim_faster == std::chrono::milliseconds{0});
    CHECK(theme.anim_fast == std::chrono::milliseconds{0});
    CHECK(theme.anim_normal == std::chrono::milliseconds{0});

    // A pressed push button is inverted, not pushed in, so the label does not move.
    CHECK(ui::detail::controls(theme).press_state_flip);
    CHECK(ui::detail::controls(theme).pressed_content_offset == Point{0.0f, 0.0f});
    CHECK(theme.button_pressed.background == theme.button_normal.text);
    CHECK(theme.button_pressed.text == theme.button_normal.background);
    const Theme win3x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows3x}, OsThemeSnapshot{});
    CHECK(ui::detail::controls(win3x).pressed_content_offset == Point{1.0f, 1.0f});

    // No alpha and no blend mode in the era, so nothing casts a shadow or dims.
    CHECK(theme.popup_shadow == ThemePopupShadow::None);
    CHECK(theme.colors().scrim == colors::Transparent);
}

TEST_CASE("Windows1x resolves the 2.03 color row by default, and the other three on request", "[theme][retro][winold]")
{
    // The default palette is the 2.03 display drivers' own, identical across every color
    // adapter.
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x}, OsThemeSnapshot{});
    CHECK(theme.surfaces.menu == Color::from_argb(0xFF00FFFF)); // COLOR_MENU, the cyan menu bar
    CHECK(theme.chrome.caption_color == Color::from_argb(0xFF0000FF));
    CHECK(theme.surfaces.scrollbar_track == Color::from_argb(0xFF3F3F3F));
    CHECK(theme.chrome.border_color == Color::from_argb(0xFF7F7F7F)); // COLOR_ACTIVEBORDER, new in 2.0

    // The four tables are not variants of one look, so `early_scheme` swaps the whole row.
    const Theme mono = resolve_theme(ThemeRequest{.variant      = ThemeVariant::Windows1x,
                                                  .early_scheme = EarlyPalette::windows_1x_mono()},
                                     OsThemeSnapshot{});
    CHECK(mono.surfaces.menu == colors::White);
    CHECK(mono.chrome.caption_color == colors::Black);
    CHECK(mono.surfaces.scrollbar_track == Color::from_argb(0xFFC0C0C0));
    // Ten slots, not thirteen: `COLOR_ACTIVEBORDER` and its two siblings do not exist in
    // Windows 1.x, so the frame color is the only border color there is.
    CHECK_FALSE(EarlyPalette::windows_1x_mono().active_border.has_value());
    CHECK(mono.chrome.border_color == colors::Black);

    // The three slots that moved between 1.x and 2.x, and the ones a viewer notices.
    const EarlyPalette one = EarlyPalette::windows_1x_mono();
    const EarlyPalette two = EarlyPalette::windows_2x_mono();
    CHECK(one.scrollbar != two.scrollbar);               // #C0C0C0 -> #3F3F3F
    CHECK(one.inactive_caption != two.inactive_caption); // #808080 -> #FFFFFF
    CHECK(one.background != two.background);             // #808080 -> #7F7F7F
    CHECK(one.window == two.window);
    CHECK(one.window_frame == two.window_frame);
    CHECK(one.menu_text == two.menu_text);

    // The 1.01 color row, also the alpha/beta 1 colors the 2.03 drivers kept.
    const EarlyPalette ega = EarlyPalette::windows_1x_color();
    CHECK(ega.scrollbar == Color::from_argb(0xFFFF0000));
    CHECK(ega.background == Color::from_argb(0xFF00FF40));
    CHECK(ega.menu == Color::from_argb(0xFFFFFF00));
    CHECK(ega.inactive_caption == Color::from_argb(0xFF4040C8));
    CHECK(ega.window == colors::White);

    // `early_scheme` is variant-scoped, like `classic_scheme`: asking for it under another
    // variant changes nothing.
    const Theme win9x = resolve_theme(ThemeRequest{.variant      = ThemeVariant::Windows9x,
                                                   .early_scheme = EarlyPalette::windows_1x_color()},
                                      OsThemeSnapshot{});
    CHECK(win9x.colors().background == Color::from_argb(0xFFC0C0C0));
}

// Render-level: `BorderStyle::Flat` + `draw_border` is what routes `draw_control_chrome` past
// both bevel primitives, and a token check would not see a wrong code path.
TEST_CASE("a Windows1x Button renders one outline and no edge lines at all", "[theme][retro][winold]")
{
    auto stream = [](ThemeVariant v)
    {
        ButtonControl button{"Ok"};                     // `Ok`, not `OK`: the 1.0x spelling
        button.arrange(Rect{0.0f, 0.0f, 64.0f, 21.0f}); // 32 x 14 DU, the canonical Ok/Cancel size
        TestRenderer renderer;
        button.render(renderer, resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{}));
        return renderer;
    };

    const TestRenderer old = stream(ThemeVariant::Windows1x);
    // A fill and exactly one stroked outline, zero `StrokeLine`s. Windows 3.x emits four edge
    // lines and 9x eight.
    CHECK(old.count_of<StrokeLine>() == 0);
    CHECK(old.count_of<StrokeRoundedRect>() == 1);
    CHECK(stream(ThemeVariant::Windows3x).count_of<StrokeLine>() == 4);
    CHECK(stream(ThemeVariant::Windows9x).count_of<StrokeLine>() == 8);

    // One pixel of `COLOR_WINDOWFRAME` over the page's own white, stroked inside the bounds: the
    // path is pulled in half a stroke, so the radius is the 1px cut minus that half.
    const Rect bounds{0.0f, 0.0f, 64.0f, 21.0f};
    for (const auto& cmd : old.commands)
    {
        if (std::holds_alternative<StrokeRoundedRect>(cmd))
        {
            const auto& stroke = std::get<StrokeRoundedRect>(cmd);
            CHECK(stroke.color == colors::Black);
            CHECK(stroke.stroke_width == 1.0f);
            CHECK(stroke.radius == 0.5f); // 1px `OBM_BTNCORNERS` cut, less the half-stroke inset
            const float half = stroke.stroke_width * 0.5f;
            CHECK(stroke.bounds.left() - half >= bounds.left());
            CHECK(stroke.bounds.top() - half >= bounds.top());
            CHECK(stroke.bounds.right() + half <= bounds.right());
            CHECK(stroke.bounds.bottom() + half <= bounds.bottom());
        }
        if (std::holds_alternative<FillRoundedRect>(cmd))
        {
            CHECK(std::get<FillRoundedRect>(cmd).color == colors::White);
        }
    }
}

TEST_CASE("Windows1x gives its era-less controls era parts rather than Fluent defaults", "[theme][retro][winold]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x}, OsThemeSnapshot{});

    // Slider, ProgressBar, ToggleSwitch and the rest postdate the era and are built from its own
    // parts. The Slider borrows the scroll bar's `OBM_BTSIZE` thumb, 15 x 14.
    CHECK(ui::detail::controls(theme).slider_thumb.shape == SliderThumbShape::RoundedRect);
    CHECK(ui::detail::controls(theme).slider_thumb.radius == 0.0f);
    CHECK(ui::detail::controls(theme).slider_thumb.outer == 14.0f);
    CHECK(ui::detail::controls(theme).slider_thumb.rest == 15.0f);
    CHECK(ui::detail::controls(theme).slider_thumb.hover == ui::detail::controls(theme).slider_thumb.rest);
    CHECK(ui::detail::controls(theme).slider_thumb.pressed == ui::detail::controls(theme).slider_thumb.rest);
    CHECK_FALSE(ui::detail::controls(theme).slider_value_fill);
    // A one-pixel groove: `SliderControl::render()` rounds the track at half its height, so anything
    // thicker is a capsule.
    CHECK(ui::detail::controls(theme).slider_track_thickness == 1.0f);
    REQUIRE(ui::detail::controls(theme).slider_fills.has_value());
    CHECK(ui::detail::controls(theme).slider_fills->track == colors::Black); // COLOR_WINDOWFRAME, like every other rule
    CHECK(ui::detail::controls(theme).slider_fills->track_hover == ui::detail::controls(theme).slider_fills->track);
    // The thumb is drawn solid and unstroked by this path, so it cannot be the page's own white.
    CHECK(ui::detail::controls(theme).slider_fills->thumb == theme.colors().text);
    CHECK(ui::detail::controls(theme).slider_fills->thumb != theme.colors().background);

    // ToggleSwitch takes the square latched well, as `Windows3x` does. It reads the `bevel_*`
    // fields directly, so left at defaults it would draw a Windows 95 bevel here.
    CHECK(ui::detail::controls(theme).toggle_switch_shape == ToggleSwitchShape::ClassicWell);
    for (bool on : {false, true})
    {
        CAPTURE(on);
        ToggleSwitchControl sw{on};
        sw.arrange(Rect{0.0f, 0.0f, 154.0f, 20.0f});
        TestRenderer renderer;
        sw.render(renderer, theme);

        // One ring of one color: `draw_bevel_border` emits two lines per ring per axis pair, and
        // every one here is `COLOR_WINDOWFRAME`.
        CHECK(renderer.count_of<StrokeLine>() == 4);
        for (const auto& cmd : renderer.commands)
        {
            if (std::holds_alternative<StrokeLine>(cmd))
            {
                CHECK(std::get<StrokeLine>(cmd).color == colors::Black);
                CHECK(std::get<StrokeLine>(cmd).stroke_width == 1.0f);
            }
        }
    }
    // Windows 9x keeps its four-ring well, so the `bevel_rings` pass-through moved nothing else.
    {
        ToggleSwitchControl sw{false};
        sw.arrange(Rect{0.0f, 0.0f, 154.0f, 20.0f});
        TestRenderer renderer;
        sw.render(renderer, resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{}));
        CHECK(renderer.count_of<StrokeLine>() == 8);
    }

    // `COLOR_INFOBK` is Windows 95, so the tooltip falls back to the era's plain framed white box.
    CHECK(theme.surfaces.tooltip == theme.colors().background);
    REQUIRE(theme.surfaces.tooltip_border.has_value());
    CHECK(*theme.surfaces.tooltip_border == colors::Black);
    CHECK(ui::detail::tooltip_radius(theme) == 0.0f);
    CHECK_FALSE(ui::detail::controls(theme).tooltip_padding.has_value());

    // A plain one-pixel rule in `COLOR_WINDOWFRAME`: the era has no highlight/shadow pair to etch.
    CHECK(theme.divider_visual == ThemeDividerVisual::FlatStroke);
    CHECK(theme.surfaces.separator_light == theme.surfaces.separator_dark);
    CHECK(theme.colors().divider == colors::Black);
}

// ---- MDL2: Windows 10, 1507-1703 --------------------------------------------------------
// Values match Windows 10 builds 10240, 10586, 14393 and 15063.

TEST_CASE("Mdl2 is square, 2px-bordered and 8,4,8,4-padded - the era's own metrics", "[theme][retro][mdl2]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});

    // 10240 rounds one corner (the Slider thumb) and has no shared radius.
    CHECK(theme.corner_radius == 0.0f);
    CHECK(theme.overlay_corner_radius == 0.0f);
    // The Button, text field, CheckBox and RadioButton borders are 2 thick in all four builds. A 1px
    // border reads as Fluent-Win10, the next era.
    CHECK(theme.border_width == 2.0f);
    // The Button's padding in 10240 and 15063, not Fluent-Win10's 8,5,8,6.
    CHECK(theme.padding == Symmetric{.horizontal = 8.0f, .vertical = 4.0f});
    CHECK(theme.page.grid_unit == 4.0f);

    // No material, no elevation, no shadow, no gradient.
    CHECK_FALSE(theme.button_normal.background_gradient.has_value());
    CHECK_FALSE(theme.button_normal.border_gradient.has_value());
    CHECK_FALSE(theme.accent_button_normal.border_gradient.has_value());
    CHECK(theme.button_normal.border_style != BorderStyle::Raised);
    CHECK(theme.button_normal.border_style != BorderStyle::Sunken);
    CHECK_FALSE(ui::detail::controls(theme).card_surfaces);
    CHECK_FALSE(ui::detail::controls(theme).tab_layer_surfaces);
    CHECK_FALSE(ui::detail::controls(theme).list_item_subtle_selection);

    CHECK(theme.chrome.corner_preference == CornerPreference::DoNotRound);
}

TEST_CASE("Mdl2 draws the two-ring focus visual, not the classic dotted rect", "[theme][retro][mdl2]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});

    // The era's focus visual (2px outside, 1px inside, 1px margin) is what Fluent inherits, so this
    // is FluentRings, not a dotted rectangle.
    CHECK(theme.focus_visual != FocusVisual::ClassicDotted);
    CHECK(theme.focus_visual == FocusVisual::FluentRings);
    CHECK(theme.focus_ring_outer_width == 2.0f);
    CHECK(theme.focus_ring_inner_width == 1.0f);
    CHECK(theme.focus_ring_margin == 1.0f);
    CHECK(theme.focus_ring_style().kind == FocusVisual::FluentRings);
}

TEST_CASE("Mdl2's Base/Body type is 15px, not Fluent's 14", "[theme][retro][mdl2]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});

    // Body text is 15 from 1507 through 1803; 14 arrives with 1809.
    CHECK(theme.text_style.font_size == 15.0f);
    CHECK(theme.type.body.font_size == 15.0f);
    CHECK(theme.type.header.font_size == 46.0f);
    CHECK(theme.type.header.weight == FontWeight::Light);
    CHECK(theme.type.subheader.font_size == 34.0f);
    CHECK(theme.type.subheader.weight == FontWeight::Light);
    CHECK(theme.type.title.font_size == 24.0f);
    CHECK(theme.type.title.weight == FontWeight::SemiLight);
    CHECK(theme.type.subtitle.font_size == 20.0f);
    CHECK(theme.type.caption.font_size == 12.0f);
    CHECK(theme.text_style.font_family == "Segoe UI");
    // Segoe MDL2 Assets is the icon font in 10240.
    CHECK(std::string_view{theme.icon_font_family} == "Segoe MDL2 Assets");
}

TEST_CASE("Mdl2's Button is a gray alpha fill whose border only appears on hover", "[theme][retro][mdl2]")
{
    const Theme       theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});
    const Mdl2Palette p     = Mdl2Palette::mdl2_light();

    // 10240's Button states:
    //   * rest is text_20 over a transparent border;
    CHECK(theme.button_normal.background == p.text_20);
    CHECK(theme.button_normal.border == colors::Transparent);
    CHECK(theme.button_normal.text == p.text_100);
    //   * hover moves the border, not the fill;
    CHECK(theme.button_hover.background == theme.button_normal.background);
    CHECK(theme.button_hover.border == p.text_40);
    //   * press deepens the same gray, the accent never touches an ordinary button.
    CHECK(theme.button_pressed.background == p.text_40);
    CHECK(theme.button_pressed.border == colors::Transparent);

    // Secondary and disabled colors stay alpha rungs, never opaque grays baked against the page,
    // which would break over a selection or an image.
    CHECK(theme.button_normal.background.a < 1.0f);
    CHECK(theme.button_pressed.background.a < 1.0f);
    CHECK(theme.colors().text_disabled.a < 1.0f);
    CHECK(theme.surfaces.menu_highlight.a < 1.0f);

    // Subtle is flat until hover, and its two rungs are the List* pair a MenuItem uses.
    CHECK(theme.subtle_button_normal.background == colors::Transparent);
    CHECK(theme.subtle_button_hover.background == p.list_hover);
    CHECK(theme.subtle_button_pressed.background == p.list_pressed);
}

TEST_CASE("Mdl2's accent button presses to neutral gray, the era's own 1703 ladder", "[theme][retro][mdl2]")
{
    OsThemeSnapshot os;
    os.accent               = Color::from_argb(0xFF0078D7); // the era's shipped default accent
    const Theme       theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, os);
    const Mdl2Palette p     = Mdl2Palette::mdl2_light();

    // The accent button first appears in 1703. It has the neutral button's shape: hover
    // grows a border, press drops to text_40.
    CHECK(theme.accent_button_normal.background == os.accent);
    CHECK(theme.accent_button_hover.background == os.accent);
    CHECK(theme.accent_button_hover.border == p.text_40);
    CHECK(theme.accent_button_pressed.background == p.text_40);
    CHECK(theme.accent_button_pressed.background != os.accent);
}

TEST_CASE("Mdl2 reads the live OS accent - the first era whose accent reaches controls", "[theme][retro][mdl2]")
{
    // MDL2's accent is the user's choice from 48 swatches, so reading it
    // live is faithful. Aero and Metro had no such thing.
    const Color     pink = Color::from_argb(0xFFE3008C); // swatch row 5, column 2
    OsThemeSnapshot os;
    os.accent         = pink;
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, os);

    CHECK(theme.colors().selection == pink);
    CHECK(theme.colors().link == pink); // links are the accent
    CHECK(theme.accent_button_normal.background == pink);
    CHECK(ui::detail::controls(theme).check_box_box.normal.on.background == pink);
    CHECK(ui::detail::controls(theme).toggle_track.normal.on.background == pink);
    CHECK(ui::detail::controls(theme).radio_ring.normal.on.border == pink); // an accent *outline*: the checked ring never fills

    // An explicit app override still wins over the OS.
    const Color teal       = Color::from_argb(0xFF00B294);
    const Theme overridden = resolve_theme(ThemeRequest{.accent_override = teal, .variant = ThemeVariant::Mdl2}, os);
    CHECK(overridden.colors().selection == teal);
}

TEST_CASE("Mdl2 has a first-class dark theme, from the era's own two-column ramp", "[theme][retro][mdl2]")
{
    // 1507 ships a dark, a light and a high contrast palette, dark being the default.
    // A system-wide dark app mode came later, the palette pair did not.
    CHECK(theme_variant_supports_dark_mode(ThemeVariant::Mdl2));

    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Mdl2},
                                      OsThemeSnapshot{});
    const Theme dark  = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Mdl2},
                                      OsThemeSnapshot{});

    // The page background is a flat #FFFFFFFF / #FF000000.
    CHECK(light.colors().background == colors::White);
    CHECK(dark.colors().background == colors::Black);
    CHECK(light.colors().text == colors::Black);
    CHECK(dark.colors().text == colors::White);
    CHECK_FALSE(light.chrome.dark_titlebar);
    CHECK(dark.chrome.dark_titlebar);

    // Alt and Base are exact opposites, on the same six alpha steps that Fluent inherits.
    CHECK(Mdl2Palette::mdl2_light().text_20.a == Mdl2Palette::mdl2_dark().text_20.a);
    CHECK(light.button_normal.background.a == dark.button_normal.background.a);
    CHECK(light.button_normal.background != dark.button_normal.background);

    // The structure is identical in both themes. Metro's differs in border thickness too.
    CHECK(light.border_width == dark.border_width);
    CHECK(light.padding == dark.padding);
    CHECK(ui::detail::controls(light).toggle_switch_track_size == ui::detail::controls(dark).toggle_switch_track_size);
}

TEST_CASE("Mdl2's surfaces come from the era's token ramp, not from Win32 system colors", "[theme][retro][mdl2]")
{
    const Theme       theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});
    const Mdl2Palette p     = Mdl2Palette::mdl2_light();

    // The tooltip background is chrome_flyout, a flyout gray, never the
    // classic `#FFFFE1`.
    CHECK(theme.surfaces.tooltip == p.chrome_flyout);
    CHECK(theme.surfaces.tooltip != Color::from_argb(0xFFFFFFE1));
    CHECK(theme.surfaces.tooltip_text == p.text_100);
    // A hovered menu item is a faint neutral wash with the foreground left alone.
    CHECK(theme.surfaces.menu == p.chrome_flyout);
    CHECK(theme.surfaces.menu_highlight == p.list_hover);
    CHECK(theme.surfaces.menu_highlight_text == p.text_100);
    // TextBox: page_100 inside a chrome_disabled_strong border.
    CHECK(theme.colors().field == p.page_100);
    CHECK(theme.surfaces.field_border == p.chrome_disabled_strong);
    CHECK(theme.colors().text_disabled == p.text_20);
    CHECK(theme.surfaces.separator_light == theme.surfaces.separator_dark); // a flat rule, not an etched groove
}

TEST_CASE("Mdl2 carries the era's two rounded parts and its flat per-control geometry", "[theme][retro][mdl2]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});

    // The switch track is 44x20 with a 10px corner radius. The 10px knob moves by 24
    // (44 - 20) and never resizes.
    CHECK(ui::detail::controls(theme).toggle_switch_track_size == Size{44.0f, 20.0f});
    CHECK(ui::detail::controls(theme).toggle_knob.rest == 10.0f);
    CHECK(ui::detail::controls(theme).toggle_knob.hover == ui::detail::controls(theme).toggle_knob.rest);
    CHECK(ui::detail::controls(theme).toggle_knob.travel == 24.0f);

    // The CheckBox box and the RadioButton ring are 20x20; the 10x10 dot
    // does not resize.
    CHECK(ui::detail::controls(theme).toggle_box_size == 20.0f);
    CHECK(ui::detail::controls(theme).radio_dot.rest == 10.0f);
    CHECK(ui::detail::controls(theme).radio_dot.hover == ui::detail::controls(theme).radio_dot.rest);
    CHECK(ui::detail::controls(theme).radio_dot.pressed == ui::detail::controls(theme).radio_dot.rest);

    // The slider track is 2 thick (Fluent's is 4); the thumb is 8 x 24 with a corner radius of 4.
    CHECK(ui::detail::controls(theme).slider_track_thickness == 2.0f);
    CHECK(ui::detail::controls(theme).slider_thumb.outer == 24.0f);
    CHECK(ui::detail::controls(theme).slider_thumb.rest == 8.0f);
}

TEST_CASE("Mdl2's control states change instantly, as the era's did", "[theme][retro][mdl2]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});

    // In 10240, Button, CheckBox, RadioButton, ToggleSwitch, Slider, ComboBox and
    // TextBox change state with no duration at all.
    CHECK(theme.anim_faster == std::chrono::milliseconds{0});
    CHECK(theme.anim_fast == std::chrono::milliseconds{0});
    // Slider.cpp reads anim_normal for the inner-circle grow, and MDL2's thumb does not resize.
    CHECK(theme.anim_normal == std::chrono::milliseconds{0});

    // The spline 0.1,0.9 0.2,1.0 covers 75 of 10240's keyframes, a much harder decelerate than
    // Fluent's. Inert while the durations are zero.
    CHECK(theme.anim_fast_out_slow_in == CubicBezier{0.1f, 0.9f, 0.2f, 1.0f});
    CHECK(theme.anim_fast_out_slow_in != easing::ControlFastOutSlowIn);

    const Theme fluent2 = resolve_theme(ThemeRequest{}, OsThemeSnapshot{});
    CHECK(fluent2.anim_fast_out_slow_in == easing::ControlFastOutSlowIn); // unchanged elsewhere
    CHECK(fluent2.anim_normal == std::chrono::milliseconds{250});
}

TEST_CASE("Mdl2's Slider thumb is the era's 8x24 rounded rectangle, not two circles", "[theme][retro][mdl2][slider]")
{
    const Theme       theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});
    const Mdl2Palette p     = Mdl2Palette::mdl2_light();

    // One flat accent Border, where Fluent draws a disc plus an animating inner circle. `outer` is
    // the 24 across the track, each state's diameter the 8 along it.
    CHECK(ui::detail::controls(theme).slider_thumb.shape == SliderThumbShape::RoundedRect);
    CHECK(ui::detail::controls(theme).slider_thumb.radius == 4.0f);
    CHECK(ui::detail::controls(theme).slider_thumb.outer == 24.0f);
    CHECK(ui::detail::controls(theme).slider_thumb.rest == 8.0f);
    // No hover or press resize: the thumb only changes fill on Pressed.
    CHECK(ui::detail::controls(theme).slider_thumb.hover == ui::detail::controls(theme).slider_thumb.rest);
    CHECK(ui::detail::controls(theme).slider_thumb.pressed == ui::detail::controls(theme).slider_thumb.rest);
    // Independent of the theme-global radius, which is 0.
    CHECK(theme.corner_radius == 0.0f);
    CHECK(ui::detail::controls(theme).slider_thumb.radius != theme.corner_radius);

    // Fluent 1 inherited this color behavior and only adds a thumb hover rung.
    REQUIRE(ui::detail::controls(theme).slider_fills.has_value());
    CHECK(ui::detail::controls(theme).slider_fills->track == p.text_40);               // text_40
    CHECK(ui::detail::controls(theme).slider_fills->track_hover == p.text_60);         // text_60
    CHECK(ui::detail::controls(theme).slider_fills->thumb_pressed == p.chrome_strong); // chrome_strong, a neutral gray
    CHECK(ui::detail::controls(theme).slider_fills->thumb_pressed != ui::detail::controls(theme).slider_fills->thumb);
    CHECK(ui::detail::controls(theme).slider_fills->thumb_hover ==
          ui::detail::controls(theme).slider_fills->thumb); // there is no hover state

    CHECK(ui::detail::controls(theme).slider_track_thickness == 2.0f); // the track thickness; Fluent's is 4

    // Metro's thumb is as wide and tall as its 11px track: 11x11 at
    // radius 0, where MDL2's is 8x24 at radius 4.
    const Theme metro = resolve_theme(ThemeRequest{.variant = ThemeVariant::Metro}, OsThemeSnapshot{});
    CHECK(ui::detail::controls(metro).slider_thumb.shape == SliderThumbShape::RoundedRect);
    CHECK(ui::detail::controls(metro).slider_thumb.radius == 0.0f);
    CHECK(ui::detail::controls(metro).slider_thumb.outer == 11.0f);
    CHECK(ui::detail::controls(metro).slider_thumb.rest == 11.0f);
    CHECK(ui::detail::controls(metro).slider_track_thickness == 11.0f);

    // The Fluent eras keep the concentric circles. Luna, Aero and the bevel eras each have their
    // own thumb (see tests/test_slider.cpp).
    for (ThemeVariant v : {ThemeVariant::Fluent2, ThemeVariant::Fluent})
    {
        const Theme other = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        CHECK(ui::detail::controls(other).slider_thumb.shape == SliderThumbShape::ConcentricCircles);
    }
}

TEST_CASE("No classic popup is rounded either", "[theme][retro]")
{
    // Windows 9x has square corners everywhere, popups included. Every popup rounds at
    // `overlay_corner_radius`, so it must not stay at Fluent's 8.
    for (ThemeVariant v : {ThemeVariant::Windows9x, ThemeVariant::Windows3x, ThemeVariant::Windows1x})
    {
        const Theme theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        CHECK(theme.corner_radius == 0.0f);
        CHECK(theme.overlay_corner_radius == 0.0f);
        CHECK(ui::detail::tooltip_radius(theme) == 0.0f);
    }

    // Fluent is untouched: its overlay corner radius really is 8.
    CHECK(resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{}).overlay_corner_radius == 8.0f);
}

TEST_CASE("A classic Slider is a TRACKBAR_CLASS, pointed thumb and all", "[theme][retro]")
{
    // Windows 95's comctl32 trackbar: a pointed thumb on a thin sunken channel, with ticks. 3.1
    // had none, so its pack reuses the same shape with its own bevel.
    for (ThemeVariant v : {ThemeVariant::Windows9x, ThemeVariant::Windows3x})
    {
        const Theme theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        CHECK(ui::detail::controls(theme).slider_thumb.shape == SliderThumbShape::ClassicPointer);
        // `uThumbLen` across the track, `(uThumbLen / 2) | 1` along it, the same in all states.
        CHECK(ui::detail::controls(theme).slider_thumb.outer == 21.0f);
        CHECK(ui::detail::controls(theme).slider_thumb.rest == 11.0f);
        CHECK(ui::detail::controls(theme).slider_thumb.hover == 11.0f);
        CHECK(ui::detail::controls(theme).slider_thumb.pressed == 11.0f);
        // A four-pixel channel is two bevel rings and no interior at all.
        CHECK(ui::detail::controls(theme).slider_track_thickness == 4.0f);
        // `offsetedge` = `uThumbLen / 4 + 3`, and the channel rides one pixel above the thumb's
        // center (`uThumbLen / 2 - 1`).
        CHECK(ui::detail::controls(theme).classic_trackbar.channel_inset == 8.0f);
        CHECK(ui::detail::controls(theme).classic_trackbar.channel_offset == 9.0f);
        CHECK(ui::detail::controls(theme).classic_trackbar.thumb_margin == 2.0f);
        // `TIC_EDGE` is the whole of the difference between an end tick and an interior one.
        CHECK(ui::detail::controls(theme).classic_trackbar.tick_length == 3.0f);
        CHECK(ui::detail::controls(theme).classic_trackbar.tick_end_length ==
              ui::detail::controls(theme).classic_trackbar.tick_length + 1.0f);
        // No classic era fills the track behind the thumb.
        CHECK_FALSE(ui::detail::controls(theme).slider_value_fill);
    }
}

TEST_CASE("Metro's ToggleSwitch is a square 50x19 track, not a pill", "[theme][metro]")
{
    // Metro rounds nothing, so `ToggleSwitch.cpp` must not assume a capsule.
    const Theme metro = resolve_theme(ThemeRequest{.variant = ThemeVariant::Metro}, OsThemeSnapshot{});
    CHECK(ui::detail::controls(metro).toggle_switch_track_radius_ratio == 0.0f);
    CHECK(ui::detail::controls(metro).toggle_switch_track_size == Size{50.0f, 19.0f});
    // The knob is 12 wide and travels 38, and 12 + 38 == 50 exactly.
    CHECK(ui::detail::controls(metro).toggle_knob.rest == 12.0f);
    CHECK(ui::detail::controls(metro).toggle_knob.travel == 38.0f);
    CHECK(ui::detail::controls(metro).toggle_knob.rest + ui::detail::controls(metro).toggle_knob.travel ==
          ui::detail::controls(metro).toggle_switch_track_size.width);
    // The era never animates the knob's size: it never resizes.
    CHECK(ui::detail::controls(metro).toggle_knob.hover == ui::detail::controls(metro).toggle_knob.rest);

    // Everyone else keeps the capsule.
    for (ThemeVariant v : {ThemeVariant::Fluent2, ThemeVariant::Fluent, ThemeVariant::Mdl2})
    {
        const Theme other = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        CHECK(ui::detail::controls(other).toggle_switch_track_radius_ratio == 0.5f);
    }
}

TEST_CASE("Metro pins every radius-bearing token, not just the global one", "[theme][metro]")
{
    // 8.1 rounds no corner at all. Left unset, `overlay_corner_radius`
    // keeps Fluent's 8 and rounds every light-dismiss popup.
    const Theme metro = resolve_theme(ThemeRequest{.variant = ThemeVariant::Metro}, OsThemeSnapshot{});
    CHECK(metro.corner_radius == 0.0f);
    CHECK(metro.overlay_corner_radius == 0.0f);
    CHECK(ui::detail::controls(metro).slider_thumb.radius == 0.0f);
    CHECK(ui::detail::controls(metro).toggle_switch_track_radius_ratio == 0.0f);

    // The icon font stays: the render/IconGlyph.hpp codepoints are Segoe MDL2 / Fluent ones, which the
    // era's Segoe UI Symbol does not carry, so switching would render tofu.
    CHECK(std::string_view{metro.icon_font_family} == std::string_view{Theme{}.icon_font_family});
}

TEST_CASE("Every theme centers a control's own label - Metro included", "[theme][retro][mdl2][metro]")
{
    // Seeding `text_style` from the Leading-aligned Body ramp entry pushes a button's label flush
    // against its border. Neither era aligns a Button's label, so it centers.
    for (ThemeVariant v :
         {ThemeVariant::Fluent2,
          ThemeVariant::Fluent,
          ThemeVariant::Metro,
          ThemeVariant::Mdl2,
          ThemeVariant::Aero,
          ThemeVariant::Luna,
          ThemeVariant::Windows9x,
          ThemeVariant::Windows3x,
          ThemeVariant::Windows1x})
    {
        const Theme theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        CHECK(theme.text_style.horizontal == TextAlign::Center);
        CHECK(theme.text_style.vertical == TextAlign::Center);
    }

    // The type ramp stays leading-aligned: that is a page's text, not a control's label.
    for (ThemeVariant v : {ThemeVariant::Metro, ThemeVariant::Mdl2})
    {
        const Theme theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        CHECK(theme.type.body.horizontal == TextAlign::Leading);
        CHECK(theme.type.header.horizontal == TextAlign::Leading);
    }
}

// ---- MDL: Windows 8 & 8.1 merged, the genuine Metro pack ---------------------------------------

TEST_CASE("Metro dark: Button is a pure 2px outline with a transparent fill, no gradient anywhere", "[theme][metro]")
{
    const Theme theme = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});

    CHECK(theme.corner_radius == 0.0f);
    CHECK(theme.button_normal.background == colors::Transparent);
    CHECK(theme.button_normal.draw_border);
    CHECK(theme.button_normal.resolved_border_width(theme.border_width) == 2.0f);
    CHECK_FALSE(theme.button_normal.background_gradient.has_value());
    CHECK_FALSE(theme.button_normal.border_gradient.has_value());
    CHECK(ui::detail::controls(theme).press_state_flip);
}

TEST_CASE("Metro light: Button is a borderless flat gray fill", "[theme][metro]")
{
    const Theme theme = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});

    CHECK_FALSE(theme.button_normal.draw_border);
    CHECK(theme.button_normal.resolved_border_width(theme.border_width) == 0.0f);
    CHECK(theme.button_normal.background != colors::Transparent);
}

TEST_CASE("Metro's pressed state is a hard fg/bg inversion, not a lighter/darker shade", "[theme][metro]")
{
    const Theme dark = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                     OsThemeSnapshot{});
    // Dark normal: transparent bg / white text. Pressed: white bg / black text, a full flip.
    CHECK(dark.button_pressed.background == colors::White);
    CHECK(dark.button_pressed.text == colors::Black);

    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});
    CHECK(light.button_pressed.background == colors::Black);
    CHECK(light.button_pressed.text == colors::White);
}

TEST_CASE("Metro's accent fill is the raw accent color, not a lightened/darkened ramp rung", "[theme][metro]")
{
    const Theme theme = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});
    CHECK(theme.accent_button_normal.background == Color::from_argb(0xFF4617B4));
}

TEST_CASE("Metro's accent_override replaces the default 8.1 violet", "[theme][metro]")
{
    constexpr Color Override = Color::from_argb(0xFF00A000);
    const Theme     theme    = resolve_theme(ThemeRequest{.mode            = ThemeMode::Dark,
                                                          .accent_override = Override,
                                                          .variant         = ThemeVariant::Metro},
                                             OsThemeSnapshot{});
    CHECK(theme.accent_button_normal.background == Override);
}

TEST_CASE("Metro is one of the few variants that genuinely resolves a different Theme for dark vs. light",
          "[theme][metro]")
{
    const Theme dark  = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});
    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});

    CHECK(dark.colors().background != light.colors().background);
    CHECK(dark.button_normal.draw_border != light.button_normal.draw_border);

    // ThemeMode::System should also track os.dark, unlike every fixed-light-only variant.
    OsThemeSnapshot os_dark;
    os_dark.dark            = true;
    const Theme system_dark = resolve_theme(ThemeRequest{.variant = ThemeVariant::Metro}, os_dark);
    CHECK(system_dark.colors().background == dark.colors().background);
}

TEST_CASE("Metro's type ramp matches 8.1's text sizes and weights", "[theme][metro]")
{
    const Theme theme = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});

    CHECK(theme.type.header.font_size == 56.0f);
    CHECK(theme.type.header.weight == FontWeight::Light);
    CHECK(theme.type.header.line_height == 40.0f);

    CHECK(theme.type.subheader.font_size == Catch::Approx(26.667f));
    CHECK(theme.type.subheader.weight == FontWeight::Light);

    CHECK(theme.type.title.font_size == Catch::Approx(14.667f));
    CHECK(theme.type.title.weight == FontWeight::SemiBold);

    CHECK(theme.type.body.font_size == Catch::Approx(14.667f));
    CHECK(theme.type.body.weight == FontWeight::SemiLight);
    CHECK(theme.type.body.line_height == 20.0f);

    CHECK(theme.type.caption.font_size == 12.0f);
}

TEST_CASE("Metro's popup surface is white in *both* flavors: 8.1 pins every flyout to the Light theme", "[theme][metro]")
{
    // Flyouts, menus and settings flyouts are always light in 8.1,
    // so a menu in a dark Metro app is a white box.
    const Theme dark  = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});
    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});

    CHECK(dark.surfaces.menu == colors::White);
    CHECK(dark.surfaces.menu == light.surfaces.menu);
    CHECK(dark.surfaces.menu_text == light.surfaces.menu_text);
    CHECK(dark.surfaces.menu_text == Color::from_argb(0xFF000000));

    // A hovered menu item: a flat gray wash, never the violet. 8.1 spends the accent on
    // selection, and a menu item is never selected.
    CHECK(dark.surfaces.menu_highlight == Color::from_argb(0xFFE5E5E5));
    CHECK(dark.surfaces.menu_highlight_text == Color::from_argb(0xFF000000));
    CHECK(dark.surfaces.menu_highlight != dark.accent_button_normal.background);
}

TEST_CASE("Metro's scrollbar does not invert with the theme - all 17 era colors are the same in light and dark",
          "[theme][metro]")
{
    const Theme dark  = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});
    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});

    CHECK(dark.surfaces.scrollbar_track == light.surfaces.scrollbar_track);
    CHECK(dark.surfaces.scrollbar_thumb == light.surfaces.scrollbar_thumb);
    CHECK(dark.surfaces.scrollbar_arrow == light.surfaces.scrollbar_arrow);

    CHECK(dark.surfaces.scrollbar_track == Color::from_argb(0x59D5D5D5));
    CHECK(dark.surfaces.scrollbar_thumb == Color::from_argb(0xFFCDCDCD));
    CHECK(dark.surfaces.scrollbar_arrow == Color::from_argb(0x99000000));
}

TEST_CASE("Metro's remaining per-theme surface values are the 8.1 literals, not role approximations", "[theme][metro]")
{
    const Theme dark  = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});
    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});

    // The text field's border. `#FFACACAC` (a Win32 system color) occurs nowhere in 8.1.
    CHECK(dark.surfaces.field_border == Color::from_argb(0xFFFFFFFF));
    CHECK(light.surfaces.field_border == Color::from_argb(0xA3000000));

    // The menu separator, a flat mid-gray in both. `divider` must follow it or it is
    // an opaque black line.
    CHECK(dark.surfaces.separator_light == Color::from_argb(0xFF7A7A7A));
    CHECK(light.surfaces.separator_light == Color::from_argb(0xFF7A7A7A));
    CHECK(dark.colors().divider == Color::from_argb(0xFF7A7A7A));
    CHECK(light.colors().divider == Color::from_argb(0xFF7A7A7A));

    // The pressed switch fill. `#FF5729C1` is the dark switch's rest color.
    CHECK(dark.accent_button_pressed.background == Color::from_argb(0xFF7E4FEC));
    CHECK(light.accent_button_pressed.background == Color::from_argb(0xFF7241E4));

    // The Button's own padding, symmetric; "12,4,12,5" belongs to the repeat button.
    CHECK(dark.padding.bottom == 4.0f);
    CHECK(dark.padding.top == 4.0f);
    CHECK(dark.padding.left == 12.0f);
}

TEST_CASE("Metro asks DWM for square corners like every other pre-Win11 retro variant", "[theme][metro]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Metro}, OsThemeSnapshot{});
    CHECK(theme.chrome.corner_preference == CornerPreference::DoNotRound);
}

// ---- Fluent 1 / Windows 10 --------------------------------------------------------------

TEST_CASE("Fluent is dark/light + accent aware, like Fluent2 and Metro", "[theme][fluent1]")
{
    constexpr ThemeVariant Variant = ThemeVariant::Fluent;
    const Theme dark  = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = Variant}, OsThemeSnapshot{});
    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = Variant}, OsThemeSnapshot{});
    CHECK(dark.colors().background != light.colors().background);

    constexpr Color Override = Color::from_argb(0xFF00A000);
    const Theme accented = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .accent_override = Override, .variant = Variant},
                                         OsThemeSnapshot{});
    CHECK(accented.colors().background == dark.colors().background);
}

// Thirteen Windows 11 controls draw the "lift" border and no Fluent 1 control does, so it
// is a Windows 11 signature and Fluent 1 drops it.
TEST_CASE("Fluent drops the elevation-lift border that Fluent2 draws", "[theme][fluent1]")
{
    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});
    REQUIRE(fluent2.button_normal.border_gradient.has_value());
    REQUIRE(ui::detail::controls(fluent2).field_border_gradient.has_value());

    const Theme fluent1 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{});
    CHECK_FALSE(fluent1.button_normal.border_gradient.has_value());
    CHECK_FALSE(fluent1.button_hover.border_gradient.has_value());
    CHECK_FALSE(fluent1.accent_button_normal.border_gradient.has_value());
    CHECK_FALSE(fluent1.accent_button_hover.border_gradient.has_value());
    CHECK_FALSE(ui::detail::controls(fluent1).field_border_gradient.has_value());
}

// Fluent 1's corner radii (2 / 4) vs Fluent 2's (4 / 8), and Fluent 1's button padding
// 8,5,8,6 vs 11,5,11,6.
TEST_CASE("Fluent uses its own geometry: radius 2/4 and the narrower button padding", "[theme][fluent1]")
{
    const Theme fluent1 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{});
    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});

    CHECK(fluent1.corner_radius == 2.0f);
    CHECK(fluent1.overlay_corner_radius == 4.0f);
    CHECK(fluent1.padding == Edges{.left = 8.0f, .top = 5.0f, .right = 8.0f, .bottom = 6.0f});

    CHECK(fluent2.corner_radius == 4.0f);
    CHECK(fluent2.overlay_corner_radius == 8.0f);
    CHECK(fluent2.padding == Edges{.left = 11.0f, .top = 5.0f, .right = 11.0f, .bottom = 6.0f});
}

// Fluent 1 makes all eight button borders transparent in light and dark:
// a bare fill, with hover and press carried by the fill alone.
TEST_CASE("Fluent draws no button border in any state", "[theme][fluent1]")
{
    for (ThemeMode mode : {ThemeMode::Dark, ThemeMode::Light})
    {
        const Theme theme = resolve_theme(ThemeRequest{.mode = mode, .variant = ThemeVariant::Fluent}, OsThemeSnapshot{});
        for (const ButtonVisual& v :
             {theme.button_normal,
              theme.button_hover,
              theme.button_pressed,
              theme.button_disabled,
              theme.accent_button_normal,
              theme.accent_button_hover,
              theme.accent_button_pressed,
              theme.accent_button_disabled})
        {
            CHECK(v.border == colors::Transparent);
        }
    }
}

// ---- Fluent 1's own color layer --------------------------------------------------------
// Values match Fluent 1 as of Windows 10.0.20348.0.

TEST_CASE("Fluent's page is pure white/black, not Fluent 2's near-white/near-black", "[theme][fluent1][palette]")
{
    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Fluent},
                                      OsThemeSnapshot{});
    const Theme dark  = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Fluent},
                                      OsThemeSnapshot{});

    // The page background, the loudest difference from a Windows 11 screenshot.
    CHECK(light.colors().background == colors::White);
    CHECK(dark.colors().background == colors::Black);
    CHECK(light.colors().text == colors::Black); // text_100, fully opaque
    CHECK(dark.colors().text == colors::White);

    const Theme fluent2 = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Fluent2},
                                        OsThemeSnapshot{});
    CHECK(fluent2.colors().background != light.colors().background);
    CHECK(fluent2.colors().text != light.colors().text); // Fluent 2's is 0xE4-alpha, never opaque
}

// The button fill is text_20 (20 %) at rest, text_100 at 10 % on hover,
// text_40 (40 %) pressed, and text_20 again disabled.
TEST_CASE("Fluent's button fill goes DOWN on hover and up on press", "[theme][fluent1][palette]")
{
    for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark})
    {
        const Theme t = resolve_theme(ThemeRequest{.mode = mode, .variant = ThemeVariant::Fluent}, OsThemeSnapshot{});
        // One color at four alphas: black in Light, white in Dark.
        const Color base = mode == ThemeMode::Dark ? colors::White : colors::Black;
        CHECK(t.button_normal.background == base.with_alpha(0x33 / 255.0f));
        CHECK(t.button_hover.background == base.with_alpha(0x19 / 255.0f));
        CHECK(t.button_pressed.background == base.with_alpha(0x66 / 255.0f));
        // Disabled is the same fill as rest; only the label grays out.
        CHECK(t.button_disabled.background == t.button_normal.background);
        CHECK(t.button_disabled.text == base.with_alpha(0x66 / 255.0f));

        // Hover is lighter than rest.
        CHECK(t.button_hover.background.a < t.button_normal.background.a);
        CHECK(t.button_pressed.background.a > t.button_normal.background.a);
    }

    // ...and Fluent 2 does the opposite, which is why this cannot be shared.
    const Theme fluent2 = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Fluent2},
                                        OsThemeSnapshot{});
    CHECK(fluent2.button_hover.background.a > fluent2.button_normal.background.a);
}

// The accent button's hover and press fills are the accent's Light1 and Dark1 rungs
// in light and dark alike, where Fluent 2 fades one fill to 90 %/80 % alpha.
TEST_CASE("Fluent's accent button is base / Light1 / Dark1 in both themes", "[theme][fluent1][palette]")
{
    OsThemeSnapshot os;
    os.accent             = Color::from_argb(0xFF0063B1);
    os.accent_ramp.light1 = Color::from_argb(0xFF3B8CD9); // rungs distinct from every other field
    os.accent_ramp.dark1  = Color::from_argb(0xFF00396B);

    for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark})
    {
        const Theme t = resolve_theme(ThemeRequest{.mode = mode, .variant = ThemeVariant::Fluent}, os);
        CHECK(t.accent_button_normal.background == os.accent); // the *base* accent, not a rung
        CHECK(t.accent_button_hover.background == os.accent_ramp.light1);
        CHECK(t.accent_button_pressed.background == os.accent_ramp.dark1);
        // The accent button's label is white in light and dark: white
        // text on accent even in dark theme, the reverse of Fluent 2.
        CHECK(t.accent_button_normal.text == colors::White);
        CHECK(t.accent_button_pressed.text == colors::White);
        // The disabled accent button is text_20: fully neutral.
        CHECK(t.accent_button_disabled.background ==
              (mode == ThemeMode::Dark ? colors::White : colors::Black).with_alpha(0x33 / 255.0f));
    }

    // Fluent 2 picks a ramp rung per theme for the *rest* fill; Fluent 1 never does.
    const Theme fluent2 = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Fluent2}, os);
    CHECK(fluent2.accent_button_normal.background != os.accent);
}

// The unchecked CheckBox, RadioButton and ToggleSwitch strokes:
// one 60 -> 80 -> 100 % ladder over a fill transparent until Pressed.
TEST_CASE("Fluent's unchecked toggle is a stroke ladder, not a filling box", "[theme][fluent1][palette]")
{
    const Theme t = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Fluent},
                                  OsThemeSnapshot{});

    for (const ToggleVisuals* tv :
         {&ui::detail::controls(t).check_box_box, &ui::detail::controls(t).radio_ring, &ui::detail::controls(t).toggle_track})
    {
        CHECK(tv->normal.off.border == colors::Black.with_alpha(0x99 / 255.0f));
        CHECK(tv->hover.off.border == colors::Black.with_alpha(0xCC / 255.0f));
        CHECK(tv->pressed.off.border == colors::Black);
        CHECK(tv->disabled.off.border == colors::Black.with_alpha(0x66 / 255.0f));

        CHECK(tv->normal.off.background == colors::Transparent);
        CHECK(tv->hover.off.background == colors::Transparent);
        CHECK(tv->pressed.off.background == colors::Black.with_alpha(0x66 / 255.0f)); // the one filling state
        CHECK(tv->disabled.off.background == colors::Transparent);

        // The off knob is text_100 in all enabled states,
        // where Fluent 2 uses the secondary text color.
        CHECK(tv->normal.off.text == colors::Black);
        CHECK(tv->pressed.off.text == colors::Black);
    }

    // Fluent 2 walks a fill ramp and keeps one stroke across rest and hover.
    const Theme fluent2 = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Fluent2},
                                        OsThemeSnapshot{});
    CHECK(ui::detail::controls(fluent2).check_box_box.normal.off.border ==
          ui::detail::controls(fluent2).check_box_box.hover.off.border);
}

// The focus ring is text_100 outside and page_60 inside:
// opaque black/white outside, 60 % of its inverse inside, never accent.
TEST_CASE("Fluent's focus ring is text_100 over page_60, and never accent", "[theme][fluent1][palette]")
{
    OsThemeSnapshot os;
    os.accent = Color::from_argb(0xFF00A000);

    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Fluent}, os);
    CHECK(light.colors().focus == colors::Black);
    CHECK(light.colors().focus_inner == colors::White.with_alpha(0x99 / 255.0f));

    const Theme dark = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Fluent}, os);
    CHECK(dark.colors().focus == colors::White);
    CHECK(dark.colors().focus_inner == colors::Black.with_alpha(0x99 / 255.0f));

    CHECK(light.colors().focus != os.accent);
    CHECK(dark.colors().focus != os.accent);
    // Geometry is shared with Fluent 2: 2 epx outer, 1 epx inner, -3 margin.
    CHECK(light.focus_ring_outer_width == 2.0f);
    CHECK(light.focus_ring_inner_width == 1.0f);
}

// The era's seven text styles, 1809 and later (Base/Body 14, not 15). This era's large
// styles get lighter as they grow; Fluent 2's are SemiBold throughout.
TEST_CASE("Fluent uses the era type ramp, which lightens as it grows", "[theme][fluent1][palette]")
{
    const Theme t = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{});

    CHECK(t.type.header.font_size == 46.0f);
    CHECK(t.type.header.weight == FontWeight::Light);
    CHECK(t.type.subheader.font_size == 34.0f);
    CHECK(t.type.subheader.weight == FontWeight::Light);
    CHECK(t.type.title.font_size == 24.0f);
    CHECK(t.type.title.weight == FontWeight::SemiLight);
    CHECK(t.type.subtitle.font_size == 20.0f);
    CHECK(t.type.subtitle.weight == FontWeight::Regular);
    CHECK(t.type.body.font_size == 14.0f);
    CHECK(t.type.caption.font_size == 12.0f);

    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});
    CHECK(fluent2.type.header.font_size == 68.0f); // Display
    CHECK(fluent2.type.title.weight == FontWeight::SemiBold);
}

// A focused text field's border is a uniform `2` this era, against Windows 11's
// bottom-only `1,1,1,2`: a focused Windows 10 field is ringed in accent, not underlined.
TEST_CASE("Fluent thickens a focused text field on all four sides", "[theme][fluent1]")
{
    const Theme t       = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{});
    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});

    CHECK(ui::detail::controls(t).field_focus_border_thickness == Thickness{2.0f});
    CHECK(ui::detail::controls(fluent2).field_focus_border_thickness == Edges{.bottom = 2.0f});

    // The focused fill is page_100: opaque, the page color. Rest and hover
    // are alt-family washes, the inverse of the page.
    REQUIRE(ui::detail::controls(t).field_fills.has_value());
    CHECK(ui::detail::controls(t).field_fills->focused == colors::White);
    CHECK(t.colors().field == colors::White.with_alpha(0.4f));
    CHECK(ui::detail::controls(t).field_fills->hover == colors::White.with_alpha(0.6f));

    const Theme dark = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Fluent},
                                     OsThemeSnapshot{});
    REQUIRE(ui::detail::controls(dark).field_fills.has_value());
    CHECK(ui::detail::controls(dark).field_fills->focused == colors::Black); // NOT the white-in-both-themes OS value
    CHECK(dark.colors().field_text == colors::White);                        // ...which would be invisible against it
}

// The faster, fast and normal animation durations are Windows 11 additions. Every Fluent 1 state
// color change is instant, so the fill snaps.
TEST_CASE("Fluent snaps its state colors instead of cross-fading them", "[theme][fluent1]")
{
    const Theme t       = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{});
    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});

    CHECK(t.anim_faster == std::chrono::milliseconds{0});
    CHECK(fluent2.anim_faster == std::chrono::milliseconds{83}); // Fluent 2's 83 ms brush transition
    // The ToggleSwitch knob still slides, through its own reposition animation.
    CHECK(t.anim_fast == fluent2.anim_fast);
}

// The dialog's backdrop is page_60,
// 60 % of the page's inverse, so Windows 10 washes a light page out rather than darkening it.
TEST_CASE("Fluent's modal scrim is 60 % of the page's inverse", "[theme][fluent1]")
{
    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Fluent},
                                      OsThemeSnapshot{});
    const Theme dark  = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Fluent},
                                      OsThemeSnapshot{});
    CHECK(light.colors().scrim == colors::White.with_alpha(0.6f));
    CHECK(dark.colors().scrim == colors::Black.with_alpha(0.6f));

    // Fluent 2's scrim is a flat 30 % black in light *and* dark.
    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});
    CHECK(fluent2.colors().scrim == Color::from_argb(0x4D000000));
}

// Windows 10 list rows fill with accent; Windows 11 marks selection with a pill. Both agree on an
// unselected row, so the shared flag stays on for both.
TEST_CASE("Fluent fills a selected list row rather than washing it", "[theme][fluent1][palette]")
{
    OsThemeSnapshot os;
    os.accent = Color::from_argb(0xFF0063B1);

    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, os);
    CHECK(ui::detail::controls(fluent2).list_item_subtle_selection);
    CHECK_FALSE(ui::detail::controls(fluent2).row_selection_fills.has_value()); // a pill, not a fill

    for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark})
    {
        const Theme t = resolve_theme(ThemeRequest{.mode = mode, .variant = ThemeVariant::Fluent}, os);
        CHECK(ui::detail::controls(t).list_item_subtle_selection); // the unselected half is shared
        REQUIRE(ui::detail::controls(t).row_selection_fills.has_value());

        // The accent at its medium-low / medium list rungs (Light) or its low rung
        // (Dark), both 0.6 / the very-high rung.
        CHECK(ui::detail::subtle_row_background(t, ButtonState::Normal, true) == os.accent.with_alpha(0.75f));
        CHECK(ui::detail::subtle_row_background(t, ButtonState::Hover, true) == os.accent.with_alpha(0.6f));
        CHECK(ui::detail::subtle_row_background(t, ButtonState::Pressed, true) == os.accent.with_alpha(0.9f));
        // Hovering a selected row makes it fainter, the same down-then-up shape as the chrome ladder.
        CHECK(ui::detail::subtle_row_background(t, ButtonState::Hover, true).a <
              ui::detail::subtle_row_background(t, ButtonState::Normal, true).a);

        // An unselected row keeps the neutral subtle ramp: list_hover / list_pressed.
        const Color base = mode == ThemeMode::Dark ? colors::White : colors::Black;
        CHECK(ui::detail::subtle_row_background(t, ButtonState::Normal, false) == colors::Transparent);
        CHECK(ui::detail::subtle_row_background(t, ButtonState::Hover, false) == base.with_alpha(0x19 / 255.0f));
        CHECK(ui::detail::subtle_row_background(t, ButtonState::Pressed, false) == base.with_alpha(0x33 / 255.0f));

        // A selected ComboBox row is the low rung, where a ListView row rests at medium-low.
        const float low = mode == ThemeMode::Dark ? 0.6f : 0.4f; // dark runs 0.2 hotter at every step
        CHECK(ui::detail::controls(t).row_selection_fills->popup_selected == os.accent.with_alpha(low));

        // A selected NavigationShell row has no fill this era; the
        // accent bar carries selection alone.
        CHECK(ui::detail::subtle_row_background(t, ButtonState::Normal, true, ThemeRowSelection::IndicatorOnly) ==
              ui::detail::subtle_row_background(t, ButtonState::Normal, false));
        CHECK(ui::detail::subtle_row_background(fluent2, ButtonState::Normal, true, ThemeRowSelection::IndicatorOnly) ==
              ui::detail::subtle_row_background(fluent2, ButtonState::Normal, true));
    }
}

// A hovered text field has its own border color this era (60 % -> 80 %); Windows 11
// keeps the rest color.
TEST_CASE("Fluent recolors a hovered text field's border", "[theme][fluent1][palette]")
{
    const Theme t       = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Fluent},
                                        OsThemeSnapshot{});
    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});

    REQUIRE(ui::detail::controls(t).field_border_hover.has_value());
    CHECK(t.surfaces.field_border == colors::Black.with_alpha(0x99 / 255.0f));                     // text_60
    CHECK(*ui::detail::controls(t).field_border_hover == colors::Black.with_alpha(0xCC / 255.0f)); // text_80
    CHECK_FALSE(ui::detail::controls(fluent2).field_border_hover.has_value());
}

// The hover-grow and press-shrink indicators are Windows 11 additions; Fluent 1 sizes never animate.
TEST_CASE("Fluent's indicators are fixed-size, at the era's geometry", "[theme][fluent1]")
{
    const Theme t = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{});

    CHECK(ui::detail::controls(t).toggle_switch_track_size == Size{40.0f, 20.0f}); // unchanged in Windows 11
    CHECK(ui::detail::controls(t).toggle_knob.rest == 10.0f);                      // 12 in Fluent 2
    CHECK(ui::detail::controls(t).toggle_knob.hover == ui::detail::controls(t).toggle_knob.rest);
    CHECK(ui::detail::controls(t).radio_dot.rest == 8.0f); // 12/14/10 in Fluent 2
    CHECK(ui::detail::controls(t).radio_dot.hover == ui::detail::controls(t).radio_dot.rest);
    CHECK(ui::detail::controls(t).radio_dot.pressed == ui::detail::controls(t).radio_dot.rest);
    CHECK(ui::detail::controls(t).slider_track_thickness == 2.0f); // the track thickness; Fluent 2 draws 4
    // A single 20x20 thumb, so the inner and outer disc are the same size.
    CHECK(ui::detail::controls(t).slider_thumb.outer == 20.0f);
    CHECK(ui::detail::controls(t).slider_thumb.rest == ui::detail::controls(t).slider_thumb.outer);
}

// The icon font is `Segoe MDL2 Assets` from 1709 to 20348; asking the system for its icon font
// returns the Windows 11 one, so the era font is named outright. Same codepoints.
TEST_CASE("Fluent draws its glyphs in Segoe MDL2 Assets", "[theme][fluent1]")
{
    const Theme t       = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{});
    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});
    CHECK(std::string_view{t.icon_font_family} == "Segoe MDL2 Assets");
    CHECK(std::string_view{fluent2.icon_font_family} == "Segoe Fluent Icons");

    // Retro packs keep the default too: `Segoe MDL2 Assets` is a Windows 10 font.
    const Theme luna = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});
    CHECK(std::string_view{luna.icon_font_family} == "Segoe Fluent Icons");
}

// The indicator's width, height and radius, and its 4,0,0,0 margin.
// A different bar in each era.
TEST_CASE("Fluent's NavigationShell indicator is the era's 2x24 bar, inset 4", "[theme][fluent1]")
{
    const Theme t       = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{});
    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});

    CHECK(ui::detail::controls(t).nav_indicator.size == Size{2.0f, 24.0f});
    CHECK(ui::detail::controls(t).nav_indicator.left_inset == 4.0f);
    CHECK(ui::detail::controls(t).nav_indicator.radius == 2.0f);

    CHECK(ui::detail::controls(fluent2).nav_indicator.size == Size{3.0f, 16.0f});
    CHECK(ui::detail::controls(fluent2).nav_indicator.left_inset == 0.0f); // flush with the pill's left edge
    CHECK(ui::detail::controls(fluent2).nav_indicator.radius == 2.0f);     // the one number the two eras share
}

// In Fluent 1 the thumb follows the accent triple. chrome_contrast /
// chrome_strong belong to the ColorPicker's slider, whose thumb must not take the accent.
TEST_CASE("Fluent's Slider thumb follows the era's accent triple, not ColorPicker's grays",
          "[theme][fluent1][palette][slider]")
{
    OsThemeSnapshot os;
    os.accent = Color::from_argb(0xFF0063B1);

    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Fluent}, os);
    REQUIRE(ui::detail::controls(light).slider_fills.has_value());
    CHECK(ui::detail::controls(light).slider_fills->thumb == os.accent);                    // the accent
    CHECK(ui::detail::controls(light).slider_fills->thumb_hover == os.accent_ramp.light1);  // the Light1 rung
    CHECK(ui::detail::controls(light).slider_fills->thumb_pressed == os.accent_ramp.dark1); // the Dark1 rung
    // Not the ColorPicker pair.
    CHECK(ui::detail::controls(light).slider_fills->thumb_hover != Color::from_argb(0xFF171717));   // chrome_contrast
    CHECK(ui::detail::controls(light).slider_fills->thumb_pressed != Color::from_argb(0xFFCCCCCC)); // chrome_strong
    // The track's rest -> hover fill is the 40 % -> 60 % base step.
    CHECK(ui::detail::controls(light).slider_fills->track == colors::Black.with_alpha(0x66 / 255.0f));
    CHECK(ui::detail::controls(light).slider_fills->track_hover == colors::Black.with_alpha(0x99 / 255.0f));

    // The thumb is the same triple in dark and light; only the track inverts.
    const Theme dark = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Fluent}, os);
    REQUIRE(ui::detail::controls(dark).slider_fills.has_value());
    CHECK(ui::detail::controls(dark).slider_fills->thumb_hover == os.accent_ramp.light1);
    CHECK(ui::detail::controls(dark).slider_fills->thumb_pressed == os.accent_ramp.dark1);
    CHECK(ui::detail::controls(dark).slider_fills->track == colors::White.with_alpha(0x66 / 255.0f));

    // MDL2 is the neutral-press case, and it has no hover rung.
    const Theme mdl2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, os);
    REQUIRE(ui::detail::controls(mdl2).slider_fills.has_value());
    CHECK(ui::detail::controls(mdl2).slider_fills->thumb_pressed == Color::from_argb(0xFFCCCCCC)); // chrome_strong, Light
    CHECK(ui::detail::controls(mdl2).slider_fills->thumb_hover == ui::detail::controls(mdl2).slider_fills->thumb);

    // Fluent 2 names none of these and keeps Slider.cpp's derived approximation.
    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, os);
    CHECK_FALSE(ui::detail::controls(fluent2).slider_fills.has_value());
}

// An open ComboBox's field is the accent's low list rung, focused or not, so an open
// Windows 10 ComboBox washes its field in accent.
TEST_CASE("Fluent accent-washes an open ComboBox field", "[theme][fluent1][palette]")
{
    OsThemeSnapshot os;
    os.accent = Color::from_argb(0xFF0063B1);

    for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark})
    {
        const Theme t   = resolve_theme(ThemeRequest{.mode = mode, .variant = ThemeVariant::Fluent}, os);
        const float low = mode == ThemeMode::Dark ? 0.6f : 0.4f;
        REQUIRE(ui::detail::controls(t).combo_open_fill.has_value());
        CHECK(*ui::detail::controls(t).combo_open_fill == os.accent.with_alpha(low));
        // Same rung as the popup's selected row, and deliberately not the same object.
        CHECK(*ui::detail::controls(t).combo_open_fill == ui::detail::controls(t).row_selection_fills->popup_selected);
        // The popup ladder deepens on hover where the row ladder fades.
        CHECK(ui::detail::controls(t).row_selection_fills->popup_selected_hover.a >
              ui::detail::controls(t).row_selection_fills->popup_selected.a);
        CHECK(ui::detail::controls(t).row_selection_fills->hover.a < ui::detail::controls(t).row_selection_fills->rest.a);
    }

    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, os);
    CHECK_FALSE(ui::detail::controls(fluent2).combo_open_fill.has_value());
}

// The scroll bar thumb is chrome_disabled_strong, an opaque gray, and the
// arrows are full-strength text_100, not Fluent 2's 60 %.
TEST_CASE("Fluent's scrollbar is opaque chrome gray, not a wash", "[theme][fluent1][palette]")
{
    const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Fluent},
                                      OsThemeSnapshot{});
    CHECK(light.surfaces.scrollbar_thumb == Color::from_argb(0xFF7A7A7A));
    CHECK(light.surfaces.scrollbar_thumb.a == 1.0f);
    CHECK(light.surfaces.scrollbar_arrow == colors::Black);                                 // text_100
    CHECK(light.surfaces.scrollbar_track == Color::from_argb(0xFFE6E6E6).with_alpha(0.9f)); // chrome_mid at 0.9

    const Theme dark = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Fluent},
                                     OsThemeSnapshot{});
    CHECK(dark.surfaces.scrollbar_thumb == Color::from_argb(0xFF858585));
    CHECK(dark.surfaces.scrollbar_arrow == colors::White);

    // Fluent 2's thumb is the strong control fill, a translucent wash.
    const Theme fluent2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});
    CHECK(fluent2.surfaces.scrollbar_thumb.a < 1.0f);
}

TEST_CASE("theme_variant_supports_dark_mode() mirrors traits_of()'s dark/light-aware set", "[theme]")
{
    for (ThemeVariant v : {ThemeVariant::Fluent2, ThemeVariant::Fluent, ThemeVariant::Metro, ThemeVariant::Mdl2})
    {
        CHECK(theme_variant_supports_dark_mode(v));
    }
    for (ThemeVariant v :
         {ThemeVariant::Aero, ThemeVariant::Luna, ThemeVariant::Windows9x, ThemeVariant::Windows3x, ThemeVariant::Windows1x})
    {
        CHECK_FALSE(theme_variant_supports_dark_mode(v));
    }
}

TEST_CASE("High contrast recolors a retro variant without dropping its construction", "[theme][retro]")
{
    OsThemeSnapshot os;
    os.high_contrast         = true;
    os.hc.control_background = Color::from_argb(0xFF000000);

    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, os);

    // A classic page is BTNFACE-colored, and HC is one more classic color scheme.
    CHECK(theme.colors().background == os.hc.control_background);
    CHECK(theme.button_normal.border_style == BorderStyle::Raised);
}

// ---- Control-level token wirings --------------------------------------------------------
// The palette had the right value and the control read a neighboring token instead.

TEST_CASE("ToggleSwitch's off knob is the foreground token, not the track stroke", "[palette][theme]")
{
    // The off knob is the secondary text color, a different token from the
    // off track's outline.
    for (const Palette& palette : {Palette::fluent_light(), Palette::fluent_dark()})
    {
        const Theme theme = Theme::fluent(palette);
        CHECK(ui::detail::controls(theme).toggle_knob_off_foreground);
        CHECK(ui::detail::controls(theme).toggle_track.normal.off.text == palette.text_fill_secondary);
        CHECK(ui::detail::controls(theme).toggle_track.hover.off.text == palette.text_fill_secondary);
        CHECK(ui::detail::controls(theme).toggle_track.pressed.off.text == palette.text_fill_secondary);
        CHECK(ui::detail::controls(theme).toggle_track.disabled.off.text == palette.text_fill_disabled);
        // Different values in both themes.
        CHECK(ui::detail::controls(theme).toggle_track.normal.off.text !=
              ui::detail::controls(theme).toggle_track.normal.off.border);
        CHECK(ui::detail::controls(theme).toggle_track.normal.off.border == palette.control_strong_stroke_default);
    }
    // Every non-Fluent pack reads the off visual's border, where its toggle tokens put the knob.
    CHECK_FALSE(
        ui::detail::controls(Theme::classic(ClassicPalette::windows_standard(), /*hot_track=*/false, "MS Sans Serif"))
            .toggle_knob_off_foreground);
}

TEST_CASE("A focused text field fills with the opaque input-active fill", "[palette][theme]")
{
    // The Focused rung is opaque where the others are washes.
    CHECK(Palette::fluent_light().control_fill_input_active == colors::White);
    CHECK(Palette::fluent_dark().control_fill_input_active == Color::from_argb(0xB31E1E1E));

    for (const Palette& palette : {Palette::fluent_light(), Palette::fluent_dark()})
    {
        const Theme theme = Theme::fluent(palette);
        REQUIRE(ui::detail::controls(theme).field_fills);
        CHECK(ui::detail::controls(theme).field_fills->hover == palette.control_fill_secondary);
        CHECK(ui::detail::controls(theme).field_fills->focused == palette.control_fill_input_active);
        CHECK(ui::detail::controls(theme).field_fills->disabled == palette.control_fill_disabled);
        CHECK(ui::detail::controls(theme).field_fills->focused != theme.colors().field);
    }
    // Light's is plain white, not the #F9F9F9 of the surface ramp it sits next to.
    CHECK(Palette::fluent_light().control_fill_input_active != Palette::fluent_light().solid_background_tertiary);
    // Unset everywhere else, so a classic sunken well keeps its single flat fill.
    CHECK_FALSE(
        ui::detail::controls(Theme::classic(ClassicPalette::windows_standard(), /*hot_track=*/false, "MS Sans Serif"))
            .field_fills.has_value());
}

TEST_CASE("A text field's focus underline is accent-colored, not the focus ring's outer color", "[palette][theme]")
{
    // A focused text field's border ends on the default accent fill. The focus ring's outer color
    // is the ComboBox's token and would paint it black.
    for (const Palette& palette : {Palette::fluent_light(), Palette::fluent_dark()})
    {
        const Theme theme = Theme::fluent(palette);
        REQUIRE(ui::detail::controls(theme).field_focus_underline);
        CHECK(*ui::detail::controls(theme).field_focus_underline == palette.accent_fill_default);
        CHECK(*ui::detail::controls(theme).field_focus_underline != theme.colors().focus);
    }
    CHECK_FALSE(
        ui::detail::controls(Theme::classic(ClassicPalette::windows_standard(), /*hot_track=*/false, "MS Sans Serif"))
            .field_focus_underline.has_value());
}

TEST_CASE("Fluent2's focus underline follows the live OS accent, not the hard-coded fallback", "[theme][fluent2]")
{
    OsThemeSnapshot os;
    os.accent_ramp.dark1 = Color::from_argb(0xFF7A0000);
    const Theme theme    = resolve_theme(ThemeRequest{}, os);
    REQUIRE(ui::detail::controls(theme).field_focus_underline);
    CHECK(*ui::detail::controls(theme).field_focus_underline == Color::from_argb(0xFF7A0000));
}

// `Theme::palette` is only filled by `Theme::fluent()`, and `Color` defaults to opaque black, so an
// ungated `palette` read paints black under most themes.
TEST_CASE("No theme resolves InfoBar or ProgressBar surfaces to opaque black", "[theme][regression]")
{
    // Asserted as relationships, not "is it black": high contrast may paint a black surface, but the
    // mark must still differ from the surface under it.
    auto check = [&](const char* name, const Theme& t)
    {
        INFO("theme=" << name);
        for (const Theme::SeverityVisual* s :
             {&t.severity.informational, &t.severity.success, &t.severity.caution, &t.severity.critical})
        {
            CHECK(s->fill != s->background);           // the disc reads on the bar
            CHECK(t.severity.text_on_fill != s->fill); // and the glyph reads on the disc
            CHECK(t.severity.stroke != s->background); // and the bar has an edge
        }
        CHECK(ui::detail::controls(t).progress_track !=
              ui::detail::controls(t).progress_fill.value_or(t.accent_button_normal.background));
    };

    for (ThemeVariant v :
         {ThemeVariant::Fluent2,
          ThemeVariant::Fluent,
          ThemeVariant::Mdl2,
          ThemeVariant::Metro,
          ThemeVariant::Aero,
          ThemeVariant::Luna,
          ThemeVariant::Windows9x,
          ThemeVariant::Windows3x,
          ThemeVariant::Windows1x})
    {
        check("variant", resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{}));
        check("variant/dark", resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = v}, OsThemeSnapshot{}));
    }

    OsThemeSnapshot hc;
    hc.high_contrast        = true;
    hc.hc.window_background = colors::Black;
    hc.hc.window_text       = colors::White;
    hc.hc.control_border    = colors::White;
    hc.hc.accent            = Color::from_argb(0xFFFFFF00);
    hc.hc.accent_text       = colors::Black;
    check("high contrast", resolve_theme(ThemeRequest{}, hc));

    for (ThemeVariant v :
         {ThemeVariant::Fluent2,
          ThemeVariant::Fluent,
          ThemeVariant::Mdl2,
          ThemeVariant::Metro,
          ThemeVariant::Aero,
          ThemeVariant::Luna,
          ThemeVariant::Windows9x,
          ThemeVariant::Windows3x,
          ThemeVariant::Windows1x})
    {
        CAPTURE(static_cast<int>(v));
        check("forced high contrast",
              resolve_theme(ThemeRequest{.mode = ThemeMode::HighContrast, .variant = v}, OsThemeSnapshot{}));
    }
}

// Luna is the one era that states a severity scale, so it can be checked against a document.
TEST_CASE("Luna's severity scale is the era's own four hues", "[theme][retro][luna]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    // XP's color rules: Blue neutral, Green "the start of an action", Red the Close button,
    // Yellow the Log Off button. Each value matches the part the rule names.
    CHECK(theme.severity.informational.fill == Color::from_argb(0xFF316AC5)); // [SysMetrics] Highlight
    CHECK(theme.severity.success.fill == Color::from_argb(0xFF21A121));       // the check/radio glyph green
    CHECK(theme.severity.caution.fill == Color::from_argb(0xFFDFA550));       // the log-off/standby amber
    CHECK(theme.severity.critical.fill == Color::from_argb(0xFFE45D3D));      // [Window.CloseButton] FillColorHint
    // Luna tints signs, not surfaces, so all four bars are `COLOR_WINDOW` white in the field edge.
    CHECK(theme.severity.text_on_fill == colors::White);
    CHECK(theme.severity.stroke == theme.surfaces.field_border);
    for (const Theme::SeverityVisual* s :
         {&theme.severity.informational, &theme.severity.success, &theme.severity.caution, &theme.severity.critical})
    {
        CHECK(s->background == colors::White);
    }

    // "The progress indicator is the green square": `[Progress] fillColor = 46 211 49` over
    // a white track. Fixed, because the era has no control accent.
    REQUIRE(ui::detail::controls(theme).progress_fill.has_value());
    CHECK(*ui::detail::controls(theme).progress_fill == Color::from_argb(0xFF2ED331));
    CHECK(ui::detail::controls(theme).progress_track == colors::White);
    // Every accent-bearing era leaves it unset and keeps reading the accent surface.
    for (ThemeVariant v : {ThemeVariant::Fluent2, ThemeVariant::Fluent, ThemeVariant::Mdl2})
    {
        CHECK_FALSE(
            ui::detail::controls(resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{})).progress_fill.has_value());
    }
}

// Luna themes only `[Tooltip.Close]` and none of `WindowFrame`, `InfoBk`, `InfoText`,
// so a Luna tooltip is the classic square system box.
TEST_CASE("Luna's tooltip is the classic system tooltip, square and black-framed", "[theme][retro][luna]")
{
    const Theme luna = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    CHECK(luna.surfaces.tooltip == Color::from_argb(0xFFFFFFE1)); // COLOR_INFOBK
    CHECK(luna.surfaces.tooltip_text == colors::Black);           // COLOR_INFOTEXT
    REQUIRE(luna.surfaces.tooltip_border.has_value());
    CHECK(*luna.surfaces.tooltip_border == colors::Black); // COLOR_WINDOWFRAME
    // Not `field_border`, the edit control's themed `#7F9DB9`.
    CHECK(*luna.surfaces.tooltip_border != luna.surfaces.field_border);

    // Square, while the era's push button is not, so it needs its own token.
    CHECK(ui::detail::tooltip_radius(luna) == 0.0f);
    CHECK(luna.corner_radius == 3.0f);
    // `Padding = 1,1,3,1` inside a 1 px frame, against the default `9,6,9,8`.
    REQUIRE(ui::detail::controls(luna).tooltip_padding.has_value());
    CHECK(*ui::detail::controls(luna).tooltip_padding == Edges{.left = 2.0f, .top = 2.0f, .right = 4.0f, .bottom = 2.0f});

    // Every other theme but Metro (its own frame, tested with its radio ring) keeps the default.
    for (ThemeVariant v :
         {ThemeVariant::Fluent2,
          ThemeVariant::Fluent,
          ThemeVariant::Mdl2,
          ThemeVariant::Aero,
          ThemeVariant::Windows9x,
          ThemeVariant::Windows3x})
    {
        const Theme other = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        CHECK(ui::detail::tooltip_radius(other) == other.corner_radius);
        CHECK_FALSE(ui::detail::controls(other).tooltip_padding.has_value());
        CHECK_FALSE(other.surfaces.tooltip_border.has_value());
    }
}

// ---- Tokens every era must restate ------------------------------------------------------
// `Theme::fluent()` fills fields the era resolvers do not restate, so a token added for Fluent
// applies to every pack. Each check sweeps every variant.

namespace
{
constexpr ThemeVariant EveryVariant[] =
    {ThemeVariant::Fluent2,
     ThemeVariant::Fluent,
     ThemeVariant::Mdl2,
     ThemeVariant::Metro,
     ThemeVariant::Aero,
     ThemeVariant::Luna,
     ThemeVariant::Windows9x,
     ThemeVariant::Windows3x,
     ThemeVariant::Windows1x};

OsThemeSnapshot black_high_contrast()
{
    OsThemeSnapshot os;
    os.high_contrast         = true;
    os.hc.window_background  = colors::Black;
    os.hc.window_text        = colors::White;
    os.hc.control_background = colors::Black;
    os.hc.control_text       = colors::White;
    os.hc.control_border     = Color::from_argb(0xFF00FF00);
    os.hc.accent             = Color::from_argb(0xFF1AEBFF);
    os.hc.accent_text        = colors::Black;
    return os;
}
} // namespace

TEST_CASE("Every theme states a divider that reads against its own page", "[theme][regression]")
{
    // `SplitterControl::render()` draws in `colors.divider` without consulting `divider_visual`, and a
    // `Color` defaults to opaque black.
    for (ThemeVariant v : EveryVariant)
    {
        for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark})
        {
            CAPTURE(static_cast<int>(v), static_cast<int>(mode));
            const Theme theme = resolve_theme(ThemeRequest{.mode = mode, .variant = v}, OsThemeSnapshot{});
            CHECK(theme.colors().divider != theme.colors().background);
        }
    }
    // A black rule on a black HC page is not wrong, it is absent.
    const Theme hc = resolve_theme(ThemeRequest{}, black_high_contrast());
    CHECK(hc.colors().divider != hc.colors().background);
    CHECK(hc.colors().divider == black_high_contrast().hc.control_border);
    // Windows 9x takes the etched groove's dark half.
    const Theme win9x = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    CHECK(win9x.colors().divider == win9x.surfaces.separator_dark);
    CHECK(win9x.divider_visual == ThemeDividerVisual::EtchedGroove);
}

TEST_CASE("Only the Fluent eras draw NavigationShell's accent selection bar", "[theme][regression]")
{
    // Windows 9x has no control accent, so no sliding bar.
    for (ThemeVariant v : EveryVariant)
    {
        CAPTURE(static_cast<int>(v));
        const Theme theme  = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        const bool  fluent = v == ThemeVariant::Fluent2 || v == ThemeVariant::Fluent;
        CHECK(ui::detail::controls(theme).accent_selection_indicator == fluent);
    }
}

TEST_CASE("No era pack sits on Fluent Light's severity surfaces or progress track", "[theme][regression]")
{
    // `Theme::severity` and `progress_track` default to Fluent Light's literals. The hues may stay
    // on the fallback where an era states none; the surfaces may not.
    const Theme defaults{};
    for (ThemeVariant v : EveryVariant)
    {
        if (v == ThemeVariant::Fluent2 || v == ThemeVariant::Fluent)
        {
            continue;
        }
        for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark})
        {
            CAPTURE(static_cast<int>(v), static_cast<int>(mode));
            const Theme t = resolve_theme(ThemeRequest{.mode = mode, .variant = v}, OsThemeSnapshot{});
            CHECK(t.severity.informational.background != defaults.severity.informational.background);
            CHECK(t.severity.success.background != defaults.severity.success.background);
            CHECK(t.severity.caution.background != defaults.severity.caution.background);
            CHECK(t.severity.critical.background != defaults.severity.critical.background);
            CHECK(t.severity.stroke != defaults.severity.stroke);
            CHECK(ui::detail::controls(t).progress_track != ui::detail::controls(defaults).progress_track);
        }
    }
}

TEST_CASE("Aero's severities are the era's stated color meanings", "[theme][retro][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    // Vista's rule: red error, yellow warning, green normal, blue information.
    // The Aero style's `PP_FILL` hints give the three progress states.
    CHECK(theme.severity.success.fill == Color::from_argb(0xFF52D763));
    CHECK(theme.severity.critical.fill == Color::from_argb(0xFFE40021));
    CHECK(theme.severity.caution.fill == Color::from_argb(0xFFDCD400));
    CHECK(theme.severity.informational.fill == theme.colors().link);
    CHECK(ui::detail::controls(theme).progress_fill == Color::from_argb(0xFF52D763));
    CHECK(ui::detail::controls(theme).progress_track == Color::from_argb(0xFFBABABA)); // the ends of Aero's track gradient
    // Signs, not surfaces.
    CHECK(theme.severity.critical.background == theme.colors().field);
    CHECK(theme.severity.stroke == theme.surfaces.field_border);
}

TEST_CASE("Metro gives every severity one app color, and its progress bar the era's own brushes",
          "[theme][retro][metro]")
{
    // Windows 8: error and warning bars use one color from the app's palette.
    for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark})
    {
        CAPTURE(static_cast<int>(mode));
        const bool dark = mode == ThemeMode::Dark;
        const Theme theme = resolve_theme(ThemeRequest{.mode = mode, .variant = ThemeVariant::Metro}, OsThemeSnapshot{});
        const Color accent = theme.accent_button_normal.background;
        CHECK(theme.severity.informational.fill == accent);
        CHECK(theme.severity.success == theme.severity.informational);
        CHECK(theme.severity.caution == theme.severity.informational);
        CHECK(theme.severity.critical == theme.severity.informational);
        CHECK(theme.severity.critical.background == theme.colors().background);
        // The track.
        CHECK(ui::detail::controls(theme).progress_track ==
              (dark ? Color::from_argb(0x59FFFFFF) : Color::from_argb(0x30000000)));
        // The fill is the accent literal in light and a lighter violet in dark.
        CHECK(ui::detail::controls(theme).progress_fill == (dark ? theme.accent_button_hover.background : accent));
    }
}

TEST_CASE("MDL2's progress track is text_20, and its severity bars sit on the page", "[theme][retro][mdl2]")
{
    for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark})
    {
        CAPTURE(static_cast<int>(mode));
        const bool  dark  = mode == ThemeMode::Dark;
        const Theme theme = resolve_theme(ThemeRequest{.mode = mode, .variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});
        // text_20, 20 % of the foreground.
        CHECK(ui::detail::controls(theme).progress_track ==
              (dark ? Color::from_argb(0x33FFFFFF) : Color::from_argb(0x33000000)));
        CHECK_FALSE(ui::detail::controls(theme).progress_fill.has_value()); // the accent
        CHECK(theme.severity.caution.background == theme.colors().background);
        CHECK(theme.severity.stroke == theme.surfaces.field_border);
    }
}

TEST_CASE("Windows1x draws every severity in ink, because it has no color to draw one in", "[theme][retro][winold]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows1x}, OsThemeSnapshot{});
    // The era's five `OIC_*` icons are 1 bpp.
    CHECK(theme.severity.critical.fill == theme.colors().text);
    CHECK(theme.severity.success == theme.severity.critical);
    CHECK(theme.severity.caution == theme.severity.critical);
    CHECK(theme.severity.informational == theme.severity.critical);
    CHECK(theme.severity.text_on_fill == theme.colors().background);
    CHECK(theme.severity.critical.background == theme.colors().background);
    CHECK(theme.severity.stroke == colors::Black); // WINDOWFRAME
}

TEST_CASE("Only the 9x-lineage classic pack engraves a disabled label", "[theme][retro]")
{
    // `DrawState(DSS_DISABLED)` engraves a disabled static like a push-button caption. Windows 3.x
    // has no `DrawState`; nothing after Windows 2000 engraves.
    for (ThemeVariant v : EveryVariant)
    {
        CAPTURE(static_cast<int>(v));
        const Theme theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        if (v == ThemeVariant::Windows9x)
        {
            CHECK(theme.disabled_text_effect.kind == TextEffectKind::Emboss);
            CHECK(theme.disabled_text_effect == theme.button_disabled.text_effect);
        }
        else
        {
            CHECK(theme.disabled_text_effect.kind == TextEffectKind::None);
        }
    }
}

// ---- One token set per toggle control ----------------------------------------------------

TEST_CASE("set_toggle_visuals states one set for all three toggles, and each can then differ", "[theme]")
{
    Theme              theme = Theme::fluent(Palette::fluent_light());
    const ButtonVisual mark{colors::White, colors::Black, Color::from_argb(0xFFFF0000)};
    ToggleVisuals      set;
    set.normal.on = mark;
    ui::detail::edit_controls(theme).set_toggle_visuals(set);
    CHECK(ui::detail::controls(theme).check_box_box == set);
    CHECK(ui::detail::controls(theme).radio_ring == set);
    CHECK(ui::detail::controls(theme).toggle_track == set);

    // for_state() picks the state, then the half.
    CHECK(ui::detail::controls(theme).radio_ring.for_state(ButtonState::Normal, true) == mark);
    CHECK(ui::detail::controls(theme).radio_ring.for_state(ButtonState::Hover, true) == set.hover.on);
    CHECK(ui::detail::controls(theme).radio_ring.for_state(ButtonState::Disabled, false) == set.disabled.off);

    ui::detail::edit_controls(theme).radio_ring.normal.on.text = colors::Black;
    CHECK(ui::detail::controls(theme).check_box_box.normal.on.text == Color::from_argb(0xFFFF0000)); // independent sets
    CHECK(ui::detail::edit_controls(theme).toggles().size() == 3);
    CHECK(ui::detail::edit_controls(theme).toggle_track.all().size() == 8);
}

TEST_CASE("The toggle controls genuinely differ where their eras say they do", "[theme][retro]")
{
    // Fluent 2: the radio's dot stays the on-accent text color when pressed and disabled.
    const Theme fluent2 = Theme::fluent(Palette::fluent_dark());
    CHECK(ui::detail::controls(fluent2).radio_ring.pressed.on.text == Palette::fluent_dark().text_on_accent_primary);
    CHECK(ui::detail::controls(fluent2).radio_ring.disabled.on.text == Palette::fluent_dark().text_on_accent_primary);
    CHECK(ui::detail::controls(fluent2).radio_ring.pressed.on.background ==
          ui::detail::controls(fluent2).check_box_box.pressed.on.background);

    // Metro 8.1: the check box and ring keep their near-white well and only the mark appears; the
    // ToggleSwitch curtain turns violet.
    const Theme metro = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = ThemeVariant::Metro},
                                      OsThemeSnapshot{});
    CHECK(ui::detail::controls(metro).check_box_box.normal.on.background ==
          ui::detail::controls(metro).check_box_box.normal.off.background);
    CHECK(ui::detail::controls(metro).check_box_box.normal.on.text == colors::Black);
    CHECK(ui::detail::controls(metro).radio_ring == ui::detail::controls(metro).check_box_box);
    CHECK(ui::detail::controls(metro).toggle_track.normal.on.background == metro.accent_button_normal.background);

    // MDL2: the checked ring is an accent outline around a hollow, never filled.
    const Theme mdl2 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});
    for (ButtonState s : {ButtonState::Normal, ButtonState::Hover, ButtonState::Pressed, ButtonState::Disabled})
    {
        CAPTURE(static_cast<int>(s));
        CHECK(ui::detail::controls(mdl2).radio_ring.for_state(s, true).background == colors::Transparent);
        CHECK(ui::detail::controls(mdl2).radio_ring.for_state(s, false).background == colors::Transparent);
    }
    CHECK(ui::detail::controls(mdl2).check_box_box.normal.on.background == mdl2.accent_button_normal.background);
}

// ---- The indeterminate check box, and the radio dot's lift ------------------------------------

TEST_CASE("Every pack states its own indeterminate check box", "[theme][retro]")
{
    // Left at `Theme::fluent()`'s, a pack draws Windows 11's accent box and dash, so every variant
    // is checked.
    for (ThemeVariant v : EveryVariant)
    {
        for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark})
        {
            CAPTURE(static_cast<int>(v), static_cast<int>(mode));
            const Theme theme   = resolve_theme(ThemeRequest{.mode = mode, .variant = v}, OsThemeSnapshot{});
            const Theme fluent2 = resolve_theme(ThemeRequest{.mode = mode, .variant = ThemeVariant::Fluent2},
                                                OsThemeSnapshot{});
            if (v == ThemeVariant::Fluent2)
            {
                CHECK(ui::detail::controls(theme).check_box_indeterminate ==
                      IndeterminateVisuals::like_checked(ui::detail::controls(theme).check_box_box, IndeterminateMark::Glyph));
            }
            else
            {
                CHECK(ui::detail::controls(theme).check_box_indeterminate !=
                      ui::detail::controls(fluent2).check_box_indeterminate);
            }
        }
    }

    const Theme hc = resolve_theme(ThemeRequest{}, black_high_contrast());
    CHECK(ui::detail::controls(hc).check_box_indeterminate ==
          IndeterminateVisuals::like_checked(ui::detail::controls(hc).check_box_box, IndeterminateMark::Glyph));
}

TEST_CASE("Each era marks the mixed value its own way", "[theme][retro]")
{
    const OsThemeSnapshot os;
    auto                  theme_of = [&](ThemeVariant v, ThemeMode mode = ThemeMode::Light)
    { return resolve_theme(ThemeRequest{.mode = mode, .variant = v}, os); };

    // Windows 10 Fluent: the checked ladder, with the MDL2 glyph.
    const Theme fluent1 = theme_of(ThemeVariant::Fluent);
    CHECK(ui::detail::controls(fluent1).check_box_indeterminate ==
          IndeterminateVisuals::like_checked(ui::detail::controls(fluent1).check_box_box,
                                             IndeterminateMark::Glyph,
                                             glyphs::CheckBoxIndeterminateMdl2));

    // MDL2 10240: a hollow accent outline around a foreground-gray square glyph.
    const Theme mdl2 = theme_of(ThemeVariant::Mdl2);
    CHECK(ui::detail::controls(mdl2).check_box_indeterminate.kind == IndeterminateMark::Glyph);
    CHECK(ui::detail::controls(mdl2).check_box_indeterminate.glyph.codepoint == 0xE73C);
    CHECK(ui::detail::controls(mdl2).check_box_indeterminate.normal.box.background == colors::Transparent);
    CHECK(ui::detail::controls(mdl2).check_box_indeterminate.normal.box.border ==
          ui::detail::controls(mdl2).check_box_box.normal.on.background); // the accent
    CHECK(ui::detail::controls(mdl2).check_box_indeterminate.normal.box.text !=
          ui::detail::controls(mdl2).check_box_box.normal.on.text); // not white

    // Windows 8.x: a 9x9 square in the tick's color, on the same well.
    for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark})
    {
        const Theme metro = theme_of(ThemeVariant::Metro, mode);
        CHECK(ui::detail::controls(metro).check_box_indeterminate.kind == IndeterminateMark::Square);
        CHECK(ui::detail::controls(metro).check_box_indeterminate.square_size == 9.0f);
        CHECK(ui::detail::controls(metro).check_box_indeterminate.normal.box ==
              ui::detail::controls(metro).check_box_box.normal.on);
    }

    // Luna: a 7x7 green square, flat full-strength when hot.
    const Theme luna = theme_of(ThemeVariant::Luna);
    CHECK(ui::detail::controls(luna).check_box_indeterminate.kind == IndeterminateMark::Square);
    CHECK(ui::detail::controls(luna).check_box_indeterminate.square_size == 7.0f);
    REQUIRE(ui::detail::controls(luna).check_box_indeterminate.normal.mark);
    REQUIRE(ui::detail::controls(luna).check_box_indeterminate.normal.mark->background_gradient);
    CHECK(ui::detail::controls(luna).check_box_indeterminate.normal.mark->background_gradient->stops.front().color ==
          Color::from_argb(0xFF6DBB6C));
    REQUIRE(ui::detail::controls(luna).check_box_indeterminate.hover.mark);
    CHECK(ui::detail::controls(luna).check_box_indeterminate.hover.mark->background == Color::from_argb(0xFF21A121));

    // Aero: a 9x9 blue square with its own inner border and highlight ring.
    const Theme aero = theme_of(ThemeVariant::Aero);
    CHECK(ui::detail::controls(aero).check_box_indeterminate.kind == IndeterminateMark::Square);
    CHECK(ui::detail::controls(aero).check_box_indeterminate.square_size == 9.0f);
    REQUIRE(ui::detail::controls(aero).check_box_indeterminate.normal.mark);
    CHECK(ui::detail::controls(aero).check_box_indeterminate.normal.mark->border_gradient.has_value());
    CHECK(ui::detail::controls(aero).check_box_indeterminate.normal.mark->inner_border.has_value());
    CHECK(ui::detail::controls(aero).check_box_indeterminate.normal.box.background ==
          ui::detail::controls(aero).check_box_box.normal.off.background);

    // Windows 9x: the tick in BTNSHADOW over a face/highlight dither; pressed drops the dither.
    const Theme w9x = theme_of(ThemeVariant::Windows9x);
    CHECK(ui::detail::controls(w9x).check_box_indeterminate.kind == IndeterminateMark::Glyph);
    CHECK(ui::detail::controls(w9x).check_box_indeterminate.glyph == ui::detail::controls(w9x).check_glyph);
    CHECK(ui::detail::controls(w9x).check_box_indeterminate.normal.box.text == Color::from_argb(0xFF808080));
    CHECK(ui::detail::controls(w9x).check_box_indeterminate.normal.box.border_style == BorderStyle::Sunken);
    CHECK(ui::detail::controls(w9x).check_box_indeterminate.normal.dither == colors::White);
    CHECK(ui::detail::controls(w9x).check_box_indeterminate.normal.box.background != colors::White); // the face under it
    CHECK_FALSE(ui::detail::controls(w9x).check_box_indeterminate.pressed.dither.has_value());

    // Windows 1.x-3.x: no mark at all, the gray pattern is the state.
    for (ThemeVariant v : {ThemeVariant::Windows3x, ThemeVariant::Windows1x})
    {
        CAPTURE(static_cast<int>(v));
        const Theme theme = theme_of(v);
        CHECK(ui::detail::controls(theme).check_box_indeterminate.kind == IndeterminateMark::None);
        CHECK(ui::detail::controls(theme).check_box_indeterminate.normal.dither ==
              ui::detail::controls(theme).check_box_box.normal.on.text);
        CHECK(ui::detail::controls(theme).check_box_indeterminate.normal.box.background ==
              ui::detail::controls(theme).check_box_box.normal.on.background);
        CHECK(ui::detail::controls(theme).check_box_indeterminate.disabled.dither ==
              ui::detail::controls(theme).check_box_box.disabled.on.text);
    }
}

TEST_CASE("Only Fluent 2 rims its checked radio dot with the circle elevation border", "[theme]")
{
    for (ThemeVariant v : EveryVariant)
    {
        for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark})
        {
            CAPTURE(static_cast<int>(v), static_cast<int>(mode));
            Theme theme = resolve_theme(ThemeRequest{.mode = mode, .variant = v}, OsThemeSnapshot{});
            for (ToggleVisuals* tv :
                 {&ui::detail::edit_controls(theme).check_box_box, &ui::detail::edit_controls(theme).toggle_track})
            {
                for (ButtonVisual* half : tv->all())
                {
                    CHECK_FALSE(half->mark_border_gradient.has_value());
                }
            }
            for (ButtonState s : {ButtonState::Normal, ButtonState::Hover, ButtonState::Pressed, ButtonState::Disabled})
            {
                CHECK_FALSE(ui::detail::controls(theme).radio_ring.for_state(s, false).mark_border_gradient.has_value());
                CHECK(ui::detail::controls(theme).radio_ring.for_state(s, true).mark_border_gradient.has_value() ==
                      (v == ThemeVariant::Fluent2));
            }
        }
    }
    Theme hc = resolve_theme(ThemeRequest{}, black_high_contrast());
    for (ButtonVisual* half : ui::detail::edit_controls(hc).radio_ring.all())
    {
        CHECK_FALSE(half->mark_border_gradient.has_value()); // the dot's stroke is transparent in HC
    }

    // Stops 0.5 Default / 0.7 Secondary in light and dark; no flip.
    for (const Palette& palette : {Palette::fluent_light(), Palette::fluent_dark()})
    {
        const Theme theme = Theme::fluent(palette);
        REQUIRE(ui::detail::controls(theme).radio_ring.normal.on.mark_border_gradient);
        const auto& stops = ui::detail::controls(theme).radio_ring.normal.on.mark_border_gradient->stops;
        REQUIRE(stops.size() == 2);
        CHECK(stops[0] == GradientStop{0.5f, palette.control_stroke_default});
        CHECK(stops[1] == GradientStop{0.7f, palette.control_stroke_secondary});
    }
}

TEST_CASE("A resolved Fluent theme carries the accent ramp it was resolved from", "[theme]")
{
    OsThemeSnapshot os;
    os.accent_ramp.light2 = Color::from_argb(0xFF99CCFF);
    os.accent_ramp.dark2  = Color::from_argb(0xFF003366);
    const Theme fluent2   = resolve_theme(ThemeRequest{}, os);
    CHECK(fluent2.accent_ramp == os.accent_ramp);

    ThemeRequest fluent1_request;
    fluent1_request.variant = ThemeVariant::Fluent;
    CHECK(resolve_theme(fluent1_request, os).accent_ramp == os.accent_ramp);

    // A default Theme (not resolved from any snapshot) has the shipped Windows-default ramp.
    CHECK(Theme::light().accent_ramp == AccentRamp{});
}

// `Theme::popup_motion`, per era.
TEST_CASE("Popup motion follows each era, and USER32's switches under Aero and Luna", "[theme][motion]")
{
    using PopupAnimation = ThemePopupAnimation;
    auto resolve         = [](ThemeVariant v, OsThemeSnapshot os = {}, ThemeMode mode = ThemeMode::Light)
    { return resolve_theme(ThemeRequest{.mode = mode, .variant = v}, os).popup_motion; };

    // The menu's popup transition, and a list that simply appears.
    for (ThemeVariant v : {ThemeVariant::Fluent2, ThemeVariant::Fluent, ThemeVariant::Mdl2, ThemeVariant::Metro})
    {
        const Theme::PopupMotion m = resolve(v);
        CHECK(m.menu == PopupAnimation::Slide);
        CHECK(m.list == PopupAnimation::None);
        CHECK(m.open == std::chrono::milliseconds{250});
        CHECK(m.close == std::chrono::milliseconds{83});
    }

    // No era source dates or times the classic menu effects.
    for (ThemeVariant v : {ThemeVariant::Windows9x, ThemeVariant::Windows3x, ThemeVariant::Windows1x})
    {
        const Theme::PopupMotion m = resolve(v);
        CHECK(m.menu == PopupAnimation::None);
        CHECK(m.list == PopupAnimation::None);
    }

    for (ThemeVariant v : {ThemeVariant::Aero, ThemeVariant::Luna})
    {
        CAPTURE(static_cast<int>(v));
        // Windows' defaults: menus fade, the combo slides, ~200 ms, gone at once on close.
        const Theme::PopupMotion m = resolve(v);
        CHECK(m.menu == PopupAnimation::Fade);
        CHECK(m.list == PopupAnimation::Slide);
        CHECK(m.open == std::chrono::milliseconds{200});
        CHECK(m.close == std::chrono::milliseconds{0});

        OsThemeSnapshot slide;
        slide.menu_fade = false;
        CHECK(resolve(v, slide).menu == PopupAnimation::Slide);

        OsThemeSnapshot off;
        off.menu_animation      = false;
        off.combo_box_animation = false;
        CHECK(resolve(v, off).menu == PopupAnimation::None);
        CHECK(resolve(v, off).list == PopupAnimation::None);

        // The switches are system settings a contrast theme leaves alone.
        CHECK(resolve(v, {}, ThemeMode::HighContrast).menu == PopupAnimation::Fade);
    }
}

TEST_CASE("Windows 9x, Windows 3.x and MDL2 popups cast no shadow", "[theme][retro]")
{
    for (ThemeVariant v : {ThemeVariant::Windows9x, ThemeVariant::Windows3x, ThemeVariant::Mdl2})
    {
        CHECK(resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{}).popup_shadow == ThemePopupShadow::None);
    }
}

TEST_CASE("Aero and Luna keep the CS_DROPSHADOW popup shadow under high contrast", "[theme][retro]")
{
    for (ThemeVariant v : {ThemeVariant::Aero, ThemeVariant::Luna})
    {
        CHECK(resolve_hc(v, distinct_hc_palette()).popup_shadow == ThemePopupShadow::AeroDropShadow);
    }
}

TEST_CASE("An MDL2 dialog dims the window with the page_60 rung", "[theme][mdl2]")
{
    ThemeRequest request;
    request.variant = ThemeVariant::Mdl2;
    request.mode    = ThemeMode::Light;
    CHECK(resolve_theme(request, OsThemeSnapshot{}).colors().scrim == Mdl2Palette::mdl2_light().page_60);
    request.mode = ThemeMode::Dark;
    CHECK(resolve_theme(request, OsThemeSnapshot{}).colors().scrim == Mdl2Palette::mdl2_dark().page_60);
}

TEST_CASE("Metro's radio ring is 23 px and its tooltip has the era's frame and padding", "[theme][metro]")
{
    const Theme metro = resolve_theme(ThemeRequest{.variant = ThemeVariant::Metro}, OsThemeSnapshot{});
    CHECK(ui::detail::controls(metro).toggle_box_size == 21.0f);
    CHECK(ui::detail::controls(metro).resolved_radio_box_size() == 23.0f);
    CHECK(metro.surfaces.tooltip_border == Color::from_argb(0xFF808080));
    CHECK(ui::detail::controls(metro).tooltip_padding == Edges{.left = 12.0f, .top = 8.0f, .right = 12.0f, .bottom = 9.0f});

    const HighContrastPalette hc = distinct_hc_palette();
    CHECK(resolve_hc(ThemeVariant::Metro, hc).surfaces.tooltip_border == hc.window_text);
    // Every other theme's radio ring follows the check box.
    const Theme fluent = resolve_theme(ThemeRequest{}, OsThemeSnapshot{});
    CHECK(ui::detail::controls(fluent).resolved_radio_box_size() == ui::detail::controls(fluent).toggle_box_size);
}

TEST_CASE("Luna's slider ticks are the trackbar's own gray, not the groove's", "[theme][luna]")
{
    const Theme luna = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});
    REQUIRE(ui::detail::controls(luna).slider_chrome);
    CHECK(ui::detail::controls(luna).slider_chrome->tick == Color::from_argb(0xFFA1A192));
}

TEST_CASE("Fluent 1 fades a hovered selected row to 0.6 in both modes", "[theme][fluent1]")
{
    for (ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark})
    {
        ThemeRequest request;
        request.variant   = ThemeVariant::Fluent;
        request.mode      = mode;
        const Theme theme = resolve_theme(request, OsThemeSnapshot{});
        REQUIRE(ui::detail::controls(theme).row_selection_fills);
        CHECK(ui::detail::controls(theme).row_selection_fills->hover.a == Catch::Approx(0.6f));
    }
}

TEST_CASE("Fluent 1 draws ClearType text, Fluent 2 grayscale", "[theme]")
{
    CHECK(resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{}).rasterization.text ==
          TextAA::Subpixel);
    CHECK(resolve_hc(ThemeVariant::Fluent, distinct_hc_palette()).rasterization.text == TextAA::Subpixel);
    CHECK(resolve_theme(ThemeRequest{}, OsThemeSnapshot{}).rasterization.text == TextAA::Grayscale);
}

TEST_CASE("Fluent 2 keeps its corner radii under high contrast", "[theme]")
{
    const Theme hc = resolve_hc(ThemeVariant::Fluent2, distinct_hc_palette());
    CHECK(hc.corner_radius == 4.0f);
    CHECK(hc.overlay_corner_radius == 8.0f);
}

TEST_CASE("an OS theme change re-resolves the window's theme unless the app pinned one", "[theme][window]")
{
    Window window{{.theme = ThemeRequest{.mode = ThemeMode::System}}};
    seams(window).report_os_theme(OsThemeSnapshot{.dark = false});
    CHECK(window.theme().colors().background == Palette::fluent_light().solid_background_base);

    seams(window).report_os_theme(OsThemeSnapshot{.dark = true});
    CHECK(window.theme().colors().background == Palette::fluent_dark().solid_background_base);

    SECTION("pin_theme() pins")
    {
        window.pin_theme(Theme::light());
        const Color pinned = window.theme().colors().background;
        CHECK(window.theme_pinned());
        seams(window).report_os_theme(OsThemeSnapshot{.dark = true}); // a settings broadcast
        CHECK(window.theme().colors().background == pinned);
    }
    SECTION("set_theme() unpins and resolves against the last OS state")
    {
        window.pin_theme(Theme::light());
        window.set_theme({.mode = ThemeMode::System});
        CHECK_FALSE(window.theme_pinned());
        CHECK(window.theme().colors().background == Palette::fluent_dark().solid_background_base);
    }
    SECTION("an explicit mode ignores the OS app mode")
    {
        window.set_theme({.mode = ThemeMode::Light});
        seams(window).report_os_theme(OsThemeSnapshot{.dark = true});
        CHECK(window.theme().colors().background == Palette::fluent_light().solid_background_base);
    }
}

TEST_CASE("theme copies share their control chrome until one is edited", "[theme]")
{
    const Theme original = Theme::light();
    Theme       copy     = original;
    CHECK(&ui::detail::controls(copy) == &ui::detail::controls(original));

    ui::detail::edit_controls(copy).toggle_box_size = 30.0f;
    CHECK(ui::detail::controls(copy).toggle_box_size == 30.0f);
    CHECK(ui::detail::controls(original).toggle_box_size == 20.0f);
    CHECK(&ui::detail::controls(copy) != &ui::detail::controls(original));

    // Once it owns its record alone, another edit writes in place.
    const ui::detail::ControlChrome* own            = &ui::detail::controls(copy);
    ui::detail::edit_controls(copy).toggle_box_size = 31.0f;
    CHECK(&ui::detail::controls(copy) == own);
    CHECK(ui::detail::controls(original).toggle_box_size == 20.0f);
}

TEST_CASE("a default-constructed theme reads default control chrome", "[theme]")
{
    const Theme blank{};
    CHECK(ui::detail::controls(blank).toggle_box_size == 20.0f);
    CHECK_FALSE(ui::detail::controls(blank).dropdown_chrome.has_value());

    Theme edited                                      = blank;
    ui::detail::edit_controls(edited).toggle_box_size = 5.0f;
    CHECK(ui::detail::controls(edited).toggle_box_size == 5.0f);
    CHECK(ui::detail::controls(blank).toggle_box_size == 20.0f);
}

TEST_CASE("every pack and resolver states the variant it draws", "[theme]")
{
    CHECK(Theme::light().variant == ThemeVariant::Fluent2);
    CHECK(Theme::classic(ClassicPalette::windows_standard(), /*hot_track=*/false, "MS Sans Serif").variant ==
          ThemeVariant::Windows9x);
    CHECK(Theme::classic(ClassicPalette::windows_standard(), /*hot_track=*/false, "MS Sans Serif", /*win3x_edge=*/true).variant ==
          ThemeVariant::Windows3x);
    CHECK(Theme::metro(MetroPalette::metro_light(), /*dark=*/false).variant == ThemeVariant::Metro);
    CHECK(Theme::mdl2(Mdl2Palette::mdl2_light(), /*dark=*/false, colors::Black).variant == ThemeVariant::Mdl2);
    CHECK(Theme::early(EarlyPalette::windows_2x_color()).variant == ThemeVariant::Windows1x);
    CHECK_FALSE(Theme::light().high_contrast);

    for (ThemeVariant v :
         {ThemeVariant::Fluent2,
          ThemeVariant::Fluent,
          ThemeVariant::Mdl2,
          ThemeVariant::Metro,
          ThemeVariant::Aero,
          ThemeVariant::Luna,
          ThemeVariant::Windows9x,
          ThemeVariant::Windows3x,
          ThemeVariant::Windows1x})
    {
        CAPTURE(static_cast<int>(v));
        const Theme plain = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        CHECK(plain.variant == v);
        CHECK_FALSE(plain.high_contrast);
        if (plain.with_dark_mode)
        {
            CHECK(plain.with_dark_mode(true).variant == v);
        }
        const Theme contrast = resolve_theme(ThemeRequest{.mode = ThemeMode::HighContrast, .variant = v}, OsThemeSnapshot{});
        CHECK(contrast.variant == v);
        CHECK(contrast.high_contrast);
    }
}

// ---- Theme::dark, generation, and the window's theme changes ----

namespace
{
// Records whether the theme it was drawn with is dark, and a few of its tokens.
class DarkProbe final : public Element
{
public:
    mutable bool  drawn_dark          = false;
    mutable float drawn_corner_radius = -1.0f;
    mutable float drawn_ring_width    = -1.0f;
    mutable float drawn_body_size     = -1.0f;
    mutable Color drawn_background{};

protected:
    Size measure_override(Size, const MeasureContext&) const override
    {
        return {10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext& context) const override
    {
        const Theme& theme  = context.theme;
        drawn_dark          = theme.dark;
        drawn_corner_radius = theme.corner_radius;
        drawn_ring_width    = theme.focus_ring_outer_width;
        drawn_body_size     = theme.type.body.font_size;
        drawn_background    = theme.colors().background;
    }
};
} // namespace

TEST_CASE("Theme::dark is set by every variant that has a dark mode", "[theme][dark]")
{
    for (const ThemeVariant variant : {ThemeVariant::Fluent2, ThemeVariant::Fluent, ThemeVariant::Mdl2, ThemeVariant::Metro})
    {
        CAPTURE(static_cast<int>(variant));
        CHECK(resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = variant}, OsThemeSnapshot{}).dark);
        CHECK_FALSE(resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = variant}, OsThemeSnapshot{}).dark);
    }
    // The fixed-palette eras are light whatever the mode asks.
    for (const ThemeVariant variant :
         {ThemeVariant::Aero, ThemeVariant::Luna, ThemeVariant::Windows9x, ThemeVariant::Windows3x, ThemeVariant::Windows1x})
    {
        CAPTURE(static_cast<int>(variant));
        CHECK_FALSE(resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = variant}, OsThemeSnapshot{}).dark);
    }
    CHECK(Theme::metro(MetroPalette::metro_dark(), true).dark);
    CHECK(Theme::mdl2(Mdl2Palette::mdl2_dark(), true, colors::White).dark);
    CHECK_FALSE(Theme::light().dark);
}

TEST_CASE("A contrast theme is dark by its window color", "[theme][dark]")
{
    OsThemeSnapshot black;
    black.high_contrast = true;
    black.hc            = HighContrastPalette::windows10_high_contrast_black();
    OsThemeSnapshot white;
    white.high_contrast = true; // the default palette is a white page
    for (const ThemeVariant variant : {ThemeVariant::Fluent2, ThemeVariant::Mdl2, ThemeVariant::Metro, ThemeVariant::Luna})
    {
        CAPTURE(static_cast<int>(variant));
        CHECK(resolve_theme(ThemeRequest{.variant = variant}, black).dark);
        CHECK_FALSE(resolve_theme(ThemeRequest{.variant = variant}, white).dark);
    }
}

TEST_CASE("A light requested_theme under dark Mdl2 and Metro draws light", "[theme][dark]")
{
    for (const ThemeVariant variant : {ThemeVariant::Mdl2, ThemeVariant::Metro, ThemeVariant::Fluent2})
    {
        CAPTURE(static_cast<int>(variant));
        const Theme  dark = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = variant}, OsThemeSnapshot{});
        DarkProbe    probe;
        TestRenderer renderer;
        probe.render(renderer, dark);
        CHECK(probe.drawn_dark);
        probe.set_requested_theme(ElementTheme::Light);
        probe.render(renderer, dark);
        CHECK_FALSE(probe.drawn_dark);
    }
}

TEST_CASE("A requested_theme subtree keeps the edited tokens that are not colors", "[theme][dark]")
{
    const OsThemeSnapshot os;
    const Theme           dark   = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark}, os);
    Theme                 light  = resolve_theme(ThemeRequest{.mode = ThemeMode::Light}, os);
    light.corner_radius          = 0.0f;
    light.focus_ring_outer_width = 5.0f;
    light.type.body.font_size    = 20.0f;
    light = light.with_content_colors(light.colors().text, Color::from_argb(0xFFFF0000)); // a color edit belongs to the light mode

    DarkProbe probe;
    probe.set_requested_theme(ElementTheme::Dark);
    TestRenderer renderer;
    probe.render(renderer, light);
    CHECK(probe.drawn_dark);
    CHECK(probe.drawn_corner_radius == 0.0f);
    CHECK(probe.drawn_ring_width == 5.0f);
    CHECK(probe.drawn_body_size == 20.0f);
    CHECK(probe.drawn_background == dark.colors().background);

    // Unedited, the subtree draws the other mode exactly as resolved.
    DarkProbe plain;
    plain.set_requested_theme(ElementTheme::Dark);
    plain.render(renderer, resolve_theme(ThemeRequest{.mode = ThemeMode::Light}, os));
    CHECK(plain.drawn_corner_radius == dark.corner_radius);
    CHECK(plain.drawn_ring_width == dark.focus_ring_outer_width);
    CHECK(plain.drawn_body_size == dark.type.body.font_size);
}

TEST_CASE("Every Theme value has a generation of its own", "[theme][generation]")
{
    const Theme resolved = resolve_theme(ThemeRequest{}, OsThemeSnapshot{});
    Theme       edited   = resolved;
    CHECK(resolved.generation() != 0);
    CHECK(edited.generation() != resolved.generation());

    Theme      assigned = Theme::light();
    const auto before   = assigned.generation();
    assigned            = edited;
    CHECK(assigned.generation() != before);
    CHECK(assigned.generation() != edited.generation());

    const auto  moved_from = edited.generation();
    const Theme moved      = std::move(edited);
    CHECK(moved.generation() != moved_from);
}

TEST_CASE("Window::set_theme marks the whole tree for layout and redraw", "[theme][window]")
{
    HeadlessUI ui     = create_headless();
    auto       button = ui.button("Deep");
    Element*   leaf   = button.get();
    ui.window().set_content(std::move(button));
    ui.layout({400, 300});
    REQUIRE_FALSE(leaf->is_dirty());

    Theme theme                  = ui.window().theme();
    theme.focus_ring_outer_width = 3.0f;
    ui.window().pin_theme(std::move(theme));
    CHECK(leaf->is_dirty(DirtyFlags::Layout));
    CHECK(leaf->is_dirty(DirtyFlags::Visual));
    CHECK(ui.window().content()->is_dirty(DirtyFlags::Layout));
}

TEST_CASE("A window's explicit theme mode applies before any host reports the OS", "[theme][window]")
{
    Window dark{{.theme = ThemeRequest{.mode = ThemeMode::Dark}}};
    CHECK(dark.theme().dark);
    Window mdl2{{.theme = ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Mdl2}}};
    CHECK(mdl2.theme().variant == ThemeVariant::Mdl2);
    CHECK(mdl2.theme().dark);
    Window plain{{}};
    CHECK_FALSE(plain.theme().dark);
}

// ---- in-app acrylic on the overlays ----

TEST_CASE("Fluent 2 fills its overlays with in-app acrylic while the OS allows transparency", "[theme][acrylic]")
{
    OsThemeSnapshot os;
    const Theme     light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light}, os);
    REQUIRE(light.overlay_material.has_value());
    CHECK(*light.overlay_material == Palette::fluent_light().acrylic_in_app_default());

    const Theme dark = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark}, os);
    REQUIRE(dark.overlay_material.has_value());
    CHECK(*dark.overlay_material == Palette::fluent_dark().acrylic_in_app_default());

    // requested_theme re-resolves the same request, the material included.
    REQUIRE(light.with_dark_mode);
    CHECK(light.with_dark_mode(true).overlay_material == dark.overlay_material);
}

TEST_CASE("Overlays stay solid without transparency, under high contrast and in every other era", "[theme][acrylic]")
{
    OsThemeSnapshot off;
    off.transparency_enabled = false;
    CHECK_FALSE(resolve_theme(ThemeRequest{}, off).overlay_material);

    CHECK_FALSE(resolve_theme(ThemeRequest{.mode = ThemeMode::HighContrast}, OsThemeSnapshot{}).overlay_material);
    for (const ThemeVariant variant :
         {ThemeVariant::Fluent,
          ThemeVariant::Mdl2,
          ThemeVariant::Metro,
          ThemeVariant::Aero,
          ThemeVariant::Luna,
          ThemeVariant::Windows9x,
          ThemeVariant::Windows3x,
          ThemeVariant::Windows1x})
    {
        CHECK_FALSE(resolve_theme(ThemeRequest{.variant = variant}, OsThemeSnapshot{}).overlay_material);
    }
}

TEST_CASE("fill_overlay_surface draws the material over the surface's color, or the color alone", "[theme][acrylic]")
{
    Theme      theme = resolve_theme(ThemeRequest{.mode = ThemeMode::Light}, OsThemeSnapshot{});
    const Rect rect{10.0f, 10.0f, 100.0f, 50.0f};
    {
        testing::TestRenderer renderer;
        fill_overlay_surface(renderer, theme, rect, 8.0f, theme.surfaces.menu);
        REQUIRE(renderer.commands.size() == 1);
        const auto* geometry = std::get_if<DrawGeometry>(&renderer.commands[0]);
        REQUIRE(geometry != nullptr);
        REQUIRE(geometry->fill.has_value());
        const auto* brush = std::get_if<AcrylicBrush>(&*geometry->fill);
        REQUIRE(brush != nullptr);
        CHECK(brush->fallback_color == theme.surfaces.menu);
        CHECK(brush->tint_color == theme.overlay_material->tint_color);
    }
    theme.overlay_material.reset();
    {
        testing::TestRenderer renderer;
        fill_overlay_surface(renderer, theme, rect, 8.0f, theme.surfaces.menu);
        REQUIRE(renderer.commands.size() == 1);
        const auto* fill = std::get_if<FillRoundedRect>(&renderer.commands[0]);
        REQUIRE(fill != nullptr);
        CHECK(fill->color == theme.surfaces.menu);
        CHECK(fill->radius == 8.0f);
    }
}

TEST_CASE("The System variant draws the running Windows' own Fluent", "[theme]")
{
    OsThemeSnapshot windows11;
    OsThemeSnapshot windows10;
    windows10.system_variant = ThemeVariant::Fluent;

    CHECK(resolve_theme(ThemeRequest{}, windows11).variant == ThemeVariant::Fluent2);
    CHECK(resolve_theme(ThemeRequest{}, windows10).variant == ThemeVariant::Fluent);
    CHECK(resolve_theme(ThemeRequest{}, windows10).colors().background ==
          resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, windows10).colors().background);
    // Asked for by name, Fluent 2 stays Fluent 2 on Windows 10.
    CHECK(resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, windows10).variant == ThemeVariant::Fluent2);

    // Under high contrast, and in the other mode through with_dark_mode, System still follows the OS.
    windows10.high_contrast = true;
    CHECK(resolve_theme(ThemeRequest{}, windows10).variant == ThemeVariant::Fluent);
    windows10.high_contrast = false;
    const Theme light       = resolve_theme(ThemeRequest{.mode = ThemeMode::Light}, windows10);
    REQUIRE(light.with_dark_mode);
    CHECK(light.with_dark_mode(true).variant == ThemeVariant::Fluent);
}

TEST_CASE("The test binary has the period theme packs linked in", "[theme]")
{
    REQUIRE(era_themes_enabled());
    CHECK(resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{}).variant == ThemeVariant::Luna);
}

TEST_CASE("theme_colors name every theme color, and with_alpha() replaces its alpha", "[theme][paint]")
{
    const Theme light = Theme::light();
    const Theme dark  = Theme::fluent(Palette::fluent_dark());
    CHECK(theme_colors::Accent(light) == light.colors().accent);
    CHECK(theme_colors::Accent(dark) == dark.colors().accent);
    CHECK(theme_colors::TextSecondary(light) == light.colors().text_secondary);

    const ThemeColor faded = theme_colors::Accent.with_alpha(0.5f);
    CHECK(faded(light) == light.colors().accent.with_alpha(0.5f));
    CHECK(faded != theme_colors::Accent);
    CHECK(faded == theme_colors::Accent.with_alpha(0.5f));

    const Paint paint = faded;
    REQUIRE(paint.resolve(dark));
    CHECK(*paint.resolve(dark) == dark.colors().accent.with_alpha(0.5f));

    ThemeColor none;
    CHECK(none == nullptr);
    CHECK_FALSE(none);
}

TEST_CASE("on_theme_changed reports each newly resolved or pinned theme", "[theme][window]")
{
    Window            window;
    std::vector<bool> dark;
    window.on_theme_changed([&](const Theme& theme) { dark.push_back(theme.dark); });
    window.set_theme(ThemeRequest{.mode = ThemeMode::Dark});
    window.set_theme(ThemeRequest{.mode = ThemeMode::Light});
    window.pin_theme(Theme::default_light());
    CHECK(dark == std::vector<bool>{true, false, false});
}
