// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "winui/core/NativeHandle.hpp"
#include "winui/render/NativeHost.hpp"

#include <optional>
#include <unordered_map>

namespace ui::win32
{

/// Every native control becomes a real WS_CHILD HWND parented to `parent`, tracked by the
/// element's VisualId. Bounds are authored in DIPs like every other coordinate in the
/// framework; this converts to device pixels using `dpi` the same way Win32WindowHost.cpp's
/// own HostState::to_dips does for pointer coordinates.
///
/// ui::run() creates one per window and installs it with Window::set_native_host(); a
/// NativeControl (native_controls::RichEditBox, native_controls::EditBox) then gets its HWND from it during layout.
/// Construct one yourself only in a custom host.
///
/// Example:
/// @code
/// ui::win32::Win32NativeHost native{hwnd, GetDpiForWindow(hwnd)};
/// testing::seams(window).set_native_host(&native); // non-owning; `native` must outlive the window's controls
///
/// // WM_DPICHANGED:
/// native.set_dpi(HIWORD(wparam));
/// // WM_CONTEXTMENU naming a child window:
/// if (ui::NativeControl* owner = native.owner_for(reinterpret_cast<ui::NativeHandle>(wparam)))
/// {
///     // route the menu to `owner`
/// }
/// @endcode
class Win32NativeHost final : public NativeHost
{
public:
    /// Creates child windows under `parent` (non-owning), converting DIPs to pixels at `dpi`.
    explicit Win32NativeHost(NativeHandle parent, unsigned dpi = 96) : parent_(parent), dpi_(dpi)
    {
    }
    /// Tells every control still registered that its host is gone: the app owns the Window and
    /// its tree, and they outlive a closed window's host.
    ~Win32NativeHost() override;

    /// CreateWindowExW a WS_CHILD of `desc.class_name` for `id`, once; later calls are no-ops.
    void ensure_created(VisualId id, const NativeControlDesc& desc) override;
    /// SetWindowPos to `bounds` (DIPs, converted to pixels at the current DPI).
    void set_bounds(VisualId id, const Rect& bounds) override;
    /// ShowWindow SW_SHOW / SW_HIDE; unknown ids are ignored.
    void set_visible(VisualId id, bool visible) override;
    /// DestroyWindow and forget `id`; unknown ids are ignored.
    void destroy(VisualId id) override;
    /// Clips the child to `visible` (DIPs) with a window region; nullopt removes the clip.
    void set_clip(VisualId id, const std::optional<Rect>& visible) override;
    /// set_bounds() and set_clip() in one move, so a scrolled child never flashes unclipped.
    void place(VisualId id, const Rect& bounds, const std::optional<Rect>& visible) override;
    /// Records the element owning `id`'s window, for owner_for().
    void set_owner(VisualId id, NativeControl* owner) override;
    /// The HWND created for `id`, as void*; nullptr if none.
    [[nodiscard]] NativeHandle native_handle(VisualId id) const override;

    /// The element that owns `hwnd`, or nullptr if it isn't one of this host's controls.
    /// The reverse of native_handle(), for the one direction the framework can't drive
    /// itself: a message the OS delivers naming the child window (Win32WindowHost.cpp's
    /// WM_CONTEXTMENU) has to find its way back to the Element that asked for it.
    [[nodiscard]] NativeControl* owner_for(NativeHandle hwnd) const;

    /// WM_DPICHANGED: later bounds convert at `dpi`. Existing children move on the next place().
    void set_dpi(unsigned dpi)
    {
        dpi_ = dpi;
    }

private:
    [[nodiscard]] int to_px(float dips) const;
    // SetWindowRgn for `visible` (nullopt: none) on a window whose top-left is at `origin_px` in
    // the parent's client area.
    void apply_region(NativeHandle hwnd, int origin_x_px, int origin_y_px, const std::optional<Rect>& visible, bool redraw) const;
    // Repaints the child at once, frame and children included.
    static void repaint(NativeHandle hwnd);

    NativeHandle                               parent_ = nullptr;
    unsigned                                   dpi_    = 96;
    std::unordered_map<VisualId, NativeHandle> controls_;
    // Keyed by HWND because every lookup starts from a handle the OS named in a message.
    // Kept in lockstep with controls_: destroy() erases from both.
    std::unordered_map<NativeHandle, NativeControl*> owners_;
};

} // namespace ui::win32

#endif // _WIN32
