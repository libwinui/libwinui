// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"

#include <functional>
#include <vector>

namespace ui
{

// What MenuFlyoutControl, MenuBarControl and TrayIcon share to run an item (defined in MenuFlyout.cpp).

namespace detail
{
// The subscriptions that keep one owner's items in step with the properties their fields are
// bound to. The owner holds it by unique_ptr, so a moved owner only has to replace `changed`.
struct MenuChecks
{
    Callback<void()>          changed;       // after a bound property changed an item; may be empty
    std::vector<Subscription> subscriptions; // one per bound field
};
} // namespace detail

// Follows every bound field of every item in `items` (text, icon, shortcut, enabled and
// checked), submenus included, through subscriptions added to `checks`. Each such field becomes a
// constant holding the current value; `checked` keeps the binding's writer and group, so a click
// can change the state and still write back. The items must stay
// where they are while `checks` holds the subscriptions: clear them before replacing the items.
void follow_menu_bindings(std::vector<MenuItem>& items, detail::MenuChecks& checks);

// Invokes `item` as a click would, within the menu tree `root`: a toggle flips, a radio item
// checks and unchecks its group-mates in `root`, then Click and the command run. `before_run`
// is called once the state has changed and before anything else runs; the menu closes there.
// Then every changed item's two-way `checked` is written, then its on_checked_changed runs.
// Everything the handlers need is copied first, so a handler that replaces the items is safe.
void invoke_menu_item(std::vector<MenuItem>& root, MenuItem& item, const std::function<void()>& before_run);

// Finds the item in `root` whose shortcut is `e`, enabled along with every submenu it is in.
[[nodiscard]] MenuItem* find_menu_shortcut(std::vector<MenuItem>& root, const KeyEvent& e);

} // namespace ui
