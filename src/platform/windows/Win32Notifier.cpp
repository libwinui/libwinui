// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Backend.hpp"
#include "Win32Apartment.hpp"
#include "Win32AppIdentity.hpp"
#include "Win32Icon.hpp"
#include "Win32MessageWindow.hpp"
#include "Win32String.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/ui/Notification.hpp"

// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
// clang-format on

#include <exception>
#include <filesystem>
#include <format>
#include <list>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>

#include <cstdint>

namespace ui::win32
{

namespace
{

namespace notifications = winrt::Windows::UI::Notifications;

// The executable's FileDescription, which is what Explorer and Task Manager call the program.
std::wstring file_description(const std::filesystem::path& exe)
{
    DWORD      handle = 0;
    const auto size   = GetFileVersionInfoSizeW(exe.c_str(), &handle);
    if (size == 0)
    {
        return {};
    }
    std::vector<std::byte> info(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, info.data()))
    {
        return {};
    }
    struct Translation
    {
        WORD language;
        WORD code_page;
    };
    Translation* translations = nullptr;
    UINT         length       = 0;
    if (!VerQueryValueW(info.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &length) ||
        length < sizeof(Translation))
    {
        return {};
    }
    const std::wstring key   = std::format(L"\\StringFileInfo\\{:04x}{:04x}\\FileDescription",
                                           translations[0].language,
                                           translations[0].code_page);
    wchar_t*           value = nullptr;
    if (!VerQueryValueW(info.data(), key.c_str(), reinterpret_cast<void**>(&value), &length) || length == 0)
    {
        return {};
    }
    return value;
}

// `icon` written as a PNG to `path`, which a toast can show; empty when that failed.
std::filesystem::path write_icon(HICON icon, const std::filesystem::path& path)
{
    if (icon == nullptr || path.empty())
    {
        return {};
    }
    try
    {
        const Bitmap pixels = icon_pixels(icon);
        if (pixels.empty())
        {
            return {};
        }
        pixels.write_png(path);
        return path;
    } catch (const std::exception&)
    {
        return {};
    }
}

std::wstring escape_xml(std::wstring_view text)
{
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t c : text)
    {
        switch (c)
        {
            case L'&':
                out += L"&amp;";
                break;
            case L'<':
                out += L"&lt;";
                break;
            case L'>':
                out += L"&gt;";
                break;
            case L'"':
                out += L"&quot;";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

std::wstring file_uri(const std::filesystem::path& path)
{
    return L"file:///" + escape_xml(path.generic_wstring());
}

// The toast notifier, and with it the program's registration: an unpackaged program shows toasts
// under an AppUserModelID that has a key under HKCU\Software\Classes\AppUserModelId naming it
// and its icon.
class ToastNotifier final : public Notifier
{
public:
    void notify(Notification notification) override
    {
        try
        {
            if (!ready())
            {
                return;
            }
            std::wstring xml = L"<toast><visual><binding template=\"ToastGeneric\">";
            xml += L"<text>" + escape_xml(widen(notification.title)) + L"</text>";
            if (!notification.text.empty())
            {
                xml += L"<text>" + escape_xml(widen(notification.text)) + L"</text>";
            }
            if (const std::filesystem::path image = picture(notification.icon); !image.empty())
            {
                xml += L"<image placement=\"appLogoOverride\" src=\"" + file_uri(image) + L"\"/>";
            }
            xml += L"</binding></visual></toast>";

            winrt::Windows::Data::Xml::Dom::XmlDocument document;
            document.LoadXml(xml);
            notifications::ToastNotification toast{document};

            // The toast keeps a handle on the click handler until it is dismissed; the handler
            // forwards to the UI thread, which owns the callback.
            MessageWindow& window = MessageWindow::get();
            auto           entry  = std::make_shared<Callback<void()>>(std::move(notification.on_click));
            pending_.push_back(entry);
            std::weak_ptr<Callback<void()>> weak = entry;
            toast.Activated(
                [&window, weak, this](const notifications::ToastNotification&, const winrt::Windows::Foundation::IInspectable&)
                {
                    window.post(
                        [weak, this]
                        {
                            if (const std::shared_ptr<Callback<void()>> click = weak.lock())
                            {
                                if (*click)
                                {
                                    (*click)();
                                }
                                pending_.remove(click);
                            }
                        });
                });
            const auto forget = [&window, weak, this]
            {
                window.post(
                    [weak, this]
                    {
                        if (const std::shared_ptr<Callback<void()>> click = weak.lock())
                        {
                            pending_.remove(click);
                        }
                    });
            };
            toast.Dismissed([forget](const auto&, const auto&) { forget(); });
            toast.Failed([forget](const auto&, const auto&) { forget(); });
            notifier_.Show(toast);
        } catch (const winrt::hresult_error&)
        {
            // Notifications are best effort: a system without them shows nothing.
        }
    }

private:
    // Registers the program and makes the notifier on the first call; false when the system has
    // no toasts for it or the user turned them off.
    bool ready()
    {
        if (!tried_)
        {
            tried_ = true;
            keep_mta_alive();
            const std::wstring id = register_program();
            if (!id.empty())
            {
                notifier_ = notifications::ToastNotificationManager::CreateToastNotifier(id);
            }
        }
        if (!notifier_)
        {
            return false;
        }
        try
        {
            return notifier_.Setting() == notifications::NotificationSetting::Enabled;
        } catch (const winrt::hresult_error&)
        {
            // An unpackaged program's notifier can't report its setting ("Element not found"),
            // but shows its toasts; the system still applies the user's choices to them.
            return true;
        }
    }

    // The AppUserModelID toasts go out under: the one the app set for itself, or one made from
    // the executable's path, so two copies of a program in different folders stay apart.
    std::wstring register_program()
    {
        const std::filesystem::path exe = module_path();
        std::wstring                id  = explicit_app_user_model_id();
        if (id.empty())
        {
            std::uint32_t hash = 2166136261u; // FNV-1a over the lower-cased path
            for (wchar_t c : exe.wstring())
            {
                hash = (hash ^ static_cast<std::uint32_t>(towlower(c))) * 16777619u;
            }
            id = std::format(L"libwinui.{}.{:08x}", exe.stem().wstring(), hash);
        }

        folder_           = cache_folder(L"notifications");
        std::wstring name = file_description(exe);
        if (name.empty())
        {
            name = exe.stem().wstring();
        }
        HICON large = nullptr;
        ExtractIconExW(exe.c_str(), 0, &large, nullptr, 1);
        app_icon_ = write_icon(large,
                               folder_.empty() ? std::filesystem::path{} : folder_ / (exe.stem().wstring() + L".png"));
        if (large != nullptr)
        {
            DestroyIcon(large);
        }

        HKEY               key  = nullptr;
        const std::wstring path = L"Software\\Classes\\AppUserModelId\\" + id;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
            ERROR_SUCCESS)
        {
            return {};
        }
        const auto set = [key](const wchar_t* value_name, const std::wstring& value)
        {
            RegSetValueExW(key,
                           value_name,
                           0,
                           REG_SZ,
                           reinterpret_cast<const BYTE*>(value.c_str()),
                           static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        };
        set(L"DisplayName", name);
        if (!app_icon_.empty())
        {
            set(L"IconUri", app_icon_.wstring());
        }
        RegCloseKey(key);
        return id;
    }

    // The picture file for `icon`, written on first use; empty for none.
    std::filesystem::path picture(NotificationIcon icon)
    {
        SHSTOCKICONID  stock{};
        const wchar_t* file = nullptr;
        switch (icon)
        {
            case NotificationIcon::None:
            case NotificationIcon::App: // the toast's header already shows the program's icon
                return {};
            case NotificationIcon::Info:
                stock = SIID_INFO;
                file  = L"info.png";
                break;
            case NotificationIcon::Warning:
                stock = SIID_WARNING;
                file  = L"warning.png";
                break;
            case NotificationIcon::Error:
                stock = SIID_ERROR;
                file  = L"error.png";
                break;
        }
        if (folder_.empty())
        {
            return {};
        }
        const std::filesystem::path path = folder_ / file;
        std::error_code             error;
        if (std::filesystem::exists(path, error))
        {
            return path;
        }
        SHSTOCKICONINFO info{};
        info.cbSize = sizeof(info);
        if (FAILED(SHGetStockIconInfo(stock, SHGSI_ICON | SHGSI_LARGEICON, &info)))
        {
            return {};
        }
        std::filesystem::path written = write_icon(info.hIcon, path);
        DestroyIcon(info.hIcon);
        return written;
    }

    bool                                         tried_ = false;
    notifications::ToastNotifier                 notifier_{nullptr};
    std::filesystem::path                        folder_;
    std::filesystem::path                        app_icon_;
    std::list<std::shared_ptr<Callback<void()>>> pending_; // clicks of the toasts still shown
};

} // namespace

} // namespace ui::win32

namespace ui::detail
{

Notifier& platform_notifier()
{
    static win32::ToastNotifier notifier;
    return notifier;
}

} // namespace ui::detail

#endif // _WIN32
