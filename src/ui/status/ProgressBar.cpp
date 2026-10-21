// SPDX-License-Identifier: MIT
#include "winui/ui/status/ProgressBar.hpp"

#include "../../render/Compositor.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/PathGeometry.hpp"
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
#include <span>
#include <utility>

#include <cmath>

namespace ui
{

namespace
{
constexpr float ProgressBarDefaultLength   = 200.0f; // nominal desired width, as Slider
constexpr float Height                     = 3.0f;   // the minimum height, which is also the real height
constexpr float TrackHeight                = 1.0f;   // the track's height
constexpr float TrackRadius                = 0.5f;   // the track's corner radius
constexpr float ProgressBarIndicatorRadius = 1.5f;   // the indicator's corner radius

// The indeterminate sweep's timing.
constexpr double PeriodMs   = 2000.0;
constexpr double Seg1EndMs  = 1500.0;
constexpr double Seg2HoldMs = 750.0;

float lerp_f(float a, float b, float t)
{
    return a + (b - a) * t;
}
} // namespace

ProgressBarControl::ProgressBarControl(double value, double min, double max) : min_(min), max_(std::max(min, max))
{
    value_ = std::clamp(value, min_, max_);
}

void ProgressBarControl::set_range(double min, double max)
{
    min_ = min;
    max_ = std::max(min, max);
    // The fraction can change even when value_ does not, so always repaint.
    mark_dirty(DirtyFlags::Visual);
    set_value(value_);
}

void ProgressBarControl::set_value(double value)
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

void ProgressBarControl::set_indeterminate(bool indeterminate)
{
    if (indeterminate == indeterminate_)
    {
        return;
    }
    indeterminate_ = indeterminate;
    if (indeterminate_)
    {
        phase_ms_ = 0.0; // always restart the sweep fresh on entering the state
    }
    mark_dirty(DirtyFlags::Visual);
}

void ProgressBarControl::set_show_paused(bool show_paused)
{
    if (show_paused == show_paused_)
    {
        return;
    }
    show_paused_ = show_paused;
    mark_dirty(DirtyFlags::Visual);
}

void ProgressBarControl::set_show_error(bool show_error)
{
    if (show_error == show_error_)
    {
        return;
    }
    show_error_ = show_error;
    mark_dirty(DirtyFlags::Visual);
}

float ProgressBarControl::value_fraction() const
{
    return max_ > min_ ? static_cast<float>(std::clamp((value_ - min_) / (max_ - min_), 0.0, 1.0)) : 0.0f;
}

Size ProgressBarControl::measure_override(Size available, const MeasureContext& context) const
{
    const auto& glossy = detail::controls(context.theme).glossy_progress_bar;
    const auto& framed = detail::controls(context.theme).framed_progress_bar;
    glossy_            = glossy.has_value();
    framed_            = !glossy_ && framed.has_value();
    Size desired{ProgressBarDefaultLength, glossy ? glossy->height : framed ? framed->height : Height};
    desired.width = std::min(desired.width, available.width);
    return desired;
}

bool ProgressBarControl::glinting() const
{
    return !show_error_ && !show_paused_ && (indeterminate_ || value_fraction() > 0.0f);
}

// The Aero bar, back to front: the track with its glassy top share, light
// inner edges and top highlight; the indicator in its color, the green wash, the glint, the dark
// ends, the warm light in its lower half and its own gloss; then the frame. Indeterminate, the
// indicator spans the track and keeps only the gloss and the glint, which takes the indicator's
// color.
void ProgressBarControl::render_glossy(Renderer& renderer, const Theme& theme) const
{
    const auto& bar   = *detail::controls(theme).glossy_progress_bar;
    const Rect  b     = bounds();
    const auto  split = [&](Color top)
    {
        return LinearGradientBrush{{GradientStop{0.0f, top},
                                    GradientStop{bar.gloss_split, top},
                                    GradientStop{bar.gloss_split, top.with_alpha(0.0f)},
                                    GradientStop{1.0f, top.with_alpha(0.0f)}},
                                   GradientAxis::Vertical};
    };

    renderer.fill_rounded_rect_gradient(b, bar.radius, LinearGradientBrush{bar.track, GradientAxis::Horizontal});
    const Rect in = b.inset(Thickness{1.0f});
    if (in.width <= 0.0f || in.height <= 0.0f)
    {
        return;
    }
    renderer.fill_rounded_rect_gradient(in, bar.radius, split(bar.track_gloss));
    renderer.fill_rounded_rect_gradient(in, 0.0f, LinearGradientBrush{bar.top_highlight, GradientAxis::Vertical});
    renderer.stroke_line(Point{in.left() + 0.5f, in.top()}, Point{in.left() + 0.5f, in.bottom()}, bar.inner_edge, 1.0f);
    renderer.stroke_line(Point{in.right() - 0.5f, in.top()}, Point{in.right() - 0.5f, in.bottom()}, bar.inner_edge, 1.0f);
    renderer.stroke_line(Point{in.left() + 1.0f, in.bottom() - 0.5f},
                         Point{in.right() - 1.0f, in.bottom() - 0.5f},
                         bar.inner_edge,
                         1.0f);

    const bool  stopped = show_error_ || show_paused_;
    const bool  sweep   = indeterminate_ && !stopped; // a stopped sweep shows a full bar
    const float filled  = indeterminate_ ? in.width : in.width * value_fraction();
    const Rect  ind{in.x, in.y, filled, in.height};
    if (filled > 0.0f)
    {
        if (!sweep)
        {
            const Color fill = show_error_    ? theme.colors().critical
                               : show_paused_ ? theme.colors().caution
                                              : bar.indicator;
            renderer.fill_rounded_rect(ind, 0.0f, fill);
            if (!stopped)
            {
                renderer.fill_rounded_rect(ind, 0.0f, bar.indicator_wash);
            }
        }
        if (glinting())
        {
            const double travel_ms = (filled + bar.glow_width * 2.0f) / bar.glow_speed * 1000.0;
            const double t         = std::fmod(glow_ms_, travel_ms + bar.glow_pause_ms);
            if (t < travel_ms)
            {
                const float x = -bar.glow_width + static_cast<float>(t / travel_ms) * (filled + bar.glow_width * 2.0f);
                renderer.push_clip(ind);
                renderer.fill_rounded_rect_gradient(Rect{ind.x + x, ind.y, bar.glow_width, ind.height},
                                                    0.0f,
                                                    LinearGradientBrush{sweep ? bar.sweep : bar.glow,
                                                                        GradientAxis::Horizontal});
                renderer.pop_clip();
            }
        }
        if (!sweep)
        {
            // Three columns, the ends star-sized and capped, the middle a tenth of a star; the
            // light sits in the lower of two equal rows.
            const float cap   = std::min(bar.cap_width, filled / 2.1f);
            const float lower = ind.y + ind.height * 0.5f;
            if (cap > 1.0f && ind.height > 2.0f)
            {
                renderer.fill_rounded_rect_gradient(Rect{ind.x + 1.0f, ind.y + 1.0f, cap - 1.0f, ind.height - 2.0f},
                                                    1.0f,
                                                    LinearGradientBrush{bar.left_cap, GradientAxis::Horizontal});
                renderer.fill_rounded_rect_gradient(Rect{ind.right() - cap, ind.y + 1.0f, cap - 1.0f, ind.height - 2.0f},
                                                    1.0f,
                                                    LinearGradientBrush{bar.right_cap, GradientAxis::Horizontal});
            }
            const auto corner_light = [&](const Rect& cell, Point center)
            {
                renderer.draw_geometry(
                    DrawGeometry{.geometry = PathGeometry::rectangle(cell),
                                 .fill     = RadialGradientBrush{.stops           = bar.light,
                                                                 .center          = center,
                                                                 .radius_x        = 1.0f,
                                                                 .radius_y        = 1.0f,
                                                                 .gradient_origin = center}});
            };
            corner_light(Rect{ind.x, lower, cap, ind.height * 0.5f}, Point{1.0f, 1.0f});
            corner_light(Rect{ind.right() - cap, lower, cap, ind.height * 0.5f}, Point{0.0f, 1.0f});
            std::vector<GradientStop> rising(bar.light.rbegin(), bar.light.rend());
            for (GradientStop& stop : rising)
            {
                stop.offset = 1.0f - stop.offset;
            }
            renderer.fill_rounded_rect_gradient(Rect{ind.x + cap, lower, std::max(0.0f, filled - cap * 2.0f), ind.height * 0.5f},
                                                0.0f,
                                                LinearGradientBrush{std::move(rising), GradientAxis::Vertical});
        }
        renderer.fill_rounded_rect_gradient(ind, 0.0f, split(bar.indicator_gloss));
        renderer.fill_rounded_rect_gradient(ind, 0.0f, LinearGradientBrush{bar.top_highlight, GradientAxis::Vertical});
    }

    renderer.draw_geometry(
        DrawGeometry{.geometry = PathGeometry::rectangle(b.inset(Thickness{0.5f}), bar.radius, bar.radius),
                     .stroke   = LinearGradientBrush{bar.border, GradientAxis::Vertical}});
}

// The classic bar: the well and its frame, then the indicator inside the padding. Indeterminate,
// a band sweeps across and wraps, in whole chunks when the bar is chunked.
void ProgressBarControl::render_framed(Renderer& renderer, const Theme& theme) const
{
    const auto& bar = *detail::controls(theme).framed_progress_bar;
    // Whole pixels, so the 1 px frame and the chunks stay crisp.
    const float left = std::round(bounds().left());
    const float top  = std::round(bounds().top());
    const Rect  b{left, top, std::round(bounds().right()) - left, std::round(bounds().bottom()) - top};
    if (b.width < 2.0f || b.height < 2.0f)
    {
        return;
    }
    renderer.fill_rounded_rect(b, 0.0f, detail::controls(theme).progress_track);
    renderer.fill_rounded_rect(Rect{b.x, b.y, b.width, 1.0f}, 0.0f, bar.top_left);
    renderer.fill_rounded_rect(Rect{b.x, b.y, 1.0f, b.height}, 0.0f, bar.top_left);
    renderer.fill_rounded_rect(Rect{b.x, b.bottom() - 1.0f, b.width, 1.0f}, 0.0f, bar.bottom_right);
    renderer.fill_rounded_rect(Rect{b.right() - 1.0f, b.y, 1.0f, b.height}, 0.0f, bar.bottom_right);

    const Rect in = b.inset(Thickness{1.0f + bar.padding});
    if (in.width <= 0.0f || in.height <= 0.0f)
    {
        return;
    }
    const Color fill  = show_error_    ? theme.colors().critical
                        : show_paused_ ? theme.colors().caution
                                       : detail::controls(theme).progress_fill.value_or(theme.colors().accent);
    const float chunk = std::max(1.0f, std::round(in.height * 2.0f / 3.0f));
    const float pitch = chunk + bar.chunk_gap;
    // The indicator from `from` to `to`, in chunks on the bar's pitch or as one block, clipped to
    // the inside of the well.
    const auto indicator = [&](float from, float to)
    {
        if (!bar.chunked)
        {
            const float x0 = std::max(in.left(), std::round(in.left() + from));
            const float x1 = std::min(in.right(), std::round(in.left() + to));
            if (x1 > x0)
            {
                renderer.fill_rounded_rect(Rect{x0, in.y, x1 - x0, in.height}, 0.0f, fill);
            }
            return;
        }
        for (float x = std::floor(from / pitch) * pitch; x < to; x += pitch)
        {
            const float x0 = std::max(in.left(), in.left() + x);
            const float x1 = std::min(in.right(), in.left() + x + chunk);
            if (x1 > x0)
            {
                renderer.fill_rounded_rect(Rect{x0, in.y, x1 - x0, in.height}, 0.0f, fill);
            }
        }
    };

    const bool stopped = show_error_ || show_paused_;
    if (!indeterminate_ || stopped)
    {
        // A stopped sweep shows a full bar. A started chunk is drawn whole, as PROGRESS_CLASS does.
        const float filled = indeterminate_ ? in.width : in.width * value_fraction();
        if (filled > 0.0f)
        {
            indicator(0.0f, bar.chunked ? std::ceil(filled / pitch) * pitch - bar.chunk_gap : filled);
        }
        return;
    }
    const float  band   = bar.chunked ? pitch * static_cast<float>(bar.sweep_chunks) : std::round(in.width * 0.25f);
    const double travel = in.width + band;
    const float  head   = static_cast<float>(std::fmod(phase_ms_, PeriodMs) / PeriodMs * travel);
    const float  start  = bar.chunked ? std::floor((head - band) / pitch) * pitch : head - band;
    indicator(start, start + band - (bar.chunked ? bar.chunk_gap : 0.0f));
}

void ProgressBarControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (detail::controls(theme).glossy_progress_bar)
    {
        render_glossy(renderer, theme);
        return;
    }
    if (detail::controls(theme).framed_progress_bar)
    {
        render_framed(renderer, theme);
        return;
    }

    // The error and paused colors, error outranking paused. Reads
    // Theme::severity, not theme.palette, because only Theme::fluent() fills the palette.
    const Color accent = show_error_    ? theme.colors().critical
                         : show_paused_ ? theme.colors().caution
                                        : detail::controls(theme).progress_fill.value_or(theme.colors().accent);

    if (indeterminate_)
    {
        // Paused or in error, the sweep freezes into one static bar.
        if (show_error_ || show_paused_)
        {
            renderer.fill_rounded_rect(bounds(), ProgressBarIndicatorRadius, accent);
            return;
        }

        if (detail::TreeAccess::layers_composed(*this))
        {
            return; // the compositor runs the sweep
        }

        const float w = bounds().width;
        const float h = bounds().height;

        const float w1 = w * 0.4f;
        const float w2 = w * 0.6f;

        const double t = std::fmod(phase_ms_, PeriodMs);

        const float start1 = -w1;
        const float end1   = w1 * 3.0f;
        const float x1     = t < Seg1EndMs ? lerp_f(start1, end1, static_cast<float>(t / Seg1EndMs)) : end1;

        const float start2 = w2 * -1.5f;
        const float end2   = w2 * 1.66f; // 1.66 exactly, not 5/3
        const float x2     = t < Seg2HoldMs
                                 ? start2
                                 : lerp_f(start2, end2, static_cast<float>((t - Seg2HoldMs) / (PeriodMs - Seg2HoldMs)));

        renderer.push_clip(bounds());
        renderer.fill_rounded_rect(Rect{bounds().left() + x1, bounds().top(), w1, h}, ProgressBarIndicatorRadius, accent);
        renderer.fill_rounded_rect(Rect{bounds().left() + x2, bounds().top(), w2, h}, ProgressBarIndicatorRadius, accent);
        renderer.pop_clip();
        return;
    }

    const Rect track{bounds().left(), bounds().top() + (bounds().height - TrackHeight) * 0.5f, bounds().width, TrackHeight};
    renderer.fill_rounded_rect(track, TrackRadius, detail::controls(theme).progress_track);

    const float indicator_width = bounds().width * value_fraction();
    if (indicator_width > 0.0f)
    {
        renderer.fill_rounded_rect(Rect{bounds().left(), bounds().top(), indicator_width, bounds().height},
                                   ProgressBarIndicatorRadius,
                                   accent);
    }
}

void ProgressBarControl::tick_override(Duration delta)
{
    if (glossy_)
    {
        if (glinting())
        {
            glow_ms_ += delta.count();
            mark_dirty(DirtyFlags::Visual);
        }
        return;
    }
    // Frozen while paused or in error. The framed bar's sweep is drawn here, never composed.
    if (!indeterminate_ || show_error_ || show_paused_ || (!framed_ && detail::TreeAccess::layers_composed(*this)))
    {
        return;
    }
    phase_ms_ = std::fmod(phase_ms_ + delta.count(), PeriodMs);
    mark_dirty(DirtyFlags::Visual);
}

std::size_t ProgressBarControl::layer_count() const
{
    return !glossy_ && !framed_ && indeterminate_ && !show_error_ && !show_paused_ ? 2 : 0;
}

detail::LayerPlacement ProgressBarControl::layer_placement(std::size_t index) const
{
    const float width = bounds().width * (index == 0 ? 0.4f : 0.6f);
    return detail::LayerPlacement{Rect{bounds().left(), bounds().top(), width, bounds().height}, bounds()};
}

void ProgressBarControl::render_layer(std::size_t index, Renderer& renderer, const Theme& theme) const
{
    const Color accent = detail::controls(theme).progress_fill.value_or(theme.colors().accent);
    renderer.fill_rounded_rect(layer_placement(index).bounds, ProgressBarIndicatorRadius, accent);
}

void ProgressBarControl::animate_layers(detail::Compositor& compositor, std::span<const VisualId> layers, Duration elapsed)
{
    // render_override()'s two bars as key frames: the first crosses in 1.5 s and waits out the
    // cycle past the end, the second waits 0.75 s before the start and crosses in the rest.
    const float  w1    = bounds().width * 0.4f;
    const float  w2    = bounds().width * 0.6f;
    const double phase = std::fmod(phase_ms_ + elapsed.count(), PeriodMs);
    compositor.animate_layer(layers[0],
                             LayerAnimation{.property   = LayerProperty::OffsetX,
                                            .key_frames = {KeyFrame::linear(Duration{0.0}, -w1),
                                                           KeyFrame::linear(Duration{Seg1EndMs}, w1 * 3.0f),
                                                           KeyFrame::linear(Duration{PeriodMs}, w1 * 3.0f)},
                                            .duration   = Duration{PeriodMs},
                                            .phase      = Duration{phase}});
    compositor.animate_layer(layers[1],
                             LayerAnimation{.property   = LayerProperty::OffsetX,
                                            .key_frames = {KeyFrame::linear(Duration{0.0}, w2 * -1.5f),
                                                           KeyFrame::linear(Duration{Seg2HoldMs}, w2 * -1.5f),
                                                           KeyFrame::linear(Duration{PeriodMs}, w2 * 1.66f)},
                                            .duration   = Duration{PeriodMs},
                                            .phase      = Duration{phase}});
}

void ProgressBarControl::layers_stopped(Duration elapsed)
{
    phase_ms_ = std::fmod(phase_ms_ + elapsed.count(), PeriodMs);
}

std::unique_ptr<ui::Element> ProgressBar::build() &&
{
    auto node = std::make_unique<ui::ProgressBarControl>(value.initial().value_or(min.initial()), min.initial(), max.initial());
    detail::apply_range(min, max, *node);

    apply(value,
          *node,
          [raw = node.get()](const std::optional<double>& v)
          {
              if (v)
              {
                  raw->set_value(*v);
              }
              raw->set_indeterminate(!v);
          });
    apply(show_paused, *node, [raw = node.get()](const bool& v) { raw->set_show_paused(v); });
    apply(show_error, *node, [raw = node.get()](const bool& v) { raw->set_show_error(v); });

    if (on_value_changed)
    {
        node->on_value_changed(std::move(on_value_changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
