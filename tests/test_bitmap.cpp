// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/render/Bitmap.hpp"

#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using namespace ui;

TEST_CASE("Integer blend is exact and deterministic", "[image][blend]")
{
    Bitmap img(1, 1, colors::White);
    img.blend(0, 0, Color::from_argb(0xFF0000FF), 128); // blue at ~50% coverage over white
    const Color got = img.get(0, 0);
    REQUIRE(got == Color::from_rgba(127, 127, 255, 255));
}

TEST_CASE("PNG encode/decode round-trips exactly", "[image][png]")
{
    Bitmap img(37, 19);
    for (int y = 0; y < img.height(); ++y)
    {
        for (int x = 0; x < img.width(); ++x)
        {
            img.set(x,
                    y,
                    Color::from_rgba(static_cast<std::uint8_t>(x * 7),
                                     static_cast<std::uint8_t>(y * 13),
                                     static_cast<std::uint8_t>((x + y) * 3),
                                     static_cast<std::uint8_t>(255 - x)));
        }
    }

    const std::vector<std::uint8_t> encoded = img.encode_png();
    const Bitmap                    decoded = Bitmap::decode_png(encoded);

    REQUIRE(decoded.width() == img.width());
    REQUIRE(decoded.height() == img.height());
    REQUIRE(decoded == img);
}

TEST_CASE("Encoded PNG has a valid signature", "[image][png]")
{
    Bitmap     img(4, 4, colors::White);
    const auto png = img.encode_png();
    REQUIRE(png.size() > 8);
    const std::uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (int i = 0; i < 8; ++i)
    {
        REQUIRE(png[i] == sig[i]);
    }
}

TEST_CASE("compare() detects size mismatch and channel deltas", "[image][diff]")
{
    Bitmap a(4, 4, colors::White);
    Bitmap b(4, 4, colors::White);
    REQUIRE(compare(a, b).same_size);
    REQUIRE(compare(a, b).max_channel_delta == 0);

    b.set(1, 1, Color::from_rgba(255, 250, 255, 255)); // green off by 5
    REQUIRE(compare(a, b).max_channel_delta == 5);
    REQUIRE(compare(a, b, /*threshold*/ 5).mismatched_pixels == 0);
    REQUIRE(compare(a, b, /*threshold*/ 4).mismatched_pixels == 1);

    Bitmap c(5, 4, colors::White);
    REQUIRE_FALSE(compare(a, c).same_size);
}

TEST_CASE("A premultiplied BGRA bitmap keeps its bytes and reads back as colors", "[image][pixel-format]")
{
    Bitmap page(2, 1, PixelFormat::Bgra8Premultiplied);
    CHECK(page.format() == PixelFormat::Bgra8Premultiplied);
    CHECK(page.get(0, 0) == colors::Transparent);

    // What a rasterizer writes: blue, then red at half alpha, both premultiplied.
    const std::vector<std::uint8_t> written{255, 0, 0, 255, 0, 0, 128, 128};
    std::copy(written.begin(), written.end(), page.bytes().begin());
    CHECK(std::vector<std::uint8_t>(page.bytes().begin(), page.bytes().end()) == written);
    CHECK(page.get(0, 0) == Color::from_rgba(0, 0, 255, 255));
    CHECK(page.get(1, 0) == Color::from_rgba(255, 0, 0, 128));

    // As straight RGBA, and back.
    const Bitmap straight = page.converted(PixelFormat::Rgba8);
    CHECK(straight.format() == PixelFormat::Rgba8);
    CHECK(std::vector<std::uint8_t>(straight.bytes().begin(), straight.bytes().end()) ==
          std::vector<std::uint8_t>{0, 0, 255, 255, 255, 0, 0, 128});
    CHECK(straight.converted(PixelFormat::Bgra8Premultiplied) == page);
    CHECK_FALSE(straight == page);
    CHECK(compare(straight, page).mismatched_pixels == 0);

    // set(), fill() and the PNG encoder work in it too.
    page.set(0, 0, Color::from_rgba(0, 255, 0, 255));
    CHECK(page.bytes()[1] == 255);
    CHECK(Bitmap::decode_png(page.encode_png()).get(0, 0) == Color::from_rgba(0, 255, 0, 255));
    page.fill(Color::from_rgba(255, 0, 0, 255));
    CHECK(page.bytes()[2] == 255);
    CHECK(page.bytes()[0] == 0);
}

TEST_CASE("A premultiplied bitmap blends, scales and turns in its own format", "[image][pixel-format]")
{
    Bitmap img(4, 2, PixelFormat::Bgra8Premultiplied);
    img.fill(colors::White);
    img.blend(0, 0, Color::from_argb(0xFF0000FF), 128);
    CHECK(img.get(0, 0) == Color::from_rgba(127, 127, 255, 255));

    const Bitmap half = img.scaled_to_fit(2, 1);
    CHECK(half.format() == PixelFormat::Bgra8Premultiplied);
    CHECK(half.width() == 2);
    CHECK(half.get(1, 0) == colors::White);

    const Bitmap turned = img.oriented(ImageOrientation::Rotate90);
    CHECK(turned.format() == PixelFormat::Bgra8Premultiplied);
    CHECK(turned.width() == 2);
    CHECK(turned.get(1, 0) == Color::from_rgba(127, 127, 255, 255));
}
