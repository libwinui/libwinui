// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Duration.hpp"
#include "winui/core/Geometry.hpp"

#include <filesystem>
#include <span>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ui
{

/// How content is stretched into its destination rect. The Image element, Icon,
/// PersonPicture, AnimatedVisualPlayer, ScaleToFit and the shapes read it; fit_stretch() does the
/// math.
enum class Stretch
{
    None,          ///< Native size, centered, cropped by the destination if larger.
    Fill,          ///< Distorts to exactly fill the destination on both axes.
    Uniform,       ///< Fits inside, aspect kept, one axis letterboxed; the Image default.
    UniformToFill, ///< Covers the destination, aspect kept, one axis cropped (PersonPicture).
};

/// How a photo's stored pixels have to be turned to be seen upright: the EXIF orientation tag
/// (TIFF tag 274). The values are the tag's own; the names say what the correction is, the way
/// exiftool does. Rotations are clockwise.
enum class ImageOrientation : std::uint8_t
{
    Normal           = 1, ///< Already upright.
    MirrorHorizontal = 2, ///< Mirror left-right.
    Rotate180        = 3, ///< Rotate 180°.
    MirrorVertical   = 4, ///< Mirror top-bottom.
    Transpose        = 5, ///< Mirror left-right, then rotate 270°.
    Rotate90         = 6, ///< Rotate 90° clockwise (a portrait photo from most phones).
    Transverse       = 7, ///< Mirror left-right, then rotate 90°.
    Rotate270        = 8, ///< Rotate 270° clockwise.
};

/// Whether `orientation` swaps width and height.
[[nodiscard]] constexpr bool swaps_axes(ImageOrientation orientation)
{
    return static_cast<int>(orientation) >= 5;
}

/// What Bitmap::decode()/read() should do beyond decoding.
///
/// Example:
/// @code
/// // A 256px thumbnail of a camera photo, upright.
/// const ui::Bitmap thumb = ui::Bitmap::read("IMG_0042.jpg", {.max_width = 256, .max_height = 256});
/// @endcode
struct ImageDecodeOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The largest the result may be, aspect ratio kept; zero leaves that axis unconstrained.
    /// A picture already inside the bounds is never enlarged. The bounds apply to the upright
    /// picture, so a portrait photo stored sideways fits the same box as one stored upright.
    int max_width = 0;
    /// See `max_width`.
    int max_height = 0;
    /// Turn the pixels upright per the file's EXIF orientation.
    bool apply_orientation = true;
};

/// How a Bitmap's bytes are laid out: 4 bytes per pixel, row-major, rows packed with no padding.
enum class PixelFormat : std::uint8_t
{
    /// R, G, B, A with straight alpha: the default, and what the decoders and PNG give.
    Rgba8,
    /// B, G, R, A with each color already multiplied by alpha: what most rasterizers write,
    /// PDFium among them. Drawn as it is, with no conversion on the way.
    Bgra8Premultiplied,
};

/// A CPU bitmap, 4 bytes per pixel: RGBA with straight alpha unless made in another PixelFormat.
///
/// The picture type everything image-shaped uses: ImageSource and the Image element, Icon,
/// PersonPicture, IconSource::bitmap(), the clipboard, and pixel tests, which capture into one
/// and check it with compare(). PNG encodes and decodes byte for byte the same on every
/// platform. decode() and read() handle photos (JPEG, GIF and the other formats the system's
/// image codecs read) with optional downscaling and EXIF orientation. A value type: copying
/// copies the pixels.
///
/// A renderer that writes premultiplied BGRA can draw straight into a bitmap made in that format,
/// through bytes(), and the picture then reaches the screen with no pass over its pixels. get(),
/// set() and the encoders work in either format.
///
/// Example:
/// @code
/// // Load a photo for display, bounded to 1024px and upright.
/// ui::Bitmap photo = ui::Bitmap::read("holiday.jpg", {.max_width = 1024, .max_height = 1024});
/// window.set_content(ui::Image{.source = std::move(photo), .stretch = ui::Stretch::Uniform});
///
/// // Build one by hand, e.g. a color swatch for a palette export, and save it as PNG.
/// ui::Bitmap swatch{16, 16, ui::colors::White};
/// swatch.set(0, 0, ui::Color::from_rgba(255, 0, 0));
/// swatch.write_png("swatch.png"); // lossless: read_png() gives back the same pixels
///
/// // A page a PDF renderer draws right into the bitmap's bytes, shown as they are. Here the
/// // page is filled opaque white, the same in premultiplied BGRA.
/// ui::Bitmap page{612, 792, ui::PixelFormat::Bgra8Premultiplied};
/// std::ranges::fill(page.bytes(), std::uint8_t{0xFF});
/// @endcode
class Bitmap
{
public:
    /// An empty (0x0) image.
    Bitmap() = default;
    /// A `width` x `height` image, every pixel `fill` (transparent by default).
    Bitmap(int width, int height, const Color& fill = colors::Transparent);
    /// A `width` x `height` image in `format`, every pixel transparent: zero bytes, ready for a
    /// renderer to draw into through bytes().
    Bitmap(int width, int height, PixelFormat format);

    /// Width in pixels; 0 when empty.
    [[nodiscard]] int width() const
    {
        return width_;
    }
    /// Height in pixels; 0 when empty.
    [[nodiscard]] int height() const
    {
        return height_;
    }
    /// True when either dimension is zero (nothing to draw).
    [[nodiscard]] bool empty() const
    {
        return width_ <= 0 || height_ <= 0;
    }
    /// How the bytes are laid out; Rgba8 unless the bitmap was made in another format.
    [[nodiscard]] PixelFormat format() const
    {
        return format_;
    }
    /// The same picture with its bytes in `format`; a copy when it is already in it.
    [[nodiscard]] Bitmap converted(PixelFormat format) const;

    /// The pixel bytes in format()'s layout, row-major, 4 per pixel, width * 4 per row.
    [[nodiscard]] std::span<const std::uint8_t> bytes() const
    {
        return px_;
    }
    /// The pixel bytes, writable; width * height * 4 of them. Asking for them counts as a change
    /// to the pixels (content_id()).
    [[nodiscard]] std::span<std::uint8_t> bytes()
    {
        content_id_ = 0;
        return px_;
    }

    /// A number that stays the same while the pixels do and is never reused for other pixels: a
    /// copy shares it until either side is written to (fill(), set(), blend(), the writable
    /// bytes()). A renderer keeps the copy it uploaded to the GPU under it.
    [[nodiscard]] std::uint64_t content_id() const;

    /// Same size, format and bytes.
    friend bool operator==(const Bitmap& a, const Bitmap& b)
    {
        return a.width_ == b.width_ && a.height_ == b.height_ && a.format_ == b.format_ && a.px_ == b.px_;
    }

    /// Sets every pixel to `c` (a replace, not a blend).
    void fill(const Color& c);

    /// Replaces the pixel, ignoring what was there.
    void set(int x, int y, const Color& c);
    /// Reads a pixel back as a Color, with straight alpha in either format.
    [[nodiscard]] Color get(int x, int y) const;

    /// Source-over composite of `c` at coverage (0..255) onto the existing pixel.
    void blend(int x, int y, const Color& c, std::uint8_t coverage);

    // ---- PNG ----
    /// The image as PNG bytes, identical on every platform.
    [[nodiscard]] std::vector<std::uint8_t> encode_png() const;
    /// encode_png() written to `path`. Throws std::runtime_error if the file can't be written.
    void write_png(const std::filesystem::path& path) const;

    /// Decodes PNG bytes, identically on every platform.
    /// Throws std::runtime_error if `data` is not a readable PNG.
    [[nodiscard]] static Bitmap decode_png(const std::vector<std::uint8_t>& data);
    /// decode_png() of the file at `path`. Throws std::runtime_error if it can't be read.
    [[nodiscard]] static Bitmap read_png(const std::filesystem::path& path);

    // ---- JPEG ----
    /// Baseline JPEG at `quality` (1..100). Alpha is dropped; JPEG has none.
    [[nodiscard]] std::vector<std::uint8_t> encode_jpeg(int quality = 90) const;

    // ---- any supported format (see the class comment) ----
    /// Decodes an encoded image held in memory. Throws std::runtime_error if it can't.
    [[nodiscard]] static Bitmap decode(std::span<const std::uint8_t> data, const ImageDecodeOptions& options = {});
    /// Decodes the file at `path`. Any file name opens, whatever characters it holds. Throws
    /// std::runtime_error if the file can't be read or decoded. Thread-safe and meant to be
    /// called from worker threads, which need no setup for it.
    [[nodiscard]] static Bitmap read(const std::filesystem::path& path, const ImageDecodeOptions& options = {});
    /// read() into a `max_edge` × `max_edge` box, upright.
    [[nodiscard]] static Bitmap read_thumbnail(const std::filesystem::path& path, int max_edge);

    /// The EXIF orientation recorded in an encoded JPEG or TIFF, Normal when there is none.
    /// Reads only the header; decode()/read() already apply it unless told not to.
    [[nodiscard]] static ImageOrientation read_orientation(std::span<const std::uint8_t> data);

    // ---- transforms ----
    /// This image turned upright, given the `orientation` it was stored with.
    [[nodiscard]] Bitmap oriented(ImageOrientation orientation) const;
    /// This image shrunk to fit inside `max_width` × `max_height` (zero: unconstrained), aspect
    /// ratio kept, by area averaging with alpha weighting, so a transparent pixel's color doesn't
    /// bleed into its neighbors. Never enlarges: an image that already fits comes back as is.
    /// The result keeps the format.
    [[nodiscard]] Bitmap scaled_to_fit(int max_width, int max_height) const;

private:
    [[nodiscard]] std::size_t index(int x, int y) const
    {
        return (static_cast<std::size_t>(y) * width_ + x) * 4;
    }

    int                       width_  = 0;
    int                       height_ = 0;
    PixelFormat               format_ = PixelFormat::Rgba8;
    std::vector<std::uint8_t> px_;
    // 0 until content_id() hands one out, and again after every write.
    mutable std::uint64_t content_id_ = 0;
};

/// An animated picture: a decoded animated GIF. The frames are already
/// composited, each the whole picture as it should look at that point (the GIF's frame
/// offsets, disposal and transparency applied), so a player only ever shows one of them.
///
/// A frame delay under 20 ms is shown for 100 ms, because tools write 0 or 1 hundredths and
/// expect browsers to play it at 10 fps. `loop_count` is how many times the whole sequence
/// plays, as the GIF's loop block says: 1 when it has none, 0 for forever.
///
/// Example:
/// @code
/// ui::AnimatedImage spinner = ui::AnimatedImage::read("spinner.gif");
/// spinner.auto_play = false; // show the first frame until play() is called
/// window.set_content(ui::Image{.source = std::move(spinner)});
/// @endcode
struct AnimatedImage
{
    std::vector<Bitmap>   frames;         ///< Fully composited frames, all the same size.
    std::vector<Duration> delays;         ///< Display time of each frame; one per frame.
    int                   loop_count = 0; ///< Plays of the whole sequence; 0 loops forever.
    /// Whether an Image starts playing this the moment it is given it.
    /// Off, the first frame shows until something calls play().
    bool auto_play = true;

    /// True with no frames, or an empty first frame.
    [[nodiscard]] bool empty() const
    {
        return frames.empty() || frames.front().empty();
    }
    /// True with more than one frame.
    [[nodiscard]] bool animated() const
    {
        return frames.size() > 1;
    }
    /// The first frame's width in pixels; 0 with no frames.
    [[nodiscard]] int width() const
    {
        return frames.empty() ? 0 : frames.front().width();
    }
    /// The first frame's height in pixels; 0 with no frames.
    [[nodiscard]] int height() const
    {
        return frames.empty() ? 0 : frames.front().height();
    }

    /// Every frame of an animated GIF on every platform; any other format Bitmap::decode()
    /// reads comes back as its one frame. Throws std::runtime_error if it can't be decoded.
    [[nodiscard]] static AnimatedImage decode(std::span<const std::uint8_t> data);
    /// decode() of the file at `path`.
    [[nodiscard]] static AnimatedImage read(const std::filesystem::path& path);

    /// Same frames, delays, loop count and auto_play.
    friend bool operator==(const AnimatedImage&, const AnimatedImage&) = default;
};

/// Comparison result for golden-image tests, from compare().
///
/// Example:
/// @code
/// // A screen-capture tool that keeps a new capture only when it differs from the last one.
/// void keep_if_changed(const ui::Bitmap& capture, ui::Bitmap& last_capture, int& capture_index)
/// {
///     const ui::ImageDiff diff = ui::compare(capture, last_capture, 2); // ignore differences up to 2
///     if (!diff.same_size || diff.mismatched_pixels > 0)
///     {
///         capture.write_png(std::format("capture-{}.png", ++capture_index));
///         last_capture = capture;
///     }
/// }
/// @endcode
struct ImageDiff
{
    bool        same_size         = false; ///< False when the dimensions differ (nothing else is compared).
    int         max_channel_delta = 0;     ///< Largest absolute per-channel difference, 0..255.
    std::size_t mismatched_pixels = 0;     ///< Pixels differing by more than the threshold.
    std::size_t total_pixels      = 0;     ///< Pixels compared.
};

/// Compares two images. A pixel mismatches when any channel differs by more than `threshold`.
/// Two images in different formats are compared as RGBA.
[[nodiscard]] ImageDiff compare(const Bitmap& a, const Bitmap& b, int threshold = 0);

/// Fits a `natural` (unstretched) size into `bounds` per `stretch`, returning the resulting
/// centered destination rect. Renderer::draw_image() fits an image's pixel size the same way,
/// and AnimatedVisualPlayer and AnimatedIcon use this to fit caller-drawn content. With
/// UniformToFill and None the result may extend past `bounds`, so clip if that matters. A
/// zero-area `natural` size returns `bounds` unchanged.
///
/// Example:
/// @code
/// const ui::Rect dest = ui::fit_stretch(ui::Rect{0, 0, 200, 100}, ui::Size{50, 50},
///                                             ui::Stretch::Uniform);
/// // dest == {50, 0, 100, 100}: scaled to the height and centered horizontally.
/// @endcode
[[nodiscard]] Rect fit_stretch(const Rect& bounds, Size natural, Stretch stretch);

/// The size `width` × `height` shrinks to inside `max_width` × `max_height` (zero:
/// unconstrained), aspect ratio kept, rounded to whole pixels and never below 1. Never larger
/// than the input. What Bitmap::scaled_to_fit() and the decoders size their output by.
[[nodiscard]] std::pair<int, int> fit_within(int width, int height, int max_width, int max_height);

} // namespace ui
