// SPDX-License-Identifier: MIT
#pragma once

#include "winui/Macros.hpp"

#ifndef LIBWINUI_IMPORTED

// Specs, list entries and option structs are written with designated initializers only:
// `ui::TableViewColumn{.header = "Name", .width = 160}`. Each one declares a DesignatedOnly as its
// first member, which nothing converts to, so a positional `{"Name", 160}` stops at that member
// with an error naming this type. Every call site then names its fields, and a field can be
// inserted anywhere without breaking one.
namespace ui::detail
{

// Not an aggregate (the constructor is user-declared), so brace elision cannot skip it.
struct DesignatedOnly
{
    constexpr DesignatedOnly() = default;

    friend constexpr bool operator==(DesignatedOnly, DesignatedOnly) noexcept = default;
};

} // namespace ui::detail

#endif // LIBWINUI_IMPORTED
