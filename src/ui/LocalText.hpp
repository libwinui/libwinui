// SPDX-License-Identifier: MIT
#pragma once

// For a control that hands built-in text to a part it holds (a button's name, a tooltip's
// text): keeping that text in step with set_strings().

#include "../core/StringsDetail.hpp"
#include "winui/core/Callback.hpp"
#include "winui/core/Strings.hpp"
#include "winui/ui/Element.hpp"

#include <utility>

namespace ui::detail
{

// Runs `apply` now, and again after every set_strings() for as long as `owner` lives.
inline void follow_strings(Element& owner, Callback<void()> apply)
{
    apply();
    owner.add_subscription(strings_changed().observe(std::move(apply)));
}

} // namespace ui::detail
