// SPDX-License-Identifier: MIT
#include "winui/ui/media/Image.hpp"

#include "../WindowRegistry.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/ImageSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/SvgImage.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <utility>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{

// Equal within 0.01, near enough for DIPs.
bool close(float a, float b)
{
    return std::fabs(a - b) < 0.01f;
}

Rect intersect(const Rect& a, const Rect& b)
{
    const float left   = std::max(a.left(), b.left());
    const float top    = std::max(a.top(), b.top());
    const float right  = std::min(a.right(), b.right());
    const float bottom = std::min(a.bottom(), b.bottom());
    return Rect{left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
}

// The source rect of `image_rect` (a part of the picture's pixels) that lands on `part` of
// `dest`, where the whole of `image_rect` is drawn onto the whole of `dest`.
Rect source_for(const Rect& image_rect, const Rect& dest, const Rect& part)
{
    const float sx = dest.width > 0.0f ? image_rect.width / dest.width : 0.0f;
    const float sy = dest.height > 0.0f ? image_rect.height / dest.height : 0.0f;
    return Rect{image_rect.x + (part.x - dest.x) * sx, image_rect.y + (part.y - dest.y) * sy, part.width * sx, part.height * sy};
}

// Draws `image_rect` of `image` onto `dest`, showing only what falls inside `visible`.
void draw_clipped(Renderer& renderer, const Bitmap& image, const Rect& image_rect, const Rect& dest, const Rect& visible)
{
    if (image_rect.width <= 0.0f || image_rect.height <= 0.0f)
    {
        return;
    }
    const Rect part = intersect(dest, visible);
    if (part.width <= 0.0f || part.height <= 0.0f)
    {
        return;
    }
    renderer.draw_image_rect(image, source_for(image_rect, dest, part), part);
}

} // namespace

ImageControl::ImageControl(ImageSource source)
{
    set_source(std::move(source));
}

void ImageControl::set_source(ImageSource source)
{
    source_       = std::move(source);
    seen_load_    = source_.load_state();
    load_watched_ = false; // a tick follows whatever set it, and tick_override() watches the decode from there
    opened();
}

void ImageControl::opened()
{
    frame_      = 0;
    loop_       = 0;
    frame_time_ = 0.0;
    // A source plays when it opens if it is animated and auto-plays; a source that
    // does not auto-play waits, stopped, on its first frame.
    const AnimatedImage* animated = source_.animated();
    const bool           plays    = animated != nullptr && animated->animated() && animated->auto_play;
    is_playing_                   = plays;
    stopped_                      = !plays;
    // Only a picture that moves needs to know whether anyone sees it.
    set_screen_tracking(animated != nullptr && animated->animated());
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void ImageControl::play()
{
    if (!is_animated())
    {
        return;
    }
    is_playing_ = true;
    // Playing restarts only a stopped animation, and the frame it restarts on shows for its whole
    // delay.
    if (stopped_)
    {
        stopped_    = false;
        frame_time_ = 0.0;
        mark_dirty(DirtyFlags::Visual);
    }
}

void ImageControl::stop()
{
    if (!is_animated())
    {
        return;
    }
    is_playing_ = false;
    restart_animation();
    stopped_ = true;
}

void ImageControl::restart_animation()
{
    frame_      = 0;
    loop_       = 0;
    frame_time_ = 0.0;
    mark_dirty(DirtyFlags::Visual);
}

void ImageControl::tick_override(Duration delta)
{
    if (seen_load_ == ImageLoadState::Loading)
    {
        seen_load_ = source_.load_state();
        if (seen_load_ == ImageLoadState::Loading)
        {
            watch_load();
            return;
        }
        opened();
        if (seen_load_ == ImageLoadState::Loaded && on_image_opened_)
        {
            on_image_opened_();
        }
        else if (seen_load_ == ImageLoadState::Failed && on_image_failed_)
        {
            on_image_failed_(source_.load_error());
        }
        return;
    }
    const AnimatedImage* animated = source_.animated();
    if (stopped_ || animated == nullptr || !animated->animated())
    {
        return;
    }
    // Off screen it stands still and asks for no tick; on_screen_changed() starts it again.
    if (!on_screen())
    {
        return;
    }
    // The first tick after a resume counts from the frame it stopped on.
    const double      step  = std::exchange(resumed_, false) ? 0.0 : delta.count();
    const std::size_t count = animated->frames.size();
    frame_time_ += step;
    for (;;)
    {
        const double delay = frame_ < animated->delays.size() ? animated->delays[frame_].count() : 100.0;
        if (frame_time_ < delay)
        {
            request_tick_in(Duration{delay - frame_time_});
            return;
        }
        // The frame after this one would start a loop that is not
        // played, so this one stays.
        const std::size_t next      = frame_ + 1;
        const int         next_loop = next == count ? loop_ + 1 : loop_;
        if (animated->loop_count != 0 && next_loop >= animated->loop_count)
        {
            frame_time_ = delay;
            return;
        }
        frame_time_ -= std::max(delay, 1.0);
        frame_ = next % count;
        loop_  = next_loop;
        mark_dirty(DirtyFlags::Visual);
    }
}

void ImageControl::watch_load()
{
    if (load_watched_)
    {
        return;
    }
    // The decode posts to the window when it is done, so the window sleeps until then. The post
    // holds only a WindowRef: it is dropped once the window is gone, and this element may be gone
    // already.
    if (Window* window = detail::ticking_window())
    {
        load_watched_ = source_.when_loaded([ref = window->ref()] { (void)ref.post([] {}); });
        if (!load_watched_)
        {
            request_tick_in(Duration{}); // done between the two reads: look again at once
        }
        return;
    }
    request_tick(); // ticked without a window: look again on the next tick
}

void ImageControl::on_screen_changed(bool on_screen)
{
    // It goes on from the frame it stopped on, for what was left of that frame's delay: the
    // time off screen does not count.
    if (on_screen && !stopped_)
    {
        resumed_ = true;
        request_tick_in(Duration{});
    }
}

Size ImageControl::scaled(Size available, StretchDirection preferred, StretchDirection* chosen) const
{
    // The scale factor, with 0 standing for infinity.
    const Size natural    = source_.natural_size();
    const bool inf_width  = !std::isfinite(available.width);
    const bool inf_height = !std::isfinite(available.height);
    float      scale_x    = 1.0f;
    float      scale_y    = 1.0f;
    if (chosen != nullptr)
    {
        *chosen = StretchDirection::Unknown;
    }
    if ((inf_width && inf_height) || stretch_ == Stretch::None)
    {
        return natural;
    }

    scale_y = natural.height == 0.0f ? 0.0f : available.height / natural.height;
    scale_x = natural.width == 0.0f ? 0.0f : available.width / natural.width;
    if (inf_width)
    {
        scale_x = scale_y;
    }
    else if (inf_height)
    {
        scale_y = scale_x;
    }
    else
    {
        StretchDirection direction = preferred;
        if (direction == StretchDirection::Unknown)
        {
            switch (stretch_)
            {
                case Stretch::Uniform:
                    direction = scale_x < scale_y ? StretchDirection::Horizontal : StretchDirection::Vertical;
                    break;
                case Stretch::UniformToFill:
                    direction = scale_x > scale_y ? StretchDirection::Horizontal : StretchDirection::Vertical;
                    break;
                case Stretch::Fill:
                case Stretch::None:
                    break;
            }
        }
        if (direction == StretchDirection::Horizontal)
        {
            scale_y = scale_x;
        }
        else if (direction == StretchDirection::Vertical)
        {
            scale_x = scale_y;
        }
        if (chosen != nullptr)
        {
            *chosen = direction;
        }
    }
    return Size{natural.width * scale_x, natural.height * scale_y};
}

Size ImageControl::measure_override(Size available, const MeasureContext&) const
{
    if (source_.empty())
    {
        measured_ = {};
        return {};
    }
    measured_    = scaled(available, StretchDirection::Unknown, &measure_direction_);
    Size desired = measured_;
    // Measure clips the desired size to what was offered; the unclipped
    // size is what arrange compares against.
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

ImageControl::Placement ImageControl::placement() const
{
    const Rect& slot    = bounds();
    const Size  natural = source_.natural_size();
    if (natural.width <= 0.0f || natural.height <= 0.0f || slot.width <= 0.0f || slot.height <= 0.0f)
    {
        return Placement{slot, slot};
    }

    // Arrange: measure's axis is kept only when arrange gave exactly what was asked for.
    const StretchDirection preferred = close(slot.width, measured_.width) && close(slot.height, measured_.height)
                                           ? measure_direction_
                                           : StretchDirection::Unknown;
    const Size             ink       = scaled(Size{slot.width, slot.height}, preferred, nullptr);

    // Ink beyond max_width/max_height (an explicit size is a max too) is clipped, keeping its
    // top-left.
    const auto limit = [](std::optional<float> size, float min, float max)
    { return std::max(min, size ? std::min(*size, max) : max); };
    const Size clipped{std::min(ink.width, limit(width(), min_width(), max_width())),
                       std::min(ink.height, limit(height(), min_height(), max_height()))};

    // Smaller ink is aligned in the slot (Stretch centers it). Larger ink
    // degenerates from Stretch to left/top, stays centered for Center, hangs off for Trailing.
    const auto offset = [](Alignment alignment, float space, float size)
    {
        if (alignment == Alignment::Leading || (alignment == Alignment::Stretch && size > space))
        {
            return 0.0f;
        }
        if (alignment == Alignment::Trailing)
        {
            return space - size;
        }
        return (space - size) * 0.5f;
    };
    const float x = slot.x + offset(horizontal_alignment(), slot.width, clipped.width);
    const float y = slot.y + offset(vertical_alignment(), slot.height, clipped.height);
    const Rect  ink_rect{x, y, ink.width, ink.height};
    const Rect  visible = intersect(Rect{x, y, clipped.width, clipped.height}, slot);

    // The picture is stretched into the ink, centered. With Stretch None the ink is the
    // picture's size, and the ink clip above crops it from the top-left.
    Size content = natural;
    switch (stretch_)
    {
        case Stretch::None:
            break;
        case Stretch::Fill:
            content = ink;
            break;
        case Stretch::Uniform:
        case Stretch::UniformToFill:
        {
            const float sx = ink.width / natural.width;
            const float sy = ink.height / natural.height;
            const float s  = stretch_ == Stretch::Uniform ? std::min(sx, sy) : std::max(sx, sy);
            content        = Size{natural.width * s, natural.height * s};
            break;
        }
    }
    const float cx = ink_rect.x + (ink_rect.width - content.width) * 0.5f;
    const float cy = ink_rect.y + (ink_rect.height - content.height) * 0.5f;
    return Placement{Rect{cx, cy, content.width, content.height}, visible};
}

void ImageControl::render_override(Renderer& renderer, const RenderContext&) const
{
    if (source_.empty())
    {
        return;
    }
    const Placement p = placement();
    if (p.visible.width <= 0.0f || p.visible.height <= 0.0f || p.content.width <= 0.0f || p.content.height <= 0.0f)
    {
        return;
    }

    if (const SvgImage* svg = source_.svg())
    {
        const bool clip = p.content.left() < p.visible.left() || p.content.top() < p.visible.top() ||
                          p.content.right() > p.visible.right() || p.content.bottom() > p.visible.bottom();
        if (clip)
        {
            renderer.push_clip(p.visible);
        }
        renderer.draw_svg(*svg, p.content);
        if (clip)
        {
            renderer.pop_clip();
        }
        return;
    }

    const Bitmap* image = source_.bitmap();
    if (const AnimatedImage* animated = source_.animated())
    {
        image = &animated->frames[std::min(frame_, animated->frames.size() - 1)];
    }
    if (image == nullptr || image->empty())
    {
        return;
    }
    const float iw = static_cast<float>(image->width());
    const float ih = static_cast<float>(image->height());

    const bool whole = nine_grid_.left <= 0.0f && nine_grid_.top <= 0.0f && nine_grid_.right <= 0.0f &&
                       nine_grid_.bottom <= 0.0f;
    if (whole)
    {
        draw_clipped(renderer, *image, Rect{0.0f, 0.0f, iw, ih}, p.content, p.visible);
        return;
    }

    // A nine-grid: corners keep the picture's size, edges stretch along their free axis, the center
    // along both. Cut points are pulled in so the cells never overlap.
    const float sl = std::clamp(nine_grid_.left, 0.0f, iw);
    const float sr = std::clamp(nine_grid_.right, 0.0f, iw - sl);
    const float st = std::clamp(nine_grid_.top, 0.0f, ih);
    const float sb = std::clamp(nine_grid_.bottom, 0.0f, ih - st);
    const Rect& d  = p.content;
    const float dl = std::min(sl, d.width);
    const float dr = std::min(sr, d.width - dl);
    const float dt = std::min(st, d.height);
    const float db = std::min(sb, d.height - dt);

    const std::array<float, 4> src_x{0.0f, sl, iw - sr, iw};
    const std::array<float, 4> src_y{0.0f, st, ih - sb, ih};
    const std::array<float, 4> dst_x{d.left(), d.left() + dl, d.right() - dr, d.right()};
    const std::array<float, 4> dst_y{d.top(), d.top() + dt, d.bottom() - db, d.bottom()};
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t col = 0; col < 3; ++col)
        {
            const Rect source{src_x[col], src_y[row], src_x[col + 1] - src_x[col], src_y[row + 1] - src_y[row]};
            const Rect dest{dst_x[col], dst_y[row], dst_x[col + 1] - dst_x[col], dst_y[row + 1] - dst_y[row]};
            if (dest.width > 0.0f && dest.height > 0.0f)
            {
                draw_clipped(renderer, *image, source, dest, p.visible);
            }
        }
    }
}

std::unique_ptr<ui::Element> Image::build() &&
{
    auto node = std::make_unique<ui::ImageControl>();

    apply(stretch, *node, [raw = node.get()](const ui::Stretch& v) { raw->set_stretch(v); });
    apply(nine_grid, *node, [raw = node.get()](const ui::Thickness& v) { raw->set_nine_grid(v); });
    apply(source, *node, [raw = node.get()](const ui::ImageSource& v) { raw->set_source(v); });

    apply_common(*this, *node);
    if (on_image_opened)
    {
        node->on_image_opened(std::move(on_image_opened));
    }
    if (on_image_failed)
    {
        node->on_image_failed(std::move(on_image_failed));
    }
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
