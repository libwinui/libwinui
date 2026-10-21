# Documentation

How this site is built and kept in sync with the code.

## Zensical

The site is built with [Zensical](https://zensical.org) from Markdown under `docs/`. The configuration is `zensical.toml` in the repository root: the navigation, the theme features and the Markdown extensions.

| Path | What it is |
| --- | --- |
| `zensical.toml` | Site configuration and the whole navigation tree. A new page needs an entry in `nav`, or it is built but not reachable from the menu. |
| `docs/index.md` | The home page. |
| `docs/tutorials/` | The tutorials, one page per step, with code pulled from `examples/tutorials/`. |
| `docs/reference/` | The API reference. Generated, apart from `index.md`, `conventions.md` and `themes/index.md`. |
| `docs/contributing/` | Development, how it works, and the hook pages. |
| `docs/background/` | History, the theme eras and the studies behind the design. |
| `docs/tools/` | The reference generator, its Doxygen configuration and `control_map.toml`. |
| `docs/assets/` | Extra CSS. |

The enabled Markdown extensions include admonitions (`!!! note`), collapsible blocks (`??? note`), content tabs, footnotes, tables, definition lists, Mermaid diagrams (a `mermaid` fenced block) and snippets (`--8<--`). Snippet paths are relative to the repository root, and a missing file fails the build.

## The Reference Generator

Everything under `docs/reference/` except the three hand-written pages comes from the public headers. The pipeline is:

1. Doxygen reads `include/winui/**/*.hpp` with `docs/tools/Doxyfile` and writes XML only, into `build/doxygen/xml/`. It sees only `///` comments.
2. `docs/tools/gen_reference.py` reads that XML and `docs/tools/control_map.toml`, and writes one Markdown page per spec/control pair, class, struct or namespace.
3. The script also rewrites the block between the `BEGIN generated reference nav` and `END generated reference nav` markers in `zensical.toml`. Never edit that block by hand.

`control_map.toml` decides where things land:

- A `[[page]]` entry puts one or more specs (`specs`) and the control they build (`control`) on one page, with `related` types, the `headers` it claims, the `tests` to list and an optional `example` snippet (`"path:region"`). A page in the `controls` or `layout` section needs a `group` from `[section_groups]`; that is what puts it into the right sub-menu.
- A `[[page]]` with `related` but no `headers` is a topic page that bundles small types that would make poor pages on their own.
- Every other public class and struct gets a page named after it, in the section its header's directory maps to (`[sections]`: `core` to core, `render` to rendering, `ui` to tree, `testing` to testing). Headers listed in `theme_headers` go to the themes section instead.
- The free functions, enumerations, aliases and constants of each namespace go on one page per namespace and section.
- `hidden_namespaces` (`ui::detail`) is never documented.

Every generated file starts with a marker comment on its first line. The script never overwrites a file without the marker, and deletes marked files it no longer produces, so a renamed class does not leave a stale page behind.

```sh
python docs/tools/gen_reference.py                  # run doxygen, then generate
python docs/tools/gen_reference.py --skip-doxygen   # reuse build/doxygen/xml
python docs/tools/gen_reference.py --check          # exit 1 if anything would change
```

`--check` is the one to run before committing a header change. It lists the files that are out of date without writing them.

What this means for code:

- A doc comment on a public declaration is reference documentation. Write it for someone who has the reference page open and not the header.
- A new control needs an entry in `control_map.toml` with a `group`, or it gets a generic class page under Tree and window instead of a control page.
- Spec fields appear in declaration order, and the common fields are folded into one line that links to [Conventions](../../reference/conventions.md#common-fields-on-every-spec).

## The API Surface

`api/surface.txt` lists the public API one declaration per line, from the same Doxygen XML: every type in `ui` with its public and protected members, spec fields in declaration order, enumerators with their values, and the free functions, aliases and constants. `api/surface-testing.txt` holds the testing tier. A change to either file in a commit shows the reviewer that the public API moved.

```sh
cmake --build build --target api-surface   # run doxygen, rewrite both files
cmake --build build --target api-check     # exit 1 if one is stale
```

Both run Doxygen and then `winui_api_surface` (`tools/api_surface.cpp`). Regenerate the files with every header change, next to the reference.

## Tutorial Snippets

Tutorial code lives in real projects, one per page: `examples/tutorials/<nn>_<name>/main.cpp` with its own `CMakeLists.txt`, listed in the top-level `CMakeLists.txt`. It is built with the examples, so the code a tutorial shows always compiles.

The source marks regions with snippet comments on their own lines:

```cpp
// --8<-- [start:state]
ui::Property<int> clicks{0};
// --8<-- [end:state]
```

and the page pulls a region in by path and name:

````markdown
```cpp
;--8<-- "examples/tutorials/01_first_window/main.cpp:state"
```
````

<!-- The leading `;` above escapes the snippet line so this page shows it instead of including it. -->

A `[[page]]` in `control_map.toml` can use the same `"path:region"` form as its `example`, which puts the snippet at the end of that control's reference page.

To add a tutorial, add the project under `examples/tutorials/`, add its `add_subdirectory()` to `CMakeLists.txt`, write the page, and add the page to `nav` in `zensical.toml`.

## Building the Site Locally

Zensical needs a Python virtual environment in the repository root, and the reference needs Doxygen on `PATH`:

```sh
python -m venv .venv
.venv/Scripts/pip install zensical      # .venv/bin/pip outside Windows
```

With [uv](https://docs.astral.sh/uv/) the same environment is two commands, and `--clear` replaces one that no longer works (a venv records the path of the Python it was made with, so it breaks when that Python moves or the folder is copied to another machine):

```sh
uv venv --clear .venv
uv pip install --python .venv zensical
```

Without any environment, `uvx zensical serve` and `uvx zensical build` run Zensical straight from uv's cache. The `docs` CMake target still needs the `.venv` (or `zensical` on `PATH`) to exist at configure time.

Two CMake targets drive it. They only exist when their tools are found at configure time, so re-run `cmake -S . -B build` after installing them:

```sh
cmake --build build --target docs-reference   # doxygen + gen_reference.py
cmake --build build --target docs             # docs-reference, then zensical build into site/
```

For writing, run the live preview. It rebuilds on every save:

```sh
.venv/Scripts/zensical serve
```

The preview does not rerun the generator. After changing a header comment, run `docs-reference` (or `gen_reference.py --skip-doxygen` after a Doxygen run) and the preview picks up the new pages.

## Deploying

`zensical build` (or the `docs` target) writes a static site to `site/`, which any static file server can host. To look at the built site locally:

```sh
python -m http.server --directory site 8000
```

`site/` is ignored by git.

## Comment Rules

The code keeps what only the code can say, and this site keeps the rest.

**Doc comments on public declarations use `///`.** Every public class, struct, enum, function, member and alias gets one. Doxygen reads only `///` (and `///<` trailing a member or enum value), so a `//` comment on a public declaration never reaches the reference.

- The first sentence is the summary. The generator uses it in tables and member lists, so make it stand on its own.
- Say what the member does in libwinui's own terms. Doc comments name no WinUI, XAML or WPF property, event or type.
- An example goes between `@code` and `@endcode`, and it has to compile. The whole-tree build compiles every example (`winui_doc_example_check`, from `tools/doc_examples.cpp` and `cmake/DocExamples.cmake`): definitions such as a struct or a function go to namespace scope, the other statements into a function body, in their order. `window`, `renderer`, `theme`, `measurer` and a few app functions like `save()` and `open_file()` exist without a declaration (`tools/doc_examples/placeholders.hpp`). Anything else the example uses, it declares. A piece of a member function becomes a small class with that member; an object the example works on is built in the example or taken as a parameter. The check compiles eight headers' examples per translation unit, so a batch stops at the compiler's error limit; configure with `-DLIBWINUI_DOC_EXAMPLES_BATCH=1` to see every failing example at once.
- An example that can't stand on its own, such as one needing `<windows.h>`, opts out with `/// <!-- not compiled: reason -->` on the line before `@code`. The reference doesn't show the comment.
- Markdown works inside doc comments, and backticked or bare type names are linked to their reference pages.

```cpp
/// A small count, icon or dot badge. Builds a ui::InfoBadgeControl.
///
/// Example:
/// @code
/// ui::InfoBadge{.value = ui::bind(unread)}
/// @endcode
struct InfoBadge
{
    /// The number; unset renders the icon, or the dot form when there is none.
    ui::Binding<std::optional<int>>           value{};
    ui::Binding<std::optional<ui::Thickness>> margin{}; ///< Outer margin, DIPs.
};
```

**Everything else uses `//`**: the `// SPDX-License-Identifier: MIT` line, `// ---- section ----` dividers, private members, and comments inside function bodies.

**Other comments are rare.** A `//` comment is one or two lines. It explains a surprising why, or names context the reader can look up, such as a Win32 API or a spec document. It does not narrate what the next line does.

```cpp
// Never narrower than tall, so a one-digit badge stays a circle.
desired.width = std::max(desired.width, desired.height);
```

**Longer explanations belong on this site.** A design decision, a measured table of era values or the history of a bug goes into a page under [How It Works](../how-it-works/index.md) or [Background](../../background/index.md), not into a comment block.

**Code never points at markdown files.** No comment refers to a page on this site, a plan document or a research note by path or section number. Pages move and sections get renumbered; a reference into them rots silently. Name the thing itself instead (the API, the Win32 call, the test).
