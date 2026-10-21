// SPDX-License-Identifier: MIT
#include "winui/ui/Automation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/collections/TableView.hpp"
#include "winui/ui/collections/TreeView.hpp"
#include "winui/ui/containers/Expander.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/input/DropdownButton.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/input/Rating.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/input/SplitButton.hpp"
#include "winui/ui/input/ToggleButton.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/menus/AppBarButton.hpp"
#include "winui/ui/navigation/Breadcrumb.hpp"
#include "winui/ui/navigation/Carousel.hpp"
#include "winui/ui/navigation/PipsPager.hpp"
#include "winui/ui/navigation/Sections.hpp"
#include "winui/ui/navigation/SelectorBar.hpp"
#include "winui/ui/navigation/Tabs.hpp"
#include "winui/ui/scrolling/AnnotatedScrollBar.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/status/ProgressBar.hpp"
#include "winui/ui/status/ProgressRing.hpp"
#include "winui/ui/text/AutoSuggestBox.hpp"
#include "winui/ui/text/NumberBox.hpp"
#include "winui/ui/text/PasswordBox.hpp"
#include "winui/ui/text/RichEditBox.hpp"
#include "winui/ui/text/TextBox.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace ui;

namespace
{
// The patterns `element` provides, in a fixed order.
[[nodiscard]] std::string provided(const Element& element)
{
    const AutomationProviders p = automation_providers(element);
    std::string               out;
    const auto                add = [&out](const void* provider, const char* name)
    {
        if (provider != nullptr)
        {
            out += out.empty() ? name : std::string{" "} + name;
        }
    };
    add(p.invoke, "Invoke");
    add(p.toggle, "Toggle");
    add(p.selection_item, "SelectionItem");
    add(p.selection, "Selection");
    add(p.expand_collapse, "ExpandCollapse");
    add(p.range_value, "RangeValue");
    add(p.value, "Value");
    add(p.scroll, "Scroll");
    add(p.grid, "Grid");
    add(p.grid_item, "GridItem");
    add(p.table, "Table");
    add(p.table_item, "TableItem");
    return out;
}
} // namespace

// The patterns each control advertised before it moved to provider interfaces.
TEST_CASE("Each control provides the patterns it advertised before", "[automation][providers]")
{
    CHECK(provided(ButtonControl{"OK"}) == "Invoke");
    CHECK(provided(ToggleButtonControl{}) == "Toggle");
    CHECK(provided(CheckBoxControl{"Check"}) == "Toggle");
    CHECK(provided(RadioButtonControl{"Radio"}) == "SelectionItem");
    CHECK(provided(SliderControl{}) == "RangeValue");
    CHECK(provided(RatingControl{}) == "RangeValue");
    CHECK(provided(ProgressBarControl{}) == "RangeValue");
    CHECK(provided(ProgressRingControl{}) == "RangeValue");
    CHECK(provided(ComboBoxControl{{"One"}, 0}) == "ExpandCollapse Value");
    CHECK(provided(ComboBoxControl{{"One"}}) == "ExpandCollapse Value");
    CHECK(provided(DropdownButtonControl{}) == "Invoke ExpandCollapse");
    CHECK(provided(SplitButtonControl{}) == "Invoke ExpandCollapse");
    CHECK(provided(ToggleSplitButtonControl{}) == "Toggle ExpandCollapse");
    CHECK(provided(ToggleSwitchControl{}) == "Toggle");
    CHECK(provided(ExpanderControl{}) == "ExpandCollapse");
    CHECK(provided(TextBoxControl{}) == "Value");
    CHECK(provided(PasswordBoxControl{}) == "Value");
    CHECK(provided(NumberBoxControl{}) == "RangeValue");
    CHECK(provided(AutoSuggestBoxControl{}) == "Value");
    CHECK(provided(RichEditBoxControl{}) == "Value");
    CHECK(provided(ScrollControl{}) == "Scroll");
    CHECK(provided(SectionsControl{}) == "Value");
    CHECK(provided(TabsControl{}) == "Selection Value");
    CHECK(provided(SelectorBarControl{}) == "Value");
    CHECK(provided(BreadcrumbControl{}) == "Value");
    CHECK(provided(PipsPagerControl{}) == "RangeValue");
    CHECK(provided(CarouselControl{}) == "Selection");
    CHECK(provided(AppBarToggleButtonControl{}) == "Toggle");
    CHECK(provided(AnnotatedScrollBarControl{}) == "RangeValue");
    CHECK(provided(TreeViewControl{}) == "Value");
    CHECK(provided(ListViewControl{}) == "Selection Value");
    CHECK(provided(TableViewControl{}) == "Selection Grid Table");
    CHECK(provided(TextControl{"plain"}).empty());
}

TEST_CASE("A provider drives the control the way a client would", "[automation][providers]")
{
    SliderControl slider;
    slider.set_range(0.0, 10.0);
    RangeValueProvider* range = automation_providers(slider).range_value;
    REQUIRE(range != nullptr);
    CHECK(range->set_range_value(4.0));
    CHECK(slider.value() == 4.0);
    CHECK(range->range_maximum() == 10.0);
    CHECK_FALSE(range->range_read_only());

    slider.set_enabled(false);
    CHECK_FALSE(range->set_range_value(6.0)); // refused while disabled
    CHECK(slider.value() == 4.0);
}
