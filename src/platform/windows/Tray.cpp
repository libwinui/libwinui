// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../../ui/WindowHost.hpp"
#include "../../ui/menus/MenuInvoke.hpp"
#include "../Backend.hpp"
#include "DWriteMeasurer.hpp"
#include "DarkMode.hpp"
#include "Win32Icon.hpp"
#include "Win32String.hpp"
#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/TrayIcon.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <memory>
#include <optional>
#include <shellapi.h>
#include <shellscalingapi.h> // GetDpiForMonitor
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM
#include <wrl/client.h>

#include <cmath>
#include <cwchar>

namespace ui::win32
{

namespace
{

using Microsoft::WRL::ComPtr;

constexpr wchar_t TrayClassName[] = L"libwinui.TrayIcon";
constexpr UINT    IconId          = 1;
constexpr UINT    CallbackMessage = WM_APP + 1; // the icon's NOTIFYICON_VERSION_4 events
constexpr UINT    DismissMessage  = WM_APP + 2; // the flyout lost the activation
constexpr UINT    FirstMenuId     = 1;          // the menu's item i is FirstMenuId + i

// The notification area is on the primary monitor's taskbar, and draws its icons at that DPI.
UINT taskbar_dpi()
{
    UINT x = 96;
    UINT y = 96;
    if (FAILED(GetDpiForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), MDT_EFFECTIVE_DPI, &x, &y)))
    {
        return GetDpiForSystem();
    }
    return x;
}

Rect to_rect(const RECT& r)
{
    return {static_cast<float>(r.left),
            static_cast<float>(r.top),
            static_cast<float>(r.right - r.left),
            static_cast<float>(r.bottom - r.top)};
}

void set_cloaked(HWND hwnd, bool cloaked)
{
    const BOOL value = cloaked ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &value, sizeof(value));
}

// What the icon shows and how the flyout behaves, as plain values: the TrayIconOptions with its
// bindings resolved.
struct Settings
{
    std::string           tool_tip;
    WindowIcon            icon{};
    bool                  icon_follows_taskbar_theme = true;
    std::vector<MenuItem> menu{};
    float                 flyout_max_height = 0.0f;
    float                 flyout_margin     = 12.0f;
    bool                  light_dismiss     = true;
    std::optional<Color>  background{};
};

} // namespace
} // namespace ui::win32

namespace ui
{
using namespace ui::win32;

struct TrayIcon::Impl
{
    Settings         options;
    Window*          flyout = nullptr; // the app's window, or none
    Callback<void()> click;
    Callback<void()> notification_click;
    Callback<void()> last_notification_click; // the on_click of the last notify(), if it had one
    Subscription     tool_tip_binding;
    Subscription     icon_binding;
    // The menu's bound `checked`; nothing to repaint, as the menu is built when it opens.
    detail::MenuChecks menu_checks;

    HWND  hwnd            = nullptr; // the icon's window
    UINT  taskbar_created = 0;       // RegisterWindowMessageW(L"TaskbarCreated")
    bool  icon_added      = false;
    HICON icon            = nullptr;
    bool  icon_owned      = false; // the stock icon is shared and must not be destroyed

    // A click on the icon while the flyout is open first light-dismisses it, then NIN_SELECT
    // arrives. Within a double-click time the click was the dismissal and must not reopen it.
    ULONGLONG dismissed_at = 0;
    // Enter on the focused icon can deliver NIN_KEYSELECT twice; the second must not close again
    // what the first just opened.
    ULONGLONG    shown_at = 0;
    Subscription activation;

    ComPtr<ID2D1Factory>            d2d;
    ComPtr<IDWriteFactory>          dwrite;
    std::unique_ptr<DWriteMeasurer> measurer;

    explicit Impl(TrayIconOptions opts) :
        flyout(opts.flyout),
        click(std::move(opts.on_click)),
        notification_click(std::move(opts.on_notification_click))
    {
        options.tool_tip                   = opts.tool_tip.initial();
        options.icon                       = opts.icon.initial();
        options.icon_follows_taskbar_theme = opts.icon_follows_taskbar_theme;
        options.menu                       = std::move(opts.menu);
        options.flyout_max_height          = opts.flyout_max_height;
        options.flyout_margin              = opts.flyout_margin;
        options.light_dismiss              = opts.light_dismiss;
        options.background                 = opts.flyout_background;
        follow_menu_bindings(options.menu, menu_checks);
        start();
        if (opts.tool_tip.reactive())
        {
            tool_tip_binding = opts.tool_tip.subscribe([this](std::string value) { set_tool_tip(std::move(value)); });
        }
        if (opts.icon.reactive())
        {
            icon_binding = opts.icon.subscribe([this](WindowIcon value) { set_icon(std::move(value)); });
        }
    }
    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl()
    {
        remove_icon();
        if (hwnd != nullptr)
        {
            DestroyWindow(hwnd);
        }
        release_icon(icon, icon_owned);
    }

    void set_tool_tip(std::string tool_tip)
    {
        options.tool_tip = std::move(tool_tip);
        if (icon_added)
        {
            NOTIFYICONDATAW data = icon_data();
            data.uFlags          = NIF_TIP | NIF_SHOWTIP;
            wcsncpy_s(data.szTip, widen(options.tool_tip).c_str(), _TRUNCATE);
            Shell_NotifyIconW(NIM_MODIFY, &data);
        }
    }

    void set_icon(WindowIcon next)
    {
        options.icon = std::move(next);
        if (hwnd != nullptr)
        {
            refresh_icon();
        }
    }

    // Whether the flyout is showing. A Window reports visible until a host says otherwise, so one
    // whose OS window was never created counts as closed here.
    [[nodiscard]] bool flyout_open() const
    {
        return flyout != nullptr && flyout->native_handle() != nullptr && flyout->visible().get();
    }

    [[nodiscard]] float flyout_width() const
    {
        return flyout->options().size.width;
    }

    // The flyout's OS window is made hidden, the first time it is needed.
    bool ensure_flyout_window()
    {
        if (flyout == nullptr)
        {
            return false;
        }
        if (flyout->native_handle() != nullptr)
        {
            return true;
        }
        if (!detail::create_native_window(*flyout, false))
        {
            return false;
        }
        // Light dismiss. Posted, not done here: this runs inside the flyout's own WM_ACTIVATE.
        activation = flyout->active().observe(
            [this](const bool& active)
            {
                if (!active && options.light_dismiss && flyout_open())
                {
                    PostMessageW(hwnd, DismissMessage, 0, 0);
                }
            });
        return true;
    }

    static void release_icon(HICON handle, bool owned)
    {
        if (handle != nullptr && owned)
        {
            DestroyIcon(handle);
        }
    }

    [[nodiscard]] NOTIFYICONDATAW icon_data() const
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd   = hwnd;
        data.uID    = IconId;
        return data;
    }

    // ---- the icon ----

    // Redrawn, not cached: the taskbar's DPI and light/dark setting can both change under it.
    void refresh_icon()
    {
        const int size  = GetSystemMetricsForDpi(SM_CXSMICON, taskbar_dpi());
        HICON     next  = nullptr;
        bool      owned = true;
        if (!options.icon.empty())
        {
            WindowIcon drawn = options.icon;
            if (options.icon_follows_taskbar_theme && drawn.image.empty() && drawn.background.a == 0.0f)
            {
                drawn.foreground = taskbar_uses_light_theme() ? colors::Black : colors::White;
            }
            next = create_icon(drawn, size, d2d.Get(), dwrite.Get());
        }
        if (next == nullptr)
        {
            next  = LoadIconW(nullptr, IDI_APPLICATION);
            owned = false;
        }
        const HICON previous       = icon;
        const bool  previous_owned = icon_owned;
        icon                       = next;
        icon_owned                 = owned;
        if (icon_added)
        {
            NOTIFYICONDATAW data = icon_data();
            data.uFlags          = NIF_ICON;
            data.hIcon           = icon;
            Shell_NotifyIconW(NIM_MODIFY, &data);
        }
        release_icon(previous, previous_owned); // after the shell has the new one
    }

    // Fails while there is no taskbar yet (an app started before Explorer); TaskbarCreated retries.
    void add_icon()
    {
        NOTIFYICONDATAW data  = icon_data();
        data.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = CallbackMessage;
        data.hIcon            = icon;
        wcsncpy_s(data.szTip, widen(options.tool_tip).c_str(), _TRUNCATE);
        if (!Shell_NotifyIconW(NIM_ADD, &data))
        {
            return;
        }
        // Version 4: NIN_SELECT/NIN_KEYSELECT/WM_CONTEXTMENU with the anchor in wParam, and the
        // tooltip shown by the shell (NIF_SHOWTIP) rather than a standard one.
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
        icon_added = true;
    }

    void remove_icon()
    {
        if (icon_added)
        {
            NOTIFYICONDATAW data = icon_data();
            Shell_NotifyIconW(NIM_DELETE, &data);
            icon_added = false;
        }
    }

    // ---- the flyout ----

    // `allows_system_backdrop` tells Fluent 2 apart: era packs and high contrast turn it off and
    // keep their own popup surface.
    void apply_background()
    {
        const Theme& theme = flyout->theme();
        if (options.background)
        {
            flyout->set_background(options.background);
        }
        else if (!theme.chrome.allows_system_backdrop)
        {
            flyout->set_background(theme.surfaces.menu);
        }
        else
        {
            // Sampled from Windows 11's Quick Settings (dark). The light value sits at the same step
            // between #F9F9F9 and #EEEEEE as #242424 does between #2C2C2C and #1C1C1C.
            flyout->set_background(theme.chrome.dark_titlebar ? Color::from_argb(0xFF242424) : Color::from_argb(0xFFF3F3F3));
        }
    }

    void apply_corners()
    {
        ChromeOverrides chrome = flyout->chrome();
        chrome.corner_preference.reset();
        if (flyout->theme().chrome.corner_preference == CornerPreference::Default)
        {
            chrome.corner_preference = CornerPreference::Round;
        }
        if (!(chrome == flyout->chrome()))
        {
            flyout->set_chrome(chrome);
        }
    }

    void show_flyout(bool keyboard)
    {
        if (!ensure_flyout_window())
        {
            return;
        }
        const HWND popup = flyout->native_handle();
        apply_corners();
        apply_background();

        RECT                 anchor{};
        NOTIFYICONIDENTIFIER id{};
        id.cbSize = sizeof(id);
        id.hWnd   = hwnd;
        id.uID    = IconId;
        if (!icon_added || FAILED(Shell_NotifyIconGetRect(&id, &anchor)))
        {
            POINT cursor{};
            GetCursorPos(&cursor);
            anchor = {cursor.x, cursor.y, cursor.x, cursor.y};
        }
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);

        // Onto the icon's monitor first, still hidden, so a change of DPI there reaches the host
        // (WM_DPICHANGED) before the content is measured and sized at it.
        SetWindowPos(popup, nullptr, monitor.rcWork.left, monitor.rcWork.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        const float scale = static_cast<float>(GetDpiForWindow(popup)) / 96.0f;

        const Rect work       = to_rect(monitor.rcWork);
        float      max_height = work.height / scale - 2.0f * options.flyout_margin;
        if (options.flyout_max_height > 0.0f)
        {
            max_height = std::min(max_height, options.flyout_max_height);
        }
        max_height   = std::max(max_height, 1.0f);
        float height = max_height;
        if (const Element* content = flyout->content(); content != nullptr && measurer)
        {
            const Size desired = content->measure(Size{flyout_width(), max_height}, *measurer, flyout->theme());
            height             = std::clamp(std::ceil(desired.height), 1.0f, max_height);
        }
        flyout->resize(Size{flyout_width(), height});

        RECT bounds{};
        GetWindowRect(popup, &bounds);
        const Point at = place_tray_flyout(to_rect(anchor),
                                           work,
                                           Size{static_cast<float>(bounds.right - bounds.left),
                                                static_cast<float>(bounds.bottom - bounds.top)},
                                           std::round(options.flyout_margin * scale));
        SetWindowPos(popup,
                     HWND_TOPMOST,
                     static_cast<int>(std::lround(at.x)),
                     static_cast<int>(std::lround(at.y)),
                     0,
                     0,
                     SWP_NOSIZE | SWP_NOACTIVATE);

        // Cloaked across the show, as a new window is: DWM composes nothing until the frame at the
        // new size has been painted.
        set_cloaked(popup, true);
        flyout->show();
        if (keyboard)
        {
            flyout->focus_content();
        }
        UpdateWindow(popup);
        set_cloaked(popup, false);
        shown_at = GetTickCount64();
    }

    void hide_flyout()
    {
        if (flyout != nullptr)
        {
            flyout->hide();
        }
    }

    // A click or Enter/Space on the icon.
    void toggle_from_icon(bool keyboard)
    {
        const ULONGLONG now    = GetTickCount64();
        const ULONGLONG window = GetDoubleClickTime();
        if (flyout == nullptr)
        {
            if (click)
            {
                click();
            }
            return;
        }
        if (flyout_open())
        {
            if (now - shown_at >= window)
            {
                hide_flyout();
            }
        }
        else if (now - dismissed_at >= window)
        {
            show_flyout(keyboard);
        }
    }

    // ---- the menu ----

    void show_menu(int x, int y)
    {
        const HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
        {
            return;
        }
        for (std::size_t i = 0; i < options.menu.size(); ++i)
        {
            const MenuItem&   item = options.menu[i];
            const std::string text = item.display_text();
            if (text.empty() || item.kind == MenuItemKind::Separator)
            {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                continue;
            }
            const UINT flags = MF_STRING | (item.is_checked() ? MF_CHECKED : 0u) | (item.is_enabled() ? 0u : MF_GRAYED);
            AppendMenuW(menu, flags, FirstMenuId + i, widen(text).c_str());
        }
        if (GetMenuItemCount(menu) <= 0)
        {
            DestroyMenu(menu);
            return;
        }

        // The menu follows the flyout's theme, as a context menu inside it would; with no flyout,
        // the taskbar's.
        set_app_dark_mode(flyout != nullptr ? merge_chrome(flyout->theme().chrome, flyout->chrome()).dark_titlebar
                                            : !taskbar_uses_light_theme());
        // "Shell_NotifyIcon": without the foreground, a click elsewhere does not close the menu;
        // without the WM_NULL after, the next right click can open and close it at once.
        SetForegroundWindow(hwnd);
        const UINT align  = GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0 ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
        const UINT chosen = static_cast<UINT>(
            TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align, x, y, hwnd, nullptr));
        DestroyMenu(menu);
        PostMessageW(hwnd, WM_NULL, 0, 0);

        if (chosen >= FirstMenuId && chosen - FirstMenuId < options.menu.size())
        {
            // As in a MenuFlyout: a toggle flips, a radio item unchecks its group, a two-way
            // `checked` is written, then the handlers run. A handler may call set_menu().
            invoke_menu_item(options.menu, options.menu[chosen - FirstMenuId], {});
        }
    }

    // ---- the icon's window ----

    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_NCCREATE)
        {
            const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self == nullptr)
        {
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        // Registered, so not a case label: Explorer (re)started, and every icon is gone.
        if (msg == self->taskbar_created && msg != 0)
        {
            self->icon_added = false;
            self->refresh_icon();
            self->add_icon();
            return 0;
        }

        switch (msg)
        {
            case CallbackMessage:
                // NOTIFYICON_VERSION_4: the event in LOWORD(lParam), the anchor point in wParam.
                switch (LOWORD(lp))
                {
                    case NIN_SELECT:
                        self->toggle_from_icon(false);
                        break;
                    case NIN_KEYSELECT:
                        self->toggle_from_icon(true);
                        break;
                    case WM_CONTEXTMENU:
                        self->show_menu(GET_X_LPARAM(wp), GET_Y_LPARAM(wp));
                        break;
                    case NIN_BALLOONUSERCLICK:
                        if (self->last_notification_click)
                        {
                            self->last_notification_click();
                        }
                        else if (self->notification_click)
                        {
                            self->notification_click();
                        }
                        else
                        {
                            self->show_flyout(false);
                        }
                        break;
                    default:
                        break;
                }
                return 0;

            case DismissMessage:
                // Posted from the flyout's WM_ACTIVATE; by now the activation may be back.
                if (self->flyout_open() && !self->flyout->active().get())
                {
                    self->hide_flyout();
                    self->dismissed_at = GetTickCount64();
                }
                return 0;

            case WM_SETTINGCHANGE:
                // The taskbar's light/dark switch. The flyout's own host follows the app theme.
                if (lp != 0 && std::wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0)
                {
                    self->refresh_icon();
                }
                return 0;

            case WM_DISPLAYCHANGE:
            case WM_DPICHANGED:
                self->refresh_icon(); // a new scale on the taskbar's monitor
                return 0;

            case WM_NCDESTROY:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                return DefWindowProcW(hwnd, msg, wp, lp);

            default:
                return DefWindowProcW(hwnd, msg, wp, lp);
        }
    }

    bool create_window()
    {
        static bool     class_ready = false;
        const HINSTANCE instance    = GetModuleHandleW(nullptr);
        if (!class_ready)
        {
            WNDCLASSEXW wc{};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = wnd_proc;
            wc.hInstance     = instance;
            wc.lpszClassName = TrayClassName;
            class_ready      = RegisterClassExW(&wc) != 0;
        }
        // Never shown: a zero-size popup has no taskbar button and no place in Alt+Tab.
        hwnd = CreateWindowExW(WS_EX_TOOLWINDOW,
                               TrayClassName,
                               widen(options.tool_tip).c_str(),
                               WS_POPUP,
                               0,
                               0,
                               0,
                               0,
                               nullptr,
                               nullptr,
                               instance,
                               this);
        if (hwnd == nullptr)
        {
            return false;
        }
        taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        // An elevated app still hears from the (unelevated) shell.
        ChangeWindowMessageFilterEx(hwnd, taskbar_created, MSGFLT_ALLOW, nullptr);
        return true;
    }

    void start()
    {
        // Before the icon's window exists; a window created later asks again, harmlessly.
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) &&
            SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                          __uuidof(IDWriteFactory),
                                          reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))))
        {
            measurer = std::make_unique<DWriteMeasurer>(dwrite.Get());
        }
        if (!create_window())
        {
            return;
        }
        if (flyout != nullptr)
        {
            // A flyout is its content, edge to edge.
            flyout->set_stretch_content(true);
        }
        refresh_icon();
        add_icon();
    }
};

TrayIcon::TrayIcon(TrayIconOptions options) : impl_(std::make_unique<Impl>(std::move(options)))
{
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::show_flyout()
{
    impl_->show_flyout(false);
}

void TrayIcon::hide_flyout()
{
    impl_->hide_flyout();
}

void TrayIcon::toggle_flyout()
{
    if (impl_->flyout_open())
    {
        impl_->hide_flyout();
    }
    else
    {
        impl_->show_flyout(false);
    }
}

void TrayIcon::set_tool_tip(std::string tool_tip)
{
    impl_->set_tool_tip(std::move(tool_tip));
}

void TrayIcon::set_icon(WindowIcon icon)
{
    impl_->set_icon(std::move(icon));
}

void TrayIcon::set_menu(std::vector<MenuItem> items)
{
    impl_->menu_checks.subscriptions.clear(); // they point into the items about to go
    impl_->options.menu = std::move(items);
    follow_menu_bindings(impl_->options.menu, impl_->menu_checks);
}

void TrayIcon::notify(Notification notification)
{
    if (!impl_->icon_added)
    {
        return;
    }
    impl_->last_notification_click = std::move(notification.on_click);
    NOTIFYICONDATAW data           = impl_->icon_data();
    data.uFlags                    = NIF_INFO;
    const std::wstring title_text  = widen(notification.title);
    const std::wstring body_text   = widen(notification.text);
    wcsncpy_s(data.szInfoTitle, title_text.c_str(), _TRUNCATE);
    wcsncpy_s(data.szInfo, body_text.c_str(), _TRUNCATE);
    switch (notification.icon)
    {
        case NotificationIcon::None:
            data.dwInfoFlags = NIIF_NONE;
            break;
        case NotificationIcon::Info:
            data.dwInfoFlags = NIIF_INFO;
            break;
        case NotificationIcon::Warning:
            data.dwInfoFlags = NIIF_WARNING;
            break;
        case NotificationIcon::Error:
            data.dwInfoFlags = NIIF_ERROR;
            break;
        case NotificationIcon::App:
            data.dwInfoFlags = NIIF_USER; // the tray icon, as no hBalloonIcon is given
            break;
    }
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

} // namespace ui

#endif // _WIN32
