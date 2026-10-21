// SPDX-License-Identifier: MIT
#include "FlowLayoutAlgorithm.hpp"

#include "winui/core/Geometry.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/layouts/Layout.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui::detail
{

// ---- measure ----

Size FlowLayoutAlgorithm::measure(Size                       available,
                                  VirtualizingLayoutContext& context,
                                  const MeasureParams&       params,
                                  FlowAlgorithmDelegates&    delegates)
{
    om_           = OrientationMeasures{params.vertical_scroll};
    context_      = &context;
    same_as_flow_ = std::isinf(om_.minor(available));
    window_       = context.realization_rect();
    virtualizing_ = params.virtualize && !is_unbounded(window_);

    const std::size_t count = context.item_count();
    if (count == 0)
    {
        clear_realized_range();
    }
    else if (first_index_ + elements_.size() > count)
    {
        // Items the collection no longer has (a change the host did not tell the layout about).
        discard_beyond(true, count - 1);
    }

    // Before measuring, recycle what is outside the window up front, keeping one
    // outside each end, which protects an anchor being brought into view.
    if (virtualizing_)
    {
        discard_outside_window();
    }

    const std::optional<std::size_t> anchor = anchor_index(available, params, delegates);
    generate(true, anchor, available, params, delegates);
    generate(false, anchor, available, params, delegates);

    // Backward generation right-aligns the lines it starts; once the first item is realized the
    // lines are laid out from it again, left-aligned (FlowLayoutAlgorithm's reflow).
    if (params.wrapping && is_realized(0) && om_.minor_start(bounds_at(0)) != 0.0f)
    {
        om_.set_minor_start(bounds_at(0), 0.0f);
        generate(true, std::size_t{0}, available, params, delegates);
    }

    raise_line_arranged(delegates);

    const Element* first = nullptr;
    const Element* last  = nullptr;
    Rect           first_rect{};
    Rect           last_rect{};
    std::size_t    first_index = 0;
    std::size_t    last_index  = 0;
    for (std::size_t i = 0; i < elements_.size(); ++i)
    {
        if (elements_[i] != nullptr)
        {
            first       = elements_[i];
            first_rect  = bounds_[i];
            first_index = first_index_ + i;
            break;
        }
    }
    for (std::size_t i = elements_.size(); i-- > 0;)
    {
        if (elements_[i] != nullptr)
        {
            last       = elements_[i];
            last_rect  = bounds_[i];
            last_index = first_index_ + i;
            break;
        }
    }
    last_extent_ = delegates.extent(available, context, first, first_index, first_rect, last, last_index, last_rect);
    context.set_layout_origin(Point{last_extent_.x, last_extent_.y});

    last_available_minor_      = om_.minor(available);
    last_item_spacing_         = params.min_item_spacing;
    collection_change_pending_ = false;
    return last_extent_.size();
}

std::optional<std::size_t> FlowLayoutAlgorithm::anchor_index(Size available, const MeasureParams& params, FlowAlgorithmDelegates& d)
{
    const std::size_t          count = context_->item_count();
    std::optional<std::size_t> anchor;
    float                      minor_pos = 0.0f;
    float                      major_pos = 0.0f;

    if (!virtualizing_)
    {
        if (count > 0)
        {
            anchor = 0;
        }
    }
    else
    {
        const bool reeval = params.wrapping &&
                            (om_.minor(available) != last_available_minor_ ||
                             params.min_item_spacing != last_item_spacing_ || collection_change_pending_);
        const std::optional<std::size_t> suggested = context_->recommended_anchor_index();
        if (suggested && *suggested < count)
        {
            // A bring-into-view target: start from the item the layout says reaches it.
            const AnchorInfo info = d.anchor_for_target(*suggested, available, *context_);
            anchor                = info.index;
            if (anchor && is_realized(*anchor))
            {
                const Rect& b = bounds_at(*anchor);
                minor_pos     = reeval ? 0.0f : om_.minor_start(b);
                major_pos     = om_.major_start(b);
            }
            else
            {
                major_pos = info.offset;
            }
        }
        else if (reeval || !is_window_connected())
        {
            const AnchorInfo info = d.anchor_for_realization_rect(available, *context_);
            anchor                = info.index;
            major_pos             = info.offset;
        }
        else
        {
            for (std::size_t i = 0; i < elements_.size(); ++i)
            {
                if (elements_[i] != nullptr)
                {
                    anchor    = first_index_ + i;
                    minor_pos = om_.minor_start(bounds_[i]);
                    major_pos = om_.major_start(bounds_[i]);
                    break;
                }
            }
        }
    }

    if (anchor && *anchor < count)
    {
        // An anchor outside the realized range starts a new one; one inside it (a slot a collection
        // change emptied) is simply filled.
        const bool in_range = *anchor >= first_index_ && *anchor - first_index_ < elements_.size();
        // An anchor right next to the range (one item inserted just before it, or appended after it)
        // extends it, so the rows already realized keep their elements.
        const bool adjacent = !elements_.empty() &&
                              (*anchor + 1 == first_index_ || *anchor == first_index_ + elements_.size());
        if (!in_range && !adjacent)
        {
            clear_realized_range();
        }
        ensure_realized(*anchor);
        const Size provisional = measure_element(*anchor, available, d);
        bounds_at(*anchor)     = om_.make_rect(minor_pos, major_pos, om_.minor(provisional), om_.major(provisional));
        return anchor;
    }
    clear_realized_range();
    return std::nullopt;
}

void FlowLayoutAlgorithm::generate(bool                       forward,
                                   std::optional<std::size_t> anchor,
                                   Size                       available,
                                   const MeasureParams&       params,
                                   FlowAlgorithmDelegates&    d)
{
    if (!anchor || !is_realized(*anchor))
    {
        return;
    }
    const std::size_t count   = context_->item_count();
    const auto        next_of = [&](std::size_t i) -> std::optional<std::size_t>
    {
        if (forward)
        {
            return i + 1 < count ? std::optional<std::size_t>{i + 1} : std::nullopt;
        }
        return i > 0 ? std::optional<std::size_t>{i - 1} : std::nullopt;
    };

    std::size_t previous    = *anchor;
    float       line_offset = om_.major_start(bounds_at(previous));
    float       line_major  = om_.major_size(bounds_at(previous));
    std::size_t in_line     = 1;
    if (params.max_items_per_line != Unlimited && params.max_items_per_line > 0)
    {
        const std::size_t column = *anchor % params.max_items_per_line;
        in_line                  = forward ? column + 1 : params.max_items_per_line - column;
    }
    bool       reposition      = false;
    const auto reposition_line = [&](std::size_t last_generated)
    {
        // Every item on the line just finished gets the line's major size.
        for (std::size_t k = 0; k < in_line; ++k)
        {
            const std::size_t index = forward ? last_generated - k : last_generated + k;
            if (is_realized(index))
            {
                om_.set_major_size(bounds_at(index), line_major);
            }
        }
    };

    std::optional<std::size_t> next = next_of(previous);
    while (next && (!virtualizing_ || should_continue_filling(forward, previous)))
    {
        const std::size_t index = *next;
        ensure_realized(index);
        const Size desired = measure_element(index, available, d);
        const Rect prev    = bounds_at(previous);
        Rect       cur     = om_.make_rect(0.0f, 0.0f, om_.minor(desired), om_.major(desired));
        if (forward)
        {
            const float remaining = om_.minor(available) -
                                    (om_.minor_end(prev) + params.min_item_spacing + om_.minor(desired));
            if (in_line >= params.max_items_per_line || d.should_break_line(index, remaining))
            {
                om_.set_minor_start(cur, 0.0f);
                om_.set_major_start(cur, om_.major_start(prev) + line_major + params.line_spacing);
                if (reposition)
                {
                    reposition_line(previous);
                }
                line_major  = om_.major_size(cur);
                line_offset = om_.major_start(cur);
                in_line     = 1;
                reposition  = false;
            }
            else
            {
                om_.set_minor_start(cur, om_.minor_end(prev) + params.min_item_spacing);
                om_.set_major_start(cur, line_offset);
                reposition = reposition || om_.major_size(prev) != om_.major_size(cur);
                line_major = std::max(line_major, om_.major_size(cur));
                ++in_line;
            }
        }
        else
        {
            const float remaining = om_.minor_start(prev) - (om_.minor(desired) + params.min_item_spacing);
            if (in_line >= params.max_items_per_line || d.should_break_line(index, remaining))
            {
                const float avail_minor = om_.minor(available);
                const float start = params.wrapping
                                        ? (std::isfinite(avail_minor) ? avail_minor - om_.minor(desired)
                                                                      : om_.minor_size(last_extent_) - om_.minor(desired))
                                        : 0.0f;
                om_.set_minor_start(cur, start);
                om_.set_major_start(cur, line_offset - om_.major(desired) - params.line_spacing);
                if (reposition)
                {
                    reposition_line(previous);
                }
                line_major  = om_.major_size(cur);
                line_offset = om_.major_start(cur);
                in_line     = 1;
                reposition  = false;
            }
            else
            {
                om_.set_minor_start(cur, om_.minor_start(prev) - om_.minor(desired) - params.min_item_spacing);
                om_.set_major_start(cur, line_offset);
                reposition = reposition || om_.major_size(prev) != om_.major_size(cur);
                line_major = std::max(line_major, om_.major_size(cur));
                ++in_line;
            }
        }
        bounds_at(index) = cur;
        previous         = index;
        next             = next_of(previous);
    }
    if (reposition)
    {
        reposition_line(previous);
    }

    // Whatever was realized beyond the last item generated goes back to the host.
    if (virtualizing_)
    {
        discard_beyond(forward, previous);
    }
}

Size FlowLayoutAlgorithm::measure_element(std::size_t index, Size available, FlowAlgorithmDelegates& d)
{
    Element&   element      = *elements_[index - first_index_];
    const Size measure_size = d.measure_size(index, available);
    const Size desired      = context_->measure(element, measure_size);
    const Size provisional  = d.provisional_arrange_size(index, measure_size, desired);
    d.on_element_measured(index, available, measure_size, desired, provisional);
    return provisional;
}

bool FlowLayoutAlgorithm::should_continue_filling(bool forward, std::size_t previous) const
{
    // Tests the item *before* the one about to be generated, so one item past each edge of the window
    // is always realized.
    const Rect& b = bounds_[previous - first_index_];
    if (same_as_flow_)
    {
        return forward ? om_.minor_start(b) < om_.minor_end(window_) : om_.minor_end(b) > om_.minor_start(window_);
    }
    if (forward)
    {
        return om_.major_start(b) < om_.major_end(window_) && om_.minor_start(b) < om_.minor_end(window_);
    }
    return om_.major_end(b) > om_.major_start(window_) && om_.minor_end(b) > om_.minor_start(window_);
}

bool FlowLayoutAlgorithm::is_window_connected() const
{
    std::size_t lo = 0;
    std::size_t hi = elements_.size();
    while (lo < hi && elements_[lo] == nullptr)
    {
        ++lo;
    }
    while (hi > lo && elements_[hi - 1] == nullptr)
    {
        --hi;
    }
    if (lo == hi)
    {
        return false;
    }
    const Rect& first = bounds_[lo];
    const Rect& last  = bounds_[hi - 1];
    if (same_as_flow_)
    {
        return om_.minor_start(first) <= om_.minor_end(window_) && om_.minor_end(last) >= om_.minor_start(window_);
    }
    return om_.major_start(first) <= om_.major_end(window_) && om_.major_end(last) >= om_.major_start(window_);
}

void FlowLayoutAlgorithm::discard_outside_window()
{
    const std::size_t n = elements_.size();
    if (n == 0)
    {
        return;
    }
    const auto intersects = [&](std::size_t i)
    {
        if (elements_[i] == nullptr)
        {
            return false;
        }
        const Rect& b = bounds_[i];
        if (same_as_flow_)
        {
            return om_.minor_end(b) >= om_.minor_start(window_) && om_.minor_start(b) <= om_.minor_end(window_);
        }
        return om_.major_end(b) >= om_.major_start(window_) && om_.major_start(b) <= om_.major_end(window_);
    };
    std::size_t front = 0;
    while (front < n && !intersects(front))
    {
        ++front;
    }
    if (front == n)
    {
        clear_realized_range();
        return;
    }
    std::size_t back = 0;
    while (back < n && !intersects(n - 1 - back))
    {
        ++back;
    }
    // Keep one outside each end.
    if (back > 1)
    {
        discard_beyond(true, first_index_ + (n - back));
    }
    if (front > 1)
    {
        discard_beyond(false, first_index_ + front - 1);
    }
}

void FlowLayoutAlgorithm::discard_beyond(bool forward, std::size_t index)
{
    if (elements_.empty())
    {
        return;
    }
    if (forward)
    {
        // Everything after `index`.
        if (index < first_index_)
        {
            clear_realized_range();
            return;
        }
        const std::size_t keep = std::min(elements_.size(), index - first_index_ + 1);
        for (std::size_t i = keep; i < elements_.size(); ++i)
        {
            if (elements_[i] != nullptr)
            {
                context_->recycle_element(*elements_[i]);
            }
        }
        elements_.resize(keep);
        bounds_.resize(keep);
        return;
    }
    // Everything before `index`.
    if (index <= first_index_)
    {
        return;
    }
    const std::size_t drop = std::min(elements_.size(), index - first_index_);
    for (std::size_t i = 0; i < drop; ++i)
    {
        if (elements_[i] != nullptr)
        {
            context_->recycle_element(*elements_[i]);
        }
    }
    elements_.erase(elements_.begin(), elements_.begin() + static_cast<std::ptrdiff_t>(drop));
    bounds_.erase(bounds_.begin(), bounds_.begin() + static_cast<std::ptrdiff_t>(drop));
    first_index_ += drop;
}

void FlowLayoutAlgorithm::clear_realized_range()
{
    if (context_ != nullptr)
    {
        for (Element* e : elements_)
        {
            if (e != nullptr)
            {
                context_->recycle_element(*e);
            }
        }
    }
    elements_.clear();
    bounds_.clear();
    first_index_ = 0;
}

void FlowLayoutAlgorithm::ensure_realized(std::size_t index)
{
    const auto fetch = [&]
    { return &context_->get_or_create_element_at(index, ElementRealizationOptions::SuppressAutoRecycle); };
    if (elements_.empty())
    {
        first_index_ = index;
        elements_.push_back(fetch());
        bounds_.emplace_back();
        return;
    }
    if (index >= first_index_ && index - first_index_ < elements_.size())
    {
        if (elements_[index - first_index_] == nullptr)
        {
            elements_[index - first_index_] = fetch();
        }
        return;
    }
    if (index == first_index_ + elements_.size())
    {
        elements_.push_back(fetch());
        bounds_.emplace_back();
        return;
    }
    if (index + 1 == first_index_)
    {
        elements_.insert(elements_.begin(), fetch());
        bounds_.insert(bounds_.begin(), Rect{});
        first_index_ = index;
        return;
    }
    // Not next to the range: start a new one there.
    clear_realized_range();
    first_index_ = index;
    elements_.push_back(fetch());
    bounds_.emplace_back();
}

void FlowLayoutAlgorithm::raise_line_arranged(FlowAlgorithmDelegates& d)
{
    // Only whole lines count: the first realized line may have been started mid-line by backward
    // generation and the last cut short by the window, unless they are the collection's own ends
    // (only the lines inside the window are reported).
    struct Line
    {
        std::size_t start;
        std::size_t count;
        float       size;
    };
    std::vector<Line> lines;
    std::size_t       i = 0;
    while (i < elements_.size())
    {
        if (elements_[i] == nullptr)
        {
            ++i;
            continue;
        }
        const float start = om_.major_start(bounds_[i]);
        float       size  = 0.0f;
        std::size_t j     = i;
        while (j < elements_.size() && elements_[j] != nullptr && om_.major_start(bounds_[j]) == start)
        {
            size = std::max(size, om_.major_size(bounds_[j]));
            ++j;
        }
        lines.push_back(Line{first_index_ + i, j - i, size});
        i = j;
    }
    const std::size_t count = context_->item_count();
    for (std::size_t k = 0; k < lines.size(); ++k)
    {
        const Line& line  = lines[k];
        const bool  whole = (k > 0 || line.start == 0) && (k + 1 < lines.size() || line.start + line.count == count);
        if (whole || !virtualizing_)
        {
            d.on_line_arranged(line.start, line.count, line.size, *context_);
        }
    }
}

// ---- arrange ----

Size FlowLayoutAlgorithm::arrange(Size final_size, VirtualizingLayoutContext& context, bool wrapping, LineAlignment alignment)
{
    context_            = &context;
    const std::size_t n = elements_.size();
    std::size_t       i = 0;
    while (i < n)
    {
        if (elements_[i] == nullptr)
        {
            ++i;
            continue;
        }
        const float line_start = om_.major_start(bounds_[i]);
        float       line_size  = 0.0f;
        std::size_t j          = i;
        while (j < n && elements_[j] != nullptr && om_.major_start(bounds_[j]) == line_start)
        {
            line_size = std::max(line_size, om_.major_size(bounds_[j]));
            ++j;
        }

        // Align the line along the minor axis.
        const float space_start = om_.minor_start(bounds_[i]);
        const float space_end   = om_.minor(final_size) - om_.minor_end(bounds_[j - 1]);
        const float total       = space_start + space_end;
        const float count       = static_cast<float>(j - i);
        for (std::size_t k = i; k < j; ++k)
        {
            Rect b = bounds_[k];
            om_.set_major_size(b, line_size);
            if (!same_as_flow_ && (space_start != 0.0f || space_end != 0.0f))
            {
                const float kf = static_cast<float>(k - i);
                float       m  = om_.minor_start(b);
                switch (alignment)
                {
                    case LineAlignment::Start:
                        m -= space_start;
                        break;
                    case LineAlignment::End:
                        m += space_end;
                        break;
                    case LineAlignment::Center:
                        m += -space_start + total / 2.0f;
                        break;
                    case LineAlignment::SpaceAround:
                        m += -space_start + (total / (2.0f * count)) * (2.0f * kf + 1.0f);
                        break;
                    case LineAlignment::SpaceBetween:
                        m += -space_start + (count > 1.0f ? total / (count - 1.0f) : 0.0f) * kf;
                        break;
                    case LineAlignment::SpaceEvenly:
                        m += -space_start + (total / (count + 1.0f)) * (kf + 1.0f);
                        break;
                }
                om_.set_minor_start(b, m);
            }
            b.x -= last_extent_.x;
            b.y -= last_extent_.y;
            if (!wrapping)
            {
                om_.set_minor_size(b, std::max(om_.minor_size(b), om_.minor(final_size)));
            }
            context.arrange(*elements_[k], b);
        }
        i = j;
    }
    return Size{std::max(final_size.width, last_extent_.width), std::max(final_size.height, last_extent_.height)};
}

// ---- collection changes ----

void FlowLayoutAlgorithm::on_items_changed(VirtualizingLayoutContext& context, const ItemsChange& change)
{
    context_                   = &context;
    collection_change_pending_ = true;
    // The host clears the elements of removed and replaced items itself; the range only forgets them.
    const auto remove_range = [&](std::size_t index, std::size_t n)
    {
        const std::size_t end          = first_index_ + elements_.size();
        const std::size_t lo           = std::max(index, first_index_);
        const std::size_t hi           = std::min(index + n, end);
        std::size_t       before_first = 0;
        if (index < first_index_)
        {
            before_first = std::min(n, first_index_ - index);
        }
        if (lo < hi)
        {
            elements_.erase(elements_.begin() + static_cast<std::ptrdiff_t>(lo - first_index_),
                            elements_.begin() + static_cast<std::ptrdiff_t>(hi - first_index_));
            bounds_.erase(bounds_.begin() + static_cast<std::ptrdiff_t>(lo - first_index_),
                          bounds_.begin() + static_cast<std::ptrdiff_t>(hi - first_index_));
        }
        first_index_ -= before_first;
        if (elements_.empty())
        {
            first_index_ = 0;
        }
    };
    const auto insert_range = [&](std::size_t index, std::size_t n)
    {
        if (elements_.empty())
        {
            return;
        }
        if (index <= first_index_)
        {
            first_index_ += n;
        }
        else if (index < first_index_ + elements_.size())
        {
            elements_.insert(elements_.begin() + static_cast<std::ptrdiff_t>(index - first_index_), n, nullptr);
            bounds_.insert(bounds_.begin() + static_cast<std::ptrdiff_t>(index - first_index_), n, Rect{});
        }
    };
    switch (change.action)
    {
        case ItemsChangeAction::Reset:
            elements_.clear();
            bounds_.clear();
            first_index_ = 0;
            break;
        case ItemsChangeAction::Add:
            insert_range(change.new_index, change.new_count);
            break;
        case ItemsChangeAction::Remove:
            remove_range(change.old_index, change.old_count);
            break;
        case ItemsChangeAction::Replace:
            for (std::size_t i = change.old_index; i < change.old_index + change.old_count; ++i)
            {
                if (i >= first_index_ && i - first_index_ < elements_.size())
                {
                    elements_[i - first_index_] = nullptr;
                }
            }
            if (change.new_count != change.old_count)
            {
                elements_.clear();
                bounds_.clear();
                first_index_ = 0;
            }
            break;
        case ItemsChangeAction::Move:
        {
            // One item moving inside the range takes its element along: the host keeps that element
            // too, so the range stays whole and nothing next to it is made again.
            const std::size_t end = first_index_ + elements_.size();
            if (change.old_count == 1 && change.new_count == 1 && change.old_index >= first_index_ &&
                change.old_index < end && change.new_index >= first_index_ && change.new_index < end)
            {
                const auto from = static_cast<std::ptrdiff_t>(change.old_index - first_index_);
                const auto to   = static_cast<std::ptrdiff_t>(change.new_index - first_index_);
                const auto turn = [from, to](auto& items)
                {
                    if (from < to)
                    {
                        std::rotate(items.begin() + from, items.begin() + from + 1, items.begin() + to + 1);
                    }
                    else
                    {
                        std::rotate(items.begin() + to, items.begin() + from, items.begin() + from + 1);
                    }
                };
                turn(elements_);
                turn(bounds_);
                break;
            }
            remove_range(change.old_index, change.old_count);
            insert_range(change.new_index, change.new_count);
            break;
        }
    }
}

void FlowLayoutAlgorithm::reset()
{
    elements_.clear();
    bounds_.clear();
    first_index_          = 0;
    last_extent_          = Rect{};
    last_available_minor_ = -1.0f;
    last_item_spacing_    = -1.0f;
    context_              = nullptr;
}

} // namespace ui::detail
