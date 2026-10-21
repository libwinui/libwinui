// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/NativeHandle.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>

namespace ui
{

/// An Element that hosts a real platform child window (an HWND on Windows) of any window class,
/// the escape hatch for OS controls the framework does not draw itself.
///
/// The window is created lazily on the first sync_native(), moved whenever the arranged bounds
/// change, clipped to what scrolling ancestors leave in view, hidden when it stops being shown,
/// and destroyed with the element. It paints itself, so the theme doesn't apply to it, sits above
/// everything the framework draws, and is outside the Tab-focus ring. A
/// derived class answers the window's parent notifications in handle_native_notification() and
/// configures it once in on_native_created(). The NativeHost it was last synced with must
/// outlive it. Windows only: without a native host (a headless window, another platform) the
/// element keeps its size and draws nothing.
///
/// Example:
/// <!-- not compiled: needs <windows.h> and <commctrl.h>, which the check cannot include inside its namespace -->
/// @code
/// // Any window class, by name, answering its WM_NOTIFY (Windows only).
/// class MonthCalendar final : public ui::NativeControl
/// {
/// public:
///     MonthCalendar()
///         : ui::NativeControl(ui::NativeControlDesc{.class_name = MONTHCAL_CLASSA}, ui::Size{230, 160})
///     {
///     }
///
///     bool handle_native_notification(ui::NativeNotification& n) override
///     {
///         const auto* header = reinterpret_cast<const NMHDR*>(n.lparam);
///         if (n.message != WM_NOTIFY || header->code != MCN_SELECT)
///             return false;
///         selected = true; // read the date with MCM_GETCURSEL on native_handle()
///         return true;
///     }
///
///     bool selected = false;
///
/// protected:
///     void on_native_created(ui::NativeHandle handle) override
///     {
///         RECT r{};
///         MonthCal_GetMinReqRect(handle, &r);
///         set_native_size(ui::Size{float(r.right - r.left), float(r.bottom - r.top)});
///     }
/// };
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = { ui::Text{.text = "Pick a date"}, std::make_unique<MonthCalendar>() },
/// });
/// @endcode
class NativeControl : public Element
{
public:
    /// `desc` describes the window class/style to create; `size` is the fixed desired
    /// size measure() reports. Native controls take no part in text measurement, so a caller
    /// sizes them explicitly.
    NativeControl(NativeControlDesc desc, Size size);
    /// Destroys the native window through the host it was last synced with, if it was created.
    ~NativeControl() override;

    NativeControl(const NativeControl&)            = delete;
    NativeControl& operator=(const NativeControl&) = delete;

    // ---- native lifecycle ----
    /// Called by Window after layout and input. Creates the native window on the first call and
    /// moves it whenever bounds() changed since the last sync.
    void sync_native(NativeHost& host) override;

    /// Called when the hosted control reports that its content changed. On Windows that is an
    /// EN_CHANGE notification, as a RichEdit sends. The framework can't see such an edit
    /// otherwise, since the text lives inside the native control and the keystrokes go straight
    /// to the child HWND.
    virtual void on_native_content_changed()
    {
    }

    /// A message the hosted window sent its parent about itself (render/NativeHost.hpp's
    /// NativeNotification): WM_COMMAND, WM_NOTIFY, WM_CTLCOLOR* and a trackbar's or scroll bar's
    /// WM_HSCROLL/WM_VSCROLL on Windows. Return true, with `notification.result` set, to answer it;
    /// false lets the host's window procedure handle it. This is the parent half of hosting any
    /// window class: a month calendar's MCN_SELECT, a date picker's DTN_DATETIMECHANGE or the
    /// colors an EDIT asks for, none of which the element could see otherwise. EN_CHANGE still
    /// reaches on_native_content_changed() when this declines it.
    virtual bool handle_native_notification(NativeNotification& /*notification*/)
    {
        return false;
    }

    /// The fixed size measure() reports. Changing it measures the control again, for a control
    /// that learns the size it wants from the native window once it exists (a month calendar's
    /// MCM_GETMINREQRECT, read in on_native_created()).
    [[nodiscard]] Size native_size() const
    {
        return size_;
    }
    /// Sets the desired size (DIPs) and marks layout dirty when it changed.
    void set_native_size(Size size);

    /// The part of the control its scrolling ancestors leave in view, in arranged coordinates:
    /// empty when it is Collapsed, under a Collapsed ancestor or scrolled wholly out of view.
    /// What sync_native() shows and clips the native window to.
    [[nodiscard]] Rect visible_rect() const;

    // ---- automation ----
    /// True: the framework's automation tree leaves the control out, because the child HWND
    /// provides its own accessibility once created.
    [[nodiscard]] bool automation_is_native_host() const override
    {
        return true;
    }

    /// The raw platform handle (HWND on Windows), for a derived control to send it
    /// control-specific messages. nullptr before the first sync_native() call, and after the
    /// window that held it was closed.
    [[nodiscard]] NativeHandle native_handle() const
    {
        return host_ ? host_->native_handle(visual_id()) : nullptr;
    }

protected:
    /// Called once, from the sync_native() that created the real window, after it has been
    /// registered with the host and moved to its first bounds. `handle` is native_handle()'s
    /// value and is never null here. Not called if creation failed.
    ///
    /// This is where a derived control sends the one-time messages its window class needs to
    /// be configured with. A NativeControl's window is created lazily on the first sync, so the
    /// handle does not exist yet in the constructor.
    ///
    /// It is also the place to read a size the native control wants (a month calendar's
    /// MCM_GETMINREQRECT) and hand it to set_native_size().
    virtual void on_native_created(NativeHandle /*handle*/)
    {
    }

    /// Always native_size(), whatever space is available.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Emits nothing: the native window paints itself on top of the framework's surface.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    friend class NativeHost; // notify_host_lost()

    // The host most recently passed to sync_native(), which native_handle() asks. Non-owning; a
    // host that goes first says so through NativeHost::notify_host_lost().
    NativeHost* host_ = nullptr;

    // A NativeHost that is going away while this control lives on: forget it and the native
    // window, and get a new one from the next sync_native(), when the window is shown again.
    void host_lost() noexcept
    {
        host_    = nullptr;
        created_ = false;
        last_clip_.reset();
    }

    NativeControlDesc   desc_;
    Size                size_;
    bool                created_ = false;
    Rect                last_synced_bounds_{};
    std::optional<Rect> last_clip_; // what set_clip() last sent (nullopt: unclipped)
};

/// A real Win32 child window hosted in the tree. Builds a ui::NativeControl.
///
/// Use `.control` to reach the built control and, through NativeControl::native_handle(), its
/// HWND. The window is created when the tree is attached to a real window host.
///
/// Example:
/// @code
/// ui::Control<ui::Native> edit;
///
/// window.set_content(ui::Native{
///     .desc    = { .class_name = "EDIT", .initial_text = "Hello", .style = 0x0080 /* ES_AUTOHSCROLL */ },
///     .native_size = { 200.0f, 24.0f },
///     .control = edit,
/// });
/// @endcode
struct Native
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Native>` points at.
    using control_type = NativeControl;

    // ---- properties ----
    /// The window class, initial text and WS_* / WS_EX_* style bits to create it with.
    NativeControlDesc desc{};
    /// The fixed size the control reports (NativeControl::set_native_size()). Native controls
    /// take no part in text measurement, so set their size here.
    Binding<Size> native_size{};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<Native> control{}; ///< Out: the handle that receives the built control, the way to its HWND.

    /// Consumes the spec and returns the built ui::NativeControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
