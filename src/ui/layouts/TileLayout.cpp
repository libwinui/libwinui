// SPDX-License-Identifier: MIT
#include "winui/ui/layouts/TileLayout.hpp"

#include "FlowLayoutAlgorithm.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/layouts/Layout.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

class TileLayoutEngine::Delegates final : public detail::FlowAlgorithmDelegates
{
public:
    explicit Delegates(TileLayoutEngine& owner) : owner_(owner)
    {
    }

    // Every cell is the effective item size, whatever the item asked for.
    Size measure_size(std::size_t, Size) override
    {
        return owner_.item_size_;
    }
    Size provisional_arrange_size(std::size_t, Size, Size) override
    {
        return owner_.item_size_;
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
        const Rect&       last     = owner_.algorithm_->last_extent();
        const std::size_t per_line = owner_.items_per_line(om.minor(available), count);
        const float       stride   = om.major(owner_.item_size_) + owner_.line_spacing();
        const float       extent   = owner_.major_size_of(count, per_line);
        const float       start    = om.major_start(rect) - om.major_start(last);
        if (start > extent || start + om.major_size(rect) < 0.0f || stride <= 0.0f)
        {
            return {};
        }
        const auto        line        = static_cast<std::size_t>(std::max(0.0f, start) / stride);
        const std::size_t anchor      = std::min(line * per_line, count - 1);
        const std::size_t anchor_line = anchor / per_line;
        return {anchor, static_cast<float>(anchor_line) * stride + om.major_start(last)};
    }

    detail::AnchorInfo anchor_for_target(std::size_t target, Size available, VirtualizingLayoutContext& context) override
    {
        // The start of the target's line, so the line keeps its columns.
        const auto&       om       = owner_.algorithm_->om();
        const std::size_t per_line = owner_.items_per_line(om.minor(available), context.item_count());
        const float       stride   = om.major(owner_.item_size_) + owner_.line_spacing();
        const std::size_t line     = target / per_line;
        return {line * per_line, static_cast<float>(line) * stride + om.major_start(owner_.algorithm_->last_extent())};
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
        const auto&       om           = owner_.algorithm_->om();
        const std::size_t count        = context.item_count();
        const float       avail_minor  = om.minor(available);
        const bool        finite       = std::isfinite(avail_minor);
        const std::size_t per_line     = owner_.items_per_line(avail_minor, count);
        const float       minor_stride = om.minor(owner_.item_size_) + owner_.item_spacing();
        const float       major_stride = om.major(owner_.item_size_) + owner_.line_spacing();

        const float minor_size = finite && owner_.stretch_ == TileLayoutItemsStretch::Fill
                                     ? avail_minor
                                     : std::max(0.0f, static_cast<float>(per_line) * minor_stride - owner_.item_spacing());
        float major_start = 0.0f;
        float major_size  = owner_.major_size_of(count, per_line);
        if (first != nullptr && last != nullptr && count > 0)
        {
            const std::size_t first_line = first_index / per_line;
            major_start                  = om.major_start(first_bounds) - static_cast<float>(first_line) * major_stride;
            const std::size_t after_last = count - last_index - 1;
            const std::size_t column     = (last_index + 1) % per_line;
            const std::size_t on_line    = std::min(after_last, column == 0 ? std::size_t{0} : per_line - column);
            const float       rest       = owner_.major_size_of(after_last - on_line, per_line);
            major_size = om.major_end(last_bounds) - major_start + (rest > 0.0f ? owner_.line_spacing() + rest : 0.0f);
        }
        return om.make_rect(0.0f, major_start, minor_size, major_size);
    }

private:
    TileLayoutEngine& owner_;
};

TileLayoutEngine::TileLayoutEngine() : algorithm_(std::make_unique<detail::FlowLayoutAlgorithm>())
{
}

TileLayoutEngine::~TileLayoutEngine() = default;

void TileLayoutEngine::set_orientation(Orientation orientation)
{
    if (orientation_ != orientation)
    {
        orientation_ = orientation;
        algorithm_->reset();
        invalidate_measure();
    }
}

void TileLayoutEngine::set_min_item_width(OptionalFloat width)
{
    min_item_width_ = width;
    invalidate_measure();
}

void TileLayoutEngine::set_min_item_height(OptionalFloat height)
{
    min_item_height_ = height;
    invalidate_measure();
}

void TileLayoutEngine::set_min_row_spacing(float spacing)
{
    min_row_spacing_ = spacing;
    invalidate_measure();
}

void TileLayoutEngine::set_min_column_spacing(float spacing)
{
    min_column_spacing_ = spacing;
    invalidate_measure();
}

void TileLayoutEngine::set_items_justification(ItemsJustification justification)
{
    justification_ = justification;
    invalidate_measure();
}

void TileLayoutEngine::set_items_stretch(TileLayoutItemsStretch stretch)
{
    stretch_ = stretch;
    invalidate_measure();
}

void TileLayoutEngine::set_maximum_rows_or_columns(std::size_t max)
{
    max_rows_or_columns_ = max;
    invalidate_measure();
}

void TileLayoutEngine::set_group_sizes(std::vector<std::size_t> sizes)
{
    group_sizes_ = std::move(sizes);
    placed_.clear();
    algorithm_->reset();
    invalidate_measure();
}

std::size_t TileLayoutEngine::items_per_line(float available_minor, std::size_t count) const
{
    const detail::OrientationMeasures om{vertical_scroll()};
    const float                       stride = om.minor(item_size_) + item_spacing();
    if (!std::isfinite(available_minor))
    {
        return max_rows_or_columns_ == 0 ? std::max<std::size_t>(1, count) : max_rows_or_columns_;
    }
    const std::size_t fit = stride > 0.0f
                                ? std::max<std::size_t>(1, static_cast<std::size_t>((available_minor + item_spacing()) / stride))
                                : 1;
    return std::min(fit, std::max<std::size_t>(1, max_per_line()));
}

float TileLayoutEngine::major_size_of(std::size_t count, std::size_t per_line) const
{
    const detail::OrientationMeasures om{vertical_scroll()};
    if (count == 0 || per_line == 0)
    {
        return 0.0f;
    }
    const std::size_t lines = (count + per_line - 1) / per_line;
    return static_cast<float>(lines) * (om.major(item_size_) + line_spacing()) - line_spacing();
}

void TileLayoutEngine::ensure_item_size(Size available, VirtualizingLayoutContext& context, std::size_t first_index, bool keep)
{
    const detail::OrientationMeasures om{vertical_scroll()};
    if (context.item_count() <= first_index)
    {
        item_size_ = Size{};
        return;
    }
    const float avail_minor = om.minor(available);
    const auto  extra_for   = [&](float item_minor)
    {
        // The room left once as many cells as fit are laid out, shared per cell,
        // in integer pixels, and negative when not even the minimum cells fit.
        if (!std::isfinite(avail_minor))
        {
            return 0.0f;
        }
        const float       stride = item_minor + item_spacing();
        const std::size_t n    = std::min(std::max<std::size_t>(1, max_per_line()),
                                          stride > 0.0f
                                              ? std::max<std::size_t>(1, static_cast<std::size_t>(avail_minor / stride))
                                              : 1);
        const float       used = static_cast<float>(n) * stride - item_spacing();
        const int         spare = static_cast<int>(avail_minor - used) / static_cast<int>(n);
        return static_cast<float>(spare);
    };

    // Item 0 decides whatever the minimums leave open.
    Size                       measure_in = available;
    const std::optional<float> set_minor  = vertical_scroll() ? min_item_width_ : min_item_height_;
    if (set_minor && std::isfinite(avail_minor))
    {
        const float minor = *set_minor + (stretch_ != TileLayoutItemsStretch::None ? extra_for(*set_minor) : 0.0f);
        measure_in        = om.make_size(minor, om.major(available));
    }
    const bool held    = keep || algorithm_->is_realized(first_index);
    Element&   first   = context.get_or_create_element_at(first_index, ElementRealizationOptions::ForceCreate);
    const Size desired = context.measure(first, measure_in);
    if (!held)
    {
        context.recycle_element(first);
    }

    const float width  = min_item_width_ ? *min_item_width_ : desired.width;
    const float height = min_item_height_ ? *min_item_height_ : desired.height;
    float       minor  = vertical_scroll() ? width : height;
    float       major  = vertical_scroll() ? height : width;
    if (stretch_ != TileLayoutItemsStretch::None)
    {
        const float extra = extra_for(minor);
        if (stretch_ == TileLayoutItemsStretch::Uniform && minor > 0.0f)
        {
            major += major * (extra / minor);
        }
        minor += extra;
    }
    item_size_ = om.make_size(std::max(0.0f, minor), std::max(0.0f, major));
}

Size TileLayoutEngine::measure_grouped(VirtualizingLayoutContext& context, Size available)
{
    const detail::OrientationMeasures om{vertical_scroll()};
    const std::size_t                 count = context.item_count();
    placed_.clear();

    // The first item sizes the cells, the first header every header.
    std::size_t first_item = count;
    std::size_t largest    = 0;
    for (std::size_t g = 0, pos = 0; g < group_sizes_.size(); pos += 1 + group_sizes_[g], ++g)
    {
        if (group_sizes_[g] > 0 && first_item == count)
        {
            first_item = pos + 1;
        }
        largest = std::max(largest, group_sizes_[g]);
    }
    if (first_item < count)
    {
        ensure_item_size(available, context, first_item, true);
    }
    else
    {
        item_size_ = Size{};
    }

    const float       avail_minor  = om.minor(available);
    const bool        finite       = std::isfinite(avail_minor);
    const std::size_t per_line     = items_per_line(avail_minor, largest);
    const float       minor_stride = om.minor(item_size_) + item_spacing();
    const float       major_stride = om.major(item_size_) + line_spacing();
    const float minor_size = finite ? avail_minor
                                    : std::max(0.0f, static_cast<float>(per_line) * minor_stride - item_spacing());

    float header_major = 0.0f;
    if (count > 0)
    {
        Element& header = context.get_or_create_element_at(0, ElementRealizationOptions::ForceCreate);
        header_major    = om.major(context.measure(header, om.make_size(minor_size, Unbounded)));
    }

    const Rect  rect    = context.realization_rect();
    const float r_start = om.major_start(rect);
    const float r_end   = om.major_end(rect);
    const auto  place   = [&](std::size_t index, const Rect& at, Size measure_in)
    {
        if (index >= count)
        {
            return;
        }
        Element& e = context.get_or_create_element_at(index);
        (void)context.measure(e, measure_in);
        placed_.emplace_back(index, at);
    };

    float       major = 0.0f;
    std::size_t pos   = 0;
    for (std::size_t g = 0; g < group_sizes_.size() && pos < count; ++g)
    {
        const std::size_t items = group_sizes_[g];
        // The first header and the first item stay placed: they were realized to size the rest.
        if ((major + header_major >= r_start && major <= r_end) || pos == 0)
        {
            place(pos, om.make_rect(0.0f, major, minor_size, header_major), om.make_size(minor_size, header_major));
        }
        const float       top   = major + header_major;
        const std::size_t lines = per_line > 0 ? (items + per_line - 1) / per_line : 0;
        const float       block = lines > 0 ? static_cast<float>(lines) * major_stride - line_spacing() : 0.0f;
        if (lines > 0 && major_stride > 0.0f && top + block >= r_start && top <= r_end)
        {
            const auto first_line = static_cast<std::size_t>(std::max(0.0f, (r_start - top) / major_stride));
            const auto last_line  = std::min(lines - 1,
                                             static_cast<std::size_t>(std::max(0.0f, (r_end - top) / major_stride)));
            for (std::size_t line = first_line; line <= last_line; ++line)
            {
                for (std::size_t column = 0; column < per_line; ++column)
                {
                    const std::size_t k = line * per_line + column;
                    if (k >= items)
                    {
                        break;
                    }
                    place(pos + 1 + k,
                          om.make_rect(static_cast<float>(column) * minor_stride,
                                       top + static_cast<float>(line) * major_stride,
                                       om.minor(item_size_),
                                       om.major(item_size_)),
                          item_size_);
                }
            }
        }
        if (first_item == pos + 1 && std::ranges::none_of(placed_, [&](const auto& p) { return p.first == first_item; }))
        {
            place(first_item, om.make_rect(0.0f, top, om.minor(item_size_), om.major(item_size_)), item_size_);
        }
        major = top + block + (g + 1 < group_sizes_.size() ? line_spacing() : 0.0f);
        pos += 1 + items;
    }
    return om.make_size(minor_size, major);
}

Size TileLayoutEngine::measure(VirtualizingLayoutContext& context, Size available)
{
    if (!group_sizes_.empty())
    {
        return measure_grouped(context, available);
    }
    // Cells of another size put the realized items in other columns; the algorithm only knows
    // where they were
    const Size cell = item_size_;
    ensure_item_size(available, context);
    detail::FlowLayoutAlgorithm::MeasureParams params;
    params.items_resized      = item_size_.width != cell.width || item_size_.height != cell.height;
    params.wrapping           = true;
    params.min_item_spacing   = item_spacing();
    params.line_spacing       = line_spacing();
    params.max_items_per_line = max_per_line();
    params.vertical_scroll    = vertical_scroll();
    Delegates delegates(*this);
    return algorithm_->measure(available, context, params, delegates);
}

Size TileLayoutEngine::arrange(VirtualizingLayoutContext& context, Size final_size)
{
    if (!group_sizes_.empty())
    {
        for (const auto& [index, rect] : placed_)
        {
            if (index < context.item_count())
            {
                context.arrange(context.get_or_create_element_at(index), rect);
            }
        }
        return final_size;
    }
    return algorithm_->arrange(final_size, context, true, static_cast<detail::LineAlignment>(justification_));
}

void TileLayoutEngine::on_items_changed(VirtualizingLayoutContext& context, const ItemsChange& change)
{
    placed_.clear();
    algorithm_->on_items_changed(context, change);
    invalidate_measure();
}

void TileLayoutEngine::uninitialize_for_context(VirtualizingLayoutContext&)
{
    placed_.clear();
    algorithm_->reset();
}

} // namespace ui
