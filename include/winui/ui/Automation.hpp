// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <cstddef>

namespace ui
{

class Element;

/// What kind of thing a control is, as reported to accessibility clients such as screen readers.
/// The default, `Group`, is right for every plain container. A `Group` node still appears in the
/// accessibility tree, as a grouping node with no interaction.
///
/// A control reports its role by overriding Element::automation_role(), and what it lets a
/// client do through the provider interfaces it lists in Element::automation_providers().
///
/// Example:
/// @code
/// // A custom rating control exposed like a Slider to screen readers.
/// class Stars final : public ui::Element, private ui::RangeValueProvider
/// {
/// public:
///     ui::AutomationRole automation_role() const override { return ui::AutomationRole::Slider; }
///
/// protected:
///     void automation_providers(ui::AutomationProviders& out) override { out.range_value = this; }
///     // measure_override()/render_override() omitted
///
/// private:
///     double range_value() const override { return rating_; }
///     double range_minimum() const override { return 0.0; }
///     double range_maximum() const override { return 5.0; }
///     double range_small_change() const override { return 1.0; }
///     bool   range_read_only() const override { return false; }
///     bool   set_range_value(double value) override { rating_ = value; return true; }
///
///     double rating_ = 3.0;
/// };
/// @endcode
enum class AutomationRole
{
    Group,       ///< Generic container with no more specific semantics (StackControl, Grid, ...).
    Pane,        ///< A scrollable/major content region (Scroll, the window's own root).
    Button,      ///< A push button.
    CheckBox,    ///< A two- or three-state check box.
    RadioButton, ///< One option of a mutually exclusive group.
    Edit,        ///< Single/multi-line text entry (TextBox, NumberBox's edit part, ...).
    Text,        ///< Static, read-only text (TextControl).
    Image,       ///< A picture or icon (Image, AnimatedIcon).
    List,        ///< A list of items.
    ListItem,    ///< One entry of a List.
    ComboBox,    ///< A drop-down selector (ComboBox).
    Slider,      ///< A control that sets a bounded value (Slider, AnnotatedScrollBar).
    ProgressBar, ///< A progress indicator.
    Tab,         ///< The tab strip/container (Tabs, NavigationShell, SelectorBar).
    TabItem,     ///< One tab of a Tab.
    Tree,        ///< A hierarchical list (TreeView).
    TreeItem,    ///< One node of a Tree.
    Menu,        ///< A popup menu of commands.
    MenuItem,    ///< One command of a Menu or MenuBar.
    MenuBar,     ///< A horizontal bar of top-level menus.
    ToolBar,     ///< A bar of commands.
    Hyperlink,   ///< A navigation link.
    Document,    ///< A rich, multi-paragraph text surface.
    Header,      ///< A row of column headers.
    HeaderItem,  ///< One column header of a Header.
    Separator,   ///< A visual divider with no interaction (AppBarSeparator).
    SpinButton,  ///< An up/down value stepper.
    Thumb,       ///< A draggable divider/handle with no bounded value (Splitter); Slider has one.
    SplitButton, ///< A button with a second part that opens a flyout (SplitButton, ToggleSplitButton).
    DataGrid,    ///< A table of rows and columns (TableView).
    DataItem,    ///< One row of a data grid (TableView).
    Custom,      ///< Something with real semantics that doesn't map to any role above.
    TitleBar,    ///< A window's caption with its buttons, when the theme draws it (Frame::Themed).
    Calendar,    ///< A month calendar (CalendarView): a grid of days with Selection and Grid patterns.
};

/// The Toggle pattern's three-valued state, reported by ToggleProvider::toggle_value().
enum class ToggleState
{
    Off,           ///< Unchecked / off.
    On,            ///< Checked / on.
    Indeterminate, ///< Neither, e.g. a three-state toggle's middle state.
};

// ---------------------------------------------------------------------------------------
// Provider interfaces, one per pattern. A control privately inherits the ones it implements,
// overrides every member privately, and lists them in Element::automation_providers(). Most
// members are pure virtual, so an incomplete provider does not compile. Member names start with
// the pattern's name, so one control can implement two patterns (NumberBox: RangeValue and Value).
// ---------------------------------------------------------------------------------------

/// The Invoke pattern: a single parameterless action, as a button has. Implement every member.
class InvokeProvider
{
public:
    /// Run the action as a click would; true when it ran (false when disabled).
    virtual bool invoke() = 0;

protected:
    ~InvokeProvider() = default;
};

/// The Toggle pattern: an on, off or indeterminate state a click advances, as a check box has.
/// Implement every member.
class ToggleProvider
{
public:
    /// The current state.
    [[nodiscard]] virtual ToggleState toggle_value() const = 0;
    /// Advance to the next state as a click would; true when it did (false when disabled).
    virtual bool toggle_next() = 0;

protected:
    ~ToggleProvider() = default;
};

/// The SelectionItem pattern: one selectable entry of a Selection container (a radio button, a
/// list item). Implement item_selected() and select_item(); an entry of a container that selects
/// several at once also overrides add_to_selection() and remove_from_selection().
class SelectionItemProvider
{
public:
    /// Whether this entry is selected.
    [[nodiscard]] virtual bool item_selected() const = 0;
    /// Make this the selected entry; true when it did.
    virtual bool select_item() = 0;
    /// Adds this entry to the selection and keeps the others; true when it did. The default is
    /// select_item(), for a container that selects one entry at a time.
    virtual bool add_to_selection()
    {
        return select_item();
    }
    /// Takes this entry out of the selection; true when it did. The default refuses, for an
    /// entry that is deselected only by selecting another one.
    virtual bool remove_from_selection()
    {
        return false;
    }

protected:
    ~SelectionItemProvider() = default;
};

/// The Selection pattern: a container whose entries are SelectionItems (a list, a Carousel).
/// Implement every member.
class SelectionProvider
{
public:
    /// Appends the selected entries' nodes to `out`. Only entries that exist as nodes can be
    /// reported, so a virtualizing list lists the selected entries it has realized.
    virtual void selected_items(std::vector<Element*>& out) const = 0;
    /// Whether more than one entry can be selected at once.
    [[nodiscard]] virtual bool can_select_multiple() const = 0;
    /// Whether the control always keeps one entry selected.
    [[nodiscard]] virtual bool selection_required() const = 0;

protected:
    ~SelectionProvider() = default;
};

/// The ExpandCollapse pattern: a collapsed or expanded state (an Expander, a ComboBox, a
/// DropdownButton). Implement every member.
class ExpandCollapseProvider
{
public:
    /// Whether it is expanded (open).
    [[nodiscard]] virtual bool expanded_state() const = 0;
    /// Expand (true) or collapse (false); true when it acted (false when disabled).
    virtual bool set_expanded_state(bool expanded) = 0;

protected:
    ~ExpandCollapseProvider() = default;
};

/// The RangeValue pattern: a bounded number (a Slider, a ProgressBar, a RatingControl), in the
/// control's own units. Implement every member.
class RangeValueProvider
{
public:
    /// The value.
    [[nodiscard]] virtual double range_value() const = 0;
    /// The lowest value.
    [[nodiscard]] virtual double range_minimum() const = 0;
    /// The highest value.
    [[nodiscard]] virtual double range_maximum() const = 0;
    /// The step an arrow key makes.
    [[nodiscard]] virtual double range_small_change() const = 0;
    /// True when a client cannot set it.
    [[nodiscard]] virtual bool range_read_only() const = 0;
    /// Set the value (the control clamps it); true when it did (false when disabled or read-only).
    virtual bool set_range_value(double value) = 0;

protected:
    ~RangeValueProvider() = default;
};

/// The Value pattern: a text value (a TextBox's contents, a read-only ComboBox's selected item).
/// Implement every member.
class ValueProvider
{
public:
    /// The text.
    [[nodiscard]] virtual std::string text_value() const = 0;
    /// True when a client cannot set it.
    [[nodiscard]] virtual bool text_value_read_only() const = 0;
    /// Replace the text; true when it did (false when read-only or disabled).
    virtual bool set_text_value(std::string_view text) = 0;

protected:
    ~ValueProvider() = default;
};

/// The Scroll pattern: how far a view is scrolled (Scroll, VirtualList, TableView). Implement
/// every member.
class ScrollProvider
{
public:
    /// Percent scrolled horizontally in [0, 100], or -1 when that axis does not scroll (0 is
    /// scrollable, at the start).
    [[nodiscard]] virtual float scroll_horizontal_percent() const = 0;
    /// As scroll_horizontal_percent(), for the vertical axis.
    [[nodiscard]] virtual float scroll_vertical_percent() const = 0;
    /// Scroll each axis to a percentage in [0, 100]; -1 leaves that axis alone. True when it
    /// scrolled.
    virtual bool set_scroll_percent(float horizontal, float vertical) = 0;

protected:
    ~ScrollProvider() = default;
};

/// The Grid pattern: cells addressed by row and column (a TableView). Implement every member.
class GridProvider
{
public:
    /// How many rows.
    [[nodiscard]] virtual std::size_t grid_row_count() const = 0;
    /// How many columns.
    [[nodiscard]] virtual std::size_t grid_column_count() const = 0;
    /// The node of the cell at `row` and `column`, or nullptr when the cell has no node, such as
    /// a cell of a virtualized row out of view.
    [[nodiscard]] virtual Element* grid_item(std::size_t row, std::size_t column) const = 0;

protected:
    ~GridProvider() = default;
};

/// The GridItem pattern: one cell of a Grid. Implement every member.
class GridItemProvider
{
public:
    /// The cell's row.
    [[nodiscard]] virtual std::size_t grid_item_row() const = 0;
    /// The cell's column.
    [[nodiscard]] virtual std::size_t grid_item_column() const = 0;
    /// The node providing the Grid this cell is in.
    [[nodiscard]] virtual Element* grid_item_container() const = 0;

protected:
    ~GridItemProvider() = default;
};

/// The Table pattern: a Grid with column headers. Implement every member.
class TableProvider
{
public:
    /// Appends the column headers' nodes to `out`. A table is read row by row and has no row
    /// headers.
    virtual void table_column_headers(std::vector<Element*>& out) const = 0;

protected:
    ~TableProvider() = default;
};

/// The TableItem pattern: one cell of a Table, which knows its column header. Implement every
/// member.
class TableItemProvider
{
public:
    /// Appends the cell's column header node to `out`.
    virtual void table_item_column_headers(std::vector<Element*>& out) const = 0;

protected:
    ~TableItemProvider() = default;
};

/// What a node lets an accessibility client do: one pointer per pattern, null for a pattern it
/// does not support. Element::automation_providers() fills it.
///
/// Example:
/// @code
/// // Click a button the way a screen reader does.
/// ui::ButtonControl button{"Save"};
/// if (ui::InvokeProvider* invoke = ui::automation_providers(button).invoke)
/// {
///     invoke->invoke();
/// }
/// @endcode
struct AutomationProviders
{
    InvokeProvider*         invoke          = nullptr; ///< The Invoke pattern.
    ToggleProvider*         toggle          = nullptr; ///< The Toggle pattern.
    SelectionItemProvider*  selection_item  = nullptr; ///< The SelectionItem pattern.
    SelectionProvider*      selection       = nullptr; ///< The Selection pattern.
    ExpandCollapseProvider* expand_collapse = nullptr; ///< The ExpandCollapse pattern.
    RangeValueProvider*     range_value     = nullptr; ///< The RangeValue pattern.
    ValueProvider*          value           = nullptr; ///< The Value pattern.
    ScrollProvider*         scroll          = nullptr; ///< The Scroll pattern.
    GridProvider*           grid            = nullptr; ///< The Grid pattern.
    GridItemProvider*       grid_item       = nullptr; ///< The GridItem pattern.
    TableProvider*          table           = nullptr; ///< The Table pattern.
    TableItemProvider*      table_item      = nullptr; ///< The TableItem pattern.
};

} // namespace ui
