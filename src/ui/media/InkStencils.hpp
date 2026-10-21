// SPDX-License-Identifier: MIT
// The ruler and the protractor an InkCanvasControl shows: where a point falls on one, how a stroke
// snaps to it, and how it is drawn.
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/media/InkCanvas.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <optional>

namespace ui::detail
{

// Where a point falls on a stencil: on its body (which drags it), within reach of the edge a stroke
// snaps to, or off it.
enum class InkStencilHit
{
    None,
    Body,
    Edge,
};

// A stencil a point fell on: which one, and for the ruler which long edge (-1 top, 1 bottom).
struct InkStencilTarget
{
    bool          ruler = true;
    InkStencilHit hit   = InkStencilHit::None;
    int           side  = 0;
};

// `p` (canvas DIPs) against the ruler and the protractor centered at the given points; the ruler
// first, since it is drawn on top. A hidden stencil is never hit.
[[nodiscard]] InkStencilTarget ink_stencil_hit(
    Point                p,
    const InkRuler&      ruler,
    Point                ruler_center,
    const InkProtractor& protractor,
    Point                protractor_center);

// Where `p` lands once drawn along the edge `target` names, `half_nib` outside it.
[[nodiscard]] Point ink_stencil_snap(
    Point                   p,
    const InkStencilTarget& target,
    const InkRuler&         ruler,
    Point                   ruler_center,
    const InkProtractor&    protractor,
    Point                   protractor_center,
    float                   half_nib);

// Draws a visible stencil in canvas coordinates, centered at `center`.
void draw_ink_ruler(Renderer& renderer, const Theme& theme, const InkRuler& ruler, Point center);
void draw_ink_protractor(Renderer& renderer, const Theme& theme, const InkProtractor& protractor, Point center);

} // namespace ui::detail
