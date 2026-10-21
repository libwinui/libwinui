// SPDX-License-Identifier: MIT
#pragma once

namespace ui
{

/// A cubic Bézier easing curve. The start and end anchors are fixed at (0,0) and (1,1), with the
/// elapsed-time fraction in and the progress fraction out, both 0..1, so only the two interior
/// control points are given. The default, `easing::Linear`, is a straight ramp, which is what
/// BrushTransition always uses. ScalarTransition and Vector3Transition use a (0.8,0)-(0.2,1)
/// curve. `easing::ControlFastOutSlowIn` is the curve the rest of the themed motion uses. Its
/// control points (0,0)-(0,1) make it a pure decelerate, fast at the start and slow at the end.
///
/// Example:
/// @code
/// const ui::CubicBezier ease_out{0.1f, 0.9f, 0.2f, 1.0f};
/// const float halfway = ui::easing::evaluate(ease_out, 0.5); // progress at half the time
/// @endcode
struct CubicBezier
{
    /// The two interior control points, (x1,y1) and (x2,y2), each coordinate 0..1 (x is time,
    /// y is progress). The default is the linear ramp.
    float x1 = 0.0f, y1 = 0.0f, x2 = 1.0f, y2 = 1.0f;

    friend constexpr bool operator==(const CubicBezier&, const CubicBezier&) = default;
};

/// Named CubicBezier curves and the CPU-side evaluator.
namespace easing
{
/// A straight ramp: progress equals elapsed-time fraction.
inline constexpr CubicBezier Linear{0.0f, 0.0f, 1.0f, 1.0f};
/// A pure decelerate, fast at the start and slow at the end, used by most themed motion.
inline constexpr CubicBezier ControlFastOutSlowIn{0.0f, 0.0f, 0.0f, 1.0f};

/// Evaluates `c` at elapsed-time fraction `x`, returning the progress fraction (the
/// curve's y at the t where its x equals `x`), for a control that eases a value itself, such
/// as Expander's content slide or the toggle knob. `x` is clamped to 0..1.
///
/// x(t) is inverted by 24 steps of bisection, which puts t within 2^-24, far finer than any
/// frame can show.
[[nodiscard]] inline float evaluate(const CubicBezier& c, double x)
{
    const auto axis = [](float p1, float p2, double t)
    {
        const double u = 1.0 - t;
        return 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t;
    };
    x         = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 24; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        if (axis(c.x1, c.x2, mid) < x)
        {
            lo = mid;
        }
        else
        {
            hi = mid;
        }
    }
    return static_cast<float>(axis(c.y1, c.y2, 0.5 * (lo + hi)));
}
} // namespace easing

} // namespace ui
