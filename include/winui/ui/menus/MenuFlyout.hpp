// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/menus/MenuItem.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui
{

namespace detail
{
class MenuPresenter;
class MenuHost;
struct MenuChecks;
} // namespace detail


/// A list of commands in a popup: hung from an anchor element (a click on a
/// Button anchor toggles it) or opened at a point with show_at() for a context menu.
///
/// The flyout sits in the tree in its anchor's place and owns the anchor (set_anchor()); the
/// menu floats in the Window's overlay layer, cascades into submenus, and closes on invoke, light
/// dismiss or Escape. Placement and show()/hide() come from FlyoutBase. The declarative form is
/// ui::MenuFlyout.
///
/// Example:
/// @code
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto menu  = std::make_unique<ui::MenuFlyoutControl>();
/// ui::ButtonControl* options = menu->set_anchor(std::make_unique<ui::ButtonControl>("Options"));
/// menu->set_items({
///     {.text = "Open", .on_click = [&] { open_file(); }},
///     ui::MenuItem::sub_item("Send to", {{.text = "Bluetooth"}, {.text = "Desktop"}}),
///     ui::MenuItem::separator(),
///     ui::MenuItem::toggle("Repeat", /*checked=*/true),
/// });
/// ui::MenuFlyoutControl* flyout = stack->add(std::move(menu)); // the stack owns flyout and anchor
///
/// // Also a context menu: a right-click on the anchor opens it at the pointer.
/// options->on_context_requested([flyout, options](std::optional<ui::Point> at) {
///     flyout->show_at(at.value_or(options->bounds().center()));
/// });
///
/// // Or declaratively:
/// stack->add(ui::MenuFlyout{
///     .items   = {{.text = "Reset"}, ui::MenuItem::separator(),
///                 ui::MenuItem::toggle("Repeat", true)},
///     .anchor  = ui::Button{.text = "Options"},
/// }.build());
/// @endcode
class MenuFlyoutControl final : public FlyoutBase
{
public:
    /// An empty menu with no anchor.
    MenuFlyoutControl();
    ~MenuFlyoutControl() override;

    /// The items. Toggle and radio items' is_checked() track clicks made in the menu and the
    /// properties their `checked` is bound to.
    [[nodiscard]] const std::vector<MenuItem>& items() const
    {
        return items_;
    }
    /// Replaces the items; an open menu closes first. The menu redraws when the command of any
    /// item changes, submenus included, and follows every bound `checked` while it holds the items.
    void set_items(std::vector<MenuItem> items);

    /// The shortcut of an item, in the menu or in a submenu, invokes that item whether the menu is
    /// open or not.
    bool handle_global_shortcut(const KeyEvent& e) override;

    /// The menu's opacity during its open and close transitions. A menu that slides in
    /// (Theme::popup_motion) opens fully opaque, and one that fades in blends in. Closing fades it
    /// out over the theme's close duration.
    [[nodiscard]] float transition_opacity() const override;

protected:
    /// Picks up the theme's menu open and close durations, then advances the flyout's transition.
    void tick_override(Duration delta) override;


private:
    class Host;

    // The floating menu presenter, created on first use and owned by the flyout.
    [[nodiscard]] Element* flyout_surface() override;

    std::vector<MenuItem>                  items_;
    std::unique_ptr<Host>                  host_;
    std::unique_ptr<detail::MenuPresenter> presenter_;
    std::vector<Subscription>              command_changed_; // every item's command, submenus too
    std::unique_ptr<detail::MenuChecks>    checks_;          // every item's bound `checked`, submenus too

    void watch_commands();
    void opening_override() override;
    void closing_override() override;
};

/// A list of commands in a popup, hung from an anchor. Builds a ui::MenuFlyoutControl. A Button
/// anchor opens it on click. To use it as a context menu, call `show_at(point)` on its `.control`
/// handle from an `on_context_requested` handler. The built element takes the anchor's place in
/// the tree, and the menu floats in the window's popup layer.
///
/// Example:
/// @code
/// bool repeat = true;
/// window.set_content(ui::MenuFlyout{
///     .items   = { { .text = "Reset", .on_click = [&] { repeat = true; } },
///                  ui::MenuItem::separator(),
///                  ui::MenuItem::toggle("Repeat", true, [&](bool on) { repeat = on; }),
///                  ui::MenuItem::sub_item("Sort by", {
///                      ui::MenuItem::radio("Name", true),
///                      ui::MenuItem::radio("Date") }) },
///     .anchor    = ui::Button{ .text = "Options" },
/// });
/// @endcode
struct MenuFlyout
{
    /// The control this spec builds; what `ui::Control<MenuFlyout>` points at.
    using control_type = MenuFlyoutControl;

    // ---- properties ----
    /// The menu's entries: items, toggles, radio items, separators and submenus.
    Binding<std::vector<MenuItem>> items{};
    /// Whether the flyout is showing. Two-way with ui::bind(): closing it writes false, and a
    /// false that `on_closing` cancels is put back to true.
    TwoWayField<bool> is_open{false};
    /// Which side of the anchor it opens on, centered on the anchor; PopupPlacement::Below by
    /// default.
    Binding<PopupPlacement> placement{PopupPlacement::Below};
    /// How the menu lines up with the anchor along that side.
    Binding<PopupPlacementAlignment> placement_alignment{PopupPlacementAlignment::Center};
    /// Whether an outside click or Escape closes it; true by default. Off, it stays open until
    /// `is_open` turns false, hide() runs or a command in it runs.
    Binding<bool> light_dismiss{true};

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
    /// The flyout is showing. A two-way `is_open` has been written by then.
    Action on_opened{};
    /// Gets why it is about to close; return ui::Cancel to keep it open.
    Callback<Decision(const PopupClosingArgs&)> on_closing{};
    /// Gets why it closed, once the close fade has finished. A two-way `is_open` has been written
    /// by then.
    Callback<void(PopupCloseReason)> on_closed{};
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
    ControlOut<MenuFlyout> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The in-tree element the menu hangs from.
    Spec anchor{};

    /// Consumes the spec and returns the built ui::MenuFlyoutControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
