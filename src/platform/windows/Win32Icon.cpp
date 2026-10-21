// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "Win32Icon.hpp"

#include "../../ui/WindowHost.hpp"
#include "Win32String.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Bitmap.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <vector>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace ui::win32
{
namespace
{

using Microsoft::WRL::ComPtr;

// A top-down 32bpp DIB and the pixels behind it, BGRA.
struct Dib
{
    HBITMAP       bitmap = nullptr;
    std::uint8_t* bits   = nullptr;
    int           size   = 0;
};

Dib create_dib(int size)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth       = size;
    info.bmiHeader.biHeight      = -size; // top-down
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;
    Dib   dib;
    void* bits = nullptr;
    dib.bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    dib.bits   = static_cast<std::uint8_t*>(bits);
    dib.size   = size;
    return dib;
}

// Bilinear resample, fitted Uniform and centered. Filtered on premultiplied values, or a
// transparent pixel's arbitrary color bleeds into its neighbors' edges.
void resample_image(const Bitmap& source, std::uint8_t* out, int size)
{
    const Bitmap image  = source.converted(PixelFormat::Rgba8);
    const float  scale  = std::min(static_cast<float>(size) / static_cast<float>(image.width()),
                                   static_cast<float>(size) / static_cast<float>(image.height()));
    const float  draw_w = static_cast<float>(image.width()) * scale;
    const float  draw_h = static_cast<float>(image.height()) * scale;
    const float  off_x  = (static_cast<float>(size) - draw_w) * 0.5f;
    const float  off_y  = (static_cast<float>(size) - draw_h) * 0.5f;

    const auto& px     = image.bytes();
    const auto  sample = [&](int x, int y, int channel)
    {
        x                   = std::clamp(x, 0, image.width() - 1);
        y                   = std::clamp(y, 0, image.height() - 1);
        const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width()) + x) * 4;
        const float       a = px[i + 3] / 255.0f;
        return channel == 3 ? a : (px[i + channel] / 255.0f) * a;
    };

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            std::uint8_t* d = out + (static_cast<std::size_t>(y) * size + x) * 4;
            const float   u = (static_cast<float>(x) + 0.5f - off_x) / scale - 0.5f;
            const float   v = (static_cast<float>(y) + 0.5f - off_y) / scale - 0.5f;
            if (u < -0.5f || v < -0.5f || u > static_cast<float>(image.width()) - 0.5f ||
                v > static_cast<float>(image.height()) - 0.5f)
            {
                std::memset(d, 0, 4);
                continue;
            }
            const int   x0 = static_cast<int>(std::floor(u));
            const int   y0 = static_cast<int>(std::floor(v));
            const float fx = u - static_cast<float>(x0);
            const float fy = v - static_cast<float>(y0);
            float       rgba[4]{};
            for (int c = 0; c < 4; ++c)
            {
                const float top    = sample(x0, y0, c) * (1.0f - fx) + sample(x0 + 1, y0, c) * fx;
                const float bottom = sample(x0, y0 + 1, c) * (1.0f - fx) + sample(x0 + 1, y0 + 1, c) * fx;
                rgba[c]            = top * (1.0f - fy) + bottom * fy;
            }
            const float a    = rgba[3];
            const auto  byte = [](float value)
            { return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
            const auto unmult = [a](float value) { return a > 0.0f ? value / a : 0.0f; };
            d[0]              = byte(unmult(rgba[2]));
            d[1]              = byte(unmult(rgba[1]));
            d[2]              = byte(unmult(rgba[0]));
            d[3]              = byte(a);
        }
    }
}

// D2D writes premultiplied pixels; an icon's color bitmap carries straight alpha.
bool render_glyph(const WindowIcon& icon, Dib& dib, ID2D1Factory* d2d, IDWriteFactory* dwrite)
{
    if (d2d == nullptr || dwrite == nullptr)
    {
        return false;
    }
    std::memset(dib.bits, 0, static_cast<std::size_t>(dib.size) * dib.size * 4);

    const HDC screen = GetDC(nullptr);
    const HDC dc     = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    const HGDIOBJ previous = SelectObject(dc, dib.bitmap);

    bool ok = false;
    // 96 DPI: one DIP per pixel, so every size below is in the icon's own pixels.
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                    96.0f,
                                                    96.0f);
    ComPtr<ID2D1DCRenderTarget> target;
    if (SUCCEEDED(d2d->CreateDCRenderTarget(&props, &target)))
    {
        RECT bounds{0, 0, dib.size, dib.size};
        target->BindDC(dc, &bounds);
        // Nothing under it is opaque, and ClearType needs to know what it blends against.
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

        const float extent       = static_cast<float>(dib.size);
        const bool  plate        = icon.background.a > 0.0f;
        float       glyph_extent = extent;
        if (plate)
        {
            ComPtr<ID2D1SolidColorBrush> fill;
            const Color&                 bg = icon.background;
            target->CreateSolidColorBrush(D2D1::ColorF(bg.r, bg.g, bg.b, bg.a), &fill);
            const float radius = extent * 0.2f;
            target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, extent, extent), radius, radius),
                                         fill.Get());
            glyph_extent = extent * 0.625f; // leave the plate a margin around the mark
        }

        ComPtr<IDWriteTextFormat> format;
        const std::wstring        family = widen(
            !icon.glyph.font_family.empty() ? icon.glyph.font_family.view() : std::string_view{"Segoe Fluent Icons"});
        if (SUCCEEDED(dwrite->CreateTextFormat(family.c_str(),
                                               nullptr,
                                               DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL,
                                               DWRITE_FONT_STRETCH_NORMAL,
                                               glyph_extent,
                                               L"",
                                               &format)))
        {
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            ComPtr<ID2D1SolidColorBrush> ink;
            const Color fg = icon.foreground.value_or(taskbar_uses_light_theme() ? colors::Black : colors::White);
            target->CreateSolidColorBrush(D2D1::ColorF(fg.r, fg.g, fg.b, fg.a), &ink);
            const std::wstring glyph = widen(encode_utf8(icon.glyph.codepoint)); // a surrogate pair past the BMP
            target->DrawText(glyph.c_str(),
                             static_cast<UINT32>(glyph.size()),
                             format.Get(),
                             D2D1::RectF(0.0f, 0.0f, extent, extent),
                             ink.Get());
        }
        ok = SUCCEEDED(target->EndDraw());
    }

    SelectObject(dc, previous);
    DeleteDC(dc);
    if (!ok)
    {
        return false;
    }

    for (int i = 0; i < dib.size * dib.size; ++i)
    {
        std::uint8_t* p = dib.bits + static_cast<std::size_t>(i) * 4;
        const int     a = p[3];
        if (a > 0 && a < 255)
        {
            for (int c = 0; c < 3; ++c)
            {
                p[c] = static_cast<std::uint8_t>(std::min(255, (p[c] * 255 + a / 2) / a));
            }
        }
    }
    return true;
}

} // namespace

bool taskbar_uses_light_theme()
{
    DWORD value = 0;
    DWORD size  = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"SystemUsesLightTheme",
                        RRF_RT_REG_DWORD,
                        nullptr,
                        &value,
                        &size) == ERROR_SUCCESS &&
           value != 0;
}

HICON load_executable_icon(int size_px)
{
    // The first RT_GROUP_ICON in resource order, the icon Explorer shows for the file.
    HMODULE module = GetModuleHandleW(nullptr);
    LPWSTR  first  = nullptr;
    EnumResourceNamesW(
        module,
        RT_GROUP_ICON,
        [](HMODULE, LPCWSTR, LPWSTR name, LONG_PTR found) -> BOOL
        {
            *reinterpret_cast<LPWSTR*>(found) = name; // valid while the module is loaded
            return FALSE;                             // the first is enough
        },
        reinterpret_cast<LONG_PTR>(&first));
    if (first == nullptr || size_px <= 0)
    {
        return nullptr;
    }
    return static_cast<HICON>(LoadImageW(module, first, IMAGE_ICON, size_px, size_px, LR_DEFAULTCOLOR));
}

HICON create_icon(const WindowIcon& icon, int size_px, ID2D1Factory* d2d, IDWriteFactory* dwrite)
{
    if (icon.empty() || size_px <= 0)
    {
        return nullptr;
    }
    if (icon.image.empty() && icon.glyph.codepoint == 0)
    {
        // LoadImageW reads "#101" as resource number 101, like FindResourceW.
        const std::wstring name = widen(icon.resource);
        return static_cast<HICON>(
            LoadImageW(GetModuleHandleW(nullptr), name.c_str(), IMAGE_ICON, size_px, size_px, LR_DEFAULTCOLOR));
    }
    Dib color = create_dib(size_px);
    if (color.bitmap == nullptr)
    {
        return nullptr;
    }

    bool ok = true;
    if (!icon.image.empty())
    {
        resample_image(icon.image, color.bits, size_px);
    }
    else
    {
        ok = render_glyph(icon, color, d2d, dwrite);
    }

    HICON result = nullptr;
    if (ok)
    {
        // A 32bpp color bitmap carries its own alpha, but the mask must exist: all zeros, explicit
        // because CreateBitmap leaves null bits undefined (rows padded to 16 bits).
        const std::vector<std::uint8_t> zeros(static_cast<std::size_t>((size_px + 15) / 16 * 2) * size_px, 0);
        const HBITMAP                   mask = CreateBitmap(size_px, size_px, 1, 1, zeros.data());
        ICONINFO                        info{TRUE, 0, 0, mask, color.bitmap};
        result = CreateIconIndirect(&info);
        DeleteObject(mask);
    }
    DeleteObject(color.bitmap);
    return result;
}

Bitmap icon_pixels(HICON icon)
{
    ICONINFO info{};
    if (icon == nullptr || !GetIconInfo(icon, &info))
    {
        return {};
    }
    Bitmap out;
    BITMAP color{};
    if (info.hbmColor != nullptr && GetObjectW(info.hbmColor, sizeof(color), &color) != 0)
    {
        const int  w = color.bmWidth;
        const int  h = color.bmHeight;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h; // top-down
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        std::vector<std::uint8_t> bgra(static_cast<std::size_t>(w) * h * 4);
        std::vector<std::uint8_t> mask(bgra.size());
        const HDC                 dc = GetDC(nullptr);
        const bool ok = GetDIBits(dc, info.hbmColor, 0, static_cast<UINT>(h), bgra.data(), &bi, DIB_RGB_COLORS) == h &&
                        GetDIBits(dc, info.hbmMask, 0, static_cast<UINT>(h), mask.data(), &bi, DIB_RGB_COLORS) == h;
        ReleaseDC(nullptr, dc);
        if (ok)
        {
            bool has_alpha = false;
            for (std::size_t i = 3; i < bgra.size() && !has_alpha; i += 4)
            {
                has_alpha = bgra[i] != 0;
            }
            out                        = Bitmap{w, h};
            std::span<std::uint8_t> px = out.bytes();
            for (std::size_t i = 0; i < bgra.size(); i += 4)
            {
                px[i]     = bgra[i + 2];
                px[i + 1] = bgra[i + 1];
                px[i + 2] = bgra[i];
                // A set mask bit is a transparent pixel.
                px[i + 3] = has_alpha ? bgra[i + 3] : (mask[i] != 0 ? 0 : 255);
            }
        }
    }
    if (info.hbmColor != nullptr)
    {
        DeleteObject(info.hbmColor);
    }
    if (info.hbmMask != nullptr)
    {
        DeleteObject(info.hbmMask);
    }
    return out;
}

} // namespace ui::win32

#endif // _WIN32
