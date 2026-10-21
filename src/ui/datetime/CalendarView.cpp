// SPDX-License-Identifier: MIT
#include "winui/ui/datetime/CalendarView.hpp"

#include "../../core/StringsDetail.hpp"
#include "../scrolling/ScrollArrowButton.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/DateFormat.hpp"
#include "winui/core/Strings.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/menus/MenuItem.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <cmath>

namespace ui
{

namespace
{
using std::chrono::days;
using std::chrono::sys_days;
using std::chrono::weekday;
using std::chrono::year;
using std::chrono::year_month_day;

constexpr int    CalendarColumns       = 7;
constexpr int    CalendarWeeks         = 6;
constexpr int    CalendarZoomCells     = 4; // the year and decade views are 4 x 4
constexpr double CalendarSlideMs       = 250.0;
constexpr double CalendarHeadingFadeMs = 167.0;
constexpr float  CalendarZoomSmall     = 0.84f;
constexpr float  CalendarZoomLarge     = 1.29f;
constexpr int    CalendarYearsEachWay  = 100;

year_month_day calendar_first_of_month(year_month_day date)
{
    return year_month_day{date.year(), date.month(), std::chrono::day{1}};
}

year_month_day calendar_add_months(year_month_day date, int months)
{
    const std::chrono::year_month ym = std::chrono::year_month{date.year(), date.month()} + std::chrono::months{months};
    const auto last = std::chrono::year_month_day_last{ym.year(), std::chrono::month_day_last{ym.month()}};
    return year_month_day{ym.year(), ym.month(), std::min(date.day(), last.day())};
}

year_month_day calendar_add_years(year_month_day date, int years)
{
    return calendar_add_months(date, years * 12);
}

int calendar_decade_start(int y)
{
    return y - (((y % 10) + 10) % 10);
}

// The text layout a cell's number is drawn in: one centered line.
TextStyle calendar_cell_style(const Theme& theme, float size, FontWeight weight)
{
    TextStyle style   = theme.text_style;
    style.font_size   = size;
    style.weight      = weight;
    style.horizontal  = TextAlign::Center;
    style.vertical    = TextAlign::Center;
    style.line_height = 0.0f;
    style.wrapping    = TextWrapping::NoWrap;
    return style;
}

float calendar_ease_out(float t)
{
    // The controls' fast-out-slow-in spline (0,0,0,1), close enough as a cubic ease-out.
    const float u = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - u * u * u;
}

} // namespace

// ---- the look, per theme ----

struct CalendarViewControl::Look
{
    enum class Kind
    {
        Rounded, // Fluent 2
        Square,  // Windows 10
        Classic, // comctl32, Windows 95 to XP (and the 3.x and 1.x/2.x stand-ins)
        Aero,    // comctl32 6, Windows Vista to 8
        Applet,  // the Date/Time control panel, Windows 95 and 98
    };
    Kind  kind          = Kind::Rounded;
    bool  rounded       = true;
    float width         = 300.0f;
    float height        = 350.0f;
    float border        = 1.0f;
    float corner        = 4.0f;
    float header_height = 47.0f; // the heading row
    float divider       = 1.0f;
    float weekday_row   = 44.0f; // the weekday names, with the grid's top margin
    float grid_margin   = 2.0f;  // around the grid, left, right and bottom
    float item_margin   = 1.0f;
    float item_border   = 1.0f;
    float day_font      = 14.0f;
    float label_font    = 8.0f;
    float zoom_font     = 14.0f;
    float heading_font  = 14.0f;
    float weekday_font  = 12.0f;

    FontWeight heading_weight = FontWeight::SemiBold;
    FontWeight weekday_weight = FontWeight::SemiBold;
    FontWeight today_weight   = FontWeight::Regular;

    IconGlyph up{};
    IconGlyph down{};

    // Where things go, relative to the control's top-left corner: the day grid (7 x 6), the
    // weekday names over it, the year and decade grid (4 columns, `zoom_rows` rows), and the
    // "Today:" footer (Classic, Aero).
    Rect month_grid{3.0f, 95.0f, 294.0f, 252.0f};
    Rect weekday_band{3.0f, 51.0f, 294.0f, 42.0f};
    Rect zoom_area{3.0f, 51.0f, 294.0f, 296.0f};
    int  zoom_rows = 4;
    Rect footer{};
    Rect separator{}; // the rule under the weekday names (Classic, Aero)

    // The parts, relative to the control's top-left corner.
    Rect  heading{};
    Rect  previous{};
    Rect  next{};
    float heading_text_inset = 8.0f;
    float part_radius        = 4.0f;

    Color background, border_color, heading_text, heading_hover_text, heading_pressed_text, heading_disabled_text;
    Color glyph, glyph_hover, glyph_pressed, glyph_disabled;
    Color part_hover_fill, part_pressed_fill;
    Color weekday_text, weekday_disabled;
    Color item_text, item_hover_fill, item_pressed_fill, item_pressed_text;
    Color hover_ring, pressed_ring, selected_ring, selected_hover_ring, selected_pressed_ring;
    Color selected_text, selected_hover_text, selected_pressed_text;
    Color today_fill, today_hover_fill, today_pressed_fill, today_disabled_fill, today_text, today_selected_ring;
    Color today_hover_ring, today_pressed_ring;
    Color out_of_scope_text, out_of_scope_hover_text, out_of_scope_pressed_text, out_of_scope_fill;
    Color blackout_text, strikethrough, disabled_text;

    // Classic and Aero.
    Color title_fill, title_text, title_hover, separator_color, trailing_text, selected_fill, hover_fill, today_mark;
    Color footer_text;
    bool  rect_selection    = false;
    bool  today_frame       = false;
    float footer_text_width = 0.0f; // measured by measure_override()

    // Applet: the month list, the year spin box and the sunken box around the band and the grid,
    // and the digit size the selected day's box is fitted to.
    Rect  month_list{};
    Rect  year_spin{};
    Rect  box{};
    float digit_width = 6.0f;  // measured by measure_override()
    float line_height = 13.0f; // measured by measure_override()

    // The designs without motion, a month or year view of their own, or hover.
    [[nodiscard]] bool comctl() const
    {
        return kind == Kind::Classic || kind == Kind::Aero || kind == Kind::Applet;
    }
    // Applet: the selected day's box, one pixel wider than its number and a line tall, centered
    // in the cell.
    [[nodiscard]] Rect day_box(const Rect& cell, std::size_t digits) const
    {
        const float w = std::round(digit_width * static_cast<float>(digits)) + 1.0f;
        const float h = std::round(line_height);
        return Rect{std::round(cell.center().x - w * 0.5f), std::round(cell.center().y - h * 0.5f), w, h};
    }
};

CalendarViewControl::Look CalendarViewControl::make_look(const Theme& theme)
{
    CalendarViewControl::Look look;
    const ThemeColors&        c = theme.colors();

    const auto& era = detail::controls(theme).calendar_chrome;
    if (era && era->kind == detail::ControlChrome::CalendarChrome::Kind::Applet)
    {
        // 164 x 162: the 75 x 21 month list and the 79 x 21 year spin box 10 apart, then 11 below
        // them the 162 x 130 sunken box, its 18 px band of initials over six 18 px rows.
        look.kind              = Look::Kind::Applet;
        look.rounded           = false;
        look.border            = 0.0f;
        look.corner            = 0.0f;
        look.width             = 164.0f;
        look.height            = 162.0f;
        look.header_height     = 0.0f;
        look.divider           = 0.0f;
        look.item_margin       = 0.0f;
        look.item_border       = 0.0f;
        look.day_font          = theme.text_style.font_size;
        look.label_font        = theme.text_style.font_size;
        look.zoom_font         = theme.text_style.font_size;
        look.heading_font      = theme.text_style.font_size;
        look.weekday_font      = theme.text_style.font_size;
        look.heading_weight    = FontWeight::Regular;
        look.weekday_weight    = FontWeight::Regular;
        look.part_radius       = 0.0f;
        look.month_list        = Rect{0.0f, 0.0f, 75.0f, 21.0f};
        look.year_spin         = Rect{85.0f, 0.0f, 79.0f, 21.0f};
        look.box               = Rect{0.0f, 32.0f, 162.0f, 130.0f};
        look.weekday_band      = Rect{2.0f, 34.0f, 158.0f, 18.0f};
        look.month_grid        = Rect{2.0f, 52.0f, 158.0f, 108.0f};
        look.zoom_area         = look.month_grid;
        look.background        = era->background;
        look.border_color      = colors::Transparent;
        look.title_fill        = era->title_fill;
        look.weekday_text      = era->weekday_text;
        look.weekday_disabled  = era->weekday_text;
        look.item_text         = era->item_text;
        look.trailing_text     = era->trailing_text;
        look.out_of_scope_text = era->trailing_text;
        look.selected_fill     = era->selected_fill;
        look.selected_text     = era->title_text;
        look.blackout_text     = era->blackout_text;
        look.disabled_text     = era->disabled_text;
        return look;
    }
    if (era && era->kind == detail::ControlChrome::CalendarChrome::Kind::Classic)
    {
        // comctl32's month calendar: a 30 px caption band, 15 px rows, a 7 x 6 grid of
        // `cell_width` cells under a 1 px rule, and an 18 px footer.
        const float cw             = era->cell_width;
        look.kind                  = Look::Kind::Classic;
        look.rounded               = false;
        look.border                = 0.0f;
        look.corner                = 0.0f;
        look.width                 = cw * 7.0f + 1.0f;
        look.height                = 153.0f;
        look.header_height         = 30.0f;
        look.divider               = 0.0f;
        look.item_margin           = 0.0f;
        look.item_border           = 1.0f;
        look.day_font              = theme.text_style.font_size;
        look.label_font            = theme.text_style.font_size;
        look.zoom_font             = theme.text_style.font_size;
        look.heading_font          = theme.text_style.font_size;
        look.weekday_font          = theme.text_style.font_size;
        look.heading_weight        = FontWeight::Bold;
        look.weekday_weight        = FontWeight::Regular;
        look.today_weight          = FontWeight::Regular;
        look.part_radius           = 0.0f;
        look.heading               = Rect{32.0f, 6.0f, look.width - 64.0f, 17.0f};
        look.previous              = Rect{5.0f, 6.0f, 22.0f, 17.0f};
        look.next                  = Rect{look.width - 5.0f - 22.0f, 6.0f, 22.0f, 17.0f};
        look.weekday_band          = Rect{0.0f, 30.0f, cw * 7.0f, 14.0f};
        look.separator             = Rect{4.0f, 44.0f, look.width - 9.0f, 1.0f};
        look.month_grid            = Rect{0.0f, 45.0f, cw * 7.0f, 90.0f};
        look.zoom_area             = Rect{0.0f, 30.0f, cw * 7.0f, 105.0f};
        look.zoom_rows             = 3;
        look.footer                = Rect{0.0f, 135.0f, look.width, 18.0f};
        look.background            = era->background;
        look.border_color          = colors::Transparent;
        look.title_fill            = era->title_fill;
        look.title_text            = era->title_text;
        look.title_hover           = era->title_text;
        look.heading_text          = era->title_text;
        look.heading_disabled_text = era->title_text;
        look.weekday_text          = era->weekday_text;
        look.weekday_disabled      = era->disabled_text;
        look.separator_color       = era->separator;
        look.item_text             = era->item_text;
        look.trailing_text         = era->trailing_text;
        look.out_of_scope_text     = era->trailing_text;
        look.selected_fill         = era->selected_fill;
        look.selected_text         = era->title_text;
        look.today_mark            = era->today_mark;
        look.today_text            = era->item_text;
        look.footer_text           = era->footer_text;
        look.blackout_text         = era->blackout_text;
        look.disabled_text         = era->disabled_text;
        look.rect_selection        = era->rect_selection;
        look.today_frame           = era->today_frame;
        return look;
    }
    if (era && era->kind == detail::ControlChrome::CalendarChrome::Kind::Aero)
    {
        // The Vista design: a 1 px frame, a plain title over 31 x 15 cells, a 20 px footer.
        look.kind                  = Look::Kind::Aero;
        look.rounded               = false;
        look.border                = 1.0f;
        look.corner                = 0.0f;
        look.width                 = 225.0f;
        look.height                = 160.0f;
        look.header_height         = 31.0f;
        look.divider               = 0.0f;
        look.item_margin           = 0.0f;
        look.item_border           = 1.0f;
        look.day_font              = 12.0f; // Segoe UI 9 pt
        look.label_font            = 12.0f;
        look.zoom_font             = 12.0f;
        look.heading_font          = 12.0f;
        look.weekday_font          = 12.0f;
        look.heading_weight        = FontWeight::Regular;
        look.weekday_weight        = FontWeight::Regular;
        look.today_weight          = FontWeight::Regular;
        look.part_radius           = 0.0f;
        look.heading               = Rect{25.0f, 8.0f, 175.0f, 20.0f};
        look.previous              = Rect{3.0f, 10.0f, 16.0f, 16.0f};
        look.next                  = Rect{206.0f, 10.0f, 16.0f, 16.0f};
        look.weekday_band          = Rect{4.0f, 32.0f, 217.0f, 16.0f};
        look.separator             = Rect{4.0f, 48.0f, 217.0f, 1.0f};
        look.month_grid            = Rect{4.0f, 49.0f, 217.0f, 90.0f};
        look.zoom_area             = Rect{4.0f, 33.0f, 217.0f, 105.0f};
        look.zoom_rows             = 3;
        look.footer                = Rect{1.0f, 139.0f, 223.0f, 20.0f};
        look.background            = era->background;
        look.border_color          = era->border;
        look.heading_text          = era->heading;
        look.title_text            = era->heading;
        look.title_hover           = era->heading_hover;
        look.heading_disabled_text = era->heading_disabled;
        look.glyph                 = era->heading;
        look.glyph_hover           = era->heading_hover;
        look.glyph_pressed         = era->heading_pressed;
        look.glyph_disabled        = era->heading_disabled;
        look.weekday_text          = era->weekday_text;
        look.weekday_disabled      = era->disabled_text;
        look.separator_color       = era->separator;
        look.item_text             = era->item_text;
        look.trailing_text         = era->trailing_text;
        look.out_of_scope_text     = era->trailing_text;
        look.selected_ring         = era->selected_border;
        look.selected_fill         = era->selected_fill;
        look.hover_ring            = era->hover_border;
        look.hover_fill            = era->hover_fill;
        look.today_mark            = era->today_mark;
        look.today_text            = era->today_text;
        look.footer_text           = era->footer_text;
        look.blackout_text         = era->blackout_text;
        look.disabled_text         = era->disabled_text;
        return look;
    }
    if (const auto& square = era)
    {
        // Windows 10's square construction.
        look.kind               = Look::Kind::Square;
        look.rounded            = false;
        look.width              = 296.0f;
        look.height             = 332.0f;
        look.corner             = square->corner_radius;
        look.header_height      = 40.0f;
        look.divider            = 0.0f;
        look.weekday_row        = 38.0f;
        look.grid_margin        = 0.0f;
        look.item_border        = 2.0f;
        look.day_font           = 20.0f;
        look.label_font         = 12.0f;
        look.zoom_font          = 20.0f;
        look.heading_font       = 20.0f;
        look.heading_weight     = FontWeight::Regular;
        look.weekday_weight     = FontWeight::Regular;
        look.today_weight       = FontWeight::SemiBold;
        look.up                 = IconGlyph{0xE0E4, "Segoe MDL2 Assets", 20.0f};
        look.down               = IconGlyph{0xE0E5, "Segoe MDL2 Assets", 20.0f};
        look.heading_text_inset = 12.0f;
        look.part_radius        = 0.0f;
        // Columns 5* | 1* | 1* across the 294 DIPs inside the frame.
        const float inner = look.width - 2.0f * look.border;
        const float unit  = inner / 7.0f;
        look.heading      = Rect{look.border, look.border, unit * 5.0f, look.header_height};
        look.previous     = Rect{look.border + unit * 5.0f, look.border, unit, look.header_height};
        look.next         = Rect{look.border + unit * 6.0f, look.border, unit, look.header_height};
        look.weekday_band = Rect{look.border, look.border + look.header_height, inner, look.weekday_row};
        look.month_grid   = Rect{look.border,
                                 look.weekday_band.bottom(),
                                 inner,
                                 look.height - look.border - look.weekday_band.bottom()};
        look.zoom_area = Rect{look.border, look.weekday_band.y, inner, look.height - look.border - look.weekday_band.y};

        look.background                = square->background;
        look.border_color              = square->border;
        look.heading_text              = square->heading;
        look.heading_hover_text        = square->heading_hover;
        look.heading_pressed_text      = square->heading_pressed;
        look.heading_disabled_text     = square->heading_disabled;
        look.glyph                     = square->heading;
        look.glyph_hover               = square->heading_hover;
        look.glyph_pressed             = square->heading_pressed;
        look.glyph_disabled            = square->heading_disabled;
        look.part_hover_fill           = colors::Transparent;
        look.part_pressed_fill         = colors::Transparent;
        look.weekday_text              = square->item_text;
        look.weekday_disabled          = square->disabled_text;
        look.item_text                 = square->item_text;
        look.item_hover_fill           = colors::Transparent;
        look.item_pressed_fill         = colors::Transparent;
        look.item_pressed_text         = square->item_text;
        look.hover_ring                = square->hover_border;
        look.pressed_ring              = square->pressed_border;
        look.selected_ring             = square->selected_border;
        look.selected_hover_ring       = square->selected_hover_border;
        look.selected_pressed_ring     = square->selected_pressed_border;
        look.selected_text             = square->item_text;
        look.selected_hover_text       = square->item_text;
        look.selected_pressed_text     = square->item_text;
        look.today_fill                = square->today_fill;
        look.today_hover_fill          = square->today_fill;
        look.today_pressed_fill        = square->today_fill;
        look.today_disabled_fill       = square->disabled_text;
        look.today_text                = square->today_text;
        look.today_selected_ring       = square->today_selected_ring;
        look.today_hover_ring          = square->today_hover_border;
        look.today_pressed_ring        = square->today_pressed_border;
        look.out_of_scope_text         = square->item_text;
        look.out_of_scope_hover_text   = square->item_text;
        look.out_of_scope_pressed_text = square->item_text;
        look.out_of_scope_fill         = square->out_of_scope_fill;
        look.blackout_text             = square->blackout_text;
        look.strikethrough             = colors::Transparent;
        look.disabled_text             = square->disabled_text;
        return look;
    }

    // Fluent 2's round construction, from the theme's colors.
    look.up               = glyphs::ScrollUp;
    look.down             = glyphs::ScrollDown;
    look.up.font_family   = theme.icon_font_family;
    look.down.font_family = theme.icon_font_family;
    // The heading button sits at 7,6 with 3 to its right, the paging buttons are 32 wide with 3
    // and 3 (7 at the far end) around them, and all three are 34 tall.
    const float top   = look.border + 6.0f;
    const float part  = 34.0f;
    const float right = look.width - look.border;
    look.next         = Rect{right - 7.0f - 32.0f, top, 32.0f, part};
    look.previous     = Rect{look.next.x - 3.0f - 3.0f - 32.0f, top, 32.0f, part};
    look.heading      = Rect{look.border + 7.0f, top, look.previous.x - 3.0f - 3.0f - (look.border + 7.0f), part};

    const auto& field              = detail::controls(theme).field_fills;
    look.background                = field ? field->focused : c.field;
    look.border_color              = c.control_stroke;
    look.heading_text              = c.text;
    look.heading_hover_text        = c.text;
    look.heading_pressed_text      = c.text_secondary;
    look.heading_disabled_text     = c.text_disabled;
    look.glyph                     = c.control_stroke_strong;
    look.glyph_hover               = c.control_stroke_strong;
    look.glyph_pressed             = c.control_stroke_strong;
    look.glyph_disabled            = c.text_disabled;
    look.part_hover_fill           = theme.subtle_button_hover.background;
    look.part_pressed_fill         = theme.subtle_button_pressed.background;
    look.weekday_text              = c.text;
    look.weekday_disabled          = c.text_disabled;
    look.item_text                 = c.text;
    look.item_hover_fill           = theme.subtle_button_hover.background;
    look.item_pressed_fill         = theme.subtle_button_pressed.background;
    look.item_pressed_text         = c.text_secondary;
    look.hover_ring                = theme.subtle_button_hover.background;
    look.pressed_ring              = theme.subtle_button_pressed.background;
    look.selected_ring             = c.accent;
    look.selected_hover_ring       = c.accent_hover;
    look.selected_pressed_ring     = theme.subtle_button_pressed.background;
    look.selected_text             = c.link;
    look.selected_hover_text       = c.link;
    look.selected_pressed_text     = c.accent;
    look.today_fill                = c.accent;
    look.today_hover_fill          = c.accent_hover;
    look.today_pressed_fill        = c.accent_pressed;
    look.today_disabled_fill       = theme.accent_button_disabled.background;
    look.today_text                = c.text_on_accent;
    look.today_selected_ring       = c.text_on_accent;
    look.today_hover_ring          = colors::Transparent;
    look.today_pressed_ring        = colors::Transparent;
    look.out_of_scope_text         = c.text_secondary;
    look.out_of_scope_hover_text   = c.text;
    look.out_of_scope_pressed_text = c.control_stroke_strong;
    look.out_of_scope_fill         = colors::Transparent;
    look.blackout_text             = c.text;
    look.strikethrough             = c.control_stroke_strong;
    look.disabled_text             = c.text_disabled;
    return look;
}

// ---- the parts: the heading and the paging buttons ----

namespace detail
{

// A button the calendar draws itself: focusable, invokable, with its hover and press state read
// by the calendar when it draws.
class CalendarPart final : public Element, private InvokeProvider
{
public:
    enum class Kind
    {
        Heading,
        Previous,
        Next,
    };

    CalendarPart(CalendarViewControl& owner, Kind kind) : owner_(owner), kind_(kind)
    {
        set_parent_of(*this, &owner);
    }

    [[nodiscard]] bool available() const
    {
        if (!owner_.is_enabled() || owner_.applet())
        {
            return false;
        }
        switch (kind_)
        {
            case Kind::Heading:
                return owner_.display_mode() != CalendarViewDisplayMode::Decade ||
                       owner_.look_->kind == CalendarViewControl::Look::Kind::Classic;
            case Kind::Previous:
                return owner_.has_more_before();
            case Kind::Next:
                return owner_.has_more_after();
        }
        return false;
    }
    [[nodiscard]] bool hovered() const
    {
        return hovered_;
    }
    [[nodiscard]] bool pressed() const
    {
        return pressed_;
    }

    [[nodiscard]] bool focusable() const override
    {
        return available();
    }
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Button;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        switch (kind_)
        {
            case Kind::Heading:
                return owner_.heading(owner_.display_mode(), owner_.display_date());
            case Kind::Previous:
                return strings().date_time.previous;
            case Kind::Next:
                return strings().date_time.next;
        }
        return {};
    }
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override
    {
        return detail::controls(theme).calendar_chrome ? 0.0f : 4.0f;
    }
    [[nodiscard]] bool focusable_by_pointer() const
    {
        return available();
    }
    [[nodiscard]] bool focus_ring_inside() const override
    {
        return true;
    }

protected:
    [[nodiscard]] Size measure_override(Size available_size, const MeasureContext&) const override
    {
        return available_size;
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
        // The calendar draws its parts.
    }
    bool pointer_override(const PointerEvent& e) override
    {
        const bool inside = bounds().contains(e.position);
        switch (e.action)
        {
            case PointerAction::Move:
                set_state(inside, pressed_);
                return false;
            case PointerAction::Leave:
                set_state(false, pressed_);
                return false;
            case PointerAction::Down:
                if (e.button == PointerButton::Left && inside && available())
                {
                    set_state(true, true);
                    return true;
                }
                return false;
            case PointerAction::Up:
                if (e.button == PointerButton::Left && pressed_)
                {
                    set_state(inside, false);
                    if (inside)
                    {
                        invoke();
                    }
                    return true;
                }
                return false;
            case PointerAction::Wheel:
                return false;
        }
        return false;
    }
    bool key_override(const KeyEvent& e) override
    {
        if (e.action == KeyAction::Down && e.modifiers.none() && (e.key == Key::Enter || e.key == Key::Space))
        {
            invoke();
            return true;
        }
        return false;
    }

private:
    void automation_providers(AutomationProviders& out) override
    {
        out.invoke = this;
    }
    bool invoke() override
    {
        if (!available())
        {
            return false;
        }
        switch (kind_)
        {
            case Kind::Heading:
                if (owner_.look_->kind == CalendarViewControl::Look::Kind::Classic)
                {
                    owner_.open_month_menu(Point{bounds().x, bounds().bottom()});
                }
                else
                {
                    owner_.zoom(true);
                }
                break;
            case Kind::Previous:
                owner_.page(-1);
                break;
            case Kind::Next:
                owner_.page(1);
                break;
        }
        return true;
    }
    void set_state(bool hovered, bool pressed)
    {
        if (hovered == hovered_ && pressed == pressed_)
        {
            return;
        }
        hovered_ = hovered;
        pressed_ = pressed;
        owner_.mark_dirty(DirtyFlags::Visual);
    }

    CalendarViewControl& owner_;
    Kind                 kind_;
    bool                 hovered_ = false;
    bool                 pressed_ = false;
};

// One cell as automation sees it: a DataItem a client can select (a day), or a Button it can
// invoke (a month or a year).
class CalendarCellAutomation final :
    public Element,
    private SelectionItemProvider,
    private InvokeProvider,
    private GridItemProvider,
    private TableItemProvider
{
public:
    CalendarCellAutomation(CalendarViewControl& owner, std::size_t index) : owner_(owner), index_(index)
    {
        set_parent_of(*this, &owner);
    }

    void place()
    {
        if (const auto* cell = this->cell())
        {
            LayoutAccess::set_bounds(*this, cell->bounds);
        }
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return owner_.display_mode() == CalendarViewDisplayMode::Month ? AutomationRole::DataItem : AutomationRole::Button;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        const auto* cell = this->cell();
        return cell ? owner_.cell_name(*cell, owner_.display_mode()) : std::string{};
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

private:
    [[nodiscard]] const CalendarViewControl::Cell* cell() const
    {
        owner_.ensure_cells();
        if (index_ >= owner_.cells_.size())
        {
            return nullptr;
        }
        return &owner_.cells_[index_];
    }
    void automation_providers(AutomationProviders& out) override
    {
        if (owner_.display_mode() == CalendarViewDisplayMode::Month)
        {
            out.selection_item = this;
            out.table_item     = this;
        }
        else
        {
            out.invoke = this;
        }
        out.grid_item = this;
    }
    [[nodiscard]] bool item_selected() const override
    {
        const auto* cell = this->cell();
        return cell && owner_.is_selected(cell->date);
    }
    bool select_item() override
    {
        const auto* cell = this->cell();
        if (!cell || owner_.selection_mode() == CalendarViewSelectionMode::None)
        {
            return false;
        }
        if (!owner_.is_selected(cell->date))
        {
            owner_.toggle(cell->date);
        }
        return owner_.is_selected(cell->date);
    }
    bool remove_from_selection() override
    {
        const auto* cell = this->cell();
        if (!cell || !owner_.is_selected(cell->date))
        {
            return false;
        }
        owner_.toggle(cell->date);
        return true;
    }
    bool invoke() override
    {
        const auto* cell = this->cell();
        if (!cell || !cell->in_range)
        {
            return false;
        }
        owner_.activate_cell(*cell);
        return true;
    }
    [[nodiscard]] std::size_t grid_item_row() const override
    {
        return index_ / owner_.grid_column_count();
    }
    [[nodiscard]] std::size_t grid_item_column() const override
    {
        return index_ % owner_.grid_column_count();
    }
    [[nodiscard]] Element* grid_item_container() const override
    {
        return &owner_;
    }
    void table_item_column_headers(std::vector<Element*>& out) const override
    {
        std::vector<Element*> headers;
        owner_.table_column_headers(headers);
        if (grid_item_column() < headers.size())
        {
            out.push_back(headers[grid_item_column()]);
        }
    }

    CalendarViewControl& owner_;
    std::size_t          index_;
};

// A weekday column's heading as automation sees it: its full name.
class CalendarWeekdayAutomation final : public Element
{
public:
    CalendarWeekdayAutomation(CalendarViewControl& owner, std::string name) : name_(std::move(name))
    {
        set_parent_of(*this, &owner);
    }
    void set_name(std::string name, const Rect& bounds)
    {
        name_ = std::move(name);
        LayoutAccess::set_bounds(*this, bounds);
    }
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::HeaderItem;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return name_;
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

private:
    std::string name_;
};

// Windows 9x's year field: the year in an edit's field edge, with an up-down control at its right.
// Up and Down or the buttons step the year, a held button repeats, and four typed digits pick one.
class CalendarYearSpin final : public Element, private RangeValueProvider
{
public:
    explicit CalendarYearSpin(CalendarViewControl& owner) : owner_(owner)
    {
        set_parent_of(*this, &owner);
    }

    [[nodiscard]] bool focusable() const override
    {
        return owner_.is_enabled();
    }
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Edit;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return strings().date_time.year;
    }
    // The selected text shows the focus, as in an edit control.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return false;
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
    {
        return available;
    }
    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme&        theme   = context.theme;
        const bool          enabled = owner_.is_enabled();
        const ButtonVisual& normal  = theme.button_normal;
        const Rect          f       = field();
        renderer.fill_rounded_rect(f, 0.0f, enabled ? theme.colors().field : theme.colors().background);
        draw_bevel_border(renderer,
                          f,
                          BorderStyle::Sunken,
                          normal.bevel_light,
                          normal.bevel_highlight,
                          normal.bevel_shadow,
                          normal.bevel_dark_shadow,
                          1.0f);

        // The text 2 px in from the edge; while focused it is a selected run.
        const std::string text  = typed_.empty() ? std::to_string(year()) : typed_;
        TextStyle         style = theme.text_style;
        style.horizontal        = TextAlign::Leading;
        style.vertical          = TextAlign::Center;
        const Rect label{f.x + 4.0f, f.y + 2.0f, std::max(0.0f, f.width - 6.0f), std::max(0.0f, f.height - 4.0f)};
        Color      color = enabled ? theme.colors().field_text : theme.colors().text_disabled;
        if (is_focused() && enabled && typed_.empty())
        {
            const float w = std::round(owner_.look_->digit_width * static_cast<float>(text.size())) + 1.0f;
            renderer.fill_rounded_rect(Rect{label.x - 1.0f, label.y, w, label.height}, 0.0f, theme.colors().selection);
            color = theme.colors().selection_text;
        }
        renderer.draw_text(text, label, color, style);

        // The up-down: two raised buttons, the pressed one sunken, each with a small arrow.
        const auto button = [&](const Rect& r, bool up)
        {
            const bool          down = pressed_ == (up ? 1 : -1);
            const bool          can  = enabled && (up ? year() < max_year() : year() > min_year());
            const ButtonVisual& v    = !can ? theme.button_disabled : down ? theme.button_pressed : normal;
            renderer.fill_rounded_rect(r, 0.0f, v.background);
            draw_bevel_border(renderer,
                              r,
                              down ? BorderStyle::Sunken : BorderStyle::Raised,
                              v.bevel_light,
                              v.bevel_highlight,
                              v.bevel_shadow,
                              v.bevel_dark_shadow,
                              1.0f,
                              BevelSides::All,
                              !down);
            const Rect arrow = down ? Rect{r.x + 1.0f, r.y + 1.0f, r.width, r.height} : r;
            draw_classic_arrow(renderer, arrow, up ? ArrowDirection::Up : ArrowDirection::Down, v.text, 3.0f);
        };
        button(up_button(), true);
        button(down_button(), false);
    }
    bool pointer_override(const PointerEvent& e) override
    {
        if (!owner_.is_enabled())
        {
            return false;
        }
        switch (e.action)
        {
            case PointerAction::Down:
                if (e.button != PointerButton::Left || !bounds().contains(e.position))
                {
                    return false;
                }
                (void)focus();
                if (up_button().contains(e.position) || down_button().contains(e.position))
                {
                    pressed_ = up_button().contains(e.position) ? 1 : -1;
                    held_ms_ = 0.0;
                    step(pressed_);
                }
                owner_.mark_dirty(DirtyFlags::Visual);
                return true;
            case PointerAction::Up:
                if (e.button == PointerButton::Left && pressed_ != 0)
                {
                    pressed_ = 0;
                    owner_.mark_dirty(DirtyFlags::Visual);
                    return true;
                }
                return false;
            case PointerAction::Wheel:
                if (is_focused() && e.wheel_delta != 0.0f && !e.horizontal_wheel)
                {
                    step(e.wheel_delta > 0.0f ? 1 : -1);
                    return true;
                }
                return false;
            default:
                return false;
        }
    }
    bool key_override(const KeyEvent& e) override
    {
        if (e.action != KeyAction::Down || !e.modifiers.none() || !owner_.is_enabled())
        {
            return false;
        }
        switch (e.key)
        {
            case Key::ArrowUp:
                step(1);
                return true;
            case Key::ArrowDown:
                step(-1);
                return true;
            case Key::Backspace:
                if (!typed_.empty())
                {
                    typed_.pop_back();
                    owner_.mark_dirty(DirtyFlags::Visual);
                }
                return true;
            case Key::Escape:
                if (typed_.empty())
                {
                    return false;
                }
                typed_.clear();
                owner_.mark_dirty(DirtyFlags::Visual);
                return true;
            case Key::Enter:
                commit();
                return true;
            default:
                return false;
        }
    }
    bool text_override(const TextInputEvent& e) override
    {
        if (!owner_.is_enabled() || e.text.size() != 1 || e.text[0] < '0' || e.text[0] > '9')
        {
            return false;
        }
        typed_ += e.text;
        if (typed_.size() == 4)
        {
            commit();
        }
        owner_.mark_dirty(DirtyFlags::Visual);
        return true;
    }
    void focus_changed(bool focused) override
    {
        if (!focused)
        {
            commit();
            pressed_ = 0;
        }
        owner_.mark_dirty(DirtyFlags::Visual);
    }
    void tick_override(Duration delta) override
    {
        if (pressed_ == 0)
        {
            return;
        }
        // A held button repeats after half a second, ten times a second.
        held_ms_ += delta.count();
        while (held_ms_ >= 500.0)
        {
            step(pressed_);
            held_ms_ -= 100.0;
        }
        request_tick();
    }

private:
    [[nodiscard]] Rect field() const
    {
        const Rect b = bounds();
        return Rect{b.x, b.y, std::max(0.0f, b.width - SpinWidth), b.height};
    }
    [[nodiscard]] Rect up_button() const
    {
        const Rect  b    = bounds();
        const float half = std::floor(b.height * 0.5f);
        return Rect{b.right() - SpinWidth, b.y, SpinWidth, half};
    }
    [[nodiscard]] Rect down_button() const
    {
        const Rect b  = bounds();
        const Rect up = up_button();
        return Rect{up.x, up.bottom(), SpinWidth, b.bottom() - up.bottom()};
    }
    [[nodiscard]] int year() const
    {
        return static_cast<int>(owner_.display_.year());
    }
    [[nodiscard]] int min_year() const
    {
        return static_cast<int>(owner_.min_date().year());
    }
    [[nodiscard]] int max_year() const
    {
        return static_cast<int>(owner_.max_date().year());
    }
    void step(int delta)
    {
        typed_.clear();
        owner_.set_display_year(std::clamp(year() + delta, min_year(), max_year()));
    }
    void commit()
    {
        if (typed_.empty())
        {
            return;
        }
        const int typed = std::stoi(typed_);
        typed_.clear();
        owner_.set_display_year(std::clamp(typed, min_year(), max_year()));
        owner_.mark_dirty(DirtyFlags::Visual);
    }

    void automation_providers(AutomationProviders& out) override
    {
        out.range_value = this;
    }
    [[nodiscard]] double range_value() const override
    {
        return static_cast<double>(year());
    }
    [[nodiscard]] double range_minimum() const override
    {
        return static_cast<double>(min_year());
    }
    [[nodiscard]] double range_maximum() const override
    {
        return static_cast<double>(max_year());
    }
    [[nodiscard]] double range_small_change() const override
    {
        return 1.0;
    }
    [[nodiscard]] bool range_read_only() const override
    {
        return !owner_.is_enabled();
    }
    bool set_range_value(double value) override
    {
        if (!owner_.is_enabled())
        {
            return false;
        }
        owner_.set_display_year(std::clamp(static_cast<int>(std::lround(value)), min_year(), max_year()));
        return true;
    }

    static constexpr float SpinWidth = 16.0f;

    CalendarViewControl& owner_;
    std::string          typed_;
    int                  pressed_ = 0; // +1 the up button, -1 the down button
    double               held_ms_ = 0.0;
};

} // namespace detail

// ---- construction and state ----

CalendarViewControl::CalendarViewControl() :
    look_(std::make_unique<Look>()),
    header_(std::make_unique<detail::CalendarPart>(*this, detail::CalendarPart::Kind::Heading)),
    previous_(std::make_unique<detail::CalendarPart>(*this, detail::CalendarPart::Kind::Previous)),
    next_(std::make_unique<detail::CalendarPart>(*this, detail::CalendarPart::Kind::Next))
{
    display_        = today();
    focus_date_     = display_;
    format_changed_ = regional_date_format_changed().observe(
        [this]
        {
            cells_valid_ = false;
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        });
}

CalendarViewControl::~CalendarViewControl() = default;

year_month_day CalendarViewControl::today() const
{
    return today_.value_or(ui::today());
}

void CalendarViewControl::set_today(std::optional<year_month_day> day)
{
    if (day == today_)
    {
        return;
    }
    today_       = day;
    cells_valid_ = false;
    mark_dirty(DirtyFlags::Visual);
}

year_month_day CalendarViewControl::min_date() const
{
    return min_.value_or(
        year_month_day{today().year() - std::chrono::years{CalendarYearsEachWay}, std::chrono::January, std::chrono::day{1}});
}

year_month_day CalendarViewControl::max_date() const
{
    return max_.value_or(year_month_day{today().year() + std::chrono::years{CalendarYearsEachWay},
                                        std::chrono::December,
                                        std::chrono::day{31}});
}

year_month_day CalendarViewControl::clamp(year_month_day date) const
{
    if (!date.ok())
    {
        return today();
    }
    const year_month_day lo = min_date();
    const year_month_day hi = std::max(lo, max_date());
    return sys_days{date} < sys_days{lo} ? lo : (sys_days{date} > sys_days{hi} ? hi : date);
}

void CalendarViewControl::set_date_range(std::optional<year_month_day> min, std::optional<year_month_day> max)
{
    if (min == min_ && max == max_)
    {
        return;
    }
    min_         = min;
    max_         = max;
    display_     = clamp(display_);
    focus_date_  = clamp(focus_date_);
    cells_valid_ = false;
    mark_dirty(DirtyFlags::Visual);
    set_selected_dates(selected_);
}

weekday CalendarViewControl::first_day_of_week() const
{
    return first_day_.value_or(regional_date_format().first_day_of_week);
}

void CalendarViewControl::set_first_day_of_week(std::optional<weekday> day)
{
    if (day == first_day_)
    {
        return;
    }
    first_day_   = day;
    cells_valid_ = false;
    mark_dirty(DirtyFlags::Visual);
}

void CalendarViewControl::set_today_highlighted(bool highlighted)
{
    if (highlighted != today_highlighted_)
    {
        today_highlighted_ = highlighted;
        mark_dirty(DirtyFlags::Visual);
    }
}

void CalendarViewControl::set_out_of_scope_enabled(bool enabled)
{
    if (enabled != out_of_scope_enabled_)
    {
        out_of_scope_enabled_ = enabled;
        mark_dirty(DirtyFlags::Visual);
    }
}

void CalendarViewControl::set_group_label_visible(bool visible)
{
    if (visible != group_label_visible_)
    {
        group_label_visible_ = visible;
        mark_dirty(DirtyFlags::Visual);
    }
}

void CalendarViewControl::set_blackout(Callback<bool(year_month_day)> is_blackout)
{
    blackout_ = std::move(is_blackout);
    mark_dirty(DirtyFlags::Visual);
    set_selected_dates(selected_);
}

bool CalendarViewControl::is_blackout(year_month_day date) const
{
    return blackout_ && const_cast<Callback<bool(year_month_day)>&>(blackout_)(date); // NOLINT: a predicate
}

bool CalendarViewControl::is_selected(year_month_day date) const
{
    return std::find(selected_.begin(), selected_.end(), date) != selected_.end();
}

void CalendarViewControl::notify_selection(std::vector<year_month_day> previous)
{
    if (previous == selected_)
    {
        return;
    }
    mark_dirty(DirtyFlags::Visual);
    if (on_selected_dates_changed_)
    {
        on_selected_dates_changed_(selected_);
    }
}

void CalendarViewControl::set_selected_dates(std::vector<year_month_day> dates)
{
    std::vector<year_month_day> kept;
    if (selection_mode_ != CalendarViewSelectionMode::None)
    {
        const year_month_day lo = min_date();
        const year_month_day hi = max_date();
        for (const year_month_day& date : dates)
        {
            if (!date.ok() || sys_days{date} < sys_days{lo} || sys_days{date} > sys_days{hi} || is_blackout(date) ||
                std::find(kept.begin(), kept.end(), date) != kept.end())
            {
                continue;
            }
            kept.push_back(date);
            if (selection_mode_ == CalendarViewSelectionMode::Single)
            {
                break;
            }
        }
    }
    std::vector<year_month_day> previous = std::exchange(selected_, std::move(kept));
    notify_selection(std::move(previous));
}

void CalendarViewControl::set_selection_mode(CalendarViewSelectionMode mode)
{
    if (mode == selection_mode_)
    {
        return;
    }
    selection_mode_ = mode;
    set_selected_dates(selected_);
}

void CalendarViewControl::toggle(year_month_day date)
{
    if (selection_mode_ == CalendarViewSelectionMode::None || is_blackout(date) || clamp(date) != date)
    {
        return;
    }
    std::vector<year_month_day> previous = selected_;
    if (const auto it = std::find(selected_.begin(), selected_.end(), date); it != selected_.end())
    {
        selected_.erase(it);
    }
    else if (selection_mode_ == CalendarViewSelectionMode::Single)
    {
        selected_ = {date};
    }
    else
    {
        selected_.push_back(date);
    }
    notify_selection(std::move(previous));
}

// ---- what is shown ----

bool CalendarViewControl::same_scope(year_month_day a, year_month_day b, CalendarViewDisplayMode mode) const
{
    switch (mode)
    {
        case CalendarViewDisplayMode::Month:
            return a.year() == b.year() && a.month() == b.month();
        case CalendarViewDisplayMode::Year:
            return a.year() == b.year();
        case CalendarViewDisplayMode::Decade:
            return calendar_decade_start(static_cast<int>(a.year())) == calendar_decade_start(static_cast<int>(b.year()));
    }
    return false;
}

bool CalendarViewControl::applet() const
{
    return look_->kind == Look::Kind::Applet;
}

void CalendarViewControl::show_month(year_month_day first)
{
    // The keyboard keeps its day in the month shown, or goes to its first day.
    const year_month_day target = clamp(first);
    const int            months = (static_cast<int>(target.year()) - static_cast<int>(focus_date_.year())) * 12 +
                                  static_cast<int>(static_cast<unsigned>(target.month())) -
                                  static_cast<int>(static_cast<unsigned>(focus_date_.month()));
    show_scope(target, 0);
    focus_date_ = clamp(calendar_add_months(focus_date_, months));
    if (!same_scope(focus_date_, display_, display_mode_))
    {
        focus_date_ = display_;
    }
    mark_dirty(DirtyFlags::Visual);
}

void CalendarViewControl::set_display_year(int y)
{
    show_month(year_month_day{year{y}, display_.month(), std::chrono::day{1}});
}

void CalendarViewControl::sync_applet_parts()
{
    if (!month_list_)
    {
        return;
    }
    syncing_parts_                 = true;
    const auto&              names = regional_date_format().month_names;
    std::vector<std::string> items(names.begin(), names.end());
    if (month_list_->items() != items)
    {
        month_list_->set_items(std::move(items));
    }
    month_list_->set_selected_index(static_cast<unsigned>(display_.month()) - 1u);
    syncing_parts_ = false;
}

void CalendarViewControl::show_scope(year_month_day date, int slide_direction)
{
    date = clamp(date);
    if (same_scope(date, display_, display_mode_))
    {
        display_ = date;
        return;
    }
    if (slide_direction != 0 && animations_enabled() && !look_->comctl())
    {
        transition_      = Transition::Slide;
        transition_ms_   = 0.0;
        slide_direction_ = slide_direction;
        from_mode_       = display_mode_;
        from_scope_      = display_;
    }
    display_     = date;
    cells_valid_ = false;
    hover_cell_.reset();
    pressed_cell_.reset();
    mark_dirty(DirtyFlags::Visual);
    if (applet())
    {
        sync_applet_parts();
    }
}

void CalendarViewControl::set_display_date(year_month_day date)
{
    show_scope(date, 0);
    if (!same_scope(focus_date_, display_, display_mode_))
    {
        focus_date_ = display_;
    }
}

void CalendarViewControl::set_display_mode(CalendarViewDisplayMode mode)
{
    if (mode == display_mode_ || (applet() && mode != CalendarViewDisplayMode::Month))
    {
        return;
    }
    const bool out = static_cast<int>(mode) > static_cast<int>(display_mode_);
    if (animations_enabled() && !look_->comctl())
    {
        transition_    = Transition::Zoom;
        transition_ms_ = 0.0;
        zoom_out_      = out;
        from_mode_     = display_mode_;
        from_scope_    = display_;
    }
    display_mode_ = mode;
    display_ = focus_date_ = clamp(focus_date_);
    cells_valid_           = false;
    hover_cell_.reset();
    pressed_cell_.reset();
    mark_dirty(DirtyFlags::Visual);
    if (on_display_mode_changed_)
    {
        on_display_mode_changed_(mode);
    }
}

void CalendarViewControl::zoom(bool out)
{
    if (out && display_mode_ != CalendarViewDisplayMode::Decade)
    {
        set_display_mode(static_cast<CalendarViewDisplayMode>(static_cast<int>(display_mode_) + 1));
    }
    else if (!out && display_mode_ != CalendarViewDisplayMode::Month)
    {
        set_display_mode(static_cast<CalendarViewDisplayMode>(static_cast<int>(display_mode_) - 1));
    }
}

void CalendarViewControl::page(int direction)
{
    year_month_day target = focus_date_;
    switch (display_mode_)
    {
        case CalendarViewDisplayMode::Month:
            target = calendar_add_months(display_, direction);
            break;
        case CalendarViewDisplayMode::Year:
            target = calendar_add_years(display_, direction);
            break;
        case CalendarViewDisplayMode::Decade:
            target = calendar_add_years(display_, direction * 10);
            break;
    }
    if ((direction < 0 && !has_more_before()) || (direction > 0 && !has_more_after()))
    {
        return;
    }
    show_scope(target, direction);
    // The keyboard follows to the same place in the new page.
    switch (display_mode_)
    {
        case CalendarViewDisplayMode::Month:
            focus_date_ = clamp(calendar_add_months(focus_date_, direction));
            break;
        case CalendarViewDisplayMode::Year:
            focus_date_ = clamp(calendar_add_years(focus_date_, direction));
            break;
        case CalendarViewDisplayMode::Decade:
            focus_date_ = clamp(calendar_add_years(focus_date_, direction * 10));
            break;
    }
    if (!same_scope(focus_date_, display_, display_mode_))
    {
        focus_date_ = display_;
    }
}

bool CalendarViewControl::has_more_before() const
{
    const year_month_day lo = min_date();
    switch (display_mode_)
    {
        case CalendarViewDisplayMode::Month:
            return sys_days{calendar_first_of_month(display_)} > sys_days{lo};
        case CalendarViewDisplayMode::Year:
            return display_.year() > lo.year();
        case CalendarViewDisplayMode::Decade:
            return calendar_decade_start(static_cast<int>(display_.year())) > static_cast<int>(lo.year());
    }
    return false;
}

bool CalendarViewControl::has_more_after() const
{
    const year_month_day hi = max_date();
    switch (display_mode_)
    {
        case CalendarViewDisplayMode::Month:
            return display_.year() < hi.year() || (display_.year() == hi.year() && display_.month() < hi.month());
        case CalendarViewDisplayMode::Year:
            return display_.year() < hi.year();
        case CalendarViewDisplayMode::Decade:
            return calendar_decade_start(static_cast<int>(display_.year())) + 9 < static_cast<int>(hi.year());
    }
    return false;
}

void CalendarViewControl::move_focus(year_month_day date)
{
    date = clamp(date);
    if (!same_scope(date, display_, display_mode_))
    {
        const int direction = sys_days{date} < sys_days{display_} ? -1 : 1;
        show_scope(date, direction);
    }
    focus_date_ = date;
    mark_dirty(DirtyFlags::Visual);
}

void CalendarViewControl::activate_cell(const Cell& cell)
{
    if (!cell.in_range)
    {
        return;
    }
    if (display_mode_ == CalendarViewDisplayMode::Month)
    {
        focus_date_ = cell.date;
        if (!same_scope(cell.date, display_, display_mode_))
        {
            show_scope(cell.date, sys_days{cell.date} < sys_days{display_} ? -1 : 1);
        }
        toggle(cell.date);
        mark_dirty(DirtyFlags::Visual);
        return;
    }
    focus_date_ = clamp(cell.date);
    display_    = focus_date_;
    zoom(false);
}

std::string CalendarViewControl::heading(CalendarViewDisplayMode mode, year_month_day scope) const
{
    switch (mode)
    {
        case CalendarViewDisplayMode::Month:
            return format_date(calendar_first_of_month(scope), regional_date_format().year_month_pattern);
        case CalendarViewDisplayMode::Year:
            return std::to_string(static_cast<int>(scope.year()));
        case CalendarViewDisplayMode::Decade:
        {
            const int first = calendar_decade_start(static_cast<int>(scope.year()));
            return std::to_string(first) + (look_->comctl() ? "-" : " - ") + std::to_string(first + 9);
        }
    }
    return {};
}

std::string CalendarViewControl::cell_text(const Cell& cell, CalendarViewDisplayMode mode) const
{
    switch (mode)
    {
        case CalendarViewDisplayMode::Month:
            return std::to_string(static_cast<unsigned>(cell.date.day()));
        case CalendarViewDisplayMode::Year:
            return regional_date_format().abbreviated_month_names[static_cast<unsigned>(cell.date.month()) - 1];
        case CalendarViewDisplayMode::Decade:
            return std::to_string(static_cast<int>(cell.date.year()));
    }
    return {};
}

std::string CalendarViewControl::cell_name(const Cell& cell, CalendarViewDisplayMode mode) const
{
    std::string          name = mode == CalendarViewDisplayMode::Year
                                    ? regional_date_format().month_names[static_cast<unsigned>(cell.date.month()) - 1]
                                    : cell_text(cell, mode);
    const year_month_day now  = today();
    const bool is_today       = mode == CalendarViewDisplayMode::Month
                                    ? cell.date == now
                                    : same_scope(cell.date,
                                                 now,
                                                 mode == CalendarViewDisplayMode::Year ? CalendarViewDisplayMode::Month
                                                                                       : CalendarViewDisplayMode::Year);
    if (is_today && today_highlighted_)
    {
        name = detail::fill(strings().date_time.today, {name});
    }
    return name;
}

Rect CalendarViewControl::grid_rect() const
{
    const Rect& g = look_->month_grid;
    return Rect{bounds().x + g.x, bounds().y + g.y, g.width, g.height};
}

Rect CalendarViewControl::footer_rect() const
{
    const Rect& f = look_->footer;
    return Rect{bounds().x + f.x, bounds().y + f.y, f.width, f.height};
}

std::vector<CalendarViewControl::Cell> CalendarViewControl::cells(CalendarViewDisplayMode mode,
                                                                  year_month_day          scope,
                                                                  const Rect&             month_grid) const
{
    const Look&          l  = *look_;
    const year_month_day lo = min_date();
    const year_month_day hi = max_date();
    std::vector<Cell>    out;
    if (mode == CalendarViewDisplayMode::Month)
    {
        const year_month_day first   = calendar_first_of_month(scope);
        const unsigned       offset  = (weekday{sys_days{first}} - first_day_of_week()).count();
        const sys_days       start   = sys_days{first} - days{offset};
        const float          pitch_x = month_grid.width / static_cast<float>(CalendarColumns);
        const float          pitch_y = month_grid.height / static_cast<float>(CalendarWeeks);
        for (int i = 0; i < CalendarColumns * CalendarWeeks; ++i)
        {
            Cell cell;
            cell.date       = year_month_day{start + days{i}};
            cell.bounds     = Rect{month_grid.x + pitch_x * static_cast<float>(i % CalendarColumns),
                                   month_grid.y + pitch_y * static_cast<float>(i / CalendarColumns),
                                   pitch_x,
                                   pitch_y};
            cell.in_scope   = cell.date.month() == scope.month() && cell.date.year() == scope.year();
            cell.in_range   = sys_days{cell.date} >= sys_days{lo} && sys_days{cell.date} <= sys_days{hi};
            cell.group_head = cell.date.day() == std::chrono::day{1};
            out.push_back(cell);
        }
        return out;
    }
    // The year and decade views take the weekday row's room as well. The comctl32 designs show
    // a 4 x 3 grid, a decade with the year before and after it.
    const Rect  area{month_grid.x - l.month_grid.x + l.zoom_area.x,
                     month_grid.y - l.month_grid.y + l.zoom_area.y,
                     l.zoom_area.width,
                     l.zoom_area.height};
    const int   rows    = l.zoom_rows;
    const float pitch_x = area.width / static_cast<float>(CalendarZoomCells);
    const float pitch_y = area.height / static_cast<float>(rows);
    const int   y       = static_cast<int>(scope.year());
    const int   lead    = mode == CalendarViewDisplayMode::Decade && l.comctl() ? 1 : 0;
    const int   first   = mode == CalendarViewDisplayMode::Year ? y : calendar_decade_start(y) - lead;
    for (int i = 0; i < CalendarZoomCells * rows; ++i)
    {
        Cell cell;
        cell.bounds = Rect{area.x + pitch_x * static_cast<float>(i % CalendarZoomCells),
                           area.y + pitch_y * static_cast<float>(i / CalendarZoomCells),
                           pitch_x,
                           pitch_y};
        if (mode == CalendarViewDisplayMode::Year)
        {
            const std::chrono::year_month ym = std::chrono::year_month{year{first}, std::chrono::January} +
                                               std::chrono::months{i};
            cell.date                        = year_month_day{ym.year(), ym.month(), std::chrono::day{1}};
            cell.in_scope                    = static_cast<int>(ym.year()) == first;
            cell.group_head                  = ym.month() == std::chrono::January;
            const auto last = std::chrono::year_month_day_last{ym.year(), std::chrono::month_day_last{ym.month()}};
            cell.in_range   = sys_days{year_month_day{last}} >= sys_days{lo} && sys_days{cell.date} <= sys_days{hi};
        }
        else
        {
            cell.date     = year_month_day{year{first + i}, std::chrono::January, std::chrono::day{1}};
            cell.in_scope = i >= lead && i < lead + 10;
            cell.in_range = first + i >= static_cast<int>(lo.year()) && first + i <= static_cast<int>(hi.year());
        }
        out.push_back(cell);
    }
    return out;
}

void CalendarViewControl::cached_cells_dirty()
{
    cells_valid_ = false;
}

void CalendarViewControl::ensure_cells() const
{
    if (cells_valid_)
    {
        return;
    }
    cells_       = cells(display_mode_, display_, grid_rect());
    cells_valid_ = true;
    for (auto& node : cell_nodes_)
    {
        node->place();
    }
}

std::optional<std::size_t> CalendarViewControl::cell_at(Point p) const
{
    ensure_cells();
    for (std::size_t i = 0; i < cells_.size(); ++i)
    {
        if (cells_[i].in_range && (cells_[i].in_scope || !applet()) && cells_[i].bounds.contains(p))
        {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> CalendarViewControl::focused_cell() const
{
    ensure_cells();
    for (std::size_t i = 0; i < cells_.size(); ++i)
    {
        const year_month_day d     = cells_[i].date;
        const bool           match = display_mode_ == CalendarViewDisplayMode::Month ? d == focus_date_
                                     : display_mode_ == CalendarViewDisplayMode::Year
                                         ? d.year() == focus_date_.year() && d.month() == focus_date_.month()
                                         : d.year() == focus_date_.year();
        if (match && (cells_[i].in_scope || display_mode_ != CalendarViewDisplayMode::Month))
        {
            return i;
        }
    }
    return std::nullopt;
}

// ---- layout ----

Size CalendarViewControl::measure_override(Size, const MeasureContext& context) const
{
    *look_ = make_look(context.theme);
    if (corner_radius_override_ >= 0.0f)
    {
        look_->corner = corner_radius_override_;
    }
    if (applet())
    {
        const Size digit   = context.measurer.measure("0", context.theme.text_style);
        look_->digit_width = digit.width;
        look_->line_height = digit.height;
        if (popup_frame_)
        {
            // In a date picker's popup: the dialog face around it, 4 px in a 1 px frame.
            for (Rect* r :
                 {&look_->month_list, &look_->year_spin, &look_->box, &look_->weekday_band, &look_->month_grid, &look_->zoom_area})
            {
                r->x += 5.0f;
                r->y += 5.0f;
            }
            look_->width += 10.0f;
            look_->height += 10.0f;
        }
    }
    if (look_->kind == Look::Kind::Aero)
    {
        TextStyle         style  = calendar_cell_style(context.theme, look_->day_font, FontWeight::Regular);
        const std::string line   = detail::fill(strings().date_time.today_footer,
                                                {format_date(today(), regional_date_format().short_date_pattern)});
        look_->footer_text_width = context.measurer.measure(line, style).width;
    }
    return Size{look_->width, look_->height};
}

void CalendarViewControl::arrange_override(const Rect& final_rect, const ArrangeContext& context)
{
    const Look& l = *look_;
    const Point o{final_rect.x, final_rect.y};
    const auto  place = [&](detail::CalendarPart& part, const Rect& r)
    {
        const Rect at{o.x + r.x, o.y + r.y, r.width, r.height};
        (void)part.measure(at.size(), context.measurer, context.theme);
        part.arrange(at);
    };
    place(*header_, l.heading);
    place(*previous_, l.previous);
    place(*next_, l.next);
    if (month_menu_)
    {
        (void)month_menu_->measure(Size{}, context.measurer, context.theme);
        month_menu_->arrange(Rect{o.x + l.heading.x, o.y + l.heading.bottom(), 0.0f, 0.0f});
    }
    if (applet())
    {
        if (!month_list_)
        {
            month_list_ = std::make_unique<ComboBoxControl>();
            set_parent_of(*month_list_, this);
            month_list_->on_selection_changed(
                [this](std::optional<std::size_t> index)
                {
                    if (!syncing_parts_ && index)
                    {
                        show_month(year_month_day{display_.year(),
                                                  std::chrono::month{static_cast<unsigned>(*index) + 1u},
                                                  std::chrono::day{1}});
                    }
                });
            year_spin_ = std::make_unique<detail::CalendarYearSpin>(*this);
        }
        sync_applet_parts();
        for (Element* part : {static_cast<Element*>(month_list_.get()), static_cast<Element*>(year_spin_.get())})
        {
            const Rect& r = part == month_list_.get() ? l.month_list : l.year_spin;
            const Rect  at{o.x + r.x, o.y + r.y, r.width, r.height};
            (void)part->measure(at.size(), context.measurer, context.theme);
            part->arrange(at);
        }
    }
    cells_valid_ = false;
}

// ---- drawing ----

void CalendarViewControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    const Look&  l     = *look_;
    const Rect   b{bounds().x, bounds().y, l.width, l.height};
    const bool   enabled = is_enabled();

    ensure_cells();
    if (applet())
    {
        render_applet(renderer, theme);
        return;
    }
    if (l.comctl())
    {
        render_comctl(renderer, theme);
        return;
    }

    // The frame.
    renderer.fill_rounded_rect(b, l.corner, l.background);
    renderer.stroke_rounded_rect(b.inset(Thickness{l.border * 0.5f}),
                                 std::max(0.0f, l.corner - l.border * 0.5f),
                                 l.border_color,
                                 l.border);

    // The heading and the paging buttons.
    float heading_opacity = 1.0f;
    if (transition_ == Transition::Zoom)
    {
        heading_opacity = static_cast<float>(std::clamp(transition_ms_ / CalendarHeadingFadeMs, 0.0, 1.0));
    }
    const auto part_fill = [&](const detail::CalendarPart& part)
    {
        if (!part.available())
        {
            return;
        }
        const Color fill = part.pressed() ? l.part_pressed_fill : (part.hovered() ? l.part_hover_fill : colors::Transparent);
        if (fill.a > 0.0f)
        {
            renderer.fill_rounded_rect(part.bounds(), l.part_radius, fill);
        }
    };
    part_fill(*header_);
    part_fill(*previous_);
    part_fill(*next_);
    {
        TextStyle style  = calendar_cell_style(theme, l.heading_font, l.heading_weight);
        style.horizontal = TextAlign::Leading;
        const Rect hb    = header_->bounds();
        const Rect text{hb.x + l.heading_text_inset, hb.y, std::max(0.0f, hb.width - l.heading_text_inset), hb.height};
        Color      color = !enabled                ? l.heading_disabled_text
                           : header_->pressed()    ? l.heading_pressed_text
                           : header_->hovered()    ? l.heading_hover_text
                           : !header_->available() ? l.heading_text
                                                   : l.heading_text;
        if (heading_opacity < 1.0f)
        {
            renderer.push_opacity_layer(heading_opacity);
        }
        renderer.draw_text(heading(display_mode_, display_), text, color, style);
        if (heading_opacity < 1.0f)
        {
            renderer.pop_opacity_layer();
        }
    }
    const auto glyph = [&](const detail::CalendarPart& part, const IconGlyph& icon)
    {
        const Color color = !part.available() ? l.glyph_disabled
                            : part.pressed()  ? l.glyph_pressed
                            : part.hovered()  ? l.glyph_hover
                                              : l.glyph;
        draw_icon(renderer, icon, part.bounds(), color);
    };
    glyph(*previous_, l.up);
    glyph(*next_, l.down);

    if (l.divider > 0.0f)
    {
        renderer.fill_rounded_rect(Rect{b.x + l.border, b.y + l.border + l.header_height, l.width - 2.0f * l.border, l.divider},
                                   0.0f,
                                   l.border_color);
    }

    // The views, clipped to the area under the heading.
    const Rect views{b.x + l.border,
                     b.y + l.border + l.header_height + l.divider,
                     l.width - 2.0f * l.border,
                     l.height - 2.0f * l.border - l.header_height - l.divider};
    renderer.push_clip(views);

    const Rect month_grid = grid_rect();
    const auto draw_view  = [&](CalendarViewDisplayMode mode, year_month_day scope, Point offset, bool interactive)
    {
        if (offset.x != 0.0f || offset.y != 0.0f)
        {
            renderer.push_transform(1.0f, offset);
        }
        if (mode == CalendarViewDisplayMode::Month)
        {
            // The weekday names.
            const unsigned first = first_day_of_week().c_encoding();
            const float    pitch = month_grid.width / static_cast<float>(CalendarColumns);
            TextStyle      style = calendar_cell_style(theme, l.weekday_font, l.weekday_weight);
            for (unsigned i = 0; i < CalendarColumns; ++i)
            {
                const Rect r{month_grid.x + pitch * static_cast<float>(i),
                             month_grid.y - l.month_grid.y + l.weekday_band.y,
                             pitch,
                             l.weekday_band.height};
                renderer.draw_text(regional_date_format().shortest_day_names[(first + i) % 7],
                                   r,
                                   enabled ? l.weekday_text : l.weekday_disabled,
                                   style);
            }
        }
        const std::vector<Cell>          view_cells = interactive ? cells_ : cells(mode, scope, month_grid);
        const year_month_day             now        = today();
        const std::optional<std::size_t> focus      = interactive ? focused_cell() : std::nullopt;
        for (std::size_t i = 0; i < view_cells.size(); ++i)
        {
            const Cell& cell = view_cells[i];
            if (!cell.in_range)
            {
                continue;
            }
            const bool hovered      = interactive && hover_cell_ == i;
            const bool pressed      = interactive && pressed_cell_ == i;
            const bool selected     = mode == CalendarViewDisplayMode::Month && is_selected(cell.date);
            const bool blackout     = mode == CalendarViewDisplayMode::Month && is_blackout(cell.date);
            const bool is_today     = today_highlighted_ &&
                                      (mode == CalendarViewDisplayMode::Month ? cell.date == now
                                       : mode == CalendarViewDisplayMode::Year
                                           ? cell.date.year() == now.year() && cell.date.month() == now.month()
                                           : cell.date.year() == now.year());
            const bool out_of_scope = out_of_scope_enabled_ && !cell.in_scope;
            (void)focus;

            // The item: the cell less its margin, a circle in Fluent 2.
            const Rect item   = cell.bounds.inset(Thickness{l.item_margin});
            Rect       shape  = item;
            float      radius = 0.0f;
            if (l.rounded)
            {
                if (mode == CalendarViewDisplayMode::Month)
                {
                    const float d = std::min(item.width, item.height);
                    shape         = Rect{item.center().x - d * 0.5f, item.center().y - d * 0.5f, d, d};
                }
                else
                {
                    // The year and decade items shrink by 9 on each side, then square up.
                    const float d = std::max(0.0f, std::min(item.width, item.height) - 2.0f * 8.0f);
                    shape         = Rect{item.center().x - d * 0.5f, item.center().y - d * 0.5f, d, d};
                }
                radius = shape.width * 0.5f;
            }

            Color fill = colors::Transparent;
            Color ring = colors::Transparent;
            Color text = l.item_text;
            if (!enabled)
            {
                text = is_today ? l.today_text : l.disabled_text;
                fill = is_today ? l.today_disabled_fill : colors::Transparent;
            }
            else if (is_today)
            {
                fill = pressed ? l.today_pressed_fill : (hovered ? l.today_hover_fill : l.today_fill);
                text = l.today_text;
                ring = pressed ? l.today_pressed_ring : (hovered ? l.today_hover_ring : colors::Transparent);
            }
            else
            {
                if (l.rounded)
                {
                    fill = pressed ? l.item_pressed_fill : (hovered ? l.item_hover_fill : colors::Transparent);
                }
                else if (out_of_scope)
                {
                    fill = l.out_of_scope_fill;
                }
                if (selected)
                {
                    ring = pressed ? l.selected_pressed_ring : (hovered ? l.selected_hover_ring : l.selected_ring);
                    text = pressed ? l.selected_pressed_text : (hovered ? l.selected_hover_text : l.selected_text);
                }
                else
                {
                    ring = pressed ? l.pressed_ring : (hovered ? l.hover_ring : colors::Transparent);
                    if (out_of_scope)
                    {
                        text = pressed ? l.out_of_scope_pressed_text
                                       : (hovered ? l.out_of_scope_hover_text : l.out_of_scope_text);
                    }
                    else if (pressed)
                    {
                        text = l.item_pressed_text;
                    }
                }
                if (blackout)
                {
                    text = l.blackout_text;
                    if (l.rounded)
                    {
                        ring = colors::Transparent;
                    }
                }
            }

            if (fill.a > 0.0f)
            {
                renderer.fill_rounded_rect(shape, radius, fill);
            }
            if (ring.a > 0.0f)
            {
                const float w = l.item_border;
                renderer.stroke_rounded_rect(shape.inset(Thickness{w * 0.5f}), std::max(0.0f, radius - w * 0.5f), ring, w);
            }
            if (is_today && selected && enabled)
            {
                // Today, selected: an inner ring in the text-on-accent color.
                const float w     = l.rounded ? 1.0f : 2.0f;
                const Rect  inner = shape.inset(Thickness{l.item_border + w * 0.5f});
                renderer.stroke_rounded_rect(inner, std::max(0.0f, radius - l.item_border - w * 0.5f), l.today_selected_ring, w);
            }

            // The text: the number (or name) centered, a first-of-month label above it.
            const bool       month_mode = mode == CalendarViewDisplayMode::Month;
            const float      size       = month_mode ? l.day_font : l.zoom_font;
            const FontWeight weight     = is_today && month_mode ? l.today_weight : FontWeight::Regular;
            TextStyle        style      = calendar_cell_style(theme, size, weight);
            // Days sit a little low in their circle: 3 down from the top, 4 up from the bottom.
            Rect text_box = month_mode ? Rect{item.x, item.y + 3.0f, item.width, item.height - 7.0f} : item;
            if (!l.rounded)
            {
                text_box = item;
                if (group_label_visible_ && cell.group_head && mode != CalendarViewDisplayMode::Decade)
                {
                    text_box = Rect{item.x, item.y + l.label_font * 0.5f, item.width, item.height};
                }
            }
            renderer.draw_text(cell_text(cell, mode), text_box, text, style);
            if (blackout && l.rounded && enabled)
            {
                // A diagonal through a square 1.25 times the type size, centered.
                const float side = std::floor(l.day_font * 1.25f);
                const float x0   = std::floor(item.center().x - side * 0.5f);
                const float y0   = std::floor(item.center().y - side * 0.5f);
                renderer.stroke_line(Point{x0, y0}, Point{x0 + side, y0 + side}, is_today ? l.today_text : l.strikethrough, 1.0f);
            }
            if (group_label_visible_ && cell.group_head && mode != CalendarViewDisplayMode::Decade)
            {
                TextStyle label = calendar_cell_style(theme, l.label_font, FontWeight::Regular);
                label.vertical  = TextAlign::Leading;
                const float top = !l.rounded ? 0.0f : month_mode ? 2.0f : 9.0f;
                const Rect  label_box{item.x, item.y + top, item.width, l.label_font * 1.5f};
                const std::string label_text = month_mode
                                                   ? regional_date_format()
                                                         .abbreviated_month_names[static_cast<unsigned>(cell.date.month()) - 1]
                                                   : std::to_string(static_cast<int>(cell.date.year()));
                renderer.draw_text(label_text, label_box, text, label);
            }
        }
        if (offset.x != 0.0f || offset.y != 0.0f)
        {
            renderer.pop_transform();
        }
    };

    if (transition_ == Transition::Slide)
    {
        // The old page slides out and the new one in, one grid height apart.
        const float t = calendar_ease_out(static_cast<float>(transition_ms_ / CalendarSlideMs));
        const float travel = (display_mode_ == CalendarViewDisplayMode::Month ? month_grid.height : l.zoom_area.height) *
                             static_cast<float>(slide_direction_);
        if (display_mode_ == CalendarViewDisplayMode::Month)
        {
            // The weekday row stays put: clip the slide to the grid.
            renderer.push_clip(month_grid);
        }
        draw_view(from_mode_, from_scope_, Point{0.0f, -travel * t}, false);
        draw_view(display_mode_, display_, Point{0.0f, travel * (1.0f - t)}, true);
        if (display_mode_ == CalendarViewDisplayMode::Month)
        {
            renderer.pop_clip();
        }
    }
    else if (transition_ == Transition::Zoom)
    {
        // The old view fades and scales away in the first 150 ms, the new one scales back to
        // size and fades in by 500 ms, both about the views' center.
        const double out_ms   = l.rounded ? 150.0 : 233.0;
        const double total_ms = l.rounded ? 500.0 : 733.0;
        const Point  center   = views.center();
        const auto   scaled =
            [&](float scale, float opacity, CalendarViewDisplayMode mode, year_month_day scope, bool interactive)
        {
            if (opacity <= 0.0f)
            {
                return;
            }
            renderer.push_opacity_layer(opacity);
            renderer.push_transform(scale, Point{center.x * (1.0f - scale), center.y * (1.0f - scale)});
            draw_view(mode, scope, Point{}, interactive);
            renderer.pop_transform();
            renderer.pop_opacity_layer();
        };
        if (transition_ms_ < out_ms)
        {
            const float t = calendar_ease_out(static_cast<float>(transition_ms_ / out_ms));
            scaled(1.0f + ((zoom_out_ ? CalendarZoomSmall : CalendarZoomLarge) - 1.0f) * t,
                   1.0f - t,
                   from_mode_,
                   from_scope_,
                   false);
        }
        else
        {
            const float t     = calendar_ease_out(static_cast<float>((transition_ms_ - out_ms) / (total_ms - out_ms)));
            const float start = zoom_out_ ? CalendarZoomLarge : CalendarZoomSmall;
            scaled(start + (1.0f - start) * t, t, display_mode_, display_, true);
        }
    }
    else
    {
        draw_view(display_mode_, display_, Point{}, true);
    }
    renderer.pop_clip();
}


// ---- the comctl32 designs ----

namespace
{
// The red loop the classic month calendar circles today with, measured from Windows 2000's
// comctl32 5.81: a 26 x 17 box whose first row is one pixel above the day's cell.
constexpr std::array<const char*, 17> CalendarTodayLoop{
    "..........................",
    "..........................",
    "#################.........",
    "####################......",
    "............#...######....",
    "......#######......####...",
    "...##########........###..",
    "..#####................##.",
    ".###..................###.",
    ".##....................##.",
    "##.....................##.",
    ".##....................##.",
    ".####................####.",
    "..#####............#####..",
    "....##################....",
    "......##############......",
    "..........................",
};

void calendar_draw_loop(Renderer& renderer, Point origin, const Color& color)
{
    for (std::size_t row = 0; row < CalendarTodayLoop.size(); ++row)
    {
        const char* line = CalendarTodayLoop[row];
        for (std::size_t col = 0; line[col] != '\0'; ++col)
        {
            // Runs of set pixels as one rectangle each.
            if (line[col] != '#')
            {
                continue;
            }
            std::size_t end = col;
            while (line[end + 1] == '#')
            {
                ++end;
            }
            renderer.fill_rounded_rect(Rect{origin.x + static_cast<float>(col),
                                            origin.y + static_cast<float>(row),
                                            static_cast<float>(end - col + 1),
                                            1.0f},
                                       0.0f,
                                       color);
            col = end;
        }
    }
}

// Aero's paging glyph: a solid 4 x 7 triangle, centered.
void calendar_draw_triangle(Renderer& renderer, const Rect& area, bool left, const Color& color)
{
    const float        x      = std::round(area.center().x - 2.0f);
    const float        y      = std::round(area.center().y - 3.5f);
    std::vector<Point> points = left ? std::vector<Point>{{x + 4.0f, y}, {x + 4.0f, y + 7.0f}, {x + 0.5f, y + 3.5f}}
                                     : std::vector<Point>{{x, y}, {x, y + 7.0f}, {x + 3.5f, y + 3.5f}};
    renderer.fill_polygon(FillPolygon{std::move(points), color});
}
} // namespace

void CalendarViewControl::render_comctl(Renderer& renderer, const Theme& theme) const
{
    const Look& l = *look_;
    const Rect  b{bounds().x, bounds().y, l.width, l.height};
    const Point o{b.x, b.y};
    const bool  enabled = is_enabled();
    const bool  classic = l.kind == Look::Kind::Classic;
    const auto  at      = [&](const Rect& r) { return Rect{o.x + r.x, o.y + r.y, r.width, r.height}; };

    renderer.fill_rounded_rect(b, 0.0f, l.background);
    if (l.border > 0.0f)
    {
        renderer.stroke_rounded_rect(b.inset(Thickness{l.border * 0.5f}), 0.0f, l.border_color, l.border);
    }

    // The title: a caption band with the scroll bar's arrow buttons (classic), or plain text with
    // small triangles (Aero).
    if (classic)
    {
        renderer.fill_rounded_rect(Rect{b.x, b.y, l.width, l.header_height}, 0.0f, l.title_fill);
        const auto button = [&](const detail::CalendarPart& part, ArrowDirection direction)
        {
            const ButtonState state = part.pressed() && part.hovered() ? ButtonState::Pressed : ButtonState::Normal;
            if (!detail::draw_scroll_arrow_button(renderer, theme, part.bounds(), direction, state, part.available()))
            {
                const ButtonVisual& face = theme.for_state(ButtonState::Normal);
                renderer.fill_rounded_rect(part.bounds(), 0.0f, face.background);
                draw_bevel_border(renderer,
                                  part.bounds(),
                                  BorderStyle::Raised,
                                  face.bevel_light,
                                  face.bevel_highlight,
                                  face.bevel_shadow,
                                  face.bevel_dark_shadow,
                                  1.0f);
                draw_classic_arrow(renderer, part.bounds(), direction, face.text, 4.0f);
            }
        };
        button(*previous_, ArrowDirection::Left);
        button(*next_, ArrowDirection::Right);
    }
    else
    {
        const auto triangle = [&](const detail::CalendarPart& part, bool left)
        {
            const Color color = !part.available() ? l.glyph_disabled
                                : part.pressed()  ? l.glyph_pressed
                                : part.hovered()  ? l.glyph_hover
                                                  : l.glyph;
            calendar_draw_triangle(renderer, part.bounds(), left, color);
        };
        triangle(*previous_, true);
        triangle(*next_, false);
    }
    {
        TextStyle   style = calendar_cell_style(theme, l.heading_font, l.heading_weight);
        const Color color = classic                                      ? l.title_text
                            : !enabled                                   ? l.disabled_text
                            : header_->hovered() && header_->available() ? l.title_hover
                                                                         : l.title_text;
        renderer.draw_text(heading(display_mode_, display_), header_->bounds(), color, style);
    }

    const bool month = display_mode_ == CalendarViewDisplayMode::Month;
    if (month)
    {
        // The weekday names in their row, a rule under them.
        const unsigned first = first_day_of_week().c_encoding();
        const Rect     band  = at(l.weekday_band);
        const float    pitch = band.width / static_cast<float>(CalendarColumns);
        TextStyle      style = calendar_cell_style(theme, l.weekday_font, l.weekday_weight);
        for (unsigned i = 0; i < CalendarColumns; ++i)
        {
            renderer.draw_text(regional_date_format().abbreviated_day_names[(first + i) % 7],
                               Rect{band.x + pitch * static_cast<float>(i), band.y, pitch, band.height},
                               enabled ? l.weekday_text : l.weekday_disabled,
                               style);
        }
        renderer.fill_rounded_rect(at(l.separator), 0.0f, l.separator_color);
    }

    const year_month_day now = today();
    for (std::size_t i = 0; i < cells_.size(); ++i)
    {
        const Cell& cell = cells_[i];
        if (!cell.in_range)
        {
            continue;
        }
        const bool selected = month && is_selected(cell.date);
        const bool blackout = month && is_blackout(cell.date);
        const bool hovered  = !classic && enabled && hover_cell_ == i;
        const bool is_today = today_highlighted_ &&
                              (month ? cell.date == now
                               : display_mode_ == CalendarViewDisplayMode::Year
                                   ? cell.date.year() == now.year() && cell.date.month() == now.month()
                                   : cell.date.year() == now.year());
        const bool dimmed   = !cell.in_scope && (out_of_scope_enabled_ || !month);
        Color text = !enabled ? l.disabled_text : blackout ? l.blackout_text : dimmed ? l.trailing_text : l.item_text;

        if (classic)
        {
            if (selected && enabled)
            {
                if (l.rect_selection)
                {
                    renderer.fill_rounded_rect(cell.bounds, 0.0f, l.selected_fill);
                }
                else
                {
                    // A filled ellipse 2 px inside the cell.
                    const Rect e = cell.bounds.inset(Thickness{2.0f});
                    renderer.fill_ellipse(e.center(), e.width * 0.5f, e.height * 0.5f, l.selected_fill);
                }
                text = l.selected_text;
            }
            if (is_today && enabled)
            {
                if (l.today_frame || !month)
                {
                    renderer.stroke_rounded_rect(cell.bounds.inset(Thickness{0.5f}), 0.0f, l.today_mark, 1.0f);
                }
                else
                {
                    calendar_draw_loop(renderer, Point{cell.bounds.x, cell.bounds.y - 1.0f}, l.today_mark);
                }
            }
        }
        else
        {
            // Aero: a light blue box for the selected and the hot cell, a rounded outline for today.
            const Rect box = cell.bounds.inset(Thickness{0.5f});
            if (selected && enabled)
            {
                renderer.fill_rounded_rect(cell.bounds, 0.0f, l.selected_fill);
                renderer.stroke_rounded_rect(box, 0.0f, l.selected_ring, 1.0f);
            }
            else if (hovered)
            {
                renderer.fill_rounded_rect(cell.bounds, 0.0f, l.hover_fill);
                renderer.stroke_rounded_rect(box, 0.0f, l.hover_ring, 1.0f);
            }
            if (is_today && enabled)
            {
                renderer.stroke_rounded_rect(box, 1.0f, l.today_mark, 1.0f);
                text = l.today_text;
            }
        }

        TextStyle style    = calendar_cell_style(theme, month ? l.day_font : l.zoom_font, FontWeight::Regular);
        Rect      text_box = cell.bounds;
        if (month && !classic)
        {
            // Aero right-aligns the day numbers, 5 px in from the cell's right edge.
            style.horizontal = TextAlign::Trailing;
            text_box.width -= 5.0f;
        }
        renderer.draw_text(cell_text(cell, display_mode_), text_box, text, style);
        if (blackout && enabled)
        {
            const float y = std::round(cell.bounds.center().y) + 0.5f;
            renderer.stroke_line(Point{cell.bounds.x + 6.0f, y}, Point{cell.bounds.right() - 6.0f, y}, l.blackout_text, 1.0f);
        }
    }

    // In a date picker's popup the classic calendar sits in a 1 px COLOR_WINDOWFRAME frame.
    if (classic && popup_frame_)
    {
        renderer.stroke_rounded_rect(b.inset(Thickness{0.5f}), 0.0f, l.separator_color, 1.0f);
    }

    // The footer: today's date after its mark, bold in classic.
    if (today_highlighted_)
    {
        const Rect f     = footer_rect();
        TextStyle  style = calendar_cell_style(theme, l.day_font, classic ? FontWeight::Bold : FontWeight::Regular);
        style.horizontal = TextAlign::Leading;
        const std::string line  = detail::fill(strings().date_time.today_footer,
                                               {format_date(now, regional_date_format().short_date_pattern)});
        const Color       color = enabled ? l.footer_text : l.disabled_text;
        if (classic)
        {
            if (enabled)
            {
                if (l.today_frame)
                {
                    renderer.stroke_rounded_rect(Rect{f.x + 4.5f, f.y + 3.5f, 20.0f, 11.0f}, 0.0f, l.today_mark, 1.0f);
                }
                else
                {
                    calendar_draw_loop(renderer, Point{f.x + 2.0f, f.y}, l.today_mark);
                }
            }
            renderer.draw_text(line, Rect{f.x + 30.0f, f.y, f.width - 30.0f, f.height}, color, style);
        }
        else
        {
            // A 41 x 13 box like today's outline, then the line, the pair centered.
            const float group = 41.0f + 6.0f + l.footer_text_width;
            const float x     = std::round(f.x + (f.width - group) * 0.5f);
            if (enabled)
            {
                renderer.stroke_rounded_rect(Rect{x + 0.5f, f.y + 3.5f, 40.0f, 12.0f}, 1.0f, l.today_mark, 1.0f);
            }
            renderer.draw_text(line, Rect{x + 47.0f, f.y, f.right() - x - 47.0f, f.height}, color, style);
        }
    }
}

void CalendarViewControl::render_applet(Renderer& renderer, const Theme& theme) const
{
    const Look& l = *look_;
    const Rect  b{bounds().x, bounds().y, l.width, l.height};
    const auto  at      = [&](const Rect& r) { return Rect{b.x + r.x, b.y + r.y, r.width, r.height}; };
    const bool  enabled = is_enabled();

    if (popup_frame_)
    {
        renderer.fill_rounded_rect(b, 0.0f, theme.colors().background);
        renderer.stroke_rounded_rect(b.inset(Thickness{0.5f}), 0.0f, l.item_text, 1.0f);
    }
    if (month_list_ && year_spin_)
    {
        month_list_->render(renderer, RenderContext{theme});
        year_spin_->render(renderer, RenderContext{theme});
    }

    // The sunken box, the band of initials across its top.
    const Rect          box  = at(l.box);
    const ButtonVisual& face = theme.button_normal;
    renderer.fill_rounded_rect(box, 0.0f, l.background);
    draw_bevel_border(renderer,
                      box,
                      BorderStyle::Sunken,
                      face.bevel_light,
                      face.bevel_highlight,
                      face.bevel_shadow,
                      face.bevel_dark_shadow,
                      1.0f);
    const Rect band = at(l.weekday_band);
    renderer.fill_rounded_rect(band, 0.0f, l.title_fill);
    const unsigned first = first_day_of_week().c_encoding();
    const float    pitch = band.width / static_cast<float>(CalendarColumns);
    TextStyle      style = calendar_cell_style(theme, l.weekday_font, FontWeight::Regular);
    for (unsigned i = 0; i < CalendarColumns; ++i)
    {
        // The day's initial: the first character of its abbreviated name.
        const std::string& name   = regional_date_format().abbreviated_day_names[(first + i) % 7];
        std::size_t        length = name.empty() ? 0 : 1;
        while (length < name.size() && (static_cast<unsigned char>(name[length]) & 0xC0u) == 0x80u)
        {
            ++length;
        }
        renderer.draw_text(std::string_view{name}.substr(0, length),
                           Rect{band.x + pitch * static_cast<float>(i), band.y, pitch, band.height},
                           l.weekday_text,
                           style);
    }

    // The month's days, the selected one boxed in the selection color.
    style = calendar_cell_style(theme, l.day_font, FontWeight::Regular);
    for (const Cell& cell : cells_)
    {
        if (!cell.in_scope || !cell.in_range)
        {
            continue;
        }
        const bool        blackout = is_blackout(cell.date);
        const std::string text     = cell_text(cell, CalendarViewDisplayMode::Month);
        Color             color    = !enabled ? l.disabled_text : blackout ? l.blackout_text : l.item_text;
        if (enabled && is_selected(cell.date))
        {
            renderer.fill_rounded_rect(l.day_box(cell.bounds, text.size()), 0.0f, l.selected_fill);
            color = l.selected_text;
        }
        renderer.draw_text(text, cell.bounds, color, style);
        if (blackout && enabled)
        {
            const float y = std::round(cell.bounds.center().y) + 0.5f;
            renderer.stroke_line(Point{cell.bounds.x + 6.0f, y}, Point{cell.bounds.right() - 6.0f, y}, l.blackout_text, 1.0f);
        }
    }
}

void CalendarViewControl::open_month_menu(Point at)
{
    if (!month_menu_)
    {
        month_menu_ = std::make_unique<MenuFlyoutControl>();
        set_parent_of(*month_menu_, this);
    }
    std::vector<MenuItem> items;
    for (unsigned m = 1; m <= 12; ++m)
    {
        MenuItem item;
        item.text     = regional_date_format().month_names[m - 1];
        item.on_click = [this, m]
        { show_month(year_month_day{display_.year(), std::chrono::month{m}, std::chrono::day{1}}); };
        items.push_back(std::move(item));
    }
    month_menu_->set_items(std::move(items));
    month_menu_->show_at(at);
}

Rect CalendarViewControl::focus_ring_rect(const Theme&) const
{
    if (applet())
    {
        // The dotted rectangle just around the focused day's box.
        if (const auto i = focused_cell())
        {
            const Cell& cell = cells_[*i];
            return look_->day_box(cell.bounds, cell_text(cell, display_mode_).size()).inset(Thickness{-1.0f});
        }
        return grid_rect();
    }
    if (const auto i = focused_cell())
    {
        const Rect item = cells_[*i].bounds.inset(Thickness{look_->item_margin});
        if (look_->comctl())
        {
            return cells_[*i].bounds;
        }
        if (look_->rounded && display_mode_ == CalendarViewDisplayMode::Month)
        {
            const float d = std::min(item.width, item.height);
            return Rect{item.center().x - d * 0.5f, item.center().y - d * 0.5f, d, d};
        }
        if (look_->rounded)
        {
            const float d = std::max(0.0f, std::min(item.width, item.height) - 16.0f);
            return Rect{item.center().x - d * 0.5f, item.center().y - d * 0.5f, d, d};
        }
        return item;
    }
    return grid_rect();
}

float CalendarViewControl::focus_ring_corner_radius(const Theme& theme) const
{
    return look_->rounded ? focus_ring_rect(theme).width * 0.5f : 0.0f;
}

// ---- input ----

bool CalendarViewControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    const std::optional<std::size_t> at = cell_at(e.position);
    const bool on_footer                = look_->comctl() && today_highlighted_ && footer_rect().contains(e.position);
    if (e.button == PointerButton::Left && e.action == PointerAction::Down && on_footer)
    {
        footer_pressed_ = true;
        return true;
    }
    if (e.button == PointerButton::Left && e.action == PointerAction::Up && footer_pressed_)
    {
        footer_pressed_ = false;
        if (on_footer)
        {
            // "Today:" goes to today and selects it.
            const year_month_day now = clamp(today());
            if (display_mode_ != CalendarViewDisplayMode::Month)
            {
                set_display_mode(CalendarViewDisplayMode::Month);
            }
            show_scope(now, 0);
            focus_date_ = now;
            if (!is_selected(now))
            {
                toggle(now);
            }
            mark_dirty(DirtyFlags::Visual);
        }
        return true;
    }
    switch (e.action)
    {
        case PointerAction::Move:
            if (look_->kind == Look::Kind::Classic || applet())
            {
                return false;
            }
            if (at != hover_cell_)
            {
                hover_cell_ = at;
                mark_dirty(DirtyFlags::Visual);
            }
            return false;
        case PointerAction::Leave:
            if (hover_cell_)
            {
                hover_cell_.reset();
                mark_dirty(DirtyFlags::Visual);
            }
            return false;
        case PointerAction::Down:
            if (e.button == PointerButton::Left && at)
            {
                pressed_cell_ = at;
                mark_dirty(DirtyFlags::Visual);
                return true;
            }
            return false;
        case PointerAction::Up:
            if (e.button == PointerButton::Left && pressed_cell_)
            {
                const bool same = at == pressed_cell_;
                const Cell cell = cells_[*pressed_cell_];
                pressed_cell_.reset();
                mark_dirty(DirtyFlags::Visual);
                if (same)
                {
                    activate_cell(cell);
                }
                return true;
            }
            return false;
        case PointerAction::Wheel:
            if (e.wheel_delta != 0.0f && !e.horizontal_wheel)
            {
                page(e.wheel_delta > 0.0f ? -1 : 1);
                return true;
            }
            return false;
    }
    return false;
}

bool CalendarViewControl::key_override(const KeyEvent& e)
{
    if (e.action != KeyAction::Down || !is_enabled())
    {
        return false;
    }
    const bool classic = look_->kind == Look::Kind::Classic || applet();
    if (classic && e.modifiers.ctrl && !e.modifiers.shift && !e.modifiers.alt &&
        (e.key == Key::PageUp || e.key == Key::PageDown) && display_mode_ == CalendarViewDisplayMode::Month)
    {
        move_focus(calendar_add_years(focus_date_, e.key == Key::PageUp ? -1 : 1));
        return true;
    }
    if (e.modifiers.ctrl && !e.modifiers.shift && !e.modifiers.alt && !classic)
    {
        if (e.key == Key::ArrowUp)
        {
            zoom(true);
            return true;
        }
        if (e.key == Key::ArrowDown)
        {
            zoom(false);
            return true;
        }
        return false;
    }
    if (!e.modifiers.none() && !(classic && e.modifiers.ctrl && !e.modifiers.shift && !e.modifiers.alt))
    {
        return false;
    }
    const bool month = display_mode_ == CalendarViewDisplayMode::Month;
    const int  row   = month ? CalendarColumns : CalendarZoomCells; // 4 columns in every zoom grid
    const auto step  = [&](int cells_moved)
    {
        if (month)
        {
            return year_month_day{sys_days{focus_date_} + days{cells_moved}};
        }
        if (display_mode_ == CalendarViewDisplayMode::Year)
        {
            return calendar_add_months(focus_date_, cells_moved);
        }
        return calendar_add_years(focus_date_, cells_moved);
    };
    switch (e.key)
    {
        case Key::ArrowLeft:
            move_focus(step(-1));
            return true;
        case Key::ArrowRight:
            move_focus(step(1));
            return true;
        case Key::ArrowUp:
            move_focus(step(-row));
            return true;
        case Key::ArrowDown:
            move_focus(step(row));
            return true;
        case Key::Home:
        case Key::End:
        {
            const bool home = e.key == Key::Home;
            if (month)
            {
                const auto last = std::chrono::year_month_day_last{display_.year(),
                                                                   std::chrono::month_day_last{display_.month()}};
                move_focus(home ? calendar_first_of_month(display_) : year_month_day{last});
            }
            else if (display_mode_ == CalendarViewDisplayMode::Year)
            {
                move_focus(year_month_day{display_.year(),
                                          home ? std::chrono::January : std::chrono::December,
                                          std::chrono::day{1}});
            }
            else
            {
                const int first = calendar_decade_start(static_cast<int>(display_.year()));
                move_focus(year_month_day{year{home ? first : first + 9}, std::chrono::January, std::chrono::day{1}});
            }
            return true;
        }
        case Key::PageUp:
            page(-1);
            return true;
        case Key::PageDown:
            page(1);
            return true;
        case Key::Enter:
        case Key::Space:
            if (month)
            {
                toggle(focus_date_);
            }
            else
            {
                display_ = focus_date_;
                zoom(false);
            }
            return true;
        default:
            return false;
    }
}

void CalendarViewControl::tick_override(Duration delta)
{
    if (transition_ == Transition::None)
    {
        return;
    }
    const double total = transition_ == Transition::Slide ? CalendarSlideMs : (look_->rounded ? 500.0 : 733.0);
    transition_ms_ += delta.count();
    if (transition_ms_ >= total || !animations_enabled())
    {
        transition_ = Transition::None;
    }
    mark_dirty(DirtyFlags::Visual);
}

void CalendarViewControl::enabled_changed()
{
    hover_cell_.reset();
    pressed_cell_.reset();
    mark_dirty(DirtyFlags::Visual);
}

// ---- tree ----

void CalendarViewControl::for_each_child(ChildVisitor visit) const
{
    visit(*header_);
    visit(*previous_);
    visit(*next_);
    if (month_menu_)
    {
        visit(*month_menu_);
    }
    if (applet() && month_list_)
    {
        visit(*month_list_);
        visit(*year_spin_);
    }
}

void CalendarViewControl::collect_focusables(std::vector<Element*>& out)
{
    if (applet() && month_list_)
    {
        month_list_->collect_focusables(out);
        if (year_spin_->focusable())
        {
            out.push_back(year_spin_.get());
        }
        if (focusable())
        {
            out.push_back(this);
        }
        return;
    }
    for (detail::CalendarPart* part : {header_.get(), previous_.get(), next_.get()})
    {
        if (part->focusable())
        {
            out.push_back(part);
        }
    }
    if (focusable())
    {
        out.push_back(this);
    }
}

void CalendarViewControl::automation_children(std::vector<Element*>& out) const
{
    if (applet() && month_list_)
    {
        out.push_back(month_list_.get());
        out.push_back(year_spin_.get());
    }
    else
    {
        out.push_back(header_.get());
        out.push_back(previous_.get());
        out.push_back(next_.get());
    }
    if (display_mode_ == CalendarViewDisplayMode::Month)
    {
        std::vector<Element*> headers;
        table_column_headers(headers);
        out.insert(out.end(), headers.begin(), headers.end());
    }
    for (std::size_t r = 0; r < grid_row_count(); ++r)
    {
        for (std::size_t c = 0; c < grid_column_count(); ++c)
        {
            if (Element* cell = grid_item(r, c))
            {
                out.push_back(cell);
            }
        }
    }
}

// ---- automation ----

std::size_t CalendarViewControl::grid_row_count() const
{
    return display_mode_ == CalendarViewDisplayMode::Month ? CalendarWeeks : static_cast<std::size_t>(look_->zoom_rows);
}

std::size_t CalendarViewControl::grid_column_count() const
{
    return display_mode_ == CalendarViewDisplayMode::Month ? CalendarColumns : CalendarZoomCells;
}

Element* CalendarViewControl::grid_item(std::size_t row, std::size_t column) const
{
    ensure_cells();
    const std::size_t index = row * grid_column_count() + column;
    if (row >= grid_row_count() || column >= grid_column_count() || index >= cells_.size() || !cells_[index].in_range)
    {
        return nullptr;
    }
    auto* self = const_cast<CalendarViewControl*>(this); // NOLINT(cppcoreguidelines-pro-type-const-cast): a cache
    while (cell_nodes_.size() <= index)
    {
        cell_nodes_.push_back(std::make_unique<detail::CalendarCellAutomation>(*self, cell_nodes_.size()));
    }
    cell_nodes_[index]->place();
    return cell_nodes_[index].get();
}

void CalendarViewControl::table_column_headers(std::vector<Element*>& out) const
{
    if (display_mode_ != CalendarViewDisplayMode::Month)
    {
        return;
    }
    auto*       self = const_cast<CalendarViewControl*>(this); // NOLINT(cppcoreguidelines-pro-type-const-cast): a cache
    const Rect  grid = grid_rect();
    const float pitch    = grid.width / static_cast<float>(CalendarColumns);
    const unsigned first = first_day_of_week().c_encoding();
    for (unsigned i = 0; i < CalendarColumns; ++i)
    {
        if (!weekday_nodes_[i])
        {
            weekday_nodes_[i] = std::make_unique<detail::CalendarWeekdayAutomation>(*self, std::string{});
        }
        auto& node = static_cast<detail::CalendarWeekdayAutomation&>(*weekday_nodes_[i]);
        node.set_name(regional_date_format().day_names[(first + i) % 7],
                      Rect{grid.x + pitch * static_cast<float>(i),
                           bounds().y + look_->weekday_band.y,
                           pitch,
                           look_->weekday_band.height});
        out.push_back(&node);
    }
}

void CalendarViewControl::selected_items(std::vector<Element*>& out) const
{
    if (display_mode_ != CalendarViewDisplayMode::Month)
    {
        return;
    }
    for (std::size_t i = 0; i < CalendarColumns * CalendarWeeks; ++i)
    {
        Element* node = grid_item(i / CalendarColumns, i % CalendarColumns);
        if (node && cells_[i].in_scope && is_selected(cells_[i].date))
        {
            out.push_back(node);
        }
    }
}

std::string CalendarViewControl::text_value() const
{
    if (selected_.size() == 1)
    {
        return format_date(selected_.front(), regional_date_format().long_date_pattern);
    }
    return heading(display_mode_, display_);
}

// ---- spec ----

std::unique_ptr<ui::Element> CalendarView::build() &&
{
    auto node = std::make_unique<ui::CalendarViewControl>();
    apply(selection_mode, *node, [raw = node.get()](const CalendarViewSelectionMode& v) { raw->set_selection_mode(v); });
    // The range first, so a bound selection or display date is clamped to it.
    apply(min_date,
          *node,
          [raw = node.get()](const std::optional<std::chrono::year_month_day>& v) { raw->set_date_range(v, raw->max_); });
    apply(max_date,
          *node,
          [raw = node.get()](const std::optional<std::chrono::year_month_day>& v) { raw->set_date_range(raw->min_, v); });
    apply(first_day_of_week,
          *node,
          [raw = node.get()](const std::optional<std::chrono::weekday>& v) { raw->set_first_day_of_week(v); });
    apply(today_highlighted, *node, [raw = node.get()](const bool& v) { raw->set_today_highlighted(v); });
    apply(out_of_scope_enabled, *node, [raw = node.get()](const bool& v) { raw->set_out_of_scope_enabled(v); });
    apply(group_label_visible, *node, [raw = node.get()](const bool& v) { raw->set_group_label_visible(v); });
    apply(display_date,
          *node,
          [raw = node.get()](const std::optional<std::chrono::year_month_day>& v)
          {
              if (v)
              {
                  raw->set_display_date(*v);
              }
          });
    if (is_blackout)
    {
        node->set_blackout(std::move(is_blackout));
    }

    // A selection the control trimmed (out of range, blackout, single mode) goes back to a
    // two-way property, so both end up equal.
    const auto write_dates = ui::shared_writer(selected_dates);
    apply(selected_dates,
          *node,
          [raw = node.get(), write_dates](const std::vector<std::chrono::year_month_day>& v)
          {
              raw->set_selected_dates(v);
              if (write_dates && raw->selected_dates() != v)
              {
                  (*write_dates)(raw->selected_dates());
              }
          });
    // A date that starts out selected is the one shown.
    if (!node->selected_dates().empty() && !display_date.initial())
    {
        node->set_display_date(node->selected_dates().front());
    }
    const auto write_mode = ui::shared_writer(display_mode);
    apply(display_mode, *node, [raw = node.get()](const CalendarViewDisplayMode& v) { raw->set_display_mode(v); });
    // Settles any zoom the initial display mode started.
    node->transition_ = CalendarViewControl::Transition::None;

    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });

    if (auto changed = ui::write_back(write_dates, std::move(on_selected_dates_changed)))
    {
        node->on_selected_dates_changed(std::move(changed));
    }
    if (auto changed = ui::write_back(write_mode, std::move(on_display_mode_changed)))
    {
        node->on_display_mode_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
