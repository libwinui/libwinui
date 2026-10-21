// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ui
{

/// A box that is checked, unchecked or indeterminate, with an optional label to its right.
///
/// The value is a nullable bool: `true`, `false`, or `std::nullopt` for indeterminate. Compare it
/// against a value instead of testing it for truth. `if (box.checked())` asks whether the state is
/// determinate and is true for an unchecked box, so write `box.checked() == true`. A click, Space
/// (not Enter, which belongs to a dialog's default button) or UI Automation's Toggle advances the
/// state; whether that can reach indeterminate is three_state().
///
/// Example:
/// @code
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// ui::CheckBoxControl* remember = stack->add(std::make_unique<ui::CheckBoxControl>("Remember me", /*checked=*/false));
/// ui::ButtonControl*   sign_in  = stack->add(std::make_unique<ui::ButtonControl>("Sign in"));
/// sign_in->set_enabled(false);
/// remember->on_checked_changed([sign_in](std::optional<bool> v) { sign_in->set_enabled(v == true); });
/// window.set_content(std::move(stack));
///
/// // Declaratively, bound both ways to a Property:
/// ui::Property<bool> remember_me{false};
/// window.set_content(ui::CheckBox{
///     .text               = "Remember me",
///     .checked            = remember_me,
///     .on_checked_changed = [&](std::optional<bool> v) { remember_me = v.value_or(false); },
/// });
/// @endcode
class CheckBoxControl final : public Element, private ToggleProvider
{

    // ---- composition ----
    /// Fades the hover/press highlight in and out, as ButtonControl does.
    void sync_composition_override(detail::Compositor& compositor, const Theme& theme) override;

public:
    /// An unlabeled, unchecked box.
    CheckBoxControl() = default;
    /// A box labeled `text` (UTF-8) in state `checked` (nullopt: indeterminate). Seeding the
    /// state raises nothing: no handler is attached yet.
    explicit CheckBoxControl(std::string text, std::optional<bool> checked = false);

    // ---- content ----
    /// The label right of the box (UTF-8); empty draws the box alone. Also the automation name
    /// fallback.
    [[nodiscard]] const std::string& text() const
    {
        return text_;
    }
    /// Sets the label (UTF-8). The box is measured again and redrawn.
    void set_text(std::string text)
    {
        text_ = std::move(text);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// True, false, or nullopt for indeterminate; false by default. Compare against a value
    /// (`== true`) instead of testing the optional for truth.
    [[nodiscard]] std::optional<bool> checked() const
    {
        return checked_;
    }
    /// Sets the state, allowed to be indeterminate even when not three_state(). Fires
    /// on_checked_changed() when the value changes, and redraws.
    void set_checked(std::optional<bool> checked);

    /// Whether clicking (or Space, or UI Automation's Toggle) cycles through indeterminate:
    /// unchecked -> checked -> indeterminate -> unchecked. Off, the cycle is unchecked <-> checked,
    /// and an indeterminate box set in code clicks to unchecked. Off by default.
    [[nodiscard]] bool three_state() const
    {
        return three_state_;
    }
    /// Affects only the next click; the current state is left as it is.
    void set_three_state(bool three_state)
    {
        three_state_ = three_state;
    }


    /// Invoked whenever `checked` changes (click, Space, or set_checked()), including into
    /// and out of indeterminate. Receives the new state. Replaces any previous handler.
    void on_checked_changed(Callback<void(std::optional<bool>)> handler)
    {
        on_checked_changed_ = std::move(handler);
    }

    // ---- layout ----
    /// True: measuring reads only the box's own state, the measurer and the theme.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

    // ---- focus ----
    /// Whether the box holds keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// A disabled box is skipped in the tab order.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }

    // ---- derived visual state ----

protected:
    friend struct testing::VisualStates; // the tests read the state a control draws in
    /// Disabled, Pressed, Hover or Normal, from the enabled flag and the pointer/key input.
    [[nodiscard]] ButtonState visual_state() const;
    /// The box plus, when there is a label, the gap and the label's measured text.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;

    // ---- input ----
    /// A press and release inside the control (box or label) toggles it.
    bool pointer_override(const PointerEvent& e) override;

    // ---- render ----
    /// Draws the box (with its check or indeterminate dash), the label and the focus ring.
    void render_override(Renderer& renderer, const RenderContext& context) const override;

    // ---- time ----
    /// In themes that animate the mark, draws the tick in (about 317 ms) when
    /// checked and erases it (67 ms) when cleared. The box's colors change at once and an
    /// indeterminate dash appears without animation. Until the box gets its first animation
    /// frame, and with animations off, the mark appears at once.
    void tick_override(Duration delta) override;


public:
    /// True while pressed by the pointer or a held Space.
    [[nodiscard]] bool is_pressed() const;

    // ---- automation ----
    /// Always AutomationRole::CheckBox.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::CheckBox;
    }
    /// text(), used when Accessibility::name is not set.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return text_;
    }

private:
    void               toggle();
    [[nodiscard]] bool hot() const;
    [[nodiscard]] Rect box_bounds(const Theme& theme) const;
    // The running draw-in or erase, if any, as the stretch of the tick to draw (0..1 along it).
    [[nodiscard]] std::optional<std::pair<float, float>> check_motion() const;

    std::string         text_;
    std::optional<bool> checked_     = false;
    bool                three_state_ = false;

    bool hovered_      = false;
    bool pointer_down_ = false;
    bool key_down_     = false;

    bool composed_hot_ = false;

    bool   ticking_       = false;
    bool   check_erasing_ = false;
    double check_ms_      = -1.0; // into the running draw-in or erase; negative at rest
    double check_len_ms_  = 0.0;

    Callback<void(std::optional<bool>)> on_checked_changed_;

protected:
    void enabled_changed() override;
    /// Called by Window. Losing focus cancels a Space press in progress. Redraws.
    void focus_changed(bool focused) override;
    /// Space down presses, Space up toggles. Enter is not handled.
    bool key_override(const KeyEvent& e) override;

protected:
    /// Around box and label, the label or the box, as the theme places it.
    [[nodiscard]] Rect  focus_ring_rect(const Theme& theme) const override;
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.toggle = this;
    }
    // On, Off, or Indeterminate for a nullopt checked().
    [[nodiscard]] ToggleState toggle_value() const override
    {
        if (!checked_)
        {
            return ToggleState::Indeterminate;
        }
        return *checked_ ? ToggleState::On : ToggleState::Off;
    }
    // Advances the state as a click does (honoring three_state()) when enabled; false and
    // nothing happens when disabled.
    bool toggle_next() override
    {
        if (!is_enabled())
        {
            return false;
        }
        toggle();
        return true;
    }
};

/// A checkbox with a trailing label. Builds a ui::CheckBoxControl. `checked` is nullable:
/// `std::nullopt` is indeterminate, and a plain `true` or `false` converts to it, so
/// `.checked = true` works. `three_state` lets a click reach indeterminate.
///
/// `ui::bind()` of a `Property<std::optional<bool>>` is two-way: a click writes the new state
/// back before `.on_checked_changed` runs.
///
/// Example:
/// @code
/// ui::Property<std::optional<bool>> agreed{false};
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::CheckBox{.text = "I agree", .checked = agreed},
///         ui::Button{.text    = "Continue",
///                    .enabled = ui::bind(agreed, [](std::optional<bool> on) { return on == true; })},
///     },
/// });
/// @endcode
struct CheckBox
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<CheckBox>` points at.
    using control_type = CheckBoxControl;

    // ---- properties ----
    /// The label right of the box.
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
    /// The new state, after a click or the keyboard changed it. A bound value doesn't run it.
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
    ControlOut<CheckBox> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::CheckBoxControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
