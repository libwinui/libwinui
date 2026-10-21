# Extending the Tree

Your own elements, native controls, and hand-built subtrees.

Sooner or later an app needs something the library does not ship: a chart, a color swatch, a canvas with custom hit testing, or an OS control that would be silly to redraw. This track shows the three ways in, from the most to the least integrated:

- write an `Element` yourself, which then lays out, draws, takes focus and is tested exactly like a built-in control;
- host a real Win32 child window through `NativeControl`, for what the OS already does well;
- build a subtree with the imperative API and drop it into a declared tree, which is also how the first two end up on screen.

Read the Basics course up to [Components](../basics/06-components.md) first; the pages assume you know specs, `.control` and `ui::Spec`.

## The Track

| Page | What you build | Project |
| --- | --- | --- |
| [01 - Your Own Element](01-your-own-element.md) | A clickable, focusable color swatch, with its own spec so it sits in a declared tree like any built-in control | `examples/tutorials/11_your_own_element/` |
| [02 - Hosting Native Controls](02-hosting-native-controls.md) | A form with a Win32 `EDIT`, a `RICHEDIT50W` with bold and italic buttons, and a bare window class created by name | `examples/tutorials/12_hosting_native_controls/` |
| [03 - Mixing Imperative and Declarative](03-mixing-imperative-and-declarative.md) | A tagging panel whose buttons are built in a loop and whose history grows at runtime | `examples/tutorials/13_mixing_imperative_declarative/` |

The pages stand alone, but 03 is the shortest and explains the mechanism the other two rely on, so it is a fine place to start.
