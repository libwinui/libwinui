// SPDX-License-Identifier: MIT
#include "winui/ui/panels/TilePanel.hpp"

#include "../LayoutSnapshot.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ui
{

namespace
{

constexpr float TilePanelInfinity = std::numeric_limits<float>::infinity();

// Where a block of `required` starts inside `available`, and
// the gap Stretch puts around each of `lines` lines. Nothing moves in an unbounded extent.
void alignment_offsets(Alignment alignment, float available, float required, std::size_t lines, float& starting, float& justification)
{
    starting      = 0.0f;
    justification = 0.0f;
    if (available == TilePanelInfinity)
    {
        return;
    }
    switch (alignment)
    {
        case Alignment::Center:
            starting = std::max((available - required) * 0.5f, 0.0f);
            break;
        case Alignment::Trailing:
            starting = std::max(available - required, 0.0f);
            break;
        case Alignment::Stretch:
            justification = std::max((available - required) / static_cast<float>(lines + 1), 0.0f);
            break;
        case Alignment::Leading:
            break;
    }
}

} // namespace

// ---- attached properties ----

TilePanelControl::Span TilePanelControl::span_of(std::size_t index) const
{
    // Anything below 1 counts as 1.
    const Span s = index < spans_.size() ? spans_[index] : Span{};
    return Span{std::max(1, s.rows), std::max(1, s.columns)};
}

void TilePanelControl::set_row_span(const Element* child, int span)
{
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        if (children_[i].get() == child && spans_[i].rows != span)
        {
            spans_[i].rows = span;
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
}

void TilePanelControl::set_column_span(const Element* child, int span)
{
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        if (children_[i].get() == child && spans_[i].columns != span)
        {
            spans_[i].columns = span;
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
}

int TilePanelControl::row_span(const Element* child) const
{
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        if (children_[i].get() == child)
        {
            return spans_[i].rows;
        }
    }
    return 1;
}

int TilePanelControl::column_span(const Element* child) const
{
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        if (children_[i].get() == child)
        {
            return spans_[i].columns;
        }
    }
    return 1;
}

// ---- occupancy map ----

bool TilePanelControl::OccupancyMap::find_next_available(std::size_t row_span, std::size_t column_span)
{
    // Walk the cells line by line from the current one until the
    // span fits; true when it does, false (and back to the start) when the map is full.
    row_span    = std::min(row_span, max_row);
    column_span = std::min(column_span, max_col);
    while (current_col < max_col)
    {
        bool occupied = true;
        if (current_row == max_row)
        {
            current_row = 0;
            current_col += 1;
        }
        for (std::size_t j = 0; j < column_span; ++j)
        {
            if (j + current_col >= max_col)
            {
                column_span = max_col - current_col;
                break;
            }
            for (std::size_t i = 0; i < row_span; ++i)
            {
                occupied = true;
                if (current_row + i < max_row && current_col + j < max_col)
                {
                    occupied = at(current_row + i, current_col + j) != 0;
                }
                if (occupied)
                {
                    break;
                }
            }
            if (occupied)
            {
                break;
            }
        }
        if (occupied)
        {
            ++current_row;
            if (current_row == max_row || current_row + row_span > max_row)
            {
                current_row = 0;
                ++current_col;
            }
        }
        else
        {
            break;
        }
    }
    if (current_col >= max_col || current_row >= max_row)
    {
        current_col = 0;
        current_row = 0;
        return false;
    }
    return true;
}

void TilePanelControl::OccupancyMap::fill_current(std::size_t row_span, std::size_t column_span, std::uint32_t value)
{
    // The span is marked as far as it fits, and the cursor moves on past it.
    for (std::size_t col = 0; col < column_span; ++col)
    {
        for (std::size_t row = 0; row < row_span; ++row)
        {
            if (current_col + col < max_col && current_row + row < max_row)
            {
                set(current_row + row, current_col + col, value);
            }
        }
    }
    locations[value - 1] = Location{current_row, current_col};
    current_row += row_span;
    if (current_row >= max_row)
    {
        current_row = 0;
        current_col += 1;
    }
}

void TilePanelControl::OccupancyMap::used_size(std::size_t& lines, std::size_t& per_line) const
{
    // The last line and the last position within a line holding anything.
    std::size_t last_col = 0;
    std::size_t last_row = 0;
    for (std::size_t col = 0; col < max_col; ++col)
    {
        for (std::size_t row = 0; row < max_row; ++row)
        {
            if (at(row, col) != 0)
            {
                last_col = col;
                break;
            }
        }
    }
    for (std::size_t row = 0; row < max_row; ++row)
    {
        for (std::size_t col = 0; col < max_col; ++col)
        {
            if (at(row, col) != 0)
            {
                last_row = row;
                break;
            }
        }
    }
    lines    = last_col + 1;
    per_line = last_row + 1;
}

Rect TilePanelControl::OccupancyMap::item_rect(std::size_t index, Size item) const
{
    // The cells the child actually got, found by walking out from its first
    // cell while they still hold the same child.
    const Location      loc   = locations[index];
    const std::uint32_t value = at(loc.row, loc.col);

    std::size_t left = loc.col;
    while (left > 0 && at(loc.row, left - 1) == value)
    {
        --left;
    }
    std::size_t right = loc.col;
    while (right + 1 < max_col && at(loc.row, right + 1) == value)
    {
        ++right;
    }
    std::size_t top = loc.row;
    while (top > 0 && at(top - 1, loc.col) == value)
    {
        --top;
    }
    std::size_t bottom = loc.row;
    while (bottom + 1 < max_row && at(bottom + 1, loc.col) == value)
    {
        ++bottom;
    }
    const auto col_span = static_cast<float>(right - left + 1);
    const auto row_span = static_cast<float>(bottom - top + 1);
    const auto row      = static_cast<float>(loc.row);
    const auto col      = static_cast<float>(loc.col);
    if (horizontal)
    {
        return Rect{item.width * row, item.height * col, item.width * row_span, item.height * col_span};
    }
    return Rect{item.width * col, item.height * row, item.width * col_span, item.height * row_span};
}

// ---- layout ----

void TilePanelControl::compute_alignment(Size available, Size& starting, Size& justification) const
{
    // The orientation is deliberately not read here: the width
    // aligns against the lines and the height against the cells in a line, whatever the orientation.
    alignment_offsets(horizontal_children_alignment_,
                      available.width,
                      item_size_.width * static_cast<float>(line_count_),
                      line_count_,
                      starting.width,
                      justification.width);
    alignment_offsets(vertical_children_alignment_,
                      available.height,
                      item_size_.height * static_cast<float>(items_per_line_),
                      items_per_line_,
                      starting.height,
                      justification.height);
}

Size TilePanelControl::measure_override(Size available, const MeasureContext& context) const
{
    const bool horizontal = orientation_ == Orientation::Horizontal;
    items_per_line_       = 0;
    line_count_           = 0;
    map_                  = OccupancyMap{};

    // A fixed cell size, or the room itself until the first child says otherwise.
    float item_w = item_width_ ? *item_width_ : available.width;
    float item_h = item_height_ ? *item_height_ : available.height;
    if (!children_.empty())
    {
        const Span first = span_of(0);
        const Size offer{item_width_ ? item_w * static_cast<float>(first.columns) : item_w,
                         item_height_ ? item_h * static_cast<float>(first.rows) : item_h};
        const Size desired = children_.front()->measure(offer, context);
        if (!item_width_)
        {
            item_w = desired.width;
        }
        if (!item_height_)
        {
            item_h = desired.height;
        }
    }
    item_size_ = Size{item_w, item_h};
    if (children_.empty())
    {
        return Size{};
    }

    // The occupancy map.
    std::size_t row_tiles    = 0;
    std::size_t column_tiles = 0;
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        row_tiles += static_cast<std::size_t>(span_of(i).rows);
        column_tiles += static_cast<std::size_t>(span_of(i).columns);
    }
    const float direct_extent   = horizontal ? available.width : available.height;
    const float direct_item     = horizontal ? item_w : item_h;
    const float indirect_extent = horizontal ? available.height : available.width;
    const float indirect_item   = horizontal ? item_h : item_w;

    std::size_t per_line = horizontal ? column_tiles : row_tiles;
    if (direct_extent != TilePanelInfinity && direct_item > 0.0f)
    {
        per_line = static_cast<std::size_t>(direct_extent / direct_item);
    }
    per_line = std::max<std::size_t>(1, per_line);
    if (maximum_rows_or_columns_ > 0)
    {
        per_line = std::min(per_line, maximum_rows_or_columns_);
    }
    std::size_t max_col = horizontal ? row_tiles : column_tiles;
    if (indirect_extent != TilePanelInfinity && indirect_item > 0.0f)
    {
        max_col = std::min(static_cast<std::size_t>(std::floor(indirect_extent / indirect_item)), max_col);
    }
    max_col = std::max<std::size_t>(1, max_col);

    map_.max_row       = per_line;
    map_.max_col       = max_col;
    map_.horizontal    = horizontal;
    map_.cells         = std::vector<std::uint32_t>(per_line * max_col, 0u);
    map_.locations     = std::vector<OccupancyMap::Location>(children_.size());
    map_.element_count = children_.size();
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        const Span  s        = span_of(i);
        std::size_t row_span = static_cast<std::size_t>(s.rows);
        std::size_t col_span = static_cast<std::size_t>(s.columns);
        if (horizontal)
        {
            std::swap(row_span, col_span); // spans along the orientation
        }
        if (!map_.find_next_available(row_span, col_span))
        {
            map_.element_count = i;
            break;
        }
        map_.fill_current(row_span, col_span, static_cast<std::uint32_t>(i + 1));
    }
    map_.used_size(line_count_, items_per_line_);

    const auto tile_offer = [&](std::size_t i)
    {
        const Span s = span_of(i);
        return Size{item_w * static_cast<float>(s.columns), item_h * static_cast<float>(s.rows)};
    };
    if (parallel_measure_active() && children_.size() > 2)
    {
        std::vector<MeasureRequest> batch;
        batch.reserve(children_.size() - 1);
        for (std::size_t i = 1; i < children_.size(); ++i)
        {
            batch.push_back({children_[i].get(), tile_offer(i)});
        }
        measure_batch(batch, context);
    }
    else
    {
        for (std::size_t i = 1; i < children_.size(); ++i)
        {
            (void)children_[i]->measure(tile_offer(i), context);
        }
    }

    Size starting{};
    Size justification{};
    compute_alignment(available, starting, justification);
    const auto per_line_f = static_cast<float>(items_per_line_);
    const auto lines_f    = static_cast<float>(line_count_);
    if (horizontal)
    {
        return Size{item_w * per_line_f + justification.width * (per_line_f + 1.0f) + starting.width * 2.0f,
                    item_h * lines_f + justification.height * (lines_f + 1.0f) + starting.height * 2.0f};
    }
    return Size{item_w * lines_f + justification.width * (lines_f + 1.0f) + starting.width * 2.0f,
                item_h * per_line_f + justification.height * (per_line_f + 1.0f) + starting.height * 2.0f};
}

void TilePanelControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    if (children_.empty() || map_.locations.size() != children_.size() || items_per_line_ == 0)
    {
        return;
    }
    const bool horizontal = orientation_ == Orientation::Horizontal;
    Size       starting{};
    Size       justification{};
    compute_alignment(Size{final_rect.width, final_rect.height}, starting, justification);

    Size total_justification{};
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        if (i >= map_.element_count)
        {
            // Past a full map the source stops arranging; an empty rect keeps these from drawing
            // wherever they were last put.
            children_[i]->arrange(Rect{final_rect.x, final_rect.y, 0.0f, 0.0f});
            continue;
        }
        Rect r = map_.item_rect(i, item_size_);
        if (r.y == 0.0f || r.x == 0.0f)
        {
            // A child at the start of a line that spans the whole line or more may be taller (or
            // wider) than the cells the map could give it, up to the panel's own size.
            const Span s = span_of(i);
            if (!horizontal && r.y == 0.0f && static_cast<std::size_t>(s.rows) >= items_per_line_)
            {
                const float height = std::min(static_cast<float>(s.rows) * item_size_.height, final_rect.height);
                r.height           = std::max(r.height, height);
            }
            else if (horizontal && r.x == 0.0f && static_cast<std::size_t>(s.columns) >= items_per_line_)
            {
                const float width = std::min(static_cast<float>(s.columns) * item_size_.width, final_rect.width);
                r.width           = std::max(r.width, width);
            }
        }
        if (i % items_per_line_ == 0)
        {
            const std::size_t line_index = i / items_per_line_;
            const auto        line       = static_cast<float>(line_index + 1);
            if (horizontal)
            {
                total_justification.width  = 0.0f;
                total_justification.height = justification.height * line;
            }
            else
            {
                total_justification.height = 0.0f;
                total_justification.width  = justification.width * line;
            }
        }
        if (horizontal)
        {
            total_justification.width += justification.width;
        }
        else
        {
            total_justification.height += justification.height;
        }
        r.x += final_rect.x + starting.width + total_justification.width;
        r.y += final_rect.y + starting.height + total_justification.height;
        children_[i]->arrange(r);
    }
}

std::unique_ptr<ui::Element> TilePanel::build() &&
{
    auto                  node = std::make_unique<ui::TilePanelControl>();
    ui::TilePanelControl* raw  = node.get();
    apply(orientation, *node, [raw](const ui::Orientation& v) { raw->set_orientation(v); });
    if (item_width.reactive() || item_width.initial())
    {
        apply(item_width, *node, [raw = node.get()](const std::optional<float>& v) { raw->set_item_width(v); });
    }
    if (item_height.reactive() || item_height.initial())
    {
        apply(item_height, *node, [raw = node.get()](const std::optional<float>& v) { raw->set_item_height(v); });
    }
    apply(maximum_rows_or_columns,
          *node,
          [raw = node.get()](const std::size_t& v) { raw->set_maximum_rows_or_columns(v); });
    apply(horizontal_children_alignment,
          *node,
          [raw = node.get()](const Alignment& v) { raw->set_horizontal_children_alignment(v); });
    apply(vertical_children_alignment,
          *node,
          [raw = node.get()](const Alignment& v) { raw->set_vertical_children_alignment(v); });
    apply(background,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });
    for (auto& item : content.items_)
    {
        if (item.node)
        {
            node->add(std::move(item.node), item.row_span, item.column_span);
        }
    }
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
