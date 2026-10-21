// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/Notification.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/WindowOptions.hpp"
#include "winui/ui/WindowShell.hpp"
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui
{

/// What a TrayIcon is created with: the icon, its hover text, its right-click menu and,
/// optionally, the window a click opens next to it.
///
/// Example:
/// @code
/// ui::Property<bool> quiet{false};
///
/// ui::TrayIcon tray{{
///     .tool_tip = ui::bind(quiet, [](bool q) { return std::string{q ? "Timer (muted)" : "Timer"}; }),
///     .icon     = ui::WindowIcon{.glyph = ui::IconGlyph{0xE916}},
///     .menu     = {{.text = "Quit", .on_click = [] { ui::quit(); }}},
/// }};
/// @endcode
struct TrayIconOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The icon's hover text. The shell cuts it at 127 characters.
    Binding<std::string> tool_tip{};
    /// The icon, drawn at the taskbar's small-icon size. `ui::WindowIcon{.resource = "#101"}` uses
    /// the executable's own icon resource. Empty shows the stock application icon.
    Binding<WindowIcon> icon{};
    /// A glyph icon on a transparent background is drawn black on a light taskbar and white on a
    /// dark one, whatever `icon.foreground` says, and redrawn when the user switches. Without
    /// it, a white glyph is invisible on a light taskbar.
    bool icon_follows_taskbar_theme = true;
    /// The right-click menu, top to bottom. It uses each item's text, `on_click`, `command` and
    /// `enabled`, separators, and the `checked` state of toggle and radio items. A toggle flips and
    /// a radio item unchecks its group as in any menu, and `.checked = prop` follows and writes the
    /// property. The app writes every item, including its way out
    /// (`{.text = "Quit", .on_click = [] { ui::quit(); }}`). Change it later with set_menu().
    std::vector<MenuItem> menu{};

    // ---- the flyout ----
    /// A window the app owns, opened next to the icon by a click and hidden by the next one.
    /// Create it with tray_flyout_options(). It must outlive the TrayIcon. With nullptr the icon
    /// has no flyout, and a click runs `on_click`.
    Window* flyout = nullptr;
    /// The flyout is as tall as its content, measured each time it opens, up to this many DIPs.
    /// Zero makes it as tall as the work area allows, less the margin.
    float flyout_max_height = 0.0f;
    /// Space between the flyout and the taskbar, and the screen edges beside it, in DIPs.
    float flyout_margin = 12.0f;
    /// Close the flyout when it stops being the active window, such as on a click anywhere else,
    /// Alt+Tab or the Win key. Otherwise it stays open until hide_flyout() or a second click
    /// on the icon.
    bool light_dismiss = true;
    /// The flyout's fill. nullopt uses the theme's flyout surface.
    std::optional<Color> flyout_background{};

    // ---- behavior ----
    /// A click or Enter on the icon, when there is no `flyout`.
    Callback<void()> on_click{};
    /// The user clicked the last notification, when it had no `on_click` of its own. Empty opens
    /// the flyout.
    Callback<void()> on_notification_click{};
};

/// An icon in the taskbar's notification area, with a hover text, a right-click menu and
/// notifications. It is only the icon. The app runs its own loop (ui::run()) and ends it with
/// ui::quit(). A flyout is an ordinary Window the app owns and attaches through
/// TrayIconOptions::flyout.
///
/// The icon is added when the TrayIcon is created and removed when it is destroyed. UI thread
/// only.
///
/// Example:
/// @code
/// int main()
/// {
///     ui::Property<float> volume{40.0f};
///
///     ui::Window flyout{ui::tray_flyout_options({.title = "Volume", .width = 320.0f})};
///     flyout.set_content(ui::VerticalStack{
///         .spacing  = 16.0f,
///         .margin   = ui::Thickness{20.0f},
///         .content  = {ui::Slider{.value = volume}},
///     });
///
///     ui::TrayIcon tray{{
///         .tool_tip = "Volume",
///         .icon     = ui::WindowIcon{.glyph = ui::IconGlyph{0xE767}},
///         .menu     = {{.text = "Open", .on_click = [&] { tray.show_flyout(); }},
///                     {.text = "Quit", .on_click = [] { ui::quit(); }}},
///         .flyout  = &flyout,
///     }};
///     return ui::run(); // until ui::quit(); closing the flyout only hides it
/// }
/// @endcode
class TrayIcon
{
public:
    /// Adds the icon. When there is no taskbar yet, the icon is added as soon as the taskbar
    /// starts.
    explicit TrayIcon(TrayIconOptions options = {});
    /// Removes the icon.
    ~TrayIcon();
    /// Not copyable: owns the icon.
    TrayIcon(const TrayIcon&) = delete;
    /// Not copyable: owns the icon.
    TrayIcon& operator=(const TrayIcon&) = delete;

    /// Replace the icon's hover text.
    void set_tool_tip(std::string tool_tip);
    /// Replace the icon, for example to show a state. See TrayIconOptions::icon_follows_taskbar_theme.
    void set_icon(WindowIcon icon);
    /// Replace the right-click menu.
    void set_menu(std::vector<MenuItem> items);

    /// Shows a notification from the icon. It is what ui::notify() shows, sent with the tray icon
    /// as its source, so NotificationIcon::App is the tray icon. The user's notification settings decide
    /// whether it is shown. Replaces one still showing. A click runs `notification.on_click`, or
    /// TrayIconOptions::on_notification_click when that is empty.
    ///
    /// Example:
    /// @code
    /// ui::TrayIcon tray{{.tool_tip = "Timer"}};
    /// tray.notify({.title = "Timer", .text = "Time is up.", .on_click = [&] { tray.show_flyout(); }});
    /// @endcode
    void notify(Notification notification);

    /// Open the flyout next to the icon, sized to its content, and give it the focus. Does
    /// nothing without a flyout.
    void show_flyout();
    /// Hide the flyout. It is not destroyed, so its content and state stay.
    void hide_flyout();
    /// show_flyout() if hidden, hide_flyout() if open: what a click on the icon does.
    void toggle_flyout();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// What tray_flyout_options() takes: the few things an app chooses about a tray flyout.
struct TrayFlyoutOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The window's title. No caption shows it, but screen readers read it.
    std::string title{};
    /// The flyout's client width, in DIPs. Its height follows its content.
    float width = 360.0f;
    /// A drop shadow under the flyout. Off by default, as under the system's own flyouts. The
    /// rounded corners and the 1px rim stay either way.
    bool shadow = false;
    /// What to resolve the flyout's Theme from.
    ThemeRequest theme{};
    /// A system backdrop behind the flyout instead of its fill. Off by default.
    Backdrop backdrop = Backdrop::None;
    /// See SpellCheckOptions.
    SpellCheckOptions spell_check{};
};

/// The WindowOptions of a tray flyout: a bare, always-on-top popup with rounded corners and no
/// taskbar button, created hidden, that hides instead of closing. Construct an ordinary Window
/// with it and hand that window to TrayIconOptions::flyout.
///
/// Example:
/// @code
/// ui::Window flyout{ui::tray_flyout_options({.title = "Timer", .width = 320.0f})};
/// @endcode
[[nodiscard]] WindowOptions tray_flyout_options(TrayFlyoutOptions options = {});

/// Where a flyout of `size` goes, beside a tray icon at `anchor` on a monitor whose work area is
/// `work_area`, in any unit as long as all four agree. Returns the top-left. The taskbar is on
/// the edge of the work area that the anchor lies outside of. The flyout sits `margin` inside
/// that edge, centered on the anchor along it and kept `margin` clear of the corners. An anchor
/// inside the work area, such as one in the overflow flyout or an empty rect, is treated as a
/// bottom taskbar.
///
/// Public for an app that places a window of its own the same way.
///
/// Example:
/// @code
/// // A notes window of the app's own, opened beside the tray icon the way a flyout is. `icon` is
/// // the icon's rect in screen DIPs, wherever the app got it from.
/// ui::Window     notes{{.title = "Notes"}};
/// const ui::Rect icon{1000.0f, 1040.0f, 32.0f, 32.0f};
/// const ui::Size size{360.0f, 400.0f};
/// if (const std::optional<ui::Rect> work = notes.work_area())
/// {
///     notes.resize(size);
///     notes.move(ui::place_tray_flyout(icon, *work, size, 12.0f));
/// }
/// // With the icon at {1000, 1040, 32, 32} and a 1920x1032 work area above a bottom taskbar, the
/// // window goes to {836, 620}: centered on the icon, 12 DIPs above the taskbar.
/// @endcode
[[nodiscard]] Point place_tray_flyout(const Rect& anchor, const Rect& work_area, Size size, float margin);

} // namespace ui
