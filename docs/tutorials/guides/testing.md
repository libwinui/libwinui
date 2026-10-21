# Testing Your UI

Run your UI in a test without a window on screen: lay it out, click and type into it, and check what it draws.

## Link the Testing Library

The testing tools are in a separate library, `winui::testing`, which brings `winui::winui` along. Link it into your test executable together with the test framework you use. The examples here use Catch2.

```cmake
add_executable(my_app_tests tests.cpp)
target_compile_features(my_app_tests PRIVATE cxx_std_23)
target_link_libraries(my_app_tests PRIVATE winui::testing Catch2::Catch2WithMain)
```

Include `<winui/testing.hpp>` next to `<winui/winui.hpp>`. Everything in it is in namespace `ui::testing`.

## A Headless Window

`ui::testing::create_headless()` returns a `ui::testing::HeadlessUI`, a `ui::Window` that never opens an operating system window. Layout, focus and input run exactly as on screen. Text is measured with fixed per-character widths, so sizes and positions are the same on every machine.

```cpp
ui::testing::HeadlessUI ui = ui::testing::create_headless();

ui::Property<int>       clicks{0};
ui::Control<ui::Button> save;

ui.window().set_content(ui::VerticalStack{
    .content = {
        ui::Text{.text = ui::bind(clicks, [](int n) { return std::to_string(n); })},
        ui::Button{.text = "Save", .on_click = [&] { ++clicks; }, .control = save},
    },
});
ui.layout({400, 300});
```

`ui.layout()` lays the window out at the given client size in DIPs. Call it again after a change that affects layout.

Fill a `.control` handle for every control the test needs to reach. The handle points at the built control, so `save->bounds()` is the button's rectangle in the window.

## Simulate Input

`ui::testing::seams(window)` gives a test the input entry points the operating system would call. Input goes through the window, so hit testing, focus and capture behave as they do for a user.

```cpp
const ui::Point center = save->bounds().center();
ui::testing::seams(ui.window()).handle_pointer({center, ui::PointerButton::Left, ui::PointerAction::Down});
ui::testing::seams(ui.window()).handle_pointer({center, ui::PointerButton::Left, ui::PointerAction::Up});
REQUIRE(clicks.get() == 1);
```

Keys and typed text work the same way:

```cpp
ui::testing::seams(ui.window()).handle_key({.key = ui::Key::Tab});            // move focus
ui::testing::seams(ui.window()).handle_key({.key = ui::Key::S, .modifiers = {.ctrl = true}}); // a shortcut
ui::testing::seams(ui.window()).handle_text("Ada");                            // typed into the focused control
```

Check the results through your properties or the control handles, for example `name->text()` on a `ui::Control<ui::TextBox>`. `ui.window().focused()` returns the element that has keyboard focus.

Animations and timers advance only when the test says so. `ui.tick(16ms)` moves time forward by 16 ms.

## Replace System Services

The same `seams()` installs test doubles in place of the system's spell checker, context menus and other services of a window. The clipboard and the launcher belong to the whole process, so they have setters of their own. `HeadlessUI` installs a `TestClipboard` and a `TestLauncher` already, unless the test installed its own first:

```cpp
ui::testing::TestClipboard clipboard;
ui::testing::set_clipboard(&clipboard);
clipboard.set_text("Ada");
```

The doubles live in `include/winui/testing/`, one header each.

## Check What Is Drawn

A control draws by sending commands to a `ui::Renderer`. `ui::testing::TestRenderer` records them instead of drawing, so a test can check what a control would draw without a GPU.

```cpp
ui::ButtonControl         button{"Hello"};
ui::testing::TestRenderer renderer;
button.render(renderer);

REQUIRE(renderer.count_of<ui::DrawTextRun>() == 1);
REQUIRE(std::get<ui::DrawTextRun>(renderer.commands[2]).text == "Hello");
```

`ui::testing::describe(window)` renders a whole laid-out window and returns the commands as text, which you can compare against a file saved from an earlier run.

## Reference

- [`HeadlessUI`](../../reference/testing/headless-ui.md)
- [`WindowSeams`](../../reference/testing/window-seams.md)
- [`TestRenderer`](../../reference/testing/test-renderer.md)
