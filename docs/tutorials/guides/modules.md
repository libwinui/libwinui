# C++ Modules

How to use libwinui through `import winui;` instead of its headers.

## Link the Module

libwinui builds its modules by default with the Ninja or Visual Studio generator and MSVC 19.36 or clang-cl 17 and newer. `LIBWINUI_BUILD_MODULES=OFF` leaves them out. They are two targets:

- `winui::winui` for `import winui;`, the whole public API of `<winui/winui.hpp>`
- `winui::testing` for `import winui.testing;`, the testing tier, with `winui` re-exported

An app links `winui::main`, which brings `winui::winui` and lets a `WIN32` executable start at a plain `int main()`:

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_app LANGUAGES CXX)

add_subdirectory(libwinui)

add_executable(my_app WIN32 main.cpp)
target_link_libraries(my_app PRIVATE winui::main)
```

A `cmake_minimum_required` of 3.28 or newer turns on the module scanning a file with an `import` needs. With an older minimum, set the target's `CXX_SCAN_FOR_MODULES` property to `ON`.

An installed libwinui built with the modules has the same targets: `find_package(winui CONFIG REQUIRED)` gives `winui::winui`, and `COMPONENTS testing` adds `winui::testing`. The package ships the module interface sources next to the module libraries, since a compiled module interface only works with the compiler and options it was made with. Your build compiles them once per configuration.

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

- Put the `#include` lines of the standard library before the `import`, or use `import std;`.
- Include the standard headers you use yourself. The module exports the names in `ui` and nothing else, so `std::optional`, `std::move` and `std::vector` need `<optional>`, `<utility>` and `<vector>` even where a libwinui type uses them. A coroutine that awaits a `ui::Task` needs `<coroutine>`.
- The literals work as before, with `using namespace ui::literals;` or `using ui::operator""_fr;`.

## Module or Headers

A program uses the module or the headers, not both. The headers are included inside the module, so what they declare belongs to the module `winui` there, and the library's sources are compiled once more as units of that module. `winui::winui` is therefore a library of its own, apart from `winui::headers`, and the two never link into one program.

Keep every libwinui `#include` out of a program that imports the module, also the ones in a header of your own and in other libraries you link. A mix shows up as a compile error about a redefinition or as an unresolved symbol when linking.

## Macros

A module exports no macros. `<winui/Macros.hpp>` has all of libwinui's, and declares nothing, so a file that imports the module can include it:

- `LIBWINUI_VERSION_MAJOR`, `LIBWINUI_VERSION_MINOR` and `LIBWINUI_VERSION_PATCH`, for an `#if`. The version is also `ui::VersionMajor`, `ui::VersionMinor` and `ui::VersionPatch`, which work in `if constexpr` and `static_assert` without the header.
- `LIBWINUI_NO_DEPRECATION_WARNINGS` silences the deprecation warnings only when the module is compiled with it. Add it as a compile definition for the whole build, not with a `#define` in a source file.
- `LIBWINUI_THREAD_CHECKS` comes from the CMake option of the same name, so the module and your files agree on it.

## The Standard Library

By default the module includes the standard headers before its module declaration. `LIBWINUI_MODULE_IMPORT_STD=ON` makes it use `import std;` instead, which needs CMake to provide `import std;` for C++23: CMake 4.5 and newer, or CMake 4.4 with its experimental gate `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD` set before `project()` in the top-level project. CMake 4.4 offers it for clang only with libc++ or libstdc++, so clang-cl builds keep the headers there.

With `import std;` in the module, every target that links `winui::winui` needs the `CXX_MODULE_STD` property on, even one that never writes `import std;` itself. MSVC defines a few standard constants (`std::strong_ordering::less`, ...) only in the standard library module's object file, and CMake links it only into targets with the property:

```cmake
set_target_properties(my_app PROPERTIES CXX_MODULE_STD ON)
```

An installed libwinui built that way hands it on: the consumer's CMake has to provide `import std;` as well, with CMake 4.4 through the same gate in its own top-level project.

libwinui's own build turns `import std;` on wherever CMake provides it (`AUTO`, the default, does that only when libwinui is the top-level project) and sets the property on its targets.

Either way your files can use the standard headers or `import std;`. Both name the same entities.

## Compilers

CI builds the modules with MSVC and clang-cl, imports them in every example and tutorial, and imports the installed package once more from a project outside the tree. GCC is not tested. The module library is static; with `LIBWINUI_BUILD_SHARED` the shared library is for the headers only.
