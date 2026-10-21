// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/ui/input/RadioButton.hpp"

#include <utility>
#include <vector>

namespace ui
{

/// Makes a set of loose `RadioButton`s mutually exclusive. Not an element: it holds
/// non-owning observers of buttons owned elsewhere in the tree.
///
/// Every registered button that is clicked (or activated with Space or UI Automation) is
/// routed through `select()`, which checks it and unchecks the rest. The group and its buttons
/// may be destroyed in either order: a button that goes leaves the group, and a group that goes
/// leaves its buttons plain radio buttons. For a headed, laid-out group selected by index, use
/// `RadioButtons` instead.
///
/// Example:
/// @code
/// ui::RadioGroup group;
///
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// ui::RadioButtonControl* a = stack->add(std::make_unique<ui::RadioButtonControl>("A", true));
/// ui::RadioButtonControl* b = stack->add(std::make_unique<ui::RadioButtonControl>("B"));
/// group.add(a); // already checked: the initial selection
/// group.add(b);
/// group.on_change([](ui::RadioButtonControl* now) { /* now->text() */ });
/// group.select(b); // programmatic: checks B, unchecks A, fires on_change once
/// window.set_content(std::move(stack));
/// @endcode
class RadioGroup
{
public:
    /// A group with no members.
    RadioGroup() = default;
    /// Lets go of every member; the buttons stay as they are.
    ~RadioGroup();
    /// Not copyable or movable: its buttons point at it.
    RadioGroup(const RadioGroup&) = delete;
    /// Not copyable or movable: its buttons point at it.
    RadioGroup& operator=(const RadioGroup&) = delete;

    /// Registers `button` (a non-owning observer) with this group. If the button is
    /// already checked and no member has been selected yet, it becomes the initial
    /// selection. Otherwise the selection waits for select() or a click.
    /// A button belongs to one group: adding it takes it out of the one it was in.
    RadioButtonControl* add(RadioButtonControl* button);
    /// Takes `button` out of the group, keeping its checked state. False when it is not a member.
    /// A member that is destroyed leaves by itself.
    bool remove(RadioButtonControl* button);

    /// Checks `selected` and unchecks every other member, then fires on_change() once. Does
    /// nothing if `selected` is already the current selection, so clicking the checked button
    /// again changes nothing. A click on a member calls it, and an app may call it to select a
    /// member from code.
    void select(RadioButtonControl* selected);

    /// The current selection (non-owning), or nullptr before anything was selected.
    [[nodiscard]] RadioButtonControl* selected() const
    {
        return selected_;
    }

    /// Fired once per selection change with the newly selected button, after every
    /// member's checked state is updated. Replaces any previous handler.
    void on_change(Callback<void(RadioButtonControl*)> handler)
    {
        on_change_ = std::move(handler);
    }

private:
    std::vector<RadioButtonControl*>    buttons_;
    RadioButtonControl*                 selected_ = nullptr;
    Callback<void(RadioButtonControl*)> on_change_;
};

} // namespace ui
