// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/VirtualListControl.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/scrolling/ScrollBarOverlay.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/ItemsModel.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <compare>
#include <cstddef>

namespace ui
{

class TextBoxControl;

/// Which grid lines a TableView draws. Row banding is separate (set_alternating_rows()).
enum class TableViewGridLinesVisibility
{
    All,        ///< Row separators and column separators (the default).
    Horizontal, ///< Row separators only.
    None,       ///< No grid lines.
    Vertical,   ///< Column separators only.
};

/// The direction a TableView column is sorted in.
enum class TableViewSortDirection
{
    None,       ///< Not sorted; the rows keep their source order.
    Ascending,  ///< Smallest first; the header shows an up chevron.
    Descending, ///< Largest first; the header shows a down chevron.
};

/// What `on_sorted` gets after a sort change.
struct TableViewSortedArgs
{
    std::optional<std::size_t> column{};                                 ///< The sorted column; unset when cleared.
    TableViewSortDirection     direction = TableViewSortDirection::None; ///< Its new direction.
};

/// What `on_sorting` gets before a user sort: the column and the direction it is about to take.
/// Return ui::Cancel from the handler to keep the current order.
struct TableViewSortingArgs
{
    std::size_t            column    = 0;                            ///< The column about to be sorted.
    TableViewSortDirection direction = TableViewSortDirection::None; ///< The direction it is about to take.
};

/// What `on_column_resized` gets on every step of a user resize.
struct TableViewColumnResizedArgs
{
    std::size_t column = 0;    ///< The resized column.
    float       width  = 0.0f; ///< Its new width, DIPs.
};

/// A TableViewControl's `on_beginning_edit`: the cell an editor is about to open on.
struct TableViewCellArgs
{
    std::size_t row    = 0; ///< An index into rows().
    std::size_t column = 0; ///< The column.
};

/// A TableViewControl's `on_cell_edited`: the cell and the editor's text, which the handler may
/// rewrite to what the cell should show.
struct TableViewCellEditArgs
{
    std::size_t  row;    ///< An index into rows().
    std::size_t  column; ///< The column.
    std::string& text;   ///< The editor's text.
};

/// A TableView spec's `on_beginning_edit`: the item and the column an editor is about to open on.
template <typename T>
struct TableViewBeginningEditArgs
{
    T&          item;   ///< The row's item.
    std::size_t column; ///< The column.
};

/// A TableView spec's `on_cell_edited`: the item, the column and the editor's text.
template <typename T>
struct TableViewCellEditedArgs
{
    T&                 item;   ///< The row's item (the edited copy for an `editing_cell` column).
    std::size_t        column; ///< The column.
    const std::string& text;   ///< The editor's text.
};

/// The directions repeated clicks on a column header step through. An unsorted column starts at
/// the first one named. The `*None` cycles end on unsorted, so a third click restores the source
/// order.
enum class TableViewSortCycle
{
    AscendingDescending,     ///< Ascending, Descending, Ascending, and so on (the default).
    AscendingDescendingNone, ///< Ascending, Descending, None.
    DescendingAscending,     ///< Descending, Ascending, Descending, and so on. Suits metrics, largest first.
    DescendingAscendingNone, ///< Descending, Ascending, None.
};

/// A TableView column's width, in the same units as a ui::Grid track:
///
/// - Fixed DIPs: a plain number, or `120_dip` in a spec.
/// - Auto: `ui::GridLength::auto_()`, or `ui::AutoSize` in a spec. As wide as its header and the
///   cells in view.
/// - A fraction: `ui::GridLength::fraction()`, or `1_fr` in a spec. A weighted share of what the
///   fixed and Auto columns leave of the viewport.
///
/// Converts implicitly from a number and from a ui::GridLength, so `{"Name", 140.0f}` and
/// `{"Name", 1_fr}` both name a column.
struct TableViewColumnWidth : GridLength
{
    /// A fixed width of `dips`.
    constexpr TableViewColumnWidth(float dips) // NOLINT(google-explicit-constructor): a number is a width
        :
        GridLength{GridUnit::Dip, dips}
    {
    }
    /// Auto, a fraction or fixed DIPs, as `length` says.
    constexpr TableViewColumnWidth(GridLength length) // NOLINT(google-explicit-constructor)
        :
        GridLength{length}
    {
    }
};

/// One column of a TableView: its header, its width and how the user may size, sort and pin it.
struct TableViewColumn
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// Caption drawn in the header strip above the rows. A caption too wide for the column is
    /// trimmed with an ellipsis, like the cells.
    std::string header;
    /// Fixed DIPs including the cell padding, Auto or a fraction; see TableViewColumnWidth. Cell
    /// text past the resolved width is trimmed with a trailing ellipsis. A user resize makes the
    /// column fixed at the width it was dragged to, and fixes the fraction columns before it at the
    /// widths they had, so only the columns after it give or take the room.
    TableViewColumnWidth width = 100.0f;
    /// The smallest and largest width, DIPs. The resolved width and a user resize stay inside them.
    float min_width = 20.0f;
    float max_width = Unbounded; ///< See `min_width`.
    /// Whether the user can drag this column's header divider. The table's
    /// can_user_resize_columns() must be on too.
    bool can_resize = true;
    /// Whether a click on this header sorts by it. The table's can_user_sort_columns() must be on
    /// too. sort_by_column() works either way.
    bool can_sort = true;
    /// The directions repeated header clicks step through.
    TableViewSortCycle sort_cycle = TableViewSortCycle::AscendingDescending;
    /// Whether the column stays put while the others scroll sideways. Only a leading run of frozen
    /// columns is pinned. A frozen column after an unfrozen one scrolls.
    bool frozen = false;
    /// Whether the column stays put against the right edge while the others scroll sideways. Only
    /// a trailing run of such columns is pinned, and `frozen` wins on a column that sets both.
    bool frozen_trailing = false;
    /// Whether the column's cells are never edited, even in a table that allows editing
    /// (TableViewControl::set_read_only(false)). A read-only column can still be the current one.
    bool read_only = false;
};

class TableViewHeaderAutomation; // TableView.cpp: the header strip as screen readers see it
template <typename T>
struct TableView;

/// A table: a header strip over virtualized rows, with sorting, resizable columns, frozen leading
/// and trailing columns, four selection modes, cell templates, cell editing and collapsible groups.
///
/// Only the rows in view are realized, so a grid of 100,000 rows costs what a screenful does.
/// Columns are fixed, Auto or fraction-sized (TableViewColumnWidth). When they are wider than the
/// viewport, the table scrolls sideways with Shift+wheel, a horizontal wheel, Left and Right, or
/// the horizontal scroll bar. Leading and trailing frozen columns stay in place. A click on a
/// header sorts by that column through its TableViewSortCycle and draws a chevron. Sorting
/// reorders the table's own view of the rows, never rows(), and the selection stays on the same
/// rows. Dragging a header's trailing divider resizes the column between its min and max width.
///
/// A cell shows its text in rows() unless set_cell_factory() builds an Element for it, such as a
/// check box, a progress bar or an icon beside a caption. Those are built only for the rows in
/// view and kept while their row stays in view or holds focus. They are children of the table, so
/// they take pointer input and focus and screen readers see them. An Auto column measures them
/// too, and the column still sorts by its text.
///
/// Editing is off until set_read_only(false). Then F2, or a double-click on a cell, opens an
/// editor in the current cell. That is a TextBox over a text cell, or what
/// set_cell_editor_factory() builds, which is the only way to edit a template cell. Enter
/// commits and Escape cancels. A press elsewhere in the table, or focus leaving it, commits.
/// on_cell_edited() sees every commit and can reject it, which keeps the editor open. While
/// editing is possible, Left and Right move the current column instead of scrolling sideways, and
/// scrolling follows it. The current column holds the cell F2 edits, ringed when focused.
///
/// set_group_keys() files the rows under collapsible group header rows, the groups in the order
/// their keys first appear. A sort orders the rows within each group. A header row toggles on a
/// click, Space or Enter. Left collapses it and Right expands it. Selection never lands on a
/// header row, and it stays put while the columns scroll sideways.
///
/// The grid itself is the single tab stop. Up, Down, PageUp, PageDown, Home and End move through
/// the rows and group headers in view order and scroll the row into view. Space selects in Single
/// and Extended mode and toggles in Multiple mode. Shift extends a range in Multiple and Extended
/// mode, and Ctrl moves without selecting there. Row indices everywhere in this API
/// (selected_row(), selected_rows(), on_selection_changed(), on_cell_edited()) are indices into
/// rows(), whatever the sort or grouping. Up from the first row moves onto the column headers.
/// There Left and Right move between them, Enter or Space sorts, Ctrl+Left and Ctrl+Right narrow
/// and widen the column, and Down goes back to the rows.
///
/// Example:
/// @code
/// // Declarative: ui::TableView builds a ui::TableViewControl, one row per item.
/// using ui::operator""_fr;
/// struct Person { std::string name; std::string role; int years = 0; };
/// ui::Property<std::string> status{"Nothing selected"};
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::TableView<Person>{
///             .items   = {{"Ada Lovelace", "Engineer", 5},
///                         {"Grace Hopper", "Architect", 12},
///                         {"Alan Turing", "Researcher", 8}},
///             .columns = {{.header = "Name", .width = 1_fr, .text = [](const Person& p) { return p.name; }},
///                         {.header = "Role", .width = 160.0f, .text = [](const Person& p) { return p.role; }},
///                         {.header  = "Years",
///                          .width   = 80.0f,
///                          .text    = [](const Person& p) { return std::to_string(p.years); },
///                          .compare = [](const Person& a, const Person& b) { return a.years <=> b.years; }}},
///             .height  = 240,
///             .on_selection_changed = [&status](std::optional<Person> p)
///             { status = p ? p->name + " selected" : "Nothing selected"; },
///         },
///         ui::Text{.text = status},
///     },
/// });
///
/// // Imperative: the same control built by hand.
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto grid  = std::make_unique<ui::TableViewControl>();
/// grid->set_columns({{.header = "Name", .width = 100.0f}, {.header = "Age", .width = 60.0f}});
/// grid->set_rows({{"Ada", "36"}, {"Grace", "85"}});
/// grid->on_selection_changed([&status](std::optional<std::size_t> row)
///                            { status = row ? std::format("Row {}", *row) : ""; });
/// grid->set_selected_row(1); // fires the handler, scrolls row 1 into view
/// grid->sort_by_column(1, ui::TableViewSortDirection::Descending);
/// grid->set_read_only(false); // F2 or a double-click edits a cell
/// grid->on_cell_edited([](const ui::TableViewCellEditArgs& args)
///                      { return args.text.empty() ? ui::Cancel : ui::Proceed; });
/// grid->set_group_keys({"Staff", "Guests"}); // one key per row
/// stack->add(std::move(grid));
/// @endcode
class TableViewControl final : public Element, private SelectionProvider, private GridProvider, private TableProvider
{
public:
    /// Orders two rows, by their indices into rows(), for the column being sorted.
    using SortComparer = Callback<std::weak_ordering(std::size_t column, std::size_t row_a, std::size_t row_b)>;

    /// An empty grid: no columns, no rows, nothing selected, 40 DIP rows.
    TableViewControl();
    ~TableViewControl() override;

    /// Not movable or copyable.
    TableViewControl(const TableViewControl&)            = delete;
    TableViewControl& operator=(const TableViewControl&) = delete;
    TableViewControl(TableViewControl&&)                 = delete;
    TableViewControl& operator=(TableViewControl&&)      = delete;

    // ---- content ----
    /// The columns, left to right. A row's cell `i` is drawn under column `i`.
    [[nodiscard]] const std::vector<TableViewColumn>& columns() const
    {
        return columns_;
    }
    /// Replaces every column, clears the sort, and lays the table out again.
    void set_columns(std::vector<TableViewColumn> columns);

    /// The rows in source order. Each inner vector is one row's cell text, indexed by column. A
    /// row with fewer cells than there are columns shows blank cells past its own size.
    [[nodiscard]] const std::vector<std::vector<std::string>>& rows() const
    {
        return rows_;
    }
    /// Replaces every row, sorts them again by the current sort, clears the selection, and lays the
    /// table out again. Nothing is selected afterwards, in every selection mode. Raises
    /// on_selection_changed() when something was selected before.
    void set_rows(std::vector<std::vector<std::string>> rows);

    /// Height of every body row in DIPs; 40 by default. The header strip has its own fixed height
    /// of 40.
    [[nodiscard]] float row_height() const
    {
        return row_height_;
    }
    /// Changes the row height, lays the table out again and rebuilds the visible rows.
    void set_row_height(float height);

    /// Whether each row grows past row_height() to fit its tallest cell. Text cells then wrap
    /// instead of being trimmed, and a template cell or an editor is measured at its own height.
    /// Off by default. A row keeps its height while it is out of view. Rows not yet seen count as
    /// row_height() until they scroll in.
    [[nodiscard]] bool size_rows_to_content() const
    {
        return size_rows_to_content_;
    }
    void set_size_rows_to_content(bool on); ///< See size_rows_to_content().

    // ---- columns ----
    /// The width the last arrange gave a column, whether it is fixed, Auto or a fraction. 0 before
    /// the first arrange or past the last column.
    [[nodiscard]] float column_actual_width(std::size_t column) const
    {
        return column < actual_widths_.size() ? actual_widths_[column] : 0.0f;
    }
    /// Sets one column's width. A user resize does this, then raises on_column_resized().
    void set_column_width(std::size_t column, TableViewColumnWidth width);
    /// Pins one column, or lets it scroll again (TableViewColumn::frozen). Keeps the sort.
    void set_column_frozen(std::size_t column, bool frozen);
    /// Pins one column against the right edge, or lets it scroll again
    /// (TableViewColumn::frozen_trailing). Keeps the sort.
    void set_column_frozen_trailing(std::size_t column, bool frozen);

    /// The table-wide switch for dragging header dividers; on by default. A column opts out with
    /// TableViewColumn::can_resize.
    [[nodiscard]] bool can_user_resize_columns() const
    {
        return can_user_resize_columns_;
    }
    void set_can_user_resize_columns(bool can); ///< See can_user_resize_columns().

    /// Called with the column and its new width in DIPs on every step of a user resize drag.
    void on_column_resized(Callback<void(const TableViewColumnResizedArgs&)> handler)
    {
        on_column_resized_ = std::move(handler);
    }

    /// How far the columns are scrolled sideways, DIPs. 0 is the leading edge.
    [[nodiscard]] float horizontal_offset() const
    {
        return horizontal_offset_;
    }
    /// Scrolls sideways, clamped to how far the columns overflow the viewport, and redraws.
    void set_horizontal_offset(float offset);

    // ---- sorting ----
    /// The table-wide switch for sorting by a header click; on by default. A column opts out with
    /// TableViewColumn::can_sort.
    [[nodiscard]] bool can_user_sort_columns() const
    {
        return can_user_sort_columns_;
    }
    void set_can_user_sort_columns(bool can); ///< See can_user_sort_columns().

    /// The sorted column. Unset while the rows are in source order.
    [[nodiscard]] std::optional<std::size_t> sort_column() const
    {
        return detail::optional_index(sort_column_);
    }
    /// The sorted column's direction. None while unsorted.
    [[nodiscard]] TableViewSortDirection sort_direction() const
    {
        return sort_direction_;
    }
    /// Sorts the view by `column` and clears any other column's sort. TableViewSortDirection::None
    /// restores the source order. The selected rows stay selected. Returns true when the sort
    /// changed, and then raises on_sorted().
    bool sort_by_column(std::size_t column, TableViewSortDirection direction);
    /// Does what a header click does: steps `column` to the next direction in its sort_cycle,
    /// unless on_sorting() cancels. Returns true when the sort changed.
    bool toggle_sort_direction(std::size_t column);
    /// The column header the keyboard is on. Unset while the keys move through the rows.
    [[nodiscard]] std::optional<std::size_t> header_cursor() const
    {
        return detail::optional_index(header_cursor_);
    }
    /// Restores the source order. Returns true when there was a sort to clear.
    bool clear_sort()
    {
        return sort_column_ != detail::NoIndex && sort_by_column(sort_column_, TableViewSortDirection::None);
    }
    /// Sets how two rows compare for a sort. Without one, the default, the cell text is compared,
    /// as numbers when both cells read as numbers (compare_text()).
    void set_sort_comparer(SortComparer comparer)
    {
        sort_comparer_ = std::move(comparer);
    }
    /// Called before a header click or key sorts a column. Return ui::Cancel to keep the current
    /// order. sort_by_column() does not raise it.
    void on_sorting(Callback<Decision(const TableViewSortingArgs&)> handler)
    {
        on_sorting_ = std::move(handler);
    }
    /// Called after every sort change, with the column and its new direction. When the sort was
    /// cleared, the direction is None and there is no column.
    void on_sorted(Callback<void(const TableViewSortedArgs&)> handler)
    {
        on_sorted_ = std::move(handler);
    }
    /// The default cell comparison. Compares as numbers when both texts parse entirely as numbers,
    /// else as text, ignoring ASCII case first.
    [[nodiscard]] static std::weak_ordering compare_text(const std::string& a, const std::string& b);

    /// The index into rows() of the row shown at `position` in the sorted and grouped view. Unset
    /// for a group header and past the end.
    [[nodiscard]] std::optional<std::size_t> row_at_position(std::size_t position) const
    {
        return detail::optional_index(row_at(position));
    }
    /// Where row `row`, an index into rows(), is shown in the view. Unset while its group is
    /// collapsed.
    [[nodiscard]] std::optional<std::size_t> position_of_row(std::size_t row) const
    {
        return detail::optional_index(row_position(row));
    }
    /// How many positions the view has: the rows shown plus the group header rows.
    [[nodiscard]] std::size_t position_count() const
    {
        return order_.size();
    }

    // ---- selection ----
    /// The selection mode; Single by default. Switching to None clears the selection. Switching to
    /// Single keeps at most one row, the first selected.
    [[nodiscard]] SelectionMode selection_mode() const
    {
        return selection_mode_;
    }
    void set_selection_mode(SelectionMode mode); ///< See selection_mode().

    /// The selected row with the lowest index into rows(). Unset when nothing is selected.
    [[nodiscard]] std::optional<std::size_t> selected_row() const
    {
        return detail::optional_index(first_selected_row());
    }
    /// Selects `index` alone, clamped to the rows, and scrolls it into view. Does nothing, and
    /// fires no callback, if it is already the selection, if there are no rows, or under
    /// SelectionMode::None. Unset clears the selection.
    void set_selected_row(std::optional<std::size_t> index)
    {
        select_row(detail::index_or_none(index));
    }

    /// Every selected row, as ascending indices into rows().
    [[nodiscard]] const std::vector<std::size_t>& selected_rows() const
    {
        return selection_;
    }
    /// Replaces the selection. Indices past the rows are dropped, Single keeps the lowest, None
    /// keeps nothing, and an empty list clears it. Raises on_selection_changed() when the
    /// selection changed.
    void set_selected_rows(std::vector<std::size_t> rows);
    /// Whether row `row`, an index into rows(), is selected.
    [[nodiscard]] bool is_row_selected(std::size_t row) const;

    /// Called with selected_row() whenever the selection changes: by click, by key, by
    /// set_selected_row() or set_selected_rows(), or by set_rows() clearing a selection. Replaces
    /// any previous handler.
    void on_selection_changed(Callback<void(std::optional<std::size_t>)> handler)
    {
        on_selection_changed_ = std::move(handler);
    }
    /// Sets the context menu of row `row`, an index into rows(). It is asked for on a right-click
    /// on the row, or on Shift+F10 or the Menu key while the row is the current one. The row is
    /// selected first, unless it already is or the selection mode is None. An empty list opens no
    /// menu. Replaces any previous one.
    void set_item_context_menu(Callback<std::vector<MenuItem>(std::size_t row)> menu)
    {
        item_context_menu_ = std::move(menu);
    }
    /// Opens the row's context menu (set_item_context_menu()) when no on_context_requested()
    /// handler inside the row took the request.
    bool raise_context_requested(std::optional<Point> position) override;

    // ---- cell templates ----
    /// Builds the content of one cell, by its row (an index into rows()) and column. Returns
    /// nullptr to leave that cell as text.
    using CellFactory = Callback<std::unique_ptr<Element>(std::size_t row, std::size_t column)>;
    /// Sets the cell factory. A cell it returns an Element for shows that Element instead of its
    /// text, stretched over the whole cell with no cell padding. Called once per cell as its row
    /// comes into view. The Element is kept while the row stays in view or holds focus, and built
    /// again after set_rows() or set_columns(). Replacing the factory rebuilds every cell.
    void set_cell_factory(CellFactory factory);
    /// The tip for a body cell, by its row (an index into rows()) and column. Empty means none, and
    /// the table's own tool_tip() shows instead.
    using CellToolTip = Callback<std::string(std::size_t row, std::size_t column) const>;
    /// Sets the per-cell tips; see CellToolTip.
    void set_cell_tool_tip(CellToolTip tip)
    {
        cell_tool_tip_ = std::move(tip);
    }
    /// The tip of the cell under `p` (set_cell_tool_tip()), else tool_tip().
    [[nodiscard]] std::string tool_tip_at(Point p) const override;
    /// The Element a template cell shows while its row is in view. nullptr for a text cell or a
    /// row out of view.
    [[nodiscard]] Element* cell_element(std::size_t row, std::size_t column) const;

    // ---- editing ----
    /// Whether editing is off for the whole table, the default. A column opts out on its own with
    /// TableViewColumn::read_only.
    [[nodiscard]] bool read_only() const
    {
        return read_only_;
    }
    /// Turns editing off or on. Turning it off commits an open edit, or cancels it when the commit
    /// is rejected.
    void set_read_only(bool read_only);

    /// Sets the factory for a cell's editor, by its row and column. Returning nullptr gives the
    /// default, a TextBox over a text cell. A template cell without an editor is not editable. The
    /// editor is laid over the cell while the edit is open. The table stays the focused element and
    /// passes the keys and text on to the editor's first focusable control.
    void set_cell_editor_factory(CellFactory factory);

    /// Whether a cell editor is open.
    [[nodiscard]] bool is_editing() const
    {
        return editor_ != nullptr;
    }
    /// The row of the open edit, an index into rows(). Unset when none is open.
    [[nodiscard]] std::optional<std::size_t> editing_row() const
    {
        return detail::optional_index(edit_row_);
    }
    /// The column of the open edit. Unset when none is open.
    [[nodiscard]] std::optional<std::size_t> editing_column() const
    {
        return detail::optional_index(edit_column_);
    }
    /// The open editor, either the TextBox or what the editor factory built, or nullptr.
    [[nodiscard]] Element* editor() const
    {
        return editor_.get();
    }
    /// Closes the open edit and keeps its text. on_cell_edited() runs first and may reject it.
    /// Returns false when nothing was open or the commit was rejected.
    bool commit_edit();
    /// Closes the open edit and leaves the cell as it was. Returns false when nothing was open.
    bool cancel_edit();

    /// The current column, where a click on a cell or Left and Right put it. F2 edits its cell on
    /// the keyboard's row. Unset until then, and F2 then edits the first editable column.
    [[nodiscard]] std::optional<std::size_t> current_column() const
    {
        return detail::optional_index(current_column_);
    }
    /// Moves the current column, clamped to the columns, and scrolls it into view.
    void set_current_column(std::size_t column);

    /// Called before an editor opens on a cell, with its row and column. Return ui::Cancel to keep
    /// it closed.
    void on_beginning_edit(Callback<Decision(const TableViewCellArgs&)> handler)
    {
        on_beginning_edit_ = std::move(handler);
    }
    /// Called on a commit with the cell and the editor's text. For an editor from the factory, the
    /// text is the cell's own text. Return ui::Cancel to reject the value, which keeps the editor
    /// open. The handler may rewrite `text` to what the cell should show. Without a handler a
    /// commit stores the text in rows().
    void on_cell_edited(Callback<Decision(const TableViewCellEditArgs&)> handler)
    {
        on_cell_edited_ = std::move(handler);
    }
    /// Replaces one cell's text in rows() and redraws. The row keeps its place until the next sort.
    void set_cell_text(std::size_t row, std::size_t column, std::string text);
    /// Replaces one row's text in rows(), and its group key when `group_key` is set, and builds its
    /// template cells again. The row keeps its place until the next sort, and an open cell editor
    /// stays open. A changed group key files the row under its new group.
    void update_row(std::size_t row, std::vector<std::string> cells, std::optional<std::string> group_key = std::nullopt);

    // ---- grouping ----
    /// Sets one key per row, by index into rows(), the text its group header shows. Rows with the
    /// same key form a group. Groups follow the order their keys first appear in rows(), and a
    /// sort orders the rows within each group. A collapsed group stays collapsed while its key is
    /// in use. An empty list, the default, shows the rows ungrouped. A row past the end of the list
    /// is filed under an empty key.
    void set_group_keys(std::vector<std::string> keys);
    /// Builds the content of group `group`'s header row.
    using GroupHeaderFactory = Callback<std::unique_ptr<Element>(std::size_t group)>;
    /// Sets the group header content, shown after the chevron in place of the key and count. It is
    /// built for the headers in view only, and again when the groups change. A click on the row
    /// still toggles the group.
    void set_group_header_factory(GroupHeaderFactory factory);
    /// Group `group`'s built header content while its header is in view, else nullptr.
    [[nodiscard]] Element* group_header_element(std::size_t group) const;
    /// The keys set_group_keys() was given.
    [[nodiscard]] const std::vector<std::string>& group_keys() const
    {
        return group_keys_;
    }
    /// Whether the keys group the rows; on by default. Off shows the rows ungrouped and keeps the
    /// keys, so turning it back on groups them again.
    [[nodiscard]] bool grouping_enabled() const
    {
        return grouping_enabled_;
    }
    void set_grouping_enabled(bool enabled); ///< See grouping_enabled().
    /// Whether group headers are sticky; on by default. The current group's header stays pinned
    /// under the column headers until the next group's header pushes it up. A click on it still
    /// toggles the group.
    [[nodiscard]] bool sticky_group_headers() const
    {
        return sticky_headers_;
    }
    void set_sticky_group_headers(bool sticky); ///< See sticky_group_headers().

    /// How many groups there are. 0 while the rows are not grouped.
    [[nodiscard]] std::size_t group_count() const
    {
        return groups_.size();
    }
    /// A group's key, the text on its header. Empty past the last group.
    [[nodiscard]] const std::string& group_key(std::size_t group) const;
    /// How many rows a group holds, in or out of view. 0 past the last group.
    [[nodiscard]] std::size_t group_size(std::size_t group) const
    {
        return group < groups_.size() ? groups_[group].count : 0;
    }
    /// Whether a group's rows are shown.
    [[nodiscard]] bool is_group_expanded(std::size_t group) const
    {
        return group < groups_.size() && groups_[group].expanded;
    }
    /// Expands or collapses a group. A keyboard cursor on a row that this hides moves to the
    /// group's header. The selection stays.
    void set_group_expanded(std::size_t group, bool expanded);
    void expand_all_groups();   ///< Expands every group.
    void collapse_all_groups(); ///< Collapses every group.
    /// The group a row is filed under. Unset while the rows are not grouped.
    [[nodiscard]] std::optional<std::size_t> group_of_row(std::size_t row) const
    {
        return detail::optional_index(row_group(row));
    }
    /// Whether view position `position` is a group header row.
    [[nodiscard]] bool is_group_header(std::size_t position) const
    {
        return position < order_.size() && order_[position] == detail::NoIndex;
    }
    /// The group of the row or header at view position `position`. Unset past the end or while the
    /// rows are not grouped.
    [[nodiscard]] std::optional<std::size_t> group_at_position(std::size_t position) const
    {
        return detail::optional_index(position_group(position));
    }

    // ---- appearance ----
    /// Row separators, column separators, both (the default) or neither.
    [[nodiscard]] TableViewGridLinesVisibility grid_lines_visibility() const
    {
        return grid_lines_;
    }
    void set_grid_lines_visibility(TableViewGridLinesVisibility lines); ///< See grid_lines_visibility().

    /// Whether rows are banded: every second row in view order gets the alternating background.
    /// Off by default.
    [[nodiscard]] bool alternating_rows() const
    {
        return alternating_rows_;
    }
    void set_alternating_rows(bool on); ///< See alternating_rows().
    /// Sets the banding color. Empty, the default, uses the theme's banding color.
    void set_alternating_row_background(Paint paint);
    /// See set_alternating_row_background().
    [[nodiscard]] const Paint& alternating_row_background() const
    {
        return alternating_background_;
    }

    /// Sets what the body shows, centered, while there are no rows. nullptr, the default, shows
    /// nothing. The table owns it.
    void set_empty_content(std::unique_ptr<Element> content);
    /// The empty-state content, or nullptr.
    [[nodiscard]] Element* empty_content() const
    {
        return empty_content_.get();
    }


    /// The body's current vertical scroll offset, DIPs.
    [[nodiscard]] float scroll_offset() const
    {
        return list_->scroll_offset();
    }
    /// Handles the table's shortcuts. An Escape that closes a popup the editor opened, such as a
    /// ComboBox's list, leaves the edit open.
    bool handle_shortcut(const KeyEvent& e) override;
    /// The resize cursor over a resizable header divider.
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point p) const override;

    // ---- focus ----
    /// Whether the grid holds keyboard focus and draws its focus ring.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// Focusable while enabled and holding at least one row.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled() && !rows_.empty();
    }
    /// The table, then the focusable controls in its template cells and empty content. Never the
    /// editor, because the table keeps focus while one is open.
    void collect_focusables(std::vector<Element*>& out) override;

    // ---- tree ----
    /// The table body, the empty-state content while it shows, the realized template cells and the
    /// open editor.
    void for_each_child(ChildVisitor visit) const override;

    // ---- automation ----
    /// Reported to screen readers as a data grid. It holds a header with one item per column, and
    /// invoking a sortable one sorts by it. Then come the rows in view, each selectable, and the
    /// group headers, which expand and collapse. A row holds its cells, one per column, each named
    /// by its text. A template cell's Element or an open editor sits under its cell. As in every
    /// virtualizing list, only the rows in view are reported.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::DataGrid;
    }
    /// The empty-state content while it shows, the header, then the rows in view.
    void automation_children(std::vector<Element*>& out) const override;
    /// The row the keyboard is on, so a screen reader reads it as the arrow keys move.
    [[nodiscard]] Element* automation_focus() const override;
    /// The cell around a template cell or an open editor, and the header row around a group
    /// header's content, while their row is in view.
    [[nodiscard]] Element* automation_parent_of(const Element& child) const override;

protected:
    /// Clears the row context menu too, which is listed as a child only while it shows.
    void clear_dirty_override(DirtyFlags flags) override;

    // ---- layout ----
    /// Desired size: the sum of the column widths, with fraction columns at their content width,
    /// by the header height plus every row. Each is clamped to `available` where it is bounded.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Resolves the column widths against the final width, splits the bounds into the header
    /// strip and the body, and arranges the body and the empty-state content.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- render ----
    /// Draws the header strip, the realized rows, the grid lines, the scroll bars and the focus ring.
    void render_override(Renderer& renderer, const RenderContext& context) const override;

    // ---- input ----
    /// A press on a header sorts, or starts a resize on its trailing divider. A press on a body
    /// row selects it by the selection mode's rules.
    bool pointer_override(const PointerEvent& e) override;
    /// A press a template cell took still commits an open edit elsewhere in the table.
    void handled_pointer_override(const PointerEvent& e) override;
    /// Vertical turns scroll the rows. Horizontal turns and Shift+wheel scroll the columns
    /// sideways when they overflow.
    bool               wheel_override(const PointerEvent& e) override;
    void               hit_test_children(Point p, std::vector<Element*>& out) const override;
    [[nodiscard]] bool covers_children_at(Point p) const override;
    /// Ticks the children and the horizontal scroll bar's fade, and commits an edit whose focus
    /// left the table.
    void tick_override(Duration delta) override;


private:
    // While an editor is open, whatever its focused control claims (a TextBox's Ctrl+C).
    [[nodiscard]] bool takes_shortcut(const KeyEvent& e) const;
    // Enter while an editor is open (it commits), the cursor is on a group header (it toggles) or
    // the keyboard is on a column header (it sorts).
    [[nodiscard]] bool takes_enter_key() const
    {
        return editor_ != nullptr || cursor_header_ != detail::NoIndex || header_cursor_ != detail::NoIndex;
    }
    // Up/Down always; Left/Right while the columns overflow the viewport, while editing is
    // possible, or while an editor is open. Claimed so Window's directional focus navigation
    // leaves them alone (Element::key_override).
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowUp || key == Key::ArrowDown ||
               ((key == Key::ArrowLeft || key == Key::ArrowRight) &&
                (horizontal_extent() > 0.0f || !read_only_ || editor_ != nullptr || cursor_header_ != detail::NoIndex ||
                 header_cursor_ != detail::NoIndex));
    }
    // The public index getters' private forms, NoIndex for none, as the code below works with them.
    [[nodiscard]] std::size_t row_at(std::size_t position) const
    {
        return position < order_.size() ? order_[position] : detail::NoIndex;
    }
    [[nodiscard]] std::size_t row_position(std::size_t row) const
    {
        return row < positions_.size() ? positions_[row] : detail::NoIndex;
    }
    [[nodiscard]] std::size_t row_group(std::size_t row) const
    {
        return row < row_group_.size() ? row_group_[row] : detail::NoIndex;
    }
    [[nodiscard]] std::size_t position_group(std::size_t position) const
    {
        return position < position_group_.size() ? position_group_[position] : detail::NoIndex;
    }
    [[nodiscard]] std::size_t first_selected_row() const
    {
        return selection_.empty() ? detail::NoIndex : selection_.front();
    }
    void select_row(std::size_t index); // set_selected_row(), NoIndex clearing
    // set_rows() with `selected` (rows of the new list) as the selection afterwards; raises
    // on_selection_changed() once, when the selection differs from the one before. The spec uses
    // it to keep the selection on the same items through a collection change.
    void replace_rows(std::vector<std::vector<std::string>> rows, std::vector<std::size_t> selected);

    Callback<std::vector<MenuItem>(std::size_t)> item_context_menu_;
    std::unique_ptr<MenuFlyoutControl>           item_menu_; // made on the first row menu
    friend class TableViewRow; // defined in TableView.cpp; reads the column geometry and selection
    template <typename T>
    friend struct TableView; // the spec's build() calls replace_rows()
    friend class TableViewCellAutomation;
    friend class TableViewHeaderAutomation;
    friend class TableViewHeaderItemAutomation;

    // The realized row element at view position `position`, or nullptr out of view.
    [[nodiscard]] Element* realized_row_at(std::size_t position) const;
    // The header's node for `column`, built on first use.
    [[nodiscard]] Element* header_item_automation(std::size_t column) const;

    // A press on the row at view position `position`, by the selection mode's rules.
    void row_clicked(std::size_t position, bool shift, bool ctrl);
    // A pointer press on a body row at window x `x`: moves the current column there and, on a
    // double-click, opens an editor.
    void cell_pressed(std::size_t position, float x, int click_count);
    // A click on the group header row at `position`.
    void group_header_clicked(std::size_t position);
    // Moves the keyboard cursor to `position`, a row or a group header.
    void move_cursor(std::size_t position, bool shift, bool ctrl);
    // The cursor's view position: the header it is on, else the keyed row's; detail::NoIndex when neither.
    [[nodiscard]] std::size_t cursor_position() const;
    [[nodiscard]] std::size_t first_row_position() const;
    [[nodiscard]] std::size_t column_at(float x) const; // the column under window x, or detail::NoIndex
    void                      scroll_column_into_view(std::size_t column);
    // The keys while the keyboard is on a column header.
    void handle_header_key(const KeyEvent& e);
    void set_header_cursor(std::size_t column);
    // Narrows or widens `column` by `delta` DIPs, as a divider drag does.
    void resize_column_by(std::size_t column, float delta);
    // Fixes the fraction columns before `column` at their resolved widths, so resizing `column` moves
    // its divider by exactly the drag.
    void lock_star_columns_before(std::size_t column);
    // Editing.
    [[nodiscard]] bool               column_editable(std::size_t column) const;
    [[nodiscard]] std::size_t        first_editable_column() const;
    bool                             begin_edit(std::size_t row, std::size_t column);
    bool                             close_edit(bool commit);
    [[nodiscard]] const std::string& cell_text(std::size_t row, std::size_t column) const;
    // Template cells: the realized Elements of `row`, built when missing; nullptr without a
    // factory.
    struct RealizedRow
    {
        std::size_t                           row = detail::NoIndex;
        std::vector<std::unique_ptr<Element>> cells; // by column; nullptr for a text cell
    };
    RealizedRow*               realize_cells(std::size_t row) const;
    Element*                   realize_group_header(std::size_t group) const;
    void                       retire_group_headers();
    [[nodiscard]] RealizedRow* realized(std::size_t row) const;
    void                       prune_cells();
    void                       clear_cells();
    void                       refresh_rows(); // re-realizes the visible rows after the view changed
    void                       toggle_group(std::size_t group);
    // Moves the keyboard cursor to `position` and applies the mode's rules for a key move.
    void cursor_moved(std::size_t position, bool shift, bool ctrl);
    // Replaces the selection with sorted, deduplicated `rows`; raises the handler on a change.
    void                replace_selection(std::vector<std::size_t> rows);
    void                select_range(std::size_t from_position, std::size_t to_position, bool clear_first);
    void                scroll_into_view(std::size_t position);
    void                apply_sort();
    void                after_vertical_scroll();
    [[nodiscard]] bool  wants_horizontal_lines() const;
    [[nodiscard]] bool  wants_vertical_lines() const;
    [[nodiscard]] float content_width() const;
    [[nodiscard]] float horizontal_extent() const; // how far the columns overflow the body
    [[nodiscard]] float frozen_width() const;
    [[nodiscard]] Rect  inner_rect() const; // the bounds inside the outer border
    [[nodiscard]] Rect  body_rect() const;
    // The columns' room: the bounds less the vertical bar's gutter (an era bar's; 0 otherwise).
    [[nodiscard]] float view_width() const;
    // Where the horizontal bar is drawn and hit-tested: the body, short of the vertical gutter.
    [[nodiscard]] Rect hbar_viewport() const;
    // Whether the horizontal bar is drawn and takes input.
    [[nodiscard]] bool hbar_shown() const;
    // The column's left edge in the body, after the horizontal scroll (frozen ones do not move).
    [[nodiscard]] float column_left(std::size_t column) const;
    // In the leading run of frozen columns.
    [[nodiscard]] bool column_frozen(std::size_t column) const;
    // In the trailing run of frozen_trailing columns, and not in the leading run.
    [[nodiscard]] bool column_frozen_trailing(std::size_t column) const;
    [[nodiscard]] bool column_pinned(std::size_t column) const
    {
        return column_frozen(column) || column_frozen_trailing(column);
    }
    [[nodiscard]] float trailing_frozen_width() const;
    // The band the scrolling columns show in, between the two frozen bands, in window coordinates.
    [[nodiscard]] float scrolling_left() const;
    [[nodiscard]] float scrolling_right() const;
    // Draws the scrolling columns clipped to their band, then the frozen ones over it.
    template <typename Fn>
    void for_each_column_in_paint_order(Renderer& renderer, float top, float height, Fn&& draw) const;
    // The column whose resize divider is under `p`, or detail::NoIndex.
    [[nodiscard]] std::size_t divider_at(Point p) const;
    // The column whose header is under `p`, or detail::NoIndex.
    [[nodiscard]] std::size_t header_at(Point p) const;
    void                      resolve_columns(float viewport_width) const;
    [[nodiscard]] float       natural_width(std::size_t column) const;

    std::vector<TableViewColumn>          columns_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<std::size_t>              order_;     // view position -> row; detail::NoIndex for a group header
    std::vector<std::size_t>              positions_; // row -> view position; detail::NoIndex while collapsed
    std::vector<std::size_t>              selection_; // ascending rows

    // Grouping: the keys by row, the groups in first-appearance order, each row's group and each
    // position's group (both empty while ungrouped), and the keys collapsed by the user.
    struct Group
    {
        std::string key;
        std::size_t count    = 0;
        bool        expanded = true;
        std::size_t header   = detail::NoIndex; // its header row's view position
    };
    std::vector<std::string> group_keys_;
    bool                     grouping_enabled_ = true;
    std::vector<Group>       groups_;
    std::vector<std::size_t> row_group_;
    std::vector<std::size_t> position_group_;
    std::vector<std::string> collapsed_;
    std::size_t              cursor_header_ = detail::NoIndex; // the group whose header the cursor is on
    // The pinned copy of the current group's header row, and where it is (empty while none shows).
    bool                     sticky_headers_ = true;
    std::unique_ptr<Element> sticky_row_;
    std::size_t              sticky_position_ = detail::NoIndex;
    Rect                     sticky_rect_{};
    void                     update_sticky_header();

    // Template cells and editing.
    mutable CellFactory              cell_factory_;
    CellFactory                      editor_factory_;
    mutable std::vector<RealizedRow> realized_;
    mutable GroupHeaderFactory       group_header_factory_;
    // The built group header content in view, by group.
    mutable std::vector<std::pair<std::size_t, std::unique_ptr<Element>>> realized_headers_;
    // Template cells and editors on their way out, destroyed by the next arrange: Window's tab ring
    // and open popups may still name them until that layout pass refreshes them.
    std::vector<std::unique_ptr<Element>>            retired_;
    bool                                             read_only_      = true;
    std::size_t                                      current_column_ = detail::NoIndex;
    std::size_t                                      edit_row_       = detail::NoIndex;
    std::size_t                                      edit_column_    = detail::NoIndex;
    std::unique_ptr<Element>                         editor_;
    Element*                                         edit_target_ = nullptr; // the editor's control the keys go to
    TextBoxControl*                                  edit_box_    = nullptr; // the default editor, when it is one
    bool                                             focus_commit_pending_ = false;
    bool                                             escape_closes_popup_  = false;
    Callback<Decision(const TableViewCellArgs&)>     on_beginning_edit_;
    Callback<Decision(const TableViewCellEditArgs&)> on_cell_edited_;

    // Resolved by arrange (or measure, for the desired size): each column's width and its left
    // edge before the horizontal scroll.
    mutable std::vector<float> actual_widths_;
    mutable std::vector<float> column_lefts_;
    // Each column's content width (header and the cells in view), worked out by measure().
    mutable std::vector<float> natural_widths_;
    // Each column's header, trimmed to its width; render() has no measurer.
    mutable std::vector<std::string> header_text_;
    // Cached by measure() so arrange can trim the headers to widths it only then knows. Relies on
    // the host-owned measurer and theme outliving the grid, as VirtualList's own cache does.
    mutable const TextMeasurer* measurer_ = nullptr;
    mutable const Theme*        theme_    = nullptr;

    // The default body row height.
    float row_height_           = 40.0f;
    bool  size_rows_to_content_ = false;
    // The header strip, as tall as a standard row.
    float header_height_ = 40.0f;

    SelectionMode                selection_mode_   = SelectionMode::Single;
    std::size_t                  cursor_           = detail::NoIndex; // keyboard cursor, an index into rows()
    std::size_t                  anchor_           = detail::NoIndex; // range anchor, an index into rows()
    TableViewGridLinesVisibility grid_lines_       = TableViewGridLinesVisibility::All;
    bool                         alternating_rows_ = false;
    Paint                        alternating_background_{};

    std::size_t            sort_column_    = detail::NoIndex;
    TableViewSortDirection sort_direction_ = TableViewSortDirection::None;
    SortComparer           sort_comparer_;
    bool                   can_user_sort_columns_   = true;
    bool                   can_user_resize_columns_ = true;

    float horizontal_offset_ = 0.0f;
    // Header interaction: the header pressed (for click-to-sort), the divider being dragged.
    std::size_t pressed_header_ = detail::NoIndex;
    std::size_t resizing_       = detail::NoIndex;
    std::size_t header_cursor_  = detail::NoIndex; // the column header the keyboard is on
    float       resize_start_x_ = 0.0f;
    float       resize_start_w_ = 0.0f;


    std::unique_ptr<VirtualListControl> list_;         // internal body viewport; never exposed
    bool                     opened_by_press_ = false; // cell_pressed() opened the editor for the press being delivered
    std::unique_ptr<Element> empty_content_;
    mutable Size             empty_desired_{}; // its measure(), margins included
    ScrollBarOverlay         hbar_{Orientation::Horizontal};
    mutable float            horizontal_gutter_ = 0.0f; // measure(): an era bar's thickness while it shows

    // The header strip's automation node, built the first time a client asks for it.
    mutable std::unique_ptr<TableViewHeaderAutomation> header_automation_;

    Callback<void(std::optional<std::size_t>)>        on_selection_changed_;
    Callback<void(const TableViewColumnResizedArgs&)> on_column_resized_;
    Callback<Decision(const TableViewSortingArgs&)>   on_sorting_;
    CellToolTip                                       cell_tool_tip_;
    Callback<void(const TableViewSortedArgs&)>        on_sorted_;

protected:
    void enabled_changed() override;
    /// The row keys described in the class comment. Left and Right scroll sideways, move the
    /// current column while editing is possible, or collapse and expand the group header the
    /// cursor is on. F2 opens an editor. While one is open, Enter commits, Escape cancels and every
    /// other key goes to the editor. Ignored while disabled or unfocused.
    bool key_override(const KeyEvent& e) override;
    /// Typed text goes to an open editor; without one it is ignored.
    bool text_override(const TextInputEvent& text_event) override;
    /// Called by Window as focus arrives or leaves. Redraws. An open editor follows the table's
    /// focus. Focus leaving commits the edit on the next tick unless it has come back by then.
    void focus_changed(bool focused) override;

protected:
    /// Inside the header cell, group header, cell or row the keyboard is on, cut to the part in
    /// view. Around the whole table otherwise, and nowhere while an editor is open.
    [[nodiscard]] Rect  focus_ring_rect(const Theme& theme) const override;
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override;
    [[nodiscard]] bool  focus_ring_inside() const override;

private:
    // Where the focus ring goes: the rect, whether inside it, and whether it takes the theme's
    // corner radius instead of square corners.
    struct RingPlace
    {
        Rect rect{};
        bool inside  = false;
        bool rounded = false;
    };
    [[nodiscard]] RingPlace ring_place() const;

    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.selection = this;
        out.grid      = this;
        out.table     = this;
    }
    // Every view position, group headers included: accessibility grid rows count in view order.
    [[nodiscard]] std::size_t grid_row_count() const override
    {
        return position_count();
    }
    [[nodiscard]] std::size_t grid_column_count() const override
    {
        return columns_.size();
    }
    // The cell at view position `row` while that row is in view; nullptr for a group header.
    [[nodiscard]] Element* grid_item(std::size_t row, std::size_t column) const override;
    // The header's items, one per column.
    void table_column_headers(std::vector<Element*>& out) const override;
    // The selected rows in view.
    void selected_items(std::vector<Element*>& out) const override;
    // True under Multiple and Extended selection.
    [[nodiscard]] bool can_select_multiple() const override
    {
        return selection_mode_ == SelectionMode::Multiple || selection_mode_ == SelectionMode::Extended;
    }
    // False: every mode can have nothing selected.
    [[nodiscard]] bool selection_required() const override
    {
        return false;
    }
};

namespace detail
{

// The selected rows after `change`, kept on the same items: an insert or remove shifts the rows
// after it, a removed row drops out and a moved one follows its item. A Reset (no key, a new
// vector) keeps the positions.
inline void carry_selection(std::vector<std::size_t>& rows, const CollectionChange& change)
{
    std::vector<std::size_t> carried;
    carried.reserve(rows.size());
    for (std::size_t row : rows)
    {
        switch (change.kind)
        {
            case CollectionChangeKind::Inserted:
                row += row >= change.index ? change.count : 0;
                break;
            case CollectionChangeKind::Removed:
                if (row >= change.index && row < change.index + change.count)
                {
                    continue;
                }
                row -= row >= change.index + change.count ? change.count : 0;
                break;
            case CollectionChangeKind::Moved:
                if (row == change.index)
                {
                    row = change.to;
                }
                else if (change.index < change.to && row > change.index && row <= change.to)
                {
                    --row;
                }
                else if (change.to < change.index && row >= change.to && row < change.index)
                {
                    ++row;
                }
                break;
            case CollectionChangeKind::Replaced:
            case CollectionChangeKind::Reset:
                break;
        }
        carried.push_back(row);
    }
    std::ranges::sort(carried);
    rows = std::move(carried);
}

} // namespace detail

/// One column of a ui::TableView: its header, its width, the text it shows for an item, and how
/// the user may size, sort and pin it.
///
/// The callables are std::function because columns are written as a braced list, which copies
/// its entries.
template <typename T>
struct TableColumn
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// Caption drawn in the header strip above the rows.
    std::string header;
    /// A number of DIPs including the cell padding, `ui::AutoSize` (as wide as the header and the
    /// cells in view) or a fraction (`1_fr`, `2_fr`: a weighted share of what the other columns
    /// leave). Header and cell text past the width is trimmed with an ellipsis.
    TableViewColumnWidth width = 100.0f;
    /// The cell text for an item.
    std::function<std::string(const T&)> text{};
    /// The column's minimum and maximum width, DIPs. The resolved width and a user resize stay
    /// inside them.
    float min_width = 20.0f;
    float max_width = Unbounded; ///< See `min_width`.
    /// Whether the column stays put while the other columns scroll sideways. Only a leading run of
    /// frozen columns is pinned.
    bool frozen = false;
    /// Whether the column stays put against the right edge while the other columns scroll
    /// sideways. Only a trailing run of such columns is pinned.
    bool frozen_trailing = false;
    /// Whether the user can drag this header's divider. The table's `.can_user_resize_columns`
    /// must be on too.
    bool can_resize = true;
    /// Whether a header click sorts by this column. The table's `.can_user_sort_columns` must be
    /// on too.
    bool can_sort = true;
    /// The directions repeated header clicks step through.
    TableViewSortCycle sort_cycle = TableViewSortCycle::AscendingDescending;
    /// How two items order for this column, for example
    /// `[](const T& a, const T& b) { return a.size <=> b.size; }`. Left out, the cell text is
    /// compared, as numbers when both cells are numbers.
    std::function<std::weak_ordering(const T&, const T&)> compare{};
    /// Whether this column is never edited, even in a table with `.read_only = false`.
    bool read_only = false;
    /// The cell's content for an item, for example
    /// `[](Task& t) -> ui::Spec { return ui::CheckBox{.checked = t.done}; }`, stretched over the
    /// whole cell with no padding. Built for the rows in view only, and again when the collection
    /// changes. `text` still sorts the column and names the cell.
    std::function<Spec(T&)> cell{};
    /// The editor for an item, shown over the cell while it is edited. It works on a copy of the
    /// item. A commit copies it back after `.on_cell_edited` accepts it, and a cancel drops it, so
    /// the item must be copyable. Without an editor, a text column edits in a TextBox and a column
    /// with a `cell` template cannot be edited.
    std::function<Spec(T&)> editing_cell{};
    /// The tip shown while the pointer rests on the item's cell in this column, for example the
    /// full text of a trimmed cell. Empty for none.
    std::function<std::string(const T&)> tool_tip{};
};

/// A virtualized table: one row per item, one cell per column. Builds a ui::TableViewControl.
///
/// `.items` is the collection and takes the same sources as ui::ListView. Each column says which
/// text it shows for an item, or gives a `cell` template that builds the cell's content from the
/// item. Only visible rows are realized, so a large table costs what the viewport costs. When a
/// bound collection changes, the rows are read again.
///
/// A header click sorts the table's view of the rows by that column, never `.items`, and the
/// selection stays on the same items. Dragging a header's divider resizes the column and reports
/// it through `.on_column_resized`. Columns wider than the table scroll sideways, except frozen
/// ones. Row indices (`.selected_row`, `.selected_indices`) are positions in `.items`, whatever
/// the sort or grouping. Nothing is selected until the user or `.selected_row` picks a row.
///
/// `.read_only = false` lets the user edit cells with F2 or a double-click. Enter commits and
/// Escape cancels. `.on_cell_edited` writes each value into its item or rejects it. `.group_by`
/// files the rows under collapsible group headers.
///
/// Example:
/// @code
/// struct Person
/// {
///     std::string name;
///     std::string role;
///     int         years = 0;
///     friend bool operator==(const Person&, const Person&) = default;
/// };
/// using ui::operator""_fr;
/// ui::Property<std::vector<Person>> people{{{"Ada Lovelace", "Engineer", 5}, {"Grace Hopper", "Architect", 12}}};
/// ui::Property<std::string>         status{};
///
/// window.set_content(ui::VerticalStack{.content = {
///     ui::TableView<Person>{
///         .items   = people,
///         .columns = {
///             {.header = "Name", .width = 140.0f, .text = [](const Person& p) { return p.name; }, .frozen = true},
///             {.header = "Role", .width = 1_fr, .text = [](const Person& p) { return p.role; }},
///             {.header  = "Years",
///              .width   = 80.0f,
///              .text    = [](const Person& p) { return std::to_string(p.years); },
///              .compare   = [](const Person& a, const Person& b) { return a.years <=> b.years; },
///              .read_only = true},
///         },
///         .alternating_rows     = true,
///         .read_only            = false,
///         .group_by             = [](const Person& p) { return p.role; },
///         .height               = 300.0f,
///         .on_selection_changed = [&](std::optional<Person> p) { status = p ? p->name : ""; },
///         .on_cell_edited       = [](const ui::TableViewCellEditedArgs<Person>& args)
///         {
///             if (args.text.empty())
///             {
///                 return ui::Cancel; // keeps the editor open
///             }
///             (args.column == 0 ? args.item.name : args.item.role) = args.text;
///             return ui::Proceed;
///         },
///         .empty_content        = ui::Text{.text = "No people yet"},
///     },
///     ui::Text{.text = status},
/// }});
/// @endcode
template <typename T>
struct TableView
{
    /// The control this spec builds; what `ui::Control<TableView>` points at.
    using control_type = TableViewControl;

    // ---- properties ----
    /// The collection, one row per item: a ui::ObservableVector<T>, a
    /// `ui::Property<std::vector<T>>`, a plain vector or a braced list.
    Binding<std::vector<T>> items{};
    /// What identifies an item when `.items` is bound to a property: a member pointer or a
    /// callable returning the key (see ui::ItemKey). Keys must be unique. With a key, a new vector
    /// keeps the selection on the same items. Without one, the rows before and after an edit keep
    /// theirs. A change that arrives as a reset, such as an ObservableVector's assign() or clear()
    /// or a very large reorder, clears the selection.
    ItemKey<std::type_identity_t<T>> key{};
    /// The columns, left to right: header, width in DIPs and the cell text for an item.
    std::vector<TableColumn<std::type_identity_t<T>>> columns{};
    /// The selected row. Unset, the default, selects nothing. For a table that opens on its first
    /// row, set 0.
    TwoWayField<std::optional<std::size_t>> selected_row{};
    /// Every selected row, as ascending positions in `.items`. Bind a
    /// `ui::Property<std::vector<std::size_t>>` to read and set the selection under Multiple or
    /// Extended. Two-way with ui::bind(). Left empty and unbound, `.selected_row` decides.
    Binding<std::vector<std::size_t>> selected_indices{};
    /// Row height, DIPs. 30 and 48 are the compact and comfortable densities.
    Binding<float> row_height{40.0f};
    /// Whether rows grow past `row_height` to fit their tallest cell, wrapping the text cells.
    Binding<bool> size_rows_to_content{false};
    /// None, Single (the default), Multiple or Extended.
    Binding<SelectionMode> selection_mode{SelectionMode::Single};
    /// Whether header clicks sort. Columns opt out with `can_sort`.
    Binding<bool> can_user_sort_columns{true};
    /// Whether header dividers can be dragged. Columns opt out with `can_resize`.
    Binding<bool> can_user_resize_columns{true};
    /// Which grid lines show: All (the default), Horizontal, Vertical or None.
    Binding<TableViewGridLinesVisibility> grid_lines_visibility{TableViewGridLinesVisibility::All};
    /// Whether rows are banded: every second row takes `alternating_row_background`.
    Binding<bool> alternating_rows{false};
    /// False turns editing on, by F2 or a double-click on a cell. Columns opt out with their own
    /// `read_only`. Each commit reaches `.on_cell_edited`.
    Binding<bool> read_only{true};
    /// The group an item is filed under, as the text its header shows. Groups follow the order
    /// their keys first appear in `.items`, and a sort orders the rows within each group.
    Callback<std::string(const T&)> group_by{};
    /// Whether `.group_by` groups the rows. False shows them ungrouped, and true groups them
    /// again, with the collapsed groups still collapsed.
    Binding<bool> grouping_enabled{true};
    /// Whether the current group's header stays pinned under the column headers while its rows
    /// scroll under it.
    Binding<bool> sticky_group_headers{true};

    // ---- appearance / layout ----
    /// The banding color. Empty, the default, uses the theme's banding color. Drawn only with
    /// `alternating_rows`.
    Binding<Paint>                    alternating_row_background{};
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// Fires when the user changes the selection, by click or keys. A change that comes from a
    /// binding, a write to `selected_row` or a change of `.items`, doesn't run it. Called with a copy of the
    /// selected row's item, the first in `.items` order under Multiple and Extended, or unset when
    /// nothing is selected (see ui::SelectedItem). The copy is safe to keep.
    Callback<void(SelectedItem<std::type_identity_t<T>>)> on_selection_changed{};
    /// Called with the column and the direction a header click or key is about to sort it in.
    /// Return ui::Cancel to keep the current order.
    Callback<Decision(const TableViewSortingArgs&)> on_sorting{};
    /// Called with the column and its new direction after a sort change. The column is unset when
    /// the sort was cleared.
    Callback<void(const TableViewSortedArgs&)> on_sorted{};
    /// Called with the column and its new width in DIPs on every step of a user resize.
    Callback<void(const TableViewColumnResizedArgs&)> on_column_resized{};
    /// Called with the item and the column before an editor opens. Return ui::Cancel to keep it
    /// closed.
    Callback<Decision(const TableViewBeginningEditArgs<std::type_identity_t<T>>&)> on_beginning_edit{};
    /// Called on a commit with the item, the column and the editor's text. Write the value into
    /// the item and return ui::Proceed or nothing. Return ui::Cancel to reject it and keep the
    /// editor open. For a column with an `editing_cell`, `item` is the edited copy and the text is
    /// its cell text, and proceeding copies it into the collection. A `ui::Property` collection is
    /// written back after an accepted edit. An ObservableVector's item is written back with
    /// ObservableVector::replace(), so everything else showing it hears of the edit; an item that
    /// can't be copied is changed in place, without a notification. Without a handler a text edit only changes
    /// the cell as shown, until the rows are read again.
    Callback<Decision(const TableViewCellEditedArgs<std::type_identity_t<T>>&)> on_cell_edited{};
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    /// Out: the handle that receives the built control. Its type uses std::type_identity_t, so
    /// passing a handle does not take part in deducing the type argument.
    ControlOut<std::type_identity_t<TableView>> control{};

    // ---- children ----
    /// Shown, centered in the body, while there are no rows.
    Spec empty_content{};

    // ---- templates ----
    /// A group header's content from its key, shown after the chevron in place of the key and the
    /// count. Needs `.group_by`.
    Callback<Spec(const std::string& key)> group_header_template{};
    /// The context menu of an item: a callable taking the item and returning the menu's entries.
    /// Called on a right-click on its row, or on Shift+F10 or the Menu key while it is current.
    /// The row is selected first. An empty list opens no menu. An item of a ui::ObservableVector
    /// stays where it is while it is in the collection, so an entry's `on_click` may keep a
    /// reference to it. Any other item is valid during the call only, so capture it by value.
    Callback<std::vector<MenuItem>(const std::type_identity_t<T>&)> item_context_menu{};

    /// Consumes the spec and returns the built ui::TableViewControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&
    {
        detail::ItemsModel<T> model = detail::items_model<T>(items, key);

        auto              node = std::make_unique<TableViewControl>();
        TableViewControl* raw  = node.get();
        apply(row_height, *node, [raw = node.get()](const float& v) { raw->set_row_height(v); });
        apply(size_rows_to_content, *node, [raw = node.get()](const bool& v) { raw->set_size_rows_to_content(v); });
        apply(selection_mode, *node, [raw](const SelectionMode& mode) { raw->set_selection_mode(mode); });

        std::vector<TableViewColumn>                                       headers;
        std::vector<std::function<std::string(const T&)>>                  cells;
        std::vector<std::function<std::weak_ordering(const T&, const T&)>> compares;
        std::vector<std::function<Spec(T&)>>                               templates;
        std::vector<std::function<Spec(T&)>>                               editors;
        bool                                                               custom_order = false;
        bool                                                               any_template = false;
        bool                                                               any_editor   = false;
        std::vector<std::function<std::string(const T&)>>                  tips;
        bool                                                               any_tip = false;
        for (TableColumn<T>& column : columns)
        {
            headers.push_back(
                TableViewColumn{.header          = std::move(column.header),
                                .width           = column.width,
                                .min_width       = column.min_width,
                                .max_width       = column.max_width,
                                .can_resize      = column.can_resize,
                                .can_sort        = column.can_sort,
                                .sort_cycle      = column.sort_cycle,
                                .frozen          = column.frozen,
                                .frozen_trailing = column.frozen_trailing,
                                .read_only       = column.read_only});
            cells.push_back(std::move(column.text));
            custom_order = custom_order || static_cast<bool>(column.compare);
            compares.push_back(std::move(column.compare));
            any_template = any_template || static_cast<bool>(column.cell);
            templates.push_back(std::move(column.cell));
            any_editor = any_editor || static_cast<bool>(column.editing_cell);
            editors.push_back(std::move(column.editing_cell));
            any_tip = any_tip || static_cast<bool>(column.tool_tip);
            tips.push_back(std::move(column.tool_tip));
        }
        node->set_columns(std::move(headers));
        if (item_context_menu)
        {
            node->set_item_context_menu(detail::indexed_function<T>(model.state, std::move(item_context_menu)));
        }
        if (group_header_template)
        {
            node->set_group_header_factory([raw, make = std::move(group_header_template)](std::size_t group) mutable
                                           { return ui::build(make(raw->group_key(group))); });
        }
        if (any_tip)
        {
            node->set_cell_tool_tip(
                [state = model.state, tips = std::move(tips)](std::size_t row, std::size_t column) -> std::string
                {
                    if (column >= tips.size() || !tips[column] || row >= state->size())
                    {
                        return {};
                    }
                    return tips[column](state->at(row));
                });
        }
        if (any_template)
        {
            // A template cell: built from the item, by the row's index into `.items`.
            node->set_cell_factory(
                [state     = model.state,
                 templates = std::move(templates)](std::size_t row, std::size_t column) -> std::unique_ptr<Element>
                {
                    if (column >= templates.size() || !templates[column] || row >= state->size())
                    {
                        return nullptr;
                    }
                    return templates[column](state->at(row)).take();
                });
        }
        // A cell editor works on a draft of the item, which a commit copies back.
        auto draft = std::make_shared<std::unique_ptr<T>>();
        if constexpr (std::copyable<T>)
        {
            if (any_editor)
            {
                node->set_cell_editor_factory(
                    [state = model.state, draft, editors](std::size_t row, std::size_t column) -> std::unique_ptr<Element>
                    {
                        if (column >= editors.size() || !editors[column] || row >= state->size())
                        {
                            return nullptr;
                        }
                        *draft = std::make_unique<T>(state->at(row));
                        return editors[column](**draft).take();
                    });
            }
        }
        if (custom_order)
        {
            // Rows are filled in `.items` order, so a row index is an item index.
            node->set_sort_comparer(
                [raw, state = model.state, compares = std::move(compares)](std::size_t column, std::size_t a, std::size_t b)
                {
                    if (column < compares.size() && compares[column] && a < state->size() && b < state->size())
                    {
                        return compares[column](state->at(a), state->at(b));
                    }
                    const auto& rows = raw->rows();
                    const auto  cell = [&](std::size_t row)
                    { return column < rows[row].size() ? rows[row][column] : std::string{}; };
                    return TableViewControl::compare_text(cell(a), cell(b));
                });
        }

        // The cell text (and group key) of every item, read again whenever the collection reports
        // a change.
        auto fill = [raw, state = model.state, cells, group_by = std::move(group_by)](const CollectionChange* change) mutable
        {
            const auto row_text = [&](std::size_t i)
            {
                std::vector<std::string> row;
                row.reserve(cells.size());
                for (const auto& cell : cells)
                {
                    row.push_back(cell ? cell(state->at(i)) : std::string{});
                }
                return row;
            };
            // A replaced item reads its own row again, not every row: one edit in a long table.
            if (change != nullptr && change->kind == CollectionChangeKind::Replaced)
            {
                for (std::size_t i = change->index; i < change->index + change->count && i < state->size(); ++i)
                {
                    raw->update_row(i, row_text(i), group_by ? std::optional{group_by(state->at(i))} : std::nullopt);
                }
                return;
            }
            std::vector<std::vector<std::string>> rows;
            std::vector<std::string>              keys;
            rows.reserve(state->size());
            for (std::size_t i = 0; i < state->size(); ++i)
            {
                rows.push_back(row_text(i));
                if (group_by)
                {
                    keys.push_back(group_by(state->at(i)));
                }
            }
            raw->set_group_keys(std::move(keys));
            // The items that were selected stay selected; a Reset replaces them all and clears it.
            std::vector<std::size_t> selected;
            if (change != nullptr && change->kind != CollectionChangeKind::Reset)
            {
                selected = raw->selected_rows();
                detail::carry_selection(selected, *change);
            }
            raw->replace_rows(std::move(rows), std::move(selected));
        };
        fill(nullptr);
        if (model.count.reactive())
        {
            node->add_subscription(model.count.subscribe(
                [fill = std::move(fill)](const CollectionChange& change) mutable { fill(&change); }));
        }

        apply(selected_row, *node, [raw](const std::optional<std::size_t>& index) { raw->set_selected_row(index); });
        if (selected_indices.reactive() || !selected_indices.initial().empty())
        {
            apply(selected_indices, *node, [raw](const std::vector<std::size_t>& rows) { raw->set_selected_rows(rows); });
        }
        apply(can_user_sort_columns, *node, [raw](const bool& value) { raw->set_can_user_sort_columns(value); });
        apply(can_user_resize_columns, *node, [raw](const bool& value) { raw->set_can_user_resize_columns(value); });
        apply(grid_lines_visibility,
              *node,
              [raw](const TableViewGridLinesVisibility& value) { raw->set_grid_lines_visibility(value); });
        apply(alternating_rows, *node, [raw](const bool& value) { raw->set_alternating_rows(value); });
        apply(read_only, *node, [raw](const bool& value) { raw->set_read_only(value); });
        apply(grouping_enabled, *node, [raw](const bool& value) { raw->set_grouping_enabled(value); });
        apply(sticky_group_headers, *node, [raw](const bool& value) { raw->set_sticky_group_headers(value); });
        apply(enabled, *node, [raw](const bool& value) { raw->set_enabled(value); });
        apply(alternating_row_background,
              *node,
              [raw = node.get()](const Paint& v) { raw->set_alternating_row_background(v); });
        node->set_empty_content(empty_content.take());

        if (on_beginning_edit)
        {
            node->on_beginning_edit(
                [state = model.state, handler = std::move(on_beginning_edit)](const TableViewCellArgs& args) mutable -> Decision
                {
                    if (args.row >= state->size())
                    {
                        return Cancel;
                    }
                    return handler(TableViewBeginningEditArgs<T>{.item = state->at(args.row), .column = args.column});
                });
        }
        if (on_cell_edited || any_editor)
        {
            node->on_cell_edited(
                [raw, state = model.state, draft, cells, editors = std::move(editors), handler = std::move(on_cell_edited)](
                    const TableViewCellEditArgs& args) mutable -> Decision
                {
                    const std::size_t row    = args.row;
                    const std::size_t column = args.column;
                    std::string&      text   = args.text;
                    if (row >= state->size())
                    {
                        return Cancel;
                    }
                    const bool templated = column < editors.size() && editors[column] && *draft != nullptr;
                    if (templated)
                    {
                        if constexpr (std::copyable<T>)
                        {
                            T& edited = **draft;
                            const std::string shown = column < cells.size() && cells[column] ? cells[column](edited) : text;
                            if (handler &&
                                detail::canceled(
                                    handler(TableViewCellEditedArgs<T>{.item = edited, .column = column, .text = shown})))
                            {
                                return Cancel;
                            }
                            state->at(row) = std::move(edited);
                            draft->reset();
                        }
                    }
                    else if (!handler)
                    {
                        return Proceed; // nowhere to write it: the cell only shows the text
                    }
                    else if (detail::canceled(handler(
                                 TableViewCellEditedArgs<T>{.item = state->at(row), .column = column, .text = text})))
                    {
                        return Cancel;
                    }
                    if (state->live == nullptr && state->write)
                    {
                        state->write(state->copy); // the property now equals the copy: no change reaches back
                    }
                    if constexpr (std::copy_constructible<T>)
                    {
                        // The edit reaches everything else showing the item, and this table reads
                        // the row again from the replacement.
                        if (state->live != nullptr)
                        {
                            T edited = state->at(row);
                            state->live->replace(row, std::move(edited));
                        }
                    }
                    // What the item now shows, the edited cell and any that derive from it.
                    const T& item = state->at(row);
                    for (std::size_t c = 0; c < cells.size(); ++c)
                    {
                        if (!cells[c])
                        {
                            continue;
                        }
                        if (c == column)
                        {
                            text = cells[c](item);
                        }
                        else
                        {
                            raw->set_cell_text(row, c, cells[c](item));
                        }
                    }
                    return Proceed;
                });
        }

        auto changed = write_back(selected_row, detail::indexed_selection<T>(model.state, std::move(on_selection_changed)));
        auto write_rows = shared_writer(selected_indices);
        if (changed || write_rows)
        {
            node->on_selection_changed(
                [raw, changed = std::move(changed), write_rows](std::optional<std::size_t> row) mutable
                {
                    if (write_rows)
                    {
                        (*write_rows)(raw->selected_rows());
                    }
                    if (changed)
                    {
                        changed(row);
                    }
                });
        }
        if (on_sorting)
        {
            node->on_sorting(std::move(on_sorting));
        }
        if (on_sorted)
        {
            node->on_sorted(std::move(on_sorted));
        }
        if (on_column_resized)
        {
            node->on_column_resized(std::move(on_column_resized));
        }

        apply_common(*this, *node);
        publish_control(control, raw);
        return node;
    }
};

} // namespace ui
