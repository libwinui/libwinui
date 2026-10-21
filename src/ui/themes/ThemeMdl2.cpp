// SPDX-License-Identifier: MIT
// Colors described below are the ones Windows 10 1507 uses at that spot, unless another build is
// named.
#include "../../render/ControlGlyphs.hpp"
#include "ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/themes/Mdl2Palette.hpp"
#include "winui/ui/themes/Theme.hpp"

namespace ui
{

Theme Theme::mdl2(const Mdl2Palette& p, bool dark, const Color& accent)
{
    Theme                  theme;
    detail::ControlChrome& controls = detail::edit_controls(theme);
    theme.variant                   = ThemeVariant::Mdl2;
    theme.dark                      = dark;

    // ---- Geometry: square, 2 px, 4-epx ---------------------------------------------------
    theme.corner_radius         = 0.0f;
    theme.overlay_corner_radius = 0.0f;
    // The border of a button, a text field, a check box and a radio button is 2 in all four era
    // SDKs. A 1 px border reads as Fluent-Win10.
    theme.border_width = 2.0f;
    // A button's default padding in 10240 and 15063. Fluent-Win10's is 8,5,8,6.
    theme.padding        = Symmetric{.horizontal = 8.0f, .vertical = 4.0f};
    theme.page.grid_unit = 4.0f;

    // The era drew non-transformed text with ClearType.
    theme.rasterization = SetRasterization{GeometryAA::Antialiased, TextAA::Subpixel, /*snap_to_device_pixels=*/false};

    detail::edit_colors(theme).background = p.page_background; // the page background
    detail::edit_colors(theme).text       = p.text_100;        // the foreground at full strength

    // Windows 10 desktop windows were square; rounding is a Windows 11 change.
    theme.chrome.corner_preference  = CornerPreference::DoNotRound;
    theme.chrome.dark_titlebar      = dark;
    theme.chrome.caption_color      = p.chrome_mid; // the era's own title-bar gray, per theme
    theme.chrome.caption_text_color = p.text_100;
    theme.chrome.border_color       = p.chrome_strong;
    // Mica is a material from after this era.
    theme.chrome.allows_system_backdrop = false;

    // ---- Focus: the two-ring visual, square ----------------------------------------------
    //
    // The 2 + 1 + 1 geometry is confirmed for 1607-1703 only; 1507 and 1511 record no focus visual.
    theme.focus_visual           = FocusVisual::FluentRings;
    theme.focus_ring_outer_width = 2.0f;
    theme.focus_ring_inner_width = 1.0f;
    theme.focus_ring_margin      = 1.0f;
    // Brush pairing as Fluent ships it; this era's prose names only the geometry.
    detail::edit_colors(theme).focus       = p.text_100;
    detail::edit_colors(theme).focus_inner = p.page_60;

    // ---- Motion: zero, and now for a sourced reason ---------------------------------------
    //
    // Every interaction-state animation of Button, CheckBox, RadioButton, ToggleSwitch, Slider,
    // ComboBox and TextBox in 10240 is zero-duration. State changes are instant.
    theme.anim_faster = theme.anim_fast = theme.anim_normal = std::chrono::milliseconds{0};
    // The era's most common easing curve, 0.1,0.9 0.2,1.0. Inert while the durations are zero.
    theme.anim_fast_out_slow_in = CubicBezier{0.1f, 0.9f, 0.2f, 1.0f};

    // ---- Popups: no shadow, the alt-medium smoke ------------------------------------------
    //
    // Popup shadows need 19H1, so no MDL2 popup casts one. DialogControl dims the window with the
    // alt-medium page color (the dialog's smoke layer), which 10586 adds. Menus keep the default
    // popup_motion, since the era's theme resources do not state the menu open animation's
    // timings.
    theme.popup_shadow               = ThemePopupShadow::None;
    detail::edit_colors(theme).scrim = p.page_60;

    // ---- Type: the 15 px ramp -------------------------------------------------------------
    //
    // Base and Body are 15 px through 1703 and only become 14 in the 1809 SDK.
    auto style = [](float size, FontWeight weight)
    { return TextStyle{size, TextAlign::Leading, TextAlign::Center, "Segoe UI", weight}; };
    theme.type.header    = style(46.0f, FontWeight::Light);
    theme.type.subheader = style(34.0f, FontWeight::Light);
    theme.type.title     = style(24.0f, FontWeight::SemiLight);
    theme.type.subtitle  = style(20.0f, FontWeight::Regular);
    theme.type.body      = style(15.0f, FontWeight::Regular);
    theme.type.caption   = style(12.0f, FontWeight::Regular);
    // A Button's content is centered (a control's default content alignment), while the
    // ramp is authored Leading for page text, so only size/weight/family come from Body.
    theme.text_style            = theme.type.body;
    theme.text_style.horizontal = TextAlign::Center;
    theme.text_style.vertical   = TextAlign::Center;

    // The system symbol font resolves to the Windows 11 redraw today, so the font is named outright.
    theme.icon_font_family = "Segoe MDL2 Assets";

    // ---- Surfaces --------------------------------------------------------------------------
    detail::edit_colors(theme).field      = p.page_100;               // the text field's fill
    detail::edit_colors(theme).field_text = p.text_100;               // the text field's text
    theme.surfaces.field_border           = p.chrome_disabled_strong; // the text field's border
    // A text field's selection is the accent. Not pinned to the era's #0078D7, because MDL2's
    // accent is the user's choice.
    detail::edit_colors(theme).selection      = accent;
    detail::edit_colors(theme).selection_text = p.white_100;
    // Low-contrast disabled text is the era's, not a bug.
    detail::edit_colors(theme).text_disabled = p.text_20;
    // The menu surface, framed in the high chrome color.
    theme.surfaces.menu      = p.chrome_flyout;
    theme.surfaces.menu_text = p.text_100;
    // A hovered menu item's fill; the foreground stays unchanged.
    theme.surfaces.menu_highlight      = p.list_hover;
    theme.surfaces.menu_highlight_text = p.text_100;
    // ToolTip: a medium-low chrome fill, a high chrome border and full-strength text.
    theme.surfaces.tooltip      = p.chrome_flyout;
    theme.surfaces.tooltip_text = p.text_100;
    // A link is the accent color.
    detail::edit_colors(theme).link = accent;
    // A link's hover, pressed and disabled colors: the medium base color, then medium-low twice.
    theme.surfaces.hyperlink_hover    = p.text_60;
    theme.surfaces.hyperlink_pressed  = p.text_40;
    theme.surfaces.hyperlink_disabled = p.text_40;
    // A menu separator is a single flat stroke in the medium-low base color.
    theme.surfaces.separator_light     = p.text_40;
    theme.surfaces.separator_dark      = p.text_40;
    detail::edit_colors(theme).divider = p.text_40;
    // Theme-independent literals in this era's theme resources.
    theme.surfaces.scrollbar_track = Color::from_argb(0x59D5D5D5);
    theme.surfaces.scrollbar_thumb = Color::from_argb(0xFFCDCDCD);
    theme.surfaces.scrollbar_arrow = p.text_60; // the scroll bar arrows, the 60 % rung

    // ---- Button: a gray alpha fill with an invisible border ---------------------------------
    //
    // Hover changes the border, not the fill. Pressed deepens the same gray; no accent.
    theme.button_normal   = ButtonVisual{p.text_20, colors::Transparent, p.text_100};
    theme.button_hover    = ButtonVisual{p.text_20, p.text_40, p.text_100};
    theme.button_pressed  = ButtonVisual{p.text_40, colors::Transparent, p.text_100};
    theme.button_disabled = ButtonVisual{p.text_20, colors::Transparent, p.text_20};

    // ---- Accent button: the same shape, filled ------------------------------------------------
    //
    // The accent button first appears in 1703; its ladder is the only sourced one, so it is used
    // for the whole era. Hover grows a border and press drops to neutral gray, like Button.
    theme.accent_button_normal   = ButtonVisual{accent, colors::Transparent, p.white_100};
    theme.accent_button_hover    = ButtonVisual{accent, p.text_40, p.white_100};
    theme.accent_button_pressed  = ButtonVisual{p.text_40, colors::Transparent, p.text_100};
    theme.accent_button_disabled = ButtonVisual{p.text_20, colors::Transparent, p.text_40};

    // ---- Subtle (the app-bar/command-bar button) -----------------------------------------------
    theme.subtle_button_normal   = ButtonVisual{colors::Transparent, colors::Transparent, p.text_100};
    theme.subtle_button_hover    = ButtonVisual{p.list_hover, colors::Transparent, p.text_100};
    theme.subtle_button_pressed  = ButtonVisual{p.list_pressed, colors::Transparent, p.text_100};
    theme.subtle_button_disabled = ButtonVisual{colors::Transparent, colors::Transparent, p.text_20};

    // ---- Toggles: a 2 px stroked box that fills with accent ---------------------------------
    const ButtonVisual off{colors::Transparent, p.text_80, colors::Transparent};
    const ButtonVisual on{accent, accent, p.white_100};
    ToggleVisuals toggles{ToggleVisual{off, on}, ToggleVisual{off, on}, ToggleVisual{off, on}, ToggleVisual{off, on}};
    // Pressed, checked or not: the medium base color.
    toggles.pressed.on = ButtonVisual{p.text_60, p.text_60, p.white_100};
    toggles.disabled   = ToggleVisual{ButtonVisual{colors::Transparent, p.text_20, colors::Transparent},
                                      ButtonVisual{p.text_20, p.text_20, p.text_20}};
    controls.set_toggle_visuals(toggles);

    // Indeterminate puts the accent on the stroke and draws the glyph in the foreground ladder.
    auto outlined = [&](const Color& stroke, const Color& glyph)
    { return IndeterminateVisual{ButtonVisual{colors::Transparent, stroke, glyph}}; };
    controls.check_box_indeterminate = IndeterminateVisuals{outlined(accent, p.text_80),
                                                            outlined(accent, p.text_100),
                                                            outlined(p.text_60, p.text_60),
                                                            outlined(p.text_20, p.text_20),
                                                            IndeterminateMark::Glyph,
                                                            glyphs::CheckBoxIndeterminateMdl2};

    // A checked MDL2 radio is an accent outline around a gray dot; the ring never fills.
    const ButtonVisual hollow = ButtonVisual{colors::Transparent, p.text_80, colors::Transparent};
    auto ring = [&](const Color& stroke, const Color& dot) { return ButtonVisual{colors::Transparent, stroke, dot}; };
    controls.radio_ring = ToggleVisuals{
        ToggleVisual{hollow, ring(accent, p.text_80)},
        ToggleVisual{ring(p.text_100, colors::Transparent), ring(accent, p.text_100)},
        ToggleVisual{ring(p.text_60, colors::Transparent), ring(p.text_60, p.text_60)},
        ToggleVisual{ring(p.text_20, colors::Transparent), ring(p.text_20, p.text_20)},
    };

    // ---- Per-control geometry, all measured from 10240's controls ------------------------------
    controls.toggle_box_size        = 20.0f;
    controls.toggle_content_spacing = 8.0f; // CheckBox's label padding is 8,5,0,0
    controls.radio_dot.rest = controls.radio_dot.hover = controls.radio_dot.pressed = 10.0f;

    // ToggleSwitch: a 44 x 20 pill (radius 10), a 10 px knob that travels 24 when switched on. No resize.
    controls.toggle_switch_track_size = Size{44.0f, 20.0f};
    controls.toggle_knob.rest         = 10.0f;
    controls.toggle_knob.hover        = 10.0f;
    controls.toggle_knob.pressed      = Size{10.0f, 10.0f};
    controls.toggle_knob.travel       = 24.0f;

    // ---- Slider ------------------------------------------------------------------------
    //
    // An 8 x 24 thumb at corner radius 4, a different construction from Fluent's two discs.
    controls.slider_track_thickness = 2.0f;
    controls.slider_thumb.shape     = SliderThumbShape::RoundedRect;
    controls.slider_thumb.radius    = 4.0f;
    controls.slider_thumb.outer     = 24.0f;
    controls.slider_thumb.rest = controls.slider_thumb.hover = controls.slider_thumb.pressed = 8.0f;

    // The thumb has no hover state in this era, so hover is the resting accent.
    controls.slider_fills = detail::ControlChrome::SliderFills{/*thumb=*/accent,
                                                               /*thumb_hover=*/accent,
                                                               /*thumb_pressed=*/p.chrome_strong,
                                                               /*thumb_disabled=*/p.chrome_disabled_faint,
                                                               /*track=*/p.text_40,
                                                               /*track_hover=*/p.text_60,
                                                               /*track_disabled=*/p.chrome_disabled_faint};

    // ---- Selection: a filled row, and no indicator anywhere ------------------------------------
    //
    // A list row's low, medium and high accent rungs. Setting these also suppresses the
    // accent pill in the list controls.
    const float sel_low          = dark ? 0.6f : 0.4f;
    const float sel_medium       = dark ? 0.8f : 0.6f;
    const float sel_high         = dark ? 0.9f : 0.7f;
    controls.row_selection_fills = detail::ControlChrome::
        AccentSelectionFills{/*rest=*/accent.with_alpha(sel_low),
                             /*hover=*/accent.with_alpha(sel_medium),
                             /*pressed=*/accent.with_alpha(sel_high),
                             /*popup_selected=*/accent.with_alpha(sel_low),
                             /*popup_selected_hover=*/accent.with_alpha(sel_medium)};

    // MDL2 has no NavigationShell or SelectorBar; its Sections marks selection in the label color.
    controls.accent_selection_indicator = false;

    // ---- Progress and severity ---------------------------------------------------------------
    //
    // ProgressBar's track: the low base color, 20 % of the foreground.
    controls.progress_track = p.text_20;

    // The era has no semantic color (an error text color arrives in 1809), so only the surface
    // the hues sit on is fixed.
    for (Theme::SeverityVisual* sv :
         {&theme.severity.informational, &theme.severity.success, &theme.severity.caution, &theme.severity.critical})
    {
        sv->background = theme.colors().background;
    }
    theme.severity.text_on_fill = p.white_100;
    theme.severity.stroke       = theme.surfaces.field_border;

    detail::fill_derived_colors(theme);

    // CalendarView's square cells, from the era's ramp; no corner radius before 2017.
    {
        detail::ControlChrome::CalendarChrome calendar;
        calendar.kind                                = detail::ControlChrome::CalendarChrome::Kind::Square;
        calendar.background                          = p.page_100;
        calendar.border                              = p.chrome_mid;
        calendar.heading                             = p.text_80;
        calendar.heading_hover                       = p.text_100;
        calendar.heading_pressed                     = p.text_60;
        calendar.heading_disabled                    = p.text_40;
        calendar.item_text                           = p.text_100;
        calendar.disabled_text                       = p.text_40;
        calendar.out_of_scope_fill                   = p.chrome_flyout;
        calendar.hover_border                        = p.text_40;
        calendar.pressed_border                      = p.text_60;
        calendar.selected_border                     = accent;
        calendar.selected_hover_border               = accent.with_alpha(dark ? 0.8f : 0.6f);
        calendar.selected_pressed_border             = accent.with_alpha(dark ? 0.9f : 0.7f);
        calendar.today_fill                          = accent;
        calendar.today_text                          = p.white_100;
        calendar.today_selected_ring                 = p.page_100;
        calendar.today_hover_border                  = p.text_40;
        calendar.today_pressed_border                = p.text_60;
        calendar.blackout_text                       = p.text_40;
        calendar.corner_radius                       = 0.0f;
        detail::edit_controls(theme).calendar_chrome = calendar;
    }

    // DatePicker and TimePicker: Windows 10's flyout from before 2017, with the 2 px field border
    // and dividers.
    controls.date_time_picker_chrome = detail::ControlChrome::
        DateTimePickerChrome{.kind       = detail::ControlChrome::DateTimePickerChrome::Kind::Flyout,
                             .plain_band = true,
                             .border     = 2.0f,
                             .divider    = 2.0f};
    return theme;
}

} // namespace ui
