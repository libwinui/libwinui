# Specs and build()

The public API has two kinds of type in one namespace. A spec (`ui::Button`) describes a piece of
the tree; a control (`ui::ButtonControl`) is the retained node that description builds. This page
explains why the split exists, what `build()` does, how
a braced child list owns its children, and how bindings stay safe.

```cpp
ui::Property<int> clicks{0};

window.set_content(ui::VerticalStack{
    .spacing  = 12,
    .content  = {
        ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })},
        ui::Button{.text = "Click me", .on_click = [&] { ++clicks; }},
        ui::Button{.text = "Reset", .on_click = [&] { clicks = 0; }},
    },
});
```

## Why a Separate Layer

Designated initializers (`.spacing = 12`) only work on aggregates. An aggregate has no virtual
functions, no private data and no user-declared constructors, which is everything `Element` is
built from. A control cannot be the target of a designated initializer, so the declarative
surface is a separate layer of plain structs that build controls.

That split turns out to be useful on its own:

- The spec is trivially movable, has no virtuals, and can be printed and tested.
- The control keeps its invariants behind setters.
- The two evolve independently. The spec owns the short name and the control is that name plus
  `Control`: `ui::Text` builds a `ui::TextControl`. `ui::VerticalStack` and `ui::HorizontalStack`
  both build a `ui::StackControl`.

Keep the pairs straight. `ui::Button` describes; `ui::ButtonControl` is the control you hold a
pointer to. `ui::Spec` is a description value; `ui::Element` is a retained node. Mixing up
that last pair is the most expensive naming mistake available here, which is why the
type-erased description is called `Spec` and not `Element`.

The imperative path stays first class. Every control keeps its setters, `stack->add(...)` still
works, and a spec list accepts a plain `std::unique_ptr<Element>`, so a hand-built subtree (or a
control with no spec, like `native_controls::RichEditBox` (example code)) drops into a declared tree unchanged.

### Field Order Is a Public Contract

C++ requires designators in member-declaration order. A caller writes `.spacing` before
`.content` or not at all. So every spec follows one order:

1. properties, themselves in a fixed order:
   1. the texts that label the control: `header`, `title`, `subtitle`, a button's `text`,
      `description`, `placeholder_text`,
   2. the main value: `value`, `checked`, `items`, `text`, `selected_index` and its siblings,
      `is_open`, `expanded`,
   3. the values that limit it: `min`, `max`, `step`, `max_length`, `three_state`,
   4. every other property,
   5. the appearance properties last: `role`, `variant`, colors, fonts, `padding`, borders,
      `wrapping`, `text_alignment`,
2. appearance and layout (`style`, the common sizing fields, `visible`), then `tool_tip` and, on a
   control that takes input, `enabled`,
3. `accessibility`,
4. behavior: `command`, `command_parameter`, the main event, the other change events, then the
   generic pointer, focus and context events,
5. `control`,
6. children, then the template callables (`item`, `cell`, the `content` of `Match`,
   `Lazy` and `State`), always last.

Fields may be inserted anywhere, since an omitted designator is fine. They are never reordered:
that silently breaks every call site that named two of them.

### Naming the Children

Three names cover every list a spec holds:

- `content` is the UI the control lays out: one `ui::Spec`, or a list of them on a panel, the
  placed entries of Grid, Canvas, RelativePanel and TilePanel, and the pages of Carousel.
- `items` is the entries the control draws itself: data with an `item` template, strings, or
  typed entries such as `ui::MenuItem`, `ui::NavItem`, `ui::Section` and `ui::Tab`.
- `children` is nesting inside one entry: `ui::NavItem::children`, `ui::TreeNode::children`.

A slot with its own role keeps its own name (`pane_footer_content`, `primary`, `blocks`).

A plain name holds text, and the same name with `_content` holds UI in the same place: `header` is
a string on Expander, ListView and TileView, `header_content` a `ui::Spec` there (and wins over the
text). `footer_content` and NavigationShell's `pane_header_content` follow the same rule.

### The Common Fields

Every spec repeats the same eleven fields (`style`, `margin`, `width`, `height`, `min_width`,
`min_height`, `max_width`, `max_height`, `horizontal_alignment`, `vertical_alignment`, `visible`), then
`tool_tip`, `enabled` on a control that takes input, and `accessibility`, written out flat rather than
inherited. An aggregate with a base class has to be initialized positionally
(`Button{{.margin = 4}, ...}`), which wrecks the readability the layer exists for. The nine layout
fields are `Binding<std::optional<T>>`: unset lets the style fill in, and a set 0 means zero. The field list is on the
[conventions reference](../../reference/conventions.md#common-fields-on-every-spec).

`ui::apply_common(spec, node)` applies them, duck-typed on the field names. An explicitly
written field wins; `.style` (a `ui::Style`) fills in whatever was left at its default.

## build() Order

Each spec has an rvalue-qualified `std::unique_ptr<ui::Element> build() &&`. A description is
consumed exactly once; calling `build()` on a named spec needs an explicit `std::move`, which is
the diagnostic you want if someone tries to build the same description twice.

Every `build()` follows the same order:

1. **Allocate** the control.
2. **Apply values and bindings.** `ui::apply(binding, node, sink)` seeds the initial value and,
   when the binding is reactive, hands the resulting `Subscription` to the node.
3. **Install handlers**, after step 2, so seeding an initial value never fires a change callback
   the caller did not cause. Later bound values don't run it either. `apply()` and every bound
   collection update open a `detail::BoundUpdate` scope while they deliver a value, and inside one
   `ui::write_back()` writes the two-way property but skips the spec's handler, so an
   `on_*_changed` handler hears only what the user changed. `write_back_value()`, for open and
   close, doesn't check the scope.
4. **Apply the common fields** with `apply_common()`, then **publish `.control`** with
   `publish_control()` and return.

`Button::build()` in `src/ui/input/Button.cpp`, shortened:

```cpp
std::unique_ptr<ui::Element> Button::build() &&
{
    auto node = std::make_unique<ui::ButtonControl>();                       // 1
    apply(variant, *node, [raw = node.get()](const ui::ButtonVariant& v) { raw->set_variant(v); });
    apply(text, *node, [raw = node.get()](const std::string& v) { raw->set_text(v); }); // 2
    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });
    if (on_click)
    {
        node->on_click(std::move(on_click));                              // 3
    }
    // ... the remaining fields ...
    apply_common(*this, *node);                                           // 4
    publish_control(control, node.get());
    return node;
}
```

Containers add one step between 3 and 4: children are adopted in declaration order through the
same `add()`/`set_child()` calls a hand-built tree uses. The built node is exactly the tree the
imperative path would have produced, which is why the existing golden tests also cover the spec
layer.

Within step 2, order matters where one value constrains another:

- **Items before the selection.** The collection specs (`ComboBox.cpp`, `ListView.cpp`, `TreeView.cpp` and the rest) always set
  the items before applying the selection binding, because every one of these controls clamps
  its selection into the current item range. Selecting index 3 of a still empty list would land
  on nothing and then disagree with what the caller bound. When a bound item list changes later,
  ComboBox, RadioButtons and SelectorBar set the bound selected index again after the new items,
  so an index bound with `ui::bind_choice()` to the same list keeps its item.
- **Most specific wins.** Where a spec field overlaps the named `.style`, the explicit
  field wins, the same ladder `apply_common()` uses. A style holds layout only, so `ui::Text`
  resolves its typography in two steps: `.role` loses to an explicit `.text_style`.

`.control` takes a typed handle, `ui::Control<ui::Button>`; the field itself is a `ui::ControlOut<Button>`
that remembers where the handle is. The spec's `using control_type = ui::ButtonControl;` line is what
tells the handle its control type. It publishes the same non-owning observer `add()` returns and
changes no ownership. Most nodes need none; you name only what you
address later.

## Spec and SpecList

A braced list of children looks like it should force shared ownership.
`std::initializer_list<T>` hands out `const` references, because its backing array is
`const T[N]`, so a list of `std::unique_ptr` does not compile: it tries to copy a deleted
constructor. That constraint is what pushed comparable C++ declarative libraries onto
`shared_ptr` for their whole tree.

libwinui's lists take no `initializer_list` at all, and own their children with no reference
counting:

- `ui::Spec` (`include/winui/ui/spec/Spec.hpp`) converts implicitly from any `SpecLike` value
  (built on the spot, in the constructor) and from a `std::unique_ptr` to any `Element` subclass.
  That is what lets a list mix specs and hand-built controls.
- `ui::SpecList` takes a braced list of two or more entries through a variadic constructor. Each
  entry arrives as its own argument with its own type, becomes a `Spec` inside the constructor,
  and its node moves into a `std::vector<std::unique_ptr<Element>>`. A single entry goes through
  the one-spec constructor. From then on the list owns real `unique_ptr`s and can be stored,
  moved or returned.
- A list of slots (`ui::CellList`, `ui::CanvasItemList`, `ui::TabList`, ...) takes its entries
  the same way, through a variadic constructor whose entries must all be the slot type. So each
  entry is written with its type, `ui::Cell{.row = 0, .content = ...}`: a bare braced entry has
  no type to deduce. The constructor moves each slot's content out.
- Guaranteed copy elision constructs each `ui::Button{...}` prvalue directly as the argument, so
  no spec is copied along the way.

The reason for both is compile memory on MSVC, below.

```cpp
ui::VerticalStack{.content = {
    ui::Text{.text = "Sales"}, // a spec: built as the list is formed
    std::move(chart),          // a std::unique_ptr<MyChart>: adopted unchanged
}}
```

A single element needs no braces, `.content = ui::Text{.text = "Sales"}` is a list of one.
`ui::content(...)` is the variadic form, for a list passed through a function where braces
would not deduce. For a list built in a loop, push onto `SpecList::nodes` directly.

`SpecLike` is a concept: anything with `build() &&` returning something convertible to
`std::unique_ptr<ui::Element>`. It is constrained rather than duck-typed so a mistake
reports at the caller's line ("no conversion from 'int' to 'ui::Spec'") instead of ten frames
inside the library. An app's own spec type satisfies it and composes anywhere a built-in one
does. `ui::Spec` (`ui/spec/Spec.hpp`) is a move-only, type-erased description that already holds
its built node; a component is just a function returning one.

### Compile Time on MSVC

MSVC's compile time and memory grow with how deeply specs nest inside one expression. Two parts of
the design keep that growth down:

- `ui::SpecList` takes a braced list of two or more entries as a variadic constructor. Each entry
  keeps its own type and becomes a `ui::Spec` inside the constructor body. Lists of slots (Grid
  cells, Canvas items, tabs) work the same way, which is why an entry is written `ui::Cell{...}`
  rather than as a bare braced `{...}`.
- `Binding<T>` has a small constructor set. Its converting constructor refuses a lossy value (a
  pointer into a bool field, a double into an int one) with a `static_assert` in its body, so
  `std::is_convertible_v<int*, Binding<bool>>` is true and the tests check
  `detail::binding_value_converts` instead.

Very deep nesting in one expression still compiles slowly. Building an inner subtree into a named
`ui::Spec` first helps, since it is examined once, in its own full-expression.

## Property and Subscription

`include/winui/core/Property.hpp` is the reactive half. A piece of state is written down once
and every display of it is derived. It is deliberately small: no reflection, no string paths, no
property registry, no change-notification base class. A binding is an ordinary C++ expression.

Two rules make it safe, and `tests/test_property.cpp` pins both down. Change them only with a
very good reason.

**The equality gate.** `Property<T>::set()` compares before it assigns. An unchanged value
notifies nobody, a recomputation landing on the same result stops there, and an accidental
dependency cycle terminates instead of recursing. `T` must be `std::equality_comparable`; the
gate is not optional. `update(fn)` mutates a copy and then goes through the same gate.

**The subscription is owned by the subscriber, and is safe in both lifetime directions.**
`observe()` returns a move-only `Subscription` the caller must keep; dropping it unsubscribes.
Elements keep theirs through `Element::add_subscription()`, so because the tree is single-owner
and destroyed hierarchically, a subscription lives exactly as long as its node. The mirror case
(the `Property` dying first, say a view-model owned by a closing document while a dialog bound
to it is still being torn down) is handled by the same structure:

```text
Property<T> --head--> Subscription <--> Subscription <--> ...
      ^                     |
      +------- owner_ ------+
```

`~Subscription()` unlinks itself from the list. `~PropertyBase()` walks the list and nulls every
`owner_`, after which those subscriptions report `alive() == false` and their destructors touch
nothing. Destroying either end severs the edge. The cost is two pointers per edge: no
allocation, no atomics, no `shared_ptr`.

`dispatch()` caches `next` before calling each handler, because a handler may destroy its own
`Subscription` from inside the callback.

`Subscription` is not a template. An element holds one `std::vector<Subscription>` with bindings
to an `int`, a `std::string` and a `bool` side by side, so the value type is erased once, at
dispatch. That vector is also why `Element` is move-only.

`Signal` is the same edge list with no value and no equality gate: `raise()` always reaches
every observer. A `Command` raises one when any of its bound fields changes (`Command::observe()`).

Everything here is UI-thread only. `set()` is not synchronized; a worker hands a result back
through `Window::post()`.

## Bindings in build()

A spec field that can be reactive has type `Binding<T>`. It holds either a constant or a live
binding to a `Property`, and the two are told apart in the source:

```cpp
ui::Text{.text = "static label"}      // a value: set once, never tracked
ui::Text{.text = title}  // a binding: tracked
ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })}
```

`Binding<T>` converts implicitly from a plain `T` (and from a string literal when `T` is
`std::string`), so the constant case stays quiet. `bind()` builds the reactive form: an initial
value plus an `Attach` thunk that, when called with a sink, returns the `Subscription`. The
transform overload runs `fn` once for the initial value and again on every change, so a format
string exists once in a program instead of once per writer.

`ui::apply()` is the single place the reactive layer touches the control layer:

```cpp
template <typename T, typename Fn>
    requires std::invocable<Fn&, const T&>
inline void apply(Binding<T>& binding, Element& target, Fn sink)
{
    {
        const detail::BoundUpdate bound;
        sink(binding.initial());
    }
    if (binding.reactive())
    {
        target.add_subscription(binding.subscribe(
            [sink = std::move(sink)](const T& value) mutable
            {
                const detail::BoundUpdate bound;
                sink(value);
            }));
    }
}
```

While a `BoundUpdate` scope is open, `write_back()` skips the spec's handler but still writes the
two-way property.

The sink is the control's own setter, so a bound value changes exactly the way an imperative
call would: the setter runs, and the setter marks the element dirty. A spec that binds N fields
produces N subscriptions on the node (`Element::subscription_count()`), and
`clear_subscriptions()` is the escape hatch for "stop tracking, I am driving this by hand now".

A few details worth knowing:

- `bind()` of a `const` Property, and `bind_one_way()`, are one-way, so a property the host writes
  and the app only reads (`Window::state()`) binds like any other. `bind()` of a non-const
  Property returns a `TwoWayBinding`, which an editable field writes back through `write_back()`.
  `bind(prop, to, from)` converts both ways, and `bind_choice(prop, choices)` maps an index onto
  one of a fixed list or of a `Property<std::vector<T>>`.
- The rvalue overloads of `bind()` are deleted. A binding keeps a reference to its source, and a
  temporary `Property` would be gone before the first change arrived.
- The non-const `bind(prop, fn)` overload forwards to the const one, so that `bind(title, fn)`
  never finds `std::bind` through argument-dependent lookup.

### Handlers and Lifetimes

Handlers are `ui::Callback<Sig>`, which is `std::move_only_function`, never `std::function`:
a `std::function` cannot hold a lambda that captures a `unique_ptr`. The exceptions are the
items that live in brace-initialized `std::vector`s, since a braced list can only be copied from:
`MenuItem::on_click` and `on_checked_changed`, `ThumbnailButton::on_click`, `Inline::on_click`,
the callables of a `TableColumn<T>` and `SwipeItem::on_invoked`. `Theme::with_dark_mode` and a
`Binding`'s thunks stay copyable for the same reason, because `Theme` and `Binding` are copyable values.

A handler in a spec that is returned from a function must capture by value, or capture a
reference to state that outlives the window. `[&]` is right in a `main()`-scoped example and a
dangling reference in a component factory. Taking `Property<T>&` parameters (model state the
caller owns) makes the right thing the easy thing.

### Where Declarative Construction Stops

A spec describes a fixed tree. Where the structure depends on state, `ui/spec/Dynamic.hpp` (and
`ui/collections/ForEach.hpp` for the repeater) makes
each case a named node with a visible rebuild boundary instead of a global re-derive. None of
them compares trees: each holds the change it was told about.

- **`.visible`** on every spec maps to `Element::set_visibility()`. Collapsing destroys nothing,
  so it is the default answer to "show this while that box is ticked".
- **`ui::Match<T>`** builds a `ContentHost` that owns one subtree and rebuilds it (through
  `.content`) whenever its value changes. The equality gate makes a redundant write free, and
  the rebuild costs the subtree, not the screen. A rebuild destroys the old subtree, so `.control`
  observers from inside it dangle afterwards. With `.keep` the host holds a layout-less
  `LayoutPanelControl` instead, one `ContentHost` per value seen, all but the current one
  collapsed.
- **`ui::Lazy`** is `.visible` with the build deferred to the first `true`.
- **`ui::State<S>`** builds a `ContentHost` subclass that owns an `S`. The state is a base class
  listed before `ContentHost`, so it is destroyed after the subtree whose handlers refer to it.
- **`ui::ForEach<T>`** builds a `ForEachControl` and forwards collection changes to its
  `notify_*()` calls. An `ObservableVector` reports them itself (`core/ObservableVector.hpp`,
  the same intrusive subscription list as `Property`). For a `Property<std::vector<T>>` with a
  `.key`, `ItemKey::match()` pairs old and new items by key and `detail::keyed_changes()`
  (`src/core/ObservableVector.cpp`) turns that into removals, the fewest moves (the rows on the
  longest increasing run stay put), insertions and replacements. That is the one diff in the
  library, and it compares keys. `ui::item_count(prop, key)` feeds the same change stream to the
  index-based views, which is why their selection survives a sort.

A child that leaves with a transition (`AddRemoveTransition` on a stack or a `ForEach`,
`ContentTransition` on a `ContentHost`) is handed to `detail::ExitGhosts`
(`src/ui/ExitGhosts.hpp`, private): out of layout, input and the tab ring at once, drawn through its exit,
then destroyed.

## Where the Code Is

| What | Where |
| --- | --- |
| A spec aggregate | its control's header, named after the spec: `include/winui/ui/input/Button.hpp` holds `ui::Button` and `ui::ButtonControl` |
| Its `build()` body | the control's source: `src/ui/input/Button.cpp` |
| `Style`, `apply()`, `apply_common()` | `include/winui/ui/spec/Common.hpp` |
| `Spec`, `SpecList`, `content()`, `when()`, `SpecLike` | `include/winui/ui/spec/Spec.hpp` |
| `Control<S>`, `ControlOut<S>`, `publish_control()` | `include/winui/ui/spec/Control.hpp` |
| `LayoutSpec`, `LayoutLike` | `include/winui/ui/spec/LayoutSpec.hpp`; the layout values (`StackLayout`, `TileLayout`, ...) sit beside their engines in `include/winui/ui/layouts/` |
| `Match`, `Lazy`, `State` | `include/winui/ui/spec/Dynamic.hpp` (templates), `src/ui/spec/Dynamic.cpp` |
| `ForEach` | beside its control in `include/winui/ui/collections/ForEach.hpp` (a template), `src/ui/collections/ForEach.cpp` |
| `ListView<T>`, `TileView<T>`, `TableView<T>` | each view's header in `include/winui/ui/collections/`, a template over the index-based build in `ui::detail` beside it; what they share (`ItemTemplate`, the item-to-index adapters) is `include/winui/ui/spec/ItemsModel.hpp` |
| `ObservableVector`, `CollectionChange`, `ItemCount`, `ItemKey`, `item_count()`, `bind_each()` | `include/winui/core/ObservableVector.hpp`, `src/core/ObservableVector.cpp` |
| `Property`, `Subscription`, `Signal`, `Binding`, `TwoWayBinding`, `bind()`, `bind_one_way()`, `bind_choice()` | `include/winui/core/Property.hpp` |

When you add a control, add its spec in the same change. A control whose spec looks awkward
usually has an awkward API underneath. The step-by-step is on
[Add a Control](../hooks/add-a-control.md) and [Add a Spec Field](../hooks/add-a-spec-field.md).
