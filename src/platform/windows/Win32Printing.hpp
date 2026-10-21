// SPDX-License-Identifier: MIT
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "DWriteMeasurer.hpp"
#include "winui/ui/Printing.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <windows.h>
// After windows.h, which they need.
#include <d2d1_1.h>
#include <d3d11.h>
#include <documenttarget.h>
#include <dwrite.h>
#include <optional>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstddef>

// What the print dialog (Win32Printing.cpp) and the print dialog with a preview
// (Win32PrintPreview.cpp) share.
namespace ui::detail
{

// The text measurer and theme a job measures with: DirectWrite, as the page is drawn, and light.
struct PrintMeasuring
{
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite;
    std::optional<win32::DWriteMeasurer>   measurer;
    Theme                                  theme = Theme::default_light();

    PrintMeasuring()
    {
        if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                          __uuidof(IDWriteFactory),
                                          reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))))
        {
            measurer.emplace(dwrite.Get());
        }
    }
    [[nodiscard]] PrintContext context() const
    {
        return PrintContext{*measurer, theme};
    }
};

// A Direct2D device to draw pages with: the hardware one, else WARP.
struct PrintDevice
{
    Microsoft::WRL::ComPtr<ID3D11Device>        d3d;
    Microsoft::WRL::ComPtr<ID2D1Factory1>       factory;
    Microsoft::WRL::ComPtr<ID2D1Device>         device;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext>  context;
    Microsoft::WRL::ComPtr<IWICImagingFactory2> wic;

    // False when there is no Direct2D device to be had.
    bool create();
};

// Draws `pages` of `job` on sheets like `page` into the XPS print job `target`, rasterizing what
// can't stay vector at `raster_dpi`.
PrintResult draw_print_job(PrintJob&                       job,
                           const PrintMeasuring&           measuring,
                           PrintDevice&                    device,
                           IPrintDocumentPackageTarget*    target,
                           PrintPage                       page,
                           const std::vector<std::size_t>& pages,
                           float                           raster_dpi);

// A visible, transparent pixel in the middle of the screen, for a print dialog that needs an
// owner when the app has no window. Destroy it with DestroyWindow().
HWND make_print_owner();

// Shows the print dialog with a live preview over `owner`, and prints what the user chose.
// nullopt when the system has none, so the caller falls back to the one without a preview.
std::optional<PrintResult> print_with_preview(HWND owner, PrintJob& job, const PrintMeasuring& measuring);

} // namespace ui::detail
