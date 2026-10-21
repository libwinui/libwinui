# Studies

The research behind specific design decisions.

Each study started as a question libwinui had to answer before it could build something, and
each ends with what the library does now. They are written to stand on their own, with primary
sources in footnotes, so they are useful even if you never touch libwinui.

## The Studies

| Study | Question | What it led to |
|-------|----------|----------------|
| [WinUI Internals](winui-internals.md) | How is WinUI 3 put together: control model, styling, layout, rendering, input, virtualization? | The retained `Element` tree and the Fluent token set |
| [WinUI Animations](winui-animations.md) | What are WinUI's animation layers, and which motion curves and durations does it use? | libwinui's animation surface and its motion vocabulary |
| [Declarative UI in C++](declarative-ui-in-cpp.md) | What does C++23 allow for a declarative UI, and should it rebuild and diff? | The `ui::` spec layer, `Property<T>` and the build-once model |
| [Win32 Theme Stacks](win32-theme-stacks.md) | What can GDI, uxtheme, GDI+ and WinForms each express, and how does Windows 11 actually draw them? | How the classic and XP-era theme packs are drawn |
| [Metro](metro.md) | What was Metro mechanically, and why can a role-based palette not express it? | The `Metro` theme variant |
| [Spell Checking](spell-checking.md) | How does the Windows Spell Checking API work behind `TextBox` and `RichEditBox`? | The `SpellChecker` seam |

The design eras themselves, with their measured values, are under [Theme Eras](../themes/index.md),
and the lineage that connects them is in [A History of Windows UI Frameworks](../history.md).
