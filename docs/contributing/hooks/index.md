# Hooks

One page per kind of contribution, walking the real files in the order you touch them.

## The Hooks

| Page | When | Main files |
| --- | --- | --- |
| [Add a Control](add-a-control.md) | A new control, from an empty header to a gallery page and a reference page. | `include/winui/ui/<group>/<Spec>.hpp` (control and spec), `src/ui/<group>/<Spec>.cpp`, `tests/test_<control>.cpp`, `examples/gallery/pages_<group>.cpp`, `docs/tools/control_map.toml` |
| [Add a Spec Field](add-a-spec-field.md) | A control grew a property and its spec should expose it. | `include/winui/ui/<group>/<Spec>.hpp`, `src/ui/<group>/<Spec>.cpp`, `tests/test_spec.cpp` |
| [Add a Theme Era](add-a-theme-era.md) | A new look for the whole catalog, selected by `ThemeVariant`. | `include/winui/ui/themes/ThemeSource.hpp`, `src/ui/themes/ThemeSource.cpp`, a palette header and a `Theme::<era>()` derivation, `tests/test_theme.cpp` |
| [Add a Render Command](add-a-render-command.md) | A control needs to draw something no existing primitive can express. | `include/winui/render/Renderer.hpp`, `TestRenderer.hpp`, `RenderRecording.hpp`, `src/render/Describe.cpp`, `src/platform/windows/D2DRenderer.hpp` |
| [Add a Platform Seam](add-a-platform-seam.md) | A control needs an operating system service. | `include/winui/render/<Seam>.hpp`, `include/winui/testing/Test<Seam>.hpp`, `src/platform/windows/Win32<Seam>.hpp`, `src/platform/windows/Win32<Seam>.cpp`, `Window`, `src/platform/windows/Win32WindowHost.cpp` |

Each page assumes you have read the matching [How It Works](../how-it-works/index.md) page and know the [conventions](../development/conventions.md). The code sketches are trimmed from the real sources; open the files they name for the full versions.
