// SPDX-License-Identifier: MIT
#include "winui/ui/panels/Stack.hpp"

#include "../ExitGhosts.hpp"
#include "../LayoutSnapshot.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panels/Fill.hpp"
#include "winui/ui/panels/Spacer.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{

// A visible fill or spacer, or null for any other child and for a collapsed one.
[[nodiscard]] const FillControl* as_fill(const Element& child)
{
    return child.visibility() == Visibility::Visible ? dynamic_cast<const FillControl*>(&child) : nullptr;
}
[[nodiscard]] const SpacerControl* as_spacer(const Element& child)
{
    return child.visibility() == Visibility::Visible ? dynamic_cast<const SpacerControl*>(&child) : nullptr;
}

} // namespace

Size StackControl::measure_override(Size outer, const MeasureContext& context) const
{
    measured_.assign(children_.size(), Size{});
    const Thickness inset = content_inset();
    const Size available{std::max(0.0f, outer.width - inset.horizontal()), std::max(0.0f, outer.height - inset.vertical())};

    const bool vert = vertical();

    // A stack constrains only the cross axis and gives unbounded room
    // along the stack. Passing `available` on both axes made n fill-the-space children measure n x.
    const Size child_constraint = vert ? Size{available.width, Unbounded} : Size{Unbounded, available.height};

    // The fills are measured last, against the room the others leave. In a stack with no bound
    // along its axis there is no such room, and they are measured like every other child.
    const float main_available = vert ? available.height : available.width;
    bool        any_fill       = false;
    for (const auto& child : children_)
    {
        any_fill = any_fill || as_fill(*child) != nullptr;
    }
    const bool share_room = any_fill && main_available != Unbounded;
    const auto deferred   = [&](std::size_t i) { return share_room && as_fill(*children_[i]) != nullptr; };

    if (parallel_measure_active() && children_.size() > 1)
    {
        std::vector<MeasureRequest> batch;
        std::vector<std::size_t>    index;
        batch.reserve(children_.size());
        index.reserve(children_.size());
        for (std::size_t i = 0; i < children_.size(); ++i)
        {
            if (!deferred(i))
            {
                batch.push_back({children_[i].get(), child_constraint});
                index.push_back(i);
            }
        }
        measure_batch(batch, context);
        for (std::size_t k = 0; k < batch.size(); ++k)
        {
            measured_[index[k]] = batch[k].desired;
        }
    }
    else
    {
        for (std::size_t i = 0; i < children_.size(); ++i)
        {
            if (!deferred(i))
            {
                measured_[i] = children_[i]->measure(child_constraint, context);
            }
        }
    }

    if (share_room)
    {
        float used   = spacing_total();
        float weight = 0.0f;
        for (std::size_t i = 0; i < children_.size(); ++i)
        {
            if (const FillControl* fill = as_fill(*children_[i]))
            {
                weight += fill->weight();
                continue;
            }
            used += vert ? measured_[i].height : measured_[i].width;
            if (const SpacerControl* spacer = as_spacer(*children_[i]))
            {
                weight += spacer->weight();
            }
        }
        const float room = (std::max)(0.0f, main_available - used);
        for (std::size_t i = 0; i < children_.size(); ++i)
        {
            const FillControl* fill = as_fill(*children_[i]);
            if (fill == nullptr)
            {
                continue;
            }
            const float share   = weight > 0.0f ? room * fill->weight() / weight : 0.0f;
            const Size  bound   = vert ? Size{available.width, share} : Size{share, available.height};
            const Size  desired = children_[i]->measure(bound, context);
            // The whole share, so the arrange hands it all over; more only where the content does
            // not go that small (a minimum size).
            measured_[i] = vert ? Size{desired.width, (std::max)(share, desired.height)}
                                : Size{(std::max)(share, desired.width), desired.height};
        }
    }

    float main  = 0.0f; // accumulated size along the stack direction
    float cross = 0.0f; // max size across it
    for (const Size& cs : measured_)
    {
        main += vert ? cs.height : cs.width;
        cross = std::max(cross, vert ? cs.width : cs.height);
    }
    main += spacing_total();

    return vert ? Size{cross + inset.horizontal(), main + inset.vertical()}
                : Size{main + inset.horizontal(), cross + inset.vertical()};
}

float StackControl::spacing_total() const
{
    // One gap between each two children that take room: a collapsed child leaves none behind.
    const auto shown = std::count_if(children_.begin(),
                                     children_.end(),
                                     [](const std::unique_ptr<Element>& child)
                                     { return child->visibility() != Visibility::Collapsed; });
    return shown > 1 ? spacing_ * static_cast<float>(shown - 1) : 0.0f;
}

void StackControl::arrange_override(const Rect& outer_rect, const ArrangeContext&)
{
    const Rect final_rect = outer_rect.inset(content_inset());

    // Arrange without a matching measure should not happen through Window; skip a stale cache.
    if (measured_.size() != children_.size())
    {
        return;
    }

    const bool  vert         = vertical();
    const float cross_origin = vert ? final_rect.left() : final_rect.top();
    const float cross_extent = vert ? final_rect.width : final_rect.height;
    float       pos          = vert ? final_rect.top() : final_rect.left();

    // The room the children leave along the stack goes to its spacers by weight, or to its fills
    // when it has no spacer. The fills' share of the room measured against is in measured_ already.
    float used          = spacing_total();
    float spacer_weight = 0.0f;
    float fill_weight   = 0.0f;
    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        used += vert ? measured_[i].height : measured_[i].width;
        if (const SpacerControl* spacer = as_spacer(*children_[i]))
        {
            spacer_weight += spacer->weight();
        }
        else if (const FillControl* fill = as_fill(*children_[i]))
        {
            fill_weight += fill->weight();
        }
    }
    const float leftover    = std::max(0.0f, (vert ? final_rect.height : final_rect.width) - used);
    const bool  to_spacers  = spacer_weight > 0.0f;
    const float flex_weight = to_spacers ? spacer_weight : fill_weight;

    for (std::size_t i = 0; i < children_.size(); ++i)
    {
        float main_size  = vert ? measured_[i].height : measured_[i].width;
        float cross_size = vert ? measured_[i].width : measured_[i].height;
        if (flex_weight > 0.0f && leftover > 0.0f)
        {
            if (to_spacers)
            {
                if (const SpacerControl* spacer = as_spacer(*children_[i]))
                {
                    main_size += leftover * spacer->weight() / flex_weight;
                }
            }
            else if (const FillControl* fill = as_fill(*children_[i]))
            {
                main_size += leftover * fill->weight() / flex_weight;
            }
        }

        float cross_pos = 0.0f;
        switch (child_alignment_)
        {
            case Alignment::Leading:
                cross_pos = cross_origin;
                break;
            case Alignment::Trailing:
                cross_pos = cross_origin + cross_extent - cross_size;
                break;
            case Alignment::Stretch:
                // Arrange uses max(final size, child desired) on the cross axis,
                // so a child that measured wider than the panel overflows instead of being squeezed.
                cross_pos  = cross_origin;
                cross_size = std::max(cross_extent, cross_size);
                break;
            case Alignment::Center:
                cross_pos = cross_origin + (cross_extent - cross_size) * 0.5f;
                break;
        }

        // Whole pixels, as Window::layout does.
        const Rect child_rect = vert ? Rect{std::round(cross_pos), std::round(pos), cross_size, main_size}
                                     : Rect{std::round(pos), std::round(cross_pos), main_size, cross_size};
        children_[i]->arrange(child_rect);

        // A collapsed child takes no room, so it takes no gap either.
        if (children_[i]->visibility() != Visibility::Collapsed)
        {
            pos += main_size + spacing_;
        }
    }
}

void StackControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    draw_border(renderer, bounds(), background_.resolve(theme), border_brush_.resolve(theme), border_thickness_, corner_radius_);
    for (const auto& child : children_)
    {
        child->render(renderer, context);
    }
    if (ghosts_)
    {
        ghosts_->render(renderer, theme, bounds().origin());
    }
}

void StackControl::retire_child(std::unique_ptr<Element> child)
{
    // The delete half of AddRemoveTransition: the child shrinks away where it was. Without
    // the transition it goes at once.
    if (find_transition<AddRemoveTransition>(children_transitions()) != nullptr)
    {
        detail::ensure(ghosts_).add(std::move(child), bounds().origin(), transition_storyboards::remove());
        note_child_deleted();
    }
    // Not destroyed here: the child may be the one whose handler removed it.
    detail::retire(std::move(child));
}

void StackControl::for_each_child(ChildVisitor visit) const
{
    for (std::size_t i = 0; i < children_.size(); ++i) // by index: see pointer_override()
    {
        visit(*children_[i]);
    }
}

void StackControl::tick_override(Duration delta)
{
    if (background_.advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    if (ghosts_ && ghosts_->advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
}

namespace
{

// Vertical and Horizontal differ only in the orientation they pass, so the body lives once.
template <typename S>
std::unique_ptr<ui::Element> build_stack(S& spec, ui::Orientation orientation)
{
    auto              node = std::make_unique<ui::StackControl>(orientation);
    ui::StackControl* raw  = node.get();
    apply(spec.child_alignment, *node, [raw](const ui::Alignment& v) { raw->set_child_alignment(v); });
    apply(spec.spacing, *node, [raw](const float& v) { raw->set_spacing(v); });
    apply(spec.background_transition,
          *node,
          [raw](const std::optional<ui::BrushTransition>& v) { raw->set_background_transition(v); });
    apply(spec.background,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });
    apply(spec.border_brush,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_border_brush(c); }); });
    apply(spec.border_thickness, *node, [raw](const ui::Thickness& v) { raw->set_border_thickness(v); });
    apply(spec.corner_radius, *node, [raw](const float& v) { raw->set_corner_radius(v); });
    apply(spec.padding, *node, [raw](const ui::Thickness& v) { raw->set_padding(v); });
    apply(spec.children_transitions, *node, [raw](const ui::TransitionList& v) { raw->set_children_transitions(v); });

    for (auto& child : spec.content.nodes)
    {
        node->add(std::move(child));
    }

    apply(spec.enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply_common(spec, *node);
    ui::publish_control(spec.control, node.get());
    return node;
}

} // namespace

std::unique_ptr<ui::Element> VerticalStack::build() &&
{
    return build_stack(*this, ui::Orientation::Vertical);
}

std::unique_ptr<ui::Element> HorizontalStack::build() &&
{
    return build_stack(*this, ui::Orientation::Horizontal);
}

} // namespace ui
