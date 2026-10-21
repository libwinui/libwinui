// SPDX-License-Identifier: MIT
#include "winui/ui/themes/Theme.hpp"

#include "ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/themes/ClassicPalette.hpp"
#include "winui/ui/themes/Palette.hpp"

#include <atomic>
#include <optional>

namespace ui
{
namespace
{

// `soft` picks `DrawEdge`'s BF_SOFT arrangement, the *command button* shape, as against the
// window-border/field shape every other classic surface draws (render/Bevel.hpp).
ButtonVisual classic_bevel_visual(const Color&            face,
                                  const Color&            text,
                                  BorderStyle             style,
                                  const ClassicBevelRamp& ramp,
                                  bool                    soft = false)
{
    ButtonVisual v;
    v.background        = face;
    v.border            = ramp.dark_shadow; // fallback for anything that ignores border_style
    v.text              = text;
    v.border_style      = style;
    v.bevel_light       = ramp.light;
    v.bevel_highlight   = ramp.highlight;
    v.bevel_shadow      = ramp.shadow;
    v.bevel_dark_shadow = ramp.dark_shadow;
    v.bevel_soft        = soft;
    return v;
}

} // namespace

std::uint64_t detail::next_theme_generation() noexcept
{
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

void detail::fill_derived_colors(Theme& theme)
{
    ThemeColors& c          = edit_colors(theme);
    const bool   fluent     = (theme.variant == ThemeVariant::Fluent2 || theme.variant == ThemeVariant::Fluent) &&
                              !theme.high_contrast;
    c.text_secondary        = fluent ? theme.palette.text_fill_secondary : c.text;
    c.text_on_accent        = theme.accent_button_normal.text;
    c.card                  = fluent ? theme.palette.card_background_default : c.background;
    c.overlay               = theme.surfaces.menu;
    c.accent                = theme.accent_button_normal.background;
    c.accent_hover          = theme.accent_button_hover.background;
    c.accent_pressed        = theme.accent_button_pressed.background;
    c.control_fill          = theme.button_normal.background;
    c.control_fill_hover    = theme.button_hover.background;
    c.control_fill_pressed  = theme.button_pressed.background;
    c.control_fill_disabled = theme.button_disabled.background;
    c.control_stroke        = theme.button_normal.border;
    c.control_stroke_strong = fluent ? theme.palette.control_strong_stroke_default : theme.surfaces.field_border;
    c.critical              = theme.severity.critical.fill;
    c.caution               = theme.severity.caution.fill;
    c.success               = theme.severity.success.fill;
    c.informational         = theme.severity.informational.fill;
}

namespace
{

// The accent states fluent(const ThemeColors&) can set apart from the accent itself; unset ones
// follow `Palette::accent_fill_default` as fluent(const Palette&) derives them.
struct FluentAccentStates
{
    std::optional<Color> hover;
    std::optional<Color> pressed;
    std::optional<Color> informational;
};

Theme fluent_theme(const Palette& palette, const FluentAccentStates& states)
{
    Theme                  theme;
    detail::ControlChrome& controls = detail::edit_controls(theme);
    theme.variant                   = ThemeVariant::Fluent2;
    theme.palette                   = palette;
    theme.dark                      = palette.dark;
    controls.check_mark_draws_in    = true;

    detail::edit_colors(theme).background = palette.solid_background_base;
    detail::edit_colors(theme).text       = palette.text_fill_primary;

    // `field` is a distinct "well" surface (Button's Neutral rest fill), not the page background.
    detail::edit_colors(theme).field      = palette.control_fill_default;
    detail::edit_colors(theme).field_text = palette.text_fill_primary;
    theme.surfaces.field_border           = palette.control_stroke_default;

    // The text field's raised border: a 0.5 -> 1.0 gradient from the strong control stroke to the
    // flat control stroke fallback above.
    controls.field_border_gradient = LinearGradientBrush{
        {GradientStop{0.5f, palette.control_strong_stroke_default}, GradientStop{1.0f, palette.control_stroke_default}}};
    // The text field's hover, focused and disabled fills. The focused one is opaque, so the
    // field stops being a wash over the page while you type in it.
    controls.field_fills = detail::ControlChrome::FieldFills{palette.control_fill_secondary,
                                                             palette.control_fill_input_active,
                                                             palette.control_fill_disabled};
    // A focused text field's underline is the accent fill, not the outer focus stroke (see
    // ControlChrome::field_focus_underline).
    controls.field_focus_underline            = palette.accent_fill_default;
    detail::edit_colors(theme).selection      = palette.accent_fill_default;
    detail::edit_colors(theme).selection_text = palette.text_on_accent_primary;
    detail::edit_colors(theme).text_disabled  = palette.text_fill_disabled;
    // Every light-dismiss overlay and the tooltip get Fluent 2's in-app acrylic, here its
    // fallback color. The page color would sink a popup into the surface it floats over.
    theme.surfaces.menu                = palette.acrylic_fallback_default;
    theme.surfaces.menu_text           = palette.text_fill_primary;
    theme.surfaces.menu_highlight      = palette.subtle_fill_secondary;
    theme.surfaces.menu_highlight_text = palette.text_fill_primary;
    theme.surfaces.menu_pressed        = palette.subtle_fill_tertiary; // a pressed menu item's fill
    theme.surfaces.menu_secondary_text = palette.text_fill_secondary;  // the shortcut and chevron text
    // Same brush for the tooltip. The page's secondary color is a step down, which in Dark would
    // make a tooltip darker than its page.
    theme.surfaces.tooltip      = palette.acrylic_fallback_default;
    theme.surfaces.tooltip_text = palette.text_fill_primary;
    // TeachingTip: the tertiary solid background, not the acrylic
    // fallback the light-dismiss surfaces use (#282828 / #F9F9F9 against #2C2C2C / #F9F9F9).
    theme.surfaces.teaching_tip       = palette.solid_background_tertiary;
    theme.surfaces.surface_stroke     = palette.surface_stroke_default;
    theme.surfaces.dialog_top_overlay = palette.layer_fill_alt;
    theme.surfaces.dialog_separator   = palette.card_stroke_default;
    // The low chrome color, which the palette has no token for, in its dark and light values; told
    // apart by the page color the palette is built around.
    theme.surfaces.split_view_pane         = palette.solid_background_base.r < 0.5f ? Color::from_argb(0xFF171717)
                                                                                    : Color::from_argb(0xFFF2F2F2);
    detail::edit_colors(theme).link        = palette.accent_text_primary;
    theme.surfaces.hyperlink_hover         = palette.accent_text_secondary;
    theme.surfaces.hyperlink_pressed       = palette.accent_text_tertiary;
    theme.surfaces.hyperlink_disabled      = palette.accent_text_disabled;
    theme.surfaces.separator_light         = palette.control_stroke_secondary;
    theme.surfaces.separator_dark          = palette.control_stroke_default;
    detail::edit_colors(theme).divider     = palette.divider_stroke_default;
    theme.divider_visual                   = ThemeDividerVisual::FlatStroke; // toolbar and menu separators
    controls.scroll_button_chromeless      = true;                           // AnnotatedScrollBar's step buttons
    controls.list_item_subtle_selection    = true;                           // ListView/TreeView/TableView rows
    controls.card_surfaces                 = true;                           // Expander's card header/content chrome
    controls.chromeless_text_states        = true;                           // SelectorBar/Breadcrumb item text ramps
    controls.tab_layer_surfaces            = true;                           // Tabs control's tab-pill strip chrome
    controls.person_picture_dedicated_fill = true;                           // PersonPicture's placeholder ellipse
    controls.toggle_knob_off_foreground    = true;                           // the off knob's fill
    theme.surfaces.scrollbar_track         = palette.solid_background_secondary;
    theme.surfaces.scrollbar_thumb         = palette.control_strong_stroke_default;
    theme.surfaces.scrollbar_arrow         = palette.text_fill_secondary;

    detail::edit_colors(theme).focus       = palette.focus_stroke_outer;
    detail::edit_colors(theme).focus_inner = palette.focus_stroke_inner;
    theme.spelling_error                   = palette.semantic_fill_critical;

    // InfoBar / ProgressBar (see `Theme::severity`). `informational.fill` is set below, once
    // `accent` is known.
    theme.severity.success = Theme::SeverityVisual{palette.semantic_fill_success, palette.semantic_background_success};
    theme.severity.caution = Theme::SeverityVisual{palette.semantic_fill_caution, palette.semantic_background_caution};
    theme.severity.critical = Theme::SeverityVisual{palette.semantic_fill_critical, palette.semantic_background_critical};
    theme.severity.informational.background = palette.semantic_background_informational;
    theme.severity.neutral                  = palette.semantic_fill_solid_neutral;
    theme.severity.text_on_fill             = palette.text_on_accent_primary;
    theme.severity.stroke                   = palette.card_stroke_default;
    controls.progress_track                 = palette.control_strong_stroke_default;

    // Neutral Button. Pressed and disabled use the flat control stroke, rest and hover the raised
    // gradient border set just below.
    theme.button_normal = ButtonVisual{palette.control_fill_default, palette.control_stroke_default, palette.text_fill_primary};
    theme.button_hover = ButtonVisual{palette.control_fill_secondary, palette.control_stroke_default, palette.text_fill_primary};
    theme.button_pressed = ButtonVisual{palette.control_fill_tertiary, palette.control_stroke_default, palette.text_fill_secondary};
    theme.button_disabled = ButtonVisual{palette.control_fill_disabled, palette.control_stroke_default, palette.text_fill_disabled};

    // Accent button: pressed/disabled have no border. Hover/pressed reuse the fill at reduced
    // opacity instead of another ramp rung: pressed means recede, not darken.
    const Color accent         = palette.accent_fill_default;
    theme.accent_button_normal = ButtonVisual{accent, accent, palette.text_on_accent_primary};
    // Fluent has no informational fill color: that severity's disc is the plain accent.
    theme.severity.informational.fill = states.informational.value_or(accent);
    theme.accent_button_hover         = ButtonVisual{states.hover.value_or(accent.with_alpha(0.9f)),
                                                     accent,
                                                     palette.text_on_accent_primary};
    theme.accent_button_pressed       = ButtonVisual{states.pressed.value_or(accent.with_alpha(0.8f)),
                                                     colors::Transparent,
                                                     palette.text_on_accent_secondary};
    theme.accent_button_disabled = ButtonVisual{palette.accent_fill_disabled, colors::Transparent, palette.text_on_accent_disabled};

    // The neutral and the accent button's raised borders are different gradients, and only the
    // neutral one flips per theme. A negative `border_gradient_extent_px` is that flip.
    const LinearGradientBrush elevation_border{
        {GradientStop{0.33f, palette.control_stroke_secondary}, GradientStop{1.0f, palette.control_stroke_default}}};
    const LinearGradientBrush accent_elevation_border{{GradientStop{0.33f, palette.control_stroke_on_accent_secondary},
                                                       GradientStop{1.0f, palette.control_stroke_on_accent_default}}};
    // Light flips the gradient vertically; Dark leaves it as is.
    const float neutral_extent = palette.dark ? 3.0f : -3.0f;

    theme.button_normal.border_gradient                  = elevation_border;
    theme.button_normal.border_gradient_extent_px        = neutral_extent;
    theme.button_hover.border_gradient                   = elevation_border;
    theme.button_hover.border_gradient_extent_px         = neutral_extent;
    theme.accent_button_normal.border_gradient           = accent_elevation_border;
    theme.accent_button_normal.border_gradient_extent_px = -3.0f; // flipped in both themes
    theme.accent_button_hover.border_gradient            = accent_elevation_border;
    theme.accent_button_hover.border_gradient_extent_px  = -3.0f;

    // Subtle button: borderless, with only a wash on hover/press.
    theme.subtle_button_normal = ButtonVisual{palette.subtle_fill_transparent, colors::Transparent, palette.text_fill_primary};
    theme.subtle_button_hover = ButtonVisual{palette.subtle_fill_secondary, colors::Transparent, palette.text_fill_primary};
    theme.subtle_button_pressed = ButtonVisual{palette.subtle_fill_tertiary, colors::Transparent, palette.text_fill_secondary};
    theme.subtle_button_disabled = ButtonVisual{palette.subtle_fill_transparent, colors::Transparent, palette.text_fill_disabled};

    // SwipeItem's pre-threshold Execute look: the tertiary control fill, the label in the strong stroke color.
    controls.swipe_item_pre_threshold = ButtonVisual{palette.control_fill_tertiary,
                                                     colors::Transparent,
                                                     palette.control_strong_stroke_default};

    // SwipeItem's Reveal and settled Execute look; only the foreground differs from the one above.
    controls.swipe_item_default = ButtonVisual{palette.control_fill_tertiary, colors::Transparent, palette.text_fill_primary};

    // Toggle indicators: "off" is CheckBox's alternate control fill ramp with the strong control
    // stroke border, "on" the accent button's fill. The off `text` is ToggleSwitch's knob fill.
    const ButtonVisual off_normal{palette.control_alt_fill_secondary,
                                  palette.control_strong_stroke_default,
                                  palette.text_fill_secondary};
    const ButtonVisual off_hover{palette.control_alt_fill_tertiary,
                                 palette.control_strong_stroke_default,
                                 palette.text_fill_secondary};
    // Pressed drops to the *disabled* strong stroke, for CheckBox and RadioButton alike.
    const ButtonVisual off_pressed{palette.control_alt_fill_quaternary,
                                   palette.control_strong_stroke_disabled,
                                   palette.text_fill_secondary};
    const ButtonVisual off_disabled{palette.control_alt_fill_disabled,
                                    palette.control_strong_stroke_disabled,
                                    palette.text_fill_disabled};

    controls.set_toggle_visuals(ToggleVisuals{ToggleVisual{off_normal, theme.accent_button_normal},
                                              ToggleVisual{off_hover, theme.accent_button_hover},
                                              ToggleVisual{off_pressed, theme.accent_button_pressed},
                                              ToggleVisual{off_disabled, theme.accent_button_disabled}});

    // ToggleSwitch is the exception: its pressed off track keeps the strong control stroke.
    controls.toggle_track.pressed.off.border = palette.control_strong_stroke_default;

    // The checked radio's dot is the primary text-on-accent color in every state, where the accent
    // button drops its label to the secondary and disabled ones.
    controls.radio_ring.pressed.on.text  = palette.text_on_accent_primary;
    controls.radio_ring.disabled.on.text = palette.text_on_accent_primary;

    // The checked dot's outline is the round raised border. An accent outline shows only during
    // the check animation and is gone once the pointer enters or leaves, so the resting dot shows
    // this one.
    const LinearGradientBrush circle_elevation_border{
        {GradientStop{0.5f, palette.control_stroke_default}, GradientStop{0.7f, palette.control_stroke_secondary}}};
    controls.radio_ring.normal.on.mark_border_gradient   = circle_elevation_border;
    controls.radio_ring.hover.on.mark_border_gradient    = circle_elevation_border;
    controls.radio_ring.pressed.on.mark_border_gradient  = circle_elevation_border;
    controls.radio_ring.disabled.on.mark_border_gradient = circle_elevation_border;

    // An indeterminate box is the checked box; only the glyph swaps to a dash.
    controls.check_box_indeterminate = IndeterminateVisuals::like_checked(controls.check_box_box, IndeterminateMark::Glyph);

    // The text type ramp. Header and Subheader take the two largest rungs, display and large
    // title.
    // Each rung is a size and its line height (12/16, 14/20, 20/28, 28/36, 40/52, 68/92): the design
    // values, set explicitly instead of taking the font's own line gap, which differs between font
    // versions.
    //
    // The family is Segoe UI Variable, in the optical cut made for the size: Small for captions,
    // Text for body sizes, Display from 20 up. Windows 11 installs the three as families of their
    // own. Where they are missing (Windows 10) DirectWrite falls back to the system's Segoe UI.
    constexpr const char* Small      = "Segoe UI Variable Small";
    constexpr const char* Text       = "Segoe UI Variable Text";
    constexpr const char* Display    = "Segoe UI Variable Display";
    auto                  type_style = [](float size, float line_height, FontWeight weight, const char* family)
    { return TextStyle{size, TextAlign::Leading, TextAlign::Center, family, weight, false, line_height}; };
    theme.type.caption     = type_style(12.0f, 16.0f, FontWeight::Regular, Small);    // caption
    theme.type.body        = type_style(14.0f, 20.0f, FontWeight::Regular, Text);     // body
    theme.type.body_strong = type_style(14.0f, 20.0f, FontWeight::SemiBold, Text);    // body strong
    theme.type.subtitle    = type_style(20.0f, 28.0f, FontWeight::SemiBold, Display); // subtitle
    theme.type.title       = type_style(28.0f, 36.0f, FontWeight::SemiBold, Display); // title
    theme.type.subheader   = type_style(40.0f, 52.0f, FontWeight::SemiBold, Display); // large title
    theme.type.header      = type_style(68.0f, 92.0f, FontWeight::SemiBold, Display); // display
    // What a control's own content is set in: 14.
    theme.text_style.font_family = Text;

    detail::fill_derived_colors(theme);
    return theme;
}

// Whether a background color reads as dark, by its relative luminance.
bool is_dark_background(const Color& c)
{
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b < 0.5f;
}

// Copies `wanted` into `slot` when it differs from what the base theme resolved to (`was`).
void take_changed(Color& slot, const Color& wanted, const Color& was)
{
    if (wanted != was)
    {
        slot = wanted;
    }
}

} // namespace

Theme Theme::fluent(const Palette& palette)
{
    return fluent_theme(palette, FluentAccentStates{});
}

Theme Theme::fluent(const ThemeColors& roles)
{
    Palette     palette    = is_dark_background(roles.background) ? Palette::fluent_dark() : Palette::fluent_light();
    const Theme base       = fluent_theme(palette, FluentAccentStates{});
    const ThemeColors& was = base.colors_;

    take_changed(palette.text_fill_primary, roles.text, was.text);
    take_changed(palette.text_fill_secondary, roles.text_secondary, was.text_secondary);
    take_changed(palette.text_fill_disabled, roles.text_disabled, was.text_disabled);
    take_changed(palette.text_on_accent_primary, roles.text_on_accent, was.text_on_accent);
    take_changed(palette.accent_text_primary, roles.link, was.link);
    take_changed(palette.solid_background_base, roles.background, was.background);
    take_changed(palette.card_background_default, roles.card, was.card);
    take_changed(palette.acrylic_fallback_default, roles.overlay, was.overlay);
    take_changed(palette.accent_fill_default, roles.accent, was.accent);
    take_changed(palette.control_fill_default, roles.control_fill, was.control_fill);
    take_changed(palette.control_fill_secondary, roles.control_fill_hover, was.control_fill_hover);
    take_changed(palette.control_fill_tertiary, roles.control_fill_pressed, was.control_fill_pressed);
    take_changed(palette.control_fill_disabled, roles.control_fill_disabled, was.control_fill_disabled);
    take_changed(palette.control_stroke_default, roles.control_stroke, was.control_stroke);
    take_changed(palette.control_strong_stroke_default, roles.control_stroke_strong, was.control_stroke_strong);
    take_changed(palette.divider_stroke_default, roles.divider, was.divider);
    take_changed(palette.focus_stroke_outer, roles.focus, was.focus);
    take_changed(palette.focus_stroke_inner, roles.focus_inner, was.focus_inner);
    take_changed(palette.semantic_fill_critical, roles.critical, was.critical);
    take_changed(palette.semantic_fill_caution, roles.caution, was.caution);
    take_changed(palette.semantic_fill_success, roles.success, was.success);

    FluentAccentStates states;
    if (roles.accent_hover != was.accent_hover)
    {
        states.hover = roles.accent_hover;
    }
    if (roles.accent_pressed != was.accent_pressed)
    {
        states.pressed = roles.accent_pressed;
    }
    if (roles.informational != was.informational)
    {
        states.informational = roles.informational;
    }
    Theme theme = fluent_theme(palette, states);

    // The roles that share a token with another role, or have none, go over the derivation.
    ThemeColors& out = theme.colors_;
    take_changed(out.field, roles.field, was.field);
    take_changed(out.field_text, roles.field_text, was.field_text);
    take_changed(out.selection, roles.selection, was.selection);
    take_changed(out.selection_text, roles.selection_text, was.selection_text);
    take_changed(out.scrim, roles.scrim, was.scrim);
    return theme;
}

Theme Theme::classic(const ThemeColors& roles, bool hot_track, const char* font_family, bool win3x_edge)
{
    ClassicPalette     palette = ClassicPalette::windows_standard();
    const Theme        base    = classic(palette, hot_track, font_family, win3x_edge);
    const ThemeColors& was     = base.colors_;

    take_changed(palette.face, roles.control_fill, was.control_fill);
    take_changed(palette.face, roles.background, was.background);
    take_changed(palette.face_text, roles.text, was.text);
    take_changed(palette.window, roles.field, was.field);
    take_changed(palette.window_text, roles.field_text, was.field_text);
    take_changed(palette.selection, roles.selection, was.selection);
    take_changed(palette.selection_text, roles.selection_text, was.selection_text);
    take_changed(palette.gray_text, roles.text_disabled, was.text_disabled);
    take_changed(palette.hot_light, roles.link, was.link);
    take_changed(palette.menu, roles.overlay, was.overlay);
    return classic(palette, hot_track, font_family, win3x_edge);
}

Theme Theme::with_content_colors(Color text, std::optional<Color> background) const
{
    Theme theme        = *this;
    theme.colors_.text = text;
    if (background)
    {
        theme.colors_.background = *background;
    }
    return theme;
}

Theme Theme::classic(const ClassicPalette& palette, bool hot_track, const char* font_family, bool win3x_edge)
{
    ClassicBevelRamp ramp = palette.bevel.value_or(classic_bevel_ramp_from_face(palette.face));
    if (win3x_edge)
    {
        // Windows 3.x has three button color slots, not five. The etched separators still read
        // `light`/`dark_shadow`, so collapse them onto the two colors the era has.
        ramp.light       = ramp.highlight;
        ramp.dark_shadow = ramp.shadow;
    }
    const Color disabled_text = ramp.shadow;

    Theme                  theme;
    detail::ControlChrome& controls = detail::edit_controls(theme);
    theme.variant                   = win3x_edge ? ThemeVariant::Windows3x : ThemeVariant::Windows9x;
    // The desktop edit control: no clear button, and no spin-button popup.
    controls.spin_button_popup      = false;
    controls.text_box_delete_button = false;
    theme.corner_radius             = 0.0f;
    // No rounded surface anywhere in the classic design language, popups included.
    theme.overlay_corner_radius = 0.0f;
    theme.border_width          = 1.0f;
    detail::edit_colors(theme).background = palette.face; // classic dialogs are BTNFACE-colored, not a separate WINDOW color
    detail::edit_colors(theme).text  = palette.face_text;
    detail::edit_colors(theme).focus = palette.face_text;
    theme.focus_visual               = FocusVisual::ClassicDotted;
    // Hard 1px edges and aliased text, snapped to the device pixel grid so a fractionally placed
    // control still gets crisp bevels.
    theme.rasterization = SetRasterization{GeometryAA::Aliased, TextAA::Aliased, /*snap_to_device_pixels=*/true};

    // The scheme's captions. Unstated, ACTIVECAPTION follows the selection, as it does in Windows
    // 95 and 98's Windows Standard, so a user scheme gets a caption matching its selection. An
    // inactive caption is the shadow gray with face-colored text in 95 and 98, and the face with
    // black text in most of Windows 3.1's shipped schemes (12 and 18 of the 20 CONTROL.SRC rows).
    const ClassicCaption caption = palette.caption.value_or(
        ClassicCaption{.active            = palette.selection,
                       .gradient_active   = palette.selection,
                       .inactive          = win3x_edge ? palette.face : ramp.shadow,
                       .gradient_inactive = win3x_edge ? palette.face : ramp.shadow,
                       .text              = palette.selection_text,
                       .inactive_text     = win3x_edge ? palette.face_text : palette.face,
                       .active_border     = palette.face,
                       .inactive_border   = palette.face});

    // Pre-Vista packs ask DWM for square corners and their own caption color.
    theme.chrome.corner_preference           = CornerPreference::DoNotRound;
    theme.chrome.dark_titlebar               = false;
    theme.chrome.caption_color               = caption.active;
    theme.chrome.caption_text_color          = caption.text;
    theme.chrome.inactive_caption_color      = caption.inactive;
    theme.chrome.inactive_caption_text_color = caption.inactive_text;
    theme.chrome.border_color                = palette.window_frame;
    // A classic page is an opaque face color (`ChromeHints::allows_system_backdrop`).
    theme.chrome.allows_system_backdrop = false;

    // The frame a Frame::Themed window draws.
    detail::ControlChrome::WindowFrame frame;
    frame.active_border         = caption.active_border;
    frame.inactive_border       = caption.inactive_border;
    frame.caption_rule          = 1.0f;
    frame.active_caption        = LinearGradientBrush{{GradientStop{0.0f, caption.active},
                                                       GradientStop{1.0f, caption.gradient_active}},
                                                      GradientAxis::Horizontal};
    frame.inactive_caption      = LinearGradientBrush{{GradientStop{0.0f, caption.inactive},
                                                       GradientStop{1.0f, caption.gradient_inactive}},
                                                      GradientAxis::Horizontal};
    frame.active_text           = caption.text;
    frame.inactive_text         = caption.inactive_text;
    frame.font_family           = font_family;
    frame.font_weight           = FontWeight::Bold;
    frame.frame_line            = palette.window_frame;
    frame.button_face           = palette.face;
    frame.bevel_light           = ramp.light;
    frame.bevel_highlight       = ramp.highlight;
    frame.bevel_shadow          = ramp.shadow;
    frame.bevel_dark_shadow     = ramp.dark_shadow;
    frame.glyph                 = palette.face_text;
    frame.glyph_disabled        = ramp.highlight;
    frame.glyph_disabled_shadow = ramp.shadow;
    if (win3x_edge)
    {
        // Two frame lines with the border color between, as thick as Control Panel's default
        // BorderWidth of 3. The caption is as tall as the VGA driver's 19 x 18 caption buttons,
        // whose art carries the frame line on its left; the Control-menu box is 18 square.
        frame.kind           = detail::ControlChrome::WindowFrame::Kind::Win3x;
        frame.border         = 5.0f;
        frame.caption_height = 18.0f;
        frame.font_size      = 12.0f; // System 10 pt bold, as resolve_windows3x() sets the chrome text
        frame.title_centered = true;
        frame.menu_box_width = 18.0f;
        frame.button         = Size{19.0f, 18.0f};
    }
    else
    {
        // DrawCaption's caption over a sizing frame, the WindowMetrics defaults at 96 DPI
        // (BorderWidth 1, so a 4 px frame).
        frame.kind           = detail::ControlChrome::WindowFrame::Kind::Classic;
        frame.border         = 4.0f;
        frame.caption_height = caption.button_cell;
        frame.font_size      = 11.0f; // 8 pt
        frame.icon_size      = 16.0f;
        frame.icon_left      = 1.0f;
        frame.title_left     = 3.0f;
        frame.button         = Size{caption.button_cell - 2.0f, caption.button_cell - 4.0f};
        frame.button_top     = 2.0f;
        frame.button_right   = 2.0f;
        frame.close_gap      = 2.0f;
    }
    controls.window_frame = frame;

    // COLOR_WINDOW is white regardless of the face color.
    detail::edit_colors(theme).field          = palette.window;
    detail::edit_colors(theme).field_text     = palette.window_text;
    theme.surfaces.field_border               = palette.window_frame;
    detail::edit_colors(theme).selection      = palette.selection;
    detail::edit_colors(theme).selection_text = palette.selection_text;
    detail::edit_colors(theme).text_disabled  = palette.gray_text;
    theme.surfaces.menu                       = palette.menu;
    theme.surfaces.menu_text                  = palette.menu_text;
    theme.surfaces.menu_highlight             = palette.selection;
    theme.surfaces.menu_highlight_text        = palette.selection_text;
    theme.surfaces.tooltip                    = palette.info_bk;
    theme.surfaces.tooltip_text               = palette.info_text;
    detail::edit_colors(theme).link           = palette.hot_light;
    theme.surfaces.separator_light            = ramp.light;
    theme.surfaces.separator_dark             = ramp.shadow;
    // `SplitterControl::render()` reads this without consulting `divider_visual`, so it must be set. The
    // era has no flat rule color; the etched groove's dark half stands in.
    detail::edit_colors(theme).divider = ramp.shadow;
    theme.surfaces.scrollbar_track     = palette.face;
    theme.surfaces.scrollbar_thumb     = palette.face;
    theme.surfaces.scrollbar_arrow     = palette.window_frame;
    {
        // The era's own bar in a gutter. Windows 95 to 2000: SM_CXVSCROLL 16, DrawFrameControl's
        // raised arrow buttons and scroll box, and the shaft USER32 hands out for
        // CTLCOLOR_SCROLLBAR: a 50 % dither of 3DHILIGHT and 3DFACE while 3DHILIGHT equals WINDOW
        // (so the shaft never matches the page), else flat COLOR_SCROLLBAR, which no classic
        // palette here sets apart from the face. Windows 3.x: VGA.DRV's 17 px arrows and a fixed
        // scroll box as long as the bar is thick, on the flat shaft.
        detail::ControlChrome::GutterScrollBar bar;
        bar.style     = win3x_edge ? detail::ControlChrome::GutterScrollBar::Style::Win3x
                                   : detail::ControlChrome::GutterScrollBar::Style::Classic;
        bar.thickness = win3x_edge ? 17.0f : 16.0f;
        bar.min_thumb = 8.0f;
        if (win3x_edge)
        {
            bar.fixed_thumb_vertical   = 17.0f;
            bar.fixed_thumb_horizontal = 17.0f;
        }
        bar.shaft                  = palette.face;
        bar.shaft_alt              = ramp.highlight;
        bar.dither                 = !win3x_edge && ramp.highlight == palette.window;
        bar.arrow                  = palette.face_text;
        bar.corner                 = palette.face;
        controls.gutter_scroll_bar = std::move(bar);
    }
    {
        // BS_GROUPBOX. USER32 raises the frame's top one pixel above the label's middle and draws
        // the label 7 px in over a 1 px gap either side. Windows 95 etches the frame (EDGE_ETCHED:
        // 3DSHADOW with 3DHILIGHT one pixel down and right); Windows 3.x strokes one WINDOWFRAME
        // line. Insets from the guidelines' dialog units at 8 pt: 9x's 11 DU down and 7 DU up
        // from the bottom, controls aligned under the label; 3.x's PROGMAN 7 DU across and 10 DU
        // down.
        detail::ControlChrome::EraGroupBox box;
        box.header      = palette.face_text;
        box.frame_raise = 1.0f;
        if (win3x_edge)
        {
            box.frame   = palette.window_frame;
            box.padding = Edges{.left = 10.0f, .top = 16.0f, .right = 10.0f, .bottom = 10.0f};
        }
        else
        {
            box.frame       = ramp.shadow;
            box.frame_light = ramp.highlight;
            box.padding     = Edges{.left = 7.0f, .top = 18.0f, .right = 7.0f, .bottom = 11.0f};
        }
        controls.era_group_box = box;
    }

    // Solid arrows where Fluent draws chevrons, and the tree view's plus/minus box.
    theme.disclosure_visual = ThemeDisclosureVisual::ClassicArrow;

    // A focused classic field shows only its blinking caret and never changes its edge.
    controls.field_focus_border_thickness = Thickness{};

    // The COMBOBOX class: a WINDOW field with a raised button as wide as a scroll bar arrow
    // (SM_CXVSCROLL, 16) and a LISTBOX drop-down in a 1px WINDOWFRAME border.
    {
        detail::ControlChrome::DropdownChrome combo;
        combo.arrow_column        = 16.0f;
        combo.arrow_size          = Size{7.0f, 4.0f};
        combo.arrow               = palette.face_text;
        combo.arrow_disabled      = disabled_text;
        combo.text_inset          = 4.0f;
        combo.list_background     = palette.window;
        combo.list_border         = palette.window_frame;
        combo.list_highlight      = palette.selection;
        combo.list_highlight_text = palette.selection_text;
        combo.list_item_padding   = Symmetric{.horizontal = 2.0f, .vertical = 1.0f};
        combo.field_with_button   = true;
        combo.arrow_margin        = Thickness{};
        controls.dropdown_chrome  = combo;
    }

    // Real 95/98/2000 buttons are far tighter than Fluent's: OK/Cancel is about 75x23 at 8pt
    // MS Sans Serif.
    theme.padding                   = Symmetric{.horizontal = 10.0f, .vertical = 3.0f};
    controls.toggle_box_size        = 13.0f; // the real classic checkbox/radio indicator size
    controls.toggle_content_spacing = 4.0f;
    theme.text_style.font_family    = font_family;
    theme.text_style.font_size      = 11.0f; // MS Sans Serif 8pt reads about this size on screen
    // No hover/press dot size change: keep a fixed 0.44 ring-to-dot ratio in every state.
    controls.radio_dot.rest = controls.radio_dot.hover = controls.radio_dot.pressed = controls.toggle_box_size * 0.44f;
    // A real TRACKBAR_CLASS has no grow-shrink thumb, so all four sizes are the same. `outer` is
    // the extent across the track, the per-state size the extent along it.
    controls.slider_thumb.shape     = SliderThumbShape::ClassicPointer;
    controls.slider_track_thickness = 4.0f;
    controls.slider_thumb.outer     = 21.0f;                                                          // uThumbLen
    controls.slider_thumb.rest = controls.slider_thumb.hover = controls.slider_thumb.pressed = 11.0f; // (uThumbLen / 2) | 1
    // The groove is one uniform channel. render_classic() never draws a value fill, but the
    // theme data should say so too.
    controls.slider_value_fill = false;
    // PROGRESS_CLASS: the fill is COLOR_HIGHLIGHT by definition, on a COLOR_BTNFACE well with a
    // sunken edge. ProgressRing would otherwise draw in the face color and vanish.
    controls.progress_track = palette.face;
    controls.progress_fill  = palette.selection;
    if (win3x_edge)
    {
        // The era has no progress control. Its gauges are a 1 px frame around one solid block,
        // the frame every 3.x field has.
        controls.framed_progress_bar = detail::ControlChrome::FramedProgressBar{.top_left     = palette.window_frame,
                                                                                .bottom_right = palette.window_frame,
                                                                                .padding      = 0.0f,
                                                                                .chunked      = false};
    }
    else
    {
        // The static edge (BDR_SUNKENOUTER), a pixel of face inside it, then the chunks.
        controls.framed_progress_bar = detail::ControlChrome::FramedProgressBar{.top_left     = ramp.shadow,
                                                                                .bottom_right = ramp.highlight};
    }
    // The era has no semantic color slots, so a severity bar is COLOR_WINDOW inside a 1px
    // WINDOWFRAME rule; the hues keep the readable fallback.
    for (SeverityVisual* sv :
         {&theme.severity.informational, &theme.severity.success, &theme.severity.caution, &theme.severity.critical})
    {
        sv->background = palette.window;
    }
    theme.severity.stroke = palette.window_frame;

    // This era has no animation at all.
    theme.anim_faster = theme.anim_fast = std::chrono::milliseconds{0};
    // Menus and drop-down lists too. Luna and Aero put USER32's own effects back (resolve_theme()).
    theme.popup_motion.menu  = ThemePopupAnimation::None;
    theme.popup_motion.list  = ThemePopupAnimation::None;
    theme.popup_motion.open  = std::chrono::milliseconds{0};
    theme.popup_motion.close = std::chrono::milliseconds{0};
    // No popup shadow either: CS_DROPSHADOW is a Windows XP API. Luna and Aero set their own.
    theme.popup_shadow = ThemePopupShadow::None;
    // A pressed classic button moves its label one pixel right and down.
    controls.pressed_content_offset = Point{1.0f, 1.0f};

    // Command buttons use BF_SOFT and have no hover state: hot-tracking is a toolbar and menu bar
    // affordance. `hot_track` selects the flat-until-hover toolbar button below.
    theme.button_normal = classic_bevel_visual(palette.face, palette.face_text, BorderStyle::Raised, ramp, /*soft=*/true);
    theme.button_hover = theme.button_normal;
    theme.button_pressed = classic_bevel_visual(palette.face, palette.face_text, BorderStyle::Sunken, ramp, /*soft=*/true);
    theme.button_disabled = classic_bevel_visual(palette.face, disabled_text, BorderStyle::Raised, ramp, /*soft=*/true);
    if (win3x_edge)
    {
        // As the era's OBM_* bitmaps draw it: 1px WINDOWFRAME frame, 1px highlight top/left,
        // 2px shadow bottom/right, pressed drops the bottom/right edge. `bevel_frame` routes this.
        for (ButtonVisual* v : {&theme.button_normal, &theme.button_hover, &theme.button_pressed, &theme.button_disabled})
        {
            v->bevel_frame = palette.window_frame;
        }
        // OBM_BTNCORNERS cuts one pixel off each corner: the era's one rounded surface, and the most
        // visible difference between a Windows 3.1 push button and a Windows 95 one.
        theme.button_corner_radius = 1.0f;
        // OBM_CHECKBOXES' checked cell is a boxed X. The tick is the menu mark; Windows 95 merged them.
        controls.check_glyph = glyphs::Cancel;
        // A plain one-pixel WINDOWFRAME frame with the label at the upper left. The etched groove
        // is a Windows 95 convention.
        theme.divider_visual               = ThemeDividerVisual::FlatStroke;
        detail::edit_colors(theme).divider = palette.window_frame;
        theme.surfaces.separator_light     = palette.window_frame;
        theme.surfaces.separator_dark      = palette.window_frame;
        // A Windows 3.x disabled label is plain GRAYTEXT. The engraved two-pass look belongs to
        // 3.1's color chrome glyphs, and there is no DrawState in this era.
        theme.button_disabled.text = palette.gray_text;
    }
    else
    {
        // DrawState(DSS_DISABLED): a highlight pass offset by one pixel, then the shadow color on
        // top at the origin.
        theme.button_disabled.text_effect = TextEffect{TextEffectKind::Emboss, ramp.highlight, ramp.shadow, 1.0f, 1.0f};
        // The same call draws a disabled static, so every disabled label engraves. The indicator
        // glyphs stay flat BTNSHADOW.
        theme.disabled_text_effect = theme.button_disabled.text_effect;
    }

    // Classic has no accent or subtle rest look; the toolbar look is filled in below when
    // hot_track is set. Windows 95 predates it and reuses the normal bevel.
    theme.accent_button_normal   = theme.button_normal;
    theme.accent_button_hover    = theme.button_hover;
    theme.accent_button_pressed  = theme.button_pressed;
    theme.accent_button_disabled = theme.button_disabled;

    if (hot_track)
    {
        // Office 97/IE4's flat-until-hover toolbar button: no bevel at rest, raised on hover/press
        // (ButtonVariant::Subtle).
        ButtonVisual subtle_rest = theme.button_normal;
        subtle_rest.border_style = BorderStyle::Flat;
        subtle_rest.bevel_soft   = false;
        subtle_rest.bevel_light = subtle_rest.bevel_highlight = subtle_rest.bevel_shadow = subtle_rest.bevel_dark_shadow = colors::Transparent;

        // A hot toolbar button has the raised inner border only, no outer edge, and pressed mirrors
        // it. One ring with the inner pair's colors.
        auto toolbar_edge = [&](BorderStyle style)
        {
            ButtonVisual v      = classic_bevel_visual(palette.face, palette.face_text, style, ramp);
            v.bevel_light       = ramp.highlight;
            v.bevel_dark_shadow = ramp.shadow;
            v.bevel_rings       = 1;
            return v;
        };
        theme.subtle_button_normal        = subtle_rest;
        theme.subtle_button_hover         = toolbar_edge(BorderStyle::Raised);
        theme.subtle_button_pressed       = toolbar_edge(BorderStyle::Sunken);
        theme.subtle_button_disabled      = subtle_rest;
        theme.subtle_button_disabled.text = disabled_text;
    }
    else
    {
        // 95 predates the Popup toolbar idiom; Subtle just reuses the normal bevel.
        theme.subtle_button_normal   = theme.button_normal;
        theme.subtle_button_hover    = theme.button_hover;
        theme.subtle_button_pressed  = theme.button_pressed;
        theme.subtle_button_disabled = theme.button_disabled;
    }

    // The classic check box and radio are a sunken COLOR_WINDOW well with the mark drawn in `text`.
    // Windows 3.x flattens them below; the well is a 95 invention.
    ButtonVisual off          = classic_bevel_visual(palette.window, palette.face_text, BorderStyle::Sunken, ramp);
    ButtonVisual on           = classic_bevel_visual(palette.window, palette.face_text, BorderStyle::Sunken, ramp);
    ButtonVisual off_disabled = classic_bevel_visual(palette.face, disabled_text, BorderStyle::Sunken, ramp);
    ButtonVisual on_disabled  = off_disabled;
    controls.set_toggle_visuals(ToggleVisuals{ToggleVisual{off, on},
                                              ToggleVisual{off, on},
                                              ToggleVisual{off, on},
                                              ToggleVisual{off_disabled, on_disabled}});
    // DFCS_PUSHED: while the button is held, the well turns BTNFACE and the mark stays WINDOWTEXT.
    {
        const ButtonVisual held = classic_bevel_visual(palette.face, palette.face_text, BorderStyle::Sunken, ramp);
        controls.check_box_box.pressed = controls.radio_ring.pressed = ToggleVisual{held, held};
    }

    // BST_INDETERMINATE: the sunken well filled with a face/highlight dither and a gray tick.
    // Disabled draws the mark in BTNSHADOW.
    {
        const ButtonVisual mixed_well = classic_bevel_visual(palette.face, ramp.shadow, BorderStyle::Sunken, ramp);
        const IndeterminateVisual mixed{mixed_well, std::nullopt, ramp.highlight};
        controls.check_box_indeterminate = IndeterminateVisuals{mixed,
                                                                mixed,
                                                                IndeterminateVisual{mixed_well},
                                                                IndeterminateVisual{classic_bevel_visual(palette.face,
                                                                                                         ramp.shadow,
                                                                                                         BorderStyle::Sunken,
                                                                                                         ramp)},
                                                                IndeterminateMark::Glyph,
                                                                controls.check_glyph};
    }
    if (win3x_edge)
    {
        // Windows 3.x check boxes and radios are not bevelled: OBM_CHECKBOXES is flat 1bpp outline
        // art in the text colors, and the mark (X or disc) is the visual's `text`.
        auto flat_well = [&](const Color& outline, const Color& mark)
        {
            ButtonVisual v{palette.window, outline, mark};
            v.border_style = BorderStyle::Flat;
            return v;
        };
        const ToggleVisual flat{flat_well(palette.window_frame, palette.face_text),
                                flat_well(palette.window_frame, palette.face_text)};
        const ToggleVisual flat_disabled{flat_well(palette.gray_text, palette.gray_text),
                                         flat_well(palette.gray_text, palette.gray_text)};
        controls.check_box_box = controls.radio_ring = ToggleVisuals{flat, flat, flat, flat_disabled};

        // A mixed check box is "filled with a gray pattern" with no X: a 50 %
        // checkerboard of ink and background, gray when disabled.
        auto grayed = [&](const Color& outline, const Color& ink)
        { return IndeterminateVisual{flat_well(outline, ink), std::nullopt, ink}; };
        const IndeterminateVisual grayed_rest = grayed(palette.window_frame, palette.face_text);
        controls.check_box_indeterminate      = IndeterminateVisuals{grayed_rest,
                                                                     grayed_rest,
                                                                     grayed_rest,
                                                                     grayed(palette.gray_text, palette.gray_text),
                                                                     IndeterminateMark::None,
                                                                     controls.check_glyph};

        // ToggleSwitch has no era equivalent; it is a latched button well drawn with this era's
        // edge (`bevel_frame`). The knob is the outline color off and the mark color on.
        auto switch_well = [&](const Color& outline, const Color& mark)
        {
            ButtonVisual v = classic_bevel_visual(palette.window, mark, BorderStyle::Flat, ramp);
            v.border       = outline;
            v.bevel_frame  = palette.window_frame;
            return v;
        };
        const ToggleVisual track{switch_well(palette.window_frame, palette.face_text),
                                 switch_well(palette.window_frame, palette.face_text)};
        const ToggleVisual track_disabled{switch_well(palette.gray_text, palette.gray_text),
                                          switch_well(palette.gray_text, palette.gray_text)};
        controls.toggle_track = ToggleVisuals{track, track, track, track_disabled};
    }
    // Stated, not inferred from `border_style` (see `ToggleSwitchShape`).
    controls.toggle_switch_shape = ToggleSwitchShape::ClassicWell;

    // Every role uses this era's own font, not Segoe UI.
    theme.type
        .header = theme.type
                      .subheader = theme.type.title = theme.type.subtitle = theme.type.body = theme.type.caption = theme.text_style;

    detail::fill_derived_colors(theme);

    // CalendarView in Windows 9x as the Date/Time control panel's calendar, the one the system
    // itself showed: a BTNSHADOW band with the weekdays' initials in BTNFACE, the selected day in
    // HIGHLIGHT.
    if (!win3x_edge)
    {
        detail::ControlChrome::CalendarChrome calendar;
        calendar.kind            = detail::ControlChrome::CalendarChrome::Kind::Applet;
        calendar.background      = palette.window;
        calendar.title_fill      = ramp.shadow;
        calendar.title_text      = palette.selection_text;
        calendar.weekday_text    = palette.face;
        calendar.item_text       = palette.window_text;
        calendar.trailing_text   = palette.gray_text;
        calendar.selected_fill   = palette.selection;
        calendar.blackout_text   = palette.gray_text;
        calendar.disabled_text   = palette.gray_text;
        controls.calendar_chrome = calendar;
    }
    // Windows 3.x had no calendar: its stand-in is comctl32's month calendar built from the era's
    // parts, the selected cell filled and today framed in COLOR_WINDOWFRAME.
    else
    {
        detail::ControlChrome::CalendarChrome calendar;
        calendar.kind            = detail::ControlChrome::CalendarChrome::Kind::Classic;
        calendar.background      = palette.window;
        calendar.title_fill      = palette.selection;
        calendar.title_text      = palette.selection_text;
        calendar.weekday_text    = palette.window_text;
        calendar.separator       = palette.window_text;
        calendar.item_text       = palette.window_text;
        calendar.trailing_text   = palette.gray_text;
        calendar.selected_fill   = palette.selection;
        calendar.today_mark      = palette.window_frame;
        calendar.footer_text     = palette.window_text;
        calendar.blackout_text   = palette.gray_text;
        calendar.disabled_text   = palette.gray_text;
        calendar.cell_width      = 25.0f;
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
