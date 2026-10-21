// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/RadioButtons.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;

namespace
{
constexpr float ColumnSpacing = 7.0f; // between columns
constexpr float RowSpacing    = 8.0f; // between rows

std::unique_ptr<RadioButtonsControl> make_group(std::vector<std::string>   items,
                                                std::optional<std::size_t> selected = std::nullopt,
                                                float                      width    = 600.0f)
{
    auto                 group = std::make_unique<RadioButtonsControl>(std::move(items), selected);
    HeadlessTextMeasurer measurer;
    const Size           desired = group->measure({width, Unbounded}, measurer, Theme::default_light());
    group->arrange({0.0f, 0.0f, desired.width, desired.height});
    return group;
}

void relayout(RadioButtonsControl& group, float width = 600.0f)
{
    HeadlessTextMeasurer measurer;
    const Size           desired = group.measure({width, Unbounded}, measurer, Theme::default_light());
    group.arrange({0.0f, 0.0f, desired.width, desired.height});
}

void click(RadioButtonsControl& group, std::size_t index)
{
    const Point p = group.item(index)->bounds().center();
    group.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    group.handle_pointer({p, PointerButton::Left, PointerAction::Up});
}

void press(RadioButtonsControl& group, Key key, bool ctrl = false)
{
    KeyEvent down{key, KeyAction::Down};
    down.modifiers.ctrl = ctrl;
    KeyEvent up{key, KeyAction::Up};
    up.modifiers.ctrl = ctrl;
    group.handle_key(down);
    group.handle_key(up);
}
} // namespace

TEST_CASE("RadioButtons checks the item at selected_index and nothing else", "[radio_buttons]")
{
    RadioButtonsControl group{{"Green", "Yellow", "White"}, 1};
    CHECK(group.item_count() == 3);
    CHECK(group.selected_index() == 1);
    CHECK(group.selected_item() == "Yellow");
    CHECK_FALSE(group.item(0)->checked());
    CHECK(group.item(1)->checked());
    CHECK(group.item(3) == nullptr);

    std::vector<std::optional<std::size_t>> changes;
    group.on_selection_changed([&](std::optional<std::size_t> index) { changes.push_back(index); });
    group.set_selected_index(2);
    group.set_selected_index(2); // no change, no callback
    CHECK(group.item(2)->checked());
    CHECK_FALSE(group.item(1)->checked());

    group.set_selected_index(7); // out of range clears
    CHECK(group.selected_index() == std::nullopt);
    CHECK(group.selected_item().empty());
    CHECK(changes == std::vector<std::optional<std::size_t>>{2, std::nullopt});
}

TEST_CASE("Replacing the items clears the selection", "[radio_buttons]")
{
    RadioButtonsControl                     group{{"A", "B"}, 0};
    std::vector<std::optional<std::size_t>> changes;
    group.on_selection_changed([&](std::optional<std::size_t> index) { changes.push_back(index); });
    group.set_items({"C", "D", "E"});
    CHECK(group.item_count() == 3);
    CHECK(group.selected_index() == std::nullopt);
    CHECK(changes == std::vector<std::optional<std::size_t>>{std::nullopt});
}

TEST_CASE("Clicking an item selects it, and re-clicking changes nothing", "[radio_buttons][input]")
{
    auto                                    group = make_group({"Option 1", "Option 2", "Option 3"});
    std::vector<std::optional<std::size_t>> changes;
    group->on_selection_changed([&](std::optional<std::size_t> index) { changes.push_back(index); });

    click(*group, 2);
    CHECK(group->selected_index() == 2);
    click(*group, 2);
    click(*group, 0);
    CHECK(changes == std::vector<std::optional<std::size_t>>{2, 0});
    CHECK(group->item(0)->checked());
    CHECK_FALSE(group->item(2)->checked());
}

TEST_CASE("RadioButtons is one tab stop, entered on the selected item", "[radio_buttons][focus]")
{
    auto group = make_group({"A", "B", "C"}, 2);

    std::vector<Element*> ring;
    group->collect_focusables(ring);
    REQUIRE(ring.size() == 1);
    CHECK(ring[0] == group.get());
    CHECK(group->focused_index() == std::nullopt);

    ui::testing::set_focused(*group, true);
    CHECK(group->focused_index() == 2);
    CHECK(group->item(2)->focused());
    CHECK_FALSE(group->item(0)->focused());

    auto none = make_group({"A", "B", "C"});
    none->item(0)->set_enabled(false);
    ui::testing::set_focused(*none, true);
    CHECK(none->focused_index() == 1);
    CHECK(none->selected_index() == std::nullopt); // entering does not select
}

TEST_CASE("Up and Down move focus and selection together, skipping disabled items", "[radio_buttons][focus]")
{
    auto group = make_group({"A", "B", "C", "D"}, 0);
    group->item(2)->set_enabled(false);
    ui::testing::set_focused(*group, true);

    CHECK_FALSE(group->handle_key({Key::ArrowUp, KeyAction::Down})); // nothing above: focus leaves the group
    CHECK(group->focused_index() == 0);

    CHECK(group->handle_key({Key::ArrowDown, KeyAction::Down}));
    CHECK(group->focused_index() == 1);
    CHECK(group->selected_index() == 1);

    press(*group, Key::ArrowDown); // over the disabled C
    CHECK(group->focused_index() == 3);
    CHECK(group->selected_index() == 3);
    CHECK_FALSE(group->handle_key({Key::ArrowDown, KeyAction::Down}));

    press(*group, Key::ArrowUp);
    CHECK(group->selected_index() == 1);
}

TEST_CASE("Ctrl moves focus without selecting, and Space then selects", "[radio_buttons][focus]")
{
    auto group = make_group({"A", "B", "C"}, 0);
    ui::testing::set_focused(*group, true);

    press(*group, Key::ArrowDown, /*ctrl=*/true);
    CHECK(group->focused_index() == 1);
    CHECK(group->selected_index() == 0);

    press(*group, Key::Space);
    CHECK(group->selected_index() == 1);
}

TEST_CASE("Items fill columns top to bottom, the extra ones in the leftmost columns", "[radio_buttons][layout]")
{
    // Five items, at most two columns: [0 1 2] [3 4], every cell as big as the largest item.
    auto group = make_group({"One", "Two", "Three", "Four", "A much longer option"});
    group->set_max_columns(2);
    relayout(*group, 1000.0f);

    // A cell is an item's measured size, which includes the focus-ring band each item reserves
    // around its bounds, so the pitch is read off the positions rather than off bounds().
    const Rect  first  = group->item(0)->bounds();
    const float row    = group->item(1)->bounds().y - first.y;
    const float column = group->item(3)->bounds().x - first.x;
    CHECK(row > RowSpacing);
    CHECK(column > group->item(4)->bounds().width + ColumnSpacing - 1e-3f); // sized to the longest item
    CHECK(group->item(2)->bounds().y == first.y + 2.0f * row);
    CHECK(group->item(3)->bounds().y == first.y);
    CHECK(group->item(4)->bounds().x == first.x + column);
    CHECK(group->item(4)->bounds().y == first.y + row);
    const float cell_w = column - ColumnSpacing;

    ui::testing::set_focused(*group, true);
    group->set_selected_index(1);
    ui::testing::set_focused(*group, false);
    ui::testing::set_focused(*group, true);
    press(*group, Key::ArrowRight);
    CHECK(group->selected_index() == 4);
    CHECK_FALSE(group->handle_key({Key::ArrowRight, KeyAction::Down}));
    press(*group, Key::ArrowLeft);
    CHECK(group->selected_index() == 1);

    relayout(*group, cell_w + 1.0f);
    CHECK(group->item(3)->bounds().x == first.x);
    CHECK(group->item(3)->bounds().y == first.y + 3.0f * row);
    CHECK_FALSE(group->handle_key({Key::ArrowRight, KeyAction::Down}));
}

TEST_CASE("A header sits 8px above the items and names the group", "[radio_buttons][layout]")
{
    HeadlessTextMeasurer measurer;
    auto                 plain  = make_group({"A", "B"});
    const Rect           bare   = plain->item(0)->bounds();
    const float          height = plain->measure({600.0f, Unbounded}, measurer, Theme::default_light()).height;

    auto headed = make_group({"A", "B"});
    headed->set_header("Options:");
    relayout(*headed);
    const float line = measurer.measure("Options:", Theme::light().text_style).height;
    CHECK(headed->item(0)->bounds().y == bare.y + line + 8.0f);
    CHECK(headed->measure({600.0f, Unbounded}, measurer, Theme::default_light()).height == height + line + 8.0f);
    relayout(*headed);
    // bounds() is the group of options itself; the header sits above it.
    CHECK(headed->bounds().y == plain->bounds().y + line + 8.0f);

    CHECK(headed->automation_role() == AutomationRole::Group);
    CHECK(headed->automation_name() == "Options:");
    std::vector<Element*> children;
    headed->automation_children(children);
    CHECK(children.size() == 2);
}

TEST_CASE("The focused item draws its ring only while the group's focus is visible", "[radio_buttons][render]")
{
    auto       group    = make_group({"A", "B"}, 0);
    const auto commands = [&]
    {
        TestRenderer renderer;
        ui::testing::render_with_focus_ring(*group, renderer, Theme::light());
        return renderer.commands.size();
    };
    const std::size_t unfocused = commands();

    ui::testing::set_focused(*group, true);
    ui::testing::set_focus_visible(*group, true);
    const std::size_t ringed = commands();
    CHECK(ringed > unfocused);

    ui::testing::set_focus_visible(*group, false); // a click focused it: no ring, whatever the item's own flag says
    CHECK(commands() == unfocused);
}

TEST_CASE("A disabled RadioButtons disables every item and leaves the tab ring", "[radio_buttons][state]")
{
    auto group = make_group({"A", "B"}, 0);
    group->set_enabled(false);
    CHECK_FALSE(group->focusable());
    CHECK_FALSE(group->item(0)->enabled());
    CHECK_FALSE(group->item(1)->enabled());

    click(*group, 1);
    CHECK(group->selected_index() == 0);

    RadioButtonsControl empty;
    CHECK_FALSE(empty.focusable());
}

TEST_CASE("Inside a Window, Tab reaches the group once and the arrows select", "[radio_buttons][focus][window]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    auto                 stack  = std::make_unique<StackControl>(Orientation::Vertical);
    ButtonControl*       before = stack->add(std::make_unique<ButtonControl>("Before"));
    RadioButtonsControl* group  = stack->add(
        std::make_unique<RadioButtonsControl>(std::vector<std::string>{"A", "B", "C"}, 0));
    ButtonControl* after = stack->add(std::make_unique<ButtonControl>("After"));
    window.set_content(std::move(stack));
    window.layout({400.0f, 400.0f}, measurer);

    window.focus_content();
    REQUIRE(window.focused() == before);
    ui::testing::seams(window).handle_key({Key::Tab, KeyAction::Down});
    REQUIRE(window.focused() == group);
    CHECK(group->focused_index() == 0);

    ui::testing::seams(window).handle_key({Key::ArrowDown, KeyAction::Down});
    ui::testing::seams(window).handle_key({Key::ArrowDown, KeyAction::Up});
    CHECK(window.focused() == group);
    CHECK(group->selected_index() == 1);

    ui::testing::seams(window).handle_key({Key::Tab, KeyAction::Down});
    CHECK(window.focused() == after);
}
