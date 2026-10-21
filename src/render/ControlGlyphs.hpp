// SPDX-License-Identifier: MIT
#pragma once

#include "winui/render/IconGlyph.hpp"

namespace ui
{

// The glyphs only one control draws, beside the public `glyphs::` table in
// winui/render/IconGlyph.hpp. Same namespace, not installed.
namespace glyphs
{
// CheckBox's indeterminate mark on Windows 11.
inline constexpr IconGlyph CheckBoxIndeterminate{0xE9AE};
// CheckBox's indeterminate mark on Windows 10: a filled square, in Segoe MDL2 Assets.
inline constexpr IconGlyph CheckBoxIndeterminateMdl2{0xE73C};

// A TreeView row's collapsed chevron (0xE76C) at 8pt, smaller than the generic 12pt
// ChevronDown/ChevronUp, which is why TreeView gets its own pair.
inline constexpr IconGlyph TreeChevronCollapsed{0xE76C, "Segoe Fluent Icons", 8.0f};
// A TreeView row's expanded chevron: ChevronDown's codepoint at 8pt.
inline constexpr IconGlyph TreeChevronExpanded{0xE70D, "Segoe Fluent Icons", 8.0f};
// A NavigationShell item's expand/collapse chevron. The animated one
// (`AnimatedChevronUpDownSmallVisualSource`) morphs from down to up, and 0xE70D at 8pt is its
// fallback without animation. This render path has no glyph rotation, so the two ends of that
// morph are named separately here, as TreeView's pair above is and for the same reason (8pt,
// smaller than ChevronDown/Up's 12). This one is the collapsed end.
inline constexpr IconGlyph NavChevronCollapsed{0xE70D, "Segoe Fluent Icons", 8.0f};
// A NavigationShell item's chevron when expanded (see NavChevronCollapsed).
inline constexpr IconGlyph NavChevronExpanded{0xE70E, "Segoe Fluent Icons", 8.0f};

inline constexpr IconGlyph RatingStar{0xE735};        // Filled star (RatingControl).
inline constexpr IconGlyph RatingStarOutline{0xE734}; // Outline star (RatingControl).

// The attention InfoBadge's icon glyph (0xEA38).
inline constexpr IconGlyph InfoBadgeAttention{0xEA38};

// InfoBar's Informational symbol, also InfoBadge's informational icon.
inline constexpr IconGlyph InfoBarInformational{0xF13F};
inline constexpr IconGlyph InfoBarSuccess{0xF13E}; // InfoBar's Success symbol.
inline constexpr IconGlyph InfoBarWarning{0xF13C}; // InfoBar's Warning symbol.
inline constexpr IconGlyph InfoBarError{0xF13D};   // InfoBar's Error symbol.
} // namespace glyphs

} // namespace ui
