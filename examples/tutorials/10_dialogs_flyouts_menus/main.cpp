// SPDX-License-Identifier: MIT
// libwinui tutorial: dialogs, flyouts and menus.
// --8<-- [start:includes]
#include <winui/winui.hpp>

#include <format>
#include <optional>
#include <string>
#include <vector>

#include <cstddef>
// --8<-- [end:includes]

int main()
{
    using ui::operator""_fr;

    ui::Window  window{{.title = "Dialogs, flyouts and menus", .size = {640, 520}}};
    ui::Window* w = &window;

    // --8<-- [start:state]
    ui::Property<std::string>              status{"Try the menu bar, the buttons, and a right-click on the list."};
    std::string                            new_name;
    ui::Control<ui::ListView<std::string>> files;
    ui::Control<ui::Flyout>                new_file;
    // --8<-- [end:state]

    // --8<-- [start:menu]
    ui::Spec menu_bar = ui::MenuBar{
        .items =
            {
                {.text = "File",
                 .items =
                     {
                         {.text = "New", .shortcut = ui::Ctrl + ui::Key::N, .on_click = [&] { status = "File > New"; }},
                         {.text     = "Open...",
                          .shortcut = ui::Ctrl + ui::Key::O,
                          .on_click = [&] { status = "File > Open"; }},
                         ui::MenuItem::separator(),
                         {.text = "Exit", .on_click = [w] { w->close(); }},
                     }},
                {.text = "View",
                 .items =
                     {
                         ui::MenuItem::toggle("Word wrap",
                                              false,
                                              [&](bool on) { status = on ? "Word wrap on" : "Word wrap off"; }),
                         ui::MenuItem::sub_item("Sort by",
                                                {
                                                    ui::MenuItem::radio("Name", true),
                                                    ui::MenuItem::radio("Date"),
                                                }),
                     }},
            },
    };
    // --8<-- [end:menu]

    // --8<-- [start:dialog]
    auto confirm_delete = [&]
    {
        const std::optional<std::size_t> index = files->selected_index();
        if (!index)
        {
            status = "Select a file first.";
            return;
        }
        w->show_modal(ui::Dialog{
            .title          = "Delete " + files->items()[*index] + "?",
            .default_button = ui::DialogDefaultButton::Close,
            .primary        = {.text = "Delete"},
            .close          = {.text = "Cancel"},
            .on_closed =
                [&, index](ui::DialogResult result)
            {
                if (result == ui::DialogResult::Primary)
                {
                    status = "Deleted " + files->items()[*index];
                    files->remove_item(*index);
                }
                else
                {
                    status = "Kept it.";
                }
            },
            .content = ui::Text{.text = "The file is gone for good.", .wrapping = ui::TextWrapping::Wrap},
        });
    };
    // --8<-- [end:dialog]

    // --8<-- [start:dialog_await]
    // A coroutine waits for the answer and goes on below it. `clear_all` lives as long as main(),
    // so capturing by reference is safe.
    auto clear_all = [&]() -> ui::FireAndForget
    {
        const ui::DialogResult answer = co_await ui::show_dialog(*w,
                                                                 ui::Dialog{
                                                                     .title          = "Remove every file?",
                                                                     .default_button = ui::DialogDefaultButton::Close,
                                                                     .primary        = {.text = "Remove all"},
                                                                     .close          = {.text = "Cancel"},
                                                                 });
        if (answer != ui::DialogResult::Primary)
        {
            status = "Kept them.";
            co_return;
        }
        while (!files->items().empty())
        {
            files->remove_item(0);
        }
        status = "Removed every file.";
    };
    ui::Spec clear_button = ui::Button{.text = "Clear...", .on_click = [&] { clear_all(); }};
    // --8<-- [end:dialog_await]

    // --8<-- [start:flyout]
    ui::Spec new_file_button = ui::Flyout{
        .control = new_file,
        .anchor  = ui::Button{.text = "New file..."},
        .content =
            ui::VerticalStack{
                .spacing = 12,
                .content =
                    {
                        ui::TextBox{
                            .header           = "Name",
                            .placeholder_text = "notes.txt",
                            .width            = 240,
                            .on_text_changed  = [&](const std::string& text) { new_name = text; },
                        },
                        ui::Button{
                            .text    = "Create",
                            .variant = ui::ButtonVariant::Accent,
                            .on_click =
                                [&]
                            {
                                if (!new_name.empty())
                                {
                                    files->add_item(new_name);
                                    status = "Created " + new_name;
                                }
                                new_file->hide();
                            },
                        },
                    },
            },
    };
    // --8<-- [end:flyout]

    // --8<-- [start:tooltip]
    ui::Spec delete_button = ui::ToolTip{
        .placement = ui::PopupPlacement::Below,
        .anchor    = ui::Button{.text = "Delete...", .on_click = confirm_delete},
        .content   = ui::Text{.text = "Asks first. Enter picks Cancel."},
    };
    // --8<-- [end:tooltip]

    // --8<-- [start:menu_flyout]
    ui::Spec more_button = ui::MenuFlyout{
        .items =
            {
                {.text = "Select first", .on_click = [&] { files->set_selected_index(0); }},
                {.text = "Select none", .on_click = [&] { files->set_selected_index(std::nullopt); }},
            },
        .placement_alignment = ui::PopupPlacementAlignment::Leading,
        .anchor              = ui::Button{.text = "More", .tool_tip = "Selection shortcuts"},
    };
    // --8<-- [end:menu_flyout]

    // --8<-- [start:context_menu]
    ui::Spec file_list = ui::ListView<std::string>{
        .items             = {"readme.md", "budget.xlsx", "holiday.jpg", "notes.txt"},
        .height            = 240,
        .control           = files,
        .item_context_menu = [&](const std::string&) -> std::vector<ui::MenuItem>
        {
            // The file is selected by the time the menu opens, so both items work on the selection.
            return {
                {.text = "Delete...", .on_click = confirm_delete},
                {.text = "Duplicate",
                 .on_click =
                     [&]
                 {
                     if (const std::optional<std::size_t> index = files->selected_index())
                     {
                         files->add_item(files->items()[*index] + " (copy)");
                     }
                 }},
            };
        },
    };
    // --8<-- [end:context_menu]

    // --8<-- [start:layout]
    window.set_content(ui::
                           Grid{
                               .rows = {ui::AutoSize, 1_fr, ui::AutoSize},
                               .content =
                                   {
                                       ui::Cell{.row = 0, .content = std::move(menu_bar)},
                                       ui::
                                           Cell{
                                               .row = 1,
                                               .content =
                                                   ui::
                                                       VerticalStack{
                                                           .spacing = 12,
                                                           .margin  = 16,
                                                           .content =
                                                               {
                                                                   ui::HorizontalStack{
                                                                       .spacing = 8,
                                                                       .content = {std::move(new_file_button), std::move(delete_button), std::move(clear_button), std::move(more_button)},
                                                                   },
                                                                   std::move(file_list),
                                                               },
                                                       }},
                                       ui::Cell{.row = 2,
                                                .content = ui::Text{.text = status,
                                                                    .margin = ui::Edges{.left = 16, .top = 8, .right = 16, .bottom = 12}}},
                                   },
                           });
    // --8<-- [end:layout]

    return ui::run(window);
}
