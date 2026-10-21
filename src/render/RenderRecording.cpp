// SPDX-License-Identifier: MIT
#include "winui/render/RenderRecording.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/SvgImage.hpp"
#include "winui/render/Text.hpp"

#include <string>
#include <string_view>

namespace ui
{

void RenderRecording::replay(Renderer& target) const
{
    for (const auto& call : calls_)
    {
        call(target);
    }
}

void RenderRecording::fill_rounded_rect(const Rect& bounds, float radius, const Color& color)
{
    calls_.emplace_back([=](Renderer& r) { r.fill_rounded_rect(bounds, radius, color); });
}

void RenderRecording::stroke_rounded_rect(const Rect& bounds, float radius, const Color& color, float stroke_width)
{
    calls_.emplace_back([=](Renderer& r) { r.stroke_rounded_rect(bounds, radius, color, stroke_width); });
}

void RenderRecording::draw_text(std::string_view text, const Rect& bounds, const Color& color, const TextStyle& style)
{
    calls_.emplace_back([text = std::string{text}, bounds, color, style](Renderer& r)
                        { r.draw_text(text, bounds, color, style); });
}

void RenderRecording::fill_rounded_rect_gradient(const Rect& bounds, float radius, const LinearGradientBrush& brush)
{
    calls_.emplace_back([=](Renderer& r) { r.fill_rounded_rect_gradient(bounds, radius, brush); });
}

void RenderRecording::stroke_rounded_rect_gradient(
    const Rect&                bounds,
    float                      radius,
    const LinearGradientBrush& brush,
    float                      extent_px,
    float                      stroke_width)
{
    calls_.emplace_back([=](Renderer& r)
                        { r.stroke_rounded_rect_gradient(bounds, radius, brush, extent_px, stroke_width); });
}

void RenderRecording::stroke_line(Point from, Point to, const Color& color, float stroke_width)
{
    calls_.emplace_back([=](Renderer& r) { r.stroke_line(from, to, color, stroke_width); });
}

void RenderRecording::stroke_wavy_line(Point from, Point to, const Color& color, float stroke_width, float amplitude, float wavelength)
{
    calls_.emplace_back([=](Renderer& r) { r.stroke_wavy_line(from, to, color, stroke_width, amplitude, wavelength); });
}

void RenderRecording::fill_polygon(const FillPolygon& polygon)
{
    calls_.emplace_back([=](Renderer& r) { r.fill_polygon(polygon); });
}

void RenderRecording::stroke_polyline(const StrokePolyline& polyline)
{
    calls_.emplace_back([=](Renderer& r) { r.stroke_polyline(polyline); });
}

void RenderRecording::draw_geometry(const DrawGeometry& geometry)
{
    calls_.emplace_back([=](Renderer& r) { r.draw_geometry(geometry); });
}

void RenderRecording::push_clip(const Rect& bounds)
{
    calls_.emplace_back([=](Renderer& r) { r.push_clip(bounds); });
}

void RenderRecording::pop_clip()
{
    calls_.emplace_back([](Renderer& r) { r.pop_clip(); });
}

void RenderRecording::push_clip_ellipse(Point center, float radius_x, float radius_y)
{
    calls_.emplace_back([=](Renderer& r) { r.push_clip_ellipse(center, radius_x, radius_y); });
}

void RenderRecording::push_clip_rounded_rect(const Rect& bounds, float radius)
{
    calls_.emplace_back([=](Renderer& r) { r.push_clip_rounded_rect(bounds, radius); });
}

void RenderRecording::fill_ellipse(Point center, float radius_x, float radius_y, const Color& color)
{
    calls_.emplace_back([=](Renderer& r) { r.fill_ellipse(center, radius_x, radius_y, color); });
}

void RenderRecording::stroke_ellipse(Point center, float radius_x, float radius_y, const Color& color, float stroke_width)
{
    calls_.emplace_back([=](Renderer& r) { r.stroke_ellipse(center, radius_x, radius_y, color, stroke_width); });
}

void RenderRecording::fill_rect_pattern(const Rect& bounds, Pattern pattern, const Color& fg, const Color& bg)
{
    calls_.emplace_back([=](Renderer& r) { r.fill_rect_pattern(bounds, pattern, fg, bg); });
}

void RenderRecording::draw_image_nine_slice(const std::string& image_name, const Rect& bounds, const Thickness& slice_margins)
{
    calls_.emplace_back([=](Renderer& r) { r.draw_image_nine_slice(image_name, bounds, slice_margins); });
}

void RenderRecording::draw_image(const Bitmap& image, const Rect& bounds, Stretch stretch)
{
    calls_.emplace_back([=](Renderer& r) { r.draw_image(image, bounds, stretch); });
}

void RenderRecording::draw_image_rect(const Bitmap& image, const Rect& source, const Rect& bounds)
{
    calls_.emplace_back([=](Renderer& r) { r.draw_image_rect(image, source, bounds); });
}

void RenderRecording::draw_svg(const SvgImage& svg, const Rect& bounds)
{
    calls_.emplace_back([=](Renderer& r) { r.draw_svg(svg, bounds); });
}

void RenderRecording::push_opacity_layer(float opacity)
{
    calls_.emplace_back([=](Renderer& r) { r.push_opacity_layer(opacity); });
}

void RenderRecording::pop_opacity_layer()
{
    calls_.emplace_back([](Renderer& r) { r.pop_opacity_layer(); });
}

void RenderRecording::push_matrix(const Matrix& transform)
{
    calls_.emplace_back([=](Renderer& r) { r.push_transform(transform); });
}

void RenderRecording::pop_transform()
{
    calls_.emplace_back([](Renderer& r) { r.pop_transform(); });
}

void RenderRecording::set_rasterization(const SetRasterization& policy)
{
    calls_.emplace_back([=](Renderer& r) { r.set_rasterization(policy); });
}

} // namespace ui
