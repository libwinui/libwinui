// SPDX-License-Identifier: MIT
// Printing: the classic print dialog (PrintDlgEx) picks the printer, the pages, the copies and the
// paper; each page is then drawn by the same D2DRenderer the window uses, into a Direct2D command
// list, and Direct2D's print control turns the pages into an XPS print job. Text stays text and
// shapes stay vectors on paper.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Win32Printing.hpp"

#include "../Backend.hpp"
#include "D2DRenderer.hpp"
#include "DWriteMeasurer.hpp"
#include "Win32Apartment.hpp"
#include "Win32String.hpp"
#include "winui/ui/Printing.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <windows.h>
// After windows.h, which they need.
#include <algorithm>
#include <array>
#include <d2d1_1.h>
#include <d3d11.h>
#include <documenttarget.h>
#include <dwrite.h>
#include <dxgi.h>
#include <optional>
#include <prntvpt.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <wincodec.h>
#include <winspool.h>
#include <wrl/client.h>

#include <commdlg.h>
#include <cstddef>

namespace ui::detail
{
namespace
{
using Microsoft::WRL::ComPtr;

// A global memory handle locked for the length of a scope.
template <typename T>
class Locked
{
public:
    explicit Locked(HGLOBAL handle) :
        handle_(handle),
        data_(handle != nullptr ? static_cast<T*>(GlobalLock(handle)) : nullptr)
    {
    }
    ~Locked()
    {
        if (data_ != nullptr)
        {
            GlobalUnlock(handle_);
        }
    }
    Locked(const Locked&)                     = delete;
    Locked&          operator=(const Locked&) = delete;
    [[nodiscard]] T* get() const
    {
        return data_;
    }

private:
    HGLOBAL handle_;
    T*      data_;
};

// What the dialog (or the named printer) settled: which printer, its settings and its paper.
struct PrinterChoice
{
    std::wstring           name;
    std::vector<std::byte> devmode{}; // the DEVMODEW with its driver part, as the dialog left it
    PrintPage              page{};
};

// The sheet and the printable area of a printer DC, in DIPs.
PrintPage page_of(HDC dc)
{
    const float dpi_x = static_cast<float>(std::max(1, GetDeviceCaps(dc, LOGPIXELSX)));
    const float dpi_y = static_cast<float>(std::max(1, GetDeviceCaps(dc, LOGPIXELSY)));
    const float to_x  = 96.0f / dpi_x;
    const float to_y  = 96.0f / dpi_y;
    const Size  sheet{static_cast<float>(GetDeviceCaps(dc, PHYSICALWIDTH)) * to_x,
                      static_cast<float>(GetDeviceCaps(dc, PHYSICALHEIGHT)) * to_y};
    const Rect  printable{static_cast<float>(GetDeviceCaps(dc, PHYSICALOFFSETX)) * to_x,
                          static_cast<float>(GetDeviceCaps(dc, PHYSICALOFFSETY)) * to_y,
                          static_cast<float>(GetDeviceCaps(dc, HORZRES)) * to_x,
                          static_cast<float>(GetDeviceCaps(dc, VERTRES)) * to_y};
    return PrintPage{.index = 0, .size = sheet, .printable = printable};
}

std::vector<std::byte> copy_devmode(HGLOBAL handle)
{
    const Locked<DEVMODEW> mode{handle};
    if (mode.get() == nullptr)
    {
        return {};
    }
    const std::size_t size = mode.get()->dmSize + mode.get()->dmDriverExtra;
    const auto*       from = reinterpret_cast<const std::byte*>(mode.get());
    return {from, from + size};
}

std::wstring printer_name(HGLOBAL names)
{
    const Locked<DEVNAMES> dn{names};
    if (dn.get() == nullptr)
    {
        return {};
    }
    return reinterpret_cast<const wchar_t*>(dn.get()) + dn.get()->wDeviceOffset;
}

// A printer named by the app: its default settings and paper, without a dialog.
std::optional<PrinterChoice> named_printer(const std::wstring& name)
{
    HANDLE printer = nullptr;
    if (!OpenPrinterW(const_cast<wchar_t*>(name.c_str()), &printer, nullptr))
    {
        return std::nullopt;
    }
    PrinterChoice choice{.name = name};
    const LONG    size = DocumentPropertiesW(nullptr, printer, const_cast<wchar_t*>(name.c_str()), nullptr, nullptr, 0);
    if (size > 0)
    {
        choice.devmode.resize(static_cast<std::size_t>(size));
        auto* mode = reinterpret_cast<DEVMODEW*>(choice.devmode.data());
        if (DocumentPropertiesW(nullptr, printer, const_cast<wchar_t*>(name.c_str()), mode, nullptr, DM_OUT_BUFFER) != IDOK)
        {
            choice.devmode.clear();
        }
    }
    ClosePrinter(printer);
    const auto* mode = choice.devmode.empty() ? nullptr : reinterpret_cast<const DEVMODEW*>(choice.devmode.data());
    const HDC   dc   = CreateDCW(nullptr, name.c_str(), nullptr, mode);
    if (dc == nullptr)
    {
        return std::nullopt;
    }
    choice.page = page_of(dc);
    DeleteDC(dc);
    return choice;
}

// Shows PrintDlgEx with the document's page range; the choice, the pages and how it ended.
PrintResult ask(HWND owner, PrintJob& job, const PrintMeasuring& measuring, PrinterChoice& choice, std::vector<std::size_t>& pages)
{
    // The default printer first, for the page count the dialog's range shows.
    PRINTDLGEXW defaults{};
    defaults.lStructSize = sizeof(defaults);
    defaults.hwndOwner   = owner;
    defaults.Flags       = PD_RETURNDEFAULT | PD_RETURNDC;
    defaults.nStartPage  = START_PAGE_GENERAL;
    std::size_t count    = 0;
    if (SUCCEEDED(PrintDlgExW(&defaults)) && defaults.hDC != nullptr)
    {
        count = job.page_count(page_of(defaults.hDC), measuring.context());
        DeleteDC(defaults.hDC);
    }

    std::array<PRINTPAGERANGE, 16> ranges{};
    PRINTDLGEXW                    dialog{};
    dialog.lStructSize    = sizeof(dialog);
    dialog.hwndOwner      = owner;
    dialog.hDevMode       = defaults.hDevMode; // the default printer's, preselected
    dialog.hDevNames      = defaults.hDevNames;
    dialog.Flags          = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_NOSELECTION | PD_NOCURRENTPAGE;
    dialog.nPageRanges    = 0;
    dialog.nMaxPageRanges = static_cast<DWORD>(ranges.size());
    dialog.lpPageRanges   = ranges.data();
    dialog.nMinPage       = 1;
    dialog.nMaxPage       = static_cast<DWORD>(std::max<std::size_t>(count, 1));
    dialog.nCopies        = 1;
    dialog.nStartPage     = START_PAGE_GENERAL;
    const HRESULT hr      = PrintDlgExW(&dialog);
    const auto    release = [&dialog]
    {
        if (dialog.hDC != nullptr)
        {
            DeleteDC(dialog.hDC);
        }
        if (dialog.hDevMode != nullptr)
        {
            GlobalFree(dialog.hDevMode);
        }
        if (dialog.hDevNames != nullptr)
        {
            GlobalFree(dialog.hDevNames);
        }
    };
    if (FAILED(hr))
    {
        release();
        return PrintResult::Failed;
    }
    if (dialog.dwResultAction != PD_RESULT_PRINT || dialog.hDC == nullptr)
    {
        release();
        return PrintResult::Canceled;
    }
    choice.name    = printer_name(dialog.hDevNames);
    choice.devmode = copy_devmode(dialog.hDevMode);
    choice.page    = page_of(dialog.hDC);
    release();

    // The paper may have changed with the printer or its settings.
    count = job.page_count(choice.page, measuring.context());
    if ((dialog.Flags & PD_PAGENUMS) != 0)
    {
        for (DWORD r = 0; r < dialog.nPageRanges; ++r)
        {
            for (DWORD p = ranges[r].nFromPage; p <= ranges[r].nToPage && p >= 1 && p <= count; ++p)
            {
                pages.push_back(p - 1);
            }
        }
    }
    else
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            pages.push_back(i);
        }
    }
    return pages.empty() ? PrintResult::Canceled : PrintResult::Printed;
}

// The print ticket a job carries: the printer's settings (copies, paper, orientation, duplex).
ComPtr<IStream> print_ticket(const PrinterChoice& choice)
{
    if (choice.devmode.empty())
    {
        return nullptr;
    }
    HPTPROVIDER provider = nullptr;
    if (FAILED(PTOpenProvider(choice.name.c_str(), 1, &provider)))
    {
        return nullptr;
    }
    ComPtr<IStream> ticket;
    if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &ticket)))
    {
        const auto* mode = reinterpret_cast<const DEVMODEW*>(choice.devmode.data());
        if (FAILED(PTConvertDevModeToPrintTicket(provider,
                                                 static_cast<ULONG>(choice.devmode.size()),
                                                 const_cast<DEVMODEW*>(mode),
                                                 kPTJobScope,
                                                 ticket.Get())))
        {
            ticket.Reset();
        }
        else
        {
            const LARGE_INTEGER start{};
            ticket->Seek(start, STREAM_SEEK_SET, nullptr);
        }
    }
    PTCloseProvider(provider);
    return ticket;
}

// Draws `pages` and hands them to the printer named in `choice` through Direct2D's print control.
PrintResult spool(PrintJob&                       job,
                  const PrintMeasuring&           measuring,
                  const PrinterChoice&            choice,
                  const std::vector<std::size_t>& pages)
{
    PrintDevice device;
    if (!device.create())
    {
        return PrintResult::Failed;
    }
    // The XPS print job: to the printer, or into the output file when there is one.
    ComPtr<IPrintDocumentPackageTargetFactory> targets;
    if (FAILED(CoCreateInstance(__uuidof(PrintDocumentPackageTargetFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&targets))))
    {
        return PrintResult::Failed;
    }
    ComPtr<IStream> output;
    if (job.output_file &&
        FAILED(SHCreateStreamOnFileEx(job.output_file->c_str(),
                                      STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
                                      FILE_ATTRIBUTE_NORMAL,
                                      TRUE,
                                      nullptr,
                                      &output)))
    {
        return PrintResult::Failed;
    }
    const ComPtr<IStream>               ticket = print_ticket(choice);
    const std::wstring                  title = job.title.empty() ? std::wstring(L"Document") : win32::widen(job.title);
    ComPtr<IPrintDocumentPackageTarget> target;
    if (FAILED(
            targets->CreateDocumentPackageTargetForPrintJob(choice.name.c_str(), title.c_str(), output.Get(), ticket.Get(), &target)))
    {
        return PrintResult::Failed;
    }
    return draw_print_job(job, measuring, device, target.Get(), choice.page, pages, 150.0f);
}

} // namespace

bool PrintDevice::create()
{
    for (const D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP})
    {
        if (SUCCEEDED(
                D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, nullptr)))
        {
            break;
        }
    }
    ComPtr<IDXGIDevice> dxgi;
    return d3d != nullptr && SUCCEEDED(d3d.As(&dxgi)) &&
           SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())) &&
           SUCCEEDED(factory->CreateDevice(dxgi.Get(), &device)) &&
           SUCCEEDED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context)) &&
           SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
}

PrintResult draw_print_job(PrintJob&                       job,
                           const PrintMeasuring&           measuring,
                           PrintDevice&                    device,
                           IPrintDocumentPackageTarget*    target,
                           PrintPage                       page,
                           const std::vector<std::size_t>& pages,
                           float                           raster_dpi)
{
    ComPtr<ID2D1PrintControl> control;
    const D2D1_PRINT_CONTROL_PROPERTIES properties{D2D1_PRINT_FONT_SUBSET_MODE_DEFAULT, raster_dpi, D2D1_COLOR_SPACE_SRGB};
    if (FAILED(device.device->CreatePrintControl(device.wic.Get(), target, &properties, &control)))
    {
        return PrintResult::Failed;
    }
    ID2D1DeviceContext* const context = device.context.Get();
    for (const std::size_t index : pages)
    {
        page.index = index;
        ComPtr<ID2D1CommandList> commands;
        if (FAILED(context->CreateCommandList(&commands)))
        {
            return PrintResult::Failed;
        }
        context->SetTarget(commands.Get());
        context->BeginDraw();
        {
            win32::D2DRenderer renderer(context, measuring.dwrite.Get());
            renderer.begin_frame();
            job.draw_page(renderer, page, measuring.context());
        }
        const HRESULT drawn = context->EndDraw();
        context->SetTarget(nullptr);
        if (FAILED(drawn) || FAILED(commands->Close()) ||
            FAILED(control->AddPage(commands.Get(), D2D1::SizeF(page.size.width, page.size.height), nullptr, nullptr, nullptr)))
        {
            return PrintResult::Failed;
        }
    }
    return SUCCEEDED(control->Close()) ? PrintResult::Printed : PrintResult::Failed;
}

HWND make_print_owner()
{
    const int  x     = GetSystemMetrics(SM_CXSCREEN) / 2;
    const int  y     = GetSystemMetrics(SM_CYSCREEN) / 2;
    const HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                                       L"STATIC",
                                       L"",
                                       WS_POPUP,
                                       x,
                                       y,
                                       1,
                                       1,
                                       nullptr,
                                       nullptr,
                                       GetModuleHandleW(nullptr),
                                       nullptr);
    if (owner != nullptr)
    {
        SetLayeredWindowAttributes(owner, 0, 0, LWA_ALPHA);
        ShowWindow(owner, SW_SHOW);
    }
    return owner;
}

PrintResult platform_print(const Window* owner, PrintJob& job)
{
    (void)win32::enter_ole_apartment(); // the print dialog and the XPS print API are COM
    const PrintMeasuring measuring;
    if (!measuring.measurer)
    {
        return PrintResult::Failed;
    }
    PrinterChoice            choice;
    std::vector<std::size_t> pages;
    if (job.printer)
    {
        const std::optional<PrinterChoice> named = named_printer(win32::widen(*job.printer));
        if (!named)
        {
            return PrintResult::Failed;
        }
        choice                  = *named;
        const std::size_t count = job.page_count(choice.page, measuring.context());
        for (std::size_t i = 0; i < count; ++i)
        {
            pages.push_back(i);
        }
        if (pages.empty())
        {
            return PrintResult::Canceled;
        }
    }
    else
    {
        // The dialogs need an owner, and the one Windows 11 shows a visible one: without a shown
        // window, a transparent pixel in the middle of the screen stands in.
        HWND       owner_window = owner != nullptr ? static_cast<HWND>(owner->native_handle()) : nullptr;
        const HWND stand_in     = owner_window == nullptr ? make_print_owner() : nullptr;
        if (stand_in != nullptr)
        {
            owner_window = stand_in;
        }
        std::optional<PrintResult> result;
        if (job.dialog == PrintDialog::Preview && !job.output_file)
        {
            result = print_with_preview(owner_window, job, measuring);
        }
        if (!result)
        {
            result = ask(owner_window, job, measuring, choice, pages);
            if (*result == PrintResult::Printed)
            {
                result = spool(job, measuring, choice, pages);
            }
        }
        if (stand_in != nullptr)
        {
            DestroyWindow(stand_in);
        }
        return *result;
    }
    return spool(job, measuring, choice, pages);
}

} // namespace ui::detail
