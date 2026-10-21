// SPDX-License-Identifier: MIT
#include "winui/ui/input/RadioGroup.hpp"

#include "winui/ui/input/RadioButton.hpp"

#include <algorithm>

#include <cstddef>

namespace ui
{

RadioGroup::~RadioGroup()
{
    for (RadioButtonControl* button : buttons_)
    {
        button->set_group(nullptr);
    }
}

bool RadioGroup::remove(RadioButtonControl* button)
{
    const auto it = std::find(buttons_.begin(), buttons_.end(), button);
    if (it == buttons_.end())
    {
        return false;
    }
    buttons_.erase(it);
    button->set_group(nullptr);
    if (selected_ == button)
    {
        selected_ = nullptr;
    }
    return true;
}

RadioButtonControl* RadioGroup::add(RadioButtonControl* button)
{
    if (button == nullptr || button->group_.group == this)
    {
        return button;
    }
    if (button->group_.group != nullptr)
    {
        button->group_.group->remove(button);
    }
    button->set_group(this);
    buttons_.push_back(button);
    if (!selected_ && button->checked())
    {
        selected_ = button;
    }
    return button;
}

void RadioGroup::select(RadioButtonControl* selected)
{
    if (selected_ == selected)
    {
        return;
    }
    // By index: a checked-changed handler may add or remove a member.
    for (std::size_t i = 0; i < buttons_.size(); ++i)
    {
        buttons_[i]->set_checked(buttons_[i] == selected);
    }
    selected_ = selected;
    if (on_change_)
    {
        on_change_(selected_);
    }
}

} // namespace ui
