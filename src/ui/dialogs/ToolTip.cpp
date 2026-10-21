// SPDX-License-Identifier: MIT
#include "winui/ui/dialogs/ToolTip.hpp"

#include "../themes/ControlChrome.hpp"
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
#include <string_view>

namespace ui
{

namespace
{
// The padding around a tooltip's text. A theme that draws the system tooltip rather
// than its own surface sets `ControlChrome::tooltip_padding` instead.
constexpr Thickness ToolTipPadding = Edges{.left = 9.0f, .top = 6.0f, .right = 9.0f, .bottom = 8.0f};
// ToolTipMaxWidth: long content wraps rather than growing past it.
constexpr float ToolTipMaxWidth = 320.0f;
} // namespace

class ToolTipSurface final : public Element
{
public:
    explicit ToolTipSurface(ToolTipControl* owner) : owner_(owner)
    {
    }

protected:
    // Content is offered ToolTipMaxWidth less the padding, so wrapping text wraps there.
    // A centered popup drops the anchor-width hint, which is a ComboBox's.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override
    {
        padding_ = detail::controls(context.theme).tooltip_padding.value_or(ToolTipPadding); // cached for arrange_override(), no Theme there
        if (!owner_->content_)
        {
            return {};
        }
        const float inner_height = std::max(0.0f, available.height - padding_.vertical());
        const float inner_width  = ToolTipMaxWidth - padding_.horizontal();
        const Size  inner        = owner_->content_->measure(Size{inner_width, inner_height}, context);
        // Capped at ToolTipMaxWidth: content that does not wrap is clipped there.
        const float width = std::min(inner.width + padding_.horizontal(), ToolTipMaxWidth);
        return Size{width, inner.height + padding_.vertical()};
    }

    void arrange_override(const Rect& final_rect, const ArrangeContext&) override
    {
        if (owner_->content_)
        {
            owner_->content_->arrange(final_rect.inset(padding_));
        }
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        // A tooltip uses `theme.corner_radius`, unlike every other popup's `overlay_corner_radius`. Luna
        // squares it off through tooltip_radius().
        const float radius = detail::tooltip_radius(theme);
        renderer.fill_rounded_rect(bounds(), radius, theme.surfaces.tooltip);
        stroke_rounded_rect_inside(renderer,
                                   bounds(),
                                   radius,
                                   theme.surfaces.tooltip_border.value_or(theme.surfaces.field_border),
                                   theme.border_width);
        if (owner_->content_)
        {
            owner_->content_->render(renderer, context);
        }
    }


private:
    ToolTipControl* owner_; // non-owning; owner_ outlives this (built on the tip's first open)
    // Resolved in measure_override() for arrange_override(), which has no Theme.
    mutable Thickness padding_{ToolTipPadding};
};

void ToolTipControl::sync_popup()
{
    if (!open_ || !content_)
    {
        close_popup();
        return;
    }
    if (!surface_)
    {
        surface_ = std::make_unique<ToolTipSurface>(this);
    }
    // The dock rect, in this control's coordinates: placement_rect() when set, else the pointer's
    // position when the tip opened, else the whole control.
    Rect dock{0.0f, 0.0f, bounds().width, bounds().height};
    if (placement_rect_)
    {
        dock = *placement_rect_;
    }
    else if (dock_point_)
    {
        dock = Rect{dock_point_->x - bounds().x, dock_point_->y - bounds().y, 0.0f, 0.0f};
    }
    // The 20 DIP default offsets match the system's offset for a mouse-opened tooltip.
    const float h = horizontal_offset_;
    const float v = vertical_offset_;
    // InflateRect, which a negative offset turns inside out: that is the intended arithmetic, and
    // Window places against the edges as they fall.
    open_popup({
        .content             = surface_.get(),
        .anchor_rect         = Rect{dock.x - h, dock.y - v, dock.width + 2.0f * h, dock.height + 2.0f * v},
        .placement           = placement_,
        .placement_alignment = PopupPlacementAlignment::Center,
        // `theme.corner_radius`, not `overlay_corner_radius`, and a tooltip's shadow is cast at Z 16.
        .chrome     = {.corner_radius = [](const Theme& theme) { return detail::tooltip_radius(theme); },
                       .elevation     = elevation_z::ToolTip},
        .on_dismiss = [this](PopupCloseReason) { hide(); },
    });
}

void ToolTipControl::show_at(Point pointer)
{
    dock_point_ = pointer;
    if (!open_)
    {
        open_ = true;
        mark_dirty(DirtyFlags::Visual);
    }
    sync_popup();
}

void ToolTipControl::hide()
{
    hovered_    = false;
    elapsed_ms_ = 0.0;
    if (!open_)
    {
        return;
    }
    open_ = false;
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

Size ToolTipControl::measure_override(Size available, const MeasureContext& context) const
{
    return anchor_ ? anchor_->measure(available, context) : Size{};
}

void ToolTipControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    if (anchor_)
    {
        anchor_->arrange(final_rect);
    }
    if (open_)
    {
        sync_popup(); // the dock rect is kept relative to the control, which may have moved
    }
}

void ToolTipControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    if (anchor_)
    {
        anchor_->render(renderer, context);
    }
}

bool ToolTipControl::pointer_override(const PointerEvent& e)
{
    // Leave and a click close the tip before the event reaches the anchor.
    switch (e.action)
    {
        case PointerAction::Move:
        {
            const bool now_over = bounds().contains(e.position);
            if (now_over)
            {
                last_pointer_ = e.position;
            }
            if (now_over && !hovered_)
            {
                hovered_    = true;
                elapsed_ms_ = 0.0;
            }
            else if (!now_over && hovered_)
            {
                hide();
            }
            break;
        }
        case PointerAction::Leave:
            hide();
            break;
        case PointerAction::Down:
            if (bounds().contains(e.position))
            {
                hide();
            }
            break;
        default:
            break;
    }

    return false;
}

void ToolTipControl::handled_pointer_override(const PointerEvent& e)
{
    if (e.action == PointerAction::Down && bounds().contains(e.position))
    {
        hide();
    }
}

bool ToolTipControl::key_override(const KeyEvent& e)
{
    return anchor_ ? anchor_->handle_key(e) : false;
}

bool ToolTipControl::text_override(const TextInputEvent& e)
{
    return anchor_ ? anchor_->handle_text(e) : false;
}

bool ToolTipControl::focusable() const
{
    return anchor_ && anchor_->focusable();
}

void ToolTipControl::focus_changed(bool focused)
{
    if (anchor_)
    {
        set_focused_of(*anchor_, focused);
    }
}

void ToolTipControl::focus_visible_changed(bool visible)
{
    if (anchor_)
    {
        set_focus_visible_of(*anchor_, visible);
    }
}

void ToolTipControl::tick_override(Duration delta)
{
    if (hovered_ && !open_)
    {
        elapsed_ms_ += delta.count();
        // The 800 ms default delay is the system's mouse hover time (SPI_GETMOUSEHOVERTIME, 400 ms
        // by default) doubled, the initial show delay for the mouse.
        if (elapsed_ms_ >= delay_ms_)
        {
            open_       = true;
            dock_point_ = last_pointer_;
            mark_dirty(DirtyFlags::Visual);
            sync_popup();
        }
        else
        {
            request_tick_in(Duration{delay_ms_ - elapsed_ms_}); // one wake when the delay is up
        }
    }
}

std::unique_ptr<ui::Element> ToolTip::build() &&
{
    auto node = std::make_unique<ui::ToolTipControl>();
    apply(delay, *node, [raw = node.get()](const Duration& v) { raw->set_delay(v); });
    apply(placement, *node, [raw = node.get()](const PopupPlacement& v) { raw->set_placement(v); });
    apply(horizontal_offset, *node, [raw = node.get()](const float& v) { raw->set_horizontal_offset(v); });
    apply(vertical_offset, *node, [raw = node.get()](const float& v) { raw->set_vertical_offset(v); });
    apply(placement_rect, *node, [raw = node.get()](const std::optional<Rect>& v) { raw->set_placement_rect(v); });
    if (anchor)
    {
        node->set_anchor(anchor.take());
    }
    if (content)
    {
        node->set_content(content.take());
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
