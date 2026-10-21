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
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/menus/MenuItem.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui
{

/// The state a window is in, read through Window::state() and asked for with
/// Window::request_state(). Two things are not states:
///
///   * A snapped or arranged window (snap layouts, Win+Left/Right) is in the Normal state. The OS
///     controls snapping, and an app cannot request it.
///   * Exclusive fullscreen, where the app owns the display mode, is not supported. `Fullscreen` is
///     the borderless kind that browsers and video players use.
///
/// Visibility (Window::visible()) and always-on-top (WindowStyle::always_on_top) are independent
/// of the state.
///
/// Example:
/// @code
/// window.set_content(ui::Button{
///     .text = ui::bind(window.state(), [](ui::WindowState s) {
///         return std::string{s == ui::WindowState::Maximized ? "Restore" : "Maximize"}; }),
///     .on_click = [&] {
///         if (window.state().get() == ui::WindowState::Maximized) window.restore();
///         else window.request_state(ui::WindowState::Maximized);
///     },
/// });
/// @endcode
enum class WindowState
{
    Normal,     ///< framed, at its normal bounds (possibly snapped)
    Minimized,  ///< a taskbar button only
    Maximized,  ///< fills the monitor's work area; the taskbar stays visible
    Fullscreen, ///< covers the whole monitor, with no caption or border
};

/// Where a window was, in a form an app can store on close and hand back on the next run
/// (Window::placement(), ui::WindowOptions::placement). The app decides where to store it, such
/// as in a file or the registry. A saved monitor that has since gone away is not a problem, since
/// the OS pulls the window back on screen when the placement is applied.
///
/// Example:
/// @code
/// // The app's own storage, such as a settings file:
/// std::optional<ui::WindowPlacement> load_placement();
/// void                               save_placement(const ui::WindowPlacement& placement);
///
/// const std::optional<ui::WindowPlacement> saved = load_placement(); // nullopt on the first run
/// ui::Window editor{{.title = "Editor", .placement = saved}};
/// ui::run(editor);
/// if (auto p = editor.placement()) save_placement(*p); // where the window was when it closed
/// @endcode
struct WindowPlacement
{
    /// The restored bounds, in DIPs, screen coordinates. They are kept while the window is
    /// minimized, maximized or full screen.
    Rect normal_bounds{};
    /// The state the window was in when the placement was read; the next run shows in it.
    WindowState state = WindowState::Normal;
    /// Whether leaving Minimized or Fullscreen goes back to Maximized rather than Normal.
    bool restore_to_maximized = false;

    friend bool operator==(const WindowPlacement&, const WindowPlacement&) = default;
};

/// The caption space a window with content extended into its title bar still has to leave
/// alone (WindowStyle::extend_content_into_title_bar): the OS's caption buttons on one side,
/// and on the other whatever the OS keeps there (nothing on a left-to-right window).
/// DIPs, window-client coordinates. All zero when the content is not extended, so a TitleBar
/// can bind to it unconditionally. Read through Window::caption_insets().
///
/// Example:
/// @code
/// // Keep a custom title row clear of the caption buttons.
/// ui::Border{
///     .padding = ui::bind(window.caption_insets(), [](const ui::CaptionInsets& c) {
///         return ui::Edges{.left = c.left, .right = c.right}; }),
///     .content = ui::Text{.text = "My app"},
/// }
/// @endcode
struct CaptionInsets
{
    float left   = 0.0f; ///< reserved at the left edge, in DIPs
    float right  = 0.0f; ///< reserved at the right edge (the caption buttons), in DIPs
    float height = 0.0f; ///< the extended caption row's height

    friend bool operator==(const CaptionInsets&, const CaptionInsets&) = default;
};

/// The state of the taskbar button's progress bar.
enum class TaskbarProgressState
{
    None,          ///< no progress bar
    Indeterminate, ///< marquee; `value` is ignored
    Normal,        ///< green
    Paused,        ///< yellow
    Error,         ///< red
};

/// The taskbar button's progress bar: Window::taskbar_progress() is a Property of it, written by
/// the app and shown on the taskbar on every change.
///
/// Example:
/// @code
/// window.taskbar_progress() = ui::TaskbarProgress{ui::TaskbarProgressState::Normal, 0.4};   // 40 %
/// window.taskbar_progress() = ui::TaskbarProgress{ui::TaskbarProgressState::Error, 0.4};    // turn it red
/// window.taskbar_progress() = ui::TaskbarProgress{};                                     // remove it
/// @endcode
struct TaskbarProgress
{
    /// Whether a bar shows and in which color; None hides it.
    TaskbarProgressState state = TaskbarProgressState::None;
    /// Completion in [0, 1].
    double value = 0.0;

    friend bool operator==(const TaskbarProgress&, const TaskbarProgress&) = default;
};

/// A small picture for the OS to show: the caption and taskbar icon, a taskbar overlay badge, or
/// a thumbnail-toolbar button. It is one of three kinds, and the first one set wins:
/// - an image (straight-alpha RGBA), scaled to each size the OS needs
/// - a glyph drawn over an optional background, the same IconGlyph controls draw symbols with
/// - an icon from the app's own resources
///
/// Example:
/// @code
/// // A glyph on the accent color as the caption/taskbar icon...
/// window.set_icon(ui::WindowIcon{.glyph      = ui::IconGlyph{0xE80F},
///                             .foreground = ui::colors::White,
///                             .background = ui::Color::from_argb(0xFF0F6CBD)});
/// // ...and a red badge over the taskbar button.
/// window.set_taskbar_overlay(
///     ui::WindowIcon{.glyph = ui::IconGlyph{0xE7E7}, .foreground = ui::Color::from_argb(0xFFC42B1C)}, "3 unread");
/// @endcode
struct WindowIcon
{
    /// The picture, straight-alpha RGBA; takes precedence over `glyph` when not empty.
    Bitmap image{};
    /// A Segoe Fluent Icons glyph, used when `image` is empty; codepoint 0 is none.
    IconGlyph glyph{0};
    /// The glyph's color. Unset draws it black on a light taskbar and white on a dark one, since
    /// the taskbar shows the icon, its overlay and the thumbnail buttons. It is redrawn when the
    /// user switches the taskbar's theme.
    std::optional<Color> foreground{};
    /// Filled behind the glyph; transparent leaves just the glyph.
    Color background = colors::Transparent;
    /// An icon the app ships with, used when `image` and `glyph` are both empty. It names an icon
    /// resource in the executable: `"APPICON"` by name, `"#101"` by number. It is loaded at each
    /// size the OS needs.
    std::string resource{};

    /// No image, glyph or resource: nothing to show.
    [[nodiscard]] bool empty() const
    {
        return image.empty() && glyph.codepoint == 0 && resource.empty();
    }

    friend bool operator==(const WindowIcon&, const WindowIcon&) = default;
};

/// A button under the taskbar thumbnail, as media players use for play, pause and skip. A window
/// has at most seven. The OS fixes the set of buttons at the first call, so later calls can change
/// a button's icon, tooltip, enabled and visible state, but not add or remove one. `tool_tip`,
/// `enabled` and `visible` take a value or a property, and the window follows a bound one for as
/// long as it holds the buttons. `on_click` is a std::function, so the button stays copyable in a
/// brace-initialized std::vector.
///
/// Example:
/// @code
/// ui::Property<int>  track{0};
/// ui::Property<bool> playing{false};
/// ui::Property<bool> has_previous{false};
/// ui::Command        next_track{.text = "Next", .execute = [&] { track = track.get() + 1; }};
/// window.set_thumbnail_buttons({
///     {.icon = {.glyph = ui::IconGlyph{0xE892}}, .tool_tip = "Previous", .enabled = has_previous,
///      .on_click = [&] { track = track.get() - 1; }},
///     {.icon     = {.glyph = ui::IconGlyph{0xE768}},
///      .tool_tip = ui::bind(playing, [](bool on) { return std::string{on ? "Pause" : "Play"}; }),
///      .on_click = [&] { playing = !playing.get(); }},
///     {.icon = {.glyph = ui::IconGlyph{0xE893}}, .tool_tip = "Next", .command = next_track},
/// });
/// @endcode
struct ThumbnailButton
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The button's picture, scaled to the thumbnail toolbar's size.
    WindowIcon icon{};
    /// Shown when the pointer rests on the button. Falls back to the command's description.
    Binding<std::string> tool_tip{};
    /// False grays the button out; a disabled command does too.
    Binding<bool> enabled{true};
    /// False hides the button while keeping its slot in the fixed set.
    Binding<bool> visible{true};
    /// Run with `command_parameter` after `on_click`; it also disables the button while it is
    /// disabled.
    CommandRef command{};
    /// Passed to the command's execute.
    std::string command_parameter{};
    /// Run on the UI thread when the button is clicked, before the command.
    std::function<void()> on_click{};

    /// `tool_tip`, or the command's description when it is empty.
    [[nodiscard]] std::string display_tool_tip() const;
    /// Enabled, and its command (if any) is too.
    [[nodiscard]] bool is_enabled() const;
};

} // namespace ui
