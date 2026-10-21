// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/SvgImage.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace ui
{

/// Where an ImageSource made by ImageSource::load() is. Every other source is Loaded.
enum class ImageLoadState
{
    Loaded,  ///< The picture is there (or the source was made from one).
    Loading, ///< Decoding on a background thread; the source draws nothing yet.
    Failed,  ///< The file could not be read or decoded; load_error() says why.
};

namespace detail
{
// The decode a load() started, shared by every copy of its ImageSource (src/render/ImageSource.cpp).
struct ImageLoad;
} // namespace detail

/// What an Image element shows: a still Bitmap, an AnimatedImage or an SvgImage, held by value.
///
/// Converts implicitly from each of the three, so `.source = photo` works whichever `photo` is.
/// Copying an ImageSource copies a still picture's pixels, so build one once and move it in.
/// load() instead decodes a file on a background thread: the source is empty while it loads,
/// and an Image showing it lays itself out again and raises on_image_opened (or
/// on_image_failed) once the decode is done.
///
/// Example:
/// @code
/// ui::Bitmap       photo = ui::Bitmap::read("photo.jpg", {.max_width = 800});
/// ui::ImageSource gif   = ui::AnimatedImage::read("spinner.gif");
/// ui::ImageSource logo  = ui::SvgImage::read("logo.svg");
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::Image{.source = std::move(photo), .stretch = ui::Stretch::UniformToFill, .height = 200},
///         ui::Image{.source = gif},
///         ui::Image{.source = logo, .width = 64, .height = 64},
///     },
/// });
/// @endcode
class ImageSource
{
public:
    /// An empty source; an Image showing it draws nothing.
    ImageSource() = default;
    /// A still picture.
    ImageSource(Bitmap image) : content_(std::move(image)) // NOLINT(google-explicit-constructor): a picture is a source
    {
    }
    /// An animated picture, which the Image element plays.
    ImageSource(AnimatedImage image) : content_(std::move(image)) // NOLINT(google-explicit-constructor)
    {
    }
    /// An SVG document.
    ImageSource(SvgImage svg) : content_(std::move(svg)) // NOLINT(google-explicit-constructor)
    {
    }

    /// Decodes the picture at `path` on a background thread, choosing the decoder by its
    /// extension: an SVG document for `.svg`, an animated image for `.gif`, a still picture
    /// otherwise. Copies share the decode.
    ///
    /// Example:
    /// @code
    /// ui::Image{.source = ui::ImageSource::load("photos/cliff.jpg"), .stretch = ui::Stretch::UniformToFill}
    /// @endcode
    [[nodiscard]] static ImageSource load(std::filesystem::path path);

    /// Loading or Failed for a load() still decoding or one that failed; Loaded otherwise.
    [[nodiscard]] ImageLoadState load_state() const;
    /// Why a load() failed; empty unless load_state() is Failed.
    [[nodiscard]] std::string load_error() const;

    /// True when there is no content, or the content itself is empty (zero-size or no frames).
    [[nodiscard]] bool empty() const
    {
        if (const Bitmap* image = bitmap())
        {
            return image->empty();
        }
        if (const AnimatedImage* image = animated())
        {
            return image->empty();
        }
        if (const SvgImage* image = svg())
        {
            return image->empty();
        }
        return true;
    }

    /// The content's own size in DIPs, one pixel to a DIP for a bitmap: what an Image with
    /// Stretch None draws at.
    [[nodiscard]] Size natural_size() const
    {
        if (const Bitmap* image = bitmap())
        {
            return {static_cast<float>(image->width()), static_cast<float>(image->height())};
        }
        if (const AnimatedImage* image = animated())
        {
            return {static_cast<float>(image->width()), static_cast<float>(image->height())};
        }
        if (const SvgImage* image = svg())
        {
            return image->natural_size();
        }
        return {};
    }

    /// The still picture, or nullptr when the source is something else (or still loading).
    [[nodiscard]] const Bitmap* bitmap() const
    {
        return std::get_if<Bitmap>(&content());
    }
    /// The animated picture, or nullptr when the source is something else (or still loading).
    [[nodiscard]] const AnimatedImage* animated() const
    {
        return std::get_if<AnimatedImage>(&content());
    }
    /// The SVG document, or nullptr when the source is something else (or still loading).
    [[nodiscard]] const SvgImage* svg() const
    {
        return std::get_if<SvgImage>(&content());
    }

    /// The same picture: equal content, or the same load().
    friend bool operator==(const ImageSource& a, const ImageSource& b)
    {
        return a.content_ == b.content_ && a.load_ == b.load_;
    }

private:
    friend class ImageControl; // when_loaded()

    using Content = std::variant<std::monostate, Bitmap, AnimatedImage, SvgImage>;
    // Calls `notify` on the decoding thread once the load() finishes, and returns true; returns
    // false, without calling it, when nothing is loading any more.
    bool when_loaded(Callback<void()> notify) const;
    // The decoded picture: this source's own, or a finished load()'s.
    [[nodiscard]] const Content& content() const;

    Content                            content_;
    std::shared_ptr<detail::ImageLoad> load_; // set by load()
};

} // namespace ui
