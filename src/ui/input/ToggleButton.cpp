// SPDX-License-Identifier: MIT
#include "winui/ui/input/ToggleButton.hpp"

#include "winui/core/DirtyFlags.hpp"
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

ToggleButtonControl::ToggleButtonControl(std::string text, std::optional<bool> checked) :
    ButtonControl(std::move(text)),
    checked_(checked)
{
}

void ToggleButtonControl::set_checked(std::optional<bool> checked)
{
    if (checked_ == checked)
    {
        return;
    }
    checked_ = checked;
    mark_dirty(DirtyFlags::Visual);
    if (on_checked_changed_)
    {
        on_checked_changed_(checked_);
    }
}

void ToggleButtonControl::on_activated()
{
    // Unchecked to checked, checked to indeterminate when three-state, and on to unchecked. A
    // two-state button checks from indeterminate. One of a set of choices (ui::selects()) stays
    // checked, as a radio button does.
    if (checked_ == true)
    {
        if (!choice_)
        {
            set_checked(three_state_ ? std::nullopt : std::optional<bool>{false});
        }
    }
    else
    {
        set_checked(!(three_state_ && !checked_.has_value()));
    }
}

ButtonVisual ToggleButtonControl::current_visual(const Theme& theme) const
{
    if (checked_ != true)
    {
        return ButtonControl::current_visual(theme);
    }
    const ButtonState  state      = visual_state();
    const ButtonVisual accent     = style_visual(theme, ButtonVariant::Accent, ButtonState::Normal);
    const ButtonVisual standard   = style_visual(theme, ButtonVariant::Neutral, ButtonState::Normal);
    const bool         has_accent = accent.background != standard.background || accent.border != standard.border ||
                                    accent.text != standard.text;
    if (has_accent)
    {
        return style_visual(theme, ButtonVariant::Accent, state);
    }
    // No accent to show it with: pushed in, the way a Win32 push-like check box shows "checked".
    return style_visual(theme, ButtonVariant::Neutral, state == ButtonState::Disabled ? state : ButtonState::Pressed);
}

std::unique_ptr<ui::Element> ToggleButton::build() &&
{
    auto node = std::make_unique<ui::ToggleButtonControl>();

    apply(text, *node, [raw = node.get()](const std::string& value) { raw->set_text(value); });
    apply(checked, *node, [raw = node.get()](const std::optional<bool>& value) { raw->set_checked(value); });
    assert((!three_state || !checked.writes_bool()) &&
           "three_state needs `checked` bound to a Property<std::optional<bool>>: a click can reach "
           "indeterminate, which a Property<bool> cannot hold");
    node->set_three_state(three_state);
    node->choice_ = checked.selection_group() != nullptr;
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    if (padding.reactive() || padding.initial())
    {
        apply(padding, *node, [raw = node.get()](const std::optional<Thickness>& v) { raw->set_padding(v); });
    }
    if (content)
    {
        node->set_content(content.take());
    }

    if (auto changed = ui::write_back(checked, std::move(on_checked_changed)))
    {
        node->on_checked_changed(std::move(changed));
    }
    if (command)
    {
        node->set_command(command, std::move(command_parameter));
    }
    if (on_click)
    {
        node->on_click(std::move(on_click));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
