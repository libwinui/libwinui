// SPDX-License-Identifier: MIT
#include "winui/ui/containers/ScaleToFit.hpp"

#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string_view>

#include <cmath>

namespace ui
{

namespace
{

// The alignment offset along one axis: a Stretch element whose content is
// smaller than its slot is centered in it, one whose content is larger is anchored at the start.
float align_offset(Alignment alignment, float client, float ink)
{
    if (alignment == Alignment::Stretch && ink > client)
    {
        alignment = Alignment::Leading;
    }
    switch (alignment)
    {
        case Alignment::Center:
        case Alignment::Stretch:
            return (client - ink) * 0.5f;
        case Alignment::Trailing:
            return client - ink;
        case Alignment::Leading:
            break;
    }
    return 0.0f;
}

} // namespace

Size ScaleToFitControl::compute_scale_factor(Size available, Size content) const
{
    // The scale factor, with 0 standing for the infinite axis.
    float      scale_x       = 1.0f;
    float      scale_y       = 1.0f;
    const bool constrained_w = std::isfinite(available.width);
    const bool constrained_h = std::isfinite(available.height);
    if (stretch_ != Stretch::None && (constrained_w || constrained_h))
    {
        scale_x = content.width < 1e-6f ? 0.0f : available.width / content.width;
        scale_y = content.height < 1e-6f ? 0.0f : available.height / content.height;
        if (!constrained_w)
        {
            scale_x = scale_y;
        }
        else if (!constrained_h)
        {
            scale_y = scale_x;
        }
        else
        {
            switch (stretch_)
            {
                case Stretch::Uniform:
                    scale_x = scale_y = std::min(scale_x, scale_y);
                    break;
                case Stretch::UniformToFill:
                    scale_x = scale_y = std::max(scale_x, scale_y);
                    break;
                case Stretch::Fill:
                case Stretch::None:
                    break;
            }
        }
        switch (stretch_direction_)
        {
            case StretchDirection::UpOnly:
                scale_x = std::max(1.0f, scale_x);
                scale_y = std::max(1.0f, scale_y);
                break;
            case StretchDirection::DownOnly:
                scale_x = std::min(1.0f, scale_x);
                scale_y = std::min(1.0f, scale_y);
                break;
            case StretchDirection::Both:
                break;
        }
    }
    return Size{scale_x, scale_y};
}

Size ScaleToFitControl::measure_override(Size available, const MeasureContext& context) const
{
    child_desired_   = child_ ? child_->measure(Size{Unbounded, Unbounded}, context) : Size{};
    const Size scale = compute_scale_factor(available, child_desired_);
    return Size{scale.width * child_desired_.width, scale.height * child_desired_.height};
}

void ScaleToFitControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    const Size scale  = compute_scale_factor(Size{final_rect.width, final_rect.height}, child_desired_);
    scale_x_          = scale.width;
    scale_y_          = scale.height;
    const float ink_w = scale_x_ * child_desired_.width;
    const float ink_h = scale_y_ * child_desired_.height;
    const float x     = final_rect.x + align_offset(horizontal_alignment(), final_rect.width, ink_w);
    const float y     = final_rect.y + align_offset(vertical_alignment(), final_rect.height, ink_h);

    // The child keeps its natural size, laid out with its top-left at the ScaleToFit's, and is drawn
    // through `screen = child * scale + offset_`: scaled about that corner, then moved to (x, y).
    if (child_)
    {
        child_->arrange(Rect{final_rect.x, final_rect.y, child_desired_.width, child_desired_.height});
    }
    offset_ = Point{x - final_rect.x * scale_x_, y - final_rect.y * scale_y_};
}

Point ScaleToFitControl::to_child(Point p) const
{
    return Point{scale_x_ != 0.0f ? (p.x - offset_.x) / scale_x_ : p.x,
                 scale_y_ != 0.0f ? (p.y - offset_.y) / scale_y_ : p.y};
}

void ScaleToFitControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    if (!child_ || scale_x_ <= 0.0f || scale_y_ <= 0.0f)
    {
        return;
    }
    renderer.push_clip(bounds());
    renderer.push_transform(scale_x_, scale_y_, offset_);
    child_->render(renderer, context);
    renderer.pop_transform();
    renderer.pop_clip();
}

std::unique_ptr<ui::Element> ScaleToFit::build() &&
{
    auto                   node = std::make_unique<ui::ScaleToFitControl>();
    ui::ScaleToFitControl* raw  = node.get();
    apply(stretch, *node, [raw](const ui::Stretch& v) { raw->set_stretch(v); });
    apply(stretch_direction, *node, [raw](const ui::StretchDirection& v) { raw->set_stretch_direction(v); });
    if (content)
    {
        node->set_child(content.take());
    }
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
