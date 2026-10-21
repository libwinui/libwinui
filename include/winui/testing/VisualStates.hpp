// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/input/SplitButton.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/navigation/PipsPager.hpp"
#include "winui/ui/text/NumberBox.hpp"

namespace ui::testing
{

/// Reads the interaction state a control draws in (Normal, Hover, Pressed, Disabled), which only a
/// test looks at. The controls keep it protected; the free functions below are the way in.
///
/// Example:
/// @code
/// ui::ButtonControl button{"OK"};
/// button.set_enabled(false);
/// REQUIRE(ui::testing::visual_state(button) == ui::ButtonState::Disabled);
/// @endcode
struct VisualStates
{
    template <typename C>
    [[nodiscard]] static auto visual_state(const C& control)
    {
        return control.visual_state();
    }
    [[nodiscard]] static ButtonState up_visual_state(const NumberBoxControl& box)
    {
        return box.up_visual_state();
    }
    [[nodiscard]] static ButtonState down_visual_state(const NumberBoxControl& box)
    {
        return box.down_visual_state();
    }
    [[nodiscard]] static ButtonState previous_button_visual_state(const PipsPagerControl& pager)
    {
        return pager.previous_button_visual_state();
    }
    [[nodiscard]] static ButtonState next_button_visual_state(const PipsPagerControl& pager)
    {
        return pager.next_button_visual_state();
    }
};

/// The state a Button (or a ToggleButton, AppBarButton, ...) is drawn in.
[[nodiscard]] inline ButtonState visual_state(const ButtonControl& c)
{
    return VisualStates::visual_state(c);
}
/// The state a CheckBox is drawn in.
[[nodiscard]] inline ButtonState visual_state(const CheckBoxControl& c)
{
    return VisualStates::visual_state(c);
}
/// The state a RadioButton is drawn in.
[[nodiscard]] inline ButtonState visual_state(const RadioButtonControl& c)
{
    return VisualStates::visual_state(c);
}
/// The state a Slider's thumb is drawn in.
[[nodiscard]] inline ButtonState visual_state(const SliderControl& c)
{
    return VisualStates::visual_state(c);
}
/// The state a ToggleSwitch is drawn in.
[[nodiscard]] inline ButtonState visual_state(const ToggleSwitchControl& c)
{
    return VisualStates::visual_state(c);
}
/// The state a SplitButton is drawn in, which tells its two halves apart.
[[nodiscard]] inline SplitButtonState visual_state(const SplitButtonControl& c)
{
    return VisualStates::visual_state(c);
}
/// The state a ComboBox is drawn in: its whole box when read-only, its chevron button when editable.
[[nodiscard]] inline ButtonState visual_state(const ComboBoxControl& c)
{
    return VisualStates::visual_state(c);
}
/// A NumberBox's inline up button.
[[nodiscard]] inline ButtonState up_visual_state(const NumberBoxControl& c)
{
    return VisualStates::up_visual_state(c);
}
/// A NumberBox's inline down button.
[[nodiscard]] inline ButtonState down_visual_state(const NumberBoxControl& c)
{
    return VisualStates::down_visual_state(c);
}
/// A PipsPager's Previous button.
[[nodiscard]] inline ButtonState previous_button_visual_state(const PipsPagerControl& c)
{
    return VisualStates::previous_button_visual_state(c);
}
/// A PipsPager's Next button.
[[nodiscard]] inline ButtonState next_button_visual_state(const PipsPagerControl& c)
{
    return VisualStates::next_button_visual_state(c);
}

} // namespace ui::testing
