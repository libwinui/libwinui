// SPDX-License-Identifier: MIT
#include "winui/ui/themes/ThemeSource.hpp"

#include "../../render/ControlGlyphs.hpp"
#include "../AnimationsScope.hpp"
#include "ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/themes/ClassicPalette.hpp"
#include "winui/ui/themes/EarlyPalette.hpp"
#include "winui/ui/themes/Mdl2Palette.hpp"
#include "winui/ui/themes/MetroPalette.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <array>
#include <atomic>
#include <initializer_list>
#include <optional>
#include <utility>

#include <cstddef>
#include <cstdint>

namespace ui
{
namespace
{

[[nodiscard]] Color lighten(const Color& c, float t)
{
    return lerp(c, colors::White, t);
}

[[nodiscard]] Color darken(const Color& c, float t)
{
    return lerp(c, colors::Black, t);
}

// Windows exposes the tonal ramp only for the live system accent (UISettings), so an app's
// accent_override gets an approximated one.
[[nodiscard]] AccentRamp derive_ramp(const Color& accent)
{
    AccentRamp ramp;
    ramp.light1 = lighten(accent, 0.15f);
    ramp.light2 = lighten(accent, 0.30f);
    ramp.light3 = lighten(accent, 0.45f);
    ramp.dark1  = darken(accent, 0.15f);
    ramp.dark2  = darken(accent, 0.30f);
    ramp.dark3  = darken(accent, 0.45f);
    return ramp;
}

// Every role defaults to the era's own text_style, so a role-less TextControl renders in it too.
void apply_body_type_ramp(Theme& theme)
{
    theme.type
        .header = theme.type
                      .subheader = theme.type.title = theme.type.subtitle = theme.type.body = theme.type.caption = theme.text_style;
}

// HC has no light/dark setting of its own, only the page color the user's scheme picked.
[[nodiscard]] bool is_dark_surface(const Color& c)
{
    return (0.299f * c.r + 0.587f * c.g + 0.114f * c.b) < 0.5f;
}

// Fluent 2's high contrast, and the color base Fluent 1's reuses (resolve_theme_high_contrast()).
[[nodiscard]] Theme resolve_high_contrast(const HighContrastPalette& hc)
{
    Theme                  theme;
    detail::ControlChrome& controls = detail::edit_controls(theme);
    theme.variant                   = ThemeVariant::Fluent2;
    theme.high_contrast             = true;
    // The check mark still animates under a contrast theme.
    controls.check_mark_draws_in = true;
    // The corner radii stay under a contrast theme; only the colors come from the OS.
    detail::edit_colors(theme).background  = hc.window_background;
    detail::edit_colors(theme).text        = hc.window_text;
    detail::edit_colors(theme).focus       = hc.accent;
    detail::edit_colors(theme).focus_inner = hc.window_background;
    theme.spelling_error                   = hc.window_text;

    theme.chrome.dark_titlebar = is_dark_surface(hc.window_background);
    // Nothing may show through: a contrast ratio measured against colors.background means
    // nothing over the user's wallpaper.
    theme.chrome.allows_system_backdrop = false;

    detail::edit_colors(theme).field          = hc.window_background;
    detail::edit_colors(theme).field_text     = hc.window_text;
    theme.surfaces.field_border               = hc.control_border;
    detail::edit_colors(theme).selection      = hc.accent;
    detail::edit_colors(theme).selection_text = hc.accent_text;
    detail::edit_colors(theme).text_disabled  = hc.disabled_text;
    theme.surfaces.menu                       = hc.window_background;
    theme.surfaces.menu_text                  = hc.window_text;
    theme.surfaces.menu_highlight             = hc.accent;
    theme.surfaces.menu_highlight_text        = hc.accent_text;
    theme.surfaces.tooltip                    = hc.window_background;
    theme.surfaces.tooltip_text               = hc.window_text;
    // The teaching tip takes Window and WindowText.
    theme.surfaces.teaching_tip   = hc.window_background;
    theme.surfaces.surface_stroke = hc.window_text;
    // The side pane takes the Window color.
    theme.surfaces.split_view_pane  = hc.window_background;
    detail::edit_colors(theme).link = hc.hyperlink;
    // The hyperlink button's high contrast colors: WindowText on hover, Highlight pressed and
    // GrayText disabled.
    theme.surfaces.hyperlink_hover    = hc.window_text;
    theme.surfaces.hyperlink_pressed  = hc.accent;
    theme.surfaces.hyperlink_disabled = hc.disabled_text;
    theme.surfaces.separator_light    = hc.control_border;
    theme.surfaces.separator_dark     = hc.control_border;
    // Splitter draws its rest rule in this. The border color is the only HC slot guaranteed to
    // read against colors.background.
    detail::edit_colors(theme).divider = hc.control_border;
    theme.surfaces.scrollbar_track     = hc.window_background;
    theme.surfaces.scrollbar_thumb     = hc.control_border;
    theme.surfaces.scrollbar_arrow     = hc.window_text;

    // High contrast has no semantic hues, so every severity resolves to the same verbatim pair.
    // Fluent Light's pale severity tints under HC text would fail contrast.
    const Theme::SeverityVisual hc_severity{hc.accent, hc.window_background};
    theme.severity.informational = theme.severity.success = theme.severity.caution = theme.severity.critical = hc_severity;
    theme.severity.neutral      = hc_severity.fill;
    theme.severity.text_on_fill = hc.accent_text;
    theme.severity.stroke       = hc.control_border;
    // The channel is filled, so it takes the border color: an unfilled track must still show.
    controls.progress_track = hc.control_border;
    controls.progress_fill  = hc.accent;

    // Every HC button is outlined in ButtonText. Without
    // it a rest button is a ButtonFace fill on a page of the same color.
    auto outlined = [](ButtonVisual v)
    {
        v.draw_border = true;
        return v;
    };
    const ButtonVisual rest     = outlined({hc.control_background, hc.control_border, hc.control_text});
    const ButtonVisual hot      = outlined({hc.accent, hc.accent, hc.accent_text});
    const ButtonVisual disabled = outlined({hc.control_background, hc.disabled_text, hc.disabled_text});
    theme.button_normal         = rest;
    theme.button_hover          = hot;
    theme.button_pressed        = hot;
    theme.button_disabled       = disabled;

    // HC has no separate accent button. Subtle is transparent at rest, ButtonFace otherwise,
    // never a border.
    theme.accent_button_normal = theme.accent_button_hover = theme.accent_button_pressed = hot;
    theme.accent_button_disabled                                                         = disabled;
    const ButtonVisual subtle_fill{hc.control_background, colors::Transparent, hc.control_text};
    theme.subtle_button_normal   = ButtonVisual{colors::Transparent, colors::Transparent, hc.control_text};
    theme.subtle_button_hover    = subtle_fill;
    theme.subtle_button_pressed  = subtle_fill;
    theme.subtle_button_disabled = ButtonVisual{hc.control_background, colors::Transparent, hc.disabled_text};

    const ButtonVisual off_rest{hc.window_background, hc.control_border, hc.window_background};
    const ButtonVisual off_disabled{hc.window_background, hc.disabled_text, hc.window_background};
    controls.set_toggle_visuals(ToggleVisuals{ToggleVisual{off_rest, hot},
                                              ToggleVisual{off_rest, hot},
                                              ToggleVisual{off_rest, hot},
                                              ToggleVisual{off_disabled, disabled}});
    // An indeterminate check box takes the checked pair here too, with the Windows 11 dash.
    controls.check_box_indeterminate = IndeterminateVisuals::like_checked(controls.check_box_box, IndeterminateMark::Glyph);
    return theme;
}

// Accent fills take light2 in dark and dark1 in light, so the fill stands out from the page.
[[nodiscard]] Theme resolve_fluent2(bool dark, const Color& accent, const AccentRamp& ramp)
{
    (void)accent;
    Palette palette             = dark ? Palette::fluent_dark() : Palette::fluent_light();
    palette.accent_fill_default = dark ? ramp.light2 : ramp.dark1;
    // The primary, secondary and tertiary accent text: the rungs one further out than the fill.
    palette.accent_text_primary   = dark ? ramp.light3 : ramp.dark2;
    palette.accent_text_secondary = dark ? ramp.light3 : ramp.dark3;
    palette.accent_text_tertiary  = dark ? ramp.light2 : ramp.dark1;
    Theme theme                   = Theme::fluent(palette);
    // Windows 11 rounds by default, so DWM only needs the titlebar tint.
    theme.chrome.dark_titlebar = dark;
    // NavigationShell's content layer: the layer the page sits on, set apart from the pane. A
    // later restyle; Windows 10's NavigationShell has none.
    theme.surfaces.nav_content        = palette.layer_fill_default;
    theme.surfaces.nav_content_stroke = palette.card_stroke_default;
    return theme;
}

// Fluent 1, the Windows 10 look as of the 10.0.20348.0 SDK.
// The palette carries the colors; this handles where Windows 10's shape differs.
[[nodiscard]] Theme resolve_fluent1(bool dark, const Color& accent, const AccentRamp& ramp)
{
    Palette palette = dark ? Palette::fluent1_dark() : Palette::fluent1_light();
    // The plain accent in Light and Dark. The era's accent is base, Light1 and Dark1, never
    // Fluent 2's per-theme rung.
    palette.accent_fill_default = accent;
    palette.accent_text_primary = accent; // the hyperlink text

    Theme                  theme    = Theme::fluent(palette);
    detail::ControlChrome& controls = detail::edit_controls(theme);
    theme.variant                   = ThemeVariant::Fluent;
    theme.chrome.dark_titlebar      = dark;
    // Windows 10 never rounded its window corners.
    theme.chrome.corner_preference = CornerPreference::DoNotRound;
    // No Mica: it shipped on 2021-09-14, after this era. Acrylic is control-level only.
    theme.chrome.allows_system_backdrop = false;
    // Windows 10 apps drew non-transformed text on an opaque page with ClearType, as in MDL2.
    theme.rasterization = SetRasterization{GeometryAA::Antialiased, TextAA::Subpixel, /*snap_to_device_pixels=*/false};

    // ---- Geometry -----------------------------------------------------------------------
    // The corner radii: 2 / 4. The era's first controls were square; these are its later
    // rounded restyle.
    theme.corner_radius         = 2.0f;
    theme.overlay_corner_radius = 4.0f;

    // The button padding 8,5,8,6, standing in for every control.
    theme.padding = Edges{.left = 8.0f, .top = 5.0f, .right = 8.0f, .bottom = 6.0f};

    // ---- Borders: the lift comes off, and nothing replaces it ----------------------------
    // No control of this era has the elevation border. The era's Reveal lighting is not modeled.
    theme.button_normal.border_gradient        = std::nullopt;
    theme.button_hover.border_gradient         = std::nullopt;
    theme.accent_button_normal.border_gradient = std::nullopt;
    theme.accent_button_hover.border_gradient  = std::nullopt;

    // The era's button keeps all eight borders transparent, so only the fill shows state.
    for (ButtonVisual* v :
         {&theme.button_normal,
          &theme.button_hover,
          &theme.button_pressed,
          &theme.button_disabled,
          &theme.accent_button_normal,
          &theme.accent_button_hover,
          &theme.accent_button_pressed,
          &theme.accent_button_disabled})
    {
        v->border = colors::Transparent;
    }

    // ---- Accent: base / Light1 / Dark1 ----------------------------------------------------
    // The accent button's hover and pressed fills are the Light1 and Dark1 rungs, where Fluent 2
    // fades the fill instead.
    theme.accent_button_hover.background   = ramp.light1;
    theme.accent_button_pressed.background = ramp.dark1;
    theme.accent_button_pressed.text       = palette.text_on_accent_primary;

    // ---- Toggles: a stroke, not a filling box ---------------------------------------------
    // The unchecked stroke carries the state (60/80/100 %, 40 % disabled), identically in the
    // CheckBox, RadioButton and ToggleSwitch.
    for (ToggleVisuals* tv : controls.toggles())
    {
        tv->hover.off.border   = palette.control_stroke_secondary; // 80 %
        tv->pressed.off.border = palette.text_fill_primary;        // 100 %
    }

    // The off knob is the full-strength text color in every state; Fluent 2 uses 60 %.
    for (ToggleVisuals* tv : controls.toggles())
    {
        tv->normal.off.text = tv->hover.off.text = tv->pressed.off.text = palette.text_fill_primary;
    }

    // The checked half is the accent button's base/Light1/Dark1 trio, with a white mark in
    // every state.
    for (ToggleVisuals* tv : controls.toggles())
    {
        tv->normal.on  = theme.accent_button_normal;
        tv->hover.on   = theme.accent_button_hover;
        tv->pressed.on = theme.accent_button_pressed;
    }
    // The era has no elevation rim, so the radio dot has none.
    for (ButtonVisual* v : controls.radio_ring.all())
    {
        v->mark_border_gradient.reset();
    }
    // The era's CheckBox: indeterminate repeats the checked ladder, with the Windows 10 glyph.
    controls.check_box_indeterminate = IndeterminateVisuals::like_checked(controls.check_box_box,
                                                                          IndeterminateMark::Glyph,
                                                                          glyphs::CheckBoxIndeterminateMdl2);

    // ---- The text field ---------------------------------------------------------------------
    // Rest and hover wash the field in the page's inverse (40 %, 60 %), where the neutral button
    // washes in the text color.
    const Color alt                  = dark ? colors::Black : colors::White;
    detail::edit_colors(theme).field = alt.with_alpha(0.4f);
    controls.field_fills = detail::ControlChrome::FieldFills{alt.with_alpha(0.6f),              // hover, 60 %
                                                             palette.control_fill_input_active, // focused, opaque
                                                             palette.control_fill_disabled};    // disabled

    // The focused border is a uniform 2 this era: a focused field is ringed in
    // accent, not underlined.
    controls.field_focus_border_thickness = Thickness{2.0f};
    controls.field_border_gradient.reset(); // no elevation gradient this era
    // The hover border is 80 % against 60 % at rest; Windows 11 uses one color for both.
    controls.field_border_hover = palette.control_stroke_secondary;

    // ---- List rows: the era really does fill them with accent --------------------------------
    // The opacities are applied to the live accent: the low rung is 0.4 in Light and 0.6 in Dark,
    // the medium rung 0.6 in Light.
    controls.row_selection_fills = detail::ControlChrome::AccentSelectionFills{
        accent.with_alpha(0.75f), // an added rung
        // A hovered selected row takes the medium rung in Light and the low rung in Dark: hovering a
        // selection fades it to 0.6 in both.
        accent.with_alpha(0.6f),
        accent.with_alpha(0.9f),               // the other added rung
        accent.with_alpha(dark ? 0.6f : 0.4f), // a selected combo box item: a different ladder's rest rung
        accent.with_alpha(dark ? 0.8f : 0.6f), // its hover: this one deepens
    };
    // An open combo box field takes the low rung, with or without focus.
    controls.combo_open_fill = accent.with_alpha(dark ? 0.6f : 0.4f);

    // The era's Slider: the thumb is the accent triple. The neutral gray ladder belongs to
    // ColorPicker's sliders.
    controls.slider_fills = detail::ControlChrome::SliderFills{
        accent,                                           // thumb: the plain accent
        ramp.light1,                                      // hover: Light1
        ramp.dark1,                                       // pressed: Dark1
        Color::from_argb(dark ? 0xFF333333 : 0xFFCCCCCC), // disabled
        palette.text_fill_tertiary,                       // track: 40 %
        palette.text_fill_secondary,                      // track hover: 60 %
        Color::from_argb(dark ? 0xFF333333 : 0xFFCCCCCC), // disabled
    };

    // NavigationShell's indicator: 2 x 24 at radius 2, inset 4 from the left, against Windows 11's
    // 3 x 16 flush with the pill's left edge.
    controls.nav_indicator.size       = Size{2.0f, 24.0f};
    controls.nav_indicator.left_inset = 4.0f;

    // The icon font is Segoe MDL2 Assets throughout the era. Named outright because today's
    // default icon font is Segoe Fluent Icons.
    theme.icon_font_family = "Segoe MDL2 Assets";

    // The scroll bar thumb is an opaque gray, the track the secondary background at 0.9, and the
    // arrows the full-strength text color.
    theme.surfaces.scrollbar_thumb = Color::from_argb(dark ? 0xFF858585 : 0xFF7A7A7A);
    theme.surfaces.scrollbar_track = palette.solid_background_secondary.with_alpha(0.9f);
    theme.surfaces.scrollbar_arrow = palette.text_fill_primary;

    // ---- Motion: there is none, at the level `Theme` models ------------------------------------
    // Every state color change of the era is instant, so fills snap. anim_fast
    // stays, since the era does animate the ToggleSwitch knob's slide.
    theme.anim_faster = std::chrono::milliseconds{0};

    // ---- Indicator geometry ---------------------------------------------------------------
    // The era never resizes indicators: a 10 px knob, a fixed 8 px radio dot, a 2 px
    // slider track and one 20 px thumb disc.
    controls.toggle_knob.rest = controls.toggle_knob.hover = 10.0f;
    controls.toggle_knob.pressed                           = Size{10.0f, 10.0f};
    controls.radio_dot.rest = controls.radio_dot.hover = controls.radio_dot.pressed = 8.0f;
    controls.slider_track_thickness                                                 = 2.0f;
    controls.slider_thumb.outer = controls.slider_thumb.rest = controls.slider_thumb.hover = controls.slider_thumb.pressed = 20.0f;

    // ---- The modal scrim washes the page *out* in Light --------------------------------------
    // The dialog's backdrop is the page's inverse at 60 %. Fluent 2
    // uses 30 % black in both themes.
    detail::edit_colors(theme).scrim = (dark ? colors::Black : colors::White).with_alpha(0.6f);

    // ---- Type: a different ramp, not a rescaled one -------------------------------------------
    // The era's seven text styles at their 1809-and-later sizes (Body is 15 in 1709/1803).
    // The large styles get lighter as they grow, where Fluent 2's stay SemiBold.
    auto type_style = [](float size, FontWeight weight)
    { return TextStyle{size, TextAlign::Leading, TextAlign::Center, "Segoe UI", weight}; };
    theme.type.caption     = type_style(12.0f, FontWeight::Regular);
    theme.type.body        = type_style(14.0f, FontWeight::Regular);
    theme.type.body_strong = type_style(14.0f, FontWeight::SemiBold);
    theme.type.subtitle    = type_style(20.0f, FontWeight::Regular);
    theme.type.title       = type_style(24.0f, FontWeight::SemiLight);
    theme.type.subheader   = type_style(34.0f, FontWeight::Light);
    theme.type.header      = type_style(46.0f, FontWeight::Light);
    // Windows 10 has no Segoe UI Variable: control text is the static Segoe UI as well.
    theme.text_style.font_family = "Segoe UI";


    // CalendarView's square cells (the OS generic.xaml of 2017 to 2021), on the System ramp.
    {
        const auto pick = [dark](std::uint32_t light, std::uint32_t dark_value)
        { return Color::from_argb(dark ? dark_value : light); };
        detail::ControlChrome::CalendarChrome calendar;
        calendar.kind                                = detail::ControlChrome::CalendarChrome::Kind::Square;
        calendar.background                          = pick(0xFFFFFFFF, 0xFF000000); // AltHigh
        calendar.border                              = pick(0xFFE6E6E6, 0xFF1F1F1F); // ChromeMedium
        calendar.heading                             = pick(0xCC000000, 0xCCFFFFFF); // BaseMediumHigh
        calendar.heading_hover                       = pick(0xFF000000, 0xFFFFFFFF); // BaseHigh
        calendar.heading_pressed                     = pick(0x99000000, 0x99FFFFFF); // BaseMedium
        calendar.heading_disabled                    = pick(0x66000000, 0x66FFFFFF); // BaseMediumLow
        calendar.item_text                           = pick(0xFF000000, 0xFFFFFFFF);
        calendar.disabled_text                       = pick(0x66000000, 0x66FFFFFF);
        calendar.out_of_scope_fill                   = pick(0xFFF2F2F2, 0xFF2B2B2B); // ChromeMediumLow
        calendar.hover_border                        = pick(0x66000000, 0x66FFFFFF);
        calendar.pressed_border                      = pick(0x99000000, 0x99FFFFFF);
        calendar.selected_border                     = accent;
        calendar.selected_hover_border               = accent.with_alpha(dark ? 0.8f : 0.6f);
        calendar.selected_pressed_border             = accent.with_alpha(dark ? 0.9f : 0.7f);
        calendar.today_fill                          = accent;
        calendar.today_text                          = colors::White;
        calendar.today_selected_ring                 = pick(0xFFFFFFFF, 0xFF000000); // AltAltHigh
        calendar.today_hover_border                  = pick(0x66000000, 0x66FFFFFF);
        calendar.today_pressed_border                = pick(0x99000000, 0x99FFFFFF);
        calendar.blackout_text                       = pick(0x66000000, 0x66FFFFFF);
        calendar.corner_radius                       = theme.corner_radius;
        detail::edit_controls(theme).calendar_chrome = calendar;
    }

    // DatePicker and TimePicker: Windows 10's flyout, the band a plain accent wash.
    detail::edit_controls(theme).date_time_picker_chrome = detail::ControlChrome::
        DateTimePickerChrome{.kind = detail::ControlChrome::DateTimePickerChrome::Kind::Flyout, .plain_band = true};
    return theme;
}

// ---- Retro theme packs --------------------------------------------------------------------
//
// Fixed, light-only palettes: retro is an explicit opt-in, so none take a dark flag or accent.

// The whole look is one EarlyPalette plus Theme::early(). Defaults to the Windows 2.03 color row;
// ThemeRequest::early_scheme picks one of the other three.
Theme resolve_windows_old(const std::optional<EarlyPalette>& scheme)
{
    return Theme::early(scheme.value_or(EarlyPalette::windows_2x_color()));
}

// MS Sans Serif 8 pt, the dialog font of both classic eras, is a bitmap .fon that DirectWrite
// cannot load, and a request for it by name falls back to Segoe UI, a smaller face with a taller
// line. Its TrueType successor stands in, at 8 pt (10.667 DIPs; the strike's em is 11 px) on the
// strike's own 13 px line, which puts the caps and the ascenders on the same rows.
void use_ms_sans_serif(Theme& theme)
{
    theme.text_style.font_family = "Microsoft Sans Serif";
    theme.text_style.font_size   = 32.0f / 3.0f;
    theme.text_style.line_height = 13.0f;
    apply_body_type_ramp(theme);
}

// Windows 3.x shares ClassicPalette::windows_standard() with 9x (#C0C0C0 is a Windows 3.0
// color). Only the edge construction differs, see Theme::classic's win3x_edge.
Theme resolve_windows3x(const std::optional<ClassicPalette>& scheme)
{
    // Windows 3.x has no hover state anywhere; its guide argues against rollover feedback.
    Theme                  theme = Theme::classic(scheme.value_or(ClassicPalette::windows_standard()),
                                                  /*hot_track=*/false,
                                                  // The dialog font: 8pt "Helv", which WIN.SRC maps to MS Sans Serif.
                                                  "Microsoft Sans Serif",
                                                  /*win3x_edge=*/true);
    detail::ControlChrome& controls = detail::edit_controls(theme);
    use_ms_sans_serif(theme);

    // The chrome font is System 10pt bold (VGASYS.FON), so menus and title bars read bold. System
    // is a bitmap face too, a 13 px em on a 16 px line; Microsoft Sans Serif Bold at 12 px sets
    // the same menu bar within a pixel ("File Edit View Help Options": 174 px against 175).
    TextStyle chrome        = theme.text_style;
    chrome.font_size        = 12.0f;
    chrome.line_height      = 16.0f;
    chrome.weight           = FontWeight::Bold;
    theme.chrome_text_style = chrome;

    // No NavigationShell accent bar: the era has no control accent, no rounding and no animation.
    // Selection is already COLOR_HIGHLIGHT behind COLOR_HIGHLIGHTTEXT.
    controls.accent_selection_indicator = false;
    return theme;
}

// 95/98 and 2000 are two coherent rows, mixing them reproduces no released version. This takes
// the 95/98 row; windows_standard_2000() is one classic_scheme away.
Theme resolve_windows9x(const std::optional<ClassicPalette>& scheme)
{
    Theme                  theme    = Theme::classic(scheme.value_or(ClassicPalette::windows_standard()),
                                                     /*hot_track=*/true,
                                                     "Microsoft Sans Serif");
    detail::ControlChrome& controls = detail::edit_controls(theme);
    use_ms_sans_serif(theme);
    // No NavigationShell accent bar, for the same reason as Windows 3.x.
    controls.accent_selection_indicator = false;
    return theme;
}

// ---- Luna: Windows XP -------------------------------------------------------------------
//
// Values follow the Luna msstyles' NormalBlue properties and its bitmaps. Luna Blue is the scheme.
Theme resolve_luna()
{
    Theme                  theme;
    detail::ControlChrome& controls = detail::edit_controls(theme);
    theme.variant                   = ThemeVariant::Luna;
    // The frame a Frame::Themed window draws, measured from the style (LunaWindowFrame.cpp).
    controls.window_frame = detail::luna_window_frame();
    // The desktop edit control: no clear button, and no spin-button popup.
    controls.spin_button_popup      = false;
    controls.text_box_delete_button = false;
    // The push button art's border arc is about 3 px.
    theme.corner_radius = 3.0f;
    // Luna popups are square: the themed ToolTip's radius is 0 and USER32 draws menus unrounded.
    theme.overlay_corner_radius           = 0.0f;
    theme.border_width                    = 1.0f;
    detail::edit_colors(theme).background = Color::from_argb(0xFFECE9D8); // `[SysMetrics] Btnface = 236 233 216`
    {
        // Luna's scroll bar, Blue. The shaft is the six-stop bevel across the bar, its bright end the
        // art's measured #FEFEFB; the corner is the button face color. 17, the shaft bitmap's width.
        using GutterBar = detail::ControlChrome::GutterScrollBar;
        const auto argb = [](std::uint32_t v) { return Color::from_argb(v); };
        const auto six  = [](Color edge, Color from, Color to)
        {
            return std::vector<GradientStop>{GradientStop{0.0f, edge},
                                             GradientStop{0.05f, edge},
                                             GradientStop{0.06f, from},
                                             GradientStop{0.94f, to},
                                             GradientStop{0.95f, edge},
                                             GradientStop{1.0f, edge}};
        };
        const auto stops = [](std::initializer_list<GradientStop> s) { return std::vector<GradientStop>(s); };
        GutterBar  bar;
        bar.style          = GutterBar::Style::Luna;
        bar.thickness      = 17.0f;
        bar.track          = six(argb(0xFFEEEDE5), argb(0xFFF3F1EC), argb(0xFFFEFEFB));
        bar.track_disabled = argb(0xFFF9F8F5);
        bar.corner         = argb(0xFFECE9D8);
        GutterBar::LunaParts luna;
        const Color          ink = argb(0xFF4D6185);
        luna.thumb_rest    = {stops({{0.0f, argb(0xFFC9D8FC)}, {0.65f, argb(0xFFC2D3FC)}, {1.0f, argb(0xFFB6CDFB)}}),
                              argb(0xFFB4C8F6),
                              ink};
        luna.thumb_hover   = {stops({{0.0f, argb(0xFFDAE9FF)}, {0.65f, argb(0xFFD4E6FF)}, {1.0f, argb(0xFFCAE0FF)}}),
                              argb(0xFFACCEFF),
                              ink};
        luna.thumb_pressed = {stops({{0.0f, argb(0xFFA8BEF5)}, {0.65f, argb(0xFFA1BDFA)}, {1.0f, argb(0xFF98B0EE)}}),
                              argb(0xFF838FDA),
                              ink};
        luna.arrow_rest =
            {stops({{0.0f, argb(0xFFE1EAFE)}, {0.3f, argb(0xFFC3D3FD)}, {0.6f, argb(0xFFC3D3FD)}, {1.0f, argb(0xFFBBCDF9)}}),
             argb(0xFFB4C8F6),
             ink};
        luna.arrow_hover = {stops({{0.0f, argb(0xFFFDFFFF)}, {0.25f, argb(0xFFE2F3FD)}, {1.0f, argb(0xFFB9DAFB)}}),
                            argb(0xFF98B1E4),
                            ink};
        luna.arrow_pressed =
            {stops({{0.0f, argb(0xFF6E8EF1)}, {0.3f, argb(0xFF809DF1)}, {0.7f, argb(0xFFAFBFED)}, {1.0f, argb(0xFFD2DEEB)}}),
             argb(0xFF838FDA),
             ink};
        luna.disabled =
            {stops({{0.0f, argb(0xFFF7F7F7)}, {0.3f, argb(0xFFF0F0F0)}, {0.6f, argb(0xFFECECEC)}, {1.0f, argb(0xFFE3E3E3)}}),
             argb(0xFFE8E8DF),
             argb(0xFFC9C9C2)};
        luna.outer_border    = colors::White;
        luna.shadow          = stops({{0.0f, argb(0x00A0B5D3)}, {0.5f, argb(0xFFA0B5D3)}, {1.0f, argb(0xFF7C9FD3)}});
        luna.shadow_disabled = stops({{0.0f, argb(0x00CCCCBA)}, {0.5f, argb(0xFFCCCCBA)}, {1.0f, argb(0xFFC4C4AF)}});
        luna.track_pressed   = six(argb(0xFFD7D5C2), argb(0xFFE3DED3), argb(0xFFFDFDF6));
        luna.gripper         = {argb(0xFFEEF4FE), argb(0xFFFCFDFF), argb(0xFFCFDDFD)};
        luna.gripper_shadow  = {argb(0xFF8CB0F8), argb(0xFF9CC5FF), argb(0xFF839ED8)};
        bar.luna             = std::move(luna);
        controls.gutter_scroll_bar = std::move(bar);
    }
    {
        // BP_GROUPBOX: a 1 px `#D0D0BF` frame rounded by two pixels (the art's keyed corner), and
        // the 2001 guidelines' blue title. Themed comctl32 starts the frame at the title's middle
        // and draws the title 12 px in, with a 2 px gap either side. The insets are the XP-era
        // guidelines' 16 down to the first control, 9 from the sides and 11 above the bottom.
        detail::ControlChrome::EraGroupBox box;
        box.frame              = Color::from_argb(0xFFD0D0BF);
        box.radius             = 2.0f;
        box.header             = Color::from_argb(0xFF0046D5);
        box.header_x           = 12.0f;
        box.header_gap         = 2.0f;
        box.padding            = Edges{.left = 9.0f, .top = 16.0f, .right = 9.0f, .bottom = 11.0f};
        controls.era_group_box = box;
    }
    // MenuText, WindowText and BtnText are absent from [SysMetrics]: Luna leaves text alone.
    detail::edit_colors(theme).text  = colors::Black;
    detail::edit_colors(theme).focus = colors::Black; // the focus rectangle strokes COLOR_BTNTEXT
    theme.focus_visual               = FocusVisual::ClassicDotted;
    // The dotted DrawFocusRect survives Luna. The chrome is antialiased, the text grayscale.
    theme.rasterization = SetRasterization{GeometryAA::Antialiased, TextAA::Grayscale, /*snap_to_device_pixels=*/false};
    detail::edit_colors(theme).field      = colors::White; // `[edit] FillColor = 255 255 255`
    detail::edit_colors(theme).field_text = colors::Black;
    // `[edit] BorderColor = 127 157 185`, shared by the combo, list, list view and tree
    // view. Not the button's #003C74.
    theme.surfaces.field_border               = Color::from_argb(0xFF7F9DB9);
    detail::edit_colors(theme).selection      = Color::from_argb(0xFF316AC5); // `Highlight = 49 106 197`
    detail::edit_colors(theme).selection_text = colors::White;                // `HighlightText = 255 255 255`
    // The themed disabled gray (`[button.pushbutton(disabled)] TextColor`). COLOR_GRAYTEXT
    // #ACA899 only reaches classically drawn controls.
    detail::edit_colors(theme).text_disabled = Color::from_argb(0xFFA1A192);
    // Surfaces::menu is the popup surface, and Luna's is white (`Menu = 255 255 255`).
    // #ECE9D8 is MenuBar, a different slot.
    theme.surfaces.menu      = colors::White;
    theme.surfaces.menu_text = colors::Black;
    // `MenuHilight` equals Highlight in Blue. The menu border, separator and disabled text are
    // #808080.
    theme.surfaces.menu_highlight      = Color::from_argb(0xFF316AC5);
    theme.surfaces.menu_highlight_text = colors::White;
    theme.surfaces.menu_border         = Color::from_argb(0xFF808080);
    // Luna leaves COLOR_INFOBK/INFOTEXT to the system, so its tooltip is the classic one.
    theme.surfaces.tooltip      = Color::from_argb(0xFFFFFFE1);
    theme.surfaces.tooltip_text = colors::Black;
    // The frame is COLOR_WINDOWFRAME.
    theme.surfaces.tooltip_border = colors::Black;
    // The tooltip is square; the push button's 3 px would otherwise round it.
    controls.tooltip_corner_radius = 0.0f;
    // Padding 1,1,3,1 inside a 1 px frame. Wine's comctl32 also uses about 2 px.
    controls.tooltip_padding = Edges{.left = 2.0f, .top = 2.0f, .right = 4.0f, .bottom = 2.0f};
    // Hottracking is absent from Blue's [SysMetrics], so hyperlinks take the classic default.
    detail::edit_colors(theme).link = Color::from_argb(0xFF0000FF);
    // The status bar keeps the classic two-tone groove.
    theme.surfaces.separator_light = colors::White;
    theme.surfaces.separator_dark  = Color::from_argb(0xFFC7C5B2);
    // Luna's ordinary separator is one flat rule (`[Toolbar.Separator]`), not the classic
    // etched groove.
    detail::edit_colors(theme).divider = Color::from_argb(0xFFCAC6AF);
    theme.divider_visual               = ThemeDividerVisual::FlatStroke;
    // Sampled from the scroll shaft art. The track's FillColorHint is the thumb's
    // value pasted in, so do not trust it.
    theme.surfaces.scrollbar_track = Color::from_argb(0xFFF9F8F5);
    // The thumb really is light blue: `[Scrollbar.ThumbBtnVert] FillColorHint`.
    theme.surfaces.scrollbar_thumb = Color::from_argb(0xFFC6D5FD);
    // The scroll arrow art draws its chevrons in a dark slate `#4D6185` (and `#C9C9C2`
    // when disabled) over magenta, never in black.
    theme.surfaces.scrollbar_arrow = Color::from_argb(0xFF4D6185);
    // A Luna button's width comes from its dialog template (75 x 23 px), not its label. The
    // horizontal 14 keeps an "OK" button near that width.
    theme.padding = Symmetric{.horizontal = 14.0f, .vertical = 4.0f};
    // The 96 DPI bullet is the 13 x 13 image.
    controls.toggle_box_size = 13.0f;
    // Square while the button is rounded: the check box art has no arc.
    controls.toggle_box_radius      = 0.0f;
    controls.toggle_content_spacing = 5.0f;
    // `[SysMetrics]` fonts are Tahoma 8, about 11 px.
    theme.text_style.font_family = "Tahoma";
    theme.text_style.font_size   = 11.0f;
    // Luna publishes no type ramp, so every role takes the body style (apply_body_type_ramp()).
    // The radio dot is 5 px and does not resize with state.
    controls.radio_dot.rest = controls.radio_dot.hover = controls.radio_dot.pressed = 5.0f;
    // Instant: the XP schema has no time-valued property, and GetThemeTransitionDuration is
    // Vista-only.
    theme.anim_faster = std::chrono::milliseconds{0};
    // A focused Luna text box changes no chrome, only the caret shows.
    controls.field_focus_border_thickness = Thickness{};
    // No selection pill or bar: the era has no control accent, and its amber hot state is fixed
    // across all three schemes.
    controls.accent_selection_indicator = false;
    // XP's SPI_GETDROPSHADOW block, the same shadow as Aero's, so both eras share one popup
    // shadow.
    theme.popup_shadow = ThemePopupShadow::AeroDropShadow;
    // Luna rounds only the top corners, with a bitmap mask. DWM rounds all four or none, and none
    // is closer.
    theme.chrome.corner_preference = CornerPreference::DoNotRound;
    // `ActiveCaption = 0 84 227`. The caption art is a curved gradient, but
    // DWMWA_CAPTION_COLOR takes one flat color.
    theme.chrome.caption_color      = Color::from_argb(0xFF0054E3);
    theme.chrome.caption_text_color = colors::White; // `CaptionText = 255 255 255`
    // The measured border art brackets the caption color, so one flat DWM color stands in.
    theme.chrome.border_color = Color::from_argb(0xFF0054E3);
    // Luna is entirely opaque. This resolver bypasses Theme::classic(), so it opts out itself.
    theme.chrome.allows_system_backdrop = false;

    // ---- Push button: five cells of the button art, 20 x 115 = 5 x (20 x 23) ----------------
    //
    // `BorderColorHint = 0 60 116`. ButtonControl::render() draws no flat-border fallback, so
    // draw_border must be set or a Rounded visual has no outline.
    const Color border = Color::from_argb(0xFF003C74);

    // The hot ring, measured off the hot cell. It is the
    // whole hover feedback: the face does not change.
    const LinearGradientBrush hot_ring{
        {GradientStop{0.0f, Color::from_argb(0xFFFFF0CF)},
         GradientStop{0.08f, Color::from_argb(0xFFFDD98C)},
         GradientStop{0.75f, Color::from_argb(0xFFF8B330)},
         GradientStop{1.0f, Color::from_argb(0xFFE59700)}}};
    // The defaulted ring, same geometry: Luna's default button glows blue.
    const LinearGradientBrush defaulted_ring{
        {GradientStop{0.0f, Color::from_argb(0xFFCEE7FF)},
         GradientStop{0.08f, Color::from_argb(0xFFBCD4F6)},
         GradientStop{0.75f, Color::from_argb(0xFF89ADE4)},
         GradientStop{1.0f, Color::from_argb(0xFF6982EE)}}};

    auto luna = [&](std::initializer_list<GradientStop> stops, Color flat_fallback, Color text)
    {
        ButtonVisual v;
        v.background          = flat_fallback;
        v.border              = border;
        v.text                = text;
        v.border_style        = BorderStyle::Rounded;
        v.draw_border         = true;
        v.background_gradient = LinearGradientBrush{stops};
        return v;
    };

    // The normal face, measured down cell 1's center column. A real Luna button is a nine-grid
    // (`SizingMargins = 8, 8, 9, 9`), so one gradient is exact only at the era's 23 px height.
    theme.button_normal = luna({GradientStop{0.0f, colors::White},
                                GradientStop{0.15f, Color::from_argb(0xFFFCFCFB)},
                                GradientStop{0.50f, Color::from_argb(0xFFF3F3EF)},
                                GradientStop{0.85f, Color::from_argb(0xFFECEBE6)},
                                GradientStop{0.92f, Color::from_argb(0xFFE2DFD6)},
                                GradientStop{1.0f, Color::from_argb(0xFFD6D0C5)}},
                               Color::from_argb(0xFFF3F3EF), // `FillColorHint = 243 243 239`
                               colors::Black);               // `[button.pushbutton] TextColor = 0 0 0`
    // Hot: the same face plus the ring. The art is pixel-identical inside the ring.
    theme.button_hover                    = theme.button_normal;
    theme.button_hover.inner_border       = hot_ring;
    theme.button_hover.inner_border_width = 2.0f;
    // Pressed: the light direction inverts and the ring goes.
    theme.button_pressed = luna({GradientStop{0.0f, Color::from_argb(0xFFD1CCC1)},
                                 GradientStop{0.10f, Color::from_argb(0xFFDCD8CF)},
                                 GradientStop{0.20f, Color::from_argb(0xFFE6E6E0)},
                                 GradientStop{0.55f, Color::from_argb(0xFFE2E1DA)},
                                 GradientStop{0.85f, Color::from_argb(0xFFE0E0D7)},
                                 GradientStop{1.0f, Color::from_argb(0xFFF2F1EE)}},
                                Color::from_argb(0xFFE2E1DA),
                                colors::Black);
    // Disabled: a flat recolor, never the classic DSS_DISABLED emboss.
    theme.button_disabled              = ButtonVisual{Color::from_argb(0xFFF5F4EA),
                                                      Color::from_argb(0xFFC9C7BA),
                                                      Color::from_argb(0xFFA1A192)};
    theme.button_disabled.border_style = BorderStyle::Rounded;
    theme.button_disabled.draw_border  = true;

    // Luna has no accent button, so Accent maps onto PBS_DEFAULTED. Hot wins over defaulted; no
    // PBS_* state combines the two.
    theme.accent_button_normal                    = theme.button_normal;
    theme.accent_button_normal.inner_border       = defaulted_ring;
    theme.accent_button_normal.inner_border_width = 2.0f;
    theme.accent_button_hover                     = theme.button_hover;
    theme.accent_button_pressed                   = theme.button_pressed;
    theme.accent_button_disabled                  = theme.button_disabled;

    // ---- Toolbar buttons: the toolbar button art, 20 x 138 = 6 x (20 x 23) -----------------
    //
    // Normal and disabled cells are fully keyed out, so XP toolbars read flat.
    theme.subtle_button_normal = ButtonVisual{colors::Transparent, colors::Transparent, colors::Black};
    theme.subtle_button_hover  = ButtonVisual{Color::from_argb(0xFFF4F4EE),
                                              Color::from_argb(0xFFCECEC3), // `BorderColorHint = 206 206 195`
                                              colors::Black};
    theme.subtle_button_hover.border_style        = BorderStyle::Rounded;
    theme.subtle_button_hover.draw_border         = true;
    theme.subtle_button_hover.background_gradient = LinearGradientBrush{
        {GradientStop{0.0f, Color::from_argb(0xFFFEFEFD)},
         GradientStop{0.30f, Color::from_argb(0xFFF6F6F1)},
         GradientStop{0.80f, Color::from_argb(0xFFF1F1EB)},
         GradientStop{0.92f, Color::from_argb(0xFFE4E1D6)},
         GradientStop{1.0f, Color::from_argb(0xFFD7D2C6)}}};
    theme.subtle_button_pressed = ButtonVisual{Color::from_argb(0xFFE2E1D9), Color::from_argb(0xFF9D9D92), colors::Black};
    theme.subtle_button_pressed.border_style        = BorderStyle::Rounded;
    theme.subtle_button_pressed.draw_border         = true;
    theme.subtle_button_pressed.background_gradient = LinearGradientBrush{
        {GradientStop{0.0f, Color::from_argb(0xFFDCDAD1)},
         GradientStop{0.12f, Color::from_argb(0xFFE7E6E0)},
         GradientStop{0.55f, Color::from_argb(0xFFE2E1D9)},
         GradientStop{0.85f, Color::from_argb(0xFFE0E0D7)},
         GradientStop{1.0f, Color::from_argb(0xFFEBEAE5)}}};
    theme.subtle_button_disabled = ButtonVisual{colors::Transparent, colors::Transparent, theme.button_disabled.text};

    // ---- Check box and radio button: 12 and 8 cells of 13 x 13 -------------------------------
    //
    // Green, scheme-invariant glyphs (`FillColorHint = 33 161 33`) in a #1C5180
    // border, not the button's #003C74. The well is a 45 degree ramp that reads as sunken.
    const Color toggle_border = Color::from_argb(0xFF1C5180);
    const Color tick          = Color::from_argb(0xFF21A121);
    auto        well          = [&](Color from, Color to, Color glyph)
    {
        ButtonVisual v;
        v.background          = from;
        v.border              = toggle_border;
        v.text                = glyph;
        v.background_gradient = LinearGradientBrush{{GradientStop{0.0f, from}, GradientStop{1.0f, to}},
                                                    GradientAxis::Diagonal};
        return v;
    };
    const Color   well_top = Color::from_argb(0xFFDCDCD7);
    ToggleVisuals toggles;
    toggles.normal = ToggleVisual{well(well_top, colors::White, colors::Transparent), well(well_top, colors::White, tick)};
    // Hot is not purely additive: the well also flattens and lightens under the ring.
    const LinearGradientBrush toggle_ring{
        {GradientStop{0.0f, Color::from_argb(0xFFFFF0CF)}, GradientStop{1.0f, Color::from_argb(0xFFF8B330)}}};
    ButtonVisual hot_off = well(Color::from_argb(0xFFE7E7E3), Color::from_argb(0xFFE7E7E3), colors::Transparent);
    ButtonVisual hot_on  = well(Color::from_argb(0xFFF7F7F5), Color::from_argb(0xFFF4F4F3), tick);
    for (ButtonVisual* v : {&hot_off, &hot_on})
    {
        v->inner_border       = toggle_ring;
        v->inner_border_width = 2.0f;
    }
    toggles.hover = ToggleVisual{hot_off, hot_on};
    // Pressed: the well darkens, the ring goes, the tick darkens.
    toggles.pressed = ToggleVisual{well(Color::from_argb(0xFFB7B7AD), Color::from_argb(0xFFEDEBDB), colors::Transparent),
                                   well(Color::from_argb(0xFFB7B7AD),
                                        Color::from_argb(0xFFEDEBDB),
                                        Color::from_argb(0xFF1C861A))};
    // Disabled: outline and mark share one color, so the toggle reads as empty.
    const Color disabled_edge = Color::from_argb(0xFFCAC8BB);
    toggles.disabled          = ToggleVisual{ButtonVisual{colors::White, disabled_edge, colors::Transparent},
                                             ButtonVisual{colors::White, disabled_edge, disabled_edge}};
    controls.set_toggle_visuals(toggles);

    // The radio dot is a different green from the tick (the center of a lit sphere), does not
    // darken when pressed, and has its own disabled color.
    const Color dot                    = Color::from_argb(0xFF38B935);
    controls.radio_ring.normal.on.text = controls.radio_ring.hover.on.text = controls.radio_ring.pressed.on.text = dot;
    controls.radio_ring.disabled.on.text = Color::from_argb(0xFFC0BFB5);

    // Mixed has its own 7x7 square mark, in cells 9-12 of the check box art.
    // Its well matches the checked cell, and it has a hot variant of its own.
    auto mixed = [&](const ButtonVisual& cell, Color from, Color to)
    {
        IndeterminateVisual v{cell};
        v.box.text = from;
        ButtonVisual square{from, colors::Transparent, colors::Transparent};
        square.background_gradient = LinearGradientBrush{{GradientStop{0.0f, from}, GradientStop{1.0f, to}},
                                                         GradientAxis::Diagonal};
        v.mark                     = square;
        return v;
    };
    const Color mixed_hot      = tick;
    const Color mixed_disabled = disabled_edge;
    controls.check_box_indeterminate = IndeterminateVisuals{mixed(controls.check_box_box.normal.on,
                                                                  Color::from_argb(0xFF6DBB6C),
                                                                  Color::from_argb(0xFF76C576)),
                                                            mixed(controls.check_box_box.hover.on, mixed_hot, mixed_hot),
                                                            mixed(controls.check_box_box.pressed.on,
                                                                  Color::from_argb(0xFF187416),
                                                                  Color::from_argb(0xFF1F941C)),
                                                            mixed(controls.check_box_box.disabled.on, mixed_disabled, mixed_disabled),
                                                            IndeterminateMark::Square,
                                                            glyphs::CheckBoxIndeterminate,
                                                            7.0f};

    // ---- ToggleSwitch: a control Windows XP does not have ------------------------------------
    //
    // Luna has no switch, so this is assembled from era values. 0.15 of the 20 px track is 3,
    // the push button's own arc, where the inherited 0.5 would draw a Fluent capsule.
    controls.toggle_switch_track_radius_ratio = 0.15f;

    // ---- TrackBar: the horizontal thumb art, 11 x 105 = 5 x (11 x 21) ------------------------
    //
    // A rounded-rect thumb with a 1 px bevel and colored end bands. The bands carry the state
    // and the body never changes, hence slider_chrome. The long axis runs across the track.
    controls.slider_thumb.shape = SliderThumbShape::RoundedRect;
    controls.slider_thumb.outer = 21.0f;
    controls.slider_thumb.rest = controls.slider_thumb.hover = controls.slider_thumb.pressed = 11.0f;
    controls.slider_thumb.radius                                                             = 1.0f;
    // The track is 4 px, as the art's nine-grid (`SizingMargins = 2, 2, 2, 2`) has it.
    controls.slider_track_thickness = 4.0f;
    // No value fill: `[TrackBar.Track] imagecount = 1`, the track has no states at all.
    controls.slider_value_fill = false;
    // `[TrackBar.Thumb] FillColorHint` and `[TrackBar.Track] FillColorHint`, both
    // exact against the art.
    const Color track_fill = Color::from_argb(0xFFF2F1E9);
    controls.slider_fills  = detail::ControlChrome::SliderFills{Color::from_argb(0xFFF3F3EF),
                                                                Color::from_argb(0xFFF3F3EF),
                                                                Color::from_argb(0xFFF3F3EF),
                                                                Color::from_argb(0xFFF5F4EA),
                                                                track_fill,
                                                                track_fill,
                                                                track_fill};
    detail::ControlChrome::SliderChrome chrome;
    // The saturated row of each band. `AccentColorHint = 71 196 70` matches the rest band.
    chrome.rest = {Color::from_argb(0xFF48CB46), Color::from_argb(0xFF1FAE1D)};
    // Hot is amber. The focused cell equals the hot cell, so focus shows only as the dotted
    // rectangle on top.
    chrome.hover       = {Color::from_argb(0xFFFAC158), Color::from_argb(0xFFE2A330)};
    chrome.pressed     = {Color::from_argb(0xFF48A73B), Color::from_argb(0xFF1F8710)};
    chrome.disabled    = {Color::from_argb(0xFFD2D0C6), Color::from_argb(0xFFC2C0B3)};
    chrome.band_extent = 2.0f;
    chrome.band_inset  = 1.0f;
    // `BorderColorHint = 181 196 205` is the lit edge exactly; the shaded one is measured.
    chrome.thumb_edge_light          = Color::from_argb(0xFFB5C4CD);
    chrome.thumb_edge_dark           = Color::from_argb(0xFF778892);
    chrome.thumb_edge_light_disabled = Color::from_argb(0xFFD6D4C6);
    chrome.thumb_edge_dark_disabled  = Color::from_argb(0xFFBFBCAD);
    // The channel is sunken: `[TrackBar.Track] BorderColorHint = 157 156 153` top-left.
    chrome.track_edge_dark  = Color::from_argb(0xFF9D9C99);
    chrome.track_edge_light = colors::White;
    // `[TrackBar.Tics] color = 161 161 146`, the disabled-text gray, in every state.
    chrome.tick            = Color::from_argb(0xFFA1A192);
    controls.slider_chrome = chrome;

    // ---- Severity, and the era states the rule ------------------------------------------------
    //
    // The era names blue, green, yellow and red for neutral, start, less severe and high impact.
    // Each is the color of the Luna part it points to; surfaces stay white.
    const Color banner_surface   = colors::White;
    theme.severity.informational = Theme::SeverityVisual{Color::from_argb(0xFF316AC5), banner_surface};
    theme.severity.success       = Theme::SeverityVisual{tick, banner_surface};
    theme.severity.caution       = Theme::SeverityVisual{Color::from_argb(0xFFDFA550), banner_surface};
    theme.severity.critical      = Theme::SeverityVisual{Color::from_argb(0xFFE45D3D), banner_surface};
    theme.severity.text_on_fill  = colors::White; // "adding a white sign to the colored background"
    theme.severity.stroke        = theme.surfaces.field_border;

    // ---- Progress: `[Progress]` and `[Progress.Bar]`/`[Progress.Chunk]` ------------------------
    //
    // `[Progress] fillColor = 46 211 49`, fixed. The 6 px chunks are not modeled.
    controls.progress_track = colors::White;
    controls.progress_fill  = Color::from_argb(0xFF2ED331);

    apply_body_type_ramp(theme);

    // CalendarView as XP's month calendar: comctl32's classic design in Luna's system colors.
    {
        detail::ControlChrome::CalendarChrome calendar;
        calendar.kind            = detail::ControlChrome::CalendarChrome::Kind::Classic;
        calendar.background      = colors::White;
        calendar.title_fill      = Color::from_argb(0xFF0054E3); // ACTIVECAPTION
        calendar.title_text      = colors::White;
        calendar.weekday_text    = Color::from_argb(0xFF0054E3);
        calendar.separator       = colors::Black;
        calendar.item_text       = colors::Black;
        calendar.trailing_text   = Color::from_argb(0xFFACA899); // GRAYTEXT
        calendar.selected_fill   = Color::from_argb(0xFF0054E3);
        calendar.today_mark      = Color::from_argb(0xFFFF0000);
        calendar.footer_text     = colors::Black;
        calendar.blackout_text   = Color::from_argb(0xFFACA899);
        calendar.disabled_text   = Color::from_argb(0xFFACA899);
        calendar.cell_width      = 24.0f;
        controls.calendar_chrome = calendar;
    }

    // DatePicker and TimePicker: comctl32's date and time picker.
    detail::edit_controls(theme).date_time_picker_chrome = detail::ControlChrome::DateTimePickerChrome{
        .kind = detail::ControlChrome::DateTimePickerChrome::Kind::Field};
    return theme;
}

// ---- Aero: Windows Vista & 7 merged -----------------------------------------------------
//
// Neutral gray at rest, blue only on interaction, no accent and no glass.
Theme resolve_aero()
{
    Theme                  theme;
    detail::ControlChrome& controls = detail::edit_controls(theme);
    theme.variant                   = ThemeVariant::Aero;
    // The frame a Frame::Themed window draws: the opaque Basic frame, since glass needs the desktop
    // behind the window blurred (AeroWindowFrame.cpp).
    controls.window_frame = detail::aero_basic_window_frame();
    // The button border is stroked at radius 2.75; the inner ring at 1.75 follows.
    theme.corner_radius = 2.75f;
    // The themed ToolTip's radius. Menus and drop-downs are square.
    theme.overlay_corner_radius = 2.0f;
    theme.border_width          = 1.0f;
    // `Btnface` 240 240 240.
    detail::edit_colors(theme).background = Color::from_argb(0xFFF0F0F0);
    detail::edit_colors(theme).text       = colors::Black;
    detail::edit_colors(theme).focus      = colors::Black; // COLOR_BTNTEXT, the focus rectangle's stroke
    theme.focus_visual                    = FocusVisual::ClassicDotted;
    // ClearType text. Text on glass would be grayscale, but this pack draws no glass.
    theme.rasterization = SetRasterization{GeometryAA::Antialiased, TextAA::Subpixel, /*snap_to_device_pixels=*/false};
    detail::edit_colors(theme).field      = colors::White;                // `Window` 255 255 255
    detail::edit_colors(theme).field_text = colors::Black;                // `WindowText`
    theme.surfaces.field_border           = Color::from_argb(0xFFABADB3); // `EditStyle`/`ComboboxStyle` BorderColorHint
    detail::edit_colors(theme).selection = Color::from_argb(0xFF3399FF); // `Highlight` 51 153 255, identical in Vista and 7
    detail::edit_colors(theme).selection_text = colors::White;           // `HighlightText`
    detail::edit_colors(theme).text_disabled  = Color::from_argb(
        0xFF6D6D6D); // `GrayText` (7; Vista's `#808080` is the whole era delta)
    theme.surfaces.menu        = Color::from_argb(0xFFF5F5F5); // the submenu background; the Win32 hint is `#F4F4F4`
    theme.surfaces.menu_text   = colors::Black;
    theme.surfaces.menu_border = Color::from_argb(0xFF959595); // the popup border
    // One flat color for a 20 % wash plus a stroke: the stroke composited over the popup, since
    // the wash alone is invisible. Win32's `MenuHilight` #3399FF is the flat-menu color.
    theme.surfaces.menu_highlight      = Color::from_argb(0xFFB3E0F3);
    theme.surfaces.menu_highlight_text = colors::Black;
    // The themed tooltip, darker stop, since ToolTipControl::render() draws no stroke.
    // #FFFFE1 is the unthemed COLOR_INFOBK.
    theme.surfaces.tooltip          = Color::from_argb(0xFFE4E5F0);
    theme.surfaces.tooltip_text     = Color::from_argb(0xFF575757);
    detail::edit_colors(theme).link = Color::from_argb(0xFF0066CC); // `HyperLinkText` == `HotTracking`
    theme.surfaces.separator_light  = colors::White;
    theme.surfaces.separator_dark   = Color::from_argb(0xFF828790); // `ToolbarStyle` · `Separator` FillColorHint
    // SplitterControl::render() reads divider unconditionally. The era has no flat-rule token, so the
    // groove's dark half stands in.
    detail::edit_colors(theme).divider = Color::from_argb(0xFF828790);
    theme.surfaces.scrollbar_track     = Color::from_argb(0xFFF4F4F4); // `ScrollbarStyle` track FillColorHint
    // Gray at rest, blue only on hover: where the thumb's split rest ramp meets at 50 %.
    theme.surfaces.scrollbar_thumb  = Color::from_argb(0xFFDFDFE1);
    theme.surfaces.scrollbar_arrow  = colors::Black;
    theme.padding                   = Symmetric{.horizontal = 14.0f, .vertical = 5.0f};
    controls.toggle_box_size        = 13.0f; // the era's check/radio glyph is 13 x 13
    controls.toggle_content_spacing = 5.0f;
    theme.text_style.font_family    = "Segoe UI";
    // `CaptionFont`/`MenuFont`/`MsgBoxFont` are Segoe UI 9, 12 epx at 96 dpi.
    theme.text_style.font_size = 12.0f;
    // Hover in 0.3 s, out 0.2 s, press 0.1 s. One slot takes the
    // hover-in duration.
    theme.anim_faster              = std::chrono::milliseconds{300};
    theme.chrome.corner_preference = CornerPreference::DoNotRound;
    // Aero's glass is a frame material, and Mica is a different one. An unpainted Aero page would
    // take the wallpaper tint instead of #F0F0F0.
    theme.chrome.allows_system_backdrop = false;
    // The one shadow Aero owns is the CS_DROPSHADOW block under a popup.
    theme.popup_shadow = ThemePopupShadow::AeroDropShadow;

    // A focused field turns its border blue with no dotted rectangle. #3D7BAD is the top of the
    // era's focus gradient, laid over the whole outline.
    controls.field_focus_border_thickness = Thickness{1.0f};
    controls.field_focus_underline        = Color::from_argb(0xFF3D7BAD);

    // Rest, hover and focus border gradients, darkest on the top edge. Hover switches at once
    // instead of fading in over 0.3 s.
    auto field_border = [](Color top, Color side, Color bottom)
    { return LinearGradientBrush{{GradientStop{0.05f, top}, GradientStop{0.07f, side}, GradientStop{1.0f, bottom}}}; };
    controls.field_border_states = detail::ControlChrome::
        FieldBorderStates{field_border(Color::from_argb(0xFFABADB3), Color::from_argb(0xFFE2E3EA), Color::from_argb(0xFFE3E9EF)),
                          field_border(Color::from_argb(0xFF5794BF), Color::from_argb(0xFFB7D5EA), Color::from_argb(0xFFC7E2F1)),
                          field_border(Color::from_argb(0xFF3D7BAD), Color::from_argb(0xFFA4C9E3), Color::from_argb(0xFFB7D9ED)),
                          20.0f};
    controls.field_corner_radius = 1.0f;
    controls.field_fills = detail::ControlChrome::FieldFills{colors::White, colors::White, Color::from_argb(0xFFF4F4F4)};

    // Face gradient, outer border, white inner ring, label. ButtonControl::render() draws no flat-border
    // fallback, so draw_border must be set.
    const Color border = Color::from_argb(0xFF707070);

    // The white inner highlight, `#FAFFFFFF` at the top to `#85FFFFFF` at the bottom.
    const LinearGradientBrush inner_highlight{{GradientStop{0.0f, Color::from_rgba(255, 255, 255, 0xFA)},
                                               GradientStop{1.0f, Color::from_rgba(255, 255, 255, 0x85)}}};

    // The hard 50 % split is the Aero gloss. One linear gradient over the full height loses it.
    auto aero = [&](Color top, Color upper_mid, Color lower_mid, Color bottom, Color border_color, Color text)
    {
        ButtonVisual v;
        v.background          = lower_mid; // flat fallback
        v.border              = border_color;
        v.text                = text;
        v.border_style        = BorderStyle::Rounded;
        v.draw_border         = true;
        v.background_gradient = LinearGradientBrush{
            {GradientStop{0.0f, top}, GradientStop{0.5f, upper_mid}, GradientStop{0.5f, lower_mid}, GradientStop{1.0f, bottom}}};
        v.inner_border = inner_highlight;
        return v;
    };

    // Rest: the normal gradient, border #707070. The Win32 hint #8E8F8F is used on the
    // check box instead; which one real Vista/7 drew is unsettled.
    theme.button_normal = aero(Color::from_argb(0xFFF3F3F3),
                               Color::from_argb(0xFFEBEBEB),
                               Color::from_argb(0xFFDDDDDD),
                               Color::from_argb(0xFFCDCDCD),
                               border,
                               colors::Black);
    // Hover: blue overlay and a #3C7FB1 border pen.
    theme.button_hover = aero(Color::from_argb(0xFFEAF6FD),
                              Color::from_argb(0xFFD9F0FC),
                              Color::from_argb(0xFFBEE6FD),
                              Color::from_argb(0xFFA7D9F5),
                              Color::from_argb(0xFF3C7FB1),
                              colors::Black);
    // Pressed: deeper blue, no white ring, 2 px inner shadows top and left. The label stays put.
    theme.button_pressed = aero(Color::from_argb(0xFFC2E4F6),
                                Color::from_argb(0xFFC2E4F6),
                                Color::from_argb(0xFFABDAF3),
                                Color::from_argb(0xFF90CBEB),
                                Color::from_argb(0xFF2C628B),
                                colors::Black);
    theme.button_pressed.inner_border.reset();
    theme.button_pressed.inner_shadow = Color::from_argb(0x80333333);
    // Disabled: flat #F4F4F4, #ADB2B5 border, white ring kept.
    theme.button_disabled              = ButtonVisual{Color::from_argb(0xFFF4F4F4),
                                                      Color::from_argb(0xFFADB2B5),
                                                      Color::from_argb(0xFFADADAD)};
    theme.button_disabled.border_style = BorderStyle::Rounded;
    theme.button_disabled.draw_border  = true;
    theme.button_disabled.inner_border = inner_highlight;

    // Accent maps onto the defaulted button: its inner ring turns cyan. The defaulted button's
    // 2 s pulse is not modeled, Theme has no looping-animation slot.
    const LinearGradientBrush defaulted_ring{{GradientStop{0.0f, Color::from_rgba(0x00, 0xCC, 0xFF, 0xF9)},
                                              GradientStop{1.0f, Color::from_rgba(0x00, 0xCC, 0xFF, 0xF9)}}};
    theme.accent_button_normal              = theme.button_normal;
    theme.accent_button_normal.inner_border = defaulted_ring;
    theme.accent_button_hover               = theme.button_hover;
    theme.accent_button_hover.inner_border  = defaulted_ring;
    theme.accent_button_pressed             = theme.button_pressed;
    theme.accent_button_disabled            = theme.button_disabled;

    // Toolbar buttons are flat until hovered, then take the full chrome.
    theme.subtle_button_normal   = ButtonVisual{colors::Transparent, colors::Transparent, colors::Black};
    theme.subtle_button_hover    = theme.button_hover;
    theme.subtle_button_pressed  = theme.button_pressed;
    theme.subtle_button_disabled = ButtonVisual{colors::Transparent, colors::Transparent, theme.button_disabled.text};

    // The check box stays light when checked: a #31347C glyph in a #F4F4F4 well. Hover and press
    // tint the well.
    const Color glyph       = Color::from_argb(0xFF31347C);
    const Color well_border = Color::from_argb(0xFF8E8F8F);
    // The radio is the same well, round, with the mark as a dot.
    const ButtonVisual off = ButtonVisual{Color::from_argb(0xFFF4F4F4), well_border, colors::Transparent};
    const ButtonVisual on  = ButtonVisual{Color::from_argb(0xFFF4F4F4), well_border, glyph};
    controls.set_toggle_visuals(
        ToggleVisuals{ToggleVisual{off, on},
                      ToggleVisual{ButtonVisual{Color::from_argb(0xFFDEF9FA), Color::from_argb(0xFF3C7FB1), colors::Transparent},
                                   ButtonVisual{Color::from_argb(0xFFDEF9FA), Color::from_argb(0xFF3C7FB1), glyph}},
                      ToggleVisual{ButtonVisual{Color::from_argb(0xFFC2E4F6), Color::from_argb(0xFF2C628B), colors::Transparent},
                                   ButtonVisual{Color::from_argb(0xFFC2E4F6), Color::from_argb(0xFF2C628B), glyph}},
                      ToggleVisual{ButtonVisual{Color::from_argb(0xFFF4F4F4), Color::from_argb(0xFFADB2B5), colors::Transparent},
                                   ButtonVisual{Color::from_argb(0xFFF4F4F4),
                                                Color::from_argb(0xFFADB2B5),
                                                theme.colors().text_disabled}}});

    // Mixed is a filled blue square, the UX Guide's "solid blue" box. Its inner border and
    // highlight run diagonally in the art; draw_control_chrome strokes them top to bottom.
    auto aero_mixed =
        [&](const ButtonVisual& well, Color fill_from, Color fill_to, std::array<Color, 3> edge, LinearGradientBrush highlight)
    {
        ButtonVisual square{fill_from, edge[1], fill_from};
        square.background_gradient = LinearGradientBrush{{GradientStop{0.2f, fill_from}, GradientStop{0.8f, fill_to}},
                                                         GradientAxis::Diagonal};
        square.border_gradient     = LinearGradientBrush{
            {GradientStop{0.0f, edge[0]}, GradientStop{0.5f, edge[1]}, GradientStop{1.0f, edge[2]}}};
        square.border_gradient_extent_px = 8.0f; // the 9 px square's stroke path, top to bottom
        square.border_width              = 1.0f;
        square.inner_border              = std::move(highlight);
        IndeterminateVisual v{well};
        v.box.text = fill_from;
        v.mark     = square;
        return v;
    };
    const Color         clear_white = Color::from_rgba(0xFF, 0xFF, 0xFF, 0x00);
    const Color         clear_navy  = Color::from_rgba(0x33, 0x33, 0xA0, 0x00);
    LinearGradientBrush mixed_highlight{
        {GradientStop{0.0f, Color::from_rgba(0xFF, 0xFF, 0xFF, 0x80)},
         GradientStop{0.5f, clear_white},
         GradientStop{0.5f, clear_navy},
         GradientStop{1.0f, clear_navy}}};
    LinearGradientBrush mixed_highlight_hot = mixed_highlight;
    mixed_highlight_hot.stops.back().color  = Color::from_rgba(0x33, 0x33, 0xA0, 0x80);
    const LinearGradientBrush mixed_highlight_pressed{
        {GradientStop{0.0f, clear_white},
         GradientStop{0.5f, clear_white},
         GradientStop{1.0f, Color::from_rgba(0xFF, 0xFF, 0xFF, 0x20)}}};
    const std::array<Color, 3> mixed_edge{Color::from_argb(0xFF2A628D),
                                          Color::from_argb(0xFF245479),
                                          Color::from_argb(0xFF193B55)};
    controls.check_box_indeterminate = IndeterminateVisuals{
        aero_mixed(controls.check_box_box.normal.off,
                   Color::from_argb(0xFF2FA8D5),
                   Color::from_argb(0xFF25598C),
                   mixed_edge,
                   mixed_highlight),
        aero_mixed(controls.check_box_box.hover.off,
                   Color::from_argb(0xFF33D7ED),
                   Color::from_argb(0xFF2094CE),
                   {Color::from_argb(0xFF29628D), mixed_edge[1], mixed_edge[2]},
                   mixed_highlight_hot),
        aero_mixed(controls.check_box_box.pressed.off,
                   Color::from_argb(0xFF17447A),
                   Color::from_argb(0xFF218BC3),
                   {mixed_edge[2], mixed_edge[1], Color::from_argb(0xFF29628D)},
                   mixed_highlight_pressed),
        aero_mixed(controls.check_box_box.disabled.off,
                   Color::from_argb(0xFFC0E5F3),
                   Color::from_argb(0xFFBDCDDC),
                   {Color::from_argb(0xFFBFD0DD), Color::from_argb(0xFFBDCBD7), Color::from_argb(0xFFBAC4CC)},
                   mixed_highlight),
        IndeterminateMark::Square,
        glyphs::CheckBoxIndeterminate,
        9.0f};

    // The well is recessed: a diagonal rim and inner fill. Disabled keeps the rim only, and the
    // mixed square covers both. The box is stroked square.
    controls.toggle_box_radius = 0.0f;
    auto aero_well             = [](std::array<GradientStop, 3> rim, std::optional<std::array<Color, 2>> fill)
    {
        InsetWell well{LinearGradientBrush{{rim[0], rim[1], rim[2]}, GradientAxis::Diagonal}};
        if (fill)
        {
            well.fill = LinearGradientBrush{{GradientStop{0.2f, (*fill)[0]}, GradientStop{0.8f, (*fill)[1]}},
                                            GradientAxis::Diagonal};
        }
        return well;
    };
    const std::array<InsetWell, 4> wells{
        aero_well({GradientStop{0.25f, Color::from_argb(0xFFAEB3B9)},
                   GradientStop{0.5f, Color::from_argb(0xFFC2C4C6)},
                   GradientStop{1.0f, Color::from_argb(0xFFEAEBEB)}},
                  std::array{Color::from_argb(0xFFCBCFD5), Color::from_argb(0xFFF7F7F7)}),
        aero_well({GradientStop{0.3f, Color::from_argb(0xFF79C6F9)},
                   GradientStop{0.5f, Color::from_argb(0xFF79C6F9)},
                   GradientStop{1.0f, Color::from_argb(0xFFD2EDFD)}},
                  std::array{Color::from_argb(0xFFB1DFFD), Color::from_argb(0xFFE9F7FE)}),
        aero_well({GradientStop{0.3f, Color::from_argb(0xFF54A6D5)},
                   GradientStop{0.5f, Color::from_argb(0xFF5EB5E4)},
                   GradientStop{1.0f, Color::from_argb(0xFFC4E5F6)}},
                  std::array{Color::from_argb(0xFF7FBADC), Color::from_argb(0xFFD6EDF9)}),
        aero_well({GradientStop{0.25f, Color::from_argb(0xFFE1E3E5)},
                   GradientStop{0.5f, Color::from_argb(0xFFE8E9EA)},
                   GradientStop{1.0f, Color::from_argb(0xFFF3F3F3)}},
                  std::nullopt),
    };
    ToggleVisual* box_states[] = {&controls.check_box_box.normal,
                                  &controls.check_box_box.hover,
                                  &controls.check_box_box.pressed,
                                  &controls.check_box_box.disabled};
    for (std::size_t i = 0; i < wells.size(); ++i)
    {
        box_states[i]->off.inset_well = wells[i];
        box_states[i]->on.inset_well  = wells[i];
    }
    // A disabled tick is #AEB7CF, a grayed check blue, not the label's GrayText.
    controls.check_box_box.disabled.on.text = Color::from_argb(0xFFAEB7CF);

    // The radio dot is a lit blue bead: a radial fill inside a dark #193B55 ring, 7 px on the
    // 13 px glyph. Pressing an unchecked radio previews the pressed dot.
    controls.radio_dot.rest = controls.radio_dot.hover = controls.radio_dot.pressed = 7.0f;
    auto radio_well = [](Color rim_from, Color rim_to, std::optional<std::array<Color, 2>> fill)
    {
        InsetWell well{
            LinearGradientBrush{{GradientStop{0.0f, rim_from}, GradientStop{1.0f, rim_to}}, GradientAxis::Diagonal}};
        if (fill)
        {
            well.fill = LinearGradientBrush{{GradientStop{0.2f, (*fill)[0]}, GradientStop{0.8f, (*fill)[1]}},
                                            GradientAxis::Diagonal};
        }
        return well;
    };
    auto bead = [](std::array<GradientStop, 3> stops)
    {
        // Center and gradient origin at (0.25, 0.25), both radii 0.75.
        return RadialGradientBrush{{stops[0], stops[1], stops[2]}, Point{0.25f, 0.25f}, 0.75f, 0.75f, Point{0.25f, 0.25f}};
    };
    const RadialGradientBrush bead_pressed = bead(
        {GradientStop{0.0f, Color::from_argb(0xFF95D9FC)},
         GradientStop{0.35f, Color::from_argb(0xFF3A84AA)},
         GradientStop{1.0f, Color::from_argb(0xFF075483)}});
    const Color bead_ring = Color::from_argb(0xFF193B55);
    struct RadioState
    {
        ToggleVisual*       visual;
        InsetWell           well;
        RadialGradientBrush dot;
        Color               ring;
    };
    const std::array<RadioState, 4> radio_states{
        RadioState{&controls.radio_ring.normal,
                   radio_well(Color::from_argb(0xFFB3B8BD),
                              Color::from_argb(0xFFEBEBEB),
                              std::array{Color::from_argb(0xFFCBCFD5), Color::from_argb(0xFFF7F7F7)}),
                   bead({GradientStop{0.1f, Color::from_argb(0xFFE5E5E5)},
                         GradientStop{0.35f, Color::from_argb(0xFF5DCEDD)},
                         GradientStop{1.0f, Color::from_argb(0xFF0B82C7)}}),
                   bead_ring},
        RadioState{&controls.radio_ring.hover,
                   radio_well(Color::from_argb(0xFF80CAF9),
                              Color::from_argb(0xFFD2EEFD),
                              std::array{Color::from_argb(0xFFB1DFFD), Color::from_argb(0xFFE9F7FE)}),
                   bead({GradientStop{0.1f, colors::White},
                         GradientStop{0.35f, Color::from_argb(0xFF74FFFF)},
                         GradientStop{1.0f, Color::from_argb(0xFF0DA0F3)}}),
                   bead_ring},
        RadioState{&controls.radio_ring.pressed,
                   radio_well(Color::from_argb(0xFF5CAAD7),
                              Color::from_argb(0xFFC3E4F6),
                              std::array{Color::from_argb(0xFF7FBADC), Color::from_argb(0xFFD6EDF9)}),
                   bead_pressed,
                   bead_ring},
        RadioState{&controls.radio_ring.disabled,
                   radio_well(Color::from_argb(0xFFE1E3E5), Color::from_argb(0xFFF3F3F3), std::nullopt),
                   bead({GradientStop{0.0f, Color::from_argb(0xFFC9D5DE).with_alpha(0.7f)},
                         GradientStop{0.35f, Color::from_argb(0xFFC0E3E8).with_alpha(0.7f)},
                         GradientStop{1.0f, Color::from_argb(0xFFB0D4E9).with_alpha(0.7f)}}),
                   Color::from_argb(0xFFA2AEB9)},
    };
    for (const RadioState& s : radio_states)
    {
        s.visual->off.inset_well   = s.well;
        s.visual->on.inset_well    = s.well;
        s.visual->on.mark_gradient = s.dot;
        s.visual->on.mark_stroke   = s.ring;
    }
    controls.radio_ring.pressed.off.mark_gradient = bead_pressed;
    controls.radio_ring.pressed.off.mark_stroke   = bead_ring;

    // Aero's Slider: an 11x22 glossy block over a 4 px groove, with no value fill (a Win32
    // trackbar has none). One-sided ticks turn the block into a pointer.
    controls.slider_thumb.shape = SliderThumbShape::ButtonBlock;
    controls.slider_thumb.outer = 22.0f;
    controls.slider_thumb.rest = controls.slider_thumb.hover = controls.slider_thumb.pressed = 11.0f;
    controls.slider_track_thickness                                                          = 4.0f;
    // Aero's scroll bar: the track and the part chrome.
    {
        const auto rgb = [](std::uint32_t argb) { return Color::from_argb(0xFF000000 | argb); };
        detail::ControlChrome::GutterScrollBar bar;
        bar.track                  = {GradientStop{0.0f, rgb(0xE1E1E1)},
                                      GradientStop{0.2f, rgb(0xEDEDED)},
                                      GradientStop{0.8f, rgb(0xEDEDED)},
                                      GradientStop{1.0f, rgb(0xE3E3E3)}};
        bar.track_disabled         = rgb(0xF4F4F4);
        bar.corner                 = rgb(0xF0F0F0); // the scroll corner, COLOR_BTNFACE
        bar.rest                   = {{GradientStop{0.0f, rgb(0xF3F3F3)},
                                       GradientStop{0.5f, rgb(0xE8E8E9)},
                                       GradientStop{0.5f, rgb(0xD6D6D8)},
                                       GradientStop{1.0f, rgb(0xBCBDC0)}},
                                      rgb(0x959595),
                                      rgb(0x212121)};
        bar.hover                  = {{GradientStop{0.0f, rgb(0xE3F4FC)},
                                       GradientStop{0.5f, rgb(0xD6EEFB)},
                                       GradientStop{0.5f, rgb(0xA9DBF6)},
                                       GradientStop{1.0f, rgb(0xA4D5EF)}},
                                      rgb(0x3C7FB1),
                                      rgb(0x0D2A3A)};
        bar.pressed                = {{GradientStop{0.0f, rgb(0xCAECF9)},
                                       GradientStop{0.5f, rgb(0xAFE1F7)},
                                       GradientStop{0.5f, rgb(0x6FCAF0)},
                                       GradientStop{1.0f, rgb(0x66BADD)}},
                                      rgb(0x15598A),
                                      rgb(0x0E222D)};
        bar.idle_arrow             = rgb(0x707070);
        bar.inner_border           = colors::White.with_alpha(0.63f);
        bar.shadow                 = rgb(0xCFCFCF);
        bar.gripper_rest           = {GradientStop{0.5f, rgb(0x000000)},
                                      GradientStop{0.7f, rgb(0x979797)},
                                      GradientStop{1.0f, rgb(0xCACACA)}};
        bar.gripper_hover          = {GradientStop{0.5f, rgb(0x15303E)},
                                      GradientStop{0.7f, rgb(0x3C7FB1)},
                                      GradientStop{1.0f, rgb(0x9CCEE9)}};
        bar.gripper_pressed        = {GradientStop{0.5f, rgb(0x0F2430)},
                                      GradientStop{0.7f, rgb(0x2E7397)},
                                      GradientStop{1.0f, rgb(0x8FB8CE)}};
        bar.gripper_shadow         = colors::White.with_alpha(0.63f);
        controls.gutter_scroll_bar = std::move(bar);
    }
    // An up-down control's default width is SM_CXVSCROLL, the same 17 as the scroll bar.
    controls.stacked_spinner_width = 17.0f;
    // The desktop edit control: no clear button, and no spin-button popup.
    controls.spin_button_popup      = false;
    controls.text_box_delete_button = false;
    {
        // Aero's menu bar, menus and context menus.
        detail::ControlChrome::EraMenu menu;
        const auto                     argb = [](std::uint32_t v) { return Color::from_argb(v); };
        menu.bar_background                 = {GradientStop{0.0f, argb(0xFFF6F6F6)},
                                               GradientStop{0.25f, argb(0xFFF6F6F6)},
                                               GradientStop{0.25f, argb(0xFFEAE8E8)},
                                               GradientStop{0.8f, argb(0xFFDCD9D9)},
                                               GradientStop{1.0f, argb(0xFFFFFFFF)}};
        menu.bar_item_padding               = Edges{.left = 7.0f, .top = 2.0f, .right = 8.0f, .bottom = 3.0f};
        menu.bar_hover_stroke               = argb(0x90717070);
        menu.bar_hover_light                = argb(0x50FFFFFF);
        menu.bar_open_fill                  = {GradientStop{0.0f, argb(0x28717070)},
                                               GradientStop{0.75f, argb(0x50717070)},
                                               GradientStop{1.0f, argb(0x90717070)}};
        menu.bar_open_stroke                = argb(0xE0717070);
        menu.bar_open_inner                 = argb(0x50747272);
        // The scroll area's 1,0 and the items' 2.
        menu.popup_padding               = Symmetric{.horizontal = 3.0f, .vertical = 2.0f};
        menu.item_padding                = Symmetric{.horizontal = 2.0f, .vertical = 3.0f};
        menu.gutter                      = argb(0xFFF1F1F1);
        menu.gutter_dark                 = argb(0xFFE2E3E3);
        menu.gutter_light                = colors::White;
        menu.hot_fill                    = {GradientStop{0.0f, argb(0x34C5EBFF)}, GradientStop{1.0f, argb(0x3481D8FF)}};
        menu.hot_stroke                  = argb(0x8071CBF1);
        menu.hot_inner                   = argb(0x40FFFFFF);
        menu.check_panel                 = argb(0xFFE6EFF4);
        menu.check_panel_border          = argb(0xFFCDD3E6);
        menu.check                       = argb(0xFF0C12A1);
        menu.check_panel_disabled        = argb(0xFFEEE9E9);
        menu.check_panel_border_disabled = argb(0xFFDBD6D6);
        menu.check_disabled              = argb(0xFF848589);
        menu.disabled_text               = argb(0xFF9A9A9A);
        menu.separator_dark              = argb(0xFFE0E0E0);
        menu.separator_light             = colors::White;
        controls.era_menu                = std::move(menu);
    }
    {
        // Aero's tabs.
        detail::ControlChrome::EraTabs tabs;
        const auto                     argb = [](std::uint32_t v) { return Color::from_argb(v); };
        tabs.face                           = {GradientStop{0.0f, argb(0xFFF3F3F3)},
                                               GradientStop{0.5f, argb(0xFFEBEBEB)},
                                               GradientStop{0.5f, argb(0xFFDDDDDD)},
                                               GradientStop{1.0f, argb(0xFFCDCDCD)}};
        tabs.hover_face                     = {GradientStop{0.15f, argb(0xFFEAF6FD)},
                                               GradientStop{0.5f, argb(0xFFD9F0FC)},
                                               GradientStop{0.5f, argb(0xFFBEE6FD)},
                                               GradientStop{1.0f, argb(0xFFA7D9F5)}};
        tabs.border                         = argb(0xFF8C8E94);
        tabs.hover_border                   = argb(0xFF3C7FB1);
        tabs.disabled_face                  = argb(0xFFF4F4F4);
        tabs.disabled_border                = argb(0xFFC9C7BA);
        tabs.page                           = argb(0xFFF9F9F9);
        controls.era_tabs                   = std::move(tabs);
    }
    {
        // BP_GROUPBOX: a 1 px `#D5DFE5` frame with one diagonal step at each corner and a black
        // title, which themed comctl32 draws 12 px in with a 2 px gap either side, the frame's top
        // at its middle. Controls start 16 px down, 9 px in and end 11 px above the bottom.
        detail::ControlChrome::EraGroupBox box;
        box.frame              = Color::from_argb(0xFFD5DFE5);
        box.radius             = 2.0f;
        box.header             = colors::Black;
        box.header_x           = 12.0f;
        box.header_gap         = 2.0f;
        box.padding            = Edges{.left = 9.0f, .top = 16.0f, .right = 9.0f, .bottom = 11.0f};
        controls.era_group_box = box;
    }
    controls.slider_value_fill = false;
    {
        const LinearGradientBrush white_ring{{GradientStop{0.0f, colors::White}, GradientStop{1.0f, colors::White}}};
        auto                      block = [&](std::array<GradientStop, 3> face, Color outline)
        {
            ButtonVisual v{face[2].color, outline, colors::Transparent};
            v.background_gradient = LinearGradientBrush{{face[0], face[1], face[2]}};
            v.inner_border        = white_ring;
            v.draw_border         = true;
            return v;
        };
        ButtonVisual disabled{Color::from_argb(0xFFF4F4F4), Color::from_argb(0xFFAEB1AF), colors::Transparent};
        disabled.draw_border  = true;
        controls.slider_block = detail::ControlChrome::
            SliderBlock{block({GradientStop{0.0f, colors::White},
                               GradientStop{0.4f, Color::from_argb(0xFFF0EFEF)},
                               GradientStop{0.8f, Color::from_argb(0xFFD6D5D5)}},
                              Color::from_argb(0xFF929292)),
                        block({GradientStop{0.0f, colors::White},
                               GradientStop{0.5f, Color::from_argb(0xFFB8E2F9)},
                               GradientStop{1.0f, Color::from_argb(0xFFB0DFF8)}},
                              Color::from_argb(0xFF3C7FB1)),
                        block({GradientStop{0.25f, Color::from_argb(0xFFB8E4FA)},
                               GradientStop{0.5f, Color::from_argb(0xFF5EB4E3)},
                               GradientStop{1.0f, Color::from_argb(0xFF4C8BB4)}},
                              Color::from_argb(0xFF2C628B)),
                        disabled,
                        Size{11.0f, 22.0f},
                        Size{9.0f, 17.0f},
                        4.0f,
                        Color::from_argb(0xFFE7EAEA),
                        LinearGradientBrush{
                            {GradientStop{0.1f, Color::from_argb(0xFFAEB1AF)}, GradientStop{0.9f, colors::White}}},
                        5.0f,
                        1.0f,
                        Color::from_argb(0xFFC4C4C4),
                        4.0f};
    }

    // A drop-down list is a push button, an editable combo a field with a drop button. A 7x4
    // arrow in a 17 px column; padding 4,3 gives the era's ~22 px control.
    controls.dropdown_chrome       = detail::ControlChrome::DropdownChrome{17.0f,
                                                                           Size{7.0f, 4.0f},
                                                                           colors::Black,
                                                                           Color::from_argb(0xFFAFAFAF),
                                                                           6.0f,
                                                                           colors::White,
                                                                           Color::from_argb(0xFF646464),
                                                                           theme.colors().selection,
                                                                           theme.colors().selection_text};
    theme.sizing.combo_box_padding = Symmetric{.horizontal = 4.0f, .vertical = 3.0f};

    // Vista and 7 had no switch. This is a push button sliding in an edit field; the 56x22 track,
    // 32x16 knob and #C1DFF6 on fill are modern approximations, not era values.
    controls.toggle_switch_shape              = ToggleSwitchShape::ButtonKnob;
    controls.toggle_switch_track_size         = Size{56.0f, 22.0f};
    controls.toggle_switch_track_radius_ratio = 10.0f / 22.0f;
    controls.toggle_button_knob_size          = Size{32.0f, 16.0f};
    auto track                                = [&](Color fill, const LinearGradientBrush& stroke)
    {
        ButtonVisual v{fill, stroke.stops.front().color, colors::Transparent};
        v.border_gradient           = stroke;
        v.border_gradient_extent_px = controls.field_border_states->extent_px;
        return v;
    };
    const Color        on_fill = Color::from_argb(0xFFC1DFF6);
    const ButtonVisual track_disabled{Color::from_argb(0xFFF4F4F4), Color::from_argb(0xFFADB2B5), colors::Transparent};
    const ButtonVisual track_on = track(on_fill, controls.field_border_states->focused);
    controls.toggle_track = ToggleVisuals{ToggleVisual{track(colors::White, controls.field_border_states->rest), track_on},
                                          ToggleVisual{track(colors::White, controls.field_border_states->hover), track_on},
                                          ToggleVisual{track(colors::White, controls.field_border_states->hover), track_on},
                                          ToggleVisual{track_disabled, track_disabled}};

    // The navigation pane has its own fill and navy link text. The
    // stale Vista teal FillColorHint is ignored; Windows 7's pane is a measured pale blue.
    controls.nav_pane.background = LinearGradientBrush{
        {GradientStop{0.0f, Color::from_argb(0xFFEDF2F9)},
         GradientStop{0.5f, Color::from_argb(0xFFDEE8F6)},
         GradientStop{1.0f, Color::from_argb(0xFFEDF5FF)}}};
    controls.nav_pane.text     = Color::from_argb(0xFF151C55);
    controls.nav_pane.text_hot = Color::from_argb(0xFF074AE5);

    // No selection bar or tab pill: the era has no control accent. The current TaskLink is bold
    // and Explorer rows use the #3399FF highlight.
    controls.accent_selection_indicator = false;

    // Aero's text-style ramp carries color as well as size. The era has one
    // body size, so the bottom rungs coincide.
    auto aero_style = [](float size, FontWeight weight)
    { return TextStyle{size, TextAlign::Leading, TextAlign::Center, "Segoe UI", weight}; };
    theme.type.header            = aero_style(16.0f, FontWeight::Regular); // MainInstruction, Segoe UI 12 pt
    theme.type.subheader         = aero_style(12.0f, FontWeight::Regular); // Instruction
    theme.type.title             = aero_style(12.0f, FontWeight::Bold);    // BodyTitle
    theme.type.subtitle          = aero_style(12.0f, FontWeight::Regular); // BodyText
    theme.type.body              = theme.text_style;
    theme.type.caption           = aero_style(12.0f, FontWeight::Regular); // SecondaryText
    const Color main_instruction = Color::from_argb(0xFF003399);
    theme.type.color.header      = main_instruction;
    theme.type.color.subheader   = main_instruction;

    // ---- Progress and severity: `ProgressStyle` · `Fill` -------------------------------------------
    //
    // The Vista UX guide ties red, yellow and green to stopped, paused and normal progress.
    // The track is gray, since a white one vanishes.
    const Color progress_green = Color::from_argb(0xFF52D763);
    controls.progress_track    = Color::from_argb(0xFFBABABA);
    controls.progress_fill     = progress_green;
    {
        // Aero's glossy progress bar, drawn as ProgressBar.cpp's glossy path describes. 15 tall,
        // the UX guide's height.
        detail::ControlChrome::GlossyProgressBar bar;
        const Color                              warm = Color::from_argb(0x60FFFFC4);
        bar.indicator                                 = Color::from_argb(0xFF01D328);
        bar.track                                     = {GradientStop{0.0f, Color::from_argb(0xFFBABABA)},
                                                         GradientStop{0.5f, Color::from_argb(0xFFC7C7C7)},
                                                         GradientStop{1.0f, Color::from_argb(0xFFBABABA)}};
        bar.border = {GradientStop{0.0f, Color::from_argb(0xFFB2B2B2)}, GradientStop{1.0f, Color::from_argb(0xFF8C8C8C)}};
        bar.track_gloss              = Color::from_argb(0x50FFFFFF);
        bar.indicator_gloss          = Color::from_argb(0x90FFFFFF);
        bar.inner_edge               = Color::from_argb(0x80FFFFFF);
        bar.top_highlight            = {GradientStop{0.05f, Color::from_argb(0x80FFFFFF)},
                                        GradientStop{0.25f, Color::from_argb(0x00FFFFFF)}};
        bar.indicator_wash           = Color::from_argb(0x80B5FFA9);
        bar.left_cap                 = {GradientStop{0.0f, Color::from_argb(0x0C000000)},
                                        GradientStop{0.3f, Color::from_argb(0x20000000)},
                                        GradientStop{1.0f, Color::from_argb(0x00000000)}};
        bar.right_cap                = {GradientStop{0.0f, Color::from_argb(0x00000000)},
                                        GradientStop{0.7f, Color::from_argb(0x20000000)},
                                        GradientStop{1.0f, Color::from_argb(0x0C000000)}};
        bar.light                    = {GradientStop{0.0f, warm}, GradientStop{1.0f, warm.with_alpha(0.0f)}};
        bar.glow                     = {GradientStop{0.0f, Color::from_argb(0x00FFFFFF)},
                                        GradientStop{0.4f, Color::from_argb(0x60FFFFFF)},
                                        GradientStop{0.6f, Color::from_argb(0x60FFFFFF)},
                                        GradientStop{1.0f, Color::from_argb(0x00FFFFFF)}};
        bar.sweep                    = {GradientStop{0.0f, bar.indicator.with_alpha(0.0f)},
                                        GradientStop{0.4f, bar.indicator},
                                        GradientStop{0.6f, bar.indicator},
                                        GradientStop{1.0f, bar.indicator.with_alpha(0.0f)}};
        controls.glossy_progress_bar = std::move(bar);
    }
    // No pale severity wash in the era: the color lives in the sign, the bar is the field white.
    theme.severity.informational = Theme::SeverityVisual{theme.colors().link, theme.colors().field};
    theme.severity.success       = Theme::SeverityVisual{progress_green, theme.colors().field};
    theme.severity.caution       = Theme::SeverityVisual{Color::from_argb(0xFFDCD400), theme.colors().field};
    theme.severity.critical      = Theme::SeverityVisual{Color::from_argb(0xFFE40021), theme.colors().field};
    theme.severity.text_on_fill  = colors::White;
    theme.severity.stroke        = theme.surfaces.field_border;


    // CalendarView as the Vista design of comctl32 6: a light blue box for the selected and the
    // hot day, a rounded blue outline for today. The colors are Windows 8's; Vista's and 7's
    // style files are not on record.
    {
        detail::ControlChrome::CalendarChrome calendar;
        calendar.kind                                = detail::ControlChrome::CalendarChrome::Kind::Aero;
        calendar.background                          = colors::White;
        calendar.border                              = Color::from_argb(0xFF979797);
        calendar.heading                             = colors::Black;
        calendar.heading_hover                       = Color::from_argb(0xFF0066CC);
        calendar.heading_pressed                     = Color::from_argb(0xFF0066CC);
        calendar.heading_disabled                    = Color::from_argb(0xFFA0A0A0);
        calendar.weekday_text                        = colors::Black;
        calendar.separator                           = Color::from_argb(0xFFF5F5F5);
        calendar.item_text                           = colors::Black;
        calendar.trailing_text                       = Color::from_argb(0xFF646464);
        calendar.selected_border                     = Color::from_argb(0xFF26A0DA);
        calendar.selected_fill                       = Color::from_argb(0x3D26A0DA); // 24 %
        calendar.hover_border                        = Color::from_argb(0xA826A0DA); // 66 %
        calendar.hover_fill                          = Color::from_argb(0x1F26A0DA); // 12 %
        calendar.today_mark                          = Color::from_argb(0xFF0066CC);
        calendar.today_text                          = Color::from_argb(0xFF0066CC);
        calendar.footer_text                         = colors::Black;
        calendar.blackout_text                       = Color::from_argb(0xFFA0A0A0);
        calendar.disabled_text                       = Color::from_argb(0xFF6D6D6D);
        detail::edit_controls(theme).calendar_chrome = calendar;
    }

    // DatePicker and TimePicker: comctl32's date and time picker.
    detail::edit_controls(theme).date_time_picker_chrome = detail::ControlChrome::DateTimePickerChrome{
        .kind = detail::ControlChrome::DateTimePickerChrome::Kind::Field};
    return theme;
}

// ---- MDL2: Windows 10, 1507-1703 ---------------------------------------------------------
//
// Honors light/dark and the user accent, as the era did. Theme::mdl2() is the derivation.
Theme resolve_mdl2(bool dark, const Color& accent)
{
    return Theme::mdl2(dark ? Mdl2Palette::mdl2_dark() : Mdl2Palette::mdl2_light(), dark, accent);
}

// ---- MDL: Windows 8 & 8.1 merged ------------------------------------------------------
//
// Dark/light is genuine (dark is 8.x's default), the accent is not: Windows 8's violet is a
// literal in its theme resources and no era API exposed the user's color. Only accent_override wins.
Theme resolve_mdl(bool dark, const Color& accent, bool accent_is_override)
{
    MetroPalette palette = dark ? MetroPalette::metro_dark() : MetroPalette::metro_light();
    if (accent_is_override)
    {
        // The era has no ramp, its hover and pressed rungs are unrelated literals, so these are
        // approximated like derive_ramp().
        palette.accent         = accent;
        palette.accent_hover   = lighten(accent, 0.15f);
        palette.accent_pressed = darken(accent, 0.10f);
    }
    Theme theme                = Theme::metro(palette, dark);
    theme.chrome.dark_titlebar = dark;
    // The frame a Frame::Themed window draws. Windows 8 took the window color from the wallpaper
    // by default; the default wallpaper (img0.jpg) averages this sky blue.
    detail::edit_controls(theme).window_frame = detail::mdl_window_frame(
        accent_is_override ? accent : Color::from_argb(0xFF60B0F5));
    return theme;
}

// Only these four eras had a dark mode and a user accent. Every other variant ignores mode
// and accent entirely.
struct VariantTraits
{
    bool supports_dark;
    bool supports_accent;
};

[[nodiscard]] constexpr VariantTraits traits_of(ThemeVariant v)
{
    switch (v)
    {
        case ThemeVariant::System:
        case ThemeVariant::Fluent2:
        case ThemeVariant::Fluent:
        case ThemeVariant::Metro:
        case ThemeVariant::Mdl2:
            return {true, true};
        default:
            return {false, false};
    }
}

Theme resolve_retro(const ThemeRequest& request)
{
    switch (request.variant)
    {
        case ThemeVariant::Windows1x:
            return resolve_windows_old(request.early_scheme);
        case ThemeVariant::Windows3x:
            return resolve_windows3x(request.classic_scheme);
        case ThemeVariant::Windows9x:
            return resolve_windows9x(request.classic_scheme);
        case ThemeVariant::Luna:
            return resolve_luna();
        case ThemeVariant::Aero:
            return resolve_aero();
        case ThemeVariant::System:
        case ThemeVariant::Fluent2:
        case ThemeVariant::Fluent:
        case ThemeVariant::Metro:
        case ThemeVariant::Mdl2:
            break;
    }
    return Theme{}; // unreachable, resolve_theme() only calls this for the fixed-palette variants
}

// ---- High contrast, per era ------------------------------------------------------------
//
// The variant decides the construction, as each Windows did. Colors are the OS contrast theme's,
// or, when an app forces ThemeMode::HighContrast, the scheme that era shipped.

// Windows 11's contrast themes have no bevel slots, so the ramp is derived, shaped after
// Windows 2000/XP's High Contrast Black.
[[nodiscard]] ClassicBevelRamp high_contrast_bevel_ramp(const HighContrastPalette& hc)
{
    return ClassicBevelRamp{hc.control_text,
                            lerp(hc.control_text, hc.control_background, 0.25f),
                            lerp(hc.control_text, hc.control_background, 0.5f),
                            hc.control_text};
}

// Windows 95 to 2000 treat high contrast as one more color scheme. COLOR_MENU and COLOR_INFOBK
// take the window color, as Windows 2000's High Contrast Black does.
[[nodiscard]] ClassicPalette high_contrast_classic_palette(const HighContrastPalette& hc)
{
    ClassicPalette p;
    p.face           = hc.control_background;
    p.face_text      = hc.control_text;
    p.window         = hc.window_background;
    p.window_text    = hc.window_text;
    p.window_frame   = hc.control_border;
    p.selection      = hc.accent;
    p.selection_text = hc.accent_text;
    p.gray_text      = hc.disabled_text;
    p.menu           = hc.window_background;
    p.menu_text      = hc.window_text;
    p.info_bk        = hc.window_background;
    p.info_text      = hc.window_text;
    p.hot_light      = hc.hyperlink;
    p.bevel          = high_contrast_bevel_ramp(hc);
    return p;
}

// The reverse of high_contrast_classic_palette(): the eight roles a shipped classic contrast scheme
// fills, for the pieces below that are written against them.
[[nodiscard]] HighContrastPalette high_contrast_palette_of(const ClassicPalette& p)
{
    HighContrastPalette hc;
    hc.window_background  = p.window;
    hc.window_text        = p.window_text;
    hc.control_background = p.face;
    hc.control_text       = p.face_text;
    hc.control_border     = p.window_frame;
    hc.accent             = p.selection;
    hc.accent_text        = p.selection_text;
    hc.disabled_text      = p.gray_text;
    hc.hyperlink          = p.hot_light;
    return hc;
}

// Severity and the spelling squiggle, restated for HC.
void apply_high_contrast_semantics(Theme& theme, const HighContrastPalette& hc)
{
    const Theme::SeverityVisual sign{hc.accent, hc.window_background};
    theme.severity.informational = theme.severity.success = theme.severity.caution = theme.severity.critical = sign;
    theme.severity.neutral      = sign.fill;
    theme.severity.text_on_fill = hc.accent_text;
    theme.severity.stroke       = hc.control_border;
    theme.spelling_error        = hc.window_text;
}

// `Theme::classic()` writes the caption text as white, which is right for every shipped scheme's
// dark `COLOR_ACTIVECAPTION` and unreadable on a pale HC highlight.
void finish_classic_high_contrast(Theme& theme, const HighContrastPalette& hc)
{
    theme.chrome.caption_text_color = hc.accent_text;
    apply_high_contrast_semantics(theme, hc);
}

// Windows 1.x/2.x had no HC setting, but every slot was user-settable, so the contrast theme
// fills the slots.
[[nodiscard]] EarlyPalette high_contrast_early_palette(const HighContrastPalette& hc)
{
    EarlyPalette p;
    p.scrollbar        = hc.window_background;
    p.background       = hc.window_background;
    p.active_caption   = hc.accent;
    p.inactive_caption = hc.control_background;
    p.menu             = hc.window_background;
    p.window           = hc.window_background;
    p.window_frame     = hc.control_border;
    p.menu_text        = hc.window_text;
    p.window_text      = hc.window_text;
    p.caption_text     = hc.accent_text;
    p.active_border    = hc.control_border;
    p.inactive_border  = hc.control_border;
    p.app_workspace    = hc.window_background;
    return p;
}

// XP and Vista/7 switched to the Classic style under high contrast, keeping the OS UI font
// (Tahoma 8 pt on XP, Segoe UI 9 pt on Vista/7).
[[nodiscard]] Theme resolve_classic_high_contrast(const ClassicPalette& scheme, const char* font_family, float font_size)
{
    // `hot_track` for the flat toolbar buttons both OSes' Classic style still had.
    Theme                  theme    = Theme::classic(scheme, /*hot_track=*/true, font_family);
    detail::ControlChrome& controls = detail::edit_controls(theme);
    theme.text_style.font_size      = font_size;
    apply_body_type_ramp(theme);
    // No control accent in Classic, so no accent bar.
    controls.accent_selection_indicator = false;
    // Classic on XP and later still draws CS_DROPSHADOW under menus and tooltips.
    theme.popup_shadow = ThemePopupShadow::AeroDropShadow;
    finish_classic_high_contrast(theme, high_contrast_palette_of(scheme));
    return theme;
}

// A classic era's own resolver on a contrast scheme, finished the way every classic HC theme is.
[[nodiscard]] Theme resolve_classic_era_high_contrast(Theme (*resolve)(const std::optional<ClassicPalette>&),
                                                      const ClassicPalette& scheme)
{
    Theme theme = resolve(scheme);
    finish_classic_high_contrast(theme, high_contrast_palette_of(scheme));
    return theme;
}

// The version's monochrome-display row: the only palettes the era shipped with no color.
[[nodiscard]] EarlyPalette early_monochrome_of(const std::optional<EarlyPalette>& scheme)
{
    const bool windows_1x = scheme == EarlyPalette::windows_1x_color() || scheme == EarlyPalette::windows_1x_mono();
    return windows_1x ? EarlyPalette::windows_1x_mono() : EarlyPalette::windows_2x_mono();
}

// Windows 8.1's high contrast colors, role by role. Only the
// rows Theme::metro() reads are listed.
[[nodiscard]] MetroPalette high_contrast_metro_palette(const HighContrastPalette& hc)
{
    MetroPalette p;
    p.page_background      = hc.window_background; // Window
    p.foreground           = hc.window_text;       // WindowText
    p.foreground_secondary = hc.window_text;       // WindowText
    p.foreground_pressed   = hc.hyperlink;         // Hotlight

    p.button_background          = hc.control_background; // ButtonFace
    p.button_border              = hc.control_text;       // ButtonText
    p.button_foreground          = hc.control_text;       // ButtonText
    p.button_hover_background    = hc.accent;             // Highlight
    p.button_pressed_background  = hc.control_text;       // ButtonText
    p.button_pressed_foreground  = hc.control_background; // ButtonFace
    p.button_disabled_background = hc.control_background; // ButtonFace
    p.button_disabled_border     = hc.disabled_text;      // GrayText
    p.button_disabled_foreground = hc.disabled_text;      // GrayText

    p.toggle_box_background = hc.control_background; // ButtonFace
    p.toggle_box_border     = hc.control_text;       // ButtonText
    p.toggle_glyph          = hc.control_text;       // ButtonText

    p.field_background    = hc.control_background; // ButtonFace
    p.field_text          = hc.control_text;       // ButtonText
    p.field_border        = hc.control_text;       // ButtonText
    p.menu_background     = hc.window_background;  // Window
    p.menu_text           = hc.window_text;
    p.menu_highlight      = hc.accent;            // Highlight
    p.menu_highlight_text = hc.accent_text;       // HighlightText
    p.tooltip_background  = hc.window_background; // Window
    p.tooltip_text        = hc.window_text;       // WindowText
    p.tooltip_border      = hc.window_text;       // WindowText
    p.hyperlink           = hc.hyperlink;         // Hotlight
    p.separator           = hc.window_text;       // WindowText

    p.scrollbar_track = hc.control_background; // ButtonFace
    p.scrollbar_thumb = hc.control_text;       // ButtonText
    p.scrollbar_arrow = hc.control_text;       // ButtonText

    // The toggle switch's on fill is Highlight in every state: no ladder to walk.
    p.accent = p.accent_hover = p.accent_pressed = hc.accent;

    // Button and check box borders are both 2 here:
    // high contrast never removes a border.
    p.button_border_width    = 2.0f;
    p.indicator_border_width = 2.0f;
    return p;
}

[[nodiscard]] Theme resolve_mdl_high_contrast(const HighContrastPalette& hc)
{
    // `dark` draws the button as an outline, the shape a 2 px high contrast border asks for.
    Theme                  theme    = Theme::metro(high_contrast_metro_palette(hc), /*dark=*/true);
    detail::ControlChrome& controls = detail::edit_controls(theme);
    theme.chrome.dark_titlebar      = is_dark_surface(hc.window_background);

    // What Theme::metro() writes as literals. The dashed focus pair stays black/white, it is one
    // of the six HC colors that are not system colors.
    detail::edit_colors(theme).selection_text = hc.accent_text;   // HighlightText
    detail::edit_colors(theme).text_disabled  = hc.disabled_text; // the era reads `foreground_pressed`, Hotlight here
    theme.surfaces.menu_border                = hc.window_text;   // WindowText
    theme.button_hover.text                   = hc.accent_text;   // HighlightText
    for (ButtonVisual* v :
         {&theme.accent_button_normal,
          &theme.accent_button_hover,
          &theme.accent_button_pressed,
          &theme.subtle_button_hover,
          &theme.subtle_button_pressed})
    {
        v->text = hc.accent_text; // every one of these sits on a Highlight fill
    }

    // HC is the one mode where the box reacts: Highlight/HighlightText on hover and
    // ButtonText/ButtonFace pressed, RadioButton alike.
    for (ToggleVisuals* tv : {&controls.check_box_box, &controls.radio_ring})
    {
        tv->hover.off.background = tv->hover.on.background = hc.accent;
        tv->hover.on.text                                  = hc.accent_text;
        tv->pressed.off.background = tv->pressed.on.background = hc.control_text;
        tv->pressed.on.text                                    = hc.control_background;
        tv->disabled.on.text                                   = hc.disabled_text; // GrayText
    }
    controls.check_box_indeterminate = IndeterminateVisuals::like_checked(controls.check_box_box,
                                                                          IndeterminateMark::Square,
                                                                          glyphs::CheckBoxIndeterminate,
                                                                          controls.check_box_indeterminate.square_size);

    // ToggleSwitch: ButtonFace track in a ButtonText border, Highlight curtain when on, ButtonText
    // thumb. Disabled goes GrayText.
    for (ButtonVisual* v : controls.toggle_track.all())
    {
        v->background = hc.control_background;
        v->border     = hc.control_text;
        v->text       = hc.control_text;
    }
    for (ButtonVisual* v :
         {&controls.toggle_track.normal.on, &controls.toggle_track.hover.on, &controls.toggle_track.pressed.on})
    {
        v->background = hc.accent;
    }
    for (ButtonVisual* v : {&controls.toggle_track.disabled.off, &controls.toggle_track.disabled.on})
    {
        v->border = v->text = hc.disabled_text;
    }

    controls.progress_track = hc.control_background; // ButtonFace
    controls.progress_fill  = hc.accent;             // Highlight
    apply_high_contrast_semantics(theme, hc);
    // CalendarView draws Fluent's round cells from the contrast colors.
    detail::edit_controls(theme).calendar_chrome.reset();
    // DatePicker and TimePicker keep Windows 8.1's drop-down lists in the contrast colors.
    return theme;
}

// Windows 10 1507's high contrast rebinds every color by its role, so this re-reads
// Theme::mdl2()'s shape role by role.
[[nodiscard]] Theme resolve_mdl2_high_contrast(const HighContrastPalette& hc)
{
    Theme                  theme    = Theme::mdl2(Mdl2Palette::mdl2_dark(), /*dark=*/true, hc.accent);
    detail::ControlChrome& controls = detail::edit_controls(theme);
    const Color            face     = hc.control_background;
    const Color            text     = hc.control_text;
    const Color            hi       = hc.accent;
    const Color            hi_text  = hc.accent_text;
    const Color            gray     = hc.disabled_text;

    detail::edit_colors(theme).background = hc.window_background; // Window
    detail::edit_colors(theme).text       = hc.window_text;       // WindowText
    theme.chrome.dark_titlebar            = is_dark_surface(hc.window_background);
    // The era's title-bar grays have no HC color; the frame is the OS's own.
    theme.chrome.caption_color.reset();
    theme.chrome.caption_text_color.reset();
    theme.chrome.border_color.reset();
    // Drawn on the page, so the page's own pair.
    detail::edit_colors(theme).focus       = hc.window_text;
    detail::edit_colors(theme).focus_inner = hc.window_background;

    detail::edit_colors(theme).field          = face; // ButtonFace
    detail::edit_colors(theme).field_text     = text; // ButtonText
    theme.surfaces.field_border               = text; // ButtonText
    detail::edit_colors(theme).selection      = hi;   // Highlight
    detail::edit_colors(theme).selection_text = hi_text;
    detail::edit_colors(theme).text_disabled  = gray;         // GrayText
    theme.surfaces.menu                       = face;         // ButtonFace
    theme.surfaces.menu_text                  = text;         // ButtonText
    theme.surfaces.menu_highlight             = hi;           // Highlight
    theme.surfaces.menu_highlight_text        = hi_text;      // HighlightText
    theme.surfaces.tooltip                    = face;         // ButtonFace
    theme.surfaces.tooltip_text               = text;         // ButtonText
    detail::edit_colors(theme).link           = hc.hyperlink; // Hotlight
    theme.surfaces.hyperlink_hover            = text;         // ButtonText
    theme.surfaces.hyperlink_pressed          = hc.accent;    // Highlight
    theme.surfaces.hyperlink_disabled         = gray;         // GrayText
    theme.surfaces.separator_light = theme.surfaces.separator_dark = detail::edit_colors(theme).divider = text; // ButtonText
    theme.surfaces.scrollbar_track = face;
    theme.surfaces.scrollbar_thumb = text;
    theme.surfaces.scrollbar_arrow = text;

    // HC binds the transparent rest border to ButtonText, so every button is outlined; hover and
    // press recolor border and label in Highlight.
    auto boxed = [](const Color& fill, const Color& border, const Color& label)
    {
        ButtonVisual v{fill, border, label};
        v.draw_border = true;
        return v;
    };
    theme.button_normal   = boxed(face, text, text);
    theme.button_hover    = boxed(face, hi, hi);
    theme.button_pressed  = boxed(face, hi, hi);
    theme.button_disabled = boxed(face, gray, gray);

    // 1703's accent button is ButtonText in HC. Highlight instead, because Slider draws
    // its value fill from this and a ButtonText fill vanishes into the track.
    theme.accent_button_normal   = boxed(hi, hi, hi_text);
    theme.accent_button_hover    = boxed(hi, text, hi_text);
    theme.accent_button_pressed  = boxed(face, hi, hi);
    theme.accent_button_disabled = boxed(face, gray, gray);

    theme.subtle_button_normal   = ButtonVisual{colors::Transparent, colors::Transparent, text};
    theme.subtle_button_hover    = ButtonVisual{hi, colors::Transparent, hi_text}; // Highlight, hover
    theme.subtle_button_pressed  = ButtonVisual{hi, colors::Transparent, hi_text}; // Highlight, pressed
    theme.subtle_button_disabled = ButtonVisual{colors::Transparent, colors::Transparent, gray};

    // The checked-disabled glyph takes the face, since GrayText on its GrayText fill would vanish.
    const ButtonVisual off{colors::Transparent, text, colors::Transparent};
    const ButtonVisual on{hi, hi, hi_text};
    controls.set_toggle_visuals(
        ToggleVisuals{ToggleVisual{off, on},
                      ToggleVisual{off, on},
                      ToggleVisual{off, on},
                      ToggleVisual{ButtonVisual{colors::Transparent, gray, colors::Transparent},
                                   ButtonVisual{gray, gray, face}}});
    auto outlined = [&](const Color& stroke, const Color& glyph)
    { return IndeterminateVisual{ButtonVisual{colors::Transparent, stroke, glyph}}; };
    controls.check_box_indeterminate = IndeterminateVisuals{outlined(hi, text),
                                                            outlined(hi, text),
                                                            outlined(hi, hi),
                                                            outlined(gray, gray),
                                                            IndeterminateMark::Glyph,
                                                            glyphs::CheckBoxIndeterminateMdl2};
    // The ring never fills in this era, so its stroke and its dot, HighlightText otherwise, take
    // Highlight and ButtonText instead.
    auto ring = [&](const Color& stroke, const Color& dot) { return ButtonVisual{colors::Transparent, stroke, dot}; };
    controls.radio_ring = ToggleVisuals{
        ToggleVisual{ring(text, colors::Transparent), ring(hi, text)},
        ToggleVisual{ring(hi, colors::Transparent), ring(hi, text)},
        ToggleVisual{ring(hi, colors::Transparent), ring(hi, hi)},
        ToggleVisual{ring(gray, colors::Transparent), ring(gray, gray)},
    };

    controls.slider_fills = detail::ControlChrome::SliderFills{/*thumb=*/text,
                                                               /*thumb_hover=*/text,
                                                               /*thumb_pressed=*/hi,
                                                               /*thumb_disabled=*/gray,
                                                               /*track=*/text,
                                                               /*track_hover=*/text,
                                                               /*track_disabled=*/gray};
    // Every row selection rung is the one opaque Highlight.
    controls.row_selection_fills = detail::ControlChrome::AccentSelectionFills{hi, hi, hi, hi, hi};

    controls.progress_track = face; // ButtonFace
    controls.progress_fill  = hi;   // Highlight
    apply_high_contrast_semantics(theme, hc);
    // CalendarView draws Fluent's round cells from the contrast colors.
    detail::edit_controls(theme).calendar_chrome.reset();
    return theme;
}

// Fluent 1 derives HC from the same system color roles as Fluent 2, so only the shape differs.
// The era's corner radii are the same in Light, Dark and HC, so HC keeps 2/4.
[[nodiscard]] Theme resolve_fluent1_high_contrast(const HighContrastPalette& hc)
{
    Theme                  theme    = resolve_high_contrast(hc);
    detail::ControlChrome& controls = detail::edit_controls(theme);
    const Theme            era      = resolve_fluent1(/*dark=*/true, hc.accent, derive_ramp(hc.accent));

    theme.chrome.corner_preference         = era.chrome.corner_preference;
    theme.rasterization                    = era.rasterization;
    theme.corner_radius                    = era.corner_radius;
    theme.overlay_corner_radius            = era.overlay_corner_radius;
    theme.padding                          = era.padding;
    controls.field_focus_border_thickness  = detail::controls(era).field_focus_border_thickness;
    controls.nav_indicator                 = detail::controls(era).nav_indicator;
    theme.icon_font_family                 = era.icon_font_family;
    theme.anim_faster                      = era.anim_faster;
    controls.toggle_knob                   = detail::controls(era).toggle_knob;
    controls.radio_dot                     = detail::controls(era).radio_dot;
    controls.slider_track_thickness        = detail::controls(era).slider_track_thickness;
    controls.slider_thumb                  = detail::controls(era).slider_thumb;
    controls.check_box_indeterminate.glyph = detail::controls(era).check_box_indeterminate.glyph;
    // One by one, since copying `type` whole would bring the era's role colors along.
    theme.type.caption   = era.type.caption;
    theme.type.body      = era.type.body;
    theme.type.subtitle  = era.type.subtitle;
    theme.type.title     = era.type.title;
    theme.type.subheader = era.type.subheader;
    theme.type.header    = era.type.header;
    // CalendarView draws Fluent's round cells from the contrast colors.
    detail::edit_controls(theme).calendar_chrome.reset();
    return theme;
}

[[nodiscard]] bool is_era(ThemeVariant variant)
{
    return variant != ThemeVariant::System && variant != ThemeVariant::Fluent2 && variant != ThemeVariant::Fluent;
}

// The period packs, under high contrast or not. Reached only through era_resolver, which
// enable_era_themes() sets, so an app that never calls it links none of them.
[[nodiscard]] Theme resolve_era(const ThemeRequest& request, const OsThemeSnapshot& os)
{
    if (os.high_contrast || request.mode == ThemeMode::HighContrast)
    {
        // The OS's contrast theme is the user's own and always wins. Only a forced HC with none
        // on falls back to the era's scheme.
        const bool forced = !os.high_contrast;
        auto       modern = [&](const HighContrastPalette& era) { return forced ? era : os.hc; };
        auto classic = [&](const ClassicPalette& era) { return forced ? era : high_contrast_classic_palette(os.hc); };

        switch (request.variant)
        {
            case ThemeVariant::Mdl2:
                return resolve_mdl2_high_contrast(modern(HighContrastPalette::windows10_high_contrast_black()));
            case ThemeVariant::Metro:
                // Windows 8 is where these contrast themes were re-authored, so 8.1 has the same values.
                return resolve_mdl_high_contrast(modern(HighContrastPalette::windows10_high_contrast_black()));
            case ThemeVariant::Aero:
                // No Vista/7 scheme values are on disk. Both drew HC under XP's scheme names, so XP's
                // values stand in.
                return resolve_classic_high_contrast(classic(ClassicPalette::high_contrast_black_2000()), "Segoe UI", 12.0f);
            case ThemeVariant::Luna:
                return resolve_classic_high_contrast(classic(ClassicPalette::high_contrast_black_2000()), "Tahoma", 11.0f);
            case ThemeVariant::Windows9x:
                // The Windows 98 row, for the same reason resolve_windows9x() defaults to it.
                return resolve_classic_era_high_contrast(resolve_windows9x,
                                                         classic(ClassicPalette::high_contrast_black_98()));
            case ThemeVariant::Windows3x:
                return resolve_classic_era_high_contrast(resolve_windows3x, classic(ClassicPalette::monochrome_3x()));
            case ThemeVariant::Windows1x:
            {
                if (forced)
                {
                    return resolve_windows_old(early_monochrome_of(request.early_scheme));
                }
                Theme theme          = resolve_windows_old(high_contrast_early_palette(os.hc));
                theme.spelling_error = os.hc.window_text;
                return theme;
            }
            case ThemeVariant::System:
            case ThemeVariant::Fluent2:
            case ThemeVariant::Fluent:
                break;
        }
        return Theme{}; // unreachable, resolve_theme_once() only calls this for a period pack
    }

    // Most period variants ignore the OS light/dark and accent. Checked after high contrast, whose
    // colors must still win.
    const VariantTraits traits = traits_of(request.variant);
    if (!traits.supports_dark && !traits.supports_accent)
    {
        return resolve_retro(request);
    }
    const bool  dark   = request.mode == ThemeMode::Dark || (request.mode == ThemeMode::System && os.dark);
    const Color accent = request.accent_override.value_or(os.accent);
    return request.variant == ThemeVariant::Metro
               ? resolve_mdl(dark, accent, request.accent_override.has_value())
               : resolve_mdl2(dark, accent);
}

using EraResolver = Theme (*)(const ThemeRequest&, const OsThemeSnapshot&);
std::atomic<EraResolver> era_resolver{nullptr};

// The variant a request is drawn in: System is the running Windows' own, and a period pack that
// is not linked in draws that too.
[[nodiscard]] ThemeVariant effective_variant(ThemeVariant variant, const OsThemeSnapshot& os)
{
    const ThemeVariant system = os.system_variant == ThemeVariant::Fluent ? ThemeVariant::Fluent : ThemeVariant::Fluent2;
    if (variant == ThemeVariant::System || (is_era(variant) && era_resolver.load(std::memory_order_acquire) == nullptr))
    {
        return system;
    }
    return variant;
}

[[nodiscard]] Theme resolve_theme_high_contrast(const ThemeRequest& request, const OsThemeSnapshot& os)
{
    // The OS's contrast theme is the user's own and always wins. Only a forced HC with none on
    // falls back to the era's scheme.
    const bool forced = !os.high_contrast;
    auto       modern = [&](const HighContrastPalette& era) { return forced ? era : os.hc; };

    if (request.variant == ThemeVariant::Fluent)
    {
        return resolve_fluent1_high_contrast(modern(HighContrastPalette::windows10_high_contrast_black()));
    }
    // Fluent 2's high contrast recolors but never resizes, so the type ramp comes over
    // style by style, leaving the role colors behind.
    Theme       theme      = resolve_high_contrast(modern(HighContrastPalette::aquatic()));
    const Theme era        = resolve_fluent2(/*dark=*/true, colors::White, AccentRamp{});
    theme.type.caption     = era.type.caption;
    theme.type.body        = era.type.body;
    theme.type.subtitle    = era.type.subtitle;
    theme.type.title       = era.type.title;
    theme.type.subheader   = era.type.subheader;
    theme.type.header      = era.type.header;
    theme.type.body_strong = era.type.body_strong;
    return theme;
}

} // namespace

bool theme_variant_supports_dark_mode(ThemeVariant variant)
{
    return traits_of(variant).supports_dark;
}

void enable_era_themes()
{
    era_resolver.store(&resolve_era, std::memory_order_release);
}

bool era_themes_enabled()
{
    return era_resolver.load(std::memory_order_acquire) != nullptr;
}

namespace
{
Theme resolve_theme_once(const ThemeRequest& request, const OsThemeSnapshot& os);

// USER32's popup effects, driven only by the user's switches. The 200 ms is AnimateWindow's
// "typically"; no era states its own.
[[nodiscard]] Theme::PopupMotion user32_popup_motion(const OsThemeSnapshot& os)
{
    using PopupAnimation = ThemePopupAnimation;
    Theme::PopupMotion motion;
    motion.menu  = !os.menu_animation ? PopupAnimation::None
                   : os.menu_fade     ? PopupAnimation::Fade
                                      : PopupAnimation::Slide;
    motion.list  = os.combo_box_animation ? PopupAnimation::Slide : PopupAnimation::None;
    motion.open  = std::chrono::milliseconds{200};
    motion.close = std::chrono::milliseconds{0};
    return motion;
}
} // namespace

Theme resolve_theme(const ThemeRequest& asked, const OsThemeSnapshot& os)
{
    ThemeRequest request = asked;
    request.variant      = effective_variant(asked.variant, os);
    Theme theme          = resolve_theme_once(request, os);
    // The variant asked for, under high contrast too, where some eras build their contrast look
    // from another era's pack (Luna's and Aero's from the classic one).
    const bool contrast = os.high_contrast || request.mode == ThemeMode::HighContrast;
    theme.variant       = request.variant;
    theme.high_contrast = contrast;
    // A contrast scheme is dark or light by its window color, whichever resolver drew it: some
    // pass a fixed `dark` to their era's derivation for the shape alone.
    if (contrast)
    {
        theme.dark = is_dark_surface(theme.colors().background);
    }
    // Under high contrast too: the menu and combo effects are system settings the contrast theme
    // leaves alone.
    if (request.variant == ThemeVariant::Aero || request.variant == ThemeVariant::Luna)
    {
        theme.popup_motion = user32_popup_motion(os);
    }
    theme.animations_enabled = request.animations == AnimationPreference::On ||
                               (request.animations == AnimationPreference::System && os.animations_enabled);
    detail::apply_animations_switch(theme);
    // The same request in the other mode, for the variants that have both, and never under high
    // contrast, which has no light or dark mode.
    if (!contrast && traits_of(request.variant).supports_accent)
    {
        theme.accent_ramp.base = request.accent_override.value_or(os.accent); // the plain accent
    }
    if (!contrast && traits_of(request.variant).supports_dark)
    {
        theme.with_dark_mode = [asked, os](bool dark)
        {
            ThemeRequest r = asked;
            r.mode         = dark ? ThemeMode::Dark : ThemeMode::Light;
            return resolve_theme(r, os);
        };
    }
    detail::fill_derived_colors(theme);
    return theme;
}

namespace
{
Theme resolve_theme_once(const ThemeRequest& request, const OsThemeSnapshot& os)
{
    if (is_era(request.variant))
    {
        return era_resolver.load(std::memory_order_acquire)(request, os); // set, effective_variant() checked
    }
    if (os.high_contrast || request.mode == ThemeMode::HighContrast)
    {
        return resolve_theme_high_contrast(request, os);
    }

    const bool       dark   = request.mode == ThemeMode::Dark || (request.mode == ThemeMode::System && os.dark);
    const Color      accent = request.accent_override.value_or(os.accent);
    const AccentRamp ramp   = request.accent_override ? derive_ramp(*request.accent_override) : os.accent_ramp;

    if (request.variant == ThemeVariant::Fluent)
    {
        Theme theme       = resolve_fluent1(dark, accent, ramp);
        theme.accent_ramp = ramp;
        return theme;
    }
    Theme theme       = resolve_fluent2(dark, accent, ramp);
    theme.accent_ramp = ramp;
    // In-app acrylic on the overlays, where the OS draws transparency at all. Windows 10's Fluent
    // popups were acrylic too, but blurred the desktop behind the window, which an
    // in-app blur would misrepresent, so only Fluent 2 takes it.
    if (os.transparency_enabled)
    {
        theme.overlay_material = theme.palette.acrylic_in_app_default();
    }
    return theme;
}
} // namespace

} // namespace ui
