// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/ui/themes/ClassicPalette.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;

namespace
{

struct NamedTheme
{
    std::string name;
    Theme       theme;
};

// Every variant in light, dark and high contrast, plus the factories called directly.
std::vector<NamedTheme> every_theme()
{
    const std::pair<ThemeVariant, const char*> variants[] = {
        {ThemeVariant::Fluent2, "Fluent2"},
        {ThemeVariant::Fluent, "Fluent"},
        {ThemeVariant::Mdl2, "Mdl2"},
        {ThemeVariant::Metro, "Metro"},
        {ThemeVariant::Aero, "Aero"},
        {ThemeVariant::Luna, "Luna"},
        {ThemeVariant::Windows9x, "Windows9x"},
        {ThemeVariant::Windows3x, "Windows3x"},
        {ThemeVariant::Windows1x, "Windows1x"},
    };
    const std::pair<ThemeMode, const char*> modes[] = {
        {ThemeMode::Light, "light"},
        {ThemeMode::Dark, "dark"},
        {ThemeMode::HighContrast, "high contrast"},
    };
    std::vector<NamedTheme> themes;
    for (const auto& [variant, variant_name] : variants)
    {
        for (const auto& [mode, mode_name] : modes)
        {
            themes.push_back({std::string(variant_name) + " " + mode_name,
                              resolve_theme(ThemeRequest{.mode = mode, .variant = variant}, OsThemeSnapshot{})});
        }
    }
    // The OS's own contrast theme, too.
    OsThemeSnapshot contrast{};
    contrast.high_contrast = true;
    themes.push_back({"Fluent2 OS contrast", resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, contrast)});
    themes.push_back({"Theme::light()", Theme::light()});
    themes.push_back({"Theme::fluent(dark)", Theme::fluent(Palette::fluent_dark())});
    return themes;
}

} // namespace

TEST_CASE("Every theme fills the roles a custom control reads from theme.colors()", "[theme][theme-colors]")
{
    for (const NamedTheme& named : every_theme())
    {
        INFO(named.name);
        const Theme&       t = named.theme;
        const ThemeColors& c = t.colors();

        // Each foreground reads against its own surface.
        CHECK(c.text != c.background);
        CHECK(c.field_text != c.field);
        CHECK(c.selection_text != c.selection);
        CHECK(c.text_on_accent != c.accent);
        CHECK(c.text_disabled != c.background);
        CHECK(c.link != c.background);
        // A two-color focus visual (Metro's white and black dashes) shows on any background.
        CHECK((c.focus != c.background || c.focus_inner != c.background));

        // The roles copied from the era's records match them.
        CHECK(c.accent == t.accent_button_normal.background);
        CHECK(c.accent_hover == t.accent_button_hover.background);
        CHECK(c.accent_pressed == t.accent_button_pressed.background);
        CHECK(c.text_on_accent == t.accent_button_normal.text);
        CHECK(c.control_fill == t.button_normal.background);
        CHECK(c.control_fill_hover == t.button_hover.background);
        CHECK(c.control_fill_pressed == t.button_pressed.background);
        CHECK(c.control_fill_disabled == t.button_disabled.background);
        CHECK(c.control_stroke == t.button_normal.border);
        CHECK(c.overlay == t.surfaces.menu);
        CHECK(c.critical == t.severity.critical.fill);
        CHECK(c.caution == t.severity.caution.fill);
        CHECK(c.success == t.severity.success.fill);
        CHECK(c.informational == t.severity.informational.fill);

        // The Fluent palette's roles only where the palette is filled, the era's own otherwise.
        const bool fluent = (t.variant == ThemeVariant::Fluent2 || t.variant == ThemeVariant::Fluent) && !t.high_contrast;
        CHECK(c.text_secondary == (fluent ? t.palette.text_fill_secondary : c.text));
        CHECK(c.card == (fluent ? t.palette.card_background_default : c.background));
        CHECK(c.control_stroke_strong == (fluent ? t.palette.control_strong_stroke_default : t.surfaces.field_border));
    }
}

TEST_CASE("A contrast theme's role colors are the contrast scheme's", "[theme][theme-colors]")
{
    const HighContrastPalette hc = HighContrastPalette::aquatic();
    OsThemeSnapshot           os{};
    os.high_contrast  = true;
    os.hc             = hc;
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, os);

    CHECK(theme.colors().text == hc.window_text);
    CHECK(theme.colors().background == hc.window_background);
    CHECK(theme.colors().text_disabled == hc.disabled_text);
    CHECK(theme.colors().text_secondary == hc.window_text); // no secondary text in a contrast scheme
    CHECK(theme.colors().card == hc.window_background);
}

TEST_CASE("The resolved roles follow the theme's mode", "[theme][theme-colors]")
{
    for (const ThemeVariant variant : {ThemeVariant::Fluent2, ThemeVariant::Fluent, ThemeVariant::Mdl2, ThemeVariant::Metro})
    {
        const Theme light = resolve_theme(ThemeRequest{.mode = ThemeMode::Light, .variant = variant}, OsThemeSnapshot{});
        const Theme dark = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = variant}, OsThemeSnapshot{});
        CHECK(light.colors().text != dark.colors().text);
        CHECK(light.colors().background != dark.colors().background);
        CHECK(light.colors().control_fill != dark.colors().control_fill);
        CHECK(light.colors().text_secondary != dark.colors().text_secondary);
    }
}

// ---- colors as input: Theme::fluent(ThemeColors), Theme::classic(ThemeColors, ...) ----

namespace
{

// Every role, for the tests that walk them all.
constexpr Color ThemeColors::* const EveryRole[] = {
    &ThemeColors::text,
    &ThemeColors::text_secondary,
    &ThemeColors::text_disabled,
    &ThemeColors::text_on_accent,
    &ThemeColors::link,
    &ThemeColors::background,
    &ThemeColors::card,
    &ThemeColors::overlay,
    &ThemeColors::field,
    &ThemeColors::field_text,
    &ThemeColors::scrim,
    &ThemeColors::accent,
    &ThemeColors::accent_hover,
    &ThemeColors::accent_pressed,
    &ThemeColors::control_fill,
    &ThemeColors::control_fill_hover,
    &ThemeColors::control_fill_pressed,
    &ThemeColors::control_fill_disabled,
    &ThemeColors::control_stroke,
    &ThemeColors::control_stroke_strong,
    &ThemeColors::divider,
    &ThemeColors::selection,
    &ThemeColors::selection_text,
    &ThemeColors::focus,
    &ThemeColors::focus_inner,
    &ThemeColors::critical,
    &ThemeColors::caution,
    &ThemeColors::success,
    &ThemeColors::informational,
};

} // namespace

TEST_CASE("A theme's colors are read-only", "[theme][theme-colors]")
{
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Theme&>().colors()), const ThemeColors&>);
}

TEST_CASE("ThemeColors defaults to the Fluent light theme's colors", "[theme][theme-colors]")
{
    CHECK(ThemeColors{} == Theme::light().colors());
}

TEST_CASE("Theme::fluent(ThemeColors) given a theme's own colors makes that theme", "[theme][theme-colors]")
{
    for (const Palette& palette : {Palette::fluent_light(), Palette::fluent_dark()})
    {
        const Theme from_palette = Theme::fluent(palette);
        const Theme from_colors  = Theme::fluent(from_palette.colors());
        CHECK(from_colors.colors() == from_palette.colors());
        CHECK(from_colors.dark == palette.dark);
        CHECK(from_colors.button_normal == from_palette.button_normal);
        CHECK(from_colors.accent_button_hover == from_palette.accent_button_hover);
    }
}

TEST_CASE("Theme::fluent(ThemeColors) reads back every role it was given", "[theme][theme-colors]")
{
    for (Color ThemeColors::* const role : EveryRole)
    {
        ThemeColors wanted = ThemeColors{};
        wanted.*role       = Color::from_argb(0xFF123456);
        const Theme theme  = Theme::fluent(wanted);
        CHECK(theme.colors().*role == Color::from_argb(0xFF123456));
    }
}

TEST_CASE("Theme::fluent(ThemeColors) derives the rest of the theme from the changed roles", "[theme][theme-colors]")
{
    const Color purple = Color::from_argb(0xFF8764B8);
    const Theme theme  = Theme::fluent(ThemeColors{.accent = purple});
    CHECK(theme.palette.accent_fill_default == purple);
    CHECK(theme.accent_button_normal.background == purple);
    CHECK(theme.colors().accent_hover == purple.with_alpha(0.9f));
    CHECK(theme.colors().accent_pressed == purple.with_alpha(0.8f));
    CHECK(theme.colors().selection == purple);
    CHECK(theme.colors().informational == purple);
    // The roles left alone keep the light theme's values.
    CHECK(theme.colors().text == Theme::light().colors().text);
    CHECK(theme.colors().background == Theme::light().colors().background);

    // A dark background picks the dark palette for everything not given.
    ThemeColors dark_roles = Theme::fluent(Palette::fluent_dark()).colors();
    dark_roles.background  = Color::from_argb(0xFF101820);
    const Theme dark       = Theme::fluent(dark_roles);
    CHECK(dark.dark);
    CHECK(dark.colors().background == Color::from_argb(0xFF101820));
    CHECK(dark.colors().text == Palette::fluent_dark().text_fill_primary);
}

TEST_CASE("Theme::classic(ThemeColors) maps the changed roles onto the scheme", "[theme][theme-colors]")
{
    const Theme standard = Theme::classic(ClassicPalette::windows_standard(), true, "Tahoma");
    CHECK(Theme::classic(standard.colors(), true, "Tahoma").colors() == standard.colors());

    ThemeColors roles = standard.colors();
    roles.selection   = Color::from_rgb(0x008080);
    roles.background  = Color::from_rgb(0xD4D0C8);
    roles.field       = Color::from_rgb(0xFFFFE0);
    const Theme theme = Theme::classic(roles, true, "Tahoma");
    CHECK(theme.variant == ThemeVariant::Windows9x);
    CHECK(theme.colors().selection == Color::from_rgb(0x008080));
    CHECK(theme.colors().background == Color::from_rgb(0xD4D0C8));
    CHECK(theme.colors().field == Color::from_rgb(0xFFFFE0));
    CHECK(theme.button_normal.background == Color::from_rgb(0xD4D0C8)); // the face follows the background
    CHECK(theme.colors().text == standard.colors().text);
}

TEST_CASE("with_content_colors changes the text and background roles of a copy", "[theme][theme-colors]")
{
    const Theme theme  = Theme::light();
    const Theme inside = theme.with_content_colors(colors::White, Color::from_argb(0xFF202020));
    CHECK(inside.colors().text == colors::White);
    CHECK(inside.colors().background == Color::from_argb(0xFF202020));
    CHECK(inside.colors().accent == theme.colors().accent);
    CHECK(theme.colors().text == Theme::light().colors().text);
    CHECK(theme.with_content_colors(colors::White).colors().background == theme.colors().background);
}
