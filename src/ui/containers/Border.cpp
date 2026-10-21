// SPDX-License-Identifier: MIT
#include "winui/ui/containers/Border.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/OverlayShadow.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>

namespace ui
{

Thickness BorderControl::combined() const
{
    return Edges{.left   = border_thickness_.left + padding_.left,
                 .top    = border_thickness_.top + padding_.top,
                 .right  = border_thickness_.right + padding_.right,
                 .bottom = border_thickness_.bottom + padding_.bottom};
}

Size BorderControl::measure_override(Size available, const MeasureContext& context) const
{
    // The child is offered what is left inside the border and padding
    // (an unbounded axis stays unbounded), and asks for its own size plus them.
    const Thickness c = combined();
    const Size inner{std::max(0.0f, available.width - c.horizontal()), std::max(0.0f, available.height - c.vertical())};
    const Size child = ContentHost::measure_override(inner, context);
    return Size{child.width + c.horizontal(), child.height + c.vertical()};
}

void BorderControl::arrange_override(const Rect& final_rect, const ArrangeContext& context)
{
    // The rect inside the border and the padding, never negative.
    const Thickness c = combined();
    const Rect      inner{final_rect.x + c.left,
                          final_rect.y + c.top,
                          std::max(0.0f, final_rect.width - c.horizontal()),
                          std::max(0.0f, final_rect.height - c.vertical())};
    ContentHost::arrange_override(inner, context);
}

void BorderControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (shadow_)
    {
        draw_drop_shadow(renderer, bounds(), corner_radius_, ShadowRecipe::for_elevation(current_translation().z, theme.dark));
    }
    draw_border(renderer,
                bounds(),
                background_.resolve(theme),
                border_brush_resource_ != nullptr ? std::optional<Color>{border_brush_resource_(theme)} : border_brush_,
                border_thickness_,
                corner_radius_);
    if (!clip_content_)
    {
        ContentHost::render_override(renderer, context);
        return;
    }
    // The inside of the border, its corners rounded by what the border leaves of the radius.
    const Thickness& t = border_thickness_;
    const Rect       inner{bounds().x + t.left,
                           bounds().y + t.top,
                           std::max(0.0f, bounds().width - t.horizontal()),
                           std::max(0.0f, bounds().height - t.vertical())};
    const float      thickest = std::max({t.left, t.top, t.right, t.bottom});
    const ClipScope  clip{renderer, inner, std::max(0.0f, corner_radius_ - thickest)};
    ContentHost::render_override(renderer, context);
}

void BorderControl::tick_override(Duration delta)
{
    if (background_.advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    ContentHost::tick_override(delta);
}

std::unique_ptr<ui::Element> Border::build() &&
{
    auto               node = std::make_unique<ui::BorderControl>();
    ui::BorderControl* raw  = node.get();
    apply(corner_radius, *node, [raw = node.get()](const float& v) { raw->set_corner_radius(v); });
    apply(background,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });
    apply(border_brush,
          *node,
          [raw](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_border_brush(c); }); });
    apply(border_thickness, *node, [raw](const ui::Thickness& v) { raw->set_border_thickness(v); });
    apply(padding, *node, [raw](const ui::Thickness& v) { raw->set_padding(v); });
    apply(shadow, *node, [raw = node.get()](const bool& v) { raw->set_shadow(v); });
    apply(elevation, *node, [raw](const float& v) { raw->set_elevation(v); });
    apply(clip_content, *node, [raw](const bool& v) { raw->set_clip_content(v); });
    if (content)
    {
        node->set_child(content.take());
    }
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
