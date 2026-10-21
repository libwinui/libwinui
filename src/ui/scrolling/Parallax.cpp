// SPDX-License-Identifier: MIT
#include "winui/ui/scrolling/Parallax.hpp"

#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>

#include <cmath>

namespace ui
{

namespace
{

// The shift at source offset `x`, with `start`/`end` the effective source offsets.
float parallax(float shift, float start, float end, float max_ratio, bool clamped, float x)
{
    max_ratio = std::max(0.0f, max_ratio);
    if (clamped)
    {
        if (shift > 0.0f)
        {
            if (x <= start)
            {
                return 0.0f;
            }
            if (x < end)
            {
                return -std::min(max_ratio, shift / (end - start)) * (x - start);
            }
            return -std::min(max_ratio * std::max(0.0f, end - start), shift);
        }
        if (x <= start)
        {
            return -std::min(max_ratio * std::max(0.0f, end - start), -shift);
        }
        if (x < end)
        {
            return std::min(max_ratio, shift / (start - end)) * (x - end);
        }
        return 0.0f;
    }
    if (start == end)
    {
        return 0.0f;
    }
    if (shift > 0.0f)
    {
        return -std::min(max_ratio, shift / (end - start)) * (x - start);
    }
    return std::min(max_ratio, shift / (start - end)) * (x - end);
}

bool is_ancestor(const Element* ancestor, const Element* e)
{
    for (const Element* p = e != nullptr ? e->parent() : nullptr; p != nullptr; p = p->parent())
    {
        if (p == ancestor)
        {
            return true;
        }
    }
    return false;
}

} // namespace

void ParallaxControl::attach_child()
{
    // An expression on the child's Translation, so rendering and hit testing are never a frame
    // behind the source.
    child_->start_animation(AnimatedProperty::Translation,
                            ExpressionAnimation{Callback<Vector3()>{[this]
                                                                    {
                                                                        const Point p = current_shift();
                                                                        return Vector3{p.x, p.y, 0.0f};
                                                                    }}});
}

float ParallaxControl::axis_shift(const Axis& axis, bool horizontal) const
{
    const Element* source = source_.get();
    if (axis.shift == 0.0f || source == nullptr)
    {
        return 0.0f;
    }
    const std::optional<ScrollInfo> info = source->scroll_info();
    if (!info)
    {
        return 0.0f;
    }
    const float x        = horizontal ? info->offset.x : info->offset.y;
    const float viewport = horizontal ? info->viewport.width : info->viewport.height;
    const float content  = horizontal ? info->extent.width : info->extent.height;
    const float pan      = horizontal ? info->out_of_bounds_pan.width : info->out_of_bounds_pan.height;
    const float zoom     = info->zoom;

    float start = 0.0f;
    float end   = 0.0f;
    if (axis.kind == ParallaxSourceOffsetKind::Relative)
    {
        // The start and end offsets for Relative.
        if (info->content != nullptr && (info->content == this || is_ancestor(info->content, this)))
        {
            const float offset = horizontal ? bounds().x - info->content->bounds().x
                                            : bounds().y - info->content->bounds().y;
            const float size   = horizontal ? bounds().width : bounds().height;
            start              = (offset + axis.start) * zoom - viewport - pan;
            end                = (offset + size + axis.end) * zoom + pan;
        }
        else
        {
            start = axis.start * zoom - pan;
            end   = std::max(0.0f, (content + axis.end) * zoom - viewport) + pan;
        }
    }
    else
    {
        start = axis.start > 0.0f ? axis.start * zoom : axis.start;
        if (content > viewport)
        {
            end = axis.end <= content - viewport
                      ? std::max(0.0f, axis.end * zoom)
                      : std::max(0.0f, (content - viewport) * zoom) + axis.end - content + viewport;
        }
        else
        {
            end = axis.end <= 0.0f ? std::max(0.0f, (content + axis.end) * zoom - viewport)
                                   : std::max(0.0f, content * zoom - viewport) + axis.end;
        }
    }
    return parallax(axis.shift, start, end, axis.max_ratio, axis.clamped, x);
}

Point ParallaxControl::current_shift() const
{
    return Point{axis_shift(horizontal_, true), axis_shift(vertical_, false)};
}

Size ParallaxControl::measure_override(Size available, const MeasureContext& context) const
{
    if (!child_)
    {
        child_desired_ = Size{};
        return Size{};
    }
    // Measure: the shift is offered on top, so the child can be as much
    // larger as it moves.
    const Size offered{available.width + std::abs(horizontal_.shift), available.height + std::abs(vertical_.shift)};
    child_desired_ = child_->measure(offered, context);
    return Size{std::isfinite(available.width) ? available.width : child_desired_.width,
                std::isfinite(available.height) ? available.height : child_desired_.height};
}

void ParallaxControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    if (!child_)
    {
        return;
    }
    // Arrange: a child stretching on the other axis grows there by the
    // same ratio.
    Rect        rect{0.0f, 0.0f, child_desired_.width, child_desired_.height};
    const float hshift = std::abs(horizontal_.shift);
    const float vshift = std::abs(vertical_.shift);
    if (horizontal_.shift != 0.0f && rect.width < final_rect.width + hshift)
    {
        const float ratio = rect.width > 0.0f ? (final_rect.width + hshift) / rect.width : 0.0f;
        rect.width        = final_rect.width + hshift;
        if (ratio != 0.0f && !child_->height() && child_->vertical_alignment() == Alignment::Stretch)
        {
            rect.height *= ratio;
        }
    }
    if (vertical_.shift != 0.0f && rect.height < final_rect.height + vshift)
    {
        const float ratio = rect.height > 0.0f ? (final_rect.height + vshift) / rect.height : 0.0f;
        rect.height       = final_rect.height + vshift;
        if (ratio != 0.0f && !child_->width() && child_->horizontal_alignment() == Alignment::Stretch)
        {
            rect.width *= ratio;
        }
    }
    switch (child_->horizontal_alignment())
    {
        case Alignment::Center:
            rect.x = (final_rect.width - rect.width) / 2.0f;
            break;
        case Alignment::Trailing:
            rect.x = final_rect.width - rect.width;
            break;
        case Alignment::Stretch:
            rect.x = rect.width < final_rect.width ? (final_rect.width - rect.width) / 2.0f : 0.0f;
            break;
        case Alignment::Leading:
            break;
    }
    switch (child_->vertical_alignment())
    {
        case Alignment::Center:
            rect.y = (final_rect.height - rect.height) / 2.0f;
            break;
        case Alignment::Trailing:
            rect.y = final_rect.height - rect.height;
            break;
        case Alignment::Stretch:
            rect.y = rect.height < final_rect.height ? (final_rect.height - rect.height) / 2.0f : 0.0f;
            break;
        case Alignment::Leading:
            break;
    }
    child_->arrange(Rect{final_rect.x + rect.x, final_rect.y + rect.y, rect.width, rect.height});
}

void ParallaxControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    if (!child_)
    {
        return;
    }
    renderer.push_clip(bounds());
    child_->render(renderer, context);
    renderer.pop_clip();
}

std::unique_ptr<ui::Element> Parallax::build() &&
{
    auto node = std::make_unique<ui::ParallaxControl>();
    apply(horizontal_shift, *node, [raw = node.get()](const float& v) { raw->set_horizontal_shift(v); });
    apply(vertical_shift, *node, [raw = node.get()](const float& v) { raw->set_vertical_shift(v); });
    apply(horizontal_source_start_offset,
          *node,
          [raw = node.get()](const float& v) { raw->set_horizontal_source_start_offset(v); });
    apply(horizontal_source_end_offset,
          *node,
          [raw = node.get()](const float& v) { raw->set_horizontal_source_end_offset(v); });
    apply(vertical_source_start_offset,
          *node,
          [raw = node.get()](const float& v) { raw->set_vertical_source_start_offset(v); });
    apply(vertical_source_end_offset,
          *node,
          [raw = node.get()](const float& v) { raw->set_vertical_source_end_offset(v); });
    apply(horizontal_source_offset_kind,
          *node,
          [raw = node.get()](const ParallaxSourceOffsetKind& v) { raw->set_horizontal_source_offset_kind(v); });
    apply(vertical_source_offset_kind,
          *node,
          [raw = node.get()](const ParallaxSourceOffsetKind& v) { raw->set_vertical_source_offset_kind(v); });
    apply(max_horizontal_shift_ratio,
          *node,
          [raw = node.get()](const float& v) { raw->set_max_horizontal_shift_ratio(v); });
    apply(max_vertical_shift_ratio, *node, [raw = node.get()](const float& v) { raw->set_max_vertical_shift_ratio(v); });
    node->set_horizontal_shift_clamped(horizontal_shift_clamped);
    node->set_vertical_shift_clamped(vertical_shift_clamped);

    if (content)
    {
        node->set_child(content.take());
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
