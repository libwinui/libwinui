# Rendering

Controls never talk to Direct2D. They describe what to draw as a stream of calls on an abstract
`Renderer`, and a backend decides what those calls mean: record them for a test, print them for
a golden file, or draw them with Direct2D and DirectWrite.

## Render Commands

`include/winui/render/Renderer.hpp` defines the `Renderer` interface. All coordinates are DIPs in
window space, and every `push_*` must be balanced by its `pop_*` within the same `render()` call.

| Group | Calls |
| --- | --- |
| Rects | `fill_rounded_rect`, `stroke_rounded_rect`, `fill_rounded_rect_gradient`, `stroke_rounded_rect_gradient` (Fluent's elevation "lift") |
| Lines and shapes | `stroke_line`, `stroke_wavy_line` (the spelling squiggle), `fill_polygon`, `stroke_polyline`, `fill_ellipse`, `stroke_ellipse`, `draw_geometry` (a `PathGeometry` filled and stroked, what every `Shape` draws) |
| Text | `draw_text(text, bounds, color, TextStyle)` |
| Images | `draw_image`, `draw_image_rect`, `draw_svg`, `draw_image_nine_slice` |
| Patterns | `fill_rect_pattern` (the classic 50 % dither) |
| State | `push_clip`/`pop_clip`, `push_clip_ellipse`, `push_clip_rounded_rect`, `push_opacity_layer`/`pop_opacity_layer`, `push_transform`/`pop_transform` (every `push_transform` overload lands in `push_matrix`, the one a backend implements) |
| Frame policy | `set_rasterization(SetRasterization)` |

Each call has a matching plain struct (`FillRoundedRect`, `DrawTextRun`, `PushClip`, ...), and
`RenderCommand` is a `std::variant` of all of them. That is the data form a test or a golden
file reads. `DrawTextRun` is not called `DrawText` because `<windows.h>` defines `DrawText` as a
macro.

What a control may emit, in practice:

- Only through this interface. A control that needs something the interface cannot express gets
  a new primitive, added to every backend (see
  [Add a Render Command](../hooks/add-a-render-command.md)). The interface grows only when a
  control genuinely needs it.
- Composite looks are built from existing primitives, not new ones. `draw_bevel_border()`
  (`render/Bevel.hpp`) draws the classic Windows 95/98/2000 `DrawEdge` border as four concentric
  rings of `stroke_line` calls; `draw_control_chrome()` (`render/Chrome.hpp`) draws a
  `ButtonVisual`'s fill, optional inner shadow or inset well, and exactly one border (a bevel,
  the elevation gradient, or a flat stroke) in a fixed priority order. `Button`, `CheckBox`,
  `ToggleSwitch`, `Slider`, `ComboBox`, `Expander`, the command-bar buttons and the item
  containers' multi-select box all share that one sequence.
- Borders are stroked *inside* their rect (`stroke_rounded_rect_inside()`), as XAML's `Border`
  does, so a control filling a clipping parent does not lose half its outline. The classic frames
  follow the same rule: `draw_win3x_border()` strokes its 1 px frame on a path pulled in by half a
  step. A caller that clips to its own rect (`SwipeControl` does) would otherwise cut the outer
  half of a centered stroke, and under the era's aliased, pixel-snapped rasterization the
  remaining half pixel rounds to nothing, so the frame vanishes.
- An `AcrylicBrush` fill in `draw_geometry` is the one order-dependent paint: it blurs whatever
  was drawn before it. `Window::render()` draws popups last, so a popup's acrylic sees the
  finished tree.

### Rasterization Is Theme Data

`SetRasterization` carries the era's edge policy: aliased or antialiased geometry; aliased,
grayscale or subpixel text; and whether to snap primitives to device pixels. `Window::render()`
emits it once, first, from `Theme::rasterization`. Because it is a command and not hidden
backend state, "does the Windows 95 theme ask for aliased rendering" is a semantic-golden
assertion that runs on any OS. Snapping happens in the backend at draw time, rounding to the
nearest *device* pixel, never in layout, so the logical model stays resolution-independent.

### What Window::render() Emits

Bottom to top:

1. `SetRasterization` from the theme.
2. The theme's window frame, when it draws one, with the content clipped to its content area.
3. The content tree, through `root.render(renderer, theme_)`, and the focus ring of the focused
   element in it, drawn after the tree.
4. Connected animations (`ConnectedAnimationService`), above the tree.
5. The popup layer: for each open popup, an opacity layer at the owner's
   `transition_opacity()`, its shadow (Fluent's faked drop shadow, Aero's drop shadow, or none,
   per `Theme::popup_shadow`), then the popup content.
6. An open modal: an opacity layer covering the full-client scrim (`ThemeColors::scrim`) and the
   dialog together, so the dimming fades with the dialog, and the popups opened from the modal
   over it.
7. A drag in progress: the dragged item's recording, at 0.8 opacity under the pointer, and the
   drag's caption.

The window background is not part of the stream. The host fills the client area before calling
`render()`.

### Repainting Only What Changed

A frame repaints only the areas that look different from the last frame. Nothing depends on a
control marking itself dirty for this: `Window::take_damage()` draws the whole frame into a
`PaintTracker` (`src/ui/PaintTracker.hpp`) that issues nothing and only measures. For each element
it hashes the commands the element itself emits, together with the transform, clip, opacity and
rasterization they are drawn under, and works out the area they cover in window-client DIPs, from
the commands' own rects and the current transform and clip. The element keeps the hash and the
areas (`painted_hash_`, `painted_own_`, `painted_area_`), so the next frame compares:

- a different hash damages where the element's own commands drew and where they draw now,
- a different list of children drawn, or another order, damages the whole subtree's old and new
  area (a removed child's pixels are inside the parent's old area),
- an element not drawn in the last frame damages where it draws now,
- what the window draws outside every element (popup shadows, the modal's scrim, a drag visual)
  is compared the same way.

The damage is clipped to the client area, merged where rects overlap, collapsed to one union past
four rects, and becomes the whole client area past 60% of it, for the first frame and after a
change of size or background.

The host then clears and draws each damage rect under an axis-aligned clip, through
`Window::render(renderer, clip)`. That draws through a culling `PaintTracker`: an element whose
area from the measuring pass, which ran over the same tree a moment earlier, misses the clip is
skipped with its whole subtree. The Win32 host creates its render target with
`D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS`, so the pixels outside the clip stay as the last frame left
them. Its own requests for a frame invalidate a single pixel (`request_frame()`), which `WM_PAINT`
tells apart from an update region the OS made (a resize, a restored window); those repaint
everything.

The measuring pass costs a walk of the tree and a hash per command, about what a `TestRenderer`
frame costs. What it saves is the Direct2D work for everything outside the damage: hovering one
button in a window of a thousand redraws that button and its neighbors. The `[perf-count]` and
`[damage]` tests check the damage of a hover and of a layout change, and that a frame drawn only
in its damage comes out pixel for pixel as a full frame does
(`OffscreenCapture::capture_changes()`).

In-app acrylic samples what is under it, up to 90 DIPs around its shape. The measuring pass
notes where each acrylic surface reads, and a damage rect that touches one grows to cover all of
it: the pixels kept from the last frame there are the acrylic's own, so the content under it has
to be drawn again before the blur can read it.

### Popup Shadows

`Renderer` has no blur primitive, so the Fluent drop shadow (`include/winui/render/OverlayShadow.hpp`)
is two of WinUI's `GetDropShadowRecipe` shadows, each drawn as a stack of up to eight nested flat
`fill_rounded_rect()` rings whose alphas composite to a Gaussian edge. A Gaussian straddles the
edge it blurs, so the composite is at about half the recipe's alpha exactly on the surface's
boundary, not at full alpha; full alpha there reads as a hard band about twice as dark. Each ring
is flat and aims at its midpoint, so the innermost ring carries the true value about a pixel out.
That averaging is on purpose.

The shadow is drawn at the radius the request's `PopupChrome::corner_radius` gives for the theme,
not always at `Theme::overlay_corner_radius`. A `ToolTip` rounds at its own tooltip radius
(Fluent's `ControlCornerRadius` of 4, against an overlay radius of 8; Luna's 0), which is control
chrome, so `ToolTipControl` asks for it in `ToolTip.cpp` through `detail::tooltip_radius()`. A
shadow at the wrong radius shows its corners past the surface. A bare `Popup` casts none
(`PopupChrome::shadow` off), since its child draws whatever chrome it has.

## Renderer Implementations

| Renderer | Where | What it does |
| --- | --- | --- |
| `TestRenderer` | `include/winui/testing/TestRenderer.hpp` | Appends one `RenderCommand` per call to `commands`. Images and SVGs are recorded by size only, never pixels. |
| `RenderRecording` | `include/winui/render/RenderRecording.hpp` | Keeps every call with full arguments so it can be `replay()`ed into any other renderer, any number of times. |
| `D2DRenderer` | `src/platform/windows/D2DRenderer.hpp` (private) | Draws into a render target it is handed, with Direct2D and DirectWrite. |

`TestRenderer` is the headless testing boundary. Any element can render into it without a window
and the result is deterministic:

```cpp
ui::ButtonControl button{"Hello"};
ui::testing::TestRenderer  renderer;
button.render(renderer); // Theme::default_light() by default

REQUIRE(renderer.count() == 3); // background, elevation border, label
REQUIRE(std::holds_alternative<ui::FillRoundedRect>(renderer.commands[0]));
REQUIRE(renderer.count_of<ui::DrawTextRun>() == 1);
```

`RenderRecording` is libwinui's snapshot of an element. A connected animation flies a recording
of its source, so the snapshot stays resolution-independent. The drag-and-drop visual under the pointer is a recording too.

`D2DRenderer` does not own its device or target. The Win32 host creates an
`ID2D1HwndRenderTarget` per window and one `D2DRenderer` for it, calls `begin_frame()` on every
`WM_PAINT`, and drops both when `EndDraw()` reports `D2DERR_RECREATE_TARGET` (device lost; they
are rebuilt on the next paint). `OffscreenCapture` keeps its target and renderer between captures
of the same size the same way. A few things it does that a contributor tends to trip over:

- Nothing is created per frame for a frame that looks like the last one. Solid brushes are
  cached by color; gradient stop collections and their brushes by the stops, with the points or
  transform set on each use; path geometries by their points; stroke styles by their properties;
  uploaded bitmaps by `Bitmap::content_id()`; layers come from a pool. `begin_frame()` ages the
  caches and drops what has not been drawn for a while once one has grown. The
  `[perf-count]` test "Drawing an unchanged frame through Direct2D creates no text, brush or
  geometry" holds it to that.
- `set_rasterization()` maps the policy onto `SetAntialiasMode` and `SetTextAntialiasMode` and
  turns device-pixel snapping on or off for the frame. Text layout boxes are not snapped, since
  shaving a fraction off a tight width would re-wrap text that fitted.
- `push_opacity_layer(1.0f)` pushes no real `ID2D1Layer` (it still records an entry so the pop
  stays balanced). Every popup and modal renders through one every frame, and almost none of
  them are mid-fade.
- `draw_text()` draws a text layout from the process-wide cache in `DWriteCache.hpp`, keyed by
  the string, the style and the box, and the formats in that cache are the ones `DWriteMeasurer`
  measures with, so measured and drawn text agree. A wrapping run still gets half a DIP of extra layout width: it is usually drawn into a
  box exactly as wide as its measured widest line, and a sub-pixel disagreement between measure
  and draw would break a line one word early. A non-wrapping style sets
  `DWRITE_WORD_WRAPPING_NO_WRAP` explicitly, because `IDWriteTextFormat` defaults to wrapping and a
  box sized to the exact natural width (worst at fractional sizes like Metro's 14.667 px) would
  otherwise push a trailing word onto a clipped second line.
- `stroke_line()` snaps an axis-aligned stroke as a band by its two edges, the same way a filled
  rect is snapped, not by its centerline. A classic bevel puts its far rings at
  `bounds.right() - width / 2`, and on a control with a fractional width a snapped centerline can
  land a pixel inside the fill, leaving the light top and left edges overhanging as a 1 px spur at
  every corner. A band thinner than a device pixel gets one pixel instead of snapping away.
- `push_transform()` composes onto the current transform, so a zoomed `Scroll` inside another
  magnifies twice. Snapping stays in untransformed DIPs, which under a scale no longer land on
  device pixels. That is accepted, since a zoomed view is a magnified raster anyway.
- A `Bitmap` in `PixelFormat::Rgba8` stores straight alpha, but `CreateBitmap` rejects
  `D2D1_ALPHA_MODE_STRAIGHT` with initial pixel data (`D2DERR_UNSUPPORTED_PIXEL_FORMAT`), so its
  upload premultiplies. One in `Bgra8Premultiplied` goes up as B8G8R8A8 unchanged. `draw_image()` and `draw_image_rect()` keep the upload under the
  bitmap's `content_id()`, which a copy shares and any write changes, within a 64 MB budget;
  image-pack bitmaps are cached by name for as long as the renderer lives.
- `stroke_wavy_line()` (the spelling squiggle) is a chain of quadratic Béziers, one per half
  period on alternating sides, with the control point at twice the amplitude so the apex lands
  exactly `amplitude` off the axis. The last half period is shortened to end on `to`.
- Acrylic is done in-app, as an `AcrylicBrush` effect graph: the pixels already under the
  shape (its bounds grown by three standard deviations, clamped to the target) are copied with
  `CopyFromRenderTarget`, composited over the opaque fallback, Gaussian-blurred with a standard
  deviation of 30 (WinUI's `sc_blurRadius`), blended with the luminosity color in Luminosity mode
  and the tint in Color mode, and used to fill the geometry. `CopyFromRenderTarget` fails while a
  clip or layer is pushed, so the renderer tracks its pushes, pops them all, copies, and pushes
  them again. An opaque tint just fills with the tint, and the 2 % noise layer is not drawn.
  Effects need `ID2D1DeviceContext` (the window and offscreen targets both have it); without it
  the fallback color fills.
- `draw_image_nine_slice()` looks the name up in an optional `ImageThemePack` (see
  [Theming](theming.md#the-image-theme-backend)) and draws nothing when there is none, which is
  always the case today.

## Images, Icons and Geometry

### Bitmap

`Bitmap` (`include/winui/render/Bitmap.hpp`) is straight-alpha RGBA by default
(`PixelFormat::Rgba8`), or premultiplied BGRA (`PixelFormat::Bgra8Premultiplied`), and it has two
codec paths on purpose:

| Calls | Backed by | Guarantee | Used for |
| --- | --- | --- | --- |
| `decode_png()`, `read_png()`, `encode_png()` | stb on every platform | byte-identical everywhere | the golden tests |
| `decode()`, `read()`, `read_thumbnail()` | WIC on Windows (every installed codec: TIFF, BMP, HEIC, WebP, ...), stb elsewhere (PNG, JPEG, GIF) | may differ by a few levels between platforms | app pictures |

The app path can shrink while decoding and applies EXIF orientation by default; decode bounds
apply to the upright picture. On Windows `WicDecoder.cpp` builds one chain and pulls it once:
frame, then scaler, then an RGBA format converter.

- **Scaling comes first**, so the JPEG codec answers with its DCT-domain reduction and a thumbnail
  of a large photo never decodes the full frame.
- **The converter comes last.** A WIC scaler picks its own output format (asked for 32bppRGBA it
  reports 32bppBGRA), so a conversion placed before it swaps red and blue. The output format of
  the last stage is checked. Only a scaled decode goes through the scaler, so a test has to decode
  scaled to catch this.
- **Orientation** is read from EXIF (tag 274, under `/app1/ifd` in a JPEG, at the top level in a
  TIFF) but applied afterwards by `Bitmap::oriented()` on the already small result. A WIC
  flip-rotator in the chain would re-run the lazy decode-and-scale for nearly every row.
- **COM** is initialized per call, because `Bitmap::read()` is meant for app worker threads, and
  asking the app to call `CoInitializeEx` would put `<windows.h>` back into its code. `S_FALSE` and
  `RPC_E_CHANGED_MODE` both mean COM is usable.

`tests/test_image_decode.cpp` therefore samples the middle of flat color blocks with a tolerance
instead of comparing bytes. The transforms (`fit_within()`, `scaled_to_fit()`, `oriented()`,
`read_orientation()`) are exact and tested exactly, and `read()` is tested with a path outside
the ANSI code page.

### SvgImage and IconSource

`SvgImage` (`render/SvgImage.hpp`) keeps the markup as text and hands it to Direct2D whole
(`ID2D1DeviceContext5::CreateSvgDocument`, Windows 10 1703 and later), which is also what WinUI's
`SvgImageSource` uses, so the same SVG subset works. Its natural size comes from the root's
`width` and `height`, falling back to `viewBox` (keeping its aspect ratio when only one is
given); with neither it draws at whatever size it is given. The markup string is shared between
copies, so a spec, its control and a page's copy all point at one string.

Every `IconSource` holder draws its icon in a ScaleToFit: the icon's
natural size scaled uniformly to fit the slot and centered. A glyph's natural size is its measured
text box, a path's is its geometry's bottom-right corner (as a path icon measures), a
picture's is its pixel size.

### Geometry

Every geometry (a line, a rectangle, an ellipse, a path or a group of them) is one `PathGeometry`
class (`render/PathGeometry.hpp`): a list of figures with one fill rule. `PathGeometry::group()`
concatenates figures, which is all a geometry group is to Direct2D. Arcs (path markup `A`, rounded corners, ellipses) become cubic Béziers when the
geometry is built, so a segment is only ever a line, a quadratic or a cubic. A quarter circle as
one cubic is off by at most 0.027 % of its radius.

`PathGeometry::parse()` reads the "Move and draw commands" path markup: an optional `F0`/`F1`,
then `M L H V C Q S T A Z` in absolute and relative forms, with repeated operands repeating the
command (after `M`, further pairs are lines). It returns `std::nullopt` for malformed markup.
How a `Shape` lays its geometry out is on [Layout](layout.md#shapes).

## describe()

`include/winui/render/Describe.hpp` turns a command stream into stable text: one line per
command, fields in a fixed order, floats at two decimals, colors as `#RRGGBBAA`, images and SVGs
by size. `ui::testing::describe(window)` (`include/winui/testing/TestRenderer.hpp`, in the
`winui::testing` library) renders a laid-out window through a `TestRenderer` and describes it.

```text
SetRasterization geometry=antialiased text=grayscale snap=false
FillRoundedRect rect=(94.00,30.00,52.80,29.90) radius=4.00 color=#FFFFFFB3
StrokeRoundedRectGradient rect=(94.50,30.50,51.80,28.90) radius=3.50 extent=-3.00 width=1.00 stops=[0.33:#00000029 1.00:#0000000F]
DrawTextRun rect=(94.00,30.00,52.80,29.90) color=#000000E4 size=14.00 weight=regular halign=center valign=center text="Save"
```

That is `tests/golden/button-normal.txt`. The `.txt` goldens are the **semantic** layer: laid out
with `HeadlessTextMeasurer`, so they do not depend on resolution, fonts or the OS. They are the
primary, low-fragility regression check and run on every platform. A change to what a control
draws shows up as a readable line diff.

`describe_automation()` (`ui/AutomationTree.hpp`) is the same idea for the accessibility tree.

## OffscreenCapture

`ui::testing::OffscreenCapture` (`include/winui/testing/OffscreenCapture.hpp`, implemented in
`src/testing/OffscreenCapture.cpp`, in the `winui::testing` library) renders a laid-out `Window` into an RGBA
`Bitmap` through the shipping path: the same `D2DRenderer` and DirectWrite the on-screen window
uses, not a stand-in rasterizer.

```cpp
ui::testing::OffscreenCapture d2d;
ui::Window              window;
window.set_content(ui::Button{.text = "Hello"});
const ui::Size client{200, 60};
window.layout(client, d2d.measurer()); // DirectWrite metrics, to match what capture() paints
const ui::Bitmap image = d2d.capture(window, client, ui::Theme::light().colors().background);
```

It draws into a software WIC bitmap render target at 96 DPI (so DIPs map 1:1 to pixels), with no
GPU and no HWND, and sets grayscale text anti-aliasing on the target before drawing, so the
result does not depend on the monitor's ClearType geometry. That makes it stable run to run.

The `.png` goldens are the **pixel** layer. They trade cross-platform reproducibility for
fidelity to the real renderer on purpose, so they are Windows-only. `tests/test_golden.cpp`
compares with a per-channel threshold of 1 and requires zero mismatched pixels; that small
tolerance only guards against rounding differences between DirectWrite and Direct2D versions. Set
`LIBWINUI_UPDATE_GOLDEN=1` to regenerate either layer, then read the diff before you keep it. See
[Write Tests](../development/write-tests.md).

## Composition

Motion in libwinui runs on four mechanisms. Which one a control uses depends on what moves.

### The Compositor Seam

`src/render/Compositor.hpp`, private to libwinui, is for state-transition animations that should not cost
the UI thread a frame at a time. A control hands the compositor a property animation (fade,
offset, scale, anchored stretch) on a *state change*, and the compositor interpolates it on its
own. `Button`'s, shortened:

```cpp
void ButtonControl::sync_composition_override(detail::Compositor& compositor, const Theme& theme)
{
    // Under a popup, a clip or a transform, render_override() draws the wash instead.
    if (detail::TreeAccess::visuals_covered(*this))
    {
        compositor.remove_visual(visual_id());
        composed_hot_ = false;
        return;
    }
    const bool now_hot = washed(theme); // hovered or pressed (Metro: hover only)
    compositor.sync_visual(visual_id(), bounds(), theme.resolved_button_corner_radius(), colors::White);
    if (now_hot == composed_hot_)
    {
        return; // only spend an animation on an actual flip
    }
    compositor.animate_opacity(visual_id(), composed_hot_ ? ButtonHotOverlayOpacity : 0.0f,
                               now_hot ? ButtonHotOverlayOpacity : 0.0f, theme.anim_faster);
    composed_hot_ = now_hot;
}
```

- `Window` calls `Element::sync_composition()` tree-wide after layout, after every input event
  and after focus moves. Containers recurse; a leaf compares its current state with the one it
  last composed and only animates on a flip.
- Each overlay is keyed by the element's `VisualId`. `sync_visual()` is idempotent and always
  places the visual at its final bounds immediately.
- On Win32 the implementation is `DCompCompositor` (`src/platform/windows/DCompCompositor.hpp`):
  a DirectComposition target bound to the same HWND with `topmost = TRUE`, holding solid-color
  rounded-rect overlay visuals on top of the Direct2D content. The D2D pipeline is untouched.
  If D3D11 or DirectComposition is unavailable, `create()` returns null and the window runs
  without a compositor. The window holds a `LazyCompositor` in front of it: making the D3D11
  device and DirectComposition costs over 100 ms, so the overlays the tree describes are only
  remembered until the message loop first runs out of messages (after the first frame) or the
  first animation asks for one, whichever comes first, and are then replayed.
- With no compositor installed (the headless host, a failed `create()`), no calls are made and
  controls show their current state through the ordinary repaint, just without the fade.
- `TestCompositor` (`src/testing/TestCompositor.hpp`, for libwinui's own tests) records every call for assertions.

`Button`, `CheckBox`, `RadioButton` and `ToggleSwitch` use it for their hover wash. The compositor
only composites overlays on top of the rendered content; it never moves the content itself. A part
that moves or resizes (`ToggleSwitch`'s knob, `RadioButton`'s dot, `Slider`'s inner circle) is
drawn by `render()` where a `detail::RectTween` has it: `sync_composition_override()` aims the tween
at the size or place the state asks for, `tick_override()` moves it on, and `render()` draws it.
With no compositor nothing aims the tween, so the part is drawn where its state puts it.

A few DirectComposition details in `DCompCompositor` that are easy to break:

- The device is an `IDCompositionDesktopDevice` from `DCompositionCreateDevice2`, the one
  interface with both the desktop-only `CreateTargetForHwnd` and the newer `CreateVisual`,
  `CreateSurface` and `CreateAnimation`. Overlays are `IDCompositionVisual3`, the first visual
  interface with `SetOpacity`.
- Every overlay is created at opacity 0, so a wash shows only once `animate_opacity()` fades it in.
- DirectComposition animations are time polynomials, not Bézier key splines. The opacity fade
  is linear, so it is one straight segment.

### Compositor Layers

A motion that never stops, such as an indeterminate `ProgressRing` or `ProgressBar`, runs on a
layer: a compositor visual with content of its own that the compositor animates while the UI
thread draws nothing. An element asks for layers through four virtuals on `Element`:

- `layer_count()` says how many layers it wants right now, 0 when it wants none.
- `layer_placement(i)` says where layer `i` sits and what clips it, in the element's coordinates.
- `render_layer(i, renderer, theme)` draws the layer's content at rest, with primitives.
- `animate_layers(compositor, ids, elapsed)` starts the layers' `LayerAnimation`s (key frames
  of offset, scale, rotation, opacity or clip edges, once or forever).

`Window` does the rest in `take_damage()`. The measuring pass (`PaintTracker`) probes every element
with layers: it records each layer's content into a `RenderRecording` and hashes it, notes the
transform, clip and opacity the element is drawn under, and marks the probe occluded when anything
drawn later in the frame covers the layers' area (a sibling, a popup, the modal's scrim). Then
`Window::sync_layers()` decides, element by element:

- The layers run when there is a compositor whose `supports_layers()` is true, animations are on,
  nothing covers them, and the element and its ancestors are neither scaled, rotated nor
  translucent. `layers_composed()` turns true, `render_override()` leaves out what the layers show,
  and `tick_override()` stops marking the element dirty. The window then wants no ticks and draws no
  frames for it.
- Otherwise the element draws the motion itself, exactly as without a compositor.
- Handing the drawing over in either direction damages the element's area, so the same frame
  shows the change. `layers_stopped(elapsed)` tells the element how long the compositor ran
  the motion, and the control's own phase clock continues from there. `Compositor::now()` is the
  clock both sides time it with.
- A layer is synced again only when it moves, painted again only when its content hash changes,
  and its animations restart only when its size changes. A layer clipped entirely out of view
  (scrolled away) is removed, but the element stays composed, so it costs nothing until it is
  scrolled back. The layers of an element that is not drawn at all (collapsed, removed, destroyed)
  are removed after the frame. The registry holds an `ElementWatch`, so a destroyed element is
  never touched.

`DCompCompositor` builds each layer from three visuals under one `layers_root()`, which is the
DirectComposition root's first child, so every overlay sits above every layer:

```
root
├── layers_root
│   └── clip visual        window pixels, clipped to the placement's clip
│       └── frame visual   offset to the surface's top-left pixel, clipped by the animated clip
│           └── content    the surface, under scale, rotation, translation and opacity
└── overlays (sync_visual), in the order they were made
```

The surface starts on a whole pixel two pixels before the layer's bounds, and the content is
drawn shifted by the fraction left over, so a layer lands on the same pixels the frame would have
drawn it on. It is drawn again on a new size, a new fraction of a pixel or a new DPI, and the
running animations are then handed over again from where they are. Key frames become
DirectComposition segments: a linear frame is one straight segment, a discrete frame a hold and a
jump, a curved frame sixteen straight pieces, and a repeating animation is its first partial
cycle, one whole cycle and an `AddRepeat()` of it.

`TestCompositor` records `SyncLayer`, `PaintLayer` (the content replayed into a `TestRenderer`),
`AnimateLayer`, `StopLayerAnimations` and `RemoveLayer`, and its clock moves only with `advance()`.

### On Screen

An element whose drawing moves on its own can ask whether anyone sees it, to stand still when
nobody does. It calls `set_screen_tracking(true)`, and from then on the measuring pass lists it
whenever it is drawn. After the pass `Window::sync_screen()` compares the area the element and its
children drew (`painted_area_`, already cut to every clip above it) with the client area:

- Some of it inside: `on_screen()` is true.
- None of it inside, which is what scrolling out of view or clipping away leaves, or not drawn at
  all (collapsed, fully transparent, removed, inside something that skipped it): `on_screen()` is
  false.
- When that changes, the element gets `on_screen_changed(bool)` after the frame. Every state is
  settled before the first call, and the calls go through an `ElementWatch`, so a handler may
  change the tree.

The Window keeps the tracked elements the last frame found on screen, which is how it notices one
that is no longer drawn. An element no frame has measured yet counts as on screen, so a control
used without a Window behaves as before, and an element that does not track costs the pass one
flag test. Occlusion does not count: a tracked element under a popup is still on screen.

`ImageControl` tracks while its source is animated. Off screen its `tick_override()` neither advances
nor asks for a tick, so the window sleeps; back on screen it goes on from the frame it stopped on,
with what was left of that frame's delay, and asks for a tick at once (`request_tick_in(0)`).

`AnimatedVisualPlayerControl` tracks while a source is loaded, and keeps real time instead, since
a play has a position the app can read and an end it waits for. Off screen its `tick_override()` still
moves the play's clock with every tick the window gets, but marks nothing dirty, so the window
sleeps. A play that is not looped asks for one wake at the time left to its end
(`request_tick_in()`, at the playback rate) and completes then. Back on screen it marks itself
dirty, so the frame shows the progress real time reached, a looped play wrapped however long it
was away, and it asks for a tick at once to go on animating.

### The Element Visual Layer

`Element` carries an opacity, translation, rotation, scale and center point (the
`AnimatedProperty` values `Opacity`, `Translation`, `Rotation`, `Scale` and `CenterPoint`), plus
everything that animates them (`include/winui/ui/Animation.hpp`):

- Storyboard-style animations: `DoubleAnimation`, `KeyFrameAnimation` with eleven
  easing functions, started with `Element::start_animation(AnimatedProperty, Animation)`.
- Composition-style ones: `SpringAnimation`, and `ExpressionAnimation`, which is a C++ callable
  evaluated every frame instead of an expression string.
- Implicit transitions (`set_opacity_transition()` and friends): set once, every later change
  animates from the value on screen.
- Transitions (`EntranceTransition`, `RepositionTransition`, ...) that fire from
  layout: the first arrange is an element's load, a moved arrange its reposition.

There is no dependency-property system, so an animation never targets a property by name. These
run on the CPU: `Window::tick()` advances them through `Element::tick()`, and `Element::render()`
draws the element through `push_transform()` and `push_opacity_layer()`. Pointer input is mapped
back through the same transform, so a moved element is hit where it is drawn. The state lives in
the lazily allocated `ElementMotion` block, so an element at rest emits no extra command.

### Owner-Driven Fades

Popup and modal open/close fades (`Flyout`, `DialogControl`) are a plain `tick_override()` phase clock
on the owner. `Window::render()` wraps the owner's whole block in an opacity layer at
`transition_opacity()`. Controls that animate a *layout* value (an `Expander`'s content slide)
sample the curve themselves with `easing::evaluate()`, which reads the same `CubicBezier` a
compositor would be handed.

!!! note "What Composition Does Not Do Yet"
    The compositor carries overlay visuals and the layers of elements that ask for them. Every
    other piece of content is drawn by Direct2D on the UI thread, and the element visual layer
    animates by redrawing. A retained compositor tree that owns all element content, so that most
    motion costs no UI-thread frames, is a design direction and not implemented.
