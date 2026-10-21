// SPDX-License-Identifier: MIT
#pragma once

#include <concepts>
#include <optional>
#include <type_traits>

namespace ui
{

namespace detail
{
template <typename T>
concept OptionalFloatNumber = std::is_arithmetic_v<T> && !std::same_as<T, bool>;
} // namespace detail

/// A float that may be unset: the type of every optional size, length and value in an option field
/// or setter. Any number fills it (`.min_item_width = 120`, `set_width(240)`), `std::nullopt` or
/// `{}` leaves it unset, and it converts to and from `std::optional<float>`. Reading it works as
/// on an optional: test it, then dereference it or call value_or().
///
/// Example:
/// @code
/// const ui::TileLayout tiles{ .min_item_width = 120, .min_item_height = 90 };
///
/// auto panel = std::make_unique<ui::TilePanelControl>();
/// panel->set_item_width(std::nullopt); // back to the first item's width
/// @endcode
class OptionalFloat
{
public:
    /// Unset.
    constexpr OptionalFloat() noexcept = default;

    /// Unset.
    constexpr OptionalFloat(std::nullopt_t) noexcept
    {
    }

    /// Set to `value`, converted to float.
    template <detail::OptionalFloatNumber T>
    constexpr OptionalFloat(T value) noexcept : value_(static_cast<float>(value))
    {
    }

    /// The same value as `value`, set or unset.
    constexpr OptionalFloat(const std::optional<float>& value) noexcept : value_(value)
    {
    }

    /// The value as a std::optional.
    constexpr operator std::optional<float>() const noexcept
    {
        return value_;
    }

    /// Whether a value is set.
    [[nodiscard]] constexpr bool has_value() const noexcept
    {
        return value_.has_value();
    }

    /// Whether a value is set.
    constexpr explicit operator bool() const noexcept
    {
        return value_.has_value();
    }

    /// The value; it must be set.
    [[nodiscard]] constexpr float operator*() const noexcept
    {
        return *value_;
    }

    /// The value, or `fallback` when unset.
    [[nodiscard]] constexpr float value_or(float fallback) const noexcept
    {
        return value_.value_or(fallback);
    }

    /// Unsets the value.
    constexpr void reset() noexcept
    {
        value_.reset();
    }

    /// Equal when both are unset, or both hold the same value.
    friend constexpr bool operator==(const OptionalFloat&, const OptionalFloat&) = default;

    /// Equal when both are unset, or both hold the same value.
    friend constexpr bool operator==(const OptionalFloat& lhs, const std::optional<float>& rhs) noexcept
    {
        return lhs.value_ == rhs;
    }

    /// Equal when unset.
    friend constexpr bool operator==(const OptionalFloat& lhs, std::nullopt_t) noexcept
    {
        return !lhs.value_;
    }

    /// Equal when set to `rhs`.
    template <detail::OptionalFloatNumber T>
    friend constexpr bool operator==(const OptionalFloat& lhs, T rhs) noexcept
    {
        return lhs.value_ && *lhs.value_ == static_cast<float>(rhs);
    }

private:
    std::optional<float> value_{};
};

} // namespace ui
