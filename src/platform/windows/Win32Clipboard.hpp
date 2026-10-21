// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "winui/ui/Clipboard.hpp"

#include <span>
#include <vector>

#include <cstdint>

namespace ui::win32
{

struct ClipboardListener;

/// OpenClipboard/CloseClipboard bracket each call rather than holding the clipboard open
/// across calls (holding it open blocks every other app's own clipboard access for as long
/// as it's held, which nothing here has a reason to do). The one piece of state is the
/// hidden message-only window that receives WM_CLIPBOARDUPDATE, created by the first
/// content_changed() on the thread that calls it.
///
/// ui::clipboard() returns the process's one, made on its first call; app code goes through that
/// and so stays portable.
///
/// Example:
/// @code
/// ui::Clipboard& clipboard = ui::clipboard(); // the Win32Clipboard on Windows
/// ui::ClipboardContent content;
/// content.text   = "Report";
/// content.bitmap = chart_image;
/// clipboard.set_content(content, {.is_allowed_in_history = false});
///
/// if (clipboard.contains(ui::StandardDataFormat::StorageItems))
/// {
///     for (const std::filesystem::path& file : clipboard.storage_items()) { /* ... */ }
/// }
/// @endcode
class Win32Clipboard final : public Clipboard
{
public:
    /// Touches nothing until the first call.
    Win32Clipboard() = default;
    /// Destroys the WM_CLIPBOARDUPDATE listener window, if content_changed() created one.
    ~Win32Clipboard() override;

    /// Replaces the clipboard with `text` as CF_UNICODETEXT.
    void set_text(const std::string& text) override;
    /// CF_UNICODETEXT as UTF-8; nullopt when there is no text or the clipboard is locked.
    [[nodiscard]] std::optional<std::string> text() const override;

    /// Replaces the clipboard with every part of `content` at once (formats in the file comment).
    /// False if the clipboard could not be opened (another app holds it).
    bool set_content(const ClipboardContent& content, const ClipboardContentOptions& options = {}) override;
    /// The picture on the clipboard (PNG, then CF_DIBV5, then CF_DIB), decoded by WIC.
    [[nodiscard]] std::optional<Bitmap> bitmap() const override;
    /// The CF_HDROP paths; empty when there are none.
    [[nodiscard]] std::vector<std::filesystem::path> storage_items() const override;
    /// "Preferred DropEffect" of the storage items; None when absent.
    [[nodiscard]] DataPackageOperation requested_operation() const override;
    /// Whether a format that can be read as `format` is available (IsClipboardFormatAvailable).
    [[nodiscard]] bool contains(StandardDataFormat format) const override;
    /// The names of every format on the clipboard, standard (CF_*) and registered, in order.
    [[nodiscard]] std::vector<std::string> available_formats() const override;
    /// EmptyClipboard.
    void clear() override;
    /// WinRT Clipboard::IsHistoryEnabled: the user's "Clipboard history" (Win+V) switch.
    [[nodiscard]] bool is_history_enabled() const override;
    /// WinRT Clipboard::IsRoamingEnabled: the user's "Sync across your devices" switch.
    [[nodiscard]] bool is_roaming_enabled() const override;

protected:
    /// Creates the message-only window and AddClipboardFormatListener on the calling thread.
    void start_watching() override;

private:
    friend struct ClipboardListener; // its window procedure raises content_changed()

    void* listener_ = nullptr; // HWND
};

/// `image` as the CF_DIBV5 block Win32Clipboard puts on the clipboard: a BITMAPV5HEADER
/// (32 bpp, BI_BITFIELDS, BGRA masks, sRGB) followed by bottom-up rows of straight-alpha
/// BGRA.
[[nodiscard]] std::vector<std::uint8_t> dib_from_image(const Bitmap& image);

/// A CF_DIB or CF_DIBV5 block (a BITMAPINFOHEADER or later, color masks and palette if any,
/// then the pixels) with a BITMAPFILEHEADER in front, making it a .bmp file WIC can decode.
/// Empty when the header is not one.
[[nodiscard]] std::vector<std::uint8_t> bmp_file_from_dib(std::span<const std::uint8_t> dib);

} // namespace ui::win32

#endif // _WIN32
