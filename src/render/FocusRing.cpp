// SPDX-License-Identifier: MIT
#include "winui/render/FocusRing.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"

#include <algorithm>

#include <cmath>

namespace ui
{
namespace
{

void draw_dashed_line(Renderer& renderer, Point from, Point to, const Color& color, float dash_length, float phase)
{
    const float dx     = to.x - from.x;
    const float dy     = to.y - from.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0f || dash_length <= 0.0f)
    {
        return;
    }
    const float ux     = dx / length;
    const float uy     = dy / length;
    const float period = dash_length * 2.0f;
    // `phase` (Metro's second dashed pass) shifts where the first dash starts. A dash starting before
    // the line's origin is clipped rather than skipped, so both passes cover the same span.
    const float start = std::fmod(phase, period) - period;

    for (float pos = start; pos < length; pos += period)
    {
        const float dash_start = std::max(pos, 0.0f);
        const float dash_end   = std::min(pos + dash_length, length);
        if (dash_end <= dash_start)
        {
            continue;
        }
        renderer.stroke_line({from.x + ux * dash_start, from.y + uy * dash_start},
                             {from.x + ux * dash_end, from.y + uy * dash_end},
                             color,
                             1.0f);
    }
}

} // namespace

void draw_dotted_rect(Renderer& renderer, const Rect& bounds, const Color& color, float dash_length, float phase)
{
    // 1px dashes at phase 0 match DrawFocusRect, whose dots alternate every pixel, not every two.
    draw_dashed_line(renderer, {bounds.left(), bounds.top()}, {bounds.right(), bounds.top()}, color, dash_length, phase);
    draw_dashed_line(renderer, {bounds.right(), bounds.top()}, {bounds.right(), bounds.bottom()}, color, dash_length, phase);
    draw_dashed_line(renderer, {bounds.right(), bounds.bottom()}, {bounds.left(), bounds.bottom()}, color, dash_length, phase);
    draw_dashed_line(renderer, {bounds.left(), bounds.bottom()}, {bounds.left(), bounds.top()}, color, dash_length, phase);
}

void draw_focus_ring_rings(Renderer& renderer, const Rect& bounds, float radius, const FocusRingStyle& style)
{
    // Each focus ring is painted as a border inside its layout rect (a -3 margin), so the rings
    // touch. stroke_rounded_rect centers on the path, so each path is inset half its width into its band.
    const float outer_offset = style.margin - style.outer_width * 0.5f;
    const Rect  outer        = bounds.inflate(outer_offset);
    renderer.stroke_rounded_rect(outer, radius + outer_offset, style.outer_color, style.outer_width);

    const float inner_offset = style.margin - style.outer_width - style.inner_width * 0.5f;
    const Rect  inner        = bounds.inflate(inner_offset);
    renderer.stroke_rounded_rect(inner, radius + inner_offset, style.inner_color, style.inner_width);
}

void draw_focus_ring_at(Renderer& renderer, const Rect& bounds, const Thickness& outward, float radius, const FocusRingStyle& style)
{
    if (style.kind != FocusVisual::FluentRings)
    {
        draw_focus_ring(renderer, bounds, radius, style);
        return;
    }
    // draw_focus_ring_rings() goes `style.margin` out from the rect it is given: hand it the rect
    // that leaves each side where `outward` asks.
    const Rect seed = bounds.inset(
        Edges{.left   = style.margin - outward.left,
              .top    = style.margin - outward.top,
              .right  = style.margin - outward.right,
              .bottom = style.margin - outward.bottom});
    draw_focus_ring_rings(renderer, seed, radius, style);
}

void draw_focus_ring(Renderer& renderer, const Rect& bounds, float radius, const FocusRingStyle& style)
{
    switch (style.kind)
    {
        case FocusVisual::ClassicDotted:
            draw_dotted_rect(renderer, bounds.inset(Thickness{4.0f}), style.outer_color);
            return;
        case FocusVisual::MetroDashedPair:
        {
            const Rect inset = bounds.inset(Thickness{4.0f});
            draw_dotted_rect(renderer, inset, style.outer_color, style.dash_length, 0.0f);
            draw_dotted_rect(renderer, inset, style.inner_color, style.dash_length, style.dash_length);
            return;
        }
        case FocusVisual::FluentRings:
            break;
    }
    draw_focus_ring_rings(renderer, bounds, radius, style);
}

} // namespace ui
