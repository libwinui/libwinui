// SPDX-License-Identifier: MIT
#include "winui/ui/media/AnimatedIcon.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <charconv>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{

constexpr std::string_view Infix       = "To";
constexpr std::string_view StartSuffix = "_Start";
constexpr std::string_view EndSuffix   = "_End";

constexpr std::size_t QueueLength     = 4;    // how many state changes queue up
constexpr double      SpeedUp         = 7.0;  // how much faster a queued transition plays
constexpr double      MinimumDuration = 20.0; // a segment shorter than this jumps to its end, ms

} // namespace

void AnimatedIconControl::set_state(std::string state)
{
    if (state == state_)
    {
        return;
    }
    state_                     = std::move(state);
    const std::string& pending = state_;

    // Waiting a layout pass out before applying is only needed for an owner
    // that sets states twice in a row. Nothing here does that, so apply at once.
    switch (queue_behavior_)
    {
        case AnimatedIconQueueBehavior::Cut:
            transition_and_update(current_state_, pending);
            break;
        case AnimatedIconQueueBehavior::QueueOne:
            if (playing_)
            {
                if (queued_.size() >= QueueLength)
                {
                    transition_and_update(current_state_, queued_.front());
                }
                queued_.push_back(pending);
            }
            else
            {
                transition_and_update(current_state_, pending);
            }
            break;
        case AnimatedIconQueueBehavior::SpeedUpQueueOne:
            if (playing_)
            {
                if (queued_.size() >= QueueLength)
                {
                    transition_and_update(current_state_, queued_.front(), SpeedUp);
                    queued_.push_back(pending);
                }
                else
                {
                    queued_.push_back(pending);
                    if (!speed_up_)
                    {
                        speed_up_ = true;
                        if (source_ != nullptr)
                        {
                            const auto&       markers  = source_->markers();
                            const std::string end_name = previous_state_ + std::string(Infix) + current_state_ +
                                                         std::string(EndSuffix);
                            if (const auto it = markers.find(end_name); it != markers.end())
                            {
                                play_segment(std::nullopt, it->second, SpeedUp);
                            }
                        }
                    }
                }
            }
            else
            {
                transition_and_update(current_state_, pending);
            }
            break;
    }
    mark_dirty(DirtyFlags::Visual);
}

void AnimatedIconControl::transition_and_update(const std::string& from, const std::string& to, double multiplier)
{
    // Move the state on before the segment's play completes, because
    // completing plays the next queued state, which must not still be this one. The copies are
    // needed: callers pass current_state_ and queued_.front(), which cleanup overwrites and pops.
    const std::string from_copy = from; // NOLINT(performance-unnecessary-copy-initialization)
    const std::string to_copy   = to;   // NOLINT(performance-unnecessary-copy-initialization)
    bool              cleaned   = false;
    Action            cleanup   = [this, &from_copy, &to_copy, &cleaned]
    {
        if (std::exchange(cleaned, true))
        {
            return;
        }
        previous_state_ = from_copy;
        current_state_  = to_copy;
        if (!queued_.empty())
        {
            queued_.pop_front();
        }
    };
    transition(from_copy, to_copy, multiplier, &cleanup);
    cleanup();
}

void AnimatedIconControl::transition(const std::string& from, const std::string& to, double multiplier, Action* cleanup)
{
    if (source_ == nullptr)
    {
        return;
    }
    const auto&       markers    = source_->markers();
    const std::string name       = from + std::string(Infix) + to;
    const std::string start_name = name + std::string(StartSuffix);
    const std::string end_name   = name + std::string(EndSuffix);
    const auto        start      = markers.find(start_name);
    const auto        end        = markers.find(end_name);

    const auto cut = [this](double value)
    {
        playing_  = false;
        progress_ = value;
    };

    if (start != markers.end() && end != markers.end())
    {
        play_segment(start->second, end->second, multiplier, cleanup);
        last_segment_start_ = start_name;
        last_segment_end_   = end_name;
    }
    else if (end != markers.end())
    {
        cut(end->second);
        last_segment_start_.clear();
        last_segment_end_ = end_name;
    }
    else if (start != markers.end())
    {
        cut(start->second);
        last_segment_start_ = start_name;
        last_segment_end_.clear();
    }
    else if (const auto whole = markers.find(name); whole != markers.end())
    {
        cut(whole->second);
        last_segment_start_.clear();
        last_segment_end_ = name;
    }
    else if (const auto state = markers.find(to); state != markers.end())
    {
        cut(state->second);
        last_segment_start_.clear();
        last_segment_end_ = to;
    }
    else
    {
        // Cut to the end of any transition ending in `to`. The first marker in key order wins,
        // since the walk goes over the ordered marker map.
        const std::string  fragment = std::string(Infix) + to + std::string(EndSuffix);
        const std::string* found    = nullptr;
        double             value    = 0.0;
        for (const auto& [key, marker] : markers)
        {
            if (key.find(fragment) != std::string::npos && (found == nullptr || key < *found))
            {
                found = &key;
                value = marker;
            }
        }
        if (found != nullptr)
        {
            cut(value);
            last_segment_start_.clear();
            last_segment_end_ = *found;
        }
        else
        {
            double     parsed = 0.0;
            const auto result = std::from_chars(to.data(), to.data() + to.size(), parsed);
            if (!to.empty() && result.ec == std::errc{} && result.ptr == to.data() + to.size())
            {
                play_segment(std::nullopt, parsed, multiplier, cleanup);
                last_segment_start_.clear();
                last_segment_end_ = to;
            }
            else
            {
                cut(0.0);
                last_segment_start_.clear();
                last_segment_end_ = "0.0";
            }
        }
    }
    last_segment_ = name;
}

void AnimatedIconControl::play_segment(std::optional<double> from, double to, double multiplier, Action* cleanup)
{
    const double length      = from ? std::abs(to - *from) : previous_segment_length_;
    previous_segment_length_ = length;
    const double duration    = source_ != nullptr ? source_->duration().count() * length / multiplier : 0.0;
    if (duration < MinimumDuration)
    {
        progress_ = to;
        playing_  = false;
        if (cleanup != nullptr && *cleanup)
        {
            (*cleanup)();
        }
        on_animation_completed();
        return;
    }
    from_        = from.value_or(progress_);
    to_          = to;
    duration_ms_ = duration;
    elapsed_ms_  = 0.0;
    progress_    = from_;
    playing_     = true;
}

void AnimatedIconControl::on_animation_completed()
{
    playing_ = false;
    switch (queue_behavior_)
    {
        case AnimatedIconQueueBehavior::Cut:
            break;
        case AnimatedIconQueueBehavior::QueueOne:
            if (!queued_.empty())
            {
                transition_and_update(current_state_, queued_.front());
            }
            break;
        case AnimatedIconQueueBehavior::SpeedUpQueueOne:
            if (!queued_.empty())
            {
                transition_and_update(current_state_, queued_.front(), queued_.size() == 1 || !speed_up_ ? 1.0 : SpeedUp);
            }
            break;
    }
}

Size AnimatedIconControl::measure_override(Size available, const MeasureContext&) const
{
    if (showing_source())
    {
        // Uniform: the natural size scaled by the smaller ratio, unscaled when neither axis is bounded.
        const Size visual = source_->natural_size();
        if (visual.width <= 0.0f || visual.height <= 0.0f)
        {
            return visual;
        }
        const bool w_bounded = std::isfinite(available.width);
        const bool h_bounded = std::isfinite(available.height);
        if (!w_bounded && !h_bounded)
        {
            return visual;
        }
        const float width_scale  = available.width / visual.width;
        const float height_scale = available.height / visual.height;
        if (!w_bounded)
        {
            return Size{visual.width * height_scale, available.height};
        }
        if (!h_bounded)
        {
            return Size{available.width, visual.height * width_scale};
        }
        return height_scale > width_scale ? Size{available.width, visual.height * width_scale}
                                          : Size{visual.width * height_scale, available.height};
    }
    if (fallback_)
    {
        Size desired   = fallback_.slot_size();
        desired.width  = std::min(desired.width, available.width);
        desired.height = std::min(desired.height, available.height);
        return desired;
    }
    return Size{};
}

void AnimatedIconControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    render_in(renderer, theme, foreground_.resolve(theme).value_or(theme.colors().text));
}

void AnimatedIconControl::render_in(Renderer& renderer, const Theme& theme, const Color& foreground) const
{
    if (showing_source())
    {
        const Size visual = source_->natural_size();
        if (visual.width <= 0.0f || visual.height <= 0.0f || bounds().empty())
        {
            return;
        }
        const float scale = std::min(bounds().width / visual.width, bounds().height / visual.height);
        const Size  drawn{visual.width * scale, visual.height * scale};
        const Rect  dest{bounds().x + (bounds().width - drawn.width) * 0.5f,
                         bounds().y + (bounds().height - drawn.height) * 0.5f,
                         drawn.width,
                         drawn.height};
        source_->render_icon_frame(renderer, dest, progress_, foreground);
        return;
    }
    if (fallback_)
    {
        fallback_.draw_in_slot(renderer, bounds(), foreground, 0.0f, theme.icon_font_family);
    }
}

void AnimatedIconControl::tick_override(Duration delta)
{
    if (!playing_)
    {
        return;
    }
    elapsed_ms_ += animation_delta(delta).count();
    const double t = duration_ms_ > 0.0 ? std::clamp(elapsed_ms_ / duration_ms_, 0.0, 1.0) : 1.0;
    progress_      = from_ + (to_ - from_) * t;
    mark_dirty(DirtyFlags::Visual);
    if (t >= 1.0)
    {
        progress_ = to_;
        on_animation_completed();
    }
}

std::unique_ptr<ui::Element> AnimatedIcon::build() &&
{
    auto node = std::make_unique<ui::AnimatedIconControl>();

    if (fallback.reactive() || fallback.initial())
    {
        apply(fallback, *node, [raw = node.get()](const IconSource& v) { raw->set_fallback(v); });
    }
    if (source.reactive() || source.initial() != nullptr)
    {
        apply(source, *node, [raw = node.get()](const AnimatedIconSource* const& v) { raw->set_source(v); });
    }
    apply(queue_behavior, *node, [raw = node.get()](const AnimatedIconQueueBehavior& v) { raw->set_queue_behavior(v); });
    apply(state, *node, [raw = node.get()](const std::string& v) { raw->set_state(v); });
    apply(foreground, *node, [raw = node.get()](const ui::Paint& v) { raw->set_foreground(v); });

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
