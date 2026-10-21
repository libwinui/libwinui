# Conventions

The rules the language or the ownership model enforces. They hold for every spec and every control, so each reference page can leave them out.

## Spec Field Order

Every spec declares its fields in the same order of blocks:

1. **Properties**: what the control shows and holds (`.text`, `.value`, `.severity`). They come in a fixed order. First the texts that label the control (`.header`, `.title`, a button's `.text`, `.description`, `.placeholder_text`), then its main value (`.value`, `.checked`, `.items`, `.text`, `.selected_index`, `.is_open`), then what limits it (`.min`, `.max`, `.step`, `.max_length`), then the other properties, and the appearance properties last (`.role`, `.variant`, colors, fonts, `.padding`).
2. **Appearance and layout**: `.style` and the common fields below, then `.tool_tip`, then `.enabled` on a control that takes input, then `.accessibility`.
3. **Behavior**: `.command`, `.command_parameter`, then the `.on_*` handlers, the control's main event first and the generic pointer, focus and key events last.
4. **`.control`**: the `ui::Control<...>` handle that receives the built control.
5. **Children**: `.content` and the other child fields.
6. **Templates**: `.item`, `.item_template`, and the `.content` function of `ui::Match`, `ui::Lazy` and `ui::State`, always last.

C++ requires designated initializers in declaration order, so a call site has to follow this order too. Write `.text` before `.on_click` and `.spacing` before `.content`, or the code does not compile:

```cpp
ui::Button{.text = "Save", .on_click = [&] { save(); }}  // compiles
ui::Button{.on_click = [&] { save(); }, .text = "Save"}  // error: out of declaration order
```

Leave out any field you do not need. Each spec's reference page opens with every field in declaration order, common ones included, so that list is also the order to write them in.

New fields are appended or inserted into their block; existing fields are never reordered. Code that compiles against one version keeps compiling when fields are added.

## Common Fields on Every Spec

Every spec has the same fields in its appearance and layout block, and each spec declares them itself. An aggregate with a base class has to be initialized positionally (`Button{{.margin = 4}, ...}`), which would lose the field names at the call site.

| Field | Type | Default | Meaning |
| --- | --- | --- | --- |
| `style` | `ui::Style` | `{}` | A named style. Any field it sets fills in a layout field below that was left unset. |
| `margin` | `ui::Binding<std::optional<ui::Thickness>>` | unset | Outer margin, in DIPs. Unset is no margin. |
| `width` | `ui::Binding<std::optional<float>>` | unset | Fixed width, in DIPs. Unset is Auto. |
| `height` | `ui::Binding<std::optional<float>>` | unset | Fixed height, in DIPs. Unset is Auto. |
| `min_width` | `ui::Binding<std::optional<float>>` | unset | Minimum width, in DIPs. Unset is none. |
| `min_height` | `ui::Binding<std::optional<float>>` | unset | Minimum height, in DIPs. Unset is none. |
| `max_width` | `ui::Binding<std::optional<float>>` | unset | Maximum width, in DIPs. Unset is no limit. |
| `max_height` | `ui::Binding<std::optional<float>>` | unset | Maximum height, in DIPs. Unset is no limit. |
| `horizontal_alignment` | `ui::Binding<std::optional<ui::Alignment>>` | unset | Horizontal alignment within the slot the parent gives it. Unset is `Stretch`. |
| `vertical_alignment` | `ui::Binding<std::optional<ui::Alignment>>` | unset | Vertical alignment within that slot. Unset is `Stretch`. |
| `visible` | `ui::Binding<bool>` | `true` | `false` collapses the control: no room in layout, not drawn, no input, no tab stop. Nothing is destroyed, so it keeps its state. |
| `tool_tip` | `ui::Binding<std::string>` | `""` | Shown when the pointer rests on the control. Empty leaves the control's own tooltip, such as a command's description. |
| `enabled` | `ui::Binding<bool>` | `true` | Only on controls that take input, always right after `tool_tip`. `false` grays the control out and takes no input. |
| `accessibility` | `ui::Accessibility` | `{}` | Name, description and test id for UI Automation. |

A set value means what it says: `.width = 0.0f` is zero wide, and only an unset width is Auto. A bound `std::nullopt` puts the control back to its own default.

A layout field that is set on the spec wins over `.style`, and `.style` fills in the ones left unset. So `.style = ui::styles::Spaced` alone gives an 8 DIP margin, and `.style = ui::styles::Spaced, .margin = ui::Thickness{16.0f}` gives 16. A bound field always wins. Styles compose with `|` at compile time, the right-hand side winning where both set a field:

```cpp
inline constexpr ui::Style note = ui::styles::Compact | ui::styles::Leading;

ui::Text{.text = "Saved 2 minutes ago", .style = note}
```

A style holds only the margin, the six sizes and the two alignments. Typography goes through a `ui::Text`'s `.role` or `.text_style`.

A spec you write yourself gets the same vocabulary by declaring the same fields and calling `ui::apply_common(*this, *node)` in its `build()`.

## Values and Bindings

A property field is usually a `ui::Binding<T>`. It takes a plain value, or a `ui::Property<T>` written as it is, and the source says which:

```cpp
ui::Property<std::string> title{"Untitled"};

ui::Text{.text = "Static label"}   // a value: set once
ui::Text{.text = title}            // a property: follows every change to title
ui::Text{.text = ui::bind(title, [](const std::string& t) { return t + " - Editor"; })}
```

`ui::bind()` is only needed to convert: a function from the property's value to the field's, or several properties into one value. `title.get()` is the explicit snapshot, a value read once.

On a field the user edits (a text box's `.text`, a slider's `.value`, a toggle's `.checked`, a list's `.selected_index`, an `.is_open`), a property is two-way: the control shows it, and what the user enters is written back into it before the field's own `on_*_changed` handler runs. On a display field it is one-way, as there is nothing to write.

```cpp
ui::Property<float> volume{50.0f};

ui::Slider{.value = volume}                    // dragging changes volume
ui::Slider{.value = ui::bind_one_way(volume)}  // the slider only shows it
```

A binding with a conversion (`ui::bind(prop, fn)`), a `const` property and a `ui::Computed` are one-way, and `ui::bind_one_way(prop)` opts an editable field out, for an input whose edits the app checks before storing. `Property::set()` does nothing when the value is unchanged, so the round trip stops after one round.

A spec's `on_*_changed` handler reports what the user changed. A value written into the bound property updates the control, and a two-way property, without running it, and so does a change that value causes, such as new items clearing a selection.

Three helpers cover the fields whose type isn't the model's:

| You have | Field | Use |
| --- | --- | --- |
| A value of another type, both ways | any editable field | `ui::bind(prop, to, from)`: `to` turns the property's value into the field's, `from` turns the user's back |
| One of several values | an index field (`.selected_index`) | `ui::bind_choice(prop, {a, b, c})`, or `ui::bind_choice(prop, list)` for a `Property<std::vector<T>>` list that changes |
| A collection of items with a Property each | a list field (`.items`) | `ui::bind_each(collection, &Item::member)`, one-way, following the collection and each item's Property |

## Collections and Layouts

There are two kinds of collection spec. A **repeater** turns a collection into children and nothing else: `ui::ForEach`. A **view** is a finished control around one, with scrolling, selection and keyboard navigation: `ui::ListView`, `ui::TileView`, `ui::TreeView`, `ui::TableView`. If you want a control, use a view; if you want to generate children, use `ui::ForEach`.

```cpp
ui::Property<std::vector<Photo>> photos{load_photos()};

ui::ForEach<Photo>{
    .items  = photos,                                                            // the collection
    .layout = ui::TileLayout{.min_item_width = 160.0f},                          // how they are arranged
    .item   = [](const Photo& p) -> ui::Spec { return ui::Text{.text = p.title}; },  // how one looks
}
```

A collection spec names its item type, `ui::ForEach<Photo>`, `ui::ListView<Contact>`: C++ works out a spec's type only from the field's own type, which a Property is not. `ui::ForEach` builds every item and keeps each row's state by default. `.virtualize = true` is the opt-in for a long collection inside a `ui::Scroll`: only the rows in and near view exist, so state then belongs in the item.

`.items` takes three sources, which differ in how a change reaches the rows:

| Source | A change | `.item` receives |
| --- | --- | --- |
| a `ui::ObservableVector<T>` | One row built per insert, one destroyed per removal, a moved item keeps its row. | `T&`, valid while the item is in the collection |
| a `ui::Property<std::vector<T>>` with `.key` | Rows follow their keys through any reorder; a row is rebuilt when its item compares unequal. | `const T&` to a copy, not to be kept |
| the same without `.key` | The rows before and after the edit are kept, those in between rebuilt. | `const T&` to a copy, not to be kept |
| a plain `std::vector<T>` | None, it is copied once. | `const T&` to a copy, not to be kept |

The views take `.items` from the same three sources, and their handlers receive the item:

| View | `.key` | Handlers that receive the item |
| --- | --- | --- |
| `ui::ListView`, `ui::TileView` | yes | `on_item_click(const T&)`, `on_item_invoked(const T&)`, `on_selection_changed(std::optional<T>)`, a copy, unset when nothing is selected |
| `ui::TableView` | yes | `on_selection_changed(std::optional<T>)` |
| `ui::TreeView` | yes, a string or number | `on_item_click(const T&)`, `on_item_invoked(const T&)`, `on_expanding(const T&)`, `on_selection_changed(std::optional<T>)` |

A `const T&` is into the view's copy of the items, or into the `ui::ObservableVector`, so do not keep it past the handler; the `std::optional<T>` a selection handler gets is a copy, safe to keep. `ui::TreeView<T>` takes its top-level items in `.items` and finds their children through `.children`; `ui::TreeNode` is the ready-made item. In a `ui::ListView` or `ui::TileView`, a drag reorder is written back into the bound collection. A list of strings needs no `.item`: `ui::ListView<std::string>{.items = {"Inbox", "Sent"}}`. The controls stay index-based: through `.control`, an item is its position.

## Structure That Follows State

Nothing in libwinui compares two trees. Structure that depends on state is written with one of these, and each says in its name whether it keeps what the user did inside the subtree:

| Situation | Tool | Keeps state |
| --- | --- | --- |
| Decided once, while the tree is built | `ui::when(condition, spec)`, or an empty `ui::Spec{}` | n/a |
| Show or hide, cheap subtree | `.visible = shown` (a `ui::Property<bool>`) | yes |
| Show or hide, expensive subtree | `ui::Lazy{.visible, .content}` | yes (`.unload = true`: no) |
| One of several, state should reset | `ui::Match{.value, .content}` | no |
| One of several, state should persist | `ui::Match{.keep = true}` | yes |
| A list you edit in place | `ui::ForEach` over a `ui::ObservableVector` | yes, per item |
| A list you assign as a whole | `ui::ForEach{.items, .key}` | yes, per key |
| A long list | `ui::ForEach{.virtualize = true}` or a view | per realized row |
| State that belongs to one component | `ui::State<S>{.initial, .content}` | as long as the component |

A condition over several properties is `ui::bind(fn, a, b, ...)`: the function first, then the properties it reads, computed again when any of them changes.

A layout is a plain value handed to a `.layout` field, with no `make_unique` and no setters. A name ending in `Layout` is never a control.

| Arrangement | Layout control (hand-written children) | Layout value (generated children) | View |
| --- | --- | --- | --- |
| a line | `ui::VerticalStack`, `ui::HorizontalStack` | `ui::StackLayout` | `ui::ListView` |
| a wrapping line | `ui::WrapPanel` | `ui::WrapLayout` | |
| wrapping cells | `ui::TilePanel` (children may span) | `ui::TileLayout` | `ui::TileView` |
| justified lines | | `ui::LinedFlowLayout` | |
| a table | | | `ui::TableView` |
| defined rows and columns | `ui::Grid` | | |

Hand-written children pick a layout control; children generated from a collection pick a `.layout`. `ui::ForEach` and `ui::TileView` take one: `ui::TileView{.layout = ui::WrapLayout{}}` keeps the view's selection, keyboard navigation and drag reordering and only changes where the cells go. `ui::Grid`, `ui::Canvas` and `ui::RelativePanel` exist only as controls, because they need placement data per child. `ui::Canvas` is the one for exact positions: each child carries its `.left` and `.top` in DIPs and nothing is worked out for it, see [Layout](../tutorials/basics/02-layout.md#canvas-exact-positions). A layout of your own is a class derived from `ui::VirtualizingLayout` or `ui::NonVirtualizingLayout`, handed to the same field as a `std::unique_ptr`.

## Ownership

Every node in the tree has exactly one owner, its parent, which holds it in a `std::unique_ptr`. There is no shared ownership anywhere in the tree, and destroying a node destroys its whole subtree, in order.

- `Window::set_content()` and a container's `add()` take ownership and return a plain pointer to the node. That pointer is an observer: use it, never delete it.
- `.control = my_button`, with `ui::Control<ui::Button> my_button;`, gives you the same kind of observer from a spec, typed by the spec's name. It is filled when the spec is built, so it is set by the time the statement that built the tree has finished. `my_button->` reaches the `ui::ButtonControl`.
- An observer is valid as long as the node is in the tree. A `ui::Match` without `.keep` rebuilds its subtree when the value it watches changes, and observers published from inside the old subtree dangle after that. The same goes for a `ui::ForEach` row whose item was removed and for a `ui::Lazy` with `.unload`.
- A spec is consumed by its `build()`, which is `&&`-qualified. To build the same description twice, write a function that returns a fresh spec each time.

A hand-built `std::unique_ptr<Element>` can go anywhere a spec can, in a child list or a content field, and is adopted unchanged.

## Handler Capture

Handlers are `ui::Callback<Sig>`, a `std::move_only_function`, so a handler can capture move-only state such as a `std::unique_ptr`. `ui::Action` is `Callback<void()>`.

What a handler captures by reference has to outlive the control. In `main()`, `[&]` is fine: the state lives until the message loop returns. In a function that returns a spec or a `ui::Spec`, it is not:

```cpp
// Wrong: `clicks` dies when the function returns, the button keeps a dangling reference.
ui::Spec counter()
{
    ui::Property<int> clicks{0};
    return ui::Button{.text = "+", .on_click = [&] { clicks = clicks.get() + 1; }};
}

// Right: the state is passed in and outlives the window.
ui::Spec counter(ui::Property<int>& clicks)
{
    return ui::HorizontalStack{
        .spacing  = 8,
        .content  = {
            ui::Text{.text = ui::bind(clicks, [](int n) { return std::to_string(n); })},
            ui::Button{.text = "+", .on_click = [&clicks] { clicks = clicks.get() + 1; }},
        },
    };
}
```

Capture by value, or capture a reference to state that outlives the window. Taking `Property<T>&` parameters makes the second one the natural thing to write.

## build() Order

Every `build()` runs the same four steps:

1. **Allocate** the control.
2. **Apply values and bindings**: each field's setter is called once with its initial value, and a `bind(...)` subscribes the control to its property.
3. **Install handlers**: `.on_*` and `.command`.
4. **Publish `.control`**.

Because handlers come after values, building a spec never fires a change handler for its own initial value. `ui::CheckBox{.checked = true, .on_checked_changed = ...}` builds a checked box without calling the handler; the handler runs for the first change the user makes after that.

Each binding's subscription belongs to the control that uses it, so it ends when the control is destroyed. The `Property` may be destroyed first as well; either side going away cuts the link.

## Naming

`ui::Button` describes and `ui::ButtonControl` is the control, both in the one namespace, `ui`. A name ending in `Control` is the live object, and the same name without it is the description you write. `ui::Spec` is the one "any piece of UI" value: what a component function returns, what a `.content` field and each entry of a `.content` list hold. `ui::Element` is the base of every node in the tree; keep those two apart.

A control's name is its spec's name plus `Control` (`ui::Slider` builds a `ui::SliderControl`). These are the pairs that do not follow that rule:

| Spec | Builds |
| --- | --- |
| `ui::VerticalStack`, `ui::HorizontalStack` | `ui::StackControl` (also reachable as `ui::VerticalStackControl` and `ui::HorizontalStackControl`) |
| `ui::Match`, `ui::Lazy`, `ui::State`, `ui::Shortcuts` | `ui::ContentHost` |

Constants are PascalCase, with no prefix: `ui::Unbounded`, `ui::colors::White`, `ui::styles::Spaced`, `ui::AutoSize`.

Item specs that only exist inside another control's spec (`ui::Tab`, `ui::NavItem`, `ui::Section`, `ui::Cell`) are documented on their parent control's page.

The same few shapes recur across controls, so a handler you have used on one control reads the same on the next:

| What | Shape |
| --- | --- |
| A two-state control | `.checked` and `on_checked_changed(bool)`, or `std::optional<bool>` where there is an indeterminate state (CheckBox, ToggleButton, AppBarToggleButton). There a `Property<bool>` binds two-way and an indeterminate state writes nothing to it, and a `Property<std::optional<bool>>` gets every state. |
| A selected index | `std::optional<std::size_t>`, unset for nothing selected, `on_selection_changed(std::optional<std::size_t>)`; a control that always has one selected (Pager, Sections) passes a plain `std::size_t`. |
| A selected item of a typed view | `on_selection_changed(std::optional<T>)`, a copy of the item, unset for nothing selected (ListView, TileView, TableView). |
| A selection with no single answer | `on_selection_changed()` (a text selection). |
| An event that can be stopped | The handler returns `ui::Cancel` to stop it, or `ui::Proceed`; one that returns nothing lets it go ahead. Every `on_closing` (Window, Dialog, the flyouts, InfoBar, TeachingTip, CommandBar), `on_pane_closing`, TableView's `on_sorting`, `on_beginning_edit` and `on_cell_edited`, AnnotatedScrollBar's `on_scrolling`, ComboBox's `on_text_submitted`, a view's `on_drag_items_starting`. Its args are input only. |
| A request a handler acts on | The handler returns `ui::NotHandled` to let it go on, or `ui::Handled`; one that returns nothing counts as handled. A shortcut's `invoked`, `on_context_requested`. `Window::on_key_preview` only watches keys pass by, so there a handler that returns nothing lets the key go on. |
| A number | `double` for values (Slider, NumberBox, Rating, ProgressBar); `float` for DIPs. |
