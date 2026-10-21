// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ui
{

/// The shortcuts of a ui::Shortcuts, written as a braced list of ui::Shortcut. A shortcut owns its
/// handler, which may capture move-only state, so the list takes them by value.
class ShortcutList
{
public:
    /// No shortcuts.
    ShortcutList() = default;
    /// The shortcuts in order. Implicit, so `.items = {ui::Shortcut{...}, ...}` works.
    template <typename... S>
        requires(sizeof...(S) > 0 && (std::same_as<S, Shortcut> && ...))
    ShortcutList(S&&... items) // NOLINT(google-explicit-constructor)
    {
        entries.reserve(sizeof...(S));
        (entries.push_back(std::move(items)), ...);
    }
    /// Move-only, since a shortcut owns its handler.
    ShortcutList(const ShortcutList&)            = delete;
    ShortcutList& operator=(const ShortcutList&) = delete;
    ShortcutList(ShortcutList&&)                 = default;
    ShortcutList& operator=(ShortcutList&&)      = default;

    /// The shortcuts, in order.
    std::vector<Shortcut> entries;

    /// Appends one shortcut, for a list put together in a loop.
    void add(Shortcut shortcut)
    {
        entries.push_back(std::move(shortcut));
    }
};

/// Keyboard shortcuts for one area of a window. Builds a ui::ContentHost around `.content`.
///
/// Each shortcut fires while keyboard focus is anywhere inside `.content`, and only then. The
/// focused control gets the key first, then the shortcuts of the innermost area, then those of the
/// areas around it.
/// For a shortcut that fires wherever focus is, give a ui::Command its `.shortcut` and register it
/// with Window::add_command(), or offer it through a button or menu item.
///
/// Example:
/// @code
/// ui::Property<std::string> message;
/// window.set_content(ui::Shortcuts{
///     .items =
///         {
///             ui::Shortcut{.keys = ui::Ctrl + ui::Key::Enter, .on_pressed = [&] { std::print("{}", message.get()); }},
///             ui::Shortcut{.keys = ui::Key::Escape, .on_pressed = [&] { message = ""; }},
///         },
///     .content = ui::TextBox{.header = "Message", .text = message, .accepts_return = true},
/// });
/// @endcode
struct Shortcuts
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Shortcuts>` points at.
    using control_type = ContentHost;

    // ---- properties ----
    /// The shortcuts. One that returns ui::NotHandled lets the key go on to the shortcuts
    /// further out.
    ShortcutList items{};

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
    ControlOut<Shortcuts> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The area the shortcuts belong to.
    Spec content{};

    /// Consumes the spec and returns the built ui::ContentHost; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
