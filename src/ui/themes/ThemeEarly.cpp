// SPDX-License-Identifier: MIT
#include "ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/themes/EarlyPalette.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <optional>

namespace ui
{

Theme Theme::early(const EarlyPalette& palette)
{
    Theme theme; // theme.palette (Fluent's tier-1 table) is left default, as every other era resolver does
    theme.variant                   = ThemeVariant::Windows1x;
    detail::ControlChrome& controls = detail::edit_controls(theme);
    // The desktop edit control: no clear button, and no spin-button popup.
    controls.spin_button_popup      = false;
    controls.text_box_delete_button = false;

    // ---- Geometry: one pixel, and nothing else ------------------------------------------
    // cxBorder is 1 on square-pixel adapters and 2 on CGA/EGA low resolution; this pack
    // deliberately picks the square-pixel adapters.
    theme.border_width = 1.0f;
    // Square everywhere but the push button, whose OBM_BTNCORNERS bitmap is byte-identical from
    // Windows 1.01 through 3.0.
    theme.corner_radius         = 0.0f;
    theme.overlay_corner_radius = 0.0f;
    // Glyph A of that sheet is a 6x6 ring with one pixel cut off each corner, a 1px radius. Glyph B
    // (the default button's 2px ring) is the Accent button further down.
    theme.button_corner_radius = 1.0f;

    // 1-bit glyphs and hard 1px edges: the era has no gray level to antialias with.
    theme.rasterization = SetRasterization{GeometryAA::Aliased, TextAA::Aliased, /*snap_to_device_pixels=*/true};

    // ---- The page is `COLOR_WINDOW`, and that is the single biggest signature -------------
    // There is no COLOR_BTNFACE and no gray dialog background in this era.
    detail::edit_colors(theme).background = palette.window;
    detail::edit_colors(theme).text       = palette.window_text;

    // ---- Non-client chrome ----------------------------------------------------------------
    theme.chrome.corner_preference  = CornerPreference::DoNotRound;
    theme.chrome.dark_titlebar      = false;
    theme.chrome.caption_color      = palette.active_caption;
    theme.chrome.caption_text_color = palette.caption_text;
    // `COLOR_ACTIVEBORDER` arrives with Windows 2.0; on a 1.x row the frame color is the only one.
    theme.chrome.border_color = palette.active_border.value_or(palette.window_frame);
    // Flat opaque fills only (`ChromeHints::allows_system_backdrop`).
    theme.chrome.allows_system_backdrop = false;

    // ---- Surfaces --------------------------------------------------------------------------
    detail::edit_colors(theme).field      = palette.window;
    detail::edit_colors(theme).field_text = palette.window_text;
    theme.surfaces.field_border           = palette.window_frame;

    // No COLOR_HIGHLIGHT until Windows 3.0: a selection swaps foreground and background
    // (`early_reverse_video`).
    const auto [selection_bg, selection_fg]   = early_reverse_video(palette.window, palette.window_text);
    detail::edit_colors(theme).selection      = selection_bg;
    detail::edit_colors(theme).selection_text = selection_fg;

    // No COLOR_GRAYTEXT either. The era grays with a 50 % dither Renderer cannot draw, so this is
    // its average (`early_dither_gray`). Only the text grays; frames stay unchanged.
    detail::edit_colors(theme).text_disabled = early_dither_gray(palette.window_text, palette.window);

    theme.surfaces.menu      = palette.menu;
    theme.surfaces.menu_text = palette.menu_text;
    // A menu highlight is the same swap. Real InvertRect would turn the 2.03 cyan menu bar red;
    // this models the documented "swap foreground and background" rule instead.
    const auto [menu_hi_bg, menu_hi_fg] = early_reverse_video(palette.menu, palette.menu_text);
    theme.surfaces.menu_highlight       = menu_hi_bg;
    theme.surfaces.menu_highlight_text  = menu_hi_fg;
    theme.surfaces.menu_border          = palette.window_frame;

    // No tooltip, COLOR_INFOBK or hyperlink in this era: a plain framed white box and window text.
    theme.surfaces.tooltip          = palette.window;
    theme.surfaces.tooltip_text     = palette.window_text;
    theme.surfaces.tooltip_border   = palette.window_frame;
    detail::edit_colors(theme).link = palette.window_text;

    // A one-pixel WINDOWFRAME rule. The etched groove needs a highlight/shadow pair the era lacks,
    // so both stops collapse onto the frame color.
    theme.divider_visual               = ThemeDividerVisual::FlatStroke;
    detail::edit_colors(theme).divider = palette.window_frame;
    theme.surfaces.separator_light     = palette.window_frame;
    theme.surfaces.separator_dark      = palette.window_frame;

    // COLOR_SCROLLBAR is the trough (a dark #3F3F3F on the 2.03 row). The thumb and arrows are 1bpp
    // art blitted into it, so they take COLOR_WINDOW and COLOR_WINDOWFRAME.
    theme.surfaces.scrollbar_track = palette.scrollbar;
    theme.surfaces.scrollbar_thumb = palette.window;
    theme.surfaces.scrollbar_arrow = palette.window_frame;
    {
        // The era's own bar in a gutter: the shaft, the stemmed arrows boxed in COLOR_WINDOWFRAME
        // and a fixed white scroll box, CONFIG.ASM's 18 tall on a vertical bar and 20 wide on a
        // horizontal one. The bar's thickness is not on file; the square-pixel arrow art is 15 x 15,
        // so 17 holds it inside its frame.
        detail::ControlChrome::GutterScrollBar bar;
        bar.style                  = detail::ControlChrome::GutterScrollBar::Style::Win1x2x;
        bar.thickness              = 17.0f;
        bar.fixed_thumb_vertical   = 18.0f;
        bar.fixed_thumb_horizontal = 20.0f;
        bar.shaft                  = palette.scrollbar;
        bar.arrow                  = palette.window_frame;
        bar.frame                  = palette.window_frame;
        bar.box                    = palette.window;
        bar.corner                 = palette.window;
        controls.gutter_scroll_bar = std::move(bar);
    }
    {
        // BS_GROUPBOX: "a rectangle that has an identifying text string in its upper left
        // corner", one WINDOWFRAME pixel like every frame of the era. The shipped Control Panel
        // insets its children 4 DU across and 12 DU down.
        detail::ControlChrome::EraGroupBox box;
        box.frame              = palette.window_frame;
        box.header             = palette.window_text;
        box.frame_raise        = 1.0f;
        box.padding            = Edges{.left = 8.0f, .top = 24.0f, .right = 8.0f, .bottom = 8.0f};
        controls.era_group_box = box;
    }

    // ---- Metrics --------------------------------------------------------------------------
    // Push buttons are 14 DU (21 px) tall and Ok/Cancel 32 x 14 DU, so 8 px padding each side. The
    // odd vertical pixel goes at the bottom, where Windows 1.0x draws the focus underscore.
    theme.padding = Edges{.left = 8.0f, .top = 3.0f, .right = 8.0f, .bottom = 4.0f};

    // OBM_CHECKBOXES cells are 14 x 14 at EGA high resolution; the radio is a 14px ring around an
    // 8px disc.
    controls.toggle_box_size   = 14.0f;
    controls.toggle_box_radius = 0.0f; // square; the ring is drawn as a circle by the control
    controls.radio_dot.rest = controls.radio_dot.hover = controls.radio_dot.pressed = 8.0f;
    // Not stated anywhere, so this keeps the classic packs' value.
    controls.toggle_content_spacing = 4.0f;

    // ---- Type: one face, fixed pitch, regular weight, and no ramp at all --------------------
    // The era's System font is fixed pitch at an 8 px advance, and the dialog geometry depends on
    // that, so a monospaced face stands in. Consolas at 14 px advances 7.70 px; it is a trade-off.
    theme.text_style             = TextStyle{};
    theme.text_style.font_family = "Consolas";
    theme.text_style.font_size   = 14.0f;
    theme.text_style.line_height = 14.0f;
    theme.text_style.weight      = FontWeight::Regular;
    theme.text_style.horizontal  = TextAlign::Center;
    theme.text_style.vertical    = TextAlign::Center;

    // No type ramp at all: every role collapses onto `text_style`.
    theme.type
        .header = theme.type
                      .subheader = theme.type.title = theme.type.subtitle = theme.type.body = theme.type.caption = theme.text_style;

    // ---- States ------------------------------------------------------------------------------
    // Windows 2.0 focus is a dotted box (1.0x used a blinking underscore, not modeled). No source
    // states its pattern, so this reuses the shared classic dotted visual.
    theme.focus_visual                     = FocusVisual::ClassicDotted;
    detail::edit_colors(theme).focus       = palette.window_text;
    detail::edit_colors(theme).focus_inner = palette.window;

    // Nothing animates: InvertRect is the only state operation.
    theme.anim_faster = theme.anim_fast = theme.anim_normal = std::chrono::milliseconds{0};
    // Menus and drop-down lists included (`Theme::popup_motion`).
    theme.popup_motion.menu     = ThemePopupAnimation::None;
    theme.popup_motion.list     = ThemePopupAnimation::None;
    theme.popup_motion.open     = std::chrono::milliseconds{0};
    theme.popup_motion.close    = std::chrono::milliseconds{0};
    theme.anim_fast_out_slow_in = CubicBezier{0.0f, 0.0f, 1.0f, 1.0f};

    // No popup casts a shadow; CS_DROPSHADOW is a Windows XP API.
    theme.popup_shadow = ThemePopupShadow::None;

    // ---- The frame a Frame::Themed window draws ------------------------------------------
    // The caption buttons are 1-bit art: white boxes with window-text ink whatever the scheme.
    // Windows 2.0's art is 25 x 19 at VGA. 1.0x's System-menu box is 13 pixels wide at EGA and
    // the caption is taken at the 2.x height. A 2.x row (one with COLOR_ACTIVEBORDER) gets the
    // sizing frame of two lines around the border color, 1.x a single frame line.
    {
        const bool                         two = palette.active_border.has_value();
        detail::ControlChrome::WindowFrame frame;
        frame.kind = two ? detail::ControlChrome::WindowFrame::Kind::Win2x : detail::ControlChrome::WindowFrame::Kind::Win1x;
        frame.border           = two ? 5.0f : 1.0f;
        frame.frame_line       = palette.window_frame;
        frame.active_border    = palette.active_border.value_or(palette.window_frame);
        frame.inactive_border  = palette.inactive_border.value_or(frame.active_border);
        frame.caption_height   = 19.0f;
        frame.caption_rule     = 1.0f;
        frame.active_caption   = LinearGradientBrush{{GradientStop{0.0f, palette.active_caption},
                                                      GradientStop{1.0f, palette.active_caption}},
                                                     GradientAxis::Horizontal};
        frame.inactive_caption = LinearGradientBrush{{GradientStop{0.0f, palette.inactive_caption},
                                                      GradientStop{1.0f, palette.inactive_caption}},
                                                     GradientAxis::Horizontal};
        frame.active_text      = palette.caption_text;
        // COLOR_CAPTIONTEXT is the active title only, and 2.x's inactive caption is white, so its
        // inactive title is the window text. 1.x's inactive captions are dark enough to keep it.
        frame.inactive_text   = two ? palette.window_text : palette.caption_text;
        frame.font_family     = theme.text_style.font_family;
        frame.font_size       = theme.text_style.font_size;
        frame.font_weight     = theme.text_style.weight;
        frame.title_centered  = true;
        frame.menu_box_width  = two ? 25.0f : 13.0f;
        frame.button          = Size{26.0f, 19.0f}; // the box and the frame line on its left
        frame.button_face     = palette.window;
        frame.glyph           = palette.window_text;
        controls.window_frame = frame;
    }

    // No scrim: the era has no alpha. WS_DLGFRAME's double frame has no Theme field.
    detail::edit_colors(theme).scrim = colors::Transparent;

    // No accent color; the era's selection cue is reverse video.
    controls.accent_selection_indicator = false;

    // A pressed push button is "blackened" (inverted), not pushed in, and the label does not move.
    // `press_state_flip` suppresses Button's hover/press wash.
    controls.press_state_flip       = true;
    controls.pressed_content_offset = Point{0.0f, 0.0f};

    // ---- Buttons: a rounded one-pixel outline on the page, with no face of its own ----------
    // With no COLOR_BTNFACE, a button uses the same window background as every other control.
    auto outline = [&](const Color& fill, const Color& stroke, const Color& text)
    {
        ButtonVisual v = ButtonVisual{fill, stroke, text};
        // Drawn inside `bounds`: the fill is the page's own white, so a clipped outline would
        // leave nothing of the control.
        v.border_style = BorderStyle::Flat; // never Raised/Sunken: there is no bevel to draw
        v.draw_border  = true;
        v.border_width = 1.0f;
        return v;
    };

    theme.button_normal = outline(palette.window, palette.window_frame, palette.window_text);
    // No hover state: the era's books never mention one and BN_* has no pointer entry or exit.
    // Pointer-over is identical to rest on purpose.
    theme.button_hover = theme.button_normal;
    // Inverted, per `BM_SETSTATE`.
    theme.button_pressed = outline(palette.window_text, palette.window_text, palette.window);
    // "When a button cannot be chosen, the label in the button should be grayed": only the label.
    theme.button_disabled = outline(palette.window, palette.window_frame, theme.colors().text_disabled);

    // No accent color, so `ButtonVariant::Accent` is the default push button (BS_DEFPUSHBUTTON):
    // same colors, twice the stroke, like OBM_BTNCORNERS glyph B.
    auto default_button = [&](const ButtonVisual& plain)
    {
        ButtonVisual v = plain;
        v.border_width = 2.0f;
        return v;
    };
    theme.accent_button_normal   = default_button(theme.button_normal);
    theme.accent_button_hover    = default_button(theme.button_hover);
    theme.accent_button_pressed  = default_button(theme.button_pressed);
    theme.accent_button_disabled = default_button(theme.button_disabled);

    // The era has no toolbar, so a subtle button reuses the plain one.
    theme.subtle_button_normal   = theme.button_normal;
    theme.subtle_button_hover    = theme.button_hover;
    theme.subtle_button_pressed  = theme.button_pressed;
    theme.subtle_button_disabled = theme.button_disabled;

    // ---- Toggles ----------------------------------------------------------------------------
    // A flat 14x14 square, checked with a corner-to-corner X (already in Windows 1.01's artwork).
    // The tick is OBM_CHECK, the menu mark.
    controls.check_glyph = glyphs::Cancel;

    const ButtonVisual box = outline(palette.window, palette.window_frame, palette.window_text);
    const ButtonVisual box_disabled = outline(palette.window, theme.colors().text_disabled, theme.colors().text_disabled);
    // Every state is the same well; only the glyph changes. OBM_CHECKBOXES' pressed column thickens
    // the stroke, which ButtonVisual cannot express. A radio's dot is drawn in `text`.
    controls.set_toggle_visuals(ToggleVisuals{ToggleVisual{box, box},
                                              ToggleVisual{box, box},
                                              ToggleVisual{box, box},
                                              ToggleVisual{box_disabled, box_disabled}});

    // BS_3STATE's grayed value: row 2 of OBM_CHECKBOXES, the outline around a 50 % checkerboard
    // and no X. On a 1bpp display that checkerboard is the only gray.
    const IndeterminateVisual grayed{box, std::nullopt, palette.window_text};
    const IndeterminateVisual grayed_disabled{box_disabled, std::nullopt, theme.colors().text_disabled};
    controls.check_box_indeterminate = IndeterminateVisuals{grayed,
                                                            grayed,
                                                            grayed,
                                                            grayed_disabled,
                                                            IndeterminateMark::None,
                                                            controls.check_glyph};
    // ToggleSwitch has no era equivalent, so it is a square latched well. It reads the `bevel_*`
    // fields directly, so they all collapse onto COLOR_WINDOWFRAME with one ring: a flat 1px frame.
    for (ButtonVisual* v : controls.toggle_track.all())
    {
        v->bevel_light = v->bevel_highlight = v->bevel_shadow = v->bevel_dark_shadow = v->border;
        v->bevel_rings                                                               = 1;
    }
    controls.toggle_switch_shape = ToggleSwitchShape::ClassicWell;

    // ---- Controls the era does not have -----------------------------------------------------
    // The era has five window classes. Everything else is assembled from its own parts.

    // Slider: a one-pixel WINDOWFRAME rule and a solid OBM_BTSIZE block (15 x 14), the same in
    // every state. Solid, because a COLOR_WINDOW thumb on a COLOR_WINDOW page would vanish.
    controls.slider_thumb.shape  = SliderThumbShape::RoundedRect;
    controls.slider_thumb.radius = 0.0f;
    controls.slider_thumb.outer  = 14.0f;
    controls.slider_thumb.rest = controls.slider_thumb.hover = controls.slider_thumb.pressed = 15.0f;
    controls.slider_track_thickness                                                          = 1.0f;
    controls.slider_value_fill                                                               = false;
    controls.slider_fills = detail::ControlChrome::SliderFills{palette.window_text,
                                                               palette.window_text,
                                                               palette.window_text,
                                                               theme.colors().text_disabled,
                                                               palette.window_frame,
                                                               palette.window_frame,
                                                               theme.colors().text_disabled};

    // ProgressBar (an invention): a field's 1 px WINDOWFRAME rule around the window color, filled
    // with one ink block, reverse video being the era's only state color.
    controls.progress_track      = palette.window;
    controls.progress_fill       = palette.window_text;
    controls.framed_progress_bar = detail::ControlChrome::FramedProgressBar{.top_left     = palette.window_frame,
                                                                            .bottom_right = palette.window_frame,
                                                                            .padding      = 0.0f,
                                                                            .chunked      = false};

    // Severity: the OIC_* icons are 1bpp, so every severity is an ink disc with the page as the
    // glyph, inside a WINDOWFRAME rule.
    const Theme::SeverityVisual ink_sign{palette.window_text, palette.window};
    theme.severity.informational = theme.severity.success = theme.severity.caution = theme.severity.critical = ink_sign;
    theme.severity.neutral      = ink_sign.fill;
    theme.severity.text_on_fill = palette.window;
    theme.severity.stroke       = palette.window_frame;

    detail::fill_derived_colors(theme);

    // CalendarView: the era had no such control. Its stand-in is the classic month calendar built
    // from the era's parts: the caption band, the weekday names in window text, the selected day
    // inverted (reverse video, the era's only state color) and today framed.
    {
        detail::ControlChrome::CalendarChrome calendar;
        calendar.kind            = detail::ControlChrome::CalendarChrome::Kind::Classic;
        calendar.background      = palette.window;
        calendar.title_fill      = palette.active_caption;
        calendar.title_text      = palette.caption_text;
        calendar.weekday_text    = palette.window_text;
        calendar.separator       = palette.window_frame;
        calendar.item_text       = palette.window_text;
        calendar.trailing_text   = theme.colors().text_disabled;
        calendar.selected_fill   = palette.window_text;
        calendar.today_mark      = palette.window_frame;
        calendar.footer_text     = palette.window_text;
        calendar.blackout_text   = theme.colors().text_disabled;
        calendar.disabled_text   = theme.colors().text_disabled;
        calendar.cell_width      = 32.0f; // the fixed-pitch System font's three-letter names
        calendar.rect_selection  = true;
        calendar.today_frame     = true;
        controls.calendar_chrome = calendar;
    }

    // DatePicker and TimePicker: comctl32's date and time picker.
    detail::edit_controls(theme).date_time_picker_chrome = detail::ControlChrome::DateTimePickerChrome{
        .kind = detail::ControlChrome::DateTimePickerChrome::Kind::Field};
    return theme;
}

} // namespace ui
