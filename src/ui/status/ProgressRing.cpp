// SPDX-License-Identifier: MIT
#include "winui/ui/status/ProgressRing.hpp"

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
#include <numbers>
#include <optional>
#include <span>
#include <utility>

#include <cmath>

namespace ui
{

namespace
{
constexpr float DefaultDiameter = 32.0f; // the default width and height
constexpr float StrokeWidth     = 4.0f;  // the ring's stroke width

constexpr float ProgressRingPi = std::numbers::pi_v<float>;
constexpr float TwoPi          = 2.0f * ProgressRingPi;
constexpr float HalfPi         = ProgressRingPi * 0.5f;

// The indeterminate motion's numbers are picked by eye.
constexpr double SpinPeriodMs               = 1500.0;
constexpr float  IndeterminateSweepFraction = 0.75f;

// Each Bezier segment covers at most a quarter turn, where a cubic stays within 0.03% of the circle.
constexpr float MaxSegmentSweep = HalfPi;

// One path of cubic Beziers with round ends, so the arc has no joints to show.
void stroke_arc(Renderer& renderer, Point center, float radius, float start_angle, float sweep_angle, const Color& color)
{
    if (sweep_angle <= 0.0f || radius <= 0.0f)
    {
        return;
    }
    const bool  full     = sweep_angle >= TwoPi;
    const float sweep    = std::min(sweep_angle, TwoPi);
    const int   segments = std::max(1, static_cast<int>(std::ceil(sweep / MaxSegmentSweep - 1e-4f)));
    const float step     = sweep / static_cast<float>(segments);
    const float handle   = radius * (4.0f / 3.0f) * std::tan(step * 0.25f);

    auto point_at = [&](float angle)
    { return Point{center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)}; };

    PathFigure figure{.start = point_at(start_angle), .closed = full, .filled = false};
    for (int i = 0; i < segments; ++i)
    {
        const float a    = start_angle + step * static_cast<float>(i);
        const float b    = a + step;
        const Point from = point_at(a);
        const Point to   = point_at(b);
        figure.segments.push_back(PathSegment{.kind = PathSegmentKind::Cubic,
                                              .p1 = Point{from.x - handle * std::sin(a), from.y + handle * std::cos(a)},
                                              .p2 = Point{to.x + handle * std::sin(b), to.y - handle * std::cos(b)},
                                              .p3 = to});
    }

    DrawGeometry arc{.stroke       = Brush{color},
                     .stroke_style = StrokeStyle{.thickness = StrokeWidth,
                                                 .start_cap = PenLineCap::Round,
                                                 .end_cap   = PenLineCap::Round}};
    arc.geometry.figures.push_back(std::move(figure));
    renderer.draw_geometry(arc);
}
} // namespace

ProgressRingControl::ProgressRingControl(double value, double min, double max) : min_(min), max_(std::max(min, max))
{
    value_ = std::clamp(value, min_, max_);
    // The ring's minimum width and height.
    set_min_width(16.0f);
    set_min_height(16.0f);
}

void ProgressRingControl::set_range(double min, double max)
{
    min_ = min;
    max_ = std::max(min, max);
    mark_dirty(DirtyFlags::Visual);
    set_value(value_);
}

void ProgressRingControl::set_value(double value)
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

void ProgressRingControl::set_indeterminate(bool indeterminate)
{
    if (indeterminate == indeterminate_)
    {
        return;
    }
    indeterminate_ = indeterminate;
    if (indeterminate_)
    {
        phase_ms_ = 0.0;
    }
    mark_dirty(DirtyFlags::Visual);
}

void ProgressRingControl::set_active(bool active)
{
    if (active == active_)
    {
        return;
    }
    active_ = active;
    mark_dirty(DirtyFlags::Visual);
}

void ProgressRingControl::set_background(std::optional<Color> background)
{
    if (background == background_ && background_resource_ == nullptr)
    {
        return;
    }
    background_          = background;
    background_resource_ = nullptr;
    mark_dirty(DirtyFlags::Visual);
}

void ProgressRingControl::set_background(ThemeColor resource)
{
    if (resource == background_resource_ && !background_)
    {
        return;
    }
    background_          = std::nullopt;
    background_resource_ = resource;
    mark_dirty(DirtyFlags::Visual);
}

float ProgressRingControl::value_fraction() const
{
    return max_ > min_ ? static_cast<float>(std::clamp((value_ - min_) / (max_ - min_), 0.0, 1.0)) : 0.0f;
}

Size ProgressRingControl::measure_override(Size available, const MeasureContext&) const
{
    Size desired{DefaultDiameter, DefaultDiameter};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

Color ProgressRingControl::arc_color(const Theme& theme) const
{
    // progress_fill first: under Windows1x the accent button is an outline filled with the page's
    // own white, so reading it alone drew the ring white on white.
    return detail::controls(theme).progress_fill.value_or(theme.colors().accent);
}

void ProgressRingControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (!active_)
    {
        return;
    }

    const Color accent   = arc_color(theme);
    const Point center   = bounds().center();
    const float diameter = std::min(bounds().width, bounds().height);
    const float radius   = diameter * 0.5f - StrokeWidth * 0.5f;

    const std::optional<Color> track = background_resource_ != nullptr ? std::optional<Color>{background_resource_(theme)}
                                                                       : background_;
    if (track && track->a > 0.0f)
    {
        renderer.stroke_ellipse(center, radius, radius, *track, StrokeWidth);
    }

    if (indeterminate_)
    {
        if (detail::TreeAccess::layers_composed(*this))
        {
            return; // the compositor turns the arc
        }
        const double t           = std::fmod(phase_ms_, SpinPeriodMs);
        const float  start_angle = TwoPi * static_cast<float>(t / SpinPeriodMs) - HalfPi;
        stroke_arc(renderer, center, radius, start_angle, TwoPi * IndeterminateSweepFraction, accent);
        return;
    }

    stroke_arc(renderer, center, radius, -HalfPi, TwoPi * value_fraction(), accent);
}

void ProgressRingControl::tick_override(Duration delta)
{
    if (!active_ || !indeterminate_ || detail::TreeAccess::layers_composed(*this))
    {
        return;
    }
    phase_ms_ = std::fmod(phase_ms_ + delta.count(), SpinPeriodMs);
    mark_dirty(DirtyFlags::Visual);
}

std::size_t ProgressRingControl::layer_count() const
{
    return active_ && indeterminate_ ? 1 : 0;
}

detail::LayerPlacement ProgressRingControl::layer_placement(std::size_t /*index*/) const
{
    // The square the ring is drawn in, which a stretched ring's bounds are wider than.
    const float diameter = std::min(bounds().width, bounds().height);
    const Point center   = bounds().center();
    return detail::LayerPlacement{Rect{center.x - diameter * 0.5f, center.y - diameter * 0.5f, diameter, diameter},
                                  std::nullopt};
}

void ProgressRingControl::render_layer(std::size_t /*index*/, Renderer& renderer, const Theme& theme) const
{
    const float radius = std::min(bounds().width, bounds().height) * 0.5f - StrokeWidth * 0.5f;
    stroke_arc(renderer, bounds().center(), radius, -HalfPi, TwoPi * IndeterminateSweepFraction, arc_color(theme));
}

void ProgressRingControl::animate_layers(detail::Compositor& compositor, std::span<const VisualId> layers, Duration elapsed)
{
    // The arc drawn at the start of its turn, turned by as much as render_override() would start it
    // further along: the same arc at every moment.
    const Point center = bounds().center();
    const Rect  square = layer_placement(0).bounds;
    compositor.animate_layer(layers[0],
                             LayerAnimation{.property   = LayerProperty::Rotation,
                                            .key_frames = {KeyFrame::linear(Duration{0.0}, 0.0f),
                                                           KeyFrame::linear(Duration{SpinPeriodMs}, 360.0f)},
                                            .phase  = Duration{std::fmod(phase_ms_ + elapsed.count(), SpinPeriodMs)},
                                            .center = Point{center.x - square.x, center.y - square.y}});
}

void ProgressRingControl::layers_stopped(Duration elapsed)
{
    phase_ms_ = std::fmod(phase_ms_ + elapsed.count(), SpinPeriodMs);
}

std::unique_ptr<ui::Element> ProgressRing::build() &&
{
    auto node = std::make_unique<ui::ProgressRingControl>(value.initial().value_or(min.initial()), min.initial(), max.initial());
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
    apply(active, *node, [raw = node.get()](const bool& v) { raw->set_active(v); });
    apply(background,
          *node,
          [raw = node.get()](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });

    if (on_value_changed)
    {
        node->on_value_changed(std::move(on_value_changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
