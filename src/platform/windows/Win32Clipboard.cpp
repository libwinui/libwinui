// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Win32Clipboard.hpp"

#include "../Backend.hpp"
#include "Win32Apartment.hpp"
#include "Win32String.hpp"

// <windows.h> must come before the shell headers.
// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h> // DROPFILES
// clang-format on

#include "winui/render/Bitmap.hpp"
#include "winui/ui/Clipboard.hpp"

#include <array>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.h>

#include <cstdint>
#include <cstring>
#include <cwchar>

namespace ui::win32
{

// The window procedure of the hidden window Win32Clipboard owns: WM_CLIPBOARDUPDATE becomes
// content_changed().
struct ClipboardListener
{
    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_CLIPBOARDUPDATE)
        {
            if (auto* self = reinterpret_cast<Win32Clipboard*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
            {
                self->raise_content_changed();
            }
            return 0;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
};

namespace
{

constexpr wchar_t ListenerClass[] = L"libwinui.ClipboardListener";

UINT registered_format(const wchar_t* name)
{
    return RegisterClipboardFormatW(name);
}

UINT png_format()
{
    static const UINT format = registered_format(L"PNG");
    return format;
}
UINT drop_effect_format()
{
    static const UINT format = registered_format(L"Preferred DropEffect"); // CFSTR_PREFERREDDROPEFFECT
    return format;
}
UINT history_format()
{
    static const UINT format = registered_format(L"CanIncludeInClipboardHistory");
    return format;
}
UINT cloud_format()
{
    static const UINT format = registered_format(L"CanUploadToCloudClipboard");
    return format;
}

// Another app may hold the clipboard for a moment (a clipboard manager reacting to the last
// change, typically), so an open that fails is retried briefly before giving up.
bool open_clipboard(HWND owner)
{
    for (int attempt = 0; attempt < 10; ++attempt)
    {
        if (OpenClipboard(owner))
        {
            return true;
        }
        Sleep(10);
    }
    return false;
}

struct ClipboardScope
{
    explicit ClipboardScope(HWND owner) : open(open_clipboard(owner))
    {
    }
    ~ClipboardScope()
    {
        if (open)
        {
            CloseClipboard();
        }
    }
    ClipboardScope(const ClipboardScope&)            = delete;
    ClipboardScope& operator=(const ClipboardScope&) = delete;

    bool open;
};

bool put(UINT format, const void* data, std::size_t bytes)
{
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem)
    {
        return false;
    }
    void* dst = GlobalLock(mem);
    if (!dst)
    {
        GlobalFree(mem);
        return false;
    }
    std::memcpy(dst, data, bytes);
    GlobalUnlock(mem);
    // SetClipboardData takes ownership of `mem` only on success; freeing it after that frees
    // memory the clipboard still references.
    if (!SetClipboardData(format, mem))
    {
        GlobalFree(mem);
        return false;
    }
    return true;
}

bool put_dword(UINT format, DWORD value)
{
    return put(format, &value, sizeof(value));
}

std::vector<std::uint8_t> get_bytes(UINT format)
{
    std::vector<std::uint8_t> bytes;
    HANDLE                    handle = GetClipboardData(format);
    if (!handle)
    {
        return bytes;
    }
    const SIZE_T size = GlobalSize(handle);
    if (const auto* data = static_cast<const std::uint8_t*>(GlobalLock(handle)))
    {
        bytes.assign(data, data + size);
        GlobalUnlock(handle);
    }
    return bytes;
}

std::wstring format_name(UINT format)
{
    static constexpr std::array<const wchar_t*, 18> Standard{
        nullptr,
        L"CF_TEXT",
        L"CF_BITMAP",
        L"CF_METAFILEPICT",
        L"CF_SYLK",
        L"CF_DIF",
        L"CF_TIFF",
        L"CF_OEMTEXT",
        L"CF_DIB",
        L"CF_PALETTE",
        L"CF_PENDATA",
        L"CF_RIFF",
        L"CF_WAVE",
        L"CF_UNICODETEXT",
        L"CF_ENHMETAFILE",
        L"CF_HDROP",
        L"CF_LOCALE",
        L"CF_DIBV5",
    };
    if (format > 0 && format < Standard.size())
    {
        return Standard[format];
    }
    switch (format)
    {
        case CF_OWNERDISPLAY:
            return L"CF_OWNERDISPLAY";
        case CF_DSPTEXT:
            return L"CF_DSPTEXT";
        case CF_DSPBITMAP:
            return L"CF_DSPBITMAP";
        case CF_DSPMETAFILEPICT:
            return L"CF_DSPMETAFILEPICT";
        case CF_DSPENHMETAFILE:
            return L"CF_DSPENHMETAFILE";
        default:
            break;
    }
    if (format >= CF_PRIVATEFIRST && format <= CF_PRIVATELAST)
    {
        return L"CF_PRIVATEFIRST+" + std::to_wstring(format - CF_PRIVATEFIRST);
    }
    if (format >= CF_GDIOBJFIRST && format <= CF_GDIOBJLAST)
    {
        return L"CF_GDIOBJFIRST+" + std::to_wstring(format - CF_GDIOBJFIRST);
    }
    std::array<wchar_t, 256> name{};
    if (const int length = GetClipboardFormatNameW(format, name.data(), static_cast<int>(name.size())); length > 0)
    {
        return std::wstring(name.data(), static_cast<std::size_t>(length));
    }
    // By hand: std::format's wide-character formatter is about 50 KB and swprintf's 10 KB, in
    // every app.
    std::wstring hex = L"0x0000";
    for (std::size_t i = hex.size() - 1; format != 0 && i > 1; --i, format >>= 4)
    {
        hex[i] = L"0123456789ABCDEF"[format & 0xF];
    }
    return hex;
}

// WinRT's Clipboard statics, which read the user's settings (and the policies over them) the
// way Settings > System > Clipboard shows them. False when they cannot be reached.
template <typename Query>
bool winrt_clipboard_flag(Query query)
{
    keep_mta_alive();
    try
    {
        return query();
    } catch (const winrt::hresult_error&)
    {
        return false;
    }
}

} // namespace

std::vector<std::uint8_t> dib_from_image(const Bitmap& image)
{
    if (image.empty())
    {
        return {};
    }
    const int         width  = image.width();
    const int         height = image.height();
    const std::size_t stride = static_cast<std::size_t>(width) * 4;

    BITMAPV5HEADER header{};
    header.bV5Size        = sizeof(BITMAPV5HEADER);
    header.bV5Width       = width;
    header.bV5Height      = height; // positive: bottom-up, which every reader handles
    header.bV5Planes      = 1;
    header.bV5BitCount    = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5SizeImage   = static_cast<DWORD>(stride * static_cast<std::size_t>(height));
    header.bV5RedMask     = 0x00FF0000;
    header.bV5GreenMask   = 0x0000FF00;
    header.bV5BlueMask    = 0x000000FF;
    header.bV5AlphaMask   = 0xFF000000;
    header.bV5CSType      = LCS_sRGB;
    header.bV5Intent      = LCS_GM_IMAGES;

    std::vector<std::uint8_t> dib(sizeof(header) + header.bV5SizeImage);
    std::memcpy(dib.data(), &header, sizeof(header));
    const Bitmap                        straight = image.converted(PixelFormat::Rgba8);
    const std::span<const std::uint8_t> rgba     = straight.bytes();
    std::uint8_t*                       out      = dib.data() + sizeof(header);
    for (int y = 0; y < height; ++y)
    {
        const std::uint8_t* src = rgba.data() + static_cast<std::size_t>(height - 1 - y) * stride;
        std::uint8_t*       dst = out + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < width; ++x, src += 4, dst += 4)
        {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
        }
    }
    return dib;
}

std::vector<std::uint8_t> bmp_file_from_dib(std::span<const std::uint8_t> dib)
{
    if (dib.size() < sizeof(BITMAPINFOHEADER))
    {
        return {};
    }
    BITMAPINFOHEADER info{};
    std::memcpy(&info, dib.data(), sizeof(info));
    if (info.biSize < sizeof(BITMAPINFOHEADER) || info.biSize > dib.size())
    {
        return {};
    }

    // What sits between the header and the pixels: the color masks a plain BITMAPINFOHEADER
    // keeps outside itself (the V4/V5 headers hold them), then the palette.
    std::size_t masks = 0;
    if (info.biSize == sizeof(BITMAPINFOHEADER))
    {
        if (info.biCompression == BI_BITFIELDS)
        {
            masks = 3 * sizeof(DWORD);
        }
        else if (info.biCompression == 6) // BI_ALPHABITFIELDS
        {
            masks = 4 * sizeof(DWORD);
        }
    }
    std::size_t colors = info.biClrUsed;
    if (colors == 0 && info.biBitCount <= 8)
    {
        colors = std::size_t{1} << info.biBitCount;
    }

    BITMAPFILEHEADER file{};
    file.bfType    = 0x4D42; // "BM"
    file.bfSize    = static_cast<DWORD>(sizeof(file) + dib.size());
    file.bfOffBits = static_cast<DWORD>(sizeof(file) + info.biSize + masks + colors * sizeof(RGBQUAD));

    std::vector<std::uint8_t> bmp(sizeof(file) + dib.size());
    std::memcpy(bmp.data(), &file, sizeof(file));
    std::memcpy(bmp.data() + sizeof(file), dib.data(), dib.size());
    return bmp;
}

Win32Clipboard::~Win32Clipboard()
{
    if (listener_)
    {
        const auto hwnd = static_cast<HWND>(listener_);
        RemoveClipboardFormatListener(hwnd);
        DestroyWindow(hwnd);
    }
}

void Win32Clipboard::set_text(const std::string& text)
{
    ClipboardContent content;
    content.text = text;
    set_content(content);
}

std::optional<std::string> Win32Clipboard::text() const
{
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
    {
        return std::nullopt;
    }
    const ClipboardScope scope{nullptr};
    if (!scope.open)
    {
        return std::nullopt;
    }
    std::optional<std::string> result;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT))
    {
        if (const auto* data = static_cast<const wchar_t*>(GlobalLock(handle)))
        {
            // Up to the terminator, and never past the block (a malformed one may lack it).
            // narrow() turns another app's unpaired surrogate into U+FFFD.
            const std::size_t capacity = GlobalSize(handle) / sizeof(wchar_t);
            result                     = narrow(std::wstring_view(data, wcsnlen(data, capacity)));
            GlobalUnlock(handle);
        }
    }
    return result;
}

bool Win32Clipboard::set_content(const ClipboardContent& content, const ClipboardContentOptions& options)
{
    if (!content.text && !content.bitmap && content.storage_items.empty())
    {
        return false;
    }
    // Encoded before opening: a page-sized PNG takes long enough that other apps would find the
    // clipboard locked meanwhile.
    std::vector<std::uint8_t> png;
    std::vector<std::uint8_t> dib;
    if (content.bitmap && !content.bitmap->empty())
    {
        png = content.bitmap->encode_png();
        dib = dib_from_image(*content.bitmap);
    }

    const ClipboardScope scope{nullptr};
    if (!scope.open)
    {
        return false;
    }
    EmptyClipboard();

    bool ok = true;
    if (content.text)
    {
        // +1 for the terminator CF_UNICODETEXT requires.
        const std::wstring text = widen(*content.text);
        ok                      = put(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t)) && ok;
    }
    if (!png.empty())
    {
        ok = put(png_format(), png.data(), png.size()) && ok;
    }
    if (!dib.empty())
    {
        ok = put(CF_DIBV5, dib.data(), dib.size()) && ok;
    }
    if (!content.storage_items.empty())
    {
        // DROPFILES, then each path with its terminator, then one more terminator.
        std::vector<std::uint8_t> drop(sizeof(DROPFILES));
        DROPFILES                 header{};
        header.pFiles = sizeof(DROPFILES);
        header.fWide  = TRUE;
        std::memcpy(drop.data(), &header, sizeof(header));
        for (const std::filesystem::path& item : content.storage_items)
        {
            const std::wstring& path  = item.native();
            const auto*         first = reinterpret_cast<const std::uint8_t*>(path.c_str());
            drop.insert(drop.end(), first, first + (path.size() + 1) * sizeof(wchar_t));
        }
        drop.insert(drop.end(), sizeof(wchar_t), 0);
        ok = put(CF_HDROP, drop.data(), drop.size()) && ok;
        ok = put_dword(drop_effect_format(), static_cast<DWORD>(content.requested_operation)) && ok;
    }
    if (!options.is_allowed_in_history)
    {
        ok = put_dword(history_format(), 0) && ok;
    }
    if (!options.is_roamable)
    {
        ok = put_dword(cloud_format(), 0) && ok;
    }
    return ok;
}

std::optional<Bitmap> Win32Clipboard::bitmap() const
{
    const ClipboardScope scope{nullptr};
    if (!scope.open)
    {
        return std::nullopt;
    }
    try
    {
        if (IsClipboardFormatAvailable(png_format()))
        {
            const std::vector<std::uint8_t> png = get_bytes(png_format());
            if (!png.empty())
            {
                return Bitmap::decode(png);
            }
        }
        for (const UINT format : {UINT{CF_DIBV5}, UINT{CF_DIB}})
        {
            if (!IsClipboardFormatAvailable(format))
            {
                continue;
            }
            const std::vector<std::uint8_t> bmp = bmp_file_from_dib(get_bytes(format));
            if (!bmp.empty())
            {
                return Bitmap::decode(bmp);
            }
        }
    } catch (const std::exception&) // NOLINT(bugprone-empty-catch)
    {
        // Undecodable data: as if there were no picture.
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> Win32Clipboard::storage_items() const
{
    std::vector<std::filesystem::path> paths;
    if (!IsClipboardFormatAvailable(CF_HDROP))
    {
        return paths;
    }
    const ClipboardScope scope{nullptr};
    if (!scope.open)
    {
        return paths;
    }
    if (HANDLE handle = GetClipboardData(CF_HDROP))
    {
        if (auto drop = static_cast<HDROP>(GlobalLock(handle)))
        {
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i)
            {
                const UINT   length = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(length, L'\0');
                DragQueryFileW(drop, i, path.data(), length + 1);
                paths.push_back(std::move(path));
            }
            GlobalUnlock(handle);
        }
    }
    return paths;
}

DataPackageOperation Win32Clipboard::requested_operation() const
{
    if (!IsClipboardFormatAvailable(drop_effect_format()))
    {
        return DataPackageOperation::None;
    }
    const ClipboardScope scope{nullptr};
    if (!scope.open)
    {
        return DataPackageOperation::None;
    }
    const std::vector<std::uint8_t> bytes = get_bytes(drop_effect_format());
    if (bytes.size() < sizeof(DWORD))
    {
        return DataPackageOperation::None;
    }
    DWORD effect = 0;
    std::memcpy(&effect, bytes.data(), sizeof(effect));
    // DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK; DROPEFFECT_SCROLL and anything else dropped.
    return static_cast<DataPackageOperation>(effect & 0x7u);
}

bool Win32Clipboard::contains(StandardDataFormat format) const
{
    switch (format)
    {
        case StandardDataFormat::Text:
            return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
        case StandardDataFormat::Bitmap:
            return IsClipboardFormatAvailable(png_format()) || IsClipboardFormatAvailable(CF_DIBV5) ||
                   IsClipboardFormatAvailable(CF_DIB);
        case StandardDataFormat::StorageItems:
            return IsClipboardFormatAvailable(CF_HDROP) != FALSE;
    }
    return false;
}

std::vector<std::string> Win32Clipboard::available_formats() const
{
    std::vector<std::string> names;
    const ClipboardScope     scope{nullptr};
    if (!scope.open)
    {
        return names;
    }
    for (UINT format = EnumClipboardFormats(0); format != 0; format = EnumClipboardFormats(format))
    {
        names.push_back(narrow(format_name(format)));
    }
    return names;
}

void Win32Clipboard::clear()
{
    const ClipboardScope scope{nullptr};
    if (scope.open)
    {
        EmptyClipboard();
    }
}

bool Win32Clipboard::is_history_enabled() const
{
    return winrt_clipboard_flag(
        [] { return winrt::Windows::ApplicationModel::DataTransfer::Clipboard::IsHistoryEnabled(); });
}

bool Win32Clipboard::is_roaming_enabled() const
{
    return winrt_clipboard_flag(
        [] { return winrt::Windows::ApplicationModel::DataTransfer::Clipboard::IsRoamingEnabled(); });
}

void Win32Clipboard::start_watching()
{
    const HINSTANCE   instance   = GetModuleHandleW(nullptr);
    static const bool registered = [instance]
    {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &ClipboardListener::proc;
        wc.hInstance     = instance;
        wc.lpszClassName = ListenerClass;
        return RegisterClassExW(&wc) != 0;
    }();
    if (!registered)
    {
        return;
    }
    HWND hwnd = CreateWindowExW(0, ListenerClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!hwnd)
    {
        return;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    AddClipboardFormatListener(hwnd);
    listener_ = hwnd;
}

} // namespace ui::win32

namespace ui::detail
{

Clipboard& platform_clipboard()
{
    static win32::Win32Clipboard clipboard;
    return clipboard;
}

} // namespace ui::detail

#endif // _WIN32
