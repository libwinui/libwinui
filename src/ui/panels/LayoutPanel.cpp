// SPDX-License-Identifier: MIT
#include "winui/ui/panels/LayoutPanel.hpp"

#include "../LayoutSnapshot.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/layouts/Layout.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include <cstddef>

namespace ui
{

// ---- the contexts ----

class PanelLayoutContext final : public NonVirtualizingLayoutContext
{
public:
    PanelLayoutContext(const LayoutPanelControl& panel, const TextMeasurer* measurer, const Theme* theme, Point origin) :
        panel_(panel),
        measurer_(measurer),
        theme_(theme),
        origin_(origin)
    {
    }

    [[nodiscard]] std::size_t item_count() const override
    {
        return panel_.children().size();
    }
    Size measure(std::size_t index, Size available) override
    {
        if (index >= item_count() || measurer_ == nullptr || theme_ == nullptr)
        {
            return Size{};
        }
        const Size desired     = panel_.children()[index]->measure(available, *measurer_, *theme_);
        panel_.desired_[index] = desired;
        return desired;
    }
    [[nodiscard]] Size desired_size(std::size_t index) const override
    {
        return index < panel_.desired_.size() ? panel_.desired_[index] : Size{};
    }
    void arrange(std::size_t index, const Rect& rect) override
    {
        if (index < item_count())
        {
            panel_.children()[index]->arrange(Rect{origin_.x + rect.x, origin_.y + rect.y, rect.width, rect.height});
        }
    }

private:
    const LayoutPanelControl& panel_;
    const TextMeasurer*       measurer_;
    const Theme*              theme_;
    Point                     origin_;
};

// The context a panel hands its layout: every child realized, an unbounded realization rect,
// nothing to recycle.
class PanelVirtualizingContext final : public VirtualizingLayoutContext
{
public:
    PanelVirtualizingContext(const LayoutPanelControl& panel, const TextMeasurer* measurer, const Theme* theme, Point origin) :
        panel_(panel),
        measurer_(measurer),
        theme_(theme),
        origin_(origin)
    {
    }

    [[nodiscard]] std::size_t item_count() const override
    {
        return panel_.children().size();
    }
    Element& get_or_create_element_at(std::size_t index, ElementRealizationOptions) override
    {
        return *panel_.children()[std::min(index, item_count() - 1)];
    }
    void recycle_element(Element&) override
    {
    }
    [[nodiscard]] std::optional<std::size_t> index_of(const Element& element) const override
    {
        const auto& children = panel_.children();
        for (std::size_t i = 0; i < children.size(); ++i)
        {
            if (children[i].get() == &element)
            {
                return i;
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] Rect realization_rect() const override
    {
        return unbounded_rect();
    }
    [[nodiscard]] Rect visible_rect() const override
    {
        return unbounded_rect();
    }
    [[nodiscard]] Point layout_origin() const override
    {
        return Point{};
    }
    void set_layout_origin(Point) override
    {
        // A panel's layout starts at its content box (the origin stays 0,0).
    }
    Size measure(Element& element, Size available) override
    {
        const std::optional<std::size_t> index = index_of(element);
        if (!index || measurer_ == nullptr || theme_ == nullptr)
        {
            return Size{};
        }
        const Size desired      = element.measure(available, *measurer_, *theme_);
        panel_.desired_[*index] = desired;
        return desired;
    }
    [[nodiscard]] Size desired_size(const Element& element) const override
    {
        const std::optional<std::size_t> index = index_of(element);
        return index && *index < panel_.desired_.size() ? panel_.desired_[*index] : Size{};
    }
    void arrange(Element& element, const Rect& rect) override
    {
        element.arrange(Rect{origin_.x + rect.x, origin_.y + rect.y, rect.width, rect.height});
    }

private:
    const LayoutPanelControl& panel_;
    const TextMeasurer*       measurer_;
    const Theme*              theme_;
    Point                     origin_;
};

// ---- LayoutPanel ----

LayoutPanelControl::~LayoutPanelControl()
{
    if (layout_)
    {
        layout_->set_owner(nullptr);
    }
}

void LayoutPanelControl::replace_layout(std::unique_ptr<Layout> layout)
{
    if (layout_)
    {
        layout_->set_owner(nullptr);
        if (auto* virtualizing = dynamic_cast<VirtualizingLayout*>(layout_.get()))
        {
            PanelVirtualizingContext context(*this, measurer_, theme_, Point{});
            virtualizing->uninitialize_for_context(context);
        }
    }
    layout_ = std::move(layout);
    if (layout_)
    {
        layout_->set_owner(this);
        if (auto* virtualizing = dynamic_cast<VirtualizingLayout*>(layout_.get()))
        {
            PanelVirtualizingContext context(*this, measurer_, theme_, Point{});
            virtualizing->initialize_for_context(context);
        }
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

Size LayoutPanelControl::measure_override(Size available, const MeasureContext& context) const
{
    // Padding and border off the room, back onto the answer.
    measurer_ = &context.measurer;
    theme_    = &context.theme;
    desired_.resize(children_.size());
    const Thickness inset = content_inset();
    const Size      room{std::max(0.0f, available.width - inset.horizontal()),
                         std::max(0.0f, available.height - inset.vertical())};
    Size            desired{};
    if (auto* nonvirtual = dynamic_cast<NonVirtualizingLayout*>(layout_.get()))
    {
        PanelLayoutContext layout(*this, &context.measurer, &context.theme, Point{});
        desired = nonvirtual->measure(layout, room);
    }
    else if (auto* virtualizing = dynamic_cast<VirtualizingLayout*>(layout_.get()))
    {
        PanelVirtualizingContext layout(*this, &context.measurer, &context.theme, Point{});
        desired = children_.empty() ? Size{} : virtualizing->measure(layout, room);
    }
    else if (parallel_measure_active() && children_.size() > 1)
    {
        std::vector<MeasureRequest> batch;
        batch.reserve(children_.size());
        for (const auto& child : children_)
        {
            batch.push_back({child.get(), room});
        }
        measure_batch(batch, context);
        for (std::size_t i = 0; i < batch.size(); ++i)
        {
            desired_[i]    = batch[i].desired;
            desired.width  = std::max(desired.width, batch[i].desired.width);
            desired.height = std::max(desired.height, batch[i].desired.height);
        }
    }
    else
    {
        PanelLayoutContext layout(*this, &context.measurer, &context.theme, Point{});
        for (std::size_t i = 0; i < children_.size(); ++i)
        {
            const Size d   = layout.measure(i, room);
            desired.width  = std::max(desired.width, d.width);
            desired.height = std::max(desired.height, d.height);
        }
    }
    return Size{desired.width + inset.horizontal(), desired.height + inset.vertical()};
}

bool LayoutPanelControl::parallel_measure_safe() const
{
    return !layout_ || layout_->parallel_measure_safe();
}

void LayoutPanelControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    const Thickness inset = content_inset();
    const Size      room{std::max(0.0f, final_rect.width - inset.horizontal()),
                         std::max(0.0f, final_rect.height - inset.vertical())};
    const Point     origin{final_rect.x + inset.left, final_rect.y + inset.top};
    if (auto* nonvirtual = dynamic_cast<NonVirtualizingLayout*>(layout_.get()))
    {
        PanelLayoutContext context(*this, measurer_, theme_, origin);
        (void)nonvirtual->arrange(context, room);
        return;
    }
    if (auto* virtualizing = dynamic_cast<VirtualizingLayout*>(layout_.get()))
    {
        if (!children_.empty())
        {
            PanelVirtualizingContext context(*this, measurer_, theme_, origin);
            (void)virtualizing->arrange(context, room);
        }
        return;
    }
    PanelLayoutContext context(*this, measurer_, theme_, origin);
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        context.arrange(i, Rect{0.0f, 0.0f, room.width, room.height});
    }
}

std::unique_ptr<ui::Element> LayoutPanel::build() &&
{
    auto                    node = std::make_unique<ui::LayoutPanelControl>();
    ui::LayoutPanelControl* raw  = node.get();
    if (layout)
    {
        node->set_layout(layout.take());
    }
    apply(background,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });
    apply(border_brush,
          *node,
          [raw = node.get()](const ui::Paint& v)
          { ui::detail::set_paint(v, [raw](auto c) { raw->set_border_brush(c); }); });
    apply(border_thickness, *node, [raw = node.get()](const Thickness& v) { raw->set_border_thickness(v); });
    apply(corner_radius, *node, [raw = node.get()](const float& v) { raw->set_corner_radius(v); });
    apply(padding, *node, [raw](const ui::Thickness& v) { raw->set_padding(v); });
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
