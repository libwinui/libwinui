# Write Tests

Catch2, tags, the headless host, goldens.

Every control in libwinui is testable without an `HWND`, a GPU or a Windows session. Controls draw through the `Renderer` interface, and `TestRenderer` records those calls as plain data. Layout and input run against the same logical tree on every backend. Most tests therefore build a small tree, lay it out with a deterministic text measurer, poke it, and look at the recorded commands.

## Catch2 Layout

All tests go into one binary, `winui_tests`, built from `tests/test_<thing>.cpp`. A new file has to be added to the `add_executable(winui_tests ...)` list in `tests/CMakeLists.txt`; it is not globbed. The binary links `winui::testing`, the testing tier's own library (`include/winui/testing/`, `src/testing/`), which brings `winui::winui` along; the library itself never compiles a testing source.

A typical file starts like this:

```cpp
// SPDX-License-Identifier: MIT
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/status/InfoBadge.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;
```

The smallest useful test measures, arranges and renders one control, then checks the command stream (from `tests/test_info_badge.cpp`):

```cpp
TEST_CASE("Value mode renders a filled pill plus the value's own text", "[info-badge][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBadgeControl badge{42};
    const Size       desired = badge.measure({100.0f, Unbounded}, m, theme);
    badge.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    badge.render(r, theme);
    REQUIRE(r.count() == 2);
    REQUIRE(r.count_of<FillRoundedRect>() == 1);
    REQUIRE(r.count_of<DrawTextRun>() == 1);
    const auto& text = std::get<DrawTextRun>(r.commands[1]);
    REQUIRE(text.text == "42");
}
```

`TestRenderer::commands` is a `std::vector<RenderCommand>`, a `std::variant` of one struct per draw call. `count()`, `count_of<T>()`, `std::get<T>` and `std::holds_alternative<T>` cover most assertions. Each command struct has a defaulted `operator==`, so a whole command can be compared at once.

## Tags

Every test case carries tags. Use the control's name in kebab case plus the area it tests:

| Tag | Covers |
| --- | --- |
| `[button]`, `[info-badge]`, ... | One control. |
| `[layout]`, `[render]`, `[input]`, `[focus]`, `[keyboard]` | What part of the control's contract the case checks. |
| `[spec]`, `[binding]` | The declarative surface and the reactive layer. |
| `[theme]`, `[palette]`, `[retro]`, `[luna]`, `[aero]`, `[metro]`, `[mdl2]`, ... | Theme resolution and the era packs. |
| `[golden]` | Every golden test. With `[semantic]` for the text dumps, `[pixel]` for the Direct2D captures, `[scene]` for multi-control scenes. |

Run a subset by passing tags to the binary. Tags in one bracket group must all match:

```sh
./build/bin/Debug/winui_tests.exe "[info-badge]"
./build/bin/Debug/winui_tests.exe "[golden][semantic]"
./build/bin/Debug/winui_tests.exe "[theme]~[retro]"     # theme tests, minus the era packs
```

Tests that need Windows (pixel goldens, Win32 implementations of the seams) sit inside `#ifdef _WIN32`. Everything else runs on any platform.

## The Headless Host

`HeadlessUI` (`include/winui/testing/HeadlessUI.hpp`) is a complete window host without an `HWND`: one `Window`, a `HeadlessWindowHost` for window state and chrome requests, and a `HeadlessTextMeasurer` for layout. It is the tool for layout, focus, input and window-state tests.

Declare the tree with specs, and take the observers you need through `.control`:

```cpp
TEST_CASE("Clicking Save counts", "[button][headless]")
{
    HeadlessUI     host = create_headless();
    Property<int>  clicks{0};
    ui::Control<ui::Button> save;

    host.window().set_content(ui::VerticalStack{.content = {
        ui::Text{.text = ui::bind(clicks, [](int n) { return std::to_string(n); })},
        ui::Button{.text = "Save", .on_click = [&] { ++clicks; }, .control = save},
    }});
    host.layout({400, 300});

    const Point center = save->bounds().center();
    ui::testing::seams(host.window()).handle_pointer({center, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(host.window()).handle_pointer({center, PointerButton::Left, PointerAction::Up});
    REQUIRE(clicks.get() == 1);
}
```

What makes it deterministic:

- **Text.** `HeadlessTextMeasurer` gives every code point the same advance (0.55 em, scaled by weight) and every line 1.35 em. Layout numbers do not depend on installed fonts, DPI or the OS version.
- **Time.** `host.tick(16ms)` advances the window's clock by exactly that much. Hover delays, animations and caret blinking are driven through it, never by a real clock.
- **The OS theme.** A test resolves its theme from a fixed `OsThemeSnapshot`, never from the machine it runs on.
- **OS services.** The clipboard, spell checker, context menus, launcher and native host all have test doubles (`TestClipboard`, `TestSpellChecker`, ...). The clipboard and the launcher belong to the process and are installed with `ui::testing::set_clipboard()` and `set_launcher()`; `HeadlessUI` installs its own unless the test did. The others are installed on the `Window` through `seams()`. Declare the double before the `Window`, so it outlives it:

```cpp
struct LinkWindow
{
    HeadlessTextMeasurer measurer;
    TestLauncher         launcher; // destroyed after the window
    Window               window;

    LinkWindow()
    {
        ui::testing::set_launcher(&launcher);
    }
};
```

Declare the test's tree with specs, since that is the path real callers take.

### Traps in Control Tests

- **Composite controls live on the heap.** `ComboBox`, `AutoSuggestBox` and `NumberBox` own an inner `TextBox` and call `set_parent_of(*text_box_, this)` in their constructor. A by-value return that is not elided moves the control and leaves the inner field's parent pointer at the old address. Build them with `std::make_unique`, usually in a `make_<control>()` helper that also measures and arranges them.
- **Popups need a real `Window` and a second layout.** `Window::sync_popups()` discovers and positions popups inside `layout()`, not inside `handle_pointer()` or a setter. After the input that opens a popup, call `layout()` again before looking at it. A real host does the same, since it lays out again on the paint that follows every input event. A click on popup content (a drop-down row) only hits it through `Window::handle_pointer()`.
- **Animations settle without a clock.** Headless layout tests never call `tick()`, so an animated control must show its end state when no frame has ticked yet. A test that wants to watch an animation run calls `tick()` once first (`e.tick(16ms)`), which is what a host with a frame clock does.
- **Timing tests do not gate.** A test that measures a speed-up is tagged `[.][benchmark]`. The `[.]` hides it from a plain run, because the result depends on the machine (a single-core CI runner can legitimately show no speed-up). Run it explicitly with `winui_tests.exe "[benchmark]"`.
- **Never write to the user's dictionary.** The `Win32SpellChecker` tests never call `add_to_dictionary()` or `ISpellChecker::AutoCorrect()`, because both change the user's own Windows dictionary. `ignore_for_session()` and `add_autocorrection()` are session-only and in-process, so they are safe. Set `LIBWINUI_SPELL_PNG` to a file path to have the Direct2D squiggle test also write its frame as a PNG for eyeballing.

## Testing the Declarative Surface

A spec builds the same controls a hand-written tree does. So the spec layer's main test is an equivalence: a declared tree and the hand-built tree it replaces must emit the identical render-command stream (`tests/test_spec.cpp`):

```cpp
REQUIRE(describe_tree(*declared) == describe_tree(*manual));
```

That makes every existing golden the regression net for the spec layer, without a golden per spec. What a spec test does check, per field, is that the value reaches the control, a `bind(...)` keeps it in sync, and neither seeding a value nor a later bound value fires a change handler:

```cpp
TEST_CASE("ui::InfoBadge binds severity, icon and background", "[spec][binding]")
{
    Property<InfoBadgeSeverity> severity{InfoBadgeSeverity::Success};
    ui::Control<ui::InfoBadge> badge;
    std::unique_ptr<Element>    tree  = ui::build(ui::InfoBadge{.severity = bind(severity), .control = badge});

    CHECK(badge->severity() == InfoBadgeSeverity::Success);
    severity = InfoBadgeSeverity::Critical;
    CHECK(badge->severity() == InfoBadgeSeverity::Critical);
}
```

The reactive layer has its own file, `tests/test_property.cpp`. It covers every lifetime case of a subscription (subscriber dies first, property dies first, the equality gate, a handler unsubscribing itself during dispatch, subscriptions moved when a vector reallocates). Change its invariants only with a very good reason.

## Goldens

Goldens are checked-in expected outputs in `tests/golden/`, driven by `tests/test_golden.cpp`. There are two kinds, and the semantic one comes first.

**Semantic goldens** (`.txt`) are the render-command stream printed by `describe()` (`include/winui/render/Describe.hpp`; `ui::testing::describe(window)` renders a whole window first), one line per command, for a tree laid out with `HeadlessTextMeasurer`:

```text
SetRasterization geometry=antialiased text=grayscale snap=false
FillRoundedRect rect=(94.00,30.00,52.80,29.90) radius=4.00 color=#FFFFFFB3
StrokeRoundedRectGradient rect=(94.50,30.50,51.80,28.90) radius=3.50 extent=-3.00 width=1.00 stops=[0.33:#00000029 1.00:#0000000F]
DrawTextRun rect=(94.00,30.00,52.80,29.90) color=#000000E4 size=14.00 weight=regular halign=center valign=center text="Save"
```

That is `tests/golden/button-normal.txt`, a Fluent 2 button at rest.

They do not depend on fonts, resolution or the OS, run on every platform, and a diff reads as "this rectangle moved" or "this color changed". Colors and metrics are just values in the stream, so a semantic golden covers a theme as well as a layout.

**Pixel goldens** (`.png`) are real Direct2D and DirectWrite output. The scene is laid out with the DirectWrite measurer and drawn by the same `D2DRenderer` the on-screen window uses, captured by `ui::testing::OffscreenCapture` into a software WIC target with grayscale text anti-aliasing, so the result is the same from run to run. They are Windows-only and trade portability for fidelity to what actually ships. The comparison allows a channel difference of 1 and no mismatched pixels beyond that. Capture over the theme's real window background (`theme.colors().background`), since Fluent's control fills are translucent and look wrong over plain white.

Use a pixel golden only when the raster itself is the question: a new primitive in the Direct2D backend, anti-aliasing, text rendering. For a primitive, a targeted check often beats a checked-in PNG. The `Icon` bitmap test in `tests/test_golden.cpp` captures a 40x40 scene and samples two pixels to prove that a bitmap was drawn at all. A wrong bitmap format can leave the recorded command correct while Direct2D draws nothing, and only a real capture shows that. The same file has spot checks of this kind for `draw_image_rect()` and `draw_svg()`, `draw_geometry()`, radial gradients and in-app acrylic (a hard red/white edge under it must come out as a ramp).

`OffscreenCapture` initializes COM only if the thread has not already, and releases every WIC and Direct2D interface before its own `CoUninitialize`, since releasing one afterwards crashes. Keep that order if you touch it.

A theme gets a pixel golden for one representative scene, not one per control. The control-suite scene has a light Fluent 2 golden plus one each for Windows 9x, Luna and dark Metro. Everything else about an era is pinned by `tests/test_theme.cpp` and the semantic goldens. The two non-obvious choices:

- **Metro is captured in dark mode on purpose.** Metro's `CheckBox` and `RadioButton` border thickness is 0 in dark and 2 in light (the reverse of its `Button`), so dark is where the era's toggle polarity shows. Light mode and the Metro button asymmetry are pinned by the `[theme][metro]` cases in `tests/test_theme.cpp`, since the scene holds no button.
- **Luna's scene holds every Luna toggle correction** (the checked box's near-white well with its `#21A121` tick inside a `#1C5180` border, the different green of the radio dot). For Luna the semantic golden carries the weight: a pixel golden can be made self-consistent, but it cannot be validated without a real Windows XP to compare against.

### Updating Goldens

A missing golden fails the test with a hint. To write or rewrite them, set `LIBWINUI_UPDATE_GOLDEN`:

```sh
LIBWINUI_UPDATE_GOLDEN=1 ./build/bin/Debug/winui_tests.exe "[golden]"
```

Do this on purpose, after a visual or layout change you intended, and read the diff before you commit it. A semantic diff should only touch the lines you expected to move. A pixel diff needs a look at the images.

The pixel goldens are made on a development machine, and another Windows build draws text with other font and DirectWrite versions. CI sets `LIBWINUI_SKIP_PIXEL_GOLDENS=1`, which skips every comparison against a `.png` (ctest reports those cases as skipped). The semantic goldens and the pixel tests that compare two renders with each other still run there.

Semantic goldens are compared byte for byte and `describe()` writes LF line endings. `.gitattributes` checks every text file out with LF, so a checkout with `core.autocrlf` does not turn every line into a mismatch. If every semantic golden fails with output that looks identical, check the line endings first.

### Adding a Golden

1. Write a function that builds the scene into a `Window` and lays it out with a given `TextMeasurer`, as `build_controls_scene()` does.
2. Add a semantic `TEST_CASE` tagged `[golden][semantic]` that compares `describe(*window)` against `tests/golden/<name>.txt`.
3. If the raster matters, add a pixel `TEST_CASE` tagged `[golden][pixel]` inside `#ifdef _WIN32` that captures with `OffscreenCapture` and calls `check_pixel_golden()`.
4. Run with `LIBWINUI_UPDATE_GOLDEN=1`, check the new files, and commit them with the test.

## What Not to Test

- **Pixels in unit tests.** A unit test asserts on the command stream and on layout bounds, never on a rendered bitmap. The raster belongs to the pixel goldens.
- **Windows in the core.** A test of layout, input, focus or rendering must run on any platform. Only a test of the Windows backend itself sits inside `#ifdef _WIN32`.
- **The real OS.** No test reads the user's theme, the real clipboard or the wall clock. Use the snapshot, the doubles and `tick()`.
- **Each spec's rendering.** The equivalence test already ties the spec layer to the control's own tests and goldens. A spec test checks that fields arrive, not what the control draws.
- **Controls by string lookup.** There is no `find<ButtonControl>("save")`. Hold the control through `.control`, which fails at compile time instead of at runtime.
