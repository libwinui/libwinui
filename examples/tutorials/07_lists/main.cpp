// SPDX-License-Identifier: MIT
// libwinui tutorial: lists.
// --8<-- [start:includes]
#include <winui/winui.hpp>

#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>
// --8<-- [end:includes]

namespace
{
// --8<-- [start:model]
struct Contact
{
    std::string name;
    std::string company;

    // A Property compares before it notifies, so what it holds needs ==.
    friend bool operator==(const Contact&, const Contact&) = default;
};

std::vector<Contact> make_contacts()
{
    const char* first[]     = {"Ada", "Grace", "Alan", "Edsger", "Barbara", "Donald", "Frances", "Niklaus"};
    const char* last[]      = {"Lovelace", "Hopper", "Turing", "Dijkstra", "Liskov", "Knuth", "Allen", "Wirth"};
    const char* companies[] = {"Contoso", "Fabrikam", "Northwind", "Tailspin"};

    std::vector<Contact> contacts;
    for (std::size_t i = 0; i < 8; ++i)
    {
        for (std::size_t j = 0; j < 8; ++j)
        {
            contacts.push_back({std::format("{} {}", first[i], last[j]), companies[(i + j) % 4]});
        }
    }
    return contacts;
}
// --8<-- [end:model]

} // namespace

int main()
{
    using ui::operator""_fr;

    // --8<-- [start:state]
    // Declared before the window, so everything here outlives it and its tree.
    ui::Property<std::vector<Contact>> contacts{make_contacts()};
    std::vector<std::string>           recent;
    ui::Property<std::string>          events{"Click, select, expand: the newest event is on the left."};

    auto note = [&recent, &events](std::string event)
    {
        recent.insert(recent.begin(), std::move(event));
        if (recent.size() > 3)
        {
            recent.pop_back();
        }
        std::string line;
        for (const std::string& e : recent)
        {
            line += line.empty() ? e : "  |  " + e;
        }
        events = line;
    };
    // --8<-- [end:state]

    // --8<-- [start:folders]
    ui::Spec folders = ui::ListView<std::string>{
        .items                = {"Inbox", "Drafts", "Sent", "Archive"},
        .selected_index       = 0,
        .height               = 180,
        .on_selection_changed = [&](std::optional<std::string> folder)
        { note("folder SelectionChanged " + (folder ? *folder : std::string{"(none)"})); },
    };
    // --8<-- [end:folders]

    // --8<-- [start:people]
    ui::Spec people = ui::ListView<Contact>{
        .items                = contacts,
        .key                  = &Contact::name,
        .selection_mode       = ui::SelectionMode::Single,
        .on_selection_changed = [&](std::optional<Contact> contact)
        { note("SelectionChanged " + (contact ? contact->name : std::string{"(none)"})); },
        .on_item_click = [&](const Contact& contact) { note("ItemClick " + contact.name); },
        .item          = [](const Contact& contact) -> ui::Spec
        {
            return ui::VerticalStack{
                .margin = ui::Symmetric{.horizontal = 0, .vertical = 6},
                .content =
                    {
                        ui::Text{.text = contact.name},
                        ui::Text{.text = contact.company, .role = ui::TextRole::Caption},
                    },
            };
        },
    };
    // --8<-- [end:people]

    // --8<-- [start:grid]
    ui::Spec table = ui::TableView<Contact>{
        .items = contacts,
        .columns =
            {
                {.header = "Name", .width = 160, .text = [](const Contact& contact) { return contact.name; }},
                {.header = "Company", .width = 110, .text = [](const Contact& contact) { return contact.company; }},
                {.header = "Letters",
                 .width  = 70,
                 .text   = [](const Contact& contact) { return std::format("{}", contact.name.size()); }},
            },
        .on_selection_changed = [&](std::optional<Contact> contact)
        { note("TableView " + (contact ? contact->name : std::string{"(none)"})); },
    };
    // --8<-- [end:grid]

    // --8<-- [start:tree]
    ui::Property<std::vector<ui::TreeNode>> org{std::vector<ui::TreeNode>{
        {.text = "Contoso", .children = {{.text = "Sales"}, {.text = "Support"}}, .expanded = true},
        {.text = "Fabrikam", .children = {{.text = "Research"}, {.text = "Legal"}}},
        {.text = "Archive (loads on expand)", .has_children = true},
    }};

    ui::Spec tree = ui::TreeView<ui::TreeNode>{
        .items                = org,
        .on_selection_changed = [&](std::optional<ui::TreeNode> node)
        { note("TreeView selected " + (node ? node->text : std::string{"(none)"})); },
        .on_item_invoked = [&](const ui::TreeNode& node) { note("TreeView invoked " + node.text); },
        .on_expanding =
            [&](const ui::TreeNode& node)
        {
            if (!node.has_children || !node.children.empty())
            {
                return; // already loaded
            }
            // The data is the model: load the children into it, and the row opens with them.
            org.update(
                [&](std::vector<ui::TreeNode>& roots)
                {
                    for (ui::TreeNode& root : roots)
                    {
                        if (root.text == node.text)
                        {
                            for (int year = 2021; year <= 2025; ++year)
                            {
                                root.children.push_back({.text = std::format("{}", year)});
                            }
                        }
                    }
                });
            note("TreeView loaded " + node.text);
        },
    };
    // --8<-- [end:tree]

    ui::Window window{{.title = "Lists", .size = {1100, 680}}};

    // --8<-- [start:layout]
    // The first column holds both lists, in a grid of their own.
    ui::Spec lists = ui::Grid{
        .rows        = {ui::AutoSize, 1_fr},
        .row_spacing = 12,
        .content = {ui::Cell{.row = 0, .content = std::move(folders)}, ui::Cell{.row = 1, .content = std::move(people)}},
    };
    auto heading = [](const char* text) { return ui::Text{.text = text, .role = ui::TextRole::Subtitle}; };

    window.set_content(ui::Grid{
        .columns        = {1_fr, 1_fr, 1_fr},
        .rows           = {ui::AutoSize, 1_fr, ui::AutoSize},
        .row_spacing    = 12,
        .column_spacing = 16,
        .margin         = 16,
        .content =
            {
                ui::Cell{.row = 0, .column = 0, .content = heading("ListView")},
                ui::Cell{.row = 0, .column = 1, .content = heading("TableView")},
                ui::Cell{.row = 0, .column = 2, .content = heading("TreeView")},
                ui::Cell{.row = 1, .column = 0, .content = std::move(lists)},
                ui::Cell{.row = 1, .column = 1, .content = std::move(table)},
                ui::Cell{.row = 1, .column = 2, .content = std::move(tree)},
                ui::Cell{.row = 2, .column = 0, .column_span = 3, .content = ui::Text{.text = events}},
            },
    });
    // --8<-- [end:layout]

    return ui::run(window);
}
