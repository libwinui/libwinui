# Building

Requirements, configure, build, test, format.

## Requirements

- **CMake 3.28** or newer.
- **A C++23 compiler.** MSVC 19.3x, Clang 16 or newer, or GCC 14 or newer for the whole library. The platform-independent core also builds with GCC 13.
- **A Windows SDK** for the Windows backend. Configuring runs the C++/WinRT generator against the installed SDK's metadata.

CMake fetches the rest at configure time with `FetchContent`, pinned to fixed versions:

| Dependency | Used for |
| --- | --- |
| [stb](https://github.com/nothings/stb) (`stb_image`, `stb_image_write`) | Reading and writing PNGs for the pixel goldens, and decoding images off Windows. Compiled into the library, so consumers never see it. |
| [Catch2](https://github.com/catchorg/Catch2) v3.6.0 | The test binary. Only fetched when tests are built. With MSVC or clang-cl and a multi-config generator it is built once per machine and compiler instead, see `LIBWINUI_SHARED_CATCH2`. |
| C++/WinRT (NuGet package, Windows only) | Projection headers for `Windows.UI.ViewManagement` (the OS accent color ramp) and `Windows.System` (opening a URI with its default handler). Generated once into `build/cppwinrt_generated/`. |

Everything else is part of Windows: Direct2D, DirectWrite, WIC, DirectComposition, UI Automation and the usual Win32 libraries.

## Configure and Build

From the repository root:

```sh
cmake -S . -B build
cmake --build build --config Debug
```

With a multi-config generator (Visual Studio, Ninja Multi-Config) the binaries land in `build/bin/Debug/` or `build/bin/Release/`. With a single-config generator they land in `build/bin/`.

For day-to-day work, configure with Ninja Multi-Config, the precompiled header and unity batching from a shell with the MSVC environment, and build only the target you are working on:

```sh
cmake -S . -B build -G "Ninja Multi-Config" -DLIBWINUI_ENABLE_PCH=ON -DLIBWINUI_ENABLE_UNITY_BUILD=ON
cmake --build build --config Debug --target winui_tests
```

Build the whole tree once before calling a change done: the examples, the tutorials and the doc example check keep the documentation's code compiling.

The library target is `winui::winui`. An application links `winui::main`, which pulls the library in.

`winui::testing` is the testing tier, a library of its own built from `src/testing/`: `HeadlessUI`, `TestRenderer`, the test doubles and `OffscreenCapture`, all behind `<winui/testing.hpp>`. It links `winui::winui` publicly, so a test executable links only `winui::testing` (and its test framework); an app never links it. `winui_tests` links it. Installed, it is the package's `testing` component (see [Install and find_package](#install-and-find_package)).

`winui::main` is an interface target for applications. Linking it lets a `WIN32` executable use a plain `int main()` instead of `wWinMain`; command-line arguments then come from `ui::command_line()` as UTF-8. With MSVC and clang-cl it passes `/ENTRY:mainCRTStartup` to the linker, so the CRT calls an ordinary `main()` whatever the subsystem. MinGW's CRT already calls `main()` for a GUI executable, so there the target adds nothing. Prefer `ui::command_line()` over `argv`, which Windows hands over in the ANSI code page. The target is kept apart from `winui::winui` because the tests bring Catch2's own `main`, and a DLL linking the library has no entry point of this kind. With `LIBWINUI_BUILD_SHARED`, `winui::main_shared` does the same over `winui::winui_shared`.

On Windows the library links the usual Win32 libraries (`user32`, `gdi32`, `shell32`, ...) publicly, plus these:

| Library | Needed for |
| --- | --- |
| `d2d1`, `dwrite` | The renderer and the text measurer. |
| `windowscodecs`, `ole32` | WIC: image decoding and the offscreen golden capture. |
| `d3d11`, `dxgi`, `dcomp` | The DirectComposition overlay visuals. |
| `dwmapi` | Frame attributes, and the `DwmGetColorizationColor` accent fallback. |
| `windowsapp` | C++/WinRT: the `UISettings` accent ramp and the URI launcher. |
| `uiautomationcore` | The UI Automation provider returned from `WM_GETOBJECT`. |
| `shcore` | `GetDpiForMonitor`, for window placement. |
| `uxtheme` | `SetWindowThemeAttribute` (caption icon and title switches). |
| `comdlg32`, `prntvpt`, `winspool` | Printing: the print dialog, print tickets and the printer's settings. |
| `comctl32` | `SetWindowSubclass`, which the hosted edit box needs to take a message away from its own window class. |

GCC and Clang also need `Threads::Threads` (`-pthread`) linked explicitly for the job system's `std::jthread`; MSVC does not, but the target links it everywhere.

## Options

| Option | Default | Effect |
| --- | --- | --- |
| `LIBWINUI_BUILD_TESTS` | `ON` | Builds `winui_tests` and registers it with CTest. |
| `LIBWINUI_BUILD_EXAMPLES` | `ON` | Builds the example apps and one project per tutorial. |
| `LIBWINUI_BUILD_SHARED` | `OFF` | Also builds a shared library (`winui::winui_shared`, and `winui::testing_shared` on top of it) from the same sources, exporting every symbol. The DLLs are `winui.dll` and `winui_testing.dll` (`winuid.dll` and `winui_testingd.dll` in Debug); their import libraries are `winui_shared.lib` and `winui_testing_shared.lib` (`winui_sharedd.lib` and `winui_testing_sharedd.lib` in Debug), since `winui.lib` is the static library. The static library is always built. There is no ABI promise: the DLL only works with the headers of its own version, built by the same compiler with the same options. |
| `LIBWINUI_WARNINGS_AS_ERRORS` | `ON` top-level, `OFF` as a subproject | Every compiler warning is an error. The tree builds without a single warning under MSVC. |
| `LIBWINUI_ENABLE_ASAN` | `OFF` | AddressSanitizer for everything in the build. Under MSVC the runtime DLLs are copied next to each executable. |
| `LIBWINUI_ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer, with GCC and Clang off Windows. |
| `LIBWINUI_ENABLE_CLANG_TIDY` | `OFF` | Runs clang-tidy on each file as it compiles, with the checks in `.clang-tidy`. |
| `LIBWINUI_TIDY_JOBS` | `0` | How many clang-tidy processes the `tidy` target runs; `0` is one per core. |
| `LIBWINUI_ENABLE_IPO` | `OFF` | Link-time optimization for Release, RelWithDebInfo and MinSizeRel. |
| `LIBWINUI_ENABLE_HARDENING` | `OFF` | The exploit mitigations: `/sdl /guard:cf /CETCOMPAT` under MSVC, stack protection, `_FORTIFY_SOURCE` and `_GLIBCXX_ASSERTIONS` with GCC and Clang. |
| `LIBWINUI_ENABLE_UNITY_BUILD` | `OFF` | Compiles the library's sources in batches of `LIBWINUI_UNITY_BUILD_BATCH_SIZE` (16) files per translation unit. The tests stay out: each test file keeps small helpers of the same name. |
| `LIBWINUI_ENABLE_PCH` | `OFF` | Precompiles the standard library headers for the library and the tests. Our own headers stay out, so editing one does not rebuild the precompiled header. |
| `LIBWINUI_SHARED_CATCH2` | `ON` | Builds Catch2 once, Debug and Release, into `LIBWINUI_CATCH2_ROOT` (`%LOCALAPPDATA%/libwinui`, one folder per compiler and version) on the first configure, and links every later build tree and worktree against that copy. Falls back to fetching Catch2 into the tree with AddressSanitizer, a set `CMAKE_MSVC_RUNTIME_LIBRARY`, compilers without the MSVC ABI and single-config generators. Delete the folder to rebuild it. |
| `LIBWINUI_THREAD_CHECKS` | `AUTO` | Reports a tree, `Property` or `ObservableVector` call made on the wrong thread. `AUTO` turns it on unless `NDEBUG` is defined, `ON` forces it on and `OFF` compiles every check out. Public, so an app's own headers see the same setting. |
| `LIBWINUI_ENABLE_TRACING` | `OFF` | Emits TraceLogging events around layout, render, present, the animation tick and startup, so Windows Performance Analyzer shows where a frame's time goes. Off, the spans compile to nothing. |
| `LIBWINUI_DOC_EXAMPLES_BATCH` | `8` | How many headers' `@code` examples the doc example check compiles per translation unit. `1` shows every failing example at once (see [Documentation](docs.md)). |

`CMakePresets.json` bundles the common combinations into `out/build/<preset>/`, each build limited to 10 jobs. Run them from a Developer Command Prompt:

```sh
cmake --preset msvc                 # also clang-cl, msvc-asan, msvc-release, unix
cmake --build --preset msvc-debug
ctest --preset msvc-debug
cmake --build --preset tidy         # clang-tidy over every source file
```

A unity build needs the names a source file keeps to itself (in an anonymous namespace) to be unique across the library: two files that each define `MaxWidth` no longer compile once they share a translation unit. Prefix such a name with what it belongs to (`DialogMaxWidth`), or share it through a private header when it is the same value.

A configure from inside the source tree is refused, and a single-config generator without `CMAKE_BUILD_TYPE` builds RelWithDebInfo.

For example, a release build without examples:

```sh
cmake -S . -B build -DLIBWINUI_BUILD_EXAMPLES=OFF
cmake --build build --config Release
```

Every public string is UTF-8. With MSVC the library adds `/utf-8` as a public compile option, so your sources are read as UTF-8 too. A translation unit built without it fails to compile on purpose.

## Install and find_package

`cmake --install build --config Debug` and `--config Release` can share one prefix: every library gets a `d` postfix in Debug (`winuid.lib`, `winui_testingd.lib`), so the two installs never overwrite each other. The package config under `lib/cmake/winui/` picks the right one per configuration:

```cmake
find_package(winui 0.1.0 EXACT CONFIG REQUIRED)                       # winui::winui, winui::main
find_package(winui 0.1.0 EXACT CONFIG REQUIRED COMPONENTS testing)    # adds winui::testing
```

`tests/package/` is a project outside the tree that does exactly this: it finds an installed copy, builds an app against `winui::main` and a headless UI test against `winui::testing`, and runs the test. CI installs its MSVC and clang-cl builds and checks them this way. To try it locally, install into a prefix and configure it with `-DCMAKE_PREFIX_PATH=<prefix>`.

The version file only accepts the exact version that was installed, since there is no ABI promise. The headers carry the same version as `LIBWINUI_VERSION_MAJOR`, `LIBWINUI_VERSION_MINOR` and `LIBWINUI_VERSION_PATCH` (and `ui::VersionMajor`, ...) in `<winui/core/Version.hpp>`; configuring fails when those differ from `project(VERSION)`.

Names kept only for compatibility are marked `LIBWINUI_DEPRECATED("use ... instead")`, which is `[[deprecated]]` and names the replacement. Define `LIBWINUI_NO_DEPRECATION_WARNINGS` before the first libwinui include to silence them while migrating.

The thread checks (`LIBWINUI_THREAD_CHECKS`) never change a class layout, so an app built with a different `NDEBUG` than the installed library is fine; it only checks less or more.

## Running Tests

All tests live in one binary, `winui_tests`. Run it directly:

```sh
./build/bin/Debug/winui_tests.exe                     # everything
./build/bin/Debug/winui_tests.exe "[golden]"          # one tag
./build/bin/Debug/winui_tests.exe "[theme][retro]"    # tests with both tags
./build/bin/Debug/winui_tests.exe "Re-layout at a new size re-centers"  # one test case by name
```

`ctest --test-dir build -C Debug --output-on-failure` works too, since the tests are registered with `catch_discover_tests`. Running the binary directly is faster and gives Catch2's own output.

Note that a crash or a silent abort in `winui_tests` after pulling changes to headers is almost always a stale build. Do a clean rebuild before hunting for a bug.

[Write Tests](write-tests.md) covers tags, the headless host and the golden files, including how to regenerate them.

## Examples

`LIBWINUI_BUILD_EXAMPLES` builds these, all into the same `bin/` folder:

| Target | What it shows |
| --- | --- |
| `hello` | The smallest app: a window, a text and two buttons, declared with the spec API. |
| `virtual_list_demo` | A 100,000-row list that keeps about a screenful of rows alive. |
| `tile_view_demo` | A board of pictures in a TileView: check several and drag them together, carry them with Ctrl and the arrow keys, size the tiles with Ctrl and the wheel, and drop image files from Explorer where the gap opens. |
| `gallery` | Every control, one page each, in navigation groups, with a theme and dark mode picker. The main way to check a change by eye. |
| `window_chrome` | Window states (minimize, maximize, full screen), placement, title bar and taskbar features that tests cannot see. |
| `tray` | An app whose entry point is a notification-area icon, with no main window. |
| `tutorial_01_first_window`, ... | One project per tutorial page under `examples/tutorials/`, so the code the tutorials show always compiles. |

The examples are only added on Windows, since they need a backend. Off Windows the build has the library and its tests.

With both the tests and the examples on, Windows builds also get the benchmarks under `benchmarks/` (`winui_bench`, `winui_startup`, `winui_timers`, `winui_idle`). They measure timings and startup milestones and are never run by CTest.

## Format

The tree is formatted with clang-format and the checked-in `.clang-format`. The `format` target runs it over every `.hpp` and `.cpp` in `examples/`, `include/`, `src/`, `tests/` and `tools/`:

```sh
cmake --build build --target format
```

The configuration expects clang-format 22. If a different version is first on your `PATH`, point the build at the right one with `-DCLANG_FORMAT_EXECUTABLE=path/to/clang-format`. The target only exists when libwinui is the top-level project. Other versions align some constructs differently, so a run with the wrong version produces a large diff of unrelated whitespace changes.
