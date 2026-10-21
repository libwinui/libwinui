// SPDX-License-Identifier: MIT
#pragma once

#include "winui/render/Bitmap.hpp"
#include "winui/ui/Clipboard.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ui::testing
{

/// An in-memory Clipboard for headless tests. It holds one ClipboardContent and never touches the
/// system clipboard.
///
/// Behaves like the real backend where a test can observe it: set_text() replaces every part,
/// set_content() with nothing set fails and leaves the old content, every write and clear()
/// advances content_changed(), and available_formats() lists the format names a real copy would
/// put on the system clipboard. `history_enabled`/`roaming_enabled` are plain fields a test sets.
///
/// Install it with set_clipboard() so ui::clipboard() and every control use it. It uninstalls
/// itself when destroyed, so a test that ends early never leaves a dangling clipboard behind.
///
/// Example:
/// @code
/// // A test: Ctrl+V pastes from the clipboard.
/// ui::testing::TestClipboard clipboard;
/// ui::testing::set_clipboard(&clipboard);
/// clipboard.set_text("Portland");
/// REQUIRE(clipboard.contains(ui::StandardDataFormat::Text));
/// REQUIRE(clipboard.available_formats() == std::vector<std::string>{"CF_UNICODETEXT"});
///
/// auto box = std::make_unique<ui::TextBoxControl>();
/// ui::testing::set_focused(*box, true);
/// box->handle_key({ui::Key::V, ui::KeyAction::Down, {.ctrl = true}});
/// REQUIRE(box->text() == "Portland");
///
/// int changes = 0;
/// ui::Subscription watch = clipboard.content_changed().observe([&](const std::uint64_t&) { ++changes; });
/// clipboard.clear();
/// REQUIRE(changes == 1);
/// REQUIRE_FALSE(clipboard.text().has_value());
/// @endcode
class TestClipboard final : public Clipboard
{
public:
    TestClipboard() = default;
    /// Uninstalls it if it is still the installed clipboard.
    ~TestClipboard() override
    {
        if (ui::detail::installed_clipboard() == this)
        {
            ui::detail::install_clipboard(nullptr);
        }
    }
    TestClipboard(const TestClipboard&)            = delete;
    TestClipboard& operator=(const TestClipboard&) = delete;

    /// Replaces the whole content with `text`, as a real copy does.
    void set_text(const std::string& text) override
    {
        ClipboardContent content;
        content.text = text;
        set_content(content);
    }

    /// The stored text, or nullopt if none was set.
    [[nodiscard]] std::optional<std::string> text() const override
    {
        return content_.text;
    }

    /// Stores `content` and `options` and raises a change; false (nothing changed) when
    /// `content` has no part set.
    bool set_content(const ClipboardContent& content, const ClipboardContentOptions& options = {}) override
    {
        if (!content.text && !content.bitmap && content.storage_items.empty())
        {
            return false;
        }
        content_ = content;
        options_ = options;
        raise_content_changed();
        return true;
    }

    /// The stored picture, returned as stored (no encode/decode round trip).
    [[nodiscard]] std::optional<Bitmap> bitmap() const override
    {
        return content_.bitmap;
    }

    /// The stored paths, in order.
    [[nodiscard]] std::vector<std::filesystem::path> storage_items() const override
    {
        return content_.storage_items;
    }

    /// The stored operation, or None when there are no storage items for it to apply to.
    [[nodiscard]] DataPackageOperation requested_operation() const override
    {
        return content_.storage_items.empty() ? DataPackageOperation::None : content_.requested_operation;
    }

    /// Whether the stored content has the part `format` names.
    [[nodiscard]] bool contains(StandardDataFormat format) const override
    {
        switch (format)
        {
            case StandardDataFormat::Text:
                return content_.text.has_value();
            case StandardDataFormat::Bitmap:
                return content_.bitmap.has_value();
            case StandardDataFormat::StorageItems:
                return !content_.storage_items.empty();
        }
        return false;
    }

    /// The system format names for the stored parts: `CF_UNICODETEXT` for text, `PNG` and
    /// `CF_DIBV5` for a bitmap, `CF_HDROP` and `Preferred DropEffect` for storage items. The
    /// history and cloud opt-out markers follow when the options turned those off.
    [[nodiscard]] std::vector<std::string> available_formats() const override
    {
        std::vector<std::string> formats;
        if (content_.text)
        {
            formats.emplace_back("CF_UNICODETEXT");
        }
        if (content_.bitmap)
        {
            formats.emplace_back("PNG");
            formats.emplace_back("CF_DIBV5");
        }
        if (!content_.storage_items.empty())
        {
            formats.emplace_back("CF_HDROP");
            formats.emplace_back("Preferred DropEffect");
        }
        if (!formats.empty() && !options_.is_allowed_in_history)
        {
            formats.emplace_back("CanIncludeInClipboardHistory");
        }
        if (!formats.empty() && !options_.is_roamable)
        {
            formats.emplace_back("CanUploadToCloudClipboard");
        }
        return formats;
    }

    /// Drops the content and options and raises a change.
    void clear() override
    {
        content_ = {};
        options_ = {};
        raise_content_changed();
    }

    /// Reports `history_enabled`.
    [[nodiscard]] bool is_history_enabled() const override
    {
        return history_enabled;
    }
    /// Reports `roaming_enabled`.
    [[nodiscard]] bool is_roaming_enabled() const override
    {
        return roaming_enabled;
    }

    /// The options the last set_content() was given.
    [[nodiscard]] const ClipboardContentOptions& options() const
    {
        return options_;
    }

    /// What is_history_enabled() reports; off by default.
    bool history_enabled = false;
    /// What is_roaming_enabled() reports; off by default.
    bool roaming_enabled = false;

private:
    ClipboardContent        content_;
    ClipboardContentOptions options_;
};

/// Makes ui::clipboard() return `clipboard` for the whole process, so every control copies to and
/// pastes from it; nullptr goes back to the platform's. Non-owning: `clipboard` must stay alive
/// while installed (a TestClipboard uninstalls itself when destroyed).
///
/// Example:
/// @code
/// ui::testing::TestClipboard clipboard;
/// ui::testing::set_clipboard(&clipboard);
/// clipboard.set_text("Portland");
///
/// auto box = std::make_unique<ui::TextBoxControl>();
/// ui::testing::set_focused(*box, true);
/// box->handle_key({ui::Key::V, ui::KeyAction::Down, {.ctrl = true}});
/// REQUIRE(box->text() == "Portland");
/// @endcode
inline void set_clipboard(Clipboard* clipboard)
{
    ui::detail::install_clipboard(clipboard);
}

} // namespace ui::testing
