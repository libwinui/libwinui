// SPDX-License-Identifier: MIT
#pragma once

// The std::filesystem::path seams of core/Utf8.hpp, apart so that <filesystem> stays out of every
// header that only needs the string helpers.

#include "winui/core/Utf8.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace ui
{

/// A path from a UTF-8 string. Use it wherever a string becomes a path:
/// `std::filesystem::path(std::string)` reads the string in the system's legacy code page, which
/// mangles characters outside it.
///
/// Example:
/// @code
/// // A file name typed into a TextBox is UTF-8; the "ü" survives into the path.
/// ui::Property<std::string> file_name{"Zürich.png"};
/// const std::filesystem::path path = ui::path_from_utf8(file_name.get());
/// const ui::Bitmap picture{16, 16, ui::colors::White};
/// picture.write_png(path);
///
/// // path_to_utf8() goes back the other way, for a label.
/// ui::Property<std::string> status{"Saved " + ui::path_to_utf8(path.filename())}; // "Saved Zürich.png"
/// @endcode
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view text);

/// `path` as UTF-8, the inverse of path_from_utf8().
[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);

} // namespace ui
