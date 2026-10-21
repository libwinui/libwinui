// SPDX-License-Identifier: MIT
#include "winui/render/Describe.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"

#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace ui
{
namespace
{

std::string fnum(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
    return buf;
}

std::string rect_str(const Rect& r)
{
    return "(" + fnum(r.x) + "," + fnum(r.y) + "," + fnum(r.width) + "," + fnum(r.height) + ")";
}

std::uint8_t q(float v)
{
    v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    return static_cast<std::uint8_t>(std::lround(v * 255.0f));
}

std::string color_str(const Color& c)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", q(c.r), q(c.g), q(c.b), q(c.a));
    return buf;
}

const char* axis_str(GradientAxis a)
{
    switch (a)
    {
        case GradientAxis::Vertical:
            return "vertical";
        case GradientAxis::Horizontal:
            return "horizontal";
        case GradientAxis::Diagonal:
            return "diagonal";
    }
    return "vertical";
}

const char* stretch_str(Stretch s)
{
    switch (s)
    {
        case Stretch::None:
            return "none";
        case Stretch::Fill:
            return "fill";
        case Stretch::Uniform:
            return "uniform";
        case Stretch::UniformToFill:
            return "uniform_to_fill";
    }
    return "uniform";
}

const char* align_str(TextAlign a)
{
    switch (a)
    {
        case TextAlign::Leading:
            return "leading";
        case TextAlign::Trailing:
            return "trailing";
        case TextAlign::Center:
            return "center";
    }
    return "center";
}

const char* weight_str(FontWeight w)
{
    switch (w)
    {
        case FontWeight::Light:
            return "light";
        case FontWeight::SemiLight:
            return "semilight";
        case FontWeight::Regular:
            return "regular";
        case FontWeight::SemiBold:
            return "semibold";
        case FontWeight::Bold:
            return "bold";
    }
    return "regular";
}

// A label for the semantic dump: every code point outside printable ASCII is escaped, so the
// golden is pure ASCII and stable regardless of locale.
std::string narrow(std::string_view text)
{
    std::string s;
    for (const char32_t cp : code_points(text))
    {
        if (cp >= 0x20 && cp < 0x7F)
        {
            s.push_back(static_cast<char>(cp));
        }
        else
        {
            char buf[12];
            std::snprintf(buf, sizeof(buf), cp > 0xFFFF ? "\\U%08X" : "\\u%04X", static_cast<unsigned>(cp));
            s += buf;
        }
    }
    return s;
}


std::string point_str(Point p)
{
    return "(" + fnum(p.x) + "," + fnum(p.y) + ")";
}

std::string stops_str(const GradientStops& stops)
{
    std::string out = "[";
    for (std::size_t i = 0; i < stops.size(); ++i)
    {
        if (i != 0)
        {
            out += " ";
        }
        out += fnum(stops[i].offset) + ":" + color_str(stops[i].color);
    }
    return out + "]";
}

std::string brush_str(const Brush& brush)
{
    return std::visit(
        [](const auto& b) -> std::string
        {
            using B = std::decay_t<decltype(b)>;
            if constexpr (std::is_same_v<B, Color>)
            {
                return color_str(b);
            }
            else if constexpr (std::is_same_v<B, LinearGradientBrush>)
            {
                return std::string("linear(") + axis_str(b.axis) + " " + stops_str(b.stops) + ")";
            }
            else if constexpr (std::is_same_v<B, RadialGradientBrush>)
            {
                const char* spread = b.spread_method == GradientSpreadMethod::Pad       ? "pad"
                                     : b.spread_method == GradientSpreadMethod::Reflect ? "reflect"
                                                                                        : "repeat";
                return "radial(center=" + point_str(b.center) + " r=(" + fnum(b.radius_x) + "," + fnum(b.radius_y) +
                       ") origin=" + point_str(b.gradient_origin) +
                       (b.mapping_mode == BrushMappingMode::Absolute ? " absolute " : " relative ") + spread + " " +
                       stops_str(b.stops) + ")";
            }
            else
            {
                return "acrylic(tint=" + color_str(b.effective_tint()) + " lum=" + color_str(b.effective_luminosity()) +
                       " fallback=" + color_str(b.fallback_color) + (b.always_use_fallback ? " fallback-only" : "") +
                       ")";
            }
        },
        brush);
}

const char* cap_str(PenLineCap c)
{
    switch (c)
    {
        case PenLineCap::Flat:
            return "flat";
        case PenLineCap::Square:
            return "square";
        case PenLineCap::Round:
            return "round";
        case PenLineCap::Triangle:
            return "triangle";
    }
    return "flat";
}
} // namespace

std::string describe(const std::vector<RenderCommand>& commands)
{
    std::string out;
    for (const auto& cmd : commands)
    {
        std::visit(
            [&](const auto& c)
            {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, FillRoundedRect>)
                {
                    out += "FillRoundedRect rect=" + rect_str(c.bounds) + " radius=" + fnum(c.radius) +
                           " color=" + color_str(c.color) + "\n";
                }
                else if constexpr (std::is_same_v<T, StrokeRoundedRect>)
                {
                    out += "StrokeRoundedRect rect=" + rect_str(c.bounds) + " radius=" + fnum(c.radius) +
                           " color=" + color_str(c.color) + " width=" + fnum(c.stroke_width) + "\n";
                }
                else if constexpr (std::is_same_v<T, DrawTextRun>)
                {
                    out += "DrawTextRun rect=" + rect_str(c.bounds) + " color=" + color_str(c.color) +
                           " size=" + fnum(c.style.font_size) + " weight=" + weight_str(c.style.weight) +
                           " halign=" + align_str(c.style.horizontal) + " valign=" + align_str(c.style.vertical);
                    if (c.style.line_height > 0.0f)
                    {
                        out += " lineheight=" + fnum(c.style.line_height);
                    }
                    if (c.style.effect.kind == TextEffectKind::Emboss)
                    {
                        out += " effect=emboss";
                    }
                    if (c.style.wrapping != TextWrapping::NoWrap)
                    {
                        out += c.style.wrapping == TextWrapping::Wrap ? " wrap=wrap" : " wrap=words";
                    }
                    if (c.style.italic)
                    {
                        out += " italic";
                    }
                    if (c.style.underline)
                    {
                        out += " underline";
                    }
                    if (c.style.strikethrough)
                    {
                        out += " strikethrough";
                    }
                    if (c.style.character_spacing != 0)
                    {
                        out += " spacing=" + std::to_string(c.style.character_spacing);
                    }
                    out += " text=\"" + narrow(c.text) + "\"\n";
                }
                else if constexpr (std::is_same_v<T, PushClip>)
                {
                    out += "PushClip rect=" + rect_str(c.bounds) + "\n";
                }
                else if constexpr (std::is_same_v<T, PopClip>)
                {
                    out += "PopClip\n";
                }
                else if constexpr (std::is_same_v<T, PushClipEllipse>)
                {
                    out += "PushClipEllipse center=(" + fnum(c.center.x) + "," + fnum(c.center.y) +
                           ") rx=" + fnum(c.radius_x) + " ry=" + fnum(c.radius_y) + "\n";
                }
                else if constexpr (std::is_same_v<T, PushClipRoundedRect>)
                {
                    out += "PushClipRoundedRect rect=" + rect_str(c.bounds) + " radius=" + fnum(c.radius) + "\n";
                }
                else if constexpr (std::is_same_v<T, PushOpacityLayer>)
                {
                    out += "PushOpacityLayer opacity=" + fnum(c.opacity) + "\n";
                }
                else if constexpr (std::is_same_v<T, PopOpacityLayer>)
                {
                    out += "PopOpacityLayer\n";
                }
                else if constexpr (std::is_same_v<T, PushTransform>)
                {
                    // A uniform scale prints as one number, as it did before scale_y existed.
                    const std::string scale = c.scale_y == c.scale ? fnum(c.scale) : fnum(c.scale) + "x" + fnum(c.scale_y);
                    out += "PushTransform scale=" + scale + " offset=(" + fnum(c.offset.x) + "," + fnum(c.offset.y) +
                           ")";
                    // The off-diagonal terms only print for a rotation, so every older dump reads the same.
                    if (c.m12 != 0.0f || c.m21 != 0.0f)
                    {
                        out += " skew=(" + fnum(c.m12) + "," + fnum(c.m21) + ")";
                    }
                    out += "\n";
                }
                else if constexpr (std::is_same_v<T, PopTransform>)
                {
                    out += "PopTransform\n";
                }
                else if constexpr (std::is_same_v<T, FillRoundedRectGradient>)
                {
                    out += "FillRoundedRectGradient rect=" + rect_str(c.bounds) + " radius=" + fnum(c.radius) +
                           " axis=" + axis_str(c.brush.axis) + " stops=[";
                    for (std::size_t i = 0; i < c.brush.stops.size(); ++i)
                    {
                        if (i != 0)
                        {
                            out += " ";
                        }
                        out += fnum(c.brush.stops[i].offset) + ":" + color_str(c.brush.stops[i].color);
                    }
                    out += "]\n";
                }
                else if constexpr (std::is_same_v<T, StrokeRoundedRectGradient>)
                {
                    out += "StrokeRoundedRectGradient rect=" + rect_str(c.bounds) + " radius=" + fnum(c.radius) +
                           " extent=" + fnum(c.extent_px) + " width=" + fnum(c.stroke_width) + " stops=[";
                    for (std::size_t i = 0; i < c.brush.stops.size(); ++i)
                    {
                        if (i != 0)
                        {
                            out += " ";
                        }
                        out += fnum(c.brush.stops[i].offset) + ":" + color_str(c.brush.stops[i].color);
                    }
                    out += "]\n";
                }
                else if constexpr (std::is_same_v<T, StrokeLine>)
                {
                    out += "StrokeLine from=(" + fnum(c.from.x) + "," + fnum(c.from.y) + ") to=(" + fnum(c.to.x) + "," +
                           fnum(c.to.y) + ") color=" + color_str(c.color) + " width=" + fnum(c.stroke_width) + "\n";
                }
                else if constexpr (std::is_same_v<T, StrokeWavyLine>)
                {
                    out += "StrokeWavyLine from=(" + fnum(c.from.x) + "," + fnum(c.from.y) + ") to=(" + fnum(c.to.x) +
                           "," + fnum(c.to.y) + ") color=" + color_str(c.color) + " width=" + fnum(c.stroke_width) +
                           " amplitude=" + fnum(c.amplitude) + " wavelength=" + fnum(c.wavelength) + "\n";
                }
                else if constexpr (std::is_same_v<T, FillPolygon>)
                {
                    out += "FillPolygon points=[";
                    for (std::size_t i = 0; i < c.points.size(); ++i)
                    {
                        if (i != 0)
                        {
                            out += " ";
                        }
                        out += "(" + fnum(c.points[i].x) + "," + fnum(c.points[i].y) + ")";
                    }
                    out += "] fill=" + color_str(c.fill) + "\n";
                }
                else if constexpr (std::is_same_v<T, StrokePolyline>)
                {
                    out += "StrokePolyline points=[";
                    for (std::size_t i = 0; i < c.points.size(); ++i)
                    {
                        if (i != 0)
                        {
                            out += " ";
                        }
                        out += "(" + fnum(c.points[i].x) + "," + fnum(c.points[i].y) + ")";
                    }
                    out += "] color=" + color_str(c.color) + " width=" + fnum(c.stroke_width) +
                           (c.closed ? " closed" : " open") + "\n";
                }
                else if constexpr (std::is_same_v<T, FillEllipse>)
                {
                    out += "FillEllipse center=(" + fnum(c.center.x) + "," + fnum(c.center.y) + ") rx=" + fnum(c.radius_x) +
                           " ry=" + fnum(c.radius_y) + " color=" + color_str(c.color) + "\n";
                }
                else if constexpr (std::is_same_v<T, StrokeEllipse>)
                {
                    out += "StrokeEllipse center=(" + fnum(c.center.x) + "," + fnum(c.center.y) +
                           ") rx=" + fnum(c.radius_x) + " ry=" + fnum(c.radius_y) + " color=" + color_str(c.color) +
                           " width=" + fnum(c.stroke_width) + "\n";
                }
                else if constexpr (std::is_same_v<T, FillRectPattern>)
                {
                    out += "FillRectPattern rect=" + rect_str(c.bounds) +
                           " pattern=checkerboard50 fg=" + color_str(c.fg) + " bg=" + color_str(c.bg) + "\n";
                }
                else if constexpr (std::is_same_v<T, DrawImageNineSlice>)
                {
                    out += "DrawImageNineSlice image=\"" + c.image_name + "\" rect=" + rect_str(c.bounds) + " slice=(" +
                           fnum(c.slice_margins.left) + "," + fnum(c.slice_margins.top) + "," +
                           fnum(c.slice_margins.right) + "," + fnum(c.slice_margins.bottom) + ")\n";
                }
                else if constexpr (std::is_same_v<T, DrawImage>)
                {
                    out += "DrawImage size=(" + fnum(c.image_size.width) + "," + fnum(c.image_size.height) +
                           ") rect=" + rect_str(c.bounds) + " stretch=" + stretch_str(c.stretch) + "\n";
                }
                else if constexpr (std::is_same_v<T, DrawImageRect>)
                {
                    out += "DrawImageRect size=(" + fnum(c.image_size.width) + "," + fnum(c.image_size.height) +
                           ") source=" + rect_str(c.source) + " rect=" + rect_str(c.bounds) + "\n";
                }
                else if constexpr (std::is_same_v<T, DrawSvg>)
                {
                    out += "DrawSvg size=(" + fnum(c.natural_size.width) + "," + fnum(c.natural_size.height) +
                           ") rect=" + rect_str(c.bounds) + "\n";
                }
                else if constexpr (std::is_same_v<T, DrawGeometry>)
                {
                    out += "DrawGeometry bounds=" + rect_str(c.geometry.bounds()) +
                           " figures=" + std::to_string(c.geometry.figures.size()) +
                           (c.geometry.fill_rule == FillRule::Nonzero ? " nonzero" : " evenodd");
                    if (c.fill)
                    {
                        out += " fill=" + brush_str(*c.fill);
                    }
                    if (c.stroke)
                    {
                        const StrokeStyle& st = c.stroke_style;
                        out += " stroke=" + brush_str(*c.stroke) + " width=" + fnum(st.thickness);
                        if (st.start_cap != PenLineCap::Flat || st.end_cap != PenLineCap::Flat)
                        {
                            out += std::string(" caps=") + cap_str(st.start_cap) + "/" + cap_str(st.end_cap);
                        }
                        if (st.line_join != PenLineJoin::Miter)
                        {
                            out += st.line_join == PenLineJoin::Round ? " join=round" : " join=bevel";
                        }
                        if (!st.dash_array.empty())
                        {
                            out += " dashes=[";
                            for (std::size_t i = 0; i < st.dash_array.size(); ++i)
                            {
                                out += (i != 0 ? " " : "") + fnum(st.dash_array[i]);
                            }
                            out += "]";
                        }
                    }
                    out += "\n";
                }
                else if constexpr (std::is_same_v<T, SetRasterization>)
                {
                    out += std::string("SetRasterization geometry=") +
                           (c.geometry == GeometryAA::Aliased ? "aliased" : "antialiased") + " text=" +
                           (c.text == TextAA::Aliased     ? "aliased"
                            : c.text == TextAA::Grayscale ? "grayscale"
                                                          : "subpixel") +
                           " snap=" + (c.snap_to_device_pixels ? "true" : "false") + "\n";
                }
            },
            cmd);
    }
    return out;
}

} // namespace ui
