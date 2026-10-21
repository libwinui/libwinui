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

/// A virtualizing layout that places items one after another, `spacing` apart. A ui::ForEach or a
/// TileView stacks its items with it.
///
/// Only the items in or near the viewport are realized. The rest are estimated at the average
/// measured size, so the scroll extent stays stable. Every item gets the full cross extent. It
/// also works in a LayoutPanel, where every child is realized.
///
/// Example:
/// @code
/// std::vector<std::string> rows = {"Inbox", "Drafts", "Sent"}; // outlives the window
///
/// auto  scroll   = std::make_unique<ui::ScrollControl>();
/// auto* repeater = scroll->set_child(std::make_unique<ui::ForEachControl>());
/// auto* stack    = repeater->set_layout(std::make_unique<ui::StackLayoutEngine>());
/// stack->set_orientation(ui::Orientation::Vertical);
/// stack->set_spacing(8.0f);
/// repeater->set_item_template([&rows](std::size_t i) { return std::make_unique<ui::TextControl>(rows[i]); });
/// repeater->set_item_count(rows.size());
/// window.set_content(std::move(scroll));
/// @endcode
class StackLayoutEngine final : public VirtualizingLayout
{
public:
    /// A vertical stack with no spacing and virtualization on.
    StackLayoutEngine();
    ~StackLayoutEngine() override;

    /// The stacking direction; Vertical by default.
    [[nodiscard]] Orientation orientation() const
    {
        return orientation_;
    }
    /// Changes the direction and invalidates the host's measure.
    void set_orientation(Orientation orientation);
    /// The gap between neighboring items along the stack, in DIPs; 0 by default.
    [[nodiscard]] float spacing() const
    {
        return spacing_;
    }
    /// Changes the gap and invalidates the host's measure.
    void set_spacing(float spacing);

    /// Whether only the items in or near the viewport are realized. When off, every item is
    /// realized, however few of them show.
    [[nodiscard]] bool is_virtualization_enabled() const
    {
        return virtualize_;
    }
    /// Switches virtualization and invalidates the host's measure.
    void set_virtualization_enabled(bool enabled);

    /// Realizes and measures the items the viewport and its cache need, each unbounded along the
    /// stack. Returns the estimated extent of all items along the stack and the widest item across.
    [[nodiscard]] Size measure(VirtualizingLayoutContext& context, Size available) override;
    /// Places the realized items one after another, each stretched across `final_size`.
    Size arrange(VirtualizingLayoutContext& context, Size final_size) override;
    /// Shifts the realized range to follow an insert, remove or reset of the source.
    void on_items_changed(VirtualizingLayoutContext& context, const ItemsChange& change) override;
    /// Drops the per-host layout state when a host stops using this layout.
    void uninitialize_for_context(VirtualizingLayoutContext& context) override;

private:
    class Delegates;
    friend class Delegates;

    // The last hundred measured sizes along the stack, and the widest item this pass.
    static constexpr std::size_t BufferSize = 100;
    [[nodiscard]] float          average_element_size(Size available, VirtualizingLayoutContext& context);
    void                         on_element_measured(std::size_t index, float major, float minor);

    Orientation orientation_ = Orientation::Vertical;
    float       spacing_     = 0.0f;
    bool        virtualize_  = true;

    std::unique_ptr<detail::FlowLayoutAlgorithm> algorithm_;
    std::array<float, BufferSize>                sizes_{};
    float                                        total_size_     = 0.0f;
    std::size_t                                  total_measured_ = 0;
    bool                                         regular_        = true;
    float                                        last_measured_  = 0.0f;
    float                                        max_arrange_    = 0.0f;
};

/// Items one after another in a line, like the children of a ui::VerticalStack or
/// ui::HorizontalStack. Builds a ui::StackLayoutEngine.
///
/// Example:
/// @code
/// window.set_content(ui::ForEach<std::string>{
///     .items  = {"One", "Two", "Three"},
///     .layout = ui::StackLayout{.orientation = ui::Orientation::Horizontal, .spacing = 8.0f},
///     .item   = [](const std::string& text) -> ui::Spec { return ui::Text{.text = text}; },
/// });
/// @endcode
struct StackLayout
{
    Orientation orientation = Orientation::Vertical; ///< The direction items follow each other in.
    float       spacing     = 0.0f;                  ///< Gap between adjacent items, DIPs.

    /// Consumes the value and returns the layout object.
    [[nodiscard]] std::unique_ptr<Layout> build() &&
    {
        auto layout = std::make_unique<StackLayoutEngine>();
        layout->set_orientation(orientation);
        layout->set_spacing(spacing);
        return layout;
    }
};

} // namespace ui
