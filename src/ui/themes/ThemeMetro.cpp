// SPDX-License-Identifier: MIT
#include "../../render/ControlGlyphs.hpp"
#include "ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/themes/MetroPalette.hpp"
#include "winui/ui/themes/Theme.hpp"

namespace ui
{

Theme Theme::metro(const MetroPalette& palette, bool dark)
{
    Theme                  theme;
    detail::ControlChrome& controls = detail::edit_controls(theme);
    theme.variant                   = ThemeVariant::Metro;
    theme.dark                      = dark;

    theme.corner_radius = 0.0f;
    // 8.1 rounds nothing at all, so every radius token is pinned.
    theme.overlay_corner_radius = 0.0f;
    // A text field's, a combo box's and a flyout's border are 2 px in both themes. Button is
    // asymmetric and overrides this per visual.
    theme.border_width = 2.0f;

    detail::edit_colors(theme).background = palette.page_background;
    detail::edit_colors(theme).text       = palette.foreground;

    // Windows 8/8.1 desktop windows were square.
    theme.chrome.corner_preference      = CornerPreference::DoNotRound;
    theme.chrome.allows_system_backdrop = false;

    detail::edit_colors(theme).focus       = colors::White;
    detail::edit_colors(theme).focus_inner = colors::Black;
    // The default light red is too dark on Metro's black page; Fluent dark's critical fill instead.
    if (dark)
    {
        theme.spelling_error = Color::from_argb(0xFFFF99A4);
    }
    // Both dashed rectangles are 1 px, and a 3 px margin keeps them clear of the button's 2 px
    // stroke.
    theme.focus_ring_outer_width = 1.0f;
    theme.focus_ring_inner_width = 1.0f;
    theme.focus_ring_margin      = 3.0f;
    theme.focus_visual           = FocusVisual::MetroDashedPair;

    // 8.1 has no NavigationShell, SidePane, Sections or Tabs. Its selection cue is the filled row
    // (or the label color, for a hub section header), so a bar on top would be two cues for one state.
    controls.accent_selection_indicator = false;

    // Press is a hard polarity flip, never a wash (see ButtonControl::sync_composition()).
    controls.press_state_flip = true;
    // Every state change in 8.1's controls is an instant switch at time 0.
    theme.anim_faster = theme.anim_fast = std::chrono::milliseconds{0};
    // Linear, and not a measurement: the era has no easing resource, and linear is what "no
    // easing" should degrade to. Inert while the durations are zero.
    theme.anim_fast_out_slow_in = CubicBezier{0.0f, 0.0f, 1.0f, 1.0f};

    // icon_font_family stays at the default: the era's Segoe UI Symbol uses different PUA
    // codepoints (U+E100-U+E1E9) from render/IconGlyph.hpp, so naming it would render missing glyphs.

    // Hierarchy comes from size/weight jumps and an explicit per-role line box.
    auto style = [](float size, FontWeight weight, float line_height)
    { return TextStyle{size, TextAlign::Leading, TextAlign::Center, "Segoe UI", weight, false, line_height}; };
    theme.type.header    = style(56.0f, FontWeight::Light, 40.0f);
    theme.type.subheader = style(26.667f, FontWeight::Light, 30.0f);
    theme.type.title     = style(14.667f, FontWeight::SemiBold, 20.0f);
    theme.type.subtitle  = style(14.667f, FontWeight::Regular, 20.0f); // the list item text style
    theme.type.body      = style(14.667f, FontWeight::SemiLight, 20.0f);
    theme.type.caption   = style(12.0f, FontWeight::Regular, 20.0f);
    // A Metro Button's content is centered (a control's default content alignment), while
    // the ramp is authored Leading for page text, so only size/weight/family come from Body.
    theme.text_style            = theme.type.body;
    theme.text_style.horizontal = TextAlign::Center;
    theme.text_style.vertical   = TextAlign::Center;

    theme.page.grid_unit          = 20.0f;
    theme.page.content_margin     = Edges{.left = 120.0f, .top = 140.0f, .right = 120.0f, .bottom = 50.0f};
    theme.page.header_height      = 140.0f;
    theme.page.item_spacing       = 10.0f;
    theme.page.command_bar_height = 68.0f;

    // A button's padding is a literal 12,4,12,4; 8.1 has no padding token for it.
    theme.padding = Symmetric{.horizontal = 12.0f, .vertical = 4.0f};

    detail::edit_colors(theme).field          = palette.field_background;
    detail::edit_colors(theme).field_text     = palette.field_text;
    theme.surfaces.field_border               = palette.field_border;
    detail::edit_colors(theme).selection      = palette.accent;
    detail::edit_colors(theme).selection_text = colors::White;
    detail::edit_colors(theme).text_disabled  = palette.foreground_pressed;
    // 8.1 forces its popups to the light theme, so menus stay white in both flavors.
    theme.surfaces.menu                = palette.menu_background;
    theme.surfaces.menu_text           = palette.menu_text;
    theme.surfaces.menu_highlight      = palette.menu_highlight;
    theme.surfaces.menu_highlight_text = palette.menu_highlight_text;
    // The light theme's flyout border. field_border is white here.
    theme.surfaces.menu_border  = Color::from_argb(0xFF000000);
    theme.surfaces.tooltip      = palette.tooltip_background;
    theme.surfaces.tooltip_text = palette.tooltip_text;
    // A 2 px border, then a padding of 10,6,10,7 inside it.
    theme.surfaces.tooltip_border   = palette.tooltip_border;
    controls.tooltip_padding        = Edges{.left = 12.0f, .top = 8.0f, .right = 12.0f, .bottom = 9.0f};
    detail::edit_colors(theme).link = palette.hyperlink;
    // One flat mid-gray stroke; with both stops equal the etched groove collapses to a single line.
    theme.surfaces.separator_light     = palette.separator;
    theme.surfaces.separator_dark      = palette.separator;
    detail::edit_colors(theme).divider = palette.separator;
    // 8.1's scroll bar colors are the same in the dark and the light theme.
    theme.surfaces.scrollbar_track = palette.scrollbar_track;
    theme.surfaces.scrollbar_thumb = palette.scrollbar_thumb;
    theme.surfaces.scrollbar_arrow = palette.scrollbar_arrow;

    // Dark is a pure 2 px outline with no fill, light a flat 70 % gray fill with no border: two
    // constructions of one control.
    theme.button_normal = ButtonVisual{palette.button_background, palette.button_border, palette.button_foreground};
    theme.button_normal.border_width = palette.button_border_width;
    theme.button_normal.draw_border  = dark;

    theme.button_hover = ButtonVisual{palette.button_hover_background, palette.button_border, palette.button_foreground};
    theme.button_hover.border_width = palette.button_border_width;
    theme.button_hover.draw_border  = dark;

    theme.button_pressed              = ButtonVisual{palette.button_pressed_background,
                                                     palette.button_pressed_background,
                                                     palette.button_pressed_foreground};
    theme.button_pressed.border_width = palette.button_border_width;
    theme.button_pressed.draw_border  = dark;

    theme.button_disabled              = ButtonVisual{palette.button_disabled_background,
                                                      palette.button_disabled_border,
                                                      palette.button_disabled_foreground};
    theme.button_disabled.border_width = palette.button_border_width;
    theme.button_disabled.draw_border  = dark;

    // The accent button uses the accent raw, never a ramp rung.
    theme.accent_button_normal   = ButtonVisual{palette.accent, palette.accent, colors::White};
    theme.accent_button_hover    = ButtonVisual{palette.accent_hover, palette.accent_hover, colors::White};
    theme.accent_button_pressed  = ButtonVisual{palette.accent_pressed, palette.accent_pressed, colors::White};
    theme.accent_button_disabled = theme.button_disabled;

    // The flat app-bar button: borderless, a wash on hover, the accent on press.
    theme.subtle_button_normal = ButtonVisual{colors::Transparent, colors::Transparent, palette.foreground};
    theme.subtle_button_hover  = ButtonVisual{palette.button_hover_background, colors::Transparent, palette.foreground};
    theme.subtle_button_pressed = ButtonVisual{palette.accent, colors::Transparent, colors::White};
    theme.subtle_button_disabled = ButtonVisual{colors::Transparent, colors::Transparent, palette.button_disabled_foreground};

    // CheckBox/RadioButton: the box and ring never change color when checked, only the glyph
    // appears. RadioButton's colors are CheckBox's, state for state.
    const ButtonVisual box_off{palette.toggle_box_background, palette.toggle_box_border, colors::Transparent};
    const ButtonVisual box_on{palette.toggle_box_background, palette.toggle_box_border, palette.toggle_glyph};
    ButtonVisual box_off_disabled{palette.button_disabled_background, palette.button_disabled_border, colors::Transparent};
    // The disabled mark is #66000000 in both themes.
    ButtonVisual box_on_disabled = box_off_disabled;
    box_on_disabled.text         = palette.toggle_glyph.with_alpha(0.4f);
    controls.set_toggle_visuals(ToggleVisuals{ToggleVisual{box_off, box_on},
                                              ToggleVisual{box_off, box_on},
                                              ToggleVisual{box_off, box_on},
                                              ToggleVisual{box_off_disabled, box_on_disabled}});

    // Of the three toggles only ToggleSwitch's curtain, the track's filled part, turns accent.
    const ButtonVisual curtain{palette.accent, palette.accent, colors::White};
    ButtonVisual       curtain_disabled = curtain;
    curtain_disabled.background = curtain_disabled.border = palette.accent.with_alpha(0.4f);
    controls.toggle_track.normal.on = controls.toggle_track.hover.on = controls.toggle_track.pressed.on = curtain;
    controls.toggle_track.disabled.on = curtain_disabled;

    // The check box's and the radio button's borders are 0 in dark and 2 in light, the inverse of Button.
    for (ToggleVisuals* tv : controls.toggles())
    {
        for (ButtonVisual* v : tv->all())
        {
            v->border_width = palette.indicator_border_width;
        }
    }

    // The indeterminate mark is a 9x9 square in the check box's foreground color.
    controls.check_box_indeterminate = IndeterminateVisuals::like_checked(controls.check_box_box,
                                                                          IndeterminateMark::Square,
                                                                          glyphs::CheckBoxIndeterminate,
                                                                          9.0f);

    // ---- Per-control geometry, measured from 8.1's own controls ---------------------------
    //
    // CheckBox's box is 21x21, RadioButton's ring 23x23.
    controls.toggle_box_size        = 21.0f;
    controls.radio_box_size         = 23.0f;
    controls.toggle_content_spacing = 6.0f;
    controls.radio_dot.rest = controls.radio_dot.hover = controls.radio_dot.pressed = 13.0f;

    // ToggleSwitch: a 44 px curtain inside 2 px and 1 px borders makes a 50 x 19 track; the 12 px
    // knob travels 38.
    controls.toggle_switch_track_size         = Size{50.0f, 19.0f};
    controls.toggle_switch_track_radius_ratio = 0.0f;
    controls.toggle_knob.rest                 = 12.0f;
    controls.toggle_knob.hover                = 12.0f;
    controls.toggle_knob.pressed              = Size{12.0f, 19.0f};
    controls.toggle_knob.travel               = 38.0f;

    // Slider: a track height of 11 sizes both the track and the square thumb.
    controls.slider_track_thickness = 11.0f;
    controls.slider_thumb.shape     = SliderThumbShape::RoundedRect;
    controls.slider_thumb.radius    = 0.0f;
    controls.slider_thumb.outer     = 11.0f;
    controls.slider_thumb.rest = controls.slider_thumb.hover = controls.slider_thumb.pressed = 11.0f;

    // ---- Progress and severity ---------------------------------------------------------------
    //
    // 8.1: the dark bar is a lighter violet (#FF5B2EC5); accent_hover is the
    // closest slot and still follows accent_override.
    controls.progress_track = dark ? Color::from_argb(0x59FFFFFF) : Color::from_argb(0x30000000);
    controls.progress_fill  = dark ? palette.accent_hover : palette.accent;

    // The Windows 8 UX guide asks for one app color for all message bars, so all four severities
    // are the same accent.
    const Theme::SeverityVisual bar{palette.accent, theme.colors().background};
    theme.severity.informational = theme.severity.success = theme.severity.caution = theme.severity.critical = bar;
    theme.severity.neutral                                                                                   = bar.fill;
    theme.severity.text_on_fill = colors::White;
    theme.severity.stroke       = theme.colors().divider;

    detail::fill_derived_colors(theme);

    // CalendarView as Windows 8's desktop month calendar (the Vista design in the Windows 8
    // style). Store apps had no calendar before Windows 10, so the dark theme takes the pack's
    // own colors.
    {
        detail::ControlChrome::CalendarChrome calendar;
        calendar.kind             = detail::ControlChrome::CalendarChrome::Kind::Aero;
        calendar.background       = dark ? palette.page_background : colors::White;
        calendar.border           = dark ? palette.field_border : Color::from_argb(0xFF979797);
        calendar.heading          = dark ? palette.foreground : colors::Black;
        calendar.heading_hover    = dark ? palette.hyperlink : Color::from_argb(0xFF0066CC);
        calendar.heading_pressed  = calendar.heading_hover;
        calendar.heading_disabled = palette.button_disabled_foreground;
        calendar.weekday_text     = calendar.heading;
        calendar.separator        = dark ? palette.separator : Color::from_argb(0xFFF5F5F5);
        calendar.item_text        = calendar.heading;
        calendar.trailing_text    = dark ? palette.foreground_secondary : Color::from_argb(0xFF646464);
        calendar.selected_border  = Color::from_argb(0xFF26A0DA);
        calendar.selected_fill    = Color::from_argb(0x3D26A0DA);
        calendar.hover_border     = Color::from_argb(0xA826A0DA);
        calendar.hover_fill       = Color::from_argb(0x1F26A0DA);
        calendar.today_mark       = calendar.heading_hover;
        calendar.today_text       = calendar.heading_hover;
        calendar.footer_text      = calendar.heading;
        calendar.blackout_text    = palette.button_disabled_foreground;
        calendar.disabled_text    = palette.button_disabled_foreground;
        detail::edit_controls(theme).calendar_chrome = calendar;
    }

    // DatePicker and TimePicker: Windows 8.1's three drop-down lists.
    controls.date_time_picker_chrome = detail::ControlChrome::DateTimePickerChrome{
        .kind = detail::ControlChrome::DateTimePickerChrome::Kind::ComboBoxes};
    return theme;
}

} // namespace ui
