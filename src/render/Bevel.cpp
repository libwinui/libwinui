// SPDX-License-Identifier: MIT
#include "winui/render/Bevel.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

void draw_bevel_border(
    Renderer&    renderer,
    const Rect&  bounds,
    BorderStyle  style,
    const Color& light,
    const Color& highlight,
    const Color& shadow,
    const Color& dark_shadow,
    float        step,
    BevelSides   sides,
    bool         soft,
    int          rings)
{
    if (style != BorderStyle::Raised && style != BorderStyle::Sunken)
    {
        return;
    }

    const bool  raised = style == BorderStyle::Raised;
    const float half   = step * 0.5f; // stroke_line centers the stroke on the path
    const float inner  = step + half; // one ring further in from the edge

    // DrawEdge: EDGE_RAISED puts 3DLIGHT/BTNHIGHLIGHT on the top/left (outer/inner) and 3DDKSHADOW/
    // BTNSHADOW on the bottom/right, EDGE_SUNKEN mirrors it, and BF_SOFT swaps the two top/left rings.
    // `light`, `highlight`, `shadow` and `dark_shadow` are COLOR_3DLIGHT, COLOR_BTNHIGHLIGHT,
    // COLOR_BTNSHADOW and COLOR_3DDKSHADOW, `sides` is the BF_LEFT/TOP/RIGHT/BOTTOM half of uFlags,
    // and `soft` is BF_SOFT, which DrawFrameControl(DFC_BUTTON, DFCS_BUTTONPUSH) always passes.
    const Color& top_left_outer     = soft ? (raised ? highlight : dark_shadow) : (raised ? light : shadow);
    const Color& top_left_inner     = soft ? (raised ? light : shadow) : (raised ? highlight : dark_shadow);
    const Color& bottom_right_outer = raised ? dark_shadow : highlight;
    const Color& bottom_right_inner = raised ? shadow : light;

    const bool top    = has_side(sides, BevelSides::Top);
    const bool left   = has_side(sides, BevelSides::Left);
    const bool bottom = has_side(sides, BevelSides::Bottom);
    const bool right  = has_side(sides, BevelSides::Right);

    // The inner ring stops one step short at each corner the outer ring owns, as DrawEdge does when
    // both sides of that corner are drawn (ReactOS user32 draw.c, IntDrawRectEdge).
    const float lt = (top && left) ? step : 0.0f;
    const float rt = (top && right) ? step : 0.0f;
    const float lb = (bottom && left) ? step : 0.0f;
    const float rb = (bottom && right) ? step : 0.0f;

    const bool two_rings = rings > 1;

    if (top)
    {
        renderer.stroke_line({bounds.left(), bounds.top() + half}, {bounds.right(), bounds.top() + half}, top_left_outer, step);
        if (two_rings)
        {
            renderer.stroke_line({bounds.left() + lt, bounds.top() + inner},
                                 {bounds.right() - rt, bounds.top() + inner},
                                 top_left_inner,
                                 step);
        }
    }
    if (left)
    {
        renderer.stroke_line({bounds.left() + half, bounds.top()}, {bounds.left() + half, bounds.bottom()}, top_left_outer, step);
        if (two_rings)
        {
            renderer.stroke_line({bounds.left() + inner, bounds.top() + lt},
                                 {bounds.left() + inner, bounds.bottom() - lb},
                                 top_left_inner,
                                 step);
        }
    }
    if (bottom)
    {
        renderer.stroke_line({bounds.left(), bounds.bottom() - half},
                             {bounds.right(), bounds.bottom() - half},
                             bottom_right_outer,
                             step);
        if (two_rings)
        {
            renderer.stroke_line({bounds.left() + lb, bounds.bottom() - inner},
                                 {bounds.right() - rb, bounds.bottom() - inner},
                                 bottom_right_inner,
                                 step);
        }
    }
    if (right)
    {
        renderer.stroke_line({bounds.right() - half, bounds.top()},
                             {bounds.right() - half, bounds.bottom()},
                             bottom_right_outer,
                             step);
        if (two_rings)
        {
            renderer.stroke_line({bounds.right() - inner, bounds.top() + rt},
                                 {bounds.right() - inner, bounds.bottom() - rb},
                                 bottom_right_inner,
                                 step);
        }
    }
}

void draw_win3x_border(Renderer&    renderer,
                       const Rect&  bounds,
                       BorderStyle  style,
                       const Color& frame,
                       const Color& highlight,
                       const Color& shadow,
                       float        step,
                       float        radius)
{
    if (style != BorderStyle::Raised && style != BorderStyle::Sunken)
    {
        return;
    }

    // Measured from the 34 OBM_* chrome bitmaps in Windows 3.x's VGA.DRV: every 4-bpp one uses
    // exactly these four colors in this arrangement. Windows 3.x has BTNFACE, BTNSHADOW and
    // BTNHIGHLIGHT but no 3DLIGHT or 3DDKSHADOW, so the frame is a color of its own. The push button's
    // round corners are OBM_BTNCORNERS.
    const Rect  inner = bounds.inset(Thickness{step});
    const float half  = step * 0.5f; // both stroke calls center the stroke on the path

    // Stroked on a path pulled in by half a step so the whole ring lands inside `bounds`. A caller that
    // clips to its own rect (SwipeControl) cuts the outer half of a centered stroke, and aliasing drops the rest.
    renderer.stroke_rounded_rect(bounds.inset(Thickness{half}), std::max(0.0f, radius - half), frame, step);

    if (inner.width <= 0.0f || inner.height <= 0.0f)
    {
        return;
    }

    if (style == BorderStyle::Sunken)
    {
        // Pressed, read straight off `OBM_UPARROWD`: no white anywhere, a single 1px shadow
        // edge on the top and left, and the bottom/right edge gone entirely.
        renderer.stroke_line({inner.left(), inner.top() + half}, {inner.right(), inner.top() + half}, shadow, step);
        renderer.stroke_line({inner.left() + half, inner.top()}, {inner.left() + half, inner.bottom()}, shadow, step);
        return;
    }

    // Raised: the shadow runs the full span of its two edges, the highlight is drawn over it and stops
    // one step short of both, as in `OBM_UPARROW`.
    renderer.stroke_line({inner.right() - step, inner.top()}, {inner.right() - step, inner.bottom()}, shadow, step * 2.0f);
    renderer.stroke_line({inner.left(), inner.bottom() - step}, {inner.right(), inner.bottom() - step}, shadow, step * 2.0f);
    renderer.stroke_line({inner.left(), inner.top() + half}, {inner.right() - step, inner.top() + half}, highlight, step);
    renderer.stroke_line({inner.left() + half, inner.top()}, {inner.left() + half, inner.bottom() - step}, highlight, step);
}

namespace
{

// A grid of whole device pixels whose origin is the device pixel nearest `area`'s top-left, so a
// rect built from it lands exactly on pixels once the backend snaps (and is plain integers at 1x).
struct DeviceGrid
{
    float scale  = 1.0f;
    float left   = 0.0f;
    float top    = 0.0f;
    int   width  = 0; // `area`'s extent in device pixels
    int   height = 0;

    DeviceGrid(const Renderer& renderer, const Rect& area) : scale(std::max(renderer.device_scale(), 0.01f))
    {
        const float x0 = std::round(area.left() * scale);
        const float y0 = std::round(area.top() * scale);
        left           = x0 / scale;
        top            = y0 / scale;
        width          = static_cast<int>(std::round(area.right() * scale) - x0);
        height         = static_cast<int>(std::round(area.bottom() * scale) - y0);
    }

    void fill(Renderer& renderer, int x, int y, int w, int h, const Color& color) const
    {
        if (w > 0 && h > 0)
        {
            renderer.fill_rounded_rect(Rect{left + static_cast<float>(x) / scale,
                                            top + static_cast<float>(y) / scale,
                                            static_cast<float>(w) / scale,
                                            static_cast<float>(h) / scale},
                                       0.0f,
                                       color);
        }
    }
};

// A disc `d` device pixels across: every pixel whose center lies within d/2 of the middle. At 12
// and 4 this is exactly the shape of the era's radio ring and dot.
bool in_disc(int x, int y, int d)
{
    const float c  = static_cast<float>(d) * 0.5f;
    const float dx = static_cast<float>(x) + 0.5f - c;
    const float dy = static_cast<float>(y) + 0.5f - c;
    return dx * dx + dy * dy <= c * c;
}

} // namespace

void draw_classic_radio(Renderer&                   renderer,
                        const Rect&                 box,
                        const Color&                light,
                        const Color&                highlight,
                        const Color&                shadow,
                        const Color&                dark_shadow,
                        const Color&                well,
                        const std::optional<Color>& dot)
{
    const DeviceGrid grid{renderer, box};
    // At 13 px this is pixel for pixel the five Marlett layers DrawFrameControl(DFCS_BUTTONRADIO)
    // stacks: BTNSHADOW/BTNHIGHLIGHT outside 3DDKSHADOW/3DLIGHT.
    // The ring is 12/13 of the box, rounded down to even, and each ring one device pixel until
    // 200 %: Marlett's own sizes at 13, 16, 20 and 23 px.
    const int d = (static_cast<int>(std::floor(static_cast<float>(std::min(grid.width, grid.height)) * 12.0f / 13.0f)) / 2) *
                  2;
    if (d < 4)
    {
        return;
    }
    const int ring = std::max(1, static_cast<int>(std::floor(grid.scale)));
    // The spare pixels go to the left and the bottom, where DrawFrameControl leaves them at 13 px.
    const int spare_x = grid.width - d;
    const int spare_y = grid.height - d;
    const int ox      = spare_x - spare_x / 2;
    const int oy      = spare_y / 2;

    // Each pixel's ring layer, peeled off the disc one four-neighbor boundary at a time: below
    // `ring` the outer ring, below `layers` the inner one, `layers` the well, -1 outside.
    const int        layers = 2 * ring;
    std::vector<int> layer(static_cast<std::size_t>(d) * static_cast<std::size_t>(d), -1);
    auto             at = [&](int x, int y) -> int& { return layer[static_cast<std::size_t>(y * d + x)]; };
    for (int y = 0; y < d; ++y)
    {
        for (int x = 0; x < d; ++x)
        {
            if (in_disc(x, y, d))
            {
                at(x, y) = layers;
            }
        }
    }
    for (int peel = 0; peel < layers; ++peel)
    {
        auto remaining = [&](int x, int y) { return x >= 0 && y >= 0 && x < d && y < d && at(x, y) >= peel; };
        std::vector<std::pair<int, int>> edge;
        for (int y = 0; y < d; ++y)
        {
            for (int x = 0; x < d; ++x)
            {
                if (at(x, y) == layers &&
                    (!remaining(x - 1, y) || !remaining(x + 1, y) || !remaining(x, y - 1) || !remaining(x, y + 1)))
                {
                    edge.emplace_back(x, y);
                }
            }
        }
        for (const auto& [x, y] : edge)
        {
            at(x, y) = peel;
        }
    }

    // The dot is a little under half the ring, even so it centers: 4 at 12, 6 at 14, 8 at 18 and 20.
    const int dot_d   = ((d / 2 - 1) / 2) * 2;
    const int dot_off = (d - dot_d) / 2;

    // The top/left half of the split along the top-right to bottom-left diagonal. A pixel on that
    // diagonal goes to the top/left half above the other diagonal, to the bottom/right one below it.
    auto lit_half = [&](int x, int y)
    {
        const int side = x + y + 1 - d;
        return side < 0 || (side == 0 && x > y);
    };
    auto color_of = [&](int x, int y) -> const Color*
    {
        const int l = at(x, y);
        if (l < 0)
        {
            return nullptr;
        }
        if (l < ring)
        {
            return lit_half(x, y) ? &shadow : &highlight;
        }
        if (l < layers)
        {
            return lit_half(x, y) ? &dark_shadow : &light;
        }
        if (dot && x >= dot_off && y >= dot_off && x < dot_off + dot_d && y < dot_off + dot_d &&
            in_disc(x - dot_off, y - dot_off, dot_d))
        {
            return &*dot;
        }
        return &well;
    };

    // One rect per run of one color along a row.
    for (int y = 0; y < d; ++y)
    {
        int x = 0;
        while (x < d)
        {
            const Color* c   = color_of(x, y);
            int          end = x + 1;
            while (end < d && color_of(end, y) == c)
            {
                ++end;
            }
            if (c)
            {
                grid.fill(renderer, ox + x, oy + y, end - x, 1, *c);
            }
            x = end;
        }
    }
}

void draw_classic_arrow(Renderer& renderer, const Rect& area, ArrowDirection direction, const Color& color, float depth)
{
    // The Marlett arrows: Left is `3`, Up `5`, Right `4` (and the menu cascade `8`), Down `6`.
    const DeviceGrid grid{renderer, area};
    const int        deep     = std::max(1, static_cast<int>(std::round(depth * grid.scale)));
    const int        base     = 2 * deep - 1;
    const bool       vertical = direction == ArrowDirection::Up || direction == ArrowDirection::Down;
    const int        across   = vertical ? grid.width : grid.height;
    const int        along    = vertical ? grid.height : grid.width;
    // Centered, an odd pixel along the axis going the way the arrow points: Marlett's `6` sits a
    // pixel lower in its cell than `5`.
    const bool forward     = direction == ArrowDirection::Down || direction == ArrowDirection::Right;
    const int  base_start  = (across - base) / 2;
    const int  along_start = forward ? (along - deep + 1) / 2 : (along - deep) / 2;
    for (int i = 0; i < deep; ++i)
    {
        // `i` steps from the base: `base - 2i` long, `i` in from either end.
        const int step = forward ? along_start + i : along_start + deep - 1 - i;
        const int from = base_start + i;
        const int len  = base - 2 * i;
        if (vertical)
        {
            grid.fill(renderer, from, step, len, 1, color);
        }
        else
        {
            grid.fill(renderer, step, from, 1, len, color);
        }
    }
}

void draw_classic_tree_button(Renderer& renderer, const Rect& area, bool expanded, const Color& frame, const Color& well, const Color& sign)
{
    // The TVS_HASBUTTONS expand button.
    const DeviceGrid grid{renderer, area};
    // 9 px at 96 DPI and odd at every scale, so the sign has a middle pixel.
    const int box = 2 * static_cast<int>(std::round(4.0f * grid.scale)) + 1;
    if (box > grid.width || box > grid.height)
    {
        return;
    }
    const int edge  = std::max(1, static_cast<int>(std::floor(grid.scale)));
    const int x0    = (grid.width - box) / 2;
    const int y0    = (grid.height - box) / 2;
    const int mid   = box / 2;
    const int arm   = mid - edge - 1;                  // a one-pixel gap between the sign and the frame
    const int width = edge % 2 == 1 ? edge : edge - 1; // odd, so the bar centers
    grid.fill(renderer, x0, y0, box, box, frame);
    grid.fill(renderer, x0 + edge, y0 + edge, box - 2 * edge, box - 2 * edge, well);
    grid.fill(renderer, x0 + mid - arm, y0 + mid - width / 2, 2 * arm + 1, width, sign);
    if (!expanded)
    {
        grid.fill(renderer, x0 + mid - width / 2, y0 + mid - arm, width, 2 * arm + 1, sign);
    }
}

} // namespace ui
