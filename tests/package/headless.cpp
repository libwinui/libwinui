// SPDX-License-Identifier: MIT
// Clicks a button through the testing tier of an installed libwinui and checks the bound text.
#include <winui/testing.hpp>
#include <winui/winui.hpp>

#include <string>

#include <cstdio>

int main()
{
    ui::testing::HeadlessUI ui = ui::testing::create_headless();

    ui::Property<int>       clicks{0};
    ui::Control<ui::Text>   label;
    ui::Control<ui::Button> button;

    ui.window().set_content(ui::VerticalStack{
        .content =
            {
                ui::Text{.text = ui::bind(clicks, [](int n) { return std::to_string(n); }), .control = label},
                ui::Button{.text = "Click me", .on_click = [&] { ++clicks; }, .control = button},
            },
    });
    ui.layout({400, 300});

    const ui::Point center = button->bounds().center();
    ui::testing::seams(ui.window()).handle_pointer({center, ui::PointerButton::Left, ui::PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({center, ui::PointerButton::Left, ui::PointerAction::Up});

    if (clicks.get() != 1 || label->text() != "1")
    {
        std::fprintf(stderr, "package check failed: clicks=%d, text=\"%s\"\n", clicks.get(), label->text().c_str());
        return 1;
    }
    std::puts("package check passed");
    return 0;
}
