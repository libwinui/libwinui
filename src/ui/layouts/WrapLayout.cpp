// SPDX-License-Identifier: MIT
#include "winui/ui/layouts/WrapLayout.hpp"

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

class WrapLayoutEngine::Delegates final : public detail::FlowAlgorithmDelegates
{
public:
    explicit Delegates(WrapLayoutEngine& owner) : owner_(owner)
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
    bool should_break_line(std::size_t, float remaining) override
    {
        return remaining < 0.0f;
    }

    detail::AnchorInfo anchor_for_realization_rect(Size available, VirtualizingLayoutContext& context) override
    {
        const std::size_t count = context.item_count();
        const auto&       om    = owner_.algorithm_->om();
        const Rect        rect  = context.realization_rect();
        if (count == 0 || om.major_size(rect) <= 0.0f)
        {
            return {};
        }
        const LineInfo info   = owner_.average_line(available, context);
        const float    stride = info.line_size + owner_.line_spacing_;
        const Rect&    last   = owner_.algorithm_->last_extent();
        const float extent_major = om.major_size(last) == 0.0f ? (static_cast<float>(count) / info.items_per_line) * stride
                                                               : om.major_size(last);
        if (om.major_start(rect) > om.major_start(last) + extent_major || om.major_end(rect) < om.major_start(last) ||
            stride <= 0.0f)
        {
            return {};
        }
        const auto line = static_cast<std::size_t>(std::max(0.0f, (om.major_start(rect) - om.major_start(last)) / stride));
        const auto anchor = std::min(static_cast<std::size_t>(static_cast<float>(line) * info.items_per_line), count - 1);
        return {anchor, static_cast<float>(line) * stride + om.major_start(last)};
    }

    detail::AnchorInfo anchor_for_target(std::size_t target, Size available, VirtualizingLayoutContext& context) override
    {
        const LineInfo info   = owner_.average_line(available, context);
        const float    stride = info.line_size + owner_.line_spacing_;
        const auto     line   = static_cast<std::size_t>(static_cast<float>(target) / info.items_per_line);
        return {target,
                static_cast<float>(line) * stride + owner_.algorithm_->om().major_start(owner_.algorithm_->last_extent())};
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
        const float       avail_minor = om.minor(available);
        const bool        finite      = std::isfinite(avail_minor);
        if (count == 0)
        {
            return om.make_rect(0.0f, 0.0f, finite ? avail_minor : 0.0f, 0.0f);
        }
        const LineInfo info   = owner_.average_line(available, context);
        const float    stride = info.line_size + owner_.line_spacing_;
        if (first != nullptr && last != nullptr)
        {
            const float major_start = om.major_start(first_bounds) -
                                      static_cast<float>(static_cast<std::size_t>(
                                          static_cast<float>(first_index) / info.items_per_line)) *
                                          stride;
            const float after       = static_cast<float>(
                static_cast<std::size_t>(static_cast<float>(count - last_index - 1) / info.items_per_line));
            const float major_size = om.major_end(last_bounds) - major_start + after * stride;
            const float minor_size = finite ? avail_minor : std::max(0.0f, om.minor_end(last_bounds));
            return om.make_rect(0.0f, major_start, minor_size, major_size);
        }
        if (finite)
        {
            const float lines = std::ceil(static_cast<float>(count) / info.items_per_line);
            return om.make_rect(0.0f, 0.0f, avail_minor, std::max(0.0f, lines * stride - owner_.line_spacing_));
        }
        const float minor = std::max(0.0f,
                                     (om.minor(owner_.special_size_) + owner_.min_item_spacing_) * static_cast<float>(count) -
                                         owner_.min_item_spacing_);
        return om.make_rect(0.0f, 0.0f, minor, std::max(0.0f, stride - owner_.line_spacing_));
    }

    void on_line_arranged(std::size_t start, std::size_t count, float line_size, VirtualizingLayoutContext& context) override
    {
        // The collection's last line is usually short and would skew the average, unless it is
        // all there is.
        if (start + count == context.item_count() && owner_.total_lines_ > 0)
        {
            return;
        }
        owner_.record_line(start, count, line_size);
    }

private:
    WrapLayoutEngine& owner_;
};

WrapLayoutEngine::WrapLayoutEngine() : algorithm_(std::make_unique<detail::FlowLayoutAlgorithm>())
{
}

WrapLayoutEngine::~WrapLayoutEngine() = default;

void WrapLayoutEngine::set_orientation(Orientation orientation)
{
    if (orientation_ != orientation)
    {
        orientation_ = orientation;
        algorithm_->reset();
        reset_state();
        invalidate_measure();
    }
}

void WrapLayoutEngine::set_min_item_spacing(float spacing)
{
    min_item_spacing_ = spacing;
    invalidate_measure();
}

void WrapLayoutEngine::set_line_spacing(float spacing)
{
    line_spacing_ = spacing;
    invalidate_measure();
}

void WrapLayoutEngine::set_line_alignment(ItemsJustification alignment)
{
    line_alignment_ = alignment;
    invalidate_arrange();
}

void WrapLayoutEngine::reset_state()
{
    line_sizes_.fill(0.0f);
    items_per_line_.fill(0);
    total_line_size_ = 0.0f;
    total_lines_     = 0;
    total_items_     = 0;
}

void WrapLayoutEngine::record_line(std::size_t start, std::size_t count, float size)
{
    const std::size_t slot = start % BufferSize;
    if (items_per_line_[slot] == 0)
    {
        ++total_lines_;
    }
    total_line_size_ += size - line_sizes_[slot];
    total_items_ += count;
    total_items_ -= items_per_line_[slot];
    line_sizes_[slot]     = size;
    items_per_line_[slot] = count;
}

WrapLayoutEngine::LineInfo WrapLayoutEngine::average_line(Size available, VirtualizingLayoutContext& context)
{
    const detail::OrientationMeasures om{vertical_scroll()};
    if (total_lines_ == 0)
    {
        // No line measured yet: item 0 stands in for a line of as many like it as fit
        // as the average line, realized just for this unless the layout already holds it.
        if (context.item_count() == 0)
        {
            return {};
        }
        const bool held    = algorithm_->is_realized(0);
        Element&   element = context.get_or_create_element_at(0, ElementRealizationOptions::ForceCreate);
        special_size_      = context.measure(element, available);
        if (!held)
        {
            context.recycle_element(element);
        }
        const float minor    = om.minor(special_size_);
        const float avail    = om.minor(available);
        const float per_line = std::isfinite(avail) && minor > 0.0f ? std::max(1.0f, std::floor(avail / minor)) : 1.0f;
        return {per_line, om.major(special_size_)};
    }
    return {std::max(1.0f, static_cast<float>(total_items_) / static_cast<float>(total_lines_)),
            std::round(total_line_size_ / static_cast<float>(total_lines_))};
}

Size WrapLayoutEngine::measure(VirtualizingLayoutContext& context, Size available)
{
    detail::FlowLayoutAlgorithm::MeasureParams params;
    params.wrapping         = true;
    params.min_item_spacing = min_item_spacing_;
    params.line_spacing     = line_spacing_;
    params.vertical_scroll  = vertical_scroll();
    Delegates delegates(*this);
    return algorithm_->measure(available, context, params, delegates);
}

Size WrapLayoutEngine::arrange(VirtualizingLayoutContext& context, Size final_size)
{
    return algorithm_->arrange(final_size, context, true, static_cast<detail::LineAlignment>(line_alignment_));
}

void WrapLayoutEngine::on_items_changed(VirtualizingLayoutContext& context, const ItemsChange& change)
{
    algorithm_->on_items_changed(context, change);
    if (change.action == ItemsChangeAction::Reset)
    {
        reset_state();
    }
    invalidate_measure();
}

void WrapLayoutEngine::uninitialize_for_context(VirtualizingLayoutContext&)
{
    algorithm_->reset();
}

} // namespace ui
