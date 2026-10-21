// SPDX-License-Identifier: MIT
// Presses that land somewhere else: for a control that closes on a press outside itself, which
// routed pointer input never delivers to it.
#pragma once

#include "winui/core/Callback.hpp"

namespace ui
{
class Element;

namespace detail
{

// Call `on_press` for every press a pointer dispatch delivers that does not land on `owner` or on
// anything inside it. One watch per owner; a second call replaces it.
void watch_outside_presses(const Element& owner, Callback<void()> on_press);
// Stop watching for `owner`; its destructor must call this.
void unwatch_outside_presses(const Element& owner);

} // namespace detail
} // namespace ui
