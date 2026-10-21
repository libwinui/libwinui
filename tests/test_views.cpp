// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/DragDrop.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/collections/ListViewBase.hpp"
#include "winui/ui/collections/TableView.hpp"
#include "winui/ui/collections/TileView.hpp"
#include "winui/ui/layouts/TileLayout.hpp"
#include "winui/ui/layouts/WrapLayout.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;

namespace
{
struct Contact
{
    int         id = 0;
    std::string name;
    friend bool operator==(const Contact&, const Contact&) = default;
};

std::vector<Contact> contacts(int count)
{
    std::vector<Contact> all;
    all.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        all.push_back({i, "Contact " + std::to_string(i)});
    }
    return all;
}

std::vector<std::string> names_of(const std::vector<Contact>& all)
{
    std::vector<std::string> out;
    out.reserve(all.size());
    for (const Contact& contact : all)
    {
        out.push_back(contact.name);
    }
    return out;
}

void lay_out(Element& root, Size size = {300.0f, 400.0f})
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();
    (void)root.measure(size, measurer, theme);
    root.arrange(Rect{0.0f, 0.0f, size.width, size.height});
}

Rect item_rect(const ListViewBase& list, std::size_t index)
{
    const Element* e = list.repeater()->try_get_element(index);
    return e != nullptr ? e->bounds() : Rect{};
}

void click(ListViewBase& list, std::size_t index)
{
    const Point p = item_rect(list, index).center();
    list.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    list.handle_pointer({p, PointerButton::Left, PointerAction::Up});
}

// The text of the realized row at `index`, looking through the item container.
std::string row_text(const ListViewBase& list, std::size_t index)
{
    const Element* container = list.repeater()->try_get_element(index);
    if (container == nullptr)
    {
        return "<not realized>";
    }
    std::vector<Element*> content;
    container->automation_children(content);
    const auto* text = content.empty() ? nullptr : dynamic_cast<const TextControl*>(content.front());
    return text != nullptr ? text->text() : "<not a text>";
}

// Drags the row at `from` to just past the middle of the row at `onto`, through the window.
void drag_row(Window& window, const ListViewBase& list, std::size_t from, std::size_t onto)
{
    const Point start = item_rect(list, from).center();
    ui::testing::seams(window).handle_pointer({start, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(window).handle_pointer({{start.x, start.y + 10.0f}, PointerButton::None, PointerAction::Move});
    REQUIRE(DragDrop::active());
    const Rect target = item_rect(list, onto);
    ui::testing::seams(window).handle_pointer(
        {{start.x, target.center().y + 5.0f}, PointerButton::None, PointerAction::Move});
    ui::testing::seams(window).handle_pointer({{start.x, target.center().y + 5.0f}, PointerButton::Left, PointerAction::Up});
    REQUIRE_FALSE(DragDrop::active());
}
} // namespace

TEST_CASE("a typed ListView deduces its item type and hands the item to its handlers", "[spec][views]")
{
    Property<std::vector<Contact>> model{contacts(5)};
    std::vector<std::string>       clicked;
    std::vector<std::string>       selected;

    auto spec = ui::ListView{
        .items                = ui::bind(model),
        .key                  = &Contact::id,
        .item_click_enabled   = true,
        .on_selection_changed = [&](std::optional<Contact> contact)
        { selected.push_back(contact ? contact->name : "<none>"); },
        .on_item_click = [&](const Contact& contact) { clicked.push_back(contact.name); },
        .item          = [](const Contact& contact) -> ui::Spec { return ui::Text{.text = contact.name}; },
    };
    static_assert(std::is_same_v<decltype(spec), ui::ListView<Contact>>);

    ui::Control<ui::ListView<Contact>> list;
    spec.control                  = list;
    std::unique_ptr<Element> tree = std::move(spec).build();
    lay_out(*tree);
    REQUIRE(list->item_count() == 5);
    CHECK(row_text(*list, 2) == "Contact 2");

    click(*list, 3);
    CHECK(clicked == std::vector<std::string>{"Contact 3"});
    CHECK(selected == std::vector<std::string>{"Contact 3"});

    // Reversed: the selection is still on Contact 3, now the second row, and nobody is told.
    model.update([](std::vector<Contact>& v) { std::reverse(v.begin(), v.end()); });
    lay_out(*tree);
    CHECK(list->selected_index() == 1);
    CHECK(row_text(*list, 1) == "Contact 3");
    CHECK(selected.size() == 1);

    // The handlers read the collection as it is now.
    click(*list, 0);
    CHECK(clicked.back() == "Contact 4");
    CHECK(selected.back() == "Contact 4");

    // Removing the selected item leaves nothing selected, and the handler, which reports the
    // user's changes, isn't told.
    const std::size_t reports = selected.size();
    model.update([](std::vector<Contact>& v) { v.erase(v.begin()); });
    CHECK(list->item_count() == 4);
    CHECK(list->selected_index() == std::nullopt);
    CHECK(selected.size() == reports);
}

TEST_CASE("a ListView of strings needs no item template", "[spec][views]")
{
    // A fixed list keeps the control's own text rows.
    ui::Control<ui::ListView<std::string>> fixed;
    std::unique_ptr<Element>               tree = ui::build(
        ui::ListView<std::string>{.items = {"Inbox", "Drafts", "Sent"}, .control = fixed});
    CHECK(fixed->items() == std::vector<std::string>{"Inbox", "Drafts", "Sent"});
    CHECK(fixed->selected_index() == std::nullopt);

    // A bound one shows each string as text and follows the property.
    Property<std::vector<std::string>>     folders{std::vector<std::string>{"Inbox", "Drafts"}};
    std::string                            picked;
    ui::Control<ui::ListView<std::string>> live;
    std::unique_ptr<Element>               bound = ui::build(ui::ListView{
        .items                = ui::bind(folders),
        .on_selection_changed = [&](std::optional<std::string> folder) { picked = folder ? *folder : ""; },
        .control              = live,
    });
    lay_out(*bound);
    REQUIRE(live->item_count() == 2);
    CHECK(row_text(*live, 1) == "Drafts");
    folders.update([](std::vector<std::string>& v) { v.push_back("Sent"); });
    lay_out(*bound);
    CHECK(live->item_count() == 3);
    CHECK(row_text(*live, 2) == "Sent");
    live->set_selected_index(2);
    CHECK(picked == "Sent");
}

TEST_CASE("a typed ListView follows an ObservableVector and lets a row edit its item", "[spec][views]")
{
    struct Task
    {
        explicit Task(std::string text) : title{std::move(text)}
        {
        }
        Property<std::string> title;
    };
    ObservableVector<Task> tasks;
    tasks.emplace_back("a");
    tasks.emplace_back("b");

    std::vector<Task*>              seen;
    ui::Control<ui::ListView<Task>> list;
    std::unique_ptr<Element>        tree = ui::build(ui::ListView{
        .items   = ui::bind(tasks),
        .control = list,
        .item    = [&seen](Task& task) -> ui::Spec
        {
            seen.push_back(&task);
            return ui::Text{.text = ui::bind(task.title)};
        },
    });
    lay_out(*tree);
    REQUIRE(list->item_count() == 2);
    // The items themselves, not copies.
    REQUIRE_FALSE(seen.empty());
    CHECK(std::all_of(seen.begin(), seen.end(), [&](const Task* task) { return task == &tasks[0] || task == &tasks[1]; }));
    CHECK(seen.back() == &tasks[1]);

    tasks[1].title = "B";
    CHECK(row_text(*list, 1) == "B");

    tasks.emplace(0, "first");
    lay_out(*tree);
    CHECK(list->item_count() == 3);
    CHECK(row_text(*list, 0) == "first");
    CHECK(row_text(*list, 2) == "B");

    tree.reset();
    CHECK(tasks.observer_count() == 0);
}

TEST_CASE("a reorder in a typed ListView is written back to its collection", "[spec][views][drag]")
{
    SECTION("an ObservableVector is moved in place")
    {
        ObservableVector<std::string> names{"A", "B", "C", "D", "E"};
        const std::string*            first = &names[0];
        int                           moves = 0;
        Subscription                  watch = names.observe(
            [&](const CollectionChange& change) { moves += change.kind == CollectionChangeKind::Moved ? 1 : 0; });
        std::vector<std::pair<std::size_t, std::size_t>> reordered;
        ui::Control<ui::ListView<std::string>>           list;

        HeadlessUI ui = create_headless();
        ui.window().set_content(ui::ListView{
            .items              = ui::bind(names),
            .item_height        = 40.0f,
            .can_drag_items     = true,
            .can_reorder_items  = true,
            .allow_drop         = true,
            .width              = 200.0f,
            .height             = 300.0f,
            .on_items_reordered = [&](const ItemsReorderedArgs& args) { reordered.emplace_back(args.from, args.to); },
            .control            = list,
        });
        ui.layout({400.0f, 400.0f});

        drag_row(ui.window(), *list, 0, 3);
        CHECK(std::vector<std::string>(names.begin(), names.end()) == std::vector<std::string>{"B", "C", "D", "A", "E"});
        CHECK(&names[3] == first); // moved, not copied
        CHECK(moves == 1);
        CHECK(reordered == std::vector<std::pair<std::size_t, std::size_t>>{{0, 3}});
        ui.layout({400.0f, 400.0f});
        CHECK(list->item_count() == 5);
        CHECK(row_text(*list, 3) == "A");
        CHECK(row_text(*list, 0) == "B");
    }
    SECTION("a bound property is assigned the reordered vector")
    {
        Property<std::vector<Contact>>     model{contacts(5)};
        ui::Control<ui::ListView<Contact>> list;

        HeadlessUI ui = create_headless();
        ui.window().set_content(ui::ListView{
            .items             = ui::bind(model),
            .key               = &Contact::id,
            .item_height       = 40.0f,
            .can_drag_items    = true,
            .can_reorder_items = true,
            .allow_drop        = true,
            .width             = 200.0f,
            .height            = 300.0f,
            .control           = list,
            .item              = [](const Contact& contact) -> ui::Spec { return ui::Text{.text = contact.name}; },
        });
        ui.layout({400.0f, 400.0f});

        drag_row(ui.window(), *list, 0, 3);
        CHECK(names_of(model.get()) ==
              std::vector<std::string>{"Contact 1", "Contact 2", "Contact 3", "Contact 0", "Contact 4"});
        ui.layout({400.0f, 400.0f});
        CHECK(row_text(*list, 3) == "Contact 0");
        CHECK(row_text(*list, 0) == "Contact 1");
    }
}

TEST_CASE("a typed TileView hands the item to its click and invoked handlers", "[spec][views]")
{
    std::vector<int>               numbers{10, 20, 30, 40};
    int                            clicked = 0;
    int                            invoked = 0;
    ui::Control<ui::TileView<int>> tiles;

    std::unique_ptr<Element> tree = ui::build(ui::VerticalStack{
        .content = {
            ui::TileView<int>{
                .items                = numbers,
                .item_click_enabled   = true,
                .item_invoked_enabled = true,
                .height               = 100.0f,
                .on_item_click        = [&](const int& n) { clicked = n; },
                .on_item_invoked      = [&](const int& n) { invoked = n; },
                .control              = tiles,
                .item                 = [](const int& n) -> ui::Spec
                { return ui::Text{.text = std::to_string(n), .width = 40.0f, .height = 40.0f}; },
            },
        }});
    lay_out(*tree);
    CHECK(tiles->item_count() == 4);

    click(*tiles, 2);
    CHECK(clicked == 30);
    CHECK(invoked == 0); // a single click selects
    ui::testing::set_focused(*tiles, true);
    tiles->handle_key(KeyEvent{Key::Enter, KeyAction::Down});
    CHECK(invoked == 30);
}

TEST_CASE("a typed TableView shows a row per item and follows its collection", "[spec][views]")
{
    Property<std::vector<Contact>>      model{contacts(3)};
    std::string                         picked = "<untouched>";
    ui::Control<ui::TableView<Contact>> table;

    auto spec = ui::TableView{
        .items = ui::bind(model),
        .columns =
            {
                {.header = "Id",
                 .width  = 60.0f,
                 .text   = [](const Contact& contact) { return std::to_string(contact.id); }},
                {.header = "Name", .width = 160.0f, .text = [](const Contact& contact) { return contact.name; }},
            },
        .on_selection_changed = [&](std::optional<Contact> contact) { picked = contact ? contact->name : "<none>"; },
        .control              = table,
    };
    static_assert(std::is_same_v<decltype(spec), ui::TableView<Contact>>);
    std::unique_ptr<Element> tree = std::move(spec).build();

    REQUIRE(table->rows().size() == 3);
    CHECK(table->rows()[1] == std::vector<std::string>{"1", "Contact 1"});

    table->set_selected_row(1);
    CHECK(picked == "Contact 1");

    // The rows are read again, and the selected row stays selected.
    model.update([](std::vector<Contact>& v) { v.push_back({7, "Seven"}); });
    REQUIRE(table->rows().size() == 4);
    CHECK(table->rows()[3] == std::vector<std::string>{"7", "Seven"});
    CHECK(table->selected_row() == 1);

    // An ObservableVector is followed the same way.
    ObservableVector<std::string>           names{"x"};
    ui::Control<ui::TableView<std::string>> plain;
    std::unique_ptr<Element>                other = ui::build(ui::TableView{
        .items   = ui::bind(names),
        .columns = {{.header = "Name", .width = 100.0f, .text = [](const std::string& name) { return name; }}},
        .control = plain,
    });
    names.push_back("y");
    CHECK(plain->rows().size() == 2);
    names.clear();
    CHECK(plain->rows().empty());
}

TEST_CASE("a TileView keeps its tile layout's orientation and cap unless the spec sets them", "[spec][views]")
{
    ui::Control<ui::TileView<std::string>> kept;
    std::unique_ptr<Element>               a = ui::build(ui::TileView<std::string>{
        .items   = {"a", "b", "c"},
        .layout  = ui::TileLayout{.orientation = ui::Orientation::Vertical, .maximum_rows_or_columns = 3},
        .control = kept,
    });
    CHECK(kept->orientation() == ui::Orientation::Vertical);
    CHECK(kept->maximum_rows_or_columns() == 3);
    CHECK(kept->scroll_view()->content_orientation() == ui::ScrollContentOrientation::Horizontal);

    ui::Control<ui::TileView<std::string>> overridden;
    std::unique_ptr<Element>               b = ui::build(ui::TileView<std::string>{
        .items                   = {"a", "b", "c"},
        .maximum_rows_or_columns = 2,
        .layout      = ui::TileLayout{.orientation = ui::Orientation::Vertical, .maximum_rows_or_columns = 3},
        .orientation = ui::Orientation::Horizontal,
        .control     = overridden,
    });
    CHECK(overridden->orientation() == ui::Orientation::Horizontal);
    CHECK(overridden->maximum_rows_or_columns() == 2);

    ui::Control<ui::TileView<std::string>> plain;
    std::unique_ptr<Element>               c = ui::build(ui::TileView<std::string>{.items = {"a"}, .control = plain});
    CHECK(plain->orientation() == ui::Orientation::Horizontal);
    CHECK(plain->maximum_rows_or_columns() == 0);
}

TEST_CASE("a TileView takes a layout of its own and still selects, navigates and reorders", "[spec][views][drag]")
{
    // Cells of their own width: 60, 120, 60, 120, ... wrapping in 300.
    ObservableVector<int>          widths{60, 120, 60, 120, 60, 120};
    ui::Control<ui::TileView<int>> tiles;

    HeadlessUI ui = create_headless();
    ui.window().set_content(ui::TileView{
        .items                 = ui::bind(widths),
        .can_drag_items        = true,
        .can_reorder_items     = true,
        .allow_drop            = true,
        .layout                = ui::WrapLayout{},
        .item_container_margin = std::optional<ui::Thickness>{ui::Thickness{}},
        .width                 = 300.0f,
        .height                = 300.0f,
        .horizontal_alignment  = ui::Alignment::Leading,
        .control               = tiles,
        .item                  = [](const int& width) -> ui::Spec
        { return ui::Text{.text = std::to_string(width), .width = static_cast<float>(width), .height = 40.0f}; },
    });
    ui.layout({400.0f, 400.0f});
    REQUIRE(dynamic_cast<WrapLayoutEngine*>(tiles->repeater()->layout()) != nullptr);
    CHECK(tiles->maximum_rows_or_columns() == 0); // nothing to ask a wrap layout

    // 60 + 120 + 60 fit in 300, the fourth cell starts the second line.
    CHECK(item_rect(*tiles, 1).x > item_rect(*tiles, 0).x);
    CHECK(item_rect(*tiles, 2).y == item_rect(*tiles, 0).y);
    CHECK(item_rect(*tiles, 3).y > item_rect(*tiles, 0).y);
    CHECK(item_rect(*tiles, 1).width > item_rect(*tiles, 0).width);

    // Down from the first cell goes to the cell below it, up comes back.
    Window&     window = ui.window();
    const Point first  = item_rect(*tiles, 0).center();
    seams(window).handle_pointer({first, PointerButton::Left, PointerAction::Down});
    seams(window).handle_pointer({first, PointerButton::Left, PointerAction::Up});
    REQUIRE(window.focused() == tiles);
    seams(window).handle_key({.key = Key::ArrowDown, .action = KeyAction::Down});
    CHECK(tiles->selected_index() == 3);
    seams(window).handle_key({.key = Key::ArrowUp, .action = KeyAction::Down});
    CHECK(tiles->selected_index() == 0);

    // Dragging the first cell past the second swaps them in the collection.
    const Point start = item_rect(*tiles, 0).center();
    const Rect  onto  = item_rect(*tiles, 1);
    seams(window).handle_pointer({start, PointerButton::Left, PointerAction::Down});
    seams(window).handle_pointer({{start.x + 10.0f, start.y}, PointerButton::None, PointerAction::Move});
    REQUIRE(DragDrop::active());
    seams(window).handle_pointer({{onto.center().x + 5.0f, start.y}, PointerButton::None, PointerAction::Move});
    seams(window).handle_pointer({{onto.center().x + 5.0f, start.y}, PointerButton::Left, PointerAction::Up});
    REQUIRE_FALSE(DragDrop::active());
    CHECK(std::vector<int>(widths.begin(), widths.end()) == std::vector<int>{120, 60, 60, 120, 60, 120});
    ui.layout({400.0f, 400.0f});
    CHECK(item_rect(*tiles, 0).width > item_rect(*tiles, 1).width);
}

TEST_CASE("a view's click and invoke handlers switch their flags on unless the flag is bound", "[spec][views]")
{
    std::vector<std::string>           clicked;
    ui::Control<ui::ListView<Contact>> list;
    auto                               tree = ui::build(ui::ListView<Contact>{
        .items         = contacts(3),
        .on_item_click = [&](const Contact& contact) { clicked.push_back(contact.name); },
        .control       = list,
        .item          = [](const Contact& contact) -> ui::Spec { return ui::Text{.text = contact.name}; },
    });
    CHECK(list->is_item_click_enabled());
    CHECK_FALSE(list->is_item_invoked_enabled());
    lay_out(*tree);
    click(*list, 1);
    CHECK(clicked == std::vector<std::string>{"Contact 1"});

    Property<bool>                     allowed{false};
    ui::Control<ui::TileView<Contact>> tiles;
    auto                               other = ui::build(ui::TileView<Contact>{
        .items                = contacts(3),
        .item_invoked_enabled = ui::bind(allowed),
        .on_item_invoked      = [](const Contact&) {},
        .control              = tiles,
        .item                 = [](const Contact& contact) -> ui::Spec { return ui::Text{.text = contact.name}; },
    });
    CHECK_FALSE(tiles->is_item_invoked_enabled()); // the bound flag decides
    allowed = true;
    CHECK(tiles->is_item_invoked_enabled());
}

TEST_CASE("a view's selected_indices binds the whole selection both ways", "[spec][views][binding]")
{
    Property<std::vector<std::size_t>> chosen{std::vector<std::size_t>{2, 0}};
    ui::Control<ui::ListView<Contact>> list;
    auto                               tree = ui::build(ui::ListView<Contact>{
        .items            = contacts(4),
        .selected_indices = ui::bind(chosen),
        .selection_mode   = ui::SelectionMode::Multiple,
        .control          = list,
        .item             = [](const Contact& contact) -> ui::Spec { return ui::Text{.text = contact.name}; },
    });
    CHECK(list->selected_items() == std::vector<std::size_t>{0, 2});

    list->select(3); // the user's change is written back
    CHECK(chosen.get() == std::vector<std::size_t>{0, 2, 3});

    chosen = std::vector<std::size_t>{1, 9}; // out of range is dropped
    CHECK(list->selected_items() == std::vector<std::size_t>{1});

    Property<std::vector<std::size_t>> tiles_chosen;
    ui::Control<ui::TileView<Contact>> tiles;
    auto                               other = ui::build(ui::TileView<Contact>{
        .items            = contacts(3),
        .selected_indices = ui::bind(tiles_chosen),
        .selection_mode   = ui::SelectionMode::Extended,
        .control          = tiles,
        .item             = [](const Contact& contact) -> ui::Spec { return ui::Text{.text = contact.name}; },
    });
    tiles->select_all();
    CHECK(tiles_chosen.get() == std::vector<std::size_t>{0, 1, 2});
}

TEST_CASE("A typed view's selection handler gets a copy of the item, safe to keep", "[views][list-view]")
{
    ui::Property<std::vector<std::string>>  fruits{std::vector<std::string>{"Apple", "Pear"}};
    std::vector<std::optional<std::string>> kept;
    ui::Control<ui::ListView<std::string>>  list;
    std::unique_ptr<Element>                tree = ui::build(ui::ListView<std::string>{
        .items                = fruits,
        .selection_mode       = SelectionMode::Single,
        .on_selection_changed = [&](std::optional<std::string> fruit) { kept.push_back(std::move(fruit)); },
        .control              = list,
    });
    list->set_selected_index(1);
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0] == "Pear");
    fruits = std::vector<std::string>{"Plum"}; // the items it came from are gone; the copy is not
    CHECK(kept[0] == "Pear");

    STATIC_REQUIRE(std::is_same_v<ui::SelectedItem<std::string>, std::optional<std::string>>);
    STATIC_REQUIRE(std::is_same_v<ui::SelectedItem<std::unique_ptr<int>>, std::optional<std::size_t>>);
}
