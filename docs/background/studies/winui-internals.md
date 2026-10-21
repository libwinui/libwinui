# WinUI Internals

How WinUI 3 is put together, which parts of its look are architecture and which are data, and what libwinui takes from it.

The question behind this study was simple: libwinui wants to look and feel like Windows 11, so what actually makes WinUI look the way it does? The answer turned out to be encouraging. Most of the Windows 11 look lives in design tokens (colors, radii, paddings, durations) and a small set of drawing conventions, not in the engine. An engine that is built very differently can still match it, as long as it carries the same numbers and the same token structure.

Everything here was read from the MIT-licensed `microsoft-ui-xaml` repository[^repo], mostly from the token files under `controls/dev/CommonStyles/`, the system styles in `dxaml/xcp/dxaml/themes/generic.xaml`, and the subsystem design notes under `docs/design-notes/`. The token values are the same numbers Microsoft publishes for the Windows 11 design system[^color][^geometry]. Code is described here, not transcribed.

## Repository Shape

The repository holds two generations of controls side by side, and they are built in completely different ways. Reading anything in it correctly starts with knowing which one you are looking at.

```
microsoft-ui-xaml/
    dxaml/xcp/            "system XAML", the framework itself (Silverlight lineage)
        core/             CDependencyObject, CUIElement, CFrameworkElement, core elements, layout
        components/       styles, theming, focus, brushes, composition tree, resources, parser
        dxaml/themes/     generic.xaml: legacy styles for the ~66 built-in controls
    controls/dev/<Name>/  "MUX controls": one folder per modern control
    docs/design-notes/    ~78 subsystem design documents
    specs/                public API specs
```

The **old-generation controls** (Button, CheckBox, TextBox, ListView and the rest of the original set) have three layers[^control-overview]. The IDL identifies the type, the DXaml layer holds generated property boilerplate (`Foo.g.cpp`) and hand-written behavior (`Foo_partial.cpp`), and the Core layer (`CFoo` under `xcp/core/core/elements/`) is where the control "loses its identity as a control" and becomes part of lifetime, tree entry, layout and rendering.

The **new-generation MUX controls** (NavigationView, Expander, SplitButton, ItemsView, TabView, InfoBar, TitleBar and friends) are ordinary C++/WinRT templated controls with no core peer at all. They are written against the public XAML API, the same way a third party would write a control. The design notes say so plainly: newer controls that came from WinUI 2 don't follow the three-layer pattern.

Microsoft's own direction is away from the deep, core-peered control and toward thin compositions over a handful of primitives (Border, ContentPresenter, Grid) plus a state machine. `CUIElement`'s header runs to about 1,700 lines and carries around twenty layout dirty-flag bits. libwinui's `Element` (`include/winui/ui/Element.hpp`) is one flat interface, which is much closer to how the MUX controls are written than to `CUIElement`.

## Control Model

### The Type Hierarchy

```
DependencyObject          property storage, change notification, theme walk
 └─ UIElement             hit testing, input, rendering, layout dirty flags, transforms
     └─ FrameworkElement  Width/Height/Min/Max, Margin, alignment, Style, Resources, Loaded
        ├─ Panel          Children: StackPanel, Grid, Canvas, RelativePanel, ...
        ├─ Border         one Child plus background, border brush, thickness, corner radius
        ├─ TextBlock      not a Control, so no template
        └─ Control        Template, Background, Foreground, BorderBrush, Padding, IsTabStop, ...
            ├─ ContentControl → ButtonBase → Button, ToggleButton, RepeatButton
            ├─ ItemsControl   → Selector → ListViewBase → ListView, GridView, ComboBox
            └─ RangeBase      → Slider, ProgressBar, ScrollBar
```

Two properties of this hierarchy shape everything built on it.

First, `Control` carries a bag of placeholder properties that do nothing on their own. `Background`, `BorderBrush`, `BorderThickness`, `CornerRadius`, `Padding` and `HorizontalContentAlignment` have no built-in effect. They only matter because the control's template `TemplateBinding`s them onto real elements. The styling guide gives control authors a list of seventeen such properties to walk through and wire up, or consciously not[^styling-guide].

Second, only the font properties and `Foreground` inherit down the tree implicitly. Everything else has to be bound explicitly.

### Anatomy of a Templated Control

A modern MUX control is six pieces:

| Piece | File | Holds |
| --- | --- | --- |
| Public API | `Foo.idl` | dependency properties, events, `FooTemplateSettings` |
| Behavior | `Foo.cpp` / `Foo.h` | the state machine, event handling, `OnApplyTemplate`, `UpdateVisualStates` |
| Default style and template | `Foo.xaml` | `DefaultFooStyle` and its `ControlTemplate` |
| Tokens and variants | `Foo_themeresources.xaml` | theme dictionaries, metrics, alternate styles |
| Accessibility | `FooAutomationPeer.cpp` | the UI Automation pattern, name and role |
| Strings | `Strings/` | localized text |

The template contract is defensive by design[^authoring]. Named parts are fetched in `OnApplyTemplate()` via `GetTemplateChild`, stored as the most generic type that works, and null-checked on every use, because a consumer may supply a custom template that leaves parts out. `SplitButton.cpp` is a clean example: it fetches its primary and secondary buttons, registers about fourteen revokers on each, and guards every access.

Computed values go the other way through `TemplateSettings`: a read-only property bag (for example `ExpanderTemplateSettings.ContentHeight`) that the template binds to. The styling guide's rule is "never modify a control's visual with codebehind", since that would break re-templating.

### The VisualStateManager

A template declares `VisualStateGroup`s, and each group holds mutually exclusive `VisualState`s. The universal group is `CommonStates` with `Normal`, `PointerOver`, `Pressed` and `Disabled`. Code never sets visuals directly. It computes a state name and calls `VisualStateManager::GoToState`.

One hard rule appears twice in the design notes: two different groups must never target the same property of the same element. That is why CheckBox has a single `CombinedStates` group of twelve states (`UncheckedNormal` through `IndeterminateDisabled`) rather than a four-state group crossed with a three-state one.

A state's effect is either a plain `Setter` or a `Storyboard`. The most interesting finding in the whole repository is what happened to the storyboards. The `*_perf2026.xaml` files are a mechanical rewrite of the templates, and diffing `Button_themeresources.xaml` against its `perf2026` twin shows every state storyboard of the shape "`ObjectAnimationUsingKeyFrames` with one `DiscreteObjectKeyFrame` at time 0" being replaced by a `VisualState.Setters` entry. In other words, WinUI is removing animation machinery from state changes that were never animated in the first place and turning them into direct property assignments. What stays animated is genuine motion.

The real animation of a hover or press now comes from a property transition declared once on the element:

```xml
<ContentPresenter.BackgroundTransition>
  <BrushTransition Duration="0:0:0.083" />
</ContentPresenter.BackgroundTransition>
```

That is an 83 ms cross-fade on every `Background` change, handed to the compositor (`CBorder::SetValue` calls `WUCBrushManager::SetUpBrushTransitionIfAllowed`). The [WinUI animations study](winui-animations.md) follows this thread further.

**What libwinui does.** A control computes a `ButtonState` (`Normal`, `Hover`, `Pressed`, `Disabled` in `include/winui/ui/themes/Theme.hpp`) and looks up a fully resolved `ButtonVisual` for that state. That is the `perf2026` design, written in C++ instead of markup. Focus is not a member of `ButtonState`: as in WinUI, where `CommonStates` has no `Focused` state, the focus visual is a separate ring drawn over whatever state the control is in, so a focused button still shows its hover appearance.

### Two Details Worth Copying

- **`BackgroundSizing`.** The modern Button uses `InnerBorderEdge`, so the fill stops at the inner edge of the border instead of running under it. With a translucent 1px border over a translucent fill the difference is visible. libwinui's `Border` (`include/winui/ui/containers/Border.hpp`) follows the same default.
- **Negative `FocusVisualMargin`.** Button uses `-3` and CheckBox `-7,-3,-7,-3`, so the focus ring sits outside the control's bounds and overlaps its neighbors.

## Styling System and Fluent Tokens

This is the part of WinUI that explains why Windows 11 looks coherent, and the part libwinui adopted most directly.

### Three Tiers of Indirection

Every color in WinUI goes through three named levels before it reaches a pixel:

```
Tier 1  raw Color, per theme            ControlFillColorDefault = #0FFFFFFF (dark) / #B3FFFFFF (light)
Tier 2  semantic brush                  ControlFillColorDefaultBrush
Tier 3  per-control alias, per theme    ButtonBackground            -> ControlFillColorDefaultBrush
                                        ButtonBackgroundPointerOver -> ControlFillColorSecondaryBrush
                                        ButtonBackgroundPressed     -> ControlFillColorTertiaryBrush
                                        ButtonBackgroundDisabled    -> ControlFillColorDisabledBrush
Template                                {ThemeResource ButtonBackground}
```

Each tier exists for a reason. Color and brush are split because some properties want a brush and others (a gradient stop) want a color. Tier 3 is mandatory: the styling guide says a control must never reference a tier-2 brush directly, even when it would theme correctly[^styling-guide]. The reason is lightweight styling[^xaml-styles]. `ButtonBackgroundPointerOver` is a public, documented key, so an app that wants a different hover color overrides one resource instead of copying a two-hundred-line template.

Key names follow a mechanical pattern, `<Control><Part?><Property><State?>`, as in `CheckBoxCheckGlyphForegroundCheckedDisabled`. Ramp positions carry a fixed meaning:

| Suffix | Meaning |
| --- | --- |
| `Default` / `Primary` | rest |
| `Secondary` | pointer over |
| `Tertiary` | pressed |
| `Disabled` | disabled |

So `ControlFillColorDefault/Secondary/Tertiary/Disabled` literally is "a control fill in its four states", and every control reuses the same four rungs. There is one interaction ramp per class of surface, not one per control, and that is a large part of why WinUI apps look consistent.

### Theme Dictionaries and Lookup

Each token file holds three theme dictionaries: `Default`, `Light` and `HighContrast`. The trap is that **`Default` is the dark theme**, not a fallback. `Light` is the explicit one.

Lookup goes from the dictionary holding the reference, to `Application.Resources`, to the framework's `generic.xaml`, to system colors[^resources]. Within a dictionary the order is the dictionary itself, then merged dictionaries (last merged first), then theme dictionaries. The first hit wins.

`{StaticResource}` is resolved once at parse time. `{ThemeResource}` is re-evaluated when the theme changes[^theme-resources]. Every control file mixes them on purpose: the template references its tier-3 key with `ThemeResource`, and the tier-3 entry references its tier-2 brush with `StaticResource`, because the whole dictionary is swapped anyway. That is also why tier-3 keys are repeated in all three dictionaries even when the right-hand side is textually the same. The duplication is what makes the swap work.

**What libwinui does.** There is no late binding at all. A `Palette` (`include/winui/ui/themes/Palette.hpp`) is the tier-1/2 table, with one field per Fluent role named after the XAML token (`control_fill_default`, `text_fill_primary`, `accent_fill_default`, ...). The pure function `Theme::fluent(palette)` derives every per-control field of a `Theme` from it, which is the direct analog of the `*_themeresources.xaml` files. Lightweight styling is assigning to one field of the resolved `Theme` or `Palette`. A theme change re-resolves the whole snapshot, which collapses static and theme references into one "recompute" step. `Palette::fluent_light()` and `fluent_dark()` hold the Windows 11 tables below, and `fluent1_light()`/`fluent1_dark()` fill the same roles from the Windows 10 resources. See [Theming](../../contributing/how-it-works/theming.md) for how the resolved theme reaches the controls.

### High Contrast

High contrast is not a third color scheme derived from the design system. It is a separate dictionary where almost every brush resolves straight to a system color (`SystemColorWindowTextColor`, `SystemColorButtonFaceColor`, `SystemColorHighlightColor`, `SystemColorGrayTextColor`, `SystemColorHotlightColor`), bypassing the Fluent ramp. The Fluent color tokens are still defined in the high-contrast dictionary, but set to `#FF0000` on purpose, so a code path that wrongly reads one shows bright red instead of failing quietly. Elevation gradients degrade to flat window-text strokes.

Internally, `Theming::Theme` (`dxaml/xcp/components/theming/inc/Theme.h`) packs this into five bits: a two-bit base (none, light, dark) and a three-bit high-contrast field (none, high contrast, white, black, custom). High contrast adds to the base and takes precedence over it.

libwinui does the same: `ThemeMode::HighContrast` (`include/winui/ui/themes/ThemeSource.hpp`) wins over every other request field, and the colors come verbatim from the OS contrast theme.

### Accent Color

The accent is `SystemAccentColor` plus a six-rung tonal ramp, `SystemAccentColorLight1..3` and `SystemAccentColorDark1..3`, which WinUI reads from `UISettings::GetColorValue` (`SystemThemingInterop.cpp`)[^color]. Which rung a token uses depends on the theme:

| Token | Dark | Light |
| --- | --- | --- |
| `AccentFillColorDefaultBrush` | `Light2` | `Dark1` |
| `AccentFillColorSecondaryBrush` | `Light2` at opacity 0.9 | `Dark1` at 0.9 |
| `AccentFillColorTertiaryBrush` | `Light2` at opacity 0.8 | `Dark1` at 0.8 |
| `AccentTextFillColorPrimaryBrush` | `Light3` | `Dark2` |
| `AccentTextFillColorTertiaryBrush` | `Light2` | `Dark1` |

Hover and pressed accents are the same color at lower opacity, not different rungs. And the light theme uses a darker accent while the dark theme uses a lighter one, so a filled accent surface always contrasts with the page. libwinui's `AccentRamp` (`include/winui/ui/themes/Theme.hpp`) carries the six rungs and follows this table.

### The Elevation Border

The subtle thing that makes Windows 11 controls look very slightly raised without a shadow is a gradient stroke:

```xml
<LinearGradientBrush x:Key="ControlElevationBorderBrush"
                     MappingMode="Absolute" StartPoint="0,0" EndPoint="0,3">
  <GradientStop Offset="0.33" Color="{StaticResource ControlStrokeColorSecondary}" />
  <GradientStop Offset="1.0"  Color="{StaticResource ControlStrokeColorDefault}" />
</LinearGradientBrush>
```

The mapping is absolute, so the gradient spans three device pixels from the edge it starts at and then holds, whatever the control's height. On a 32px button almost the whole 1px border is one stroke color, and only the first few pixels of the rim blend into the other. The result is a barely perceptible contour that reads as lift.

`AccentControlElevationBorderBrush` carries a `ScaleTransform ScaleY="-1"` as its relative transform, which flips the gradient so it starts from the bottom edge instead. The neutral brush is not the same in every dictionary either: the Light dictionary carries the flip and `Default` (dark) does not. This per-theme flip is the Fluent detail most often missed. The styling guide's illustrative snippet does not match the token file here, and the token file is the one to trust. Round controls use `CircleElevationBorderBrush` instead, mapped relative to the bounding box with stops at 0.50 and 0.70, so it stretches with the control.

libwinui draws this with a dedicated render command, `StrokeRoundedRectGradient` (`include/winui/render/Renderer.hpp`). Its `extent_px` is the gradient span in device pixels, anchored at the top when positive and at the bottom when negative, which is how the flip is expressed without a second brush. `Palette::dark` exists so `Theme::fluent()` can tell which dictionary's shape of the neutral brush to use.

### Metrics and Timing Are Tokens Too

Colors are only half the system. Corner radii, paddings, sizes and durations are tokens as well, and the styling guide forbids hard-coding them.

There are exactly **two** corner radii in the Windows 11 system[^geometry]: `ControlCornerRadius` = 4 for controls and `OverlayCornerRadius` = 8 for flyouts, dialogs and popups. Everything else derives from them (`SliderThumbCornerRadius` 10 is a circle, `ListViewItemSelectionIndicatorCornerRadius` is 1.5).

| Token | Value |
| --- | --- |
| `ControlFasterAnimationDuration` | 83 ms |
| `ControlFastAnimationDuration` | 167 ms |
| `ControlFastAnimationAfterDuration` | 168 ms |
| `ControlNormalAnimationDuration` | 250 ms |
| `ControlFastOutSlowInKeySpline` | `0,0,0,1` |

libwinui's `Theme` carries both radii (`corner_radius`, `overlay_corner_radius`) and the durations (`anim_faster`, `anim_fast`, `anim_normal`, plus `anim_fast_out_slow_in`).

### The Type Ramp

`TextBlock_themeresources.xaml` is 51 lines long and it is the entire typographic system[^typography]:

| Style | Size | Weight |
| --- | --- | --- |
| `CaptionTextBlockStyle` | 12 | Normal |
| `BodyTextBlockStyle` | 14 | Normal |
| `BodyStrongTextBlockStyle` | 14 | SemiBold |
| `BodyLargeTextBlockStyle` | 18 | Normal |
| `SubtitleTextBlockStyle` | 20 | SemiBold |
| `TitleTextBlockStyle` | 28 | SemiBold |
| `TitleLargeTextBlockStyle` | 40 | SemiBold |
| `DisplayTextBlockStyle` | 68 | SemiBold |

All of them derive from `BaseTextBlockStyle`: `XamlAutoFontFamily` (Segoe UI Variable on Windows 11), trimming with a character ellipsis, wrapping on, `LineStackingStrategy=MaxHeight`. Styles of 18 and above also trim side bearings. Control content has its own size token, `ControlContentThemeFontSize` = 14, and icons use `SymbolThemeFontFamily` = `Segoe Fluent Icons, Segoe MDL2 Assets`.

### The Color Tables

From `Common_themeresources_any.xaml`. Dark is the `Default` dictionary. Eight hex digits are `#AARRGGBB`; six digits are opaque.

**Text**

| Token | Dark | Light |
| --- | --- | --- |
| `TextFillColorPrimary` | `#FFFFFF` | `#E4000000` |
| `TextFillColorSecondary` | `#C5FFFFFF` | `#9E000000` |
| `TextFillColorTertiary` | `#87FFFFFF` | `#72000000` |
| `TextFillColorDisabled` | `#5DFFFFFF` | `#5C000000` |
| `TextFillColorInverse` | `#E4000000` | `#FFFFFF` |
| `TextOnAccentFillColorPrimary` | `#000000` | `#FFFFFF` |
| `TextOnAccentFillColorSecondary` | `#80000000` | `#B3FFFFFF` |
| `TextOnAccentFillColorDisabled` | `#87FFFFFF` | `#FFFFFF` |

Primary text in the light theme is black at 89 % alpha, not pure black.

**Control fills**

| Token | Dark | Light |
| --- | --- | --- |
| `ControlFillColorDefault` | `#0FFFFFFF` | `#B3FFFFFF` |
| `ControlFillColorSecondary` (hover) | `#15FFFFFF` | `#80F9F9F9` |
| `ControlFillColorTertiary` (pressed) | `#08FFFFFF` | `#4DF9F9F9` |
| `ControlFillColorQuarternary` | `#0FFFFFFF` | `#C2F3F3F3` |
| `ControlFillColorDisabled` | `#0BFFFFFF` | `#4DF9F9F9` |
| `ControlFillColorTransparent` | `#00FFFFFF` | `#00FFFFFF` |
| `ControlFillColorInputActive` | `#B31E1E1E` | `#FFFFFF` |
| `ControlStrongFillColorDefault` | `#8BFFFFFF` | `#72000000` |
| `ControlSolidFillColorDefault` | `#454545` | `#FFFFFF` |

Every control fill is translucent. A Fluent button in the dark theme is not gray, it is about 6 % white over whatever lies behind it, and that layering is central to the look. Note also that the pressed rung is *less* opaque than rest in the dark theme (`#08` against `#0F`): pressed means "recede", not "darken".

**Subtle fills** (transparent buttons, list and menu items)

| Token | Dark | Light |
| --- | --- | --- |
| `SubtleFillColorTransparent` | `#00FFFFFF` | `#00FFFFFF` |
| `SubtleFillColorSecondary` (hover) | `#0FFFFFFF` | `#09000000` |
| `SubtleFillColorTertiary` (pressed) | `#0AFFFFFF` | `#06000000` |

**Strokes**

| Token | Dark | Light |
| --- | --- | --- |
| `ControlStrokeColorDefault` | `#12FFFFFF` | `#0F000000` |
| `ControlStrokeColorSecondary` | `#18FFFFFF` | `#29000000` |
| `ControlStrokeColorOnAccentDefault` | `#14FFFFFF` | `#14FFFFFF` |
| `ControlStrokeColorOnAccentSecondary` | `#23000000` | `#66000000` |
| `ControlStrongStrokeColorDefault` | `#8BFFFFFF` | `#72000000` |
| `CardStrokeColorDefault` | `#19000000` | `#0F000000` |
| `SurfaceStrokeColorFlyout` | `#33000000` | `#0F000000` |
| `DividerStrokeColorDefault` | `#15FFFFFF` | `#0F000000` |
| `FocusStrokeColorOuter` | `#FFFFFF` | `#E4000000` |
| `FocusStrokeColorInner` | `#B3000000` | `#B3FFFFFF` |

**Backgrounds and surfaces**

| Token | Dark | Light |
| --- | --- | --- |
| `SolidBackgroundFillColorBase` | `#202020` | `#F3F3F3` |
| `SolidBackgroundFillColorSecondary` | `#1C1C1C` | `#EEEEEE` |
| `SolidBackgroundFillColorTertiary` | `#282828` | `#F9F9F9` |
| `SolidBackgroundFillColorQuarternary` | `#2C2C2C` | `#FFFFFF` |
| `SolidBackgroundFillColorBaseAlt` | `#0A0A0A` | `#DADADA` |
| `CardBackgroundFillColorDefault` | `#0DFFFFFF` | `#B3FFFFFF` |
| `LayerFillColorDefault` | `#4C3A3A3A` | `#80FFFFFF` |
| `SmokeFillColorDefault` (dialog scrim) | `#4D000000` | `#4D000000` |

`SolidBackgroundFillColorBase` is the window background when no Mica backdrop is present. libwinui's Fluent theme uses it for `ThemeColors::background`.

**Status fills**

| Token | Dark | Light |
| --- | --- | --- |
| `SystemFillColorSuccess` | `#6CCB5F` | `#0F7B0F` |
| `SystemFillColorCaution` | `#FCE100` | `#9D5D00` |
| `SystemFillColorCritical` | `#FF99A4` | `#C42B1C` |
| `SystemFillColorSuccessBackground` | `#393D1B` | `#DFF6DD` |
| `SystemFillColorCautionBackground` | `#433519` | `#FFF4CE` |
| `SystemFillColorCriticalBackground` | `#442726` | `#FDE7E9` |

### Per-Control Metrics

These come from the modern `*_themeresources.xaml` files, with shared tokens from `generic.xaml`.

**Focus visual.** Two concentric rings: a 2px outer ring in `FocusStrokeColorOuter` and a 1px inner ring in `FocusStrokeColorInner`, so the ring stays visible against any backdrop. libwinui's `Theme` has `focus_ring_outer_width` = 2, `focus_ring_inner_width` = 1 and `focus_ring_margin` = 3 (the outward extent), drawn by `draw_focus_ring()` in `include/winui/render/FocusRing.hpp`.

**Button.**

| Property | Value |
| --- | --- |
| `ButtonPadding` | `11,5,11,6` (5 top, 6 bottom) |
| Corner radius | 4 |
| Border thickness | 1 (legacy `generic.xaml` says 2) |
| Font | 14, Normal |
| `BackgroundSizing` | `InnerBorderEdge` |
| Alignment | Left, Center |
| `FocusVisualMargin` | -3 |
| Background transition | `BrushTransition`, 83 ms |
| Template | a single `ContentPresenter`, no wrapping Grid or Border |

| State | Background | Border | Foreground |
| --- | --- | --- | --- |
| Normal | `ControlFillColorDefault` | `ControlElevationBorderBrush` | `TextFillColorPrimary` |
| PointerOver | `ControlFillColorSecondary` | `ControlElevationBorderBrush` | `TextFillColorPrimary` |
| Pressed | `ControlFillColorTertiary` | `ControlStrokeColorDefault` | `TextFillColorSecondary` |
| Disabled | `ControlFillColorDisabled` | `ControlStrokeColorDefault` | `TextFillColorDisabled` |

`AccentButtonStyle` swaps in the accent fills, uses `AccentControlElevationBorderBrush` at rest and hover, and has **no border at all** when pressed or disabled. Its pressed foreground dims to `TextOnAccentFillColorSecondary`, and the border does not change on hover. There is also a borderless subtle family built on `SubtleFillColor*`, which is what toolbar, command-bar and menu buttons use. In a real Fluent app most buttons are subtle or neutral, with at most one accent button per view.

libwinui's `ButtonVariant` (`include/winui/ui/input/Button.hpp`) has `Neutral` (the default), `Accent`, `Subtle` and `Hyperlink`, and `ButtonControl::style_visual()` resolves the table above for each.

**CheckBox.** The box is 20 × 20 with corner radius 4 and a 1px border, the glyph 12, the row at least 32 high and 120 wide, content offset by `CheckBoxPadding` `8,5,0,0`, focus margin `-7,-3,-7,-3`. The glyphs are U+E73E (checked) and U+E9AE (indeterminate) from Segoe Fluent Icons. The unchecked box uses the `ControlAltFill` ramp, a slightly recessed surface, not `ControlFill`; the checked box uses the accent ramp. The modern template animates the check mark with a Lottie `AnimatedIcon` and falls back to the font glyph.

**RadioButton.** A 20 × 20 outer ellipse with a 1px border and an inner dot that changes size with state: **12** at rest, **14** on hover, **10** when pressed. That grow-and-shrink is a large part of why Fluent radio buttons feel responsive. The template stacks three ellipses: the unchecked ring, the accent-filled checked ring (toggled by opacity) and the dot. libwinui's `ControlChrome::radio_dot` carries the three sizes.

**ToggleSwitch.**

| Property | Value |
| --- | --- |
| Track | 40 × 20, radius 10 |
| Track stroke | 1; on-state stroke 0 (dark) or 1 (light) |
| Knob | 12 × 12 at rest, 14 × 14 on hover, 17 × 14 when pressed |
| Knob travel | 20 |
| Gap to label | 12 |
| Minimum width | 154 |
| Header margin | `0,0,0,4` |

The pressed knob stretches asymmetrically: the template swaps `HorizontalAlignment` and `Margin` so the knob stays anchored to the edge it rests on and stretches toward the other. The off and on tracks are two stacked rectangles cross-faded by opacity, and so are the two knobs. "Stack both appearances and animate opacity" is the pattern throughout the modern templates, and it suits a compositor well. libwinui's `ControlChrome::toggle_knob` holds `rest` 12, `hover` 14, `pressed` 17 × 14 and `travel` 20, independent of the track size.

**Slider.** The control is 32 high (horizontal) or wide (vertical), the track 4 thick with radius 2 (2 in the legacy style), the thumb 18 × 18 with an inner 12 × 12 circle, content margins 14, outside tick bars 4.

**Text controls.** Minimum 32 high and 64 wide, header margin `0,0,0,8`, inner button margin `0,4,4,4`, icon size 12. The background opacity goes 0.4 at rest, 0.6 on hover, 1.0 when focused, and the focused fill is `ControlFillColorInputActive`. The focused TextBox draws a 2px accent underline, which needs per-side border thickness. libwinui's `TextBox` draws that underline as part of its chrome.

**Lists, menus, tooltips and dialogs.**

| Token | Value |
| --- | --- |
| `ListViewItemMinHeight` / `MinWidth` | 40 / 88 |
| `ListViewItemCornerRadius` | 4 |
| `ListViewItemSelectionIndicatorCornerRadius` | 1.5 |
| `MenuFlyoutItemThemePadding` | `11,8,11,9` |
| `MenuFlyoutItemMargin` | `4,2,4,2` |
| `MenuFlyoutThemeMinHeight` | 32 |
| `MenuFlyoutPresenterThemePadding` | `0,2,0,2` |
| `ToolTipBorderPadding` | `9,6,9,8` |
| `ToolTipContentThemeFontSize` | 12 |
| `ToolTipMaxWidth` | 320 |
| `FlyoutThemeMaxWidth` / `MaxHeight` | 456 / 758 |
| `ContentDialogMinWidth` / `MaxWidth` | 320 / 548 |
| `ContentDialogPadding` | `24,18,24,24` |

A selected list item is a rounded fill with radius 4 plus a narrow accent pill at the left edge with radius 1.5. It is one of the most recognizable Windows 11 details and costs almost nothing to draw.

## Layout

WinUI lays out in two passes plus one[^layout]. Measure hands each child an available size and gets a desired size back. Arrange places each child in its final rect. A third pass, effective-viewport computation, was added for scrollers and feeds `EffectiveViewportChanged`, which virtualization depends on.

- `CFrameworkElement::MeasureCore` wraps the author's `MeasureOverride` with the sizing pipeline: explicit Width/Height, the `MinWidth ≤ Width ≤ MaxWidth` coercion, and `Margin`. `ArrangeCore` does the same around `ArrangeOverride` and applies alignment.
- `MaxLayoutDepth` is 250 on both passes; deeper trees fail.
- `CUIElement` keeps two kinds of dirty flag per pass: "I am dirty" and "a descendant of mine is dirty" (`LF_MEASURE_DIRTY` against `LF_ON_MEASURE_DIRTY_PATH`, and their arrange twins). That lets the layout walk descend only along dirty spines.
- Invalidation is declarative. Properties are annotated with `AffectsMeasure` or `AffectsArrange` in the code-generation model, so setting one invalidates automatically without a hand-written call in every setter.
- Dirtying is asynchronous: it requests a UI-thread tick. `UpdateLayout()` forces a synchronous pass and is described as taboo because it invites layout cycles.
- `UseLayoutRounding` snaps an element and its subtree to whole device pixels: at 125 % a width of 100.1 becomes 125 physical pixels, not 125.125.

libwinui's `Element::measure`/`arrange` is the same two-pass contract, and `DirtyFlags` (`include/winui/core/DirtyFlags.hpp`) marks `Measure`, `Arrange` and `Visual` and bubbles the bits to every ancestor. It bubbles a union, where XAML separates "self" from "on the path"; at the tree sizes libwinui handles, the union is enough. Every element has WinUI's sizing pipeline (margin, explicit and min/max size, horizontal and vertical alignment), because matching Fluent's metrics (`MinWidth` 120 on CheckBox, `MinHeight` 32 on text controls) is not possible without it. libwinui adds one thing WinUI does not have: a parallel measure pass over a frozen `LayoutSnapshot` (`src/ui/LayoutSnapshot.hpp`). The [Layout](../../contributing/how-it-works/layout.md) page covers the details.

## Rendering and Composition

The rendering design notes[^rendering] tell a history that explains the shape of the code: a Silverlight software rasterizer, then Primitive Composition on its own GPU thread in Windows 8, then DirectComposition in Windows 8.1, then `Windows.UI.Composition` from the Anniversary Update, and finally `Microsoft.UI.Composition`, lifted out of the OS for WinUI 3.

The central fact is that XAML does not render pixels to the screen. It builds a tree of composition visuals, and the system compositor (DWM) renders it as part of the whole desktop.

- XAML keeps a *CompNode* tree, a sparse projection of the element tree. An element gets a composition visual only where it needs one: a clip, a transform, opacity, an independent animation, a backdrop, media. The visual tree is coarser than the element tree.
- **Independent animations** run in the compositor with no UI-thread involvement, which is why a hover fade survives a busy UI thread.
- Direct2D and Direct3D are used for things that need real bitmaps (text, images, shapes), with surfaces atlased through a surface factory and device-lost handling.
- **Mica and desktop acrylic** cannot be done by the lifted in-process compositor at all, because it cannot sample other processes' pixels or the wallpaper[^mica-notes]. They need the system compositor, reached through `SystemBackdropController`. In-app acrylic, which blurs content inside the same window, can be done in-process.

libwinui renders differently: controls emit a render-command stream and Direct2D rasterizes it into the window (see [Rendering](../../contributing/how-it-works/rendering.md)). Mica is still reachable, because DWM can paint the system backdrop behind a client area that libwinui leaves unpainted. `Window::set_backdrop()` takes a `Backdrop` (`None`, `Mica`, `MicaAlt`, `Acrylic`, in `include/winui/ui/WindowStyle.hpp`), and the Win32 host applies it with `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)`[^backdrop], which needs Windows 11 22H2 or later. Mica together with the translucent `ControlFillColor*` ramp is a large part of what reads as "Windows 11".

For the small set of animations that should survive a busy UI thread (the hover wash and knob motion on buttons and toggles), libwinui has a `Compositor` seam backed by DirectComposition. The [animations study](winui-animations.md) describes it.

## Input, Focus and Accessibility

**Focus visuals** are drawn by the framework, not by templates. `FocusRectManager.cpp` builds the two-ring rect from `FocusVisualPrimaryThickness` (2) and `FocusVisualSecondaryThickness` (1), places it by `FocusVisualMargin` (usually negative, so outside the control), and clips it to an element marked `IsTemplateFocusTarget` if the template names one. Controls opt in through `UseSystemFocusVisuals`[^focus]. There is also an Xbox-era "reveal" focus glow, off by default.

**Input.** Pointer events are routed and bubble; controls keep `IsPointerOver` and `IsPressed` as properties. `SplitButton` registers for property changes on its two child buttons' `IsPressed` and `IsPointerOver` to derive its own combined state, which is a neat pattern for composite controls. The pointer device type is tracked because touch gets different visual states.

**Accessibility** is not optional in WinUI. Every control ships an automation peer with the right UI Automation pattern, decorative template parts are hidden with `AutomationProperties.AccessibilityView="Raw"`, and names are localized. The lesson from `AccessibilityView` is that the automation tree is a *filtered* view of the visual tree, so a node needs a "content or chrome" bit from the start rather than as a retrofit.

libwinui follows that. Every element carries an `Accessibility` value (`include/winui/ui/Accessibility.hpp`) with a name, a description and a `decorative` flag that keeps a node out of the automation tree. Controls report an `AutomationRole` and the pattern provider interfaces they implement (`include/winui/ui/Automation.hpp`), and the Win32 host exposes them through a UI Automation provider. See [Input and Focus](../../contributing/how-it-works/input-and-focus.md).

## Virtualization

`controls/dev/Repeater/` is WinUI's modern answer, and it is cleanly decomposed[^items-repeater]:

- **`ItemsRepeater`** is a panel-like element with no look, no selection and no scrolling. It only realizes elements for a data source.
- **`Layout`**, split into `VirtualizingLayout` and `NonVirtualizingLayout`, is a separate, pluggable object rather than a panel subclass. `StackLayout`, `UniformGridLayout`, `FlowLayout` and `LinedFlowLayout` are the stock ones.
- **`LayoutContext`** is how a layout asks for elements by index. It never touches a children collection directly.
- **`ElementFactory`** and **`RecyclePool`** own realization and recycling.
- **`ElementManager`** and `FlowLayoutAlgorithm` keep the realized-range bookkeeping, and `BuildTreeScheduler` builds large trees incrementally across frames.
- **`ItemsView`** and **`ItemContainer`** are the newest, lighter, selection-aware layer on top.

libwinui has two virtualizing paths. `VirtualListControl` (`include/winui/ui/collections/VirtualListControl.hpp`) bakes viewport math, an item factory and a fixed item height into one class; `TableView` uses it privately. `ForEach` (`include/winui/ui/collections/ForEach.hpp`) follows WinUI's decomposition: an attachable `Layout` from `include/winui/ui/layouts/Layout.hpp` with a virtualizing and a non-virtualizing context, the four stock layouts (`StackLayout`, `TileLayout`, `WrapLayout`, `LinedFlowLayout`), collection-change notifications and a pinned focused element. `ListView` and `TileView` are built on it. Neither path pools elements: items are created when they come into view and destroyed when they leave, where WinUI hands them to a `RecyclePool`. The [virtual list tutorial](../../tutorials/collections/02-virtual-list.md) shows both paths in use.

## Where the Two Designs Differ

What libwinui deliberately does not have is WinUI's template machinery: `ControlTemplate`, `TemplateBinding`, `TemplateSettings` and re-templating. The customization it offers instead is the lightweight-styling half, by overriding fields of a resolved `Theme` or `Palette`, plus composition through the content model and the imperative `Element` API. The reference's [Coming From WinUI or WPF](../../reference/index.md#coming-from-winui-or-wpf) table maps each WinUI control to its libwinui counterpart.

For reference, the main differences in one place:

| Topic | WinUI | libwinui |
| --- | --- | --- |
| Markup | XAML, a compiler, a parser, a type system, WinMD activation | none; plain C++ aggregates and constructors |
| Property system | `DependencyProperty` with metadata, value-source layering and generated code | plain members with setters, `Property<T>` for bindings |
| Object model | DependencyObject → UIElement → FrameworkElement → Control → ... | one flat `Element` interface |
| Ownership | ref-counted COM plus a reference tracker to break cycles | `unique_ptr` single owner, raw observers; cycles cannot form |
| Threading | UI thread plus compositor thread; layout is UI-thread only | UI thread owns the tree, checked in debug builds; `Window::layout()` measures large batches of independent siblings on workers |
| Rendering | builds a composition visual tree that DWM rasterizes | emits render commands that Direct2D rasterizes |
| Tests | API and interaction tests through a real app host | a recorded render-command stream and semantic goldens, no window needed |

The last row matters most for a from-scratch framework. WinUI's control tests need a running app, a desktop session and real input injection. libwinui's controls can be tested from the command stream alone (see [Rendering](../../contributing/how-it-works/rendering.md)), and there is nothing in the WinUI repository to copy on that front.

## Sources

[^repo]: `microsoft/microsoft-ui-xaml`, MIT license. <https://github.com/microsoft/microsoft-ui-xaml>. Token files: `controls/dev/CommonStyles/Common_themeresources_any.xaml`, `CornerRadius_themeresources.xaml`, `TextBlock_themeresources.xaml` and the per-control `*_themeresources.xaml` / `*_themeresources_perf2026.xaml`; system styles: `dxaml/xcp/dxaml/themes/generic.xaml`.
[^color]: Microsoft Learn, "Color in Windows". <https://learn.microsoft.com/windows/apps/design/style/color>
[^geometry]: Microsoft Learn, "Geometry in Windows 11". <https://learn.microsoft.com/windows/apps/design/signature-experiences/geometry>
[^typography]: Microsoft Learn, "Typography in Windows". <https://learn.microsoft.com/windows/apps/design/signature-experiences/typography>
[^control-overview]: `microsoft-ui-xaml`, `docs/design-notes/control-overview.md`.
[^styling-guide]: `microsoft-ui-xaml`, `docs/design-notes/xaml-styling-guide.md`.
[^authoring]: `microsoft-ui-xaml`, `docs/how-to-author-a-xaml-control.md`.
[^resources]: `microsoft-ui-xaml`, `docs/design-notes/resources.md` and `docs/design-notes/styles.md`.
[^theme-resources]: Microsoft Learn, "XAML theme resources". <https://learn.microsoft.com/windows/apps/design/style/xaml-theme-resources>
[^xaml-styles]: Microsoft Learn, "XAML styles", section on lightweight styling. <https://learn.microsoft.com/windows/apps/design/style/xaml-styles>
[^layout]: `microsoft-ui-xaml`, `docs/design-notes/Layout-overview.md`, and `dxaml/xcp/core/layout/`.
[^rendering]: `microsoft-ui-xaml`, `docs/design-notes/rendering.md`.
[^mica-notes]: `microsoft-ui-xaml`, `docs/design-notes/mica-desktop-acrylic.md`; Microsoft Learn, "Mica material". <https://learn.microsoft.com/windows/apps/design/style/mica>
[^backdrop]: Microsoft Learn, `DWM_SYSTEMBACKDROP_TYPE` enumeration. <https://learn.microsoft.com/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type>
[^focus]: `microsoft-ui-xaml`, `docs/design-notes/focus.md` and `dxaml/xcp/components/FocusRect/FocusRectManager.cpp`; Microsoft Learn, "Focus navigation without a mouse", focus visuals. <https://learn.microsoft.com/windows/apps/design/input/focus-navigation>
[^items-repeater]: `microsoft-ui-xaml`, `controls/dev/Repeater/`; Microsoft Learn, "ItemsRepeater". <https://learn.microsoft.com/windows/apps/design/controls/items-repeater>
