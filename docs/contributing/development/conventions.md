# Conventions

The rules that keep the tree navigable. They are the C++ side; the rules a library user meets when writing specs are in the reference's [Conventions](../../reference/conventions.md).

## Include Mirror

`include/winui/<dir>/` mirrors `src/<dir>/` one to one. A header and its `.cpp` sit in matching directories, and a header-only component sits in the directory of the subsystem it serves.

```text
include/winui/                     src/
    core/      Geometry, Color, Input, Property, Callback, ...       core/
    render/    Renderer, Describe, Text, Bitmap, ...                render/
    ui/        Element, Window, one header per control and its spec  ui/
        themes/  Theme, the palettes, the theme source              themes/
        spec/    the shared spec machinery (Spec, Control, ...)     spec/
    testing/   HeadlessUI, TestRenderer, the test doubles,         testing/
               OffscreenCapture (the winui::testing library)
                                                                   platform/windows/  (private)
    winui.hpp  umbrella header
    testing.hpp  the testing tier's umbrella header
```

When you add a public header, put it in the `include/` directory that matches where its `.cpp` goes in `src/`. A new `.cpp` also goes into `LIBWINUI_CORE_SOURCES` or, for Windows-only code, `LIBWINUI_WIN32_SOURCES` in the top-level `CMakeLists.txt` (`LIBWINUI_TESTING_SOURCES` for the testing tier, which builds into its own library, `winui::testing`), and a new control's header into `include/winui/winui.hpp`.

Private Windows headers that expose backend internals (`src/platform/windows/D2DRenderer.hpp`, `DWriteMeasurer.hpp`, `Win32Launcher.hpp`, ...) stay in `src/` and are never installed.

Two directories are special:

- `core/`, `render/`, `ui/` and `testing/` compile on any platform, and their headers never include `<windows.h>`. The few source files there that call the OS directly (the command line, the thread check, tracing, the performance counters, image decoding through WIC, local time) do it behind `#ifdef _WIN32`. `src/testing/OffscreenCapture.cpp` draws through the backend's private `D2DRenderer` on Windows and is an empty capture elsewhere.
- No public header includes an operating system header. The Windows backend's headers are private, under `src/platform/windows/`.

## Fully-Qualified Includes

Every include of a libwinui header spells the full path from `include/`:

```cpp
#include "winui/ui/Element.hpp"   // yes
#include "Element.hpp"            // no
#include "../render/Renderer.hpp" // no
```

A header then resolves the same way from anywhere, and moving a file is a search-and-replace of one string. clang-format regroups includes into blocks: quoted project headers first, then `<winui/...>`, then standard headers, then the C headers (`<cstddef>`, `<cstdint>`, ...).

## Namespaces

| Namespace | Holds |
| --- | --- |
| `ui` | Everything a user writes: the specs (`ui::Button`), the controls they build (`ui::ButtonControl`), `Element`, `Window`, `Property`, `Theme`, the core types, the render interface and the headless host. The library is called libwinui and its headers live under `winui/`, but the namespace is `ui`. |
| `ui::win32` | The private Win32 backend classes under `src/platform/windows/`. Nothing in it is public. |
| `ui::testing` | The testing tier, opt-in through `<winui/testing.hpp>`: `HeadlessUI`, `TestRenderer`, the test doubles for the platform seams, `OffscreenCapture`, `seams()`. |
| `ui::detail` | Implementation details that have to live in a header. Never documented. |

Small nested namespaces group constants and helpers: `ui::colors`, `ui::glyphs`, `ui::styles`, `ui::easing`, `ui::literals`.

## Naming

| Kind | Style | Examples |
| --- | --- | --- |
| Types, enums, concepts | `PascalCase` | `StackControl`, `DirtyFlags`, `SpecLike` |
| Functions, variables, parameters | `snake_case` | `set_text()`, `measure_override()`, `final_rect` |
| Private data members | `snake_case_` with a trailing underscore | `value_`, `bounds_`, `clipboard_` |
| Constants | `PascalCase` | `Unbounded`, `colors::White`, `styles::Spaced` |
| Enum values | `PascalCase` | `Alignment::Stretch`, `ThemeVariant::Luna` |
| Files | `PascalCase`, after the type they hold; a topic name when no type leads | `InfoBadge.hpp` (`ui::InfoBadge` and `ui::InfoBadgeControl`), `VisualId.hpp`, `Geometry.hpp` |
| Tests, examples, tools | `snake_case` | `test_info_badge.cpp`, `pages_basic_input.cpp`, `gen_reference.py` |
| Directories | lowercase | `ui/input/`, `platform/windows/` |

Name a thing after what it is. A control keeps its common name (`Button`, `Slider`, `TreeView`), and a name that only makes sense inside another framework gets a plain one (`Text`, `Stack`, `Dialog`). A public enum sits at namespace scope, never inside a class: `ui::` plus one name, prefixed by the owner's spec name when it belongs to one control (`SliderTickPlacement`, `PipsPagerWrapMode`, `ButtonVariant`). `ui::Orientation` and `ui::Alignment` are shared by every control; an enum that is only internal state stays nested and private. A control class is its spec's name plus `Control` (`ui::InfoBadge` builds an `InfoBadgeControl`, `ui::Rating` a `RatingControl`). The few pairs that do not follow the rule (`ui::VerticalStack` and `ui::HorizontalStack` both build a `StackControl`) are listed in the [reference conventions](../../reference/conventions.md).

Implementations of one interface follow one pattern:

| Role | Pattern | Examples |
| --- | --- | --- |
| The interface | the plain noun | `Clipboard`, `SpellChecker`, `TextMeasurer`, `ContextMenuHost` |
| The test double | `Test<Noun>` | `TestClipboard`, `TestSpellChecker`, `TestRenderer` |
| The Win32 implementation | `win32::Win32<Noun>` | `win32::Win32Clipboard`, `win32::Win32SpellChecker` |
| The headless implementation | `Headless<Noun>` | `HeadlessTextMeasurer`, `HeadlessWindowHost` |

Test tags are the control's name in kebab case (`[info-badge]`, `[dropdown-button]`), plus the area (`[layout]`, `[render]`, `[input]`).

## Ownership

Every node in the tree has exactly one owner, a `std::unique_ptr` in its parent. There is no `shared_ptr` anywhere in the UI tree.

- A method that adopts a child takes `std::unique_ptr<T>` and returns a plain `T*`, a non-owning observer for wiring up behavior.
- References that are not ownership (the focus ring, a window's pointer to its clipboard) are raw pointers or `std::vector<Element*>` into memory the tree already owns.
- An `Element` is not copyable, and only movable before it has a parent.
- COM objects live in `Microsoft::WRL::ComPtr`, and platform state behind a pimpl `std::unique_ptr`. The library never reaches into Windows' object lifetimes, and Windows never reaches into the tree's.

A setter compares before assigning, and marks only what changed:

```cpp
void InfoBadgeControl::set_severity(InfoBadgeSeverity severity)
{
    if (severity == severity_)
    {
        return;
    }
    severity_ = severity;
    mark_dirty(DirtyFlags::Visual); // the color changed, the size did not
}
```

Use `DirtyFlags::Layout` when the size can change, `Arrange` when only the placement of children changes, and `Visual` for appearance only. Invalidating more than needed costs a relayout, and invalidating less leaves a stale screen.

## `Callback` for Handlers

A handler a control stores is a `ui::Callback<Sig>`, which is `std::move_only_function<Sig>`. `ui::Action` is `Callback<void()>`. `std::function` requires a copyable target, so it cannot hold a lambda that captures a `std::unique_ptr` or any other move-only state.

```cpp
void on_click(Action handler);                            // yes
void on_value_changed(Callback<void(double)> handler);    // yes
void on_click(std::function<void()> handler);             // no, for a control's handler
```

The exception is an item type that callers put in a braced `std::vector` (`MenuItem`, `ThumbnailButton`, ...). `std::initializer_list` only hands out copies, so those handlers have to be copyable and use `std::function`.

Use the unqualified signature, so a caller may pass a `mutable` lambda that carries state. A handler that must run through a const path keeps it `mutable` instead of const-qualifying the signature: `CommandHandler` (a `Command`'s `execute`) runs from the `const Command*` that every button, menu item and shortcut pointing at the command shares.

## Strings

Every string in the library is a `std::string` holding UTF-8, and every text offset is a byte offset on a code-point boundary.

- `std::u8string` is not used: it has no `std::format` and no iostreams support, and `char8_t` would need a cast at every seam.
- A `static_assert` in `include/winui/core/Utf8.hpp` fails a translation unit compiled without `/utf-8` (MSVC, clang-cl), since string literals would otherwise not be UTF-8.
- The UTF-8/UTF-16 helpers in `Utf8.hpp` are hand-written rather than `MultiByteToWideChar`, so they behave the same on every platform without `<windows.h>`.
- Malformed input (an unpaired surrogate, a truncated or overlong sequence) becomes U+FFFD instead of an error, so a file name with one bad byte still displays. A truncated sequence swallows only its own bytes.

At the Win32 boundary, where every `...W` call takes UTF-16, `widen()` and `narrow()` in `src/platform/windows/Win32String.hpp` are the only crossing points. They follow the same U+FFFD policy (no `MB_ERR_INVALID_CHARS`/`WC_ERR_INVALID_CHARS`), so anything coming back from the OS, such as clipboard text or UI Automation values, is valid UTF-8 by the time it reaches the tree.

## Comment Style

Doxygen reads only `///`, and the [reference](../../reference/index.md) is generated from what it reads.

- `///` on every public declaration: classes, structs, enums, functions, members. `///<` for a short comment trailing a member or enum value.
- `//` everywhere else: the SPDX line, `// ---- section ----` dividers, private members, function bodies.
- Other comments are rare: one or two lines that explain a surprising why, or name something the reader can look up, such as a Win32 API. Longer explanations belong on this site, and code never points at markdown files.

The full set of rules, with examples, is on [Documentation](docs.md#comment-rules).

## Clang-Format

The checked-in `.clang-format` decides formatting. Its main points:

- 4 spaces, no tabs, 120 columns.
- Braces on their own line for classes, functions, namespaces and control statements, and braces around every `if`/`for`/`while` body, even a one-liner (`InsertBraces`).
- Consecutive declarations and assignments aligned in columns.
- `Type* name` and `Type& name` (pointer and reference bind to the type).
- One argument or parameter per line once a call does not fit.

Run it with `cmake --build build --target format`. See [Building](building.md#format) for the version it needs.
