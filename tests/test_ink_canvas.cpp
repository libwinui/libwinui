// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/media/InkCanvas.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr Color Red    = Color::from_rgba(230, 27, 27);
constexpr Color Blue   = Color::from_rgba(0, 77, 230);
constexpr Color Yellow = Color::from_rgba(255, 230, 0);

PointerEvent ink_down(Point p, PointerButton button = PointerButton::Left)
{
    return {p, button, PointerAction::Down};
}
PointerEvent ink_move(Point p)
{
    return {p, PointerButton::None, PointerAction::Move};
}
PointerEvent ink_up(Point p, PointerButton button = PointerButton::Left)
{
    return {p, button, PointerAction::Up};
}

// A pen frame at `pressure`, its eraser end down when `eraser`.
PointerEvent pen(PointerEvent e, float pressure, bool eraser = false)
{
    e.device   = PointerDeviceType::Pen;
    e.pressure = pressure;
    e.eraser   = eraser;
    return e;
}
PointerEvent touch(PointerEvent e)
{
    e.device = PointerDeviceType::Touch;
    return e;
}

// A canvas laid out at (10, 20), 200 by 100.
void place(InkCanvasControl& canvas)
{
    HeadlessTextMeasurer m;
    (void)canvas.measure({200.0f, 100.0f}, m, Theme::default_light());
    canvas.arrange({10.0f, 20.0f, 200.0f, 100.0f});
}

void draw(InkCanvasControl& canvas, std::vector<Point> path)
{
    canvas.handle_pointer(ink_down(path.front()));
    for (std::size_t i = 1; i < path.size(); ++i)
    {
        canvas.handle_pointer(ink_move(path[i]));
    }
    canvas.handle_pointer(ink_up(path.back()));
}

InkStroke line_stroke(Point a, Point b)
{
    return InkStroke{.points = {{.position = a}, {.position = b}}};
}

std::vector<DrawGeometry> geometries(const TestRenderer& r)
{
    std::vector<DrawGeometry> out;
    for (const auto& c : r.commands)
    {
        if (const auto* g = std::get_if<DrawGeometry>(&c))
        {
            out.push_back(*g);
        }
    }
    return out;
}
} // namespace

TEST_CASE("A new InkCanvasControl is empty and inks with a black 2 DIP round pen", "[ink-canvas]")
{
    InkCanvasControl canvas;
    REQUIRE(canvas.strokes().empty());
    REQUIRE(canvas.input_mode() == InkCanvasInputMode::Inking);
    REQUIRE(canvas.enabled());
    REQUIRE(canvas.wet_stroke() == nullptr);
    REQUIRE(canvas.drawing_attributes() == InkDrawingAttributes{});
    REQUIRE(canvas.drawing_attributes().color == colors::Black);
    REQUIRE(canvas.drawing_attributes().size == Size{2.0f, 2.0f});
    REQUIRE(canvas.drawing_attributes().pen_tip == InkPenTip::Circle);
    REQUIRE(canvas.drawing_attributes().fit_to_curve);
    REQUIRE(canvas.automation_role() == AutomationRole::Pane);
    REQUIRE(InkDrawingAttributes::for_pencil().kind == InkDrawingAttributesKind::Pencil);
}

TEST_CASE("The stroke container numbers strokes and reports every change", "[ink-canvas]")
{
    InkCanvasControl canvas;
    int              changes = 0;
    canvas.on_strokes_changed([&] { ++changes; });

    const auto a = canvas.strokes().add_stroke(line_stroke({0, 0}, {10, 0}));
    const auto b = canvas.strokes().add_stroke(line_stroke({0, 5}, {10, 5}));
    REQUIRE(a != 0);
    REQUIRE(b != a);
    REQUIRE(canvas.strokes().size() == 2);
    REQUIRE(changes == 2);
    REQUIRE(canvas.strokes().stroke_by_id(b)->points.front().position == Point{0, 5});

    // A stroke with no points is dropped.
    REQUIRE(canvas.strokes().add_stroke(InkStroke{}) == 0);
    REQUIRE(changes == 2);

    REQUIRE(canvas.strokes().remove_stroke(a));
    REQUIRE_FALSE(canvas.strokes().remove_stroke(a));
    REQUIRE(canvas.strokes().stroke_by_id(a) == nullptr);
    REQUIRE(changes == 3);

    canvas.strokes().add_strokes({line_stroke({0, 0}, {1, 1}), line_stroke({2, 2}, {3, 3})});
    REQUIRE(canvas.strokes().size() == 3);
    REQUIRE(changes == 4);

    // replace() renumbers what it is given, as one change.
    std::vector<InkStroke> saved{canvas.strokes().strokes().begin(), canvas.strokes().strokes().end()};
    canvas.strokes().replace(saved);
    REQUIRE(canvas.strokes().size() == 3);
    REQUIRE(canvas.strokes().strokes().front().id != saved.front().id);
    REQUIRE(changes == 5);

    canvas.strokes().clear();
    canvas.strokes().clear(); // already empty: no change
    REQUIRE(canvas.strokes().empty());
    REQUIRE(changes == 6);
}

TEST_CASE("A stroke's bounding rect includes half the nib", "[ink-canvas]")
{
    InkStroke s       = line_stroke({10, 10}, {30, 20});
    s.attributes.size = {4.0f, 6.0f};
    REQUIRE(s.bounding_rect() == Rect{8.0f, 7.0f, 24.0f, 16.0f});
    REQUIRE(InkStroke{}.bounding_rect() == Rect{});

    InkCanvasControl canvas;
    canvas.strokes().add_stroke(s);
    canvas.strokes().add_stroke(line_stroke({50, 50}, {60, 60}));
    REQUIRE(canvas.strokes().bounding_rect() == Rect{8.0f, 7.0f, 53.0f, 54.0f});
}

TEST_CASE("A left-button drag draws a stroke in canvas coordinates", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);
    canvas.set_drawing_attributes({.color = Red, .size = {4.0f, 4.0f}});

    std::vector<InkStroke> collected;
    canvas.on_strokes_collected([&](std::span<const InkStroke> s) { collected.assign(s.begin(), s.end()); });

    canvas.handle_pointer(ink_down({20, 30}));
    REQUIRE(canvas.wet_stroke() != nullptr);
    canvas.handle_pointer(ink_move({40, 50}));
    canvas.handle_pointer(ink_move({40.2f, 50.1f})); // too close to the last point: not added
    canvas.handle_pointer(ink_move({60, 40}));
    REQUIRE(canvas.wet_stroke()->points.size() == 3);
    REQUIRE(canvas.strokes().empty()); // not committed until the release
    canvas.handle_pointer(ink_up({80, 30}));

    REQUIRE(canvas.wet_stroke() == nullptr);
    REQUIRE(canvas.strokes().size() == 1);
    const InkStroke& s = canvas.strokes().strokes().front();
    REQUIRE(s.points.size() == 4);
    REQUIRE(s.points.front().position == Point{10, 10});
    REQUIRE(s.points.back().position == Point{70, 10});
    REQUIRE(s.points.front().pressure == 0.5f);
    REQUIRE(s.attributes.color == Red);
    REQUIRE(s.id != 0);
    REQUIRE(collected.size() == 1);
    REQUIRE(collected.front() == s);

    // New attributes apply from the next stroke on.
    canvas.set_drawing_attributes({.color = Blue});
    REQUIRE(canvas.strokes().strokes().front().attributes.color == Red);
}

TEST_CASE("The canvas ignores the right button, presses outside, None mode and a disabled canvas", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);

    canvas.handle_pointer(ink_down({50, 50}, PointerButton::Right));
    canvas.handle_pointer(ink_move({60, 60}));
    canvas.handle_pointer(ink_up({60, 60}, PointerButton::Right));
    draw(canvas, {{5, 5}, {50, 50}}); // pressed outside the bounds
    REQUIRE(canvas.strokes().empty());

    canvas.set_input_mode(InkCanvasInputMode::None);
    draw(canvas, {{50, 50}, {60, 60}});
    REQUIRE(canvas.strokes().empty());

    canvas.set_input_mode(InkCanvasInputMode::Inking);
    canvas.set_enabled(false);
    draw(canvas, {{50, 50}, {60, 60}});
    REQUIRE(canvas.strokes().empty());

    canvas.set_enabled(true);
    draw(canvas, {{50, 50}, {60, 60}});
    REQUIRE(canvas.strokes().size() == 1);
}

TEST_CASE("A stroke in progress is kept when input is switched off or the pointer leaves the window", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);
    canvas.handle_pointer(ink_down({50, 50}));
    canvas.handle_pointer(ink_move({60, 60}));
    canvas.set_enabled(false);
    REQUIRE(canvas.strokes().size() == 1);

    canvas.set_enabled(true);
    canvas.handle_pointer(ink_down({50, 50}));
    canvas.handle_pointer(ink_move({70, 60}));
    canvas.handle_pointer(PointerEvent{{300, 300}, PointerButton::None, PointerAction::Leave});
    REQUIRE(canvas.strokes().size() == 2);
    REQUIRE(canvas.wet_stroke() == nullptr);
}

TEST_CASE("An empty canvas draws nothing; ink is clipped and offset to the canvas", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);
    TestRenderer r;
    canvas.render(r);
    REQUIRE(r.count() == 0);

    canvas.strokes().add_stroke(line_stroke({0, 0}, {50, 50}));
    canvas.render(r);
    REQUIRE(r.count() == 5);
    REQUIRE(std::get<PushClip>(r.commands[0]).bounds == Rect{10, 20, 200, 100});
    REQUIRE(std::get<PushTransform>(r.commands[1]).matrix() == Matrix::translation(10, 20));
    REQUIRE(std::holds_alternative<DrawGeometry>(r.commands[2]));
    REQUIRE(std::holds_alternative<PopTransform>(r.commands[3]));
    REQUIRE(std::holds_alternative<PopClip>(r.commands[4]));
}

TEST_CASE("An even round stroke is a stroked path; smoothed into cubics only from three points", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);
    canvas.strokes().add_stroke(line_stroke({0, 0}, {50, 50}));
    canvas.strokes().add_stroke(InkStroke{.points = {{.position = {0, 0}}, {.position = {20, 30}}, {.position = {40, 0}}}});
    canvas.strokes().add_stroke(InkStroke{.points = {{.position = {0, 0}}, {.position = {20, 30}}, {.position = {40, 0}}},
                                          .attributes = {.fit_to_curve = false}});

    TestRenderer r;
    canvas.render(r);
    const auto g = geometries(r);
    REQUIRE(g.size() == 3);

    REQUIRE_FALSE(g[0].fill.has_value());
    REQUIRE(g[0].stroke == Brush{colors::Black});
    REQUIRE(g[0].stroke_style.thickness == 2.0f);
    REQUIRE(g[0].stroke_style.start_cap == PenLineCap::Round);
    REQUIRE(g[0].stroke_style.end_cap == PenLineCap::Round);
    REQUIRE(g[0].stroke_style.line_join == PenLineJoin::Round);
    REQUIRE(g[0].geometry.figures.size() == 1);
    REQUIRE(g[0].geometry.figures[0].segments.size() == 1);
    REQUIRE(g[0].geometry.figures[0].segments[0].kind == PathSegmentKind::Line);

    REQUIRE(g[1].geometry.figures[0].segments.size() == 2);
    REQUIRE(g[1].geometry.figures[0].segments[0].kind == PathSegmentKind::Cubic);
    REQUIRE(g[1].geometry.figures[0].segments[1].p3 == Point{40, 0});

    REQUIRE(g[2].geometry.figures[0].segments.size() == 2);
    REQUIRE(g[2].geometry.figures[0].segments[0].kind == PathSegmentKind::Line);
}

TEST_CASE("A tap leaves a dot the size of the nib", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);
    canvas.set_drawing_attributes({.size = {6.0f, 6.0f}});
    canvas.handle_pointer(ink_down({50, 50}));
    canvas.handle_pointer(ink_up({50, 50}));
    REQUIRE(canvas.strokes().size() == 1);
    REQUIRE(canvas.strokes().strokes().front().points.size() == 1);

    TestRenderer r;
    canvas.render(r);
    const auto g = geometries(r);
    REQUIRE(g.size() == 1);
    REQUIRE(g[0].fill == Brush{colors::Black});
    REQUIRE(g[0].geometry.bounds().width == 6.0f);
}

TEST_CASE("Highlighters are drawn first and at half opacity, as a swept chisel", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);
    canvas.strokes().add_stroke(line_stroke({0, 0}, {50, 0}));
    InkStroke marker = line_stroke({0, 10}, {50, 10});
    marker.attributes = {.color = Yellow, .size = {4.0f, 12.0f}, .pen_tip = InkPenTip::Rectangle, .draw_as_highlighter = true};
    canvas.strokes().add_stroke(marker);

    TestRenderer r;
    canvas.render(r);
    const auto g = geometries(r);
    REQUIRE(g.size() == 2);
    REQUIRE(g[0].fill == Brush{Yellow.with_alpha(0.5f)});
    REQUIRE_FALSE(g[0].stroke.has_value());
    REQUIRE(g[0].geometry.fill_rule == FillRule::Nonzero);
    // The 4x12 nib swept 50 DIPs right covers 54 by 12.
    const Rect b = g[0].geometry.bounds();
    REQUIRE_THAT(b.width, WithinAbs(54.0, 0.01));
    REQUIRE_THAT(b.height, WithinAbs(12.0, 0.01));
    REQUIRE(g[1].stroke == Brush{colors::Black});
}

TEST_CASE("Pressure widens the swept nib unless the stroke ignores it", "[ink-canvas]")
{
    InkStroke pressed{.points     = {{.position = {0, 0}, .pressure = 0.0f}, {.position = {40, 0}, .pressure = 1.0f}},
                      .attributes = {.size = {4.0f, 4.0f}}};
    InkCanvasControl canvas;
    place(canvas);
    canvas.strokes().add_stroke(pressed);
    pressed.attributes.ignore_pressure = true;
    canvas.strokes().add_stroke(pressed);

    TestRenderer r;
    canvas.render(r);
    const auto g = geometries(r);
    REQUIRE(g.size() == 2);
    REQUIRE(g[0].fill.has_value());
    // Full pressure is 1.75 times the 4 DIP nib at the far end.
    REQUIRE_THAT(g[0].geometry.bounds().height, WithinAbs(7.0, 0.05));
    REQUIRE(g[1].stroke.has_value());
    REQUIRE(g[1].stroke_style.thickness == 4.0f);
}

TEST_CASE("Pencil ink is drawn lighter under a grain, at its opacity", "[ink-canvas]")
{
    InkCanvasControl     canvas;
    InkDrawingAttributes pencil = InkDrawingAttributes::for_pencil();
    pencil.pencil_opacity       = 0.25f;
    pencil.size                 = {6.0f, 6.0f};
    canvas.strokes().add_stroke(InkStroke{.points = {{.position = {0, 0}}, {.position = {40, 30}}}, .attributes = pencil});
    place(canvas);
    TestRenderer r;
    canvas.render(r);
    const std::vector<DrawGeometry> drawn = geometries(r);
    REQUIRE(drawn.size() == 2);
    REQUIRE(drawn[0].stroke == Brush{colors::Black.with_alpha(0.25f * 0.55f)});
    REQUIRE(drawn[1].fill == Brush{colors::Black.with_alpha(0.25f)});
    REQUIRE(drawn[1].geometry.figures.size() > 20);

    // The grain is the same every time the stroke is drawn.
    canvas.strokes().replace({canvas.strokes().strokes()[0]});
    TestRenderer again;
    canvas.render(again);
    REQUIRE(geometries(again).at(1) == drawn[1]);
    // Every speck lies within the nib of the line from (0, 0) to (40, 30).
    for (const PathFigure& f : drawn[1].geometry.figures)
    {
        const float distance = std::abs(f.start.x * 0.6f - f.start.y * 0.8f); // to the line 3x - 4y = 0
        REQUIRE(distance < 3.0f * 1.0f + 2.0f);
    }

    // Pen ink has no grain.
    canvas.strokes().replace({InkStroke{.points = {{.position = {0, 0}}, {.position = {40, 30}}}}});
    TestRenderer pen;
    canvas.render(pen);
    REQUIRE(geometries(pen).size() == 1);
}

TEST_CASE("The stroke being drawn is drawn on top as it grows", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);
    canvas.handle_pointer(ink_down({20, 30}));
    canvas.handle_pointer(ink_move({60, 30}));
    TestRenderer r;
    canvas.render(r);
    REQUIRE(geometries(r).size() == 1);
}

TEST_CASE("The stroke eraser removes every whole stroke the drag touches", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);
    canvas.strokes().add_stroke(line_stroke({10, 10}, {100, 10}));
    const auto keep = canvas.strokes().add_stroke(line_stroke({10, 80}, {100, 80}));
    canvas.strokes().add_stroke(line_stroke({150, 10}, {150, 60}));

    std::vector<InkStroke> erased;
    canvas.on_strokes_erased([&](std::span<const InkStroke> s) { erased.insert(erased.end(), s.begin(), s.end()); });
    canvas.set_input_mode(InkCanvasInputMode::Erasing);

    // A vertical sweep at x = 50 crosses the first line only; then right across to the third.
    draw(canvas, {{60, 20}, {60, 50}, {170, 50}});
    REQUIRE(canvas.strokes().size() == 1);
    REQUIRE(canvas.strokes().strokes().front().id == keep);
    REQUIRE(erased.size() == 2);
    REQUIRE(canvas.wet_stroke() == nullptr);

    // A press right on a stroke takes it too.
    draw(canvas, {{60, 100}});
    REQUIRE(canvas.strokes().empty());
}

TEST_CASE("The point eraser takes only the ink under it and splits the stroke it crosses", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);
    const auto line = canvas.strokes().add_stroke(line_stroke({10, 40}, {100, 40}));
    const auto keep = canvas.strokes().add_stroke(line_stroke({10, 90}, {100, 90}));

    std::vector<InkStroke> erased;
    canvas.on_strokes_erased([&](std::span<const InkStroke> s) { erased.insert(erased.end(), s.begin(), s.end()); });
    canvas.set_input_mode(InkCanvasInputMode::Erasing);
    canvas.set_erase_mode(InkCanvasEraseMode::Point);
    REQUIRE(canvas.eraser_size() == 8.0f);

    draw(canvas, {{65, 40}, {65, 80}}); // straight down through the first line, at canvas x = 55
    REQUIRE(erased.size() == 1);
    CHECK(erased.front().id == line);

    const auto strokes = canvas.strokes().strokes();
    REQUIRE(strokes.size() == 3);
    // The two halves take the cut stroke's place, ahead of the untouched one.
    CHECK(strokes[0].points.front().position.x == 10.0f);
    CHECK(strokes[0].points.back().position.x < 51.0f);
    CHECK(strokes[1].points.front().position.x > 59.0f);
    CHECK(strokes[1].points.back().position.x == 100.0f);
    CHECK(strokes[2].id == keep);
    CHECK(strokes[0].id != line);
    CHECK(strokes[1].id != line);
}

TEST_CASE("The container selects strokes inside an outline or across a line, and moves or deletes them",
          "[ink-canvas][selection]")
{
    InkCanvasControl canvas;
    int              changes = 0;
    canvas.on_strokes_changed([&] { ++changes; });
    InkStrokeContainer& strokes = canvas.strokes();
    const std::uint32_t a       = strokes.add_stroke(line_stroke({10, 10}, {30, 10}));
    const std::uint32_t b       = strokes.add_stroke(line_stroke({100, 10}, {120, 10}));
    changes                     = 0;

    // A square around the first stroke only.
    const std::vector<Point> lasso{{0, 0}, {50, 0}, {50, 30}, {0, 30}};
    const Rect               selected = strokes.select_with_polyline(lasso);
    REQUIRE(strokes.stroke_by_id(a)->selected);
    REQUIRE_FALSE(strokes.stroke_by_id(b)->selected);
    REQUIRE(selected == strokes.stroke_by_id(a)->bounding_rect());
    REQUIRE(strokes.has_selection());
    REQUIRE(changes == 1);

    // A stroke reaching out of the outline is not taken.
    strokes.add_stroke(line_stroke({40, 20}, {80, 20}));
    (void)strokes.select_with_polyline(lasso);
    REQUIRE(strokes.strokes()[2].selected == false);

    const Rect moved = strokes.move_selected({5, 40});
    REQUIRE(strokes.stroke_by_id(a)->points.front().position == Point{15, 50});
    REQUIRE(moved == strokes.selection_bounding_rect());
    REQUIRE(strokes.stroke_by_id(b)->points.front().position == Point{100, 10});

    // A line selects what it crosses, and deselects the rest.
    (void)strokes.select_with_line({110, 0}, {110, 30});
    REQUIRE_FALSE(strokes.stroke_by_id(a)->selected);
    REQUIRE(strokes.stroke_by_id(b)->selected);

    REQUIRE_FALSE(strokes.delete_selected().empty());
    REQUIRE(strokes.stroke_by_id(b) == nullptr);
    REQUIRE(strokes.size() == 2);
    REQUIRE(strokes.delete_selected().empty()); // nothing left selected

    REQUIRE(strokes.set_selected(a, true));
    REQUIRE_FALSE(strokes.set_selected(9999, true));
    strokes.clear_selection();
    REQUIRE_FALSE(strokes.has_selection());
}

TEST_CASE("A selected stroke is drawn hollow, in its color with a white core", "[ink-canvas][selection]")
{
    InkCanvasControl canvas;
    place(canvas);
    InkStroke stroke       = line_stroke({10, 10}, {60, 10});
    stroke.attributes.size = {6.0f, 6.0f};
    const std::uint32_t id = canvas.strokes().add_stroke(stroke);

    TestRenderer plain;
    canvas.render(plain);
    REQUIRE(geometries(plain).size() == 1);

    (void)canvas.strokes().set_selected(id, true);
    TestRenderer selected;
    canvas.render(selected);
    const std::vector<DrawGeometry> drawn = geometries(selected);
    REQUIRE(drawn.size() == 2);
    REQUIRE(drawn[0].stroke == std::optional<Brush>{Brush{colors::Black}});
    REQUIRE(drawn[1].stroke == std::optional<Brush>{Brush{colors::White}});
    REQUIRE(drawn[1].stroke_style.thickness == 6.0f);
    REQUIRE(drawn[0].stroke_style.thickness == 8.0f);

    // A hairline grows its rim instead of losing its core.
    canvas.strokes().replace({line_stroke({10, 10}, {60, 10})});
    (void)canvas.strokes().set_selected(canvas.strokes().strokes()[0].id, true);
    TestRenderer thin;
    canvas.render(thin);
    const std::vector<DrawGeometry> hairline = geometries(thin);
    REQUIRE(hairline.size() == 2);
    REQUIRE(hairline[0].stroke_style.thickness == 4.0f);
    REQUIRE(hairline[1].stroke == std::optional<Brush>{Brush{colors::White}});
    REQUIRE(hairline[1].stroke_style.thickness == 2.0f);

    // A filled shape (a flat nib) is filled white inside an outline in its color.
    InkStroke flat       = line_stroke({10, 10}, {60, 30});
    flat.attributes.size = {2.0f, 8.0f};
    canvas.strokes().replace({flat});
    (void)canvas.strokes().set_selected(canvas.strokes().strokes()[0].id, true);
    TestRenderer filled;
    canvas.render(filled);
    const std::vector<DrawGeometry> shape = geometries(filled);
    REQUIRE(shape.size() == 2);
    REQUIRE(shape[0].stroke == shape[0].fill);
    REQUIRE(shape[1].fill == std::optional<Brush>{Brush{colors::White}});
    REQUIRE_FALSE(shape[1].stroke);
}

TEST_CASE("The lasso outline is drawn dashed over the ink until it is cleared", "[ink-canvas][selection]")
{
    InkCanvasControl canvas;
    place(canvas);
    TestRenderer none;
    canvas.render(none);
    REQUIRE(none.commands.empty());

    canvas.set_lasso_outline({{10, 10}, {50, 10}, {50, 40}});
    REQUIRE(canvas.lasso_outline().size() == 3);
    TestRenderer drawn;
    canvas.render(drawn);
    const std::vector<DrawGeometry> lines = geometries(drawn);
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[1].geometry.figures.at(0).segments.size() == 2);
    REQUIRE_FALSE(lines[1].geometry.figures[0].closed);
    REQUIRE_FALSE(lines[1].stroke_style.dash_array.empty());

    canvas.set_lasso_outline({});
    TestRenderer cleared;
    canvas.render(cleared);
    REQUIRE(cleared.commands.empty());
}

TEST_CASE("Input the canvas does not ink is reported in canvas coordinates", "[ink-canvas][selection]")
{
    InkCanvasControl          canvas;
    std::vector<PointerEvent> reported;
    canvas.on_unprocessed_input([&](const PointerEvent& e) { reported.push_back(e); });
    place(canvas);

    // A right-button drag (a pen's barrel button) draws nothing and is reported, press to release.
    canvas.handle_pointer(ink_down({50, 50}, PointerButton::Right));
    canvas.handle_pointer(ink_move({300, 60})); // outside the canvas, still the drag's
    canvas.handle_pointer(ink_up({300, 60}, PointerButton::Right));
    REQUIRE(canvas.strokes().empty());
    REQUIRE(reported.size() == 3);
    REQUIRE(reported[0].position == Point{40, 30});
    REQUIRE(reported[0].action == PointerAction::Down);
    REQUIRE(reported[2].action == PointerAction::Up);

    // Hover over an inking canvas is not reported.
    reported.clear();
    canvas.handle_pointer(ink_move({50, 50}));
    REQUIRE(reported.empty());

    // AllowProcessing inks with the right button too.
    canvas.set_right_drag_action(InkCanvasRightDragAction::AllowProcessing);
    canvas.handle_pointer(ink_down({50, 50}, PointerButton::Right));
    canvas.handle_pointer(ink_move({70, 60}));
    canvas.handle_pointer(ink_up({70, 60}, PointerButton::Right));
    REQUIRE(canvas.strokes().size() == 1);
    REQUIRE(reported.empty());

    // In None mode every press, move and release over the canvas is reported, hover included:
    // a lasso built from them selects what it surrounds.
    canvas.set_input_mode(InkCanvasInputMode::None);
    std::vector<Point> lasso;
    canvas.on_unprocessed_input(
        [&](const PointerEvent& e)
        {
            reported.push_back(e);
            if (e.action != PointerAction::Down && e.action != PointerAction::Move)
            {
                return;
            }
            if (e.action == PointerAction::Down)
            {
                lasso.clear();
            }
            lasso.push_back(e.position);
        });
    canvas.handle_pointer(ink_move({20, 30}));
    REQUIRE(reported.size() == 1);
    for (const Point p : {Point{20, 30}, Point{100, 30}, Point{100, 100}, Point{20, 100}})
    {
        canvas.handle_pointer(p == Point{20, 30} ? ink_down(p) : ink_move(p));
    }
    canvas.handle_pointer(ink_up({20, 100}));
    REQUIRE(canvas.strokes().size() == 1);
    REQUIRE(lasso.size() == 4);
    REQUIRE_FALSE(canvas.strokes().select_with_polyline(lasso).empty());
    REQUIRE(canvas.strokes().strokes()[0].selected);
}

TEST_CASE("A stroke started on the ruler's edge is drawn straight along it", "[ink-canvas][stencil]")
{
    InkCanvasControl canvas;
    place(canvas); // at (10, 20), 200 by 100
    canvas.set_ruler({.visible = true, .center = Point{100, 50}, .length = 160.0f, .width = 40.0f});
    canvas.set_drawing_attributes({.size = {4.0f, 4.0f}});

    // Pressed just above the top edge (y 30 in the canvas), wandering off it: every point lies on
    // y 30 - 2 (half the nib), and no further out than the ruler's ends.
    draw(canvas, {{60, 48}, {120, 35}, {300, 60}});
    REQUIRE(canvas.strokes().size() == 1);
    for (const InkPoint& p : canvas.strokes().strokes()[0].points)
    {
        REQUIRE_THAT(p.position.y, WithinAbs(28.0, 1e-3));
        REQUIRE(p.position.x <= 180.0f + 1e-3f);
    }

    // Away from the ruler, ink goes where the pointer does.
    draw(canvas, {{20, 110}, {40, 105}});
    REQUIRE_THAT(canvas.strokes().strokes()[1].points.front().position.y, WithinAbs(90.0, 1e-3));
}

TEST_CASE("The ruler drags from its body and turns with the wheel", "[ink-canvas][stencil]")
{
    InkCanvasControl canvas;
    int              changes = 0;
    canvas.on_stencils_changed([&] { ++changes; });
    place(canvas);
    canvas.set_ruler({.visible = true, .center = Point{100, 50}, .length = 160.0f, .width = 40.0f});

    draw(canvas, {{110, 70}, {130, 80}}); // the body's middle, moved 20 right and 10 down
    REQUIRE(canvas.strokes().empty());
    REQUIRE(canvas.ruler().center == std::optional<Point>{Point{120, 60}});
    REQUIRE(changes == 1);

    PointerEvent wheel{{130, 80}, PointerButton::None, PointerAction::Wheel, 80.0f};
    REQUIRE(canvas.handle_wheel(wheel));
    REQUIRE_THAT(canvas.ruler().angle, WithinAbs(10.0, 1e-3));
    PointerEvent elsewhere{{15, 25}, PointerButton::None, PointerAction::Wheel, 80.0f};
    REQUIRE_FALSE(canvas.handle_wheel(elsewhere)); // off the ruler, left to what scrolls the canvas

    // Turned 90 degrees, its long edges run up and down.
    canvas.set_ruler({.visible = true, .center = Point{100, 50}, .angle = 90.0f, .length = 80.0f, .width = 40.0f});
    draw(canvas, {{140, 40}, {150, 90}});
    REQUIRE(canvas.strokes().size() == 1);
    REQUIRE_THAT(canvas.strokes().strokes()[0].points.back().position.x, WithinAbs(121.0, 1e-3));
}

TEST_CASE("A stroke started on the protractor's edge is drawn round it", "[ink-canvas][stencil]")
{
    InkCanvasControl canvas;
    place(canvas);
    canvas.set_protractor({.visible = true, .center = Point{100, 50}, .radius = 40.0f});
    canvas.set_drawing_attributes({.size = {2.0f, 2.0f}});

    draw(canvas, {{150, 70}, {110, 110}, {60, 70}}); // pressed just right of it
    REQUIRE(canvas.strokes().size() == 1);
    for (const InkPoint& p : canvas.strokes().strokes()[0].points)
    {
        REQUIRE_THAT(std::hypot(p.position.x - 100.0f, p.position.y - 50.0f), WithinAbs(41.0, 1e-3));
    }

    // Its body drags it.
    draw(canvas, {{110, 70}, {120, 70}});
    REQUIRE(canvas.protractor().center == std::optional<Point>{Point{110, 50}});
}

TEST_CASE("Stencils draw over the ink and only while they show", "[ink-canvas][stencil]")
{
    InkCanvasControl canvas;
    place(canvas);
    TestRenderer none;
    canvas.render(none);
    REQUIRE(none.commands.empty());

    canvas.set_ruler({.visible = true});
    canvas.set_protractor({.visible = true});
    TestRenderer both;
    canvas.render(both);
    std::size_t texts = 0;
    for (const auto& cmd : both.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&cmd))
        {
            texts += t->text == "0\u00B0" ? 1 : 0;
        }
    }
    REQUIRE(texts == 2); // each reads out its angle

    // Centered on the canvas by default.
    REQUIRE(canvas.ruler().center == std::nullopt);
}

TEST_CASE("ui::InkCanvas builds the control with its strokes, attributes and handlers", "[ink-canvas][spec]")
{
    Control<InkCanvas>           handle;
    Property<bool>               on{true};
    Property<InkCanvasInputMode> mode{InkCanvasInputMode::Inking};
    int                          changes = 0;

    auto node =
        InkCanvas{
            .drawing_attributes = InkDrawingAttributes{.color = Blue},
            .input_mode         = bind(mode),
            .strokes            = {line_stroke({0, 0}, {5, 5})},
            .enabled            = bind(on),
            .accessibility      = {.name = "Drawing canvas"},
            .on_strokes_changed = [&] { ++changes; },
            .control            = handle,
        }
            .build();

    REQUIRE(handle);
    REQUIRE(handle.get() == node.get());
    REQUIRE(handle->strokes().size() == 1);
    REQUIRE(changes == 0); // the initial strokes do not raise the handler
    REQUIRE(handle->drawing_attributes().color == Blue);
    REQUIRE(handle->automation_name() == "Drawing canvas");

    on = false;
    REQUIRE_FALSE(handle->enabled());
    mode = InkCanvasInputMode::Erasing;
    REQUIRE(handle->input_mode() == InkCanvasInputMode::Erasing);

    handle->strokes().clear();
    REQUIRE(changes == 1);
}

TEST_CASE("Pointer device sets combine and test membership", "[ink-canvas][input]")
{
    constexpr auto pen_mouse = PointerDeviceTypes::Pen | PointerDeviceTypes::Mouse;
    STATIC_REQUIRE(has_device(pen_mouse, PointerDeviceType::Pen));
    STATIC_REQUIRE(has_device(pen_mouse, PointerDeviceType::Mouse));
    STATIC_REQUIRE_FALSE(has_device(pen_mouse, PointerDeviceType::Touch));
    STATIC_REQUIRE(has_device(PointerDeviceTypes::All, PointerDeviceType::Touch));
    STATIC_REQUIRE_FALSE(has_device(PointerDeviceTypes::None, PointerDeviceType::Mouse));

    // An event nobody filled in is the mouse's: no pressure reading, no tilt, no pen buttons.
    const PointerEvent plain{{1, 2}, PointerButton::Left, PointerAction::Down};
    REQUIRE(plain.device == PointerDeviceType::Mouse);
    REQUIRE(plain.pressure == 0.5f);
    REQUIRE(plain.tilt_x == 0.0f);
    REQUIRE(plain.tilt_y == 0.0f);
    REQUIRE_FALSE(plain.eraser);
    REQUIRE_FALSE(plain.barrel_button);
}

TEST_CASE("Pen ink keeps the pen's pressure per point; the release keeps the last move's", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);

    canvas.handle_pointer(pen(ink_down({20, 30}), 0.2f));
    canvas.handle_pointer(pen(ink_move({40, 30}), 0.9f));
    canvas.handle_pointer(pen(ink_move({60, 30}), 1.5f)); // out of range: clamped
    canvas.handle_pointer(pen(ink_up({80, 30}), 0.0f));   // lifting off
    REQUIRE(canvas.strokes().size() == 1);
    const std::span<const InkStroke> all    = canvas.strokes().strokes();
    const auto&                      points = all.front().points;
    REQUIRE(points.size() == 4);
    REQUIRE(points[0].pressure == 0.2f);
    REQUIRE(points[1].pressure == 0.9f);
    REQUIRE(points[2].pressure == 1.0f);
    REQUIRE(points[3].pressure == 1.0f);

    // A harder stroke is drawn wider than the mouse's even one.
    REQUIRE(canvas.strokes().strokes().front().bounding_rect().height >
            InkStroke{.points = {{.position = {0, 0}}}}.bounding_rect().height);
}

TEST_CASE("Only the input device types draw: the pen and the mouse by default", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);
    REQUIRE(canvas.input_device_types() == (PointerDeviceTypes::Pen | PointerDeviceTypes::Mouse));

    // Touch is left alone by default.
    canvas.handle_pointer(touch(ink_down({50, 50})));
    REQUIRE(canvas.wet_stroke() == nullptr);
    canvas.handle_pointer(touch(ink_move({60, 60})));
    canvas.handle_pointer(touch(ink_up({60, 60})));
    REQUIRE(canvas.strokes().empty());

    draw(canvas, {{50, 50}, {60, 60}}); // the mouse
    canvas.handle_pointer(pen(ink_down({50, 70}), 0.5f));
    canvas.handle_pointer(pen(ink_up({70, 70}), 0.5f));
    REQUIRE(canvas.strokes().size() == 2);

    canvas.set_input_device_types(PointerDeviceTypes::Pen);
    draw(canvas, {{50, 50}, {60, 60}});
    REQUIRE(canvas.strokes().size() == 2);

    canvas.set_input_device_types(PointerDeviceTypes::All);
    canvas.handle_pointer(touch(ink_down({50, 50})));
    canvas.handle_pointer(touch(ink_move({60, 60})));
    canvas.handle_pointer(touch(ink_up({60, 60})));
    REQUIRE(canvas.strokes().size() == 3);

    // Erasing obeys the same filter.
    canvas.set_input_mode(InkCanvasInputMode::Erasing);
    canvas.set_input_device_types(PointerDeviceTypes::Pen);
    draw(canvas, {{30, 30}, {80, 80}});
    REQUIRE(canvas.strokes().size() == 3);
}

TEST_CASE("A pen's eraser erases whole strokes in Inking mode unless eraser input is off", "[ink-canvas]")
{
    InkCanvasControl canvas;
    place(canvas);
    canvas.strokes().add_stroke(line_stroke({10, 10}, {100, 10}));
    canvas.strokes().add_stroke(line_stroke({10, 80}, {100, 80}));
    REQUIRE(canvas.eraser_input_enabled());

    std::size_t erased = 0;
    canvas.on_strokes_erased([&](std::span<const InkStroke> s) { erased += s.size(); });

    canvas.handle_pointer(pen(ink_down({60, 20}), 0.5f, true));
    REQUIRE(canvas.wet_stroke() == nullptr); // erasing, not inking
    canvas.handle_pointer(pen(ink_move({60, 50}), 0.5f, true));
    canvas.handle_pointer(pen(ink_up({60, 50}), 0.5f, true));
    REQUIRE(erased == 1);
    REQUIRE(canvas.strokes().size() == 1);
    REQUIRE(canvas.input_mode() == InkCanvasInputMode::Inking); // the mode is untouched

    canvas.set_eraser_input_enabled(false);
    canvas.handle_pointer(pen(ink_down({60, 90}), 0.5f, true));
    canvas.handle_pointer(pen(ink_move({60, 110}), 0.5f, true));
    canvas.handle_pointer(pen(ink_up({60, 110}), 0.5f, true));
    REQUIRE(erased == 1);
    REQUIRE(canvas.strokes().size() == 1); // and it drew nothing either

    canvas.set_eraser_input_enabled(true);
    canvas.set_input_mode(InkCanvasInputMode::None);
    canvas.handle_pointer(pen(ink_down({60, 100}), 0.5f, true));
    canvas.handle_pointer(pen(ink_up({60, 100}), 0.5f, true));
    REQUIRE(erased == 1);
}

TEST_CASE("Pen fields reach the canvas through the window's pointer seam", "[ink-canvas][headless]")
{
    HeadlessTextMeasurer m;
    Window               window;
    InkCanvasControl*    canvas = window.set_content(std::make_unique<InkCanvasControl>());
    window.layout(Size{300.0f, 200.0f}, m);

    PointerEvent down = pen(ink_down({50, 50}), 0.8f);
    down.tilt_x       = 30.0f;
    seams(window).handle_pointer(down);
    seams(window).handle_pointer(pen(ink_move({80, 60}), 0.3f));
    seams(window).handle_pointer(pen(ink_up({80, 60}), 0.0f));
    REQUIRE(canvas->strokes().size() == 1);
    const std::span<const InkStroke> all    = canvas->strokes().strokes();
    const auto&                      points = all.front().points;
    REQUIRE(points.size() == 2);
    REQUIRE(points[0].pressure == 0.8f);
    REQUIRE(points[1].pressure == 0.3f);

    seams(window).handle_pointer(touch(ink_down({50, 50})));
    seams(window).handle_pointer(touch(ink_up({90, 90})));
    REQUIRE(canvas->strokes().size() == 1);
}

TEST_CASE("ui::InkCanvas binds the input device types and eraser input", "[ink-canvas][spec]")
{
    Control<InkCanvas>           handle;
    Property<PointerDeviceTypes> devices{PointerDeviceTypes::Touch};
    Property<bool>               eraser{false};

    auto node = InkCanvas{.input_device_types = bind(devices), .eraser_input_enabled = bind(eraser), .control = handle}.build();
    REQUIRE(handle->input_device_types() == PointerDeviceTypes::Touch);
    REQUIRE_FALSE(handle->eraser_input_enabled());

    devices = PointerDeviceTypes::All;
    eraser  = true;
    REQUIRE(handle->input_device_types() == PointerDeviceTypes::All);
    REQUIRE(handle->eraser_input_enabled());

    auto plain = InkCanvas{}.build();
    REQUIRE(static_cast<InkCanvasControl&>(*plain).input_device_types() ==
            (PointerDeviceTypes::Pen | PointerDeviceTypes::Mouse));
}
