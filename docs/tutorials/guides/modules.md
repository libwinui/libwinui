# C++ Modules

How to use libwinui through `import winui;` instead of its headers.

## Build the Modules

The modules are opt-in. Configure libwinui with `LIBWINUI_BUILD_MODULES=ON`, with the Ninja or Visual Studio generator. It adds two targets:

- `winui::module` for `import winui;`, the whole public API of `<winui/winui.hpp>`
- `winui::testing_module` for `import winui.testing;`, the testing tier, with `winui` re-exported

Link the module target next to `winui::main`:

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_app LANGUAGES CXX)

set(LIBWINUI_BUILD_MODULES ON CACHE BOOL "" FORCE)
add_subdirectory(libwinui)

add_executable(my_app WIN32 main.cpp)
target_link_libraries(my_app PRIVATE winui::main winui::module)
```

A `cmake_minimum_required` of 3.28 or newer turns on the module scanning a file with an `import` needs. With an older minimum, set the target's `CXX_SCAN_FOR_MODULES` property to `ON`.

An installed libwinui built with the option has the same targets: `find_package(winui CONFIG REQUIRED)` gives `winui::module`, and `COMPONENTS testing` adds `winui::testing_module`. The package ships the module sources, not compiled modules, since a compiled module only works with the compiler and options it was made with. Your build compiles them once per configuration.

## Import It

The first tutorial's window, with the import:

```cpp
#include <format>

import winui;

int main()
{
    ui::Property<int> clicks{0};
    ui::Window        window{{.title = "My first window", .size = {480, 320}}};

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
```

The rules for a file that imports the module:

- Put the `#include` lines of the standard library before the `import`.
- Include the standard headers you use yourself. The module exports the names in `ui` and nothing else, so `std::optional`, `std::move` and `std::vector` need `<optional>`, `<utility>` and `<vector>` even where a libwinui type uses them. A coroutine that awaits a `ui::Task` needs `<coroutine>`.
- Don't include libwinui headers in the same file, also not through a header of your own. Keep a file on either the headers or the module.
- The literals work as before, with `using namespace ui::literals;` or `using ui::operator""_fr;`.

Files that include the headers and files that import the module can be mixed in one program. The module is made from the headers, so both see the same types and link the same library.

## Macros

A module exports no macros. What the headers define for the preprocessor has a replacement or takes effect when the module is compiled:

- The version is also `ui::VersionMajor`, `ui::VersionMinor` and `ui::VersionPatch`, which work in `if constexpr` and `static_assert`. A file that needs `LIBWINUI_VERSION_MAJOR` in an `#if` includes `<winui/core/Version.hpp>`.
- `LIBWINUI_NO_DEPRECATION_WARNINGS` silences the deprecation warnings only when the module is compiled with it. Add it as a compile definition for the whole build, not with a `#define` in a source file.
- `LIBWINUI_THREAD_CHECKS` comes from the CMake option of the same name, so the module and your files agree on it.

## Compilers

CI builds the modules with MSVC and clang-cl, imports them in every tutorial but the one that hosts native controls, and imports the installed package once more from a project outside the tree. GCC is not tested. The modules sit on the static library; with `LIBWINUI_BUILD_SHARED` the shared library stays header-only.
