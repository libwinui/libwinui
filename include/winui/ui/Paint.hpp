// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/render/Brush.hpp"

#include <concepts>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include <cstddef>

namespace ui
{

struct Theme;

/// A color read from the theme each time it is used: a theme-resource brush. A captureless
/// lambda taking a `const Theme&` converts to it, and ui::theme_colors names one for every
/// color of ThemeColors. with_alpha() gives the same color at another opacity.
///
/// It holds a plain function, so the resource is stateless. It follows an element's requested
/// theme and theme switches without any subscription.
///
/// Example:
/// @code
/// auto grid = std::make_unique<ui::GridControl>();
/// grid->set_background([](const ui::Theme& t) { return t.colors().background; });
/// ui::Border{.background = ui::theme_colors::Accent.with_alpha(0.7f)};
/// @endcode
class ThemeColor
{
public:
    /// The function a resource reads its color with.
    using Read = Color (*)(const Theme&);

    /// None.
    constexpr ThemeColor() noexcept = default;
    /// None, as the default.
    constexpr ThemeColor(std::nullptr_t) noexcept // NOLINT(google-explicit-constructor)
    {
    }
    /// The color `read` gives under a theme; a captureless lambda converts too.
    template <typename F>
        requires std::convertible_to<F, Read> && (!std::same_as<std::remove_cvref_t<F>, std::nullptr_t>)
    constexpr ThemeColor(F read) noexcept : read_(read) // NOLINT(google-explicit-constructor)
    {
    }

    /// The same color with its alpha replaced by `alpha` (0..1), as Color::with_alpha() does.
    [[nodiscard]] constexpr ThemeColor with_alpha(float alpha) const noexcept
    {
        ThemeColor copy = *this;
        copy.alpha_     = alpha;
        return copy;
    }
    /// The color under `theme`. The resource must be set.
    [[nodiscard]] Color operator()(const Theme& theme) const
    {
        const Color color = read_(theme);
        return alpha_ ? color.with_alpha(*alpha_) : color;
    }
    /// Whether a resource is set.
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return read_ != nullptr;
    }

    /// The same function and the same alpha.
    friend constexpr bool operator==(const ThemeColor&, const ThemeColor&) = default;
    /// True when no resource is set.
    friend constexpr bool operator==(const ThemeColor& color, std::nullptr_t) noexcept
    {
        return color.read_ == nullptr;
    }

private:
    Read                 read_ = nullptr;
    std::optional<float> alpha_{};
};

/// What a color field takes: a fixed Color, a Brush (a gradient or acrylic), a ThemeColor read
/// from the theme each time it is drawn, or nothing, which keeps the control's own color. A
/// Color, an optional Color, any Brush, a ThemeColor and a captureless lambda taking a
/// `const Theme&` all convert to it.
///
/// A control that paints one flat color (a text foreground, a background behind a Border, a
/// table's row banding) paints a gradient as its first stop and an acrylic brush as its
/// `fallback_color`. Shapes paint every kind.
///
/// Example:
/// @code
/// ui::Property<ui::Color> picked{ui::colors::White};
/// ui::Property<bool>      dark{false};
///
/// ui::Text{.text = "Fixed", .foreground = ui::Color::from_rgb(196, 43, 28)};
/// ui::Text{.text = "Themed", .foreground = [](const ui::Theme& t) { return t.colors().text_secondary; }};
/// ui::Rectangle{.fill = ui::LinearGradientBrush{{{0.0f, ui::colors::White}, {1.0f, ui::colors::Black}}}};
/// ui::Rectangle{.fill = picked}; // followed
/// ui::Rectangle{.fill = ui::bind(dark, [](bool on) { return on ? ui::colors::Black : ui::colors::White; })};
/// @endcode
class Paint
{
public:
    /// Nothing: the control keeps its own color.
    Paint() noexcept = default;
    /// Nothing, as the default.
    Paint(std::nullopt_t) noexcept // NOLINT(google-explicit-constructor)
    {
    }
    /// A fixed color.
    Paint(Color color) noexcept : brush_(std::in_place, color) // NOLINT(google-explicit-constructor)
    {
    }
    /// A fixed color, or nothing.
    Paint(std::optional<Color> color) noexcept // NOLINT(google-explicit-constructor)
    {
        if (color)
        {
            brush_.emplace(*color);
        }
    }
    /// A Brush or one of its kinds: a LinearGradientBrush, RadialGradientBrush or AcrylicBrush.
    template <typename B>
        requires std::constructible_from<Brush, B> && (!std::same_as<std::remove_cvref_t<B>, Color>) &&
                 (!std::same_as<std::remove_cvref_t<B>, Paint>)
    Paint(B&& brush) : brush_(std::in_place, std::forward<B>(brush)) // NOLINT(google-explicit-constructor)
    {
    }
    /// A Brush, or nothing.
    Paint(std::optional<Brush> brush) noexcept : brush_(std::move(brush)) // NOLINT(google-explicit-constructor)
    {
    }
    /// A theme resource; a captureless lambda converts too. Null is nothing.
    template <typename F>
        requires std::convertible_to<F, ThemeColor> && (!std::same_as<std::remove_cvref_t<F>, std::nullptr_t>)
    Paint(F resource) noexcept : resource_(resource) // NOLINT(google-explicit-constructor)
    {
    }

    /// The fixed brush (a Color included); nullopt for a resource or nothing.
    [[nodiscard]] const std::optional<Brush>& brush() const noexcept
    {
        return brush_;
    }
    /// The fixed paint as one color: the Color, a gradient's first stop or an acrylic brush's
    /// `fallback_color`; nullopt for a resource or nothing.
    [[nodiscard]] std::optional<Color> color() const noexcept
    {
        return brush_ ? std::optional<Color>{flat(*brush_)} : std::nullopt;
    }
    /// The theme resource, or nullptr.
    [[nodiscard]] ThemeColor resource() const noexcept
    {
        return resource_;
    }
    /// True when neither a brush nor a resource is set.
    [[nodiscard]] bool empty() const noexcept
    {
        return !brush_ && resource_ == nullptr;
    }
    /// The paint under `theme` as one color (see color()); nullopt when empty.
    [[nodiscard]] std::optional<Color> resolve(const Theme& theme) const
    {
        return resource_ != nullptr ? std::optional<Color>{resource_(theme)} : color();
    }
    /// The paint under `theme` as a brush, for a control that paints gradients and acrylic;
    /// nullopt when empty.
    [[nodiscard]] std::optional<Brush> resolve_brush(const Theme& theme) const
    {
        return resource_ != nullptr ? std::optional<Brush>{Brush{resource_(theme)}} : brush_;
    }

    /// The one color a brush paints in a control that paints flat colors.
    [[nodiscard]] static Color flat(const Brush& brush) noexcept
    {
        if (const Color* c = std::get_if<Color>(&brush))
        {
            return *c;
        }
        if (const LinearGradientBrush* g = std::get_if<LinearGradientBrush>(&brush))
        {
            return g->stops.empty() ? colors::Transparent : g->stops.front().color;
        }
        if (const RadialGradientBrush* g = std::get_if<RadialGradientBrush>(&brush))
        {
            return g->stops.empty() ? colors::Transparent : g->stops.front().color;
        }
        return std::get<AcrylicBrush>(brush).fallback_color;
    }

    /// Same brush, or same resource.
    friend bool operator==(const Paint&, const Paint&) = default;

private:
    std::optional<Brush> brush_;
    ThemeColor           resource_ = nullptr;
};

namespace detail
{
// Hands a Paint to a pair of setters, as `set(ThemeColor)` for a resource and as
// `set(std::optional<Color>)` otherwise: how a spec's build() feeds a control that has both.
template <typename Set>
void set_paint(const Paint& paint, Set&& set)
{
    if (paint.resource() != nullptr)
    {
        set(paint.resource());
    }
    else
    {
        set(paint.color());
    }
}
} // namespace detail

} // namespace ui
