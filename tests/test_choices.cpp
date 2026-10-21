// SPDX-License-Identifier: MIT
// A check box bound to a plain bool, and ui::selects() choice groups.
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/input/ToggleButton.hpp"
#include "winui/ui/navigation/PipsPager.hpp"
#include "winui/ui/navigation/SelectorBar.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

namespace
{
enum class Pick
{
    Small,
    Medium,
    Large,
};

KeyEvent key_down(Key k, ModifierKeys modifiers = {})
{
    return {k, KeyAction::Down, modifiers};
}
} // namespace

// ---- a check box bound to a plain bool ----

TEST_CASE("A check box takes a Property<bool> two-way, and a mixed state writes nothing", "[choices][checkbox]")
{
    STATIC_REQUIRE(std::is_convertible_v<Property<bool>&, CheckedField>);
    STATIC_REQUIRE(std::is_convertible_v<TwoWayBinding<bool>, CheckedField>);
    STATIC_REQUIRE(std::is_convertible_v<Property<std::optional<bool>>&, CheckedField>);
    // Every other editable optional field still refuses a plain property (W18).
    STATIC_REQUIRE_FALSE(std::is_convertible_v<Property<bool>&, TwoWayField<std::optional<bool>>>);

    Property<bool>            agreed{false};
    ui::Control<ui::CheckBox> box;
    std::unique_ptr<Element>  tree = ui::build(ui::CheckBox{.text = "I agree", .checked = agreed, .control = box});
    REQUIRE(box->checked() == false);

    REQUIRE(ui::automation_providers(*box).toggle->toggle_next());
    CHECK(agreed.get());
    REQUIRE(ui::automation_providers(*box).toggle->toggle_next());
    CHECK_FALSE(agreed.get());

    agreed = true;
    CHECK(box->checked() == true);
    box->set_checked(std::nullopt); // shown mixed by the app: the bool keeps its value
    CHECK(agreed.get());
    REQUIRE(ui::automation_providers(*box).toggle->toggle_next()); // a two-state box checks from mixed
    CHECK(box->checked() == true);
    CHECK(agreed.get());
}

TEST_CASE("A select-all box shows mixed, and a click resolves it to checked", "[choices][checkbox]")
{
    Property<std::optional<bool>> all{std::nullopt};
    ui::Control<ui::CheckBox>     box;
    std::unique_ptr<Element>      tree = ui::build(ui::CheckBox{.text = "Select all", .checked = all, .control = box});
    REQUIRE_FALSE(box->checked().has_value());

    REQUIRE(ui::automation_providers(*box).toggle->toggle_next());
    CHECK(all.get() == true);
    REQUIRE(ui::automation_providers(*box).toggle->toggle_next());
    CHECK(all.get() == false); // never back to mixed without three_state
    REQUIRE(ui::automation_providers(*box).toggle->toggle_next());
    CHECK(all.get() == true);
}

TEST_CASE("A three-state box bound to an optional cycles through all three states", "[choices][checkbox]")
{
    Property<std::optional<bool>> sync{std::nullopt}; // follows the default
    ui::Control<ui::CheckBox>     box;
    std::unique_ptr<Element>      tree = ui::build(
        ui::CheckBox{.text = "Sync Photos", .checked = sync, .three_state = true, .control = box});

    REQUIRE(ui::automation_providers(*box).toggle->toggle_next());
    CHECK(sync.get() == false);
    REQUIRE(ui::automation_providers(*box).toggle->toggle_next());
    CHECK(sync.get() == true);
    REQUIRE(ui::automation_providers(*box).toggle->toggle_next());
    CHECK_FALSE(sync.get().has_value());
}

// ---- ui::selects() ----

TEST_CASE("ui::selects() checks the button whose value the property holds", "[choices][radio]")
{
    Property<Pick>               size{Pick::Medium};
    ui::Control<ui::RadioButton> small;
    ui::Control<ui::RadioButton> medium;
    ui::Control<ui::RadioButton> large;
    std::unique_ptr<Element>     tree = ui::build(ui::VerticalStack{
        .content = {
            ui::RadioButton{.text = "Small", .checked = ui::selects(size, Pick::Small), .control = small},
            ui::RadioButton{.text = "Medium", .checked = ui::selects(size, Pick::Medium), .control = medium},
            ui::RadioButton{.text = "Large", .checked = ui::selects(size, Pick::Large), .control = large},
        }});
    CHECK_FALSE(small->checked());
    CHECK(medium->checked());

    REQUIRE(ui::automation_providers(*large).selection_item->select_item());
    CHECK(size.get() == Pick::Large);
    CHECK(large->checked());
    CHECK_FALSE(medium->checked());

    size = Pick::Small;
    CHECK(small->checked());
    CHECK_FALSE(large->checked());

    // One property, one group; another property is another group.
    CHECK(small->selection_group() == &size);
    CHECK(large->selection_group() == small->selection_group());
}

TEST_CASE("A choice group is one Tab stop, and the arrow keys move and check within it", "[choices][radio][focus]")
{
    HeadlessUI                   ui = create_headless();
    Property<Pick>               size{Pick::Medium};
    ui::Control<ui::Button>      before;
    ui::Control<ui::Button>      after;
    ui::Control<ui::RadioButton> small;
    ui::Control<ui::RadioButton> medium;
    ui::Control<ui::RadioButton> large;
    ui.window().set_content(ui::VerticalStack{
        .content = {
            ui::Button{.text = "Before", .control = before},
            ui::RadioButton{.text = "Small", .checked = ui::selects(size, Pick::Small), .control = small},
            ui::RadioButton{.text = "Medium", .checked = ui::selects(size, Pick::Medium), .control = medium},
            ui::RadioButton{.text = "Large", .checked = ui::selects(size, Pick::Large), .control = large},
            ui::Button{.text = "After", .control = after},
        }});
    ui.layout({400, 600});

    Window& win = ui.window();
    win.focus_content();
    REQUIRE(win.focused() == before.get());

    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    CHECK(win.focused() == medium.get()); // the checked button is the group's stop
    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    CHECK(win.focused() == after.get()); // Tab leaves the group
    ui::testing::seams(win).handle_key(key_down(Key::Tab, {.shift = true}));
    CHECK(win.focused() == medium.get());

    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown));
    CHECK(win.focused() == large.get());
    CHECK(size.get() == Pick::Large);
    ui::testing::seams(win).handle_key(key_down(Key::ArrowDown)); // wraps
    CHECK(win.focused() == small.get());
    CHECK(size.get() == Pick::Small);
    ui::testing::seams(win).handle_key(key_down(Key::ArrowUp, {.ctrl = true})); // moves only
    CHECK(win.focused() == large.get());
    CHECK(size.get() == Pick::Small);

    ui::testing::seams(win).handle_key(key_down(Key::Tab, {.shift = true}));
    CHECK(win.focused() == before.get());
    ui::testing::seams(win).handle_key(key_down(Key::Tab));
    CHECK(win.focused() == small.get()); // the stop followed the selection
}

TEST_CASE("A ToggleButton bound with ui::selects() stays checked when clicked again", "[choices][toggle-button]")
{
    Property<Pick>                size{Pick::Small};
    ui::Control<ui::ToggleButton> small;
    ui::Control<ui::ToggleButton> large;
    std::unique_ptr<Element>      tree = ui::build(ui::HorizontalStack{
        .content = {
            ui::ToggleButton{.text = "S", .checked = ui::selects(size, Pick::Small), .control = small},
            ui::ToggleButton{.text = "L", .checked = ui::selects(size, Pick::Large), .control = large},
        }});
    REQUIRE(small->checked() == true);

    REQUIRE(ui::automation_providers(*small).toggle->toggle_next());
    CHECK(small->checked() == true);
    CHECK(size.get() == Pick::Small);

    REQUIRE(ui::automation_providers(*large).toggle->toggle_next());
    CHECK(size.get() == Pick::Large);
    CHECK(small->checked() == false);
}

// ---- ui::bind_choice() ----

TEST_CASE("ui::bind_choice() selects the position of the property's value, and a pick writes the value",
          "[choices][bind-choice]")
{
    Property<Pick>           size{Pick::Medium};
    Control<SelectorBar>     bar;
    std::unique_ptr<Element> built =
        SelectorBar{
            .items          = {"Small", "Medium", "Large"},
            .selected_index = bind_choice(size, {Pick::Small, Pick::Medium, Pick::Large}),
            .control        = bar,
        }
            .build();

    CHECK(bar->selected_index() == 1);
    size = Pick::Large;
    CHECK(bar->selected_index() == 2);
    bar->set_selected_index(0);
    CHECK(size.get() == Pick::Small);
}

TEST_CASE("ui::bind_choice() lists the values in any order, not only the enum's", "[choices][bind-choice]")
{
    Property<int>            page{3};
    Control<PipsPager>       pager;
    std::unique_ptr<Element> built =
        PipsPager{
            .selected_index  = bind_choice(page, {1, 2, 3}),
            .number_of_pages = 3,
            .control         = pager,
        }
            .build();

    CHECK(pager->selected_index() == 2);
    pager->set_selected_index(0);
    CHECK(page.get() == 1);
}

TEST_CASE("ui::bind_choice() shows no selection for a value not in the list where the field allows none",
          "[choices][bind-choice]")
{
    Property<std::string>    color{"Teal"};
    Control<ComboBox>        dropdown;
    std::unique_ptr<Element> built =
        ComboBox{
            .items          = {"Red", "Green", "Blue"},
            .selected_index = bind_choice(color, {"Red", "Green", "Blue"}),
            .control        = dropdown,
        }
            .build();

    CHECK_FALSE(dropdown->selected_index().has_value());
    color = "Blue";
    CHECK(dropdown->selected_index() == std::optional<std::size_t>{2});
    dropdown->set_selected_index(0);
    CHECK(color.get() == "Red");
    // Clearing the selection writes nothing.
    dropdown->set_selected_index(std::nullopt);
    CHECK(color.get() == "Red");
    color = "Teal";
    CHECK_FALSE(dropdown->selected_index().has_value());
}

TEST_CASE("ui::bind_choice() keeps the selection of a field that always has one", "[choices][bind-choice]")
{
    Property<std::string>    color{"Teal"};
    Control<SelectorBar>     bar;
    std::unique_ptr<Element> built =
        SelectorBar{
            .items          = {"Red", "Green", "Blue"},
            .selected_index = bind_choice(color, {"Red", "Green", "Blue"}),
            .control        = bar,
        }
            .build();

    CHECK(bar->selected_index() == 0); // starts on the first choice
    CHECK(color.get() == "Teal");      // which is not written back
    color = "Green";
    CHECK(bar->selected_index() == 1);
    color = "Teal";
    CHECK(bar->selected_index() == 1); // stays
}

TEST_CASE("ui::bind_choice() over a changing list follows the value through reorders", "[choices][bind-choice]")
{
    Property<std::vector<std::string>> names{std::vector<std::string>{"Ada", "Grace"}};
    Property<std::string>              chosen{"Grace"};
    Control<ComboBox>                  combo;
    std::unique_ptr<Element>           built =
        ComboBox{
            .items          = bind(names),
            .selected_index = bind_choice(chosen, names),
            .control        = combo,
        }
            .build();
    CHECK(combo->selected_index() == 1);

    names = std::vector<std::string>{"Grace", "Ada", "Hedy"}; // the selection moves with "Grace"
    CHECK(combo->selected_index() == 0);
    CHECK(chosen.get() == "Grace");

    combo->set_selected_index(2); // a pick writes the value at that position now
    CHECK(chosen.get() == "Hedy");

    names = std::vector<std::string>{"Ada"}; // gone from the list: nothing shows, the value stays
    CHECK(combo->selected_index() == std::nullopt);
    CHECK(chosen.get() == "Hedy");
}
