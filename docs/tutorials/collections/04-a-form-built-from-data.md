# A Form Built From Data

A form whose questions are data. You add, edit and reorder them, and the preview and the answers table follow.

The example for this page builds as `tutorial_16_form_from_data`.

![The question list, the editor of one question, the form preview and the answers table](../../assets/screenshots/tutorial-16-form-from-data-light.png#only-light){ .screenshot width="1100" }
![The question list, the editor of one question, the form preview and the answers table](../../assets/screenshots/tutorial-16-form-from-data-dark.png#only-dark){ .screenshot width="1100" }

In the [todo list](03-a-todo-list.md) every row has the same shape. Here the data decides the shape. A question's type decides which input the preview shows, another question's answer decides whether it shows at all, and the questions are the answer table's columns. The full source is `examples/tutorials/16_form_from_data/main.cpp`.

The window has four parts: the list of questions on the left, the selected question's editor next to it, the form as someone filling it in sees it, and the answers sent so far at the bottom.

## The Model

```cpp
--8<-- "examples/tutorials/16_form_from_data/main.cpp:model"
```

A question is a struct of Properties in a `ui::ObservableVector`, like a task in the todo list. It holds what the editor changes (its type, its question, its choices and the question it follows) and the answer the preview collects. The editor and the preview bind to the same Properties, so neither copies anything to the other: typing a question in the editor changes the preview's label as you type.

The answer has one Property per kind of input. A question whose type changes keeps what was entered, and gets it back if the type changes back.

## The Form

```cpp
--8<-- "examples/tutorials/16_form_from_data/main.cpp:form"
```

`refresh()` makes the lists that name the questions: the choices of the "Show" pickers and the answer table's columns. Each question subscribes to what those lists depend on with one `ui::observe()` over several Properties. The subscriptions live in the question, so a question removed from the collection takes them along.

`update_shown()` decides which questions show. A question can follow one above it, and shows once that one shows and has an answer. One pass from the top settles them all.

## The Editor

```cpp
--8<-- "examples/tutorials/16_form_from_data/main.cpp:editor"
```

The type picker is a ComboBox, whose `.selected_index` is an index, while the model holds a `Kind`. `ui::bind(q.kind, to, from)` is two-way through a pair of conversions: `to` turns the property's value into the field's, and `from` turns what the user picks back into a `Kind`. Use it wherever a control's type isn't the model's.

The "Show" picker stores the id of the question to follow, picked from `form.after_ids`, a list that changes as questions come and go. `ui::bind_choice(q.after, form.after_ids)` shows the position of the id in the list as it is now, so dragging the questions into another order moves the selection with them.

The editor is a `ui::Match` on the selected question's id, rebuilt each time another question is selected and at no other time. Typing in it doesn't rebuild it. The content looks up its question from the id it was given. That matters because a Match's content runs inside the write that changed its value, while other state may still be halfway through changing, so the value is the one thing it can rely on.

None of the fields needs a handler, since each two-way binding writes the user's change into the question. A handler, where you add one, reports the changes the user made and calls on the control. A write to the bound Property doesn't run it, so filling the editor from the model never looks like an edit (see [Events and Commands](../basics/04-events-and-commands.md)).

## The Preview

```cpp
--8<-- "examples/tutorials/16_form_from_data/main.cpp:preview"
```

Each question is a `ui::Match` on its type. Changing the type in the editor rebuilds that one input, and the answer survives because it lives in the question. `.visible = ui::bind(q.shown)` hides a question that doesn't apply without destroying its input.

The question goes into the input's `.header`. An input uses its header as its name for screen readers, so the name follows the question as it changes.

The choices are an `ObservableVector<Choice>` whose texts are Properties, and a ComboBox wants a `std::vector<std::string>`. `ui::bind_each(q.choices, &Choice::text)` is that list. It follows the collection and each choice's text, so adding a choice or retyping one shows at once.

## The Answers Table

```cpp
--8<-- "examples/tutorials/16_form_from_data/main.cpp:answers"
```

A `ui::TableView`'s columns are given once, when it is built, so a table whose columns come from data is rebuilt when they change. The Match keys on the columns themselves, a `std::vector<Column>` that compares equal while no question was added, removed, moved or renamed. Answering, sending or changing a question's type rebuilds nothing. Key a Match on what its content is built from, and it rebuilds exactly when the content would differ.

## Putting It Together

```cpp
--8<-- "examples/tutorials/16_form_from_data/main.cpp:main"
```

## The Tools on This Page

| You need | Use |
| --- | --- |
| A control whose type isn't the model's | `ui::bind(prop, to, from)` |
| An index into a list that changes | `ui::bind_choice(prop, list)` |
| One Property of each item, as a list | `ui::bind_each(items, &T::member)` |
| A part whose shape comes from data | `ui::Match` keyed on that data, built from its value |
| A part that comes and goes with an answer | `.visible = ui::bind(...)` |
