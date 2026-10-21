// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ui
{

class CommandBarControl;

/// Where a CommandBar puts its buttons' text.
enum class CommandBarDefaultTextPosition
{
    Bottom,    ///< Under the icon, revealed when the bar opens; the default.
    Right,     ///< Beside the icon, always visible.
    Collapsed, ///< No text at all.
};

/// Where one button puts its text. Collapsed hides it wherever the bar puts text, Default
/// follows the bar.
enum class CommandBarTextPosition
{
    Default,   ///< Follow the bar's CommandBarDefaultTextPosition.
    Collapsed, ///< Never show this button's text.
};

/// How the CommandBar holding an app-bar element wants it drawn. A button on its own keeps the
/// default: full size, text at the bottom. The bar pushes one down with
/// CommandBarElement::set_app_bar_look() whenever its state changes; apps rarely build one.
struct AppBarLook
{
    bool                          compact          = false;                                 ///< the bar is closed
    CommandBarDefaultTextPosition text_position    = CommandBarDefaultTextPosition::Bottom; ///< the bar's text position
    bool                          overflow         = false;       ///< a row of the overflow menu
    bool                          overflow_icons   = false;       ///< some overflow item has an icon
    bool                          overflow_toggles = false;       ///< some overflow item is a toggle button
    float                         overflow_shortcut_width = 0.0f; ///< the widest shortcut text in the overflow
    bool                          flyout_primary = false; ///< a CommandBarFlyout's primary row: icon-only, 40x40
    /// With `overflow`: a row of a CommandBarFlyout's secondary menu, which is laid out a little
    /// differently from a CommandBar's overflow row.
    bool flyout_overflow = false;

    /// Memberwise equality.
    friend bool operator==(const AppBarLook&, const AppBarLook&) = default;
};

class CommandBarElement;

/// What holds app-bar commands (a CommandBar, a CommandBarFlyout): told when one is clicked, so a
/// bar can close after a click on one of its commands. Implement it only for a custom command
/// container, and register it on each element with
/// CommandBarElement::set_command_host().
class AppBarCommandHost
{
public:
    /// Called after `command`'s own click handler and command have run.
    virtual void command_invoked(CommandBarElement& command) = 0;

protected:
    ~AppBarCommandHost() = default;
};

/// What a CommandBar needs of the things in it: the look it pushes down, the overflow order and
/// the facts the overflow menu sizes its columns by. Mixed into AppBarButton,
/// AppBarToggleButton and AppBarSeparator; use command_bar_element() to reach it from an Element.
class CommandBarElement
{
public:
    virtual ~CommandBarElement() = default;

    /// The look the holding bar last set (the default look on its own).
    [[nodiscard]] const AppBarLook& app_bar_look() const
    {
        return look_;
    }
    /// Applies a new look from the holding bar; implementations update their layout and redraw.
    virtual void set_app_bar_look(const AppBarLook& look) = 0;

    /// Which commands move into the overflow first when the bar runs out of room. The lowest
    /// positive order goes first, a whole group at once, and 0 goes last, from the right.
    [[nodiscard]] int dynamic_overflow_order() const
    {
        return dynamic_overflow_order_;
    }
    /// Sets the overflow order, which takes effect on the bar's next layout.
    void set_dynamic_overflow_order(int order)
    {
        dynamic_overflow_order_ = order;
    }

    /// Whether the bar currently shows this element as an overflow-menu row.
    [[nodiscard]] bool is_in_overflow() const
    {
        return look_.overflow;
    }

    /// Whether there is text for a bar with Bottom text to reveal on opening. False by
    /// default.
    [[nodiscard]] virtual bool has_text() const
    {
        return false;
    }
    /// The shortcut as display text ("Ctrl+S"), empty for none; sizes the overflow's shortcut column.
    [[nodiscard]] virtual std::string shortcut_text() const
    {
        return {};
    }
    /// Whether the element draws an icon; the overflow then reserves an icon column.
    [[nodiscard]] virtual bool has_icon() const
    {
        return false;
    }
    /// True for AppBarToggleButton; the overflow then reserves a check column.
    [[nodiscard]] virtual bool is_toggle() const
    {
        return false;
    }
    /// True for AppBarSeparator.
    [[nodiscard]] virtual bool is_separator() const
    {
        return false;
    }

    /// Set by the CommandBar (or CommandBarFlyout) that holds the element: a click on a command
    /// closes the bar. Non-owning; null (the default) for a command on its own.
    void set_command_host(AppBarCommandHost* host)
    {
        command_host_ = host;
    }

protected:
    AppBarLook         look_{};
    AppBarCommandHost* command_host_           = nullptr;
    int                dynamic_overflow_order_ = 0;
};

/// The app-bar element `element` is, or wraps: an AppBarButton, AppBarToggleButton or
/// AppBarSeparator, or the trigger of a flyout wrapped around one (an AppBarButton with a flyout,
/// where the flyout owns its button). Null for anything else.
[[nodiscard]] CommandBarElement* command_bar_element(Element* element);
/// Const overload of command_bar_element().
[[nodiscard]] const CommandBarElement* command_bar_element(const Element* element);

/// A command-bar button: an icon over a small label that changes shape with where it sits.
///
/// On its own it is full size: 68 DIPs wide, at least 64 tall, with the label under the icon.
/// Inside a CommandBar the bar sets its look. It is compact while the bar is closed, with the
/// label on the right or hidden as the bar's default says, or a full-width row in the overflow
/// menu with icon, check and shortcut columns. A Command fills in the text, icon, shortcut and
/// tooltip the button leaves unset and follows their changes. The command runs after the click
/// handler and disables the button while the command is disabled. The shortcut fires app-wide.
/// Clicking a command in a bar closes the bar.
///
/// Example:
/// @code
/// auto bar = std::make_unique<ui::CommandBarControl>();
/// auto* save = bar->add_primary(std::make_unique<ui::AppBarButtonControl>("Save", ui::Symbol::Save));
/// save->set_shortcut(ui::Ctrl + ui::Key::S); // tooltip reads "Save (Ctrl+S)"
/// save->on_click([] { /* save */ });
///
/// auto* share = bar->add_secondary(std::make_unique<ui::AppBarButtonControl>("Share", ui::Symbol::Share));
/// share->set_text_position(ui::CommandBarTextPosition::Collapsed);
/// window.set_content(std::move(bar));
///
/// // Declaratively:
/// window.set_content(ui::CommandBar{
///     .primary = {ui::AppBarButton{.text     = "Save",
///                                  .icon     = ui::Symbol::Save,
///                                  .shortcut = ui::Ctrl + ui::Key::S,
///                                  .on_click = [] { /* save */ }}},
/// });
/// @endcode
class AppBarButtonControl : public ButtonControl, public CommandBarElement
{
    /// Drives the hover/press overlay over the plate rather than the whole button.
    void sync_composition_override(detail::Compositor& compositor, const Theme& theme) override;

public:
    /// An empty button, aligned left and top in whatever slot it gets.
    AppBarButtonControl();
    /// A button showing `text` under `icon` (a Symbol, an IconGlyph or any other IconSource).
    explicit AppBarButtonControl(std::string text, IconSource icon = {});

    /// The text under (or beside) the icon, also the default tooltip and
    /// accessible name. Falls back to the command's text. Set it with ButtonControl::set_text().
    [[nodiscard]] std::string text() const;

    /// The icon, 16 DIPs in the bar. Falls back to the command's icon.
    [[nodiscard]] const IconSource& icon() const;
    /// Sets the icon; updates the layout and redraws.
    void set_icon(IconSource icon)
    {
        icon_ = std::move(icon);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Collapsed hides the text whatever the bar's default; Default
    /// (the default) follows the bar.
    [[nodiscard]] CommandBarTextPosition text_position() const
    {
        return text_position_;
    }
    /// Sets the text position; updates the layout and redraws.
    void set_text_position(CommandBarTextPosition position)
    {
        text_position_ = position;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Whether the button is drawn compact, without its text. A CommandBar sets it while closed.
    [[nodiscard]] bool is_compact() const
    {
        return look_.compact;
    }
    /// Sets the compact flag through set_app_bar_look(); updates the layout and redraws.
    void set_compact(bool compact)
    {
        AppBarLook look = look_;
        look.compact    = compact;
        set_app_bar_look(look);
    }

    /// The keyboard shortcut, which fires app-wide and clicks the button. Falls back to the
    /// command's.
    [[nodiscard]] KeyCombo shortcut() const;
    /// Sets the shortcut; updates the layout, since the overflow sizes its shortcut column by it,
    /// and redraws.
    void set_shortcut(KeyCombo shortcut)
    {
        shortcut_ = shortcut;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The shortcut text shown in the overflow and the tooltip
    /// in place of the one derived from shortcut() (e.g. "Ctrl+Shift+Del" for a custom
    /// binding). Empty uses the derived text.
    void set_shortcut_text(std::string text)
    {
        shortcut_text_ = std::move(text);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    // The command and its parameter come from ButtonControl::set_command(). The command also fills
    // the text, icon, shortcut and tooltip left unset.

    // ---- CommandBarElement ----
    /// Takes the holding bar's look; updates the layout and redraws.
    void set_app_bar_look(const AppBarLook& look) override;
    /// True when text() (own or the command's) is non-empty.
    [[nodiscard]] bool has_text() const override
    {
        return !text().empty();
    }
    /// The shortcut text override, or the shortcut formatted as "Ctrl+S"; empty for none.
    [[nodiscard]] std::string shortcut_text() const override;
    /// True when icon() (own or the command's) is set.
    [[nodiscard]] bool has_icon() const override
    {
        return !icon().empty();
    }

    // ---- Element ----
    /// True: the button can be measured in parallel with other elements, since measuring reads
    /// only its text and icon.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }
    /// An explicit tooltip, else the command's description, else "Text (Ctrl+S)" outside the
    /// overflow (empty inside it).
    [[nodiscard]] std::string tool_tip() const override;
    /// Clicks the button when `e` matches shortcut() and the button is enabled; true if handled.
    bool handle_global_shortcut(const KeyEvent& e) override;
    /// The text, when no accessible name was set.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return text();
    }

protected:
    /// Tells the command host a command ran, which closes the bar (not when the button opens a
    /// flyout).
    void on_activated() override;
    /// The toggle's checked state; nullopt for a plain button. AppBarToggleButton overrides it so
    /// the shared rendering can draw the checked look.
    [[nodiscard]] virtual std::optional<std::optional<bool>> toggle_state() const
    {
        return std::nullopt;
    }
    /// The hover plate: the part of the button that takes a background.
    [[nodiscard]] Rect plate_rect() const;
    /// Sizes for the current mode: full size, compact, label on the right, collapsed or an
    /// overflow row.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Positions the icon, label and hover plate for the current mode.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws the hover plate, icon, label, flyout chevron, and in the overflow the check and shortcut.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    IconSource             icon_;
    CommandBarTextPosition text_position_ = CommandBarTextPosition::Default;
    KeyCombo               shortcut_{};
    std::string            shortcut_text_;

    mutable float label_height_ = 0.0f; // the wrapped bottom label's height, from measure_override()

    enum class Mode
    {
        FullSize,
        Compact,
        LabelCollapsed,
        LabelOnRight,
        Overflow,
        FlyoutPrimary,
    };
    [[nodiscard]] Mode mode() const;

protected:
    /// Around the hover plate inside the button.
    [[nodiscard]] Rect focus_ring_rect(const Theme& /*theme*/) const override
    {
        return plate_rect();
    }
};

/// An app-bar button with a checked state, drawn as the accent button in the bar and as a check
/// mark in the overflow.
///
/// A click advances the checked state (unchecked, checked, then indeterminate when three_state())
/// and fires on_checked_changed().
///
/// Example:
/// @code
/// auto bar  = std::make_unique<ui::CommandBarControl>();
/// auto* bold = bar->add_primary(std::make_unique<ui::AppBarToggleButtonControl>("Bold", ui::Symbol::Bold));
/// bold->set_shortcut(ui::Ctrl + ui::Key::B);
/// bold->on_checked_changed([](std::optional<bool> on) { /* apply bold when on == true */ });
/// window.set_content(std::move(bar));
///
/// // Declaratively, bound to state:
/// ui::Property<std::optional<bool>> is_bold{false};
/// window.set_content(ui::CommandBar{
///     .primary = {ui::AppBarToggleButton{.text               = "Bold",
///                                        .checked            = is_bold,
///                                        .icon               = ui::Symbol::Bold,
///                                        .on_checked_changed = [&](std::optional<bool> on) { is_bold = on; }}},
/// });
/// @endcode
class AppBarToggleButtonControl final : public AppBarButtonControl, private ToggleProvider
{
public:
    /// An empty, unchecked toggle.
    AppBarToggleButtonControl();
    /// A toggle showing `text` under `icon`, starting at `checked` (nullopt is indeterminate).
    explicit AppBarToggleButtonControl(std::string text, IconSource icon = {}, std::optional<bool> checked = false);

    /// The checked state: true, false, or nullopt for indeterminate. Default false.
    [[nodiscard]] std::optional<bool> checked() const
    {
        return checked_;
    }
    /// Sets the checked state; redraws and fires on_checked_changed() when it changed.
    void set_checked(std::optional<bool> checked);

    /// Whether a click can reach indeterminate (checked, then indeterminate, then unchecked).
    [[nodiscard]] bool three_state() const
    {
        return three_state_;
    }
    /// Enables the indeterminate step in the click cycle.
    void set_three_state(bool three_state)
    {
        three_state_ = three_state;
    }

    /// Called with the new state whenever the checked state changes.
    void on_checked_changed(Callback<void(std::optional<bool>)> handler)
    {
        on_checked_changed_ = std::move(handler);
    }

    /// Always true.
    [[nodiscard]] bool is_toggle() const override
    {
        return true;
    }


protected:
    /// Advances the checked state, then closes the holding bar as AppBarButton does.
    void on_activated() override;
    /// checked(), so the shared rendering draws the checked look.
    [[nodiscard]] std::optional<std::optional<bool>> toggle_state() const override
    {
        return checked_;
    }

private:
    std::optional<bool> checked_     = false;
    bool                three_state_ = false;
    bool                choice_      = false; // bound through ui::selects(): a click never unchecks
    friend struct AppBarToggleButton;         // sets choice_
    Callback<void(std::optional<bool>)> on_checked_changed_;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.toggle = this;
    }
    // The checked state as On/Off/Indeterminate.
    [[nodiscard]] ToggleState toggle_value() const override
    {
        if (!checked_)
        {
            return ToggleState::Indeterminate;
        }
        return *checked_ ? ToggleState::On : ToggleState::Off;
    }
    // Advances the checked state as a click would; false while disabled.
    bool toggle_next() override;
};

/// A 1px rule in the theme's divider color: vertical between groups of commands in the bar
/// (padding 2,8,2,8), horizontal across the overflow menu (0,4,0,4 around it).
///
/// Example:
/// @code
/// auto bar = std::make_unique<ui::CommandBarControl>();
/// bar->add_primary(std::make_unique<ui::AppBarButtonControl>("Cut", ui::Symbol::Cut));
/// bar->add_primary(std::make_unique<ui::AppBarButtonControl>("Copy", ui::Symbol::Copy));
/// bar->add_primary(std::make_unique<ui::AppBarSeparatorControl>());
/// bar->add_primary(std::make_unique<ui::AppBarButtonControl>("Delete", ui::Symbol::Delete));
/// @endcode
class AppBarSeparatorControl final : public Element, public CommandBarElement
{
public:
    /// A separator; its orientation follows the look the bar sets.
    AppBarSeparatorControl() = default;

    /// Switches between the bar's vertical rule and the overflow's horizontal one, and updates the
    /// layout.
    void set_app_bar_look(const AppBarLook& look) override;
    /// Always true.
    [[nodiscard]] bool is_separator() const override
    {
        return true;
    }
    /// Exposed to UI Automation as a separator.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Separator;
    }

protected:
    /// 5 DIPs wide in the bar (1px rule plus padding); a 9 DIP tall row in the overflow.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Draws the 1px rule in the theme's divider color.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
};

/// A command-bar button: an icon over a small label. Builds a ui::AppBarButtonControl. On its own it
/// is full size (68 by 64 DIPs, the label under the icon). In a ui::CommandBar the bar sets its look.
///
/// Example:
/// @code
/// window.set_content(ui::CommandBar{
///     .primary = {
///         ui::AppBarButton{ .text     = "Save",
///                           .icon     = ui::Symbol::Save,
///                           .shortcut = ui::Ctrl + ui::Key::S,
///                           .on_click = [&] { save(); } },
///         ui::AppBarButton{ .text = "Share", .icon = ui::Symbol::Share },
///     },
/// });
/// @endcode
struct AppBarButton
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<AppBarButton>` points at.
    using control_type = AppBarButtonControl;

    // ---- properties ----
    /// The text under (or beside) the icon, and the default tooltip and accessible name.
    Binding<std::string> text{};
    /// A Symbol, an IconGlyph, or any other IconSource.
    Binding<IconSource> icon{};
    /// Collapsed hides this button's text whatever the bar's default says.
    Binding<CommandBarTextPosition> text_position{CommandBarTextPosition::Default};
    /// Draws the button without its text. A CommandBar sets this itself while closed.
    bool compact = false;
    /// The keyboard shortcut, which fires app-wide and clicks the button.
    Binding<KeyCombo> shortcut{};
    /// When the bar runs out of room, the lowest positive order moves into
    /// the overflow first (a whole group at once); 0 moves last, from the right.
    Binding<int> dynamic_overflow_order{0};

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
    /// Shown when the pointer rests on it. Unset: the command's description, or "Text (Ctrl+S)".
    Binding<std::string> tool_tip{};
    Binding<bool>        enabled{true};   ///< False grays it out and takes no input.
    Accessibility        accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The command, which also fills the text, icon, shortcut and tooltip left unset.
    CommandRef command{};
    /// Handed to the command's execute.
    std::string command_parameter{};
    /// Fires on a pointer click, Enter/Space, or the shortcut. In an open bar it also closes it.
    Action on_click{};
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
    ControlOut<AppBarButton> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::AppBarButtonControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

/// A command-bar button that stays pressed. Builds a ui::AppBarToggleButtonControl. Apart from
/// the checked state it works like ui::AppBarButton. `.checked` bound with ui::bind() to a
/// `Property<std::optional<bool>>` is two-way: a click writes the new state back.
///
/// Example:
/// @code
/// ui::Property<std::optional<bool>> bold{false};
///
/// window.set_content(ui::CommandBar{
///     .primary = {
///         ui::AppBarToggleButton{ .text     = "Bold",
///                                 .checked  = bold,
///                                 .icon     = ui::Symbol::Bold,
///                                 .shortcut = ui::Ctrl + ui::Key::B },
///     },
/// });
/// @endcode
struct AppBarToggleButton
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<AppBarToggleButton>` points at.
    using control_type = AppBarToggleButtonControl;

    // ---- properties ----
    /// The text under (or beside) the icon, and the default tooltip and accessible name.
    Binding<std::string> text{};
    /// True, false, or std::nullopt for indeterminate. Seeding it raises nothing. Two-way with a
    /// `Property<bool>` (a click writes true or false; an indeterminate state set on the control
    /// writes nothing) or a `Property<std::optional<bool>>` (every state). See ui::CheckedField.
    CheckedField checked{false};
    /// A click also reaches indeterminate: unchecked, checked, indeterminate, unchecked. Without
    /// it, a click on an indeterminate box checks it. Needs `checked` bound to a
    /// `Property<std::optional<bool>>` (or one-way), since a `Property<bool>` cannot hold the
    /// third state; a debug build asserts on that. Read once, when the spec is built.
    bool three_state = false;
    /// A Symbol, an IconGlyph, or any other IconSource.
    Binding<IconSource> icon{};
    /// Collapsed hides this button's text whatever the bar's default says.
    Binding<CommandBarTextPosition> text_position{CommandBarTextPosition::Default};
    /// Draws the button without its text. A CommandBar sets this itself while closed.
    bool compact = false;
    /// The keyboard shortcut, which fires app-wide and toggles the button.
    Binding<KeyCombo> shortcut{};
    /// The overflow order; see ui::AppBarButton::dynamic_overflow_order.
    Binding<int> dynamic_overflow_order{0};

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
    /// Shown when the pointer rests on it. Unset: the command's description, or "Text (Ctrl+B)".
    Binding<std::string> tool_tip{};
    Binding<bool>        enabled{true};   ///< False grays it out and takes no input.
    Accessibility        accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The command, which also fills the text, icon, shortcut and tooltip left unset.
    CommandRef command{};
    /// Handed to the command's execute.
    std::string command_parameter{};
    /// Fires on a click, after the state has toggled.
    Action on_click{};
    /// Fires with the new state on every change the user makes. A bound value doesn't run it.
    Callback<void(std::optional<bool>)> on_checked_changed{};
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
    ControlOut<AppBarToggleButton> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::AppBarToggleButtonControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

/// The rule between groups of commands. Builds a ui::AppBarSeparatorControl.
///
/// Example:
/// @code
/// window.set_content(ui::CommandBar{
///     .primary = { ui::AppBarButton{ .text = "Cut", .icon = ui::Symbol::Cut },
///                  ui::AppBarButton{ .text = "Copy", .icon = ui::Symbol::Copy },
///                  ui::AppBarSeparator{},
///                  ui::AppBarButton{ .text = "Delete", .icon = ui::Symbol::Delete } },
/// });
/// @endcode
struct AppBarSeparator
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<AppBarSeparator>` points at.
    using control_type = AppBarSeparatorControl;

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
    ControlOut<AppBarSeparator> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::AppBarSeparatorControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
