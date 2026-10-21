// SPDX-License-Identifier: MIT
#include "winui/ui/Element.hpp"

#include "../render/Compositor.hpp"
#include "OutsidePress.hpp"
#include "PaintTracker.hpp"
#include "winui/core/Callback.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/PerfCounters.hpp"
#include "winui/core/ThreadCheck.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/OverlayShadow.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/DragDrop.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <source_location>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ui
{

namespace
{

// The components an animation can drive, in AnimatedProperty's order of appearance.
enum Channel : std::uint8_t
{
    Opacity,
    Rotation,
    TranslationX,
    TranslationY,
    TranslationZ,
    ScaleX,
    ScaleY,
    ScaleZ,
    CenterX,
    CenterY,
    CenterZ,
    ChannelCount,
};

using ChannelMask   = std::uint16_t;
using ChannelValues = std::array<float, ChannelCount>;

constexpr ChannelMask bit(Channel c)
{
    return static_cast<ChannelMask>(1u << c);
}

ChannelMask channels_of(AnimatedProperty p)
{
    switch (p)
    {
        case AnimatedProperty::Opacity:
            return bit(Opacity);
        case AnimatedProperty::Rotation:
            return bit(Rotation);
        case AnimatedProperty::Translation:
            return bit(TranslationX) | bit(TranslationY) | bit(TranslationZ);
        case AnimatedProperty::TranslationX:
            return bit(TranslationX);
        case AnimatedProperty::TranslationY:
            return bit(TranslationY);
        case AnimatedProperty::TranslationZ:
            return bit(TranslationZ);
        case AnimatedProperty::Scale:
            return bit(ScaleX) | bit(ScaleY) | bit(ScaleZ);
        case AnimatedProperty::ScaleX:
            return bit(ScaleX);
        case AnimatedProperty::ScaleY:
            return bit(ScaleY);
        case AnimatedProperty::ScaleZ:
            return bit(ScaleZ);
        case AnimatedProperty::CenterPoint:
            return bit(CenterX) | bit(CenterY) | bit(CenterZ);
        case AnimatedProperty::CenterPointX:
            return bit(CenterX);
        case AnimatedProperty::CenterPointY:
            return bit(CenterY);
        case AnimatedProperty::CenterPointZ:
            return bit(CenterZ);
    }
    return 0;
}

// Which component of a Vector3 a channel is (0..2), for the Vector3-valued animations.
int component_of(int channel)
{
    switch (channel)
    {
        case TranslationY:
        case ScaleY:
        case CenterY:
            return 1;
        case TranslationZ:
        case ScaleZ:
        case CenterZ:
            return 2;
        default:
            return 0;
    }
}

float pick(const Vector3& v, int component)
{
    return component == 0 ? v.x : (component == 1 ? v.y : v.z);
}

// The curve ScalarTransition and Vector3Transition ease along.
constexpr CubicBezier ImplicitCurve{0.8f, 0.0f, 0.2f, 1.0f};

// A damped harmonic oscillator, solved in closed form: position and velocity (per ms) `t` ms in.
// Composition's natural motion is the same spring (period 2π/ω), integrated by the compositor.
struct SpringState
{
    float position;
    float velocity;
};

SpringState spring_at(float x0, float v0, float target, float damping, double period_ms, double t)
{
    const double omega = 2.0 * std::numbers::pi / std::max(period_ms, 1.0);
    const double a     = x0 - target;
    const double z     = std::max(0.0f, damping);
    double       x     = 0.0;
    double       v     = 0.0;
    if (z < 1.0)
    {
        const double wd    = omega * std::sqrt(1.0 - z * z);
        const double b     = (v0 + z * omega * a) / wd;
        const double decay = std::exp(-z * omega * t);
        const double c     = std::cos(wd * t);
        const double s     = std::sin(wd * t);
        x                  = decay * (a * c + b * s);
        v                  = decay * ((-z * omega) * (a * c + b * s) + (-a * wd * s + b * wd * c));
    }
    else if (z == 1.0)
    {
        const double b     = v0 + omega * a;
        const double decay = std::exp(-omega * t);
        x                  = (a + b * t) * decay;
        v                  = (b - omega * (a + b * t)) * decay;
    }
    else
    {
        const double root = std::sqrt(z * z - 1.0);
        const double r1   = -omega * (z - root);
        const double r2   = -omega * (z + root);
        const double c2   = (v0 - r1 * a) / (r2 - r1);
        const double c1   = a - c2;
        x                 = c1 * std::exp(r1 * t) + c2 * std::exp(r2 * t);
        v                 = c1 * r1 * std::exp(r1 * t) + c2 * r2 * std::exp(r2 * t);
    }
    return SpringState{static_cast<float>(target + x), static_cast<float>(v)};
}

} // namespace

// ---- the private state ------------------------------------------------------------------------

struct ElementMotion
{
    // One running animation, over the channels in `mask`.
    struct Running
    {
        enum class Kind
        {
            Timeline,   // DoubleAnimation
            KeyFrames,  // KeyFrameAnimation
            Spring,     // SpringAnimation
            Expression, // ExpressionAnimation
            Implicit,   // a ScalarTransition/Vector3Transition
        };
        Kind          kind    = Kind::Implicit;
        ChannelMask   mask    = 0;
        double        elapsed = 0.0;
        ChannelValues from{};
        ChannelValues to{};
        ChannelValues velocity{}; // springs: per millisecond
        bool          finished = false;
        bool          sampled  = false; // expressions: `to` holds the last value seen

        DoubleAnimation     timeline{};
        KeyFrameAnimation   key_frames{};
        SpringAnimation     spring{};
        ExpressionAnimation expression{};
        double              implicit_duration = 0.0;
    };

    ChannelValues        base{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<Running> running;

    std::optional<ScalarTransition>  opacity_transition;
    std::optional<ScalarTransition>  rotation_transition;
    std::optional<Vector3Transition> translation_transition;
    std::optional<Vector3Transition> scale_transition;

    TransitionList transitions;
    TransitionList children_transitions;

    TransitionTargetState               target{};
    std::optional<TransitionStoryboard> storyboard;
    double                              storyboard_elapsed = 0.0;
    Callback<void()>                    storyboard_done;

    bool        loaded = false;
    Point       last_offset{};
    std::size_t entrance_stagger   = 0;     // as a parent: children loaded in this layout pass
    std::size_t reposition_stagger = 0;     // ... and moved
    bool        child_deleted      = false; // as a parent: a child began its delete exit since the last arrange
    bool        deleting_pass      = false; // ... and this layout pass is the one closing that gap

    std::vector<ElementWatch*> watches; // cleared, and each watch nulled, with the element

    Callback<void()> pointer_entered;
    Callback<void()> pointer_exited;
    bool             pointer_over = false;
    Callback<void()> got_focus;
    Callback<void()> lost_focus;

    Callback<void(const DragEventArgs&)>         drag_enter;
    Callback<DragResponse(const DragEventArgs&)> drag_over;
    Callback<void(const DragEventArgs&)>         drag_leave;
    Callback<void(const DragEventArgs&)>         drop_handler;

    ElementMotion()                                = default;
    ElementMotion(const ElementMotion&)            = delete;
    ElementMotion& operator=(const ElementMotion&) = delete;
    ~ElementMotion()
    {
        for (ElementWatch* w : watches)
        {
            w->target_ = nullptr;
        }
    }

    float sample(Running& r, int c) const
    {
        using Kind = Running::Kind;
        switch (r.kind)
        {
            case Kind::Implicit:
            {
                const double p = r.implicit_duration > 0.0 ? std::min(1.0, r.elapsed / r.implicit_duration) : 1.0;
                return r.from[c] + (r.to[c] - r.from[c]) * ease(ImplicitCurve, static_cast<float>(p));
            }
            case Kind::Timeline:
            {
                const DoubleAnimation& a = r.timeline;
                if (r.elapsed < a.begin_time.count())
                {
                    return base[c];
                }
                const double one       = std::max(a.duration.count(), 0.0);
                const double iteration = one * (a.auto_reverse ? 2.0 : 1.0);
                const double local     = r.elapsed - a.begin_time.count();
                const double total     = iteration * a.repeat_count;
                double       progress  = 1.0;
                if (iteration <= 0.0)
                {
                    progress = a.auto_reverse ? 0.0 : 1.0;
                }
                else if (local >= total)
                {
                    // The end of the last iteration: a whole number of reversing iterations ends
                    // back at the start.
                    const double whole = a.repeat_count - std::floor(a.repeat_count);
                    const double end   = whole == 0.0 ? iteration : whole * iteration;
                    progress           = end / one;
                    if (a.fill == FillBehavior::Stop)
                    {
                        return base[c];
                    }
                }
                else
                {
                    progress = std::fmod(local, iteration) / one;
                }
                if (progress > 1.0)
                {
                    progress = 2.0 - progress; // the reversing half
                }
                const float eased = a.easing ? ease(*a.easing, static_cast<float>(progress)) : static_cast<float>(progress);
                return r.from[c] + (r.to[c] - r.from[c]) * eased;
            }
            case Kind::KeyFrames:
            {
                const KeyFrameAnimation& a = r.key_frames;
                const double one = a.duration.count() > 0.0 ? a.duration.count() : key_frames_duration(a.key_frames).count();
                const double total = one * a.repeat_count;
                double       t     = r.elapsed;
                if (t >= total)
                {
                    if (a.fill == FillBehavior::Stop)
                    {
                        return base[c];
                    }
                    t = one;
                }
                else if (one > 0.0)
                {
                    t = std::fmod(t, one);
                }
                return sample_key_frames(a.key_frames, r.from[c], Duration{t});
            }
            case Kind::Spring:
            {
                return spring_at(r.from[c], r.velocity[c], r.to[c], r.spring.damping_ratio, r.spring.period.count(), r.elapsed)
                    .position;
            }
            case Kind::Expression:
            {
                return std::visit(
                    [c](auto& f) -> float
                    {
                        using F = std::decay_t<decltype(f)>;
                        if (!f)
                        {
                            return 0.0f;
                        }
                        if constexpr (std::is_same_v<F, Callback<float()>>)
                        {
                            return f();
                        }
                        else
                        {
                            return pick(f(), component_of(c));
                        }
                    },
                    r.expression.expression);
            }
        }
        return base[c];
    }

    // Every channel as drawn now: the property, then each running animation over it in start order.
    ChannelValues current()
    {
        ChannelValues values = base;
        for (Running& r : running)
        {
            for (int c = 0; c < ChannelCount; ++c)
            {
                if ((r.mask & bit(static_cast<Channel>(c))) != 0)
                {
                    values[c] = sample(r, c);
                }
            }
        }
        return values;
    }

    // The per-millisecond velocity of channel `c` in a running spring, for a spring that replaces it.
    std::optional<float> spring_velocity(int c)
    {
        for (Running& r : running)
        {
            if (r.kind == Running::Kind::Spring && (r.mask & bit(static_cast<Channel>(c))) != 0)
            {
                return spring_at(r.from[c], r.velocity[c], r.to[c], r.spring.damping_ratio, r.spring.period.count(), r.elapsed)
                    .velocity;
            }
        }
        return std::nullopt;
    }

    void drop(ChannelMask mask)
    {
        std::erase_if(running, [mask](const Running& r) { return (r.mask & mask) != 0; });
    }

    bool done(const Running& r) const
    {
        using Kind = Running::Kind;
        switch (r.kind)
        {
            case Kind::Implicit:
                return r.elapsed >= r.implicit_duration;
            case Kind::Timeline:
            {
                const DoubleAnimation& a = r.timeline;
                return r.elapsed >= a.begin_time.count() +
                                        std::max(a.duration.count(), 0.0) * (a.auto_reverse ? 2.0 : 1.0) * a.repeat_count;
            }
            case Kind::KeyFrames:
            {
                const KeyFrameAnimation& a = r.key_frames;
                const double one = a.duration.count() > 0.0 ? a.duration.count() : key_frames_duration(a.key_frames).count();
                return r.elapsed >= one * a.repeat_count;
            }
            case Kind::Spring:
            {
                if (r.elapsed > 30000.0)
                {
                    return true;
                }
                for (int c = 0; c < ChannelCount; ++c)
                {
                    if ((r.mask & bit(static_cast<Channel>(c))) == 0)
                    {
                        continue;
                    }
                    const SpringState s = spring_at(r.from[c],
                                                    r.velocity[c],
                                                    r.to[c],
                                                    r.spring.damping_ratio,
                                                    r.spring.period.count(),
                                                    r.elapsed);
                    // At rest: within a thousandth of a unit (a DIP, a scale step) and nearly still.
                    if (std::abs(s.position - r.to[c]) > 0.001f || std::abs(s.velocity) > 0.00005f)
                    {
                        return false;
                    }
                }
                return true;
            }
            case Kind::Expression:
                return false;
        }
        return true;
    }

    // Whether a finished `r` leaves its last value in the property.
    static bool holds_end(const Running& r)
    {
        using Kind = Running::Kind;
        switch (r.kind)
        {
            case Kind::Timeline:
                return r.timeline.fill == FillBehavior::HoldEnd;
            case Kind::KeyFrames:
                return r.key_frames.fill == FillBehavior::HoldEnd;
            default:
                return true;
        }
    }

    // Moves a finite animation to its end, for a tick with animations off. One that never ends (an
    // expression, a forever repeat) keeps its own clock.
    void finish_now(Running& r) const
    {
        using Kind = Running::Kind;
        switch (r.kind)
        {
            case Kind::Implicit:
                r.elapsed = std::max(r.elapsed, r.implicit_duration);
                break;
            case Kind::Timeline:
            {
                const DoubleAnimation& a = r.timeline;
                const double end = a.begin_time.count() +
                                   std::max(a.duration.count(), 0.0) * (a.auto_reverse ? 2.0 : 1.0) * a.repeat_count;
                if (std::isfinite(end))
                {
                    r.elapsed = std::max(r.elapsed, end);
                }
                break;
            }
            case Kind::KeyFrames:
            {
                const KeyFrameAnimation& a = r.key_frames;
                const double one = a.duration.count() > 0.0 ? a.duration.count() : key_frames_duration(a.key_frames).count();
                const double end = one * a.repeat_count;
                if (std::isfinite(end))
                {
                    r.elapsed = std::max(r.elapsed, end);
                }
                break;
            }
            case Kind::Spring:
                // At rest on the target: no overshoot, whatever the damping.
                r.from     = r.to;
                r.velocity = ChannelValues{};
                break;
            case Kind::Expression:
                break;
        }
    }

    TransitionTargetState sample_target() const
    {
        return storyboard ? sample_transition(*storyboard, Duration{storyboard_elapsed}) : TransitionTargetState{};
    }
};

void ElementMotionDeleter::operator()(ElementMotion* motion) const
{
    delete motion;
}

void ElementWatch::reset(Element* element)
{
    if (target_ == element)
    {
        return;
    }
    if (target_ != nullptr && target_->motion_)
    {
        std::erase(target_->motion_->watches, this);
    }
    target_ = element;
    if (target_ != nullptr)
    {
        target_->motion().watches.push_back(this);
    }
}

// ---- Element ----------------------------------------------------------------------------------

// Every member but the identity: the visual id is fresh (its default initializer), the parent
// link stays null and the element starts fully dirty, so the first layout measures it.
Element::Element(Element&& other) noexcept :
    motion_(std::move(other.motion_)),
    visibility_(other.visibility_),
    requested_theme_(other.requested_theme_),
    hit_test_visible_(other.hit_test_visible_),
    allow_drop_(other.allow_drop_),
    has_children_transitions_(other.has_children_transitions_),
    focus_visible_(other.focus_visible_),
    focus_scope_(other.focus_scope_),
    margin_(other.margin_),
    width_(other.width_),
    height_(other.height_),
    min_width_(other.min_width_),
    min_height_(other.min_height_),
    max_width_(other.max_width_),
    max_height_(other.max_height_),
    h_align_(other.h_align_),
    v_align_(other.v_align_),
    bounds_(other.bounds_),
    subscriptions_(std::move(other.subscriptions_)),
    extras_(std::move(other.extras_))
{
    assert(other.parent_ == nullptr && "an element in a tree is never moved");
    assert(!other.keyboard_focused_ && other.focus_within_ == 0 && "an element holding focus is in a tree");
    if (motion_)
    {
        for (ElementWatch* w : motion_->watches)
        {
            w->target_ = this;
        }
    }
}

namespace
{
thread_local std::uint64_t removed_elements_on_thread = 0;
} // namespace

namespace detail
{
void forget_routed(const Element* e); // the pointer routing below
} // namespace detail

Element::~Element()
{
    ++removed_elements_on_thread;
    if (routed_)
    {
        detail::forget_routed(this);
    }
    // The focused element going away with its page: its ancestors stop counting it. They are still
    // alive, since a parent destroys its children before its own Element part.
    if (keyboard_focused_)
    {
        note_keyboard_focus(false);
    }
}

void Element::note_keyboard_focus(bool focused)
{
    if (keyboard_focused_ == focused)
    {
        return;
    }
    keyboard_focused_ = focused;
    for (Element* e = this; e != nullptr; e = e->parent_)
    {
        if (focused)
        {
            ++e->focus_within_;
        }
        else if (e->focus_within_ > 0)
        {
            --e->focus_within_;
        }
    }
}

void Element::set_enabled(bool enabled)
{
    check_thread();
    if (enabled_ == enabled)
    {
        return;
    }
    const bool before = is_enabled();
    enabled_          = enabled;
    if (is_enabled() != before)
    {
        notify_enabled(!before);
    }
}

void Element::set_enabled_gate(bool open)
{
    if (enabled_gate_ == open)
    {
        return;
    }
    const bool before = is_enabled();
    enabled_gate_     = open;
    if (is_enabled() != before)
    {
        notify_enabled(!before);
    }
}

void Element::notify_enabled(bool now)
{
    // Below a locally disabled element nothing changed: it was off and stays off.
    if (!enabled_ || !enabled_gate_)
    {
        if (now)
        {
            return;
        }
    }
    mark_dirty(DirtyFlags::Visual);
    enabled_changed();
    for_each_child(
        [now](Element& child)
        {
            if (child.enabled_ && child.enabled_gate_)
            {
                child.notify_enabled(now);
            }
        });
}

bool Element::key_handled(const KeyEvent& e, bool arrow, bool enter, bool shortcut)
{
    switch (e.key)
    {
        case Key::ArrowUp:
        case Key::ArrowDown:
        case Key::ArrowLeft:
        case Key::ArrowRight:
            return arrow && !e.modifiers.alt;
        case Key::Enter:
            return enter;
        case Key::Tab:
        case Key::Escape:
            return false;
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
            return shortcut;
        default:
            return (e.modifiers.ctrl || e.modifiers.alt) ? shortcut : true;
    }
}

void Element::render_own(Renderer& renderer, const RenderContext& context) const
{
    render_override(renderer, context);
    if (focused_ && focus_visible_)
    {
        if (detail::FocusRingPass* pass = detail::FocusRingPass::active())
        {
            pass->found.push_back(this);
        }
    }
}

namespace detail
{

namespace
{
FocusRingPass*& active_focus_ring_pass() noexcept
{
    thread_local FocusRingPass* pass = nullptr;
    return pass;
}

// The affine map a child's coordinates are drawn through, read off map_to_children() (which goes
// the other way) at (0,0), (1,0) and (0,1).
std::optional<Matrix> children_to_parent(Point o, Point x, Point y)
{
    if (o == Point{0.0f, 0.0f} && x == Point{1.0f, 0.0f} && y == Point{0.0f, 1.0f})
    {
        return std::nullopt;
    }
    const Matrix to_children{x.x - o.x, x.y - o.y, y.x - o.x, y.y - o.y, o.x, o.y};
    return to_children.inverted();
}
} // namespace

FocusRingPass::FocusRingPass() noexcept : outer_(active_focus_ring_pass())
{
    active_focus_ring_pass() = this;
}

FocusRingPass::~FocusRingPass()
{
    active_focus_ring_pass() = outer_;
}

FocusRingPass* FocusRingPass::active() noexcept
{
    return active_focus_ring_pass();
}

void render_focus_rings(Renderer& renderer, const Element& root, const Theme& theme, const FocusRingPass& pass)
{
    for (const Element* focused : pass.found)
    {
        if (!focused->draws_focus_ring() || !focused->focusable())
        {
            continue;
        }
        std::vector<const Element*> chain; // root first
        for (const Element* e = focused; e != nullptr; e = e->parent_)
        {
            chain.push_back(e);
            if (e == &root)
            {
                break;
            }
        }
        if (chain.back() != &root)
        {
            continue; // drawn under another root
        }
        std::reverse(chain.begin(), chain.end());

        const Theme*      scoped = &theme;
        std::vector<bool> pushed; // true for a clip, false for a transform, in push order
        for (const Element* e : chain)
        {
            if (e->requested_theme_ != ElementTheme::Default)
            {
                scoped = &e->scoped_theme(*scoped);
            }
            if (e->motion_)
            {
                if (const Matrix m = e->visual_transform(); !m.is_identity())
                {
                    renderer.push_transform(m);
                    pushed.push_back(false);
                }
            }
            if (e == focused)
            {
                break;
            }
            if (const std::optional<Rect> clip = e->children_hit_clip())
            {
                renderer.push_clip(*clip);
                pushed.push_back(true);
            }
            if (const std::optional<Matrix> m = children_to_parent(e->map_to_children(Point{0.0f, 0.0f}),
                                                                   e->map_to_children(Point{1.0f, 0.0f}),
                                                                   e->map_to_children(Point{0.0f, 1.0f})))
            {
                renderer.push_transform(*m);
                pushed.push_back(false);
            }
        }

        const FocusRingStyle style  = scoped->focus_ring_style();
        Rect                 rect   = focused->focus_ring_rect(*scoped);
        const float          radius = focused->focus_ring_corner_radius(*scoped);
        if (focused->focus_ring_inside())
        {
            rect = rect.inset(Thickness{style.outward_extent()}); // the rings' outer edge on the rect
        }
        if (!rect.empty())
        {
            draw_focus_ring(renderer, rect, radius, style);
        }
        for (auto it = pushed.rbegin(); it != pushed.rend(); ++it)
        {
            if (*it)
            {
                renderer.pop_clip();
            }
            else
            {
                renderer.pop_transform();
            }
        }
    }
}

} // namespace detail

void Element::set_focused(bool focused)
{
    if (focused_ == focused)
    {
        return;
    }
    focused_ = focused;
    mark_dirty(DirtyFlags::Visual);
    focus_changed(focused);
}

void Element::set_focus_visible(bool visible)
{
    if (focus_visible_ == visible)
    {
        return;
    }
    focus_visible_ = visible;
    mark_dirty(DirtyFlags::Visual);
    focus_visible_changed(visible);
}

void Element::notify_window(bool attached)
{
    attached ? this->attached() : this->detached();
    for_each_child([attached](Element& child) { child.notify_window(attached); });
}

void Element::unlink_subscriptions() noexcept
{
    for (Subscription& subscription : subscriptions_)
    {
        subscription.unlink();
    }
    for_each_child([](Element& child) { child.unlink_subscriptions(); });
}

void Element::set_parent(Element* parent)
{
    if (parent_ == parent)
    {
        return;
    }
    Window* const old_window  = window();
    const bool    old_enabled = is_enabled();
    for (Element* e = parent_; e != nullptr && focus_within_ > 0; e = e->parent_)
    {
        e->focus_within_ -= std::min(e->focus_within_, focus_within_);
    }
    parent_ = parent;
    if (parent_ != nullptr)
    {
        for (Element* e = parent_; e != nullptr && focus_within_ > 0; e = e->parent_)
        {
            e->focus_within_ += focus_within_;
        }
        if (any(dirty_))
        {
            parent_->mark_dirty(dirty_);
        }
    }
    if (enabled_ && enabled_gate_ && is_enabled() != old_enabled)
    {
        notify_enabled(!old_enabled);
    }
    if (Window* const new_window = window(); new_window != old_window)
    {
        if (old_window != nullptr)
        {
            notify_window(false);
        }
        if (new_window != nullptr)
        {
            notify_window(true);
        }
    }
}

// ---- the tree-wide walks: each recurses through for_each_child() ----

void Element::collect_focusables(std::vector<Element*>& out)
{
    if (!enabled_ || !enabled_gate_)
    {
        return; // nothing inside a disabled element takes focus
    }
    if (focusable())
    {
        out.push_back(this);
    }
    for_each_child([&out](Element& child) { child.collect_focusables(out); });
}

void Element::collect_live(std::vector<Element*>& out)
{
    out.push_back(this);
    for_each_child([&out](Element& child) { child.collect_live(out); });
}

// ---- popup ----

void Element::open_popup(PopupRequest request)
{
    check_thread();
    Extras&             x   = extras();
    const PopupRequest* old = x.popup.get();
    // Callbacks never compare equal, so only whether they are set counts: an owner re-sending
    // the same request from arrange_override() does not ask for another layout.
    const bool changed = old == nullptr || old->content != request.content || old->anchor != request.anchor ||
                         old->anchor_rect != request.anchor_rect || old->placement != request.placement ||
                         old->placement_alignment != request.placement_alignment ||
                         old->light_dismiss != request.light_dismiss || old->takes_focus != request.takes_focus ||
                         old->chrome.shadow != request.chrome.shadow ||
                         static_cast<bool>(old->chrome.corner_radius) != static_cast<bool>(request.chrome.corner_radius) ||
                         old->chrome.elevation != request.chrome.elevation ||
                         static_cast<bool>(old->chrome.shadow_rect) != static_cast<bool>(request.chrome.shadow_rect) ||
                         static_cast<bool>(old->place) != static_cast<bool>(request.place);
    if (x.popup)
    {
        *x.popup = std::move(request);
    }
    else
    {
        x.popup = std::make_unique<PopupRequest>(std::move(request));
    }
    ++x.popup_generation;
    if (changed)
    {
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void Element::close_popup()
{
    check_thread();
    if (!popup_open())
    {
        return;
    }
    extras_->popup.reset();
    ++extras_->popup_generation;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

bool Element::place_popup(Element& content, Size client, const MeasureContext& context)
{
    PopupRequest* request = popup_request();
    if (request == nullptr || !request->place)
    {
        return false;
    }
    // Moved out for the call, as on_dismiss is: the callback may close the popup.
    auto                place      = std::move(request->place);
    const std::uint64_t generation = extras_->popup_generation;
    const ElementWatch  watch{this};
    const bool          placed = place(content, client, context);
    if (watch.get() != nullptr && extras_->popup && extras_->popup_generation == generation)
    {
        extras_->popup->place = std::move(place);
    }
    return placed;
}

void Element::dismiss_popup(PopupCloseReason reason)
{
    PopupRequest* request = popup_request();
    if (request == nullptr)
    {
        return;
    }
    if (!request->on_dismiss)
    {
        close_popup();
        return;
    }
    // Moved out for the call: the handler may close the popup, reopen it or destroy this element.
    Callback<void(PopupCloseReason)> handler    = std::move(request->on_dismiss);
    const std::uint64_t              generation = extras_->popup_generation;
    const ElementWatch               watch{this};
    handler(reason);
    if (watch.get() != nullptr && extras_->popup && extras_->popup_generation == generation)
    {
        extras_->popup->on_dismiss = std::move(handler);
    }
}

void Element::automation_children(std::vector<Element*>& out) const
{
    for_each_child([&out](Element& child) { out.push_back(&child); });
}

void Element::check_tree_thread(const std::source_location& where) const
{
    // Defined either way: a consumer built with checks calls it from mark_dirty() even when the
    // library was built without.
    // Up to the root, which carries its Window's thread when it is that window's content or modal.
    // On the wrong thread this walk itself races the owner; a report is all it is good for then.
    const Element* root = this;
    while (root->parent_ != nullptr)
    {
        root = root->parent_;
    }
    if (root->tree_thread_ != std::thread::id{} && root->tree_thread_ != std::this_thread::get_id())
    {
        detail::report_thread_violation({}, "the tree", root->tree_thread_, where);
    }
}

void Element::clear_dirty(DirtyFlags flags)
{
    // A clean element has a clean subtree (flags bubble up), so the walk stops there.
    if (!any(dirty_ & flags))
    {
        return;
    }
    dirty_ &= ~flags;
    // Layout skipped what is under a Collapsed element. A change in there, like an item added to a
    // hidden page's list, has to be measured once the element shows again, and every ancestor of
    // the change must still say so, or a clean one returns its cached size before reaching it.
    if (visibility_ == Visibility::Collapsed)
    {
        return;
    }
    for_each_child([flags](Element& child) { child.clear_dirty(flags); });
    clear_dirty_override(flags);
}

void Element::build_visual_tree(VisualTree& tree, VisualId parent_id) const
{
    tree.add_node(visual_id_, parent_id, bounds_, dirty_);
    for_each_child([&tree, this](Element& child) { child.build_visual_tree(tree, visual_id_); });
    build_visual_tree_override(tree);
}

bool Element::focus()
{
    Window* const host = window();
    return host != nullptr && host->focus(*this);
}

detail::LayerPlacement Element::layer_placement(std::size_t index) const
{
    (void)index;
    return detail::LayerPlacement{bounds_, std::nullopt};
}

void Element::sync_composition(detail::Compositor& compositor, const Theme& theme)
{
    sync_composition_override(compositor, theme);
    for_each_child([&compositor, &theme](Element& child) { child.sync_composition(compositor, theme); });
}

void Element::sync_native(NativeHost& host)
{
    for_each_child([&host](Element& child) { child.sync_native(host); });
}

void Element::set_screen_tracking(bool on)
{
    if (tracks_screen_ == on)
    {
        return;
    }
    tracks_screen_ = on;
    on_screen_     = true;
    if (on)
    {
        mark_dirty(DirtyFlags::Visual); // a frame, so the Window measures where it is
    }
}

void Element::drag_enter(const DragEventArgs& e)
{
    if (motion_ && motion_->drag_enter)
    {
        motion_->drag_enter(e);
    }
}

DragResponse Element::drag_over(const DragEventArgs& e)
{
    if (motion_ && motion_->drag_over)
    {
        return motion_->drag_over(e);
    }
    return {};
}

void Element::drag_leave(const DragEventArgs& e)
{
    if (motion_ && motion_->drag_leave)
    {
        motion_->drag_leave(e);
    }
}

void Element::drop(const DragEventArgs& e)
{
    if (motion_ && motion_->drop_handler)
    {
        motion_->drop_handler(e);
    }
}

bool Element::raise_context_requested_in(Element& root, std::optional<Point> position)
{
    Element* deepest = &root;
    if (position)
    {
        // Down the topmost child under the point at each level, through the visual children: a
        // flyout lists no automation children, yet its anchor is what was clicked.
        for (bool descended = true; descended;)
        {
            descended = false;
            std::vector<Element*> children;
            deepest->for_each_child([&children](Element& child) { children.push_back(&child); });
            for (auto it = children.rbegin(); it != children.rend(); ++it)
            {
                Element* child = *it;
                if (child != nullptr && child->visibility_ == Visibility::Visible && child->bounds_.contains(*position))
                {
                    deepest   = child;
                    descended = true;
                    break;
                }
            }
        }
    }
    for (Element* e = deepest; e != nullptr; e = e->parent_)
    {
        if (e->raise_context_requested(position))
        {
            return true;
        }
        if (e == &root)
        {
            break;
        }
    }
    return false;
}

void Element::on_drag_enter(Callback<void(const DragEventArgs&)> handler)
{
    check_thread();
    motion().drag_enter = std::move(handler);
}

void Element::on_drag_over(Callback<DragResponse(const DragEventArgs&)> handler)
{
    check_thread();
    motion().drag_over = std::move(handler);
}

void Element::on_drag_leave(Callback<void(const DragEventArgs&)> handler)
{
    check_thread();
    motion().drag_leave = std::move(handler);
}

void Element::on_drop(Callback<void(const DragEventArgs&)> handler)
{
    check_thread();
    motion().drop_handler = std::move(handler);
}

Rect Element::effective_viewport() const
{
    // Every scrolling ancestor shows its content under its own bounds (unzoomed), and the root is
    // the client area. Not clipped to the element's own bounds.
    constexpr float Huge    = std::numeric_limits<float>::max() / 4.0f;
    Rect            visible = Rect{-Huge, -Huge, 2.0f * Huge, 2.0f * Huge};
    for (const Element* a = parent_; a != nullptr; a = a->parent_)
    {
        if (const std::optional<ScrollInfo> info = a->scroll_info())
        {
            const float zoom = info->zoom > 0.0f ? info->zoom : 1.0f;
            visible = intersection(visible,
                                   Rect{a->bounds_.x, a->bounds_.y, a->bounds_.width / zoom, a->bounds_.height / zoom});
        }
        else if (a->parent_ == nullptr)
        {
            visible = intersection(visible, a->bounds_);
        }
        if (visible.empty())
        {
            return Rect{};
        }
    }
    return visible;
}

namespace
{
// RequestedTheme re-resolves the theme in the other mode, which would drop whatever the app edited
// on the theme it handed the window. The edits that are not colors (metrics, type, motion, the
// focus ring's widths, the era's visual choices) carry over; colors stay the other mode's, the way
// a theme dictionary's do. `resolved` is the edited theme's own mode as resolved, which tells an
// edit from the resolver's value.
void carry_token_edits(const Theme& edited, const Theme& resolved, Theme& target)
{
    const auto carry = [&](auto field)
    {
        if (!(field(edited) == field(resolved)))
        {
            field(target) = field(edited);
        }
    };
    carry([](auto& t) -> auto& { return t.corner_radius; });
    carry([](auto& t) -> auto& { return t.overlay_corner_radius; });
    carry([](auto& t) -> auto& { return t.button_corner_radius; });
    carry([](auto& t) -> auto& { return t.padding; });
    carry([](auto& t) -> auto& { return t.border_width; });
    carry([](auto& t) -> auto& { return t.sizing; });
    carry([](auto& t) -> auto& { return t.focus_ring_outer_width; });
    carry([](auto& t) -> auto& { return t.focus_ring_inner_width; });
    carry([](auto& t) -> auto& { return t.focus_ring_margin; });
    carry([](auto& t) -> auto& { return t.anim_faster; });
    carry([](auto& t) -> auto& { return t.anim_fast; });
    carry([](auto& t) -> auto& { return t.anim_normal; });
    carry([](auto& t) -> auto& { return t.anim_fast_out_slow_in; });
    carry([](auto& t) -> auto& { return t.animations_enabled; });
    carry([](auto& t) -> auto& { return t.text_style; });
    carry([](auto& t) -> auto& { return t.chrome_text_style; });
    carry([](auto& t) -> auto& { return t.type.header; });
    carry([](auto& t) -> auto& { return t.type.subheader; });
    carry([](auto& t) -> auto& { return t.type.title; });
    carry([](auto& t) -> auto& { return t.type.subtitle; });
    carry([](auto& t) -> auto& { return t.type.body; });
    carry([](auto& t) -> auto& { return t.type.caption; });
    carry([](auto& t) -> auto& { return t.type.body_strong; });
    carry([](auto& t) -> auto& { return t.page.grid_unit; });
    carry([](auto& t) -> auto& { return t.page.content_margin; });
    carry([](auto& t) -> auto& { return t.page.header_height; });
    carry([](auto& t) -> auto& { return t.page.item_spacing; });
    carry([](auto& t) -> auto& { return t.page.command_bar_height; });
    carry([](auto& t) -> auto& { return t.rasterization; });
    carry([](auto& t) -> auto& { return t.focus_visual; });
    carry([](auto& t) -> auto& { return t.divider_visual; });
    carry([](auto& t) -> auto& { return t.disclosure_visual; });
    carry([](auto& t) -> auto& { return t.popup_motion; });
    carry([](auto& t) -> auto& { return t.icon_font_family; });
}
} // namespace

ArrangeContext Element::arrange_context() const
{
    // Arranged before it was ever measured: a measurer that sizes everything at nothing.
    struct Unmeasured final : TextMeasurer
    {
        TextMetrics measure(std::string_view, const TextStyle&) const override
        {
            return {};
        }
    };
    static const Unmeasured unmeasured;
    const Theme&            handed = measured_theme_ != nullptr ? *measured_theme_ : Theme::default_light();
    return {measured_with_ != nullptr ? *measured_with_ : unmeasured,
            requested_theme_ == ElementTheme::Default ? handed : scoped_theme(handed)};
}

const Theme& Element::scoped_theme(const Theme& handed) const
{
    const bool dark = requested_theme_ == ElementTheme::Dark;
    // Nothing to switch to, or already that mode: the handed theme as it is.
    if (!handed.with_dark_mode || handed.dark == dark)
    {
        // Only measure and render hand their own theme in, never a temporary.
        return handed; // NOLINT(bugprone-return-const-ref-from-parameter)
    }
    Extras& x = extras();
    if (!x.scoped_theme || x.scoped_generation != handed.generation() || x.scoped_dark != dark)
    {
        Theme other = handed.with_dark_mode(dark);
        carry_token_edits(handed, handed.with_dark_mode(handed.dark), other);
        x.scoped_theme      = std::make_unique<Theme>(std::move(other));
        x.scoped_generation = handed.generation();
        x.scoped_dark       = dark;
    }
    return *x.scoped_theme;
}

void Element::mark_subtree_dirty(DirtyFlags flags)
{
    mark_dirty(flags);
    for_each_child([flags](Element& child) { child.mark_subtree_dirty(flags); });
}

ElementMotion& Element::motion()
{
    if (!motion_)
    {
        motion_.reset(new ElementMotion{});
    }
    return *motion_;
}

namespace
{

// Starts an implicit transition from what is on screen when one is configured and the element is
// live. `animated` is the subset of `mask` the transition covers; the rest snap.
void set_channels(ElementMotion& m, bool live, ChannelMask mask, const ChannelValues& to, std::optional<double> duration, ChannelMask animated)
{
    const ChannelValues shown = m.current();
    bool                any   = false;
    for (int c = 0; c < ChannelCount; ++c)
    {
        if ((mask & bit(static_cast<Channel>(c))) != 0 && m.base[c] != to[c])
        {
            any = true;
        }
    }
    if (!any)
    {
        return; // the equality gate: an unchanged value disturbs nothing
    }
    for (int c = 0; c < ChannelCount; ++c)
    {
        if ((mask & bit(static_cast<Channel>(c))) != 0)
        {
            m.base[c] = to[c];
        }
    }
    if (!animations_enabled())
    {
        m.drop(mask); // lands now, over a transition already running too
        return;
    }
    if (!duration || !live)
    {
        return;
    }
    animated &= mask;
    m.drop(mask);
    if (animated == 0)
    {
        return;
    }
    ElementMotion::Running r;
    r.kind              = ElementMotion::Running::Kind::Implicit;
    r.mask              = animated;
    r.implicit_duration = std::max(*duration, 1.0); // Composition's 1 ms floor
    r.from              = shown;
    r.to                = m.base;
    m.running.push_back(std::move(r));
}

ChannelMask vector_mask(Channel x, Vector3Components components)
{
    ChannelMask mask = 0;
    if (has_component(components, Vector3Components::X))
    {
        mask |= bit(x);
    }
    if (has_component(components, Vector3Components::Y))
    {
        mask |= bit(static_cast<Channel>(x + 1));
    }
    if (has_component(components, Vector3Components::Z))
    {
        mask |= bit(static_cast<Channel>(x + 2));
    }
    return mask;
}

constexpr ChannelMask Translation = bit(TranslationX) | bit(TranslationY) | bit(TranslationZ);
constexpr ChannelMask Scale       = bit(ScaleX) | bit(ScaleY) | bit(ScaleZ);
constexpr ChannelMask Center      = bit(CenterX) | bit(CenterY) | bit(CenterZ);

} // namespace

float Element::opacity() const
{
    return motion_ ? motion_->base[Opacity] : 1.0f;
}

void Element::set_opacity(float opacity)
{
    ElementMotion& m  = motion();
    ChannelValues  to = m.base;
    to[Opacity]       = std::clamp(opacity, 0.0f, 1.0f);
    set_channels(m,
                 laid_out_,
                 bit(Opacity),
                 to,
                 m.opacity_transition ? std::optional<double>{m.opacity_transition->duration.count()} : std::nullopt,
                 bit(Opacity));
    mark_dirty(DirtyFlags::Visual);
}

Vector3 Element::translation() const
{
    return motion_ ? Vector3{motion_->base[TranslationX], motion_->base[TranslationY], motion_->base[TranslationZ]}
                   : Vector3{};
}

void Element::set_translation(Vector3 translation)
{
    ElementMotion& m  = motion();
    ChannelValues  to = m.base;
    to[TranslationX]  = translation.x;
    to[TranslationY]  = translation.y;
    to[TranslationZ]  = translation.z;
    const auto& tr    = m.translation_transition;
    set_channels(m,
                 laid_out_,
                 Translation,
                 to,
                 tr ? std::optional<double>{tr->duration.count()} : std::nullopt,
                 tr ? vector_mask(TranslationX, tr->components) : ChannelMask{0});
    mark_dirty(DirtyFlags::Visual);
}

float Element::rotation() const
{
    return motion_ ? motion_->base[Rotation] : 0.0f;
}

void Element::set_rotation(float degrees)
{
    ElementMotion& m  = motion();
    ChannelValues  to = m.base;
    to[Rotation]      = degrees;
    set_channels(m,
                 laid_out_,
                 bit(Rotation),
                 to,
                 m.rotation_transition ? std::optional<double>{m.rotation_transition->duration.count()} : std::nullopt,
                 bit(Rotation));
    mark_dirty(DirtyFlags::Visual);
}

Vector3 Element::scale() const
{
    return motion_ ? Vector3{motion_->base[ScaleX], motion_->base[ScaleY], motion_->base[ScaleZ]}
                   : Vector3{1.0f, 1.0f, 1.0f};
}

void Element::set_scale(Vector3 scale)
{
    ElementMotion& m  = motion();
    ChannelValues  to = m.base;
    to[ScaleX]        = scale.x;
    to[ScaleY]        = scale.y;
    to[ScaleZ]        = scale.z;
    const auto& tr    = m.scale_transition;
    set_channels(m,
                 laid_out_,
                 Scale,
                 to,
                 tr ? std::optional<double>{tr->duration.count()} : std::nullopt,
                 tr ? vector_mask(ScaleX, tr->components) : ChannelMask{0});
    mark_dirty(DirtyFlags::Visual);
}

Vector3 Element::center_point() const
{
    return motion_ ? Vector3{motion_->base[CenterX], motion_->base[CenterY], motion_->base[CenterZ]} : Vector3{};
}

void Element::set_center_point(Vector3 center)
{
    ElementMotion& m  = motion();
    ChannelValues  to = m.base;
    to[CenterX]       = center.x;
    to[CenterY]       = center.y;
    to[CenterZ]       = center.z;
    set_channels(m, laid_out_, Center, to, std::nullopt, 0);
    mark_dirty(DirtyFlags::Visual);
}

float Element::current_opacity() const
{
    return motion_ ? motion_->current()[Opacity] : 1.0f;
}

Vector3 Element::current_translation() const
{
    if (!motion_)
    {
        return Vector3{};
    }
    const ChannelValues v = motion_->current();
    return Vector3{v[TranslationX], v[TranslationY], v[TranslationZ]};
}

float Element::current_rotation() const
{
    return motion_ ? motion_->current()[Rotation] : 0.0f;
}

Vector3 Element::current_scale() const
{
    if (!motion_)
    {
        return Vector3{1.0f, 1.0f, 1.0f};
    }
    const ChannelValues v = motion_->current();
    return Vector3{v[ScaleX], v[ScaleY], v[ScaleZ]};
}

Vector3 Element::current_center_point() const
{
    if (!motion_)
    {
        return Vector3{};
    }
    const ChannelValues v = motion_->current();
    return Vector3{v[CenterX], v[CenterY], v[CenterZ]};
}

void Element::set_opacity_transition(std::optional<ScalarTransition> transition)
{
    motion().opacity_transition = transition;
}
void Element::set_rotation_transition(std::optional<ScalarTransition> transition)
{
    motion().rotation_transition = transition;
}
void Element::set_translation_transition(std::optional<Vector3Transition> transition)
{
    motion().translation_transition = transition;
}
void Element::set_scale_transition(std::optional<Vector3Transition> transition)
{
    motion().scale_transition = transition;
}
std::optional<ScalarTransition> Element::opacity_transition() const
{
    return motion_ ? motion_->opacity_transition : std::nullopt;
}
std::optional<ScalarTransition> Element::rotation_transition() const
{
    return motion_ ? motion_->rotation_transition : std::nullopt;
}
std::optional<Vector3Transition> Element::translation_transition() const
{
    return motion_ ? motion_->translation_transition : std::nullopt;
}
std::optional<Vector3Transition> Element::scale_transition() const
{
    return motion_ ? motion_->scale_transition : std::nullopt;
}

void Element::start_animation(AnimatedProperty property, Animation animation)
{
    using Running           = ElementMotion::Running;
    ElementMotion&    m     = motion();
    const ChannelMask mask  = channels_of(property);
    const auto        shown = m.current();

    Running r;
    r.mask = mask;
    r.from = shown;
    r.to   = m.base;
    std::visit(
        [&](auto& a)
        {
            using A = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<A, DoubleAnimation>)
            {
                r.kind = Running::Kind::Timeline;
                for (int c = 0; c < ChannelCount; ++c)
                {
                    if (a.from)
                    {
                        r.from[c] = *a.from;
                    }
                    if (a.to)
                    {
                        r.to[c] = *a.to;
                    }
                    else if (a.by)
                    {
                        r.to[c] = r.from[c] + *a.by;
                    }
                }
                r.timeline = std::move(a);
            }
            else if constexpr (std::is_same_v<A, KeyFrameAnimation>)
            {
                r.kind       = Running::Kind::KeyFrames;
                r.key_frames = std::move(a);
            }
            else if constexpr (std::is_same_v<A, SpringAnimation>)
            {
                r.kind = Running::Kind::Spring;
                for (int c = 0; c < ChannelCount; ++c)
                {
                    const int component = component_of(c);
                    r.to[c]             = pick(a.final_value, component);
                    if (a.initial_value)
                    {
                        r.from[c] = pick(*a.initial_value, component);
                    }
                    if (a.initial_velocity)
                    {
                        r.velocity[c] = pick(*a.initial_velocity, component) / 1000.0f; // per second -> per ms
                    }
                    else if ((mask & bit(static_cast<Channel>(c))) != 0)
                    {
                        r.velocity[c] = m.spring_velocity(c).value_or(0.0f);
                    }
                }
                r.spring = std::move(a);
            }
            else
            {
                r.kind       = Running::Kind::Expression;
                r.expression = std::move(a);
            }
        },
        animation);

    m.drop(mask);
    m.running.push_back(std::move(r));
    mark_dirty(DirtyFlags::Visual);
}

void Element::stop_animation(AnimatedProperty property)
{
    if (motion_)
    {
        motion_->drop(channels_of(property));
        mark_dirty(DirtyFlags::Visual);
    }
}

bool Element::is_animating(AnimatedProperty property) const
{
    if (!motion_)
    {
        return false;
    }
    const ChannelMask mask = channels_of(property);
    return std::any_of(motion_->running.begin(),
                       motion_->running.end(),
                       [mask](const ElementMotion::Running& r) { return (r.mask & mask) != 0; });
}

namespace
{
const TransitionList NoTransitions{};
}

const TransitionList& Element::transitions() const
{
    return motion_ ? motion_->transitions : NoTransitions;
}

void Element::set_transitions(TransitionList transitions)
{
    motion().transitions = std::move(transitions);
}

const TransitionList& Element::children_transitions() const
{
    return motion_ ? motion_->children_transitions : NoTransitions;
}

void Element::set_children_transitions(TransitionList transitions)
{
    has_children_transitions_     = !transitions.empty();
    motion().children_transitions = std::move(transitions);
}

const TransitionTargetState& Element::transition_target() const
{
    static const TransitionTargetState Identity{};
    return motion_ ? motion_->target : Identity;
}

void Element::set_transition_target(const TransitionTargetState& state)
{
    ElementMotion& m = motion();
    if (m.target != state)
    {
        m.target = state;
        mark_dirty(DirtyFlags::Visual);
    }
}

void Element::run_transition(TransitionStoryboard storyboard, Callback<void()> on_completed)
{
    ElementMotion& m = motion();
    if (storyboard.empty())
    {
        m.storyboard.reset();
        m.target = TransitionTargetState{};
        mark_dirty(DirtyFlags::Visual);
        if (on_completed)
        {
            on_completed();
        }
        return;
    }
    m.storyboard         = std::move(storyboard);
    m.storyboard_elapsed = 0.0;
    m.storyboard_done    = std::move(on_completed);
    if (!animations_enabled())
    {
        // Drawn at its end from the start; it completes on the next tick, as a played one would.
        m.storyboard_elapsed = m.storyboard->duration().count();
    }
    m.target = m.sample_target();
    mark_dirty(DirtyFlags::Visual);
}

bool Element::transition_running() const
{
    return motion_ && motion_->storyboard.has_value();
}

Matrix Element::visual_transform() const
{
    if (!motion_)
    {
        return Matrix{};
    }
    const ChannelValues          v  = motion_->current();
    const TransitionTargetState& tt = motion_->target;

    Matrix      m{};
    const float cx = v[CenterX];
    const float cy = v[CenterY];
    if (v[ScaleX] != 1.0f || v[ScaleY] != 1.0f)
    {
        m = m * Matrix::translation(-cx, -cy) * Matrix::scale(v[ScaleX], v[ScaleY]) * Matrix::translation(cx, cy);
    }
    if (v[Rotation] != 0.0f)
    {
        m = m * Matrix::translation(-cx, -cy) * Matrix::rotation(v[Rotation]) * Matrix::translation(cx, cy);
    }
    if (v[TranslationX] != 0.0f || v[TranslationY] != 0.0f)
    {
        m = m * Matrix::translation(v[TranslationX], v[TranslationY]);
    }
    if (!m.is_identity())
    {
        m = Matrix::translation(-bounds_.x, -bounds_.y) * m * Matrix::translation(bounds_.x, bounds_.y);
    }
    // The transition target last, about the layout rect (window coordinates already).
    return m * transition_transform(tt, bounds_);
}

void Element::on_pointer_entered(Callback<void()> handler)
{
    check_thread();
    motion().pointer_entered = std::move(handler);
}

void Element::on_pointer_exited(Callback<void()> handler)
{
    check_thread();
    motion().pointer_exited = std::move(handler);
}

void Element::on_got_focus(Callback<void()> handler)
{
    check_thread();
    motion().got_focus = std::move(handler);
}

void Element::on_lost_focus(Callback<void()> handler)
{
    check_thread();
    motion().lost_focus = std::move(handler);
}

bool Element::has_focus_handler() const noexcept
{
    return motion_ && (motion_->got_focus || motion_->lost_focus);
}

void Element::raise_focus_event(bool got)
{
    if (motion_)
    {
        if (Callback<void()>& handler = got ? motion_->got_focus : motion_->lost_focus)
        {
            handler();
        }
    }
}

// ---- pointer routing ----

namespace detail
{

void notify_outside_press(const std::vector<Element*>& hit);

// The pointer's state at one dispatch root: what it is over, and who holds the capture. Both are
// paths from the root down, so a Leave can be delivered to everything the pointer left. An element
// destroyed while on one is nulled out (forget()), and a path ends at its first null.
struct PointerRoute
{
    std::vector<Element*> hover;
    std::vector<Element*> captured;
    PointerButton         capture_button = PointerButton::None;

    PointerRoute();
    ~PointerRoute();
    PointerRoute(const PointerRoute&)            = delete;
    PointerRoute& operator=(const PointerRoute&) = delete;
};

void PointerRouteDeleter::operator()(PointerRoute* route) const noexcept
{
    delete route;
}

// One handle_pointer()/handle_wheel() running on this thread. A nested call into an element this
// dispatch already reaches does nothing, so a container that still hands events to its children
// does not deliver them twice.
struct PointerDispatch
{
    using Path = std::vector<Element*>;

    PointerRoute*  route = nullptr;
    const Element* root  = nullptr; // the dispatch root; null for a Leave sent on the side
    Path           path;            // what this event is delivered to, root first
    Point          position{};      // the event's, in the root's parent's coordinates
    // handle_pointer()'s other paths, here so ~Element() nulls them too.
    Path hit;
    Path hovered;
    Path captured;

    static std::vector<PointerDispatch*>& stack()
    {
        thread_local std::vector<PointerDispatch*> active;
        return active;
    }
    static std::vector<PointerRoute*>& routes()
    {
        thread_local std::vector<PointerRoute*> all;
        return all;
    }

    // ~Element(): no route or running dispatch may keep pointing at it.
    static void forget(const Element* e)
    {
        const auto clear = [e](Path& path)
        { std::replace(path.begin(), path.end(), const_cast<Element*>(e), static_cast<Element*>(nullptr)); };
        for (PointerRoute* r : routes())
        {
            clear(r->hover);
            clear(r->captured);
        }
        for (PointerDispatch* d : stack())
        {
            clear(d->path);
            clear(d->hit);
            clear(d->hovered);
            clear(d->captured);
        }
    }

    // Whether a dispatch running now delivers to `e` already, so a container that still hands the
    // event on to its children does not deliver it twice. A child the routing did not pick is
    // handed it as before, as a dispatch of its own.
    static bool reaches(const Element* e)
    {
        for (const PointerDispatch* d : stack())
        {
            if (contains(d->path, e) || contains(d->hit, e))
            {
                return true;
            }
        }
        return false;
    }

    // The topmost element under `p` (in `e`'s parent's coordinates), and the way down to it.
    static bool hit_test(Element& e, Point p, bool root, Path& out)
    {
        if (e.visibility_ != Visibility::Visible || !e.hit_test_visible_ || e.hit_test_transparent() ||
            (root ? !e.is_enabled() : (!e.enabled_ || !e.enabled_gate_)))
        {
            return false;
        }
        const Point local = e.to_local(p);
        if (!e.bounds_.contains(local))
        {
            return false;
        }
        out.push_back(&e);
        e.routed_ = true;
        if (e.covers_children_at(local))
        {
            return true;
        }
        if (const std::optional<Rect> clip = e.children_hit_clip(); clip && !clip->contains(local))
        {
            return true;
        }
        const Point           inner = e.map_to_children(local);
        std::vector<Element*> children;
        e.hit_test_children(local, children);
        for (Element* child : children)
        {
            if (hit_test(*child, inner, false, out))
            {
                return true;
            }
        }
        return true;
    }

    // Each path element's own coordinates for a point given in the root's parent's.
    static std::vector<Point> locals(const Path& path, Point p)
    {
        std::vector<Point> out;
        out.reserve(path.size());
        for (const Element* e : path)
        {
            if (e == nullptr)
            {
                break;
            }
            const Point local = e->to_local(p);
            out.push_back(local);
            p = e->map_to_children(local);
        }
        return out;
    }

    static Path alive(const Path& path)
    {
        const auto end = std::find(path.begin(), path.end(), nullptr);
        return Path(path.begin(), end);
    }

    static void assign(Path& to, const Path& from, std::size_t count)
    {
        to.assign(from.begin(), from.begin() + static_cast<std::ptrdiff_t>(std::min(count, from.size())));
        for (Element* e : to)
        {
            e->routed_ = true;
        }
    }

    // Deliver `e` up `path` (this dispatch's) from its end until one returns true. The index that
    // handled it, or -1.
    static std::ptrdiff_t deliver(Path& path, const PointerEvent& e, bool wheel)
    {
        const std::vector<Point> at = locals(path, e.position);
        for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(at.size()) - 1; i >= 0; --i)
        {
            Element* const el = path[static_cast<std::size_t>(i)];
            if (el == nullptr)
            {
                continue; // destroyed by a handler further down
            }
            PointerEvent local = e;
            local.position     = at[static_cast<std::size_t>(i)];
            if (wheel ? el->raise_wheel(local) : el->pointer_override(local))
            {
                if (!wheel)
                {
                    handled_above(path, at, e, i);
                }
                return i;
            }
        }
        return -1;
    }

    // What the element at `handled` took, shown to each element above it on `path`.
    static void handled_above(Path& path, const std::vector<Point>& at, const PointerEvent& e, std::ptrdiff_t handled)
    {
        for (std::ptrdiff_t i = handled - 1; i >= 0; --i)
        {
            if (Element* const el = path[static_cast<std::size_t>(i)])
            {
                PointerEvent local = e;
                local.position     = at[static_cast<std::size_t>(i)];
                el->handled_pointer_override(local);
            }
        }
    }

    // A Leave for each of `elements` (a path, or part of one, root first), innermost first.
    static void leave(Path elements, Point p)
    {
        PointerDispatch nested; // so ~Element() nulls them here too
        nested.path = std::move(elements);
        stack().push_back(&nested);
        for (std::size_t i = nested.path.size(); i-- > 0;)
        {
            if (Element* const el = nested.path[i])
            {
                PointerEvent local{};
                local.action   = PointerAction::Leave;
                local.position = el->to_local(p);
                (void)el->pointer_override(local);
            }
        }
        stack().pop_back();
    }

    static bool contains(const Path& path, const Element* e)
    {
        return std::find(path.begin(), path.end(), e) != path.end();
    }
};

void forget_routed(const Element* e)
{
    PointerDispatch::forget(e);
}

namespace
{
struct OutsidePressWatch
{
    const Element*   owner;
    Callback<void()> on_press;
};
std::vector<OutsidePressWatch>& outside_press_watches()
{
    thread_local std::vector<OutsidePressWatch> watches;
    return watches;
}
} // namespace

void watch_outside_presses(const Element& owner, Callback<void()> on_press)
{
    unwatch_outside_presses(owner);
    outside_press_watches().push_back({&owner, std::move(on_press)});
}

void unwatch_outside_presses(const Element& owner)
{
    std::erase_if(outside_press_watches(), [&owner](const OutsidePressWatch& w) { return w.owner == &owner; });
}

// A press landed on `hit` (a path, root first): every watcher it is not on is told.
void notify_outside_press(const std::vector<Element*>& hit)
{
    std::vector<const Element*> owners;
    for (const OutsidePressWatch& w : outside_press_watches())
    {
        if (std::find(hit.begin(), hit.end(), w.owner) == hit.end())
        {
            owners.push_back(w.owner);
        }
    }
    for (const Element* owner : owners) // a callback may watch or unwatch
    {
        auto& watches = outside_press_watches();
        auto  it      = std::find_if(watches.begin(),
                                     watches.end(),
                                     [owner](const OutsidePressWatch& w) { return w.owner == owner; });
        if (it != watches.end() && it->on_press)
        {
            it->on_press();
        }
    }
}

PointerRoute::PointerRoute()
{
    PointerDispatch::routes().push_back(this);
}

PointerRoute::~PointerRoute()
{
    std::erase(PointerDispatch::routes(), this);
}

} // namespace detail

Point Element::to_local(Point p) const
{
    if (!motion_)
    {
        return p;
    }
    const Matrix m = visual_transform();
    if (m.is_identity())
    {
        return p;
    }
    if (const std::optional<Matrix> inverse = m.inverted())
    {
        return inverse->apply(p);
    }
    return Point{-1.0e9f, -1.0e9f}; // scaled to nothing: nothing of it is anywhere to be hit
}

void Element::raise_pointer_over(bool over)
{
    if (!motion_)
    {
        return;
    }
    ElementMotion& m = *motion_;
    if (m.pointer_over == over)
    {
        return;
    }
    m.pointer_over = over;
    if (Callback<void()>& handler = over ? m.pointer_entered : m.pointer_exited)
    {
        handler();
    }
}

void Element::hit_test_children(Point /*p*/, std::vector<Element*>& out) const
{
    for_each_child([&out](Element& child) { out.push_back(&child); });
    std::reverse(out.begin(), out.end());
}

bool Element::wheel_override(const PointerEvent& e)
{
    bool leaf = true;
    for_each_child([&leaf](Element&) { leaf = false; });
    if (leaf)
    {
        (void)pointer_override(e);
    }
    return false;
}

bool Element::handle_pointer(const PointerEvent& e)
{
    using detail::PointerDispatch;
    using Path = PointerDispatch::Path;
    if (PointerDispatch::reaches(this))
    {
        return false; // this dispatch delivers to it already
    }
    // A handler may let go of the element it runs on (a row whose click reflows its list): what is
    // retired stays alive until the event has been delivered.
    const detail::RetireScope retired;
    if (!extras().pointer_route)
    {
        extras_->pointer_route.reset(new detail::PointerRoute());
    }
    detail::PointerRoute& route = *extras_->pointer_route;

    PointerDispatch d;
    d.route    = &route;
    d.root     = this;
    d.position = e.position;
    PointerDispatch::stack().push_back(&d);
    struct Pop
    {
        ~Pop()
        {
            PointerDispatch::stack().pop_back();
        }
    } pop;
    if (e.action != PointerAction::Leave)
    {
        (void)PointerDispatch::hit_test(*this, e.position, true, d.hit);
    }
    d.hovered  = PointerDispatch::alive(route.hover);
    d.captured = PointerDispatch::alive(route.captured);
    if (e.action == PointerAction::Down && e.button == route.capture_button)
    {
        d.captured.clear(); // a second press of the same button, its release missed: a new press
        route.captured.clear();
    }
    const bool holding = !d.captured.empty();
    const auto in      = [](const Path& path, const Element* el)
    { return el != nullptr && PointerDispatch::contains(path, el); };

    // What the pointer moved off is exited now, and gets a Leave unless it holds the capture.
    Path left;
    for (Element* el : d.hovered)
    {
        if (el != nullptr && !in(d.hit, el))
        {
            el->raise_pointer_over(false);
            if (e.action != PointerAction::Leave && !in(d.captured, el))
            {
                left.push_back(el);
            }
        }
    }
    if (!left.empty())
    {
        PointerDispatch::leave(std::move(left), e.position);
    }

    if (e.action == PointerAction::Down && PointerDispatch::stack().size() == 1)
    {
        detail::notify_outside_press(d.hit);
    }

    bool handled = false;
    if (e.action == PointerAction::Leave)
    {
        // Everything hovered or holding the capture gets it; nothing handles it for the others.
        Path all = d.hovered;
        for (Element* el : d.captured)
        {
            if (el != nullptr && !in(all, el))
            {
                all.push_back(el);
            }
        }
        route.captured.clear();
        route.capture_button = PointerButton::None;
        PointerDispatch::leave(std::move(all), e.position);
    }
    else
    {
        d.path  = holding ? d.captured : d.hit;
        handled = PointerDispatch::deliver(d.path, e, false) >= 0;
        if (e.action == PointerAction::Down && !holding && !d.path.empty())
        {
            // The press captures for the element it landed on and its parents, so the one that
            // handled it, and whatever below it was pressed, get the moves and the release. A
            // handler may have called capture_pointer() itself.
            if (route.captured.empty())
            {
                const Path pressed = PointerDispatch::alive(d.path);
                PointerDispatch::assign(route.captured, pressed, pressed.size());
            }
            route.capture_button = e.button;
        }
        else if (e.action == PointerAction::Up && holding && e.button == route.capture_button)
        {
            // The release: what held the capture and is no longer under the pointer leaves.
            route.captured.clear();
            route.capture_button = PointerButton::None;
            Path released;
            for (Element* el : d.captured)
            {
                if (el != nullptr && !in(d.hit, el))
                {
                    released.push_back(el);
                }
            }
            if (!released.empty())
            {
                PointerDispatch::leave(std::move(released), e.position);
            }
        }
    }

    // What the pointer moved onto.
    const Path now = PointerDispatch::alive(d.hit);
    PointerDispatch::assign(route.hover, now, now.size());
    for (Element* el : now)
    {
        el->raise_pointer_over(true);
    }
    return handled;
}

bool Element::handle_wheel(const PointerEvent& e)
{
    using detail::PointerDispatch;
    if (PointerDispatch::reaches(this))
    {
        return false;
    }
    const detail::RetireScope retired;
    PointerDispatch           dispatch;
    dispatch.root = this;
    PointerDispatch::stack().push_back(&dispatch);
    struct Pop
    {
        ~Pop()
        {
            PointerDispatch::stack().pop_back();
        }
    } pop;
    (void)PointerDispatch::hit_test(*this, e.position, true, dispatch.path);
    return PointerDispatch::deliver(dispatch.path, e, true) >= 0;
}

void Element::capture_pointer()
{
    using detail::PointerDispatch;
    for (auto it = PointerDispatch::stack().rbegin(); it != PointerDispatch::stack().rend(); ++it)
    {
        PointerDispatch& d = **it;
        if (d.route == nullptr)
        {
            continue;
        }
        const auto at = std::find(d.path.begin(), d.path.end(), this);
        if (at != d.path.end())
        {
            const std::size_t count = static_cast<std::size_t>(at - d.path.begin()) + 1;
            // What it is taken from gets a Leave, which cancels a press in progress there.
            PointerDispatch::Path lost;
            for (Element* el : PointerDispatch::alive(d.route->captured.empty() ? d.path : d.route->captured))
            {
                if (!PointerDispatch::contains(PointerDispatch::Path(d.path.begin(),
                                                                     d.path.begin() + static_cast<std::ptrdiff_t>(count)),
                                               el))
                {
                    lost.push_back(el);
                }
            }
            PointerDispatch::assign(d.route->captured, d.path, count);
            if (d.route->capture_button == PointerButton::None)
            {
                d.route->capture_button = PointerButton::Left;
            }
            if (!lost.empty())
            {
                PointerDispatch::leave(std::move(lost), d.position);
            }
            return;
        }
    }
}

void Element::release_pointer()
{
    using detail::PointerDispatch;
    for (PointerDispatch* d : PointerDispatch::stack())
    {
        if (d->route != nullptr && std::find(d->path.begin(), d->path.end(), this) != d->path.end())
        {
            d->route->captured.clear();
            d->route->capture_button = PointerButton::None;
        }
    }
}

const Accessibility& Element::no_accessibility()
{
    static const Accessibility none{};
    return none;
}

void Element::render_tracked(detail::PaintTracker& tracker, const RenderContext& handed) const
{
    // A clipped frame skips what the measuring pass just found to draw nowhere near the clip.
    if (tracker.culling() && painted_frame_ == tracker.frame() && !painted_area_.intersects(tracker.clip()))
    {
        return;
    }
    const RenderContext context = requested_theme_ == ElementTheme::Default
                                      ? handed
                                      : handed.with_theme(scoped_theme(handed.theme));
#ifndef NDEBUG
    const detail::RenderGuard guard(this);
#endif
    if (tracker.culling())
    {
        detail::count(detail::PerfCounter::Renders);
    }
    tracker.begin_element(*this);
    if (!tracker.culling())
    {
        const std::size_t layers = layer_count();
        const float opacity      = motion_ ? std::clamp(current_opacity() * motion_->target.opacity, 0.0f, 1.0f) : 1.0f;
        if (layers > 0 || layers_composed_)
        {
            tracker.probe_layers(*this, context.theme, visual_transform(), opacity, layers);
        }
        if (composes_visuals_)
        {
            tracker.probe_visuals(*this, visual_transform(), opacity);
        }
    }
    if (!motion_)
    {
        render_own(tracker, context);
    }
    else
    {
        render_with_visual_layer(tracker, context);
    }
    tracker.end_element(*this);
}

void Element::render_with_visual_layer(Renderer& renderer, const RenderContext& context) const
{
    const float opacity = std::clamp(current_opacity() * motion_->target.opacity, 0.0f, 1.0f);
    if (opacity <= 0.0f)
    {
        return;
    }
    const Matrix transform   = visual_transform();
    const bool   transformed = !transform.is_identity();
    const bool   layered     = opacity < 1.0f;
    if (transformed)
    {
        renderer.push_transform(transform);
    }
    if (layered)
    {
        renderer.push_opacity_layer(opacity);
    }
    render_own(renderer, context);
    if (layered)
    {
        renderer.pop_opacity_layer();
    }
    if (transformed)
    {
        renderer.pop_transform();
    }
}

void Element::advance_motion(Duration delta)
{
    ElementMotion&                m       = *motion_;
    bool                          changed = false;
    std::vector<Callback<void()>> completed;

    if (!m.running.empty())
    {
        const bool animate = animations_enabled();
        for (ElementMotion::Running& r : m.running)
        {
            r.elapsed += delta.count();
            if (!animate)
            {
                m.finish_now(r);
            }
            r.finished = m.done(r);
            if (r.kind != ElementMotion::Running::Kind::Expression)
            {
                changed = true;
                continue;
            }
            // An expression only repaints when what it reads has moved it (`to` keeps its last
            // value), so a Parallax over a list at rest leaves the window idle.
            for (int c = 0; c < ChannelCount; ++c)
            {
                if ((r.mask & bit(static_cast<Channel>(c))) == 0)
                {
                    continue;
                }
                const float v = m.sample(r, c);
                if (!r.sampled || v != r.to[c])
                {
                    changed = true;
                }
                r.to[c] = v;
            }
            r.sampled = true;
        }
        // Finished animations hand their last value to the property (or not, FillBehavior Stop).
        for (ElementMotion::Running& r : m.running)
        {
            if (!r.finished)
            {
                continue;
            }
            if (ElementMotion::holds_end(r))
            {
                for (int c = 0; c < ChannelCount; ++c)
                {
                    if ((r.mask & bit(static_cast<Channel>(c))) != 0)
                    {
                        m.base[c] = m.sample(r, c);
                    }
                }
            }
            Callback<void()>* done = nullptr;
            switch (r.kind)
            {
                case ElementMotion::Running::Kind::Timeline:
                    done = &r.timeline.on_completed;
                    break;
                case ElementMotion::Running::Kind::KeyFrames:
                    done = &r.key_frames.on_completed;
                    break;
                case ElementMotion::Running::Kind::Spring:
                    done = &r.spring.on_completed;
                    break;
                default:
                    break;
            }
            if (done != nullptr && *done)
            {
                completed.push_back(std::move(*done));
            }
        }
        std::erase_if(m.running, [](const ElementMotion::Running& r) { return r.finished; });
    }

    if (m.storyboard)
    {
        changed = true;
        m.storyboard_elapsed += animation_delta(delta).count();
        if (m.storyboard_elapsed >= m.storyboard->duration().count())
        {
            m.storyboard.reset();
            m.target = TransitionTargetState{};
            if (m.storyboard_done)
            {
                completed.push_back(std::move(m.storyboard_done));
                m.storyboard_done = nullptr;
            }
        }
        else
        {
            m.target = m.sample_target();
        }
    }

    if (changed)
    {
        mark_dirty(DirtyFlags::Visual);
    }
    // Last, so a handler that starts another animation (or destroys something) finds this element's
    // state settled.
    for (Callback<void()>& done : completed)
    {
        done();
    }
}

void Element::note_child_deleted()
{
    motion().child_deleted = true;
}

void Element::note_arranged()
{
    ElementMotion& m = motion();
    // As a parent: the children arranged after this start a fresh stagger sequence.
    m.entrance_stagger   = 0;
    m.reposition_stagger = 0;
    m.deleting_pass      = std::exchange(m.child_deleted, false);

    const Point offset = actual_offset();
    if (m.loaded && offset == m.last_offset)
    {
        return;
    }

    std::vector<const Transition*> all;
    for (const Transition& t : m.transitions)
    {
        all.push_back(&t);
    }
    ElementMotion* parent_motion = parent_ != nullptr ? parent_->motion_.get() : nullptr;
    if (parent_motion != nullptr)
    {
        for (const Transition& t : parent_motion->children_transitions)
        {
            all.push_back(&t);
        }
    }

    if (!m.loaded)
    {
        m.loaded      = true;
        m.last_offset = offset;
        if (laid_out_ || !animations_enabled())
        {
            return; // laid out before anything here was watching: no Load to see
        }
        // The Load trigger.
        for (const Transition* t : all)
        {
            if (const auto* entrance = std::get_if<EntranceTransition>(t))
            {
                TransitionStoryboard sb = transition_storyboards::entrance(Point{entrance->offset_x, entrance->offset_y});
                if (entrance->stagger)
                {
                    // ENTERPAGE's stagger delay.
                    const std::size_t index = parent_motion != nullptr ? parent_motion->entrance_stagger++ : 0;
                    sb.delay                = stagger_delay(index, Duration{35.0});
                }
                run_transition(std::move(sb));
                return;
            }
            if (const auto* content = std::get_if<ContentTransition>(t))
            {
                run_transition(transition_storyboards::content_in(Point{content->offset_x, content->offset_y}));
                return;
            }
            if (std::holds_alternative<AddRemoveTransition>(*t))
            {
                // The added element waits for the ones it displaces (scheduleAffectedFirst,
                // affectedDuration 300), and, added in the pass that closes a deleted sibling's
                // gap, for that exit and move as well.
                const bool mixed = parent_motion != nullptr && parent_motion->deleting_pass;
                run_transition(transition_storyboards::add(Duration{mixed ? 600.0 : 300.0}));
                return;
            }
        }
        return;
    }

    // The Layout trigger: the element moved relative to its parent. It starts from where it is
    // drawn now, which may be partway through an earlier move.
    const Point from{m.last_offset.x + m.target.translate.x - offset.x, m.last_offset.y + m.target.translate.y - offset.y};
    m.last_offset = offset;
    if (!animations_enabled())
    {
        return; // it is simply where layout put it
    }
    // A sibling's delete exit is playing: the AddRemoveTransition owns this move and closes
    // the gap after it, whatever the order of the list. A RepositionTransition starting now
    // would slide this element over the one still leaving.
    if (parent_motion != nullptr && parent_motion->deleting_pass &&
        std::ranges::any_of(all, [](const Transition* t) { return std::holds_alternative<AddRemoveTransition>(*t); }))
    {
        run_transition(transition_storyboards::make_room(from, Duration{333.0}, Duration{220.0}));
        return;
    }
    for (const Transition* t : all)
    {
        if (const auto* reposition = std::get_if<RepositionTransition>(t))
        {
            TransitionStoryboard sb = transition_storyboards::reposition(from);
            if (reposition->stagger)
            {
                // REPOSITION's stagger delay.
                const std::size_t index = parent_motion != nullptr ? parent_motion->reposition_stagger++ : 0;
                sb.delay                = stagger_delay(index, Duration{33.0});
            }
            run_transition(std::move(sb));
            return;
        }
        if (std::holds_alternative<AddRemoveTransition>(*t))
        {
            run_transition(transition_storyboards::make_room(from, Duration{400.0}, Duration{}));
            return;
        }
    }
}

namespace detail
{
namespace
{
thread_local std::uint32_t current_layout_pass = 0;
std::atomic<std::uint32_t> next_layout_pass{1};
} // namespace

namespace
{
thread_local bool tick_requested = false;
} // namespace

void note_tick_request() noexcept
{
    tick_requested = true;
}

bool take_tick_request() noexcept
{
    return std::exchange(tick_requested, false);
}

namespace
{
thread_local std::optional<double> wake_requested;

// What Element::tick() needs to tell a timed step from other changes: `own` counts the marks made
// by the element ticking now (a child's tick saves and restores it), `woke` whether it called
// request_tick_in(), and `busy` whether any element dirtied something outside a timed step.
struct ElementTickMarks
{
    std::uint64_t total = 0;
    std::uint64_t own   = 0;
    bool          woke  = false;
    bool          busy  = false;
};
thread_local ElementTickMarks element_tick_marks;
} // namespace

void note_wake_request(double ms) noexcept
{
    const double wait       = (std::max)(0.0, ms);
    wake_requested          = wake_requested ? (std::min)(*wake_requested, wait) : wait;
    element_tick_marks.woke = true;
}

void note_dirty_mark() noexcept
{
    ++element_tick_marks.total;
    ++element_tick_marks.own;
}

std::uint64_t dirty_marks() noexcept
{
    return element_tick_marks.total;
}

bool take_busy_tick() noexcept
{
    return std::exchange(element_tick_marks.busy, false);
}

namespace
{
// One Element::tick(): the marks and the wake request of the element itself, not of the children
// its tick_override() ticks in turn.
class ElementTickScope
{
public:
    ElementTickScope() noexcept :
        own_(std::exchange(element_tick_marks.own, 0)),
        woke_(std::exchange(element_tick_marks.woke, false))
    {
    }
    ElementTickScope(const ElementTickScope&)            = delete;
    ElementTickScope& operator=(const ElementTickScope&) = delete;
    ~ElementTickScope()
    {
        element_tick_marks.own  = own_;
        element_tick_marks.woke = woke_;
    }

    // An animation of the element's visual layer that moved: the window stays busy.
    static void end_motion() noexcept
    {
        if (std::exchange(element_tick_marks.own, 0) > 0)
        {
            element_tick_marks.busy = true;
        }
    }
    // A change tick_override() made without asking for a wake: busy too. One with a wake is a timed step.
    static void end_on_tick() noexcept
    {
        if (element_tick_marks.own > 0 && !element_tick_marks.woke)
        {
            element_tick_marks.busy = true;
        }
    }

private:
    std::uint64_t own_;
    bool          woke_;
};
} // namespace

std::optional<double> take_wake_request() noexcept
{
    return std::exchange(wake_requested, std::nullopt);
}

std::uint32_t layout_pass() noexcept
{
    return current_layout_pass;
}

LayoutPassScope::LayoutPassScope() noexcept : outer_(current_layout_pass)
{
    std::uint32_t pass = next_layout_pass.fetch_add(1, std::memory_order_relaxed);
    if (pass == 0)
    {
        pass = next_layout_pass.fetch_add(1, std::memory_order_relaxed); // 0 means "no pass"
    }
    current_layout_pass = pass;
}

LayoutPassScope::~LayoutPassScope()
{
    current_layout_pass = outer_;
}

std::uint64_t removed_element_count() noexcept
{
    return removed_elements_on_thread;
}

namespace
{
struct Retired
{
    int                                   depth = 0;
    std::vector<std::unique_ptr<Element>> parked;
};

Retired& retired()
{
    thread_local Retired state;
    return state;
}
} // namespace

void retire(std::unique_ptr<Element> element)
{
    if (!element)
    {
        return;
    }
    TreeAccess::set_parent(*element, nullptr); // its parent may be gone before it is
    Retired& state = retired();
    if (state.depth > 0)
    {
        // The model the subtree was built for may already be gone: a table's new rows reaching
        // its old columns. Its handlers stay alive, as one of them may be what retired it.
        TreeAccess::unlink_subscriptions(*element);
        state.parked.push_back(std::move(element));
    }
}

namespace
{
void forget_routes_below(Element& element)
{
    forget_routed(&element);
    element.for_each_child([](Element& child) { forget_routes_below(child); });
}
} // namespace

std::unique_ptr<Element> orphan(std::unique_ptr<Element> element)
{
    if (!element)
    {
        return element;
    }
    if (Window* const window = element->window(); window != nullptr && element->has_focus_within())
    {
        window->blur();
    }
    TreeAccess::set_parent(*element, nullptr);
    forget_routes_below(*element);
    ++removed_elements_on_thread; // the window looks at its tab ring again
    return element;
}

RetireScope::RetireScope() noexcept
{
    ++retired().depth;
}

RetireScope::~RetireScope()
{
    Retired& state = retired();
    if (--state.depth > 0)
    {
        return;
    }
    // A destructor may retire more; with no scope open that is destroyed on the spot.
    while (!state.parked.empty())
    {
        std::vector<std::unique_ptr<Element>> batch = std::move(state.parked);
        state.parked.clear();
        batch.clear();
    }
}
} // namespace detail

void Element::tick(Duration delta)
{
    detail::count(detail::PerfCounter::ElementTicks);
    const detail::ElementTickScope scope;
    // A handler run by the tick may retire this element or a child; they are parked until the
    // outermost tick() is done.
    const detail::RetireScope keep_alive;
    if (motion_)
    {
        advance_motion(delta);
        detail::ElementTickScope::end_motion();
    }
    tick_override(delta);
    detail::ElementTickScope::end_on_tick();

    // The children are collected up front, since a handler the tick runs may add or remove some.
    // One stack per thread: a nested tick() appends past its caller's entries and trims back to
    // them, so the walk allocates nothing once the stack has grown.
    // A child whose parent changed meanwhile was removed (retire() clears it) and is skipped.
    thread_local std::vector<std::pair<Element*, Element*>> pending;
    const std::size_t                                       first = pending.size();
    for_each_child([](Element& child) { pending.emplace_back(&child, child.parent_); });
    const std::size_t last = pending.size();
    for (std::size_t i = first; i < last; ++i)
    {
        const auto [child, parent] = pending[i];
        if (child->parent_ == parent)
        {
            child->tick(delta);
        }
    }
    pending.resize(first);
}

} // namespace ui
