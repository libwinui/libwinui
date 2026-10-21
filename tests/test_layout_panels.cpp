// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Describe.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/containers/Border.hpp"
#include "winui/ui/containers/ScaleToFit.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/layouts/Layout.hpp"
#include "winui/ui/layouts/StackLayout.hpp"
#include "winui/ui/layouts/WrapLayout.hpp"
#include "winui/ui/panels/Canvas.hpp"
#include "winui/ui/panels/LayoutPanel.hpp"
#include "winui/ui/panels/Panel.hpp"
#include "winui/ui/panels/RelativePanel.hpp"
#include "winui/ui/panels/TilePanel.hpp"
#include "winui/ui/panels/WrapPanel.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{

constexpr Color Red   = Color::from_rgba(255, 0, 0);
constexpr Color Blue  = Color::from_rgba(0, 0, 255);
constexpr Color Green = Color::from_rgba(0, 128, 0);

// A fixed-size rectangle that remembers the last room it was offered and the presses it saw.
class Block final : public Element
{
public:
    Block(float width, float height, Color color = colors::Black) : size_(width, height), color_(color)
    {
    }

    mutable Size offered{};
    int          presses = 0;

protected:
    Size measure_override(Size available, const MeasureContext&) const override
    {
        offered = available;
        return size_;
    }
    void render_override(Renderer& renderer, const RenderContext&) const override
    {
        renderer.fill_rounded_rect(bounds(), 0.0f, color_);
    }
    bool pointer_override(const PointerEvent& e) override
    {
        if (e.action == PointerAction::Down && bounds().contains(e.position))
        {
            ++presses;
        }
        return false;
    }


private:
    Size  size_;
    Color color_;
};

void layout(Element& e, Size available, Rect slot)
{
    static HeadlessTextMeasurer measurer;
    (void)e.measure(available, measurer, Theme::default_light());
    e.arrange(slot);
}

void check_rect(const Rect& r, float x, float y, float w, float h)
{
    CHECK_THAT(r.x, WithinAbs(x, 1e-3));
    CHECK_THAT(r.y, WithinAbs(y, 1e-3));
    CHECK_THAT(r.width, WithinAbs(w, 1e-3));
    CHECK_THAT(r.height, WithinAbs(h, 1e-3));
}

PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}

} // namespace

// ---- Border ----

TEST_CASE("Border adds its border and padding around the child", "[border][layout]")
{
    HeadlessTextMeasurer measurer;
    BorderControl        border;
    Block*               child = border.set_child(std::make_unique<Block>(50.0f, 20.0f));
    border.set_border_thickness(Thickness{2.0f});
    border.set_padding(Symmetric{.horizontal = 8.0f, .vertical = 5.0f});

    const Size desired = border.measure(Size{200.0f, 100.0f}, measurer, Theme::default_light());
    CHECK_THAT(desired.width, WithinAbs(50.0f + 4.0f + 16.0f, 1e-3));
    CHECK_THAT(desired.height, WithinAbs(20.0f + 4.0f + 10.0f, 1e-3));
    // A Border offers the child the room less both.
    CHECK_THAT(child->offered.width, WithinAbs(200.0f - 20.0f, 1e-3));
    CHECK_THAT(child->offered.height, WithinAbs(100.0f - 14.0f, 1e-3));

    border.arrange(Rect{10.0f, 10.0f, 200.0f, 100.0f});
    check_rect(child->bounds(), 20.0f, 17.0f, 180.0f, 86.0f);
}

TEST_CASE("Border fills inside its border and strokes the border inside its bounds", "[border][render]")
{
    BorderControl border;
    border.set_background(colors::White);
    border.set_border_brush(Color::from_rgba(255, 215, 0));
    border.set_border_thickness(Thickness{2.0f});
    layout(border, Size{100.0f, 40.0f}, Rect{0.0f, 0.0f, 100.0f, 40.0f});

    TestRenderer r;
    border.render(r, Theme::light());
    REQUIRE(r.count() == 2);
    const auto& fill = std::get<FillRoundedRect>(r.commands[0]);
    check_rect(fill.bounds, 2.0f, 2.0f, 96.0f, 36.0f);
    const auto& stroke = std::get<StrokeRoundedRect>(r.commands[1]);
    check_rect(stroke.bounds, 1.0f, 1.0f, 98.0f, 38.0f); // half the stroke in from the edge
    CHECK(stroke.stroke_width == 2.0f);
}

TEST_CASE("A Border with no brushes draws nothing and a non-uniform border draws four bands", "[border][render]")
{
    BorderControl border;
    layout(border, Size{100.0f, 40.0f}, Rect{0.0f, 0.0f, 100.0f, 40.0f});
    TestRenderer none;
    border.render(none, Theme::light());
    CHECK(none.count() == 0);

    border.set_border_brush(colors::Black);
    border.set_border_thickness(Edges{.left = 1.0f, .top = 2.0f, .right = 3.0f, .bottom = 4.0f});
    layout(border, Size{100.0f, 40.0f}, Rect{0.0f, 0.0f, 100.0f, 40.0f});
    TestRenderer bands;
    border.render(bands, Theme::light());
    CHECK(bands.count_of<FillRoundedRect>() == 4);
}

TEST_CASE("A Border with clip_content clips its child to the rounded inside of the border", "[border][render][clip]")
{
    BorderControl border;
    border.set_child(std::make_unique<Block>(100.0f, 40.0f));
    border.set_border_brush(colors::Black);
    border.set_border_thickness(Thickness{2.0f});
    border.set_corner_radius(8.0f);
    layout(border, Size{100.0f, 40.0f}, Rect{0.0f, 0.0f, 100.0f, 40.0f});

    TestRenderer unclipped;
    border.render(unclipped, Theme::light());
    CHECK(unclipped.count_of<PushClipRoundedRect>() == 0); // off by default

    border.set_clip_content(true);
    TestRenderer clipped;
    border.render(clipped, Theme::light());
    REQUIRE(clipped.count_of<PushClipRoundedRect>() == 1);
    const auto push = std::find_if(clipped.commands.begin(),
                                   clipped.commands.end(),
                                   [](const RenderCommand& c) { return std::holds_alternative<PushClipRoundedRect>(c); });
    const auto& clip = std::get<PushClipRoundedRect>(*push);
    check_rect(clip.bounds, 2.0f, 2.0f, 96.0f, 36.0f);
    CHECK(clip.radius == 6.0f); // the radius less the border
    CHECK(std::holds_alternative<PopClip>(clipped.commands.back()));
    CHECK(clipped.count() == unclipped.count() + 2); // the child in between, as before
}

// ---- Canvas ----

TEST_CASE("Canvas asks for nothing and places children at their offsets and desired size", "[canvas][layout]")
{
    HeadlessTextMeasurer measurer;
    CanvasControl        canvas;
    Block*               a = canvas.add(std::make_unique<Block>(40.0f, 40.0f), 20.0f, 30.0f);
    Block*               b = canvas.add(std::make_unique<Block>(40.0f, 40.0f), 200.0f, 0.0f);

    const Size desired = canvas.measure(Size{100.0f, 100.0f}, measurer, Theme::default_light());
    CHECK(desired.width == 0.0f);
    CHECK(desired.height == 0.0f);
    CHECK(a->offered.width == Unbounded);
    CHECK(a->offered.height == Unbounded);

    canvas.arrange(Rect{10.0f, 10.0f, 100.0f, 100.0f});
    check_rect(a->bounds(), 30.0f, 40.0f, 40.0f, 40.0f);
    check_rect(b->bounds(), 210.0f, 10.0f, 40.0f, 40.0f); // past the edge, not clipped or moved

    ui::testing::clear_dirty(canvas);
    canvas.set_left(a, 0.0f);
    canvas.set_top(a, 0.0f);
    CHECK(canvas.is_dirty(DirtyFlags::Layout));
    layout(canvas, Size{100.0f, 100.0f}, Rect{10.0f, 10.0f, 100.0f, 100.0f});
    check_rect(a->bounds(), 10.0f, 10.0f, 40.0f, 40.0f);
}

TEST_CASE("Canvas gives a press to the topmost child under it only", "[canvas][input]")
{
    CanvasControl canvas;
    Block*        below = canvas.add(std::make_unique<Block>(20.0f, 20.0f), 0.0f, 0.0f, 2);
    Block*        above = canvas.add(std::make_unique<Block>(20.0f, 20.0f), 10.0f, 10.0f, 1);
    layout(canvas, Size{Unbounded, Unbounded}, Rect{0.0f, 0.0f, 50.0f, 50.0f});

    canvas.handle_pointer(down(Point{15.0f, 15.0f})); // both: `below` has the higher ZIndex
    CHECK(below->presses == 1);
    CHECK(above->presses == 0);
    canvas.handle_pointer(down(Point{25.0f, 25.0f})); // only `above`
    CHECK(above->presses == 1);

    canvas.set_z_index(above, 3);
    canvas.handle_pointer(down(Point{15.0f, 15.0f}));
    CHECK(below->presses == 1);
    CHECK(above->presses == 2);
}

TEST_CASE("Canvas draws in ZIndex order, ties in the order added", "[canvas][render]")
{
    CanvasControl canvas;
    Block*        red = canvas.add(std::make_unique<Block>(10.0f, 10.0f, Red));
    canvas.add(std::make_unique<Block>(10.0f, 10.0f, Blue), 0.0f, 0.0f, 1);
    canvas.add(std::make_unique<Block>(10.0f, 10.0f, Green), 0.0f, 0.0f, 2);
    layout(canvas, Size{Unbounded, Unbounded}, Rect{0.0f, 0.0f, 50.0f, 50.0f});

    auto draw_order = [&canvas]
    {
        TestRenderer r;
        canvas.render(r, Theme::light());
        std::vector<Color> order;
        order.reserve(r.commands.size());
        for (const auto& c : r.commands)
        {
            order.push_back(std::get<FillRoundedRect>(c).color);
        }
        return order;
    };
    std::vector<Color> order = draw_order();
    REQUIRE(order.size() == 3);
    CHECK(order[0] == Red);
    CHECK(order[2] == Green);

    canvas.set_z_index(red, 4);
    order = draw_order();
    CHECK(order[0] == Blue);
    CHECK(order[2] == Red);
    CHECK(canvas.z_index(red) == 4);
}

// ---- ScaleToFit ----

TEST_CASE("ScaleToFit's scale factor follows Stretch and StretchDirection", "[viewbox]")
{
    ScaleToFitControl box;
    const Size        content{100.0f, 50.0f};

    CHECK(box.compute_scale_factor(Size{200.0f, 200.0f}, content).width == 2.0f); // Uniform: the smaller
    CHECK(box.compute_scale_factor(Size{200.0f, 200.0f}, content).height == 2.0f);

    box.set_stretch(Stretch::UniformToFill);
    CHECK(box.compute_scale_factor(Size{200.0f, 200.0f}, content).width == 4.0f);

    box.set_stretch(Stretch::Fill);
    const Size fill = box.compute_scale_factor(Size{200.0f, 200.0f}, content);
    CHECK(fill.width == 2.0f);
    CHECK(fill.height == 4.0f);

    box.set_stretch(Stretch::None);
    CHECK(box.compute_scale_factor(Size{200.0f, 200.0f}, content).width == 1.0f);

    box.set_stretch(Stretch::Uniform);
    box.set_stretch_direction(StretchDirection::DownOnly);
    CHECK(box.compute_scale_factor(Size{200.0f, 200.0f}, content).width == 1.0f);
    CHECK(box.compute_scale_factor(Size{50.0f, 200.0f}, content).width == 0.5f);
    box.set_stretch_direction(StretchDirection::UpOnly);
    CHECK(box.compute_scale_factor(Size{50.0f, 200.0f}, content).width == 1.0f);

    // An unbounded axis borrows the other's factor; both unbounded never scales.
    box.set_stretch_direction(StretchDirection::Both);
    CHECK(box.compute_scale_factor(Size{Unbounded, 100.0f}, content).width == 2.0f);
    CHECK(box.compute_scale_factor(Size{Unbounded, Unbounded}, content).width == 1.0f);
}

TEST_CASE("ScaleToFit draws its child scaled and centered, and maps pointer input back", "[viewbox][render]")
{
    ScaleToFitControl box;
    Block*            child = box.set_child(std::make_unique<Block>(100.0f, 50.0f));
    box.set_width(200.0f);
    box.set_height(200.0f);
    layout(box, Size{400.0f, 400.0f}, Rect{0.0f, 0.0f, 200.0f, 200.0f});

    CHECK(box.scale_x() == 2.0f);
    CHECK(box.scale_y() == 2.0f);
    check_rect(child->bounds(), 0.0f, 0.0f, 100.0f, 50.0f); // laid out once, at its own size

    TestRenderer r;
    box.render(r, Theme::light());
    const auto& push = std::get<PushTransform>(r.commands[1]);
    CHECK(push.scale == 2.0f);
    CHECK(push.scale_y == 2.0f);
    // A Stretch ScaleToFit centers content narrower than itself: 200x100 in a 200x200 box.
    CHECK_THAT(push.offset.y, WithinAbs(50.0f, 1e-3));

    // (50, 25) in the child is (100, 100) on screen.
    box.handle_pointer(down(Point{100.0f, 100.0f}));
    CHECK(child->presses == 1);
    box.handle_pointer(down(Point{100.0f, 20.0f})); // above the scaled content
    CHECK(child->presses == 1);
}

TEST_CASE("ScaleToFit Fill scales the axes apart and prints both", "[viewbox][render]")
{
    ScaleToFitControl box;
    box.set_child(std::make_unique<Block>(100.0f, 50.0f));
    box.set_stretch(Stretch::Fill);
    layout(box, Size{200.0f, 200.0f}, Rect{0.0f, 0.0f, 200.0f, 200.0f});

    TestRenderer r;
    box.render(r, Theme::light());
    CHECK(describe(r.commands).find("PushTransform scale=2.00x4.00") != std::string::npos);
}

// ---- WrapPanel ----

TEST_CASE("WrapPanel wraps a line that would cross the far edge less the padding", "[wrap-panel][layout]")
{
    HeadlessTextMeasurer measurer;
    WrapPanelControl     panel;
    panel.set_item_spacing(8.0f);
    panel.set_line_spacing(4.0f);
    panel.set_padding(Thickness{12.0f});
    Block* a = panel.add(std::make_unique<Block>(100.0f, 30.0f));
    Block* b = panel.add(std::make_unique<Block>(100.0f, 40.0f));
    Block* c = panel.add(std::make_unique<Block>(100.0f, 30.0f));

    // 12 + 100 + 8 + 100 + 12 = 232 fits in 250; a third would need 340.
    const Size desired = panel.measure(Size{250.0f, Unbounded}, measurer, Theme::default_light());
    // The measured width keeps the spacing after the widest line's last child.
    CHECK_THAT(desired.width, WithinAbs(12.0f + 100.0f + 8.0f + 100.0f + 8.0f + 12.0f, 1e-3));
    CHECK_THAT(desired.height, WithinAbs(12.0f + 40.0f + 4.0f + 30.0f + 12.0f, 1e-3));

    panel.arrange(Rect{0.0f, 0.0f, 250.0f, desired.height});
    check_rect(a->bounds(), 12.0f, 12.0f, 100.0f, 40.0f); // as tall as its line
    check_rect(b->bounds(), 120.0f, 12.0f, 100.0f, 40.0f);
    check_rect(c->bounds(), 12.0f, 56.0f, 100.0f, 30.0f);
    CHECK(a->offered.width == 250.0f - 24.0f);
}

TEST_CASE("WrapPanel items_stretch Last widens the last child to the edge", "[wrap-panel][layout]")
{
    WrapPanelControl panel;
    panel.set_items_stretch(WrapPanelItemsStretch::Last);
    panel.add(std::make_unique<Block>(50.0f, 20.0f));
    auto* last = panel.add(std::make_unique<Block>(50.0f, 20.0f));
    layout(panel, Size{300.0f, Unbounded}, Rect{0.0f, 0.0f, 300.0f, 20.0f});
    CHECK_THAT(last->bounds().width, WithinAbs(250.0f, 1e-3));
}

TEST_CASE("A vertical WrapPanel wraps into columns", "[wrap-panel][layout]")
{
    WrapPanelControl panel;
    panel.set_orientation(Orientation::Vertical);
    auto* a = panel.add(std::make_unique<Block>(30.0f, 60.0f));
    auto* b = panel.add(std::make_unique<Block>(40.0f, 60.0f));
    layout(panel, Size{Unbounded, 100.0f}, Rect{0.0f, 0.0f, 70.0f, 100.0f});
    check_rect(a->bounds(), 0.0f, 0.0f, 30.0f, 60.0f);
    check_rect(b->bounds(), 30.0f, 0.0f, 40.0f, 60.0f);
}

// ---- TilePanel ----

TEST_CASE("TilePanel fills columns first-fit, spans included", "[variable-sized-wrap-grid][layout]")
{
    // The sample: 44x44 cells, three to a column, four spans.
    HeadlessTextMeasurer measurer;
    TilePanelControl     grid;
    grid.set_item_width(44.0f);
    grid.set_item_height(44.0f);
    grid.set_maximum_rows_or_columns(3);
    grid.set_width(400.0f);
    Block* red    = grid.add(std::make_unique<Block>(44.0f, 44.0f));
    Block* blue   = grid.add(std::make_unique<Block>(44.0f, 80.0f), 2, 1);
    Block* green  = grid.add(std::make_unique<Block>(80.0f, 44.0f), 1, 2);
    Block* yellow = grid.add(std::make_unique<Block>(80.0f, 80.0f), 2, 2);

    const Size desired = grid.measure(Size{Unbounded, Unbounded}, measurer, Theme::default_light());
    CHECK(grid.items_per_line() == 3);
    CHECK(grid.line_count() == 3);
    CHECK_THAT(desired.height, WithinAbs(132.0f, 1e-3));

    grid.arrange(Rect{0.0f, 0.0f, 400.0f, 132.0f});
    check_rect(red->bounds(), 0.0f, 0.0f, 44.0f, 44.0f);
    check_rect(blue->bounds(), 0.0f, 44.0f, 44.0f, 88.0f); // stretched to its two cells
    check_rect(green->bounds(), 44.0f, 0.0f, 88.0f, 44.0f);
    check_rect(yellow->bounds(), 44.0f, 44.0f, 88.0f, 88.0f);
    CHECK(yellow->offered.width == 88.0f);
}

TEST_CASE("A horizontal TilePanel fills rows and takes the first child's cell size",
          "[variable-sized-wrap-grid][layout]")
{
    TilePanelControl grid;
    grid.set_orientation(Orientation::Horizontal);
    Block* a = grid.add(std::make_unique<Block>(30.0f, 20.0f));
    Block* b = grid.add(std::make_unique<Block>(30.0f, 20.0f));
    Block* c = grid.add(std::make_unique<Block>(30.0f, 20.0f));
    layout(grid, Size{70.0f, Unbounded}, Rect{0.0f, 0.0f, 70.0f, 40.0f});
    CHECK(grid.cell_size().width == 30.0f);
    CHECK(grid.items_per_line() == 2);
    check_rect(a->bounds(), 0.0f, 0.0f, 30.0f, 20.0f);
    check_rect(b->bounds(), 30.0f, 0.0f, 30.0f, 20.0f);
    check_rect(c->bounds(), 0.0f, 20.0f, 30.0f, 20.0f);
}

TEST_CASE("TilePanel leaves the children past a full map unplaced", "[variable-sized-wrap-grid][layout]")
{
    TilePanelControl grid;
    grid.set_item_width(10.0f);
    grid.set_item_height(10.0f);
    grid.add(std::make_unique<Block>(10.0f, 10.0f));
    Block* extra = grid.add(std::make_unique<Block>(10.0f, 10.0f));
    layout(grid, Size{10.0f, 10.0f}, Rect{0.0f, 0.0f, 10.0f, 10.0f});
    CHECK(extra->bounds().width == 0.0f);
}

// ---- RelativePanel ----

TEST_CASE("RelativePanel resolves the sample layout", "[relative-panel][layout]")
{
    RelativePanelControl panel;
    auto                 r1 = std::make_unique<Block>(50.0f, 50.0f);
    auto                 r2 = std::make_unique<Block>(50.0f, 50.0f);
    auto                 r3 = std::make_unique<Block>(50.0f, 50.0f);
    auto                 r4 = std::make_unique<Block>(50.0f, 50.0f);
    r2->set_margin(Edges{.left = 8.0f});
    r4->set_margin(Edges{.top = 8.0f});
    Block* one   = panel.add(std::move(r1));
    Block* two   = panel.add(std::move(r2), {.right_of = one});
    Block* three = panel.add(std::move(r3), {.align_right_with_panel = true});
    // Declared before nothing it depends on: the graph orders it.
    Block* four = panel.add(std::move(r4), {.below = three, .align_horizontal_center_with = three});
    panel.set_width(300.0f);

    layout(panel, Size{Unbounded, Unbounded}, Rect{0.0f, 0.0f, 300.0f, 108.0f});
    check_rect(one->bounds(), 0.0f, 0.0f, 50.0f, 50.0f);
    check_rect(two->bounds(), 58.0f, 0.0f, 50.0f, 50.0f);
    check_rect(three->bounds(), 250.0f, 0.0f, 50.0f, 50.0f);
    check_rect(four->bounds(), 250.0f, 58.0f, 50.0f, 50.0f);
}

TEST_CASE("RelativePanel's desired size walks the dependency chains", "[relative-panel][layout]")
{
    HeadlessTextMeasurer measurer;
    RelativePanelControl panel;
    Block*               a = panel.add(std::make_unique<Block>(40.0f, 20.0f));
    Block*               b = panel.add(std::make_unique<Block>(60.0f, 30.0f));
    panel.set_constraints(b, {.right_of = a, .below = a});
    const Size desired = panel.measure(Size{Unbounded, Unbounded}, measurer, Theme::default_light());
    CHECK_THAT(desired.width, WithinAbs(100.0f, 1e-3));
    CHECK_THAT(desired.height, WithinAbs(50.0f, 1e-3));
}

TEST_CASE("RelativePanel stretches between two anchored edges and breaks a cycle", "[relative-panel][layout]")
{
    RelativePanelControl panel;
    Block*               left  = panel.add(std::make_unique<Block>(20.0f, 20.0f), {.align_left_with_panel = true});
    Block*               right = panel.add(std::make_unique<Block>(20.0f, 20.0f), {.align_right_with_panel = true});
    Block*               mid   = panel.add(std::make_unique<Block>(10.0f, 20.0f));
    panel.set_constraints(mid, {.left_of = right, .right_of = left});
    layout(panel, Size{200.0f, 50.0f}, Rect{0.0f, 0.0f, 200.0f, 50.0f});
    CHECK_THAT(mid->bounds().x, WithinAbs(20.0f, 1e-3));
    CHECK_THAT(mid->bounds().width, WithinAbs(160.0f, 1e-3));

    // A left_of B left_of A: the constraint that closes the loop is dropped.
    RelativePanelControl cyclic;
    Block*               x = cyclic.add(std::make_unique<Block>(20.0f, 20.0f));
    Block*               y = cyclic.add(std::make_unique<Block>(20.0f, 20.0f));
    cyclic.set_constraints(x, {.left_of = y});
    cyclic.set_constraints(y, {.left_of = x});
    layout(cyclic, Size{200.0f, 50.0f}, Rect{0.0f, 0.0f, 200.0f, 50.0f});
    (void)x;
    CHECK(y->bounds().width == 20.0f);
}

// ---- LayoutPanel ----

namespace
{
// A featured-tile layout: the first child across the top, the rest two to a row.
class FeaturedTiles final : public NonVirtualizingLayout
{
public:
    float spacing = 8.0f;

    Size measure(NonVirtualizingLayoutContext& context, Size available) override
    {
        const float width  = available.width == std::numeric_limits<float>::infinity() ? 520.0f : available.width;
        const float column = std::max(0.0f, (width - spacing) / 2.0f);
        for (std::size_t i = 0; i < context.item_count(); ++i)
        {
            (void)context.measure(i, Size{i == 0 ? width : column, std::numeric_limits<float>::infinity()});
        }
        float height = context.item_count() == 0 ? 0.0f : context.desired_size(0).height;
        for (std::size_t i = 1; i < context.item_count(); i += 2)
        {
            height += spacing + row_height(context, i);
        }
        return Size{width, height};
    }
    Size arrange(NonVirtualizingLayoutContext& context, Size final_size) override
    {
        if (context.item_count() == 0)
        {
            return final_size;
        }
        const float column = std::max(0.0f, (final_size.width - spacing) / 2.0f);
        context.arrange(0, Rect{0.0f, 0.0f, final_size.width, context.desired_size(0).height});
        float y = context.desired_size(0).height;
        for (std::size_t i = 1; i < context.item_count(); i += 2)
        {
            y += spacing;
            const float h = row_height(context, i);
            context.arrange(i, Rect{0.0f, y, column, h});
            if (i + 1 < context.item_count())
            {
                context.arrange(i + 1, Rect{column + spacing, y, column, h});
            }
            y += h;
        }
        return Size{final_size.width, y};
    }
    void change_spacing(float s)
    {
        spacing = s;
        invalidate_measure();
    }

private:
    static float row_height(NonVirtualizingLayoutContext& context, std::size_t first)
    {
        const float h = context.desired_size(first).height;
        return first + 1 < context.item_count() ? std::max(h, context.desired_size(first + 1).height) : h;
    }
};
} // namespace

TEST_CASE("LayoutPanel hands its room less border and padding to its layout", "[layout-panel][layout]")
{
    HeadlessTextMeasurer measurer;
    LayoutPanelControl   panel;
    panel.set_padding(Thickness{12.0f});
    panel.set_border_thickness(Thickness{1.0f});
    auto*  tiles = panel.set_layout(std::make_unique<FeaturedTiles>());
    Block* first = panel.add(std::make_unique<Block>(10.0f, 96.0f));
    Block* a     = panel.add(std::make_unique<Block>(10.0f, 72.0f));
    Block* b     = panel.add(std::make_unique<Block>(10.0f, 92.0f));

    const Size desired = panel.measure(Size{226.0f, Unbounded}, measurer, Theme::default_light());
    CHECK_THAT(desired.width, WithinAbs(226.0f, 1e-3));
    CHECK_THAT(desired.height, WithinAbs(96.0f + 8.0f + 92.0f + 26.0f, 1e-3));

    panel.arrange(Rect{0.0f, 0.0f, 226.0f, desired.height});
    check_rect(first->bounds(), 13.0f, 13.0f, 200.0f, 96.0f); // across the whole room
    CHECK_THAT(a->bounds().y, WithinAbs(13.0f + 96.0f + 8.0f, 1e-3));
    CHECK_THAT(b->bounds().x, WithinAbs(13.0f + 96.0f + 8.0f, 1e-3));

    ui::testing::clear_dirty(panel);
    tiles->change_spacing(4.0f);
    CHECK(panel.is_dirty(DirtyFlags::Layout));
}

TEST_CASE("A layout that only needs arranging again marks its LayoutPanel for arrange", "[layout-panel][layout]")
{
    LayoutPanelControl panel;
    auto*              wrap = panel.set_layout(std::make_unique<WrapLayoutEngine>());
    panel.add(std::make_unique<Block>(30.0f, 20.0f));
    panel.add(std::make_unique<Block>(30.0f, 20.0f));
    layout(panel, Size{100.0f, Unbounded}, Rect{0.0f, 0.0f, 100.0f, 20.0f});

    ui::testing::clear_dirty(panel);
    wrap->set_line_alignment(ItemsJustification::SpaceBetween);
    CHECK(panel.is_dirty(DirtyFlags::Arrange));
    CHECK_FALSE(panel.is_dirty(DirtyFlags::Measure));
}

TEST_CASE("StackLayout stacks with spacing across the full width, and no layout overlays", "[layout-panel][layout]")
{
    HeadlessTextMeasurer measurer;
    LayoutPanelControl   panel;
    auto*                stack = panel.set_layout(std::make_unique<StackLayoutEngine>());
    stack->set_spacing(8.0f);
    Block*     a       = panel.add(std::make_unique<Block>(30.0f, 20.0f));
    Block*     b       = panel.add(std::make_unique<Block>(60.0f, 10.0f));
    const Size desired = panel.measure(Size{200.0f, Unbounded}, measurer, Theme::default_light());
    CHECK(desired.width == 60.0f);
    CHECK(desired.height == 38.0f);
    panel.arrange(Rect{0.0f, 0.0f, 200.0f, 38.0f});
    CHECK(a->bounds().y == 0.0f);
    CHECK(b->bounds().y == 28.0f);

    panel.set_layout<Layout>(nullptr);
    layout(panel, Size{200.0f, 100.0f}, Rect{0.0f, 0.0f, 200.0f, 100.0f});
    CHECK(a->bounds().y == 0.0f);
    CHECK(b->bounds().y == 0.0f);
}

// ---- custom panels and layouts ----

namespace
{
// A custom panel that stacks its children on top of one another.
class Pile final : public Panel
{
public:
    Element* add(std::unique_ptr<Element> child)
    {
        return adopt_child(std::move(child));
    }
    bool remove(Element* child)
    {
        return remove_child(child);
    }
    void clear()
    {
        clear_children();
    }
    [[nodiscard]] std::size_t count() const
    {
        return children_.size();
    }

protected:
    Size measure_override(Size available, const MeasureContext& context) const override
    {
        Size size{};
        for (const auto& child : children_)
        {
            const Size d = child->measure(available, context);
            size         = Size{std::max(size.width, d.width), std::max(size.height, d.height)};
        }
        return size;
    }
    void arrange_override(const Rect& final_rect, const ArrangeContext&) override
    {
        for (const auto& child : children_)
        {
            child->arrange(final_rect);
        }
    }
};

// Deriving from Layout itself, not one of its two kinds, cannot construct: no host could drive it.
class StrayLayout final : public Layout
{
};
static_assert(!std::is_default_constructible_v<StrayLayout>);
static_assert(std::is_default_constructible_v<FeaturedTiles>);
} // namespace

TEST_CASE("A custom panel removes and clears its children through Panel", "[panel]")
{
    Pile     pile;
    Element* first  = pile.add(std::make_unique<BorderControl>());
    Element* second = pile.add(std::make_unique<BorderControl>());
    REQUIRE(first->parent() == &pile);
    ui::testing::clear_dirty(pile);

    REQUIRE(pile.remove(first));
    REQUIRE(pile.count() == 1);
    REQUIRE(pile.is_dirty(DirtyFlags::Layout));
    REQUIRE_FALSE(pile.remove(first)); // no longer a child
    REQUIRE(second->parent() == &pile);

    ui::testing::clear_dirty(pile);
    pile.clear();
    REQUIRE(pile.count() == 0);
    REQUIRE(pile.is_dirty(DirtyFlags::Layout));
}

TEST_CASE("A custom panel's removed child outlives the dispatch that removed it", "[panel][lifetime]")
{
    HeadlessUI ui    = create_headless();
    auto       owned = std::make_unique<Pile>();
    Pile*      pile  = owned.get();
    auto*      child = static_cast<ButtonControl*>(pile->add(ui.button("Remove me")));
    bool       alive = false;
    child->on_click(
        [&]
        {
            pile->remove(child);
            alive = !child->text().empty(); // still a valid object inside its own handler
        });
    ui.window().set_content(std::move(owned));
    ui.layout({400, 300});

    const Point at = child->bounds().center();
    seams(ui.window()).handle_pointer({at, PointerButton::Left, PointerAction::Down});
    seams(ui.window()).handle_pointer({at, PointerButton::Left, PointerAction::Up});
    REQUIRE(alive);
    REQUIRE(pile->count() == 0);
}

namespace
{
// Records its ticks and its destruction, to tell whether on_tick() ran on a live object.
class Fading final : public Element
{
public:
    Fading(bool& destroyed, bool& destroyed_in_tick, int& ticks) :
        destroyed_(destroyed),
        destroyed_in_tick_(destroyed_in_tick),
        ticks_(ticks)
    {
    }
    ~Fading() override
    {
        destroyed_ = true;
    }
    Fading(const Fading&)            = delete;
    Fading& operator=(const Fading&) = delete;
    Fading(Fading&&)                 = delete;
    Fading& operator=(Fading&&)      = delete;

protected:
    Size measure_override(Size, const MeasureContext&) const override
    {
        return {10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

    void tick_override(Duration) override
    {
        ++ticks_;
        destroyed_in_tick_ = destroyed_;
    }


private:
    bool& destroyed_;
    bool& destroyed_in_tick_;
    int&  ticks_;
};
} // namespace

TEST_CASE("An element its own finished transition removes lives through its tick", "[panel][lifetime][tick]")
{
    bool  destroyed         = false;
    bool  destroyed_in_tick = true;
    int   ticks             = 0;
    Pile  pile;
    auto* fading = static_cast<Fading*>(pile.add(std::make_unique<Fading>(destroyed, destroyed_in_tick, ticks)));
    fading->run_transition(transition_storyboards::content_out(), [&] { pile.remove(fading); });
    REQUIRE(fading->transition_running());

    // Ticked directly, with no Window around it: the handler retires the element mid-tick.
    fading->tick(Duration{10'000.0});
    REQUIRE(ticks == 1);
    REQUIRE_FALSE(destroyed_in_tick);
    REQUIRE(destroyed);
    REQUIRE(pile.count() == 0);
}
