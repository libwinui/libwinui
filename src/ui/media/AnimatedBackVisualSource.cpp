// SPDX-License-Identifier: MIT
// The AnimatedBackVisualSource animation (80 frames at 60 fps).
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
constexpr Easing Cubic3 = cubic(0.166999996f, 0.0f, 0.0f, 1.0f);
constexpr Easing Cubic4 = cubic(0.850000024f, 0.0f, 0.75f, 1.0f);

// The round cap at the arrow's tail.
const PathGeometry& round_cap()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(7.5f, 0.499000013f)
                                      .bezier(7.77600002f, 0.499000013f, 8.0f, 0.275000006f, 8.0f, -0.00100000005f)
                                      .bezier(8.0f, -0.27700001f, 7.77600002f, -0.500999987f, 7.5f, -0.500999987f)
                                      .line(7.5f, 0.499000013f)
                                      .end(true)
                                      .build();
    return g;
}

// The stem.
const PathGeometry& stem()
{
    static const PathGeometry g = PathBuilder(FillRule::Nonzero)
                                      .begin(-5.33400011f, 0.495000005f)
                                      .line(7.5f, 0.499000013f)
                                      .line(7.5f, -0.500999987f)
                                      .line(-5.33099985f, -0.504999995f)
                                      .line(-5.33400011f, 0.495000005f)
                                      .end(true)
                                      .build();
    return g;
}

// The arrow head.
const PathGeometry& head()
{
    static const PathGeometry
        g = PathBuilder(FillRule::Nonzero)
                .begin(-0.837000012f, 6.86800003f)
                .bezier(-0.633000016f, 7.0539999f, -0.317000002f, 7.03999996f, -0.130999997f, 6.83599997f)
                .bezier(0.0549999997f, 6.63199997f, 0.0410000011f, 6.31500006f, -0.163000003f, 6.12900019f)
                .line(-6.33099985f, 0.499000013f)
                .line(-5.32800007f, 0.488999993f)
                .line(-5.32800007f, -0.510999978f)
                .line(-6.32800007f, -0.500999987f)
                .line(-0.163000003f, -6.12900019f)
                .bezier(0.0410000011f, -6.31500006f, 0.0549999997f, -6.63199997f, -0.130999997f, -6.83599997f)
                .bezier(-0.317000002f, -7.03999996f, -0.633000016f, -7.0539999f, -0.837000012f, -6.86800003f)
                .line(-7.75299978f, -0.55400002f)
                .bezier(-7.89300013f, -0.425999999f, -7.97300005f, -0.257999986f, -7.99300003f, -0.0850000009f)
                .bezier(-7.99800014f, -0.0579999983f, -8.0f, -0.0299999993f, -8.0f, -0.00100000005f)
                .bezier(-8.0f, 0.0260000005f, -7.99800014f, 0.0520000011f, -7.99399996f, 0.0780000016f)
                .bezier(-7.97599983f, 0.254000008f, -7.89499998f, 0.423999995f, -7.75299978f, 0.55400002f)
                .line(-0.837000012f, 6.86800003f)
                .end(true)
                .build();
    return g;
}

} // namespace

Duration AnimatedBackVisualSource::duration() const
{
    return Duration{DurationMs};
}

const std::unordered_map<std::string, double>& AnimatedBackVisualSource::markers() const
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

void AnimatedBackVisualSource::render_icon_frame(Renderer& renderer, const Rect& bounds, double progress, const Color& foreground) const
{
    const float   p = static_cast<float>(progress);
    const Painter painter(renderer, bounds, natural_size(), foreground);

    // The progress along a path key, which the tail group's offset runs on.
    const float t[1] = {
        animate<float>({{0.62500006f, 0.0f, StepThenHold},
                        {0.737499952f, 1.0f, Cubic2},
                        {0.87500006f, 0.0f, StepThenHold},
                        {0.987499952f, 1.0f, Cubic2}},
                       p,
                       0.0f),
    };

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

        // The tail group, its offset animated.
        constexpr Point Rest{24.0f, 24.0f};
        constexpr Point Pushed{24.9379997f, 24.0f};
        constexpr Point Pulled{20.25f, 24.0f};
        const Point     cap_offset = animate_path(
            {
                value_key(0.0f, Rest, StepThenHold),
                value_key(0.125f, Rest, HoldThenStep),
                value_key(0.237499997f, Pushed, Cubic0),
                value_key(0.375f, Rest, HoldThenStep),
                value_key(0.487500012f, Pushed, Cubic0),
                value_key(0.5f, Pushed, HoldThenStep),
                value_key(0.625f, Pulled, Cubic1),
                path_key(0.737499952f, Pulled, Point{20.094f, 24.0f}, Point{23.375f, 24.0f}, Rest, 0, StepThenHold),
                value_key(0.737500012f, Rest, StepThenHold),
                value_key(0.75f, Pushed, HoldThenStep),
                value_key(0.875f, Pulled, Cubic1),
                path_key(0.987499952f, Pulled, Point{20.094f, 24.0f}, Point{23.375f, 24.0f}, Rest, 0, StepThenHold),
                value_key(0.987500012f, Rest, StepThenHold),
            },
            p,
            Point{},
            t);
        const Matrix container = ShapeTransform{.offset = cap_offset}.to_matrix();

        // The round cap, scaled 2.25.
        painter.fill(round_cap(), Matrix{2.25f, 0.0f, 0.0f, 2.25f, 0.0f, 0.0f} * container);

        // The stem, its x and y scale animated.
        const float          stem_sx = animate<float>({{0.0f, 2.25f, StepThenHold},
                                                       {0.125f, 2.25f, HoldThenStep},
                                                       {0.237499997f, 1.62211001f, Cubic0},
                                                       {0.375f, 2.25f, HoldThenStep},
                                                       {0.487500012f, 1.62211001f, Cubic0},
                                                       {0.5f, 1.62211001f, HoldThenStep},
                                                       {0.625f, 2.26947999f, Cubic1},
                                                       {0.737500012f, 2.25f, Cubic2},
                                                       {0.75f, 1.62211001f, HoldThenStep},
                                                       {0.875f, 2.26947999f, Cubic1},
                                                       {0.987500012f, 2.25f, Cubic2}},
                                                      p,
                                                      1.0f);
        const float          stem_sy = animate<float>({{0.0f, 2.25f, StepThenHold},
                                                       {0.125f, 2.25f, HoldThenStep},
                                                       {0.237499997f, 2.25f, Cubic3},
                                                       {0.375f, 2.25f, HoldThenStep},
                                                       {0.487500012f, 2.25f, Cubic3},
                                                       {0.5f, 2.25f, HoldThenStep},
                                                       {0.625f, 2.25f, Cubic1},
                                                       {0.737500012f, 2.25f, Cubic2},
                                                       {0.75f, 2.25f, HoldThenStep},
                                                       {0.875f, 2.25f, Cubic1},
                                                       {0.987500012f, 2.25f, Cubic2}},
                                                      p,
                                                      1.0f);
        const ShapeTransform stem_transform{.center = {7.5f, -0.00300000003f},
                                            .scale  = {stem_sx, stem_sy},
                                            .offset = {9.375f, -0.00400000019f}};
        painter.fill(stem(), stem_transform.to_matrix() * container);

        // The head, scaled 2.25, its offset animated.
        const Point          head_offset = animate<Point>({{0.0f, Point{24.0f, 24.0f}, StepThenHold},
                                                           {0.125f, Point{24.0f, 24.0f}, HoldThenStep},
                                                           {0.237499997f, Point{33.0f, 24.0f}, Cubic0},
                                                           {0.375f, Point{24.0f, 24.0f}, HoldThenStep},
                                                           {0.487500012f, Point{33.0f, 24.0f}, Cubic0},
                                                           {0.5f, Point{33.0f, 24.0f}, HoldThenStep},
                                                           {0.625f, Point{19.9839993f, 24.0f}, Cubic4},
                                                           {0.737500012f, Point{24.0f, 24.0f}, Cubic2},
                                                           {0.75f, Point{33.0f, 24.0f}, HoldThenStep},
                                                           {0.875f, Point{19.9839993f, 24.0f}, Cubic4},
                                                           {0.987500012f, Point{24.0f, 24.0f}, Cubic2}},
                                                          p,
                                                          Point{});
        const ShapeTransform head_transform{.scale = {2.25f, 2.25f}, .offset = head_offset};
        painter.fill(head(), head_transform.to_matrix());

        if (opacity0 < 1.0f)
        {
            painter.pop_opacity();
        }
        painter.pop_clip();
    }

    // The pointer-over transitions, a static arrow: visibility and opacity animated, identity
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

        // The cap, stem and head again, at fixed transforms.
        painter.fill(round_cap(), Matrix{2.25f, 0.0f, 0.0f, 2.25f, 24.0f, 24.0f});
        painter.fill(stem(), Matrix{2.25f, 0.0f, 0.0f, 2.25f, 24.0f, 23.9997501f});
        painter.fill(head(), Matrix{2.25f, 0.0f, 0.0f, 2.25f, 24.0f, 24.0f});

        if (opacity1 < 1.0f)
        {
            painter.pop_opacity();
        }
        painter.pop_clip();
    }
}

} // namespace ui
