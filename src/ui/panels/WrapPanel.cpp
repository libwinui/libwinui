// SPDX-License-Identifier: MIT
#include "winui/ui/panels/WrapPanel.hpp"

#include "../LayoutSnapshot.hpp"
#include "winui/core/Color.hpp"
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

#include <cstddef>

namespace ui
{

namespace
{

constexpr float WrapPanelInfinity = std::numeric_limits<float>::infinity();

} // namespace

Size WrapPanelControl::measure_override(Size available, const MeasureContext& context) const
{
    // Children get the room inside the padding and
    // an unbounded axis stays unbounded.
    const Thickness pad = content_inset();
    const Size      child_available{std::max(0.0f, available.width - pad.horizontal()),
                                    std::max(0.0f, available.height - pad.vertical())};
    desired_.clear();
    desired_.reserve(children_.size());
    if (parallel_measure_active() && children_.size() > 1)
    {
        std::vector<MeasureRequest> batch;
        batch.reserve(children_.size());
        for (const auto& child : children_)
        {
            batch.push_back({child.get(), child_available});
        }
        measure_batch(batch, context);
        for (const MeasureRequest& r : batch)
        {
            desired_.push_back(r.desired);
        }
    }
    else
    {
        for (const auto& child : children_)
        {
            desired_.push_back(child->measure(child_available, context));
        }
    }
    panel_desired_ = update_rows(available);
    return panel_desired_;
}

Size WrapPanelControl::update_rows(Size extent) const
{
    lines_.clear();
    const bool      horizontal  = orientation_ == Orientation::Horizontal;
    const Thickness pad         = content_inset();
    const float     pad_start_u = horizontal ? pad.left : pad.top;
    const float     pad_start_v = horizontal ? pad.top : pad.left;
    const float     pad_end_u   = horizontal ? pad.right : pad.bottom;
    const float     pad_end_v   = horizontal ? pad.bottom : pad.right;
    auto            to_size     = [&](float u, float v) { return horizontal ? Size{u, v} : Size{v, u}; };

    if (children_.empty() || desired_.size() != children_.size())
    {
        return to_size(pad_start_u + pad_end_u, pad_start_v + pad_end_v);
    }

    const float parent_u = horizontal ? extent.width : extent.height;
    float       pos_u    = pad_start_u;
    float       pos_v    = pad_start_v;
    float       final_u  = 0.0f;
    Line        current;

    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        const Size  d         = desired_[i];
        float       desired_u = horizontal ? d.width : d.height;
        const float desired_v = horizontal ? d.height : d.width;
        if (desired_u + pos_u + pad_end_u > parent_u && !current.items.empty())
        {
            pos_u = pad_start_u;
            pos_v += current.v_size + line_spacing_;
            lines_.push_back(std::move(current));
            current = Line{};
        }
        const bool last = i + 1 == children_.size();
        if (last && items_stretch_ == WrapPanelItemsStretch::Last && parent_u < WrapPanelInfinity)
        {
            // The last child reaches the far edge of the room, not less the end padding, because
            // the source measures it to parentMeasure.U itself.
            desired_u = parent_u - pos_u;
        }
        current.items.push_back(UvRect{pos_u, pos_v, desired_u, desired_v});
        current.v_size = std::max(current.v_size, desired_v);
        // A collapsed child takes no room, so it takes no gap either.
        pos_u += desired_u + (children_[i]->visibility() != Visibility::Collapsed ? item_spacing_ : 0.0f);
        final_u = std::max(final_u, pos_u);
    }
    if (!current.items.empty())
    {
        lines_.push_back(std::move(current));
    }
    if (lines_.empty())
    {
        return to_size(pad_start_u + pad_end_u, pad_start_v + pad_end_v);
    }
    const Line& last_line = lines_.back();
    const float final_v   = last_line.items.front().v + last_line.v_size;
    return to_size(final_u + pad_end_u, final_v + pad_end_v);
}

void WrapPanelControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    if (desired_.size() != children_.size())
    {
        return;
    }
    const bool horizontal = orientation_ == Orientation::Horizontal;
    // Given less than it asked for along the orientation, the lines
    // are worked out again against what it got.
    if ((horizontal && final_rect.width < panel_desired_.width) ||
        (!horizontal && final_rect.height < panel_desired_.height))
    {
        (void)update_rows(Size{final_rect.width, final_rect.height});
    }

    std::size_t index = 0;
    for (const Line& line : lines_)
    {
        for (const UvRect& item : line.items)
        {
            if (index >= children_.size())
            {
                return;
            }
            const Rect r = horizontal ? Rect{final_rect.x + item.u, final_rect.y + item.v, item.u_size, line.v_size}
                                      : Rect{final_rect.x + item.v, final_rect.y + item.u, line.v_size, item.u_size};
            children_[index]->arrange(r);
            ++index;
        }
    }
}

std::unique_ptr<ui::Element> WrapPanel::build() &&
{
    auto                  node = std::make_unique<ui::WrapPanelControl>();
    ui::WrapPanelControl* raw  = node.get();
    apply(orientation, *node, [raw](const ui::Orientation& v) { raw->set_orientation(v); });
    apply(item_spacing, *node, [raw](const float& v) { raw->set_item_spacing(v); });
    apply(line_spacing, *node, [raw](const float& v) { raw->set_line_spacing(v); });
    apply(items_stretch, *node, [raw](const ui::WrapPanelItemsStretch& v) { raw->set_items_stretch(v); });
    apply(padding, *node, [raw](const ui::Thickness& v) { raw->set_padding(v); });
    apply(border_brush,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_border_brush(c); }); });
    apply(border_thickness, *node, [raw](const ui::Thickness& v) { raw->set_border_thickness(v); });
    apply(corner_radius, *node, [raw](const float& v) { raw->set_corner_radius(v); });
    apply(background,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });
    for (auto& child : content.nodes)
    {
        node->add(std::move(child));
    }
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
