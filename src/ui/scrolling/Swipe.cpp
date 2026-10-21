// SPDX-License-Identifier: MIT
#include "winui/ui/scrolling/Swipe.hpp"

#include "../../render/Compositor.hpp"
#include "../OutsidePress.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
// A tap-vs-drag disambiguation threshold: a drag commits once the pointer has moved this far,
// and more across than down.
constexpr float SwipeDragThreshold = 8.0f;
// An item is at least 68 wide and 40 tall (a floor for the whole control once it has items),
// content in a 4,4,4,2 margin, a 16px icon with 2 below it, 12px text.
constexpr float     MinItemWidth      = 68.0f;
constexpr float     MinItemHeight     = 40.0f;
constexpr Thickness ItemContentMargin = Edges{.left = 4.0f, .top = 4.0f, .right = 4.0f, .bottom = 2.0f};
constexpr float     SwipeIconSize     = 16.0f;
constexpr float     SwipeIconGap      = 2.0f;
constexpr float     TextSize          = 12.0f;
// The commit threshold: an absolute budget of min(extent, 100), not "all the way".
constexpr float CommitThresholdPx = 100.0f;
// The normal control animation duration on the fast-out-slow-in curve, standing in for
// inertia.
constexpr double SnapMs = 250.0;

TextStyle item_text_style(const Theme& theme)
{
    TextStyle style = theme.text_style;
    style.font_size = TextSize;
    style.wrapping  = TextWrapping::Wrap;
    return style;
}

// The SwipeControl the pointer last started on: starting on another closes it.
ElementWatch& last_interacted()
{
    static ElementWatch watch;
    return watch;
}
} // namespace

std::string SwipeItem::display_text() const
{
    return text.empty() && command != nullptr ? command->current().text : text;
}

const IconSource& SwipeItem::display_icon() const
{
    return icon.empty() && command != nullptr ? command->current().icon : icon;
}

void SwipeControl::set_left_items(SwipeItems items)
{
    left_items_ = std::move(items);
    animating_  = false;
    offset_     = 0.0f;
    watch_commands();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void SwipeControl::set_right_items(SwipeItems items)
{
    right_items_ = std::move(items);
    animating_   = false;
    offset_      = 0.0f;
    watch_commands();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void SwipeControl::watch_commands()
{
    // Items read their command's label and icon as they are measured and drawn.
    command_changed_.clear();
    for (const SwipeItems* side : {&left_items_, &right_items_})
    {
        for (const SwipeItem& item : side->items)
        {
            if (item.command != nullptr)
            {
                command_changed_.push_back(
                    item.command->observe([this] { mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); }));
            }
        }
    }
}

void SwipeControl::close()
{
    invoke_side_  = Side::None;
    invoke_index_ = detail::NoIndex;
    if (offset_ == 0.0f && !animating_)
    {
        return;
    }
    animate_to(0.0f);
}

void SwipeControl::animate_to(float target)
{
    if (!animating_ && offset_ == target)
    {
        return;
    }
    from_      = offset_;
    target_    = target;
    anim_ms_   = 0.0;
    animating_ = true;
    mark_dirty(DirtyFlags::Visual);
}

void SwipeControl::recompute_item_widths(const TextMeasurer& measurer, const Theme& theme) const
{
    const TextStyle style        = item_text_style(theme);
    auto            measure_side = [&](const SwipeItems& items, std::vector<float>& widths, float& extent)
    {
        widths.clear();
        widths.reserve(items.items.size());
        extent = 0.0f;
        for (const SwipeItem& item : items.items)
        {
            const float text_width = measurer.measure(item.display_text(), style).width;
            const float icon_width = item.display_icon().empty() ? 0.0f : SwipeIconSize;
            (void)item.display_icon().measure(measurer);
            widths.push_back(std::max(MinItemWidth, std::max(text_width, icon_width) + ItemContentMargin.horizontal()));
            extent += widths.back();
        }
    };
    measure_side(left_items_, left_item_widths_, left_extent_);
    measure_side(right_items_, right_item_widths_, right_extent_);
}

float SwipeControl::extent(Side side) const
{
    const SwipeItems& items = side == Side::Left ? left_items_ : right_items_;
    if (side == Side::None || items.items.empty())
    {
        return 0.0f;
    }
    // Execute's single item is as wide as the control; a Reveal strip is as wide as its items.
    if (items.mode == SwipeMode::Execute)
    {
        return bounds().width;
    }
    return side == Side::Left ? left_extent_ : right_extent_;
}

float SwipeControl::threshold(Side side) const
{
    return std::min(extent(side), CommitThresholdPx);
}

Size SwipeControl::measure_override(Size available, const MeasureContext& context) const
{
    recompute_item_widths(context.measurer, context.theme);
    const Size inner_available{std::max(0.0f, available.width - border_thickness_.horizontal()),
                               std::max(0.0f, available.height - border_thickness_.vertical())};
    Size       desired = content_ ? content_->measure(inner_available, context) : Size{};
    desired.width += border_thickness_.horizontal();
    desired.height += border_thickness_.vertical();
    // The item's minimum height applies only when there is a strip to reveal.
    if (!left_items_.items.empty() || !right_items_.items.empty())
    {
        desired.height = std::max(desired.height, MinItemHeight);
    }
    return desired;
}

void SwipeControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    content_base_rect_ = final_rect;
    if (content_)
    {
        content_->arrange(final_rect.inset(border_thickness_));
    }
}

SwipeControl::Side SwipeControl::side_at(Point pos) const
{
    if (offset_ > 0.0f && !left_items_.items.empty() && pos.x >= bounds().left() && pos.x < bounds().left() + offset_)
    {
        return Side::Left;
    }
    if (offset_ < 0.0f && !right_items_.items.empty() && pos.x <= bounds().right() && pos.x > bounds().right() + offset_)
    {
        return Side::Right;
    }
    return Side::None;
}

std::size_t SwipeControl::item_index_at(Side side, Point pos) const
{
    const SwipeItems&         items  = side == Side::Left ? left_items_ : right_items_;
    const std::vector<float>& widths = side == Side::Left ? left_item_widths_ : right_item_widths_;
    if (items.items.empty())
    {
        return detail::NoIndex;
    }
    if (items.mode == SwipeMode::Execute)
    {
        return 0; // the one item fills the whole revealed strip
    }
    float cursor = side == Side::Left ? bounds().left() : bounds().right();
    for (std::size_t i = 0; i < items.items.size(); ++i)
    {
        const float w = widths[i];
        if (side == Side::Left)
        {
            if (pos.x >= cursor && pos.x < cursor + w)
            {
                return i;
            }
            cursor += w;
        }
        else
        {
            if (pos.x <= cursor && pos.x > cursor - w)
            {
                return i;
            }
            cursor -= w;
        }
    }
    return detail::NoIndex;
}

void SwipeControl::invoke(Side side, std::size_t index)
{
    SwipeItems& items = side == Side::Left ? left_items_ : right_items_;
    if (index >= items.items.size())
    {
        return;
    }
    // An invoked item runs on_invoked, then the command, then closes for Auto and Close. The close
    // starts first, and the handlers run last, since one may tear the page down.
    SwipeItem& item = items.items[index];
    if (item.behavior_on_invoked != SwipeBehaviorOnInvoked::RemainOpen)
    {
        close();
    }
    const SwipeItemInvokedHandler handler   = item.on_invoked;
    const Command* const          command   = item.command;
    const std::string             parameter = item.command_parameter;
    SwipeItemInvokedEventArgs     args{*this, item};
    handler(args);
    if (command != nullptr)
    {
        (*command)(parameter);
    }
}

void SwipeControl::snap_to_rest()
{
    const Side side = offset_ > 0.0f ? Side::Left : (offset_ < 0.0f ? Side::Right : Side::None);
    if (side == Side::None)
    {
        animate_to(0.0f);
        return;
    }
    const SwipeItems& items    = side == Side::Left ? left_items_ : right_items_;
    const float       sign     = side == Side::Left ? 1.0f : -1.0f;
    const float       distance = std::abs(offset_);
    const float       full     = extent(side);
    // Where a released drag rests: from closed, past min(extent, 100)
    // opens fully; from open, anything short of fully open closes.
    const bool was_open = drag_start_offset_ != 0.0f;
    const bool opens    = was_open ? distance >= full : distance >= threshold(side);
    if (!opens)
    {
        animate_to(0.0f);
        return;
    }
    animate_to(sign * full);
    if (items.mode == SwipeMode::Execute)
    {
        // An Execute swipe past its threshold settles fully open and invokes its item there.
        invoke_side_  = side;
        invoke_index_ = 0;
    }
}

void SwipeControl::draw_item_content(Renderer&        renderer,
                                     const Theme&     theme,
                                     const SwipeItem& item,
                                     const Rect&      box,
                                     const Color&     foreground) const
{
    // The item's content: the icon row over the text row, centered in its 4,4,4,2 margin.
    const Rect        inner       = box.inset(ItemContentMargin);
    const IconSource& icon        = item.display_icon();
    const std::string text        = item.display_text();
    const TextStyle   style       = item_text_style(theme);
    const float       text_height = text.empty() ? 0.0f : TextSize * 1.35f;
    const float       icon_height = icon.empty() ? 0.0f : SwipeIconSize + SwipeIconGap;
    float             y           = inner.center().y - (text_height + icon_height) * 0.5f;
    if (!icon.empty())
    {
        icon.draw(renderer, Rect{inner.center().x - SwipeIconSize * 0.5f, y, SwipeIconSize, SwipeIconSize}, foreground);
        y += icon_height;
    }
    if (!text.empty())
    {
        renderer.draw_text(text, Rect{inner.x, y, inner.width, text_height}, foreground, style);
    }
}

void SwipeControl::render_side(Renderer& renderer, const Theme& theme, Side side, const Rect& revealed) const
{
    const SwipeItems&         items  = side == Side::Left ? left_items_ : right_items_;
    const std::vector<float>& widths = side == Side::Left ? left_item_widths_ : right_item_widths_;
    if (items.items.empty())
    {
        return;
    }
    renderer.push_clip(revealed);
    auto fill = [&](const Brush& brush, const Rect& rect, const Rect& brush_box)
    {
        if (const Color* color = std::get_if<Color>(&brush))
        {
            renderer.fill_rounded_rect(rect, 0.0f, *color);
            return;
        }
        DrawGeometry cmd;
        cmd.geometry  = PathGeometry::rectangle(rect);
        cmd.fill      = brush;
        cmd.brush_box = brush_box;
        renderer.draw_geometry(cmd);
    };

    if (items.mode == SwipeMode::Execute)
    {
        // One color before the threshold and the accent after it, unless the item sets its own
        // brushes. The brush spans the whole control; icon and text stay centered in the reveal.
        const bool          past = std::abs(offset_) >= threshold(side);
        const ButtonVisual& role = past ? theme.accent_button_normal : detail::controls(theme).swipe_item_pre_threshold;
        const SwipeItem&    item = items.items.front();
        fill(item.background.resolve_brush(theme).value_or(Brush{role.background}), revealed, bounds());
        draw_item_content(renderer, theme, item, revealed, item.foreground.value_or(role.text));
    }
    else
    {
        // Each item its own background and foreground over its own rect.
        float cursor = side == Side::Right ? revealed.right() : revealed.left();
        for (std::size_t i = 0; i < items.items.size(); ++i)
        {
            const SwipeItem& item = items.items[i];
            const float      w    = widths[i];
            const Rect       rect = side == Side::Right ? Rect{cursor - w, revealed.top(), w, revealed.height}
                                                        : Rect{cursor, revealed.top(), w, revealed.height};
            fill(item.background.resolve_brush(theme).value_or(Brush{detail::controls(theme).swipe_item_default.background}),
                 rect,
                 rect);
            draw_item_content(renderer,
                              theme,
                              item,
                              rect,
                              item.foreground.value_or(detail::controls(theme).swipe_item_default.text));
            cursor = side == Side::Right ? cursor - w : cursor + w;
        }
    }
    renderer.pop_clip();
}

void SwipeControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (!content_)
    {
        return;
    }
    if (offset_ > 0.0f)
    {
        render_side(renderer, theme, Side::Left, Rect{bounds().left(), bounds().top(), offset_, bounds().height});
    }
    else if (offset_ < 0.0f)
    {
        render_side(renderer, theme, Side::Right, Rect{bounds().right() + offset_, bounds().top(), -offset_, bounds().height});
    }

    // Re-arranged on every render(), because a drag marks only DirtyFlags::Visual and this is the
    // only place that sees the latest offset (as Parallax does).
    const Rect moved{content_base_rect_.left() + offset_,
                     content_base_rect_.top(),
                     content_base_rect_.width,
                     content_base_rect_.height};
    content_->arrange(moved.inset(border_thickness_));

    renderer.push_clip(bounds());
    // The content's background and border, moving with the content.
    const std::optional<Color> background = background_.resolve(theme);
    const std::optional<Color> border     = border_brush_.resolve(theme);
    if (background || (border && (border_thickness_.left > 0.0f || border_thickness_.top > 0.0f ||
                                  border_thickness_.right > 0.0f || border_thickness_.bottom > 0.0f)))
    {
        draw_border(renderer, moved, background, border, border_thickness_, 0.0f);
    }
    content_->render(renderer, context);
    renderer.pop_clip();
}

void SwipeControl::enabled_changed()
{
    if (!is_enabled())
    {
        pointer_down_      = false;
        drag_owns_gesture_ = false;
        pressed_side_      = Side::None;
        pressed_index_     = detail::NoIndex;
        close();
    }
    mark_dirty(DirtyFlags::Visual);
}

SwipeControl::~SwipeControl()
{
    detail::unwatch_outside_presses(*this);
}

bool SwipeControl::pointer_override(const PointerEvent& e)
{
    // While closed the content has each event first, and it reaches this as it bubbles up; while
    // open this covers the content (covers_children_at()).
    switch (e.action)
    {
        case PointerAction::Down:
        {
            if (e.button != PointerButton::Left)
            {
                return false;
            }
            if (Element* last = last_interacted().get(); last != nullptr && last != this)
            {
                static_cast<SwipeControl*>(last)->close();
            }
            last_interacted().reset(this);
            // A press anywhere outside an open Reveal strip dismisses it. An Execute item that stays
            // open waits for close().
            detail::watch_outside_presses(*this,
                                          [this]
                                          {
                                              const Side open_side = offset_ > 0.0f
                                                                         ? Side::Left
                                                                         : (offset_ < 0.0f ? Side::Right : Side::None);
                                              if (open_side != Side::None &&
                                                  (open_side == Side::Left ? left_items_ : right_items_).mode ==
                                                      SwipeMode::Reveal)
                                              {
                                                  close();
                                              }
                                          });
            if (animating_)
            {
                animating_ = false; // a press takes hold of the strip where it is
            }
            pointer_down_      = true;
            drag_owns_gesture_ = false;
            drag_start_pos_    = e.position;
            drag_start_offset_ = offset_;
            pressed_side_      = Side::None;
            pressed_index_     = detail::NoIndex;
            if (offset_ != 0.0f)
            {
                pressed_side_ = side_at(e.position);
                pressed_index_ = pressed_side_ == Side::None ? detail::NoIndex : item_index_at(pressed_side_, e.position);
                return true;
            }
            return false; // closed: the content's press goes on as an ordinary one
        }

        case PointerAction::Move:
        {
            if (!pointer_down_)
            {
                return false;
            }
            const float dx = e.position.x - drag_start_pos_.x;
            const float dy = e.position.y - drag_start_pos_.y;
            if (!drag_owns_gesture_ && std::abs(dx) > SwipeDragThreshold && std::abs(dx) > std::abs(dy))
            {
                drag_owns_gesture_ = true;
                pressed_side_      = Side::None; // committing to a drag cancels any pending item tap
                capture_pointer();               // the content's press is canceled with a Leave
            }
            if (drag_owns_gesture_)
            {
                // The content translation is clamped to the side that has items.
                const float lo      = -extent(Side::Right);
                const float hi      = extent(Side::Left);
                const float clamped = std::clamp(drag_start_offset_ + dx, lo, hi);
                if (clamped != offset_)
                {
                    offset_ = clamped;
                    mark_dirty(DirtyFlags::Visual);
                }
                return true;
            }
            return false;
        }

        case PointerAction::Up:
        {
            if (e.button != PointerButton::Left)
            {
                return false;
            }
            const bool was_down = pointer_down_;
            pointer_down_       = false;
            if (!was_down)
            {
                return false;
            }
            const Side        side  = pressed_side_;
            const std::size_t index = pressed_index_;
            pressed_side_           = Side::None;
            pressed_index_          = detail::NoIndex;
            if (drag_owns_gesture_)
            {
                drag_owns_gesture_ = false;
                snap_to_rest();
                return true;
            }
            if (side != Side::None)
            {
                invoke(side, index); // last: a handler may tear the page down
                return true;
            }
            if (drag_start_offset_ != 0.0f)
            {
                close(); // a tap on the content while open dismisses it
                return true;
            }
            return false; // an ordinary click, the content's
        }

        case PointerAction::Leave:
        case PointerAction::Wheel:
            break;
    }
    return false;
}

void SwipeControl::handled_pointer_override(const PointerEvent& e)
{
    (void)pointer_override(e);
}

bool SwipeControl::covers_children_at(Point /*p*/) const
{
    // Open or being dragged, this control takes every press on the strip and the content.
    return offset_ != 0.0f;
}

void SwipeControl::tick_override(Duration delta)
{
    if (background_.advance(delta) || border_brush_.advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    if (!animating_)
    {
        return;
    }
    anim_ms_ += animation_delta(delta).count();
    const double progress = std::clamp(anim_ms_ / SnapMs, 0.0, 1.0);
    offset_               = from_ + (target_ - from_) * easing::evaluate(easing::ControlFastOutSlowIn, progress);
    mark_dirty(DirtyFlags::Visual);
    if (progress < 1.0)
    {
        return;
    }
    offset_    = target_;
    animating_ = false;
    // An Execute swipe that has slid fully open invokes its item now, once it is idle.
    const Side        side  = invoke_side_;
    const std::size_t index = invoke_index_;
    invoke_side_            = Side::None;
    invoke_index_           = detail::NoIndex;
    if (side != Side::None && offset_ != 0.0f)
    {
        invoke(side, index); // last: a handler may tear the page down
    }
}

std::unique_ptr<ui::Element> Swipe::build() &&
{
    auto node = std::make_unique<ui::SwipeControl>();
    apply(left_items,
          *node,
          [raw = node.get(), mode = left_mode](const std::vector<ui::SwipeItem>& v)
          { raw->set_left_items(ui::SwipeItems{.items = v, .mode = mode}); });
    apply(right_items,
          *node,
          [raw = node.get(), mode = right_mode](const std::vector<ui::SwipeItem>& v)
          { raw->set_right_items(ui::SwipeItems{.items = v, .mode = mode}); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply(background,
          *node,
          [raw = node.get()](const ui::Paint& v)
          {
              if (v.resource() != nullptr)
              {
                  raw->set_background(v.resource());
              }
              else
              {
                  raw->set_background(v.color());
              }
          });
    apply(border_brush,
          *node,
          [raw = node.get()](const ui::Paint& v)
          {
              if (v.resource() != nullptr)
              {
                  raw->set_border_brush(v.resource());
              }
              else
              {
                  raw->set_border_brush(v.color());
              }
          });
    apply(border_thickness, *node, [raw = node.get()](const Thickness& v) { raw->set_border_thickness(v); });

    if (content)
    {
        node->set_content(content.take());
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
