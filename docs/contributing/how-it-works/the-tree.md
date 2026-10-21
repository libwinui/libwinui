# The Tree

Every control, panel and window content is an `Element`. This page covers what an `Element`
is, who owns it, how other code points at it without owning it, and how a change finds its way
to the next layout pass.

## Element

`include/winui/ui/Element.hpp` defines one abstract class that every node derives from: measure,
arrange, render, take input, take focus. There is no ladder of base classes in between. A control
overrides the hooks it needs and inherits no-op defaults for the rest.

The naming rule is the whole interface in one line: you call a verb, and you override
`<verb>_override`. The public verb does the work common to every element and then calls the
protected hook the control implements:

| Verb (public, not virtual) | Hook (protected) | What the verb adds |
| --- | --- | --- |
| `measure(available, context)` | `measure_override()` (required) | Collapsed check, `requested_theme()`, margin, explicit/min/max size |
| `arrange(final_rect)` | `arrange_override()` (default: nothing) | margin, alignment, max size, theme-transition triggers, writes `bounds()` |
| `render(renderer, context)` | `render_override()` (required) | Collapsed check, `requested_theme()`, visual transform and opacity, the focus ring after the hook |
| `handle_pointer(e)` | `pointer_override()` | hit-testing, the routing up the parents, capture, `on_pointer_entered()`/`on_pointer_exited()` |
| `handle_wheel(e)` | `wheel_override()` | the same routing, returns whether something scrolled |
| `handle_key(e)` | `key_override()` | nothing; `Window` offers the key to the focused element and then each parent |
| `handle_text(e)` | `text_override()` | nothing; typed text is routed like a key |
| `tick(delta)` | `tick_override()` | advances the element's own animations first, then every child |

The other virtuals are plain questions with inert defaults: `focusable()`, `focus_changed()`,
`enabled_changed()`, `attached()`, `detached()`, `automation_role()`, `automation_providers()`,
`for_each_child()`, the hit-test hooks and the focus-ring hooks. Window-only notifications
(`set_parent()`, `set_focused()`, `clear_dirty()`) are private, with the window and the container
bases as friends; a control reacts through the protected hooks instead.

Input is routed, not broadcast. A pointer event goes to the topmost element under the pointer and
then up its parents until a hook returns true; a handled press captures the pointer for the path
it landed on until the release. A key goes to the focused element first and then up its parents,
and only what nothing handled reaches the shortcuts and the focus moves. [Input and
Focus](input-and-focus.md) has the full routing.

A container lists its children once, in `for_each_child()`, and the tree-wide walks reach them
through that. The walks are not virtual, so no override can forget the children:

| Walk | Default | Hook for a control's own part |
| --- | --- | --- |
| `collect_focusables()` | itself if `focusable()` and enabled, then each child's | overridden when the control is one tab stop with parts inside (`ComboBox` and its `TextBox`), is a focus scope, or keeps hidden pages out of the ring |
| the walk for popups, tool tips and global shortcuts (private, built-in controls only) | itself, then each child's | an unselected page or collapsed pane must not open a popup |
| `automation_children()` | every child | the client should see another order (a `Canvas` in z-index order) or fewer nodes (a composite is one node) |
| `tick()` | `tick_override()`, then each child | a part it owns but does not list is ticked from `tick_override()` |
| `clear_dirty()` | itself, then each child | `clear_dirty_override()` |
| `build_visual_tree()` | itself, then each child | `build_visual_tree_override()` |
| `sync_composition()` | each child | `sync_composition_override()` (built-in controls only) |

`collect_scope_focusables()` is the exception: a focus scope's inner ring is opt-in, so its
default stays empty.

Two rules keep the verbs meaningful:

- A container draws and measures its children through their public verbs (`child->render()`,
  `child->measure()`), never through their hooks, or the child's margin, visibility and transform
  are skipped. A debug build asserts if `render()` is re-entered from the same element's
  `render_override()`.
- A container never hands pointer or key input down to its children. The window routes it; a
  container whose children are not where `for_each_child()` and its bounds say (scaled, zoomed,
  clipped, or drawn under chrome of its own) answers `hit_test_children()`, `map_to_children()`,
  `children_hit_clip()` or `covers_children_at()`.

The visual layer (`set_opacity()`, `set_translation()`, `set_rotation()`, `set_scale()`,
`set_center_point()`, implicit transitions, transitions) lives in a separately allocated `ElementMotion` block that is
created the first time any of it is set. An element that never moves pays for one null pointer,
and its `render()` emits exactly what `render_override()` emits.

For a worked example of a custom element, see
[Your Own Element](../../tutorials/extending-the-tree/01-your-own-element.md).

## Single-Owner Ownership

Every node in the tree is owned by exactly one parent through `std::unique_ptr`. There is no
`shared_ptr` anywhere in the UI tree, and destruction follows the hierarchy deterministically.

A container's `add()` (or `set_content()`, `set_child()`, ...) takes a `std::unique_ptr<T>` and
hands back a `T*`. The caller keeps an observer, never a second ownership claim:

```cpp
auto                  stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
ui::TextControl*   label = stack->add(std::make_unique<ui::TextControl>("Clicks: 0"));
ui::ButtonControl* click = stack->add(std::make_unique<ui::ButtonControl>("Click me"));
click->on_click([label] { label->set_text("Clicked"); });
window.set_content(std::move(stack)); // the Window now owns the whole subtree
```

The window is owned the same way one level up. `HeadlessUI` holds its `Window` by `unique_ptr`,
and `ui::run(window)` only borrows a window the app owns, usually a local in `main()`.
`Window::set_content()` and `Window::show_modal()` take a `unique_ptr` (or a spec) and return an
observer.

`Element` is not copyable. A control carries a parent link, a stable `VisualId` and a list of
move-only subscriptions, none of which has a sensible duplicate. It is movable, protected, so a
factory can return a not-yet-parented control by value. Once an element is in a tree it lives
behind a stable `unique_ptr` and never moves again.

The declarative layer builds exactly the same graph. How a braced list of specs ends up as
`unique_ptr`s without reference counting is on [Specs and build()](specs-and-build.md#spec-and-speclist).

OS and COM resources use their own RAII (`Microsoft::WRL::ComPtr` for Direct2D, DirectWrite and
WIC, pimpl `unique_ptr`s for device objects). The framework never reaches into Windows' object
lifetimes, and Windows never reaches into the tree's.

## Observers

Anything that needs to reach an element without owning it holds a raw pointer into memory the
tree already owns. The contract is simple: the observer is valid until the tree that holds the
element changes shape (the content is replaced, a modal closes, a virtualized row scrolls away).

The window's tab ring is the everyday example. `Window` keeps a `std::vector<Element*>` of focus
stops, rebuilt from `collect_focusables()` on every `layout()` and after every wheel event (a
`VirtualList` destroys and recreates rows on the spot). Before it trusts a stored pointer, it
checks it is still in the freshly collected ring. Popup owners, entered focus scopes and focus
traps get the same treatment: they are re-found in the tree rather than trusted.

Two more tools exist for observers that cannot re-collect:

- **`ElementWatch`** is a non-owning pointer that goes null when its element is destroyed. The
  element keeps a list of the watches on it, and either side going away unhooks the
  other. `ConnectedAnimation` uses it for a destination that can die with its page.
- **`Window::content_generation()`** is a counter bumped by `set_content()`, `show_modal()` and
  `close_modal()`. The UI Automation provider stamps each `Element*` it hands to a client with
  the generation it saw, and refuses to touch the pointer once the number has moved on.

A handler captured in a spec follows the same rule from the other direction: it must not keep a
reference to something that dies before the window. See
[Specs and build()](specs-and-build.md#handlers-and-lifetimes).

## DirtyFlags and VisualId

A setter does not re-measure or redraw anything. It records what went out of date and returns:

```cpp
void set_margin(const Thickness& margin)
{
    if (margin_ == margin)
    {
        return; // the equality gate: an unchanged value costs no layout
    }
    margin_ = margin;
    mark_dirty(DirtyFlags::Layout);
}
```

`DirtyFlags` (`include/winui/core/DirtyFlags.hpp`) is a one-byte bitmask:

| Flag | Meaning | Typical setter |
| --- | --- | --- |
| `Measure` | the desired size must be recomputed, and so the arrangement | `set_text()`, `set_margin()`, `set_visibility()` |
| `Arrange` | the place of the element or its children changed, its desired size did not | a scroll offset, `Carousel`'s slide |
| `Visual` | appearance changed, size and place unaffected | `set_enabled()`, a focus change, a color |
| `Layout` | `Measure` and `Arrange` together | what a size-affecting setter marks |

`mark_dirty()` sets the bits on the element and bubbles only the *newly set* bits to the parent.
Bubbling stops at the first ancestor that already has them, which keeps the invariant cheap: if
a node is dirty in some way, every ancestor is too. The window can ask its root
`is_dirty(DirtyFlags::Layout)` without walking the tree.

The flags are consumed at two points:

- `Window::layout()` measures again only the elements marked `Measure` and arranges again only
  those marked `Measure` or `Arrange` (or everything on the first pass, or when the client size
  changed), and clears every flag afterwards, since a host lays out right before it paints.
- `Window::tick()` returns true when the root or the modal is dirty in any way, which is how the
  Win32 message loop decides to call `InvalidateRect`.

A new element starts with `DirtyFlags::All`, so the first layout always runs.

Every element also gets a **`VisualId`** at construction (`include/winui/core/VisualId.hpp`): a
32-bit integer from a process-wide atomic counter, starting at 1, never reused. `0` is
`InvalidVisualId`. Other systems key their side tables by it instead of by address: the
`Compositor` keys each overlay visual by the element's id (a layer gets ids of its own),
`NativeHost` keys each child `HWND`
by it, and `VisualTree` uses it for parent/child links.

This invalidation model is also why the declarative layer does not rebuild and diff. A setter
already knows exactly what changed at the moment of assignment. Diffing a rebuilt tree is a way
of recovering invalidation information that was thrown away, and this codebase does not throw
it away.

## VisualTree

`include/winui/ui/VisualTree.hpp` is a compact, data-oriented snapshot of the laid-out tree's
shape. `Window::layout()` clears and rebuilds it on every pass that re-lays out, right after
arrange, through `Element::build_visual_tree()`, which adds every child `for_each_child()` lists
and the parts a control adds in `build_visual_tree_override()`. Each node is one slot in parallel arrays:

| Array | Content |
| --- | --- |
| `id_` | the element's `VisualId` |
| `parent_`, `first_child_`, `next_sibling_` | links, by `VisualId` |
| `bounds_` | arranged bounds in window DIPs |
| `flags_` | the dirty flags at snapshot time (built before the flags are cleared) |

Slots are in pre-order and only valid until the next rebuild; a `VisualId` stays the same across
rebuilds, and `find(id, slot)` looks one up in O(1).

It is an inspection surface, not a way to drive controls. Today only the tests read it
(`tests/test_visual_tree.cpp`, `tests/test_perf_count.cpp`), through `Window::visual_tree()`. The compositor and the
automation provider both work from the live `Element` tree instead. It is kept because it is
the shape a future inspector or a retained compositor tree would start from, and because it
proves the logical tree can be flattened by id without touching control objects.
