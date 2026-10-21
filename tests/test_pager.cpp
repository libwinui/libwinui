// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/navigation/Pager.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/NumberBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
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
std::unique_ptr<PagerControl> make_pager(std::optional<std::size_t> pages,
                                         int                        selected = 0,
                                         PagerDisplayMode           mode     = PagerDisplayMode::Auto)
{
    auto pager = std::make_unique<PagerControl>();
    pager->set_display_mode(mode);
    pager->set_number_of_pages(pages);
    pager->set_selected_index(selected);
    HeadlessTextMeasurer m;
    const Size           size = pager->measure({Unbounded, Unbounded}, m, Theme::default_light());
    pager->arrange({0, 0, size.width, size.height});
    return pager;
}

void click(Element& root, ButtonControl* button)
{
    REQUIRE(button != nullptr);
    const Point p = button->bounds().center();
    root.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    root.handle_pointer({p, PointerButton::Left, PointerAction::Up});
}
} // namespace

TEST_CASE("PagerControl defaults to no pages, Auto mode, and the default strings", "[pager-control]")
{
    PagerControl pager;
    REQUIRE(pager.number_of_pages() == 0);
    REQUIRE(pager.selected_index() == 0);
    REQUIRE(pager.display_mode() == PagerDisplayMode::Auto);
    REQUIRE(pager.prefix_text() == "Page");
    REQUIRE(pager.suffix_text() == "of");
    REQUIRE(pager.first_button_visibility() == PagerButtonVisibility::Visible);
    REQUIRE(pager.button_panel_always_show_first_last_page_index());
    REQUIRE(pager.first_button()->accessibility().name == "First page");
    REQUIRE(pager.last_button()->accessibility().name == "Last page");
}

TEST_CASE("Auto is a ComboBox under ten pages and a NumberBox from ten or unknown", "[pager-control]")
{
    auto pager = make_pager(9);
    REQUIRE(pager->effective_display_mode() == PagerDisplayMode::ComboBox);
    pager->set_number_of_pages(10);
    REQUIRE(pager->effective_display_mode() == PagerDisplayMode::NumberBox);
    pager->set_number_of_pages(std::nullopt);
    REQUIRE(pager->effective_display_mode() == PagerDisplayMode::NumberBox);
}

TEST_CASE("The navigation buttons move the selection and disable at the edges", "[pager-control][input]")
{
    auto                     pager = make_pager(12, 2, PagerDisplayMode::NumberBox);
    std::vector<std::size_t> changes;
    pager->on_selection_changed([&](std::size_t now) { changes.push_back(now); });

    click(*pager, pager->next_button());
    REQUIRE(pager->selected_index() == 3);
    REQUIRE(changes == std::vector<std::size_t>{3});
    click(*pager, pager->last_button());
    REQUIRE(pager->selected_index() == 11);
    REQUIRE_FALSE(pager->next_button()->enabled());
    REQUIRE_FALSE(pager->last_button()->enabled());
    REQUIRE(pager->first_button()->enabled());

    click(*pager, pager->previous_button());
    REQUIRE(pager->selected_index() == 10);
    click(*pager, pager->first_button());
    REQUIRE(pager->selected_index() == 0);
    REQUIRE_FALSE(pager->first_button()->enabled());
    REQUIRE_FALSE(pager->previous_button()->enabled());

    // Page numbers are one-based.
    REQUIRE_THAT(pager->number_box()->value().value(), WithinAbs(1.0, 1e-9));
    pager->number_box()->set_value(7.0);
    REQUIRE(pager->selected_index() == 6);
}

TEST_CASE("The combo box lists every page and selects through", "[pager-control]")
{
    auto pager = make_pager(5, 1, PagerDisplayMode::ComboBox);
    REQUIRE(pager->combo_box()->items().size() == 5);
    REQUIRE(pager->combo_box()->selected_index() == 1);
    pager->combo_box()->set_selected_index(4);
    REQUIRE(pager->selected_index() == 4);
}

TEST_CASE("HiddenOnEdge hides a button on its own edge but keeps its room; Hidden collapses it",
          "[pager-control][layout]")
{
    HeadlessTextMeasurer m;
    auto                 pager   = make_pager(20, 0, PagerDisplayMode::NumberBox);
    const Size           visible = pager->measure({Unbounded, Unbounded}, m, Theme::default_light());

    pager->set_first_button_visibility(PagerButtonVisibility::HiddenOnEdge);
    REQUIRE_FALSE(pager->first_button_visible());
    REQUIRE(pager->measure({Unbounded, Unbounded}, m, Theme::default_light()).width == visible.width);
    pager->set_selected_index(3);
    REQUIRE(pager->first_button_visible());

    pager->set_first_button_visibility(PagerButtonVisibility::Hidden);
    REQUIRE_FALSE(pager->first_button_visible());
    REQUIRE(pager->measure({Unbounded, Unbounded}, m, Theme::default_light()).width ==
            visible.width); // the right pair still sets the sides
    pager->set_last_button_visibility(PagerButtonVisibility::Hidden);
    REQUIRE(pager->measure({Unbounded, Unbounded}, m, Theme::default_light()).width < visible.width);
}

TEST_CASE("The button panel follows UpdateNumberPanel's patterns", "[pager-control][button-panel]")
{
    auto pager = make_pager(5, 2, PagerDisplayMode::ButtonPanel);
    REQUIRE(pager->button_panel_items() == std::vector<int>{1, 2, 3, 4, 5});
    REQUIRE(pager->button_panel_selected_item() == 2);

    pager->set_number_of_pages(20);
    pager->set_selected_index(1);
    REQUIRE(pager->button_panel_items() == std::vector<int>{1, 2, 3, 4, 5, 0, 20});
    REQUIRE(pager->button_panel_selected_item() == 1);

    pager->set_selected_index(9); // page 10: 1 … 9 10 11 … 20
    REQUIRE(pager->button_panel_items() == std::vector<int>{1, 0, 9, 10, 11, 0, 20});
    REQUIRE(pager->button_panel_selected_item() == 3);

    pager->set_selected_index(18); // page 19: 1 … 16 17 18 19 20
    REQUIRE(pager->button_panel_items() == std::vector<int>{1, 0, 16, 17, 18, 19, 20});
    REQUIRE(pager->button_panel_selected_item() == 5);

    pager->set_button_panel_always_show_first_last_page_index(false);
    REQUIRE(pager->button_panel_items() == std::vector<int>{16, 17, 18, 19, 20});
    REQUIRE(pager->button_panel_selected_item() == 3);
    pager->set_selected_index(9);
    REQUIRE(pager->button_panel_items() == std::vector<int>{9, 10, 11});
    REQUIRE(pager->button_panel_selected_item() == 1);
}

TEST_CASE("A page button selects its page and is relabeled in place", "[pager-control][button-panel][input]")
{
    HeadlessTextMeasurer m;
    auto                 pager = make_pager(20, 0, PagerDisplayMode::ButtonPanel);
    ButtonControl*       four  = pager->button_panel_button(3);
    REQUIRE(four->text() == "4");
    click(*pager, four);
    REQUIRE(pager->selected_index() == 3);
    REQUIRE(pager->button_panel_items() == std::vector<int>{1, 2, 3, 4, 5, 0, 20});
    REQUIRE(pager->button_panel_selected_item() == 3);
    click(*pager, pager->button_panel_button(4));
    // Page 5 moves to the centered pattern, reusing the same button objects.
    REQUIRE(pager->selected_index() == 4);
    REQUIRE(pager->button_panel_items() == std::vector<int>{1, 0, 4, 5, 6, 0, 20});
    REQUIRE(pager->button_panel_button(3) == four);
    REQUIRE(four->text() == "5");

    const Size size = pager->measure({Unbounded, Unbounded}, m, Theme::default_light());
    pager->arrange({0, 0, size.width, size.height});
    TestRenderer r;
    pager->render(r, Theme::light());
    bool found = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&c); fill != nullptr && fill->bounds.height == 2.0f)
        {
            found = fill->bounds.bottom() == size.height && fill->bounds.x == four->bounds().x;
        }
    }
    REQUIRE(found);
}

TEST_CASE("An unknown page count drops the total and the Last button", "[pager-control][infinite]")
{
    auto pager = make_pager(std::nullopt, 99, PagerDisplayMode::NumberBox);
    REQUIRE(pager->selected_index() == 99);
    REQUIRE_FALSE(pager->last_button_visible());
    REQUIRE(pager->next_button()->enabled());
    click(*pager, pager->next_button());
    REQUIRE(pager->selected_index() == 100);

    auto panel = make_pager(std::nullopt, 99, PagerDisplayMode::ButtonPanel);
    REQUIRE(panel->button_panel_items() == std::vector<int>{1, 0, 99, 100, 101});
    REQUIRE(panel->button_panel_selected_item() == 3);

    auto combo = make_pager(std::nullopt, 0, PagerDisplayMode::ComboBox);
    REQUIRE(combo->combo_box()->items().size() == 100);
}

TEST_CASE("Fewer pages clamp the selection to the last one", "[pager-control]")
{
    auto pager = make_pager(12, 11);
    pager->set_number_of_pages(4);
    REQUIRE(pager->selected_index() == 3);
}

TEST_CASE("ui::Pager builds a PagerControl with its properties", "[pager-control][spec]")
{
    ui::Control<ui::Pager> ref;
    auto                   node = ui::build(ui::Pager{.selected_index  = 9,
                                                      .number_of_pages = 50,
                                                      .display_mode    = PagerDisplayMode::NumberBox,
                                                      .prefix_text     = "Page",
                                                      .control         = ref});
    REQUIRE(ref != nullptr);
    REQUIRE(ref->number_of_pages() == 50);
    REQUIRE(ref->selected_index() == 9);
    REQUIRE(ref->effective_display_mode() == PagerDisplayMode::NumberBox);
}
