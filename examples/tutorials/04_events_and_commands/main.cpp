// SPDX-License-Identifier: MIT
// libwinui tutorial: events and commands.
#include <winui/winui.hpp>

#include <format>
#include <memory>
#include <string>
#include <vector>

int main()
{
    ui::Property<std::string> note{};
    ui::Property<bool>        dirty{false};
    ui::Property<int>         saves{0};
    ui::Property<std::string> status{"Nothing saved yet."};

    // --8<-- [start:command]
    ui::Command save{
        .text        = "Save",
        .shortcut    = ui::Ctrl + ui::Key::S,
        .description = "Save the note (Ctrl+S)",
        .enabled     = dirty,
        .execute =
            [&]
        {
            ++saves;
            dirty  = false;
            status = std::format("Saved {} characters.", note->size());
        },
    };
    // --8<-- [end:command]

    ui::Window window{{.title = "Events and commands", .size = {520, 420}}};

    // --8<-- [start:keyboard]
    ui::Shortcut reset_saves{
        .keys = ui::Ctrl + ui::Key::R,
        .on_pressed =
            [&]
        {
            if (saves.get() == 0)
            {
                return ui::NotHandled; // nothing to reset: Ctrl+R goes on
            }
            saves = 0;
            return ui::Handled;
        },
    };
    // --8<-- [end:keyboard]

    // --8<-- [start:tree]
    ui::Spec page = ui::VerticalStack{
        .spacing = 12,
        .margin  = 24,
        .content =
            {
                ui::TextBox{
                    .header          = "Note",
                    .accepts_return  = true,
                    .height          = 120,
                    .on_text_changed =
                        [&](const std::string& text) {
                            note  = text;
                            dirty = true;
                        },
                },
                ui::HorizontalStack{
                    .spacing = 8,
                    .content =
                        {
                            ui::Button{.text = "Save", .variant = ui::ButtonVariant::Accent, .command = save},
                            ui::Button{
                                .text     = "Reset counter",
                                .enabled  = ui::bind(saves, [](int n) { return n > 0; }),
                                .on_click = [&] { saves = 0; },
                            },
                            // --8<-- [start:move_only]
                            ui::Button{
                                .text     = "Keep a snapshot",
                                .on_click = [&, kept = std::make_unique<std::vector<std::string>>()] {
                                    kept->push_back(note.get());
                                    status = std::format("{} snapshots kept.", kept->size());
                                },
                            },
                            // --8<-- [end:move_only]
                        },
                },
                ui::Text{.text = ui::bind(saves, [](int n) { return std::format("Saves: {}", n); })},
                ui::Text{.text = status, .role = ui::TextRole::Caption},
            },
    };

    // Ctrl+R works while focus is anywhere in the page, which here is the whole window.
    window.set_content(ui::Shortcuts{.items = {std::move(reset_saves)}, .content = std::move(page)});
    // --8<-- [end:tree]

    return ui::run(window);
}
