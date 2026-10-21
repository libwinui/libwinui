// SPDX-License-Identifier: MIT
// --8<-- [start:includes]
#include <winui/winui.hpp>

#include <format>
// --8<-- [end:includes]

int main()
{
    // --8<-- [start:state]
    ui::Property<int> clicks{0};
    // --8<-- [end:state]

    // --8<-- [start:window]
    ui::Window window{{.title = "My first window", .size = {480, 320}}};
    // --8<-- [end:window]

    // --8<-- [start:tree]
    window.set_content(ui::VerticalStack{
        .spacing              = 12,
        .horizontal_alignment = ui::Alignment::Center,
        .vertical_alignment   = ui::Alignment::Center,
        .content =
            {
                ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })},
                ui::Button{.text = "Click me", .on_click = [&] { ++clicks; }},
                ui::Button{.text = "Reset", .on_click = [&] { clicks = 0; }},
            },
    });
    // --8<-- [end:tree]

    // --8<-- [start:run]
    return ui::run(window);
    // --8<-- [end:run]
}
