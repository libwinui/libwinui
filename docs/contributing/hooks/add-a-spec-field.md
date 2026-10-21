# Add a Spec Field

Grow an aggregate without breaking a call site.

The usual case: a control gained a setter (say `RatingControl::set_read_only()`), and its spec should expose it. Three files change:

| Step | File |
| --- | --- |
| 1. The field | the control's header, `include/winui/ui/<group>/<Spec>.hpp` |
| 2. Wiring it in `build()` | the control's source, `src/ui/<group>/<Spec>.cpp` |
| 3. The test | `tests/test_spec.cpp` |

The reference page picks up the new field and its doc comment on the next generator run; nothing to add to `control_map.toml`.

## Append or Insert, Never Reorder

C++ requires designated initializers in member declaration order. A call site that names `.value` and then `.caption` compiles only while `value` is declared before `caption`. So the order of fields in a spec is part of its public API:

- **Adding** a field anywhere in its block is safe. Existing call sites skip over it.
- **Reordering**, renaming or removing a field breaks every call site that named it, or named two fields around it.

Put the new field into the block it belongs to, usually at the end of the properties:

```cpp
struct Rating
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    using control_type = RatingControl;

    // ---- properties ----
    Binding<std::string>               header{};
    TwoWayField<std::optional<double>> value{};
    Binding<std::size_t>               max_rating{5};
    // ...
    Binding<std::string> caption{};
    /// Ignores input but keeps the enabled look and the tab stop; see
    /// ui::RatingControl::set_read_only.
    Binding<bool> read_only{false}; // new

    // ---- appearance / layout ----
    Style style{};
    // ... the common fields, then tool_tip, then enabled, then accessibility ...

    // ---- behavior ----
    Callback<void(std::optional<double>)> on_value_changed{};
    // ... the generic pointer, focus and key events ...

    // ---- identity ----
    ControlOut<Rating> control{};

    [[nodiscard]] std::unique_ptr<Element> build() &&;
};
```

The blocks are fixed: properties, then appearance and layout (the common fields, `tool_tip`, `enabled` and `accessibility`), then behavior, then `control`, then children and template callables. The behavior block starts with `command` and `command_parameter`, then the control's main event, its other change events, and the generic pointer, focus and context events last. A new handler goes at the end of its group. Only template callables go after the children field.

Give the field a `///` comment. Say anything a caller would otherwise have to look up, such as a sentinel value or "fixed at build time". Keep the WinUI default as the field's default, so leaving it out behaves like WinUI.

## Values Before Handlers

In `build()`, apply the new field with the other values, before any handler is installed:

```cpp
std::unique_ptr<ui::Element> Rating::build() &&
{
    auto node = std::make_unique<ui::RatingControl>(max_rating.initial(), value.initial());

    const auto write = shared_writer(value);
    apply(value, *node, [raw = node.get()](const std::optional<double>& v) { raw->set_value(v); });
    apply(caption, *node, [raw = node.get()](const std::string& v) { raw->set_caption(v); });
    apply(read_only, *node, [raw = node.get()](const bool& v) { raw->set_read_only(v); }); // new

    if (auto changed = write_back(write, std::move(on_value_changed)))
    {
        node->on_value_changed(std::move(changed));
    }

    apply_common(*this, *node);
    publish_control(control, node.get());
    return node;
}
```

The order matters. Seeding a control with its initial value goes through the same setter a user action would, and if a change handler were already installed it would fire for a change the caller never made. Installing handlers last means a handler only runs for changes that happen after the control exists.

## Binding or Plain Value

Make the field a `ui::Binding<T>` whenever the control has a setter for it: text, enabled, a selected value, a severity, a color, a count. `Binding<T>` converts implicitly from a plain `T` (and from a braced list of `T`'s constructor arguments), so `.read_only = true` still works, and `.read_only = bind(locked)` keeps the control in sync with a `Property<bool>`. A value the constructor needs is passed as `field.initial()` and then applied like any other.

Keep it a plain `T` only for what has no setter or no meaning after build:

- the control, the children and the handlers,
- template callables (`item`, `cell`),
- a value the control can only take at construction,
- a move-only value such as a `LayoutSpec`.

A field the user edits is two-way: `ui::bind(prop)` on it writes the user's value back into the property. Declare it as a `ui::TwoWayField<T>` rather than a `Binding<T>`, so a two-way bind of a property that cannot hold the field's values fails to compile instead of quietly going one-way. The spec pairs the field with an `on_*_changed` handler, and `build()` installs the control's change notification through `ui::write_back(field, std::move(on_x_changed))`, which writes the binding first and then runs the handler, unless the change came from a binding: a bound value reaching the control, or something it caused, never runs it. For an open/closed state whose handlers carry no value, use `ui::shared_writer()` with `ui::write_back_value()`. A display field does neither and its binding stays one-way. The `Property` equality gate stops the round trip after one round.

For an optional value ("unset keeps the theme's"), use `std::optional<T>` or `Binding<std::optional<T>>`, and check it in `build()` before calling the setter.

## Test

Add a case to `tests/test_spec.cpp`, tagged `[spec]` and `[binding]` for a bindable field. Build the spec with a `bind(...)` and a `.control`, check the initial value arrived, change the property and check the control followed. This is the real test for `read_only`:

```cpp
TEST_CASE("ui::Rating's read_only binds through to the RatingControl", "[spec][binding]")
{
    Property<bool>           read_only{true};
    ui::Control<ui::Rating>  rating;
    std::unique_ptr<Element> tree = ui::build(ui::Rating{.read_only = bind(read_only), .control = rating});
    CHECK(rating->read_only());
    read_only = false;
    CHECK_FALSE(rating->read_only());
}
```

If the field sits next to a handler, also check that building with an initial value does not call the handler. The control's own behavior for the property belongs in the control's test file, not here.
