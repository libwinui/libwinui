// SPDX-License-Identifier: MIT
#include "winui/testing/TestRenderer.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Describe.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/SvgImage.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Window.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ui::testing
{

TestRenderer::TestRenderer() = default;

TestRenderer::TestRenderer(const TestRenderer& other) = default;

TestRenderer::TestRenderer(TestRenderer&& other) noexcept = default;

TestRenderer& TestRenderer::operator=(const TestRenderer& other) = default;

TestRenderer& TestRenderer::operator=(TestRenderer&& other) noexcept = default;

TestRenderer::~TestRenderer() = default;

void TestRenderer::fill_rounded_rect(const Rect& bounds, float radius, const Color& color)
{
    commands.push_back(FillRoundedRect{bounds, radius, color});
}

void TestRenderer::stroke_rounded_rect(const Rect& bounds, float radius, const Color& color, float stroke_width)
{
    commands.push_back(StrokeRoundedRect{bounds, radius, color, stroke_width});
}

void TestRenderer::draw_text(std::string_view text, const Rect& bounds, const Color& color, const TextStyle& style)
{
    commands.push_back(DrawTextRun{std::string{text}, bounds, color, style});
}

void TestRenderer::fill_rounded_rect_gradient(const Rect& bounds, float radius, const LinearGradientBrush& brush)
{
    commands.push_back(FillRoundedRectGradient{bounds, radius, brush});
}

void TestRenderer::stroke_rounded_rect_gradient(
    const Rect&                bounds,
    float                      radius,
    const LinearGradientBrush& brush,
    float                      extent_px,
    float                      stroke_width)
{
    commands.push_back(StrokeRoundedRectGradient{bounds, radius, brush, extent_px, stroke_width});
}

void TestRenderer::stroke_line(Point from, Point to, const Color& color, float stroke_width)
{
    commands.push_back(StrokeLine{from, to, color, stroke_width});
}

void TestRenderer::stroke_wavy_line(Point from, Point to, const Color& color, float stroke_width, float amplitude, float wavelength)
{
    commands.push_back(StrokeWavyLine{from, to, color, stroke_width, amplitude, wavelength});
}

void TestRenderer::fill_polygon(const FillPolygon& polygon)
{
    commands.push_back(polygon);
}

void TestRenderer::stroke_polyline(const StrokePolyline& polyline)
{
    commands.push_back(polyline);
}

void TestRenderer::draw_geometry(const DrawGeometry& geometry)
{
    commands.push_back(geometry);
}

void TestRenderer::push_clip(const Rect& bounds)
{
    commands.push_back(PushClip{bounds});
}

void TestRenderer::pop_clip()
{
    commands.push_back(PopClip{});
}

void TestRenderer::push_clip_ellipse(Point center, float radius_x, float radius_y)
{
    commands.push_back(PushClipEllipse{center, radius_x, radius_y});
}

void TestRenderer::push_clip_rounded_rect(const Rect& bounds, float radius)
{
    commands.push_back(PushClipRoundedRect{bounds, radius});
}

void TestRenderer::push_opacity_layer(float opacity)
{
    commands.push_back(PushOpacityLayer{opacity});
}

void TestRenderer::pop_opacity_layer()
{
    commands.push_back(PopOpacityLayer{});
}

void TestRenderer::push_matrix(const Matrix& m)
{
    commands.push_back(PushTransform{m.m11, Point{m.dx, m.dy}, m.m22, m.m12, m.m21});
}

void TestRenderer::pop_transform()
{
    commands.push_back(PopTransform{});
}

void TestRenderer::set_rasterization(const SetRasterization& policy)
{
    commands.push_back(policy);
}

void TestRenderer::fill_ellipse(Point center, float radius_x, float radius_y, const Color& color)
{
    commands.push_back(FillEllipse{center, radius_x, radius_y, color});
}

void TestRenderer::stroke_ellipse(Point center, float radius_x, float radius_y, const Color& color, float stroke_width)
{
    commands.push_back(StrokeEllipse{center, radius_x, radius_y, color, stroke_width});
}

void TestRenderer::fill_rect_pattern(const Rect& bounds, Pattern pattern, const Color& fg, const Color& bg)
{
    commands.push_back(FillRectPattern{bounds, pattern, fg, bg});
}

void TestRenderer::draw_image_nine_slice(const std::string& image_name, const Rect& bounds, const Thickness& slice_margins)
{
    commands.push_back(DrawImageNineSlice{image_name, bounds, slice_margins});
}

void TestRenderer::draw_image(const Bitmap& image, const Rect& bounds, Stretch stretch)
{
    commands.push_back(
        DrawImage{Size{static_cast<float>(image.width()), static_cast<float>(image.height())}, bounds, stretch});
}

void TestRenderer::draw_image_rect(const Bitmap& image, const Rect& source, const Rect& bounds)
{
    commands.push_back(
        DrawImageRect{Size{static_cast<float>(image.width()), static_cast<float>(image.height())}, source, bounds});
}

void TestRenderer::draw_svg(const SvgImage& svg, const Rect& bounds)
{
    commands.push_back(DrawSvg{svg.natural_size(), bounds});
}

void TestRenderer::clear()
{
    commands.clear();
}

std::string describe(const Window& window)
{
    TestRenderer r;
    window.render(r);
    return ui::describe(r.commands);
}

} // namespace ui::testing
