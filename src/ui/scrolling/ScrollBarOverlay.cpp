// SPDX-License-Identifier: MIT
#include "winui/ui/scrolling/ScrollBarOverlay.hpp"

#include "../themes/ControlChrome.hpp"
#include "ScrollArrowButton.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include <cmath>

namespace ui
{

namespace
{
// The bar a mouse gets. PointerEvent has no touch, so there is no touch indicator.
constexpr float BarThickness       = 12.0f; // expanded thumb+track+button thickness
constexpr float ThicknessCollapsed = 2.0f;  // thinner than the 8px minimum thumb width on purpose
constexpr float MinThumbLength     = 30.0f; // the minimum thumb length
constexpr float CornerRadius       = 3.0f;  // the bar's corner radius

// AnnotatedScrollBar's fallback small change ratio; there is no small_change here.
constexpr float StepRatio = 8.0f;

constexpr float ExpandDuration    = 0.167f; // expand duration
constexpr float ExpandBeginTime   = 0.40f;  // expand delay
constexpr float ContractDuration  = 0.167f; // contract duration
constexpr float ContractBeginTime = 0.50f;  // contract delay

// The bar and its separator both hide after 2 seconds idle.
constexpr float IdleHideDelay            = 2.0f;
constexpr float IdlePresenceFadeDuration = 0.083f; // the opacity change's duration

// The cubic Bezier (0,0, 0,1). With x1 == x2 == 0, x(t) = t^3, so t = cbrt(x) and
// y = 3t^2 - 2t^3, an ease-out curve.
[[nodiscard]] float scrollbar_ease(float x)
{
    x             = std::clamp(x, 0.0f, 1.0f);
    const float t = std::cbrt(x);
    return 3.0f * t * t - 2.0f * t * t * t;
}

// Scales the existing alpha instead of replacing it. Many tokens are already translucent, and
// with_alpha(progress) would turn a subtle wash into an opaque block.
[[nodiscard]] Color faded(const Color& c, float progress)
{
    return c.with_alpha(c.a * progress);
}
} // namespace

void ScrollBarOverlay::apply_theme(const Theme& theme) const
{
    const auto& era = detail::controls(theme).gutter_scroll_bar;
    gutter_         = era ? era->thickness : 0.0f;
    min_thumb_      = era ? era->min_thumb : 0.0f;
    fixed_thumb_    = !era                                    ? 0.0f
                      : orientation_ == Orientation::Vertical ? era->fixed_thumb_vertical
                                                              : era->fixed_thumb_horizontal;
}

float ScrollBarOverlay::thickness() const
{
    return gutter_ > 0.0f ? gutter_ : BarThickness;
}

// The era bar's buttons share a bar too short for both at full length.
float ScrollBarOverlay::button_length(const Rect& viewport) const
{
    return gutter_ > 0.0f ? std::min(gutter_, viewport.height * 0.5f) : BarThickness;
}

Rect ScrollBarOverlay::to_axis(const Rect& r) const
{
    return orientation_ == Orientation::Vertical ? r : Rect{r.y, r.x, r.height, r.width};
}

Point ScrollBarOverlay::to_axis(Point p) const
{
    return orientation_ == Orientation::Vertical ? p : Point{p.y, p.x};
}

Rect ScrollBarOverlay::band_bounds(const Rect& viewport) const
{
    return to_axis(band_bounds_v(to_axis(viewport)));
}

Rect ScrollBarOverlay::up_button_bounds(const Rect& viewport) const
{
    return to_axis(up_button_bounds_v(to_axis(viewport)));
}

Rect ScrollBarOverlay::down_button_bounds(const Rect& viewport) const
{
    return to_axis(down_button_bounds_v(to_axis(viewport)));
}

Rect ScrollBarOverlay::thumb_bounds(const Rect& viewport, float content_length, float offset) const
{
    return to_axis(thumb_bounds_v(to_axis(viewport), content_length, offset));
}

Rect ScrollBarOverlay::band_bounds_v(const Rect& viewport) const
{
    return Rect{viewport.right() - thickness(), viewport.top(), thickness(), viewport.height};
}

Rect ScrollBarOverlay::up_button_bounds_v(const Rect& viewport) const
{
    const Rect band = band_bounds_v(viewport);
    return Rect{band.left(), band.top(), band.width, button_length(viewport)};
}

Rect ScrollBarOverlay::down_button_bounds_v(const Rect& viewport) const
{
    const Rect  band   = band_bounds_v(viewport);
    const float length = button_length(viewport);
    return Rect{band.left(), band.bottom() - length, band.width, length};
}

float ScrollBarOverlay::step_amount(const Rect& viewport) const
{
    return viewport.height / StepRatio;
}

ButtonState ScrollBarOverlay::up_button_state() const
{
    if (pressed_up_ && hovered_up_)
    {
        return ButtonState::Pressed;
    }
    return hovered_up_ ? ButtonState::Hover : ButtonState::Normal;
}

ButtonState ScrollBarOverlay::down_button_state() const
{
    if (pressed_down_ && hovered_down_)
    {
        return ButtonState::Pressed;
    }
    return hovered_down_ ? ButtonState::Hover : ButtonState::Normal;
}

float ScrollBarOverlay::thumb_length(const Rect& viewport, float content_length) const
{
    // The step buttons' rows are reserved unconditionally, so the track is always two buttons
    // shorter than the viewport.
    const float track_height = std::max(0.0f, viewport.height - 2.0f * button_length(viewport));
    if (gutter_ > 0.0f && fixed_thumb_ > 0.0f)
    {
        // An era scroll box that does not show the viewport's share: the same size always.
        return std::min(fixed_thumb_, track_height);
    }
    if (content_length <= 0.0f)
    {
        return track_height;
    }
    const float proportional = track_height * (viewport.height / content_length);
    // The era bar keeps its thumb at least half a button long, or its era's own minimum.
    const float minimum = gutter_ > 0.0f ? (min_thumb_ > 0.0f ? min_thumb_ : gutter_ * 0.5f) : MinThumbLength;
    return std::clamp(proportional, std::min(minimum, track_height), track_height);
}

float ScrollBarOverlay::thumb_top(const Rect& viewport, float content_length, float offset, float length) const
{
    const float button       = button_length(viewport);
    const float track_top    = viewport.top() + button;
    const float track_height = std::max(0.0f, viewport.height - 2.0f * button);
    const float max_offset   = std::max(0.0f, content_length - viewport.height);
    const float track        = std::max(0.0f, track_height - length);
    if (max_offset <= 0.0f || track <= 0.0f)
    {
        return track_top;
    }
    return track_top + std::clamp(offset / max_offset, 0.0f, 1.0f) * track;
}

Rect ScrollBarOverlay::thumb_bounds_v(const Rect& viewport, float content_length, float offset) const
{
    // Settled geometry for hit testing; render() animates its own. Flush right in both states:
    // the collapsed thumb is not centered in the bar.
    const float across = gutter_ > 0.0f || expanded() ? thickness() : ThicknessCollapsed;
    const float length = thumb_length(viewport, content_length);
    const float top    = thumb_top(viewport, content_length, offset, length);
    return Rect{viewport.right() - across, top, across, length};
}

bool ScrollBarOverlay::update(Duration delta)
{
    const auto elapsed_seconds = static_cast<float>(delta.count() / 1000.0);
    if (gutter_ > 0.0f)
    {
        // The era bar has no expand animation and never hides.
        presence_ = 1.0f;
        return false;
    }
    const bool target = expanded();
    if (target != target_expanded_)
    {
        // Flipped mid-animation: restart the timer from this frame, since elapsed_seconds already
        // belongs to the new state, and ease on from the current progress_.
        target_expanded_ = target;
        state_time_      = elapsed_seconds;
        progress_start_  = progress_;
    }
    else
    {
        state_time_ += elapsed_seconds;
    }

    const float begin_time   = target_expanded_ ? ExpandBeginTime : ContractBeginTime;
    const float duration     = target_expanded_ ? ExpandDuration : ContractDuration;
    const float target_value = target_expanded_ ? 1.0f : 0.0f;

    if (state_time_ >= begin_time)
    {
        const float t     = (state_time_ - begin_time) / duration;
        const float eased = animations_enabled() ? scrollbar_ease(t) : 1.0f;
        progress_         = std::lerp(progress_start_, target_value, eased);
    }

    // Hovering or dragging keeps the idle clock reset; the bar never hides mid-interaction.
    if (hovered_ || dragging_)
    {
        idle_seconds_ = 0.0f;
    }
    else
    {
        idle_seconds_ += elapsed_seconds;
    }
    const float presence_target = idle_seconds_ >= IdleHideDelay ? 0.0f : 1.0f;
    if (presence_ != presence_target)
    {
        const float step = animations_enabled() ? elapsed_seconds / IdlePresenceFadeDuration : 1.0f;
        presence_ = presence_target > presence_ ? std::min(1.0f, presence_ + step) : std::max(0.0f, presence_ - step);
    }

    return progress_ != target_value || presence_ != presence_target;
}

std::optional<Duration> ScrollBarOverlay::fade_wait() const
{
    if (hovered_ || dragging_ || presence_ != 1.0f || idle_seconds_ >= IdleHideDelay)
    {
        return std::nullopt;
    }
    // Whole milliseconds and one more, so the float idle clock is past the delay when it wakes.
    return Duration{std::ceil(static_cast<double>(IdleHideDelay - idle_seconds_) * 1000.0) + 1.0};
}

float ScrollBarOverlay::animation_progress() const
{
    return progress_;
}

bool ScrollBarOverlay::handle_pointer(const PointerEvent& e_in, const Rect& viewport_in, float content_length, float& offset)
{
    // Axis space: the horizontal bar runs the vertical code on transposed geometry.
    const Rect   viewport = to_axis(viewport_in);
    PointerEvent e        = e_in;
    e.position            = to_axis(e_in.position);

    if (!is_scrollable(viewport, content_length))
    {
        hovered_ = dragging_ = hovered_up_ = hovered_down_ = hovered_thumb_ = pressed_up_ = pressed_down_ = false;
        pressed_page_                                                                                     = 0;
        return false;
    }

    const float max_offset = std::max(0.0f, content_length - viewport.height);

    switch (e.action)
    {
        case PointerAction::Move:
        {
            if (dragging_)
            {
                const float length  = thumb_length(viewport, content_length);
                const float track   = std::max(1.0f, viewport.height - 2.0f * button_length(viewport) - length);
                const float delta_y = e.position.y - drag_start_pointer_y_;
                offset              = std::clamp(drag_start_offset_ + delta_y / track * max_offset, 0.0f, max_offset);
                return true;
            }
            hovered_       = band_bounds_v(viewport).contains(e.position);
            hovered_up_    = hovered_ && up_button_bounds_v(viewport).contains(e.position);
            hovered_down_  = hovered_ && down_button_bounds_v(viewport).contains(e.position);
            hovered_thumb_ = hovered_ && thumb_bounds_v(viewport, content_length, offset).contains(e.position);
            return hovered_; // claim the event so hidden content doesn't see a false hover under the band
        }

        case PointerAction::Down:
        {
            if (e.button != PointerButton::Left || !band_bounds_v(viewport).contains(e.position))
            {
                return false;
            }
            if (up_button_bounds_v(viewport).contains(e.position))
            {
                pressed_up_ = true;
                offset      = std::max(0.0f, offset - step_amount(viewport));
                return true;
            }
            if (down_button_bounds_v(viewport).contains(e.position))
            {
                pressed_down_ = true;
                offset        = std::min(max_offset, offset + step_amount(viewport));
                return true;
            }
            const Rect thumb = thumb_bounds_v(viewport, content_length, offset);
            if (thumb.contains(e.position))
            {
                dragging_             = true;
                drag_start_pointer_y_ = e.position.y;
                drag_start_offset_    = offset;
            }
            else
            {
                // Track click: page toward the click.
                pressed_page_ = e.position.y < thumb.top() ? -1 : 1;
                offset        = e.position.y < thumb.top() ? std::max(0.0f, offset - viewport.height)
                                                           : std::min(max_offset, offset + viewport.height);
            }
            return true;
        }

        case PointerAction::Up:
            pressed_up_ = pressed_down_ = false;
            pressed_page_               = 0;
            if (dragging_)
            {
                dragging_ = false;
                return true;
            }
            return false;

        case PointerAction::Leave:
            hovered_ = hovered_up_ = hovered_down_ = hovered_thumb_ = false;
            return false;

        default:
            return false;
    }
}

void ScrollBarOverlay::render(Renderer& renderer, const Theme& theme, const Rect& viewport_in, float content_length, float offset) const
{
    const Rect viewport   = to_axis(viewport_in);
    const bool scrollable = is_scrollable(viewport, content_length);
    if (!scrollable && !always_visible_)
    {
        return;
    }
    if (gutter_ > 0.0f && detail::controls(theme).gutter_scroll_bar)
    {
        render_gutter(renderer, theme, viewport, content_length, offset, scrollable);
        return;
    }
    if (!scrollable)
    {
        // Visible with nothing to scroll: the thumb spans the whole track.
        content_length = viewport.height;
        offset         = 0.0f;
    }

    const float progress  = animation_progress();
    const float thickness = std::lerp(ThicknessCollapsed, BarThickness, progress);
    const float length    = thumb_length(viewport, content_length);
    const float top       = thumb_top(viewport, content_length, offset, length);
    const Rect  thumb{viewport.right() - thickness, top, thickness, length};

    const bool      vertical = orientation_ == Orientation::Vertical;
    const IconGlyph back     = vertical ? glyphs::ScrollUp : glyphs::ScrollLeft;
    const IconGlyph forward  = vertical ? glyphs::ScrollDown : glyphs::ScrollRight;

    // One clock for thumb, track and buttons, with no separate 83 ms timer for track and buttons.
    if (progress > 0.0f)
    {
        const Rect track{thumb.left(),
                         viewport.top() + BarThickness,
                         thumb.width,
                         std::max(0.0f, viewport.height - 2.0f * BarThickness)};
        renderer.fill_rounded_rect(to_axis(track), CornerRadius, faded(theme.surfaces.scrollbar_track, progress));

        // The arrow uses scrollbar_thumb: arrow and thumb are the same strong fill color.
        const Rect          up        = to_axis(up_button_bounds_v(viewport));
        const ButtonVisual& up_visual = theme.for_state(up_button_state());
        renderer.fill_rounded_rect(up, CornerRadius, faded(up_visual.background, progress));
        draw_icon(renderer, back, up, faded(theme.surfaces.scrollbar_thumb, progress), 0.0f, theme.icon_font_family);

        const Rect          down        = to_axis(down_button_bounds_v(viewport));
        const ButtonVisual& down_visual = theme.for_state(down_button_state());
        renderer.fill_rounded_rect(down, CornerRadius, faded(down_visual.background, progress));
        draw_icon(renderer, forward, down, faded(theme.surfaces.scrollbar_thumb, progress), 0.0f, theme.icon_font_family);
    }

    // presence_ only drops below 1 while collapsed, so this is the idle fade.
    renderer.fill_rounded_rect(to_axis(thumb), CornerRadius, faded(theme.surfaces.scrollbar_thumb, presence_));
}

namespace
{
using GutterBar = detail::ControlChrome::GutterScrollBar;

[[nodiscard]] LinearGradientBrush across(const GradientStops& stops, bool vertical_bar)
{
    return LinearGradientBrush{stops, vertical_bar ? GradientAxis::Horizontal : GradientAxis::Vertical};
}

// Aero's part chrome: the part sits one pixel in from the bar's near edge across it, a soft
// shadow line under it, then the outline with the fill, then the white inner ring. Strokes run on
// pixel centers, so each rect is the part's less half a pixel all round. Returns what is left
// for the glyph.
Rect draw_gutter_part(Renderer& renderer, Rect cell, bool vertical_bar, const GutterBar& bar, const GutterBar::Part& part)
{
    if (vertical_bar)
    {
        cell.x += 1.0f;
        cell.width -= 1.0f;
    }
    else
    {
        cell.y += 1.0f;
        cell.height -= 1.0f;
    }
    Rect r{cell.x + 0.5f, cell.y + 0.5f, cell.width - 1.0f, cell.height - 1.0f};
    if (r.width > 0.0f && r.height > 2.0f)
    {
        renderer.stroke_rounded_rect(Rect{r.x, r.y + 2.0f, r.width, r.height - 2.0f}, 3.0f, bar.shadow, 1.0f);
        r.height -= 1.0f;
        r.width = std::max(0.0f, r.width - 1.0f);
    }
    if (r.width >= 2.0f && r.height >= 2.0f)
    {
        renderer.fill_rounded_rect_gradient(r, 1.0f, across(part.fill, vertical_bar));
        renderer.stroke_rounded_rect(r, 1.0f, part.border, 1.0f);
        r = r.inset(Thickness{1.0f});
        if (r.width >= 2.0f && r.height >= 2.0f)
        {
            renderer.stroke_rounded_rect(r, 0.5f, bar.inner_border, 1.0f);
            r = r.inset(Thickness{1.0f});
        }
    }
    return r;
}

// The 7 x 4 arrow (4 x 7 across), centered.
void draw_gutter_arrow(Renderer& renderer, const Rect& area, ArrowDirection direction, const Color& color)
{
    const bool         sideways = direction == ArrowDirection::Left || direction == ArrowDirection::Right;
    const float        w        = sideways ? 4.0f : 7.0f;
    const float        h        = sideways ? 7.0f : 4.0f;
    const float        x        = area.x + (area.width - w) * 0.5f;
    const float        y        = area.y + (area.height - h) * 0.5f;
    std::vector<Point> points;
    switch (direction)
    {
        case ArrowDirection::Up:
            points = {Point{x, y + h}, Point{x + w * 0.5f, y}, Point{x + w, y + h}};
            break;
        case ArrowDirection::Down:
            points = {Point{x, y}, Point{x + w, y}, Point{x + w * 0.5f, y + h}};
            break;
        case ArrowDirection::Left:
            points = {Point{x + w, y}, Point{x, y + h * 0.5f}, Point{x + w, y + h}};
            break;
        case ArrowDirection::Right:
            points = {Point{x, y}, Point{x + w, y + h * 0.5f}, Point{x, y + h}};
            break;
    }
    renderer.fill_polygon(FillPolygon{std::move(points), color});
}

// Three 2 px lines across the thumb's middle, 3 apart, each in a light outline, once the thumb
// is longer than 15.
void draw_gripper(Renderer& renderer, const Rect& area, bool vertical_bar, const GutterBar& bar, const GradientStops& lines)
{
    const float along = vertical_bar ? area.height : area.width;
    const float cross = vertical_bar ? area.width : area.height;
    if (along <= 15.0f || cross <= 2.0f)
    {
        return;
    }
    const float width = std::min(7.0f, cross);
    for (int i = 0; i < 9; i += 3)
    {
        const float step = static_cast<float>(i);
        if (vertical_bar)
        {
            const float x = area.x + (area.width - width) * 0.5f;
            const float y = area.y + area.height * 0.5f - 4.0f + step;
            renderer.fill_rounded_rect(Rect{x - 0.5f, y - 0.5f, width + 1.0f, 3.0f}, 0.0f, bar.gripper_shadow);
            renderer.fill_rounded_rect_gradient(Rect{x, y, width, 2.0f},
                                                0.0f,
                                                LinearGradientBrush{lines, GradientAxis::Vertical});
        }
        else
        {
            const float x = area.x + area.width * 0.5f - 4.0f + step;
            const float y = area.y + (area.height - width) * 0.5f;
            renderer.fill_rounded_rect(Rect{x - 0.5f, y - 0.5f, 3.0f, width + 1.0f}, 0.0f, bar.gripper_shadow);
            renderer.fill_rounded_rect_gradient(Rect{x, y, 2.0f, width},
                                                0.0f,
                                                LinearGradientBrush{lines, GradientAxis::Horizontal});
        }
    }
}
} // namespace

void ScrollBarOverlay::render_gutter(
    Renderer&    renderer,
    const Theme& theme,
    const Rect&  viewport,
    float        content_length,
    float        offset,
    bool         enabled) const
{
    const GutterBar& bar = *detail::controls(theme).gutter_scroll_bar;
    switch (bar.style)
    {
        case GutterBar::Style::Aero:
            break;
        case GutterBar::Style::Luna:
            render_luna(renderer, theme, viewport, content_length, offset, enabled);
            return;
        case GutterBar::Style::Classic:
        case GutterBar::Style::Win3x:
        case GutterBar::Style::Win1x2x:
            render_classic(renderer, theme, viewport, content_length, offset, enabled);
            return;
    }
    const bool vertical = orientation_ == Orientation::Vertical;

    // The bar's own background runs under its buttons too.
    const Rect band = to_axis(band_bounds_v(viewport));
    if (enabled)
    {
        renderer.fill_rounded_rect_gradient(band, 0.0f, across(bar.track, vertical));
    }
    else
    {
        renderer.fill_rounded_rect(band, 0.0f, bar.track_disabled);
    }

    const auto arrow = [&](const Rect& cell, ButtonState state, ArrowDirection direction)
    {
        // The arrows are only enabled, and so only chrome, while the pointer is over the bar.
        if (!enabled || !hovered_)
        {
            draw_gutter_arrow(renderer, cell, direction, bar.idle_arrow);
            return;
        }
        const GutterBar::Part& part = bar.part_for_state(state);
        draw_gutter_arrow(renderer, draw_gutter_part(renderer, cell, vertical, bar, part), direction, part.glyph);
    };
    arrow(to_axis(up_button_bounds_v(viewport)), up_button_state(), vertical ? ArrowDirection::Up : ArrowDirection::Left);
    arrow(to_axis(down_button_bounds_v(viewport)),
          down_button_state(),
          vertical ? ArrowDirection::Down : ArrowDirection::Right);

    // A disabled track hides its thumb, and so does one too short for it.
    const float track  = viewport.height - 2.0f * button_length(viewport);
    const float length = thumb_length(viewport, content_length);
    if (!enabled || track < gutter_ * 0.5f || length <= 0.0f)
    {
        return;
    }
    const ButtonState state = dragging_        ? ButtonState::Pressed
                              : hovered_thumb_ ? ButtonState::Hover
                                               : ButtonState::Normal;
    const Rect thumb{viewport.right() - gutter_, thumb_top(viewport, content_length, offset, length), gutter_, length};
    const Rect inside = draw_gutter_part(renderer, to_axis(thumb), vertical, bar, bar.part_for_state(state));
    draw_gripper(renderer,
                 inside,
                 vertical,
                 bar,
                 state == ButtonState::Pressed ? bar.gripper_pressed
                 : state == ButtonState::Hover ? bar.gripper_hover
                                               : bar.gripper_rest);
}


Rect ScrollBarOverlay::pressed_page_rect(const Rect& viewport, float content_length, float offset) const
{
    if (pressed_page_ == 0)
    {
        return {};
    }
    const Rect band  = band_bounds_v(viewport);
    const Rect up    = up_button_bounds_v(viewport);
    const Rect down  = down_button_bounds_v(viewport);
    const Rect thumb = thumb_bounds_v(viewport, content_length, offset);
    return pressed_page_ < 0 ? Rect{band.x, up.bottom(), band.width, std::max(0.0f, thumb.top() - up.bottom())}
                             : Rect{band.x, thumb.bottom(), band.width, std::max(0.0f, down.top() - thumb.bottom())};
}

namespace
{
// Luna's part chrome: in to pixel centers and one pixel off the bar's near edge, a
// soft gradient shadow line under the part, then the gradient inside a white ring, then the
// inner ring. Returns what is left for the glyph.
Rect draw_luna_part(Renderer&                   renderer,
                    Rect                        cell,
                    bool                        vertical_bar,
                    const GutterBar::LunaParts& luna,
                    const GutterBar::Part&      part,
                    GradientAxis                fill_axis,
                    bool                        enabled)
{
    Rect r{cell.x + 0.5f, cell.y + 0.5f, cell.width - 1.0f, cell.height - 1.0f};
    if (vertical_bar)
    {
        r.x += 1.0f;
        r.width -= 1.0f;
    }
    else
    {
        r.y += 1.0f;
        r.height -= 1.0f;
    }
    if (r.width > 0.0f && r.height > 2.0f)
    {
        renderer.draw_geometry(
            DrawGeometry{.geometry = PathGeometry::rectangle(Rect{r.x, r.y + 2.0f, r.width, r.height - 2.0f}, 3.0f, 3.0f),
                         .stroke = LinearGradientBrush{enabled ? luna.shadow : luna.shadow_disabled, GradientAxis::Vertical}});
        r.height -= 1.0f;
        r.width = std::max(0.0f, r.width - 1.0f);
    }
    if (r.width >= 2.0f && r.height >= 2.0f)
    {
        renderer.fill_rounded_rect_gradient(r, 2.0f, LinearGradientBrush{part.fill, fill_axis});
        renderer.stroke_rounded_rect(r, 2.0f, luna.outer_border, 1.0f);
        r = r.inset(Thickness{1.0f});
        if (r.width >= 2.0f && r.height >= 2.0f)
        {
            renderer.stroke_rounded_rect(r, 1.5f, part.border, 1.0f);
            r = r.inset(Thickness{1.0f});
        }
    }
    return r;
}

// Luna's arrows: a filled six-point chevron, 9 x 8 (8 x 9 sideways), centered.
void draw_luna_arrow(Renderer& renderer, const Rect& area, ArrowDirection direction, const Color& color)
{
    const bool         sideways = direction == ArrowDirection::Left || direction == ArrowDirection::Right;
    const float        w        = sideways ? 8.0f : 9.0f;
    const float        h        = sideways ? 9.0f : 8.0f;
    const float        x        = area.x + (area.width - w) * 0.5f;
    const float        y        = area.y + (area.height - h) * 0.5f;
    std::vector<Point> points;
    switch (direction)
    {
        case ArrowDirection::Up:
            points = {{0.0f, 4.5f}, {4.5f, 0.0f}, {9.0f, 4.5f}, {7.5f, 6.0f}, {4.5f, 3.0f}, {1.5f, 6.0f}};
            break;
        case ArrowDirection::Down:
            points = {{0.0f, 3.5f}, {4.5f, 8.0f}, {9.0f, 3.5f}, {7.5f, 2.0f}, {4.5f, 5.0f}, {1.5f, 2.0f}};
            break;
        case ArrowDirection::Left:
            points = {{4.5f, 0.0f}, {0.0f, 4.5f}, {4.5f, 9.0f}, {6.0f, 7.5f}, {3.0f, 4.5f}, {6.0f, 1.5f}};
            break;
        case ArrowDirection::Right:
            points = {{3.5f, 0.0f}, {8.0f, 4.5f}, {3.5f, 9.0f}, {2.0f, 7.5f}, {5.0f, 4.5f}, {2.0f, 1.5f}};
            break;
    }
    for (Point& p : points)
    {
        p = Point{x + p.x, y + p.y};
    }
    renderer.fill_polygon(FillPolygon{std::move(points), color});
}

// Luna's gripper: four 1 px bars at a 2 px pitch across the thumb's middle, each with a light
// copy one pixel down and right.
void draw_luna_gripper(Renderer& renderer, const Rect& area, bool vertical_bar, const Color& line, const Color& shadow)
{
    if (vertical_bar ? (area.width <= 2.0f || area.height <= 8.0f) : (area.height <= 2.0f || area.width <= 8.0f))
    {
        return;
    }
    const float across = std::min(6.0f, vertical_bar ? area.width : area.height) - 1.0f;
    for (int i = 0; i < 8; i += 2)
    {
        const float step = static_cast<float>(i);
        if (vertical_bar)
        {
            const float x = area.x + (area.width - across - 1.0f) * 0.5f;
            const float y = area.y + area.height * 0.5f - 4.0f + step;
            renderer.fill_rounded_rect(Rect{x, y, across, 1.0f}, 0.0f, line);
            renderer.fill_rounded_rect(Rect{x + 1.0f, y + 1.0f, across, 1.0f}, 0.0f, shadow);
        }
        else
        {
            const float x = area.x + area.width * 0.5f - 4.0f + step;
            const float y = area.y + (area.height - across - 1.0f) * 0.5f;
            renderer.fill_rounded_rect(Rect{x, y, 1.0f, across}, 0.0f, line);
            renderer.fill_rounded_rect(Rect{x + 1.0f, y + 1.0f, 1.0f, across}, 0.0f, shadow);
        }
    }
}

// The arrow of Windows 1.x to 3.x: a solid head `depth` deep on a 3 px stem `stem` long, centered.
void draw_stemmed_arrow(Renderer& renderer, const Rect& area, ArrowDirection direction, const Color& color, float depth, float stem)
{
    const float head     = 2.0f * depth - 1.0f;
    const bool  sideways = direction == ArrowDirection::Left || direction == ArrowDirection::Right;
    const float w        = sideways ? depth + stem : head;
    const float h        = sideways ? head : depth + stem;
    const float x        = std::round(area.x + (area.width - w) * 0.5f);
    const float y        = std::round(area.y + (area.height - h) * 0.5f);
    const float inset    = (head - 3.0f) * 0.5f;
    switch (direction)
    {
        case ArrowDirection::Up:
            draw_classic_arrow(renderer, Rect{x, y, head, depth}, direction, color, depth);
            renderer.fill_rounded_rect(Rect{x + inset, y + depth, 3.0f, stem}, 0.0f, color);
            break;
        case ArrowDirection::Down:
            renderer.fill_rounded_rect(Rect{x + inset, y, 3.0f, stem}, 0.0f, color);
            draw_classic_arrow(renderer, Rect{x, y + stem, head, depth}, direction, color, depth);
            break;
        case ArrowDirection::Left:
            draw_classic_arrow(renderer, Rect{x, y, depth, head}, direction, color, depth);
            renderer.fill_rounded_rect(Rect{x + depth, y + inset, stem, 3.0f}, 0.0f, color);
            break;
        case ArrowDirection::Right:
            renderer.fill_rounded_rect(Rect{x, y + inset, stem, 3.0f}, 0.0f, color);
            draw_classic_arrow(renderer, Rect{x + stem, y, depth, head}, direction, color, depth);
            break;
    }
}

[[nodiscard]] Color inverted(const Color& c)
{
    return Color{1.0f - c.r, 1.0f - c.g, 1.0f - c.b, c.a};
}
} // namespace

namespace detail
{

bool draw_scroll_arrow_button(Renderer&      renderer,
                              const Theme&   theme,
                              const Rect&    cell,
                              ArrowDirection direction,
                              ButtonState    state,
                              bool           enabled)
{
    const auto& gutter = controls(theme).gutter_scroll_bar;
    if (!gutter || gutter->style == GutterBar::Style::Aero)
    {
        return false;
    }
    const GutterBar& bar          = *gutter;
    const bool       vertical_bar = direction == ArrowDirection::Up || direction == ArrowDirection::Down;
    if (bar.style == GutterBar::Style::Luna && bar.luna)
    {
        const GutterBar::LunaParts& luna = *bar.luna;
        const GutterBar::Part&      part = !enabled                        ? luna.disabled
                                           : state == ButtonState::Pressed ? luna.arrow_pressed
                                           : state == ButtonState::Hover   ? luna.arrow_hover
                                                                           : luna.arrow_rest;
        draw_luna_arrow(renderer,
                        draw_luna_part(renderer, cell, vertical_bar, luna, part, GradientAxis::Diagonal, enabled),
                        direction,
                        part.glyph);
        return true;
    }

    const ButtonVisual& face  = theme.for_state(ButtonState::Normal);
    const bool          early = bar.style == GutterBar::Style::Win1x2x;
    const bool          win3x = bar.style == GutterBar::Style::Win3x;
    const auto          glyph = [&](const Rect& area, const Color& color)
    {
        if (early)
        {
            draw_stemmed_arrow(renderer, area, direction, color, 5.0f, 5.0f);
        }
        else if (win3x)
        {
            draw_stemmed_arrow(renderer, area, direction, color, 4.0f, 3.0f);
        }
        else
        {
            draw_classic_arrow(renderer, area, direction, color, 4.0f);
        }
    };
    if (early)
    {
        // The art has no pressed variant.
        renderer.fill_rounded_rect(cell, 0.0f, bar.box);
        stroke_rounded_rect_inside(renderer, cell, 0.0f, bar.frame, 1.0f);
        glyph(cell, bar.arrow);
        return true;
    }
    // The 3.x art leaves 2 px above and left of its content and 3 below and right.
    const Rect content = win3x ? Rect{cell.x + 2.0f, cell.y + 2.0f, cell.width - 5.0f, cell.height - 5.0f} : cell;
    const Rect nudged{content.x + 1.0f, content.y + 1.0f, content.width, content.height};
    renderer.fill_rounded_rect(cell, 0.0f, face.background);
    if (state == ButtonState::Pressed && enabled)
    {
        if (win3x)
        {
            draw_win3x_border(renderer,
                              cell,
                              BorderStyle::Sunken,
                              face.bevel_frame.value_or(face.bevel_dark_shadow),
                              face.bevel_highlight,
                              face.bevel_shadow,
                              1.0f);
        }
        else
        {
            stroke_rounded_rect_inside(renderer, cell, 0.0f, face.bevel_shadow, 1.0f);
        }
        glyph(nudged, bar.arrow);
        return true;
    }
    if (win3x)
    {
        draw_win3x_border(renderer,
                          cell,
                          BorderStyle::Raised,
                          face.bevel_frame.value_or(face.bevel_dark_shadow),
                          face.bevel_highlight,
                          face.bevel_shadow,
                          1.0f);
    }
    else
    {
        draw_bevel_border(renderer,
                          cell,
                          BorderStyle::Raised,
                          face.bevel_light,
                          face.bevel_highlight,
                          face.bevel_shadow,
                          face.bevel_dark_shadow,
                          1.0f);
    }
    if (!enabled)
    {
        // DrawState's DSS_DISABLED: the arrow in 3DSHADOW over a 3DHILIGHT copy one pixel down and right.
        glyph(nudged, face.bevel_highlight);
        glyph(content, face.bevel_shadow);
        return true;
    }
    glyph(content, bar.arrow);
    return true;
}

} // namespace detail

// Luna's scroll bar: the bevelled shaft across the bar (its held half pressed), arrows lit on
// their own hover with a chevron, and a thumb with four gripper bars. Disabled, the arrows go gray
// and the thumb goes.
void ScrollBarOverlay::render_luna(Renderer&    renderer,
                                   const Theme& theme,
                                   const Rect&  viewport,
                                   float        content_length,
                                   float        offset,
                                   bool         enabled) const
{
    const GutterBar&            bar      = *detail::controls(theme).gutter_scroll_bar;
    const GutterBar::LunaParts& luna     = *bar.luna;
    const bool                  vertical = orientation_ == Orientation::Vertical;
    const GradientAxis          across   = vertical ? GradientAxis::Horizontal : GradientAxis::Vertical;

    renderer.fill_rounded_rect_gradient(to_axis(band_bounds_v(viewport)), 0.0f, LinearGradientBrush{bar.track, across});
    if (const Rect page = pressed_page_rect(viewport, content_length, offset); enabled && !page.empty())
    {
        renderer.fill_rounded_rect_gradient(to_axis(page), 0.0f, LinearGradientBrush{luna.track_pressed, across});
    }

    const auto arrow = [&](const Rect& cell, ButtonState state, ArrowDirection direction)
    { detail::draw_scroll_arrow_button(renderer, theme, cell, direction, state, enabled); };
    arrow(to_axis(up_button_bounds_v(viewport)), up_button_state(), vertical ? ArrowDirection::Up : ArrowDirection::Left);
    arrow(to_axis(down_button_bounds_v(viewport)),
          down_button_state(),
          vertical ? ArrowDirection::Down : ArrowDirection::Right);

    const float track  = viewport.height - 2.0f * button_length(viewport);
    const float length = thumb_length(viewport, content_length);
    if (!enabled || track < gutter_ * 0.5f || length <= 0.0f)
    {
        return;
    }
    const int  state = dragging_ ? 2 : hovered_thumb_ ? 1 : 0;
    const Rect thumb{viewport.right() - gutter_, thumb_top(viewport, content_length, offset, length), gutter_, length};
    const GutterBar::Part& part   = state == 2 ? luna.thumb_pressed : state == 1 ? luna.thumb_hover : luna.thumb_rest;
    const Rect             inside = draw_luna_part(renderer, to_axis(thumb), vertical, luna, part, across, true);
    draw_luna_gripper(renderer,
                      inside,
                      vertical,
                      luna.gripper[static_cast<std::size_t>(state)],
                      luna.gripper_shadow[static_cast<std::size_t>(state)]);
}

// The classic eras' bars. Windows 95 to 2000: DrawFrameControl's raised arrow buttons, flat with
// a 3DSHADOW frame and the arrow nudged while held, embossed while disabled; a raised scroll box;
// the shaft dithered or flat, inverted under a held page click. Windows 3.x: the same parts in
// the era's 1 : 2 edge, pressed as its single dark top-left edge, with stemmed arrows and a fixed
// box. Windows 1.x/2.x: arrow boxes and a white fixed scroll box framed in COLOR_WINDOWFRAME on a
// flat shaft, the arrows with no pressed look. No era hot-tracks.
void ScrollBarOverlay::render_classic(
    Renderer&    renderer,
    const Theme& theme,
    const Rect&  viewport,
    float        content_length,
    float        offset,
    bool         enabled) const
{
    const GutterBar&    bar      = *detail::controls(theme).gutter_scroll_bar;
    const ButtonVisual& face     = theme.for_state(ButtonState::Normal);
    const bool          vertical = orientation_ == Orientation::Vertical;
    const bool          early    = bar.style == GutterBar::Style::Win1x2x;
    const bool          win3x    = bar.style == GutterBar::Style::Win3x;

    const auto shaft = [&](const Rect& r, bool held)
    {
        if (bar.dither)
        {
            renderer.fill_rect_pattern(r,
                                       Pattern::Checkerboard50,
                                       held ? inverted(bar.shaft_alt) : bar.shaft_alt,
                                       held ? inverted(bar.shaft) : bar.shaft);
        }
        else
        {
            renderer.fill_rounded_rect(r, 0.0f, held ? inverted(bar.shaft) : bar.shaft);
        }
    };
    shaft(to_axis(band_bounds_v(viewport)), false);
    if (const Rect page = pressed_page_rect(viewport, content_length, offset); enabled && !page.empty())
    {
        shaft(to_axis(page), true);
    }

    const auto raised = [&](const Rect& r)
    {
        renderer.fill_rounded_rect(r, 0.0f, face.background);
        if (win3x)
        {
            draw_win3x_border(renderer,
                              r,
                              BorderStyle::Raised,
                              face.bevel_frame.value_or(face.bevel_dark_shadow),
                              face.bevel_highlight,
                              face.bevel_shadow,
                              1.0f);
        }
        else
        {
            draw_bevel_border(renderer,
                              r,
                              BorderStyle::Raised,
                              face.bevel_light,
                              face.bevel_highlight,
                              face.bevel_shadow,
                              face.bevel_dark_shadow,
                              1.0f);
        }
    };
    const auto arrow = [&](const Rect& cell, bool held, ArrowDirection direction)
    {
        detail::draw_scroll_arrow_button(renderer, theme, cell, direction, held ? ButtonState::Pressed : ButtonState::Normal, enabled);
    };
    arrow(to_axis(up_button_bounds_v(viewport)),
          pressed_up_ && hovered_up_,
          vertical ? ArrowDirection::Up : ArrowDirection::Left);
    arrow(to_axis(down_button_bounds_v(viewport)),
          pressed_down_ && hovered_down_,
          vertical ? ArrowDirection::Down : ArrowDirection::Right);

    const float track  = viewport.height - 2.0f * button_length(viewport);
    const float length = thumb_length(viewport, content_length);
    if (!enabled || track < length || length <= 0.0f)
    {
        return;
    }
    const Rect thumb = to_axis(
        Rect{viewport.right() - gutter_, thumb_top(viewport, content_length, offset, length), gutter_, length});
    if (early)
    {
        renderer.fill_rounded_rect(thumb, 0.0f, bar.box);
        stroke_rounded_rect_inside(renderer, thumb, 0.0f, bar.frame, 1.0f);
        return;
    }
    raised(thumb);
}

} // namespace ui
