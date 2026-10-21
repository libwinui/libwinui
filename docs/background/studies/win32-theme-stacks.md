# Win32 Theme Stacks

Four rendering stacks sit under every Win32 application on Windows 11, and each one explains a different part of what "the Windows look" meant in a given year. This study takes them apart layer by layer, measures what they produce on a current machine, and works out which of those looks a retained, Direct2D-backed framework can reproduce from an algorithm and which it can only imitate.

The measurements were taken on Windows 11 Enterprise build 26200 with the 10.0.26100.0 SDK headers, in dark mode. Values read off that machine are marked as measured; values taken from documentation are marked as documented.

A Win32 window on Windows 11 is four layers composited on top of each other:

| Layer | Since | What it is | Code or data |
| --- | --- | --- | --- |
| DWM | Vista | Composites every window and owns the frame, caption, rounded corners, drop shadow and Mica. The app can only set attributes. | Attributes |
| uxtheme + msstyles | XP | `DrawThemeBackground` blits nine-sliced 32-bit PNGs out of a theme file. This is what "themed" means. | Almost entirely data |
| GDI+ | XP (redistributable to 98) | Antialiased, floating-point, alpha-blended. Optional; used by WinForms owner-draw paths. | Algorithm |
| GDI | 1.0 | Aliased, integer, no alpha. `DrawEdge`, `FillRect`, `DrawText`, `DrawFocusRect`. The "Windows Classic" look, and the fallback when visual styles are off. | Algorithm |

The fact that shapes everything below: **GDI and GDI+ are algorithms, uxtheme is a bitmap archive.** Windows 95's look can be computed from five colors and a rule. Windows XP's and Vista's looks are pictures of themselves, and a framework that does not ship those pictures produces a homage, not a replica.

## GDI

### The Drawing Model

GDI is a stateful, integer, aliased rasterizer that paints opaque by default.[^gdi] A device context (`HDC`) holds the currently selected pen, brush, font, bitmap, clip region and, historically, a color palette, and every drawing call uses whatever is selected. The properties that produce the look:

- **No antialiasing.** Lines, rectangles, ellipses and polygons have hard pixel edges. GDI has no concept of coverage, so a 1 px line is exactly one pixel at an integer coordinate.
- **No alpha.** A `COLORREF` is `0x00BBGGRR` and the high byte is ignored by essentially every core call. Translucency only arrived as bolt-ons (see below).
- **Integer coordinates**, in logical units mapped through the mapping mode. For UI code that is `MM_TEXT`, i.e. one unit per pixel.
- **Raster operations.** `BitBlt` and `PatBlt` combine source, pattern and destination with a boolean ROP (`SRCCOPY`, `PATINVERT`, `DSTINVERT`). The focus rectangle, the drag rubber band and marching-ants selection are XOR operations, so drawing them twice erases them.
- **Palette-era colors.** `#C0C0C0`, `#808080`, navy `#000080` and teal `#008080` are entries of the 20-color default system palette of the 8-bit era, not design choices. Anything outside the palette was dithered, which is why GDI still has hatch and halftone brushes.

The resulting signature: hard edges, flat fills, no gradients, no shadows, no curves except circles, and a strict 1 px structural grid.

### The 3-D Bevel Is `DrawEdge`

The whole Windows 95 to 2000 control language reduces to `DrawEdge` plus a short color ramp.[^drawedge] `DrawEdge(hdc, rc, uType, uFlags)` draws up to two concentric 1 px rings, and `uType` picks each ring's polarity on its own:

```text
BDR_RAISEDOUTER 0x0001   BDR_SUNKENOUTER 0x0002
BDR_RAISEDINNER 0x0004   BDR_SUNKENINNER 0x0008

EDGE_RAISED = RAISEDOUTER | RAISEDINNER   a control that sticks out
EDGE_SUNKEN = SUNKENOUTER | SUNKENINNER   a well: edit box, check box, status pane
EDGE_ETCHED = SUNKENOUTER | RAISEDINNER   a group box or separator groove
EDGE_BUMP   = RAISEDOUTER | SUNKENINNER   a ridge
```

`uFlags` picks the sides (`BF_LEFT | BF_TOP | BF_RIGHT | BF_BOTTOM`) and modifiers (`BF_MIDDLE` fills the interior, plus `BF_SOFT`, `BF_FLAT`, `BF_MONO`, `BF_ADJUST`). The colors per ring and side, documented and matching what Wine reimplements from the published behavior:

| `uType` | Top/left outer | Top/left inner | Bottom/right outer | Bottom/right inner |
| --- | --- | --- | --- | --- |
| `EDGE_RAISED` | `COLOR_3DLIGHT` | `COLOR_BTNHIGHLIGHT` | `COLOR_3DDKSHADOW` | `COLOR_BTNSHADOW` |
| `EDGE_SUNKEN` | `COLOR_BTNSHADOW` | `COLOR_3DDKSHADOW` | `COLOR_BTNHIGHLIGHT` | `COLOR_3DLIGHT` |

Two details make a hand-rolled bevel look "not quite right" when they are missed:

1. **On the shadow side the darkest color is outside.** A raised button's bottom-right edge is `3DDKSHADOW` (black) and then `BTNSHADOW` (mid gray) moving inward, not the other way around.
2. **On the lit side the outer ring is `COLOR_3DLIGHT`, not white.** In Windows 2000's scheme `3DLIGHT` equals the face color, so the outer ring vanishes into the parent and the eye reads one crisp white line; in Windows 95's it is `#DFDFDF`, a soft step between face and white. Either way the top-left reads as one highlight and the bottom-right as two shadows. That asymmetry comes from the palette, not the geometry, so an implementation should draw all four rings and let the colors collapse them. Hard-coding the asymmetry breaks every custom scheme where `3DLIGHT` differs from the face.

`BF_SOFT` adds a second raised shape. `DrawFrameControl(DFC_BUTTON, DFCS_BUTTONPUSH)` passes it for every command button, and it swaps the two top/left rings, so on a push button the white highlight is the outermost pixel. A window border, a menu edge or a scroll arrow uses the plain arrangement. Drawing every raised surface with one of the two is the easiest way to get the era subtly wrong. A third raised shape is the hot toolbar button: a single ring with no second one behind it.

`DrawFrameControl` layers state on top with `DFCS_PUSHED`, `DFCS_CHECKED`, `DFCS_INACTIVE`, `DFCS_HOT`, `DFCS_FLAT` and `DFCS_MONO`, and supplies the check box, radio, scroll-arrow and combo-arrow glyphs.[^dfc] A pressed push button is not a mirrored bevel: it is a single `COLOR_BTNSHADOW` frame around the face, with the content moved one pixel right and down. The default push button gets an extra 1 px `COLOR_WINDOWFRAME` frame outside the bevel, and its face shrinks by that pixel.

### The System Color Table Is the Whole Palette

`winuser.h` defines 31 `COLOR_*` indices (0 to 30).[^getsyscolor] Classic controls never hard-code a color, they call `GetSysColor(i)` or `GetSysColorBrush(i)`. The ones that carry the 3-D language:

| Index | Name | Role |
| --- | --- | --- |
| 15 | `COLOR_BTNFACE` / `COLOR_3DFACE` | Every control's flat fill, and the dialog background |
| 20 | `COLOR_BTNHIGHLIGHT` / `COLOR_3DHILIGHT` | The bright bevel edge |
| 22 | `COLOR_3DLIGHT` | The outer light bevel edge |
| 16 | `COLOR_BTNSHADOW` / `COLOR_3DSHADOW` | The mid-gray bevel edge |
| 21 | `COLOR_3DDKSHADOW` | The dark outer bevel edge |
| 6 | `COLOR_WINDOWFRAME` | 1 px hard frames (default-button ring, list borders) |
| 5 / 8 | `COLOR_WINDOW` / `COLOR_WINDOWTEXT` | Editable and scrollable surfaces: white, not face |
| 13 / 14 | `COLOR_HIGHLIGHT` / `COLOR_HIGHLIGHTTEXT` | Selection |
| 17 | `COLOR_GRAYTEXT` | Disabled text |
| 4 / 7 / 30 / 29 | `COLOR_MENU` / `MENUTEXT` / `MENUBAR` / `MENUHILIGHT` | Menus |
| 2 / 3 / 9 / 27 / 28 | Caption colors, including the Windows 98 gradient pair | Title bars |
| 24 / 23 | `COLOR_INFOBK` / `COLOR_INFOTEXT` | Tooltips (`#FFFFE1`) |
| 26 | `COLOR_HOTLIGHT` | Hyperlinks |
| 0 / 12 | `COLOR_SCROLLBAR` / `COLOR_APPWORKSPACE` | Scroll bar trough, MDI backdrop |

The split between `WINDOW` and `BTNFACE` is the most important structural distinction of the classic language. A dialog is face gray, but anything you can type into or scroll is `COLOR_WINDOW` white inside a sunken border. Getting that one relationship right does more for "this looks like Windows 98" than any amount of palette tuning.

### Focus, Disabled State and Patterns

- **`DrawFocusRect`** XORs a 50 % checkerboard brush along a 1 px rectangle. The dots alternate every pixel, not every two, and the pattern is aligned to the window origin so neighboring focus rects phase-match. Because it is XOR, a second call erases it. On screen it reads as a fine gray shimmer, not a dashed line.[^focusrect]
- **Disabled text is not simply gray.** `DrawState(..., DSS_DISABLED)` draws the string twice, first in `COLOR_3DHILIGHT` offset by (1, 1), then in `COLOR_3DSHADOW` at the origin, which gives the engraved label. Flat gray disabled text is an XP-and-later look.[^drawstate]
- **`COLOR_GRAYTEXT == 0`** used to mean "no gray available": disabled menu text was then drawn with a 50 % pattern brush over the normal text.
- **Hatch and pattern brushes** (`CreateHatchBrush`, `CreatePatternBrush`) supply the drag-rectangle stipple, the disabled scroll bar trough and the classic desktop pattern.

### Text

- Windows 3.x and 95 set UI text in **raster fonts** (`System`, `MS Sans Serif`, `Fixedsys`, `Terminal`), hand-drawn glyph bitmaps at fixed sizes (8, 10, 12, 14, 18, 24 pt). They do not scale, hint or antialias, and the stepped diagonals of MS Sans Serif 8 pt are the visual texture of Windows 95.
- **MS Sans Serif is not installed on Windows 11** (measured: the installed font collection has `Microsoft Sans Serif` but not `MS Sans Serif`). A request for it silently gets the TrueType `Microsoft Sans Serif`, which is metrically compatible but outline-based and antialiased. `Fixedsys`, `Terminal` and `System` are gone too.
- Grayscale font smoothing arrived as an option with Windows 95 Plus! and Windows 98. **ClearType** (subpixel RGB) arrived in XP, off by default, and became the default in Vista. Era-faithful text is therefore aliased for 95/98/2000, grayscale for XP and ClearType from Vista on.
- `DrawText`/`DrawTextEx` own layout (`DT_SINGLELINE | DT_VCENTER | DT_CENTER`, `DT_END_ELLIPSIS`, `DT_HIDEPREFIX`), including the `&` mnemonic underline that only appears after the user presses Alt (`WM_UPDATEUISTATE`, `UISF_HIDEACCEL`).

### The Bolt-Ons That Broke the Flat-Fill Rule

Three `msimg32.dll` additions made pre-uxtheme polish possible:[^msimg32]

- **`GradientFill`** with `GRADIENT_FILL_RECT_H` (0), `GRADIENT_FILL_RECT_V` (1) and `GRADIENT_FILL_TRIANGLE` (2). Two-vertex axis-aligned gradients; the Windows 98 gradient caption (`COLOR_GRADIENTACTIVECAPTION`) is exactly this.
- **`AlphaBlend`** with `BLENDFUNCTION{AC_SRC_OVER, 0, SourceConstantAlpha, AC_SRC_ALPHA}`: constant and per-pixel alpha from a 32 bpp DIB. The only per-pixel translucency GDI ever had, and what themed 32-bit bitmaps blit through.
- **`TransparentBlt`**: color-key transparency, the magenta-mask idiom from before alpha.

## Uxtheme and Msstyles

### Class, Part, State

XP replaced "controls draw themselves from five system colors" with "controls ask a theme file what this part looks like in this state". The API is small:[^uxtheme]

```c
HTHEME OpenThemeData(HWND, L"BUTTON");                    // or OpenThemeDataForDpi
DrawThemeBackground  (hTheme, hdc, BP_PUSHBUTTON, PBS_HOT, &rc, NULL);
DrawThemeBackgroundEx(hTheme, hdc, part, state, &rc, &opts);
DrawThemeText / DrawThemeTextEx(hTheme, hdc, part, state, str, len, flags, &rc, &dttopts);
GetThemePartSize / GetThemeMargins / GetThemeColor / GetThemeFont / GetThemeInt
GetThemeSysColor(hTheme, COLOR_*);                        // the theme's own system colors
IsThemeBackgroundPartiallyTransparent(...);               // paint the parent first if true
CloseThemeData(hTheme);
```

`vsstyle.h` holds the vocabulary: `VSCLASS_BUTTON` with parts `BP_PUSHBUTTON`, `BP_RADIOBUTTON`, `BP_CHECKBOX`, `BP_GROUPBOX`, `BP_COMMANDLINK`; `PUSHBUTTONSTATES` is `PBS_NORMAL`, `PBS_HOT`, `PBS_PRESSED`, `PBS_DISABLED`, `PBS_DEFAULTED`, `PBS_DEFAULTED_ANIMATING`. `CHECKBOXSTATES` has twenty entries (unchecked, checked, mixed, implicit and excluded, each normal, hot, pressed and disabled), and `EDITPARTS` tells `EP_EDITBORDER_NOSCROLL` apart from the `_HSCROLL`, `_VSCROLL` and `_HVSCROLL` borders. Two things follow for any theme model:

- **`PBS_DEFAULTED` is its own state**, with its own artwork, not "normal plus a ring".
- **State counts are per control and large.** A single four-member state enum shared by every control runs out of room quickly.

### How a Part Is Drawn

`TMT_BGTYPE` in `vssym32.h` picks one of three strategies per part and state:[^vssym32]

| `TMT_BGTYPE` | Value | Meaning |
| --- | --- | --- |
| `BT_IMAGEFILE` | 0 | Blit from a bitmap in the msstyles |
| `BT_BORDERFILL` | 1 | Draw a border and a fill from properties |
| `BT_NONE` | 2 | Draw nothing |

**`BT_BORDERFILL` is a vector recipe**: `TMT_BORDERTYPE` (`BT_RECT`, `BT_ROUNDRECT`, `BT_ELLIPSE`), `TMT_BORDERCOLOR`, `TMT_BORDERSIZE`, `TMT_ROUNDCORNERWIDTH/HEIGHT`, and `TMT_FILLTYPE` (`FT_SOLID`, `FT_VERTGRADIENT`, `FT_HORZGRADIENT`, `FT_RADIALGRADIENT`, `FT_TILEIMAGE`) with `TMT_GRADIENTCOLOR1..5` and `TMT_GRADIENTRATIO1..5`. Five gradient stops with explicit ratios, four gradient geometries and three border shapes are the theme engine's whole vector vocabulary, and a reasonable target for what a retained renderer's brush model should express.

**`BT_IMAGEFILE` is a nine-slice blit**, described by:

```text
TMT_IMAGEFILE, TMT_IMAGEFILE1..7        the bitmap(s)
TMT_IMAGECOUNT                          how many states are stacked in one strip
TMT_IMAGELAYOUT     IL_VERTICAL | IL_HORIZONTAL
TMT_SIZINGTYPE      ST_TRUESIZE | ST_STRETCH | ST_TILE
TMT_SIZINGMARGINS                       the nine-slice cuts (L, R, T, B)
TMT_CONTENTMARGINS                      where the caller's content goes
TMT_TRANSPARENT, TMT_TRANSPARENTCOLOR, TMT_ALPHALEVEL, TMT_ALPHATHRESHOLD
TMT_IMAGESELECTTYPE IST_NONE | IST_SIZE | IST_DPI
TMT_MINDPI1..7                          the seven DPI rungs
```

`TMT_SIZINGMARGINS` is CSS `border-image-slice` and `TMT_CONTENTMARGINS` is padding. Drawing a themed part means picking the bitmap for the DPI, cutting it into nine, stretching or tiling edges and center, and laying the content out inside the content margins.

The rest of the 257 `TMT_*` properties cover glyphs (`GT_IMAGEGLYPH`, `GT_FONTGLYPH`), text decoration (`TMT_TEXTSHADOWTYPE`, `TMT_TEXTGLOW`, `TMT_TEXTGLOWSIZE`, `TMT_TEXTBORDERSIZE`), animation (`TMT_TRANSITIONDURATIONS` and friends) and composition (`TMT_COMPOSITED`, `TMT_GLOWINTENSITY`, `TMT_COLORIZATIONCOLOR`). `TMT_CLASSICVALUE` (3202) lets every themed part point back at what the classic renderer would have drawn: the engine was built with a classic fallback from day one, which is the same "one control, several appearances" split a multi-era framework needs.

### What Is Inside Windows 11's `aero.msstyles` (Measured)

`aero.msstyles` is a resource-only PE. A byte-level scan of the 1'859'728-byte file on the test machine found:

| Measurement | Result |
| --- | --- |
| Embedded PNG images | 1'614 |
| Theme class names | `Button`, `Explorer::Button`, `DarkMode_Explorer::Button`, `DarkMode_DarkTheme::Button`, and 69 distinct `DarkMode_*` classes |
| Recurring size families | e.g. 19×21, 24×26, 29×32, 38×42, 47×52, 57×63, 76×84, sixteen images at each size |
| Tall narrow strips (h ≥ 3w, w ≤ 40) | 676 |

The seven widths 19, 24, 29, 38, 47, 57 and 76 are 19 × {1.00, 1.26, 1.53, 2.00, 2.47, 3.00, 4.00}: the 100/125/150/200/250/300/400 % DPI ladder, seven rungs, matching `TMT_MINDPI1..7`. The 676 tall strips are `IL_VERTICAL` multi-state image strips.

So the Windows 11 Win32 control look is a bitmap archive authored at seven fixed DPIs, not a computed style. A framework can imitate its geometry and color; it cannot derive it. The bitmaps are also copyrighted image content, so extracting or shipping them is off the table for any library.

### Dark Mode Is Not a Second Theme File

`aero.theme` and `dark.theme` both point at the same file (`Path=%ResourceDir%\Themes\Aero\Aero.msstyles`) and differ only in `SystemMode` and `AppMode`. Dark mode for common controls comes from **undocumented class names inside the same msstyles**: an app calls `SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr)` and gets dark artwork from then on. Present in the binary: `DarkMode::Menu`, `DarkMode::Toolbar`, `DarkMode::ItemsView`, `DarkMode_Explorer::{Button, Edit, ListView, ScrollBar, TreeView, Progress, Tooltip, TaskDialog}`, `DarkMode_DarkTheme::{Button, Combobox, Edit, Header, ListView, ScrollBar, Tab, Toolbar, TreeView}` and more.

That makes Win32 dark mode opt-in per window, undocumented and partial, which is why Win32 and WinForms apps look broken in dark mode by default. A framework cannot get a dark classic palette from the OS and has to carry its own.

### Glass Text and Transitions

- `DrawThemeTextEx` with `DTTOPTS` is how Vista drew **text on glass**: `DTT_GLOWSIZE` and `DTT_COMPOSITED` render the string with an outward glow into a premultiplied top-down DIB so it stays readable over a blurred backdrop.[^drawthemetextex]
- **Buffered animation** (`BeginBufferedAnimation`, `BP_ANIMATIONSTYLE`, `GetThemeTransitionDuration`, `TMT_TRANSITIONDURATIONS`) is how Vista and 7 cross-fade a button between normal and hot, over roughly 100 to 150 ms (documented). Architecturally that is the same thing as a compositor opacity cross-fade; the era differs only in duration and in which states animate.
- `IsThemeBackgroundPartiallyTransparent` plus `DrawThemeParentBackground` is the engine's answer to "this part has holes, paint the parent first".

## GDI+

GDI+ (`gdiplus.dll`, XP onward, redistributable to 98) is a second rasterizer next to GDI, not a layer on top of it:[^gdiplus]

| Axis | GDI | GDI+ |
| --- | --- | --- |
| Coordinates | Integer | `float`, with a full 3×2 `Matrix` |
| Edges | Aliased | `SmoothingMode`: None, AntiAlias, HighQuality |
| Alpha | None (bolt-ons only) | Native ARGB, `CompositingMode`, `CompositingQuality` |
| Fills | Solid, hatch, pattern | Solid, `LinearGradientBrush`, `PathGradientBrush`, `TextureBrush`, hatch |
| Geometry | Rect, ellipse, polygon, polyline | `GraphicsPath`: Béziers, arcs, figures, widening, flattening |
| Text | `DrawText` | Its own rasterizer, `TextRenderingHint` |

Four settings explain most "my GDI+ drawing looks wrong" reports:

1. **`SmoothingMode`** defaults to `None` on a `Graphics` from `PaintEventArgs`, so naive code looks like GDI. Switch on `AntiAlias` and every 1 px line becomes a 2 px smear, unless the next setting is also changed.
2. **`PixelOffsetMode::Half`** shifts sampling by half a pixel so integer rectangles land on pixel boundaries. Without it a 1 px `DrawRectangle(0, 0, w, h)` under antialiasing straddles two pixel rows at 50 % each.
3. **`TextRenderingHint`**: `SingleBitPerPixelGridFit` is aliased, `AntiAliasGridFit` grayscale, `ClearTypeGridFit` subpixel. GDI+ text was widely judged worse-spaced than GDI text, which is why .NET 2.0 switched WinForms to `TextRenderer` (a `DrawTextW` wrapper) with `UseCompatibleTextRendering = false` as the default.
4. **Gamma.** GDI+ interpolates in non-linear sRGB unless `LinearGradientBrush.GammaCorrection` is set. Direct2D also blends in sRGB by default, so the two agree; a linear-light renderer would put the midpoint of the same two stops somewhere else.

GDI+ is not a look worth reproducing. It is the closest existing analog to a Direct2D UI renderer (float coordinates, antialiasing, alpha, gradient brushes), and its lesson is point 2: **an antialiased float renderer must be able to switch antialiasing off and snap to the pixel grid, or it can never draw a convincing 1990s control.**

## WinForms

WinForms is the most useful reference here, because it had exactly this problem: one control model, several visual eras, on a renderer that is not the OS's own.

### Three Painting Paths

1. **The OS paints it.** Most WinForms controls are Win32 controls (`Button` is a `BUTTON` window). With a comctl32 v6 manifest and `Application.EnableVisualStyles()`, `user32`/`comctl32` paint them through uxtheme.
2. **Managed, themed.** `VisualStyleRenderer` and the per-control wrappers (`ButtonRenderer`, `CheckBoxRenderer`, `ComboBoxRenderer`, `TabRenderer`) call `DrawThemeBackground` from managed code.
3. **Fully managed GDI+.** `ControlPaint` and the `ButtonInternal` adapters draw the classic 3-D look from first principles whenever visual styles are off, `FlatStyle` is `Flat` or `Popup`, or the control is owner-drawn anyway (`ToolStrip`, `DataGridView`).

`Application.RenderWithVisualStyles` is the switch, and it is checked per paint, not once at startup.

### `ControlPaint` and `HLSColor`: The Ramp as an Algorithm

`ControlPaint.Light`, `LightLight`, `Dark` and `DarkDark` derive the bevel ramp from any face color, through a struct whose own comment says it copies the lightening and darkening logic from the Windows sources.[^controlpaint] Its shape:

- RGB is converted to integer HLS on a 0 to 240 range (`HLSMax = 240`, the range of the Windows 3.x color picker), with luminosity `(max + min) * 240 / 510`, rounded.
- Lightening moves luminosity toward a target scaled by `HighlightAdjustment = +500`, darkening toward zero scaled by `ShadowAdjustment = -333`. For a positive `n` the rule is `lum * (1000 - n) / 1000 + 241 * n / 1000`; for a negative one `lum * (1000 + n) / 1000`.
- **When the face is exactly `SystemColors.Control`**, it does not compute anything and returns the OS's own `ControlLight`, `ControlLightLight`, `ControlDark` and `ControlDarkDark`. The computed ramp and the shipped ramp disagree, and Microsoft picked the shipped one for the default scheme.

`ButtonBaseAdapter.ColorOptions.Calculate()` builds each button's ramp from that: the shadows and highlight from the system colors when the back color is `Control`, from `Dark`/`DarkDark`/`LightLight` otherwise, plus a `LowButtonFace` and `LowHighlight` 10 % brighter or darker depending on whether the face is bright or dark. That brightness test is how the classic look inverts itself on dark backgrounds instead of disappearing, and it is what makes user schemes like Hot Dog Stand or High Contrast Black work with the same code. Every color then still goes through `hdc.FindNearestColor`, a palette-era holdover.

### `FlatStyle`: Four Looks, One Control

| `FlatStyle` | What it draws |
| --- | --- |
| `System` | Hands off to the Win32 control; the OS decides |
| `Standard` | Themed via `ButtonRenderer` with visual styles on, else the classic 2 px bevel from `ControlPaint` |
| `Flat` | 1 px `FlatAppearance.BorderColor` border, flat fill, explicit hover and pressed colors |
| `Popup` | Flat at rest, raises to a 1 px bevel on hover: the Office 97 / IE4 toolbar idiom |

`FlatStyle` is a per-control choice of rendering strategy, orthogonal to the app-wide visual styles switch. Era belongs to the theme, but "this particular button is a flat toolbar button" belongs to the control.

### `ToolStrip` and `ProfessionalColors`

`ToolStripProfessionalRenderer` with `ProfessionalColorTable` derives a whole Office 2003-style identity (`MenuItemPressedGradientBegin/Middle/End`, `ButtonSelectedGradientBegin`, `SeparatorDark/Light` and so on) from `SystemColors`, using the same ramp plus a color-scheme detector. It is a token-derivation function aimed at a different decade.

### What .NET 9 and 10 Dark Mode Does

The newest evidence sits in `ButtonInternal/DarkMode/`. `ModernButtonDarkModeRenderer` is described as a "modern, WinUI-inspired push button renderer used when `Control.VisualStylesMode` is `VisualStylesMode.Net11` or later", and it hand-draws Fluent geometry in GDI+:

| Property | Value |
| --- | --- |
| Corner radius | 8 logical px unfocused, 6 focused |
| Border thickness | 1 logical px |
| Focus ring | 2 px ring plus 1 px gap, white in dark mode |
| Dark normal / hover / pressed | `#2D2D2D` / `#323232` / `#2A2A2A`, gap ring `#0A0A0A` |
| Light normal / hover / pressed | `#FBFBFB` / `#F9F9F9` / `#F5F5F5`, border `#D0D0D0` |

It also brings a `ButtonBackColorAnimator` for the state cross-fade. Microsoft's own answer to "make a Win32-era framework look like Windows 11" is to leave the theme engine alone and draw the Fluent geometry by hand. That is the approach libwinui takes natively, and it means the reverse direction, faithful older eras on a modern renderer, is where the open problem is.

## Measured Windows 11 Behavior

### The System Palette Is Frozen, Light-Only, and Not the Accent

Read with `GetSysColor` on a machine **in dark mode** (`AppsUseLightTheme = 0`, `SystemUsesLightTheme = 0`):

| Index | Value | Comment |
| --- | --- | --- |
| `COLOR_BTNFACE` | `#F0F0F0` | The Windows 7 light gray, not `#C0C0C0`, not `#D4D0C8`, and not dark |
| `COLOR_WINDOW` | `#FFFFFF` | White, in dark mode |
| `COLOR_WINDOWTEXT` | `#000000` | Black, in dark mode |
| `COLOR_BTNHIGHLIGHT` | `#FFFFFF` | |
| `COLOR_3DLIGHT` | `#E3E3E3` | No longer equal to the face, so the ring collapse no longer happens |
| `COLOR_BTNSHADOW` | `#A0A0A0` | |
| `COLOR_3DDKSHADOW` | `#696969` | Not black any more |
| `COLOR_WINDOWFRAME` | `#646464` | |
| `COLOR_HIGHLIGHT` | `#0078D7` | The Windows 10 blue, frozen, not the user's accent |
| `COLOR_GRAYTEXT` | `#6D6D6D` | |
| `COLOR_ACTIVECAPTION` | `#99B4D1` | The Windows 7 caption; DWM has drawn the caption since Vista |
| `COLOR_INFOBK` | `#FFFFE1` | The 1995 tooltip yellow, unchanged |
| `COLOR_MENU` / `MENUBAR` | `#F0F0F0` | |
| `COLOR_MENUHILIGHT` | `#3399FF` | |

Meanwhile the DWM registry keys report the user's real accent: `AccentColor = 0xFFD47800` (ABGR), i.e. `#0078D4`, and `ColorizationColor = 0xC40078D4`. Three conclusions:

1. **`GetSysColor` does not follow dark mode.** A framework that builds its modern theme from it is wrong in dark mode by construction.
2. **`COLOR_HIGHLIGHT` is not the accent.** `#0078D7` is not `#0078D4`, and the system value does not track the user setting. Selection color must come from the accent.
3. **`GetSysColor` is the right source for exactly one thing: high contrast**, where the user picked those exact values on purpose.

libwinui follows all three. The Win32 theme source reads light/dark from `AppsUseLightTheme` and the accent from `UISettings::GetColorValue`, falling back to the DWM `AccentColor` key only when `UISettings` is unreachable (`src/platform/windows/Win32ThemeSource.cpp`). It reads `GetSysColor` only to fill `HighContrastPalette` while a contrast theme is on.

### The DWM Layer

The app does not draw the frame, it configures it through `DwmSetWindowAttribute` (values from `dwmapi.h`):[^dwm]

| Attribute | Value | Effect |
| --- | --- | --- |
| `DWMWA_USE_IMMERSIVE_DARK_MODE` | 20 | Dark caption and frame (19 before Windows 10 build 19041) |
| `DWMWA_WINDOW_CORNER_PREFERENCE` | 33 | `DWMWCP_DEFAULT`, `DONOTROUND`, `ROUND`, `ROUNDSMALL` |
| `DWMWA_BORDER_COLOR` | 34 | The 1 px frame line |
| `DWMWA_CAPTION_COLOR` | 35 | Caption fill |
| `DWMWA_TEXT_COLOR` | 36 | Caption text |
| `DWMWA_VISIBLE_FRAME_BORDER_THICKNESS` | 37 | Reads the real border thickness |
| `DWMWA_SYSTEMBACKDROP_TYPE` | 38 | `DWMSBT_AUTO`, `NONE`, `MAINWINDOW` (Mica), `TRANSIENTWINDOW` (Acrylic), `TABBEDWINDOW` |

Windows 11 rounds top-level windows by default, and a Windows 95 theme inside a rounded, drop-shadowed Windows 11 frame does not read as Windows 95. An era theme has to at least ask for `DWMWCP_DONOTROUND`.

### Why a WinForms App Looks "Off" on Windows 11

Put together, a stock WinForms app today is a DWM-drawn, rounded Windows 11 frame around a Windows 7 gray `#F0F0F0` client area from a frozen color table, holding Windows 8-era flat bitmap controls from `aero.msstyles` that ignore dark mode unless each `HWND` opts into an undocumented `DarkMode_*` class, set in Segoe UI 9 pt while the shell around it uses Segoe UI Variable, with a `#0078D7` selection that ignores the user's `#0078D4` accent. That is five visual eras in one window, and the reason a framework that renders every client pixel from one coherent theme does not inherit the layering.

### Typography

| Metric | Value on the test machine |
| --- | --- |
| `CaptionFont`, `MenuFont`, `MessageFont`, `StatusFont`, `SmCaptionFont`, `IconFont` | All `Segoe UI`, `lfWeight = 400`, `lfHeight = -18` at 144 DPI, i.e. 9 pt |
| `SystemFonts.MessageBoxFont` | `Segoe UI, 9pt` |
| `Segoe UI Variable Text` | Present |
| `Segoe UI Variable` as a family name | Absent; the families are `Segoe UI Variable Text`, `Small` and `Display` |
| `Segoe Fluent Icons`, `Segoe MDL2 Assets` | Present |
| `Tahoma` | Present, so an XP theme's font is the real one |
| `Microsoft Sans Serif` | Present |
| `MS Sans Serif`, `Fixedsys`, `Terminal`, `System` | Absent |

Win32 on Windows 11 is still Segoe UI 9 pt. Segoe UI Variable is a XAML and WinUI choice.

## Era Catalog

What each era is, expressed as primitives a renderer needs, and whether it can be produced from an algorithm without shipping Microsoft assets:

| Era | Defining primitives | Font | Focus | Reproducible? |
| --- | --- | --- | --- | --- |
| [Windows 95](../themes/win9x.md) | Flat fills, 2 px `DrawEdge` bevels from a short ramp, square corners, 1 px XOR dotted focus, aliased everything, navy `#000080` selection, teal desktop | MS Sans Serif 8 pt (bitmap) | Dotted rect, inset | Yes, fully: an algorithm over a handful of colors |
| [Windows 98](../themes/win9x.md) | As 95, plus hot tracking, gradient captions (`GradientFill`), flat toolbars | MS Sans Serif 8 pt | Dotted rect | Yes |
| [Windows 2000](../themes/win9x.md) | As 98, plus menu fade and slide, gradient captions on by default, warmer `#D4D0C8` face | Tahoma 8 pt | Dotted rect | Yes |
| [Windows XP (Luna)](../themes/luna.md) | 3 to 5 stop vertical gradients, 3 px rounded buttons, `#003C74` blue borders, amber hover, `#ECE9D8` dialog face, glossy caption; image-based | Tahoma 8 pt | Dotted rect | Homage only: the real thing is `luna.msstyles` bitmaps |
| [Windows Vista / 7 (Aero)](../themes/aero.md) | DWM blur-behind glass with colorization, glossy two-tone buttons, animated hot-track cross-fade, glow text on glass, 32 bpp themed bitmaps | Segoe UI 9 pt | Dotted rect | Approximation: client-area glass was removed after 7 |
| [Windows 8 (Metro)](../themes/mdl.md) | Flat, no glass, no bevels, square, a saturated accent, thin borders | Segoe UI 9 pt | Dotted rect | Yes, flat is easy |
| [Windows 10 (MDL2)](../themes/mdl2.md) | Flat plus accent plus 1 px borders plus `Segoe MDL2 Assets` glyphs; Reveal in UWP only | Segoe UI 9 pt | Dotted rect (Win32), accent ring (UWP) | Yes |
| [Windows 11 (Fluent)](../themes/fluent2.md) | 4 to 8 px corners, layered elevation stroke gradients, Mica and Acrylic, `Segoe Fluent Icons`, two-ring focus visual | Segoe UI Variable (XAML), Segoe UI (Win32) | 2 px outer and 1 px inner ring | Yes |

The table is for the Win32 side of each era. The XAML-era pages ([Metro](metro.md), and the theme pages linked above) cover what Windows 8 and later looked like in their own frameworks.

## What libwinui Can Express

The study started from a renderer with eight commands (filled and stroked rounded rects, their gradient variants, a line, a text run and an axis-aligned clip) and a `Theme` shaped entirely around buttons. Against the era catalog that setup had three problems worth more than all palette work put together: geometry went through an antialiasing rasterizer, text was always grayscale-smoothed, and layout produced fractional bounds. A Windows 95 button is defined by hard 1 px edges, so the colors were right while the rasterization came from the wrong century.

The capability list that came out of it, and where libwinui stands now:

| Capability | Needed by | libwinui now |
| --- | --- | --- |
| Aliased geometry, aliased or grayscale or ClearType text, device-pixel snapping | Every classic era | `SetRasterization{geometry, text, snap_to_device_pixels}` (`render/Renderer.hpp`), emitted once per frame from `Theme::rasterization` |
| Four-ring bevel with the right order, `BF_SOFT`, per-side flags | 95 / 98 / 2000 | `draw_bevel_border()` with `BevelSides`, `soft` and `rings` (`render/Bevel.hpp`); `draw_win3x_border()` for the asymmetric 3.x edge |
| 1-on-1-off dotted focus rect | 95 to 7 | `draw_dotted_rect()` with `dash_length = 1` and `FocusVisual::ClassicDotted` (`render/FocusRing.hpp`) |
| 50 % stipple | Disabled dither, drag rect, scroll bar trough | `FillRectPattern` with `Pattern::Checkerboard50` |
| Horizontal and diagonal gradients | XP and 98 captions, ToolStrip | `GradientAxis::{Vertical, Horizontal, Diagonal}` (`render/Brush.hpp`) |
| Radial gradients | `FT_RADIALGRADIENT`, the Aero orb | `RadialGradientBrush` |
| Real ellipses | Classic circular radio bevel | `FillEllipse`, `StrokeEllipse`, `PushClipEllipse` |
| Engraved disabled text | Classic disabled labels | `TextEffect{TextEffectKind::Emboss, ...}` on `TextStyle::effect` (`render/Text.hpp`); no glow or shadow kind |
| Nine-slice bitmap chrome | Authentic XP / Vista | `DrawImageNineSlice`, backed by an internal image theme pack; libwinui ships no images |
| Group opacity | Fluent overlays | `PushOpacityLayer` / `PopOpacityLayer` |
| Backdrop blur | Aero glass, Mica, Acrylic | An `AcrylicBrush` fill blurs what was drawn before it; Mica comes from DWM |
| Drop shadow | Vista, Fluent flyouts | Approximated by stacked translucent shapes (`render/OverlayShadow.hpp`); there is no blur primitive for shadows |

Rasterization is a command in the stream, not hidden renderer state, so `TestRenderer` records it and the semantic goldens print it. "Does the Windows 95 theme ask for aliased rendering" is then a cross-platform assertion instead of something only a pixel golden can catch. Snapping happens in the Direct2D backend, at device pixels and at emit time, so the logical layout stays resolution-independent and headless tests do not move. The per-era policies:

| Themes | Geometry | Text | Snap |
| --- | --- | --- | --- |
| Early Windows, Windows 3.x, Windows 9x | Aliased | Aliased | Yes |
| Luna | Antialiased | Grayscale | No |
| Aero, MDL2, Fluent | Antialiased | Subpixel | No |
| Metro, Fluent 2 (the default rasterization) | Antialiased | Grayscale | No |

On the theme side the button-shaped struct got role colors, `Theme::colors()` (`ThemeColors` in `ui/themes/Theme.hpp`), whose fields mirror `COLOR_*` indices: `field` (`COLOR_WINDOW`), `selection` (`COLOR_HIGHLIGHT`), `text_disabled` and `link`. A second block, `Theme::surfaces` (`Surfaces`), holds the era-specific menu and tooltip colors, the etched separator pair and the scroll bar parts. Lists, trees, text fields and menus draw from those roles instead of borrowing a button's colors, which gives the classic eras their white wells on a gray dialog and navy-on-white selection.

The classic eras also got their own token tier, following the WinForms model. `ClassicPalette` (`ui/themes/ClassicPalette.hpp`) is modeled on the roughly twenty `COLOR_*` indices that carry the 3-D language, and `Theme::classic()` derives a whole `Theme` from one. `classic_lighter()` and `classic_darker()` port the integer-HLS rule described above, and `classic_bevel_ramp_from_face()` derives the four bevel colors from any face, with the same override WinForms uses: for the two canonical grays it returns the published ramps instead of the computed ones.

| Face | `3DLIGHT` | `BTNHIGHLIGHT` | `BTNSHADOW` | `3DDKSHADOW` | Used by |
| --- | --- | --- | --- | --- | --- |
| `#C0C0C0` | `#DFDFDF` | `#FFFFFF` | `#808080` | `#000000` | Windows 95, 98 SE (`ClassicPalette::windows_standard()`) |
| `#D4D0C8` | `#D4D0C8` | `#FFFFFF` | `#808080` | `#404040` | Windows 2000, XP Classic (`ClassicPalette::windows_standard_2000()`) |
| anything else | computed | computed | computed | computed | User schemes via `ClassicPalette::from_face()` |

A user scheme is one literal away: `ThemeRequest::classic_scheme` swaps any `ClassicPalette` into the Windows 9x variant. High-contrast classic schemes, where no lighten or darken of black gives the right ramp, set `ClassicPalette::bevel` directly.

Window chrome follows through `Theme::chrome` (`ChromeHints`), which the Win32 host applies with `DwmSetWindowAttribute`. Every pre-Fluent era asks for `CornerPreference::DoNotRound` and disallows a system backdrop.

What stays out of reach:

- **Microsoft's bitmaps.** XP and Vista are bitmap themes, so the shipped Luna and Aero packs are computed homages. The image backend exists, but only for originally authored or freely licensed art.
- **Raster fonts.** The Windows 9x theme asks for `MS Sans Serif` and gets `Microsoft Sans Serif`, which is outline-based. Short of bundling a licensed clone there is no fix.
- **Aero glass.** The client-area glass API was removed after Windows 7. A translucent look-alike is the ceiling.
- **The era's corners and shadow.** With `Frame::Themed` each era pack draws its own caption and border, but the window stays a DWM-framed window. Its corners are square, never Luna's top-only rounding, and its shadow is Windows 11's.

## Measured Data

All values read on Windows 11 Enterprise 26200 with SDK 10.0.26100.0, in dark mode.

### `GetSysColor`, Complete

```text
SCROLLBAR                #C8C8C8      BTNFACE                  #F0F0F0
BACKGROUND               #000000      BTNSHADOW                #A0A0A0
ACTIVECAPTION            #99B4D1      GRAYTEXT                 #6D6D6D
INACTIVECAPTION          #BFCDDB      BTNTEXT                  #000000
MENU                     #F0F0F0      INACTIVECAPTIONTEXT      #000000
WINDOW                   #FFFFFF      BTNHIGHLIGHT             #FFFFFF
WINDOWFRAME              #646464      3DDKSHADOW               #696969
MENUTEXT                 #000000      3DLIGHT                  #E3E3E3
WINDOWTEXT               #000000      INFOTEXT                 #000000
CAPTIONTEXT              #000000      INFOBK                   #FFFFE1
ACTIVEBORDER             #B4B4B4      HOTLIGHT                 #0066CC
INACTIVEBORDER           #F4F7FC      GRADIENTACTIVECAPTION    #B9D1EA
APPWORKSPACE             #ABABAB      GRADIENTINACTIVECAPTION  #D7E4F2
HIGHLIGHT                #0078D7      MENUHILIGHT              #3399FF
HIGHLIGHTTEXT            #FFFFFF      MENUBAR                  #F0F0F0
```

### Theme and Accent State

```text
HKCU\...\Themes\Personalize   AppsUseLightTheme = 0   SystemUsesLightTheme = 0
HKCU\SOFTWARE\Microsoft\Windows\DWM
    ColorizationColor = 0xC40078D4
    AccentColor       = 0xFFD47800  (ABGR, i.e. #0078D4)
    ColorPrevalence   = 0
aero.theme  [VisualStyles] Path=...\Aero.msstyles  SystemMode=Light  AppMode=Light
dark.theme  [VisualStyles] Path=...\Aero.msstyles  SystemMode=Dark   AppMode=Dark
```

### `aero.msstyles`

```text
size                 1'859'728 bytes
embedded PNGs        1'614
DarkMode_* classes   69   (incl. DarkMode_Explorer::Button, DarkMode_DarkTheme::Button)
DarkMode:: classes   Menu, Toolbar, ItemsView, Navigation, ProperTree, SearchBox,
                     PreviewPane, ReadingPane, ExplorerNavPane, TaskManager, ...
DPI ladder           19x21, 24x26, 29x32, 38x42, 47x52, 57x63, 76x84 (16 images each)
                     = 19 x {1.00, 1.26, 1.53, 2.00, 2.47, 3.00, 4.00}
                     = 100/125/150/200/250/300/400 %  = TMT_MINDPI1..7
vertical strips      676 images with h >= 3w and w <= 40
```

### Fonts

```text
present : Microsoft Sans Serif, Tahoma, Segoe UI, Segoe UI Variable Text,
          Segoe Fluent Icons, Segoe MDL2 Assets, Courier New       (310 families)
absent  : MS Sans Serif, Fixedsys, Terminal, System, "Segoe UI Variable"
system UI font : Segoe UI, weight 400, 9 pt (all six WindowMetrics LOGFONTs)
```

### Constants Referenced

```text
winuser.h    COLOR_* 0..30 ; BDR_/EDGE_/BF_* ; DFC_/DFCS_*
wingdi.h     GRADIENT_FILL_RECT_H=0, _RECT_V=1, _TRIANGLE=2 ; AC_SRC_OVER=0, AC_SRC_ALPHA=1
vssym32.h    257 TMT_* properties ; BGTYPE, FILLTYPE, BORDERTYPE, SIZINGTYPE,
             IMAGELAYOUT, IMAGESELECTTYPE, GLYPHTYPE, TEXTSHADOWTYPE
vsstyle.h    BUTTONPARTS, PUSHBUTTONSTATES (6), CHECKBOXSTATES (20), EDITPARTS (9),
             EDITTEXTSTATES (8)
uxtheme.h    DTBG_*, DTT_*, BPBF_*, BPPF_*, BP_ANIMATIONSTYLE
dwmapi.h     DWMWA_USE_IMMERSIVE_DARK_MODE=20, WINDOW_CORNER_PREFERENCE=33,
             BORDER_COLOR=34, CAPTION_COLOR=35, TEXT_COLOR=36,
             VISIBLE_FRAME_BORDER_THICKNESS=37, SYSTEMBACKDROP_TYPE=38
```

## Sources

Headers from the Windows SDK 10.0.26100.0: `winuser.h`, `wingdi.h`, `uxtheme.h`, `vsstyle.h`, `vssym32.h`, `dwmapi.h`. The WinForms sources referenced are in the [dotnet/winforms](https://github.com/dotnet/winforms) repository under `src/System.Windows.Forms/`: `ControlPaint.cs`, `ControlPaint.HLSColor.cs`, `Controls/Buttons/ButtonInternal/*` and its `DarkMode/` folder.

[^gdi]: Microsoft Learn, [Windows GDI](https://learn.microsoft.com/en-us/windows/win32/gdi/windows-gdi).
[^drawedge]: Microsoft Learn, [DrawEdge function (winuser.h)](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-drawedge).
[^dfc]: Microsoft Learn, [DrawFrameControl function (winuser.h)](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-drawframecontrol).
[^getsyscolor]: Microsoft Learn, [GetSysColor function (winuser.h)](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsyscolor).
[^focusrect]: Microsoft Learn, [DrawFocusRect function (winuser.h)](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-drawfocusrect).
[^drawstate]: Microsoft Learn, [DrawStateW function (winuser.h)](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-drawstatew).
[^msimg32]: Microsoft Learn, [GradientFill](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gradientfill), [AlphaBlend](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-alphablend) and [TransparentBlt](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-transparentblt).
[^uxtheme]: Microsoft Learn, [Visual Styles](https://learn.microsoft.com/en-us/windows/win32/controls/themes-overview) and the [uxtheme.h reference](https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/).
[^vssym32]: Microsoft Learn, [Property Identifiers](https://learn.microsoft.com/en-us/windows/win32/controls/property-typedefs) and [Parts and States](https://learn.microsoft.com/en-us/windows/win32/controls/parts-and-states).
[^drawthemetextex]: Microsoft Learn, [DrawThemeTextEx function (uxtheme.h)](https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-drawthemetextex) and [DTTOPTS structure](https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/ns-uxtheme-dttopts).
[^gdiplus]: Microsoft Learn, [GDI+](https://learn.microsoft.com/en-us/windows/win32/gdiplus/-gdiplus-gdi-start).
[^controlpaint]: `ControlPaint.HLSColor.cs` in [dotnet/winforms](https://github.com/dotnet/winforms), and Microsoft Learn, [ControlPaint Class](https://learn.microsoft.com/en-us/dotnet/api/system.windows.forms.controlpaint).
[^dwm]: Microsoft Learn, [DWMWINDOWATTRIBUTE enumeration (dwmapi.h)](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute).
