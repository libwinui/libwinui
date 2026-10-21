// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "WicDecoder.hpp"

#include "winui/core/Utf8Path.hpp"
#include "winui/render/Bitmap.hpp"

#include <filesystem>
#include <format>
#include <objbase.h>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>

namespace ui::win32
{
namespace
{

using Microsoft::WRL::ComPtr;

[[noreturn]] void fail(const std::string& what, HRESULT hr)
{
    throw std::runtime_error(std::format("image: {} (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr)));
}

// COM per call: a thread already in an apartment keeps it (the WIC factory is free-threaded).
// Not per thread, since a thread_local destructor runs under the loader lock in a DLL build.
class ComScope
{
public:
    ComScope()
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owned_           = SUCCEEDED(hr);
    }
    ~ComScope()
    {
        if (owned_)
        {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&)            = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool owned_ = false;
};

ComPtr<IWICImagingFactory> create_factory()
{
    ComPtr<IWICImagingFactory> factory;
    const HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        fail("cannot create the WIC factory", hr);
    }
    return factory;
}

ImageOrientation read_orientation(IWICBitmapFrameDecode* frame)
{
    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(&reader)) || !reader)
    {
        return ImageOrientation::Normal;
    }

    // JPEG keeps the EXIF IFD under /app1; TIFF (and the raw formats built on it) has the tag
    // at the top level.
    for (const wchar_t* query : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"})
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        const HRESULT  hr  = reader->GetMetadataByName(query, &value);
        const unsigned tag = SUCCEEDED(hr) && value.vt == VT_UI2 ? value.uiVal : 0;
        PropVariantClear(&value);
        if (tag >= 1 && tag <= 8)
        {
            return static_cast<ImageOrientation>(tag);
        }
    }
    return ImageOrientation::Normal;
}

WicDecoded decode_frame(IWICImagingFactory* factory, IWICBitmapDecoder* decoder, const ImageDecodeOptions& options)
{
    ComPtr<IWICBitmapFrameDecode> frame;
    if (const HRESULT hr = decoder->GetFrame(0, &frame); FAILED(hr))
    {
        fail("cannot read the first frame", hr);
    }

    UINT width = 0, height = 0;
    if (const HRESULT hr = frame->GetSize(&width, &height); FAILED(hr) || width == 0 || height == 0)
    {
        fail("the image has no size", FAILED(hr) ? hr : E_FAIL);
    }

    WicDecoded result;
    if (options.apply_orientation)
    {
        result.orientation = read_orientation(frame.Get());
    }

    // The bounds are the upright picture's; the scaler works on the stored pixels.
    int max_width  = options.max_width;
    int max_height = options.max_height;
    if (swaps_axes(result.orientation))
    {
        std::swap(max_width, max_height);
    }
    const auto [target_width,
                target_height] = fit_within(static_cast<int>(width), static_cast<int>(height), max_width, max_height);

    ComPtr<IWICBitmapSource> source = frame;

    // Scale first, straight off the frame: the JPEG codec answers a scaler with its DCT-domain
    // reduction, so a thumbnail of a large photo never decodes the full frame.
    if (target_width != static_cast<int>(width) || target_height != static_cast<int>(height))
    {
        ComPtr<IWICBitmapScaler> scaler;
        HRESULT                  hr = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr))
        {
            hr = scaler->Initialize(source.Get(),
                                    static_cast<UINT>(target_width),
                                    static_cast<UINT>(target_height),
                                    WICBitmapInterpolationModeFant);
        }
        if (FAILED(hr))
        {
            fail("cannot scale the image", hr);
        }
        source = scaler;
    }

    // The converter goes last: a scaler picks its own output format (asked for RGBA it gives
    // BGRA), so only the final stage guarantees the format.
    ComPtr<IWICFormatConverter> converter;
    HRESULT                     hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
    {
        hr = converter->Initialize(source.Get(),
                                   GUID_WICPixelFormat32bppRGBA, // straight alpha, R first: Image's own layout
                                   WICBitmapDitherTypeNone,
                                   nullptr,
                                   0.0,
                                   WICBitmapPaletteTypeMedianCut);
    }
    if (FAILED(hr))
    {
        fail("cannot convert the image to RGBA", hr);
    }

    // No pixels at all beats pixels in an order nobody checked.
    GUID format{};
    if (FAILED(converter->GetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppRGBA)
    {
        fail("the converter did not produce RGBA", E_UNEXPECTED);
    }

    UINT out_width = 0, out_height = 0;
    if (hr = converter->GetSize(&out_width, &out_height); FAILED(hr) || out_width == 0 || out_height == 0)
    {
        fail("the scaled image has no size", FAILED(hr) ? hr : E_FAIL);
    }

    Bitmap                        image{static_cast<int>(out_width), static_cast<int>(out_height)};
    const std::span<std::uint8_t> bytes = image.bytes();
    if (hr = converter->CopyPixels(nullptr, out_width * 4, static_cast<UINT>(bytes.size()), bytes.data()); FAILED(hr))
    {
        fail("cannot decode the pixels", hr);
    }

    result.image = std::move(image);
    return result;
}

} // namespace

WicDecoded wic_decode_file(const std::filesystem::path& path, const ImageDecodeOptions& options)
{
    const ComScope com;
    const auto     factory = create_factory();

    ComPtr<IWICBitmapDecoder> decoder;
    if (const HRESULT
            hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
        FAILED(hr))
    {
        fail("cannot open " + path_to_utf8(path), hr);
    }
    return decode_frame(factory.Get(), decoder.Get(), options);
}

WicDecoded wic_decode_memory(std::span<const std::uint8_t> data, const ImageDecodeOptions& options)
{
    const ComScope com;
    const auto     factory = create_factory();

    // InitializeFromMemory takes a mutable pointer but only reads through it.
    ComPtr<IWICStream> stream;
    HRESULT            hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr))
    {
        hr = stream->InitializeFromMemory(const_cast<BYTE*>(data.data()), static_cast<DWORD>(data.size()));
    }
    if (FAILED(hr))
    {
        fail("cannot wrap the data in a stream", hr);
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder); FAILED(hr))
    {
        fail("unrecognized image data", hr);
    }
    return decode_frame(factory.Get(), decoder.Get(), options);
}

} // namespace ui::win32

#endif // _WIN32
