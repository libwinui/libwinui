# Era Packs

How each of libwinui's nine theme variants maps its design era onto `Theme` data: what the pack
reproduces exactly, what it approximates and why, and what it cannot do. The eras themselves, with
their measured values and sources, are documented under
[Theme Eras](../../background/themes/index.md); this page is the libwinui side of each one.
Fields linked into the reference are public `Theme` tokens; the unlinked per-control ones
(`slider_fills`, `toggle_box_radius`, `nav_pane`, ...) are the private control chrome
(`src/ui/themes/ControlChrome.hpp`, see [Theming](theming.md#theme-and-tokens)).

## Variants

| Era | `ThemeVariant` | Dark and light | Era page |
| --- | --- | --- | --- |
| the running Windows | `System` (default) | yes | Fluent 2 on Windows 11, Fluent on Windows 10 |
| Fluent 2 | `Fluent2` | yes | [Fluent 2](../../background/themes/fluent2.md) |
| Fluent | `Fluent` | yes | [Fluent](../../background/themes/fluent.md) |
| MDL2 | `Mdl2` | yes | [MDL2](../../background/themes/mdl2.md) |
| Metro (MDL) | `Metro` | yes | [Metro (MDL)](../../background/themes/mdl.md) |
| Aero | `Aero` | light only | [Aero](../../background/themes/aero.md) |
| Luna | `Luna` | light only | [Luna](../../background/themes/luna.md) |
| Windows 9x | `Windows9x` | light only | [Windows 9x](../../background/themes/win9x.md) |
| Windows 3.x | `Windows3x` | light only | [Windows 3.x](../../background/themes/win3x.md) |
| Early Windows | `Windows1x` | light only | [Early Windows](../../background/themes/winold.md) |

Fluent 2 is the framework's real target, and the goal there is to match Windows 11 as closely as
possible. Every other variant is an era pack: best effort, lower fidelity by comparison, and in a
few places a deliberate merge of neighboring releases into one representative look. Aero covers
Vista and 7, Metro covers 8 and 8.1, Windows 9x covers 95 and 98 (with Windows 2000's palette one
`ClassicPalette` away), and Early Windows covers 1.x and 2.x.

`System`, `Fluent2` and `Fluent` are always linked. The packs from `Mdl2` down are reached only
through `resolve_era()`, which [`enable_era_themes()`](../../reference/themes/namespace-ui.md#enable_era_themes)
installs behind a function pointer, so an app that never calls it links none of them or their window
frames. Without it, `resolve_theme()` draws a request for one of them as `System`.
`OsThemeSnapshot::system_variant`, read from the build number, says which Fluent `System` is.

Every pack follows [the one rule](theming.md#the-one-rule-every-pack-follows): only `Theme` data
changes. Where a pack draws something the era never had (a `ToggleSwitch` in Windows 95, a `Slider`
in Windows 2.x), it builds it from the era's own parts, and the pack's section below says so.

## Mode, Accent and High Contrast

An app picks an era with [`ThemeRequest::variant`](../../reference/themes/theme-request.md) and a
light or dark appearance with `ThemeRequest::mode`;
[`resolve_theme()`](../../reference/themes/namespace-ui.md#resolve_theme) combines the request
with the OS snapshot into a `Theme`. Dark and light are never a variant. They live on
[`ThemeMode`](../../reference/themes/namespace-ui.md#thememode), which defaults to `System` and
follows the OS setting live. Only `Fluent2`, `Fluent`, `Mdl2` and `Metro` honor `mode` and
`accent_override`
([`theme_variant_supports_dark_mode()`](../../reference/themes/namespace-ui.md#theme_variant_supports_dark_mode));
the five older eras are fixed, light-only palettes.

High contrast applies to all nine. While the OS is in a contrast theme the colors are the user's
own, read verbatim into a [`HighContrastPalette`](../../reference/themes/high-contrast-palette.md)
from `GetSysColor`. When an app forces `ThemeMode::HighContrast` without one, each era falls back to
the contrast scheme it shipped itself. Outside high contrast, `Theme` never reads `GetSysColor`; see
the [note on the era index](../../background/themes/index.md#light-dark-and-high-contrast) for why.

The two classic eras and Early Windows also take a user color scheme.
[`ThemeRequest::classic_scheme`](../../reference/themes/theme-request.md) swaps any
[`ClassicPalette`](../../reference/themes/classic-palette.md) into `Windows9x` or `Windows3x`
("Windows 98 with Eggplant" is one literal), and `ThemeRequest::early_scheme` selects one of the four
recovered [`EarlyPalette`](../../reference/themes/early-palette.md) tables for `Windows1x`.

## Per-Era Chrome and Rasterization

| Property | Where it lives | Per era |
| --- | --- | --- |
| Rasterization | `Theme::rasterization`, emitted once per frame as a `SetRasterization` render command | Early Windows, 3.x and 9x: aliased geometry, aliased text, snapped to device pixels. Luna: antialiased, grayscale text. Aero, MDL2 and Fluent: antialiased, subpixel text. Metro and Fluent 2: the antialiased, grayscale default |
| Window corners | `ChromeHints::corner_preference`, a hint to DWM | Square for every pack except Fluent 2, which keeps DWM's rounded default |
| Caption color | `ChromeHints`, a hint to DWM | Early Windows, 3.x, 9x and Luna hint their own title-bar blue, MDL2 its chrome gray. Aero, Metro and Fluent leave DWM's color alone |
| System backdrop | `ChromeHints::allows_system_backdrop` | Only Fluent 2 accepts Mica. Every other era predates it and draws an opaque page |

Because rasterization is a render command, it shows up in the semantic golden stream like any other
drawing. See [`Theme`](../../reference/themes/theme.md),
[`ChromeHints`](../../reference/themes/chrome-hints.md) and the
[themes reference](../../reference/themes/index.md) for the fields themselves.

## Date and Time Controls

[CalendarView](../../reference/controls/calendar-view.md),
[CalendarDatePicker](../../reference/controls/calendar-date-picker.md),
[DatePicker](../../reference/controls/date-picker.md) and
[TimePicker](../../reference/controls/time-picker.md) change construction with the era, not only
color. The calendar reads `ControlChrome::calendar_chrome`, DatePicker and TimePicker read
`ControlChrome::date_time_picker_chrome`, and CalendarDatePicker's field reads the combo box's
`ControlChrome::dropdown_chrome`. A pack picks a kind and the control draws it.

| Era | Calendar | DatePicker and TimePicker | CalendarDatePicker field |
| --- | --- | --- | --- |
| Fluent 2 | Round day cells, 300 x 350 | Looping columns in a flyout, an accent band behind the chosen row, Accept and Dismiss buttons | Field with a calendar glyph |
| Fluent, MDL2 | Square day cells, 296 x 332 | The same flyout with a plain band. MDL2 frames it and divides the columns with 2 px rules | Field with a calendar glyph |
| Metro | The Aero calendar in Windows 8.1's colors, dark from the pack's palette | Three drop-down lists, one per part | Field with a calendar glyph |
| Aero | The Aero calendar, 225 x 160 | The in-place field | Push-button face with the drop-down arrow |
| Luna | The classic month calendar in Luna's colors, 24 px columns | The in-place field | Field with a calendar glyph |
| Windows 9x | The Date/Time control panel's calendar, 164 x 162: a month list and a year spin box over a sunken box of the month's days | The in-place field | The combo box's field and button |
| Windows 3.x | The classic month calendar, 25 px columns, a framed today and a rectangle selection | The in-place field | The combo box's field and button |
| Early Windows | The classic month calendar, 32 px columns, a framed today and a rectangle selection | The in-place field | Field with a calendar glyph |

The classic month calendar has a colored title band, a red loop around today (Luna), a
"Today:" footer and a month menu on the title. Windows 9x draws the calendar the system itself
showed in its Date/Time control panel: one-letter weekday names on a `BTNSHADOW` band, only the
month's own days, the selected day in a `HIGHLIGHT` box just around its number, and no today mark.
Its month list and year spin box are their own tab stops, and there is no month or year view. The in-place field edits one part at a
time: Left and Right move between parts, Up and Down turn the part, and digits type it. In
DatePicker, F4 opens the era's calendar under the field. Every kind lays the parts out in the
order of the user's short date pattern, and TimePicker's hour follows the regional 12- or 24-hour
clock unless the control sets `clock`.

Approximations:

- Vista and 7 take Windows 8.1's calendar colors.
- The classic calendar has no year spin box. Ctrl+Page Up and Ctrl+Page Down move a year.
- Luna's, Metro's and Early Windows' CalendarDatePicker show the calendar glyph where the era had a
  drop-down button.
- Early Windows had no calendar or date control. Its pack builds the classic one from its own
  frame and colors.
- The flyout opens without an animation.
- High contrast in Fluent, MDL2 and Fluent 2 draws the round cells in the contrast colors.

The packs below are in the era table's order, newest first. Each is split the same way:
reproduced, approximated, cannot, and high contrast. libwinui never extracts or ships Microsoft's
bitmaps, fonts or `*.msstyles` art, which accounts for most of the "cannot" entries.

## Fluent 2

`ThemeVariant::Fluent2` is what the default `System` variant resolves to on Windows 11, and every
other era pack is measured against it.
The era itself is described on [Fluent 2](../../background/themes/fluent2.md).

`ThemeVariant::Fluent2` resolves through `resolve_fluent2()`, which takes the Light or Dark
[`Palette`](../../reference/themes/palette.md) table
([`fluent_light`](../../reference/themes/palette.md#fluent_light),
[`fluent_dark`](../../reference/themes/palette.md#fluent_dark)), writes the live accent rungs into
it, and derives every per-control color with
[`Theme::fluent()`](../../reference/themes/theme.md#fluent). The OS side comes from
`Win32ThemeSource` (private, `src/platform/windows/`) as an
[`OsThemeSnapshot`](../../reference/themes/os-theme-snapshot.md), and an app steers it with a
[`ThemeRequest`](../../reference/themes/theme-request.md).

### Reproduced

- **The token tables, literally.** Every Fluent 2 token a control reads is a
  [`Palette`](../../reference/themes/palette.md) field named after its resource key, with the Tier 1
  value for both dictionaries, including the black-alpha dark unchecked recess, the dark card
  stroke and the solid, card, layer and status families.
- **The accent, from the OS.** The Win32 theme source reads all seven values from
  `UISettings::GetColorValue`. Headless and CI paths get seven fallback constants as
  the [`AccentRamp`](../../reference/themes/accent-ramp.md) default, so goldens show what Windows
  11 shows at the default accent. The polarity flip is applied (Dark1 fill in Light, Light2 in
  Dark), the link text takes Dark2/Dark3/Dark1 and Light3/Light3/Light2, and accent hover and press
  are the same fill at 90 % and 80 %.
- **Geometry.** [`corner_radius`](../../reference/themes/theme.md#corner_radius) 4,
  [`overlay_corner_radius`](../../reference/themes/theme.md#overlay_corner_radius) 8, tooltips at
  4, [`padding`](../../reference/themes/theme.md#padding) `11,5,11,6`, 1 epx borders; indicator 20,
  label gap 8, switch track 40 x 20, knob 12/14/17 x 14 with 20 travel, radio dot 12/14/10, slider
  track 4 with a 22 epx outer thumb and a 12/14/10 inner disc.
- **The contour gradient.** Separate neutral and accent brushes with stops at 0.33 and 1.0 over
  3 epx absolute, the neutral one flipped in Light only and the accent one in both, dropped for a
  flat stroke on press and disable. The text field has its own 0.5/1.0, 2 epx bottom-anchored
  gradient, an opaque focused fill, and a bottom-only accent underline. The checked radio dot
  carries `CircleElevationBorderBrush`.
- **No shadow on controls.** [`ShadowRecipe::for_elevation`](../../reference/rendering/shadow-recipe.md#for_elevation)
  is `DropShadowRecipe.h` transcribed, branches and per-theme alphas included, and the
  [`elevation_z`](../../reference/rendering/namespace-ui-elevation_z.md) constants are the
  published ladder. Controls cast nothing; a `Border` with an `elevation` casts what the recipe
  says. Open popups cast the flyout (Z 32) shadow and tooltips the Z 16 one
  ([`PopupChrome::elevation`](../../reference/tree/popup-chrome.md)). The modal
  scrim is `#4D000000`.
- **Focus.** Two rings, 2 and 1 epx, whose outer edge lands 3 epx outside the control, concentric
  with its radius, in `FocusStrokeColorOuter`/`Inner`, drawn only after keyboard navigation moved
  focus ([`focus_ring_margin`](../../reference/themes/theme.md#focus_ring_margin),
  [`draw_focus_ring`](../../reference/rendering/namespace-ui.md#draw_focus_ring)).
- **State ladders.** Standard, accent and subtle fills per state; the unchecked indicator on the
  `ControlAltFill` ramp with the pressed stroke dropping to the disabled strong stroke, and the
  switch's off track as the one exception; foregrounds one step behind.
- **Motion.** [`anim_faster`](../../reference/themes/theme.md#anim_faster),
  [`anim_fast`](../../reference/themes/theme.md#anim_fast) and
  [`anim_normal`](../../reference/themes/theme.md#anim_normal) are 83, 167 and 250 ms, with
  [`ControlFastOutSlowIn`](../../reference/rendering/cubic-bezier.md) `0,0,0,1`. `Expander` opens
  in 333 ms and closes in 167 ms on `1,1,0,1`; implicit transitions run the runtime's `0.8,0,0.2,1`;
  connected animation reproduces the gravity dip (80, 32, 1.1 at 66 %) and the 200 ms direct
  configuration; menus slide in over 250 ms and fade out over 83 ms. The page transitions take
  150 ms out and 300 ms in, with a 140 epx offset where the guidance says 150 px.
- **Type sizes and weights.** Caption 12, Body 14, BodyStrong 14 SemiBold, Subtitle 20, Title 28,
  and the two larger roles at TitleLarge 40 and Display 68, all SemiBold from Subtitle up
  ([`type`](../../reference/themes/theme.md#type)).
- **System backdrops.** `Backdrop::Mica`, `MicaAlt` and `Acrylic` ask DWM for
  `DWMWA_SYSTEMBACKDROP_TYPE` and extend the frame; the page is then cleared transparent so the
  material shows. Only Fluent 2 accepts the request
  ([`ChromeHints::allows_system_backdrop`](../../reference/themes/chrome-hints.md)); every older
  era and high contrast decline it, and the decision is re-made on every theme change. DWM also
  gets the dark title bar that matches the theme, which is where Mica takes its tint from.
- **In-app acrylic as a brush.** [`AcrylicBrush`](../../reference/rendering/acrylic-brush.md)
  implements the luminosity recipe (blur standard deviation 30, luminosity then color blend,
  the tint-opacity caps) for shape fills, and
  `Palette::acrylic_in_app_default()` returns the Fluent 2 recipe for the current theme.
- **Dark and light.** `ThemeMode::System` follows the OS app mode live, and an element given an
  `ElementTheme` through `set_requested_theme()` draws its subtree in that mode through
  [`with_dark_mode`](../../reference/themes/theme.md#with_dark_mode).
- **High contrast.** The colors come verbatim from `GetSysColor` while
  `SPI_GETHIGHCONTRAST` reports a contrast theme
  ([`HighContrastPalette`](../../reference/themes/high-contrast-palette.md)). No gradients, no
  backdrop, every button outlined, the type ramp kept.
- **Details.** Segoe Fluent Icons, a flat single-stroke divider, the severity colors for
  `InfoBar`, `InfoBadge` and `ProgressBar`, and in-app acrylic on the light-dismiss surfaces.

### Approximated

- **Popup acrylic is always in-app.** While `OsThemeSnapshot::transparency_enabled` holds, the
  Fluent 2 resolve sets [`overlay_material`](../../reference/themes/theme.md#overlay_material) to
  `Palette::acrylic_in_app_default()`, and flyouts, menus, the `ComboBox` and suggestion lists and
  the other light-dismiss popups fill with it. Windows 11 draws menus and `CommandBarFlyout` with
  desktop acrylic, which needs a separate popup window that libwinui does not open, so they take
  the in-app material as a stand-in. With transparency off and under high contrast, popups fill
  with the brush's fallback color, `#F9F9F9` (Light) or `#2C2C2C` (Dark), which is also what
  USER32's own popup menu was measured painting on Windows 11. Tooltips always take the fallback
  color.
- **Slider colors.** The track and outer disc are derived from the page and text colors (the
  track is text at 50 %, the disc a lightened page with a 16 % text rim) instead of
  `ControlStrongFillColorDefault`, `ControlSolidFillColorDefault` and the elevation border. The
  track lands close; the disc is visibly darker than WinUI's white (Light) or `#454545` (Dark).
- **The font.** Text uses Segoe UI Variable in the optical cut made for the size (Small for
  captions, Text for body, Display from subtitles up), with the published line heights
  16/20/28/36/52/92, and falls back to Segoe UI where the Variable cuts are missing (Windows 10).
  The ramp has no `BodyLarge`.
- **The CheckBox and RadioButton focus ring** hugs the indicator instead of wrapping indicator and
  label with the `-7,-3,-7,-3` margin.
- **An app accent override** has no OS ramp to borrow, so its six rungs are blended toward white and
  black at 15, 30 and 45 %. That is the shape WinUI's own test hook suggests, not what the shell
  would produce for that color. The same blend is the fallback if `UISettings` itself fails.
- **`BackgroundSizing`** is not modeled. The default button's fill runs inside the border and the
  accent button's to its outer edge in WinUI; libwinui draws both the same way, a 1 px difference.
- **Motion asymmetry.** `Theme` holds three state durations and one curve, so entrance/exit pairs
  live as literals in the controls that need them (`Expander`, page transitions) rather than as
  theme data.
- **High contrast strokes.** The border slot comes from `COLOR_WINDOWFRAME` where WinUI strokes
  controls in `ButtonText`. The radii stay 4 and 8. With `ThemeMode::HighContrast` forced and no
  contrast theme on, the colors are the Aquatic scheme.
- **Text rasterization** is antialiased geometry with grayscale text
  ([`rasterization`](../../reference/themes/theme.md#rasterization)), not ClearType.
- **The collapsed scroll bar is 2 epx, not 8.** `ScrollBarOverlay` ports WinUI's `MouseIndicator`
  (the pointer model has no touch), with every shipped value except this one: the 8 epx collapsed
  thumb read noticeably heavier than real WinUI apps side by side, so libwinui draws 2 epx
  (`ThicknessCollapsed`). The track and step buttons also fade on the thumb's clock, where WinUI
  fades their opacity on its separate 83 ms timer. A step button moves an eighth of the viewport per
  click, with no auto-repeat while held and no disabled look at either end.
- **TreeView reordering** shows where a dragged node would land as an accent line. WinUI slides the
  rows apart after 200 ms (`ListViewBase`'s live reorder), which is not ported.

### Cannot

- **The Mica pixels themselves.** DWM composes Mica behind the window. libwinui draws a transparent
  page over it and cannot see, measure or golden-test the result, and the headless host has no
  wallpaper to sample. Tests see the opaque page.
- **Background acrylic for in-window popups.** A popup drawn inside the window has nothing of the
  desktop behind it to sample.
- **Material disablement for Mica and background acrylic.** That policy is inside the closed
  compositor and DWM, which apply it on their own; libwinui only makes the request.
- **The accent algorithm.** It exists only in the shell. libwinui reads its output and never
  computes a ramp for the system accent.
- **The navigation storyboards' exact curves.** They are OS-side; the published numbers are the
  ceiling.
- **Reveal.** Not part of the current system, and not implemented.

The Windows 10 predecessor, with 2/4 radii, a transparent button border and no lift gradient, is
[Fluent](../../background/themes/fluent.md). The theme types are listed under
[Themes](../../reference/themes/index.md).

### Notes

- **RadioButton rim.** A checked dot the pointer has touched carries the neutral
  `CircleElevationBorderBrush` rim (every `CommonStates` storyboard sets it back after `Checked`
  sets the accent one), and that is the one libwinui draws
  ([RadioButton](../../background/themes/fluent2.md#checkbox-radiobutton-togglebutton)).

## Fluent

Era reference: [Fluent](../../background/themes/fluent.md) (Windows 10, 2017 to 2021, the WinUI 2
`_v1` style set).

### Selecting the Pack

Select the pack with [`ThemeVariant::Fluent`](../../reference/themes/namespace-ui.md#themevariant):

```cpp
ui::WindowOptions options{.title = "Windows 10"};
options.theme.variant = ui::ThemeVariant::Fluent; // follows ThemeMode and the OS accent
ui::Window window{options};
return ui::run(window);
```

`resolve_fluent1()` in `src/ui/themes/ThemeSource.cpp` builds it: the color layer is a pair of
[`Palette`](../../reference/themes/palette.md) tables,
[`fluent1_light()`](../../reference/themes/palette.md#fluent1_light) and
[`fluent1_dark()`](../../reference/themes/palette.md#fluent1_dark), filled from the era's
`System*Color` ramp and `_v1` control keys, fed through the same
[`Theme::fluent()`](../../reference/themes/theme.md#fluent) derivation Fluent 2 uses. The resolver
then overrides everything where Windows 10's *shape* differs, not only its values.

### Reproduced

| Area | What the pack does |
| --- | --- |
| Dark and light | Both dictionaries, following [`ThemeMode`](../../reference/themes/namespace-ui.md#thememode) and the live OS setting |
| Accent | The live OS accent (or [`accent_override`](../../reference/themes/theme-request.md)): base at rest, `Light1` on hover, `Dark1` on press, for the accent button, the checked toggles and the Slider thumb; base for hyperlink text. The shades come from the [`AccentRamp`](../../reference/themes/accent-ramp.md) |
| Page | `#FFFFFF` / `#000000` |
| Corners | [`corner_radius`](../../reference/themes/theme.md#corner_radius) 2, `overlay_corner_radius` 4, square window corners via [`ChromeHints`](../../reference/themes/chrome-hints.md) |
| Button | [`padding`](../../reference/themes/theme.md#padding) `8,5,8,6`; the 20 / 10 / 40 % ladder with hover lighter than rest; transparent borders in all eight neutral and accent states; no elevation gradient |
| Toggles | Unfilled indicators with the 60 / 80 / 100 % stroke ladder, a 40 % pressed fill, a solid off knob, `ChromeWhite` marks, a 10 epx knob and an 8 epx radio dot with no hover or press resize |
| Text fields | 40 / 60 % alt washes, an opaque focused fill, a uniform 2 epx accent focus border, a hover outline at 80 % |
| Lists | Accent-filled selected rows at 0.75 at rest, 0.6 hovered and 0.9 pressed (`row_selection_fills`); the accent-washed open ComboBox |
| Navigation | The 2 × 24 indicator inset 4 epx |
| Slider | Accent thumb triple, 2 epx track, one 20 epx disc (`slider_fills`) |
| ScrollBar | Opaque `ChromeDisabledLow` thumb, `ChromeMedium` track at 0.9, full-strength arrows |
| Focus | The two-ring square [focus visual](../../reference/themes/theme-colors.md), base high outside and alt medium inside, never accent |
| Dialog | The inverse-of-page 60 % [modal scrim](../../reference/themes/theme-colors.md) |
| Transient hairline | Black in both themes, 14 % / 36 % |
| Type | The seven-style ramp with its Light and SemiLight weights, pinned to 1809 and later (Base and Body at 14) |
| Icons | [`icon_font_family`](../../reference/themes/theme.md#icon_font_family) set to `Segoe MDL2 Assets` by name |
| Motion | Fill changes snap ([`anim_faster`](../../reference/themes/theme.md#anim_faster) is 0) |
| Rasterization | Antialiased geometry with ClearType text ([`rasterization`](../../reference/themes/theme.md#rasterization)), what UWP drew on an opaque page |
| High contrast | The OS contrast colors with the era's shape: radii, padding, focus border, indicator sizes, icon font, type ramp and rasterization |

### Approximated

- **Acrylic** is drawn as its fallback color. Flyouts, menus, drop-downs and tooltips use
  `SystemChromeMediumLowColor` (`#F2F2F2` / `#2B2B2B`), the value every transient acrylic brush
  names as its `FallbackColor`, so they look the way Windows 10 looks with transparency effects
  off. The renderer does have an [`AcrylicBrush`](../../reference/rendering/acrylic-brush.md) an app
  can use itself; the theme does not.
- **The popup shadow** is the shared Fluent recipe ([`popup_shadow`](../../reference/themes/theme.md#popup_shadow),
  [`ShadowRecipe`](../../reference/rendering/shadow-recipe.md)). The era's 16 / 32 / 64 ladder and its
  pre-19H1 absence of shadows are not modeled separately.
- **The sub-era is fixed** at 1809 and later: 14 epx body text. A 1709/1803 reconstruction would
  use 15.
- **The corner choice** is "a WinUI 2 app", not "a bare UWP app". The OS controls were square; an
  app merging WinUI 2.2 or later got 2 / 4.
- **The `_v1` dictionary wins** where it and the SDK's UAP `themeresources.xaml` (10.0.20348)
  disagree, for the same reason. It shows in the text field: the OS dictionary makes a focused
  field `SystemChromeWhiteColor` with black text in both themes, while `TextBox_themeresources_v1.xaml`
  re-points both at alt high and base high, so a focused field is opaque white in Light and opaque
  black in Dark. The field border's 60 % and 80 % are also the `_v1` values, one rung bolder than
  the OS dictionary's 40 % and 60 %.
- **Check box and radio outlines** are stroked at the theme's 1 epx; the OS thickness for those
  two controls is 2.
- **InfoBar and InfoBadge colors** are inherited from Fluent 2 and labeled as such. Both controls
  postdate the era, and its ramp has no success or caution hue to derive one from.
- **Expander cards, Tabs strips and TableView bands** are derived from the era's `Chrome*` and
  list rungs for the same reason. A tab's hover and press, which Windows 11 draws from the
  `LayerOnMicaBaseAlt` ramp, take the list ladder's 10 % and 20 %. The tab strip was a WinUI 2.2
  control with no OS ancestor, and the era has no Mica.
- `ComboBoxItemBackgroundSelectedPressed` is the one rung not bound: the ComboBox popup tracks a
  single hot item rather than per-row hover and press state.

### Not Modeled

- **Reveal.** Every constant is recovered (see
  [Reveal](../../background/themes/fluent.md#reveal)), and it is still left out on purpose. It
  needs per-pointer state and a lighting model whose border highlight reaches neighboring
  controls, which is an engine feature rather than theme data. Substituting Windows 11's elevation
  border for it would be the one visual that most clearly dates a surface to the wrong era, so
  controls use the flat state fills instead.
- **Mica.** Declined through `ChromeHints::allows_system_backdrop`; it did not exist yet.
- **`PointerDownThemeAnimation`**, the press tilt. Its timings are OS-internal.
- **Transition motion**: pane slides, list reflow, the CommandBar and ProgressBar splines. `Theme`
  has no fields for them in any era.
- **`ElementSoundPlayer`**, the era's seven UI sounds. libwinui has no sound layer.
- **Input-mode and full-screen visual states.** Layout is identical across every theme by design.

## MDL2

The era page is [MDL2](../../background/themes/mdl2.md). libwinui's variant is
`ThemeVariant::Mdl2`.

`ThemeVariant::Mdl2` resolves through `resolve_mdl2()`, which picks
[`Mdl2Palette::mdl2_light()` or `mdl2_dark()`](../../reference/themes/mdl2-palette.md) and hands it,
with the accent, to `Theme::mdl2()` (`src/ui/themes/ThemeMdl2.cpp`). See
[`ThemeVariant`](../../reference/themes/namespace-ui.md#themevariant) and
[`Theme`](../../reference/themes/theme.md).

### Reproduced

- **The token system itself.** `Mdl2Palette` is the 24-key ramp, both columns, transcribed from the
  1507 SDK, plus the page background. Every control color in `Theme::mdl2()` is derived from it and
  the accent, with the `SystemControl*` key named at each spot. The alpha rungs stay alpha.
- **Dark and light, and the live accent.** The variant follows
  [`ThemeMode`](../../reference/themes/namespace-ui.md#thememode) and uses the OS accent, or
  [`ThemeRequest::accent_override`](../../reference/themes/theme-request.md) when one is set. The
  era's `#0078D7` is not pinned: the era's own policy is that the accent is the user's.
- **Geometry.** `corner_radius` and `overlay_corner_radius` 0, `border_width` 2, `padding`
  `8,4,8,4`, `page.grid_unit` 4. Square window corners through
  [`ChromeHints`](../../reference/themes/chrome-hints.md), the caption in the theme's own
  [`chrome_mid`](../../reference/themes/mdl2-palette.md#chrome_mid) gray, and no Mica (`allows_system_backdrop` is false).
- **The 15 px ramp**, Segoe UI, with the control label at 15 and centered. Icons use
  `Segoe MDL2 Assets`, named outright because `SymbolThemeFontFamily` resolves to the Windows 11
  redraw today.
- **The two-ring focus visual**, 2 + 1, in [`text_100`](../../reference/themes/mdl2-palette.md#text_100) outside and
  [`page_60`](../../reference/themes/mdl2-palette.md#page_60) inside.
- **Instant state changes.** `anim_faster`, `anim_fast` and `anim_normal` are all zero, the
  era's measured behavior. `anim_fast_out_slow_in` is the era's `0.1,0.9 0.2,1.0` for
  any motion added later.
- **Every control shape on the era page**: the gray-fill button with its hover border, the 1703
  accent button ladder, the subtle (app-bar) button on the two `List*` rungs, the stroked check box,
  the hollow indeterminate box with the MDL2 square glyph, the outline radio with a fixed 10 px dot,
  the 44 x 20 pill switch with a 10 px knob traveling 24, and the 2 px slider track under an 8 x 24
  rounded-rectangle thumb (`SliderThumbShape::RoundedRect`) with the era's per-state
  `slider_fills`, no hover rung and a gray press.
- **Selection by fill.** `row_selection_fills`
  carries the 0.4 / 0.6 / 0.7 (Light) and 0.6 / 0.8 / 0.9 (Dark) accent ladder, and setting it
  suppresses the Fluent pill in ListView, TreeView, TableView and ComboBox.
  `accent_selection_indicator` is off,
  so NavigationShell and SelectorBar draw no pill either. A selected NavigationShell row is the filled
  list row, the pane the era actually built. `SelectorBar` moves the cue into the label, the way
  the `Sections` headers do: the unselected label is the text color at 60 % alpha absolute, which is
  exactly the [`text_60`](../../reference/themes/mdl2-palette.md#text_60) rung's `0x99`.
- **Surfaces.** Menus, tooltips, separators, hyperlinks, scroll bars and the progress track as in
  the era page's [control tables](../../background/themes/mdl2.md#controls). Disabled text is the
  20 % rung, which is low contrast on purpose.
- **Rasterization:** antialiased geometry with subpixel (ClearType) text, which is what UWP drew.
- **Popups.** No popup casts a shadow ([`popup_shadow`](../../reference/themes/theme.md#popup_shadow)
  `None`; `ThemeShadow` needs 19H1). A `DialogControl` dims the window with the [`page_60`](../../reference/themes/mdl2-palette.md#page_60) rung,
  60 % of the inverse of the page ([`colors.scrim`](../../reference/themes/theme-colors.md)),
  the brush UWP's dialog smoke layer reads from 1511 on.
- **High contrast.** Under a contrast theme the variant keeps its MDL2 shape and recolors from the
  OS's eight values, the era's own mechanism
  ([era high contrast](../../background/themes/mdl2.md#high-contrast)). Forcing
  [`ThemeMode::HighContrast`](../../reference/themes/namespace-ui.md#thememode) without an OS
  contrast theme falls back to Windows 10's High Contrast Black
  ([`HighContrastPalette`](../../reference/themes/high-contrast-palette.md)).

### Approximated

- **The focus visual before 1607** is drawn with the 1607 to 1703 geometry, since nothing records
  what 1507 and 1511 drew, and its brush pairing comes from Fluent.
- **Severity colors.** The era has none. `InfoBar` and friends keep readable fallback hues, and
  only their surface is fixed: the bar is the page color, framed like a field, with white text on
  the fills.
- **The accent button before 1703.** libwinui draws the 1703 ladder for the whole era, since it is
  the only one with a source.
- **Menu motion.** A menu keeps the default slide-in over 250 ms and 83 ms fade-out
  ([`popup_motion`](../../reference/themes/theme.md#popup_motion)). The era ran the runtime's
  `MenuPopupThemeTransition`, whose timings its XAML never states.
- **The dialog scrim in 1507.** The 10240 SDK has no `SystemControlPageBackgroundMediumAltMediumBrush`,
  so a 1507 dialog did not dim the window. The pack draws the 1511-and-later smoke.

### Cannot

- **The named theme animations.** Their timings live in the OS and are not readable from XAML.
- **Transition motion.** `Theme` holds control-state durations and one curve. It has no field for
  the ListViewItem, AppBar, ScrollViewer or ProgressBar storyboards, and ProgressRing's spin needs a
  keyframe track, not a duration.
- **Layout and behavior.** The 4-epx grid, 44-epx targets, breakpoints, the dialog button order,
  the Xbox-only dimming and `ElementSoundPlayer` are behavior or content, which stays identical
  across every theme.

## Metro (MDL)

The era page is [Metro (MDL)](../../background/themes/mdl.md). libwinui's variant is
`ThemeVariant::Metro`. The `Metro` pack draws the Windows 8.1 variant of Metro, because that is the one
with a control framework whose theme files shipped in an SDK.

`ThemeVariant::Metro` resolves through `resolve_mdl()` into
[`Theme::metro()`](../../reference/themes/theme.md), fed by a
[`MetroPalette`](../../reference/themes/metro-palette.md) that transcribes the 8.1 dictionaries:
`MetroPalette::metro_dark()` for `Default` and `MetroPalette::metro_light()` for `Light`. Windows 8.0
and 8.1 are one variant. 8.0 differs by two brushes (the app bar) and 139 missing keys, and its
real change was mechanical (`{StaticResource}` became `{ThemeResource}`, which is what made
per-element theming possible in 8.1), so it is a delta rather than a variant of its own.

### Reproduced

- **A genuine dark/light pair, with dark as the default look.** `Metro` is one of the four variants
  that honor [`ThemeMode`](../../reference/themes/namespace-ui.md#thememode), and
  `ThemeMode::System` follows the OS like the others.
- **Two constructions, not a recolor.** `MetroPalette::button_border_width` (2 dark, 0 light) and
  `indicator_border_width` (0 dark, 2 light) carry the two thickness tokens, so a dark button is an
  outline on nothing and a light one a borderless gray fill, and the check box does the reverse.
- **The press inversion.** The pressed `ButtonVisual` swaps foreground and background, and
  `ControlChrome::press_state_flip` keeps the composition overlay from laying its usual press wash on top of
  it. Hover is the `#21FFFFFF` / `#D1CDCDCD` wash.
- **Square everywhere, as every radius token.** `corner_radius`, `overlay_corner_radius`,
  `toggle_switch_track_radius_ratio` and the slider thumb radius are all 0, so popups, the switch and
  the slider do not fall back to Windows 11 shapes.
- **The era's control geometry**: 21 px check box and 23 px radio ring
  (`radio_box_size`) with a 6 px gap to the label, 13 px radio dot,
  a 50 × 19 switch track with a 12 px knob traveling 38 px, an 11 × 11 square slider thumb on an
  11 px track, the 9 × 9 indeterminate square, and `12,4,12,4` button padding with a centered label.
- **The dashed focus pair.** `FocusVisual::MetroDashedPair` draws two interleaved 1 px dashed
  rectangles in white and black at a 3 px margin (see
  [`FocusRingStyle`](../../reference/rendering/focus-ring-style.md)). The colors stay black and white
  under high contrast, as the era's do.
- **The type ramp with its line boxes.** Header 56 Light (40), Subheader 26.667 Light (30), Title
  14.667 SemiBold, Subtitle 14.667 Regular, Body 14.667 SemiLight, Caption 12 Regular, all on a
  20 px line box through `TextStyle::line_height`.
- **Instant state changes.** `anim_faster` and `anim_fast` are 0, and the easing curve is linear
  rather than a borrowed Fluent curve. The era has no curve to borrow, and linear is what "no
  easing" should degrade to.
- **The theme-invariant surfaces.** Text fields are white in both themes; menus and flyouts are white
  with a black 2 px border and a `#FFE5E5E5` hover even in a dark app; the scroll bar is the same
  light gray in both themes; separators and dividers are `#FF7A7A7A`. A tooltip is white with
  `#FF666666` text in a 2 px `#FF808080` frame, padded `10,6,10,7` inside it
  (`tooltip_padding`).
- **The fixed violet.** Selection, the switch curtain, the accent button, progress and hyperlinks use
  the era literals. The ProgressBar track and fill follow the per-theme brushes of the era page.
- **One color for every severity.** The Windows 8 guide asks error and warning bars to use *"a color
  from the app's palette"*, one for all bars, so every InfoBar severity is the accent.
- **High contrast in the era's own shape.** `Metro` under high contrast still goes through
  `Theme::metro()`, with each slot mapped to the system color the 8.1 `HighContrast` dictionary
  names, both border thicknesses at 2, and the check box reacting to hover and press. See
  [`HighContrastPalette`](../../reference/themes/high-contrast-palette.md) and the era page's
  [high-contrast section](../../background/themes/mdl.md#high-contrast).
- **Square desktop windows and an opaque page.** The pack's
  [`ChromeHints`](../../reference/themes/chrome-hints.md) ask DWM not to round the window and decline
  a system backdrop.
- **No selection pill.** `accent_selection_indicator` is off. The era had no NavigationShell, and its
  selection cue is the filled row, so NavigationShell and SelectorBar mark selection with the fill and
  the label instead. The selected NavigationShell row takes `colors.selection`, 8.1's opaque accent
  row (`ListViewItemSelectedBackgroundThemeBrush` `#FF4617B4`), where MDL2 uses a 40 % / 60 % wash:
  same shape, different rung. An unselected SelectorBar label is the text color at exactly `0x99`
  alpha, `ApplicationSecondaryForegroundThemeBrush`'s value, not 60 % of the `#DEFFFFFF` dark text
  (which would land near `0x85`).
- **The Light-forced flyout.** A dark Metro app's `Flyout` is white, and the pack re-resolves its
  content the way the era page's
  [flyouts section](../../background/themes/mdl.md#flyouts-menus-and-tooltips) asks: `Flyout` hands
  caller content a theme whose `colors.text` and `colors.background` are the popup's
  `surfaces.menu_text` and `surfaces.menu`, and strokes the card with `surfaces.menu_border`, since
  the field border is white there. In every Fluent-era theme the popup and page surfaces agree, so
  the swap changes nothing there.

### Approximated

- **The accent override.** Windows 8 had no accent, so the OS accent deliberately reaches nothing
  under `Metro`. Only an explicit
  [`ThemeRequest::accent_override`](../../reference/themes/theme-request.md) changes it, as a
  non-era personalization knob. The era's hover and pressed shades are unrelated literals rather than
  a ramp, so for an override they are derived (15 % lighter, 10 % darker) and are not era data.
- **The dark ProgressBar fill** uses the pack's hover shade `#FF5F37BE` rather than the era's
  `#FF5B2EC5` (within 9/255 per channel), so the bar follows an accent override like everything else.
- **List selection.** Selected rows get the accent fill with white text. The 4 px accent border and
  the 40 × 40 check earmark are not drawn.
- **Rasterization** stays on the library's antialiased, grayscale-text default. Nothing in the era
  contradicts that, and nothing states it either.
- **Stylistic Set 20.** `TextStyle` has no OpenType feature switch, so Segoe UI renders with its
  default glyph set rather than `ss20`.
- **Spelling squiggles in dark.** The default light red squiggle is too dark to read on the
  `#FF1D1D1D` page, so dark Metro borrows Fluent dark's `SystemFillColorCritical` `#FFFF99A4` for
  `Theme::spelling_error`.
- **TableView** reads `surfaces.menu` for its header band. WinRT XAML 8.1 had no grid control, so the
  white popup value there has no era value to contradict.

### Not Modeled, or Not Theme Data

- **Icons stay on the Segoe Fluent / MDL2 codepoints.** The era's commands are Segoe UI Symbol glyphs
  in the PUA range `U+E100` to `U+E1E9`, a different set of codepoints from the one
  `render/IconGlyph.hpp` uses. Naming the era font without a per-glyph mapping would render missing-glyph
  boxes for every icon, and no era build of Segoe UI Symbol is available to check the shapes against.
  `icon_font_family` is therefore left at the default.
- **The page silhouette is data, not layout.** `Theme::page` records the 20 px grid unit, the
  `120,140,120,50` content margin, the 140 px header row, the 10 px item spacing and the 68 px command
  bar, but no control lays itself out from it. Layout is identical across every theme by design, and
  the grid is the most Metro thing an application can do for itself.
- **Theme animation timings.** They lived in the OS animation library and are not in any era source,
  so nothing is borrowed. ProgressBar and ProgressRing run the library's own animation, not the
  era's dot choreography.
- **Shell surfaces**: tiles, badges, toasts, charms, the Start screen, `MessageDialog` and
  `PopupMenu`. They are system-drawn, not controls.
- **Windows Phone.** The phone's Segoe WP ramp, twenty accents, pivots, panoramas, turnstile and tilt
  are the same design language on a different framework, and `Metro` does not draw them.
- **Windows 8.0's translucent dark app bar** (`#E5000000`). The pack takes the 8.1 value.

## Aero

The era page is [Aero](../../background/themes/aero.md). libwinui's variant is
`ThemeVariant::Aero`. It merges Windows Vista and Windows 7 into one pack and defaults to
Windows 7's values.

`ThemeVariant::Aero` resolves to a fixed, light-only theme (see
[`ThemeVariant`](../../reference/themes/namespace-ui.md#themevariant)). It ignores
`ThemeMode`'s light and dark and `ThemeRequest::accent_override`, because the era has neither a
dark palette nor a control accent. The values come from `AeroStyle.xml` for system and text
colors and from WPF Aero for every drawn surface, transcribed as numbers. The UX Guide's color
meanings (see [Color](../../background/themes/aero.md#color)) are what the pack uses for its
severity colors.

### Reproduced

| Area | What the pack draws | Reference |
| --- | --- | --- |
| Button | Opaque gray four-stop face with the hard 50 % split, `#707070` border, white `#FAFFFFFF` → `#85FFFFFF` inner ring at radii 2.75 / 1.75 | [`ButtonVisual`](../../reference/themes/button-visual.md), [`Button`](../../reference/controls/button.md) |
| Button states | Hover and pressed blues with their borders; pressed drops the ring and adds the `#80333333` inner shadow; the label does not move; disabled `#F4F4F4` / `#ADB2B5` / `#ADADAD` | [`ButtonVisual::inner_shadow`](../../reference/themes/button-visual.md#inner_shadow) |
| Default button | `ButtonVariant::Accent` draws the defaulted look: the normal face with a solid `#F900CCFF` ring. Aero has no filled accent button, so this is the closest the era offers | |
| Toolbar-style buttons | Flat until hovered, then the full chrome | |
| Check box | Square 13 x 13 light well with its diagonal rim and inner fill, `#31347C` mark, `#AEB7CF` disabled mark, and the layered blue mixed square per state | [`CheckBox`](../../reference/controls/check-box.md), [`InsetWell`](../../reference/themes/inset-well.md) |
| Radio button | The same well made round, a radial blue bead in a `#193B55` ring, per-state bead gradients. The radial fill covers the whole bead, the ring sits 1 px inside its edge, and the gradient's (0.25, 0.25) center is a fraction of the disc inside that ring, the ellipse WPF fills | [`RadioButton`](../../reference/controls/radio-button.md) |
| Text fields | The three-stop border gradient over 20 px for rest, hover and focus; 1 px radius; `#F4F4F4` disabled fill; focus shows as the blue border, not a dotted rectangle | [`TextBox`](../../reference/controls/text-box.md) |
| Combo box | A drop-down list drawn as a push button; the 7 x 4 black arrow in a 17 px column; a white, square list framed in `#646464` with a `#3399FF` highlight | [`ComboBox`](../../reference/controls/combo-box.md) |
| Slider | WPF's 11 x 22 glossy block over a 4 px `#E7EAEA` groove, no value fill and `#C4C4C4` ticks | [`Slider`](../../reference/controls/slider.md) |
| Selection and links | `#3399FF` with white text; links `#0066CC`; `GrayText` `#6D6D6D` | |
| Surfaces | `#F0F0F0` page, `#FFFFFF` fields, `#F4F4F4` scroll track, `#828790` separators and dividers | |
| Navigation pane | Its own gradient `#EDF2F9` → `#DEE8F6` → `#EDF5FF`, navy `#151C55` links, hot `#074AE5`; no sliding selection indicator. The selected NavigationShell row is the flat `#3399FF` highlight at the pack's 2.75 radius, the Explorer row look; the era has no control accent to draw an overlay in. A `SelectorBar` marks selection in its label instead of a pill, unselected labels at 60 % black (`#666666`, within a rung of `GrayText` `#6D6D6D`) | `ControlChrome::nav_pane`, [`NavigationShell`](../../reference/controls/navigation-shell.md) |
| Type | Segoe UI 9 pt = 12 epx; the ramp mapped onto the era's five text styles, with the header at 16 px and header and subheader in `#003399` | [`Theme::type`](../../reference/themes/theme.md#type) |
| Rasterization | Antialiased geometry, ClearType text | [`Theme::rasterization`](../../reference/themes/theme.md#rasterization) |
| Popup shadow | The `CS_DROPSHADOW` block: `#71000000`, displaced +5 / +5, the WPF fall-off, inside the displaced rect | [`Theme::popup_shadow`](../../reference/themes/theme.md#popup_shadow) |
| Popup motion | Menus fade or slide and drop-down lists slide according to the user's own `SPI_GETMENUANIMATION`, `SPI_GETMENUFADE` and `SPI_GETCOMBOBOXANIMATION` switches, over 200 ms, and close at once. A sliding drop-down list casts no shadow until the slide has finished, since `AnimateWindow` only shows `CS_DROPSHADOW` then | [`Theme::popup_motion`](../../reference/themes/theme.md#popup_motion) |
| Progress and severity | `#52D763` fill on WPF's `#BABABA` track; severity signs green `#52D763`, yellow `#DCD400`, red `#E40021` and link blue `#0066CC`, on white field surfaces | [`ProgressBar`](../../reference/controls/progress-bar.md), [`InfoBar`](../../reference/controls/info-bar.md) |
| Window chrome | Square window corners; the caption color is left to DWM; the page is opaque and the pack declines `Backdrop::Mica` | [`ChromeHints`](../../reference/themes/chrome-hints.md) |
| High contrast | The classic construction in the contrast scheme, with Segoe UI 9 pt, as Vista and 7 did | [High contrast](../../background/themes/aero.md#high-contrast) |

### Approximated

- **One animation slot for an asymmetric triple.** WPF's hover in 300 ms, hover out 200 ms and
  press 100 ms have to share `Theme::anim_faster`, which takes the 300 ms hover-in, the one a user
  watches most. The text field's hover fade is not animated at all; it switches.
- **The hot menu item** is a translucent fill plus an `#8071CBF1` outline, and the pack has one
  flat slot for it. The fill alone composites to a 2 % step over the popup, so the slot carries
  the outline composited over the popup, `#B3E0F3`. The row reads slightly more saturated than
  the era's rather than invisible.
- **The tooltip** is one flat fill, the darker stop `#E4E5F0`, with `#575757` text. Its
  `#767676` border has no slot of its own, so the frame falls back to the field border
  `#ABADB3`, and its radius and padding follow the pack's general values rather than WPF's 2 and
  5,1,5,4.
- **The scroll thumb** is one flat color, `#DFDFE1`, the pair meeting at its 50 % split, rather
  than the four-stop gloss.
- **Diagonal gradients.** WPF runs the mixed square's inner border and highlight diagonally; the
  pack strokes both top to bottom, keeping the stops.
- **The progress track** takes WPF's trough brush because the pack's channel is a filled bar with
  no frame, where Win32's is a white well inside a `#686868` border.
- **The Vista column** is not a separate variant. The whole Vista to Windows 7 delta that reaches a
  control is `GrayText` and the navigation-pane colors (plus `CaptionBarHeight`, which is frame
  data), about 16 lines of data, so the pack uses Windows 7's values.
- **The toggle switch** is an invention. Vista and 7 had no switch and the era's own advice was a
  check box. The pack builds one from two era surfaces, a text field as the track and a push
  button as the knob, using the field's own border gradients. The 56 x 22 track, the 32 x 16 knob
  and the `#C1DFF6` on fill come from a third-party Aero theme and have nothing in the era to
  check them against, and so does the knob's slide, 250 ms on a quintic ease-out. That duration is a
  literal in the control rather than theme data. See
  [`ToggleSwitch`](../../reference/controls/toggle-switch.md).

### Cannot

- **The default button's pulse.** A defaulted Aero button breathes the hover overlay over itself
  on a 2 s loop. `Theme` has no looping per-state animation slot, so the cyan ring is drawn and
  the breathing is not.
- **The Explorer selection look.** Its gradients are bitmaps that are not in any text source, and
  the pack keeps the stock control's flat `#3399FF` row.
- **Glass.** Not drawn, on purpose. See below.
- **The artwork.** `libwinui` never extracts or ships `*.msstyles` content. Aero is the lucky
  era here, since WPF's numbers stand in for almost every drawn surface and agree with measured
  Win32 pixels to within a few levels.

### Why There Is No Glass

Two separate walls stand in the way, and the second is the one usually missed (see
[Materials: glass](../../background/themes/aero.md#materials-glass)).

1. **The API is gone.** *"Beginning with Windows 8, calling this function doesn't result in the
   blur effect"*: client-area blur-behind is inert on every OS `libwinui` runs on. The
   compositor's constants were never published either, so any glass would be a construction.
2. **The era forbade glass where a pack would put it.** The UX Guide allows glass *"in small
   regions touching the window frame without text"* and says *"Don't use glass in situations where
   a plain window background would be more attractive or easier to use"*. Every era example, IE7's
   address band or Media Player's control strip, is a frame extension holding chrome, never a page
   behind content.

Since client-area controls looked the same in the Glass and Basic tiers, an opaque page is what
Aero actually drew in the client area, not a low-fidelity stand-in for it. For the same reason the
pack refuses a system backdrop: `ChromeHints::allows_system_backdrop` is `false`, so a window that
requests `Backdrop::Mica` gets an opaque `#F0F0F0` page under `Aero`. Mica and Acrylic are
saturated and noise-textured, a different material from a different decade, and a page left
unpainted would take its bare bands' color from the user's wallpaper. The non-client frame
itself belongs to DWM, which today draws the Windows 11 frame; the pack only asks it for square
corners.

### High Contrast

On Vista and 7, turning on a High Contrast theme turned visual styles off (see
[High contrast](../../background/themes/aero.md#high-contrast)). The pack does the same. Under high
contrast `Aero` resolves to the classic beveled construction in the contrast colors, with
Segoe UI 9 pt, the font Classic kept from the OS underneath, and no accent selection indicator.
Popup motion still follows the user's menu and combo box animation switches, since those are
system settings the contrast theme leaves alone, and popups keep the `CS_DROPSHADOW` shadow, which
Classic still drew.
While the OS is in a contrast theme the colors are the user's own; when an app forces
`ThemeMode::HighContrast` without one, the pack uses High Contrast Black as Windows 2000 and XP
ship it, since no Vista or 7 scheme values are on disk and both still drew contrast themes under
the scheme names XP had. See [`ClassicPalette`](../../reference/themes/classic-palette.md) and
the [era overview](../../background/themes/index.md).

## Luna

The era page is [Luna](../../background/themes/luna.md). libwinui's variant is
`ThemeVariant::Luna`.

`ThemeVariant::Luna` is resolved by its own function rather than through the classic derivation.
It is light-only and ignores `ThemeMode` light/dark and `ThemeRequest::accent_override`, because
Luna has no dark scheme and no accent (see
[`ThemeVariant`](../../reference/themes/namespace-ui.md#themevariant) and
[`resolve_theme`](../../reference/themes/namespace-ui.md#resolve_theme)). The Blue scheme is the
one modeled.

Luna is a renderer, not a control library (see
[A skin over Windows 2000's behavior](../../background/themes/luna.md#a-skin-over-windows-2000s-behavior)),
which is also a good description of what a theme pack in libwinui is.

### Reproduced

| Area | What the pack draws | Tokens |
|---|---|---|
| page and text | `#ECE9D8` page, black text, white fields | [`surfaces`](../../reference/themes/theme.md#surfaces) |
| field edge | `#7F9DB9`, not the button's blue | `surfaces.field_border` |
| selection and menus | `#316AC5` with white text; white menus with a `#808080` frame | `colors.selection`, `surfaces.menu*` |
| disabled text | themed `#A1A192` | `colors.text_disabled` |
| push button | the near-white face ramp inside a 1 px `#003C74` border at radius 3; hot adds a 2 px amber ring and leaves the face; pressed reverses the light and drops the ring; disabled flat `#F5F4EA`/`#C9C7BA`/`#A1A192` | [`button_normal`](../../reference/themes/theme.md#button_normal) and siblings, [`ButtonVisual::inner_border`](../../reference/themes/button-visual.md#inner_border) |
| accent button | `ButtonVariant::Accent` maps onto `PBS_DEFAULTED`: the blue glow ring, hot winning over defaulted | [`accent_button_normal`](../../reference/themes/theme.md#accent_button_normal) |
| toolbar-style button | paints nothing at rest; warm near-white hot and warm gray pressed | [`subtle_button_normal`](../../reference/themes/theme.md#subtle_button_normal) |
| check box and radio | square 13 px box, flat `#1C5180` border, 45° `#DCDCD7` → white well, `#21A121` tick, `#38B935` radio dot, amber hot ring over a flattened well, darker pressed well | `ToggleVisuals`, `toggle_box_radius` |
| mixed check box | the 7 × 7 green square in its four measured fills | `check_box_indeterminate` |
| slider | 11 × 21 beveled thumb whose bands carry the state (green, amber, dark green, gray), a sunken `#F2F1E9` groove, no value fill, `#A1A192` tick marks | `slider_chrome`, `slider_value_fill` |
| progress bar | `#2ED331` on a white track | `progress_fill` |
| scroll bar | `#F9F8F5` track, `#C6D5FD` thumb, `#4D6185` arrows | `surfaces.scrollbar_*` |
| separators | a single 1 px `#CAC6AF` rule; the status bar's `#C7C5B2`/white pair kept for the etched case | [`divider_visual`](../../reference/themes/theme.md#divider_visual) |
| tooltip | classic: `#FFFFE1`, black frame, square, 2/2/4/2 inset | `tooltip_corner_radius`, `tooltip_padding` |
| severity | the four-step scale on white bars, because Luna tints signs, not surfaces: blue `#316AC5`, green `#21A121`, amber `#DFA550` (the Log Off disc), red `#E45D3D` (the Close button's hint) | [`severity`](../../reference/themes/theme.md#severity) |
| type | Tahoma 8 pt = 11 px, a flat ramp since Luna assigns no roles | [`text_style`](../../reference/themes/theme.md#text_style), [`type`](../../reference/themes/theme.md#type) |
| rasterization | antialiased geometry, grayscale text, no pixel snapping | [`rasterization`](../../reference/themes/theme.md#rasterization) |
| focus | the dotted rectangle in black; no change on a focused field | [`focus_visual`](../../reference/themes/theme.md#focus_visual) |
| popups | square corners, no selection pill or sliding indicator | [`overlay_corner_radius`](../../reference/themes/theme.md#overlay_corner_radius), `accent_selection_indicator` |
| popup shadow | the shared `CS_DROPSHADOW`-style shadow: `#71000000`, +5/+5, 5 px fall-off inside the displaced rect | [`popup_shadow`](../../reference/themes/theme.md#popup_shadow) |
| motion | none; state changes are instant | [`anim_faster`](../../reference/themes/theme.md#anim_faster) is zero |
| popup motion | menus fade or slide and drop-down lists slide as the user's `SPI_GETMENUANIMATION`, `SPI_GETMENUFADE` and `SPI_GETCOMBOBOXANIMATION` switches say, over `AnimateWindow`'s "typical" 200 ms, and close at once; under high contrast too, since the contrast theme leaves those switches alone | [`popup_motion`](../../reference/themes/theme.md#popup_motion) |
| window chrome | square DWM corners, `#0054E3` caption and border, white caption text, no system backdrop | [`ChromeHints`](../../reference/themes/chrome-hints.md) |

### High Contrast

Under high contrast the pack does what XP did (see
[High contrast](../../background/themes/luna.md#high-contrast)). So `Luna` under
`ThemeMode::HighContrast` becomes the classic beveled construction in the contrast colors,
keeping Tahoma 8 pt and the `CS_DROPSHADOW` popup shadow. The colors are the OS's own contrast theme while one is on, and High
Contrast Black as Windows 2000 and XP ship it when an app forces the mode without one.

### Approximated

- **The nine-grid.** A real Luna button is 9 fixed rows, a stretched 5-row center and 9 fixed
  rows. One `LinearGradientBrush` over the full height matches Luna at the era's own 23 px and
  drifts at any other height, most visibly where the fixed bands hold the top highlight and the
  bottom turn-down. The renderer has no nine-grid fill for computed gradients.
- **The check mark** comes from the icon font, tinted `#21A121`, where Luna's is a hand-tuned
  2 px polyline.
- **Fields are drawn at the button's 3 px radius.** The pack sets no separate field radius, so
  "square fields, rounded chrome" holds for the check box (which has its own
  `toggle_box_radius`) but not for text
  boxes.
- **The radio dot** is one flat `#38B935` standing in for a 5 px lit sphere.
- **Button padding.** Luna sizes a button from its dialog template, not its label. The pack uses
  4 px vertically and 14 px horizontally so an "OK" button lands near the era's width.
- **The progress indicator** is one continuous bar. The 6 px chunks on an 8 px pitch are not
  drawn, because `ProgressBar` draws one rectangle.
- **Window chrome** is a hint to DWM, not Luna's frame: one flat caption color for a curved
  gradient, and square corners because DWM's corner attribute is all four corners or none, while
  Luna rounds only the top two.
- **ToggleSwitch is an invention.** XP has no switch; its two-state idiom is a latched button. The
  pack assembles one from era values: the recessed `#DCDCD7` well at a 3 px radius instead of a
  capsule, with the knob in the check-box blue when off and the tick green when on.
- **The Blue scheme only.** Olive Green and Silver are measured, not modeled.

### Cannot, on Purpose

libwinui never extracts or ships Microsoft's `*.msstyles` assets. `luna.msstyles` carries its own
notice, "Copyright © Microsoft Corporation, 2000", and no redistribution license exists for it,
so embedding its bitmaps would ship unlicensed art under an MIT header. Size was never the
argument: all 558 bitmaps come to about 440 KB as optimized PNG. License is.

The line runs inside "procedural", not around it:

> Parametric descriptions and small measured tables of continuous tone: yes.
> Per-pixel reproductions of authored shapes: no.

That is why the pack draws gradients from measured stops and draws its glyphs from the icon font
instead of copying Luna's (see
[Reproducing it today](../../background/themes/luna.md#reproducing-it-today)).

For byte-exact skinned chrome the right route is the one Wine and ReactOS took: read a style the
user already owns rather than shipping one. libwinui provides the plumbing for bitmap-backed
themes in the [`DrawImageNineSlice`](../../reference/rendering/render-commands.md#struct-drawimagenineslice)
render command, which a backend resolves through an image pack, and ships no images for it.
Any art used with it has to be originally authored or freely licensed.

The window frame is out of scope for libwinui's client-area rendering; its numbers on the era page
explain the chrome hints above.

## Windows 9x

The era page is [Windows 9x](../../background/themes/win9x.md). libwinui's variant is
`ThemeVariant::Windows9x`.

`resolve_windows9x()` builds this pack from one [`ClassicPalette`](../../reference/themes/classic-palette.md)
literal through the shared [`Theme::classic()`](../../reference/themes/theme.md#classic)
derivation. The default is `ClassicPalette::windows_standard()`, the **Windows 95/98 row**: the one
the variant is named after, the one that covers two of the three versions, and the one MS Sans
Serif belongs to. [`ThemeRequest::classic_scheme`](../../reference/themes/theme-request.md) swaps in
any other palette, including `ClassicPalette::windows_standard_2000()` (the other shipped row),
the high-contrast schemes, or `ClassicPalette::from_face()` for a scheme of your own.

```cpp
ui::WindowOptions options{.title = "Retro"};
options.theme.variant        = ui::ThemeVariant::Windows9x;
options.theme.classic_scheme = ui::ClassicPalette::windows_standard_2000(); // the Windows 2000 row
```

The pack is light-only and ignores `ThemeMode` light/dark and the accent override. See the
[theme eras overview](../../background/themes/index.md) for how every pack shares one set of controls.

The [DLU system](../../background/themes/win9x.md#dialog-units), the OK-before-Cancel rule and the
dialog ceiling are layout and behavior, not theme data, and libwinui keeps those identical across
every theme. They matter for an example app written in period style, which is what makes a
screenshot read as 1997 rather than a gray repaint.

### Reproduced

| Area | What the pack draws |
|---|---|
| Palette | the 95/98 row verbatim: `#C0C0C0` face, `#000080` selection and caption, `#FFFFFF` fields, `#808080` gray text, `#FFFFE1` tooltips, `#0000FF` links |
| Bevel ramp | the shipped five-stop ramp `#FFFFFF`/`#DFDFDF`/`#C0C0C0`/`#808080`/`#000000`, returned verbatim for `#C0C0C0` and `#D4D0C8` faces by [`classic_bevel_ramp_from_face()`](../../reference/themes/namespace-ui.md#classic_bevel_ramp_from_face) rather than computed |
| Edges | [`draw_bevel_border()`](../../reference/rendering/namespace-ui.md#draw_bevel_border) with nested rings and the `IntDrawRectEdge` corner ownership; command buttons use the `BF_SOFT` arrangement, windows and wells the plain one, a hot toolbar button the single inner edge |
| Page | the dialog page is `BTNFACE`, fields are `COLOR_WINDOW` |
| Push button | raised at rest, sunken and label +1, +1 when pressed, **no hover state**, engraved disabled label |
| Toolbar-style button | `ButtonVariant::Subtle` is the flat-until-hover toolbar button: nothing at rest, raised inner edge when hot, mirrored when pressed |
| Check box, radio | 13 px sunken wells (the classic indicator size) with a `COLOR_WINDOW` interior, a `BTNFACE` interior while held (`DFCS_PUSHED`) or disabled, a `BTNSHADOW` mark when disabled, engraved disabled label ([`disabled_text_effect`](../../reference/themes/theme.md#disabled_text_effect)); the mixed value is the gray tick on a face/highlight checkerboard. The radio is [`draw_classic_radio()`](../../reference/rendering/namespace-ui.md#draw_classic_radio): `BTNSHADOW`/`BTNHIGHLIGHT` outside `3DDKSHADOW`/`3DLIGHT`, split along the diagonal, a 12 px ring and a 4 px dot that match the five Marlett layers `DrawFrameControl` stacks pixel for pixel at 96 DPI, and are rebuilt at each device size above it (14, 18 and 20 px rings at 125, 150 and 175 %) rather than scaled. No hover in either |
| Fields | no focus underline or edge change; the caret is the cue (*"For text boxes, the system provides a blinking cursor"*, 1999 guidelines p. 428), so the pack overrides Fluent's bottom-only focus thickness that would otherwise grow a 2 px underline |
| Combo box | a field with a 16 px raised button and the 7 x 4 scroll arrow, a list in a 1 px `WINDOWFRAME` border |
| Expand and drop-down marks | [`disclosure_visual`](../../reference/themes/theme.md#disclosure_visual) `ClassicArrow`: solid 45-degree arrows drawn per device pixel ([`draw_classic_arrow()`](../../reference/rendering/namespace-ui.md#draw_classic_arrow), Marlett's `3` `4` `5` `6` and `8`) wherever Fluent draws a chevron: DropdownButton, SplitButton, NumberBox's spin buttons, a menu's cascade arrow, BreadcrumbBar's separators, NavigationShell's groups and TableView's group headers. The era has no expander, so Expander takes the drop-down's arrow, turning up once open. TreeView draws `TVS_HASBUTTONS`' 9 x 9 `GRAYTEXT` box with a plus or minus ([`draw_classic_tree_button()`](../../reference/rendering/namespace-ui.md#draw_classic_tree_button)) |
| Slider | a real trackbar with the [era's geometry](../../background/themes/win9x.md#trackbar-slider) ([`Slider`](../../reference/controls/slider.md), `classic_trackbar`); `set_tick_frequency()` is `TBM_SETTICFREQ`, and its default of 0 means the two end ticks, not none |
| Progress | `COLOR_HIGHLIGHT` on `COLOR_BTNFACE`, for `ProgressRing` too, which would otherwise draw in the accent button's background, the face color in this era, and vanish |
| Separators and dividers | the etched groove's dark half (`BTNSHADOW`) as the single-stroke divider, which `Splitter` also draws at rest |
| Corners | 0 everywhere, popups, flyouts, dialogs, menus and tooltips included |
| Rasterization | aliased geometry, aliased text, snapped to device pixels |
| Motion | `anim_faster` and `anim_fast` zero; menus and drop-downs appear without an effect |
| Shadow | none, popups included ([`popup_shadow`](../../reference/themes/theme.md#popup_shadow) `None`); `CS_DROPSHADOW` is a Windows XP API |
| Focus | a dotted rectangle inset inside the control ([`focus_visual`](../../reference/themes/theme.md#focus_visual)) |
| Selection | `HIGHLIGHT` behind `HIGHLIGHTTEXT`; no sliding accent bar in `NavigationShell` or `SelectorBar`, because the era has no accent |
| Window frame | [`ChromeHints`](../../reference/themes/chrome-hints.md) ask DWM for square corners, the palette's caption color and a `WINDOWFRAME` border, and decline Mica |
| High contrast | the OS's contrast colors through this same beveled construction, as a Windows 95 user in High Contrast Black still had beveled buttons; when an app forces `ThemeMode::HighContrast` without one, Windows 98 SE's own High Contrast Black |

### Approximated

- **The font.** MS Sans Serif is a bitmap `.fon` that DirectWrite cannot use, and asking for it by
  name gets the system fallback, Segoe UI, not its TrueType successor. The pack names **Microsoft
  Sans Serif** outright, at 8 pt (10.667 DIPs) on a 13 px line. That is the 8 pt strike's own
  geometry as GDI reports it (`tmHeight` 13, ascent 11, internal leading 2, so an 11 px em), and
  rendered aliased it puts the caps and ascenders on the same nine rows as the bitmap. Widths
  stay within a few percent: "The quick brown fox, OK Cancel 0123" is 179 DIPs against the
  strike's 181 px, "Cancel" 33.3 against 33. The mismatch left is the outline face's own stems
  and sidebearings, so a line break can still land one word apart. Every text role uses this one
  face and size; the era has no type ramp.
- **The button's inner light ring follows the reference implementation, not the prose.** The pack
  draws `3DLIGHT` (`#DFDFDF`) where the 1999 guidelines say `BTNFACE` (`#C0C0C0`). See the
  [edge grammar](../../background/themes/win9x.md#the-edge-grammar) for why the two disagree.
- **The focus rectangle** is a dotted line in a fixed color (`BTNTEXT`), not an XOR of a dither
  brush. A render-command stream has no raster operation. It matches the original over flat faces,
  which is nearly everywhere, and differs over anything non-uniform.
- **The check mark** comes from the icon font, not Marlett: the icon font's tick drawn in the
  era's colors. The radio, the arrows and the tree view's box are drawn pixel by pixel in
  Marlett's shapes instead (see above); no Marlett outline is copied or shipped.
- **ToggleSwitch** has no era equivalent. It is drawn as a square latched-button well
  (`ToggleSwitchShape::ClassicWell`), raised when off and sunken when on, with no knob motion.
- **ProgressBar** is `PROGRESS_CLASS` without `PBS_SMOOTH`: a 16 px `COLOR_BTNFACE` well inside
  the static edge (`BDR_SUNKENOUTER`), a pixel of face, then `COLOR_HIGHLIGHT` chunks two thirds
  as wide as the space inside is tall, 2 px apart (`ControlChrome::framed_progress_bar`). The
  height is the library's pick, since the control takes whatever its dialog gives it. The
  indeterminate mode, five chunks sweeping across, is an animation the era never had.
- **A custom scheme's ramp** from `ClassicPalette::from_face()` is computed with WinForms'
  integer-HLS lighten and darken. That is a ramp in the Windows 95 manner, not "the classic ramp";
  no function reproduces Windows 2000's flattening, which is why that row is literal data.
- **Severity.** The era has no semantic color slots, so an `InfoBar` is `COLOR_WINDOW` inside a
  1 px `WINDOWFRAME` rule, the shape Luna uses too. The hues stay on Fluent's readable fallback: the
  MessageBox icons are 16-color art inside the 16-bit `USER.EXE`, which the tooling cannot read,
  and Windows 98 SE's `user32.dll` has only a version resource.
- **The bevel under a Windows 11 contrast theme.** Those themes have no bevel slots, so the ramp is
  derived: the outer rings are the button text color, the inner highlight and shadow the text
  blended 25 % and 50 % toward the face. For white on black that gives `#BFBFBF` and `#808080`,
  Windows 2000's and XP's High Contrast Black to within one step, and both edges stay visible on
  any face. The missing `COLOR_MENU` and `COLOR_INFOBK` slots take the window color, as Windows
  2000's High Contrast Black does.

### Not Reproduced

- **The `BS_DEFPUSHBUTTON` ring.** Every era source says "heavy" and none gives a width, so
  `ButtonVariant::Accent` draws the same button as the default style.
- **The option-set toolbar dither.** A checked toolbar button does not sit on the checkerboard.
- **`TBS_TOP`, `TBS_LEFT` and `TBS_BOTH` trackbars.** Only the stock style (point and ticks on the
  far side) is drawn, and every tick placement other than none gets the far-side row. Given more
  room across than it asked for, the whole assembly is centered on a whole pixel.
- **The artwork.** Marlett and the standard toolbar bitmaps are Microsoft's, and libwinui never
  ships Microsoft art. Nothing in the edge grammar needs any.
- **Non-client chrome.** The title bar, borders and system menu stay DWM's; the pack only hints
  colors. Windows 2000's caption gradient is not drawn.
- **Windows 2000 as its own variant.** Its palette row exists and is one `classic_scheme` away, but
  Tahoma, flat menus, the caption gradient and the blue-gray desktop are not modeled as a pack.
- **Windows Me**, whose palette is unknown, and the named alternate schemes, which were never
  decoded.

### Reference

The generated reference has the details: [`Theme`](../../reference/themes/theme.md),
[`ClassicPalette`](../../reference/themes/classic-palette.md),
[`ClassicBevelRamp`](../../reference/themes/classic-bevel-ramp.md),
[`ButtonVisual`](../../reference/themes/button-visual.md) and the
[themes overview](../../reference/themes/index.md).

## Windows 3.x

The era page is [Windows 3.x](../../background/themes/win3x.md). libwinui's variant is
`ThemeVariant::Windows3x`.

The rule every pack follows applies here: behavior, layout and semantics are identical to every
other theme, and only [`Theme`](../../reference/themes/theme.md) data changes. `Windows3x` is
`resolve_windows3x()`, which is [`Theme::classic()`](../../reference/themes/theme.md#classic) over
[`ClassicPalette::windows_standard()`](../../reference/themes/classic-palette.md#windows_standard)
with `win3x_edge = true`. It shares the palette and the derivation with `Windows9x` and uses a
different edge construction.

```cpp
ui::WindowOptions options{.title = "Program Manager"};
options.theme.variant        = ui::ThemeVariant::Windows3x;
options.theme.classic_scheme = ui::ClassicPalette::monochrome_3x(); // optional, any ClassicPalette
```

The variant is light-only and ignores `ThemeMode` Light/Dark and the accent override. A
[`ThemeRequest`](../../reference/themes/theme-request.md)'s `classic_scheme` swaps in any other
[`ClassicPalette`](../../reference/themes/classic-palette.md), so "Windows 3.1 with Hotdog Stand"
is one literal.

The palette is labeled the
[shipped-scheme consensus](../../background/themes/win3x.md#the-22-shipped-schemes-and-the-missing-default),
never "the Windows Default", and libwinui keeps that label.

### Reproduced

| Era trait | How libwinui states it |
|-----------|------------------------|
| The four-color 1 : 2 edge | [`draw_win3x_border()`](../../reference/rendering/namespace-ui.md#draw_win3x_border): 1 px `window_frame` frame, 1 px highlight top/left, 2 px shadow right/bottom. `Theme::classic()` sets `ButtonVisual::bevel_frame` on every push-button state, which routes the renderer there instead of the Windows 95 bevel. Every ring lands inside `bounds`, the frame included: its path is pulled in half a step and its radius reduced to match, so a control that clips to its own rect (`SwipeControl` does) keeps the whole frame instead of losing half a pixel to the clip and the rest to aliasing. |
| Pressed drops an edge | the sunken style draws one shadow edge top/left and none on the bottom/right; `pressed_content_offset` is +1, +1 |
| Rounded push-button corners | [`button_corner_radius`](../../reference/themes/theme.md#button_corner_radius) 1. The radius rounds the frame only; the highlight and shadow inside stay straight, as in the art. Every other surface is square, popups included (`overlay_corner_radius` 0). |
| The palette | the shipped-scheme consensus: face `#C0C0C0`, highlight `#FFFFFF`, shadow `#808080`, frame and text `#000000`, `COLOR_WINDOW` white fields, gray text `#808080`. `3DLIGHT` and `3DDKSHADOW` are collapsed onto the highlight and shadow, since the era has neither slot. |
| Flat 13 x 13 check box with an X | `check_glyph` is the X (`glyphs::Cancel`); the box and the radio ring are flat 1 px `window_frame` outlines on `COLOR_WINDOW` with the mark in `COLOR_BTNTEXT`, no bevel; `toggle_box_size` 13 |
| The gray-pattern mixed check box | `check_box_indeterminate` draws a real 50 % checkerboard of ink and window color inside the square, with no X |
| Plain one-pixel group box and separators | [`divider_visual`](../../reference/themes/theme.md#divider_visual) `FlatStroke` in `COLOR_WINDOWFRAME`, never the etched groove |
| Two UI fonts, the chrome one bold | `text_style` is MS Sans Serif 8 pt's stand-in, Microsoft Sans Serif at 10.667 DIPs on a 13 px line, for every control; [`chrome_text_style`](../../reference/themes/theme.md#chrome_text_style) stands in for System (a 13 px em on a 16 px line, bold) with Microsoft Sans Serif **Bold** at 12 px on a 16 px line, which [MenuBar](../../reference/controls/menu-bar.md) uses for its bar and items |
| Aliased, snapped rasterization | [`rasterization`](../../reference/themes/theme.md#rasterization): aliased geometry, aliased text, device-pixel snapping |
| Solid arrows, never chevrons | [`disclosure_visual`](../../reference/themes/theme.md#disclosure_visual) `ClassicArrow`, shared with Windows 9x: the era's scroll and menu arrows are the same solid triangles (`OBM_DNARROW`, `OBM_MNARROW`), and the tree view's plus/minus box stands in for File Manager's marked folders |
| No hover | `hot_track` is off, so `button_hover` equals `button_normal` and the subtle (toolbar) button reuses the normal bevel |
| No motion | `anim_faster` and `anim_fast` are 0, and menus and drop-downs open without animation (`anim_normal` only drives Fluent's slider thumb grow, which the classic thumb does not have) |
| No shadow | [`popup_shadow`](../../reference/themes/theme.md#popup_shadow) `None` |
| Disabled text | plain `COLOR_GRAYTEXT`. No emboss, because the era has no `DrawState`; the embossed look belongs to its color chrome glyphs, not to text |
| No accent | `accent_selection_indicator` is off; a selected row is `COLOR_HIGHLIGHT` behind `COLOR_HIGHLIGHTTEXT`. `ButtonVariant::Accent` draws as a normal button. |
| Dotted focus box around a check box's or radio's label | [`focus_visual`](../../reference/themes/theme.md#focus_visual) |
| Square window frame | [`ChromeHints`](../../reference/themes/chrome-hints.md) asks DWM not to round corners, with a navy `#000080` caption and a black border |

### Approximated

- **The fonts.** System and MS Sans Serif are bitmap `.FON` faces DirectWrite cannot load; asked
  for by name, both fall back to Segoe UI. libwinui names Microsoft Sans Serif instead, sized to
  the strikes' metrics (see the [Windows 9x pack](#windows-9x)); a menu bar set in it is 174 px
  where System sets 175. The mismatch left is metric, the outline face's stems against the
  bitmaps'.
- **The corner.** `OBM_BTNCORNERS` is hand-drawn art. libwinui draws a 1 px radius instead of the
  shipped corner mask; the art is never extracted.
- **The disabled check box.** The era dithers the glyph 50 %; libwinui draws the outline and the
  mark flat in `COLOR_GRAYTEXT`. The mixed check box is drawn as a real dither, the disabled one is
  not.
- **The chosen colors.** The desktop gray and the caption blue are known only as the words "gray"
  and "blue". The caption takes the classic navy `#000080`, which is the palette's selection color.
- **Surfaces the era leaves open.** The page is `COLOR_BTNFACE` gray, the classic derivation shared
  with Windows 9x; a plain Windows 3.1 dialog without CTL3D did not paint its background in the
  face color. Menus take the face color as well, while the three decoded 3.1 schemes all set
  `Menu` to white.
- **Push-button focus.** The guide puts the dotted box around the label of a command button too;
  libwinui's [Button](../../reference/controls/button.md) insets it inside the button, the Windows
  95 placement. Focus is also hidden until keyboard use in every theme, where the era always shows
  it.
- **Controls the era does not have.** [ToggleSwitch](../../reference/controls/toggle-switch.md) is
  approximated as a latched button well drawn with the era's own edge, square and motionless
  (`toggle_switch_shape` `ClassicWell`).
  [Slider](../../reference/controls/slider.md) draws the Windows 95 trackbar construction from the
  shared classic derivation, and the progress controls fill with `COLOR_HIGHLIGHT` on a
  `COLOR_BTNFACE` well. The bar is one solid block inside a 1 px `COLOR_WINDOWFRAME` frame, the
  frame of the era's fields. The era has no progress control, so that is the classic grammar
  extended, not a measurement. Tooltips and hyperlinks take the palette's yellow and blue defaults, slots
  the era does not have. Severity bars use `COLOR_WINDOW` in a 1 px frame with readable fallback
  hues, since the era's only severity vocabulary is the message-box icon art inside `USER.EXE`.

### Cannot, on Purpose

- **The artwork.** `OBM_CHECKBOXES` and `OBM_BTNCORNERS` are Microsoft's art. They are documented
  as measurements and are never extracted; re-authoring them by hand stays future work.
- **Things that are not theme data.** The right-hand button column, OK instead of a verb, the
  Cancel-to-Close relabeling, modality shown in the frame, MDI and Program Manager chrome are
  layout, content or non-client behavior. A period-style example application can use them; a
  theme may not change them.
- **The default button's heavy ring.** No source states its width, and `ButtonVariant::Accent` has
  no era marker to draw.
- **Custom non-client rendering.** The title bar, the Control-menu box and the modal frame come
  from DWM with a caption color hint; drawing the era's own non-client area is not done.

### High Contrast

The era has no high-contrast concept, but every slot is user-settable, so contrast was something
a user dialed in by hand. While the OS is in a contrast theme, libwinui maps its colors into a
`ClassicPalette` slot for slot and keeps the Windows 3.x edge. When an app forces
`ThemeMode::HighContrast` with none on, it uses the era's own closest scheme,
[`ClassicPalette::monochrome_3x()`](../../reference/themes/classic-palette.md#monochrome_3x):
black on white, silver buttons, a black selection and title bar. The other classic types involved
are [ClassicBevelRamp](../../reference/themes/classic-bevel-ramp.md) and
[ButtonVisual](../../reference/themes/button-visual.md).

## Early Windows

libwinui draws the [Early Windows](../../background/themes/winold.md) era (Windows 1.x and 2.x,
1985 to 1990) as `ThemeVariant::Windows1x`.

`ThemeVariant::Windows1x` is resolved by `Theme::early()` from one
[`EarlyPalette`](../../reference/themes/early-palette.md). It is deliberately not
[`Theme::classic()`](../../reference/themes/theme.md#classic) with the bevel turned off: this era has
no face, no ramp and no highlight slot to hand a bevel routine, so it has its own tier-1 struct
whose three 2.x-only slots are `std::optional`. A struct with a `face` field is already the wrong
era. See [`Theme::early()`](../../reference/themes/theme.md#early).

The variant defaults to the Windows 2.03 color row.
[`ThemeRequest::early_scheme`](../../reference/themes/theme-request.md) selects any of the other
three:

```cpp
ui::WindowOptions options{.title = "MS-DOS Executive"};
options.theme.variant      = ui::ThemeVariant::Windows1x;
options.theme.early_scheme = ui::EarlyPalette::windows_1x_mono(); // the 1985 look
```

The pack is light-only and ignores `ThemeMode`'s light/dark and `accent_override`.

### High Contrast

Under high contrast the pack keeps its construction and fills every slot from the OS contrast
theme, because every slot was user-settable in Control Panel and contrast was something a user
dialed in by hand. A forced `ThemeMode::HighContrast` with no contrast theme on takes the
monochrome row of the same version (1.01 mono for a 1.x scheme, 2.03 mono otherwise).

### Reproduced

| Era fact | How the pack states it |
| --- | --- |
| The four default palettes | `EarlyPalette::windows_2x_color()` (default), `windows_2x_mono()`, `windows_1x_mono()`, `windows_1x_color()`, value for value as in [the era page's table](../../background/themes/winold.md#the-four-default-palettes) |
| White page, no gray dialog | `colors.background` is `COLOR_WINDOW`; fields, tooltips and dialogs use it too |
| No bevel, 1 px frames | `border_width` 1, every `ButtonVisual` is `BorderStyle::Flat`, stroked inside its bounds |
| Square everything but the push button | `corner_radius` and `overlay_corner_radius` 0; `button_corner_radius` 1, the one pixel motif A cuts |
| Reverse-video selection | `colors.selection` / `selection_text` and the menu highlight are the swapped pair from [`early_reverse_video()`](../../reference/themes/namespace-ui.md#early_reverse_video) |
| Push button is an outline on the page | `COLOR_WINDOW` fill, `COLOR_WINDOWFRAME` outline, `COLOR_WINDOWTEXT` label |
| Pressed is inverted, label does not move | `press_state_flip` on; ink fill with paper label; `pressed_content_offset` 0,0 |
| No hover | hover visuals equal the rest visuals |
| Disabled grays the label only | frame and corners unchanged, label in `colors.text_disabled` |
| Default button's bold border | `ButtonVariant::Accent` maps to it: same colors, 2 px stroke. The era has no accent color, so its one prominence marker is a shape. |
| Button padding | 8, 3, 8, 4 px: 32 x 14 DU around a six-cell `Cancel` gives 8 px each side, and the odd vertical pixel goes to the bottom where 1.0x drew the underscore |
| Check box | 14 x 14 flat square, checked mark an X |
| Radio | a ring with an 8 px disc in the text color |
| Grayed third state | a real 50 % checkerboard inside the box, no X (`IndeterminateMark::None`) |
| Aliased everything | `rasterization`: aliased geometry, aliased text, snapped to device pixels |
| One face, no ramp | every `type` role equals `text_style`; `chrome_text_style` unset; regular weight |
| Focus | the 2.x dotted box (`FocusVisual::ClassicDotted`), matching the default 2.03 palette |
| No motion | all three state durations 0 ms, linear easing, menus and lists open with no animation |
| No shadow, no scrim | `PopupShadow::None`; `colors.scrim` transparent |
| No accent selection bar | `accent_selection_indicator` off |
| Flat separators | `DividerVisual::FlatStroke` in `COLOR_WINDOWFRAME` |
| Scroll bar | `COLOR_SCROLLBAR` trough, `COLOR_WINDOW` thumb, `COLOR_WINDOWFRAME` arrows |
| Title bar | [`ChromeHints`](../../reference/themes/chrome-hints.md) ask DWM for square corners and the palette's caption and border colors (`COLOR_ACTIVEBORDER`, or the frame color on a 1.x row that has none); no system backdrop |

### Approximated

| Gap | What the pack does, and why |
| --- | --- |
| The `System` font | This is the main approximation. The pack asks for **`Consolas` at 14 px** with a 14 px line height: 7.70 px advance against the era's 8.00, and 14 px is the era's own line pitch. Unlike the Windows 3.x and 9x packs, which set Microsoft Sans Serif in place of their proportional bitmap faces, this one asks for a monospaced face, because losing the pitch changes layout, not just texture. The stems are 1 px where the era's are 2, and the zero is dotted where the era's is slashed. |
| Disabled gray | The era's disabled label is a 50 % checkerboard; [`early_dither_gray()`](../../reference/themes/namespace-ui.md#early_dither_gray) draws the flat color it averages to (`#808080` for black on white). A disabled glyph gets the same flat gray. |
| Reverse video on color rows | The pack swaps foreground and background, which is the era's prose rule. Real `InvertRect` is a bitwise complement: over the 2.03 cyan menu bar it would come out red, not black. |
| Rounded corner | A 1 px radius stands in for motif A's hand-drawn cut. The default button keeps the same radius at 2 px stroke; motif B cuts two pixels. |
| The X | An icon-font glyph, not the hand-drawn 1-bit X. |
| `cxBorder` | Always 1; the CGA and EGA low-resolution value of 2 is not modeled. The pack chooses the square-pixel adapters. |
| Controls the era lacks | Each is built from era parts and labeled an invention in `src/ui/themes/ThemeEarly.cpp`. The [Slider](../../reference/controls/slider.md) is a 1 px `COLOR_WINDOWFRAME` rule under a solid 15 x 14 block (the `OBM_BTSIZE` size), never filled behind the thumb. The [ProgressBar](../../reference/controls/progress-bar.md) is a field's 1 px `COLOR_WINDOWFRAME` rule around `COLOR_WINDOW`, filled with one `COLOR_WINDOWTEXT` block, reverse video being the era's only state color. The [ToggleSwitch](../../reference/controls/toggle-switch.md) is a square, motionless well with a single flat frame and a moving knob. Severity is an ink sign on white, because the era's icons are 1 bpp and cannot color a state. |

### Cannot

| Gap | Why |
| --- | --- |
| Pressed check box and radio | The era emboldens the outline from 1 px to 2 px. `ButtonVisual` has no per-state stroke width for the indicator, so pressed looks like rest. |
| `WS_DLGFRAME`'s double frame | No `Theme` field holds a second frame ring, so a dialog's outline is not distinguished from a plain window's. |
| The 1.0x blinking underscore | No `Theme` field for it. The pack draws the 2.x dotted box, consistent with its default 2.03 palette. |
| The original artwork | `OBM_CHECKBOXES` and `OBM_BTNCORNERS` are Microsoft's hand-drawn art. They are documented as measurements and never extracted; re-authoring them stays future work. |
| Era behavior | Tiled window placement, the Del / F2 / Ins shortcuts, the five-item System menu and `Ok` vs `OK` are behavior and content. Every theme in libwinui keeps behavior, layout and semantics identical, so none of them is theme data. |

Unusually for an era pack there is otherwise very little to approximate. The chrome is line art and
the colors are in Microsoft's own driver source, so most of the look is reproducible exactly. The
next era, [Windows 3.x](../../background/themes/win3x.md), keeps this era's rounded button corner
and check-box X and adds the first real bevel.
