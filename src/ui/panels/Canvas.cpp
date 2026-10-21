// SPDX-License-Identifier: MIT
#include "winui/ui/panels/Canvas.hpp"

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
#include <memory>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

CanvasControl::Placement* CanvasControl::placement_of(const Element* child)
{
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        if (children_[i].get() == child)
        {
            return &placements_[i];
        }
    }
    return nullptr;
}

const CanvasControl::Placement* CanvasControl::placement_of(const Element* child) const
{
    return const_cast<CanvasControl*>(this)->placement_of(child);
}

void CanvasControl::set_left(const Element* child, float left)
{
    if (Placement* p = placement_of(child); p != nullptr && p->left != left)
    {
        p->left = left;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void CanvasControl::set_top(const Element* child, float top)
{
    if (Placement* p = placement_of(child); p != nullptr && p->top != top)
    {
        p->top = top;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void CanvasControl::set_z_index(const Element* child, int z_index)
{
    if (Placement* p = placement_of(child); p != nullptr && p->z_index != z_index)
    {
        p->z_index = z_index;
        mark_dirty(DirtyFlags::Visual);
    }
}

float CanvasControl::left(const Element* child) const
{
    const Placement* p = placement_of(child);
    return p != nullptr ? p->left : 0.0f;
}

float CanvasControl::top(const Element* child) const
{
    const Placement* p = placement_of(child);
    return p != nullptr ? p->top : 0.0f;
}

int CanvasControl::z_index(const Element* child) const
{
    const Placement* p = placement_of(child);
    return p != nullptr ? p->z_index : 0;
}

Size CanvasControl::measure_override(Size, const MeasureContext& context) const
{
    // Every child gets infinite room, and the canvas itself
    // asks for nothing.
    desired_.clear();
    desired_.reserve(children_.size());
    if (parallel_measure_active() && children_.size() > 1)
    {
        std::vector<MeasureRequest> batch;
        batch.reserve(children_.size());
        for (const auto& child : children_)
        {
            batch.push_back({child.get(), Size{Unbounded, Unbounded}});
        }
        measure_batch(batch, context);
        for (const MeasureRequest& r : batch)
        {
            desired_.push_back(r.desired);
        }
        return Size{0.0f, 0.0f};
    }
    for (const auto& child : children_)
    {
        desired_.push_back(child->measure(Size{Unbounded, Unbounded}, context));
    }
    return Size{0.0f, 0.0f};
}

void CanvasControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    if (desired_.size() != children_.size())
    {
        return;
    }
    // Each child at its offset, at its desired size.
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        const Placement& p = placements_[i];
        children_[i]->arrange(Rect{final_rect.x + p.left, final_rect.y + p.top, desired_[i].width, desired_[i].height});
    }
}

std::vector<Element*> CanvasControl::render_order() const
{
    std::vector<std::size_t> index(children_.size());
    std::iota(index.begin(), index.end(), std::size_t{0});
    std::stable_sort(index.begin(),
                     index.end(),
                     [this](std::size_t a, std::size_t b) { return placements_[a].z_index < placements_[b].z_index; });
    std::vector<Element*> order;
    order.reserve(index.size());
    for (std::size_t i : index)
    {
        order.push_back(children_[i].get());
    }
    return order;
}

std::unique_ptr<ui::Element> Canvas::build() &&
{
    auto               node = std::make_unique<ui::CanvasControl>();
    ui::CanvasControl* raw  = node.get();
    apply(background,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });
    for (auto& item : content.items_)
    {
        if (!item.node)
        {
            continue;
        }
        ui::Element* child = node->add(std::move(item.node), item.left.initial(), item.top.initial(), item.z_index.initial());
        if (item.left.reactive())
        {
            apply(item.left, *child, [raw, child](const float& v) { raw->set_left(child, v); });
        }
        if (item.top.reactive())
        {
            apply(item.top, *child, [raw, child](const float& v) { raw->set_top(child, v); });
        }
        if (item.z_index.reactive())
        {
            apply(item.z_index, *child, [raw, child](const int& v) { raw->set_z_index(child, v); });
        }
    }
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
