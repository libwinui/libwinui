# Input and Focus

From Win32 messages to a control's visual state. The host translates messages into plain event
values, `Window` routes them, and each control derives what it looks like from a few input
flags. None of the routing needs an `HWND`, so all of it is tested headlessly.

## Pointer and Key Events

`include/winui/core/Input.hpp` defines the event values. They carry no Windows types.

| Event | Fields | Delivered by |
| --- | --- | --- |
| `PointerEvent` | `position` (window-client DIPs), `button` (`None`/`Left`/`Right`/`Middle`), `action` (`Move`/`Down`/`Up`/`Leave`/`Wheel`), `wheel_delta`, `modifiers` (`ctrl`, `shift`, `alt`, `win`), `click_count`, `horizontal_wheel`, `device`, and for a pen `pressure`, `tilt_x`, `tilt_y`, `eraser` and `barrel_button` | `Window::handle_pointer()` |
| `KeyEvent` | `key` (a small semantic `Key` set, not a virtual-key mirror), `action` (`Down`/`Up`), `modifiers` (`ctrl`, `shift`, `alt`, `win`) | `Window::handle_key()` |
| typed text | UTF-8 `std::string_view` | `Window::handle_text()` |
| context menu | an optional position (none for Shift+F10 or the Menu key) | `Window::handle_context_menu()` |

A virtual key and the character it types are two different pieces of information that Windows
delivers as two messages, so they stay two calls. Control characters (Backspace, Tab, Enter,
Escape) arrive as both a `KeyEvent` and text; `handle_text()` implementations ignore anything
below `0x20`.

The Win32 host (`src/platform/windows/Win32WindowHost.cpp`) does the translating:

- **Mouse messages** become `PointerEvent`s with the position converted to DIPs, `modifiers.shift`
  and `modifiers.ctrl` from the `MK_*` bits, and Alt and Win from `GetKeyState()`. `WM_LBUTTONDOWN` counts clicks itself against `GetDoubleClickTime()` and
  `SM_CXDOUBLECLK`/`SM_CYDOUBLECLK`, since Win32 only sends `WM_LBUTTONDBLCLK` for the second
  click and nothing for a third. The streak never wraps, so a slow fourth click still counts as a
  triple. `WM_MOUSELEAVE` becomes `Leave`.
- **Wheel messages** keep the fractional notch (`delta / WHEEL_DELTA`). `WHEEL_DELTA` is a unit,
  not a quantum: high-resolution wheels and precision touchpads send smaller deltas, and integer
  division would floor slow scrolls to zero. The notch is scaled to 48 DIPs. `WM_MOUSEWHEEL`
  positions arrive in screen coordinates and positive means "away from the user", so a vertical
  wheel is converted to client DIPs and negated so that positive means "scroll down";
  `WM_MOUSEHWHEEL` positive already means right and sets `horizontal_wheel`. The `MK_*` bits come
  from wParam's low word, which is what makes Ctrl+wheel zoom work.
- **Keys** go through `translate_key()`; anything outside the `Key` set arrives as
  `Key::Unknown`. An Alt chord goes to the tree first, with `modifiers.alt` set. One the tree
  doesn't use goes on to the system (the menu), Alt+Up and Alt+Down never do, since they open
  drop-downs, and the system keeps Alt+F4, Alt+Space, Alt+Tab and Alt+Esc to itself. Shift+F10
  arrives as `WM_SYSKEYDOWN` and the Menu key as `VK_APPS`; both are passed on to
  `DefWindowProc`, which synthesizes the `WM_CONTEXTMENU` that ends in `handle_context_menu()`
  without a position.
- **`WM_CHAR`** is UTF-16. A character outside the BMP arrives as two `WM_CHAR`s, which the host
  pairs with a `Utf16Decoder` (an unpaired half becomes U+FFFD) before delivering UTF-8.
- **Window focus**: `WM_KILLFOCUS` clears focus and remembers the stop that had it
  (`Window::suspend_focus()`); `WM_SETFOCUS` gives it back if it is still in the ring, else the
  first stop (`Window::restore_focus()`).
  `WM_SETFOCUS` also re-reads the keyboard layout, since the spell-check language may have changed
  while another app had focus.
- **The cursor**: `WM_SETCURSOR`'s wParam is the window under the pointer. A native child's
  message reaches the host through `DefWindowProc` first and is left alone; the content's cursor
  applies only over the host's own client area or while the pointer is captured.

Every input message ends with `request_frame(hwnd)`. Controls update their state immediately but
never paint; the next `WM_PAINT` lays out and draws.

### Routing a Pointer Event

`Window::handle_pointer()` resolves precedence in this order:

1. **Drag in progress.** A running drag has the pointer to itself; the tree sees nothing until
   the drop.
2. **Modal.** While a modal is open, only it receives pointer events. A press still focuses the
   stop under it, within the modal's ring.
3. **Popups.** A `Down` or `Up` inside an open popup goes to that popup only, topmost first (a
   submenu over its menu wins). A `Down` outside every popup and every popup owner
   light-dismisses the ones that allow it, and is swallowed when a popup closed. `Move` and
   `Leave` are broadcast to every open popup as well as the tree, so hover tracks correctly across
   popup edges. A wheel turn over a popup goes to that popup only, and one anywhere else is
   broadcast.
4. **Focus on press.** A `Down` gives focus to the first stop in the ring whose
   `takes_focus_at(point)` accepts it, before the control's own click handling runs. A click on
   empty space leaves focus where it was.
5. **The tree.** `Wheel` goes through `handle_wheel()`, everything else through
   `handle_pointer()`.

Inside the tree, `Element::handle_pointer()` **hit-tests once and routes**:

- It walks down from the root to the topmost element under the point. An element is a candidate
  when it is Visible, hit-test visible (`is_hit_test_visible()`) and enabled, and its `bounds()`
  contain the point. Its children are tried topmost first, in the order `hit_test_children()`
  gives (by default what `for_each_child()` lists, last first; `Panel` uses its z-index order).
- On the way down the point is mapped into each element's layout coordinates: through its
  `visual_transform()`, and through `map_to_children()` for a control that scrolls, zooms or
  scales its content (`Scroll`'s zoom, `ScaleToFit`). `children_hit_clip()` keeps content scrolled
  out of a viewport from being hit, and `covers_children_at()` lets a control's own chrome drawn
  over its children (a scroll bar, a search box's query button, an open `Swipe` strip) take the
  point.
- The event goes to that element's `pointer_override()`, then to each parent on the way back up,
  until one returns true. Every built-in control that acts on a press (a button, a check box, a
  slider, a text field's caret, a list row) returns true for the press and its release, so a click
  on a button inside a list row clicks the button and leaves the row's selection alone.
- The elements above the one that handled it get the event in `handled_pointer_override()`
  instead. A container that must see every press inside it uses that: `Swipe` and
  `PullToRefresh` still start a drag from a press on a button, `ToolTip` closes, `TableView`
  commits an open edit, and a list row moves the keyboard cursor to itself without selecting.
- A press captures the pointer for the path it landed on until its release, so the control that
  was pressed gets the moves and the release wherever the pointer goes. `capture_pointer()`
  narrows it to one element and its parents (a `Swipe` taking a drag from its content); what the
  capture is taken from gets a `Leave`, which cancels its press.
- When the element under the pointer changes, everything the pointer left gets a `Leave` (once
  its capture is over, if it holds it), and `on_pointer_entered()`/`on_pointer_exited()` are
  raised.

The state behind this (the hovered path and the capture) is kept per dispatch root, so a test
that calls `handle_pointer()` on a control or a container routes within it the same way.

A control that closes on a press anywhere else (an open `Swipe` strip) never gets that press. It
watches for one through `detail::watch_outside_presses()` instead.

The wheel goes the same way through `handle_wheel()`: the element under the pointer gets it in
`wheel_override()`, then each parent until one returns true, so the innermost `Scroll` or
`VirtualList` under the pointer scrolls and the page behind it does not.

The refusal is also how scroll chaining works. A scroller already at the end a wheel turn points
toward returns "not consumed", and the scroller around it takes the turn. `Scroll` and `VirtualList` both do this, and `VirtualList`
also refuses a horizontal turn. Without it a list at its bottom swallows the wheel and the page
never moves. Toward the other end the list still has room and scrolls itself.

A wheel turn can make a `VirtualList` destroy and recreate its row elements on the spot, outside
`Window::layout()`. The window therefore rebuilds the tab ring right after dispatching a wheel
event, so `focused()` never points at an evicted row. A dangling focus pointer crashed on the
next `WM_KILLFOCUS` blur.

### Pointer Capture

Two things keep a press together:

- The host calls `SetCapture` on `WM_LBUTTONDOWN` and `ReleaseCapture` on `WM_LBUTTONUP`, so the
  release arrives even when the pointer left the window.
- The press captures the pointer for the path it landed on (see above), so the control that saw
  the `Down` also sees the `Up` wherever it lands and decides for itself: `Button` clicks only on
  an `Up` inside its bounds after its own `Down`.

The window keeps the cursor found at the press (`pressed_cursor_`) until the release, so dragging
a splitter keeps its resize arrow even when the pointer runs ahead of it.

### Routing a Key Event

`Window::handle_key()` offers a key to each of these in turn, and the first taker ends it:

1. **A running drag**: Escape cancels it; nothing else gets through.
2. **`on_key_preview()`**, the app's one window-wide hook (the F11 full-screen toggle).
3. **Tab and Shift+Tab**: the window owns them. A control sees Tab only when its
   `takes_tab_key()` returns true, as a `TextBoxControl` with `accepts_tab` does. Ctrl+Tab goes on, since
   it is a `Tabs`'s shortcut.
4. **Enter on a focus scope** steps into it.
5. **Escape**: leaves the innermost entered focus scope when no popup is open; otherwise closes
   the modal, or the innermost nested popup, or every light-dismissable popup.
6. **The key itself**: the focused element's `key_override()`, then each parent's, until one
   returns true. A focused control answers with `key_handled()` as a rule: its plain keys
   always, an arrow, Enter or a shortcut only when it uses that one. A `CheckBox` leaves Enter
   alone, so it bubbles up to the `DialogControl` around it, whose `key_override()` clicks the
   default button; an in-tree focus trap (`SidePane`'s pane) closes on an Escape that bubbles up
   to it.
7. **Scoped shortcuts**: the focused element's `handle_shortcut()`, then each ancestor's,
   innermost first. `Element::add_shortcut()` registers one; it fires only while
   focus is inside that element.
8. **Global shortcuts**: every popup owner's `handle_global_shortcut()` in tree order, for
   keys that are shortcuts (Ctrl or Alt held, or a function or editing key), then the window's own
   commands (`Window::add_command()`) unless a modal is open. This is where a `MenuBar`'s
   Ctrl+S or an `AppBarButton`'s shortcut lives, and why a `TextBox`'s Ctrl+C copies text instead
   of invoking a menu's Copy: the `TextBox` handled it in step 6.
9. **An arrow nothing handled** moves focus geometrically. Alt+arrow is never a direction.

Typed text (`Window::handle_text()`) goes the way of step 6: the focused element's
`text_override()`, then each parent's.

After every key and pointer dispatch the window calls `sync_composition()` and `sync_native()`.

Access keys (Alt+letter mnemonics on labels) are not implemented.

### Deferred Teardown

A control's callback can destroy the tree it runs in. A `DialogControl` button's `on_click` that
calls `close_modal()` would free the button whose `handle_pointer()` is still on the stack. So
every entry point (`handle_pointer()`, `handle_key()`, `handle_text()`, `tick()`,
`handle_context_menu()`, `handle_automation_invoke()` and the native-control notifications)
runs under a `DispatchGuard`, and `close_modal()` only records the request. The outermost guard
carries it out once the whole dispatch has unwound.

## Drag and Drop

`include/winui/ui/DragDrop.hpp` is modeled on WinUI's drag and drop: a source starts a drag with a
`DataPackage`, elements with `allow_drop()` set hear `drag_enter()`, `drag_over()`, `drag_leave()`
and `drop()`, and the source hears `drag_completed()`. A drag started in a libwinui window stays
inside that window and runs without OLE. Nothing is serialized, so `DataPackage::data` can hold
any copyable C++ value.

1. A source calls `DragDrop::start(source, package, visual, visual_bounds, pointer)` from its
   pointer handling. The drag is only pending at that point; after dispatching the pointer event
   the window takes it over (`DragDrop::take_pending()`), which is what step 1 of
   [pointer routing](#routing-a-pointer-event) then refers to.
2. On every move the window finds the drop targets under the pointer: the innermost element whose
   `allow_drop()` is set, found through `automation_children()` the same way `hit_region()` walks
   the tree, and its ancestors with `allow_drop()` set.
3. It asks them in that order with `drag_over()`, which returns a `DragResponse`: the operation a
   drop would do (`None` refuses), a `caption` and whether the glyph shows. The first that accepts
   ends the asking. An element hears `drag_enter()` before it is first asked and `drag_leave()` once
   it is no longer asked, because the pointer left it or a target below it accepted.
4. A release while a target accepts calls its `drop()`, and the others asked hear `drag_leave()`.
   A release anywhere else, or Escape, ends the drag with nothing done. Either way the source's
   `drag_completed()` hears the operation (`None` when nothing happened).

The drag visual is a `RenderRecording` of the dragged thing, drawn at the pointer with the offset
it was grabbed at, at opacity 0.8 (`ListViewItemDragThemeOpacity` in
`ListViewItem_themeresources.xaml`).

Files and text dragged in from another application arrive through the same events, with no
source and no visual. The Win32 host registers each window as an OLE drop target (`IDropTarget`)
and turns the drag's `CF_HDROP` and `CF_UNICODETEXT` into a `DataPackage`. Its `DragEnter`,
`DragOver`, `DragLeave` and `Drop` become `WindowSeams::handle_external_drag_enter()`, `_over()`,
`_leave()` and `_drop()`, which run steps 2 to 4 as the pointer moves:

- The keys held set `requested_operation`: Shift asks for a move, Ctrl+Shift for a link, anything
  else for a copy, among the operations the source allows. An answer the source does not allow is
  refused.
- The shell's drag image follows the pointer over the window (`IDropTargetHelper`), so the window
  draws no caption of its own. The target's caption becomes the drag's `DROPDESCRIPTION`, shown
  under that image where the file manager shows its "Copy to ...", with the glyph for the effect
  (none when `glyph_visible` is off). A source that draws no shell drag image shows no caption.

OLE needs the UI thread in a single-threaded apartment. `create_host()` enters it with
`OleInitialize()` before anything else on the thread picks an apartment, and the WinRT calls
elsewhere in the backend keep the MTA alive with `CoIncrementMTAUsage()` instead of joining it
(`Win32Apartment.hpp`). Two cases keep the older path, `DragAcceptFiles` and `WM_DROPFILES`, which
report a drop of files only where it is released (`WindowSeams::handle_external_drop()`, the enter
and the drop at once):

- the app put the UI thread in the MTA itself before the first window
- the process is elevated, where a file manager's OLE drag cannot reach it but `WM_DROPFILES`
  passes once the message filter allows it

Dragging from a libwinui window into another application is not implemented.

## Input Flags to Visual State

A control does not store "I am hovered and pressed and look like this". It stores the raw input
flags it saw and derives its visual state from them when asked:

```cpp
ButtonState ButtonControl::visual_state() const
{
    if (!is_enabled()) return ButtonState::Disabled;
    if (is_pressed()) return ButtonState::Pressed;
    if (hovered_)     return ButtonState::Hover;
    return ButtonState::Normal;
}
```

`pointer_override()` only updates `hovered_` and `pointer_down_`; `key_override()` updates the keyboard
press. `render_override()` asks `visual_state()` and picks the matching `ButtonVisual` from the
theme (`theme.button_hover`, `theme.accent_button_pressed`, ...). A flag can never disagree with
the drawn state because nothing else holds the drawn state.

`ButtonState` has no `Focused` member on purpose: the focus visual is an orthogonal ring drawn on top of whichever of the four states the control is
in. A focused and hovered button keeps its hover appearance.

`sync_composition()` works from the same flags: the control compares the derived state with
the one it last composed and animates only on a change.

## Focus and the Tab Ring

`Window` owns keyboard focus. There is exactly one focused element (`focused()`), and the ring
is a flat `std::vector<Element*>` rebuilt on every `layout()`:

- `collect_focusables()` walks the tree. An element that is `focusable()` pushes itself, then
  the walk recurses into the children `for_each_child()` lists. Most containers are not
  focusable, so Tab walks the whole tree leaf by leaf, in tree order.
- Anything not effectively visible (itself or an ancestor Collapsed) is filtered out.
- While a modal is open the ring is the modal's; while a focus-taking popup is open
  (`PopupRequest::takes_focus`, a `Flyout` in standard mode, a light-dismissable `TeachingTip`) it is
  the popup's; while an in-tree focus trap is active (a `SidePane`'s light-dismissable pane) it
  is the trap's. Focus goes back to whatever had it when each one closes.
- If the focused element is no longer in the new ring, focus is dropped rather than left
  dangling.

Tab and Shift+Tab step through the ring and wrap. `focus_element(e)` focuses a named element if
it is in the ring (UI Automation's SetFocus uses it). `set_focus()` takes focus off the old element and puts it on
the new one (each one's `is_focused()` changes and its `focus_changed()` hook runs), then `bring_into_view()`, because Tab
walking into a scrolled-off region is indistinguishable from Tab having stopped working.
`bring_into_view()` reveals `bounds()`; a ring drawn outside a control flush with a viewport
edge is cut there, which is why the items inside viewports ring inside themselves.

Only elements that exist can be in the ring. A `VirtualList` contributes its realized rows only,
and Tab does not scroll an unrealized row into view.

### Focusable and the focus ring

`focusable()` is a virtual. Controls usually derive it from `is_enabled()`, so a disabled button
leaves the ring.

The window draws the focus ring, not the control. While it draws each root (the content, each
popup, the modal), `Element::render()` notes every focused, focus-visible element it draws; once
the root is done the window draws those elements' rings over everything in it. It goes down from
the root to the element on the way, through each ancestor's visual transform and child mapping
(`map_to_children()`) and clipped to each one's viewport (`children_hit_clip()`), so a ring is
cut by a scroller's edge and by nothing else. Layout keeps no room for it.

The theme decides where the ring goes. The Fluent eras draw their two rings just outside
`focus_ring_rect(theme)` (bounds by default), Windows 8 its two dashed rectangles inside it
(`MetroDashedPair`), and the classic eras their dotted rectangle 4 inside it.
A control places it with three overrides:

- `focus_ring_rect(theme)` puts it around a part: a slider's thumb, the focused tab, the list row
  under the keyboard. A control whose ring sits a set distance from its content (a check box's 7
  at the sides and 3 above and below) returns the rect that puts it there.
- `focus_ring_inside()` returns true to ring inside the rect in every era, its outer edge on the
  rect. The items inside viewports do this (list, tree and table rows, tabs), as does a control in
  a tight spot.
- `focus_ring_corner_radius(theme)` rounds it (the theme's control radius by default).

`draws_focus_ring()` returning false is the escape hatch. A text field returns it because its
accent border shows focus. Menus and the navigation shell's flyout ring their keyboard item
themselves, since their owner keeps focus while they are open and the window's pass never sees
them.

`ui::testing::render_with_focus_ring(element, renderer)` draws an element and then the ring the
way the window does, for a test without a `Window`.

### focus_visible

Focus can come from a click as easily as from Tab, but the ring should only show after keyboard
navigation, the CSS `:focus-visible` split. The window tracks modality: any key switches it to
keyboard, any pointer `Down` switches it back. It sets `focus_visible()` on the focused
element, and the window draws a ring only around an element that is focused and
`focus_visible()`. The default is `true`, so a control driven directly in a test
(`ui::testing::set_focused(element, true)` and `render_with_focus_ring()`) shows its ring.

### Focus Scopes

A focus scope is a container that is one tab stop in the outer ring. Enter steps into it, Tab
then cycles only its inner ring (`collect_scope_focusables()`), and Escape steps back out onto
the container. A navigation pane of fifteen collapsible groups stays fifteen stops wide instead
of eighty.

It is a stored flag (`set_focus_scope()`), not a virtual, because whether an `Expander` is a
scope is a property of that layout, not of `Expander` as a class. A control that can serve as a
scope stops recursing in `collect_focusables()` when the flag is set. An inner ring of just
`this` makes an *item-navigating* scope: the control's items are drawn, not realized as elements
(a `ListView`'s rows), so focus stays on the control and Tab goes to `scope_step()`. An empty
inner ring means there is nothing to enter, and Enter falls through to the control's own
`handle_key()`. Scopes nest; the stack is revalidated level by level on every rebuild.

An item-navigating scope's rows report the ring through `focus_ring_rect()` and
`focus_ring_inside()` like any control, so the ring sits inside the row and its sides stay within
the list's clip.

### Directional Navigation

An arrow the focused control does not claim moves focus geometrically (`focus_direction()`), in
two passes: a candidate that lines up across the pressed axis wins outright, and only when none
does is an off-axis one scored. A single weighted score puts the two within a few pixels of each
other in a two-column layout, and pressing Down in a navigation pane would jump into the page
beside it.

Which arrows a control handles is per key, because the split is normally per axis: a `Slider`
keeps all four, a `TextBox` keeps Left and Right for its caret and lets Up and Down leave, a
`Splitter` keeps only its own axis. An arrow a control's `key_override()` returns false for is
taken by focus movement.

A claimed arrow keeps its meaning in every orientation. On a `Slider`, Up and Right
always increase and Down and Left always decrease, in either orientation; only
`direction_reversed()` swaps them.

### Popups and Modals

A popup is part of its owner, not a separate window. An element opens one with the protected
`open_popup(PopupRequest)`, updates it by calling that again, and closes it with
`close_popup()`; each element has at most one, and it closes when the element is destroyed.
Every `layout()` the window walks the elements that are live (not the tab ring, since an owner
need not be focusable: a `ToolTip` around a plain label is one, and not an unselected page or a
closed pane), keeps the ones with a popup open, and lays the request's `content` out as
described on [Layout](layout.md#popups-and-modals). The owner stays responsible for its own
state: on Escape or a light-dismiss click the window calls the request's `on_dismiss`, and the
owner closes the popup (at once, or after an exit animation) and updates whatever made it open.
Without an `on_dismiss` the window closes it.

A modal (`show_modal()`) is a separate ownership slot on the window. It swallows every input
event outside itself, has no light-dismiss, scopes the ring to itself, and focuses its
`default_focus()` when it opens.

A modal is deliberately not a popup. A `PopupRequest` carries a light-dismiss-on-outside-click
contract, while a modal keeps everything outside itself blocked until it is closed explicitly,
so `DialogControl` is a plain composed `Element` in the modal slot. The window hands it Escape
before anything inside it, and it takes it in `key_override()` (the Close button's click), as it
takes an Enter nothing inside it handled; `default_focus()` gives its opening focus. `Window`
drives it without knowing its type.

A few consequences a control or app author runs into:

- A `Flyout` opens from its anchor only when the anchor is the element it wraps directly.
  Wrapping the button in a `ToolTip` element hides the flyout from the popup layer; put the tool
  tip on the button itself (`.tool_tip`).
- Focus stays inside an open focus-taking flyout. To hand focus to another control after a pick,
  set it once the flyout has closed.
- Do not open a modal from a handler that runs during a focus change, such as a `ComboBox`'s
  `on_text_submitted()`. A commit triggered by losing focus would then re-enter the focus change
  that raised it. Show the message inline instead.

Tooltips set with `Element::set_tool_tip()` are a window service: the pointer resting on an
element for 800 ms (counted in `tick()`) opens one shared `ToolTip`; a click, a key, the wheel or
the pointer leaving closes it.

### Cursor

`Window::cursor_at(point)` finds the innermost element under the pointer (in the topmost popup,
the modal or the tree) and asks it and each parent in turn: first `cursor()` (set by the app),
then `cursor_at()` (the control's own: an I-beam over text, a hand over a link). The first answer
wins; the default is the arrow. The host applies it on `WM_SETCURSOR` and after each pointer
event.

## Composite Controls

`TextBoxControl` is `final`, so a control that needs an editable field (`ComboBox`,
`AutoSuggestBox`, `NumberBox`) owns one as a private `std::unique_ptr<TextBoxControl>` member instead of deriving from it,
and parents it to itself in its constructor. The composite, not the inner `TextBox`, is the one
focusable stop in the tab ring. It decides per event whether to act itself or delegate: a
`NumberBox` handles its spin buttons, ArrowUp/Down, PageUp/Down and Enter, and hands every
character edit to the `TextBox`.

The composite lists the inner `TextBox` in `for_each_child()`, so `clear_dirty()`,
`build_visual_tree()`, `sync_composition()`, `sync_native()` and `tick()` reach it without
forwarding. It overrides `collect_focusables()`, the window's private walk of live elements and
`automation_children()` to stop there, since it is the one tab stop and the one automation node.
`sync_spell_checker()`, which is only ever called on the focused element, is deliberately not
forwarded, since none of these fields hold prose. The inner `TextBox` copies and pastes through
`ui::clipboard()` and needs nothing forwarded for that.

Because the inner field stores its parent pointer, a composite must not move after construction.
Tests heap-allocate them for that reason (see [Write Tests](../development/write-tests.md)).

A `NumberBox` parses typed text only at three moments: Enter, losing focus
(`commit_text()`), and before every spin-button or arrow-key step, so stepping starts from what is
on screen (typing "7", then Up, gives 8). A valid parse is clamped and reformatted, an invalid one
reverts to the last value's text. Press-and-hold on a spin button repeats through `tick_override()`:
the first repeat after 500 ms, then every 100 ms.
On Windows the message loop ticks the window after every message, with a timer waking an idle
loop, and a headless host or test passes a synthetic delta.

## Text Editing

`TextBox` is the one control with a caret, so most of the text-editing rules live in it.

**Hit testing without glyph metrics.** `TextMeasurer` only measures whole strings. A single-line
`TextBox` finds caret and pointer positions by measuring prefixes of its text up to each cluster
boundary and binary-searching them, since prefix width never decreases for left-to-right text.
That is O(log n) measure calls per hit test, fine for field-sized text. The interface was
deliberately not grown for this; per-glyph metrics are the next step if profiling ever shows a
need. A multi-line box goes through its `TextLayout` instead.

**The cached measurer.** `render()` and `handle_pointer()` get no `TextMeasurer`, so `TextBox`
caches the measurer pointer, the text style and the left inset during `measure()`, the same
`mutable` cache pattern `VirtualList` uses. That is safe only because every measurer
(`DWriteMeasurer`, `HeadlessTextMeasurer`) is a long-lived object owned by the host.

**Selection.** The selection is `[min(anchor, caret), max(anchor, caret))`; equal means none.

- Shift+Left/Right/Home/End extend it. A plain Left/Right with a selection collapses it to the
  near boundary instead of stepping.
- Shift+click extends from the anchor. A double-click selects the run under the pointer
  (letters, digits and underscore form one class, everything else another); a triple-click
  selects the paragraph, which is the whole text on a single line. The host counts the clicks
  itself (see above).
- A right-click moves the caret, except inside the selection, as the Win32 `EDIT` and RichEdit
  controls do.

**Typing and pasting.** Paste strips characters below `0x20` (the same policy as typed input),
except that a box accepting returns keeps line breaks, normalized to `\n`. Past the first or last
line, Up and Down go to the start or end of the text, like a Win32 edit. A keystroke that would
exceed `max_length()` is dropped, as RichEdit does.

**The caret** blinks every 530 ms (`GetCaretBlinkTime()`'s default; the live OS setting is not
queried), driven by `tick_override()`. Typing or moving the caret resets it to visible, and gaining
focus starts it fully visible.

**The delete button** (the X) shows while a single-line, editable, non-empty box has focus and is
wider than five ems (`CTextBox::CanInvokeDeleteButton`); a click clears the text. Its column comes
and goes with focus, so a focus change is a layout change for a `TextBox`. A password box offers
only Paste and Select All in its context menu, and a double-click selects everything, so no word
boundaries are given away.

**Not done, on purpose for now:** undo and redo (Ctrl+Z only reverts an autocorrection, as the
very next key), IME composition input, horizontal scrolling of a single line longer than the box,
and Ctrl+Left/Right word jumps.

A `TextControl` with `set_selection_enabled(true)` takes focus on a click (it is
focusable when enabled and selectable). Drag, double-click and triple-click select a
word or paragraph, Ctrl+A and Ctrl+C work, and a Copy/Select All context menu acts on the
selection. The selection is drawn in `selection_highlight_color` (the accent by default) under the
theme's selection text, and only while the block has focus.

## Automation

The accessibility tree is a semantic layer on `Element` itself, not a second model:

- `automation_role()` returns the UIA control type (`Group` by default).
- Each pattern (`Invoke`, `Toggle`, `SelectionItem`, `Selection`, `ExpandCollapse`,
  `RangeValue`, `Value`, `Scroll`, `Grid`, `GridItem`, `Table`, `TableItem`) is a provider
  interface in `ui/Automation.hpp` (`InvokeProvider`, `RangeValueProvider`, ...), every member
  pure virtual and named after its pattern (`range_value()`, `text_value()`,
  `toggle_value()`), so one control can implement two. A control inherits the ones it implements
  privately and overrides them privately, so they never show in its public API.
- The protected `automation_providers(AutomationProviders& out)` lists what the control
  provides right now: `out.range_value = this;`, with an `if` for a pattern that only applies in
  some states (a sortable column header's Invoke). `ui::automation_providers(node)` reads it, for
  the UI Automation provider, `describe_automation()` and tests. A wrapper that stands in for its
  anchor (`ToolTip`, a `Flyout`) hands out the anchor's providers.
- `automation_name()` is `final`: the caller's `accessibility().name` wins, otherwise the
  control's `automation_name_fallback()` (its visible text). A control with no visible text
  (a `Slider`, an icon-only button) needs an explicit name from the caller, which every spec's
  `.accessibility` field provides.
- `automation_children()` lists immediate children, one level per call. It defaults to the
  children `for_each_child()` lists. A container whose automation tree has a level its element
  tree lacks answers `automation_parent_of()` for the children it moves under that level: a
  `TableView` lists a node per cell, and a template cell it owns sits under its cell's node.
- A control that draws its items itself makes a small childless `Element` per item for
  automation, parented to the control and placed over the item on arrange: `TreeView` rows
  (TreeItem, keyed by node key and nested by depth), `Tabs` tabs (TabItem, with a Close
  button), `MenuBar` headers and menu items, `Text` hyperlinks, `AutoSuggestBox` suggestions. Each
  reads its item live from the owner by index or key, and the owner keeps it for its own life, so
  a client holding one never sees it freed. One whose item went away reads as empty.
- `automation_focus()` names the node inside a control that has keyboard focus as automation
  sees it: the row, tab, menu item, list item or suggestion the control moves its own cursor over.
  `ui::automation_focus(window.focused())` follows it, and the provider answers `GetFocus` and
  `HasKeyboardFocus` with it.
- Events: after each `layout()` (the host lays out after every input), once the popups are
  resolved, the window reports to its `AutomationEvents`: menus that opened or closed (a menu
  counts as closed once its fade ends) with a structure change for the node that lists them, then
  a new focus node as a focus change or the same node's changed name, toggle, expand, selected,
  value or range value as a property change, then a structure change when the items around the
  focus node were added, removed or renamed.
  A node rebuilt for the same item (a table builds its rows again on every layout) with the same
  role and name is not reported again. The Windows host's `Win32AutomationEvents` raises the UIA
  events, only while `UiaClientsAreListening()`; tests install `ui::testing::TestAutomationEvents`.
- An open popup's content is listed last under the element that opened it, so a flyout's menu
  is a child of its button. Content that names a closer parent through `automation_parent_of()`
  is listed there instead: a menu under its header, a submenu under its item.

`include/winui/ui/AutomationTree.hpp` walks that layer headlessly. `automation_children(node)`
splices out decorative nodes (`Accessibility::decorative`, the "content versus chrome" bit) and
`NativeControl`s (their real child `HWND` answers `WM_GETOBJECT` itself), promoting their
children in their place. `describe_automation(root)` prints one line per node (role, quoted name,
patterns) and is the automation counterpart of `describe()`:

```text
Group
  Button "Save" [Invoke]
  CheckBox "Remember me" [Toggle=Off]
```

It walks the live tree on every call instead of caching, because UI Automation navigation is
low-frequency polling and a live walk can never go stale between layout passes.

On Windows, `src/platform/windows/Win32AutomationProvider.hpp` is one generic COM provider for every
control (`IRawElementProviderSimple`, `Fragment`, `FragmentRoot` and every pattern interface),
driven entirely by the semantic layer. `QueryInterface` succeeds for a pattern only when the
element advertises it. Providers are created per navigation call and never cached. Each stamps
itself with `Window::content_generation()` and returns `UIA_E_ELEMENTNOTAVAILABLE` once the tree
it came from has been replaced, instead of touching freed memory. An Invoke from a client is
dispatched through `Window::handle_automation_invoke()`, under the same `DispatchGuard` as a
click.

Provider lifetime has two guards, because they cover different things:

- **`Window::content_generation()`** catches whole-tree replacement (`set_content()`,
  `show_modal()`, `close_modal()`). Otherwise `Element` pointers are stable for the tree's life,
  since no container removes a single child once added.
- **`AutomationLifetime`**, one per `HWND` and shared by every provider spawned from it, carries an
  `alive` flag that the host clears at `WM_DESTROY`, before the `Window` is destroyed. That is the
  one case the generation cannot cover, as there is no window left to ask.

The root provider is the window's effective root (the modal if one is open, else the content),
recomputed on every call. The root's runtime id is null and UIA derives it from the `HWND`;
every other node uses `{UiaAppendRuntimeId, visual_id}`. Creating providers per call is fine
because UIA compares identity by runtime id, not by provider pointer.
The root provider leaves `ControlType`, `Name` and `AutomationId` empty, so UIA takes them from the
`HWND`: a top-level Window named after the title, which is how test tools find an app's windows.
`get_HostRawElementProvider` returns `UiaHostProviderFromHwnd`, so UIA merges the window frame
with the custom tree.

The host answers `WM_GETOBJECT` only for `UiaRootObjectId` (not `OBJID_CLIENT`), which is the
documented lParam for a UIA-only provider replying through `UiaReturnRawElementProvider`. At
`WM_DESTROY` it calls `UiaReturnRawElementProvider(hwnd, 0, 0, nullptr)` to tell UIA the window is
going.

Known simplifications of the Win32 provider:

| Where | What it does |
| --- | --- |
| Native controls | No `IRawElementProviderHwndOverride`; a `NativeControl`'s child `HWND` answers `WM_GETOBJECT` itself. |
| Selection | List views and `TableView` provide Selection. An item of another container (a radio button) treats `AddToSelection` as `Select` and refuses `RemoveFromSelection` with `UIA_E_INVALIDOPERATION`, since only selecting another item deselects it. |
| RangeValue | Writable unless the control says otherwise (a read-only `RatingControl`); disabled is not read-only. `ProgressBar` and `ProgressRing` report it as writable without a setter. `LargeChange` reuses the small change. |
| Scroll | View sizes always report 100 %. |

One build trap: under `WIN32_LEAN_AND_MEAN`, `UIAutomationCore.h` fails with "redefinition;
different basic types" unless `<objbase.h>` comes first, because its forward declarations rely on
the `interface` macro that `<objbase.h>` defines and lean mode leaves out. The `UIA_*ControlTypeId`,
`PatternId` and `PropertyId` constants live in `UIAutomationClient.h`.

## Menu access

Alt or F10 alone, or Alt with a letter no shortcut or control used, reach Windows as the system's
menu key (`WM_SYSCOMMAND SC_KEYMENU`). The host hands it to `Window::handle_menu_access()`, which
offers it to the tree (`Element::take_menu_access()`) in order. A `MenuBar` takes it: focus moves
to its first header, or the menu whose header starts with the letter opens. The window remembers
where focus was, and `Window::end_menu_access()` gives it back on Escape, on Alt again and after a
menu item runs. Alt+Space stays the system menu's.
