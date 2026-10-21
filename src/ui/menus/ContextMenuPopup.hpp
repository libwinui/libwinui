// SPDX-License-Identifier: MIT
// The menu a control opens on a context request: a MenuFlyoutControl with no anchor, owned by
// the control and listed among its children while it shows, so the window finds its popup.
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"

#include <memory>
#include <vector>

namespace ui::detail
{

// Opens `items` at `at` (window-client DIPs) in the menu kept in `slot`, made on first use and
// parented to `owner`. Nothing opens for an empty list.
inline void show_context_menu(std::unique_ptr<MenuFlyoutControl>& slot, Element& owner, std::vector<MenuItem> items, Point at)
{
    if (items.empty())
    {
        return;
    }
    if (!slot)
    {
        slot = std::make_unique<MenuFlyoutControl>();
        detail::TreeAccess::set_parent(*slot, &owner);
    }
    slot->set_items(std::move(items));
    slot->show_at(at);
}

// Whether the menu in `slot` is to be listed as a child: while it shows or fades out.
[[nodiscard]] inline bool context_menu_showing(const std::unique_ptr<MenuFlyoutControl>& slot)
{
    return slot && detail::TreeAccess::popup_request(*slot) != nullptr;
}

// Where a keyboard request opens a menu: over the innermost element inside `root` with focus, or
// over `root` itself.
[[nodiscard]] inline Point keyboard_menu_point(const Element& root)
{
    const Element* at = &root;
    for (bool descended = true; descended;)
    {
        descended = false;
        at->for_each_child(
            [&](Element& child)
            {
                if (!descended && child.has_focus_within())
                {
                    at        = &child;
                    descended = true;
                }
            });
    }
    return at->bounds().center();
}

} // namespace ui::detail
