# libwinui

A native-like Windows UI library built with C++23 that you link statically and ship as one small
`.exe`. No XAML, no .NET, no Windows App SDK and no installer.

The controls are drawn with Direct2D and DirectWrite into a single `HWND` per window, and an app
only imports DLLs that ship with Windows.

Documentation, tutorials and the API reference: <https://libwinui.com>

## What it looks like

A UI is described with plain aggregates and designated initializers, and state is written down
once, with every display of it derived:

```cpp
ui::Property<int> clicks{0};

window.set_content(ui::VerticalStack{
    .spacing = 12.0f,
    .content =
        {
            ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })},
            ui::Button{.text = "Click me", .on_click = [&] { ++clicks; }},
            ui::Button{.text = "Reset", .on_click = [&] { clicks = 0; }},
        },
});
```

`ui::Button` is a description you fill in and hand over once, `ui::ButtonControl` is the control it
builds, which keeps its own setters. Every node has exactly one owner, there is no garbage
collector and no reference counting in the UI tree, and you can step into any control's source
with your usual debugger.

## What you get

- One small file to ship. Built with MSVC for size (`MinSizeRel`) and the static runtime
  (`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`), the counter app
  above is about 1.2 MB, and the gallery example with every control is about 7.3 MB.
- Controls follow Windows 11's Fluent design, with its parts, states, metrics and keyboard rules,
  plus Mica, light and dark mode and the system accent color.
  Theme packs for older Windows eras are included as well.
- Invalidation is tracked. A setter marks its control dirty, layout only runs when a
  size or position changed, and an idle window doesn't wake up. A control that waits for a known
  time, such as the caret's next blink, wakes it once at that time.
- Lists virtualize, so a list realizes a screenful of rows, whether the model holds a hundred items
  or a hundred thousand.
- UI Automation, high contrast and keyboard navigation are part of every control.
- Native Win32 controls, e.g. a rich edit box, can be hosted inside the tree.
- Controls emit a render command stream, so layout, input and drawing are testable without an
  `HWND` or a GPU.

## Requirements

- A C++23 compiler: MSVC 19.3x, Clang 16+ or GCC 14+
- CMake 3.28 or newer
- A Windows SDK for the Windows backend
- Windows 10, version 1703 or newer, to run the result

The first configure fetches `stb_image` and, on Windows, the C++/WinRT tool, plus Catch2 when the
tests are built. Direct2D, DirectWrite, WIC and UI Automation are part of Windows.

## Building

```sh
cmake -S . -B build
cmake --build build --config Release
```

Run the tests and the gallery:

```sh
ctest --test-dir build -C Release --output-on-failure
./build/bin/Release/gallery.exe
```

| Option                 | Default | Effect                                                   |
| ---------------------- | ------- | -------------------------------------------------------- |
| `LIBWINUI_BUILD_TESTS`    | `ON`    | Builds `winui_tests` and registers it with CTest, when libwinui is the top-level project |
| `LIBWINUI_BUILD_EXAMPLES` | `ON`    | Builds the example apps and one project per tutorial, when libwinui is the top-level project |
| `LIBWINUI_BUILD_SHARED`   | `OFF`   | Also builds a shared library next to the static one, with no ABI promise: it only works with the headers, compiler and options it was built with |

## Using it in your project

Add the repository with `add_subdirectory()` (or `FetchContent`) and link `winui::main`.
`winui::main` lets a `WIN32` executable use a plain `int main()`:

```cmake
add_subdirectory(libwinui)

add_executable(my_app WIN32 main.cpp)
target_link_libraries(my_app PRIVATE winui::main)
```

Tests of your UI link `winui::testing` instead, the opt-in testing tier (`<winui/testing.hpp>`:
the headless host, `TestRenderer` and the test doubles):

```cmake
add_executable(my_app_tests tests.cpp)
target_link_libraries(my_app_tests PRIVATE winui::testing Catch2::Catch2WithMain)
```

Catch2 comes from your own build here: libwinui fetches it only for its own tests.

Note that every public string is UTF-8. With MSVC the library adds `/utf-8` as a public compile
option, so your sources are read as UTF-8 too.

The [first tutorial](https://libwinui.com/tutorials/basics/01-first-window/) takes it from
`main()` to a running window.

## Status

libwinui is at version 0.1.0 and runs on Windows only. The platform-independent core (layout,
input, rendering to a command stream, the headless host) builds and passes its tests on other
systems, but there is no Linux or macOS backend yet.

The public API isn't frozen, so a 0.x release can still rename or reshape things. Not there yet:

- there is no theme drawn through Windows' own visual styles (uxtheme)
- some older era packs still borrow Fluent shapes for a few controls
- there are no developer tools, such as a UI inspector

## LLM usage

libwinui was built with the help of large language models (LLMs), for the code, the tests and
the documentation.

## License

libwinui is licensed under the MIT license, see [LICENSE](LICENSE). The third-party material it contains or
was derived from, and what an app that links it has to carry along, is listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

libwinui is an independent project and isn't affiliated with or endorsed by Microsoft. Windows
and WinUI are names of Microsoft products, used here to describe what the library targets and
which design it follows.
