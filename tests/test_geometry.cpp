// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"

#include <type_traits>

#include <catch2/catch_test_macros.hpp>

using namespace ui;

TEST_CASE("Rect::contains treats left/top inclusive, right/bottom exclusive", "[geometry]")
{
    const Rect r{10, 20, 100, 40};        // x:[10,110) y:[20,60)
    REQUIRE(r.contains({10, 20}));        // top-left corner included
    REQUIRE(r.contains({60, 40}));        // interior
    REQUIRE_FALSE(r.contains({110, 40})); // right edge excluded
    REQUIRE_FALSE(r.contains({60, 60}));  // bottom edge excluded
    REQUIRE_FALSE(r.contains({9, 40}));
}

TEST_CASE("Rect derived accessors", "[geometry]")
{
    const Rect r{10, 20, 100, 40};
    REQUIRE(r.right() == 110.0f);
    REQUIRE(r.bottom() == 60.0f);
    REQUIRE(r.center() == Point{60, 40});
    REQUIRE(r.size() == Size{100, 40});
}

TEST_CASE("Rect::intersects and intersection", "[geometry]")
{
    const Rect a{0, 0, 100, 100};
    const Rect b{50, 50, 100, 100};
    const Rect c{200, 200, 10, 10};

    REQUIRE(a.intersects(b));
    REQUIRE_FALSE(a.intersects(c));
    REQUIRE_FALSE(a.intersects({100, 0, 10, 10})); // edge-touching is not overlap

    REQUIRE(intersection(a, b) == Rect{50, 50, 50, 50});
    REQUIRE(intersection(a, c).empty());
}

TEST_CASE("Rect::inset shrinks by thickness and never goes negative", "[geometry]")
{
    const Rect r{0, 0, 100, 50};
    REQUIRE(r.inset(Symmetric{.horizontal = 10, .vertical = 5}) == Rect{10, 5, 80, 40});

    const Rect small{0, 0, 10, 10};
    const Rect clamped = small.inset(Thickness{20});
    REQUIRE(clamped.width == 0.0f);
    REQUIRE(clamped.height == 0.0f);
}

TEST_CASE("Rect::inflate grows on every edge", "[geometry]")
{
    const Rect r{10, 10, 20, 20};
    REQUIRE(r.inflate(5) == Rect{5, 5, 30, 30});
}

TEST_CASE("Thickness horizontal/vertical sums", "[geometry]")
{
    const Thickness t = Symmetric{.horizontal = 16, .vertical = 8};
    REQUIRE(t.horizontal() == 32.0f);
    REQUIRE(t.vertical() == 16.0f);
    REQUIRE(Thickness{4}.horizontal() == 8.0f);
}

TEST_CASE("Color conversions and lerp", "[color]")
{
    REQUIRE(Color::from_rgba(255, 0, 0, 255) == Color{1.0f, 0.0f, 0.0f, 1.0f});
    REQUIRE(Color::from_argb(0xFF0078D4) == Color::from_rgba(0x00, 0x78, 0xD4, 0xFF));

    const Color mid = lerp(colors::Black, colors::White, 0.5f);
    REQUIRE(mid.r == 0.5f);
    REQUIRE(mid.a == 1.0f);

    REQUIRE(colors::White.with_alpha(0.25f) == Color{1.0f, 1.0f, 1.0f, 0.25f});
}

TEST_CASE("Color takes float channels and defaults to opaque", "[color]")
{
    STATIC_REQUIRE(Color{1.0f, 0.5f, 0.0f} == Color{1.0f, 0.5f, 0.0f, 1.0f});
    STATIC_REQUIRE(Color{0.25, 0.5, 0.75} == Color{0.25f, 0.5f, 0.75f}); // doubles too
    STATIC_REQUIRE(Color{} == colors::Black);
    // Color{255, 128, 0} stops at a static_assert naming from_rgb(); it cannot be checked here.
    STATIC_REQUIRE(Color::from_rgb(255, 128, 0) == Color{1.0f, 128 / 255.0f, 0.0f});
}

TEST_CASE("Thickness takes one uniform value, or names its edges", "[geometry]")
{
    const Thickness uniform = 8.0f;
    CHECK(uniform == Thickness{Edges{.left = 8, .top = 8, .right = 8, .bottom = 8}});
    const Thickness pair = Symmetric{.horizontal = 12.0f, .vertical = 6.0f};
    CHECK(pair == Edges{.left = 12.0f, .top = 6.0f, .right = 12.0f, .bottom = 6.0f});
    const Thickness left = Edges{.left = 32};
    CHECK((left.left == 32.0f && left.top == 0.0f && left.right == 0.0f && left.bottom == 0.0f));

    // Two or four plain numbers say nothing about which edge gets which, so they do not compile.
    STATIC_REQUIRE_FALSE(std::is_constructible_v<Thickness, float, float>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<Thickness, float, float, float, float>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<Edges, float>);
}

TEST_CASE("Color::from_rgb is opaque, where from_argb of the same number is transparent", "[geometry][color]")
{
    CHECK(Color::from_rgb(0x0078D4) == Color::from_argb(0xFF0078D4));
    CHECK(Color::from_argb(0x0078D4).a == 0.0f);
    CHECK(Color::from_rgb(0xFF0078D4) == Color::from_rgb(0x0078D4));
    CHECK(Color::from_rgb(0x00, 0x78, 0xD4) == Color::from_rgb(0x0078D4));
}
