// SPDX-License-Identifier: MIT
#include "winui/render/Brush.hpp"

#include "winui/core/Color.hpp"

#include <algorithm>

#include <cmath>

namespace ui
{

namespace
{

struct BrushHsv
{
    double h = 0.0; // degrees
    double s = 0.0;
    double v = 0.0;
};

BrushHsv to_hsv(const Color& c)
{
    const double r  = c.r;
    const double g  = c.g;
    const double b  = c.b;
    const double mx = std::max({r, g, b});
    const double mn = std::min({r, g, b});
    const double d  = mx - mn;
    BrushHsv     out;
    out.v = mx;
    out.s = mx > 0.0 ? d / mx : 0.0;
    if (d > 0.0)
    {
        if (mx == r)
        {
            out.h = 60.0 * std::fmod((g - b) / d, 6.0);
        }
        else if (mx == g)
        {
            out.h = 60.0 * ((b - r) / d + 2.0);
        }
        else
        {
            out.h = 60.0 * ((r - g) / d + 4.0);
        }
        if (out.h < 0.0)
        {
            out.h += 360.0;
        }
    }
    return out;
}

Color from_hsv(const BrushHsv& hsv, float alpha)
{
    const double c  = hsv.v * hsv.s;
    const double hp = hsv.h / 60.0;
    const double x  = c * (1.0 - std::abs(std::fmod(hp, 2.0) - 1.0));
    double       r = 0.0, g = 0.0, b = 0.0;
    if (hp < 1.0)
    {
        r = c, g = x;
    }
    else if (hp < 2.0)
    {
        r = x, g = c;
    }
    else if (hp < 3.0)
    {
        g = c, b = x;
    }
    else if (hp < 4.0)
    {
        g = x, b = c;
    }
    else if (hp < 5.0)
    {
        r = x, b = c;
    }
    else
    {
        r = c, b = x;
    }
    const double m = hsv.v - c;
    return Color{static_cast<float>(r + m), static_cast<float>(g + m), static_cast<float>(b + m), alpha};
}

// Brush colors are 8-bit, and the brush rounds its combined alphas to a byte.
float byte_alpha(double a)
{
    return static_cast<float>(std::round(std::clamp(a, 0.0, 1.0) * 255.0) / 255.0);
}

// The factor on the tint's opacity: 0.9 for a mid-gray tint, falling toward 0.45 for white and
// 0.85 for black, less so the more saturated the tint is.
double tint_opacity_modifier(const Color& tint)
{
    constexpr double MidPoint           = 0.50;
    constexpr double WhiteMaxOpacity    = 0.45;
    constexpr double MidPointMaxOpacity = 0.90;
    constexpr double BlackMaxOpacity    = 0.85;

    const BrushHsv hsv      = to_hsv(tint);
    double         modifier = MidPointMaxOpacity;
    if (hsv.v != MidPoint)
    {
        double lowest_max    = MidPointMaxOpacity;
        double max_deviation = MidPoint;
        if (hsv.v > MidPoint)
        {
            lowest_max    = WhiteMaxOpacity;
            max_deviation = 1.0 - max_deviation;
        }
        else if (hsv.v < MidPoint)
        {
            lowest_max = BlackMaxOpacity;
        }
        double       suppression = MidPointMaxOpacity - lowest_max;
        const double normalized  = std::abs(hsv.v - MidPoint) / max_deviation;
        if (hsv.s > 0.0)
        {
            suppression *= std::max(1.0 - hsv.s * 2.0, 0.0);
        }
        modifier = MidPointMaxOpacity - suppression * normalized;
    }
    return modifier;
}

} // namespace

Color AcrylicBrush::effective_tint() const
{
    Color tint = tint_color;
    if (tint_luminosity_opacity)
    {
        tint.a = byte_alpha(static_cast<double>(tint.a) * tint_opacity);
    }
    else
    {
        tint.a = byte_alpha(static_cast<double>(tint.a) * tint_opacity * tint_opacity_modifier(tint_color));
    }
    return tint;
}

Color AcrylicBrush::effective_luminosity() const
{
    // The *original* tint opacity, without the modifier.
    Color tint = tint_color;
    tint.a     = byte_alpha(static_cast<double>(tint.a) * tint_opacity);
    if (tint_luminosity_opacity)
    {
        return Color{tint.r, tint.g, tint.b, std::clamp(*tint_luminosity_opacity, 0.0f, 1.0f)};
    }
    constexpr double MinHsvV       = 0.125;
    constexpr double MaxHsvV       = 0.965;
    constexpr double MinLumOpacity = 0.15;
    constexpr double MaxLumOpacity = 1.03;
    BrushHsv         hsv           = to_hsv(tint);
    hsv.v                          = std::clamp(hsv.v, MinHsvV, MaxHsvV);
    const double mapped            = static_cast<double>(tint.a) * (MaxLumOpacity - MinLumOpacity) + MinLumOpacity;
    return from_hsv(hsv, static_cast<float>(std::min(mapped, 1.0)));
}

} // namespace ui
