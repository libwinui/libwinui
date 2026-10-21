// SPDX-License-Identifier: MIT
#include "winui/ui/layouts/StackLayout.hpp"

#include "FlowLayoutAlgorithm.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/layouts/Layout.hpp"

#include <algorithm>
#include <memory>

#include <cmath>
#include <cstddef>

namespace ui
{

class StackLayoutEngine::Delegates final : public detail::FlowAlgorithmDelegates
{
public:
    explicit Delegates(StackLayoutEngine& owner) : owner_(owner)
    {
    }

    Size measure_size(std::size_t, Size available) override
    {
        return available;
    }
    Size provisional_arrange_size(std::size_t, Size, Size desired) override
    {
        return desired;
    }
    bool should_break_line(std::size_t, float) override
    {
        return true; // every item is a line of its own
    }

    detail::AnchorInfo anchor_for_realization_rect(Size available, VirtualizingLayoutContext& context) override
    {
        const std::size_t count = context.item_count();
        if (count == 0)
        {
            return {};
        }
        const auto& om         = owner_.algorithm_->om();
        const Rect  rect       = context.realization_rect();
        const Rect& last       = owner_.algorithm_->last_extent();
        const float stride     = owner_.average_element_size(available, context) + owner_.spacing_;
        const float offset     = om.major_start(rect) - om.major_start(last);
        const float major_size = om.major_size(last) == 0.0f
                                     ? std::max(0.0f, stride * static_cast<float>(count) - owner_.spacing_)
                                     : om.major_size(last);
        // A zero-size rect still realizes one element (`>= 0`), for a repeater nested out of view.
        if (om.major_size(rect) >= 0.0f && offset + om.major_size(rect) >= 0.0f && offset <= major_size)
        {
            const std::size_t anchor = stride > 0.0f
                                           ? std::min(static_cast<std::size_t>(std::max(0.0f, offset / stride)), count - 1)
                                           : 0;
            return {anchor, static_cast<float>(anchor) * stride + om.major_start(last)};
        }
        return {};
    }

    detail::AnchorInfo anchor_for_target(std::size_t target, Size available, VirtualizingLayoutContext& context) override
    {
        const float stride = owner_.average_element_size(available, context) + owner_.spacing_;
        return {target,
                static_cast<float>(target) * stride + owner_.algorithm_->om().major_start(owner_.algorithm_->last_extent())};
    }

    Rect extent(Size                       available,
                VirtualizingLayoutContext& context,
                const Element*             first,
                std::size_t                first_index,
                const Rect&                first_bounds,
                const Element*             last,
                std::size_t                last_index,
                const Rect&                last_bounds) override
    {
        const auto&       om          = owner_.algorithm_->om();
        const std::size_t count       = context.item_count();
        const float       stride      = owner_.average_element_size(available, context) + owner_.spacing_;
        float             major_start = 0.0f;
        float             major_size  = std::max(0.0f, static_cast<float>(count) * stride - owner_.spacing_);
        if (first != nullptr && last != nullptr)
        {
            major_start = om.major_start(first_bounds) - static_cast<float>(first_index) * stride;
            major_size  = om.major_end(last_bounds) - major_start + static_cast<float>(count - last_index - 1) * stride;
        }
        return om.make_rect(0.0f, major_start, owner_.max_arrange_, major_size);
    }

    void on_element_measured(std::size_t index, Size, Size, Size, Size provisional) override
    {
        const auto& om = owner_.algorithm_->om();
        owner_.on_element_measured(index, om.major(provisional), om.minor(provisional));
    }

private:
    StackLayoutEngine& owner_;
};

StackLayoutEngine::StackLayoutEngine() : algorithm_(std::make_unique<detail::FlowLayoutAlgorithm>())
{
}

StackLayoutEngine::~StackLayoutEngine() = default;

void StackLayoutEngine::set_orientation(Orientation orientation)
{
    if (orientation_ != orientation)
    {
        orientation_ = orientation;
        algorithm_->reset();
        sizes_.fill(0.0f);
        total_size_     = 0.0f;
        total_measured_ = 0;
        regular_        = true;
        last_measured_  = 0.0f;
        invalidate_measure();
    }
}

void StackLayoutEngine::set_spacing(float spacing)
{
    if (spacing_ != spacing)
    {
        spacing_ = spacing;
        invalidate_measure();
    }
}

void StackLayoutEngine::set_virtualization_enabled(bool enabled)
{
    if (virtualize_ != enabled)
    {
        virtualize_ = enabled;
        invalidate_measure();
    }
}

float StackLayoutEngine::average_element_size(Size available, VirtualizingLayoutContext& context)
{
    if (total_measured_ == 0 && context.item_count() > 0)
    {
        // Nothing measured yet: item 0 stands in for the rest as the average item, realized
        // just for this unless the layout already holds it.
        const bool  held    = algorithm_->is_realized(0);
        Element&    element = context.get_or_create_element_at(0, ElementRealizationOptions::ForceCreate);
        const Size  desired = context.measure(element, available);
        const auto& om      = algorithm_->om();
        on_element_measured(0, om.major(desired), 0.0f);
        if (!held)
        {
            context.recycle_element(element);
        }
    }
    if (total_measured_ == 0)
    {
        return 0.0f;
    }
    const float average = total_size_ / static_cast<float>(total_measured_);
    return regular_ ? average : std::round(average);
}

void StackLayoutEngine::on_element_measured(std::size_t index, float major, float minor)
{
    const std::size_t slot = index % BufferSize;
    if (sizes_[slot] == 0.0f && major != 0.0f)
    {
        ++total_measured_;
    }
    else if (sizes_[slot] != 0.0f && major == 0.0f && total_measured_ > 0)
    {
        --total_measured_;
    }
    total_size_ += major - sizes_[slot];
    sizes_[slot] = major;
    if (regular_ && last_measured_ != 0.0f && major != 0.0f && major != last_measured_)
    {
        regular_ = false;
    }
    if (major != 0.0f)
    {
        last_measured_ = major;
    }
    max_arrange_ = std::max(max_arrange_, minor);
}

Size StackLayoutEngine::measure(VirtualizingLayoutContext& context, Size available)
{
    max_arrange_ = 0.0f; // reset at the start of every measure
    detail::FlowLayoutAlgorithm::MeasureParams params;
    params.wrapping        = false;
    params.line_spacing    = spacing_;
    params.vertical_scroll = orientation_ == Orientation::Vertical;
    params.virtualize      = virtualize_;
    Delegates delegates(*this);
    return algorithm_->measure(available, context, params, delegates);
}

Size StackLayoutEngine::arrange(VirtualizingLayoutContext& context, Size final_size)
{
    return algorithm_->arrange(final_size, context, false, detail::LineAlignment::Start);
}

void StackLayoutEngine::on_items_changed(VirtualizingLayoutContext& context, const ItemsChange& change)
{
    algorithm_->on_items_changed(context, change);
    if (change.action == ItemsChangeAction::Reset)
    {
        sizes_.fill(0.0f);
        total_size_     = 0.0f;
        total_measured_ = 0;
        regular_        = true;
        last_measured_  = 0.0f;
    }
    invalidate_measure();
}

void StackLayoutEngine::uninitialize_for_context(VirtualizingLayoutContext&)
{
    algorithm_->reset();
}

} // namespace ui
