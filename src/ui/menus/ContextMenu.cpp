// SPDX-License-Identifier: MIT
#include "winui/ui/menus/ContextMenu.hpp"

#include "ContextMenuPopup.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace ui
{

ContextMenuControl::ContextMenuControl() : menu_(std::make_unique<MenuFlyoutControl>())
{
    set_parent_of(*menu_, this);
}

ContextMenuControl::~ContextMenuControl() = default;

const std::vector<MenuItem>& ContextMenuControl::items() const
{
    return menu_->items();
}

void ContextMenuControl::set_items(std::vector<MenuItem> items)
{
    menu_->set_items(std::move(items));
}

void ContextMenuControl::show_at(Point point)
{
    if (!menu_->items().empty())
    {
        menu_->show_at(point);
    }
}

bool ContextMenuControl::is_open() const
{
    return menu_->is_open();
}

void ContextMenuControl::hide()
{
    menu_->hide();
}

void ContextMenuControl::for_each_child(ChildVisitor visit) const
{
    ContentHost::for_each_child(visit);
    if (detail::TreeAccess::popup_request(*menu_) != nullptr)
    {
        visit(*menu_);
    }
}

void ContextMenuControl::automation_children(std::vector<Element*>& out) const
{
    ContentHost::for_each_child([&out](Element& child) { out.push_back(&child); });
    if (detail::TreeAccess::popup_request(*menu_) != nullptr)
    {
        out.push_back(menu_.get()); // the open menu
    }
}

void ContextMenuControl::clear_dirty_override(DirtyFlags flags)
{
    clear_dirty_of(*menu_, flags);
}

bool ContextMenuControl::raise_context_requested(std::optional<Point> position)
{
    if (Element::raise_context_requested(position))
    {
        return true;
    }
    if (menu_->items().empty())
    {
        return false;
    }
    menu_->show_at(position.value_or(detail::keyboard_menu_point(*this)));
    return true;
}

std::unique_ptr<ui::Element> ContextMenu::build() &&
{
    auto node = std::make_unique<ui::ContextMenuControl>();
    apply(items, *node, [raw = node.get()](const std::vector<MenuItem>& v) { raw->set_items(v); });
    if (content)
    {
        node->set_child(content.take());
    }
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
