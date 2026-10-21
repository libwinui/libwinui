// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Duration.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/NativeHandle.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/ThreadCheck.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/ConnectedAnimation.hpp"
#include "winui/ui/DragDrop.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/ParallelLayout.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/WindowOptions.hpp"
#include "winui/ui/WindowRef.hpp"
#include "winui/ui/WindowShell.hpp"
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ui
{

class ContextMenuHost;
class AutomationEvents;
struct Command;
class NativeHost;
class SpellChecker;
class WindowHost; // the backend's side of a window, private to libwinui

namespace testing
{
class WindowSeams;
}
class NativeControl;
struct NativeNotification;

namespace detail
{
struct MenuChecks;
// The measuring pass's stacks (src/ui/PaintTracker.hpp), kept from frame to frame.
struct PaintScratch;
struct PaintScratchDelete
{
    void operator()(PaintScratch* scratch) const noexcept;
};
} // namespace detail

/// What a Window's `on_closing` gets. It carries no fields yet.
struct WindowClosingArgs
{
};

/// The logical, platform-independent window: the root of a UI tree. It owns its content (and an
/// open modal), lays it out, routes pointer, key and text input, owns keyboard focus and the Tab
/// ring, shows popups and tooltips, and emits render commands, all without needing an OS window.
///
/// The app owns it, usually as a local in `main()`. ui::run(window) and show() create the OS
/// window for it from its WindowOptions and install its host, renderer and text measurer.
/// Closing destroys the OS window and leaves the Window a valid object that can be shown again.
/// Everything window-level the app configures (title, WindowStyle, backdrop, icon, system menu,
/// taskbar state) is stored here and applied once the OS window exists, so it can be set before
/// or after show(). The first control that can take keyboard focus gets it when the window is
/// activated. The window's actual state (state(), active(), visible(), caption_insets()) is
/// written only by the host. The app asks for a change through request_state() and similar
/// functions and binds to the result.
///
/// Single-owner: set_content() and show_modal() take a `std::unique_ptr` (or a `ui::` spec) and
/// hand back a non-owning observer. Not copyable or movable. UI-thread only, except post() and
/// ref().
///
/// Example:
/// @code
/// int main()
/// {
///     ui::Window window{{.title = "Demo", .size = {480, 320}}};
///     ui::Property<int> clicks{0};
///     bool              unsaved = false;
///     window.set_content(ui::VerticalStack{
///         .spacing  = 12.0f,
///         .content  = {
///             ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })},
///             ui::Button{.text = "Click me", .on_click = [&] { ++clicks; unsaved = true; }},
///         },
///     });
///     window.on_closing([&] { return unsaved ? ui::Cancel : ui::Proceed; });
///     return ui::run(window);
/// }
/// @endcode
class Window
{
public:
    /// A window with no content and no OS window yet, Theme::light(), state() Normal. `options`
    /// is what it will be created with; its title, style, backdrop, chrome, system menu and size
    /// limits become the Window's own properties right away.
    explicit Window(WindowOptions options = {});
    /// Destroys the OS window too, if the window is still shown. Not from inside one of this
    /// window's own handlers: call close() there instead.
    ~Window();
    // Not copyable or movable: the bindable properties below are observed by address.
    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    /// Take ownership of the content element and return a non-owning observer to it, valid
    /// until the content is replaced or the window destroyed. Replacing content drops focus,
    /// entered focus scopes, open popups and any open modal, since all of them pointed into
    /// the old tree, and bumps content_generation(). The next layout() lays the new tree out.
    /// A null `content` leaves the window empty.
    ///
    /// Safe to call from a handler inside the content being replaced (a page's own "Next"
    /// button): the old tree is destroyed once the input or tick that ran the handler has
    /// returned.
    ///
    /// `where` is for the thread check (ThreadCheck.hpp); leave it defaulted.
    template <typename T>
    T* set_content(std::unique_ptr<T> content, std::source_location where = std::source_location::current())
    {
        static_assert(std::is_base_of_v<Element, T>, "Window content must be an Element");
        check_thread("ui::Window::set_content", where);
        T* observer = content.get();
        if (observer)
        {
            observer->set_parent(nullptr); // content is the root of the logical tree
            adopt_root(*observer);
        }
        detail::retire(std::move(content_));
        content_ = std::move(content);
        focus_order_.clear();
        focus_scope_stack_.clear();          // scopes pointed into the tree we just replaced
        set_focused_element(nullptr, false); // the old tree is going away: no focus events for it
        focus_after_.reset(nullptr);
        focus_before_.reset(nullptr);
        open_popups_.clear();              // last frame's popups pointed into the tree we just replaced
        detail::retire(std::move(modal_)); // same for a modal: it cannot outlive the tree it interrupted
        modal_.reset();
        modal_close_pending_ = false;
        pre_modal_focus_.reset(nullptr);
        focus_popup_ = nullptr;
        pre_popup_focus_.reset(nullptr);
        suspended_focus_.reset(nullptr);
        focus_traps_.clear();
        ++content_generation_;
        return observer;
    }
    /// The declarative overload: hand set_content() a spec (or a ui::Spec returned from a
    /// component function), e.g. `window.set_content(ui::VerticalStack{.spacing = 12, .content = {...}})`,
    /// and the window takes the tree it builds.
    ///
    /// It returns Element*, since the spec's type is erased by the time it gets here. A spec's
    /// `.control` field gives a typed observer to any node inside the tree.
    template <ui::SpecLike S>
    Element* set_content(S&& spec, std::source_location where = std::source_location::current())
    {
        return set_content(std::forward<S>(spec).build(), where);
    }

    /// The root of the content tree (non-owning), or nullptr before set_content().
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }

private:
    // ---- tooltips set on any element (Element::set_tool_tip()) ----
    // The target is only compared against a fresh hit test, never dereferenced, so a page torn
    // down under a resting pointer leaves nothing dangling.
    // The shared ToolTip element; an app sets a tip with Element::set_tool_tip().
    std::unique_ptr<Element> tool_tip_service_;
    // The element the tip is currently for (compared, never dereferenced).
    Element* tool_tip_target_ = nullptr;
    // The element clicked last; no tip for it until the pointer moves off it.
    Element* tool_tip_suppressed_ = nullptr;
    // The text of the tip being shown or counted down to.
    std::string tool_tip_text_;
    // Where the pointer rested, in window-client DIPs; the tip is placed from it.
    Point tool_tip_point_{};
    // Milliseconds the pointer has rested on the target, driven by tick().
    double tool_tip_elapsed_ = 0.0;
    // The tip opened or closed since the last tick().
    bool tool_tip_changed_ = false;
    // Re-hit-tests `position` (window-client DIPs) after a pointer move and restarts
    // or cancels the hover countdown.
    void update_tool_tip(Point position);
    // Closes the tip at once (a press, a key, the pointer leaving the window).
    void hide_tool_tip();
    // Whether the tip is currently showing.
    [[nodiscard]] bool tool_tip_open() const;

public:
    // ---- modal ----
    /// Show `dialog` modally over the content, taking ownership, and return a non-owning
    /// observer valid until close_modal() takes effect. DialogControl is the usual one.
    ///
    /// The modal is owned separately from the content and from popups opened with
    /// Element::open_popup(). An outside click doesn't dismiss it. While it is open, all pointer
    /// and key input goes to it and the Tab ring covers only the modal.
    /// Focus goes to the dialog's Element::default_focus() if that is in its ring, else its first
    /// stop, and is restored to whatever had it before once the modal closes (if that element
    /// is still in the tree). Showing a new modal replaces an open one, also from a handler of
    /// the open one; a null `dialog` closes the open one. `where` is for the thread check.
    template <typename T>
    T* show_modal(std::unique_ptr<T> dialog, std::source_location where = std::source_location::current())
    {
        static_assert(std::is_base_of_v<Element, T>, "Window modal must be an Element");
        check_thread("ui::Window::show_modal", where);
        T* observer = dialog.get();
        if (!observer)
        {
            close_modal();
            return nullptr;
        }
        observer->set_parent(nullptr);
        adopt_root(*observer);
        if (modal_)
        {
            // Replacing an open modal, possibly from its own handler: focus is inside the one that
            // goes, and what had focus before the first one stays the place to return to.
            set_focus(nullptr);
            detail::retire(std::move(modal_));
        }
        else
        {
            pre_modal_focus_.reset(focus_watch_.get());
        }
        modal_close_pending_ = false; // a close asked for earlier in this dispatch meant the old one
        modal_               = std::move(dialog);
        attach_modal(*observer);
        focus_scope_stack_.clear(); // a scope entered in the interrupted tree is not the modal's
        focus_popup_ = nullptr;     // a popup in the interrupted tree gives focus up to the modal
        pre_popup_focus_.reset(nullptr);
        rebuild_focus_order(); // now scoped to modal_
        // The modal's own pick first (Element::default_focus(), a dialog's initial focus
        // element), else the first stop in its ring.
        Element* initial = observer->default_focus();
        if (initial == nullptr || std::find(focus_order_.begin(), focus_order_.end(), initial) == focus_order_.end())
        {
            initial = focus_order_.empty() ? nullptr : focus_order_.front();
        }
        set_focus(initial);
        ++content_generation_;
        return observer;
    }
    /// The declarative overload, mirroring set_content()'s: builds `spec` (typically a
    /// `ui::Dialog`) and shows the result modally. Use the spec's `.control` for a typed observer.
    template <ui::SpecLike S>
    Element* show_modal(S&& spec, std::source_location where = std::source_location::current())
    {
        return show_modal(std::forward<S>(spec).build(), where);
    }

    /// Close the open modal and restore the focus it interrupted. No-op if no modal is open.
    /// A DialogControl closes itself, so this is for closing one from outside; its on_closed()
    /// then reports DialogResult::None. Safe to call from a callback inside the modal, such as a
    /// button's on_click. The modal is destroyed at the start of the next input dispatch or
    /// layout(), after that callback has returned.
    void close_modal();
    /// Whether a modal is open (a close_modal() still pending counts as open).
    [[nodiscard]] bool has_modal() const
    {
        return modal_ != nullptr;
    }
    /// The open modal (non-owning), or nullptr.
    [[nodiscard]] Element* modal() const
    {
        return modal_.get();
    }

    // ---- automation ----
    /// A counter bumped by set_content()/show_modal()/close_modal(), every point that can
    /// destroy part of the tree while the Window itself survives. Code that holds an Element*
    /// across calls, such as the accessibility provider, records the generation it saw and stops
    /// using the pointer once the counter has changed.
    [[nodiscard]] std::uint64_t content_generation() const
    {
        return content_generation_;
    }

    // ---- visual tree ----
    /// The compact, data-oriented shape of the tree (stable VisualIds, dirty flags) as of the
    /// most recent layout() call; empty before the first one. Built when asked for, not by every
    /// layout pass.
    [[nodiscard]] const VisualTree& visual_tree() const;

    // ---- invalidation ----
    /// How many times layout() has run measure and arrange. A layout() call that finds nothing
    /// dirty skips the pass and doesn't count. Useful for checking that invalidation works.
    [[nodiscard]] std::size_t layout_pass_count() const
    {
        return layout_pass_count_;
    }

    /// The client size, in DIPs, passed to the most recent layout(); zero before the first.
    /// WindowOptions::size is the size asked for at creation.
    [[nodiscard]] Size layout_size() const
    {
        return client_size_;
    }
    /// The theme every control in the tree measures and draws with. Theme::light() until the
    /// host reports the OS theme state, then the one resolved from theme_request(), unless the
    /// app pinned one with pin_theme().
    [[nodiscard]] const Theme& theme() const
    {
        return theme_;
    }
    /// Pins `theme`: the window keeps it through OS theme changes (dark mode, accent color,
    /// contrast themes) until set_theme() hands the choice back. Bumps
    /// theme_revision(), so the host re-applies the frame colors and dark-mode caption. Takes
    /// effect on the next layout()/render(). A theme with Theme::animations_enabled off has its
    /// animation durations zeroed here, as resolve_theme() does.
    void pin_theme(Theme theme);
    /// What the theme is resolved from; WindowOptions::theme until changed. The window
    /// re-resolves it whenever the OS theme state changes, unless pin_theme() pinned a theme.
    [[nodiscard]] const ThemeRequest& theme_request() const
    {
        return theme_request_;
    }
    /// Resolves the theme from `request` against the OS theme state last reported, and keeps
    /// following it. Unpins a theme set with pin_theme().
    void set_theme(ThemeRequest request)
    {
        theme_request_ = request;
        theme_pinned_  = false;
        set_resolved_theme();
    }
    /// Whether pin_theme() pinned the current theme.
    [[nodiscard]] bool theme_pinned() const
    {
        return theme_pinned_;
    }
    /// Bumped by every pin_theme(). The host checks it to notice a theme change the app made,
    /// such as a dark mode toggle, and then updates what the OS draws for the window: the caption
    /// colors from Theme::chrome and the light or dark system backdrop.
    [[nodiscard]] unsigned theme_revision() const
    {
        return theme_revision_;
    }

    // ---- composition ----

    // ---- native controls ----

    // ---- spell checking / context menu ----

    // ---- window host ----

    /// The state the window is in, never the one last asked for. Only the host
    /// writes it (report_state()), so it is const here; bind to it as usual:
    ///
    /// @code
    /// ui::Button{
    ///     .text = ui::bind(window.state(), [](ui::WindowState s) {
    ///         return std::string{s == ui::WindowState::Fullscreen ? "Exit full screen" : "Full screen"}; }),
    ///     .on_click = [w = &window] { w->toggle_fullscreen(); },
    /// }
    /// @endcode
    [[nodiscard]] const Property<WindowState>& state() const
    {
        return state_;
    }
    /// The state restore() would land in now: what Minimized or Fullscreen goes back to
    /// (Maximized, Normal, or Fullscreen for a window minimized from full screen), Normal from
    /// Maximized and from Normal. Written by the host with state(); bind to it for a caption button:
    ///
    /// @code
    /// ui::Button{
    ///     .text = ui::bind(window.restores_to(), [](ui::WindowState s) {
    ///         return std::string{s == ui::WindowState::Maximized ? "Restore to maximized" : "Restore"}; }),
    ///     .on_click = [w = &window] { w->restore(); },
    /// }
    /// @endcode
    [[nodiscard]] const Property<WindowState>& restores_to() const
    {
        return restores_to_;
    }
    /// Ask the host for a state. The answer arrives later through
    /// state(), and may be a refusal. Without an OS window it becomes options().initial_state,
    /// what the next one is shown in.
    void request_state(WindowState state);
    /// Back to what was there before Minimized or Fullscreen, or Maximized to Normal.
    /// Without an OS window, options().initial_state goes back to Normal.
    void restore();
    /// Fullscreen, or back out of it to the state it was entered from (the F11 idiom).
    void toggle_fullscreen();
    /// Close the window as its close button would, so on_closing() can still veto it. The one
    /// way out of a `Frame::None` window. No-op without a host.
    void close();
    /// Close the window without asking on_closing(), otherwise as close(). The way to finish a
    /// close that on_closing() held back until the user confirmed it:
    ///
    /// @code
    /// bool unsaved = false; // set by the app's edits
    /// window.on_closing([&] {
    ///     if (!unsaved)
    ///         return ui::Proceed;
    ///     window.show_modal(ui::Dialog{
    ///         .title     = "Save changes?",
    ///         .primary   = {.text = "Save"},
    ///         .secondary = {.text = "Don't save"},
    ///         .close     = {.text = "Cancel"},
    ///         .on_closed = [&](ui::DialogResult result) {
    ///             if (result == ui::DialogResult::Primary)
    ///                 save();
    ///             if (result != ui::DialogResult::Close)
    ///                 window.force_close();
    ///         },
    ///     });
    ///     return ui::Cancel;
    /// });
    /// @endcode
    void force_close();

    /// Asked before the window closes, however that was requested (close button, Alt+F4, the
    /// taskbar, close()); return ui::Cancel to keep it open. One slot; empty, or a handler that
    /// returns nothing, lets the close through.
    void on_closing(Callback<Decision(const WindowClosingArgs&)> handler)
    {
        closing_handler_ = std::move(handler);
    }
    /// Called once as the window goes away, while this Window and its tree still exist. It's
    /// the place to stop and join worker threads that post() to it: post() drops its work from
    /// here on, until the window is shown again. The Window object itself stays valid after it
    /// closed; it is the app's to destroy.
    void on_closed(Callback<void()> handler)
    {
        closed_handler_ = std::move(handler);
    }
    /// Called after the theme was resolved again: the OS switched dark mode, accent color or
    /// contrast, or set_theme() or pin_theme() asked for another. theme() is the new one, which may
    /// equal the old. For colors an app works out itself, such as a syntax highlighter's palette.
    void on_theme_changed(Callback<void(const Theme&)> handler)
    {
        theme_changed_handler_ = std::move(handler);
    }

    /// Gives keyboard focus back to where it was before Alt, F10 or an Alt+letter moved it to a
    /// menu bar (Element::take_menu_access()). A menu bar calls it on Escape and after running an
    /// item. False, and nothing changes, when focus didn't get there that way.
    bool end_menu_access();

private:
    // Called by whatever hosts the window (the backend, the headless host, a test), through
    // ui::testing::seams(window).
    /// Host side of on_closing(): the host calls this when the window is asked to close and
    /// closes only if it returns true (true without a handler).
    [[nodiscard]] bool handle_close_request();
    /// Host side of on_closed(), called when the OS window is destroyed: runs the handler once
    /// and from then on makes post() drop its work.
    void handle_closed();

public:
    /// The placement to persist (WindowPlacement): where the window is, or where it was when it
    /// closed. Nullopt before the window was first shown. Read it after `ui::run()` returns and
    /// hand it back through `ui::WindowOptions::placement` on the next run.
    [[nodiscard]] std::optional<WindowPlacement> placement() const;

    // ---- position, size and visibility ----
    // DIPs at the window's DPI, screen coordinates. Each is a no-op without a host, except the
    // position, the sizes and the size limits, which a host installed later still picks up, as it
    // does the title and style.

    /// Move the outer top-left. Without an OS window it becomes options().position, where
    /// the next one is created. A non-finite position is ignored.
    void move(Point position);
    /// Resize the client area, the room the content gets; the frame comes on top. A maximized,
    /// minimized or full-screen window stays as it is and takes the size when restored. Without
    /// an OS window it becomes options().size, what the next one is created with. A negative,
    /// infinite or NaN size is ignored.
    void resize(Size size);
    /// The window's position and size, as one rect; nullopt without a host.
    [[nodiscard]] std::optional<Rect> bounds() const;
    /// The work area of the display the window is mostly on; nullopt
    /// without a host.
    [[nodiscard]] std::optional<Rect> work_area() const;
    /// Shows a hidden window and brings it to the front with focus. visible() follows once the
    /// host reports it.
    void show();
    /// Shows a hidden window without activating it: it stays behind the active window, and focus
    /// stays where it is.
    void show_inactive();
    /// What the OS window is created with. `title`, `style`, `backdrop`, `chrome`, `system_menu`,
    /// `min_size` and `max_size` were read by the constructor, so writing them here does nothing:
    /// change those with set_title(), set_style(), set_backdrop(), set_chrome(), set_system_menu()
    /// and set_size_limits(). The other fields are read when the OS window is created, so they
    /// are writable until then; a change after the window was shown only takes effect the next
    /// time it is created.
    [[nodiscard]] WindowOptions& options()
    {
        return options_;
    }
    /// The same, read-only.
    [[nodiscard]] const WindowOptions& options() const
    {
        return options_;
    }
    /// The operating system's handle of this window (an `HWND` on Windows), or null while it has
    /// none: before ui::run() or show() created it, and after it closed. For the OS calls
    /// libwinui does not wrap.
    [[nodiscard]] NativeHandle native_handle() const;

private:
    // Called by whatever hosts the window (the backend, the headless host, a test), through
    // ui::testing::seams(window).
    /// Host side: an OS window was created for this Window (again), so post() accepts work.
    void handle_opened();

public:
    /// Takes the window off the screen, the taskbar and Alt+Tab, and keeps
    /// it, its tree and its state for the next show().
    void hide();
    /// Whether the window is shown. Host-written, like state(); false until
    /// a host reports the window shown.
    [[nodiscard]] const Property<bool>& visible() const
    {
        return visible_;
    }
    /// Whether the window is open: true from the moment ui::run() or show() created it until it
    /// closes, hidden or not. Host-written, like visible(), so it can be bound, a menu item's
    /// check mark following whether a tool window is open.
    [[nodiscard]] const Property<bool>& is_open() const
    {
        return open_;
    }
    /// Brings the window to the front, with focus.
    void activate();
    /// The minimum and maximum client sizes the user can drag the window to. Zero on
    /// an axis leaves it unconstrained; not applied while full screen.
    void set_size_limits(Size min, Size max);
    /// The minimum client size last given to set_size_limits(), in DIPs (zero: none).
    [[nodiscard]] const Size& min_size() const
    {
        return min_size_;
    }
    /// The maximum client size last given to set_size_limits(), in DIPs (zero: none).
    [[nodiscard]] const Size& max_size() const
    {
        return max_size_;
    }

    /// Whether this is the foreground window, which a TitleBar dims itself by. Host-written;
    /// false until a host reports the window activated.
    [[nodiscard]] const Property<bool>& active() const
    {
        return active_;
    }
    /// Whether a drag from another app (files from Explorer, text from a browser) is over this
    /// window: true from when it enters until it leaves or drops, whichever drop target inside it
    /// the pointer is over. For drop zones that show only while something is dragged in; what is
    /// dragged is external_drag_data().
    ///
    /// Example:
    /// @code
    /// ui::Border{
    ///     .visible = ui::bind(window.external_drag()),
    ///     .content = ui::Text{.text = "Drop PDF files here"},
    /// };
    /// @endcode
    [[nodiscard]] const Property<bool>& external_drag() const
    {
        return external_drag_;
    }
    /// Device pixels per DIP on the monitor the window is on: 1 at 100 %, 1.5 at 150 %, 2 at 200 %.
    /// Host-written, and it changes when the window moves to a monitor with another scale; 1 until
    /// a host reports one. For an image decoded at the size it is shown at, in pixels.
    ///
    /// Example:
    /// @code
    /// const float pixels = 160.0f * window.scale().get(); // a 160 DIP thumbnail, sharp at any scale
    /// @endcode
    [[nodiscard]] const Property<float>& scale() const
    {
        return scale_;
    }
    /// What the drag from another app over this window carries, while external_drag() is true;
    /// nullptr otherwise. Valid until the drag leaves or drops.
    [[nodiscard]] const DataPackage* external_drag_data() const;
    /// The caption space an extended title bar must leave alone (CaptionInsets). Host-written;
    /// all zero unless WindowStyle::extend_content_into_title_bar is on.
    [[nodiscard]] const Property<CaptionInsets>& caption_insets() const
    {
        return caption_insets_;
    }

    // ---- host-side writers ----

private:
    // Called by whatever hosts the window (the backend, the headless host, a test), through
    // ui::testing::seams(window).
    /// Called by the host, never the app: the window's state changed, whoever asked. The
    /// equality gate means a repeated report disturbs nothing. restores_to() is worked out from
    /// the state before: what was left for Minimized, Maximized or Normal for Fullscreen.
    void report_state(WindowState state);
    /// The same, from a host that knows where restore() lands (the OS's restore-to-maximized
    /// flag, a placement applied at startup) and says so.
    void report_state(WindowState state, WindowState restores_to)
    {
        restores_to_.set(restores_to); // first, so an observer of state() reads the new answer
        state_.set(state);
    }
    /// Host-only: the window became (or stopped being) the foreground window; feeds active().
    void report_active(bool active)
    {
        active_.set(active);
    }
    /// Host-only: device pixels per DIP on the window's monitor; feeds scale().
    void report_scale(float scale)
    {
        scale_.set(scale > 0.0f ? scale : 1.0f);
    }
    /// Host-only: the space the caption buttons take; feeds caption_insets().
    void report_caption_insets(const CaptionInsets& insets)
    {
        caption_insets_.set(insets);
    }
    /// Host-only: the window was shown or hidden; feeds visible().
    void report_visible(bool visible)
    {
        visible_.set(visible);
    }
    /// Host-only: the small icon the OS shows for this window, as pixels: the app's own, or the
    /// platform's default while it has none. A caption the theme draws shows it.
    void report_caption_icon(Bitmap icon);
    /// Host-only: whether the desktop behind the window is blurred for a frame that asked for it
    /// (wants_blur_behind()). While it is, the frame paints glass instead of its opaque art.
    void report_blur_behind(bool active);
    /// Host-only: the OS theme state, at creation and after every change. Re-resolves the
    /// theme unless pin_theme() pinned one.
    void report_os_theme(const OsThemeSnapshot& os)
    {
        os_theme_ = os;
        if (!theme_pinned_)
        {
            set_resolved_theme();
        }
    }
    void set_resolved_theme()
    {
        theme_ = resolve_theme(theme_request_, os_theme_);
        ++theme_revision_;
        theme_changed();
        if (theme_changed_handler_)
        {
            theme_changed_handler_(theme_);
        }
    }
    // Marks every element the theme reaches dirty, so the next layout re-measures and redraws it.
    void theme_changed();
    // show() and show_inactive(): creates the OS window when there is none yet.
    void show_window(bool activate);
    // What the host asks the window, through ui::testing::seams(window): see WindowSeams.
    [[nodiscard]] WindowStyle                   effective_style() const;
    [[nodiscard]] float                         drawn_frame_border() const;
    [[nodiscard]] Element*                      drawn_title_bar() const;
    [[nodiscard]] bool                          wants_blur_behind() const;
    [[nodiscard]] std::span<const std::uint8_t> frame_corner_cut() const;
    [[nodiscard]] const Bitmap&                 caption_icon() const
    {
        return caption_icon_;
    }
    [[nodiscard]] bool blur_behind() const
    {
        return blur_behind_;
    }

public:
    // ---- frame ----
    /// The caption and taskbar text. Seeded from `ui::WindowOptions::title` at creation.
    [[nodiscard]] const std::string& title() const
    {
        return title_;
    }
    /// Stores the title and pushes it to the host if one is installed.
    void set_title(std::string title);
    /// The frame the window is drawn with (WindowStyle). Seeded from
    /// `ui::WindowOptions::style` at creation.
    [[nodiscard]] const WindowStyle& style() const
    {
        return style_;
    }
    /// Applied live by the host; while full screen it is only
    /// remembered for the way back. Edit a copy of style() and hand it back.
    void set_style(const WindowStyle& style);
    /// The system backdrop (ui/WindowStyle.hpp's Backdrop). Seeded from the host's options at
    /// creation (ui::WindowOptions::backdrop) and applied live by the host afterwards.
    [[nodiscard]] Backdrop backdrop() const
    {
        return backdrop_;
    }
    /// A request the theme or the system can decline (see Backdrop); pushed to the host at once.
    void set_backdrop(Backdrop backdrop);
    /// This window's own frame colors and corners (ChromeOverrides), merged over the theme's
    /// ChromeHints by the host. Seeded from `ui::WindowOptions::chrome`.
    [[nodiscard]] const ChromeOverrides& chrome() const
    {
        return chrome_;
    }
    /// Replace this window's frame overrides; applied live by the host.
    void set_chrome(const ChromeOverrides& chrome);
    /// Classify a window-client point for a host that lets content stand in for the caption
    /// (a `Frame::Borderless`, `Frame::None` or `Frame::Themed` window, or content extended into
    /// the title bar): Caption where a press should drag the window, a caption button or the
    /// system menu over a frame the theme draws, Client everywhere else. Asks the content tree
    /// through Element::hit_region(); an open modal or popup is always Client.
    [[nodiscard]] HitRegion hit_region_at(Point client_dips) const;

    /// The pointer cursor at a window-client point. Starting at the innermost element under the
    /// point (in the topmost open popup, the modal, or the tree) and walking up its parents, the
    /// first element that asks for a cursor decides, through set_cursor() and then its own
    /// cursor_at(). The arrow is the default. While the left button is held, the cursor found at
    /// the press stays, since the pressed element has the pointer captured. During a drag, the
    /// arrow shows over a target that accepts it and the no-entry sign elsewhere. The host asks
    /// whenever the OS needs a cursor and after each pointer event.
    [[nodiscard]] CursorShape cursor_at(Point client_dips) const;

    // ---- native extension points ----
    /// The icon set with set_icon() or seeded from `ui::WindowOptions::icon`, or nullopt, which
    /// shows the executable's first icon resource (or the system's default without one).
    [[nodiscard]] const std::optional<WindowIcon>& icon() const
    {
        return icon_;
    }
    /// The caption and taskbar icon; nullopt goes back to the default one.
    void set_icon(std::optional<WindowIcon> icon);

    /// The app's own system-menu entries, in the order set. A toggle's or radio item's
    /// is_checked() is its current state.
    [[nodiscard]] const std::vector<MenuItem>& system_menu() const
    {
        return system_menu_;
    }
    /// Replaces the app's system-menu entries. Call again to change a check mark.
    ///
    /// The entries are the same ui::MenuItem a menu takes, at the top level only: the OS menu
    /// shows the text, the check mark of a toggle or radio item, the disabled state and the
    /// separators, and drops icons, shortcuts and submenus. A pick invokes the item as a menu
    /// click would (a toggle flips, a radio item checks), and a bound field updates the entry live.
    void set_system_menu(std::vector<MenuItem> items);

private:
    // Called by whatever hosts the window (the backend, the headless host, a test), through
    // ui::testing::seams(window).
    /// Called by the host when entry `index` was picked: invokes that item as a menu click would.
    /// An index out of range is ignored.
    void handle_system_menu_item(std::size_t index);

public:
    /// The taskbar button's progress bar. App-written, unlike state(): nothing can refuse it,
    /// so it is an ordinary Property. Write it, or drive it from a binding of your own.
    [[nodiscard]] Property<TaskbarProgress>& taskbar_progress()
    {
        return taskbar_progress_;
    }
    /// The badge over the taskbar button, or nullopt for none (set_taskbar_overlay()).
    [[nodiscard]] const std::optional<WindowIcon>& taskbar_overlay() const
    {
        return taskbar_overlay_;
    }
    /// What a screen reader announces for taskbar_overlay().
    [[nodiscard]] const std::string& taskbar_overlay_description() const
    {
        return taskbar_overlay_description_;
    }
    /// A badge over the taskbar button (an unread count, a status dot); nullopt removes it.
    /// `description` is the badge's accessible text.
    void set_taskbar_overlay(std::optional<WindowIcon> icon, std::string description = {});
    /// Flash the taskbar button until the window is brought to the front or cancel_attention()
    /// is called. On a window already in front that is at once, so there is nothing to see; ask
    /// when active() is false, e.g. when work finishes in the background.
    void request_attention();
    /// Stop the flashing request_attention() started.
    void cancel_attention();

    /// The taskbar thumbnail's buttons as last set (ThumbnailButton).
    [[nodiscard]] const std::vector<ThumbnailButton>& thumbnail_buttons() const
    {
        return thumbnail_buttons_;
    }
    /// Up to seven buttons under the taskbar thumbnail. The OS fixes the set on first use; a bound
    /// field, or a command's state, updates its button live, and a call with the same number of
    /// buttons replaces them (ThumbnailButton).
    void set_thumbnail_buttons(std::vector<ThumbnailButton> buttons);

private:
    // Called by whatever hosts the window (the backend, the headless host, a test), through
    // ui::testing::seams(window).
    /// Called by the host when button `index` was clicked: runs that button's `on_click`, then
    /// its command. An index out of range or a disabled button is ignored.
    void handle_thumbnail_button(std::size_t index);

public:
    /// Measures and arranges the content across a `client` sized area (or centered at its
    /// desired size, see set_stretch_content()), then refreshes the focus ring, since the set of
    /// controls may have changed.
    ///
    /// Does nothing while state() is Minimized. A minimized window has a 0x0 client area, and
    /// laying out at that size would only collapse the tree until it is restored.
    void layout(Size client, const TextMeasurer& measurer);

    /// Let layout() measure large batches of independent siblings on worker threads (see
    /// ParallelLayout). On by default; `{.enabled = false}` keeps every measure on the UI thread.
    /// The layout is the same, bit for bit, either way.
    void set_parallel_layout(ParallelLayout options)
    {
        parallel_layout_ = options;
    }
    /// The parallel layout options set_parallel_layout() last set.
    [[nodiscard]] const ParallelLayout& parallel_layout() const
    {
        return parallel_layout_;
    }

    /// Arrange the content at the whole client size, where its own alignment and max size place
    /// it. On by default. Off centers the content at its desired size instead.
    void set_stretch_content(bool stretch);
    /// Whether the content is arranged at the whole client size (see set_stretch_content()).
    [[nodiscard]] bool stretch_content() const
    {
        return stretch_content_;
    }

    /// What the host fills the client area with before drawing the content, over the theme's
    /// `colors.background`; nullopt goes back to that. Not drawn while a system backdrop is on,
    /// which the host leaves transparent. A tray flyout uses it for the shell's flyout surface.
    void set_background(std::optional<Color> background);
    /// The effective client background: set_background()'s color, else the theme's
    /// `colors.background`.
    [[nodiscard]] Color background() const
    {
        return background_.value_or(theme_.colors().background);
    }

    /// Emit render commands for the whole window, bottom to top: the content tree, connected
    /// animations, the popup layer (tooltips included), then an open modal. Does not paint
    /// background(); the host fills the client area first. Call after layout().
    void render(Renderer& renderer) const;

    /// render() for a frame that only repaints `clip` (window-client DIPs), one of the rects
    /// take_damage() returned: an element that drew nowhere near it last frame, and has not
    /// changed since, is skipped. Clipped to `clip` through the renderer; the host fills `clip`
    /// first.
    void render(Renderer& renderer, const Rect& clip) const;

    /// What has to be painted again since the last call: the areas, in window-client DIPs, where
    /// the next frame differs from the last one (another color, text or place, gone or new).
    /// Elements don't have to mark themselves for this. Empty when nothing visible changed, and
    /// the whole client area for the first frame and after a change of size or background. Call
    /// after layout(), once per frame, right before rendering those areas with
    /// render(renderer, clip).
    [[nodiscard]] std::vector<Rect> take_damage();

    // ---- connected animations ----
    /// The window's connected animations
    /// (ui/ConnectedAnimation.hpp), drawn above the tree and below the popups.
    [[nodiscard]] ConnectedAnimationService& connected_animation_service()
    {
        return connected_animations_;
    }

    // ---- time ----
    /// Advances elapsed time by `delta`, driving every control's tick_override(), such as a
    /// tooltip's hover delay. The host calls this from its timer, so the window itself never
    /// reads a real clock. Returns whether the tree became dirty, so the caller knows whether to
    /// repaint.
    bool tick(Duration delta);

    /// Whether the last tick() found the window busy with time: something changed in it, an
    /// element called request_tick(), work was posted, a drag or a connected animation runs, or
    /// an on_tick() handler is set. A host that has seen no busy tick for a while stops ticking,
    /// so an idle window doesn't keep waking the process. A pending after() or every() timer, a
    /// tip waiting out its delay and an Element::request_tick_in() don't make it busy, and
    /// next_wake() says when to tick for them. Neither does a change made only by such a timed
    /// step (a caret blink, an image's next frame), as long as nothing else was marked dirty
    /// since the last tick.
    [[nodiscard]] bool wants_ticks() const
    {
        return wants_ticks_;
    }

    /// How long until the window has to tick again, in tick() time, or 0 when that is overdue.
    /// It's the earliest of a pending after() or every() timer, the delay of a tip set with
    /// Element::set_tool_tip(), and a wait an element asked for with Element::request_tick_in()
    /// in the last tick() or frame. Nullopt when none of them is pending. A host ticks the window
    /// once at that time and not while it waits.
    [[nodiscard]] std::optional<Duration> next_wake() const;

    /// The app's own per-frame hook, so polling something (a worker's progress, a file watcher)
    /// doesn't take a custom Element just to get a tick_override(). Called at the start of every
    /// tick(), before the controls' own tick_override(), with the same `delta`. There is no fixed
    /// rate. The window ticks after every input or system event it handles and at least every
    /// 100 ms while idle. One slot, like on_key_preview(); an empty handler removes it.
    void on_tick(Callback<void(Duration)> handler)
    {
        tick_handler_ = std::move(handler);
    }

    /// Runs `fn` once on the UI thread, `delay` from now, on the window's clock (tick()). The
    /// returned Subscription owns the timer, and the timer ends with it: destroying or reset()ting
    /// it cancels the call, so keep it for as long as the call should still happen. A
    /// Subscription left unstored is destroyed at once and the call never runs; for a call that
    /// needs no canceling, use post_after(). It runs on the first tick at or after
    /// its time, which while the window is otherwise idle comes at least every 100 ms. Time keeps
    /// running while the window is hidden or minimized; a closed window does not tick.
    ///
    /// Example:
    /// @code
    /// using namespace std::chrono_literals;
    /// ui::Property<bool> saved{false};
    /// ui::Subscription   hide_saved;
    /// ui::Button{.text = "Save", .on_click = [&] {
    ///     saved      = true;
    ///     hide_saved = window.after(2s, [&] { saved = false; }); // a second click starts over
    /// }};
    /// @endcode
    [[nodiscard]] Subscription after(Duration delay, Callback<void()> fn);
    /// Runs `fn` once on the UI thread, `delay` from now, as after() does, with nothing to keep:
    /// the call cannot be canceled. It is dropped without running when the window closes first,
    /// and on a window that is closed already. Called on the UI thread, like after(); a worker
    /// thread hands work over with post().
    ///
    /// Example:
    /// @code
    /// using namespace std::chrono_literals;
    /// const std::string         link = "https://example.com/notes/42";
    /// ui::Property<std::string> status;
    /// ui::Button{.text = "Copy", .on_click = [&] {
    ///     ui::clipboard().set_text(link);
    ///     status = "Copied";
    ///     window.post_after(2s, [&] { status = ""; });
    /// }};
    /// @endcode
    void post_after(Duration delay, Callback<void()> fn);
    /// Runs `fn` on the UI thread every `interval`, first one `interval` from now, until the
    /// returned Subscription is destroyed or reset(). A late tick runs it once, not once for every
    /// interval it missed, and the next run is an interval after that. An interval of zero runs
    /// it on every tick.
    ///
    /// Example:
    /// @code
    /// using namespace std::chrono_literals;
    /// ui::Property<int> seconds{0};
    /// ui::Subscription  clock = window.every(1s, [&] { ++seconds; });
    /// @endcode
    [[nodiscard]] Subscription every(Duration interval, Callback<void()> fn);

    /// Makes `command`'s `.shortcut` fire while this window has focus, whether or not a button or
    /// menu item offers the command, until the returned Subscription is destroyed or reset(). A
    /// key the focused control or a shortcut of an area around it takes never reaches it. The
    /// command runs only while it is enabled, with an empty parameter.
    ///
    /// Example:
    /// @code
    /// ui::Command open{.text = "Open", .shortcut = ui::Ctrl + ui::Key::O, .execute = [&] { open_file(); }};
    /// ui::Subscription keep_open = window.add_command(open);
    /// @endcode
    [[nodiscard]] Subscription add_command(const Command& command);

    /// Run `work` on the UI thread at the start of the next tick(), ahead of the on_tick()
    /// hook. Unlike every other member, this one may be called from any thread. It is how a
    /// worker hands its result back without the app polling for it:
    ///
    ///     std::thread([home = window.ref(), path] {
    ///         auto image = decode(path);
    ///         home.post([image = std::move(image)]() mutable { show(std::move(image)); });
    ///     }).detach();
    ///
    /// The thread holds a WindowRef, so work it posts after the window is gone is dropped and
    /// never reaches a destroyed object.
    ///
    /// The host is woken, so the work doesn't wait for the idle timer.
    ///
    /// Work posted after the window closed, or still queued then, is dropped without running.
    /// Dropped work is destroyed outside the window's lock: by the posting thread when post()
    /// refuses it, by the UI thread as the window closes when it was still queued, so its
    /// destructor may post again.
    ///
    /// As a member, this needs the Window to exist: called on a destroyed one it is undefined
    /// behavior. A worker that can outlive the Window posts through ref() instead
    /// (WindowRef::post()), which drops the work once the Window is gone, as
    /// `co_await ui::on_ui_thread(window.ref())` does.
    ///
    /// The UI thread is the one that constructed the Window; a build with thread checks
    /// (ThreadCheck.hpp) reports any other member called from another thread.
    void post(Callback<void()> work);

    /// A weak handle to this Window for a worker that may outlive it: its WindowRef::post() is
    /// post() while this Window exists and drops the work once it is destroyed, also when another
    /// Window was built at the same address since. Callable from any thread while the Window exists.
    [[nodiscard]] WindowRef ref() const noexcept
    {
        return WindowRef{this, serial_};
    }

private:
    // Called by whatever hosts the window (the backend, the headless host, a test), through
    // ui::testing::seams(window).
    /// Route a pointer event (position in window-client DIPs): to the topmost open popup it
    /// hits (a press outside light-dismisses it), else the open modal, else down the content
    /// tree. A press moves focus to the focusable element under it and leaves any entered
    /// focus scope it falls outside of. Also drives tooltips and a drag in progress.
    void handle_pointer(const PointerEvent& e);
    /// Route a key event, stopping at the first taker: on_key_preview(), Tab/Shift+Tab (which
    /// the window consumes to move focus around the ring), Enter into a focus scope, Escape (out
    /// of an entered scope, else to the modal or the open popups), the focused element and then
    /// each parent, the focused element's and its ancestors' shortcuts, the window-wide
    /// shortcuts (shortcut keys only), and last an arrow key's focus move. Escape cancels a
    /// drag in progress. With a modal open, only its tree is reachable. True when anything used
    /// the key; false when nothing did, which a host uses to hand an unused Alt key combination
    /// back to the system.
    bool handle_key(const KeyEvent& e);

public:
    /// A window-wide shortcut: `handler` sees every key before Tab handling, focus scopes and
    /// the focused element do, and returns ui::Handled to consume it. It watches keys pass by, so
    /// a handler that returns nothing lets every key go on. One slot, for the app, such as an F11
    /// full-screen toggle:
    ///
    /// @code
    /// window.on_key_preview([w = &window](const ui::KeyEvent& e) {
    ///     if (e.key != ui::Key::F11 || e.action != ui::KeyAction::Down) return ui::NotHandled;
    ///     w->toggle_fullscreen();
    ///     return ui::Handled;
    /// });
    /// @endcode
    void on_key_preview(Callback<Handling(const KeyEvent&)> handler)
    {
        key_preview_ = std::move(handler);
    }

private:
    // Called by whatever hosts the window (the backend, the headless host, a test), through
    // ui::testing::seams(window).
    /// Typed text, UTF-8, forwarded to the element that has focus. The window itself consumes
    /// none of it.
    void handle_text(std::string_view text);
    /// A context-menu request: a right-click at `position` (window-client DIPs), or nullopt for
    /// Shift+F10 or the Menu key.
    /// A positioned request first focuses the focusable element under the pointer (nothing
    /// there: no menu), then goes to the focused element's handle_context_menu(). Returns
    /// whether a menu was shown; always false without a context-menu host.
    bool handle_context_menu(std::optional<Point> position);
    /// An accessibility client's Invoke on `element`: runs the element's InvokeProvider as a
    /// dispatch like any input, so a dialog it closes is torn down only once the call has
    /// returned. Returns whether the element handled the invoke.
    bool handle_automation_invoke(Element& element);
    /// The context-menu request for a NativeControl's own child window. The window doesn't
    /// hit-test inside native controls, so the host resolves the owning element and passes it
    /// in. Skips everything handle_context_menu() does around the call. Focus stays inside the
    /// native control, which took keyboard focus on the click, and the spell checker is not
    /// synced, since a native control checks spelling through the OS on its own.
    /// `position` is the click point in window-client DIPs, or nullopt for Shift+F10 or the Menu
    /// key. Returns whether a menu was shown.
    bool handle_native_context_menu(Element& native, std::optional<Point> position);

    /// A hosted native control edited its own text, resolved to its element by the host. The
    /// keystrokes went straight to the native control, so this is the only way the window
    /// learns of the edit, and it gives the control the chance to spell-check itself again.
    void handle_native_content_changed(NativeControl& native);

    /// A hosted native control sent its parent a notification about itself, resolved to its
    /// element by the host. Hands it to NativeControl::handle_native_notification() and, when
    /// that handled it, syncs the native controls again. Color requests made while the control
    /// paints don't come through here, since the host asks the element directly. Returns whether
    /// the control handled the notification.
    bool handle_native_notification(NativeControl& native, NativeNotification& notification);

public:
    // ---- focus ----
    /// Focus the first stop in the tab ring. The window does this on its own when it is first
    /// activated, so call it only to send focus back to the start later on.
    void focus_content();
    void focus_next();     ///< Advance focus to the next stop in the ring (wraps), as Tab does.
    void focus_previous(); ///< Retreat focus to the previous stop (wraps), as Shift+Tab does.
    void blur();           ///< Clear focus: no element has it afterwards.
    /// Move keyboard focus to `element`, or, when it takes no focus itself (a panel, a view's
    /// host), to the first stop of the tab ring inside it. False, and focus stays where it was,
    /// when neither is a stop in this window right now: hidden, disabled or in another window.
    /// The same as Element::focus().
    bool focus(Element& element);
    /// focus() that remembers what had focus, for a view that takes the keyboard while it is open
    /// (a page view over a list): pop_focus() gives focus back. False, and nothing is remembered,
    /// when focus() could not move focus.
    ///
    /// Example:
    /// @code
    /// void show_page(ui::Element& page_view)
    /// {
    ///     window.push_focus(page_view); // opening the view
    ///     // ...
    ///     window.pop_focus();           // closing it: back to the thumbnail that had focus
    /// }
    /// @endcode
    bool push_focus(Element& element);
    /// Give focus back to what had it at the last push_focus() that is not yet popped. False when
    /// nothing was pushed, or what had focus is gone or no longer a stop; focus then stays put.
    bool pop_focus();
    /// The host window lost keyboard focus: clear focus, remembering which stop had it.
    void suspend_focus();
    /// The host window got keyboard focus back: refocus the stop suspend_focus() remembered if it
    /// is still in the ring, otherwise the first stop, as focus_content() does.
    void restore_focus();
    /// The element with keyboard focus (non-owning), or nullptr.
    [[nodiscard]] Element* focused() const
    {
        return focus_watch_.get();
    }
    /// Where text typed now would go: the focused element's Element::caret_bounds(), in client
    /// DIPs, or nullopt. The backend keeps an input method's candidate window and the emoji panel
    /// there.
    [[nodiscard]] std::optional<Rect> caret_bounds() const
    {
        return focused() != nullptr ? focused()->caret_bounds() : std::nullopt;
    }
    /// Focuses `e` if it is in the tab-focus ring and returns true. Otherwise does nothing and
    /// returns false. Unlike focus(), it doesn't look for a stop inside `e`. Accessibility
    /// clients set focus through it.
    bool focus_element(Element* e);

    // ---- focus scopes (Element::focus_scope()) ----
    /// Step *into* `scope`, making its own inner ring the tab ring until the matching
    /// exit: Tab then cycles only within it and Escape steps back out onto `scope` itself.
    /// Refuses (returns false, changing nothing) unless `scope` is a focus scope sitting in
    /// the current ring with a non-empty inner ring of its own, which lets the key
    /// that would have entered fall through to the element's ordinary handle_key() instead.
    /// Scopes nest: entering one inside an entered scope pushes onto the stack.
    bool enter_focus_scope(Element* scope);
    /// Step back out of the innermost entered scope, restoring focus to the scope element.
    /// Returns false when no scope is entered.
    bool exit_focus_scope();
    /// The innermost entered scope, or nullptr when the tab ring is the whole tree.
    [[nodiscard]] Element* entered_focus_scope() const
    {
        return focus_scope_stack_.empty() ? nullptr : focus_scope_stack_.back()->get();
    }

private:
    // ---- platform seams ----
    // Installed by whatever hosts the window: the backend when it creates the OS window, the
    // headless host, or a test handing in a test double. Reached through
    // ui::testing::seams(window) (winui/testing/WindowSeams.hpp).
    friend class testing::WindowSeams;

    /// Optional, non-owning: the backend installs its compositor here. Left null, no composition
    /// calls are made, and controls draw their current state through the ordinary repaint.
    void set_compositor(detail::Compositor* compositor)
    {
        compositor_ = compositor;
    }

    /// Optional and non-owning, like set_compositor(): the backend's native-control host
    /// installs itself here. Left null, no native-control calls are made, and a NativeControl
    /// in the tree never creates its real window.
    void set_native_host(NativeHost* host)
    {
        native_host_ = host;
    }

    /// Optional and non-owning, like set_native_host(): the backend installs its spell checker
    /// here. Left null, a TextBox shows no squiggles.
    /// See Element::sync_spell_checker() for how it reaches a control.
    void set_spell_checker(SpellChecker* checker)
    {
        spell_checker_ = checker;
    }
    /// Optional: what set_spell_checker() would be given, asked the first time a control needs a
    /// checker and none is set, so the backend makes one only for a window that has use for it.
    void set_spell_checker_source(Callback<SpellChecker*()> source)
    {
        spell_checker_source_ = std::move(source);
    }

    /// Optional and non-owning: the host that pops up real context menus. Left null,
    /// right-click and Shift+F10 do nothing; see handle_context_menu() and
    /// Element::handle_context_menu().
    void set_context_menu_host(ContextMenuHost* host)
    {
        context_menu_host_ = host;
    }

    /// Optional and non-owning, like set_compositor(): the backend installs the host of the OS
    /// window here. Left null, every request below is remembered but goes
    /// nowhere, and state() stays Normal. Pass nullptr before the host is destroyed.
    ///
    /// The host reads everything configured so far (title, style, chrome, icon, system menu,
    /// taskbar state) when it is ready for it, so the order of these setters and of installing
    /// the host doesn't matter.
    void set_window_host(WindowHost* host)
    {
        const std::lock_guard lock{posted_mutex_}; // post() reads it from other threads
        window_host_ = host;
    }

    void rebuild_focus_order();
    // Rebuilds the ring when it is empty or an element died since it was built: input can arrive
    // between a handler destroying part of the tree and the next layout().
    void ensure_focus_order();
    // Tab/Shift+Tab inside an entered item-navigating scope (Element::scope_step()). True when the
    // step was the scope's to make, so the ordinary ring walk stays out of it.
    bool step_inside_scope(int delta);
    // Moves focus to the nearest element in the direction of `key`, within the current ring. False
    // when there is nothing that way, so the key can still reach the focused element.
    bool focus_direction(Key key);
    // A choice group's members (Element::selection_group()) share one Tab stop: true when Tab or
    // an arrow from outside should pass `candidate` by, because it is not its group's stop or it
    // is in the same group as `from`.
    [[nodiscard]] bool passes_over(const Element* candidate, const Element* from) const;
    // An arrow key on a member of a choice group: moves to the previous or next member and, unless
    // Ctrl is held, selects it. False when the focused element is in no group.
    bool step_in_choice_group(const KeyEvent& e);
    // Pops every entered scope the given pointer position falls outside of, so a click can
    // always move focus out of a scope Tab is trapped inside.
    void leave_scopes_outside(Point position);
    void set_focus(Element* e);
    // Flips to keyboard on any key, to pointer on a press. set_focus() stamps new focus with it, so a
    // click-focused control shows no ring until a key is pressed.
    void note_keyboard_activity();
    void note_pointer_activity();
    void resolve_pending_modal_close();
    // Lets a DialogControl modal close itself, through close_modal().
    void attach_modal(Element& modal);

    // RAII depth counter around handle_pointer/handle_key/handle_text, so resolve_pending_modal_close()
    // only runs once the outermost call returned, never under a control's own callback.
    struct DispatchGuard;
    void          sync_composition();                              // no-op if compositor_ is null
    void          sync_layers(std::vector<Rect>& damage);          // after take_damage()'s measuring pass
    void          sync_covered_visuals(std::vector<Rect>& damage); // after it too: Element visuals under other drawing
    void          sync_screen();                                   // after it too: Element::on_screen_changed()
    void          sync_native();                                   // no-op if native_host_ is null
    SpellChecker* live_spell_checker();                            // spell_checker_, made by its source on first ask
    void          sync_popups(const TextMeasurer& measurer);       // the overlay/popup layer

    /// A currently-open popup, resolved fresh by sync_popups() every layout() pass. Both
    /// pointers are non-owning: `owner` is a node in content_'s tree, and `content` is
    /// the content of `owner`'s PopupRequest, which the owner keeps (a ComboBox owns its list).
    /// `bounds` is `content`'s own bounds() right after sync_popups() arranged it, cached so
    /// render() and handle_pointer() don't recompute it.
    struct OpenPopup
    {
        Element* owner;
        Element* content;
        Rect     bounds;
        /// Index into open_popups_ of the popup this one's owner floats in (a cascading menu's
        /// submenu, a ComboBox in a Flyout), or -1 for an owner in the tree itself.
        int parent = -1;
        /// Watches on `owner` and `content`, so input that arrives after a handler destroyed
        /// either one, before the next layout() rebuilds this list, finds the entry dead
        /// (drop_dead_popups()) instead of dereferencing it. Boxed: a watch does not move.
        std::unique_ptr<ElementWatch> owner_watch;
        std::unique_ptr<ElementWatch> content_watch;
        /// Both the owner and its popup content are still alive.
        [[nodiscard]] bool alive() const
        {
            return owner_watch && owner_watch->get() != nullptr && content_watch && content_watch->get() != nullptr;
        }
    };
    std::vector<OpenPopup>  open_popups_;
    [[nodiscard]] OpenPopup make_open_popup(Element* owner, Element* content, int parent) const;
    // The window rect `owner`'s popup hangs from: the request's anchor (the owner by default), or
    // the rect inside it.
    [[nodiscard]] static Rect anchor_rect(const Element& owner, const PopupRequest& request);
    // Whether the popup is open and dismissed by a press outside it or Escape.
    [[nodiscard]] static bool light_dismiss(const OpenPopup& popup);
    /// Forgets the open popups whose owner or content was destroyed since the last layout().
    void drop_dead_popups();
    /// Every popup owner alive right now: the tree's (content_ and modal_), then level by level
    /// the owners inside each open popup's content. `parents`, when given, receives for each
    /// owner the owner whose popup it was found in (null for a tree owner).
    void collect_live_popup_owners(std::vector<Element*>& owners, std::vector<Element*>* parents = nullptr);

    // ---- modal ----
    // The frame the theme draws under Frame::Themed (src/ui/WindowFrame.hpp), around content_;
    // null while the OS draws it.
    std::unique_ptr<Element> frame_;
    // The frame last handed to the host, so a theme change only re-sends the style when it moves.
    Frame host_frame_ = Frame::Standard;
    // Creates or drops frame_ to match the theme, and re-sends the style when that changed the
    // frame the host runs.
    void sync_frame();
    // The client rectangle the content is laid out in: the whole client, less frame_'s border
    // and caption.
    [[nodiscard]] Rect       content_area(Size client) const;
    std::unique_ptr<Element> modal_;           // non-null while a DialogControl (or similar) is open
    ElementWatch             pre_modal_focus_; // the focused element at show_modal() time, restored by close_modal()
    bool modal_close_pending_ = false;         // close_modal() sets this; resolve_pending_modal_close() acts on it

    // ---- focus-taking popups (PopupRequest::takes_focus) ----
    // The owner whose popup the tab ring is scoped to while open, and what had focus before. The owner
    // is re-checked against a fresh owner walk in rebuild_focus_order(), so an owner destroyed with its
    // page is dropped; what had focus is watched, so it reads null once destroyed.
    Element*     focus_popup_ = nullptr;
    ElementWatch focus_popup_content_; // its content, which outlives a close (a Flyout keeps it)
    ElementWatch pre_popup_focus_;
    void         sync_focus_popup();
    // What had focus when the host window was deactivated.
    ElementWatch suspended_focus_;

    // ---- in-tree focus traps (Element::take_focus_request()) ----
    // Each element that asked to Enter and not yet to Restore, with what had focus before. Both are
    // watched, so an element destroyed before the next layout() reads null. Boxed: a watch does not move.
    struct FocusTrap
    {
        std::unique_ptr<ElementWatch> owner;
        std::unique_ptr<ElementWatch> previous;
    };
    std::vector<FocusTrap> focus_traps_;
    void                   sync_focus_requests(const std::vector<Element*>& owners);
    // What had focus at each push_focus() not yet popped, innermost last. Boxed, as above.
    std::vector<std::unique_ptr<ElementWatch>> pushed_focus_;

    // ---- drag and drop (ui/DragDrop.hpp) ----
    // The drag started in this window, which the pointer drives until it is dropped or canceled.
    DragDrop::SessionPtr drag_;
    void                 adopt_drag();
    void                 drag_pointer(const PointerEvent& e);
    void                 update_drag_target();
    void                 leave_drop_targets(const Element* except);
    void                 end_drag(DataPackageOperation result);
    // Drops the running drag on its target, if that accepted it, and ends the drag; returns what
    // the drop did.
    DataPackageOperation finish_drag();
    // A drag from another app, reached through WindowSeams: it comes over the window with `data`,
    // moves, leaves or is dropped. The first two answer with what the targets under it say.
    DragResponse         handle_external_drag_enter(DataPackage data, Point position);
    DragResponse         handle_external_drag_over(Point position, std::optional<DataPackageOperation> requested);
    void                 handle_external_drag_leave();
    DataPackageOperation handle_external_drag_drop(Point position);
    // All four at once, for a drop reported only where it was released.
    DataPackageOperation       handle_external_drop(DataPackage data, Point position);
    [[nodiscard]] DragResponse external_drag_response() const;
    [[nodiscard]] Element*     drop_target_at(Point position) const;

    // ---- pointer cursor ----
    [[nodiscard]] CursorShape  resolve_cursor(Point position) const;
    std::optional<CursorShape> pressed_cursor_{}; // latched by a left press until its release

    // ---- automation ----
    std::uint64_t content_generation_ = 0;
    int           dispatch_depth_     = 0;

    std::unique_ptr<Element> content_;
    Size                     client_size_{};
    /// Starts as Theme::light(). A default-constructed `Theme{}` leaves the per-control
    /// ButtonVisual fields (button_normal, accent_button_*, ...) unfilled, so every Theme member
    /// has to be initialized explicitly.
    Theme           theme_          = Theme::light();
    unsigned        theme_revision_ = 0;
    ThemeRequest    theme_request_{};
    OsThemeSnapshot os_theme_{};
    bool            theme_pinned_ = false;
    // After theme_: it keeps a pointer to it (set in the constructor).
    ConnectedAnimationService connected_animations_;
    bool                      laid_out_        = false; // has layout() run at least once?
    bool                      stretch_content_ = true;
    ParallelLayout            parallel_layout_{};
    std::optional<Color>      background_{};
    detail::Compositor*       compositor_  = nullptr; // non-owning
    NativeHost*               native_host_ = nullptr; // non-owning

    SpellChecker*             spell_checker_ = nullptr; // non-owning
    Callback<SpellChecker*()> spell_checker_source_;
    ContextMenuHost*          context_menu_host_ = nullptr; // non-owning
    // ---- automation events ----
    struct AutomationTracker; // Window.cpp: where events go, and the focus node last reported
    std::unique_ptr<AutomationTracker> automation_;
    void                               set_automation_events(AutomationEvents* events);
    // Reports a new automation focus node, or the changed properties of the same one.
    void sync_automation();

    // ---- window host ----
    WindowHost*                         window_host_ = nullptr; // non-owning
    WindowOptions                       options_;
    Property<WindowState>               state_{WindowState::Normal};
    Property<WindowState>               restores_to_{WindowState::Normal};
    Property<bool>                      active_{false};
    Property<bool>                      external_drag_{false};
    Property<float>                     scale_{1.0f};
    Property<CaptionInsets>             caption_insets_{};
    Property<bool>                      visible_{false};
    Size                                min_size_{};
    Size                                max_size_{};
    std::string                         title_;
    WindowStyle                         style_{};
    Backdrop                            backdrop_ = Backdrop::None;
    ChromeOverrides                     chrome_{};
    std::optional<WindowIcon>           icon_{};
    Bitmap                              caption_icon_{};
    bool                                blur_behind_ = false;
    std::vector<MenuItem>               system_menu_;
    std::unique_ptr<detail::MenuChecks> system_menu_checks_; // bound fields of system_menu_ -> host
    Property<TaskbarProgress>           taskbar_progress_{};
    Subscription                        taskbar_progress_forward_; // taskbar_progress_ -> host
    std::vector<Subscription>           option_bindings_;          // bound WindowOptions fields -> the setters
    std::optional<WindowIcon>           taskbar_overlay_{};
    std::string                         taskbar_overlay_description_;
    std::vector<ThumbnailButton>        thumbnail_buttons_;
    std::vector<Subscription>           thumbnail_bindings_; // bound fields and commands -> host

    Callback<Handling(const KeyEvent&)> key_preview_;
    Callback<void(Duration)>            tick_handler_;
    bool                                wants_ticks_ = true;
    // What tick() left dirty, if only timed steps did (Element::request_tick_in()), and the
    // thread's mark_dirty() count then: dirt still undrawn at the next tick is no reason to be busy
    // unless something marked since.
    bool          last_tick_quiet_ = true;
    std::uint64_t last_tick_marks_ = 0;

    // ---- timers (after(), every()) ----
    struct Timer; // Window.cpp
    std::vector<std::unique_ptr<Timer>> timers_;
    struct WindowCommand; // Window.cpp: add_command()
    std::vector<std::unique_ptr<WindowCommand>> commands_;
    double                                      timer_clock_ms_ = 0.0; // the sum of every tick()'s delta
    std::optional<double>                       element_wake_ms_;      // Element::request_tick_in(), on timer_clock_ms_
    Subscription add_timer(double delay_ms, double interval_ms, bool repeat, Callback<void()> fn);
    void         run_timers(); // fires what is due, drops what is done or canceled
    Callback<Decision(const WindowClosingArgs&)> closing_handler_;
    bool                         force_close_pending_ = false; // force_close(): skip closing_handler_ once
    Property<bool>               open_{false};                 // is_open()
    Callback<void()>             closed_handler_;
    Callback<void(const Theme&)> theme_changed_handler_;

    // ---- menu access (Alt, F10, Alt+letter) ----
    ElementWatch menu_access_target_; // what took it, while it still has focus
    ElementWatch menu_access_return_; // where focus was before
    // Host side: offers the menus to the tree (Element::take_menu_access()); true when taken.
    bool handle_menu_access(char32_t letter);

    // ---- cross-thread work (post()) ----
    std::mutex                     posted_mutex_; // guards posted_ and window_host_'s wake()
    std::vector<Callback<void()>>  posted_;
    bool                           closed_ = false;   // handle_closed() ran: post() drops its work
    std::optional<WindowPlacement> closed_placement_; // placement() once the host is gone
    std::uint64_t                  serial_ = 0;       // from detail::register_window(), for ref()
    // Queues `work` and wakes the host, or leaves it in `work` when the window is closed, for the
    // caller to destroy once no lock is held.
    bool enqueue(Callback<void()>& work);
    friend class WindowRef; // calls enqueue() under the registry's lock

    // ---- thread affinity (ThreadCheck.hpp) ----
    // Stored with or without LIBWINUI_THREAD_CHECKS, so the layout never depends on it; only the
    // check itself is switched.
    std::thread::id thread_ = std::this_thread::get_id(); // the UI thread: the constructing one
    // Reports a call from another thread. `call` empty names the caller from `where`.
    void check_thread(std::string_view call = {}, std::source_location where = std::source_location::current()) const
    {
#if LIBWINUI_THREAD_CHECKS
        if (thread_ != std::this_thread::get_id())
        {
            detail::report_thread_violation(call, "the window", thread_, where);
        }
#else
        (void)call;
        (void)where;
#endif
    }
    // A content, modal or popup root carries the window's thread for Element's own check.
    // A root with no parent also records the window, for Element::window(), and is told so.
    void adopt_root(Element& root) const
    {
        root.tree_thread_ = thread_;
        if (root.parent_ == nullptr && root.window_ == nullptr)
        {
            root.window_ = const_cast<Window*>(this);
            root.notify_window(true);
        }
    }

    mutable VisualTree visual_tree_;
    mutable bool       visual_tree_stale_ = true;
    std::size_t        layout_pass_count_ = 0;

    // What layout() last laid out, beyond the dirty flags: a new root or popup needs a pass of
    // its own even when nothing in a tree is marked.
    struct LayoutSignature
    {
        const Element* content            = nullptr;
        const Element* frame              = nullptr;
        const Element* modal              = nullptr;
        std::size_t    popups             = 0;
        std::uint64_t  content_generation = 0;

        friend bool operator==(const LayoutSignature&, const LayoutSignature&) = default;
    };
    LayoutSignature last_layout_{};

    // ---- what take_damage() measured, for the next one ----
    void          render_frame(Renderer& renderer) const; // render()'s body, for every kind of pass
    std::uint32_t paint_frame_   = 0;                     // the PaintTracker frame, counted by take_damage()
    bool          measured_once_ = false;
    Size          measured_client_{};
    Color         measured_background_{};
    std::uint64_t painted_window_hash_ = 0;
    Rect          painted_window_area_{};
    // Each root the last frame drew (content, popups, modal) and where it drew. A root no longer
    // drawn leaves its area to repaint: no parent's children hash notices a popup closing. The
    // pointer is only compared, never followed, since the root may be gone.
    std::vector<std::pair<const Element*, Rect>>                      painted_roots_;
    std::unique_ptr<detail::PaintScratch, detail::PaintScratchDelete> paint_scratch_; // made by the first take_damage()
    // What the last full or incremental pass laid out with; a change makes the next pass full.
    Size                last_client_{};
    const TextMeasurer* last_measurer_         = nullptr;
    std::uint64_t       last_theme_generation_ = 0;
    // layout()'s incremental path; false when only a full pass will do.
    bool relayout_from_dirty(Element& root, const TextMeasurer& measurer);

    std::uint64_t         focus_order_epoch_ = 0; // detail::removed_element_count() at the last rebuild
    std::vector<Element*> focus_order_;           // flat tab ring over the content tree or, while a
                                                  // scope is entered, over that scope alone
    /// Entered focus scopes, outermost first (Element::focus_scope()). Usually empty, and the
    /// ring is the whole tree. Checked and unwound on every rebuild_focus_order(), so a scope that
    /// collapses, empties or is replaced while entered stops being entered. Watched, so a scope
    /// destroyed before that reads null. Boxed: a watch does not move.
    std::vector<std::unique_ptr<ElementWatch>> focus_scope_stack_;
    /// The focused element, read through a watch so input that arrives after a handler destroyed
    /// it (before the next layout() refreshes the ring) finds null instead of a dangling pointer.
    /// An element that only left the ring (disabled, collapsed) is still told it no longer has
    /// keyboard focus (Element::has_focus_within()), since the watch still reaches it.
    ElementWatch focus_watch_;
    /// The stops after and before the focused one in the ring, refreshed whenever focus moves or the
    /// ring is rebuilt around it. When the focused element is destroyed or leaves the ring, Tab and
    /// Shift+Tab carry on from here instead of starting over at the first stop.
    ElementWatch focus_after_;
    ElementWatch focus_before_;
    void         anchor_focus();
    /// Every change of focus goes through here, keeping Element::note_keyboard_focus() in step.
    /// With `notify`, queues Element::on_got_focus()/on_lost_focus() for the elements focus
    /// entered or left, which deliver_focus_events() raises once the elements have been told.
    void set_focused_element(Element* e, bool notify = true);
    /// Raises the focus events set_focused_element() queued, skipping elements destroyed since.
    void deliver_focus_events();
    struct PendingFocusEvent
    {
        std::unique_ptr<ElementWatch> element;
        bool                          got = false;
    };
    std::vector<PendingFocusEvent> pending_focus_events_;
    /// Starts in pointer modality (false), so a control focused from code before any input,
    /// such as the initial focus when the window is activated, shows no focus ring until a key
    /// is pressed.
    bool keyboard_modality_ = false;
};

} // namespace ui
