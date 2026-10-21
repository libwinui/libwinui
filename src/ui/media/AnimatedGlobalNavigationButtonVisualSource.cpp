// SPDX-License-Identifier: MIT
// The AnimatedGlobalNavigationButtonVisualSource animation (80 frames at 60 fps).
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
constexpr Easing Cubic1 = cubic(0.850000024f, 0.0f, 0.75f, 1.0f);
constexpr Easing Cubic2 = cubic(0.349999994f, 0.0f, 0.0f, 1.0f);
constexpr Easing Cubic3 = cubic(0.166999996f, 0.0f, 0.0f, 1.0f);

// The right round caps of the top, middle and bottom bars.
const PathGeometry& geometry_00()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(7.5f, 0.5f)
                                      .bezier(7.63500023f, 0.5f, 7.75299978f, 0.451000005f, 7.85200024f, 0.351999998f)
                                      .bezier(7.95100021f, 0.252999991f, 8.0f, 0.135000005f, 8.0f, 0.0f)
                                      .bezier(8.0f, -0.135000005f, 7.95100021f, -0.252999991f, 7.85200024f, -0.351999998f)
                                      .bezier(7.75299978f, -0.451000005f, 7.63500023f, -0.5f, 7.5f, -0.5f)
                                      .line(7.5f, 0.5f)
                                      .end(true)
                                      .build();
    return g;
}

const PathGeometry& geometry_01()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(7.5f, -0.486999989f)
                                      .line(7.5f, 0.513000011f)
                                      .bezier(7.63500023f, 0.513000011f, 7.75299978f, 0.463f, 7.85200024f, 0.363999993f)
                                      .bezier(7.95100021f, 0.264999986f, 8.0f, 0.148000002f, 8.0f, 0.0130000003f)
                                      .bezier(8.0f, -0.122000001f, 7.95100021f, -0.239999995f, 7.85200024f, -0.338999987f)
                                      .bezier(7.75299978f, -0.437999994f, 7.63500023f, -0.486999989f, 7.5f, -0.486999989f)
                                      .end(true)
                                      .build();
    return g;
}

const PathGeometry& geometry_02()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(7.5f, -0.5f)
                                      .line(7.5f, 0.5f)
                                      .bezier(7.63500023f, 0.5f, 7.75299978f, 0.451000005f, 7.85200024f, 0.351999998f)
                                      .bezier(7.95100021f, 0.252999991f, 8.0f, 0.135000005f, 8.0f, 0.0f)
                                      .bezier(8.0f, -0.135000005f, 7.95100021f, -0.252999991f, 7.85200024f, -0.351999998f)
                                      .bezier(7.75299978f, -0.451000005f, 7.63500023f, -0.5f, 7.5f, -0.5f)
                                      .end(true)
                                      .build();
    return g;
}

// The left round caps.
const PathGeometry& geometry_03()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(-7.5f, 0.5f)
                                      .line(-7.5f, -0.5f)
                                      .bezier(-7.63500023f, -0.5f, -7.75299978f, -0.451000005f, -7.85200024f, -0.351999998f)
                                      .bezier(-7.95100021f, -0.252999991f, -8.0f, -0.135000005f, -8.0f, 0.0f)
                                      .bezier(-8.0f, 0.135000005f, -7.95100021f, 0.252999991f, -7.85200024f, 0.351999998f)
                                      .bezier(-7.75299978f, 0.451000005f, -7.63500023f, 0.5f, -7.5f, 0.5f)
                                      .end(true)
                                      .build();
    return g;
}

const PathGeometry& geometry_04()
{
    static const PathGeometry
        g = PathBuilder(FillRule::Nonzero)
                .begin(-7.5f, -0.486999989f)
                .bezier(-7.63500023f, -0.486999989f, -7.75299978f, -0.437999994f, -7.85200024f, -0.338999987f)
                .bezier(-7.95100021f, -0.239999995f, -8.0f, -0.122000001f, -8.0f, 0.0130000003f)
                .bezier(-8.0f, 0.148000002f, -7.95100021f, 0.264999986f, -7.85200024f, 0.363999993f)
                .bezier(-7.75299978f, 0.463f, -7.63500023f, 0.513000011f, -7.5f, 0.513000011f)
                .line(-7.5f, -0.486999989f)
                .end(true)
                .build();
    return g;
}

const PathGeometry& geometry_05()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(-7.5f, -0.5f)
                                      .bezier(-7.63500023f, -0.5f, -7.75299978f, -0.451000005f, -7.85200024f, -0.351999998f)
                                      .bezier(-7.95100021f, -0.252999991f, -8.0f, -0.135000005f, -8.0f, 0.0f)
                                      .bezier(-8.0f, 0.135000005f, -7.95100021f, 0.252999991f, -7.85200024f, 0.351999998f)
                                      .bezier(-7.75299978f, 0.451000005f, -7.63500023f, 0.5f, -7.5f, 0.5f)
                                      .line(-7.5f, -0.5f)
                                      .end(true)
                                      .build();
    return g;
}

// The bars' straight bodies in the pressed layer (top, middle, bottom).
const PathGeometry& geometry_06()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(-7.60599995f, 0.5f)
                                      .line(7.70100021f, 0.5f)
                                      .line(7.70100021f, -0.5f)
                                      .line(-7.60599995f, -0.5f)
                                      .line(-7.60599995f, 0.5f)
                                      .end(true)
                                      .build();
    return g;
}

const PathGeometry& geometry_07()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(7.70100021f, -0.486999989f)
                                      .line(-7.60599995f, -0.486999989f)
                                      .line(-7.60599995f, 0.513000011f)
                                      .line(7.70100021f, 0.513000011f)
                                      .line(7.70100021f, -0.486999989f)
                                      .end(true)
                                      .build();
    return g;
}

const PathGeometry& geometry_08()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(7.70100021f, -0.5f)
                                      .line(-7.60500002f, -0.5f)
                                      .line(-7.60500002f, 0.5f)
                                      .line(7.70100021f, 0.5f)
                                      .line(7.70100021f, -0.5f)
                                      .end(true)
                                      .build();
    return g;
}

// The bars' straight bodies in the pointer-over layer.
const PathGeometry& geometry_09()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(-7.5999999f, 0.5f)
                                      .line(7.6960001f, 0.5f)
                                      .line(7.6960001f, -0.5f)
                                      .line(-7.5999999f, -0.5f)
                                      .line(-7.5999999f, 0.5f)
                                      .end(true)
                                      .build();
    return g;
}

const PathGeometry& geometry_10()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(7.6960001f, -0.486999989f)
                                      .line(-7.5999999f, -0.486999989f)
                                      .line(-7.5999999f, 0.513000011f)
                                      .line(7.6960001f, 0.513000011f)
                                      .line(7.6960001f, -0.486999989f)
                                      .end(true)
                                      .build();
    return g;
}

const PathGeometry& geometry_11()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(7.6960001f, -0.5f)
                                      .line(-7.59899998f, -0.5f)
                                      .line(-7.59899998f, 0.5f)
                                      .line(7.6960001f, 0.5f)
                                      .line(7.6960001f, -0.5f)
                                      .end(true)
                                      .build();
    return g;
}

// The three bars in each group: the top one offset by (0, -10), the middle one by (0, -4.85),
// the bottom one untransformed.
void fill_bars(const Painter&      painter,
               const PathGeometry& top,
               const PathGeometry& middle,
               const PathGeometry& bottom,
               const Matrix&       container)
{
    painter.fill(top, Matrix::translation(0.0f, -10.0f) * container);
    painter.fill(middle, Matrix::translation(0.0f, -4.8499999f) * container);
    painter.fill(bottom, container);
}

} // namespace

Duration AnimatedGlobalNavigationButtonVisualSource::duration() const
{
    return Duration{DurationMs};
}

const std::unordered_map<std::string, double>& AnimatedGlobalNavigationButtonVisualSource::markers() const
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

void AnimatedGlobalNavigationButtonVisualSource::render_icon_frame(
    Renderer&    renderer,
    const Rect&  bounds,
    double       progress,
    const Color& foreground) const
{
    const float   p = static_cast<float>(progress);
    const Painter painter(renderer, bounds, natural_size(), foreground);

    // The pressed transitions: hidden until 0.125, opacity animated, identity transform.
    const bool  visible0 = animate_bool({{0.125f, true}}, p, false);
    const float opacity0 = animate<float>({{0.0f, 1.0f, StepThenHold},
                                           {0.125f, 1.0f, HoldThenStep},
                                           {0.25f, 0.0f, HoldThenStep},
                                           {0.375f, 1.0f, HoldThenStep}},
                                          p,
                                          1.0f);
    if (visible0 && opacity0 > 0.0f)
    {
        painter.push_clip(Rect{0.0f, 0.0f, 48.0f, 48.0f}); // the layer's clip
        if (opacity0 < 1.0f)
        {
            painter.push_opacity(opacity0);
        }

        // The right caps, scaled 2.75 about (7.5, -5), their offset animated.
        const Point          right_offset = animate<Point>({{0.0f, Point{37.5079994f, 29.0f}, StepThenHold},
                                                            {0.125f, Point{37.5079994f, 29.0f}, HoldThenStep},
                                                            {0.237499997f, Point{27.5079994f, 29.0f}, Cubic0},
                                                            {0.375f, Point{37.5079994f, 29.0f}, HoldThenStep},
                                                            {0.487500012f, Point{27.5079994f, 29.0f}, Cubic0},
                                                            {0.5f, Point{27.5079994f, 29.0f}, HoldThenStep},
                                                            {0.600000024f, Point{38.5040016f, 29.0f}, Cubic1},
                                                            {0.737500012f, Point{37.5079994f, 29.0f}, Cubic2},
                                                            {0.75f, Point{27.5079994f, 29.0f}, HoldThenStep},
                                                            {0.850000024f, Point{38.5040016f, 29.0f}, Cubic1},
                                                            {0.987500012f, Point{37.5079994f, 29.0f}, Cubic2}},
                                                           p,
                                                           Point{});
        const ShapeTransform right{.center = {7.5f, -5.0f}, .scale = {2.75f, 2.75f}, .offset = right_offset};
        fill_bars(painter, geometry_00(), geometry_01(), geometry_02(), right.to_matrix());

        // The left caps, scaled 2.75 about (-7.5, -5), their offset animated.
        const Point          left_offset = animate<Point>({{0.0f, Point{10.5039997f, 29.0f}, StepThenHold},
                                                           {0.125f, Point{10.5039997f, 29.0f}, HoldThenStep},
                                                           {0.237499997f, Point{20.5079994f, 29.0f}, Cubic0},
                                                           {0.375f, Point{10.5039997f, 29.0f}, HoldThenStep},
                                                           {0.487500012f, Point{20.5079994f, 29.0f}, Cubic0},
                                                           {0.5f, Point{20.5079994f, 29.0f}, HoldThenStep},
                                                           {0.600000024f, Point{9.50800037f, 29.0f}, Cubic1},
                                                           {0.737500012f, Point{10.5039997f, 29.0f}, Cubic2},
                                                           {0.75f, Point{20.5079994f, 29.0f}, HoldThenStep},
                                                           {0.850000024f, Point{9.50800037f, 29.0f}, Cubic1},
                                                           {0.987500012f, Point{10.5039997f, 29.0f}, Cubic2}},
                                                          p,
                                                          Point{});
        const ShapeTransform left{.center = {-7.5f, -5.0f}, .scale = {2.75f, 2.75f}, .offset = left_offset};
        fill_bars(painter, geometry_03(), geometry_04(), geometry_05(), left.to_matrix());

        // The bodies, offset (23.956, 29), their x and y scale animated about (0.048, -5).
        const float          body_sx = animate<float>({{0.0f, 2.74900007f, StepThenHold},
                                                       {0.125f, 2.74900007f, HoldThenStep},
                                                       {0.237499997f, 1.43256998f, Cubic0},
                                                       {0.375f, 2.74900007f, HoldThenStep},
                                                       {0.487500012f, 1.43256998f, Cubic0},
                                                       {0.5f, 1.43256998f, HoldThenStep},
                                                       {0.600000024f, 2.88000011f, Cubic1},
                                                       {0.737500012f, 2.74850011f, Cubic2},
                                                       {0.75f, 1.43256998f, HoldThenStep},
                                                       {0.850000024f, 2.88000011f, Cubic1},
                                                       {0.987500012f, 2.74850011f, Cubic2}},
                                                      p,
                                                      1.0f);
        const float          body_sy = animate<float>({{0.0f, 2.75f, StepThenHold},
                                                       {0.125f, 2.75f, HoldThenStep},
                                                       {0.237499997f, 2.75f, Cubic3},
                                                       {0.375f, 2.75f, HoldThenStep},
                                                       {0.487500012f, 2.75f, Cubic3},
                                                       {0.5f, 2.75f, HoldThenStep},
                                                       {0.600000024f, 2.73294997f, Cubic1},
                                                       {0.737500012f, 2.75f, Cubic2},
                                                       {0.75f, 2.75f, HoldThenStep},
                                                       {0.850000024f, 2.73294997f, Cubic1},
                                                       {0.987500012f, 2.75f, Cubic2}},
                                                      p,
                                                      1.0f);
        const ShapeTransform body{.center = {0.0480000004f, -5.0f},
                                  .scale  = {body_sx, body_sy},
                                  .offset = {23.9559994f, 29.0f}};
        fill_bars(painter, geometry_06(), geometry_07(), geometry_08(), body.to_matrix());

        if (opacity0 < 1.0f)
        {
            painter.pop_opacity();
        }
        painter.pop_clip();
    }

    // The pointer-over transitions, a static hamburger: visibility and opacity animated, identity
    // transform.
    const bool visible1 = animate_bool({{0.375f, false}}, p, true);
    const float opacity1 = animate<float>({{0.0f, 1.0f, HoldThenStep}, {0.125f, 0.0f, HoldThenStep}, {0.25f, 1.0f, HoldThenStep}},
                                          p,
                                          1.0f);
    if (visible1 && opacity1 > 0.0f)
    {
        painter.push_clip(Rect{0.0f, 0.0f, 48.0f, 48.0f}); // the layer's clip
        if (opacity1 < 1.0f)
        {
            painter.push_opacity(opacity1);
        }

        // The right and left caps, the same geometries as above.
        painter.fill(geometry_00(), Matrix{2.75f, 0.0f, 0.0f, 2.75f, 24.3829994f, 10.25f});
        painter.fill(geometry_01(), Matrix{2.75f, 0.0f, 0.0f, 2.75f, 24.3829994f, 24.4125004f});
        painter.fill(geometry_02(), Matrix{2.75f, 0.0f, 0.0f, 2.75f, 24.3829994f, 37.75f});
        painter.fill(geometry_03(), Matrix{2.75f, 0.0f, 0.0f, 2.75f, 23.6289997f, 10.25f});
        painter.fill(geometry_04(), Matrix{2.75f, 0.0f, 0.0f, 2.75f, 23.6289997f, 24.4125004f});
        painter.fill(geometry_05(), Matrix{2.75f, 0.0f, 0.0f, 2.75f, 23.6289997f, 37.75f});

        // The bodies. The animation names a -0.00914 degree rotation, which
        // is already rounded out of these matrices.
        painter.fill(geometry_09(), Matrix{2.74900007f, 0.0f, 0.0f, 2.75f, 23.8720474f, 10.25f});
        painter.fill(geometry_10(), Matrix{2.74900007f, 0.0f, 0.0f, 2.75f, 23.8720474f, 24.4125004f});
        painter.fill(geometry_11(), Matrix{2.74900007f, 0.0f, 0.0f, 2.75f, 23.8720474f, 37.75f});

        if (opacity1 < 1.0f)
        {
            painter.pop_opacity();
        }
        painter.pop_clip();
    }
}

} // namespace ui
