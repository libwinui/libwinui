// SPDX-License-Identifier: MIT
// The AnimatedChevronRightDownSmallVisualSource animation (220 frames at 60 fps).
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

constexpr double DurationMs = 36666666.0 / 10000.0; // 36666666 ticks of 100 ns

// The cubic easings, in order of first use.
constexpr Easing Cubic0 = cubic(0.166999996f, 0.166999996f, 0.0f, 1.0f);
constexpr Easing Cubic1 = cubic(0.850000024f, 0.0f, 0.75f, 1.0f);
constexpr Easing Cubic2 = cubic(0.349999994f, 0.0f, 0.0f, 1.0f);

// The filled chevron, pointing down in its own coordinates.
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

Duration AnimatedChevronRightDownSmallVisualSource::duration() const
{
    return Duration{DurationMs};
}

const std::unordered_map<std::string, double>& AnimatedChevronRightDownSmallVisualSource::markers() const
{
    static const std::unordered_map<std::string, double> Markers{
        {"NormalOnToNormalOff_Start", 0.0},
        {"NormalOnToNormalOff_End", 0.0411363636363636},
        {"NormalOnToPointerOverOn_Start", 0.0456818181818182},
        {"NormalOnToPointerOverOn_End", 0.0865909090909091},
        {"NormalOnToPressedOn_Start", 0.0911363636363636},
        {"NormalOnToPressedOn_End", 0.132045454545455},
        {"NormalOffToNormalOn_Start", 0.136590909090909},
        {"NormalOffToNormalOn_End", 0.268409090909091},
        {"NormalOffToPointerOverOff_Start", 0.272954545454545},
        {"NormalOffToPointerOverOff_End", 0.313863636363636},
        {"NormalOffToPressedOff_Start", 0.318409090909091},
        {"NormalOffToPressedOff_End", 0.359318181818182},
        {"PointerOverOnToPointerOverOff_Start", 0.363863636363636},
        {"PointerOverOnToPointerOverOff_End", 0.404772727272727},
        {"PointerOverOnToNormalOn_Start", 0.409318181818182},
        {"PointerOverOnToNormalOn_End", 0.450227272727273},
        {"PointerOverOnToPressedOn_Start", 0.454772727272727},
        {"PointerOverOnToPressedOn_End", 0.495681818181818},
        {"PointerOverOffToPointerOverOn_Start", 0.500227272727273},
        {"PointerOverOffToPointerOverOn_End", 0.541136363636364},
        {"PointerOverOffToNormalOff_Start", 0.545681818181818},
        {"PointerOverOffToNormalOff_End", 0.586590909090909},
        {"PointerOverOffToPressedOff_Start", 0.591136363636364},
        {"PointerOverOffToPressedOff_End", 0.632045454545455},
        {"PressedOnToPressedOff_Start", 0.636590909090909},
        {"PressedOnToPressedOff_End", 0.6775},
        {"PressedOnToPointerOverOff_Start", 0.682045454545455},
        {"PressedOnToPointerOverOff_End", 0.722954545454546},
        {"PressedOnToNormalOff_Start", 0.7275},
        {"PressedOnToNormalOff_End", 0.768409090909091},
        {"PressedOffToPressedOn_Start", 0.772954545454546},
        {"PressedOffToPressedOn_End", 0.813863636363636},
        {"PressedOffToPointerOverOn_Start", 0.818409090909091},
        {"PressedOffToPointerOverOn_End", 0.904772727272727},
        {"PressedOffToNormalOn_Start", 0.909318181818182},
        {"PressedOffToNormalOn_End", 0.995681818181818},
    };
    return Markers;
}

void AnimatedChevronRightDownSmallVisualSource::render_icon_frame(
    Renderer&    renderer,
    const Rect&  bounds,
    double       progress,
    const Color& foreground) const
{
    const float   p = static_cast<float>(progress);
    const Painter painter(renderer, bounds, natural_size(), foreground);

    // The one layer, clipped to its square. Its transform is the identity and ignored.
    painter.push_clip(Rect{0.0f, 0.0f, 48.0f, 48.0f});

    // The shape group: scaled 4, its rotation animated (90 = pointing down, 0 = pointing right)
    // and its offset animated.
    const float rotation = animate<
        float>({{0.0f, 90.0f, HoldThenStep},          {0.0409090891f, 0.0f, Cubic0},
                {0.0454545468f, 90.0f, HoldThenStep}, {0.0909090936f, 90.0f, HoldThenStep},
                {0.131818175f, 0.0f, Cubic0},         {0.13636364f, 0.0f, HoldThenStep},
                {0.181818187f, 90.0f, Cubic0},        {0.272727281f, 0.0f, HoldThenStep},
                {0.318181813f, 0.0f, HoldThenStep},   {0.359090894f, 90.0f, Cubic0},
                {0.363636374f, 90.0f, HoldThenStep},  {0.404545456f, 0.0f, Cubic0},
                {0.409090906f, 90.0f, HoldThenStep},  {0.454545468f, 90.0f, HoldThenStep},
                {0.49545455f, 0.0f, Cubic0},          {0.5f, 0.0f, HoldThenStep},
                {0.540909111f, 90.0f, Cubic0},        {0.545454562f, 0.0f, HoldThenStep},
                {0.590909064f, 0.0f, HoldThenStep},   {0.631818175f, 90.0f, Cubic0},
                {0.636363626f, 0.0f, HoldThenStep},   {0.677272737f, 90.0f, Cubic0},
                {0.681818187f, 0.0f, HoldThenStep},   {0.727272749f, 0.0f, HoldThenStep},
                {0.772727251f, 90.0f, HoldThenStep},  {0.813636363f, 0.0f, Cubic0},
                {0.818181813f, 90.0f, HoldThenStep}},
               p,
               0.0f);
    constexpr Point Center{24.0f, 24.0f};
    constexpr Point Nudged{24.1709995f, 24.0f};
    constexpr Point Down{24.0f, 30.0f};
    const Point offset = animate<Point>({{0.0f, Center, HoldThenStep},         {0.0454545468f, Nudged, HoldThenStep},
                                         {0.13636364f, Center, HoldThenStep},  {0.181818187f, Center, HoldThenStep},
                                         {0.204545453f, Down, Cubic1},         {0.268181831f, Center, Cubic2},
                                         {0.272727281f, Nudged, HoldThenStep}, {0.318181813f, Nudged, HoldThenStep},
                                         {0.363636374f, Center, HoldThenStep}, {0.409090906f, Nudged, HoldThenStep},
                                         {0.454545468f, Nudged, HoldThenStep}, {0.5f, Center, HoldThenStep},
                                         {0.545454562f, Nudged, HoldThenStep}, {0.590909064f, Nudged, HoldThenStep},
                                         {0.636363626f, Center, HoldThenStep}, {0.818181813f, Center, HoldThenStep},
                                         {0.840909064f, Down, Cubic1},         {0.904545426f, Center, Cubic2},
                                         {0.909090936f, Center, HoldThenStep}, {0.931818187f, Down, Cubic1},
                                         {0.99545455f, Center, Cubic2}},
                                        p,
                                        Point{});
    const ShapeTransform container{.scale = {4.0f, 4.0f}, .rotation = rotation, .offset = offset};

    // The chevron, rotated -90 degrees, filled.
    const ShapeTransform sprite{.matrix = Matrix{0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f}};
    painter.fill(chevron(), sprite.to_matrix() * container.to_matrix());

    painter.pop_clip();
}

} // namespace ui
