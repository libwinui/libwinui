// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"


namespace ui
{

/// How much of the frame the OS draws (WindowStyle::frame).
enum class Frame
{
    /// The OS caption and border. The default.
    Standard,
    /// No caption and no visible border. The system still treats it as a normal window, so
    /// snap layouts, Win+arrow keys, shake to minimize, the minimize animation, the drop shadow
    /// and Alt+Space's system menu keep working. Resize edges follow `resizable`. Dragging needs a
    /// region that answers HitRegion::Caption (TitleBar, DragArea).
    Borderless,
    /// A bare popup: no caption, no border, no resizing, no shadow and no snapping. For splash
    /// screens and overlays. WindowState::Fullscreen uses this frame too.
    None,
    /// The theme draws the caption and the border in its era's style. Underneath it is
    /// `Borderless`, so snapping, Win+arrow keys, the system menu and the drop shadow keep working,
    /// and hovering the drawn maximize button opens the snap layouts where the system has them.
    /// Corners are square. The caption shows the window's icon, and screen readers see it as a
    /// title bar with its three buttons. A theme with no frame of its own (Fluent 2, Fluent, MDL2) gives `Standard`,
    /// and switching themes switches between the two live.
    Themed,
};

/// The system material behind the window. Where the system has no backdrop materials, the
/// window keeps its opaque background.
///
/// The theme can decline it (ChromeHints::allows_system_backdrop does for every era before
/// Windows 11, and for high contrast), and so can the system, for example with transparency
/// effects off or in energy saver mode. Seeded by `ui::WindowOptions::backdrop`, applied live
/// through Window::set_backdrop(). While a backdrop is on, the client area is left transparent
/// and Window::background() is not painted.
///
/// Example:
/// @code
/// int main()
/// {
///     ui::Window window{{.title = "Mica", .backdrop = ui::Backdrop::Mica}};
///     window.set_content(ui::Button{
///         .text     = "Switch to Acrylic",
///         .on_click = [&window] { window.set_backdrop(ui::Backdrop::Acrylic); }, // applied live
///     });
///     return ui::run(window);
/// }
/// @endcode
enum class Backdrop
{
    /// No material: the window's opaque background. The default.
    None,
    /// The desktop wallpaper, sampled once, tinting a long-lived window.
    Mica,
    /// Like Mica with a stronger tint, for tabbed windows.
    MicaAlt,
    /// A blurred, semi-transparent view of what is behind the window, updated continuously.
    Acrylic,
};

/// The frame a window is drawn with: caption buttons, sizing border, frame kind, z-order and
/// title-bar extension.
///
/// Seed it through `ui::WindowOptions::style`. To change it live, edit a copy of Window::style()
/// and hand it to Window::set_style(), which applies it to the running window. The defaults give
/// a plain resizable window with a caption. Fill it with designated initializers.
///
/// Example:
/// @code
/// // A fixed-size tool palette that stays on top and has no maximize button.
/// ui::Window palette{{.title = "Tools",
///                     .style = {.resizable = false, .maximize_button = false, .always_on_top = true}}};
///
/// // Content under the caption of the main window, with a TitleBar in the 48 DIP row it leaves.
/// ui::WindowStyle style = window.style();
/// style.extend_content_into_title_bar = true;
/// style.title_bar_height              = 48;
/// window.set_style(style);
/// @endcode
struct WindowStyle
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// A sizing border, and with it drag-to-snap. On `Frame::None` it has no effect: a popup
    /// has no border to size by.
    bool resizable = true;
    /// The caption's minimize button (and Win+Down's fallback target).
    bool minimize_button = true;
    /// The caption's maximize button, maximizing by double-clicking the caption, and Win+Up.
    /// Independent of `resizable`. It only limits the user. Window::request_state() still
    /// maximizes a window that has no maximize button.
    bool maximize_button = true;
    /// Stays above every non-topmost window. Off by default.
    bool always_on_top = false;

    /// How much of the frame the OS draws; see `Frame`.
    Frame frame = Frame::Standard;
    /// A short caption, no taskbar button and no entry in Alt+Tab, for a palette or a tool
    /// strip that belongs to another window.
    bool tool_window = false;
    /// Whether the close button and Alt+F4 are enabled. Off also grays out the system menu's
    /// Close item. An app that must stay open also has to refuse to quit when it is asked
    /// another way.
    bool closable = true;
    /// The OS's open/close/minimize/maximize animations. Off suits a kiosk or a remote
    /// session.
    bool animations = true;
    /// Whether the caption draws the window title. Only meaningful on `Frame::Standard`; the
    /// caption buttons stay either way.
    bool caption_title = true;
    /// Whether the caption draws the window icon. Only meaningful on `Frame::Standard`. Without
    /// the icon, clicking where it was no longer opens the system menu (Alt+Space and a caption
    /// right-click still do).
    bool caption_icon = true;
    /// Let the client area extend over the caption. The OS keeps drawing the caption buttons and
    /// the caption background, and the app lays out its own title row, usually a TitleBar,
    /// underneath them. Window::caption_insets() reports the space the buttons take. Only
    /// meaningful on `Frame::Standard`. Ignored while fullscreen.
    bool extend_content_into_title_bar = false;
    /// How tall the extended caption row is at least, in DIPs, when
    /// `extend_content_into_title_bar` is on. The row is never shorter than the OS's own caption,
    /// so 0 leaves it at that height. A taller row (TitleBar's 48 DIP expanded height) extends the
    /// caption background further; the buttons stay at the top.
    float title_bar_height = 0.0f;

    /// The caption's minimize, maximize and close buttons. Off takes all three away, and the
    /// system menu with them. Meaningful on `Frame::Standard`, and on `Frame::Borderless` under a
    /// system backdrop, where the system would otherwise draw the buttons over the content (a tray
    /// flyout turns it off for that). `Frame::None` has no caption at all. Window::close() still
    /// closes. With content extended into the title bar, off means no buttons and no caption band
    /// beyond `title_bar_height`, so 0 leaves no band at all.
    bool caption_buttons = true;

    /// On `Frame::None`, gives the popup rounded corners and a 1px rim like a top-level window
    /// has, where a bare popup is square. The system's own flyouts (Start, Quick Settings, the
    /// calendar) look this way, with no shadow. Applies only while the theme's
    /// `ChromeHints::corner_preference` is `Default`, so an era that asks for square corners keeps
    /// them. No effect on any other frame, which the system already rounds.
    bool round_popup_corners = false;

    friend bool operator==(const WindowStyle&, const WindowStyle&) = default;
};

} // namespace ui
