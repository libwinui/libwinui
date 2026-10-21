// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ui
{

class RadioGroup;
class RadioButtonsControl;

/// A round option button with a label, one of a set of mutually exclusive choices.
///
/// Clicking it (or Space while focused) checks it; clicking an already-checked RadioButton
/// does nothing, so the user can turn a lone button on but never off. The button doesn't uncheck
/// the others by itself. Register the buttons with a `RadioGroup`, which doesn't own them, or use
/// `RadioButtons`, which owns and lays out a whole set.
/// `ui::RadioButton` is the declarative form of a single button.
///
/// Example:
/// @code
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// ui::RadioButtonControl* small = stack->add(std::make_unique<ui::RadioButtonControl>("Small", true));
/// ui::RadioButtonControl* large = stack->add(std::make_unique<ui::RadioButtonControl>("Large"));
///
/// ui::RadioGroup group;
/// group.add(small);        // already checked: becomes the initial selection
/// group.add(large);
/// group.on_change([](ui::RadioButtonControl* chosen) { /* chosen->text() */ });
/// large->on_checked_changed([](bool checked) { /* fires for checks and unchecks */ });
/// window.set_content(std::move(stack));
/// @endcode
class RadioButtonControl final : public Element, private SelectionItemProvider
{

    // ---- composition ----
    /// Fades the hover wash in and out, and starts resizing the checked dot when the state changes.
    void sync_composition_override(detail::Compositor& compositor, const Theme& theme) override;
    /// Advances the dot's resize animation.
    void tick_override(Duration delta) override;

public:
    /// An unchecked, enabled button with no label.
    RadioButtonControl() = default;
    /// A button labeled `text` (UTF-8). Seeding `checked` fires nothing, since no handler is
    /// attached yet.
    explicit RadioButtonControl(std::string text, bool checked = false);

    // ---- content ----
    /// The label right of the dot; also the automation name when none is set.
    [[nodiscard]] const std::string& text() const
    {
        return text_;
    }
    /// Sets the label. The button is measured again and redrawn.
    void set_text(std::string text)
    {
        text_ = std::move(text);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Whether the button is selected; false by default.
    [[nodiscard]] bool checked() const
    {
        return checked_;
    }
    /// Sets the checked flag without unchecking the other buttons of a RadioGroup.
    /// RadioGroup::select() calls it to update every member. Call it yourself only on a
    /// standalone RadioButton with no group.
    void set_checked(bool checked);


    /// Invoked with the new state whenever `checked` changes. Replaces any previous handler.
    void on_checked_changed(Callback<void(bool)> handler)
    {
        on_checked_changed_ = std::move(handler);
    }

    // ---- layout ----
    /// True: measuring reads only the button's own state, the measurer and the theme.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

    // ---- focus ----
    /// Whether the button has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }

    // ---- derived visual state ----

protected:
    friend struct testing::VisualStates; // the tests read the state a control draws in
    /// The state the button is drawn in: Disabled, Pressed, Hover or Normal, in that priority.
    [[nodiscard]] ButtonState visual_state() const;
    /// The dot plus the measured label.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;

    // ---- input ----
    /// A left click released over the button checks it (through its group, if any).
    bool pointer_override(const PointerEvent& e) override;

    // ---- render ----
    /// Draws the ring, the checked dot and the label.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


public:
    /// Whether a press is in progress: the pointer held down over the button, or Space held.
    [[nodiscard]] bool is_pressed() const;

    // ---- automation ----
    /// Exposed to UI Automation as a RadioButton with a SelectionItem pattern.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::RadioButton;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return text_;
    }
    /// The property this button was bound to with ui::selects(), which groups it with the other
    /// buttons bound to it (Element::selection_group()); nullptr otherwise.
    [[nodiscard]] const void* selection_group() const override
    {
        return selection_group_;
    }

private:
    friend class RadioGroup;
    void set_group(RadioGroup* group)
    {
        group_.group  = group;
        group_.member = this;
    }

    // Membership of a RadioGroup, severed from this end when the button is destroyed. A button
    // that is moved (only possible before it has a parent) starts outside any group; the one it
    // was moved from stays the member until it is destroyed.
    struct GroupLink
    {
        RadioGroup*         group  = nullptr;
        RadioButtonControl* member = nullptr;

        GroupLink() = default;
        GroupLink(GroupLink&&) noexcept
        {
        }
        GroupLink& operator=(GroupLink&&) noexcept
        {
            return *this;
        }
        ~GroupLink();
    };

    // RadioButtons selects through activate(), and rings the focused item itself.
    friend class RadioButtonsControl;
    void set_owner(RadioButtonsControl* owner)
    {
        owner_ = owner;
    }

    void               activate(); // click / Space release: asks the group to select this one
    [[nodiscard]] bool hot() const;
    [[nodiscard]] Rect dot_bounds(const Theme& theme) const;
    // The checked dot's diameter in the current state: grows on hover, shrinks on press.
    [[nodiscard]] float dot_diameter(const Theme& theme) const;

    std::string text_;
    bool        checked_ = false;

    bool hovered_      = false;
    bool pointer_down_ = false;
    bool key_down_     = false;

    bool composed_hot_ = false;

    // The checked dot's size, eased between rest, hover and pressed. Reset while no dot shows, so
    // a freshly checked dot has nothing to animate from and appears at its size.
    detail::RectTween dot_tween_;

    GroupLink   group_;                     // set by RadioGroup::add()
    const void* selection_group_ = nullptr; // set by the spec from a ui::selects() binding
    friend struct RadioButton;
    RadioButtonsControl* owner_ = nullptr; // the RadioButtons that owns this button, if any

    Callback<void(bool)> on_checked_changed_;

protected:
    void enabled_changed() override;
    /// Losing focus cancels a Space press in progress.
    void focus_changed(bool focused) override;
    /// Space pressed and released while focused checks the button, like a click.
    bool key_override(const KeyEvent& e) override;

protected:
    /// Not while a RadioButtons owns it: the group rings it.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return owner_ == nullptr;
    }
    /// Around dot and label, or the dot alone, as the theme places it.
    [[nodiscard]] Rect  focus_ring_rect(const Theme& theme) const override;
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.selection_item = this;
    }
    [[nodiscard]] bool item_selected() const override
    {
        return checked_;
    }
    // Checks the button as a click would; false while disabled.
    bool select_item() override
    {
        if (!is_enabled())
        {
            return false;
        }
        activate();
        return true;
    }
};

/// One radio button. Buttons that choose one value of a property are bound with ui::selects():
/// checking one sets the property, the others follow it, and the buttons act as one group for
/// the keyboard (one Tab stop, arrow keys between them) wherever they sit in the tree. For a plain
/// list of options, ui::RadioButtons is the whole group in one spec; imperative code can still
/// tie loose buttons together with a RadioGroup.
///
/// Example:
/// @code
/// enum class Size { Small, Large };
/// ui::Property<Size> size{Size::Small};
///
/// window.set_content(ui::VerticalStack{.content = {
///     ui::RadioButton{.text = "Small", .checked = ui::selects(size, Size::Small)},
///     ui::RadioButton{.text = "Large", .checked = ui::selects(size, Size::Large)},
/// }});
/// @endcode
struct RadioButton
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<RadioButton>` points at.
    using control_type = RadioButtonControl;

    // ---- properties ----
    /// The label right of the dot.
    Binding<std::string> text{};
    /// Whether the button is selected; seeding it raises nothing. ui::selects() makes it one choice
    /// of a property's values and groups it with the other buttons bound to that property.
    TwoWayField<bool> checked{false};

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
    /// The new checked state, after a click, or another button of its group taking the check. A
    /// bound value doesn't run it.
    Callback<void(bool)> on_checked_changed{};
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
    ControlOut<RadioButton> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::RadioButtonControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
