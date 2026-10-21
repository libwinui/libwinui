// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panels/Panel.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ui
{

/// A grid of equal cells that children fill in order, each spanning as many rows and columns of
/// cells as it asks for, wrapping to a new line when one is full.
///
/// The cell is item_width() x item_height(), each taken from the first child when unset. Children
/// go first-fit into the free cells, so a small child fills a hole a larger one left behind. A
/// child bigger than the lines is clipped to them. Children that don't fit anywhere are not
/// placed and get an empty rect. The panel asks for the cells it used, plus any alignment
/// offsets. Use it for tile layouts such as a start screen or a photo mosaic. For many items,
/// prefer a virtualizing TileLayoutEngine.
///
/// Example:
/// @code
/// auto grid = std::make_unique<ui::TilePanelControl>();
/// grid->set_orientation(ui::Orientation::Horizontal);
/// grid->set_item_width(80.0f);
/// grid->set_item_height(80.0f);
/// grid->set_maximum_rows_or_columns(4);
/// grid->add(std::make_unique<ui::ButtonControl>("Mail"), 2, 2);   // a 2x2 tile
/// grid->add(std::make_unique<ui::ButtonControl>("Photos"), 1, 2); // one row, two columns
/// grid->add(std::make_unique<ui::ButtonControl>("Maps"));
///
/// // Or declaratively:
/// window.set_content(ui::TilePanel{
///     .orientation             = ui::Orientation::Horizontal,
///     .item_width              = 80.0f,
///     .item_height             = 80.0f,
///     .maximum_rows_or_columns = 4,
///     .content                 = {
///         ui::TilePanelItem{.row_span = 2, .column_span = 2, .content = ui::Button{.text = "Mail"}},
///         ui::TilePanelItem{.column_span = 2, .content = ui::Button{.text = "Photos"}},
///         ui::TilePanelItem{.content = ui::Button{.text = "Maps"}},
///     },
/// });
/// @endcode
class TilePanelControl final : public Panel
{
public:
    /// An empty grid: Vertical, cells sized from the first child, no line cap, Leading alignment.
    TilePanelControl() = default;

    /// Takes ownership of a child spanning `row_span` x `column_span` cells (0 counts as 1),
    /// appends it after the existing children, and returns a non-owning observer to it.
    template <typename T>
    T* add(std::unique_ptr<T> child, int row_span = 1, int column_span = 1)
    {
        static_assert(std::is_base_of_v<Element, T>, "TilePanel children must be Elements");
        T* observer = child.get();
        if (adopt_child(std::move(child)) != nullptr)
        {
            spans_.push_back(Span{row_span, column_span});
        }
        return observer;
    }
    /// The declarative overload: builds `spec` (or takes the node a ui::Spec holds) and adds it as
    /// add() does a control. Returns the built node as an Element, or nullptr for an empty
    /// ui::Spec, which adds nothing; a spec's `.control` field gives a typed observer.
    template <SpecLike S>
    Element* add(S&& spec, int row_span = 1, int column_span = 1)
    {
        auto node = std::forward<S>(spec).build();
        return node != nullptr ? add(std::move(node), row_span, column_span) : nullptr;
    }

    /// Changes how many cell rows `child` covers and updates the layout on a change. Ignored
    /// when `child` is not one of this grid's children.
    void set_row_span(const Element* child, int span);
    /// Changes how many cell columns `child` covers.
    void set_column_span(const Element* child, int span);
    /// The row span given for `child`, or 1 when it is not a child of this grid.
    [[nodiscard]] int row_span(const Element* child) const;
    /// The column span given for `child`, or 1 when it is not a child of this grid.
    [[nodiscard]] int column_span(const Element* child) const;

    /// Orientation: Vertical (the default) fills a column top to bottom before moving right.
    [[nodiscard]] Orientation orientation() const
    {
        return orientation_;
    }
    /// Changes the fill direction and updates the layout.
    void set_orientation(Orientation orientation)
    {
        orientation_ = orientation;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The cell width in DIPs; nullopt (the default) takes the first child's desired size on
    /// that axis.
    [[nodiscard]] std::optional<float> item_width() const
    {
        return item_width_;
    }
    /// Sets or clears the cell width and updates the layout.
    void set_item_width(OptionalFloat width)
    {
        item_width_ = width;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The cell height in DIPs; see item_width().
    [[nodiscard]] std::optional<float> item_height() const
    {
        return item_height_;
    }
    /// Sets or clears the cell height and updates the layout.
    void set_item_height(OptionalFloat height)
    {
        item_height_ = height;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The most cells in a line; 0 (the default) is no cap.
    [[nodiscard]] std::size_t maximum_rows_or_columns() const
    {
        return maximum_rows_or_columns_;
    }
    /// Caps the cells per line and updates the layout.
    void set_maximum_rows_or_columns(std::size_t maximum)
    {
        maximum_rows_or_columns_ = maximum;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Where the block of cells sits horizontally; Leading (Left/Top) by default.
    /// In a panel larger than the used cells, Center and Trailing offset the whole block and
    /// Stretch spreads the lines apart.
    [[nodiscard]] Alignment horizontal_children_alignment() const
    {
        return horizontal_children_alignment_;
    }
    /// Changes the horizontal alignment and updates the layout.
    void set_horizontal_children_alignment(Alignment alignment)
    {
        horizontal_children_alignment_ = alignment;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The vertical alignment of the cell block; see horizontal_children_alignment().
    [[nodiscard]] Alignment vertical_children_alignment() const
    {
        return vertical_children_alignment_;
    }
    /// Changes the vertical alignment and updates the layout.
    void set_vertical_children_alignment(Alignment alignment)
    {
        vertical_children_alignment_ = alignment;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The size of one cell from the last measure.
    [[nodiscard]] Size cell_size() const
    {
        return item_size_;
    }
    /// Cells per line from the last measure.
    [[nodiscard]] std::size_t items_per_line() const
    {
        return items_per_line_;
    }
    /// Lines used by the last measure.
    [[nodiscard]] std::size_t line_count() const
    {
        return line_count_;
    }
    /// True: measuring touches only the panel and its children.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;


private:
    struct Span
    {
        int rows    = 1;
        int columns = 1;
    };
    // OccupancyMap: `max_row` cells along a line by `max_col` lines, each holding the 1-based child
    // index (0 empty). "Row" is the position within a line whatever the orientation.
    struct OccupancyMap
    {
        std::size_t                max_row    = 0;
        std::size_t                max_col    = 0;
        bool                       horizontal = false;
        std::vector<std::uint32_t> cells;
        struct Location
        {
            std::size_t row = 0;
            std::size_t col = 0;
        };
        std::vector<Location> locations;
        std::size_t           element_count = 0;
        std::size_t           current_row   = 0;
        std::size_t           current_col   = 0;

        [[nodiscard]] std::uint32_t at(std::size_t row, std::size_t col) const
        {
            return cells[row * max_col + col];
        }
        void set(std::size_t row, std::size_t col, std::uint32_t value)
        {
            cells[row * max_col + col] = value;
        }
        bool               find_next_available(std::size_t row_span, std::size_t column_span);
        void               fill_current(std::size_t row_span, std::size_t column_span, std::uint32_t value);
        void               used_size(std::size_t& lines, std::size_t& per_line) const;
        [[nodiscard]] Rect item_rect(std::size_t index, Size item) const;
    };

    [[nodiscard]] Span span_of(std::size_t index) const;
    // The block's alignment offsets within `available`.
    void compute_alignment(Size available, Size& starting, Size& justification) const;

    std::vector<Span>    spans_; // parallel to children_
    Orientation          orientation_ = Orientation::Vertical;
    std::optional<float> item_width_;
    std::optional<float> item_height_;
    std::size_t          maximum_rows_or_columns_       = 0;
    Alignment            horizontal_children_alignment_ = Alignment::Leading;
    Alignment            vertical_children_alignment_   = Alignment::Leading;

    mutable OccupancyMap map_;
    mutable Size         item_size_{};
    mutable std::size_t  items_per_line_ = 0;
    mutable std::size_t  line_count_     = 0;
};

/// One child of a TilePanel with the cell rows and columns it spans.
/// Written inline in the grid's `.content`; see ui::TilePanel for an example.
struct TilePanelItem
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    int                                               row_span    = 1; ///< Cells covered along the column axis.
    int                                               column_span = 1; ///< Cells covered along the row axis.
    Spec                                              content{};       ///< The placed child.
};

/// A TilePanel's children, built, in the same shape as CellList. Callers write it as a braced list of
/// `ui::TilePanelItem{...}`.
class TilePanelItemList
{
public:
    /// One built child with its spans; the node is already owned by the list.
    struct Placed
    {
        int                      row_span;    ///< Cell rows covered.
        int                      column_span; ///< Cell columns covered.
        std::unique_ptr<Element> node;        ///< The built child.
    };

    /// An empty list.
    TilePanelItemList() = default;
    /// Builds every item's content and takes ownership of it.
    template <typename... S>
        requires(sizeof...(S) > 0 && (std::same_as<S, TilePanelItem> && ...))
    TilePanelItemList(S&&... items) // NOLINT(google-explicit-constructor)
    {
        items_.reserve(sizeof...(S));
        (add(std::move(items)), ...);
    }
    TilePanelItemList(const TilePanelItemList&)            = delete;
    TilePanelItemList& operator=(const TilePanelItemList&) = delete;
    TilePanelItemList(TilePanelItemList&&)                 = default;
    TilePanelItemList& operator=(TilePanelItemList&&)      = default;

    /// The placed children, in fill order.
    std::vector<Placed> items_;

    /// Builds `item`'s content and appends it, as one more entry of the braced list would.
    void add(TilePanelItem item)
    {
        items_.push_back(Placed{item.row_span, item.column_span, item.content.take()});
    }
};

/// Equal cells filled in order, children spanning several. Builds a ui::TilePanelControl.
///
/// Example:
/// @code
/// ui::Spec tile(ui::Color c) { return ui::Border{.background = c}; }
///
/// window.set_content(ui::TilePanel{
///     .orientation             = ui::Orientation::Horizontal,
///     .item_width              = 44.0f,
///     .item_height             = 44.0f,
///     .maximum_rows_or_columns = 3,
///     .content                 = {
///         ui::TilePanelItem{.content = tile(ui::colors::Black)},
///         ui::TilePanelItem{.row_span = 2, .content = tile(ui::colors::White)},
///         ui::TilePanelItem{.column_span = 2, .content = tile(ui::Color::from_rgba(0, 120, 215))},
///     },
/// });
/// @endcode
struct TilePanel
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<TilePanel>` points at.
    using control_type = TilePanelControl;

    // ---- properties ----
    /// Vertical (the default) fills columns top to bottom, then wraps right; Horizontal fills rows.
    Binding<Orientation> orientation{Orientation::Vertical};
    /// The cell width, DIPs; unset takes the first child's width.
    Binding<std::optional<float>> item_width{};
    /// The cell height, DIPs; unset takes the first child's height.
    Binding<std::optional<float>> item_height{};
    /// Cells per line before wrapping; 0 is no cap.
    Binding<std::size_t> maximum_rows_or_columns{0};
    /// Where the block of cells sits horizontally; see TilePanelControl::horizontal_children_alignment().
    Binding<Alignment> horizontal_children_alignment{Alignment::Leading};
    /// Where the block of cells sits vertically.
    Binding<Alignment> vertical_children_alignment{Alignment::Leading};
    /// Background: a Color, or a ThemeColor read from the theme; empty (the default) draws none.
    Binding<Paint> background{};

    // ---- appearance / layout ----
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
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool> enabled{true}; ///< False disables everything inside: no input, the disabled look, out of the tab ring.
    Accessibility accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
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
    ControlOut<TilePanel> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The children with their spans; see ui::TilePanelItem.
    TilePanelItemList content{};

    /// Consumes the spec and returns the built ui::TilePanelControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
