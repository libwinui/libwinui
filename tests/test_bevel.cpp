// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/TestRenderer.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>

using namespace ui;
using namespace ui::testing;

namespace
{
constexpr Rect  Bounds{10.0f, 20.0f, 100.0f, 40.0f};
constexpr Color Light      = Color::from_argb(0xFFE3E3E3);
constexpr Color Highlight  = colors::White;
constexpr Color Shadow     = Color::from_argb(0xFF808080);
constexpr Color DarkShadow = colors::Black;
} // namespace

TEST_CASE("draw_bevel_border is a no-op for Flat and Rounded", "[bevel]")
{
    for (BorderStyle style : {BorderStyle::Flat, BorderStyle::Rounded})
    {
        TestRenderer renderer;
        draw_bevel_border(renderer, Bounds, style, Light, Highlight, Shadow, DarkShadow, 1.0f);
        REQUIRE(renderer.count() == 0);
    }
}

TEST_CASE("draw_bevel_border emits eight StrokeLine edges: two rings per side", "[bevel]")
{
    TestRenderer renderer;
    draw_bevel_border(renderer, Bounds, BorderStyle::Raised, Light, Highlight, Shadow, DarkShadow, 1.0f);

    REQUIRE(renderer.count() == 8);
    REQUIRE(renderer.count_of<StrokeLine>() == 8);

    int light = 0, highlight = 0, shadow = 0, dark_shadow = 0;
    for (const auto& cmd : renderer.commands)
    {
        const auto& line = std::get<StrokeLine>(cmd);
        if (line.color == Light)
        {
            ++light;
        }
        else if (line.color == Highlight)
        {
            ++highlight;
        }
        else if (line.color == Shadow)
        {
            ++shadow;
        }
        else if (line.color == DarkShadow)
        {
            ++dark_shadow;
        }
    }
    REQUIRE(light == 2);
    REQUIRE(highlight == 2);
    REQUIRE(shadow == 2);
    REQUIRE(dark_shadow == 2);
}

TEST_CASE("Raised puts light+highlight on the top/left edges and dark_shadow+shadow on bottom/right", "[bevel]")
{
    TestRenderer renderer;
    draw_bevel_border(renderer, Bounds, BorderStyle::Raised, Light, Highlight, Shadow, DarkShadow, 1.0f);

    for (const auto& cmd : renderer.commands)
    {
        const auto& line = std::get<StrokeLine>(cmd);
        const bool top_or_left = (line.from.y == line.to.y && line.from.y <= Bounds.top() + 1.5f) || // a horizontal top edge
                                 (line.from.x == line.to.x && line.from.x <= Bounds.left() + 1.5f); // a vertical left edge
        if (top_or_left)
        {
            REQUIRE((line.color == Light || line.color == Highlight));
        }
        else
        {
            REQUIRE((line.color == Shadow || line.color == DarkShadow));
        }
    }
}

TEST_CASE("draw_bevel_border restricts drawing to the requested sides", "[bevel]")
{
    TestRenderer renderer;
    draw_bevel_border(renderer, Bounds, BorderStyle::Raised, Light, Highlight, Shadow, DarkShadow, 1.0f, BevelSides::Top);
    REQUIRE(renderer.count() == 2); // one ring pair, top only

    TestRenderer none_renderer;
    draw_bevel_border(none_renderer, Bounds, BorderStyle::Raised, Light, Highlight, Shadow, DarkShadow, 1.0f, BevelSides::None);
    REQUIRE(none_renderer.count() == 0);
}

namespace
{
const Color* horizontal_line_color_at(const TestRenderer& renderer, float y)
{
    for (const auto& cmd : renderer.commands)
    {
        const auto& line = std::get<StrokeLine>(cmd);
        if (line.from.y == line.to.y && line.from.y == y)
        {
            return &line.color;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("Sunken swaps the bevel shape relative to Raised", "[bevel]")
{
    TestRenderer raised;
    draw_bevel_border(raised, Bounds, BorderStyle::Raised, Light, Highlight, Shadow, DarkShadow, 1.0f);
    TestRenderer sunken;
    draw_bevel_border(sunken, Bounds, BorderStyle::Sunken, Light, Highlight, Shadow, DarkShadow, 1.0f);

    REQUIRE(raised.count() == sunken.count());

    // Both draw at the outer top and bottom rows, with opposite shading.
    const float top_y    = Bounds.top() + 0.5f;
    const float bottom_y = Bounds.bottom() - 0.5f;

    const Color* raised_top    = horizontal_line_color_at(raised, top_y);
    const Color* sunken_top    = horizontal_line_color_at(sunken, top_y);
    const Color* raised_bottom = horizontal_line_color_at(raised, bottom_y);
    const Color* sunken_bottom = horizontal_line_color_at(sunken, bottom_y);
    REQUIRE(raised_top != nullptr);
    REQUIRE(sunken_top != nullptr);
    REQUIRE(raised_bottom != nullptr);
    REQUIRE(sunken_bottom != nullptr);

    REQUIRE(*raised_top == Light);
    REQUIRE(*sunken_top == Shadow);
    REQUIRE(*raised_bottom == DarkShadow);
    REQUIRE(*sunken_bottom == Highlight);
}

TEST_CASE("The inner ring nests inside the outer one instead of overrunning its corners", "[bevel]")
{
    // IntDrawRectEdge pulls each inner-ring edge back one step at every corner whose two sides
    // are both drawn, so the outer ring owns its corner pixels.
    TestRenderer renderer;
    draw_bevel_border(renderer, Bounds, BorderStyle::Raised, Light, Highlight, Shadow, DarkShadow, 1.0f);

    const auto span = [&](const Color& color, bool horizontal) -> std::pair<float, float>
    {
        for (const auto& cmd : renderer.commands)
        {
            const auto& line = std::get<StrokeLine>(cmd);
            const bool  is_h = line.from.y == line.to.y;
            if (line.color == color && is_h == horizontal)
            {
                return horizontal ? std::pair{line.from.x, line.to.x} : std::pair{line.from.y, line.to.y};
            }
        }
        FAIL("no such edge");
        return {};
    };

    // Outer rings run the full span of their edge...
    REQUIRE(span(Light, true) == std::pair{Bounds.left(), Bounds.right()});
    REQUIRE(span(DarkShadow, true) == std::pair{Bounds.left(), Bounds.right()});
    // ...and inner rings stop one step short at both ends.
    REQUIRE(span(Highlight, true) == std::pair{Bounds.left() + 1.0f, Bounds.right() - 1.0f});
    REQUIRE(span(Shadow, true) == std::pair{Bounds.left() + 1.0f, Bounds.right() - 1.0f});
    REQUIRE(span(Highlight, false) == std::pair{Bounds.top() + 1.0f, Bounds.bottom() - 1.0f});
    REQUIRE(span(Shadow, false) == std::pair{Bounds.top() + 1.0f, Bounds.bottom() - 1.0f});
}

TEST_CASE("An edge with no adjoining side drawn keeps its inner ring full-length", "[bevel]")
{
    // The pull-back only applies where both sides of a corner are drawn.
    TestRenderer renderer;
    draw_bevel_border(renderer, Bounds, BorderStyle::Raised, Light, Highlight, Shadow, DarkShadow, 1.0f, BevelSides::Top);

    REQUIRE(renderer.count() == 2);
    for (const auto& cmd : renderer.commands)
    {
        const auto& line = std::get<StrokeLine>(cmd);
        REQUIRE(line.from.x == Bounds.left());
        REQUIRE(line.to.x == Bounds.right());
    }
}

TEST_CASE("rings == 1 draws the outer ring alone - the era's flat toolbar edge", "[bevel]")
{
    // A hot toolbar button is a single edge in the inner pair's colors, which is why its
    // caller collapses the ramp.
    TestRenderer renderer;
    draw_bevel_border(renderer,
                      Bounds,
                      BorderStyle::Raised,
                      Highlight,
                      Highlight,
                      Shadow,
                      Shadow,
                      1.0f,
                      BevelSides::All,
                      /*soft=*/false,
                      /*rings=*/1);

    REQUIRE(renderer.count() == 4);
    for (const auto& cmd : renderer.commands)
    {
        const auto& line = std::get<StrokeLine>(cmd);
        REQUIRE((line.color == Highlight || line.color == Shadow));
    }
}

// ---- Windows 3.x: a different construction, not this one with stops removed -----------

TEST_CASE("draw_win3x_border is a no-op for Flat and Rounded", "[bevel][win3x]")
{
    for (BorderStyle style : {BorderStyle::Flat, BorderStyle::Rounded})
    {
        TestRenderer renderer;
        draw_win3x_border(renderer, Bounds, style, DarkShadow, Highlight, Shadow, 1.0f);
        REQUIRE(renderer.count() == 0);
    }
}

TEST_CASE("draw_win3x_border raised is a 1px frame, a 1px highlight and a 2px shadow", "[bevel][win3x]")
{
    TestRenderer renderer;
    draw_win3x_border(renderer, Bounds, BorderStyle::Raised, DarkShadow, Highlight, Shadow, 1.0f);

    // One frame stroke plus four edges, where the 9x path draws eight.
    REQUIRE(renderer.count() == 5);
    REQUIRE(renderer.count_of<StrokeRoundedRect>() == 1);
    REQUIRE(renderer.count_of<StrokeLine>() == 4);

    // COLOR_WINDOWFRAME on all four sides, outside everything else. Its path is pulled in half a
    // step so the ring lands inside bounds.
    const auto& frame = std::get<StrokeRoundedRect>(renderer.commands[0]);
    CHECK(frame.color == DarkShadow);
    CHECK(frame.bounds == Bounds.inset(Thickness{0.5f}));
    CHECK(frame.stroke_width == 1.0f);

    // The era's signature: a 1 px highlight and a 2 px shadow.
    for (std::size_t i = 1; i < renderer.commands.size(); ++i)
    {
        const auto& line = std::get<StrokeLine>(renderer.commands[i]);
        CHECK(line.stroke_width == (line.color == Shadow ? 2.0f : 1.0f));
    }

    const auto& top   = std::get<StrokeLine>(renderer.commands[3]);
    const auto& right = std::get<StrokeLine>(renderer.commands[1]);
    CHECK(top.color == Highlight);
    CHECK(top.from.y == Bounds.top() + 1.5f); // frame, then the highlight's own center
    CHECK(right.color == Shadow);
    CHECK(right.from.x == Bounds.right() - 2.0f); // a 2px-wide stroke centered one in from the frame
}

TEST_CASE("draw_win3x_border pressed drops the bottom/right edge instead of mirroring it", "[bevel][win3x]")
{
    TestRenderer renderer;
    draw_win3x_border(renderer, Bounds, BorderStyle::Sunken, DarkShadow, Highlight, Shadow, 1.0f);

    // OBM_UPARROWD has no white at all: pressed drops the bottom/right edge instead of mirroring.
    REQUIRE(renderer.count() == 3);
    CHECK(std::get<StrokeRoundedRect>(renderer.commands[0]).color == DarkShadow);
    for (std::size_t i = 1; i < renderer.commands.size(); ++i)
    {
        const auto& line = std::get<StrokeLine>(renderer.commands[i]);
        CHECK(line.color == Shadow);
        CHECK(line.stroke_width == 1.0f);
    }
}

TEST_CASE("draw_win3x_border rounds its frame and nothing else", "[bevel][win3x]")
{
    // OBM_BTNCORNERS curves the outer outline only.
    TestRenderer renderer;
    draw_win3x_border(renderer, Bounds, BorderStyle::Raised, DarkShadow, Highlight, Shadow, 1.0f, /*radius=*/1.0f);

    // The path is pulled in half a step, so half a step less radius puts the ring's outer edge
    // on the requested curve.
    CHECK(std::get<StrokeRoundedRect>(renderer.commands[0]).radius == 0.5f);
    CHECK(renderer.count_of<StrokeLine>() == 4);
}

// A clip at bounds (SwipeControl clips its content to bounds()) must keep the whole frame.
TEST_CASE("draw_win3x_border draws entirely inside its bounds, so a clip at bounds keeps it", "[bevel][win3x]")
{
    for (BorderStyle style : {BorderStyle::Raised, BorderStyle::Sunken})
    {
        for (float step : {1.0f, 2.0f})
        {
            TestRenderer renderer;
            draw_win3x_border(renderer, Bounds, style, DarkShadow, Highlight, Shadow, step);

            for (const auto& cmd : renderer.commands)
            {
                // A stroke covers half its width either side of the path it runs along.
                if (std::holds_alternative<StrokeRoundedRect>(cmd))
                {
                    const auto& r    = std::get<StrokeRoundedRect>(cmd);
                    const float half = r.stroke_width * 0.5f;
                    CHECK(r.bounds.left() - half >= Bounds.left());
                    CHECK(r.bounds.top() - half >= Bounds.top());
                    CHECK(r.bounds.right() + half <= Bounds.right());
                    CHECK(r.bounds.bottom() + half <= Bounds.bottom());
                }
                else
                {
                    const auto& l    = std::get<StrokeLine>(cmd);
                    const float half = l.stroke_width * 0.5f;
                    // One of the two axes is the stroke's thickness; the other is its run.
                    CHECK(std::min(l.from.x, l.to.x) - half >= Bounds.left());
                    CHECK(std::min(l.from.y, l.to.y) - half >= Bounds.top());
                    CHECK(std::max(l.from.x, l.to.x) + half <= Bounds.right());
                    CHECK(std::max(l.from.y, l.to.y) + half <= Bounds.bottom());
                }
            }
        }
    }
}

// ---- The classic marks drawn per device pixel --------------------------------------------------

namespace
{
constexpr Color Well = Color::from_argb(0xFFFFFFFE); // distinct from Highlight, so the two read apart
constexpr Color Ink  = Color::from_argb(0xFF000001); // distinct from DarkShadow

// Rasterizes the solid rects a classic mark emits into a `w` x `h` device-pixel grid, one letter
// per color: S shadow, H highlight, K dark shadow, L light, w well, * ink, '.' nothing.
std::vector<std::string> raster(const TestRenderer& renderer, int w, int h)
{
    std::vector<std::string> grid(static_cast<std::size_t>(h), std::string(static_cast<std::size_t>(w), '.'));
    for (const auto& cmd : renderer.commands)
    {
        const auto* f = std::get_if<FillRoundedRect>(&cmd);
        REQUIRE(f != nullptr);
        CHECK(f->radius == 0.0f);
        char c = '?';
        if (f->color == Shadow)
        {
            c = 'S';
        }
        else if (f->color == Highlight)
        {
            c = 'H';
        }
        else if (f->color == DarkShadow)
        {
            c = 'K';
        }
        else if (f->color == Light)
        {
            c = 'L';
        }
        else if (f->color == Well)
        {
            c = 'w';
        }
        else if (f->color == Ink)
        {
            c = '*';
        }
        const float s  = renderer.scale;
        const int   x0 = static_cast<int>(std::lround(f->bounds.left() * s));
        const int   y0 = static_cast<int>(std::lround(f->bounds.top() * s));
        const int   x1 = static_cast<int>(std::lround(f->bounds.right() * s));
        const int   y1 = static_cast<int>(std::lround(f->bounds.bottom() * s));
        for (int y = y0; y < y1; ++y)
        {
            for (int x = x0; x < x1; ++x)
            {
                REQUIRE((x >= 0 && y >= 0 && x < w && y < h));
                CHECK(grid[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == '.'); // no overdraw
                grid[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = c;
            }
        }
    }
    return grid;
}
} // namespace

// DrawFrameControl(DFCS_BUTTONRADIO | DFCS_CHECKED) in a 13 x 13 rect: Marlett's `n` in WINDOW,
// `j`/`k` in BTNSHADOW/BTNHIGHLIGHT, `l`/`m` in 3DDKSHADOW/3DLIGHT and `i` in WINDOWTEXT, as GDI
// renders marlett.ttf at a 13 px cell.
TEST_CASE("draw_classic_radio at 96 DPI is the Marlett radio, pixel for pixel", "[bevel][radio][retro]")
{
    TestRenderer renderer;
    draw_classic_radio(renderer, Rect{0, 0, 13, 13}, Light, Highlight, Shadow, DarkShadow, Well, Ink);
    const std::vector<std::string> expected{
        ".....SSSS....",
        "...SSKKKKSS..",
        "..SKKwwwwKKH.",
        "..SKwwwwwwLH.",
        ".SKwww**wwwLH",
        ".SKww****wwLH",
        ".SKww****wwLH",
        ".SKwww**wwwLH",
        "..SKwwwwwwLH.",
        "..SLLwwwwLLH.",
        "...HHLLLLHH..",
        ".....HHHH....",
        ".............",
    };
    CHECK(raster(renderer, 13, 13) == expected);

    // Unchecked is the same well with no dot.
    TestRenderer empty;
    draw_classic_radio(empty, Rect{0, 0, 13, 13}, Light, Highlight, Shadow, DarkShadow, Well, std::nullopt);
    std::vector<std::string> no_dot = expected;
    for (std::string& row : no_dot)
    {
        std::replace(row.begin(), row.end(), '*', 'w');
    }
    CHECK(raster(empty, 13, 13) == no_dot);
}

TEST_CASE("draw_classic_radio is built at each device size, not scaled from 96 DPI", "[bevel][radio][retro]")
{
    struct Case
    {
        float scale;
        int   box;  // 13 DIPs in device pixels
        int   ring; // the outer ring's width, Marlett's at the same cell
        int   dot;
        int   edge; // one ring's thickness
    };
    for (const Case& c :
         {Case{1.25f, 16, 14, 6, 1}, Case{1.5f, 20, 18, 8, 1}, Case{1.75f, 23, 20, 8, 1}, Case{2.0f, 26, 24, 10, 2}})
    {
        CAPTURE(c.scale);
        TestRenderer renderer;
        renderer.scale = c.scale;
        draw_classic_radio(renderer, Rect{0, 0, 13, 13}, Light, Highlight, Shadow, DarkShadow, Well, Ink);
        const std::vector<std::string> grid = raster(renderer, c.box, c.box);

        int top = c.box, bottom = -1, left = c.box, right = -1, dot_rows = 0, edge = 0;
        for (int y = 0; y < c.box; ++y)
        {
            const std::string& row = grid[static_cast<std::size_t>(y)];
            if (row.find_first_not_of('.') != std::string::npos)
            {
                top    = std::min(top, y);
                bottom = std::max(bottom, y);
                left   = std::min(left, static_cast<int>(row.find_first_not_of('.')));
                right  = std::max(right, static_cast<int>(row.find_last_not_of('.')));
            }
            dot_rows += row.find('*') != std::string::npos ? 1 : 0;
        }
        // The middle row from the left: shadow pixels, then dark shadow, then the well.
        const std::string& middle = grid[static_cast<std::size_t>(top + c.ring / 2)];
        edge = static_cast<int>(middle.find_first_not_of('S', static_cast<std::size_t>(left))) - left;
        CHECK(right - left + 1 == c.ring);
        CHECK(bottom - top + 1 == c.ring);
        CHECK(dot_rows == c.dot);
        CHECK(edge == c.edge);
        // Symmetric about the split: the lit half's colors never reach the shaded side.
        CHECK(middle.find('S') < middle.find('H'));
        CHECK(middle.find('K') < middle.find('L'));
    }
}

TEST_CASE("draw_classic_arrow is the 7 x 4 scroll arrow, a true diagonal at any scale", "[bevel][retro]")
{
    TestRenderer down;
    draw_classic_arrow(down, Rect{0, 0, 13, 13}, ArrowDirection::Down, Ink);
    std::vector<std::string> grid = raster(down, 13, 13);
    // Marlett `6` at a 13 px cell.
    CHECK(grid[5] == "...*******...");
    CHECK(grid[6] == "....*****....");
    CHECK(grid[7] == ".....***.....");
    CHECK(grid[8] == "......*......");
    CHECK(grid[4] == ".............");
    CHECK(grid[9] == ".............");

    // Marlett `5`, a pixel higher in its cell.
    TestRenderer up;
    draw_classic_arrow(up, Rect{0, 0, 13, 13}, ArrowDirection::Up, Ink);
    grid = raster(up, 13, 13);
    CHECK(grid[4] == "......*......");
    CHECK(grid[7] == "...*******...");

    // Marlett `4`: four columns, seven rows tall.
    TestRenderer right;
    draw_classic_arrow(right, Rect{0, 0, 13, 13}, ArrowDirection::Right, Ink);
    grid = raster(right, 13, 13);
    CHECK(grid[3] == ".....*.......");
    CHECK(grid[6] == ".....****....");
    CHECK(grid[9] == ".....*.......");

    // 150 %: six rows, 11 to 1, each one pixel in from either end.
    TestRenderer scaled;
    scaled.scale = 1.5f;
    draw_classic_arrow(scaled, Rect{0, 0, 16, 16}, ArrowDirection::Down, Ink);
    std::vector<int> widths;
    for (const std::string& row : raster(scaled, 24, 24))
    {
        const auto n = std::count(row.begin(), row.end(), '*');
        if (n > 0)
        {
            widths.push_back(static_cast<int>(n));
        }
    }
    CHECK(widths == std::vector<int>{11, 9, 7, 5, 3, 1});
}

TEST_CASE("draw_classic_tree_button is a 9 x 9 box with a plus, or a minus once expanded", "[bevel][retro][tree]")
{
    TestRenderer collapsed;
    draw_classic_tree_button(collapsed, Rect{0, 0, 19, 17}, false, Shadow, Well, Ink);
    // Fills overlap here (frame, then well, then the sign), so read the last color per pixel.
    auto paint = [](const TestRenderer& r, int w, int h)
    {
        std::vector<std::string> grid(static_cast<std::size_t>(h), std::string(static_cast<std::size_t>(w), '.'));
        for (const auto& cmd : r.commands)
        {
            const auto& f = std::get<FillRoundedRect>(cmd);
            const char  c = f.color == Shadow ? 'S' : f.color == Well ? 'w' : '*';
            for (int y = static_cast<int>(f.bounds.top()); y < static_cast<int>(f.bounds.bottom()); ++y)
            {
                for (int x = static_cast<int>(f.bounds.left()); x < static_cast<int>(f.bounds.right()); ++x)
                {
                    grid[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = c;
                }
            }
        }
        return grid;
    };
    const std::vector<std::string> plus = paint(collapsed, 19, 17);
    CHECK(plus[3] == "...................");
    CHECK(plus[4] == ".....SSSSSSSSS.....");
    CHECK(plus[5] == ".....SwwwwwwwS.....");
    CHECK(plus[6] == ".....Swww*wwwS.....");
    CHECK(plus[8] == ".....Sw*****wS.....");
    CHECK(plus[10] == ".....Swww*wwwS.....");
    CHECK(plus[11] == ".....SwwwwwwwS.....");
    CHECK(plus[12] == ".....SSSSSSSSS.....");

    TestRenderer expanded;
    draw_classic_tree_button(expanded, Rect{0, 0, 19, 17}, true, Shadow, Well, Ink);
    const std::vector<std::string> minus = paint(expanded, 19, 17);
    CHECK(minus[6] == ".....SwwwwwwwS.....");
    CHECK(minus[8] == ".....Sw*****wS.....");
}
