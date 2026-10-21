// SPDX-License-Identifier: MIT
#include "PaintTracker.hpp"

#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/SvgImage.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <variant>

#include <cmath>

namespace ui::detail
{

namespace
{
thread_local PaintTracker* tracker_on_thread = nullptr;

// Anti-aliasing and device-pixel snapping reach this far past a shape's own edge.
constexpr float PaintSlack = 1.0f;

constexpr std::uint64_t HashBasis = 0xcbf29ce484222325ull;
constexpr std::uint64_t HashPrime = 0x100000001b3ull;

Rect segment_box(Point a, Point b)
{
    const float left = (std::min)(a.x, b.x);
    const float top  = (std::min)(a.y, b.y);
    return {left, top, std::abs(a.x - b.x), std::abs(a.y - b.y)};
}

enum class Command : std::uint64_t
{
    FillRoundedRect = 1,
    StrokeRoundedRect,
    DrawText,
    FillGradient,
    StrokeGradient,
    StrokeLine,
    StrokeWavyLine,
    FillPolygon,
    DrawGeometry,
    PushClip,
    PopClip,
    PushClipEllipse,
    PushClipRoundedRect,
    FillEllipse,
    StrokeEllipse,
    FillPattern,
    NineSlice,
    DrawImage,
    DrawImageRect,
    DrawSvg,
    PushOpacity,
    PopOpacity,
    PushMatrix,
    PopTransform,
    StrokePolyline,
};
} // namespace

Rect united(const Rect& a, const Rect& b)
{
    if (a.width <= 0.0f || a.height <= 0.0f)
    {
        return b;
    }
    if (b.width <= 0.0f || b.height <= 0.0f)
    {
        return a;
    }
    const float left   = (std::min)(a.left(), b.left());
    const float top    = (std::min)(a.top(), b.top());
    const float right  = (std::max)(a.right(), b.right());
    const float bottom = (std::max)(a.bottom(), b.bottom());
    return {left, top, right - left, bottom - top};
}

PaintTracker::PaintTracker(std::uint32_t frame, PaintScratch& scratch) :
    frame_(frame),
    s_(&scratch),
    outer_(tracker_on_thread)
{
    s_->stack.clear();
    s_->matrices.clear();
    s_->clips.clear();
    s_->opacities.clear();
    s_->acrylic.clear();
    s_->damage.clear();
    s_->layers.clear();
    s_->visuals.clear();
    s_->screen.clear();
    s_->stack.push_back({.own = HashBasis, .children = HashBasis});
    s_->matrices.emplace_back();
    s_->clips.emplace_back();
    s_->opacities.push_back(1.0f);
    tracker_on_thread = this;
}

PaintTracker::PaintTracker(Renderer& inner, std::uint32_t frame, const Rect& clip) :
    inner_(&inner),
    frame_(frame),
    clip_(clip),
    outer_(tracker_on_thread)
{
    tracker_on_thread = this;
}

PaintTracker::~PaintTracker()
{
    tracker_on_thread = outer_;
}

PaintTracker* PaintTracker::active(const Renderer& renderer) noexcept
{
    return tracker_on_thread == &renderer ? tracker_on_thread : nullptr;
}

PaintTracker* paint_tracker(const Renderer& renderer) noexcept
{
    return PaintTracker::active(renderer);
}

void PaintTracker::hash_bytes(const void* data, std::size_t size)
{
    std::uint64_t& h     = s_->stack.back().own;
    const auto*    bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i)
    {
        h = (h ^ bytes[i]) * HashPrime;
    }
}

void PaintTracker::hash(std::uint64_t value)
{
    hash_value(value);
}

void PaintTracker::cover(const Rect& local)
{
    Rect window = s_->matrices.back().is_identity() ? local : s_->matrices.back().transform_bounds(local);
    if (covering_ && (!s_->layers.empty() || !s_->visuals.empty()))
    {
        // Drawn after a layered element and over its layers: they would show on top of it.
        Rect drawn = window;
        if (const std::optional<Rect>& clip = s_->clips.back())
        {
            drawn = intersection(drawn, *clip);
        }
        for (std::vector<LayerProbe>* probes : {&s_->layers, &s_->visuals})
        {
            for (LayerProbe& probe : *probes)
            {
                if (!probe.open && !probe.occluded && drawn.intersects(probe.area))
                {
                    probe.occluded = true;
                }
            }
        }
    }
    window = window.inflate(PaintSlack);
    if (const std::optional<Rect>& clip = s_->clips.back())
    {
        window = intersection(window, *clip);
    }
    PaintLevel& level = s_->stack.back();
    level.own_area    = united(level.own_area, window);
    level.area        = united(level.area, window);
}

void PaintTracker::begin_element(const Element& e)
{
    if (culling())
    {
        return;
    }
    // Which child, in which order, is the parent's business.
    const VisualId id = e.visual_id();
    {
        std::uint64_t& h     = s_->stack.back().children;
        const auto*    bytes = reinterpret_cast<const unsigned char*>(&id);
        for (std::size_t i = 0; i < sizeof(id); ++i)
        {
            h = (h ^ bytes[i]) * HashPrime;
        }
    }
    s_->stack.push_back({.own = HashBasis, .children = HashBasis});
    // What the element draws looks different under another transform, clip, opacity or
    // rasterization even when its commands are the same.
    const Matrix& m = s_->matrices.back();
    for (const float v : {m.m11, m.m12, m.m21, m.m22, m.dx, m.dy, s_->opacities.back()})
    {
        hash_value(v);
    }
    if (const std::optional<Rect>& clip = s_->clips.back())
    {
        hash_value(*clip);
    }
    hash(rasterization_);
}

void PaintTracker::end_element(const Element& e)
{
    if (culling())
    {
        return;
    }
    for (std::vector<LayerProbe>* probes : {&s_->layers, &s_->visuals})
    {
        for (auto it = probes->rbegin(); it != probes->rend(); ++it)
        {
            if (it->open && it->element == &e)
            {
                it->open = false;
                break;
            }
        }
    }
    const PaintLevel level = s_->stack.back();
    s_->stack.pop_back();
    s_->stack.back().area = united(s_->stack.back().area, level.area);

    if (e.painted_frame_ == frame_)
    {
        // Drawn twice in one frame: compared once, and culled by where it drew both times.
        e.painted_own_  = united(e.painted_own_, level.own_area);
        e.painted_area_ = united(e.painted_area_, level.area);
        return;
    }
    if (e.painted_frame_ == 0 || e.painted_frame_ + 1 != frame_)
    {
        s_->damage.push_back(level.area); // not drawn in the last frame: all of it is new
    }
    else
    {
        if (level.own != e.painted_hash_)
        {
            s_->damage.push_back(united(e.painted_own_, level.own_area));
        }
        if (level.children != e.painted_children_hash_)
        {
            s_->damage.push_back(united(e.painted_area_, level.area));
        }
    }
    if (e.tracks_screen_)
    {
        s_->screen.push_back(&e);
    }
    e.painted_hash_          = level.own;
    e.painted_children_hash_ = level.children;
    e.painted_own_           = level.own_area;
    e.painted_area_          = level.area;
    e.painted_frame_         = frame_;
}

void PaintTracker::probe_layers(const Element& e, const Theme& theme, const Matrix& own, float own_opacity, std::size_t count)
{
    if (culling())
    {
        return;
    }
    LayerProbe& probe = s_->layers.emplace_back();
    probe.element     = &e;
    probe.transform   = own * s_->matrices.back();
    probe.clip        = s_->clips.back();
    probe.opacity     = s_->opacities.back() * own_opacity;
    for (std::size_t i = 0; i < count; ++i)
    {
        ProbedLayer& layer = probe.layers.emplace_back();
        layer.placement    = e.layer_placement(i);
        layer.content      = std::make_unique<RenderRecording>();
        layer.content->set_rasterization(theme.rasterization);
        layer.content->push_transform(1.0f, Point{-layer.placement.bounds.x, -layer.placement.bounds.y});
        e.render_layer(i, *layer.content, theme);
        layer.content->pop_transform();
        {
            // The content's hash, from a measuring tracker of its own.
            PaintScratch scratch;
            PaintTracker hasher{0, scratch};
            layer.content->replay(hasher);
            layer.hash = hasher.window_hash();
        }
        Rect area = layer.placement.clip.value_or(layer.placement.bounds);
        area      = probe.transform.is_identity() ? area : probe.transform.transform_bounds(area);
        if (probe.clip)
        {
            area = intersection(area, *probe.clip);
        }
        probe.area = united(probe.area, area);
    }
}

void PaintTracker::probe_visuals(const Element& e, const Matrix& own, float own_opacity)
{
    if (culling())
    {
        return;
    }
    LayerProbe& probe = s_->visuals.emplace_back();
    probe.element     = &e;
    probe.transform   = own * s_->matrices.back();
    probe.clip        = s_->clips.back();
    probe.opacity     = s_->opacities.back() * own_opacity;
    // Unclipped, so Window can tell a clip that cuts the visuals from one that leaves them whole.
    probe.area = probe.transform.is_identity() ? e.bounds() : probe.transform.transform_bounds(e.bounds());
}

ComposedLayers::ComposedLayers()                                     = default;
ComposedLayers::~ComposedLayers()                                    = default;
ComposedLayers::ComposedLayers(ComposedLayers&&) noexcept            = default;
ComposedLayers& ComposedLayers::operator=(ComposedLayers&&) noexcept = default;

// ---- Renderer: hash and cover when measuring, hand on when culling ----

void PaintTracker::fill_rounded_rect(const Rect& bounds, float radius, const Color& color)
{
    if (culling())
    {
        inner_->fill_rounded_rect(bounds, radius, color);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::FillRoundedRect));
    hash_value(bounds);
    hash_value(radius);
    hash_value(color);
    cover(bounds);
}

void PaintTracker::stroke_rounded_rect(const Rect& bounds, float radius, const Color& color, float stroke_width)
{
    if (culling())
    {
        inner_->stroke_rounded_rect(bounds, radius, color, stroke_width);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::StrokeRoundedRect));
    hash_value(bounds);
    hash_value(radius);
    hash_value(color);
    hash_value(stroke_width);
    cover(bounds.inflate(stroke_width * 0.5f));
}

void PaintTracker::draw_text(std::string_view text, const Rect& bounds, const Color& color, const TextStyle& style)
{
    if (culling())
    {
        inner_->draw_text(text, bounds, color, style);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::DrawText));
    hash_bytes(text.data(), text.size());
    hash(text.size());
    hash_value(bounds);
    hash_value(color);
    hash_value(style.font_size);
    hash_value(style.line_height);
    hash_bytes(style.font_family.data(), style.font_family.size());
    hash(static_cast<std::uint64_t>(style.weight) | (static_cast<std::uint64_t>(style.horizontal) << 16) |
         (static_cast<std::uint64_t>(style.vertical) << 20) | (static_cast<std::uint64_t>(style.wrapping) << 24) |
         (static_cast<std::uint64_t>(style.italic) << 28) | (static_cast<std::uint64_t>(style.underline) << 29) |
         (static_cast<std::uint64_t>(style.strikethrough) << 30) | (static_cast<std::uint64_t>(style.effect.kind) << 32));
    hash(static_cast<std::uint64_t>(static_cast<std::int64_t>(style.character_spacing)));
    hash_value(style.effect.highlight);
    hash_value(style.effect.shadow);
    hash_value(style.effect.offset_x);
    hash_value(style.effect.offset_y);
    // An embossed run's raised pass is offset; a wrapping run gets half a DIP of slack.
    cover(bounds.inflate(1.0f + std::abs(style.effect.offset_x) + std::abs(style.effect.offset_y)));
}

void PaintTracker::fill_rounded_rect_gradient(const Rect& bounds, float radius, const LinearGradientBrush& brush)
{
    if (culling())
    {
        inner_->fill_rounded_rect_gradient(bounds, radius, brush);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::FillGradient));
    hash_value(bounds);
    hash_value(radius);
    hash(static_cast<std::uint64_t>(brush.axis));
    for (const GradientStop& stop : brush.stops)
    {
        hash_value(stop.offset);
        hash_value(stop.color);
    }
    cover(bounds);
}

void PaintTracker::stroke_rounded_rect_gradient(
    const Rect&                bounds,
    float                      radius,
    const LinearGradientBrush& brush,
    float                      extent_px,
    float                      stroke_width)
{
    if (culling())
    {
        inner_->stroke_rounded_rect_gradient(bounds, radius, brush, extent_px, stroke_width);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::StrokeGradient));
    hash_value(bounds);
    hash_value(radius);
    hash_value(extent_px);
    hash_value(stroke_width);
    hash(static_cast<std::uint64_t>(brush.axis));
    for (const GradientStop& stop : brush.stops)
    {
        hash_value(stop.offset);
        hash_value(stop.color);
    }
    cover(bounds.inflate(stroke_width * 0.5f));
}

void PaintTracker::stroke_line(Point from, Point to, const Color& color, float stroke_width)
{
    if (culling())
    {
        inner_->stroke_line(from, to, color, stroke_width);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::StrokeLine));
    hash_value(from);
    hash_value(to);
    hash_value(color);
    hash_value(stroke_width);
    cover(segment_box(from, to).inflate(stroke_width * 0.5f));
}

void PaintTracker::stroke_wavy_line(Point from, Point to, const Color& color, float stroke_width, float amplitude, float wavelength)
{
    if (culling())
    {
        inner_->stroke_wavy_line(from, to, color, stroke_width, amplitude, wavelength);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::StrokeWavyLine));
    hash_value(from);
    hash_value(to);
    hash_value(color);
    hash_value(stroke_width);
    hash_value(amplitude);
    hash_value(wavelength);
    cover(segment_box(from, to).inflate(std::abs(amplitude) * 2.0f + stroke_width));
}

void PaintTracker::fill_polygon(const FillPolygon& polygon)
{
    if (culling())
    {
        inner_->fill_polygon(polygon);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::FillPolygon));
    hash_value(polygon.fill);
    cover_points(polygon.points, 0.0f);
}

void PaintTracker::stroke_polyline(const StrokePolyline& polyline)
{
    if (culling())
    {
        inner_->stroke_polyline(polyline);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::StrokePolyline));
    hash_value(polyline.color);
    hash_value(polyline.stroke_width);
    hash(polyline.closed ? 1u : 0u);
    cover_points(polyline.points, polyline.stroke_width * 0.5f);
}

void PaintTracker::cover_points(const std::vector<Point>& points, float outset)
{
    if (points.empty())
    {
        return;
    }
    Rect box{points.front().x, points.front().y, 0.0f, 0.0f};
    for (const Point& p : points)
    {
        hash_value(p);
        const float right  = (std::max)(box.right(), p.x);
        const float bottom = (std::max)(box.bottom(), p.y);
        box.x              = (std::min)(box.x, p.x);
        box.y              = (std::min)(box.y, p.y);
        box.width          = right - box.x;
        box.height         = bottom - box.y;
    }
    cover(box.inflate(outset));
}

void PaintTracker::draw_geometry(const DrawGeometry& geometry)
{
    if (culling())
    {
        inner_->draw_geometry(geometry);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::DrawGeometry));
    hash(static_cast<std::uint64_t>(geometry.geometry.fill_rule));
    for (const PathFigure& figure : geometry.geometry.figures)
    {
        hash_value(figure.start);
        hash((figure.closed ? 1u : 0u) | (figure.filled ? 2u : 0u));
        for (const PathSegment& segment : figure.segments)
        {
            hash(static_cast<std::uint64_t>(segment.kind));
            hash_value(segment.p1);
            hash_value(segment.p2);
            hash_value(segment.p3);
        }
    }
    const auto hash_stops = [this](const GradientStops& stops)
    {
        for (const GradientStop& stop : stops)
        {
            hash_value(stop.offset);
            hash_value(stop.color);
        }
    };
    const auto hash_brush = [this, &hash_stops](const std::optional<Brush>& brush)
    {
        if (!brush)
        {
            hash(0);
            return;
        }
        hash(brush->index() + 1);
        if (const auto* color = std::get_if<Color>(&*brush))
        {
            hash_value(*color);
        }
        else if (const auto* linear = std::get_if<LinearGradientBrush>(&*brush))
        {
            hash(static_cast<std::uint64_t>(linear->axis));
            hash_stops(linear->stops);
        }
        else if (const auto* radial = std::get_if<RadialGradientBrush>(&*brush))
        {
            for (const float v :
                 {radial->center.x,
                  radial->center.y,
                  radial->radius_x,
                  radial->radius_y,
                  radial->gradient_origin.x,
                  radial->gradient_origin.y})
            {
                hash_value(v);
            }
            hash(static_cast<std::uint64_t>(radial->mapping_mode) |
                 (static_cast<std::uint64_t>(radial->spread_method) << 8));
            hash_stops(radial->stops);
        }
        else if (const auto* acrylic = std::get_if<AcrylicBrush>(&*brush))
        {
            hash_value(acrylic->tint_color);
            hash_value(acrylic->tint_opacity);
            hash_value(acrylic->tint_luminosity_opacity.value_or(-1.0f));
            hash_value(acrylic->fallback_color);
            hash(acrylic->always_use_fallback ? 1u : 0u);
        }
    };
    hash_brush(geometry.fill);
    hash_brush(geometry.stroke);
    const StrokeStyle& s = geometry.stroke_style;
    for (const float v : {s.thickness, s.miter_limit, s.dash_offset})
    {
        hash_value(v);
    }
    hash(static_cast<std::uint64_t>(s.start_cap) | (static_cast<std::uint64_t>(s.end_cap) << 8) |
         (static_cast<std::uint64_t>(s.dash_cap) << 16) | (static_cast<std::uint64_t>(s.line_join) << 24));
    for (const float dash : s.dash_array)
    {
        hash_value(dash);
    }
    hash_value(geometry.brush_box);
    if (geometry.fill && std::holds_alternative<AcrylicBrush>(*geometry.fill) && !geometry.geometry.empty())
    {
        // The blur reads three standard deviations (30 DIPs each) around the shape.
        constexpr float BlurReach = 90.0f;
        const Rect      local     = geometry.geometry.bounds().inflate(BlurReach);
        s_->acrylic.push_back(s_->matrices.back().is_identity() ? local : s_->matrices.back().transform_bounds(local));
    }
    if (!geometry.geometry.empty())
    {
        // A miter can reach well past the outline; half the miter limit's length covers it.
        const float stroke = geometry.stroke ? std::abs(s.thickness) : 0.0f;
        cover(geometry.geometry.bounds().inflate(stroke * (std::max)(1.0f, s.miter_limit) * 0.5f));
    }
}

void PaintTracker::push_clip(const Rect& bounds)
{
    if (culling())
    {
        inner_->push_clip(bounds);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::PushClip));
    hash_value(bounds);
    Rect window = s_->matrices.back().is_identity() ? bounds : s_->matrices.back().transform_bounds(bounds);
    if (const std::optional<Rect>& outer = s_->clips.back())
    {
        window = intersection(window, *outer);
    }
    s_->clips.emplace_back(window);
}

void PaintTracker::pop_clip()
{
    if (culling())
    {
        inner_->pop_clip();
        return;
    }
    hash(static_cast<std::uint64_t>(Command::PopClip));
    if (s_->clips.size() > 1)
    {
        s_->clips.pop_back();
    }
}

void PaintTracker::push_clip_ellipse(Point center, float radius_x, float radius_y)
{
    if (culling())
    {
        inner_->push_clip_ellipse(center, radius_x, radius_y);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::PushClipEllipse));
    hash_value(center);
    hash_value(radius_x);
    hash_value(radius_y);
    const Rect box{center.x - radius_x, center.y - radius_y, radius_x * 2.0f, radius_y * 2.0f};
    Rect       window = s_->matrices.back().is_identity() ? box : s_->matrices.back().transform_bounds(box);
    if (const std::optional<Rect>& outer = s_->clips.back())
    {
        window = intersection(window, *outer);
    }
    s_->clips.emplace_back(window);
}

void PaintTracker::push_clip_rounded_rect(const Rect& bounds, float radius)
{
    if (culling())
    {
        inner_->push_clip_rounded_rect(bounds, radius);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::PushClipRoundedRect));
    hash_value(bounds);
    hash_value(radius);
    // The corners cut away nothing that matters to which pixels can change: the box is the reach.
    Rect window = s_->matrices.back().is_identity() ? bounds : s_->matrices.back().transform_bounds(bounds);
    if (const std::optional<Rect>& outer = s_->clips.back())
    {
        window = intersection(window, *outer);
    }
    s_->clips.emplace_back(window);
}

void PaintTracker::fill_ellipse(Point center, float radius_x, float radius_y, const Color& color)
{
    if (culling())
    {
        inner_->fill_ellipse(center, radius_x, radius_y, color);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::FillEllipse));
    hash_value(center);
    hash_value(radius_x);
    hash_value(radius_y);
    hash_value(color);
    cover(Rect{center.x - radius_x, center.y - radius_y, radius_x * 2.0f, radius_y * 2.0f});
}

void PaintTracker::stroke_ellipse(Point center, float radius_x, float radius_y, const Color& color, float stroke_width)
{
    if (culling())
    {
        inner_->stroke_ellipse(center, radius_x, radius_y, color, stroke_width);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::StrokeEllipse));
    hash_value(center);
    hash_value(radius_x);
    hash_value(radius_y);
    hash_value(color);
    hash_value(stroke_width);
    cover(Rect{center.x - radius_x, center.y - radius_y, radius_x * 2.0f, radius_y * 2.0f}.inflate(stroke_width * 0.5f));
}

void PaintTracker::fill_rect_pattern(const Rect& bounds, Pattern pattern, const Color& fg, const Color& bg)
{
    if (culling())
    {
        inner_->fill_rect_pattern(bounds, pattern, fg, bg);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::FillPattern));
    hash_value(bounds);
    hash(static_cast<std::uint64_t>(pattern));
    hash_value(fg);
    hash_value(bg);
    cover(bounds);
}

void PaintTracker::draw_image_nine_slice(const std::string& image_name, const Rect& bounds, const Thickness& slice_margins)
{
    if (culling())
    {
        inner_->draw_image_nine_slice(image_name, bounds, slice_margins);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::NineSlice));
    hash_bytes(image_name.data(), image_name.size());
    hash_value(bounds);
    hash_value(slice_margins);
    cover(bounds);
}

void PaintTracker::draw_image(const Bitmap& image, const Rect& bounds, Stretch stretch)
{
    if (culling())
    {
        inner_->draw_image(image, bounds, stretch);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::DrawImage));
    hash(image.content_id());
    hash_value(bounds);
    hash(static_cast<std::uint64_t>(stretch));
    cover(bounds);
}

void PaintTracker::draw_image_rect(const Bitmap& image, const Rect& source, const Rect& bounds)
{
    if (culling())
    {
        inner_->draw_image_rect(image, source, bounds);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::DrawImageRect));
    hash(image.content_id());
    hash_value(source);
    hash_value(bounds);
    cover(bounds);
}

void PaintTracker::draw_svg(const SvgImage& svg, const Rect& bounds)
{
    if (culling())
    {
        inner_->draw_svg(svg, bounds);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::DrawSvg));
    const std::string_view markup = svg.markup();
    hash_bytes(markup.data(), markup.size());
    hash_value(bounds);
    cover(bounds);
}

void PaintTracker::push_opacity_layer(float opacity)
{
    if (culling())
    {
        inner_->push_opacity_layer(opacity);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::PushOpacity));
    hash_value(opacity);
    s_->opacities.push_back(s_->opacities.back() * opacity);
}

void PaintTracker::pop_opacity_layer()
{
    if (culling())
    {
        inner_->pop_opacity_layer();
        return;
    }
    hash(static_cast<std::uint64_t>(Command::PopOpacity));
    if (s_->opacities.size() > 1)
    {
        s_->opacities.pop_back();
    }
}

void PaintTracker::push_matrix(const Matrix& transform)
{
    if (culling())
    {
        inner_->push_matrix(transform);
        return;
    }
    hash(static_cast<std::uint64_t>(Command::PushMatrix));
    hash_value(transform);
    s_->matrices.push_back(transform * s_->matrices.back());
}

void PaintTracker::pop_transform()
{
    if (culling())
    {
        inner_->pop_transform();
        return;
    }
    hash(static_cast<std::uint64_t>(Command::PopTransform));
    if (s_->matrices.size() > 1)
    {
        s_->matrices.pop_back();
    }
}

void PaintTracker::set_rasterization(const SetRasterization& policy)
{
    if (culling())
    {
        inner_->set_rasterization(policy);
        return;
    }
    rasterization_ = static_cast<std::uint64_t>(policy.geometry) | (static_cast<std::uint64_t>(policy.text) << 8) |
                     (static_cast<std::uint64_t>(policy.snap_to_device_pixels) << 16);
    hash(rasterization_);
}

float PaintTracker::device_scale() const
{
    return inner_ != nullptr ? inner_->device_scale() : 1.0f;
}

} // namespace ui::detail
