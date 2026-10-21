// SPDX-License-Identifier: MIT
#pragma once

// What the library's own code needs around ui::strings(): filling a pattern's slots, and a
// Binding that follows set_strings().

#include "winui/core/Property.hpp"
#include "winui/core/Strings.hpp"

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include <cstdint>

namespace ui::detail
{

// `pattern` with each `{n}` replaced by args[n]; `{{` and `}}` write a brace. A slot with no
// argument, or a brace that opens no slot, is written as it is, so a broken translation shows
// its text rather than throwing.
[[nodiscard]] std::string fill(std::string_view pattern, std::initializer_list<std::string_view> args);

// Counts the set_strings() calls; a source for bind().
[[nodiscard]] const Property<std::uint64_t>& strings_revision();

// A Binding<std::string> of `get(strings())` that follows set_strings().
template <typename Fn>
[[nodiscard]] Binding<std::string> bind_strings(Fn get)
{
    return ui::bind(strings_revision(), [get = std::move(get)](std::uint64_t) { return std::string{get(strings())}; });
}

} // namespace ui::detail
