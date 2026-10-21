// SPDX-License-Identifier: MIT
// The windows ui::open_window() handed to the loop, released by the backend at a safe point.
#pragma once

namespace ui::detail
{

// Destroys every window ui::open_window() was given that has closed since. The backend calls it
// between messages, when no handler of such a window is running.
void release_closed_windows();

} // namespace ui::detail
