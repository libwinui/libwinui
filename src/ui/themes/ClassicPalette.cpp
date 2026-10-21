// SPDX-License-Identifier: MIT
#include "winui/ui/themes/ClassicPalette.hpp"

#include "winui/core/Color.hpp"

#include <algorithm>

namespace ui
{
namespace
{

struct HLS
{
    float h = 0.0f;
    float l = 0.0f;
    float s = 0.0f;
};

HLS rgb_to_hls(const Color& c)
{
    const float mx = std::max({c.r, c.g, c.b});
    const float mn = std::min({c.r, c.g, c.b});
    HLS         hls;
    hls.l = (mx + mn) * 0.5f;
    if (mx != mn)
    {
        const float d = mx - mn;
        hls.s         = hls.l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
        if (mx == c.r)
        {
            hls.h = (c.g - c.b) / d + (c.g < c.b ? 6.0f : 0.0f);
        }
        else if (mx == c.g)
        {
            hls.h = (c.b - c.r) / d + 2.0f;
        }
        else
        {
            hls.h = (c.r - c.g) / d + 4.0f;
        }
        hls.h /= 6.0f;
    }
    return hls;
}

float hue_to_rgb(float p, float q, float t)
{
    if (t < 0.0f)
    {
        t += 1.0f;
    }
    if (t > 1.0f)
    {
        t -= 1.0f;
    }
    if (t < 1.0f / 6.0f)
    {
        return p + (q - p) * 6.0f * t;
    }
    if (t < 0.5f)
    {
        return q;
    }
    if (t < 2.0f / 3.0f)
    {
        return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    }
    return p;
}

Color hls_to_rgb(const HLS& hls, float alpha)
{
    float r, g, b;
    if (hls.s == 0.0f)
    {
        r = g = b = hls.l;
    }
    else
    {
        const float q = hls.l < 0.5f ? hls.l * (1.0f + hls.s) : hls.l + hls.s - hls.l * hls.s;
        const float p = 2.0f * hls.l - q;
        r             = hue_to_rgb(p, q, hls.h + 1.0f / 3.0f);
        g             = hue_to_rgb(p, q, hls.h);
        b             = hue_to_rgb(p, q, hls.h - 1.0f / 3.0f);
    }
    return {std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f), alpha};
}

} // namespace

Color classic_lighter(const Color& c, float percent)
{
    HLS hls = rgb_to_hls(c);
    // HighlightAdjustment = +500 (of a 0-1000 scale) is the fixed maximum move toward
    // white; `percent` scales how much of that maximum this call asks for.
    const float n = std::clamp(percent, 0.0f, 1.0f) * 0.5f;
    hls.l         = std::clamp(hls.l * (1.0f - n) + n, 0.0f, 1.0f);
    return hls_to_rgb(hls, c.a);
}

Color classic_darker(const Color& c, float percent)
{
    HLS hls = rgb_to_hls(c);
    // ShadowAdjustment = -333 (of the same scale) is the fixed maximum move toward black.
    const float n = std::clamp(percent, 0.0f, 1.0f) * 0.333f;
    hls.l         = std::clamp(hls.l * (1.0f - n), 0.0f, 1.0f);
    return hls_to_rgb(hls, c.a);
}

ClassicBevelRamp classic_bevel_ramp_from_face(const Color& face)
{
    // The shipped Windows Standard ramps, kept as is as WinForms' ControlPaint does: no lighten
    // function yields 95/98's extra 3DLIGHT or 2000's 3DLIGHT == face.
    constexpr Color Windows95Face   = Color::from_argb(0xFFC0C0C0); // Windows 95 and 98 SE, byte-identical
    constexpr Color Windows2000Face = Color::from_argb(0xFFD4D0C8);
    if (face == Windows95Face)
    {
        return ClassicBevelRamp{Color::from_argb(0xFFDFDFDF), colors::White, Color::from_argb(0xFF808080), colors::Black};
    }
    if (face == Windows2000Face)
    {
        // 3DLIGHT == BTNFACE, and 3DDKSHADOW softened off pure black: Windows 2000's bevel is
        // lower-contrast at both ends of the ramp than Windows 95/98's.
        return ClassicBevelRamp{face, colors::White, Color::from_argb(0xFF808080), Color::from_argb(0xFF404040)};
    }

    // Any other face is a user scheme, ramped in the Windows 95 manner. Nothing reproduces
    // Windows 2000's flatter ramp.
    return ClassicBevelRamp{
        classic_lighter(face, 0.5f),
        classic_lighter(face, 1.0f),
        classic_darker(face, 0.5f),
        classic_darker(face, 1.0f),
    };
}

ClassicPalette ClassicPalette::from_face(const Color& face)
{
    // Each field is the GetSysColor index named beside it. The bevel ramp is COLOR_3DLIGHT,
    // COLOR_BTNHIGHLIGHT, COLOR_BTNSHADOW and COLOR_3DDKSHADOW; the caption fields are
    // COLOR_(GRADIENT)(IN)ACTIVECAPTION, COLOR_(INACTIVE)CAPTIONTEXT and COLOR_(IN)ACTIVEBORDER.
    ClassicPalette p;
    p.face      = face;          // COLOR_BTNFACE
    p.face_text = colors::Black; // COLOR_BTNTEXT

    p.window       = colors::White; // COLOR_WINDOW
    p.window_text  = colors::Black; // COLOR_WINDOWTEXT
    p.window_frame = colors::Black; // COLOR_WINDOWFRAME

    p.selection      = Color::from_argb(0xFF000080); // COLOR_HIGHLIGHT, the classic navy
    p.selection_text = colors::White;                // COLOR_HIGHLIGHTTEXT
    p.gray_text      = Color::from_argb(0xFF808080); // COLOR_GRAYTEXT

    p.menu      = face;          // COLOR_MENU
    p.menu_text = colors::Black; // COLOR_MENUTEXT

    p.info_bk   = Color::from_argb(0xFFFFFFE1); // COLOR_INFOBK, the 1995 tooltip yellow, unchanged since
    p.info_text = colors::Black;                // COLOR_INFOTEXT
    p.hot_light = Color::from_argb(0xFF0000FF); // COLOR_HOTLIGHT
    return p;
}

ClassicPalette ClassicPalette::windows_standard()
{
    return from_face(Color::from_argb(0xFFC0C0C0));
}

ClassicPalette ClassicPalette::windows_standard_2000()
{
    ClassicPalette p = from_face(Color::from_argb(0xFFD4D0C8));
    // What Windows 2000 changes from the 95/98 row. The face alone is not enough:
    // paired with 95's navy selection it is a hybrid that never shipped.
    p.selection = Color::from_argb(0xFF0A246A); // HIGHLIGHT, the same value as ACTIVECAPTION
    p.hot_light = Color::from_argb(0xFF000080); // HOTLIGHT, darkened from Windows 98's pure blue
    // Control Panel\Colors in HIVEDEF.INF: the first Windows Standard with a real caption gradient.
    p.caption = ClassicCaption{.active            = Color::from_argb(0xFF0A246A),
                               .gradient_active   = Color::from_argb(0xFFA6CAF0),
                               .inactive          = Color::from_argb(0xFF808080),
                               .gradient_inactive = Color::from_argb(0xFFC0C0C0),
                               .text              = colors::White,
                               .inactive_text     = Color::from_argb(0xFFD4D0C8),
                               .active_border     = Color::from_argb(0xFFD4D0C8),
                               .inactive_border   = Color::from_argb(0xFFD4D0C8)};
    return p;
}

ClassicPalette ClassicPalette::high_contrast_black_98()
{
    ClassicPalette p;
    p.face           = colors::Black;                                 // BTNFACE
    p.face_text      = colors::White;                                 // BTNTEXT
    p.window         = colors::Black;                                 // WINDOW
    p.window_text    = colors::White;                                 // WINDOWTEXT
    p.window_frame   = colors::White;                                 // WINDOWFRAME
    p.selection      = Color::from_argb(0xFF800080);                  // HIGHLIGHT, also ACTIVECAPTION
    p.selection_text = colors::White;                                 // HIGHLIGHTTEXT
    p.gray_text      = Color::from_argb(0xFF00FF00);                  // GRAYTEXT
    p.menu           = colors::Black;                                 // MENU
    p.menu_text      = colors::White;                                 // MENUTEXT
    p.info_bk        = colors::White;                                 // INFOBK
    p.info_text      = colors::Black;                                 // INFOTEXT
    p.hot_light      = Color::from_argb(0xFF0000FF);                  // HOTLIGHT
    p.bevel          = ClassicBevelRamp{Color::from_argb(0xFFD0B0B8), // 3DLIGHT
                                        Color::from_argb(0xFFC0C0C0), // BTNHIGHLIGHT
                                        Color::from_argb(0xFF808080), // BTNSHADOW
                                        colors::White};               // 3DDKSHADOW
    // Purple when active, green when not, a yellow frame, and bigger caption buttons. Windows
    // 2000 ships the same.
    p.caption = ClassicCaption{.active            = Color::from_argb(0xFF800080),
                               .gradient_active   = Color::from_argb(0xFF800080),
                               .inactive          = Color::from_argb(0xFF008000),
                               .gradient_inactive = Color::from_argb(0xFF008000),
                               .text              = colors::White,
                               .inactive_text     = colors::White,
                               .active_border     = Color::from_argb(0xFFFFFF00),
                               .inactive_border   = Color::from_argb(0xFF008000),
                               .button_cell       = 20.0f};
    return p;
}

ClassicPalette ClassicPalette::high_contrast_black_2000()
{
    ClassicPalette p = high_contrast_black_98();
    p.info_bk        = colors::Black;                // INFOBK
    p.info_text      = colors::White;                // INFOTEXT
    p.hot_light      = Color::from_argb(0xFF800080); // HOTLIGHT
    p.bevel->light   = colors::White;                // 3DLIGHT
    return p;
}

ClassicPalette ClassicPalette::monochrome_3x()
{
    ClassicPalette p = windows_standard(); // the silver face, its ramp and black button text
    p.window         = colors::White;
    p.window_text    = colors::Black;
    p.window_frame   = colors::Black;
    p.selection      = colors::Black; // Hilight, also ActiveTitle
    p.selection_text = colors::White;
    p.gray_text      = Color::from_argb(0xFF808080);
    p.menu           = colors::White;
    p.menu_text      = colors::Black;
    p.info_bk        = p.window;
    p.info_text      = p.window_text;
    p.hot_light      = p.window_text;
    return p;
}

} // namespace ui
