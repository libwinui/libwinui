// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"

#include <string>
#include <string_view>

namespace ui
{

/// One symbol-font glyph, the way every Fluent control draws its small marks (checkmark,
/// chevron, overflow "more"): a codepoint, a font family and a default size.
///
/// A plain aggregate, so an app declares any glyph the `glyphs::` table doesn't name. Draw one
/// with draw_icon(); it converts implicitly to an IconSource for controls that take an icon.
///
/// Example:
/// @code
/// const ui::Rect icon_rect{0, 0, 16, 16};
/// const ui::Rect chevron_rect{100, 0, 16, 16};
///
/// constexpr ui::IconGlyph ZoomIn{0xE8A3}; // Segoe Fluent Icons "ZoomIn", 12 DIPs
/// ui::draw_icon(renderer, ZoomIn, icon_rect, theme.palette.text_fill_primary);
/// ui::draw_icon(renderer, ui::glyphs::ChevronDown, chevron_rect, theme.colors().text, 0.0f,
///               theme.icon_font_family); // the era's symbol font
/// @endcode
struct IconGlyph
{
    /// A Segoe Fluent Icons codepoint, zero in a default-constructed glyph. Every glyph in
    /// `glyphs::` sits in the Private Use Area (U+E000-U+F8FF) and fits one UTF-16 code unit.
    /// draw_icon() doesn't handle surrogate pairs.
    char32_t codepoint = 0;
    /// The symbol font.
    FontName font_family = "Segoe Fluent Icons";
    /// Default font size in DIPs, used when draw_icon() gets no size override.
    float size = 12.0f;

    /// Same codepoint, size and family name.
    friend constexpr bool operator==(const IconGlyph& a, const IconGlyph& b) = default;
};

/// Names for the glyphs the library's controls use. It is not Segoe Fluent Icons' full
/// catalog.
///
/// An app that needs a glyph the table doesn't name declares its own: IconGlyph is a plain
/// aggregate, so a codepoint from the Segoe Fluent Icons catalog is all it takes, and it works
/// everywhere a `glyphs::` constant does.
///
/// @code
/// constexpr ui::IconGlyph ZoomIn{0xE8A3};  // ZoomIn
/// constexpr ui::IconGlyph ZoomOut{0xE71F}; // ZoomOut
/// @endcode
namespace glyphs
{
inline constexpr IconGlyph Accept{0xE73E};      ///< Checkmark (CheckBox).
inline constexpr IconGlyph ChevronDown{0xE70D}; ///< ComboBox's expand indicator.
inline constexpr IconGlyph ChevronUp{0xE70E};   ///< Expander's collapse indicator.
/// The chevron Breadcrumb draws between its items. Layout is left-to-right only, so there is no
/// constant for the mirrored chevron (0xE973).
inline constexpr IconGlyph ChevronRight{0xE974};
inline constexpr IconGlyph More{0xE712}; ///< Overflow "...".

/// The older Segoe MDL2 Assets "X" (0xE10A, distinct from the newer 0xE711): InfoBar's close
/// button, and InfoBadge's critical icon.
inline constexpr IconGlyph Cancel{0xE10A};
/// The exclamation mark (0xE171): InfoBadge's caution icon.
inline constexpr IconGlyph Important{0xE171};
/// The older Segoe MDL2 Assets check (0xE10B, distinct from `Accept`'s 0xE73E):
/// InfoBadge's success icon.
inline constexpr IconGlyph AcceptSymbol{0xE10B};

/// InfoBar's icon is two overlaid glyphs, a filled circle and a small symbol on top of it, both
/// drawn at 16 DIPs through draw_icon()'s `size_override`. This is the circle.
inline constexpr IconGlyph CircleFill{0xF136};

/// TitleBar's back button.
inline constexpr IconGlyph Back{0xE72B};
/// TitleBar's pane-toggle ("hamburger") button.
inline constexpr IconGlyph GlobalNavigationButton{0xE700};

/// PersonPicture's placeholder when it has neither a photo nor initials.
inline constexpr IconGlyph Contact{0xE77B};
/// PersonPicture's group placeholder, shown for a group whatever its photo or initials.
inline constexpr IconGlyph People{0xE716};

/// AnnotatedScrollBar's step-button glyphs at 8 DIPs, named by the direction each button
/// scrolls. This one scrolls up.
inline constexpr IconGlyph ScrollUp{0xEDDB, "Segoe Fluent Icons", 8.0f};
/// The down-scrolling step glyph (see ScrollUp).
inline constexpr IconGlyph ScrollDown{0xEDDC, "Segoe Fluent Icons", 8.0f};
/// The horizontal pair, also the scroll bar's right arrow (0xEDDA) and Carousel's previous and next
/// arrows (0xEDD9, 0xEDDA). PipsPager's horizontal navigation buttons rotate ScrollUp and
/// ScrollDown by -90 degrees, which draws these two. This one points left.
inline constexpr IconGlyph ScrollLeft{0xEDD9, "Segoe Fluent Icons", 8.0f};
/// The right-pointing arrow (see ScrollLeft).
inline constexpr IconGlyph ScrollRight{0xEDDA, "Segoe Fluent Icons", 8.0f};
} // namespace glyphs

/// Draws `glyph` centered in `bounds` through Renderer::draw_text(), in `color` at
/// `size_override` (or `glyph.size` when `size_override` is 0, the default).
///
/// `font_family`, when not empty, overrides the glyph's own family. A theme picks its era's
/// symbol font this way through `Theme::icon_font_family`: `Segoe MDL2 Assets` or
/// `Segoe Fluent Icons`, which draw the same codepoints in two styles.
///
/// Example:
/// @code
/// // InfoBar's icon: a filled circle, then the symbol on top, both at 16.
/// const ui::Rect  icon_rect{12, 14, 16, 16};
/// const ui::Color severity_color = ui::Color::from_rgba(196, 43, 28);
/// ui::draw_icon(renderer, ui::glyphs::CircleFill, icon_rect, severity_color, 16.0f, theme.icon_font_family);
/// ui::draw_icon(renderer, ui::glyphs::Important, icon_rect, ui::colors::White, 16.0f, theme.icon_font_family);
/// @endcode
inline void draw_icon(Renderer&        renderer,
                      const IconGlyph& glyph,
                      const Rect&      bounds,
                      const Color&     color,
                      float            size_override = 0.0f,
                      std::string_view font_family   = {})
{
    const std::string text = encode_utf8(glyph.codepoint);
    TextStyle         style;
    style.font_family = !font_family.empty() ? font_family : glyph.font_family.view();
    style.font_size   = size_override > 0.0f ? size_override : glyph.size;
    renderer.draw_text(text, bounds, color, style);
}

} // namespace ui
