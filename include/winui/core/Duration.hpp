// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>

namespace ui
{

/// A length of time: every delay, duration, interval and elapsed time in the API. Milliseconds
/// with a fractional part, so `std::chrono_literals` write it (`800ms`, `0.3s`, `2s`) and a frame
/// step of 16.7 ms keeps its fraction. A bare number does not convert, so a value always says its
/// unit.
///
/// Example:
/// @code
/// using namespace std::chrono_literals;
///
/// ui::Property<std::string> hint{"Saved"};
/// window.set_content(ui::ToolTip{
///     .delay   = 400ms,
///     .anchor  = ui::Button{ .text = "Refresh" },
///     .content = ui::Text{ .text = "Reload the list" },
/// });
/// ui::Subscription hide_hint = window.after(2s, [&] { hint = ""; });
/// @endcode
using Duration = std::chrono::duration<double, std::milli>;

} // namespace ui
