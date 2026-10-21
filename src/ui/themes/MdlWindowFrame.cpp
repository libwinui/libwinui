// SPDX-License-Identifier: MIT
// Windows 8's window frame, measured from the Windows 8 style's DWM atlas: one flat color for the
// frame and the caption, a slightly darker 2 px outer edge, a centered title, and flat caption
// buttons, blue when hot and red for close.
#include "ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Text.hpp"

#include <vector>

#include <cstddef>
#include <cstdint>

namespace ui::detail
{
namespace
{

constexpr Color mdl_argb(std::uint32_t v)
{
    return Color::from_argb(v);
}

LinearGradientBrush flat(const Color& c)
{
    return LinearGradientBrush{{GradientStop{0.0f, c}, GradientStop{1.0f, c}}, GradientAxis::Vertical};
}

// `c` under black at `alpha`, the atlas's frame overlay.
Color darkened(const Color& c, float alpha)
{
    return Color{c.r * (1.0f - alpha), c.g * (1.0f - alpha), c.b * (1.0f - alpha), 1.0f};
}

// `c` at `amount` over white.
Color tinted(const Color& c, float amount)
{
    return Color{1.0f - (1.0f - c.r) * amount, 1.0f - (1.0f - c.g) * amount, 1.0f - (1.0f - c.b) * amount, 1.0f};
}

// The caption ramp, one stop per row: the outer edge's two rows, then the color.
LinearGradientBrush caption_rows(const Color& edge, const Color& fill, float rows)
{
    LinearGradientBrush brush{{}, GradientAxis::Vertical};
    for (int row = 0; row < static_cast<int>(rows); ++row)
    {
        brush.stops.push_back(GradientStop{(static_cast<float>(row) + 0.5f) / rows, row < 2 ? edge : fill});
    }
    return brush;
}

// A strip's columns or rows from the outside in: the edge's two, then the color.
std::vector<Color> strip(const Color& edge, const Color& fill, std::size_t width, bool edge_first)
{
    std::vector<Color> out(width, fill);
    for (std::size_t i = 0; i < 2 && i < width; ++i)
    {
        out[edge_first ? i : width - 1 - i] = edge;
    }
    return out;
}

} // namespace

ControlChrome::WindowFrame mdl_window_frame(const Color& color)
{
    using Face = ControlChrome::WindowFrame::Face;
    ControlChrome::WindowFrame frame;
    frame.kind = ControlChrome::WindowFrame::Kind::Metro;

    // The window color fills frame and caption alike. Inactive, the style's colorization opacity
    // drops from 100 to 20, which leaves a pale tint. The frame part's overlay darkens the outer
    // 2 px by black at 10 %.
    const Color inactive = tinted(color, 0.2f);
    const Color edge     = darkened(color, 0.1f);
    const Color edge_off = darkened(inactive, 0.1f);

    // SizingBorderWidth 1 and PaddedBorderWidth 4: an 8 px frame. CaptionBarHeight 22 gives a
    // 23 px caption, so the content starts 31 px down.
    frame.border           = 8.0f;
    frame.caption_height   = 31.0f;
    frame.caption_rule     = 0.0f;
    frame.active_caption   = caption_rows(edge, color, frame.caption_height);
    frame.inactive_caption = caption_rows(edge_off, inactive, frame.caption_height);
    frame.active_left      = strip(edge, color, 8, true);
    frame.inactive_left    = strip(edge_off, inactive, 8, true);
    frame.active_right     = strip(edge, color, 8, false);
    frame.inactive_right   = strip(edge_off, inactive, 8, false);
    frame.active_bottom    = strip(edge, color, 8, false);
    frame.inactive_bottom  = strip(edge_off, inactive, 8, false);
    frame.active_border    = color;
    frame.inactive_border  = inactive;

    // The caption's TEXTCOLOR 40 40 40, active or not, Segoe UI 9 pt, centered.
    frame.active_text    = mdl_argb(0xFF282828);
    frame.inactive_text  = mdl_argb(0xFF282828);
    frame.font_family    = "Segoe UI";
    frame.font_size      = 12.0f;
    frame.font_weight    = FontWeight::Regular;
    frame.title_centered = true;
    frame.icon_size      = 16.0f;
    frame.icon_left      = 9.0f;
    frame.title_left     = 5.0f;

    // The art's filled extents: a 28 x 20 minimize, a 30 x 20 maximize and a 47 x 20 close, 3 px
    // and 2 px apart, hanging a pixel below the top edge.
    frame.button         = Size{30.0f, 20.0f};
    frame.minimize_width = 28.0f;
    frame.close_width    = 47.0f;
    frame.button_top     = 1.0f;
    frame.button_right   = 7.0f;
    frame.button_gap     = 3.0f;
    frame.close_gap      = 2.0f;

    // Normal, hot, pressed, disabled, then the same on an inactive window. Minimize and maximize
    // are clear at rest with a 40 40 40 glyph, the glyph white while hot or pressed and at half
    // alpha while disabled. Close is red; on an inactive window it rests as black at 20 %.
    const Color clear        = colors::Transparent;
    const Color glyph        = mdl_argb(0xFF282828);
    const Color glyph_off    = mdl_argb(0x80272727);
    const Color white        = colors::White;
    const Color white_off    = mdl_argb(0x80FFFFFF);
    const Face  caption_hot  = Face{clear, flat(mdl_argb(0xFF3665B3)), white};
    const Face  caption_down = Face{clear, flat(mdl_argb(0xFF3D6099)), white};
    frame.caption_faces      = {Face{clear, flat(clear), glyph},
                                caption_hot,
                                caption_down,
                                Face{clear, flat(clear), glyph_off},
                                Face{clear, flat(clear), glyph},
                                caption_hot,
                                caption_down,
                                Face{clear, flat(clear), glyph_off}};
    const Face close_hot     = Face{clear, flat(mdl_argb(0xFFE04343)), white};
    const Face close_down    = Face{clear, flat(mdl_argb(0xFF993D3D)), white};
    frame.close_faces        = {Face{clear, flat(mdl_argb(0xFFC75050)), white},
                                close_hot,
                                close_down,
                                Face{clear, flat(mdl_argb(0x80C74F4F)), white_off},
                                Face{clear, flat(mdl_argb(0x33000000)), white},
                                close_hot,
                                close_down,
                                Face{clear, flat(mdl_argb(0x33000000)), white_off}};
    return frame;
}

} // namespace ui::detail
