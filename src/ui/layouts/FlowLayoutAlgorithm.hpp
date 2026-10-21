// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/Element.hpp"
#include "winui/ui/layouts/Layout.hpp"

#include <limits>
#include <optional>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui::detail
{

/// The minor axis runs along a line, the major axis across lines — the scroll direction.
struct OrientationMeasures
{
    bool vertical_scroll = true; // the major axis is y

    [[nodiscard]] float major(Size s) const
    {
        return vertical_scroll ? s.height : s.width;
    }
    [[nodiscard]] float minor(Size s) const
    {
        return vertical_scroll ? s.width : s.height;
    }
    [[nodiscard]] float major_start(const Rect& r) const
    {
        return vertical_scroll ? r.y : r.x;
    }
    [[nodiscard]] float minor_start(const Rect& r) const
    {
        return vertical_scroll ? r.x : r.y;
    }
    [[nodiscard]] float major_size(const Rect& r) const
    {
        return vertical_scroll ? r.height : r.width;
    }
    [[nodiscard]] float minor_size(const Rect& r) const
    {
        return vertical_scroll ? r.width : r.height;
    }
    [[nodiscard]] float major_end(const Rect& r) const
    {
        return major_start(r) + major_size(r);
    }
    [[nodiscard]] float minor_end(const Rect& r) const
    {
        return minor_start(r) + minor_size(r);
    }
    void set_major_start(Rect& r, float v) const
    {
        (vertical_scroll ? r.y : r.x) = v;
    }
    void set_minor_start(Rect& r, float v) const
    {
        (vertical_scroll ? r.x : r.y) = v;
    }
    void set_major_size(Rect& r, float v) const
    {
        (vertical_scroll ? r.height : r.width) = v;
    }
    void set_minor_size(Rect& r, float v) const
    {
        (vertical_scroll ? r.width : r.height) = v;
    }
    [[nodiscard]] Size make_size(float minor_value, float major_value) const
    {
        return vertical_scroll ? Size{minor_value, major_value} : Size{major_value, minor_value};
    }
    [[nodiscard]] Rect make_rect(float minor_start_value, float major_start_value, float minor_size_value, float major_size_value) const
    {
        return vertical_scroll ? Rect{minor_start_value, major_start_value, minor_size_value, major_size_value}
                               : Rect{major_start_value, minor_start_value, major_size_value, minor_size_value};
    }
};

/// ItemsJustification (and ItemsJustification, the same values).
enum class LineAlignment
{
    Start,
    Center,
    End,
    SpaceAround,
    SpaceBetween,
    SpaceEvenly,
};

/// An item to lay out first and the major offset it starts at.
struct AnchorInfo
{
    std::optional<std::size_t> index{};
    float                      offset = 0.0f;
};

class FlowLayoutAlgorithm;

/// IFlowLayoutAlgorithmDelegates.
class FlowAlgorithmDelegates
{
public:
    virtual ~FlowAlgorithmDelegates() = default;

    /// The size to measure item `index` in.
    [[nodiscard]] virtual Size measure_size(std::size_t index, Size available) = 0;
    /// The size to lay item `index` out at, given what it asked for.
    [[nodiscard]] virtual Size provisional_arrange_size(std::size_t index, Size measure_size, Size desired) = 0;
    /// Whether item `index` starts a new line, `remaining` being the minor room left after it.
    [[nodiscard]] virtual bool should_break_line(std::size_t index, float remaining) = 0;
    /// The item to start from for the context's realization rect, and its major offset.
    [[nodiscard]] virtual AnchorInfo anchor_for_realization_rect(Size available, VirtualizingLayoutContext& context) = 0;
    /// The item to start from to reach `target`, and its major offset.
    [[nodiscard]] virtual AnchorInfo anchor_for_target(std::size_t                target,
                                                       Size                       available,
                                                       VirtualizingLayoutContext& context) = 0;
    /// Every item's extent, realized or estimated, in layout coordinates.
    [[nodiscard]] virtual Rect extent(
        Size                       available,
        VirtualizingLayoutContext& context,
        const Element*             first_realized,
        std::size_t                first_index,
        const Rect&                first_bounds,
        const Element*             last_realized,
        std::size_t                last_index,
        const Rect&                last_bounds) = 0;
    virtual void on_element_measured(std::size_t index, Size available, Size measure_size, Size desired, Size provisional)
    {
        (void)index;
        (void)available;
        (void)measure_size;
        (void)desired;
        (void)provisional;
    }
    /// A realized line: `count` items from `start`, `line_size` along the major axis.
    virtual void on_line_arranged(std::size_t start, std::size_t count, float line_size, VirtualizingLayoutContext& context)
    {
        (void)start;
        (void)count;
        (void)line_size;
        (void)context;
    }
};

class FlowLayoutAlgorithm
{
public:
    static constexpr std::size_t Unlimited = std::numeric_limits<std::size_t>::max();

    struct MeasureParams
    {
        bool        wrapping           = false;
        float       min_item_spacing   = 0.0f;
        float       line_spacing       = 0.0f;
        std::size_t max_items_per_line = Unlimited;
        bool        vertical_scroll    = true;
        bool        virtualize         = true;
        // The items take another size than at the last measure, so a wrapping layout finds the
        // anchor's column again, as for a new available width
        bool items_resized = false;
    };

    [[nodiscard]] Size measure(Size                       available,
                               VirtualizingLayoutContext& context,
                               const MeasureParams&       params,
                               FlowAlgorithmDelegates&    delegates);
    Size arrange(Size final_size, VirtualizingLayoutContext& context, bool wrapping, LineAlignment alignment);

    /// Keep the realized range in step with the collection.
    void on_items_changed(VirtualizingLayoutContext& context, const ItemsChange& change);
    /// Forget everything (a layout detached from its host, or a property that invalidates it all).
    void reset();

    [[nodiscard]] const Rect& last_extent() const
    {
        return last_extent_;
    }
    [[nodiscard]] const OrientationMeasures& om() const
    {
        return om_;
    }

    // ---- the realized range ----
    [[nodiscard]] std::size_t realized_count() const
    {
        return elements_.size();
    }
    [[nodiscard]] std::size_t first_realized_index() const
    {
        return first_index_;
    }
    [[nodiscard]] bool is_realized(std::size_t index) const
    {
        return index >= first_index_ && index - first_index_ < elements_.size() &&
               elements_[index - first_index_] != nullptr;
    }
    [[nodiscard]] Element* realized_element(std::size_t index) const
    {
        return is_realized(index) ? elements_[index - first_index_] : nullptr;
    }
    [[nodiscard]] Rect realized_bounds(std::size_t index) const
    {
        return is_realized(index) ? bounds_[index - first_index_] : Rect{};
    }

private:
    [[nodiscard]] std::optional<std::size_t> anchor_index(Size available, const MeasureParams& params, FlowAlgorithmDelegates& d);
    void                generate(bool                       forward,
                                 std::optional<std::size_t> anchor,
                                 Size                       available,
                                 const MeasureParams&       params,
                                 FlowAlgorithmDelegates&    d);
    [[nodiscard]] Size  measure_element(std::size_t index, Size available, FlowAlgorithmDelegates& d);
    [[nodiscard]] bool  should_continue_filling(bool forward, std::size_t previous) const;
    [[nodiscard]] bool  is_window_connected() const;
    void                discard_outside_window();
    void                discard_beyond(bool forward, std::size_t index);
    void                clear_realized_range();
    void                ensure_realized(std::size_t index);
    void                raise_line_arranged(FlowAlgorithmDelegates& d);
    [[nodiscard]] Rect& bounds_at(std::size_t index)
    {
        return bounds_[index - first_index_];
    }

    OrientationMeasures        om_{};
    VirtualizingLayoutContext* context_ = nullptr;
    Rect                       window_{};
    bool                       virtualizing_ = true;
    bool                       same_as_flow_ = false; // one line, the minor axis unbounded

    // The realized range: contiguous items starting at first_index_.
    std::vector<Element*> elements_;
    std::vector<Rect>     bounds_;
    std::size_t           first_index_ = 0;

    Rect  last_extent_{};
    float last_available_minor_      = -1.0f;
    float last_item_spacing_         = -1.0f;
    bool  collection_change_pending_ = false;
};

} // namespace ui::detail
