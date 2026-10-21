# Built-in Text

How to show the text libwinui writes itself in your app's language.

## What libwinui Writes

Some text comes from libwinui rather than from your code:

- the labels and tooltips of the standard commands (`ui::standard_command()`)
- the Cut, Copy, Paste and spelling items of the text controls' context menus
- key names in shortcut text, such as "Ctrl+S"
- labels inside controls: a ToggleSwitch's On and Off, a ColorPicker's channel names, a Pager's "Page" and "of"
- the names screen readers read for built-in buttons: the caption buttons, a Pager's arrows, the close button of an InfoBar
- the date and time controls' labels: the calendar's Today footer, the picker's Accept and Dismiss buttons, the names of the day, month and year columns

Month and day names, AM and PM, the first day of the week and the order of a date's parts are not in `ui::Strings`. They come from the user's Region settings through [`ui::regional_date_format()`](../../reference/core/namespace-ui.md#regional_date_format), and the date controls update when the user changes them. [`ui::set_regional_date_format()`](../../reference/core/namespace-ui.md#set_regional_date_format) replaces them with a [`ui::DateFormatSymbols`](../../reference/core/date-format-symbols.md) of your own. Call it after the first window opens, since opening it reads the Region settings, and keep in mind that a later change to the Region settings replaces yours again.

All of it lives in one `ui::Strings` value, English by default. `ui::strings()` returns the current set.

## Replace It

Copy the current set, change the fields you translate and hand it back with `ui::set_strings()`. Fields you leave alone keep their English text.

```cpp
ui::Strings german = ui::strings();
german.commands.copy       = {"Kopieren", "Die Auswahl in die Zwischenablage kopieren"};
german.commands.paste      = {"Einfügen", "Den Inhalt der Zwischenablage einfügen"};
german.keys.ctrl           = "Strg";
german.toggle_switch.on    = "Ein";
german.toggle_switch.off   = "Aus";
ui::set_strings(std::move(german));
```

Call it before the first window opens, or at any time later. Open windows lay out and draw again, and every control shows the new text at once, so an app can switch language while it runs.

The fields are grouped by control: `commands`, `keys`, `common`, `window_frame`, `text_menu`, `color_picker`, `toggle_switch`, `pager`, `tabs`, `navigation_shell`, `command_bar`, `ink_toolbar`, `person_picture`, `file_picker` and `date_time`. The [Strings reference](../../reference/core/strings.md) lists every field with its English text.

## Patterns and Counts

A field with `{0}` or `{1}` in it is a pattern. The control puts a value in each slot, and a translation can move the slots to where its language needs them. `{{` and `}}` write a literal brace.

```cpp
german.pager.page_number   = "Seite {0}";        // a page button's name
german.common.with_shortcut = "{0} ({1})";        // "Speichern (Strg+S)"
german.color_picker.percent = "{0} %";
```

A field whose wording depends on a number is a function of that number, so each language applies its own plural rules:

```cpp
german.person_picture.badge = [](std::size_t n) {
    return n == 1 ? std::string{"1 Element"} : std::format("{} Elemente", n);
};
```

## A Control's Own Text Wins

Text you give a control directly is used instead of the shared set. A ToggleSwitch with `.checked_text` shows that text, and a Pager with `.prefix_text` shows its own prefix. Leave the field unset to follow `ui::strings()`.

```cpp
ui::ToggleSwitch{.checked_text = "Connected", .unchecked_text = "Offline"}
```

## Text You Cache

Controls follow `ui::set_strings()` on their own. Code that copies text out of `ui::strings()` and keeps it can subscribe to `ui::strings_changed()` and read it again:

```cpp
ui::Subscription relabel = ui::strings_changed().observe([&] { status = ui::strings().commands.save.text; });
```

## What Stays Outside

- Message box buttons such as OK and Cancel come from Windows in the user's language.
- Number separators follow the user's region through `ui::regional_number_format()`.
- Your own text is yours to translate. A `ui::Property<std::string>` bound to a label switches it along with the built-in text.
