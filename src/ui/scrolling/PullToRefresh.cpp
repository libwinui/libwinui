// SPDX-License-Identifier: MIT
#include "winui/ui/scrolling/PullToRefresh.hpp"

#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Symbol.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include <cmath>

namespace ui
{

namespace
{
// The indicator and the pull defaults.
constexpr float  VisualizerSize = 100.0f; // the visualizer's size along the pull
constexpr float  ExecutionRatio = 0.8f;   // the share of the pull that arms a refresh
constexpr float  IndicatorSize  = 30.0f;  // the indicator's width and height
constexpr float  MinOpacity     = 0.4f;   // the indicator's opacity until a refresh is pending
constexpr float  ParallaxRatio  = 0.5f;   // the indicator's parallax share of the pull
constexpr double SettleMs       = 100.0;  // the default animation handler's moves
constexpr double PendingMs      = 300.0;  // the Pending scale key frames
constexpr double SpinMs         = 500.0;  // one Refreshing revolution
// A pull starts once a drag mostly along the pull passes this (SwipeControl's tap-vs-drag distance).
constexpr float PullToRefreshDragThreshold = 8.0f;
// How near its edge the content must be scrolled for a pull to start.
constexpr float EdgeThreshold = 1.0f;
} // namespace

PullToRefreshControl::PullToRefreshControl()
{
    replace_indicator(nullptr);
}

PullToRefreshControl::~PullToRefreshControl() = default;

void PullToRefreshControl::replace_indicator(std::unique_ptr<Element> content)
{
    if (!content)
    {
        auto icon = std::make_unique<IconControl>(symbol_glyph(Symbol::Refresh));
        icon->set_width(IndicatorSize);
        icon->set_height(IndicatorSize);
        content = std::move(icon);
    }
    set_parent_of(*content, this);
    indicator_ = std::move(content);
    start_indicator_animations();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

// Rotation, scale and opacity are animations restarted on every state change, so an
// on_refresh_state_changed handler may stop any of them.
void PullToRefreshControl::start_indicator_animations()
{
    if (!indicator_)
    {
        return;
    }
    indicator_->start_animation(AnimatedProperty::Rotation,
                                ExpressionAnimation{Callback<float()>{[this] { return indicator_angle(); }}});
    indicator_->start_animation(AnimatedProperty::Scale,
                                ExpressionAnimation{Callback<Vector3()>{[this]
                                                                        {
                                                                            const float s = indicator_scale();
                                                                            return Vector3{s, s, 1.0f};
                                                                        }}});
    indicator_->start_animation(AnimatedProperty::Opacity,
                                ExpressionAnimation{Callback<float()>{[this] { return indicator_opacity(); }}});
}

float PullToRefreshControl::interaction_ratio() const
{
    return std::clamp(pull_distance_ / VisualizerSize, 0.0f, 1.0f);
}

void PullToRefreshControl::set_pull_direction(RefreshPullDirection direction)
{
    if (direction == direction_)
    {
        return;
    }
    if (drag_owns_gesture_)
    {
        end_pull();
    }
    pointer_down_ = false;
    direction_    = direction;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

float PullToRefreshControl::start_angle() const
{
    // The indicator's starting rotation for each orientation, in degrees.
    switch (orientation_)
    {
        case RefreshVisualizerOrientation::Normal:
            return 0.0f;
        case RefreshVisualizerOrientation::Rotate90DegreesCounterclockwise:
            return 90.0f;
        case RefreshVisualizerOrientation::Rotate270DegreesCounterclockwise:
            return -90.0f;
        case RefreshVisualizerOrientation::Auto:
            break;
    }
    switch (direction_)
    {
        case RefreshPullDirection::LeftToRight:
            return -90.0f;
        case RefreshPullDirection::RightToLeft:
            return 90.0f;
        case RefreshPullDirection::TopToBottom:
        case RefreshPullDirection::BottomToTop:
            break;
    }
    return 0.0f;
}

float PullToRefreshControl::indicator_angle() const
{
    switch (state_)
    {
        case PullToRefreshState::Interacting:
            return start_angle() + 360.0f * std::clamp(interaction_ratio(), 0.0f, ExecutionRatio) / ExecutionRatio;
        case PullToRefreshState::Refreshing:
            return start_angle() + static_cast<float>(360.0 * std::fmod(refreshing_ms_, SpinMs) / SpinMs);
        case PullToRefreshState::Idle:
        case PullToRefreshState::Pending:
            break;
    }
    return start_angle();
}

float PullToRefreshControl::indicator_scale() const
{
    if (state_ != PullToRefreshState::Pending || pending_ms_ >= PendingMs)
    {
        return 1.0f;
    }
    // Key frames 1.5 at half-time and 1.0 at the end.
    const float t = static_cast<float>(pending_ms_ / PendingMs);
    return t < 0.5f ? 1.0f + t : 1.5f - (t - 0.5f);
}

float PullToRefreshControl::indicator_opacity() const
{
    return state_ == PullToRefreshState::Pending || state_ == PullToRefreshState::Refreshing ? 1.0f : MinOpacity;
}

float PullToRefreshControl::content_offset() const
{
    if (settle_ms_ < 0.0)
    {
        return pull_distance_;
    }
    const float t = static_cast<float>(std::min(1.0, settle_ms_ / SettleMs));
    return settle_from_ + (settle_to_ - settle_from_) * t;
}

bool PullToRefreshControl::indicator_visible() const
{
    return content_offset() > 0.0f;
}

bool PullToRefreshControl::can_start_pull() const
{
    if (state_ == PullToRefreshState::Refreshing)
    {
        return false; // a refresh in flight is not interrupted
    }
    if (content_)
    {
        if (const std::optional<ScrollInfo> info = content_->scroll_info())
        {
            const float scrollable_height = std::max(0.0f, info->extent.height * info->zoom - info->viewport.height);
            const float scrollable_width  = std::max(0.0f, info->extent.width * info->zoom - info->viewport.width);
            switch (direction_)
            {
                case RefreshPullDirection::TopToBottom:
                    return info->offset.y < EdgeThreshold;
                case RefreshPullDirection::BottomToTop:
                    return info->offset.y > scrollable_height - EdgeThreshold;
                case RefreshPullDirection::LeftToRight:
                    return info->offset.x < EdgeThreshold;
                case RefreshPullDirection::RightToLeft:
                    return info->offset.x > scrollable_width - EdgeThreshold;
            }
        }
    }
    return true;
}

Rect PullToRefreshControl::pulled(const Rect& base, float offset) const
{
    const float d = pull_sign() * offset;
    return vertical() ? Rect{base.x, base.y + d, base.width, base.height}
                      : Rect{base.x + d, base.y, base.width, base.height};
}

void PullToRefreshControl::end_pull()
{
    drag_owns_gesture_ = false;
    if (state_ == PullToRefreshState::Pending)
    {
        begin_refresh();
        return;
    }
    settle_from_   = content_offset();
    settle_to_     = 0.0f;
    settle_ms_     = 0.0;
    pull_distance_ = 0.0f;
    set_state(PullToRefreshState::Idle);
}

void PullToRefreshControl::set_state(PullToRefreshState state)
{
    if (state == state_)
    {
        return;
    }
    const PullToRefreshState old = state_;
    state_                       = state;
    if (state == PullToRefreshState::Pending)
    {
        pending_ms_ = 0.0;
    }
    if (state == PullToRefreshState::Refreshing)
    {
        refreshing_ms_ = 0.0;
    }
    start_indicator_animations();
    mark_dirty(DirtyFlags::Visual);
    if (on_refresh_state_changed_)
    {
        on_refresh_state_changed_(PullToRefreshStateChangedArgs{.old_state = old, .new_state = state_});
    }
}

void PullToRefreshControl::set_pull_distance(float distance)
{
    distance = std::clamp(distance, 0.0f, VisualizerSize);
    if (state_ == PullToRefreshState::Refreshing || distance == pull_distance_)
    {
        return;
    }
    pull_distance_ = distance;
    settle_ms_     = -1.0;
    mark_dirty(DirtyFlags::Visual);
    if (pull_distance_ <= 0.0f)
    {
        set_state(PullToRefreshState::Idle);
    }
    else if (interaction_ratio() > ExecutionRatio)
    {
        set_state(PullToRefreshState::Pending);
    }
    else
    {
        set_state(PullToRefreshState::Interacting);
    }
}

void PullToRefreshControl::begin_refresh()
{
    if (state_ == PullToRefreshState::Refreshing)
    {
        return;
    }
    settle_from_   = content_offset();
    settle_to_     = ExecutionRatio * VisualizerSize;
    settle_ms_     = 0.0;
    pull_distance_ = settle_to_;
    set_state(PullToRefreshState::Refreshing);
    if (on_refresh_requested_)
    {
        on_refresh_requested_();
    }
}

void PullToRefreshControl::request_refresh()
{
    begin_refresh();
}

void PullToRefreshControl::complete_refresh()
{
    if (state_ != PullToRefreshState::Refreshing)
    {
        return;
    }
    settle_from_   = content_offset();
    settle_to_     = 0.0f;
    settle_ms_     = 0.0;
    pull_distance_ = 0.0f;
    set_state(PullToRefreshState::Idle);
}

// ---- layout ----

Size PullToRefreshControl::measure_override(Size available, const MeasureContext& context) const
{
    measurer_ = &context.measurer;
    theme_    = &context.theme;
    if (indicator_)
    {
        (void)indicator_->measure(Size{Unbounded, Unbounded}, context);
    }
    return content_ ? content_->measure(available, context) : Size{};
}

void PullToRefreshControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    content_base_rect_ = final_rect;
    if (content_)
    {
        content_->arrange(pulled(final_rect, content_offset()));
    }
}

// ---- render ----

void PullToRefreshControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const float offset = content_offset();
    if (indicator_visible() && indicator_)
    {
        Rect revealed{};
        switch (direction_)
        {
            case RefreshPullDirection::TopToBottom:
                revealed = Rect{bounds().left(), bounds().top(), bounds().width, offset};
                break;
            case RefreshPullDirection::BottomToTop:
                revealed = Rect{bounds().left(), bounds().bottom() - offset, bounds().width, offset};
                break;
            case RefreshPullDirection::LeftToRight:
                revealed = Rect{bounds().left(), bounds().top(), offset, bounds().height};
                break;
            case RefreshPullDirection::RightToLeft:
                revealed = Rect{bounds().right() - offset, bounds().top(), offset, bounds().height};
                break;
        }
        renderer.push_clip(revealed);
        float parallax = 0.0f;
        if (state_ == PullToRefreshState::Refreshing || state_ == PullToRefreshState::Pending)
        {
            parallax = (1.0f - ExecutionRatio) * VisualizerSize * ParallaxRatio;
        }
        else if (state_ == PullToRefreshState::Interacting)
        {
            parallax = (1.0f - ExecutionRatio) * VisualizerSize * ParallaxRatio *
                       std::min(interaction_ratio() / ExecutionRatio, 1.0f);
        }
        // The default animation handler's -V + min(V, pull), mirrored for a pull from the far edge.
        const float along = VisualizerSize * 0.5f - VisualizerSize + offset + parallax;
        float       cx    = bounds().left() + bounds().width * 0.5f;
        float       cy    = bounds().top() + bounds().height * 0.5f;
        switch (direction_)
        {
            case RefreshPullDirection::TopToBottom:
                cy = bounds().top() + along;
                break;
            case RefreshPullDirection::BottomToTop:
                cy = bounds().bottom() - along;
                break;
            case RefreshPullDirection::LeftToRight:
                cx = bounds().left() + along;
                break;
            case RefreshPullDirection::RightToLeft:
                cx = bounds().right() - along;
                break;
        }
        Element* icon = indicator_.get();
        if (measurer_ != nullptr && theme_ != nullptr)
        {
            const Size d = icon->measure(Size{Unbounded, Unbounded}, *measurer_, *theme_);
            icon->arrange(Rect{cx - d.width * 0.5f, cy - d.height * 0.5f, d.width, d.height});
            icon->set_center_point(Vector3{d.width * 0.5f, d.height * 0.5f, 0.0f});
        }
        icon->render(renderer, context);
        renderer.pop_clip();
    }

    // Re-arranged on every render: a pull only marks Visual, so render() is what sees it.
    if (content_)
    {
        content_->arrange(pulled(content_base_rect_, offset));
        renderer.push_clip(bounds());
        content_->render(renderer, context);
        renderer.pop_clip();
    }
}

// ---- input ----

bool PullToRefreshControl::pointer_override(const PointerEvent& e)
{
    // The content has each event first, and it reaches this as it bubbles up. Once a pull takes
    // the gesture, the capture moves here and the content gets a Leave for its press.
    switch (e.action)
    {
        case PointerAction::Down:
            if (e.button == PointerButton::Left)
            {
                pointer_down_      = true;
                drag_owns_gesture_ = false;
                drag_start_pos_    = e.position;
            }
            return false;
        case PointerAction::Move:
        {
            if (!pointer_down_)
            {
                return false;
            }
            const float dx     = e.position.x - drag_start_pos_.x;
            const float dy     = e.position.y - drag_start_pos_.y;
            const float along  = pull_sign() * (vertical() ? dy : dx);
            const float across = vertical() ? dx : dy;
            if (!drag_owns_gesture_ && along > PullToRefreshDragThreshold && along > std::abs(across) && can_start_pull())
            {
                drag_owns_gesture_ = true;
                capture_pointer();
            }
            if (drag_owns_gesture_)
            {
                set_pull_distance(along);
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
            if (was_down && drag_owns_gesture_)
            {
                end_pull();
                return true;
            }
            return false;
        }
        case PointerAction::Leave:
        case PointerAction::Wheel:
            break;
    }
    return false;
}

void PullToRefreshControl::handled_pointer_override(const PointerEvent& e)
{
    (void)pointer_override(e);
}

// ---- plumbing ----

void PullToRefreshControl::clear_dirty_override(DirtyFlags flags)
{
    if (indicator_)
    {
        clear_dirty_of(*indicator_, flags);
    }
}

void PullToRefreshControl::build_visual_tree_override(VisualTree& tree) const
{
    if (indicator_visible() && indicator_)
    {
        indicator_->build_visual_tree(tree, visual_id());
    }
}

void PullToRefreshControl::tick_override(Duration delta)
{
    bool moving = false;
    if (settle_ms_ >= 0.0)
    {
        settle_ms_ += animation_delta(delta).count();
        moving = true;
        if (settle_ms_ >= SettleMs)
        {
            settle_ms_ = -1.0;
        }
        if (content_)
        {
            content_->arrange(pulled(content_base_rect_, content_offset()));
        }
    }
    if (state_ == PullToRefreshState::Pending && pending_ms_ < PendingMs)
    {
        pending_ms_ += animation_delta(delta).count();
        moving = true;
    }
    if (state_ == PullToRefreshState::Refreshing)
    {
        refreshing_ms_ += delta.count();
        moving = true;
    }
    if (moving)
    {
        mark_dirty(DirtyFlags::Visual);
    }
    if (indicator_)
    {
        indicator_->tick(delta);
    }
}

std::unique_ptr<ui::Element> PullToRefresh::build() &&
{
    auto node = std::make_unique<ui::PullToRefreshControl>();
    apply(pull_direction, *node, [raw = node.get()](const RefreshPullDirection& v) { raw->set_pull_direction(v); });
    apply(visualizer_orientation,
          *node,
          [raw = node.get()](const RefreshVisualizerOrientation& v) { raw->set_visualizer_orientation(v); });

    if (on_refresh_requested)
    {
        node->on_refresh_requested(std::move(on_refresh_requested));
    }
    if (on_refresh_state_changed)
    {
        node->on_refresh_state_changed(std::move(on_refresh_state_changed));
    }

    if (content)
    {
        node->set_content(content.take());
    }
    if (visualizer_content)
    {
        node->set_visualizer_content(visualizer_content.take());
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
