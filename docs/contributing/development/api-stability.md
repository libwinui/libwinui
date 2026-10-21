# API Stability

What a release promises about the public API, and the rules that keep a change inside that promise.

## The Promise

Until 1.0, any release may change the API. From 1.0 on, inside one major version:

| Tier | Promise |
|---|---|
| App (`ui::` specs, `Window`, `Property`, `bind`, `run`, value types) | Source compatible. A name is deprecated for at least one minor release before it goes |
| Extending (`Element`, `Renderer`, `Theme`, input events) | Source compatible for code that calls it. A new virtual function always has a default body, so a class that overrides keeps compiling |
| Testing (`ui::testing`) | Best effort. A minor release may change it, and the changelog says so |
| Backend (`src/platform/`) | None. It is private and never installed |

There is no ABI promise. A shared build of libwinui only works with the headers of its own version, built by the same compiler with the same options.

## Rules for a Change

- **Spec fields.** Append or insert a field, never reorder or remove one. C++ requires designated initializers in declaration order, so a reorder breaks every call site that names two fields. Remove a field only after deprecating it.
- **Enums.** Give an explicit value to every enumerator that gets stored or sent somewhere, such as a value an app saves to its settings, so adding another never changes it.
- **Variants.** `RenderCommand`, `Brush`, `Animation` and `EasingFunction` grow. Code that visits one needs a catch-all case.
- **Virtual functions.** A new virtual on a class users derive from has a default body. Every interface says in its comment whether you implement it (`Element`, the layouts, the automation providers) or call it while the library implements it (`Renderer`, `TextMeasurer`, `Clipboard`). Before 1.0 a call-it interface may gain a pure virtual. From 1.0 on every new virtual has a default body, since a backend or a test double outside the library implements the call-it interfaces too.
- **Parameters.** A function that may need more parameters later takes an options struct. No new positional `bool`.
- **Defaults.** A changed default value changes behavior. It goes in the changelog like a signature change.
- **Deprecation.** Mark the old name with `LIBWINUI_DEPRECATED("use X")` and keep it working. Defining `LIBWINUI_NO_DEPRECATION_WARNINGS` before the first libwinui include silences the warnings.

## Seeing a Change

`api/surface.txt` lists every public declaration, one per line, and `api/surface-testing.txt` the testing tier. Both are generated from the headers (see [Documentation](docs.md#the-api-surface)), and `api-check` fails when they are stale. A commit that changes the public API changes one of these files, so the change shows up in review.

`LIBWINUI_VERSION_MAJOR`, `LIBWINUI_VERSION_MINOR` and `LIBWINUI_VERSION_PATCH` in `<winui/core/Version.hpp>` let code test the version with the preprocessor.
