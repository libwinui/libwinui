// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/menus/AppBarButton.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// The mini-toolbar of an in-app object: a small row of icon-only commands in a flyout, with
/// a menu of more commands under it behind a "More" button.
///
/// Like every FlyoutBase, it sits in the tree wrapping its anchor (set_anchor()) and shows its
/// content as a popup. Primary commands (app-bar buttons) form a row of 40x40 icon buttons, and
/// secondary commands form the menu. The show mode decides how it opens. Transient, for a click,
/// starts with the menu collapsed and takes no focus. Standard, for a context menu, starts
/// expanded and focuses the first command. Clicking a secondary command closes the flyout, and
/// clicking a primary one collapses the menu. The menu always opens downward, and primary commands
/// never move into it.
///
/// Example:
/// @code
/// auto flyout = std::make_unique<ui::CommandBarFlyoutControl>();
/// flyout->set_placement(ui::PopupPlacement::Right);
/// flyout->set_show_mode(ui::FlyoutShowMode::Transient);
/// flyout->add_primary(std::make_unique<ui::AppBarButtonControl>("Share", ui::Symbol::Share));
/// flyout->add_primary(std::make_unique<ui::AppBarButtonControl>("Delete", ui::Symbol::Delete));
/// auto* resize = flyout->add_secondary(std::make_unique<ui::AppBarButtonControl>("Resize"));
/// resize->on_click([] { /* resize the photo */ });
/// flyout->set_anchor(std::make_unique<ui::ButtonControl>("Photo")); // a Button anchor toggles it
/// window.set_content(std::move(flyout));
///
/// // Declaratively; open it through `.control`:
/// ui::Control<ui::CommandBarFlyout> photo_menu;
/// window.set_content(ui::CommandBarFlyout{
///     .placement = ui::PopupPlacement::Right,
///     .show_mode = ui::FlyoutShowMode::Transient,
///     .control   = photo_menu,
///     .anchor    = ui::Button{.text = "Photo", .on_click = [&] { photo_menu->show(); }},
///     .primary   = {ui::AppBarButton{.text = "Share", .icon = ui::Symbol::Share},
///                   ui::AppBarButton{.text = "Delete", .icon = ui::Symbol::Delete}},
///     .secondary = {ui::AppBarButton{.text = "Resize"}},
/// });
/// @endcode
class CommandBarFlyoutControl final : public FlyoutBase, public AppBarCommandHost
{
public:
    /// An empty flyout with no anchor.
    CommandBarFlyoutControl();
    /// Destroys every command and the presenter.
    ~CommandBarFlyoutControl() override;

    /// Appends `command` to the icon row. Takes ownership and hands back a
    /// non-owning observer (the same convention as StackControl::add()).
    template <typename T>
    T* add_primary(std::unique_ptr<T> command)
    {
        static_assert(std::is_base_of_v<Element, T>, "CommandBarFlyout commands must be Elements");
        T* observer = command.get();
        add_command(primary_, std::move(command), /*primary=*/true);
        return observer;
    }
    /// Appends `command` to the menu. Takes ownership and hands back a
    /// non-owning observer.
    template <typename T>
    T* add_secondary(std::unique_ptr<T> command)
    {
        static_assert(std::is_base_of_v<Element, T>, "CommandBarFlyout commands must be Elements");
        T* observer = command.get();
        add_command(secondary_, std::move(command), /*primary=*/false);
        return observer;
    }
    /// The number of primary commands.
    [[nodiscard]] std::size_t primary_count() const
    {
        return primary_.size();
    }
    /// The number of secondary commands.
    [[nodiscard]] std::size_t secondary_count() const
    {
        return secondary_.size();
    }
    /// The primary command at `index` as a non-owning observer, or nullptr when out of range.
    [[nodiscard]] Element* primary(std::size_t index) const
    {
        return index < primary_.size() ? primary_[index].get() : nullptr;
    }
    /// The secondary command at `index` as a non-owning observer, or nullptr when out of range.
    [[nodiscard]] Element* secondary(std::size_t index) const
    {
        return index < secondary_.size() ? secondary_[index].get() : nullptr;
    }

    /// Whether the menu of secondary commands stays open, with no More button.
    /// False by default.
    [[nodiscard]] bool always_expanded() const
    {
        return always_expanded_;
    }
    /// Takes effect the next time the flyout opens.
    void set_always_expanded(bool always_expanded)
    {
        always_expanded_ = always_expanded;
    }

    /// Whether the secondary menu shows. Every open resets it from the show mode: expanded for
    /// Standard when there are secondary commands, collapsed for Transient.
    [[nodiscard]] bool is_expanded() const
    {
        return expanded_;
    }
    /// Expands or collapses the menu with a slide, as the More button does. Ignored while
    /// always_expanded(), so the menu stays expanded.
    void set_expanded(bool expanded);

    /// Called when a command is clicked. A secondary command hides the flyout, and a primary one
    /// collapses the menu.
    void command_invoked(CommandBarElement& command) override;
    /// The shortcuts of the secondary commands and of the anchor fire app-wide.
    bool handle_global_shortcut(const KeyEvent& e) override;


protected:
    /// Runs the open/close fade and the menu's slide.
    void tick_override(Duration delta) override;


private:
    friend class CommandBarFlyoutPresenter;

    // The presenter (row, More button and menu), built on first use and owned by the flyout.
    [[nodiscard]] Element* flyout_surface() override;
    // The shadow follows the visible surface: the row, plus the menu as far as it has slid out.
    void flyout_chrome(PopupChrome& chrome) override;
    friend class CommandBarFlyoutMoreButton;

    std::vector<std::unique_ptr<Element>> primary_;
    std::vector<std::unique_ptr<Element>> secondary_;
    std::unique_ptr<Element>              presenter_;
    std::unique_ptr<Element>              more_;
    bool                                  always_expanded_ = false;
    bool                                  expanded_        = false;
    double                                expand_ms_       = 0.0;
    bool                                  expanding_       = false;
    bool                                  collapsing_      = false;

    void add_command(std::vector<std::unique_ptr<Element>>& list, std::unique_ptr<Element> command, bool primary);
    [[nodiscard]] float menu_reveal() const;
    void                opening_override() override;
};

/// A small bar of commands in a flyout, with a menu of more under it. Builds a
/// ui::CommandBarFlyoutControl. To open it, call `show()` on its `.control` handle from the anchor's
/// handlers. Use `show_mode` Transient for a click and Standard for a context menu.
///
/// Example:
/// @code
/// ui::Control<ui::CommandBarFlyout> flyout;
///
/// window.set_content(ui::CommandBarFlyout{
///     .placement = ui::PopupPlacement::Right,
///     .show_mode = ui::FlyoutShowMode::Transient,
///     .control   = flyout,
///     .anchor    = ui::Button{ .text = "Photo", .on_click = [&] { flyout->show(); } },
///     .primary   = { ui::AppBarButton{ .text = "Share", .icon = ui::Symbol::Share },
///                    ui::AppBarButton{ .text = "Save", .icon = ui::Symbol::Save },
///                    ui::AppBarButton{ .text = "Delete", .icon = ui::Symbol::Delete } },
///     .secondary = { ui::AppBarButton{ .text = "Resize" } },
/// });
/// @endcode
struct CommandBarFlyout
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<CommandBarFlyout>` points at.
    using control_type = CommandBarFlyoutControl;

    // ---- properties ----
    /// Whether the flyout is showing. Two-way with ui::bind(): closing it writes false, and a
    /// false that `on_closing` cancels is put back to true.
    TwoWayField<bool> is_open{false};
    /// Which side of the anchor it opens on.
    Binding<PopupPlacement> placement{PopupPlacement::Above};
    /// How the flyout lines up with the anchor along that side.
    Binding<PopupPlacementAlignment> placement_alignment{PopupPlacementAlignment::Center};
    /// The secondary menu shows whenever the flyout does.
    Binding<bool> always_expanded{false};
    /// Transient, for a click, opens with the menu collapsed and leaves focus alone. Standard, for
    /// a context menu, opens it expanded and takes focus.
    Binding<FlyoutShowMode> show_mode{FlyoutShowMode::Standard};
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
    /// Out: the handle that receives the built control, through which to show() it.
    ControlOut<CommandBarFlyout> control{};

    // ---- children ----
    /// The in-tree element the flyout hangs from.
    Spec anchor{};
    /// The icon row, as app-bar specs.
    SpecList primary{};
    /// The menu under the row, as app-bar specs.
    SpecList secondary{};

    /// Consumes the spec and returns the built ui::CommandBarFlyoutControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
