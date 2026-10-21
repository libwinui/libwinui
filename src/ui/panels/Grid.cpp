// SPDX-License-Identifier: MIT
#include "winui/ui/panels/Grid.hpp"

#include "../LayoutSnapshot.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

void GridControl::set_row_height(std::size_t index, float dips)
{
    if (index >= rows_.size())
    {
        return;
    }
    rows_[index].unit  = GridUnit::Dip;
    rows_[index].value = dips;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void GridControl::set_column_width(std::size_t index, float dips)
{
    if (index >= columns_.size())
    {
        return;
    }
    columns_[index].unit  = GridUnit::Dip;
    columns_[index].value = dips;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

namespace
{
// `size` kept within the track's min and max.
[[nodiscard]] float limited(const GridLength& track, float size)
{
    return std::max(track.min, std::min(size, track.max));
}
} // namespace

std::optional<float> GridControl::resize_beside(const Element* splitter, Orientation orientation, float delta)
{
    const auto slot = std::ranges::find_if(children_,
                                           [splitter](const ChildSlot& s) { return s.element.get() == splitter; });
    if (slot == children_.end())
    {
        return std::nullopt;
    }
    // A vertical bar sits in a column and resizes the columns either side of it.
    const bool               columns = orientation == Orientation::Vertical;
    std::vector<GridLength>& tracks  = columns ? columns_ : rows_;
    std::vector<float>&      sizes   = columns ? column_sizes_ : row_sizes_;
    const std::size_t        index   = columns ? slot->column : slot->row;
    const std::size_t        span    = columns ? slot->column_span : slot->row_span;
    if (span != 1 || index == 0 || index + 1 >= tracks.size() || sizes.size() != tracks.size())
    {
        return std::nullopt;
    }
    GridLength& before = tracks[index - 1];
    GridLength& after  = tracks[index + 1];
    const float a      = sizes[index - 1];
    const float b      = sizes[index + 1];

    // As far as both tracks' limits allow, so the two keep their total.
    const float lowest  = std::max(limited(before, 0.0f) - a, b - limited(after, Unbounded));
    const float highest = std::min(limited(before, Unbounded) - a, b - limited(after, 0.0f));
    delta               = std::clamp(delta, std::min(lowest, 0.0f), std::max(highest, 0.0f));
    if (delta == 0.0f)
    {
        return 0.0f;
    }
    const float new_a = a + delta;
    const float new_b = b - delta;
    if (before.unit == GridUnit::Fraction && after.unit == GridUnit::Fraction)
    {
        // Two fractions trade weight, so the other fraction tracks keep theirs.
        const float weight = before.value + after.value;
        before.value       = new_a + new_b > 0.0f ? weight * new_a / (new_a + new_b) : before.value;
        after.value        = weight - before.value;
    }
    else
    {
        // A fixed or auto track becomes a fixed one at its new size; a fraction next to it takes
        // what is left.
        for (auto [track, size] : {std::pair{&before, new_a}, std::pair{&after, new_b}})
        {
            if (track->unit != GridUnit::Fraction)
            {
                track->unit  = GridUnit::Dip;
                track->value = size;
            }
        }
    }
    // Until the next layout, a second step starts from where this one left the tracks.
    sizes[index - 1] = new_a;
    sizes[index + 1] = new_b;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return delta;
}

std::vector<float> GridControl::resolve_tracks(const std::vector<GridLength>& lengths,
                                               float                          available,
                                               float                          spacing,
                                               const std::vector<float>&      auto_desired)
{
    std::vector<float> sizes(lengths.size(), 0.0f);
    std::vector<bool>  sharing(lengths.size(), false);
    float              fixed_auto_total = 0.0f;

    // With nothing to take a share of, a Fraction track behaves like Auto on an infinite axis.
    const bool star_as_auto = !std::isfinite(available);

    for (std::size_t i = 0; i < lengths.size(); ++i)
    {
        const GridUnit unit = (lengths[i].unit == GridUnit::Fraction && star_as_auto) ? GridUnit::Auto : lengths[i].unit;
        switch (unit)
        {
            case GridUnit::Dip:
                sizes[i] = limited(lengths[i], std::max(0.0f, lengths[i].value));
                fixed_auto_total += sizes[i];
                break;
            case GridUnit::Auto:
                sizes[i] = limited(lengths[i], auto_desired[i]);
                fixed_auto_total += sizes[i];
                break;
            case GridUnit::Fraction:
                sharing[i] = true;
                break;
        }
    }

    const float total_spacing = lengths.empty() ? 0.0f : spacing * static_cast<float>(lengths.size() - 1);
    float       remaining     = star_as_auto ? 0.0f : std::max(0.0f, available - fixed_auto_total - total_spacing);

    // Split by weight; a track whose share falls outside its limits is held at the limit and
    // leaves the rest to the others, until every share fits.
    for (bool settled = false; !settled;)
    {
        settled            = true;
        float weight_total = 0.0f;
        for (std::size_t i = 0; i < lengths.size(); ++i)
        {
            weight_total += sharing[i] ? std::max(0.0f, lengths[i].value) : 0.0f;
        }
        for (std::size_t i = 0; i < lengths.size(); ++i)
        {
            if (!sharing[i])
            {
                continue;
            }
            const float share = weight_total > 0.0f ? remaining * (std::max(0.0f, lengths[i].value) / weight_total) : 0.0f;
            sizes[i] = share;
            if (const float held = limited(lengths[i], share); held != share)
            {
                sizes[i]   = held;
                sharing[i] = false;
                remaining  = std::max(0.0f, remaining - held);
                settled    = false;
                break;
            }
        }
    }

    return sizes;
}

namespace
{
// How much room the tracks `[start, start+span)` offer a child, or Unbounded when any of them
// is not sized yet. Auto tracks never are; Star tracks not until every Pixels/Auto total is known.
[[nodiscard]] float span_extent(const std::vector<float>& sizes,
                                const std::vector<bool>&  resolved,
                                std::size_t               start,
                                std::size_t               span,
                                float                     spacing)
{
    float total = 0.0f;
    for (std::size_t i = start; i < start + span; ++i)
    {
        if (i >= sizes.size() || !resolved[i])
        {
            return Unbounded;
        }
        total += sizes[i];
    }
    return total + spacing * static_cast<float>(span - 1);
}

// No definitions on an axis means one implicit Star track.
[[nodiscard]] const std::vector<GridLength>& effective(const std::vector<GridLength>& tracks)
{
    static const std::vector<GridLength> Implicit{GridLength::fraction()};
    // Only ever handed rows_ or columns_, never a temporary.
    return tracks.empty() ? Implicit : tracks; // NOLINT(bugprone-return-const-ref-from-parameter)
}

[[nodiscard]] bool spans_star(const std::vector<GridLength>& lengths, std::size_t start, std::size_t span)
{
    for (std::size_t t = start; t < std::min(start + span, lengths.size()); ++t)
    {
        if (lengths[t].unit == GridUnit::Fraction)
        {
            return true;
        }
    }
    return false;
}

// Where a child goes once its placement is clamped to the tracks there are: an index past the
// last track lands in the last one, and a span stops at the last track.
struct ClampedGridCell
{
    std::size_t row         = 0;
    std::size_t column      = 0;
    std::size_t row_span    = 1;
    std::size_t column_span = 1;
};

template <typename Slots>
[[nodiscard]] std::vector<ClampedGridCell> clamped_cells(const Slots& slots, std::size_t n_rows, std::size_t n_cols)
{
    std::vector<ClampedGridCell> cells;
    cells.reserve(slots.size());
    for (const auto& slot : slots)
    {
        ClampedGridCell cell;
        cell.row         = std::min(slot.row, n_rows - 1);
        cell.column      = std::min(slot.column, n_cols - 1);
        cell.row_span    = std::min(slot.row_span, n_rows - cell.row);
        cell.column_span = std::min(slot.column_span, n_cols - cell.column);
        cells.push_back(cell);
    }
    return cells;
}
} // namespace

Size GridControl::measure_override(Size outer, const MeasureContext& context) const
{
    const Thickness inset = content_inset();
    const Size available{std::max(0.0f, outer.width - inset.horizontal()), std::max(0.0f, outer.height - inset.vertical())};

    const std::vector<GridLength>&     rows    = effective(rows_);
    const std::vector<GridLength>&     columns = effective(columns_);
    const std::size_t                  n_rows  = rows.size();
    const std::size_t                  n_cols  = columns.size();
    const std::vector<ClampedGridCell> cells   = clamped_cells(children_, n_rows, n_cols);

    // Star-as-Auto on an unbounded axis, as resolve_tracks() decides below, so such a track
    // collects the desired sizes that will size it.
    const bool star_rows_are_auto = !std::isfinite(available.height);
    const bool star_cols_are_auto = !std::isfinite(available.width);
    const auto sizes_to_content   = [](GridUnit unit, bool star_as_auto)
    { return unit == GridUnit::Auto || (unit == GridUnit::Fraction && star_as_auto); };

    // Pass 1: Pixels tracks are known up front; Auto and Star tracks are unbounded, so a child
    // reports its natural size, which is what sizes an Auto track.
    std::vector<float> row_pass1(n_rows, 0.0f);
    std::vector<bool>  row_known(n_rows, false);
    for (std::size_t i = 0; i < n_rows; ++i)
    {
        row_known[i] = rows[i].unit == GridUnit::Dip;
        row_pass1[i] = row_known[i] ? limited(rows[i], std::max(0.0f, rows[i].value)) : 0.0f;
    }
    std::vector<float> col_pass1(n_cols, 0.0f);
    std::vector<bool>  col_known(n_cols, false);
    for (std::size_t i = 0; i < n_cols; ++i)
    {
        col_known[i] = columns[i].unit == GridUnit::Dip;
        col_pass1[i] = col_known[i] ? limited(columns[i], std::max(0.0f, columns[i].value)) : 0.0f;
    }

    const auto constraint_for =
        [&](const ClampedGridCell&    slot,
            const std::vector<float>& rs,
            const std::vector<bool>&  rk,
            const std::vector<float>& cs,
            const std::vector<bool>&  ck) -> Size
    {
        return Size{span_extent(cs, ck, slot.column, slot.column_span, column_spacing_),
                    span_extent(rs, rk, slot.row, slot.row_span, row_spacing_)};
    };

    // Within each pass below every offer depends only on track sizes fixed before it, so the cells
    // of one pass are independent of each other and may be measured as one batch.
    const bool                  parallel = parallel_measure_active() && children_.size() > 1;
    std::vector<MeasureRequest> batch;
    std::vector<std::size_t>    batch_index;
    const auto                  run_batch = [&]
    {
        measure_batch(batch, context);
        for (std::size_t k = 0; k < batch.size(); ++k)
        {
            child_desired_[batch_index[k]] = batch[k].desired;
        }
        batch.clear();
        batch_index.clear();
    };

    // A child that sizes no Auto (or Star-as-Auto) track and sits in a Star track is measured in
    // pass 2 at its resolved size, which is the only measure that counts for it. Measuring it
    // unbounded first would only cost: a virtualized list offered infinite height measures every
    // row, and each Grid nested in a Star track would double the work below it.
    const auto sizes_a_row = [&](const ClampedGridCell& slot)
    { return slot.row_span == 1 && sizes_to_content(rows[slot.row].unit, star_rows_are_auto); };
    const auto sizes_a_column = [&](const ClampedGridCell& slot)
    { return slot.column_span == 1 && sizes_to_content(columns[slot.column].unit, star_cols_are_auto); };
    const auto measured_in_pass2 = [&](const ClampedGridCell& slot)
    {
        return (!star_rows_are_auto && spans_star(rows, slot.row, slot.row_span)) ||
               (!star_cols_are_auto && spans_star(columns, slot.column, slot.column_span));
    };

    child_desired_.assign(children_.size(), Size{});
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        if (measured_in_pass2(cells[i]) && !sizes_a_row(cells[i]) && !sizes_a_column(cells[i]))
        {
            continue;
        }
        const Size constraint = constraint_for(cells[i], row_pass1, row_known, col_pass1, col_known);
        if (parallel)
        {
            batch.push_back({children_[i].element.get(), constraint});
            batch_index.push_back(i);
        }
        else
        {
            child_desired_[i] = children_[i].element->measure(constraint, context);
        }
    }
    if (parallel)
    {
        run_batch();
    }

    std::vector<float> auto_row_desired(n_rows, 0.0f);
    std::vector<float> auto_col_desired(n_cols, 0.0f);

    // A child in a Star column is measured again below at the column's real width, and it is
    // that height, not this pass' unbounded-width one, that may size its Auto row: a wrapping
    // TextControl is one line long at unbounded width and several lines tall at its column's.
    const auto in_star_column = [&](const ClampedGridCell& slot)
    { return !star_cols_are_auto && spans_star(columns, slot.column, slot.column_span); };
    const auto collect_auto_row = [&](const ClampedGridCell& slot, const Size& desired)
    {
        // Auto sizing only accounts for children with a span of 1 on that axis.
        if (slot.row_span == 1 && sizes_to_content(rows[slot.row].unit, star_rows_are_auto))
        {
            auto_row_desired[slot.row] = std::max(auto_row_desired[slot.row], desired.height);
        }
    };

    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        const ClampedGridCell& slot    = cells[i];
        const Size&            desired = child_desired_[i];
        if (!in_star_column(slot))
        {
            collect_auto_row(slot, desired);
        }
        if (slot.column_span == 1 && sizes_to_content(columns[slot.column].unit, star_cols_are_auto))
        {
            auto_col_desired[slot.column] = std::max(auto_col_desired[slot.column], desired.width);
        }
    }

    // Columns before rows, in cell groups: once the Star
    // columns have their widths, a Star-column child in an Auto row is measured at that width
    // and only then does the row take its height.
    column_sizes_ = resolve_tracks(columns, available.width, column_spacing_, auto_col_desired);
    const std::vector<bool> all_cols_resolved(n_cols, true);
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        const ClampedGridCell& slot = cells[i];
        // This measure only sizes Auto rows; pass 2 measures the child again at its resolved size.
        if (!in_star_column(slot) || !sizes_a_row(slot))
        {
            continue;
        }
        const Size constraint = constraint_for(slot, row_pass1, row_known, column_sizes_, all_cols_resolved);
        if (parallel)
        {
            batch.push_back({children_[i].element.get(), constraint});
            batch_index.push_back(i);
            continue;
        }
        child_desired_[i] = children_[i].element->measure(constraint, context);
        collect_auto_row(slot, child_desired_[i]);
    }
    if (parallel)
    {
        // collect_auto_row() keeps a maximum, so collecting after the batch gives the same rows.
        const std::vector<std::size_t> measured = batch_index;
        run_batch();
        for (const std::size_t i : measured)
        {
            collect_auto_row(cells[i], child_desired_[i]);
        }
    }

    row_sizes_           = resolve_tracks(rows, available.height, row_spacing_, auto_row_desired);
    auto_row_desired_    = std::move(auto_row_desired);
    auto_column_desired_ = std::move(auto_col_desired);

    // Pass 2: re-measure the Star-track occupants at their resolved size. Auto-track occupants
    // sized their track, so feeding it back could only shrink them.
    const std::vector<bool> all_rows_resolved(n_rows, true);
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        const ClampedGridCell& slot     = cells[i];
        const bool             star_row = !star_rows_are_auto && spans_star(rows, slot.row, slot.row_span);
        const bool             star_col = !star_cols_are_auto && spans_star(columns, slot.column, slot.column_span);
        if (!star_row && !star_col)
        {
            continue; // an Auto (or Star-as-Auto) track is sized by this child
        }
        const Size constraint = constraint_for(slot, row_sizes_, all_rows_resolved, column_sizes_, all_cols_resolved);
        if (parallel)
        {
            batch.push_back({children_[i].element.get(), constraint});
            batch_index.push_back(i);
            continue;
        }
        child_desired_[i] = children_[i].element->measure(constraint, context);
    }
    if (parallel)
    {
        run_batch();
    }

    const auto axis_total = [](const std::vector<float>& sizes, float spacing)
    {
        return std::accumulate(sizes.begin(), sizes.end(), 0.0f) +
               (sizes.empty() ? 0.0f : spacing * static_cast<float>(sizes.size() - 1));
    };

    // A Star track asks only for what the children in it want, up to the share it was given, so a
    // Grid in a slot that does not stretch it (Leading alignment, a tile) takes its content's size
    // rather than all it is offered. Arrange still shares out the room it gets.
    const auto desired_extent =
        [&](const std::vector<GridLength>& tracks, const std::vector<float>& sizes, bool star_as_auto, float spacing, bool along_columns)
    {
        std::vector<float> want = sizes;
        if (star_as_auto)
        {
            return axis_total(want, spacing);
        }
        for (std::size_t t = 0; t < tracks.size(); ++t)
        {
            if (tracks[t].unit == GridUnit::Fraction)
            {
                want[t] = std::min(sizes[t], limited(tracks[t], 0.0f));
            }
        }
        // The children spanning one track first, then the ones spanning several, which add what
        // they still miss to the Star tracks they cross.
        for (const bool spanning : {false, true})
        {
            for (std::size_t i = 0; i < children_.size(); ++i)
            {
                const std::size_t start   = along_columns ? cells[i].column : cells[i].row;
                const std::size_t span    = along_columns ? cells[i].column_span : cells[i].row_span;
                const float       desired = along_columns ? child_desired_[i].width : child_desired_[i].height;
                if ((span > 1) != spanning || !spans_star(tracks, start, span))
                {
                    continue;
                }
                if (!spanning)
                {
                    want[start] = std::max(want[start], std::min(desired, sizes[start]));
                    continue;
                }
                const std::size_t end   = std::min(start + span, tracks.size());
                float             have  = spacing * static_cast<float>(end - start - 1);
                float             room  = have;
                std::size_t       stars = 0;
                for (std::size_t t = start; t < end; ++t)
                {
                    have += want[t];
                    room += sizes[t];
                    stars += tracks[t].unit == GridUnit::Fraction ? 1 : 0;
                }
                const float missing = std::min(desired, room) - have;
                for (std::size_t t = start; missing > 0.0f && t < end; ++t)
                {
                    if (tracks[t].unit == GridUnit::Fraction)
                    {
                        want[t] = std::min(sizes[t], want[t] + missing / static_cast<float>(stars));
                    }
                }
            }
        }
        return axis_total(want, spacing);
    };

    return Size{desired_extent(columns, column_sizes_, star_cols_are_auto, column_spacing_, true) + inset.horizontal(),
                desired_extent(rows, row_sizes_, star_rows_are_auto, row_spacing_, false) + inset.vertical()};
}

void GridControl::arrange_override(const Rect& outer_rect, const ArrangeContext&)
{
    const Rect                     final_rect = outer_rect.inset(content_inset());
    const std::vector<GridLength>& rows       = effective(rows_);
    const std::vector<GridLength>& columns    = effective(columns_);
    if (auto_row_desired_.size() != rows.size() || auto_column_desired_.size() != columns.size() ||
        child_desired_.size() != children_.size())
    {
        return; // no matching measure_override(): do not read a stale cache
    }

    // Arrange resolves both axes again: a Star track is a share of the
    // final extent, which need not be the extent measure was offered.
    row_sizes_    = resolve_tracks(rows, final_rect.height, row_spacing_, auto_row_desired_);
    column_sizes_ = resolve_tracks(columns, final_rect.width, column_spacing_, auto_column_desired_);

    std::vector<float> row_offset(rows.size() + 1, 0.0f);
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        row_offset[i + 1] = row_offset[i] + row_sizes_[i] + row_spacing_;
    }
    std::vector<float> column_offset(columns.size() + 1, 0.0f);
    for (std::size_t i = 0; i < columns.size(); ++i)
    {
        column_offset[i + 1] = column_offset[i] + column_sizes_[i] + column_spacing_;
    }

    const std::vector<ClampedGridCell> cells = clamped_cells(children_, rows.size(), columns.size());
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        const ClampedGridCell& slot        = cells[i];
        const std::size_t      row_span    = slot.row_span;
        const std::size_t      column_span = slot.column_span;

        float width = 0.0f;
        for (std::size_t c = slot.column; c < slot.column + column_span; ++c)
        {
            width += column_sizes_[c];
        }
        width += column_spacing_ * static_cast<float>(column_span - 1);

        float height = 0.0f;
        for (std::size_t r = slot.row; r < slot.row + row_span; ++r)
        {
            height += row_sizes_[r];
        }
        height += row_spacing_ * static_cast<float>(row_span - 1);

        const Rect child_rect{std::round(final_rect.left() + column_offset[slot.column]),
                              std::round(final_rect.top() + row_offset[slot.row]),
                              std::round(width),
                              std::round(height)};
        children_[i].element->arrange(child_rect);
    }
}

std::unique_ptr<Element> GridControl::orphan(const Element* child)
{
    const auto it = std::ranges::find_if(children_,
                                         [child](const ChildSlot& slot) { return slot.element.get() == child; });
    if (it == children_.end())
    {
        return nullptr;
    }
    std::unique_ptr<Element> owned = std::move(it->element);
    children_.erase(it);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    return detail::orphan(std::move(owned));
}

void GridControl::set_cell(const Element* child, std::size_t row, std::size_t column, std::size_t row_span, std::size_t column_span)
{
    for (ChildSlot& slot : children_)
    {
        if (slot.element.get() == child)
        {
            row_span    = std::max<std::size_t>(1, row_span);
            column_span = std::max<std::size_t>(1, column_span);
            if (slot.row != row || slot.column != column || slot.row_span != row_span || slot.column_span != column_span)
            {
                slot.row         = row;
                slot.column      = column;
                slot.row_span    = row_span;
                slot.column_span = column_span;
                mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
            }
            return;
        }
    }
}

void GridControl::set_row(const Element* child, std::size_t row)
{
    for (const ChildSlot& slot : children_)
    {
        if (slot.element.get() == child)
        {
            set_cell(child, row, slot.column, slot.row_span, slot.column_span);
            return;
        }
    }
}

void GridControl::set_column(const Element* child, std::size_t column)
{
    for (const ChildSlot& slot : children_)
    {
        if (slot.element.get() == child)
        {
            set_cell(child, slot.row, column, slot.row_span, slot.column_span);
            return;
        }
    }
}

void GridControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    draw_border(renderer, bounds(), background_.resolve(theme), border_brush_.resolve(theme), border_thickness_, corner_radius_);
    for (const auto& slot : children_)
    {
        slot.element->render(renderer, context);
    }
}

void GridControl::for_each_child(ChildVisitor visit) const
{
    for (const auto& slot : children_)
    {
        visit(*slot.element);
    }
}

void GridControl::tick_override(Duration delta)
{
    if (background_.advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
}

std::unique_ptr<ui::Element> Grid::build() &&
{
    auto node = std::make_unique<ui::GridControl>();
    for (const ui::GridLength& column : columns)
    {
        node->add_column(column);
    }
    for (const ui::GridLength& row : rows)
    {
        node->add_row(row);
    }
    ui::GridControl* raw = node.get();
    apply(row_spacing, *node, [raw](const float& v) { raw->set_row_spacing(v); });
    apply(column_spacing, *node, [raw](const float& v) { raw->set_column_spacing(v); });
    apply(background,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });
    apply(border_brush,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_border_brush(c); }); });
    apply(border_thickness, *node, [raw](const ui::Thickness& v) { raw->set_border_thickness(v); });
    apply(corner_radius, *node, [raw](const float& v) { raw->set_corner_radius(v); });
    apply(padding, *node, [raw](const ui::Thickness& v) { raw->set_padding(v); });

    for (auto& cell : content.cells)
    {
        if (cell.node)
        {
            ui::Element* child = node->add(std::move(cell.node),
                                           cell.row.initial(),
                                           cell.column.initial(),
                                           cell.row_span,
                                           cell.column_span);
            // A bound cell moves with its state; the subscription lives on the child it moves.
            if (cell.row.reactive())
            {
                apply(cell.row, *child, [raw, child](const std::size_t& v) { raw->set_row(child, v); });
            }
            if (cell.column.reactive())
            {
                apply(cell.column, *child, [raw, child](const std::size_t& v) { raw->set_column(child, v); });
            }
        }
    }

    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
