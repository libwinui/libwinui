// SPDX-License-Identifier: MIT
#pragma once

// The scroll bar's arrow button, for the controls that borrow it (CalendarView's paging buttons
// in the classic eras). Private.

#include "winui/core/Geometry.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/themes/Theme.hpp"

namespace ui::detail
{

// Draws the theme's scroll bar arrow button in `cell`, the way its scroll bar draws one: the
// classic raised button, the Windows 3.x and 1.x/2.x arrow boxes, or Luna's blue button. Returns
// false, drawing nothing, for a theme whose scroll bar has no such button (Fluent, Aero).
bool draw_scroll_arrow_button(Renderer&      renderer,
                              const Theme&   theme,
                              const Rect&    cell,
                              ArrowDirection direction,
                              ButtonState    state,
                              bool           enabled);

} // namespace ui::detail
