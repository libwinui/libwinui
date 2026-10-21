// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestAutomationEvents.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/collections/TableView.hpp"
#include "winui/ui/collections/TreeView.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/menus/MenuBar.hpp"
#include "winui/ui/navigation/Tabs.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Inlines.hpp"
#include "winui/ui/text/RichText.hpp"
#include "winui/ui/text/Text.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

namespace
{
KeyEvent press(Key key, ModifierKeys modifiers = {})
{
    return KeyEvent{key, KeyAction::Down, modifiers};
}

// A window around one control, laid out, reporting to `events`.
struct EventsWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    TestAutomationEvents events;

    explicit EventsWindow(std::unique_ptr<Element> content)
    {
        window.set_content(std::move(content));
        seams(window).set_automation_events(&events);
        relayout();
    }
    void relayout()
    {
        window.layout({500.0f, 400.0f}, measurer);
    }
    void key(Key k, ModifierKeys modifiers = {})
    {
        seams(window).handle_key(press(k, modifiers));
        seams(window).handle_key(KeyEvent{k, KeyAction::Up, modifiers});
        relayout();
    }
    [[nodiscard]] const std::string& last() const
    {
        static const std::string none = "(none)";
        return events.log.empty() ? none : events.log.back();
    }
};
} // namespace

TEST_CASE("Focus moving and a toggle flipping are reported, and nothing while no client listens", "[automation][events]")
{
    auto  stack = std::make_unique<StackControl>(Orientation::Vertical);
    auto* save  = stack->add(std::make_unique<ButtonControl>("Save"));
    stack->add(std::make_unique<CheckBoxControl>("Remember me"));
    EventsWindow w{std::move(stack)};
    (void)w.window.focus(*save);
    w.relayout();
    REQUIRE(w.events.log == std::vector<std::string>{"focus Button \"Save\""});

    w.key(Key::Tab);
    REQUIRE(w.last() == "focus CheckBox \"Remember me\"");
    w.key(Key::Space);
    REQUIRE(w.last() == "ToggleState \"Remember me\": Off -> On");

    const std::size_t before = w.events.log.size();
    w.events.listening_now   = false;
    w.key(Key::Tab);
    w.key(Key::Space);
    CHECK(w.events.log.size() == before);
}

TEST_CASE("The arrow keys in a tree report each row as focused", "[automation][events][tree_view]")
{
    auto tree = std::make_unique<TreeViewControl>();
    tree->set_roots({{.text = "Docs", .children = {{.text = "Plan"}}, .expanded = true}, {.text = "Notes"}});
    auto*        raw = tree.get();
    EventsWindow w{std::move(tree)};
    (void)w.window.focus(*raw);
    w.relayout();
    w.key(Key::ArrowDown);
    w.key(Key::ArrowDown);
    REQUIRE(w.last() == "focus TreeItem \"Plan\"");
    REQUIRE(automation_focus(w.window.focused())->automation_name() == "Plan");
    w.key(Key::ArrowDown);
    REQUIRE(w.last() == "focus TreeItem \"Notes\"");
}

TEST_CASE("A menu bar reports its header, then each item the keyboard reaches in the open menu",
          "[automation][events][menu]")
{
    auto bar = std::make_unique<MenuBarControl>();
    bar->set_items({MenuBarItem{.text = "File", .items = {{.text = "New"}, {.text = "Open"}}}});
    auto*        raw = bar.get();
    EventsWindow w{std::move(bar)};
    (void)w.window.focus(*raw);
    w.relayout();
    REQUIRE(w.last() == "focus MenuItem \"File\"");
    w.key(Key::ArrowDown); // opens with the first item focused
    REQUIRE(w.last() == "focus MenuItem \"New\"");
    w.key(Key::ArrowDown);
    REQUIRE(w.last() == "focus MenuItem \"Open\"");
    w.key(Key::Escape);
    REQUIRE(w.last() == "focus MenuItem \"File\"");
}

TEST_CASE("Tabs report the tab the arrow keys move to", "[automation][events][tabs]")
{
    auto tabs = std::make_unique<TabsControl>();
    tabs->add_tab("One", std::make_unique<TextControl>("1"));
    tabs->add_tab("Two", std::make_unique<TextControl>("2"));
    auto*        raw = tabs.get();
    EventsWindow w{std::move(tabs)};
    (void)w.window.focus(*raw);
    w.relayout();
    REQUIRE(w.last() == "focus TabItem \"One\"");
    w.key(Key::ArrowRight);
    REQUIRE(w.last() == "focus TabItem \"Two\"");
}

TEST_CASE("An open ComboBox lists its items and reports the highlighted one", "[automation][events][combo_box]")
{
    auto         combo = std::make_unique<ComboBoxControl>(std::vector<std::string>{"Red", "Green", "Blue"}, 0);
    auto*        raw   = combo.get();
    EventsWindow w{std::move(combo)};
    (void)w.window.focus(*raw);
    w.relayout();
    CHECK(automation_children(*raw).empty()); // closed: one node

    w.key(Key::ArrowDown, {.alt = true});
    REQUIRE(w.last() == "focus ListItem \"Red\"");
    const std::vector<Element*> top = automation_children(*raw);
    REQUIRE(top.size() == 1);
    REQUIRE(top[0]->automation_role() == AutomationRole::List);
    const std::vector<Element*> items = automation_children(*top[0]);
    REQUIRE(items.size() == 3);
    CHECK(automation_parent(*items[1]) == top[0]);
    CHECK(ui::automation_providers(*items[0]).selection_item->item_selected());

    w.key(Key::ArrowDown);
    REQUIRE(w.last() == "focus ListItem \"Green\"");

    // Selecting an item picks it and closes the list, as a click does.
    REQUIRE(ui::automation_providers(*items[2]).selection_item->select_item());
    w.relayout();
    CHECK(raw->selected_index() == 2);
    CHECK(automation_children(*raw).empty());
    REQUIRE(w.last() == "focus ComboBox \"\""); // back on the box, named by its header (none here)
    CHECK(std::get<std::string>(automation_value(*raw, AutomationProperty::Value)) == "Blue");
}

TEST_CASE("A table's row rebuilt by a layout is not reported again", "[automation][events][data-grid]")
{
    auto grid = std::make_unique<TableViewControl>();
    grid->set_columns({{.header = "Name", .width = 100.0f}});
    grid->set_rows({{"Ada"}, {"Grace"}});
    auto*        raw = grid.get();
    EventsWindow w{std::move(grid)};
    (void)w.window.focus(*raw);
    w.relayout();
    w.key(Key::ArrowDown);
    const std::size_t count = w.events.log.size();
    REQUIRE(w.last().starts_with("focus DataItem"));
    raw->set_column_width(0, 120.0f); // a layout that builds the rows again
    w.relayout();
    w.relayout();
    CHECK(w.events.log.size() == count);
}

TEST_CASE("RichText lists its hyperlink inlines as Hyperlinks that Invoke follows", "[automation][rich_text]")
{
    HeadlessTextMeasurer measurer;
    int                  clicks = 0;
    RichTextControl      text;
    text.set_blocks(
        {Paragraph{.inlines = {{.text = "Read the "}, {.text = "guide", .on_click = [&] { ++clicks; }}, {.text = "."}}}});
    (void)text.measure({400.0f, 200.0f}, measurer, Theme::default_light());
    text.arrange({10.0f, 10.0f, 400.0f, 200.0f});

    const std::vector<Element*> links = automation_children(text);
    REQUIRE(links.size() == 1);
    CHECK(links[0]->automation_role() == AutomationRole::Hyperlink);
    CHECK(links[0]->automation_name() == "guide");
    CHECK(automation_parent(*links[0]) == &text);
    CHECK(links[0]->bounds().width > 0.0f);
    REQUIRE(ui::automation_providers(*links[0]).invoke->invoke());
    CHECK(clicks == 1);
}

TEST_CASE("Menus opening and closing are reported, before the focus moves into them", "[automation][events][menu]")
{
    auto bar = std::make_unique<MenuBarControl>();
    bar->set_items({MenuBarItem{.text = "File", .items = {{.text = "New"}}}});
    auto*        raw = bar.get();
    EventsWindow w{std::move(bar)};
    (void)w.window.focus(*raw);
    w.relayout();
    const std::size_t start = w.events.log.size();
    w.key(Key::ArrowDown);
    REQUIRE(w.events.log.size() >= start + 2);
    CHECK(w.events.log[start].starts_with("menu opened"));
    CHECK(w.last() == "focus MenuItem \"New\"");
    CHECK_FALSE(w.events.structure.empty()); // the header's children changed: the menu came
    w.key(Key::Escape);
    w.window.tick(std::chrono::milliseconds(200)); // a menu counts as closed once its fade is done
    w.relayout();
    CHECK(std::ranges::count_if(w.events.log, [](const std::string& line) { return line.starts_with("menu closed"); }) == 1);
}

TEST_CASE("A row added next to the focused one is reported as a structure change", "[automation][events][tree_view]")
{
    auto tree = std::make_unique<TreeViewControl>();
    tree->set_roots({{.text = "A"}, {.text = "B"}});
    auto*        raw = tree.get();
    EventsWindow w{std::move(tree)};
    (void)w.window.focus(*raw);
    w.relayout();
    w.key(Key::ArrowDown);
    const std::size_t before = w.events.structure.size();
    w.relayout();
    CHECK(w.events.structure.size() == before); // nothing changed
    raw->set_roots({{.text = "A"}, {.text = "B"}, {.text = "C"}});
    w.relayout();
    REQUIRE(w.events.structure.size() == before + 1);
    CHECK(w.events.structure.back() == raw);
}
