// SPDX-License-Identifier: MIT
// libwinui tutorial: a todo list, a tree whose structure follows state.
// --8<-- [start:includes]
#include <winui/winui.hpp>

#include <format>
#include <optional>
#include <string>
#include <vector>

#include <cstddef>
// --8<-- [end:includes]

namespace
{
// --8<-- [start:model]
// An item holds Properties, so a row binds to them and an edit never touches the collection.
struct Todo
{
    explicit Todo(std::string text) : title{std::move(text)}
    {
    }

    ui::Property<std::string> title;
    ui::Property<bool>        done{false};
};

enum class Filter
{
    All,
    Active,
    Done,
};

enum class Page
{
    Tasks,
    Notes,
};

struct Model
{
    ui::ObservableVector<Todo> todos;
    ui::Property<Filter>       filter{Filter::All};
    ui::Property<std::string>  draft;

    // Derived from the collection, kept as Properties so that the tree can bind to them.
    ui::Property<std::size_t> total{0};
    ui::Property<std::size_t> remaining{0};

    void recount()
    {
        std::size_t open = 0;
        for (const Todo& todo : todos)
        {
            open += todo.done.get() ? 0 : 1;
        }
        total     = todos.size();
        remaining = open;
    }
};
// --8<-- [end:model]

// --8<-- [start:row]
// One row. Whether it is being edited is the row's own business, so ui::State keeps that flag in
// the row: it is created with the row and destroyed with it, and the handlers below may capture it.
ui::Spec todo_row(Model& model, Todo& todo)
{
    return ui::State<ui::Property<bool>>{
        // A margin rather than spacing in the list: a hidden row then leaves no gap behind.
        .margin = ui::Symmetric{.horizontal = 0, .vertical = 3},
        // The row hides itself when the filter leaves it out. Two sources, one binding.
        .visible = ui::bind([](bool done, Filter filter)
                            { return filter == Filter::All || (filter == Filter::Done) == done; },
                            todo.done,
                            model.filter),
        .content = [&model, &todo](ui::Property<bool>& editing) -> ui::Spec
        {
            return ui::HorizontalStack{
                .spacing         = 8,
                .child_alignment = ui::Alignment::Center,
                .content =
                    {
                        ui::CheckBox{
                            .checked = ui::bind_one_way(todo.done),
                            .on_checked_changed =
                                [&model, &todo](std::optional<bool> checked)
                            {
                                todo.done = checked == true;
                                model.recount();
                            },
                        },
                        // Two controls for the title, one of them visible. Both always exist, so the
                        // text box keeps its caret and selection between edits.
                        ui::Text{
                            .text    = todo.title,
                            .width   = 260,
                            .visible = ui::bind(editing, [](bool on) { return !on; }),
                        },
                        ui::TextBox{
                            .text      = todo.title,
                            .width     = 260,
                            .visible   = editing,
                            .on_submit = [&editing] { editing = false; },
                        },
                        ui::Button{
                            .text     = ui::bind(editing, [](bool on) { return on ? "Done" : "Edit"; }),
                            .on_click = [&editing] { editing.toggle(); },
                        },
                        ui::Button{.text = "Remove", .on_click = [&model, &todo] { model.todos.remove(todo); }},
                    },
            };
        },
    };
}
// --8<-- [end:row]

// --8<-- [start:tasks]
ui::Spec tasks_page(Model& model)
{
    auto add = [&model]
    {
        if (!model.draft.get().empty())
        {
            model.todos.emplace_back(model.draft.get());
            model.draft = "";
        }
    };

    return ui::VerticalStack{
        .spacing = 12,
        .content =
            {
                ui::HorizontalStack{
                    .spacing = 8,
                    .content =
                        {
                            ui::TextBox{
                                .placeholder_text = "What needs doing?",
                                .text             = model.draft,
                                .width            = 320,
                                .on_submit        = add,
                            },
                            ui::Button{.text = "Add", .variant = ui::ButtonVariant::Accent, .on_click = add},
                        },
                },
                ui::SelectorBar{
                    .items          = {"All", "Active", "Done"},
                    .selected_index = ui::bind_choice(model.filter, {Filter::All, Filter::Active, Filter::Done}),
                },
                // Shown instead of the list while the collection is empty.
                ui::Text{
                    .text    = "Nothing to do. Add a task above.",
                    .role    = ui::TextRole::Caption,
                    .visible = ui::bind(model.total, [](std::size_t count) { return count == 0; }),
                },
                ui::Scroll{
                    .height = 260,
                    .content =
                        ui::ForEach<Todo>{
                            .items            = model.todos,
                            .item_transitions = {ui::AddRemoveTransition{}},
                            .item             = [&model](Todo& todo) -> ui::Spec { return todo_row(model, todo); },
                        },
                },
                ui::HorizontalStack{
                    .spacing         = 12,
                    .child_alignment = ui::Alignment::Center,
                    .content =
                        {
                            ui::Text{.text = ui::bind([](std::size_t open, std::size_t all)
                                                      { return std::format("{} of {} left", open, all); },
                                                      model.remaining,
                                                      model.total)},
                            ui::Button{
                                .text     = "Clear completed",
                                .enabled  = ui::bind([](std::size_t open, std::size_t all) { return open < all; },
                                                     model.remaining,
                                                     model.total),
                                .on_click = [&model]
                                { model.todos.erase_if([](const Todo& todo) { return todo.done.get(); }); },
                            },
                        },
                },
            },
    };
}
// --8<-- [end:tasks]

// --8<-- [start:notes]
ui::Spec notes_page(ui::Property<bool>& show_help)
{
    return ui::VerticalStack{
        .spacing = 12,
        .content =
            {
                ui::TextBox{
                    .header         = "Notes",
                    .accepts_return = true,
                    .wrapping       = ui::TextWrapping::Wrap,
                    .height         = 160,
                },
                ui::CheckBox{.text = "Show the keyboard help", .checked = show_help},
                // Built the first time the box is ticked, and kept from then on.
                ui::Lazy{
                    .visible = show_help,
                    .content = []() -> ui::Spec
                    {
                        return ui::VerticalStack{
                            .spacing = 4,
                            .content =
                                {
                                    ui::Text{.text = "Enter adds the task or ends an edit."},
                                    ui::Text{.text = "Tab moves between the rows' controls."},
                                    ui::Text{.text = "Space ticks the focused check box."},
                                },
                        };
                    },
                },
            },
    };
}
// --8<-- [end:notes]

} // namespace

int main()
{
    // --8<-- [start:state]
    // Declared before the window, so everything here outlives it and its tree.
    Model              model;
    ui::Property<Page> page{Page::Tasks};
    ui::Property<bool> show_help{false};

    // Whatever changes the collection, the counts follow.
    ui::Subscription counting = model.todos.observe([&model](const ui::CollectionChange&) { model.recount(); });

    // `tutorial_14_todo_list --sample` starts with three tasks.
    const std::vector<std::string> args   = ui::command_line();
    const bool                     sample = args.size() > 1 && args[1] == "--sample";
    if (sample)
    {
        model.todos.emplace_back("Read the tutorial");
        model.todos.emplace_back("Build the project");
        model.todos.emplace_back("Remove this task");
    }
    // --8<-- [end:state]

    ui::Window window{{.title = "Todo List", .size = {560, 600}}};

    // --8<-- [start:layout]
    window.set_content(ui::VerticalStack{
        .spacing = 16,
        .margin  = 16,
        .content =
            {
                ui::SelectorBar{
                    .items          = {"Tasks", "Notes"},
                    .selected_index = ui::bind_choice(page, {Page::Tasks, Page::Notes}),
                },
                // Decided once, while the tree is built: there, or not a child at all.
                ui::when(sample, ui::Text{.text = "Started with sample tasks.", .role = ui::TextRole::Caption}),
                // Both pages are built once and kept: what was typed on one is still there after a
                // visit to the other.
                ui::Match<Page>{
                    .value   = page,
                    .keep    = true,
                    .content = [&model, &show_help](const Page& current) -> ui::Spec
                    { return current == Page::Tasks ? tasks_page(model) : notes_page(show_help); },
                },
            },
    });
    // --8<-- [end:layout]

    return ui::run(window);
}
