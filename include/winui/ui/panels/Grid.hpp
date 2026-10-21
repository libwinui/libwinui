// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cassert>
#include <cstddef>

namespace ui
{

/// How a Grid track's GridLength::value is read.
enum class GridUnit
{
    Auto,     ///< sized to the largest single-span child in the track; `value` is ignored
    Fraction, ///< a weighted share of the space left after Dip and Auto tracks; `value` is the weight
    Dip,      ///< a fixed size; `value` is DIPs
};

/// The size of one Grid row or column: Auto, a fraction of the remaining space, or fixed DIPs,
/// optionally kept between a smallest and a largest size. Defaults to one fraction.
///
/// In a spec, the literals `ui::AutoSize`, `1_fr` and `48_dip` are shorthand for auto_(),
/// fraction() and dip(), and `ui::minmax(200_dip, 1_fr)` adds the limits.
///
/// Example:
/// @code
/// auto grid = std::make_unique<ui::GridControl>();
/// grid->add_column(ui::GridLength::dip(200.0f));     // a 200-DIP sidebar
/// grid->add_column(ui::GridLength::fraction());      // the rest
/// grid->add_row(ui::GridLength::auto_());            // a header sized to its content
/// grid->add_row(ui::GridLength::fraction(2.0f));     // twice the share of a fraction() row
/// grid->add_column(ui::minmax(ui::GridLength::dip(120.0f), ui::GridLength::fraction())); // never under 120
/// @endcode
struct GridLength
{
    /// How `value` is read.
    GridUnit unit = GridUnit::Fraction;
    /// The Fraction weight or the Dip size in DIPs; ignored for Auto.
    float value = 1.0f;
    /// The smallest the track gets, DIPs; 0 by default.
    float min = 0.0f;
    /// The largest the track gets, DIPs; ui::Unbounded (no limit) by default.
    float max = Unbounded;

    /// An Auto track, sized to its content.
    [[nodiscard]] static constexpr GridLength auto_()
    {
        return GridLength{GridUnit::Auto, 0.0f};
    }
    /// A fraction of the remaining space, weighted by `weight`: a fraction(2) track gets twice
    /// what a fraction(1) track does.
    [[nodiscard]] static constexpr GridLength fraction(float weight = 1.0f)
    {
        return GridLength{GridUnit::Fraction, weight};
    }
    /// A fixed track of `dips` DIPs.
    [[nodiscard]] static constexpr GridLength dip(float dips)
    {
        return GridLength{GridUnit::Dip, dips};
    }

    /// Memberwise equality.
    friend bool operator==(const GridLength&, const GridLength&) = default;
};

/// A Grid track kept between two sizes, as CSS's minmax(): `ui::minmax(200_dip, 1_fr)` is a
/// fraction of the remaining space that never gets narrower than 200 DIPs, and
/// `ui::minmax(ui::AutoSize, 300_dip)` sizes to its content up to 300 DIPs.
///
/// `smallest` is a Dip length, which sets the lower limit, or Auto, which sets none. `largest`
/// decides how the track is sized. When both are Dip lengths, the track takes a fraction of the
/// remaining space, kept between the two. The limits also hold while a Splitter resizes the
/// track.
///
/// Example:
/// @code
/// using namespace ui::literals;
/// ui::Grid{.columns = {ui::minmax(160_dip, 1_fr), 6_dip, 2_fr}, .content = {/* ... */}};
/// @endcode
[[nodiscard]] constexpr GridLength minmax(GridLength smallest, GridLength largest)
{
    GridLength track = largest;
    if (smallest.unit == GridUnit::Dip)
    {
        track.min = smallest.value;
    }
    if (largest.unit == GridUnit::Dip && smallest.unit == GridUnit::Dip)
    {
        // Between two fixed sizes: a share of the space, limited both ways.
        track = GridLength{GridUnit::Fraction, 1.0f, smallest.value, largest.value};
    }
    else if (largest.unit == GridUnit::Dip)
    {
        // Auto up to a fixed size.
        track = GridLength{GridUnit::Auto, 0.0f, 0.0f, largest.value};
    }
    return track;
}

/// A two-dimensional layout panel: children placed in cells of row and column tracks, each track
/// Auto, a fraction of the remaining space, or fixed DIPs.
///
/// Measure sizes Dip tracks first, then Auto tracks from their single-span children, then
/// splits what is left across the Fraction tracks by weight. Arrange sizes the tracks again for
/// the final rect, so Fraction tracks share the room the Grid is given. Every track stays within
/// its min and max (ui::minmax()). On an unbounded axis a Fraction track behaves like Auto.
/// An axis with no tracks has one implicit Fraction track, so a Grid without definitions layers
/// every child in one cell. A child spanning several Auto tracks does not grow them. A border and
/// padding inset the cells. Tracks can be resized at runtime with set_row_height() and
/// set_column_width(). A Splitter in a track of its own between two others resizes them itself.
///
/// Example:
/// @code
/// // Declarative: header, a sidebar beside the content, footer.
/// using ui::AutoSize;
/// using namespace ui::literals;
/// window.set_content(ui::Grid{
///     .columns        = {200_dip, 1_fr},
///     .rows           = {ui::AutoSize, 1_fr, ui::AutoSize},
///     .column_spacing = 8.0f,
///     .content = {
///         ui::Cell{.row = 0, .column = 0, .column_span = 2, .content = ui::Text{.text = "Header"}},
///         ui::Cell{.row = 1, .column = 0, .content = ui::Text{.text = "Sidebar"}},
///         ui::Cell{.row = 1, .column = 1, .content = ui::Text{.text = "Content"}},
///         ui::Cell{.row = 2, .column = 0, .column_span = 2, .content = ui::Text{.text = "Footer"}},
///     },
/// });
///
/// // Imperative:
/// auto grid = std::make_unique<ui::GridControl>();
/// grid->add_column(ui::GridLength::auto_());
/// grid->add_column(ui::GridLength::fraction());
/// grid->add_row(ui::GridLength::auto_());
/// grid->set_column_spacing(12.0f);
/// grid->add(std::make_unique<ui::TextControl>("Name:"), 0, 0);
/// grid->add(std::make_unique<ui::TextBoxControl>(), 0, 1);
/// window.set_content(std::move(grid));
/// @endcode
class GridControl final : public Element
{
public:
    /// An empty grid with no track definitions (one implicit Fraction cell).
    GridControl() = default;

    /// Appends a row track and updates the layout.
    void add_row(GridLength length)
    {
        rows_.push_back(length);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// Appends a column track and updates the layout.
    void add_column(GridLength length)
    {
        columns_.push_back(length);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// How many rows were added (0 means one implicit Fraction row).
    [[nodiscard]] std::size_t row_count() const
    {
        return rows_.size();
    }
    /// How many columns were added (0 means one implicit Fraction column).
    [[nodiscard]] std::size_t column_count() const
    {
        return columns_.size();
    }
    /// Row track `index`'s definition; throws std::out_of_range past row_count().
    [[nodiscard]] const GridLength& row(std::size_t index) const
    {
        return rows_.at(index);
    }
    /// Column track `index`'s definition; throws std::out_of_range past column_count().
    [[nodiscard]] const GridLength& column(std::size_t index) const
    {
        return columns_.at(index);
    }

    /// Switches a track to a fixed size in DIPs at runtime, keeping its min and max. A custom
    /// Splitter handler drives it from on_drag_delta (ui/panes/Splitter.hpp). An index past the
    /// current track count is ignored, so a drag handler need not guard against a grid with fewer
    /// tracks.
    void set_row_height(std::size_t index, float dips);
    /// The column counterpart of set_row_height().
    void set_column_width(std::size_t index, float dips);

    /// Moves `delta` DIPs from the track after `splitter`'s to the track before it (a negative
    /// delta the other way), within both tracks' min and max, so the two keep their total.
    /// `splitter` must be a child of this grid in a track of its own, span 1, with a track on
    /// either side: the columns for a Vertical bar, the rows for a Horizontal one. Two Fraction
    /// tracks trade weight; any other track becomes a Dip track at its new size. Returns the
    /// delta applied after the limits, or nullopt (and nothing changes) when `splitter` is not
    /// placed that way. SplitterControl calls this.
    std::optional<float> resize_beside(const Element* splitter, Orientation orientation, float delta);

    /// A track's current size in DIPs: the literal value for a Dip track (so it reflects a
    /// set_row_height()/set_column_width() call immediately, before any layout pass has run
    /// on it), otherwise the size the last layout resolved for it. 0 before the first
    /// layout, or for an out-of-range index.
    ///
    /// A Splitter or ResizeGripper drag handler must add its delta to this, not to an occupant's
    /// `bounds()`. A child's bounds are its inner rect, already shrunk by its own margin and
    /// focus-ring outset (Element::arrange). Feeding `child->bounds().width + delta` into
    /// set_column_width() takes that band off again on every drag event, so the track collapses
    /// on the first one and never grows back.
    [[nodiscard]] float row_size(std::size_t index) const
    {
        if (index >= rows_.size())
        {
            return 0.0f;
        }
        return rows_[index].unit == GridUnit::Dip
                   ? (std::max)(rows_[index].min, (std::min)(rows_[index].value, rows_[index].max))
                   : (index < row_sizes_.size() ? row_sizes_[index] : 0.0f);
    }
    /// The column counterpart of row_size().
    [[nodiscard]] float column_size(std::size_t index) const
    {
        if (index >= columns_.size())
        {
            return 0.0f;
        }
        return columns_[index].unit == GridUnit::Dip
                   ? (std::max)(columns_[index].min, (std::min)(columns_[index].value, columns_[index].max))
                   : (index < column_sizes_.size() ? column_sizes_[index] : 0.0f);
    }

    /// Takes ownership of a child and returns a non-owning observer to it, as StackControl::add()
    /// does. `row`/`column` index the tracks added via
    /// add_row()/add_column(); an index past the last track places the child in the last one,
    /// and a span stops at the last track. Spans below 1 are raised to 1.
    template <typename T>
    T* add(std::unique_ptr<T> child, std::size_t row, std::size_t column, std::size_t row_span = 1, std::size_t column_span = 1)
    {
        static_assert(std::is_base_of_v<Element, T>, "Grid children must be Elements");
        assert(child != nullptr && "a Grid child must not be null");
        T* observer = child.get();
        set_parent_of(*observer, this);
        children_.push_back(
            ChildSlot{std::move(child), row, column, (std::max<std::size_t>)(1, row_span), (std::max<std::size_t>)(1, column_span)});
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The declarative overload: builds `spec` (or takes the node a ui::Spec holds) and adds it as
    /// add() does a control. Returns the built node as an Element, or nullptr for an empty
    /// ui::Spec, which adds nothing; a spec's `.control` field gives a typed observer.
    template <SpecLike S>
    Element* add(S&& spec, std::size_t row, std::size_t column, std::size_t row_span = 1, std::size_t column_span = 1)
    {
        auto node = std::forward<S>(spec).build();
        return node != nullptr ? add(std::move(node), row, column, row_span, column_span) : nullptr;
    }

    /// Takes `child` out of the grid alive and hands it back, for adding somewhere else. Focus
    /// inside it is cleared. Null when it is not one of this grid's children.
    [[nodiscard]] std::unique_ptr<Element> orphan(const Element* child);

    /// Moves a child to another cell and span. An element that is not a child of this grid is
    /// ignored.
    void set_cell(const Element* child, std::size_t row, std::size_t column, std::size_t row_span = 1, std::size_t column_span = 1);
    /// Moves `child` to `row`, keeping its column and spans.
    void set_row(const Element* child, std::size_t row);
    /// Moves `child` to `column`, keeping its row and spans.
    void set_column(const Element* child, std::size_t column);

    /// The color filled behind the children; nullopt (the default) draws nothing.
    /// A ThemeColor is a theme resource, re-read whenever it is drawn (ui/BackgroundBrush.hpp).
    [[nodiscard]] const std::optional<Color>& background() const
    {
        return background_.color();
    }
    /// The background as a theme resource, or nullptr when a fixed color (or nothing) is set.
    [[nodiscard]] ThemeColor background_resource() const
    {
        return background_.resource();
    }
    /// Sets a fixed background color (nullopt for none); redraws.
    void set_background(std::optional<Color> color)
    {
        background_.set(color);
        mark_dirty(DirtyFlags::Visual);
    }
    /// Sets the background to a theme resource; redraws.
    void set_background(ThemeColor resource)
    {
        background_.set(resource);
        mark_dirty(DirtyFlags::Visual);
    }
    /// With a transition set, a later change of what the background draws (a new color, or a
    /// resource under a new theme) blends from the color on screen (Animation.hpp's
    /// BrushTransition). nullopt (the default) changes it at once.
    [[nodiscard]] const std::optional<BrushTransition>& background_transition() const
    {
        return background_.transition();
    }
    /// Sets or clears the background transition.
    void set_background_transition(std::optional<BrushTransition> transition)
    {
        background_.set_transition(transition);
    }

    // ---- border and padding ----
    /// The outline color; nullopt (the default) draws no border even with a non-zero
    /// border_thickness(). Nullopt as well while a ThemeColor is set.
    [[nodiscard]] std::optional<Color> border_brush() const
    {
        return border_brush_.color();
    }
    /// The theme resource the border is drawn from, or null.
    [[nodiscard]] ThemeColor border_brush_resource() const
    {
        return border_brush_.resource();
    }
    /// A fixed outline color; replaces any resource. Redraws.
    void set_border_brush(std::optional<Color> color)
    {
        border_brush_ = color;
        mark_dirty(DirtyFlags::Visual);
    }
    /// An outline color read from the theme each time it is drawn; replaces any fixed color.
    /// Redraws.
    void set_border_brush(ThemeColor resource)
    {
        border_brush_ = resource;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The border width in DIPs per side (default zero); it insets the children like padding.
    [[nodiscard]] const Thickness& border_thickness() const
    {
        return border_thickness_;
    }
    /// Sets the border width; updates the layout and redraws.
    void set_border_thickness(const Thickness& thickness)
    {
        border_thickness_ = thickness;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The corner radius in DIPs, uniform for all four corners (default 0, square). It rounds the
    /// background and the border; the children are not clipped.
    [[nodiscard]] float corner_radius() const
    {
        return corner_radius_;
    }
    /// Sets the corner radius; redraws.
    void set_corner_radius(float radius)
    {
        corner_radius_ = radius;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The room in DIPs between the border and the children (default zero).
    [[nodiscard]] const Thickness& padding() const
    {
        return padding_;
    }
    /// Sets the padding and updates the layout.
    void set_padding(const Thickness& padding)
    {
        padding_ = padding;
        mark_dirty(DirtyFlags::Layout);
    }

    /// The gap between adjacent rows, DIPs; 0 by default.
    [[nodiscard]] float row_spacing() const
    {
        return row_spacing_;
    }
    /// Sets the row gap and updates the layout.
    void set_row_spacing(float spacing)
    {
        row_spacing_ = spacing;
        mark_dirty(DirtyFlags::Layout);
    }
    /// The gap between adjacent columns, DIPs; 0 by default.
    [[nodiscard]] float column_spacing() const
    {
        return column_spacing_;
    }
    /// Sets the column gap and updates the layout.
    void set_column_spacing(float spacing)
    {
        column_spacing_ = spacing;
        mark_dirty(DirtyFlags::Layout);
    }

    /// True: measuring touches only the grid and its children.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }
    /// Lists the children in the order they were added.
    void for_each_child(ChildVisitor visit) const override;

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Advances the background transition, then ticks every child.
    void tick_override(Duration delta) override;


private:
    struct ChildSlot
    {
        std::unique_ptr<Element> element;
        std::size_t              row         = 0;
        std::size_t              column      = 0;
        std::size_t              row_span    = 1;
        std::size_t              column_span = 1;
    };

    /// Resolves final sizes for one axis' tracks: Dip tracks take their literal value, Auto
    /// tracks take `auto_desired[i]`, and any remaining space (available minus the Dip/Auto total
    /// minus inter-track spacing) is split across Fraction tracks proportional to weight. Every
    /// track is kept within its min and max; a Fraction track held at a limit leaves the rest to
    /// the others. An unbounded (ui::Unbounded) `available` sizes Fraction tracks from
    /// `auto_desired` too: a Fraction track is then treated as Auto.
    [[nodiscard]] static std::vector<float> resolve_tracks(const std::vector<GridLength>& lengths,
                                                           float                          available,
                                                           float                          spacing,
                                                           const std::vector<float>&      auto_desired);

    // Border plus padding, taken off every side before the children are laid out.
    [[nodiscard]] Thickness content_inset() const
    {
        return Edges{.left   = border_thickness_.left + padding_.left,
                     .top    = border_thickness_.top + padding_.top,
                     .right  = border_thickness_.right + padding_.right,
                     .bottom = border_thickness_.bottom + padding_.bottom};
    }

    std::vector<GridLength> rows_;
    std::vector<GridLength> columns_;
    std::vector<ChildSlot>  children_;
    float                   row_spacing_    = 0.0f;
    float                   column_spacing_ = 0.0f;
    BackgroundBrush         background_;
    Paint                   border_brush_;
    Thickness               border_thickness_{};
    float                   corner_radius_ = 0.0f;
    Thickness               padding_{};

    /// Populated by measure(); consumed by arrange(). Mirrors StackControl's `measured_`
    /// mutable-cache pattern.
    mutable std::vector<Size>  child_desired_;
    mutable std::vector<float> row_sizes_;
    mutable std::vector<float> column_sizes_;
    /// The per-track Auto/Fraction content sizes measure() derived, kept so arrange() can
    /// re-resolve the tracks against its own final extent without re-measuring anything.
    mutable std::vector<float> auto_row_desired_;
    mutable std::vector<float> auto_column_desired_;
};

/// The `_dip` and `_fr` track-size literals: `using namespace ui::literals;` brings them in, as
/// does `using ui::operator""_fr;` for one of them.
inline namespace literals
{
/// A fixed track size in DIPs: `200_dip`, `12.5_dip`.
consteval GridLength operator""_dip(long double value)
{
    return GridLength::dip(static_cast<float>(value));
}

/// Integer form of `_dip`.
consteval GridLength operator""_dip(unsigned long long value)
{
    return GridLength::dip(static_cast<float>(value));
}

/// A fraction of the remaining space: a weighted share of what is left after fixed and auto
/// tracks, so a `2_fr` track gets twice what a `1_fr` one does. `0.5_fr`.
consteval GridLength operator""_fr(long double weight)
{
    return GridLength::fraction(static_cast<float>(weight));
}

/// Integer form of `_fr`.
consteval GridLength operator""_fr(unsigned long long weight)
{
    return GridLength::fraction(static_cast<float>(weight));
}
} // namespace literals

/// A track sized to its content.
inline constexpr GridLength AutoSize = GridLength::auto_();

/// One placed child. `row`/`column` name tracks declared in `.rows`/`.columns`, and either may be
/// bound to move the child as state changes.
///
/// Written inline in a ui::Grid's `.content` list; see ui::Grid for a full example.
///
/// Example:
/// @code
/// ui::Property<std::size_t> column{0};
/// ui::Cell moving{.row = 1, .column = column, .column_span = 2, .content = ui::Text{.text = "Here"}};
/// @endcode
struct Cell
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    Binding<std::size_t>                              row{std::size_t{0}};    ///< The row index.
    Binding<std::size_t>                              column{std::size_t{0}}; ///< The column index.
    std::size_t                                       row_span    = 1;        ///< Rows covered.
    std::size_t                                       column_span = 1;        ///< Columns covered.
    Spec                                              content{};              ///< The placed child.
};

/// A move-only, already-built list of placed children: the Grid analog of SpecList.
///
/// Callers write it as a braced list of `ui::Cell{...}`; build one in a loop with add().
///
/// Example:
/// @code
/// std::vector<std::string> names = {"Ada", "Grace", "Linus"};
///
/// ui::CellList cells;
/// for (std::size_t i = 0; i < names.size(); ++i)
/// {
///     cells.add(ui::Cell{.row = i, .content = ui::Text{.text = names[i]}});
/// }
/// window.set_content(ui::Grid{.rows    = std::vector(names.size(), ui::AutoSize),
///                             .content = std::move(cells)});
/// @endcode
class CellList
{
public:
    /// One built child with its placement; the node is already owned by the list.
    struct Placed
    {
        Binding<std::size_t>     row;         ///< Row index, possibly bound.
        Binding<std::size_t>     column;      ///< Column index, possibly bound.
        std::size_t              row_span;    ///< Rows covered.
        std::size_t              column_span; ///< Columns covered.
        std::unique_ptr<Element> node;        ///< The built child.
    };

    /// An empty list.
    CellList() = default;
    /// Builds every cell's content and takes ownership of it.
    template <typename... S>
        requires(sizeof...(S) > 0 && (std::same_as<S, Cell> && ...))
    CellList(S&&... items) // NOLINT(google-explicit-constructor)
    {
        cells.reserve(sizeof...(S));
        (add(std::move(items)), ...);
    }
    CellList(const CellList&)            = delete;
    CellList& operator=(const CellList&) = delete;
    CellList(CellList&&)                 = default;
    CellList& operator=(CellList&&)      = default;

    /// The placed children, in declaration order.
    std::vector<Placed> cells;

    /// Builds `cell`'s content and appends it, as one more entry of the braced list would.
    void add(Cell cell)
    {
        cells.push_back(
            Placed{std::move(cell.row), std::move(cell.column), cell.row_span, cell.column_span, cell.content.take()});
    }
};

/// A two-dimensional track layout. Builds a ui::GridControl.
///
/// Tracks are sized with `_dip` (fixed), `_fr` (a fraction of the remaining space) and `AutoSize`
/// (the content), and `ui::minmax()` keeps one between two sizes. A track list left empty is one
/// fraction track. A ui::Splitter in a track of its own between two others resizes them. To
/// resize tracks from code, call set_column_width() or set_row_height() on `.control`.
///
/// A deeply nested tree compiles faster with MSVC when a subtree is built into a named
/// `ui::Spec` first and placed from there.
///
/// Example:
/// @code
/// using namespace ui::literals;
///
/// struct Model
/// {
///     ui::Property<std::string> name;
///     ui::Property<std::string> email;
/// };
/// Model model;
///
/// window.set_content(ui::Grid{
///     .columns        = {120_dip, 1_fr},
///     .rows           = {ui::AutoSize, ui::AutoSize},
///     .row_spacing    = 8,
///     .column_spacing = 12,
///     .content        = {
///         ui::Cell{.row = 0, .column = 0, .content = ui::Text{.text = "Name"}},
///         ui::Cell{.row = 0, .column = 1, .content = ui::TextBox{.text = model.name}},
///         ui::Cell{.row = 1, .column = 0, .content = ui::Text{.text = "Email"}},
///         ui::Cell{.row = 1, .column = 1, .content = ui::TextBox{.text = model.email}},
///     },
/// });
/// @endcode
struct Grid
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Grid>` points at.
    using control_type = GridControl;

    // ---- properties ----
    /// One GridLength per column.
    std::vector<GridLength> columns{};
    /// One GridLength per row.
    std::vector<GridLength> rows{};
    /// Gap between rows, DIPs.
    Binding<float> row_spacing{0.0f};
    /// Gap between columns, DIPs.
    Binding<float> column_spacing{0.0f};
    /// Background behind the cells: a Color, or a ThemeColor read from the theme; empty (the
    /// default) draws nothing.
    Binding<Paint> background{};
    /// Border color, a Color or a ThemeColor; empty (the default) draws no border.
    Binding<Paint> border_brush{};
    /// Border width per edge, DIPs; it insets the children like padding.
    Binding<Thickness> border_thickness{Thickness{}};
    /// Corner radius of the background and the border, DIPs.
    Binding<float> corner_radius{0.0f};
    /// Room between the border and the children, DIPs.
    Binding<Thickness> padding{Thickness{}};

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
    ControlOut<Grid> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The placed children; see ui::Cell.
    CellList content{};

    /// Consumes the spec and returns the built ui::GridControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
