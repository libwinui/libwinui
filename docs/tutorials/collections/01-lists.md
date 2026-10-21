# Lists

`ListView`, `TableView` and `TreeView` show collections of data.

The example for this page builds as `tutorial_07_lists`.

![A ListView, a TableView and a TreeView side by side](../../assets/screenshots/tutorial-07-lists-light.png#only-light){ .screenshot width="1100" }
![A ListView, a TableView and a TreeView side by side](../../assets/screenshots/tutorial-07-lists-dark.png#only-dark){ .screenshot width="1100" }

The example for this page puts all three next to each other and writes every event they raise into a status line at the bottom, newest first. The full source is `examples/tutorials/07_lists/main.cpp`.

```cpp
--8<-- "examples/tutorials/07_lists/main.cpp:includes"
```

## ListView

A `ListView` shows one row per item, scrolls on its own and keeps a selection. It is typed on its item: `.items` is the collection, `.item` says how one row looks, and the handlers receive the item they are about.

### Strings

The short form is a list of strings in `.items`, with no template at all:

```cpp
--8<-- "examples/tutorials/07_lists/main.cpp:folders"
```

`ui::ListView<std::string>` names the item type, since a braced list of literals does not say it. The list copies the strings and owns them from then on. Nothing is selected until the user picks a row or `.selected_index` names one, so a list that opens with its first row selected sets `.selected_index = 0`. The field takes a `bind()` too if the selection should follow a `Property<std::optional<std::size_t>>`. Unset (`std::nullopt`) means "nothing selected", which is why the property is optional.

Give a list a height, or put it somewhere that gives it one (a `1_fr` grid row, a window). A list in a `ui::VerticalStack` with no height is measured with unbounded height and grows to fit every row, which defeats the point.

To change a string list later, keep an observer through `.control` and use the item calls on it: `add_item()`, `insert_item()`, `remove_item()` and `move_item()`. The selection stays on the items it was on.

### Items and a Template

For anything richer than one line of text, give the list your items and a function that builds the row for one of them:

```cpp
--8<-- "examples/tutorials/07_lists/main.cpp:model"
```

```cpp
--8<-- "examples/tutorials/07_lists/main.cpp:people"
```

`.item` returns a `ui::Spec`, so a row is written with the same specs as the rest of the window. Here `.items` is `contacts`, a `ui::Property<std::vector<Contact>>`, and the spec names its item type, `ui::ListView<Contact>`. C++ works out a spec's type only from the field's own type, which a Property is not, so a view always names it. `.items` takes three sources:

| Source | The list |
| --- | --- |
| A `ui::Property<std::vector<T>>` | Follows the property. With `.key`, a sort or a filter keeps the selection on its item. |
| A `ui::ObservableVector<T>` | Follows every insert, removal and move, see [A Todo List](03-a-todo-list.md). |
| A plain `std::vector<T>` or a braced list | Copies it once. |

`.key` says what makes two contacts the same contact when the vector is replaced, here the name. Leave it out and the list still follows the property, but a reorder then looks like a new set of rows and the selection goes.

The template does not run once per item. It runs for the rows that are on screen, when they come into view, and again when a row scrolls out and back in. So it has to be cheap and it must not keep state in the row. A row can be destroyed and rebuilt at any time, and whatever you keep there is gone. Keep state in your model and derive the row from it.

Rows size to their content unless `.item_height` fixes them. The minimum is 40 DIPs.

`.selection_mode` picks the selection model:

| Mode | Gesture |
| --- | --- |
| `None` | Nothing can be selected. Use it with `.on_item_click` for a list of actions. |
| `Single` | A click selects one item. The default. |
| `Multiple` | Each row gets a check box; a click toggles it. |
| `Extended` | A click selects one, Ctrl+click toggles, Shift+click selects a range. |

The list is one tab stop, with the focus ring on the current item. The arrow keys move that item. In `Extended` mode the selection moves with it (Shift extends, Ctrl moves focus only), and in `Single` mode too, as long as `single_selection_follows_focus()` is on. Ctrl+A selects everything in `Multiple` and `Extended`, or clears the selection when everything already is selected.

When the model changes, there is nothing to announce. Assign the property, or edit the `ObservableVector`, and the list follows, keeps the selection on the right items and plays the item transitions if you set any. With drag reordering on (`.can_reorder_items`), the list writes the new order back into the bound collection.

The control itself stays index-based. Through `.control`, an item is its position: `selected_index()`, `scroll_into_view(i)`. If you drive a control without a spec, `set_item_count()` and the `notify_inserted()`, `notify_removed()`, `notify_replaced()` and `notify_moved()` calls are what the spec uses underneath.

`ui::ListView` also files its items under group headers by a key (`.group_by`, `.group_header_template`, sticky by default), has a header and footer that scroll with the items, and drag reordering (`.can_reorder_items`). `ui::TileView` and `ui::TableView` group the same way. The [ListView reference](../../reference/controls/list-view.md) lists them all.

## TableView

A `TableView` is a table: a header strip over rows of cells, which the user can sort, resize, group and, if you allow it, edit.

```cpp
--8<-- "examples/tutorials/07_lists/main.cpp:grid"
```

`.items` is the same collection the list shows, one table row per item. `.columns` gives each column a caption, a width and a function returning the cell text for an item. The user can sort by clicking a header and resize columns by dragging their edges, and neither changes your collection. The body is virtualized like a `ListView`. Columns can also hold templated cells, rows can be edited in place and grouped under collapsible headers, all described in the [TableView reference](../../reference/controls/table-view.md).

### Columns That Come From Data

`.columns` is read once, when the table is built. When the columns themselves come from data, the result of a query or the questions of a form, put the table in a `ui::Match` keyed on what the columns are made from:

```cpp
ui::Match<std::vector<Column>>{
    .value   = ui::bind(columns),
    .content = [&](const std::vector<Column>& now) -> ui::Spec { return answers_table(now); },
}
```

The table is rebuilt only when the column list compares unequal, and edits to the rows go through `.items` as usual. [A Form Built From Data](04-a-form-built-from-data.md) builds such a table.

## TreeView

A `TreeView` shows a hierarchy of expandable rows. Like the other views it shows your data, and your data is the model. Here the items are `ui::TreeNode`, the ready-made item, held in a property:

```cpp
--8<-- "examples/tutorials/07_lists/main.cpp:tree"
```

A `TreeNode` has a `text`, its `children` (by value), a `key`, whether it starts `expanded`, and `has_children`. The tree draws the rows itself: indentation, chevron, selection indicator. There is one element for the whole tree, not one per node.

What the tree keeps is only what the user did: which rows are expanded and which are selected, by each node's key. A node without a key is known by the path of texts from its root. Assign the property a changed tree, a reload or a rename, and the rows with the same keys stay open and selected.

`has_children` shows a chevron on a node that has no children yet. When the user expands it, `.on_expanding` runs first. Load the children into the property there, as the example does, and the row opens with them. That is how you load a folder's content only when someone opens it. If the node still has no children afterwards, it stays collapsed.

Your own item type works the same way. Name it, say where its children are and what identifies it, and give it a row:

```cpp
ui::TreeView<Folder>{
    .items        = folders,              // ui::Property<std::vector<Folder>>
    .children     = &Folder::subfolders,  // or a function returning them
    .key          = &Folder::path,
    .has_children = &Folder::has_subfolders,
    .on_expanding = [&](const Folder& f) { load_into(folders, f.path); },
    .item         = [](const Folder& f) -> ui::Spec { return ui::Text{.text = f.name}; },
}
```

Its handlers get your items: `on_selection_changed` a copy of the selected one, `on_item_invoked` and the others the item itself.

Two things to keep in mind:

- The tree is not virtualized. Every visible row is realized, so keep very large trees collapsed and load them lazily.
- Drag and drop is off by default. Turn on `.can_drag_items` and `.allow_drop` when the hierarchy is the user's to rearrange: a row dragged onto another row becomes its last child, and `.on_drag_items_completed` reports the move. With a member `.children` and a bound property, the moved tree is written back into the property.

The tree is a single tab stop. Up and Down move between rows, Left and Right collapse, expand or step to the parent and first child, Enter invokes.

All four controls go into one grid. The two lists share the first column, the string list in an `AutoSize` row, the contacts in the `1_fr` row below it, which gives them their height:

```cpp
--8<-- "examples/tutorials/07_lists/main.cpp:layout"
```

## Selection and Item Callbacks

Run the example and click around while watching the status line. The item and selection handlers per control, of which the example logs the selection, click, invoke and expanding ones:

| Control | Handler | Fires when |
| --- | --- | --- |
| `ListView` | `.on_item_click` | A row is clicked, before the click selects. |
| `ListView` | `.on_selection_changed` | The selection actually changed: a click, the keyboard or `set_selected_index()`. Clicking the row that is already selected raises nothing, and so does a write to a bound `selected_index`. |
| `TableView` | `.on_selection_changed` | The selected row changed, by click or keys. A write to a bound `selected_row` doesn't run it. |
| `TableView` | `.on_cell_edited` | An edit is being committed. Returning `ui::Cancel` rejects the value. |
| `TreeView` | `.on_item_click` | A row is clicked, before the click selects. |
| `TreeView` | `.on_item_invoked` | A row is opened: double-clicked, or Enter is pressed on it. |
| `TreeView` | `.on_selection_changed` | A node's selection changed: a click, the arrow keys or a check box. |
| `TreeView` | `.on_expanding` | A node is about to open. |
| `TreeView` | `.on_collapsed` | A node has closed. |

So a click on an unselected contact logs `ItemClick` first and `SelectionChanged` right after it, and a second click on the same contact logs only `ItemClick`. Use `on_item_invoked` for "open this": it fires on Enter and a double-click, and on a single click in a view that selects nothing (`SelectionMode::None`). Use `on_selection_changed` for "show the details of whatever is selected", since it also follows the keyboard, and `on_item_click` for anything that reacts to every click.

## Item Data Lifetime

A binding keeps a reference to its property, and the list reads through it for as long as it exists, not only while the window is being set up. So the property has to outlive the window:

```cpp
--8<-- "examples/tutorials/07_lists/main.cpp:state"
```

Here that holds because both are locals of `main()`, declared before the window, so the window and its tree are destroyed first when `main()` returns. In a larger app the model usually lives in an object that owns the window, or next to it, and the same rule applies: declare the data first, so it is destroyed last.

Who owns what a view is given:

| Source | Who owns it after build |
| --- | --- |
| `.items` given a vector or a braced list | The view, which copies it. |
| `.items` given a `ui::Property` or a `ui::ObservableVector` | You. The property or collection must outlive the view, or be destroyed first, which just ends the binding. |
| `.item` and the handlers | The view stores them, so they must not capture anything that dies before the window. The item reference they receive is only valid during the call, unless the source is an `ObservableVector`. |

## Other Item Controls

The same ideas carry over to the rest of the collection family:

| Control | Use it for |
| --- | --- |
| [`ui::TileView`](../../reference/controls/tile-view.md) | Templated items in wrapping rows of equal cells, such as a photo grid. Same selection, click and drag fields as `ListView`. |
| [`ui::ForEach`](../../reference/controls/for-each.md) | Turns a collection into children arranged by its `.layout`, with no selection or chrome of its own, virtualized with `.virtualize = true`. The building block when none of the above fit. |
| [Item Layouts](../../reference/controls/item-layouts.md) | The arrangements a `.layout` field takes: `ui::StackLayout`, `ui::TileLayout`, `ui::WrapLayout` and `ui::LinedFlowLayout`. A `ui::LayoutPanel` takes them too, for children you write. |

[Long Lists](02-virtual-list.md) looks at what virtualization buys with 100,000 rows.
