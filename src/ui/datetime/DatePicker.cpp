// SPDX-License-Identifier: MIT
#include "winui/ui/datetime/DatePicker.hpp"

#include "PickerEngine.hpp"
#include "winui/core/DateFormat.hpp"
#include "winui/core/Strings.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace ui
{

namespace
{
using std::chrono::year_month_day;

enum class DatePart
{
    Day,
    Month,
    Year,
};

unsigned date_picker_days_in(int y, unsigned m)
{
    return static_cast<unsigned>(
        std::chrono::year_month_day_last{std::chrono::year{y}, std::chrono::month_day_last{std::chrono::month{m}}}.day());
}

// The order the locale's short date writes day, month and year in.
std::vector<DatePart> date_picker_order(const std::string& pattern)
{
    std::vector<DatePart> order;
    bool                  quoted = false;
    for (const char c : pattern)
    {
        if (c == '\'')
        {
            quoted = !quoted;
            continue;
        }
        if (quoted)
        {
            continue;
        }
        const std::optional<DatePart> part = c == 'd'   ? std::optional{DatePart::Day}
                                             : c == 'M' ? std::optional{DatePart::Month}
                                             : c == 'y' ? std::optional{DatePart::Year}
                                                        : std::nullopt;
        if (part && std::find(order.begin(), order.end(), *part) == order.end())
        {
            order.push_back(*part);
        }
    }
    for (const DatePart part : {DatePart::Month, DatePart::Day, DatePart::Year})
    {
        if (std::find(order.begin(), order.end(), part) == order.end())
        {
            order.push_back(part);
        }
    }
    return order;
}
} // namespace

// What the engine asks of a DatePicker.
class DatePickerControl::Model final : public detail::PickerModel
{
public:
    explicit Model(DatePickerControl& owner) : owner_(owner)
    {
    }

    // The visible parts in the locale's order.
    [[nodiscard]] std::vector<DatePart> visible() const
    {
        std::vector<DatePart> out;
        for (const DatePart part : date_picker_order(regional_date_format().short_date_pattern))
        {
            const bool shown = part == DatePart::Day     ? owner_.day_visible_
                               : part == DatePart::Month ? owner_.month_visible_
                                                         : owner_.year_visible_;
            if (shown)
            {
                out.push_back(part);
            }
        }
        return out;
    }

    // The date a proposal stands for: each shown part from it, the rest from the date (or
    // today), the day kept where it fits.
    [[nodiscard]] year_month_day resolve(std::span<const std::size_t> proposal) const
    {
        const year_month_day base  = owner_.date_.value_or(owner_.clamp(ui::today()));
        int                  y     = static_cast<int>(base.year());
        unsigned             m     = static_cast<unsigned>(base.month());
        unsigned             d     = static_cast<unsigned>(base.day());
        const auto           shown = visible();
        for (std::size_t i = 0; i < proposal.size() && i < shown.size(); ++i)
        {
            switch (shown[i])
            {
                case DatePart::Day:
                    d = static_cast<unsigned>(proposal[i]) + 1;
                    break;
                case DatePart::Month:
                    m = static_cast<unsigned>(proposal[i]) + 1;
                    break;
                case DatePart::Year:
                    y = owner_.min_year() + static_cast<int>(proposal[i]);
                    break;
            }
        }
        m = std::clamp(m, 1u, 12u);
        d = std::clamp(d, 1u, date_picker_days_in(y, m));
        return year_month_day{std::chrono::year{y}, std::chrono::month{m}, std::chrono::day{d}};
    }

    [[nodiscard]] std::vector<detail::PickerPart> parts(std::span<const std::size_t> proposal) const override
    {
        const year_month_day            date    = resolve(proposal);
        const DateFormatSymbols&        symbols = regional_date_format();
        const auto&                     text    = strings().date_time;
        std::vector<detail::PickerPart> out;
        for (const DatePart part : visible())
        {
            detail::PickerPart p;
            switch (part)
            {
                case DatePart::Day:
                {
                    p.name = p.placeholder = text.day;
                    const unsigned days    = date_picker_days_in(static_cast<int>(date.year()),
                                                                 static_cast<unsigned>(date.month()));
                    for (unsigned i = 1; i <= days; ++i)
                    {
                        p.items.push_back(std::to_string(i));
                    }
                    p.selected = static_cast<unsigned>(date.day()) - 1;
                    p.weight   = 78.0f;
                    break;
                }
                case DatePart::Month:
                    p.name = p.placeholder = text.month;
                    p.items.assign(symbols.month_names.begin(), symbols.month_names.end());
                    p.selected = static_cast<unsigned>(date.month()) - 1;
                    p.weight   = 132.0f;
                    p.leading  = true;
                    break;
                case DatePart::Year:
                    p.name = p.placeholder = text.year;
                    for (int y = owner_.min_year(); y <= owner_.max_year(); ++y)
                    {
                        p.items.push_back(std::to_string(y));
                    }
                    p.selected = static_cast<std::size_t>(static_cast<int>(date.year()) - owner_.min_year());
                    p.loops    = false;
                    p.weight   = 78.0f;
                    break;
            }
            out.push_back(std::move(p));
        }
        return out;
    }
    [[nodiscard]] bool has_value() const override
    {
        return owner_.date_.has_value();
    }
    void commit(std::span<const std::size_t> selection) override
    {
        owner_.set_selected_date(resolve(selection));
    }
    [[nodiscard]] std::vector<detail::PickerSegment> segments() const override
    {
        // The short pattern, its letters standing for the parts.
        const year_month_day date    = resolve({});
        const std::string&   pattern = regional_date_format().short_date_pattern;
        const auto           shown   = visible();
        const auto           index   = [&](DatePart part)
        {
            const auto it = std::find(shown.begin(), shown.end(), part);
            return it == shown.end() ? -1 : static_cast<int>(it - shown.begin());
        };
        std::vector<detail::PickerSegment> out;
        std::string                        literal;
        const auto                         flush = [&]
        {
            if (!literal.empty())
            {
                out.push_back(detail::PickerSegment{format_date(date, literal), -1});
                literal.clear();
            }
        };
        for (std::size_t i = 0; i < pattern.size();)
        {
            const char c = pattern[i];
            if (c == '\'')
            {
                // Quoted text stays literal, quotes and all, for format_date() to unquote.
                const std::size_t end  = pattern.find('\'', i + 1);
                const std::size_t stop = end == std::string::npos ? pattern.size() : end + 1;
                literal += pattern.substr(i, stop - i);
                i = stop;
                continue;
            }
            if (c != 'd' && c != 'M' && c != 'y')
            {
                literal += c;
                ++i;
                continue;
            }
            std::size_t n = 1;
            while (i + n < pattern.size() && pattern[i + n] == c)
            {
                ++n;
            }
            flush();
            const DatePart part = c == 'd' ? DatePart::Day : c == 'M' ? DatePart::Month : DatePart::Year;
            out.push_back(detail::PickerSegment{format_date(date, pattern.substr(i, n)), index(part)});
            i += n;
        }
        flush();
        return out;
    }
    [[nodiscard]] std::optional<std::size_t> index_for_number(std::size_t part, int number) const override
    {
        const auto shown = visible();
        if (part >= shown.size())
        {
            return std::nullopt;
        }
        const year_month_day date = resolve({});
        switch (shown[part])
        {
            case DatePart::Day:
                if (number >= 1 &&
                    static_cast<unsigned>(number) <=
                        date_picker_days_in(static_cast<int>(date.year()), static_cast<unsigned>(date.month())))
                {
                    return static_cast<std::size_t>(number - 1);
                }
                return std::nullopt;
            case DatePart::Month:
                return number >= 1 && number <= 12 ? std::optional<std::size_t>{static_cast<std::size_t>(number - 1)}
                                                   : std::nullopt;
            case DatePart::Year:
                if (number >= owner_.min_year() && number <= owner_.max_year())
                {
                    return static_cast<std::size_t>(number - owner_.min_year());
                }
                return std::nullopt;
        }
        return std::nullopt;
    }
    [[nodiscard]] bool calendar_button() const override
    {
        return true;
    }
    [[nodiscard]] std::optional<year_month_day> calendar_date() const override
    {
        return owner_.date_;
    }
    void commit_calendar_date(year_month_day date) override
    {
        owner_.set_selected_date(date);
    }
    [[nodiscard]] std::string accept_name() const override
    {
        return strings().date_time.accept;
    }
    [[nodiscard]] std::string dismiss_name() const override
    {
        return strings().date_time.dismiss;
    }
    void adopt(Element& part) override
    {
        DatePickerControl::set_parent_of(part, &owner_);
    }
    void invalidate(DirtyFlags flags) override
    {
        owner_.mark_dirty(flags);
    }
    void open_popup(PopupRequest request) override
    {
        owner_.open_popup(std::move(request));
    }
    void close_popup() override
    {
        owner_.close_popup();
    }
    void request_tick() override
    {
        owner_.request_tick();
    }

private:
    DatePickerControl& owner_;
};

DatePickerControl::DatePickerControl() :
    model_(std::make_unique<Model>(*this)),
    engine_(std::make_unique<detail::PickerEngine>(*this, *model_))
{
    format_changed_  = regional_date_format_changed().observe([this] { engine_->value_changed(); });
    strings_changed_ = strings_changed().observe([this] { engine_->value_changed(); });
}

DatePickerControl::~DatePickerControl() = default;

int DatePickerControl::min_year() const
{
    return min_year_.value_or(static_cast<int>(ui::today().year()) - 100);
}

int DatePickerControl::max_year() const
{
    return std::max(min_year(), max_year_.value_or(static_cast<int>(ui::today().year()) + 100));
}

std::chrono::year_month_day DatePickerControl::clamp(std::chrono::year_month_day date) const
{
    const year_month_day lo{std::chrono::year{min_year()}, std::chrono::January, std::chrono::day{1}};
    const year_month_day hi{std::chrono::year{max_year()}, std::chrono::December, std::chrono::day{31}};
    if (!date.ok())
    {
        return lo;
    }
    return std::chrono::sys_days{date} < std::chrono::sys_days{lo} ? lo
           : std::chrono::sys_days{date} > std::chrono::sys_days{hi}
               ? hi
               : date;
}

void DatePickerControl::set_selected_date(std::optional<std::chrono::year_month_day> date)
{
    if (date)
    {
        *date = clamp(*date);
    }
    if (date == date_)
    {
        return;
    }
    date_ = date;
    engine_->value_changed();
    if (on_selected_date_changed_)
    {
        on_selected_date_changed_(date_);
    }
}

void DatePickerControl::set_year_range(std::optional<int> min, std::optional<int> max)
{
    if (min == min_year_ && max == max_year_)
    {
        return;
    }
    min_year_ = min;
    max_year_ = max;
    engine_->value_changed();
    set_selected_date(date_);
}

void DatePickerControl::set_parts_visible(bool day, bool month, bool year)
{
    if (day == day_visible_ && month == month_visible_ && year == year_visible_)
    {
        return;
    }
    day_visible_   = day;
    month_visible_ = month;
    year_visible_  = year;
    engine_->value_changed();
}

void DatePickerControl::set_header(std::string text)
{
    if (header_.set_text(std::move(text)))
    {
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

bool DatePickerControl::is_open() const
{
    return engine_->is_open();
}

void DatePickerControl::set_open(bool open)
{
    engine_->set_open(open);
}

std::string DatePickerControl::text_value() const
{
    return date_ ? format_date(*date_, regional_date_format().long_date_pattern) : std::string{};
}

Size DatePickerControl::measure_override(Size available, const MeasureContext& context) const
{
    const Size header = header_.measure(available.width, context);
    available         = ControlHeader::below(available, header);
    return ControlHeader::above(engine_->measure(available, context), header);
}

void DatePickerControl::arrange_override(const Rect&, const ArrangeContext& context)
{
    header_.arrange(*this);
    engine_->arrange(context);
}

void DatePickerControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    header_.render(renderer, context.theme, is_enabled());
    engine_->render(renderer, context.theme);
}

bool DatePickerControl::pointer_override(const PointerEvent& e)
{
    return engine_->pointer(e);
}

bool DatePickerControl::key_override(const KeyEvent& e)
{
    return engine_->key(e);
}

bool DatePickerControl::text_override(const TextInputEvent& e)
{
    return engine_->text(e);
}

void DatePickerControl::tick_override(Duration delta)
{
    engine_->tick(delta);
}

void DatePickerControl::enabled_changed()
{
    engine_->enabled_changed();
    mark_dirty(DirtyFlags::Visual);
}

void DatePickerControl::focus_changed(bool)
{
    engine_->focus_changed();
}

void DatePickerControl::for_each_child(ChildVisitor visit) const
{
    engine_->for_each_child(visit);
}

void DatePickerControl::collect_focusables(std::vector<Element*>& out)
{
    engine_->collect_focusables(out);
}

void DatePickerControl::automation_children(std::vector<Element*>& out) const
{
    engine_->automation_children(out);
}

std::unique_ptr<ui::Element> DatePicker::build() &&
{
    auto node = std::make_unique<ui::DatePickerControl>();
    apply(header, *node, [raw = node.get()](const std::string& v) { raw->set_header(v); });
    // The years first, so the date is clamped to them.
    apply(min_year, *node, [raw = node.get()](const std::optional<int>& v) { raw->set_year_range(v, raw->max_year_); });
    apply(max_year, *node, [raw = node.get()](const std::optional<int>& v) { raw->set_year_range(raw->min_year_, v); });
    apply(day_visible,
          *node,
          [raw = node.get()](const bool& v) { raw->set_parts_visible(v, raw->month_visible_, raw->year_visible_); });
    apply(month_visible,
          *node,
          [raw = node.get()](const bool& v) { raw->set_parts_visible(raw->day_visible_, v, raw->year_visible_); });
    apply(year_visible,
          *node,
          [raw = node.get()](const bool& v) { raw->set_parts_visible(raw->day_visible_, raw->month_visible_, v); });

    const auto write = ui::shared_writer(selected_date);
    apply(selected_date,
          *node,
          [raw = node.get(), write](const std::optional<std::chrono::year_month_day>& v)
          {
              raw->set_selected_date(v);
              if (write && raw->selected_date() != v)
              {
                  (*write)(raw->selected_date());
              }
          });
    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });
    if (auto changed = ui::write_back(write, std::move(on_selected_date_changed)))
    {
        node->on_selected_date_changed(std::move(changed));
    }
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
