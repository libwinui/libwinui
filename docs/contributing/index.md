# Contributing

How libwinui works inside, and where a change hooks in.

## Where to Start

Get the library building and the tests green first. [Building](development/building.md) covers the requirements, the CMake options and the commands; after a first build you have `winui_tests.exe` and the example apps in `build/bin/Debug/`.

Then run the gallery (`gallery.exe`). It has one page per control and a theme picker in its header that switches between every era pack and between light and dark. Most changes end with a look at the gallery page of the control you touched, in more than one theme.

## What a Contribution Looks Like

Most changes are one of a few shapes, and each has its own page under [Hooks](hooks/index.md):

- **A control.** A header and source file, its spec and `build()`, tests with `TestRenderer` and the headless host, a gallery page and a reference entry. See [Add a Control](hooks/add-a-control.md).
- **A spec field.** A new property on an existing description, without breaking a single call site. See [Add a Spec Field](hooks/add-a-spec-field.md).
- **A theme era.** A palette, the derivation that turns it into a `Theme`, a `ThemeVariant` value and the tests that pin its values. See [Add a Theme Era](hooks/add-a-theme-era.md).
- **A render command.** A new drawing primitive through the recorder, the semantic dump and the Direct2D backend. See [Add a Render Command](hooks/add-a-render-command.md).
- **A platform seam.** An operating system service behind an interface, with a test double and a Win32 implementation. See [Add a Platform Seam](hooks/add-a-platform-seam.md).

Bug fixes follow the same rule as everything else: a test that fails before the fix and passes after it, in `tests/`.

## LLM Usage

libwinui was built with the help of large language models (LLMs), and you're welcome to use them for a contribution too.

## Reading Order

1. [Development](development/index.md): build it, learn the [conventions](development/conventions.md), [write tests](development/write-tests.md), and keep [these docs](development/docs.md) in sync.
2. [How It Works](how-it-works/index.md): [the tree](how-it-works/the-tree.md), [specs and `build()`](how-it-works/specs-and-build.md), [layout](how-it-works/layout.md), [rendering](how-it-works/rendering.md), [input and focus](how-it-works/input-and-focus.md), [theming](how-it-works/theming.md), [era packs](how-it-works/era-packs.md) and [platform seams](how-it-works/platform-seams.md).
3. The [hook page](hooks/index.md) for the change you are making.
