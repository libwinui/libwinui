// SPDX-License-Identifier: MIT
#include "winui/ui/menus/MenuItem.hpp"

#include "winui/core/Input.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/ui/Command.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ui
{

MenuItem MenuItem::toggle(std::string text, Binding<bool> checked, std::function<void(bool)> on_checked_changed)
{
    MenuItem item;
    item.text               = std::move(text);
    item.kind               = MenuItemKind::Toggle;
    item.checked            = std::move(checked);
    item.on_checked_changed = std::move(on_checked_changed);
    return item;
}

MenuItem MenuItem::radio(std::string text, Binding<bool> checked, std::function<void(bool)> on_checked_changed)
{
    MenuItem item;
    item.text               = std::move(text);
    item.kind               = MenuItemKind::Radio;
    item.checked            = std::move(checked);
    item.on_checked_changed = std::move(on_checked_changed);
    return item;
}

MenuItem MenuItem::separator()
{
    MenuItem item;
    item.kind = MenuItemKind::Separator;
    return item;
}

MenuItem MenuItem::sub_item(std::string text, std::vector<MenuItem> items)
{
    MenuItem item;
    item.text  = std::move(text);
    item.kind  = MenuItemKind::SubItem;
    item.items = std::move(items);
    return item;
}

MenuItem MenuItem::split(std::string text, std::function<void()> on_click, std::vector<MenuItem> items)
{
    MenuItem item;
    item.text     = std::move(text);
    item.on_click = std::move(on_click);
    item.kind     = MenuItemKind::Split;
    item.items    = std::move(items);
    return item;
}

MenuItem MenuItem::from_command(const Command& command, std::string parameter)
{
    MenuItem item;
    item.command           = command;
    item.command_parameter = std::move(parameter);
    return item;
}

std::string MenuItem::display_text() const
{
    // The command's label fills an empty text.
    const std::string& own = text.initial();
    if (own.empty() && command != nullptr)
    {
        return command->current().text;
    }
    return own;
}

const IconSource& MenuItem::display_icon() const
{
    static const IconSource NoIcon;
    if (!icon.initial().empty())
    {
        return icon.initial();
    }
    return command != nullptr ? command->current().icon : NoIcon;
}

KeyCombo MenuItem::display_shortcut() const
{
    if (!shortcut.initial().empty())
    {
        return shortcut.initial();
    }
    return command != nullptr ? command->current().shortcut : KeyCombo{};
}

std::string MenuItem::display_shortcut_text() const
{
    return shortcut_text.empty() ? display_shortcut().text() : shortcut_text;
}

bool MenuItem::is_enabled() const
{
    return enabled.initial() && (command == nullptr || command->current().enabled);
}

} // namespace ui
