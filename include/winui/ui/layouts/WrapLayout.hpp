// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/ui/layouts/Layout.hpp"

#include <array>
#include <memory>

#include <cstddef>

namespace ui
{

namespace detail
{
class FlowLayoutAlgorithm;
}

/// A virtualizing ForEach layout that places items of any size one after another and wraps to a new
/// line when the next one would not fit. Only the items that show are realized.
///
/// Every item keeps its own desired size. Items sit min_item_spacing() apart along a line. Lines
/// are as thick as their largest item and line_spacing() apart, and line_alignment() spreads the
/// items of each line. Lines outside the viewport are not realized. Their extent is estimated from
/// the average of the last hundred measured lines, so the scroll extent can shift slightly as more
/// of the collection is seen. Use TileLayoutEngine when every item is the same size.
///
/// Example:
/// @code
/// std::vector<std::string> tags = {"news", "sport", "travel", "food", "music"};
///
/// auto flow = std::make_unique<ui::WrapLayoutEngine>();
/// flow->set_min_item_spacing(8.0f);
/// flow->set_line_spacing(8.0f);
/// flow->set_line_alignment(ui::ItemsJustification::SpaceBetween);
/// ui::WrapLayoutEngine* layout = flow.get(); // observer; the repeater owns it
///
/// window.set_content(ui::Scroll{
///     .height  = 320.0f,
///     .content = ui::ForEach<std::string>{
///         .items      = tags,
///         .layout     = std::move(flow),
///         .virtualize = true,
///         .item       = [](const std::string& tag) -> ui::Spec { return ui::Button{.text = tag}; },
///     },
/// });
///
/// // Later: switch to columns that scroll horizontally.
/// layout->set_orientation(ui::Orientation::Vertical);
/// @endcode
class WrapLayoutEngine final : public VirtualizingLayout
{
public:
    /// Horizontal rows, no spacing, Start alignment.
    WrapLayoutEngine();
    ~WrapLayoutEngine() override;

    /// The line direction; Horizontal by default.
    [[nodiscard]] Orientation orientation() const
    {
        return orientation_;
    }
    /// Changes the line direction, discards the collected line estimates and measures the host again.
    void set_orientation(Orientation orientation);
    /// The least gap between neighbors along a line, in DIPs; 0 by default. The Space* alignments
    /// widen it.
    [[nodiscard]] float min_item_spacing() const
    {
        return min_item_spacing_;
    }
    /// Changes the item spacing and measures the host again.
    void set_min_item_spacing(float spacing);
    /// The gap between consecutive lines, in DIPs; 0 by default.
    [[nodiscard]] float line_spacing() const
    {
        return line_spacing_;
    }
    /// Changes the line spacing and measures the host again.
    void set_line_spacing(float spacing);
    /// How each line distributes its spare room; ItemsJustification::Start by default.
    [[nodiscard]] ItemsJustification line_alignment() const
    {
        return line_alignment_;
    }
    /// Changes the line alignment and arranges the host again.
    void set_line_alignment(ItemsJustification alignment);

    /// Realizes and measures the items that fall in the context's realization window, wraps them
    /// into lines and returns the estimated size of the whole collection.
    [[nodiscard]] Size measure(VirtualizingLayoutContext& context, Size available) override;
    /// Positions the realized items within their lines according to line_alignment().
    Size arrange(VirtualizingLayoutContext& context, Size final_size) override;
    /// Updates the realized range and the line estimates after an insert, removal or reset.
    void on_items_changed(VirtualizingLayoutContext& context, const ItemsChange& change) override;
    /// Drops the realized-line state when the layout leaves its host.
    void uninitialize_for_context(VirtualizingLayoutContext& context) override;

private:
    class Delegates;
    friend class Delegates;

    [[nodiscard]] bool vertical_scroll() const
    {
        return orientation_ == Orientation::Horizontal;
    }
    struct LineInfo
    {
        float items_per_line = 1.0f;
        float line_size      = 0.0f; // without the line spacing
    };
    [[nodiscard]] LineInfo average_line(Size available, VirtualizingLayoutContext& context);
    void                   record_line(std::size_t start, std::size_t count, float size);
    void                   reset_state();

    Orientation        orientation_      = Orientation::Horizontal;
    float              min_item_spacing_ = 0.0f;
    float              line_spacing_     = 0.0f;
    ItemsJustification line_alignment_   = ItemsJustification::Start;

    std::unique_ptr<detail::FlowLayoutAlgorithm> algorithm_;

    // The last hundred measured lines, for estimating the lines not realized.
    static constexpr std::size_t        BufferSize = 100;
    std::array<float, BufferSize>       line_sizes_{};
    std::array<std::size_t, BufferSize> items_per_line_{};
    float                               total_line_size_ = 0.0f;
    std::size_t                         total_lines_     = 0;
    std::size_t                         total_items_     = 0;
    Size                                special_size_{};
};

/// Items at their own size in a line that wraps, like the children of a ui::WrapPanel. Builds a
/// ui::WrapLayoutEngine.
///
/// Example:
/// @code
/// window.set_content(ui::ForEach<std::string>{
///     .items  = {"news", "sport", "travel"},
///     .layout = ui::WrapLayout{.min_item_spacing = 8.0f, .line_spacing = 8.0f},
///     .item   = [](const std::string& tag) -> ui::Spec { return ui::Button{.text = tag}; },
/// });
/// @endcode
struct WrapLayout
{
    Orientation orientation      = Orientation::Horizontal; ///< The direction of a line.
    float       min_item_spacing = 0.0f;                    ///< Least gap between items on a line, DIPs.
    float       line_spacing     = 0.0f;                    ///< Gap between lines, DIPs.
    /// How a line's leftover space is distributed.
    ItemsJustification line_alignment = ItemsJustification::Start;

    /// Consumes the value and returns the layout object.
    [[nodiscard]] std::unique_ptr<Layout> build() &&
    {
        auto layout = std::make_unique<WrapLayoutEngine>();
        layout->set_orientation(orientation);
        layout->set_min_item_spacing(min_item_spacing);
        layout->set_line_spacing(line_spacing);
        layout->set_line_alignment(line_alignment);
        return layout;
    }
};

} // namespace ui
