# Add a Theme Era

A palette, its token tables, and the research behind them.

An era pack makes every control in the catalog look like one period of Windows: Fluent 2, Fluent, MDL2, Metro, Aero, Luna, Windows 9x, Windows 3.x and Windows 1.x/2.x ship today. Behavior, layout rules and semantics stay the same across all of them. Only the data in `Theme` changes, plus the handful of border and fill constructions that data selects. [Theming](../how-it-works/theming.md) explains the model; this page lists what to touch.

| Step | File |
| --- | --- |
| 1. Research | the era's own shipped files, written up under `docs/background/themes/` |
| 2. The variant | `include/winui/ui/themes/Theme.hpp` (`ThemeVariant`) |
| 3. The palette | `include/winui/ui/themes/<Era>Palette.hpp` (and a `.cpp` if it has functions) |
| 4. The derivation | `Theme::<era>()` in `include/winui/ui/themes/Theme.hpp`, `src/ui/themes/Theme<Era>.cpp`; the controls' own fields are in `src/ui/themes/ControlChrome.hpp` |
| 5. The resolver | `src/ui/themes/ThemeSource.cpp` |
| 6. High contrast | `src/ui/themes/ThemeSource.cpp` (`resolve_era()`) |
| 7. Tests | `tests/test_theme.cpp`, the variant lists in other tests, `tests/test_golden.cpp` |
| 8. Gallery and docs | `examples/gallery/main.cpp`, `docs/tools/control_map.toml` (`theme_headers`), a page under `docs/background/themes/` plus its `nav` entry in `zensical.toml`, and a section on `docs/contributing/how-it-works/era-packs.md` |

## What a Pack Is

A pack is a function from a request to a fully populated `Theme`. It has up to four layers:

1. **Tier 1, the palette.** A flat struct of the era's own raw values, named by role: `Palette` for Fluent, `ClassicPalette` for the 95/98/2000 bevel family and Windows 3.x, `MetroPalette` for Windows 8.1, `Mdl2Palette` for Windows 10 before Fluent, `EarlyPalette` for Windows 1.x/2.x. A palette holds values, not decisions.
2. **Tier 3, the derivation.** A static function on `Theme` (`Theme::fluent()`, `classic()`, `metro()`, `mdl2()`, `early()`) that turns one palette into every token (button visuals per state, surfaces, focus visuals, the type ramp, radii, motion) and every built-in control's own chrome (toggles, slider, fields, rows; the private `detail::ControlChrome`). It is the analog of a WinUI `*_themeresources.xaml` file.
3. **The resolver.** A `resolve_<era>()` function in `src/ui/themes/ThemeSource.cpp` that picks the palette (and dark/light, and the accent, where the era has them) and adds anything the derivation cannot know: chrome hints for DWM, popup motion, per-era overrides.
4. **Optional image data.** A pack whose look cannot be drawn from primitives could use the image theme backend (below). No shipped pack does.

Luna and Aero skip the first two layers. Each is a hand-built resolver (`resolve_luna()`, `resolve_aero()`) because their looks are gradients and glass tuned per control, which do not reduce to a small table of roles. Prefer a palette plus a derivation when the era has a real color model; a user scheme (Eggplant, High Contrast Black) is then just another palette literal, which is how `ThemeRequest::classic_scheme` works.

## Palette

Write the palette header in `include/winui/ui/themes/`, next to the others, and add it to `theme_headers` in `docs/tools/control_map.toml` so its reference page lands under Themes. Name fields by role, not by the era's resource keys. A role survives an era, a key name does not, and the derivation reads roles.

```cpp
/// The <era> tier-1 color tokens, transcribed from <the era's own file>.
struct EraPalette
{
    Color window{};        ///< COLOR_WINDOW: the page behind content.
    Color window_text{};   ///< COLOR_WINDOWTEXT.
    Color face{};          ///< The control face.
    Color highlight{};     ///< Selection background.
    // ...

    /// The era's default scheme, as shipped.
    [[nodiscard]] static EraPalette default_scheme();
};
```

Every era has to answer the same questions, because `Theme` has a field for each:

- page, text and disabled text colors, and the selection pair;
- the push button face and border per state (rest, hover, pressed, disabled), and the accent or default button;
- the toggle marks (check box, radio button, toggle switch) checked, unchecked and indeterminate;
- field, popup, tooltip and scrollbar surfaces, and the severity colors for InfoBar and InfoBadge;
- the focus visual (Fluent's two rings, Metro's dashed pair, the classic dotted rectangle);
- the UI font, the chrome font if it differs, the icon font, and the type ramp;
- corner radii, border style (`Flat`, `Rounded`, `Raised`, `Sunken`) and paddings;
- motion: state transitions, popup animations, or none;
- chrome: what DWM should do with the caption and corners (`Theme::chrome`).

An era with no answer to one of these (Windows 1.x has no button face, no highlight color and no gray text) still has to fill the field. Fill it with what the era actually did instead (reverse video for selection, ink for every severity), and write that down.

## Token Tables

The derivation goes into `src/ui/themes/Theme<Era>.cpp` (add it to the library sources in `CMakeLists.txt`) and its declaration into `Theme` in `include/winui/ui/themes/Theme.hpp`:

```cpp
/// The <era> tier-1 -> tier-3 derivation (Theme<Era>.cpp), sibling to fluent()/classic().
[[nodiscard]] static Theme era(const EraPalette& palette);
```

The tokens are `Theme`'s public fields; the built-in controls' own records (`check_box_box`, `slider_thumb`, `field_fills`, `press_state_flip`, ...) go through `detail::edit_controls()`, which hands out the theme's private `ControlChrome` for writing. Take that reference once, right after the `Theme` is made, and keep the whole era in this one function:

```cpp
Theme Theme::era(const EraPalette& p)
{
    Theme theme;
    theme.variant = ThemeVariant::Era;
    detail::ControlChrome& controls = detail::edit_controls(theme);

    detail::edit_colors(theme).background = p.window;
    detail::edit_colors(theme).text       = p.window_text;

    theme.button_normal.background   = p.face;
    theme.button_normal.border_style = BorderStyle::Flat;
    controls.toggle_box_size         = 13.0f;
    // ... every ButtonVisual state, the toggles, the surfaces, focus, type, motion ...
    return theme;
}
```

Start from the Fluent defaults a bare `Theme` has, then set every field the era has an opinion on. A field left alone draws Fluent Light, which is the most common way an era pack goes wrong. Grep `Theme.hpp` and `src/ui/themes/ControlChrome.hpp` for fields you did not touch and decide each one.

Two kinds of field need extra care, because leaving them alone goes wrong in a less obvious way:

- **Fields only `Theme::fluent()` fills.** `Theme::palette` stays default under every other derivation, and `Color` default-constructs to opaque black, not transparent. `Theme::fluent()` also fills fields the other derivations do not restate, such as `colors.divider`, `severity`, `overlay_corner_radius`, and in the control chrome `progress_track`, the `NavigationShell` indicator and the indeterminate check box. A new resolver has to set every such field itself. Otherwise it either paints opaque black (an ungated `theme.palette.*` read, or `Splitter` drawing `colors.divider`) or quietly inherits the Fluent value (Windows 11 popup corners, the accent selection bar).
- **Fields that look like they can be seeded from another.** Do not copy `Theme::text_style` from the type ramp's Body entry: the ramp is leading-aligned, which is right for page text, but `Button` draws its label into its full bounds, so the label ends up against the left border. `text_style` stays centered on both axes. Likewise `ToggleSwitch` reads its own `toggle_switch_shape` (control chrome) and never infers "retro" from the check box's border style.

[Theming](../how-it-works/theming.md#reading-a-token-that-every-era-fills) lists the tokens that mean something different from one era to the next.

Mark which values are measured and which are approximated. A value read out of the era's shipped files (a XAML resource dictionary, a `.msstyles` INI, a system color table in a setup file, a driver's color table) is measured, and the comment names the file and key. A value estimated from screenshots, or borrowed from a neighboring era because the era's own is not on disk, is approximated, and the comment says so and says why. Tests pin measured values exactly.

Then wire the resolver in `src/ui/themes/ThemeSource.cpp`:

1. Add a `resolve_<era>()` function, which calls the derivation and sets what it cannot know (chrome hints, popup motion, era-wide switches such as the control chrome's `accent_selection_indicator`).
2. Decide whether the era has a dark mode and a user accent. If yes, add it to `traits_of()` and to the end of `resolve_era()`, which hands it `dark` and `accent`. If not (a fixed, light-only palette), add it to `resolve_retro()`. `theme_variant_supports_dark_mode()` follows `traits_of()` on its own. A period pack is only ever reached through `resolve_era()`, which `enable_era_themes()` installs, so an app that does not call it does not link the pack.
3. Add a case to the high contrast `switch` in `resolve_era()`. The OS's contrast theme always wins; a forced contrast mode with the OS in normal mode uses the era's own contrast scheme. Either way the era keeps its shape (bevels stay bevels) and only the colors change.
4. If the era shipped several alternative palettes, expose them through a `ThemeRequest` field the way `classic_scheme` and `early_scheme` do.

Finally add the `ThemeVariant` value in `include/winui/ui/themes/Theme.hpp` with a doc comment that says which Windows versions it covers and what it merges, and set `theme.variant` to it in the derivation or resolver (`resolve_theme()` stamps the requested one on its result too, which is what a high-contrast theme built from another era's pack relies on). Append it after the existing values. `resolve_retro()` and the high contrast `switch` in `resolve_era()` list every value without a `default`, so the compiler's switch warning points at them. `traits_of()`, `is_era()` and the end of `resolve_era()` stay silent, so check those by hand.

## The Image Theme Backend

`src/render/ImageThemePack.hpp` (private) has an `ImageThemePack`: named images with nine-slice margins, drawn through `Renderer::draw_image_nine_slice()`. The Direct2D renderer can hold one, and a lookup that finds no image draws nothing, so a pack is optional by construction.

It is the honest answer only when an era's look is bitmap artwork that the primitives cannot reproduce closely enough. Every shipped pack, Luna and Aero included, is built from primitives (gradients, bevels, strokes), which scale to any DPI and show up in semantic goldens. Nothing in the public host installs an image pack yet.

The assets in a pack must be originally authored, freely licensed, or generated by libwinui itself. Extracting images from a `.msstyles` file or any other Windows resource is a copyright violation and out of scope, whatever the era.

## Research to Read

Read the era's page under [Theme Eras](../../background/themes/index.md) first. Each page collects what the era's own documents and files say: colors, metrics, fonts, motion, per-control notes, and the sources behind them. When you add a new era, add a page for it there (and to `nav` in `zensical.toml`), with the sources as footnotes, before or alongside the code. The era page stays a generic reference: no libwinui types, no `ThemeVariant`, no reference links. What the pack reproduces, approximates and cannot goes into a section of [Era Packs](../how-it-works/era-packs.md). The code comments then only need to name the file and key a value came from.

Two rules from the existing packs:

- An era is a sibling of Fluent 2, not a variant of it. Do not start from Fluent 2's derivation and tweak; start from the era's own model.
- When the era's evidence and a modern expectation disagree, the evidence wins, and the test says which source it follows.

## Tests

Add cases to `tests/test_theme.cpp`, tagged `[theme][retro][<era>]` for a fixed-palette era:

```cpp
TEST_CASE("Era's push button is a flat face inside a 1px frame", "[theme][retro][era]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Era}, OsThemeSnapshot{});

    CHECK(theme.button_normal.background == Color::from_argb(0xFFC0C0C0));
    CHECK(theme.button_normal.border_style == BorderStyle::Flat);
}
```

Pin each measured value with the source in the test's comment, and assert the era's absences as well as its presences (no hover state, no animation, no accent bar). A render test that builds a control under the new theme and checks the `TestRenderer` stream catches construction mistakes that value checks miss.

Then add the variant to every hand-written list of variants in the tests. `AllVariants` in `tests/test_theme.cpp` drives the high contrast and cross-era regression cases, and several control tests have their own inline list:

```sh
grep -rn "ThemeVariant::Windows1x}" tests/
```

A pixel golden for the era is optional. If the era introduces a construction nothing else draws, add the control-suite scene for it to `tests/test_golden.cpp`, as Windows 9x, Luna and Metro have.

Last, add the variant and its display name to `ThemeVariants` and `ThemeVariantNames` in `examples/gallery/main.cpp`, and run the gallery through a few pages in the new theme.
