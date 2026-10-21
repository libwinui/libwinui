# Platform Seams

Every OS service a control needs sits behind an interface with a test double. A control calls
the interface; the Win32 host installs the real implementation; a test installs a recording or
scripted one. That is how a `TextBox` can copy, paste and show spelling squiggles in a unit test
with no clipboard and no dictionary.

## The Seam Pattern

Each seam has the same four parts:

1. **An interface** in `include/winui/render/` (or `ui/` for the window-level ones): a small
   abstract class with only the calls controls actually make.
2. **A test double** in `include/winui/testing/`, named `Test<Seam>` (`TestClipboard`,
   `TestNativeHost`, ...), part of the `winui::testing` library. It records what a control asked
   for and lets a test script the answer. It never touches the OS.
3. **A Win32 implementation** under `src/platform/windows/`, header included: the backend is
   private and never installed.
4. **An installer**: on `Window` for a per-window service (`set_compositor()`,
   `set_spell_checker()`, and so on), reached by a test through `ui::testing::seams(window)`. The
   pointer is optional and non-owning. The Win32 host creates the real object, owns it for the
   window's lifetime and installs it on `WM_CREATE`; a test installs its double; a headless host
   usually installs nothing. The clipboard and the launcher belong to the process instead: the
   backend makes them on first use, and a test swaps them with `ui::testing::set_clipboard()` and
   `ui::testing::set_launcher()`.

A missing per-window seam degrades gracefully. With no compositor, controls repaint without the fade; with no native host, a `NativeControl`
never creates its window. So the pure layout and focus tests need no seams at all.

How a seam reaches a control depends on who needs it:

| Delivery | Seams | Why |
| --- | --- | --- |
| Tree-wide, after every layout and input dispatch (`sync_composition()`, `sync_native()`) | Compositor, NativeHost | Every visual needs them, focused or not (hover state, a native control's position). |
| To the focused element only, right before a key or char (`sync_spell_checker()`) | SpellChecker | Only the focused field edits text. It is also synced as a field gains focus, so it is checked before the first keystroke. |
| Passed into the one call that may use it (`handle_context_menu(position, host)`) | ContextMenuHost | A menu may only be shown during that call; a control never caches the host. |
| Process-wide (`ui::clipboard()`, `ui::launch_uri()`, `ui::message_box()`, `ui::notify()`, `ui::set_jump_list()`, `set_text_analyzer()`) | Clipboard, Launcher, MessageBoxHost, Notifier, JumpListHost, TextAnalyzer | There is one of each for the whole OS session, so no window has its own, and a control needs no path to its window to use them. |

| Seam | Interface | Test double | Win32 implementation | Installed with |
| --- | --- | --- | --- | --- |
| Text measurement | `TextMeasurer` (`render/Text.hpp`) | `HeadlessTextMeasurer` | `DWriteMeasurer` (private) | passed to `Window::layout()` |
| Drawing | `Renderer` (`render/Renderer.hpp`) | `TestRenderer` | `D2DRenderer` (private) | passed to `Window::render()` |
| Composition | `Compositor` (`src/render/Compositor.hpp`, private) | `TestCompositor` (`src/testing/`) | `DCompCompositor` (private) | `Window::set_compositor()` |
| Native controls | `NativeHost` (`render/NativeHost.hpp`) | `TestNativeHost` | `Win32NativeHost` | `Window::set_native_host()` |
| Clipboard | `Clipboard` (`ui/Clipboard.hpp`) | `TestClipboard` | `Win32Clipboard` | `ui::testing::set_clipboard()` |
| Spell checking | `SpellChecker` (`render/SpellChecker.hpp`) | `TestSpellChecker` | `Win32SpellChecker` | `Window::set_spell_checker()` |
| Context menus | `ContextMenuHost` (`render/ContextMenuHost.hpp`) | `TestContextMenuHost` | `Win32ContextMenuHost` | `Window::set_context_menu_host()` |
| Launching URIs and files | `Launcher` (`ui/Launcher.hpp`) | `TestLauncher` | `Win32Launcher` (private) | `ui::testing::set_launcher()` |
| Message boxes | `MessageBoxHost` (`ui/MessageBox.hpp`) | `TestMessageBoxHost` | the task dialog in `Win32MessageBox.cpp` | `ui::testing::set_message_box_host()` |
| Notifications | `Notifier` (`ui/Notification.hpp`) | `TestNotifier` | the toast notifier in `Win32Notifier.cpp` | `ui::testing::set_notifier()` |
| Jump list | `JumpListHost` (`ui/JumpList.hpp`) | `TestJumpListHost` | the shell's destination list in `Win32JumpList.cpp` | `ui::testing::set_jump_list_host()` |
| Window state and frame | `WindowHost` (`src/ui/WindowHost.hpp`, private) | `HeadlessWindowHost`, through an adapter | the HWND host in `Win32WindowHost.cpp` | `Window::set_window_host()` |
| Text segmentation | `TextAnalyzer` (`render/TextAnalyzer.hpp`) | the built-in base class | `Win32TextAnalyzer` (system ICU) | `set_text_analyzer()` |

The step-by-step for a new one is on [Add a Platform Seam](../hooks/add-a-platform-seam.md).

### The Backend Is Private

Every Win32 implementation (`Win32Clipboard`, `Win32NativeHost`, `Win32ThemeSource`,
`Win32ContextMenuHost`, `Win32SpellChecker`, `Win32TextAnalyzer`, `D2DRenderer`, `DWriteMeasurer`,
the DirectComposition compositor, `Win32Launcher`) lives in `src/platform/windows/` and is not
installed. An app never names one: `ui::run()` and `Window::show()` install them, and what an app
used to ask a backend class directly has a portable function, such as `ui::os_theme()` for what
`ThemeMode::System` resolves to right now. The tests of the backend itself include these headers
from `src/`.

The seams are installed on a `Window` through `ui::testing::seams(window)`
(`winui/testing/WindowSeams.hpp`): the setters are private members of `Window`, so an app cannot
swap a service out from under a running window, and a test can still hand it a test double.

None of these headers includes `<windows.h>`. They name a window as `ui::NativeHandle`
(`core/NativeHandle.hpp`), which on Windows is `HWND__*`. `Window::native_handle()` and
`NativeControl::native_handle()` return it. Under `STRICT`, which `<windows.h>`
turns on unless `NO_STRICT` is defined, `HWND` is `struct HWND__*`, so forward-declaring
`HWND__` gives the identical type: a caller with a real `HWND` passes it unchanged, and a caller
that never touches Win32 never pays for the header. Inside `src/platform/windows/`,
`Win32String.hpp`'s `widen()` and `narrow()` are the only places a string crosses between the
framework's UTF-8 and the `...W` APIs' UTF-16.

## Clipboard

`Clipboard` is the system clipboard as an app sees it, trimmed to the three standard
formats an app actually puts there: text, a bitmap and storage items (file paths). It adds the
history and roaming switches of `SetContentWithOptions`, `available_formats()`, `clear()` and
`content_changed()`, a `Property<std::uint64_t>` counter that advances on every change so an app
can observe it. Reading is per format, so pasting text never decodes a bitmap that
happens to be there too. HTML, RTF, links, delayed rendering and custom formats are not covered.

`TextBox` uses `set_text()`/`text()` for Ctrl+C, Ctrl+X and Ctrl+V, through `ui::clipboard()`,
the one clipboard of the process. App code calls the same function, before `ui::run()` too. The
Win32 backend makes its `Win32Clipboard` on the first call and keeps it for the life of the
process; a backend without a clipboard returns one that lives in memory. `HeadlessUI` installs a
`TestClipboard` unless the test installed one first, so a test never writes to the real
clipboard.

`Win32Clipboard` writes these formats:

| Content | Formats written | Windows synthesizes |
| --- | --- | --- |
| Text | `CF_UNICODETEXT` | `CF_TEXT`, `CF_OEMTEXT`, `CF_LOCALE` |
| Bitmap | "PNG" first (alpha intact, what current apps paste first), then `CF_DIBV5`: 32 bpp, `BI_BITFIELDS` with an alpha mask, bottom-up | `CF_DIB`, `CF_BITMAP` (without alpha) |
| Storage items | `CF_HDROP` (a wide `DROPFILES` list) plus "Preferred DropEffect" holding the requested operation as a `DROPEFFECT` | |
| History and roaming switched off | "CanIncludeInClipboardHistory" / "CanUploadToCloudClipboard" set to 0 | |

Pasting a bitmap prefers PNG, then `CF_DIBV5`, then `CF_DIB`, decoded by WIC. `TestClipboard`
holds one `ClipboardContent` in plain members a test can seed and read, and reports the same
format names the Win32 backend would.

## Spell Checker

`SpellChecker` is the same shape: `available()`, `language()`, `check(text)` returning
`SpellingError` ranges with a suggested action, `suggest(word)`, `add_to_dictionary()`,
`ignore_for_session()`, `add_autocorrection()`, and two user settings, `highlight_enabled()` and
`autocorrect_enabled()`. `generation()` moves whenever anything that changes results changes, so
a field re-checks without being told why.

`Win32SpellChecker` wraps the Windows Spell Checking API (`ISpellChecker`, Windows 8+) and owns
the policy that API leaves to its caller:

- **The language** is the current keyboard layout's tag (`GetKeyboardLayout`, then
  `LCIDToLocaleName`), re-derived on `WM_INPUTLANGCHANGE` and `WM_SETFOCUS`, since the layout may
  change while another app has focus. One `ISpellChecker` is created per language on first use
  and kept for the process. A layout with no installed dictionary makes the checker unavailable
  rather than falling back to another language, and is cached as a null checker so it is not
  probed again on every keystroke.
- **The two Typing settings** ("Highlight misspelled words", "Autocorrect misspelled words") are
  undocumented registry values, `EnableSpellchecking` and `EnableAutocorrection` under
  `HKCU\Software\Microsoft\TabletTip\1.7`, read on `WM_SETTINGCHANGE` and when the window
  gets focus back. A missing value reads as on, Windows' own default, so an OS that drops them
  fails open.
- **Autocorrect** has a small built-in table of classic English typos ("teh"), applied only when
  the OS dictionary flags the word. App entries from `add_autocorrection()` are keyed lowercase,
  and a correction takes the typed word's capitalization ("Teh" becomes "The", "TEH" becomes
  "THE").
- **Offsets**: `ISpellChecker` reports UTF-16 ranges, which are mapped back to UTF-8 byte offsets.

A caller just reads the two flags and never learns where they came from. COM is initialized
lazily on the UI thread, which is normally in OLE's single-threaded apartment by then (the drop
target needs it, see [Drag and Drop](input-and-focus.md#drag-and-drop)). The spell-checker factory
is free-threaded, so either apartment works; the class owns the initialization only when it made
it. Every call must come from the UI thread.

A field is checked from the moment it first gains focus (the window syncs the checker in
`set_focus()` as well as before each key). It keeps the pointer after losing focus, so its
squiggles stay and it re-checks when `generation()` moves. A field that was never focused is
never checked, the same rule browsers apply to pre-filled text. `TextBox` and `RichEditBox`
check. The composites that own a `TextBox` don't forward it, since none of them hold prose.

`TestSpellChecker` takes the misspelled words and their suggestions up front, lets a test flip
the two settings, and counts what the control asked for. The `Win32SpellChecker` tests never
call `add_to_dictionary()` or autocorrect through the OS, since both write into the user's own
dictionary.

## Text Analyzer

`TextAnalyzer` answers three questions a text field needs: where the grapheme clusters are
(caret movement, Backspace), which characters count as word characters (the run a double-click selects) and
how `ui::CharacterCasing` changes typed text. It is a process-wide seam, installed with
`set_text_analyzer()`.

The base class is the fallback and what headless tests use. It is small and deterministic: a
cluster is a code point plus the combining marks, variation selectors, emoji modifiers and ZWJ
sequences after it; casing covers Latin, Greek and Cyrillic; anything past Latin-1 that is not in
a punctuation or symbol block counts as a letter.

`Win32TextAnalyzer` loads the system ICU (`icu.dll`, Windows 10 1903 and later) at run time. It
runs a character break iterator over a UTF-8 `UText`, so no offset crosses to UTF-16, uses
`u_isalnum` plus the mark categories for word characters, and ICU's locale-aware case mapping.
The ICU functions are declared by hand, because the SDK's `<icu.h>` links against `icu.lib`, a
load-time dependency Windows before 1903 cannot satisfy. The break iterator holds the text it was
last given, so calls are serialized with a mutex. Without ICU, clusters and word characters fall
back to the base class and casing to `LCMapStringEx` linguistic casing in the user's locale, so
older Windows still edits text, just less precisely.

## Native Host

`NativeControl` (`ui/hosting/NativeControl.hpp`) is the deliberate exception to "a control has no
`HWND`": a leaf that wraps a real Win32 child window (a RichEdit, a shell control, WebView2) for
things nothing in the framework can or should reimplement. `NativeHost` is the seam between that
leaf and whatever owns the real window. The control never calls Win32; it calls:

| Call | When |
| --- | --- |
| `ensure_created(id, desc)` | every `sync_native()`; idempotent, like `Compositor::sync_visual()` |
| `set_owner(id, control)` | right after creation, so the host can route a child's `WM_CONTEXTMENU` and notifications back |
| `place(id, bounds, visible)` / `set_bounds()` / `set_clip()` | whenever the arranged bounds or the visible part change |
| `set_visible(id, bool)` | Collapsed, scrolled wholly out of view, or no longer reached by the sync walk |
| `destroy(id)` | from the control's destructor |
| `native_handle(id)` | for control-specific messaging (`WM_SETTEXT` to a RichEdit); null in a headless host |

Everything is keyed by the control's `VisualId` and in DIPs; `Win32NativeHost` converts to device
pixels itself. `Window::sync_native()` wraps each pass in `begin_sync()`/`end_sync()`, so a control
the walk did not reach (a `Tabs` page that is no longer selected) gets hidden. The same pass
hands the window's spell checker to every native control, creation pass included, so a control
several levels down gets it after one sync. The control decides whether that means re-checking,
since only it knows whether its content changed.

`TestNativeHost` records the lifecycle (create once, move on every changed arrange, hide, show,
clip, destroy) as plain state, since that part is ordinary C++ behavior. It must outlive every
control synced against it.

Known limits, stated in the header: the native window is not theme-driven, it is not in the tab
ring (it takes real keyboard focus from Windows when clicked), and it is clipped only by
scrolling ancestors (through `SetWindowRgn`), not by rounded clips or render transforms. The
host window has `WS_CLIPCHILDREN`, so Direct2D never paints over a child. Hosting one is walked
through in [Hosting Native Controls](../../tutorials/extending-the-tree/02-hosting-native-controls.md).

### Moving a Child Window

After it moves a child, `Win32NativeHost` repaints it synchronously (`RedrawWindow` with
`RDW_UPDATENOW`) instead of trusting the `WM_PAINT` that `MoveWindow` queued. `WM_PAINT` is
synthesized, not posted: `GetMessage` only makes one once the queue holds nothing else, and a
parent's is served before its children's. While any animation runs, the host window is
invalidated after every message (see [Message loop](#message-loop)), so the child's paint could
starve for as long as the animation runs, and a scrolled control would show stale pixels.
`MoveWindow`'s blit is kept (no `SWP_NOCOPYBITS`), so the redraw only refreshes what changed. One
extra paint per move is fine for an escape hatch used a few times per window.

The clip region is set before the move and without its own repaint, so the synchronous repaint
already paints inside it and a control scrolling out of a viewport never flashes its old region.
Region edges are rounded the same way `set_bounds()` rounds the window, so the clip meets the
viewport edge exactly. The system owns an `HRGN` only once `SetWindowRgn` succeeds, so a failed
call deletes it.

### EditBox

`native_controls::EditBox` (`examples/native_controls/edit_box.hpp`) wraps the stock `EDIT` class,
the least capable native control, to show that the `NativeControl` escape hatch works with no
special handling anywhere. It is example code, not part of the library: the library ships the
extension point (`ui::Native`, `ui::NativeControl`), the gallery, tutorial 12 and the tests use
the example. It maps `TextBox`/`PasswordBox` properties onto EDIT messages and styles:

| Property | EDIT |
| --- | --- |
| `set_placeholder_text()` | `EM_SETCUEBANNER` |
| `set_max_length()` | `EM_SETLIMITTEXT` (0 restores the class limit) |
| `set_character_casing()` | `ES_LOWERCASE` / `ES_UPPERCASE` |
| `set_foreground()` / `set_background()` | answered from `WM_CTLCOLOREDIT` / `WM_CTLCOLORSTATIC` through `NativeControl::handle_native_notification()` |
| `set_number_only()` | `ES_NUMBER`, which `TextBox` has no counterpart for |

Multi-line, wrapping, alignment and password are creation-only styles and live in
`EditBoxOptions`; only `ES_LOWERCASE`, `ES_UPPERCASE` and `ES_NUMBER` can change after creation.
What the class cannot do, EditBox does not offer: character spacing, a selection color (it is
always the system Highlight), per-range formatting, Redo (EDIT keeps a single undo step) and spell
checking.

The class has a few traps the wrapper deals with:

- **Border.** It uses `WS_EX_CLIENTEDGE`, not `WS_BORDER`. A themed EDIT draws its visual-style
  border into the client edge, and a `WS_BORDER` box under the light visual style draws no border
  at all (dialog templates convert `WS_BORDER` for the same reason).
- **Font.** A stock EDIT with no font draws in `SYSTEM_FONT`, the Windows 3.1 bitmap face. EditBox
  sets the message font at the window's DPI and rebuilds it when the window moves to a monitor
  with another scale. Before the window and font exist, a box with no height estimates 23 DIPs,
  one line of the 9 pt message font at 96 DPI, as a dialog lays an EDIT out.
- **Change notification.** A multi-line EDIT sends no `EN_CHANGE` for `WM_SETTEXT` (a single-line
  one does), so EditBox calls its `on_text_changed()` handler itself for text set from code.
- **Password mask.** `EM_SETPASSWORDCHAR` takes one UTF-16 unit, so a mask outside the BMP falls
  back to the default. Revealing the text sets a zero mask, but the box stays a password box
  with no Cut or Copy.
- **Colors.** An enabled, editable EDIT asks for `WM_CTLCOLOREDIT`, a read-only or disabled one
  for `WM_CTLCOLORSTATIC`, whose system answer is the dialog color. In a dark theme the EDIT gets
  the `DarkMode_Explorer` visual style (dark scroll bars) and answers both with the palette's text
  on the Fluent `TextBox` rest fill over the window's base background, made opaque because GDI
  paints with a solid brush. In a light theme it is the plain system EDIT. The rest of the
  `Theme` (era packs, high contrast colors) does not reach it.
- **Context menu.** An EDIT answers `WM_CONTEXTMENU` itself with its own system-themed popup and
  never tells the parent. EditBox subclasses it (`SetWindowSubclass`, hence the `comctl32`
  dependency) so it forwards the message like RichEdit does, and the menu comes from the
  framework's context menu host in the window's light or dark palette. The items are
  RichEditBox's minus Redo and the spelling section. EDIT has no `EM_CANPASTE`, so Paste is
  enabled from `IsClipboardFormatAvailable(CF_UNICODETEXT)`.

`PlaceholderText` and the dark visual style need comctl32 version 6, which an app opts into with
a manifest dependency on Microsoft.Windows.Common-Controls 6.0 (the gallery has one). Without it
the EDIT is comctl32 5's and shows no placeholder.

### RichEditBox

`native_controls::RichEditBox` (`examples/native_controls/rich_edit_box.hpp`, example code like
`EditBox`) wraps `RICHEDIT50W` from
`MSFTEDIT.DLL`, the version current Win32 apps ship and the same RichEdit WinUI's own
`RichEditBox` runs windowless, not the legacy `RICHEDIT20W` from `RICHED20.DLL`. `MSFTEDIT.DLL`
registers the class as a side effect of loading and has no init call (unlike
`InitCommonControlsEx`), so it is loaded once, lazily, before the first RichEditBox is created.
`EDIT`, by contrast, is registered by user32 for every process.

Its API follows `RichEditTextDocument` where the gallery samples need it:

| Text document API | RichEditBox |
| --- | --- |
| `ITextCharacterFormat` Bold, Italic, Underline, ForegroundColor, BackgroundColor | the character format setters; `FormatEffect.Toggle` is `toggle_bold()` / `toggle_italic()` |
| `ITextRange::FindText` | `find_all()` |
| `Document.LoadFromStream` / `SaveToStream` with `TextSetOptions.FormatRtf` | `load_rtf()` / `save_rtf()` |
| `CommandBarFlyout` customization | app context-menu items |

Offsets are UTF-8 byte offsets over the control's text, with each paragraph mark counted as one
`\r`, and are mapped to the control's UTF-16 positions on every message.

The context menu has `TextBox`'s items plus Undo, Redo and Delete. Paste asks `EM_CANPASTE` with
format 0 ("can anything be pasted"), which also covers the read-only state. The right-clicked
word, for the spelling section, is found with `EM_CHARFROMPOS`, converting window-client DIPs to
the control's client pixels through `bounds()` and the window scale. A menu opened from the
keyboard (Shift+F10, the Menu key) anchors 8 DIPs inside the control's top-left corner instead of
at the caret, because `GetCaretPos` returns device pixels with no DPI scale at hand.

The in-tree `ui::RichEditBoxControl` is a different control: it is drawn by the framework and never
touches RichEdit. It reads and writes a subset of RTF 1.9 through a private parser
(`src/ui/text/Rtf.hpp`, never installed), enough for what a `RichTextFormat` carries: the font and
color tables, `\b`, `\i`, `\ul` and its variants, `\strike`, `\cf`, `\highlight`, `\cb`,
`\chcbpat`, `\f`, `\fs`, `\plain`, `\par`, `\line`, `\tab`, `\uN` with `\ucN`, `\'hh` in code page
1252, and the control symbols. Destinations it does not understand (the `\*` ones, the
stylesheet, info, pictures, objects, headers) are skipped whole, so a WordPad or Word file loads
as its text in those formats.

## Context Menu

A right-click menu is shown by the OS, not drawn in the tree. Every native Windows text field pops
a real USER32 menu for its spelling suggestions, and that brings OS theming, keyboard navigation
and screen-reader support for free.

`ContextMenuHost::show(position, items)` takes a flat list of `ContextMenuItem`s (text, enabled,
separator) and returns the index picked, or nothing for a dismissal. It is deliberately flat:
no submenus, icons or check marks, since a spelling menu needs none. The flow:

1. The host receives `WM_CONTEXTMENU` (a right-click, Shift+F10 or the Menu key) and calls
   `Window::handle_context_menu(position)`.
2. A positioned request first focuses the focusable element under the pointer, as a press would.
3. `on_context_requested()` handlers get the first chance, innermost first. Then the focused
   element's `handle_context_menu(position, host)` may call `host.show()`, and only during that
   call.

`Win32ContextMenuHost` turns it into `TrackPopupMenuEx` with `TPM_RETURNCMD` (escaping `&` so
text is not read as a mnemonic). `show()` runs the menu's own modal loop and returns only when
the menu closes; the window keeps repainting, since that loop still dispatches `WM_PAINT`. A
right-click inside a native child window never reaches the framework's hit testing, so the host
resolves the owning `NativeControl` itself and calls `Window::handle_native_context_menu()`.

### Dark Menus

USER32 draws popup menus in the system menu colors, which follow neither the libwinui `Theme`
nor "Apps use dark mode". The only lever is uxtheme's undocumented immersive app mode, wrapped
by `set_app_dark_mode()` in `src/platform/windows/DarkMode.hpp`. Its entry points are exported by
ordinal only, so each is resolved at run time and the call degrades to a no-op (light menus)
where it is missing:

| Ordinal | Function | Windows |
| --- | --- | --- |
| 135 | `SetPreferredAppMode(PreferredAppMode)` | 10 1903 and later (build 18362) |
| 135 | `AllowDarkModeForApp(bool)` | 10 1809 (build 17763) |
| 104 | `RefreshImmersiveColorPolicyState()` | |
| 136 | `FlushMenuThemes()` | |

Ordinal 135 changed its signature between the two builds, so the build number is checked
through ntdll's `RtlGetNtVersionNumbers` (`GetVersionExW` lies without a compatibility manifest)
instead of just testing for the export. Below build 17763 there is no dark mode. On 1809 dark can
only be allowed, not forced, so a dark-themed window still gets a light menu unless the OS is
dark too. From 1903 on it uses `ForceDark`/`ForceLight`, so the menu follows the app's theme and
not the OS setting.

The switch is binary: dark or light, never an era theme's own colors (that would need
owner-draw). It is also per process while a `Theme` is per window, so the last window to show a
menu wins. The host is told dark or light through `set_dark()` rather than reading the `Theme`,
because nothing below `Window` may depend on the theme's owner. `EditBox` and the tray menu use
the same switch.

`TestContextMenuHost` records every menu and answers with a scripted choice:
`host.pick("Add to dictionary")` by visible text, `pick_index()`, or the default dismissal. A
script applies to the next `show()` only.

In-tree menus (`MenuFlyout`, `CommandBarFlyout`) are ordinary popups, not this seam. An app's own
right-click menu is a `MenuFlyout` opened from `on_context_requested()`.

## Compositor

The seam for compositor-owned animation: `sync_visual()`, `animate_opacity()` and
`remove_visual()`, keyed by `VisualId`. It reaches controls
through `sync_composition()` after layout, input and focus changes. `DCompCompositor` layers
DirectComposition overlay visuals over the Direct2D content; `TestCompositor` records each call
as a struct (`SyncVisual`, `AnimateOpacity`, ...). The layer calls (`supports_layers()`,
`sync_layer()`, `paint_layer()`, `animate_layer()`, `stop_layer_animations()`, `remove_layer()`,
`now()`) carry an element's continuous motion; `Window` makes them from `take_damage()`, and each
has a default body, so a compositor without layers implements none of them. When
DirectComposition is unavailable the window simply has no compositor. The full picture is on
[Rendering](rendering.md#the-compositor-seam) and [Compositor Layers](rendering.md#compositor-layers).

## Launcher and Shell Dialogs

### Launcher

`ui::launch_uri()` hands a URI to whatever app the OS has registered for it, and
`ui::launch_file()` opens a file with the app registered for its type. A hyperlink `Button`'s and
a hyperlink inline's `navigate_uri` call `launch_uri()` after the click handler and the command
have run. Both go to the process's one `Launcher`.

`Win32Launcher` calls `Windows.System.Launcher::LaunchUriAsync` through C++/WinRT for a URI and
`ShellExecuteEx` with `SEE_MASK_ASYNCOK` for a file, fire and forget, so the UI thread never
blocks on the shell. `TestLauncher` records the URIs and files; `HeadlessUI` installs one unless
the test installed its own.

### Message Box

`ui::message_box()` shows the OS's own message box and returns the button the user chose. It goes
to the process's one `MessageBoxHost`. The Win32 one is the task dialog (`TaskDialogIndirect`),
which has a heading line the plain `MessageBox` lacks. The task dialog is in version 6 of the
common controls, which a process only gets through a manifest; an executable without one borrows
shell32's manifest (its resource 124) in an activation context for the length of the call. Where
even that fails, `MessageBoxW` shows the box with the heading as its first paragraph.

The call runs the dialog's own modal loop on the UI thread, which keeps the app's windows
painting; no second thread is needed, as the task dialog works in either apartment.
`TestMessageBoxHost` records each box with its owner and answers with a value the test sets;
`HeadlessUI` installs one unless the test installed its own, so a test never blocks on a real
box. A backend without message boxes answers the choice that changes nothing: Cancel, No when
there is no Cancel, else OK.

### Notifications

`ui::notify()` shows a toast through the process's one `Notifier`. The Win32 one uses
`Windows.UI.Notifications` through C++/WinRT and needs neither a package nor a tray icon:

- Toasts go out under an AppUserModelID: the one the app set for itself
  (`SetCurrentProcessExplicitAppUserModelID`), else `libwinui.<exe name>.<hash of its path>`, so
  two copies of a program in different folders stay apart.
- The first call writes `HKCU\Software\Classes\AppUserModelId\<id>` with a `DisplayName` (the
  executable's FileDescription, else its file name) and an `IconUri` (its first icon, saved as a
  PNG under `%LOCALAPPDATA%\libwinui\notifications`). That is what the toast's header shows.
- `NotificationIcon::Info`, `Warning` and `Error` become the toast's app logo, the system's stock
  icon saved as a PNG beside it. `App` and `None` add no picture, since the header already shows
  the program's icon.
- The notifier of an unpackaged program can't report its setting (`Setting()` fails with "Element
  not found") but its toasts show, and the system applies the user's notification settings and
  do-not-disturb to them.
- A click raises `Activated` on a worker thread; it is posted to the UI thread through a hidden
  message-only window (`Win32MessageWindow`) and runs `on_click`. Without a COM activator, a click
  after the app has quit opens nothing.

`TrayIcon::notify()` takes the same `Notification` and shows it as the tray icon's balloon, which
Windows 10 and later turn into a toast with the tray icon as its source. `TestNotifier` keeps each
notification and can click one; `HeadlessUI` installs one unless the test installed its own.

### Jump List

`ui::set_jump_list()` and `ui::add_recent_file()` go to the process's one `JumpListHost`. The Win32
one builds the list with the shell's `ICustomDestinationList`:

- Each `JumpTask` becomes an `IShellLink` to the running executable with the task's arguments,
  its description as the tooltip and its title in the link's `System.Title` property, added with
  `AddUserTasks()`. The list also keeps the Recent section (`AppendKnownCategory(KDC_RECENT)`), and
  an empty list deletes the app's list.
- The shell's list objects are made for a single-threaded apartment, so each call runs them on a
  short-lived thread of its own and waits for it, whatever apartment the UI thread is in.
- The list belongs to an AppUserModelID: the one the app set for itself, which its toasts use too
  (`Win32AppIdentity.cpp` reads it for both), else none, which leaves it to the ID the shell
  derives from the executable, the same one its taskbar button has.
- An empty `icon` uses the executable's first icon and a `"#101"` resource names it in place. Any
  other icon is drawn at 32 pixels and saved as an icon file under
  `%LOCALAPPDATA%\libwinui\jumplist`, since a shell link only takes an icon from a file.
- `add_recent_file()` calls `SHAddToRecentDocs`, under the app's own ID when it set one. The shell
  shows a recent file in the jump list only for a file type the app is registered to open.

`TestJumpListHost` keeps the last tasks and every recent file; `HeadlessUI` installs one unless
the test installed its own.

### Single Instance

`ui::claim_single_instance(id, on_activated)` is not a seam either: a free function with a Win32
implementation in `Win32SingleInstance.cpp` and one in `NoBackend.cpp` that always answers true.

- The claim is a named mutex, `Local\libwinui.instance.<id>`, so it holds per user session. The
  first process to create it keeps it open until it exits.
- The holder titles its message-only window (`Win32MessageWindow`, the one that also carries a
  toast's click) `libwinui.instance.<id>`. A later start finds that window with
  `FindWindowEx(HWND_MESSAGE, ...)`, retrying for up to five seconds while the holder is still
  starting, and sends `WM_COPYDATA` with its working directory and arguments, each UTF-8 and ended
  by a NUL.
- The holder copies the data, answers at once and posts the rest, so `on_activated` runs on its
  UI thread from the message loop after the sender has been released. The sender calls
  `AllowSetForegroundWindow` for the holder first, so `on_activated` may bring a window forward.
- The message window lets `WM_COPYDATA` through UIPI, so an elevated holder still hears a start
  that is not elevated.

The tests run both sides in one process: the claim on the test thread, the forwarding on a second
thread, with the test thread pumping messages.

### Shell Dialogs

The file and folder pickers (`include/winui/ui/Pickers.hpp`) are not a seam. They are free
functions, `ui::pick_file()`, `pick_files()`, `pick_save_file()`, `pick_folder()` and
`pick_folders()`, each taking the owner `Window` and an options aggregate, kept outside the
element tree on purpose: what sits in the tree is a spec, what you do is a function. The Win32
implementation (`src/platform/windows/ShellDialogs.cpp`) keeps the Windows App SDK's picker
shapes as private types underneath; a platform without a backend returns nothing. A caller wires the result into a
control from a handler, like any other side effect.

They use the common item dialog (`IFileOpenDialog`/`IFileSaveDialog`), not the legacy
`GetOpenFileName`/`GetSaveFileName`. Titles, names and filters are UTF-8, paths are
`std::filesystem::path` both ways. The owner is passed as a `Window` and the host resolves its
`HWND`, so an app never includes `<windows.h>` for this header.

Each call blocks until the dialog closes, but the dialog runs on its own thread in the
single-threaded apartment the common item dialog needs:

- The UI thread may be in the MTA (an app may have put it there before its first window, which
  also keeps OLE's drag and drop off). `CoInitializeEx` cannot change that afterwards, and
  `IFileDialog::Show()` called from the MTA never returns instead of failing.
- The calling thread keeps pumping messages rather than joining. An owned window on another
  thread shares its owner's input queue and sends it messages, so a plain join would hang the
  dialog too. The window keeps painting meanwhile, and the dialog stays modal because `Show()`
  disables the owner.
- A `WM_QUIT` that arrives while the dialog is up is posted again once it has closed.
- The focus the dialog thread hands back on close is useless, since that thread does not own the
  window, which would then take no keys until clicked. So the focused window is recorded first
  and focused again on the calling thread afterwards, mirroring WinAppSDK's
  `PickerCommon::DialogFocusRestorer`.

The storage pickers are ported from the Windows App SDK's own implementation
(`dev/Interop/StoragePickers`): filter validation and configuration follow `PickerCommon`'s
`ValidateSingleFileTypeFilterElement`, `ValidateFolderPath`, `TryParseFolderItem`,
`CaptureFilterSpecData` and `ConfigureDialog`. A picker's `folder` wins over its
`start_location`. With `remember_last` on (the default) either one only applies when the dialog
has no remembered folder, and with it off the dialog opens there every time. A known folder that
is unavailable falls back to Documents. The folder dialog sets `FOS_FORCEFILESYSTEM`, since a
library or "This PC" has no path to hand back.

There is no headless double: a real modal OS dialog has no useful headless form, so only the
filter-building helper (`picker_file_types()`, in the backend's `ShellDialogs.hpp`) is unit-tested. If you need a testable file
choice in an app, put your own interface in front of these functions.

## Window Host

`WindowHost` carries window state across the boundary in both directions: the user maximizes
from the caption, the app goes full screen from a button. The host is the only writer of what
the window *is* (`Window::state()`, `active()`, `visible()`, `caption_insets()`); the app only
*asks* (`request_state()`, `restore()`, `close()`), and a request may be refused or complete on
the OS's schedule. Everything the app configures on `Window` (title, style, backdrop, icon, system
menu, taskbar state) is remembered there and pulled by the host when it is ready, so the order
of setting it and installing the host does not matter. `WindowHost` is private to libwinui. `HeadlessWindowHost` implements the same
rules in memory, and `HeadlessUI::simulate_state()` plays the user changing the state from
outside.

## The Win32 Host

`src/platform/windows/Win32WindowHost.cpp` is the one place that turns a `Window` into an `HWND`. It
keeps one heap-allocated `HostState` per window: the Direct2D target, the measurer, the seam
implementations above, the frame state and the `Window` itself.

### Message Loop

`ui::run()` handles one message, then ticks every open window with real elapsed time, and
blocks in `GetMessageW` only when the queue is empty. The reasons:

- `WM_TIMER` is low priority and only synthesized once the queue is empty, so continuous
  `WM_MOUSEMOVE` starves it. An `tick_override()` animation driven by the timer alone (a spinning
  `ProgressRing`) freezes while the mouse moves.
- Draining the queue with `PeekMessage` before ticking does not help, since the drain never ends
  while fresh mouse moves keep arriving, and throttling ticks to 60 Hz makes the repaints follow
  the bursty arrival of input.
- The clock is `QueryPerformanceCounter`; `GetTickCount64()`'s roughly 15.6 ms steps are too
  coarse for smooth motion.
- It also ticks right before dispatching. Otherwise an animation a click starts gets all the idle
  time since the previous message on its first tick and skips up to 100 ms of its curve.

The 100 ms `WM_TIMER` wakes the loop while a window is busy with time and nothing else arrives.
After each tick the host asks `Window::wants_ticks()`: whether the tick changed something on a
shown window, an element called `request_tick()` (a wait of unknown length that changes nothing
until it ends), work was posted, a drag or connected animation runs, the app set an `on_tick()`
handler. Three seconds (`IdleGraceMs`) after the last such tick of any window, the
timers stop, so an idle app takes no CPU at all until a message arrives; the next busy tick starts
them again.

A pending `after()`/`every()` timer does not make a window busy, and neither does an element that
called `request_tick_in(ms)` (a wait of known length, such as the next frame of an animated image).
After each tick the host also asks `Window::next_wake()`, the earlier of the two, and sets the
`WM_TIMER` period to that wait when it is sooner than the 100 ms tick, or when the window is idle,
so a one-hour `after()` wakes the process once. An element's request holds for one tick: it asks
again from its next `tick_override()`, and a hidden or minimized window keeps none.

What such an element changes when its wake falls due is a timed step, and a timed step does not
make the window busy either: a caret blink or the next frame of a GIF is drawn, and the window goes
back to sleep until the next one. `Window::tick()` tells it from other changes with a count of
`mark_dirty()` calls per thread:

- `Element::tick()` notes, per element and without its children, whether its `tick_override()` marked
  anything and whether it asked for `request_tick_in()`. A mark without a wake is busy, and so is
  any mark from the element's own animations.
- In a tick where no element's wake was due, every mark an element makes is busy. So an animation
  that also waits for something later (a Carousel sliding while its buttons wait to fade) keeps
  the window busy in all the ticks between.
- Posted work, timers and the `on_tick()` hook are never a timed step.
- Dirt the last tick's timed steps left undrawn does not count at the next tick, unless anything
  marked the tree since. Input, layout or a setter in between makes the tick busy, which is what
  starts the grace for a hover fade.

The timer is re-armed only when it has to go off sooner or after it has gone off, since re-arming
after every message would keep postponing it under a stream of mouse moves. Windows rounds the
period up to its timer resolution (about 15.6 ms), so `every(20ms)` runs on a 20 ms schedule with
that much jitter, and nothing runs more often than every 10 ms (`USER_TIMER_MINIMUM`). Every quiet wait in the controls asks for its end with `request_tick_in()` and costs one wake-up:

- a caret blink (530 ms each way, so a focused text box wakes its window twice per 1.06 s)
- a tip's delay, the window's own for `set_tool_tip()` (800 ms) and a `ToolTip`'s `delay()`
- a submenu's hover delay to open or close (400 ms)
- the spell check after the last keystroke (250 ms)
- an overlay scroll bar's 2 s before it fades (`ScrollBarOverlay::fade_wait()`), and Carousel's
  three seconds before its buttons hide

The fades and slides that follow are animations and keep the window busy while they run. A test runs a real window past the grace and counts the
timer messages of the next second: none.

An animated `Image` asks for its next frame with `request_tick_in()`, so a GIF with 30 ms frames
plays at its own pace however rarely the window ticks otherwise. It also pauses while nobody can
see it, through the measuring pass of each frame ([On Screen](rendering.md#on-screen)). The scroll
that brings it back produces a frame and so a measuring pass, which tells it at once, and the
`request_tick_in()` it makes from there wakes the window too.

An `Image` whose source is still decoding (`ImageSource::load()`) costs no wake-ups either. Its
first tick hands the decode a callback that posts empty work to the window through a `WindowRef`,
and the decoding thread runs it when it is done. The post wakes the window, and that tick shows the
picture. The callback holds no pointer to the Image, so the Image may be destroyed first, and a
post to a closed or destroyed window is dropped.

Extra ticks are cheap tree walks, because drawing is paced by `WM_PAINT`. A paint that finds
nothing on screen changed still calls `DwmFlush()` before it returns, so it waits for the next
composition the way a presented frame does. Without that wait, an animation scrolled out of view
(dirty on every tick, drawing nothing) ran the loop at full speed and took a whole core. While a window is
minimized its time keeps moving, so a tooltip delay or animation is in the right place on restore,
but no repaint is asked for, and a change on a hidden or minimized window does not keep it busy.

After each `WM_PAINT` the host calls `RedrawWindow(RDW_ALLCHILDREN | RDW_UPDATENOW)`. Child
`WM_PAINT` has the same starvation problem as `Win32NativeHost`'s moves: during an animation the
host window is invalidated after every message, and a hosted RichEdit's paint never came, so
typing showed nothing. `RDW_UPDATENOW` without `RDW_INVALIDATE` costs nothing when no child owes a
paint, and `WS_CLIPCHILDREN` keeps the Direct2D drawing out of the child rectangles.

### Closing and Destruction

Every close path (the caption button, Alt+F4, the taskbar, `Window::close()`) goes through
`WM_CLOSE`, where `handle_close_request()` may refuse. `Window::close()` posts `WM_CLOSE` rather
than sending it, because it is usually called from a click handler deep inside the window's own
dispatch. For the same reason a `HostState` is destroyed only at the loop's next safe point after
its `WM_DESTROY`, never inside a message dispatched to it.

At `WM_DESTROY` the host clears the automation lifetime flag and calls
`UiaReturnRawElementProvider(hwnd, 0, 0, nullptr)` to tell UI Automation the window is going (see
[Input and Focus](input-and-focus.md#automation)). Inside `HostState`, `window` is declared last so
it is destroyed first: `NativeControl` destructors in the tree call into the native host, which
must still be alive.

### More Than One Window

`Window::show()` runs another window on the existing message loop, next to the main one. The
host only borrows a `Window` (`HostState::window` is a plain pointer): the app owns every window
it shows and decides when the object dies. Closing destroys the OS window and leaves the `Window`
a valid object that `show()` can open again; `~Window()` destroys the OS window if it is still
there (`detail::destroy_native_window()`, `src/platform/Backend.hpp`), which is why a window
must not be destroyed from inside one of its own handlers. Nothing in a window's content may
capture another window's state by reference unless that state outlives it.

An owned modal window disables its owner while it is up. The owner is enabled again before
`DefWindowProc` destroys the window, or the still-disabled owner cannot take the activation and
Windows gives it to another app.

### Frame and Window State

The style is `WS_OVERLAPPEDWINDOW` minus what `WindowStyle` turns off, plus `WS_CLIPCHILDREN`
(without it every Direct2D repaint, one per mouse move, paints over native child windows and they
flicker).

| `Frame` | Style | Why |
| --- | --- | --- |
| `Standard` | overlapped | the OS draws the caption and borders |
| `Borderless` | overlapped, caption removed in `WM_NCCALCSIZE` | the caption bits make the shell treat it as a real window: snap, Win+arrows, the minimize animation, the shadow |
| `None` | `WS_POPUP`, plus `WS_SYSMENU` and the minimize and maximize boxes | the taskbar button can still minimize and restore it |

A few rules the frame code follows:

- `WS_SYSMENU` is what puts caption buttons on a caption at all. There is no style bit for the
  close button alone; graying the system menu's Close disables it (`CS_NOCLOSE` would too, but
  it is class-wide).
- Style changes need `SWP_FRAMECHANGED`, or the frame keeps cached metrics and the caption
  buttons do not update. Topmost can only change through `SetWindowPos` (`HWND_TOPMOST` /
  `HWND_NOTOPMOST`), never through `GWL_EXSTYLE`. `WS_EX_TOOLWINDOW` is only re-read by the shell
  when a window is shown, so a visible window is hidden around that change.
- Hiding the caption icon needs `WTNCA_NOSYSMENU` next to `WTNCA_NODRAWICON`, because the icon is
  also the system-menu button. That drops only click-the-icon; Alt+Space and a caption
  right-click still open the menu.
- Custom system-menu ids stay below `SC_*` (0xF000) in steps of 16, because the system keeps the
  low four bits of `WM_SYSCOMMAND`'s wParam for itself. The menu is rebuilt from
  `GetSystemMenu(TRUE)` each time, with app items placed above Close.
- Windows are created cloaked (`DWMWA_CLOAK`), so there is no white flash before the first frame.

**Extended title bar and borderless.** With the title bar extended, only the caption leaves the
non-client area; the side and bottom borders stay native. The caption band left to DWM is the
largest of the OS caption (plus the top sizing band while restored), the caption buttons and
`WindowStyle::title_bar_height`. `WM_NCHITTEST` asks DWM first (`DwmDefWindowProc`, which gives
the caption buttons their hover, press and Windows 11 Snap Layouts on `HTMAXBUTTON`), then the
resize band, then the logical `Window`. The creation-time `WM_NCCALCSIZE` comes with wParam
`FALSE`, so a frame change is forced in `WM_CREATE`. A maximized overlapped window overshoots its
monitor by its sizing border, so with the non-client area collapsed the client is inset by the
frame thickness. A maximized window covering the whole work area would block an auto-hide
taskbar's reveal edge, so that edge gives up one pixel.

`TitleBar` never draws minimize, maximize or close buttons.
With content extended into the caption, DWM keeps drawing the OS buttons over it, and they keep
their UI Automation providers, high contrast colors and Snap Layouts, which a self-drawn set
would lose. `Window::caption_insets()` tells the title bar how much room they take.

**DWM attributes.** `DwmExtendFrameIntoClientArea` is decided in one function, because the last
call wins: a system backdrop needs the whole client (Mica shows wherever paint leaves
transparency), an extended title bar needs the caption band, and a borderless frame needs a
token 1 px so DWM keeps the shadow. Turning a backdrop off must also reset the margins, since
`DWMSBT_NONE` alone leaves the frame extended and any transparent pixel shows the desktop.
`DWMWA_SYSTEMBACKDROP_TYPE` exists from Windows 11 22H2; the first refusal is latched, so theme
changes do not retry it forever. DWM takes Mica's light or dark tint from
`DWMWA_USE_IMMERSIVE_DARK_MODE`, not from what the app draws, so that flag has to match the
theme. Frame colors are pushed on every theme change, with `DWMWA_COLOR_DEFAULT` for "follow the
OS", or a previous theme's caption color (a Windows 9x navy) outlives a switch. DWM rounds
nothing on a popup by default, so `Frame::None` with `round_popup_corners` asks for
`DWMWCP_ROUND`.

**Full screen** follows Raymond Chen's "How do I switch a window between normal and fullscreen?":
save the placement (restoring first, so the saved one is the windowed state), switch to
`WS_POPUP`, and drop `WS_MAXIMIZE` so the monitor rectangle becomes the normal position. A
minimize from full screen then restores to full screen for free. `WS_MINIMIZEBOX` and
`WS_SYSMENU` stay for the taskbar button, `WS_MAXIMIZEBOX` goes so Win+Up cannot maximize it,
and the taskbar is told explicitly (`MarkFullscreenWindow`). It covers the monitor the window
mostly sits on and re-fits on display changes. Leaving re-derives the windowed style from
`WindowStyle`, so a `set_style()` made meanwhile lands. Style bits gate the user, not the API:
`SW_MAXIMIZE` works without a maximize button, and `resizable = false` does not block full
screen.

**Placement.** `WindowPlacement` normal bounds are converted at the DPI of the monitor they sit on,
not the window's current one, so they can be applied without knowing where the window was.
Applying takes two passes: the system DPI finds the monitor, whose DPI then gives the real size.
Size limits are client DIPs; `WM_GETMINMAXINFO` adds the current frame back. Resizing the client
measures the frame instead of using `AdjustWindowRectEx`, which knows nothing of a caption the
content took over. The first window of the process shows with `SW_SHOWDEFAULT`, which honors a
shortcut's "Run: Maximized/Minimized".

### Taskbar and Icons

`Win32Taskbar` (`src/platform/windows/Win32Taskbar.hpp`) wraps `ITaskbarList3` for progress, the
overlay badge, the thumbnail toolbar and `MarkFullscreenWindow`. Calls about the taskbar button
fail until the shell sends the registered "TaskbarButtonCreated" message, and it sends it again
after an Explorer restart. So the class keeps the last value of everything and replays it from
`button_created()`; a setter before that only records. `SetProgressValue` also switches an
indeterminate bar back to normal, so it is only sent for states that show a value. The thumbnail
button set is fixed by the first `ThumbBarAddButtons`, which cannot be undone, and is capped at
the shell's seven. `ChangeWindowMessageFilterEx` lets an elevated process still hear
"TaskbarButtonCreated" and the thumbnail buttons' `WM_COMMAND` from the unelevated shell.

The caption icon, the overlay badge and the thumbnail buttons each need an `HICON` at a size only
the host knows. `Win32Icon.hpp` builds one from a `WindowIcon`: an RGBA image is resampled
bilinearly on premultiplied values into the square (uniform, centered); a glyph is drawn through a
Direct2D DC target at 96 DPI with grayscale antialiasing (nothing under it is opaque, so
ClearType has nothing to blend against) and then un-premultiplied. The 32 bpp color bitmap
carries the alpha, but an all-zero monochrome mask still has to exist.

## Tray

`ui::TrayIcon` (`include/winui/ui/TrayIcon.hpp`) is a notification-area icon and nothing more:
a hover text, a right-click menu of `MenuItem`s and notifications. A flyout is an ordinary
`Window` the app owns, made with the `ui::tray_flyout_options()` preset and attached through
`TrayIconOptions::flyout`: a left click (or Enter or Space on the focused icon) opens it next to
the icon, and clicking anywhere else closes it. The app runs its own loop, `ui::run()` with no
main window, and ends it with `ui::quit()`, which is an item it writes into the menu itself.

It has three pieces:

- **A hidden icon window** owns the icon and hears the shell. It is a top-level zero-size
  `WS_EX_TOOLWINDOW` popup that is never shown, so it has no taskbar button or Alt+Tab entry. It
  is top-level rather than message-only because a message-only window never receives the
  registered "TaskbarCreated" broadcast, which says Explorer restarted and every icon must be
  added again. Adding the icon also fails while there is no taskbar yet, and TaskbarCreated
  retries it. `ChangeWindowMessageFilterEx` lets an elevated app still hear it.
- **The flyout** is the app's `Window`. Its OS window is created hidden the first time it is
  needed, with `WindowOptions::close_hides` from the preset, so every control, binding, theme and
  popup works in it unchanged. The icon only keeps a pointer to it, so the window has to outlive
  the icon: declare the flyout first.
- **The loop** is the app's: `ui::run()` without a main window, so only `ui::quit()` ends it.

The icon uses `NOTIFYICON_VERSION_4`: `NIN_SELECT`, `NIN_KEYSELECT` and `WM_CONTEXTMENU` with the
anchor point in wParam and the event in `LOWORD(lParam)`, and the shell's own tooltip
(`NIF_SHOWTIP`). Two events need debouncing: Enter on the focused icon can deliver `NIN_KEYSELECT`
twice, and a click on the icon while the flyout is open first light-dismisses the flyout (it
loses activation) before `NIN_SELECT` arrives, which would reopen it. The second case is filtered
within the double-click time.

The icon follows Explorer's own taskbar light or dark setting (`SystemUsesLightTheme`, changed
through `WM_SETTINGCHANGE` "ImmersiveColorSet"), not "Apps use light theme"; the flyout follows
the app theme. It is drawn at the primary monitor's DPI, because the notification area lives on
the primary taskbar, and redrawn rather than cached, since DPI and light or dark can change.

The native tray menu needs `SetForegroundWindow` before `TrackPopupMenu`, or a click elsewhere
does not close it, and a posted `WM_NULL` after, or the next right click opens and closes it at
once (both per the `Shell_NotifyIcon` docs). It follows the flyout's theme through
`set_app_dark_mode()`.

**Placement.** Along the taskbar the flyout is centered on the icon and kept clear of both
corners; one too big for the work area sticks to the leading corner. With a bottom taskbar, or an
anchor inside the work area (the overflow flyout, or an auto-hide taskbar overlapping while
shown), it goes above the anchor. Before sizing, the hidden flyout is moved onto the icon's
monitor so a DPI change (`WM_DPICHANGED`) arrives before the content is measured, and it stays
DWM-cloaked across the show until the frame at the new size has painted.

**Look.** The flyout is a bare `Frame::None` popup with Windows 11's rounded corners and rim and
no shadow, like the shell's own flyouts, which DWM does not shadow either. `TrayFlyoutOptions::shadow`
switches it to `Borderless`, an overlapped window DWM shadows and rounds itself. Windows 11 would
otherwise give a tool window a tooltip's small corner radius, while the shell's flyouts use
8 px (`OverlayCornerRadius`); an era pack that asks for square corners keeps them. There is no
system menu, since DWM would draw a tool window's close button over the backdrop. The default
Fluent 2 background is `#242424` in dark (sampled from Quick Settings) and `#F3F3F3` in light (the
same step between `#F9F9F9` and `#EEEEEE`); themes that do not allow a system backdrop (era
packs, high contrast) use their menu surface. Light dismiss is posted from the flyout's
`WM_ACTIVATE` rather than handled inside it.

## Process-Wide OS State

Two small pieces of OS state live outside any window.

**The command line.** `ui::command_line()` (`core/CommandLine.hpp`) does not use `main()`'s
`argv`: a plain `main(int, char**)` on Windows gets its arguments in the ANSI code page, so a
folder name with an umlaut dropped on the exe arrives with a `?` unless the manifest opts into
UTF-8. The function re-reads what Windows stored (`GetCommandLineW`) and splits it like the shell
and CRT do (`CommandLineToArgvW`), which is what lets an app pair it with `winui::main` and write
`int main()`. Elsewhere it reads `/proc/self/cmdline`, which is already UTF-8.

**The regional number format.** A `DecimalFormatter` that names no separators follows the
process's regional format, like WinRT's `DecimalFormatter()` and NumberBox's regional-settings
aware formatter ("1,5" in de-DE, "1.5" in en-US). The regional format stays the invariant one
(`.`, and `,` in groups of three) until a host sets it with `set_regional_number_format()`, so a
headless run formats the same everywhere. The Win32 host reads `LOCALE_SDECIMAL`,
`LOCALE_STHOUSAND` and `LOCALE_SGROUPING` of the user locale (with `LOCALE_NOUSEROVERRIDE`, the
locale's own data) at startup and again on `WM_SETTINGCHANGE` "intl". Digits are always ASCII and
the sign is always `-`.

!!! note "Drag and Drop"
    Drag and drop (`ui/DragDrop.hpp`) needs no seam. A drag started in a window stays inside it
    and runs without OLE. Files and text dragged in from other apps reach the window through the
    Win32 host's OLE drop target, which a test replaces by calling the `WindowSeams` external-drag
    functions directly (see [Drag and Drop](input-and-focus.md#drag-and-drop)). Dragging out to
    other apps is not implemented.
