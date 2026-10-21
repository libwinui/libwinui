// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <concepts>

#include <cstdint>

namespace ui
{

/// An RGBA color with float channels in 0..1 and straight (non-premultiplied) alpha.
///
/// The value type every brush, theme palette entry and render command uses. Channels hold the
/// sRGB-encoded values as authored (not linearized); `a` defaults to opaque. Build one from
/// 8-bit channels with from_rgba(), from_rgb() or from_argb(), or use a named constant from
/// `colors`.
///
/// Example:
/// @code
/// const ui::Color accent = ui::Color::from_argb(0xFF0078D4);
/// const ui::Color hover  = ui::lerp(accent, ui::colors::White, 0.2f);
///
/// window.set_content(ui::Button{
///     .text       = "Save",
///     .background = hover.with_alpha(0.9f),
///     .foreground = ui::colors::White,
/// });
/// @endcode
struct Color
{
    float r = 0.0f; ///< Red, 0..1.
    float g = 0.0f; ///< Green, 0..1.
    float b = 0.0f; ///< Blue, 0..1.
    float a = 1.0f; ///< Straight alpha, 0 (transparent) .. 1 (opaque).

    /// Opaque black.
    constexpr Color() = default;
    /// From float channels in 0..1, opaque: `ui::Color{1.0f, 0.5f, 0.0f}`.
    constexpr Color(std::floating_point auto red, std::floating_point auto green, std::floating_point auto blue) :
        r(static_cast<float>(red)),
        g(static_cast<float>(green)),
        b(static_cast<float>(blue))
    {
    }
    /// From float channels and alpha in 0..1: `ui::Color{1.0f, 0.5f, 0.0f, 0.5f}`.
    constexpr Color(std::floating_point auto red,
                    std::floating_point auto green,
                    std::floating_point auto blue,
                    std::floating_point auto alpha) :
        r(static_cast<float>(red)),
        g(static_cast<float>(green)),
        b(static_cast<float>(blue)),
        a(static_cast<float>(alpha))
    {
    }
    /// Integer channels do not compile: `Color{255, 128, 0}` would be white, since a channel
    /// is 0..1. 8-bit channels go through from_rgb() or from_rgba().
    template <std::integral R, std::integral G, std::integral B, std::integral... A>
        requires(sizeof...(A) <= 1)
    constexpr Color(R, G, B, A...)
    {
        static_assert(sizeof(R) == 0,
                      "Color takes float channels in 0..1; for 8-bit channels use Color::from_rgb(r, g, b) "
                      "or Color::from_rgba(r, g, b, a)");
    }

    /// Exact channel-wise comparison, no tolerance.
    friend constexpr bool operator==(const Color&, const Color&) = default;

    /// Construct from 8-bit sRGB channels (0..255); alpha defaults to opaque.
    [[nodiscard]] static constexpr Color from_rgba(std::uint8_t r8, std::uint8_t g8, std::uint8_t b8, std::uint8_t a8 = 255)
    {
        return {r8 / 255.0f, g8 / 255.0f, b8 / 255.0f, a8 / 255.0f};
    }

    /// Construct from a packed 0xAARRGGBB value (`#AARRGGBB` order), so `0xFF0078D4` is an
    /// opaque blue. A value written as 0xRRGGBB has alpha 0 and is fully transparent. Use
    /// from_rgb() for that.
    [[nodiscard]] static constexpr Color from_argb(std::uint32_t argb)
    {
        return from_rgba(static_cast<std::uint8_t>((argb >> 16) & 0xFF),
                         static_cast<std::uint8_t>((argb >> 8) & 0xFF),
                         static_cast<std::uint8_t>(argb & 0xFF),
                         static_cast<std::uint8_t>((argb >> 24) & 0xFF));
    }

    /// Construct an opaque color from a packed 0xRRGGBB value (`#RRGGBB`), so `0x0078D4` is the
    /// same blue. Bits above the low 24 are ignored.
    [[nodiscard]] static constexpr Color from_rgb(std::uint32_t rgb)
    {
        return from_argb(0xFF000000u | (rgb & 0x00FFFFFFu));
    }
    /// Construct an opaque color from 8-bit sRGB channels (0..255): `from_rgb(0, 120, 212)`.
    [[nodiscard]] static constexpr Color from_rgb(std::uint8_t r8, std::uint8_t g8, std::uint8_t b8)
    {
        return from_rgba(r8, g8, b8);
    }

    /// A copy with `a` replaced by `alpha` (0..1, not clamped); r/g/b are untouched.
    [[nodiscard]] constexpr Color with_alpha(float alpha) const
    {
        return {r, g, b, alpha};
    }
};

/// Channel-wise linear interpolation from `a` (t = 0) to `b` (t = 1), alpha included. `t` is
/// clamped to [0, 1]. Interpolates the stored sRGB values directly, not in linear light.
[[nodiscard]] constexpr Color lerp(const Color& a, const Color& b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

/// Theme-independent named colors. Theme-dependent ones live on `Theme::palette`.
namespace colors
{
/// Fully transparent black (all channels 0); the "no fill" value.
inline constexpr Color Transparent{0.0f, 0.0f, 0.0f, 0.0f};
/// Opaque black.
inline constexpr Color Black{0.0f, 0.0f, 0.0f, 1.0f};
/// Opaque white.
inline constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
} // namespace colors

} // namespace ui
