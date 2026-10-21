// SPDX-License-Identifier: MIT
#include "winui/ui/datetime/TimePicker.hpp"

#include "PickerEngine.hpp"
#include "winui/core/DateFormat.hpp"
#include "winui/core/Strings.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <ctime>

namespace ui
{

namespace
{
enum class TimePart
{
    Hour,
    Minute,
    Period,
};

// The order the locale's short time writes hour, minute and AM/PM in.
std::vector<TimePart> time_picker_order(const std::string& pattern, bool twelve)
{
    std::vector<TimePart> order;
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
        const std::optional<TimePart> part = (c == 'h' || c == 'H') ? std::optional{TimePart::Hour}
                                             : c == 'm'             ? std::optional{TimePart::Minute}
                                             : c == 't'             ? std::optional{TimePart::Period}
                                                                    : std::nullopt;
        if (part && std::find(order.begin(), order.end(), *part) == order.end())
        {
            order.push_back(*part);
        }
    }
    for (const TimePart part : {TimePart::Hour, TimePart::Minute, TimePart::Period})
    {
        if (std::find(order.begin(), order.end(), part) == order.end())
        {
            order.push_back(part);
        }
    }
    if (!twelve)
    {
        order.erase(std::remove(order.begin(), order.end(), TimePart::Period), order.end());
    }
    return order;
}

std::chrono::minutes time_picker_now()
{
    const std::time_t now = std::time(nullptr);
    std::tm           local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return std::chrono::hours{local.tm_hour} + std::chrono::minutes{local.tm_min};
}

std::string two_digits(int n)
{
    return (n < 10 ? "0" : "") + std::to_string(n);
}
} // namespace

// What the engine asks of a TimePicker.
class TimePickerControl::Model final : public detail::PickerModel
{
public:
    explicit Model(TimePickerControl& owner) : owner_(owner)
    {
    }

    [[nodiscard]] bool twelve() const
    {
        return owner_.clock() == TimePickerClock::TwelveHour;
    }
    [[nodiscard]] std::vector<TimePart> visible() const
    {
        return time_picker_order(regional_date_format().short_time_pattern, twelve());
    }
    [[nodiscard]] int step() const
    {
        return owner_.increment_ == 0 ? 60 : owner_.increment_;
    }

    // The time a proposal stands for: each part from it, the rest from the time (or now).
    [[nodiscard]] std::chrono::minutes resolve(std::span<const std::size_t> proposal) const
    {
        const long long base   = owner_.time_.value_or(owner_.coerce(time_picker_now())).count();
        int             hour   = static_cast<int>(base / 60);
        int             minute = static_cast<int>(base % 60);
        const auto      shown  = visible();
        bool            pm     = hour >= 12;
        int             hour12 = hour % 12;
        for (std::size_t i = 0; i < proposal.size() && i < shown.size(); ++i)
        {
            switch (shown[i])
            {
                case TimePart::Hour:
                    if (twelve())
                    {
                        hour12 = static_cast<int>(proposal[i]) % 12; // "12" first, then 1 to 11
                    }
                    else
                    {
                        hour = static_cast<int>(proposal[i]) % 24;
                    }
                    break;
                case TimePart::Minute:
                    minute = std::min(59, static_cast<int>(proposal[i]) * step());
                    break;
                case TimePart::Period:
                    pm = proposal[i] == 1;
                    break;
            }
        }
        if (twelve())
        {
            hour = hour12 + (pm ? 12 : 0);
        }
        return std::chrono::minutes{hour * 60 + minute};
    }

    [[nodiscard]] std::vector<detail::PickerPart> parts(std::span<const std::size_t> proposal) const override
    {
        const long long                 t       = resolve(proposal).count();
        const int                       hour    = static_cast<int>(t / 60);
        const int                       minute  = static_cast<int>(t % 60);
        const DateFormatSymbols&        symbols = regional_date_format();
        const auto&                     text    = strings().date_time;
        std::vector<detail::PickerPart> out;
        for (const TimePart part : visible())
        {
            detail::PickerPart p;
            switch (part)
            {
                case TimePart::Hour:
                    p.name = p.placeholder = text.hour;
                    if (twelve())
                    {
                        p.items.push_back("12");
                        for (int h = 1; h <= 11; ++h)
                        {
                            p.items.push_back(std::to_string(h));
                        }
                        p.selected = static_cast<std::size_t>(hour % 12);
                    }
                    else
                    {
                        for (int h = 0; h <= 23; ++h)
                        {
                            p.items.push_back(std::to_string(h));
                        }
                        p.selected = static_cast<std::size_t>(hour);
                    }
                    break;
                case TimePart::Minute:
                    p.name = p.placeholder = text.minute;
                    for (int m = 0; m < 60; m += step())
                    {
                        p.items.push_back(two_digits(m));
                    }
                    p.selected = static_cast<std::size_t>(minute / step());
                    p.loops    = owner_.increment_ != 0;
                    break;
                case TimePart::Period:
                    p.name        = text.period;
                    p.placeholder = symbols.am_designator;
                    p.items       = {symbols.am_designator, symbols.pm_designator};
                    p.selected    = hour >= 12 ? 1 : 0;
                    p.loops       = false;
                    break;
            }
            out.push_back(std::move(p));
        }
        return out;
    }
    [[nodiscard]] bool has_value() const override
    {
        return owner_.time_.has_value();
    }
    void commit(std::span<const std::size_t> selection) override
    {
        owner_.set_selected_time(resolve(selection));
    }
    [[nodiscard]] std::vector<detail::PickerSegment> segments() const override
    {
        const std::chrono::minutes time    = resolve({});
        std::string                pattern = regional_date_format().short_time_pattern;
        if (twelve() != regional_date_format().twelve_hour_clock())
        {
            pattern = twelve() ? "h:mm tt" : "HH:mm";
        }
        const auto shown = visible();
        const auto index = [&](TimePart part)
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
                out.push_back(detail::PickerSegment{format_time(time, literal), -1});
                literal.clear();
            }
        };
        for (std::size_t i = 0; i < pattern.size();)
        {
            const char c = pattern[i];
            if (c == '\'')
            {
                const std::size_t end  = pattern.find('\'', i + 1);
                const std::size_t stop = end == std::string::npos ? pattern.size() : end + 1;
                literal += pattern.substr(i, stop - i);
                i = stop;
                continue;
            }
            if (c != 'h' && c != 'H' && c != 'm' && c != 't' && c != 's')
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
            const TimePart part = (c == 'h' || c == 'H') ? TimePart::Hour
                                  : c == 't'             ? TimePart::Period
                                                         : TimePart::Minute;
            out.push_back(detail::PickerSegment{format_time(time, pattern.substr(i, n)), c == 's' ? -1 : index(part)});
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
        switch (shown[part])
        {
            case TimePart::Hour:
                if (twelve())
                {
                    return number >= 1 && number <= 12
                               ? std::optional<std::size_t>{static_cast<std::size_t>(number % 12)}
                               : std::nullopt;
                }
                return number >= 0 && number <= 23 ? std::optional<std::size_t>{static_cast<std::size_t>(number)}
                                                   : std::nullopt;
            case TimePart::Minute:
                return number >= 0 && number <= 59
                           ? std::optional<std::size_t>{static_cast<std::size_t>(number / step())}
                           : std::nullopt;
            case TimePart::Period:
                return std::nullopt;
        }
        return std::nullopt;
    }
    [[nodiscard]] bool calendar_button() const override
    {
        return false;
    }
    [[nodiscard]] std::optional<std::chrono::year_month_day> calendar_date() const override
    {
        return std::nullopt;
    }
    void commit_calendar_date(std::chrono::year_month_day) override
    {
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
        TimePickerControl::set_parent_of(part, &owner_);
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
    TimePickerControl& owner_;
};

TimePickerControl::TimePickerControl() :
    model_(std::make_unique<Model>(*this)),
    engine_(std::make_unique<detail::PickerEngine>(*this, *model_))
{
    format_changed_  = regional_date_format_changed().observe([this] { engine_->value_changed(); });
    strings_changed_ = strings_changed().observe([this] { engine_->value_changed(); });
}

TimePickerControl::~TimePickerControl() = default;

TimePickerClock TimePickerControl::clock() const
{
    return clock_.value_or(
        regional_date_format().twelve_hour_clock() ? TimePickerClock::TwelveHour : TimePickerClock::TwentyFourHour);
}

void TimePickerControl::set_clock(std::optional<TimePickerClock> clock)
{
    if (clock != clock_)
    {
        clock_ = clock;
        engine_->value_changed();
    }
}

std::chrono::minutes TimePickerControl::coerce(std::chrono::minutes time) const
{
    long long m    = ((time.count() % 1440) + 1440) % 1440;
    const int step = increment_ == 0 ? 60 : increment_;
    m -= (m % 60) % step;
    return std::chrono::minutes{m};
}

void TimePickerControl::set_selected_time(std::optional<std::chrono::minutes> time)
{
    if (time)
    {
        *time = coerce(*time);
    }
    if (time == time_)
    {
        return;
    }
    time_ = time;
    engine_->value_changed();
    if (on_selected_time_changed_)
    {
        on_selected_time_changed_(time_);
    }
}

void TimePickerControl::set_minute_increment(int increment)
{
    if (increment < 0 || increment > 59 || increment == increment_)
    {
        return;
    }
    increment_ = increment;
    engine_->value_changed();
    set_selected_time(time_);
}

void TimePickerControl::set_header(std::string text)
{
    if (header_.set_text(std::move(text)))
    {
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

bool TimePickerControl::is_open() const
{
    return engine_->is_open();
}

void TimePickerControl::set_open(bool open)
{
    engine_->set_open(open);
}

std::string TimePickerControl::text_value() const
{
    return time_ ? format_time(*time_, regional_date_format().short_time_pattern) : std::string{};
}

Size TimePickerControl::measure_override(Size available, const MeasureContext& context) const
{
    const Size header = header_.measure(available.width, context);
    available         = ControlHeader::below(available, header);
    return ControlHeader::above(engine_->measure(available, context), header);
}

void TimePickerControl::arrange_override(const Rect&, const ArrangeContext& context)
{
    header_.arrange(*this);
    engine_->arrange(context);
}

void TimePickerControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    header_.render(renderer, context.theme, is_enabled());
    engine_->render(renderer, context.theme);
}

bool TimePickerControl::pointer_override(const PointerEvent& e)
{
    return engine_->pointer(e);
}

bool TimePickerControl::key_override(const KeyEvent& e)
{
    return engine_->key(e);
}

bool TimePickerControl::text_override(const TextInputEvent& e)
{
    return engine_->text(e);
}

void TimePickerControl::tick_override(Duration delta)
{
    engine_->tick(delta);
}

void TimePickerControl::enabled_changed()
{
    engine_->enabled_changed();
    mark_dirty(DirtyFlags::Visual);
}

void TimePickerControl::focus_changed(bool)
{
    engine_->focus_changed();
}

void TimePickerControl::for_each_child(ChildVisitor visit) const
{
    engine_->for_each_child(visit);
}

void TimePickerControl::collect_focusables(std::vector<Element*>& out)
{
    engine_->collect_focusables(out);
}

void TimePickerControl::automation_children(std::vector<Element*>& out) const
{
    engine_->automation_children(out);
}

std::unique_ptr<ui::Element> TimePicker::build() &&
{
    auto node = std::make_unique<ui::TimePickerControl>();
    apply(header, *node, [raw = node.get()](const std::string& v) { raw->set_header(v); });
    apply(clock, *node, [raw = node.get()](const std::optional<TimePickerClock>& v) { raw->set_clock(v); });
    // The increment first, so the time is rounded to it.
    apply(minute_increment, *node, [raw = node.get()](const int& v) { raw->set_minute_increment(v); });
    const auto write = ui::shared_writer(selected_time);
    apply(selected_time,
          *node,
          [raw = node.get(), write](const std::optional<std::chrono::minutes>& v)
          {
              raw->set_selected_time(v);
              if (write && raw->selected_time() != v)
              {
                  (*write)(raw->selected_time());
              }
          });
    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });
    if (auto changed = ui::write_back(write, std::move(on_selected_time_changed)))
    {
        node->on_selected_time_changed(std::move(changed));
    }
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
