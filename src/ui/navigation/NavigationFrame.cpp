// SPDX-License-Identifier: MIT
#include "winui/ui/navigation/NavigationFrame.hpp"

#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace ui
{

std::optional<PageTransition> NavigationFrameControl::effective_transition(const std::optional<PageTransition>& transition) const
{
    // The transition lives in the content transitions. The navigation's own transition wins over the
    // NavigationTransition's default, which wins over the entrance transition.
    const auto* ntt = find_transition<NavigationTransition>(content_transitions_);
    if (ntt == nullptr)
    {
        return std::nullopt;
    }
    if (transition)
    {
        return transition;
    }
    if (ntt->default_transition)
    {
        return ntt->default_transition;
    }
    return PageTransition{RisePageTransition{}};
}

void NavigationFrameControl::settle_leaving()
{
    // Runs first in every navigation: a cached leaving page lives in a stack entry, and clearing
    // the forward stack must not leave leaving_ pointing at it.
    if (leaving_ != nullptr)
    {
        leaving_->run_transition({});
        leaving_->set_hit_test_visible(true);
    }
    leaving_owned_.reset();
    leaving_ = nullptr;
}

bool NavigationFrameControl::navigate(PageFactory page, std::optional<PageTransition> transition, NavigationCacheMode cache)
{
    if (!page)
    {
        return false;
    }
    settle_leaving();
    std::unique_ptr<Element> incoming = page();
    if (!incoming)
    {
        return false;
    }
    Entry* home = nullptr;
    if (content_)
    {
        back_stack_.push_back(std::move(current_));
        home = &back_stack_.back();
    }
    forward_stack_.clear();
    current_ = Entry{std::move(page), transition, cache, nullptr};
    show(std::move(incoming), home, transition, false, NavigationMode::New);
    return true;
}

bool NavigationFrameControl::go_back(std::optional<PageTransition> transition)
{
    if (back_stack_.empty())
    {
        return false;
    }
    settle_leaving();
    Entry previous = std::move(back_stack_.back());
    back_stack_.pop_back();
    std::unique_ptr<Element> incoming = previous.cached ? std::move(previous.cached) : previous.factory();
    if (!incoming)
    {
        return false;
    }
    // go_back() plays the transition the page being left arrived with, backwards.
    const std::optional<PageTransition> used = transition ? transition : current_.transition;
    forward_stack_.push_back(std::move(current_));
    Entry* home = &forward_stack_.back();
    current_    = std::move(previous);
    show(std::move(incoming), home, used, true, NavigationMode::Back);
    return true;
}

bool NavigationFrameControl::go_forward()
{
    if (forward_stack_.empty())
    {
        return false;
    }
    settle_leaving();
    Entry next = std::move(forward_stack_.back());
    forward_stack_.pop_back();
    std::unique_ptr<Element> incoming = next.cached ? std::move(next.cached) : next.factory();
    if (!incoming)
    {
        return false;
    }
    const std::optional<PageTransition> used = next.transition;
    back_stack_.push_back(std::move(current_));
    Entry* home = &back_stack_.back();
    current_    = std::move(next);
    show(std::move(incoming), home, used, false, NavigationMode::Forward);
    return true;
}

void NavigationFrameControl::show(std::unique_ptr<Element>             incoming,
                                  Entry*                               outgoing_home,
                                  const std::optional<PageTransition>& transition,
                                  bool                                 back,
                                  NavigationMode                       mode)
{
    using transition_storyboards::NavigationTrigger;
    const std::optional<PageTransition> used = effective_transition(transition);

    settle_leaving();

    if (content_)
    {
        if (on_navigating_from_)
        {
            on_navigating_from_(NavigatingFromEventArgs{.page = *content_, .mode = mode});
        }
        std::unique_ptr<Element> outgoing = std::move(content_);
        const bool keep = outgoing_home != nullptr && outgoing_home->cache == NavigationCacheMode::Enabled;
        TransitionStoryboard away = used ? transition_storyboards::navigation(*used,
                                                                              back ? NavigationTrigger::BackNavigatingAway
                                                                                   : NavigationTrigger::NavigatingAway)
                                         : TransitionStoryboard{};
        Element* view = outgoing.get();
        if (keep)
        {
            outgoing_home->cached = std::move(outgoing);
        }
        if (!away.empty())
        {
            // Let go of by tick_override() once done, never from the transition's own completion
            // callback, which runs on the page itself.
            view->set_hit_test_visible(false);
            view->run_transition(std::move(away));
            leaving_owned_ = std::move(outgoing); // empty when the stack entry holds it
            leaving_       = view;
        }
        else
        {
            view->run_transition({});
            // Not destroyed here: a button on the page may be what navigated away from it.
            detail::retire(std::move(outgoing));
        }
    }

    content_ = std::move(incoming);
    set_parent_of(*content_, this);
    content_->set_hit_test_visible(true);
    const TransitionStoryboard to = used ? transition_storyboards::navigation(*used,
                                                                              back ? NavigationTrigger::BackNavigatingTo
                                                                                   : NavigationTrigger::NavigatingTo)
                                         : TransitionStoryboard{};
    if (!to.empty())
    {
        content_->set_hit_test_visible(false);
    }
    content_->run_transition(to);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);

    if (on_navigated_)
    {
        on_navigated_(NavigationEventArgs{content_.get(), mode});
    }
}

// ---- layout ----

Size NavigationFrameControl::measure_override(Size available, const MeasureContext& context) const
{
    if (leaving_)
    {
        (void)leaving_->measure(available, context);
    }
    return content_ ? content_->measure(available, context) : Size{};
}

void NavigationFrameControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    arranged_ = final_rect;
    if (leaving_)
    {
        leaving_->arrange(final_rect);
    }
    if (content_)
    {
        content_->arrange(final_rect);
    }
}

void NavigationFrameControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    if (leaving_)
    {
        leaving_->render(renderer, context);
    }
    if (content_)
    {
        content_->render(renderer, context);
    }
}

void NavigationFrameControl::clear_dirty_override(DirtyFlags flags)
{
    if (leaving_)
    {
        clear_dirty_of(*leaving_, flags);
    }
    if (content_)
    {
        clear_dirty_of(*content_, flags);
    }
}

void NavigationFrameControl::tick_override(Duration delta)
{
    if (leaving_ != nullptr)
    {
        leaving_->tick(delta);
        if (!leaving_->transition_running())
        {
            // The exit has played: gone, or left on its stack entry if that one caches it.
            leaving_->set_hit_test_visible(true);
            leaving_owned_.reset();
            leaving_ = nullptr;
            mark_dirty(DirtyFlags::Visual);
        }
    }
    if (content_) // ticked as a child, after this
    {
        if (!content_->is_hit_test_visible() && !content_->transition_running())
        {
            content_->set_hit_test_visible(true);
        }
    }
}

std::unique_ptr<ui::Element> NavigationFrame::build() &&
{
    auto node = std::make_unique<ui::NavigationFrameControl>();
    apply(content_transitions, *node, [raw = node.get()](const TransitionList& v) { raw->set_content_transitions(v); });
    if (on_navigated)
    {
        node->on_navigated(std::move(on_navigated));
    }
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
