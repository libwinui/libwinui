// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Describe.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/media/Shape.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{

void check_rect(const Rect& r, float x, float y, float w, float h, float eps = 1e-3f)
{
    CHECK_THAT(r.x, WithinAbs(x, eps));
    CHECK_THAT(r.y, WithinAbs(y, eps));
    CHECK_THAT(r.width, WithinAbs(w, eps));
    CHECK_THAT(r.height, WithinAbs(h, eps));
}

Size measure(Element& e, Size available)
{
    static HeadlessTextMeasurer measurer;
    return e.measure(available, measurer, Theme::default_light());
}

void layout(Element& e, Size available, Rect slot)
{
    (void)measure(e, available);
    e.arrange(slot);
}

const DrawGeometry& only_geometry(const TestRenderer& r)
{
    REQUIRE(r.count() == 1);
    return std::get<DrawGeometry>(r.commands[0]);
}

} // namespace

// ---- PathGeometry ----

TEST_CASE("Path markup parses every command in absolute and relative form", "[geometry]")
{
    const auto g = PathGeometry::parse("M 10,100 C 100,25 300,250 400,75 H 200");
    REQUIRE(g);
    REQUIRE(g->figures.size() == 1);
    const PathFigure& f = g->figures[0];
    CHECK(f.start == Point{10.0f, 100.0f});
    REQUIRE(f.segments.size() == 2);
    CHECK(f.segments[0].kind == PathSegmentKind::Cubic);
    CHECK(f.segments[0].p3 == Point{400.0f, 75.0f});
    CHECK(f.segments[1].kind == PathSegmentKind::Line);
    CHECK(f.segments[1].p1 == Point{200.0f, 75.0f});
    CHECK_FALSE(f.closed);

    // Relative commands and implicit repeats: after `m`, further pairs are relative lines.
    const auto rel = PathGeometry::parse("m 10 10 5 0 0 5 z l 1 1");
    REQUIRE(rel);
    REQUIRE(rel->figures.size() == 2);
    CHECK(rel->figures[0].closed);
    CHECK(rel->figures[0].segments[1].p1 == Point{15.0f, 15.0f});
    // After `z` the current point is the figure's start, and a new figure starts there.
    CHECK(rel->figures[1].start == Point{10.0f, 10.0f});
    CHECK(rel->figures[1].segments[0].p1 == Point{11.0f, 11.0f});
}

TEST_CASE("Path markup reads the fill rule prefix and rejects garbage", "[geometry]")
{
    const auto g = PathGeometry::parse("F1 M 16,12 20,2L 20,16 1,16");
    REQUIRE(g);
    CHECK(g->fill_rule == FillRule::Nonzero);
    REQUIRE(g->figures.size() == 1);
    CHECK(g->figures[0].segments.size() == 3);
    check_rect(g->bounds(), 1.0f, 2.0f, 19.0f, 14.0f);

    CHECK(PathGeometry::parse("F0 M0,0 L1,1")->fill_rule == FillRule::EvenOdd);
    CHECK_FALSE(PathGeometry::parse("M 0,0 L 5").has_value());
    CHECK_FALSE(PathGeometry::parse("Q 1 2").has_value());
    CHECK(PathGeometry::parse("")->empty());
}

TEST_CASE("Smooth curves reflect the previous control point", "[geometry]")
{
    const auto g = PathGeometry::parse("M0,0 C 0,10 10,10 10,0 S 20,-10 20,0 Q 25,5 30,0 T 40,0");
    REQUIRE(g);
    const auto& s = g->figures[0].segments;
    REQUIRE(s.size() == 4);
    CHECK(s[1].p1 == Point{10.0f, -10.0f}); // reflection of (10,10) about (10,0)
    CHECK(s[3].kind == PathSegmentKind::Quadratic);
    CHECK(s[3].p1 == Point{35.0f, -5.0f}); // reflection of (25,5) about (30,0)
}

TEST_CASE("Arcs become cubics that end exactly where the arc does", "[geometry]")
{
    const auto g = PathGeometry::parse("M 0,50 A 50,50 0 0 1 100,50");
    REQUIRE(g);
    const auto& s = g->figures[0].segments;
    REQUIRE(s.size() == 2); // a half circle, two quarter pieces
    CHECK(s.back().p3 == Point{100.0f, 50.0f});
    // Sweep 1 (clockwise on screen) from the left to the right point goes over the top.
    const Rect b = g->bounds();
    CHECK_THAT(b.y, WithinAbs(0.0f, 0.05f));
    CHECK_THAT(b.height, WithinAbs(50.0f, 0.05f));
    CHECK_THAT(b.width, WithinAbs(100.0f, 0.05f));
}

TEST_CASE("Geometry bounds are tight around curves, not their control points", "[geometry]")
{
    const PathGeometry e = PathGeometry::ellipse(Point{40.0f, 70.0f}, 30.0f, 20.0f);
    check_rect(e.bounds(), 10.0f, 50.0f, 60.0f, 40.0f, 1e-2f);

    const auto c = PathGeometry::parse("M 0,0 C 0,100 100,100 100,0");
    REQUIRE(c);
    // The curve peaks at 75, three quarters of the way to its control points.
    CHECK_THAT(c->bounds().height, WithinAbs(75.0f, 1e-3));
}

TEST_CASE("A rounded rectangle is CRectangleGeometry's sixteen-point figure", "[geometry]")
{
    const PathGeometry g = PathGeometry::rectangle(Rect{0.0f, 0.0f, 100.0f, 50.0f}, 80.0f, 10.0f);
    REQUIRE(g.figures.size() == 1);
    const PathFigure& f = g.figures[0];
    CHECK(f.closed);
    CHECK(f.start == Point{0.0f, 10.0f});
    REQUIRE(f.segments.size() == 7); // four corners, three edges, the fourth edge closes it
    // radius_x is clamped to half the width.
    CHECK(f.segments[0].p3 == Point{50.0f, 0.0f});
    CHECK(f.segments[1].p1 == Point{50.0f, 0.0f});
    check_rect(g.bounds(), 0.0f, 0.0f, 100.0f, 50.0f);

    const PathGeometry square = PathGeometry::rectangle(Rect{1.0f, 2.0f, 3.0f, 4.0f});
    CHECK(square.figures[0].segments.size() == 3);
}

TEST_CASE("Fill hit testing follows the fill rule", "[geometry]")
{
    // Two nested squares: even-odd leaves a hole, nonzero (same direction) does not.
    std::vector<PathGeometry> parts{PathGeometry::rectangle(Rect{0.0f, 0.0f, 30.0f, 30.0f}),
                                    PathGeometry::rectangle(Rect{10.0f, 10.0f, 10.0f, 10.0f})};
    const PathGeometry        even    = PathGeometry::group(parts, FillRule::EvenOdd);
    const PathGeometry        nonzero = PathGeometry::group(parts, FillRule::Nonzero);
    CHECK(even.fill_contains(Point{5.0f, 5.0f}));
    CHECK_FALSE(even.fill_contains(Point{15.0f, 15.0f}));
    CHECK(nonzero.fill_contains(Point{15.0f, 15.0f}));
    CHECK_FALSE(even.fill_contains(Point{40.0f, 5.0f}));
}

TEST_CASE("Transforming a geometry moves every point", "[geometry]")
{
    const PathGeometry g = PathGeometry::line(Point{1.0f, 2.0f}, Point{3.0f, 4.0f})
                               .transformed(Matrix::scale(2.0f, 3.0f) * Matrix::translation(10.0f, 20.0f));
    CHECK(g.figures[0].start == Point{12.0f, 26.0f});
    CHECK(g.figures[0].segments[0].p1 == Point{16.0f, 32.0f});
}

// ---- Rectangle and Ellipse ----

TEST_CASE("A Rectangle asks for its explicit size only, and fills its slot otherwise", "[shape][layout]")
{
    RectangleControl rect;
    rect.set_fill(Color::from_rgba(70, 130, 180));
    CHECK(measure(rect, Size{300.0f, 200.0f}) == Size{0.0f, 0.0f});
    rect.arrange(Rect{10.0f, 10.0f, 300.0f, 200.0f});
    check_rect(rect.bounds(), 10.0f, 10.0f, 300.0f, 200.0f);

    rect.set_width(120.0f);
    rect.set_height(100.0f);
    CHECK(measure(rect, Size{300.0f, 200.0f}) == Size{120.0f, 100.0f});
}

TEST_CASE("A stroked Rectangle keeps its stroke inside its bounds", "[shape][render]")
{
    RectangleControl rect;
    rect.set_fill(Color::from_rgba(70, 130, 180));
    rect.set_stroke(colors::Black);
    rect.set_stroke_thickness(4.0f);
    rect.set_radius_x(10.0f);
    rect.set_radius_y(20.0f);
    rect.set_width(120.0f);
    rect.set_height(100.0f);
    layout(rect, Size{Unbounded, Unbounded}, Rect{0.0f, 0.0f, 120.0f, 100.0f});

    TestRenderer r;
    rect.render(r, Theme::light());
    const DrawGeometry& g = only_geometry(r);
    check_rect(g.geometry.bounds(), 2.0f, 2.0f, 116.0f, 96.0f);
    CHECK(g.stroke_style.thickness == 4.0f);
    CHECK(g.brush_box == rect.bounds());
    // Stroke as thick as a side: CRectangle's degenerate check keeps the whole rect.
    rect.set_stroke_thickness(200.0f);
    layout(rect, Size{Unbounded, Unbounded}, Rect{0.0f, 0.0f, 120.0f, 100.0f});
    TestRenderer thick;
    rect.render(thick, Theme::light());
    check_rect(only_geometry(thick).geometry.bounds(), 0.0f, 0.0f, 120.0f, 100.0f);
}

TEST_CASE("An Ellipse is inscribed in its outline rect, squared by Uniform", "[shape][render]")
{
    EllipseControl e;
    e.set_fill(colors::White);
    e.set_stroke(colors::Black);
    e.set_stroke_thickness(2.0f);
    layout(e, Size{150.0f, 100.0f}, Rect{0.0f, 0.0f, 150.0f, 100.0f});
    TestRenderer r;
    e.render(r, Theme::light());
    check_rect(only_geometry(r).geometry.bounds(), 1.0f, 1.0f, 148.0f, 98.0f, 1e-2f);

    // Uniform squares the rect at its top-left (it is not centered).
    e.set_stretch(Stretch::Uniform);
    layout(e, Size{150.0f, 100.0f}, Rect{0.0f, 0.0f, 150.0f, 100.0f});
    TestRenderer u;
    e.render(u, Theme::light());
    check_rect(only_geometry(u).geometry.bounds(), 1.0f, 1.0f, 98.0f, 98.0f, 1e-2f);

    // UniformToFill asks for a square of the larger bounded side.
    e.set_stretch(Stretch::UniformToFill);
    CHECK(measure(e, Size{150.0f, 100.0f}) == Size{150.0f, 150.0f});
    CHECK(measure(e, Size{Unbounded, 80.0f}) == Size{80.0f, 80.0f});
}

TEST_CASE("A shape with neither fill nor stroke, or no area, draws nothing", "[shape][render]")
{
    EllipseControl e;
    layout(e, Size{50.0f, 50.0f}, Rect{0.0f, 0.0f, 50.0f, 50.0f});
    TestRenderer none;
    e.render(none, Theme::light());
    CHECK(none.count() == 0);

    e.set_fill(colors::Black);
    layout(e, Size{50.0f, Unbounded}, Rect{0.0f, 0.0f, 50.0f, 0.0f});
    TestRenderer flat;
    e.render(flat, Theme::light());
    CHECK(flat.count() == 0);
}

// ---- Line, Polyline, Polygon, Path ----

TEST_CASE("A Line asks for its far corner plus half the stroke, and draws where it is", "[shape][layout]")
{
    LineControl line(Point{10.0f, 20.0f}, Point{250.0f, 60.0f});
    line.set_stroke(Color::from_rgba(70, 130, 180));
    line.set_stroke_thickness(6.0f);
    CHECK(measure(line, Size{Unbounded, Unbounded}) == Size{253.0f, 63.0f});

    line.arrange(Rect{100.0f, 50.0f, 253.0f, 63.0f});
    TestRenderer r;
    line.render(r, Theme::light());
    const DrawGeometry& g = only_geometry(r);
    CHECK(g.geometry.figures[0].start == Point{110.0f, 70.0f});
    CHECK(g.geometry.figures[0].segments[0].p1 == Point{350.0f, 110.0f});
    CHECK_FALSE(g.fill.has_value());

    // An unstroked line reserves no stroke room (the thickness counts as 0 without a stroke).
    LineControl bare(Point{0.0f, 0.0f}, Point{10.0f, 10.0f});
    bare.set_stroke_thickness(20.0f);
    CHECK(measure(bare, Size{Unbounded, Unbounded}) == Size{10.0f, 10.0f});
}

TEST_CASE("A stretched Path is scaled into its rect less the stroke", "[shape][layout]")
{
    PathControl path(*PathGeometry::parse("M 10,10 L 30,10 30,50 Z"));
    path.set_fill(colors::Black);
    path.set_stroke(colors::Black);
    path.set_stroke_thickness(2.0f);
    path.set_stretch(Stretch::Fill);

    // Stretching scales by (constraint - stroke) / natural per axis, plus the stroke.
    CHECK(measure(path, Size{102.0f, 42.0f}) == Size{102.0f, 42.0f});
    path.arrange(Rect{0.0f, 0.0f, 102.0f, 42.0f});
    TestRenderer r;
    path.render(r, Theme::light());
    check_rect(only_geometry(r).geometry.bounds(), 1.0f, 1.0f, 100.0f, 40.0f);

    // Uniform keeps the aspect ratio and centers the free axis.
    path.set_stretch(Stretch::Uniform);
    CHECK(measure(path, Size{102.0f, 42.0f}) == Size{22.0f, 42.0f});
    path.arrange(Rect{0.0f, 0.0f, 102.0f, 42.0f});
    TestRenderer u;
    path.render(u, Theme::light());
    check_rect(only_geometry(u).geometry.bounds(), 41.0f, 1.0f, 20.0f, 40.0f);

    // Unbounded, a stretched geometry keeps its natural size.
    path.set_stretch(Stretch::Fill);
    CHECK(measure(path, Size{Unbounded, Unbounded}) == Size{22.0f, 42.0f});
}

TEST_CASE("Polyline stays open and Polygon closes, both with their fill rule", "[shape][render]")
{
    const std::vector<Point> pts{{10.0f, 100.0f}, {60.0f, 40.0f}, {200.0f, 40.0f}, {250.0f, 100.0f}};
    PolylineControl          open(pts);
    open.set_stroke(colors::Black);
    PolygonControl closed(pts);
    closed.set_fill(Color::from_rgba(70, 130, 180));
    closed.set_fill_rule(FillRule::Nonzero);
    layout(open, Size{Unbounded, Unbounded}, Rect{0.0f, 0.0f, 300.0f, 200.0f});
    layout(closed, Size{Unbounded, Unbounded}, Rect{0.0f, 0.0f, 300.0f, 200.0f});

    TestRenderer a;
    open.render(a, Theme::light());
    CHECK_FALSE(only_geometry(a).geometry.figures[0].closed);
    TestRenderer b;
    closed.render(b, Theme::light());
    CHECK(only_geometry(b).geometry.figures[0].closed);
    CHECK(only_geometry(b).geometry.fill_rule == FillRule::Nonzero);
}

TEST_CASE("DrawGeometry describes its bounds and brushes", "[shape][describe]")
{
    RectangleControl rect;
    rect.set_fill(RadialGradientBrush{{{0.0f, Color::from_rgba(255, 255, 0)}, {1.0f, Color::from_rgba(0, 0, 255)}},
                                      Point{0.25f, 0.25f}});
    rect.set_width(200.0f);
    rect.set_height(200.0f);
    layout(rect, Size{Unbounded, Unbounded}, Rect{0.0f, 0.0f, 200.0f, 200.0f});
    TestRenderer r;
    rect.render(r, Theme::light());
    CHECK(describe(r.commands) ==
          "DrawGeometry bounds=(0.00,0.00,200.00,200.00) figures=1 evenodd fill=radial(center=(0.25,0.25) "
          "r=(0.50,0.50) "
          "origin=(0.50,0.50) relative pad [0.00:#FFFF00FF 1.00:#0000FFFF])\n");
}

// ---- Brushes ----

TEST_CASE("AcrylicBrush derives its tint and luminosity", "[brush][acrylic]")
{
    // The Fluent 2 light in-app token: a named luminosity opacity passes through as named.
    AcrylicBrush light;
    light.tint_color              = Color::from_rgba(0xFC, 0xFC, 0xFC);
    light.tint_opacity            = 0.0f;
    light.tint_luminosity_opacity = 0.85f;
    light.fallback_color          = Color::from_rgba(0xF9, 0xF9, 0xF9);
    CHECK(light.effective_tint().a == 0.0f);
    CHECK_THAT(light.effective_luminosity().a, WithinAbs(0.85, 1e-6));
    CHECK_THAT(light.effective_luminosity().r, WithinAbs(0xFC / 255.0, 1e-6));

    // No luminosity opacity: a pure black tint at full opacity is held to 85 %...
    AcrylicBrush black;
    black.tint_color   = colors::Black;
    black.tint_opacity = 1.0f;
    CHECK_THAT(black.effective_tint().a, WithinAbs(std::round(0.85 * 255.0) / 255.0, 1e-6));
    // ...and its luminosity color is the tint with V clamped up to 0.125, at the mapped opacity.
    const Color lum = black.effective_luminosity();
    CHECK_THAT(lum.r, WithinAbs(0.125, 1e-4));
    CHECK_THAT(lum.a, WithinAbs(1.0, 1e-6)); // 0.15 + 1 * 0.88 = 1.03, capped

    // White is held to 45 %, and an 80 % sample tint scales with it.
    AcrylicBrush white;
    white.tint_color   = colors::White;
    white.tint_opacity = 0.8f;
    CHECK_THAT(white.effective_tint().a, WithinAbs(std::round(0.8 * 0.45 * 255.0) / 255.0, 1e-6));
}

TEST_CASE("Acrylic describes itself by its derived colors", "[brush][acrylic][describe]")
{
    AcrylicBrush brush;
    brush.tint_color     = colors::Black;
    brush.tint_opacity   = 0.8f;
    brush.fallback_color = Color::from_rgba(0, 128, 0);
    RectangleControl rect;
    rect.set_fill(brush);
    layout(rect, Size{100.0f, 50.0f}, Rect{0.0f, 0.0f, 100.0f, 50.0f});
    TestRenderer r;
    rect.render(r, Theme::light());
    const std::string text = describe(r.commands);
    CHECK(text.find("fill=acrylic(tint=#000000AD") != std::string::npos); // 0.8 * 0.85 = 0.68
    CHECK(text.find("fallback=#008000FF") != std::string::npos);
}

// ---- specs ----

TEST_CASE("Shape specs build their controls and follow bound values", "[shape][spec]")
{
    Property<float>       thickness{2.0f};
    Property<float>       x2{200.0f};
    ui::Control<ui::Line> line;
    auto                  node = ui::build(
        ui::Line{.x1               = 0.0f,
                 .y1               = 50.0f,
                 .x2               = bind(x2, [](float v) { return v; }),
                 .y2               = 50.0f,
                 .stroke           = Color::from_rgba(70, 130, 180),
                 .stroke_thickness = bind(thickness, [](float v) { return v; }),
                 .control          = line});
    REQUIRE(line != nullptr);
    CHECK(line->stroke_thickness() == 2.0f);
    thickness = 8.0f;
    x2        = 250.0f;
    CHECK(line->stroke_thickness() == 8.0f);
    CHECK(line->to() == Point{250.0f, 50.0f});

    ui::Control<ui::Path> path;
    auto p = ui::build(ui::Path{.data = *PathGeometry::parse("M0,0 L10,10"), .stroke = colors::Black, .control = path});
    REQUIRE(path != nullptr);
    CHECK(path->data().figures.size() == 1);
    CHECK(path->stretch() == Stretch::None);

    ui::Control<ui::Rectangle> rect;
    auto rn = ui::build(ui::Rectangle{.fill = colors::White, .radius_x = 4.0f, .width = 30.0f, .control = rect});
    REQUIRE(rect != nullptr);
    CHECK(rect->stretch() == Stretch::Fill);
    CHECK(rect->radius_x() == 4.0f);
    CHECK(rect->width() == std::optional<float>{30.0f});
    CHECK(rect->fill().color() == colors::White);
}
