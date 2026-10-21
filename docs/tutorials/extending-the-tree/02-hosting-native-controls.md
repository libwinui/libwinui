# Hosting Native Controls

Put a real Win32 child window inside a declared tree.

The example for this page builds as `tutorial_12_hosting_native_controls`.

Every libwinui control is drawn by the framework and has no `HWND` of its own. `NativeControl` is the deliberate exception: an element that wraps a real child window of any window class, laid out by the same layout pass as everything around it. The example hosts a plain `EDIT`, a `RICHEDIT50W` and a bare window class created by name. The full source is `examples/tutorials/12_hosting_native_controls/main.cpp`.

Native controls are Windows only. Without a native host, in a headless test or on another platform, a `NativeControl` keeps its size and draws nothing.

## When a Native Control Is the Right Answer

Reach for one when the OS already does the job well and redoing it would take months:

- Rich text editing with RTF in and out, where the OS control's undo stack, clipboard formats and IME handling come along for free.
- A window class libwinui has no counterpart for: a shell control, or a third-party control shipped as a window class.
- Interop with existing Win32 code that already talks to an `HWND`.

And stay away from it when a framework control would do, because a hosted window gives up a lot:

| Hosted `HWND` | Framework control |
| --- | --- |
| Paints itself; follows the window's light and dark mode at best, never an era theme or high contrast palette. | Drawn from the theme. |
| Sits above everything the framework draws, so a flyout, menu or tooltip opening over it is hidden behind it. | Popups draw on top. |
| Clipped only by scrolling ancestors. Rounded corners, a `ScaleToFit` or a render transform do not reach it. | Clipped and transformed like the rest. |
| Not in the framework's tab ring. | In the tab ring. |
| Cannot be a row in a virtualizing list; creating a window per row per scroll step is far too slow. | Fine as a row. |
| Does not exist under the headless host, so it only appears in tests as recorded calls. | Fully testable. |

For text input, `ui::TextBox` and the framework's own `ui::RichEditBox` cover most needs without any of these limits.

## NativeControl and NativeHost

The design splits the job in two. `ui::NativeControl` is the element in the tree: it measures to a fixed size, draws nothing, and on every sync tells a `ui::NativeHost` what it wants. The host owns the real windows and does the Win32 calls. The element never calls Win32 itself.

```text
NativeControl (in the tree) --- sync_native() ---> NativeHost
                                                    |- Win32NativeHost: CreateWindowExW, MoveWindow, SetWindowRgn, ...
                                                    `- TestNativeHost: records the calls
```

The window is created lazily, on the first sync after the tree is attached to a real window under `ui::run()`, not in the constructor. From then on it moves when its arranged bounds change, is clipped to what scrolling ancestors leave in view, is hidden when it is collapsed or scrolled out, and is destroyed with the element.

The generic form needs nothing but a window class name. `ui::Native` is its spec:

```cpp
--8<-- "examples/tutorials/12_hosting_native_controls/main.cpp:native"
```

`.desc` is a `ui::NativeControlDesc`: the class name, the initial window text, and extra `WS_*` or control style bits (`WS_CHILD` is added for you). `.native_size` is required, because a native control takes no part in text measurement; it reports this size and nothing else.

`.control` matters more here than anywhere else, since reaching the `HWND` is the whole point. `native_handle()` returns it as a `ui::NativeHandle`, which is the `HWND` on Windows, or `nullptr` before the window exists, so check it.

To answer a window's notifications to its parent (`WM_COMMAND`, `WM_NOTIFY`, `WM_CTLCOLOR*`, a trackbar's `WM_HSCROLL`), derive from `NativeControl` and override `handle_native_notification()`. One-time setup messages go into `on_native_created()`, which runs once, right after the window was created. By then the window is registered with the host and already at its real size, because some control classes size their internals off the client rect when they process their first configuration message. That is also the place to ask the control what size it wants and pass it to `set_native_size()`. The [NativeControl reference](../../reference/controls/native-control.md) has a month calendar written that way.

## Two Examples: RichEditBox and EditBox

The repository has two hosted controls as example code, in `examples/native_controls` (`namespace native_controls`). They are not part of the library: copy them, or write your own the same way. Neither has a spec: build them with `std::make_unique`, keep a raw observer, and move the `unique_ptr` into the declared tree, which takes it as a child like any other (see [Mixing Imperative and Declarative](03-mixing-imperative-and-declarative.md)).

```cpp
--8<-- "examples/tutorials/12_hosting_native_controls/main.cpp:includes"
```

### EditBox

`native_controls::EditBox` is the stock Win32 `EDIT` class, with the `TextBox` and `PasswordBox` features the class can do: a header, placeholder text, read-only, max length, casing, font and colors, selection, and a password mode.

```cpp
--8<-- "examples/tutorials/12_hosting_native_controls/main.cpp:edit_box"
```

The styles the class reads only at creation are `EditBoxOptions`: one line or several (`accepts_return`), wrapping, alignment, and `password`. Everything else has a setter that works before and after the window exists. A height of 0 means one line of the box's font.

Placeholder text and the dark scroll bars need version 6 of the common controls, which an application opts into through its manifest. Without it the `EDIT` is the one from common controls version 5 and shows no placeholder. The example adds the dependency with a linker pragma:

```cpp
--8<-- "examples/tutorials/12_hosting_native_controls/main.cpp:manifest"
```

### RichEditBox

`native_controls::RichEditBox` is a real `RICHEDIT50W` window with the document calls the gallery samples use: character formatting, find, RTF load and save, spell checking with the window's own checker, and a context menu that follows the window's light or dark palette.

```cpp
--8<-- "examples/tutorials/12_hosting_native_controls/main.cpp:rich_edit"
```

`text()` returns UTF-8, empty until the window exists. The text lives inside the OS control, and keystrokes go straight to its `HWND` without passing through the framework. `on_text_changed()` is how the framework hears about them: the control tells its parent (`EN_CHANGE`), and the host routes that back to the element.

The formatting buttons call `toggle_bold()` and `toggle_italic()` on the observer, then `focus()` to hand keyboard focus back to the editor, since clicking the button took it:

```cpp
--8<-- "examples/tutorials/12_hosting_native_controls/main.cpp:layout"
```

## Layout and Focus Across the Boundary

**Bounds.** A native control sizes like any leaf: its fixed size plus margin, placed by its parent's alignment. `EditBox` adds its header above the box. After each arrange, the host moves the child window to the element's bounds, converted from DIPs to device pixels. Because only changed bounds cause a move, a still layout makes no Win32 calls.

**Z-order.** The child window is above the framework's whole surface, always. That is the airspace problem every host of `HWND`s inside a composited UI has, and there is no fix short of not hosting a window. Keep popups away from hosted controls, or accept that they open behind them.

**Tab ring.** A hosted control is not in the window's tab ring. It gets real keyboard focus from Windows when it is clicked, or from its own `focus()` call, and Tab inside it is the control's business. Tabbing from a framework control skips it. For a form the user fills in with the keyboard, that alone is a good reason to prefer `ui::TextBox`.

**Visibility.** A control inside a collapsed element, an unselected `Tabs` tab or an unselected `NavigationShell` page is hidden, and shown again when it comes back. Scrolled wholly out of a `Scroll`, it is hidden too; half out, it is clipped to the viewport.

**Lifetime.** The host has to outlive every native control synced against it, because the control's destructor asks the host to destroy its window. Under `ui::run()` that is automatic. In a test it means declaring the host before the controls.

## Testing With TestNativeHost

A hosted window cannot exist in a headless test, but everything the element asks for can be checked. `ui::testing::TestNativeHost` implements `NativeHost` by recording each call per `VisualId`. Like every test double it is in the testing tier, `<winui/testing.hpp>`, which the test executable gets by linking `winui::testing`:

```cpp
#include "winui/testing.hpp"
#include "winui/winui.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("A native control is created once and moved with its bounds", "[native]")
{
    ui::testing::TestNativeHost host; // before the controls: they call it from their destructors
    ui::StackControl            panel{ui::Orientation::Vertical};
    ui::NativeControl*          edit = panel.add(std::make_unique<ui::NativeControl>(
        ui::NativeControlDesc{.class_name = "EDIT", .initial_text = "hello"}, ui::Size{100.0f, 20.0f}));

    const ui::Size desired = panel.measure({200.0f, 200.0f}, ui::testing::HeadlessTextMeasurer{}, ui::Theme::default_light());
    panel.arrange({0.0f, 0.0f, desired.width, desired.height});
    panel.sync_native(host);
    panel.sync_native(host);

    const ui::testing::TestNativeHost::Entry* entry = host.find(edit->visual_id());
    REQUIRE(entry != nullptr);
    REQUIRE(entry->create_count == 1);
    REQUIRE(entry->desc.class_name == "EDIT");
    REQUIRE(entry->bounds.width == 100.0f);
    REQUIRE(entry->owner == edit);
}
```

An `Entry` records the description it was created with, the last bounds, visibility and clip, the owning element, and counters for creates, moves, clips and visibility changes. That is enough to test the parts of a native control that are yours: the size it asks for, when it moves, when a collapsed ancestor hides it. What the OS control then does with its window is Windows' business and needs a real run, so ask a person to click through the example.

`native_handle()` is always `nullptr` under a `TestNativeHost`, so code that talks to the `HWND` has to check for that, as `write_through_hwnd` above does.
