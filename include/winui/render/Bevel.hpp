// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"

#include <optional>

#include <cstdint>

namespace ui
{

/// Which edges of a bevel draw. Group boxes, status-bar panes and etched separators draw only
/// some of the four. A bit set: combine with `|`.
enum class BevelSides : std::uint8_t
{
    None   = 0,                           ///< No edge.
    Left   = 1 << 0,                      ///< The left edge.
    Top    = 1 << 1,                      ///< The top edge.
    Right  = 1 << 2,                      ///< The right edge.
    Bottom = 1 << 3,                      ///< The bottom edge.
    All    = Left | Top | Right | Bottom, ///< Every edge (the default).
};

/// The union of two side sets.
constexpr BevelSides operator|(BevelSides a, BevelSides b)
{
    return static_cast<BevelSides>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
/// The intersection of two side sets.
constexpr BevelSides operator&(BevelSides a, BevelSides b)
{
    return static_cast<BevelSides>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
/// True when `sides` includes any of the edges in `side`.
constexpr bool has_side(BevelSides sides, BevelSides side)
{
    return (sides & side) != BevelSides::None;
}

/// Draws a classic 3D border around `bounds`, two concentric rings of `step` width:
///   Raised (a control at rest): the top and left get `light` outside `highlight`, the
///     bottom and right get `dark_shadow` outside `shadow`.
///   Sunken (a pressed or checked control): the same shape mirrored. The top and left get
///     `shadow` outside `dark_shadow`, the bottom and right get `highlight` outside `light`.
/// `step` is usually Theme::border_width, 1 DIP in every classic theme. `sides` picks which of
/// the four edges draw (default: all). Any other BorderStyle draws nothing.
///
/// `soft` is the arrangement a push button is drawn with. It swaps the two top and left rings
/// and leaves the bottom and right alone, so a soft raised button has `highlight` outside
/// `light` on its lit side, where a window border has `light` outside `highlight`.
///
/// The outer ring runs the full length of each edge. The inner ring stops one step short at
/// each corner where both adjoining edges draw, so the two rings nest without overlapping.
///
/// `rings` is how many of the two rings to draw. Every framed surface draws two. One draws the
/// outer ring alone, the shape of a hot toolbar button. That shape takes the inner color pair
/// on its one ring, so its caller passes `light == highlight` and `dark_shadow == shadow`.
/// `highlight` and `shadow` go unread at `rings == 1`.
///
/// Every edge is a Renderer::stroke_line(): eight StrokeLine commands for a two-ring border on
/// all four sides.
///
/// Example:
/// @code
/// const ui::Color light       = ui::Color::from_rgba(223, 223, 223);
/// const ui::Color highlight   = ui::colors::White;
/// const ui::Color shadow      = ui::Color::from_rgba(128, 128, 128);
/// const ui::Color dark_shadow = ui::colors::Black;
/// const ui::Rect  bounds{0, 0, 75, 23};
/// const ui::Rect  pane{0, 0, 120, 20};
///
/// // A Windows 95 push button at rest, and the same button pressed.
/// ui::draw_bevel_border(renderer, bounds, ui::BorderStyle::Raised, light, highlight, shadow,
///                          dark_shadow, 1.0f, ui::BevelSides::All, /*soft=*/true);
/// ui::draw_bevel_border(renderer, bounds, ui::BorderStyle::Sunken, light, highlight, shadow,
///                          dark_shadow, 1.0f);
///
/// // A status-bar pane separator: the left and right edges only.
/// ui::draw_bevel_border(renderer, pane, ui::BorderStyle::Sunken, light, highlight, shadow,
///                          dark_shadow, 1.0f, ui::BevelSides::Left | ui::BevelSides::Right);
/// @endcode
void draw_bevel_border(
    Renderer&    renderer,
    const Rect&  bounds,
    BorderStyle  style,
    const Color& light,
    const Color& highlight,
    const Color& shadow,
    const Color& dark_shadow,
    float        step,
    BevelSides   sides = BevelSides::All,
    bool         soft  = false,
    int          rings = 2);

/// Draws the Windows 3.x border around `bounds`, which is lopsided where draw_bevel_border()
/// is symmetric:
///
///   raised:   1px `frame` on all four sides (outermost)
///             1px `highlight` on the top and left, inside it
///             2px `shadow` on the right and bottom, inside it
///   pressed:  1px `frame` on all four sides
///             1px `shadow` on the top and left, and no bottom or right edge
///
/// Content inside this border sits off center, 2px in from the top and left and 3px from the
/// right and bottom, which suits the era's pressed content offset of one pixel right and down.
///
/// `radius` rounds the frame only, for the push button, the era's one round-cornered surface.
/// The highlight and shadow inside the frame stay straight. Pass 0 for every other surface. `step` is the width of the 1px units above, in DIPs. Flat and Rounded styles draw
/// nothing.
///
/// Example:
/// @code
/// const ui::Color frame     = ui::colors::Black;
/// const ui::Color highlight = ui::colors::White;
/// const ui::Color shadow    = ui::Color::from_rgba(128, 128, 128);
/// const ui::Rect  bounds{0, 0, 75, 23};
/// const bool      pressed = false;
///
/// // A Windows 3.1 push button: rounded frame, raised at rest, pressed while held.
/// const ui::BorderStyle style = pressed ? ui::BorderStyle::Sunken : ui::BorderStyle::Raised;
/// ui::draw_win3x_border(renderer, bounds, style, frame, highlight, shadow, 1.0f, /*radius=*/2.0f);
/// @endcode
void draw_win3x_border(Renderer&    renderer,
                       const Rect&  bounds,
                       BorderStyle  style,
                       const Color& frame,
                       const Color& highlight,
                       const Color& shadow,
                       float        step,
                       float        radius = 0.0f);

/// The classic radio button, drawn per device pixel at the target's own scale
/// (Renderer::device_scale()): the sunken bevel made round, two one-pixel rings split along the
/// diagonal from the top-right to the bottom-left corner, around a `well` disc.
///
///   outer ring: `shadow` on the top/left half, `highlight` on the bottom/right
///   inner ring: `dark_shadow` on the top/left half, `light` on the bottom/right
///   dot:        `dot`, a disc a little under half the ring's width, when set
///
/// At 96 DPI the ring is 12 x 12 inside a 13 x 13 `box`, with the spare column on the left and
/// the spare row at the bottom, and the dot is 4 x 4 with its corners cut, matching the classic
/// radio button pixel for pixel. Larger scales draw the same shape at their own size (a 14 px
/// ring at 125 %, 18 at 150 %, 20 at 175 %) instead of scaling the 12 px one. Each ring stays one
/// device pixel wide up to 200 %, where it becomes two.
///
/// Example:
/// @code
/// // A checked Windows 95 radio button.
/// ui::draw_classic_radio(renderer, ui::Rect{0, 0, 13, 13},
///                        ui::Color::from_rgba(223, 223, 223), ui::colors::White,
///                        ui::Color::from_rgba(128, 128, 128), ui::colors::Black,
///                        ui::colors::White, ui::colors::Black);
/// @endcode
void draw_classic_radio(Renderer&                   renderer,
                        const Rect&                 box,
                        const Color&                light,
                        const Color&                highlight,
                        const Color&                shadow,
                        const Color&                dark_shadow,
                        const Color&                well,
                        const std::optional<Color>& dot);

/// Which way a solid classic arrow points (draw_classic_arrow()).
enum class ArrowDirection : std::uint8_t
{
    Left,  ///< A scroll bar's left arrow.
    Up,    ///< A scroll bar's or spin button's up arrow.
    Right, ///< A scroll bar's right arrow, a menu's cascade arrow.
    Down,  ///< A scroll bar's, spin button's or combo box's down arrow.
};

/// The classic solid arrow: a 45-degree triangle `depth` DIPs deep with a base of
/// `2 * depth - 1`, centered in `area` and drawn per device pixel (Renderer::device_scale()), so
/// its sides stay true diagonals at any scale. The default depth 4 is the 7 x 4 arrow every
/// classic scroll bar, spin button, combo box and drop-down carries at 96 DPI.
///
/// Example:
/// @code
/// // A combo box's drop button arrow, and a menu item's cascade arrow.
/// const ui::Rect button_rect{104, 2, 16, 17};
/// const ui::Rect cascade_rect{140, 0, 16, 18};
/// ui::draw_classic_arrow(renderer, button_rect, ui::ArrowDirection::Down, ui::colors::Black);
/// ui::draw_classic_arrow(renderer, cascade_rect, ui::ArrowDirection::Right, ui::colors::Black);
/// @endcode
void draw_classic_arrow(Renderer& renderer, const Rect& area, ArrowDirection direction, const Color& color, float depth = 4.0f);

/// The classic tree view's expand button: a 9 x 9 box in a one-pixel `frame`
/// around a `well`, holding a 5 px plus while collapsed or a minus while expanded, in `sign`.
/// Centered in `area` and drawn per device pixel like draw_classic_arrow().
///
/// Example:
/// @code
/// const ui::Rect chevron_rect{0, 0, 16, 16};
/// const bool     node_expanded = true;
/// ui::draw_classic_tree_button(renderer, chevron_rect, node_expanded,
///                              ui::Color::from_rgba(128, 128, 128), ui::colors::White, ui::colors::Black);
/// @endcode
void draw_classic_tree_button(Renderer&    renderer,
                              const Rect&  area,
                              bool         expanded,
                              const Color& frame,
                              const Color& well,
                              const Color& sign);

} // namespace ui
