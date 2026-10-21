// SPDX-License-Identifier: MIT
#include "winui/ui/Animation.hpp"

#include "../render/Compositor.hpp"
#include "AnimationsScope.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <numbers>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <cmath>

namespace ui
{

namespace
{

constexpr float AnimationPi      = std::numbers::pi_v<float>;
constexpr float E                = std::numbers::e_v<float>;
constexpr float AnimationEpsilon = 0.0001f;

// ---- the ease-in curve of each easing function ----

float circle_in(float t)
{
    t = std::clamp(t, -1.0f, 1.0f);
    return -1.0f * (std::sqrt(1.0f - t * t) - 1.0f);
}

float back_in(float t, float amplitude)
{
    t = std::max(t, 0.0f);
    return t * t * t - t * amplitude * std::sin(t * AnimationPi);
}

float exponential_in(float t, float exponent)
{
    if (exponent >= -AnimationEpsilon && exponent <= AnimationEpsilon)
    {
        return t;
    }
    return (std::pow(E, exponent * t) - 1.0f) / (std::pow(E, exponent) - 1.0f);
}

float power_in(float t, float power)
{
    return std::pow(t, std::max(power, 0.0f));
}

float elastic_in(float t, int oscillations_in, float springiness)
{
    const float oscillations = static_cast<float>(std::max(oscillations_in, 0));
    float       modifier     = t;
    if (!(springiness >= -AnimationEpsilon && springiness <= AnimationEpsilon))
    {
        modifier = (std::pow(E, springiness * t) - 1.0f) / (std::pow(E, springiness) - 1.0f);
    }
    return modifier * std::sin(t * (2.0f * AnimationPi * oscillations + AnimationPi / 2.0f));
}

float bounce_in(float t, int bounces_in, float bounciness_in)
{
    const float bounces    = bounces_in >= 0 ? static_cast<float>(bounces_in) : 0.0f;
    const float bounciness = bounciness_in <= 1.0f + AnimationEpsilon ? 1.01f : bounciness_in; // at least 1.01
    const float pow_b      = std::pow(bounciness, bounces);
    const float one_minus  = 1.0f - bounciness;

    // "unit" space: a geometric series with only half the last term.
    const float sum_of_units = (1.0f - pow_b) / one_minus + pow_b / 2.0f;
    const float unit_at_t    = t * sum_of_units;

    // "bounce" space.
    const float bounce_at_t = static_cast<float>(std::log(static_cast<double>(-unit_at_t * (1.0f - bounciness) + 1.0f)) /
                                                 std::log(static_cast<double>(bounciness)));
    const float start = std::floor(bounce_at_t);
    const float end   = start + 1.0f;

    // "time" space.
    const float start_time = (1.0f - std::pow(bounciness, start)) / (one_minus * sum_of_units);
    const float end_time   = (1.0f - std::pow(bounciness, end)) / (one_minus * sum_of_units);

    // A quadratic through (start_time, 0) and (end_time, 0), peaking at the bounce's amplitude.
    const float mid_time  = (start_time + end_time) / 2.0f;
    const float relative  = t - mid_time;
    const float radius    = mid_time - start_time;
    const float amplitude = std::pow(1.0f / bounciness, bounces - start);
    return (-amplitude / (radius * radius)) * (relative - radius) * (relative + radius);
}

float sine_in(float t)
{
    return 1.0f - std::sin((1.0f - t) * (AnimationPi / 2.0f));
}

// The mode picks which end of the ease-in curve applies.
template <typename In>
float apply_mode(EasingMode mode, float t, In&& in)
{
    switch (mode)
    {
        case EasingMode::EaseIn:
            return in(t);
        case EasingMode::EaseOut:
            return 1.0f - in(1.0f - t);
        case EasingMode::EaseInOut:
            return t < 0.5f ? in(t * 2.0f) / 2.0f : (1.0f - in(2.0f - t * 2.0f)) / 2.0f + 0.5f;
    }
    return t;
}

constexpr CubicBezier PvlDecelerate{0.1f, 0.9f, 0.2f, 1.0f}; // the OS timing function 4 (FASTIN)
constexpr CubicBezier PvlShrink{0.11f, 0.5f, 0.24f, 0.96f};  // timing function 6

TransitionTrack track(TransitionChannel channel, std::vector<KeyFrame> frames)
{
    return TransitionTrack{channel, std::move(frames)};
}

// A translation: start offset at 0 (and held until the start time),
// then along the curve to rest.
void add_translate(TransitionStoryboard& sb, Point offset, double start, double duration, CubicBezier curve)
{
    const auto frames = [&](float from)
    {
        std::vector<KeyFrame> f;
        if (start > 0.0)
        {
            f.push_back(KeyFrame::spline(Duration{0.0}, from, curve));
        }
        f.push_back(KeyFrame::spline(Duration{start}, from, curve));
        f.push_back(KeyFrame::spline(Duration{start + duration}, 0.0f, curve));
        return f;
    };
    sb.tracks.push_back(track(TransitionChannel::TranslateX, frames(offset.x)));
    sb.tracks.push_back(track(TransitionChannel::TranslateY, frames(offset.y)));
}

// An opacity fade. `initial` is the opacity to start from (TATF_HASINITIALVALUES), or nullopt to
// fade from the current opacity. A linear curve makes linear key frames.
KeyFrame curve_frame(double time, float value, CubicBezier curve)
{
    return curve == easing::Linear ? KeyFrame::linear(Duration{time}, value)
                                   : KeyFrame::spline(Duration{time}, value, curve);
}

void add_opacity(TransitionStoryboard& sb, std::optional<float> initial, float to, double start, double duration, CubicBezier curve)
{
    std::vector<KeyFrame> f;
    if (initial)
    {
        if (start > 0.0)
        {
            f.push_back(curve_frame(0.0, *initial, curve));
        }
        f.push_back(curve_frame(start, *initial, curve));
    }
    f.push_back(curve_frame(start + duration, to, curve));
    sb.tracks.push_back(track(TransitionChannel::Opacity, std::move(f)));
}

// A scale, both axes.
void add_scale(TransitionStoryboard& sb, std::optional<float> initial, float to, double start, double duration, CubicBezier curve)
{
    for (TransitionChannel channel : {TransitionChannel::ScaleX, TransitionChannel::ScaleY})
    {
        std::vector<KeyFrame> f;
        if (initial)
        {
            if (start > 0.0)
            {
                f.push_back(curve_frame(0.0, *initial, curve));
            }
            f.push_back(curve_frame(start, *initial, curve));
        }
        f.push_back(curve_frame(start + duration, to, curve));
        sb.tracks.push_back(track(channel, std::move(f)));
    }
}

} // namespace

float ease(const EasingFunction& function, float t)
{
    return std::visit(
        [t](const auto& f) -> float
        {
            using F = std::decay_t<decltype(f)>;
            if constexpr (std::is_same_v<F, CubicBezier>)
            {
                // The ends exactly: the bisection lands a hair off them.
                if (t <= 0.0f || t >= 1.0f)
                {
                    return t <= 0.0f ? 0.0f : 1.0f;
                }
                return easing::evaluate(f, t);
            }
            else if constexpr (std::is_same_v<F, BackEase>)
            {
                return apply_mode(f.mode, t, [&](float x) { return back_in(x, f.amplitude); });
            }
            else if constexpr (std::is_same_v<F, BounceEase>)
            {
                return apply_mode(f.mode, t, [&](float x) { return bounce_in(x, f.bounces, f.bounciness); });
            }
            else if constexpr (std::is_same_v<F, CircleEase>)
            {
                return apply_mode(f.mode, t, circle_in);
            }
            else if constexpr (std::is_same_v<F, CubicEase>)
            {
                return apply_mode(f.mode, t, [](float x) { return x * x * x; });
            }
            else if constexpr (std::is_same_v<F, ElasticEase>)
            {
                return apply_mode(f.mode, t, [&](float x) { return elastic_in(x, f.oscillations, f.springiness); });
            }
            else if constexpr (std::is_same_v<F, ExponentialEase>)
            {
                return apply_mode(f.mode, t, [&](float x) { return exponential_in(x, f.exponent); });
            }
            else if constexpr (std::is_same_v<F, PowerEase>)
            {
                return apply_mode(f.mode, t, [&](float x) { return power_in(x, f.power); });
            }
            else if constexpr (std::is_same_v<F, QuadraticEase>)
            {
                return apply_mode(f.mode, t, [](float x) { return x * x; });
            }
            else if constexpr (std::is_same_v<F, QuarticEase>)
            {
                return apply_mode(f.mode, t, [](float x) { return x * x * x * x; });
            }
            else if constexpr (std::is_same_v<F, QuinticEase>)
            {
                return apply_mode(f.mode, t, [](float x) { return x * x * x * x * x; });
            }
            else
            {
                static_assert(std::is_same_v<F, SineEase>);
                return apply_mode(f.mode, t, sine_in);
            }
        },
        function);
}

float sample_key_frames(std::span<const KeyFrame> frames, float base, Duration time)
{
    const double time_ms        = time.count();
    float        previous_value = base;
    double       previous_time  = 0.0;
    for (const KeyFrame& frame : frames)
    {
        if (time_ms < frame.time.count())
        {
            const double span     = frame.time.count() - previous_time;
            const float  progress = span > 0.0 ? static_cast<float>((time_ms - previous_time) / span) : 1.0f;
            float        eased    = progress;
            switch (frame.kind)
            {
                case KeyFrameKind::Linear:
                    break;
                case KeyFrameKind::Discrete:
                    return previous_value;
                case KeyFrameKind::Spline:
                case KeyFrameKind::Easing:
                    eased = ease(frame.easing, progress);
                    break;
            }
            return previous_value + (frame.value - previous_value) * eased;
        }
        previous_value = frame.value;
        previous_time  = frame.time.count();
    }
    return previous_value;
}

Duration key_frames_duration(std::span<const KeyFrame> frames)
{
    Duration end{};
    for (const KeyFrame& frame : frames)
    {
        end = std::max(end, frame.time);
    }
    return end;
}

TransitionTargetState sample_transition(const TransitionStoryboard& storyboard, Duration elapsed)
{
    TransitionTargetState state{};
    state.scale_origin = storyboard.scale_origin;
    const Duration t   = std::max(Duration{}, elapsed - storyboard.delay);
    for (const TransitionTrack& track : storyboard.tracks)
    {
        switch (track.channel)
        {
            case TransitionChannel::Opacity:
                state.opacity = sample_key_frames(track.key_frames, 1.0f, t);
                break;
            case TransitionChannel::TranslateX:
                state.translate.x = sample_key_frames(track.key_frames, 0.0f, t);
                break;
            case TransitionChannel::TranslateY:
                state.translate.y = sample_key_frames(track.key_frames, 0.0f, t);
                break;
            case TransitionChannel::ScaleX:
                state.scale_x = sample_key_frames(track.key_frames, 1.0f, t);
                break;
            case TransitionChannel::ScaleY:
                state.scale_y = sample_key_frames(track.key_frames, 1.0f, t);
                break;
        }
    }
    return state;
}

Matrix transition_transform(const TransitionTargetState& target, const Rect& bounds)
{
    if (target.scale_x == 1.0f && target.scale_y == 1.0f && target.translate == Point{})
    {
        return Matrix{};
    }
    const float ox = bounds.x + target.scale_origin.x * bounds.width;
    const float oy = bounds.y + target.scale_origin.y * bounds.height;
    return Matrix::translation(-ox, -oy) * Matrix::scale(target.scale_x, target.scale_y) * Matrix::translation(ox, oy) *
           Matrix::translation(target.translate.x, target.translate.y);
}

Duration TransitionStoryboard::duration() const
{
    Duration end{};
    for (const TransitionTrack& t : tracks)
    {
        end = std::max(end, key_frames_duration(t.key_frames));
    }
    return delay + end;
}

namespace transition_storyboards
{

// The timings follow the system animation library's storyboards: entrance is ENTERPAGE,
// reposition REPOSITION, content_in/content_out TRANSITIONCONTENT, add/make_room/remove
// ADDTOGRID and DELETEFROMGRID, popup_in SHOWPOPUP and popup_out HIDEPOPUP, as Windows 11 ships
// them.
TransitionStoryboard entrance(Point offset)
{
    TransitionStoryboard sb;
    add_translate(sb, offset, 0.0, 667.0, PvlDecelerate);
    add_opacity(sb, 0.0f, 1.0f, 0.0, 667.0, PvlDecelerate);
    return sb;
}

TransitionStoryboard reposition(Point offset)
{
    TransitionStoryboard sb;
    add_translate(sb, offset, 0.0, 367.0, PvlDecelerate);
    return sb;
}

TransitionStoryboard content_in(Point offset)
{
    TransitionStoryboard sb;
    add_translate(sb, offset, 0.0, 500.0, PvlDecelerate);
    add_opacity(sb, 0.0f, 1.0f, 50.0, 170.0, PvlDecelerate);
    return sb;
}

TransitionStoryboard content_out()
{
    TransitionStoryboard sb;
    add_opacity(sb, std::nullopt, 0.0f, 0.0, 50.0, easing::Linear);
    return sb;
}

TransitionStoryboard add(Duration delay)
{
    TransitionStoryboard sb;
    add_scale(sb, 0.9f, 1.0f, 166.0 + delay.count(), 333.0, PvlDecelerate);
    add_opacity(sb, 0.0f, 1.0f, 166.0 + delay.count(), 333.0, easing::Linear);
    return sb;
}

TransitionStoryboard make_room(Point offset, Duration duration, Duration delay)
{
    TransitionStoryboard sb;
    add_translate(sb, offset, delay.count(), duration.count(), PvlDecelerate);
    return sb;
}

TransitionStoryboard remove()
{
    TransitionStoryboard sb;
    add_scale(sb, std::nullopt, 0.9f, 0.0, 100.0, PvlShrink);
    add_opacity(sb, std::nullopt, 0.0f, 0.0, 100.0, easing::Linear);
    return sb;
}

TransitionStoryboard popup_in(Point offset)
{
    TransitionStoryboard sb;
    add_opacity(sb, 0.0f, 1.0f, 83.0, 83.0, easing::Linear);
    add_translate(sb, offset, 0.0, 367.0, PvlDecelerate);
    return sb;
}

TransitionStoryboard popup_out()
{
    TransitionStoryboard sb;
    add_opacity(sb, std::nullopt, 0.0f, 0.0, 83.0, easing::Linear);
    return sb;
}

namespace
{

// The navigation transitions' timings and the transitions' control points.
constexpr CubicBezier NavIn{0.1f, 0.9f, 0.2f, 1.0f};
constexpr CubicBezier NavOut{0.7f, 0.0f, 1.0f, 0.5f};
constexpr double      NavOutMs = 150.0;
constexpr double      NavInMs  = 300.0;

TransitionStoryboard entrance_navigation(NavigationTrigger trigger)
{
    constexpr float      Offset = 140.0f;
    TransitionStoryboard sb;
    switch (trigger)
    {
        case NavigationTrigger::NavigatingAway:
            sb.tracks.push_back(
                track(TransitionChannel::Opacity,
                      {KeyFrame::discrete(Duration{0.0}, 1.0f), KeyFrame::spline(Duration{NavOutMs}, 0.0f, NavOut)}));
            break;
        case NavigationTrigger::NavigatingTo:
            sb.tracks.push_back(
                track(TransitionChannel::Opacity,
                      {KeyFrame::discrete(Duration{0.0}, 0.0f), KeyFrame::discrete(Duration{NavOutMs}, 1.0f)}));
            sb.tracks.push_back(track(TransitionChannel::TranslateY,
                                      {KeyFrame::discrete(Duration{0.0}, Offset),
                                       KeyFrame::discrete(Duration{NavOutMs}, Offset),
                                       KeyFrame::spline(Duration{NavOutMs + NavInMs}, 0.0f, NavIn)}));
            break;
        case NavigationTrigger::BackNavigatingAway:
            sb.tracks.push_back(
                track(TransitionChannel::Opacity,
                      {KeyFrame::discrete(Duration{0.0}, 1.0f), KeyFrame::discrete(Duration{NavOutMs}, 0.0f)}));
            sb.tracks.push_back(
                track(TransitionChannel::TranslateY,
                      {KeyFrame::discrete(Duration{0.0}, 0.0f), KeyFrame::spline(Duration{NavOutMs}, Offset, NavOut)}));
            break;
        case NavigationTrigger::BackNavigatingTo:
            sb.tracks.push_back(track(TransitionChannel::Opacity,
                                      {KeyFrame::discrete(Duration{0.0}, 0.0f),
                                       KeyFrame::discrete(Duration{NavOutMs}, 0.0f),
                                       KeyFrame::spline(Duration{NavOutMs + NavInMs}, 1.0f, NavIn)}));
            break;
    }
    return sb;
}

TransitionStoryboard slide_navigation(PageSlideDirection direction, NavigationTrigger trigger)
{
    TransitionStoryboard sb;
    if (direction == PageSlideDirection::FromLeft || direction == PageSlideDirection::FromRight)
    {
        constexpr float             Exit     = 150.0f;
        constexpr float             Entrance = -200.0f;
        const float                 f        = direction == PageSlideDirection::FromLeft ? 1.0f : -1.0f;
        const std::vector<KeyFrame> fade_out{KeyFrame::discrete(Duration{0.0}, 1.0f),
                                             KeyFrame::discrete(Duration{NavOutMs}, 0.0f)};
        const std::vector<KeyFrame> fade_in{KeyFrame::discrete(Duration{0.0}, 0.0f),
                                            KeyFrame::discrete(Duration{NavOutMs}, 1.0f)};
        switch (trigger)
        {
            case NavigationTrigger::NavigatingAway:
                sb.tracks.push_back(track(TransitionChannel::Opacity, fade_out));
                sb.tracks.push_back(track(TransitionChannel::TranslateX,
                                          {KeyFrame::discrete(Duration{0.0}, 0.0f),
                                           KeyFrame::spline(Duration{NavOutMs}, Exit * f, NavOut)}));
                break;
            case NavigationTrigger::NavigatingTo:
                sb.tracks.push_back(track(TransitionChannel::Opacity, fade_in));
                sb.tracks.push_back(track(TransitionChannel::TranslateX,
                                          {KeyFrame::discrete(Duration{NavOutMs}, Entrance * f),
                                           KeyFrame::spline(Duration{NavOutMs + NavInMs}, 0.0f, NavIn)}));
                break;
            case NavigationTrigger::BackNavigatingAway:
                sb.tracks.push_back(track(TransitionChannel::Opacity, fade_out));
                sb.tracks.push_back(track(TransitionChannel::TranslateX,
                                          {KeyFrame::discrete(Duration{0.0}, 0.0f),
                                           KeyFrame::spline(Duration{NavOutMs}, Entrance * f, NavOut)}));
                break;
            case NavigationTrigger::BackNavigatingTo:
                sb.tracks.push_back(track(TransitionChannel::Opacity, fade_in));
                sb.tracks.push_back(track(TransitionChannel::TranslateX,
                                          {KeyFrame::discrete(Duration{NavOutMs}, Exit * f),
                                           KeyFrame::spline(Duration{NavOutMs + NavInMs}, 0.0f, NavIn)}));
                break;
        }
        return sb;
    }

    // FromBottom: a 200 DIP offset, switching at 250 ms and ending at 600 ms, with an
    // ExponentialEase of 6.
    constexpr float             Offset = 200.0f;
    constexpr double            Mid    = 250.0;
    constexpr double            End    = 600.0;
    const std::vector<KeyFrame> hide_until_mid{KeyFrame::discrete(Duration{0.0}, 0.0f),
                                               KeyFrame::discrete(Duration{Mid - 10.0}, 0.0f),
                                               KeyFrame::discrete(Duration{Mid}, 1.0f)};
    const std::vector<KeyFrame> show_until_mid{KeyFrame::discrete(Duration{0.0}, 1.0f),
                                               KeyFrame::discrete(Duration{Mid - 10.0}, 1.0f),
                                               KeyFrame::discrete(Duration{Mid}, 0.0f)};
    switch (trigger)
    {
        case NavigationTrigger::NavigatingTo:
            sb.tracks.push_back(track(TransitionChannel::TranslateY,
                                      {KeyFrame::discrete(Duration{0.0}, Offset),
                                       KeyFrame::discrete(Duration{Mid}, Offset),
                                       KeyFrame::eased(Duration{End}, 0.0f, ExponentialEase{6.0f, EasingMode::EaseOut})}));
            sb.tracks.push_back(track(TransitionChannel::Opacity, hide_until_mid));
            break;
        case NavigationTrigger::NavigatingAway:
            sb.tracks.push_back(track(TransitionChannel::Opacity, show_until_mid));
            break;
        case NavigationTrigger::BackNavigatingTo:
            sb.tracks.push_back(track(TransitionChannel::Opacity, hide_until_mid));
            break;
        case NavigationTrigger::BackNavigatingAway:
            sb.tracks.push_back(
                track(TransitionChannel::TranslateY,
                      {KeyFrame::discrete(Duration{0.0}, 0.0f),
                       KeyFrame::eased(Duration{End}, Offset, ExponentialEase{6.0f, EasingMode::EaseIn})}));
            sb.tracks.push_back(track(TransitionChannel::Opacity, show_until_mid));
            break;
    }
    return sb;
}

TransitionStoryboard zoom_navigation(NavigationTrigger trigger)
{
    constexpr CubicBezier ScaleEase{0.1f, 0.9f, 0.2f, 1.0f};
    constexpr CubicBezier BackScale{0.12f, 0.0f, 0.0f, 1.0f};
    constexpr CubicBezier Opacity{0.17f, 0.17f, 0.0f, 1.0f};
    TransitionStoryboard  sb;
    sb.scale_origin  = Point{0.5f, 0.5f};
    const auto scale = [&](float from, float to, double duration, CubicBezier curve)
    {
        for (TransitionChannel c : {TransitionChannel::ScaleX, TransitionChannel::ScaleY})
        {
            sb.tracks.push_back(
                track(c, {KeyFrame::spline(Duration{0.0}, from, curve), KeyFrame::spline(Duration{duration}, to, curve)}));
        }
    };
    const auto opacity = [&](float from, float to, double duration)
    {
        sb.tracks.push_back(
            track(TransitionChannel::Opacity,
                  {KeyFrame::spline(Duration{0.0}, from, Opacity), KeyFrame::spline(Duration{duration}, to, Opacity)}));
    };
    // ZoomPageTransition's durations.
    switch (trigger)
    {
        case NavigationTrigger::NavigatingTo:
            scale(0.94f, 1.0f, 783.0, ScaleEase);
            opacity(0.0f, 1.0f, 333.0);
            break;
        case NavigationTrigger::NavigatingAway:
            scale(1.0f, 1.04f, 100.0, ScaleEase);
            opacity(1.0f, 0.0f, 100.0);
            break;
        case NavigationTrigger::BackNavigatingTo:
            scale(1.06f, 1.0f, 333.0, BackScale);
            opacity(0.0f, 1.0f, 333.0);
            break;
        case NavigationTrigger::BackNavigatingAway:
            scale(1.0f, 0.96f, 100.0, ScaleEase);
            opacity(1.0f, 0.0f, 100.0);
            break;
    }
    return sb;
}

// FadeThroughPageTransition's "background" (the page) animations. Its exit/entrance element
// parts would rotate in 3D and are not built, and nothing marks such an element here.
TransitionStoryboard fade_through_navigation(NavigationTrigger trigger)
{
    const EasingFunction circle_out = CircleEase{EasingMode::EaseOut};
    TransitionStoryboard sb;
    sb.scale_origin = Point{0.5f, 0.5f};
    switch (trigger)
    {
        case NavigationTrigger::NavigatingTo:
        {
            constexpr double Start = 267.0;
            constexpr double End   = 350.0 + 267.0;
            for (TransitionChannel c : {TransitionChannel::ScaleX, TransitionChannel::ScaleY})
            {
                sb.tracks.push_back(track(c,
                                          {KeyFrame::eased(Duration{Start}, 0.9f, circle_out),
                                           KeyFrame::eased(Duration{End}, 1.0f, circle_out)}));
            }
            sb.tracks.push_back(track(TransitionChannel::Opacity,
                                      {KeyFrame::linear(Duration{0.0}, 0.0f),
                                       KeyFrame::linear(Duration{Start - 1.0}, 0.0f),
                                       KeyFrame::linear(Duration{Start}, 1.0f)}));
            break;
        }
        case NavigationTrigger::NavigatingAway:
            sb.tracks.push_back(track(TransitionChannel::Opacity,
                                      {KeyFrame::linear(Duration{0.0}, 1.0f),
                                       KeyFrame::linear(Duration{0.0}, 1.0f),
                                       KeyFrame::linear(Duration{120.0}, 1.0f),
                                       KeyFrame::eased(Duration{250.0}, 0.0f, circle_out)}));
            break;
        case NavigationTrigger::BackNavigatingTo:
            sb.tracks.push_back(track(TransitionChannel::Opacity,
                                      {KeyFrame::linear(Duration{0.0}, 0.0f),
                                       KeyFrame::eased(Duration{267.0}, 0.0f, circle_out),
                                       KeyFrame::eased(Duration{617.0}, 1.0f, circle_out)}));
            break;
        case NavigationTrigger::BackNavigatingAway:
            sb.tracks.push_back(
                track(TransitionChannel::TranslateY,
                      {KeyFrame::linear(Duration{0.0}, 0.0f),
                       KeyFrame::eased(Duration{250.0}, 200.0f, ExponentialEase{6.0f, EasingMode::EaseIn})}));
            sb.tracks.push_back(track(TransitionChannel::Opacity,
                                      {KeyFrame::linear(Duration{0.0}, 1.0f),
                                       KeyFrame::linear(Duration{0.0}, 1.0f),
                                       KeyFrame::linear(Duration{240.0}, 1.0f),
                                       KeyFrame::linear(Duration{250.0}, 0.0f)}));
            break;
    }
    return sb;
}

} // namespace

TransitionStoryboard navigation(const PageTransition& transition, NavigationTrigger trigger)
{
    return std::visit(
        [trigger](const auto& i) -> TransitionStoryboard
        {
            using I = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<I, NoPageTransition>)
            {
                return {};
            }
            else if constexpr (std::is_same_v<I, ZoomPageTransition>)
            {
                return zoom_navigation(trigger);
            }
            else if constexpr (std::is_same_v<I, SlidePageTransition>)
            {
                return slide_navigation(i.direction, trigger);
            }
            else if constexpr (std::is_same_v<I, FadeThroughPageTransition>)
            {
                return fade_through_navigation(trigger);
            }
            else
            {
                static_assert(std::is_same_v<I, RisePageTransition>);
                return entrance_navigation(trigger);
            }
        },
        transition);
}

} // namespace transition_storyboards

// ---- the animations switch ----

namespace
{
thread_local bool animations_on = true;

// Longer than any finite animation runs, short enough to stay exact in a double.
constexpr double FinishingDeltaMs = 1.0e9;
} // namespace

bool animations_enabled()
{
    return animations_on;
}

Duration animation_delta(Duration delta)
{
    return animations_on ? delta : std::max(delta, Duration{FinishingDeltaMs});
}

namespace detail
{

void RectTween::retarget(const Rect& to, Duration duration, const CubicBezier& curve)
{
    if (known_ && to == to_)
    {
        return;
    }
    from_     = known_ ? current(to_) : to;
    to_       = to;
    curve_    = curve;
    elapsed_  = 0.0;
    duration_ = duration.count();
    active_   = known_ && animations_on && duration_ > 0.0 && !(from_ == to_);
    fresh_    = true;
    known_    = true;
}

bool RectTween::advance(Duration delta)
{
    if (!active_)
    {
        return false;
    }
    // The first tick after a change can carry a whole idle stretch, which would end the tween
    // before it was ever drawn; it counts as one frame at most.
    elapsed_ += fresh_ ? std::min(animation_delta(delta).count(), 1000.0 / 60.0) : animation_delta(delta).count();
    fresh_ = false;
    if (elapsed_ >= duration_)
    {
        active_ = false;
    }
    return true;
}

void RectTween::reset()
{
    known_  = false;
    active_ = false;
}

Rect RectTween::current(const Rect& to) const
{
    if (!active_)
    {
        return to;
    }
    const float t = easing::evaluate(curve_, elapsed_ / duration_);
    return Rect{from_.left() + (to_.left() - from_.left()) * t,
                from_.top() + (to_.top() - from_.top()) * t,
                from_.width + (to_.width - from_.width) * t,
                from_.height + (to_.height - from_.height) * t};
}

AnimationsScope::AnimationsScope(bool enabled) : previous_(animations_on)
{
    animations_on = enabled;
}

AnimationsScope::~AnimationsScope()
{
    animations_on = previous_;
}

void apply_animations_switch(Theme& theme)
{
    if (theme.animations_enabled)
    {
        return;
    }
    theme.anim_faster = theme.anim_fast = theme.anim_normal = std::chrono::milliseconds{0};
    theme.popup_motion.menu                                 = ThemePopupAnimation::None;
    theme.popup_motion.list                                 = ThemePopupAnimation::None;
    theme.popup_motion.open                                 = std::chrono::milliseconds{0};
    theme.popup_motion.close                                = std::chrono::milliseconds{0};
}

} // namespace detail

} // namespace ui
