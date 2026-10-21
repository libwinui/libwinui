// SPDX-License-Identifier: MIT
#include "winui/ui/media/Shape.hpp"

#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
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

namespace ui
{

namespace
{

constexpr float ShapeEpsilon = 1.192092896e-07f; // REAL_EPSILON

// The stretch matrix, centered on the free axis of an aspect-preserving stretch.
Matrix stretch_matrix(Stretch stretch, const Rect& render, const Rect& natural)
{
    const float natural_w = natural.width > 0.0f ? natural.width : 1.0f;
    const float natural_h = natural.height > 0.0f ? natural.height : 1.0f;
    const float render_w  = render.width;
    const float render_h  = render.height;
    if (render_w <= 0.0f || render_h <= 0.0f)
    {
        return Matrix{};
    }
    float       scale_x = render_w / natural_w;
    float       scale_y = render_h / natural_h;
    const float aspect  = scale_x / scale_y;
    float       delta_x = 0.0f;
    float       delta_y = 0.0f;
    switch (stretch)
    {
        case Stretch::Fill:
        case Stretch::None:
            break;
        case Stretch::Uniform:
            if (aspect > 1.0f)
            {
                scale_x = scale_y;
                delta_x = render_w - natural_w * scale_x;
            }
            else
            {
                scale_y = scale_x;
                delta_y = render_h - natural_h * scale_y;
            }
            break;
        case Stretch::UniformToFill:
            if (aspect > 1.0f)
            {
                scale_y = scale_x;
            }
            else
            {
                scale_x = scale_y;
            }
            break;
    }
    return Matrix::translation(-natural.x, -natural.y) * Matrix::scale(scale_x, scale_y) *
           Matrix::translation(render.x, render.y) * Matrix::translation(delta_x * 0.5f, delta_y * 0.5f);
}

// A shape's content stretch. Infinity marks an unbounded axis of `constraint`.
Size content_stretch(float stroke, Stretch stretch, const Rect& natural_bounds, Size constraint)
{
    if (stretch == Stretch::None)
    {
        return Size{natural_bounds.right() + stroke / 2.0f, natural_bounds.bottom() + stroke / 2.0f};
    }
    const float natural_w = std::max(natural_bounds.width, 0.0f);
    const float natural_h = std::max(natural_bounds.height, 0.0f);
    const bool  x_bounded = std::isfinite(constraint.width);
    const bool  y_bounded = std::isfinite(constraint.height);
    float       scale_x   = 1.0f;
    float       scale_y   = 1.0f;
    if (x_bounded && natural_w > ShapeEpsilon)
    {
        scale_x = std::max(constraint.width - stroke, 0.0f) / natural_w;
    }
    if (y_bounded && natural_h > ShapeEpsilon)
    {
        scale_y = std::max(constraint.height - stroke, 0.0f) / natural_h;
    }
    if (x_bounded && y_bounded)
    {
        if (stretch == Stretch::Uniform)
        {
            scale_x = scale_y = std::min(scale_x, scale_y);
        }
        else if (stretch == Stretch::UniformToFill)
        {
            scale_x = scale_y = std::max(scale_x, scale_y);
        }
    }
    else if (stretch == Stretch::Uniform || stretch == Stretch::UniformToFill)
    {
        if (x_bounded)
        {
            scale_y = scale_x;
        }
        else if (y_bounded)
        {
            scale_x = scale_y;
        }
    }
    return Size{natural_w * scale_x + stroke, natural_h * scale_y + stroke};
}

// Rectangle/Ellipse under UniformToFill ask for a square of the larger bounded side
// (the rectangle's measure); every other stretch asks for nothing but an explicit size.
Size uniform_to_fill_square(Size available)
{
    const bool w_bounded = std::isfinite(available.width);
    const bool h_bounded = std::isfinite(available.height);
    if (!w_bounded && !h_bounded)
    {
        return Size{};
    }
    float side = 0.0f;
    if (!w_bounded || !h_bounded)
    {
        side = w_bounded ? available.width : available.height;
    }
    else
    {
        side = std::max(available.width, available.height);
    }
    return Size{side, side};
}

} // namespace

Rect Shape::outline_rect(Size size, bool check_degenerate) const
{
    const float thickness = std::abs(style_.thickness);
    const bool degenerate = check_degenerate && !stroke_.empty() && (thickness >= size.width || thickness >= size.height);
    const float stroke = !stroke_.empty() && !degenerate ? thickness : 0.0f;

    Rect r{stroke * 0.5f, stroke * 0.5f, std::max(0.0f, size.width - stroke), std::max(0.0f, size.height - stroke)};
    switch (stretch_)
    {
        case Stretch::Fill:
            break;
        case Stretch::Uniform:
            if (r.width > r.height)
            {
                r.width = r.height;
            }
            else
            {
                r.height = r.width;
            }
            break;
        case Stretch::UniformToFill:
            if (r.width < r.height)
            {
                r.width = r.height;
            }
            else
            {
                r.height = r.width;
            }
            break;
        case Stretch::None:
            r.width = r.height = 0.0f;
            break;
    }
    return r;
}

PathGeometry Shape::rendered_geometry() const
{
    const Matrix to_window = Matrix::translation(bounds().x, bounds().y);
    if (!stretches_geometry())
    {
        return outline_geometry(bounds().size()).transformed(to_window);
    }
    PathGeometry natural = natural_geometry();
    if (natural.empty())
    {
        return natural;
    }
    if (stretch_ == Stretch::None)
    {
        return natural.transformed(to_window);
    }
    // The stretch transform: the layout rect less the stroke, never of zero area.
    const float stroke = effective_stroke_thickness();
    Rect clip{stroke / 2.0f, stroke / 2.0f, std::max(bounds().width - stroke, 0.0f), std::max(bounds().height - stroke, 0.0f)};
    if (clip.width <= ShapeEpsilon || clip.height <= ShapeEpsilon)
    {
        clip.width  = clip.width > 0.0f ? clip.width : stroke;
        clip.height = clip.height > 0.0f ? clip.height : stroke;
    }
    return natural.transformed(stretch_matrix(stretch_, clip, natural.bounds()) * to_window);
}

Size Shape::measure_override(Size available, const MeasureContext&) const
{
    // A shape's measure: the stretched bounds' bottom-right.
    const PathGeometry natural = natural_geometry();
    if (natural.empty())
    {
        return Size{};
    }
    const Size size = content_stretch(effective_stroke_thickness(), stretch_, natural.bounds(), available);
    return Size{std::max(0.0f, size.width), std::max(0.0f, size.height)};
}

void Shape::render_override(Renderer& renderer, const RenderContext& context) const
{
    // A laid-out shape of zero area draws nothing.
    if ((fill_.empty() && stroke_.empty()) || bounds().width <= ShapeEpsilon || bounds().height <= ShapeEpsilon)
    {
        return;
    }
    PathGeometry geometry = rendered_geometry();
    if (geometry.empty())
    {
        return;
    }
    DrawGeometry cmd;
    cmd.geometry               = std::move(geometry);
    cmd.fill                   = fill_.resolve_brush(context.theme);
    cmd.stroke                 = stroke_.resolve_brush(context.theme);
    cmd.stroke_style           = style_;
    cmd.stroke_style.thickness = std::abs(style_.thickness);
    cmd.brush_box              = bounds();
    renderer.draw_geometry(cmd);
}

Size RectangleControl::measure_override(Size available, const MeasureContext&) const
{
    return stretch() == Stretch::UniformToFill ? uniform_to_fill_square(available) : Size{};
}

PathGeometry RectangleControl::outline_geometry(Size size) const
{
    return PathGeometry::rectangle(outline_rect(size, true), radius_x_, radius_y_);
}

Size EllipseControl::measure_override(Size available, const MeasureContext&) const
{
    return stretch() == Stretch::UniformToFill ? uniform_to_fill_square(available) : Size{};
}

PathGeometry EllipseControl::outline_geometry(Size size) const
{
    // An ellipse takes the outline rect without the degenerate-stroke check.
    const Rect  o  = outline_rect(size, false);
    const float rx = o.width * 0.5f;
    const float ry = o.height * 0.5f;
    return PathGeometry::ellipse(Point{o.x + rx, o.y + ry}, rx, ry);
}

void LineControl::set_x1(float v)
{
    from_.x = v;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}
void LineControl::set_y1(float v)
{
    from_.y = v;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}
void LineControl::set_x2(float v)
{
    to_.x = v;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}
void LineControl::set_y2(float v)
{
    to_.y = v;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

namespace
{

// The paint and stroke fields every shape spec shares.
template <typename S>
void apply_paint(S& spec, ui::Shape& node)
{
    ui::Shape* raw = &node;
    apply(spec.stroke_style, node, [raw](const ui::StrokeStyle& v) { raw->set_stroke_style(v); });
    apply(spec.stroke_thickness, node, [raw](const float& v) { raw->set_stroke_thickness(v); });
    apply(spec.stretch, node, [raw](const ui::Stretch& v) { raw->set_stretch(v); });
    apply(spec.stroke, node, [raw](const ui::Paint& v) { raw->set_stroke(v); });
}

template <typename S>
void apply_fill(S& spec, ui::Shape& node)
{
    ui::Shape* raw = &node;
    apply(spec.fill, node, [raw](const ui::Paint& v) { raw->set_fill(v); });
}

} // namespace

std::unique_ptr<ui::Element> Rectangle::build() &&
{
    auto                  node = std::make_unique<ui::RectangleControl>();
    ui::RectangleControl* raw  = node.get();
    apply_fill(*this, *node);
    apply_paint(*this, *node);
    apply(radius_x, *node, [raw](const float& v) { raw->set_radius_x(v); });
    apply(radius_y, *node, [raw](const float& v) { raw->set_radius_y(v); });
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

std::unique_ptr<ui::Element> Ellipse::build() &&
{
    auto node = std::make_unique<ui::EllipseControl>();
    apply_fill(*this, *node);
    apply_paint(*this, *node);
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

std::unique_ptr<ui::Element> Line::build() &&
{
    auto             node = std::make_unique<ui::LineControl>();
    ui::LineControl* raw  = node.get();
    apply_paint(*this, *node);
    apply(x1, *node, [raw](const float& v) { raw->set_x1(v); });
    apply(y1, *node, [raw](const float& v) { raw->set_y1(v); });
    apply(x2, *node, [raw](const float& v) { raw->set_x2(v); });
    apply(y2, *node, [raw](const float& v) { raw->set_y2(v); });
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

std::unique_ptr<ui::Element> Polyline::build() &&
{
    auto                 node = std::make_unique<ui::PolylineControl>();
    ui::PolylineControl* raw  = node.get();
    apply(fill_rule, *node, [raw = node.get()](const FillRule& v) { raw->set_fill_rule(v); });
    apply_fill(*this, *node);
    apply_paint(*this, *node);
    apply(points, *node, [raw](const std::vector<ui::Point>& v) { raw->set_points(v); });
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

std::unique_ptr<ui::Element> Polygon::build() &&
{
    auto                node = std::make_unique<ui::PolygonControl>();
    ui::PolygonControl* raw  = node.get();
    apply(fill_rule, *node, [raw = node.get()](const FillRule& v) { raw->set_fill_rule(v); });
    apply_fill(*this, *node);
    apply_paint(*this, *node);
    apply(points, *node, [raw](const std::vector<ui::Point>& v) { raw->set_points(v); });
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

std::unique_ptr<ui::Element> Path::build() &&
{
    auto             node = std::make_unique<ui::PathControl>();
    ui::PathControl* raw  = node.get();
    apply_fill(*this, *node);
    apply_paint(*this, *node);
    apply(data, *node, [raw](const ui::PathGeometry& v) { raw->set_data(v); });
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
