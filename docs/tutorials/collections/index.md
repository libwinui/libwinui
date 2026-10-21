# Collections

Lists, grids and trees, from a handful of rows to a hundred thousand.

The controls in this track take their content as data rather than as a braced list of children. You hand over a vector of strings, a collection of your own items plus a template, or a tree of plain `TreeNode` values, and the control decides which rows to build. That is why a list of 100,000 entries costs about as much as a list of twenty.

Read [State and Bindings](../basics/03-state-and-bindings.md) first. Every page here uses `Property<T>` and `bind()`.

## Four Words

| Word | What it is | Examples |
| --- | --- | --- |
| View | A finished control around data. It takes `.items` and an `.item` template and adds selection, scrolling and keyboard navigation. | `ui::ListView`, `ui::TileView`, `ui::TreeView`, `ui::TableView` |
| `ForEach` | Turns a collection into children, with no look or selection of its own. | `ui::ForEach` |
| Layout | A value that says how items are arranged, given in a `.layout` field. `ui::ForEach`, `ui::TileView` and `ui::LayoutPanel` take one. | `ui::StackLayout`, `ui::TileLayout`, `ui::WrapLayout`, `ui::LinedFlowLayout` |
| Panel | Arranges the children you write in the spec yourself. | `ui::VerticalStack`, `ui::Grid`, `ui::TilePanel`, `ui::LayoutPanel` |

The end of a name says which one it is. `ui::TilePanel` places children you wrote, `ui::TileLayout` is how a `ForEach` or a `TileView` arranges its items, and `ui::TileView` is the finished control. Only these four views end in `View`. Panels are in the [Layout](../../reference/layout/index.md) reference, everything else here in [Collections](../../reference/controls/index.md#collections).

## The Track

| Page | What you build | Project |
| --- | --- | --- |
| [01 - Lists](01-lists.md) | A `ListView` of strings, a templated `ListView`, a `TableView` and a `TreeView` side by side, with a status line that shows which events fire and in what order | `examples/tutorials/07_lists/` |
| [02 - Long Lists](02-virtual-list.md) | 100,000 orders in a `ui::ListView`, buttons that grow and shrink the collection, and a readout of how many rows are alive | `examples/tutorials/08_virtual_list/` |
| [03 - A Todo List](03-a-todo-list.md) | A list you add to, filter and remove from, with rows that edit themselves and pages that keep what you typed: `ui::ObservableVector`, `ui::ForEach`, `.visible`, `ui::State`, `ui::Match{.keep}` and `ui::Lazy` | `examples/tutorials/14_todo_list/` |
| [04 - A Form Built From Data](04-a-form-built-from-data.md) | A form whose questions you add, retype and reorder, with a live preview and an answers table that follow them: `ui::Match` keyed on data, `ui::bind(prop, to, from)`, `ui::bind_choice()` over a list that changes and `ui::bind_each()` | `examples/tutorials/16_form_from_data/` |

The projects are built with the rest of the examples. After `cmake --build build --config Debug`, run `tutorial_07_lists.exe`, `tutorial_08_virtual_list.exe`, `tutorial_14_todo_list.exe` or `tutorial_16_form_from_data.exe` from `build/bin/Debug/`.
