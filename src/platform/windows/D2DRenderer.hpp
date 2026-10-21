// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "../../render/ImageThemePack.hpp"
#include "DWriteCache.hpp"
#include "DWriteMeasurer.hpp"
#include "winui/core/PerfCounters.hpp"
#include "winui/render/Renderer.hpp"

#include <algorithm>
#include <d2d1.h>
#include <d2d1_3.h>
#include <d2d1effects.h>
#include <dwrite.h>
#include <shlwapi.h>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

#include <cmath>

namespace ui::win32
{

using Microsoft::WRL::ComPtr;

class D2DRenderer final : public Renderer
{
public:
    // Lives as long as `target`: its caches hold device resources made on it. A window keeps one
    // and calls begin_frame() before every frame.
    D2DRenderer(ID2D1RenderTarget* target, IDWriteFactory* dwrite) : target_(target), dwrite_(dwrite)
    {
        target_->GetFactory(&factory_);
        target_->GetDpi(&dpi_x_, &dpi_y_);
    }

    // Before every frame: picks up a DPI change and ages the caches by one frame, dropping what
    // has not been drawn for a while once a cache has grown.
    void begin_frame()
    {
        target_->GetDpi(&dpi_x_, &dpi_y_);
        pushed_.clear();
        transforms_.clear();
        ++frame_;
        if (brushes_.size() > 1024)
        {
            brushes_.clear(); // an animated color makes a brush per frame
        }
        age_out(geometries_);
        age_out(stroke_styles_);
        age_out(gradients_);
        if (device_bitmap_bytes_ > BitmapBudget)
        {
            std::erase_if(device_bitmaps_,
                          [this](const auto& item)
                          {
                              const bool old = item.second.last_used + 1 < frame_;
                              if (old)
                              {
                                  device_bitmap_bytes_ -= item.second.bytes;
                              }
                              return old;
                          });
        }
        next_text_frame();
    }

    void fill_rounded_rect(const Rect& b, float radius, const Color& color) override
    {
        const D2D1_ROUNDED_RECT rr{to_d2d(snap(b)), radius, radius};
        target_->FillRoundedRectangle(rr, brush(color));
    }

    void stroke_rounded_rect(const Rect& b, float radius, const Color& color, float stroke_width) override
    {
        const SnappedStroke     s = snap_stroke(b, stroke_width);
        const D2D1_ROUNDED_RECT rr{to_d2d(s.path), radius, radius};
        target_->DrawRoundedRectangle(rr, brush(color), s.width);
    }

    // Through a text layout from the process-wide cache (DWriteCache.hpp), keyed by the string, its
    // style and the box, so a label drawn again in the next frame reuses the layout.
    void draw_text(std::string_view utf8, const Rect& b, const Color& color, const TextStyle& style) override
    {
        // Not snapped: rounding a wrap box's edges can shave a pixel and re-wrap text that fit.
        // Half a DIP of slack absorbs measure/draw rounding, so a line never breaks a word early.
        const Rect layout_box = style.wrapping == TextWrapping::NoWrap ? b : Rect{b.x, b.y, b.width + 0.5f, b.height};
        const ComPtr<IDWriteTextLayout> layout = cached_text_layout(dwrite_,
                                                                    utf8,
                                                                    style,
                                                                    std::max(0.0f, layout_box.width),
                                                                    std::max(0.0f, layout_box.height));
        if (!layout)
        {
            return;
        }
        // Color glyphs (emoji) in their own colors, the default for text.
        constexpr D2D1_DRAW_TEXT_OPTIONS Options = D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT;
        if (style.effect.kind == TextEffectKind::Emboss)
        {
            // The classic DSS_DISABLED engraved look: a raised pass offset by (dx,dy), then the base pass.
            target_->DrawTextLayout(D2D1::Point2F(layout_box.x + style.effect.offset_x, layout_box.y + style.effect.offset_y),
                                    layout.Get(),
                                    brush(style.effect.highlight),
                                    Options);
            target_->DrawTextLayout(D2D1::Point2F(layout_box.x, layout_box.y), layout.Get(), brush(style.effect.shadow), Options);
            return;
        }
        target_->DrawTextLayout(D2D1::Point2F(layout_box.x, layout_box.y), layout.Get(), brush(color), Options);
    }

    void fill_rounded_rect_gradient(const Rect& b, float radius, const LinearGradientBrush& brush_desc) override
    {
        const Rect    sb = snap(b);
        D2D1_POINT_2F p0, p1;
        switch (brush_desc.axis)
        {
            case GradientAxis::Horizontal:
                p0 = D2D1::Point2F(sb.left(), sb.y);
                p1 = D2D1::Point2F(sb.right(), sb.y);
                break;
            case GradientAxis::Diagonal:
                p0 = D2D1::Point2F(sb.left(), sb.top());
                p1 = D2D1::Point2F(sb.right(), sb.bottom());
                break;
            case GradientAxis::Vertical:
            default:
                p0 = D2D1::Point2F(sb.x, sb.top());
                p1 = D2D1::Point2F(sb.x, sb.bottom());
                break;
        }
        ID2D1LinearGradientBrush* gradient = linear_gradient(brush_desc.stops, D2D1_EXTEND_MODE_CLAMP, p0, p1);
        if (!gradient)
        {
            return;
        }
        const D2D1_ROUNDED_RECT rr{to_d2d(sb), radius, radius};
        target_->FillRoundedRectangle(rr, gradient);
    }

    void stroke_rounded_rect_gradient(const Rect&                b,
                                      float                      radius,
                                      const LinearGradientBrush& brush_desc,
                                      float                      extent_px,
                                      float                      stroke_width) override
    {
        const SnappedStroke s  = snap_stroke(b, stroke_width);
        const Rect&         sb = s.path;
        // Absolute mapping: the axis spans `extent_px` device pixels from one edge. A negative extent
        // anchors at the bottom, the flip an accent control's elevation border uses.
        const float               y0       = extent_px >= 0.0f ? sb.top() : sb.bottom();
        ID2D1LinearGradientBrush* gradient = linear_gradient(brush_desc.stops,
                                                             D2D1_EXTEND_MODE_CLAMP,
                                                             D2D1::Point2F(sb.x, y0),
                                                             D2D1::Point2F(sb.x, y0 + extent_px));
        if (!gradient)
        {
            return;
        }
        const D2D1_ROUNDED_RECT rr{to_d2d(sb), radius, radius};
        target_->DrawRoundedRectangle(rr, gradient, s.width);
    }

    // Snap the band by its two edges, like snap(Rect) does for a fill. Snapping a fractional
    // center lands one pixel inside the fill and leaves a 1px spur on classic bevels.
    void stroke_line(Point from, Point to, const Color& color, float stroke_width) override
    {
        D2D1_POINT_2F a{snap_x(from.x), snap_y(from.y)};
        D2D1_POINT_2F b{snap_x(to.x), snap_y(to.y)};
        float         width = stroke_width;

        if (snap_ && stroke_width > 0.0f)
        {
            if (from.y == to.y && from.x != to.x)
            {
                const float lo = snap_y(from.y - stroke_width * 0.5f);
                // A band thinner than a device pixel still gets one pixel (Slider's groove, classic toolbar edge).
                const float hi = std::max(snap_y(from.y + stroke_width * 0.5f), lo + 96.0f / dpi_y_);
                a.y = b.y = (lo + hi) * 0.5f;
                width     = hi - lo;
            }
            else if (from.x == to.x && from.y != to.y)
            {
                const float lo = snap_x(from.x - stroke_width * 0.5f);
                const float hi = std::max(snap_x(from.x + stroke_width * 0.5f), lo + 96.0f / dpi_x_);
                a.x = b.x = (lo + hi) * 0.5f;
                width     = hi - lo;
            }
        }
        target_->DrawLine(a, b, brush(color), width);
    }

    // Quadratic Béziers, one per half-period. A control point at twice the amplitude puts the
    // apex exactly `amplitude` off the axis.
    void stroke_wavy_line(Point from, Point to, const Color& color, float stroke_width, float amplitude, float wavelength) override
    {
        const D2D1_POINT_2F a{snap_x(from.x), snap_y(from.y)};
        const D2D1_POINT_2F b{snap_x(to.x), snap_y(to.y)};
        const float         dx     = b.x - a.x;
        const float         dy     = b.y - a.y;
        const float         length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0f || wavelength <= 0.0f)
        {
            return;
        }
        key_begin('w');
        key_add(a);
        key_add(b);
        key_add(amplitude);
        key_add(wavelength);
        ID2D1Geometry* path = cached_geometry(
            [&](ComPtr<ID2D1PathGeometry>& geometry, ID2D1GeometrySink* sink)
            {
                const float ux   = dx / length;
                const float uy   = dy / length;
                const float nx   = -uy;
                const float ny   = ux;
                const float half = wavelength * 0.5f;

                sink->BeginFigure(a, D2D1_FIGURE_BEGIN_HOLLOW);
                float t    = 0.0f;
                float side = -1.0f; // first crest rises (screen-space up for a left-to-right line)
                while (t < length)
                {
                    const float step  = std::min(half, length - t);
                    const float apex  = 2.0f * amplitude * side * (step / half);
                    const float mid   = t + step * 0.5f;
                    const float end   = t + step;
                    const auto  ctrl  = D2D1::Point2F(a.x + ux * mid + nx * apex, a.y + uy * mid + ny * apex);
                    const auto  point = D2D1::Point2F(a.x + ux * end, a.y + uy * end);
                    sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(ctrl, point));
                    t    = end;
                    side = -side;
                }
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                (void)geometry;
            });
        if (path)
        {
            target_->DrawGeometry(path, brush(color), stroke_width);
        }
    }

    void fill_polygon(const FillPolygon& polygon) override
    {
        if (polygon.fill.a > 0 && polygon.points.size() >= 3)
        {
            if (ID2D1Geometry* fill = polyline_geometry(polygon.points, true, true))
            {
                target_->FillGeometry(fill, brush(polygon.fill));
            }
        }
    }

    void stroke_polyline(const StrokePolyline& polyline) override
    {
        if (polyline.color.a > 0 && polyline.stroke_width > 0.0f && polyline.points.size() >= 2)
        {
            if (ID2D1Geometry* outline = polyline_geometry(polyline.points, false, polyline.closed))
            {
                target_->DrawGeometry(outline, brush(polyline.color), polyline.stroke_width);
            }
        }
    }

    // The snapped path through `points`, cached: filled (and so closed) for fill_polygon(), hollow
    // and open or closed for stroke_polyline().
    ID2D1Geometry* polyline_geometry(const std::vector<Point>& points, bool filled, bool closed)
    {
        key_begin(filled ? 'f' : (closed ? 'c' : 'o'));
        for (const Point& p : points)
        {
            key_add(snap_x(p.x));
            key_add(snap_y(p.y));
        }
        return cached_geometry(
            [&](ComPtr<ID2D1PathGeometry>&, ID2D1GeometrySink* sink)
            {
                sink->BeginFigure(D2D1::Point2F(snap_x(points.front().x), snap_y(points.front().y)),
                                  filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
                for (std::size_t i = 1; i < points.size(); ++i)
                {
                    sink->AddLine(D2D1::Point2F(snap_x(points[i].x), snap_y(points[i].y)));
                }
                sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
            });
    }

    void push_clip(const Rect& bounds) override
    {
        Pushed entry;
        entry.kind = Pushed::Kind::AxisClip;
        entry.clip = to_d2d(bounds);
        target_->GetTransform(&entry.transform);
        push(entry);
        pushed_.push_back(std::move(entry));
    }

    void pop_clip() override
    {
        // The top entry says which pop matches: PopAxisAlignedClip or push_clip_ellipse()'s PopLayer.
        pop_top();
    }

    void push_clip_ellipse(Point center, float radius_x, float radius_y) override
    {
        Pushed entry;
        entry.kind = Pushed::Kind::GeometryLayer;
        key_begin('e');
        key_add(center.x);
        key_add(center.y);
        key_add(radius_x);
        key_add(radius_y);
        if (auto it = geometries_.find(key_); it != geometries_.end())
        {
            it->second.last_used = frame_;
            entry.mask           = it->second.value;
        }
        else if (factory_)
        {
            ComPtr<ID2D1EllipseGeometry> geometry;
            detail::count(detail::PerfCounter::GeometriesCreated);
            factory_->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(center.x, center.y), radius_x, radius_y), &geometry);
            if (geometry)
            {
                entry.mask = geometry;
                geometries_.emplace(key_, Cached<ComPtr<ID2D1Geometry>>{entry.mask, frame_});
            }
        }
        entry.layer = acquire_layer();
        target_->GetTransform(&entry.transform);
        push(entry);
        pushed_.push_back(std::move(entry));
    }

    void push_clip_rounded_rect(const Rect& bounds, float radius) override
    {
        if (radius <= 0.0f)
        {
            push_clip(bounds);
            return;
        }
        // A layer masked by the rounded rectangle, as push_clip_ellipse(); the radius never
        // exceeds half the shorter side, as a filled rounded rect would draw it.
        const float r = std::min({radius, bounds.width * 0.5f, bounds.height * 0.5f});
        Pushed      entry;
        entry.kind = Pushed::Kind::GeometryLayer;
        key_begin('r');
        key_add(bounds.x);
        key_add(bounds.y);
        key_add(bounds.width);
        key_add(bounds.height);
        key_add(r);
        if (auto it = geometries_.find(key_); it != geometries_.end())
        {
            it->second.last_used = frame_;
            entry.mask           = it->second.value;
        }
        else if (factory_)
        {
            ComPtr<ID2D1RoundedRectangleGeometry> geometry;
            detail::count(detail::PerfCounter::GeometriesCreated);
            factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(to_d2d(bounds), r, r), &geometry);
            if (geometry)
            {
                entry.mask = geometry;
                geometries_.emplace(key_, Cached<ComPtr<ID2D1Geometry>>{entry.mask, frame_});
            }
        }
        entry.layer = acquire_layer();
        target_->GetTransform(&entry.transform);
        push(entry);
        pushed_.push_back(std::move(entry));
    }

    // Opacity 1 pushes no real layer but still records an entry, so the pop stays balanced.
    // Every popup that is not mid-fade comes through here each frame.
    void push_opacity_layer(float opacity) override
    {
        Pushed entry;
        entry.kind    = opacity >= 1.0f ? Pushed::Kind::Nothing : Pushed::Kind::OpacityLayer;
        entry.opacity = std::clamp(opacity, 0.0f, 1.0f);
        if (entry.kind == Pushed::Kind::OpacityLayer)
        {
            entry.layer = acquire_layer();
        }
        target_->GetTransform(&entry.transform);
        push(entry);
        pushed_.push_back(std::move(entry));
    }

    void pop_opacity_layer() override
    {
        pop_top();
    }

    void draw_geometry(const DrawGeometry& g) override
    {
        if (g.geometry.empty())
        {
            return;
        }
        ID2D1Geometry* path = path_geometry(g.geometry);
        if (!path)
        {
            return;
        }
        const Rect bounds  = g.geometry.bounds();
        const bool has_box = g.brush_box.width > 0.0f || g.brush_box.height > 0.0f;
        if (g.fill)
        {
            if (const auto* acrylic = std::get_if<AcrylicBrush>(&*g.fill))
            {
                fill_acrylic(path, bounds, *acrylic);
            }
            else if (const ComPtr<ID2D1Brush> fill = make_brush(*g.fill, has_box ? g.brush_box : bounds))
            {
                target_->FillGeometry(path, fill.Get());
            }
        }
        const float thickness = std::abs(g.stroke_style.thickness);
        if (g.stroke && thickness > 0.0f)
        {
            const ComPtr<ID2D1Brush> stroke = make_brush(*g.stroke, has_box ? g.brush_box : bounds.inflate(thickness * 0.5f));
            ID2D1StrokeStyle* style = stroke_style(g.stroke_style);
            if (stroke)
            {
                target_->DrawGeometry(path, stroke.Get(), thickness, style);
            }
        }
    }

    // Composes onto the current transform, so nested zoomed Scroll controls magnify twice. Snapping
    // stays in untransformed DIPs; a zoomed view is a magnified raster anyway.
    void push_matrix(const Matrix& m) override
    {
        D2D1_MATRIX_3X2_F current{};
        target_->GetTransform(&current);
        transforms_.push_back(current);
        // Matrix and D2D1_MATRIX_3X2_F are the same row-vector layout, so this is a straight copy.
        const D2D1::Matrix3x2F local{m.m11, m.m12, m.m21, m.m22, m.dx, m.dy};
        target_->SetTransform(local * *D2D1::Matrix3x2F::ReinterpretBaseType(&current));
    }

    void pop_transform() override
    {
        if (transforms_.empty())
        {
            return;
        }
        target_->SetTransform(transforms_.back());
        transforms_.pop_back();
    }

    void fill_ellipse(Point center, float radius_x, float radius_y, const Color& color) override
    {
        const D2D1_ELLIPSE e{D2D1::Point2F(snap_x(center.x), snap_y(center.y)), radius_x, radius_y};
        target_->FillEllipse(e, brush(color));
    }

    void stroke_ellipse(Point center, float radius_x, float radius_y, const Color& color, float stroke_width) override
    {
        const D2D1_ELLIPSE e{D2D1::Point2F(snap_x(center.x), snap_y(center.y)), radius_x, radius_y};
        target_->DrawEllipse(e, brush(color), stroke_width);
    }

    // 1x1 fills instead of a bitmap brush: fine for the small dither patches this is for.
    void fill_rect_pattern(const Rect& bounds, Pattern pattern, const Color& fg, const Color& bg) override
    {
        (void)pattern; // only Checkerboard50 exists today
        target_->FillRectangle(to_d2d(bounds), brush(bg));
        ID2D1SolidColorBrush* fg_brush = brush(fg);
        int                   row      = 0;
        for (float y = bounds.top(); y < bounds.bottom(); y += 1.0f, ++row)
        {
            const float x0 = bounds.left() + ((row % 2 == 0) ? 0.0f : 1.0f);
            for (float x = x0; x < bounds.right(); x += 2.0f)
            {
                target_->FillRectangle(D2D1::RectF(x, y, std::min(x + 1.0f, bounds.right()), std::min(y + 1.0f, bounds.bottom())),
                                       fg_brush);
            }
        }
    }

    void set_rasterization(const SetRasterization& policy) override
    {
        target_->SetAntialiasMode(
            policy.geometry == GeometryAA::Aliased ? D2D1_ANTIALIAS_MODE_ALIASED : D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        D2D1_TEXT_ANTIALIAS_MODE text_mode = D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;
        switch (policy.text)
        {
            case TextAA::Aliased:
                text_mode = D2D1_TEXT_ANTIALIAS_MODE_ALIASED;
                break;
            case TextAA::Grayscale:
                text_mode = D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;
                break;
            case TextAA::Subpixel:
                text_mode = D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE;
                break;
        }
        target_->SetTextAntialiasMode(text_mode);
        snap_ = policy.snap_to_device_pixels;
    }

    [[nodiscard]] float device_scale() const override
    {
        return dpi_x_ / 96.0f;
    }

    // libwinui ships no ImageThemePack; null (the default) makes draw_image_nine_slice() a no-op.
    void set_image_pack(const ImageThemePack* pack)
    {
        image_pack_ = pack;
        image_bitmaps_.clear();
    }

    void draw_image_nine_slice(const std::string& image_name, const Rect& bounds, const Thickness& slice_margins) override
    {
        if (!image_pack_)
        {
            return;
        }
        const Bitmap* image = image_pack_->find_image(image_name);
        if (!image || image->empty())
        {
            return;
        }
        ID2D1Bitmap* bmp = image_bitmap(image_name, *image);
        if (!bmp)
        {
            return;
        }

        const float iw = static_cast<float>(image->width());
        const float ih = static_cast<float>(image->height());
        const Rect  db = snap(bounds);

        // Cut at TMT_SIZINGMARGINS: corners 1:1, edges and center stretched along their free axis.
        const float sl = std::min(slice_margins.left, iw);
        const float sr = std::min(slice_margins.right, iw - sl);
        const float st = std::min(slice_margins.top, ih);
        const float sb = std::min(slice_margins.bottom, ih - st);

        const float src_x[4] = {0.0f, sl, iw - sr, iw};
        const float src_y[4] = {0.0f, st, ih - sb, ih};
        const float dst_x[4] = {db.left(), db.left() + sl, db.right() - sr, db.right()};
        const float dst_y[4] = {db.top(), db.top() + st, db.bottom() - sb, db.bottom()};

        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                const D2D1_RECT_F src = D2D1::RectF(src_x[col], src_y[row], src_x[col + 1], src_y[row + 1]);
                const D2D1_RECT_F dst = D2D1::RectF(dst_x[col], dst_y[row], dst_x[col + 1], dst_y[row + 1]);
                if ((dst.right - dst.left) <= 0.0f || (dst.bottom - dst.top) <= 0.0f)
                {
                    continue;
                }
                target_->DrawBitmap(bmp, dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
            }
        }
    }

    // The upload is cached by Bitmap::content_id(), so a picture is copied to the GPU once.
    void draw_image(const Bitmap& image, const Rect& bounds, Stretch stretch) override
    {
        if (image.empty())
        {
            return;
        }
        ID2D1Bitmap* bmp = device_bitmap(image);
        if (!bmp)
        {
            return;
        }

        const float iw = static_cast<float>(image.width());
        const float ih = static_cast<float>(image.height());
        const Rect  db = snap(bounds);

        D2D1_RECT_F src = D2D1::RectF(0.0f, 0.0f, iw, ih);
        D2D1_RECT_F dst = to_d2d(db);

        switch (stretch)
        {
            case Stretch::Fill:
                break;
            case Stretch::None:
            {
                const float dx = db.left() + (db.width - iw) * 0.5f;
                const float dy = db.top() + (db.height - ih) * 0.5f;
                dst            = D2D1::RectF(dx, dy, dx + iw, dy + ih);
                break;
            }
            case Stretch::Uniform:
            {
                const float scale = std::min(db.width / iw, db.height / ih);
                const float w = iw * scale, h = ih * scale;
                const float dx = db.left() + (db.width - w) * 0.5f;
                const float dy = db.top() + (db.height - h) * 0.5f;
                dst            = D2D1::RectF(dx, dy, dx + w, dy + h);
                break;
            }
            case Stretch::UniformToFill:
            {
                // UniformToFill crops the source to the destination's aspect ratio, like PersonPicture does.
                const float scale = std::max(db.width / iw, db.height / ih);
                const float src_w = db.width / scale;
                const float src_h = db.height / scale;
                const float sx    = (iw - src_w) * 0.5f;
                const float sy    = (ih - src_h) * 0.5f;
                src               = D2D1::RectF(sx, sy, sx + src_w, sy + src_h);
                break;
            }
        }

        target_->DrawBitmap(bmp, dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
    }

    // A nine-grid draws the same bitmap nine times; the upload is cached as draw_image()'s is.
    void draw_image_rect(const Bitmap& image, const Rect& source, const Rect& bounds) override
    {
        if (image.empty() || bounds.width <= 0.0f || bounds.height <= 0.0f)
        {
            return;
        }
        const float left   = std::clamp(source.left(), 0.0f, static_cast<float>(image.width()));
        const float top    = std::clamp(source.top(), 0.0f, static_cast<float>(image.height()));
        const float right  = std::clamp(source.right(), left, static_cast<float>(image.width()));
        const float bottom = std::clamp(source.bottom(), top, static_cast<float>(image.height()));
        if (right <= left || bottom <= top)
        {
            return;
        }
        ID2D1Bitmap* bmp = device_bitmap(image);
        if (!bmp)
        {
            return;
        }
        target_->DrawBitmap(bmp, to_d2d(bounds), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, D2D1::RectF(left, top, right, bottom));
    }

    // ID2D1DeviceContext5's SVG renderer (Windows 10 1703+).
    // Parsed per call, since a renderer lives for one frame.
    void draw_svg(const SvgImage& svg, const Rect& bounds) override
    {
        if (svg.empty() || bounds.width <= 0.0f || bounds.height <= 0.0f)
        {
            return;
        }
        ComPtr<ID2D1DeviceContext5> dc;
        if (FAILED(target_->QueryInterface(IID_PPV_ARGS(&dc))) || !dc)
        {
            return;
        }
        const std::string_view markup = svg.markup();
        ComPtr<IStream>        stream;
        stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(markup.data()), static_cast<UINT>(markup.size())));
        if (!stream)
        {
            return;
        }
        const Size               natural  = svg.natural_size();
        const D2D1_SIZE_F        viewport = natural.width > 0.0f && natural.height > 0.0f
                                                ? D2D1::SizeF(natural.width, natural.height)
                                                : D2D1::SizeF(bounds.width, bounds.height);
        ComPtr<ID2D1SvgDocument> document;
        if (FAILED(dc->CreateSvgDocument(stream.Get(), viewport, &document)) || !document)
        {
            return;
        }
        D2D1_MATRIX_3X2_F current{};
        dc->GetTransform(&current);
        dc->SetTransform(
            D2D1::Matrix3x2F::Scale(bounds.width / viewport.width, bounds.height / viewport.height) *
            D2D1::Matrix3x2F::Translation(bounds.x, bounds.y) * *D2D1::Matrix3x2F::ReinterpretBaseType(&current));
        dc->DrawSvgDocument(document.Get());
        dc->SetTransform(current);
    }

    void reset_device_resources()
    {
        brushes_.clear();
        image_bitmaps_.clear();
        device_bitmaps_.clear();
        device_bitmap_bytes_ = 0;
        gradients_.clear();
        layers_.clear();
    }

private:
    static D2D1_RECT_F to_d2d(const Rect& r)
    {
        return D2D1::RectF(r.left(), r.top(), r.right(), r.bottom());
    }
    static D2D1_COLOR_F to_d2d(const Color& c)
    {
        return D2D1::ColorF(c.r, c.g, c.b, c.a);
    }

    // Rounds to a device pixel at the target's DPI, not to a whole DIP. A no-op unless the
    // theme's SetRasterization asked for snap_to_device_pixels.
    [[nodiscard]] float snap_x(float dip) const
    {
        if (!snap_)
        {
            return dip;
        }
        const float scale = dpi_x_ / 96.0f;
        return std::round(dip * scale) / scale;
    }
    [[nodiscard]] float snap_y(float dip) const
    {
        if (!snap_)
        {
            return dip;
        }
        const float scale = dpi_y_ / 96.0f;
        return std::round(dip * scale) / scale;
    }
    struct SnappedStroke
    {
        Rect  path;
        float width = 0.0f;
    };

    // A rectangle's stroke snapped by the two edges of its band, as stroke_line() does: the outer
    // edge to device pixels, the width to whole device pixels (at least one), and the path back to
    // the middle of the band. Snapping the path itself puts a one-pixel stroke on a pixel boundary,
    // where aliased rasterization drops the top-left corner pixel.
    [[nodiscard]] SnappedStroke snap_stroke(const Rect& path, float width) const
    {
        if (!snap_ || width <= 0.0f)
        {
            return {path, width};
        }
        const float scale  = dpi_x_ / 96.0f;
        const float device = std::max(1.0f, std::round(width * scale)) / scale;
        const Rect  outer  = snap(path.inflate(width * 0.5f));
        return {outer.inset(Thickness{device * 0.5f}), device};
    }

    [[nodiscard]] Rect snap(const Rect& r) const
    {
        if (!snap_)
        {
            return r;
        }
        const float left   = snap_x(r.left());
        const float top    = snap_y(r.top());
        const float right  = snap_x(r.right());
        const float bottom = snap_y(r.bottom());
        return Rect{left, top, right - left, bottom - top};
    }

    ID2D1SolidColorBrush* brush(const Color& c)
    {
        const std::uint32_t key = pack(c);
        auto                it  = brushes_.find(key);
        if (it != brushes_.end())
        {
            return it->second.Get();
        }
        ComPtr<ID2D1SolidColorBrush> b;
        detail::count(detail::PerfCounter::BrushesCreated);
        target_->CreateSolidColorBrush(to_d2d(c), &b);
        auto* raw = b.Get();
        brushes_.emplace(key, std::move(b));
        return raw;
    }

    static std::uint32_t pack(const Color& c)
    {
        auto q = [](float v)
        {
            v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
            return static_cast<std::uint32_t>(std::lround(v * 255.0f));
        };
        return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
    }

    // Cached by name for the process; image packs are small and static.
    ID2D1Bitmap* image_bitmap(const std::string& name, const Bitmap& image)
    {
        auto it = image_bitmaps_.find(name);
        if (it != image_bitmaps_.end())
        {
            return it->second.Get();
        }
        ComPtr<ID2D1Bitmap> bmp = upload(image);
        auto*               raw = bmp.Get();
        image_bitmaps_.emplace(name, std::move(bmp));
        return raw;
    }

    // Uploaded once per Bitmap::content_id(), within a byte budget: once the uploads outgrow it,
    // the ones not drawn in this frame go.
    ID2D1Bitmap* device_bitmap(const Bitmap& image)
    {
        const std::uint64_t id = image.content_id();
        if (auto it = device_bitmaps_.find(id); it != device_bitmaps_.end())
        {
            it->second.last_used = frame_;
            return it->second.value.Get();
        }
        ComPtr<ID2D1Bitmap> bmp = upload(image);
        ID2D1Bitmap*        raw = bmp.Get();
        if (raw)
        {
            device_bitmap_bytes_ += image.bytes().size();
            device_bitmaps_.emplace(id, CachedBitmap{std::move(bmp), image.bytes().size(), frame_});
        }
        return raw;
    }

    // A premultiplied BGRA bitmap is the layout the GPU takes and goes up as it is. CreateBitmap
    // rejects D2D1_ALPHA_MODE_STRAIGHT with initial data, so straight RGBA is premultiplied first.
    ComPtr<ID2D1Bitmap> upload(const Bitmap& image)
    {
        std::vector<std::uint8_t> premultiplied;
        const std::uint8_t*       pixels = image.bytes().data();
        DXGI_FORMAT               format = DXGI_FORMAT_B8G8R8A8_UNORM;
        if (image.format() == ui::PixelFormat::Rgba8)
        {
            premultiplied = premultiply_bytes(image);
            pixels        = premultiplied.data();
            format        = DXGI_FORMAT_R8G8B8A8_UNORM;
        }
        ComPtr<ID2D1Bitmap> bmp;
        detail::count(detail::PerfCounter::BitmapsCreated);
        target_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(image.width()), static_cast<UINT32>(image.height())),
                              pixels,
                              static_cast<UINT32>(image.width()) * 4,
                              D2D1::BitmapProperties(D2D1::PixelFormat(format, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                              &bmp);
        return bmp;
    }

    static std::vector<std::uint8_t> premultiply_bytes(const Bitmap& image)
    {
        const auto&               src_bytes = image.bytes();
        std::vector<std::uint8_t> out(src_bytes.size());
        for (std::size_t i = 0; i + 3 < src_bytes.size(); i += 4)
        {
            const std::uint8_t a = src_bytes[i + 3];
            out[i + 0]           = static_cast<std::uint8_t>(src_bytes[i + 0] * a / 255);
            out[i + 1]           = static_cast<std::uint8_t>(src_bytes[i + 1] * a / 255);
            out[i + 2]           = static_cast<std::uint8_t>(src_bytes[i + 2] * a / 255);
            out[i + 3]           = a;
        }
        return out;
    }

    // ---- the push stack ----
    // Every clip and layer pushed this frame, innermost last. CopyFromRenderTarget fails while
    // any is applied, so fill_acrylic() pops them all, copies and pushes them back.
    struct Pushed
    {
        enum class Kind
        {
            AxisClip,
            GeometryLayer,
            OpacityLayer,
            Nothing, // an opacity layer at 1, skipped
        };
        Kind                  kind = Kind::Nothing;
        D2D1_RECT_F           clip{};
        ComPtr<ID2D1Geometry> mask;
        ComPtr<ID2D1Layer>    layer; // from layers_, handed back by the pop
        float                 opacity = 1.0f;
        D2D1_MATRIX_3X2_F     transform{};
    };

    void push(const Pushed& e)
    {
        switch (e.kind)
        {
            case Pushed::Kind::AxisClip:
                target_->PushAxisAlignedClip(e.clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                break;
            case Pushed::Kind::GeometryLayer:
                target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), e.mask.Get()), e.layer.Get());
                break;
            case Pushed::Kind::OpacityLayer:
                target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),
                                                         nullptr,
                                                         D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                         D2D1::IdentityMatrix(),
                                                         e.opacity),
                                   e.layer.Get());
                break;
            case Pushed::Kind::Nothing:
                break;
        }
    }

    // A layer from the pool (made on first need); every push of a layer kind takes one.
    ComPtr<ID2D1Layer> acquire_layer()
    {
        if (!layers_.empty())
        {
            ComPtr<ID2D1Layer> layer = std::move(layers_.back());
            layers_.pop_back();
            return layer;
        }
        ComPtr<ID2D1Layer> layer;
        target_->CreateLayer(nullptr, &layer);
        return layer;
    }

    // Pops the innermost push and gives its layer back to the pool.
    void pop_top()
    {
        if (pushed_.empty())
        {
            return;
        }
        pop(pushed_.back());
        if (pushed_.back().layer)
        {
            layers_.push_back(std::move(pushed_.back().layer));
        }
        pushed_.pop_back();
    }

    void pop(const Pushed& e)
    {
        switch (e.kind)
        {
            case Pushed::Kind::AxisClip:
                target_->PopAxisAlignedClip();
                break;
            case Pushed::Kind::GeometryLayer:
            case Pushed::Kind::OpacityLayer:
                target_->PopLayer();
                break;
            case Pushed::Kind::Nothing:
                break;
        }
    }

    // ---- geometry ----

    ID2D1Geometry* path_geometry(const PathGeometry& g)
    {
        key_begin('g');
        key_add(static_cast<std::uint8_t>(g.fill_rule));
        for (const PathFigure& f : g.figures)
        {
            key_add(f.start);
            key_add(static_cast<std::uint8_t>((f.closed ? 1 : 0) | (f.filled ? 2 : 0)));
            key_add(f.segments.size());
            for (const PathSegment& seg : f.segments)
            {
                key_add(static_cast<std::uint8_t>(seg.kind));
                key_add(seg.p1);
                key_add(seg.p2);
                key_add(seg.p3);
            }
        }
        return cached_geometry(
            [&g](ComPtr<ID2D1PathGeometry>&, ID2D1GeometrySink* sink)
            {
                sink->SetFillMode(g.fill_rule == FillRule::Nonzero ? D2D1_FILL_MODE_WINDING : D2D1_FILL_MODE_ALTERNATE);
                const auto pt = [](Point p) { return D2D1::Point2F(p.x, p.y); };
                for (const PathFigure& f : g.figures)
                {
                    sink->BeginFigure(pt(f.start), f.filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
                    for (const PathSegment& seg : f.segments)
                    {
                        switch (seg.kind)
                        {
                            case PathSegmentKind::Line:
                                sink->AddLine(pt(seg.p1));
                                break;
                            case PathSegmentKind::Quadratic:
                                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pt(seg.p1), pt(seg.p2)));
                                break;
                            case PathSegmentKind::Cubic:
                                sink->AddBezier(D2D1::BezierSegment(pt(seg.p1), pt(seg.p2), pt(seg.p3)));
                                break;
                        }
                    }
                    sink->EndFigure(f.closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
                }
            });
    }

    static D2D1_CAP_STYLE cap(PenLineCap c)
    {
        switch (c)
        {
            case PenLineCap::Square:
                return D2D1_CAP_STYLE_SQUARE;
            case PenLineCap::Round:
                return D2D1_CAP_STYLE_ROUND;
            case PenLineCap::Triangle:
                return D2D1_CAP_STYLE_TRIANGLE;
            case PenLineCap::Flat:
            default:
                return D2D1_CAP_STYLE_FLAT;
        }
    }

    // The stroke style: every stroke property, and a custom dash when there is an array.
    // Cached by those properties.
    ID2D1StrokeStyle* stroke_style(const StrokeStyle& s)
    {
        key_begin('s');
        key_add(static_cast<std::uint8_t>(s.start_cap));
        key_add(static_cast<std::uint8_t>(s.end_cap));
        key_add(static_cast<std::uint8_t>(s.dash_cap));
        key_add(static_cast<std::uint8_t>(s.line_join));
        key_add(s.miter_limit);
        key_add(s.dash_offset);
        for (const float dash : s.dash_array)
        {
            key_add(dash);
        }
        if (auto it = stroke_styles_.find(key_); it != stroke_styles_.end())
        {
            it->second.last_used = frame_;
            return it->second.value.Get();
        }
        if (!factory_)
        {
            return nullptr;
        }
        D2D1_LINE_JOIN join = D2D1_LINE_JOIN_MITER_OR_BEVEL;
        if (s.line_join == PenLineJoin::Bevel)
        {
            join = D2D1_LINE_JOIN_BEVEL;
        }
        else if (s.line_join == PenLineJoin::Round)
        {
            join = D2D1_LINE_JOIN_ROUND;
        }
        const D2D1_STROKE_STYLE_PROPERTIES
            props = D2D1::StrokeStyleProperties(cap(s.start_cap),
                                                cap(s.end_cap),
                                                cap(s.dash_cap),
                                                join,
                                                std::max(1.0f, s.miter_limit),
                                                s.dash_array.empty() ? D2D1_DASH_STYLE_SOLID : D2D1_DASH_STYLE_CUSTOM,
                                                s.dash_offset);
        ComPtr<ID2D1StrokeStyle> style;
        detail::count(detail::PerfCounter::StrokeStylesCreated);
        factory_->CreateStrokeStyle(props,
                                    s.dash_array.empty() ? nullptr : s.dash_array.data(),
                                    static_cast<UINT32>(s.dash_array.size()),
                                    &style);
        ID2D1StrokeStyle* raw = style.Get();
        if (raw)
        {
            stroke_styles_.emplace(key_, Cached<ComPtr<ID2D1StrokeStyle>>{std::move(style), frame_});
        }
        return raw;
    }

    // A gradient's stop collection, and the brushes made from it, cached by the stops and the extend
    // mode. A brush is handed out again with its points (or transform) set for this use; Direct2D
    // takes a brush's state at each draw, so one brush serves every element with the same stops.
    struct Gradient
    {
        ComPtr<ID2D1GradientStopCollection> stops{};
        ComPtr<ID2D1LinearGradientBrush>    linear{};
        ComPtr<ID2D1RadialGradientBrush>    radial{};
        std::uint64_t                       last_used = 0;
    };

    Gradient* gradient(const GradientStops& in, D2D1_EXTEND_MODE extend)
    {
        key_begin('G');
        key_add(static_cast<std::uint8_t>(extend));
        for (const GradientStop& stop : in)
        {
            key_add(stop.offset);
            key_add(stop.color);
        }
        if (auto it = gradients_.find(key_); it != gradients_.end())
        {
            it->second.last_used = frame_;
            return &it->second;
        }
        std::vector<D2D1_GRADIENT_STOP> stops;
        stops.reserve(in.size());
        for (const GradientStop& s : in)
        {
            stops.push_back(D2D1::GradientStop(s.offset, to_d2d(s.color)));
        }
        ComPtr<ID2D1GradientStopCollection> collection;
        target_->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()), D2D1_GAMMA_2_2, extend, &collection);
        if (!collection)
        {
            return nullptr;
        }
        return &gradients_.emplace(key_, Gradient{.stops = std::move(collection), .last_used = frame_}).first->second;
    }

    ID2D1LinearGradientBrush* linear_gradient(const GradientStops& stops, D2D1_EXTEND_MODE extend, D2D1_POINT_2F p0, D2D1_POINT_2F p1)
    {
        Gradient* g = gradient(stops, extend);
        if (!g)
        {
            return nullptr;
        }
        if (!g->linear)
        {
            detail::count(detail::PerfCounter::BrushesCreated);
            target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(p0, p1), g->stops.Get(), &g->linear);
            return g->linear.Get();
        }
        g->linear->SetStartPoint(p0);
        g->linear->SetEndPoint(p1);
        return g->linear.Get();
    }

    ID2D1RadialGradientBrush* radial_gradient(const GradientStops&                         stops,
                                              D2D1_EXTEND_MODE                             extend,
                                              const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& props)
    {
        Gradient* g = gradient(stops, extend);
        if (!g)
        {
            return nullptr;
        }
        if (!g->radial)
        {
            detail::count(detail::PerfCounter::BrushesCreated);
            target_->CreateRadialGradientBrush(props, g->stops.Get(), &g->radial);
            return g->radial.Get();
        }
        g->radial->SetCenter(props.center);
        g->radial->SetGradientOriginOffset(props.gradientOriginOffset);
        g->radial->SetRadiusX(props.radiusX);
        g->radial->SetRadiusY(props.radiusY);
        return g->radial.Get();
    }

    // `box` is what relatively mapped gradients span. An AcrylicBrush here (a stroke) uses its fallback color.
    ComPtr<ID2D1Brush> make_brush(const Brush& b, const Rect& box)
    {
        if (const auto* color = std::get_if<Color>(&b))
        {
            return ComPtr<ID2D1Brush>(brush(*color));
        }
        if (const auto* acrylic = std::get_if<AcrylicBrush>(&b))
        {
            return ComPtr<ID2D1Brush>(brush(acrylic->fallback_color));
        }
        if (const auto* linear = std::get_if<LinearGradientBrush>(&b))
        {
            D2D1_POINT_2F p0 = D2D1::Point2F(box.x, box.top());
            D2D1_POINT_2F p1 = D2D1::Point2F(box.x, box.bottom());
            if (linear->axis == GradientAxis::Horizontal)
            {
                p0 = D2D1::Point2F(box.left(), box.y);
                p1 = D2D1::Point2F(box.right(), box.y);
            }
            else if (linear->axis == GradientAxis::Diagonal)
            {
                p0 = D2D1::Point2F(box.left(), box.top());
                p1 = D2D1::Point2F(box.right(), box.bottom());
            }
            return ComPtr<ID2D1Brush>(linear_gradient(linear->stops, D2D1_EXTEND_MODE_CLAMP, p0, p1));
        }
        const auto& radial = std::get<RadialGradientBrush>(b);
        // The extend mode for each spread method.
        const D2D1_EXTEND_MODE extend = radial.spread_method == GradientSpreadMethod::Reflect  ? D2D1_EXTEND_MODE_MIRROR
                                        : radial.spread_method == GradientSpreadMethod::Repeat ? D2D1_EXTEND_MODE_WRAP
                                                                                               : D2D1_EXTEND_MODE_CLAMP;
        const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES
            props = D2D1::RadialGradientBrushProperties(D2D1::Point2F(radial.center.x, radial.center.y),
                                                        D2D1::Point2F(radial.gradient_origin.x - radial.center.x,
                                                                      radial.gradient_origin.y - radial.center.y),
                                                        radial.radius_x,
                                                        radial.radius_y);
        ID2D1RadialGradientBrush* gradient = radial_gradient(radial.stops, extend, props);
        if (!gradient)
        {
            return nullptr;
        }
        // The brush's points are in its mapping space; one transform takes them to the box.
        if (radial.mapping_mode == BrushMappingMode::RelativeToBoundingBox)
        {
            gradient->SetTransform(
                D2D1::Matrix3x2F::Scale(box.width, box.height) * D2D1::Matrix3x2F::Translation(box.x, box.y));
        }
        else
        {
            gradient->SetTransform(D2D1::Matrix3x2F::Translation(box.x, box.y));
        }
        return gradient;
    }

    // In-app acrylic as an effect chain, without a 2% noise layer.
    void fill_acrylic(ID2D1Geometry* geometry, const Rect& bounds, const AcrylicBrush& a)
    {
        const Color tint = a.effective_tint();
        if (a.always_use_fallback)
        {
            target_->FillGeometry(geometry, brush(a.fallback_color));
            return;
        }
        if (tint.a >= 1.0f)
        {
            target_->FillGeometry(geometry, brush(tint)); // an opaque tint hides everything under it
            return;
        }
        ComPtr<ID2D1DeviceContext> dc;
        if (FAILED(target_->QueryInterface(IID_PPV_ARGS(&dc))) || !dc)
        {
            target_->FillGeometry(geometry, brush(a.fallback_color));
            return;
        }

        constexpr float   Blur = 30.0f; // the acrylic blur radius, a standard deviation
        D2D1_MATRIX_3X2_F current{};
        target_->GetTransform(&current);
        const D2D1::Matrix3x2F m = *D2D1::Matrix3x2F::ReinterpretBaseType(&current);

        // Grown by three standard deviations so the blur near the edges sees real content.
        const D2D1_POINT_2F corners[4] = {m.TransformPoint(D2D1::Point2F(bounds.left(), bounds.top())),
                                          m.TransformPoint(D2D1::Point2F(bounds.right(), bounds.top())),
                                          m.TransformPoint(D2D1::Point2F(bounds.left(), bounds.bottom())),
                                          m.TransformPoint(D2D1::Point2F(bounds.right(), bounds.bottom()))};
        float               left = corners[0].x, top = corners[0].y, right = corners[0].x, bottom = corners[0].y;
        for (const D2D1_POINT_2F& c : corners)
        {
            left   = std::min(left, c.x);
            top    = std::min(top, c.y);
            right  = std::max(right, c.x);
            bottom = std::max(bottom, c.y);
        }
        const float       sx     = dpi_x_ / 96.0f;
        const float       sy     = dpi_y_ / 96.0f;
        const D2D1_SIZE_U pixels = target_->GetPixelSize();
        const auto        clampu = [](float v, UINT32 hi)
        { return static_cast<UINT32>(std::clamp(v, 0.0f, static_cast<float>(hi))); };
        const D2D1_RECT_U src{clampu(std::floor((left - 3.0f * Blur) * sx), pixels.width),
                              clampu(std::floor((top - 3.0f * Blur) * sy), pixels.height),
                              clampu(std::ceil((right + 3.0f * Blur) * sx), pixels.width),
                              clampu(std::ceil((bottom + 3.0f * Blur) * sy), pixels.height)};
        if (src.right <= src.left || src.bottom <= src.top)
        {
            return;
        }

        const D2D1_BITMAP_PROPERTIES1
            props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, target_->GetPixelFormat(), dpi_x_, dpi_y_);
        ComPtr<ID2D1Bitmap1> backdrop;
        detail::count(detail::PerfCounter::BitmapsCreated);
        if (FAILED(dc->CreateBitmap(D2D1::SizeU(src.right - src.left, src.bottom - src.top), nullptr, 0, props, &backdrop)))
        {
            target_->FillGeometry(geometry, brush(a.fallback_color));
            return;
        }
        // CopyFromRenderTarget refuses while a clip or layer is applied: unwind, copy, rewind.
        for (auto it = pushed_.rbegin(); it != pushed_.rend(); ++it)
        {
            pop(*it);
        }
        const D2D1_POINT_2U origin{0, 0};
        const HRESULT       copied = backdrop->CopyFromRenderTarget(&origin, target_, &src);
        for (const Pushed& e : pushed_)
        {
            target_->SetTransform(e.transform);
            push(e);
        }
        target_->SetTransform(current);
        if (FAILED(copied))
        {
            target_->FillGeometry(geometry, brush(a.fallback_color));
            return;
        }

        const auto flood = [&](const Color& c) -> ComPtr<ID2D1Effect>
        {
            ComPtr<ID2D1Effect> e;
            if (SUCCEEDED(dc->CreateEffect(CLSID_D2D1Flood, &e)))
            {
                // The flood color is premultiplied. Straight, a light translucent layer reads as
                // brighter than white, and the luminosity blend's clip turns its hue around.
                e->SetValue(D2D1_FLOOD_PROP_COLOR, D2D1::Vector4F(c.r * c.a, c.g * c.a, c.b * c.a, c.a));
            }
            return e;
        };
        Color opaque_fallback                    = a.fallback_color;
        opaque_fallback.a                        = 1.0f;
        const ComPtr<ID2D1Effect> fallback_flood = flood(opaque_fallback);
        const ComPtr<ID2D1Effect> luminosity     = flood(a.effective_luminosity());
        const ComPtr<ID2D1Effect> tint_flood     = flood(tint);
        ComPtr<ID2D1Effect>       composite, blur, lum_blend, color_blend;
        if (!fallback_flood || !luminosity || !tint_flood || FAILED(dc->CreateEffect(CLSID_D2D1Composite, &composite)) ||
            FAILED(dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur)) ||
            FAILED(dc->CreateEffect(CLSID_D2D1Blend, &lum_blend)) ||
            FAILED(dc->CreateEffect(CLSID_D2D1Blend, &color_blend)))
        {
            target_->FillGeometry(geometry, brush(a.fallback_color));
            return;
        }
        composite->SetInputEffect(0, fallback_flood.Get());
        composite->SetInput(1, backdrop.Get());
        blur->SetInputEffect(0, composite.Get());
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, Blur);
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
        // Input 0 of a blend is the destination (below), input 1 the source laid over it.
        lum_blend->SetInputEffect(0, blur.Get());
        lum_blend->SetInputEffect(1, luminosity.Get());
        lum_blend->SetValue(D2D1_BLEND_PROP_MODE, D2D1_BLEND_MODE_LUMINOSITY);
        color_blend->SetInputEffect(0, lum_blend.Get());
        color_blend->SetInputEffect(1, tint_flood.Get());
        color_blend->SetValue(D2D1_BLEND_PROP_MODE, D2D1_BLEND_MODE_COLOR);

        ComPtr<ID2D1Image> output;
        color_blend->GetOutput(&output);
        // The copy sits at the effect graph's origin; in target DIPs it belongs at the copied
        // rect's top-left. The brush lives in the current user space, so `current` is undone too.
        const float             ox     = static_cast<float>(src.left) / sx;
        const float             oy     = static_cast<float>(src.top) / sy;
        const D2D1_RECT_F       source = D2D1::RectF(0.0f,
                                                     0.0f,
                                                     static_cast<float>(src.right - src.left) / sx,
                                                     static_cast<float>(src.bottom - src.top) / sy);
        ComPtr<ID2D1ImageBrush> image_brush;
        detail::count(detail::PerfCounter::BrushesCreated);
        if (FAILED(dc->CreateImageBrush(output.Get(), D2D1::ImageBrushProperties(source), &image_brush)) || !image_brush)
        {
            target_->FillGeometry(geometry, brush(a.fallback_color));
            return;
        }
        D2D1::Matrix3x2F inverse = m;
        inverse.Invert();
        image_brush->SetTransform(D2D1::Matrix3x2F::Translation(ox, oy) * inverse);
        target_->FillGeometry(geometry, image_brush.Get());
    }

    // ---- cache keys and entries ----
    template <typename Value>
    struct Cached
    {
        Value         value;
        std::uint64_t last_used = 0;
    };
    struct CachedBitmap
    {
        ComPtr<ID2D1Bitmap> value;
        std::size_t         bytes     = 0;
        std::uint64_t       last_used = 0;
    };

    // A cache past this many entries drops those not used in MaxAge frames.
    static constexpr std::size_t   CacheSoftLimit = 256;
    static constexpr std::uint64_t MaxAge         = 60;
    // Uploaded pictures past this many bytes drop those not drawn in the last frame.
    static constexpr std::size_t BitmapBudget = std::size_t{64} << 20;

    template <typename Map>
    void age_out(Map& map)
    {
        if (map.size() > CacheSoftLimit)
        {
            std::erase_if(map, [this](const auto& item) { return item.second.last_used + MaxAge < frame_; });
        }
    }

    // The key of the next cache lookup, built field by field into one reused buffer.
    void key_begin(char kind)
    {
        key_.clear();
        key_.push_back(kind);
    }
    template <typename T>
    void key_add(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        key_.append(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    // The path geometry cached under key_, which the caller has just built, or a new one that
    // `fill` describes into its sink.
    template <typename Fill>
    ID2D1Geometry* cached_geometry(Fill&& fill)
    {
        if (auto it = geometries_.find(key_); it != geometries_.end())
        {
            it->second.last_used = frame_;
            return it->second.value.Get();
        }
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        detail::count(detail::PerfCounter::GeometriesCreated);
        if (!factory_ || FAILED(factory_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink)))
        {
            return nullptr;
        }
        fill(path, sink.Get());
        if (FAILED(sink->Close()))
        {
            return nullptr;
        }
        ID2D1Geometry* raw = path.Get();
        geometries_.emplace(key_, Cached<ComPtr<ID2D1Geometry>>{std::move(path), frame_});
        return raw;
    }

    ID2D1RenderTarget*                                              target_ = nullptr;
    IDWriteFactory*                                                 dwrite_ = nullptr;
    ComPtr<ID2D1Factory>                                            factory_;
    std::unordered_map<std::uint32_t, ComPtr<ID2D1SolidColorBrush>> brushes_;
    float                                                           dpi_x_ = 96.0f;
    float                                                           dpi_y_ = 96.0f;
    bool                                                            snap_  = false;
    std::uint64_t                                                   frame_ = 0;

    const ImageThemePack*                                image_pack_ = nullptr;
    std::unordered_map<std::string, ComPtr<ID2D1Bitmap>> image_bitmaps_;

    std::string                                                       key_;
    std::unordered_map<std::string, Cached<ComPtr<ID2D1Geometry>>>    geometries_;
    std::unordered_map<std::string, Cached<ComPtr<ID2D1StrokeStyle>>> stroke_styles_;
    std::unordered_map<std::string, Gradient>                         gradients_;
    std::unordered_map<std::uint64_t, CachedBitmap>                   device_bitmaps_;
    std::size_t                                                       device_bitmap_bytes_ = 0;
    std::vector<ComPtr<ID2D1Layer>>                                   layers_; // free layers

    // Every clip and layer currently pushed, innermost last.
    std::vector<Pushed> pushed_;

    // The transform each push_transform() replaced, restored by the matching pop.
    std::vector<D2D1_MATRIX_3X2_F> transforms_;
};

} // namespace ui::win32

#endif // _WIN32
