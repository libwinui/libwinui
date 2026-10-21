// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <cstdint>

namespace ui
{

/// What the source of a clipboard or drag payload wants done with it.
/// The values are bit flags, so a combination such as Copy | Link is representable.
enum class DataPackageOperation : unsigned
{
    None = 0, ///< Nothing requested (or nothing to do).
    Copy = 1, ///< Copy the items (the default for a copy).
    Move = 2, ///< Move the items; what a file manager's cut asks for.
    Link = 4, ///< Create links/shortcuts to the items.
};

/// The three formats Clipboard::contains() answers for.
enum class StandardDataFormat
{
    Text,         ///< UTF-8 text.
    Bitmap,       ///< A picture, read back as an Image.
    StorageItems, ///< File-system paths.
};

/// Whether content may be kept in the clipboard history (Win+V) and synced to the user's other
/// devices. Both default to allowed. Each only matters while the user has that feature turned on.
struct ClipboardContentOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    bool is_allowed_in_history = true; ///< May appear in the Win+V clipboard history.
    bool is_roamable           = true; ///< May sync to the user's other devices.
};

/// What to put on the clipboard. Every part that is set goes on it together, in one operation,
/// and an app pasting picks the format it understands.
///
/// Example:
/// @code
/// ui::ClipboardContent content;
/// content.text                = "report.pdf";
/// content.storage_items       = {"C:/Reports/report.pdf"};
/// content.requested_operation = ui::DataPackageOperation::Move; // a cut
/// ui::clipboard().set_content(content, {.is_allowed_in_history = false});
/// @endcode
struct ClipboardContent
{
    /// UTF-8 text, or nullopt to put no text on the clipboard.
    std::optional<std::string> text;
    /// A picture (straight-alpha RGBA), or nullopt for none.
    std::optional<Bitmap> bitmap;
    /// File-system paths of files or folders.
    std::vector<std::filesystem::path> storage_items;
    /// The operation requested of whoever pastes the storage items (a file
    /// manager copies or moves them accordingly).
    DataPackageOperation requested_operation = DataPackageOperation::Copy;
};

/// The system clipboard as a control or app sees it: text, a bitmap and storage items.
///
/// The library provides the implementation. There is one for the whole process, returned by
/// ui::clipboard(). TextBox's Ctrl+C/X/V and every other control go through it, and app code uses
/// it the same way, also before ui::run(). A test swaps in a double with
/// `ui::testing::set_clipboard()`. Non-copyable. The library owns it.
///
/// Example:
/// @code
/// ui::Clipboard& clipboard = ui::clipboard();
/// clipboard.set_text("Copied!");
/// if (clipboard.contains(ui::StandardDataFormat::Bitmap))
/// {
///     const std::optional<ui::Bitmap> picture = clipboard.bitmap();
/// }
/// // Keep the subscription alive for as long as the notifications are wanted.
/// ui::ButtonControl paste_button{"Paste"};
/// ui::Subscription  paste_watch = clipboard.content_changed().observe([&](const std::uint64_t&) {
///     paste_button.set_enabled(clipboard.contains(ui::StandardDataFormat::Text));
/// });
/// @endcode
class Clipboard
{
public:
    Clipboard()                            = default;
    Clipboard(const Clipboard&)            = delete;
    Clipboard& operator=(const Clipboard&) = delete;
    virtual ~Clipboard()                   = default;

    /// Replace the clipboard's contents with `text`.
    virtual void set_text(const std::string& text) = 0;

    /// The clipboard's current text, or nullopt if it holds no text (empty, or a
    /// non-text format like an image). Holding an empty string returns "", not nullopt.
    [[nodiscard]] virtual std::optional<std::string> text() const = 0;

    /// Replace the clipboard's contents with every part of `content`
    /// that is set. False when the clipboard could not be opened (another app holds it) or
    /// `content` has nothing in it; the old contents are then left alone.
    ///
    /// Works from any thread. A large bitmap takes a while to encode, so an app copying one
    /// can call it from a background thread to keep its window responsive.
    virtual bool set_content(const ClipboardContent& content, const ClipboardContentOptions& options = {}) = 0;

    /// The clipboard's picture, or nullopt when it holds none or it cannot be
    /// decoded.
    [[nodiscard]] virtual std::optional<Bitmap> bitmap() const = 0;

    /// The paths on the clipboard; empty when it holds none.
    [[nodiscard]] virtual std::vector<std::filesystem::path> storage_items() const = 0;

    /// What the app that copied the storage items wants done with them, such as Move after a cut
    /// in a file manager. None when it said nothing.
    [[nodiscard]] virtual DataPackageOperation requested_operation() const = 0;

    /// Whether the clipboard holds data in `format`.
    [[nodiscard]] virtual bool contains(StandardDataFormat format) const = 0;

    /// The name of every format on the clipboard, in the order its owner put them there. These
    /// are the system's format names, such as "CF_UNICODETEXT", "PNG" or "CF_HDROP". Empty when the
    /// clipboard is empty.
    [[nodiscard]] virtual std::vector<std::string> available_formats() const = 0;

    /// Empty the clipboard.
    virtual void clear() = 0;

    /// Whether the user has turned on the clipboard history (Win+V).
    [[nodiscard]] virtual bool is_history_enabled() const = 0;
    /// Whether the user has turned on syncing the clipboard across devices.
    [[nodiscard]] virtual bool is_roaming_enabled() const = 0;

    /// A count of clipboard changes. It goes up every time any app, this one included, changes
    /// the clipboard. Observe it to get notified of changes, and drop the Subscription to stop.
    /// The clipboard is only watched from the first call on.
    [[nodiscard]] const Property<std::uint64_t>& content_changed()
    {
        if (!watching_)
        {
            watching_ = true;
            start_watching();
        }
        return changes_;
    }

protected:
    /// Starts watching for clipboard changes. Called once, on the first content_changed().
    virtual void start_watching()
    {
    }
    /// Advances content_changed() by one; an implementation calls it on every change it sees,
    /// its own writes included.
    void raise_content_changed()
    {
        ++changes_;
    }

private:
    Property<std::uint64_t> changes_{0};
    bool                    watching_ = false;
};

/// The system clipboard, one for the whole process. It works from the first call, before any
/// window exists. On a platform with no clipboard, it is an in-memory one. Use it on the UI
/// thread.
///
/// Example:
/// @code
/// ui::Property<std::string> report{"All tests passed"};
/// ui::ButtonControl         copy_button{"Copy report"};
/// copy_button.on_click([&] { ui::clipboard().set_text(report.get()); });
/// @endcode
[[nodiscard]] Clipboard& clipboard();

namespace detail
{
// The clipboard ui::clipboard() returns in place of the platform's, or nullptr for the platform's
// own. ui::testing::set_clipboard() is the public way to set it.
void                     install_clipboard(Clipboard* clipboard);
[[nodiscard]] Clipboard* installed_clipboard();
} // namespace detail

} // namespace ui
