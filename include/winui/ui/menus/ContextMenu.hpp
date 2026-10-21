// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ui
{

/// A child with a context menu. Built by ui::ContextMenu.
///
/// A right-click anywhere on the child, or Shift+F10 or the Menu key while focus is inside it,
/// opens the items as a menu: at the pointer, or for the keyboard over the focused element. A
/// context menu nested deeper wins over this one, and an on_context_requested() handler on an
/// element inside it runs first. With no items it opens nothing.
///
/// Example:
/// @code
/// auto  host  = std::make_unique<ui::ContextMenuControl>();
/// auto* label = host->set_child(std::make_unique<ui::TextControl>("Order 1042"));
/// host->set_items({
///     {.text = "Copy", .on_click = [label] { ui::clipboard().set_text(label->text()); }},
///     {.text = "Save as...", .on_click = [] { save(); }},
/// });
/// @endcode
class ContextMenuControl final : public ContentHost
{
public:
    /// No child and no items yet.
    ContextMenuControl();
    ~ContextMenuControl() override;

    /// The menu's entries.
    [[nodiscard]] const std::vector<MenuItem>& items() const;
    /// Replaces the entries; an open menu closes first.
    void set_items(std::vector<MenuItem> items);

    /// Opens the menu at `point` (window-client DIPs), as a right-click there does.
    void show_at(Point point);
    /// Whether the menu is open.
    [[nodiscard]] bool is_open() const;
    /// Closes the menu.
    void hide();
    /// The flyout that shows the menu, for its placement and its opened and closed events.
    [[nodiscard]] MenuFlyoutControl& menu() const
    {
        return *menu_;
    }

    /// The child, and the menu while it shows, so the window finds its popup.
    void for_each_child(ChildVisitor visit) const override;
    /// The child only: the menu is not part of the page.
    void automation_children(std::vector<Element*>& out) const override;
    /// Opens the menu after any on_context_requested() handler of this element declined.
    bool raise_context_requested(std::optional<Point> position) override;

protected:
    /// Clears the menu too, which is listed as a child only while it shows.
    void clear_dirty_override(DirtyFlags flags) override;


private:
    std::unique_ptr<MenuFlyoutControl> menu_;
};

/// A context menu around any piece of UI. Builds a ui::ContextMenuControl.
///
/// A right-click on `.content`, or Shift+F10 or the Menu key while focus is inside it, opens
/// `.items` as a menu at the pointer, or over the focused element for the keyboard. The content
/// does not need to be focusable: a right-click on a Text or an Image opens it too. For one menu
/// per row of a list use the view's `.item_context_menu` instead.
///
/// Example:
/// @code
/// ui::Property<std::string> note{"Buy milk"};
/// window.set_content(ui::ContextMenu{
///     .items =
///         {
///             {.text = "Copy", .icon = ui::Symbol::Copy, .on_click = [&] { ui::clipboard().set_text(note.get()); }},
///             ui::MenuItem::separator(),
///             {.text = "Delete", .icon = ui::Symbol::Delete, .on_click = [&] { note = ""; }},
///         },
///     .content = ui::Text{.text = note},
/// });
/// @endcode
struct ContextMenu
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ContextMenu>` points at.
    using control_type = ContextMenuControl;

    // ---- properties ----
    /// The menu's entries: items, toggles, radio items, separators and submenus.
    Binding<std::vector<MenuItem>> items{};

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
    ControlOut<ContextMenu> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// What the menu belongs to.
    Spec content{};

    /// Consumes the spec and returns the built ui::ContextMenuControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
