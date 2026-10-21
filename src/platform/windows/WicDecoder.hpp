// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "winui/render/Bitmap.hpp"

#include <filesystem>
#include <span>

namespace ui::win32
{

/// The stored pixels, scaled, plus the orientation they still need.
struct WicDecoded
{
    Bitmap           image;
    ImageOrientation orientation = ImageOrientation::Normal;
};

/// `options` as Bitmap::decode() takes them: the bounds are the upright picture's. Throw
/// std::runtime_error on failure.
[[nodiscard]] WicDecoded wic_decode_file(const std::filesystem::path& path, const ImageDecodeOptions& options);
[[nodiscard]] WicDecoded wic_decode_memory(std::span<const std::uint8_t> data, const ImageDecodeOptions& options);

} // namespace ui::win32

#endif // _WIN32
