// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ui
{

/// A Button that stays pressed, with a nullable checked state.
///
/// Everything a Button has (text or content, style, padding, focus, Enter/Space, `on_click()`)
/// still applies. Each activation first advances the state (unchecked -> checked -> unchecked,
/// or -> indeterminate when three_state() is on) and then runs the click handler.
///
/// Example:
/// @code
/// ui::Property<std::optional<bool>> bold{false};
///
/// auto toolbar = std::make_unique<ui::StackControl>(ui::Orientation::Horizontal);
/// auto* toggle = toolbar->add(std::make_unique<ui::ToggleButtonControl>("Bold"));
/// toggle->on_checked_changed([&](std::optional<bool> on) { bold = on; });
///
/// // Or declaratively, bound both ways through the Property:
/// toolbar->add(ui::ToggleButton{
///     .text               = "Italic",
///     .checked            = bold,
/// }.build());
/// window.set_content(std::move(toolbar));
/// @endcode
class ToggleButtonControl final : public ButtonControl, private ToggleProvider
{
public:
    /// An unchecked toggle with no text; give it text with set_text() or content with
    /// set_content().
    ToggleButtonControl() = default;
    /// A toggle labeled `text`, starting in `checked` (`std::nullopt` is indeterminate).
    explicit ToggleButtonControl(std::string text, std::optional<bool> checked = false);

    /// The checked state: `true`, `false`, or `std::nullopt` for indeterminate. Defaults to
    /// `false`.
    [[nodiscard]] std::optional<bool> checked() const
    {
        return checked_;
    }
    /// Changes the state and redraws. A no-op when unchanged; otherwise fires
    /// on_checked_changed(). Any value may be set, even `std::nullopt` without three_state().
    void set_checked(std::optional<bool> checked);

    /// Whether a click can reach indeterminate (checked -> indeterminate ->
    /// unchecked). Off by default.
    [[nodiscard]] bool three_state() const
    {
        return three_state_;
    }
    /// Takes effect on the next activation; does not change the current state.
    void set_three_state(bool three_state)
    {
        three_state_ = three_state;
    }

    /// Invoked with the new state whenever `checked` changes (click, Enter/Space, UI Automation
    /// or set_checked()), including into and out of indeterminate. Replaces any previous handler.
    void on_checked_changed(Callback<void(std::optional<bool>)> handler)
    {
        on_checked_changed_ = std::move(handler);
    }

    // ---- automation ----

protected:
    /// Advances the checked state before the click handler runs.
    void on_activated() override;
    /// The accent button's colors while checked; the standard button's otherwise.
    [[nodiscard]] ButtonVisual current_visual(const Theme& theme) const override;

private:
    std::optional<bool> checked_     = false;
    bool                three_state_ = false;
    bool                choice_      = false; // bound through ui::selects(): a click never unchecks
    friend struct ToggleButton;               // sets choice_
    Callback<void(std::optional<bool>)> on_checked_changed_;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.toggle = this;
    }
    // Maps checked() to On / Off, and `std::nullopt` to Indeterminate.
    [[nodiscard]] ToggleState toggle_value() const override
    {
        if (!checked_)
        {
            return ToggleState::Indeterminate;
        }
        return *checked_ ? ToggleState::On : ToggleState::Off;
    }
    // Advances the state as a click would, but without running the click handler. Returns
    // false (and does nothing) while disabled.
    bool toggle_next() override
    {
        if (!is_enabled())
        {
            return false;
        }
        on_activated(); // an automation Toggle toggles without raising on_click
        return true;
    }
};

/// A button that stays pressed. Builds a ui::ToggleButtonControl. `checked` is nullable, as
/// ui::CheckBox's is, and two-way with ui::bind() of a `Property<std::optional<bool>>`.
/// `three_state` lets a click reach indeterminate.
///
/// Example:
/// @code
/// ui::Property<std::optional<bool>> bold{false};
///
/// window.set_content(ui::ToggleButton{.text = "Bold", .checked = bold});
/// @endcode
struct ToggleButton
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ToggleButton>` points at.
    using control_type = ToggleButtonControl;

    // ---- properties ----
    /// The label, ignored while `.content` is set.
    Binding<std::string> text{};
    /// True, false, or std::nullopt for indeterminate. Seeding it raises nothing. Two-way with a
    /// `Property<bool>` (a click writes true or false; an indeterminate state set on the control
    /// writes nothing) or a `Property<std::optional<bool>>` (every state). See ui::CheckedField.
    CheckedField checked{false};
    /// A click also reaches indeterminate: unchecked, checked, indeterminate, unchecked. Without
    /// it, a click on an indeterminate button checks it. Needs `checked` bound to a
    /// `Property<std::optional<bool>>` (or one-way), since a `Property<bool>` cannot hold the
    /// third state; a debug build asserts on that. Read once, when the spec is built.
    bool three_state = false;
    /// The padding around the text or content; nullopt keeps the theme's button padding.
    Binding<std::optional<Thickness>> padding{};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named Style; explicit fields below win over it.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Fixed width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Fixed height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Minimum width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Minimum height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Maximum width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Maximum height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment within the parent's slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment within the parent's slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// A shared, named action (ui/Command.hpp), as ui::Button's: run after `.on_click` with
    /// `.command_parameter`, and the button is enabled only while both the command and
    /// `.enabled` allow it. An unset `.tool_tip` falls back to the command's description.
    CommandRef command{};
    /// Handed to `.command` when it runs.
    std::string command_parameter{};
    /// Click, as ui::Button's. The checked state has already changed when it runs.
    Action on_click{};
    /// The new state after the user changed it, including into and out of indeterminate. A bound
    /// value doesn't run it.
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
    ControlOut<ToggleButton> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// Arbitrary content instead of a plain label, as ui::Button's.
    Spec content{};

    /// Consumes the spec and returns the built ui::ToggleButtonControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
