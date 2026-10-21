// SPDX-License-Identifier: MIT
// Only built, never run: it proves an installed libwinui links into a WIN32 app.
#include <winui/winui.hpp>

#include <format>

int main()
{
    ui::Property<int> clicks{0};

    ui::Window window{{.title = "Package check", .size = {480, 320}}};
    window.set_content(ui::VerticalStack{
        .spacing = 12,
        .content =
            {
                ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })},
                ui::Button{.text = "Click me", .on_click = [&] { ++clicks; }},
            },
    });
    return ui::run(window);
}
