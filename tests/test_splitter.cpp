// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panes/Splitter.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE("Splitter measure respects orientation and fills the cross axis", "[splitter][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    SplitterControl vertical{Orientation::Vertical};
    const Size      v_desired = vertical.measure({Unbounded, 400}, m, theme);
    REQUIRE_THAT(v_desired.width, WithinAbs(vertical.thickness(), 1e-3));
    REQUIRE_THAT(v_desired.height, WithinAbs(400.0, 1e-3));

    SplitterControl horizontal{Orientation::Horizontal};
    const Size      h_desired = horizontal.measure({500, Unbounded}, m, theme);
    REQUIRE_THAT(h_desired.height, WithinAbs(horizontal.thickness(), 1e-3));
    REQUIRE_THAT(h_desired.width, WithinAbs(500.0, 1e-3));
}

TEST_CASE("Splitter is focusable by default and can leave the tab ring", "[splitter][focus]")
{
    SplitterControl splitter;
    REQUIRE(splitter.focusable());

    std::vector<Element*> focusables;
    splitter.collect_focusables(focusables);
    REQUIRE(focusables.size() == 1);

    splitter.set_focusable(false);
    focusables.clear();
    splitter.collect_focusables(focusables);
    REQUIRE(focusables.empty());
}

TEST_CASE("Splitter reports hover state from Move events", "[splitter][input]")
{
    SplitterControl splitter;
    splitter.arrange({0, 0, 6, 200});
    REQUIRE_FALSE(splitter.hovered());

    splitter.handle_pointer({{3, 100}, PointerButton::None, PointerAction::Move});
    REQUIRE(splitter.hovered());

    splitter.handle_pointer({{}, PointerButton::None, PointerAction::Leave});
    REQUIRE_FALSE(splitter.hovered());
}

TEST_CASE("Vertical Splitter fires incremental horizontal deltas while dragging", "[splitter][input]")
{
    SplitterControl splitter{Orientation::Vertical};
    splitter.arrange({100, 0, 6, 200});

    std::vector<float> deltas;
    splitter.on_drag_delta([&](float d) { deltas.push_back(d); });

    splitter.handle_pointer({{103, 50}, PointerButton::Left, PointerAction::Down});
    REQUIRE(splitter.dragging());

    splitter.handle_pointer({{120, 50}, PointerButton::None, PointerAction::Move});
    splitter.handle_pointer({{135, 60}, PointerButton::None, PointerAction::Move});

    REQUIRE(deltas.size() == 2);
    REQUIRE_THAT(deltas[0], WithinAbs(17.0, 1e-3)); // 120 - 103
    REQUIRE_THAT(deltas[1], WithinAbs(15.0, 1e-3)); // 135 - 120

    splitter.handle_pointer({{135, 60}, PointerButton::Left, PointerAction::Up});
    REQUIRE_FALSE(splitter.dragging());

    splitter.handle_pointer({{200, 60}, PointerButton::None, PointerAction::Move});
    REQUIRE(deltas.size() == 2);
}

TEST_CASE("Horizontal Splitter fires incremental vertical deltas while dragging", "[splitter][input]")
{
    SplitterControl splitter{Orientation::Horizontal};
    splitter.arrange({0, 100, 200, 6});

    std::vector<float> deltas;
    splitter.on_drag_delta([&](float d) { deltas.push_back(d); });

    splitter.handle_pointer({{50, 102}, PointerButton::Left, PointerAction::Down});
    splitter.handle_pointer({{50, 115}, PointerButton::None, PointerAction::Move});

    REQUIRE(deltas.size() == 1);
    REQUIRE_THAT(deltas[0], WithinAbs(13.0, 1e-3)); // 115 - 102
}

TEST_CASE("A drag started outside the splitter's bounds does not start dragging", "[splitter][input]")
{
    SplitterControl splitter;
    splitter.arrange({0, 0, 6, 200});

    splitter.handle_pointer({{500, 500}, PointerButton::Left, PointerAction::Down});
    REQUIRE_FALSE(splitter.dragging());
}

TEST_CASE("A Splitter taken out of the tab ring does not resize by keys", "[splitter][keyboard]")
{
    SplitterControl s{Orientation::Vertical};
    float           total = 0.0f;
    s.on_drag_delta([&](float d) { total += d; });

    s.set_focusable(false);
    ui::testing::set_focused(s, true);
    s.handle_key({Key::ArrowRight, KeyAction::Down});
    REQUIRE_THAT(total, WithinAbs(0.0, 1e-3));

    s.set_focusable(true);
    s.handle_key({Key::ArrowRight, KeyAction::Down});
    REQUIRE_THAT(total, WithinAbs(8.0, 1e-3)); // c_defaultKeyboardIncrement
}

TEST_CASE("Arrow keys step along the drag axis only, x4 with Shift", "[splitter][keyboard]")
{
    // One step per arrow key, four with Shift.
    SplitterControl s{Orientation::Vertical};
    s.set_focusable(true);
    ui::testing::set_focused(s, true);
    float total = 0.0f;
    s.on_drag_delta([&](float d) { total += d; });

    s.handle_key({Key::ArrowUp, KeyAction::Down});   // cross axis for a vertical bar
    s.handle_key({Key::ArrowDown, KeyAction::Down}); // ditto
    REQUIRE_THAT(total, WithinAbs(0.0, 1e-3));

    s.handle_key({Key::ArrowLeft, KeyAction::Down});
    REQUIRE_THAT(total, WithinAbs(-8.0, 1e-3));

    s.handle_key({Key::ArrowRight, KeyAction::Down, {.shift = true}});
    REQUIRE_THAT(total, WithinAbs(24.0, 1e-3)); // -8 + 8*4

    s.handle_key({Key::ArrowRight, KeyAction::Up}); // key-up is not a second step
    REQUIRE_THAT(total, WithinAbs(24.0, 1e-3));

    SplitterControl horizontal{Orientation::Horizontal};
    horizontal.set_focusable(true);
    ui::testing::set_focused(horizontal, true);
    float vertical_total = 0.0f;
    horizontal.on_drag_delta([&](float d) { vertical_total += d; });
    horizontal.handle_key({Key::ArrowLeft, KeyAction::Down});
    REQUIRE_THAT(vertical_total, WithinAbs(0.0, 1e-3));
    horizontal.handle_key({Key::ArrowDown, KeyAction::Down});
    REQUIRE_THAT(vertical_total, WithinAbs(8.0, 1e-3));
}

TEST_CASE("The separator is theme-driven and thickens to 2px while hot", "[splitter][render]")
{
    // The accent separator (theme.colors().selection here) at
    // 2dip, over the theme's rest-state divider rule.
    Theme           theme = Theme::light();
    SplitterControl s{Orientation::Vertical};
    s.set_thickness(6.0f);
    s.arrange({10.0f, 0.0f, 6.0f, 100.0f});

    {
        TestRenderer r;
        s.render(r, theme);
        REQUIRE(r.count_of<FillRoundedRect>() == 1);
        const auto& rest = std::get<FillRoundedRect>(r.commands[0]);
        REQUIRE(rest.color == theme.colors().divider);
        REQUIRE_THAT(rest.bounds.width, WithinAbs(1.0, 1e-3));
    }

    s.handle_pointer({{13.0f, 50.0f}, PointerButton::None, PointerAction::Move});
    {
        TestRenderer r;
        s.render(r, theme);
        const auto& hot = std::get<FillRoundedRect>(r.commands[0]);
        REQUIRE(hot.color == theme.colors().selection);
        REQUIRE_THAT(hot.bounds.width, WithinAbs(2.0, 1e-3));
        // Centered in the bar rather than hugging one edge.
        REQUIRE_THAT(hot.bounds.center().x, WithinAbs(13.0, 1e-3));
    }
}

TEST_CASE("A resting splitter's rule is visible on a black high-contrast page", "[splitter][render][regression]")
{
    // colors.divider default-constructs to opaque black, so high contrast has to set it or the
    // rule is black on a black page.
    OsThemeSnapshot os;
    os.high_contrast        = true;
    os.hc.window_background = colors::Black;
    os.hc.window_text       = colors::White;
    os.hc.control_border    = colors::White;
    const Theme theme       = resolve_theme(ThemeRequest{}, os);

    HeadlessTextMeasurer measurer;
    SplitterControl      splitter{Orientation::Vertical};
    (void)splitter.measure(Size{8.0f, 100.0f}, measurer, theme);
    splitter.arrange(Rect{0.0f, 0.0f, 8.0f, 100.0f});
    TestRenderer renderer;
    splitter.render(renderer, theme);

    REQUIRE(renderer.count() >= 1);
    const auto& rule = std::get<FillRoundedRect>(renderer.commands[0]);
    CHECK(rule.color != theme.colors().background);
    CHECK(rule.color == colors::White);
}

namespace
{
// A Grid of three columns, the middle one a 6 DIP splitter, laid out at 406 x 50.
struct SplitGrid
{
    GridControl      grid;
    SplitterControl* bar = nullptr;

    void layout()
    {
        HeadlessTextMeasurer m;
        const Theme          theme = Theme::light();
        (void)grid.measure({406.0f, 50.0f}, m, theme);
        grid.arrange({0.0f, 0.0f, 406.0f, 50.0f});
    }
    void drag(float from_x, float to_x)
    {
        const float y = 25.0f;
        bar->handle_pointer({{from_x, y}, PointerButton::Left, PointerAction::Down});
        bar->handle_pointer({{to_x, y}, PointerButton::None, PointerAction::Move});
        bar->handle_pointer({{to_x, y}, PointerButton::Left, PointerAction::Up});
        layout();
    }
};

void make_split_grid(SplitGrid& g, GridLength left, GridLength right)
{
    g.grid.add_column(left);
    g.grid.add_column(GridLength::dip(6.0f));
    g.grid.add_column(right);
    g.bar = g.grid.add(std::make_unique<SplitterControl>(), 0, 1);
    g.layout();
}
} // namespace

TEST_CASE("A Splitter in a Grid resizes the two fraction columns beside it", "[splitter][grid]")
{
    SplitGrid g;
    make_split_grid(g, GridLength::fraction(), GridLength::fraction());
    REQUIRE_THAT(g.grid.column_size(0), WithinAbs(200.0, 1e-3));

    g.drag(203.0f, 253.0f);
    REQUIRE_THAT(g.grid.column_size(0), WithinAbs(250.0, 0.5));
    REQUIRE_THAT(g.grid.column_size(2), WithinAbs(150.0, 0.5));
    REQUIRE(g.grid.column(0).unit == GridUnit::Fraction); // the two traded weight
    REQUIRE(g.grid.column(2).unit == GridUnit::Fraction);

    // The arrow keys step the same way.
    ui::testing::set_focused(*g.bar, true);
    g.bar->handle_key({Key::ArrowLeft, KeyAction::Down});
    g.layout();
    REQUIRE_THAT(g.grid.column_size(0), WithinAbs(242.0, 0.5));
}

TEST_CASE("A Splitter stops at the tracks' minmax limits", "[splitter][grid]")
{
    SplitGrid g;
    make_split_grid(g, minmax(GridLength::dip(150.0f), GridLength::fraction()), GridLength::fraction());

    const auto move = [&](float x) { g.bar->handle_pointer({{x, 25.0f}, PointerButton::None, PointerAction::Move}); };
    g.bar->handle_pointer({{203.0f, 25.0f}, PointerButton::Left, PointerAction::Down});
    move(103.0f); // 100 left: only 50 fit before the left column's 150
    g.layout();
    REQUIRE_THAT(g.grid.column_size(0), WithinAbs(150.0, 0.5));
    REQUIRE_THAT(g.grid.column_size(2), WithinAbs(250.0, 0.5));

    // The pointer has to come back to where the bar stopped before it moves again; two moves
    // before a layout both count.
    move(133.0f);
    REQUIRE_THAT(g.grid.column_size(0), WithinAbs(150.0, 0.5));
    move(163.0f);
    move(183.0f);
    g.layout();
    REQUIRE_THAT(g.grid.column_size(0), WithinAbs(180.0, 0.5));
}

TEST_CASE("A Splitter next to a fixed column turns it into the new fixed size", "[splitter][grid]")
{
    SplitGrid g;
    make_split_grid(g, GridLength::dip(100.0f), GridLength::fraction());
    g.drag(103.0f, 143.0f);
    REQUIRE(g.grid.column(0) == GridLength::dip(140.0f));
    REQUIRE_THAT(g.grid.column_size(2), WithinAbs(260.0, 0.5));
}

TEST_CASE("A Splitter with a drag handler leaves the Grid's tracks alone", "[splitter][grid]")
{
    SplitGrid g;
    make_split_grid(g, GridLength::fraction(), GridLength::fraction());
    float total = 0.0f;
    g.bar->on_drag_delta([&](float d) { total += d; });
    g.drag(203.0f, 233.0f);
    REQUIRE_THAT(total, WithinAbs(30.0, 1e-3));
    REQUIRE_THAT(g.grid.column_size(0), WithinAbs(200.0, 1e-3));
}
