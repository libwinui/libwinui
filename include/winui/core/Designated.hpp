// SPDX-License-Identifier: MIT
#pragma once

// Specs, list entries and option structs are written with designated initializers only:
// `ui::TableViewColumn{.header = "Name", .width = 160}`. Each one declares a DesignatedOnly as its
// first member, which nothing converts to, so a positional `{"Name", 160}` stops at that member
// with an error naming this type. Every call site then names its fields, and a field can be
// inserted anywhere without breaking one.
// An empty member that takes no room. MSVC ignores the standard attribute to keep its ABI, and
// clang-cl matches that ABI (and warns that it ignores it), so both get nothing: the layout is the
// same whichever of the two compiles a translation unit.
#if defined(_MSC_VER)
#define LIBWINUI_NO_UNIQUE_ADDRESS
#else
#define LIBWINUI_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace ui::detail
{

// Not an aggregate (the constructor is user-declared), so brace elision cannot skip it.
struct DesignatedOnly
{
    constexpr DesignatedOnly() = default;

    friend constexpr bool operator==(DesignatedOnly, DesignatedOnly) noexcept = default;
};

} // namespace ui::detail
