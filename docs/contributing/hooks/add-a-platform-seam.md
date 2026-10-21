# Add a Platform Seam

An OS service behind an interface, testable without the OS.

A control that needs something from the operating system (the clipboard, a spell checker, a native context menu, a URI launcher, text measurement, the user's theme) never calls Windows directly. It talks to a small interface. The Win32 host installs the real implementation, and tests install a double. That keeps the control testable on any platform, and it is also where a future non-Windows backend plugs in. [Platform Seams](../how-it-works/platform-seams.md) describes the existing ones; this page walks the files for a new one, using the clipboard as the example.

| Step | File |
| --- | --- |
| 1. Interface | `include/winui/ui/Clipboard.hpp` |
| 2. Test double | `include/winui/testing/TestClipboard.hpp` |
| 3. Win32 implementation | `src/platform/windows/Win32Clipboard.hpp`, `src/platform/windows/Win32Clipboard.cpp`, `LIBWINUI_WIN32_SOURCES` |
| 4. The window's slot | `include/winui/ui/Window.hpp`, `src/ui/Window.cpp` |
| 5. Reaching controls | `include/winui/ui/Element.hpp`, the control that uses it |
| 6. The Win32 host | `src/platform/windows/Win32WindowHost.cpp` |
| 7. Tests | `tests/test_clipboard.cpp`, the control's tests |

The seams that exist today:

| Interface | Test double | Win32 implementation |
| --- | --- | --- |
| `TextMeasurer` (`render/Text.hpp`) | `HeadlessTextMeasurer` | DirectWrite measurer (private) |
| `Clipboard` (`ui/Clipboard.hpp`) | `TestClipboard` | `win32::Win32Clipboard` |
| `SpellChecker` (`render/SpellChecker.hpp`) | `TestSpellChecker` | `win32::Win32SpellChecker` |
| `ContextMenuHost` (`render/ContextMenuHost.hpp`) | `TestContextMenuHost` | `win32::Win32ContextMenuHost` |
| `Launcher` (`ui/Launcher.hpp`) | `TestLauncher` | WinRT launcher (private) |
| `MessageBoxHost` (`ui/MessageBox.hpp`) | `TestMessageBoxHost` | Win32 message box (private) |
| `Notifier` (`ui/Notification.hpp`) | `TestNotifier` | Win32 notifier (private) |
| `JumpListHost` (`ui/JumpList.hpp`) | `TestJumpListHost` | Win32 jump list (private) |
| `PrintHost` (`ui/Printing.hpp`) | `TestPrintHost` | Win32 printing (private) |
| `NativeHost` (`render/NativeHost.hpp`) | `TestNativeHost` | `win32::Win32NativeHost` |
| `Compositor` (`src/render/Compositor.hpp`, private) | `TestCompositor` (`src/testing/`) | DirectComposition (private) |
| `TextAnalyzer` (`render/TextAnalyzer.hpp`) | the base class itself | `win32::Win32TextAnalyzer` |

## The Interface

The interface goes in `include/winui/render/<Seam>.hpp` for a seam a window uses, or in
`include/winui/ui/<Seam>.hpp` for a process-wide one such as `Clipboard`. It is an abstract class with a virtual destructor, not copyable, and it uses the library's own types only: UTF-8 `std::string`, `Bitmap`, `Point`, `std::filesystem::path`. No `HWND`, no `HRESULT`, no `wchar_t`.

```cpp
/// The system clipboard, as a control or an app sees it.
class Clipboard
{
public:
    Clipboard()                            = default;
    Clipboard(const Clipboard&)            = delete;
    Clipboard& operator=(const Clipboard&) = delete;
    virtual ~Clipboard()                   = default;

    /// Replaces the clipboard's content with `text`.
    virtual void set_text(const std::string& text) = 0;
    /// The clipboard's text, or nullopt when it holds none.
    [[nodiscard]] virtual std::optional<std::string> text() const = 0;
    // ...
};
```

Keep it small. Model what controls and apps actually use, and write down what is left out in the class comment (the clipboard has text, bitmaps and file paths, and no HTML, RTF or delayed rendering).

Keep it synchronous where you can. A control calls the seam from inside input handling or rendering, on the UI thread, and a synchronous call keeps the test simple: call, then assert. When the OS only offers an asynchronous API, keep the interface fire-and-forget and bring results back on the UI thread. For a change the app may want to watch, expose a `Property` from the interface, as `Clipboard::content_changed()` does with a change counter; the host then only has to bump it.

## Test Double

The test double goes in `include/winui/testing/Test<Seam>.hpp`, header-only and `final`. It keeps its state in plain members a test can seed and read, and it answers the way the real implementation would for the same input:

```cpp
class TestClipboard final : public Clipboard
{
public:
    void set_text(const std::string& text) override
    {
        ClipboardContent content;
        content.text = text;
        set_content(content);
    }
    [[nodiscard]] std::optional<std::string> text() const override
    {
        return content_.text;
    }
    // ...
private:
    ClipboardContent content_;
};
```

It lives under `include/`, not `tests/`, because applications test with it too.

## Win32 Implementation

The real implementation is `win32::Win32<Seam>` in namespace `ui::win32`, with its `.cpp` in `src/platform/windows/` and listed in `LIBWINUI_WIN32_SOURCES` in `CMakeLists.txt`. If it needs a system library, add it to `target_link_libraries` in `winui_configure_target()`.

The header goes next to the source, in `src/platform/windows/`: the backend is private and not installed. The seam's interface and its test double are the public parts (`include/winui/render/<Seam>.hpp`, or `ui/` for a process-wide one, and `Test<Seam>.hpp`, the latter in namespace `ui::testing`).

Convert at the boundary. UTF-8 in, UTF-16 to the Win32 call, UTF-8 back out; COM pointers in `Microsoft::WRL::ComPtr`; failures reported through the interface's own return values, never as exceptions escaping into a control.

## Wiring Into the Host

**Per window or per process.** A service the OS has once per session, like the clipboard or the shell that opens links, belongs to the process: a free function returns it (`ui::clipboard()`), the backend makes it on first use (`detail::platform_clipboard()` in `src/platform/Backend.hpp`, answered by the Win32 backend and by `NoBackend.cpp`), and a test swaps it with a free setter in the testing tier (`ui::testing::set_clipboard()`). Everything else belongs to a window.

**The window's slot.** `Window` holds a non-owning pointer per window-level seam, with a setter the host and the tests reach through `ui::testing::seams(window)`. Null is valid and means the feature does nothing, which is what a headless window without a double gets:

```cpp
/// Optional and non-owning: the host that pops up real context menus. Left null,
/// right-click and Shift+F10 do nothing.
void set_context_menu_host(ContextMenuHost* host)
{
    context_menu_host_ = host;
}
```

**Reaching controls.** Pick the narrowest of the shapes that exist:

- *Called where it is needed.* A process-wide service needs no delivery: `TextBox` calls `ui::clipboard()` on Ctrl+C, and a hyperlink calls `ui::launch_uri()`.
- *Handed to the focused element.* `Element::sync_spell_checker(SpellChecker&)` is a no-op virtual. `Window` calls it on the focused element when it gains focus and before delivering a key, and `TextBox` overrides it to keep the pointer.
- *Handed in for one call.* `Element::handle_context_menu(position, ContextMenuHost& host)` gets the host as a parameter that is only valid during the call.
- *Walked over the tree.* `sync_composition()` and `sync_native()` visit every element once per frame, for seams that every instance of a control needs (overlay visuals, hosted native windows).

**The Win32 host.** `src/platform/windows/Win32WindowHost.cpp` keeps one state struct per window. Add a window-level implementation there, as a plain value member if it needs nothing, or a `std::unique_ptr` created in `WM_CREATE` if it needs the `HWND`, and install it on the window:

```cpp
// In the per-window state:
std::unique_ptr<Win32ContextMenuHost> context_menu;  // needs the HWND

// In WM_CREATE:
s->context_menu = std::make_unique<Win32ContextMenuHost>(hwnd, s->dpi);
ui::testing::seams(*s->window).set_context_menu_host(s->context_menu.get());
```

If the app should be able to configure or switch off the service, add an option to `ui::WindowOptions` (the spell checker has `WindowOptions::spell_check`).

## Tests

- **The double's contract**, so tests built on it can trust it: `tests/test_clipboard.cpp` checks that `TestClipboard` holds one content at a time, reports the formats the Win32 clipboard would, and counts changes.
- **The control through the double.** Install the double and drive the control. Declare the double before the window, so the window is destroyed first. A process-wide double uninstalls itself when it is destroyed:

    ```cpp
    struct LinkWindow
    {
        HeadlessTextMeasurer measurer;
        TestLauncher         launcher;
        Window               window;

        LinkWindow()
        {
            ui::testing::set_launcher(&launcher);
        }
    };
    ```

- **The Win32 implementation**, where it can be tested without side effects on the user's machine, inside `#ifdef _WIN32`. `tests/test_clipboard.cpp` round-trips a picture through `Win32Clipboard`'s bitmap encoding without touching the real clipboard. What cannot be tested that way (real menus, real OS dialogs) gets a gallery page instead, and a manual check.
