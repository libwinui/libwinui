// SPDX-License-Identifier: MIT
// The AnimatedChevronDownSmallVisualSource animation (29 frames at 60 fps).
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

constexpr double DurationMs = 4833333.0 / 10000.0; // 4833333 ticks of 100 ns

// The cubic easings of the two offset animations, numbered in order of first use.
constexpr Easing Cubic0 = cubic(0.166999996f, 0.166999996f, 0.0f, 1.0f);
constexpr Easing Cubic1 = cubic(0.550000012f, 0.0f, 0.75f, 1.0f);
constexpr Easing Cubic2 = cubic(0.349999994f, 0.0f, 0.0f, 1.0f);
constexpr Easing Cubic3 = cubic(0.166999996f, 0.166999996f, 0.833000004f, 0.833000004f);
constexpr Easing Cubic4 = cubic(0.166999996f, 0.166999996f, 0.649999976f, 1.0f);

// The filled chevron, shared by both shapes.
const PathGeometry& chevron()
{
    static const PathGeometry
        g = PathBuilder(FillRule::Nonzero)
                .begin(-3.85400009f, -2.10400009f)
                .bezier(-3.65899992f, -2.29900002f, -3.34100008f, -2.29900002f, -3.14599991f, -2.10400009f)
                .line(0.0f, 1.04299998f)
                .line(3.14599991f, -2.10400009f)
                .bezier(3.34100008f, -2.29900002f, 3.65899992f, -2.29900002f, 3.85400009f, -2.10400009f)
                .bezier(4.04899979f, -1.90900004f, 4.04899979f, -1.59099996f, 3.85400009f, -1.39600003f)
                .line(0.354000002f, 2.10400009f)
                .bezier(0.158999994f, 2.29900002f, -0.158999994f, 2.29900002f, -0.354000002f, 2.10400009f)
                .line(-3.85400009f, -1.39600003f)
                .bezier(-4.04899979f, -1.59099996f, -4.04899979f, -1.90900004f, -3.85400009f, -2.10400009f)
                .end(true)
                .build();
    return g;
}

} // namespace

Duration AnimatedChevronDownSmallVisualSource::duration() const
{
    return Duration{DurationMs};
}

const std::unordered_map<std::string, double>& AnimatedChevronDownSmallVisualSource::markers() const
{
    static const std::unordered_map<std::string, double> Markers{
        {"NormalToPointerOver_Start", 0.0},
        {"NormalToPointerOver_End", 0.0},
        {"NormalToPressed_Start", 0.0},
        {"NormalToPressed_End", 0.312068965517241},
        {"PointerOverToNormal_Start", 0.0},
        {"PointerOverToNormal_End", 0.0},
        {"PointerOverToPressed_Start", 0.0},
        {"PointerOverToPressed_End", 0.312068965517241},
        {"PressedToNormal_Start", 0.346551724137931},
        {"PressedToNormal_End", 0.967241379310345},
        {"PressedToPointerOver_Start", 0.346551724137931},
        {"PressedToPointerOver_End", 0.967241379310345},
    };
    return Markers;
}

void AnimatedChevronDownSmallVisualSource::render_icon_frame(
    Renderer&    renderer,
    const Rect&  bounds,
    double       progress,
    const Color& foreground) const
{
    const float   p = static_cast<float>(progress);
    const Painter painter(renderer, bounds, natural_size(), foreground);

    // The PressedToNormal layer: hidden at first, shown from frame 10. Its transform is the
    // identity and ignored.
    if (animate_bool({{0.344827592f, true}}, p, false))
    {
        painter.push_clip(Rect{0.0f, 0.0f, 48.0f, 48.0f}); // the layer's clip

        // The chevron, scaled 4, its offset animated.
        const Point          offset = animate<Point>({{0.0f, Point{24.0f, 24.0f}, HoldThenStep},
                                                      {0.344827592f, Point{24.0f, 31.5f}, Cubic0},
                                                      {0.517241359f, Point{24.0f, 21.0f}, Cubic1},
                                                      {1.0f, Point{24.0f, 24.0f}, Cubic2}},
                                                     p,
                                                     Point{});
        const ShapeTransform shape{.scale = {4.0f, 4.0f}, .offset = offset};
        painter.fill(chevron(), shape.to_matrix());

        painter.pop_clip();
    }

    // The PointerOverToPressed layer: visible until frame 10.
    if (animate_bool({{0.344827592f, false}}, p, true))
    {
        painter.push_clip(Rect{0.0f, 0.0f, 48.0f, 48.0f}); // the layer's clip

        // The chevron, scaled 4, its offset animated.
        const Point offset = animate<Point>({{0.0f, Point{24.0f, 24.0f}, Cubic3}, {0.310344815f, Point{24.0f, 31.5f}, Cubic4}},
                                            p,
                                            Point{});
        const ShapeTransform shape{.scale = {4.0f, 4.0f}, .offset = offset};
        painter.fill(chevron(), shape.to_matrix());

        painter.pop_clip();
    }
}

} // namespace ui
