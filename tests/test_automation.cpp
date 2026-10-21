// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestAutomationEvents.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/containers/Expander.hpp"
#include "winui/ui/hosting/NativeControl.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/navigation/Carousel.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;

TEST_CASE("describe_automation() dumps role/name/patterns for a simple tree", "[automation]")
{
    StackControl stack;
    stack.add(std::make_unique<ButtonControl>("Save"));
    stack.add(std::make_unique<CheckBoxControl>("Remember me", /*checked=*/false));

    const std::string dump = describe_automation(stack);
    REQUIRE(dump ==
            "Group\n"
            "  Button \"Save\" [Invoke]\n"
            "  CheckBox \"Remember me\" [Toggle=Off]\n");
}

TEST_CASE("automation_name() prefers accessibility().name over the control's own text", "[automation]")
{
    ButtonControl b{"Save"};
    REQUIRE(b.automation_name() == "Save");

    b.set_accessibility(Accessibility{.name = "Save document"});
    REQUIRE(b.automation_name() == "Save document");
}

TEST_CASE("A decorative node is spliced out of the automation tree, its children promoted", "[automation]")
{
    StackControl outer;
    auto         inner_owned = std::make_unique<StackControl>();
    inner_owned->set_accessibility(Accessibility{.decorative = true});
    StackControl*  inner = outer.add(std::move(inner_owned));
    ButtonControl* save  = inner->add(std::make_unique<ButtonControl>("Save"));

    const std::vector<Element*> children = automation_children(outer);
    REQUIRE(children.size() == 1);
    REQUIRE(children.front() == save);
    REQUIRE(automation_parent(*save) == &outer); // nearest non-decorative ancestor, skipping `inner`
}

TEST_CASE("A collapsed node and everything in it are left out of the automation tree", "[automation]")
{
    StackControl   outer;
    ButtonControl* save  = outer.add(std::make_unique<ButtonControl>("Save"));
    StackControl*  pager = outer.add(std::make_unique<StackControl>());
    (void)pager->add(std::make_unique<ButtonControl>("Next page"));
    ButtonControl* undo = outer.add(std::make_unique<ButtonControl>("Undo"));
    pager->set_visibility(Visibility::Collapsed);
    undo->set_visibility(Visibility::Collapsed);

    CHECK(automation_children(outer) == std::vector<Element*>{save});
    pager->set_visibility(Visibility::Visible);
    CHECK(automation_children(outer) == std::vector<Element*>{save, pager});
}

TEST_CASE("A NativeControl is excluded from the automation tree entirely", "[automation]")
{
    StackControl stack;
    stack.add(std::make_unique<ButtonControl>("Save"));
    stack.add(std::make_unique<NativeControl>(NativeControlDesc{.class_name = "EDIT"}, Size{100.0f, 24.0f}));

    const std::vector<Element*> children = automation_children(stack);
    REQUIRE(children.size() == 1); // the NativeControl itself never appears, and has no children to splice
    REQUIRE(children.front()->automation_role() == AutomationRole::Button);
}

TEST_CASE("A Button's InvokeProvider fires on_click", "[automation]")
{
    ButtonControl b{"Save"};
    bool          clicked = false;
    b.on_click([&] { clicked = true; });

    REQUIRE(ui::automation_providers(b).invoke != nullptr);
    REQUIRE(ui::automation_providers(b).toggle == nullptr);
    REQUIRE(ui::automation_providers(b).invoke->invoke());
    REQUIRE(clicked);
}

TEST_CASE("A Button's InvokeProvider no-ops while disabled", "[automation]")
{
    ButtonControl b{"Save"};
    bool          clicked = false;
    b.on_click([&] { clicked = true; });
    b.set_enabled(false);

    REQUIRE_FALSE(ui::automation_providers(b).invoke->invoke());
    REQUIRE_FALSE(clicked);
}

TEST_CASE("A CheckBox's ToggleProvider flips checked() and reports ToggleState", "[automation]")
{
    CheckBoxControl c{"Remember me", false};
    REQUIRE(ui::automation_providers(c).toggle->toggle_value() == ToggleState::Off);

    REQUIRE(ui::automation_providers(c).toggle->toggle_next());
    REQUIRE(c.checked() == true);
    REQUIRE(ui::automation_providers(c).toggle->toggle_value() == ToggleState::On);
}

TEST_CASE("A TextBox's ValueProvider changes text() and fires on_text_changed", "[automation]")
{
    TextBoxControl box{"old"};
    std::string    seen;
    box.on_text_changed([&](const std::string& t) { seen = t; });

    REQUIRE(ui::automation_providers(box).value != nullptr);
    REQUIRE(ui::automation_providers(box).invoke == nullptr);
    REQUIRE_FALSE(ui::automation_providers(box).value->text_value_read_only());
    REQUIRE(ui::automation_providers(box).value->set_text_value("new"));
    REQUIRE(box.text() == "new");
    REQUIRE(seen == "new");
}

TEST_CASE("Slider::automation_set_range_value() clamps into [min, max]", "[automation]")
{
    SliderControl s{Orientation::Horizontal, 0.0f, 10.0f, 5.0f};
    REQUIRE(ui::automation_providers(s).range_value->range_minimum() == 0.0);
    REQUIRE(ui::automation_providers(s).range_value->range_maximum() == 10.0);

    REQUIRE(ui::automation_providers(s).range_value->set_range_value(20.0));
    REQUIRE(s.value() == 10.0f); // SliderControl::set_value()'s own clamp
    REQUIRE(ui::automation_providers(s).range_value->range_value() == 10.0);
}

TEST_CASE("A read-only ComboBox exposes ExpandCollapse and a read-only Value naming the current selection",
          "[automation]")
{
    ComboBoxControl d{{"Small", "Medium", "Large"}, 1};
    REQUIRE(ui::automation_providers(d).expand_collapse != nullptr);
    REQUIRE(ui::automation_providers(d).value != nullptr);
    REQUIRE(ui::automation_providers(d).invoke == nullptr);
    REQUIRE(ui::automation_providers(d).value->text_value() == "Medium");
    REQUIRE_FALSE(ui::automation_providers(d).expand_collapse->expanded_state());

    REQUIRE(ui::automation_providers(d).expand_collapse->set_expanded_state(true));
    REQUIRE(d.expanded());
    REQUIRE(ui::automation_providers(d).expand_collapse->expanded_state());
}

TEST_CASE("ListView exposes a read-only Value naming the selected row", "[automation]")
{
    ListViewControl list;
    list.set_items({"Alpha", "Beta", "Gamma"});
    list.set_selected_index(2);
    REQUIRE(list.automation_role() == AutomationRole::List);
    REQUIRE(ui::automation_providers(list).value->text_value() == "Gamma");
}

TEST_CASE("ListView reports its selected rows through the Selection pattern", "[automation]")
{
    testing::HeadlessTextMeasurer m;
    ListViewControl               list;
    list.set_items({"Alpha", "Beta", "Gamma"});
    list.set_selected_index(1);
    (void)list.measure({200, 200}, m, Theme::default_light());
    list.arrange({0, 0, 200, 200});

    REQUIRE(ui::automation_providers(list).selection != nullptr);
    REQUIRE_FALSE(ui::automation_providers(list).selection->can_select_multiple());
    std::vector<Element*> selected;
    ui::automation_providers(list).selection->selected_items(selected);
    REQUIRE(selected.size() == 1);
    REQUIRE(selected.front()->automation_role() == AutomationRole::ListItem);
    REQUIRE(ui::automation_providers(*selected.front()).selection_item->item_selected());

    list.set_selection_mode(SelectionMode::Multiple);
    list.select(2);
    REQUIRE(ui::automation_providers(list).selection->can_select_multiple());
    selected.clear();
    ui::automation_providers(list).selection->selected_items(selected);
    REQUIRE(selected.size() == 2);
}

TEST_CASE("Carousel reports its current item through the Selection pattern", "[automation]")
{
    CarouselControl view;
    view.add_item(std::make_unique<TextControl>("One"));
    TextControl* two = view.add_item(std::make_unique<TextControl>("Two"));
    view.set_selected_index(1);

    REQUIRE(ui::automation_providers(view).selection->selection_required());
    std::vector<Element*> selected;
    ui::automation_providers(view).selection->selected_items(selected);
    REQUIRE(selected == std::vector<Element*>{two});
}

TEST_CASE("Expander's content only joins the automation tree while expanded", "[automation]")
{
    ExpanderControl expander{"Details", /*expanded=*/false};
    ButtonControl*  body = expander.set_content(std::make_unique<ButtonControl>("Inside"));

    REQUIRE(automation_children(expander).empty());
    REQUIRE(ui::automation_providers(expander).expand_collapse->set_expanded_state(true));
    const std::vector<Element*> children = automation_children(expander);
    REQUIRE(children.size() == 1);
    REQUIRE(children.front() == body);
}

// The examples of the accessibility guide (docs/tutorials/guides/accessibility.md), kept compiling.
TEST_CASE("The accessibility guide's form reads as the guide says", "[automation][guide]")
{
    testing::HeadlessUI ui     = testing::create_headless();
    ui::Window&         window = ui.window();
    window.set_content(ui::VerticalStack{
        .content =
            {
                ui::TextBox{.header = "Name"},
                ui::Slider{.accessibility = {.name = "Volume"}},
                ui::HorizontalStack{
                    .accessibility = {.decorative = true},
                    .content =
                        {
                            ui::Button{.text = "Save"},
                            ui::Button{.accessibility = {.name = "Delete"}, .content = ui::Icon{.source = ui::Symbol::Delete}},
                        },
                },
            },
    });
    ui.layout({400, 300});

    REQUIRE(ui::describe_automation(*window.content()) ==
            "Group\n"
            "  Edit \"Name\" [Value=\"\"]\n"
            "  Slider \"Volume\" [RangeValue=0.00]\n"
            "  Button \"Save\" [Invoke]\n"
            "  Button \"Delete\" [Invoke]\n");

    ui::testing::TestAutomationEvents events;
    ui::testing::seams(window).set_automation_events(&events);
    ui::testing::seams(window).handle_key({ui::Key::Tab, ui::KeyAction::Down});
    ui.layout({400, 300});
    REQUIRE(events.log == std::vector<std::string>{"focus Edit \"Name\""});
}

TEST_CASE("describe_automation() shows an automation id after the name", "[automation]")
{
    ButtonControl b{"Save"};
    b.set_accessibility({.automation_id = "save-button"});
    REQUIRE(describe_automation(b) == "Button \"Save\" #save-button [Invoke]\n");
}

TEST_CASE("The accessibility guide's automation ids show in the dump", "[automation][guide]")
{
    testing::HeadlessUI ui    = testing::create_headless();
    int                 saved = 0;
    const auto          save  = [&] { ++saved; };
    ui.window().set_content(ui::VerticalStack{
        .content =
            {
                ui::TextBox{.header = "Name", .accessibility = {.automation_id = "customer-name"}},
                ui::Button{.text = "Save", .accessibility = {.automation_id = "save"}, .on_click = [&] { save(); }},
            },
    });
    ui.layout({400, 300});
    REQUIRE(ui::describe_automation(*ui.window().content()) ==
            "Group\n"
            "  Edit \"Name\" #customer-name [Value=\"\"]\n"
            "  Button \"Save\" #save [Invoke]\n");
}

TEST_CASE("The accessibility guide's toggle acts as a screen reader's", "[automation][guide]")
{
    testing::HeadlessUI       ui = testing::create_headless();
    ui::Control<ui::CheckBox> remember;
    ui.window().set_content(ui::CheckBox{.text = "Remember me", .control = remember});
    ui.layout({400, 300});

    if (ui::ToggleProvider* toggle = ui::automation_providers(*remember).toggle)
    {
        toggle->toggle_next();
    }
    REQUIRE(remember->checked() == std::optional<bool>{true});
}
