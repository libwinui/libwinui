// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Utf8Path.hpp"
#include "winui/render/Bitmap.hpp"

#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstdlib>

using namespace ui;

namespace
{

const Color Red   = Color::from_rgba(230, 20, 20, 255);
const Color Green = Color::from_rgba(20, 200, 20, 255);
const Color Blue  = Color::from_rgba(20, 20, 230, 255);
const Color White = Color::from_rgba(250, 250, 250, 255);

// Stored the way a camera held sideways stores it: four flat quadrants, red top-left, green
// top-right, blue bottom-left, white bottom-right.
Bitmap quadrants(int width, int height)
{
    Bitmap image(width, height);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const bool right  = x >= width / 2;
            const bool bottom = y >= height / 2;
            image.set(x, y, bottom ? (right ? White : Blue) : (right ? Green : Red));
        }
    }
    return image;
}

bool near(const Color& a, const Color& b, int tolerance = 40)
{
    auto u8 = [](float v) { return static_cast<int>(std::lround(v * 255.0f)); };
    return std::abs(u8(a.r) - u8(b.r)) <= tolerance && std::abs(u8(a.g) - u8(b.g)) <= tolerance &&
           std::abs(u8(a.b) - u8(b.b)) <= tolerance && std::abs(u8(a.a) - u8(b.a)) <= tolerance;
}

// The colors at the centers of an image's four quadrants: TL, TR, BL, BR.
std::vector<Color> quadrant_samples(const Bitmap& image)
{
    const int w = image.width(), h = image.height();
    return {image.get(w / 4, h / 4), image.get(3 * w / 4, h / 4), image.get(w / 4, 3 * h / 4), image.get(3 * w / 4, 3 * h / 4)};
}

// An EXIF APP1 segment with only IFD0's orientation tag, right after SOI where a camera puts it.
std::vector<std::uint8_t> with_orientation(std::vector<std::uint8_t> jpeg, int orientation, bool big_endian = false)
{
    const auto o = static_cast<std::uint8_t>(orientation);
    // clang-format off
    const std::vector<std::uint8_t> tiff = big_endian
        ? std::vector<std::uint8_t>{'M', 'M', 0, 42, 0, 0, 0, 8,
                                    0, 1,                                    // one entry
                                    0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, o, 0, 0, // 274, SHORT, 1, value
                                    0, 0, 0, 0}                              // no next IFD
        : std::vector<std::uint8_t>{'I', 'I', 42, 0, 8, 0, 0, 0,
                                    1, 0,
                                    0x12, 0x01, 3, 0, 1, 0, 0, 0, o, 0, 0, 0,
                                    0, 0, 0, 0};
    // clang-format on
    std::vector<std::uint8_t> segment{0xFF, 0xE1, 0, 0, 'E', 'x', 'i', 'f', 0, 0};
    segment.insert(segment.end(), tiff.begin(), tiff.end());
    const std::size_t length = segment.size() - 2;
    segment[2]               = static_cast<std::uint8_t>(length >> 8);
    segment[3]               = static_cast<std::uint8_t>(length & 0xFF);
    jpeg.insert(jpeg.begin() + 2, segment.begin(), segment.end());
    return jpeg;
}

// A scratch directory whose name is outside every ANSI code page, removed again afterwards.
struct ScratchDir
{
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 path_from_utf8("winui-tests-Dürrenhauser-漢字");

    ScratchDir()
    {
        std::filesystem::create_directories(path);
    }
    ~ScratchDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream file{path, std::ios::binary};
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("fit_within keeps the aspect ratio and never enlarges", "[image][decode]")
{
    REQUIRE(fit_within(400, 200, 100, 100) == std::pair{100, 50});
    REQUIRE(fit_within(200, 400, 100, 100) == std::pair{50, 100});
    REQUIRE(fit_within(400, 200, 0, 50) == std::pair{100, 50});   // width unconstrained
    REQUIRE(fit_within(400, 200, 100, 0) == std::pair{100, 50});  // height unconstrained
    REQUIRE(fit_within(40, 20, 100, 100) == std::pair{40, 20});   // already fits
    REQUIRE(fit_within(400, 200, 0, 0) == std::pair{400, 200});   // no bounds at all
    REQUIRE(fit_within(10000, 1, 100, 100) == std::pair{100, 1}); // never below one pixel
}

TEST_CASE("scaled_to_fit averages areas, weighted by alpha", "[image][decode]")
{
    SECTION("an exact 2:1 reduction is the mean of each 2x2 block")
    {
        Bitmap image(4, 2);
        image.set(0, 0, Color::from_rgba(0, 0, 0, 255));
        image.set(1, 0, Color::from_rgba(100, 0, 0, 255));
        image.set(0, 1, Color::from_rgba(200, 0, 0, 255));
        image.set(1, 1, Color::from_rgba(100, 0, 0, 255));
        for (int y = 0; y < 2; ++y)
        {
            image.set(2, y, colors::White);
            image.set(3, y, colors::White);
        }

        const Bitmap half = image.scaled_to_fit(2, 1);
        REQUIRE(half.width() == 2);
        REQUIRE(half.height() == 1);
        REQUIRE(half.get(0, 0) == Color::from_rgba(100, 0, 0, 255));
        REQUIRE(half.get(1, 0) == colors::White);
    }

    SECTION("a transparent pixel's color does not bleed")
    {
        Bitmap image(2, 1);
        image.set(0, 0, Color::from_rgba(255, 0, 0, 255));
        image.set(1, 0, Color::from_rgba(0, 0, 255, 0)); // blue, but fully transparent
        const Bitmap one = image.scaled_to_fit(1, 1);
        REQUIRE(one.get(0, 0) == Color::from_rgba(255, 0, 0, 128));
    }

    SECTION("an image that already fits comes back unchanged")
    {
        const Bitmap image = quadrants(8, 4);
        REQUIRE(image.scaled_to_fit(8, 8) == image);
        REQUIRE(image.scaled_to_fit(0, 0) == image);
    }

    SECTION("a non-integer ratio keeps flat regions flat")
    {
        const Bitmap small = quadrants(90, 60).scaled_to_fit(40, 40);
        REQUIRE(small.width() == 40);
        REQUIRE(small.height() == 27);
        const auto samples = quadrant_samples(small);
        REQUIRE(samples[0] == Red);
        REQUIRE(samples[3] == White);
    }
}

TEST_CASE("oriented() turns every EXIF orientation upright", "[image][decode]")
{
    // a b c
    // d e f
    Bitmap stored(3, 2);
    const Color a = Color::from_rgba(1, 0, 0, 255), b = Color::from_rgba(2, 0, 0, 255), c = Color::from_rgba(3, 0, 0, 255);
    const Color d = Color::from_rgba(4, 0, 0, 255), e = Color::from_rgba(5, 0, 0, 255), f = Color::from_rgba(6, 0, 0, 255);
    stored.set(0, 0, a);
    stored.set(1, 0, b);
    stored.set(2, 0, c);
    stored.set(0, 1, d);
    stored.set(1, 1, e);
    stored.set(2, 1, f);

    auto rows = [](const Bitmap& image)
    {
        std::vector<std::vector<Color>> out(static_cast<std::size_t>(image.height()));
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                out[static_cast<std::size_t>(y)].push_back(image.get(x, y));
            }
        }
        return out;
    };
    using Rows = std::vector<std::vector<Color>>;

    REQUIRE(rows(stored.oriented(ImageOrientation::Normal)) == Rows{{a, b, c}, {d, e, f}});
    REQUIRE(rows(stored.oriented(ImageOrientation::MirrorHorizontal)) == Rows{{c, b, a}, {f, e, d}});
    REQUIRE(rows(stored.oriented(ImageOrientation::Rotate180)) == Rows{{f, e, d}, {c, b, a}});
    REQUIRE(rows(stored.oriented(ImageOrientation::MirrorVertical)) == Rows{{d, e, f}, {a, b, c}});
    REQUIRE(rows(stored.oriented(ImageOrientation::Transpose)) == Rows{{a, d}, {b, e}, {c, f}});
    REQUIRE(rows(stored.oriented(ImageOrientation::Rotate90)) == Rows{{d, a}, {e, b}, {f, c}});
    REQUIRE(rows(stored.oriented(ImageOrientation::Transverse)) == Rows{{f, c}, {e, b}, {d, a}});
    REQUIRE(rows(stored.oriented(ImageOrientation::Rotate270)) == Rows{{c, f}, {b, e}, {a, d}});

    REQUIRE(stored.oriented(ImageOrientation::Rotate90).oriented(ImageOrientation::Rotate270) == stored);
}

TEST_CASE("read_orientation finds the EXIF tag in a JPEG header", "[image][decode]")
{
    const std::vector<std::uint8_t> jpeg = quadrants(16, 16).encode_jpeg();

    REQUIRE(Bitmap::read_orientation(jpeg) == ImageOrientation::Normal);
    for (int o = 1; o <= 8; ++o)
    {
        REQUIRE(Bitmap::read_orientation(with_orientation(jpeg, o)) == static_cast<ImageOrientation>(o));
        REQUIRE(Bitmap::read_orientation(with_orientation(jpeg, o, /*big_endian*/ true)) ==
                static_cast<ImageOrientation>(o));
    }

    // Out of range, truncated, and not an image at all: all read as "already upright".
    REQUIRE(Bitmap::read_orientation(with_orientation(jpeg, 9)) == ImageOrientation::Normal);
    const std::vector<std::uint8_t> tagged = with_orientation(jpeg, 6);
    REQUIRE(Bitmap::read_orientation(std::span{tagged}.first(20)) == ImageOrientation::Normal);
    REQUIRE(Bitmap::read_orientation(std::vector<std::uint8_t>{1, 2, 3}) == ImageOrientation::Normal);
    REQUIRE(Bitmap::read_orientation({}) == ImageOrientation::Normal);
}

TEST_CASE("decode reads PNG and JPEG with the channels in RGBA order", "[image][decode]")
{
    const Bitmap source = quadrants(64, 32);

    SECTION("PNG, exact, alpha kept")
    {
        Bitmap with_alpha = source;
        with_alpha.set(0, 0, Color::from_rgba(10, 20, 30, 40));
        const Bitmap decoded = Bitmap::decode(with_alpha.encode_png());
        REQUIRE(decoded.width() == 64);
        REQUIRE(decoded.height() == 32);
        REQUIRE(decoded == with_alpha);
    }

    SECTION("JPEG, full size")
    {
        const Bitmap decoded = Bitmap::decode(source.encode_jpeg(95));
        REQUIRE(decoded.width() == 64);
        REQUIRE(decoded.height() == 32);
        const auto samples = quadrant_samples(decoded);
        CHECK(near(samples[0], Red));
        CHECK(near(samples[1], Green));
        CHECK(near(samples[2], Blue));
        CHECK(near(samples[3], White));
    }

    // A WIC scaler ahead of the RGBA conversion can report BGRA and turn red into blue. Only a
    // scaled decode goes through a scaler.
    SECTION("JPEG and PNG, scaled while decoding: red stays red")
    {
        for (const auto& encoded : {source.encode_jpeg(95), source.encode_png()})
        {
            const Bitmap decoded = Bitmap::decode(encoded, {.max_width = 32, .max_height = 32});
            REQUIRE(decoded.width() == 32);
            REQUIRE(decoded.height() == 16);
            const auto samples = quadrant_samples(decoded);
            CHECK(near(samples[0], Red));
            CHECK(near(samples[1], Green));
            CHECK(near(samples[2], Blue));
            CHECK(near(samples[3], White));
        }
    }
}

TEST_CASE("decode turns a sideways JPEG upright, bounds applying to the upright picture", "[image][decode]")
{
    // Stored landscape, tagged "rotate 90° clockwise": a portrait photo.
    const std::vector<std::uint8_t> jpeg = with_orientation(quadrants(64, 32).encode_jpeg(95), 6);

    SECTION("full size")
    {
        const Bitmap upright = Bitmap::decode(jpeg);
        REQUIRE(upright.width() == 32);
        REQUIRE(upright.height() == 64);
        // Turning clockwise brings the stored bottom-left to the top-left.
        const auto samples = quadrant_samples(upright);
        CHECK(near(samples[0], Blue));
        CHECK(near(samples[1], Red));
        CHECK(near(samples[2], White));
        CHECK(near(samples[3], Green));
    }

    SECTION("scaled: a 16-wide box holds the 16 x 32 portrait")
    {
        const Bitmap upright = Bitmap::decode(jpeg, {.max_width = 16, .max_height = 100});
        REQUIRE(upright.width() == 16);
        REQUIRE(upright.height() == 32);
        const auto samples = quadrant_samples(upright);
        CHECK(near(samples[0], Blue));
        CHECK(near(samples[1], Red));
    }

    SECTION("apply_orientation = false keeps the stored pixels")
    {
        const Bitmap stored = Bitmap::decode(jpeg, {.apply_orientation = false});
        REQUIRE(stored.width() == 64);
        REQUIRE(stored.height() == 32);
        CHECK(near(quadrant_samples(stored)[0], Red));
    }
}

TEST_CASE("read opens a file whose path is outside the ANSI code page", "[image][decode]")
{
    const ScratchDir dir;
    const auto       path = dir.path / "Gärten-ü.jpg";
    write_file(path, with_orientation(quadrants(400, 200).encode_jpeg(95), 8));

    const Bitmap full = Bitmap::read(path);
    REQUIRE(full.width() == 200);
    REQUIRE(full.height() == 400);

    const Bitmap thumb = Bitmap::read_thumbnail(path, 100);
    REQUIRE(thumb.width() == 50);
    REQUIRE(thumb.height() == 100);
    // Turning counter-clockwise brings the stored top-right to the top-left.
    const auto samples = quadrant_samples(thumb);
    CHECK(near(samples[0], Green));
    CHECK(near(samples[1], White));
    CHECK(near(samples[2], Red));
    CHECK(near(samples[3], Blue));
}

TEST_CASE("decode and read fail with an exception, not a garbage image", "[image][decode]")
{
    const std::vector<std::uint8_t> garbage{'n', 'o', 't', ' ', 'a', 'n', ' ', 'i', 'm', 'a', 'g', 'e'};
    REQUIRE_THROWS_AS(Bitmap::decode(garbage), std::runtime_error);
    REQUIRE_THROWS_AS(Bitmap::decode({}), std::runtime_error);
    REQUIRE_THROWS_AS(Bitmap::read(std::filesystem::temp_directory_path() / "winui-no-such-file.jpg"), std::runtime_error);
}
