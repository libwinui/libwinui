// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../../core/Trace.hpp"
#include "../../ui/OpenWindows.hpp"
#include "../../ui/WindowHost.hpp"
#include "../Backend.hpp"
#include "D2DRenderer.hpp"
#include "DCompCompositor.hpp"
#include "DWriteMeasurer.hpp"
#include "ProcAddress.hpp"
#include "Win32Apartment.hpp"
#include "Win32AutomationProvider.hpp"
#include "Win32ContextMenuHost.hpp"
#include "Win32Icon.hpp"
#include "Win32NativeHost.hpp"
#include "Win32Pointer.hpp"
#include "Win32SpellChecker.hpp"
#include "Win32String.hpp"
#include "Win32Taskbar.hpp"
#include "Win32TextAnalyzer.hpp"
#include "Win32ThemeSource.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DateFormat.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/NumberFormat.hpp"
#include "winui/core/PerfCounters.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/App.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/WindowOptions.hpp"
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/hosting/NativeControl.hpp" // WM_CONTEXTMENU routes a child HWND back to its Element
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <imm.h> // after <windows.h>, which it needs
#include <memory>
#include <optional>
#include <shellapi.h>        // SHAppBarMessage: the auto-hide taskbar gap
#include <shellscalingapi.h> // GetDpiForMonitor: WindowPlacement's DIPs
#include <shlobj.h>          // IDropTargetHelper, DROPDESCRIPTION
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <uxtheme.h> // SetWindowThemeAttribute: WindowStyle::caption_title/caption_icon
#include <vector>
#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM
#include <wrl/client.h>
#include <wtsapi32.h> // WTSRegisterSessionNotification: a remote session turns transparency off

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cwchar>

namespace ui::win32
{
namespace
{

using Microsoft::WRL::ComPtr;

// A zeroed Win32 struct with its size field set, as the API asks. `T value{sizeof(T)}` does the
// same but leaves the other fields to aggregate defaults, which clang warns about.
template <typename T, typename F>
T sized_win32_struct(F T::* size_field)
{
    T value{};
    value.*size_field = static_cast<F>(sizeof(T));
    return value;
}

constexpr wchar_t  Win32WindowClassName[] = L"libwinui.Window";
constexpr UINT_PTR TickTimerId            = 1;
constexpr UINT     TickIntervalMs         = 100; // plenty for a ~500ms tooltip delay
// How long the timer keeps running after the last tick a window wanted (Window::wants_ticks()).
// Long enough for every quiet wait in the controls (a caret blink, a tooltip delay, a hover before
// a submenu opens); after it an idle window gets no timer messages at all.
constexpr double IdleGraceMs = 3000.0;
// While a modal loop runs (see tick_from_timer()) the timer is the only clock there is.
constexpr UINT ModalTickIntervalMs = 15;

void tick_from_timer(HWND hwnd);

// QueryPerformanceCounter: GetTickCount64()'s ~15.6ms steps are too coarse for smooth animation.
double now_ms()
{
    static const LARGE_INTEGER frequency = []
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

class Win32WindowHost; // below: the WindowHost this file implements for a real HWND

// NumberBox's regional formatter uses the user locale's own data (LOCALE_NOUSEROVERRIDE), so a
// customized decimal symbol in the Region settings does not reach it either.
NumberFormatSymbols user_number_format()
{
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) == 0)
    {
        return NumberFormatSymbols::invariant();
    }
    if (wchar_t* sort = std::wcschr(locale, L'_'))
    {
        *sort = L'\0';
    }
    const auto read = [&](LCTYPE type) -> std::optional<std::wstring>
    {
        wchar_t buffer[32]{};
        if (GetLocaleInfoEx(locale, type | LOCALE_NOUSEROVERRIDE, buffer, 32) == 0)
        {
            return std::nullopt;
        }
        return std::wstring(buffer);
    };
    NumberFormatSymbols symbols;
    if (const auto decimal = read(LOCALE_SDECIMAL); decimal && !decimal->empty())
    {
        symbols.decimal_separator = narrow(*decimal);
    }
    if (const auto group = read(LOCALE_STHOUSAND))
    {
        symbols.grouping_separator = narrow(*group);
    }
    // LOCALE_SGROUPING: "3;0" is 1,234,567 (the 0 repeats the size before it), "3;2;0" is
    // 12,34,567, "3" groups only the first three.
    if (const auto grouping = read(LOCALE_SGROUPING))
    {
        symbols.group_sizes.clear();
        symbols.repeat_last_group = false;
        for (std::size_t i = 0; i < grouping->size();)
        {
            const std::size_t end  = std::min(grouping->find(L';', i), grouping->size());
            const int         size = std::wcstol(grouping->substr(i, end - i).c_str(), nullptr, 10);
            if (size == 0)
            {
                symbols.repeat_last_group = !symbols.group_sizes.empty();
                break;
            }
            symbols.group_sizes.push_back(size);
            i = end + 1;
        }
    }
    return symbols;
}

// The date and time controls' names and patterns, from the user's locale with the Region
// settings' own changes, as the system's date picker shows them.
DateFormatSymbols user_date_format()
{
    const auto read = [](LCTYPE type) -> std::optional<std::string>
    {
        wchar_t buffer[96]{};
        if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, type, buffer, 96) == 0 || buffer[0] == L'\0')
        {
            return std::nullopt;
        }
        return narrow(buffer);
    };
    DateFormatSymbols symbols;
    for (int i = 0; i < 12; ++i)
    {
        if (auto name = read(LOCALE_SMONTHNAME1 + i))
        {
            symbols.month_names[i] = std::move(*name);
        }
        if (auto name = read(LOCALE_SABBREVMONTHNAME1 + i))
        {
            symbols.abbreviated_month_names[i] = std::move(*name);
        }
    }
    // The locale's day lists start on Monday; DateFormatSymbols' start on Sunday.
    for (int i = 0; i < 7; ++i)
    {
        const std::size_t sunday_first = static_cast<std::size_t>((i + 1) % 7);
        if (auto name = read(LOCALE_SDAYNAME1 + i))
        {
            symbols.day_names[sunday_first] = std::move(*name);
        }
        if (auto name = read(LOCALE_SABBREVDAYNAME1 + i))
        {
            symbols.abbreviated_day_names[sunday_first] = std::move(*name);
        }
        if (auto name = read(LOCALE_SSHORTESTDAYNAME1 + i))
        {
            symbols.shortest_day_names[sunday_first] = std::move(*name);
        }
    }
    DWORD first = 0; // 0 is Monday
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT,
                        LOCALE_IFIRSTDAYOFWEEK | LOCALE_RETURN_NUMBER,
                        reinterpret_cast<LPWSTR>(&first),
                        sizeof(first) / sizeof(wchar_t)) != 0)
    {
        symbols.first_day_of_week = std::chrono::weekday{static_cast<unsigned>((first + 1) % 7)};
    }
    if (auto pattern = read(LOCALE_SSHORTDATE))
    {
        symbols.short_date_pattern = std::move(*pattern);
    }
    if (auto pattern = read(LOCALE_SLONGDATE))
    {
        symbols.long_date_pattern = std::move(*pattern);
    }
    if (auto pattern = read(LOCALE_SYEARMONTH))
    {
        symbols.year_month_pattern = std::move(*pattern);
    }
    if (auto pattern = read(LOCALE_SSHORTTIME))
    {
        symbols.short_time_pattern = std::move(*pattern);
    }
    if (auto designator = read(LOCALE_S1159))
    {
        symbols.am_designator = std::move(*designator);
    }
    if (auto designator = read(LOCALE_S2359))
    {
        symbols.pm_designator = std::move(*designator);
    }
    return symbols;
}

// An HICON this host created. reset() swaps first and destroys second, so the window has
// stopped using it by then.
struct OwnedIcon
{
    HICON handle = nullptr;

    OwnedIcon()                            = default;
    OwnedIcon(const OwnedIcon&)            = delete;
    OwnedIcon& operator=(const OwnedIcon&) = delete;
    ~OwnedIcon()
    {
        reset(nullptr);
    }
    void reset(HICON icon)
    {
        const HICON previous = handle;
        handle               = icon;
        if (previous != nullptr)
        {
            DestroyIcon(previous);
        }
    }
};

class DropTarget;

// `window` is borrowed: the app owns the Window, which may outlive this state. WM_DESTROY takes
// every service off it (`native_host` included) before the state is freed, so nothing in its tree
// calls into a host that is gone.
struct HostState
{
    ComPtr<ID2D1Factory>          d2d_factory;
    ComPtr<IDWriteFactory>        dwrite;
    ComPtr<ID2D1HwndRenderTarget> target;
    // Lives as long as `target` (its caches hold resources made on it), so declared after it.
    std::unique_ptr<D2DRenderer>    renderer;
    std::unique_ptr<DWriteMeasurer> measurer;

    // Null when DirectComposition is unavailable; controls then repaint without the fade.
    std::unique_ptr<LazyCompositor> compositor;

    std::unique_ptr<Win32NativeHost> native_host;


    // Null when WindowOptions::spell_check.enabled is false.
    std::unique_ptr<SpellChecker>          spell_checker; // made on first ask (spell_checker_link)
    std::unique_ptr<Win32ContextMenuHost>  context_menu;
    std::unique_ptr<Win32AutomationEvents> automation_events;
    // Registered with OLE for drags from other apps; null where WM_DROPFILES stands in for it.
    ComPtr<DropTarget> drop_target;

    // Re-queried on WM_SETTINGCHANGE and WM_DWMCOLORIZATIONCOLORCHANGED, and for the
    // transparency switch (OsThemeSnapshot::transparency_enabled) when Energy Saver or a remote
    // session comes or goes, which no settings message reports.
    Win32ThemeSource               theme_source;
    std::optional<OsThemeSnapshot> reported_os_theme; // the last one apply_theme() passed on
    std::vector<HPOWERNOTIFY>      power_notifications;

    // `backdrop_active` is false before Windows 11 22H2 and whenever the theme declines a backdrop;
    // apply_chrome() re-settles it on every theme change.
    Backdrop backdrop_request = Backdrop::None;
    bool     backdrop_active  = false;
    // Latched at the first refusal: on any pre-22H2 OS DWM never accepts the attribute, and
    // without this every theme change would retry it forever.
    bool backdrop_unsupported = false;

    // The desktop blurred behind a glass frame (Window::wants_blur_behind()). Latched off at the
    // first refusal, like the backdrop.
    bool blur_behind             = false;
    bool blur_behind_unsupported = false;

    // The window region cutting the frame's rounded top corners (Window::frame_corner_cut()), and
    // what it was cut for; no region while `corner_cut` is empty.
    std::vector<std::uint8_t> corner_cut;
    SIZE                      corner_cut_size{};
    UINT                      corner_cut_dpi = 0;

    // Visual styles are off while the theme draws the frame (sync_frame_theming()).
    bool frame_unthemed = false;

    // A theme the app installs arrives through no window message, so the frame attributes are
    // re-pushed when theme_revision() moves. Starts at a value pin_theme() never produces.
    unsigned chrome_revision = static_cast<unsigned>(-1);

    UINT dpi = 96;
    // Where sync_caret() last put the system caret, in client pixels; unset while there is none.
    std::optional<RECT> caret;

    // Flipped false at WM_DESTROY, before `window` is destroyed.
    std::shared_ptr<AutomationLifetime> automation_lifetime = std::make_shared<AutomationLifetime>();

    // Counted by hand: Win32 sends WM_LBUTTONDBLCLK only for the second click and nothing for a
    // third, so TextBox's triple-click needs its own streak.
    POINT last_click_pt_px{};
    DWORD last_click_time_ms = 0;
    int   click_streak       = 0;
    // The pointer is over a drawn maximize button, which answers HTMAXBUTTON, and its WM_NC* mouse
    // messages are being forwarded as pointer events.
    bool over_maximize_button = false;
    // An Alt chord the tree used: the WM_SYSCHAR that follows it is dropped.
    bool swallow_syschar = false;

    // ---- window state and frame ----
    HWND hwnd = nullptr;
    // The live frame, mirrored from Window::style()/chrome() by Win32WindowHost. Kept here too
    // because WM_NCCALCSIZE and friends run during CreateWindowExW, before the host exists.
    WindowStyle     style{};
    ChromeOverrides chrome_overrides{};
    Size            min_size{};
    Size            max_size{};
    // Windows has no full-screen state: the flag, and the placement to go back to (maximized too).
    bool            fullscreen               = false;
    bool            taskbar_light            = false; // what the taskbar's icons were last drawn for
    WINDOWPLACEMENT pre_fullscreen_placement = sized_win32_struct(&WINDOWPLACEMENT::length);
    // The frame, window rect minus client area in pixels, the last time the window was restored.
    // A size set while maximized, minimized or full screen goes into the restored rect with it.
    std::optional<SIZE> restored_frame;

    // ---- more than one window (Window::show()) ----
    // A modal window's owner is re-enabled as the window closes, before it is destroyed, so the
    // activation goes back to the owner rather than to another app's window.
    HWND owner          = nullptr;
    bool modal          = false;
    bool owner_released = false;
    // WindowOptions::close_hides: WM_CLOSE hides rather than destroys.
    bool close_hides = false;
    // WM_DESTROY has run: the run() loop destroys this state at its next safe point.
    bool destroyed = false;
    // Its tick timer's period in ms (0: off), when it goes off next, and whether it has gone off
    // since it was set (see tick_all()).
    UINT   timer_ms      = 0;
    double timer_next_ms = 0.0;
    bool   timer_fired   = false;

    UINT                          taskbar_button_created = 0; // RegisterWindowMessageW(L"TaskbarButtonCreated")
    Utf16Decoder                  char_input;                 // WM_CHAR's UTF-16 units, paired into code points
    std::unique_ptr<Win32Taskbar> taskbar;
    OwnedIcon                     big_icon;
    OwnedIcon                     small_icon;

    // Before `window`, which holds a raw pointer to it.
    std::unique_ptr<Win32WindowHost> window_host;

    // Neither applies while full screen: its WS_POPUP frame has no caption to stand in for.
    [[nodiscard]] bool extends_into_title_bar() const
    {
        return !fullscreen && style.frame == Frame::Standard && style.extend_content_into_title_bar;
    }
    // The client area covers (some of) what would be non-client: WM_NCCALCSIZE is ours.
    [[nodiscard]] bool collapses_nonclient() const
    {
        return !fullscreen && (style.frame == Frame::Borderless || extends_into_title_bar());
    }
    // Content decides where the window drags from: WM_NCHITTEST asks the logical Window.
    [[nodiscard]] bool content_is_caption() const
    {
        return !fullscreen && (style.frame != Frame::Standard || extends_into_title_bar());
    }

    Window* window = nullptr; // borrowed: the app owns it; null once it was destroyed

    float to_dips(int px) const
    {
        return static_cast<float>(px) * 96.0f / static_cast<float>(dpi);
    }
    Point pointer_dips(LPARAM lp) const
    {
        return {to_dips(GET_X_LPARAM(lp)), to_dips(GET_Y_LPARAM(lp))};
    }
    Point client_point_dips(POINT client_px) const
    {
        return {to_dips(client_px.x), to_dips(client_px.y)};
    }
};

// The window run() was handed, whose closing ends the loop; nullptr outside run(). Defined with
// the registry of open windows, at the end of this file.
HostState* main_host();

// DIPs scrolled per wheel notch (WHEEL_DELTA units). Roughly three text lines, matching
// common desktop conventions.
constexpr float WheelLineHeight = 48.0f;

bool key_down(int vk)
{
    return (GetKeyState(vk) & 0x8000) != 0;
}

// The keys held now. Keyboard messages carry no modifier bits, so all of them go through
// GetKeyState().
ModifierKeys key_modifiers()
{
    return ModifierKeys{.ctrl  = key_down(VK_CONTROL),
                        .shift = key_down(VK_SHIFT),
                        .alt   = key_down(VK_MENU),
                        .win   = key_down(VK_LWIN) || key_down(VK_RWIN)};
}

// A mouse message's own MK_SHIFT/MK_CONTROL bits, the state when it was sent; Alt and Win have
// no bit there and are read as they are now.
ModifierKeys mouse_modifiers(WPARAM wp)
{
    ModifierKeys mods = key_modifiers();
    mods.ctrl         = (wp & MK_CONTROL) != 0;
    mods.shift        = (wp & MK_SHIFT) != 0;
    return mods;
}

// The Alt chords the system keeps: closing the window, its system menu, switching windows.
bool system_alt_chord(WPARAM vk)
{
    return vk == VK_F4 || vk == VK_SPACE || vk == VK_TAB || vk == VK_ESCAPE;
}

// The pen or touch frame of the last WM_POINTER* message this thread saw. The mouse message
// Windows makes of it comes next, possibly to another of this thread's windows (the one holding
// capture), so it is kept per thread rather than per window.
std::optional<PointerSample>& last_pointer_sample()
{
    static std::optional<PointerSample> sample;
    return sample;
}

// A mouse message's device, pressure, tilt and pen buttons.
void apply_pointer_source(PointerEvent& e)
{
    apply_pointer_source(e, static_cast<std::uintptr_t>(GetMessageExtraInfo()), last_pointer_sample());
}

// 1 is a plain click, 2 a double, 3+ triple and beyond. It never wraps, so a slow fourth
// click still counts as at least triple.
int bump_click_streak(HostState& s, POINT pt_px)
{
    const DWORD now         = GetMessageTime();
    const bool  within_time = (now - s.last_click_time_ms) <= GetDoubleClickTime();
    const bool  within_dist = std::abs(pt_px.x - s.last_click_pt_px.x) <= GetSystemMetrics(SM_CXDOUBLECLK) &&
                              std::abs(pt_px.y - s.last_click_pt_px.y) <= GetSystemMetrics(SM_CYDOUBLECLK);

    s.click_streak       = (within_time && within_dist) ? s.click_streak + 1 : 1;
    s.last_click_time_ms = now;
    s.last_click_pt_px   = pt_px;
    return s.click_streak;
}

Key translate_key(WPARAM vk)
{
    switch (vk)
    {
        case VK_RETURN:
            return Key::Enter;
        case VK_SPACE:
            return Key::Space;
        case VK_TAB:
            return Key::Tab;
        case VK_ESCAPE:
            return Key::Escape;
        case VK_UP:
            return Key::ArrowUp;
        case VK_DOWN:
            return Key::ArrowDown;
        case VK_LEFT:
            return Key::ArrowLeft;
        case VK_RIGHT:
            return Key::ArrowRight;
        case VK_BACK:
            return Key::Backspace;
        case VK_DELETE:
            return Key::Delete;
        case VK_HOME:
            return Key::Home;
        case VK_END:
            return Key::End;
        case VK_PRIOR:
            return Key::PageUp;
        case VK_NEXT:
            return Key::PageDown;
        case VK_F4:
            return Key::F4;
        case VK_F11:
            return Key::F11;
        case 'A':
            return Key::A;
        case 'C':
            return Key::C;
        case 'V':
            return Key::V;
        case 'X':
            return Key::X;
        case 'Z':
            return Key::Z;
        case 'T':
            return Key::T;
        case 'W':
            return Key::W;
        case '1':
            return Key::Number1;
        case '2':
            return Key::Number2;
        case '3':
            return Key::Number3;
        case '4':
            return Key::Number4;
        case '5':
            return Key::Number5;
        case '6':
            return Key::Number6;
        case '7':
            return Key::Number7;
        case '8':
            return Key::Number8;
        case '9':
            return Key::Number9;
        case '0':
            return Key::Number0;
        case 'B':
            return Key::B;
        case 'D':
            return Key::D;
        case 'E':
            return Key::E;
        case 'F':
            return Key::F;
        case 'G':
            return Key::G;
        case 'H':
            return Key::H;
        case 'I':
            return Key::I;
        case 'J':
            return Key::J;
        case 'K':
            return Key::K;
        case 'L':
            return Key::L;
        case 'M':
            return Key::M;
        case 'N':
            return Key::N;
        case 'O':
            return Key::O;
        case 'P':
            return Key::P;
        case 'Q':
            return Key::Q;
        case 'R':
            return Key::R;
        case 'S':
            return Key::S;
        case 'U':
            return Key::U;
        case 'Y':
            return Key::Y;
        case VK_F1:
            return Key::F1;
        case VK_F2:
            return Key::F2;
        case VK_F3:
            return Key::F3;
        case VK_F5:
            return Key::F5;
        case VK_F6:
            return Key::F6;
        case VK_F7:
            return Key::F7;
        case VK_F8:
            return Key::F8;
        case VK_F9:
            return Key::F9;
        case VK_F10:
            return Key::F10;
        case VK_F12:
            return Key::F12;
        case VK_INSERT:
            return Key::Insert;
        case VK_ADD:
            return Key::Add;
        case VK_SUBTRACT:
            return Key::Subtract;
        case VK_MULTIPLY:
            return Key::Multiply;
        case VK_DIVIDE:
            return Key::Divide;
        case VK_OEM_PLUS:
            return Key::Plus;
        case VK_OEM_MINUS:
            return Key::Minus;
        case VK_OEM_COMMA:
            return Key::Comma;
        case VK_OEM_PERIOD:
            return Key::Period;
        case VK_OEM_2:
            return Key::Slash;
        default:
            return Key::Unknown;
    }
}

// ---- frame metrics ----

int metric(int index, const HostState& s)
{
    return GetSystemMetricsForDpi(index, s.dpi);
}

// A maximized window's sizing border sits off-screen on every side. With the non-client area
// collapsed the client overshoots by as much, so it is inset by this.
POINT frame_thickness(const HostState& s)
{
    // A frame the theme draws sizes from its own border, as the era's sizing frame did.
    if (s.window && ui::testing::seams(*s.window).drawn_frame_border() > 0.0f && s.style.resizable)
    {
        const int band = static_cast<int>(
            std::lround(ui::testing::seams(*s.window).drawn_frame_border() * static_cast<float>(s.dpi) / 96.0f));
        return {band, band};
    }
    const int padding = metric(SM_CXPADDEDBORDER, s);
    if (s.style.resizable)
    {
        return {metric(SM_CXSIZEFRAME, s) + padding, metric(SM_CYSIZEFRAME, s) + padding};
    }
    return {metric(SM_CXFIXEDFRAME, s) + padding, metric(SM_CYFIXEDFRAME, s) + padding};
}

// The client area's offset inside the window rect, which DWM's window-relative rects (the
// caption button bounds) are shifted by to land in client coordinates.
POINT client_origin_in_window(HWND hwnd)
{
    POINT origin{0, 0};
    ClientToScreen(hwnd, &origin);
    RECT window_rect{};
    GetWindowRect(hwnd, &window_rect);
    return {origin.x - window_rect.left, origin.y - window_rect.top};
}

// DWM may not know the bounds before the first paint, so callers fall back to system metrics
// and re-ask on every size change and paint.
std::optional<RECT> caption_button_bounds(HWND hwnd)
{
    RECT bounds{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &bounds, sizeof(bounds))) ||
        bounds.right <= bounds.left)
    {
        return std::nullopt;
    }
    const POINT origin = client_origin_in_window(hwnd);
    OffsetRect(&bounds, -origin.x, -origin.y);
    return bounds;
}

// The caption row left to DWM: the largest of the OS caption (plus the top sizing band while
// restored), the caption buttons and the app's WindowStyle::title_bar_height.
int caption_band_px(HWND hwnd, const HostState& s)
{
    const int app_band = static_cast<int>(
        std::ceil(std::max(0.0f, s.style.title_bar_height) * static_cast<float>(s.dpi) / 96.0f));
    if (!s.style.caption_buttons)
    {
        return app_band; // Collapsed: no buttons to leave room for, only the app's own row
    }
    int band = metric(SM_CYCAPTION, s);
    if (!IsZoomed(hwnd))
    {
        band += frame_thickness(s).y;
    }
    if (const std::optional<RECT> buttons = caption_button_bounds(hwnd))
    {
        band = std::max(band, static_cast<int>(buttons->bottom));
    }
    return std::max(band, app_band);
}

// Left-to-right only for now: the buttons sit on the right, nothing is reserved on the left.
void update_caption_insets(HWND hwnd, HostState& s)
{
    if (!s.window)
    {
        return;
    }
    if (!s.extends_into_title_bar())
    {
        ui::testing::seams(*s.window).report_caption_insets({});
        return;
    }
    if (IsIconic(hwnd))
    {
        return; // nothing to measure; keep the last answer for the restore
    }
    RECT client{};
    GetClientRect(hwnd, &client);
    int right_px = s.style.caption_buttons ? 3 * metric(SM_CXSIZE, s) : 0;
    if (const std::optional<RECT> buttons = caption_button_bounds(hwnd); buttons && s.style.caption_buttons)
    {
        right_px = client.right - buttons->left;
    }
    ui::testing::seams(*s.window).report_caption_insets(
        {0.0f, s.to_dips(std::max(0, right_px)), s.to_dips(caption_band_px(hwnd, s))});
}

// The last call wins, so all the cases live here: a blur behind a glass frame one row, a backdrop
// the whole client, an extended title bar the caption band, and a borderless frame 1px so DWM
// keeps the shadow.
void apply_frame_margins(HWND hwnd, const HostState& s)
{
    MARGINS margins{0, 0, 0, 0};
    if (s.blur_behind)
    {
        // Any wider extension puts DWM's opaque frame backdrop under the whole window, hiding the
        // blur, and the full one also brings DWM's own caption buttons. One row at the top keeps
        // the shadow without either.
        margins.cyTopHeight = 1;
    }
    else if (s.backdrop_active)
    {
        margins = {-1, -1, -1, -1};
    }
    else if (s.extends_into_title_bar())
    {
        margins.cyTopHeight = caption_band_px(hwnd, s);
    }
    else if (!s.fullscreen && s.style.frame == Frame::Borderless)
    {
        margins = {1, 1, 1, 1};
    }
    DwmExtendFrameIntoClientArea(hwnd, &margins);
}

DWM_SYSTEMBACKDROP_TYPE backdrop_type(Backdrop backdrop)
{
    switch (backdrop)
    {
        case Backdrop::Mica:
            return DWMSBT_MAINWINDOW;
        case Backdrop::MicaAlt:
            return DWMSBT_TABBEDWINDOW;
        case Backdrop::Acrylic:
            return DWMSBT_TRANSIENTWINDOW;
        case Backdrop::None:
            break;
    }
    return DWMSBT_NONE;
}

// DWMWA_SYSTEMBACKDROP_TYPE needs Windows 11 22H2. Elsewhere this fails and the client area
// stays un-extended, so the window never ends up transparent with nothing behind it.
bool try_enable_backdrop(HWND hwnd, Backdrop backdrop)
{
    const DWM_SYSTEMBACKDROP_TYPE type = backdrop_type(backdrop);
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type)));
}

// The margins must come back too: DWMSBT_NONE alone leaves the frame extended, and any
// transparent control pixel would show the desktop.
void disable_backdrop(HWND hwnd)
{
    const DWM_SYSTEMBACKDROP_TYPE type = DWMSBT_NONE;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
}

COLORREF to_colorref(const Color& c)
{
    auto q = [](float v)
    {
        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return static_cast<BYTE>(std::lround(v * 255.0f));
    };
    return RGB(q(c.r), q(c.g), q(c.b));
}

ChromeHints effective_chrome(const HostState& s)
{
    return merge_chrome(s.window->theme().chrome, s.chrome_overrides);
}

// DWMWA_COLOR_DEFAULT / DWMWA_COLOR_NONE, spelled out because older SDK headers lack them.
constexpr COLORREF DwmColorDefault = 0xFFFFFFFF;
constexpr COLORREF DwmColorNone    = 0xFFFFFFFE;

// Pushed every time: a FrameColor that follows the OS resets to DWMWA_COLOR_DEFAULT, so the
// previous theme's caption color (a Windows 9x navy) does not outlive a theme switch.
void push_frame_color(HWND hwnd, DWMWINDOWATTRIBUTE attribute, const FrameColor& color, bool none_allowed)
{
    COLORREF value = DwmColorDefault;
    switch (color.kind())
    {
        case FrameColorKind::Follow:
        case FrameColorKind::SystemDefault:
            break;
        case FrameColorKind::None:
            value = none_allowed ? DwmColorNone : DwmColorDefault;
            break;
        case FrameColorKind::Custom:
            value = to_colorref(*color);
            break;
    }
    DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
}

// The caption's background and title colors for the activation state the window is in: the
// inactive pair when the window is not the active one and they say anything, else the active pair.
void push_caption_colors(HWND hwnd, const ChromeHints& chrome, bool active)
{
    const auto pick = [active](const FrameColor& inactive, const FrameColor& normal) -> FrameColor
    { return !active && inactive.kind() != FrameColorKind::Follow ? inactive : normal; };
    push_frame_color(hwnd, DWMWA_CAPTION_COLOR, pick(chrome.inactive_caption_color, chrome.caption_color), false);
    push_frame_color(hwnd, DWMWA_TEXT_COLOR, pick(chrome.inactive_caption_text_color, chrome.caption_text_color), false);
}

// Cuts the rounded top corners of the frame the theme draws out of the window, the way uxtheme
// shaped an XP window: a region, so the cut pixels are the desktop's and take no clicks. A region
// costs the DWM shadow, which XP windows never had. Rebuilt on a resize, a DPI or theme change,
// and dropped while maximized.
void sync_corner_region(HWND hwnd, HostState& s)
{
    const std::span<const std::uint8_t> cut = s.window ? ui::testing::seams(*s.window).frame_corner_cut()
                                                       : std::span<const std::uint8_t>{};
    RECT                                rc{};
    GetWindowRect(hwnd, &rc);
    const SIZE size{rc.right - rc.left, rc.bottom - rc.top};
    if (cut.empty())
    {
        if (!s.corner_cut.empty())
        {
            SetWindowRgn(hwnd, nullptr, TRUE);
            s.corner_cut.clear();
        }
        return;
    }
    if (std::ranges::equal(cut, s.corner_cut) && size.cx == s.corner_cut_size.cx && size.cy == s.corner_cut_size.cy &&
        s.dpi == s.corner_cut_dpi)
    {
        return;
    }

    // Each device row takes the cut of the 96 DPI row it falls in, scaled.
    const float scale  = static_cast<float>(s.dpi) / 96.0f;
    const int   rows   = static_cast<int>(std::lround(static_cast<float>(cut.size()) * scale));
    HRGN        region = CreateRectRgn(0, 0, size.cx, size.cy);
    for (int y = 0; y < rows; ++y)
    {
        const std::size_t row   = (std::min)(cut.size() - 1, static_cast<std::size_t>(static_cast<float>(y) / scale));
        const int         width = static_cast<int>(std::lround(static_cast<float>(cut[row]) * scale));
        for (const int left : {0, static_cast<int>(size.cx) - width})
        {
            HRGN corner = CreateRectRgn(left, y, left + width, y + 1);
            CombineRgn(region, region, corner, RGN_DIFF);
            DeleteObject(corner);
        }
    }
    if (!SetWindowRgn(hwnd, region, TRUE))
    {
        DeleteObject(region); // the system owns it only once SetWindowRgn succeeds
        return;
    }
    s.corner_cut.assign(cut.begin(), cut.end());
    s.corner_cut_size = size;
    s.corner_cut_dpi  = s.dpi;
}

void apply_chrome(HWND hwnd, HostState& s)
{
    if (!s.window)
    {
        return;
    }
    const ChromeHints chrome = effective_chrome(s);

    // DWM also takes Mica's light or dark tint from this; left unset, a dark window gets light Mica.
    const BOOL dark = chrome.dark_titlebar ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_DEFAULT;
    // A frame the theme draws is square, whatever the theme asks DWM for.
    const CornerPreference corner_preference = s.window->style().frame == Frame::Themed && s.style.frame == Frame::Borderless
                                                   ? CornerPreference::DoNotRound
                                                   : chrome.corner_preference;
    switch (corner_preference)
    {
        case CornerPreference::Default:
            // DWM's default for a popup is square, so a Frame::None window that wants the Windows 11
            // look has to name it (WindowStyle::round_popup_corners).
            corner = (s.style.frame == Frame::None && s.style.round_popup_corners) ? DWMWCP_ROUND : DWMWCP_DEFAULT;
            break;
        case CornerPreference::DoNotRound:
            corner = DWMWCP_DONOTROUND;
            break;
        case CornerPreference::Round:
            corner = DWMWCP_ROUND;
            break;
        case CornerPreference::RoundSmall:
            corner = DWMWCP_ROUNDSMALL;
            break;
    }
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    // DWM documents DWMWA_COLOR_NONE for the border alone.
    push_caption_colors(hwnd, chrome, s.window->active().get());
    push_frame_color(hwnd, DWMWA_BORDER_COLOR, chrome.border_color, true);

    // Settled per theme, not once at WM_CREATE: a theme can decline Backdrop::Mica (every era
    // before Windows 11, and high contrast) and then gets the opaque colors.background fill.
    if (s.backdrop_request != Backdrop::None)
    {
        if (chrome.allows_system_backdrop)
        {
            if (!s.backdrop_active && !s.backdrop_unsupported)
            {
                s.backdrop_active      = try_enable_backdrop(hwnd, s.backdrop_request);
                s.backdrop_unsupported = !s.backdrop_active;
            }
        }
        else if (s.backdrop_active)
        {
            disable_backdrop(hwnd);
            s.backdrop_active = false;
        }
    }
    apply_frame_margins(hwnd, s);
    sync_corner_region(hwnd, s);

    s.chrome_revision = s.window->theme_revision();
}

// Aero glass's backdrop: user32's undocumented accent policy, which blurs whatever is behind the
// window. AccentFlags stay 0: with the tint flag (2) Windows 11 draws the blur nearly black
// whatever the color, so the frame paints its own tint over the blur. Resolved at run time and
// skipped when absent.
struct AccentPolicy
{
    int  state;
    int  flags;
    UINT gradient_color;
    int  animation_id;
};
struct WindowCompositionAttributeData
{
    int    attribute; // 19, WCA_ACCENT_POLICY
    void*  data;
    SIZE_T size;
};
using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WindowCompositionAttributeData*);

bool set_accent_blur(HWND hwnd, bool on)
{
    static const auto fn = proc_address<SetWindowCompositionAttributeFn>(GetModuleHandleW(L"user32.dll"),
                                                                         "SetWindowCompositionAttribute");
    if (fn == nullptr)
    {
        return false;
    }
    AccentPolicy                   policy{on ? 3 : 0, 0, 0, 0}; // ACCENT_ENABLE_BLURBEHIND / ACCENT_DISABLED
    WindowCompositionAttributeData data{19, &policy, sizeof(policy)};
    return fn(hwnd, &data) != FALSE;
}

// Called before every paint: full screen, the transparency switch and a theme change all move
// what the window wants, and a match is one compare.
void sync_blur_behind(HWND hwnd, HostState& s)
{
    const bool want = s.window && ui::testing::seams(*s.window).wants_blur_behind() && !s.blur_behind_unsupported;
    if (want == s.blur_behind)
    {
        return;
    }
    const bool ok = set_accent_blur(hwnd, want);
    if (want && !ok)
    {
        s.blur_behind_unsupported = true;
    }
    s.blur_behind = want && ok;
    apply_frame_margins(hwnd, s);
    ui::testing::seams(*s.window).report_blur_behind(s.blur_behind);
    sync_corner_region(hwnd, s); // glass has none, the opaque frame does
}

// Re-pushes the frame attributes only when the theme changed, usually one integer compare.
// WM_SETTINGCHANGE calls apply_chrome() directly to update the frame in the same message.
void sync_chrome(HWND hwnd, HostState& s)
{
    if (s.window && s.window->theme_revision() != s.chrome_revision)
    {
        apply_chrome(hwnd, s);
    }
}

// A frame for a change inside the tree: the window only has to repaint what changed, which
// Window::take_damage() works out, so the update region is one pixel the OS can tell from its own
// requests (a resize, an uncovered window), which repaint everything. The bottom-left pixel, since
// a rounded frame cuts the top corners out of the window and the OS drops an invalidation there.
RECT frame_request(HWND hwnd)
{
    RECT client{};
    GetClientRect(hwnd, &client);
    return RECT{0, std::max(0L, client.bottom - 1), 1, std::max(1L, client.bottom)};
}

void request_frame(HWND hwnd)
{
    const RECT pixel = frame_request(hwnd);
    InvalidateRect(hwnd, &pixel, FALSE);
    if (!GetUpdateRect(hwnd, nullptr, FALSE))
    {
        // A native child over that pixel (WS_CLIPCHILDREN) dropped it too: ask for all of it.
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

bool ensure_render_target(HWND hwnd, HostState& s)
{
    if (s.target)
    {
        return true;
    }
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left), static_cast<UINT32>(rc.bottom - rc.top));
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                    static_cast<float>(s.dpi),
                                                    static_cast<float>(s.dpi));
    // Retained: a frame that repaints only what changed leaves the rest of the last one in place.
    const auto hprops = D2D1::HwndRenderTargetProperties(hwnd, size, D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS);
    return SUCCEEDED(s.d2d_factory->CreateHwndRenderTarget(props, hprops, &s.target));
}

// GUID_POWER_SAVING_STATUS, and GUID_ENERGY_SAVER_STATUS, which replaces it on Windows 11 24H2
// and later. Spelled out because older SDKs lack the second; an OS that does not know a GUID
// fails its registration, which is harmless.
constexpr GUID PowerSavingStatus{0xe00958c0, 0xc213, 0x4ace, {0xac, 0x77, 0xfe, 0xcc, 0xed, 0x2e, 0xee, 0xa5}};
constexpr GUID EnergySaverStatus{0x550e8400, 0xe29b, 0x41d4, {0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00}};

void register_transparency_notifications(HWND hwnd, HostState& s)
{
    for (const GUID& setting : {PowerSavingStatus, EnergySaverStatus})
    {
        if (HPOWERNOTIFY handle = RegisterPowerSettingNotification(hwnd, &setting, DEVICE_NOTIFY_WINDOW_HANDLE))
        {
            s.power_notifications.push_back(handle);
        }
    }
    WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
}

void unregister_transparency_notifications(HWND hwnd, HostState& s)
{
    for (HPOWERNOTIFY handle : s.power_notifications)
    {
        UnregisterPowerSettingNotification(handle);
    }
    s.power_notifications.clear();
    WTSUnRegisterSessionNotification(hwnd);
}

// Reports the OS theme state to the window if it changed since the last report, and says whether
// it did. Registering for the power settings sends their current values straight away, which
// changes nothing.
bool apply_theme(HostState& s)
{
    if (!s.window)
    {
        return false;
    }
    const OsThemeSnapshot os = s.theme_source.snapshot();
    if (s.reported_os_theme == os)
    {
        return false;
    }
    s.reported_os_theme = os;
    ui::testing::seams(*s.window).report_os_theme(os);
    return true;
}

void layout_window(HWND hwnd, HostState& s)
{
    if (!s.window || !s.measurer)
    {
        return;
    }
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const Size client{s.to_dips(rc.right - rc.left), s.to_dips(rc.bottom - rc.top)};
    s.window->layout(client, *s.measurer);
}

void paint(HWND hwnd, HostState& s, bool everything)
{
    const bool fresh_target = !s.target;
    if (!ensure_render_target(hwnd, s))
    {
        return;
    }

    // Before the first pixel: a theme the app installed since the last frame also changes
    // what DWM draws behind and around this one.
    sync_chrome(hwnd, s);
    sync_blur_behind(hwnd, s);

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const Rect client{0.0f, 0.0f, s.to_dips(rc.right - rc.left), s.to_dips(rc.bottom - rc.top)};
    // Always taken, so the window compares each frame with the one before it.
    std::vector<Rect> damage = s.window ? s.window->take_damage() : std::vector<Rect>{};
    if (everything || fresh_target)
    {
        damage.assign(1, client);
    }
    if (damage.empty())
    {
        // Nothing on screen would change, but wait for the next composition all the same: a
        // presented frame waits for it, and without the wait an animation scrolled out of view
        // (dirty every tick, drawing nothing) turns the loop into tick, paint, tick at full speed.
        DwmFlush();
        return;
    }
    if (!s.renderer)
    {
        s.renderer = std::make_unique<D2DRenderer>(s.target.Get(), s.dwrite.Get());
    }

    s.target->BeginDraw();
    s.renderer->begin_frame();
    const bool        transparent = s.backdrop_active || s.blur_behind;
    const D2D1_RECT_F band = D2D1::RectF(0.0f,
                                         0.0f,
                                         client.width,
                                         s.extends_into_title_bar() ? s.to_dips(caption_band_px(hwnd, s)) : 0.0f);
    for (const Rect& area : damage)
    {
        // Whole device pixels, so the edge of one frame's area never blends with the last frame's.
        const float scale = static_cast<float>(s.dpi) / 96.0f;
        const float left  = std::floor(area.left() * scale) / scale;
        const float top   = std::floor(area.top() * scale) / scale;
        const float right = std::ceil(area.right() * scale) / scale;
        const float bot   = std::ceil(area.bottom() * scale) / scale;
        const Rect  clip{left, top, right - left, bot - top};
        s.target->PushAxisAlignedClip(D2D1::RectF(clip.left(), clip.top(), clip.right(), clip.bottom()),
                                      D2D1_ANTIALIAS_MODE_ALIASED);
        if (transparent)
        {
            // Leave the backdrop transparent instead of filling colors.background; DWM
            // composites its Mica material behind it.
            s.target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        }
        else
        {
            const Color bg = s.window ? s.window->background() : colors::White;
            s.target->Clear(D2D1::ColorF(bg.r, bg.g, bg.b, bg.a));
            if (band.bottom > 0.0f)
            {
                // The caption row stays transparent, so DWM's own caption background and buttons
                // show under the app's title row.
                s.target->PushAxisAlignedClip(band, D2D1_ANTIALIAS_MODE_ALIASED);
                s.target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
                s.target->PopAxisAlignedClip();
            }
        }
        // Only the clear is clipped here: Window::render() clips through the renderer, which lifts
        // its clips while in-app acrylic copies what is under it.
        s.target->PopAxisAlignedClip();
        if (s.window)
        {
            if (damage.size() == 1 && clip.left() <= 0.0f && clip.top() <= 0.0f && clip.right() >= client.width &&
                clip.bottom() >= client.height)
            {
                s.window->render(*s.renderer);
            }
            else
            {
                s.window->render(*s.renderer, clip);
            }
        }
    }

    LIBWINUI_TRACE_SPAN("present");
    if (s.target->EndDraw() == D2DERR_RECREATE_TARGET)
    {
        // Device lost: target and renderer are rebuilt on the next paint.
        s.renderer.reset();
        s.target.Reset();
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    detail::note_milestone(detail::Milestone::FirstFrame);
}

// ---- style bits ----

// WS_CLIPCHILDREN keeps D2D repaints off NativeControl child windows, or they flicker. Borderless
// keeps the caption bits so the shell treats it as a real window (snap, Win+arrows, shadow).
DWORD style_bits(const WindowStyle& style)
{
    DWORD bits = WS_CLIPCHILDREN;
    if (style.frame == Frame::None)
    {
        bits |= WS_POPUP | WS_SYSMENU;
    }
    else
    {
        // WS_SYSMENU is what puts the minimize/maximize/close buttons on a caption at all, on a
        // borderless one too, where DWM draws them over a backdrop that fills the client area.
        bits |= WS_OVERLAPPED | WS_CAPTION | (style.caption_buttons ? WS_SYSMENU : 0);
        if (style.resizable)
        {
            bits |= WS_THICKFRAME;
        }
    }
    if (style.minimize_button)
    {
        bits |= WS_MINIMIZEBOX;
    }
    if (style.maximize_button)
    {
        bits |= WS_MAXIMIZEBOX;
    }
    return bits;
}

// Topmost is not in here: after creation it can only change through SetWindowPos's insert-after
// (HWND_TOPMOST/HWND_NOTOPMOST), never through GWL_EXSTYLE.
DWORD ex_style_bits(const WindowStyle& style)
{
    return style.tool_window ? WS_EX_TOOLWINDOW : 0;
}

// ---- system menu ----

// Custom ids stay below SC_* (0xF000) and are multiples of 16, because the system keeps the
// low four bits of WM_SYSCOMMAND's wParam.
constexpr UINT SystemMenuFirstId = 0x1000;
constexpr UINT SystemMenuIdStep  = 16;

// Rebuilt from scratch: GetSystemMenu(TRUE) reverts to the default copy, then the items are
// re-added and Close's enabled state re-applied.
void rebuild_system_menu(HWND hwnd, const HostState& s)
{
    GetSystemMenu(hwnd, TRUE);
    const HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (menu == nullptr || !s.window)
    {
        return;
    }
    const std::vector<MenuItem>& items = s.window->system_menu();
    if (!items.empty())
    {
        // Next to Move/Size/Close, as the classic "Always on top" entry sits: between the
        // separator above Close and Close itself, with a separator of their own below.
        int       close_position = -1;
        const int count          = GetMenuItemCount(menu);
        for (int i = 0; i < count; ++i)
        {
            if (GetMenuItemID(menu, i) == SC_CLOSE)
            {
                close_position = i;
                break;
            }
        }
        UINT at = close_position >= 0 ? static_cast<UINT>(close_position) : static_cast<UINT>(count);
        if (close_position < 0)
        {
            InsertMenuW(menu, at++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        }
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            // The OS menu has no icons, shortcut column or submenus here: text, check and state only.
            const MenuItem&   item = items[i];
            const std::string text = item.display_text();
            if (item.kind == MenuItemKind::Separator || text.empty())
            {
                InsertMenuW(menu, at++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
                continue;
            }
            const UINT flags = MF_BYPOSITION | MF_STRING | (item.is_checked() ? MF_CHECKED : MF_UNCHECKED) |
                               (item.is_enabled() ? MF_ENABLED : MF_GRAYED);
            InsertMenuW(menu, at++, flags, SystemMenuFirstId + static_cast<UINT>(i) * SystemMenuIdStep, widen(text).c_str());
        }
        if (close_position >= 0)
        {
            InsertMenuW(menu, at, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        }
    }
    // No style bit exists for the close button; graying the menu's Close is what disables it
    // (CS_NOCLOSE would, but it is class-wide and every libwinui window shares Win32WindowClassName).
    EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | (s.style.closable ? MF_ENABLED : MF_GRAYED));
}

// ---- icons ----

// Re-run on a DPI change, since both icon sizes are per-DPI system metrics.
void apply_icon(HWND hwnd, HostState& s)
{
    const int big_px     = metric(SM_CXICON, s);
    const int small_px   = metric(SM_CXSMICON, s);
    HICON     large_icon = nullptr;
    HICON     small_icon = nullptr;
    if (s.window && s.window->icon() && !s.window->icon()->empty())
    {
        large_icon = create_icon(*s.window->icon(), big_px, s.d2d_factory.Get(), s.dwrite.Get());
        small_icon = create_icon(*s.window->icon(), small_px, s.d2d_factory.Get(), s.dwrite.Get());
    }
    else
    {
        // No icon of its own: the executable's, as Explorer shows it, and else the system's.
        large_icon = load_executable_icon(big_px);
        small_icon = load_executable_icon(small_px);
    }
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
    // Only now, with the window no longer pointing at them, may the previous two go.
    s.big_icon.reset(large_icon);
    s.small_icon.reset(small_icon);
    if (s.window)
    {
        // What a caption the theme draws shows: this icon, or the default application icon the
        // OS shows for a window class without one.
        const HICON shown = small_icon != nullptr
                                ? small_icon
                                : static_cast<HICON>(
                                      LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON, small_px, small_px, LR_SHARED));
        ui::testing::seams(*s.window).report_caption_icon(icon_pixels(shown));
    }
}

void apply_taskbar_overlay(HostState& s)
{
    if (!s.taskbar || !s.window)
    {
        return;
    }
    const std::optional<WindowIcon>& overlay = s.window->taskbar_overlay();
    const HICON icon = overlay ? create_icon(*overlay, metric(SM_CXSMICON, s), s.d2d_factory.Get(), s.dwrite.Get()) : nullptr;
    s.taskbar->set_overlay(icon, widen(s.window->taskbar_overlay_description()));
}

void apply_thumbnail_buttons(HostState& s)
{
    if (!s.taskbar || !s.window || s.window->thumbnail_buttons().empty())
    {
        return;
    }
    const std::vector<ThumbnailButton>& buttons = s.window->thumbnail_buttons();
    std::vector<HICON>                  icons;
    icons.reserve(buttons.size());
    for (const ThumbnailButton& button : buttons)
    {
        icons.push_back(create_icon(button.icon, metric(SM_CXSMICON, s), s.d2d_factory.Get(), s.dwrite.Get()));
    }
    s.taskbar->set_thumbnail_buttons(buttons, std::move(icons));
}

// ---- frame application ----

void apply_frame_attributes(HWND hwnd, HostState& s)
{
    const BOOL no_transitions = s.style.animations ? FALSE : TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &no_transitions, sizeof(no_transitions));

    // The frame keeps drawing the icon under WTNCA_NODRAWICON alone, since it is also the
    // system-menu button, so hiding it needs WTNCA_NOSYSMENU too. Alt+Space still opens the menu.
    WTA_OPTIONS caption{};
    caption.dwMask  = WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU;
    caption.dwFlags = (s.style.caption_title ? 0 : WTNCA_NODRAWCAPTION) |
                      (s.style.caption_icon ? 0 : WTNCA_NODRAWICON | WTNCA_NOSYSMENU);
    SetWindowThemeAttribute(hwnd, WTA_NONCLIENT, &caption, sizeof(caption));

    rebuild_system_menu(hwnd, s);
    apply_frame_margins(hwnd, s);
    update_caption_insets(hwnd, s);
}

// The shell only re-reads WS_EX_TOOLWINDOW as a window is shown, so a visible window is hidden
// around the change.
void apply_ex_style(HWND hwnd, const HostState& s)
{
    const LONG_PTR current = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const LONG_PTR wanted  = (current & ~static_cast<LONG_PTR>(WS_EX_TOOLWINDOW)) |
                             static_cast<LONG_PTR>(ex_style_bits(s.style));
    if (wanted == current)
    {
        return;
    }
    const bool visible = IsWindowVisible(hwnd) != FALSE;
    if (visible)
    {
        ShowWindow(hwnd, SW_HIDE);
    }
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, wanted);
    if (visible)
    {
        ShowWindow(hwnd, SW_SHOWNA);
    }
}

bool theme_draws_frame(const HostState& s);

// While the theme draws the frame, the window is out of uxtheme's hands. DWM composes no frame
// for it then (a region, or glass), and uxtheme answers that by giving the window a rounded
// region of its own, which DWM never recovers from: after switching to a theme with the system
// frame, the window kept the classic caption. Visual styles come back with the system frame.
void sync_frame_theming(HWND hwnd, HostState& s)
{
    const bool unthemed = theme_draws_frame(s);
    if (unthemed == s.frame_unthemed)
    {
        return;
    }
    s.frame_unthemed = unthemed;
    if (unthemed)
    {
        SetWindowTheme(hwnd, L"", L"");
    }
    else
    {
        SetWindowTheme(hwnd, nullptr, nullptr);
    }
}

void apply_style(HWND hwnd, HostState& s)
{
    if (!s.fullscreen)
    {
        const LONG_PTR kept = GetWindowLongPtrW(hwnd, GWL_STYLE) & (WS_VISIBLE | WS_DISABLED | WS_MINIMIZE | WS_MAXIMIZE);
        SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(style_bits(s.style)) | kept);
        apply_ex_style(hwnd, s);
    }
    // While full screen only the remembered windowed style changed (s.style, which the way out
    // re-derives the bits from), plus topmost, which is not part of the frame.
    SetWindowPos(hwnd,
                 s.style.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0,
                 0,
                 0,
                 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | (s.fullscreen ? 0 : SWP_FRAMECHANGED));
    apply_frame_attributes(hwnd, s);
}

// ---- window state ----

WindowState os_state(HWND hwnd, const HostState& s)
{
    if (IsIconic(hwnd))
    {
        return WindowState::Minimized;
    }
    if (s.fullscreen)
    {
        return WindowState::Fullscreen;
    }
    return IsZoomed(hwnd) ? WindowState::Maximized : WindowState::Normal;
}

// Where restore_os_state() would go: a minimized window back to full screen (WS_POPUP stayed) or
// per WPF_RESTORETOMAXIMIZED, full screen back to what it replaced, anything else to Normal.
WindowState restore_target(HWND hwnd, const HostState& s)
{
    if (IsIconic(hwnd))
    {
        if (s.fullscreen)
        {
            return WindowState::Fullscreen;
        }
        WINDOWPLACEMENT placement = sized_win32_struct(&WINDOWPLACEMENT::length);
        return GetWindowPlacement(hwnd, &placement) && (placement.flags & WPF_RESTORETOMAXIMIZED) != 0
                   ? WindowState::Maximized
                   : WindowState::Normal;
    }
    if (s.fullscreen)
    {
        return s.pre_fullscreen_placement.showCmd == SW_SHOWMAXIMIZED ? WindowState::Maximized : WindowState::Normal;
    }
    return WindowState::Normal;
}

void report_state(HWND hwnd, HostState& s)
{
    if (s.window)
    {
        ui::testing::seams(*s.window).report_state(os_state(hwnd, s), restore_target(hwnd, s));
    }
}

// The monitor the window mostly sits on, which is also the one whose DPI it already has, so
// going full screen never triggers WM_DPICHANGED.
void fit_to_monitor(HWND hwnd)
{
    MONITORINFO info = sized_win32_struct(&MONITORINFO::cbSize);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info))
    {
        return;
    }
    const RECT& r = info.rcMonitor;
    SetWindowPos(hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
}

// Raymond Chen's recipe, "How do I switch a window between normal and fullscreen?".
void enter_fullscreen(HWND hwnd, HostState& s)
{
    if (s.fullscreen)
    {
        return;
    }
    if (IsIconic(hwnd))
    {
        ShowWindow(hwnd, SW_RESTORE); // save the placement of the window the user will get back
    }
    s.pre_fullscreen_placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &s.pre_fullscreen_placement);
    if (s.pre_fullscreen_placement.showCmd != SW_SHOWMAXIMIZED)
    {
        s.pre_fullscreen_placement.showCmd = SW_SHOWNORMAL; // e.g. SW_HIDE, going full screen at startup
    }

    s.fullscreen = true;
    // WS_MAXIMIZE goes so the monitor rect becomes the normal position and a minimize restores to
    // full screen. WS_MAXIMIZEBOX goes so Win+Up cannot maximize it.
    const LONG_PTR kept = GetWindowLongPtrW(hwnd, GWL_STYLE) & (WS_VISIBLE | WS_DISABLED | WS_CLIPCHILDREN);
    SetWindowLongPtrW(hwnd, GWL_STYLE, kept | WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX);
    fit_to_monitor(hwnd);

    // Explicit, rather than leaving the shell to its full-screen heuristics.
    if (s.taskbar)
    {
        s.taskbar->mark_fullscreen(true);
    }
    apply_frame_margins(hwnd, s);
    update_caption_insets(hwnd, s);
    report_state(hwnd, s);
}

// The windowed style is re-derived from s.style, so a set_style() made while full screen lands now.
// Without a show command the window goes back to the state full screen replaced. The caller handles
// a minimized window first: restore_os_state() un-minimizes it to full screen instead.
void exit_fullscreen(HWND hwnd, HostState& s, std::optional<UINT> show_command)
{
    if (!s.fullscreen)
    {
        return;
    }
    s.fullscreen        = false;
    const LONG_PTR kept = GetWindowLongPtrW(hwnd, GWL_STYLE) & (WS_VISIBLE | WS_DISABLED | WS_MINIMIZE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(style_bits(s.style)) | kept);
    apply_ex_style(hwnd, s);

    WINDOWPLACEMENT placement = s.pre_fullscreen_placement;
    if (show_command)
    {
        placement.showCmd = *show_command;
    }
    SetWindowPlacement(hwnd, &placement);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

    if (s.taskbar)
    {
        s.taskbar->mark_fullscreen(false);
    }
    apply_frame_margins(hwnd, s);
    update_caption_insets(hwnd, s);
    report_state(hwnd, s);
}

void request_os_state(HWND hwnd, HostState& s, WindowState state)
{
    // Style bits gate the user, not the API: SW_MAXIMIZE maximizes a window with no maximize
    // button just the same, and `resizable = false` doesn't stand in the way of full screen.
    switch (state)
    {
        case WindowState::Minimized:
            ShowWindow(hwnd, SW_MINIMIZE);
            break;
        case WindowState::Maximized:
            if (s.fullscreen)
            {
                exit_fullscreen(hwnd, s, SW_SHOWMAXIMIZED);
            }
            else
            {
                ShowWindow(hwnd, SW_MAXIMIZE);
            }
            break;
        case WindowState::Normal:
            if (s.fullscreen)
            {
                exit_fullscreen(hwnd, s, SW_SHOWNORMAL);
            }
            else
            {
                ShowWindow(hwnd, SW_SHOWNORMAL);
            }
            break;
        case WindowState::Fullscreen:
            enter_fullscreen(hwnd, s);
            break;
    }
    // A request that changed nothing sends no WM_SIZE; the equality gate absorbs the repeat.
    report_state(hwnd, s);
}

// SW_RESTORE's meaning, extended to full screen (WindowHost::restore()).
void restore_os_state(HWND hwnd, HostState& s)
{
    if (IsIconic(hwnd))
    {
        ShowWindow(hwnd, SW_RESTORE); // back to full screen too: WS_POPUP stayed while iconic
    }
    else if (s.fullscreen)
    {
        exit_fullscreen(hwnd, s, std::nullopt);
    }
    else if (IsZoomed(hwnd))
    {
        ShowWindow(hwnd, SW_RESTORE);
    }
    report_state(hwnd, s);
}

// ---- placement ----

UINT dpi_for_rect(const RECT& rect)
{
    UINT x = 96;
    UINT y = 96;
    if (FAILED(GetDpiForMonitor(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &x, &y)))
    {
        return GetDpiForSystem();
    }
    return x;
}

Rect rect_to_dips(const RECT& r, UINT dpi)
{
    const float scale = 96.0f / static_cast<float>(dpi);
    return Rect{static_cast<float>(r.left) * scale,
                static_cast<float>(r.top) * scale,
                static_cast<float>(r.right - r.left) * scale,
                static_cast<float>(r.bottom - r.top) * scale};
}

RECT rect_to_px(const Rect& r, UINT dpi)
{
    const float scale = static_cast<float>(dpi) / 96.0f;
    const auto  px    = [scale](float v) { return static_cast<LONG>(std::lround(v * scale)); };
    return RECT{px(r.left()), px(r.top()), px(r.right()), px(r.bottom())};
}

// The system caret and the input method's windows follow the focused text control's caret, so the
// emoji panel (Win+.) and a candidate list open beside the text instead of in a corner of the
// screen. The caret is never shown: the framework draws its own.
void sync_caret(HWND hwnd, HostState& s)
{
    std::optional<RECT> want;
    if (GetFocus() == hwnd)
    {
        if (const std::optional<Rect> caret = s.window->caret_bounds())
        {
            want = rect_to_px(*caret, s.dpi);
        }
    }
    if (want.has_value() == s.caret.has_value() && (!want || EqualRect(&*want, &*s.caret)))
    {
        return;
    }
    s.caret = want;
    if (!want)
    {
        DestroyCaret();
        return;
    }
    const RECT& r = *want;
    CreateCaret(hwnd, nullptr, std::max<LONG>(1, r.right - r.left), std::max<LONG>(1, r.bottom - r.top));
    SetCaretPos(r.left, r.top);
    if (HIMC context = ImmGetContext(hwnd))
    {
        COMPOSITIONFORM composition{CFS_POINT, POINT{r.left, r.top}, RECT{}};
        ImmSetCompositionWindow(context, &composition);
        // Below the caret, never over the line being typed.
        CANDIDATEFORM candidate{0, CFS_EXCLUDE, POINT{r.left, r.bottom}, r};
        ImmSetCandidateWindow(context, &candidate);
        ImmReleaseContext(hwnd, context);
    }
}

// Normal bounds are converted at the DPI of their own monitor, not the window's current one,
// so apply_placement() can invert it without knowing where the window was.
std::optional<WindowPlacement> read_placement(HWND hwnd, const HostState& s)
{
    WINDOWPLACEMENT native = sized_win32_struct(&WINDOWPLACEMENT::length);
    if (s.fullscreen)
    {
        native = s.pre_fullscreen_placement;
    }
    else if (!GetWindowPlacement(hwnd, &native))
    {
        return std::nullopt;
    }
    WindowPlacement placement{};
    placement.normal_bounds        = rect_to_dips(native.rcNormalPosition, dpi_for_rect(native.rcNormalPosition));
    placement.state                = os_state(hwnd, s);
    placement.restore_to_maximized = s.fullscreen ? native.showCmd == SW_SHOWMAXIMIZED
                                                  : (IsIconic(hwnd) && (native.flags & WPF_RESTORETOMAXIMIZED) != 0);
    return placement;
}

void apply_placement(HWND hwnd, HostState& s, const WindowPlacement& placement)
{
    WINDOWPLACEMENT native = sized_win32_struct(&WINDOWPLACEMENT::length);
    GetWindowPlacement(hwnd, &native);
    // Twice: the first guess (system DPI) finds the monitor, whose own DPI gives the real size.
    RECT px                 = rect_to_px(placement.normal_bounds, GetDpiForSystem());
    px                      = rect_to_px(placement.normal_bounds, dpi_for_rect(px));
    native.rcNormalPosition = px;
    native.flags            = placement.restore_to_maximized ? WPF_RESTORETOMAXIMIZED : 0;
    switch (placement.state)
    {
        case WindowState::Normal:
            native.showCmd = SW_SHOWNORMAL;
            break;
        case WindowState::Minimized:
            native.showCmd = SW_SHOWMINIMIZED;
            break;
        case WindowState::Maximized:
            native.showCmd = SW_SHOWMAXIMIZED;
            break;
        case WindowState::Fullscreen:
            native.showCmd = SW_HIDE; // position it, stay hidden; full screen is applied next
            break;
    }
    SetWindowPlacement(hwnd, &native);
    if (placement.state == WindowState::Fullscreen)
    {
        enter_fullscreen(hwnd, s);
        s.pre_fullscreen_placement.showCmd = placement.restore_to_maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        ShowWindow(hwnd, SW_SHOW);
    }
}

// ---- position and size at runtime (Window::move()/resize()/...) ----

// DIPs at the window's own DPI, both ways: WindowHost's convention for bounds and work areas.
LONG dips_to_px(float dips, const HostState& s)
{
    return static_cast<LONG>(std::lround(dips * static_cast<float>(s.dpi) / 96.0f));
}

// Measured, not AdjustWindowRectEx: that knows nothing of a caption the content took over.
void resize_client_px(HWND hwnd, LONG width, LONG height)
{
    RECT window_rect{};
    RECT client_rect{};
    GetWindowRect(hwnd, &window_rect);
    GetClientRect(hwnd, &client_rect);
    const LONG frame_w = (window_rect.right - window_rect.left) - client_rect.right;
    const LONG frame_h = (window_rect.bottom - window_rect.top) - client_rect.bottom;
    SetWindowPos(hwnd, nullptr, 0, 0, width + frame_w, height + frame_h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Maximized, minimized and full screen have no restored size on screen to change.
bool away_from_restored(HWND hwnd, const HostState& s)
{
    return s.fullscreen || IsIconic(hwnd) || IsZoomed(hwnd);
}

// The restored frame: as last measured, else what the windowed style asks for, less the caption
// when the content takes it over.
SIZE restored_frame(HWND hwnd, const HostState& s)
{
    if (s.restored_frame)
    {
        return *s.restored_frame;
    }
    RECT r{};
    AdjustWindowRectExForDpi(&r, style_bits(s.style), FALSE, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), s.dpi);
    if (s.style.frame == Frame::Standard && s.style.extend_content_into_title_bar)
    {
        r.top = 0;
    }
    return SIZE{r.right - r.left, r.bottom - r.top};
}

// Sets the size the window returns to, outer, in pixels, and leaves it as it is now: maximized and
// minimized stay so, and full screen leaves to the new size.
void resize_restored_px(HWND hwnd, HostState& s, LONG width, LONG height)
{
    WINDOWPLACEMENT  live      = sized_win32_struct(&WINDOWPLACEMENT::length);
    WINDOWPLACEMENT& placement = s.fullscreen ? s.pre_fullscreen_placement : live;
    if (!s.fullscreen)
    {
        GetWindowPlacement(hwnd, &live);
    }
    placement.rcNormalPosition.right  = placement.rcNormalPosition.left + width;
    placement.rcNormalPosition.bottom = placement.rcNormalPosition.top + height;
    if (s.fullscreen)
    {
        return; // exit_fullscreen() applies it
    }
    if (!IsWindowVisible(hwnd))
    {
        live.showCmd = SW_HIDE;
    }
    else if (IsIconic(hwnd))
    {
        live.showCmd = SW_SHOWMINNOACTIVE;
    }
    SetWindowPlacement(hwnd, &live);
}

// New size limits apply to the size the window has now, not only to the next drag.
void clamp_to_size_limits(HWND hwnd, const HostState& s)
{
    if (s.fullscreen || IsIconic(hwnd) || IsZoomed(hwnd))
    {
        return;
    }
    RECT client{};
    GetClientRect(hwnd, &client);
    const auto clamp = [&s](LONG px, float min, float max)
    {
        if (max > 0.0f)
        {
            px = std::min(px, dips_to_px(max, s));
        }
        return min > 0.0f ? std::max(px, dips_to_px(min, s)) : px;
    };
    const LONG width  = clamp(client.right, s.min_size.width, s.max_size.width);
    const LONG height = clamp(client.bottom, s.min_size.height, s.max_size.height);
    if (width != client.right || height != client.bottom)
    {
        resize_client_px(hwnd, width, height);
    }
}

// Re-enabled before DefWindowProc destroys the window: a still-disabled owner cannot take the
// activation, and Windows hands it to another app's window.
void release_owner(HostState& s)
{
    if (s.modal && !s.owner_released && s.owner != nullptr && IsWindow(s.owner))
    {
        EnableWindow(s.owner, TRUE);
    }
    s.owner_released = true;
}

// ---- the WindowHost ----

class Win32WindowHost final : public WindowHost
{
public:
    Win32WindowHost(HWND hwnd, HostState& state) : hwnd_(hwnd), s_(state)
    {
    }

    void request_state(WindowState state) override
    {
        request_os_state(hwnd_, s_, state);
    }
    void restore() override
    {
        restore_os_state(hwnd_, s_);
    }
    void wake() override
    {
        // Any message will do: run()'s loop ticks after every one it dispatches.
        PostMessageW(hwnd_, WM_NULL, 0, 0);
    }
    void close() override
    {
        // Posted: close() is typically called from a click handler several frames deep inside
        // this window's own message dispatch, and WM_CLOSE's default is DestroyWindow().
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
    void set_title(std::string_view title) override
    {
        SetWindowTextW(hwnd_, widen(title).c_str());
    }
    void set_style(const WindowStyle& style) override
    {
        s_.style = style;
        sync_frame_theming(hwnd_, s_);
        apply_style(hwnd_, s_);
        apply_chrome(hwnd_, s_); // the corner preference reads the frame (round_popup_corners)
        layout_window(hwnd_, s_);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void set_chrome(const ChromeOverrides& chrome) override
    {
        s_.chrome_overrides = chrome;
        apply_chrome(hwnd_, s_);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    // Swapping one material for another while one is on is a straight re-set of the attribute;
    // turning it on (or back on) goes through apply_chrome(), which asks the theme first.
    void set_backdrop(Backdrop backdrop) override
    {
        if (backdrop == s_.backdrop_request)
        {
            return;
        }
        s_.backdrop_request     = backdrop;
        s_.backdrop_unsupported = false;
        if (backdrop == Backdrop::None)
        {
            if (s_.backdrop_active)
            {
                disable_backdrop(hwnd_);
                s_.backdrop_active = false;
            }
            apply_frame_margins(hwnd_, s_);
        }
        else if (s_.backdrop_active)
        {
            s_.backdrop_active = try_enable_backdrop(hwnd_, backdrop);
            apply_frame_margins(hwnd_, s_);
        }
        else
        {
            apply_chrome(hwnd_, s_);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    [[nodiscard]] std::optional<WindowPlacement> placement() const override
    {
        return read_placement(hwnd_, s_);
    }
    void set_icon(const WindowIcon*) override
    {
        apply_icon(hwnd_, s_); // reads Window::icon(), which is what was just handed in
    }
    void set_system_menu(const std::vector<MenuItem>&) override
    {
        rebuild_system_menu(hwnd_, s_);
    }
    void set_taskbar_progress(const TaskbarProgress& progress) override
    {
        if (s_.taskbar)
        {
            s_.taskbar->set_progress(progress);
        }
    }
    void set_taskbar_overlay(const WindowIcon*, std::string_view) override
    {
        apply_taskbar_overlay(s_);
    }
    void request_attention(bool flash) override
    {
        FLASHWINFO info{sizeof(FLASHWINFO), hwnd_, flash ? static_cast<DWORD>(FLASHW_ALL | FLASHW_TIMERNOFG) : FLASHW_STOP, 0, 0};
        FlashWindowEx(&info);
    }
    void set_thumbnail_buttons(const std::vector<ThumbnailButton>&) override
    {
        apply_thumbnail_buttons(s_);
    }
    [[nodiscard]] NativeHandle native_handle() const override
    {
        return hwnd_;
    }

    void move(Point position) override
    {
        SetWindowPos(hwnd_,
                     nullptr,
                     dips_to_px(position.x, s_),
                     dips_to_px(position.y, s_),
                     0,
                     0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void resize(Size size) override
    {
        const LONG width  = dips_to_px(size.width, s_);
        const LONG height = dips_to_px(size.height, s_);
        if (away_from_restored(hwnd_, s_))
        {
            const SIZE frame = restored_frame(hwnd_, s_);
            resize_restored_px(hwnd_, s_, width + frame.cx, height + frame.cy);
            return;
        }
        resize_client_px(hwnd_, width, height);
    }
    [[nodiscard]] std::optional<Rect> bounds() const override
    {
        RECT r{};
        if (!GetWindowRect(hwnd_, &r))
        {
            return std::nullopt;
        }
        return rect_to_dips(r, s_.dpi);
    }
    [[nodiscard]] std::optional<Rect> work_area() const override
    {
        MONITORINFO info = sized_win32_struct(&MONITORINFO::cbSize);
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info))
        {
            return std::nullopt;
        }
        return rect_to_dips(info.rcWork, s_.dpi);
    }
    void set_visible(bool visible, bool activate) override
    {
        ShowWindow(hwnd_, !visible ? SW_HIDE : (activate ? SW_SHOW : SW_SHOWNA));
        if (visible && activate)
        {
            SetForegroundWindow(hwnd_);
        }
        ui::testing::seams(*s_.window).report_visible(IsWindowVisible(hwnd_) != FALSE);
    }
    void activate() override
    {
        if (!IsWindowVisible(hwnd_))
        {
            ShowWindow(hwnd_, SW_SHOW);
            ui::testing::seams(*s_.window).report_visible(true);
        }
        else if (IsIconic(hwnd_))
        {
            ShowWindow(hwnd_, SW_RESTORE);
        }
        SetForegroundWindow(hwnd_);
    }
    void set_size_limits(Size min, Size max) override
    {
        s_.min_size = min;
        s_.max_size = max;
        clamp_to_size_limits(hwnd_, s_);
    }

private:
    HWND       hwnd_;
    HostState& s_;
};

// ---- non-client geometry ----

// A maximized window covering the whole work area blocks an auto-hide taskbar's reveal edge, so
// the edge it lives on gives up one pixel.
void leave_autohide_taskbar_gap(HWND hwnd, RECT& client)
{
    APPBARDATA state = sized_win32_struct(&APPBARDATA::cbSize);
    if ((SHAppBarMessage(ABM_GETSTATE, &state) & ABS_AUTOHIDE) == 0)
    {
        return;
    }
    MONITORINFO info = sized_win32_struct(&MONITORINFO::cbSize);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info))
    {
        return;
    }
    for (const UINT edge : {ABE_BOTTOM, ABE_TOP, ABE_LEFT, ABE_RIGHT})
    {
        APPBARDATA bar = sized_win32_struct(&APPBARDATA::cbSize);
        bar.uEdge      = edge;
        bar.rc         = info.rcMonitor;
        if (SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &bar) == 0)
        {
            continue;
        }
        switch (edge)
        {
            case ABE_BOTTOM:
                client.bottom -= 1;
                break;
            case ABE_TOP:
                client.top += 1;
                break;
            case ABE_LEFT:
                client.left += 1;
                break;
            default:
                client.right -= 1;
                break;
        }
        return;
    }
}

// WM_NCCALCSIZE for the two frames that give the client more of the window (collapses_nonclient()).
LRESULT calc_client_rect(HWND hwnd, HostState& s, WPARAM wp, LPARAM lp)
{
    auto*      params      = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
    const RECT window_rect = params->rgrc[0];
    const bool zoomed      = IsZoomed(hwnd) != FALSE;

    if (s.style.frame == Frame::Borderless)
    {
        RECT& client = params->rgrc[0];
        if (zoomed)
        {
            // The maximized overshoot, or the top eight-odd pixels of content sit off-screen.
            const POINT frame = frame_thickness(s);
            InflateRect(&client, -frame.x, -frame.y);
            leave_autohide_taskbar_gap(hwnd, client);
        }
        return 0;
    }

    // Extended title bar: only the caption leaves the non-client area. The side and bottom
    // borders are DefWindowProc's, native and resizable as ever.
    const LRESULT result = DefWindowProcW(hwnd, WM_NCCALCSIZE, wp, lp);
    params->rgrc[0].top  = window_rect.top + (zoomed ? frame_thickness(s).y : 0);
    return result;
}

// Order matters: DWM's caption buttons first (hover, press and Snap Layouts hang off
// HTMAXBUTTON), then the resize band, then the logical window.
LRESULT hit_test(HWND hwnd, HostState& s, WPARAM wp, LPARAM lp)
{
    if (s.extends_into_title_bar())
    {
        LRESULT dwm = 0;
        if (DwmDefWindowProc(hwnd, WM_NCHITTEST, wp, lp, &dwm))
        {
            return dwm;
        }
    }

    const POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    RECT        window_rect{};
    GetWindowRect(hwnd, &window_rect);
    const bool  sizable = s.style.resizable && s.style.frame != Frame::None && !IsZoomed(hwnd);
    const POINT band    = frame_thickness(s);

    if (s.style.frame == Frame::Borderless && sizable)
    {
        const bool left   = screen.x < window_rect.left + band.x;
        const bool right  = screen.x >= window_rect.right - band.x;
        const bool top    = screen.y < window_rect.top + band.y;
        const bool bottom = screen.y >= window_rect.bottom - band.y;
        if (top)
        {
            return left ? HTTOPLEFT : (right ? HTTOPRIGHT : HTTOP);
        }
        if (bottom)
        {
            return left ? HTBOTTOMLEFT : (right ? HTBOTTOMRIGHT : HTBOTTOM);
        }
        if (left)
        {
            return HTLEFT;
        }
        if (right)
        {
            return HTRIGHT;
        }
    }
    else if (s.extends_into_title_bar())
    {
        const LRESULT native = DefWindowProcW(hwnd, WM_NCHITTEST, wp, lp);
        if (native != HTCLIENT)
        {
            return native; // the native side and bottom borders
        }
        if (sizable && screen.y < window_rect.top + band.y)
        {
            // The top sizing band now lies inside the client; corners take the diagonal cursor.
            if (screen.x < window_rect.left + band.x * 2)
            {
                return HTTOPLEFT;
            }
            if (screen.x >= window_rect.right - band.x * 2)
            {
                return HTTOPRIGHT;
            }
            return HTTOP;
        }
    }

    POINT client = screen;
    ScreenToClient(hwnd, &client);
    if (!s.window)
    {
        return HTCLIENT;
    }
    switch (s.window->hit_region_at(s.client_point_dips(client)))
    {
        case HitRegion::Caption:
            return HTCAPTION;
        case HitRegion::SystemMenu:
            return HTSYSMENU;
        case HitRegion::MaximizeButton:
            // What brings up Snap Layouts. The mouse messages over it arrive as WM_NC*, which the
            // window procedure turns back into pointer events for the button under it.
            return HTMAXBUTTON;
        case HitRegion::Client:
        case HitRegion::MinimizeButton:
        case HitRegion::CloseButton:
            break;
    }
    return HTCLIENT;
}

// Whether the theme draws this window's frame (Frame::Themed under a theme with one), so the OS
// must paint none of its own. The rounded corners' region takes the window out of DWM's frame
// composition, and DefWindowProc then paints the classic caption and buttons over the drawn ones.
bool theme_draws_frame(const HostState& s)
{
    return s.window && s.window->style().frame == Frame::Themed && s.style.frame == Frame::Borderless;
}

// DefWindowProc for a message that also repaints the non-client area (a title or icon change),
// with redrawing off for the call so it paints nothing.
LRESULT def_window_proc_unpainted(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!IsWindowVisible(hwnd))
    {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    const LRESULT result = DefWindowProcW(hwnd, msg, wp, lp);
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    return result;
}

// CursorShape -> the system cursor of the same name.
const wchar_t* system_cursor(CursorShape shape)
{
    switch (shape)
    {
        case CursorShape::Arrow:
            return IDC_ARROW;
        case CursorShape::Cross:
            return IDC_CROSS;
        case CursorShape::Hand:
            return IDC_HAND;
        case CursorShape::Help:
            return IDC_HELP;
        case CursorShape::IBeam:
            return IDC_IBEAM;
        case CursorShape::SizeAll:
            return IDC_SIZEALL;
        case CursorShape::SizeNortheastSouthwest:
            return IDC_SIZENESW;
        case CursorShape::SizeNorthSouth:
            return IDC_SIZENS;
        case CursorShape::SizeNorthwestSoutheast:
            return IDC_SIZENWSE;
        case CursorShape::SizeWestEast:
            return IDC_SIZEWE;
        case CursorShape::UniversalNo:
            return IDC_NO;
        case CursorShape::UpArrow:
            return IDC_UPARROW;
        case CursorShape::Wait:
            return IDC_WAIT;
        case CursorShape::Pin:
            return IDC_PIN;
        case CursorShape::Person:
            return IDC_PERSON;
        case CursorShape::AppStarting:
            return IDC_APPSTARTING;
    }
    return IDC_ARROW;
}

// Only over this window's own client area or while captured: a native child sets its own
// cursor, and the frame keeps the system's.
void apply_cursor(HostState& s, HWND hwnd, bool over_client = false)
{
    if (!s.window)
    {
        return;
    }
    POINT screen{};
    if (!GetCursorPos(&screen))
    {
        return;
    }
    if (!over_client && GetCapture() != hwnd &&
        (WindowFromPoint(screen) != hwnd || SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(screen.x, screen.y)) != HTCLIENT))
    {
        return;
    }
    POINT client = screen;
    ScreenToClient(hwnd, &client);
    SetCursor(LoadCursorW(nullptr, system_cursor(s.window->cursor_at(s.client_point_dips(client)))));
}

// ---- files and text dragged in from other apps (OLE) ----

[[nodiscard]] DWORD effect_of(DataPackageOperation operation)
{
    switch (operation)
    {
        case DataPackageOperation::Copy:
            return DROPEFFECT_COPY;
        case DataPackageOperation::Move:
            return DROPEFFECT_MOVE;
        case DataPackageOperation::Link:
            return DROPEFFECT_LINK;
        case DataPackageOperation::None:
            break;
    }
    return DROPEFFECT_NONE;
}

[[nodiscard]] DataPackageOperation operation_of(DWORD effect)
{
    if ((effect & DROPEFFECT_COPY) != 0)
    {
        return DataPackageOperation::Copy;
    }
    if ((effect & DROPEFFECT_MOVE) != 0)
    {
        return DataPackageOperation::Move;
    }
    if ((effect & DROPEFFECT_LINK) != 0)
    {
        return DataPackageOperation::Link;
    }
    return DataPackageOperation::None;
}

// What the user asks for with the keys held, among what the source allows: Ctrl+Shift links,
// Shift moves, Ctrl or no key copies.
[[nodiscard]] DataPackageOperation requested_by(DWORD keys, DWORD allowed)
{
    const bool ctrl  = (keys & MK_CONTROL) != 0;
    const bool shift = (keys & MK_SHIFT) != 0;
    DWORD      wish  = DROPEFFECT_COPY;
    if (ctrl && shift)
    {
        wish = DROPEFFECT_LINK;
    }
    else if (shift)
    {
        wish = DROPEFFECT_MOVE;
    }
    return operation_of((wish & allowed) != 0 ? wish : allowed);
}

// The files (CF_HDROP) and the text (CF_UNICODETEXT) another app's drag carries.
[[nodiscard]] DataPackage package_from(IDataObject* data)
{
    DataPackage package;
    FORMATETC   files{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM   medium{};
    if (SUCCEEDED(data->GetData(&files, &medium)))
    {
        if (const auto drop = static_cast<HDROP>(GlobalLock(medium.hGlobal)))
        {
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i)
            {
                std::wstring path(DragQueryFileW(drop, i, nullptr, 0), L'\0');
                DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size() + 1));
                package.files.emplace_back(std::move(path));
            }
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
    }
    FORMATETC text{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    medium = {};
    if (SUCCEEDED(data->GetData(&text, &medium)))
    {
        if (const auto* chars = static_cast<const wchar_t*>(GlobalLock(medium.hGlobal)))
        {
            const std::size_t limit = GlobalSize(medium.hGlobal) / sizeof(wchar_t);
            package.text            = narrow(std::wstring_view{chars, wcsnlen(chars, limit)});
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
    }
    return package;
}

// The window's OLE drop target. A drag from another app is reported to the Window while it moves,
// the way a drag inside the app is, and the shell's drag image follows the pointer over the
// window (IDropTargetHelper). The caption a target answers with goes under that image as the
// drag's description, where the file manager shows its own "Copy to ...".
class DropTarget final : public IDropTarget
{
public:
    explicit DropTarget(HostState& state) : state_(&state)
    {
        (void)CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper_));
    }
    DropTarget(const DropTarget&)            = delete;
    DropTarget& operator=(const DropTarget&) = delete;

    // The window is going: nothing reaches its state from here on.
    void detach()
    {
        state_ = nullptr;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (out == nullptr)
        {
            return E_POINTER;
        }
        if (iid == IID_IUnknown || iid == IID_IDropTarget)
        {
            *out = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG left = --refs_;
        if (left == 0)
        {
            delete this;
        }
        return left;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL at, DWORD* effect) override
    {
        if (effect == nullptr)
        {
            return E_INVALIDARG;
        }
        data_                = data;
        allowed_             = *effect;
        described_image_     = DROPIMAGE_INVALID;
        described_caption_   = {};
        DataPackage  package = package_from(data);
        DragResponse response;
        if (state_ != nullptr && state_->window != nullptr && (!package.files.empty() || !package.text.empty()))
        {
            package.requested_operation = requested_by(keys, allowed_);
            response = testing::seams(*state_->window).handle_external_drag_enter(std::move(package), client_dips(at));
        }
        *effect = answer(response);
        if (helper_ && state_ != nullptr)
        {
            POINT pt{at.x, at.y};
            helper_->DragEnter(state_->hwnd, data, &pt, *effect);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL at, DWORD* effect) override
    {
        if (effect == nullptr)
        {
            return E_INVALIDARG;
        }
        allowed_ = *effect;
        DragResponse response;
        if (state_ != nullptr && state_->window != nullptr)
        {
            response = testing::seams(*state_->window).handle_external_drag_over(client_dips(at), requested_by(keys, allowed_));
        }
        *effect = answer(response);
        if (helper_)
        {
            POINT pt{at.x, at.y};
            helper_->DragOver(&pt, *effect);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        if (state_ != nullptr && state_->window != nullptr)
        {
            testing::seams(*state_->window).handle_external_drag_leave();
            request_frame(state_->hwnd);
        }
        describe(DROPIMAGE_INVALID, {});
        if (helper_)
        {
            helper_->DragLeave();
        }
        data_.Reset();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL at, DWORD* effect) override
    {
        if (effect == nullptr)
        {
            return E_INVALIDARG;
        }
        allowed_ = *effect;
        // The description goes first: the image the shell shows for the drop is the last one.
        describe(DROPIMAGE_INVALID, {});
        DataPackageOperation result = DataPackageOperation::None;
        if (state_ != nullptr && state_->window != nullptr)
        {
            (void)keys; // the last DragOver had them
            result = testing::seams(*state_->window).handle_external_drag_drop(client_dips(at));
            request_frame(state_->hwnd);
        }
        *effect = effect_of(result) & allowed_;
        if (helper_)
        {
            POINT pt{at.x, at.y};
            helper_->Drop(data, &pt, *effect);
        }
        data_.Reset();
        return S_OK;
    }

private:
    ~DropTarget() = default;

    [[nodiscard]] Point client_dips(POINTL at) const
    {
        POINT pt{at.x, at.y};
        ScreenToClient(state_->hwnd, &pt);
        return state_->client_point_dips(pt);
    }

    // The effect the source hears, and the description shown under the drag image. An operation
    // the source does not allow is refused.
    DWORD answer(const DragResponse& response)
    {
        const DWORD effect = effect_of(response.operation) & allowed_;
        if (state_ != nullptr)
        {
            request_frame(state_->hwnd);
        }
        if (response.caption.empty() && response.glyph_visible)
        {
            describe(DROPIMAGE_INVALID, {}); // the shell's own, for the effect
            return effect;
        }
        DROPIMAGETYPE image = DROPIMAGE_NONE;
        if (!response.glyph_visible)
        {
            image = DROPIMAGE_NOIMAGE;
        }
        else if ((effect & DROPEFFECT_COPY) != 0)
        {
            image = DROPIMAGE_COPY;
        }
        else if ((effect & DROPEFFECT_MOVE) != 0)
        {
            image = DROPIMAGE_MOVE;
        }
        else if ((effect & DROPEFFECT_LINK) != 0)
        {
            image = DROPIMAGE_LINK;
        }
        describe(image, widen(response.caption));
        return effect;
    }

    // Sets the drag's DROPDESCRIPTION, when it changed. The caption goes in as the inserted
    // text, so a '%' in it is shown as it is.
    void describe(DROPIMAGETYPE image, const std::wstring& caption)
    {
        if (!data_ || (image == described_image_ && caption == described_caption_))
        {
            return;
        }
        DROPDESCRIPTION description{};
        description.type = image;
        if (!caption.empty())
        {
            wcsncpy_s(description.szMessage, L"%1", _TRUNCATE);
            wcsncpy_s(description.szInsert, caption.c_str(), _TRUNCATE);
        }
        static const auto format = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_DROPDESCRIPTION));
        FORMATETC         fmt{format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM         medium{};
        medium.tymed   = TYMED_HGLOBAL;
        medium.hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(description));
        if (medium.hGlobal == nullptr)
        {
            return;
        }
        if (void* bytes = GlobalLock(medium.hGlobal))
        {
            std::memcpy(bytes, &description, sizeof(description));
            GlobalUnlock(medium.hGlobal);
        }
        if (FAILED(data_->SetData(&fmt, &medium, TRUE)))
        {
            GlobalFree(medium.hGlobal);
            return;
        }
        described_image_   = image;
        described_caption_ = caption;
    }

    ULONG                     refs_ = 1;
    HostState*                state_;
    ComPtr<IDropTargetHelper> helper_;
    ComPtr<IDataObject>       data_;
    DWORD                     allowed_         = DROPEFFECT_NONE;
    DROPIMAGETYPE             described_image_ = DROPIMAGE_INVALID;
    std::wstring              described_caption_;
};

// An elevated process gets no OLE drag from a file manager that is not elevated, while
// WM_DROPFILES gets through once the message filter lets it in.
[[nodiscard]] bool process_elevated()
{
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE)
    {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD           size = 0;
    const bool elevated  = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != FALSE &&
                           elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        if (auto* created = static_cast<HostState*>(cs->lpCreateParams))
        {
            // Now, not at WM_CREATE: the WM_NCCALCSIZE in between already measures frames.
            created->hwnd = hwnd;
            created->dpi  = GetDpiForWindow(hwnd);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    auto* s = reinterpret_cast<HostState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!s)
    {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // With content in the title bar, DWM's caption buttons live in the client area and DWM needs
    // first refusal of their messages ("Custom Window Frame Using DWM").
    if (s->extends_into_title_bar())
    {
        LRESULT dwm = 0;
        if (DwmDefWindowProc(hwnd, msg, wp, lp, &dwm))
        {
            return dwm;
        }
    }

    // Registered, so not a case label. ITaskbarList3's button calls only work from now on.
    if (msg == s->taskbar_button_created && msg != 0)
    {
        if (s->taskbar)
        {
            s->taskbar->button_created();
        }
        return 0;
    }

    switch (msg)
    {
        case WM_NCCALCSIZE:
            if (wp == TRUE && s->collapses_nonclient())
            {
                return calc_client_rect(hwnd, *s, wp, lp);
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        // A frame the theme draws gets no system caption, buttons or border painted over it.
        case WM_NCPAINT:
        case 0x00AE: // WM_NCUAHDRAWCAPTION: uxtheme's own caption repaint, undocumented
        case 0x00AF: // WM_NCUAHDRAWFRAME
            if (theme_draws_frame(*s))
            {
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        case WM_NCACTIVATE:
            // lParam -1: activate without repainting the non-client area.
            return DefWindowProcW(hwnd, msg, wp, theme_draws_frame(*s) ? -1 : lp);

        case WM_SETTEXT:
        case WM_SETICON:
            return theme_draws_frame(*s) ? def_window_proc_unpainted(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);

        case WM_NCHITTEST:
            if (s->content_is_caption())
            {
                return hit_test(hwnd, *s, wp, lp);
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        // A drawn maximize button answers HTMAXBUTTON for Snap Layouts, so the mouse over it arrives
        // as non-client messages. Hand them to the button as the client ones would be;
        // DefWindowProc would paint a system button over it on a press.
        case WM_NCMOUSEMOVE:
            if (wp == HTMAXBUTTON)
            {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
                TrackMouseEvent(&tme); // so we get WM_NCMOUSELEAVE
                s->over_maximize_button = true;
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd, &pt);
                PointerEvent ev{s->client_point_dips(pt), PointerButton::None, PointerAction::Move};
                apply_pointer_source(ev);
                ui::testing::seams(*s->window).handle_pointer(ev);
                request_frame(hwnd);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        case WM_NCMOUSELEAVE:
            if (s->over_maximize_button)
            {
                s->over_maximize_button = false;
                ui::testing::seams(*s->window).handle_pointer({{}, PointerButton::None, PointerAction::Leave});
                // Moving into the client posts this and a WM_MOUSEMOVE in either order; a Leave
                // after the move would drop the hover the move just set, so set it again.
                POINT pt{};
                RECT  client{};
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                GetClientRect(hwnd, &client);
                if (PtInRect(&client, pt) != FALSE)
                {
                    PointerEvent ev{s->client_point_dips(pt), PointerButton::None, PointerAction::Move};
                    apply_pointer_source(ev);
                    ui::testing::seams(*s->window).handle_pointer(ev);
                }
                request_frame(hwnd);
                if (theme_draws_frame(*s))
                {
                    return 0; // DefWindowProc would repaint system caption buttons to un-hot them
                }
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        case WM_NCLBUTTONDOWN:
        case WM_NCLBUTTONDBLCLK:
            if (wp == HTMAXBUTTON)
            {
                SetCapture(hwnd); // the release comes back as WM_LBUTTONUP
                SetFocus(hwnd);
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd, &pt);
                PointerEvent ev{s->client_point_dips(pt), PointerButton::Left, PointerAction::Down};
                ev.click_count = bump_click_streak(*s, pt);
                apply_pointer_source(ev);
                ui::testing::seams(*s->window).handle_pointer(ev);
                request_frame(hwnd);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        case WM_NCLBUTTONUP:
            if (wp == HTMAXBUTTON)
            {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd, &pt);
                PointerEvent ev{s->client_point_dips(pt), PointerButton::Left, PointerAction::Up};
                apply_pointer_source(ev);
                ui::testing::seams(*s->window).handle_pointer(ev);
                request_frame(hwnd);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        case WM_GETMINMAXINFO:
        {
            // WindowOptions::min_size/max_size are client sizes in DIPs; the OS wants window
            // sizes in pixels, so the frame the window has *right now* is added back on.
            if (s->fullscreen || IsIconic(hwnd) ||
                (s->min_size.width <= 0.0f && s->min_size.height <= 0.0f && s->max_size.width <= 0.0f &&
                 s->max_size.height <= 0.0f))
            {
                return DefWindowProcW(hwnd, msg, wp, lp);
            }
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            RECT  window_rect{};
            RECT  client_rect{};
            GetWindowRect(hwnd, &window_rect);
            GetClientRect(hwnd, &client_rect);
            const int  frame_w = (window_rect.right - window_rect.left) - client_rect.right;
            const int  frame_h = (window_rect.bottom - window_rect.top) - client_rect.bottom;
            const auto px      = [s](float dips)
            { return static_cast<LONG>(std::lround(dips * static_cast<float>(s->dpi) / 96.0f)); };
            if (s->min_size.width > 0.0f)
            {
                info->ptMinTrackSize.x = px(s->min_size.width) + frame_w;
            }
            if (s->min_size.height > 0.0f)
            {
                info->ptMinTrackSize.y = px(s->min_size.height) + frame_h;
            }
            if (s->max_size.width > 0.0f)
            {
                info->ptMaxTrackSize.x = px(s->max_size.width) + frame_w;
                info->ptMaxSize.x      = std::min(info->ptMaxSize.x, info->ptMaxTrackSize.x);
            }
            if (s->max_size.height > 0.0f)
            {
                info->ptMaxTrackSize.y = px(s->max_size.height) + frame_h;
                info->ptMaxSize.y      = std::min(info->ptMaxSize.y, info->ptMaxTrackSize.y);
            }
            return 0;
        }

        case WM_ACTIVATE:
            if (s->window)
            {
                const bool active = LOWORD(wp) != WA_INACTIVE;
                ui::testing::seams(*s->window).report_active(active);
                const ChromeHints chrome = effective_chrome(*s);
                if (chrome.inactive_caption_color.kind() != FrameColorKind::Follow ||
                    chrome.inactive_caption_text_color.kind() != FrameColorKind::Follow)
                {
                    push_caption_colors(hwnd, chrome, active);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return DefWindowProcW(hwnd, msg, wp, lp); // still gives the window focus

        case WM_SHOWWINDOW:
            // lParam 0: an explicit ShowWindow() (the app, or the host showing it at startup), not
            // an owner minimizing or being restored around this window.
            if (s->window && lp == 0)
            {
                ui::testing::seams(*s->window).report_visible(wp != FALSE);
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        case WM_DISPLAYCHANGE:
            // A resolution change or an unplugged display while full screen: cover the monitor
            // the window is on now.
            if (s->fullscreen)
            {
                fit_to_monitor(hwnd);
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        case WM_SYSCOMMAND:
        {
            const UINT command = static_cast<UINT>(wp & 0xFFF0); // low four bits are the system's
            if (command == SC_CLOSE && !s->style.closable)
            {
                return 0; // Alt+F4 and the taskbar's Close, not just the grayed caption button
            }
            // Alt or F10 alone (no character), or an Alt+letter nothing in the tree used: the
            // menu bar's, when there is one. Alt+Space stays the system menu's.
            if (command == SC_KEYMENU && lp != ' ' &&
                ui::testing::seams(*s->window).handle_menu_access(static_cast<char32_t>(lp)))
            {
                request_frame(hwnd);
                return 0;
            }
            if (command >= SystemMenuFirstId && command < 0xF000)
            {
                ui::testing::seams(*s->window).handle_system_menu_item((command - SystemMenuFirstId) / SystemMenuIdStep);
                request_frame(hwnd);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_CREATE:
            s->dpi           = GetDpiForWindow(hwnd);
            s->taskbar_light = taskbar_uses_light_theme();
            // The host goes in first, so everything below that reports back (caption insets,
            // state) already has somewhere to go.
            s->window_host = std::make_unique<Win32WindowHost>(hwnd, *s);
            testing::seams(*s->window).set_window_host(s->window_host.get());
            testing::seams(*s->window).report_scale(static_cast<float>(s->dpi) / 96.0f);
            s->taskbar = std::make_unique<Win32Taskbar>(hwnd);
            register_transparency_notifications(hwnd, *s);
            // An elevated process still hears from the (unelevated) shell.
            ChangeWindowMessageFilterEx(hwnd, s->taskbar_button_created, MSGFLT_ALLOW, nullptr);
            ChangeWindowMessageFilterEx(hwnd, WM_COMMAND, MSGFLT_ALLOW, nullptr);
            ChangeWindowMessageFilterEx(hwnd, WM_SYSCOMMAND, MSGFLT_ALLOW, nullptr);
            // Whatever the app configured before run() is pulled now (Window::set_window_host()).
            s->taskbar->set_progress(s->window->taskbar_progress().get());
            apply_taskbar_overlay(*s);
            apply_thumbnail_buttons(*s);
            apply_icon(hwnd, *s);
            sync_frame_theming(hwnd, *s);
            apply_frame_attributes(hwnd, *s);
            apply_chrome(hwnd, *s); // also settles the backdrop, which the theme has a say in
            // DirectComposition only once something animates (LazyCompositor).
            s->compositor = std::make_unique<LazyCompositor>(hwnd, s->dpi);
            testing::seams(*s->window).set_compositor(s->compositor.get());
            s->native_host = std::make_unique<Win32NativeHost>(hwnd, s->dpi);
            testing::seams(*s->window).set_native_host(s->native_host.get());
            s->context_menu = std::make_unique<Win32ContextMenuHost>(hwnd, s->dpi);
            testing::seams(*s->window).set_context_menu_host(s->context_menu.get());
            s->automation_events = std::make_unique<Win32AutomationEvents>(hwnd, s->window, s->automation_lifetime);
            testing::seams(*s->window).set_automation_events(s->automation_events.get());
            // Only once a text control asks, and only in an app that has one (link_spell_checker()).
            testing::seams(*s->window)
                .set_spell_checker_source(
                    [s]() -> SpellChecker*
                    {
                        const SpellCheckOptions& options = s->window->options().spell_check;
                        if (!options.enabled || spell_checker_link.create == nullptr)
                        {
                            return nullptr;
                        }
                        if (!s->spell_checker)
                        {
                            s->spell_checker = spell_checker_link.create(options.builtin_autocorrections);
                            for (const auto& [from, to] : options.autocorrections)
                            {
                                s->spell_checker->add_autocorrection(from, to);
                            }
                        }
                        return s->spell_checker.get();
                    });
            if (s->collapses_nonclient())
            {
                // CreateWindowExW's WM_NCCALCSIZE had wParam FALSE, which calc_client_rect() ignores; a frame
                // change asks again with TRUE, as "Custom Window Frame Using DWM" does.
                SetWindowPos(hwnd,
                             nullptr,
                             0,
                             0,
                             0,
                             0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            layout_window(hwnd, *s);
            SetTimer(hwnd, TickTimerId, TickIntervalMs, nullptr); // drives Window::tick()
            s->timer_ms      = TickIntervalMs;
            s->timer_next_ms = now_ms() + TickIntervalMs;
            return 0;

        case WM_SIZE:
            // Where a state change the user made is noticed: caption buttons, Win+arrows, the taskbar.
            if (wp == SIZE_MINIMIZED)
            {
                // Minimized means no layout and no paint: the 0x0 size is not one to arrange at.
                ui::testing::seams(*s->window).report_state(WindowState::Minimized, restore_target(hwnd, *s));
                return 0;
            }
            if (wp == SIZE_RESTORED || wp == SIZE_MAXIMIZED)
            {
                report_state(hwnd, *s);
            }
            if (wp == SIZE_RESTORED && !s->fullscreen)
            {
                RECT window_rect{};
                GetWindowRect(hwnd, &window_rect);
                s->restored_frame = SIZE{(window_rect.right - window_rect.left) - LOWORD(lp),
                                         (window_rect.bottom - window_rect.top) - HIWORD(lp)};
            }
            if (s->target)
            {
                s->target->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            }
            if (s->extends_into_title_bar())
            {
                // Maximizing drops the sizing band from the caption row, restoring brings it back.
                apply_frame_margins(hwnd, *s);
                update_caption_insets(hwnd, *s);
            }
            layout_window(hwnd, *s);
            sync_corner_region(hwnd, *s); // after report_state(): maximized, the frame has no border
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_DPICHANGED:
        {
            s->dpi = HIWORD(wp);
            testing::seams(*s->window).report_scale(static_cast<float>(s->dpi) / 96.0f);
            if (s->target)
            {
                s->target->SetDpi(static_cast<float>(s->dpi), static_cast<float>(s->dpi));
            }
            if (s->compositor)
            {
                s->compositor->set_dpi(s->dpi);
            }
            if (s->native_host)
            {
                s->native_host->set_dpi(s->dpi);
            }
            if (s->context_menu)
            {
                s->context_menu->set_dpi(s->dpi);
            }
            auto* prc = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (s->fullscreen)
            {
                fit_to_monitor(hwnd); // moved to a monitor of another DPI: cover that one exactly
            }
            apply_icon(hwnd, *s); // both icon sizes are per-DPI metrics
            apply_frame_margins(hwnd, *s);
            update_caption_insets(hwnd, *s);
            layout_window(hwnd, *s);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_PAINT:
        {
            // Setters only mark the tree dirty; layout runs here, before every paint. layout() skips the
            // work when nothing is dirty and the size did not change.
            if (IsIconic(hwnd))
            {
                ValidateRect(hwnd, nullptr); // nothing to show; don't let the request come back
                return 0;
            }
            if (s->extends_into_title_bar())
            {
                update_caption_insets(hwnd, *s); // DWM may only know its button bounds by now
            }
            layout_window(hwnd, *s);
            // Anything but the one-pixel frame request is the OS asking (a resize, an uncovered
            // or restored window): that repaints the whole client area.
            RECT update{};
            GetUpdateRect(hwnd, &update, FALSE);
            const RECT  request    = frame_request(hwnd);
            const bool  everything = !EqualRect(&update, &request);
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            paint(hwnd, *s, everything);
            EndPaint(hwnd, &ps);
            sync_caret(hwnd, *s); // a caret that moved marked the tree dirty, which is what got us here
            // Let every child HWND that owes a repaint do it now: this window keeps an update region while
            // an animation runs, so a child's synthesized WM_PAINT would starve.
            RedrawWindow(hwnd, nullptr, nullptr, RDW_ALLCHILDREN | RDW_UPDATENOW);
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme); // so we get WM_MOUSELEAVE
            PointerEvent ev{s->pointer_dips(lp), PointerButton::None, PointerAction::Move};
            ev.modifiers = mouse_modifiers(wp);
            apply_pointer_source(ev);
            ui::testing::seams(*s->window).handle_pointer(ev);
            apply_cursor(*s, hwnd); // WM_SETCURSOR came before the move: a link's hover is known now
            request_frame(hwnd);
            return 0;
        }

        case WM_SETCURSOR:
            // wParam is the window under the pointer; a child control's WM_SETCURSOR reaches this
            // parent through DefWindowProc first, and must be left to set its own.
            if (reinterpret_cast<HWND>(wp) == hwnd && LOWORD(lp) == HTCLIENT && s->window)
            {
                apply_cursor(*s, hwnd, true);
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        // A pen or a finger: note its pressure, tilt and buttons for the mouse message Windows makes
        // of this one, and leave the rest to DefWindowProc, which makes it. Handling these instead
        // would take pen and touch off the mouse path every control, popup and drag runs on.
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP:
            last_pointer_sample() = query_pointer_sample(GET_POINTERID_WPARAM(wp));
            return DefWindowProcW(hwnd, msg, wp, lp);

        case WM_MOUSELEAVE:
            ui::testing::seams(*s->window).handle_pointer({{}, PointerButton::None, PointerAction::Leave});
            request_frame(hwnd);
            return 0;

        case WM_LBUTTONDOWN:
        {
            SetCapture(hwnd);
            SetFocus(hwnd);
            const POINT  pt_px{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            PointerEvent ev{s->pointer_dips(lp), PointerButton::Left, PointerAction::Down};
            ev.modifiers   = mouse_modifiers(wp);
            ev.click_count = bump_click_streak(*s, pt_px);
            apply_pointer_source(ev);
            ui::testing::seams(*s->window).handle_pointer(ev);
            apply_cursor(*s, hwnd);
            request_frame(hwnd);
            return 0;
        }

        case WM_LBUTTONUP:
        {
            ReleaseCapture();
            PointerEvent ev{s->pointer_dips(lp), PointerButton::Left, PointerAction::Up};
            ev.modifiers = mouse_modifiers(wp);
            apply_pointer_source(ev);
            ui::testing::seams(*s->window).handle_pointer(ev);
            apply_cursor(*s, hwnd); // the press's cursor is released
            request_frame(hwnd);
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            // Unlike the other mouse messages, WM_MOUSEWHEEL's position is in screen
            // coordinates.
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            // Keep the fraction: high-resolution wheels and touchpads send deltas below WHEEL_DELTA, which
            // integer division floors to zero.
            const float notches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / static_cast<float>(WHEEL_DELTA);
            // Positive WHEEL_DELTA (away from the user) scrolls toward the start of the
            // content, i.e. decreases the scroll offset, hence the negation.
            const float delta = -notches * WheelLineHeight;
            // WM_MOUSEWHEEL has the MK_* bits in wParam's low word; without them Ctrl+wheel zoom is impossible.
            PointerEvent ev{s->client_point_dips(pt), PointerButton::None, PointerAction::Wheel, delta};
            ev.modifiers = mouse_modifiers(wp);
            ui::testing::seams(*s->window).handle_pointer(ev);
            request_frame(hwnd);
            return 0;
        }

        case WM_MOUSEHWHEEL:
        {
            // A tilt wheel or sideways swipe: like WM_MOUSEWHEEL, but positive already means "to the
            // right", the direction the content offset grows, so no negation.
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            const float notches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / static_cast<float>(WHEEL_DELTA);
            PointerEvent ev{s->client_point_dips(pt), PointerButton::None, PointerAction::Wheel, notches * WheelLineHeight};
            ev.modifiers        = mouse_modifiers(wp);
            ev.horizontal_wheel = true;
            ui::testing::seams(*s->window).handle_pointer(ev);
            request_frame(hwnd);
            return 0;
        }

        // The Menu key's WM_CONTEXTMENU is synthesized by DefWindowProc, so
        // VK_APPS alone still reaches it; Shift+F10 arrives as WM_SYSKEYDOWN, never handled here.
        case WM_KEYDOWN:
            ui::testing::seams(*s->window).handle_key({translate_key(wp), KeyAction::Down, key_modifiers()});
            request_frame(hwnd);
            return wp == VK_APPS ? DefWindowProcW(hwnd, msg, wp, lp) : 0;

        case WM_KEYUP:
            ui::testing::seams(*s->window).handle_key({translate_key(wp), KeyAction::Up, key_modifiers()});
            request_frame(hwnd);
            return wp == VK_APPS ? DefWindowProcW(hwnd, msg, wp, lp) : 0;

        // An Alt chord reaches the tree first: a shortcut (Alt+Enter, Ctrl+Alt+X) or a control
        // (Alt+Down opens a SplitButton's flyout). One nothing used goes on to the system, and so
        // do the chords it keeps (Alt+F4, Alt+Space, Alt+Tab, Alt+Esc) and F10 and Shift+F10.
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            if ((HIWORD(lp) & KF_ALTDOWN) != 0 && !system_alt_chord(wp) && translate_key(wp) != Key::Unknown)
            {
                const KeyAction action = msg == WM_SYSKEYDOWN ? KeyAction::Down : KeyAction::Up;
                const bool used = ui::testing::seams(*s->window).handle_key({translate_key(wp), action, key_modifiers()});
                request_frame(hwnd);
                if (used || wp == VK_UP || wp == VK_DOWN)
                {
                    // The WM_SYSCHAR TranslateMessage makes of a used chord would beep.
                    s->swallow_syschar = action == KeyAction::Down;
                    return 0;
                }
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        case WM_SYSCHAR:
            if (std::exchange(s->swallow_syschar, false))
            {
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);

        case WM_CHAR:
            // Control characters fire both WM_CHAR and WM_KEYDOWN, so handle_text() ignores them.
            // A character past the BMP arrives as two WM_CHARs, which the decoder pairs up.
            if (const std::string text = s->char_input.feed(static_cast<char16_t>(wp)); !text.empty())
            {
                ui::testing::seams(*s->window).handle_text(text);
            }
            request_frame(hwnd);
            return 0;

        case WM_SETFOCUS:
            // The keyboard layout may have changed while another app had focus, and the Typing
            // switches while the user was in Settings.
            if (s->spell_checker)
            {
                spell_checker_link.refresh(*s->spell_checker, true, true);
            }
            s->window->restore_focus();
            request_frame(hwnd);
            return 0;

        case WM_KILLFOCUS:
            s->window->suspend_focus();
            if (s->caret)
            {
                s->caret.reset();
                DestroyCaret();
            }
            request_frame(hwnd);
            return 0;

        case WM_SETTINGCHANGE:
        case WM_DWMCOLORIZATIONCOLORCHANGED:
            // Fires for many settings, not only the theme; apply_theme() passes on only a change.
            apply_theme(*s);
            apply_chrome(hwnd, *s);
            // A glyph with no color of its own follows the taskbar's theme.
            if (const bool light = taskbar_uses_light_theme(); light != s->taskbar_light)
            {
                s->taskbar_light = light;
                apply_icon(hwnd, *s);
                apply_taskbar_overlay(*s);
                apply_thumbnail_buttons(*s);
            }
            // The Region settings ("intl"): a changed decimal symbol reformats every NumberBox on
            // the default formatter (set_regional_number_format() does nothing when it is the same).
            set_regional_number_format(user_number_format());
            set_regional_date_format(user_date_format());
            // Also how a flipped "Highlight/Autocorrect misspelled words" switch arrives.
            if (s->spell_checker)
            {
                spell_checker_link.refresh(*s->spell_checker, false, true);
            }
            request_frame(hwnd);
            return 0;

        case WM_POWERBROADCAST:
            // Energy Saver switching on or off (the registered power settings).
            if (wp == PBT_POWERSETTINGCHANGE && apply_theme(*s))
            {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return TRUE;

        case WM_WTSSESSION_CHANGE:
            if ((wp == WTS_REMOTE_CONNECT || wp == WTS_REMOTE_DISCONNECT || wp == WTS_CONSOLE_CONNECT) && apply_theme(*s))
            {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_INPUTLANGCHANGE:
            // The user switched keyboard layout: spell checking follows it.
            if (s->spell_checker)
            {
                spell_checker_link.refresh(*s->spell_checker, true, false);
            }
            request_frame(hwnd);
            return DefWindowProcW(hwnd, msg, wp, lp); // also passes it on to native child controls

        case WM_COMMAND:
        {
            // A hosted native control notifying its parent, resolved to its element by handle. An
            // EN_CHANGE the element leaves goes to on_native_content_changed().
            if (HIWORD(wp) == THBN_CLICKED)
            {
                // A taskbar thumbnail button (Win32Taskbar.hpp): its command id is its index.
                const UINT id = LOWORD(wp);
                if (id >= Win32Taskbar::FirstButtonId && id < Win32Taskbar::FirstButtonId + 7)
                {
                    ui::testing::seams(*s->window).handle_thumbnail_button(id - Win32Taskbar::FirstButtonId);
                    request_frame(hwnd);
                    return 0;
                }
            }
            NativeControl* owner = s->native_host && lp != 0 ? s->native_host->owner_for(reinterpret_cast<HWND>(lp)) : nullptr;
            if (owner)
            {
                // The element sees its window's notifications first; an EN_CHANGE it declines still
                // reaches on_native_content_changed() below.
                NativeNotification notification{msg, static_cast<std::uintptr_t>(wp), static_cast<std::intptr_t>(lp)};
                if (ui::testing::seams(*s->window).handle_native_notification(*owner, notification))
                {
                    request_frame(hwnd);
                    return notification.result;
                }
            }
            if (HIWORD(wp) == EN_CHANGE && owner)
            {
                ui::testing::seams(*s->window).handle_native_content_changed(*owner);
                request_frame(hwnd);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_NOTIFY:
        case WM_HSCROLL:
        case WM_VSCROLL:
        {
            // WM_NOTIFY names the control in its NMHDR, WM_HSCROLL/WM_VSCROLL in lParam (null for a
            // window's own scroll bars, which are nobody's here).
            HWND source = nullptr;
            if (msg == WM_NOTIFY)
            {
                source = lp != 0 ? reinterpret_cast<const NMHDR*>(lp)->hwndFrom : nullptr;
            }
            else
            {
                source = reinterpret_cast<HWND>(lp);
            }
            NativeControl* owner = s->native_host && source ? s->native_host->owner_for(source) : nullptr;
            if (owner)
            {
                NativeNotification notification{msg, static_cast<std::uintptr_t>(wp), static_cast<std::intptr_t>(lp)};
                if (ui::testing::seams(*s->window).handle_native_notification(*owner, notification))
                {
                    request_frame(hwnd);
                    return notification.result;
                }
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSCROLLBAR:
        {
            // Sent while the child paints, so the element is asked directly: no dispatch guard and
            // no sync, nothing that could move a window mid-paint.
            NativeControl* owner = s->native_host ? s->native_host->owner_for(reinterpret_cast<HWND>(lp)) : nullptr;
            if (owner)
            {
                NativeNotification notification{msg, static_cast<std::uintptr_t>(wp), static_cast<std::intptr_t>(lp)};
                if (owner->handle_native_notification(notification))
                {
                    return notification.result;
                }
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_CONTEXTMENU:
        {
            // DefWindowProc turns a right-button release, Shift+F10 or the Menu key into this. wParam is
            // the window it happened in: this one or a NativeControl's child.
            const HWND           source = reinterpret_cast<HWND>(wp);
            std::optional<Point> position;
            const int            x = GET_X_LPARAM(lp);
            const int            y = GET_Y_LPARAM(lp);

            if (source != hwnd)
            {
                // A native child has no framework hit testing, so its owning element is asked directly;
                // anything else falls back to default handling.
                NativeControl* owner = s->native_host ? s->native_host->owner_for(source) : nullptr;
                if (!owner)
                {
                    return DefWindowProcW(hwnd, msg, wp, lp);
                }
                if (x != -1 || y != -1) // (-1, -1): keyboard-invoked, the element picks its anchor
                {
                    POINT pt{x, y};
                    ScreenToClient(hwnd, &pt); // this window's client space, like every other Point
                    position = s->client_point_dips(pt);
                }
                if (s->context_menu)
                {
                    s->context_menu->set_dark(effective_chrome(*s).dark_titlebar);
                }
                const bool shown = ui::testing::seams(*s->window).handle_native_context_menu(*owner, position);
                request_frame(hwnd);
                return shown ? 0 : DefWindowProcW(hwnd, msg, wp, lp);
            }

            if (x != -1 || y != -1) // (-1, -1): keyboard-invoked
            {
                POINT pt{x, y};
                ScreenToClient(hwnd, &pt);
                RECT rc{};
                GetClientRect(hwnd, &rc);
                if (!PtInRect(&rc, pt) || (s->content_is_caption() && SendMessageW(hwnd, WM_NCHITTEST, 0, lp) != HTCLIENT))
                {
                    // Caption and frame: the system menu, including client areas a borderless or extended frame
                    // hands to the caption.
                    return DefWindowProcW(hwnd, msg, wp, lp);
                }
                position = s->client_point_dips(pt);
            }
            // Per menu: the menu palette is process-wide, while the theme is per window and can change
            // without the host hearing about it.
            if (s->context_menu)
            {
                s->context_menu->set_dark(effective_chrome(*s).dark_titlebar);
            }
            const bool shown = ui::testing::seams(*s->window).handle_context_menu(position);
            request_frame(hwnd);
            return shown ? 0 : DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_TIMER:
            if (wp == TickTimerId)
            {
                detail::count(detail::PerfCounter::TimerWakeups);
                s->timer_fired = true;
                tick_from_timer(hwnd);
            }
            return 0;

        case WM_GETOBJECT:
        {
            // UiaRootObjectId, not OBJID_CLIENT, for a UIA-only provider.
            if (static_cast<long>(lp) == static_cast<long>(UiaRootObjectId))
            {
                ui::Element* root = s->window->has_modal() ? s->window->modal() : s->window->content();
                if (root)
                {
                    // UiaReturnRawElementProvider takes the references it needs; ours (the one the
                    // object starts with, adopted by Attach) is released on return.
                    Microsoft::WRL::ComPtr<Win32AutomationProvider> provider;
                    provider.Attach(
                        new Win32AutomationProvider(hwnd, s->window, root, s->automation_lifetime, /*is_root=*/true));
                    return UiaReturnRawElementProvider(hwnd, wp, lp, provider.Get());
                }
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_DROPFILES:
        {
            // Where OLE's drag and drop is not available (see create_host()): the drop arrives
            // whole, where it was released.
            const auto  drop = reinterpret_cast<HDROP>(wp);
            DataPackage package;
            package.requested_operation = DataPackageOperation::Copy;
            const UINT count            = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i)
            {
                std::wstring path(DragQueryFileW(drop, i, nullptr, 0), L'\0');
                DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size() + 1));
                package.files.emplace_back(std::move(path));
            }
            POINT at{};
            DragQueryPoint(drop, &at);
            DragFinish(drop);
            if (s->window != nullptr && !package.files.empty())
            {
                (void)ui::testing::seams(*s->window).handle_external_drop(std::move(package), s->client_point_dips(at));
            }
            return 0;
        }

        case WM_CLOSE:
            // Every way of closing arrives here (the close button, Alt+F4, the taskbar,
            // Window::close()), so this is where the app gets to say no.
            if (!ui::testing::seams(*s->window).handle_close_request())
            {
                return 0;
            }
            if (s->close_hides)
            {
                ShowWindow(hwnd, SW_HIDE);
                ui::testing::seams(*s->window).report_visible(false);
                return 0;
            }
            release_owner(*s);
            return DefWindowProcW(hwnd, msg, wp, lp); // DestroyWindow()

        case WM_DESTROY:
            // First, while the host is still attached: the app stops its workers here. The
            // Window itself stays, as the app owns it; run() only frees this state once the loop
            // sees the WM_QUIT posted below.
            ui::testing::seams(*s->window).handle_closed();
            if (s->drop_target)
            {
                RevokeDragDrop(hwnd);
                s->drop_target->detach();
                s->drop_target.Reset();
            }
            // Before the window is detached: a provider a client still holds now fails with
            // UIA_E_ELEMENTNOTAVAILABLE.
            s->automation_lifetime->alive = false;
            UiaReturnRawElementProvider(hwnd, 0, 0, nullptr); // tell UIA this window is going away
            {
                // A handler running after this has no HWND to ask, and the Window, which the app
                // owns and may keep using, must not point into a host that is about to be freed.
                auto seams = testing::seams(*s->window);
                seams.set_window_host(nullptr);
                seams.set_compositor(nullptr);
                seams.set_native_host(nullptr);
                seams.set_spell_checker(nullptr);
                seams.set_spell_checker_source({});
                seams.set_context_menu_host(nullptr);
                seams.set_automation_events(nullptr);
            }
            KillTimer(hwnd, TickTimerId);
            unregister_transparency_notifications(hwnd, *s);
            release_owner(*s); // DestroyWindow() called directly, never through WM_CLOSE
            s->destroyed = true;
            if (s == main_host())
            {
                PostQuitMessage(0); // the main window ends run()'s loop
            }
            return 0;

        case WM_NCDESTROY:
            // The last message: nothing may reach this state through the handle from here on, as
            // the run() loop is about to destroy it.
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd, msg, wp, lp);

        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// ---- every open window (ui::run(), Window::show()) ----

// Each HostState lives until the loop's next safe point after its WM_DESTROY, never freed
// inside a message dispatched to it, where a handler may still use it.
struct Registry
{
    std::vector<std::unique_ptr<HostState>> hosts;
    HostState*                              main        = nullptr;
    bool                                    class_ready = false;
    bool                                    shown_any   = false; // the first show honors STARTUPINFO
    bool                                    running     = false; // either run()'s loop

    double last_tick_ms = 0.0;
    // Set while tick_all() runs: a handler inside a tick (post() work, an animation finishing) that
    // opens a picker runs a modal loop inside it, and its timer must not tick a second time.
    bool ticking = false;
    // Set while message_loop() itself dispatches a WM_TIMER, which it ticks around anyway.
    bool dispatching_own_timer = false;
    // The window whose timer runs at ModalTickIntervalMs while a modal loop lasts.
    HWND modal_timer = nullptr;
    // When a window last wanted ticks (Window::wants_ticks()); see IdleGraceMs.
    double last_busy_ms = 0.0;
};

Registry& registry()
{
    static Registry instance;
    return instance;
}

HostState* main_host()
{
    return registry().main;
}

void ensure_process_setup()
{
    Registry& r = registry();
    if (r.class_ready)
    {
        return;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = Win32WindowClassName;
    RegisterClassExW(&wc);
    r.class_ready = true;
    // Before the first frame: a NumberBox built with the tree shows its value in these.
    set_regional_number_format(user_number_format());
    set_regional_date_format(user_date_format());
}

// One single-threaded Direct2D factory per UI thread, shared by its windows: a factory is not
// cheap, and single-threaded is right since a window's resources stay on its thread.
ComPtr<ID2D1Factory> thread_d2d_factory()
{
    thread_local ComPtr<ID2D1Factory> factory;
    if (!factory)
    {
        (void)D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
    }
    return factory;
}

// The shared DirectWrite factory, the same object for every caller in the process.
ComPtr<IDWriteFactory> process_dwrite_factory()
{
    static const ComPtr<IDWriteFactory> factory = []
    {
        ComPtr<IDWriteFactory> f;
        (void)DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                  __uuidof(IDWriteFactory),
                                  reinterpret_cast<IUnknown**>(f.GetAddressOf()));
        return f;
    }();
    return factory;
}

// The state joins the registry only once the window exists.
HostState* create_host(Window& window, bool visible, bool activate = true)
{
    // First, before anything else on this thread picks its COM apartment.
    const bool ole = enter_ole_apartment() && !process_elevated();
    ensure_process_setup();
    const WindowOptions& options = window.options();

    auto       owned = std::make_unique<HostState>();
    HostState& state = *owned;
    state.window     = &window;

    state.d2d_factory = thread_d2d_factory();
    state.dwrite      = process_dwrite_factory();
    if (!state.d2d_factory || !state.dwrite)
    {
        return nullptr;
    }
    state.measurer = std::make_unique<DWriteMeasurer>(state.dwrite.Get());

    state.backdrop_request = state.window->backdrop();
    apply_theme(state);

    // The Window's own live properties (seeded from its options, maybe changed since) are what
    // the OS window is created with; the host mirrors them from here on.
    state.style                  = ui::testing::seams(*state.window).effective_style();
    state.chrome_overrides       = state.window->chrome();
    state.min_size               = state.window->min_size();
    state.max_size               = state.window->max_size();
    state.taskbar_button_created = RegisterWindowMessageW(L"TaskbarButtonCreated");

    // An owner that is not (or no longer) a real window owns nothing.
    state.owner       = options.owner != nullptr ? static_cast<HWND>(options.owner->native_handle()) : nullptr;
    state.modal       = options.modal && state.owner != nullptr;
    state.close_hides = options.close_hides;

    // Clusters, word characters and casing from the system ICU from here on (render/TextAnalyzer.hpp).
    install_text_analyzer();


    const HINSTANCE inst = GetModuleHandleW(nullptr);
    const UINT      dpi  = GetDpiForSystem();
    // A first guess at the outer size; resized to the exact client size below, once the window
    // has the DPI and the frame it actually got.
    const int w = static_cast<int>(std::lround(options.size.width * static_cast<float>(dpi) / 96.0f));
    const int h = static_cast<int>(std::lround(options.size.height * static_cast<float>(dpi) / 96.0f));

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (options.position)
    {
        x = static_cast<int>(std::lround(options.position->x * static_cast<float>(dpi) / 96.0f));
        y = static_cast<int>(std::lround(options.position->y * static_cast<float>(dpi) / 96.0f));
    }

    // Topmost is only set through the ex style here, at creation; afterwards it takes
    // SetWindowPos's insert-after. For an overlapped window hWndParent is the owner.
    const DWORD ex_style = ex_style_bits(state.style) | (state.style.always_on_top ? WS_EX_TOPMOST : 0);
    HWND        hwnd     = CreateWindowExW(ex_style,
                                           Win32WindowClassName,
                                           widen(state.window->title()).c_str(),
                                           style_bits(state.style),
                                           x,
                                           y,
                                           w,
                                           h,
                                           state.owner,
                                           nullptr,
                                           inst,
                                           &state);
    if (!hwnd)
    {
        return nullptr;
    }
    detail::note_milestone(detail::Milestone::FirstWindow);
    Registry& r = registry();
    r.hosts.push_back(std::move(owned));

    // Files and text dragged in from other apps come through OLE, which reports them while they
    // move. Without it (the app put the thread in the MTA, or the process is elevated, which a
    // file manager's OLE drag cannot reach) WM_DROPFILES reports a drop of files where it lands.
    if (ole)
    {
        ComPtr<DropTarget> target;
        target.Attach(new DropTarget(state));
        if (SUCCEEDED(RegisterDragDrop(hwnd, target.Get())))
        {
            state.drop_target = std::move(target);
        }
    }
    if (!state.drop_target)
    {
        DragAcceptFiles(hwnd, TRUE);
        ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    }
    ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, nullptr);

    // Created cloaked, so DWM composes nothing before the first frame: no white flash.
    BOOL cloak = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));

    // At the DPI the window actually got, with the frame it actually has.
    resize_client_px(hwnd, dips_to_px(options.size.width, state), dips_to_px(options.size.height, state));
    if (state.modal)
    {
        EnableWindow(state.owner, FALSE);
    }

    if (!visible)
    {
        // Laid out now, so the first show() has a tree to paint. No placement: applying one shows.
        layout_window(hwnd, state);
    }
    else if (options.placement)
    {
        apply_placement(hwnd, state, *options.placement);
    }
    else
    {
        switch (options.initial_state)
        {
            case WindowState::Normal:
                // SW_SHOWDEFAULT honors a shortcut's "Run: Maximized/Minimized", but only for the process's
                // first window.
                ShowWindow(hwnd, !activate ? SW_SHOWNOACTIVATE : (r.shown_any ? SW_SHOWNORMAL : SW_SHOWDEFAULT));
                break;
            case WindowState::Minimized:
                ShowWindow(hwnd, activate ? SW_SHOWMINIMIZED : SW_SHOWMINNOACTIVE);
                break;
            case WindowState::Maximized:
                // There is no SW_ value that maximizes without activating.
                ShowWindow(hwnd, SW_SHOWMAXIMIZED);
                break;
            case WindowState::Fullscreen:
                enter_fullscreen(hwnd, state);
                ShowWindow(hwnd, activate ? SW_SHOW : SW_SHOWNA);
                break;
        }
    }
    r.shown_any = r.shown_any || visible;
    UpdateWindow(hwnd);
    report_state(hwnd, state);
    ui::testing::seams(*state.window).report_visible(IsWindowVisible(hwnd) != FALSE);
    ui::testing::seams(*state.window).handle_opened();

    cloak = FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
    return &state;
}

// The states of windows destroyed since the last pass. Only ever called between messages, so no
// handler of theirs is on the stack. Only the host state is freed: each Window (and its tree)
// belongs to the app and stays valid.
void sweep_destroyed()
{
    std::vector<std::unique_ptr<HostState>>& hosts = registry().hosts;
    std::erase_if(hosts, [](const std::unique_ptr<HostState>& s) { return s->destroyed && s.get() != main_host(); });
}

void tick_all()
{
    Registry& r = registry();
    if (r.ticking)
    {
        return;
    }
    const double now      = now_ms();
    const double delta_ms = now - r.last_tick_ms;
    r.last_tick_ms        = now;
    if (delta_ms <= 0.0)
    {
        return;
    }
    r.ticking = true;
    const struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{r.ticking};
    // Every open window's clock, by index: a tick handler may open another window, which
    // grows the vector (the states themselves stay put, as they are owned by pointer).
    std::vector<std::unique_ptr<HostState>>& hosts = r.hosts;
    for (std::size_t i = 0; i < hosts.size(); ++i)
    {
        HostState& s = *hosts[i];
        if (s.destroyed || !s.window || s.hwnd == nullptr)
        {
            continue;
        }
        // Time keeps moving while minimized, so a tooltip delay or an animation is in the right
        // place on restore, but nobody can see a repaint, so none is asked for.
        if (s.window->tick(Duration{delta_ms}) && !IsIconic(s.hwnd))
        {
            request_frame(s.hwnd);
        }
        if (s.window->wants_ticks())
        {
            r.last_busy_ms = now;
        }
    }
    // Idle means idle: no window has wanted a tick for a while, so the timers stop until a
    // message (input, a post(), the OS) brings something to do, or until an after()/every()
    // timer or an element's request_tick_in() is due (Window::next_wake()). A busy window
    // ticks every TickIntervalMs, or sooner for one of those.
    const bool busy = now - r.last_busy_ms < IdleGraceMs;
    for (const std::unique_ptr<HostState>& host : hosts)
    {
        HostState& s = *host;
        if (s.destroyed || s.hwnd == nullptr || s.hwnd == r.modal_timer)
        {
            continue;
        }
        std::optional<double> wait = busy ? std::optional<double>{TickIntervalMs} : std::nullopt;
        if (const std::optional<Duration> next = s.window ? s.window->next_wake() : std::nullopt)
        {
            wait = wait ? (std::min)(*wait, next->count()) : next->count();
        }
        const UINT period = wait ? static_cast<UINT>((std::clamp)(std::ceil(*wait),
                                                                  static_cast<double>(USER_TIMER_MINIMUM),
                                                                  static_cast<double>(USER_TIMER_MAXIMUM)))
                                 : 0;
        // Re-armed only when it has to go off sooner, or once it has gone off: re-arming it after
        // every message would keep postponing it under a stream of mouse moves. Gone off means its
        // WM_TIMER came, not that its time passed: one can come up to a timer step (~15.6 ms)
        // early, before the after() it was set for is due, and needs re-arming for the rest.
        const bool sooner = period != 0 && (s.timer_ms == 0 || now + period < s.timer_next_ms);
        if (period == s.timer_ms || (!sooner && !s.timer_fired))
        {
            continue;
        }
        if (period != 0)
        {
            SetTimer(s.hwnd, TickTimerId, period, nullptr);
        }
        else
        {
            KillTimer(s.hwnd, TickTimerId);
        }
        s.timer_ms      = period;
        s.timer_next_ms = now + period;
        s.timer_fired   = false;
    }
}

// message_loop() ticks after every message, so its own WM_TIMER only wakes it while idle. A
// WM_TIMER that arrives while message_loop() is not the one dispatching comes from a modal loop
// someone else runs: a window drag or resize (DefWindowProc's move/size loop), a system or context
// menu, a file picker, a message box. Those never return to message_loop() until they end, so the
// tick happens here, and the timer speeds up to frame rate until message_loop() gets control back.
//
// SFML needs fibers for the same problem (#3752), because there the app owns the loop and the modal
// loop has to hand control back to it. Here the loop is libwinui's own, so ticking from the window
// procedure is the same thing without a second stack.
void tick_from_timer(HWND hwnd)
{
    Registry& r = registry();
    if (r.dispatching_own_timer || !r.running)
    {
        return;
    }
    if (r.modal_timer == nullptr)
    {
        r.modal_timer = hwnd;
        SetTimer(hwnd, TickTimerId, ModalTickIntervalMs, nullptr);
    }
    tick_all();
}

// After a modal loop: back to the idle wake-up rate.
void end_modal_ticks()
{
    Registry& r = registry();
    if (r.modal_timer != nullptr)
    {
        if (IsWindow(r.modal_timer))
        {
            SetTimer(r.modal_timer, TickTimerId, TickIntervalMs, nullptr);
            for (const std::unique_ptr<HostState>& host : r.hosts)
            {
                if (host->hwnd == r.modal_timer)
                {
                    host->timer_ms      = TickIntervalMs;
                    host->timer_next_ms = now_ms() + TickIntervalMs;
                }
            }
        }
        r.modal_timer = nullptr;
    }
}

int message_loop()
{
    // One message, then a tick, every iteration. Continuous WM_MOUSEMOVE starves WM_TIMER, and both
    // a drain-then-tick loop and a 60 Hz throttle made animations stall or stutter.
    Registry& r = registry();
    MSG       msg{};
    r.last_tick_ms = now_ms();
    for (;;)
    {
        if (!PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            detail::note_milestone(detail::Milestone::FirstIdle);
            // Nothing to do: a good moment for what the first frame did not need.
            for (const std::unique_ptr<HostState>& host : r.hosts)
            {
                if (!host->destroyed && host->compositor)
                {
                    host->compositor->prepare();
                }
            }
            // Block while idle. While a window is busy, WM_TIMER guarantees a wake-up within
            // TickIntervalMs; once none has been for IdleGraceMs, nothing wakes the loop but a message.
            if (GetMessageW(&msg, nullptr, 0, 0) <= 0)
            {
                return static_cast<int>(msg.wParam);
            }
        }
        if (msg.message == WM_QUIT)
        {
            return static_cast<int>(msg.wParam);
        }
        // Tick before dispatching too, or an animation a click starts gets all the idle time since the
        // last message on its first tick and skips that much.
        tick_all();
        TranslateMessage(&msg);
        r.dispatching_own_timer = msg.message == WM_TIMER;
        DispatchMessageW(&msg);
        r.dispatching_own_timer = false;
        end_modal_ticks();
        sweep_destroyed();
        detail::release_closed_windows();
        tick_all();
    }
}

// The loop, then every window still open closes with the app, so each on_closed() runs while
// every Window still exists.
int run_loop()
{
    Registry& r    = registry();
    r.running      = true;
    const int code = message_loop();
    // Before the windows go: the main one would otherwise post a WM_QUIT from its WM_DESTROY
    // that nobody is left to take, and the next run() or picker would end on it at once.
    r.main = nullptr;
    for (std::size_t i = 0; i < r.hosts.size(); ++i)
    {
        HostState& s = *r.hosts[i];
        if (!s.destroyed && s.hwnd != nullptr && IsWindow(s.hwnd))
        {
            DestroyWindow(s.hwnd);
        }
    }
    r.running = false;
    r.hosts.clear();
    detail::release_closed_windows(); // every one closed with the loop
    return code;
}

// The live host of `window`, if this backend made one.
HostState* find_host(const Window& window)
{
    for (const std::unique_ptr<HostState>& s : registry().hosts)
    {
        if (s->window == &window && !s->destroyed)
        {
            return s.get();
        }
    }
    return nullptr;
}

} // namespace

} // namespace ui::win32

// ---- what the portable Window asks of the backend (src/platform/Backend.hpp)
namespace ui::detail
{

bool create_native_window(Window& window, bool visible, bool activate)
{
    if (win32::find_host(window) != nullptr)
    {
        return true;
    }
    return win32::create_host(window, visible, activate) != nullptr;
}

void destroy_native_window(Window& window)
{
    win32::HostState* state = win32::find_host(window);
    if (state == nullptr)
    {
        return;
    }
    if (state->hwnd != nullptr && IsWindow(state->hwnd))
    {
        DestroyWindow(state->hwnd); // WM_DESTROY runs on_closed() while the Window still exists
    }
    // The state is swept at the loop's next safe point; nothing may reach the Window through it.
    state->destroyed = true;
    state->window    = nullptr;
}

} // namespace ui::detail

// ---- the app loop (winui/ui/App.hpp)
namespace ui
{

int run(Window& window)
{
    win32::Registry& r = win32::registry();
    if (r.running)
    {
        return -1; // not re-entrant
    }
    win32::HostState* main = win32::find_host(window);
    if (main == nullptr)
    {
        main = win32::create_host(window, window.options().visible);
    }
    if (main == nullptr)
    {
        return -1;
    }
    r.main = main;
    return win32::run_loop();
}

int run()
{
    if (win32::registry().running)
    {
        return -1;
    }
    return win32::run_loop();
}

void quit(int exit_code)
{
    PostQuitMessage(exit_code);
}

} // namespace ui

#endif // _WIN32
