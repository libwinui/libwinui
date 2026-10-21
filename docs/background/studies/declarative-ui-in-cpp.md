# Declarative UI in C++

What C++23 permits, checked with a compiler, and the API it led to.

Most write-ups on declarative C++ UI propose syntax and argue about it. This study went the other way around: every syntactic claim was compiled first, on MSVC 19.51 with `/std:c++23preview /W4 /permissive-`, and the lifetime design was additionally run under AddressSanitizer. The single most useful result came out of that step. The most popular shape for a declarative C++ tree, properties and children in one brace pair, does not compile, and no amount of design discussion would have shown that.

The study separates two things that web frameworks welded together, surveys how other toolkits buy them, derives six constraints C++ puts on the answer, and ends with the surface libwinui ships today: plain aggregate specs for construction, and `Property<T>` for updates.

## What C++23 Permits

### Designated Fields and Positional Children Do Not Mix

The shape most proposals reach for looks like this:

```cpp
VStack{
    .spacing = 12,

    Text{"Appearance"},          // positional child after a designated field
    HStack{ /* ... */ },
}
```

It is ill-formed. Aggregate initialization forbids mixing designated and non-designated initializer clauses in one list[^dcl-init-aggr], and MSVC says so plainly:

```text
error C7556: cannot mix designated-initializers with non-designated-initializers
error C2078: too many initializers
```

This is the standard, not a compiler quirk to work around. The legal spelling is one designated field for the children, declared last:

```cpp
ui::VerticalStack{
    .spacing  = 12,
    .content  = {
        ui::Text{.text = "Appearance"},
        ui::HorizontalStack{.content = { /* ... */ }},
    },
}
```

That is `.content = ` per container, a handful of characters, and it buys a construct that exists.

### Designator Order Is Part of the API

C++, unlike C, requires designators to appear in member declaration order[^dcl-init-aggr]:

```text
error C7560: 'text': designators must appear in member declaration order of class 'Button'
```

Three consequences follow, and they fix the shape of every spec struct:

- Designated initializers are easy to *omit*, not to reorder. A caller who names two fields must name them in declaration order.
- Children have to be the last field, because a caller writes `.spacing` before `.content` or not at all.
- Field order is a source-stability contract. Reordering two fields breaks every call site that named both. *Inserting* a new field between two existing ones is fine, since omitted designators are allowed, and a probe confirmed that existing call sites keep compiling. So a spec can grow, it just cannot be rearranged.

### Positional Children and Named Properties Exclude Each Other

A variadic constructor would buy `VStack{ Text{...}, Button{...} }`. A user-declared constructor makes the type a non-aggregate though, and designated initialization then fails on that same type:

```text
error C7562: 'VStack': designated initialization can only be used to initialize aggregate class types
```

You pick one per type. Named properties win, because a container with no properties at all is the rare case, and `.content = { ... }` costs one line.

### `std::initializer_list` Cannot Own

A braced list of `std::unique_ptr` does not compile, because the list's backing array is `const T[N]` and moving out of it would need the deleted copy constructor[^knatten]:

```text
error C2672: 'std::construct_at': no matching overloaded function found
... 'std::unique_ptr<Element>::unique_ptr(const std::unique_ptr<Element> &)': attempting to reference a deleted function
```

This is the constraint that pushed AUI, the closest prior art, onto `shared_ptr` for its whole tree[^aui]. A workaround exists that keeps single ownership; it is described under [the proposed surface](#the-child-list).

### Everything Else That Was Checked

| Facility | Needed for | Result |
| --- | --- | --- |
| Designated initializers on aggregates with default member initializers | The whole spec layer | Works |
| Nested designated init, `.accessibility = {.name = "Save"}` | Grouped sub-fields | Works |
| Aggregate with a public base class | Sharing common layout fields | Works, but the base must be initialized positionally (`{{.margin = 4}, ...}`) |
| Deducing `this` returning the derived type | A fluent escape hatch | Works: `decltype(leaf.margin(4.f))` is `Leaf&`, not `Node&` |
| `std::move_only_function` | Handlers that capture a `unique_ptr` | Available, `__cpp_lib_move_only_function == 202110L` |
| `std::function` holding a move-only lambda | | Rejected: "The target function object type must be copy constructible" |
| `consteval` user-defined literals, `200_dip`, `1_fr` | A grid track DSL | Works, constant-evaluated |
| `constexpr operator\|` on a style aggregate | Composable styles | Works, checked with `static_assert` |
| Member pointer as a key, `.key = &Photo::id` | List identity without reflection | Works |
| A type-erased move-only description returned from a function | Components as functions | Works |

### Diagnostics Are Part of the API

Error messages were measured on the resulting design:

| Mistake | Diagnostic |
| --- | --- |
| Misspelled property (`.txt` for `.text`) | One line: `error C7559: 'txt': designator does not name a direct non-static data member of type 'ButtonSpec'` |
| A non-spec value in a child list (`42`) | Four lines, ending in `note: Element '2': no conversion from 'int' to 'Child'` |

Both point at the caller's own line. That is the practical payoff of keeping templates shallow: one non-template erased type at the child boundary, a `concept` on each template parameter, and specs kept as plain aggregates so a bad field name points at the field.

## The Pattern Survey

### Two Properties, Not One

"Declarative" is two independent properties:

| Property | What it buys | What goes wrong without it |
| --- | --- | --- |
| Declarative construction | The source's shape matches the UI's shape; you read the structure off the indentation | The source is a build script for the UI rather than a description of it |
| Declarative update | You never write "when X changes, call `set_text`"; the framework derives it | Every piece of state is written down twice and the copies drift |

They really are orthogonal. Cycfi Elements has excellent construction with fully manual updates. Dear ImGui has automatic updates with construction that reads as a pile of `if` statements. React, SwiftUI, Compose and Xilem buy both with a single mechanism, rebuilding a description and diffing it, and that bundling is why people believe the two are one thing. For C++ the bundle is a bad deal (see [below](#why-diffing-loses-in-c)), so the two are bought separately here: construction through plain aggregates, updates through fine-grained observable values.

### The Survey

| Pattern | Exemplars | The one thing worth taking | The reason not to adopt it |
| --- | --- | --- | --- |
| A. Imperative both ways | MFC, WTL, Qt Widgets, WinForms' `InitializeComponent` | Explicit, debuggable, fast to compile | Structure is invisible; every displayed value is written twice |
| A′. Macro tables | WTL `BEGIN_MSG_MAP_EX` | Truly declarative, compiles to a tight switch | Opaque to clang-format, IDE navigation and breakpoints; cannot compose, so it grows `#ifdef`s |
| B. Markup plus reflective binding | XAML `{Binding}`, QML | Nesting is literal; properties are named at the point of use | Boilerplate and string paths: a misspelled `RaisePropertyChanged(L"Name")` fails silently as an unchanged pixel |
| C. Markup plus compiled binding | `x:Bind`, Slint | A binding must be a compile-time-checked expression, never a string path | An external compiler is a permanent tax: a build step, an editor plugin, a second type system, an expression ceiling |
| D. C++ DSEL with value semantics | Cycfi Elements[^elements] | Composition instead of a property on every control | The subtree's type is the subtree's shape; every UI function returns `auto`; errors backtrace through every wrapper |
| E. Specs plus signals | AUI "declarative contracts"[^aui] | Designated initializers give C++ the named arguments it never had | The whole tree hangs on `shared_ptr` |
| F. Immediate mode | Dear ImGui, CIG | There is no second copy of the state | No persistent tree, so no UI Automation tree, no incremental layout, no hosted `HWND` children, no compositor animation |
| G. Diffing onto a retained tree | React, SwiftUI, Jetpack Compose, Flutter, Elm, Xilem | "The UI is a function of the state" becomes literally true | See the next two sections |

A few notes that the table compresses:

- **Pattern B's cost is concrete.** In the C++/WinRT photo-editor sample, one plain view model with twelve properties takes 45 lines of MIDL, 215 lines of header and 72 lines of source, plus generated projection code. Every setter calls `RaisePropertyChanged` with the property's name as a string.
- **Pattern D's positional arguments hurt too.** In `margin({25, 20, 25, 18}, ...)`, which one is top? Designated initializers fix exactly this.
- **AUI arrived at the same shape independently.** It started with brace composition and moved to aggregate initialization with named fields plus reactive values (`.text = AUI_REACT(...)`). Convergent evolution from the framework that pushed hardest on pure-C++ declarative syntax is worth taking seriously.
- **Immediate mode's appeal is real.** The loop reads the model directly, so a stale display is not a representable bug. CIG even uses C designated initializers for its parameter blocks.
- **Pattern G spans languages.** Elm's model/update/view architecture renders through a virtual DOM that is diffed[^elm]. Flutter rebuilds immutable widget descriptions and reconciles them against a longer-lived element tree[^flutter]. Compose re-runs composable functions and relies on a compiler plugin for positional memoization[^compose]. Xilem, in Rust, needs a separate view tree, widget tree and view-state tree to make ownership work without a garbage collector[^xilem].

Outside libwinui's own design, the only pure-C++ entry in the "both properties" quadrant is AUI. Everything else there is either a different language compiled to C++ (QML, `.slint`, XAML) or a language with a garbage collector. The constraints below are why.

## Six Constraints

**C1. `std::initializer_list` is const, so brace lists cannot own.** Shown above. There are two clean answers: a `mutable` member drained inside the full-expression, or a variadic factory. libwinui ships the first, implemented over the second.

**C2. Aggregates cannot be polymorphic.** Designated initializers need an aggregate: no virtual functions, no private non-static data members, no user-declared constructors. A control has virtual `measure_override`/`arrange_override`/`render` and private state behind accessors, as it should. A control can therefore never be the target of a designated initializer (`error C7562`). The consequence is the best part of the design rather than a compromise: the declarative surface is a separate layer of plain data structs that build controls. The spec is trivially movable, testable and printable, the control keeps its invariants, and the two evolve independently.

**C3. No reflection on MSVC.** C++26 adopted static reflection (P2996)[^p2996], but at the time of the study MSVC had no public support for it and no announced date. For an MSVC-first framework, reflection is not a design input. So there is no `{Binding Path=Document.Title}`, no property registry, no data template chosen by runtime type name, and no `find<Button>("save")` lookup. A binding has to be an ordinary C++ expression: a lambda, a member pointer, a reference to a property object. That pushes the design toward the `x:Bind` model, which is the one that works anyway.

**C4. No garbage collector, and closures have lifetimes.** In Swift or Kotlin a view captures what it likes and the runtime keeps it alive. In C++, `[&]` is a dangling reference waiting for a scope exit. It is correct in a `main()`-scoped example and wrong the moment a spec is returned from a factory function. There is no language fix; it is a documented convention (see [capture lifetime](#capture-lifetime)).

**C5. Compile time and error messages are part of the API.** Shallow templates over deep ones: one erased type at the child boundary rather than a type encoding the whole subtree, a `concept` on every template parameter, and specs as plain aggregates. The measured result is one line for a typo and four for a bad child.

**C6. Designator order is a public contract.** Children last, properties never reordered, new fields inserted anywhere. This fixes the shape of every spec struct.

## Why Diffing Loses in C++

Rebuild-and-diff is the most powerful pattern in the survey. It is the only one that makes stale UI unrepresentable instead of merely discouraged. It still loses in C++, for four reasons.

1. **Its cost model is backwards for a native framework.** Rebuild-and-diff does work proportional to the size of the UI; the change is proportional to the size of the edit. Web measurements are stark: in one reordering benchmark a diffing framework performed about 102,000 DOM mutations where a signals-based one performed 7[^signals]. Those numbers are JavaScript, but the asymmetry is structural. A native framework promising Win32-level responsiveness wants work proportional to what changed.
2. **It duplicates information the tree already has.** libwinui's retained tree carries `DirtyFlags` (`Measure`, `Arrange`, `Visual`) and a stable `VisualId` per node, and every setter marks the right flags at the moment of assignment. A diff engine would compute, expensively, exactly what a setter knows for free. Diffing is how you recover invalidation information you threw away, and this tree does not throw it away.
3. **Structural identity needs language support C++ lacks.** SwiftUI derives identity from type and tree position through result builders[^swiftui-identity]; Compose inserts positional keys with a compiler plugin. C++ has neither, so identity would need manual keys on every node, which is strictly worse than the stable `VisualId` the retained tree already hands out.
4. **The bug class it removes can be removed more cheaply.** The prize of diffing is that stale UI cannot happen. That follows from a weaker property: there is exactly one writable copy of each piece of state and every display of it is derived. Fine-grained observable values give you that without rebuilding a tree.

The honest counterpoint: signals make stale UI *unnecessary*, not *impossible*. Nothing stops a caller from reaching through a raw `ButtonControl*` and calling `set_text` behind a binding's back, and signal graphs have their own failure modes, such as diamond dependencies that recompute twice and subscription leaks. Three cheap mitigations cover most of it, and all three are in the design below: gate every write on inequality so no-op writes cost nothing and cycles terminate, make subscriptions owned and safe in both lifetime directions, and keep imperative setters as a documented escape hatch rather than the default path.

## The Proposed Surface

This section describes what shipped. The layers are:

| Layer | Where | Role | Lifetime |
| --- | --- | --- | --- |
| Description | `ui::*`, plain aggregates, each in its control's header | Movable, virtual-free spec structs consumed by an `&&`-qualified `build()` | Exists only during construction |
| Controls | `ui::*` | The retained tree: `Element`, single-owner `std::unique_ptr`, `VisualId`, `DirtyFlags` | Owned by the tree, destroyed hierarchically |
| Reactivity | `ui::Property<T>`, `ui::Subscription`, `ui::Binding<T>` in `include/winui/core/Property.hpp` | Observable values; a write updates exactly the dependent nodes | A subscription lives exactly as long as the node that owns it |

The spec layer is purely additive. The controls keep every setter, `stack->add(...)` still works, and the render-command stream a `TestRenderer` records is the same whichever way a tree was built.

### Before and After

The imperative form of a click counter:

```cpp
auto               stack   = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
ui::TextControl*   heading = stack->add(std::make_unique<ui::TextControl>("Clicks: 0"));
ui::ButtonControl* click   = stack->add(std::make_unique<ui::ButtonControl>("Click me"));
ui::ButtonControl* reset   = stack->add(std::make_unique<ui::ButtonControl>("Reset"));
stack->set_spacing(12.0f);

int clicks = 0;
click->on_click([&, heading] { heading->set_text(std::format("Clicks: {}", ++clicks)); });
reset->on_click([&, heading] { clicks = 0; heading->set_text("Clicks: 0"); });
```

Four names for three controls, the structure lives in `add()` call order, `set_spacing` sits four lines from the thing it describes, and the label's format is written twice, in two lambdas, the second of which must know how the first renders. That is the archetypal stale-UI bug. The same screen through the spec layer:

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

The format exists once. Reset cannot render differently from Click, because Reset no longer renders anything; it writes to the model and the binding does the rest.

### The Spec and the Control

Because of C2, `ui::Button` describes and `ui::ButtonControl` is the live control. A spec is a plain aggregate with no bases, no virtuals, no user-declared constructors and no private members, laid out in a fixed order: properties, then appearance and layout, then accessibility, then behavior (`command`, then the `on_*` events), then `control`, then children. The trimmed shape of `ui::Button` from `include/winui/ui/input/Button.hpp`, where it follows `ui::ButtonControl`:

```cpp
struct Button
{
    using control_type = ButtonControl;

    // ---- properties ----
    Binding<std::string>   text{};
    Binding<ButtonVariant> variant{ButtonVariant::Neutral};
    // ...

    // ---- appearance / layout ----
    Style                             style{};
    Binding<std::optional<Thickness>> margin{};
    Binding<std::optional<float>>     width{};
    // ... height, min/max sizes, horizontal_alignment, vertical_alignment, visible
    Binding<std::string> tool_tip{};
    Binding<bool>        enabled{true};
    Accessibility        accessibility{};

    // ---- behavior ----
    CommandRef  command{};
    std::string command_parameter{};
    Action      on_click{};
    // ...

    // ---- identity ----
    ControlOut<Button> control{};

    // ---- children ----
    Spec content{}; // arbitrary content instead of a plain label

    [[nodiscard]] std::unique_ptr<Element> build() &&;
};
```

Three details matter:

- **`build()` is `&&`-qualified.** A spec is consumed once. Building a named spec needs an explicit `std::move`, which is the diagnostic you want if someone tries to build the same description twice.
- **The common fields are repeated, not inherited.** An aggregate may have a public base, but that base must then be initialized positionally (`Button{{.margin = 4}, "OK"}`), which wrecks the readability the layer exists for. So every spec writes out the same eleven common fields (`style`, `margin`, `width`, `height`, `min_width`, `min_height`, `max_width`, `max_height`, `horizontal_alignment`, `vertical_alignment`, `visible`), then `tool_tip`, then `enabled` on a control that takes input, then `accessibility`.
- **The layout fields mirror `Element`.** Margin, size limits and alignment are properties on `Element` itself, applied generically by its non-virtual `measure()` and `arrange()` before the `measure_override()` and `arrange_override()` hooks run. The Elements-style alternative, one-child wrapper nodes for margin and alignment, lost to the XAML-style model, and the spec mirrors what shipped.

A spec's name need not match its control's: `ui::Text` builds a `ui::TextControl`, and `ui::VerticalStack` and `ui::HorizontalStack` both build a `ui::StackControl`. The spec layer is where caller-facing vocabulary is picked. The build order inside every `build()` is fixed too: allocate, apply values and bindings, install handlers, publish `.control`. Handlers come after values, so seeding an initial value never fires a change callback the caller did not cause. The [specs and build](../../contributing/how-it-works/specs-and-build.md) page walks through one in detail.

### The Child List

`include/winui/ui/spec/Spec.hpp` defeats C1 without reference counting:

```cpp
template <typename S>
concept SpecLike = requires(S&& spec) {
    { std::forward<S>(spec).build() } -> std::convertible_to<std::unique_ptr<ui::Element>>;
};

class Spec
{
public:
    template <SpecLike S>
    Spec(S&& spec) : node_(std::forward<S>(spec).build()) {}

    [[nodiscard]] std::unique_ptr<ui::Element> take() const { return std::move(node_); }

private:
    mutable std::unique_ptr<ui::Element> node_;
};
```

`std::initializer_list` hands out `const Spec&`, and `mutable` is precisely the specifier that permits modification through a const access path, so `take()` can move the node out exactly once. `SpecList` drains every element inside its own constructor, which runs inside the full-expression that created the backing array, so nothing dangles. Afterwards the list owns real `unique_ptr`s and can be stored, moved or returned. Guaranteed copy elision constructs each `ui::Button{...}` prvalue directly in the array, so no spec is ever copied.

The probe built a three-deep tree this way, rendered it and destroyed it correctly, and separately confirmed that the same brace list of bare `unique_ptr`s does not compile. That second check is what makes the indirection necessary rather than decorative.

`Spec` also converts from a plain `std::unique_ptr` to any `Element` subclass, so a hand-built subtree or a control with no spec drops into a declared tree unchanged. The variadic form `ui::content(a, b, c)` is there for lists assembled in code, and a `SpecList` can be filled in a loop with `push_back()`.

### Components Are Functions

C++ already has functions, templates and lambdas. A component registry would be ceremony on top of them. A component is a function returning a `ui::Spec`:

```cpp
ui::Spec user_row(ui::Property<std::string>& name, std::string email)
{
    return ui::HorizontalStack{
        .spacing  = 8,
        .content  = {
            ui::VerticalStack{.content = {
                ui::Text{.text = name},
                ui::Text{.text = std::move(email), .role = ui::TextRole::Caption},
            }},
        },
    };
}

window.set_content(ui::VerticalStack{
    .content = {user_row(alice, "alice@example.com"), user_row(bob, "bob@example.com")},
});
```

`ui::Spec` (`include/winui/ui/spec/Spec.hpp`) is a move-only, type-erased description holding the already-built node. It is itself `SpecLike`, so it fits a `.content` list, a single-content field or `Window::set_content()`. The obvious name for this type would have been `Element`, which is already the retained node base; confusing a description value with a live node would be the most expensive naming mistake available, hence `Spec`. The [components tutorial](../../tutorials/basics/06-components.md) builds on this.

### Grid Tracks and a Small Unit DSL

`consteval` literals make a track list free at runtime. `GridLength` has exactly three kinds (Auto, Star, fixed), so the vocabulary cannot sprawl:

```cpp
using ui::AutoSize;
using ui::operator""_fr;
using ui::operator""_dip;

window.set_content(ui::Grid{
    .columns = {200_dip, 1_fr},
    .rows    = {AutoSize, 1_fr},
    .content = {
        ui::Cell{.row = 0, .column = 0, .column_span = 2, .content = ui::Text{.text = "Header"}},
        ui::Cell{.row = 1, .column = 0, .content = ui::Text{.text = "Sidebar"}},
        ui::Cell{.row = 1, .column = 1, .content = ui::Text{.text = "Content"}},
    },
});
```

### Events, Commands, Styles and Accessibility

**Events** are named `on_*` fields, never a generic `.on(...)`. Every handler is a `ui::Callback<Sig>`, an alias for `std::move_only_function` in `include/winui/core/Callback.hpp`, with `ui::Action` for the common `Callback<void()>`. The reason is concrete: `std::function` cannot hold a handler that captures a `unique_ptr` (a handler owning a dialog, a buffer or a COM pointer), and MSVC rejects it outright. The move-only type holds it and keeps the spec movable.

For key events the study proposed returning an `EventResult` instead of mutating a `handled` flag, with an adapter so a `void` lambda still works. libwinui ships that as `ui::Handling`: a key hook returns `ui::Handled` when it used the key and `ui::NotHandled` when it did not, as `Window::on_key_preview()` and every spec's `on_key_down` do. A lambda that returns nothing gets the event's default.

**Commands** decouple an action from its triggers. A command is a plain aggregate that a button, a menu item, an app-bar button and a keyboard accelerator can all point at, so enablement and behavior are written once. The command is `ui::Command` (WinUI's `XamlUICommand`), which also carries a label, icon, shortcut and description:

```cpp
ui::Property<bool> dirty{false};

ui::Command save{
    .text    = "Save",
    .icon    = ui::Symbol::Save,
    .enabled = dirty,
    .execute = [&] { save_document(); dirty = false; },
};

window.set_content(ui::CommandBar{
    .primary = {ui::AppBarButton{.command = save}},
});
```

Every field but `execute` is a `Binding`, like a spec field, so `enabled` follows a property and every control using the command follows it in turn. An earlier version had a `can_execute` predicate and a `notify_can_execute_changed()` the app called when its answer might have changed (ICommand's `CanExecuteChanged`). It worked with any state shape, but every command needed that extra call, and forgetting it left a button in the wrong state.

**Styles** are named and composable, and hold layout only: the margin, the sizes and the alignments every spec carries, so `.style` works on every spec. Colors stay with the theme. That is the one constraint the naive "per-widget `.background = blue`" proposals miss: libwinui resolves appearance from the active theme (light or dark, high contrast, accent, era), and a style of literal colors would silently opt out of all of it. Typography is a `ui::Text`'s `.role`, a step of the theme's type ramp resolved at render time. Composition is `constexpr`, with the right-hand side winning where it sets a field:

```cpp
inline constexpr ui::Style card = ui::styles::Spaced | ui::styles::Leading;
```

**Accessibility** lives on the spec because the spec is the one place where the caller is already writing down each control's semantics. Nested designated initialization keeps the common case free:

```cpp
ui::Button{.text = "Save"}                                  // name derives from the text

ui::Button{
    .text          = "\xEE\x9D\x8E",                        // an icon glyph
    .accessibility = {.name = "Save document", .description = "Saves the current document"},
}
```

## Identity, Reactivity and Lifetime

### Addressing a Node You Declared

A tree declared as one expression still needs to be talked to later: focus a node, disable it, host an `HWND` in it.

| Approach | Example | Verdict |
| --- | --- | --- |
| Name in markup plus code generation | `x:Name="ItemImage"` | Needs the external compiler you were avoiding |
| Assign into a member during composition | AUI's `mOk = _new<AButton>(...)` | Works, but forces shared ownership |
| Look up by string afterwards | `tree.find("ok_button")` | Stringly typed, fails at runtime, needs a registry (C3) |
| Infer structurally | SwiftUI, Compose | Needs result builders or a compiler plugin |
| Out-parameter in the spec | `.control = ok_button` | Typed, checked, zero cost, no ownership change |

libwinui uses the last one:

```cpp
ui::Control<ui::Button> reset;

window.set_content(ui::VerticalStack{.content = {
    ui::Button{.text = "Reset", .control = reset},
}});

reset->set_enabled(false);
```

`.control` takes a `ui::Control<ui::Button>` handle, so a mismatched declaration is a compile error on the exact line. It changes no ownership: the tree owns the node, and the caller holds the same non-owning observer `StackControl::add()` returns. Most nodes need no handle at all, which is the point: you only name what you address later.

Lists need a different kind of identity: stable across data changes. The study showed that a member pointer (`.key = &Photo::id`) works as a key without reflection. A key only pays off with a container that reports inserts, removes and moves, so libwinui has two: `ui::ForEach` and the views take a `ui::ObservableVector<T>`, which reports each change itself, or, with `.key`, a `Property<std::vector<T>>` whose old and new contents are matched up by key.

### `Property<T>` and Its Two Rules

```cpp
template <typename T>
    requires std::equality_comparable<T>
class Property final : public PropertyBase
{
public:
    const T& get() const noexcept;
    void     set(T value);                 // compares first
    Property& operator=(T value);

    [[nodiscard]] Subscription observe(Callback<void(const T&)> fn) const;
};
```

**The equality gate.** `set()` compares before it assigns. An unchanged value never disturbs anything downstream, so a recomputation that lands on the same result costs nothing and propagation stops there. It is also what makes a dependency cycle terminate instead of recursing. This is why `T` must be equality comparable.

**The subscription is owned by the subscriber.** `observe()` returns a `Subscription` the caller must keep; dropping it unsubscribes. An `Element` keeps its subscriptions through `Element::add_subscription()`. Because the UI tree is single-owner and destroyed hierarchically, subscription lifetime is exactly node lifetime: no leak, no dispatch into freed memory. This works only because of single ownership, and it is precisely what `shared_ptr`-based frameworks give up.

### The Property May Die First

The subscriber-dies-first case is the easy half. The mirror case is real too: a `Property` in a view model owned by a document, observed by a dialog's controls, is destroyed when the document closes while the dialog is still being torn down. A subscription holding a bare back-pointer would then unlink through freed memory in its own destructor.

The obvious fix, a `shared_ptr` control block per property, would cost an atomic per edge and an allocation per property. The free fix is an intrusive doubly-linked list, symmetric at both ends:

```text
Property<T> --head--> Subscription <--> Subscription <--> ...
      ^                     |
      +------- owner_ ------+
```

`~Subscription()` unlinks itself from the list. `~PropertyBase()` walks the list and nulls every `owner_`, after which `alive()` reports `false`. Destroying either end severs the edge. The cost is two pointers per edge, with no allocation, no atomic and no `shared_ptr`. `Subscription` is deliberately not a template, so one `std::vector<Subscription>` on an element can hold bindings to an `int`, a string and a `bool` at once.

The probe ran four cases under AddressSanitizer, all clean:

| Case | Result |
| --- | --- |
| Subscriber destroyed first, then the property is written | No dispatch into freed memory |
| Property destroyed first, then the subscriber is destroyed | `alive()` is false; teardown touches nothing dead |
| Writing an equal value | Zero observer calls |
| Middle subscriber of three destroyed, then a write | The two survivors fire; the list stays intact |

The dispatch loop caches `next` before invoking each handler, because a handler may unsubscribe itself from inside its own callback. That is the classic signal/slot crash. `tests/test_property.cpp` covers these cases in the library, plus self-unsubscription during dispatch and subscriptions moving during vector reallocation.

### `Binding<T>`: A Value or a Stream, Visible in the Source

`.text = state.username` is ambiguous: an initial value, or a continuous synchronization? The API makes the difference visible:

```cpp
ui::Text{.text = "static label"}   // set once
ui::Text{.text = title}            // tracked
ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })}
```

`Binding<T>` converts implicitly from a plain `T` (and from a string literal for `std::string`), so the constant case stays quiet, and from a property or a `bind(...)` result, so the reactive case names its source. It is always a real C++ expression, which gets `x:Bind`'s compile-time checking with no markup compiler. A writable property bound to an editable field (`TextBox::text`, `Slider::value`) is two-way; a `const` property, a one-way conversion and `ui::bind_one_way()` stay one-way, and `ui::bind(p, to, from)` is two-way through a pair of conversions. The rvalue overloads of `bind()` are deleted: a binding keeps a reference to its source, so a temporary `Property` cannot be bound. The free function `bind(p, fn)` is preferred over a chained `bind(p).map(fn)`, which would grow a second DSL beside the spec vocabulary. The [state and bindings tutorial](../../tutorials/basics/03-state-and-bindings.md) shows it in use.

`Property<T>` is also UI-thread only. `set()` is not synchronized, matching the rule that worker threads never mutate the tree directly.

### Where Declarative Construction Breaks

Specs describe a fixed tree. Two situations are not fixed trees, and each gets a named node with an explicit rebuild boundary instead of a global mechanism re-deriving everything on the chance that something moved. You pay for dynamism where you use it, and a reader can see where that is. `ui::Match` lives in `include/winui/ui/spec/Dynamic.hpp`; `ui::ForEach` and the views live beside their controls in `include/winui/ui/collections/`.

**Structure that depends on state**, such as a wizard page or a section that only appears when a check box is on. `ui::Match<T>` owns one subtree and rebuilds it when a `Property` changes. The rebuild costs the subtree, not the screen, and the equality gate makes a redundant write free:

```cpp
enum class Page { Home, Settings };
ui::Property<Page> page{Page::Home};

ui::Match<Page>{
    .value   = page,
    .content = [](const Page& p) -> ui::Spec {
        switch (p)
        {
        case Page::Home:     return ui::Text{.text = "Welcome home"};
        case Page::Settings: return ui::CheckBox{.text = "Dark mode"};
        }
        return {};
    },
}
```

The discipline is to put the `Match` as deep as the varying structure actually goes, not at the root. Note that a rebuild destroys the old subtree, so any `.control` published from inside it dangles afterwards.

**Lists too long to write down.** A view (`ui::ListView`, `ui::TileView`, `ui::TableView`) or a `ui::ForEach` with `.virtualize = true` runs its `.item` factory once per *realized* row, not once per item. That is its quiet advantage over immediate mode, whose loop visits every item, and over diffing, which builds a description of the whole model before anything decides what is visible. The collection is a `ui::ObservableVector` or a vector property the view follows by itself. The [long lists tutorial](../../tutorials/collections/02-virtual-list.md) covers it.

A rebuild is the blunt answer, and for most cases it is the wrong one, because it throws away what the user did inside the subtree. The spec layer has since grown a tool per situation, and each says in its name whether it keeps that state:

| Situation | Tool | Keeps state |
| --- | --- | --- |
| Decided once, while the tree is built | `ui::when()` or a plain `if` | n/a |
| Show or hide, cheap subtree | `.visible = ui::bind(...)` | yes |
| Show or hide, expensive subtree | `ui::Lazy` | yes |
| One of several, state should reset | `ui::Match` | no |
| One of several, state should persist | `ui::Match{.keep = true}` | yes |
| A list you edit in place | `ui::ForEach` over a `ui::ObservableVector` | yes, per item |
| A list you assign as a whole | `ui::ForEach{.items, .key}` | yes, per key |
| A long list | `ui::ForEach{.virtualize = true}` or a view | per realized row |
| State that belongs to one component | `ui::State<S>` | as long as the component |

The rule from "Why Diffing Loses in C++" still holds. Every tool either holds the change the caller already has (a property changed, an item was inserted at 3) or, for the one input that throws that information away, a whole new `std::vector` assigned to a `Property`, compares keys. Trees are never compared, and a rebuild stays scoped to one node. `ui::ObservableVector<T>` lives in `core/`, next to `Property`, and uses the same subscription list, which settles the open question about an observable container and list keys. The [todo list tutorial](../../tutorials/collections/03-a-todo-list.md) uses most of the table.

Two things were left out on purpose. A `ForEach` or a `Lazy` does not splice its nodes into the parent's own child list: every panel would have to track ranges it does not own, and nesting costs one node. And `ui::State` is not a hook: its content is built once, so there are no call-order rules.

### Capture Lifetime

Every sample above writes `[&]`, which is correct in a `main()`-scoped example. In a component factory it dangles:

```cpp
ui::Spec counter_row()               // broken: `local` dies at return
{
    int local = 0;
    return ui::Button{.on_click = [&] { ++local; }};
}
```

There is no language fix, so it is a convention, repeated on every handler field: a handler in a spec returned from a function must capture by value, or capture a reference to state that outlives the window. Components that take `Property<T>&` parameters, model state the caller owns, make the right thing the easy thing.

### How the Patterns Score

Each column is a cost a developer pays on an ordinary day, not a benchmark.

| Pattern | Reads like the tree | No manual sync | Pure C++ toolchain | Errors at compile time | Retained tree (a11y, HWNDs) | Deterministic ownership |
| --- | :---: | :---: | :---: | :---: | :---: | :---: |
| A. Imperative (MFC, WTL) | no | no | yes | yes | yes | yes |
| A′. Macro tables | partly | no | yes | partly | yes | yes |
| B. XAML `{Binding}`, QML | yes | yes | no | no | yes | no |
| C. `x:Bind`, Slint | yes | yes | no | yes | yes | partly |
| D. Cycfi Elements | yes | no | yes | partly | yes | partly |
| E. AUI | yes | yes | yes | yes | yes | no |
| F. Dear ImGui, CIG | partly | yes | yes | yes | no | yes |
| G. SwiftUI, Compose, Xilem | yes | yes | partly | yes | yes | no |
| libwinui: specs, `unique_ptr`, `Property<T>` | yes | yes | yes | yes | yes | yes |

The result is not novel. It is pattern E with C1 solved differently so the ownership column survives, plus commands, token-based styles, accessibility fields and components as functions. The last column is the one that matters for a framework whose identity is a single-owner `unique_ptr` tree, and it is the only one where AUI gives ground.

The test applied to every step of the design: does the source of a screen read like the screen, and does each piece of state appear exactly once?

## The Probes

Twelve self-contained compile probes, built with MSVC 19.51.36256 (VC Tools 14.51.36231, x64) and `/std:c++23preview /EHsc /W4 /permissive-`, plus `/fsanitize=address /Zi` for the lifetime probe. Five must compile and run; seven must fail, and their diagnostics are the evidence.

| Probe | What it establishes | Result |
| --- | --- | --- |
| `probe_positive.cpp` | The whole surface end to end: `SpecLike`, the `mutable` child drain, three spec aggregates, `.control`, nested designated init, aggregate with a base, a deducing-`this` fluent chain, `Spec` returned from a function, `_dip`/`_fr` literals, `constexpr` style composition, the dual-return key handler, `Property<T>` with the equality gate | Compiles clean at `/W4`, runs, correct output |
| `probe3.cpp` | `Binding<T>` from a literal and from `bind(prop, fn)`; a command with `can_execute`; a style field; a list spec keyed by `&Photo::id`; properties before children | Compiles clean, runs, `Clicks: 3` propagated through the binding |
| `probe4.cpp` | The symmetric subscription list: subscriber dies first, property dies first, equality gate, middle subscriber dies | All four assertions pass under AddressSanitizer |
| `p12_mof.cpp` | `std::move_only_function` availability, and a spec holding a `unique_ptr`-capturing handler while staying movable | `__cpp_lib_move_only_function == 202110L`, runs |
| `p13_evolve.cpp` | Inserting a field between two existing spec fields keeps call sites compiling | Compiles clean |
| `n1_mixed.cpp` | Designated fields plus positional children | `C7556` |
| `n2_order.cpp` | Out-of-order designators | `C7560` |
| `n3_variadic.cpp` | A variadic constructor and designated init on one type | `C7562` |
| `n4_ilist.cpp` | A braced list of `unique_ptr` | `C2672`, deleted copy constructor |
| `n5_typo.cpp` | Cost of a misspelled property | `C7559`, one line |
| `n6_badchild.cpp` | Cost of a non-spec child | Four lines, `no conversion from 'int' to 'Child'` |
| `n7_stdfn.cpp` | `std::function` holding a move-only handler | `C2338`, "must be copy constructible" |

The probes verify language and library mechanics. They measure neither performance nor build time across a full set of specs, and they were not compiled against the real `Element`. Those questions were settled later against the library itself, where the semantic golden tests (the recorded render-command stream) are the check that a declared tree and a hand-built one produce the same output.

## Sources

[^dcl-init-aggr]: ISO C++, [dcl.init.aggr], aggregate initialization and designated initializers. See also cppreference, [Aggregate initialization](https://en.cppreference.com/w/cpp/language/aggregate_initialization).
[^knatten]: Anders Schau Knatten, [Why you can't list-initialize containers of non-copyable types](https://blog.knatten.org/2018/10/05/why-you-cant-list-initialize-containers-of-non-copyable-types/).
[^aui]: AUI Framework, [aui-framework/aui](https://github.com/aui-framework/aui), and its note on [retained versus immediate UI](https://aui-framework.github.io/develop/retained_immediate_ui/).
[^elements]: Cycfi Elements, [Design aspects](https://cycfi.github.io/elements/elements/v1.0/aspects.html).
[^elm]: [The Elm Architecture](https://guide.elm-lang.org/architecture/), Elm guide.
[^flutter]: [Flutter architectural overview](https://docs.flutter.dev/resources/architectural-overview), section on widgets, elements and render objects.
[^compose]: Android Developers, [Thinking in Compose](https://developer.android.com/develop/ui/compose/mental-model).
[^xilem]: Raph Levien, [Xilem: an architecture for UI in Rust](https://raphlinus.github.io/rust/gui/2022/05/07/ui-architecture.html).
[^p2996]: WG21 P2996, [Reflection for C++26](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r12.html).
[^signals]: [Beyond the virtual DOM: signals, benchmarks](https://thedatascientist.com/beyond-the-virtual-dom-signals-benchmarks/).
[^swiftui-identity]: [Structural identity in SwiftUI](https://swiftwithmajid.com/2021/12/09/structural-identity-in-swiftui/).

Further reading used by the study:

- Slint, [Alternative to Qt](https://slint.dev/alternative-to-qt), on compiled bindings versus QML.
- Sandor Dargo, [Deducing this](https://www.sandordargo.com/blog/2022/02/16/deducing-this-cpp23) (C++23).
- [SwiftUI is one graph](https://aleahim.com/blog/swiftui-is-one-graph/).
- Microsoft, `Windows-appsample-photo-editor` (`Photo.idl`, `Photo.h`, `Photo.cpp`, `MainPage.xaml`) for the C++/WinRT view-model cost.
- WTL `atlcrack.h` and the `BmpView` sample's `mainfrm.h` for message-map tables.
- CIG, a C immediate-mode GUI library, `README.md`, for the immediate-mode motivation.
