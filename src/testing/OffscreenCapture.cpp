// SPDX-License-Identifier: MIT
// OffscreenCapture: the backend's Direct2D renderer drawing into a WIC bitmap on Windows, and an
// empty capture where there is no backend.
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "winui/testing/OffscreenCapture.hpp"

#include "platform/windows/D2DRenderer.hpp"
#include "platform/windows/DWriteMeasurer.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Window.hpp"

#include <algorithm>
#include <d2d1.h>
#include <dwrite.h>
#include <memory>
#include <vector>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ui::win32
{
namespace
{
using Microsoft::WRL::ComPtr;

std::uint8_t unpremultiply(std::uint8_t channel, std::uint8_t alpha)
{
    if (alpha == 0 || alpha == 255)
    {
        return channel;
    }
    const int v = (static_cast<int>(channel) * 255 + alpha / 2) / alpha;
    return static_cast<std::uint8_t>(std::min(v, 255));
}
} // namespace
} // namespace ui::win32

namespace ui::testing
{
using namespace ui::win32;

struct OffscreenCapture::Impl
{
    ComPtr<ID2D1Factory>            d2d;
    ComPtr<IDWriteFactory>          dwrite;
    ComPtr<IWICImagingFactory>      wic;
    std::unique_ptr<DWriteMeasurer> measurer;
    // The last capture's target, kept for the next one of the same size so the renderer's caches
    // carry over the way a window's do from frame to frame.
    ComPtr<IWICBitmap>           bitmap;
    ComPtr<ID2D1RenderTarget>    target;
    std::unique_ptr<D2DRenderer> renderer;
    int                          width           = 0;
    int                          height          = 0;
    bool                         com_initialized = false;
    bool                         ok              = false;
};

OffscreenCapture::OffscreenCapture() : impl_(std::make_unique<Impl>())
{
    // WIC needs COM. Track whether *we* initialized it so we only uninitialize our own.
    const HRESULT co       = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    impl_->com_initialized = (co == S_OK || co == S_FALSE);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, impl_->d2d.GetAddressOf())))
    {
        return;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(impl_->dwrite.GetAddressOf()))))
    {
        return;
    }
    if (FAILED(
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(impl_->wic.GetAddressOf()))))
    {
        return;
    }
    impl_->measurer = std::make_unique<DWriteMeasurer>(impl_->dwrite.Get());
    impl_->ok       = true;
}

OffscreenCapture::~OffscreenCapture()
{
    if (!impl_)
    {
        return;
    }
    // Release every COM interface before CoUninitialize; releasing a WIC or Direct2D object
    // after that crashes.
    const bool uninitialize = impl_->com_initialized;
    impl_->renderer.reset();
    impl_->target.Reset();
    impl_->bitmap.Reset();
    impl_->measurer.reset();
    impl_->wic.Reset();
    impl_->dwrite.Reset();
    impl_->d2d.Reset();
    if (uninitialize)
    {
        CoUninitialize();
    }
}

bool OffscreenCapture::ok() const
{
    return impl_ && impl_->ok;
}

const TextMeasurer& OffscreenCapture::measurer() const
{
    return *impl_->measurer;
}

Bitmap OffscreenCapture::capture(const Window& window, Size client, const Color& background)
{
    return draw(window, client, background, [](bool) { return std::vector<Rect>{Rect{}}; });
}

Bitmap OffscreenCapture::capture_changes(Window& window, Size client, const Color& background)
{
    return draw(window,
                client,
                background,
                [&window](bool fresh)
                {
                    std::vector<Rect> damage = window.take_damage();
                    if (fresh)
                    {
                        damage.assign(1, Rect{});
                    }
                    return damage;
                });
}

template <typename Areas>
Bitmap OffscreenCapture::draw(const Window& window, Size client, const Color& background, Areas&& areas_for)
{
    if (!ok())
    {
        return {};
    }

    const int w = std::max(1, static_cast<int>(std::lround(client.width)));
    const int h = std::max(1, static_cast<int>(std::lround(client.height)));

    const bool fresh = !impl_->target || impl_->width != w || impl_->height != h;
    if (fresh)
    {
        impl_->renderer.reset();
        impl_->target.Reset();
        impl_->bitmap.Reset();
        if (FAILED(impl_->wic->CreateBitmap(static_cast<UINT>(w),
                                            static_cast<UINT>(h),
                                            GUID_WICPixelFormat32bppPBGRA,
                                            WICBitmapCacheOnLoad,
                                            &impl_->bitmap)))
        {
            return {};
        }
        // Software render target, 96 DPI so DIPs map 1:1 to pixels.
        const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                          D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                        96.0f,
                                                        96.0f);
        if (FAILED(impl_->d2d->CreateWicBitmapRenderTarget(impl_->bitmap.Get(), props, &impl_->target)))
        {
            return {};
        }
        impl_->renderer = std::make_unique<D2DRenderer>(impl_->target.Get(), impl_->dwrite.Get());
        impl_->width    = w;
        impl_->height   = h;
    }
    ID2D1RenderTarget*      rt     = impl_->target.Get();
    IWICBitmap*             bitmap = impl_->bitmap.Get();
    const std::vector<Rect> areas  = areas_for(fresh);

    // Grayscale AA: deterministic offscreen text, independent of ClearType/display.
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    rt->BeginDraw();
    rt->SetTransform(D2D1::IdentityMatrix());
    impl_->renderer->begin_frame();
    // Each area is cleared and drawn under a clip of it, the way the window host repaints; an
    // empty rect stands for all of it.
    for (const Rect& area : areas)
    {
        const bool all = area.width <= 0.0f || area.height <= 0.0f;
        // Whole pixels (96 DPI), as the window host repaints, so the clear and the redraw cover
        // the same pixels and an edge never blends with the last frame's.
        const float left = std::floor(area.left());
        const float top  = std::floor(area.top());
        const Rect  clip{left, top, std::ceil(area.right()) - left, std::ceil(area.bottom()) - top};
        if (!all)
        {
            rt->PushAxisAlignedClip(D2D1::RectF(clip.left(), clip.top(), clip.right(), clip.bottom()),
                                    D2D1_ANTIALIAS_MODE_ALIASED);
        }
        rt->Clear(D2D1::ColorF(background.r, background.g, background.b, background.a));
        if (all)
        {
            window.render(*impl_->renderer);
        }
        else
        {
            // Only the clear is clipped here: Window::render() clips through the renderer, which
            // lifts its clips while in-app acrylic copies what is under it.
            rt->PopAxisAlignedClip();
            window.render(*impl_->renderer, clip);
        }
    }
    if (FAILED(rt->EndDraw()))
    {
        impl_->renderer.reset();
        impl_->target.Reset();
        return {};
    }

    Bitmap                 img(w, h);
    const WICRect          rect{0, 0, w, h};
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(bitmap->Lock(&rect, WICBitmapLockRead, &lock)))
    {
        return img;
    }

    UINT  stride      = 0;
    UINT  buffer_size = 0;
    BYTE* data        = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&buffer_size, &data)) || !data)
    {
        return img;
    }

    auto px = img.bytes();
    for (int y = 0; y < h; ++y)
    {
        const BYTE* row = data + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < w; ++x)
        {
            const BYTE b = row[x * 4 + 0];
            const BYTE g = row[x * 4 + 1];
            const BYTE r = row[x * 4 + 2];
            const BYTE a = row[x * 4 + 3];

            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            px[i + 0]           = unpremultiply(r, a);
            px[i + 1]           = unpremultiply(g, a);
            px[i + 2]           = unpremultiply(b, a);
            px[i + 3]           = a;
        }
    }
    return img;
}

} // namespace ui::testing

#else // _WIN32

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/OffscreenCapture.hpp"
#include "winui/ui/Window.hpp"

#include <memory>

namespace ui::testing
{

// Nothing to draw with: ok() is false and a capture is an empty bitmap.
struct OffscreenCapture::Impl
{
    HeadlessTextMeasurer measurer;
};

OffscreenCapture::OffscreenCapture() : impl_(std::make_unique<Impl>())
{
}

OffscreenCapture::~OffscreenCapture() = default;

bool OffscreenCapture::ok() const
{
    return false;
}

const TextMeasurer& OffscreenCapture::measurer() const
{
    return impl_->measurer;
}

Bitmap OffscreenCapture::capture(const Window& /*window*/, Size /*client*/, const Color& /*background*/)
{
    return {};
}

Bitmap OffscreenCapture::capture_changes(Window& /*window*/, Size /*client*/, const Color& /*background*/)
{
    return {};
}

} // namespace ui::testing

#endif // _WIN32
