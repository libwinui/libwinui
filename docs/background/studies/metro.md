# Metro

What Metro was, mechanically, and why a role-based palette cannot express it. This study reads the Windows 8 and 8.1 SDKs' own theme XAML, the Windows Phone SDKs and the Windows 8 project templates, and works out which parts of the look are color, which are type and which are page geometry. The short answer is that the colors are the easy part.

For the era's measured values and full control inventory, see the [Metro (MDL) page](../themes/mdl.md).

## What Metro Was

Metro was a design language that had shipped several times before Windows 8, and the Windows 8 XAML control set is a late and somewhat compromised expression of it:

- **Encarta 95 and the MSN 2.0 kiosk UI** (mid-1990s) are the acknowledged ancestors: transit-signage wayfinding, big type, no ornament.
- **Zune (2006)** and **Windows Media Center** were the first consumer outing: giant Segoe headers running off the screen edge, lowercase pivot titles, panoramic horizontal scrolling.
- **Windows Phone 7 (2010)** was the first complete system: a black or white ground, one flat saturated accent, tiles, and no chrome.
- **Windows 8 (2012)** applied it to a desktop OS. That is where it got a control set, and with it compromises: real forms need check boxes and combo boxes, and those had to be designed in a language built to avoid them.
- **Windows 8.1 (2013)** softened it: some Light weights were pulled back, some chrome came back, and the accent became freely user-selectable.

Microsoft dropped the name "Metro" around August 2012 after a trademark dispute with the German retailer Metro AG, and the official names became "Modern UI", "Windows Store app design" and "Microsoft design language".[^name] Everyone kept calling it Metro, and so does this page.

The stated principles were five: *show pride in craftsmanship*, *do more with less*, *be fast and fluid*, *win as one*, and *authentically digital*. Two of them turn into rendering rules:

- **Content before chrome** (from *do more with less*). Push the ratio of content pixels to framing pixels as far as it goes. In practice: delete borders, backgrounds and separators, and let whitespace and type weight do their job.
- **Authentically digital.** No skeuomorphism and no fake third dimension. In practice: no gradients, bevels, drop shadows, rounded corners, textures or glass.

## Seven Mechanisms

### 1. Typography Does the Work Borders Used to Do

Metro's hierarchy is carried almost entirely by font size and weight, on a ramp with very large jumps. From `StandardStyles.xaml`, the style file every Windows 8 project template shipped:[^standardstyles]

| Style | Size (px) | Size (pt at 96 DPI) | Weight | `LineHeight` | Baseline nudge |
| --- | --- | --- | --- | --- | --- |
| `HeaderTextStyle` | 56 | 42 | Light | 40 | `Translate(-2, 8)` |
| `SubheaderTextStyle` | 26.667 | 20 | Light | 30 | `Translate(-1, 6)` |
| `TitleTextStyle` | 14.667 | 11 | SemiBold | 20 | `Translate(-1, 4)` |
| `ItemTextStyle` | 14.667 | 11 | Normal | 20 | `Translate(-1, 4)` |
| `BodyTextStyle` | 14.667 | 11 | SemiLight | 20 | `Translate(-1, 4)` |
| `CaptionTextStyle` | 12 | 9 | Normal, secondary color | 20 | `Translate(-1, 4)` |

Four things follow from this table:

1. **The ramp is 42 / 20 / 11 / 9 pt**, with nothing between 20 and 11. Hierarchy comes from discontinuity, not from a smooth scale.
2. **Weight is its own axis.** Header and Subheader are Light, Body is SemiLight, Title is SemiBold. Windows 8 shipped a new Segoe UI Semilight face for this, and retuned Segoe UI for legibility at these weights.[^segoe] A 56 px header at Regular weight reads as a different design language, so a text style with a `bool bold` cannot render Metro.
3. **Line height is explicit and quantized.** `LineHeight="20"` with `LineStackingStrategy="BlockLineHeight"` puts every body-size run into exactly one 20 px cell, whatever the font's own metrics say. Header and Subheader override it to 40 and 30.
4. **The `TranslateTransform` is a baseline correction.** `LineHeight` positions the line box, and the nudge moves the glyphs so the baseline, not the box, lands on the grid. A faithful implementation needs either baseline alignment or the same per-style offset.

### 2. Chrome Is Deleted, and What Is Left Is a Hairline

The 8.1 `generic.xaml` builds the same `Button` **differently in light and dark**:[^sdk81]

| Token | Dark (`Default`) | Light |
| --- | --- | --- |
| `ButtonBackgroundThemeBrush` | `Transparent` | `#B3B6B6B6` |
| `ButtonBorderThemeBrush` | `#FFFFFFFF` | `#33000000` |
| `ButtonBorderThemeThickness` | 2 | 0 |

In dark theme a button is nothing but a 2 px white rectangle around text, with no fill. In light theme it is a flat 70 % gray fill with no border at all. That is two constructions sharing one template, selected by a thickness token, not a color swap. The pattern repeats: check box and radio button thickness is 0 in dark and 2 in light, combo box is 2 in both, hyperlink button 2 in dark and 0 in light. **Border width cannot be a theme-wide setting**; it has to live on the per-state visual.

The pressed state is the other signature move: **pressed inverts**. Dark pressed is a white fill with black text (`ButtonPressedBackgroundThemeBrush #FFFFFFFF`, `ButtonPressedForegroundThemeBrush #FF000000`), light pressed is black with white text. There is no "slightly darker" state anywhere. Hover is a subtle wash (`#21FFFFFF` dark, `#D1CDCDCD` light) and press is a hard, instant polarity flip.

### 3. Color Is Flat, Saturated and Unmixed

- **No gradients.** The control templates use `SolidColorBrush` throughout.
- **No rounded corners.** No `Rectangle` in the templates sets `RadiusX` or `RadiusY`.
- **Absolute backgrounds.** `ApplicationPageBackgroundThemeBrush` is `#FFFFFFFF` in light and `#FF1D1D1D` in dark, in both 8.0 and 8.1. Dark is not pure black; pure black is the Windows Phone page (`PhoneBackgroundColor #FF000000`).
- **Foregrounds are alpha over the ground**, not mixed grays: `#DE` (87 %) for primary text, `#99` (60 %) for secondary, `#66` (40 %) for pressed and disabled. The whole opacity vocabulary of the 8.1 dictionaries is `0xDE`, `0xCC`, `0x99`, `0x66`, `0x45`, `0x33`, `0x21` and `0x1A`.
- **The accent is one flat saturated hue.** The 8.1 XAML default is `#FF4617B4`, a deep violet, used verbatim for `ListViewItemSelectedBackgroundThemeBrush`, `ComboBoxItemSelectedBackgroundThemeBrush` and others, with a hover rung of `#FF5F37BE`. Hyperlinks are a related violet: `#FF4F1ACB` light, `#FF9C72FF` dark.

The accent palette users actually remember is a fixed set of named hues. The Windows Phone 8.0 SDK holds twenty, one `PhoneAccentColor` per file:[^wp8]

| Name | Value | Name | Value | Name | Value | Name | Value |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Lime | `#A4C400` | Cobalt | `#0050EF` | Crimson | `#A20025` | Brown | `#825A2C` |
| Green | `#60A917` | Indigo | `#6A00FF` | Red | `#E51400` | Olive | `#6D8764` |
| Emerald | `#008A00` | Violet | `#AA00FF` | Orange | `#FA6800` | Steel | `#647687` |
| Teal | `#00ABA9` | Pink | `#F472D0` | Amber | `#F0A30A` | Mauve | `#76608A` |
| Cyan | `#1BA1E2` | Magenta | `#D80073` | Yellow | `#E3C800` | Taupe | `#87794E` |

Windows Phone 7.1 had ten: blue `#1BA1E2`, brown `#A05000`, green `#339933`, lime `#A2C139`, magenta `#D80073`, orange `#F09609`, pink `#E671B8`, purple `#A200FF`, red `#E51400` and viridian `#00ABA9`. The Windows 8 Start-screen palette was chosen by the OS and no XAML on disk records it. All of these are used undiluted: where Fluent picks a lighter or darker ramp rung so a filled surface stands out, Metro uses the accent as it is.

### 4. Iconography Is a Font, and the App Bar Button Is a Ring

Icons are glyphs from **Segoe UI Symbol** (`SymbolThemeFontFamily`), drawn as text. The app bar button is two stacked glyphs plus the command's own: `U+E0A8` (filled circle, background) behind `U+E0A7` (circle outline), both at 53.333 px in a 40×40 box, with the command glyph centered on top at 20 px. Pressed hides the outline and fills the circle. That is the whole visual: a ring, a glyph, and a 12 px caption underneath. Back navigation is also a glyph (`U+E071`, `U+E0BA` when snapped, `U+E0A6` in high contrast) on a circular `U+E0D4` background.

### 5. Indicators Are Literal Shapes at Odd Sizes

The check box is a 21×21 `Rectangle`. The radio button is a 23×23 `Ellipse` with a 13×13 dot. The toggle switch is a 19 px tall rectangular track with a 12 px wide rectangular knob and a 44 px "curtain" that slides in behind it. Each is a plain Euclidean shape at a deliberately unscaled pixel size.

### 6. Focus Is a Black-and-White Dashed Pair

Every focusable control draws two overlapping dashed rectangles:

```xml
<Rectangle x:Name="FocusVisualWhite" Stroke="{ThemeResource FocusVisualWhiteStrokeThemeBrush}"
           StrokeEndLineCap="Square" StrokeDashArray="1,1" StrokeDashOffset="1.5" />
<Rectangle x:Name="FocusVisualBlack" Stroke="{ThemeResource FocusVisualBlackStrokeThemeBrush}"
           StrokeEndLineCap="Square" StrokeDashArray="1,1" StrokeDashOffset="0.5" />
```

`StrokeDashArray="1,1"` is one pixel on, one off, and the two rectangles are offset by exactly one dash, so the white dashes land in the black one's gaps. The result is an alternating black and white dotted ring that stays visible on any background. It is Metro's answer to the problem Fluent solves with two concentric solid rings, and a third focus visual next to the classic single-color dotted rect and Fluent's rings.

### 7. What Metro Does Not Have

| Absent | Consequence for a renderer |
| --- | --- |
| Gradients | Gradient fills and strokes go unused |
| Rounded corners | Corner radius 0 throughout |
| Bevels and 3-D edges | Raised and sunken borders go unused |
| Drop shadows, glass, blur | No shadow, no translucency in chrome |
| Elevation borders | No gradient stroke |
| Tonal accent ramps | The accent is used raw |
| Intermediate hover shades | Hover is a wash, press is an inversion |

Metro needs **fewer** render primitives than a Fluent renderer already has. Its difficulty sits entirely in typography, metrics and page layout.

## The Windows 8.1 Token Tables

The values below come from the Windows 8.1 SDK's design-time theme XAML, `generic.xaml` and `themeresources.xaml` under `Include/winrt/xaml/design/`, which Visual Studio and Blend resolved 8.1 apps against. They hold `Default` (dark), `HighContrast` and `Light` dictionaries of 448, 449 and 448 keys.[^sdk81]

!!! note "A Misleading Copy"
    The WinUI repository carries an 8.1-shaped resource-loading test fixture, `genericincomplete.xaml`. It is a reduced pre-release copy: it lacks about 96 theme keys per dictionary and the default `Button` style, and some of its values differ from the shipped SDK (the app bar brushes are 8.0's, `FlyoutThemeMaxWidth`/`Height` are 510/758 against the SDK's 450/718). Its sibling `genericred.xaml` sets the dark `ButtonBackgroundThemeBrush` to `Red` on purpose as a test tamper. Neither is a value source.

### Shared Metrics

Identical in all three dictionaries:

```text
ContentControlThemeFontFamily      Segoe UI
SymbolThemeFontFamily              Segoe UI Symbol
ControlContentThemeFontSize        14.667      (11 pt)
ToolTipContentThemeFontSize        12          (9 pt)
ComboBoxArrowThemeFontSize         21
AppBarThemeMinHeight               68
TextControlThemeMinHeight          32
TextControlThemeMinWidth           64
TextControlThemePadding            10,3,10,5
TextControlBorderThemeThickness    2
TextControlBackgroundThemeOpacity  0.8
TextControlBorderThemeOpacity      0.8
FlyoutContentThemePadding          20,17,20,20
FlyoutBorderThemeThickness         2
MenuFlyoutItemThemePadding         20,10,20,12
MenuFlyoutPresenterThemePadding    0,5
AppBarTopBorderThemeThickness      0,0,0,2      (bottom edge only)
AppBarBottomBorderThemeThickness   0,2,0,0      (top edge only)
ListViewItemSelectedBorderThemeThickness  4
GridViewItemSelectedBorderThemeThickness  4
ListViewItemReorderHintThemeOffset        40.0
ListViewItemDisabledThemeOpacity          0.55
SliderTrackThemeHeight             11
ComboBoxBorderThemeThickness       2
```

Border thickness per dictionary:

| Token | Dark | HighContrast | Light |
| --- | --- | --- | --- |
| `ButtonBorderThemeThickness` | 2 | 2 | 0 |
| `CheckBoxBorderThemeThickness` | 0 | 2 | 2 |
| `RadioButtonBorderThemeThickness` | 0 | 2 | 2 |
| `HyperlinkButtonBorderThemeThickness` | 2 | 2 | 0 |
| `RepeatButtonBorderThemeThickness` | 2 | 2 | 0 |
| `ToggleButtonBorderThemeThickness` | 2 | 2 | 2 |

### Dark (`Default`) Brushes

```text
ApplicationPageBackgroundThemeBrush        #FF1D1D1D
ApplicationForegroundThemeBrush            #DEFFFFFF
ApplicationSecondaryForegroundThemeBrush   #99FFFFFF
ApplicationHeaderForegroundThemeBrush      #FFFFFFFF
ApplicationPointerOverForegroundThemeBrush #CCFFFFFF
ApplicationPressedForegroundThemeBrush     #66FFFFFF

ButtonBackgroundThemeBrush                 Transparent
ButtonForegroundThemeBrush                 #FFFFFFFF
ButtonBorderThemeBrush                     #FFFFFFFF
ButtonPointerOverBackgroundThemeBrush      #21FFFFFF
ButtonPointerOverForegroundThemeBrush      #FFFFFFFF
ButtonPressedBackgroundThemeBrush          #FFFFFFFF
ButtonPressedForegroundThemeBrush          #FF000000
ButtonDisabledBackgroundThemeBrush         Transparent
ButtonDisabledBorderThemeBrush             #66FFFFFF
ButtonDisabledForegroundThemeBrush         #66FFFFFF

CheckBoxBackgroundThemeBrush               #CCFFFFFF
CheckBoxBorderThemeBrush                   #CCFFFFFF
CheckBoxForegroundThemeBrush               #FF000000   (the glyph, on the light box)
CheckBoxPointerOverBackgroundThemeBrush    #DEFFFFFF
CheckBoxPressedBackgroundThemeBrush        #FFFFFFFF
CheckBoxDisabledBackgroundThemeBrush       #66FFFFFF
CheckBoxContentForegroundThemeBrush        #FFFFFFFF

RadioButtonBackgroundThemeBrush            #CCFFFFFF

ToggleSwitchCurtainBackgroundThemeBrush    #FF5729C1   (accent)
ToggleSwitchCurtainPointerOverBackground   #FF6E46CA
ToggleSwitchCurtainPressedBackground       #FF7E4FEC
ToggleSwitchTrackBackgroundThemeBrush      #42FFFFFF
ToggleSwitchOuterBorderBorderThemeBrush    #59FFFFFF
ToggleSwitchThumbBackgroundThemeBrush      #FFFFFFFF
ToggleSwitchThumbDisabledBackgroundThemeBrush #FF7E7E7E

FocusVisualWhiteStrokeThemeBrush           White
FocusVisualBlackStrokeThemeBrush           Black

AppBarBackgroundThemeBrush                 #FF000000   (8.0: #E5000000)
AppBarItemForegroundThemeBrush             #FFFFFFFF
AppBarItemPointerOverBackgroundThemeBrush  #21FFFFFF
AppBarItemPressedForegroundThemeBrush      #FF000000
AppBarItemDisabledForegroundThemeBrush     #66FFFFFF

HyperlinkForegroundThemeBrush              #FF9C72FF
ToolTipBackgroundThemeBrush                #FFFFFFFF
ToolTipForegroundThemeBrush                #FF666666
ToolTipBorderThemeBrush                    #FF808080
```

### Light Brushes

```text
ApplicationPageBackgroundThemeBrush        #FFFFFFFF
ApplicationForegroundThemeBrush            #DE000000
ApplicationSecondaryForegroundThemeBrush   #99000000
ApplicationHeaderForegroundThemeBrush      #FF000000
ApplicationPointerOverForegroundThemeBrush #CC000000
ApplicationPressedForegroundThemeBrush     #66000000

ButtonBackgroundThemeBrush                 #B3B6B6B6
ButtonForegroundThemeBrush                 #FF000000
ButtonBorderThemeBrush                     #33000000   (thickness 0, so unused)
ButtonPointerOverBackgroundThemeBrush      #D1CDCDCD
ButtonPointerOverForegroundThemeBrush      #FF000000
ButtonPressedBackgroundThemeBrush          #FF000000
ButtonPressedForegroundThemeBrush          #FFFFFFFF
ButtonDisabledBackgroundThemeBrush         #66CACACA
ButtonDisabledBorderThemeBrush             #1A000000
ButtonDisabledForegroundThemeBrush         #66000000

CheckBoxBackgroundThemeBrush               #CCFFFFFF
CheckBoxBorderThemeBrush                   #45000000
CheckBoxPointerOverBorderThemeBrush        #70000000
CheckBoxPressedBackgroundThemeBrush        #FF000000
CheckBoxPressedForegroundThemeBrush        #FFFFFFFF
CheckBoxForegroundThemeBrush               #FF000000
CheckBoxContentForegroundThemeBrush        #FF000000

RadioButtonBorderThemeBrush                #45000000

ListViewItemSelectedBackgroundThemeBrush   #FF4617B4   (accent)
ListViewItemSelectedPointerOverBackground  #FF5F37BE
ListViewItemPointerOverBackgroundThemeBrush #4D000000
ListViewItemOverlayBackgroundThemeBrush    #A6000000
ListViewItemOverlayForegroundThemeBrush    #FFFFFFFF
ListViewItemOverlaySecondaryForeground     #99FFFFFF
ListViewItemPlaceholderBackgroundThemeBrush #FF3D3D3D
ListViewItemFocusBorderThemeBrush          #FF000000

ComboBoxItemSelectedBackgroundThemeBrush   #FF4617B4
ComboBoxItemPointerOverBackgroundThemeBrush #21000000
ComboBoxItemPressedBackgroundThemeBrush    #FFD3D3D3

HyperlinkForegroundThemeBrush              #FF4F1ACB
HyperlinkPointerOverForegroundThemeBrush   #CC4F1ACB
HyperlinkPressedForegroundThemeBrush       #994F1ACB
```

### Per-Control Metrics

From the 8.1 control templates:

| Control | Metrics |
| --- | --- |
| Button | `Padding` `12,4,12,4` (8.0 and 8.1; `RepeatButton`, `ToggleButton` and `HyperlinkButton` use `12,4,12,5`). SemiBold, 14.667 px. One `Border` around a `ContentPresenter` with `Margin="3"`, so the focus rects sit 3 px outside the visible button. |
| CheckBox | `Padding` `2,3,0,0`; content column at 27 px. Box 21×21 with `UseLayoutRounding="False"`. The check is a vector `Path` stretched to 16×14, not a font glyph; indeterminate is a 9×9 square. Focus rects 27×27. |
| RadioButton | `Padding` `1,4,0,0`. Ring 23×23, dot 13×13 with no per-state resize. Focus rects 29×29. |
| ToggleSwitch | `MinWidth` 154, SemiBold. Knob bounds 19 px tall at `Margin="13,5,13,16"`. Outer border 2, inner border 1. A 44 px accent curtain slides from `X=-44` to 0; the knob is 12 px wide. |
| TextBox | `MinHeight` 32, `MinWidth` 64, `Padding` `10,3,10,5`, border 2, background and border at 0.8 opacity rising to 0.87 on hover. |
| ListView / GridView item | Selected border 4 px in the accent; disabled opacity 0.55; drag opacity 0.60; reorder offset 40. `GridViewItem` `Margin="0,0,2,2"` plus the template's 4 px focus inset gives the documented 10 px gap between 250×250 tiles. |
| AppBar | Minimum height 68; a 2 px border on one edge only; background `#FF000000` dark and `#FFF0F0F0` light in 8.1. `AppBarButtonStyle` is 100 px wide with a 40×40 glyph box and a 12 px caption, so a labeled app bar lands at about 88 px. |

`StandardStyles.xaml` also shipped the item templates (`Standard250x250ItemTemplate`, `Standard500x130`, `Standard130`, `Standard80`, `StandardSmallIcon300x70`, `StandardSmallIcon70`). A tile title uses `TitleTextStyle` at `Height="60"` over an `#A6000000` overlay band.

### Windows 8.0 Against 8.1

The 8.0 SDK's pair of files has 309/310/309 theme keys.[^sdk80] Every 8.0 key survives into 8.1, and a key-by-key diff finds exactly two value changes, both in the app bar: `AppBarBackgroundThemeBrush` and `AppBarBorderThemeBrush` go from `#E5000000` to `#FF000000` in dark and from `#FFFFFFFF` to `#FFF0F0F0` in light. `HighContrast` only switched its references from `StaticResource` to `ThemeResource`. The page background (`#FF1D1D1D`) and the default accent (`#FF4617B4`) are the same in both. 8.1 adds 139 keys for its new controls (Flyout, MenuFlyout, SearchBox, Hub, SettingsFlyout, DatePicker and others). So one variant built from the 8.1 tables covers both releases, with a two-brush delta for 8.0.

## The 20px Grid

Metro pages sit on a **20×20 px unit grid**, with content starting six units from the left (120 px) and seven from the top (140 px). The unit size comes from design guidance; the pixel values are confirmed by real Windows 8 pages:[^grid]

```xml
<Grid.RowDefinitions>
    <RowDefinition Height="140"/>   <!-- header band -->
    <RowDefinition Height="*"/>
</Grid.RowDefinitions>
<GridView Padding="116,136,116,46" Grid.RowSpan="2" />
```

The `116` and `136` are 120 and 140 minus the 4 px focus inset each item carries, and `StandardStyles.xaml` says so in a comment ("Default to 10-pixel spacing between grid items (after accounting for 4-pixel insets for focus)"). Portrait relaxes to `96,136,86,56`, and the snapped view drops to a `10,0,0,60` list.

| Silhouette | Value |
| --- | --- |
| Grid unit | 20 px |
| Left content margin | 120 px (6 units) |
| Top content margin | 140 px (7 units), the page header band |
| Bottom margin | 50 px (46 px plus the 4 px focus inset) |
| Tile | 250×250 with a 10 px gutter |
| App bar | 68 px minimum, about 88 px with labels |
| Page header | 56 px Segoe UI Light inside the 140 px band |

This is the part of Metro that is not a color table. An app with the right palette and the wrong margins does not look like Windows 8. An app with the right margins and type ramp looks like Windows 8 even in grayscale.

## Motion

Metro's "fast and fluid" is a small fixed vocabulary from the Windows animation library, exposed in XAML as theme transitions and theme animations:[^motion]

- **`EntranceThemeTransition`**: the page-load motion, content translating in from an offset while fading up. `LayoutRootStyle` in `StandardStyles.xaml` sets exactly this and nothing else. The default is a right-to-left entrance.
- **`PointerDownThemeAnimation` / `PointerUpThemeAnimation`**: the "tilt". A press scales the element down slightly and, on tiles, tilts it toward the touch point. The duration is preconfigured, and setting `Duration` is documented to have no effect, so every app's press feels the same.
- **`FadeInThemeAnimation` / `FadeOutThemeAnimation`**: scroll bar and indicator reveal throughout `generic.xaml`.
- **`RepositionThemeTransition`, `AddDeleteThemeTransition`, `ReorderThemeTransition`**: list reflow.

Control state changes themselves take no time at all. In the 8.1 `generic.xaml`, 184 of the state keyframes are zero-length `DiscreteObjectKeyFrame KeyTime="0"` against 13 non-zero ones, and all 13 belong to list item selection hints and the progress bar. Neither SDK defines a duration, spline or easing resource, and the 40 inline `KeySpline`s all belong to the `ProgressBar` dot choreography. There is no era curve for a control transition because no control transition in the era is animated.

## What a Theme System Needs for Metro

Read against a theme model built for Fluent and the classic eras, Metro exposes gaps that have nothing to do with color:

- **Font weight must be a value, not a flag**, with at least Light, SemiLight, Regular, SemiBold and Bold.
- **A theme needs a type ramp**, so a label can say "I am a page header" and get 56 px Light from the era.
- **Line height must be explicit**, or text will not sit on the 20 px grid.
- **Border width and "draw a flat border at all" must live on each visual**, because one theme wants 2 on its combo box and 0 on its button, and flips them between light and dark.
- **Page metrics belong to the theme**: grid unit, content margin, header band, item spacing and command bar height carry the look more than any color does.
- **Focus visuals need a third kind**, the interleaved dashed pair.
- **Era, light/dark and accent must be independent axes.** Windows 8.x shipped real `Light` and `Default` dictionaries and a user accent, so an era model that treats "retro" as "fixed light palette" gets Metro wrong.
- **An accent can have no ramp.** Metro uses the color raw; hover and pressed are separate literals, not offsets.

A palette of role colors, however complete, covers only the third mechanism above. That is why a role-based palette cannot express Metro on its own.

### What libwinui Does Now

libwinui implements Metro as `ThemeVariant::Metro`, one entry for Windows 8 and 8.1 built from the 8.1 tables:

- `MetroPalette` (`ui/themes/MetroPalette.hpp`) transcribes the two dictionaries as `MetroPalette::metro_dark()` and `MetroPalette::metro_light()`, including `button_border_width` (2 dark, 0 light) and `indicator_border_width` (0 dark, 2 light). `Theme::metro(palette, dark)` derives the full `Theme` from it.
- `TextStyle` carries a `FontWeight` (`Light`, `SemiLight`, `Regular`, `SemiBold`, `Bold`) and a `line_height`. `Theme::type` is a `TypeRamp` whose Metro rungs are the table above (56/Light/40, 26.667/Light/30, 14.667/SemiBold/20, 14.667/Regular/20, 14.667/SemiLight/20, 12/Regular/20). `TextControl` resolves its `TextRole` against it, and `ui::Text` takes `.role`.
- `ButtonVisual` has its own `border_width` and `draw_border`, so the dark outline button and the light borderless one come out of the same control.
- `Theme::page` (`PageMetrics`) holds the silhouette: grid unit 20, content margin `{120, 140, 120, 50}`, header height 140, item spacing 10, command bar height 68.
- `FocusVisual::MetroDashedPair` draws the two interleaved 1-on-1-off rectangles.
- `ControlChrome::press_state_flip` makes a press a hard color inversion with no compositor wash, and `anim_faster` and `anim_fast` are zero, matching the era's instant state changes.
- The accent is used raw. It defaults to `#FF4617B4`; an explicit accent override replaces it and derives hover and pressed rungs by lightening and darkening, which is an approximation since the era's own rungs are unrelated literals.
- Chrome asks for square corners and no system backdrop.

Two things are not there. The pointer-down tilt is not implemented; only the color flip is. And icons stay on the default icon font: Metro's commands are Segoe UI Symbol codepoints in `U+E100` to `U+E1E9`, a different range from the Segoe MDL2 and Fluent codepoints libwinui's glyph constants use, and naming the era font without remapping every glyph would draw missing-glyph boxes.

## Limits

- **Weights in the headless measurer.** The deterministic measurer approximates advances and does not model weight. Semantic goldens are stable (they record the requested weight), but pixel output on a real Windows 8 machine would differ.
- **Baselines.** The `LineHeight` plus `BlockLineHeight` plus nudge combination needs baseline-relative positioning. DirectWrite supports it through `SetLineSpacing` with an explicit baseline; a headless measurer has to model it on purpose, or vertical text position diverges slightly between the two backends.
- **The default accent.** `#FF4617B4` is what the XAML shipped, but almost nobody's Windows 8 machine looked like that, since users picked from the Start-screen palette. A faithful theme defaults to the XAML value and honors an app or user accent.
- **How freely users could pick an accent** (a fixed set in 8.0's Start screen, freer in 8.1) is OS behavior and does not appear in any XAML.
- **Scope.** Page metrics widen a theme's remit from "how controls look" to "how pages are proportioned". For era fidelity that is correct, because the era decides both.
- **Tiles.** The 250×250 tile with its `#A6000000` caption band is as much a Metro signature as the button, but it is item-template styling rather than control chrome.

## Sources

Primary, from the SDKs and templates:

- Windows 8.1 SDK, `Include/winrt/xaml/design/generic.xaml` and `themeresources.xaml`: every token and control metric above.
- Windows 8.0 SDK, the same two files: the 8.0 against 8.1 comparison.
- Windows Phone 8.0 SDK, `Design/AccentColors/*.xaml`: the twenty accent colors.
- Windows Phone 7.1 SDK, `Design/*/ThemeResources.xaml`: the ten-color set.
- `Common/StandardStyles.xaml` from the Visual Studio 2012 Windows Store project templates: type ramp, `AppBarButtonStyle`, item templates.

Secondary, for design intent, the grid unit and the accent list:

- [Metro (design language), HandWiki](https://handwiki.org/wiki/Metro_(design_language))
- [Modern UI, Microsoft Wiki](https://microsoft.fandom.com/wiki/Modern_UI)
- [Blink's Guide to Metro Design: Windows 8](http://assets.blinkux.com/metro/win8.html)
- [Five Things to Know When Designing a Windows 8 App, UX Magazine](https://uxmag.com/articles/five-things-to-know-when-designing-a-windows-8-app)
- [Implementing the Layout Guidelines in WinJS, Telerik](https://www.telerik.com/blogs/implementing-the-layout-guidelines-in-winjs)
- [Segoe UI gets a subtle facelift in Windows 8, istartedsomething](https://istartedsomething.com/20120302/segoe-ui-gets-a-subtle-facelift-in-windows-8/)

[^name]: [Metro (design language), HandWiki](https://handwiki.org/wiki/Metro_(design_language)) and [Modern UI, Microsoft Wiki](https://microsoft.fandom.com/wiki/Modern_UI).
[^standardstyles]: `Common/StandardStyles.xaml`, shipped with every Visual Studio 2012 Windows Store app template; many copies survive in the MSDN Code Gallery samples and they agree.
[^segoe]: [Segoe UI gets a subtle facelift in Windows 8, istartedsomething](https://istartedsomething.com/20120302/segoe-ui-gets-a-subtle-facelift-in-windows-8/).
[^sdk81]: Windows Software Development Kit for Windows 8.1, `Windows Kits/8.1/Include/winrt/xaml/design/generic.xaml` (9'487 lines) and `themeresources.xaml` (2'184 lines).
[^sdk80]: Windows Software Development Kit for Windows 8, `Windows Kits/8.0/Include/winrt/xaml/design/generic.xaml` (6'481 lines) and `themeresources.xaml` (943 lines).
[^wp8]: Windows Phone SDK 8.0, `Design/AccentColors/*.xaml`, one `PhoneAccentColor` per file; the Windows Phone SDK 7.1 per-accent `Design/*/ThemeResources.xaml` folders give the ten earlier colors.
[^grid]: `ItemsPage.xaml` in the Windows 8 WindowsBlogReader sample, and [Implementing the Layout Guidelines in WinJS, Telerik](https://www.telerik.com/blogs/implementing-the-layout-guidelines-in-winjs).
[^motion]: Microsoft Learn, [PointerDownThemeAnimation](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.media.animation.pointerdownthemeanimation) and [EntranceThemeTransition](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.media.animation.entrancethemetransition).
