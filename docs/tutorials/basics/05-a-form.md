# A Form

Put the first four pages together: inputs, validation, submit and reset.

The example for this page builds as `tutorial_05_a_form`.

![The sign-up form with name, email, plan, seats and terms](../../assets/screenshots/tutorial-05-a-form-light.png#only-light){ .screenshot width="520" }
![The sign-up form with name, email, plan, seats and terms](../../assets/screenshots/tutorial-05-a-form-dark.png#only-dark){ .screenshot width="520" }

The example is a sign-up form with a name, an email address, a plan, a number of seats and a terms check box. Pressing Sign up shows what is still missing under each field, a complete form signs up, and Reset puts everything back where it started.

## The Form's Model

```cpp
--8<-- "examples/tutorials/05_a_form/main.cpp:model"
```

The state of the form is a handful of properties, one per input, plus `result` for the message shown after a successful submit. Nothing about the form lives in the controls. They show the properties and write to them, and the rest of the program only ever reads the properties.

That split pays off right away: the validation below, the Sign up button and the Reset button all work on these six values and never look at a control.

### What Is Wrong, Field by Field

```cpp
--8<-- "examples/tutorials/05_a_form/main.cpp:validation"
```

Each field has a check of its own, an ordinary function that takes the value and returns what is wrong with it, or an empty string. The rules are plain `if` statements that are easy to read and to test on their own.

A field shows its error through `.error`: non-empty draws the border in the critical color and puts the text under the field, in place of its description, and screen readers report the field as invalid. The field shows exactly what it is given, so when the error appears is decided here, in the data. A form that shows every error while the user is still typing the first field is unpleasant, so these errors wait for `tried`, which Sign up sets:

1. `name_error` is a `ui::Computed` of `tried` and `name`, as [State and Bindings](03-state-and-bindings.md) introduced. While `tried` is false it is empty; after that it is whatever `name_problem()` says.
2. Once an error shows, it follows the value as the user types: the error under Email goes away as soon as the address has an `@`.

Thanks to the equality gate, recomputing is cheap. Typing a second letter into the name field computes the same empty string again, and nothing downstream is notified.

## Inputs

```cpp
--8<-- "examples/tutorials/05_a_form/main.cpp:inputs"
```

Each input follows the pattern from [State and Bindings](03-state-and-bindings.md): the property written into its value field is shown and gets back what the user enters.

| Control | Spec | Value, two-way when given a property | Change handler, if you need one |
| --- | --- | --- | --- |
| Text box | `ui::TextBox` | `.text` | `on_text_changed`, on every edit |
| Pick one of a fixed list | `ui::ComboBox` | `.selected_index` | `on_selection_changed` |
| Number | `ui::NumberBox` | `.value` | `on_value_changed`, when the user commits an edit or steps the value |
| Check box | `ui::CheckBox` | `.checked` | `on_checked_changed` |

Some details:

- `.header` is the label above the control. It saves you a `ui::Text` per input. `.error` is the text under it while something is wrong.
- The email box turns off `.spell_check`, since an address is not prose and would be full of squiggles otherwise.
- `ui::ComboBox` is a closed list: the user picks one of `.items`, and typing the first letters of an item selects it. When the user may also type a value that is not in the list, set `.editable = true` and read the value from `.text`. Typed text reaches `.on_text_submitted` on Enter and also when the box loses focus. Do not open a modal dialog from that handler: a commit caused by focus loss would re-enter the focus change that raised it. Show the problem inline instead, as this form does with its info bar.
- `.selected_index` is a position. To keep a value in the property instead (an enum, a string), bind it with `ui::bind_choice(size, {Size::Small, Size::Medium, Size::Large})`, listing the values in the order of `.items`. The combo box selects the position of the value `size` holds, and a pick writes the value at that position. This works on every index field: `ui::ComboBox`, `ui::RadioButtons`, `ui::ListView`, `ui::SelectorBar`, `ui::Sections` and the rest.
- A `ui::NumberBox` commits typed text on Enter or when it loses focus. Then it parses the text, clamps it to `[.min, .max]` and reports the result. The arrow keys, the mouse wheel and the spin buttons step by `.step`, 1 by default. `NumberBoxSpinButtonPlacementMode::Inline` shows the buttons next to the box.
- `ui::CheckBox` can show a third, indeterminate state, so `.checked` is a `std::optional<bool>` with `std::nullopt` for it. A click only ever gives `true` or `false`, so a `ui::Property<bool>` binds two-way, as `agreed` does here. Bind a `ui::Property<std::optional<bool>>` when the box can show the third state, and add `.three_state = true` when a click can reach it.

## InfoBar

```cpp
--8<-- "examples/tutorials/05_a_form/main.cpp:infobars"
```

An `InfoBar` is an inline status banner: a title, a message, an icon and a background color picked by its `.severity`. A closed bar takes no space, so it can sit in the layout permanently and only show up when it has something to say.

Both bars are driven entirely by state:

- The warning bar shows `terms_error`, the one problem that has no text field to sit under, and is open while it is not empty. It has no close button (`.closable = false`), because closing it would not fix the form.
- The success bar shows `result` and is open while `result` is not empty. When the user closes it, `on_closed` clears `result`, so the state agrees with the screen again.

There is no code that opens or closes a bar. Tick the box and `terms_error` becomes empty, the binding turns that into `is_open = false`, and the warning disappears.

## Submit and Reset as Commands

```cpp
--8<-- "examples/tutorials/05_a_form/main.cpp:commands"
```

Sign up is always enabled. It sets `tried`, which makes every field show what is wrong with it, and then runs the same checks to decide whether the form is complete. Only a complete form produces a result. The same command could be put on a menu item or a keyboard shortcut later without writing the checks twice.

Reset writes every property back to its starting value, `tried` included, so the errors disappear with the values. Every input is bound, the two text boxes included, so they all follow, without a line of code that touches a control.

## Tab Order and Focus

```cpp
--8<-- "examples/tutorials/05_a_form/main.cpp:tree"
```

The window builds its Tab order from the tree: every control that can take focus, in the order it appears. For a form that is written top to bottom, that is already the right order: Name, Email, Plan, Seats, the check box, Sign up and Reset. Disabled controls are skipped.

When the window opens, focus is on the first of them, here the Name box, so the user can start typing without reaching for the mouse.

The layout is a single `ui::VerticalStack` with `.max_width = 420`. On a wide window the form stops growing at 420 DIPs and is centered, as [Layout](02-layout.md) described for a stretched control that reaches its maximum.

## Next

[Components](06-components.md): split a UI into functions that return descriptions.
