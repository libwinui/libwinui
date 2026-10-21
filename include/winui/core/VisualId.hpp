// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace ui
{

/// A stable identity for an Element, unique in the process, assigned at construction and never
/// reused (Element::visual_id()). Use it as a key for data kept about an element elsewhere.
using VisualId = std::uint32_t;

/// The id that marks no element. allocate_visual_id() never returns 0.
inline constexpr VisualId InvalidVisualId = 0;

/// Returns a new id, unique in the process, counting up from 1. Thread-safe. Element calls this
/// itself, so controls never need to.
[[nodiscard]] VisualId allocate_visual_id();

} // namespace ui
