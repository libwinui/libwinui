// SPDX-License-Identifier: MIT
#include "winui/ui/input/Slider.hpp"

#include "../../render/Compositor.hpp"
#include "../FocusRingPlacement.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <cmath>

namespace ui
{

namespace
{
constexpr float SliderDefaultLength = 200.0f; // desired length before a container's Stretch takes over
constexpr float ContainerCross      = 32.0f;  // the track container's height, or width when vertical
// The focus ring's outer edge sits 7 DIPs left and right of the track container, flush with it
// above and below.
constexpr float FocusMargin = 7.0f;

// The length of an outside tick, the 4px gap that holds each outside tick row off the track, and
// the closest two ticks may be drawn.
constexpr float TickLength   = 4.0f;
constexpr float TickGap      = 4.0f;
constexpr float MinTickGap   = 20.0f;
constexpr int   MaxTickMarks = 512; // the same cap render_classic() puts on a runaway frequency

// Duplicates Button.cpp's select_visual(). Only Button and Slider need it, so it stays out of Theme.
const ButtonVisual& accent_for_state(const Theme& theme, ButtonState state)
{
    switch (state)
    {
        case ButtonState::Hover:
            return theme.accent_button_hover;
        case ButtonState::Pressed:
            return theme.accent_button_pressed;
        case ButtonState::Disabled:
            return theme.accent_button_disabled;
        case ButtonState::Normal:
            break;
    }
    return theme.accent_button_normal;
}

// Fluent 1 names per-state thumb fills; every other theme uses the accent ramp.
[[nodiscard]] Color thumb_fill(const Theme& theme, ButtonState state, bool enabled)
{
    if (!detail::controls(theme).slider_fills)
    {
        return accent_for_state(theme, state).background;
    }
    if (!enabled)
    {
        return detail::controls(theme).slider_fills->thumb_disabled;
    }
    switch (state)
    {
        case ButtonState::Hover:
            return detail::controls(theme).slider_fills->thumb_hover;
        case ButtonState::Pressed:
            return detail::controls(theme).slider_fills->thumb_pressed;
        case ButtonState::Disabled:
            return detail::controls(theme).slider_fills->thumb_disabled;
        case ButtonState::Normal:
            break;
    }
    return detail::controls(theme).slider_fills->thumb;
}

// Stands in for the solid control fill color, which Palette has no token for yet.
constexpr Color lighten_track(const Color& c, float amount)
{
    return {std::clamp(c.r + amount, 0.0f, 1.0f),
            std::clamp(c.g + amount, 0.0f, 1.0f),
            std::clamp(c.b + amount, 0.0f, 1.0f),
            c.a};
}
} // namespace

SliderControl::SliderControl(Orientation orientation, double min, double max, double value) :
    orientation_(orientation),
    min_(min),
    max_(std::max(min, max))
{
    value_ = std::clamp(value, min_, max_);
    // The track container's height, or width when vertical.
    if (orientation_ == Orientation::Horizontal)
    {
        set_min_height(32.0f);
    }
    else
    {
        set_min_width(32.0f);
    }
}

void SliderControl::set_range(double min, double max)
{
    min_ = min;
    max_ = std::max(min, max);
    // A new range moves where the same value sits on the track, so this always repaints.
    mark_dirty(DirtyFlags::Visual);
    set_value(value_);
}

void SliderControl::set_value(double value)
{
    const double clamped = max_ > min_ ? std::clamp(value, min_, max_) : min_;
    if (clamped == value_)
    {
        return;
    }
    value_ = clamped;
    mark_dirty(DirtyFlags::Visual);
    if (on_value_changed_)
    {
        on_value_changed_(value_);
    }
}

void SliderControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_  = false;
        dragging_ = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

void SliderControl::focus_changed(bool /*focused*/)
{
    mark_dirty(DirtyFlags::Visual);
}

bool SliderControl::rings_whole_control(const Theme& theme) const
{
    const detail::ControlChrome& chrome = detail::controls(theme);
    if (chrome.slider_thumb.shape != SliderThumbShape::ConcentricCircles || theme.focus_ring_style().outward_extent() <= 0.0f)
    {
        return false;
    }
    const BorderStyle style = chrome.toggle_track.for_state(ButtonState::Normal, true).border_style;
    return style != BorderStyle::Raised && style != BorderStyle::Sunken;
}

Rect SliderControl::focus_container(const Theme& theme) const
{
    const float cross = std::max(ContainerCross, detail::controls(theme).slider_thumb.outer);
    const Point c     = thumb_center();
    return horizontal() ? Rect{bounds().left(), c.y - cross * 0.5f, bounds().width, cross}
                        : Rect{c.x - cross * 0.5f, bounds().top(), cross, bounds().height};
}

Size SliderControl::measure_override(Size available, const MeasureContext& context) const
{
    const Size header = header_.measure(available.width, context);
    available         = ControlHeader::below(available, header);

    float cross = std::max(detail::controls(context.theme).slider_thumb.outer,
                           detail::controls(context.theme).slider_track_thickness);
    // The thumb's center stops half a thumb inside each end, so the thumb stays inside bounds().
    const auto& thumb = detail::controls(context.theme).slider_thumb;
    travel_inset_ = (thumb.shape == SliderThumbShape::RoundedRect ? std::max({thumb.rest, thumb.hover, thumb.pressed})
                                                                  : thumb.outer) *
                    0.5f;
    if (detail::controls(context.theme).slider_thumb.shape == SliderThumbShape::ClassicPointer)
    {
        const detail::ControlChrome::ClassicTrackbar& g = detail::controls(context.theme).classic_trackbar;
        travel_inset_ = g.channel_inset + detail::controls(context.theme).slider_thumb.rest * 0.5f;
        // As tall as what it draws, the way comctl32 lays it out from the client rect's edge.
        cross = g.thumb_margin + detail::controls(context.theme).slider_thumb.outer + g.tick_gap + g.tick_end_length;
    }
    else if (detail::controls(context.theme).slider_thumb.shape == SliderThumbShape::ButtonBlock &&
             detail::controls(context.theme).slider_block)
    {
        // Aero's track sits `track_inset` in from each end and keeps its whole thumb inside
        // itself, so the thumb's center stops half a thumb short of the track's own ends.
        travel_inset_ = detail::controls(context.theme).slider_block->track_inset +
                        detail::controls(context.theme).slider_block->block.width * 0.5f;
    }
    Size desired   = horizontal() ? Size{SliderDefaultLength, cross} : Size{cross, SliderDefaultLength};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    // A header must not eat into the 32 DIP track container.
    float& cross_extent = horizontal() ? desired.height : desired.width;
    cross_extent        = std::max(cross_extent, ContainerCross);
    return ControlHeader::above(desired, header);
}

void SliderControl::arrange_override(const Rect&, const ArrangeContext&)
{
    header_.arrange(*this);
}

float SliderControl::value_fraction() const
{
    return max_ > min_ ? static_cast<float>(std::clamp((value_ - min_) / (max_ - min_), 0.0, 1.0)) : 0.0f;
}

double SliderControl::position_to_value(Point p) const
{
    if (max_ <= min_)
    {
        return min_;
    }
    const float extent = std::max(0.0f, (horizontal() ? bounds().width : bounds().height) - travel_inset_ * 2.0f);
    float       f;
    if (horizontal())
    {
        f = extent > 0.0f ? (p.x - bounds().left() - travel_inset_) / extent : 0.0f;
    }
    else
    {
        // Min at the bottom, the vertical default (direction_reversed() == false).
        f = extent > 0.0f ? 1.0f - (p.y - bounds().top() - travel_inset_) / extent : 0.0f;
    }
    return min_ + static_cast<double>(travel_fraction(std::clamp(f, 0.0f, 1.0f))) * (max_ - min_);
}

double SliderControl::closest_step(double interval, double value) const
{
    if (interval <= 0.0)
    {
        return value;
    }
    // The closest step: multiples of the interval from zero, not from min(),
    // each end clamped into the range, and a tie going to the lower step.
    const double steps = value / interval;
    const double next  = std::min(max_, std::ceil(steps) * interval);
    const double prev  = std::max(min_, std::floor(steps) * interval);
    return next - value < value - prev ? next : prev;
}

double SliderControl::snapped_position_value(Point p) const
{
    const double interval = snaps_to_ == SliderSnapsTo::Ticks && tick_frequency_ > 0.0 ? tick_frequency_ : step_frequency_;
    return closest_step(interval, position_to_value(p));
}

Point SliderControl::thumb_center() const
{
    const float f      = travel_fraction(value_fraction());
    const float extent = std::max(0.0f, (horizontal() ? bounds().width : bounds().height) - travel_inset_ * 2.0f);
    if (horizontal())
    {
        return Point{bounds().left() + travel_inset_ + f * extent, bounds().top() + bounds().height * 0.5f};
    }
    return Point{bounds().left() + bounds().width * 0.5f, bounds().bottom() - travel_inset_ - f * extent};
}

Rect SliderControl::track_bounds(const Theme& theme) const
{
    const float thickness = detail::controls(theme).slider_track_thickness;
    if (horizontal())
    {
        return Rect{bounds().left(), bounds().top() + (bounds().height - thickness) * 0.5f, bounds().width, thickness};
    }
    return Rect{bounds().left() + (bounds().width - thickness) * 0.5f, bounds().top(), thickness, bounds().height};
}

Rect SliderControl::filled_bounds(const Rect& track, Point thumb_c) const
{
    if (horizontal())
    {
        const float x = std::clamp(thumb_c.x, track.left(), track.right());
        return direction_reversed_ ? Rect{x, track.top(), track.right() - x, track.height}
                                   : Rect{track.left(), track.top(), x - track.left(), track.height};
    }
    const float y = std::clamp(thumb_c.y, track.top(), track.bottom());
    return direction_reversed_ ? Rect{track.left(), track.top(), track.width, y - track.top()}
                               : Rect{track.left(), y, track.width, track.bottom() - y};
}

float SliderControl::thumb_diameter(const Theme& theme, ButtonState state) const
{
    switch (state)
    {
        case ButtonState::Hover:
            return detail::controls(theme).slider_thumb.hover;
        case ButtonState::Pressed:
            return detail::controls(theme).slider_thumb.pressed;
        case ButtonState::Normal:
        case ButtonState::Disabled:
            break;
    }
    return detail::controls(theme).slider_thumb.rest;
}

bool SliderControl::is_pressed() const
{
    return is_enabled() && dragging_;
}

ButtonState SliderControl::visual_state() const
{
    if (!is_enabled())
    {
        return ButtonState::Disabled;
    }
    if (is_pressed())
    {
        return ButtonState::Pressed;
    }
    if (hovered_)
    {
        return ButtonState::Hover;
    }
    return ButtonState::Normal;
}

bool SliderControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }

    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
            hovered_ = bounds().contains(e.position);
            if (dragging_)
            {
                set_value(snapped_position_value(e.position));
            }
            break;

        case PointerAction::Down:
            if (e.button == PointerButton::Left && bounds().contains(e.position))
            {
                dragging_ = true;
                hovered_  = true;
                handled   = true;
                set_value(snapped_position_value(e.position));
            }
            break;

        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                handled   = dragging_;
                dragging_ = false;
                hovered_  = bounds().contains(e.position);
            }
            break;

        case PointerAction::Leave:
            hovered_ = false;
            break;

        case PointerAction::Wheel:
            break;
    }
    return handled;
}

bool SliderControl::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, takes_arrow_key(e.key), false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused() || e.action != KeyAction::Down)
    {
        return handled;
    }

    // A step: SliderSnapsTo::Ticks lands on a tick, and stepping down from an off-tick
    // max() goes to the last tick below it.
    const bool by_ticks = snaps_to_ == SliderSnapsTo::Ticks && tick_frequency_ > 0.0f;
    auto       step_by  = [&](bool forward)
    {
        if (!by_ticks)
        {
            set_value(forward ? value_ + step_ : value_ - step_);
            return;
        }
        const double ticks = value_ / tick_frequency_;
        if (!forward && value_ == max_ && ticks != std::floor(ticks))
        {
            set_value(std::floor(ticks) * tick_frequency_);
            return;
        }
        set_value(closest_step(tick_frequency_, forward ? value_ + tick_frequency_ : value_ - tick_frequency_));
    };

    switch (e.key)
    {
        // Up/Right increase whatever the orientation, and
        // direction_reversed() turns all four round.
        case Key::ArrowUp:
        case Key::ArrowRight:
            step_by(!direction_reversed_);
            break;

        case Key::ArrowDown:
        case Key::ArrowLeft:
            step_by(direction_reversed_);
            break;

        case Key::Home:
            set_value(min_);
            break;

        case Key::End:
            set_value(max_);
            break;

        default:
            break;
    }
    return handled;
}

void SliderControl::sync_composition_override(detail::Compositor& /*compositor*/, const Theme& theme)
{
    const ButtonState   state     = visual_state();
    const ButtonVisual& toggle_on = detail::controls(theme).toggle_track.for_state(state, true);
    if (toggle_on.border_style == BorderStyle::Raised || toggle_on.border_style == BorderStyle::Sunken)
    {
        // Bevel themes draw one flat thumb, so there is no inner disc to overlay.
        return;
    }
    if (detail::controls(theme).slider_thumb.shape == SliderThumbShape::ClassicPointer)
    {
        // A TRACKBAR_CLASS thumb never resizes, and the era has no animation.
        return;
    }
    if (detail::controls(theme).slider_thumb.shape == SliderThumbShape::ButtonBlock)
    {
        // Aero's block thumb changes state instantly and never resizes.
        return;
    }
    if (detail::controls(theme).slider_thumb.shape == SliderThumbShape::RoundedRect)
    {
        // MDL2's thumb is one flat rectangle. Its storyboard only swaps the fill on Pressed, with
        // Duration="0".
        return;
    }

    // render() draws the inner circle at the tween's size. Keyed by the state entered: 250 ms into
    // Hover/Pressed, 167 ms into Normal/Disabled.
    const float size = thumb_diameter(theme, state);
    const Duration duration = (state == ButtonState::Hover || state == ButtonState::Pressed) ? theme.anim_normal : theme.anim_fast;
    inner_tween_.retarget(Rect{0.0f, 0.0f, size, size}, duration, theme.anim_fast_out_slow_in);
}

void SliderControl::tick_override(Duration delta)
{
    if (inner_tween_.advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
}

// comctl32's paint order: channel, ticks, thumb, focus rect. Only the stock style (ticks on the
// far side, thumb pointing at them) is modeled; nothing here asks for TBS_TOP or TBS_BOTH.
void SliderControl::render_classic(Renderer& renderer, const Theme& theme) const
{
    const detail::ControlChrome::ClassicTrackbar& g = detail::controls(theme).classic_trackbar;
    // One visual for every state: a trackbar's bevel never changes, only the dither does. The
    // ToggleVisual pair would paint the thumb white, since its "on" background is COLOR_WINDOW.
    const ButtonVisual& chrome = theme.button_normal;

    const bool  horiz      = horizontal();
    const float along_len  = detail::controls(theme).slider_thumb.rest;  // (uThumbLen / 2) | 1
    const float across_len = detail::controls(theme).slider_thumb.outer; // uThumbLen
    const float point      = std::floor(along_len * 0.5f);               // the tip is always a true 45 degrees
    const float block      = std::max(0.0f, across_len - point);         // the flat-ended part of the thumb

    // Centered across, because a caller can give a trackbar more room than it asked for and a
    // drifting assembly reads as a layout bug.
    const float total  = g.thumb_margin + across_len + g.tick_gap + g.tick_end_length;
    const float across = horiz ? bounds().height : bounds().width;
    const float origin = (horiz ? bounds().top() : bounds().left()) + std::floor(std::max(0.0f, across - total) * 0.5f);

    // (along position, along extent, across position, across extent) in the control's own axes.
    auto rect = [&](float a, float a_len, float c, float c_len)
    { return horiz ? Rect{a, c, a_len, c_len} : Rect{c, a, c_len, a_len}; };

    const float along_origin = horiz ? bounds().left() : bounds().top();
    const float along_extent = horiz ? bounds().width : bounds().height;
    // thumb_center()'s mapping on one axis, so the ticks and the thumb always agree.
    auto center_along = [&](float f)
    {
        const float travel = std::max(0.0f, along_extent - travel_inset_ * 2.0f);
        const float t      = travel_fraction(f);
        return horiz ? bounds().left() + travel_inset_ + t * travel : bounds().bottom() - travel_inset_ - t * travel;
    };

    // ---- channel ----
    // DrawEdge(EDGE_SUNKEN, BF_RECT) two rings thick: the 4 px groove is all bevel, no interior.
    const Rect channel_rect = rect(along_origin + g.channel_inset,
                                   std::max(0.0f, along_extent - g.channel_inset * 2.0f),
                                   origin + g.channel_offset,
                                   detail::controls(theme).slider_track_thickness);
    draw_bevel_border(renderer,
                      channel_rect,
                      BorderStyle::Sunken,
                      chrome.bevel_light,
                      chrome.bevel_highlight,
                      chrome.bevel_shadow,
                      chrome.bevel_dark_shadow,
                      theme.border_width);

    // ---- tick marks ----
    // The two end ticks are one pixel longer and the only ones drawn without TBS_AUTOTICKS, which
    // is what tick_frequency_ == 0 means.
    const float tick_across = origin + g.thumb_margin + across_len + g.tick_gap;
    // Placed off the thumb's rounded point instead of the raw fraction, so a tick never lands a
    // pixel away from the tip.
    auto tick = [&](float f, float length)
    {
        const float x = std::floor(center_along(f) - along_len * 0.5f) + point;
        renderer.fill_rounded_rect(rect(x, 1.0f, tick_across, length), 0.0f, chrome.bevel_dark_shadow);
    };
    // Only TBS_NOTICKS is distinguished. Every other placement gets the far-side row, since a
    // near-side one would need the point flipped.
    const bool ticks = tick_placement_ != SliderTickPlacement::None;
    if (ticks)
    {
        tick(0.0f, g.tick_end_length);
        tick(1.0f, g.tick_end_length);
    }
    if (ticks && tick_frequency_ > 0.0f && max_ > min_)
    {
        // Capped rather than trusted: a frequency far finer than the track is wide would
        // otherwise emit one render command per value unit for a solid gray smear.
        const int steps = std::min(static_cast<int>((max_ - min_) / tick_frequency_), 512);
        for (int i = 1; i < steps; ++i)
        {
            tick(static_cast<float>(i * tick_frequency_ / (max_ - min_)), g.tick_length);
        }
    }

    // ---- thumb ----
    const float thumb_along  = std::floor(center_along(value_fraction()) - along_len * 0.5f);
    const float thumb_across = origin + g.thumb_margin;
    // Disabled and dragging both show a 50% dither; the thumb stays raised throughout.
    const bool  dithered = !is_enabled() || is_pressed();
    const Color face     = chrome.background;
    // fill_rect_pattern() phases its checker from the rect it gets, so a part of the thumb passes
    // its offset or the dither breaks into stripes.
    auto fill = [&](const Rect& r, float da, float dc)
    {
        if (!dithered)
        {
            renderer.fill_rounded_rect(r, 0.0f, face);
            return;
        }
        const bool odd = ((static_cast<int>(da) + static_cast<int>(dc)) & 1) != 0;
        renderer.fill_rect_pattern(r,
                                   Pattern::Checkerboard50,
                                   odd ? chrome.bevel_highlight : face,
                                   odd ? face : chrome.bevel_highlight);
    };

    // The flat-ended part: `DrawEdge(EDGE_RAISED | BF_SOFT)` on every side but the one the point
    // grows out of.
    const Rect body = rect(thumb_along, along_len, thumb_across, block);
    fill(body, 0.0f, 0.0f);
    draw_bevel_border(renderer,
                      body,
                      BorderStyle::Raised,
                      chrome.bevel_light,
                      chrome.bevel_highlight,
                      chrome.bevel_shadow,
                      chrome.bevel_dark_shadow,
                      theme.border_width,
                      horiz ? (BevelSides::Left | BevelSides::Right | BevelSides::Top)
                            : (BevelSides::Top | BevelSides::Bottom | BevelSides::Left),
                      /*soft=*/true);

    // Row-wise, because comctl32's pentagon plus two 2px diagonal edges rasterizes to exactly this
    // on an aliased target. The dark pair goes last so it owns the tip pixel, as in comctl32.
    for (int k = 0; k < static_cast<int>(point); ++k)
    {
        const float row   = thumb_across + block + static_cast<float>(k);
        const float start = thumb_along + static_cast<float>(k) + 1.0f;
        const float width = along_len - static_cast<float>(k + 1) * 2.0f;
        if (width < 1.0f)
        {
            break;
        }
        fill(rect(start, width, row, 1.0f), static_cast<float>(k) + 1.0f, block + static_cast<float>(k));
        auto px = [&](float a, const Color& color) { renderer.fill_rounded_rect(rect(a, 1.0f, row, 1.0f), 0.0f, color); };
        if (width >= 4.0f)
        {
            px(start + 1.0f, chrome.bevel_light);
            px(start + width - 2.0f, chrome.bevel_shadow);
        }
        px(start, chrome.bevel_highlight);
        px(start + width - 1.0f, chrome.bevel_dark_shadow);
    }
}

// The tick bar: the first tick sits under the thumb's center at
// min(), and a partial last interval gets no tick.
void SliderControl::render_ticks(Renderer&    renderer,
                                 const Theme& theme,
                                 const Rect&  track,
                                 const Color& outside_fill,
                                 float        gap,
                                 float        length) const
{
    if (tick_placement_ == SliderTickPlacement::None || tick_frequency_ <= 0.0 || max_ <= min_)
    {
        return;
    }

    const float extent    = std::max(0.0f, (horizontal() ? bounds().width : bounds().height) - travel_inset_ * 2.0f);
    const float intervals = static_cast<float>(std::max(1.0, (max_ - min_) / tick_frequency_));
    int         count     = static_cast<int>(std::floor(intervals));
    float       spacing   = std::max(1.0f, extent / intervals);
    if (spacing < MinTickGap)
    {
        // Too dense to read: keep every n-th tick, n the smallest that clears the minimum gap.
        const int every = static_cast<int>(std::ceil(MinTickGap / spacing));
        spacing *= static_cast<float>(every);
        count /= every;
    }
    count = std::min(count + 1, MaxTickMarks); // +1 for the tick at min()

    // Not `near`/`far`: <windef.h> defines both as macros.
    const float track_start = horizontal() ? track.top() : track.left();
    const float track_end   = horizontal() ? track.bottom() : track.right();
    // Outside rows sit 4 DIP off the track so the 22 DIP thumb covers the tick it stands on. The
    // thumb draws over its tick rows.
    const bool near_row = tick_placement_ == SliderTickPlacement::TopLeft || tick_placement_ == SliderTickPlacement::Outside;
    const bool far_row  = tick_placement_ == SliderTickPlacement::BottomRight ||
                          tick_placement_ == SliderTickPlacement::Outside;
    const bool in_track = tick_placement_ == SliderTickPlacement::Inline;
    // Inline ticks are notches in the track. Palette has no token for their color, so the window
    // background stands in.
    const Color notch = theme.colors().background;

    for (int i = 0; i < count; ++i)
    {
        const float offset = static_cast<float>(i) * spacing;
        // Counted from the min end, which direction_reversed() moves to the other side.
        const float at   = horizontal() ? (direction_reversed_ ? bounds().right() - travel_inset_ - offset
                                                               : bounds().left() + travel_inset_ + offset)
                                        : (direction_reversed_ ? bounds().top() + travel_inset_ + offset
                                                               : bounds().bottom() - travel_inset_ - offset);
        auto        mark = [&](float across, float span)
        { return horizontal() ? Rect{at - 0.5f, across, 1.0f, span} : Rect{across, at - 0.5f, span, 1.0f}; };
        if (in_track)
        {
            renderer.fill_rounded_rect(mark(track_start, track_end - track_start), 0.0f, notch);
        }
        if (near_row)
        {
            renderer.fill_rounded_rect(mark(track_start - gap - length, length), 0.0f, outside_fill);
        }
        if (far_row)
        {
            renderer.fill_rounded_rect(mark(track_end + gap, length), 0.0f, outside_fill);
        }
    }
}

void SliderControl::render_block(Renderer& renderer, const Theme& theme) const
{
    const detail::ControlChrome::SliderBlock& b       = *detail::controls(theme).slider_block;
    const bool                                horiz   = horizontal();
    const Point                               thumb_c = thumb_center();
    const ButtonState                         state   = visual_state();

    // Two fills instead of a stroke: the border gradient runs across the groove, and only the fill
    // path takes an axis.
    const float         thickness = detail::controls(theme).slider_track_thickness;
    const Rect          track     = horiz ? Rect{bounds().left() + b.track_inset,
                                                 thumb_c.y - thickness * 0.5f,
                                                 std::max(0.0f, bounds().width - b.track_inset * 2.0f),
                                                 thickness}
                                          : Rect{thumb_c.x - thickness * 0.5f,
                                                 bounds().top() + b.track_inset,
                                                 thickness,
                                                 std::max(0.0f, bounds().height - b.track_inset * 2.0f)};
    LinearGradientBrush border    = b.track_border;
    border.axis                   = horiz ? GradientAxis::Vertical : GradientAxis::Horizontal;
    renderer.fill_rounded_rect_gradient(track, b.track_radius, border);
    const Rect groove = track.inset(Thickness{1.0f});
    if (groove.width > 0.0f && groove.height > 0.0f)
    {
        renderer.fill_rounded_rect(groove, 0.0f, b.track_fill);
    }

    // Ticks sit in a band just past the thumb's row, which is the block's height across.
    const float row_gap = std::max(0.0f, b.block.height * 0.5f - thickness * 0.5f);
    render_ticks(renderer, theme, track, b.tick, row_gap, b.tick_length);

    // Keyboard focus shows the hover face.
    const ButtonState face_state = (state == ButtonState::Normal && is_focused() && is_enabled()) ? ButtonState::Hover : state;
    const ButtonVisual& face = b.thumb_for_state(face_state);

    // Ticks on one side only turn the block into a pointer aimed at them.
    const bool pointed = tick_placement_ == SliderTickPlacement::TopLeft ||
                         tick_placement_ == SliderTickPlacement::BottomRight;
    if (!pointed)
    {
        const Size   along_across = b.block;
        const Size   size         = horiz ? along_across : Size{along_across.height, along_across.width};
        const Rect   thumb{std::round(thumb_c.x - size.width * 0.5f),
                           std::round(thumb_c.y - size.height * 0.5f),
                           size.width,
                           size.height};
        ButtonVisual v = face;
        if (v.background_gradient && !horiz)
        {
            v.background_gradient->axis = GradientAxis::Horizontal;
        }
        draw_control_chrome(renderer, thumb, is_enabled() ? 1.0f : 0.0f, v, theme.border_width);
        return;
    }

    // The pointer, in (along, across) coordinates on its own box: a path stroked on pixel
    // centers, with a flat one-pixel tip. `toward_far` points it down (right on a vertical slider).
    const bool  toward_far = tick_placement_ == SliderTickPlacement::BottomRight;
    const float w          = b.pointed.width + 1.0f; // the box the stroke covers
    const float h          = b.pointed.height + 1.0f;
    const Rect  box        = horiz ? Rect{std::round(thumb_c.x - w * 0.5f), std::round(thumb_c.y - h * 0.5f), w, h}
                                   : Rect{std::round(thumb_c.x - h * 0.5f), std::round(thumb_c.y - w * 0.5f), h, w};
    auto        at         = [&](float a, float c)
    {
        const float across = toward_far ? c : h - c;
        return horiz ? Point{box.left() + a, box.top() + across} : Point{box.left() + across, box.top() + a};
    };
    const float                shoulder = h - 0.5f - b.point;
    const float                mid      = w * 0.5f;
    const std::array<Point, 6> outer{at(0.5f, 0.5f),
                                     at(w - 0.5f, 0.5f),
                                     at(w - 0.5f, shoulder),
                                     at(mid + 0.5f, h - 0.5f),
                                     at(mid - 0.5f, h - 0.5f),
                                     at(0.5f, shoulder)};
    const std::array<Point, 5>
        inner{at(1.5f, 1.5f), at(w - 1.5f, 1.5f), at(w - 1.5f, shoulder), at(mid, h - 1.0f), at(1.5f, shoulder)};

    // The face gradient runs from the flat end to the point.
    Brush fill = face.background;
    if (face.background_gradient)
    {
        LinearGradientBrush g = *face.background_gradient;
        g.axis                = horiz ? GradientAxis::Vertical : GradientAxis::Horizontal;
        if (!toward_far)
        {
            std::reverse(g.stops.begin(), g.stops.end());
            for (GradientStop& s : g.stops)
            {
                s.offset = 1.0f - s.offset;
            }
        }
        fill = g;
    }
    renderer.draw_geometry(DrawGeometry{PathGeometry::polyline(outer, true), fill, std::nullopt, StrokeStyle{}, box});
    if (face.inner_border)
    {
        renderer.draw_geometry(
            DrawGeometry{PathGeometry::polyline(inner, true), std::nullopt, Brush{face.inner_border->stops.front().color}});
    }
    renderer.draw_geometry(DrawGeometry{PathGeometry::polyline(outer, true), std::nullopt, Brush{face.border}});
}

void SliderControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    header_.render(renderer, theme, is_enabled());

    const ButtonState state        = visual_state();
    const Rect        track        = track_bounds(theme);
    const float       track_radius = horizontal() ? track.height * 0.5f : track.width * 0.5f;
    const Point       thumb_c      = thumb_center();

    // Retro keeps ToggleSwitch's `toggle_track` colors: a Win32 trackbar groove has no value fill,
    // and its thumb is a raised block.
    const ButtonVisual& toggle_off = detail::controls(theme).toggle_track.for_state(state, false);
    const ButtonVisual& toggle_on  = detail::controls(theme).toggle_track.for_state(state, true);
    const bool is_bevel = toggle_on.border_style == BorderStyle::Raised || toggle_on.border_style == BorderStyle::Sunken;

    if (detail::controls(theme).slider_thumb.shape == SliderThumbShape::ClassicPointer)
    {
        render_classic(renderer, theme);
        return;
    }
    if (detail::controls(theme).slider_thumb.shape == SliderThumbShape::ButtonBlock && detail::controls(theme).slider_block)
    {
        render_block(renderer, theme);
        return;
    }

    if (is_bevel)
    {
        const float thumb_d = thumb_diameter(theme, state);
        const float thumb_r = thumb_d * 0.5f;
        const Rect  thumb_rect{thumb_c.x - thumb_r, thumb_c.y - thumb_r, thumb_d, thumb_d};

        renderer.fill_rounded_rect(track, 0.0f, toggle_off.background);
        draw_bevel_border(renderer,
                          track,
                          BorderStyle::Sunken,
                          toggle_off.bevel_light,
                          toggle_off.bevel_highlight,
                          toggle_off.bevel_shadow,
                          toggle_off.bevel_dark_shadow,
                          theme.border_width);

        renderer.fill_rounded_rect(thumb_rect, 0.0f, toggle_on.background);
        draw_bevel_border(renderer,
                          thumb_rect,
                          is_pressed() ? BorderStyle::Sunken : BorderStyle::Raised,
                          toggle_on.bevel_light,
                          toggle_on.bevel_highlight,
                          toggle_on.bevel_shadow,
                          toggle_on.bevel_dark_shadow,
                          theme.border_width);
        return;
    }

    // Fluent, Metro and HC share one Slider look: a neutral track, an accent
    // value fill and a thumb of two concentric discs.
    const ButtonVisual& accent = accent_for_state(theme, state);

    if (track_gradient_)
    {
        if (track_gradient_checkerboard_)
        {
            renderer.fill_rect_pattern(track, Pattern::Checkerboard50, colors::White, Color::from_rgba(205, 205, 205));
        }
        renderer.fill_rounded_rect_gradient(track, track_radius, *track_gradient_);
    }
    else
    {
        // The theme's track colors when it has them (Fluent 1), otherwise the derived
        // approximation (`ControlChrome::slider_fills`).
        Color track_fill = theme.colors().text.with_alpha(is_enabled() ? 0.5f : 0.2f);
        if (detail::controls(theme).slider_fills)
        {
            track_fill = !is_enabled()                  ? detail::controls(theme).slider_fills->track_disabled
                         : state == ButtonState::Normal ? detail::controls(theme).slider_fills->track
                                                        : detail::controls(theme).slider_fills->track_hover;
        }
        renderer.fill_rounded_rect(track, track_radius, track_fill);

        // Luna's groove is a sunken 1px rule pair. The half-step collapses
        // draw_bevel_border()'s two rings into one edge.
        if (detail::controls(theme).slider_chrome)
        {
            draw_bevel_border(renderer,
                              track,
                              BorderStyle::Sunken,
                              detail::controls(theme).slider_chrome->track_edge_light,
                              detail::controls(theme).slider_chrome->track_edge_light,
                              detail::controls(theme).slider_chrome->track_edge_dark,
                              detail::controls(theme).slider_chrome->track_edge_dark,
                              theme.border_width * 0.5f);
        }

        // The value fill is the plain accent in every Fluent-lineage era.
        const Rect filled = filled_bounds(track, thumb_c);
        if (detail::controls(theme).slider_value_fill && filled.width > 0.0f && filled.height > 0.0f)
        {
            renderer.fill_rounded_rect(filled, track_radius, accent.background);
        }
    }

    // The outside tick rows take the track's resting color.
    Color tick_fill = theme.colors().text.with_alpha(is_enabled() ? 0.5f : 0.2f);
    if (detail::controls(theme).slider_chrome)
    {
        tick_fill = detail::controls(theme).slider_chrome->tick;
    }
    else if (detail::controls(theme).slider_fills)
    {
        tick_fill = is_enabled() ? detail::controls(theme).slider_fills->track
                                 : detail::controls(theme).slider_fills->track_disabled;
    }
    render_ticks(renderer, theme, track, tick_fill, TickGap, TickLength);

    if (detail::controls(theme).slider_thumb.shape == SliderThumbShape::RoundedRect)
    {
        // MDL2: one accent rounded rect, 8 x 24 with corner radius 4, no outer disc and no stroke.
        const float along  = thumb_diameter(theme, state);
        const float across = detail::controls(theme).slider_thumb.outer;
        const Size  size   = horizontal() ? Size{along, across} : Size{across, along};
        const Rect  thumb_rect{thumb_c.x - size.width * 0.5f, thumb_c.y - size.height * 0.5f, size.width, size.height};

        renderer.fill_rounded_rect(thumb_rect,
                                   detail::controls(theme).slider_thumb.radius,
                                   thumb_fill(theme, state, is_enabled()));

        // Luna: two accent bands carry the state, and a 1px bevel lit from the upper left goes
        // round the body. MDL2 leaves `slider_chrome` unset.
        if (detail::controls(theme).slider_chrome)
        {
            const detail::ControlChrome::SliderChrome&        c     = *detail::controls(theme).slider_chrome;
            const detail::ControlChrome::SliderChrome::Bands& bands = c.bands_for_state(state);

            // `near_end` is the lit band: the top one on a horizontal slider, the left on a vertical.
            const float span      = std::max(0.0f, along - c.band_inset * 2.0f);
            const float far_start = across - c.band_inset - c.band_extent;
            const Rect
                near_band = horizontal()
                                ? Rect{thumb_rect.left() + c.band_inset, thumb_rect.top() + c.band_inset, span, c.band_extent}
                                : Rect{thumb_rect.left() + c.band_inset, thumb_rect.top() + c.band_inset, c.band_extent, span};
            const Rect far_band = horizontal()
                                      ? Rect{thumb_rect.left() + c.band_inset, thumb_rect.top() + far_start, span, c.band_extent}
                                      : Rect{thumb_rect.left() + far_start, thumb_rect.top() + c.band_inset, c.band_extent, span};
            renderer.fill_rounded_rect(near_band, 0.0f, bands.near_end);
            renderer.fill_rounded_rect(far_band, 0.0f, bands.far_end);

            draw_bevel_border(renderer,
                              thumb_rect,
                              BorderStyle::Raised,
                              is_enabled() ? c.thumb_edge_light : c.thumb_edge_light_disabled,
                              is_enabled() ? c.thumb_edge_light : c.thumb_edge_light_disabled,
                              is_enabled() ? c.thumb_edge_dark : c.thumb_edge_dark_disabled,
                              is_enabled() ? c.thumb_edge_dark : c.thumb_edge_dark_disabled,
                              theme.border_width * 0.5f);
        }
        return;
    }

    const float outer_r = detail::controls(theme).slider_thumb.outer * 0.5f;
    renderer.fill_ellipse(thumb_c, outer_r, outer_r, lighten_track(theme.colors().background, 0.1f));
    renderer.stroke_ellipse(thumb_c, outer_r, outer_r, theme.colors().text.with_alpha(0.16f), 1.0f);

    // Fluent 1 grays the thumb on interaction (one gray on hover, another on press) and grows the
    // inner disc to the outer one's size, so this fill is the whole visible thumb.
    const float inner_d = thumb_diameter(theme, state);
    const float inner_r = inner_tween_.current(Rect{0.0f, 0.0f, inner_d, inner_d}).width * 0.5f;
    renderer.fill_ellipse(thumb_c, inner_r, inner_r, thumb_fill(theme, state, is_enabled()));
}

Rect SliderControl::focus_ring_rect(const Theme& theme) const
{
    const detail::ControlChrome& chrome = detail::controls(theme);
    if (chrome.slider_thumb.shape == SliderThumbShape::ClassicPointer)
    {
        // DrawFocusRect(rcClient): the whole control, flush with its edge (draw_focus_ring()
        // insets 4).
        return bounds().inflate(4.0f);
    }
    if (chrome.slider_thumb.shape == SliderThumbShape::ButtonBlock && chrome.slider_block)
    {
        return Rect{}; // focus shows as the thumb's hover face
    }
    const ButtonState state = visual_state();
    const Point       c     = thumb_center();
    const BorderStyle on    = chrome.toggle_track.for_state(state, true).border_style;
    if (on == BorderStyle::Raised || on == BorderStyle::Sunken)
    {
        const float d = thumb_diameter(theme, state);
        return Rect{c.x - d * 0.5f, c.y - d * 0.5f, d, d};
    }
    if (chrome.slider_thumb.shape == SliderThumbShape::RoundedRect)
    {
        const float along  = thumb_diameter(theme, state);
        const float across = chrome.slider_thumb.outer;
        const Size  size   = horizontal() ? Size{along, across} : Size{across, along};
        return Rect{c.x - size.width * 0.5f, c.y - size.height * 0.5f, size.width, size.height};
    }
    if (rings_whole_control(theme))
    {
        return detail::focus_ring_seed(focus_container(theme),
                                       Symmetric{.horizontal = FocusMargin, .vertical = 0.0f},
                                       theme.focus_ring_style());
    }
    const float r = chrome.slider_thumb.outer * 0.5f;
    return Rect{c.x - r, c.y - r, r * 2.0f, r * 2.0f};
}

float SliderControl::focus_ring_corner_radius(const Theme& theme) const
{
    const detail::ControlChrome& chrome = detail::controls(theme);
    const BorderStyle            on     = chrome.toggle_track.for_state(visual_state(), true).border_style;
    if (chrome.slider_thumb.shape == SliderThumbShape::ClassicPointer || on == BorderStyle::Raised ||
        on == BorderStyle::Sunken)
    {
        return 0.0f;
    }
    if (chrome.slider_thumb.shape == SliderThumbShape::RoundedRect)
    {
        return chrome.slider_thumb.radius;
    }
    // A ring around the track container takes the control corner radius. The thumb's own ring is round.
    return rings_whole_control(theme) ? theme.corner_radius : chrome.slider_thumb.outer * 0.5f;
}

std::unique_ptr<ui::Element> Slider::build() &&
{
    // Orientation and range are constructor arguments: the control has no set_orientation(),
    // and set_range() re-clamps, so both belong up front.
    auto node = std::make_unique<ui::SliderControl>(orientation, min.initial(), max.initial(), value.initial());
    apply(step, *node, [raw = node.get()](const double& v) { raw->set_step(v); });
    apply(direction_reversed, *node, [raw = node.get()](const bool& v) { raw->set_direction_reversed(v); });
    apply(header, *node, [raw = node.get()](const std::string& v) { raw->set_header(v); });

    // A value the control coerced (clamped to the range) goes back to a two-way property, so it ends up
    // equal to the control: also when the control already held the coerced value, and at build.
    // Written directly, not through on_value_changed, since the control's value may not change.
    const auto write = ui::shared_writer(value);
    apply(value,
          *node,
          [raw = node.get(), write](const double& v)
          {
              raw->set_value(v);
              if (write && raw->value() != v)
              {
                  (*write)(raw->value());
              }
          });
    detail::apply_range(min, max, *node);
    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });
    apply(tick_frequency, *node, [raw = node.get()](const double& v) { raw->set_tick_frequency(v); });
    apply(tick_placement, *node, [raw = node.get()](const ui::SliderTickPlacement& v) { raw->set_tick_placement(v); });
    apply(step_frequency, *node, [raw = node.get()](const double& v) { raw->set_step_frequency(v); });
    apply(snaps_to, *node, [raw = node.get()](const ui::SliderSnapsTo& v) { raw->set_snaps_to(v); });

    if (auto changed = ui::write_back(write, std::move(on_value_changed)))
    {
        node->on_value_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
