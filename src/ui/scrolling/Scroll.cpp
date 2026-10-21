// SPDX-License-Identifier: MIT
#include "winui/ui/scrolling/Scroll.hpp"

#include "../../render/Compositor.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Callback.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Version.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <cmath>
#include <cstdint>

namespace ui
{

namespace
{
// ScrollBarSize: the thickness each bar claims out of the other's length when both show.
constexpr float ScrollBarSize = 12.0f;

// An animated scroll or zoom takes this long per DIP or per zoom step, clamped to the range.
constexpr double OffsetsChangeMsPerUnit    = 5.0;
constexpr double ZoomFactorChangeMsPerUnit = 250.0;
constexpr double ChangeMinMs               = 50.0;
constexpr double ChangeMaxMs               = 1000.0;

// The default inertia decay rate, and the velocity at or below which a scroll has no effect.
constexpr float DefaultInertiaDecayRate = 0.95f;
constexpr float MinVelocity             = 30.0f;

// Keyboard inertia: the decay rate, and the velocity needed per DIP of travel above
// MinVelocity (ln 2000, the decay's own rate constant, to the precision given below).
constexpr float  KeyboardInertiaDecayRate = 0.9995f;
constexpr double VelocityNeededPerPixel   = 7.600855902349023;

// An arrow key scrolls by 15 % of the viewport, and the edge epsilon for key scrolling.
constexpr double SmallScrollProportion = 0.15;
constexpr float  OffsetEpsilon         = 0.001f;

// One wheel notch in DIPs (Win32WindowHost.cpp's WheelLineHeight), and the Ctrl+wheel zoom step
// per notch, a chosen value.
constexpr float WheelNotch       = 48.0f;
constexpr float WheelZoomPerStep = 1.1f;

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

// Time for a decaying velocity to fall to MinVelocity, given k = -ln(1 - decay).
double seconds_to_min_velocity(float speed, double k)
{
    return speed <= MinVelocity ? 0.0 : std::log(speed / MinVelocity) / k;
}
} // namespace

float KeyFrameEasing::evaluate(double x) const
{
    x = std::clamp(x, 0.0, 1.0);
    if (steps > 0)
    {
        // Step easing: the value stays on a step until the next begins,
        // and reaches the key frame only at its end.
        return x >= 1.0 ? 1.0f : static_cast<float>(std::floor(x * steps) / steps);
    }
    return easing::evaluate(curve, x);
}

// ---- configuration ----

void ScrollControl::set_content_orientation(ScrollContentOrientation orientation)
{
    if (content_orientation_ != orientation)
    {
        content_orientation_ = orientation;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void ScrollControl::set_horizontal_scroll_mode(ScrollMode mode)
{
    horizontal_scroll_mode_ = mode;
    mark_dirty(DirtyFlags::Visual);
}

void ScrollControl::set_vertical_scroll_mode(ScrollMode mode)
{
    vertical_scroll_mode_ = mode;
    mark_dirty(DirtyFlags::Visual);
}

void ScrollControl::set_zoom_mode(ScrollZoomMode mode)
{
    zoom_mode_ = mode;
    mark_dirty(DirtyFlags::Visual);
}

void ScrollControl::set_pan_on_drag(bool pan)
{
    pan_on_drag_ = pan;
}

std::pair<bool, bool> ScrollControl::pannable_axes() const
{
    if (!pan_on_drag_ || !is_enabled())
    {
        return {false, false};
    }
    return {computed_horizontal_scroll_mode() == ScrollMode::Enabled && scrollable_width() > 0.0f,
            computed_vertical_scroll_mode() == ScrollMode::Enabled && scrollable_height() > 0.0f};
}

void ScrollControl::set_horizontal_scroll_bar_visibility(ScrollBarVisibility visibility)
{
    horizontal_scroll_bar_visibility_ = visibility;
    horizontal_bar_.set_always_visible(visibility == ScrollBarVisibility::Visible);
    mark_dirty(DirtyFlags::Visual);
}

void ScrollControl::set_vertical_scroll_bar_visibility(ScrollBarVisibility visibility)
{
    vertical_scroll_bar_visibility_ = visibility;
    vertical_bar_.set_always_visible(visibility == ScrollBarVisibility::Visible);
    mark_dirty(DirtyFlags::Visual);
}

// Auto is Enabled while zooming is on, so content zoomed smaller than the viewport does not get
// stuck on that axis, otherwise only while there is something to scroll.
ScrollMode ScrollControl::computed_horizontal_scroll_mode() const
{
    if (horizontal_scroll_mode_ != ScrollMode::Auto)
    {
        return horizontal_scroll_mode_;
    }
    return zoom_mode_ == ScrollZoomMode::Enabled || scrollable_horizontally() ? ScrollMode::Enabled : ScrollMode::Disabled;
}

ScrollMode ScrollControl::computed_vertical_scroll_mode() const
{
    if (vertical_scroll_mode_ != ScrollMode::Auto)
    {
        return vertical_scroll_mode_;
    }
    return zoom_mode_ == ScrollZoomMode::Enabled || scrollable_vertically() ? ScrollMode::Enabled : ScrollMode::Disabled;
}

bool ScrollControl::computed_horizontal_scroll_bar_visible() const
{
    switch (horizontal_scroll_bar_visibility_)
    {
        case ScrollBarVisibility::Visible:
            return true;
        case ScrollBarVisibility::Hidden:
            return false;
        case ScrollBarVisibility::Auto:
            break;
    }
    return user_can_scroll_horizontally();
}

bool ScrollControl::computed_vertical_scroll_bar_visible() const
{
    if (!show_scrollbar_)
    {
        return false;
    }
    switch (vertical_scroll_bar_visibility_)
    {
        case ScrollBarVisibility::Visible:
            return true;
        case ScrollBarVisibility::Hidden:
            return false;
        case ScrollBarVisibility::Auto:
            break;
    }
    return user_can_scroll_vertically();
}

void ScrollControl::set_min_zoom_factor(float value)
{
    min_zoom_factor_ = std::max(0.0f, value);
    max_zoom_factor_ = std::max(max_zoom_factor_, min_zoom_factor_);
    set_view(horizontal_offset_, vertical_offset_, zoom_factor_);
}

void ScrollControl::set_max_zoom_factor(float value)
{
    max_zoom_factor_ = std::max(value, min_zoom_factor_);
    set_view(horizontal_offset_, vertical_offset_, zoom_factor_);
}

// ---- geometry ----

Point ScrollControl::alignment_offset() const
{
    const auto axis = [](float zoomed, float viewport, Alignment alignment)
    {
        if (zoomed >= viewport)
        {
            return 0.0f;
        }
        switch (alignment)
        {
            case Alignment::Center:
            case Alignment::Stretch:
                return (viewport - zoomed) * 0.5f;
            case Alignment::Trailing:
                return viewport - zoomed;
            case Alignment::Leading:
                break;
        }
        return 0.0f;
    };
    if (!child_)
    {
        return {};
    }
    const Rect view = viewport_rect();
    return {axis(extent_.width * zoom_factor_, view.width, child_->horizontal_alignment()),
            axis(extent_.height * zoom_factor_, view.height, child_->vertical_alignment())};
}

Point ScrollControl::content_to_view(Point p) const
{
    return {bounds().x + (p.x - bounds().x) * zoom_factor_, bounds().y + (p.y - bounds().y) * zoom_factor_};
}

Point ScrollControl::view_to_content(Point p) const
{
    return {bounds().x + (p.x - bounds().x) / zoom_factor_, bounds().y + (p.y - bounds().y) / zoom_factor_};
}

PointerEvent ScrollControl::to_content(const PointerEvent& e) const
{
    PointerEvent mapped = e;
    mapped.position     = view_to_content(e.position);
    return mapped;
}

Rect ScrollControl::vertical_bar_viewport() const
{
    Rect r = bounds();
    if (horizontal_bar_.gutter() > 0.0f)
    {
        r.height = std::max(0.0f, r.height - horizontal_gutter_);
    }
    else if (computed_horizontal_scroll_bar_visible())
    {
        r.height = std::max(0.0f, r.height - ScrollBarSize);
    }
    return r;
}

Rect ScrollControl::horizontal_bar_viewport() const
{
    Rect r = bounds();
    if (vertical_bar_.gutter() > 0.0f)
    {
        r.width = std::max(0.0f, r.width - vertical_gutter_);
    }
    else if (computed_vertical_scroll_bar_visible())
    {
        r.width = std::max(0.0f, r.width - ScrollBarSize);
    }
    return r;
}

// ---- layout ----

Size ScrollControl::measure_override(Size available, const MeasureContext& context) const
{
    if (!child_)
    {
        child_measured_ = {};
        return {};
    }

    // Infinity along free axes. A constrained axis offers the
    // viewport (within width/min_width/max_width) and clips the desired size, so content is cut off there.
    const bool constrain_width  = content_orientation_ == ScrollContentOrientation::Vertical ||
                                  content_orientation_ == ScrollContentOrientation::None;
    const bool constrain_height = content_orientation_ == ScrollContentOrientation::Horizontal ||
                                  content_orientation_ == ScrollContentOrientation::None;
    const auto computed_max     = [](float viewport, std::optional<float> size, float min, float max, float margin)
    {
        float result = size ? *size + margin : viewport;
        if (min > 0.0f)
        {
            result = std::max(result, min + margin);
        }
        return std::min(result, max + margin);
    };
    const Thickness margin  = child_->margin();
    const auto      measure = [&](Size room)
    {
        const float offer_width  = constrain_width && std::isfinite(room.width)
                                       ? computed_max(room.width,
                                                      child_->width(),
                                                      child_->min_width(),
                                                      child_->max_width(),
                                                      margin.horizontal())
                                       : Unbounded;
        const float offer_height = constrain_height && std::isfinite(room.height)
                                       ? computed_max(room.height,
                                                      child_->height(),
                                                      child_->min_height(),
                                                      child_->max_height(),
                                                      margin.vertical())
                                       : Unbounded;
        child_measured_          = child_->measure({offer_width, offer_height}, context);
        child_measured_.width    = std::min(child_measured_.width, offer_width);
        child_measured_.height   = std::min(child_measured_.height, offer_height);
    };
    measure(available);

    // An era's permanent bars take a gutter each from the viewport while they show, so the
    // content is measured again in what is left.
    vertical_bar_.apply_theme(context.theme);
    horizontal_bar_.apply_theme(context.theme);
    vertical_gutter_   = 0.0f;
    horizontal_gutter_ = 0.0f;
    if (const float gutter = vertical_bar_.gutter(); gutter > 0.0f)
    {
        const auto shows = [](ScrollBarVisibility visibility, ScrollMode mode, bool overflowing)
        {
            switch (visibility)
            {
                case ScrollBarVisibility::Visible:
                    return true;
                case ScrollBarVisibility::Hidden:
                    return false;
                case ScrollBarVisibility::Auto:
                    break;
            }
            return mode != ScrollMode::Disabled && overflowing;
        };
        // A bar shows while the zoomed content overflows what the other bar's gutter leaves.
        // Re-measuring in the smaller room can change the answer (narrower text grows taller),
        // so this settles in at most a few passes.
        const auto overflows = [&](float content, float room) { return content * zoom_factor_ > room; };
        for (int pass = 0; pass < 3; ++pass)
        {
            const bool  vertical   = show_scrollbar_ &&
                                     shows(vertical_scroll_bar_visibility_,
                                           vertical_scroll_mode_,
                                           overflows(child_measured_.height, available.height - horizontal_gutter_));
            const bool  horizontal = shows(horizontal_scroll_bar_visibility_,
                                           horizontal_scroll_mode_,
                                           overflows(child_measured_.width, available.width - vertical_gutter_));
            const float v_gutter   = vertical ? gutter : 0.0f;
            const float h_gutter   = horizontal ? gutter : 0.0f;
            if (v_gutter == vertical_gutter_ && h_gutter == horizontal_gutter_)
            {
                break;
            }
            vertical_gutter_   = v_gutter;
            horizontal_gutter_ = h_gutter;
            measure(Size{std::max(0.0f, available.width - vertical_gutter_),
                         std::max(0.0f, available.height - horizontal_gutter_)});
        }
        gutter_room_ = available;
    }

    // The content's size and the bars' gutters, up to the offered space. Arrange still stretches a
    // Scroll over its whole slot.
    const Size wanted{child_measured_.width * zoom_factor_ + vertical_gutter_,
                      child_measured_.height * zoom_factor_ + horizontal_gutter_};
    return {std::min(wanted.width, available.width), std::min(wanted.height, available.height)};
}

void ScrollControl::arrange_override(const Rect&, const ArrangeContext&)
{
    const bool first = !laid_out_;
    laid_out_        = true;

    Size extent = child_measured_;
    if (child_)
    {
        // A Stretch child with
        // no explicit size is allowed to grow to the viewport.
        const Rect view = viewport_rect();
        if (child_->horizontal_alignment() == Alignment::Stretch && !child_->width() && extent.width < view.width)
        {
            extent.width = view.width;
        }
        if (child_->vertical_alignment() == Alignment::Stretch && !child_->height() && extent.height < view.height)
        {
            extent.height = view.height;
        }
    }
    const bool extent_changed = extent.width != extent_.width || extent.height != extent_.height;
    extent_                   = extent;

    reflow();
    if (extent_changed && on_extent_changed_)
    {
        on_extent_changed_();
    }

    if (first && !deferred_.empty())
    {
        std::vector<Deferred> requests;
        requests.swap(deferred_);
        for (const Deferred& request : requests)
        {
            run(request);
        }
    }
}

void ScrollControl::reflow()
{
    const float old_h = horizontal_offset_;
    const float old_v = vertical_offset_;
    if (laid_out_)
    {
        horizontal_offset_ = std::clamp(horizontal_offset_, 0.0f, scrollable_width());
        vertical_offset_   = std::clamp(vertical_offset_, 0.0f, scrollable_height());
    }
    if (child_ && laid_out_)
    {
        const Point align = alignment_offset();
        child_->arrange(Rect{bounds().x + (align.x - horizontal_offset_) / zoom_factor_,
                             bounds().y + (align.y - vertical_offset_) / zoom_factor_,
                             extent_.width,
                             extent_.height});
    }
    if ((old_h != horizontal_offset_ || old_v != vertical_offset_) && on_view_changed_)
    {
        on_view_changed_();
    }
}

void ScrollControl::set_view(float horizontal, float vertical, OptionalFloat zoom)
{
    const float old_h = horizontal_offset_;
    const float old_v = vertical_offset_;
    const float old_z = zoom_factor_;
    if (zoom)
    {
        zoom_factor_ = std::clamp(*zoom, min_zoom_factor_, max_zoom_factor_);
    }
    horizontal_offset_ = horizontal;
    vertical_offset_   = vertical;

    // reflow() reports a change it causes itself (a clamp); anything else is reported here.
    Callback<void()> handler = std::move(on_view_changed_);
    reflow();
    on_view_changed_ = std::move(handler);

    if (old_z != zoom_factor_ && vertical_bar_.gutter() > 0.0f)
    {
        // Zooming can make the content overflow, or stop: the era bars' gutters follow.
        const bool tall = child_measured_.height * zoom_factor_ > gutter_room_.height - horizontal_gutter_;
        const bool wide = child_measured_.width * zoom_factor_ > gutter_room_.width - vertical_gutter_;
        if (tall != (vertical_gutter_ > 0.0f) || wide != (horizontal_gutter_ > 0.0f))
        {
            mark_dirty(DirtyFlags::Layout);
        }
    }
    if (old_h != horizontal_offset_ || old_v != vertical_offset_ || old_z != zoom_factor_)
    {
        mark_dirty(DirtyFlags::Visual);
        if (on_view_changed_)
        {
            on_view_changed_();
        }
    }
}

void ScrollControl::jump_to_vertical_offset(float offset)
{
    interrupt();
    set_view(horizontal_offset_, offset);
    mark_dirty(DirtyFlags::Visual);
}

void ScrollControl::jump_to_horizontal_offset(float offset)
{
    interrupt();
    set_view(offset, vertical_offset_);
    mark_dirty(DirtyFlags::Visual);
}

void ScrollControl::jump_to_zoom_factor(float zoom_factor)
{
    interrupt();
    const float target = std::clamp(zoom_factor, min_zoom_factor_, max_zoom_factor_);
    const Point pivot{viewport_width() * 0.5f, viewport_height() * 0.5f};
    // The content point under the center stays put, as in start_zoom().
    const float ratio = target / zoom_factor_;
    set_view(ratio * (horizontal_offset_ + pivot.x) - pivot.x, ratio * (vertical_offset_ + pivot.y) - pivot.y, target);
}

void ScrollControl::set_state(ScrollInteractionState state)
{
    if (state_ != state)
    {
        state_ = state;
        if (on_state_changed_)
        {
            on_state_changed_();
        }
    }
}

// ---- view changes ----

void ScrollControl::interrupt()
{
    // A new view change replaces whatever is running; the replaced request still completes.
    if (offsets_animation_)
    {
        const int id = offsets_animation_->id;
        offsets_animation_.reset();
        if (on_scroll_completed_)
        {
            on_scroll_completed_(id);
        }
    }
    if (inertia_)
    {
        const int id = inertia_->id;
        inertia_.reset();
        if (on_scroll_completed_)
        {
            on_scroll_completed_(id);
        }
    }
    if (zoom_animation_)
    {
        const int id = zoom_animation_->id;
        zoom_animation_.reset();
        if (on_zoom_completed_)
        {
            on_zoom_completed_(id);
        }
    }
    if (state_ != ScrollInteractionState::Interaction)
    {
        set_state(ScrollInteractionState::Idle);
    }
}

void ScrollControl::start_scroll(int id, Point target, ScrollAnimationMode mode)
{
    interrupt();
    target = {std::clamp(target.x, 0.0f, scrollable_width()), std::clamp(target.y, 0.0f, scrollable_height())};

    if (mode == ScrollAnimationMode::Disabled)
    {
        set_view(target.x, target.y);
        if (on_scroll_completed_)
        {
            on_scroll_completed_(id);
        }
        return;
    }

    // The key frame has no easing of its own, so it uses ControlFastOutSlowIn like other themed
    // motion.
    const Point             start{horizontal_offset_, vertical_offset_};
    const double            dx       = target.x - start.x;
    const double            dy       = target.y - start.y;
    const auto              distance = static_cast<std::int64_t>(std::sqrt(dx * dx + dy * dy));
    ScrollAnimationStarting args;
    args.animation.duration = Duration{
        std::clamp(static_cast<double>(distance) * OffsetsChangeMsPerUnit, ChangeMinMs, ChangeMaxMs)};
    args.animation.key_frames = {ScrollKeyFrame{1.0f, target, KeyFrameEasing{}}};
    args.start_position       = start;
    args.end_position         = target;
    args.correlation_id       = id;
    if (on_scroll_animation_starting_)
    {
        on_scroll_animation_starting_(args);
    }
    std::stable_sort(args.animation.key_frames.begin(),
                     args.animation.key_frames.end(),
                     [](const ScrollKeyFrame& a, const ScrollKeyFrame& b) { return a.progress < b.progress; });

    offsets_animation_ = OffsetsAnimation{std::move(args.animation), start, 0.0, id};
    set_state(ScrollInteractionState::Animating);
    mark_dirty(DirtyFlags::Visual);
}

void ScrollControl::start_zoom(int id, float target, std::optional<Point> center, ScrollAnimationMode mode)
{
    interrupt();
    target            = std::clamp(target, min_zoom_factor_, max_zoom_factor_);
    const Point pivot = center.value_or(Point{viewport_width() * 0.5f, viewport_height() * 0.5f});

    if (mode == ScrollAnimationMode::Disabled)
    {
        // The content point under the center stays put: offset' = z'/z * (offset + c) - c.
        const float ratio = target / zoom_factor_;
        set_view(ratio * (horizontal_offset_ + pivot.x) - pivot.x, ratio * (vertical_offset_ + pivot.y) - pivot.y, target);
        if (on_zoom_completed_)
        {
            on_zoom_completed_(id);
        }
        return;
    }

    // 250 ms per whole zoom step (the distance is cast to an integer, so
    // anything under one step takes the 50 ms minimum), clamped to 50..1000 ms.
    const auto            distance = static_cast<std::int64_t>(std::abs(target - zoom_factor_));
    ZoomAnimationStarting args;
    args.animation.duration = Duration{
        std::clamp(static_cast<double>(distance) * ZoomFactorChangeMsPerUnit, ChangeMinMs, ChangeMaxMs)};
    args.animation.key_frames = {ZoomKeyFrame{1.0f, target, KeyFrameEasing{}}};
    args.start_zoom_factor    = zoom_factor_;
    args.end_zoom_factor      = target;
    args.center_point         = pivot;
    args.correlation_id       = id;
    if (on_zoom_animation_starting_)
    {
        on_zoom_animation_starting_(args);
    }
    std::stable_sort(args.animation.key_frames.begin(),
                     args.animation.key_frames.end(),
                     [](const ZoomKeyFrame& a, const ZoomKeyFrame& b) { return a.progress < b.progress; });

    zoom_animation_ = ZoomAnimationState{std::move(args.animation), zoom_factor_, pivot, 0.0, id};
    set_state(ScrollInteractionState::Animating);
    mark_dirty(DirtyFlags::Visual);
}

void ScrollControl::start_velocity(int id, Point velocity, std::optional<Point> decay)
{
    // Velocity added to a coasting inertia joins it;
    // anything else running is replaced.
    Point current{};
    if (inertia_)
    {
        current = inertia_->velocity;
        if (on_scroll_completed_)
        {
            on_scroll_completed_(inertia_->id);
        }
        inertia_.reset();
    }
    interrupt();

    Inertia inertia;
    inertia.velocity = {current.x + velocity.x, current.y + velocity.y};
    inertia.decay    = decay.value_or(Point{DefaultInertiaDecayRate, DefaultInertiaDecayRate});
    inertia.decay    = {std::clamp(inertia.decay.x, 0.0f, 1.0f), std::clamp(inertia.decay.y, 0.0f, 1.0f)};
    inertia.id       = id;
    if (std::abs(inertia.velocity.x) <= MinVelocity)
    {
        inertia.velocity.x = 0.0f;
    }
    if (std::abs(inertia.velocity.y) <= MinVelocity)
    {
        inertia.velocity.y = 0.0f;
    }
    if (inertia.velocity.x == 0.0f && inertia.velocity.y == 0.0f)
    {
        if (on_scroll_completed_)
        {
            on_scroll_completed_(id);
        }
        return;
    }
    inertia_ = inertia;
    set_state(ScrollInteractionState::Inertia);
    mark_dirty(DirtyFlags::Visual);
}

void ScrollControl::run(const Deferred& request)
{
    switch (request.kind)
    {
        case Deferred::Kind::ScrollTo:
            start_scroll(request.id, request.value, request.animation);
            break;
        case Deferred::Kind::ScrollBy:
            start_scroll(request.id,
                         Point{horizontal_offset_ + request.value.x, vertical_offset_ + request.value.y},
                         request.animation);
            break;
        case Deferred::Kind::Velocity:
            start_velocity(request.id, request.value, request.extra);
            break;
        case Deferred::Kind::ZoomTo:
            start_zoom(request.id, request.value.x, request.extra, request.animation);
            break;
        case Deferred::Kind::ZoomBy:
            start_zoom(request.id, zoom_factor_ + request.value.x, request.extra, request.animation);
            break;
    }
}

int ScrollControl::scroll_to(float horizontal_offset, float vertical_offset, ScrollOptions options)
{
    const Deferred request{Deferred::Kind::ScrollTo,
                           next_id(),
                           {horizontal_offset, vertical_offset},
                           std::nullopt,
                           options.animation_mode};
    laid_out_ ? run(request) : deferred_.push_back(request);
    return request.id;
}

int ScrollControl::scroll_by(float horizontal_delta, float vertical_delta, ScrollOptions options)
{
    const Deferred request{Deferred::Kind::ScrollBy,
                           next_id(),
                           {horizontal_delta, vertical_delta},
                           std::nullopt,
                           options.animation_mode};
    laid_out_ ? run(request) : deferred_.push_back(request);
    return request.id;
}

int ScrollControl::add_scroll_velocity(Point offsets_velocity, std::optional<Point> inertia_decay_rate)
{
    const Deferred request{Deferred::Kind::Velocity, next_id(), offsets_velocity, inertia_decay_rate, ScrollAnimationMode::Enabled};
    laid_out_ ? run(request) : deferred_.push_back(request);
    return request.id;
}

int ScrollControl::zoom_to(float zoom_factor, std::optional<Point> center_point, ScrollZoomOptions options)
{
    const Deferred request{Deferred::Kind::ZoomTo, next_id(), {zoom_factor, 0.0f}, center_point, options.animation_mode};
    laid_out_ ? run(request) : deferred_.push_back(request);
    return request.id;
}

int ScrollControl::zoom_by(float zoom_factor_delta, std::optional<Point> center_point, ScrollZoomOptions options)
{
    const Deferred request{Deferred::Kind::ZoomBy, next_id(), {zoom_factor_delta, 0.0f}, center_point, options.animation_mode};
    laid_out_ ? run(request) : deferred_.push_back(request);
    return request.id;
}

bool ScrollControl::set_scroll_percent(float horizontal, float vertical)
{
    // Negative means "leave this axis alone" (IScrollProvider::SetScrollPercent's own
    // convention), not a failure.
    interrupt();
    set_view(horizontal >= 0.0f ? horizontal / 100.0f * scrollable_width() : horizontal_offset_,
             vertical >= 0.0f ? vertical / 100.0f * scrollable_height() : vertical_offset_);
    return true;
}

// ---- time ----

void ScrollControl::tick_override(Duration delta)
{
    // The two bars show and hide as one: a hover or drag on either keeps both, and the delayed
    // hide resumes only once neither is hovered or dragged.
    if (vertical_bar_.expanded() || horizontal_bar_.expanded())
    {
        vertical_bar_.notify_activity();
        horizontal_bar_.notify_activity();
    }
    if (vertical_bar_.update(delta) && computed_vertical_scroll_bar_visible())
    {
        mark_dirty(DirtyFlags::Visual);
    }
    if (horizontal_bar_.update(delta) && computed_horizontal_scroll_bar_visible())
    {
        mark_dirty(DirtyFlags::Visual);
    }
    // A shown bar waiting to fade wakes the window once for it.
    if (const std::optional<Duration> wait = vertical_bar_.fade_wait(); wait && computed_vertical_scroll_bar_visible())
    {
        request_tick_in(*wait);
    }
    if (const std::optional<Duration> wait = horizontal_bar_.fade_wait(); wait && computed_horizontal_scroll_bar_visible())
    {
        request_tick_in(*wait);
    }

    if (offsets_animation_)
    {
        OffsetsAnimation& a = *offsets_animation_;
        a.elapsed_ms += animation_delta(delta).count();
        const double progress = a.animation.duration > Duration{} ? a.elapsed_ms / a.animation.duration.count() : 1.0;
        Point        previous = a.start;
        float        at       = 0.0f;
        Point        value    = a.animation.key_frames.empty() ? a.start : a.animation.key_frames.back().offsets;
        for (const ScrollKeyFrame& frame : a.animation.key_frames)
        {
            if (progress <= frame.progress)
            {
                const float span  = frame.progress - at;
                const float local = span > 0.0f ? static_cast<float>((progress - at) / span) : 1.0f;
                const float eased = frame.easing.evaluate(local);
                value = {lerp(previous.x, frame.offsets.x, eased), lerp(previous.y, frame.offsets.y, eased)};
                break;
            }
            previous = frame.offsets;
            at       = frame.progress;
        }
        set_view(value.x, value.y);
        if (progress >= 1.0)
        {
            const int id = a.id;
            offsets_animation_.reset();
            set_state(ScrollInteractionState::Idle);
            if (on_scroll_completed_)
            {
                on_scroll_completed_(id);
            }
        }
    }

    if (zoom_animation_)
    {
        ZoomAnimationState& a = *zoom_animation_;
        a.elapsed_ms += animation_delta(delta).count();
        const double progress = a.animation.duration > Duration{} ? a.elapsed_ms / a.animation.duration.count() : 1.0;
        float        previous = a.start_zoom;
        float        at       = 0.0f;
        float        value = a.animation.key_frames.empty() ? a.start_zoom : a.animation.key_frames.back().zoom_factor;
        for (const ZoomKeyFrame& frame : a.animation.key_frames)
        {
            if (progress <= frame.progress)
            {
                const float span  = frame.progress - at;
                const float local = span > 0.0f ? static_cast<float>((progress - at) / span) : 1.0f;
                value             = lerp(previous, frame.zoom_factor, frame.easing.evaluate(local));
                break;
            }
            previous = frame.zoom_factor;
            at       = frame.progress;
        }
        value             = std::clamp(value, min_zoom_factor_, max_zoom_factor_);
        const float ratio = value / zoom_factor_;
        set_view(ratio * (horizontal_offset_ + a.center.x) - a.center.x,
                 ratio * (vertical_offset_ + a.center.y) - a.center.y,
                 value);
        if (progress >= 1.0)
        {
            const int id = a.id;
            zoom_animation_.reset();
            set_state(ScrollInteractionState::Idle);
            if (on_zoom_completed_)
            {
                on_zoom_completed_(id);
            }
        }
    }

    if (inertia_)
    {
        const float seconds = static_cast<float>(delta.count() / 1000.0);
        // v(t) = v0 * (1 - decay)^t integrated exactly: with k = -ln(1 - decay) a tick moves
        // v * (1 - e^(-k dt)) / k. A zero decay coasts at constant speed until an edge.
        const auto advance = [seconds](float& velocity, float decay, float& offset, float max_offset)
        {
            if (velocity == 0.0f)
            {
                return;
            }
            const float speed = std::abs(velocity);
            if (decay <= 0.0f)
            {
                offset += velocity * seconds;
            }
            else if (decay >= 1.0f)
            {
                velocity = 0.0f;
                return;
            }
            else
            {
                const double k     = -std::log(1.0 - static_cast<double>(decay));
                const double limit = seconds_to_min_velocity(speed, k);
                const double step  = std::min<double>(seconds, limit);
                offset += static_cast<float>(velocity * (1.0 - std::exp(-k * step)) / k);
                velocity = step >= limit ? 0.0f : static_cast<float>(velocity * std::exp(-k * step));
            }
            if (offset <= 0.0f && velocity < 0.0f)
            {
                offset   = 0.0f;
                velocity = 0.0f;
            }
            else if (offset >= max_offset && velocity > 0.0f)
            {
                offset   = max_offset;
                velocity = 0.0f;
            }
        };
        Inertia& i = *inertia_;
        float    h = horizontal_offset_;
        float    v = vertical_offset_;
        advance(i.velocity.x, i.decay.x, h, scrollable_width());
        advance(i.velocity.y, i.decay.y, v, scrollable_height());
        set_view(h, v);
        if (i.velocity.x == 0.0f && i.velocity.y == 0.0f)
        {
            const int id = i.id;
            inertia_.reset();
            set_state(ScrollInteractionState::Idle);
            if (on_scroll_completed_)
            {
                on_scroll_completed_(id);
            }
        }
    }
}

// ---- render ----

void ScrollControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (const std::optional<Color> background = background_.resolve(theme))
    {
        renderer.fill_rounded_rect(bounds(), 0.0f, *background);
    }
    renderer.push_clip(viewport_rect());
    if (child_)
    {
        const bool zoomed = zoom_factor_ != 1.0f;
        if (zoomed)
        {
            // screen = p * zoom + origin * (1 - zoom): the content scales about the viewport's
            // top-left corner, where content_to_view() puts it.
            renderer.push_transform(zoom_factor_,
                                    Point{bounds().x * (1.0f - zoom_factor_), bounds().y * (1.0f - zoom_factor_)});
        }
        child_->render(renderer, context);
        if (zoomed)
        {
            renderer.pop_transform();
        }
    }
    renderer.pop_clip();

    // Each bar is handed a content length that makes its own range equal this view's scrollable
    // range, since a bar that stops short of the other's corner is shorter than the viewport.
    // An era bar shows exactly while it has its gutter.
    const bool vertical_bar   = vertical_bar_.gutter() > 0.0f ? vertical_gutter_ > 0.0f
                                                              : computed_vertical_scroll_bar_visible();
    const bool horizontal_bar = horizontal_bar_.gutter() > 0.0f ? horizontal_gutter_ > 0.0f
                                                                : computed_horizontal_scroll_bar_visible();
    if (vertical_bar)
    {
        const Rect r = vertical_bar_viewport();
        vertical_bar_.render(renderer, theme, r, scrollable_height() + r.height, vertical_offset_);
    }
    if (horizontal_bar)
    {
        const Rect r = horizontal_bar_viewport();
        horizontal_bar_.render(renderer, theme, r, scrollable_width() + r.width, horizontal_offset_);
    }
    if (vertical_gutter_ > 0.0f && horizontal_gutter_ > 0.0f)
    {
        // The cell where the two era bars meet.
        renderer.fill_rounded_rect(Rect{bounds().right() - vertical_gutter_,
                                        bounds().bottom() - horizontal_gutter_,
                                        vertical_gutter_,
                                        horizontal_gutter_},
                                   0.0f,
                                   detail::controls(theme).gutter_scroll_bar->corner);
    }
}

// ---- input ----

void ScrollControl::enabled_changed()
{
    mark_dirty(DirtyFlags::Visual);
}

bool ScrollControl::wheel(const PointerEvent& e)
{
    if (!is_enabled() || !bounds().contains(e.position))
    {
        return false;
    }
    if (e.modifiers.ctrl && !e.horizontal_wheel)
    {
        if (zoom_mode_ != ScrollZoomMode::Enabled)
        {
            return false;
        }
        const float target = zoom_factor_ * std::pow(WheelZoomPerStep, -e.wheel_delta / WheelNotch);
        start_zoom(next_id(), target, Point{e.position.x - bounds().x, e.position.y - bounds().y}, ScrollAnimationMode::Disabled);
        return true;
    }
    // A horizontal wheel, or Shift with a vertical one, scrolls sideways; so does a plain wheel
    // over content that only scrolls sideways.
    bool horizontal = e.horizontal_wheel || e.modifiers.shift;
    if (!horizontal && !user_can_scroll_vertically() && user_can_scroll_horizontally())
    {
        horizontal = true;
    }
    // An axis with nothing to scroll leaves the turn to whatever scrolls around this view
    // (ScrollChainMode::Auto's chaining, for the case it matters most).
    if (horizontal ? !user_can_scroll_horizontally() : !user_can_scroll_vertically())
    {
        return false;
    }
    // And so does a turn toward an end this view is already at, unless the axis says not to chain
    // (ScrollChainMode::Never): a list at its bottom hands the wheel on to the page around it.
    const ScrollChainMode chain  = horizontal ? horizontal_scroll_chain_mode_ : vertical_scroll_chain_mode_;
    const double          offset = horizontal ? horizontal_offset_ : vertical_offset_;
    const double          extent = horizontal ? scrollable_width() : scrollable_height();
    const bool            at_end = e.wheel_delta > 0.0f ? offset >= extent - OffsetEpsilon : offset <= OffsetEpsilon;
    if (at_end && chain != ScrollChainMode::Never)
    {
        return false;
    }
    interrupt();
    if (horizontal)
    {
        set_view(horizontal_offset_ + e.wheel_delta, vertical_offset_);
        horizontal_bar_.notify_activity();
    }
    else
    {
        set_view(horizontal_offset_, vertical_offset_ + e.wheel_delta);
        // A wheel turn counts as scroll bar activity for the idle-hide clock, even off the band.
        vertical_bar_.notify_activity();
    }
    return true;
}

bool ScrollControl::pointer_override(const PointerEvent& e)
{
    if (e.action == PointerAction::Wheel)
    {
        (void)wheel(e);
        return false;
    }

    // A pan holds the pointer until its release, over the bars too.
    if (pan_)
    {
        if (e.action == PointerAction::Move)
        {
            const float h = pan_->horizontal ? pan_->offsets.x - (e.position.x - pan_->start.x) : horizontal_offset_;
            const float v = pan_->vertical ? pan_->offsets.y - (e.position.y - pan_->start.y) : vertical_offset_;
            set_view(h, v);
            return true;
        }
        if (e.action == PointerAction::Up && e.button == PointerButton::Left)
        {
            pan_.reset();
            set_state(ScrollInteractionState::Idle);
            return true;
        }
        if (e.action != PointerAction::Down)
        {
            return true;
        }
        // A press while panning means the release never arrived: the pan is over.
        pan_.reset();
        set_state(ScrollInteractionState::Idle);
    }

    // The overlay bars get first refusal, so a hover, drag or track click never reaches the content.
    const auto bar =
        [&](ScrollBarOverlay& overlay, bool visible, bool enabled, const Rect& viewport, float range, bool horizontal)
    {
        if (!visible || !enabled || !is_enabled())
        {
            return false;
        }
        float offset = horizontal ? horizontal_offset_ : vertical_offset_;
        if (!overlay.handle_pointer(e, viewport, range + (horizontal ? viewport.width : viewport.height), offset))
        {
            return false;
        }
        const float current = horizontal ? horizontal_offset_ : vertical_offset_;
        if (offset != current)
        {
            interrupt();
            horizontal ? set_view(offset, vertical_offset_) : set_view(horizontal_offset_, offset);
        }
        set_state(overlay.dragging() ? ScrollInteractionState::Interaction : ScrollInteractionState::Idle);
        return true;
    };
    const bool claimed_vertical = bar(vertical_bar_,
                                      computed_vertical_scroll_bar_visible(),
                                      computed_vertical_scroll_mode() == ScrollMode::Enabled,
                                      vertical_bar_viewport(),
                                      scrollable_height(),
                                      false);
    // A hover move always reaches both bars, so a pointer that jumps from one band straight onto
    // the other still un-hovers the first; otherwise it would stay expanded and never hide.
    // A thumb drag keeps the pointer to its own bar.
    const bool hover = e.action == PointerAction::Move || e.action == PointerAction::Leave;
    if (claimed_vertical && (!hover || vertical_bar_.dragging()))
    {
        return true;
    }
    const bool claimed_horizontal = bar(horizontal_bar_,
                                        computed_horizontal_scroll_bar_visible(),
                                        computed_horizontal_scroll_mode() == ScrollMode::Enabled,
                                        horizontal_bar_viewport(),
                                        scrollable_width(),
                                        true);
    if (claimed_vertical || claimed_horizontal)
    {
        return true;
    }
    if (e.action == PointerAction::Down && e.button == PointerButton::Left)
    {
        const auto [horizontal, vertical] = pannable_axes();
        if (horizontal || vertical)
        {
            interrupt();
            pan_ = Pan{e.position, Point{horizontal_offset_, vertical_offset_}, horizontal, vertical};
            set_state(ScrollInteractionState::Interaction);
            return true;
        }
    }
    if (state_ == ScrollInteractionState::Interaction && e.action == PointerAction::Up)
    {
        set_state(ScrollInteractionState::Idle);
    }
    return false;
}

Point ScrollControl::map_to_children(Point p) const
{
    return zoom_factor_ != 1.0f ? view_to_content(p) : p;
}

std::optional<Rect> ScrollControl::children_hit_clip() const
{
    // Content scrolled out of the viewport is not hit.
    return bounds();
}

bool ScrollControl::covers_children_at(Point p) const
{
    // The overlay bars sit over the content, so a hover, drag or track click never reaches it.
    if (!is_enabled())
    {
        return false;
    }
    const auto over = [&](const ScrollBarOverlay& overlay, bool visible, bool enabled, const Rect& viewport, float length)
    { return visible && enabled && overlay.scrollable(viewport, length) && overlay.band_bounds(viewport).contains(p); };
    return over(vertical_bar_,
                computed_vertical_scroll_bar_visible(),
                computed_vertical_scroll_mode() == ScrollMode::Enabled,
                vertical_bar_viewport(),
                scrollable_height() + vertical_bar_viewport().height) ||
           over(horizontal_bar_,
                computed_horizontal_scroll_bar_visible(),
                computed_horizontal_scroll_mode() == ScrollMode::Enabled,
                horizontal_bar_viewport(),
                scrollable_width() + horizontal_bar_viewport().width);
}

std::optional<CursorShape> ScrollControl::cursor_at(Point p) const
{
    if (pan_)
    {
        return CursorShape::SizeAll;
    }
    const auto [horizontal, vertical] = pannable_axes();
    if ((horizontal || vertical) && !covers_children_at(p))
    {
        return CursorShape::SizeAll;
    }
    return std::nullopt;
}

bool ScrollControl::wheel_override(const PointerEvent& e)
{
    if (!bounds().contains(e.position))
    {
        return false;
    }
    // A nested scroller under the cursor had it first, and left it.
    return wheel(e);
}

bool ScrollControl::can_scroll(Key key) const
{
    switch (key)
    {
        case Key::ArrowDown:
        case Key::PageDown:
            return user_can_scroll_vertically() && vertical_offset_ < scrollable_height() - OffsetEpsilon;
        case Key::ArrowUp:
        case Key::PageUp:
            return user_can_scroll_vertically() && vertical_offset_ > OffsetEpsilon;
        case Key::ArrowRight:
            return user_can_scroll_horizontally() && horizontal_offset_ < scrollable_width() - OffsetEpsilon;
        case Key::ArrowLeft:
            return user_can_scroll_horizontally() && horizontal_offset_ > OffsetEpsilon;
        default:
            return false;
    }
}

bool ScrollControl::takes_arrow_key(Key key) const
{
    // An arrow with nothing to scroll toward is left unhandled, so it moves focus.
    return is_focused() && is_enabled() && can_scroll(key);
}

bool ScrollControl::scroll_for_key(Key key)
{
    // Adds enough velocity to travel `amount` above the no-effect minimum. A key repeated while the last one still coasts drops the baseline it carries.
    const auto scroll = [this](double amount, bool vertical)
    {
        const double direction = amount > 0.0 ? 1.0 : -1.0;
        double       baseline  = MinVelocity * direction;
        if (inertia_)
        {
            const float previous = vertical ? inertia_->velocity.y : inertia_->velocity.x;
            if (previous > 0.0f)
            {
                baseline -= MinVelocity;
            }
            else if (previous < 0.0f)
            {
                baseline += MinVelocity;
            }
        }
        const auto velocity = static_cast<float>(baseline + amount * VelocityNeededPerPixel);
        const int  id       = next_id();
        start_velocity(id,
                       vertical ? Point{0.0f, velocity} : Point{velocity, 0.0f},
                       Point{KeyboardInertiaDecayRate, KeyboardInertiaDecayRate});
    };

    const double height = viewport_height();
    const double width  = viewport_width();
    if ((key == Key::PageDown || key == Key::ArrowDown) && can_scroll(Key::ArrowDown))
    {
        const double remaining = scrollable_height() + OffsetEpsilon - vertical_offset_;
        scroll(std::min(remaining, height * (key == Key::PageDown ? 1.0 : SmallScrollProportion)), true);
        return true;
    }
    if ((key == Key::PageUp || key == Key::ArrowUp) && can_scroll(Key::ArrowUp))
    {
        const double remaining = vertical_offset_ + OffsetEpsilon;
        scroll(std::max(-remaining, height * (key == Key::PageUp ? -1.0 : -SmallScrollProportion)), true);
        return true;
    }
    if (key == Key::ArrowRight && can_scroll(Key::ArrowRight))
    {
        scroll(std::min<double>(scrollable_width() + OffsetEpsilon - horizontal_offset_, width * SmallScrollProportion),
               false);
        return true;
    }
    if (key == Key::ArrowLeft && can_scroll(Key::ArrowLeft))
    {
        scroll(std::max<double>(-(horizontal_offset_ + OffsetEpsilon), -width * SmallScrollProportion), false);
        return true;
    }
    if (key == Key::Home)
    {
        // Home goes to the top, or to the left end when there is no vertical scrolling.
        const bool up = can_scroll(Key::ArrowUp);
        if (up || (computed_vertical_scroll_mode() == ScrollMode::Disabled && can_scroll(Key::ArrowLeft)))
        {
            scroll_to(up ? horizontal_offset_ : 0.0f, up ? 0.0f : vertical_offset_);
            return true;
        }
    }
    if (key == Key::End)
    {
        const bool down = can_scroll(Key::ArrowDown);
        if (down || (computed_vertical_scroll_mode() == ScrollMode::Disabled && can_scroll(Key::ArrowRight)))
        {
            scroll_to(down ? horizontal_offset_ : scrollable_width(), down ? scrollable_height() : vertical_offset_);
            return true;
        }
    }
    return false;
}

bool ScrollControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, takes_arrow_key(e.key), false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_focused() || !is_enabled() || e.action != KeyAction::Down)
    {
        return handled;
    }
    (void)scroll_for_key(e.key);
    return handled;
}

void ScrollControl::focus_changed(bool /*focused*/)
{
    mark_dirty(DirtyFlags::Visual);
}

// ---- focus / tree ----

Rect ScrollControl::reveal_child(const Rect& area, const BringIntoViewOptions& options)
{
    const Rect view = viewport_rect();
    if (area.empty() || view.empty())
    {
        return area;
    }
    const Point top_left     = content_to_view(area.origin());
    const Point bottom_right = content_to_view(Point{area.right(), area.bottom()});
    const Rect  in_view{top_left.x, top_left.y, bottom_right.x - top_left.x, bottom_right.y - top_left.y};

    // Far edge first, then near, so an area larger than the viewport shows its start. A ratio
    // lines up the same point of area and view instead.
    const auto delta = [](float near_edge, float far_edge, float view_near, float view_far, std::optional<float> ratio)
    {
        if (ratio)
        {
            const float r = std::clamp(*ratio, 0.0f, 1.0f);
            return (near_edge + (far_edge - near_edge) * r) - (view_near + (view_far - view_near) * r);
        }
        float d = 0.0f;
        if (far_edge > view_far)
        {
            d = far_edge - view_far;
        }
        if (near_edge - d < view_near)
        {
            d = near_edge - view_near;
        }
        return d;
    };
    const float dx = scrollable_width() > 0.0f
                         ? delta(in_view.left(), in_view.right(), view.left(), view.right(), options.horizontal_alignment_ratio) +
                               options.horizontal_offset
                         : 0.0f;
    const float dy = scrollable_height() > 0.0f
                         ? delta(in_view.top(), in_view.bottom(), view.top(), view.bottom(), options.vertical_alignment_ratio) +
                               options.vertical_offset
                         : 0.0f;
    if (dx == 0.0f && dy == 0.0f)
    {
        return in_view; // already fully in view
    }

    const float before_h = horizontal_offset_;
    const float before_v = vertical_offset_;
    if (options.animation_desired)
    {
        // Animated: the view moves over the scroll animation's duration, so the area is reported
        // where it will end up once the scroll lands.
        const float target_h = std::clamp(horizontal_offset_ + dx, 0.0f, scrollable_width());
        const float target_v = std::clamp(vertical_offset_ + dy, 0.0f, scrollable_height());
        (void)scroll_to(target_h, target_v, ScrollOptions{.animation_mode = ScrollAnimationMode::Enabled});
        return Rect{in_view.x - (target_h - before_h), in_view.y - (target_v - before_v), in_view.width, in_view.height};
    }
    interrupt();
    set_view(horizontal_offset_ + dx, vertical_offset_ + dy); // clamps, and re-arranges the child
    // The applied delta, not the requested one: a clamp moves less than asked, and the ancestor
    // must know where the area actually ended up.
    return Rect{in_view.x - (horizontal_offset_ - before_h),
                in_view.y - (vertical_offset_ - before_v),
                in_view.width,
                in_view.height};
}

std::unique_ptr<ui::Element> Scroll::build() &&
{
    auto node = std::make_unique<ui::ScrollControl>();
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    if (content)
    {
        node->set_child(content.take());
    }
    apply(content_orientation,
          *node,
          [raw = node.get()](const ScrollContentOrientation& v) { raw->set_content_orientation(v); });
    apply(min_zoom_factor, *node, [raw = node.get()](const float& v) { raw->set_min_zoom_factor(v); });
    apply(max_zoom_factor, *node, [raw = node.get()](const float& v) { raw->set_max_zoom_factor(v); });
    node->set_focusable(focusable);
    apply(background,
          *node,
          [raw = node.get()](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });
    apply(pan_on_drag, *node, [raw = node.get()](const bool& v) { raw->set_pan_on_drag(v); });

    // The view's position, two-way. A write the view already shows does nothing, so the view's own
    // write-back never interrupts the scrolling that caused it. A value the view clamped goes back to
    // the property once the view is laid out; before that the first layout clamps it and reports
    // the change through on_view_changed.
    ui::ScrollControl* raw     = node.get();
    const auto         write_h = ui::shared_writer(horizontal_offset);
    const auto         write_v = ui::shared_writer(vertical_offset);
    const auto         write_z = ui::shared_writer(zoom_factor);
    apply(zoom_factor,
          *node,
          [raw, write_z](const float& v)
          {
              if (v != raw->zoom_factor())
              {
                  raw->jump_to_zoom_factor(v);
              }
              if (write_z && raw->zoom_factor() != v)
              {
                  (*write_z)(raw->zoom_factor());
              }
          });
    apply(horizontal_offset,
          *node,
          [raw, write_h](const float& v)
          {
              if (v != raw->horizontal_offset())
              {
                  raw->jump_to_horizontal_offset(v);
              }
              if (write_h && raw->laid_out_ && raw->horizontal_offset() != v)
              {
                  (*write_h)(raw->horizontal_offset());
              }
          });
    apply(vertical_offset,
          *node,
          [raw, write_v](const float& v)
          {
              if (v != raw->vertical_offset())
              {
                  raw->jump_to_vertical_offset(v);
              }
              if (write_v && raw->laid_out_ && raw->vertical_offset() != v)
              {
                  (*write_v)(raw->vertical_offset());
              }
          });

    apply(horizontal_scroll_mode, *node, [raw](const ui::ScrollMode& v) { raw->set_horizontal_scroll_mode(v); });
    apply(vertical_scroll_mode, *node, [raw](const ui::ScrollMode& v) { raw->set_vertical_scroll_mode(v); });
    apply(zoom_mode, *node, [raw](const ui::ScrollZoomMode& v) { raw->set_zoom_mode(v); });
    apply(horizontal_scroll_chain_mode,
          *node,
          [raw = node.get()](const ScrollChainMode& v) { raw->set_horizontal_scroll_chain_mode(v); });
    apply(vertical_scroll_chain_mode,
          *node,
          [raw = node.get()](const ScrollChainMode& v) { raw->set_vertical_scroll_chain_mode(v); });
    apply(horizontal_scroll_bar_visibility,
          *node,
          [raw](const ui::ScrollBarVisibility& v) { raw->set_horizontal_scroll_bar_visibility(v); });
    apply(vertical_scroll_bar_visibility,
          *node,
          [raw](const ui::ScrollBarVisibility& v) { raw->set_vertical_scroll_bar_visibility(v); });

    if (write_h || write_v || write_z)
    {
        node->on_view_changed(
            [raw, write_h, write_v, write_z, handler = std::move(on_view_changed)]() mutable
            {
                if (write_h)
                {
                    (*write_h)(raw->horizontal_offset());
                }
                if (write_v)
                {
                    (*write_v)(raw->vertical_offset());
                }
                if (write_z)
                {
                    (*write_z)(raw->zoom_factor());
                }
                if (handler)
                {
                    handler();
                }
            });
    }
    else if (on_view_changed)
    {
        node->on_view_changed(std::move(on_view_changed));
    }
    if (on_scroll_completed)
    {
        node->on_scroll_completed(std::move(on_scroll_completed));
    }
    if (on_zoom_completed)
    {
        node->on_zoom_completed(std::move(on_zoom_completed));
    }
    if (on_scroll_animation_starting)
    {
        node->on_scroll_animation_starting(std::move(on_scroll_animation_starting));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
