// SPDX-License-Identifier: MIT
// The backend of a platform that has none yet: windows cannot be shown and run() fails. The
// headless host (testing/HeadlessUI.hpp, in winui::testing) drives a Window without any of this.
#include "Backend.hpp"
#include "winui/ui/App.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/JumpList.hpp"
#include "winui/ui/Launcher.hpp"
#include "winui/ui/MessageBox.hpp"
#include "winui/ui/Notification.hpp"
#include "winui/ui/Pickers.hpp"
#include "winui/ui/Printing.hpp"
#include "winui/ui/SingleInstance.hpp"
#include "winui/ui/TrayIcon.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <stdexcept>

namespace ui::detail
{

bool create_native_window(Window& /*window*/, bool /*visible*/, bool /*activate*/)
{
    return false;
}

void destroy_native_window(Window& /*window*/)
{
}

void link_spell_checker()
{
}

namespace
{

// The clipboard of a platform without one: it holds what this process put on it.
class MemoryClipboard final : public Clipboard
{
public:
    void set_text(const std::string& text) override
    {
        content_      = {};
        content_.text = text;
        raise_content_changed();
    }
    [[nodiscard]] std::optional<std::string> text() const override
    {
        return content_.text;
    }
    bool set_content(const ClipboardContent& content, const ClipboardContentOptions& /*options*/) override
    {
        if (!content.text && !content.bitmap && content.storage_items.empty())
        {
            return false;
        }
        content_ = content;
        raise_content_changed();
        return true;
    }
    [[nodiscard]] std::optional<Bitmap> bitmap() const override
    {
        return content_.bitmap;
    }
    [[nodiscard]] std::vector<std::filesystem::path> storage_items() const override
    {
        return content_.storage_items;
    }
    [[nodiscard]] DataPackageOperation requested_operation() const override
    {
        return content_.storage_items.empty() ? DataPackageOperation::None : content_.requested_operation;
    }
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
    [[nodiscard]] std::vector<std::string> available_formats() const override
    {
        std::vector<std::string> formats;
        if (content_.text)
        {
            formats.emplace_back("Text");
        }
        if (content_.bitmap)
        {
            formats.emplace_back("Bitmap");
        }
        if (!content_.storage_items.empty())
        {
            formats.emplace_back("StorageItems");
        }
        return formats;
    }
    void clear() override
    {
        content_ = {};
        raise_content_changed();
    }
    [[nodiscard]] bool is_history_enabled() const override
    {
        return false;
    }
    [[nodiscard]] bool is_roaming_enabled() const override
    {
        return false;
    }

private:
    ClipboardContent content_;
};

// No shell to hand anything to.
class NullLauncher final : public Launcher
{
public:
    void launch_uri(const std::string& /*uri*/) override
    {
    }
    void launch_file(const std::filesystem::path& /*file*/) override
    {
    }
};

} // namespace

Clipboard& platform_clipboard()
{
    static MemoryClipboard clipboard;
    return clipboard;
}

PrintResult platform_print(const Window* /*owner*/, PrintJob& /*job*/)
{
    return PrintResult::Failed;
}

Launcher& platform_launcher()
{
    static NullLauncher launcher;
    return launcher;
}

namespace
{
class NullNotifier final : public Notifier
{
public:
    void notify(Notification /*notification*/) override
    {
    }
};
} // namespace

Notifier& platform_notifier()
{
    static NullNotifier notifier;
    return notifier;
}

namespace
{
class NullJumpListHost final : public JumpListHost
{
public:
    void set_tasks(std::vector<JumpTask> /*tasks*/) override
    {
    }
    void add_recent_file(const std::filesystem::path& /*path*/) override
    {
    }
};
} // namespace

JumpListHost& platform_jump_list_host()
{
    static NullJumpListHost host;
    return host;
}

MessageBoxResult platform_message_box(const Window* /*owner*/, const MessageBoxOptions& options)
{
    switch (options.buttons)
    {
        case MessageBoxButtons::Ok:
            return MessageBoxResult::Ok;
        case MessageBoxButtons::YesNo:
            return MessageBoxResult::No;
        default:
            return MessageBoxResult::Cancel;
    }
}

} // namespace ui::detail

namespace ui
{

bool claim_single_instance(std::string_view /*id*/, Callback<void(InstanceActivation)> /*on_activated*/)
{
    return true; // no other instance can be found without a backend
}

int run(Window& /*window*/)
{
    return -1;
}

int run()
{
    return -1;
}

void quit(int /*exit_code*/)
{
}

OsThemeSnapshot os_theme()
{
    return {};
}

std::optional<std::filesystem::path> pick_file(const Window& /*owner*/, const PickFileOptions& /*options*/)
{
    return std::nullopt;
}

std::vector<std::filesystem::path> pick_files(const Window& /*owner*/, const PickFileOptions& /*options*/)
{
    return {};
}

std::optional<std::filesystem::path> pick_save_file(const Window& /*owner*/, const PickSaveFileOptions& /*options*/)
{
    return std::nullopt;
}

std::optional<std::filesystem::path> pick_folder(const Window& /*owner*/, const PickFolderOptions& /*options*/)
{
    return std::nullopt;
}

std::vector<std::filesystem::path> pick_folders(const Window& /*owner*/, const PickFolderOptions& /*options*/)
{
    return {};
}

// No notification area: the icon is never shown and its calls do nothing.
struct TrayIcon::Impl
{
};

TrayIcon::TrayIcon(TrayIconOptions /*options*/) : impl_(std::make_unique<Impl>())
{
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::set_tool_tip(std::string /*tool_tip*/)
{
}

void TrayIcon::set_icon(WindowIcon /*icon*/)
{
}

void TrayIcon::set_menu(std::vector<MenuItem> /*items*/)
{
}

void TrayIcon::notify(Notification /*notification*/)
{
}

void TrayIcon::show_flyout()
{
}

void TrayIcon::hide_flyout()
{
}

void TrayIcon::toggle_flyout()
{
}

} // namespace ui
