// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/TableView.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <compare>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
std::vector<TableViewColumn> make_columns()
{
    return {{.header = "Name", .width = 100.0f}, {.header = "Age", .width = 60.0f}, {.header = "City", .width = 140.0f}};
}

std::vector<std::vector<std::string>> make_rows(std::size_t n)
{
    std::vector<std::vector<std::string>> rows;
    rows.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        rows.push_back({"Name " + std::to_string(i), std::to_string(20 + i), "City " + std::to_string(i)});
    }
    return rows;
}

KeyEvent key_down(Key k)
{
    return {k, KeyAction::Down};
}

// The table's own border, 1 DIP on every side, round the header and the rows.
constexpr float Border = 1.0f;

// The rounded-rect strokes other than the table's border: the selection's.
std::size_t selection_strokes(const TestRenderer& r, const Theme& theme)
{
    const Color border = ui::detail::controls(theme).list_item_subtle_selection
                             ? theme.palette.tabular_grid_line
                             : theme.surfaces.separator_dark;
    return static_cast<std::size_t>(
        std::ranges::count_if(r.commands,
                              [&](const auto& cmd)
                              {
                                  const auto* s = std::get_if<StrokeRoundedRect>(&cmd);
                                  return s != nullptr && s->color != border;
                              }));
}
} // namespace

TEST_CASE("TableView measures height as header_height + rows * row_height when unbounded", "[data-grid]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(10));
    grid.set_row_height(20.0f);

    const Size desired = grid.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE_THAT(desired.width, WithinAbs(300.0 + 2.0 * Border, 1e-3));               // 100+60+140
    REQUIRE_THAT(desired.height, WithinAbs(40.0 + 10.0 * 20.0 + 2.0 * Border, 1e-3)); // default header height 40
}

TEST_CASE("TableView header/body split at arrange()", "[data-grid]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(50));
    grid.set_row_height(20.0f);

    (void)grid.measure({320, 200}, m, theme);
    grid.arrange({0, 0, 320, 200});

    TestRenderer r;
    grid.render(r, theme);

    REQUIRE(r.count_of<FillRoundedRect>() >= 2); // the header strip, its rounded top and the rest
    REQUIRE(r.count_of<StrokeLine>() >= 1);
    REQUIRE(r.count_of<PushClip>() == 2); // the header's rounded top and the internal VirtualList's body clip
}

TEST_CASE("The table's border is drawn inside its bounds, with the header and rows within it", "[data-grid][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(50));
    grid.set_row_height(20.0f);
    (void)grid.measure({320, 200}, m, theme);
    grid.arrange({0, 0, 320, 200});
    const Rect bounds = grid.bounds();

    TestRenderer r;
    grid.render(r, theme);

    // The last stroke is the border: its center line half its thickness inside every edge.
    const StrokeRoundedRect* border = nullptr;
    for (const auto& cmd : r.commands)
    {
        if (const auto* s = std::get_if<StrokeRoundedRect>(&cmd); s != nullptr && s->color == theme.palette.tabular_grid_line)
        {
            border = s;
        }
    }
    REQUIRE(border != nullptr);
    CHECK(border->bounds == bounds.inset(Thickness{Border * 0.5f}));
    CHECK(border->stroke_width == Border);
    CHECK(border->radius == theme.corner_radius - Border * 0.5f);

    // Nothing else starts left of the border: the header's fills and the first column's text.
    for (const auto& cmd : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&cmd))
        {
            CHECK(f->bounds.left() >= bounds.left() + Border);
        }
        if (const auto* t = std::get_if<DrawTextRun>(&cmd))
        {
            CHECK(t->bounds.left() >= bounds.left() + Border);
        }
    }
}

TEST_CASE("Header renders each column's label", "[data-grid][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(5));
    (void)grid.measure({320, 200}, m, theme);
    grid.arrange({0, 0, 320, 200});

    TestRenderer r;
    grid.render(r, theme);

    int header_labels = 0;
    for (const auto& cmd : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&cmd))
        {
            if (t->text == "Name" || t->text == "Age" || t->text == "City")
            {
                ++header_labels;
            }
        }
    }
    REQUIRE(header_labels == 3);
}

TEST_CASE("Rows render their cell text per column", "[data-grid][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(5));
    grid.set_row_height(20.0f);
    (void)grid.measure({320, 100}, m, theme);
    grid.arrange({0, 0, 320, 100});

    TestRenderer r;
    grid.render(r, theme);

    bool found_name0 = false;
    for (const auto& cmd : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&cmd))
        {
            if (t->text == "Name 0")
            {
                found_name0 = true;
            }
        }
    }
    REQUIRE(found_name0);
}

TEST_CASE("A row with fewer cells than columns renders blank trailing cells rather than crashing", "[data-grid]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows({{"OnlyName"}}); // missing Age/City cells
    (void)grid.measure({300, 100}, m, theme);
    grid.arrange({0, 0, 300, 100});

    TestRenderer r;
    REQUIRE_NOTHROW(grid.render(r, theme));
}

TEST_CASE("set_rows selects nothing, in every selection mode", "[data-grid][selection]")
{
    for (const SelectionMode mode : {SelectionMode::Single, SelectionMode::Multiple, SelectionMode::Extended})
    {
        TableViewControl grid;
        grid.set_selection_mode(mode);
        grid.set_columns(make_columns());
        int calls = 0;
        grid.on_selection_changed([&](std::optional<std::size_t>) { ++calls; });
        grid.set_rows(make_rows(10));
        REQUIRE(grid.selected_row() == std::nullopt);
        REQUIRE(grid.selected_rows().empty());
        REQUIRE(calls == 0); // nothing was selected, so nothing changed
    }
}

TEST_CASE("set_rows clears a selection and reports it once", "[data-grid][selection]")
{
    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(10));
    grid.set_selected_row(3);

    std::vector<std::optional<std::size_t>> seen;
    grid.on_selection_changed([&](std::optional<std::size_t> row) { seen.push_back(row); });
    grid.set_rows(make_rows(10));
    REQUIRE(grid.selected_row() == std::nullopt);
    REQUIRE(seen == std::vector<std::optional<std::size_t>>{std::nullopt});

    grid.set_rows({});
    REQUIRE(seen.size() == 1); // already empty
}

TEST_CASE("Unset clears the selection under Single too", "[data-grid][selection]")
{
    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(5));
    grid.set_selected_row(2);
    grid.set_selected_row(std::nullopt);
    REQUIRE(grid.selected_row() == std::nullopt);

    grid.set_selected_rows({4});
    grid.set_selected_rows({});
    REQUIRE(grid.selected_rows().empty());

    // Switching to Single keeps what there is and adds nothing.
    grid.set_selection_mode(SelectionMode::Multiple);
    grid.set_selection_mode(SelectionMode::Single);
    REQUIRE(grid.selected_rows().empty());
}

TEST_CASE("The TableView spec selects nothing until .selected_row says so", "[data-grid][spec][selection]")
{
    ui::Control<ui::TableView<std::string>> plain;
    std::unique_ptr<Element>                a = ui::build(ui::TableView<std::string>{
        .items   = {"a", "b"},
        .columns = {{.header = "Name", .width = 100.0f, .text = [](const std::string& s) { return s; }}},
        .control = plain,
    });
    REQUIRE(plain->selected_row() == std::nullopt);

    int                                     calls = 0;
    ui::Control<ui::TableView<std::string>> first;
    std::unique_ptr<Element>                b = ui::build(ui::TableView<std::string>{
        .items                = {"a", "b"},
        .columns              = {{.header = "Name", .width = 100.0f, .text = [](const std::string& s) { return s; }}},
        .selected_row         = 0,
        .on_selection_changed = [&] { ++calls; },
        .control              = first,
    });
    REQUIRE(first->selected_row() == 0);
    REQUIRE(calls == 0); // seeding a value raises no event
}

TEST_CASE("A TableView spec keeps the selection through an edit and clears it on a Reset",
          "[data-grid][spec][selection]")
{
    ui::ObservableVector<std::string>        items{"a", "b", "c"};
    ui::Property<std::optional<std::size_t>> selected{std::size_t{1}};
    std::vector<std::string>                 seen;
    ui::Control<ui::TableView<std::string>>  table;
    std::unique_ptr<Element>                 node = ui::build(ui::TableView<std::string>{
        .items                = ui::bind(items),
        .columns              = {{.header = "Name", .width = 100.0f, .text = [](const std::string& s) { return s; }}},
        .selected_row         = ui::bind(selected),
        .on_selection_changed = [&](std::optional<std::string> s) { seen.push_back(s ? *s : "(none)"); },
        .control              = table,
    });
    REQUIRE(table->selected_row() == 1);

    // "b" moves to row 2. The bound property follows; the handler reports the user's changes only.
    items.insert(0, "z");
    REQUIRE(table->selected_row() == 2);
    REQUIRE(selected.get() == 2);
    REQUIRE(seen.empty());

    items.assign({"x", "y"}); // a Reset replaces every item
    REQUIRE(table->selected_row() == std::nullopt);
    REQUIRE(selected.get() == std::nullopt);
    REQUIRE(seen.empty());
}

TEST_CASE("set_selected_row clamps and fires on_selection_changed only on an actual change", "[data-grid]")
{
    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(10));
    grid.set_selected_row(0);

    int calls = 0;
    grid.on_selection_changed([&](std::optional<std::size_t>) { ++calls; });

    grid.set_selected_row(0); // already selected
    REQUIRE(calls == 0);

    grid.set_selected_row(5);
    REQUIRE(calls == 1);
    REQUIRE(grid.selected_row() == 5);

    grid.set_selected_row(1000);
    REQUIRE(grid.selected_row() == 9);
}

TEST_CASE("Arrow keys move the row selection and scroll it into view", "[data-grid][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(100));
    grid.set_selected_row(0);
    grid.set_row_height(20.0f);
    (void)grid.measure({320, 100}, m, theme); // header 40 + 60px body -> ~3 visible rows
    grid.arrange({0, 0, 320, 100});
    ui::testing::set_focused(grid, true);

    grid.handle_key(key_down(Key::ArrowUp)); // from the first row onto the column headers
    REQUIRE(grid.selected_row() == 0);
    REQUIRE(grid.header_cursor() == 0);
    grid.handle_key(key_down(Key::ArrowDown)); // back to the first row
    REQUIRE(grid.header_cursor() == std::nullopt);
    REQUIRE(grid.selected_row() == 0);

    grid.handle_key(key_down(Key::End));
    REQUIRE(grid.selected_row() == 99);
    REQUIRE(grid.scroll_offset() > 0.0f); // had to scroll to bring row 99 into view

    grid.handle_key(key_down(Key::Home));
    REQUIRE(grid.selected_row() == 0);
    REQUIRE_THAT(grid.scroll_offset(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Clicking a realized row selects it", "[data-grid][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(20));
    grid.set_row_height(20.0f);
    (void)grid.measure({320, 140}, m, theme); // header 40 + 100px body
    grid.arrange({0, 0, 320, 140});

    int calls = 0;
    grid.on_selection_changed([&](std::optional<std::size_t>) { ++calls; });

    // Body starts at y=40; row 1 occupies [60, 80).
    const Point p{10.0f, 65.0f};
    grid.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    grid.handle_pointer({p, PointerButton::Left, PointerAction::Up});

    REQUIRE(grid.selected_row() == 1);
    REQUIRE(calls == 1);
}

TEST_CASE("Moving the pointer over a row repaints it hovered, and off it again", "[data-grid][input][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(20));
    grid.set_row_height(20.0f);
    (void)grid.measure({320, 140}, m, theme);
    grid.arrange({0, 0, 320, 140});
    const auto fills = [&]
    {
        TestRenderer r;
        grid.render(r, theme);
        return r.count_of<FillRoundedRect>();
    };
    const std::size_t resting = fills();

    // Row 1, [60, 80), is not selected; no button is down.
    ui::testing::clear_dirty(grid);
    grid.handle_pointer({{10.0f, 65.0f}, PointerButton::None, PointerAction::Move});
    REQUIRE(grid.is_dirty(DirtyFlags::Visual));
    REQUIRE(fills() == resting + 1);

    ui::testing::clear_dirty(grid);
    grid.handle_pointer({{10.0f, 68.0f}, PointerButton::None, PointerAction::Move}); // still on it
    REQUIRE_FALSE(grid.is_dirty(DirtyFlags::Visual));

    grid.handle_pointer({{10.0f, 125.0f}, PointerButton::None, PointerAction::Move}); // row 4
    REQUIRE(grid.is_dirty(DirtyFlags::Visual));
    REQUIRE(fills() == resting + 1);

    ui::testing::clear_dirty(grid);
    grid.handle_pointer({{10.0f, 125.0f}, PointerButton::None, PointerAction::Leave});
    REQUIRE(grid.is_dirty(DirtyFlags::Visual));
    REQUIRE(fills() == resting);
}

TEST_CASE("A TableView with no rows is not focusable", "[data-grid][focus]")
{
    TableViewControl grid;
    grid.set_columns(make_columns());
    REQUIRE_FALSE(grid.focusable());

    grid.set_rows(make_rows(3));
    REQUIRE(grid.focusable());
}

TEST_CASE("Default row height (TableViewRowMinHeight, 40) is honored without an explicit set_row_height call",
          "[data-grid][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(5));

    const Size desired = grid.measure({300, Unbounded}, m, theme);
    REQUIRE_THAT(desired.height, WithinAbs(40.0 + 5.0 * 40.0 + 2.0 * Border, 1e-3));
}

TEST_CASE(
    "A selected row draws a subtle fill and an accent border, with a bottom gridline "
    "under every row",
    "[data-grid][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    REQUIRE(ui::detail::controls(theme).list_item_subtle_selection);

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(2));
    grid.set_selected_row(0);
    grid.set_row_height(20.0f);
    // Tall enough that nothing scrolls; the overlay scrollbar would add its own FillRoundedRect.
    (void)grid.measure({320, 200}, m, theme);
    grid.arrange({0, 0, 320, 200});

    TestRenderer r;
    grid.render(r, theme);

    REQUIRE(r.count_of<FillRoundedRect>() >= 1);   // at least row 0's subtle background chip
    REQUIRE(r.count_of<StrokeRoundedRect>() == 2); // row 0's selection border and the table's
    REQUIRE(selection_strokes(r, theme) == 1);
    REQUIRE(r.count_of<StrokeLine>() >= 3); // header separator + one per row
}

namespace
{
std::vector<std::string> drawn_text(const TestRenderer& r)
{
    std::vector<std::string> texts;
    for (const auto& cmd : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&cmd))
        {
            texts.push_back(t->text);
        }
    }
    return texts;
}

bool contains(const std::vector<std::string>& texts, const std::string& text)
{
    return std::ranges::find(texts, text) != texts.end();
}
} // namespace

TEST_CASE("A header too wide for its column is trimmed with an ellipsis, one that fits is drawn whole",
          "[data-grid][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    // 60 - 2 * 8 header padding leaves 44 DIPs: four 7.7-DIP characters and the ellipsis.
    grid.set_columns({{.header = "Department", .width = 60.0f}, {.header = "Name", .width = 100.0f}});
    grid.set_rows({{"A", "B"}});
    (void)grid.measure({300, 200}, m, theme);
    grid.arrange({0, 0, 300, 200});

    TestRenderer r;
    grid.render(r, theme);

    const auto texts = drawn_text(r);
    REQUIRE(contains(texts, "Depa…"));
    REQUIRE_FALSE(contains(texts, "Department"));
    REQUIRE(contains(texts, "Name"));
}

TEST_CASE("A header is trimmed again when the columns change", "[data-grid][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    grid.set_columns({{.header = "Department", .width = 60.0f}});
    grid.set_rows({{"A"}});
    (void)grid.measure({300, 200}, m, theme);
    grid.set_columns({{.header = "Department", .width = 200.0f}});
    (void)grid.measure({300, 200}, m, theme);
    grid.arrange({0, 0, 300, 200});

    TestRenderer r;
    grid.render(r, theme);
    REQUIRE(contains(drawn_text(r), "Department"));
}

TEST_CASE("A cell too wide for its column is trimmed with an ellipsis", "[data-grid][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TableViewControl grid;
    // 60 - 2 * 8 cell padding leaves 44 DIPs: four characters and the ellipsis.
    grid.set_columns({{.header = "Team", .width = 60.0f}, {.header = "Name", .width = 100.0f}});
    grid.set_rows({{"Engineering", "Ada"}});
    (void)grid.measure({300, 200}, m, theme);
    grid.arrange({0, 0, 300, 200});

    TestRenderer r;
    grid.render(r, theme);

    const auto texts = drawn_text(r);
    REQUIRE(contains(texts, "Engi…"));
    REQUIRE(contains(texts, "Ada"));
}

TEST_CASE("A disabled grid draws its rows in the disabled text color and its selection border in the disabled color",
          "[data-grid][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    REQUIRE(ui::detail::controls(theme).list_item_subtle_selection);

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(2));
    grid.set_selected_row(0);
    grid.set_row_height(20.0f);
    grid.set_enabled(false);
    (void)grid.measure({320, 200}, m, theme);
    grid.arrange({0, 0, 320, 200});

    TestRenderer r;
    grid.render(r, theme);

    int cells = 0;
    for (const auto& cmd : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&cmd); t != nullptr && t->text.starts_with("Name "))
        {
            REQUIRE(t->color == theme.colors().text_disabled);
            ++cells;
        }
        if (const auto* s = std::get_if<StrokeRoundedRect>(&cmd); s != nullptr && s->color != theme.palette.tabular_grid_line)
        {
            REQUIRE(s->color == theme.palette.text_fill_disabled);
        }
        if (const auto* f = std::get_if<FillRoundedRect>(&cmd))
        {
            // Only the header strip is filled: no selected-row backplate.
            REQUIRE(f->color == theme.palette.tabular_header_background);
        }
    }
    REQUIRE(cells == 2);
    REQUIRE(selection_strokes(r, theme) == 1);
}

TEST_CASE(
    "A disabled grid under a filled-selection theme marks the selection with a disabled-color border instead of the "
    "fill",
    "[data-grid][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme                                  = Theme::light();
    ui::detail::edit_controls(theme).list_item_subtle_selection = false; // the high-contrast and era row model

    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(2));
    grid.set_selected_row(0);
    grid.set_row_height(20.0f);
    (void)grid.measure({320, 200}, m, theme);
    grid.arrange({0, 0, 320, 200});

    {
        TestRenderer r;
        grid.render(r, theme);
        bool filled = false;
        for (const auto& cmd : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&cmd); f != nullptr && f->color == theme.colors().selection)
            {
                filled = true;
            }
            if (const auto* t = std::get_if<DrawTextRun>(&cmd); t != nullptr && t->text == "Name 0")
            {
                REQUIRE(t->color == theme.colors().selection_text);
            }
        }
        REQUIRE(filled);
    }

    grid.set_enabled(false);
    TestRenderer r;
    grid.render(r, theme);
    for (const auto& cmd : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&cmd))
        {
            REQUIRE(f->color != theme.colors().selection);
        }
        if (const auto* t = std::get_if<DrawTextRun>(&cmd); t != nullptr && t->text.starts_with("Name "))
        {
            REQUIRE(t->color == theme.colors().text_disabled);
        }
    }
    REQUIRE(selection_strokes(r, theme) == 1);
}

// ---- columns: widths, resizing ----

namespace
{
// Lays the grid out in a `width` x `height` slot and returns the rect inside its border, where the
// header and the rows go (the bounds are inset by the focus ring).
Rect lay_out(TableViewControl& grid, float width, float height)
{
    static HeadlessTextMeasurer m;
    static const Theme          theme = Theme::light();
    (void)grid.measure({width, height}, m, theme);
    grid.arrange({0, 0, width, height});
    return grid.bounds().inset(Thickness{Border});
}

TestRenderer render(const TableViewControl& grid)
{
    static const Theme theme = Theme::light();
    TestRenderer       r;
    ui::testing::render_with_focus_ring(grid, r, theme);
    return r;
}

void click(TableViewControl& grid, Point p, bool shift = false, bool ctrl = false)
{
    grid.handle_pointer({p, PointerButton::Left, PointerAction::Down, 0.0f, {.ctrl = ctrl, .shift = shift}});
    grid.handle_pointer({p, PointerButton::Left, PointerAction::Up, 0.0f, {.ctrl = ctrl, .shift = shift}});
}

// The middle of body row `position` (in view order), for a grid with `row_height` rows.
Point row_point(const Rect& bounds, std::size_t position, float row_height)
{
    return {bounds.left() + 10.0f, bounds.top() + 40.0f + (static_cast<float>(position) + 0.5f) * row_height};
}

// The middle of column `column`'s header.
Point header_point(const TableViewControl& grid, std::size_t column)
{
    float left = grid.bounds().left();
    for (std::size_t c = 0; c < column; ++c)
    {
        left += grid.column_actual_width(c);
    }
    return {left + grid.column_actual_width(column) * 0.5f, grid.bounds().top() + 20.0f};
}

// Just inside column `column`'s trailing divider.
Point divider_point(const TableViewControl& grid, std::size_t column)
{
    float right = grid.bounds().left();
    for (std::size_t c = 0; c <= column; ++c)
    {
        right += grid.column_actual_width(c);
    }
    return {right - 2.0f, grid.bounds().top() + 20.0f};
}

std::vector<std::string> column_text(const TableViewControl& grid, std::size_t column)
{
    std::vector<std::string> out;
    for (std::size_t p = 0; p < grid.rows().size(); ++p)
    {
        out.push_back(grid.rows()[grid.row_at_position(p).value()][column]);
    }
    return out;
}

const Theme& light()
{
    static const Theme theme = Theme::light();
    return theme;
}
} // namespace

TEST_CASE("Star columns share what the fixed and Auto columns leave, by weight and within their min and max",
          "[data-grid][layout]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Id", .width = 100.0f},
                      {.header = "Name", .width = GridLength::fraction(1.0f)},
                      {.header = "Notes", .width = GridLength::fraction(3.0f)},
                      {.header = "Tiny", .width = GridLength::fraction(1.0f), .max_width = 30.0f}});
    grid.set_rows(make_rows(3));
    const Rect bounds = lay_out(grid, 520.0f, 300.0f);

    // 100 fixed; Tiny clamps at 30; the other 1* and 3* split the rest, 1:3.
    const float rest = bounds.width - 100.0f - 30.0f;
    REQUIRE_THAT(grid.column_actual_width(0), WithinAbs(100.0, 1e-3));
    REQUIRE_THAT(grid.column_actual_width(3), WithinAbs(30.0, 1e-3));
    REQUIRE_THAT(grid.column_actual_width(1), WithinAbs(rest * 0.25, 1e-3));
    REQUIRE_THAT(grid.column_actual_width(2), WithinAbs(rest * 0.75, 1e-3));
}

TEST_CASE("An Auto column is as wide as its header or its widest cell in view, within its min width",
          "[data-grid][layout]")
{
    HeadlessTextMeasurer m;
    TableViewControl     grid;
    grid.set_columns({{.header = "Id", .width = GridLength::auto_()},
                      {.header = "X", .width = GridLength::auto_(), .min_width = 60.0f}});
    grid.set_rows({{"1", "a"}, {"A much longer cell", "b"}});
    (void)lay_out(grid, 600.0f, 300.0f);

    TextStyle   style = light().text_style;
    const float cell  = m.measure("A much longer cell", style).width + 16.0f; // 8 DIP padding either side
    REQUIRE_THAT(grid.column_actual_width(0), WithinAbs(std::ceil(cell), 1.0));
    REQUIRE_THAT(grid.column_actual_width(1), WithinAbs(60.0, 1e-3));
}

TEST_CASE("Dragging a header divider resizes the column within its min and max and reports each step",
          "[data-grid][input]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f, .min_width = 50.0f, .max_width = 180.0f},
                      {.header = "Age", .width = 60.0f}});
    grid.set_rows(make_rows(3));
    (void)lay_out(grid, 400.0f, 300.0f);

    std::vector<float> reported;
    grid.on_column_resized(
        [&](const TableViewColumnResizedArgs& args)
        {
            REQUIRE(args.column == 0);
            reported.push_back(args.width);
        });

    const Point start = divider_point(grid, 0);
    REQUIRE(grid.cursor_at(start) == CursorShape::SizeWestEast);
    REQUIRE_FALSE(grid.cursor_at(header_point(grid, 0)).has_value());

    grid.handle_pointer({start, PointerButton::Left, PointerAction::Down});
    grid.handle_pointer({{start.x + 30.0f, start.y}, PointerButton::None, PointerAction::Move});
    grid.handle_pointer({{start.x + 500.0f, start.y}, PointerButton::None, PointerAction::Move});
    grid.handle_pointer({{start.x + 500.0f, start.y}, PointerButton::Left, PointerAction::Up});

    REQUIRE(reported == std::vector<float>{130.0f, 180.0f});
    REQUIRE(grid.columns()[0].width == TableViewColumnWidth{180.0f});
    REQUIRE(grid.sort_column() == std::nullopt); // the drag did not also sort

    (void)lay_out(grid, 400.0f, 300.0f);
    const Point again = divider_point(grid, 0);
    grid.handle_pointer({again, PointerButton::Left, PointerAction::Down});
    grid.handle_pointer({{again.x - 400.0f, again.y}, PointerButton::None, PointerAction::Move});
    grid.handle_pointer({{again.x - 400.0f, again.y}, PointerButton::Left, PointerAction::Up});
    REQUIRE(reported.back() == 50.0f);
}

TEST_CASE("A column that cannot resize, or a table with resizing off, shows no divider", "[data-grid][input]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f, .can_resize = false}, {.header = "Age", .width = 60.0f}});
    grid.set_rows(make_rows(3));
    (void)lay_out(grid, 400.0f, 300.0f);

    REQUIRE_FALSE(grid.cursor_at(divider_point(grid, 0)).has_value());
    REQUIRE(grid.cursor_at(divider_point(grid, 1)) == CursorShape::SizeWestEast);

    grid.set_can_user_resize_columns(false);
    REQUIRE_FALSE(grid.cursor_at(divider_point(grid, 1)).has_value());
}

// ---- sorting ----

TEST_CASE("Header clicks walk a column's sort cycle, sorting the view and not rows()", "[data-grid][sort]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f},
                      {.header = "Age", .width = 80.0f, .sort_cycle = TableViewSortCycle::AscendingDescendingNone}});
    grid.set_rows({{"Grace", "85"}, {"Ada", "36"}, {"Alan", "100"}});
    (void)lay_out(grid, 400.0f, 300.0f);

    std::vector<TableViewSortDirection> sorted;
    grid.on_sorted([&](const TableViewSortedArgs& args) { sorted.push_back(args.direction); });

    click(grid, header_point(grid, 1));
    REQUIRE(grid.sort_column() == 1);
    REQUIRE(grid.sort_direction() == TableViewSortDirection::Ascending);
    // Numbers compare as numbers: 36 < 85 < 100.
    REQUIRE(column_text(grid, 1) == std::vector<std::string>{"36", "85", "100"});
    REQUIRE(grid.rows()[0][0] == "Grace"); // the source order is untouched

    click(grid, header_point(grid, 1));
    REQUIRE(column_text(grid, 1) == std::vector<std::string>{"100", "85", "36"});

    click(grid, header_point(grid, 1));
    REQUIRE(grid.sort_column() == std::nullopt);
    REQUIRE(column_text(grid, 1) == std::vector<std::string>{"85", "36", "100"});
    REQUIRE(sorted == std::vector<TableViewSortDirection>{TableViewSortDirection::Ascending,
                                                          TableViewSortDirection::Descending,
                                                          TableViewSortDirection::None});

    // The default cycle never returns to unsorted.
    click(grid, header_point(grid, 0));
    click(grid, header_point(grid, 0));
    click(grid, header_point(grid, 0));
    REQUIRE(grid.sort_direction() == TableViewSortDirection::Ascending);
    REQUIRE(column_text(grid, 0) == std::vector<std::string>{"Ada", "Alan", "Grace"});
}

TEST_CASE("A sort keeps the selected row selected and draws a chevron in the sorted header", "[data-grid][sort][render]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}, {.header = "Age", .width = 80.0f}});
    grid.set_rows({{"Grace", "85"}, {"Ada", "36"}, {"Alan", "100"}});
    grid.set_selected_row(0); // Grace
    (void)lay_out(grid, 400.0f, 300.0f);

    REQUIRE(grid.sort_by_column(1, TableViewSortDirection::Descending));
    REQUIRE_FALSE(grid.sort_by_column(1, TableViewSortDirection::Descending)); // no change
    REQUIRE(grid.selected_row() == 0);
    REQUIRE(grid.position_of_row(0) == 1); // Grace (85) is second after 100
    (void)lay_out(grid, 400.0f, 300.0f);

    const std::string down = "\xEE\xA5\xAE"; // U+E96E, the down chevron
    const std::string up   = "\xEE\xA5\xAD"; // U+E96D
    REQUIRE(contains(drawn_text(render(grid)), down));
    REQUIRE_FALSE(contains(drawn_text(render(grid)), up));

    REQUIRE(grid.clear_sort());
    REQUIRE_FALSE(contains(drawn_text(render(grid)), down));
}

TEST_CASE("A column that cannot sort, or a table with sorting off, ignores header clicks", "[data-grid][sort]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f, .can_sort = false}, {.header = "Age", .width = 80.0f}});
    grid.set_rows({{"b", "2"}, {"a", "1"}});
    (void)lay_out(grid, 400.0f, 300.0f);

    click(grid, header_point(grid, 0));
    REQUIRE(grid.sort_column() == std::nullopt);

    grid.set_can_user_sort_columns(false);
    click(grid, header_point(grid, 1));
    REQUIRE(grid.sort_column() == std::nullopt);

    // Programmatic sorting still works.
    REQUIRE(grid.sort_by_column(0, TableViewSortDirection::Ascending));
    REQUIRE(column_text(grid, 0) == std::vector<std::string>{"a", "b"});
}

TEST_CASE("A sort comparer orders the rows; ties keep their source order", "[data-grid][sort]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}});
    grid.set_rows({{"bb"}, {"a"}, {"cc"}, {"d"}});
    // By length only.
    grid.set_sort_comparer([&grid](std::size_t, std::size_t a, std::size_t b)
                           { return grid.rows()[a][0].size() <=> grid.rows()[b][0].size(); });
    grid.sort_by_column(0, TableViewSortDirection::Ascending);
    REQUIRE(column_text(grid, 0) == std::vector<std::string>{"a", "d", "bb", "cc"});
    grid.sort_by_column(0, TableViewSortDirection::Descending);
    REQUIRE(column_text(grid, 0) == std::vector<std::string>{"bb", "cc", "a", "d"});
}

TEST_CASE("compare_text compares numbers as numbers and text ignoring case", "[data-grid][sort]")
{
    REQUIRE(std::is_lt(TableViewControl::compare_text("9", "10")));
    REQUIRE(std::is_gt(TableViewControl::compare_text("1.5", "-2")));
    REQUIRE(std::is_lt(TableViewControl::compare_text("alpha", "Beta")));
    REQUIRE(std::is_lt(TableViewControl::compare_text("Item 10", "Item 9"))); // text, not numbers
    REQUIRE(std::is_eq(TableViewControl::compare_text("same", "same")));
}

// ---- horizontal scrolling and frozen columns ----

TEST_CASE("Columns wider than the table scroll sideways, and a frozen leading column stays put", "[data-grid][scroll]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Id", .width = 60.0f, .frozen = true},
                      {.header = "Name", .width = 200.0f},
                      {.header = "City", .width = 200.0f}});
    grid.set_rows({{"1", "Ada", "London"}});
    const Rect bounds = lay_out(grid, 300.0f, 200.0f);
    REQUIRE(bounds.width < 460.0f);

    const auto text_x = [&](const std::string& text)
    {
        for (const auto& cmd : render(grid).commands)
        {
            if (const auto* t = std::get_if<DrawTextRun>(&cmd); t != nullptr && t->text == text)
            {
                return t->bounds.x;
            }
        }
        return -1.0f;
    };
    const float id_x   = text_x("1");
    const float name_x = text_x("Ada");

    PointerEvent wheel{{bounds.left() + 100.0f, bounds.top() + 60.0f}, PointerButton::None, PointerAction::Wheel, 50.0f};
    wheel.horizontal_wheel = true;
    REQUIRE(grid.handle_wheel(wheel));
    REQUIRE_THAT(grid.horizontal_offset(), WithinAbs(50.0, 1e-3));
    REQUIRE_THAT(text_x("1"), WithinAbs(id_x, 1e-3));
    REQUIRE_THAT(text_x("Ada"), WithinAbs(name_x - 50.0, 1e-3));

    // Clamped at the overflow; a turn past it is left to whatever scrolls around the table.
    grid.set_horizontal_offset(10000.0f);
    REQUIRE_THAT(grid.horizontal_offset(), WithinAbs(460.0 - bounds.width, 1e-3));
    REQUIRE_FALSE(grid.handle_wheel(wheel));

    // The scrolling cells are clipped right of the frozen band.
    bool clipped = false;
    for (const auto& cmd : render(grid).commands)
    {
        if (const auto* c = std::get_if<PushClip>(&cmd);
            c != nullptr && std::abs(c->bounds.x - (bounds.left() + 60.0f)) < 1e-3f)
        {
            clipped = true;
        }
    }
    REQUIRE(clipped);
}

TEST_CASE("A frozen trailing column stays against the right edge while the others scroll", "[data-grid][scroll]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Id", .width = 60.0f, .frozen = true},
                      {.header = "Name", .width = 200.0f},
                      {.header = "City", .width = 200.0f},
                      {.header = "Total", .width = 80.0f, .frozen_trailing = true}});
    grid.set_rows({{"1", "Ada", "London", "42"}});
    const Rect bounds = lay_out(grid, 300.0f, 200.0f);
    REQUIRE(bounds.width < 540.0f);

    const auto text_x = [&](const std::string& text)
    {
        for (const auto& cmd : render(grid).commands)
        {
            if (const auto* t = std::get_if<DrawTextRun>(&cmd); t != nullptr && t->text == text)
            {
                return t->bounds.x;
            }
        }
        return -1.0f;
    };
    // Pinned against the right edge from the start, and not moved by a scroll.
    const float total_x = text_x("42");
    REQUIRE_THAT(total_x, WithinAbs(bounds.right() - 80.0f + 8.0f, 1e-3)); // 8 DIP cell padding
    const float name_x = text_x("Ada");
    grid.set_horizontal_offset(50.0f);
    REQUIRE_THAT(text_x("42"), WithinAbs(total_x, 1e-3));
    REQUIRE_THAT(text_x("Ada"), WithinAbs(name_x - 50.0, 1e-3));
    REQUIRE_THAT(text_x("1"), WithinAbs(bounds.left() + 8.0f, 1e-3));

    // The scrolling cells are clipped to the band between the two frozen ones.
    bool clipped = false;
    for (const auto& cmd : render(grid).commands)
    {
        if (const auto* c = std::get_if<PushClip>(&cmd);
            c != nullptr && std::abs(c->bounds.x - (bounds.left() + 60.0f)) < 1e-3f &&
            std::abs(c->bounds.right() - (bounds.right() - 80.0f)) < 1e-3f)
        {
            clipped = true;
        }
    }
    REQUIRE(clipped);

    // The right edge is the trailing column's divider, not one scrolled under it.
    REQUIRE(grid.cursor_at({bounds.right() - 2.0f, bounds.top() + 20.0f}) == CursorShape::SizeWestEast);

    // Moving the current column onto the City column scrolls it clear of the trailing band.
    grid.set_read_only(false);
    grid.set_current_column(2);
    REQUIRE_THAT(grid.horizontal_offset(), WithinAbs(60.0f + 400.0f + 80.0f - bounds.width, 1e-3));

    // Only a trailing run is pinned.
    grid.set_column_frozen_trailing(3, false);
    grid.set_column_frozen_trailing(2, true);
    grid.set_horizontal_offset(0.0f);
    REQUIRE_THAT(text_x("London"), WithinAbs(bounds.left() + 260.0f + 8.0f, 1e-3));
}

TEST_CASE("Resizing a column fixes the star columns before it, so its divider follows the drag", "[data-grid][input]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = GridLength::fraction(1.0f)},
                      {.header = "Role", .width = 100.0f},
                      {.header = "Notes", .width = GridLength::fraction(1.0f)}});
    grid.set_rows(make_rows(3));
    const Rect  bounds = lay_out(grid, 400.0f, 300.0f);
    const float name   = grid.column_actual_width(0);
    REQUIRE_THAT(name, WithinAbs((bounds.width - 100.0f) * 0.5f, 1e-3));

    const Point start = divider_point(grid, 1);
    grid.handle_pointer({start, PointerButton::Left, PointerAction::Down});
    grid.handle_pointer({{start.x + 40.0f, start.y}, PointerButton::None, PointerAction::Move});
    grid.handle_pointer({{start.x + 40.0f, start.y}, PointerButton::Left, PointerAction::Up});
    (void)lay_out(grid, 400.0f, 300.0f);

    // The star column before it keeps its width; the one after it gives up the 40 DIPs.
    REQUIRE(grid.columns()[0].width == TableViewColumnWidth{name});
    REQUIRE(grid.columns()[2].width.unit == GridUnit::Fraction);
    REQUIRE_THAT(grid.column_actual_width(1), WithinAbs(140.0, 1e-3));
    REQUIRE_THAT(divider_point(grid, 1).x, WithinAbs(start.x + 40.0f, 1e-3));
    REQUIRE_THAT(grid.column_actual_width(2), WithinAbs(bounds.width - name - 140.0f, 1e-3));
}

TEST_CASE("Left and Right scroll the columns sideways only when they overflow", "[data-grid][scroll][input]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}});
    grid.set_rows(make_rows(3));
    (void)lay_out(grid, 300.0f, 200.0f);
    ui::testing::set_focused(grid, true);
    REQUIRE_FALSE(grid.handle_key(key_down(Key::ArrowRight)));

    grid.set_columns({{.header = "Name", .width = 400.0f}, {.header = "Age", .width = 400.0f}});
    (void)lay_out(grid, 300.0f, 200.0f);
    REQUIRE(grid.handle_key(key_down(Key::ArrowRight)));
    REQUIRE(grid.horizontal_offset() > 0.0f);
    grid.handle_key(key_down(Key::ArrowLeft));
    REQUIRE_THAT(grid.horizontal_offset(), WithinAbs(0.0, 1e-3));
}

// ---- selection modes ----

TEST_CASE("SelectionMode None selects nothing, by click or key", "[data-grid][selection]")
{
    TableViewControl grid;
    grid.set_selection_mode(SelectionMode::None);
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(5));
    REQUIRE(grid.selected_row() == std::nullopt);
    const Rect bounds = lay_out(grid, 320.0f, 300.0f);
    ui::testing::set_focused(grid, true);

    int calls = 0;
    grid.on_selection_changed([&](std::optional<std::size_t>) { ++calls; });
    click(grid, row_point(bounds, 2, 40.0f));
    grid.handle_key(key_down(Key::ArrowDown));
    grid.handle_key(key_down(Key::Space));
    grid.set_selected_row(1);
    REQUIRE(grid.selected_rows().empty());
    REQUIRE(calls == 0);
}

TEST_CASE("SelectionMode Multiple toggles rows by click and Space and adds a range with Shift", "[data-grid][selection]")
{
    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(6));
    grid.set_selected_row(0);
    grid.set_selection_mode(SelectionMode::Multiple);
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{0}); // what Single had stays
    const Rect bounds = lay_out(grid, 320.0f, 400.0f);

    click(grid, row_point(bounds, 0, 40.0f)); // toggles row 0 off
    click(grid, row_point(bounds, 1, 40.0f));
    click(grid, row_point(bounds, 3, 40.0f));
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{1, 3});
    click(grid, row_point(bounds, 5, 40.0f), /*shift=*/true); // anchor 3 is selected: 3..5 added
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{1, 3, 4, 5});
    click(grid, row_point(bounds, 1, 40.0f));
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{3, 4, 5});

    ui::testing::set_focused(grid, true);
    grid.handle_key(key_down(Key::ArrowUp)); // the cursor moves; nothing is selected by it
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{3, 4, 5});
    grid.handle_key(key_down(Key::Space));
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{0, 3, 4, 5});
}

TEST_CASE("SelectionMode Extended selects alone, toggles with Ctrl and ranges with Shift", "[data-grid][selection]")
{
    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(6));
    grid.set_selection_mode(SelectionMode::Extended);
    const Rect bounds = lay_out(grid, 320.0f, 400.0f);

    click(grid, row_point(bounds, 2, 40.0f));
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{2});
    click(grid, row_point(bounds, 4, 40.0f), false, /*ctrl=*/true);
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{2, 4});
    click(grid, row_point(bounds, 1, 40.0f), /*shift=*/true); // anchor 4: 1..4, alone
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{1, 2, 3, 4});
    click(grid, row_point(bounds, 5, 40.0f));
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{5});

    ui::testing::set_focused(grid, true);
    KeyEvent shift_up        = key_down(Key::ArrowUp);
    shift_up.modifiers.shift = true;
    grid.handle_key(shift_up);
    grid.handle_key(shift_up);
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{3, 4, 5});

    grid.set_selection_mode(SelectionMode::Single);
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{3});
}

TEST_CASE("Selection follows the view order when the rows are sorted", "[data-grid][selection][sort]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}});
    grid.set_rows({{"c"}, {"a"}, {"b"}});
    grid.sort_by_column(0, TableViewSortDirection::Ascending);
    grid.set_rows({{"c"}, {"a"}, {"b"}});  // set_rows keeps the sort
    REQUIRE(grid.row_at_position(0) == 1); // "a"
    grid.set_selected_row(1);
    (void)lay_out(grid, 320.0f, 300.0f);
    ui::testing::set_focused(grid, true);
    grid.handle_key(key_down(Key::ArrowDown));
    REQUIRE(grid.selected_row() == 2); // "b", the next one down in view
}

// ---- grid lines, banding, empty state ----

TEST_CASE("Grid lines follow GridLinesVisibility", "[data-grid][render]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}, {.header = "Age", .width = 60.0f}});
    grid.set_rows(make_rows(2));
    grid.set_row_height(20.0f);
    (void)lay_out(grid, 320.0f, 300.0f);

    const auto lines = [&](TableViewGridLinesVisibility v)
    {
        grid.set_grid_lines_visibility(v);
        return render(grid).count_of<StrokeLine>();
    };
    // The header's bottom edge is always drawn.
    REQUIRE(lines(TableViewGridLinesVisibility::None) == 1);
    REQUIRE(lines(TableViewGridLinesVisibility::Horizontal) == 1 + 2);       // one per row
    REQUIRE(lines(TableViewGridLinesVisibility::Vertical) == 1 + 2 + 2 * 2); // header + each row, per column
    REQUIRE(lines(TableViewGridLinesVisibility::All) == 1 + 2 + 2 + 2 * 2);
}

TEST_CASE("Alternating rows band every second row with the tabular token or the given color", "[data-grid][render]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}});
    grid.set_rows(make_rows(4));
    grid.set_row_height(20.0f);
    grid.set_selection_mode(SelectionMode::None);
    (void)lay_out(grid, 320.0f, 300.0f);

    const auto bands = [&](Color color)
    {
        int n = 0;
        for (const auto& cmd : render(grid).commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&cmd); f != nullptr && f->color == color)
            {
                ++n;
            }
        }
        return n;
    };
    REQUIRE(bands(light().palette.tabular_row_alternating) == 0);
    grid.set_alternating_rows(true);
    REQUIRE(bands(light().palette.tabular_row_alternating) == 2);
    grid.set_alternating_row_background([](const Theme&) { return Color::from_argb(0xFFFF0000); });
    REQUIRE(bands(Color::from_argb(0xFFFF0000)) == 2);
}

TEST_CASE("The empty content shows, centered in the body, only while there are no rows", "[data-grid][render]")
{
    TableViewControl grid;
    grid.set_columns(make_columns());
    auto  empty = std::make_unique<TextControl>("Nothing here");
    auto* text  = empty.get();
    grid.set_empty_content(std::move(empty));
    const Rect bounds = lay_out(grid, 320.0f, 300.0f);

    REQUIRE(contains(drawn_text(render(grid)), "Nothing here"));
    const Rect body{bounds.left(), bounds.top() + 40.0f, bounds.width, bounds.height - 40.0f};
    REQUIRE_THAT(text->bounds().center().x, WithinAbs(body.center().x, 0.5));
    REQUIRE_THAT(text->bounds().center().y, WithinAbs(body.center().y, 0.5));

    int children = 0;
    grid.for_each_child([&](Element&) { ++children; });
    REQUIRE(children == 2);

    grid.set_rows(make_rows(2));
    (void)lay_out(grid, 320.0f, 300.0f);
    REQUIRE_FALSE(contains(drawn_text(render(grid)), "Nothing here"));
    children = 0;
    grid.for_each_child([&](Element&) { ++children; });
    REQUIRE(children == 1);
}

TEST_CASE("The header is SemiBold under the tabular row model", "[data-grid][render]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}});
    grid.set_rows({{"Ada"}});
    (void)lay_out(grid, 320.0f, 300.0f);
    for (const auto& cmd : render(grid).commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&cmd); t != nullptr)
        {
            REQUIRE(t->style.weight == (t->text == "Name" ? FontWeight::SemiBold : FontWeight::Regular));
        }
    }
}

// ---- the spec ----

namespace
{
struct Item
{
    std::string name;
    int         size                                 = 0;
    friend bool operator==(const Item&, const Item&) = default;
};
} // namespace

TEST_CASE("The TableView spec sorts with a column's compare, keeps the selected item and binds the selected set",
          "[data-grid][spec]")
{
    using ui::operator""_fr;
    Property<std::vector<Item>>        items{std::vector<Item>{{"b", 30}, {"a", 200}, {"c", 4}}};
    Property<std::vector<std::size_t>> selected{};
    std::vector<std::string>           sorted;
    ui::Control<ui::TableView<Item>>   table;

    std::unique_ptr<Element> node = ui::build(ui::TableView<Item>{
        .items            = ui::bind(items),
        .columns          = {{.header = "Name", .width = 1_fr, .text = [](const Item& i) { return i.name; }},
                             {.header  = "Size",
                              .width   = 80.0f,
                              .text    = [](const Item& i) { return std::to_string(i.size) + " KB"; },
                              .compare = [](const Item& a, const Item& b) { return a.size <=> b.size; }}},
        .selected_indices = ui::bind(selected),
        .selection_mode   = SelectionMode::Multiple,
        .on_sorted        = [&](const TableViewSortedArgs& args)
        { sorted.push_back(args.column ? std::to_string(*args.column) : "none"); },
        .control       = table,
        .empty_content = ui::Text{.text = "Empty"},
    });

    REQUIRE(table->selection_mode() == SelectionMode::Multiple);
    REQUIRE(table->selected_rows().empty());
    REQUIRE(table->columns()[0].width.unit == GridUnit::Fraction);
    REQUIRE(table->empty_content() != nullptr);

    table->set_selected_rows({0, 2});
    REQUIRE(selected.get() == std::vector<std::size_t>{0, 2});
    selected = std::vector<std::size_t>{1};
    REQUIRE(table->selected_rows() == std::vector<std::size_t>{1});

    // "4 KB" < "30 KB" < "200 KB" by size, which the text would not give.
    REQUIRE(table->sort_by_column(1, TableViewSortDirection::Ascending));
    REQUIRE(column_text(*table, 0) == std::vector<std::string>{"c", "b", "a"});
    REQUIRE(sorted == std::vector<std::string>{"1"});

    // A changed collection is sorted again and its selected item stays selected.
    items.update([](std::vector<Item>& v) { v.push_back({"d", 1}); });
    REQUIRE(column_text(*table, 0) == std::vector<std::string>{"d", "c", "b", "a"});
    REQUIRE(table->selected_rows() == std::vector<std::size_t>{1});
}

TEST_CASE("The TableView spec defaults to 40 DIP rows and all grid lines", "[data-grid][spec]")
{
    ui::Control<ui::TableView<std::string>> table;
    std::unique_ptr<Element>                node = ui::build(ui::TableView<std::string>{
        .items   = {"a", "b"},
        .columns = {{.header = "Name", .width = 100.0f, .text = [](const std::string& s) { return s; }}},
        .control = table,
    });
    REQUIRE(table->row_height() == 40.0f);
    REQUIRE(table->grid_lines_visibility() == TableViewGridLinesVisibility::All);
    REQUIRE(table->selection_mode() == SelectionMode::Single);
    REQUIRE(table->selected_row() == std::nullopt);
    REQUIRE_FALSE(table->alternating_rows());
}

TEST_CASE("set_column_frozen pins a column without clearing the sort", "[data-grid][scroll]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Id", .width = 60.0f}, {.header = "Name", .width = 400.0f}});
    grid.set_rows({{"2", "b"}, {"1", "a"}});
    grid.sort_by_column(0, TableViewSortDirection::Ascending);
    (void)lay_out(grid, 300.0f, 200.0f);
    grid.set_horizontal_offset(40.0f);

    const auto id_x = [&]
    {
        for (const auto& cmd : render(grid).commands)
        {
            if (const auto* t = std::get_if<DrawTextRun>(&cmd); t != nullptr && t->text == "1")
            {
                return t->bounds.x;
            }
        }
        return 0.0f;
    };
    const float scrolled = id_x();
    grid.set_column_frozen(0, true);
    REQUIRE_THAT(id_x(), WithinAbs(scrolled + 40.0, 1e-3));
    REQUIRE(grid.sort_column() == 0);
}

// ---- cell templates ----

namespace
{
// The middle of the cell at view position `position`, column `column`, for 40 DIP rows.
Point cell_point(const TableViewControl& grid, std::size_t position, std::size_t column)
{
    float left = grid.bounds().left();
    for (std::size_t c = 0; c < column; ++c)
    {
        left += grid.column_actual_width(c);
    }
    return {left + grid.column_actual_width(column) * 0.5f,
            grid.bounds().top() + 40.0f + (static_cast<float>(position) + 0.5f) * 40.0f};
}

void double_click(TableViewControl& grid, Point p)
{
    grid.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    grid.handle_pointer({p, PointerButton::Left, PointerAction::Up});
    PointerEvent second{p, PointerButton::Left, PointerAction::Down};
    second.click_count = 2;
    grid.handle_pointer(second);
    grid.handle_pointer({p, PointerButton::Left, PointerAction::Up});
}

std::size_t child_count(const Element& e)
{
    std::size_t n = 0;
    e.for_each_child([&](Element&) { ++n; });
    return n;
}
} // namespace

TEST_CASE("A cell factory builds Elements for the cells in view only, drawn instead of their text",
          "[data-grid][template]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}, {.header = "Badge", .width = GridLength::auto_()}});
    grid.set_rows(make_rows(50));
    int built = 0;
    grid.set_cell_factory(
        [&](std::size_t row, std::size_t column) -> std::unique_ptr<Element>
        {
            if (column != 1)
            {
                return nullptr;
            }
            ++built;
            return std::make_unique<TextControl>("Badge for row number " + std::to_string(row));
        });
    (void)lay_out(grid, 400.0f, 200.0f); // 160 DIP of body: rows 0..3 in view

    REQUIRE(grid.cell_element(0, 1) != nullptr);
    REQUIRE(grid.cell_element(0, 0) == nullptr); // a text cell
    REQUIRE(grid.cell_element(30, 1) == nullptr);
    REQUIRE(built <= 5);
    REQUIRE(grid.cell_element(0, 1)->parent() == &grid);

    // An Auto column is as wide as its template content.
    HeadlessTextMeasurer m;
    const float          badge = m.measure("Badge for row number 0", light().text_style).width;
    REQUIRE(grid.column_actual_width(1) >= badge);

    const std::vector<std::string> texts = drawn_text(render(grid));
    REQUIRE(contains(texts, "Badge for row number 0"));
    REQUIRE_FALSE(contains(texts, "20")); // row 0's own text in that column is not drawn
    REQUIRE(contains(texts, "Name 0"));

    // The built Element survives the rows being built again for as long as its row stays in view;
    // scrolling realizes the rows that come into view and lets the others go.
    Element* kept = grid.cell_element(1, 1);
    (void)lay_out(grid, 400.0f, 200.0f);
    REQUIRE(grid.cell_element(1, 1) == kept);
    grid.set_selected_row(30);
    (void)lay_out(grid, 400.0f, 200.0f);
    REQUIRE(grid.cell_element(30, 1) != nullptr);
    REQUIRE(grid.cell_element(0, 1) == nullptr);
    // The list and one cell for each row in view.
    REQUIRE(child_count(grid) >= 1 + 4);
    REQUIRE(child_count(grid) <= 1 + 5);
}

TEST_CASE("Template cells take pointer input and focus, and are automation children", "[data-grid][template]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}, {.header = "Done", .width = 80.0f}});
    grid.set_rows(make_rows(3));
    grid.set_cell_factory([](std::size_t, std::size_t column) -> std::unique_ptr<Element>
                          { return column == 1 ? std::make_unique<CheckBoxControl>("") : nullptr; });
    (void)lay_out(grid, 400.0f, 300.0f);

    auto* box = dynamic_cast<CheckBoxControl*>(grid.cell_element(1, 1));
    REQUIRE(box != nullptr);
    REQUIRE(box->bounds().width > 0.0f);
    const std::optional<std::size_t> before = grid.selected_row();
    REQUIRE(before != 1);
    click(grid, box->bounds().center());
    REQUIRE(box->checked() == true);
    REQUIRE(grid.selected_row() == before);                                // the check box took the click
    click(grid, Point{grid.bounds().x + 50.0f, box->bounds().center().y}); // the row's own cell
    REQUIRE(grid.selected_row() == 1);
    REQUIRE(box->checked() == true);

    std::vector<Element*> stops;
    grid.collect_focusables(stops);
    REQUIRE(stops.size() == 4);
    REQUIRE(stops.front() == &grid);
    REQUIRE(stops[2] == box); // in view order

    // Each check box sits under its cell's automation node.
    Element* cell = ui::automation_providers(grid).grid->grid_item(1, 1);
    REQUIRE(cell != nullptr);
    REQUIRE(automation_children(*cell) == std::vector<Element*>{box});
    REQUIRE(automation_parent(*box) == cell);

    // A disabled table takes no input, its cells' included.
    grid.set_enabled(false);
    stops.clear();
    grid.collect_focusables(stops);
    REQUIRE(stops.empty());
}

TEST_CASE("Rows sized to their content grow to fit wrapped text and template cells", "[data-grid][layout]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Notes", .width = 80.0f}, {.header = "Extra", .width = 60.0f}});
    grid.set_rows(
        {{"short", ""}, {"a much longer note that wraps over several lines in a narrow column", ""}, {"short", ""}});
    grid.set_cell_factory(
        [](std::size_t row, std::size_t column) -> std::unique_ptr<Element>
        {
            if (row != 2 || column != 1)
            {
                return nullptr;
            }
            auto tall = std::make_unique<TextControl>("tall");
            tall->set_height(90.0f);
            return tall;
        });
    grid.set_size_rows_to_content(true);
    (void)lay_out(grid, 400.0f, 400.0f);

    const auto row_height = [&](std::size_t position)
    {
        const Element* row = ui::automation_providers(grid).grid->grid_item(position, 0);
        REQUIRE(row != nullptr);
        return row->bounds().height;
    };
    REQUIRE_THAT(row_height(0), WithinAbs(40.0, 1e-3)); // row_height() is the least
    REQUIRE(row_height(1) > 40.0f);                     // the wrapped note
    REQUIRE_THAT(row_height(2), WithinAbs(90.0, 1e-3)); // the template cell's own height

    // The wrapped text is drawn whole, wrapping in its cell.
    bool wrapped = false;
    for (const auto& cmd : render(grid).commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&cmd); t != nullptr && t->text.starts_with("a much longer note"))
        {
            wrapped = t->style.wrapping == TextWrapping::Wrap && t->bounds.height > 40.0f;
        }
    }
    REQUIRE(wrapped);

    // Off again, every row is row_height() tall and the text is trimmed.
    grid.set_size_rows_to_content(false);
    (void)lay_out(grid, 400.0f, 400.0f);
    REQUIRE_THAT(row_height(1), WithinAbs(40.0, 1e-3));
}

TEST_CASE("Automation sees a data grid of header items, rows and cells", "[data-grid][automation]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}, {.header = "Age", .width = 60.0f, .can_sort = false}});
    grid.set_rows({{"Ada", "36"}, {"Grace", "85"}});
    grid.set_selected_row(0);
    (void)lay_out(grid, 400.0f, 300.0f);

    REQUIRE(describe_automation(grid) ==
            "DataGrid [Selection Grid=2x2 Table]\n"
            "  Header\n"
            "    HeaderItem \"Name\" [Invoke]\n"
            "    HeaderItem \"Age\"\n"
            "  DataItem \"Ada, 36\" [SelectionItem=Selected]\n"
            "    Text \"Ada\" [GridItem=0,0 TableItem]\n"
            "    Text \"36\" [GridItem=0,1 TableItem]\n"
            "  DataItem \"Grace, 85\" [SelectionItem=Unselected]\n"
            "    Text \"Grace\" [GridItem=1,0 TableItem]\n"
            "    Text \"85\" [GridItem=1,1 TableItem]\n");

    // Every node's parent is the one that lists it, with the internal list left out.
    const std::vector<Element*> top = automation_children(grid);
    REQUIRE(top.size() == 3);
    for (Element* node : top)
    {
        REQUIRE(automation_parent(*node) == &grid);
    }
    Element* cell = ui::automation_providers(grid).grid->grid_item(1, 0);
    REQUIRE(cell != nullptr);
    REQUIRE(automation_parent(*cell) == top[2]);
    REQUIRE(ui::automation_providers(*cell).grid_item->grid_item_container() == &grid);
    REQUIRE(cell->bounds() == Rect{grid.bounds().left() + Border, top[2]->bounds().top(), 100.0f, 40.0f});

    // TableItem names the column header; Table lists them all.
    std::vector<Element*> header_items;
    ui::automation_providers(*cell).table_item->table_item_column_headers(header_items);
    REQUIRE(header_items.size() == 1);
    REQUIRE(header_items.front()->automation_name() == "Name");
    std::vector<Element*> headers;
    ui::automation_providers(grid).table->table_column_headers(headers);
    REQUIRE(headers.size() == 2);

    // Selecting a row, sorting by a header and the selection all go through the patterns.
    REQUIRE(ui::automation_providers(*top[2]).selection_item->select_item());
    REQUIRE(grid.selected_row() == 1);
    std::vector<Element*> selected;
    ui::automation_providers(grid).selection->selected_items(selected);
    REQUIRE(selected == std::vector<Element*>{top[2]});
    REQUIRE_FALSE(ui::automation_providers(grid).selection->selection_required()); // every mode can select nothing
    REQUIRE(ui::automation_providers(*headers[0]).invoke->invoke());
    REQUIRE(grid.sort_column() == 0);
    REQUIRE(ui::automation_providers(*headers[1]).invoke == nullptr); // an unsortable column has nothing to invoke
    REQUIRE(ui::automation_providers(grid).grid->grid_item(5, 0) == nullptr);
}

TEST_CASE("Automation adds rows to a multiple selection and takes them out", "[data-grid][automation]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}});
    grid.set_rows({{"Ada"}, {"Grace"}, {"Linus"}});
    grid.set_selection_mode(SelectionMode::Multiple);
    (void)lay_out(grid, 400.0f, 300.0f);
    const std::vector<Element*> top = automation_children(grid); // the header, then the rows
    REQUIRE(top.size() == 4);

    REQUIRE(ui::automation_providers(*top[1]).selection_item->add_to_selection());
    REQUIRE(ui::automation_providers(*top[3]).selection_item->add_to_selection());
    CHECK(grid.selected_rows() == std::vector<std::size_t>{0, 2});
    REQUIRE(ui::automation_providers(*top[1]).selection_item->remove_from_selection());
    CHECK(grid.selected_rows() == std::vector<std::size_t>{2});

    grid.set_selection_mode(SelectionMode::Single);
    REQUIRE(ui::automation_providers(*top[2]).selection_item->add_to_selection());
    CHECK(grid.selected_rows() == std::vector<std::size_t>{1}); // one at a time
}

TEST_CASE("A group header row is a Group node that expands and collapses", "[data-grid][automation]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}});
    grid.set_rows({{"Ada"}, {"Grace"}});
    grid.set_group_keys({"Staff", "Staff"});
    (void)lay_out(grid, 400.0f, 300.0f);

    const std::vector<Element*> top = automation_children(grid);
    REQUIRE(top.size() == 4); // the header, the group header row and two rows
    Element* group = top[1];
    REQUIRE(group->automation_role() == AutomationRole::Group);
    REQUIRE(group->automation_name() == "Staff");
    REQUIRE(ui::automation_providers(*group).expand_collapse->expanded_state());
    REQUIRE(ui::automation_providers(grid).grid->grid_item(0, 0) == nullptr);
    REQUIRE(ui::automation_providers(*group).expand_collapse->set_expanded_state(false));
    REQUIRE_FALSE(grid.is_group_expanded(0));
}

TEST_CASE("Turning editing off commits an open edit, and new rows drop it", "[data-grid][edit]")
{
    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(2));
    grid.set_read_only(false);
    (void)lay_out(grid, 400.0f, 300.0f);
    ui::testing::set_focused(grid, true);

    grid.handle_key(key_down(Key::F2));
    grid.handle_text("Kept");
    grid.set_read_only(true);
    REQUIRE_FALSE(grid.is_editing());
    REQUIRE(grid.rows()[0][0] == "Kept");

    grid.set_read_only(false);
    grid.handle_key(key_down(Key::F2));
    grid.handle_text("Dropped");
    grid.set_rows(make_rows(2));
    REQUIRE_FALSE(grid.is_editing());
    REQUIRE(grid.rows()[0][0] == "Name 0");
}

// ---- editing ----

TEST_CASE("Editing is off by default; F2 then edits the current cell in a TextBox", "[data-grid][edit]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Id", .width = 60.0f, .read_only = true},
                      {.header = "Name", .width = 120.0f},
                      {.header = "City", .width = 120.0f}});
    grid.set_rows(make_rows(5));
    grid.set_selected_row(0);
    (void)lay_out(grid, 400.0f, 300.0f);
    ui::testing::set_focused(grid, true);

    grid.handle_key(key_down(Key::F2));
    REQUIRE_FALSE(grid.is_editing());

    grid.set_read_only(false);
    grid.handle_key(key_down(Key::ArrowDown)); // row 1
    grid.handle_key(key_down(Key::F2));
    REQUIRE(grid.is_editing());
    REQUIRE(grid.editing_row() == 1);
    REQUIRE(grid.editing_column() == 1); // the first editable column; Id is read-only
    auto* box = dynamic_cast<TextBoxControl*>(grid.editor());
    REQUIRE(box != nullptr);
    REQUIRE(box->text() == "21");
    REQUIRE(box->focused()); // the table keeps focus and lends it

    // Typing replaces the whole value (it opens selected) and Enter commits it.
    grid.handle_text("Zed");
    REQUIRE(box->text() == "Zed");
    grid.handle_key(key_down(Key::ArrowDown)); // the editor's, not the rows'
    REQUIRE(grid.selected_row() == 1);
    grid.handle_key(key_down(Key::Enter));
    REQUIRE_FALSE(grid.is_editing());
    REQUIRE(grid.rows()[1][1] == "Zed");
    REQUIRE(grid.current_column() == 1);

    (void)lay_out(grid, 400.0f, 300.0f);
    REQUIRE(contains(drawn_text(render(grid)), "Zed"));
}

TEST_CASE("Escape cancels an edit and a rejected commit keeps the editor open", "[data-grid][edit]")
{
    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(3));
    grid.set_read_only(false);
    (void)lay_out(grid, 400.0f, 300.0f);
    ui::testing::set_focused(grid, true);

    std::vector<std::string> seen;
    grid.on_cell_edited(
        [&](const TableViewCellEditArgs& args)
        {
            REQUIRE(args.row == 0);
            REQUIRE(args.column == 0);
            seen.push_back(args.text);
            if (args.text.empty())
            {
                return ui::Cancel;
            }
            args.text = "<" + args.text + ">"; // what the cell shows
            return ui::Proceed;
        });

    grid.handle_key(key_down(Key::F2));
    grid.handle_text("Changed");
    grid.handle_key(key_down(Key::Escape));
    REQUIRE_FALSE(grid.is_editing());
    REQUIRE(grid.rows()[0][0] == "Name 0");
    REQUIRE(seen.empty());

    grid.handle_key(key_down(Key::F2));
    grid.handle_key(key_down(Key::Delete)); // the whole value is selected: the box empties
    grid.handle_key(key_down(Key::Enter));
    REQUIRE(grid.is_editing()); // rejected
    REQUIRE_FALSE(grid.commit_edit());
    grid.handle_text("Ada");
    REQUIRE(grid.commit_edit());
    REQUIRE(grid.rows()[0][0] == "<Ada>");
    REQUIRE(seen == std::vector<std::string>{"", "", "Ada"});
    REQUIRE_FALSE(grid.cancel_edit()); // nothing open
}

TEST_CASE("A double-click edits the cell under it; a press elsewhere or focus leaving commits", "[data-grid][edit]")
{
    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(4));
    (void)lay_out(grid, 400.0f, 300.0f);
    ui::testing::set_focused(grid, true);

    double_click(grid, cell_point(grid, 2, 2));
    REQUIRE_FALSE(grid.is_editing());    // read-only
    REQUIRE(grid.current_column() == 2); // the press still moved the current cell

    grid.set_read_only(false);
    double_click(grid, cell_point(grid, 2, 2));
    REQUIRE(grid.is_editing());
    REQUIRE(grid.editing_row() == 2);
    REQUIRE(grid.editing_column() == 2);
    (void)lay_out(grid, 400.0f, 300.0f);
    REQUIRE_THAT(grid.editor()->bounds().center().y, WithinAbs(cell_point(grid, 2, 2).y, 1.0));

    // A press in the editor is the caret's; one elsewhere in the table commits.
    grid.handle_text("Paris");
    click(grid, grid.editor()->bounds().center());
    REQUIRE(grid.is_editing());
    click(grid, cell_point(grid, 0, 0));
    REQUIRE_FALSE(grid.is_editing());
    REQUIRE(grid.rows()[2][2] == "Paris");
    REQUIRE(grid.selected_row() == 0);

    // Focus leaving commits once it has settled; coming straight back keeps the edit.
    grid.handle_key(key_down(Key::F2));
    REQUIRE(grid.editing_column() == 0);
    ui::testing::set_focused(grid, false);
    ui::testing::set_focused(grid, true);
    grid.tick(16ms);
    REQUIRE(grid.is_editing());
    grid.handle_text("Ann");
    ui::testing::set_focused(grid, false);
    REQUIRE(grid.is_editing());
    grid.tick(16ms);
    REQUIRE_FALSE(grid.is_editing());
    REQUIRE(grid.rows()[0][0] == "Ann");
}

TEST_CASE("Left and Right move the current column while editing is possible, and it takes the focus ring",
          "[data-grid][edit][input]")
{
    TableViewControl grid;
    grid.set_columns(
        {{.header = "Name", .width = 300.0f}, {.header = "Age", .width = 300.0f}, {.header = "City", .width = 300.0f}});
    grid.set_rows(make_rows(3));
    grid.set_read_only(false);
    (void)lay_out(grid, 400.0f, 300.0f);
    ui::testing::set_focused(grid, true);
    REQUIRE(grid.handle_key({Key::ArrowLeft, KeyAction::Up}));

    grid.handle_key(key_down(Key::ArrowRight)); // None -> the first column
    REQUIRE(grid.current_column() == 0);
    grid.handle_key(key_down(Key::ArrowRight));
    grid.handle_key(key_down(Key::ArrowRight));
    grid.handle_key(key_down(Key::ArrowRight)); // clamped
    REQUIRE(grid.current_column() == 2);
    REQUIRE(grid.horizontal_offset() > 0.0f); // scrolled to show it
    grid.handle_key(key_down(Key::F2));
    REQUIRE(grid.editing_column() == 2);
    REQUIRE(grid.cancel_edit());

    grid.handle_key(key_down(Key::ArrowLeft));
    grid.handle_key(key_down(Key::ArrowLeft));
    REQUIRE(grid.current_column() == 0);
    REQUIRE_THAT(grid.horizontal_offset(), WithinAbs(0.0, 1e-3));

    // The ring goes round the current cell, not the grid.
    (void)lay_out(grid, 400.0f, 300.0f);
    const Rect cell{grid.bounds().left() + Border, grid.bounds().top() + Border + 40.0f, 300.0f, 40.0f};
    bool       cell_ring = false;
    bool       grid_ring = false;
    for (const auto& cmd : render(grid).commands)
    {
        if (const auto* s = std::get_if<StrokeRoundedRect>(&cmd); s != nullptr)
        {
            cell_ring = cell_ring || (s->bounds.width < 320.0f && s->bounds.height < 50.0f &&
                                      s->bounds.top() < cell.bottom() && s->bounds.bottom() > cell.top());
            grid_ring = grid_ring || (s->bounds.height > 200.0f && s->color != Theme::light().palette.tabular_grid_line);
        }
    }
    REQUIRE(cell_ring);
    REQUIRE_FALSE(grid_ring);
}

TEST_CASE("An editor factory replaces the TextBox; a template cell without one is not editable",
          "[data-grid][edit][template]")
{
    TableViewControl grid;
    grid.set_columns(
        {{.header = "Name", .width = 120.0f}, {.header = "Done", .width = 80.0f}, {.header = "City", .width = 120.0f}});
    grid.set_rows(make_rows(3));
    grid.set_read_only(false);
    grid.set_cell_factory([](std::size_t, std::size_t column) -> std::unique_ptr<Element>
                          { return column == 1 ? std::make_unique<CheckBoxControl>("") : nullptr; });
    CheckBoxControl* editor_box = nullptr;
    grid.set_cell_editor_factory(
        [&](std::size_t, std::size_t column) -> std::unique_ptr<Element>
        {
            if (column != 2)
            {
                return nullptr;
            }
            auto box   = std::make_unique<CheckBoxControl>("Visited");
            editor_box = box.get();
            return box;
        });
    (void)lay_out(grid, 400.0f, 300.0f);
    ui::testing::set_focused(grid, true);

    std::vector<std::size_t> begun;
    grid.on_beginning_edit(
        [&](const TableViewCellArgs& args)
        {
            begun.push_back(args.column);
            return ui::Proceed;
        });

    grid.set_current_column(1);
    grid.handle_key(key_down(Key::F2));
    REQUIRE_FALSE(grid.is_editing()); // a template cell with no editor
    grid.set_current_column(2);
    grid.handle_key(key_down(Key::F2));
    REQUIRE(grid.is_editing());
    REQUIRE(grid.editor() == editor_box);
    REQUIRE(editor_box->focused());
    grid.handle_key(key_down(Key::Space));
    grid.handle_key({Key::Space, KeyAction::Up});
    REQUIRE(editor_box->checked() == true); // the keys reach the editor

    std::vector<Element*> stops;
    grid.collect_focusables(stops);
    REQUIRE(std::ranges::find(stops, static_cast<Element*>(editor_box)) == stops.end()); // never a stop of its own
    REQUIRE(grid.commit_edit());
    REQUIRE(begun == std::vector<std::size_t>{1, 2});

    grid.on_beginning_edit([] { return ui::Cancel; });
    grid.handle_key(key_down(Key::F2));
    REQUIRE_FALSE(grid.is_editing());
}

namespace
{
// A focusable editor whose popup is open until told otherwise.
class PopupEditor final : public Element
{
public:
    void show()
    {
        open_popup({.content = &list_});
    }
    void hide()
    {
        close_popup();
    }
    [[nodiscard]] bool focusable() const override
    {
        return true;
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

private:
    TextControl list_{"popup"};
};
} // namespace

TEST_CASE("An Escape that closes the editor's popup leaves the edit open", "[data-grid][edit]")
{
    TableViewControl grid;
    grid.set_columns(make_columns());
    grid.set_rows(make_rows(2));
    grid.set_read_only(false);
    PopupEditor* editor = nullptr;
    grid.set_cell_editor_factory(
        [&](std::size_t, std::size_t) -> std::unique_ptr<Element>
        {
            auto e = std::make_unique<PopupEditor>();
            e->show();
            editor = e.get();
            return e;
        });
    (void)lay_out(grid, 400.0f, 300.0f);
    ui::testing::set_focused(grid, true);
    grid.handle_key(key_down(Key::F2));
    REQUIRE(grid.editor() == editor);

    // Window offers the key to the shortcuts, closes the popup, then hands it to the table.
    REQUIRE_FALSE(grid.handle_shortcut(key_down(Key::Escape)));
    editor->hide();
    grid.handle_key(key_down(Key::Escape));
    REQUIRE(grid.is_editing());
    REQUIRE_FALSE(grid.handle_shortcut(key_down(Key::Escape)));
    grid.handle_key(key_down(Key::Escape));
    REQUIRE_FALSE(grid.is_editing());
}

TEST_CASE("The TableView spec edits through on_cell_edited and an editing template's draft", "[data-grid][edit][spec]")
{
    Property<std::vector<Item>>      items{std::vector<Item>{{"b", 30}, {"a", 200}}};
    ui::Control<ui::TableView<Item>> table;
    std::unique_ptr<Element>         node = ui::build(ui::TableView<Item>{
        .items     = ui::bind(items),
        .columns   = {{.header = "Name", .width = 100.0f, .text = [](const Item& i) { return i.name; }},
                      {.header       = "Size",
                       .width        = 100.0f,
                       .text         = [](const Item& i) { return std::to_string(i.size) + " KB"; },
                       .editing_cell = [](Item& draft) -> ui::Spec
                       {
                         auto box = std::make_unique<CheckBoxControl>("Double");
                         box->on_checked_changed([&draft](std::optional<bool>) { draft.size *= 2; });
                         return box;
                       }}},
        .read_only = false,
        .on_cell_edited =
            [](const TableViewCellEditedArgs<Item>& args)
        {
            if (args.text.empty())
            {
                return ui::Cancel;
            }
            if (args.column == 0)
            {
                args.item.name = args.text;
            }
            return ui::Proceed;
        },
        .control = table,
    });
    (void)lay_out(*table, 400.0f, 300.0f);
    ui::testing::set_focused(*table, true);

    table->handle_key(key_down(Key::F2));
    table->handle_text("bee");
    table->handle_key(key_down(Key::Enter));
    REQUIRE(items.get()[0].name == "bee"); // written into the item and back into the property
    REQUIRE(table->rows()[0][0] == "bee");

    // The template editor changes a draft; Escape drops it, a commit copies it into the item.
    table->set_current_column(1);
    table->handle_key(key_down(Key::F2));
    auto* box = dynamic_cast<CheckBoxControl*>(table->editor());
    REQUIRE(box != nullptr);
    box->set_checked(true);
    table->handle_key(key_down(Key::Escape));
    REQUIRE(items.get()[0].size == 30);
    table->handle_key(key_down(Key::F2));
    dynamic_cast<CheckBoxControl*>(table->editor())->set_checked(true);
    table->handle_key(key_down(Key::Enter));
    REQUIRE(items.get()[0].size == 60);
    REQUIRE(table->rows()[0][1] == "60 KB");
}

TEST_CASE("A replaced item reads its own row again, keeps its place and leaves an edit open", "[data-grid][spec]")
{
    ui::ObservableVector<Item> items;
    for (int i = 0; i < 200; ++i)
    {
        items.push_back({"item " + std::to_string(i), i});
    }
    int                              reads = 0;
    ui::Control<ui::TableView<Item>> table;
    std::unique_ptr<Element>         node = ui::build(ui::TableView<Item>{
        .items     = ui::bind(items),
        .columns   = {{.header = "Name",
                       .width  = 100.0f,
                       .text =
                           [&reads](const Item& i)
                       {
                         ++reads;
                         return i.name;
                       }},
                      {.header  = "Size",
                       .width   = 100.0f,
                       .text    = [](const Item& i) { return std::to_string(i.size); },
                       .compare = [](const Item& a, const Item& b) { return a.size <=> b.size; }}},
        .read_only = false,
        .on_cell_edited =
            [](const TableViewCellEditedArgs<Item>& args)
        {
            args.item.name = args.text;
            return ui::Proceed;
        },
        .control = table,
    });
    (void)lay_out(*table, 400.0f, 300.0f);
    REQUIRE(table->sort_by_column(1, TableViewSortDirection::Descending));
    REQUIRE(table->row_at_position(0) == 199);

    reads = 0;
    items.replace(199, Item{"renamed", -1});
    CHECK(reads == 1);
    CHECK(table->rows()[199][0] == "renamed");
    CHECK(table->row_at_position(0) == 199); // still first, though its size sorts it last now

    // An edit open on another row stays open through a replace.
    table->set_selected_row(10);
    table->set_current_column(0);
    ui::testing::set_focused(*table, true);
    table->handle_key(key_down(Key::F2));
    REQUIRE(table->is_editing());
    items.replace(20, Item{"other", 20});
    CHECK(table->is_editing());
    CHECK(table->rows()[20][0] == "other");
}

TEST_CASE("An edit in a TableView of an ObservableVector replaces the item", "[data-grid][edit][spec]")
{
    ui::ObservableVector<Item>        items{Item{"b", 30}, Item{"a", 200}};
    std::vector<CollectionChangeKind> seen;
    const Subscription watch = items.observe([&seen](const CollectionChange& c) { seen.push_back(c.kind); });
    ui::Control<ui::TableView<Item>> table;
    std::unique_ptr<Element>         node = ui::build(ui::TableView<Item>{
        .items     = ui::bind(items),
        .columns   = {{.header = "Name", .width = 100.0f, .text = [](const Item& i) { return i.name; }},
                      {.header = "Upper",
                       .width  = 100.0f,
                       .text =
                           [](const Item& i)
                       {
                         std::string s = i.name;
                         for (char& ch : s)
                         {
                             ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                         }
                         return s;
                       }}},
        .read_only = false,
        .on_cell_edited =
            [](const TableViewCellEditedArgs<Item>& args)
        {
            args.item.name = args.text;
            return ui::Proceed;
        },
        .control = table,
    });
    (void)lay_out(*table, 400.0f, 300.0f);
    ui::testing::set_focused(*table, true);

    table->handle_key(key_down(Key::F2));
    table->handle_text("bee");
    table->handle_key(key_down(Key::Enter));
    CHECK(items[0].name == "bee");
    CHECK(seen == std::vector<CollectionChangeKind>{CollectionChangeKind::Replaced}); // others hear of the edit
    CHECK(table->rows()[0][0] == "bee");
    CHECK(table->rows()[0][1] == "BEE"); // a cell derived from the item follows it
}

TEST_CASE("In a window the table keeps focus while editing, and Tab out of the edit commits it",
          "[data-grid][edit][focus]")
{
    HeadlessUI                       ui = create_headless();
    Property<std::vector<Item>>      items{std::vector<Item>{{"a", 1}, {"b", 2}}};
    ui::Control<ui::TableView<Item>> table;
    ui.window().set_content(ui::VerticalStack{
        .content = {
            ui::TableView<Item>{
                .items     = ui::bind(items),
                .columns   = {{.header = "Name", .width = 120.0f, .text = [](const Item& i) { return i.name; }},
                              {.header = "Big",
                               .width  = 80.0f,
                               .cell   = [](Item& i) -> ui::Spec
                               { return ui::CheckBox{.checked = std::optional<bool>{i.size > 1}}; }}},
                .read_only = false,
                .height    = 200.0f,
                .on_cell_edited =
                    [](const TableViewCellEditedArgs<Item>& args)
                {
                    args.item.name = args.text;
                    return ui::Proceed;
                },
                .control = table,
            },
            ui::Button{.text = "After"},
        }});
    ui.layout({400, 400});
    Window& win = ui.window();
    win.focus_content();
    Element* grid = table;
    REQUIRE(win.focused() == grid);

    seams(win).handle_key({Key::F2, KeyAction::Down});
    seams(win).handle_text("zed");
    ui.layout({400, 400});
    REQUIRE(table->is_editing());
    REQUIRE(win.focused() == grid); // the editor is lent the keys, never focused itself

    seams(win).handle_key({Key::Tab, KeyAction::Down}); // to the first template cell's check box
    REQUIRE(win.focused() == table->cell_element(0, 1));
    REQUIRE(table->is_editing());
    ui.tick(16ms);
    REQUIRE_FALSE(table->is_editing());
    REQUIRE(items.get()[0].name == "zed");
}

// ---- grouping ----

namespace
{
void fill_grouped(TableViewControl& grid)
{
    grid.set_columns({{.header = "Name", .width = 100.0f}, {.header = "Kind", .width = 100.0f}});
    grid.set_rows({{"d", "Beta"}, {"a", "Alpha"}, {"c", "Beta"}, {"b", "Alpha"}, {"e", "Gamma"}});
    grid.set_group_keys({"Beta", "Alpha", "Beta", "Alpha", "Gamma"});
    grid.set_selected_row(0);
}
} // namespace

TEST_CASE("Group keys file the rows under header rows, in first-appearance order, sorted within", "[data-grid][group]")
{
    TableViewControl grid;
    fill_grouped(grid);
    REQUIRE(grid.group_count() == 3);
    REQUIRE(grid.group_key(0) == "Beta");
    REQUIRE(grid.group_size(1) == 2);
    REQUIRE(grid.position_count() == 5 + 3);
    REQUIRE(grid.is_group_header(0));
    REQUIRE(grid.row_at_position(1) == 0); // d
    REQUIRE(grid.row_at_position(2) == 2); // c
    REQUIRE(grid.is_group_header(3));
    REQUIRE(grid.group_at_position(4) == 1);

    grid.sort_by_column(0, TableViewSortDirection::Ascending);
    REQUIRE(grid.group_key(0) == "Beta");  // the groups keep their order
    REQUIRE(grid.row_at_position(1) == 2); // c before d within Beta
    REQUIRE(grid.row_at_position(2) == 0);

    grid.set_grouping_enabled(false);
    REQUIRE(grid.group_count() == 0);
    REQUIRE(grid.position_count() == 5);
    REQUIRE(grid.row_at_position(0) == 1); // a
    grid.set_grouping_enabled(true);
    REQUIRE(grid.group_count() == 3);

    // set_rows selects nothing, a header least of all.
    grid.set_rows({{"x", "Beta"}, {"y", "Alpha"}, {"z", "Beta"}, {"w", "Alpha"}, {"v", "Gamma"}});
    REQUIRE(grid.selected_row() == std::nullopt);
}

TEST_CASE("A group header draws its chevron, key and count, and a click collapses it", "[data-grid][group][render]")
{
    TableViewControl grid;
    fill_grouped(grid);
    (void)lay_out(grid, 400.0f, 400.0f);

    const TestRenderer             r     = render(grid);
    const std::vector<std::string> texts = drawn_text(r);
    REQUIRE(contains(texts, "Beta"));
    REQUIRE(contains(texts, "(2)"));
    REQUIRE(contains(texts, "(1)"));
    int chevrons = 0;
    for (const auto& cmd : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&cmd); t != nullptr && t->style.font_family == light().icon_font_family)
        {
            ++chevrons;
        }
    }
    REQUIRE(chevrons == 3); // one per header

    click(grid, row_point(grid.bounds(), 0, 40.0f));
    REQUIRE_FALSE(grid.is_group_expanded(0));
    REQUIRE(grid.position_of_row(0) == std::nullopt);
    REQUIRE(grid.position_count() == 6);
    REQUIRE(grid.selected_row() == 0); // the selection stays, hidden
    (void)lay_out(grid, 400.0f, 400.0f);
    REQUIRE_FALSE(contains(drawn_text(render(grid)), "d"));

    grid.collapse_all_groups();
    REQUIRE(grid.position_count() == 3);
    grid.expand_all_groups();
    REQUIRE(grid.position_count() == 8);
}

TEST_CASE("The keyboard walks group headers: Left collapses, Right expands, Space and Enter toggle",
          "[data-grid][group][input]")
{
    TableViewControl grid;
    fill_grouped(grid);
    (void)lay_out(grid, 400.0f, 400.0f);
    ui::testing::set_focused(grid, true);
    REQUIRE(grid.selected_row() == 0); // position 1

    grid.handle_key(key_down(Key::ArrowUp)); // onto the Beta header: the selection stays
    REQUIRE(grid.selected_row() == 0);
    REQUIRE(grid.handle_key({Key::Enter, KeyAction::Up}));
    grid.handle_key(key_down(Key::ArrowLeft));
    REQUIRE_FALSE(grid.is_group_expanded(0));
    grid.handle_key(key_down(Key::ArrowRight));
    REQUIRE(grid.is_group_expanded(0));
    grid.handle_key(key_down(Key::Space));
    REQUIRE_FALSE(grid.is_group_expanded(0));
    grid.handle_key(key_down(Key::Enter));
    REQUIRE(grid.is_group_expanded(0));

    grid.handle_key(key_down(Key::ArrowDown)); // back onto a row, which Single selects
    REQUIRE(grid.selected_row() == 0);
    REQUIRE_FALSE(grid.handle_key({Key::Enter, KeyAction::Up}));
    grid.handle_key(key_down(Key::ArrowDown));
    grid.handle_key(key_down(Key::ArrowDown)); // the Alpha header
    REQUIRE(grid.selected_row() == 2);
    grid.handle_key(key_down(Key::ArrowDown));
    REQUIRE(grid.selected_row() == 1); // a

    // Collapsing the group the cursor is in moves the cursor to its header.
    grid.set_group_expanded(1, false);
    grid.handle_key(key_down(Key::ArrowRight));
    REQUIRE(grid.is_group_expanded(1));
}

TEST_CASE("A range selection skips group headers and hidden rows", "[data-grid][group][selection]")
{
    TableViewControl grid;
    fill_grouped(grid);
    grid.set_selection_mode(SelectionMode::Extended);
    const Rect bounds = lay_out(grid, 400.0f, 400.0f);

    click(grid, row_point(bounds, 1, 40.0f));                 // d
    click(grid, row_point(bounds, 7, 40.0f), /*shift=*/true); // e: through Alpha
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{0, 1, 2, 3, 4});

    grid.set_group_expanded(1, false); // Alpha: positions now Beta, d, c, Alpha, Gamma, e
    (void)lay_out(grid, 400.0f, 400.0f);
    click(grid, row_point(bounds, 1, 40.0f));
    click(grid, row_point(bounds, 5, 40.0f), /*shift=*/true);
    REQUIRE(grid.selected_rows() == std::vector<std::size_t>{0, 2, 4});
}

TEST_CASE("The TableView spec groups by group_by and toggles grouping with grouping_enabled", "[data-grid][group][spec]")
{
    Property<std::vector<Item>>      items{std::vector<Item>{{"b", 1}, {"a", 2}, {"c", 1}}};
    Property<bool>                   grouped{true};
    ui::Control<ui::TableView<Item>> table;
    std::unique_ptr<Element>         node = ui::build(ui::TableView<Item>{
        .items            = ui::bind(items),
        .columns          = {{.header = "Name", .width = 100.0f, .text = [](const Item& i) { return i.name; }}},
        .group_by         = [](const Item& i) { return std::to_string(i.size) + " KB"; },
        .grouping_enabled = ui::bind(grouped),
        .control          = table,
    });
    REQUIRE(table->group_count() == 2);
    REQUIRE(table->group_key(0) == "1 KB");
    REQUIRE(table->group_size(0) == 2);

    items.update([](std::vector<Item>& v) { v.push_back({"d", 3}); });
    REQUIRE(table->group_count() == 3);

    grouped = false;
    REQUIRE(table->group_count() == 0);
    REQUIRE(table->position_count() == 4);
}

TEST_CASE("Aero's bars take gutters from a TableView's body and meet in a corner cell", "[data-grid][layout][aero]")
{
    HeadlessTextMeasurer m;
    const Theme          aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    TableViewControl grid;
    grid.set_columns(make_columns()); // 300 wide
    grid.set_rows(make_rows(50));
    grid.set_row_height(20.0f);
    (void)grid.measure({250, 200}, m, aero);
    grid.arrange({0, 0, 250, 200});

    // The columns scroll within the width the vertical bar leaves them.
    const Rect b = grid.bounds().inset(Thickness{Border});
    grid.set_horizontal_offset(1000.0f);
    CHECK(grid.horizontal_offset() == 300.0f - (b.width - 17.0f));

    TestRenderer r;
    grid.render(r, aero);
    bool corner = false;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&command))
        {
            corner = corner || fill->bounds == Rect{b.right() - 17.0f, b.bottom() - 17.0f, 17.0f, 17.0f};
        }
    }
    CHECK(corner);

    // Columns that fit leave the body whole: no horizontal gutter, no corner.
    TableViewControl narrow;
    narrow.set_columns({{.header = "Name", .width = 100.0f}});
    narrow.set_rows(make_rows(50));
    narrow.set_row_height(20.0f);
    (void)narrow.measure({250, 200}, m, aero);
    narrow.arrange({0, 0, 250, 200});
    TestRenderer n;
    narrow.render(n, aero);
    for (const RenderCommand& command : n.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&command))
        {
            CHECK(fill->bounds.height != 17.0f);
        }
    }
}

TEST_CASE("A keyed TableView keeps the selection on the same items when the vector changes", "[data-grid][spec]")
{
    Property<std::vector<Item>>      items{std::vector<Item>{{"a", 1}, {"b", 2}, {"c", 3}}};
    ui::Control<ui::TableView<Item>> table;
    std::unique_ptr<Element>         node = ui::build(ui::TableView<Item>{
        .items   = ui::bind(items),
        .key     = &Item::name,
        .columns = {{.header = "Name", .width = 100.0f, .text = [](const Item& i) { return i.name; }}},
        .control = table,
    });
    table->set_selected_row(1); // "b"

    items = std::vector<Item>{{"z", 0}, {"a", 1}, {"b", 2}, {"c", 3}};
    CHECK(table->selected_row() == 2);

    items = std::vector<Item>{{"b", 2}, {"c", 3}};
    CHECK(table->selected_row() == 0);
}

TEST_CASE("TableView keeps the selection on the same items across ObservableVector edits", "[data-grid][spec]")
{
    ObservableVector<std::string>           names{"a", "b", "c"};
    ui::Control<ui::TableView<std::string>> table;
    std::unique_ptr<Element>                node = ui::build(ui::TableView<std::string>{
        .items          = ui::bind(names),
        .columns        = {{.header = "Name", .width = 100.0f, .text = [](const std::string& s) { return s; }}},
        .selection_mode = SelectionMode::Multiple,
        .control        = table,
    });
    table->set_selected_rows({0, 2}); // "a" and "c"

    (void)names.insert(1, "x");
    CHECK(table->selected_rows() == std::vector<std::size_t>{0, 3});
    names.erase(0); // "a" goes
    CHECK(table->selected_rows() == std::vector<std::size_t>{2});
    names.move(2, 0); // "c" to the front
    CHECK(table->selected_rows() == std::vector<std::size_t>{0});
}

TEST_CASE("Up from the first row reaches the column headers, which sort and resize from the keyboard",
          "[data-grid][sort][focus]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}, {.header = "Age", .width = 80.0f}});
    grid.set_rows({{"Grace", "85"}, {"Ada", "36"}, {"Alan", "100"}});
    grid.set_selected_row(0);
    (void)lay_out(grid, 400.0f, 300.0f);
    ui::testing::set_focused(grid, true);

    std::vector<TableViewColumnResizedArgs> resized;
    grid.on_column_resized([&](const TableViewColumnResizedArgs& args) { resized.push_back(args); });

    REQUIRE(grid.selected_row() == 0);
    grid.handle_key(key_down(Key::ArrowUp));
    REQUIRE(grid.header_cursor() == 0);
    CHECK(grid.handle_key({Key::Enter, KeyAction::Up}));

    grid.handle_key(key_down(Key::ArrowRight));
    CHECK(grid.header_cursor() == 1);
    grid.handle_key(key_down(Key::Enter));
    CHECK(grid.sort_column() == 1);
    CHECK(column_text(grid, 1) == std::vector<std::string>{"36", "85", "100"});

    KeyEvent widen{Key::ArrowRight, KeyAction::Down};
    widen.modifiers.ctrl = true;
    grid.handle_key(widen);
    REQUIRE(resized.size() == 1);
    CHECK(resized[0].column == 1);
    CHECK(resized[0].width == 88.0f);
    CHECK(grid.header_cursor() == 1); // Ctrl resizes and stays put

    grid.handle_key(key_down(Key::ArrowDown));
    CHECK(grid.header_cursor() == std::nullopt);
    CHECK(grid.selected_row() == 1); // the first row in view order, "Ada"
}

TEST_CASE("on_sorting can keep the current order", "[data-grid][sort]")
{
    TableViewControl grid;
    grid.set_columns({{.header = "Name", .width = 100.0f}, {.header = "Age", .width = 80.0f}});
    grid.set_rows({{"Grace", "85"}, {"Ada", "36"}});
    (void)lay_out(grid, 400.0f, 300.0f);

    bool                                cancel = true;
    std::vector<TableViewSortDirection> asked;
    grid.on_sorting(
        [&](const TableViewSortingArgs& args)
        {
            asked.push_back(args.direction);
            return cancel ? ui::Cancel : ui::Proceed;
        });
    click(grid, header_point(grid, 0));
    CHECK(grid.sort_column() == std::nullopt);
    cancel = false;
    click(grid, header_point(grid, 0));
    CHECK(grid.sort_column() == 0);
    CHECK(asked ==
          std::vector<TableViewSortDirection>{TableViewSortDirection::Ascending, TableViewSortDirection::Ascending});
}

TEST_CASE("A column's tool_tip gives each cell its own tip", "[data-grid][spec]")
{
    ui::Control<ui::TableView<Item>> table;
    std::unique_ptr<Element>         node = ui::build(ui::TableView<Item>{
        .items    = {{"a", 1}, {"b", 2}},
        .columns  = {{.header   = "Name",
                      .width    = 100.0f,
                      .text     = [](const Item& i) { return i.name; },
                      .tool_tip = [](const Item& i) { return "Item " + i.name; }},
                     {.header = "Size", .width = 80.0f, .text = [](const Item& i) { return std::to_string(i.size); }}},
        .tool_tip = "The table",
        .control  = table,
    });
    (void)lay_out(*table, 400.0f, 300.0f);

    // The header is 40 tall; rows follow it.
    const auto cell = [&](std::size_t row, std::size_t column)
    {
        return Point{header_point(*table, column).x,
                     table->bounds().top() + 40.0f + table->row_height() * (static_cast<float>(row) + 0.5f)};
    };
    CHECK(table->tool_tip_at(cell(0, 0)) == "Item a");
    CHECK(table->tool_tip_at(cell(1, 0)) == "Item b");
    CHECK(table->tool_tip_at(cell(0, 1)) == "The table"); // no tip in that column
    CHECK(table->tool_tip_at(header_point(*table, 0)) == "The table");
}

TEST_CASE("A group header template builds each header's content in view from its key", "[data-grid][group][spec]")
{
    ui::Control<ui::TableView<Item>> table;
    std::unique_ptr<Element>         node = ui::build(ui::TableView<Item>{
        .items                 = {{"a", 1}, {"b", 1}, {"c", 2}},
        .columns               = {{.header = "Name", .width = 100.0f, .text = [](const Item& i) { return i.name; }}},
        .group_by              = [](const Item& i) { return i.size == 1 ? std::string{"One"} : std::string{"Two"}; },
        .control               = table,
        .group_header_template = [](const std::string& key) -> ui::Spec { return ui::Text{.text = "Group " + key}; },
    });
    (void)lay_out(*table, 400.0f, 300.0f);

    REQUIRE(table->group_count() == 2);
    auto* first  = dynamic_cast<TextControl*>(table->group_header_element(0));
    auto* second = dynamic_cast<TextControl*>(table->group_header_element(1));
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->text() == "Group One");
    CHECK(second->text() == "Group Two");
    CHECK(first->bounds().left() > table->bounds().left() + 24.0f); // after the chevron
}

TEST_CASE("The current group's header pins under the column headers while its rows scroll", "[data-grid][group][sticky]")
{
    TableViewControl                      grid;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string>              keys;
    for (int i = 0; i < 20; ++i)
    {
        rows.push_back({"row " + std::to_string(i)});
        keys.emplace_back(i < 10 ? "First" : "Second");
    }
    grid.set_columns({{.header = "Name", .width = 100.0f}});
    grid.set_rows(std::move(rows));
    grid.set_group_keys(std::move(keys));
    const Rect bounds = lay_out(grid, 300.0f, 200.0f);
    REQUIRE(grid.sticky_group_headers());
    REQUIRE(contains(drawn_text(render(grid)), "First")); // its own header row, at the top

    // Scrolled into the first group, its header row is gone; the pinned copy shows the key.
    PointerEvent wheel{{bounds.left() + 50.0f, bounds.top() + 100.0f}, PointerButton::None, PointerAction::Wheel, 120.0f};
    REQUIRE(grid.handle_wheel(wheel));
    (void)lay_out(grid, 300.0f, 200.0f);
    REQUIRE_FALSE(contains(drawn_text(render(grid)), "row 0"));
    REQUIRE(contains(drawn_text(render(grid)), "First"));

    // Off, nothing is pinned.
    grid.set_sticky_group_headers(false);
    (void)lay_out(grid, 300.0f, 200.0f);
    REQUIRE_FALSE(contains(drawn_text(render(grid)), "First"));
    grid.set_sticky_group_headers(true);
    const Rect pinned   = lay_out(grid, 300.0f, 200.0f);
    const auto body_top = [&]
    {
        for (const auto& cmd : render(grid).commands)
        {
            if (const auto* t = std::get_if<DrawTextRun>(&cmd); t != nullptr && t->text == "First")
            {
                return t->bounds.center();
            }
        }
        return Point{};
    }();
    REQUIRE(body_top.y > pinned.top());
    // A click on the pinned header still collapses its group.
    click(grid, Point{pinned.left() + 40.0f, body_top.y});
    REQUIRE_FALSE(grid.is_group_expanded(0));
}
