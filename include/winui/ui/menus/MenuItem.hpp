// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/ui/Command.hpp"

#include <functional>
#include <string>
#include <vector>

namespace ui
{

/// Which kind of menu item a MenuItem stands for.
enum class MenuItemKind
{
    Item,      ///< a plain command
    Toggle,    ///< a check item
    Radio,     ///< one of a group of exclusive items
    Separator, ///< a line between groups
    SubItem,   ///< `items` open as a submenu
    Split,     ///< invokes like an Item, and `items` open from its chevron
};

/// One entry of a menu. The plain case is `{.text = "Open", .on_click = [&] { open(); }}`; the
/// factories below spell out the other kinds. The fields follow the spec order: what the item
/// shows, then the command and the handlers, then the submenu.
///
/// `on_click` is a copyable std::function, since items live in a std::vector built from a brace
/// list, which only copies. A handler must not capture a move-only value.
///
/// Items are plain values: the menu that holds them (MenuFlyout, MenuBar, a context menu, the
/// window's system menu) keeps its own copy, so a toggle's or radio item's current state is read
/// back from that owner's items() with is_checked(), or followed through `on_checked_changed`.
/// `text`, `icon`, `shortcut`, `enabled` and `checked` take a value or a property: bound to a
/// property, every menu built from the item follows it, and a click writes a two-way `checked`.
///
/// Radio items form a group in one of two ways. Bound with `.checked = ui::selects(choice, value)`,
/// the items bound to the same property are the group, wherever they sit in the menu. Unbound,
/// a run of radio items next to each other in one list is a group, so a separator or any other
/// item between two runs starts a new one.
///
/// Example:
/// @code
/// enum class Size { Small, Large };
/// ui::Property<std::string> note;
/// ui::Property<bool>        word_wrap{true};
/// ui::Property<Size>        size{Size::Small};
///
/// std::vector<ui::MenuItem> items{
///     {.text = "Copy", .icon = ui::Symbol::Copy, .shortcut = ui::Ctrl + ui::Key::C,
///      .on_click = [&] { ui::clipboard().set_text(note.get()); }},
///     ui::MenuItem::separator(),
///     ui::MenuItem::toggle("Word wrap", ui::bind(word_wrap)),
///     ui::MenuItem::radio("Small", ui::selects(size, Size::Small)),
///     ui::MenuItem::radio("Large", ui::selects(size, Size::Large)),
///     ui::MenuItem::sub_item("Send to", {{.text = "Desktop"}, {.text = "Mail recipient"}}),
///     ui::MenuItem::split("Paste", [&] { note = ui::clipboard().text().value_or(""); }, {{.text = "Paste as text"}}),
/// };
/// auto flyout = std::make_unique<ui::MenuFlyoutControl>();
/// flyout->set_items(std::move(items)); // or a ui::MenuBarItem's items
/// @endcode
struct MenuItem
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The text. Falls back to the command's text.
    Binding<std::string> text{};

    /// Which kind of menu item this entry is; the factories below set it. Defaults to a plain Item.
    MenuItemKind kind = MenuItemKind::Item;

    /// The icon, drawn 16x16. Falls back to the command's icon.
    Binding<IconSource> icon{};

    /// The keyboard shortcut. It fires app-wide and invokes the item, and its text is
    /// shown in the shortcut column. Falls back to the command's.
    Binding<KeyCombo> shortcut{};

    /// Shown instead of the shortcut's own text.
    std::string shortcut_text{};

    /// A disabled item is drawn dimmed, skipped by the keyboard, and its shortcut does
    /// not fire; a disabled command disables it too (is_enabled()).
    Binding<bool> enabled{true};

    /// The checked state of a Toggle or Radio item. Two-way from a `ui::Property<bool>` or
    /// ui::selects(): the item follows the property in every menu built from it, and a click
    /// writes the new state back before `on_checked_changed` runs.
    Binding<bool> checked{false};

    /// The command, run after `on_click` with `command_parameter`. The command also supplies the text,
    /// icon and shortcut the item does not set, and disables the item while it is disabled.
    CommandRef command{};
    /// Passed to the command's execute.
    std::string command_parameter{};

    /// Runs on a click, after the menu has closed, and after a toggle or radio item has changed.
    std::function<void()> on_click{};

    /// Called when a Toggle or Radio item's `checked` changes through the menu (a click, a key,
    /// a shortcut, or a group-mate being checked), after a two-way `checked` has been written. A
    /// change of the bound property does not call it.
    std::function<void(bool checked)> on_checked_changed{};

    /// The submenu of a SubItem or Split item.
    std::vector<MenuItem> items{};

    /// A toggle item: a click flips `checked` (drawn as a check mark) and reports the new
    /// state to `on_checked_changed` before `on_click` runs.
    [[nodiscard]] static MenuItem toggle(std::string               text,
                                         Binding<bool>             checked            = false,
                                         std::function<void(bool)> on_checked_changed = {});
    /// A radio item: a click checks it and unchecks the rest of its group (see above); clicking a
    /// checked one leaves it checked. Each item unchecked this way writes false into its own
    /// two-way `checked`.
    [[nodiscard]] static MenuItem radio(std::string               text,
                                        Binding<bool>             checked            = false,
                                        std::function<void(bool)> on_checked_changed = {});
    /// A 1px line between groups of items; never focused or invoked.
    [[nodiscard]] static MenuItem separator();
    /// A submenu item: invoking it (click, Right, Enter, Space, or a 400 ms hover) opens
    /// `items` as a cascading submenu beside it. It has no `on_click` of its own.
    [[nodiscard]] static MenuItem sub_item(std::string text, std::vector<MenuItem> items);
    /// A split item: the item runs `on_click`, and the chevron button at its right edge
    /// opens `items` as a submenu.
    [[nodiscard]] static MenuItem split(std::string text, std::function<void()> on_click, std::vector<MenuItem> items);
    /// An item built entirely from `command`: its text, icon, shortcut and action.
    [[nodiscard]] static MenuItem from_command(const Command& command, std::string parameter = {});

    // ---- what the item shows, with the command filling in what the item leaves unset ----
    /// `text`, or the command's text when `text` is empty.
    [[nodiscard]] std::string display_text() const;
    /// `icon`, or the command's icon when `icon` is empty (an empty IconSource when neither is set).
    [[nodiscard]] const IconSource& display_icon() const;
    /// `shortcut`, or the command's shortcut when it is empty.
    [[nodiscard]] KeyCombo display_shortcut() const;
    /// What the shortcut column shows: `shortcut_text` if set, else display_shortcut()'s "Ctrl+S" text
    /// (empty when there is no shortcut).
    [[nodiscard]] std::string display_shortcut_text() const;
    /// The check state: `checked`'s value. In a menu's items() it is the current state, whether
    /// a click or the bound property set it last.
    [[nodiscard]] bool is_checked() const
    {
        return checked.initial();
    }
    /// Enabled, and its command (if any) is too.
    [[nodiscard]] bool is_enabled() const;
    /// Can hold focus in a menu: not a separator, and enabled.
    [[nodiscard]] bool focusable() const
    {
        return kind != MenuItemKind::Separator && is_enabled();
    }
    /// Opens a submenu (a SubItem, or a Split item with items).
    [[nodiscard]] bool has_submenu() const
    {
        return (kind == MenuItemKind::SubItem || kind == MenuItemKind::Split) && !items.empty();
    }
};

} // namespace ui
