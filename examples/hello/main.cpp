// SPDX-License-Identifier: MIT
#include <winui/winui.hpp>

#include <format>
#include <string>

int main()
{
    // The one piece of state; everything that shows it is derived from it.
    ui::Property<int> clicks{0};

    ui::Window window{{.title = "My Application", .size = {800, 600}}};

    window.set_content(ui::VerticalStack{
        .spacing              = 12.0f,
        .horizontal_alignment = ui::Alignment::Center,
        .vertical_alignment   = ui::Alignment::Center,
        .content =
            {
                ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })},
                ui::Button{.text = "Click me", .on_click = [&] { ++clicks; }},
                ui::Button{.text = "Reset", .on_click = [&] { clicks = 0; }},
            },
    });

    // `clicks` outlives the message loop, so [&] is fine here. A spec returned from a function
    // must capture by value.
    return ui::run(window);
}
