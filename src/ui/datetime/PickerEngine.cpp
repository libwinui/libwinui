// SPDX-License-Identifier: MIT
#include "PickerEngine.hpp"

#include "../../render/DropdownChrome.hpp"
#include "../scrolling/ScrollArrowButton.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/datetime/CalendarView.hpp"
#include "winui/ui/input/ComboBox.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <cmath>

namespace ui::detail
{

namespace
{
constexpr float  PickerItemHeight      = 40.0f;
constexpr float  PickerFlyoutMaxHeight = 398.0f;
constexpr float  PickerButtonRow       = 41.0f;
constexpr float  PickerMaxFieldWidth   = 456.0f;
constexpr float  PickerColumnPadding   = 24.0f; // a centered column's room around its widest item
constexpr double PickerRepeatDelayMs   = 500.0;
constexpr double PickerRepeatMs        = 33.0;
constexpr double PickerTypingResetMs   = 1500.0;
constexpr float  PickerComboMinWidth   = 80.0f;
constexpr float  PickerComboGap        = 20.0f;

constexpr IconGlyph PickerAcceptGlyph{0xE8FB, "Segoe Fluent Icons", 16.0f};
constexpr IconGlyph PickerDismissGlyph{0xE711, "Segoe Fluent Icons", 16.0f};

const ControlChrome::DateTimePickerChrome& picker_chrome(const Theme& theme)
{
    static const ControlChrome::DateTimePickerChrome fluent2{};
    const auto&                                      chrome = controls(theme).date_time_picker_chrome;
    return chrome ? *chrome : fluent2;
}

// Each part's column across `width`, from the parts' weights, with `divider` between them.
std::vector<Rect> picker_columns(const std::vector<PickerPart>& parts, const Rect& area, float divider)
{
    std::vector<Rect> out;
    float             total = 0.0f;
    for (const PickerPart& part : parts)
    {
        total += part.weight;
    }
    const float room = std::max(0.0f, area.width - divider * static_cast<float>(parts.size() > 0 ? parts.size() - 1 : 0));
    float x = area.x;
    for (const PickerPart& part : parts)
    {
        const float w = total > 0.0f ? room * part.weight / total : 0.0f;
        out.push_back(Rect{x, area.y, w, area.height});
        x += w + divider;
    }
    return out;
}

TextStyle picker_text_style(const Theme& theme, bool leading)
{
    TextStyle style  = theme.text_style;
    style.horizontal = leading ? TextAlign::Leading : TextAlign::Center;
    style.vertical   = TextAlign::Center;
    style.wrapping   = TextWrapping::NoWrap;
    return style;
}

// A part's text box inside its column: 3 above and 6 below, the month 9 in from the left.
Rect picker_text_box(const Rect& column, bool leading)
{
    return Rect{column.x + (leading ? 9.0f : 0.0f),
                column.y + 3.0f,
                std::max(0.0f, column.width - (leading ? 9.0f : 0.0f)),
                std::max(0.0f, column.height - 9.0f)};
}
} // namespace

// ---- the flyout: one looping column per part, a band over the picked row, Accept and Dismiss ----

class PickerFlyout final : public Element
{
public:
    // A column of the flyout: a part's choices around the one in the band.
    class Column final : public Element, private SelectionProvider, private ScrollProvider
    {
    public:
        Column(PickerFlyout& flyout, std::size_t index) : flyout_(flyout), index_(index)
        {
            set_parent_of(*this, &flyout);
        }

        PickerPart         part;
        float              position = 0.0f; // the index drawn in the band, animated toward part.selected
        float              target   = 0.0f; // the scroll's goal, which may run past the ends while looping
        std::optional<int> hover_row;
        std::optional<int> pressed_row;
        bool               hovered       = false;
        int                arrow_pressed = 0; // +1 the down button, -1 the up button
        double             arrow_ms      = 0.0;

        [[nodiscard]] bool focusable() const override
        {
            return true;
        }
        [[nodiscard]] AutomationRole automation_role() const override
        {
            return AutomationRole::List;
        }
        [[nodiscard]] std::string automation_name_fallback() const override
        {
            return part.name;
        }
        [[nodiscard]] bool focus_ring_inside() const override
        {
            return true;
        }

        // Moves the selection by `delta` rows, animated; clamped where the column doesn't loop.
        void step(int delta)
        {
            const int count = static_cast<int>(part.items.size());
            if (count == 0)
            {
                return;
            }
            int next = static_cast<int>(std::lround(target)) + delta;
            if (!part.loops)
            {
                next = std::clamp(next, 0, count - 1);
            }
            target                     = static_cast<float>(next);
            const std::size_t selected = static_cast<std::size_t>(((next % count) + count) % count);
            if (selected != part.selected)
            {
                part.selected = selected;
                flyout_.column_changed(index_);
            }
            if (!animations_enabled())
            {
                position = target;
            }
            mark_dirty(DirtyFlags::Visual);
        }
        void jump(std::size_t selected)
        {
            const int current = static_cast<int>(std::lround(target));
            const int count   = static_cast<int>(part.items.size());
            const int here    = count > 0 ? ((current % count) + count) % count : 0;
            step(static_cast<int>(selected) - here);
        }
        // The row under `y` relative to the band, -1 above it, +1 below.
        [[nodiscard]] int row_at(float y) const
        {
            return static_cast<int>(std::floor((y - flyout_.band_rect().y) / PickerItemHeight));
        }

    protected:
        [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
        {
            return available;
        }
        void render_override(Renderer&, const RenderContext&) const override
        {
            // The flyout draws its columns.
        }
        bool pointer_override(const PointerEvent& e) override
        {
            const Rect b      = bounds();
            const bool inside = b.contains(e.position);
            const Rect up     = flyout_.arrow_rect(b, true);
            const Rect down   = flyout_.arrow_rect(b, false);
            switch (e.action)
            {
                case PointerAction::Move:
                {
                    const std::optional<int> row = inside && !up.contains(e.position) && !down.contains(e.position)
                                                       ? std::optional<int>{row_at(e.position.y)}
                                                       : std::nullopt;
                    if (inside != hovered || row != hover_row)
                    {
                        hovered   = inside;
                        hover_row = row;
                        flyout_.mark_dirty(DirtyFlags::Visual);
                    }
                    return false;
                }
                case PointerAction::Leave:
                    hovered = false;
                    hover_row.reset();
                    flyout_.mark_dirty(DirtyFlags::Visual);
                    return false;
                case PointerAction::Down:
                    if (e.button != PointerButton::Left || !inside)
                    {
                        return false;
                    }
                    if (up.contains(e.position) || down.contains(e.position))
                    {
                        arrow_pressed = up.contains(e.position) ? -1 : 1;
                        arrow_ms      = 0.0;
                        step(arrow_pressed);
                    }
                    else
                    {
                        pressed_row = row_at(e.position.y);
                    }
                    flyout_.mark_dirty(DirtyFlags::Visual);
                    return true;
                case PointerAction::Up:
                    if (e.button != PointerButton::Left)
                    {
                        return false;
                    }
                    if (arrow_pressed != 0)
                    {
                        arrow_pressed = 0;
                        flyout_.mark_dirty(DirtyFlags::Visual);
                        return true;
                    }
                    if (pressed_row)
                    {
                        // A tap on a row brings it into the band.
                        const int row = *pressed_row;
                        pressed_row.reset();
                        if (inside && row_at(e.position.y) == row)
                        {
                            step(row);
                        }
                        flyout_.mark_dirty(DirtyFlags::Visual);
                        return true;
                    }
                    return false;
                case PointerAction::Wheel:
                    if (inside && e.wheel_delta != 0.0f)
                    {
                        step(e.wheel_delta > 0.0f ? -1 : 1);
                        return true;
                    }
                    return false;
            }
            return false;
        }
        bool key_override(const KeyEvent& e) override
        {
            if (e.action != KeyAction::Down || e.modifiers.alt)
            {
                return false;
            }
            const int count = static_cast<int>(part.items.size());
            switch (e.key)
            {
                case Key::ArrowUp:
                    step(-1);
                    return true;
                case Key::ArrowDown:
                    step(1);
                    return true;
                case Key::PageUp:
                    step(-4);
                    return true;
                case Key::PageDown:
                    step(4);
                    return true;
                case Key::Home:
                    jump(0);
                    position = target;
                    return true;
                case Key::End:
                    jump(static_cast<std::size_t>(std::max(0, count - 1)));
                    position = target;
                    return true;
                case Key::ArrowLeft:
                case Key::ArrowRight:
                    flyout_.focus_column(static_cast<int>(index_) + (e.key == Key::ArrowLeft ? -1 : 1));
                    return true;
                case Key::Enter:
                case Key::Space:
                    flyout_.accept();
                    return true;
                default:
                    return false;
            }
        }

    private:
        void automation_providers(AutomationProviders& out) override
        {
            out.selection = this;
            out.scroll    = this;
        }
        void selected_items(std::vector<Element*>&) const override
        {
        }
        [[nodiscard]] bool can_select_multiple() const override
        {
            return false;
        }
        [[nodiscard]] bool selection_required() const override
        {
            return true;
        }
        [[nodiscard]] float scroll_horizontal_percent() const override
        {
            return -1.0f;
        }
        [[nodiscard]] float scroll_vertical_percent() const override
        {
            const std::size_t count = part.items.size();
            return count > 1 ? 100.0f * static_cast<float>(part.selected) / static_cast<float>(count - 1) : 0.0f;
        }
        bool set_scroll_percent(float, float vertical) override
        {
            const std::size_t count = part.items.size();
            if (count == 0 || vertical < 0.0f)
            {
                return false;
            }
            jump(static_cast<std::size_t>(std::lround(vertical / 100.0f * static_cast<float>(count - 1))));
            return true;
        }

        PickerFlyout& flyout_;
        std::size_t   index_;
    };

    // Accept or Dismiss: a button the flyout draws itself.
    class Button final : public Element, private InvokeProvider
    {
    public:
        Button(PickerFlyout& flyout, bool accept) : flyout_(flyout), accept_(accept)
        {
            set_parent_of(*this, &flyout);
        }
        bool hovered = false;
        bool pressed = false;

        [[nodiscard]] bool focusable() const override
        {
            return true;
        }
        [[nodiscard]] AutomationRole automation_role() const override
        {
            return AutomationRole::Button;
        }
        [[nodiscard]] std::string automation_name_fallback() const override
        {
            return accept_ ? flyout_.engine_.model().accept_name() : flyout_.engine_.model().dismiss_name();
        }
        [[nodiscard]] float focus_ring_corner_radius(const Theme&) const override
        {
            return 4.0f;
        }

    protected:
        [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
        {
            return available;
        }
        void render_override(Renderer&, const RenderContext&) const override
        {
        }
        bool pointer_override(const PointerEvent& e) override
        {
            const bool inside = bounds().contains(e.position);
            switch (e.action)
            {
                case PointerAction::Move:
                case PointerAction::Leave:
                {
                    const bool now = e.action == PointerAction::Move && inside;
                    if (now != hovered)
                    {
                        hovered = now;
                        flyout_.mark_dirty(DirtyFlags::Visual);
                    }
                    return false;
                }
                case PointerAction::Down:
                    if (e.button == PointerButton::Left && inside)
                    {
                        pressed = true;
                        flyout_.mark_dirty(DirtyFlags::Visual);
                        return true;
                    }
                    return false;
                case PointerAction::Up:
                    if (e.button == PointerButton::Left && pressed)
                    {
                        pressed = false;
                        flyout_.mark_dirty(DirtyFlags::Visual);
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
            if (accept_)
            {
                flyout_.accept();
            }
            else
            {
                flyout_.engine_.dismiss();
            }
            return true;
        }
        PickerFlyout& flyout_;
        bool          accept_;
    };

    explicit PickerFlyout(PickerEngine& engine) :
        engine_(engine),
        accept_button_(*this, true),
        dismiss_button_(*this, false)
    {
    }

    void set_parts(std::vector<PickerPart> parts, bool reset_positions)
    {
        while (columns_.size() < parts.size())
        {
            columns_.push_back(std::make_unique<Column>(*this, columns_.size()));
        }
        columns_.resize(parts.size());
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            Column&    column = *columns_[i];
            const bool fresh  = reset_positions || column.part.items.size() != parts[i].items.size();
            column.part       = std::move(parts[i]);
            if (fresh)
            {
                column.target   = static_cast<float>(column.part.selected);
                column.position = column.target;
            }
        }
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    void set_width(float width)
    {
        width_ = width;
    }
    [[nodiscard]] std::vector<std::size_t> selection() const
    {
        std::vector<std::size_t> out;
        for (const auto& column : columns_)
        {
            out.push_back(column->part.selected);
        }
        return out;
    }
    // The band's rect in window coordinates.
    [[nodiscard]] Rect band_rect() const
    {
        const Rect  b  = bounds();
        const float cy = b.y + 1.0f + (b.height - 2.0f - PickerButtonRow) * 0.5f;
        return Rect{b.x + 4.0f, cy - PickerItemHeight * 0.5f, b.width - 8.0f, PickerItemHeight};
    }
    // The band's center, relative to the flyout's top: where the field's center goes.
    [[nodiscard]] float band_center_offset() const
    {
        return 1.0f + (PickerFlyoutMaxHeight - 2.0f - PickerButtonRow) * 0.5f;
    }
    [[nodiscard]] Rect arrow_rect(const Rect& column, bool up) const
    {
        const float h = plain_band_ ? 22.0f : 34.0f;
        return up ? Rect{column.x, column.y, column.width, h} : Rect{column.x, column.bottom() - h, column.width, h};
    }
    void focus_column(int index)
    {
        if (index >= 0 && index < static_cast<int>(columns_.size()))
        {
            (void)columns_[static_cast<std::size_t>(index)]->focus();
        }
    }
    void accept()
    {
        const std::vector<std::size_t> picked = selection();
        engine_.accept(picked);
    }
    void column_changed(std::size_t)
    {
        // A month or a year changes the days: refresh every column from the proposal.
        std::vector<PickerPart> parts = engine_.model().parts(selection());
        for (std::size_t i = 0; i < parts.size() && i < columns_.size(); ++i)
        {
            Column& column = *columns_[i];
            if (column.part.items != parts[i].items)
            {
                const float offset   = column.target - static_cast<float>(column.part.selected);
                column.part.items    = std::move(parts[i].items);
                column.part.selected = std::min(parts[i].selected,
                                                column.part.items.empty() ? 0 : column.part.items.size() - 1);
                column.target        = static_cast<float>(column.part.selected) + offset;
                column.position      = column.target;
            }
        }
        mark_dirty(DirtyFlags::Visual);
    }

    void for_each_child(ChildVisitor visit) const override
    {
        for (const auto& column : columns_)
        {
            visit(*column);
        }
        visit(const_cast<Button&>(accept_button_));  // NOLINT(cppcoreguidelines-pro-type-const-cast): a visitor
        visit(const_cast<Button&>(dismiss_button_)); // NOLINT(cppcoreguidelines-pro-type-const-cast): a visitor
    }
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Pane;
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext& context) const override
    {
        plain_band_ = picker_chrome(context.theme).plain_band;
        return Size{width_, PickerFlyoutMaxHeight};
    }
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override
    {
        const Rect              area{final_rect.x + 1.0f,
                                     final_rect.y + 1.0f,
                                     final_rect.width - 2.0f,
                                     final_rect.height - 2.0f - PickerButtonRow};
        std::vector<PickerPart> parts;
        for (const auto& column : columns_)
        {
            parts.push_back(column->part);
        }
        engine_.apply_column_widths(parts);
        const std::vector<Rect> rects = picker_columns(parts, area, 1.0f);
        for (std::size_t i = 0; i < columns_.size(); ++i)
        {
            (void)columns_[i]->measure(rects[i].size(), context.measurer, context.theme);
            columns_[i]->arrange(rects[i]);
        }
        const float row  = final_rect.bottom() - 1.0f - PickerButtonRow;
        const float half = (final_rect.width - 2.0f) * 0.5f;
        const Rect  accept{final_rect.x + 1.0f + 4.0f, row + 4.0f, half - 6.0f, PickerButtonRow - 8.0f};
        const Rect  dismiss{final_rect.x + 1.0f + half + 2.0f, row + 4.0f, half - 6.0f, PickerButtonRow - 8.0f};
        (void)accept_button_.measure(accept.size(), context.measurer, context.theme);
        accept_button_.arrange(accept);
        (void)dismiss_button_.measure(dismiss.size(), context.measurer, context.theme);
        dismiss_button_.arrange(dismiss);
    }
    void tick_override(Duration delta) override
    {
        bool moving = false;
        for (const auto& column : columns_)
        {
            Column& c = *column;
            if (c.position != c.target)
            {
                // An ease toward the goal, about a quarter second to settle.
                const float k = 1.0f - std::exp(-static_cast<float>(delta.count()) / 60.0f);
                c.position += (c.target - c.position) * k;
                if (std::abs(c.target - c.position) < 0.01f || !animations_enabled())
                {
                    c.position = c.target;
                }
                moving = true;
            }
            if (c.arrow_pressed != 0)
            {
                // A held arrow repeats after half a second, every 33 ms.
                c.arrow_ms += delta.count();
                while (c.arrow_ms >= PickerRepeatDelayMs)
                {
                    c.step(c.arrow_pressed);
                    c.arrow_ms -= PickerRepeatMs;
                }
                moving = true;
            }
        }
        if (moving)
        {
            mark_dirty(DirtyFlags::Visual);
        }
    }
    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme&       theme  = context.theme;
        const ThemeColors& c      = theme.colors();
        const Rect         b      = bounds();
        const float        radius = plain_band_ ? 0.0f : 4.0f;
        renderer.fill_rounded_rect(b, radius, c.overlay);
        renderer.stroke_rounded_rect(b.inset(Thickness{0.5f}), std::max(0.0f, radius - 0.5f), c.control_stroke, 1.0f);

        const Rect band = band_rect();
        if (plain_band_)
        {
            // Windows 10: a full-width accent wash under the items.
            renderer.fill_rounded_rect(Rect{b.x + 1.0f, band.y, b.width - 2.0f, band.height},
                                       0.0f,
                                       c.accent.with_alpha(theme.dark ? 0.6f : 0.4f));
        }

        for (const auto& column_ptr : columns_)
        {
            const Column& column = *column_ptr;
            const Rect    cb     = column.bounds();
            renderer.push_clip(cb);
            const int count = static_cast<int>(column.part.items.size());
            if (count > 0)
            {
                const TextStyle style = picker_text_style(theme, column.part.leading);
                const int       first = static_cast<int>(std::floor(column.position)) - 6;
                for (int v = first; v <= first + 12; ++v)
                {
                    if (!column.part.loops && (v < 0 || v >= count))
                    {
                        continue;
                    }
                    const int   item = ((v % count) + count) % count;
                    const float y    = band.y + (static_cast<float>(v) - column.position) * PickerItemHeight;
                    const Rect  row{cb.x, y, cb.width, PickerItemHeight};
                    const int   offset = v - static_cast<int>(std::lround(column.position));
                    if (column.pressed_row == offset || column.hover_row == offset)
                    {
                        const Color fill = column.pressed_row == offset ? theme.subtle_button_pressed.background
                                                                        : theme.subtle_button_hover.background;
                        renderer.fill_rounded_rect(row.inset(Symmetric{.horizontal = 4.0f, .vertical = 2.0f}), radius, fill);
                    }
                    renderer.draw_text(column.part.items[static_cast<std::size_t>(item)],
                                       picker_text_box(row, column.part.leading),
                                       c.text,
                                       style);
                }
            }
            renderer.pop_clip();
        }

        if (!plain_band_)
        {
            // Fluent 2: the band over the items, and the items inside it again in the text on it.
            renderer.fill_rounded_rect(band, 4.0f, c.accent);
            renderer.push_clip_rounded_rect(band, 4.0f);
            for (const auto& column_ptr : columns_)
            {
                const Column& column = *column_ptr;
                const int     count  = static_cast<int>(column.part.items.size());
                if (count == 0)
                {
                    continue;
                }
                const Rect      cb    = column.bounds();
                const TextStyle style = picker_text_style(theme, column.part.leading);
                for (int v = static_cast<int>(std::floor(column.position)) - 1;
                     v <= static_cast<int>(std::ceil(column.position)) + 1;
                     ++v)
                {
                    if (!column.part.loops && (v < 0 || v >= count))
                    {
                        continue;
                    }
                    const int   item = ((v % count) + count) % count;
                    const float y    = band.y + (static_cast<float>(v) - column.position) * PickerItemHeight;
                    renderer.draw_text(column.part.items[static_cast<std::size_t>(item)],
                                       picker_text_box(Rect{cb.x, y, cb.width, PickerItemHeight}, column.part.leading),
                                       c.text_on_accent,
                                       style);
                }
            }
            renderer.pop_clip();
        }

        // The dividers between the columns.
        for (std::size_t i = 1; i < columns_.size(); ++i)
        {
            const Rect cb = columns_[i]->bounds();
            renderer.fill_rounded_rect(Rect{cb.x - 1.0f, cb.y, 1.0f, cb.height}, 0.0f, c.divider);
        }

        // The arrow buttons of the column under the pointer.
        for (const auto& column_ptr : columns_)
        {
            const Column& column = *column_ptr;
            if (!column.hovered)
            {
                continue;
            }
            for (const bool up : {true, false})
            {
                const Rect r = arrow_rect(column.bounds(), up);
                renderer.fill_rounded_rect(r, 0.0f, c.overlay);
                const bool      pressed = column.arrow_pressed == (up ? -1 : 1);
                const IconGlyph glyph   = plain_band_ ? IconGlyph{up ? 0xE70Eu : 0xE70Du, theme.icon_font_family, 8.0f}
                                                      : (up ? glyphs::ScrollUp : glyphs::ScrollDown);
                draw_icon(renderer, glyph, r, pressed ? c.text : c.text_secondary, pressed ? 7.0f : 8.0f, theme.icon_font_family);
            }
        }

        // Accept and Dismiss under a divider.
        const float row = b.bottom() - 1.0f - PickerButtonRow;
        renderer.fill_rounded_rect(Rect{b.x + 1.0f, row, b.width - 2.0f, 1.0f}, 0.0f, c.divider);
        for (const Button* button : {&accept_button_, &dismiss_button_})
        {
            const Color fill = button->pressed   ? theme.subtle_button_pressed.background
                               : button->hovered ? theme.subtle_button_hover.background
                                                 : colors::Transparent;
            if (fill.a > 0.0f)
            {
                renderer.fill_rounded_rect(button->bounds(), radius, fill);
            }
        }
        draw_icon(renderer, PickerAcceptGlyph, accept_button_.bounds(), c.text, 0.0f, theme.icon_font_family);
        draw_icon(renderer, PickerDismissGlyph, dismiss_button_.bounds(), c.text, 0.0f, theme.icon_font_family);
    }

private:
    friend class Column;
    friend class Button;
    PickerEngine&                        engine_;
    std::vector<std::unique_ptr<Column>> columns_;
    Button                               accept_button_;
    Button                               dismiss_button_;
    float                                width_      = 296.0f;
    mutable bool                         plain_band_ = false;
};

// ---- the engine ----

PickerEngine::PickerEngine(Element& owner, PickerModel& model) : owner_(owner), model_(model)
{
}

PickerEngine::~PickerEngine()
{
    if (open_)
    {
        model_.close_popup();
    }
}

PickerEngine::Kind PickerEngine::kind_for(const Theme& theme) const
{
    switch (picker_chrome(theme).kind)
    {
        case ControlChrome::DateTimePickerChrome::Kind::Flyout:
            return Kind::Flyout;
        case ControlChrome::DateTimePickerChrome::Kind::ComboBoxes:
            return Kind::ComboBoxes;
        case ControlChrome::DateTimePickerChrome::Kind::Field:
            return Kind::Field;
    }
    return Kind::Flyout;
}

std::vector<PickerPart> PickerEngine::current_parts() const
{
    return model_.parts({});
}

Size PickerEngine::measure(Size available, const MeasureContext& context) const
{
    const Theme& theme = context.theme;
    const Kind   kind  = kind_for(theme);
    if (kind != kind_)
    {
        kind_ = kind;
        const_cast<PickerEngine*>(this)->sync_combos(); // NOLINT(cppcoreguidelines-pro-type-const-cast): the kind decides the parts
    }
    const float line = context.measurer.measure("", theme.text_style).height;

    if (kind_ == Kind::ComboBoxes)
    {
        float width  = 0.0f;
        float height = 0.0f;
        for (const auto& combo : combos_)
        {
            const Size s = combo->measure(Size{available.width, available.height}, context);
            width += std::max(PickerComboMinWidth, s.width);
            height = std::max(height, s.height);
        }
        width += PickerComboGap * static_cast<float>(combos_.empty() ? 0 : combos_.size() - 1);
        return Size{std::min(width, available.width), std::min(height, available.height)};
    }

    if (kind_ == Kind::Field)
    {
        // comctl32's field: the value in the short pattern, a drop-down button or spin buttons.
        const auto& era = controls(theme).dropdown_chrome;
        field_height_   = std::max(line + theme.sizing.combo_box_padding.value_or(theme.padding).vertical(),
                                   theme.sizing.combo_box_min_height.value_or(0.0f));
        button_width_   = era ? era->arrow_column : 17.0f;
        segment_widths_.clear();
        float text = 0.0f;
        for (const PickerSegment& segment : model_.segments())
        {
            const float w = context.measurer.measure(segment.text, theme.text_style).width;
            segment_widths_.push_back(w);
            text += w;
        }
        const float edge = 2.0f * theme.border_width;
        return Size{std::min(available.width, std::ceil(text + edge + 4.0f + 8.0f + button_width_)),
                    std::min(available.height, field_height_)};
    }

    // Fluent's field: a line of text, 3 above and 6 below, in the border.
    const float border = picker_chrome(theme).border;
    field_height_      = std::max(32.0f, line + 9.0f + 2.0f * border);
    // Each column as wide as its widest choice, the field as wide as its columns.
    const std::vector<PickerPart> parts = current_parts();
    column_widths_.clear();
    float width = 2.0f * border;
    for (const PickerPart& part : parts)
    {
        float widest = context.measurer.measure(part.placeholder, theme.text_style).width;
        for (const std::string& item : part.items)
        {
            widest = std::max(widest, context.measurer.measure(item, theme.text_style).width);
        }
        const float column = std::ceil(widest) + (part.leading ? 9.0f + 16.0f : PickerColumnPadding);
        column_widths_.push_back(column);
        width += column;
    }
    width += picker_chrome(theme).divider * static_cast<float>(parts.empty() ? 0 : parts.size() - 1);
    width = std::min(width, PickerMaxFieldWidth);
    return Size{std::min(width, available.width), std::min(field_height_, available.height)};
}

void PickerEngine::arrange(const ArrangeContext& context)
{
    if (kind_ == Kind::ComboBoxes)
    {
        const Rect b = owner_.bounds();
        float      x = b.x;
        for (const auto& combo : combos_)
        {
            const Size  s = combo->measure(Size{b.width, b.height}, context.measure_context());
            const float w = std::max(PickerComboMinWidth, s.width);
            combo->arrange(Rect{x, b.y, w, s.height});
            x += w + PickerComboGap;
        }
        return;
    }
    if (open_)
    {
        sync_popup(); // the flyout or calendar follows the field
    }
}

Rect PickerEngine::field_rect() const
{
    return owner_.bounds();
}

Rect PickerEngine::button_rect() const
{
    const Rect  box  = field_rect();
    const float edge = 2.0f; // the classic field edge
    return Rect{box.right() - edge - button_width_, box.y + edge, button_width_, std::max(0.0f, box.height - 2.0f * edge)};
}

void PickerEngine::render(Renderer& renderer, const Theme& theme) const
{
    if (kind_ == Kind::ComboBoxes)
    {
        // The drop-down lists, which the owner holds as its children.
        for (const auto& combo : combos_)
        {
            combo->render(renderer, RenderContext{theme});
        }
        return;
    }
    const Rect         box     = field_rect();
    const bool         enabled = owner_.is_enabled();
    const ThemeColors& c       = theme.colors();

    if (kind_ == Kind::Field)
    {
        const auto& era      = controls(theme).dropdown_chrome;
        const bool  calendar = model_.calendar_button();
        if (era && era->field_with_button)
        {
            draw_classic_combo_field(renderer,
                                     theme,
                                     *era,
                                     ClassicComboState{box, enabled, open_ || button_pressed_, false, false, {}, false});
        }
        else
        {
            const ButtonVisual& visual = theme.for_state(!enabled ? ButtonState::Disabled : ButtonState::Normal);
            renderer.fill_rounded_rect(box, 0.0f, enabled ? c.field : c.background);
            stroke_rounded_rect_inside(renderer,
                                       box,
                                       theme.resolved_button_corner_radius(),
                                       theme.surfaces.field_border,
                                       theme.border_width);
            if (calendar)
            {
                const Rect button = button_rect();
                draw_control_chrome(renderer,
                                    button,
                                    0.0f,
                                    open_ || button_pressed_ ? theme.button_pressed : visual,
                                    theme.border_width);
                if (era)
                {
                    draw_dropdown_arrow(renderer, *era, button, enabled);
                }
                else
                {
                    draw_classic_arrow(renderer, button, ArrowDirection::Down, visual.text, 4.0f);
                }
            }
        }
        if (!calendar)
        {
            // A time's spin buttons in place of the drop-down button, split in two.
            const Rect  b    = button_rect();
            const float half = std::floor(b.height * 0.5f);
            const Rect  up{b.x, b.y, b.width, half};
            const Rect  down{b.x, b.y + half, b.width, b.height - half};
            for (const bool is_up : {true, false})
            {
                const Rect r = is_up ? up : down;
                const ButtonState state = spin_pressed_ == (is_up ? 1 : -1) ? ButtonState::Pressed : ButtonState::Normal;
                if (!draw_scroll_arrow_button(renderer, theme, r, is_up ? ArrowDirection::Up : ArrowDirection::Down, state, enabled))
                {
                    const ButtonVisual& v = theme.for_state(enabled ? state : ButtonState::Disabled);
                    draw_control_chrome(renderer, r, 0.0f, v, theme.border_width);
                    draw_classic_arrow(renderer, r, is_up ? ArrowDirection::Up : ArrowDirection::Down, v.text, 3.0f);
                }
            }
        }

        // The value in the short pattern, the part being edited in the selection colors.
        const std::vector<PickerSegment> segments = model_.segments();
        TextStyle                        style    = theme.text_style;
        style.horizontal                          = TextAlign::Leading;
        float       x                             = box.x + 2.0f * theme.border_width + 2.0f;
        const bool  focused                       = owner_.is_focused() && enabled;
        const Color text = !enabled ? c.text_disabled : model_.has_value() ? c.field_text : c.text_disabled;
        for (std::size_t i = 0; i < segments.size() && i < segment_widths_.size(); ++i)
        {
            const float w = segment_widths_[i];
            const Rect  r{x, box.y, w, box.height};
            const bool  active = focused && segments[i].part >= 0 &&
                                 static_cast<std::size_t>(segments[i].part) == active_part_;
            if (active)
            {
                const float h = std::min(box.height - 4.0f, std::ceil(theme.text_style.font_size * 1.25f));
                renderer.fill_rounded_rect(Rect{x, box.center().y - h * 0.5f, w, h}, 0.0f, c.selection);
            }
            renderer.draw_text(segments[i].text, r, active ? c.selection_text : text, style);
            x += w;
        }
        return;
    }

    // Fluent's field: the button chrome, the parts in their columns with dividers between.
    const auto&         chrome = picker_chrome(theme);
    const ButtonState   state  = !enabled            ? ButtonState::Disabled
                                 : pressed_ || open_ ? ButtonState::Pressed
                                 : hovered_          ? ButtonState::Hover
                                                     : ButtonState::Normal;
    const ButtonVisual& visual = theme.for_state(state);
    draw_control_chrome(renderer, box, theme.corner_radius, visual, chrome.border);
    std::vector<PickerPart> parts = current_parts();
    apply_column_widths(parts);
    const Rect              inner   = box.inset(Thickness{chrome.border});
    const std::vector<Rect> columns = picker_columns(parts, inner, chrome.divider);
    const bool              value   = model_.has_value();
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        const TextStyle    style = picker_text_style(theme, parts[i].leading);
        const std::string& text  = value ? parts[i].items[parts[i].selected] : parts[i].placeholder;
        const Color        color = !enabled ? visual.text : value ? visual.text : c.text_secondary;
        renderer.draw_text(text, picker_text_box(columns[i], parts[i].leading), color, style);
        if (i > 0)
        {
            renderer.fill_rounded_rect(Rect{columns[i].x - chrome.divider, inner.y, chrome.divider, inner.height},
                                       0.0f,
                                       c.control_stroke);
        }
    }
}

// ---- input ----

bool PickerEngine::pointer(const PointerEvent& e)
{
    if (!owner_.is_enabled() || kind_ == Kind::ComboBoxes)
    {
        return false;
    }
    const bool inside = field_rect().contains(e.position);
    if (kind_ == Kind::Field)
    {
        if (e.button != PointerButton::Left)
        {
            return false;
        }
        const Rect b = button_rect();
        if (e.action == PointerAction::Down && inside)
        {
            if (b.contains(e.position))
            {
                if (model_.calendar_button())
                {
                    button_pressed_ = true;
                    set_open(!open_);
                }
                else
                {
                    spin_pressed_ = e.position.y < b.center().y ? 1 : -1;
                    spin_ms_      = 0.0;
                    step_part(spin_pressed_);
                }
                model_.invalidate(DirtyFlags::Visual);
                return true;
            }
            // A click on a part makes it the one the keys edit.
            float                            x        = field_rect().x + 4.0f;
            const std::vector<PickerSegment> segments = model_.segments();
            for (std::size_t i = 0; i < segments.size() && i < segment_widths_.size(); ++i)
            {
                if (e.position.x < x + segment_widths_[i] && segments[i].part >= 0)
                {
                    active_part_ = static_cast<std::size_t>(segments[i].part);
                    break;
                }
                x += segment_widths_[i];
            }
            typed_.clear();
            model_.invalidate(DirtyFlags::Visual);
            return true;
        }
        if (e.action == PointerAction::Up && (button_pressed_ || spin_pressed_ != 0))
        {
            button_pressed_ = false;
            spin_pressed_   = 0;
            model_.invalidate(DirtyFlags::Visual);
            return true;
        }
        return false;
    }
    switch (e.action)
    {
        case PointerAction::Move:
            if (inside != hovered_)
            {
                hovered_ = inside;
                model_.invalidate(DirtyFlags::Visual);
            }
            return false;
        case PointerAction::Leave:
            hovered_ = false;
            model_.invalidate(DirtyFlags::Visual);
            return false;
        case PointerAction::Down:
            if (e.button == PointerButton::Left && inside)
            {
                pressed_ = true;
                model_.invalidate(DirtyFlags::Visual);
                return true;
            }
            return false;
        case PointerAction::Up:
            if (e.button == PointerButton::Left && pressed_)
            {
                pressed_ = false;
                model_.invalidate(DirtyFlags::Visual);
                if (inside)
                {
                    set_open(!open_);
                }
                return true;
            }
            return false;
        case PointerAction::Wheel:
            return false;
    }
    return false;
}

void PickerEngine::step_part(int delta)
{
    std::vector<PickerPart> parts = current_parts();
    if (active_part_ >= parts.size() || parts[active_part_].items.empty())
    {
        return;
    }
    const int count = static_cast<int>(parts[active_part_].items.size());
    int       next  = static_cast<int>(parts[active_part_].selected) + delta;
    next            = ((next % count) + count) % count; // comctl32 wraps every part
    set_part_index(static_cast<std::size_t>(next));
}

void PickerEngine::set_part_index(std::size_t index)
{
    std::vector<PickerPart>  parts = current_parts();
    std::vector<std::size_t> selection;
    for (const PickerPart& part : parts)
    {
        selection.push_back(part.selected);
    }
    if (active_part_ < selection.size())
    {
        selection[active_part_] = index;
    }
    model_.commit(selection);
    model_.invalidate(DirtyFlags::Layout | DirtyFlags::Visual);
}

bool PickerEngine::key(const KeyEvent& e)
{
    if (e.action != KeyAction::Down || !owner_.is_enabled() || kind_ == Kind::ComboBoxes)
    {
        return false;
    }
    const bool plain = e.modifiers.none();
    const bool alt   = e.modifiers.alt && !e.modifiers.ctrl && !e.modifiers.shift;
    if (kind_ == Kind::Flyout)
    {
        if ((plain && (e.key == Key::Enter || e.key == Key::Space)) ||
            (alt && (e.key == Key::ArrowDown || e.key == Key::ArrowUp)))
        {
            set_open(true);
            return true;
        }
        return false;
    }
    // comctl32's field.
    if ((alt && e.key == Key::ArrowDown) || (plain && e.key == Key::F4))
    {
        if (model_.calendar_button())
        {
            set_open(true);
            return true;
        }
        return false;
    }
    if (!plain)
    {
        return false;
    }
    const std::size_t count = current_parts().size();
    switch (e.key)
    {
        case Key::ArrowLeft:
        case Key::ArrowRight:
            if (count > 0)
            {
                // The parts in the order the pattern writes them, wrapping around.
                std::vector<std::size_t> order;
                for (const PickerSegment& segment : model_.segments())
                {
                    if (segment.part >= 0)
                    {
                        order.push_back(static_cast<std::size_t>(segment.part));
                    }
                }
                const auto  it = std::find(order.begin(), order.end(), active_part_);
                std::size_t i  = it == order.end() ? 0 : static_cast<std::size_t>(it - order.begin());
                i            = e.key == Key::ArrowLeft ? (i + order.size() - 1) % order.size() : (i + 1) % order.size();
                active_part_ = order[i];
                typed_.clear();
                model_.invalidate(DirtyFlags::Visual);
            }
            return true;
        case Key::ArrowUp:
            step_part(1);
            return true;
        case Key::ArrowDown:
            step_part(-1);
            return true;
        case Key::Home:
        case Key::End:
        {
            const std::vector<PickerPart> parts = current_parts();
            if (active_part_ < parts.size() && !parts[active_part_].items.empty())
            {
                set_part_index(e.key == Key::Home ? 0 : parts[active_part_].items.size() - 1);
            }
            return true;
        }
        default:
            return false;
    }
}

bool PickerEngine::text(const TextInputEvent& e)
{
    if (kind_ != Kind::Field || !owner_.is_enabled() || e.text.size() != 1 || e.text[0] < '0' || e.text[0] > '9')
    {
        return false;
    }
    // Digits fill the part being edited; a pause starts a new number.
    typed_ += e.text;
    typed_ms_                        = 0.0;
    std::optional<std::size_t> index = model_.index_for_number(active_part_, std::stoi(typed_));
    if (!index)
    {
        typed_ = e.text;
        index  = model_.index_for_number(active_part_, std::stoi(typed_));
    }
    if (index)
    {
        set_part_index(*index);
    }
    return true;
}

void PickerEngine::tick(Duration delta)
{
    // The popup's content isn't in the window's tree walk: tick it from here, or its columns and
    // the calendar's month slide never move.
    if (open_ && flyout_ && kind_ == Kind::Flyout)
    {
        flyout_->tick(delta);
    }
    if (open_ && calendar_ && kind_ == Kind::Field)
    {
        calendar_->tick(delta);
    }
    if (!typed_.empty())
    {
        typed_ms_ += delta.count();
        if (typed_ms_ > PickerTypingResetMs)
        {
            typed_.clear();
        }
    }
    if (spin_pressed_ != 0)
    {
        spin_ms_ += delta.count();
        while (spin_ms_ >= PickerRepeatDelayMs)
        {
            step_part(spin_pressed_);
            spin_ms_ -= PickerRepeatMs * 3.0;
        }
        model_.request_tick();
    }
}

void PickerEngine::enabled_changed()
{
    hovered_ = pressed_ = button_pressed_ = false;
    spin_pressed_                         = 0;
    if (!owner_.is_enabled())
    {
        set_open(false);
    }
    for (const auto& combo : combos_)
    {
        combo->set_enabled(owner_.is_enabled());
    }
}

void PickerEngine::focus_changed()
{
    typed_.clear();
    model_.invalidate(DirtyFlags::Visual);
}

// ---- the popups ----

void PickerEngine::set_open(bool open)
{
    open = open && owner_.is_enabled() && kind_ != Kind::ComboBoxes;
    if (open == open_)
    {
        return;
    }
    if (open && kind_ == Kind::Field && !model_.calendar_button())
    {
        return; // a time has no drop-down
    }
    open_ = open;
    if (open_ && kind_ == Kind::Flyout)
    {
        if (!flyout_)
        {
            flyout_ = std::make_unique<PickerFlyout>(*this);
        }
        flyout_->set_parts(current_parts(), true);
    }
    if (open_ && kind_ == Kind::Field)
    {
        if (!calendar_)
        {
            calendar_               = std::make_unique<CalendarViewControl>();
            calendar_->popup_frame_ = true;
            calendar_->on_selected_dates_changed(
                [this](const std::vector<std::chrono::year_month_day>& dates)
                {
                    if (syncing_ || dates.empty())
                    {
                        return;
                    }
                    model_.commit_calendar_date(dates.front());
                    set_open(false);
                    model_.invalidate(DirtyFlags::Layout | DirtyFlags::Visual);
                });
        }
        syncing_        = true;
        const auto date = model_.calendar_date();
        calendar_->set_selected_dates(date ? std::vector{*date} : std::vector<std::chrono::year_month_day>{});
        calendar_->set_display_date(date.value_or(calendar_->today()));
        syncing_ = false;
    }
    sync_popup();
    model_.invalidate(DirtyFlags::Visual);
    if (on_open_changed)
    {
        on_open_changed(open_);
    }
}

void PickerEngine::sync_popup()
{
    if (!open_)
    {
        model_.close_popup();
        return;
    }
    if (kind_ == Kind::Field && calendar_)
    {
        model_.open_popup(PopupRequest{
            .content     = calendar_.get(),
            .placement   = PopupPlacement::Below,
            .takes_focus = true,
            .chrome      = {.corner_radius = [](const Theme&) { return 0.0f; }},
            .on_dismiss  = [this](PopupCloseReason) { set_open(false); },
        });
        return;
    }
    if (!flyout_)
    {
        return;
    }
    flyout_->set_width(field_rect().width);
    PickerFlyout* flyout = flyout_.get();
    model_.open_popup(PopupRequest{
        .content     = flyout,
        .takes_focus = true,
        .chrome = {.corner_radius = [](const Theme& theme) { return picker_chrome(theme).plain_band ? 0.0f : 4.0f; }},
        // The band lands on the field: the flyout is as wide as the field and centered on it.
        .place =
            [this, flyout](Element& content, Size client, const MeasureContext& context)
        {
            const Rect  field = field_rect();
            const Size  size  = content.measure(Size{field.width, PickerFlyoutMaxHeight}, context);
            const float y     = std::clamp(field.center().y - flyout->band_center_offset(),
                                           0.0f,
                                           std::max(0.0f, client.height - size.height));
            const float x     = std::clamp(field.x, 0.0f, std::max(0.0f, client.width - size.width));
            content.arrange(Rect{x, y, size.width, size.height});
            return true;
        },
        .on_dismiss = [this](PopupCloseReason) { dismiss(); },
    });
}

void PickerEngine::accept(std::span<const std::size_t> selection)
{
    model_.commit(selection);
    set_open(false);
    model_.invalidate(DirtyFlags::Layout | DirtyFlags::Visual);
}

void PickerEngine::dismiss()
{
    set_open(false);
}

void PickerEngine::value_changed()
{
    sync_combos();
    model_.invalidate(DirtyFlags::Layout | DirtyFlags::Visual);
}

// ---- Windows 8.1's drop-down lists ----

void PickerEngine::sync_combos()
{
    if (kind_ != Kind::ComboBoxes)
    {
        return;
    }
    const std::vector<PickerPart> parts = current_parts();
    while (combos_.size() < parts.size())
    {
        auto combo = std::make_unique<ComboBoxControl>();
        model_.adopt(*combo);
        combo->on_selection_changed(
            [this](std::optional<std::size_t>)
            {
                if (syncing_)
                {
                    return;
                }
                std::vector<std::size_t>      selection;
                const std::vector<PickerPart> now = current_parts();
                for (std::size_t i = 0; i < combos_.size() && i < now.size(); ++i)
                {
                    selection.push_back(combos_[i]->selected_index().value_or(now[i].selected));
                }
                model_.commit(selection);
                sync_combos();
                model_.invalidate(DirtyFlags::Layout | DirtyFlags::Visual);
            });
        combos_.push_back(std::move(combo));
    }
    combos_.resize(parts.size());
    syncing_ = true;
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        ComboBoxControl& combo = *combos_[i];
        if (combo.items() != parts[i].items)
        {
            combo.set_items(parts[i].items);
        }
        combo.set_placeholder_text(parts[i].placeholder);
        combo.set_selected_index(model_.has_value() ? std::optional<std::size_t>{parts[i].selected} : std::nullopt);
        combo.set_enabled(owner_.is_enabled());
        if (combo.accessibility().name != parts[i].name)
        {
            Accessibility a = combo.accessibility();
            a.name          = parts[i].name;
            combo.set_accessibility(std::move(a));
        }
    }
    syncing_ = false;
}

void PickerEngine::apply_column_widths(std::vector<PickerPart>& parts) const
{
    if (column_widths_.size() == parts.size())
    {
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            parts[i].weight = column_widths_[i];
        }
    }
}

// ---- tree ----

void PickerEngine::for_each_child(ChildVisitor visit) const
{
    if (kind_ == Kind::ComboBoxes)
    {
        for (const auto& combo : combos_)
        {
            visit(*combo);
        }
    }
}

void PickerEngine::collect_focusables(std::vector<Element*>& out)
{
    if (kind_ == Kind::ComboBoxes)
    {
        for (const auto& combo : combos_)
        {
            combo->collect_focusables(out);
        }
        return;
    }
    if (owner_.focusable())
    {
        out.push_back(&owner_);
    }
}

void PickerEngine::automation_children(std::vector<Element*>& out) const
{
    if (kind_ == Kind::ComboBoxes)
    {
        for (const auto& combo : combos_)
        {
            out.push_back(combo.get());
        }
        return;
    }
    if (open_ && flyout_ && kind_ == Kind::Flyout)
    {
        out.push_back(flyout_.get());
    }
    if (open_ && calendar_ && kind_ == Kind::Field)
    {
        out.push_back(calendar_.get());
    }
}

} // namespace ui::detail
