// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

namespace ui
{

/// Every argument, UTF-8 encoded, the program itself first (argv[0]'s place), so an app that
/// wants only what the user passed starts at index 1. A path argument goes through
/// path_from_utf8() (core/Utf8Path.hpp), not std::filesystem::path's own std::string constructor.
/// Re-reads the process command line on every call; empty only where no source exists.
///
/// Example:
/// @code
/// int main()
/// {
///     const std::vector<std::string> args = ui::command_line();
///     const std::filesystem::path folder =
///         args.size() > 1 ? ui::path_from_utf8(args[1]) : std::filesystem::current_path();
///     // ... open `folder` ...
///     return 0;
/// }
/// @endcode
[[nodiscard]] std::vector<std::string> command_line();

} // namespace ui
