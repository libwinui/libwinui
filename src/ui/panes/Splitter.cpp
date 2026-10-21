// SPDX-License-Identifier: MIT
#include "winui/ui/panes/Splitter.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <utility>

#include <cmath>

namespace ui
{

namespace
{
// The separator while hovered or dragged: 2 device pixels, in either orientation.
constexpr float SeparatorThickness = 2.0f;
// A gripper over a column header's gridline needs nothing drawn at rest. A
// standalone Splitter has nothing else marking the boundary, so it draws a 1px divider.
constexpr float RestThickness = 1.0f;
// How many keyboard increments a large step is.
constexpr float LargeIncrementMultiplier = 4.0f;
} // namespace

Size SplitterControl::measure_override(Size available, const MeasureContext&) const
{
    if (vertical())
    {
        return Size{thickness_, std::isfinite(available.height) ? available.height : thickness_};
    }
    return Size{std::isfinite(available.width) ? available.width : thickness_, thickness_};
}

void SplitterControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    // Pressed matches hover on purpose: the pointer is still over the gripper during a
    // drag, and a change mid-gesture reads as a glitch.
    const bool  active    = dragging_ || hovered_;
    const Color color     = active ? theme.colors().selection : theme.colors().divider;
    const float thickness = active ? SeparatorThickness : RestThickness;

    const Rect separator = vertical() ? Rect{bounds().left() + (bounds().width - thickness) * 0.5f,
                                             bounds().top(),
                                             thickness,
                                             bounds().height}
                                      : Rect{bounds().left(),
                                             bounds().top() + (bounds().height - thickness) * 0.5f,
                                             bounds().width,
                                             thickness};
    renderer.fill_rounded_rect(separator, 0.0f, color);
}

void SplitterControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_  = false;
        dragging_ = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

float SplitterControl::move_by(float delta)
{
    if (on_drag_delta_)
    {
        on_drag_delta_(delta);
        return delta;
    }
    if (auto* grid = dynamic_cast<GridControl*>(parent()))
    {
        if (const std::optional<float> applied = grid->resize_beside(this, orientation_, delta))
        {
            return *applied;
        }
    }
    return delta;
}

bool SplitterControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, takes_arrow_key(e.key), false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    // Only the two arrows along the drag axis, on key down, so
    // auto-repeat makes a held arrow a smooth resize.
    if (!focusable() || !is_focused() || e.action != KeyAction::Down)
    {
        return handled;
    }

    float direction = 0.0f;
    switch (e.key)
    {
        case Key::ArrowLeft:
            direction = vertical() ? -1.0f : 0.0f;
            break;
        case Key::ArrowRight:
            direction = vertical() ? 1.0f : 0.0f;
            break;
        case Key::ArrowUp:
            direction = vertical() ? 0.0f : -1.0f;
            break;
        case Key::ArrowDown:
            direction = vertical() ? 0.0f : 1.0f;
            break;
        default:
            return handled;
    }
    if (direction == 0.0f)
    {
        return handled;
    }

    // The increment's std::abs(), falling back to the default for a
    // zero increment instead of doing nothing.
    const float increment = std::abs(keyboard_increment_) > 0.0f ? std::abs(keyboard_increment_) : 8.0f;
    (void)move_by(direction * increment * (e.modifiers.shift ? LargeIncrementMultiplier : 1.0f));
    return handled;
}

bool SplitterControl::pointer_override(const PointerEvent& e)
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
                const float delta = vertical() ? (e.position.x - drag_last_pos_.x) : (e.position.y - drag_last_pos_.y);
                // Only as far as the tracks moved: past a limit the pointer has to come back to
                // where the bar stopped before it moves again.
                const float applied = delta != 0.0f ? move_by(delta) : 0.0f;
                (vertical() ? drag_last_pos_.x : drag_last_pos_.y) += applied;
                (vertical() ? drag_last_pos_.y : drag_last_pos_.x) = vertical() ? e.position.y : e.position.x;
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

std::unique_ptr<ui::Element> Splitter::build() &&
{
    auto node = std::make_unique<ui::SplitterControl>(orientation);
    apply(thickness, *node, [raw = node.get()](const float& v) { raw->set_thickness(v); });
    node->set_focusable(focusable);
    apply(keyboard_increment, *node, [raw = node.get()](const float& v) { raw->set_keyboard_increment(v); });
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
