// SPDX-License-Identifier: MIT
// libwinui tutorial: hosting native controls.
// --8<-- [start:includes]
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "edit_box.hpp"
#include "rich_edit_box.hpp"

#include <winui/winui.hpp>

#include <format>
#include <memory>
#include <sstream>
#include <string>
#include <windows.h>

#include <cstddef>
// --8<-- [end:includes]

// --8<-- [start:manifest]
// EDIT's placeholder text and dark scroll bars need comctl32 version 6.
#ifdef _MSC_VER
#pragma comment(linker,                                                                                          \
                "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' " \
                "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
// --8<-- [end:manifest]

namespace
{
std::size_t count_words(const std::string& text)
{
    std::istringstream in{text};
    std::size_t        words = 0;
    for (std::string word; in >> word;)
    {
        ++words;
    }
    return words;
}

} // namespace

int main()
{
    // --8<-- [start:state]
    ui::Property<std::string> greeting{"Type your name."};
    ui::Property<std::string> word_count{"0 words"};
    // --8<-- [end:state]

    // --8<-- [start:edit_box]
    native_controls::EditBoxOptions one_line;
    one_line.accepts_return = false;

    auto name = std::make_unique<native_controls::EditBox>(ui::Size{300.0f, 0.0f}, "", one_line);
    name->set_header("Your name");
    name->set_placeholder_text("Ada Lovelace");
    native_controls::EditBox* name_box = name.get();
    name->on_text_changed([&greeting, name_box] { greeting = "Hello, " + name_box->text(); });
    // --8<-- [end:edit_box]

    // --8<-- [start:rich_edit]
    auto notes = std::make_unique<native_controls::RichEditBox>(ui::Size{460.0f, 180.0f},
                                                                "Select a word and press Bold.");
    notes->set_accessibility({.name = "Notes"});
    native_controls::RichEditBox* editor = notes.get();
    notes->on_text_changed([&word_count, editor] { word_count = std::format("{} words", count_words(editor->text())); });
    // --8<-- [end:rich_edit]

    // --8<-- [start:native]
    ui::Control<ui::Native> bare;

    ui::Spec bare_edit = ui::Native{
        .desc = {.class_name = "EDIT", .initial_text = "A bare EDIT, created by class name", .style = WS_BORDER | ES_AUTOHSCROLL},
        .native_size = {300.0f, 24.0f},
        .control     = bare,
    };

    auto write_through_hwnd = [&bare]
    {
        if (HWND hwnd = static_cast<HWND>(bare->native_handle()))
        {
            SetWindowTextW(hwnd, L"Written with SetWindowTextW");
        }
    };
    // --8<-- [end:native]

    ui::Window window{{.title = "Hosting native controls", .size = {560, 600}}};

    // --8<-- [start:layout]
    window.set_content(ui::VerticalStack{
        .spacing         = 12,
        .child_alignment = ui::Alignment::Leading,
        .margin          = 24,
        .content =
            {
                std::move(name),
                ui::Text{.text = greeting},
                ui::HorizontalStack{
                    .spacing = 8,
                    .content =
                        {
                            ui::Button{.text = "Bold",
                                       .on_click =
                                           [editor]
                                       {
                                           editor->toggle_bold();
                                           editor->focus();
                                       }},
                            ui::Button{.text = "Italic",
                                       .on_click =
                                           [editor]
                                       {
                                           editor->toggle_italic();
                                           editor->focus();
                                       }},
                        },
                },
                std::move(notes),
                ui::Text{.text = word_count, .role = ui::TextRole::Caption},
                std::move(bare_edit),
                ui::Button{.text = "Write through the HWND", .on_click = write_through_hwnd},
            },
    });
    // --8<-- [end:layout]

    return ui::run(window);
}
