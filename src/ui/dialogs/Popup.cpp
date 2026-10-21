// SPDX-License-Identifier: MIT
#include "winui/ui/dialogs/Popup.hpp"

#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <utility>
#include <variant>

namespace ui
{

PopupControl::PopupControl()
{
    set_hit_test_visible(false);
}

void PopupControl::set_open(bool open)
{
    if (open_ == open)
    {
        return;
    }
    open_ = open;
    mark_dirty(DirtyFlags::Visual);

    // The child's transitions play as it opens and closes
    // (PopupTransition, EntranceTransition, ContentTransition).
    if (child_)
    {
        if (open_)
        {
            closing_ = false;
            child_->set_hit_test_visible(true);
            bool ran = false;
            for (const Transition& t : child_transitions_)
            {
                if (const auto* popup = std::get_if<PopupTransition>(&t))
                {
                    child_->run_transition(transition_storyboards::popup_in(Point{popup->offset_x, popup->offset_y}));
                    ran = true;
                }
                else if (const auto* entrance = std::get_if<EntranceTransition>(&t))
                {
                    child_->run_transition(transition_storyboards::entrance(Point{entrance->offset_x, entrance->offset_y}));
                    ran = true;
                }
                else if (const auto* content = std::get_if<ContentTransition>(&t))
                {
                    child_->run_transition(transition_storyboards::content_in(Point{content->offset_x, content->offset_y}));
                    ran = true;
                }
                if (ran)
                {
                    break;
                }
            }
            if (!ran)
            {
                child_->run_transition({});
            }
        }
        else
        {
            for (const Transition& t : child_transitions_)
            {
                TransitionStoryboard out;
                if (std::holds_alternative<PopupTransition>(t))
                {
                    out = transition_storyboards::popup_out();
                }
                else if (std::holds_alternative<ContentTransition>(t))
                {
                    out = transition_storyboards::content_out();
                }
                if (!out.empty())
                {
                    // The child stays up, taking no input, until the close has played.
                    closing_ = true;
                    child_->set_hit_test_visible(false);
                    child_->run_transition(std::move(out),
                                           [this]
                                           {
                                               closing_ = false;
                                               if (child_)
                                               {
                                                   child_->set_hit_test_visible(true);
                                               }
                                               mark_dirty(DirtyFlags::Visual);
                                               sync_popup();
                                           });
                    break;
                }
            }
        }
    }

    sync_popup();

    // Both are raised synchronously from the is_open change.
    if (open_ && on_opened_)
    {
        on_opened_();
    }
    else if (!open_ && on_closed_)
    {
        on_closed_();
    }
}

Size PopupControl::measure_override(Size /*available*/, const MeasureContext&) const
{
    return {}; // A popup takes no room wherever it sits
}

void PopupControl::arrange_override(const Rect& /*final_rect*/, const ArrangeContext&)
{
    // bounds() is the Popup's position; the child is placed from it by the popup's place callback.
}

void PopupControl::render_override(Renderer& /*renderer*/, const RenderContext&) const
{
    // Nothing in place. The child draws in Window's popup layer.
}

void PopupControl::clear_dirty_override(DirtyFlags flags)
{
    if (child_)
    {
        clear_dirty_of(*child_, flags);
    }
}

void PopupControl::tick_override(Duration delta)
{
    if (child_ && (open_ || closing_))
    {
        child_->tick(delta);
    }
}

void PopupControl::sync_popup()
{
    if (!(open_ || closing_) || child_ == nullptr)
    {
        close_popup();
        return;
    }
    open_popup({
        .content       = child_.get(),
        .light_dismiss = light_dismiss_,
        .takes_focus   = open_ && light_dismiss_, // only a light-dismiss popup holds focus
        .chrome        = {.shadow = false},       // the child draws whatever chrome it has
        .place =
            [this](Element& content, Size /*client*/, const MeasureContext& context)
        {
            // Left in place even when partly off the window.
            const Size    desired     = content.measure(Size{Unbounded, Unbounded}, context);
            const Vector3 translation = current_translation();
            content.arrange(Rect{bounds().x + horizontal_offset_ + translation.x,
                                 bounds().y + vertical_offset_ + translation.y,
                                 desired.width,
                                 desired.height});
            return true;
        },
        .on_dismiss = [this](PopupCloseReason) { set_open(false); },
    });
}

std::unique_ptr<ui::Element> Popup::build() &&
{
    auto node = std::make_unique<ui::PopupControl>();
    if (content)
    {
        node->set_child(content.take());
    }
    apply(horizontal_offset, *node, [raw = node.get()](const float& value) { raw->set_horizontal_offset(value); });
    apply(vertical_offset, *node, [raw = node.get()](const float& value) { raw->set_vertical_offset(value); });
    apply(light_dismiss, *node, [raw = node.get()](const bool& value) { raw->set_light_dismiss(value); });
    apply(child_transitions, *node, [raw = node.get()](const TransitionList& v) { raw->set_child_transitions(v); });
    apply(is_open, *node, [raw = node.get()](const bool& value) { raw->set_open(value); });

    // A two-way `.is_open` follows the control: the user opening or closing it writes the property.
    const auto is_open_writer = ui::shared_writer(is_open);
    if (auto handler = ui::write_back_value<bool>(is_open_writer, true, std::move(on_opened)))
    {
        node->on_opened(std::move(handler));
    }
    if (auto handler = ui::write_back_value<bool>(is_open_writer, false, std::move(on_closed)))
    {
        node->on_closed(std::move(handler));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
