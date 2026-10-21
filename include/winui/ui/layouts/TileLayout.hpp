// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/ui/layouts/Layout.hpp"

#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

namespace detail
{
class FlowLayoutAlgorithm;
}

/// Whether cells grow to use the leftover room on a line.
enum class TileLayoutItemsStretch
{
    None,    ///< Cells keep their size (the default).
    Fill,    ///< Cells widen (along the line) so the line fills the available size.
    Uniform, ///< As Fill, and the other dimension grows in proportion.
};

/// A virtualizing layout that puts every item in an equal-sized cell, as many cells per line as fit,
/// line after line.
///
/// The cell is exactly min_item_width() x min_item_height() where those are set, else item 0's
/// desired size. Hand it to a ui::ForEach or a TileView through set_layout() or the spec's
/// `.layout`. Only the lines inside the realization window are realized, but the extent covers
/// every line, so a scroller can jump anywhere. Every setter lays out the host again.
///
/// Example:
/// @code
/// std::vector<std::string> names = {"Ada", "Grace", "Linus", "Margaret"};
///
/// auto grid = std::make_unique<ui::TileLayoutEngine>();
/// grid->set_min_item_width(120.0f);
/// grid->set_min_item_height(80.0f);
/// grid->set_min_row_spacing(8.0f);
/// grid->set_min_column_spacing(8.0f);
/// grid->set_items_stretch(ui::TileLayoutItemsStretch::Fill);
///
/// window.set_content(ui::Scroll{
///     .height  = 400.0f,
///     .content = ui::ForEach<std::string>{
///         .items      = names,
///         .layout     = std::move(grid),
///         .virtualize = true,
///         .item       = [](const std::string& name) -> ui::Spec { return ui::Text{.text = name}; },
///     },
/// });
/// @endcode
class TileLayoutEngine final : public VirtualizingLayout
{
public:
    /// Horizontal, cells sized from item 0, no spacing, Start, None, unlimited per line.
    TileLayoutEngine();
    ~TileLayoutEngine() override;

    /// The direction a line of cells runs in; Horizontal by default.
    [[nodiscard]] Orientation orientation() const
    {
        return orientation_;
    }
    /// Changes the fill direction and lays out the host again.
    void set_orientation(Orientation orientation);
    /// The cell width in DIPs; nullopt (the default) takes the width from item 0.
    [[nodiscard]] std::optional<float> min_item_width() const
    {
        return min_item_width_;
    }
    /// Sets or clears the cell width and lays out the host again.
    void set_min_item_width(OptionalFloat width);
    /// The cell height in DIPs; nullopt (the default) takes it from item 0.
    [[nodiscard]] std::optional<float> min_item_height() const
    {
        return min_item_height_;
    }
    /// Sets or clears the cell height and lays out the host again.
    void set_min_item_height(OptionalFloat height);
    /// The least gap between rows in DIPs; 0 by default. Justification may widen it.
    [[nodiscard]] float min_row_spacing() const
    {
        return min_row_spacing_;
    }
    /// Sets the row gap and lays out the host again.
    void set_min_row_spacing(float spacing);
    /// The gap between columns in DIPs; 0 by default.
    [[nodiscard]] float min_column_spacing() const
    {
        return min_column_spacing_;
    }
    /// Sets the column gap and lays out the host again.
    void set_min_column_spacing(float spacing);
    /// How the cells are spread along a line; Start by default.
    [[nodiscard]] ItemsJustification items_justification() const
    {
        return justification_;
    }
    /// Changes the justification and lays out the host again.
    void set_items_justification(ItemsJustification justification);
    /// Whether cells grow to use the leftover room on a line; None by default. The leftover room
    /// is shared out in whole pixels per cell, so a line may still fall a few pixels short.
    [[nodiscard]] TileLayoutItemsStretch items_stretch() const
    {
        return stretch_;
    }
    /// Changes the stretch and lays out the host again.
    void set_items_stretch(TileLayoutItemsStretch stretch);
    /// The most cells per line. 0 (the default) is unlimited.
    [[nodiscard]] std::size_t maximum_rows_or_columns() const
    {
        return max_rows_or_columns_;
    }
    /// Caps the cells per line and lays out the host again.
    void set_maximum_rows_or_columns(std::size_t max);

    /// Lays the indices out in groups: each group is one index for its header, which takes a line
    /// of its own across the whole width, then `sizes[g]` indices in cells, from the start of a new
    /// line. The indices run header, its items, the next header, and so on. Every header is as
    /// tall as the first one, the cells are sized from the first item, and a line's cells start at
    /// its leading edge. An empty vector (the default) lays every index out as a cell. Lays out the
    /// host again. A grouped ui::TileView sets it.
    void set_group_sizes(std::vector<std::size_t> sizes);
    /// The sizes set_group_sizes() was given.
    [[nodiscard]] const std::vector<std::size_t>& group_sizes() const
    {
        return group_sizes_;
    }

    [[nodiscard]] Size measure(VirtualizingLayoutContext& context, Size available) override;
    Size               arrange(VirtualizingLayoutContext& context, Size final_size) override;
    void               on_items_changed(VirtualizingLayoutContext& context, const ItemsChange& change) override;
    void               uninitialize_for_context(VirtualizingLayoutContext& context) override;

    /// The cell size of the last measure in DIPs, stretch included. Zero before the first measure.
    [[nodiscard]] Size effective_item_size() const
    {
        return item_size_;
    }

private:
    class Delegates;
    friend class Delegates;

    [[nodiscard]] bool vertical_scroll() const
    {
        return orientation_ == Orientation::Horizontal;
    }
    [[nodiscard]] float line_spacing() const
    {
        return vertical_scroll() ? min_row_spacing_ : min_column_spacing_;
    }
    [[nodiscard]] float item_spacing() const
    {
        return vertical_scroll() ? min_column_spacing_ : min_row_spacing_;
    }
    [[nodiscard]] std::size_t max_per_line() const
    {
        return max_rows_or_columns_ == 0 ? (std::numeric_limits<std::size_t>::max)() : max_rows_or_columns_;
    }
    void ensure_item_size(Size available, VirtualizingLayoutContext& context, std::size_t first = 0, bool keep = false);
    [[nodiscard]] Size        measure_grouped(VirtualizingLayoutContext& context, Size available);
    [[nodiscard]] std::size_t items_per_line(float available_minor, std::size_t count) const;
    [[nodiscard]] float       major_size_of(std::size_t count, std::size_t per_line) const;

    Orientation            orientation_ = Orientation::Horizontal;
    std::optional<float>   min_item_width_{};
    std::optional<float>   min_item_height_{};
    float                  min_row_spacing_     = 0.0f;
    float                  min_column_spacing_  = 0.0f;
    ItemsJustification     justification_       = ItemsJustification::Start;
    TileLayoutItemsStretch stretch_             = TileLayoutItemsStretch::None;
    std::size_t            max_rows_or_columns_ = 0;

    std::unique_ptr<detail::FlowLayoutAlgorithm> algorithm_;
    Size                                         item_size_{};

    // Grouped: the items per group, and where the last measure put each index it realized.
    std::vector<std::size_t>                  group_sizes_;
    std::vector<std::pair<std::size_t, Rect>> placed_;
};

/// Equal cells, as many per line as fit, wrapping to the next line. The layout of a ui::TileView.
/// Builds a ui::TileLayoutEngine.
///
/// Example:
/// @code
/// window.set_content(ui::ForEach<std::string>{
///     .items  = {"Ada", "Grace", "Linus", "Margaret"},
///     .layout = ui::TileLayout{.min_item_width = 120.0f, .min_row_spacing = 8.0f, .min_column_spacing = 8.0f},
///     .item   = [](const std::string& name) -> ui::Spec { return ui::Text{.text = name}; },
/// });
/// @endcode
struct TileLayout
{
    Orientation   orientation = Orientation::Horizontal; ///< The direction of a line of cells.
    OptionalFloat min_item_width{};                      ///< Cell width, DIPs; unset takes the first item's.
    OptionalFloat min_item_height{};                     ///< Cell height, DIPs; unset takes the first item's.
    float         min_row_spacing    = 0.0f;             ///< Least gap between rows, DIPs.
    float         min_column_spacing = 0.0f;             ///< Least gap between columns, DIPs.
    /// How a line's leftover space is distributed between the cells.
    ItemsJustification items_justification = ItemsJustification::Start;
    /// Whether cells grow to use a line's leftover space.
    TileLayoutItemsStretch items_stretch = TileLayoutItemsStretch::None;
    /// The most cells on a line; 0 is as many as fit.
    std::size_t maximum_rows_or_columns = 0;

    /// Consumes the value and returns the layout object.
    [[nodiscard]] std::unique_ptr<Layout> build() &&
    {
        auto layout = std::make_unique<TileLayoutEngine>();
        layout->set_orientation(orientation);
        layout->set_min_item_width(min_item_width);
        layout->set_min_item_height(min_item_height);
        layout->set_min_row_spacing(min_row_spacing);
        layout->set_min_column_spacing(min_column_spacing);
        layout->set_items_justification(items_justification);
        layout->set_items_stretch(items_stretch);
        layout->set_maximum_rows_or_columns(maximum_rows_or_columns);
        return layout;
    }
};

} // namespace ui
