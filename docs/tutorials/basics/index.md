# Basics

Seven short steps from an empty project to a small working form, and how to keep a window responsive.

## Who This Is For

You know C++ well enough to be comfortable with lambdas, `std::unique_ptr` and designated initializers, and you want to write a Windows desktop UI in it. You do not need to know the Win32 API.

## The Course

Each page builds one small app that runs on its own, and each builds on the ones before it.

| Page | You learn |
| --- | --- |
| [01 - Your First Window](01-first-window.md) | Add the library to a CMake project, describe a window's content, run the message loop |
| [02 - Layout](02-layout.md) | Stacks and spacers, margins and alignment, minimum and maximum sizes, grids, a canvas for exact positions, scrolling |
| [03 - State and Bindings](03-state-and-bindings.md) | `Property<T>`, `bind()`, the equality gate, and why nothing ever dangles |
| [04 - Events and Commands](04-events-and-commands.md) | `on_*` handlers, commands shared by several triggers, enablement, keyboard shortcuts |
| [05 - A Form](05-a-form.md) | The above put together: inputs, a derived validation message, submit and reset |
| [06 - Components](06-components.md) | Functions that return `ui::Spec`, the capture rules, `.control`, building rows in a loop |
| [07 - Background Work and the UI Thread](07-background-work.md) | Coroutines and `post()` for slow work, what happens when the window closes first, the debug thread checks |

Read them in order. 01 to 03 are the core of the API: after those you know how every other control is used, since they all follow the same pattern.

## What You End Up With

A sign-up form with text boxes, a combo box, a number box and a check box. Once you press Sign up it shows what is still missing under each field and in an info bar, and it resets to its starting state with one command. All of it is derived from a handful of properties written down once, with no code that copies values into controls by hand.

On the way you pick up the handful of rules the whole API follows:

- A `ui::` struct such as `ui::Button` describes a control. `ui::ButtonControl` is the control it builds.
- Fields are written by name, in declaration order, with children always last.
- Every node in the tree has exactly one owner.
- State lives in properties, and controls are bound to it.
- A handler in a component captures by value, or captures state that outlives the window.
- Only the UI thread touches the tree and its properties. Workers hand their results back with `post()` or `co_await ui::on_ui_thread()`.
