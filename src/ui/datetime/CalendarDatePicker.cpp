// SPDX-License-Identifier: MIT
#include "winui/ui/datetime/CalendarDatePicker.hpp"

#include "../../render/DropdownChrome.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/DateFormat.hpp"
#include "winui/core/Strings.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace ui
{

namespace
{
// The calendar glyph Fluent's date field shows at its right end.
constexpr IconGlyph DatePickerCalendarGlyph{0xE787, "Segoe Fluent Icons", 12.0f};
} // namespace

CalendarDatePickerControl::CalendarDatePickerControl() : calendar_(std::make_unique<CalendarViewControl>())
{
    calendar_->set_selection_mode(CalendarViewSelectionMode::Single);
    calendar_->on_selected_dates_changed(
        [this](const std::vector<std::chrono::year_month_day>& dates)
        {
            if (syncing_)
            {
                return;
            }
            if (dates.empty())
            {
                // Clicking the picked day again clears the date and leaves the calendar open.
                set_date(std::nullopt);
                return;
            }
            set_date(dates.front());
            set_calendar_open(false);
        });
    format_changed_ = regional_date_format_changed().observe(
        [this] { mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); });
    strings_changed_ = strings_changed().observe([this] { mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); });
}

CalendarDatePickerControl::~CalendarDatePickerControl()
{
    close_popup();
}

void CalendarDatePickerControl::set_date(std::optional<std::chrono::year_month_day> date)
{
    if (date)
    {
        *date = calendar_->clamp(*date);
    }
    if (date == date_)
    {
        return;
    }
    date_ = date;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (on_date_changed_)
    {
        on_date_changed_(date_);
    }
}

void CalendarDatePickerControl::set_header(std::string text)
{
    if (header_.set_text(std::move(text)))
    {
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void CalendarDatePickerControl::set_description(std::string text)
{
    if (description_.set_text(std::move(text)))
    {
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void CalendarDatePickerControl::set_placeholder_text(std::optional<std::string> text)
{
    if (text != placeholder_)
    {
        placeholder_ = std::move(text);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void CalendarDatePickerControl::set_date_format(std::optional<std::string> pattern)
{
    if (pattern != format_)
    {
        format_ = std::move(pattern);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

std::string CalendarDatePickerControl::shown_text() const
{
    if (date_)
    {
        return format_date(*date_, format_.value_or(regional_date_format().short_date_pattern));
    }
    return placeholder_.value_or(strings().date_time.pick_a_date);
}

void CalendarDatePickerControl::set_calendar_open(bool open)
{
    open = open && is_enabled();
    if (open == open_)
    {
        return;
    }
    open_ = open;
    if (open_)
    {
        // The calendar opens on the date, or on today's month.
        syncing_ = true;
        calendar_->set_selection_mode(CalendarViewSelectionMode::Single);
        calendar_->set_selected_dates(date_ ? std::vector{*date_} : std::vector<std::chrono::year_month_day>{});
        calendar_->set_display_mode(CalendarViewDisplayMode::Month);
        calendar_->transition_ = CalendarViewControl::Transition::None;
        calendar_->set_display_date(date_.value_or(calendar_->today()));
        if (date_)
        {
            calendar_->focus_date_ = *date_;
        }
        syncing_ = false;
    }
    sync_popup();
    mark_dirty(DirtyFlags::Visual);
    if (on_open_changed_)
    {
        on_open_changed_(open_);
    }
}

void CalendarDatePickerControl::sync_popup()
{
    if (!open_)
    {
        close_popup();
        return;
    }
    open_popup(PopupRequest{
        .content     = calendar_.get(),
        .placement   = PopupPlacement::Below,
        .takes_focus = true,
        .chrome      = {.corner_radius = [](const Theme& theme)
                        { return detail::controls(theme).calendar_chrome ? 0.0f : theme.overlay_corner_radius; }},
        .on_dismiss  = [this](PopupCloseReason) { set_calendar_open(false); },
    });
}

// ---- layout ----

Size CalendarDatePickerControl::measure_override(Size available, const MeasureContext& context) const
{
    const Theme& theme     = context.theme;
    const Size   header    = header_.measure(available.width, context);
    available              = ControlHeader::below(available, header);
    const Size description = description_.measure(available.width, context);
    available              = ControlHeader::below(available, description);

    // The calendar in the popup takes the popup's corner (Fluent's overlay radius) and, in the
    // classic eras, a frame.
    calendar_->corner_radius_override_ = detail::controls(theme).calendar_chrome ? -1.0f : theme.overlay_corner_radius;
    calendar_->popup_frame_            = true;

    const auto& era    = detail::controls(theme).dropdown_chrome;
    const float line   = context.measurer.measure("", theme.text_style).height;
    const float height = std::max(line + theme.sizing.combo_box_padding.value_or(theme.padding).vertical(),
                                  theme.sizing.combo_box_min_height.value_or(0.0f));
    glyph_column_      = era ? era->arrow_column + 2.0f : height;

    // As wide as the widest of the placeholder, the date shown and a long date in the format.
    const std::string pattern = format_.value_or(regional_date_format().short_date_pattern);
    float             text    = context.measurer.measure(shown_text(), theme.text_style).width;
    text = std::max(text,
                    context.measurer.measure(placeholder_.value_or(strings().date_time.pick_a_date), theme.text_style).width);
    text              = std::max(text,
                                 context.measurer
                                     .measure(format_date(std::chrono::year_month_day{std::chrono::year{2026},
                                                                                      std::chrono::September,
                                                                                      std::chrono::day{28}},
                                                          pattern),
                                              theme.text_style)
                                     .width);
    const float inset = era ? era->text_inset : theme.padding.left;
    Size        desired{std::ceil(text + inset + 8.0f + glyph_column_), height};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return ControlHeader::above(ControlHeader::above(desired, header), description);
}

void CalendarDatePickerControl::arrange_override(const Rect&, const ArrangeContext&)
{
    header_.arrange(*this);
    description_.arrange(*this);
    if (open_)
    {
        sync_popup(); // the calendar hangs from the field, which may have moved
    }
}

// ---- drawing ----

void CalendarDatePickerControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme   = context.theme;
    const bool   enabled = is_enabled();
    header_.render(renderer, theme, enabled);
    description_.render(renderer, theme, enabled);

    const Rect        box         = bounds();
    const std::string text        = shown_text();
    const bool        placeholder = !date_;
    const auto&       era         = detail::controls(theme).dropdown_chrome;
    if (era && era->field_with_button)
    {
        // The classic eras' DATETIMEPICK_CLASS: a field in the field edge with the combo button.
        draw_classic_combo_field(renderer,
                                 theme,
                                 *era,
                                 ClassicComboState{box, enabled, open_, is_focused(), focus_visible(), text, placeholder});
        return;
    }

    const ButtonState   state  = !enabled            ? ButtonState::Disabled
                                 : pressed_ || open_ ? ButtonState::Pressed
                                 : hovered_          ? ButtonState::Hover
                                                     : ButtonState::Normal;
    const ButtonVisual& visual = theme.for_state(state);
    if (era)
    {
        draw_control_chrome(renderer, box, theme.resolved_button_corner_radius(), visual, theme.border_width);
    }
    else
    {
        draw_control_chrome(renderer, box, theme.corner_radius, visual, theme.border_width, false, false);
    }

    const float inset = era ? era->text_inset : theme.padding.left;
    TextStyle   style = theme.text_style;
    style.horizontal  = TextAlign::Leading;
    const Rect label{box.x + inset, box.y, std::max(0.0f, box.width - inset - glyph_column_), box.height};
    renderer.draw_text(text, label, placeholder && enabled ? visual.text.with_alpha(0.6f) : visual.text, style);

    const Rect column{box.right() - glyph_column_, box.y, glyph_column_, box.height};
    if (era)
    {
        draw_dropdown_arrow(renderer, *era, column.inset(Edges{.right = 2.0f}), enabled);
    }
    else
    {
        // Fluent: the calendar glyph in the secondary text color.
        draw_icon(renderer,
                  DatePickerCalendarGlyph,
                  column,
                  enabled ? theme.colors().text_secondary : visual.text,
                  0.0f,
                  theme.icon_font_family);
    }
}

// ---- input ----

bool CalendarDatePickerControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    const bool inside = bounds().contains(e.position);
    switch (e.action)
    {
        case PointerAction::Move:
            if (inside != hovered_)
            {
                hovered_ = inside;
                mark_dirty(DirtyFlags::Visual);
            }
            return false;
        case PointerAction::Leave:
            if (hovered_)
            {
                hovered_ = false;
                mark_dirty(DirtyFlags::Visual);
            }
            return false;
        case PointerAction::Down:
            if (e.button == PointerButton::Left && inside)
            {
                pressed_ = true;
                mark_dirty(DirtyFlags::Visual);
                return true;
            }
            return false;
        case PointerAction::Up:
            if (e.button == PointerButton::Left && pressed_)
            {
                pressed_ = false;
                mark_dirty(DirtyFlags::Visual);
                if (inside)
                {
                    set_calendar_open(!open_);
                }
                return true;
            }
            return false;
        case PointerAction::Wheel:
            return false;
    }
    return false;
}

bool CalendarDatePickerControl::key_override(const KeyEvent& e)
{
    if (e.action != KeyAction::Down || !is_enabled())
    {
        return false;
    }
    const bool plain   = e.modifiers.none();
    const bool alt     = e.modifiers.alt && !e.modifiers.ctrl && !e.modifiers.shift;
    const bool opening = (plain && (e.key == Key::Enter || e.key == Key::Space || e.key == Key::F4)) ||
                         (alt && e.key == Key::ArrowDown);
    if (opening)
    {
        set_calendar_open(true);
        return true;
    }
    return false;
}

// The calendar is popup content, which the window's tree walk doesn't reach: tick it from here,
// or its month slide never moves.
void CalendarDatePickerControl::tick_override(Duration delta)
{
    if (open_)
    {
        calendar_->tick(delta);
    }
}

void CalendarDatePickerControl::enabled_changed()
{
    hovered_ = false;
    pressed_ = false;
    if (!is_enabled())
    {
        set_calendar_open(false);
    }
    mark_dirty(DirtyFlags::Visual);
}

void CalendarDatePickerControl::automation_children(std::vector<Element*>& out) const
{
    if (open_)
    {
        out.push_back(calendar_.get());
    }
}

// ---- spec ----

std::unique_ptr<ui::Element> CalendarDatePicker::build() &&
{
    auto node = std::make_unique<ui::CalendarDatePickerControl>();
    apply(header, *node, [raw = node.get()](const std::string& v) { raw->set_header(v); });
    apply(description, *node, [raw = node.get()](const std::string& v) { raw->set_description(v); });
    apply(placeholder_text,
          *node,
          [raw = node.get()](const std::optional<std::string>& v) { raw->set_placeholder_text(v); });
    apply(date_format, *node, [raw = node.get()](const std::optional<std::string>& v) { raw->set_date_format(v); });
    // The calendar's settings first, so the date is clamped to its range.
    CalendarViewControl* calendar = &node->calendar();
    apply(min_date,
          *node,
          [calendar](const std::optional<std::chrono::year_month_day>& v)
          { calendar->set_date_range(v, calendar->max_); });
    apply(max_date,
          *node,
          [calendar](const std::optional<std::chrono::year_month_day>& v)
          { calendar->set_date_range(calendar->min_, v); });
    apply(first_day_of_week,
          *node,
          [calendar](const std::optional<std::chrono::weekday>& v) { calendar->set_first_day_of_week(v); });
    apply(today_highlighted, *node, [calendar](const bool& v) { calendar->set_today_highlighted(v); });
    apply(out_of_scope_enabled, *node, [calendar](const bool& v) { calendar->set_out_of_scope_enabled(v); });
    apply(group_label_visible, *node, [calendar](const bool& v) { calendar->set_group_label_visible(v); });
    if (is_blackout)
    {
        calendar->set_blackout(std::move(is_blackout));
    }

    // A date the picker clamped goes back to a two-way property, so both end up equal.
    const auto write_date = ui::shared_writer(date);
    apply(date,
          *node,
          [raw = node.get(), write_date](const std::optional<std::chrono::year_month_day>& v)
          {
              raw->set_date(v);
              if (write_date && raw->date() != v)
              {
                  (*write_date)(raw->date());
              }
          });
    const auto write_open = ui::shared_writer(is_calendar_open);
    apply(is_calendar_open, *node, [raw = node.get()](const bool& v) { raw->set_calendar_open(v); });
    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });

    if (auto changed = ui::write_back(write_date, std::move(on_date_changed)))
    {
        node->on_date_changed(std::move(changed));
    }
    {
        auto opened       = std::move(on_opened);
        auto closed       = std::move(on_closed);
        auto open_changed = [opened = std::move(opened), closed = std::move(closed)](bool open) mutable
        {
            if (open && opened)
            {
                opened();
            }
            else if (!open && closed)
            {
                closed();
            }
        };
        if (auto changed = ui::write_back(write_open, Callback<void(bool)>{std::move(open_changed)}))
        {
            node->on_calendar_open_changed(std::move(changed));
        }
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
