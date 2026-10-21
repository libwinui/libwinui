// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Duration.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/render/CubicBezier.hpp"
#include "winui/render/PathGeometry.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ui
{

// ---- the animations switch --------------------------------------------------------------------

/// Whether animations play right now. False while a Window whose theme has
/// Theme::animations_enabled off is ticking, laying out, rendering or dispatching input to its
/// tree. True outside any window. A control that runs its own tween checks it, or advances its
/// clock by animation_delta(), so the tween jumps to its end state instead of playing.
///
/// Example:
/// @code
/// class Drawer : public ui::Element
/// {
///     static constexpr ui::Duration SlideTime = 250ms;
///     ui::Duration                  slide_{};
///
///     void tick_override(ui::Duration delta) override
///     {
///         slide_ = std::min(slide_ + ui::animation_delta(delta), SlideTime);
///         mark_dirty(ui::DirtyFlags::Visual);
///     }
/// };
/// @endcode
[[nodiscard]] bool animations_enabled();

/// How far an animation's clock moves in a tick of `delta`: `delta` itself, or, while
/// animations_enabled() is false, far enough to finish any finite animation in that one tick.
/// Use it only for animations. A delay, a timeout, a blink or a looping indicator keeps counting
/// `delta`.
[[nodiscard]] Duration animation_delta(Duration delta);

// ---- easing ----------------------------------------------------------------------------------

/// Which end of the curve the easing is applied to. EaseOut is the default.
enum class EasingMode
{
    EaseOut,   ///< The curve is mirrored so it decelerates into the end; the default.
    EaseIn,    ///< The curve as defined: accelerates out of the start.
    EaseInOut, ///< EaseIn for the first half, EaseOut for the second.
};

/// Pulls back by `amplitude` before moving (t³ − t·a·sin(πt)).
///
/// Example:
/// @code
/// auto element = std::make_unique<ui::RectangleControl>();
/// element->start_animation(ui::AnimatedProperty::TranslationY,
///                          ui::DoubleAnimation{.to       = 0.0f,
///                                              .duration = 400ms,
///                                              .easing   = ui::BackEase{.amplitude = 0.5f}});
/// @endcode
struct BackEase
{
    float      amplitude = 1.0f;                ///< How far it pulls back; 0 is plain t cubed.
    EasingMode mode      = EasingMode::EaseOut; ///< Which end the curve applies to.
};
/// `bounces` bounces, each `bounciness` times smaller than the last (clamped above 1).
struct BounceEase
{
    int        bounces    = 3;                   ///< Number of bounces; negative is taken as 0.
    float      bounciness = 2.0f;                ///< Ratio between successive bounce heights.
    EasingMode mode       = EasingMode::EaseOut; ///< Which end the curve applies to.
};
/// A quarter circle.
struct CircleEase
{
    EasingMode mode = EasingMode::EaseOut; ///< Which end the curve applies to.
};
/// t³.
struct CubicEase
{
    EasingMode mode = EasingMode::EaseOut; ///< Which end the curve applies to.
};
/// `oscillations` swings, damped exponentially by `springiness`.
struct ElasticEase
{
    int        oscillations = 3;                   ///< Number of swings; negative is taken as 0.
    float      springiness  = 3.0f;                ///< Exponential damping of the swings.
    EasingMode mode         = EasingMode::EaseOut; ///< Which end the curve applies to.
};
/// (e^(k·t) − 1) / (e^k − 1).
struct ExponentialEase
{
    float      exponent = 2.0f;                ///< k in the formula; 0 is linear.
    EasingMode mode     = EasingMode::EaseOut; ///< Which end the curve applies to.
};
/// t^power (a negative power is taken as 0).
struct PowerEase
{
    float      power = 2.0f;                ///< The exponent.
    EasingMode mode  = EasingMode::EaseOut; ///< Which end the curve applies to.
};
/// t².
struct QuadraticEase
{
    EasingMode mode = EasingMode::EaseOut; ///< Which end the curve applies to.
};
/// t⁴.
struct QuarticEase
{
    EasingMode mode = EasingMode::EaseOut; ///< Which end the curve applies to.
};
/// t⁵.
struct QuinticEase
{
    EasingMode mode = EasingMode::EaseOut; ///< Which end the curve applies to.
};
/// A quarter sine.
struct SineEase
{
    EasingMode mode = EasingMode::EaseOut; ///< Which end the curve applies to.
};

/// One easing curve: a CubicBezier (the default, which is linear) or one of the eleven named
/// eases above. Assign any of them directly: `.easing = ui::SineEase{ui::EasingMode::EaseInOut}`.
using EasingFunction = std::
    variant<CubicBezier, BackEase, BounceEase, CircleEase, CubicEase, ElasticEase, ExponentialEase, PowerEase, QuadraticEase, QuarticEase, QuinticEase, SineEase>;

/// The eased progress for linear progress `t` (0..1), with the curve applied at the end its
/// EasingMode names. The curves may overshoot 0..1 (Back, Elastic).
[[nodiscard]] float ease(const EasingFunction& function, float t);

// ---- key frames --------------------------------------------------------------------------------

/// How a key frame's segment gets from the previous value to its own.
enum class KeyFrameKind
{
    Linear,   ///< Interpolates linearly.
    Discrete, ///< Holds the previous value, then jumps at the key time.
    Spline,   ///< Follows a cubic Bézier; the `easing` must be a CubicBezier.
    Easing,   ///< Follows the `easing` curve.
};

/// One key frame: a value at a time (milliseconds from the animation's start). Build one with the
/// named factories, so `kind` and `easing` agree.
///
/// Example:
/// @code
/// const std::vector<ui::KeyFrame> frames{
///     ui::KeyFrame::linear(0ms, 0.0f),
///     ui::KeyFrame::eased(300ms, 1.0f, ui::CubicEase{}),
///     ui::KeyFrame::discrete(600ms, 0.5f),
///     ui::KeyFrame::spline(900ms, 1.0f, ui::CubicBezier{0.8f, 0.0f, 0.2f, 1.0f}),
/// };
/// auto element = std::make_unique<ui::RectangleControl>();
/// element->start_animation(ui::AnimatedProperty::Opacity, ui::KeyFrameAnimation{.key_frames = frames});
/// @endcode
struct KeyFrame
{
    /// Key time from the animation's start.
    Duration time{};
    /// The value reached at `time`.
    float value = 0.0f;
    /// How the segment ending at this frame interpolates.
    KeyFrameKind kind = KeyFrameKind::Linear;
    /// The segment's curve for Spline (a CubicBezier) and Easing; ignored otherwise.
    EasingFunction easing{};

    /// Interpolates linearly from the previous value.
    [[nodiscard]] static KeyFrame linear(Duration time, float value)
    {
        return KeyFrame{time, value, KeyFrameKind::Linear, {}};
    }
    /// Holds the previous value, then jumps to `value` at `time`.
    [[nodiscard]] static KeyFrame discrete(Duration time, float value)
    {
        return KeyFrame{time, value, KeyFrameKind::Discrete, {}};
    }
    /// Interpolates along the cubic Bézier `spline`.
    [[nodiscard]] static KeyFrame spline(Duration time, float value, CubicBezier spline)
    {
        return KeyFrame{time, value, KeyFrameKind::Spline, spline};
    }
    /// Interpolates along `easing`.
    [[nodiscard]] static KeyFrame eased(Duration time, float value, EasingFunction easing)
    {
        return KeyFrame{time, value, KeyFrameKind::Easing, easing};
    }
};

/// The value of `frames` (sorted by time) at `time`. Each frame's segment starts at the previous
/// frame's value, the first one's at `base` (the value the property had when the animation
/// started). After the last frame its value holds.
[[nodiscard]] float sample_key_frames(std::span<const KeyFrame> frames, float base, Duration time);

/// The time of the last key frame: a key frame animation's natural duration.
[[nodiscard]] Duration key_frames_duration(std::span<const KeyFrame> frames);

// ---- the animations an element runs on its own visual properties ----------------------------

/// What an animation started on an element drives (Element::start_animation()): the element's
/// opacity, rotation, translation, scale or center point, whole or one component.
enum class AnimatedProperty : std::uint8_t
{
    Opacity,      ///< Element::opacity(), 0..1.
    Rotation,     ///< Element::rotation(), in degrees about the center point.
    Translation,  ///< Element::translation(), all three components (DIPs).
    TranslationX, ///< Translation's x only.
    TranslationY, ///< Translation's y only.
    TranslationZ, ///< Translation's z only.
    Scale,        ///< Element::scale(), all three components.
    ScaleX,       ///< Scale's x only.
    ScaleY,       ///< Scale's y only.
    ScaleZ,       ///< Scale's z only.
    CenterPoint,  ///< Element::center_point(), all three components (DIPs from the top-left).
    CenterPointX, ///< CenterPoint's x only.
    CenterPointY, ///< CenterPoint's y only.
    CenterPointZ, ///< CenterPoint's z only.
};

/// Whether a finished animation keeps its last value (HoldEnd, the default) or hands the
/// property back to the value it had before it started (Stop).
enum class FillBehavior
{
    HoldEnd, ///< Keep the final value (written into the property); the default.
    Stop,    ///< Hand the property back to its pre-animation value.
};

/// A from/to/by animation of one property. A missing `from` starts from the property's current
/// animated value. `to` wins over `by`, and with neither the animation goes back to the
/// property's own value. `duration` is one iteration, a second by default. `repeat_count` is the
/// number of iterations, infinity repeats forever, and an auto-reversing iteration lasts two
/// durations. On a three-component property (Translation, Scale, CenterPoint), every component
/// runs the same values.
///
/// Example:
/// @code
/// auto                  stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// ui::RectangleControl* rect  = stack->add(std::make_unique<ui::RectangleControl>());
/// rect->start_animation(ui::AnimatedProperty::TranslationX,
///                       ui::DoubleAnimation{.to           = 200.0f,
///                                           .duration     = 500ms,
///                                           .easing       = ui::CircleEase{ui::EasingMode::EaseInOut},
///                                           .on_completed = [] { /* now at x + 200 */ }});
///
/// // A pulsing fade that gives the property back when done.
/// rect->start_animation(ui::AnimatedProperty::Opacity,
///                       ui::DoubleAnimation{.from         = 1.0f,
///                                           .to           = 0.2f,
///                                           .duration     = 300ms,
///                                           .auto_reverse = true,
///                                           .repeat_count = 3.0,
///                                           .fill         = ui::FillBehavior::Stop});
/// @endcode
struct DoubleAnimation
{
    /// Start value. Unset starts from the property's current animated value.
    OptionalFloat from{};
    /// End value; wins over `by`.
    OptionalFloat to{};
    /// End value relative to the start, used when `to` is unset.
    OptionalFloat by{};
    /// Length of one iteration.
    Duration duration{1000.0};
    /// Curve over each iteration. Unset is linear.
    std::optional<EasingFunction> easing{};
    /// Delay before the first iteration.
    Duration begin_time{};
    /// Plays each iteration forward then backward, doubling its length.
    bool auto_reverse = false;
    /// Iterations to run; `std::numeric_limits<double>::infinity()` repeats forever.
    double repeat_count = 1.0;
    /// What the property shows once the animation finishes.
    FillBehavior fill = FillBehavior::HoldEnd;
    /// Fires once when the animation finishes (not when it is replaced or stopped).
    Callback<void()> on_completed{};
};

/// A key frame animation of one property. The frames play from the property's current value.
/// `duration` 0 is the last key time.
///
/// Example:
/// @code
/// auto element = std::make_unique<ui::RectangleControl>();
/// element->start_animation(ui::AnimatedProperty::ScaleX,
///                          ui::KeyFrameAnimation{.key_frames   = {ui::KeyFrame::linear(150ms, 1.2f),
///                                                                 ui::KeyFrame::linear(300ms, 1.0f)},
///                                                .repeat_count = 2.0});
/// @endcode
struct KeyFrameAnimation
{
    /// The frames, sorted by time; the first segment starts at the property's current value.
    std::vector<KeyFrame> key_frames{};
    /// Iteration length; 0 uses the last key time.
    Duration duration{};
    /// Iterations to run; infinity repeats forever.
    double repeat_count = 1.0;
    /// What the property shows once the animation finishes.
    FillBehavior fill = FillBehavior::HoldEnd;
    /// Fires once when the animation finishes.
    Callback<void()> on_completed{};
};

/// A spring: a damped harmonic oscillator pulled to `final_value` (on a scalar property,
/// `final_value.x`). `period` is the undamped period, `damping_ratio` 0..1 from undamped to
/// critically damped (above 1 overdamped). It starts from the property's current value. When it
/// replaces a running spring it also takes that spring's velocity, so a hover spring hands off
/// smoothly.
///
/// Example:
/// @code
/// // Hover grow: spring to 1.1 on enter, back to 1 on exit.
/// auto               stack  = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// ui::ButtonControl* button = stack->add(std::make_unique<ui::ButtonControl>("Hover me"));
/// button->on_pointer_entered([button] {
///     button->start_animation(ui::AnimatedProperty::Scale,
///                             ui::SpringAnimation{.final_value = ui::Vector3{1.1f, 1.1f, 1.0f}});
/// });
/// button->on_pointer_exited([button] {
///     button->start_animation(ui::AnimatedProperty::Scale,
///                             ui::SpringAnimation{.final_value = ui::Vector3{1.0f, 1.0f, 1.0f}});
/// });
/// @endcode
struct SpringAnimation
{
    /// The rest value the spring pulls to; a scalar property uses `.x`.
    Vector3 final_value{1.0f, 1.0f, 1.0f};
    /// 0 never settles, 1 is critically damped, above 1 is overdamped.
    float damping_ratio = 0.6f;
    /// Undamped oscillation period; shorter is stiffer.
    Duration period{50.0};
    /// Overrides the start value; unset starts from the property's current value.
    std::optional<Vector3> initial_value{};
    /// Overrides the start velocity (units per second); unset inherits a replaced spring's.
    std::optional<Vector3> initial_velocity{};
    /// Fires once the spring has settled on `final_value`.
    Callback<void()> on_completed{};
};

/// An animation driven by a C++ callable. It is evaluated every frame and wherever the element's
/// transform is read, so it is never a frame behind the elements it reads. A `float()` callable
/// drives a scalar property, or every component of a Vector3 one. A `Vector3()` callable drives a
/// Vector3 property, or a scalar one with its `.x`. It runs until stopped.
///
/// Example:
/// @code
/// // Keep `badge` at the inverse of `card`'s animated scale, so it never looks stretched.
/// auto                  stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// ui::BorderControl*    card  = stack->add(std::make_unique<ui::BorderControl>());
/// ui::RectangleControl* badge = stack->add(std::make_unique<ui::RectangleControl>());
/// badge->start_animation(ui::AnimatedProperty::Scale,
///                        ui::ExpressionAnimation{.expression = ui::Callback<ui::Vector3()>{[card] {
///                            const ui::Vector3 s = card->current_scale();
///                            return ui::Vector3{1.0f / s.x, 1.0f / s.y, 1.0f};
///                        }}});
/// // ... later
/// badge->stop_animation(ui::AnimatedProperty::Scale);
/// @endcode
struct ExpressionAnimation
{
    /// The expression, evaluated again every frame. Captured elements must outlive the animation.
    std::variant<Callback<float()>, Callback<Vector3()>> expression;
};

/// Any of the four, for Element::start_animation().
using Animation = std::variant<DoubleAnimation, KeyFrameAnimation, SpringAnimation, ExpressionAnimation>;

// ---- implicit transitions ----------------------------------------------------------------------

/// Animates every change to an element's opacity or rotation (Element::set_opacity_transition(),
/// Element::set_rotation_transition()) over `duration` along the cubic Bézier (0.8,0)-(0.2,1),
/// from wherever the property is drawn at that moment.
///
/// Example:
/// @code
/// auto element = std::make_unique<ui::RectangleControl>();
/// element->set_opacity_transition(ui::ScalarTransition{.duration = 200ms});
/// element->set_opacity(0.0f); // fades out over 200 ms instead of vanishing
/// @endcode
struct ScalarTransition
{
    Duration duration{300.0}; ///< Length of each transition.
};

/// Which components of a Vector3 property a transition animates;
/// the others snap to their new value.
enum class Vector3Components : std::uint8_t
{
    None = 0, ///< Every component snaps.
    X    = 1, ///< Animate x.
    Y    = 2, ///< Animate y.
    Z    = 4, ///< Animate z.
    All  = 7, ///< Animate all three; the default.
};
/// Combines two component sets (`Vector3Components::X | Vector3Components::Y`).
[[nodiscard]] constexpr Vector3Components operator|(Vector3Components a, Vector3Components b)
{
    return static_cast<Vector3Components>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
/// Whether `set` includes component `c`.
[[nodiscard]] constexpr bool has_component(Vector3Components set, Vector3Components c)
{
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(c)) != 0;
}

/// Animates every change to an element's translation or scale
/// (Element::set_translation_transition(), Element::set_scale_transition()), with
/// ScalarTransition's curve.
///
/// Example:
/// @code
/// auto element = std::make_unique<ui::RectangleControl>();
/// element->set_translation_transition(ui::Vector3Transition{.components = ui::Vector3Components::Y});
/// element->set_translation(ui::Vector3{20.0f, 40.0f, 0.0f}); // y glides, x snaps
/// @endcode
struct Vector3Transition
{
    Duration          duration{300.0};                     ///< Length of each transition.
    Vector3Components components = Vector3Components::All; ///< Which components animate.
};

/// Animates a background color change (a spec's `background_transition`) linearly in RGB over
/// `duration`, from the color on screen at that moment.
///
/// Example:
/// @code
/// window.set_content(ui::VerticalStack{
///     .background            = ui::Color::from_argb(0xFF202020),
///     .background_transition = ui::BrushTransition{.duration = 300ms},
///     .content               = {ui::Text{.text = "Fades on color changes"}},
/// });
/// @endcode
struct BrushTransition
{
    Duration duration{150.0}; ///< Length of the blend (at least 1 ms).
};

/// Runs a BrushTransition for a control that owns a background. It remembers the color on screen
/// when the background changed and blends from it to the new one. It animates only between two
/// colors. A background appearing or disappearing changes at once.
///
/// Example:
/// @code
/// class Swatch : public ui::Element
/// {
///     // Changing color: remember what is on screen now, then blend to the new one.
///     void set_color(ui::Color c)
///     {
///         animator_.retarget(animator_.current(color_), c, ui::BrushTransition{});
///         color_ = c;
///         mark_dirty(ui::DirtyFlags::Visual);
///     }
///     void tick_override(ui::Duration delta) override
///     {
///         if (animator_.advance(delta))
///             mark_dirty(ui::DirtyFlags::Visual);
///     }
///     void render_override(ui::Renderer& r, const ui::RenderContext&) const override
///     {
///         if (const auto c = animator_.current(color_))
///             r.fill_rounded_rect(bounds(), 4.0f, *c);
///     }
///     // measure_override() etc. omitted
///
///     std::optional<ui::Color> color_;
///     ui::ColorAnimator        animator_;
/// };
/// @endcode
class ColorAnimator
{
public:
    /// The background is changing from `from` (what current() draws right now) to `to`. Starts a
    /// blend only when `transition` is set, both colors are set and differ, and animations are
    /// on; otherwise stops any running blend so the new color shows at once.
    void retarget(std::optional<Color> from, std::optional<Color> to, const std::optional<BrushTransition>& transition)
    {
        if (!transition || !from || !to || *from == *to || !animations_enabled())
        {
            active_ = false;
            return;
        }
        from_     = *from;
        duration_ = (std::max)(transition->duration.count(), 1.0);
        elapsed_  = 0.0;
        active_   = true;
    }
    /// Advances the blend by `delta`; returns true while blending (including the step that
    /// finishes it), so the owner knows to repaint.
    bool advance(Duration delta)
    {
        if (!active_)
        {
            return false;
        }
        elapsed_ += animation_delta(delta).count();
        if (elapsed_ >= duration_)
        {
            active_ = false;
        }
        return true;
    }
    /// The color to draw for a background whose value is `target`: the blend from the
    /// remembered color while active, `target` itself otherwise.
    [[nodiscard]] std::optional<Color> current(const std::optional<Color>& target) const
    {
        if (!active_ || !target)
        {
            return target;
        }
        return lerp(from_, *target, static_cast<float>((std::min)(1.0, elapsed_ / duration_)));
    }
    /// Whether a blend is in progress.
    [[nodiscard]] bool active() const
    {
        return active_;
    }

private:
    Color  from_{};
    double elapsed_  = 0.0;
    double duration_ = 0.0;
    bool   active_   = false;
};

namespace detail
{
// A rect a control draws eased toward where its state puts it, sampled on the CPU: the radio dot's
// size, the toggle knob's place in its track. sync_composition_override() retargets it from the
// state (it has the theme), tick_override() advances it, render_override() draws current(). With
// no compositor nothing retargets, so current() is the target itself. Rects are in whatever space
// the control picks, so one relative to a moving part follows that part.
class RectTween
{
public:
    // The rect is heading to `to`. A change of target starts a tween from what current() shows
    // now, over `duration` along `curve`, while animations are on; the first call only records it.
    void retarget(const Rect& to, Duration duration, const CubicBezier& curve);
    // Moves the tween on by `delta`; true while moving, the step that finishes it included.
    bool advance(Duration delta);
    // The rect to draw when the target is `to`: the eased rect while moving, else `to` itself.
    [[nodiscard]] Rect current(const Rect& to) const;
    // Forgets the target, so the next retarget() only records it (a part that appears again).
    void reset();

private:
    Rect        from_{};
    Rect        to_{};
    CubicBezier curve_{};
    double      elapsed_  = 0.0;
    double      duration_ = 0.0;
    bool        known_    = false;
    bool        active_   = false;
    bool        fresh_    = false; // no tick yet since the last retarget
};
} // namespace detail

// ---- page transitions --------------------------------------------------------------------------

/// The default navigation transition. The new page rises 140 DIPs as it fades in, and the old one
/// fades out over 150 ms.
///
/// Example:
/// @code
/// auto frame = std::make_unique<ui::NavigationFrameControl>();
/// frame->set_content_transitions({ui::NavigationTransition{}});
/// frame->navigate([] { return std::make_unique<ui::TextControl>("Home"); },
///                 ui::RisePageTransition{});
/// @endcode
struct RisePageTransition
{
};
/// The new page grows from 94 % as it fades in, and the old one grows to 104 % as it fades out.
/// Going back plays the reverse.
struct ZoomPageTransition
{
};
/// No animation at all. A page that plays a connected animation navigates with it.
struct NoPageTransition
{
};
/// The direction of a SlidePageTransition.
enum class PageSlideDirection
{
    FromBottom, ///< The new page slides up; the default.
    FromLeft,   ///< The new page slides in from the left.
    FromRight,  ///< The new page slides in from the right.
};
/// From the bottom, the new page slides up 200 DIPs. From the left or right, the old page slides
/// 150 DIPs away and the new one 200 DIPs in.
///
/// Example:
/// @code
/// auto frame = std::make_unique<ui::NavigationFrameControl>();
/// frame->navigate([] { return std::make_unique<ui::TextControl>("Next"); },
///                 ui::SlidePageTransition{.direction = ui::PageSlideDirection::FromRight});
/// @endcode
struct SlidePageTransition
{
    PageSlideDirection direction = PageSlideDirection::FromBottom; ///< Where the new page comes from.
};
/// The new page zooms in from 90 % after the old one has faded. Going back, the old page drops
/// 200 DIPs away.
struct FadeThroughPageTransition
{
};

/// One of the navigation transitions, passed to `NavigationFrameControl::navigate()` or used as a
/// NavigationTransition's default.
using PageTransition = std::variant<RisePageTransition, ZoomPageTransition, NoPageTransition, SlidePageTransition, FadeThroughPageTransition>;

// ---- element transitions -----------------------------------------------------------------

/// An element entering the tree slides in from the given offset while it fades in, over 667 ms
/// along (0.1,0.9)-(0.2,1). With staggering on, the elements that enter together start 35 ms
/// apart.
///
/// Example:
/// @code
/// window.set_content(ui::VerticalStack{
///     .spacing              = 8,
///     .children_transitions = {ui::EntranceTransition{.stagger = true}},
///     .content              = {ui::Text{.text = "First"}, ui::Text{.text = "Second"}},
/// });
/// @endcode
struct EntranceTransition
{
    float offset_x = 0.0f;  ///< Start x offset from the final position, DIPs.
    float offset_y = 28.0f; ///< Start y offset from the final position, DIPs.
    bool  stagger  = false; ///< Start elements entering together 35 ms apart.
};
/// An element that layout moves glides from where it was, over 367 ms. Staggering, on by default,
/// starts the elements moved together 33 ms apart.
///
/// Example:
/// @code
/// auto item = std::make_unique<ui::BorderControl>();
/// item->set_transitions({ui::RepositionTransition{.stagger = false}});
/// @endcode
struct RepositionTransition
{
    bool stagger = true; ///< Delay elements moved together 33 ms apart.
};
/// New content slides in from the offset over 500 ms and fades in over 170 ms after 50 ms.
/// Outgoing content fades out in 50 ms.
struct ContentTransition
{
    float offset_x = 0.0f;  ///< Start x offset of incoming content, DIPs.
    float offset_y = 28.0f; ///< Start y offset of incoming content, DIPs.
};
/// An added item grows from 90 % and fades in after the items it displaces have moved. A deleted
/// one shrinks to 90 % and fades out, and the rest close the gap.
struct AddRemoveTransition
{
};
/// An opening popup slides in from the offset and fades in. A closing one fades out in 83 ms.
///
/// Example:
/// @code
/// auto popup = std::make_unique<ui::PopupControl>();
/// popup->set_child_transitions({ui::PopupTransition{.offset_y = 20.0f}});
/// @endcode
struct PopupTransition
{
    float offset_x = 0.0f;  ///< Start x offset of the opening popup, DIPs.
    float offset_y = 40.0f; ///< Start y offset of the opening popup, DIPs.
};
/// An entry of a navigation frame's content transitions (`set_content_transitions()`). It animates
/// navigation with the PageTransition passed to `navigate()`, or with this default when
/// none was passed.
///
/// Example:
/// @code
/// auto frame = std::make_unique<ui::NavigationFrameControl>();
/// frame->set_content_transitions({ui::NavigationTransition{
///     .default_transition = ui::ZoomPageTransition{}}});
/// frame->navigate([] { return std::make_unique<ui::TextControl>("Details"); }); // zooms in
/// @endcode
struct NavigationTransition
{
    /// Used when navigate() is given no transition. Unset uses RisePageTransition.
    std::optional<PageTransition> default_transition{};
};

/// One transition, for Element::set_transitions(), Element::set_children_transitions() and
/// the `transitions`-style spec fields.
using Transition = std::variant<EntranceTransition, RepositionTransition, ContentTransition, AddRemoveTransition, PopupTransition, NavigationTransition>;

/// An element's transitions, in the order they were given.
using TransitionList = std::vector<Transition>;

/// The first transition of type `T` in `transitions`, or nullptr.
template <typename T>
[[nodiscard]] const T* find_transition(const TransitionList& transitions)
{
    for (const Transition& t : transitions)
    {
        if (const T* found = std::get_if<T>(&t))
        {
            return found;
        }
    }
    return nullptr;
}

// ---- the transition target ---------------------------------------------------------------------

/// The opacity, scale and offset a transition or a navigation transition animates. They
/// apply on top of the element's own opacity, scale, rotation and translation, and reset when the
/// transition ends. The scale is about `scale_origin`, relative to the element's size.
struct TransitionTargetState
{
    float opacity = 1.0f;           ///< Multiplied into the element's own opacity, 0..1.
    Point translate{};              ///< Offset in DIPs, applied after the scale.
    float scale_x = 1.0f;           ///< Horizontal scale factor.
    float scale_y = 1.0f;           ///< Vertical scale factor.
    Point scale_origin{0.5f, 0.5f}; ///< Scale origin as a fraction of the element's size.

    /// True at rest: opaque, unmoved and unscaled (scale_origin is irrelevant then).
    [[nodiscard]] bool is_identity() const
    {
        return opacity == 1.0f && translate == Point{} && scale_x == 1.0f && scale_y == 1.0f;
    }
    /// Memberwise equality.
    friend bool operator==(const TransitionTargetState&, const TransitionTargetState&) = default;
};

/// Which transition-target value a storyboard track drives.
enum class TransitionChannel : std::uint8_t
{
    Opacity,    ///< TransitionTargetState::opacity.
    TranslateX, ///< TransitionTargetState::translate.x.
    TranslateY, ///< TransitionTargetState::translate.y.
    ScaleX,     ///< TransitionTargetState::scale_x.
    ScaleY,     ///< TransitionTargetState::scale_y.
};

/// One key-framed track of a transition storyboard.
struct TransitionTrack
{
    TransitionChannel     channel = TransitionChannel::Opacity; ///< The value this track drives.
    std::vector<KeyFrame> key_frames{};                         ///< Frames sorted by time.
};

/// The storyboard a theme or navigation transition builds for one element: key-framed tracks over
/// its TransitionTargetState, started `delay` late (a stagger), scaling about `scale_origin`.
/// When it ends the transition target goes back to identity.
///
/// Example:
/// @code
/// // A custom "nudge": slide 12 DIPs right and back over 200 ms.
/// auto                     element = std::make_unique<ui::RectangleControl>();
/// ui::TransitionStoryboard nudge;
/// nudge.tracks.push_back({ui::TransitionChannel::TranslateX,
///                         {ui::KeyFrame::linear(0ms, 0.0f), ui::KeyFrame::linear(100ms, 12.0f),
///                          ui::KeyFrame::linear(200ms, 0.0f)}});
/// element->run_transition(std::move(nudge), [] { /* back at rest */ });
///
/// // Or one of the stock ones:
/// element->run_transition(ui::transition_storyboards::popup_in(ui::Point{0.0f, 40.0f}));
/// @endcode
struct TransitionStoryboard
{
    std::vector<TransitionTrack> tracks{};                 ///< The key-framed tracks.
    Duration                     delay{};                  ///< Start delay (a stagger).
    Point                        scale_origin{0.5f, 0.5f}; ///< Scale origin as a fraction of the size.

    /// Total length: the delay plus the longest track.
    [[nodiscard]] Duration duration() const;
    /// True when there is nothing to play (no tracks), e.g. for NoPageTransition.
    [[nodiscard]] bool empty() const
    {
        return tracks.empty();
    }
};

/// The transition target `storyboard` gives `elapsed` after it started. During its delay the
/// tracks hold their first values. The target is set to the start values before a staggered
/// storyboard begins, so a staggered entrance stays invisible while it waits.
[[nodiscard]] TransitionTargetState sample_transition(const TransitionStoryboard& storyboard, Duration elapsed);

/// The transform a transition target draws an element at `bounds` (window coordinates) through:
/// its scale about `scale_origin` (relative to the size), then its translation. Identity when the
/// target is at rest.
[[nodiscard]] Matrix transition_transform(const TransitionTargetState& target, const Rect& bounds);

/// The storyboards of the transitions, with the system's own animation timings. `offset` is
/// where the element starts relative to where it ends up.
namespace transition_storyboards
{
/// Entrance: from `offset` and transparent to rest, 667 ms.
[[nodiscard]] TransitionStoryboard entrance(Point offset);
/// Reposition: from `offset` to rest, 367 ms.
[[nodiscard]] TransitionStoryboard reposition(Point offset);
/// Incoming content: from `offset` over 500 ms, fading in from 50 to 220 ms.
[[nodiscard]] TransitionStoryboard content_in(Point offset);
/// Outgoing content: fade out over 50 ms.
[[nodiscard]] TransitionStoryboard content_out();
/// An added item, after `delay`: grow from 90 % and fade in over 333 ms, starting at 166 ms.
[[nodiscard]] TransitionStoryboard add(Duration delay);
/// An item moved by an add or a delete: glide from `offset` over `duration`, after `delay`.
[[nodiscard]] TransitionStoryboard make_room(Point offset, Duration duration, Duration delay);
/// A deleted item: shrink to 90 % and fade out, 100 ms.
[[nodiscard]] TransitionStoryboard remove();
/// An opening popup: from `offset` over 367 ms, fading in from 83 to 166 ms.
[[nodiscard]] TransitionStoryboard popup_in(Point offset);
/// A closing popup: fade out over 83 ms.
[[nodiscard]] TransitionStoryboard popup_out();

/// Which side of a navigation a storyboard is for.
enum class NavigationTrigger
{
    NavigatingTo,       ///< The new page arriving on a forward navigation.
    NavigatingAway,     ///< The old page leaving on a forward navigation.
    BackNavigatingTo,   ///< The previous page returning on go_back().
    BackNavigatingAway, ///< The current page leaving on go_back().
};
/// A page transition's storyboard for one side of a navigation. Empty for
/// NoPageTransition.
[[nodiscard]] TransitionStoryboard navigation(const PageTransition& transition, NavigationTrigger trigger);
} // namespace transition_storyboards

/// The start delay of the element at `index` (from 0) among those started together. It waits
/// (index + 1) times `delay`, so the first one already waits one.
[[nodiscard]] constexpr Duration stagger_delay(std::size_t index, Duration delay)
{
    return static_cast<double>(index + 1) * delay;
}

} // namespace ui
