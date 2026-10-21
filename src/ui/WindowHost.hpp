// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/NativeHandle.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/WindowShell.hpp"
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/menus/MenuItem.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui
{

struct ChromeOverrides;

// Implemented by whatever hosts a Window. Every call is a request: a host applies what its
// platform supports and ignores the rest.
//
// Private to libwinui. The Win32 backend installs its HWND-backed host, HeadlessUI an in-memory
// one, and a test of the library a double, all with `ui::testing::seams(window).set_window_host()`. The Window forwards its
// setters here; a host installed later reads what was configured before it (Window::title(),
// style(), chrome(), ...) when it is ready for it. The host answers through the same seams:
// report_state(), report_active(), report_visible() and report_caption_insets(). They are
// private on Window itself.
// An "implement me" interface: every member has a default body that does nothing (or answers
// "no window"), so a double overrides only what its test looks at and keeps compiling when a
// member is added. UI thread only, except wake().
//
// Example:
// @code
// // The app talks to its Window, which hands each request to the host:
// window.set_title("Report.txt - Editor");          // WindowHost::set_title()
// window.request_state(ui::WindowState::Maximized); // WindowHost::request_state()
// // WindowHost::set_taskbar_progress(), on every change:
// window.taskbar_progress() = ui::TaskbarProgress{ui::TaskbarProgressState::Normal, 0.4};
//
// // The host may refuse a state or grant it later, so the app shows what the host reported:
// window.set_content(ui::Text{
//     .text = ui::bind(window.state(), [](ui::WindowState s) {
//         return std::string{s == ui::WindowState::Maximized ? "Maximized" : "Not maximized"}; }),
// });
// @endcode
class WindowHost
{
public:
    virtual ~WindowHost() = default;

    // Ask for a state. May be refused, may complete later; the answer arrives through
    // `ui::testing::seams(window).report_state()`, never as a return value.
    virtual void request_state(WindowState /*state*/)
    {
    }
    // SW_RESTORE's meaning, extended to full screen: leave Minimized or Fullscreen for
    // whatever was there before (Maximized, if that is where it was entered from); leave
    // Maximized for Normal; do nothing from Normal.
    virtual void restore()
    {
    }
    // Close the window, the way its close button would.
    virtual void close()
    {
    }

    // The caption, taskbar and Alt+Tab text (Window::set_title()).
    virtual void set_title(std::string_view /*title*/)
    {
    }
    // Applied live. While full screen, only remembered for the way back.
    virtual void set_style(const WindowStyle& /*style*/)
    {
    }
    // Merged over the theme's ChromeHints (merge_chrome()) every time the frame is pushed.
    virtual void set_chrome(const ChromeOverrides& /*chrome*/)
    {
    }
    // The current placement, or nullopt before a real window exists.
    [[nodiscard]] virtual std::optional<WindowPlacement> placement() const
    {
        return std::nullopt;
    }

    // ---- native extension points ----
    // The caption and taskbar icon (Window::set_icon()); nullptr: back to the default icon.
    virtual void set_icon(const WindowIcon* /*icon*/)
    {
    }
    // Replace the app's system-menu entries (Window::system_menu(), top level only). Picks are
    // reported back by index through `ui::testing::seams(window).handle_system_menu_item()`.
    virtual void set_system_menu(const std::vector<MenuItem>& /*items*/)
    {
    }
    // The taskbar button's progress bar, pushed on every change of Window::taskbar_progress().
    virtual void set_taskbar_progress(const TaskbarProgress& /*progress*/)
    {
    }
    // A badge over the taskbar button; nullptr: no overlay. `description` is what a screen
    // reader announces for it.
    virtual void set_taskbar_overlay(const WindowIcon* /*icon*/, std::string_view /*description*/)
    {
    }
    // Flash the taskbar button (and caption) until the window comes to the foreground; `false`
    // stops a flash in progress.
    virtual void request_attention(bool /*flash*/)
    {
    }
    // Set or update the thumbnail toolbar (at most seven; the set is fixed at the first call).
    // Clicks are reported back by index through
    // `ui::testing::seams(window).handle_thumbnail_button()`.
    virtual void set_thumbnail_buttons(const std::vector<ThumbnailButton>& /*buttons*/)
    {
    }

    // Get the UI thread to call Window::tick() soon, because Window::post() queued work for
    // it. The one member called from other threads, so it must not touch the Window. Default:
    // nothing, for a host that ticks on its own schedule anyway.
    virtual void wake()
    {
    }

    // Applied live (Window::set_backdrop()). Default: nothing, for a host with no system materials.
    virtual void set_backdrop(Backdrop /*backdrop*/)
    {
    }

    // The platform's own handle for the window: an HWND on Win32, nullptr where there is
    // none (before a real window exists). Opaque, like NativeHost::native_handle(); the
    // platform layer hands it to the OS (a dialog's owner, say). Window::native_handle() is the
    // typed form.
    [[nodiscard]] virtual NativeHandle native_handle() const
    {
        return nullptr;
    }

    // ---- position, size and visibility ----
    // DIPs at the window's DPI in screen coordinates, so bounds() and work_area() compare directly.
    // The defaults do nothing, for a host that has no screen.

    // Move the window's outer top-left to `position`.
    virtual void move(Point /*position*/)
    {
    }
    // Resize so the client area is `size`; the frame comes on top.
    virtual void resize(Size /*size*/)
    {
    }
    // The window's outer rect, or nullopt without a real window.
    [[nodiscard]] virtual std::optional<Rect> bounds() const
    {
        return std::nullopt;
    }
    // The work area (the monitor less the taskbar) of the display the window is mostly on.
    [[nodiscard]] virtual std::optional<Rect> work_area() const
    {
        return std::nullopt;
    }
    // Show or hide the window; the answer comes back through
    // `ui::testing::seams(window).report_visible()`.
    // `activate` brings a shown window to the front and gives it focus.
    virtual void set_visible(bool /*visible*/, bool /*activate*/)
    {
    }
    // Bring the window to the front and give it focus.
    virtual void activate()
    {
    }
    // The smallest and largest client size the user can drag the window to (zero on an axis:
    // unconstrained), applied to the current size at once.
    virtual void set_size_limits(Size /*min*/, Size /*max*/)
    {
    }
};

} // namespace ui
