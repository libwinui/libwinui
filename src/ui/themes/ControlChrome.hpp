// SPDX-License-Identifier: MIT
#pragma once

// The built-in controls' own chrome: how each era draws a check box, radio button, toggle switch,
// slider, text field, drop-down, list row, navigation pane, tooltip, progress bar and swipe item,
// and the per-control gates that pick a construction. Theme carries the design tokens an app and a
// custom control use; this is everything only libwinui's own controls read, kept out of the public
// header so a new control's era data costs no public API and recompiles only the files that read
// it.
//
// A Theme holds one of these behind a shared pointer: copies share it, controls() reads it and
// edit_controls() copies it first when another theme still shares it (copy-on-write). A Theme with
// none (`Theme{}`) reads as a default-constructed ControlChrome. Packs and resolvers write it in
// the same function that sets the era's tokens (Theme::fluent(), resolve_luna(), ...).

#include "../../render/ControlGlyphs.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <cstdint>

namespace ui
{

// A togglable indicator's appearance for one interaction state, split into the unchecked ("off")
// and checked ("on") half. Reuses ButtonVisual's shape rather than a new struct, since a checked
// indicator plays the same visual role as a filled Button. How a control reads the three colors
// is the same in all three toggles: `background` fills the indicator (CheckBox's box,
// RadioButton's ring, ToggleSwitch's track), `border` strokes it, and `text` is the mark drawn on
// top of it (the tick, the dot, the knob).
struct ToggleVisual
{
    // The unchecked half.
    ButtonVisual off;
    // The checked half.
    ButtonVisual on;

    friend bool operator==(const ToggleVisual&, const ToggleVisual&) = default;
};

// One toggle control's four interaction states. `ControlChrome` carries one of these per control
// (`check_box_box`, `radio_ring`, `toggle_track`) rather than one set shared by all three.
//
// It was shared, and five separate passes worked around that instead of splitting it, because the
// split moves `Fluent2`'s goldens. What forced it in the end was that the three controls genuinely
// differ in every era that was looked at closely: `Fluent2`'s ToggleSwitch keeps its unchecked
// stroke at the strong control stroke when pressed where the other two drop to its disabled rung;
// Luna's checked radio dot is a different green from its check box's tick; Windows 8.1's checked box
// and ring keep their near-white fill and only the ToggleSwitch curtain turns violet; and in
// Windows 3.x the check box is a flat 1bpp outline while the ToggleSwitch approximation needs a
// real edge to draw. The worst cost of sharing was not an inexpressible token but one control's
// correct fix silently changing another's appearance — see `ToggleSwitchShape`.
//
// A resolver with no per-control difference states one set and hands it to
// `ControlChrome::set_toggle_visuals()`.
struct ToggleVisuals
{
    // The four interaction states, each with its unchecked and checked half.
    ToggleVisual normal, hover, pressed, disabled;

    // The `ButtonState` selection every other visual set uses, then the checked or unchecked half.
    [[nodiscard]] const ButtonVisual& for_state(ButtonState s, bool checked) const
    {
        const ToggleVisual* tv = &normal;
        switch (s)
        {
            case ButtonState::Hover:
                tv = &hover;
                break;
            case ButtonState::Pressed:
                tv = &pressed;
                break;
            case ButtonState::Disabled:
                tv = &disabled;
                break;
            case ButtonState::Normal:
                break;
        }
        return checked ? tv->on : tv->off;
    }

    // All eight halves, for a resolver adjusting a field across every state at once.
    [[nodiscard]] std::array<ButtonVisual*, 8> all()
    {
        return {&normal.off, &normal.on, &hover.off, &hover.on, &pressed.off, &pressed.on, &disabled.off, &disabled.on};
    }

    friend bool operator==(const ToggleVisuals&, const ToggleVisuals&) = default;
};

// What a CheckBox draws on top of its box for the third, indeterminate value. The eras disagree
// about the *kind* of mark, not just its color, which is why this is a token and not a glyph:
enum class IndeterminateMark
{
    // `IndeterminateVisuals::glyph`, drawn exactly where and how the tick is: Windows 10 and 11's
    // dash glyph, and Windows 9x, whose mixed mark is the tick itself in `COLOR_BTNSHADOW`.
    Glyph,
    // A filled square of `IndeterminateVisuals::square_size`, centered in the box: Windows 8's 9x9
    // square, Luna's 7x7 green square, Aero's layered blue one.
    Square,
    // Nothing: the box's own dither is the whole mark — Windows 1.x-3.x, whose grayed state is a
    // separate cell of `OBM_CHECKBOXES` holding a 50 % checkerboard and no X.
    None,
};

// One interaction state of the indeterminate check box.
struct IndeterminateVisual
{
    // The box, read exactly as `check_box_box`'s halves are (`draw_control_chrome`), with the mark
    // in `text`: the glyph's color for `Glyph`, the square's flat fill for `Square`.
    ButtonVisual box;
    // `Square` only, and only when one flat color cannot say it: the square's own fill, gradient,
    // stroke and inner ring, drawn with `draw_control_chrome` at radius 0 (Luna's diagonal
    // gradient, Aero's gradient square with its darker inner border and half-white highlight).
    std::optional<ButtonVisual> mark{};
    // A 50 % checkerboard of this color over `box.background`, filling the box inside its edge —
    // the "gray pattern" the classic eras mark a mixed value with. A dither, never an alpha blend:
    // at 1:1 the two are visibly different.
    std::optional<Color> dither{};

    friend bool operator==(const IndeterminateVisual&, const IndeterminateVisual&) = default;
};

// CheckBox's third value, in the four interaction states (`CheckBoxControl::set_checked(std::nullopt)`).
// A separate set rather than a third half on `ToggleVisual`, because only the check box has the
// value (no era draws an indeterminate RadioButton differently, and ToggleSwitch has no such
// value), and because what differs is the mark's construction as much as its
// colors.
struct IndeterminateVisuals
{
    // The four interaction states.
    IndeterminateVisual normal, hover, pressed, disabled;
    // Which kind of mark is drawn on the box.
    IndeterminateMark kind = IndeterminateMark::Glyph;
    // The mark for `IndeterminateMark::Glyph`, drawn where the tick would be.
    IconGlyph glyph = glyphs::CheckBoxIndeterminate;
    // The side of the mark for `IndeterminateMark::Square`, in DIPs.
    float square_size = 0.0f;

    // The visual for an interaction state.
    [[nodiscard]] const IndeterminateVisual& for_state(ButtonState s) const
    {
        switch (s)
        {
            case ButtonState::Hover:
                return hover;
            case ButtonState::Pressed:
                return pressed;
            case ButtonState::Disabled:
                return disabled;
            case ButtonState::Normal:
                break;
        }
        return normal;
    }

    // All four states, for a resolver adjusting a field across every state at once.
    [[nodiscard]] std::array<IndeterminateVisual*, 4> all()
    {
        return {&normal, &hover, &pressed, &disabled};
    }

    // The box's four *checked* halves, marked differently: what an era states whose indeterminate
    // box repeats its checked one state for state (Windows 11's indeterminate box fills are the
    // checked ones exactly).
    [[nodiscard]] static IndeterminateVisuals like_checked(const ToggleVisuals& box,
                                                           IndeterminateMark    kind,
                                                           IconGlyph            glyph = glyphs::CheckBoxIndeterminate,
                                                           float                square_size = 0.0f)
    {
        return IndeterminateVisuals{IndeterminateVisual{box.normal.on},
                                    IndeterminateVisual{box.hover.on},
                                    IndeterminateVisual{box.pressed.on},
                                    IndeterminateVisual{box.disabled.on},
                                    kind,
                                    glyph,
                                    square_size};
    }

    friend bool operator==(const IndeterminateVisuals&, const IndeterminateVisuals&) = default;
};

// How a ToggleSwitch draws its track and knob. Two genuinely different constructions, the same
// way `SliderThumbShape` is — and, like that one, a token the theme states rather than a thing a
// control infers.
//
// It used to be inferred: `ToggleSwitch.cpp` read `ToggleVisual::border_style` and treated
// Raised/Sunken as "this is a retro era". That worked only for as long as every retro era's
// toggle *indicator* happened to be bevelled, and Windows 3.x broke it — its check box is a flat
// 1bpp outline with no bevel at all, so flattening the shared `ToggleVisual` correctly for
// CheckBox silently dropped ToggleSwitch into the Fluent branch and drew a rounded pill with an
// animated knob in a design language that has neither. `ToggleVisuals` is now split per control,
// but the shape stays a separate token: it is geometry, not color, and the era should say which
// one it wants rather than a control guess it from a field about its edge.
enum class ToggleSwitchShape
{
    // Fluent/Metro/MDL2/high contrast: a capsule track (`toggle_switch_track_radius_ratio`) with a
    // round knob that slides, grows on hover and stretches on press.
    FluentPill,
    // The classic packs. Windows never had a ToggleSwitch before Fluent (there is no authentic
    // 95-era equivalent), so there is no control to reproduce; this approximates one in the era's
    // own vocabulary for a two-state control — a square latched-toolbar-button well, raised at
    // rest and sunken when on, with no knob motion at all.
    ClassicWell,
    // Aero. Vista and 7 had no switch either, so this is the same kind of approximation
    // `ClassicWell` is, built from the era's own two most recognizable surfaces: the track is a
    // text field (`toggle_track`, stroked with its `border_gradient`) and the knob a miniature
    // push button (`button_*`, gloss, white ring and all) of `toggle_button_knob_size`, sliding
    // end to end without growing or stretching. The era had no switch of its own and showed a
    // check box instead.
    ButtonKnob,
};

// How a Slider draws the part you grab. Two genuinely different constructions, not a radius
// difference — which is why this is an enum and not a number.
enum class SliderThumbShape
{
    // Fluent (both eras): two concentric circles — a fixed opaque outer disc that never
    // animates (`ControlChrome::slider_thumb.outer`) plus a smaller accent-filled inner circle that
    // grows on hover and shrinks on press (`rest`/`hover`/`pressed`).
    ConcentricCircles,
    // MDL2: one flat accent-filled rounded rectangle, 8 x 24 at corner radius 4, the only rounded
    // corner in the whole of the 1507 theme resources and present unchanged in every build
    // through 1703. No border, no outer disc, no stroke, and no hover rung at all: the era's thumb changes only on *press*, and then to a neutral gray
    // rather than a lighter accent. `slider_thumb.outer` is read as the extent across the
    // track (24) and the per-state diameter as the extent along it (8).
    RoundedRect,
    // Classic (`TRACKBAR_CLASS`): a *pointer* — a flat-ended block whose far end is drawn to a
    // 45-degree point, riding a thin sunken channel with tick marks beside it. Windows 95 shipped
    // this control in its original comctl32 (the Volume Control's sliders are the canonical
    // sighting), and the un-themed look is what it draws. Read the same way as `RoundedRect`:
    // `slider_thumb.outer` is the extent *across* the track (comctl32's `uThumbLen`, 21) and the
    // per-state diameter the extent *along* it (`(uThumbLen / 2) | 1`, 11, identical in all four
    // states because the era has no hover or press resize). The point is half the latter deep, so
    // it is always a true 45 degrees. The rest of the geometry — where the channel sits, how far
    // it stops short of the ends, how long a tick is — is `ControlChrome::classic_trackbar`.
    ClassicPointer,
    // Aero: a small glossy block (a gradient face, a white inner ring and a state-colored outline,
    // like a push button's but in its own colors) over a thin grooved channel, with no value fill. With ticks on one side only it
    // grows a point toward them. Every color and size is `ControlChrome::slider_block`.
    ButtonBlock,
};

// Which of the two selection *shapes* a row family uses, for the one era where they diverge.
// Under Fluent 2 every family runs the same table and this changes nothing.
enum class ThemeRowSelection
{
    // ListView/TreeView/TableView: a selected row is filled (see `row_selection_fills`).
    Fill,
    // NavigationShell: a selected item's background is transparent and an accent left-edge bar
    // carries selection on its own. In this era a selected or checked item's fill is
    // transparent, with only the hover and pressed rungs washing, so this family must *not* pick
    // up the accent fill the list families take.
    IndicatorOnly,
};

namespace detail
{

// The per-control records of one theme. Plain data, like Theme; see the file comment for how a
// Theme owns it.
struct ControlChrome
{
    // `ProgressBar`'s two surfaces. `track` is the unfilled channel, the strong control fill in
    // Fluent, and the other half of the black-InfoBar bug `Theme::severity` describes. `fill` is the
    // indicator at normal severity; `nullopt` means "the accent surface", i.e.
    // `accent_button_normal.background`, which is what this control read before the field existed
    // and is right for every accent-bearing era. Luna sets it because the era's progress indicator
    // is a *fixed* green, and aliasing it to the accent surface there draws a near-white indicator
    // on a near-white track.
    Color                progress_track = Color::from_argb(0x72000000);
    std::optional<Color> progress_fill{};

    // An era's framed, glossy progress bar in place of the thin line (Aero's). The paused and error states take the severity's color for the
    // indicator, with no wash and no glint.
    struct GlossyProgressBar
    {
        float                     height = 15.0f;
        Color                     indicator;
        float                     radius = 2.0f;
        std::vector<GradientStop> track;  // across the bar
        std::vector<GradientStop> border; // down the bar
        // The glassy top share of the track and of the indicator, a hard stop at `gloss_split`.
        Color track_gloss;
        Color indicator_gloss;
        float gloss_split = 0.5385f;
        // The light inner edges (left, right and bottom) and the highlight fading from the top.
        Color                     inner_edge;
        std::vector<GradientStop> top_highlight;
        // Laid over the indicator's color while it shows normal progress.
        Color indicator_wash;
        // The darker ends of the indicator, each at most `cap_width` wide, and the warm light
        // rising from its lower half.
        std::vector<GradientStop> left_cap;
        std::vector<GradientStop> right_cap;
        float                     cap_width = 15.0f;
        std::vector<GradientStop> light;
        // The glint: a band `glow_width` wide that crosses the indicator at `glow_speed` DIPs a
        // second, then rests for `glow_pause_ms`.
        // An indeterminate bar's band is `sweep`, in the indicator's color, on the bare track.
        std::vector<GradientStop> glow;
        std::vector<GradientStop> sweep;
        float                     glow_width    = 100.0f;
        float                     glow_speed    = 200.0f;
        double                    glow_pause_ms = 1000.0;

        friend bool operator==(const GlossyProgressBar&, const GlossyProgressBar&) = default;
    };
    std::optional<GlossyProgressBar> glossy_progress_bar{};

    // CalendarView in an era of its own; unset keeps Fluent 2's round cells, drawn from the
    // theme's colors. `kind` picks the construction, and the fields below it say which kind reads
    // them.
    //
    // Square, Windows 10 from 2015 to 2021: 296 x 332, square cells with a 2 px border for hover,
    // press and selection and no hover fill, a filled accent square for today with a 2 px inner
    // ring when it is also selected, a gray fill for the neighboring months' days, 20 px type, a
    // 40 px heading row and a 38 px weekday row.
    //
    // Classic, the comctl32 month calendar of Windows 95 to XP (4.70 to 5.82): 7 cells of
    // `cell_width` by 15, a 30 px title band in the caption color with the scroll bar's arrow
    // buttons, the weekday names in the caption color over a 1 px rule, the selected day a filled
    // ellipse in the caption color, today circled by a hand-drawn red loop, and a bold "Today:"
    // footer. No hot tracking. Windows 3.x and 1.x/2.x had no such control and build it from their
    // own parts: `rect_selection` fills the selected cell, and `today_frame` frames today instead
    // of the loop.
    //
    // Aero, the Vista design of Windows Vista to 8: 225 x 160 in a 1 px frame, a plain centered
    // title with small triangles, 31 x 15 cells with the numbers right-aligned, a light blue box
    // for the selected and the hot day, a rounded blue outline for today, and a "Today:" footer.
    //
    // Applet, the Date/Time control panel of Windows 95 and 98: a month drop-down list and a year
    // spin box over a sunken box, whose 18 px `title_fill` band holds the weekdays' initials in
    // `weekday_text`, over six 18 px rows of the month's days only. The selected day is a box of
    // `selected_fill` just around its number, in `title_text`. No today mark, no month or year
    // view.
    struct CalendarChrome
    {
        enum class Kind
        {
            Square,
            Classic,
            Aero,
            Applet,
        };
        Kind  kind = Kind::Square;
        Color background;    // the control's fill
        Color border;        // the frame (Square, Aero); Classic has none
        Color heading;       // the heading and the paging glyphs
        Color heading_hover; // the heading and glyphs under the pointer
        Color heading_pressed;
        Color heading_disabled;
        Color item_text;             // a day's, month's or year's text
        Color disabled_text;         // every cell while the calendar is disabled
        Color hover_border;          // Square: the 2 px border under the pointer; Aero: the hot box
        Color pressed_border;        // Square
        Color selected_border;       // a selected day's border (Square, Aero)
        Color selected_hover_border; // Square
        Color selected_pressed_border;
        Color today_fill;          // Square: today's square
        Color today_text;          // today's number
        Color today_selected_ring; // Square: the inner ring of today when it is selected
        Color today_hover_border;  // Square
        Color today_pressed_border;
        Color blackout_text;     // a blackout day's number
        Color out_of_scope_fill; // Square: a neighboring month's day
        float corner_radius = 0.0f;

        // Classic and Aero.
        Color title_fill;             // Classic: the caption band
        Color title_text;             // the title; Classic: also the selected day's number
        Color weekday_text;           // the weekday names
        Color separator;              // the rule under the weekday names
        Color trailing_text;          // the neighboring months' days
        Color selected_fill;          // Classic: the ellipse; Aero: the box's fill
        Color hover_fill;             // Aero: the hot box's fill
        Color today_mark;             // Classic: the loop or frame; Aero: the outline
        Color footer_text;            // the "Today:" line
        float cell_width     = 25.0f; // Classic: 25 with MS Sans Serif, 24 with Tahoma
        bool  rect_selection = false; // Classic: fill the selected cell, not an ellipse
        bool  today_frame    = false; // Classic: frame today, not the red loop

        friend bool operator==(const CalendarChrome&, const CalendarChrome&) = default;
    };
    std::optional<CalendarChrome> calendar_chrome{};

    // DatePicker and TimePicker in an era of their own; unset keeps Fluent 2's: a field split into
    // its parts that opens a flyout of looping columns, the picked row in an accent band with its
    // text recolored, and Accept and Dismiss buttons under it.
    //
    // Flyout with `plain_band`, Windows 10 from 2015 to 2021: the same flyout with the band a plain
    // accent wash under the items, no recolored text, 22 px chevron buttons on the columns, and a
    // `border` (2 in 2015 to 2017, 1 later) on the field with `divider` lines between its parts.
    //
    // ComboBoxes, Windows 8.1's XAML pickers: one drop-down list per part, 80 px wide at least,
    // 20 px apart.
    //
    // Field, comctl32's date and time picker (Windows 95 to 8, and the 3.x and 1.x/2.x stand-ins):
    // the value written in the locale's short pattern inside the era's field, the part being
    // edited in the selection colors, a drop-down button that opens the era's month calendar for
    // a date, and spin buttons for a time.
    struct DateTimePickerChrome
    {
        enum class Kind
        {
            Flyout,
            ComboBoxes,
            Field,
        };
        Kind  kind       = Kind::Flyout;
        bool  plain_band = false; // Flyout: Windows 10's band
        float border     = 1.0f;  // Flyout: the field's border
        float divider    = 1.0f;  // Flyout: the lines between the field's parts

        friend bool operator==(const DateTimePickerChrome&, const DateTimePickerChrome&) = default;
    };
    std::optional<DateTimePickerChrome> date_time_picker_chrome{};

    // A classic era's progress bar in place of the thin line: a `progress_track` well inside a
    // 1 px frame, `top_left` on the top and left edges and `bottom_right` on the other two, with
    // the indicator in `progress_fill` inset by `padding`. `chunked` draws the indicator as
    // blocks two thirds as wide as the space inside the padding, `chunk_gap` apart, the way
    // PROGRESS_CLASS draws without PBS_SMOOTH; otherwise it is one block. An indeterminate bar
    // sweeps a band of `sweep_chunks` chunks across, or a quarter of the bar when not chunked.
    struct FramedProgressBar
    {
        float height = 16.0f;
        Color top_left;
        Color bottom_right;
        float padding      = 1.0f;
        bool  chunked      = true;
        float chunk_gap    = 2.0f;
        int   sweep_chunks = 5;

        friend bool operator==(const FramedProgressBar&, const FramedProgressBar&) = default;
    };
    std::optional<FramedProgressBar> framed_progress_bar{};

    // ---- ToolTip chrome (ui/dialogs/ToolTip.cpp) ----
    //
    // ToolTip is the one popup rounded at the control radius (`corner_radius`) instead of the
    // overlay radius, so a negative value here (the same sentinel `toggle_box_radius` uses) means
    // "follow `corner_radius`", which is what ToolTip.cpp did unconditionally before this
    // existed. Luna sets 0: its tooltip is square (Aero's, two releases later, is rounded at 2),
    // because a Luna tooltip is the classic comctl32 one. Left on `corner_radius` it came out
    // rounded at the *push button's* 3 px, with the square popup shadow showing past the curve.
    float tooltip_corner_radius = -1.0f;
    // Total inset from the tooltip's edge to its content, its own border included. `nullopt`
    // keeps `9,6,9,8`, the value ToolTip.cpp hard-coded before this.
    // An era whose tooltip is the *system* tooltip needs its own, and it is much tighter.
    std::optional<Thickness> tooltip_padding;

    // AnnotatedScrollBar's step buttons: their fill and border are fully transparent in the light,
    // dark and high-contrast themes, and only the glyph is ever recolored (the primary, secondary,
    // tertiary and disabled text colors for rest, hover, pressed and disabled). That is a
    // genuinely different shape from the bordered, background-filled `ButtonVisual` ramp
    // `for_state()` returns. Only
    // `Theme::fluent()` opts in (mirroring `Theme::divider_visual`); every retro/classic/Metro
    // resolver keeps drawing these buttons through the ordinary bordered `for_state()` look,
    // since a transparent, borderless icon button reads as "missing chrome" against those
    // eras rather than as the real control.
    bool scroll_button_chromeless = false;

    // NumberBox's inline spin buttons as an era's up-down control, two halves stacked flush
    // inside the field's right edge, this wide (Aero's), in place of two side-by-side columns.
    std::optional<float> stacked_spinner_width{};
    // NumberBox's Compact placement as a popup of two spin buttons. An era without one shows
    // its inline buttons for Compact too.
    bool spin_button_popup = true;
    // TextBox's delete button, which clears the field. The desktop eras' edit control has none.
    bool text_box_delete_button = true;

    // An era's own menus (Aero's menu bar, menu items and context menus): a
    // menu bar on a gradient whose items are outlined on hover and pressed in while open, and
    // square popups with an icon gutter, a glossy hot row, a boxed check and a two-line
    // separator. Unset, the Fluent menu.
    struct EraMenu
    {
        // The bar: its fill down the strip, an item's padding, the hover outline (the middle
        // ring, and the light rings outside and inside it), and an open item's pressed-in fill,
        // its stroke and its inner ring.
        std::vector<GradientStop> bar_background;
        Thickness                 bar_item_padding;
        Color                     bar_hover_stroke;
        Color                     bar_hover_light;
        std::vector<GradientStop> bar_open_fill;
        Color                     bar_open_stroke;
        Color                     bar_open_inner;
        // The popup: room inside the border round the rows, a row's padding, and its columns:
        // the icon or check, the gap before the label, the gap before the shortcut and the
        // cascade arrow's.
        Thickness popup_padding;
        Thickness item_padding;
        float     icon_column  = 24.0f;
        float     label_gap    = 4.0f;
        float     shortcut_gap = 37.0f;
        float     arrow_column = 17.0f;
        // The gutter under the icon column, 1 in from the border, and the two lines after it.
        float gutter_width = 28.0f;
        Color gutter;
        Color gutter_dark;
        Color gutter_light;
        // The hot row: a rounded gradient, its stroke and the light ring inside it.
        std::vector<GradientStop> hot_fill;
        Color                     hot_stroke;
        Color                     hot_inner;
        // A checked item's box and mark, and their disabled look.
        Color check_panel;
        Color check_panel_border;
        Color check;
        Color check_panel_disabled;
        Color check_panel_border_disabled;
        Color check_disabled;
        Color disabled_text;
        // The separator: its slot, where its dark line sits in it, and the two lines.
        float separator_height = 12.0f;
        float separator_top    = 6.0f;
        Color separator_dark;
        Color separator_light;

        friend bool operator==(const EraMenu&, const EraMenu&) = default;
    };
    std::optional<EraMenu> era_menu{};

    // An era's permanent scroll bar, where Fluent's is an overlay that narrows and fades. The bar
    // owns a `thickness` gutter its scroller gives up to it while it shows, with an arrow button
    // as long as the bar is thick at each end. It never narrows or fades. `style` picks the era's
    // construction; the fields below it say which style reads them.
    //
    // Aero: a thumb carrying three gripper lines, and arrows that are their glyph alone until the
    // pointer is over the bar, then take the thumb's chrome. The fills and the track run across
    // the bar; the draw sets the axis.
    struct GutterScrollBar
    {
        enum class Style
        {
            Aero,
            Luna,    // Luna's blue scroll bar: `luna`
            Classic, // Windows 95 to 2000: DrawFrameControl's raised buttons on a dithered track
            Win3x,   // Windows 3.x: the 1 : 2 edge, a fixed square scroll box, stemmed arrows
            Win1x2x, // Windows 1.x/2.x: a gray shaft, a white fixed box, framed stemmed arrows
        };
        Style style = Style::Aero;
        // The thumb's shortest length (0: half the thickness), and for an era whose scroll box is
        // not proportional, its fixed length along a vertical and a horizontal bar.
        float min_thumb              = 0.0f;
        float fixed_thumb_vertical   = 0.0f;
        float fixed_thumb_horizontal = 0.0f;

        // One look of a thumb or arrow: its fill, its 1 px outline, and an arrow's glyph.
        struct Part
        {
            std::vector<GradientStop> fill;
            Color                     border;
            Color                     glyph;

            friend bool operator==(const Part&, const Part&) = default;
        };
        // SM_CXVSCROLL / SM_CYHSCROLL, and each arrow button's length (SM_CYVSCROLL).
        float                     thickness = 17.0f;
        std::vector<GradientStop> track;
        Color                     track_disabled;
        // The cell where a vertical and a horizontal bar meet.
        Color corner;
        // The thumb at rest, and an arrow while the pointer is over the bar; then hovered and
        // pressed (a dragged thumb is pressed).
        Part rest;
        Part hover;
        Part pressed;
        // An arrow's glyph while the pointer is elsewhere.
        Color idle_arrow;
        // The ring one pixel inside the outline, and the soft line under each part.
        Color inner_border;
        Color shadow;
        // The gripper lines, a gradient across each line, per thumb state, and the light line
        // round each.
        std::vector<GradientStop> gripper_rest;
        std::vector<GradientStop> gripper_hover;
        std::vector<GradientStop> gripper_pressed;
        Color                     gripper_shadow;

        // Luna's blue scroll bar. Each part is a white outer ring round a
        // gradient, a colored inner ring and a soft gradient shadow line under it; the thumb's
        // fill runs across the bar and an arrow's diagonally. `track_pressed` is the shaft half
        // under a held page click.
        struct LunaParts
        {
            Part                      thumb_rest, thumb_hover, thumb_pressed;
            Part                      arrow_rest, arrow_hover, arrow_pressed;
            Part                      disabled;
            Color                     outer_border;
            std::vector<GradientStop> shadow;          // down the part
            std::vector<GradientStop> shadow_disabled; // ...
            std::vector<GradientStop> track_pressed;   // across the bar
            // The gripper's four 1 px bars and the light copy one pixel down and right, per thumb state.
            std::array<Color, 3> gripper;
            std::array<Color, 3> gripper_shadow;

            friend bool operator==(const LunaParts&, const LunaParts&) = default;
        };
        std::optional<LunaParts> luna{};

        // Classic, Win3x and Win1x2x: the shaft (a 50 % dither of `shaft` and `shaft_alt` when
        // `dither`, else flat `shaft`), drawn inverted under a held page click, and the arrows'
        // ink. Classic and Win3x take the buttons' faces and edges from the theme's button ramp;
        // Win1x2x boxes its arrows and thumb in `frame` around `box`.
        Color shaft;
        Color shaft_alt;
        bool  dither = false;
        Color arrow;
        Color frame;
        Color box;

        friend bool operator==(const GutterScrollBar&, const GutterScrollBar&) = default;

        // The thumb's or an arrow's look in an interaction state; Normal and Disabled are `rest`.
        [[nodiscard]] const Part& part_for_state(ButtonState s) const
        {
            return s == ButtonState::Pressed ? pressed : s == ButtonState::Hover ? hover : rest;
        }
    };
    // Set by the packs that draw their era's own scroll bar (Aero); unset, the overlay bar.
    std::optional<GutterScrollBar> gutter_scroll_bar{};

    // ListView/TreeView/TableView row selection (the three controls agree on the same shape, and
    // NavigationShell's left-pane items are a fourth copy of the same table, the selected-pressed
    // quirk included): a row's own foreground is never recolored and a row is never filled with
    // the strong `colors.selection`/`selection_text` highlight below. Every row state (rest,
    // hover and pressed, each unselected and selected) instead cycles through the same
    // borderless transparent/secondary/tertiary subtle fill ramp `Theme::subtle_button_*` already
    // carries. See `subtle_row_background()` below for the exact table, including a real quirk:
    // a selected row stays at the secondary fill when pressed instead of deepening to tertiary.
    // The foreground stays the primary text color throughout (a TreeView row is the one
    // exception: pressed, it uses the secondary text color), and the only cue a row is selected
    // is a small accent-colored indicator drawn on top (a left-edge pill for ListView/TreeView, a
    // border for TableView, see each control's own render()). Only `Theme::fluent()` opts in
    // (mirroring `scroll_button_chromeless` above); every retro/classic/Metro resolver keeps
    // today's flat `colors.selection`/`selection_text` highlight, genuinely correct for a classic
    // Win32 listbox's selection look, and `palette` isn't populated for those resolvers anyway (only `Palette::fluent_light()`/`fluent_dark()`
    // ever construct one), so reading `tabular_header_background`/`tabular_grid_line`
    // unconditionally would draw transparent chrome there.
    bool list_item_subtle_selection = false;

    // Windows 10 Fluent's selected-row fills, the one part of that era's row model
    // `list_item_subtle_selection` above cannot describe. Both eras agree on an *unselected*
    // row (transparent, then a neutral wash on hover and a deeper one on press, with the
    // foreground never moving), so Fluent 1 keeps that flag set and the shared
    // `subtle_row_background()` table below. They part company on what *selected* means:
    //
    //   * Windows 11 keeps the row neutral, one rung hotter on the same subtle ramp, and marks
    //     selection with a small accent pill drawn on top. The pill belongs to the rounded
    //     Windows 11 row chrome and exists only there.
    //   * Windows 10 **fills the row with accent**, and draws no indicator at all: a selected row
    //     is the accent at 0.75, a hovered selected row the accent at 0.4 in Light and 0.6 in Dark
    //     (one of the six accent rungs whose opacity differs per theme), and a pressed selected
    //     row the accent at 0.9. Hovering a selected row makes it *fainter*, the same
    //     down-then-up shape the era's chrome ladder has.
    //
    // `popup_selected` is the same idea for a drop-down list, and is a genuinely different ladder
    // rather than the same one reused: a selected drop-down item is the accent at the low rung
    // where a selected list row is at the medium-low one. ComboBox popups here track one "hot"
    // item rather than a per-row hover/press state, so this era's hovered and pressed selected
    // drop-down items have nothing to bind to.
    //
    // Unset for every other theme, which keeps today's behavior exactly: Fluent 2 runs the subtle
    // table plus a pill, and the retro/classic packs run the flat `colors.selection` highlight.
    struct AccentSelectionFills
    {
        Color rest;    // a selected row at rest
        Color hover;   // a selected row under the pointer (fainter than rest)
        Color pressed; // a selected row while pressed
        // The drop-down popup's own ladder. A selected item is the accent's low rung where a
        // row's rest is the medium-low one, and it *deepens* on hover (the medium rung) where a
        // row fades: two ladders drawn from the same six rungs, running opposite directions. Only
        // these two rungs are carried: ComboBox popups here track a single "hot" item, so a
        // pressed selected item has nothing to bind to.
        Color popup_selected;
        Color popup_selected_hover;

        friend bool operator==(const AccentSelectionFills&, const AccentSelectionFills&) = default;
    };
    // Accent fills for selected rows; set only by the Fluent 1 resolver.
    std::optional<AccentSelectionFills> row_selection_fills{};

    // An open ComboBox's field fill (Fluent 1 only): the accent's low rung whether the box is
    // focused or not, so an **open** Windows 10 ComboBox washes its own field in accent. That is
    // the era's surprise, and the state Windows 11 replaced
    // with a plain focus border.
    // Unset elsewhere, where an open ComboBox keeps its resting field fill.
    std::optional<Color> combo_open_fill{};

    // Slider's per-state track and thumb fills (Fluent 1 and MDL2). `Slider.cpp` otherwise derives
    // these from `colors.text`/`colors.background`, because Fluent 2's own strong and solid
    // control fills have no `Palette` field here. That is a documented approximation, close
    // enough for Windows 11 and wrong for Windows 10 in a specific, verifiable way: the track
    // walks the base color from 40 % to 60 % rather than sitting at one alpha, and the thumb
    // follows the era's accent triple (the accent, its first lighter shade and its first darker
    // shade).
    //
    // This comment used to say the thumb *"turns a neutral gray when you touch it"*, citing the
    // alt-low and high chrome colors. Those belong to ColorPicker's slider thumb, a different
    // control with a deliberately different ladder, because a color picker's thumb must not be
    // tinted by the accent color it is being used to choose. Corrected 2026-09-14;
    // `resolve_fluent1()` had the wrong pair too.
    //
    // **MDL2 fills this in too, with almost the same values**, which is a finding rather than a
    // convenience. In 10240 the track is the medium-low base color walking to the medium one on
    // hover, the value fill is the plain accent, and the thumb goes neutral (the high chrome
    // color) on press: Fluent 1 inherited this control's color behavior from MDL2 essentially
    // intact. The one difference is a rung MDL2 does not have: its thumb has **no hover state at
    // all**, only pressed, so `thumb_hover` is set equal to `thumb` there rather than to a
    // separate alt-low chrome color.
    struct SliderFills
    {
        // The thumb's fill per interaction state.
        Color thumb, thumb_hover, thumb_pressed, thumb_disabled;
        // The unfilled track's fill per interaction state (no pressed rung).
        Color track, track_hover, track_disabled;

        friend bool operator==(const SliderFills&, const SliderFills&) = default;
    };
    // Slider's per-state fills; unset derives them from `colors.text` / `colors.background`.
    std::optional<SliderFills> slider_fills{};

    // Whether a selected item is marked by a small accent indicator drawn *on top of* it — the
    // Windows 11 pill and its Windows 10 bar ancestor — or solely by the item's own fill/label.
    // Read by `NavigationShellControl::draw_selection_indicator()` and `SelectorBarControl::render()`, the two
    // controls whose indicator had no gate at all before: both drew a Fluent accent pill under
    // every theme, including eras that predate the idea.
    //
    // Only `Theme::mdl2()` turns it off, and the era is unambiguous about it. MDL2 has neither
    // control (its nav pane is a `SidePane` hosting a `ListView`, and its tab strip is `Sections`),
    // and in both of the things it *does* have, selection is marked without any overlay:
    //
    //   * A list row (build 10240) fills with the accent's low rung when selected and draws no
    //     indicator: **a selected row is an accent-filled row**. A drop-down item uses the
    //     identical ladder.
    //   * A `Sections` header stays transparent in every single state and puts the entire cue in
    //     the label's color: the base color at 60 % when unselected, at 100 % when selected.
    //     **No underline, no pill, no fill.**
    //
    // Deliberately *not* the gate for ListView/TreeView/TableView/ComboBox, which already
    // suppress their own pill on `!row_selection_fills` — "this theme names accent selection fills,
    // so it is a fill-era theme" — a gate with its own documented rationale that predates this flag
    // and stays correct. The two mechanisms agree for every theme: MDL2 sets both.
    //
    // It is also *not* simply "is this Fluent": Fluent 1 keeps this true, because Windows 10's
    // NavigationShell genuinely does draw a bar (2 x 24, inset 4 — `nav_indicator` below) even
    // though its ListView rows have no pill. The three eras genuinely differ per control, which is
    // why the pill gate and this one are separate fields rather than one shared "era" switch.
    bool accent_selection_indicator = true;

    // NavigationShell's accent selection indicator — the *only* accent in a selected item in either
    // era, and a different bar in each. Windows 11 draws 3 x 16 at radius 2 flush with the pill's
    // left edge; Windows 10 draws **2 x 24** at the same radius, 4 in from the left: narrower,
    // half again as tall, and inset.
    // Lives here rather than as constants in `NavigationShell.cpp` for the same reason
    // `radio_dot`/`toggle_knob`/`slider_thumb` do: it is per-era geometry, not a control invariant.
    struct
    {
        Size  size{3.0f, 16.0f}; // the bar's width and height, in DIPs
        float radius     = 2.0f; // its corner radius
        float left_inset = 0.0f; // its distance from the item's left edge
    } nav_indicator;

    // The navigation pane's own surface and link colors.
    //
    // Fluent leaves the pane transparent — on Windows 11 the Mica behind the window *is* the pane,
    // and its rows read against the page. Aero does not work that way: the pane is a distinct
    // surface with its own fill and its own text color, and even a disabled link's color is
    // chosen to show over that fill. A pack that leaves the pane on the page color and
    // paints its rows in `colors.text` draws a gray expanse with no rows in it — the opposite of
    // the era's distinct, filled navigation pane.
    //
    // Unset for every other theme, which keeps today's transparent pane and `colors.text` labels
    // byte-for-byte.
    struct NavPaneVisual
    {
        // The pane's fill. A brush rather than a color because the era's is a gradient.
        std::optional<LinearGradientBrush> background{};
        // The link color at rest, and while hovered. `nullopt` means "use `colors.text`", i.e.
        // what `NavigationShellControl::render()` did before this existed.
        std::optional<Color> text{};
        std::optional<Color> text_hot{};

        friend bool operator==(const NavPaneVisual&, const NavPaneVisual&) = default;
    };
    // NavigationShell's pane surface and link colors; all unset outside Aero.
    NavPaneVisual nav_pane{};

    // The mark a checked CheckBox draws inside its box. Every era since Windows 95 uses a
    // tick, which is why this did not exist before — but **Windows 3.x uses an X**, drawn
    // corner to corner, and reserves the tick for the *menu* check mark. The two glyphs are
    // separate bitmaps in the shipped driver (`OBM_CHECKBOXES`' second cell is a boxed X;
    // `OBM_CHECK` is a tick), so the era deliberately marks two different jobs two different
    // ways and Windows 95 is what collapses them into one. The glyph carries its own font so a
    // pack can change the mark without changing `icon_font_family` for every other control;
    // `CheckBox.cpp` still overrides the size from `toggle_box_size`. Already a CheckBox token in
    // all but placement — nothing else reads it (the menu draws no mark and the radio dot is
    // geometry) — so splitting `ToggleVisuals` per control left it where it is.
    IconGlyph check_glyph = glyphs::Accept;
    // Whether a CheckBox draws its tick in when checked and erases it when cleared, as the
    // animated accept icon of the Fluent eras does; at rest it is `check_glyph` either way.
    // Theme::fluent() and the Fluent high-contrast themes set it. The other eras' marks simply
    // appear.
    bool check_mark_draws_in = false;

    // Tabs control's tab strip:
    // a tab pill is not a button and a selected tab is not a highlight. The *selected* tab is
    // filled with `palette.solid_background_tertiary` (the page surface it is meant to be
    // continuous with), rounded only at its top two corners, and stroked in
    // `palette.card_stroke_default` on three sides with no bottom edge, and the remaining strip is
    // ruled with a 1px line of that same stroke that the selected tab breaks through; unselected
    // tabs cycle the `palette.on_backdrop_*` ramp, and adjacent unselected tabs are divided
    // by a vertically-inset 1px hairline in `palette.divider_stroke_default`. Gated like every other flag in this block: only `Theme::fluent()` opts in, and
    // every retro/classic/Metro resolver keeps this control's original look, where the selected
    // tab borrows `colors.field` to read as the content surface below it — which is exactly
    // what a Win95-era tab control does, and is the only shape that works without `palette`
    // being populated at all.
    bool tab_layer_surfaces = false;

    // An era's tab control in place of Tabs control's own strip (Aero's):
    // tabs with a two-band face and a border on three sides, a selected tab that grows over its
    // neighbors and into a framed page, and the content inset in that page.
    struct EraTabs
    {
        std::vector<GradientStop> face;       // down an unselected tab
        std::vector<GradientStop> hover_face; // ... under the pointer
        Color                     border;
        Color                     hover_border;
        Color                     disabled_face;
        Color                     disabled_border;
        // The page, which the selected tab shares, and how far in from its border the content sits.
        Color page;
        float page_padding = 4.0f;
        // How far the selected tab grows past its slot at each side and the top, and how far an
        // unselected one sits below the selected one's top.
        float selected_grow = 2.0f;

        friend bool operator==(const EraTabs&, const EraTabs&) = default;
    };
    std::optional<EraTabs> era_tabs{};

    // An era's group box in place of GroupBox's header over a card (BS_GROUPBOX as each era draws
    // it): a frame with the header breaking its top edge near the left corner, and the content
    // inset inside it.
    struct EraGroupBox
    {
        Color frame; // the line, or an etched groove's dark half
        // An etched groove's light half, one pixel down and to the right of `frame`.
        std::optional<Color> frame_light{};
        float                radius = 0.0f;
        Color                header;
        // Where the header starts from the left edge, and the gap in the frame either side of it.
        float header_x   = 7.0f;
        float header_gap = 1.0f;
        // How far the frame's top edge sits above the header's vertical middle.
        float frame_raise = 0.0f;
        // From the edges to the content; the top is measured from the top of the box.
        Thickness padding{};

        friend bool operator==(const EraGroupBox&, const EraGroupBox&) = default;
    };
    std::optional<EraGroupBox> era_group_box{};

    // Chromeless, text-only state ramps (SelectorBar's and Breadcrumb's inline items): a
    // SelectorBar item and a Breadcrumb item are alike in never filling *at all* (their
    // background is transparent in every state, selected included) and putting the entire
    // hover/press cue in the label's own color, which walks the primary, secondary and tertiary
    // text colors (and the disabled one). That is the same shape `scroll_button_chromeless` already describes for
    // AnnotatedScrollBar's step buttons, kept as a separate flag because it is a separate verified
    // control family; both read the ramp through `text_fill_for_state()` below. Only
    // `Theme::fluent()` opts in — a borderless, never-filling strip reads as "missing chrome"
    // against a bevelled era, and `palette` is not populated for those resolvers anyway (see
    // `list_item_subtle_selection` above), so every other theme keeps a background wash and a
    // single flat text color.
    bool chromeless_text_states = false;

    // Surface-card chrome (as on Expander): a card-shaped
    // control's own header/content backgrounds and border come from the card colors
    // (`palette.card_background_default`/`card_background_secondary`/`card_stroke_default`), not
    // from the `button_normal`
    // control-fill ramp that reads correctly for a *pushable* surface. The two coincide almost
    // exactly in Fluent Light, which is why the substitution went unnoticed until this pass;
    // the recessed `card_background_secondary` inner panel has no control-fill counterpart at
    // all. Gated for the same reason as `list_item_subtle_selection` above: `palette` is only
    // populated by `Palette::fluent_light()`/`fluent_dark()`, so a retro/classic/Metro resolver
    // reading these unconditionally would paint transparent chrome — those keep drawing the
    // uniform `button_normal` card `Expander` has always used, which is also the right look for
    // a bevelled Win95-era group box.
    bool card_surfaces = false;

    // PersonPicture's placeholder-state ellipse: it is filled with
    // `palette.control_alt_fill_quaternary` and stroked with `palette.card_stroke_default`, not
    // the `button_normal` ramp this control borrowed as a stand-in before this pass. Gated the same way as `card_surfaces` immediately above:
    // `palette` is only populated by `Palette::fluent_light()`/`fluent_dark()`, so a retro/
    // classic/Metro resolver reading `palette.control_alt_fill_quaternary`/
    // `card_stroke_default` unconditionally would paint transparent chrome — those keep
    // drawing PersonPicture with the uniform `button_normal` look it has always had, same as
    // every other control this family of flags covers. `palette.control_alt_fill_quaternary` was
    // still an approximated value when this flag landed; the whole `control_alt_fill_*` ramp has
    // since been replaced with the literal shipped values, so this now reads the exactly-sourced
    // color and not just the right token.
    bool person_picture_dedicated_fill = false;

    // Fluent only: a ToggleSwitch's
    // *off*-position knob is filled from the off visual's foreground (the secondary text color,
    // the disabled text color when disabled), not from its track outline. The two are easy to
    // conflate, since the off track's outline *is* the strong control stroke, and drawing the
    // knob in the stroke color makes it visibly dimmer than Windows does (0x8B vs 0xC5 white-alpha in Dark,
    // 0x72 vs 0x9E black-alpha in Light). Every retro/Metro/HC pack leaves this false and keeps
    // reading the off visual's `border`, which is what their hand-built toggle tokens encode
    // (and what the "on" half has always read from `text` regardless).
    bool toggle_knob_off_foreground = false;

    // Metro only: press is a hard, instantaneous polarity flip (white fill/black text in dark
    // theme, the reverse in light), never a "slightly darker" wash — `button_pressed`'s colors
    // already encode the full inversion, so `ButtonControl::sync_composition()` must not *also* fade in
    // the usual hover/press white overlay on top of it. Every other theme keeps this false, so
    // that overlay's existing hover-or-press behavior is unchanged there.
    bool press_state_flip = false;

    // Classic packs only: how far a pressed command button's *label* moves, in device pixels.
    // The caption shifts one pixel right and down as the bevel inverts, the same rule Windows 3.x
    // already had, and it is a large part of why a classic press reads as physical.
    // `DFCS_ADJUSTRECT`'s 2px content inset is the static half of the same geometry. Zero
    // everywhere else — Aero presses a button by shading its inside instead, and a label that
    // does not move is among the things an Aero pack has to get right.
    Point pressed_content_offset{0.0f, 0.0f};

    // CheckBox's indicator box corner radius, when the era's control radius does not imply it.
    // Negative (the default) keeps CheckBox.cpp's own original derivation — a 4px rounded square
    // when `corner_radius > 0`, square otherwise — which every theme but Luna still wants.
    //
    // Luna is the counter-example that forced this: its push button really is rounded (one arc of
    // ~3px) while its check box is **exactly square** — `BLUE_CHECKBOX13_BMP.bmp` carries a flat
    // 1px `#1C5180` border on all four edges, corner pixels included, with no arc and no
    // antialiasing anywhere on it. The era's own rule is that Luna's square-cornered controls are
    // all field surfaces and its rounded ones are all chrome, and one theme-global radius cannot
    // say that.
    float toggle_box_radius = -1.0f;

    // CheckBox/RadioButton indicator side length, and the gap between an indicator (or
    // ToggleSwitch track) and its trailing label text.
    float toggle_box_size        = 20.0f;
    float toggle_content_spacing = 8.0f;
    // RadioButton's ring side length where it differs from the check box's; negative follows
    // `toggle_box_size`. Only Metro sets it: 8.1's ring is 23 px against a 21 px check box.
    float radio_box_size = -1.0f;

    // `radio_box_size` if set, `toggle_box_size` otherwise.
    [[nodiscard]] float resolved_radio_box_size() const
    {
        return radio_box_size >= 0.0f ? radio_box_size : toggle_box_size;
    }
    // ToggleSwitch track extents.
    Size toggle_switch_track_size = Size{40.0f, 20.0f};
    // Corner radius of the ToggleSwitch track and knob, as a *fraction* of the track height:
    // 0.5 is the capsule both Fluent eras and MDL2 draw (MDL2's track and knob area are 44x20 at
    // radius 10), 0.0 the square one Metro draws. `ToggleSwitch.cpp` computed
    // `track.height * 0.5f` outright, so a flat era with no rounding anywhere still got a pill;
    // Metro's track and knob are plain square rectangles, and 8.1 rounds nothing anywhere. A fraction rather than an
    // absolute radius so it keeps meaning "capsule" if an app resizes the track.
    // Not consulted by the bevel (95/98/2000) path, which is square by construction.
    float toggle_switch_track_radius_ratio = 0.5f;

    // Which of the constructions above a ToggleSwitch draws. `Theme::classic()` moves it to
    // `ClassicWell` and `resolve_aero()` to `ButtonKnob`; everything else keeps the pill.
    ToggleSwitchShape toggle_switch_shape = ToggleSwitchShape::FluentPill;
    // `ButtonKnob` only: the knob's size. It is inset by half the height left over on every
    // side and travels the rest of the track's width.
    Size toggle_button_knob_size = Size{32.0f, 16.0f};
    // ToggleSwitch knob geometry: an independent metric rather than derived from track height, so
    // it stays 12px/20px travel even if an app customizes toggle_switch_track_size. Grows 12->14
    // on hover, in either position, over `Theme::anim_faster` (not anim_fast), a faster snap
    // than RadioButton's own dot resize. `pressed` is an asymmetric 17x14 stretch, anchored
    // to whichever edge of the knob's own resting "slot" it's already sitting on
    // (knob_bounds_pressed()) rather than a uniform
    // center-anchored resize like hover's.
    struct
    {
        float rest    = 12.0f;              // knob diameter at rest, in DIPs
        float hover   = 14.0f;              // knob diameter under the pointer
        Size  pressed = Size{17.0f, 14.0f}; // the stretched knob while pressed
        float travel  = 20.0f;              // how far the knob slides between off and on
    } toggle_knob;
    // RadioButton's inner dot scales with interaction state — grows on hover, shrinks on press.
    struct
    {
        float rest    = 12.0f; // dot diameter at rest, in DIPs
        float hover   = 14.0f; // dot diameter under the pointer
        float pressed = 10.0f; // dot diameter while pressed
    } radio_dot;

    // Slider track/thumb geometry:
    // a solid (non-two-tone) track with an accent "value" fill on top, and a thumb built from
    // *two* concentric circles — a fixed-size opaque outer disc (`slider_thumb.outer`, doesn't
    // animate) plus a smaller accent-filled inner circle that grows on hover / shrinks on press
    // (`rest`/`hover`/`pressed`, mirroring `radio_dot`'s own shape). No dedicated Palette tokens
    // exist for the track/outer-disc colors yet (the strong and solid control fills aren't in
    // this codebase's `Palette`), so `Slider.cpp` derives them from `colors.text`/
    // `colors.background` instead, which every resolver (fluent/classic/metro/HC) already sets
    // correctly, rather than adding new per-resolver derivation for just this control.
    float slider_track_thickness = 4.0f;
    // The outer disc is 18 wide plus a 2 px overhang on each side (22 total, fixed, doesn't
    // animate). The inner circle has an explicit target diameter per state rather than a single
    // fixed base and ratio: rest 12 (scaling from 14), hover 14 (from 12), pressed 10 (from 14)
    // and disabled 14 (from 12, the same target as hover). Read those literally: the scale
    // factors (0.86/1.167/0.71) are relative to whatever state the transition starts from, not
    // all relative to one shared baseline, so re-deriving a ratio from a single base (as an
    // earlier pass here did, landing on pressed≈8.5) silently drifts from the real target sizes.
    struct
    {
        float outer   = 22.0f; // fixed; the outer thumb Border doesn't animate
        float rest    = 12.0f; // inner circle diameter at rest (and while disabled: hover's)
        float hover   = 14.0f; // inner circle diameter under the pointer
        float pressed = 10.0f; // inner circle diameter while pressed
        // Which of the two constructions above to draw. Only `Theme::mdl2()` opts into
        // `RoundedRect`; every other resolver keeps the concentric-circle thumb, so nothing
        // moves for them. Under `RoundedRect`, `outer` is the extent *across* the track and
        // the per-state diameter the extent *along* it — for MDL2, 24 and a flat 8.
        SliderThumbShape shape = SliderThumbShape::ConcentricCircles;
        // Corner radius of the `RoundedRect` thumb only; ignored by `ConcentricCircles`.
        // Deliberately its own field rather than `Theme::corner_radius`, because MDL2's
        // global radius is **0** and this one is 4 — the era is square everywhere except
        // here and the ToggleSwitch track, and a shared radius cannot say that.
        float radius = 4.0f;
    } slider_thumb;

    // The rest of a classic `TRACKBAR_CLASS`'s layout, read only when `slider_thumb.shape ==
    // SliderThumbShape::ClassicPointer`. Each default is what comctl32's own layout pass works out
    // for the stock 21px thumb, written as the constant rather than the expression that produces
    // it. Nothing here is a design choice this codebase gets to make: the numbers are what the
    // control measures.
    struct ClassicTrackbar
    {
        // The thumb's flat end sits this far in from the control's near edge. 2 for the stock
        // style; the control moves it to 10 to make room when ticks are drawn on *that* side,
        // which the stock style does not do.
        float thumb_margin = 2.0f;
        // How far short of each end of the control the channel stops (`uThumbLen / 4 + 3`). The
        // thumb travels inside the channel, not inside the control, so this is also the reason a
        // classic slider's thumb never overhangs its own bounds the way the Fluent one does.
        float channel_inset = 8.0f;
        // The channel's offset across the control (`uThumbLen / 2 - 1`). Note the `- 1`: the
        // channel is deliberately *not* centered on the thumb, it rides one pixel high.
        float channel_offset = 9.0f;
        // The tick row starts this far past the thumb's pointed end, and each tick is
        // `tick_length` long — one pixel longer for the two marking the ends of the range, which
        // is the whole of comctl32's `TIC_EDGE` handling.
        float tick_gap        = 2.0f;
        float tick_length     = 3.0f;
        float tick_end_length = 4.0f;

        friend bool operator==(const ClassicTrackbar&, const ClassicTrackbar&) = default;
    } classic_trackbar;

    // Whether the stretch of track *behind* the thumb is filled in the accent color.
    //
    // Every Fluent-lineage era does this, with a value fill color of its own.
    // **No classic Win32 era does**: a `TRACKBAR_CLASS` groove is one uniform channel from end to
    // end, and the thumb's position is the only indication of the value. The bevel (95/98/2000)
    // branch of `SliderControl::render()` already omitted it for that reason; this is the same statement
    // made as theme data, so Luna — which is not bevelled but is just as classic — can make it
    // too. Defaults to true, i.e. unchanged for every theme that does not say otherwise.
    bool slider_value_fill = true;

    // Luna's `TRACKBAR`, which is the one control in this codebase whose era shape is neither a
    // Fluent concentric-circle thumb nor a classic bevelled block, and cannot be reached by
    // recoloring either.
    //
    // A Luna thumb is **four stacked shapes**: a body, an accent band at each end of its
    // across-track axis, and a 1px bevel around the whole thing. The bands are the state — green
    // at rest, **amber** when hot, a darker green when pressed — while the body and the bevel never
    // move; that is why `SliderFills` alone (one flat color per state) cannot express it, and why
    // recoloring an MDL2-style flat `RoundedRect` thumb produces a slab rather than a slider.
    // Measured from `BLUE_TRACKBARHORIZONTAL_BMP.bmp`, five 11 x 21 cells: bands on rows 1-2 and
    // 18-19, body rows 3-17, and **cell 4 (focused) byte-identical to cell 2 (hot)** — on a Luna
    // slider, focus and hover really are the same appearance.
    //
    // Unset for every other theme, so `SliderControl::render()` keeps drawing exactly what it did before
    // this existed.
    struct SliderChrome
    {
        // One state's pair, in across-track order: `near_end` is the band at the lower coordinate
        // (top on a horizontal slider, left on a vertical one), which is the lit one.
        struct Bands
        {
            Color near_end; // top (horizontal) or left (vertical) band — the lit one
            Color far_end;  // the opposite band

            friend bool operator==(const Bands&, const Bands&) = default;
        };
        Bands rest;     // green at rest
        Bands hover;    // amber when hot or focused
        Bands pressed;  // a darker green while pressed
        Bands disabled; // while disabled
        // Band thickness, and how far in from the thumb's own outer edge each one starts.
        float band_extent = 2.0f;
        float band_inset  = 1.0f;
        // The thumb's 1px bevel, lit from the upper left: `light` on top/left, `dark` on
        // bottom/right. Drawn through `draw_bevel_border()` at half a step so its two rings
        // collapse into the single 1px edge the art actually has.
        Color thumb_edge_light;
        Color thumb_edge_dark;
        Color thumb_edge_light_disabled;
        Color thumb_edge_dark_disabled;
        // The track's own edges, and they run the *other* way — the groove is sunken, so the dark
        // rule is on top/left and white on bottom/right. One image cell, no states.
        Color track_edge_light;
        Color track_edge_dark;
        // The tick marks, which are the trackbar's own color rather than the groove's.
        Color tick;

        friend bool operator==(const SliderChrome&, const SliderChrome&) = default;

        // The band pair for an interaction state.
        [[nodiscard]] const Bands& bands_for_state(ButtonState s) const
        {
            switch (s)
            {
                case ButtonState::Hover:
                    return hover;
                case ButtonState::Pressed:
                    return pressed;
                case ButtonState::Disabled:
                    return disabled;
                case ButtonState::Normal:
                    break;
            }
            return rest;
        }
    };
    // Luna's four-shape slider thumb; unset for every other theme.
    std::optional<SliderChrome> slider_chrome{};

    // `SliderThumbShape::ButtonBlock`'s colors and geometry (Aero). The thumb is a
    // `ButtonVisual` per state, drawn through `draw_control_chrome` — gradient face, outline,
    // `inner_border` ring — and, when it grows a point, as the same three layers on a polygon.
    // Keyboard focus shows the hover visual, as the era does; there is no separate focus ring.
    struct SliderBlock
    {
        // The thumb's face, outline and inner ring per interaction state.
        ButtonVisual thumb_rest;
        ButtonVisual thumb_hover;
        ButtonVisual thumb_pressed;
        ButtonVisual thumb_disabled;
        // The block without ticks as drawn, along x across (a 10x21 path stroked on pixel
        // centers covers 11x22); and the pointed one's path box, of which the point is the last
        // `point` of its height.
        Size  block   = Size{11.0f, 22.0f};
        Size  pointed = Size{9.0f, 17.0f};
        float point   = 4.0f;
        // The channel: its fill, its border (a gradient across it, darker on the top edge), how
        // far in from each end of the control it stops, and its radius.
        Color               track_fill;
        LinearGradientBrush track_border;
        float               track_inset  = 5.0f;
        float               track_radius = 1.0f;
        // Ticks: their color, and the band they sit in beside the thumb's row.
        Color tick;
        float tick_length = 4.0f;

        friend bool operator==(const SliderBlock&, const SliderBlock&) = default;

        // The thumb visual for an interaction state.
        [[nodiscard]] const ButtonVisual& thumb_for_state(ButtonState s) const
        {
            switch (s)
            {
                case ButtonState::Hover:
                    return thumb_hover;
                case ButtonState::Pressed:
                    return thumb_pressed;
                case ButtonState::Disabled:
                    return thumb_disabled;
                case ButtonState::Normal:
                    break;
            }
            return thumb_rest;
        }
    };
    // Aero's block thumb; read only when `slider_thumb.shape` is ButtonBlock.
    std::optional<SliderBlock> slider_block{};

    // A drop-down list and an editable combo box built the Vista way, where Fluent draws a field
    // with a chevron glyph:
    //   - the read-only ComboBox is **one push button** across its whole width, the
    //     button's own visuals for its states, held in the pressed look while the list is open;
    //   - the editable combo (ComboBox) is a text field (`field_border_states`, focus-blue while
    //     open) with a square drop button flush in its right edge that shows only the button's
    //     inner ring at rest and lights up as a button on hover and press;
    //   - both mark it with a small solid triangle rather than a chevron, in a column the width of
    //     a scroll bar;
    //   - the list is a square white box in a 1px frame whose highlighted row is the system
    //     selection color, the selected item highlighted until the pointer moves — no pill.
    //
    // The classic packs (`Theme::classic()`: Windows 3.x/9x, and Luna and Aero under high
    // contrast) set `field_with_button` for the `COMBOBOX` class's own shape instead: *both*
    // controls are a field with a raised push button inside its right edge, a full button at rest
    // as well as on press, and a drop-down list's focus is its text drawn in the selection
    // colors. Unset for every other pack, which keeps the Fluent construction.
    struct DropdownChrome
    {
        float arrow_column = 17.0f;            // the arrow column's width, in DIPs
        Size  arrow_size   = Size{7.0f, 4.0f}; // the solid triangle's size
        Color arrow;                           // the triangle's color
        Color arrow_disabled;                  // the triangle's color while disabled
        // Where a drop-down list's text starts, from its left edge.
        float text_inset = 6.0f;
        Color list_background;     // the open list's fill
        Color list_border;         // the open list's 1px frame
        Color list_highlight;      // the highlighted row's fill
        Color list_highlight_text; // the highlighted row's text
        // A list row's padding around its text (3 on each side, plus a pixel above and below so
        // a row is not exactly one line tall).
        Thickness list_item_padding = Symmetric{.horizontal = 3.0f, .vertical = 1.0f};
        // The classic construction described above; false is Vista's push button.
        bool field_with_button = false;
        // Taken out of the arrow's column before the triangle is centered in it: 3 left and 1 top
        // on Vista's arrow, nothing on the classic `DFCS_SCROLLCOMBOBOX` glyph.
        Thickness arrow_margin = Edges{.left = 3.0f, .top = 1.0f};

        friend bool operator==(const DropdownChrome&, const DropdownChrome&) = default;
    };
    // The Vista or classic drop-down construction; unset keeps the Fluent field-and-chevron.
    std::optional<DropdownChrome> dropdown_chrome{};

    // The text-field family's own raised gradient border: the same idea as
    // `ButtonVisual::border_gradient` but a different shape (0.5->1.0 stop offsets and a 2px
    // extent, vs. Button's 0.33->1.0/3px) and, unlike Button, the *same* brush at rest and on
    // hover, so hovering a text field never recolors its border, only its fill. Lives here rather than on `Surfaces`
    // (Color-only) or `ButtonVisual` (TextBox isn't button-shaped). Unset (default) for every
    // non-Fluent theme, so a text field falls back to a flat `surfaces.field_border` stroke
    // exactly as before this field existed.
    std::optional<LinearGradientBrush> field_border_gradient{};
    float field_border_gradient_extent_px = -2.0f; // bottom-anchored: the gradient is flipped vertically

    // The text-field family's own hover, focused and disabled fills. `Surfaces::field` is one
    // color per *role*, which is all a classic/retro sunken white well ever needs. Fluent's field
    // is a four-rung interaction ramp like every other control's, and the rung that matters most
    // is focused: `palette.control_fill_input_active` is **opaque** where rest/hover are
    // translucent washes, so a
    // focused Fluent text box visibly lifts off the page instead of only growing an underline.
    // Follows `field_border_gradient` above exactly: an optional set only by `Theme::fluent()`, so
    // every non-Fluent resolver keeps painting the single flat `colors.field` in all four
    // states, as before this field existed.
    struct FieldFills
    {
        Color hover;    // the field's fill under the pointer
        Color focused;  // the field's fill while focused (opaque)
        Color disabled; // the field's fill while disabled

        friend bool operator==(const FieldFills&, const FieldFills&) = default;
    };
    // The text field's per-state fills; unset paints `colors.field` in every state.
    std::optional<FieldFills> field_fills{};

    // The accent part of a focused text field's border: the 2 epx bottom edge it grows, the
    // accent's first darker shade in Light and its second lighter shade in Dark, i.e. the theme's
    // own `palette.accent_fill_default`, **not** `palette.focus_stroke_outer`. The two are easy to
    // conflate because a focused ComboBox's border genuinely does use the outer focus stroke
    // (ComboBox.cpp reads `focus_ring` for exactly that reason), but TextBox's underline is
    // accent-colored, and reading `focus_ring` here painted
    // it plain black in Light and plain white in Dark. Unset for every non-Fluent resolver, which
    // falls back to `focus_ring` exactly as before.
    std::optional<Color> field_focus_underline{};

    // A text field's border under the pointer, when it genuinely differs from the border at rest.
    // Windows 11 uses the same color for both, so a Fluent 2 field never recolors its outline on
    // hover and only its fill moves, which is why `TextBoxControl::render()` deliberately ignores
    // `hovered_` for the border. Windows 10 does move it, from the base color at 60 % to 80 %.
    // Unset means "hover does not recolor the border", which is both Fluent 2's behavior and
    // every retro/classic/HC pack's.
    std::optional<Color> field_border_hover{};

    // Which *sides* of a focused text field thicken, and by how much (a shape change between the
    // Fluent eras rather than a value change, and so not expressible as a color). Windows 11
    // thickens **bottom-only** (a focused border of `1,1,1,2` in the dark and light themes),
    // which is why the focused field grows an accent underline rather than an accent ring.
    // Windows 10 thickens **uniformly** (`2`, and Windows 11's high-contrast theme keeps that
    // uniform 2 as well), so a focused Fluent 1 field is ringed in accent on all four sides.
    // `TextBoxControl::render()` used to hard-code the bottom-only shape for every theme.
    //
    // Only the thickened *excess* is drawn in `field_focus_underline`; the plain 1 epx stroke
    // underneath is drawn first either way, so a uniform `2` here means "1 epx of accent laid
    // over the whole existing outline", not "a 2 epx ring outside it".
    Thickness field_focus_border_thickness = Edges{.bottom = 2.0f};

    // A text field whose *whole* border is a gradient that changes per state, Aero's field
    // border: gray at rest, a lighter blue on hover, the focus blue when focused, each
    // darkest on the top edge and fading down the sides. Where `field_border_gradient` above is
    // one brush for every state and focus is an underline on top of it, here focus *is* the
    // border, so no underline is drawn. Focus wins over hover. Absolute-mapped like
    // `field_border_gradient`: `extent_px` is the span the stops are fractions of (20 DIPs down
    // from the top), so the top edge shows the first stop and a box taller than it shows the
    // last one along its bottom. Unset for every other pack.
    struct FieldBorderStates
    {
        LinearGradientBrush rest;              // the border at rest
        LinearGradientBrush hover;             // the border under the pointer
        LinearGradientBrush focused;           // the border while focused (wins over hover)
        float               extent_px = 20.0f; // the span, in DIPs, the stops are fractions of

        friend bool operator==(const FieldBorderStates&, const FieldBorderStates&) = default;
    };
    // Aero's per-state gradient field border; unset for every other pack.
    std::optional<FieldBorderStates> field_border_states{};

    // A text field's own corner radius, when it is not `corner_radius`: Aero rounds its push
    // button at 2.75 and its edit box at 1. Negative (the default) means "use `corner_radius`",
    // which every other pack does.
    float field_corner_radius = -1.0f;

    // The three toggle controls' token sets, one each (see `ToggleVisuals`). `toggle_track` is also
    // what `Slider`'s bevelled retro thumb and channel read: the nearest control in kind, a track
    // with a part you grab.
    ToggleVisuals check_box_box; // CheckBox: the box, and the tick in `text`
    ToggleVisuals radio_ring;    // RadioButton: the ring's fill and stroke, and the dot in `text`
    ToggleVisuals toggle_track;  // ToggleSwitch: the track, and the knob in `text`
    // CheckBox's indeterminate value (see `IndeterminateVisuals`). Every resolver states it: left at
    // `Theme::fluent()`'s, a pack would draw Windows 11's accent box and dash in its era's place.
    IndeterminateVisuals check_box_indeterminate;

    // States the same set for all three controls — what a resolver whose era draws them alike does.
    void set_toggle_visuals(const ToggleVisuals& v)
    {
        check_box_box = radio_ring = toggle_track = v;
    }
    // All three, for a resolver adjusting one field on every control at once.
    [[nodiscard]] std::array<ToggleVisuals*, 3> toggles()
    {
        return {&check_box_box, &radio_ring, &toggle_track};
    }

    // SwipeItem's own pre-threshold Execute-mode look: a muted, not-yet-committed fill, distinct
    // from `accent_button_normal`. The post-threshold look equals that exactly (the accent fill
    // with the primary text-on-accent color), so post-threshold reuses it rather than
    // duplicating a second token set for the same colors. `border` stays transparent: SwipeItem
    // draws no border in either state.
    ButtonVisual swipe_item_pre_threshold;
    // SwipeItem's own Reveal-mode look, and Execute-mode's steady (non-pressed) state before any
    // drag starts: the tertiary control fill with the primary text color, a neutral surface tone.
    // Genuinely distinct from `swipe_item_pre_threshold` above despite sharing the same
    // background: its label is the primary text color, not the pre-threshold look's strong
    // control color. Reveal items never turn accent-colored the way Execute's post-threshold
    // state does; `accent_button_normal` was the wrong default for every Reveal item, not just
    // Execute's pre-threshold moment.
    ButtonVisual swipe_item_default;

    // The window frame an era draws itself under `Frame::Themed`: the border, the caption and its
    // buttons (src/ui/WindowFrame.cpp). Unset, the theme has no era frame and a themed window
    // keeps the OS one. Sizes are DIPs.
    struct WindowFrame
    {
        // How the caption buttons and the border are constructed.
        enum class Kind
        {
            // Windows 95 to 2000: a raised two-ring bevel, the border color inside it, beveled
            // caption buttons with pixel glyphs.
            Classic,
            // Windows 3.x: the border color between two 1 px frame lines, notched at the corners,
            // a centered title, the Control-menu box, and beveled arrow buttons with no close box.
            Win3x,
            // Windows 2.x: the same frame without the notches, and flat boxes with outline arrows.
            Win2x,
            // Windows 1.x: one frame line, and a caption with nothing on it but the System-menu box.
            Win1x,
            // Windows XP: painted frame strips, a caption ramp that carries the top of the frame, and
            // glossy rounded buttons with a red close button.
            Luna,
            // Windows 7 without the compositor (Aero Basic): an opaque light-blue frame, and a row of
            // buttons hanging from the top edge with a wide red close button.
            AeroBasic,
            // Windows 8: one flat color for frame and caption, a centered title, and flat buttons
            // hanging from the top edge, blue when hot, with a red close button.
            Metro,
        };
        Kind kind = Kind::Classic;

        // The frame around the caption and the content, each side; also the resize band.
        float border = 4.0f;
        // COLOR_WINDOWFRAME: the 1 px lines of the Windows 1.x to 3.x frames, under the caption and
        // between its boxes.
        Color frame_line;
        // Painted frame strips (Luna): the left and right strips' columns left to right, the bottom
        // strip's rows top to bottom, each `border` long.
        std::vector<Color> active_left, inactive_left;
        std::vector<Color> active_right, inactive_right;
        std::vector<Color> active_bottom, inactive_bottom;
        // COLOR_ACTIVEBORDER/INACTIVEBORDER: the frame between the bevel and the caption.
        Color active_border;
        Color inactive_border;
        // Rounded top corners (Luna): the pixels cut off each top corner, one entry per row from the
        // top, at 96 DPI. Empty for a square frame.
        std::vector<std::uint8_t> top_corner_cut;
        // The art of those corners, painted over the caption: `width` pixels a row from the top, as
        // many rows as top_corner_cut, the cut ones transparent. A right corner's rows end at the
        // window's right edge.
        struct CornerArt
        {
            int                width = 0;
            std::vector<Color> pixels;

            friend bool operator==(const CornerArt&, const CornerArt&) = default;
        };
        CornerArt active_top_left, active_top_right;
        CornerArt inactive_top_left, inactive_top_right;
        // The caption's own shaded side edges (Luna), each side from the outside in: its two
        // outermost columns a color per caption row, then columns fading from the second into the
        // row's caption color, `fade` the second column's share in each.
        struct CaptionEdge
        {
            std::array<std::vector<Color>, 2> columns;
            std::vector<float>                fade;

            friend bool operator==(const CaptionEdge&, const CaptionEdge&) = default;
        };
        CaptionEdge active_caption_left, active_caption_right;
        CaptionEdge inactive_caption_left, inactive_caption_right;
        // The caption bar, and the line of border color under it before the content starts.
        float caption_height = 18.0f;
        float caption_rule   = 1.0f;
        // The caption fill, left to right; two equal stops for a flat caption.
        LinearGradientBrush active_caption;
        LinearGradientBrush inactive_caption;
        Color               active_text;
        Color               inactive_text;
        std::string         font_family;
        float               font_size   = 11.0f;
        FontWeight          font_weight = FontWeight::Bold;
        // A drop shadow one pixel down and right under the active title (Luna).
        std::optional<Color> active_text_shadow{};
        // Windows 1.x to 3.x center the title between the caption's boxes.
        bool title_centered = false;
        // The Control-menu box at the caption's left (Windows 1.x to 3.x), without the frame line
        // after it.
        float menu_box_width = 0.0f;
        // The icon box at the caption's left, and the gap from it to the title.
        float icon_size  = 16.0f;
        float icon_left  = 2.0f;
        float title_left = 3.0f;
        // The buttons: their size, their offset from the caption's top and right edges, and the
        // gap that sets the close button apart from minimize/maximize. Windows 1.x to 3.x have no
        // close button, and their button size includes the frame line on its left.
        Size  button{16.0f, 14.0f};
        float button_top   = 2.0f;
        float button_right = 2.0f;
        float close_gap    = 2.0f;
        // The gap between minimize and maximize.
        float button_gap = 0.0f;
        // A close button wider than the others (Aero Basic); 0 is the same width.
        float close_width = 0.0f;
        // A minimize button narrower than maximize (Windows 8); 0 is the same width.
        float minimize_width = 0.0f;
        // The outline around a two-color glyph's fill (Aero Basic).
        Color glyph_outline{};
        // A glossy button (Luna): its 1 px rounded outline, the fill inside it running corner to
        // corner, and the glyph color.
        struct Face
        {
            Color               outline;
            LinearGradientBrush fill;
            Color               glyph;
            // The column just inside the outline on either side (Aero Basic).
            Color inner{};

            friend bool operator==(const Face&, const Face&) = default;
        };
        // Normal, hot, pressed and disabled, on an active window and then on an inactive one.
        std::array<Face, 8> caption_faces{};
        std::array<Face, 8> close_faces{};
        // The button face, its bevel ramp and its glyph; a disabled glyph is embossed in
        // `glyph_disabled` over `glyph_disabled_shadow`. Windows 3.x reads only the highlight and
        // the shadow; Windows 1.x and 2.x boxes are `button_face` with `glyph` ink.
        Color button_face;
        Color bevel_light;
        Color bevel_highlight;
        Color bevel_shadow;
        Color bevel_dark_shadow;
        Color glyph;
        Color glyph_disabled;
        Color glyph_disabled_shadow;

        // Aero glass: what the frame paints over the blurred desktop while the host provides one
        // (Window::blur_behind()); without it the frame stays opaque.
        struct Glass
        {
            // The colorization over the blur, its alpha the share it takes, active and inactive.
            Color active_tint;
            Color inactive_tint;
            // The 1 px line round the outside, and the highlight just inside it.
            Color outline;
            Color highlight;
            // The 1 px line round the content, and the white glow behind the caption title.
            Color content_edge;
            Color glow;
            // The reflection streaks' white, at its peak alpha.
            Color streak;

            friend bool operator==(const Glass&, const Glass&) = default;
        };
        std::optional<Glass> glass{};

        friend bool operator==(const WindowFrame&, const WindowFrame&) = default;
    };
    std::optional<WindowFrame> window_frame{};
};

// Luna's window frame, measured from the shipped style (LunaWindowFrame.cpp).
[[nodiscard]] ControlChrome::WindowFrame luna_window_frame();
// Aero Basic's window frame, measured from the style's Basic art (AeroWindowFrame.cpp).
[[nodiscard]] ControlChrome::WindowFrame aero_basic_window_frame();
// Windows 8's window frame in the window color `color`, measured from the style's DWM atlas
// (MdlWindowFrame.cpp).
[[nodiscard]] ControlChrome::WindowFrame mdl_window_frame(const Color& color);

// The one door into Theme's private pointer.
struct ControlChromeAccess
{
    [[nodiscard]] static const std::shared_ptr<const ControlChrome>& get(const Theme& theme)
    {
        return theme.control_chrome_;
    }
    [[nodiscard]] static std::shared_ptr<const ControlChrome>& get(Theme& theme)
    {
        return theme.control_chrome_;
    }
    [[nodiscard]] static ThemeColors& colors(Theme& theme)
    {
        return theme.colors_;
    }
};

// The theme's role colors, for writing: what the theme packs fill in while they build a theme.
[[nodiscard]] inline ThemeColors& edit_colors(Theme& theme)
{
    return ControlChromeAccess::colors(theme);
}

// What a theme without its own record reads: every field at its default.
[[nodiscard]] const ControlChrome& default_control_chrome();

// The theme's control chrome, for reading.
// Whether `theme` carries the Fluent token table (Theme::palette): Fluent 1 and 2 outside high
// contrast. Every other theme leaves it default, each color opaque black.
[[nodiscard]] inline bool has_fluent_palette(const Theme& theme)
{
    return (theme.variant == ThemeVariant::Fluent2 || theme.variant == ThemeVariant::Fluent) && !theme.high_contrast;
}

[[nodiscard]] inline const ControlChrome& controls(const Theme& theme)
{
    const std::shared_ptr<const ControlChrome>& chrome = ControlChromeAccess::get(theme);
    return chrome ? *chrome : default_control_chrome();
}

// The theme's control chrome, for writing: copies it first when another theme shares it, so an
// edit never shows through in a copy. Take a fresh reference after copying `theme`, since the old
// one may still point at the shared record.
[[nodiscard]] ControlChrome& edit_controls(Theme& theme);

// The radius a ToolTip's surface — and the popup shadow behind it — is drawn at.
[[nodiscard]] inline float tooltip_radius(const Theme& theme)
{
    const float radius = controls(theme).tooltip_corner_radius;
    return radius >= 0.0f ? radius : theme.corner_radius;
}

// `field_corner_radius` if set, `corner_radius` otherwise.
[[nodiscard]] inline float resolved_field_corner_radius(const Theme& theme)
{
    const float radius = controls(theme).field_corner_radius;
    return radius >= 0.0f ? radius : theme.corner_radius;
}

// ListView/TreeView/TableView row background when `list_item_subtle_selection` is set —
// see that field's own comment for the source. Selected rows read one rung "hotter" than
// the same unselected state (rest->secondary, hover->tertiary), except a pressed selected row,
// which stays at secondary rather than deepening to tertiary the way the unselected ramp's own
// pressed rung does: a real quirk, identical across ListView, TreeView and the tabular
// surfaces, not a typo. Callers still gate on
// `list_item_subtle_selection` themselves before calling this instead of reading
// `colors.selection` — this only picks the color once they have. With
// `row_selection_fills` set, a selected Fill row takes those accent fills instead.
[[nodiscard]] inline Color subtle_row_background(const Theme&      theme,
                                                 ButtonState       s,
                                                 bool              selected,
                                                 ThemeRowSelection family = ThemeRowSelection::Fill)
{
    if (!selected)
    {
        return theme.subtle_for_state(s).background;
    }
    // Windows 10 fills a selected row with accent (`row_selection_fills`, Fluent 1 only).
    // Disabled falls to `rest`: the era has no disabled selected fill for a list row.
    const std::optional<ControlChrome::AccentSelectionFills>& fills = controls(theme).row_selection_fills;
    if (fills)
    {
        if (family == ThemeRowSelection::IndicatorOnly)
        {
            // In this era a selected or checked NavigationShell item takes the same fills the
            // unselected states use.
            return theme.subtle_for_state(s).background;
        }
        switch (s)
        {
            case ButtonState::Hover:
                return fills->hover;
            case ButtonState::Pressed:
                return fills->pressed;
            case ButtonState::Disabled:
            case ButtonState::Normal:
                break;
        }
        return fills->rest;
    }
    return s == ButtonState::Hover ? theme.subtle_button_pressed.background : theme.subtle_button_hover.background;
}

// The bare primary/secondary/tertiary/disabled text color ramp, for a control whose states
// are carried entirely by its text color rather than by a fill (see
// `chromeless_text_states` above, which callers gate on before calling this). Reads
// `palette` directly, so it is only meaningful under `Theme::fluent()`.
[[nodiscard]] inline Color text_fill_for_state(const Theme& theme, ButtonState s)
{
    switch (s)
    {
        case ButtonState::Hover:
            return theme.palette.text_fill_secondary;
        case ButtonState::Pressed:
            return theme.palette.text_fill_tertiary;
        case ButtonState::Disabled:
            return theme.palette.text_fill_disabled;
        case ButtonState::Normal:
            break;
    }
    return theme.palette.text_fill_primary;
}

// Fills the ThemeColors roles that are copies of the era's records (the accent and control fills,
// the strokes, card and overlay, the severities, the secondary and on-accent text) from the theme's
// button visuals, surfaces, severities and, for an unforced Fluent theme, its palette. Every
// factory and resolve_theme() run it last, after the era's own edits.
void fill_derived_colors(Theme& theme);

} // namespace detail

} // namespace ui
