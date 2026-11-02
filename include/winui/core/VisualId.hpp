// SPDX-License-Identifier: MIT
#pragma once

#include "winui/Macros.hpp"

#ifndef LIBWINUI_IMPORTED

#ifndef LIBWINUI_MODULE
#include <cstdint>
#endif

namespace ui
{

/// A stable identity for an Element, unique in the process, assigned at construction and never
/// reused (Element::visual_id()). Use it as a key for data kept about an element elsewhere.
LIBWINUI_EXPORT using VisualId = std::uint32_t;

/// The id that marks no element. allocate_visual_id() never returns 0.
LIBWINUI_EXPORT inline constexpr VisualId InvalidVisualId = 0;

/// Returns a new id, unique in the process, counting up from 1. Thread-safe. Element calls this
/// itself, so controls never need to.
LIBWINUI_EXPORT [[nodiscard]] VisualId allocate_visual_id();

} // namespace ui

#endif // LIBWINUI_IMPORTED
