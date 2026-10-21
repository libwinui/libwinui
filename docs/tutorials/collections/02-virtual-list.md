# Long Lists

One hundred thousand rows with about a screenful of them alive.

The example for this page builds as `tutorial_08_virtual_list`.

![A list of 100,000 orders with buttons to add, remove and scroll](../../assets/screenshots/tutorial-08-virtual-list-light.png#only-light){ .screenshot width="640" }
![A list of 100,000 orders with buttons to add, remove and scroll](../../assets/screenshots/tutorial-08-virtual-list-dark.png#only-dark){ .screenshot width="640" }

This page builds a window with 100,000 orders in it, a few buttons that change the model, and a line that reports how many rows actually exist at the moment. The full source is `examples/tutorials/08_virtual_list/main.cpp`.

## Why Virtualize

A row of three `TextControl`s in a `StackControl` is four elements. Building one row per order would mean 400,000 elements, every one of them allocated, measured and arranged, for a window that shows about sixteen of them.

A virtualizing list does the opposite. It knows how many items there are and roughly where each one sits, so it can work out which of them fall into the viewport without touching the others. Only those get a row. Scroll down and the rows that left the view are destroyed, the new ones built. The number of live rows depends on the window height, not on the item count.

The views do this on their own. `ui::ListView`, `ui::TileView` and `ui::TableView` are always virtualized. `ui::ForEach` builds everything by default and virtualizes with `.virtualize = true`, inside a `ui::Scroll`.

## A ListView Over 100,000 Orders

The model is an ordinary struct in a `ui::ObservableVector`, the collection that reports its own edits:

```cpp
--8<-- "examples/tutorials/08_virtual_list/main.cpp:model"
```

A row is a function from one item to a `ui::Spec`, the same shape as any component:

```cpp
--8<-- "examples/tutorials/08_virtual_list/main.cpp:row"
```

And the list ties the two together:

```cpp
--8<-- "examples/tutorials/08_virtual_list/main.cpp:state"
```

```cpp
--8<-- "examples/tutorials/08_virtual_list/main.cpp:list"
```

The three fields that matter:

| Field | Meaning |
| --- | --- |
| `.items` | The collection. The view keeps a reference to `orders`, so it must outlive the window. Declare it before the window, as here. |
| `.item` | Builds the row for one item. Stored on the list for its whole life, so it must not capture anything that dies before the window. |
| `.item_height` | A fixed row height in DIPs. Left out, each row is as tall as its content, and the list estimates the rows it has not built yet. With 100,000 rows of the same height, saying so makes the scroll bar exact. |

`ui::ListView<Order>` builds a `ui::ListViewControl`, and `.control` hands you that control.

## Row Realization

The list builds the rows that overlap its viewport, plus about a viewport's worth on either side so that a small scroll has its rows ready. With 36 DIP rows and the example's default window size that is around fifty rows. A taller window realizes more, and 100,000 more orders change nothing. The "Count live rows" button reads `realized_count()` off the list's repeater to show it.

Two things follow:

- `.item` has to be cheap. It runs for every row that comes into view.
- A row keeps no state. A `TextBox` typed into and then scrolled away comes back empty. State belongs in the model, and the row shows it.

Native controls make poor rows. A hosted `HWND` created and destroyed while scrolling is far too slow, see [Hosting Native Controls](../extending-the-tree/02-hosting-native-controls.md).

## Changing the Collection

There is nothing to announce. The buttons edit the `ObservableVector`, and the list is told what was appended or removed:

```cpp
--8<-- "examples/tutorials/08_virtual_list/main.cpp:count"
```

The selection, the scroll position and the rows that are still valid stay as they are. A model you assign as a whole, a `ui::Property<std::vector<Order>>`, works the same way when it is given to `.items` with a `.key`; [Lists](01-lists.md) and [A Todo List](03-a-todo-list.md) cover both.

The Top and End buttons scroll through `.control`: `scroll_into_view(0)` goes to the top and `scroll_into_view(orders.size() - 1)` to the end.

```cpp
--8<-- "examples/tutorials/08_virtual_list/main.cpp:layout"
```

The list sits in a `ui::Fill`, so it gets the height the buttons and the text above it leave and scrolls inside it. Put straight into the stack it would be measured as tall as all its rows, and never scroll.

## Measuring It

`examples/virtual_list_demo` is the stress version: 100,000 rows, each a `Button` counting its own clicks. Scroll it with the wheel as fast as you like. The window never holds more than a few screenfuls of buttons. The click counts live in a `std::vector<int>` beside the list, because a row that scrolls out takes any state it kept with it.

[Performance](../../background/performance.md#lists) has the measured cost of scrolling each list with up to a million items.

`ui::VirtualListControl` is the simplest of the engines: rows of one fixed height, an index and a factory, no selection. `ui::TableView` uses it for its body. It has no spec. Build it by hand if a list of yours needs nothing else, see [Mixing Imperative and Declarative](../extending-the-tree/03-mixing-imperative-and-declarative.md).
