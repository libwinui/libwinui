// SPDX-License-Identifier: MIT
// The C++ modules (LIBWINUI_BUILD_MODULES): a headless UI built and driven through
// `import winui.testing;` alone, which re-exports winui. A program of its own: the files of
// winui_tests include the headers, and a file uses either the headers or the module.
#include <string>
#include <vector>

#include <cstdio>

import winui.testing;

using namespace ui::literals;

namespace
{
int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok)
    {
        std::printf("module_import: %s\n", what);
        ++failures;
    }
}
} // namespace

int main()
{
    ui::testing::HeadlessUI ui = ui::testing::create_headless();
    ui::Property<int>       clicks{0};
    ui::Control<ui::Button> save;
    ui.window().set_content(ui::Grid{
        .columns = {1_fr, 120_dip},
        .content =
            {
                ui::Cell{.column  = 0,
                         .content = ui::Text{.text = ui::bind(clicks, [](int n) { return std::to_string(n); })}},
                ui::Cell{.column  = 1,
                         .content = ui::Button{.text                 = "Save",
                                               .horizontal_alignment = ui::Alignment::Stretch,
                                               .accessibility        = {.automation_id = "save"},
                                               .on_click             = [&] { ++clicks; },
                                               .control              = save}},
            },
    });
    ui.layout({400, 300});

    check(save->bounds().width == 120.0f, "the 120_dip column is 120 wide");
    const ui::Point center = save->bounds().center();
    ui::testing::seams(ui.window()).handle_pointer({center, ui::PointerButton::Left, ui::PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({center, ui::PointerButton::Left, ui::PointerAction::Up});
    check(clicks.get() == 1, "a click on Save counts");

    ui.layout({400, 300});
    check(ui::describe_automation(*ui.window().content()) ==
              "Group\n"
              "  Text \"1\"\n"
              "  Button \"Save\" #save [Invoke]\n",
          "the automation tree shows the new count");

    ui::testing::TestRenderer renderer;
    save->render(renderer);
    check(renderer.count() > 0, "the button renders");

    return failures == 0 ? 0 : 1;
}
