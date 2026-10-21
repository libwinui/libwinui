// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/core/NativeHandle.hpp"
#include "winui/core/VisualId.hpp"

#include <optional>
#include <string>
#include <unordered_map>

#include <cstdint>

namespace ui
{

class NativeControl;
class SpellChecker;

/// The Win32 child window a NativeControl creates: a window class name, its initial text and
/// style bits. Windows only. Without a native host (a headless window, another platform) nothing
/// is created, and the control keeps its size and draws nothing. The style fields are plain
/// integers so this header compiles on any platform. On Windows, pass WS_*, WS_EX_* and control
/// style values as they are.
///
/// Example:
/// @code
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// ui::NativeControlDesc desc{
///     .class_name   = "EDIT",
///     .initial_text = "hello",
///     .style        = 0x0004 /*ES_MULTILINE*/ | 0x00200000 /*WS_VSCROLL*/,
/// };
/// stack->add(std::make_unique<ui::NativeControl>(std::move(desc), ui::Size{200.0f, 80.0f}));
/// @endcode
struct NativeControlDesc
{
    std::string   class_name;     ///< Registered window class, e.g. "RICHEDIT50W" or "EDIT".
    std::string   initial_text{}; ///< The window text at creation (initial content).
    std::uint32_t style    = 0;   ///< Extra WS_* / control style bits (the host adds WS_CHILD).
    std::uint32_t ex_style = 0;   ///< WS_EX_* bits.
};

/// A Win32 message a hosted window sent its parent about itself (WM_COMMAND, WM_NOTIFY,
/// WM_CTLCOLOR* and a scroll bar's or trackbar's WM_HSCROLL/WM_VSCROLL), handed to the element
/// that owns the window by NativeControl::handle_native_notification(). Windows only, like
/// NativeControlDesc, and plain integers for the same reason: `wparam` and `lparam` are the
/// message's own, and `result` is what the parent's window procedure returns when the element
/// handled it (a brush for WM_CTLCOLOR*, a notification's return value).
///
/// Example:
/// @code
/// class MyTrackbar : public ui::NativeControl
/// {
/// public:
///     bool handle_native_notification(ui::NativeNotification& n) override
///     {
///         if (n.message != 0x0114 /*WM_HSCROLL*/)
///         {
///             return false;
///         }
///         position_changed(read_position());
///         n.result = 0;
///         return true;
///     }
///
/// private:
///     int  read_position() const;
///     void position_changed(int position);
/// };
/// @endcode
struct NativeNotification
{
    std::uint32_t  message = 0; ///< The message id (WM_COMMAND, WM_NOTIFY, ...).
    std::uintptr_t wparam  = 0; ///< The message's WPARAM.
    std::intptr_t  lparam  = 0; ///< The message's LPARAM.
    std::intptr_t  result  = 0; ///< What the parent returns when the element handled it.
};

/// The interface between a NativeControl, a leaf wrapping a real Win32 child window such as
/// RichEdit, a shell control or WebView2, and whatever owns the real HWND.
///
/// Call it from a NativeControl. The library implements it, and a test installs TestNativeHost.
///
/// Every other control draws itself without a window of its own. NativeControl hosts what
/// libwinui doesn't reimplement. The control never calls Win32 itself. It calls this interface
/// from Element::sync_native(), keyed by its VisualId, and the host creates, moves, clips and
/// hides the child window. The window installs its host with Window::set_native_host()
/// (non-owning). The host must outlive every NativeControl synced against it, since a
/// NativeControl's destructor calls back into it.
///
/// Example:
/// @code
/// // An app never calls a NativeHost. It puts a NativeControl in the tree, and the window's host
/// // creates the child window, places it and keeps it in step with the element:
/// auto edit = std::make_unique<ui::NativeControl>(
///     ui::NativeControlDesc{.class_name = "EDIT", .initial_text = "hello"}, ui::Size{100, 20});
/// window.set_content(std::move(edit));
/// @endcode
class NativeHost
{
public:
    virtual ~NativeHost() = default;

    /// Creates the native control for `id` if it doesn't exist yet, and does nothing if it does.
    /// A NativeControl calls this once per sync_native(), whether or not it already has a live
    /// handle.
    virtual void ensure_created(VisualId id, const NativeControlDesc& desc) = 0;

    /// Moves and resizes the native control to `bounds`, in DIPs like every other Element. The
    /// host converts to device pixels.
    virtual void set_bounds(VisualId id, const Rect& bounds) = 0;

    /// Shows or hides the control without destroying its window, for example while it is
    /// inactive. Does nothing if `id` has no created control yet.
    virtual void set_visible(VisualId id, bool visible) = 0;

    /// Destroys the native control. Does nothing if `id` has none.
    virtual void destroy(VisualId id) = 0;

    /// Cuts the native control down to `visible` (DIPs, the same space as set_bounds()), or shows
    /// all of it for nullopt. A child window ignores the clips the rest of the window is drawn
    /// with, so a control half scrolled out of a Scroll would otherwise paint over whatever sits
    /// outside the viewport. NativeControl::sync_native() sends the part its scrolling ancestors
    /// leave in view, through place() whenever the control also moved, so a host may keep the
    /// region relative to the window. The default does nothing, for a host with nothing to cut.
    virtual void set_clip(VisualId /*id*/, const std::optional<Rect>& /*visible*/)
    {
    }

    /// Moves the control and clips it in one step, so a host that repaints on a move paints the
    /// new position with the new clip, not the one left over from the old position.
    /// NativeControl::sync_native() calls this whenever the bounds changed; the default is
    /// set_bounds() then set_clip().
    virtual void place(VisualId id, const Rect& bounds, const std::optional<Rect>& visible)
    {
        set_bounds(id, bounds);
        set_clip(id, visible);
    }

    /// The raw platform handle (an HWND on Windows), for control-specific messages a generic
    /// NativeControl can't express, such as WM_SETTEXT or WM_GETTEXT to a RichEdit.
    /// nullptr if `id` has no created control yet.
    [[nodiscard]] virtual NativeHandle native_handle(VisualId id) const = 0;

    /// Publishes the window's spell checker, or null when the window has none.
    /// Window::sync_native() calls it, and NativeControl::sync_native() hands the checker to the
    /// control through Element::sync_spell_checker().
    ///
    /// Other controls get the checker when they take focus. A NativeControl never becomes the
    /// window's focused element, because keyboard focus inside it belongs to the child window,
    /// so it gets the checker through this sync pass instead.
    void set_spell_checker(SpellChecker* checker)
    {
        spell_checker_ = checker;
    }
    /// The checker set_spell_checker() last published, or null. Non-owning.
    [[nodiscard]] SpellChecker* spell_checker() const
    {
        return spell_checker_;
    }

    // ---- sync passes ----
    /// Window::sync_native() brackets its tree walk with these. Every NativeControl the walk
    /// reaches reports through present() whether it should show. end_sync() then hides every
    /// control the walk didn't reach, such as one on a Tabs page or NavigationShell entry that
    /// isn't selected, and present() shows it again the next time a pass reaches it. Only
    /// end_sync() hides anything, so a host whose controls are synced directly never hides one
    /// this way. This one starts a pass.
    void begin_sync()
    {
        ++pass_;
    }
    /// Ends a pass: hides (set_visible(false)) every shown control present() wasn't called
    /// for since begin_sync().
    void end_sync()
    {
        for (auto& [id, presence] : presence_)
        {
            if (presence.pass != pass_ && presence.shown)
            {
                presence.shown = false;
                set_visible(id, false);
            }
        }
    }
    /// Called by NativeControl::sync_native() on every sync: `visible` is whether any of the
    /// control shows. Calls set_visible() only on a change; a control starts out shown.
    void present(VisualId id, bool visible)
    {
        Presence& presence = presence_[id];
        presence.pass      = pass_;
        if (presence.shown != visible)
        {
            presence.shown = visible;
            set_visible(id, visible);
        }
    }
    /// Whether present()/end_sync() last left the control shown (true for one never presented).
    [[nodiscard]] bool presented(VisualId id) const
    {
        const auto it = presence_.find(id);
        return it == presence_.end() || it->second.shown;
    }
    /// Drops the control's presence record; NativeControl's destructor calls it before destroy().
    void forget(VisualId id)
    {
        presence_.erase(id);
    }

protected:
    /// Tells `control`, a NativeControl registered through set_owner(), that this host is going
    /// away while it lives on (its window was closed; the app still owns the Window and its
    /// tree). The control forgets the host and its native window, and gets a new one from the
    /// next sync_native(). A host calls it from its destructor for every owner still registered.
    static void notify_host_lost(NativeControl& control) noexcept;

    /// Records which NativeControl owns `id`, so a host that receives platform events addressed
    /// to the child window can route them back to its element. On Windows that is a child
    /// window's WM_CONTEXTMENU: a right-click inside a native control goes to the child window
    /// and never reaches Window::handle_context_menu(), so the host looks the owner up by its
    /// handle and calls Element::handle_context_menu() on it directly.
    ///
    /// NativeControl::sync_native() calls it right after ensure_created(), so the owner is
    /// registered for exactly as long as the control exists. destroy() drops it again, and
    /// NativeControl is non-movable, so the pointer can't go stale. The default does nothing,
    /// for a host with no platform events to route. A host implements it, and only NativeControl
    /// calls it.
    virtual void set_owner(VisualId /*id*/, NativeControl* /*owner*/)
    {
    }

private:
    friend class NativeControl; // calls set_owner()

    struct Presence
    {
        std::uint64_t pass  = 0;
        bool          shown = true; // created visible (WS_VISIBLE on Win32)
    };

    SpellChecker*                          spell_checker_ = nullptr; // non-owning; outlives the host in practice
    std::uint64_t                          pass_          = 0;
    std::unordered_map<VisualId, Presence> presence_;
};

} // namespace ui
