# Layout

Two passes, one measurer interface, and the sizing rules applied once in `Element` instead of in
every control.

## Measure and Arrange

Layout is the classic two-pass model WinUI and WPF use.

**Measure** asks each element how big it wants to be. `Element::measure(available, measurer,
theme)` returns the desired size including margin, and caches the core result for arrange. A
container's `measure_override()` measures each child with whatever constraint its own layout rule
gives that child, and usually caches the answers.

**Arrange** hands each element its final rect. `Element::arrange(final_rect)` settles the
element's own `bounds()` from the rect, its margin and its alignment, then calls `arrange_override()`
so a container can place its children, typically from the sizes it cached during measure.

A few conventions hold across the whole tree:

- **Infinity means unbounded.** An available extent of `ui::Unbounded` (+infinity) on an axis
  means "as much as you like", as in WinUI; 0 is no room at all. A `StackControl` measures its
  children with `Unbounded` along the stack direction, and a vertical `Scroll` measures its
  child with `Unbounded` for the height. `Element::measure()` keeps an unbounded axis unbounded
  through margins, and a bounded axis that margins use up becomes 0. A desired size is always
  finite. The `Layout` contexts used by `ForEach` and `LayoutPanel` share the same convention.
- **Bounds are window-absolute.** Every `arrange()` passes a rect in window-client DIPs, not
  relative to the parent. `bounds()` is always in the same space as a `PointerEvent::position`,
  and `actual_offset()` subtracts the parent's position when a relative one is needed.
- **Measure before arrange.** `arrange()` relies on the size the last `measure()` cached. A
  container whose cache does not match its children (arrange without measure) does nothing
  rather than reading stale data.
- **DIPs everywhere.** Layout never sees physical pixels. `StackControl` and `Window` round
  positions to whole DIPs for crisp edges; snapping to *device* pixels is a rasterization policy
  the renderer applies (see [Rendering](rendering.md#render-commands)).
- **`measure_override()` is `const`.** A control caches measure results in `mutable` members. That is
  also what makes parallel measuring of *independent* elements possible.
- **A measure depends on its inputs and its flags only.** `measure()` returns its last answer
  without calling `measure_override()` when it is offered the same size, measurer and theme and
  nothing at or below the element was marked `DirtyFlags::Measure` since. `arrange()` does nothing
  for the rect it was last given unless something at or below was measured or marked
  `DirtyFlags::Arrange`. So a setter whose value changes the size calls `invalidate_measure()`
  (`mark_dirty(DirtyFlags::Layout)`), one that only moves things within the space they have calls
  `invalidate_arrange()`, and one that only changes the look marks `DirtyFlags::Visual`.

## Invalidation

`mark_dirty()` sets flags on the element and bubbles them to every ancestor. On the element itself
`Measure` and `Arrange` also mean "my last result is no longer good"; on an ancestor they only
mean "something below me needs it". That split is what lets a change stay local.

`Window::layout(client, measurer)` drives the whole thing:

1. Skip entirely while the window is minimized, and do nothing at all when no flag is set
   anywhere, the size is the same and no root, popup or modal came or went.
2. If something is `DirtyFlags::Layout` (or the size changed, or this is the first pass), lay out:
    - **From the dirty elements up**, when the size, theme and measurer are the ones of the last
      pass. Each element whose own measure or arrange is out of date is measured again with the
      space it was last offered. If its size came out the same, it is arranged again in its old
      rect and the change stops there. If not, its parent is measured again the same way, and so
      on. A change that reaches the root, or an element its parent measured with two different
      sizes in the last pass (a `Grid` auto track), falls back to the full pass.
    - **The full pass** otherwise: measure the content with the client size (inside the theme's
      window frame when there is one) and arrange it at the full client size, where its own alignment, margin and max size place it.
      `set_stretch_content(false)` arranges it at its desired size instead, centered in the client
      area (rounded to whole pixels). The per-element caches make the clean parts of the tree
      cost one comparison each.
3. Clear the dirty flags (the walk stops at a clean element, whose subtree is clean too), rebuild
   the tab ring.
4. Measure and place open popups and the modal.
5. Let connected animations snapshot their destinations, then `sync_composition()` and
   `sync_native()`.

The `VisualTree` is built when `Window::visual_tree()` is asked for, not by every pass. The
`[perf-count]` tests in `tests/test_perf_count.cpp` hold the counts: a label changed inside a
fixed-size cell measures the label and the cell and nothing else.

## The TextMeasurer Seam

Text is the only thing in layout that needs a font engine, so measuring text goes through an
interface (`include/winui/render/Text.hpp`):

```cpp
class TextMeasurer
{
public:
    virtual ~TextMeasurer() = default;
    [[nodiscard]] virtual TextMetrics measure(std::string_view text, const TextStyle& style) const = 0;
    [[nodiscard]] virtual TextMetrics measure_wrapped(std::string_view text, const TextStyle& style, float max_width) const;
    // ...
};
```

`TextMetrics` is the extent (`width`, `height`), the first line's `baseline` and the
`line_count`, and converts to a `Size`. `Element::measure()` takes a measurer and passes it down
the tree. There are two implementations:

| Measurer | Where | Behavior |
| --- | --- | --- |
| `HeadlessTextMeasurer` | `include/winui/testing/HeadlessUI.hpp` | Every code point advances `font_size * AdvanceRatio` (nudged by weight and character spacing); a line is `font_size * LineRatio` tall unless the style sets a line height. Deterministic on every OS. |
| `DWriteMeasurer` | `src/platform/windows/DWriteMeasurer.hpp` (private) | Real DirectWrite layout. Text formats come from a process-wide cache (`DWriteCache.hpp`) that `D2DRenderer::draw_text()` draws with too, so what is measured and what is drawn agree. Measurements are cached by string, style and width. |

The same layout code runs both ways. The semantic goldens and most layout and focus tests use
the headless measurer, so they are stable in CI and on any machine. The pixel goldens lay out
with DirectWrite (`OffscreenCapture::measurer()`) because they compare against what DirectWrite
actually paints.

Keep in mind that the headless measurer is not an approximation of DirectWrite. Do not write a
test that expects a headless width to match a real one.

`DWriteMeasurer` has two rules worth knowing when a label moves by a pixel:

- **Wrapping never moves a label that fits.** `measure_wrapped()` first measures the text on one
  line; if it fits and holds no `\n`, that result is returned unchanged. Only text that really
  breaks goes through a wrapping `IDWriteTextLayout`. A broken line's trailing space hangs past
  the layout width, so a wrapped block is as wide as its widest line without it, while no-wrap
  text measures including trailing whitespace.
- **An explicit `line_height`** (Metro puts every run on one) uses
  `DWRITE_LINE_SPACING_METHOD_UNIFORM` with the baseline at 0.8 of the line box, roughly where a
  Latin baseline falls. The format is set once in `DWriteCache.cpp`, and `D2DRenderer` draws with
  the same cached format.

## Text Layout

A plain label is one `measure()` call. Anything with several runs (TextControl inlines,
`RichText`, a multi-line `TextBox`) goes through `TextLayout`
(`include/winui/render/TextLayout.hpp`), which breaks paragraphs of styled runs into lines.

It is built only on `TextMeasurer::measure()`, so it runs headless exactly as it does on
DirectWrite. Its line breaking follows DirectWrite's word wrapping:

- Lines break greedily after a run of spaces, and a trailing space hangs off the line end.
- Under `TextWrapping::Wrap` a word longer than a line is split between characters; under
  `WrapWholeWords` it overflows.
- A `\n` inside a run is a hard break (XAML's `LineBreak`), and a word spanning two runs stays
  one word.
- Each piece between two break opportunities is measured once and cached, so a relayout at
  another width measures nothing.

Offsets address `text()`: every run's text in order, paragraphs joined by one `\n`. One known
limitation: mixed font sizes on one line share a bottom edge, not a baseline, because the
measurer interface reports no ascent.

Wrapping needs a width to wrap at. A `TextControl` only wraps under a bounded width, so a wrapping
`TextControl` in a horizontal `StackControl` (which offers its children an unbounded width) stays on
one line, as in XAML. On a bounded axis a `TextControl` never asks for more than it was offered: an
unwrapped run is clipped, and an over-long word under `WrapWholeWords` overflows the same way.

## Alignment, Margin and Minimum Size

The sizing properties every control has (`margin`, `width`, `height`, `min_*`, `max_*`,
`horizontal_alignment`, `vertical_alignment`, plus `visible`) are applied once, in
`Element::measure()` and `Element::arrange()`. A control's `measure_override()` never sees them:

```text
measure(available, context):
    Collapsed?            -> desired = 0, return 0
    reserve the margin on both sides
    constrain the offer to [min, max] per axis     (explicit width/height narrows the range)
    core = measure_override(offer, context)        (under its own theme, if it asks for one)
    clamp core to [min, max]; cache it as desired_
    return core + margin

arrange(final_rect):
    Collapsed?            -> bounds = empty rect at final_rect's origin
    inner = final_rect minus margin
    Stretch: width = min(inner, max(desired, max_width)),  else min(desired, inner)
    position by alignment (a Stretch element narrower than its slot is centered)
    bounds_ = that rect; arrange_override(bounds_, the last measure's measurer and theme)
```

The min/max folding follows WinUI's `CFrameworkElement::GetMinMax()`: an explicit size wins over
the content, `min_width` wins over the explicit size and over `max_width`, and an unbounded offer
with a finite max is offered the max.

`Visibility::Collapsed` takes no room, draws nothing, takes no input and leaves the tab ring,
along with everything under it.

### No Room for the Focus Ring

Layout never depends on focus. The window draws the focus ring over everything once a root is
drawn, so the Fluent rings drawn outside a control lie over its neighbors rather than under
them, and the default spacing between controls leaves them room. An item inside a viewport rings
inside itself instead (see [Input and focus](input-and-focus.md#focusable-and-the-focus-ring)).

### Requested Theme

An element with `set_requested_theme(ElementTheme::Dark)` (or `Light`) measures and draws its
subtree with `Theme::with_dark_mode(dark)` instead of the theme it was handed. The derived theme
is cached on the element and rebuilt when the handed theme's `generation` changes. Both
`measure()` and `render()` apply it, so a subtree lays out with the same metrics it draws with.
See [Theming](theming.md#light-dark-and-requested-themes).

## Containers

A container is an `Element` that owns children and overrides `measure_override()`/`arrange_override()`
to call their `measure()`/`arrange()`, and `render_override()`/`pointer_override()` to call their
`render()`/`handle_pointer()`. It lists its children in `for_each_child()`, which the tree-wide
walks (`collect_focusables()`, `clear_dirty()`, `tick()`, `sync_composition()`, `sync_native()`,
the window's walk for popups, `automation_children()`, `build_visual_tree()`) recurse through by
default; see [The Tree](the-tree.md#element). The main ones:

| Container | Header | Measure rule | Arrange rule |
| --- | --- | --- | --- |
| `StackControl` | `ui/panels/Stack.hpp` | Each child gets the panel's cross-axis extent and `Unbounded` along the stack; results are cached; spacing added between children. | Children placed one after another at their cached main-axis size; the cross axis follows the panel's `child_alignment`. |
| `GridControl` | `ui/panels/Grid.hpp` | Dip tracks first, then Auto tracks from their children's natural size, then Fraction tracks share what is left, each track kept within its `minmax()` limits; Fraction-track children are measured at their resolved size (one that sizes no Auto track only then). On an unbounded axis a Fraction track behaves like Auto. The border and padding come off first. | Tracks are re-resolved against the final rect, so a Fraction track is a share of the final extent. |
| `ScrollControl` | `ui/scrolling/Scroll.hpp` | The child gets the viewport width (vertical orientation) and `Unbounded` for the height; its desired size is the extent. | The child is arranged at its extent, offset by the scroll position; it is drawn through a clip and, when zoomed, a `push_transform()`. |
| `VirtualListControl` | `ui/collections/VirtualListControl.hpp` | Fixed row height; only rows inside the viewport exist. | Rows are created by an item factory when they scroll in and destroyed when they scroll out. |
| `Panel` subclasses | `ui/panels/Panel.hpp` | `Canvas`, `WrapPanel`, `TilePanel`, `RelativePanel` and `LayoutPanel` share child storage, border and padding. | Children draw in `render_order()` (insertion order unless the panel says otherwise). |
| `LayoutPanel`, `ForEach` | `ui/panels/LayoutPanel.hpp`, `ui/collections/ForEach.hpp` | Delegate to an attachable `Layout` (`StackLayoutEngine`, `TileLayoutEngine`, `WrapLayoutEngine`, `LinedFlowLayoutEngine`) through a layout context. | `ForEach` with `.virtualize` realizes only what falls inside the viewport plus a cache buffer. |
| `ContentHost` | `ui/hosting/ContentHost.hpp` | Pure forwarding to its one child. | The child can be swapped at runtime (`ui::Match`). |

Known simplifications, stated in their headers: `Grid` only sizes Auto tracks from children with
a span of 1, and `VirtualListControl` recreates rows rather than recycling them, so a row gets a fresh
`VisualId` every time it scrolls back in.

### Grid

`Grid` measures each child against its own tracks, never against the whole Grid's `available`.
Measuring against the Grid made a `TextControl` in a 60 DIP Dip column report the width of a
several-hundred-DIP single line, and every number downstream of it (cell alignment, a
neighboring Auto track, the Grid's desired size) was impossible.

Columns are resolved before rows, the order `CGrid::MeasureOverride` uses for its cell groups.
Once the Fraction columns have their widths, a child in a Fraction column is measured again at its
column's real width, and only that height may size an Auto row. A wrapping `TextControl` in a Fraction
column and an Auto row therefore sizes the row by its wrapped height, not by its one-line
unbounded height. A child that sizes no Auto track and sits in a Fraction track is measured only
once, at its resolved size: measuring it unbounded first would make a virtualized list measure
every row, and each Grid nested in a Fraction track would double the measures below it. A Grid
inside an unconstrained container (a horizontal `StackControl`, an unbounded `Scroll`) sizes to
its content and splits that width across its Fraction tracks at arrange time.

### Popups and Modals

A control that floats part of itself above the tree (a `ComboBox`'s list, a `MenuBar`'s menus,
a `Flyout`, a `ToolTip`) is not laid out inline. It opens a popup with `open_popup()`, and
`Window::layout()` measures and arranges the request's `content` separately: against the
request's `anchor` (the owner by default, or `anchor_rect` inside it), on the side `placement`
asks for, flipped to the opposite side when there is no room and clamped to the client area. An
owner whose anchor moves sends the request again from `arrange_override()`. `Popup` and
`TeachingTip` place their own content through the request's `place` callback. A modal (`show_modal()`, usually a `DialogControl`) is measured
at the client size and centered. [Input and Focus](input-and-focus.md#popups-and-modals) covers
how input reaches them.

Popups nest. A cascading menu is the main user: `MenuPresenter` (`src/ui/menus/MenuPresenter.hpp`,
private) is one level of a menu, WinUI's `MenuFlyoutPresenter` with its items and
`CascadingMenuHelper` folded in, shared by `MenuFlyout`, `MenuBar` and `CommandBar`'s overflow. It
draws and hit-tests straight from the owner's `std::vector<MenuItem>`, and each presenter
is the popup owner of the submenu open from it, so a cascade is a chain of presenters each
floating the next. The root presenter takes the keys for the whole chain (`route_key()`) and
hands them to the deepest open level. Submenu presenters are pooled per item index and live as long as their parent level,
because the window's popup list is only rebuilt on layout and may still point at one that has
just closed.

## Virtualizing Layouts

`LayoutPanel` and `ForEach` do not lay out their children themselves. They hand the job to
an attachable `Layout` (`include/winui/ui/layouts/Layout.hpp`), ported from WinUI's
`controls/dev/Repeater/`.

### Layouts and Their Contexts

A layout never measures an element directly. It goes through a layout context
(`NonVirtualizingLayoutContext` or `VirtualizingLayoutContext`), because an element needs the
host's measurer and theme to be measured at all, and the context also remembers each element's
desired size. Contexts work in coordinates relative to the host's content box, and an unbounded
extent is infinity there, as it is everywhere else in the tree.

A `Layout` is not copyable and belongs to exactly one host, which takes it by `std::unique_ptr`
(`set_layout()`). Any state it needs between passes lives in its own members, so there is no
per-context layout state object. A layout whose own properties change calls `invalidate_measure()`, which
re-lays out the host.

In a virtualizing context, an element the layout does not ask for during a measure pass is
cleared by the host afterwards. A layout that wants to manage an element itself realizes it with
`ElementRealizationOptions::SuppressAutoRecycle` and calls `recycle_element()` on its own.

`LayoutPanel` hands a `VirtualizingLayout` its children through an adapter context: every child counts as realized, the realization rect is unbounded, there
is no anchor and nothing gets recycled. The layout origin is always 0,0, which
`LayoutContextAdapter` requires too.

### The Flow Algorithm

`StackLayoutEngine`, `TileLayoutEngine` and `WrapLayoutEngine` share one private flow algorithm
(`src/ui/layouts/FlowLayoutAlgorithm.hpp`), a restatement of WinUI's `FlowLayoutAlgorithm` and
`ElementManager`. Items go along a minor axis in lines, and the lines stack along the major axis,
the one a scroller moves.

1. **Anchor.** Measure picks the first realized item while the realized range still touches the
   realization rect, otherwise the layout's own estimate (`anchor_for_realization_rect()`).
2. **Generate.** From the anchor it walks forward and then backward, realizing and measuring one
   item at a time until the item before went past the rect. One item past each edge is always
   realized. An item joins the current line unless the line is full or the layout's
   `should_break_line()` says it does not fit.
3. **Recycle.** Elements outside what was generated go back to the host.
4. **Extent.** The size of the whole collection, realized or estimated, comes from the layout's
   `extent()`.
5. **Arrange.** The realized lines are walked, every item gets its line's major size, the line is
   placed by its line alignment and, for a stack, stretched across the minor extent.

A layout supplies its sizes and estimates through `FlowAlgorithmDelegates`.

| Layout | Ported from | Lines | Estimate for unrealized items |
| --- | --- | --- | --- |
| `StackLayoutEngine` | `StackLayout.cpp`, `StackLayoutState.cpp` | One item per line. Each item is measured with the room across the stack and unbounded along it, and arranged across the full cross extent. The desired cross size is the widest item arranged in the last pass. | The average size of the last 100 measured items (a ring buffer, as `StackLayoutState` keeps), rounded once two measured sizes differ. It sizes the scroll extent and decides where a jump lands. |
| `TileLayoutEngine` | `UniformGridLayout.cpp` | Equal cells, as many per line as fit. The cell is `min_item_width()` by `min_item_height()` where set, else item 0's desired size, exactly that size. | Every line has the same size, so the extent covers every line without estimating. |
| `WrapLayoutEngine` | `FlowLayout.cpp`, `FlowLayoutState.cpp` | Each item at its desired size, `min_item_spacing` apart, a new line when the next would not fit. Lines are `line_spacing` apart, as tall as their tallest item, placed by `line_alignment`. Horizontal (the default) fills rows and scrolls vertically. | The average of the last 100 measured lines, the collection's last line left out since it is usually short. Before any line was measured, item 0 stands in for a line of as many like it as fit. |

`StackLayoutEngine` still realizes one element for a zero-size viewport, for a repeater nested out of
view.

### LinedFlowLayout

`LinedFlowLayoutEngine` (a photo wall) does not use the flow algorithm. It ports WinUI's greedy "fast
path" in `LinedFlowLayout.cpp`, the one WinUI runs when every item's aspect ratio is known. Here
each item's aspect ratio comes from measuring it at the line height with an unbounded width.

- The line height is `line_height`, or item 0's desired height when it is unset.
- Items never measured are estimated at the average aspect ratio of those that were, 1 before
  any were.
- Lines fill greedily. When an item does not fit, the layout either closes the line (to be
  stretched) or takes the item in (and shrinks the line), whichever scale factor is nearer 1.
  One factor scales every item on a line. A line wider than the room is always shrunk; a
  narrower one is widened only with `LinedFlowLayoutItemsStretch::Fill`, and never the last line.
- With `Fill` a line runs edge to edge and the gaps absorb the rounding error. With `None`,
  `items_justification` places the line (Start, Center, End, SpaceAround, SpaceBetween,
  SpaceEvenly).

Not ported: the `ItemsInfoRequested` event (aspect ratios supplied by the app), `LockItemToLine`,
the regular path's line balancing, and the item transitions.

### ForEach

A virtualized `ForEach` (`ForEachControl`, after WinUI's `ItemsRepeater.cpp`, `ViewManager.cpp` and
`ViewportManager.cpp`) has no chrome and does no scrolling of its own. Inside a `Scroll` it realizes only the items its
layout places in the visible part, plus a cache buffer of `horizontal_cache_length()` /
`vertical_cache_length()` viewports (2 by default, one on each side).

- **Collection changes** arrive through `notify_inserted()`, `notify_removed()`,
  `notify_replaced()`, `notify_moved()` and `notify_reset()` (the actions of
  `INotifyCollectionChanged`), so realized elements keep their items and the layout keeps its
  place.
- **Clearing.** An element the layout did not ask for in a measure pass is cleared, unless it
  holds keyboard focus (the pinned element, kept and parked off-screen at -10000) or the
  layout keeps it itself (`SuppressAutoRecycle`).
- **Events**: `on_element_prepared()` (before measure), `on_element_clearing()`,
  `on_element_index_changed()`.
- **`get_or_create_element()`** realizes an item where the layout would put it, so it can be
  brought into view.
- **Scrolling.** A scroll arranges without measuring, so the repeater re-runs its layout from
  `arrange_override()` when the viewport moved, and asks for a full pass next tick if that changed its size.

Elements are created and destroyed, not pooled.

## Shapes

The shape family (`include/winui/ui/media/Shape.hpp`) follows WinUI's two layout behaviors, from
`dxaml/xcp/core/core/elements/shape.cpp` and one file per shape.

| Shapes | Default `Stretch` | Measure | Geometry |
| --- | --- | --- | --- |
| `Rectangle`, `Ellipse` | `Fill` | Nothing of their own, only an explicit width or height (`CRectangle::MeasureOverride`), so without a size they stretch to their slot. Under `UniformToFill` they ask for a square of the larger bounded side. | Built from the layout size (`CShape::GetOutlineRect`): the element rect inset by half the stroke so the stroke stays inside, squared by `Uniform`/`UniformToFill`, collapsed by `None`. |
| `Line`, `Polyline`, `Polygon`, `Path` | `None` | With `None`, the geometry's bottom-right corner plus half the stroke. | Their own geometry in local coordinates. With `None` it is drawn where it is (`CShape::ApplyContentStretch`); otherwise its bounds are moved to the origin, scaled into the layout rect less the stroke, and centered on the free axis of an aspect-preserving stretch (`CGeometry::ComputeStretchMatrix`). |

`stroke_thickness` counts only while there is a stroke (`CShape::GetStrokeThickness`), so an
unstroked shape is never deflated. A laid-out shape of zero area draws nothing
(`CShape::ShouldRender`).

## Layout Traps

A few rules that follow from the model above and keep catching people:

- **A `Scroll` directly in a `StackControl` cannot scroll.** The panel measures its children
  unbounded along the stack axis (as `CStackPanel::MeasureOverride` does), so the `Scroll`
  reports its whole content as its desired size. Put it in a fixed-height `Grid` cell instead.
- **A `Scroll` directly inside another one collapses.** The outer one measures its child
  unbounded and re-arranges it at its full content height on every scroll, which erases the
  inner viewport. Put a fixed-size cell (a Dip track in a `Grid`) between them; a cell never
  re-stretches its child.
- **`TwoPane` needs a bounded parent.** It picks its mode from the extent it is offered
  during measure, so under an unbounded offer it can never pass `min_wide_mode_width` or
  `min_tall_mode_height` (641 by default). A Dip-track `Grid` cell works.
- **A leaf never reports the full `available` size as its desired size.** During an Auto-row
  measure `available` is the whole remaining budget, so a leaf that "wants to fill" balloons the
  row. A toolbar separator reports a control-sized height (a text line plus the theme's
  padding) for this reason. Stretching is the parent's job at arrange time.
- **No callbacks fire from inside layout.** Measure is `const`, so a state derived there has no
  change event. `TwoPane` has no mode change event, so callers read `mode()` after layout (from a
  `tick_override()`, say).
- **Containers never insert hidden wrappers.** `TwoPane` does not wrap its panes in a `Scroll`.
  Whether a pane scrolls, and how its scroll bars show, is the caller's choice, made by putting a
  `ui::Scroll` in the pane.

## Layout Snapshots

`src/ui/LayoutSnapshot.hpp` is the one place layout runs on more than one thread. It is a private header: an app only sees `ParallelLayout` (`include/winui/ui/ParallelLayout.hpp`), the switch on `Window`.

A `LayoutSnapshot` is a read-only batch of independent elements plus everything needed to
measure them, captured once so every worker sees the same inputs:

```cpp
struct LayoutSnapshot
{
    std::span<Element* const> elements;  // non-owning; results come back in this order
    Size                      available;
    const TextMeasurer*       measurer;  // must be safe to use from several threads
    const Theme*              theme;
};
```

### Measuring in Parallel

`measure_parallel(snapshot, scheduler)` splits the batch into at most
`scheduler.worker_count()` contiguous chunks, submits one job per chunk, waits for all of them
and returns the desired sizes in order. Each element is measured exactly as a sequential
`measure()` would, and each job writes only its own slice of the result, so no locking is
needed. With `Scheduler{Scheduler::Inline{}}` the chunks run on the calling thread, in order,
which is what the deterministic tests use (`tests/test_parallel_layout.cpp`).

The rules that make it safe:

- The elements must be independent: none may be measured elsewhere or mutated while the call
  runs. `measure()` writes each element's own `mutable` caches, which is fine only because no
  two jobs share an element.
- Workers never touch the tree beyond that measure call. Committing the sizes back (arranging
  with them) is the caller's job, on the UI thread.
- Chunks are coarse on purpose: one job per worker, never one per element.

`Scheduler` (`include/winui/core/Scheduler.hpp`) is the pool underneath: one worker per hardware
thread by default, `detail::submit()` returns a move-only `detail::Job` to `wait()` on, and the destructor runs
every job still queued before joining. It also runs `ui::on_background()`'s coroutines and the
picture decodes of `ImageSource::load()` (a pool of two of its own).

### Parallel Measuring in `Window::layout()`

`Window::layout()` measures its content inside a parallel scope (`src/ui/ParallelMeasure.hpp`),
set by `Window::set_parallel_layout(ParallelLayout)`. It is on by default:

```cpp
struct ParallelLayout
{
    bool        enabled      = true;
    std::size_t min_elements = 256;     // the smallest batch worth sending to workers
    Scheduler*  scheduler    = nullptr; // borrowed; nullptr is the process-wide layout pool
};
```

The split happens in the containers whose children are measured independently of each other:
`StackControl`, `GridControl` (each of its three measure passes is one batch: every offer in a
pass depends only on track sizes fixed before it), `WrapPanelControl`, `CanvasControl`,
`TilePanelControl` (every tile after the first, which sets the cell size) and a
`LayoutPanelControl` without a layout. Their
`measure_override()` checks `parallel_measure_active()`, a thread-local that is only set on the
thread running the window's measure, and if it is set hands its children to
`measure_batch(std::span<MeasureRequest>, context)` instead of looping. With the scope
closed (parallel layout off, arrange, popups, a modal, a worker thread) the loop is the old one,
so the sequential path costs one thread-local read per container.

`measure_batch()` then decides, per batch:

1. **Which children may go.** A child goes to a worker only when its whole subtree may: every
   element in it answers `Element::parallel_measure_safe()` (default false), walking
   `for_each_child()`. A collapsed element counts as safe, since its `measure()` returns before
   `measure_override()`. The answer and the subtree's size are cached per pass, for containers only.
2. **Whether it pays.** The safe children's subtree sizes are summed; under `min_elements`, or
   with fewer than two safe children, the batch is measured in order on the calling thread.
3. **Whether to split here or lower.** With fewer safe children than twice the threads and one
   child at least `min_elements` large by itself, splitting here would leave threads idle, so
   the batch is measured in order and that child splits its own, wider, set of children.
4. **The split.** The unsafe children are measured first, in order, on the calling thread. The
   safe ones are cut into contiguous chunks of about equal subtree size, one per worker plus one
   for the calling thread, which measures its share itself rather than waiting. Neither a worker
   nor the calling thread's share splits again: the scope is not set on workers and is
   suspended for the calling thread's share, so a batch never waits on a pool its own jobs hold.
   Each job carries the caller's `animations_enabled()` (a thread-local), and an exception
   thrown on a worker is rethrown by `measure_batch()` once every chunk is done.

The sizes are the same, bit for bit, as sequential measuring: each element is measured by the
same code with the same inputs, nothing a safe element measures depends on a sibling, and the
container sums the results in child order afterwards. The one order that changes is between an
unsafe child and its safe siblings; an unsafe child whose measure changes a safe sibling (a
binding it fires) has that sibling measured with the change, which is what the next pass its
invalidation asks for would do anyway.

`layout_scheduler()` is the process-wide pool: one worker per hardware thread but one, at most
7. It is created the first time a batch qualifies and never destroyed (joining from a static
destructor runs after `main()`, under the loader lock in a DLL build), so a small app never
starts a thread. A test passes its own `Scheduler`: `Scheduler{4}` for real threads,
`Scheduler{Scheduler::Inline{}}` to run the same batching code on the calling thread.

**What answers true.** `TextControl`, `ButtonControl` without content (a button's `content()`
is measured but, being one tab stop with the button, not listed in `for_each_child()`, so a
button with content answers false), `ToggleButtonControl` through it, `CheckBoxControl`,
`RadioButtonControl`, `ToggleSwitchControl`, `SliderControl`, `TextBoxControl`,
`ProgressBarControl`, `ProgressRingControl`, `InfoBadgeControl`, `IconControl`,
`SpacerControl`, `BorderControl`, `GroupBoxControl`, `ImageControl`, `AppBarButtonControl`, `DropdownButtonControl` (as its button answers), the
splitting panels, `RelativePanelControl` (it measures its children in dependency order on one
thread), `ScrollControl`, and the hosts `ui::Match`, `ui::Lazy` and `ui::State` build, which
swap their child when a property changes, never while measuring. A `LayoutPanelControl` with a
layout answers what `Layout::parallel_measure_safe()` says; the built-in layouts virtualize and say
false. Image and icon sources are safe to measure: bitmaps and SVG documents are immutable once
decoded, and `IconSource` keeps a glyph's measured box in one atomic word. Everything else is
false and is measured on the UI thread; a container above it can still split its own siblings, so
one unsafe control on a page only keeps its own subtree sequential.

**What was found unsafe, and stays on the UI thread.** The audit behind the list above:

- Controls that build children while measuring: `ForEachControl` realizes items from its
  template in `measure_override()`, building specs and subscribing to properties; the views and
  `LayoutPanelControl`'s virtualizing layouts realize and recycle elements on demand too, and read
  the viewport of the scroller around them. The thread checks (`core/ThreadCheck.hpp`) report such
  a subscription from a worker.
- Thread-locals that assume the UI thread: `animations_enabled()` (carried into each job, see
  above) and `detail::RetireScope`'s parked list.
- Function-local statics: the regional number format `NumberBox` reads.

The shared inputs are safe. `Theme` is read-only during layout (a requested theme's scoped copy is
per element), and the `HeadlessTextMeasurer` is pure arithmetic. `DWriteMeasurer` keeps no
state of its own. Its formats, layouts and metrics come from the process-wide cache in
`DWriteCache.hpp`, which is locked, and from the window's `DWRITE_FACTORY_TYPE_SHARED` factory,
which is free-threaded (a test lays a form out
on four threads through it and compares every bound with the sequential pass). A measurer
that keeps a cache has to lock it or keep it per thread, as `DWriteCache.hpp` does.

**Rules for a custom `Element`.** Leave `parallel_measure_safe()` false unless
`measure_override()` touches nothing but the element's own state, the children it lists in
`for_each_child()` (which answer for themselves), the measurer and the theme. It must not build
or remove children, read or write a `Property`, subscribe, invalidate, start an animation, or
use a cache or static shared with other elements. Writing its own `mutable` measure caches is
fine: no two threads ever measure the same element. A part measured without being listed in
`for_each_child()` makes the answer false, and the answer is inherited, so a subclass whose
`measure_override()` does more overrides it back. `measure_batch()` is private to the library, so a
custom container measures its children in order; its children can still split their own.

**What it buys.** `tests/test_parallel_layout.cpp`'s hidden `[benchmark]` case lays out a
settings-like form (sections of label/field grids, wrapped paragraphs, wrap panels of buttons)
at a new width each run, on a 12-thread machine (7 workers plus the UI thread), mean of 50 runs
(20 in Debug):

| Form | Measurer | Build | Sequential | Parallel |
|---|---|---|---|---|
| 2,500 elements | DirectWrite | Release | 24.3 ms | 7.8 ms |
| 2,500 elements | DirectWrite | Debug | 40.6 ms | 19.1 ms |
| 2,500 elements | headless | Release | 0.52 ms | 0.39 ms |
| 500 elements | DirectWrite | Release | 4.90 ms | 1.65 ms |
| 500 elements | headless | Release | 0.098 ms | 0.114 ms |
| 250 elements (under the threshold) | DirectWrite | Release | 2.46 ms | 2.45 ms |
| 250 elements (under the threshold) | headless | Release | 0.084 ms | 0.085 ms |

DirectWrite text measurement is what the workers take off the UI thread; the headless
measurer is so cheap that a 500-element batch barely pays for the hand-off, which is why the
threshold sits at 256 and parallel layout only matters in the shipping, DirectWrite path. Under
the threshold the scope's checks cost nothing measurable.
