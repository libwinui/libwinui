// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "DCompCompositor.hpp"

#include "../../render/Compositor.hpp"
#include "D2DRenderer.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/VisualId.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include <cmath>

namespace ui::win32
{
namespace
{
D2D1_COLOR_F to_d2d(const Color& c)
{
    return D2D1::ColorF(c.r, c.g, c.b, c.a);
}

// A straight ramp from `from` to `to` over `seconds`, as one DirectComposition segment.
void add_linear_segment(IDCompositionAnimation& anim, float from, float to, double seconds)
{
    anim.AddCubic(0.0, from, static_cast<float>((to - from) / seconds), 0.0f, 0.0f);
    anim.End(seconds, to);
}
} // namespace

std::unique_ptr<DCompCompositor> DCompCompositor::create(HWND hwnd, UINT dpi)
{
    ComPtr<ID3D11Device> d3d;
    if (FAILED(D3D11CreateDevice(nullptr,
                                 D3D_DRIVER_TYPE_HARDWARE,
                                 nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 nullptr,
                                 0,
                                 D3D11_SDK_VERSION,
                                 &d3d,
                                 nullptr,
                                 nullptr)))
    {
        return nullptr;
    }

    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(d3d.As(&dxgi)))
    {
        return nullptr;
    }

    // A surface's BeginDraw() hands out an ID2D1DeviceContext only when the composition device
    // was made from a Direct2D device; from the bare DXGI device every draw fails.
    ComPtr<ID2D1Device> d2d;
    if (FAILED(D2D1CreateDevice(dxgi.Get(), nullptr, &d2d)))
    {
        return nullptr;
    }

    // IDCompositionDesktopDevice is the one interface with both CreateTargetForHwnd
    // and the CreateVisual/CreateSurface/CreateAnimation calls used here.
    ComPtr<IDCompositionDesktopDevice> device;
    if (FAILED(DCompositionCreateDevice2(d2d.Get(), IID_PPV_ARGS(&device))))
    {
        return nullptr;
    }

    // topmost = TRUE: the overlays sit above the window's Direct2D content.
    ComPtr<IDCompositionTarget> target;
    if (FAILED(device->CreateTargetForHwnd(hwnd, TRUE, &target)))
    {
        return nullptr;
    }

    ComPtr<IDCompositionVisual2> root;
    if (FAILED(device->CreateVisual(&root)) || FAILED(target->SetRoot(root.Get())))
    {
        return nullptr;
    }
    // The layers' parent goes in first, so every overlay added later sits above every layer.
    ComPtr<IDCompositionVisual2> layers_root;
    if (FAILED(device->CreateVisual(&layers_root)) || FAILED(root->AddVisual(layers_root.Get(), TRUE, nullptr)))
    {
        return nullptr;
    }
    device->Commit();

    auto self          = std::unique_ptr<DCompCompositor>(new DCompCompositor());
    self->d3d_device_  = std::move(d3d);
    self->d2d_device_  = std::move(d2d);
    self->device_      = std::move(device);
    self->target_      = std::move(target);
    self->root_        = std::move(root);
    self->layers_root_ = std::move(layers_root);
    self->dpi_         = dpi;
    return self;
}

void DCompCompositor::paint_overlay(Overlay& overlay, int width_px, int height_px, float radius_px, const Color& color)
{
    ComPtr<IDCompositionSurface> surface;
    if (FAILED(device_->CreateSurface(static_cast<UINT>(width_px),
                                      static_cast<UINT>(height_px),
                                      DXGI_FORMAT_B8G8R8A8_UNORM,
                                      DXGI_ALPHA_MODE_PREMULTIPLIED,
                                      &surface)))
    {
        return;
    }

    ComPtr<ID2D1DeviceContext> ctx;
    POINT                      offset{};
    if (FAILED(surface->BeginDraw(nullptr, IID_PPV_ARGS(&ctx), &offset)))
    {
        return;
    }

    ctx->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
    ctx->Clear(D2D1::ColorF(0, 0, 0, 0));

    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(to_d2d(color), &brush);
    const D2D1_ROUNDED_RECT rr{D2D1::RectF(0, 0, static_cast<float>(width_px), static_cast<float>(height_px)),
                               radius_px,
                               radius_px};
    ctx->FillRoundedRectangle(rr, brush.Get());

    surface->EndDraw();
    overlay.surface = std::move(surface);
}

void DCompCompositor::sync_visual(VisualId id, const Rect& bounds, float corner_radius, const Color& color)
{
    if (!device_)
    {
        return;
    }

    Overlay& overlay = overlays_[id];
    if (!overlay.visual)
    {
        ComPtr<IDCompositionVisual2> visual2;
        ComPtr<IDCompositionVisual3> visual3;
        if (FAILED(device_->CreateVisual(&visual2)) || FAILED(visual2.As(&visual3)) ||
            FAILED(root_->AddVisual(visual3.Get(), TRUE, nullptr)))
        {
            overlays_.erase(id);
            return;
        }
        overlay.visual = std::move(visual3);
        overlay.visual->SetOpacity(0.0f);
    }

    const int   width_px  = std::max(1, static_cast<int>(std::lround(to_px(bounds.width))));
    const int   height_px = std::max(1, static_cast<int>(std::lround(to_px(bounds.height))));
    const float radius_px = to_px(corner_radius);

    if (width_px != overlay.width_px || height_px != overlay.height_px || radius_px != overlay.radius_px ||
        !(color == overlay.color))
    {
        paint_overlay(overlay, width_px, height_px, radius_px, color);
        overlay.visual->SetContent(overlay.surface.Get());
        overlay.width_px  = width_px;
        overlay.height_px = height_px;
        overlay.radius_px = radius_px;
        overlay.color     = color;
    }

    overlay.visual->SetOffsetX(to_px(bounds.left()));
    overlay.visual->SetOffsetY(to_px(bounds.top()));
    device_->Commit();
}

void DCompCompositor::animate_opacity(VisualId id, float from, float to, Duration duration)
{
    auto it = overlays_.find(id);
    if (it == overlays_.end() || !it->second.visual || !device_)
    {
        return;
    }

    ComPtr<IDCompositionAnimation> animation;
    if (FAILED(device_->CreateAnimation(&animation)))
    {
        return;
    }

    const double seconds = std::max(0.001, duration.count() / 1000.0);
    add_linear_segment(*animation.Get(), from, to, seconds);

    it->second.visual->SetOpacity(animation.Get());
    device_->Commit();
}

void DCompCompositor::remove_visual(VisualId id)
{
    auto it = overlays_.find(id);
    if (it == overlays_.end())
    {
        return;
    }
    if (root_ && it->second.visual)
    {
        root_->RemoveVisual(it->second.visual.Get());
    }
    overlays_.erase(it);
    if (device_)
    {
        device_->Commit();
    }
}

// ---- layers ----

namespace
{
// How far a layer's surface reaches past its bounds, for anti-aliasing.
constexpr int LayerPadPx = 2;
// An animated clip edge nobody animates stays this far out.
constexpr float OpenClip = 1.0e6f;

Duration clock_now()
{
    return std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now().time_since_epoch());
}

// A straight piece of an animation's cycle, in milliseconds from the cycle's start.
struct Piece
{
    double t0 = 0.0;
    double t1 = 0.0;
    float  v0 = 0.0f;
    float  v1 = 0.0f;

    [[nodiscard]] float at(double t) const
    {
        return t1 > t0 ? v0 + (v1 - v0) * static_cast<float>((t - t0) / (t1 - t0)) : v1;
    }
};

// One cycle of `animation` as straight pieces: a curve becomes 16 of them, a discrete frame a hold
// and a jump, and the cycle ends on a hold of the last value.
std::vector<Piece> pieces_of(const LayerAnimation& animation, double cycle)
{
    constexpr int      CurvePieces = 16;
    std::vector<Piece> out;
    const auto&        frames = animation.key_frames;
    float              value  = frames.front().value;
    double             time   = 0.0;
    for (const KeyFrame& frame : frames)
    {
        const double end = std::min(frame.time.count(), cycle);
        if (end > time)
        {
            switch (frame.kind)
            {
                case KeyFrameKind::Linear:
                    out.push_back({time, end, value, frame.value});
                    break;
                case KeyFrameKind::Discrete:
                    out.push_back({time, end, value, value});
                    break;
                case KeyFrameKind::Spline:
                case KeyFrameKind::Easing:
                {
                    float previous = value;
                    for (int i = 1; i <= CurvePieces; ++i)
                    {
                        const double t = time + (end - time) * i / CurvePieces;
                        const float next = i == CurvePieces ? frame.value
                                                            : sample_key_frames(frames, frames.front().value, Duration{t});
                        out.push_back({time + (end - time) * (i - 1) / CurvePieces, t, previous, next});
                        previous = next;
                    }
                    break;
                }
            }
            time = end;
        }
        value = frame.value;
    }
    if (cycle > time)
    {
        out.push_back({time, cycle, value, value});
    }
    return out;
}

// Adds `pieces` from `from` on, starting at `offset` seconds, with every value mapped to
// `scale * v + bias`.
void add_pieces(IDCompositionAnimation& animation, const std::vector<Piece>& pieces, double from, double offset, float scale, float bias)
{
    for (const Piece& piece : pieces)
    {
        if (piece.t1 <= from)
        {
            continue;
        }
        const double start = std::max(piece.t0, from);
        const double slope = piece.t1 > piece.t0 ? (piece.v1 - piece.v0) / (piece.t1 - piece.t0) * 1000.0 : 0.0;
        animation.AddCubic(offset + (start - from) / 1000.0,
                           scale * piece.at(start) + bias,
                           static_cast<float>(slope) * scale,
                           0.0f,
                           0.0f);
    }
}
} // namespace

Duration DCompCompositor::now() const
{
    return clock_now();
}

void DCompCompositor::set_dpi(UINT dpi)
{
    if (dpi == dpi_)
    {
        return;
    }
    dpi_ = dpi;
    for (auto& [id, layer] : layers_)
    {
        if (place_layer(layer))
        {
            restart_animations(layer);
        }
    }
    if (device_ && !layers_.empty())
    {
        device_->Commit();
    }
}

bool DCompCompositor::place_layer(Layer& layer)
{
    const float scale     = static_cast<float>(dpi_) / 96.0f;
    const Rect& b         = layer.placement.bounds;
    const float left      = b.x * scale;
    const float top       = b.y * scale;
    const int   origin_x  = static_cast<int>(std::floor(left)) - LayerPadPx;
    const int   origin_y  = static_cast<int>(std::floor(top)) - LayerPadPx;
    const int   width_px  = std::max(1, static_cast<int>(std::ceil(left + b.width * scale)) + LayerPadPx - origin_x);
    const int   height_px = std::max(1, static_cast<int>(std::ceil(top + b.height * scale)) + LayerPadPx - origin_y);
    const float inset_x   = left - static_cast<float>(origin_x);
    const float inset_y   = top - static_cast<float>(origin_y);

    layer.frame_visual->SetOffsetX(static_cast<float>(origin_x));
    layer.frame_visual->SetOffsetY(static_cast<float>(origin_y));
    layer.origin_x  = origin_x;
    layer.origin_y  = origin_y;
    const Rect clip = layer.placement.clip.value_or(b);
    layer.clip_visual->SetClip(
        D2D1::RectF(clip.left() * scale, clip.top() * scale, clip.right() * scale, clip.bottom() * scale));

    // The same content lands on the same pixels after a move by whole pixels, so only a new size,
    // a new fraction of a pixel or a new DPI draws it again.
    if (width_px == layer.width_px && height_px == layer.height_px && inset_x == layer.inset_x &&
        inset_y == layer.inset_y && dpi_ == layer.painted_dpi)
    {
        return false;
    }
    layer.width_px  = width_px;
    layer.height_px = height_px;
    layer.inset_x   = inset_x;
    layer.inset_y   = inset_y;
    draw_layer(layer);
    return true;
}

void DCompCompositor::draw_layer(Layer& layer)
{
    if (!dwrite_ && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                               __uuidof(IDWriteFactory),
                                               reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
    {
        return;
    }
    ComPtr<IDCompositionSurface> surface;
    if (FAILED(device_->CreateSurface(static_cast<UINT>(layer.width_px),
                                      static_cast<UINT>(layer.height_px),
                                      DXGI_FORMAT_B8G8R8A8_UNORM,
                                      DXGI_ALPHA_MODE_PREMULTIPLIED,
                                      &surface)))
    {
        return;
    }
    ComPtr<ID2D1DeviceContext> ctx;
    POINT                      offset{};
    if (FAILED(surface->BeginDraw(nullptr, IID_PPV_ARGS(&ctx), &offset)))
    {
        return;
    }
    const float scale = static_cast<float>(dpi_) / 96.0f;
    const float dpi   = static_cast<float>(dpi_);
    ctx->SetDpi(dpi, dpi);
    ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    // The surface may be part of a larger one: clear and draw only its own pixels.
    const float x0 = static_cast<float>(offset.x) / scale;
    const float y0 = static_cast<float>(offset.y) / scale;
    ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    ctx->PushAxisAlignedClip(D2D1::RectF(x0,
                                         y0,
                                         x0 + static_cast<float>(layer.width_px) / scale,
                                         y0 + static_cast<float>(layer.height_px) / scale),
                             D2D1_ANTIALIAS_MODE_ALIASED);
    ctx->Clear(D2D1::ColorF(0, 0, 0, 0));
    ctx->SetTransform(D2D1::Matrix3x2F::Translation(x0 + layer.inset_x / scale, y0 + layer.inset_y / scale));
    {
        D2DRenderer renderer{ctx.Get(), dwrite_.Get()};
        renderer.begin_frame();
        layer.content.replay(renderer);
    }
    ctx->PopAxisAlignedClip();
    surface->EndDraw();

    layer.surface     = std::move(surface);
    layer.painted_dpi = dpi_;
    layer.content_visual->SetContent(layer.surface.Get());
}

void DCompCompositor::sync_layer(VisualId id, const detail::LayerPlacement& placement)
{
    if (!device_)
    {
        return;
    }
    Layer& layer = layers_[id];
    if (!layer.clip_visual)
    {
        ComPtr<IDCompositionVisual2>   clip_visual, frame_visual, content2;
        ComPtr<IDCompositionVisual3>   content_visual;
        ComPtr<IDCompositionTransform> group;
        if (FAILED(device_->CreateVisual(&clip_visual)) || FAILED(device_->CreateVisual(&frame_visual)) ||
            FAILED(device_->CreateVisual(&content2)) || FAILED(content2.As(&content_visual)) ||
            FAILED(device_->CreateRectangleClip(&layer.own_clip)) ||
            FAILED(device_->CreateScaleTransform(&layer.scale)) || FAILED(device_->CreateRotateTransform(&layer.rotate)) ||
            FAILED(device_->CreateTranslateTransform(&layer.translate)))
        {
            layers_.erase(id);
            return;
        }
        // Scale, then rotation, then translation, as an element's own visual transform.
        IDCompositionTransform* parts[3] = {layer.scale.Get(), layer.rotate.Get(), layer.translate.Get()};
        if (FAILED(device_->CreateTransformGroup(parts, 3, &group)) ||
            FAILED(frame_visual->AddVisual(content_visual.Get(), TRUE, nullptr)) ||
            FAILED(clip_visual->AddVisual(frame_visual.Get(), TRUE, nullptr)) ||
            FAILED(layers_root_->AddVisual(clip_visual.Get(), TRUE, nullptr)))
        {
            layers_.erase(id);
            return;
        }
        layer.own_clip->SetLeft(-OpenClip);
        layer.own_clip->SetTop(-OpenClip);
        layer.own_clip->SetRight(OpenClip);
        layer.own_clip->SetBottom(OpenClip);
        frame_visual->SetClip(layer.own_clip.Get());
        content_visual->SetTransform(group.Get());
        layer.clip_visual    = std::move(clip_visual);
        layer.frame_visual   = std::move(frame_visual);
        layer.content_visual = std::move(content_visual);
    }
    layer.placement = placement;
    if (layer.painted_dpi != 0 && place_layer(layer))
    {
        restart_animations(layer);
    }
    device_->Commit();
}

void DCompCompositor::paint_layer(VisualId id, const RenderRecording& content)
{
    auto it = layers_.find(id);
    if (it == layers_.end() || !device_)
    {
        return;
    }
    Layer& layer = it->second;
    layer.content.clear();
    content.replay(layer.content);
    if (place_layer(layer))
    {
        restart_animations(layer); // the first paint, or a move since the last
    }
    else
    {
        draw_layer(layer);
    }
    device_->Commit();
}

void DCompCompositor::run_animation(Layer& layer, const LayerAnimation& animation)
{
    if (animation.key_frames.empty())
    {
        return;
    }
    ComPtr<IDCompositionAnimation> function;
    if (FAILED(device_->CreateAnimation(&function)))
    {
        return;
    }
    const float scale = static_cast<float>(dpi_) / 96.0f;
    float       a     = 1.0f; // values to pixels: a * value + b
    float       b     = 0.0f;
    switch (animation.property)
    {
        case LayerProperty::OffsetX:
        case LayerProperty::OffsetY:
            a = scale;
            break;
        case LayerProperty::ClipLeft:
        case LayerProperty::ClipRight:
            a = scale;
            b = layer.inset_x;
            break;
        case LayerProperty::ClipTop:
        case LayerProperty::ClipBottom:
            a = scale;
            b = layer.inset_y;
            break;
        case LayerProperty::ScaleX:
        case LayerProperty::ScaleY:
        case LayerProperty::Rotation:
        case LayerProperty::Opacity:
            break;
    }

    const double cycle = animation.cycle().count();
    if (cycle <= 0.0)
    {
        function->AddCubic(0.0, a * animation.key_frames.back().value + b, 0.0f, 0.0f, 0.0f);
    }
    else
    {
        const std::vector<Piece> pieces = pieces_of(animation, cycle);
        if (animation.repeat)
        {
            // The rest of the cycle it starts in, one whole cycle, then that cycle over and over.
            const double phase = std::fmod(std::max(0.0, animation.phase.count()), cycle);
            add_pieces(*function.Get(), pieces, phase, 0.0, a, b);
            const double whole = (cycle - phase) / 1000.0;
            if (phase > 0.0)
            {
                add_pieces(*function.Get(), pieces, 0.0, whole, a, b);
            }
            function->AddRepeat(phase > 0.0 ? whole + cycle / 1000.0 : cycle / 1000.0, cycle / 1000.0);
        }
        else
        {
            const double phase = std::max(0.0, animation.phase.count());
            if (phase < cycle)
            {
                add_pieces(*function.Get(), pieces, phase, 0.0, a, b);
            }
            function->AddCubic(std::max(0.0, cycle - phase) / 1000.0, a * pieces.back().v1 + b, 0.0f, 0.0f, 0.0f);
        }
    }

    const float cx = animation.center.x * scale + layer.inset_x;
    const float cy = animation.center.y * scale + layer.inset_y;
    switch (animation.property)
    {
        case LayerProperty::OffsetX:
            layer.translate->SetOffsetX(function.Get());
            break;
        case LayerProperty::OffsetY:
            layer.translate->SetOffsetY(function.Get());
            break;
        case LayerProperty::ScaleX:
            layer.scale->SetCenterX(cx);
            layer.scale->SetCenterY(cy);
            layer.scale->SetScaleX(function.Get());
            break;
        case LayerProperty::ScaleY:
            layer.scale->SetCenterX(cx);
            layer.scale->SetCenterY(cy);
            layer.scale->SetScaleY(function.Get());
            break;
        case LayerProperty::Rotation:
            layer.rotate->SetCenterX(cx);
            layer.rotate->SetCenterY(cy);
            layer.rotate->SetAngle(function.Get());
            break;
        case LayerProperty::Opacity:
            layer.content_visual->SetOpacity(function.Get());
            break;
        case LayerProperty::ClipLeft:
            layer.own_clip->SetLeft(function.Get());
            break;
        case LayerProperty::ClipTop:
            layer.own_clip->SetTop(function.Get());
            break;
        case LayerProperty::ClipRight:
            layer.own_clip->SetRight(function.Get());
            break;
        case LayerProperty::ClipBottom:
            layer.own_clip->SetBottom(function.Get());
            break;
    }
}

void DCompCompositor::restart_animations(Layer& layer)
{
    const auto now = std::chrono::steady_clock::now();
    for (Layer::Running& running : layer.running)
    {
        running.animation.phase += std::chrono::duration_cast<Duration>(now - running.started);
        running.started = now;
        run_animation(layer, running.animation);
    }
}

void DCompCompositor::animate_layer(VisualId id, const LayerAnimation& animation)
{
    auto it = layers_.find(id);
    if (it == layers_.end() || !device_)
    {
        return;
    }
    Layer& layer = it->second;
    std::erase_if(layer.running, [&](const Layer::Running& r) { return r.animation.property == animation.property; });
    layer.running.push_back({animation, std::chrono::steady_clock::now()});
    run_animation(layer, animation);
    device_->Commit();
}

void DCompCompositor::stop_layer_animations(VisualId id)
{
    auto it = layers_.find(id);
    if (it == layers_.end() || !device_)
    {
        return;
    }
    Layer& layer = it->second;
    layer.running.clear();
    layer.translate->SetOffsetX(0.0f);
    layer.translate->SetOffsetY(0.0f);
    layer.scale->SetScaleX(1.0f);
    layer.scale->SetScaleY(1.0f);
    layer.rotate->SetAngle(0.0f);
    layer.content_visual->SetOpacity(1.0f);
    layer.own_clip->SetLeft(-OpenClip);
    layer.own_clip->SetTop(-OpenClip);
    layer.own_clip->SetRight(OpenClip);
    layer.own_clip->SetBottom(OpenClip);
    device_->Commit();
}

void DCompCompositor::remove_layer(VisualId id)
{
    auto it = layers_.find(id);
    if (it == layers_.end())
    {
        return;
    }
    if (layers_root_ && it->second.clip_visual)
    {
        layers_root_->RemoveVisual(it->second.clip_visual.Get());
    }
    layers_.erase(it);
    if (device_)
    {
        device_->Commit();
    }
}

// ---- LazyCompositor ----

DCompCompositor* LazyCompositor::device()
{
    if (!tried_)
    {
        tried_ = true;
        real_  = DCompCompositor::create(hwnd_, dpi_);
        if (real_)
        {
            for (const auto& [id, visual] : pending_)
            {
                real_->sync_visual(id, visual.bounds, visual.corner_radius, visual.color);
            }
        }
        pending_.clear();
    }
    return real_.get();
}

void LazyCompositor::sync_visual(VisualId id, const Rect& bounds, float corner_radius, const Color& color)
{
    if (tried_)
    {
        if (real_)
        {
            real_->sync_visual(id, bounds, corner_radius, color);
        }
        return;
    }
    pending_[id] = Pending{bounds, corner_radius, color};
}

void LazyCompositor::animate_opacity(VisualId id, float from, float to, Duration duration)
{
    if (DCompCompositor* c = device())
    {
        c->animate_opacity(id, from, to, duration);
    }
}

void LazyCompositor::remove_visual(VisualId id)
{
    if (real_)
    {
        real_->remove_visual(id);
    }
    pending_.erase(id);
}

void LazyCompositor::sync_layer(VisualId id, const detail::LayerPlacement& placement)
{
    if (DCompCompositor* c = device())
    {
        c->sync_layer(id, placement);
    }
}

void LazyCompositor::paint_layer(VisualId id, const RenderRecording& content)
{
    if (DCompCompositor* c = device())
    {
        c->paint_layer(id, content);
    }
}

void LazyCompositor::animate_layer(VisualId id, const LayerAnimation& animation)
{
    if (DCompCompositor* c = device())
    {
        c->animate_layer(id, animation);
    }
}

void LazyCompositor::stop_layer_animations(VisualId id)
{
    if (real_)
    {
        real_->stop_layer_animations(id);
    }
}

void LazyCompositor::remove_layer(VisualId id)
{
    if (real_)
    {
        real_->remove_layer(id);
    }
}

Duration LazyCompositor::now() const
{
    return clock_now();
}

void LazyCompositor::set_dpi(UINT dpi)
{
    dpi_ = dpi;
    if (real_)
    {
        real_->set_dpi(dpi);
    }
}

} // namespace ui::win32

#endif // _WIN32
