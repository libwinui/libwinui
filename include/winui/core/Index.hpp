// SPDX-License-Identifier: MIT
#pragma once

#include <optional>

#include <cstddef>

// An index that may be "no item" is a std::optional<std::size_t> everywhere in the public API: a
// selection that can be empty, a lookup that can miss. Controls keep a plain std::size_t in their
// private state, with detail::NoIndex for none, and convert at the API.
namespace ui::detail
{

// "No item" in a private std::size_t index.
inline constexpr std::size_t NoIndex = static_cast<std::size_t>(-1);

// The public form of a private index: unset for NoIndex.
[[nodiscard]] constexpr std::optional<std::size_t> optional_index(std::size_t index) noexcept
{
    return index == NoIndex ? std::nullopt : std::optional<std::size_t>{index};
}

// The private form of a public index: NoIndex for unset.
[[nodiscard]] constexpr std::size_t index_or_none(std::optional<std::size_t> index) noexcept
{
    return index.value_or(NoIndex);
}

} // namespace ui::detail
