// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/themes/ClassicPalette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace ui;

namespace
{
float luminance(const Color& c)
{
    return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
}

// RGB -> HLS -> RGB is not bit-exact, so identity checks allow a small tolerance.
bool approx_equal(const Color& a, const Color& b, float eps = 0.01f)
{
    return std::abs(a.r - b.r) <= eps && std::abs(a.g - b.g) <= eps && std::abs(a.b - b.b) <= eps;
}
} // namespace

TEST_CASE("classic_bevel_ramp_from_face computes a monotonic ramp for a custom scheme face", "[classic_palette]")
{
    const Color            face = Color::from_argb(0xFF804060);
    const ClassicBevelRamp ramp = classic_bevel_ramp_from_face(face);

    REQUIRE(luminance(ramp.highlight) > luminance(ramp.light));
    REQUIRE(luminance(ramp.light) > luminance(face));
    REQUIRE(luminance(face) > luminance(ramp.shadow));
    REQUIRE(luminance(ramp.shadow) > luminance(ramp.dark_shadow));
}

TEST_CASE("classic_lighter/classic_darker at percent=0 are identity", "[classic_palette]")
{
    const Color face = Color::from_argb(0xFF804060);
    REQUIRE(approx_equal(classic_lighter(face, 0.0f), face));
    REQUIRE(approx_equal(classic_darker(face, 0.0f), face));
}

TEST_CASE("ClassicPalette::windows_standard is the real default Windows 95 scheme", "[classic_palette]")
{
    const ClassicPalette p = ClassicPalette::windows_standard();
    REQUIRE(p.face == Color::from_argb(0xFFC0C0C0));
    REQUIRE(p.window == colors::White);
    REQUIRE(p.window_text == colors::Black);
    REQUIRE(p.selection == Color::from_argb(0xFF000080));
    REQUIRE(p.selection_text == colors::White);
    REQUIRE(p.info_bk == Color::from_argb(0xFFFFFFE1));
}

// Windows 2000 moves as a unit: its face with Windows 95's navy selection never shipped.
TEST_CASE("ClassicPalette::windows_standard_2000 is the whole Windows 2000 row, not just a warmer face",
          "[classic_palette]")
{
    const ClassicPalette p = ClassicPalette::windows_standard_2000();
    REQUIRE(p.face == Color::from_argb(0xFFD4D0C8));
    REQUIRE(p.window == colors::White);
    REQUIRE(p.selection == Color::from_argb(0xFF0A246A));
    REQUIRE(p.hot_light == Color::from_argb(0xFF000080));

    const ClassicBevelRamp ramp = classic_bevel_ramp_from_face(p.face);
    REQUIRE(ramp.light == p.face); // 3DLIGHT collapses into the face
    REQUIRE(ramp.highlight == colors::White);
    REQUIRE(ramp.shadow == Color::from_argb(0xFF808080));
    REQUIRE(ramp.dark_shadow == Color::from_argb(0xFF404040)); // softened off pure black
}

TEST_CASE("The Windows 95/98 ramp runs a distinct fifth stop between face and highlight", "[classic_palette]")
{
    const ClassicBevelRamp ramp = classic_bevel_ramp_from_face(Color::from_argb(0xFFC0C0C0));
    REQUIRE(ramp.light == Color::from_argb(0xFFDFDFDF));
    REQUIRE(ramp.highlight == colors::White);
    REQUIRE(ramp.shadow == Color::from_argb(0xFF808080));
    REQUIRE(ramp.dark_shadow == colors::Black);
}

TEST_CASE("Theme::classic() derives the same real Windows 95 four-ring bevel as the published ramp",
          "[classic_palette][theme]")
{
    const Theme theme = Theme::classic(ClassicPalette::windows_standard(), /*hot_track=*/false, "MS Sans Serif");
    REQUIRE(theme.button_normal.background == Color::from_argb(0xFFC0C0C0));
    REQUIRE(theme.button_normal.border_style == BorderStyle::Raised);
    REQUIRE(theme.button_normal.bevel_light == Color::from_argb(0xFFDFDFDF));
    REQUIRE(theme.button_normal.bevel_highlight == colors::White);
    REQUIRE(theme.button_normal.bevel_shadow == Color::from_argb(0xFF808080));
    REQUIRE(theme.button_normal.bevel_dark_shadow == colors::Black);
    REQUIRE(theme.colors().field == colors::White);
    REQUIRE(theme.colors().selection == Color::from_argb(0xFF000080));
    // No hot-tracking: hover is byte-identical to normal.
    REQUIRE(theme.button_hover.background == theme.button_normal.background);
}

TEST_CASE("Theme::classic() hot_track is a toolbar affordance, and leaves command buttons alone",
          "[classic_palette][theme]")
{
    const Theme theme = Theme::classic(ClassicPalette::windows_standard_2000(), /*hot_track=*/true, "MS Sans Serif");
    // A command button has no hover state in this era. Nothing makes DFCS_HOT visible on one.
    REQUIRE(theme.button_hover == theme.button_normal);
    // Office 97/IE4's "Popup" toolbar idiom: flat at rest...
    REQUIRE(theme.subtle_button_normal.border_style == BorderStyle::Flat);
    // ...raising on hover to a single edge: highlight top/left, shadow bottom/right.
    REQUIRE(theme.subtle_button_hover.border_style == BorderStyle::Raised);
    REQUIRE(theme.subtle_button_hover.bevel_rings == 1);
    REQUIRE(theme.subtle_button_hover.bevel_light == theme.subtle_button_hover.bevel_highlight);
    REQUIRE(theme.subtle_button_hover.bevel_dark_shadow == theme.subtle_button_hover.bevel_shadow);
}

TEST_CASE("A classic command button is EDGE_RAISED|BF_SOFT, a window/field edge is not", "[classic_palette][theme]")
{
    const Theme theme = Theme::classic(ClassicPalette::windows_standard(), /*hot_track=*/false, "MS Sans Serif");
    // Getting this wrong puts the white highlight one pixel in from the button's lit edge.
    REQUIRE(theme.button_normal.bevel_soft);
    REQUIRE(theme.button_pressed.bevel_soft);
    REQUIRE(theme.button_disabled.bevel_soft);
    REQUIRE_FALSE(ui::detail::controls(theme).check_box_box.normal.off.bevel_soft);
    REQUIRE_FALSE(ui::detail::controls(theme).check_box_box.normal.on.bevel_soft);
}

TEST_CASE("An app-supplied ThemeRequest::classic_scheme overrides Windows9x's default palette", "[classic_palette]")
{
    ClassicPalette eggplant = ClassicPalette::from_face(Color::from_argb(0xFF804060));
    ThemeRequest   request;
    request.variant        = ThemeVariant::Windows9x;
    request.classic_scheme = eggplant;

    const Theme theme = resolve_theme(request, OsThemeSnapshot{});
    REQUIRE(theme.button_normal.background == eggplant.face);
    REQUIRE(theme.colors().background == eggplant.face);
}
