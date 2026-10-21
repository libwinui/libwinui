// SPDX-License-Identifier: MIT
// The print dialog with a live preview: Windows.Graphics.Printing's PrintManager for the window,
// fed by a document source. The system asks the source, on threads of its own, for the page count
// (Paginate), for preview pages drawn into DXGI surfaces (MakePage) and finally for the document
// (MakeDocument), which goes through the same Direct2D print control as the classic dialog. Every
// one of those calls runs on the UI thread, which print() keeps pumping meanwhile, since the job's
// callbacks are app code.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "D2DRenderer.hpp"
#include "Win32Printing.hpp"
#include "Win32String.hpp"
#include "winui/ui/Printing.hpp"

#include <windows.h>
// After windows.h, which they need.
#include <algorithm>
#include <atomic>
#include <deque>
#include <documentsource.h>
#include <dxgi.h>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <printmanagerinterop.h>
#include <printpreview.h>
#include <vector>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Printing.h>

#include <cmath>
#include <cstddef>

namespace ui::detail
{
namespace
{
using Microsoft::WRL::ComPtr;
namespace printing = winrt::Windows::Graphics::Printing;

// Runs work on the UI thread from the system's threads, while the UI thread waits in pump().
class UiThread
{
public:
    UiThread() : id_(GetCurrentThreadId()), wake_(CreateEventW(nullptr, FALSE, FALSE, nullptr))
    {
    }
    ~UiThread()
    {
        CloseHandle(wake_);
    }
    UiThread(const UiThread&)            = delete;
    UiThread& operator=(const UiThread&) = delete;

    // Runs `work` on the UI thread and returns once it ran.
    void run(const std::function<void()>& work)
    {
        if (GetCurrentThreadId() == id_)
        {
            work();
            return;
        }
        const HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        {
            const std::lock_guard lock{mutex_};
            queue_.emplace_back(
                [&work, done]
                {
                    work();
                    SetEvent(done);
                });
        }
        SetEvent(wake_);
        WaitForSingleObject(done, INFINITE);
        CloseHandle(done);
    }

    // Handles messages and queued work until `finished` turns true.
    void pump(const std::atomic<bool>& finished)
    {
        while (!finished.load())
        {
            const DWORD woke = MsgWaitForMultipleObjectsEx(1, &wake_, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (woke == WAIT_OBJECT_0)
            {
                drain();
            }
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    PostQuitMessage(static_cast<int>(msg.wParam)); // for the app's own loop
                    return;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            drain();
        }
    }

private:
    void drain()
    {
        for (;;)
        {
            std::function<void()> work;
            {
                const std::lock_guard lock{mutex_};
                if (queue_.empty())
                {
                    return;
                }
                work = std::move(queue_.front());
                queue_.pop_front();
            }
            work();
        }
    }

    DWORD                             id_;
    HANDLE                            wake_;
    std::mutex                        mutex_;
    std::deque<std::function<void()>> queue_;
};

PrintPage page_of(const printing::PrintPageDescription& description)
{
    return PrintPage{.index     = 0,
                     .size      = Size{description.PageSize.Width, description.PageSize.Height},
                     .printable = Rect{description.ImageableRect.X,
                                       description.ImageableRect.Y,
                                       description.ImageableRect.Width,
                                       description.ImageableRect.Height}};
}

// What the system asks the document for: its page count, preview pages and the document itself.
struct DocumentSource :
    winrt::implements<DocumentSource, printing::IPrintDocumentSource, ::IPrintDocumentPageSource, ::IPrintPreviewPageCollection>
{
    DocumentSource(PrintJob& job, const PrintMeasuring& measuring, PrintDevice& device, UiThread& ui) :
        job_(job),
        measuring_(measuring),
        device_(device),
        ui_(ui)
    {
    }

    // print() returned: the job and the UI thread's loop are gone.
    void close()
    {
        closed_.store(true);
    }

    // ---- IPrintDocumentPageSource ----
    HRESULT STDMETHODCALLTYPE GetPreviewPageCollection(IPrintDocumentPackageTarget*  target,
                                                       IPrintPreviewPageCollection** pages) noexcept override
    {
        if (closed_.load())
        {
            return E_ABORT;
        }
        if (target == nullptr || pages == nullptr)
        {
            return E_INVALIDARG;
        }
        const HRESULT hr = target->GetPackageTarget(ID_PREVIEWPACKAGETARGET_DXGI, IID_PPV_ARGS(&preview_));
        if (FAILED(hr))
        {
            return hr;
        }
        *pages = static_cast<IPrintPreviewPageCollection*>(this);
        (*pages)->AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE MakeDocument(::IInspectable* options, IPrintDocumentPackageTarget* target) noexcept override
    {
        if (closed_.load())
        {
            return E_ABORT;
        }
        if (options == nullptr || target == nullptr)
        {
            return E_INVALIDARG;
        }
        try
        {
            printing::PrintTaskOptions task{nullptr};
            winrt::copy_from_abi(task, options);
            const printing::PrintPageDescription description = task.GetPageDescription(1);
            const PrintPage                      page        = page_of(description);
            const float                          raster_dpi  = static_cast<float>(
                std::max<std::uint32_t>(96, std::min(description.DpiX, description.DpiY)));
            PrintResult result = PrintResult::Failed;
            ui_.run(
                [&]
                {
                    const std::size_t        count = job_.page_count(page, measuring_.context());
                    std::vector<std::size_t> pages;
                    // The pages the user typed into the range box, else every one.
                    for (const printing::PrintPageRange& range : task.CustomPageRanges())
                    {
                        for (int p = range.FirstPageNumber(); p <= range.LastPageNumber(); ++p)
                        {
                            if (p >= 1 && static_cast<std::size_t>(p) <= count)
                            {
                                pages.push_back(static_cast<std::size_t>(p - 1));
                            }
                        }
                    }
                    if (pages.empty())
                    {
                        for (std::size_t i = 0; i < count; ++i)
                        {
                            pages.push_back(i);
                        }
                    }
                    result = draw_print_job(job_, measuring_, device_, target, page, pages, raster_dpi);
                });
            return result == PrintResult::Printed ? S_OK : E_FAIL;
        } catch (...)
        {
            return winrt::to_hresult();
        }
    }

    // ---- IPrintPreviewPageCollection ----
    HRESULT STDMETHODCALLTYPE Paginate(UINT32 current_job_page, ::IInspectable* options) noexcept override
    {
        if (closed_.load())
        {
            return E_ABORT;
        }
        if (options == nullptr || preview_ == nullptr)
        {
            return E_INVALIDARG;
        }
        try
        {
            printing::PrintTaskOptions task{nullptr};
            winrt::copy_from_abi(task, options);
            const PrintPage page  = page_of(task.GetPageDescription(current_job_page));
            std::size_t     count = 0;
            ui_.run([&] { count = job_.page_count(page, measuring_.context()); });
            page_      = page;
            count_     = count;
            HRESULT hr = preview_->InvalidatePreview();
            if (SUCCEEDED(hr))
            {
                hr = preview_->SetJobPageCount(PageCountType::FinalPageCount, static_cast<UINT32>(count));
            }
            return hr;
        } catch (...)
        {
            return winrt::to_hresult();
        }
    }

    // Draws page `desired` scaled into a `width` x `height` DIP surface for the preview pane.
    HRESULT STDMETHODCALLTYPE MakePage(UINT32 desired, FLOAT width, FLOAT height) noexcept override
    {
        if (closed_.load())
        {
            return E_ABORT;
        }
        if (width <= 0.0f || height <= 0.0f || preview_ == nullptr || page_.size.width <= 0.0f)
        {
            return E_INVALIDARG;
        }
        if (desired == JOB_PAGE_APPLICATION_DEFINED)
        {
            desired = 1;
        }
        if (desired < 1 || desired > count_)
        {
            return S_OK;
        }
        HRESULT hr = S_OK;
        ui_.run([&] { hr = draw_preview(desired, width, height); });
        return hr;
    }

private:
    HRESULT draw_preview(UINT32 desired, float width, float height)
    {
        const CD3D11_TEXTURE2D_DESC texture_desc(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                 static_cast<UINT>(std::ceil(width)),
                                                 static_cast<UINT>(std::ceil(height)),
                                                 1,
                                                 1,
                                                 D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11Texture2D>     texture;
        ComPtr<IDXGISurface>        surface;
        ComPtr<ID2D1DeviceContext>  context;
        ComPtr<ID2D1Bitmap1>        bitmap;
        HRESULT                     hr = device_.d3d->CreateTexture2D(&texture_desc, nullptr, &texture);
        if (SUCCEEDED(hr))
        {
            hr = texture.As(&surface);
        }
        if (SUCCEEDED(hr))
        {
            hr = device_.device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context);
        }
        if (SUCCEEDED(hr))
        {
            const D2D1_BITMAP_PROPERTIES1
                properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                     D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                       D2D1_ALPHA_MODE_PREMULTIPLIED));
            hr             = context->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &bitmap);
        }
        if (FAILED(hr))
        {
            return hr;
        }
        // The sheet scaled to the surface: drawn in the page's DIPs at a smaller DPI.
        const float scale = width / page_.size.width;
        context->SetTarget(bitmap.Get());
        context->SetDpi(96.0f * scale, 96.0f * scale);
        context->BeginDraw();
        context->Clear(D2D1::ColorF(D2D1::ColorF::White));
        {
            win32::D2DRenderer renderer(context.Get(), measuring_.dwrite.Get());
            renderer.begin_frame();
            PrintPage page = page_;
            page.index     = desired - 1;
            job_.draw_page(renderer, page, measuring_.context());
        }
        hr = context->EndDraw();
        context->SetTarget(nullptr);
        if (SUCCEEDED(hr))
        {
            hr = preview_->DrawPage(desired, surface.Get(), 96.0f, 96.0f);
        }
        return hr;
    }

    PrintJob&                              job_;
    const PrintMeasuring&                  measuring_;
    PrintDevice&                           device_;
    UiThread&                              ui_;
    ComPtr<IPrintPreviewDxgiPackageTarget> preview_;
    PrintPage                              page_{};
    std::atomic<std::size_t>               count_{0};
    std::atomic<bool>                      closed_{false};
};

// How the dialog ended, shared with the system's callbacks, which may outlive print().
struct PreviewState
{
    std::atomic<bool>        finished{false};
    std::atomic<PrintResult> result{PrintResult::Canceled};
};

PrintResult result_of(printing::PrintTaskCompletion completion)
{
    switch (completion)
    {
        case printing::PrintTaskCompletion::Submitted:
            return PrintResult::Printed;
        case printing::PrintTaskCompletion::Canceled:
        case printing::PrintTaskCompletion::Abandoned:
            return PrintResult::Canceled;
        case printing::PrintTaskCompletion::Failed:
            break;
    }
    return PrintResult::Failed;
}

} // namespace

std::optional<PrintResult> print_with_preview(HWND owner, PrintJob& job, const PrintMeasuring& measuring)
{
    PrintDevice device;
    if (owner == nullptr || !device.create())
    {
        return std::nullopt;
    }
    try
    {
        const auto             interop = winrt::get_activation_factory<printing::PrintManager, IPrintManagerInterop>();
        printing::PrintManager manager{nullptr};
        winrt::check_hresult(
            interop->GetForWindow(owner, winrt::guid_of<printing::PrintManager>(), winrt::put_abi(manager)));

        UiThread                 ui;
        const auto               state  = std::make_shared<PreviewState>();
        const auto               source = winrt::make_self<DocumentSource>(job, measuring, device, ui);
        const winrt::hstring     title{job.title.empty() ? std::wstring(L"Document") : win32::widen(job.title)};
        const winrt::event_token token = manager.PrintTaskRequested(
            [state, source, title](const printing::PrintManager&, const printing::PrintTaskRequestedEventArgs& args)
            {
                const printing::PrintTask
                    task = args.Request()
                               .CreatePrintTask(title,
                                                [source](const printing::PrintTaskSourceRequestedArgs& requested)
                                                { requested.SetSource(source.as<printing::IPrintDocumentSource>()); });
                // The pages can be chosen, besides the usual copies, orientation and paper.
                const printing::PrintTaskOptions options = task.Options();
                options.PageRangeOptions().AllowAllPages(true);
                options.PageRangeOptions().AllowCustomSetOfPages(true);
                options.PageRangeOptions().AllowCurrentPage(false);
                const auto shown = options.DisplayedOptions();
                shown.Clear();
                shown.Append(printing::StandardPrintTaskOptions::Copies());
                shown.Append(printing::StandardPrintTaskOptions::CustomPageRanges());
                shown.Append(printing::StandardPrintTaskOptions::Orientation());
                shown.Append(printing::StandardPrintTaskOptions::MediaSize());
                shown.Append(printing::StandardPrintTaskOptions::Duplex());
                shown.Append(printing::StandardPrintTaskOptions::ColorMode());
                task.Completed(
                    [state](const printing::PrintTask&, const printing::PrintTaskCompletedEventArgs& completed)
                    {
                        state->result.store(result_of(completed.Completion()));
                        state->finished.store(true);
                    });
            });

        winrt::Windows::Foundation::IAsyncOperation<bool> shown{nullptr};
        winrt::check_hresult(
            interop->ShowPrintUIForWindowAsync(owner,
                                               winrt::guid_of<winrt::Windows::Foundation::IAsyncOperation<bool>>(),
                                               winrt::put_abi(shown)));
        // No print task (the dialog didn't show) ends it here; a task ends with its Completed.
        shown.Completed(
            [state](const winrt::Windows::Foundation::IAsyncOperation<bool>& operation,
                    winrt::Windows::Foundation::AsyncStatus                  status)
            {
                if (status != winrt::Windows::Foundation::AsyncStatus::Completed || !operation.GetResults())
                {
                    state->finished.store(true);
                }
            });
        ui.pump(state->finished);
        manager.PrintTaskRequested(token);
        source->close();
        return state->result.load();
    } catch (...)
    {
        return std::nullopt; // no PrintManager here: the classic dialog
    }
}

} // namespace ui::detail
