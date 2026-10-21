// SPDX-License-Identifier: MIT
#pragma once

// The Vista and classic drop-down pieces Dropdown and ComboBox share, drawn from the theme's
// DropdownChrome (ui/themes/ControlChrome.hpp).

#include "../ui/themes/ControlChrome.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <cstddef>

namespace ui
{

// The Vista drop-down arrow: the path `M 0 0 L 3.5 4 L 7 0 Z`, a solid triangle centered in
// `column` with a 3,1,0,0 margin, in `arrow` or, disabled, `arrow_disabled`.
void draw_dropdown_arrow(Renderer& renderer, const detail::ControlChrome::DropdownChrome& chrome, const Rect& column, bool enabled);

// The Vista drop-down list: a square white box in a 1px frame, one row of `row_height` per item,
// the `highlighted` row filled in the selection color with its text in the selection text
// color. No selected-item mark: the era highlights the selected item on opening and lets the
// highlight follow the pointer from there. `highlighted` out of range highlights nothing;
// `row_height` is in DIPs.
void draw_dropdown_list(Renderer&                                    renderer,
                        const Theme&                                 theme,
                        const detail::ControlChrome::DropdownChrome& chrome,
                        const Rect&                                  bounds,
                        const std::vector<std::string>&              items,
                        float                                        row_height,
                        std::size_t                                  highlighted);

// What the classic drop-down field draws from its control (ComboBox, CalendarDatePicker).
struct ClassicComboState
{
    Rect             box;
    bool             enabled       = false;
    bool             expanded      = false;
    bool             focused       = false;
    bool             focus_visible = false;
    std::string_view text;                // the selected item, or the placeholder
    bool             placeholder = false; // `text` is the placeholder
};

// The `COMBOBOX` class's drop-down list: a field in the classic field edge, a push button inside
// its right edge that sinks while open, and the text in selection colors as focus cue.
void draw_classic_combo_field(Renderer&                                    renderer,
                              const Theme&                                 theme,
                              const detail::ControlChrome::DropdownChrome& era,
                              const ClassicComboState&                     state);

} // namespace ui
