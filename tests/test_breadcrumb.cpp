// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/navigation/Breadcrumb.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
KeyEvent key_down(Key k)
{
    return {k, KeyAction::Down};
}
KeyEvent key_up(Key k)
{
    return {k, KeyAction::Up};
}
} // namespace

TEST_CASE("A default-constructed Breadcrumb has no items and is not focusable", "[breadcrumb]")
{
    BreadcrumbControl bar;
    REQUIRE(bar.items().empty());
    REQUIRE_FALSE(bar.focusable());
}

TEST_CASE("set_items replaces the list and drops transient interaction state", "[breadcrumb]")
{
    BreadcrumbControl bar{{"Home", "Docs"}};
    REQUIRE(bar.items().size() == 2);

    bar.set_items({"Home", "Pictures", "Vacation"});
    REQUIRE(bar.items().size() == 3);
    REQUIRE(bar.items().back() == "Vacation");
}

TEST_CASE("A single-item Breadcrumb is not focusable - the only item is the current location", "[breadcrumb][focus]")
{
    BreadcrumbControl bar{{"Home"}};
    REQUIRE_FALSE(bar.focusable());

    bar.set_items({"Home", "Docs"});
    REQUIRE(bar.focusable());

    bar.set_enabled(false);
    REQUIRE_FALSE(bar.focusable());
}

TEST_CASE("Clicking an earlier item fires on_item_click with its index", "[breadcrumb][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    BreadcrumbControl bar{{"A", "B", "C"}};
    const Size        desired = bar.measure({300.0f, 200.0f}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    int         calls = 0;
    std::size_t last  = static_cast<std::size_t>(-1);
    bar.on_item_click(
        [&](std::size_t i)
        {
            ++calls;
            last = i;
        });

    const Point p{5.0f, 5.0f}; // inside item "A"
    bar.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    bar.handle_pointer({p, PointerButton::Left, PointerAction::Up});
    REQUIRE(calls == 1);
    REQUIRE(last == 0);
}

TEST_CASE("Clicking the last item never fires on_item_click - it is the current location, not a link",
          "[breadcrumb][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    BreadcrumbControl bar{{"A", "B"}};
    const Size        desired = bar.measure({300.0f, 200.0f}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    int calls = 0;
    bar.on_item_click([&](std::size_t) { ++calls; });

    const Point p{desired.width - 2.0f, 5.0f};
    bar.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    bar.handle_pointer({p, PointerButton::Left, PointerAction::Up});
    REQUIRE(calls == 0);
}

TEST_CASE("Releasing outside the item that was pressed cancels the click", "[breadcrumb][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    BreadcrumbControl bar{{"A", "B", "C"}};
    const Size        desired = bar.measure({300.0f, 200.0f}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    int calls = 0;
    bar.on_item_click([&](std::size_t) { ++calls; });

    bar.handle_pointer({{5.0f, 5.0f}, PointerButton::Left, PointerAction::Down}); // press on "A"
    bar.handle_pointer({{5.0f, 500.0f}, PointerButton::Left, PointerAction::Up}); // release well outside
    REQUIRE(calls == 0);
}

TEST_CASE("A click outside every item's bounds is ignored", "[breadcrumb][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    BreadcrumbControl bar{{"A", "B"}};
    const Size        desired = bar.measure({300.0f, 200.0f}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    int calls = 0;
    bar.on_item_click([&](std::size_t) { ++calls; });

    bar.handle_pointer({{5.0f, 500.0f}, PointerButton::Left, PointerAction::Down});
    bar.handle_pointer({{5.0f, 500.0f}, PointerButton::Left, PointerAction::Up});
    REQUIRE(calls == 0);
}

TEST_CASE("Arrow keys rove a highlight among clickable items only, clamped at both ends", "[breadcrumb][input]")
{
    BreadcrumbControl bar{{"A", "B", "C", "D"}}; // clickable: A, B, C; D is the current location

    bar.handle_key(key_down(Key::ArrowRight)); // not focused: ignored
    ui::testing::set_focused(bar, true);       // starts the roving highlight at index 0 ("A")

    bar.handle_key(key_down(Key::ArrowLeft)); // already at 0: clamps
    bar.handle_key(key_down(Key::ArrowRight));
    bar.handle_key(key_down(Key::ArrowRight));
    bar.handle_key(key_down(Key::ArrowRight)); // clamped at "C", never "D"

    int         calls = 0;
    std::size_t last  = static_cast<std::size_t>(-1);
    bar.on_item_click(
        [&](std::size_t i)
        {
            ++calls;
            last = i;
        });
    bar.handle_key(key_down(Key::Enter));
    bar.handle_key(key_up(Key::Enter));
    REQUIRE(calls == 1);
    REQUIRE(last == 2); // "C"
}

TEST_CASE("Space activates the roved item the same way Enter does", "[breadcrumb][input]")
{
    BreadcrumbControl bar{{"A", "B"}};
    ui::testing::set_focused(bar, true); // highlight starts at "A"

    int calls = 0;
    bar.on_item_click([&](std::size_t) { ++calls; });

    bar.handle_key(key_down(Key::Space));
    bar.handle_key(key_up(Key::Space));
    REQUIRE(calls == 1);
}

TEST_CASE("Losing focus mid-press cancels the keyboard activation", "[breadcrumb][input]")
{
    BreadcrumbControl bar{{"A", "B"}};
    ui::testing::set_focused(bar, true);

    int calls = 0;
    bar.on_item_click([&](std::size_t) { ++calls; });

    bar.handle_key(key_down(Key::Enter));
    ui::testing::set_focused(bar, false);
    ui::testing::set_focused(bar, true);
    bar.handle_key(key_up(Key::Enter)); // no matching Down since refocusing
    REQUIRE(calls == 0);
}

TEST_CASE("Disabled Breadcrumb ignores pointer and keyboard input", "[breadcrumb][state]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    BreadcrumbControl bar{{"A", "B"}};
    const Size        desired = bar.measure({300.0f, 200.0f}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});
    ui::testing::set_focused(bar, true);
    bar.set_enabled(false);

    int calls = 0;
    bar.on_item_click([&](std::size_t) { ++calls; });

    bar.handle_pointer({{5.0f, 5.0f}, PointerButton::Left, PointerAction::Down});
    bar.handle_pointer({{5.0f, 5.0f}, PointerButton::Left, PointerAction::Up});
    bar.handle_key(key_down(Key::Enter));
    bar.handle_key(key_up(Key::Enter));
    REQUIRE(calls == 0);
}

TEST_CASE("Render draws one text run per item plus one chevron between every pair", "[breadcrumb][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    BreadcrumbControl bar{{"Home", "Documents", "Report.docx"}};
    const Size        desired = bar.measure({500.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    bar.render(r, theme);
    // 3 item labels + 2 chevron separators (draw_icon also goes through draw_text) = 5.
    REQUIRE(r.count_of<DrawTextRun>() == 5);
}

TEST_CASE("Hovering an earlier item recolors its label; the last item never reacts", "[breadcrumb][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    BreadcrumbControl bar{{"A", "B"}};
    const Size        desired = bar.measure({300.0f, 200.0f}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    // A breadcrumb item never fills: its states only move the label from
    // the primary to the secondary text color.
    bar.handle_pointer({{5.0f, 5.0f}, PointerButton::Left, PointerAction::Move}); // over "A"

    TestRenderer r;
    bar.render(r, theme);
    REQUIRE(r.count_of<FillRoundedRect>() == 0);
    REQUIRE(std::get<DrawTextRun>(r.commands[0]).color == theme.palette.text_fill_secondary); // "A", hovered
    REQUIRE(std::get<DrawTextRun>(r.commands[2]).color == theme.palette.text_fill_primary);   // "B", at rest

    bar.handle_pointer({{desired.width - 2.0f, 5.0f}, PointerButton::Left, PointerAction::Move});
    TestRenderer r2;
    bar.render(r2, theme);
    REQUIRE(r2.count_of<FillRoundedRect>() == 0);
    REQUIRE(std::get<DrawTextRun>(r2.commands[0]).color == theme.palette.text_fill_primary);
    REQUIRE(std::get<DrawTextRun>(r2.commands[2]).color == theme.palette.text_fill_primary);
}

// ---- item padding ----

TEST_CASE("An item is padded 1,3 - PART_ItemButton's own padding, not a Button's", "[breadcrumb][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    // One item, so the width holds no chevron slot.
    BreadcrumbControl bar{{"Home"}};
    const Size        desired = bar.measure({500.0f, Unbounded}, m, theme);
    const Size        label   = m.measure("Home", theme.text_style);

    REQUIRE_THAT(desired.width, WithinAbs(label.width + 2.0f, 1e-2));
    // LineHeight="20" is a floor on the row, plus the padding's own 3 + 3.
    REQUIRE_THAT(desired.height, WithinAbs(std::max(label.height, 20.0f) + 6.0f, 1e-2));
}

// ---- Overflow: the ellipsis item and its flyout ----

TEST_CASE("A trail too wide for the control folds its leading items into the ellipsis", "[breadcrumb][layout]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    BreadcrumbControl    bar({"Home", "Documents", "Design", "Northwind", "Images", "Folder1", "Folder2", "Folder3"});

    (void)bar.measure({2000.0f, Unbounded}, m, theme);
    bar.arrange({0.0f, 0.0f, 2000.0f, 40.0f});
    CHECK_FALSE(bar.ellipsis_shown());
    CHECK(bar.item_bounds(0).has_value());

    (void)bar.measure({260.0f, Unbounded}, m, theme);
    bar.arrange({0.0f, 0.0f, 260.0f, 40.0f});
    REQUIRE(bar.ellipsis_shown());
    const std::size_t first = bar.first_shown_item();
    CHECK(first > 0);
    CHECK_FALSE(bar.item_bounds(first - 1).has_value());
    // The ellipsis leads, then (after its chevron) the first shown item, and the last item always shows.
    const Rect ellipsis = *bar.ellipsis_bounds();
    CHECK_THAT(ellipsis.x, WithinAbs(bar.bounds().x, 1e-3));
    CHECK_THAT(bar.item_bounds(first)->x, WithinAbs(ellipsis.right() + 16.0f, 1e-3));
    REQUIRE(bar.item_bounds(7).has_value());
    CHECK(bar.item_bounds(7)->right() <= bar.bounds().right() + 1e-3f);

    bar.handle_pointer({ellipsis.center(), PointerButton::Left, PointerAction::Down});
    bar.handle_pointer({ellipsis.center(), PointerButton::Left, PointerAction::Up});
    REQUIRE(bar.flyout_open());
    REQUIRE(ui::testing::popup_open(bar));
    Element* list = ui::testing::popup_content(bar);
    REQUIRE(list != nullptr);
    const Size size = list->measure(Size{Unbounded, Unbounded}, m, theme);
    list->arrange(Rect{ellipsis.x, ellipsis.bottom(), size.width, size.height});
    TestRenderer r;
    list->render(r, theme);
    std::vector<std::string> rows;
    for (const auto& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            rows.push_back(t->text);
        }
    }
    REQUIRE(rows.size() == first);
    CHECK(rows.front() == bar.items()[first - 1]);
    CHECK(rows.back() == "Home");

    std::optional<std::size_t> clicked;
    bar.on_item_click([&](std::size_t i) { clicked = i; });
    const Point home{list->bounds().center().x, list->bounds().bottom() - 1.0f - 2.0f - 10.0f};
    list->handle_pointer({home, PointerButton::Left, PointerAction::Down});
    list->handle_pointer({home, PointerButton::Left, PointerAction::Up});
    CHECK(clicked == 0);
    CHECK_FALSE(bar.flyout_open());
}

TEST_CASE("The ellipsis is a stop for the keyboard, and opens its flyout on Enter", "[breadcrumb][input]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    BreadcrumbControl    bar({"Home", "Documents", "Design", "Northwind", "Images", "Folder1", "Folder2", "Folder3"});
    (void)bar.measure({260.0f, Unbounded}, m, theme);
    bar.arrange({0.0f, 0.0f, 260.0f, 40.0f});
    REQUIRE(bar.ellipsis_shown());

    ui::testing::set_focused(bar, true);
    bar.handle_key(key_down(Key::Enter));
    bar.handle_key(key_up(Key::Enter));
    REQUIRE(bar.flyout_open());
    std::optional<std::size_t> clicked;
    bar.on_item_click([&](std::size_t i) { clicked = i; });
    bar.handle_key(key_down(Key::ArrowDown));
    bar.handle_key(key_down(Key::Enter));
    CHECK(clicked == bar.first_shown_item() - 1);
    CHECK_FALSE(bar.flyout_open());
}

// ---- the focus visual ----

TEST_CASE("A focused item's ring stands 3 out around its button, into the chevron slots", "[breadcrumb][focus]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    BreadcrumbControl    bar{{"Home", "Documents", "Design"}};
    (void)bar.measure({1000.0f, Unbounded}, m, theme);
    bar.arrange({10.0f, 10.0f, 1000.0f, 40.0f});
    ui::testing::set_focused(bar, true); // the roving highlight starts at the first item
    bar.handle_key(key_down(Key::ArrowRight));
    ui::testing::set_focus_visible(bar, true);

    TestRenderer renderer;
    ui::testing::render_with_focus_ring(bar, renderer, theme);
    const StrokeRoundedRect* ring = nullptr;
    for (const RenderCommand& command : renderer.commands)
    {
        if (const auto* stroke = std::get_if<StrokeRoundedRect>(&command); stroke != nullptr && stroke->stroke_width == 2.0f)
        {
            ring = stroke;
        }
    }
    REQUIRE(ring != nullptr);
    const Rect item = *bar.item_bounds(1);
    // The outer ring's path is centered 1 inside its outer edge, so 2 out on every side.
    CHECK_THAT(ring->bounds.x, WithinAbs(item.x - 2.0f, 1e-3));
    CHECK_THAT(ring->bounds.y, WithinAbs(item.y - 2.0f, 1e-3));
    CHECK_THAT(ring->bounds.width, WithinAbs(item.width + 4.0f, 1e-3));
    CHECK_THAT(ring->bounds.height, WithinAbs(item.height + 4.0f, 1e-3));
}
