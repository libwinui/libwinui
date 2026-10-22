# Tutorials

From an empty project to a complete app, in one course and three tracks.

## Who They Are For

C++ developers who are new to libwinui. You should be at home with modern C++ (lambdas, move semantics, `std::unique_ptr`), but you need no background in Win32 programming. Every page explains the concept first, shows the code, then says what happens when it runs.

## What the Tutorials Cover

The tutorials are one course followed by three tracks.

| Section | What it covers |
| --- | --- |
| [Basics](basics/index.md) | A first window, layout, state and bindings, events and commands, a complete form, components, and background work. Everything else builds on it. |
| [Collections](collections/index.md) | Lists and grids of data, from a handful of rows to a hundred thousand. |
| [Navigation](navigation/index.md) | An app frame with pages, and the dialogs, flyouts and menus that float above it. |
| [Extending the Tree](extending-the-tree/index.md) | Your own controls, native Win32 controls, and mixing hand-built subtrees with declared ones. |
| Guides | [Themes](guides/themes.md), [Testing Your UI](guides/testing.md), [Accessibility and UI Automation](guides/accessibility.md) and [Built-in Text](guides/built-in-text.md). |

## Reading Order

Start with Basics and read 01 to 07 in order. Each page assumes the ones before it.

The three tracks stand on their own and can be read in any order after that. If you are in a hurry, Basics 01 to 03 are enough to follow them, and come back to [Components](basics/06-components.md) when a page builds its UI from functions returning `ui::Spec`.

## Prerequisites

- A C++23 compiler: MSVC 19.3x, Clang 16 or newer, or GCC 14 or newer.
- CMake 3.28 or newer.
- Windows to run the result. The library and the tutorials also compile elsewhere, but only Windows gets a window.
- Network access the first time you configure, since CMake downloads a few small dependencies.

To build and run a tutorial from a checkout of the repository:

```sh
cmake -S . -B build
cmake --build build --config Debug --target tutorial_03_state_and_bindings
./build/bin/Debug/tutorial_03_state_and_bindings.exe
```

Each tutorial page names its build target at the top.

## Where the Code Comes From

Every tutorial is a small program under `examples/tutorials/`, and the code on its page is taken from that file when the site is built, so the snippets always compile.
