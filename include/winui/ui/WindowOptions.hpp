// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/WindowShell.hpp"
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ui
{

class Window;

/// TextBox spell checking for one window. Read once, when the OS window is created. The user's
/// own system settings (language, "Highlight misspelled words", "Autocorrect misspelled words")
/// are still followed live.
///
/// Example:
/// @code
/// int main()
/// {
///     ui::Window window{{.title = "Notes"}};
///     window.options().spell_check.autocorrections = {{"libwnui", "libwinui"}, {"widgit", "widget"}};
///     return ui::run(window);
/// }
/// @endcode
struct SpellCheckOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// False turns the spell checker off: no squiggles, no autocorrect and no spelling entries in
    /// the right-click menu, which still offers Cut, Copy and Paste. A single field opts out with
    /// TextBoxControl::set_spell_check(false) or ui::TextBox{.spell_check = false}.
    bool enabled = true;
    /// A small built-in table of common English typos (teh → the, ...).
    bool builtin_autocorrections = true;
    /// The app's own corrections (from → to), on top of the built-in table. See
    /// SpellChecker::add_autocorrection().
    std::vector<std::pair<std::string, std::string>> autocorrections{};
};

/// What a Window is created with: title, size, theme, frame, placement and the per-window
/// services. A Window owns one, given to its constructor. `title`, `theme`, `backdrop` and `icon`
/// take a value or a property: bound to a property, the window follows it for as long as both
/// live, so one `ui::Property<ui::ThemeRequest>` can drive every window of an app. `style`,
/// `chrome`, `system_menu` and the size limits seed the Window's own live properties
/// (set_style(), ...), which is where to change them afterwards; the rest is read once, when the
/// OS window is created by ui::run() or Window::show().
///
/// A bound field subscribes to its properties while the Window is constructed. A Window that is
/// a class member therefore has to be declared after the properties it binds, since members are
/// constructed in declaration order; bound to a member declared later, it reads an object that
/// does not exist yet.
///
/// Example:
/// @code
/// int main()
/// {
///     ui::Property<ui::ThemeRequest> theme{{.mode = ui::ThemeMode::Dark}};
///     ui::Window window{{
///         .title    = "My Application",
///         .size     = {1024, 768},
///         .theme    = theme, // every window bound to `theme` switches with it
///         .backdrop = ui::Backdrop::Mica,
///         .min_size = {480, 320},
///     }};
///     window.set_content(ui::Text{.text = "Hello"});
///     return ui::run(window);
/// }
/// @endcode
struct WindowOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The caption and taskbar text, UTF-8; Window::title(). Bound, the window follows it.
    Binding<std::string> title{};
    /// The client area's size in DIPs, the room the content gets, whatever frame the theme
    /// draws around it; scaled by DPI at creation.
    Size size{800.0f, 600.0f};

    /// What to resolve the live Theme from; Window::set_theme(). Defaults to ThemeMode::System,
    /// which follows the OS light/dark, accent and high-contrast settings as they change. Bound,
    /// the window follows it (a pin_theme() in between is undone by the next change). A
    /// designated list needs its type here: `.theme = ui::ThemeRequest{.mode = ...}`.
    Binding<ThemeRequest> theme{};

    /// Opt-in system backdrop material (ui::Backdrop); Window::set_backdrop(). Bound, the window
    /// follows it.
    Binding<Backdrop> backdrop{Backdrop::None};

    /// The frame (ui::WindowStyle). The defaults give a plain resizable window with a caption, a
    /// system menu and minimize and maximize buttons.
    /// Seeds Window::set_style(); change it live through the Window afterwards.
    WindowStyle style{};

    /// See `SpellCheckOptions` above.
    SpellCheckOptions spell_check{};

    // ---- placement and window state ----
    /// The state to show in. `Normal` honors the launcher, so a shortcut set to "Run: Maximized"
    /// or "Minimized" still gets its way.
    WindowState initial_state = WindowState::Normal;
    /// Top-left of the window, in DIPs of the primary monitor. Absent lets the system cascade it.
    std::optional<Point> position{};
    /// Smallest client size the user can drag the window to, in DIPs; zero on an axis leaves it
    /// unconstrained. Not applied while full screen.
    Size min_size{};
    /// Largest client size, in DIPs; zero on an axis leaves it unconstrained.
    Size max_size{};
    /// A placement saved from Window::placement() on a previous run. Overrides `initial_state`
    /// and `position`. The system pulls it back on screen if its monitor is gone.
    std::optional<WindowPlacement> placement{};
    /// Seeds Window::set_chrome(): this window's own frame colors and corners over the theme's.
    ChromeOverrides chrome{};
    /// The caption and taskbar icon; Window::set_icon(). Absent uses the executable's first icon
    /// resource, the one the file manager shows for the file, or the system's default without one.
    /// `ui::WindowIcon{.resource = "#101"}` picks another of the executable's icons. Bound, the
    /// window follows it.
    Binding<std::optional<WindowIcon>> icon{};
    /// Seeds Window::set_system_menu() when non-empty.
    std::vector<MenuItem> system_menu{};
    /// The progress the taskbar button shows; Window::taskbar_progress(), which also sets it
    /// after construction. Bound, the button follows it, such as a job's progress while it runs.
    /// A Window class member binds only properties declared before it (see WindowOptions).
    ///
    /// Example:
    /// @code
    /// ui::Property<double> done{0.0}; // the export's progress, 0 to 1
    /// ui::Window window{{
    ///     .title            = "Export",
    ///     .taskbar_progress = ui::bind(done, [](double f) {
    ///         const auto state = f < 1.0 ? ui::TaskbarProgressState::Normal : ui::TaskbarProgressState::None;
    ///         return ui::TaskbarProgress{state, f};
    ///     }),
    /// }};
    /// @endcode
    Binding<TaskbarProgress> taskbar_progress{};

    // ---- owned windows ----
    /// The window this one belongs to: an owned window stays above its
    /// owner, minimizes and closes with it, and has no taskbar button of its own. nullptr: none.
    Window* owner = nullptr;
    /// With an `owner`: the owner takes no input while this window is open, and gets the
    /// activation back when it closes. Ignored without one.
    bool modal = false;

    // ---- hidden and hide-on-close windows (tray) ----
    /// false creates the window hidden, laid out and ready for Window::show(). `position` and
    /// `size` still apply; `initial_state` and `placement` do not, as both would show it.
    bool visible = true;
    /// Closing hides the window instead of destroying it, by the close button, Alt+F4 and
    /// Window::close() alike, after on_closing() has had its say. For a window the app shows
    /// again, like a tray flyout; ui::quit() still takes it down with the loop.
    bool close_hides = false;
};

} // namespace ui
