// SPDX-License-Identifier: MIT
#include "winui/ui/Window.hpp"

#include "../core/Trace.hpp"
#include "../platform/Backend.hpp"
#include "../render/Compositor.hpp"
#include "AnimationsScope.hpp"
#include "DragSession.hpp"
#include "OpenWindows.hpp"
#include "PaintTracker.hpp"
#include "ParallelMeasure.hpp"
#include "WindowFrame.hpp"
#include "WindowHost.hpp"
#include "WindowRegistry.hpp"
#include "menus/MenuInvoke.hpp"
#include "themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/PerfCounters.hpp"
#include "winui/core/Strings.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/OverlayShadow.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/DragDrop.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/WindowOptions.hpp"
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/dialogs/Dialog.hpp"
#include "winui/ui/dialogs/ToolTip.hpp"
#include "winui/ui/hosting/NativeControl.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

// A control's callback can call close_modal() while frames of the tree it would destroy are still
// on the stack, so the reset waits until the outermost dispatch has unwound.
struct Window::DispatchGuard
{
    detail::RetireScope     retired; // first, so what the tree let go of dies after everything below
    Window&                 window;
    detail::AnimationsScope animations;
    explicit DispatchGuard(Window& w) : window(w), animations(w.theme_.animations_enabled)
    {
        ++window.dispatch_depth_;
    }
    ~DispatchGuard()
    {
        if (--window.dispatch_depth_ == 0)
        {
            window.resolve_pending_modal_close();
        }
    }
};

struct Window::AutomationTracker
{
    AutomationEvents* events = nullptr;
    ElementWatch      focus; // the node last reported as focused
    bool              had_focus = false;
    AutomationRole    role      = AutomationRole::Group; // its role, for knowing it again rebuilt
    // Its properties as last reported, by AutomationProperty.
    std::array<AutomationValue, 6> state{};
    // The open popups' content as last seen, for menus opening and closing.
    std::vector<std::unique_ptr<ElementWatch>> popups;
    // The focus node's automation parent and its children (role and name) as last seen.
    ElementWatch                                        parent;
    std::vector<std::pair<AutomationRole, std::string>> children;
};

namespace
{
constexpr std::array<AutomationProperty, 6> TrackedProperties{AutomationProperty::Name,
                                                              AutomationProperty::ToggleState,
                                                              AutomationProperty::ExpandCollapseState,
                                                              AutomationProperty::IsSelected,
                                                              AutomationProperty::Value,
                                                              AutomationProperty::RangeValue};
} // namespace

void Window::set_automation_events(AutomationEvents* events)
{
    if (!automation_)
    {
        automation_ = std::make_unique<AutomationTracker>();
    }
    automation_->events = events;
    automation_->focus.reset(nullptr);
    automation_->had_focus = false;
}

void Window::sync_automation()
{
    if (!automation_ || automation_->events == nullptr || !automation_->events->listening())
    {
        return;
    }
    AutomationTracker& tracker = *automation_;
    AutomationEvents&  events  = *tracker.events;

    // Popups that opened or closed: a menu says so, and the node that lists it changed.
    std::vector<Element*> popups;
    for (const OpenPopup& popup : open_popups_)
    {
        if (popup.alive() && popup.owner != tool_tip_service_.get() && popup.owner->popup_request() != nullptr)
        {
            popups.push_back(popup.content);
        }
    }
    const auto shown = [&tracker](const Element* content)
    { return std::ranges::any_of(tracker.popups, [content](const auto& w) { return w->get() == content; }); };
    for (const auto& watch : tracker.popups)
    {
        Element* content = watch->get();
        if (content != nullptr && std::ranges::find(popups, content) == popups.end())
        {
            if (content->automation_role() == AutomationRole::Menu)
            {
                events.menu_closed(*content);
            }
            if (Element* lister = automation_parent(*content))
            {
                events.structure_changed(*lister);
            }
        }
    }
    for (Element* content : popups)
    {
        if (!shown(content))
        {
            if (Element* lister = automation_parent(*content))
            {
                events.structure_changed(*lister);
            }
            if (content->automation_role() == AutomationRole::Menu)
            {
                events.menu_opened(*content);
            }
        }
    }
    tracker.popups.clear();
    for (Element* content : popups)
    {
        tracker.popups.push_back(std::make_unique<ElementWatch>(content));
    }

    Element* const node = automation_focus(focused());
    if (node != tracker.focus.get())
    {
        // A list that builds its rows again on every layout hands over a new node for the same
        // item: one with the role and name of the node that went is that item, not a move.
        const bool rebuilt = node != nullptr && tracker.had_focus && tracker.focus.get() == nullptr &&
                             node->automation_role() == tracker.role &&
                             automation_value(*node, AutomationProperty::Name) == tracker.state[0];
        tracker.focus.reset(node);
        tracker.had_focus = node != nullptr;
        if (node != nullptr && !rebuilt)
        {
            tracker.role = node->automation_role();
            for (std::size_t i = 0; i < TrackedProperties.size(); ++i)
            {
                tracker.state[i] = automation_value(*node, TrackedProperties[i]);
            }
            events.focus_changed(*node);
        }
    }
    else if (node != nullptr)
    {
        for (std::size_t i = 0; i < TrackedProperties.size(); ++i)
        {
            AutomationValue now = automation_value(*node, TrackedProperties[i]);
            if (now != tracker.state[i])
            {
                AutomationValue before = std::exchange(tracker.state[i], std::move(now));
                events.property_changed(*node, TrackedProperties[i], before, tracker.state[i]);
                if (tracker.focus.get() != node)
                {
                    return; // the handler took the node away
                }
            }
        }
    }
    if (node == nullptr)
    {
        return;
    }

    // The items around the focus node: one added, removed or renamed changes the list a screen
    // reader is reading. Compared by role and name, so a list that builds its rows again is the same.
    Element* const                                      parent = automation_parent(*node);
    std::vector<std::pair<AutomationRole, std::string>> children;
    if (parent != nullptr)
    {
        for (const Element* child : automation_children(*parent))
        {
            children.emplace_back(child->automation_role(), child->automation_name());
        }
    }
    if (parent != nullptr && parent == tracker.parent.get() && children != tracker.children)
    {
        events.structure_changed(*parent);
    }
    tracker.parent.reset(parent);
    tracker.children = std::move(children);
}

namespace
{
// How long the pointer rests on an element before its tip (Element::set_tool_tip()) opens.
constexpr double ToolTipDelayMs = 800.0;

thread_local Window* current_ticking_window = nullptr;

// detail::ticking_window() for the length of one tick().
class TickingWindowScope
{
public:
    explicit TickingWindowScope(Window* window) noexcept : outer_(std::exchange(current_ticking_window, window))
    {
    }
    TickingWindowScope(const TickingWindowScope&)            = delete;
    TickingWindowScope& operator=(const TickingWindowScope&) = delete;
    ~TickingWindowScope()
    {
        current_ticking_window = outer_;
    }

private:
    Window* outer_;
};

// Found the way hit_region() finds its element: automation_children(), topmost first. A path rather
// than parent() links, because a popup's content hangs from its owner, not from its surface.
bool path_to_innermost(Element* e, Point p, std::vector<const Element*>& path)
{
    if (e == nullptr || e->visibility() != Visibility::Visible || !e->is_hit_test_visible() || !e->bounds().contains(p))
    {
        return false;
    }
    path.push_back(e);
    std::vector<Element*> children;
    e->automation_children(children);
    for (auto it = children.rbegin(); it != children.rend(); ++it)
    {
        if (path_to_innermost(*it, p, path))
        {
            return true;
        }
    }
    return true;
}

[[nodiscard]] bool is_ancestor_of(const Element* ancestor, const Element* e)
{
    for (const Element* p = e->parent(); p != nullptr; p = p->parent())
    {
        if (p == ancestor)
        {
            return true;
        }
    }
    return false;
}

// The stop a press at `p` focuses. The ring lists a container before what it holds, so a stop
// inside another one that also covers `p` wins: that is the control under the pointer.
[[nodiscard]] Element* focus_target_at(const std::vector<Element*>& ring, Point p)
{
    Element* target = nullptr;
    for (Element* candidate : ring)
    {
        if (candidate->takes_focus_at(p) && (target == nullptr || is_ancestor_of(target, candidate)))
        {
            target = candidate;
        }
    }
    return target;
}

[[nodiscard]] bool is_arrow(Key key)
{
    return key == Key::ArrowUp || key == Key::ArrowDown || key == Key::ArrowLeft || key == Key::ArrowRight;
}

// Delete and Insert count because StandardCommand::Delete has no modifier. Plain letters, arrows,
// Enter, Space and Tab never do, so typing and navigating stay the focused control's.
[[nodiscard]] bool is_shortcut_key(const KeyEvent& e)
{
    if (e.modifiers.ctrl || e.modifiers.alt)
    {
        return true;
    }
    switch (e.key)
    {
        case Key::Delete:
        case Key::Insert:
        case Key::F1:
        case Key::F2:
        case Key::F3:
        case Key::F4:
        case Key::F5:
        case Key::F6:
        case Key::F7:
        case Key::F8:
        case Key::F9:
        case Key::F10:
        case Key::F11:
        case Key::F12:
            return true;
        default:
            return false;
    }
}

// A candidate has to be strictly in the pressed direction, so a side-by-side neighbor never
// answers an Up or a Down.
[[nodiscard]] bool direction_distance(Key key, const Rect& from, const Rect& to, float& along, float& across)
{
    const bool  vertical = (key == Key::ArrowUp || key == Key::ArrowDown);
    const float delta    = vertical ? to.center().y - from.center().y : to.center().x - from.center().x;
    const float sign     = (key == Key::ArrowDown || key == Key::ArrowRight) ? 1.0f : -1.0f;
    along                = delta * sign;
    if (along <= 0.0f)
    {
        return false;
    }
    // 0 once the two line up across the pressed axis, otherwise the gap between them.
    const float low_from  = vertical ? from.left() : from.top();
    const float high_from = vertical ? from.right() : from.bottom();
    const float low_to    = vertical ? to.left() : to.top();
    const float high_to   = vertical ? to.right() : to.bottom();
    const float overlap   = std::min(high_from, high_to) - std::max(low_from, low_to);
    across                = overlap > 0.0f ? 0.0f : -overlap;
    return true;
}
// Whether `request` differs from a default ThemeRequest, which Theme::light() already answers.
[[nodiscard]] bool theme_request_asks_for_something(const ThemeRequest& request)
{
    return request.mode != ThemeMode::System || request.variant != ThemeVariant::System ||
           request.accent_override.has_value() || request.classic_scheme.has_value() ||
           request.early_scheme.has_value() || request.animations != AnimationPreference::System;
}
} // namespace

// Defined before the constructor, which destroys timers_ and commands_ if it throws.
// One after() or every(). The anchor is a source with one observer, the Subscription handed to the
// app: when that goes the anchor has none left and the timer is dropped at the next tick, and when
// the Window goes first the anchor's destructor leaves the Subscription dead.
struct Window::Timer
{
    class Anchor final : public PropertyBase
    {
    public:
        Anchor() = default;
        using PropertyBase::attach;
        using PropertyBase::observer_count;
    };

    Anchor           anchor;
    double           due      = 0.0; // on timer_clock_ms_
    double           interval = 0.0;
    bool             repeat   = false;
    bool             done     = false;
    bool             owned    = true; // false for post_after(): no Subscription ends it
    Callback<void()> fn;

    [[nodiscard]] bool finished() const
    {
        return done || (owned && anchor.observer_count() == 0);
    }
};

// A command registered with add_command(): alive while its Subscription is.
struct Window::WindowCommand
{
    Timer::Anchor anchor;
    CommandRef    command; // empty once the command was destroyed
};

Window::Window(WindowOptions options) : options_(std::move(options))
{
    // The live half of the options: these are the Window's own properties from here on.
    title_         = options_.title.initial();
    style_         = options_.style;
    backdrop_      = options_.backdrop.initial();
    chrome_        = options_.chrome;
    icon_          = options_.icon.initial();
    theme_request_ = options_.theme.initial();
    taskbar_progress_.set(options_.taskbar_progress.initial());
    // A bound field keeps the window in step for as long as both live.
    if (options_.taskbar_progress.reactive())
    {
        option_bindings_.push_back(
            options_.taskbar_progress.subscribe([this](TaskbarProgress progress) { taskbar_progress_.set(progress); }));
    }
    if (options_.title.reactive())
    {
        option_bindings_.push_back(options_.title.subscribe([this](std::string title) { set_title(std::move(title)); }));
    }
    if (options_.backdrop.reactive())
    {
        option_bindings_.push_back(options_.backdrop.subscribe([this](Backdrop backdrop) { set_backdrop(backdrop); }));
    }
    if (options_.icon.reactive())
    {
        option_bindings_.push_back(
            options_.icon.subscribe([this](std::optional<WindowIcon> icon) { set_icon(std::move(icon)); }));
    }
    if (options_.theme.reactive())
    {
        option_bindings_.push_back(options_.theme.subscribe([this](ThemeRequest request) { set_theme(request); }));
    }
    // Replaced built-in text: everything that shows some lays out and draws again.
    option_bindings_.push_back(strings_changed().observe([this] { theme_changed(); }));
    // A request that asks for anything resolves now, against the default OS state until a host
    // reports the real one, so a window that never gets a host (HeadlessUI) still draws it.
    if (theme_request_asks_for_something(theme_request_))
    {
        theme_ = resolve_theme(theme_request_, os_theme_);
    }
    if (!options_.system_menu.empty())
    {
        set_system_menu(options_.system_menu);
    }
    if (options_.min_size != Size{} || options_.max_size != Size{})
    {
        min_size_ = options_.min_size;
        max_size_ = options_.max_size;
    }
    connected_animations_.set_theme(&theme_);
    if (style_.frame == Frame::Themed)
    {
        sync_frame();
    }
    // The one app-written window property: whatever the app writes goes to the host, which
    // has nothing to refuse. A host installed later pulls the current value itself.
    taskbar_progress_forward_ = taskbar_progress_.observe(
        [this](const TaskbarProgress& progress)
        {
            if (window_host_)
            {
                window_host_->set_taskbar_progress(progress);
            }
        });
    // Last, so a constructor that throws leaves no entry behind.
    serial_ = detail::register_window(this);
}

void Window::request_state(WindowState state)
{
    check_thread();
    if (window_host_)
    {
        window_host_->request_state(state);
        return;
    }
    // No OS window yet: the state it will be shown in.
    options_.initial_state = state;
}

void Window::restore()
{
    if (window_host_)
    {
        window_host_->restore();
        return;
    }
    options_.initial_state = WindowState::Normal;
}

void Window::report_state(WindowState state)
{
    const WindowState before      = state_.get();
    WindowState       restores_to = WindowState::Normal;
    if (state == before)
    {
        restores_to = restores_to_.get();
    }
    else if (state == WindowState::Minimized)
    {
        restores_to = before;
    }
    else if (state == WindowState::Fullscreen)
    {
        // Entered from Minimized, it goes back to where the minimized window would have.
        const WindowState from = before == WindowState::Minimized ? restores_to_.get() : before;
        restores_to            = from == WindowState::Maximized ? WindowState::Maximized : WindowState::Normal;
    }
    report_state(state, restores_to);
}

void Window::toggle_fullscreen()
{
    if (state_.get() == WindowState::Fullscreen)
    {
        restore();
    }
    else
    {
        request_state(WindowState::Fullscreen);
    }
}

Subscription Window::after(Duration delay, Callback<void()> fn)
{
    return add_timer(delay.count(), 0.0, false, std::move(fn));
}

void Window::post_after(Duration delay, Callback<void()> fn)
{
    if (closed_)
    {
        check_thread();
        return; // dropped, as it would be had the window closed after the call
    }
    const Subscription owner = add_timer(delay.count(), 0.0, false, std::move(fn));
    timers_.back()->owned    = false; // keeps running once `owner` goes
}

Subscription Window::every(Duration interval, Callback<void()> fn)
{
    return add_timer(interval.count(), interval.count(), true, std::move(fn));
}

Subscription Window::add_command(const Command& command)
{
    check_thread();
    std::erase_if(commands_, [](const std::unique_ptr<WindowCommand>& c) { return c->anchor.observer_count() == 0; });
    auto entry         = std::make_unique<WindowCommand>();
    entry->command     = command;
    Subscription owner = entry->anchor.attach({});
    commands_.push_back(std::move(entry));
    return owner;
}

Subscription Window::add_timer(double delay_ms, double interval_ms, bool repeat, Callback<void()> fn)
{
    check_thread();
    auto timer         = std::make_unique<Timer>();
    timer->due         = timer_clock_ms_ + (std::max)(0.0, delay_ms);
    timer->interval    = (std::max)(0.0, interval_ms);
    timer->repeat      = repeat;
    timer->fn          = std::move(fn);
    Subscription owner = timer->anchor.attach({});
    timers_.push_back(std::move(timer));
    if (window_host_)
    {
        window_host_->wake(); // the host asks next_wake() after its next tick
    }
    return owner;
}

std::optional<Duration> Window::next_wake() const
{
    std::optional<double> next;
    if (element_wake_ms_)
    {
        next = (std::max)(0.0, *element_wake_ms_ - timer_clock_ms_);
    }
    for (const std::unique_ptr<Timer>& timer : timers_)
    {
        if (!timer->finished())
        {
            const double wait = (std::max)(0.0, timer->due - timer_clock_ms_);
            next              = next ? (std::min)(*next, wait) : wait;
        }
    }
    return next ? std::optional<Duration>{Duration{*next}} : std::nullopt;
}

void Window::run_timers()
{
    // A timer added by one that runs here waits for the next tick. By index and through the
    // pointer: a handler may add timers, which moves the vector but not the timers.
    const std::size_t count = timers_.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        Timer* timer = timers_[i].get();
        if (timer->finished() || timer->due > timer_clock_ms_)
        {
            continue;
        }
        if (timer->repeat)
        {
            timer->due += timer->interval;
            if (timer->due <= timer_clock_ms_) // late: once, then an interval from now
            {
                timer->due = timer_clock_ms_ + timer->interval;
            }
        }
        else
        {
            timer->done = true;
        }
        // Erased only after the loop, so the handler may cancel its own timer.
        timer->fn();
    }
    std::erase_if(timers_, [](const std::unique_ptr<Timer>& timer) { return timer->finished(); });
}

Window::~Window()
{
    // First: from here on WindowRef::post() and on_ui_thread() drop their work instead of reaching
    // into a Window that is being torn down. A post through the registry still in flight finishes
    // before this returns.
    detail::unregister_window(this);
    // A window that dies while shown takes its OS window with it; on_closed() runs first.
    detail::destroy_native_window(*this);
}

void Window::close()
{
    check_thread();
    if (window_host_)
    {
        window_host_->close();
    }
}

void Window::force_close()
{
    check_thread();
    if (window_host_)
    {
        force_close_pending_ = true;
        window_host_->close();
    }
}

std::optional<WindowPlacement> Window::placement() const
{
    return window_host_ ? window_host_->placement() : closed_placement_;
}

void Window::move(Point position)
{
    check_thread();
    if (window_host_)
    {
        window_host_->move(position);
        return;
    }
    // No OS window yet: where it will be created.
    if (std::isfinite(position.x) && std::isfinite(position.y))
    {
        options_.position = position;
    }
}

namespace
{
[[nodiscard]] bool valid_window_size(Size size)
{
    return std::isfinite(size.width) && std::isfinite(size.height) && size.width >= 0.0f && size.height >= 0.0f;
}
} // namespace

void Window::resize(Size size)
{
    check_thread();
    if (!valid_window_size(size))
    {
        return;
    }
    if (window_host_)
    {
        window_host_->resize(size);
        return;
    }
    // No OS window yet: what it will be created with.
    options_.size = size;
}

std::optional<Rect> Window::bounds() const
{
    return window_host_ ? window_host_->bounds() : std::nullopt;
}

std::optional<Rect> Window::work_area() const
{
    return window_host_ ? window_host_->work_area() : std::nullopt;
}

void Window::set_stretch_content(bool stretch)
{
    if (stretch_content_ != stretch)
    {
        stretch_content_ = stretch;
        laid_out_        = false; // the next layout() arranges again at the new rule
    }
}

void Window::set_background(std::optional<Color> background)
{
    // Nothing in the tree changes, so nothing asks for a frame: the host paints it with the next
    // one, and a caller that needs it at once (a hidden window about to be shown) gets one anyway.
    background_ = background;
}

void Window::show()
{
    show_window(true);
}

void Window::show_inactive()
{
    show_window(false);
}

void Window::show_window(bool activate)
{
    check_thread();
    if (window_host_)
    {
        window_host_->set_visible(true, activate);
        return;
    }
    // No OS window yet (never shown, or closed since): the backend creates one, shown.
    detail::create_native_window(*this, true, activate);
}

void Window::handle_opened()
{
    {
        const std::lock_guard lock{posted_mutex_};
        closed_ = false;
    }
    open_ = true;
}

void Window::hide()
{
    check_thread();
    if (window_host_)
    {
        window_host_->set_visible(false, false);
    }
}

void Window::activate()
{
    if (window_host_)
    {
        window_host_->activate();
    }
}

void Window::set_size_limits(Size min, Size max)
{
    min_size_ = min;
    max_size_ = max;
    if (window_host_)
    {
        window_host_->set_size_limits(min_size_, max_size_);
    }
}

void Window::set_title(std::string title)
{
    check_thread();
    title_ = std::move(title);
    if (frame_)
    {
        frame_->mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); // a drawn caption shows it
    }
    if (window_host_)
    {
        window_host_->set_title(title_);
    }
}

void Window::set_style(const WindowStyle& style)
{
    style_      = style;
    host_frame_ = effective_style().frame; // sent below, so sync_frame() need not
    sync_frame();
    if (window_host_)
    {
        window_host_->set_style(effective_style());
    }
}

WindowStyle Window::effective_style() const
{
    WindowStyle style = style_;
    if (style.frame == Frame::Themed)
    {
        style.frame = detail::controls(theme_).window_frame ? Frame::Borderless : Frame::Standard;
    }
    return style;
}

float Window::drawn_frame_border() const
{
    return frame_ ? static_cast<const detail::WindowFrameElement&>(*frame_).border() : 0.0f;
}

Element* Window::drawn_title_bar() const
{
    return frame_ ? static_cast<const detail::WindowFrameElement&>(*frame_).title_bar_node() : nullptr;
}

bool Window::wants_blur_behind() const
{
    return frame_ && static_cast<const detail::WindowFrameElement&>(*frame_).chrome().glass &&
           os_theme_.transparency_enabled && state_.get() != WindowState::Fullscreen;
}

std::span<const std::uint8_t> Window::frame_corner_cut() const
{
    if (!frame_)
    {
        return {};
    }
    const auto& frame = static_cast<const detail::WindowFrameElement&>(*frame_);
    // No border means maximized or full screen, where the window is square. Glass leaves the
    // corners unpainted instead, since a region does not clip the blur.
    const bool glass = blur_behind_ && frame.chrome().glass;
    return frame.border() > 0.0f && !glass ? std::span<const std::uint8_t>{frame.chrome().top_corner_cut}
                                           : std::span<const std::uint8_t>{};
}

void Window::report_blur_behind(bool active)
{
    if (active == blur_behind_)
    {
        return;
    }
    blur_behind_ = active;
    if (frame_)
    {
        frame_->mark_dirty(DirtyFlags::Visual);
    }
}

void Window::report_caption_icon(Bitmap icon)
{
    if (icon == caption_icon_)
    {
        return;
    }
    caption_icon_ = std::move(icon);
    if (frame_)
    {
        frame_->mark_dirty(DirtyFlags::Visual);
    }
}

void Window::sync_frame()
{
    const std::optional<detail::ControlChrome::WindowFrame>& chrome = detail::controls(theme_).window_frame;
    if (style_.frame == Frame::Themed && chrome)
    {
        if (!frame_)
        {
            frame_ = std::make_unique<detail::WindowFrameElement>(*this);
            adopt_root(*frame_);
        }
        static_cast<detail::WindowFrameElement&>(*frame_).set_chrome(*chrome);
    }
    else if (frame_)
    {
        detail::retire(std::move(frame_));
        frame_.reset();
    }
    if (content_)
    {
        content_->mark_dirty(DirtyFlags::Layout); // its area moved
    }
    laid_out_ = false;

    // A theme that gains or loses a frame changes the frame the OS window runs with.
    const Frame host_frame = effective_style().frame;
    if (host_frame != host_frame_)
    {
        host_frame_ = host_frame;
        if (window_host_)
        {
            window_host_->set_style(effective_style());
        }
    }
}

Rect Window::content_area(Size client) const
{
    const Rect whole{0.0f, 0.0f, client.width, client.height};
    return frame_ ? static_cast<const detail::WindowFrameElement&>(*frame_).content_rect(whole) : whole;
}

void Window::set_backdrop(Backdrop backdrop)
{
    backdrop_ = backdrop;
    if (window_host_)
    {
        window_host_->set_backdrop(backdrop_);
    }
}

void Window::set_chrome(const ChromeOverrides& chrome)
{
    chrome_ = chrome;
    if (window_host_)
    {
        window_host_->set_chrome(chrome_);
    }
}

HitRegion Window::hit_region_at(Point client_dips) const
{
    // Whatever floats over the tree is interactive: a dialog or a dropdown drawn across a title bar
    // must not drag the window when clicked.
    if (modal_)
    {
        return HitRegion::Client;
    }
    for (const OpenPopup& popup : open_popups_)
    {
        if (popup.bounds.contains(client_dips))
        {
            return HitRegion::Client;
        }
    }
    // The frame the theme draws: its caption, buttons and border, everywhere outside the content.
    if (frame_ && !content_area(client_size_).contains(client_dips))
    {
        return frame_->hit_region(client_dips);
    }
    if (content_ && content_->bounds().contains(client_dips))
    {
        return content_->hit_region(client_dips);
    }
    return HitRegion::Client;
}

void Window::set_icon(std::optional<WindowIcon> icon)
{
    icon_ = std::move(icon);
    if (window_host_)
    {
        window_host_->set_icon(icon_ ? &*icon_ : nullptr);
    }
}

void Window::set_system_menu(std::vector<MenuItem> items)
{
    // The old items' subscriptions go first: they point into the vector being replaced.
    if (!system_menu_checks_)
    {
        system_menu_checks_ = std::make_unique<detail::MenuChecks>();
    }
    system_menu_checks_->subscriptions.clear();
    system_menu_                 = std::move(items);
    system_menu_checks_->changed = [this]
    {
        if (window_host_)
        {
            window_host_->set_system_menu(system_menu_);
        }
    };
    follow_menu_bindings(system_menu_, *system_menu_checks_);
    if (window_host_)
    {
        window_host_->set_system_menu(system_menu_);
    }
}

void Window::handle_system_menu_item(std::size_t index)
{
    if (index >= system_menu_.size())
    {
        return;
    }
    // invoke_menu_item() copies what the handlers need first, so one that replaces the menu is
    // safe. The host is told before they run, so a check mark has moved by then.
    invoke_menu_item(system_menu_,
                     system_menu_[index],
                     [this]
                     {
                         if (window_host_)
                         {
                             window_host_->set_system_menu(system_menu_);
                         }
                     });
}

void Window::set_taskbar_overlay(std::optional<WindowIcon> icon, std::string description)
{
    taskbar_overlay_             = std::move(icon);
    taskbar_overlay_description_ = std::move(description);
    if (window_host_)
    {
        window_host_->set_taskbar_overlay(taskbar_overlay_ ? &*taskbar_overlay_ : nullptr, taskbar_overlay_description_);
    }
}

void Window::request_attention()
{
    if (window_host_)
    {
        window_host_->request_attention(true);
    }
}

void Window::cancel_attention()
{
    if (window_host_)
    {
        window_host_->request_attention(false);
    }
}

std::string ThumbnailButton::display_tool_tip() const
{
    const std::string& own = tool_tip.initial();
    if (own.empty() && command != nullptr)
    {
        return command->current().description;
    }
    return own;
}

bool ThumbnailButton::is_enabled() const
{
    return enabled.initial() && (command == nullptr || command->current().enabled);
}

namespace
{
// Follows one bound field of a thumbnail button: it becomes a constant holding the property's
// value, replaced on every change, and `changed` re-sends the buttons.
template <typename T>
void follow_thumbnail_field(Binding<T>& field, std::vector<Subscription>& out, const std::function<void()>& changed)
{
    if (!field.reactive())
    {
        return;
    }
    T now = field.current();
    out.push_back(field.subscribe(
        [&field, changed](T value)
        {
            field = Binding<T>{std::move(value)};
            changed();
        }));
    field = Binding<T>{std::move(now)};
}
} // namespace

void Window::set_thumbnail_buttons(std::vector<ThumbnailButton> buttons)
{
    // The old buttons' subscriptions go first: they point into the vector being replaced.
    thumbnail_bindings_.clear();
    thumbnail_buttons_                 = std::move(buttons);
    const std::function<void()> resend = [this]
    {
        if (window_host_)
        {
            window_host_->set_thumbnail_buttons(thumbnail_buttons_);
        }
    };
    for (ThumbnailButton& button : thumbnail_buttons_)
    {
        follow_thumbnail_field(button.tool_tip, thumbnail_bindings_, resend);
        follow_thumbnail_field(button.enabled, thumbnail_bindings_, resend);
        follow_thumbnail_field(button.visible, thumbnail_bindings_, resend);
        if (button.command != nullptr)
        {
            thumbnail_bindings_.push_back(button.command->observe([resend] { resend(); }));
        }
    }
    resend();
}

void Window::handle_thumbnail_button(std::size_t index)
{
    if (index >= thumbnail_buttons_.size() || !thumbnail_buttons_[index].is_enabled())
    {
        return;
    }
    // Copied out before the call: a handler may replace the buttons.
    const std::function<void()> handler   = thumbnail_buttons_[index].on_click;
    const Command* const        command   = thumbnail_buttons_[index].command;
    const std::string           parameter = thumbnail_buttons_[index].command_parameter;
    if (handler)
    {
        handler();
    }
    if (command != nullptr)
    {
        (*command)(parameter);
    }
}

void Window::pin_theme(Theme theme)
{
    check_thread();
    detail::apply_animations_switch(theme);
    theme_pinned_ = true;
    theme_        = std::move(theme);
    ++theme_revision_;
    theme_changed();
    if (theme_changed_handler_)
    {
        theme_changed_handler_(theme_);
    }
}

void Window::theme_changed()
{
    if (style_.frame == Frame::Themed || frame_)
    {
        sync_frame();
    }
    constexpr DirtyFlags flags = DirtyFlags::Layout | DirtyFlags::Visual;
    if (content_)
    {
        content_->mark_subtree_dirty(flags);
    }
    if (modal_)
    {
        modal_->mark_subtree_dirty(flags);
    }
    for (const OpenPopup& popup : open_popups_)
    {
        if (popup.content != nullptr)
        {
            popup.content->mark_subtree_dirty(flags);
        }
    }
}

void Window::layout(Size client, const TextMeasurer& measurer)
{
    LIBWINUI_TRACE_SPAN("layout");
    const detail::AnimationsScope animations{theme_.animations_enabled};
    // Nothing to lay out while iconic: the size a host reports for a minimized window is not one to
    // arrange the tree at.
    if (state_.get() == WindowState::Minimized)
    {
        return;
    }

    // Nothing changed since the last pass: no flag anywhere, the same size and the same roots. The
    // tab ring, the popups and the composition all still stand, so none of their walks is needed.
    const bool anything_dirty = (content_ && content_->is_dirty()) || (frame_ && frame_->is_dirty()) ||
                                (modal_ && modal_->is_dirty()) || (tool_tip_service_ && tool_tip_service_->is_dirty());
    const LayoutSignature signature{content_.get(), frame_.get(), modal_.get(), open_popups_.size(), content_generation_};
    if (laid_out_ && client == client_size_ && !anything_dirty && signature == last_layout_ &&
        focus_order_epoch_ == detail::removed_element_count())
    {
        connected_animations_.after_layout();
        sync_automation();
        return;
    }

    // Focus order is rebuilt below on any change, so a Visual-only change (ButtonControl::set_enabled)
    // needs no re-measure to keep the tab ring right.
    const bool needs_layout = !laid_out_ || !(client == client_size_) ||
                              (content_ && content_->is_dirty(DirtyFlags::Layout)) ||
                              (frame_ && frame_->is_dirty(DirtyFlags::Layout));

    client_size_ = client;
    // Measure and arrange results are reused within the pass whatever the flags say (Element::measure()).
    const detail::LayoutPassScope pass;

    if (needs_layout)
    {
        ++layout_pass_count_;
        detail::count(detail::PerfCounter::LayoutPasses);
        visual_tree_stale_ = true;
        if (frame_)
        {
            (void)frame_->measure(client, measurer, theme_);
            frame_->arrange(Rect{0.0f, 0.0f, client.width, client.height});
        }
        // A change inside the tree that leaves the size of some ancestor as it was is laid out
        // from there: the dirty elements are measured again in their old space, and only those
        // whose size changed take their parents along. A new size, theme or measurer, or a change
        // that reaches the root, is the full pass below, which the per-element caches keep cheap.
        const bool incremental = laid_out_ && client == last_client_ && &measurer == last_measurer_ &&
                                 theme_.generation() == last_theme_generation_ &&
                                 !(frame_ && frame_->is_dirty(DirtyFlags::Layout)) && content_ &&
                                 relayout_from_dirty(*content_, measurer);
        if (content_ && !incremental)
        {
            // Inside the frame the theme draws, if there is one.
            const Rect area    = content_area(client);
            const Size desired = [&]
            {
                const detail::ParallelMeasureScope parallel{parallel_layout_};
                return content_->measure(Size{area.width, area.height}, measurer, theme_);
            }();
            if (stretch_content_)
            {
                content_->arrange(area);
            }
            else
            {
                // Center within the area (rounded to whole pixels for crisp rendering).
                const float x = area.x + std::round((area.width - desired.width) * 0.5f);
                const float y = area.y + std::round((area.height - desired.height) * 0.5f);
                content_->arrange(Rect{x, y, desired.width, desired.height});
            }
        }
        laid_out_              = true;
        last_client_           = client;
        last_measurer_         = &measurer;
        last_theme_generation_ = theme_.generation();
    }
    // Every flag is consumed here, not only on a Layout pass: the host lays out right before it
    // paints, so whatever changed since the last frame is about to be drawn.
    if (content_)
    {
        content_->clear_dirty();
    }
    if (frame_)
    {
        frame_->clear_dirty();
    }

    rebuild_focus_order();
    // Unconditional: opening a ComboBox is a Visual-only change that never trips `needs_layout`,
    // but the popup still needs positioning.
    sync_popups(measurer);
    // show_modal()/close_modal() are Visual-only changes too.
    if (modal_)
    {
        const Size  desired = modal_->measure(client_size_, measurer, theme_);
        const float width   = std::min(desired.width, client_size_.width);
        const float height  = std::min(desired.height, client_size_.height);
        const float x       = std::round((client_size_.width - width) * 0.5f);
        const float y       = std::round((client_size_.height - height) * 0.5f);
        modal_->arrange(Rect{x, y, width, height});
        modal_->clear_dirty();
    }
    // A connected animation started since the last layout snapshots its now laid-out destination.
    connected_animations_.after_layout();
    last_layout_ = LayoutSignature{content_.get(), frame_.get(), modal_.get(), open_popups_.size(), content_generation_};
    sync_automation(); // what the last input, a timer or the app changed
    sync_composition();
    sync_native();
}

namespace
{
// The elements under `e` whose own measure or arrange is out of date, topmost first: one found
// is not looked under, since laying it out lays out what is below it.
void collect_layout_starts(Element& e, std::vector<Element*>& out)
{
    if (!e.is_dirty(DirtyFlags::Layout))
    {
        return;
    }
    if (detail::LayoutAccess::own_layout_invalid(e))
    {
        out.push_back(&e);
        return;
    }
    const std::size_t before = out.size();
    e.for_each_child([&out](Element& child) { collect_layout_starts(child, out); });
    if (out.size() == before)
    {
        // The change is in a part `e` does not list, such as NavigationShell's page (one it clears
        // in its own clear_dirty()): only `e`'s own measure() reaches it.
        detail::LayoutAccess::invalidate_own_measure(e);
        out.push_back(&e);
    }
}
} // namespace

bool Window::relayout_from_dirty(Element& root, const TextMeasurer& measurer)
{
    std::vector<Element*> starts;
    collect_layout_starts(root, starts);
    for (Element* start : starts)
    {
        Element* e = start;
        for (;;)
        {
            if (e == &root || e->parent() == nullptr)
            {
                return false; // reached the root
            }
            const Theme& handed = detail::LayoutAccess::handed_theme(*e, theme_);
            if (!detail::LayoutAccess::measured_with(*e, measurer, handed))
            {
                return false; // never laid out with this measurer, or under a theme of a container's own
            }
            if (detail::LayoutAccess::measure_invalid(*e))
            {
                if (!detail::LayoutAccess::remeasure(*e, measurer, handed))
                {
                    // Its size changed, or its parent measured it twice: the parent decides again.
                    Element* parent = e->parent();
                    detail::LayoutAccess::invalidate_own_measure(*parent);
                    e = parent;
                    continue;
                }
            }
            detail::LayoutAccess::rearrange(*e);
            break;
        }
    }
    return true;
}

const VisualTree& Window::visual_tree() const
{
    if (visual_tree_stale_)
    {
        visual_tree_.clear();
        if (content_)
        {
            detail::count(detail::PerfCounter::VisualTreeBuilds);
            content_->build_visual_tree(visual_tree_, InvalidVisualId);
        }
        visual_tree_stale_ = false;
    }
    return visual_tree_;
}

void Window::render(Renderer& renderer) const
{
    LIBWINUI_TRACE_SPAN("render");
    detail::count(detail::PerfCounter::Frames);
    render_frame(renderer);
}

void Window::render(Renderer& renderer, const Rect& clip) const
{
    LIBWINUI_TRACE_SPAN("render");
    detail::count(detail::PerfCounter::Frames);
    renderer.push_clip(clip);
    {
        detail::PaintTracker culling{renderer, paint_frame_, clip};
        render_frame(culling);
    }
    renderer.pop_clip();
}

namespace
{
// The damage rects merged: overlapping ones become one, and more than a few become their union.
void merge_damage(std::vector<Rect>& rects)
{
    std::erase_if(rects, [](const Rect& r) { return r.width <= 0.0f || r.height <= 0.0f; });
    for (bool merged = true; merged;)
    {
        merged = false;
        for (std::size_t i = 0; i < rects.size() && !merged; ++i)
        {
            for (std::size_t j = i + 1; j < rects.size(); ++j)
            {
                if (rects[i].intersects(rects[j]))
                {
                    rects[i] = detail::united(rects[i], rects[j]);
                    rects.erase(rects.begin() + static_cast<std::ptrdiff_t>(j));
                    merged = true;
                    break;
                }
            }
        }
    }
    if (rects.size() > 4)
    {
        Rect all{};
        for (const Rect& r : rects)
        {
            all = detail::united(all, r);
        }
        rects.assign(1, all);
    }
}
} // namespace

void detail::PaintScratchDelete::operator()(PaintScratch* scratch) const noexcept
{
    delete scratch;
}

std::vector<Rect> Window::take_damage()
{
    LIBWINUI_TRACE_SPAN("damage");
    const Rect client{0.0f, 0.0f, client_size_.width, client_size_.height};
    // Draw the frame into a tracker that only measures: every element compares what it draws
    // with what it drew in the last frame (PaintTracker.hpp).
    if (!paint_scratch_)
    {
        paint_scratch_.reset(new detail::PaintScratch);
    }
    std::vector<Rect>& rects = paint_scratch_->damage;
    ++paint_frame_;
    {
        detail::PaintTracker measuring{paint_frame_, *paint_scratch_};
        render_frame(measuring);
        // What the window draws outside every element: popup shadows, the scrim, a drag visual.
        if (measuring.window_hash() != painted_window_hash_)
        {
            rects.push_back(detail::united(painted_window_area_, measuring.window_area()));
        }
        painted_window_hash_ = measuring.window_hash();
        painted_window_area_ = measuring.window_area();

        std::vector<std::pair<const Element*, Rect>> roots;
        const auto                                   drawn = [&roots, this](const Element* root)
        {
            if (root != nullptr && root->painted_frame_ == paint_frame_)
            {
                roots.emplace_back(root, root->painted_area_);
            }
        };
        drawn(frame_.get());
        drawn(content_.get());
        for (const OpenPopup& popup : open_popups_)
        {
            drawn(popup.alive() ? popup.content : nullptr);
        }
        drawn(modal_.get());
        for (const auto& [root, area] : painted_roots_)
        {
            const bool still = std::ranges::any_of(roots, [root](const auto& r) { return r.first == root; });
            if (!still)
            {
                rects.push_back(area);
            }
        }
        painted_roots_ = std::move(roots);
        // Acrylic shows what is under it: a change anywhere it reads repaints it all, with
        // everything under it, since the pixels kept from the last frame are the acrylic's own.
        for (bool grew = true; grew;)
        {
            grew = false;
            for (const Rect& acrylic : measuring.acrylic_areas())
            {
                const bool touched = std::any_of(rects.begin(),
                                                 rects.end(),
                                                 [&acrylic](const Rect& r) { return r.intersects(acrylic); });
                const bool covered = std::any_of(rects.begin(),
                                                 rects.end(),
                                                 [&acrylic](const Rect& r)
                                                 {
                                                     return r.left() <= acrylic.left() && r.top() <= acrylic.top() &&
                                                            r.right() >= acrylic.right() && r.bottom() >= acrylic.bottom();
                                                 });
                if (touched && !covered)
                {
                    rects.push_back(acrylic);
                    grew = true;
                }
            }
        }
    }
    sync_layers(rects);
    sync_covered_visuals(rects);
    sync_screen();
    const bool full = !measured_once_ || !(client_size_ == measured_client_) || !(background() == measured_background_);
    measured_once_  = true;
    measured_client_     = client_size_;
    measured_background_ = background();
    if (full)
    {
        return {client};
    }
    if (rects.empty())
    {
        return {};
    }
    for (Rect& r : rects)
    {
        r = intersection(r, client);
    }
    merge_damage(rects);
    // Most of the window anyway: one full frame is cheaper than several clipped ones.
    float area = 0.0f;
    for (const Rect& r : rects)
    {
        area += r.width * r.height;
    }
    if (area > client.width * client.height * 0.6f)
    {
        return {client};
    }
    return rects;
}

namespace
{
// The compositor places a layer, it does not scale or turn it.
bool translation_only(const Matrix& m)
{
    return m.m11 == 1.0f && m.m12 == 0.0f && m.m21 == 0.0f && m.m22 == 1.0f;
}

Rect moved(const Rect& r, const Matrix& m)
{
    return Rect{r.x + m.dx, r.y + m.dy, r.width, r.height};
}

void remove_layers(detail::Compositor& compositor, std::span<const VisualId> ids, std::size_t from = 0)
{
    for (std::size_t i = from; i < ids.size(); ++i)
    {
        compositor.remove_layer(ids[i]);
    }
}
} // namespace

void Window::sync_screen()
{
    detail::PaintScratch& s = *paint_scratch_;
    if (s.screen.empty() && s.on_screen.empty())
    {
        return;
    }
    const Rect client{0.0f, 0.0f, client_size_.width, client_size_.height};
    // Every state is settled before any element hears of it: a handler may change the tree.
    (void)detail::take_wake_request();
    std::vector<std::pair<std::unique_ptr<ElementWatch>, bool>> changed;
    std::vector<std::unique_ptr<ElementWatch>>                  on_screen;
    for (const Element* drawn : s.screen)
    {
        auto&      e  = const_cast<Element&>(*drawn); // the window's own tree, drawn through const
        const bool on = !intersection(e.painted_area_, client).empty();
        if (on)
        {
            on_screen.push_back(std::make_unique<ElementWatch>(&e));
        }
        if (e.on_screen_ != on)
        {
            e.on_screen_ = on;
            changed.emplace_back(std::make_unique<ElementWatch>(&e), on);
        }
    }
    // On screen in the last frame and not drawn in this one: removed, collapsed, or inside
    // something that skipped it.
    for (const std::unique_ptr<ElementWatch>& watch : s.on_screen)
    {
        Element* e = watch->get();
        if (e != nullptr && e->tracks_screen_ && e->on_screen_ && e->painted_frame_ != paint_frame_)
        {
            e->on_screen_ = false;
            changed.emplace_back(std::make_unique<ElementWatch>(e), false);
        }
    }
    s.on_screen = std::move(on_screen);
    for (const auto& [watch, on] : changed)
    {
        if (Element* e = watch->get())
        {
            e->on_screen_changed(on);
        }
    }
    // A wake an element asked for on coming back: the tick after this frame asks it again.
    if (const std::optional<double> wait = detail::take_wake_request())
    {
        const double due = timer_clock_ms_ + *wait;
        element_wake_ms_ = element_wake_ms_ ? (std::min)(*element_wake_ms_, due) : due;
    }
}

void Window::sync_layers(std::vector<Rect>& damage)
{
    detail::PaintScratch& s = *paint_scratch_;
    // A new compositor knows nothing of the old one's layers, and the old one may be gone.
    if (s.compositor != compositor_)
    {
        for (detail::ComposedLayers& run : s.composed)
        {
            if (Element* e = run.element->get())
            {
                e->layers_stopped(Duration{});
                e->layers_composed_ = false;
                e->mark_dirty(DirtyFlags::Visual);
            }
        }
        s.composed.clear();
        s.compositor = compositor_;
    }
    if (s.layers.empty() && s.composed.empty())
    {
        return;
    }
    detail::Compositor* const compositor = compositor_ != nullptr && theme_.animations_enabled &&
                                                   compositor_->supports_layers()
                                               ? compositor_
                                               : nullptr;
    const Rect                client{0.0f, 0.0f, client_size_.width, client_size_.height};
    const auto                elapsed_of = [this](const detail::ComposedLayers& run)
    { return compositor_ != nullptr ? compositor_->now() - run.since : Duration{}; };

    for (detail::LayerProbe& probe : s.layers)
    {
        auto& e   = const_cast<Element&>(*probe.element); // the window's own tree, drawn through const
        auto  run = std::find_if(s.composed.begin(),
                                 s.composed.end(),
                                 [&e](const detail::ComposedLayers& r) { return r.key == e.visual_id(); });
        // Handing the drawing between the frame and the compositor repaints what both drew.
        const auto hand_over = [&](bool composed)
        {
            if (e.layers_composed_ != composed)
            {
                e.layers_composed_ = composed;
                damage.push_back(detail::united(e.painted_own_, probe.area));
            }
        };
        const bool can = compositor != nullptr && !probe.layers.empty() && translation_only(probe.transform) &&
                         probe.opacity >= 1.0f && !probe.occluded;
        if (!can)
        {
            if (run != s.composed.end())
            {
                if (run->shown && compositor_ != nullptr)
                {
                    remove_layers(*compositor_, run->ids);
                }
                e.layers_stopped(elapsed_of(*run));
                s.composed.erase(run);
            }
            hand_over(false);
            continue;
        }
        if (run == s.composed.end())
        {
            detail::ComposedLayers fresh;
            fresh.element = std::make_unique<ElementWatch>(&e);
            fresh.key     = e.visual_id();
            fresh.since   = compositor->now();
            s.composed.push_back(std::move(fresh));
            run = std::prev(s.composed.end());
        }
        run->frame = paint_frame_;
        hand_over(true);

        // Scrolled out of view or clipped away: nothing runs on the compositor, and the element
        // does not tick for it either.
        if (intersection(probe.area, client).empty())
        {
            if (run->shown)
            {
                remove_layers(*compositor, run->ids);
                run->shown = false;
            }
            continue;
        }

        const std::size_t count   = probe.layers.size();
        const bool        shown   = run->shown;
        bool              restart = !shown || run->ids.size() != count;
        if (shown && run->ids.size() > count)
        {
            remove_layers(*compositor, run->ids, count);
        }
        run->ids.resize(count, InvalidVisualId);
        run->hashes.resize(count, 0);
        run->placements.resize(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const detail::ProbedLayer& layer = probe.layers[i];
            const bool                 fresh = !shown || run->ids[i] == InvalidVisualId;
            if (run->ids[i] == InvalidVisualId)
            {
                run->ids[i] = allocate_visual_id();
            }
            Rect clip = layer.placement.clip ? moved(*layer.placement.clip, probe.transform) : client;
            clip      = intersection(intersection(clip, probe.clip.value_or(client)), client);
            const detail::LayerPlacement placement{moved(layer.placement.bounds, probe.transform), clip};
            if (fresh || !(placement == run->placements[i]))
            {
                // The animations' values are worked out from the layer's size.
                if (!fresh && !(placement.bounds.size() == run->placements[i].bounds.size()))
                {
                    restart = true;
                }
                compositor->sync_layer(run->ids[i], placement);
                run->placements[i] = placement;
            }
            if (fresh || layer.hash != run->hashes[i])
            {
                compositor->paint_layer(run->ids[i], *layer.content);
                run->hashes[i] = layer.hash;
            }
        }
        run->shown = true;
        if (restart)
        {
            e.animate_layers(*compositor, run->ids, elapsed_of(*run));
        }
    }

    // Layers of elements this frame did not draw: removed, collapsed, or inside something that
    // skipped them. Such an element keeps layers_composed(), so it stays quiet until drawn again.
    for (auto run = s.composed.begin(); run != s.composed.end();)
    {
        if (run->frame == paint_frame_)
        {
            ++run;
            continue;
        }
        if (run->shown && compositor_ != nullptr)
        {
            remove_layers(*compositor_, run->ids);
        }
        if (Element* e = run->element->get())
        {
            e->layers_stopped(elapsed_of(*run));
        }
        run = s.composed.erase(run);
    }
}

void Window::sync_covered_visuals(std::vector<Rect>& damage)
{
    // An element's compositor visuals sit at its bounds above the whole frame, so anything that
    // hides part of the element in the frame would show under them. Such an element drops them and
    // draws what they showed itself; the switch repaints it.
    for (const detail::LayerProbe& probe : paint_scratch_->visuals)
    {
        auto&      e       = const_cast<Element&>(*probe.element); // the window's own tree, drawn through const
        const bool clipped = probe.clip && !(intersection(probe.area, *probe.clip) == probe.area);
        const bool covered = probe.occluded || clipped || !probe.transform.is_identity() || probe.opacity < 1.0f;
        if (covered == e.visuals_covered_)
        {
            continue;
        }
        e.visuals_covered_ = covered;
        damage.push_back(detail::united(e.painted_own_, probe.area));
        if (compositor_ != nullptr)
        {
            const detail::AnimationsScope animations{theme_.animations_enabled};
            e.sync_composition_override(*compositor_, theme_);
        }
    }

    // An element this frame did not draw (its parent closed or collapsed it, or let it go) is
    // never asked for its visuals again until it is drawn, so it is told they are covered now: a
    // hover wash would stay above the frame where the element was. One that is gone loses the
    // visual under its own key.
    detail::PaintScratch& s = *paint_scratch_;
    for (const detail::LayerProbe& probe : s.visuals)
    {
        auto& e = const_cast<Element&>(*probe.element);
        if (!e.visuals_listed_ && !e.visuals_covered_)
        {
            e.visuals_listed_ = true;
            s.showing.push_back({std::make_unique<ElementWatch>(&e), e.visual_id()});
        }
    }
    std::erase_if(s.showing,
                  [this](const detail::ComposedVisuals& shown)
                  {
                      Element* const e = shown.element->get();
                      if (e == nullptr)
                      {
                          if (compositor_ != nullptr)
                          {
                              compositor_->remove_visual(shown.key);
                          }
                          return true;
                      }
                      if (e->painted_frame_ == paint_frame_ && !e->visuals_covered_)
                      {
                          return false;
                      }
                      e->visuals_listed_ = false;
                      if (!e->visuals_covered_)
                      {
                          e->visuals_covered_ = true;
                          if (compositor_ != nullptr)
                          {
                              const detail::AnimationsScope animations{theme_.animations_enabled};
                              e->sync_composition_override(*compositor_, theme_);
                          }
                      }
                      return true;
                  });
}

void Window::render_frame(Renderer& renderer) const
{
    const detail::AnimationsScope animations{theme_.animations_enabled};
    renderer.set_rasterization(theme_.rasterization);
    // Each root's focus ring goes over everything in that root (Element::draws_focus_ring()).
    const auto render_root = [&renderer, this](const Element& root)
    {
        const detail::FocusRingPass pass;
        root.render(renderer, theme_);
        auto* const measuring = dynamic_cast<detail::PaintTracker*>(&renderer);
        if (measuring != nullptr)
        {
            measuring->set_covering(false);
        }
        detail::render_focus_rings(renderer, root, theme_, pass);
        if (measuring != nullptr)
        {
            measuring->set_covering(true);
        }
    };
    if (frame_)
    {
        render_root(*frame_);
    }
    if (content_)
    {
        // Inside a themed frame the content stays within it, as a native client area does, so
        // content larger than the window never paints over the border or the caption.
        if (frame_)
        {
            renderer.push_clip(static_cast<const detail::WindowFrameElement&>(*frame_).content_rect(frame_->bounds()));
            render_root(*content_);
            renderer.pop_clip();
        }
        else
        {
            render_root(*content_);
        }
    }
    // Connected animations fly over the tree, under the popups.
    connected_animations_.render(renderer);
    // Popups draw on top of the whole tree, inside the owner's transition_opacity(), so an owner's
    // own open/close fade (Flyout) covers its shadow and content together. One opened from inside
    // the modal, or from a popup that was, draws over the modal instead.
    const auto in_modal = [this](const OpenPopup& popup)
    {
        for (const OpenPopup* p = &popup; p != nullptr;
             p                  = p->parent >= 0 ? &open_popups_[static_cast<std::size_t>(p->parent)] : nullptr)
        {
            if (!p->alive())
            {
                return false;
            }
            const Element* root = p->owner;
            while (root->parent() != nullptr)
            {
                root = root->parent();
            }
            if (root == modal_.get())
            {
                return true;
            }
        }
        return false;
    };
    const auto render_popup = [&](const OpenPopup& popup)
    {
        PopupRequest* request = popup.owner->popup_request();
        if (request == nullptr)
        {
            return; // closed since the last layout()
        }
        // Inside a drawn frame a popup stays within the content, as the content does, so one
        // left partly off the content never paints over the caption or the border.
        if (frame_)
        {
            renderer.push_clip(content_area(client_size_));
        }
        renderer.push_opacity_layer(popup.owner->transition_opacity());
        // The request's radius, not always `overlay_corner_radius`: a ToolTip uses
        // `corner_radius`, and a shadow at the wrong radius shows its corners past the body's curve.
        PopupChrome& chrome        = request->chrome;
        const float  shadow_radius = chrome.corner_radius ? chrome.corner_radius(theme_) : theme_.overlay_corner_radius;
        const Rect   whole         = popup.content->bounds();
        const Rect   shadow_rect   = chrome.shadow_rect ? chrome.shadow_rect(whole) : whole;
        if (!chrome.shadow)
        {
            // A bare Popup: its child draws whatever chrome it has.
        }
        else if (theme_.popup_shadow == ThemePopupShadow::AeroDropShadow)
        {
            draw_aero_popup_shadow(renderer, shadow_rect, shadow_radius);
        }
        else if (theme_.popup_shadow != ThemePopupShadow::None)
        {
            draw_overlay_shadow(renderer, shadow_rect, shadow_radius, theme_.dark, chrome.elevation.value_or(elevation_z::Flyout));
        }
        render_root(*popup.content);
        renderer.pop_opacity_layer();
        if (frame_)
        {
            renderer.pop_clip();
        }
    };
    for (const OpenPopup& popup : open_popups_)
    {
        if (popup.alive() && (!modal_ || !in_modal(popup))) // a dead one was destroyed since the last layout()
        {
            render_popup(popup);
        }
    }
    // The modal draws over everything behind a full-client scrim, Fluent's Smoke layer. One opacity
    // layer covers scrim and modal, so the smoke fades in with the dialog.
    if (modal_)
    {
        renderer.push_opacity_layer(modal_->transition_opacity());
        renderer.fill_rounded_rect(Rect{0.0f, 0.0f, client_size_.width, client_size_.height}, 0.0f, theme_.colors().scrim);
        render_root(*modal_);
        renderer.pop_opacity_layer();
        for (const OpenPopup& popup : open_popups_)
        {
            if (popup.alive() && in_modal(popup))
            {
                render_popup(popup);
            }
        }
    }
    // What a drag carries rides under the pointer, over everything, at 0.8 opacity.
    if (drag_)
    {
        renderer.push_opacity_layer(0.8f);
        renderer.push_transform(1.0f,
                                Point{drag_->position.x - drag_->grab.x - drag_->visual_bounds.x,
                                      drag_->position.y - drag_->grab.y - drag_->visual_bounds.y});
        drag_->visual.replay(renderer);
        renderer.pop_transform();
        renderer.pop_opacity_layer();
        if (!drag_->caption.empty() && !drag_->external)
        {
            // The drag's caption: beside the pointer, on the tooltip surface.
            TextStyle  style = theme_.text_style;
            const Size size{static_cast<float>(drag_->caption.size()) * 7.0f + 16.0f, 24.0f};
            const Rect box{drag_->position.x + 16.0f, drag_->position.y + 16.0f, size.width, size.height};
            renderer.fill_rounded_rect(box, detail::tooltip_radius(theme_), theme_.surfaces.tooltip);
            renderer.draw_text(drag_->caption,
                               box.inset(Symmetric{.horizontal = 8.0f, .vertical = 2.0f}),
                               theme_.surfaces.tooltip_text,
                               style);
        }
    }
}

bool Window::handle_close_request()
{
    if (std::exchange(force_close_pending_, false))
    {
        return true;
    }
    return !closing_handler_ || !detail::canceled(closing_handler_(WindowClosingArgs{}));
}

void Window::handle_closed()
{
    if (window_host_)
    {
        closed_placement_ = window_host_->placement();
    }
    force_close_pending_ = false;
    open_                = false;
    if (closed_handler_)
    {
        closed_handler_();
    }
    // post_after() calls end with the window; run_timers() erases them. after() and every() timers
    // belong to their Subscriptions.
    for (const std::unique_ptr<Timer>& timer : timers_)
    {
        if (!timer->owned)
        {
            timer->done = true;
        }
    }
    // After the handler, so a worker it stops can still post on its way out; nothing will run it.
    // Destroyed outside the lock: a coroutine waiting in there (on_ui_thread()) is destroyed with
    // it, and its destructors may post.
    std::vector<Callback<void()>> dropped;
    {
        const std::lock_guard lock{posted_mutex_};
        closed_ = true;
        dropped.swap(posted_);
    }
}

void Window::post(Callback<void()> work)
{
    // Refused work stays in `work`, destroyed after enqueue() released the lock, see handle_closed().
    (void)enqueue(work);
}

bool Window::enqueue(Callback<void()>& work)
{
    const std::lock_guard lock{posted_mutex_};
    if (closed_)
    {
        return false;
    }
    posted_.push_back(std::move(work));
    if (window_host_)
    {
        window_host_->wake();
    }
    return true;
}

bool Window::tool_tip_open() const
{
    return tool_tip_service_ && static_cast<const ToolTipControl*>(tool_tip_service_.get())->is_open();
}

void Window::hide_tool_tip()
{
    tool_tip_target_  = nullptr;
    tool_tip_elapsed_ = 0.0;
    if (tool_tip_open())
    {
        static_cast<ToolTipControl*>(tool_tip_service_.get())->close();
        std::erase_if(open_popups_, [this](const OpenPopup& p) { return p.owner == tool_tip_service_.get(); });
        tool_tip_changed_ = true;
    }
}

void Window::update_tool_tip(Point position)
{
    // The topmost element under the pointer that has a tip. Only elements inside an open popup
    // count while the pointer is over one, and only the modal's while a dialog is up.
    std::vector<Element*> owners;
    std::vector<Element*> parents;
    collect_live_popup_owners(owners, &parents);
    bool over_popup = false;
    for (const OpenPopup& popup : open_popups_)
    {
        if (popup.owner != tool_tip_service_.get() && popup.bounds.contains(position))
        {
            over_popup = true;
        }
    }
    auto inside_modal = [this](const Element* e)
    {
        for (; e != nullptr; e = e->parent())
        {
            if (e == modal_.get())
            {
                return true;
            }
        }
        return false;
    };

    Element*    target = nullptr;
    std::string text;
    for (std::size_t i = owners.size(); i-- > 0;)
    {
        Element* e = owners[i];
        if ((over_popup && parents[i] == nullptr) || (modal_ && parents[i] == nullptr && !inside_modal(e)))
        {
            continue;
        }
        if (!e->is_effectively_visible() || !e->bounds().contains(position))
        {
            continue;
        }
        text = e->tool_tip_at(position);
        if (!text.empty())
        {
            target = e;
            break;
        }
    }

    if (target != tool_tip_suppressed_)
    {
        tool_tip_suppressed_ = nullptr;
    }
    if (target == nullptr || target == tool_tip_suppressed_)
    {
        hide_tool_tip();
        return;
    }
    if (target == tool_tip_target_ && text == tool_tip_text_)
    {
        if (!tool_tip_open())
        {
            tool_tip_point_ = position; // a tip opens where the pointer came to rest
        }
        tool_tip_text_ = std::move(text);
        return;
    }
    hide_tool_tip();
    tool_tip_target_ = target;
    tool_tip_text_   = std::move(text);
    tool_tip_point_  = position;
}

bool Window::tick(Duration delta)
{
    check_thread();
    LIBWINUI_TRACE_SPAN("tick");
    detail::count(detail::PerfCounter::Ticks);
    DispatchGuard            guard{*this};
    const TickingWindowScope ticking{this};
    drop_dead_popups();
    // Before the posted work runs: a timer it starts counts from now, not from the last tick,
    // which may have been long ago on an idle window.
    timer_clock_ms_ += delta.count();
    // Only what the elements ask for in this tick counts, not a stray request from outside one.
    (void)detail::take_wake_request();
    // Busy is a change anything but a timed step made. A timed step is what an element's tick_override()
    // changes in the tick its Element::request_tick_in() wake fell due, when it asks for the next
    // one there too; an animation that also waits for something later is busy in every other
    // tick. Dirt the last tick's timed steps left undrawn does not count, unless something
    // marked since. The slack covers rounding between the element's clock and this one.
    const auto tree_dirty = [this]
    { return (content_ && content_->is_dirty()) || (modal_ && modal_->is_dirty()) || (frame_ && frame_->is_dirty()); };
    const bool          wake_due       = element_wake_ms_ && timer_clock_ms_ + 1.0 >= *element_wake_ms_;
    const std::uint64_t marks_at_start = detail::dirty_marks();
    bool                busy_change    = tree_dirty() && (!last_tick_quiet_ || marks_at_start != last_tick_marks_);
    (void)detail::take_busy_tick();

    // Taken out under the lock and run outside it, so work that posts more work queues it for
    // the next tick rather than deadlocking or looping here.
    std::vector<Callback<void()>> posted;
    {
        const std::lock_guard lock{posted_mutex_};
        posted.swap(posted_);
    }
    for (Callback<void()>& work : posted)
    {
        work();
    }
    run_timers();
    if (tick_handler_)
    {
        tick_handler_(delta);
    }
    if (drag_ && drag_detail::take_cancel_request())
    {
        end_drag(DataPackageOperation::None);
    }

    // Posted work, timers and the tick hook are never a timed step.
    const std::uint64_t marks_before_elements = detail::dirty_marks();
    busy_change                               = busy_change || marks_before_elements != marks_at_start;
    if (frame_)
    {
        frame_->tick(delta);
    }
    if (content_)
    {
        content_->tick(delta);
    }
    // modal_ sits outside content_, so it needs its own tick_override().
    if (modal_)
    {
        modal_->tick(delta);
    }
    const bool elements_busy = detail::take_busy_tick();
    busy_change = busy_change || (wake_due ? elements_busy : detail::dirty_marks() != marks_before_elements);
    // A tip set on an element (Element::set_tool_tip()) opens once the pointer has rested on it for
    // 800 ms.
    if (tool_tip_target_ != nullptr && !tool_tip_open())
    {
        tool_tip_elapsed_ += delta.count();
        if (tool_tip_elapsed_ >= ToolTipDelayMs)
        {
            if (!tool_tip_service_)
            {
                auto tip  = std::make_unique<ToolTipControl>();
                auto text = std::make_unique<TextControl>();
                text->set_wrapping(TextWrapping::Wrap);
                tip->set_content(std::move(text));
                tool_tip_service_ = std::move(tip);
            }
            auto* tip = static_cast<ToolTipControl*>(tool_tip_service_.get());
            static_cast<TextControl*>(tip->content())->set_text(tool_tip_text_);
            tip->show_at(tool_tip_point_);
            tool_tip_changed_ = true;
        }
    }
    const bool tip_changed = std::exchange(tool_tip_changed_, false);
    const bool connected   = connected_animations_.tick(delta);
    const bool repaint     = tip_changed || connected || tree_dirty();
    busy_change            = busy_change || tip_changed || connected;
    bool posted_more       = false;
    {
        const std::lock_guard lock{posted_mutex_};
        posted_more = !posted_.empty();
    }
    // A change nobody can see (the window hidden or minimized) waits for the window to be shown,
    // which repaints it anyway.
    const bool shown = visible_.get() && state_.get() != WindowState::Minimized;
    // Asked for in this tick, by elements that tick again in the next one anyway: what an earlier
    // tick or frame asked for is spent.
    element_wake_ms_.reset();
    std::optional<double> wait = detail::take_wake_request();
    // A tip waiting out its delay wakes the window once, when the delay is up.
    if (tool_tip_target_ != nullptr && !tool_tip_open())
    {
        const double tip_wait = ToolTipDelayMs - tool_tip_elapsed_;
        wait                  = wait ? (std::min)(*wait, tip_wait) : tip_wait;
    }
    if (wait && shown)
    {
        element_wake_ms_ = timer_clock_ms_ + (std::max)(0.0, *wait);
    }
    const bool busy_repaint = repaint && busy_change;
    last_tick_quiet_        = !busy_repaint;
    last_tick_marks_        = detail::dirty_marks();
    wants_ticks_ = (busy_repaint && shown) || detail::take_tick_request() || connected_animations_.active() || drag_ ||
                   tick_handler_ || posted_more;
    return repaint;
}

void Window::handle_pointer(const PointerEvent& e)
{
    check_thread();
    DispatchGuard guard{*this};
    drop_dead_popups();
    ensure_focus_order();
    if (!content_ && !modal_ && !frame_) // a modal or a drawn frame on an empty window still takes it
    {
        return;
    }
    // The pressed element captures the pointer, and its cursor with it, until the release.
    if (e.button == PointerButton::Left && e.action == PointerAction::Down)
    {
        pressed_cursor_ = resolve_cursor(e.position);
    }
    else if (e.button == PointerButton::Left && e.action == PointerAction::Up)
    {
        pressed_cursor_.reset();
    }
    // A drag in progress has the pointer to itself (OLE's drag loop captures it): the tree sees
    // nothing until it is dropped.
    if (drag_)
    {
        drag_pointer(e);
        return;
    }

    // Modality flips to pointer before the click-focus logic below, so a control this Down focuses
    // is stamped ring-less from the start.
    if (e.action == PointerAction::Down)
    {
        note_pointer_activity();
    }
    // Tips set on elements: a move retargets; a click, the wheel or leaving the window closes the
    // open one. A click blocks a reopen until the pointer moves off.
    if (e.action == PointerAction::Move)
    {
        update_tool_tip(e.position);
    }
    else if (e.action == PointerAction::Down)
    {
        Element* const clicked = tool_tip_target_;
        hide_tool_tip();
        tool_tip_suppressed_ = clicked;
    }
    else if (e.action == PointerAction::Leave || e.action == PointerAction::Wheel)
    {
        hide_tool_tip();
        if (e.action == PointerAction::Leave)
        {
            tool_tip_suppressed_ = nullptr;
        }
    }

    // A modal swallows every input event: content_ and the open popups never see it.
    if (modal_)
    {
        if (e.action == PointerAction::Down)
        {
            leave_scopes_outside(e.position);
            if (Element* target = focus_target_at(focus_order_, e.position)) // already modal-scoped
            {
                set_focus(target);
            }
        }
        modal_->handle_pointer(e);
        sync_composition();
        sync_native();
        return;
    }

    // A Down/Up inside an open popup goes to that popup only; a Down outside every popup and its
    // owner light-dismisses them. A wheel turn over a popup scrolls that popup and never the page
    // under it. Move, Leave and a wheel turn elsewhere reach every popup and the tree, so hover tracks.
    bool wheel_over_popup = false;
    if (e.action == PointerAction::Wheel)
    {
        for (auto it = open_popups_.rbegin(); it != open_popups_.rend(); ++it)
        {
            if (it->bounds.contains(e.position))
            {
                wheel_over_popup = true;
                (void)it->content->handle_wheel(e);
                break;
            }
        }
    }
    if (!open_popups_.empty() && (e.action == PointerAction::Down || e.action == PointerAction::Up))
    {
        // Topmost first: a submenu drawn over its parent menu takes the click.
        const OpenPopup* hit = nullptr;
        for (auto it = open_popups_.rbegin(); it != open_popups_.rend(); ++it)
        {
            if (it->bounds.contains(e.position))
            {
                hit = &*it;
                break;
            }
        }
        if (hit)
        {
            // The ring holds the popup's own stops only while it has taken focus (a Flyout,
            // a light-dismissable TeachingTip), so this finds nothing for a ComboBox's list.
            if (e.action == PointerAction::Down && hit->owner == focus_popup_)
            {
                if (Element* target = focus_target_at(focus_order_, e.position))
                {
                    set_focus(target);
                }
            }
            hit->content->handle_pointer(e);
            sync_composition();
            sync_native();
            return;
        }

        if (e.action == PointerAction::Down)
        {
            bool inside_owner = false;
            for (const OpenPopup& popup : open_popups_)
            {
                if (popup.owner->bounds().contains(e.position))
                {
                    inside_owner = true;
                    break;
                }
            }
            if (!inside_owner)
            {
                // Only the light-dismissable ones close (PopupRequest::light_dismiss). With
                // nothing dismissed, the click is an ordinary click on whatever lies below.
                bool dismissed = false;
                for (const OpenPopup& popup : open_popups_)
                {
                    if (popup.alive() && light_dismiss(popup))
                    {
                        popup.owner->dismiss_popup(PopupCloseReason::LightDismiss);
                        dismissed = true;
                    }
                }
                if (dismissed)
                {
                    sync_composition();
                    sync_native();
                    return; // light-dismiss: the click that closed the popup is swallowed
                }
            }
            // Inside an owner's own bounds (a ComboBox box): the owner handles its own click
            // through the normal dispatch below.
        }
    }
    else if (!wheel_over_popup)
    {
        for (const OpenPopup& popup : open_popups_)
        {
            if (popup.alive()) // an earlier popup's handler may have destroyed this one
            {
                popup.content->handle_pointer(e);
            }
        }
    }

    // A Down on a focusable element focuses it before its own click handling (Fluent convention). A
    // click on non-focusable space leaves focus where it was.
    if (e.action == PointerAction::Down)
    {
        leave_scopes_outside(e.position);
        if (Element* target = focus_target_at(focus_order_, e.position))
        {
            set_focus(target);
        }
    }

    // The wheel goes through handle_wheel() so a nested scroller under the cursor gets first refusal
    // over an ancestor one.
    // The frame the theme draws sees every event too: its buttons track their own hover and press.
    if (frame_ && e.action != PointerAction::Wheel)
    {
        frame_->handle_pointer(e);
    }
    if (content_ && e.action == PointerAction::Wheel)
    {
        if (!wheel_over_popup)
        {
            (void)content_->handle_wheel(e);
        }
    }
    else if (content_)
    {
        content_->handle_pointer(e);
    }

    // A wheel turn can make a VirtualList destroy and recreate its rows on the spot, so the ring is
    // refreshed now and focus follows the rows that replaced them.
    if (e.action == PointerAction::Wheel)
    {
        rebuild_focus_order();
    }
    // A control started a drag while handling this event (DragDrop::start()): it runs here now.
    if (DragDrop::pending())
    {
        adopt_drag();
    }
    sync_composition();
    sync_native();
}

// ---- drag and drop ----

void Window::adopt_drag()
{
    drag_ = DragDrop::take_pending();
    if (!drag_)
    {
        return;
    }
    drag_detail::set_running(true);
    hide_tool_tip();
    update_drag_target();
}

CursorShape Window::cursor_at(Point client_dips) const
{
    if (drag_)
    {
        // OLE's drag cursors: the arrow where the drop would be taken, the no-entry sign elsewhere.
        return drag_->accepted == DataPackageOperation::None ? CursorShape::UniversalNo : CursorShape::Arrow;
    }
    if (pressed_cursor_)
    {
        return *pressed_cursor_;
    }
    return resolve_cursor(client_dips);
}

CursorShape Window::resolve_cursor(Point position) const
{
    // What floats on top is hit first: the modal, else the topmost popup under the point.
    Element* root = modal_.get();
    if (root == nullptr)
    {
        for (auto it = open_popups_.rbegin(); it != open_popups_.rend(); ++it)
        {
            if (it->alive() && it->bounds.contains(position))
            {
                root = it->content;
                break;
            }
        }
    }
    if (root == nullptr)
    {
        root = content_.get();
    }
    // Up from the element under the pointer, each element's cursor() first, then its
    // cursor_at() the pointer. It stops at the root, so a popup does not inherit the cursor of the tree below.
    std::vector<const Element*> path;
    (void)path_to_innermost(root, position, path);
    for (auto it = path.rbegin(); it != path.rend(); ++it)
    {
        if (const std::optional<CursorShape> shape = (*it)->cursor())
        {
            return *shape;
        }
        if (const std::optional<CursorShape> shape = (*it)->cursor_at(position))
        {
            return *shape;
        }
    }
    return CursorShape::Arrow;
}

Element* Window::drop_target_at(Point position) const
{
    // The innermost element under the point that accepts drops, found as hit_region() finds
    // its element: through each container's automation_children(), topmost first.
    const auto find = [&](const auto& self, Element* e) -> Element*
    {
        if (e == nullptr || e->visibility() != Visibility::Visible || !e->is_hit_test_visible() ||
            !e->bounds().contains(position))
        {
            return nullptr;
        }
        std::vector<Element*> children;
        e->automation_children(children);
        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
            if (Element* hit = self(self, *it))
            {
                return hit;
            }
        }
        return e->allow_drop() ? e : nullptr;
    };
    return find(find, modal_ ? modal_.get() : content_.get());
}

void Window::update_drag_target()
{
    if (!drag_)
    {
        return;
    }
    const DragEventArgs   args{drag_->data, drag_->position};
    std::vector<Element*> under; // the drop targets under the pointer, innermost first
    for (Element* el = drop_target_at(drag_->position); el != nullptr; el = el->parent())
    {
        if (el->allow_drop())
        {
            under.push_back(el);
        }
    }
    std::vector<Element*> before;
    for (const auto& watch : drag_->asked)
    {
        if (Element* el = watch->get())
        {
            before.push_back(el);
        }
    }
    const auto contains = [](const std::vector<Element*>& list, const Element* el)
    { return std::find(list.begin(), list.end(), el) != list.end(); };

    // Those the pointer has left hear it first, then the ones under it are asked, innermost first,
    // until one accepts. One asked last time but above the one that accepts now hears it leave too.
    for (Element* el : before)
    {
        if (!contains(under, el))
        {
            el->drag_leave(args);
        }
    }
    std::vector<Element*> asked;
    DragResponse          response;
    for (Element* el : under)
    {
        if (!contains(before, el))
        {
            el->drag_enter(args);
        }
        asked.push_back(el);
        response = el->drag_over(args);
        if (response.operation != DataPackageOperation::None)
        {
            break;
        }
    }
    for (Element* el : before)
    {
        if (contains(under, el) && !contains(asked, el))
        {
            el->drag_leave(args);
        }
    }

    drag_->asked.clear();
    for (Element* el : asked)
    {
        drag_->asked.push_back(std::make_unique<ElementWatch>(el));
    }
    const bool accepted = response.operation != DataPackageOperation::None;
    drag_->responder.reset(accepted ? asked.back() : nullptr);
    drag_->accepted      = response.operation;
    drag_->caption       = accepted ? std::move(response.caption) : std::string{};
    drag_->glyph_visible = !accepted || response.glyph_visible;
}

void Window::leave_drop_targets(const Element* except)
{
    const DragEventArgs   args{drag_->data, drag_->position};
    std::vector<Element*> asked;
    for (const auto& watch : drag_->asked)
    {
        if (Element* el = watch->get(); el != nullptr && el != except)
        {
            asked.push_back(el);
        }
    }
    drag_->asked.clear();
    drag_->responder.reset(nullptr);
    drag_->accepted = DataPackageOperation::None;
    drag_->caption.clear();
    for (Element* el : asked)
    {
        el->drag_leave(args);
    }
}

void Window::drag_pointer(const PointerEvent& e)
{
    drag_->position = e.position;
    switch (e.action)
    {
        case PointerAction::Move:
            update_drag_target();
            break;
        case PointerAction::Up:
            update_drag_target();
            (void)finish_drag();
            break;
        case PointerAction::Leave:
            leave_drop_targets(nullptr);
            break;
        default:
            break;
    }
    sync_composition();
    sync_native();
}

DataPackageOperation Window::finish_drag()
{
    // The one that accepted gets the drop; the others asked on the way up hear the drag leave.
    DataPackageOperation result = DataPackageOperation::None;
    if (Element* target = drag_->responder.get(); target != nullptr && drag_->accepted != DataPackageOperation::None)
    {
        result = drag_->accepted;
        leave_drop_targets(target);
        target->drop(DragEventArgs{drag_->data, drag_->position});
    }
    end_drag(result);
    return result;
}

DragResponse Window::external_drag_response() const
{
    if (!drag_ || !drag_->external)
    {
        return {};
    }
    return DragResponse{.operation = drag_->accepted, .caption = drag_->caption, .glyph_visible = drag_->glyph_visible};
}

DragResponse Window::handle_external_drag_enter(DataPackage data, Point position)
{
    check_thread();
    DispatchGuard guard{*this};
    drop_dead_popups();
    if (drag_ || DragDrop::pending())
    {
        return {}; // an in-app drag holds the pointer
    }
    data.source = nullptr;
    drag_.reset(new DragDrop::Session);
    drag_->data     = std::move(data);
    drag_->position = position;
    drag_->external = true;
    drag_detail::set_running(true);
    external_drag_.set(true);
    hide_tool_tip();
    update_drag_target();
    sync_composition();
    sync_native();
    return external_drag_response();
}

DragResponse Window::handle_external_drag_over(Point position, std::optional<DataPackageOperation> requested)
{
    check_thread();
    if (!drag_ || !drag_->external)
    {
        return {}; // ended meanwhile (DragDrop::cancel())
    }
    DispatchGuard guard{*this};
    drag_->position = position;
    if (requested)
    {
        drag_->data.requested_operation = *requested;
    }
    update_drag_target();
    sync_composition();
    sync_native();
    return external_drag_response();
}

void Window::handle_external_drag_leave()
{
    check_thread();
    if (!drag_ || !drag_->external)
    {
        return;
    }
    DispatchGuard guard{*this};
    end_drag(DataPackageOperation::None);
    sync_composition();
    sync_native();
}

DataPackageOperation Window::handle_external_drag_drop(Point position)
{
    check_thread();
    if (!drag_ || !drag_->external)
    {
        return DataPackageOperation::None;
    }
    DispatchGuard guard{*this};
    if (position != drag_->position)
    {
        drag_->position = position;
        update_drag_target();
    }
    const DataPackageOperation result = finish_drag();
    sync_composition();
    sync_native();
    return result;
}

const DataPackage* Window::external_drag_data() const
{
    return drag_ && drag_->external ? &drag_->data : nullptr;
}

DataPackageOperation Window::handle_external_drop(DataPackage data, Point position)
{
    (void)handle_external_drag_enter(std::move(data), position);
    return handle_external_drag_drop(position);
}

void Window::end_drag(DataPackageOperation result)
{
    if (!drag_)
    {
        return;
    }
    if (result == DataPackageOperation::None)
    {
        leave_drop_targets(nullptr);
    }
    Element* const       source = drag_->source.get();
    DragDrop::SessionPtr ended  = std::move(drag_);
    drag_detail::set_running(false);
    if (ended->external)
    {
        external_drag_.set(false);
    }
    if (source != nullptr)
    {
        source->drag_completed(result);
    }
    rebuild_focus_order();
}

bool Window::handle_key(const KeyEvent& e)
{
    check_thread();
    DispatchGuard guard{*this};
    drop_dead_popups();
    ensure_focus_order();
    // Escape cancels a drag; nothing else reaches the tree while one runs.
    if (drag_)
    {
        if (e.key == Key::Escape && e.action == KeyAction::Down)
        {
            end_drag(DataPackageOperation::None);
        }
        return true;
    }
    // Any key reveals the ring on whatever is focused, even when this key does not move focus.
    note_keyboard_activity();
    if (e.action == KeyAction::Down)
    {
        hide_tool_tip();
    }
    if (key_preview_ && detail::handled(key_preview_(e), false))
    {
        sync_composition();
        sync_native();
        return true;
    }
    // The window owns tab navigation; a control sees the Tab key only when it takes it
    // (Element::takes_tab_key()). Ctrl+Tab is a Tabs control's shortcut, so it goes on.
    if (e.key == Key::Tab && !e.modifiers.ctrl && (focused() == nullptr || !focused()->takes_tab_key()))
    {
        if (e.action == KeyAction::Down)
        {
            e.modifiers.shift ? focus_previous() : focus_next();
        }
        return true;
    }
    // Enter steps into a focus scope. enter_focus_scope() refuses an empty one, so the Enter falls
    // through and can still expand a collapsed Expander marked as a scope.
    if (e.key == Key::Enter && e.action == KeyAction::Down && focused() != nullptr && focused()->focus_scope())
    {
        if (enter_focus_scope(focused()))
        {
            return true;
        }
    }
    // Escape leaves the innermost entered scope, after the popups (the more transient context) but
    // ahead of the modal, since a scope entered within a dialog is the innermost thing to leave.
    if (e.key == Key::Escape && e.action == KeyAction::Down && open_popups_.empty() && !focus_scope_stack_.empty())
    {
        exit_focus_scope();
        return true;
    }
    // Escape light-dismisses every open popup ahead of the focused element, so a popup owner needs
    // no Escape handling of its own.
    if (e.key == Key::Escape && e.action == KeyAction::Down)
    {
        if (modal_)
        {
            (void)modal_->handle_key(e); // the modal's own Escape (a dialog's Close button)
            sync_composition();
            sync_native();
            return true;
        }
        if (!open_popups_.empty())
        {
            // A popup inside another popup (a submenu, a ComboBox in a Flyout) is the innermost
            // context: Escape closes it alone, and the one around it stays open for the next Escape.
            const OpenPopup* innermost = nullptr;
            for (auto it = open_popups_.rbegin(); it != open_popups_.rend(); ++it)
            {
                if (light_dismiss(*it))
                {
                    innermost = &*it;
                    break;
                }
            }
            if (innermost != nullptr && innermost->parent >= 0)
            {
                innermost->owner->dismiss_popup(PopupCloseReason::LightDismiss);
                sync_composition();
                sync_native();
                return true;
            }
            bool dismissed = false;
            for (const OpenPopup& popup : open_popups_)
            {
                if (popup.alive() && light_dismiss(popup))
                {
                    popup.owner->dismiss_popup(PopupCloseReason::LightDismiss);
                    dismissed = true;
                }
            }
            if (dismissed)
            {
                sync_composition();
                sync_native();
                return true;
            }
        }
    }

    // The key itself: the focused element first, then each parent until one handles it. With
    // nothing focused it starts at the root (the modal's while one is up).
    Element* const start = focused() != nullptr ? focused() : (modal_ ? modal_.get() : content_.get());
    if (focused() != nullptr)
    {
        if (SpellChecker* checker = live_spell_checker())
        {
            focused()->sync_spell_checker(*checker);
        }
    }
    const ElementWatch start_watch{start};
    for (Element* el = start; el != nullptr; el = el->parent())
    {
        if (el->is_enabled() && el->handle_key(e))
        {
            sync_composition();
            sync_native();
            return true;
        }
        if (start_watch.get() == nullptr)
        {
            break; // a handler took the tree it was in down
        }
    }

    // Nothing handled it. Shortcuts next: the focused element's, then each ancestor's.
    // Ctrl+Tab is a Tabs control's, not the window's.
    if (e.action == KeyAction::Down && (start_watch.get() != nullptr))
    {
        for (Element* el = start; el != nullptr; el = el->parent())
        {
            if (el->handle_shortcut(e))
            {
                sync_composition();
                sync_native();
                return true;
            }
        }
    }
    // Then the app-wide ones (a menu item's or command button's). Only shortcut keys are offered,
    // so typing never trips one.
    if (e.action == KeyAction::Down && is_shortcut_key(e))
    {
        std::vector<Element*> owners;
        if (modal_)
        {
            modal_->collect_live(owners);
        }
        else if (content_)
        {
            content_->collect_live(owners);
        }
        for (Element* owner : owners)
        {
            if (owner->is_effectively_visible() && owner->handle_global_shortcut(e))
            {
                rebuild_focus_order();
                sync_composition();
                sync_native();
                return true;
            }
        }
        // Then the window's own commands (add_command()), unless a modal dialog holds the window.
        // By index and through the pointer: a command may register or drop others.
        for (std::size_t i = 0; i < commands_.size() && !modal_; ++i)
        {
            WindowCommand* entry = commands_[i].get();
            if (entry->anchor.observer_count() == 0 || entry->command == nullptr)
            {
                continue;
            }
            const CommandValues& now = entry->command->current();
            if (now.enabled && now.shortcut.matches(e))
            {
                (*entry->command)();
                rebuild_focus_order();
                sync_composition();
                sync_native();
                return true;
            }
        }
    }
    // An arrow nothing used moves focus, which keeps a Slider and a TextBox caret working. Alt+arrow
    // is a command (open a flyout), never a direction.
    if (is_arrow(e.key) && !e.modifiers.alt && e.action == KeyAction::Down &&
        (step_in_choice_group(e) || focus_direction(e.key)))
    {
        return true;
    }
    sync_composition();
    sync_native();
    return false;
}

void Window::handle_text(std::string_view text)
{
    check_thread();
    DispatchGuard  guard{*this};
    Element* const start = focused();
    if (start != nullptr)
    {
        if (SpellChecker* checker = live_spell_checker())
        {
            start->sync_spell_checker(*checker);
        }
        const TextInputEvent event{std::string(text)};
        const ElementWatch   start_watch{start};
        for (Element* el = start; el != nullptr && (start_watch.get() != nullptr); el = el->parent())
        {
            if (el->is_enabled() && el->handle_text(event))
            {
                break;
            }
        }
    }
    sync_composition();
    sync_native();
}

bool Window::handle_context_menu(std::optional<Point> position)
{
    DispatchGuard guard{*this};
    if (position)
    {
        // Like a pointer Down: the element under the pointer takes focus first (every Windows text
        // field does on right-click).
        note_pointer_activity();
        ensure_focus_order();
        Element* target = focus_target_at(focus_order_, *position);
        if (target)
        {
            set_focus(target);
        }
        // Then the innermost element under the pointer and everything around it, focusable or not
        // (a ui::ContextMenu around a picture), ahead of the focused element's own chain.
        if (Element* root = modal_ ? modal_.get() : content_.get();
            root && Element::raise_context_requested_in(*root, position))
        {
            sync_composition();
            sync_native();
            return true;
        }
        if (!target)
        {
            return false;
        }
    }
    if (!focused())
    {
        return false;
    }
    // ContextRequested first, bubbling from the focused element (Element::on_context_requested()):
    // an app's own flyout wins over a control's built-in native menu.
    for (Element* el = focused(); el != nullptr; el = el->parent())
    {
        if (el->raise_context_requested(position))
        {
            sync_composition();
            sync_native();
            return true;
        }
    }
    if (!context_menu_host_)
    {
        return false;
    }
    // A menu offers spelling fixes, so the checker a key would have synced is synced here too.
    if (SpellChecker* checker = live_spell_checker())
    {
        focused()->sync_spell_checker(*checker);
    }
    const bool shown = focused()->handle_context_menu(position, *context_menu_host_);
    sync_composition();
    sync_native();
    return shown;
}

bool Window::handle_automation_invoke(Element& element)
{
    DispatchGuard   guard{*this};
    InvokeProvider* invoke = detail::TreeAccess::automation_providers(element).invoke;
    return invoke != nullptr && invoke->invoke();
}

void Window::handle_native_content_changed(NativeControl& native)
{
    DispatchGuard guard{*this};
    native.on_native_content_changed();
    // The edit happened inside the child HWND, so nothing else in this frame knows the
    // control needs re-checking; syncing here is what actually runs the pass.
    sync_native();
}

bool Window::handle_native_notification(NativeControl& native, NativeNotification& notification)
{
    DispatchGuard guard{*this};
    const bool    handled = native.handle_native_notification(notification);
    if (handled)
    {
        // A handler may have changed state the tree shows; the next layout picks it up, and a
        // native control it touched is synced now.
        sync_native();
    }
    return handled;
}

bool Window::handle_native_context_menu(Element& native, std::optional<Point> position)
{
    DispatchGuard guard{*this};
    if (!context_menu_host_)
    {
        return false;
    }
    const bool shown = native.handle_context_menu(position, *context_menu_host_);
    sync_composition();
    sync_native();
    return shown;
}

void Window::focus_content()
{
    ensure_focus_order();
    set_focus(focus_order_.empty() ? nullptr : focus_order_.front());
}

void Window::focus_next()
{
    if (step_inside_scope(+1))
    {
        return;
    }
    ensure_focus_order();
    if (focus_order_.empty())
    {
        return;
    }
    if (!focused())
    {
        // Focus died or left the ring since it was given: carry on from the stop after it.
        Element* const after   = focus_after_.get();
        const bool     in_ring = after != nullptr &&
                                 std::find(focus_order_.begin(), focus_order_.end(), after) != focus_order_.end();
        set_focus(in_ring ? after : focus_order_.front());
        return;
    }
    auto it = std::find(focus_order_.begin(), focus_order_.end(), focused());
    if (it == focus_order_.end())
    {
        set_focus(focus_order_.front());
        return;
    }
    Element* const from = focused();
    for (std::size_t step = 0; step < focus_order_.size(); ++step)
    {
        ++it;
        if (it == focus_order_.end())
        {
            it = focus_order_.begin(); // wrap around
        }
        if (!passes_over(*it, from))
        {
            set_focus(*it);
            return;
        }
    }
}

void Window::focus_previous()
{
    if (step_inside_scope(-1))
    {
        return;
    }
    ensure_focus_order();
    if (focus_order_.empty())
    {
        return;
    }
    if (!focused())
    {
        Element* const before  = focus_before_.get();
        const bool     in_ring = before != nullptr &&
                                 std::find(focus_order_.begin(), focus_order_.end(), before) != focus_order_.end();
        set_focus(in_ring ? before : focus_order_.back());
        return;
    }
    auto it = std::find(focus_order_.begin(), focus_order_.end(), focused());
    if (it == focus_order_.end())
    {
        set_focus(focus_order_.back());
        return;
    }
    Element* const from = focused();
    for (std::size_t step = 0; step < focus_order_.size(); ++step)
    {
        it = it == focus_order_.begin() ? std::prev(focus_order_.end()) : std::prev(it); // wrap around
        if (!passes_over(*it, from))
        {
            set_focus(*it);
            return;
        }
    }
}

bool Window::passes_over(const Element* candidate, const Element* from) const
{
    const void* group = candidate->selection_group();
    if (group == nullptr)
    {
        return false;
    }
    if (from != nullptr && from->selection_group() == group)
    {
        return true; // Tab leaves the group it is in
    }
    const Element* stop = nullptr;
    for (const Element* member : focus_order_)
    {
        if (member->selection_group() != group)
        {
            continue;
        }
        if (const SelectionItemProvider* item = detail::TreeAccess::automation_providers(const_cast<Element&>(*member)).selection_item;
            item != nullptr && item->item_selected())
        {
            stop = member;
            break;
        }
        if (stop == nullptr)
        {
            stop = member;
        }
    }
    return candidate != stop;
}

bool Window::step_in_choice_group(const KeyEvent& e)
{
    Element* const from  = focused();
    const void*    group = from != nullptr ? from->selection_group() : nullptr;
    if (group == nullptr)
    {
        return false;
    }
    ensure_focus_order();
    std::vector<Element*> members;
    for (Element* member : focus_order_)
    {
        if (member->selection_group() == group)
        {
            members.push_back(member);
        }
    }
    const auto at = std::find(members.begin(), members.end(), from);
    if (at == members.end() || members.size() < 2)
    {
        return false; // nothing to move to: the arrow moves focus as usual
    }
    const bool back  = e.key == Key::ArrowLeft || e.key == Key::ArrowUp;
    const auto index = static_cast<std::size_t>(at - members.begin());
    const auto count = members.size();
    Element*   to    = members[back ? (index + count - 1) % count : (index + 1) % count];
    set_focus(to);
    if (!e.modifiers.ctrl)
    {
        if (SelectionItemProvider* item = detail::TreeAccess::automation_providers(*to).selection_item)
        {
            (void)item->select_item();
        }
    }
    return true;
}

bool Window::step_inside_scope(int delta)
{
    // Only an item-navigating scope needs this: its inner ring is the control itself, so a plain
    // ring walk would land straight back on it and Tab would look stuck.
    if (focus_scope_stack_.empty())
    {
        return false;
    }
    Element* scope = focus_scope_stack_.back()->get();
    if (scope == nullptr || focus_order_.size() != 1 || focus_order_.front() != scope)
    {
        return false;
    }
    if (scope->scope_step(delta))
    {
        sync_composition();
        sync_native();
        return true;
    }
    // The control declined to wrap, so the step falls out of the scope rather than trapping
    // focus in something with nowhere left to go.
    exit_focus_scope();
    return true;
}

void Window::blur()
{
    set_focus(nullptr);
}

void Window::suspend_focus()
{
    suspended_focus_.reset(focused());
    set_focus(nullptr);
}

void Window::restore_focus()
{
    Element* const restore = suspended_focus_.get();
    suspended_focus_.reset(nullptr);
    if (restore == nullptr || !focus_element(restore))
    {
        focus_content();
    }
}

bool Window::focus_element(Element* e)
{
    ensure_focus_order();
    if (std::find(focus_order_.begin(), focus_order_.end(), e) == focus_order_.end())
    {
        return false;
    }
    set_focus(e);
    return true;
}

bool Window::focus_direction(Key key)
{
    ensure_focus_order();
    if (focus_order_.empty())
    {
        return false;
    }
    if (focused() == nullptr)
    {
        set_focus(focus_order_.front());
        return true;
    }

    const Rect from = focused()->bounds();
    if (from.empty())
    {
        return false;
    }

    // Two passes, not one weighted score: in a two-column layout a score lets Down jump from a
    // navigation pane into the page beside it.
    Element* aligned          = nullptr;
    float    aligned_distance = 0.0f;
    Element* offset           = nullptr;
    float    offset_score     = 0.0f;
    for (Element* candidate : focus_order_)
    {
        if (candidate == focused() || passes_over(candidate, focused()))
        {
            continue;
        }
        const Rect to = candidate->bounds();
        if (to.empty())
        {
            continue;
        }
        float along  = 0.0f;
        float across = 0.0f;
        if (!direction_distance(key, from, to, along, across))
        {
            continue;
        }
        if (across == 0.0f)
        {
            if (aligned == nullptr || along < aligned_distance)
            {
                aligned          = candidate;
                aligned_distance = along;
            }
        }
        else if (aligned == nullptr)
        {
            const float score = along + across * 2.0f;
            if (offset == nullptr || score < offset_score)
            {
                offset       = candidate;
                offset_score = score;
            }
        }
    }

    Element* target = aligned != nullptr ? aligned : offset;
    if (target == nullptr)
    {
        return false; // nothing that way: the key still reaches the focused element
    }
    set_focus(target);
    return true;
}

void Window::ensure_focus_order()
{
    if (focus_order_.empty() || focus_order_epoch_ != detail::removed_element_count())
    {
        rebuild_focus_order();
    }
}

void Window::rebuild_focus_order()
{
    detail::count(detail::PerfCounter::FocusOrderBuilds);
    focus_order_epoch_ = detail::removed_element_count();
    // The owner is re-found in the tree every time rather than trusted: it can close by its own
    // state or die with its page, and either way the ring must come back safely.
    bool     popup_released = false;
    Element* released_owner = nullptr; // non-null when the owner is still alive, only closed
    if (focus_popup_ != nullptr)
    {
        std::vector<Element*> owners;
        collect_live_popup_owners(owners);
        const bool          alive   = std::find(owners.begin(), owners.end(), focus_popup_) != owners.end();
        const PopupRequest* request = alive ? focus_popup_->popup_request() : nullptr;
        if (request == nullptr || !request->takes_focus || request->content == nullptr)
        {
            released_owner = alive ? focus_popup_ : nullptr;
            focus_popup_   = nullptr;
            popup_released = true;
        }
    }
    if (popup_released && focused() != nullptr)
    {
        // Told it lost focus only when the popup still exists (a closed Flyout keeps its content);
        // a dead page's elements are simply forgotten.
        std::vector<Element*> inside;
        if (released_owner != nullptr && focus_popup_content_.get() != nullptr)
        {
            focus_popup_content_.get()->collect_focusables(inside);
        }
        focus_popup_content_.reset(nullptr);
        if (std::find(inside.begin(), inside.end(), focused()) != inside.end())
        {
            focused()->set_focused(false);
        }
        set_focused_element(nullptr);
    }

    focus_order_.clear();
    // While a modal is open the ring is scoped to it; trapping focus is part of the modal contract.
    // A focus-taking popup narrows it further, to the popup.
    Element* root = modal_ ? modal_.get() : content_.get();
    if (focus_popup_ != nullptr)
    {
        root = focus_popup_->popup_request()->content;
    }
    else if (!modal_ && !focus_traps_.empty() && content_)
    {
        // An in-tree focus trap (SidePane's light-dismissable pane) holds the ring the way a
        // focus-taking popup does. Re-found in the tree first, so an owner gone with its page is dropped.
        std::vector<Element*> owners;
        content_->collect_live(owners);
        std::erase_if(focus_traps_,
                      [&owners](const FocusTrap& trap)
                      { return std::find(owners.begin(), owners.end(), trap.owner->get()) == owners.end(); });
        if (!focus_traps_.empty())
        {
            root = focus_traps_.back().owner->get();
        }
    }
    if (root)
    {
        root->collect_focusables(focus_order_);
    }
    // A container answers collect_focusables() for its whole subtree, Collapsed or not; a
    // Collapsed element (or one under a Collapsed ancestor) is no tab stop (Element::visibility()).
    std::erase_if(focus_order_, [](const Element* e) { return !e->is_effectively_visible(); });

    // Descend the entered scopes level by level. A scope that left its ring (collapsed, disabled,
    // emptied, replaced) truncates the stack there, so no tree mutation has to report it.
    std::size_t depth = 0;
    while (depth < focus_scope_stack_.size())
    {
        Element* scope = focus_scope_stack_[depth]->get(); // null: destroyed with its page
        if (scope == nullptr || !scope->focus_scope() ||
            std::find(focus_order_.begin(), focus_order_.end(), scope) == focus_order_.end())
        {
            break;
        }
        std::vector<Element*> inner;
        scope->collect_scope_focusables(inner);
        std::erase_if(inner, [](const Element* e) { return !e->is_effectively_visible(); });
        if (inner.empty())
        {
            break; // nothing left inside: this scope is no longer enterable
        }
        focus_order_ = std::move(inner);
        ++depth;
    }
    for (std::size_t i = depth; i < focus_scope_stack_.size(); ++i)
    {
        if (Element* scope = focus_scope_stack_[i]->get())
        {
            scope->set_scope_entered(false);
        }
    }
    focus_scope_stack_.resize(depth);

    if (focused() && std::find(focus_order_.begin(), focus_order_.end(), focused()) == focus_order_.end())
    {
        set_focused_element(nullptr); // the anchors stay, so Tab continues from where it was
    }
    else if (focused())
    {
        anchor_focus();
    }

    // Back to whatever had focus before the popup opened, if it is
    // still a stop in the ring we are now walking.
    if (popup_released)
    {
        Element* const restore = pre_popup_focus_.get();
        pre_popup_focus_.reset(nullptr);
        if (restore != nullptr && std::find(focus_order_.begin(), focus_order_.end(), restore) != focus_order_.end())
        {
            set_focus(restore);
        }
    }
    deliver_focus_events();
}

void Window::sync_focus_popup()
{
    // The topmost open popup that takes focus and has somewhere to put it. With nothing focusable
    // inside, focus stays where it is rather than parked on the presenter.
    Element* wanted = nullptr;
    for (auto it = open_popups_.rbegin(); it != open_popups_.rend() && wanted == nullptr; ++it)
    {
        if (const PopupRequest* request = it->owner->popup_request(); request != nullptr && request->takes_focus)
        {
            std::vector<Element*> inside;
            it->content->collect_focusables(inside);
            if (!inside.empty())
            {
                wanted = it->owner;
            }
        }
    }
    if (wanted == focus_popup_ || wanted == nullptr)
    {
        return; // unchanged, or closing, which rebuild_focus_order() has already handled
    }
    if (focus_popup_ == nullptr)
    {
        pre_popup_focus_.reset(focused());
    }
    if (focused() != nullptr)
    {
        focused()->set_focused(false);
        set_focused_element(nullptr);
    }
    focus_popup_ = wanted;
    focus_popup_content_.reset(wanted->popup_request()->content);
    focus_scope_stack_.clear(); // a scope entered below the popup is not the popup's
    rebuild_focus_order();
    set_focus(focus_order_.empty() ? nullptr : focus_order_.front());
}

bool Window::enter_focus_scope(Element* scope)
{
    if (scope == nullptr || !scope->focus_scope())
    {
        return false;
    }
    if (std::find_if(focus_scope_stack_.begin(),
                     focus_scope_stack_.end(),
                     [scope](const std::unique_ptr<ElementWatch>& entered) { return entered->get() == scope; }) !=
        focus_scope_stack_.end())
    {
        // Already inside it, the common case for an item-navigating scope. Otherwise a second
        // Enter would push the scope again instead of reaching the control (ListView selects a row).
        return false;
    }
    if (std::find(focus_order_.begin(), focus_order_.end(), scope) == focus_order_.end())
    {
        return false; // not a stop in the ring currently being walked
    }
    std::vector<Element*> inner;
    scope->collect_scope_focusables(inner);
    if (inner.empty())
    {
        // Nothing inside worth entering (a collapsed Expander, an emptied list). Refusing lets the
        // Enter fall through to the element's own handle_key().
        return false;
    }

    focus_scope_stack_.push_back(std::make_unique<ElementWatch>(scope));
    focus_order_ = std::move(inner);
    scope->set_scope_entered(true);
    // For an item-navigating scope the inner ring is the scope itself, so this is a no-op
    // and focus stays put while the control takes over stepping (Element::scope_step()).
    set_focus(focus_order_.front());
    return true;
}

void Window::leave_scopes_outside(Point position)
{
    // A click is never trapped by an entered scope the way Tab is. Leaving the scopes the pointer
    // landed outside of keeps every control outside them clickable-to-focus.
    while (!focus_scope_stack_.empty())
    {
        if (const Element* scope = focus_scope_stack_.back()->get(); scope != nullptr && scope->bounds().contains(position))
        {
            break;
        }
        exit_focus_scope();
    }
}

bool Window::exit_focus_scope()
{
    if (focus_scope_stack_.empty())
    {
        return false;
    }
    Element* const scope = focus_scope_stack_.back()->get(); // null: destroyed with its page
    focus_scope_stack_.pop_back();
    if (scope != nullptr)
    {
        scope->set_scope_entered(false);
    }
    rebuild_focus_order(); // back to the ring `scope` itself is a stop in
    if (scope != nullptr)
    {
        set_focus(scope);
    }
    return true;
}

void Window::close_modal()
{
    check_thread();
    if (!modal_)
    {
        return;
    }
    modal_close_pending_ = true;
    // Outside any dispatch (a direct call, as from a test) it is safe to act now; otherwise
    // DispatchGuard does it once the outermost call returns.
    if (dispatch_depth_ == 0)
    {
        resolve_pending_modal_close();
    }
}

void Window::resolve_pending_modal_close()
{
    if (!modal_close_pending_)
    {
        return;
    }
    modal_close_pending_ = false;
    set_focus(nullptr); // focus was inside the modal
    std::unique_ptr<Element> closed = std::move(modal_);
    rebuild_focus_order(); // now scoped back to content_
    // Through the ring: what had focus before the modal may have left the tree meanwhile.
    Element* const previous = pre_modal_focus_.get();
    pre_modal_focus_.reset(nullptr);
    focus_element(previous);
    ++content_generation_;

    // A dialog reports its result once it is out of the tree and gone, so its handler may show
    // the next one.
    Callback<void(DialogResult)> on_closed;
    DialogResult                 result = DialogResult::None;
    if (auto* dialog = dynamic_cast<DialogControl*>(closed.get()))
    {
        on_closed = std::move(dialog->on_closed_);
        result    = dialog->result_;
    }
    closed.reset();
    if (on_closed)
    {
        on_closed(result);
    }
}

void Window::attach_modal(Element& modal)
{
    if (auto* dialog = dynamic_cast<DialogControl*>(&modal))
    {
        // Only while it is still the modal: a replaced dialog closing late must not close the next.
        dialog->close_modal_ = [this, dialog]
        {
            if (modal_.get() == dialog)
            {
                close_modal();
            }
        };
    }
}

bool Window::focus(Element& element)
{
    // Rebuilt, not just refreshed: the element may have been shown since the last layout.
    rebuild_focus_order();
    const auto in_ring = [this](Element* e)
    { return std::find(focus_order_.begin(), focus_order_.end(), e) != focus_order_.end(); };
    Element* target = in_ring(&element) ? &element : nullptr;
    if (target == nullptr)
    {
        std::vector<Element*> inside;
        element.collect_focusables(inside);
        const auto first = std::find_if(inside.begin(), inside.end(), in_ring);
        target           = first != inside.end() ? *first : nullptr;
    }
    if (target == nullptr)
    {
        return false;
    }
    set_focus(target);
    return true;
}

bool Window::push_focus(Element& element)
{
    auto previous = std::make_unique<ElementWatch>(focused());
    if (!focus(element))
    {
        return false;
    }
    pushed_focus_.push_back(std::move(previous));
    return true;
}

bool Window::pop_focus()
{
    if (pushed_focus_.empty())
    {
        return false;
    }
    Element* const previous = pushed_focus_.back()->get();
    pushed_focus_.pop_back();
    rebuild_focus_order();
    if (previous == nullptr || std::find(focus_order_.begin(), focus_order_.end(), previous) == focus_order_.end())
    {
        return false;
    }
    set_focus(previous);
    return true;
}

bool Window::handle_menu_access(char32_t letter)
{
    DispatchGuard guard{*this};
    note_keyboard_activity();
    // Alt or F10 again, with focus still where they put it, goes back.
    if (letter == 0 && menu_access_target_.get() != nullptr && menu_access_target_.get() == focused())
    {
        return end_menu_access();
    }
    Element* const previous = focused();
    Element* const root     = modal_ ? modal_.get() : content_.get();
    Element*       taker    = nullptr;
    const auto     offer    = [&](const auto& self, Element& node) -> void
    {
        if (taker != nullptr || node.visibility() != Visibility::Visible || !node.is_enabled())
        {
            return;
        }
        if (node.take_menu_access(letter))
        {
            taker = &node;
            return;
        }
        node.for_each_child([&](Element& child) { self(self, child); });
    };
    if (root != nullptr)
    {
        offer(offer, *root);
    }
    if (taker == nullptr)
    {
        return false;
    }
    menu_access_target_.reset(focused());
    menu_access_return_.reset(previous != focused() ? previous : nullptr);
    return true;
}

bool Window::end_menu_access()
{
    Element* const target = menu_access_target_.get();
    Element* const back   = menu_access_return_.get();
    if (target == nullptr || target != focused())
    {
        return false;
    }
    menu_access_target_.reset(nullptr);
    menu_access_return_.reset(nullptr);
    if (back != nullptr && back->focusable())
    {
        set_focus(back);
    }
    return true;
}

void Window::set_focus(Element* e)
{
    if (focused() == e)
    {
        return;
    }
    // Focus going anywhere else ends the menu access Alt started.
    if (e != menu_access_target_.get())
    {
        menu_access_target_.reset(nullptr);
        menu_access_return_.reset(nullptr);
    }
    if (focused())
    {
        focused()->set_focused(false);
    }
    set_focused_element(e);
    anchor_focus();
    if (focused())
    {
        // Before set_focused(true), so a field can schedule its first check as it gains focus.
        if (SpellChecker* checker = live_spell_checker())
        {
            focused()->sync_spell_checker(*checker);
        }
        focused()->set_focused(true);
        focused()->set_focus_visible(keyboard_modality_);
        // Tab can walk into a region scrolled out of sight, and a ring nobody can see looks like
        // Tab stopped working.
        focused()->bring_into_view();
    }
    sync_composition();
    sync_native();
    deliver_focus_events();
}

void Window::set_focused_element(Element* e, bool notify)
{
    Element* old = focus_watch_.get();
    if (notify && old != e)
    {
        // Focus left every element around `old` that is not also around `e`, innermost first, and
        // entered every element around `e` that was not around `old`.
        const auto around = [](Element* from, Element* of)
        {
            for (Element* n = of; n != nullptr; n = n->parent())
            {
                if (n == from)
                {
                    return true;
                }
            }
            return false;
        };
        for (Element* n = old; n != nullptr && !around(n, e); n = n->parent())
        {
            if (n->has_focus_handler())
            {
                pending_focus_events_.push_back({std::make_unique<ElementWatch>(n), false});
            }
        }
        for (Element* n = e; n != nullptr && !around(n, old); n = n->parent())
        {
            if (n->has_focus_handler())
            {
                pending_focus_events_.push_back({std::make_unique<ElementWatch>(n), true});
            }
        }
    }
    if (old != nullptr && old != e)
    {
        old->note_keyboard_focus(false);
    }
    focus_watch_.reset(e);
    if (e != nullptr)
    {
        e->note_keyboard_focus(true);
    }
}

void Window::deliver_focus_events()
{
    std::vector<PendingFocusEvent> events = std::move(pending_focus_events_);
    pending_focus_events_.clear();
    // Focus that left an element and came back to it before anything was raised (through no focus
    // at all, as a closing popup hands it back) did not leave it.
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        for (std::size_t j = i + 1; j < events.size() && events[i].element->get() != nullptr; ++j)
        {
            if (events[j].element->get() == events[i].element->get() && events[j].got != events[i].got)
            {
                events[i].element->reset(nullptr);
                events[j].element->reset(nullptr);
            }
        }
    }
    for (PendingFocusEvent& event : events)
    {
        if (Element* element = event.element->get())
        {
            element->raise_focus_event(event.got); // may move focus again, queueing anew
        }
    }
    if (!pending_focus_events_.empty())
    {
        deliver_focus_events();
    }
}

void Window::anchor_focus()
{
    Element* after  = nullptr;
    Element* before = nullptr;
    if (focused() != nullptr)
    {
        auto it = std::find(focus_order_.begin(), focus_order_.end(), focused());
        if (it != focus_order_.end())
        {
            after  = std::next(it) == focus_order_.end() ? focus_order_.front() : *std::next(it);
            before = it == focus_order_.begin() ? focus_order_.back() : *std::prev(it);
        }
    }
    focus_after_.reset(after);
    focus_before_.reset(before);
}

void Window::note_keyboard_activity()
{
    keyboard_modality_ = true;
    if (focused())
    {
        focused()->set_focus_visible(true);
    }
}

void Window::note_pointer_activity()
{
    keyboard_modality_ = false;
    if (focused())
    {
        focused()->set_focus_visible(false);
    }
}

void Window::sync_composition()
{
    const detail::AnimationsScope animations{theme_.animations_enabled};
    if (!compositor_)
    {
        return;
    }
    if (content_)
    {
        content_->sync_composition(*compositor_, theme_);
    }
    if (modal_) // outside content_, like its tick() and sync_native()
    {
        modal_->sync_composition(*compositor_, theme_);
    }
}

SpellChecker* Window::live_spell_checker()
{
    if (spell_checker_ == nullptr && spell_checker_source_)
    {
        spell_checker_ = spell_checker_source_();
    }
    return spell_checker_;
}

void Window::sync_native()
{
    if (native_host_ && (content_ || modal_))
    {
        // Published before the walk rather than passed down it, so no container's sync_native()
        // override needs a second argument.
        native_host_->set_spell_checker(live_spell_checker());
        native_host_->begin_sync();
        if (content_)
        {
            content_->sync_native(*native_host_);
        }
        if (modal_) // a native control inside a dialog is one too
        {
            modal_->sync_native(*native_host_);
        }
        // Whatever the walk did not reach is in a subtree no container shows any more.
        native_host_->end_sync();
    }
}

void Window::collect_live_popup_owners(std::vector<Element*>& owners, std::vector<Element*>* parents)
{
    // A dedicated walk, not focus_order_: a popup owner need not be focusable (a ToolTip around a
    // plain anchor). The modal is scanned too, so popups inside a dialog work.
    if (content_)
    {
        content_->collect_live(owners);
    }
    if (modal_)
    {
        modal_->collect_live(owners);
    }
    if (parents != nullptr)
    {
        parents->assign(owners.size(), nullptr);
    }
    // Then the owners inside every open popup, level by level (a submenu, a ComboBox in a Flyout).
    // Only owners found alive on this walk are asked anything.
    for (std::size_t i = 0; i < owners.size(); ++i)
    {
        Element*            owner   = owners[i];
        const PopupRequest* request = owner->popup_request();
        if (request == nullptr || !owner->is_effectively_visible())
        {
            continue;
        }
        Element* content = request->content;
        if (content == nullptr || content == owner)
        {
            continue;
        }
        std::vector<Element*> nested;
        content->collect_live(nested);
        for (Element* candidate : nested)
        {
            if (std::find(owners.begin(), owners.end(), candidate) != owners.end())
            {
                continue;
            }
            owners.push_back(candidate);
            if (parents != nullptr)
            {
                parents->push_back(owner);
            }
        }
    }
}

Window::OpenPopup Window::make_open_popup(Element* owner, Element* content, int parent) const
{
    return OpenPopup{
        .owner   = owner,
        .content = content,
        // Inside a drawn frame only the part over the content is drawn, so only that part takes
        // input and the caption under the rest stays the caption.
        .bounds        = frame_ ? intersection(content->bounds(), content_area(client_size_)) : content->bounds(),
        .parent        = parent,
        .owner_watch   = std::make_unique<ElementWatch>(owner),
        .content_watch = std::make_unique<ElementWatch>(content),
    };
}

Rect Window::anchor_rect(const Element& owner, const PopupRequest& request)
{
    const Element* anchor = request.anchor != nullptr ? request.anchor : &owner;
    const Rect     bounds = anchor->bounds();
    if (!request.anchor_rect)
    {
        return bounds;
    }
    const Rect& inside = *request.anchor_rect;
    return Rect{bounds.x + inside.x, bounds.y + inside.y, inside.width, inside.height};
}

bool Window::light_dismiss(const OpenPopup& popup)
{
    const PopupRequest* request = popup.owner->popup_request();
    return request != nullptr && request->light_dismiss;
}

void Window::drop_dead_popups()
{
    if (std::ranges::all_of(open_popups_, [](const OpenPopup& p) { return p.alive(); }))
    {
        return;
    }
    // A popup nested in a dead one went with it; the survivors keep their order, re-indexed.
    std::vector<int>       index(open_popups_.size(), -1);
    std::vector<OpenPopup> kept;
    for (std::size_t i = 0; i < open_popups_.size(); ++i)
    {
        OpenPopup& popup = open_popups_[i];
        if (!popup.alive() || (popup.parent >= 0 && index[static_cast<std::size_t>(popup.parent)] < 0))
        {
            continue;
        }
        if (popup.parent >= 0)
        {
            popup.parent = index[static_cast<std::size_t>(popup.parent)];
        }
        index[i] = static_cast<int>(kept.size());
        kept.push_back(std::move(popup));
    }
    open_popups_ = std::move(kept);
}

void Window::sync_popups(const TextMeasurer& measurer)
{
    open_popups_.clear();
    std::vector<Element*> owners;
    std::vector<Element*> parent_owners; // the owner whose popup each owner floats in, or null
    collect_live_popup_owners(owners, &parent_owners);
    if (tool_tip_open())
    {
        owners.push_back(tool_tip_service_.get());
        parent_owners.push_back(nullptr);
    }
    for (std::size_t owner_index = 0; owner_index < owners.size(); ++owner_index)
    {
        Element*      owner   = owners[owner_index];
        PopupRequest* request = owner->popup_request();
        if (request == nullptr || !owner->is_effectively_visible())
        {
            continue;
        }
        Element* content = request->content;
        if (!content)
        {
            continue;
        }
        // Popup content is owned by its owner but usually not parented to it, so its own root
        // carries this window's thread for Element's thread check, as content and modal roots do.
        Element* popup_root = content;
        while (popup_root->parent() != nullptr)
        {
            popup_root = popup_root->parent();
        }
        adopt_root(*popup_root);
        // Placed after its parent, which is always earlier in `owners`, so the anchor it reads
        // off the parent popup's content is already where this pass put it.
        int parent = -1;
        if (Element* parent_owner = parent_owners[owner_index]; parent_owner != nullptr)
        {
            for (std::size_t i = 0; i < open_popups_.size(); ++i)
            {
                if (open_popups_[i].owner == parent_owner)
                {
                    parent = static_cast<int>(i);
                }
            }
        }

        if (owner->place_popup(*content, client_size_, MeasureContext{measurer, theme_}))
        {
            if (owner->popup_request() != nullptr) // a place callback may close it
            {
                open_popups_.push_back(make_open_popup(owner, content, parent));
            }
            continue;
        }

        const Rect           anchor    = anchor_rect(*owner, *request);
        const Rect           area      = content_area(client_size_); // a drawn frame's caption is not room
        const PopupPlacement requested = request->placement;
        const bool           centered  = request->placement_alignment == PopupPlacementAlignment::Center;
        const bool           trailing  = request->placement_alignment == PopupPlacementAlignment::Trailing;

        if (requested == PopupPlacement::Below || requested == PopupPlacement::Above)
        {
            const float below_space = std::max(0.0f, area.bottom() - anchor.bottom());
            const float above_space = std::max(0.0f, anchor.top() - area.y);
            // An unbounded-height probe decides whether the requested side has room, without
            // changing what the chosen side measures below. An edge-aligned popup has the room from
            // its aligned edge to the window's far side, not the anchor's width; an anchor with no
            // width (a point) gives no hint, not zero room.
            const float edge_room  = trailing ? anchor.right() - area.x : area.right() - anchor.left();
            const float hint_width = centered              ? area.width
                                     : anchor.width > 0.0f ? std::max(anchor.width, edge_room)
                                                           : Unbounded;
            const Size  natural    = content->measure(Size{hint_width, Unbounded}, measurer, theme_);
            bool        below      = requested == PopupPlacement::Below;
            if (below && natural.height > below_space && natural.height <= above_space)
            {
                below = false;
            }
            else if (!below && natural.height > above_space && natural.height <= below_space)
            {
                below = true;
            }

            const float available_height = below ? below_space : above_space;
            const Size  desired          = content->measure(Size{hint_width, available_height}, measurer, theme_);
            const float width            = std::min(desired.width, area.width);
            const float height           = std::min(desired.height, available_height);
            float       left             = centered ? anchor.center().x - width * 0.5f : anchor.left();
            if (trailing)
            {
                left = anchor.right() - width; // above or below, aligned to the anchor's right edge
            }
            const float x = std::clamp(left, area.x, std::max(area.x, area.right() - width));
            const float y = below ? anchor.bottom() : std::max(area.y, anchor.top() - height);
            content->arrange(Rect{x, y, width, height});
        }
        else // Left or Right
        {
            const float right_space = std::max(0.0f, area.right() - anchor.right());
            const float left_space  = std::max(0.0f, anchor.left() - area.x);
            const float hint_height = centered ? area.height : anchor.height > 0.0f ? anchor.height : Unbounded;
            const Size  natural     = content->measure(Size{Unbounded, hint_height}, measurer, theme_);
            bool        right       = requested == PopupPlacement::Right;
            if (right && natural.width > right_space && natural.width <= left_space)
            {
                right = false;
            }
            else if (!right && natural.width > left_space && natural.width <= right_space)
            {
                right = true;
            }

            const float available_width = right ? right_space : left_space;
            const Size  desired         = content->measure(Size{available_width, hint_height}, measurer, theme_);
            const float width           = std::min(desired.width, available_width);
            const float height          = std::min(desired.height, area.height);
            float       top             = centered ? anchor.center().y - height * 0.5f : anchor.top();
            if (trailing)
            {
                top = anchor.bottom() - height; // left or right, aligned to the anchor's bottom edge
            }
            const float y = std::clamp(top, area.y, std::max(area.y, area.bottom() - height));
            const float x = right ? anchor.right() : std::max(area.x, anchor.left() - width);
            content->arrange(Rect{x, y, width, height});
        }

        open_popups_.push_back(make_open_popup(owner, content, parent));
    }
    sync_focus_popup();
    sync_focus_requests(owners);
}

void Window::sync_focus_requests(const std::vector<Element*>& owners)
{
    // Forget the traps whose owner is gone with its page.
    std::erase_if(focus_traps_,
                  [&owners](const FocusTrap& trap)
                  { return std::find(owners.begin(), owners.end(), trap.owner->get()) == owners.end(); });

    for (Element* owner : owners)
    {
        const FocusRequest request = owner->take_focus_request();
        if (request == FocusRequest::None)
        {
            continue;
        }
        auto trap = std::find_if(focus_traps_.begin(),
                                 focus_traps_.end(),
                                 [owner](const FocusTrap& t) { return t.owner->get() == owner; });
        if (request == FocusRequest::Enter)
        {
            if (trap == focus_traps_.end())
            {
                focus_traps_.push_back(
                    FocusTrap{std::make_unique<ElementWatch>(owner), std::make_unique<ElementWatch>(focused())});
            }
            // The owner narrows its own focusables once it has handed out the request, so the ring
            // is rebuilt before looking for the first stop inside it.
            rebuild_focus_order();
            std::vector<Element*> inside;
            if (Element* root = owner->focus_request_root())
            {
                root->collect_focusables(inside);
            }
            for (Element* candidate : inside)
            {
                if (std::find(focus_order_.begin(), focus_order_.end(), candidate) != focus_order_.end())
                {
                    set_focus(candidate);
                    break;
                }
            }
        }
        else if (trap != focus_traps_.end())
        {
            Element* const previous = trap->previous->get();
            focus_traps_.erase(trap);
            rebuild_focus_order();
            if (previous != nullptr && std::find(focus_order_.begin(), focus_order_.end(), previous) != focus_order_.end())
            {
                set_focus(previous);
            }
        }
    }
}

// ---- windows the loop owns (ui::open_window()) ----

namespace
{
// Leaked on purpose, like the window registry: what is still in it at exit goes with the process.
std::vector<std::unique_ptr<Window>>& owned_windows()
{
    static auto* const windows = new std::vector<std::unique_ptr<Window>>{};
    return *windows;
}
} // namespace

Window& open_window(std::unique_ptr<Window> window)
{
    Window& opened = *owned_windows().emplace_back(std::move(window));
    opened.show();
    return opened;
}

namespace detail
{

Window* ticking_window() noexcept
{
    return current_ticking_window;
}

void release_closed_windows()
{
    std::vector<std::unique_ptr<Window>>& windows = owned_windows();
    // Moved out first: destroying a window runs destructors of its tree, which may open another.
    std::vector<std::unique_ptr<Window>> closed;
    for (auto it = windows.begin(); it != windows.end();)
    {
        if (!(*it)->is_open().get())
        {
            closed.push_back(std::move(*it));
            it = windows.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace detail

NativeHandle Window::native_handle() const
{
    return window_host_ != nullptr ? window_host_->native_handle() : nullptr;
}

} // namespace ui
