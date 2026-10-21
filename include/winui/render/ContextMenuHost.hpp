// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"

#include <optional>
#include <string>
#include <vector>

#include <cstddef>

namespace ui
{

/// One entry of a system context menu shown through ContextMenuHost::show(): a text item or a
/// separator. The menu is flat, with no submenus, icons or check marks.
///
/// Example:
/// @code
/// const std::vector<ui::ContextMenuItem> items{
///     {"receive"},
///     {"recipe"},
///     ui::ContextMenuItem::separator(),
///     {"Add to dictionary"},
///     {"Paste", /*enabled=*/false},
/// };
/// @endcode
struct ContextMenuItem
{
    std::string text;                 ///< UTF-8 label; ignored for a separator.
    bool        enabled      = true;  ///< A disabled item shows grayed and can't be picked.
    bool        is_separator = false; ///< A divider line instead of an item.

    /// A separator entry (disabled, no text).
    [[nodiscard]] static ContextMenuItem separator()
    {
        return ContextMenuItem{{}, false, true};
    }

    friend bool operator==(const ContextMenuItem&, const ContextMenuItem&) = default;
};

/// Shows a context menu drawn by the system on a control's behalf.
///
/// Call it from Element::handle_context_menu(). The library implements it, and a test installs
/// TestContextMenuHost.
///
/// A control doesn't draw its right-click menu itself. It builds a flat item list and calls
/// show(), and the window shows it as a system menu, with the system's theming, keyboard
/// navigation and screen-reader support. Window passes the host into
/// Element::handle_context_menu(), the only moment a control may show a menu, so don't keep it.
///
/// Example:
/// @code
/// class MyEditor : public ui::Element
/// {
/// public:
///     bool handle_context_menu(std::optional<ui::Point> position, ui::ContextMenuHost& host) override
///     {
///         const ui::Point at = position.value_or(caret_point()); // keyboard request: own anchor
///         const std::vector<ui::ContextMenuItem> items{
///             {"Cut"}, {"Copy"}, ui::ContextMenuItem::separator(), {"Paste"}};
///         if (const std::optional<std::size_t> picked = host.show(at, items))
///         {
///             run_command(*picked); // 0 = Cut, 1 = Copy, 3 = Paste
///         }
///         return true; // a menu was shown, even if dismissed
///     }
///
/// private:
///     ui::Point caret_point() const;
///     void      run_command(std::size_t index);
/// };
/// @endcode
class ContextMenuHost
{
public:
    virtual ~ContextMenuHost() = default;

    /// Shows `items` as a popup menu whose top-left corner sits at `position` (window-client
    /// DIPs, the same space Element::bounds() uses), blocks until the user picks an item or
    /// dismisses the menu, and returns the picked item's index into `items`, or nullopt on
    /// dismissal. Separators and disabled items are never returned.
    [[nodiscard]] virtual std::optional<std::size_t> show(Point position, const std::vector<ContextMenuItem>& items) = 0;
};

} // namespace ui
