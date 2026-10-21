// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ui
{

/// An SVG document as image content: the markup, kept as text, plus the natural size read from
/// its root element.
///
/// Nothing here interprets the drawing. Renderer::draw_svg() hands the markup to the renderer,
/// which draws a subset of SVG. Copies share one immutable markup string, so passing an
/// SvgImage around by value is cheap.
///
/// Example:
/// @code
/// const ui::SvgImage logo = ui::SvgImage::read("assets/logo.svg"); // throws if unreadable
/// const ui::SvgImage dot{R"(<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16">
///                                <circle cx="8" cy="8" r="6" fill="`#0078D4`"/></svg>)"};
///
/// // The logo drawn at 64x64, the dot at twice the 16x16 its root element gives.
/// window.set_content(ui::HorizontalStack{
///     .spacing = 8,
///     .content = {
///         ui::Image{.source = logo, .width = 64, .height = 64},
///         ui::Image{.source = dot, .width = 2 * dot.natural_size().width, .height = 2 * dot.natural_size().height},
///     },
/// });
/// @endcode
class SvgImage
{
public:
    /// An empty document; draws nothing.
    SvgImage() = default;
    /// UTF-8 SVG markup. Throws nothing: a document whose root cannot be read has a zero
    /// natural size, and a backend that cannot parse it draws nothing.
    explicit SvgImage(std::string markup);

    /// The file at `path`, read as UTF-8. Throws std::runtime_error if it cannot be read.
    [[nodiscard]] static SvgImage read(const std::filesystem::path& path);

    /// True when there is no markup.
    [[nodiscard]] bool empty() const
    {
        return !markup_ || markup_->empty();
    }
    /// The UTF-8 markup as given; valid while this SvgImage (or a copy of it) lives.
    [[nodiscard]] std::string_view markup() const
    {
        return markup_ ? std::string_view{*markup_} : std::string_view{};
    }
    /// The root element's size in DIPs (see svg_natural_size()); zero when it has none.
    [[nodiscard]] Size natural_size() const
    {
        return natural_size_;
    }

    /// Equal when the markup text is equal.
    friend bool operator==(const SvgImage& a, const SvgImage& b)
    {
        return a.markup() == b.markup();
    }

private:
    std::shared_ptr<const std::string> markup_;
    Size                               natural_size_{};
};

/// The natural size of an SVG document's root element, as SvgImage's constructor reads
/// it. Lengths may be unitless or in px, pt, pc, in, cm or mm (CSS's 96 px per inch);
/// percentages and font-relative units count as absent.
[[nodiscard]] Size svg_natural_size(std::string_view markup);

} // namespace ui
