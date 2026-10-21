// SPDX-License-Identifier: MIT
// Aero Basic's window frame, the opaque Windows 7 frame drawn without the desktop compositor,
// measured from the Basic art the style still carries: the caption one stop per pixel row, the side
// and bottom strips one color per pixel, and each caption button state's ramp, edges and glyph fill.
#include "ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Text.hpp"

#include <cstdint>

namespace ui::detail
{
namespace
{
constexpr Color aero_argb(std::uint32_t v)
{
    return Color::from_argb(v);
}
} // namespace

ControlChrome::WindowFrame aero_basic_window_frame()
{
    using Face = ControlChrome::WindowFrame::Face;
    ControlChrome::WindowFrame frame;
    frame.kind = ControlChrome::WindowFrame::Kind::AeroBasic;
    // SizingBorderWidth 1 and PaddedBorderWidth 4: an 8 px frame. The caption art carries its top
    // 8 px above the 22 px bar (CaptionBarHeight 21, plus one).
    frame.border         = 8.0f;
    frame.caption_height = 30.0f;
    // The Basic caption art's rounded top corners, cut from the window as Windows 7 Basic did:
    // 4, 2, 1 and 1 px off each corner, and the art's own pixels in those rows. Glass leaves the
    // cut pixels unpainted instead.
    frame.top_corner_cut = {4, 2, 1, 1};
    frame.active_top_left =
        {6,
         {
             aero_argb(0x00000000), aero_argb(0x00000000), aero_argb(0x00000000), aero_argb(0x00000000),
             aero_argb(0xFF343434), aero_argb(0xFF343434), aero_argb(0x00000000), aero_argb(0x00000000),
             aero_argb(0xFF343434), aero_argb(0xFF424242), aero_argb(0xFFB9B9B9), aero_argb(0xFFE3E3E3),
             aero_argb(0x00000000), aero_argb(0xFF343434), aero_argb(0xFF8F8F8F), aero_argb(0xFFE4E4E4),
             aero_argb(0xFF98B4D0), aero_argb(0xFF98B4D0), aero_argb(0x00000000), aero_argb(0xFF424242),
             aero_argb(0xFFE4E4E4), aero_argb(0xFF99B4D1), aero_argb(0xFF99B4D1), aero_argb(0xFF99B4D1),
         }};
    frame.active_top_right =
        {6,
         {
             aero_argb(0xFF1C1C1C), aero_argb(0xFF181818), aero_argb(0x00000000), aero_argb(0x00000000),
             aero_argb(0x00000000), aero_argb(0x00000000), aero_argb(0xFFFCFCFC), aero_argb(0xFFDBE9EC),
             aero_argb(0xFF1B434A), aero_argb(0xFF141616), aero_argb(0x00000000), aero_argb(0x00000000),
             aero_argb(0xFF98B4D0), aero_argb(0xFF98B4D0), aero_argb(0xFFD2F0F7), aero_argb(0xFFA9D6E0),
             aero_argb(0xFF141616), aero_argb(0x00000000), aero_argb(0xFF99B4D1), aero_argb(0xFF99B4D1),
             aero_argb(0xFF99B4D1), aero_argb(0xFFC1EEF5), aero_argb(0xFF1C545B), aero_argb(0x00000000),
         }};
    frame.inactive_top_left =
        {6,
         {
             aero_argb(0x00000000), aero_argb(0x00000000), aero_argb(0x00000000), aero_argb(0x00000000),
             aero_argb(0xFF4D4D4D), aero_argb(0xFF4D4D4D), aero_argb(0x00000000), aero_argb(0x00000000),
             aero_argb(0xFF4D4D4D), aero_argb(0xFF4E4E4E), aero_argb(0xFF8F9091), aero_argb(0xFFCCD0D8),
             aero_argb(0x00000000), aero_argb(0xFF4D4D4D), aero_argb(0xFF5E5E5F), aero_argb(0xFFC9CDD5),
             aero_argb(0xFFBFCDDB), aero_argb(0xFFBFCDDB), aero_argb(0x00000000), aero_argb(0xFF4E4E4E),
             aero_argb(0xFFC9CDD5), aero_argb(0xFFBFCEDC), aero_argb(0xFFBFCEDC), aero_argb(0xFFBFCEDC),
         }};
    frame.inactive_top_right =
        {6,
         {
             aero_argb(0xFF4D4D4D), aero_argb(0xFF4D4D4D), aero_argb(0x00000000), aero_argb(0x00000000),
             aero_argb(0x00000000), aero_argb(0x00000000), aero_argb(0xFFDBDEE3), aero_argb(0xFFB0B1B1),
             aero_argb(0xFF4E4E4E), aero_argb(0xFF4D4D4D), aero_argb(0x00000000), aero_argb(0x00000000),
             aero_argb(0xFFBFCDDB), aero_argb(0xFFBFCDDB), aero_argb(0xFFD8DBE1), aero_argb(0xFF5E5E5F),
             aero_argb(0xFF4D4D4D), aero_argb(0x00000000), aero_argb(0xFFBFCEDC), aero_argb(0xFFBFCEDC),
             aero_argb(0xFFBFCEDC), aero_argb(0xFFD8DBE1), aero_argb(0xFF4E4E4E), aero_argb(0x00000000),
         }};
    frame.caption_rule   = 0.0f;
    frame.active_caption = LinearGradientBrush{
        {GradientStop{0.0167f, aero_argb(0xFF343434)}, GradientStop{0.0500f, aero_argb(0xFFFFFFFF)},
         GradientStop{0.0833f, aero_argb(0xFF98B4D0)}, GradientStop{0.1167f, aero_argb(0xFF99B4D1)},
         GradientStop{0.1500f, aero_argb(0xFF99B5D1)}, GradientStop{0.1833f, aero_argb(0xFF9AB6D2)},
         GradientStop{0.2167f, aero_argb(0xFF9BB7D3)}, GradientStop{0.2500f, aero_argb(0xFF9DB8D4)},
         GradientStop{0.2833f, aero_argb(0xFF9EB9D4)}, GradientStop{0.3167f, aero_argb(0xFF9FBAD6)},
         GradientStop{0.3500f, aero_argb(0xFFA1BBD7)}, GradientStop{0.3833f, aero_argb(0xFFA1BBD7)},
         GradientStop{0.4167f, aero_argb(0xFFA2BCD8)}, GradientStop{0.4500f, aero_argb(0xFFA4BED9)},
         GradientStop{0.4833f, aero_argb(0xFFA5BFDA)}, GradientStop{0.5167f, aero_argb(0xFFA7C0DC)},
         GradientStop{0.5500f, aero_argb(0xFFA8C1DD)}, GradientStop{0.5833f, aero_argb(0xFFA9C3DD)},
         GradientStop{0.6167f, aero_argb(0xFFABC4DF)}, GradientStop{0.6500f, aero_argb(0xFFACC5E0)},
         GradientStop{0.6833f, aero_argb(0xFFAEC7E1)}, GradientStop{0.7167f, aero_argb(0xFFB0C8E2)},
         GradientStop{0.7500f, aero_argb(0xFFB1C9E4)}, GradientStop{0.7833f, aero_argb(0xFFB2CBE5)},
         GradientStop{0.8167f, aero_argb(0xFFB3CCE6)}, GradientStop{0.8500f, aero_argb(0xFFB4CDE6)},
         GradientStop{0.8833f, aero_argb(0xFFB5CEE7)}, GradientStop{0.9167f, aero_argb(0xFFB7CFE8)},
         GradientStop{0.9500f, aero_argb(0xFFB8D0E9)}, GradientStop{0.9833f, aero_argb(0xFFB9D1EA)}},
        GradientAxis::Vertical};
    frame.inactive_caption = LinearGradientBrush{
        {GradientStop{0.0167f, aero_argb(0xFF4D4D4D)}, GradientStop{0.0500f, aero_argb(0xFFE2E6EF)},
         GradientStop{0.0833f, aero_argb(0xFFBFCDDB)}, GradientStop{0.1167f, aero_argb(0xFFBFCEDC)},
         GradientStop{0.1500f, aero_argb(0xFFC0CEDC)}, GradientStop{0.1833f, aero_argb(0xFFC1CFDD)},
         GradientStop{0.2167f, aero_argb(0xFFC1D0DE)}, GradientStop{0.2500f, aero_argb(0xFFC2D0DE)},
         GradientStop{0.2833f, aero_argb(0xFFC3D1DF)}, GradientStop{0.3167f, aero_argb(0xFFC4D2E0)},
         GradientStop{0.3500f, aero_argb(0xFFC5D3E1)}, GradientStop{0.3833f, aero_argb(0xFFC5D3E1)},
         GradientStop{0.4167f, aero_argb(0xFFC6D4E2)}, GradientStop{0.4500f, aero_argb(0xFFC7D5E3)},
         GradientStop{0.4833f, aero_argb(0xFFC8D6E4)}, GradientStop{0.5167f, aero_argb(0xFFC9D7E5)},
         GradientStop{0.5500f, aero_argb(0xFFCBD8E6)}, GradientStop{0.5833f, aero_argb(0xFFCCDAE7)},
         GradientStop{0.6167f, aero_argb(0xFFCDDBE8)}, GradientStop{0.6500f, aero_argb(0xFFCEDCE9)},
         GradientStop{0.6833f, aero_argb(0xFFCFDDEA)}, GradientStop{0.7167f, aero_argb(0xFFD0DEEB)},
         GradientStop{0.7500f, aero_argb(0xFFD1DFEC)}, GradientStop{0.7833f, aero_argb(0xFFD2E0ED)},
         GradientStop{0.8167f, aero_argb(0xFFD3E1EE)}, GradientStop{0.8500f, aero_argb(0xFFD4E2EF)},
         GradientStop{0.8833f, aero_argb(0xFFD5E2F0)}, GradientStop{0.9167f, aero_argb(0xFFD6E3F1)},
         GradientStop{0.9500f, aero_argb(0xFFD6E4F1)}, GradientStop{0.9833f, aero_argb(0xFFD7E4F2)}},
        GradientAxis::Vertical};
    frame.active_left     = {aero_argb(0xFF000000),
                             aero_argb(0xFFFFFFFF),
                             aero_argb(0xFFBAD1EA),
                             aero_argb(0xFFBAD2EA),
                             aero_argb(0xFFBAD2EA),
                             aero_argb(0xFFBAD2EA),
                             aero_argb(0xFFBAD2EA),
                             aero_argb(0xFFB9D1EA)};
    frame.inactive_left   = {aero_argb(0xFF4C4C4C),
                             aero_argb(0xFFE9ECF3),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2)};
    frame.active_right    = {aero_argb(0xFFB9D1EA),
                             aero_argb(0xFFBAD1EA),
                             aero_argb(0xFFBAD2EA),
                             aero_argb(0xFFBAD2EA),
                             aero_argb(0xFFBAD2EA),
                             aero_argb(0xFFBAD2EA),
                             aero_argb(0xFF3FCEF2),
                             aero_argb(0xFF000000)};
    frame.inactive_right  = {aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFE9ECF3),
                             aero_argb(0xFF4C4C4C)};
    frame.active_bottom   = {aero_argb(0xFFB9D1EA),
                             aero_argb(0xFFB9D1EA),
                             aero_argb(0xFFB9D1EA),
                             aero_argb(0xFFB9D1EA),
                             aero_argb(0xFFB9D1EA),
                             aero_argb(0xFFB9D1EA),
                             aero_argb(0xFF28CFE4),
                             aero_argb(0xFF000000)};
    frame.inactive_bottom = {aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFD7E4F2),
                             aero_argb(0xFFE9ECF3),
                             aero_argb(0xFF4C4C4C)};
    // CaptionText 0 0 0 in Segoe UI 9 pt.
    frame.active_text   = aero_argb(0xFF000000);
    frame.inactive_text = aero_argb(0xFF434E54);
    frame.font_family   = "Segoe UI";
    frame.font_size     = 12.0f;
    frame.font_weight   = FontWeight::Regular;
    frame.icon_size     = 16.0f;
    frame.icon_left     = 9.0f;
    frame.title_left    = 5.0f;
    // 27 x 20 minimize and maximize and a 48 x 20 close, flush with the top edge and 6 px in from
    // the right, side by side.
    frame.button        = Size{27.0f, 20.0f};
    frame.close_width   = 48.0f;
    frame.button_top    = 0.0f;
    frame.button_right  = 6.0f;
    frame.close_gap     = 0.0f;
    frame.button_gap    = 0.0f;
    frame.glyph_outline = aero_argb(0xFF535666);
    // Glass over the blur. Windows 7's default "Sky" scheme (ColorizationColor 0x6B74B8FC) mixed
    // 49 % of the blurred backdrop as it was, 43 % of the backdrop's brightness times the color
    // (the afterglow) and 8 % flat color. A window can only blend over the blur, not multiply
    // it, so the tint is the color at a quarter, where the two colored shares land over a
    // mid-gray backdrop. Inactive, it is lighter and thinner, standing in for the fade an
    // inactive Windows 7 frame showed. The edges are the Basic art's outline and highlight at the
    // alphas glass showed them with.
    frame.glass = ControlChrome::WindowFrame::Glass{
        .active_tint   = aero_argb(0x4074B8FC),
        .inactive_tint = aero_argb(0x26B4CCE6),
        .outline       = aero_argb(0xC0000000),
        .highlight     = aero_argb(0x80FFFFFF),
        .content_edge  = aero_argb(0x80000000),
        .glow          = aero_argb(0x1FFFFFFF),
        .streak        = aero_argb(0x1CFFFFFF),
    };
    // Normal, hot, pressed and disabled; an inactive window shows the disabled art at rest.
    frame.caption_faces = {
        {Face{aero_argb(0xFF63748D),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF5D6F88)}, GradientStop{0.0750f, aero_argb(0xFFDFE8F2)},
                   GradientStop{0.1250f, aero_argb(0xFFC3D4E7)}, GradientStop{0.1750f, aero_argb(0xFFC1D3E7)},
                   GradientStop{0.2250f, aero_argb(0xFFC1D3E6)}, GradientStop{0.2750f, aero_argb(0xFFBFD3E7)},
                   GradientStop{0.3250f, aero_argb(0xFFBED3E7)}, GradientStop{0.3750f, aero_argb(0xFFBFD3E8)},
                   GradientStop{0.4250f, aero_argb(0xFFBFD3E8)}, GradientStop{0.4750f, aero_argb(0xFF98B1CC)},
                   GradientStop{0.5250f, aero_argb(0xFF98B1CC)}, GradientStop{0.5750f, aero_argb(0xFF9AB3CD)},
                   GradientStop{0.6250f, aero_argb(0xFF9AB3CD)}, GradientStop{0.6750f, aero_argb(0xFF9DB5CF)},
                   GradientStop{0.7250f, aero_argb(0xFFA4BED8)}, GradientStop{0.7750f, aero_argb(0xFFACC5DF)},
                   GradientStop{0.8250f, aero_argb(0xFFB2CBE5)}, GradientStop{0.8750f, aero_argb(0xFFB7D0E9)},
                   GradientStop{0.9250f, aero_argb(0xFFD9E7F4)}, GradientStop{0.9750f, aero_argb(0xFF697A92)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFFCFCFC),
              aero_argb(0xFFC8D5E4)},
         Face{aero_argb(0xFF576371),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF515E6C)}, GradientStop{0.0750f, aero_argb(0xFFD6EBFA)},
                   GradientStop{0.1250f, aero_argb(0xFFAAD5F3)}, GradientStop{0.1750f, aero_argb(0xFFA0D0F1)},
                   GradientStop{0.2250f, aero_argb(0xFF96CBEF)}, GradientStop{0.2750f, aero_argb(0xFF8AC4EC)},
                   GradientStop{0.3250f, aero_argb(0xFF86C2EB)}, GradientStop{0.3750f, aero_argb(0xFF81C0EA)},
                   GradientStop{0.4250f, aero_argb(0xFF81C0EA)}, GradientStop{0.4750f, aero_argb(0xFF2D73A3)},
                   GradientStop{0.5250f, aero_argb(0xFF2D73A3)}, GradientStop{0.5750f, aero_argb(0xFF2D75A7)},
                   GradientStop{0.6250f, aero_argb(0xFF2D75A7)}, GradientStop{0.6750f, aero_argb(0xFF2C78AC)},
                   GradientStop{0.7250f, aero_argb(0xFF2C89BF)}, GradientStop{0.7750f, aero_argb(0xFF2C9AD0)},
                   GradientStop{0.8250f, aero_argb(0xFF29A9DF)}, GradientStop{0.8750f, aero_argb(0xFF24C6EB)},
                   GradientStop{0.9250f, aero_argb(0xFF85EFF9)}, GradientStop{0.9750f, aero_argb(0xFF5D6976)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFFCFCFC),
              aero_argb(0xFF8EB4CE)},
         Face{aero_argb(0xFF576371),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF515E6C)}, GradientStop{0.0750f, aero_argb(0xFFACBEC9)},
                   GradientStop{0.1250f, aero_argb(0xFF7F9AAC)}, GradientStop{0.1750f, aero_argb(0xFF7893A5)},
                   GradientStop{0.2250f, aero_argb(0xFF6E889C)}, GradientStop{0.2750f, aero_argb(0xFF647C92)},
                   GradientStop{0.3250f, aero_argb(0xFF617A90)}, GradientStop{0.3750f, aero_argb(0xFF5D778C)},
                   GradientStop{0.4250f, aero_argb(0xFF5D778C)}, GradientStop{0.4750f, aero_argb(0xFF203B52)},
                   GradientStop{0.5250f, aero_argb(0xFF203B52)}, GradientStop{0.5750f, aero_argb(0xFF203E54)},
                   GradientStop{0.6250f, aero_argb(0xFF203E54)}, GradientStop{0.6750f, aero_argb(0xFF1F4158)},
                   GradientStop{0.7250f, aero_argb(0xFF236273)}, GradientStop{0.7750f, aero_argb(0xFF278791)},
                   GradientStop{0.8250f, aero_argb(0xFF2BA8AC)}, GradientStop{0.8750f, aero_argb(0xFF27C9C8)},
                   GradientStop{0.9250f, aero_argb(0xFF67E6E4)}, GradientStop{0.9750f, aero_argb(0xFF5D6976)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFFCFCFC),
              aero_argb(0xFF667888)},
         Face{aero_argb(0xFF889DB7),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF7F96B0)}, GradientStop{0.0750f, aero_argb(0xFFC2D3E6)},
                   GradientStop{0.1250f, aero_argb(0xFFB5CAE1)}, GradientStop{0.1750f, aero_argb(0xFFB4CAE2)},
                   GradientStop{0.2250f, aero_argb(0xFFB5CBE2)}, GradientStop{0.2750f, aero_argb(0xFFB5CCE3)},
                   GradientStop{0.3250f, aero_argb(0xFFB5CDE4)}, GradientStop{0.3750f, aero_argb(0xFFB5CCE5)},
                   GradientStop{0.4250f, aero_argb(0xFFB5CCE5)}, GradientStop{0.4750f, aero_argb(0xFFA3BCD7)},
                   GradientStop{0.5250f, aero_argb(0xFFA3BCD7)}, GradientStop{0.5750f, aero_argb(0xFFA4BDD8)},
                   GradientStop{0.6250f, aero_argb(0xFFA4BDD8)}, GradientStop{0.6750f, aero_argb(0xFFA6BFDA)},
                   GradientStop{0.7250f, aero_argb(0xFFAAC4DE)}, GradientStop{0.7750f, aero_argb(0xFFAFC8E2)},
                   GradientStop{0.8250f, aero_argb(0xFFB4CDE7)}, GradientStop{0.8750f, aero_argb(0xFFB7D0E9)},
                   GradientStop{0.9250f, aero_argb(0xFFC9DDF0)}, GradientStop{0.9750f, aero_argb(0xFF91A6BF)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFD7D7D7),
              aero_argb(0xFFBBCEE3)},
         Face{aero_argb(0xFF889DB7),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF7F96B0)}, GradientStop{0.0750f, aero_argb(0xFFC2D3E6)},
                   GradientStop{0.1250f, aero_argb(0xFFB5CAE1)}, GradientStop{0.1750f, aero_argb(0xFFB4CAE2)},
                   GradientStop{0.2250f, aero_argb(0xFFB5CBE2)}, GradientStop{0.2750f, aero_argb(0xFFB5CCE3)},
                   GradientStop{0.3250f, aero_argb(0xFFB5CDE4)}, GradientStop{0.3750f, aero_argb(0xFFB5CCE5)},
                   GradientStop{0.4250f, aero_argb(0xFFB5CCE5)}, GradientStop{0.4750f, aero_argb(0xFFA3BCD7)},
                   GradientStop{0.5250f, aero_argb(0xFFA3BCD7)}, GradientStop{0.5750f, aero_argb(0xFFA4BDD8)},
                   GradientStop{0.6250f, aero_argb(0xFFA4BDD8)}, GradientStop{0.6750f, aero_argb(0xFFA6BFDA)},
                   GradientStop{0.7250f, aero_argb(0xFFAAC4DE)}, GradientStop{0.7750f, aero_argb(0xFFAFC8E2)},
                   GradientStop{0.8250f, aero_argb(0xFFB4CDE7)}, GradientStop{0.8750f, aero_argb(0xFFB7D0E9)},
                   GradientStop{0.9250f, aero_argb(0xFFC9DDF0)}, GradientStop{0.9750f, aero_argb(0xFF91A6BF)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFD7D7D7),
              aero_argb(0xFFBBCEE3)},
         Face{aero_argb(0xFF576371),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF515E6C)}, GradientStop{0.0750f, aero_argb(0xFFD6EBFA)},
                   GradientStop{0.1250f, aero_argb(0xFFAAD5F3)}, GradientStop{0.1750f, aero_argb(0xFFA0D0F1)},
                   GradientStop{0.2250f, aero_argb(0xFF96CBEF)}, GradientStop{0.2750f, aero_argb(0xFF8AC4EC)},
                   GradientStop{0.3250f, aero_argb(0xFF86C2EB)}, GradientStop{0.3750f, aero_argb(0xFF81C0EA)},
                   GradientStop{0.4250f, aero_argb(0xFF81C0EA)}, GradientStop{0.4750f, aero_argb(0xFF2D73A3)},
                   GradientStop{0.5250f, aero_argb(0xFF2D73A3)}, GradientStop{0.5750f, aero_argb(0xFF2D75A7)},
                   GradientStop{0.6250f, aero_argb(0xFF2D75A7)}, GradientStop{0.6750f, aero_argb(0xFF2C78AC)},
                   GradientStop{0.7250f, aero_argb(0xFF2C89BF)}, GradientStop{0.7750f, aero_argb(0xFF2C9AD0)},
                   GradientStop{0.8250f, aero_argb(0xFF29A9DF)}, GradientStop{0.8750f, aero_argb(0xFF24C6EB)},
                   GradientStop{0.9250f, aero_argb(0xFF85EFF9)}, GradientStop{0.9750f, aero_argb(0xFF5D6976)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFFCFCFC),
              aero_argb(0xFF8EB4CE)},
         Face{aero_argb(0xFF576371),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF515E6C)}, GradientStop{0.0750f, aero_argb(0xFFACBEC9)},
                   GradientStop{0.1250f, aero_argb(0xFF7F9AAC)}, GradientStop{0.1750f, aero_argb(0xFF7893A5)},
                   GradientStop{0.2250f, aero_argb(0xFF6E889C)}, GradientStop{0.2750f, aero_argb(0xFF647C92)},
                   GradientStop{0.3250f, aero_argb(0xFF617A90)}, GradientStop{0.3750f, aero_argb(0xFF5D778C)},
                   GradientStop{0.4250f, aero_argb(0xFF5D778C)}, GradientStop{0.4750f, aero_argb(0xFF203B52)},
                   GradientStop{0.5250f, aero_argb(0xFF203B52)}, GradientStop{0.5750f, aero_argb(0xFF203E54)},
                   GradientStop{0.6250f, aero_argb(0xFF203E54)}, GradientStop{0.6750f, aero_argb(0xFF1F4158)},
                   GradientStop{0.7250f, aero_argb(0xFF236273)}, GradientStop{0.7750f, aero_argb(0xFF278791)},
                   GradientStop{0.8250f, aero_argb(0xFF2BA8AC)}, GradientStop{0.8750f, aero_argb(0xFF27C9C8)},
                   GradientStop{0.9250f, aero_argb(0xFF67E6E4)}, GradientStop{0.9750f, aero_argb(0xFF5D6976)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFFCFCFC),
              aero_argb(0xFF667888)},
         Face{aero_argb(0xFF889DB7),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF7F96B0)}, GradientStop{0.0750f, aero_argb(0xFFC2D3E6)},
                   GradientStop{0.1250f, aero_argb(0xFFB5CAE1)}, GradientStop{0.1750f, aero_argb(0xFFB4CAE2)},
                   GradientStop{0.2250f, aero_argb(0xFFB5CBE2)}, GradientStop{0.2750f, aero_argb(0xFFB5CCE3)},
                   GradientStop{0.3250f, aero_argb(0xFFB5CDE4)}, GradientStop{0.3750f, aero_argb(0xFFB5CCE5)},
                   GradientStop{0.4250f, aero_argb(0xFFB5CCE5)}, GradientStop{0.4750f, aero_argb(0xFFA3BCD7)},
                   GradientStop{0.5250f, aero_argb(0xFFA3BCD7)}, GradientStop{0.5750f, aero_argb(0xFFA4BDD8)},
                   GradientStop{0.6250f, aero_argb(0xFFA4BDD8)}, GradientStop{0.6750f, aero_argb(0xFFA6BFDA)},
                   GradientStop{0.7250f, aero_argb(0xFFAAC4DE)}, GradientStop{0.7750f, aero_argb(0xFFAFC8E2)},
                   GradientStop{0.8250f, aero_argb(0xFFB4CDE7)}, GradientStop{0.8750f, aero_argb(0xFFB7D0E9)},
                   GradientStop{0.9250f, aero_argb(0xFFC9DDF0)}, GradientStop{0.9750f, aero_argb(0xFF91A6BF)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFD7D7D7),
              aero_argb(0xFFBBCEE3)}}};
    frame.close_faces = {
        {Face{aero_argb(0xFF431422),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF431422)}, GradientStop{0.0750f, aero_argb(0xFFF4D3CC)},
                   GradientStop{0.1250f, aero_argb(0xFFE9A99C)}, GradientStop{0.1750f, aero_argb(0xFFE7A699)},
                   GradientStop{0.2250f, aero_argb(0xFFE6A295)}, GradientStop{0.2750f, aero_argb(0xFFE49E90)},
                   GradientStop{0.3250f, aero_argb(0xFFE2988B)}, GradientStop{0.3750f, aero_argb(0xFFDF9587)},
                   GradientStop{0.4250f, aero_argb(0xFFDF9587)}, GradientStop{0.4750f, aero_argb(0xFFB8432C)},
                   GradientStop{0.5250f, aero_argb(0xFFB8432C)}, GradientStop{0.5750f, aero_argb(0xFFBB4933)},
                   GradientStop{0.6250f, aero_argb(0xFFBB4933)}, GradientStop{0.6750f, aero_argb(0xFFBF533E)},
                   GradientStop{0.7250f, aero_argb(0xFFC45E4A)}, GradientStop{0.7750f, aero_argb(0xFFC96A57)},
                   GradientStop{0.8250f, aero_argb(0xFFCE7564)}, GradientStop{0.8750f, aero_argb(0xFFD27F6F)},
                   GradientStop{0.9250f, aero_argb(0xFFECC6C0)}, GradientStop{0.9750f, aero_argb(0xFF431422)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFFDFDFD),
              aero_argb(0xFFD99A8E)},
         Face{aero_argb(0xFF431422),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF431422)}, GradientStop{0.0750f, aero_argb(0xFFFDE4E0)},
                   GradientStop{0.1250f, aero_argb(0xFFFCC8BF)}, GradientStop{0.1750f, aero_argb(0xFFFCC4BA)},
                   GradientStop{0.2250f, aero_argb(0xFFFCBDB2)}, GradientStop{0.2750f, aero_argb(0xFFFBB6AA)},
                   GradientStop{0.3250f, aero_argb(0xFFFAAFA1)}, GradientStop{0.3750f, aero_argb(0xFFFAA99A)},
                   GradientStop{0.4250f, aero_argb(0xFFFAA99A)}, GradientStop{0.4750f, aero_argb(0xFFD22302)},
                   GradientStop{0.5250f, aero_argb(0xFFD22302)}, GradientStop{0.5750f, aero_argb(0xFFD4300B)},
                   GradientStop{0.6250f, aero_argb(0xFFD4300B)}, GradientStop{0.6750f, aero_argb(0xFFD74116)},
                   GradientStop{0.7250f, aero_argb(0xFFDB5725)}, GradientStop{0.7750f, aero_argb(0xFFDF6D33)},
                   GradientStop{0.8250f, aero_argb(0xFFE28242)}, GradientStop{0.8750f, aero_argb(0xFFE5934D)},
                   GradientStop{0.9250f, aero_argb(0xFFF7E4B2)}, GradientStop{0.9750f, aero_argb(0xFF431422)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFFDFDFD),
              aero_argb(0xFFE78977)},
         Face{aero_argb(0xFF431422),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF431422)}, GradientStop{0.0750f, aero_argb(0xFFD7C2B7)},
                   GradientStop{0.1250f, aero_argb(0xFFB08774)}, GradientStop{0.1750f, aero_argb(0xFFAE836E)},
                   GradientStop{0.2250f, aero_argb(0xFFAB7E68)}, GradientStop{0.2750f, aero_argb(0xFFA97762)},
                   GradientStop{0.3250f, aero_argb(0xFFA6705B)}, GradientStop{0.3750f, aero_argb(0xFFA6705B)},
                   GradientStop{0.4250f, aero_argb(0xFFA6705B)}, GradientStop{0.4750f, aero_argb(0xFF741500)},
                   GradientStop{0.5250f, aero_argb(0xFF741500)}, GradientStop{0.5750f, aero_argb(0xFF741500)},
                   GradientStop{0.6250f, aero_argb(0xFF741500)}, GradientStop{0.6750f, aero_argb(0xFF741500)},
                   GradientStop{0.7250f, aero_argb(0xFF742401)}, GradientStop{0.7750f, aero_argb(0xFF803910)},
                   GradientStop{0.8250f, aero_argb(0xFF915825)}, GradientStop{0.8750f, aero_argb(0xFFA77C3F)},
                   GradientStop{0.9250f, aero_argb(0xFFDFD0A6)}, GradientStop{0.9750f, aero_argb(0xFF431422)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFFDFDFD),
              aero_argb(0xFFB28176)},
         Face{aero_argb(0xFF94A8C1),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF94A8C1)}, GradientStop{0.0750f, aero_argb(0xFFD3E3F4)},
                   GradientStop{0.1250f, aero_argb(0xFFC9DDF2)}, GradientStop{0.1750f, aero_argb(0xFFC8DCF2)},
                   GradientStop{0.2250f, aero_argb(0xFFC7DCF1)}, GradientStop{0.2750f, aero_argb(0xFFC6DBF1)},
                   GradientStop{0.3250f, aero_argb(0xFFC5DBF1)}, GradientStop{0.3750f, aero_argb(0xFFC5DBF1)},
                   GradientStop{0.4250f, aero_argb(0xFFC5DBF1)}, GradientStop{0.4750f, aero_argb(0xFFB1C9E2)},
                   GradientStop{0.5250f, aero_argb(0xFFB1C9E2)}, GradientStop{0.5750f, aero_argb(0xFFB2CAE4)},
                   GradientStop{0.6250f, aero_argb(0xFFB2CAE4)}, GradientStop{0.6750f, aero_argb(0xFFB4CCE5)},
                   GradientStop{0.7250f, aero_argb(0xFFB6CEE7)}, GradientStop{0.7750f, aero_argb(0xFFB8D0E9)},
                   GradientStop{0.8250f, aero_argb(0xFFBAD2EB)}, GradientStop{0.8750f, aero_argb(0xFFBCD4ED)},
                   GradientStop{0.9250f, aero_argb(0xFFCDE0F3)}, GradientStop{0.9750f, aero_argb(0xFF94A8C1)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFD7D7D7),
              aero_argb(0xFFC6D9EB)},
         Face{aero_argb(0xFF94A8C1),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF94A8C1)}, GradientStop{0.0750f, aero_argb(0xFFD3E3F4)},
                   GradientStop{0.1250f, aero_argb(0xFFC9DDF2)}, GradientStop{0.1750f, aero_argb(0xFFC8DCF2)},
                   GradientStop{0.2250f, aero_argb(0xFFC7DCF1)}, GradientStop{0.2750f, aero_argb(0xFFC6DBF1)},
                   GradientStop{0.3250f, aero_argb(0xFFC5DBF1)}, GradientStop{0.3750f, aero_argb(0xFFC5DBF1)},
                   GradientStop{0.4250f, aero_argb(0xFFC5DBF1)}, GradientStop{0.4750f, aero_argb(0xFFB1C9E2)},
                   GradientStop{0.5250f, aero_argb(0xFFB1C9E2)}, GradientStop{0.5750f, aero_argb(0xFFB2CAE4)},
                   GradientStop{0.6250f, aero_argb(0xFFB2CAE4)}, GradientStop{0.6750f, aero_argb(0xFFB4CCE5)},
                   GradientStop{0.7250f, aero_argb(0xFFB6CEE7)}, GradientStop{0.7750f, aero_argb(0xFFB8D0E9)},
                   GradientStop{0.8250f, aero_argb(0xFFBAD2EB)}, GradientStop{0.8750f, aero_argb(0xFFBCD4ED)},
                   GradientStop{0.9250f, aero_argb(0xFFCDE0F3)}, GradientStop{0.9750f, aero_argb(0xFF94A8C1)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFD7D7D7),
              aero_argb(0xFFC6D9EB)},
         Face{aero_argb(0xFF431422),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF431422)}, GradientStop{0.0750f, aero_argb(0xFFFDE4E0)},
                   GradientStop{0.1250f, aero_argb(0xFFFCC8BF)}, GradientStop{0.1750f, aero_argb(0xFFFCC4BA)},
                   GradientStop{0.2250f, aero_argb(0xFFFCBDB2)}, GradientStop{0.2750f, aero_argb(0xFFFBB6AA)},
                   GradientStop{0.3250f, aero_argb(0xFFFAAFA1)}, GradientStop{0.3750f, aero_argb(0xFFFAA99A)},
                   GradientStop{0.4250f, aero_argb(0xFFFAA99A)}, GradientStop{0.4750f, aero_argb(0xFFD22302)},
                   GradientStop{0.5250f, aero_argb(0xFFD22302)}, GradientStop{0.5750f, aero_argb(0xFFD4300B)},
                   GradientStop{0.6250f, aero_argb(0xFFD4300B)}, GradientStop{0.6750f, aero_argb(0xFFD74116)},
                   GradientStop{0.7250f, aero_argb(0xFFDB5725)}, GradientStop{0.7750f, aero_argb(0xFFDF6D33)},
                   GradientStop{0.8250f, aero_argb(0xFFE28242)}, GradientStop{0.8750f, aero_argb(0xFFE5934D)},
                   GradientStop{0.9250f, aero_argb(0xFFF7E4B2)}, GradientStop{0.9750f, aero_argb(0xFF431422)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFFDFDFD),
              aero_argb(0xFFE78977)},
         Face{aero_argb(0xFF431422),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF431422)}, GradientStop{0.0750f, aero_argb(0xFFD7C2B7)},
                   GradientStop{0.1250f, aero_argb(0xFFB08774)}, GradientStop{0.1750f, aero_argb(0xFFAE836E)},
                   GradientStop{0.2250f, aero_argb(0xFFAB7E68)}, GradientStop{0.2750f, aero_argb(0xFFA97762)},
                   GradientStop{0.3250f, aero_argb(0xFFA6705B)}, GradientStop{0.3750f, aero_argb(0xFFA6705B)},
                   GradientStop{0.4250f, aero_argb(0xFFA6705B)}, GradientStop{0.4750f, aero_argb(0xFF741500)},
                   GradientStop{0.5250f, aero_argb(0xFF741500)}, GradientStop{0.5750f, aero_argb(0xFF741500)},
                   GradientStop{0.6250f, aero_argb(0xFF741500)}, GradientStop{0.6750f, aero_argb(0xFF741500)},
                   GradientStop{0.7250f, aero_argb(0xFF742401)}, GradientStop{0.7750f, aero_argb(0xFF803910)},
                   GradientStop{0.8250f, aero_argb(0xFF915825)}, GradientStop{0.8750f, aero_argb(0xFFA77C3F)},
                   GradientStop{0.9250f, aero_argb(0xFFDFD0A6)}, GradientStop{0.9750f, aero_argb(0xFF431422)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFFDFDFD),
              aero_argb(0xFFB28176)},
         Face{aero_argb(0xFF94A8C1),
              LinearGradientBrush{
                  {GradientStop{0.0250f, aero_argb(0xFF94A8C1)}, GradientStop{0.0750f, aero_argb(0xFFD3E3F4)},
                   GradientStop{0.1250f, aero_argb(0xFFC9DDF2)}, GradientStop{0.1750f, aero_argb(0xFFC8DCF2)},
                   GradientStop{0.2250f, aero_argb(0xFFC7DCF1)}, GradientStop{0.2750f, aero_argb(0xFFC6DBF1)},
                   GradientStop{0.3250f, aero_argb(0xFFC5DBF1)}, GradientStop{0.3750f, aero_argb(0xFFC5DBF1)},
                   GradientStop{0.4250f, aero_argb(0xFFC5DBF1)}, GradientStop{0.4750f, aero_argb(0xFFB1C9E2)},
                   GradientStop{0.5250f, aero_argb(0xFFB1C9E2)}, GradientStop{0.5750f, aero_argb(0xFFB2CAE4)},
                   GradientStop{0.6250f, aero_argb(0xFFB2CAE4)}, GradientStop{0.6750f, aero_argb(0xFFB4CCE5)},
                   GradientStop{0.7250f, aero_argb(0xFFB6CEE7)}, GradientStop{0.7750f, aero_argb(0xFFB8D0E9)},
                   GradientStop{0.8250f, aero_argb(0xFFBAD2EB)}, GradientStop{0.8750f, aero_argb(0xFFBCD4ED)},
                   GradientStop{0.9250f, aero_argb(0xFFCDE0F3)}, GradientStop{0.9750f, aero_argb(0xFF94A8C1)}},
                  GradientAxis::Vertical},
              aero_argb(0xFFD7D7D7),
              aero_argb(0xFFC6D9EB)}}};
    return frame;
}

} // namespace ui::detail
