// SPDX-License-Identifier: MIT
#include "winui/render/Bitmap.hpp"

#include "stb_image.h"
#include "stb_image_write.h"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Utf8Path.hpp"

#include <atomic>
#include <filesystem>
#include <ios>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include "../platform/windows/WicDecoder.hpp"
#endif

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include <cmath>

namespace ui
{
namespace
{

std::uint8_t to_u8(float v)
{
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return static_cast<std::uint8_t>(std::lround(v * 255.0f));
}

// A straight channel times alpha, and back, rounded.
std::uint8_t premultiply(int channel, int alpha)
{
    return static_cast<std::uint8_t>((channel * alpha + 127) / 255);
}
std::uint8_t unpremultiply(int channel, int alpha)
{
    return alpha == 0 ? std::uint8_t{0} : static_cast<std::uint8_t>(std::min(255, (channel * 255 + alpha / 2) / alpha));
}

// stb_image_write sink that appends the encoded bytes to a std::vector.
void append_to_vector(void* context, void* data, int size)
{
    auto*               out = static_cast<std::vector<std::uint8_t>*>(context);
    const std::uint8_t* p   = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), p, p + size);
}

} // namespace

Bitmap::Bitmap(int width, int height, const Color& fill_color) :
    width_(width),
    height_(height),
    px_(static_cast<std::size_t>(width) * height * 4, 0)
{
    if (fill_color != colors::Transparent)
    {
        fill(fill_color);
    }
}

Bitmap::Bitmap(int width, int height, PixelFormat format) :
    width_(width),
    height_(height),
    format_(format),
    px_(static_cast<std::size_t>(width) * height * 4, 0)
{
}

Bitmap Bitmap::converted(PixelFormat format) const
{
    if (format == format_)
    {
        return *this;
    }
    Bitmap out(width_, height_, format);
    // Both layouts swap R and B; one side multiplies by alpha.
    const bool to_premultiplied = format == PixelFormat::Bgra8Premultiplied;
    for (std::size_t i = 0; i + 3 < px_.size(); i += 4)
    {
        const int a = px_[i + 3];
        for (std::size_t c = 0; c < 3; ++c)
        {
            const int v    = px_[i + 2 - c];
            out.px_[i + c] = to_premultiplied ? premultiply(v, a) : unpremultiply(v, a);
        }
        out.px_[i + 3] = static_cast<std::uint8_t>(a);
    }
    return out;
}

std::uint64_t Bitmap::content_id() const
{
    if (content_id_ == 0)
    {
        static std::atomic<std::uint64_t> next{1};
        content_id_ = next.fetch_add(1, std::memory_order_relaxed);
    }
    return content_id_;
}

void Bitmap::fill(const Color& c)
{
    content_id_           = 0;
    const std::uint8_t a  = to_u8(c.a);
    std::uint8_t       p0 = to_u8(c.r), p1 = to_u8(c.g), p2 = to_u8(c.b);
    if (format_ == PixelFormat::Bgra8Premultiplied)
    {
        p0 = premultiply(to_u8(c.b), a);
        p1 = premultiply(to_u8(c.g), a);
        p2 = premultiply(to_u8(c.r), a);
    }
    for (std::size_t i = 0; i < px_.size(); i += 4)
    {
        px_[i]     = p0;
        px_[i + 1] = p1;
        px_[i + 2] = p2;
        px_[i + 3] = a;
    }
}

void Bitmap::set(int x, int y, const Color& c)
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
    {
        return;
    }
    content_id_          = 0;
    const std::size_t  i = index(x, y);
    const std::uint8_t a = to_u8(c.a);
    if (format_ == PixelFormat::Bgra8Premultiplied)
    {
        px_[i]     = premultiply(to_u8(c.b), a);
        px_[i + 1] = premultiply(to_u8(c.g), a);
        px_[i + 2] = premultiply(to_u8(c.r), a);
    }
    else
    {
        px_[i]     = to_u8(c.r);
        px_[i + 1] = to_u8(c.g);
        px_[i + 2] = to_u8(c.b);
    }
    px_[i + 3] = a;
}

Color Bitmap::get(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
    {
        return colors::Transparent;
    }
    const std::size_t i = index(x, y);
    if (format_ == PixelFormat::Bgra8Premultiplied)
    {
        const int a = px_[i + 3];
        return Color::from_rgba(unpremultiply(px_[i + 2], a),
                                unpremultiply(px_[i + 1], a),
                                unpremultiply(px_[i], a),
                                px_[i + 3]);
    }
    return Color::from_rgba(px_[i], px_[i + 1], px_[i + 2], px_[i + 3]);
}

void Bitmap::blend(int x, int y, const Color& c, std::uint8_t coverage)
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || coverage == 0)
    {
        return;
    }
    content_id_ = 0;
    // Effective source alpha = c.a * coverage, all in 0..255 integer math (src-over).
    const int src_a = (static_cast<int>(to_u8(c.a)) * coverage + 127) / 255;
    if (src_a == 0)
    {
        return;
    }
    const int         sr = to_u8(c.r), sg = to_u8(c.g), sb = to_u8(c.b);
    const std::size_t i = index(x, y);
    if (format_ == PixelFormat::Bgra8Premultiplied)
    {
        // Premultiplied source-over: the source's share plus what it leaves of the destination.
        const int  inv  = 255 - src_a;
        const auto over = [&](int s, int d)
        { return static_cast<std::uint8_t>(premultiply(s, src_a) + (d * inv + 127) / 255); };
        px_[i]     = over(sb, px_[i]);
        px_[i + 1] = over(sg, px_[i + 1]);
        px_[i + 2] = over(sr, px_[i + 2]);
        px_[i + 3] = static_cast<std::uint8_t>(src_a + (px_[i + 3] * inv + 127) / 255);
        return;
    }
    const int dr = px_[i], dg = px_[i + 1], db = px_[i + 2], da = px_[i + 3];
    const int inv = 255 - src_a;
    auto      mix = [&](int s, int d) { return (s * src_a + d * inv + 127) / 255; };
    px_[i]        = static_cast<std::uint8_t>(mix(sr, dr));
    px_[i + 1]    = static_cast<std::uint8_t>(mix(sg, dg));
    px_[i + 2]    = static_cast<std::uint8_t>(mix(sb, db));
    px_[i + 3]    = static_cast<std::uint8_t>(src_a + (da * inv + 127) / 255);
}

std::vector<std::uint8_t> Bitmap::encode_png() const
{
    if (format_ != PixelFormat::Rgba8)
    {
        return converted(PixelFormat::Rgba8).encode_png();
    }
    std::vector<std::uint8_t> out;
    const int                 stride = width_ * 4;
    if (stbi_write_png_to_func(&append_to_vector, &out, width_, height_, 4, px_.data(), stride) == 0)
    {
        throw std::runtime_error("png: encode failed");
    }
    return out;
}

void Bitmap::write_png(const std::filesystem::path& path) const
{
    // Through a stream rather than stbi_write_png(): stb opens its char* name with fopen(),
    // which reads the ANSI code page on Windows.
    const std::vector<std::uint8_t> png = encode_png();
    std::ofstream                   file{path, std::ios::binary};
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file)
    {
        throw std::runtime_error("png: cannot write: " + path_to_utf8(path));
    }
}

namespace
{

// Wrap decoded stb pixels (already forced to RGBA8) into an Image.
Bitmap image_from_stb(std::uint8_t* pixels, int w, int h, const char* what)
{
    if (!pixels)
    {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(std::string("png: ") + what + ": " + (reason ? reason : "decode failed"));
    }
    Bitmap img(w, h);
    std::copy(pixels, pixels + static_cast<std::size_t>(w) * h * 4, img.bytes().begin());
    stbi_image_free(pixels);
    return img;
}

} // namespace

Bitmap Bitmap::decode_png(const std::vector<std::uint8_t>& data)
{
    int           w = 0, h = 0, channels = 0;
    std::uint8_t* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &channels, 4);
    return image_from_stb(pixels, w, h, "decode");
}

Bitmap Bitmap::read_png(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    if (!file)
    {
        throw std::runtime_error("png: cannot open " + path_to_utf8(path));
    }
    const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    return decode_png(data);
}

std::vector<std::uint8_t> Bitmap::encode_jpeg(int quality) const
{
    if (format_ != PixelFormat::Rgba8)
    {
        return converted(PixelFormat::Rgba8).encode_jpeg(quality);
    }
    std::vector<std::uint8_t> out;
    if (stbi_write_jpg_to_func(&append_to_vector, &out, width_, height_, 4, px_.data(), std::clamp(quality, 1, 100)) == 0)
    {
        throw std::runtime_error("jpeg: encode failed");
    }
    return out;
}

// ---- decode() / read() ----

namespace
{

// The portable path: stb decodes the whole frame (it has no reduced-scale decode), then the
// same scaled_to_fit()/oriented() any caller can use make it small and upright.
[[maybe_unused]] Bitmap decode_portable(std::span<const std::uint8_t> data, const ImageDecodeOptions& options)
{
    int           w = 0, h = 0, channels = 0;
    std::uint8_t* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &channels, 4);
    if (!pixels)
    {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(std::string("image: ") + (reason ? reason : "decode failed"));
    }
    Bitmap image(w, h);
    std::copy(pixels, pixels + static_cast<std::size_t>(w) * h * 4, image.bytes().begin());
    stbi_image_free(pixels);

    const ImageOrientation orientation = options.apply_orientation ? Bitmap::read_orientation(data) : ImageOrientation::Normal;
    const bool swap = swaps_axes(orientation);
    return image
        .scaled_to_fit(swap ? options.max_height : options.max_width, swap ? options.max_width : options.max_height)
        .oriented(orientation);
}

// ---- EXIF ----

struct ByteReader
{
    std::span<const std::uint8_t> data;
    bool                          little_endian = false;

    [[nodiscard]] bool has(std::size_t offset, std::size_t count) const
    {
        return offset <= data.size() && count <= data.size() - offset;
    }
    [[nodiscard]] std::uint32_t u16(std::size_t offset) const
    {
        const std::uint32_t a = data[offset], b = data[offset + 1];
        return little_endian ? a | (b << 8) : (a << 8) | b;
    }
    [[nodiscard]] std::uint32_t u32(std::size_t offset) const
    {
        const std::uint32_t first = u16(offset), second = u16(offset + 2);
        return little_endian ? first | (second << 16) : (first << 16) | second;
    }
};

// `tiff` starts at a TIFF header ("II*" or "MM*" plus IFD0's offset); returns tag 274 from
// IFD0, or 0 when it isn't there.
unsigned orientation_from_tiff(std::span<const std::uint8_t> tiff)
{
    ByteReader r{tiff};
    if (!r.has(0, 8))
    {
        return 0;
    }
    if (tiff[0] == 'I' && tiff[1] == 'I')
    {
        r.little_endian = true;
    }
    else if (tiff[0] != 'M' || tiff[1] != 'M')
    {
        return 0;
    }
    if (r.u16(2) != 42)
    {
        return 0;
    }

    const std::size_t ifd = r.u32(4);
    if (!r.has(ifd, 2))
    {
        return 0;
    }
    const std::size_t count = r.u16(ifd);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t entry = ifd + 2 + i * 12;
        if (!r.has(entry, 12))
        {
            return 0;
        }
        // SHORT (type 3), one value, which sits in the first two bytes of the value field.
        if (r.u16(entry) == 274 && r.u16(entry + 2) == 3 && r.u32(entry + 4) == 1)
        {
            return r.u16(entry + 8);
        }
    }
    return 0;
}

unsigned orientation_from_jpeg(std::span<const std::uint8_t> jpeg)
{
    std::size_t pos = 2; // past SOI
    while (pos + 4 <= jpeg.size())
    {
        if (jpeg[pos] != 0xFF)
        {
            return 0; // lost sync: not a marker where one has to be
        }
        const std::uint8_t marker = jpeg[pos + 1];
        if (marker == 0xFF)
        {
            ++pos; // fill byte
            continue;
        }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8))
        {
            pos += 2; // standalone, no length
            continue;
        }
        if (marker == 0xDA || marker == 0xD9)
        {
            return 0; // start of scan / end of image: the metadata segments are behind us
        }

        const std::size_t length = (static_cast<std::size_t>(jpeg[pos + 2]) << 8) | jpeg[pos + 3];
        if (length < 2 || pos + 2 + length > jpeg.size())
        {
            return 0;
        }
        const std::span<const std::uint8_t> segment = jpeg.subspan(pos + 4, length - 2);
        static constexpr std::uint8_t       Exif[]  = {'E', 'x', 'i', 'f', 0, 0};
        if (marker == 0xE1 && segment.size() >= 6 && std::equal(std::begin(Exif), std::end(Exif), segment.begin()))
        {
            return orientation_from_tiff(segment.subspan(6));
        }
        pos += 2 + length;
    }
    return 0;
}

// ---- area-average scaling ----

// One input pixel an output pixel covers along one axis, and how much of the output it makes.
struct Tap
{
    int   index;
    float weight;
};

std::vector<std::vector<Tap>> area_taps(int from, int to)
{
    std::vector<std::vector<Tap>> taps(static_cast<std::size_t>(to));
    const double                  scale = static_cast<double>(from) / to;
    for (int o = 0; o < to; ++o)
    {
        const double begin = o * scale;
        const double end   = std::min(static_cast<double>(from), (o + 1) * scale);
        for (int i = static_cast<int>(begin); i < from && i < end; ++i)
        {
            const double covered = std::min<double>(i + 1, end) - std::max<double>(i, begin);
            if (covered > 0.0)
            {
                taps[static_cast<std::size_t>(o)].push_back({i, static_cast<float>(covered / scale)});
            }
        }
    }
    return taps;
}

} // namespace

Bitmap Bitmap::decode(std::span<const std::uint8_t> data, const ImageDecodeOptions& options)
{
    if (data.empty())
    {
        throw std::runtime_error("image: no data");
    }
#ifdef _WIN32
    win32::WicDecoded decoded = win32::wic_decode_memory(data, options);
    return decoded.image.oriented(decoded.orientation);
#else
    return decode_portable(data, options);
#endif
}

Bitmap Bitmap::read(const std::filesystem::path& path, const ImageDecodeOptions& options)
{
#ifdef _WIN32
    win32::WicDecoded decoded = win32::wic_decode_file(path, options);
    return decoded.image.oriented(decoded.orientation);
#else
    std::ifstream file{path, std::ios::binary};
    if (!file)
    {
        throw std::runtime_error("image: cannot open " + path_to_utf8(path));
    }
    const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    return decode_portable(data, options);
#endif
}

Bitmap Bitmap::read_thumbnail(const std::filesystem::path& path, int max_edge)
{
    return read(path, {.max_width = max_edge, .max_height = max_edge});
}

ImageOrientation Bitmap::read_orientation(std::span<const std::uint8_t> data)
{
    const unsigned tag = data.size() >= 2 && data[0] == 0xFF && data[1] == 0xD8
                             ? orientation_from_jpeg(data)
                             : orientation_from_tiff(data);
    return tag >= 1 && tag <= 8 ? static_cast<ImageOrientation>(tag) : ImageOrientation::Normal;
}

Bitmap Bitmap::oriented(ImageOrientation orientation) const
{
    if (orientation == ImageOrientation::Normal || empty())
    {
        return *this;
    }

    const int  w = width_, h = height_;
    const bool swap = swaps_axes(orientation);
    Bitmap     out(swap ? h : w, swap ? w : h, format_);

    // For every output pixel, the stored pixel it shows.
    for (int y = 0; y < out.height_; ++y)
    {
        for (int x = 0; x < out.width_; ++x)
        {
            int sx = x, sy = y;
            switch (orientation)
            {
                case ImageOrientation::Normal:
                    break;
                case ImageOrientation::MirrorHorizontal:
                    sx = w - 1 - x;
                    break;
                case ImageOrientation::Rotate180:
                    sx = w - 1 - x;
                    sy = h - 1 - y;
                    break;
                case ImageOrientation::MirrorVertical:
                    sy = h - 1 - y;
                    break;
                case ImageOrientation::Transpose:
                    sx = y;
                    sy = x;
                    break;
                case ImageOrientation::Rotate90:
                    sx = y;
                    sy = h - 1 - x;
                    break;
                case ImageOrientation::Transverse:
                    sx = w - 1 - y;
                    sy = h - 1 - x;
                    break;
                case ImageOrientation::Rotate270:
                    sx = w - 1 - y;
                    sy = x;
                    break;
            }
            std::copy_n(px_.begin() + static_cast<std::ptrdiff_t>(index(sx, sy)),
                        4,
                        out.px_.begin() + static_cast<std::ptrdiff_t>(out.index(x, y)));
        }
    }
    return out;
}

Bitmap Bitmap::scaled_to_fit(int max_width, int max_height) const
{
    if (empty())
    {
        return *this;
    }
    const auto [tw, th] = fit_within(width_, height_, max_width, max_height);
    if (tw == width_ && th == height_)
    {
        return *this;
    }

    const auto columns = area_taps(width_, tw);
    const auto rows    = area_taps(height_, th);

    // Summed premultiplied, so a transparent pixel adds nothing to its neighbors' color. Pixels
    // that already are premultiplied are summed as they are, and stay so.
    const bool         premultiplied = format_ == PixelFormat::Bgra8Premultiplied;
    Bitmap             out(tw, th, format_);
    std::vector<float> sum(static_cast<std::size_t>(tw) * 4);
    for (int y = 0; y < th; ++y)
    {
        std::fill(sum.begin(), sum.end(), 0.0f);
        for (const Tap& ry : rows[static_cast<std::size_t>(y)])
        {
            const std::uint8_t* src = px_.data() + index(0, ry.index);
            for (int x = 0; x < tw; ++x)
            {
                float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
                for (const Tap& cx : columns[static_cast<std::size_t>(x)])
                {
                    const std::uint8_t* p      = src + static_cast<std::size_t>(cx.index) * 4;
                    const float         alpha  = p[3] * cx.weight;
                    const float         weight = premultiplied ? cx.weight : alpha;
                    r += p[0] * weight;
                    g += p[1] * weight;
                    b += p[2] * weight;
                    a += alpha;
                }
                float* acc = sum.data() + static_cast<std::size_t>(x) * 4;
                acc[0] += r * ry.weight;
                acc[1] += g * ry.weight;
                acc[2] += b * ry.weight;
                acc[3] += a * ry.weight;
            }
        }

        auto channel = [](float v) { return static_cast<std::uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f)); };
        for (int x = 0; x < tw; ++x)
        {
            const float* acc = sum.data() + static_cast<std::size_t>(x) * 4;
            if (acc[3] <= 0.0f)
            {
                continue; // fully transparent, which Image(w, h) already is
            }
            const std::size_t o       = out.index(x, y);
            const float       divisor = premultiplied ? 1.0f : acc[3];
            out.px_[o]                = channel(acc[0] / divisor);
            out.px_[o + 1]            = channel(acc[1] / divisor);
            out.px_[o + 2]            = channel(acc[2] / divisor);
            out.px_[o + 3]            = channel(acc[3]);
        }
    }
    return out;
}

std::pair<int, int> fit_within(int width, int height, int max_width, int max_height)
{
    if (width <= 0 || height <= 0)
    {
        return {std::max(width, 0), std::max(height, 0)};
    }
    double scale = 1.0;
    if (max_width > 0)
    {
        scale = std::min(scale, static_cast<double>(max_width) / width);
    }
    if (max_height > 0)
    {
        scale = std::min(scale, static_cast<double>(max_height) / height);
    }
    if (scale >= 1.0)
    {
        return {width, height};
    }
    const int w = std::max(1, static_cast<int>(std::lround(width * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(height * scale)));
    return {max_width > 0 ? std::min(w, max_width) : w, max_height > 0 ? std::min(h, max_height) : h};
}

ImageDiff compare(const Bitmap& a, const Bitmap& b, int threshold)
{
    ImageDiff d;
    d.same_size = (a.width() == b.width() && a.height() == b.height());
    if (!d.same_size)
    {
        return d;
    }
    if (a.format() != b.format())
    {
        return compare(a.converted(PixelFormat::Rgba8), b.converted(PixelFormat::Rgba8), threshold);
    }
    d.total_pixels = static_cast<std::size_t>(a.width()) * static_cast<std::size_t>(a.height());
    const auto& pa = a.bytes();
    const auto& pb = b.bytes();
    for (std::size_t i = 0; i < pa.size(); i += 4)
    {
        int pixel_delta = 0;
        for (int c = 0; c < 4; ++c)
        {
            const int delta = std::abs(static_cast<int>(pa[i + c]) - static_cast<int>(pb[i + c]));
            pixel_delta     = std::max(pixel_delta, delta);
        }
        d.max_channel_delta = std::max(d.max_channel_delta, pixel_delta);
        if (pixel_delta > threshold)
        {
            ++d.mismatched_pixels;
        }
    }
    return d;
}

// ---- AnimatedImage ----

namespace
{

// The loop count: NETSCAPE2.0 (or ANIMEXTS1.0) carries {3, 1, count lo, count hi},
// none means play once. Found by its 14-byte header; a false match only misreads the loop count.
int gif_loop_count(std::span<const std::uint8_t> data)
{
    static constexpr std::uint8_t Netscape[] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0'};
    static constexpr std::uint8_t Animexts[] = {0x21, 0xFF, 0x0B, 'A', 'N', 'I', 'M', 'E', 'X', 'T', 'S', '1', '.', '0'};
    for (const auto& header : {std::span<const std::uint8_t>{Netscape}, std::span<const std::uint8_t>{Animexts}})
    {
        const auto it = std::search(data.begin(), data.end(), header.begin(), header.end());
        if (it == data.end())
        {
            continue;
        }
        const auto sub = it + static_cast<std::ptrdiff_t>(header.size());
        if (data.end() - sub >= 4 && sub[0] > 0 && sub[1] == 1)
        {
            return sub[2] | (sub[3] << 8);
        }
    }
    return 1;
}

} // namespace

AnimatedImage AnimatedImage::decode(std::span<const std::uint8_t> data)
{
    AnimatedImage result;
    const bool    is_gif = data.size() >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8';
    if (!is_gif)
    {
        result.frames.push_back(Bitmap::decode(data));
        result.delays.push_back(Duration{});
        result.loop_count = 1;
        return result;
    }

    int  w = 0, h = 0, count = 0, channels = 0;
    int* delays = nullptr;
    std::uint8_t*
        pixels = stbi_load_gif_from_memory(data.data(), static_cast<int>(data.size()), &delays, &w, &h, &count, &channels, 4);
    if (!pixels)
    {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(std::string("gif: ") + (reason ? reason : "decode failed"));
    }
    const std::size_t frame_bytes = static_cast<std::size_t>(w) * h * 4;
    for (int i = 0; i < count; ++i)
    {
        Bitmap frame(w, h);
        std::copy(pixels + frame_bytes * i, pixels + frame_bytes * (i + 1), frame.bytes().begin());
        result.frames.push_back(std::move(frame));
        // stb reports hundredths of a second times ten; a delay of 0 or 1 hundredths is shown
        // for ten.
        const int delay = delays ? delays[i] : 0;
        result.delays.push_back(Duration{static_cast<double>(delay < 20 ? 100 : delay)});
    }
    stbi_image_free(delays);
    stbi_image_free(pixels);
    result.loop_count = gif_loop_count(data);
    return result;
}

AnimatedImage AnimatedImage::read(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    if (!file)
    {
        throw std::runtime_error("image: cannot open " + path_to_utf8(path));
    }
    const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return decode(data);
}

Rect fit_stretch(const Rect& bounds, Size natural, Stretch stretch)
{
    if (natural.empty())
    {
        return bounds;
    }

    float width  = bounds.width;
    float height = bounds.height;

    switch (stretch)
    {
        case Stretch::Fill:
            break; // fills bounds exactly on both axes, already the default above
        case Stretch::None:
            width  = std::min(natural.width, bounds.width);
            height = std::min(natural.height, bounds.height);
            break;
        case Stretch::Uniform:
        case Stretch::UniformToFill:
        {
            const float scale_x = bounds.width / natural.width;
            const float scale_y = bounds.height / natural.height;
            const float scale   = stretch == Stretch::Uniform ? std::min(scale_x, scale_y) : std::max(scale_x, scale_y);
            width               = natural.width * scale;
            height              = natural.height * scale;
            break;
        }
    }

    return Rect{bounds.x + (bounds.width - width) * 0.5f, bounds.y + (bounds.height - height) * 0.5f, width, height};
}

} // namespace ui
