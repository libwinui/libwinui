// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/SpellChecker.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/WindowShell.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include <cstddef>
#include <cstdint>

namespace ui::testing
{

/// Installs platform services on a Window: its host, compositor, native host, spell checker
/// and context-menu host. The clipboard and the launcher belong to the whole process instead, so
/// a test swaps them with set_clipboard() and set_launcher().
///
/// A window gets these from whatever hosts it. The backend installs the real ones when it creates
/// the native window, and HeadlessUI installs its own. A test uses this to hand the window a test
/// double. Every pointer is non-owning and must outlive the window's use of it; nullptr removes
/// the service. Get one with seams().
///
/// Example:
/// @code
/// ui::Window                       window;
/// ui::testing::TestContextMenuHost menu;
/// ui::testing::seams(window).set_context_menu_host(&menu);
/// @endcode
class WindowSeams
{
public:
    /// The seams of `window`.
    explicit WindowSeams(Window& window) : window_(window)
    {
    }

    /// The host that answers the window's requests (state, title, placement, ...).
    void set_window_host(WindowHost* host)
    {
        window_.set_window_host(host);
    }
    /// The host set_window_host() installed, or nullptr: none yet, or the window was closed.
    [[nodiscard]] WindowHost* window_host() const
    {
        return window_.window_host_;
    }
    /// Where composited overlays (shadows, acrylic) are drawn.
    void set_compositor(detail::Compositor* compositor)
    {
        window_.set_compositor(compositor);
    }
    /// What creates and places the native windows of NativeControls.
    void set_native_host(NativeHost* host)
    {
        window_.set_native_host(host);
    }
    /// The spell checker text controls ask.
    void set_spell_checker(SpellChecker* checker)
    {
        window_.set_spell_checker(checker);
    }
    /// What makes the spell checker the first time a text control needs one.
    void set_spell_checker_source(Callback<SpellChecker*()> source)
    {
        window_.set_spell_checker_source(std::move(source));
    }
    /// What shows a text control's context menu.
    void set_context_menu_host(ContextMenuHost* host)
    {
        window_.set_context_menu_host(host);
    }
    /// Where the window reports focus and property changes to automation clients.
    void set_automation_events(AutomationEvents* events)
    {
        window_.set_automation_events(events);
    }

    // ---- what the host tells the window ----
    /// The user asked to close the window (the close button, Alt+F4). False when on_closing() vetoed it.
    [[nodiscard]] bool handle_close_request()
    {
        return window_.handle_close_request();
    }
    /// The native window is gone.
    void handle_closed()
    {
        window_.handle_closed();
    }
    /// The native window exists and is about to show.
    void handle_opened()
    {
        window_.handle_opened();
    }
    /// The menu key: Alt or F10 alone (`letter` 0), or Alt with a letter nothing else used. True
    /// when an element such as a menu bar took it, so the system does nothing more.
    bool handle_menu_access(char32_t letter)
    {
        return window_.handle_menu_access(letter);
    }
    /// The window's state as the OS has it now.
    void report_state(WindowState state)
    {
        window_.report_state(state);
    }
    /// report_state() plus where restore() would land (Window::restores_to()), for a host that
    /// knows.
    void report_state(WindowState state, WindowState restores_to)
    {
        window_.report_state(state, restores_to);
    }
    /// Whether the window is the active one.
    void report_active(bool active)
    {
        window_.report_active(active);
    }
    /// Device pixels per DIP on the window's monitor.
    void report_scale(float scale)
    {
        window_.report_scale(scale);
    }
    /// The room the caption buttons take in a custom title bar.
    void report_caption_insets(const CaptionInsets& insets)
    {
        window_.report_caption_insets(insets);
    }
    /// Whether the window is shown.
    void report_visible(bool visible)
    {
        window_.report_visible(visible);
    }
    /// Whether the desktop behind the window is blurred for a glass frame.
    void report_blur_behind(bool active)
    {
        window_.report_blur_behind(active);
    }
    /// The small icon the OS shows for the window, as pixels.
    void report_caption_icon(Bitmap icon)
    {
        window_.report_caption_icon(std::move(icon));
    }
    /// The OS theme state, at creation and after every change.
    void report_os_theme(const OsThemeSnapshot& os)
    {
        window_.report_os_theme(os);
    }
    /// The user picked the app's item `index` in the system menu.
    void handle_system_menu_item(std::size_t index)
    {
        window_.handle_system_menu_item(index);
    }
    /// The user clicked taskbar thumbnail button `index`.
    void handle_thumbnail_button(std::size_t index)
    {
        window_.handle_thumbnail_button(index);
    }

    // ---- what the host asks the window ----
    /// style() with `Frame::Themed` resolved against the current theme, the style the native
    /// window runs with. That is `Borderless` while the theme draws the frame and `Standard` when
    /// it has none. Every other frame passes through.
    [[nodiscard]] WindowStyle effective_style() const
    {
        return window_.effective_style();
    }
    /// The width in DIPs of the border the theme draws, which is also the resize band; 0 when
    /// the OS draws the frame.
    [[nodiscard]] float drawn_frame_border() const
    {
        return window_.drawn_frame_border();
    }
    /// The icon a caption the theme draws shows when the window's icon() has no image or glyph of
    /// its own, as the host reported it; empty until it reports one.
    [[nodiscard]] const Bitmap& caption_icon() const
    {
        return window_.caption_icon();
    }
    /// The caption the theme draws, as an automation node (a TitleBar holding the caption
    /// buttons) for the host to list before the content; nullptr when the OS draws the frame.
    [[nodiscard]] Element* drawn_title_bar() const
    {
        return window_.drawn_title_bar();
    }
    /// Whether the frame the theme draws is glass and wants the desktop behind the window
    /// blurred: Aero's frame, while transparency effects are on and the window is not full
    /// screen. The host answers with report_blur_behind().
    [[nodiscard]] bool wants_blur_behind() const
    {
        return window_.wants_blur_behind();
    }
    /// The rounded top corners of the frame the theme draws, as the pixels to cut off each top
    /// corner, one entry per row from the top, at 96 DPI. Empty for a square frame, while the
    /// window is maximized or full screen, and while the frame is glass. The host shapes the
    /// window with it.
    [[nodiscard]] std::span<const std::uint8_t> frame_corner_cut() const
    {
        return window_.frame_corner_cut();
    }
    /// Whether the host reported the blur as live (report_blur_behind()).
    [[nodiscard]] bool blur_behind() const
    {
        return window_.blur_behind();
    }

    // ---- input ----
    /// A pointer event in client DIPs, routed to popups, the modal layer or the content.
    void handle_pointer(const PointerEvent& e)
    {
        window_.handle_pointer(e);
    }
    /// A key event, routed to the focused element after shortcuts and Tab navigation.
    bool handle_key(const KeyEvent& e)
    {
        return window_.handle_key(e);
    }
    /// Typed text for the focused element.
    void handle_text(std::string_view text)
    {
        window_.handle_text(text);
    }
    /// A drag from another app has come over the window at `position`, in client DIPs, carrying
    /// `data`. The drop targets there are asked as for a drag inside the app; returns their answer.
    /// Refused while a drag inside the app is running.
    DragResponse handle_external_drag_enter(DataPackage data, Point position)
    {
        return window_.handle_external_drag_enter(std::move(data), position);
    }
    /// The drag from another app moved to `position`; `requested` replaces the operation the
    /// package asks for (the keys held changed it). Returns the answer there.
    DragResponse handle_external_drag_over(Point position, std::optional<DataPackageOperation> requested = std::nullopt)
    {
        return window_.handle_external_drag_over(position, requested);
    }
    /// The drag from another app left the window, or was canceled.
    void handle_external_drag_leave()
    {
        window_.handle_external_drag_leave();
    }
    /// The drag from another app was released at `position`. Returns what the drop did, None when
    /// nothing took it.
    DataPackageOperation handle_external_drag_drop(Point position)
    {
        return window_.handle_external_drag_drop(position);
    }
    /// Files dropped on the window from another app at `position`, reported only on release: the
    /// enter and the drop at once. Returns what the drop did, None when nothing took it.
    DataPackageOperation handle_external_drop(DataPackage data, Point position)
    {
        return window_.handle_external_drop(std::move(data), position);
    }
    /// A context-menu request at `position`, or from the keyboard when nullopt. True when handled.
    bool handle_context_menu(std::optional<Point> position)
    {
        return window_.handle_context_menu(position);
    }
    /// An automation client invoking `element`, as a screen reader does to press a button. True
    /// when it was invoked.
    bool handle_automation_invoke(Element& element)
    {
        return window_.handle_automation_invoke(element);
    }
    /// A context-menu request from a hosted native control. True when handled.
    bool handle_native_context_menu(Element& native, std::optional<Point> position)
    {
        return window_.handle_native_context_menu(native, position);
    }
    /// A hosted native control's content changed.
    void handle_native_content_changed(NativeControl& native)
    {
        window_.handle_native_content_changed(native);
    }
    /// A notification from a hosted native control. True when the control handled it.
    bool handle_native_notification(NativeControl& native, NativeNotification& notification)
    {
        return window_.handle_native_notification(native, notification);
    }

private:
    Window& window_;
};

/// The seams of `window`, to install a service or a test double on it.
[[nodiscard]] inline WindowSeams seams(Window& window)
{
    return WindowSeams{window};
}

} // namespace ui::testing
