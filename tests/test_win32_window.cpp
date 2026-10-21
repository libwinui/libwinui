// SPDX-License-Identifier: MIT
// The Win32 backend itself: real OS windows, driven through their message queue. Windows only and
// tagged [win32], since they need a desktop session (a Windows CI runner has one). Every window is
// created hidden or far off screen and never activated on purpose, so nothing takes the focus.
#ifdef _WIN32

#include "platform/windows/Win32AutomationProvider.hpp"
#include "platform/windows/Win32Pointer.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/App.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/media/InkCanvas.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdlib>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "winui/ui/input/Button.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/TextBox.hpp"

#include <dwmapi.h>
#include <windows.h>

using namespace ui;

namespace
{

HWND hwnd_of(const Window& window)
{
    return window.native_handle();
}

// The center of the client area, in client pixels, as a mouse message carries it.
LPARAM client_center(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    return MAKELPARAM((rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2);
}

void post_click(HWND hwnd)
{
    const LPARAM at = client_center(hwnd);
    PostMessageW(hwnd, WM_MOUSEMOVE, 0, at);
    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, at);
    PostMessageW(hwnd, WM_LBUTTONUP, 0, at);
}

// What DefWindowProc's move/size loop, a menu or a file picker does: a message loop of its own,
// which libwinui's loop does not get control back from until it ends.
void run_foreign_modal_loop(std::chrono::milliseconds duration)
{
    const auto until = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < until)
    {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    }
}

} // namespace

TEST_CASE("a mouse click posted to the OS window reaches the button under it", "[win32]")
{
    Window window{{.title = "click", .size = {300, 200}, .visible = false}};
    int    clicks = 0;
    window.set_content(ui::Button{.text                 = "Press",
                                  .horizontal_alignment = Alignment::Stretch,
                                  .vertical_alignment   = Alignment::Stretch,
                                  .on_click             = [&] { ++clicks; }});
    window.post(
        [&]
        {
            post_click(hwnd_of(window));
            // After the click messages, so they are handled first.
            window.post([] { ui::quit(0); });
        });
    REQUIRE(ui::run(window) == 0);
    CHECK(clicks == 1);
}

TEST_CASE("windows keep ticking while a modal loop that is not libwinui's runs", "[win32][tick]")
{
    Window window{{.title = "modal", .size = {300, 200}, .visible = false}};
    int    ticks_during_loop = 0;
    bool   in_loop           = false;
    window.on_tick(
        [&](Duration)
        {
            if (in_loop)
            {
                ++ticks_during_loop;
            }
        });
    // From a click handler, as a real picker or drag would be: inside a dispatched message.
    window.set_content(ui::Button{.text                 = "Open",
                                  .horizontal_alignment = Alignment::Stretch,
                                  .vertical_alignment   = Alignment::Stretch,
                                  .on_click             = [&]
                                  {
                                      in_loop = true;
                                      run_foreign_modal_loop(std::chrono::milliseconds{400});
                                      in_loop = false;
                                      ui::quit(0);
                                  }});
    window.post([&] { post_click(hwnd_of(window)); });
    REQUIRE(ui::run(window) == 0);
    // The idle timer alone would wake it every 100ms; inside a modal loop it runs at frame rate.
    CHECK(ticks_during_loop >= 8);
}

TEST_CASE("a modal loop started from inside a tick does not tick re-entrantly", "[win32][tick]")
{
    Window window{{.visible = false}};
    int    depth     = 0;
    int    max_depth = 0;
    window.on_tick(
        [&](Duration)
        {
            ++depth;
            max_depth = std::max(max_depth, depth);
            --depth;
        });
    // post() work runs inside tick(): a picker opened from there runs its loop inside a tick.
    window.post(
        [&]
        {
            ++depth;
            run_foreign_modal_loop(std::chrono::milliseconds{150});
            --depth;
            ui::quit(0);
        });
    REQUIRE(ui::run(window) == 0);
    CHECK(max_depth == 1);
}

TEST_CASE("open_window() hands a window to the loop, which destroys it once its own button closed it",
          "[win32][open-window]")
{
    using namespace std::chrono_literals;
    Window       main_window{{.visible = false}};
    WindowRef    opened;
    bool         was_open  = false;
    bool         closed    = false;
    bool         destroyed = false;
    Subscription check;
    main_window.post(
        [&]
        {
            auto second = std::make_unique<Window>(
                WindowOptions{.size = {200, 120}, .position = Point{-30000.0f, -30000.0f}, .visible = false});
            Window& w = *second;
            w.set_content(ui::Button{.text                 = "Close",
                                     .horizontal_alignment = Alignment::Stretch,
                                     .vertical_alignment   = Alignment::Stretch,
                                     .on_click             = [&w] { w.close(); }});
            w.on_closed([&] { closed = true; });
            Window& shown = ui::open_window(std::move(second));
            opened        = shown.ref();
            was_open      = shown.is_open().get();
            post_click(hwnd_of(shown));
            // Polled, since a slow machine may not have laid the window out when the first click
            // arrives: click again until it closes, and give up after 5 s.
            const auto give_up = std::chrono::steady_clock::now() + 5s;
            check              = main_window.every(50ms,
                                                   [&, give_up]
                                                   {
                                          if (opened.get() == nullptr)
                                          {
                                              destroyed = true;
                                              ui::quit(0);
                                          }
                                          else if (std::chrono::steady_clock::now() > give_up)
                                          {
                                              ui::quit(0);
                                          }
                                          else if (!closed)
                                          {
                                              post_click(hwnd_of(*opened.get()));
                                          }
                                                   });
        });
    REQUIRE(ui::run(main_window) == 0);
    CHECK(was_open);
    CHECK(closed);
    CHECK(destroyed);
}

TEST_CASE("show(false) creates the OS window without activating it", "[win32]")
{
    Window main_window{{.visible = false}};
    bool   visible = false;
    bool   active  = true;
    main_window.post(
        [&]
        {
            Window other{{.size = {100, 100}, .position = Point{-30000.0f, -30000.0f}}};
            other.show_inactive();
            const HWND hwnd = hwnd_of(other);
            visible         = hwnd != nullptr && IsWindowVisible(hwnd) != FALSE;
            active          = hwnd != nullptr && GetActiveWindow() == hwnd;
            ui::quit(0);
        });
    REQUIRE(ui::run(main_window) == 0);
    CHECK(visible);
    CHECK_FALSE(active);
}

TEST_CASE("a theme the app pinned survives a settings broadcast", "[win32][theme]")
{
    Window window{{.visible = false}};
    Color  before{};
    Color  after{};
    bool   pinned = false;
    window.post(
        [&]
        {
            ThemeColors colors = Theme::light().colors();
            colors.background  = Color::from_argb(0xFF123456);
            Theme custom       = Theme::fluent(colors);
            window.pin_theme(std::move(custom));
            before = window.theme().colors().background;
            // What Windows sends every window when the app mode or accent color changes.
            SendMessageW(hwnd_of(window), WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
            after  = window.theme().colors().background;
            pinned = window.theme_pinned();
            ui::quit(0);
        });
    REQUIRE(ui::run(window) == 0);
    CHECK(pinned);
    CHECK(after == before);
}

TEST_CASE("a broadcast that changes no OS theme setting changes nothing", "[win32][theme]")
{
    Window   window{{.visible = false}};
    unsigned before           = 0;
    unsigned after            = 0;
    bool     power_repaints   = true;
    bool     session_repaints = true;
    window.post(
        [&]
        {
            const HWND hwnd = hwnd_of(window);
            ValidateRect(hwnd, nullptr);
            before = window.theme_revision();
            // Registering for the power settings sends their current values like this right away.
            SendMessageW(hwnd, WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE, 0);
            power_repaints = GetUpdateRect(hwnd, nullptr, FALSE) != FALSE;
            SendMessageW(hwnd, WM_WTSSESSION_CHANGE, WTS_CONSOLE_CONNECT, 0);
            session_repaints = GetUpdateRect(hwnd, nullptr, FALSE) != FALSE;
            SendMessageW(hwnd, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
            after = window.theme_revision();
            ui::quit(0);
        });
    REQUIRE(ui::run(window) == 0);
    CHECK(after == before);
    CHECK_FALSE(power_repaints);
    CHECK_FALSE(session_repaints);
}

TEST_CASE("a frame with rounded top corners cuts them out of the window", "[win32][window-frame]")
{
    Window window{{.size    = {320, 200},
                   .theme   = ThemeRequest{.variant = ThemeVariant::Luna},
                   .style   = {.frame = Frame::Themed},
                   .visible = false}};
    bool   top_corners_cut = false;
    bool   rest_kept       = false;
    bool   square_after    = false;
    window.post(
        [&]
        {
            const HWND hwnd = hwnd_of(window);
            RECT       rc{};
            GetWindowRect(hwnd, &rc);
            const int w      = rc.right - rc.left;
            const int h      = rc.bottom - rc.top;
            HRGN      region = CreateRectRgn(0, 0, 0, 0);
            if (GetWindowRgn(hwnd, region) == COMPLEXREGION)
            {
                top_corners_cut = !PtInRegion(region, 0, 0) && !PtInRegion(region, w - 1, 0);
                // Kept, the bottom-left pixel among them: the host asks for a frame there, and the
                // OS drops an invalidation outside the region.
                rest_kept = PtInRegion(region, w / 2, 0) && PtInRegion(region, 0, h - 1) &&
                            PtInRegion(region, w - 1, h - 1);
            }
            window.pin_theme(resolve_theme({.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{}));
            SendMessageW(hwnd, WM_PAINT, 0, 0); // a theme the app installs reaches the frame on the next paint
            square_after = GetWindowRgn(hwnd, region) == ERROR;
            DeleteObject(region);
            ui::quit(0);
        });
    REQUIRE(ui::run(window) == 0);
    CHECK(top_corners_cut);
    CHECK(rest_kept);
    CHECK(square_after);
}

TEST_CASE("the title reaches the OS window as UTF-16", "[win32]")
{
    Window       window{{.title = "Grüße 🙂", .visible = false}};
    std::wstring title;
    window.post(
        [&]
        {
            wchar_t buffer[64]{};
            GetWindowTextW(hwnd_of(window), buffer, 64);
            title = buffer;
            window.set_title("Ünïcödé");
            GetWindowTextW(hwnd_of(window), buffer, 64);
            title += L"|";
            title += buffer;
            ui::quit(0);
        });
    REQUIRE(ui::run(window) == 0);
    CHECK(title == L"Grüße \U0001F642|Ünïcödé");
}

TEST_CASE("a frame the theme draws still takes title changes, without the OS painting a caption",
          "[win32][window-frame]")
{
    // Shown, off screen: a title change on a visible window runs with redrawing locked.
    Window       window{{.title    = "Before",
                         .theme    = ThemeRequest{.variant = ThemeVariant::Luna},
                         .style    = {.frame = Frame::Themed},
                         .position = Point{-30000.0f, -30000.0f}}};
    std::wstring title;
    bool         redraw_back = false;
    window.post(
        [&]
        {
            const HWND hwnd = hwnd_of(window);
            window.set_title("After");
            wchar_t buffer[64]{};
            GetWindowTextW(hwnd, buffer, 64);
            title       = buffer;
            redraw_back = IsWindowVisible(hwnd) != FALSE; // WM_SETREDRAW FALSE hides it until TRUE
            ui::quit(0);
        });
    REQUIRE(ui::run(window) == 0);
    CHECK(title == L"After");
    CHECK(redraw_back);
}

TEST_CASE("leaving a theme that draws the frame gives the frame back to DWM", "[win32][window-frame]")
{
    // On screen, and switched from another thread with pauses so the window idles in between the
    // way an app does: that is when uxtheme put a region of its own on the window.
    const ThemeVariant from = GENERATE(ThemeVariant::Luna, ThemeVariant::Aero);
    Window             window{
        {.title = "Frame", .backdrop = Backdrop::Mica, .style = {.frame = Frame::Themed}, .position = Point{100.0f, 100.0f}}};
    struct FrameState
    {
        bool composed = false;
        bool region   = false;
    };
    const auto frame_state = [&]
    {
        const HWND hwnd     = hwnd_of(window);
        BOOL       composed = FALSE;
        DwmGetWindowAttribute(hwnd, DWMWA_NCRENDERING_ENABLED, &composed, sizeof(composed));
        HRGN      probe  = CreateRectRgn(0, 0, 0, 0);
        const int region = GetWindowRgn(hwnd, probe);
        DeleteObject(probe);
        return FrameState{composed != FALSE, region != ERROR};
    };
    FrameState  drawn;
    FrameState  back;
    std::thread driver(
        [&]
        {
            const auto after_a_pause = [&](auto step)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
                window.post(step);
            };
            after_a_pause([&] { window.set_theme({.variant = from}); });
            after_a_pause(
                [&]
                {
                    drawn = frame_state();
                    window.set_theme({.variant = ThemeVariant::Fluent2});
                });
            after_a_pause(
                [&]
                {
                    back = frame_state();
                    ui::quit(0);
                });
        });
    REQUIRE(ui::run(window) == 0);
    driver.join();
    CHECK(drawn.region == (from == ThemeVariant::Luna)); // Luna's corners are cut, Aero's glass is not
    CHECK(back.composed);
    CHECK_FALSE(back.region);
}

TEST_CASE("characters typed into the OS window reach the focused text box, surrogate pairs too", "[win32][text]")
{
    Window      window{{.visible = false}};
    std::string text;
    window.set_content(ui::TextBox{.on_text_changed = [&](const std::string& t) { text = t; }});
    window.post(
        [&]
        {
            const HWND hwnd = hwnd_of(window);
            PostMessageW(hwnd, WM_KEYDOWN, VK_TAB, 0); // focus the only control there is
            PostMessageW(hwnd, WM_KEYUP, VK_TAB, 0);
            PostMessageW(hwnd, WM_CHAR, L'a', 0);
            PostMessageW(hwnd, WM_CHAR, 0xD83D, 0); // U+1F642, as the two halves Windows sends
            PostMessageW(hwnd, WM_CHAR, 0xDE42, 0);
            window.post([] { ui::quit(0); });
        });
    REQUIRE(ui::run(window) == 0);
    CHECK(text == "a\xF0\x9F\x99\x82");
}

TEST_CASE("resizing a minimized window keeps it minimized and sizes what it restores to", "[win32]")
{
    Window window{{.size = {400, 300}, .position = Point{-30000.0f, -30000.0f}, .visible = false}};
    bool   still_minimized = false;
    LONG   restored_width  = 0;
    LONG   restored_height = 0;
    LONG   expected_width  = 0;
    LONG   expected_height = 0;
    window.post(
        [&]
        {
            const HWND hwnd = hwnd_of(window);
            ShowWindow(hwnd, SW_SHOWNOACTIVATE); // restored once, so its frame is known
            ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
            window.resize({250.0f, 150.0f});
            still_minimized = IsIconic(hwnd) != FALSE;
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            RECT client{};
            GetClientRect(hwnd, &client);
            restored_width  = client.right;
            restored_height = client.bottom;
            const UINT dpi  = GetDpiForWindow(hwnd);
            expected_width  = MulDiv(250, static_cast<int>(dpi), 96);
            expected_height = MulDiv(150, static_cast<int>(dpi), 96);
            ui::quit(0);
        });
    REQUIRE(ui::run(window) == 0);
    CHECK(still_minimized);
    CHECK(std::abs(restored_width - expected_width) <= 1);
    CHECK(std::abs(restored_height - expected_height) <= 1);
}

TEST_CASE("restores_to() follows the OS: a window minimized from maximized goes back to it", "[win32]")
{
    Window      window{{.size = {400, 300}, .position = Point{-30000.0f, -30000.0f}, .visible = false}};
    WindowState maximized_restores_to = WindowState::Minimized;
    WindowState minimized_restores_to = WindowState::Minimized;
    WindowState restored_state        = WindowState::Minimized;
    WindowState restored_restores_to  = WindowState::Minimized;
    window.post(
        [&]
        {
            const HWND hwnd = hwnd_of(window);
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            window.request_state(WindowState::Maximized);
            maximized_restores_to = window.restores_to().get();
            ShowWindow(hwnd, SW_SHOWMINNOACTIVE); // as the taskbar button would
            minimized_restores_to = window.restores_to().get();
            window.restore();
            restored_state       = window.state().get();
            restored_restores_to = window.restores_to().get();
            ui::quit(0);
        });
    REQUIRE(ui::run(window) == 0);
    CHECK(maximized_restores_to == WindowState::Normal);
    CHECK(minimized_restores_to == WindowState::Maximized);
    CHECK(restored_state == WindowState::Maximized);
    CHECK(restored_restores_to == WindowState::Normal);
}

TEST_CASE("resizing a maximized window keeps it maximized and sizes what it restores to", "[win32]")
{
    // There is no show command that maximizes without activating, so this one window does take the
    // focus, for as long as the test runs.
    Window window{{.size = {400, 300}, .position = Point{-30000.0f, -30000.0f}, .visible = false}};
    bool   still_maximized = false;
    bool   live_unchanged  = false;
    LONG   normal_width    = 0;
    LONG   normal_height   = 0;
    LONG   restored_width  = 0;
    LONG   restored_height = 0;
    LONG   expected_width  = 0;
    LONG   expected_height = 0;
    window.post(
        [&]
        {
            const HWND hwnd = hwnd_of(window);
            ShowWindow(hwnd, SW_SHOWNOACTIVATE); // restored once, so its frame is known
            window.request_state(WindowState::Maximized);
            RECT before{};
            GetWindowRect(hwnd, &before);
            window.resize({250.0f, 150.0f});
            still_maximized = IsZoomed(hwnd) != FALSE;
            RECT after{};
            GetWindowRect(hwnd, &after);
            live_unchanged = EqualRect(&before, &after) != FALSE;
            WINDOWPLACEMENT placement{};
            placement.length = sizeof(placement);
            GetWindowPlacement(hwnd, &placement);
            normal_width  = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
            normal_height = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;

            ShowWindow(hwnd, SW_RESTORE);
            RECT restored{};
            GetClientRect(hwnd, &restored);
            restored_width  = restored.right - restored.left;
            restored_height = restored.bottom - restored.top;
            const int dpi   = static_cast<int>(GetDpiForWindow(hwnd));
            expected_width  = MulDiv(250, dpi, 96);
            expected_height = MulDiv(150, dpi, 96);
            ui::quit(0);
        });
    REQUIRE(ui::run(window) == 0);
    CHECK(still_maximized);
    CHECK(live_unchanged);
    CHECK(std::abs(restored_width - expected_width) <= 1);
    CHECK(std::abs(restored_height - expected_height) <= 1);
    // The restored outer size is the client size plus the frame.
    CHECK(normal_width > expected_width);
    CHECK(normal_height > expected_height);
}

TEST_CASE("a closed window keeps no service of its host", "[win32]")
{
    Window window{{.visible = false}};
    bool   had_host = false;
    window.post(
        [&]
        {
            had_host = ui::testing::seams(window).window_host() != nullptr;
            ui::quit(0);
        });
    REQUIRE(ui::run(window) == 0);
    CHECK(had_host);
    CHECK(ui::testing::seams(window).window_host() == nullptr);
    CHECK(window.native_handle() == nullptr);
}

TEST_CASE("pen and touch data is read off the pointer info and the mouse message's signature", "[win32][pointer]")
{
    using namespace ui::win32;
    CHECK(device_from_extra_info(0) == PointerDeviceType::Mouse);
    CHECK(device_from_extra_info(0x12345678) == PointerDeviceType::Mouse);
    CHECK(device_from_extra_info(0xFF515700) == PointerDeviceType::Pen);
    CHECK(device_from_extra_info(0xFF515701) == PointerDeviceType::Pen);
    CHECK(device_from_extra_info(0xFF515780) == PointerDeviceType::Touch);

    POINTER_PEN_INFO pen{};
    pen.penMask       = PEN_MASK_PRESSURE | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
    pen.pressure      = 768;
    pen.tiltX         = -30;
    pen.tiltY         = 120; // beyond what a pen can report: clamped
    pen.penFlags      = PEN_FLAG_BARREL;
    PointerSample tip = pen_sample(pen);
    CHECK(tip.device == PointerDeviceType::Pen);
    CHECK(tip.pressure == 0.75f);
    CHECK(tip.tilt_x == -30.0f);
    CHECK(tip.tilt_y == 90.0f);
    CHECK(tip.barrel_button);
    CHECK_FALSE(tip.eraser);

    pen.penFlags = PEN_FLAG_INVERTED;
    CHECK(pen_sample(pen).eraser);
    pen.penFlags = PEN_FLAG_ERASER;
    CHECK(pen_sample(pen).eraser);
    pen.penMask = PEN_MASK_NONE; // a pen without a pressure sensor draws like the mouse
    CHECK(pen_sample(pen).pressure == 0.5f);
    CHECK(pen_sample(pen).tilt_x == 0.0f);

    POINTER_TOUCH_INFO finger{};
    CHECK(touch_sample(finger).device == PointerDeviceType::Touch);
    CHECK(touch_sample(finger).pressure == 0.5f);
    finger.touchMask = TOUCH_MASK_PRESSURE;
    finger.pressure  = 256;
    CHECK(touch_sample(finger).pressure == 0.25f);

    // The last frame is only taken for the device the mouse message says it came from.
    PointerEvent e{{}, PointerButton::Left, PointerAction::Down};
    apply_pointer_source(e, 0xFF515700, tip);
    CHECK(e.device == PointerDeviceType::Pen);
    CHECK(e.pressure == 0.75f);
    CHECK(e.barrel_button);
    apply_pointer_source(e, 0xFF515780, tip);
    CHECK(e.device == PointerDeviceType::Touch);
    CHECK(e.pressure == 0.5f);
    CHECK_FALSE(e.barrel_button);
    apply_pointer_source(e, 0, tip);
    CHECK(e.device == PointerDeviceType::Mouse);
    CHECK(e.pressure == 0.5f);
    CHECK(e.tilt_x == 0.0f);
}

TEST_CASE("mouse messages made from a pen or a finger say so, and an ink canvas tells them apart", "[win32][pointer]")
{
    Window                     window{{.title = "ink", .size = {300, 200}, .visible = false}};
    ui::Control<ui::InkCanvas> canvas;
    window.set_content(ui::InkCanvas{.control = canvas});
    // Sent, not posted: a sent message leaves the extra info SetMessageExtraInfo() put there.
    const auto drag = [&](LPARAM extra, int y)
    {
        const HWND hwnd = hwnd_of(window);
        SetMessageExtraInfo(extra);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, y));
        SetMessageExtraInfo(extra);
        SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(60, y));
        SetMessageExtraInfo(extra);
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(100, y));
        SetMessageExtraInfo(0);
    };
    window.post(
        [&]
        {
            drag(static_cast<LPARAM>(0xFF515700), 20); // pen
            drag(static_cast<LPARAM>(0xFF515780), 40); // touch: not an input device by default
            drag(0, 60);                               // mouse
            window.post([] { ui::quit(0); });
        });
    REQUIRE(ui::run(window) == 0);
    REQUIRE(canvas->strokes().size() == 2);
    CHECK(canvas->strokes().strokes()[0].points.front().pressure == 0.5f); // no pen frame seen: the default
}

TEST_CASE("an element's accessibility description is its UI Automation HelpText", "[win32][automation]")
{
    Window         window;
    ButtonControl* button = window.set_content(std::make_unique<ButtonControl>("Save"));
    button->set_accessibility({.name = "Save document", .description = "Saves the current document"});

    Microsoft::WRL::ComPtr<ui::win32::Win32AutomationProvider> provider;
    provider.Attach(new ui::win32::Win32AutomationProvider(nullptr,
                                                           &window,
                                                           button,
                                                           std::make_shared<ui::win32::AutomationLifetime>(),
                                                           /*is_root=*/false));

    VARIANT help{};
    REQUIRE(provider->GetPropertyValue(UIA_HelpTextPropertyId, &help) == S_OK);
    REQUIRE(help.vt == VT_BSTR);
    CHECK(std::wstring(help.bstrVal) == L"Saves the current document");
    VariantClear(&help);

    button->set_accessibility({.name = "Save document"});
    REQUIRE(provider->GetPropertyValue(UIA_HelpTextPropertyId, &help) == S_OK);
    CHECK(help.vt == VT_EMPTY); // no description: UIA's default
}

TEST_CASE("a disabled element is not enabled for UI Automation, nor is anything inside a disabled one",
          "[win32][automation]")
{
    Window         window;
    StackControl*  stack  = window.set_content(std::make_unique<StackControl>(Orientation::Vertical));
    ButtonControl* button = stack->add(std::make_unique<ButtonControl>("Save"));

    Microsoft::WRL::ComPtr<ui::win32::Win32AutomationProvider> provider;
    provider.Attach(new ui::win32::Win32AutomationProvider(nullptr,
                                                           &window,
                                                           button,
                                                           std::make_shared<ui::win32::AutomationLifetime>(),
                                                           /*is_root=*/false));
    const auto enabled = [&provider]
    {
        VARIANT value{};
        REQUIRE(provider->GetPropertyValue(UIA_IsEnabledPropertyId, &value) == S_OK);
        REQUIRE(value.vt == VT_BOOL);
        return value.boolVal == VARIANT_TRUE;
    };

    CHECK(enabled());
    button->set_enabled(false);
    CHECK_FALSE(enabled());
    button->set_enabled(true);
    stack->set_enabled(false);
    CHECK_FALSE(enabled());
}

TEST_CASE("a provider the automation tree hands out carries exactly the caller's reference", "[win32][automation]")
{
    Window        window;
    StackControl* stack = window.set_content(std::make_unique<StackControl>(Orientation::Vertical));
    stack->add(std::make_unique<ButtonControl>("Save"));

    Microsoft::WRL::ComPtr<ui::win32::Win32AutomationProvider> root;
    root.Attach(new ui::win32::Win32AutomationProvider(nullptr,
                                                       &window,
                                                       stack,
                                                       std::make_shared<ui::win32::AutomationLifetime>(),
                                                       /*is_root=*/true));

    IRawElementProviderFragment* child = nullptr;
    REQUIRE(root->Navigate(NavigateDirection_FirstChild, &child) == S_OK);
    REQUIRE(child != nullptr);
    CHECK(child->AddRef() == 2);
    CHECK(child->Release() == 1);
    CHECK(child->Release() == 0); // the caller's release frees it
}

TEST_CASE("UI Automation calls the providers on the UI thread, and Invoke returns before the click runs",
          "[win32][automation]")
{
    Window         window;
    int            clicks = 0;
    ButtonControl* button = window.set_content(std::make_unique<ButtonControl>("Save"));
    button->on_click([&] { ++clicks; });

    Microsoft::WRL::ComPtr<ui::win32::Win32AutomationProvider> provider;
    provider.Attach(new ui::win32::Win32AutomationProvider(nullptr,
                                                           &window,
                                                           button,
                                                           std::make_shared<ui::win32::AutomationLifetime>(),
                                                           /*is_root=*/false));

    ProviderOptions options{};
    REQUIRE(provider->get_ProviderOptions(&options) == S_OK);
    CHECK((options & ProviderOptions_UseComThreading) != 0);

    REQUIRE(provider->Invoke() == S_OK);
    CHECK(clicks == 0); // a handler that opens a dialog or a picker must not hold up the client
    window.post([] { ui::quit(0); });
    REQUIRE(ui::run(window) == 0);
    CHECK(clicks == 1);

    button->set_enabled(false);
    CHECK(provider->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED));
}

TEST_CASE("The window's automation root leaves its type and name to the window", "[win32][automation]")
{
    Window        window;
    StackControl* stack = window.set_content(std::make_unique<StackControl>(Orientation::Vertical));
    stack->set_accessibility({.name = "Form"});

    const auto property = [&](bool is_root, PROPERTYID id)
    {
        Microsoft::WRL::ComPtr<ui::win32::Win32AutomationProvider> provider;
        provider.Attach(new ui::win32::Win32AutomationProvider(nullptr,
                                                               &window,
                                                               stack,
                                                               std::make_shared<ui::win32::AutomationLifetime>(),
                                                               is_root));
        VARIANT value{};
        REQUIRE(provider->GetPropertyValue(id, &value) == S_OK);
        const VARTYPE type = value.vt;
        VariantClear(&value);
        return type;
    };

    // Empty: UIA takes them from the HWND, a Window with the title as its name.
    CHECK(property(true, UIA_ControlTypePropertyId) == VT_EMPTY);
    CHECK(property(true, UIA_NamePropertyId) == VT_EMPTY);
    // The same element anywhere else answers for itself.
    CHECK(property(false, UIA_ControlTypePropertyId) == VT_I4);
    CHECK(property(false, UIA_NamePropertyId) == VT_BSTR);
}

TEST_CASE("An element's AutomationId is the id the app gave it, and none without one", "[win32][automation]")
{
    Window         window;
    ButtonControl* button = window.set_content(std::make_unique<ButtonControl>("Save"));

    Microsoft::WRL::ComPtr<ui::win32::Win32AutomationProvider> provider;
    provider.Attach(new ui::win32::Win32AutomationProvider(nullptr,
                                                           &window,
                                                           button,
                                                           std::make_shared<ui::win32::AutomationLifetime>(),
                                                           /*is_root=*/false));
    VARIANT id{};
    REQUIRE(provider->GetPropertyValue(UIA_AutomationIdPropertyId, &id) == S_OK);
    CHECK(id.vt == VT_EMPTY);

    button->set_accessibility({.automation_id = "save-button"});
    REQUIRE(provider->GetPropertyValue(UIA_AutomationIdPropertyId, &id) == S_OK);
    REQUIRE(id.vt == VT_BSTR);
    CHECK(std::wstring(id.bstrVal) == L"save-button");
    VariantClear(&id);
}

TEST_CASE("The window's automation root follows a modal opening and closing", "[win32][automation]")
{
    Window        window;
    StackControl* stack = window.set_content(std::make_unique<StackControl>(Orientation::Vertical));
    stack->add(std::make_unique<ButtonControl>("Save"));

    Microsoft::WRL::ComPtr<ui::win32::Win32AutomationProvider> root;
    root.Attach(new ui::win32::Win32AutomationProvider(nullptr,
                                                       &window,
                                                       stack,
                                                       std::make_shared<ui::win32::AutomationLifetime>(),
                                                       /*is_root=*/true));
    const auto first_child_name = [&]
    {
        Microsoft::WRL::ComPtr<IRawElementProviderFragment> child;
        REQUIRE(root->Navigate(NavigateDirection_FirstChild, child.GetAddressOf()) == S_OK);
        REQUIRE(child != nullptr);
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
        REQUIRE(child.As(&simple) == S_OK);
        VARIANT name{};
        REQUIRE(simple->GetPropertyValue(UIA_NamePropertyId, &name) == S_OK);
        REQUIRE(name.vt == VT_BSTR);
        std::wstring text(name.bstrVal);
        VariantClear(&name);
        return text;
    };

    CHECK(first_child_name() == L"Save");
    StackControl* dialog = window.show_modal(std::make_unique<StackControl>(Orientation::Vertical));
    dialog->add(std::make_unique<ButtonControl>("OK"));
    CHECK(first_child_name() == L"OK"); // the same root provider, now over the modal
    window.close_modal();
    CHECK(first_child_name() == L"Save");
}

#endif // _WIN32
