// SPDX-License-Identifier: MIT
// The AnimatedChevronUpDownSmallVisualSource animation (260 frames at 60 fps).
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

constexpr double DurationMs = 43333333.0 / 10000.0; // 43333333 ticks of 100 ns

// The cubic easings, in order of first use.
constexpr Easing Cubic0 = cubic(0.166999996f, 0.166999996f, 0.0f, 1.0f);
constexpr Easing Cubic1 = cubic(0.850000024f, 0.0f, 0.75f, 1.0f);
constexpr Easing Cubic2 = cubic(0.349999994f, 0.0f, 0.0f, 1.0f);
constexpr Easing Cubic3 = cubic(0.850000024f, 0.0f, 0.449999988f, 1.0f);

// The open chevron polyline, pointing down in its own coordinates. The sink sets no
// fill mode, so it is D2D's default (alternate); the shape is only stroked anyway.
const PathGeometry& chevron()
{
    static const PathGeometry g = PathBuilder(FillRule::EvenOdd)
                                      .begin(-3.54299998f, -1.79299998f)
                                      .line(0.0199999996f, 1.70700002f)
                                      .line(3.48900008f, -1.71500003f)
                                      .end(false)
                                      .build();
    return g;
}

} // namespace

Duration AnimatedChevronUpDownSmallVisualSource::duration() const
{
    return Duration{DurationMs};
}

const std::unordered_map<std::string, double>& AnimatedChevronUpDownSmallVisualSource::markers() const
{
    static const std::unordered_map<std::string, double> Markers{
        {"NormalOnToNormalOff_Start", 0.0},
        {"NormalOnToNormalOff_End", 0.111730769230769},
        {"NormalOnToPointerOverOn_Start", 0.115576923076923},
        {"NormalOnToPointerOverOn_End", 0.150192307692308},
        {"NormalOnToPressedOn_Start", 0.154038461538462},
        {"NormalOnToPressedOn_End", 0.188653846153846},
        {"NormalOffToNormalOn_Start", 0.1925},
        {"NormalOffToNormalOn_End", 0.304038461538462},
        {"NormalOffToPointerOverOff_Start", 0.307884615384615},
        {"NormalOffToPointerOverOff_End", 0.3425},
        {"NormalOffToPressedOff_Start", 0.346346153846154},
        {"NormalOffToPressedOff_End", 0.380961538461538},
        {"PointerOverOnToPointerOverOff_Start", 0.384807692307692},
        {"PointerOverOnToPointerOverOff_End", 0.419423076923077},
        {"PointerOverOnToNormalOn_Start", 0.423269230769231},
        {"PointerOverOnToNormalOn_End", 0.457884615384615},
        {"PointerOverOnToPressedOn_Start", 0.461730769230769},
        {"PointerOverOnToPressedOn_End", 0.496346153846154},
        {"PointerOverOffToPointerOverOn_Start", 0.500192307692308},
        {"PointerOverOffToPointerOverOn_End", 0.534807692307692},
        {"PointerOverOffToNormalOff_Start", 0.538653846153846},
        {"PointerOverOffToNormalOff_End", 0.573269230769231},
        {"PointerOverOffToPressedOff_Start", 0.577115384615385},
        {"PointerOverOffToPressedOff_End", 0.611730769230769},
        {"PressedOnToPressedOff_Start", 0.615576923076923},
        {"PressedOnToPressedOff_End", 0.650192307692308},
        {"PressedOnToPointerOverOff_Start", 0.654038461538462},
        {"PressedOnToPointerOverOff_End", 0.727115384615385},
        {"PressedOnToNormalOff_Start", 0.730961538461539},
        {"PressedOnToNormalOff_End", 0.804038461538462},
        {"PressedOffToPressedOn_Start", 0.807884615384615},
        {"PressedOffToPressedOn_End", 0.8425},
        {"PressedOffToPointerOverOn_Start", 0.846346153846154},
        {"PressedOffToPointerOverOn_End", 0.919423076923077},
        {"PressedOffToNormalOn_Start", 0.923269230769231},
        {"PressedOffToNormalOn_End", 0.996346153846154},
    };
    return Markers;
}

void AnimatedChevronUpDownSmallVisualSource::render_icon_frame(
    Renderer&    renderer,
    const Rect&  bounds,
    double       progress,
    const Color& foreground) const
{
    const float   p = static_cast<float>(progress);
    const Painter painter(renderer, bounds, natural_size(), foreground);

    // The one layer, clipped to its square. Its transform is the identity and ignored.
    painter.push_clip(Rect{0.0f, 0.0f, 48.0f, 48.0f});

    // The chevron: scaled 4, its rotation animated (180 = pointing up, 0 = pointing down) and
    // its offset animated.
    const float rotation = animate<
        float>({{0.0f, 180.0f, StepThenHold},         {0.057692308f, 180.0f, HoldThenStep},
                {0.0961538479f, 0.0f, Cubic0},        {0.115384616f, 180.0f, HoldThenStep},
                {0.15384616f, 180.0f, HoldThenStep},  {0.192307696f, 0.0f, HoldThenStep},
                {0.25f, 0.0f, HoldThenStep},          {0.288461536f, 180.0f, Cubic0},
                {0.307692319f, 0.0f, HoldThenStep},   {0.346153855f, 0.0f, HoldThenStep},
                {0.384615391f, 180.0f, HoldThenStep}, {0.419230759f, 0.0f, Cubic0},
                {0.423076928f, 180.0f, HoldThenStep}, {0.461538464f, 180.0f, HoldThenStep},
                {0.5f, 0.0f, HoldThenStep},           {0.534615397f, 180.0f, Cubic0},
                {0.538461566f, 0.0f, HoldThenStep},   {0.576923072f, 0.0f, HoldThenStep},
                {0.615384638f, 180.0f, HoldThenStep}, {0.649999976f, 0.0f, Cubic0},
                {0.653846145f, 180.0f, HoldThenStep}, {0.673076928f, 180.0f, HoldThenStep},
                {0.711538434f, 0.0f, Cubic0},         {0.730769217f, 180.0f, HoldThenStep},
                {0.75f, 180.0f, HoldThenStep},        {0.788461566f, 0.0f, Cubic0},
                {0.807692289f, 0.0f, HoldThenStep},   {0.842307687f, 180.0f, Cubic0},
                {0.846153855f, 0.0f, HoldThenStep},   {0.865384638f, 0.0f, HoldThenStep},
                {0.903846145f, 180.0f, Cubic0},       {0.923076928f, 0.0f, HoldThenStep},
                {0.942307711f, 0.0f, HoldThenStep},   {0.980769217f, 180.0f, Cubic0}},
               p,
               0.0f);
    constexpr Point Center{24.0f, 24.0f};
    constexpr Point Nudged{24.1709995f, 23.9230003f};
    const Point     offset = animate<
        Point>({{0.0f, Center, HoldThenStep},
                    {0.0384615399f, Point{24.0f, 26.0f}, Cubic0},
                    {0.057692308f, Point{24.0f, 18.0f}, Cubic1},
                    {0.111538462f, Nudged, Cubic2},
                    {0.115384616f, Center, HoldThenStep},
                    {0.15384616f, Center, HoldThenStep},
                    {0.188461542f, Point{24.0f, 26.0f}, Cubic0},
                    {0.192307696f, Center, HoldThenStep},
                    {0.230769232f, Point{24.0f, 22.0f}, Cubic0},
                    {0.25f, Point{24.0f, 30.0f}, Cubic3},
                    {0.303846151f, Center, Cubic2},
                    {0.346153855f, Center, HoldThenStep},
                    {0.380769223f, Point{24.0f, 22.0f}, Cubic0},
                    {0.384615391f, Center, HoldThenStep},
                    {0.461538464f, Center, HoldThenStep},
                    {0.496153831f, Point{24.0f, 26.0f}, Cubic0},
                    {0.5f, Center, HoldThenStep},
                    {0.576923072f, Center, HoldThenStep},
                    {0.61153847f, Point{24.0f, 22.0f}, Cubic0},
                    {0.615384638f, Point{24.0f, 26.0f}, HoldThenStep},
                    {0.649999976f, Point{24.0f, 22.0f}, Cubic0},
                    {0.653846145f, Point{24.0f, 26.0f}, HoldThenStep},
                    {0.673076928f, Point{24.0f, 18.0f}, Cubic1},
                    {0.726923048f, Nudged, Cubic2},
                    {0.730769217f, Point{24.0f, 26.0f}, HoldThenStep},
                    {0.75f, Point{24.0f, 18.0f}, Cubic1},
                    {0.80384618f, Nudged, Cubic2},
                    {0.807692289f, Point{24.0f, 22.0f}, HoldThenStep},
                    {0.842307687f, Point{24.0f, 26.0f}, Cubic0},
                    {0.846153855f, Point{24.0f, 22.0f}, HoldThenStep},
                    {0.865384638f, Point{24.0f, 30.0f}, Cubic3},
                    {0.919230759f, Center, Cubic2},
                    {0.923076928f, Point{24.0f, 22.0f}, HoldThenStep},
                    {0.942307711f, Point{24.0f, 30.0f}, Cubic3},
                    {0.996153831f, Center, Cubic2}},
               p,
               Point{});
    const ShapeTransform sprite{.scale = {4.0f, 4.0f}, .rotation = rotation, .offset = offset};

    // Stroked at the default thickness (1, so 4 px at scale 4), round caps (start, end and
    // dash), a round join and a miter limit of 2.
    painter.stroke(chevron(), sprite.to_matrix(), 1.0f, PenLineCap::Round, PenLineJoin::Round, 2.0f);

    painter.pop_clip();
}

} // namespace ui
