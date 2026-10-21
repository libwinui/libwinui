// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/panels/WrapPanel.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
// Records the `available` extent it was last measured with.
class MeasureProbe final : public Element
{
public:
    mutable Size last_available{-1.0f, -1.0f};

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
    {
        last_available = available;
        return Size{10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
};

// Records every `available` extent it is measured with.
class MeasureLog final : public Element
{
public:
    mutable std::vector<Size> offers;

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
    {
        offers.push_back(available);
        return Size{10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
};
} // namespace

TEST_CASE("Grid measures a child that only fills Star tracks at its resolved size", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    // Three Grids deep, each with an Auto row above a Star row, as a page with a toolbar is.
    GridControl outer;
    outer.add_row(GridLength::auto_());
    outer.add_row(GridLength::fraction());
    (void)outer.add(std::make_unique<MeasureProbe>(), 0, 0);
    auto* middle = outer.add(std::make_unique<GridControl>(), 1, 0);
    middle->add_row(GridLength::auto_());
    middle->add_row(GridLength::fraction());
    middle->add_column(GridLength::fraction());
    middle->add_column(GridLength::dip(100.0f));
    (void)middle->add(std::make_unique<MeasureProbe>(), 0, 0);
    auto*       inner = middle->add(std::make_unique<GridControl>(), 1, 0);
    MeasureLog* list  = inner->add(std::make_unique<MeasureLog>(), 0, 0);

    (void)outer.measure({400.0f, 300.0f}, m, theme);
    REQUIRE_FALSE(list->offers.empty());
    for (const Size& offer : list->offers)
    {
        CHECK(offer.width == 300.0f);
        CHECK(offer.height == 280.0f); // 300 less the two 10 DIP Auto rows
    }

    // An Auto track still measures its child unbounded, so the child can size it.
    GridControl sized;
    sized.add_row(GridLength::auto_());
    MeasureLog* content = sized.add(std::make_unique<MeasureLog>(), 0, 0);
    (void)sized.measure({400.0f, 300.0f}, m, theme);
    CHECK(content->offers.front().height == Unbounded);
}

TEST_CASE("Grid with Pixels tracks arranges children at literal offsets", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::dip(100.0f));
    grid.add_column(GridLength::dip(50.0f));
    grid.add_row(GridLength::dip(80.0f));

    ButtonControl* a = grid.add(std::make_unique<ButtonControl>("A"), 0, 0);
    ButtonControl* b = grid.add(std::make_unique<ButtonControl>("B"), 0, 1);

    const Size desired = grid.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE_THAT(desired.width, WithinAbs(150.0, 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(80.0, 1e-3));

    grid.arrange({0, 0, desired.width, desired.height});
    REQUIRE_THAT(a->bounds().left(), WithinAbs(0.0f, 0.5));
    REQUIRE_THAT(a->bounds().width, WithinAbs(100.0, 0.5));
    REQUIRE_THAT(b->bounds().left(), WithinAbs(100.0, 0.5));
    REQUIRE_THAT(b->bounds().width, WithinAbs(50.0, 0.5));
    REQUIRE_THAT(a->bounds().height, WithinAbs(80.0, 0.5));
}

TEST_CASE("Grid places a child past the last track in the last track", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::dip(100.0f));
    grid.add_column(GridLength::dip(50.0f));
    grid.add_row(GridLength::dip(30.0f));
    grid.add_row(GridLength::dip(40.0f));

    MeasureProbe* past = grid.add(std::make_unique<MeasureProbe>(), 7, 9);
    MeasureProbe* span = grid.add(std::make_unique<MeasureProbe>(), 1, 0, 5, 5);

    const Size desired = grid.measure({Unbounded, Unbounded}, m, theme);
    CHECK_THAT(past->last_available.width, WithinAbs(50.0, 1e-3));
    CHECK_THAT(past->last_available.height, WithinAbs(40.0, 1e-3));
    grid.arrange({0, 0, desired.width, desired.height});
    CHECK(past->bounds() == Rect{100.0f, 30.0f, 50.0f, 40.0f});
    // The span stops at the last track on both axes.
    CHECK(span->bounds() == Rect{0.0f, 30.0f, 150.0f, 40.0f});
}

TEST_CASE("Grid distributes Star columns proportionally over the available width", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::fraction(1.0f));
    grid.add_column(GridLength::fraction(2.0f));
    grid.add_row(GridLength::fraction());

    ButtonControl* a = grid.add(std::make_unique<ButtonControl>("A"), 0, 0);
    ButtonControl* b = grid.add(std::make_unique<ButtonControl>("B"), 0, 1);

    (void)grid.measure({300, 100}, m, theme);
    grid.arrange({0, 0, 300, 100});

    REQUIRE_THAT(a->bounds().width, WithinAbs(100.0, 0.5)); // 1/3 of 300
    REQUIRE_THAT(b->bounds().width, WithinAbs(200.0, 0.5)); // 2/3 of 300
    REQUIRE_THAT(b->bounds().left(), WithinAbs(a->bounds().right(), 0.5));
}

TEST_CASE("Grid Auto row sizes to the tallest single-span child assigned to it", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::fraction());
    grid.add_row(GridLength::auto_());

    ButtonControl* button         = grid.add(std::make_unique<ButtonControl>("Item"), 0, 0);
    const Size     button_desired = button->measure({Unbounded, Unbounded}, m, theme);

    const Size desired = grid.measure({200, Unbounded}, m, theme);
    REQUIRE_THAT(desired.height, WithinAbs(button_desired.height, 1e-3));
}

TEST_CASE("Grid row/column span covers the spanned tracks plus their spacing", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::dip(60.0f));
    grid.add_column(GridLength::dip(40.0f));
    grid.add_row(GridLength::dip(30.0f));
    grid.set_column_spacing(10.0f);

    ButtonControl* spanning = grid.add(std::make_unique<ButtonControl>("Wide"), 0, 0, 1, 2);

    (void)grid.measure({Unbounded, Unbounded}, m, theme);
    grid.arrange({0, 0, 110.0f, 30.0f});

    REQUIRE_THAT(spanning->bounds().width, WithinAbs(110.0, 0.5));
}

TEST_CASE("Grid rows/columns can be added dynamically before layout", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    REQUIRE(grid.row_count() == 0);
    REQUIRE(grid.column_count() == 0);

    grid.add_row(GridLength::dip(20.0f));
    grid.add_row(GridLength::dip(30.0f));
    grid.add_column(GridLength::dip(40.0f));
    REQUIRE(grid.row_count() == 2);
    REQUIRE(grid.column_count() == 1);

    ButtonControl* top    = grid.add(std::make_unique<ButtonControl>("Top"), 0, 0);
    ButtonControl* bottom = grid.add(std::make_unique<ButtonControl>("Bottom"), 1, 0);

    (void)grid.measure({Unbounded, Unbounded}, m, theme);
    grid.arrange({0, 0, 40.0f, 50.0f});

    REQUIRE_THAT(top->bounds().top(), WithinAbs(0.0f, 0.5));
    REQUIRE_THAT(bottom->bounds().top(), WithinAbs(20.0, 0.5));
}

TEST_CASE("A Grid's Star tracks ask for their content's size, not the room offered", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::fraction());
    grid.add_column(GridLength::fraction());
    grid.add_column(GridLength::dip(20.0f));
    grid.set_column_spacing(4.0f);
    (void)grid.add(std::make_unique<MeasureProbe>(), 0, 0, 1, 2); // 10 by 10 across both Star columns

    // The child's 10 cover the spacing between the Star columns (4) and 3 of each; the Dip column
    // and its spacing add 24. The one Star row asks for the child's 10.
    const Size desired = grid.measure({300.0f, 200.0f}, m, theme);
    CHECK_THAT(desired.width, WithinAbs(34.0, 0.5));
    CHECK_THAT(desired.height, WithinAbs(10.0, 0.5));

    // Arranged wider, the Star columns still share out what they get.
    grid.arrange({0, 0, 300.0f, 200.0f});
    CHECK_THAT(grid.column_size(0), WithinAbs(136.0, 0.5)); // (300 - 20 - 2 * 4) / 2
}

TEST_CASE("A Grid with no track definitions has one implicit Star cell", "[grid][layout]")
{
    // A Grid without row or column definitions: every child fills one cell.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    auto*       a = grid.add(std::make_unique<MeasureProbe>(), 0, 0);
    auto*       b = grid.add(std::make_unique<MeasureProbe>(), 0, 0);

    // The Star cell offers its child all the room but asks only for what the child wants.
    const Size desired = grid.measure({100.0f, 80.0f}, m, theme);
    REQUIRE_THAT(desired.width, WithinAbs(10.0, 0.5));
    REQUIRE_THAT(a->last_available.width, WithinAbs(100.0, 0.5));
    grid.arrange({0, 0, 100.0f, 80.0f});
    REQUIRE_THAT(a->bounds().width, WithinAbs(100.0, 0.5));
    REQUIRE_THAT(b->bounds().height, WithinAbs(80.0, 0.5));
    REQUIRE(grid.row_count() == 0); // the implicit track is not a definition

    GridControl columns_only;
    columns_only.add_column(GridLength::fraction());
    columns_only.add_column(GridLength::fraction());
    auto* right = columns_only.add(std::make_unique<MeasureProbe>(), 0, 1);
    (void)columns_only.measure({100.0f, 50.0f}, m, theme);
    columns_only.arrange({0, 0, 100.0f, 50.0f});
    REQUIRE_THAT(right->bounds().left(), WithinAbs(50.0, 0.5));
    REQUIRE_THAT(right->bounds().height, WithinAbs(50.0, 0.5));
}

TEST_CASE("Grid::set_column_width switches a track to a fixed pixel size", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::fraction());
    grid.add_column(GridLength::fraction());
    grid.add_row(GridLength::dip(20.0f));

    ButtonControl* a = grid.add(std::make_unique<ButtonControl>("A"), 0, 0);
    ButtonControl* b = grid.add(std::make_unique<ButtonControl>("B"), 0, 1);


    (void)grid.measure({200, 20}, m, theme);
    grid.arrange({0, 0, 200, 20});
    REQUIRE_THAT(a->bounds().width, WithinAbs(100.0, 0.5)); // even split before resize

    grid.set_column_width(0, 150.0f);
    REQUIRE(grid.column(0).unit == GridUnit::Dip);
    REQUIRE_THAT(grid.column(0).value, WithinAbs(150.0, 1e-3));

    (void)grid.measure({200, 20}, m, theme);
    grid.arrange({0, 0, 200, 20});
    REQUIRE_THAT(a->bounds().width, WithinAbs(150.0, 0.5));
    REQUIRE_THAT(b->bounds().width, WithinAbs(50.0, 0.5)); // remaining Star track absorbs the rest

    // Out-of-range indices are ignored rather than throwing.
    grid.set_column_width(99, 10.0f);
    grid.set_row_height(99, 10.0f);
}

TEST_CASE("Grid::column_size/row_size report what a drag handler must accumulate", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::fraction());
    grid.add_column(GridLength::fraction());
    grid.add_row(GridLength::dip(40.0f));

    ButtonControl* a = grid.add(std::make_unique<ButtonControl>("A"), 0, 0);
    grid.add(std::make_unique<ButtonControl>("B"), 0, 1);

    (void)grid.measure({200, 40}, m, theme);
    grid.arrange({0, 0, 200, 40});

    // A Splitter or ResizeGripper handler accumulates the track size, which a margin or an
    // alignment could make differ from the child's.
    REQUIRE_THAT(grid.column_size(0), WithinAbs(100.0, 0.5));
    REQUIRE_THAT(a->bounds().width, WithinAbs(100.0, 0.5));
    REQUIRE_THAT(grid.row_size(0), WithinAbs(40.0, 0.5));

    // Feeding the track size back in accumulates: ten +1px steps grow it by 10.
    for (int step = 0; step < 10; ++step)
    {
        grid.set_column_width(0, grid.column_size(0) + 1.0f);
    }
    REQUIRE_THAT(grid.column_size(0), WithinAbs(110.0, 1e-3));

    (void)grid.measure({200, 40}, m, theme);
    grid.arrange({0, 0, 200, 40});
    REQUIRE_THAT(grid.column_size(0), WithinAbs(110.0, 0.5));
    REQUIRE_THAT(a->bounds().width, WithinAbs(110.0, 0.5));

    // Rows behave the same way, and an out-of-range track reports 0 rather than throwing.
    grid.set_row_height(0, grid.row_size(0) + 5.0f);
    REQUIRE_THAT(grid.row_size(0), WithinAbs(45.0, 1e-3));
    REQUIRE_THAT(grid.column_size(99), WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(grid.row_size(99), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Grid forwards pointer input and focus collection to its children", "[grid][headless][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    auto grid_owned = std::make_unique<GridControl>();
    grid_owned->add_column(GridLength::fraction());
    grid_owned->add_row(GridLength::dip(40.0f));
    ButtonControl* button = grid_owned->add(std::make_unique<ButtonControl>("Click"), 0, 0);

    int clicks = 0;
    button->on_click([&] { ++clicks; });

    (void)grid_owned->measure({200, 40}, m, theme);
    grid_owned->arrange({0, 0, 200, 40});

    std::vector<Element*> focusables;
    grid_owned->collect_focusables(focusables);
    REQUIRE(focusables.size() == 1);
    REQUIRE(focusables[0] == button);

    const Point p = button->bounds().center();
    grid_owned->handle_pointer({p, PointerButton::Left, PointerAction::Down});
    grid_owned->handle_pointer({p, PointerButton::Left, PointerAction::Up});
    REQUIRE(clicks == 1);
}

TEST_CASE("A child is measured against its own tracks, not the whole Grid", "[grid][layout]")
{
    // Two passes, because Star tracks resolve after Pixels and Auto.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::dip(80.0f));
    grid.add_column(GridLength::fraction());
    grid.add_row(GridLength::dip(50.0f));
    grid.add_row(GridLength::auto_());

    MeasureProbe* pixels_cell = grid.add(std::make_unique<MeasureProbe>(), 0, 0);
    MeasureProbe* star_cell   = grid.add(std::make_unique<MeasureProbe>(), 0, 1);
    MeasureProbe* auto_cell   = grid.add(std::make_unique<MeasureProbe>(), 1, 0);

    (void)grid.measure({400.0f, 400.0f}, m, theme);

    // Pixels x Pixels: both extents are known up front.
    REQUIRE_THAT(pixels_cell->last_available.width, WithinAbs(80.0, 1e-3));
    REQUIRE_THAT(pixels_cell->last_available.height, WithinAbs(50.0, 1e-3));
    // Star x Pixels: the second pass re-measures it once the star share is known (400 - 80).
    REQUIRE_THAT(star_cell->last_available.width, WithinAbs(320.0, 1e-3));
    REQUIRE_THAT(star_cell->last_available.height, WithinAbs(50.0, 1e-3));
    // Auto: unbounded, because the child's own answer sizes the track.
    REQUIRE_THAT(auto_cell->last_available.width, WithinAbs(80.0, 1e-3));
    REQUIRE(auto_cell->last_available.height == Unbounded);
}

TEST_CASE("A spanning child is measured against the sum of its tracks plus their spacing", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::dip(60.0f));
    grid.add_column(GridLength::dip(40.0f));
    grid.add_row(GridLength::dip(30.0f));
    grid.set_column_spacing(10.0f);

    MeasureProbe* wide = grid.add(std::make_unique<MeasureProbe>(), 0, 0, 1, 2);
    (void)grid.measure({500.0f, 500.0f}, m, theme);

    REQUIRE_THAT(wide->last_available.width, WithinAbs(60.0 + 10.0 + 40.0, 1e-3));
}

TEST_CASE("Star tracks size to their content when the available extent is unbounded", "[grid][layout]")
{
    // With an unbounded axis a Star track acts
    // like Auto. Otherwise a Grid inside a horizontal StackControl loses its Star columns.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::fraction());
    grid.add_column(GridLength::dip(6.0f));
    grid.add_column(GridLength::fraction());
    grid.add_row(GridLength::dip(40.0f));

    ButtonControl* left  = grid.add(std::make_unique<ButtonControl>("Left"), 0, 0);
    ButtonControl* right = grid.add(std::make_unique<ButtonControl>("Right"), 0, 2);

    const Size  desired     = grid.measure({Unbounded, Unbounded}, m, theme); // unbounded on both axes
    const float left_width  = left->measure({Unbounded, Unbounded}, m, theme).width;
    const float right_width = right->measure({Unbounded, Unbounded}, m, theme).width;

    REQUIRE_THAT(desired.width, WithinAbs(left_width + 6.0 + right_width, 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(40.0, 1e-3));

    grid.arrange({0.0f, 0.0f, desired.width, desired.height});
    REQUIRE(left->bounds().width > 0.0f);
    REQUIRE(right->bounds().width > 0.0f);
}

TEST_CASE("Star tracks re-divide the rect arrange is handed, not the one measure was offered", "[grid][layout]")
{
    // A Star track is a share of the final extent, not of what
    // measure was offered.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::fraction());
    grid.add_column(GridLength::dip(20.0f));
    grid.add_column(GridLength::fraction());
    grid.add_row(GridLength::dip(40.0f));

    MeasureProbe* left  = grid.add(std::make_unique<MeasureProbe>(), 0, 0);
    MeasureProbe* right = grid.add(std::make_unique<MeasureProbe>(), 0, 2);

    (void)grid.measure({Unbounded, Unbounded}, m, theme); // measured unbounded: both Star columns are 10 wide
    grid.arrange({0.0f, 0.0f, 420.0f, 40.0f});            // ... then handed 420

    REQUIRE_THAT(left->bounds().width, WithinAbs(200.0, 1e-3)); // (420 - 20) / 2
    REQUIRE_THAT(right->bounds().width, WithinAbs(200.0, 1e-3));
    REQUIRE_THAT(right->bounds().left(), WithinAbs(220.0, 1e-3));
}

TEST_CASE("An Auto row takes the height a Star-column child has at the column's width", "[grid][layout][wrap]")
{
    // Matches CGrid: an Auto row in a Star column is measured once the column has its width, or a
    // wrapping label is one line tall and the row clips the rest.
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::fraction());
    grid.add_column(GridLength::dip(40.0f));
    grid.add_row(GridLength::auto_());

    TextControl* label = grid.add(std::make_unique<TextControl>("aaaa bbbb cccc dddd"), 0, 0);
    TextStyle    style{10.0f, TextAlign::Leading, TextAlign::Leading, "Segoe UI"};
    style.wrapping = TextWrapping::Wrap;
    label->set_text_style(style);

    // 90px leaves the Star column 50: nine 5.5px characters a line, so two lines of 13.5.
    const Size desired = grid.measure({90.0f, Unbounded}, m, theme);
    CHECK_THAT(desired.height, WithinAbs(27.0, 1e-3));
    grid.arrange({0.0f, 0.0f, 90.0f, desired.height});
    CHECK_THAT(label->bounds().height, WithinAbs(27.0, 1e-3));
}

TEST_CASE("Grid moves a child to another cell and fills its background", "[grid]")
{
    HeadlessTextMeasurer measurer;
    GridControl          grid;
    grid.add_column(GridLength::dip(50.0f));
    grid.add_column(GridLength::dip(50.0f));
    grid.add_row(GridLength::dip(50.0f));
    TextControl* label = grid.add(std::make_unique<TextControl>("x"), 0, 0);
    grid.set_column_spacing(8.0f);
    grid.set_background(colors::Black);
    (void)grid.measure(Size{200.0f, 100.0f}, measurer, Theme::default_light());
    grid.arrange(Rect{0.0f, 0.0f, 108.0f, 50.0f});
    CHECK(label->bounds().x < 50.0f);

    grid.set_column(label, 1);
    CHECK(grid.is_dirty(DirtyFlags::Layout));
    (void)grid.measure(Size{200.0f, 100.0f}, measurer, Theme::default_light());
    grid.arrange(Rect{0.0f, 0.0f, 108.0f, 50.0f});
    CHECK(label->bounds().x >= 58.0f);

    TestRenderer r;
    grid.render(r, Theme::light());
    REQUIRE(r.count() >= 1);
    CHECK(std::get<FillRoundedRect>(r.commands[0]).color == colors::Black);
}

TEST_CASE("minmax() keeps a fraction track between its limits and gives the rest to the others", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    GridControl          grid;
    grid.add_column(minmax(GridLength::dip(150.0f), GridLength::fraction()));
    grid.add_column(GridLength::fraction(3.0f));
    grid.add_column(minmax(GridLength::auto_(), GridLength::dip(30.0f)));
    grid.add(std::make_unique<MeasureProbe>(), 0, 2);

    const Theme theme = Theme::light();
    (void)grid.measure({400.0f, 50.0f}, m, theme);
    grid.arrange({0.0f, 0.0f, 400.0f, 50.0f});
    // The auto column is 10 wide; of the 390 left, a quarter (97.5) is under 150, so the first
    // column holds at 150 and the second takes the other 240.
    REQUIRE_THAT(grid.column_size(0), WithinAbs(150.0, 1e-3));
    REQUIRE_THAT(grid.column_size(1), WithinAbs(240.0, 1e-3));
    REQUIRE_THAT(grid.column_size(2), WithinAbs(10.0, 1e-3));

    REQUIRE(minmax(GridLength::dip(100.0f), GridLength::dip(300.0f)) ==
            GridLength{GridUnit::Fraction, 1.0f, 100.0f, 300.0f});
    REQUIRE(minmax(GridLength::auto_(), GridLength::dip(300.0f)) == GridLength{GridUnit::Auto, 0.0f, 0.0f, 300.0f});
}

TEST_CASE("A fixed and an auto track hold their limits too", "[grid][layout]")
{
    HeadlessTextMeasurer m;
    GridControl          grid;
    GridLength           fixed = GridLength::dip(500.0f);
    fixed.max                  = 120.0f;
    grid.add_column(fixed);
    grid.add_column(minmax(GridLength::dip(40.0f), GridLength::auto_()));
    grid.add(std::make_unique<MeasureProbe>(), 0, 1); // 10 wide, under the 40 minimum
    const Size desired = grid.measure({Unbounded, Unbounded}, m, Theme::light());
    REQUIRE_THAT(desired.width, WithinAbs(160.0, 1e-3));
    REQUIRE_THAT(grid.column_size(0), WithinAbs(120.0, 1e-3));

    grid.set_column_width(0, 1000.0f); // keeps its max
    REQUIRE_THAT(grid.column_size(0), WithinAbs(120.0, 1e-3));
}

TEST_CASE("Grid, stacks and WrapPanel inset their children by border and padding", "[grid][stack][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme  = Theme::light();
    const Thickness      border = Edges{.left = 1.0f, .top = 2.0f, .right = 3.0f, .bottom = 4.0f};
    const Thickness      padding{10.0f};

    GridControl grid;
    grid.set_border_thickness(border);
    grid.set_padding(padding);
    MeasureProbe* in_grid = grid.add(std::make_unique<MeasureProbe>(), 0, 0);
    Size          desired = grid.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE(desired == Size{10.0f + 24.0f, 10.0f + 26.0f});
    grid.arrange({0.0f, 0.0f, 100.0f, 100.0f});
    REQUIRE(in_grid->bounds().origin() == Point{11.0f, 12.0f});

    StackControl stack{Orientation::Vertical};
    stack.set_border_thickness(border);
    stack.set_padding(padding);
    stack.set_child_alignment(Alignment::Leading);
    MeasureProbe* in_stack = stack.add(std::make_unique<MeasureProbe>());
    desired                = stack.measure({200.0f, 200.0f}, m, theme);
    REQUIRE(desired == Size{10.0f + 24.0f, 10.0f + 26.0f});
    REQUIRE(in_stack->last_available.width == 200.0f - 24.0f);
    stack.arrange({0.0f, 0.0f, 200.0f, 200.0f});
    REQUIRE(in_stack->bounds().origin() == Point{11.0f, 12.0f});

    WrapPanelControl wrap;
    wrap.set_border_thickness(border);
    wrap.set_padding(padding);
    MeasureProbe* in_wrap = wrap.add(std::make_unique<MeasureProbe>());
    desired               = wrap.measure({200.0f, 200.0f}, m, theme);
    REQUIRE(desired == Size{10.0f + 24.0f, 10.0f + 26.0f});
    wrap.arrange({0.0f, 0.0f, 200.0f, 200.0f});
    REQUIRE(in_wrap->bounds().origin() == Point{11.0f, 12.0f});
}

TEST_CASE("A Grid with a border brush draws the border around its background", "[grid][render]")
{
    GridControl grid;
    grid.set_background(Color::from_argb(0xFFFFFFFF));
    grid.set_border_brush(Color::from_argb(0xFF000000));
    grid.set_border_thickness(Thickness{2.0f});
    grid.set_corner_radius(4.0f);
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    (void)grid.measure({50.0f, 50.0f}, m, theme);
    grid.arrange({0.0f, 0.0f, 50.0f, 50.0f});

    TestRenderer renderer;
    grid.render(renderer, Theme::light());
    REQUIRE(renderer.count() >= 2); // the background inside, then the border
}
