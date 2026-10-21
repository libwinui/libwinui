// SPDX-License-Identifier: MIT
#include "winui/ui/layouts/LinedFlowLayout.hpp"

#include "winui/core/Geometry.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/layouts/Layout.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
constexpr float LinedFlowLayoutInfinity = std::numeric_limits<float>::infinity();
}

void LinedFlowLayoutEngine::set_line_height(OptionalFloat height)
{
    line_height_ = height;
    invalidate_measure();
}

void LinedFlowLayoutEngine::set_line_spacing(float spacing)
{
    line_spacing_ = spacing;
    invalidate_measure();
}

void LinedFlowLayoutEngine::set_min_item_spacing(float spacing)
{
    min_item_spacing_ = spacing;
    invalidate_measure();
}

void LinedFlowLayoutEngine::set_items_stretch(LinedFlowLayoutItemsStretch stretch)
{
    stretch_ = stretch;
    invalidate_measure();
}

void LinedFlowLayoutEngine::set_items_justification(ItemsJustification justification)
{
    justification_ = justification;
    invalidate_arrange();
}

void LinedFlowLayoutEngine::invalidate_items_info()
{
    std::fill(natural_.begin(), natural_.end(), -1.0f);
    average_ratio_ = 1.0f;
    invalidate_measure();
}

float LinedFlowLayoutEngine::item_width(std::size_t index) const
{
    return natural_[index] >= 0.0f ? natural_[index] : average_ratio_ * actual_line_height_;
}

void LinedFlowLayoutEngine::build_lines(float width)
{
    const std::size_t n = natural_.size();
    const float       s = min_item_spacing_;
    lines_.clear();
    line_of_.assign(n, 0);
    if (n == 0)
    {
        return;
    }
    if (!std::isfinite(width))
    {
        lines_.push_back(Line{0, n, 1.0f, true});
        return;
    }

    Line       cur{};
    float      widths     = 0.0f; // the items' widths alone, without spacing
    const auto line_width = [&](std::size_t count, float sum)
    { return sum + s * static_cast<float>(count > 0 ? count - 1 : 0); };
    const auto add = [&](std::size_t i)
    {
        if (cur.count == 0)
        {
            cur.first = i;
        }
        ++cur.count;
        widths += item_width(i);
    };
    const auto close = [&](float scale)
    {
        cur.scale = scale;
        lines_.push_back(cur);
        cur    = Line{};
        widths = 0.0f;
    };
    const auto fit = [&](std::size_t count, float sum)
    { return sum > 0.0f ? (width - s * static_cast<float>(count - 1)) / sum : 1.0f; };

    for (std::size_t i = 0; i < n; ++i)
    {
        const float w = item_width(i);
        if (cur.count == 0 || line_width(cur.count + 1, widths + w) <= width)
        {
            add(i);
        }
        else
        {
            // Close the line and stretch it, or take the item and shrink the line: whichever factor
            // is nearer 1 (a shrink blocked by the room left, 0, always closes).
            const float shrink = std::max(0.0f, fit(cur.count + 1, widths + w));
            const float expand = fit(cur.count, widths);
            if (shrink <= 0.0f || expand - 1.0f < 1.0f - shrink)
            {
                close(stretch_ == LinedFlowLayoutItemsStretch::Fill ? expand : 1.0f);
                add(i);
            }
            else
            {
                add(i);
                close(shrink);
                continue;
            }
        }
        const float lw = line_width(cur.count, widths);
        if (lw >= width)
        {
            // Full, or a single item wider than the room, which is shrunk to fit.
            close(lw > width ? fit(cur.count, widths) : 1.0f);
        }
    }
    if (cur.count > 0)
    {
        cur.last = true;
        close(1.0f); // the last, partial line is never stretched
        lines_.back().last = true;
    }
    for (std::size_t l = 0; l < lines_.size(); ++l)
    {
        for (std::size_t k = 0; k < lines_[l].count; ++k)
        {
            line_of_[lines_[l].first + k] = l;
        }
    }
}

Size LinedFlowLayoutEngine::measure(VirtualizingLayoutContext& context, Size available)
{
    const std::size_t count = context.item_count();
    natural_.resize(count, -1.0f);
    const float width = available.width;
    width_            = width;
    realized_first_ = realized_end_ = 0;
    if (count == 0)
    {
        lines_.clear();
        line_of_.clear();
        return Size{std::isfinite(width) ? width : 0.0f, 0.0f};
    }

    // The line height in use: the set line height, else item 0's height.
    if (line_height_)
    {
        actual_line_height_ = *line_height_;
    }
    else
    {
        Element& first      = context.get_or_create_element_at(0, ElementRealizationOptions::ForceCreate);
        actual_line_height_ = context.measure(first, available).height;
    }
    const float h = actual_line_height_;
    if (h <= 0.0f)
    {
        lines_.clear();
        return Size{std::isfinite(width) ? width : 0.0f, 0.0f};
    }

    const Rect  window        = context.realization_rect();
    const float stride        = h + line_spacing_;
    const auto  visible_items = [&]
    {
        std::size_t first = count;
        std::size_t end   = 0;
        for (std::size_t l = 0; l < lines_.size(); ++l)
        {
            const float top = static_cast<float>(l) * stride;
            if (is_unbounded(window) || (top + h >= window.top() && top <= window.bottom()))
            {
                first = std::min(first, lines_[l].first);
                end   = std::max(end, lines_[l].first + lines_[l].count);
            }
        }
        return std::pair{first, std::max(first, end)};
    };

    // Lay out from what is known, learn the natural widths of what that puts in view, and lay out
    // again when they were not what was assumed (twice at most).
    build_lines(width);
    auto range = visible_items();
    for (int pass = 0; pass < 2; ++pass)
    {
        bool changed = false;
        for (std::size_t i = range.first; i < range.second; ++i)
        {
            Element&    element = context.get_or_create_element_at(i);
            const float natural = context.measure(element, Size{LinedFlowLayoutInfinity, h}).width;
            if (natural != natural_[i])
            {
                changed     = true;
                natural_[i] = natural;
            }
        }
        if (!changed)
        {
            break;
        }
        float       ratios = 0.0f;
        std::size_t known  = 0;
        for (float w : natural_)
        {
            if (w >= 0.0f)
            {
                ratios += w / h;
                ++known;
            }
        }
        average_ratio_ = known > 0 ? ratios / static_cast<float>(known) : 1.0f;
        build_lines(width);
        range = visible_items();
    }

    float widest = 0.0f;
    for (const Line& line : lines_)
    {
        float lw = 0.0f;
        for (std::size_t k = 0; k < line.count; ++k)
        {
            lw += item_width(line.first + k) * line.scale;
        }
        widest = std::max(widest, lw + min_item_spacing_ * static_cast<float>(line.count - 1));
    }
    for (std::size_t i = range.first; i < range.second; ++i)
    {
        Element& element = context.get_or_create_element_at(i);
        (void)context.measure(element, Size{item_width(i) * lines_[line_of_[i]].scale, h});
    }
    realized_first_ = range.first;
    realized_end_   = range.second;

    const float lines  = static_cast<float>(lines_.size());
    const float height = lines * h + std::max(0.0f, lines - 1.0f) * line_spacing_;
    if (!std::isfinite(width))
    {
        return Size{widest, h};
    }
    return Size{std::max(width, widest), height};
}

Rect LinedFlowLayoutEngine::item_rect(std::size_t line_index, std::size_t index, float width) const
{
    const Line& line = lines_[line_index];
    const float h    = actual_line_height_;
    const float s    = min_item_spacing_;
    const float n    = static_cast<float>(line.count);
    float       sum  = 0.0f;
    for (std::size_t k = 0; k < line.count; ++k)
    {
        sum += item_width(line.first + k) * line.scale;
    }
    const float extra = std::isfinite(width) ? width - sum - s * (n - 1.0f) : 0.0f;
    float       x0    = 0.0f;
    float       gap   = s;
    if (stretch_ == LinedFlowLayoutItemsStretch::Fill)
    {
        // Edge to edge, the gaps absorbing what the scaling left. The last line keeps its spacing,
        // left-aligned.
        if (!line.last && n > 1.0f)
        {
            gap += extra / (n - 1.0f);
        }
    }
    else if (extra > 0.0f)
    {
        switch (justification_)
        {
            case ItemsJustification::Start:
                break;
            case ItemsJustification::Center:
                x0 = extra / 2.0f;
                break;
            case ItemsJustification::End:
                x0 = extra;
                break;
            case ItemsJustification::SpaceEvenly:
                x0 = extra / (n + 1.0f);
                gap += extra / (n + 1.0f);
                break;
            case ItemsJustification::SpaceAround:
                x0 = extra / n / 2.0f;
                gap += extra / n;
                break;
            case ItemsJustification::SpaceBetween:
                if (n > 1.0f)
                {
                    gap += extra / (n - 1.0f);
                }
                break;
        }
    }
    else if (extra < 0.0f && n > 1.0f && !line.last)
    {
        gap += extra / (n - 1.0f);
    }
    float x = x0;
    for (std::size_t k = line.first; k < index; ++k)
    {
        x += item_width(k) * line.scale + gap;
    }
    return Rect{x, static_cast<float>(line_index) * (h + line_spacing_), item_width(index) * line.scale, h};
}

Size LinedFlowLayoutEngine::arrange(VirtualizingLayoutContext& context, Size final_size)
{
    const float width = std::isfinite(width_) ? final_size.width : LinedFlowLayoutInfinity;
    for (std::size_t i = realized_first_; i < realized_end_ && i < line_of_.size(); ++i)
    {
        context.arrange(context.get_or_create_element_at(i), item_rect(line_of_[i], i, width));
    }
    return final_size;
}

void LinedFlowLayoutEngine::on_items_changed(VirtualizingLayoutContext& context, const ItemsChange& change)
{
    (void)context;
    switch (change.action)
    {
        case ItemsChangeAction::Add:
            natural_.insert(natural_.begin() + static_cast<std::ptrdiff_t>(std::min(change.new_index, natural_.size())),
                            change.new_count,
                            -1.0f);
            break;
        case ItemsChangeAction::Remove:
        {
            const std::size_t lo = std::min(change.old_index, natural_.size());
            const std::size_t hi = std::min(change.old_index + change.old_count, natural_.size());
            natural_.erase(natural_.begin() + static_cast<std::ptrdiff_t>(lo),
                           natural_.begin() + static_cast<std::ptrdiff_t>(hi));
            break;
        }
        case ItemsChangeAction::Replace:
        case ItemsChangeAction::Move:
        case ItemsChangeAction::Reset:
            natural_.clear();
            break;
    }
    realized_first_ = realized_end_ = 0;
    invalidate_measure();
}

} // namespace ui
