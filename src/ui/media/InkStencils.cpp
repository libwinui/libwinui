// SPDX-License-Identifier: MIT
#include "InkStencils.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/media/InkCanvas.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <format>
#include <numbers>
#include <string>

#include <cmath>

namespace ui::detail
{

namespace
{
constexpr float StencilEdgeBand   = 12.0f; // how far inside an edge a press still draws along it
constexpr float StencilEdgeReach  = 20.0f; // how far outside an edge a press still draws along it
constexpr float StencilDipsPerMm  = 96.0f / 25.4f;
constexpr Color StencilBackground = Color::from_rgba(242, 242, 242, 220);
constexpr Color StencilForeground = Color::from_rgba(32, 32, 32);
constexpr Color StencilOutline    = Color::from_rgba(0, 0, 0, 90);

// `p` in the frame of a stencil centered at `center` and turned `angle` degrees.
Point stencil_local(Point p, Point center, float angle)
{
    return Matrix::rotation(-angle).apply(Point{p.x - center.x, p.y - center.y});
}

Point stencil_canvas(Point local, Point center, float angle)
{
    const Point turned = Matrix::rotation(angle).apply(local);
    return Point{turned.x + center.x, turned.y + center.y};
}

// The angle as a stencil reads it out: whole degrees, 0 to 359.
std::string stencil_degrees(float angle)
{
    const auto whole = static_cast<int>(std::lround(angle));
    return std::format("{}\u00B0", ((whole % 360) + 360) % 360);
}

TextStyle stencil_text(const Theme& theme, float size)
{
    TextStyle style = theme.text_style;
    style.font_size = size;
    return style;
}
} // namespace

InkStencilTarget ink_stencil_hit(Point p, const InkRuler& ruler, Point ruler_center, const InkProtractor& protractor, Point protractor_center)
{
    if (ruler.visible)
    {
        const Point l     = stencil_local(p, ruler_center, ruler.angle);
        const float half  = ruler.width * 0.5f;
        const float along = std::abs(l.x);
        if (along <= ruler.length * 0.5f)
        {
            const float across = std::abs(l.y);
            if (across >= half - StencilEdgeBand && across <= half + StencilEdgeReach)
            {
                return InkStencilTarget{.ruler = true, .hit = InkStencilHit::Edge, .side = l.y < 0.0f ? -1 : 1};
            }
            if (across < half)
            {
                return InkStencilTarget{.ruler = true, .hit = InkStencilHit::Body};
            }
        }
    }
    if (protractor.visible)
    {
        const float r = std::hypot(p.x - protractor_center.x, p.y - protractor_center.y);
        if (r >= protractor.radius - StencilEdgeBand && r <= protractor.radius + StencilEdgeReach)
        {
            return InkStencilTarget{.ruler = false, .hit = InkStencilHit::Edge};
        }
        if (r < protractor.radius)
        {
            return InkStencilTarget{.ruler = false, .hit = InkStencilHit::Body};
        }
    }
    return InkStencilTarget{};
}

Point ink_stencil_snap(Point                   p,
                       const InkStencilTarget& target,
                       const InkRuler&         ruler,
                       Point                   ruler_center,
                       const InkProtractor&    protractor,
                       Point                   protractor_center,
                       float                   half_nib)
{
    if (target.ruler)
    {
        // Along the edge line, just outside it, and no further than the ruler's ends.
        Point l = stencil_local(p, ruler_center, ruler.angle);
        l.x     = std::clamp(l.x, -ruler.length * 0.5f, ruler.length * 0.5f);
        l.y     = static_cast<float>(target.side) * (ruler.width * 0.5f + half_nib);
        return stencil_canvas(l, ruler_center, ruler.angle);
    }
    // Round the circle, just outside it, in the direction of the pointer.
    const float dx = p.x - protractor_center.x;
    const float dy = p.y - protractor_center.y;
    const float r  = std::hypot(dx, dy);
    const float to = protractor.radius + half_nib;
    if (r <= 0.0f)
    {
        return Point{protractor_center.x + to, protractor_center.y};
    }
    return Point{protractor_center.x + dx / r * to, protractor_center.y + dy / r * to};
}

void draw_ink_ruler(Renderer& renderer, const Theme& theme, const InkRuler& ruler, Point center)
{
    const float half_length = ruler.length * 0.5f;
    const float half_width  = ruler.width * 0.5f;
    renderer.push_transform(Matrix::rotation(ruler.angle) * Matrix::translation(center.x, center.y));
    const Rect body{-half_length, -half_width, ruler.length, ruler.width};
    renderer.fill_rounded_rect(body, 4.0f, StencilBackground);
    renderer.stroke_rounded_rect(body, 4.0f, StencilOutline, 1.0f);

    if (ruler.tick_marks)
    {
        // A centimetre scale along the top edge: millimetre ticks, longer at the half and whole
        // centimetres, numbered at each centimetre.
        const TextStyle label = stencil_text(theme, 10.0f);
        for (int mm = 0;; ++mm)
        {
            const float x = -half_length + 8.0f + static_cast<float>(mm) * StencilDipsPerMm;
            if (x > half_length - 8.0f)
            {
                break;
            }
            const float tick = mm % 10 == 0 ? 16.0f : (mm % 5 == 0 ? 11.0f : 6.0f);
            renderer.stroke_line(Point{x, -half_width}, Point{x, -half_width + tick}, StencilForeground, 1.0f);
            if (mm % 10 == 0)
            {
                renderer.draw_text(std::to_string(mm / 10),
                                   Rect{x - 12.0f, -half_width + 17.0f, 24.0f, 14.0f},
                                   StencilForeground,
                                   label);
            }
        }
    }
    renderer.draw_text(stencil_degrees(ruler.angle),
                       Rect{-40.0f, half_width - 26.0f, 80.0f, 20.0f},
                       StencilForeground,
                       stencil_text(theme, 12.0f));
    renderer.pop_transform();
}

void draw_ink_protractor(Renderer& renderer, const Theme& theme, const InkProtractor& protractor, Point center)
{
    const float r = protractor.radius;
    renderer.push_transform(Matrix::rotation(protractor.angle) * Matrix::translation(center.x, center.y));
    renderer.fill_ellipse(Point{}, r, r, StencilBackground);
    renderer.stroke_ellipse(Point{}, r, r, StencilOutline, 1.0f);

    if (protractor.tick_marks)
    {
        // A degree scale round the edge, longer every 5 and 10 degrees, numbered every 10.
        const TextStyle label = stencil_text(theme, 9.0f);
        for (int degree = 0; degree < 360; ++degree)
        {
            const float a    = static_cast<float>(degree) * std::numbers::pi_v<float> / 180.0f;
            const float c    = std::cos(a);
            const float s    = -std::sin(a); // counter-clockwise from the right, as a protractor reads
            const float tick = degree % 10 == 0 ? 14.0f : (degree % 5 == 0 ? 9.0f : 5.0f);
            renderer.stroke_line(Point{c * r, s * r}, Point{c * (r - tick), s * (r - tick)}, StencilForeground, 1.0f);
            if (degree % 10 == 0)
            {
                const Point at{c * (r - 24.0f), s * (r - 24.0f)};
                renderer.draw_text(std::to_string(degree),
                                   Rect{at.x - 12.0f, at.y - 7.0f, 24.0f, 14.0f},
                                   StencilForeground,
                                   label);
            }
        }
    }
    if (protractor.center_marker)
    {
        renderer.stroke_line(Point{-8.0f, 0.0f}, Point{8.0f, 0.0f}, StencilForeground, 1.0f);
        renderer.stroke_line(Point{0.0f, -8.0f}, Point{0.0f, 8.0f}, StencilForeground, 1.0f);
    }
    if (protractor.angle_readout)
    {
        renderer.draw_text(stencil_degrees(protractor.angle),
                           Rect{-40.0f, 12.0f, 80.0f, 20.0f},
                           StencilForeground,
                           stencil_text(theme, 12.0f));
    }
    renderer.pop_transform();
}

} // namespace ui::detail
