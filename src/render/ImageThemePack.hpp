// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"

#include <string>
#include <unordered_map>
#include <utility>

namespace ui
{

/// `TMT_SIZINGMARGINS`'s nine-slice cuts (exactly CSS `border-image-slice`): the four
/// distances from each edge of a source image that separate its four corners (drawn
/// verbatim, unscaled) from its four edges (stretched along their one free axis) and its
/// center (stretched along both axes).
///
/// Example:
/// @code
/// // 3px corners in the source image; content inset 4px horizontally, 3px vertically.
/// const ui::NineSliceMetrics metrics{ui::Thickness{3}, ui::Symmetric{.horizontal = 4, .vertical = 3}};
/// @endcode
struct NineSliceMetrics
{
    Thickness slice_margins;   ///< The source image's nine-slice cuts, in image pixels.
    Thickness content_margins; ///< Where a caller's content goes, like Theme::padding (DIPs).

    friend bool operator==(const NineSliceMetrics&, const NineSliceMetrics&) = default;
};

/// A named registry of originally-authored images plus their nine-slice metrics: the
/// `TMT_IMAGEFILE`+`TMT_SIZINGMARGINS`+`TMT_CONTENTMARGINS` triple of a visual style, minus
/// every other `TMT_*` property real visual styles carry (state strips, DPI ladders, glyphs,
/// text effects, animation), deliberately.
///
/// Optional plumbing for skinned chrome: libwinui ships no pack, and the images must be
/// originally authored or freely licensed (never extracted from an `.msstyles`). A control asks
/// for an image by name with Renderer::draw_image_nine_slice(); a backend configured with a
/// pack draws the matching entry, and any other backend (or a missing name) draws nothing.
///
/// Example:
/// @code
/// ui::ImageThemePack pack;
/// pack.add("button.normal", ui::Bitmap::read_png("skin/button_normal.png"),
///          ui::NineSliceMetrics{ui::Thickness{3}, ui::Symmetric{.horizontal = 4, .vertical = 3}});
/// REQUIRE(pack.find_image("button.normal") != nullptr);
/// REQUIRE(pack.find_metrics("missing") == nullptr);
///
/// // In a control's render_override():
/// renderer.draw_image_nine_slice("button.normal", bounds(), ui::Thickness{3});
/// @endcode
class ImageThemePack
{
public:
    /// Adds `image` under `name`, replacing any entry of that name.
    void add(std::string name, Bitmap image, NineSliceMetrics metrics)
    {
        entries_.insert_or_assign(std::move(name), Entry{std::move(image), metrics});
    }

    /// The image registered as `name`, or null. Valid until that entry is replaced or the pack
    /// is destroyed.
    [[nodiscard]] const Bitmap* find_image(const std::string& name) const
    {
        auto it = entries_.find(name);
        return it == entries_.end() ? nullptr : &it->second.image;
    }

    /// The metrics registered with `name`, or null; same lifetime as find_image()'s result.
    [[nodiscard]] const NineSliceMetrics* find_metrics(const std::string& name) const
    {
        auto it = entries_.find(name);
        return it == entries_.end() ? nullptr : &it->second.metrics;
    }

    /// True with no entries, the common case: every draw_image_nine_slice() is then a no-op.
    [[nodiscard]] bool empty() const
    {
        return entries_.empty();
    }

private:
    struct Entry
    {
        Bitmap           image;
        NineSliceMetrics metrics;
    };
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace ui
