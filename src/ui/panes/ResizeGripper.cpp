// SPDX-License-Identifier: MIT
#include "winui/ui/panes/ResizeGripper.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <utility>

namespace ui
{

namespace
{
constexpr float DotRadius  = 1.0f;
constexpr float RestAlpha  = 0.55f;
constexpr float HoverAlpha = 0.85f;
} // namespace

Size ResizeGripperControl::measure_override(Size, const MeasureContext&) const
{
    return Size{size_, size_};
}

void ResizeGripperControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme     = context.theme;
    const Color  dot_color = theme.colors().text.with_alpha((dragging_ || hovered_) ? HoverAlpha : RestAlpha);

    // The Win32 SIZEGRIP glyph (1, 2, 3 dots on the anti-diagonal), drawn with FillEllipse
    // instead of its bevelled squares.
    const float step = bounds().width / 4.0f;
    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            if (col + row < 2)
            {
                continue;
            }
            const Point center{
                bounds().right() - static_cast<float>(3 - col) * step,
                bounds().bottom() - static_cast<float>(3 - row) * step,
            };
            renderer.fill_ellipse(center, DotRadius, DotRadius, dot_color);
        }
    }
}

void ResizeGripperControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_  = false;
        dragging_ = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

bool ResizeGripperControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
            hovered_ = bounds().contains(e.position);
            if (dragging_)
            {
                const float dx = e.position.x - drag_last_pos_.x;
                const float dy = e.position.y - drag_last_pos_.y;
                drag_last_pos_ = e.position;
                if ((dx != 0.0f || dy != 0.0f) && on_drag_delta_)
                {
                    on_drag_delta_(Point{dx, dy});
                }
            }
            break;

        case PointerAction::Down:
            if (e.button == PointerButton::Left && bounds().contains(e.position))
            {
                dragging_      = true;
                hovered_       = true;
                handled        = true;
                drag_last_pos_ = e.position;
            }
            break;

        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                handled   = dragging_;
                dragging_ = false;
                hovered_  = bounds().contains(e.position);
            }
            break;

        case PointerAction::Leave:
            hovered_ = false;
            break;
        case PointerAction::Wheel:
            break;
    }
    return handled;
}

std::unique_ptr<ui::Element> ResizeGripper::build() &&
{
    auto node = std::make_unique<ui::ResizeGripperControl>();
    apply(size, *node, [raw = node.get()](const float& v) { raw->set_size(v); });
    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });

    if (on_drag_delta)
    {
        node->on_drag_delta(std::move(on_drag_delta));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
