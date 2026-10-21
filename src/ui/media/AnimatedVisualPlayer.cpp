// SPDX-License-Identifier: MIT
#include "winui/ui/media/AnimatedVisualPlayer.hpp"

#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/AnimatedVisualSource.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panels/Panel.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include <cmath>

namespace ui
{

namespace
{
// A play shorter than this is not animated.
constexpr double MinimumPlayMs = 20.0;
// How close to an end of the play (0..1) counts as there.
constexpr double EndSlack = 1e-9;
} // namespace

void AnimatedVisualPlayerControl::set_source(const AnimatedVisualSource* source)
{
    // A new source stops the current play, then drops the old content and loads the new.
    stop();
    source_ = source;
    loaded_ = false;
    set_screen_tracking(false);

    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (source_ == nullptr)
    {
        return;
    }
    if (!source_->available())
    {
        // An unavailable source shows the fallback content. A source that comes back empty (below)
        // leaves the player as it was.
        show_fallback(true);
        return;
    }
    const Size size = source_->natural_size();
    if (size.width <= 0.0f || size.height <= 0.0f)
    {
        return;
    }

    show_fallback(false);
    loaded_ = true;
    // A play keeps time off screen but draws nothing there, so it has to know.
    set_screen_tracking(true);
    // Auto-play starts a looped play once the content is loaded.
    if (auto_play_)
    {
        play(0.0, 1.0, /*looped=*/true);
    }
}

void AnimatedVisualPlayerControl::show_fallback(bool fallen_back)
{
    if (fallen_back == fallen_back_)
    {
        return;
    }
    fallen_back_ = fallen_back;
    // Only showing content is a child: hidden fallback content is neither drawn nor offered input.
    if (fallen_back_ && fallback_)
    {
        adopt_child(std::move(fallback_));
    }
    else if (!fallen_back_ && !children_.empty())
    {
        fallback_ = std::move(children_.front());
        children_.clear();
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

Element* AnimatedVisualPlayerControl::set_fallback_content(std::unique_ptr<Element> content)
{
    children_.clear();
    fallback_.reset();
    if (!content)
    {
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return nullptr;
    }
    Element* observer = content.get();
    if (fallen_back_)
    {
        adopt_child(std::move(content));
    }
    else
    {
        set_parent_of(*content, this);
        fallback_ = std::move(content);
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return observer;
}

void AnimatedVisualPlayerControl::set_auto_play(bool auto_play)
{
    auto_play_ = auto_play;
    // Turning auto-play on starts a looped play when nothing is playing.
    if (auto_play_ && loaded_ && !pending_ && !playing_)
    {
        play(0.0, 1.0, /*looped=*/true);
    }
}

Duration AnimatedVisualPlayerControl::duration() const
{
    return loaded_ ? source_->duration() : Duration{};
}

void AnimatedVisualPlayerControl::play(double from_progress, double to_progress, bool looped)
{
    // A new play completes the current one first, rewinding the progress as stop() does.
    if (pending_ || playing_)
    {
        progress_ = 0.0;
        complete_play();
        mark_dirty(DirtyFlags::Visual);
    }
    if (to_progress == 0.0 && from_progress > 0.0)
    {
        to_progress = 1.0;
    }
    if (to_progress > 0.0 && from_progress == 1.0)
    {
        from_progress = 0.0;
    }
    from_progress_ = std::clamp(from_progress, 0.0, 1.0);
    to_progress_   = std::clamp(to_progress, 0.0, 1.0);
    looped_        = looped;
    paused_        = false;
    pending_       = true;
    if (loaded_)
    {
        start_play();
    }
}

void AnimatedVisualPlayerControl::start_play()
{
    // Sets up the play and starts its clock.
    const double span = from_progress_ > to_progress_ ? (1.0 - from_progress_) + to_progress_ : to_progress_ - from_progress_;
    play_ms_ = source_->duration().count() * span;
    if (play_ms_ < MinimumPlayMs)
    {
        set_progress(from_progress_);
        return;
    }
    pending_  = false;
    playing_  = true;
    time_     = playback_rate_ < 0.0 ? 1.0 : 0.0; // a reversed play starts at its end
    progress_ = progress_at(time_);
    mark_dirty(DirtyFlags::Visual);
}

void AnimatedVisualPlayerControl::complete_play()
{
    pending_ = false;
    playing_ = false;
    paused_  = false;
}

double AnimatedVisualPlayerControl::progress_at(double time) const
{
    // The key frames: from at 0 and to at 1, through the end and the start when from > to.
    if (from_progress_ <= to_progress_)
    {
        return from_progress_ + (to_progress_ - from_progress_) * time;
    }
    const double to_end = (1.0 - from_progress_) / ((1.0 - from_progress_) + to_progress_);
    if (time <= to_end)
    {
        return to_end > 0.0 ? from_progress_ + (1.0 - from_progress_) * (time / to_end) : 1.0;
    }
    return to_progress_ * (time - to_end) / (1.0 - to_end);
}

void AnimatedVisualPlayerControl::pause()
{
    if (pending_ || playing_)
    {
        paused_ = true;
    }
}

void AnimatedVisualPlayerControl::resume()
{
    if (pending_ || playing_)
    {
        paused_ = false;
    }
}

void AnimatedVisualPlayerControl::stop()
{
    if (pending_ || playing_)
    {
        set_progress(0.0); // where a play starts from, which is always 0
    }
}

void AnimatedVisualPlayerControl::set_progress(double progress)
{
    progress_ = std::clamp(progress, 0.0, 1.0);
    complete_play();
    mark_dirty(DirtyFlags::Visual);
}

void AnimatedVisualPlayerControl::tick_override(Duration delta)
{
    Panel::tick_override(delta);
    if (!playing_ || paused_ || play_ms_ <= 0.0)
    {
        return;
    }
    // A single play lands on its end with animations off; a loop keeps playing, like any other
    // indicator that something is going on.
    time_ += playback_rate_ * (looped_ ? delta.count() : animation_delta(delta).count()) / play_ms_;
    const bool forward = playback_rate_ > 0.0;
    // The slack keeps the rounding of a wake for the end from asking for another one.
    if ((forward && time_ >= 1.0 - EndSlack) || (playback_rate_ < 0.0 && time_ <= EndSlack))
    {
        if (looped_)
        {
            time_ -= std::floor(time_);
            if (!forward && time_ == 0.0)
            {
                time_ = 1.0; // running backwards, the loop restarts at the end
            }
        }
        else
        {
            // The batch completes where the clock ran out: `to` going forwards, `from` going back.
            progress_ = forward ? to_progress_ : from_progress_;
            complete_play();
            if (on_screen())
            {
                mark_dirty(DirtyFlags::Visual);
            }
            return;
        }
    }
    progress_ = progress_at(time_);
    if (on_screen())
    {
        mark_dirty(DirtyFlags::Visual);
        return;
    }
    // Off screen the play keeps real time and draws nothing, so the window may sleep. A play that
    // ends still ends on time: one wake for what is left of it.
    if (!looped_ && playback_rate_ != 0.0)
    {
        const double left = forward ? 1.0 - time_ : time_;
        request_tick_in(Duration{left * play_ms_ / std::abs(playback_rate_)});
    }
}

void AnimatedVisualPlayerControl::on_screen_changed(bool on_screen)
{
    // Back in view: draw the progress the clock reached while away, and tick on from there.
    if (on_screen)
    {
        mark_dirty(DirtyFlags::Visual);
        if (playing_ && !paused_)
        {
            request_tick_in(Duration{});
        }
    }
}

Size AnimatedVisualPlayerControl::measure_override(Size available, const MeasureContext& context) const
{
    if (fallen_back_)
    {
        return children_.empty() ? Size{} : children_.front()->measure(available, context);
    }
    if (!loaded_)
    {
        return {};
    }
    // Measure.
    const Size natural = source_->natural_size();
    const bool bounded = std::isfinite(available.width) && std::isfinite(available.height);
    switch (stretch_)
    {
        case Stretch::None:
            return Size{std::min(natural.width, available.width), std::min(natural.height, available.height)};
        case Stretch::Fill:
            if (bounded)
            {
                return available;
            }
            break; // an unbounded axis cannot be filled: Uniform
        case Stretch::UniformToFill:
            if (bounded)
            {
                const float width_scale  = available.width / natural.width;
                const float height_scale = available.height / natural.height;
                const Size measured = height_scale < width_scale ? Size{available.width, natural.height * width_scale}
                                                                 : Size{natural.width * height_scale, available.height};
                return Size{std::min(measured.width, available.width), std::min(measured.height, available.height)};
            }
            break;
        case Stretch::Uniform:
            break;
    }
    if (!std::isfinite(available.width) && !std::isfinite(available.height))
    {
        return natural; // both unbounded: nothing to fit to, and the arithmetic would overflow
    }
    if (!std::isfinite(available.width))
    {
        return Size{natural.width * available.height / natural.height, available.height};
    }
    if (!std::isfinite(available.height))
    {
        return Size{available.width, natural.height * available.width / natural.width};
    }
    const float width_scale  = available.width / natural.width;
    const float height_scale = available.height / natural.height;
    return height_scale > width_scale ? Size{available.width, natural.height * width_scale}
                                      : Size{natural.width * height_scale, available.height};
}

void AnimatedVisualPlayerControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    if (fallen_back_ && !children_.empty())
    {
        children_.front()->arrange(final_rect);
    }
}

void AnimatedVisualPlayerControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    if (fallen_back_)
    {
        Panel::render_override(renderer, context);
        return;
    }
    if (!loaded_)
    {
        return;
    }
    // Arrange: None sits at the top-left unscaled; the others are scaled and centered.
    const Size natural = source_->natural_size();
    Rect       dest{bounds().x, bounds().y, natural.width, natural.height};
    if (stretch_ != Stretch::None)
    {
        dest = fit_stretch(bounds(), natural, stretch_);
    }
    const bool clip = dest.width > bounds().width + 0.01f || dest.height > bounds().height + 0.01f;
    if (clip)
    {
        renderer.push_clip(bounds());
    }
    source_->render_frame(renderer, dest, progress_);
    if (clip)
    {
        renderer.pop_clip();
    }
}

std::unique_ptr<ui::Element> AnimatedVisualPlayer::build() &&
{
    auto node = std::make_unique<ui::AnimatedVisualPlayerControl>();

    // auto_play/playback_rate/stretch land before set_source(): set_source()'s auto-play branch
    // reads auto_play as already configured, and the play it starts reads the rate.
    apply(auto_play, *node, [raw = node.get()](const bool& v) { raw->set_auto_play(v); });
    apply(playback_rate, *node, [raw = node.get()](const double& v) { raw->set_playback_rate(v); });
    apply(stretch, *node, [raw = node.get()](const ui::Stretch& v) { raw->set_stretch(v); });
    if (fallback_content)
    {
        node->set_fallback_content(fallback_content.take());
    }
    if (source.reactive() || source.initial() != nullptr)
    {
        apply(source, *node, [raw = node.get()](const AnimatedVisualSource* const& v) { raw->set_source(v); });
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
