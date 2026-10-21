// SPDX-License-Identifier: MIT
// The AnimatedFindVisualSource animation (80 frames at 60 fps).
// Comments name each layer and shape by what it draws.
#include "AnimatedVisualsDetail.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/media/AnimatedVisuals.hpp"

#include <string>
#include <unordered_map>

namespace ui
{

using namespace lottie;

namespace
{

constexpr double DurationMs = 13333333.0 / 10000.0; // 13333333 ticks of 100 ns

// The cubic easings, in order of first use.
constexpr Easing Cubic0 = cubic(0.166999996f, 0.166999996f, 0.0f, 1.0f);
constexpr Easing Cubic1 = cubic(0.550000012f, 0.0f, 0.75f, 1.0f);
constexpr Easing Cubic2 = cubic(0.349999994f, 0.0f, 0.0f, 1.0f);
constexpr Easing Cubic3 = cubic(0.166999996f, 0.166999996f, 0.449999988f, 1.0f);
constexpr Easing Cubic4 = cubic(0.449999988f, 0.0f, 0.75f, 1.0f);
constexpr Easing Cubic5 = cubic(0.75f, 0.0f, 0.75f, 1.0f);

// The handle, in its group's coordinates.
const PathGeometry& handle()
{
    static const PathGeometry
        g = PathBuilder(FillRule::Nonzero)
                .begin(12.9329996f, 278.140015f)
                .line(9.42300034f, 281.64801f)
                .bezier(9.25599957f, 281.815002f, 9.25599957f, 282.087006f, 9.42300034f, 282.253998f)
                .bezier(9.57199955f, 282.403015f, 9.80300045f, 282.419006f, 9.97000027f, 282.303009f)
                .line(10.0290003f, 282.253998f)
                .line(13.5340004f, 278.748993f)
                .bezier(13.5340004f, 278.748993f, 13.3599997f, 278.609985f, 13.1879997f, 278.432007f)
                .bezier(13.0159998f, 278.253998f, 12.9329996f, 278.140015f, 12.9329996f, 278.140015f)
                .end(true)
                .build();
    return g;
}

} // namespace

Duration AnimatedFindVisualSource::duration() const
{
    return Duration{DurationMs};
}

const std::unordered_map<std::string, double>& AnimatedFindVisualSource::markers() const
{
    static const std::unordered_map<std::string, double> Markers{
        {"NormalToPointerOver_Start", 0.0},
        {"NormalToPointerOver_End", 0.113125},
        {"NormalToPressed_Start", 0.125625},
        {"NormalToPressed_End", 0.238125},
        {"PointerOverToNormal_Start", 0.250625},
        {"PointerOverToNormal_End", 0.363125},
        {"PointerOverToPressed_Start", 0.375625},
        {"PointerOverToPressed_End", 0.488125},
        {"PressedToNormal_Start", 0.500625},
        {"PressedToNormal_End", 0.738125},
        {"PressedToPointerOver_Start", 0.750625},
        {"PressedToPointerOver_End", 0.988125},
    };
    return Markers;
}

void AnimatedFindVisualSource::render_icon_frame(Renderer& renderer, const Rect& bounds, double progress, const Color& foreground) const
{
    const float   p = static_cast<float>(progress);
    const Painter painter(renderer, bounds, natural_size(), foreground);

    // The progress along each path key, which the handle's offset runs on.
    const float t[3] = {
        animate<float>({{0.125000015f, 0.0f, StepThenHold},
                        {0.237499982f, 1.0f, Cubic3},
                        {0.37500003f, 0.0f, StepThenHold},
                        {0.487499982f, 1.0f, Cubic3}},
                       p,
                       0.0f),
        animate<float>({{0.50000006f, 0.0f, StepThenHold},
                        {0.56249994f, 1.0f, Cubic5},
                        {0.5625f, 0.0f, StepThenHold},
                        {0.737499952f, 1.0f, Cubic2}},
                       p,
                       0.0f),
        animate<float>({{0.75000006f, 0.0f, StepThenHold},
                        {0.81249994f, 1.0f, Cubic5},
                        {0.8125f, 0.0f, StepThenHold},
                        {0.987499952f, 1.0f, Cubic2}},
                       p,
                       0.0f),
    };

    painter.push_clip(Rect{0.0f, 0.0f, 48.0f, 48.0f}); // the layer's clip

    // The handle, its offset moving along a path.
    constexpr Point Rest{5.64699984f, -249.322998f};
    constexpr Point Out{7.39699984f, -251.072998f};
    constexpr Point In{3.616f, -247.292f};
    const Point     offset = animate_path(
        {
            value_key(0.0f, Rest, HoldThenStep),
            value_key(0.125f, Rest, HoldThenStep),
            path_key(0.237499982f, Rest, Point{5.939f, -249.615f}, Point{7.105f, -250.781f}, Out, 0, StepThenHold),
            value_key(0.237499997f, Out, StepThenHold),
            value_key(0.25f, Rest, HoldThenStep),
            value_key(0.375f, Rest, HoldThenStep),
            path_key(0.487499982f, Rest, Point{5.939f, -249.615f}, Point{7.105f, -250.781f}, Out, 0, StepThenHold),
            value_key(0.487500012f, Out, StepThenHold),
            value_key(0.5f, Out, HoldThenStep),
            path_key(0.56249994f, Out, Point{6.767f, -250.443f}, In, In, 1, StepThenHold),
            path_key(0.737499952f, In, In, Point{5.308f, -248.984f}, Rest, 1, StepThenHold),
            value_key(0.737500012f, Rest, StepThenHold),
            value_key(0.75f, Out, HoldThenStep),
            path_key(0.81249994f, Out, Point{6.767f, -250.443f}, In, In, 2, StepThenHold),
            path_key(0.987499952f, In, In, Point{5.308f, -248.984f}, Rest, 2, StepThenHold),
            value_key(0.987500012f, Rest, StepThenHold),
        },
        p,
        Point{},
        t);
    const ShapeTransform handle_transform{.center = {13.566f, 278.109985f}, .scale = {3.0f, 3.0f}, .offset = offset};
    painter.fill(handle(), handle_transform.to_matrix());

    // The lens at opacity 0.97, a stroked circle in a scaled group.
    painter.push_opacity(0.970000029f);
    const float scale     = animate<float>({{0.0f, 1.0f, HoldThenStep},
                                            {0.125f, 1.0f, HoldThenStep},
                                            {0.237499997f, 0.800000012f, Cubic3},
                                            {0.25f, 1.0f, HoldThenStep},
                                            {0.375f, 1.0f, HoldThenStep},
                                            {0.487500012f, 0.800000012f, Cubic3},
                                            {0.5f, 0.800000012f, HoldThenStep},
                                            {0.5625f, 1.20000005f, Cubic4},
                                            {0.737500012f, 1.0f, Cubic2},
                                            {0.75f, 0.800000012f, HoldThenStep},
                                            {0.8125f, 1.20000005f, Cubic4},
                                            {0.987500012f, 1.0f, Cubic2}},
                                           p,
                                           1.0f);
    const float thickness = animate<float>({{0.0f, 2.5f, HoldThenStep},
                                            {0.125f, 2.5f, HoldThenStep},
                                            {0.237499997f, 3.0f, Cubic0},
                                            {0.25f, 2.5f, HoldThenStep},
                                            {0.375f, 2.5f, HoldThenStep},
                                            {0.487500012f, 3.0f, Cubic0},
                                            {0.5f, 3.0f, HoldThenStep},
                                            {0.5625f, 2.20000005f, Cubic1},
                                            {0.737500012f, 2.5f, Cubic2},
                                            {0.75f, 3.0f, HoldThenStep},
                                            {0.8125f, 2.20000005f, Cubic1},
                                            {0.987500012f, 2.5f, Cubic2}},
                                           p,
                                           1.0f);
    const ShapeTransform container{.center = {3.81299996f, -3.93799996f}, .scale = {scale, scale}, .offset = {24.0f, 24.0f}};
    const ShapeTransform      lens{.matrix = Matrix{0.922860026f, 0.0f, 0.0f, 0.922860026f, 3.81200004f, -3.93799996f}};
    static const PathGeometry Lens = PathGeometry::ellipse(Point{}, 14.1875f, 14.1875f); // radius 14.1875
    painter.stroke(Lens, lens.to_matrix() * container.to_matrix(), thickness);
    painter.pop_opacity();

    painter.pop_clip();
}

} // namespace ui
