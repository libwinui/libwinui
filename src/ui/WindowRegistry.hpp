// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace ui
{

class Window;

namespace detail
{
// The registry of live Windows behind WindowRef (src/ui/WindowRef.cpp). Window's constructor
// registers it as its last step and gets its serial; its destructor unregisters it as its first.
[[nodiscard]] std::uint64_t register_window(const Window* window);
void                        unregister_window(const Window* window);
// The Window whose tick() runs on this thread now, or null outside one.
[[nodiscard]] Window* ticking_window() noexcept;
} // namespace detail

} // namespace ui
