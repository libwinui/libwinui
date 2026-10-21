// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/ui/layouts/Layout.hpp"

#include <memory>
#include <optional>
#include <vector>

#include <cstddef>

namespace ui
{

/// Whether lines narrower than the room are widened to fill it.
enum class LinedFlowLayoutItemsStretch
{
    None, ///< lines keep their natural width and are placed by ItemsJustification (the default)
    Fill, ///< every line but the last is scaled up to run edge to edge
};

/// A virtualizing layout that puts items of any width on lines of one fixed height, like a photo
/// wall.
///
/// Each item keeps its own aspect ratio. Items are measured at the line height and fill each line
/// greedily. One scale factor per line shrinks its items to fit, and with
/// LinedFlowLayoutItemsStretch::Fill it also widens them to reach the edge. Only the lines in the
/// realization rect are realized. The widths of items never measured are estimated from the average
/// aspect ratio. Host it in a ForEach inside a Scroll, or in a LayoutPanel.
///
/// Example:
/// @code
/// ui::Property<std::vector<ui::ImageSource>> photos; // filled as the pictures load
///
/// auto layout = std::make_unique<ui::LinedFlowLayoutEngine>();
/// layout->set_line_height(160.0f);
/// layout->set_line_spacing(4.0f);
/// layout->set_min_item_spacing(4.0f);
/// layout->set_items_stretch(ui::LinedFlowLayoutItemsStretch::Fill);
///
/// window.set_content(ui::Scroll{
///     .height  = 480.0f,
///     .content = ui::ForEach<ui::ImageSource>{
///         .items      = photos,
///         .layout     = std::move(layout),
///         .virtualize = true,
///         .item       = [](const ui::ImageSource& photo) -> ui::Spec {
///             return ui::Image{.source = photo};
///         },
///     },
/// });
/// @endcode
class LinedFlowLayoutEngine final : public VirtualizingLayout
{
public:
    /// The line height in DIPs; nullopt (the default) takes the height of item 0.
    [[nodiscard]] std::optional<float> line_height() const
    {
        return line_height_;
    }
    /// Sets the line height and lays out the host again.
    void set_line_height(OptionalFloat height);
    /// The line height of the last measure.
    [[nodiscard]] float actual_line_height() const
    {
        return actual_line_height_;
    }
    /// The vertical gap between lines, DIPs; 0 by default.
    [[nodiscard]] float line_spacing() const
    {
        return line_spacing_;
    }
    /// Sets the gap between lines and lays out the host again.
    void set_line_spacing(float spacing);
    /// The smallest horizontal gap between items on a line, DIPs; 0 by default. Justification may
    /// widen it.
    [[nodiscard]] float min_item_spacing() const
    {
        return min_item_spacing_;
    }
    /// Sets the minimum item gap and lays out the host again.
    void set_min_item_spacing(float spacing);
    /// Whether short lines are stretched to fill; None by default.
    [[nodiscard]] LinedFlowLayoutItemsStretch items_stretch() const
    {
        return stretch_;
    }
    /// Sets the stretch mode and lays out the host again.
    void set_items_stretch(LinedFlowLayoutItemsStretch stretch);
    /// How short lines are placed with LinedFlowLayoutItemsStretch::None; Start by default.
    [[nodiscard]] ItemsJustification items_justification() const
    {
        return justification_;
    }
    /// Sets the justification and lays out the host again.
    void set_items_justification(ItemsJustification justification);

    /// Forgets every measured width and lays out again. Call it when items changed size while the
    /// collection stayed the same.
    void invalidate_items_info();

    [[nodiscard]] Size measure(VirtualizingLayoutContext& context, Size available) override;
    Size               arrange(VirtualizingLayoutContext& context, Size final_size) override;
    void               on_items_changed(VirtualizingLayoutContext& context, const ItemsChange& change) override;

    /// How many lines the last measure laid out.
    [[nodiscard]] std::size_t line_count() const
    {
        return lines_.size();
    }

private:
    struct Line
    {
        std::size_t first = 0;
        std::size_t count = 0;
        float       scale = 1.0f;
        bool        last  = false; // the collection's last, partial line
    };
    [[nodiscard]] float item_width(std::size_t index) const;
    void                build_lines(float width);
    [[nodiscard]] Rect  item_rect(std::size_t line, std::size_t index, float width) const;

    std::optional<float>        line_height_{};
    float                       line_spacing_     = 0.0f;
    float                       min_item_spacing_ = 0.0f;
    LinedFlowLayoutItemsStretch stretch_          = LinedFlowLayoutItemsStretch::None;
    ItemsJustification          justification_    = ItemsJustification::Start;

    float                    actual_line_height_ = 0.0f;
    std::vector<float>       natural_; // per item: its width at the line height, <0 unknown
    float                    average_ratio_ = 1.0f;
    std::vector<Line>        lines_;
    std::vector<std::size_t> line_of_; // per item: its line
    float                    width_          = 0.0f;
    std::size_t              realized_first_ = 0;
    std::size_t              realized_end_   = 0;
};

/// Justified lines of one height, each item as wide as its aspect ratio asks, like a photo wall.
/// Builds a ui::LinedFlowLayoutEngine.
///
/// Example:
/// @code
/// ui::Property<std::vector<ui::ImageSource>> photos;
///
/// window.set_content(ui::ForEach<ui::ImageSource>{
///     .items  = photos,
///     .layout = ui::LinedFlowLayout{.line_height = 160.0f, .min_item_spacing = 4.0f, .line_spacing = 4.0f},
///     .item   = [](const ui::ImageSource& photo) -> ui::Spec { return ui::Image{.source = photo}; },
/// });
/// @endcode
struct LinedFlowLayout
{
    OptionalFloat line_height{};           ///< Height of every line, DIPs; unset takes the first item's.
    float         min_item_spacing = 0.0f; ///< Least gap between items on a line, DIPs.
    float         line_spacing     = 0.0f; ///< Gap between lines, DIPs.
    /// Whether items grow to fill their line.
    LinedFlowLayoutItemsStretch items_stretch = LinedFlowLayoutItemsStretch::None;
    /// How a line's leftover space is distributed.
    ItemsJustification items_justification = ItemsJustification::Start;

    /// Consumes the value and returns the layout object.
    [[nodiscard]] std::unique_ptr<Layout> build() &&
    {
        auto layout = std::make_unique<LinedFlowLayoutEngine>();
        layout->set_line_height(line_height);
        layout->set_line_spacing(line_spacing);
        layout->set_min_item_spacing(min_item_spacing);
        layout->set_items_stretch(items_stretch);
        layout->set_items_justification(items_justification);
        return layout;
    }
};

} // namespace ui
