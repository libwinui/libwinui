// SPDX-License-Identifier: MIT
// The AnimatedSettingsVisualSource animation (120 frames at 60 fps).
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

constexpr double DurationMs = 20000000.0 / 10000.0; // 20000000 ticks of 100 ns

// The cubic easings, in order of first use.
constexpr Easing Cubic0 = cubic(0.166999996f, 0.166999996f, 1.0f, 1.0f);
constexpr Easing Cubic1 = cubic(0.0f, 0.0f, 0.0f, 1.0f);

// The gear's outer outline.
PathGeometry gear_outer()
{
    return PathBuilder(FillRule::Nonzero)
        .begin(48.8800011f, 30.9810009f)
        .bezier(49.1990013f, 29.9960003f, 49.7229996f, 29.0919991f, 50.4160004f, 28.3250008f)
        .bezier(50.5330009f, 28.1949997f, 50.7169991f, 28.1490002f, 50.8810005f, 28.2080002f)
        .line(52.5359993f, 28.7989998f)
        .bezier(52.9850006f, 28.9589996f, 53.4790001f, 28.7259998f, 53.6389999f, 28.2770004f)
        .bezier(53.6549988f, 28.2329998f, 53.6669998f, 28.1879997f, 53.6749992f, 28.1420002f)
        .line(53.9900017f, 26.4109993f)
        .bezier(54.0209999f, 26.2390003f, 54.1539993f, 26.1019993f, 54.3250008f, 26.066f)
        .bezier(54.8260002f, 25.9589996f, 55.3390007f, 25.9060001f, 55.8590012f, 25.9060001f)
        .bezier(56.3790016f, 25.9060001f, 56.8919983f, 25.9599991f, 57.3919983f, 26.066f)
        .bezier(57.5629997f, 26.1019993f, 57.6949997f, 26.2390003f, 57.7260017f, 26.4109993f)
        .line(58.0419998f, 28.1420002f)
        .bezier(58.1279984f, 28.6110001f, 58.5769997f, 28.9209995f, 59.0460014f, 28.8360004f)
        .bezier(59.0919991f, 28.8279991f, 59.137001f, 28.8150005f, 59.1809998f, 28.7989998f)
        .line(60.8359985f, 28.2080002f)
        .bezier(61.0f, 28.1490002f, 61.1850014f, 28.1949997f, 61.3019981f, 28.3250008f)
        .bezier(61.9949989f, 29.0919991f, 62.519001f, 29.9960003f, 62.8380013f, 30.9810009f)
        .bezier(62.8919983f, 31.1469994f, 62.8390007f, 31.3299999f, 62.7060013f, 31.4430008f)
        .line(61.3650017f, 32.5810013f)
        .bezier(61.0019989f, 32.8889999f, 60.9580002f, 33.4350014f, 61.2659988f, 33.7980003f)
        .bezier(61.2960014f, 33.8339996f, 61.3289986f, 33.8670006f, 61.3650017f, 33.8969994f)
        .line(62.7060013f, 35.0359993f)
        .bezier(62.8390007f, 35.1489983f, 62.8919983f, 35.3310013f, 62.8380013f, 35.4970016f)
        .bezier(62.519001f, 36.4819984f, 61.9949989f, 37.387001f, 61.3019981f, 38.1539993f)
        .bezier(61.1850014f, 38.2840004f, 61.0f, 38.3300018f, 60.8359985f, 38.2709999f)
        .line(59.1809998f, 37.6790009f)
        .bezier(58.7319984f, 37.519001f, 58.2389984f, 37.7519989f, 58.0789986f, 38.2010002f)
        .bezier(58.0629997f, 38.2449989f, 58.0499992f, 38.2910004f, 58.0419998f, 38.3370018f)
        .line(57.7260017f, 40.0680008f)
        .bezier(57.6949997f, 40.2400017f, 57.5629997f, 40.3759995f, 57.3919983f, 40.4119987f)
        .bezier(56.8919983f, 40.5180016f, 56.3790016f, 40.5730019f, 55.8590012f, 40.5730019f)
        .bezier(55.3390007f, 40.5730019f, 54.8260002f, 40.519001f, 54.3250008f, 40.4119987f)
        .bezier(54.1539993f, 40.3759995f, 54.0209999f, 40.2389984f, 53.9900017f, 40.0670013f)
        .line(53.6749992f, 38.3370018f)
        .bezier(53.5900002f, 37.868f, 53.1409988f, 37.5569992f, 52.6720009f, 37.6430016f)
        .bezier(52.6259995f, 37.651001f, 52.5800018f, 37.6629982f, 52.5359993f, 37.6790009f)
        .line(50.8810005f, 38.2709999f)
        .bezier(50.7169991f, 38.3300018f, 50.5330009f, 38.2840004f, 50.4160004f, 38.1539993f)
        .bezier(49.7229996f, 37.387001f, 49.1990013f, 36.4819984f, 48.8800011f, 35.4970016f)
        .bezier(48.8260002f, 35.3310013f, 48.8779984f, 35.1489983f, 49.0110016f, 35.0359993f)
        .line(50.3520012f, 33.8969994f)
        .bezier(50.7150002f, 33.5890007f, 50.7599983f, 33.0439987f, 50.4519997f, 32.6809998f)
        .bezier(50.4220009f, 32.6450005f, 50.3880005f, 32.6110001f, 50.3520012f, 32.5810013f)
        .line(49.0110016f, 31.4430008f)
        .bezier(48.8779984f, 31.3299999f, 48.8260002f, 31.1469994f, 48.8800011f, 30.9810009f)
        .end(true)
        .build();
}

// The gear's inner outline.
PathGeometry gear_inner()
{
    return PathBuilder(FillRule::Nonzero)
        .begin(49.7949982f, 30.9759998f)
        .line(50.9109993f, 31.9239998f)
        .bezier(50.9819984f, 31.9850006f, 51.0480003f, 32.0509987f, 51.1090012f, 32.1220016f)
        .bezier(51.7260017f, 32.8479996f, 51.637001f, 33.9370003f, 50.9109993f, 34.5540009f)
        .line(49.7949982f, 35.5019989f)
        .bezier(50.0470009f, 36.1769981f, 50.4099998f, 36.8040009f, 50.8689995f, 37.3590012f)
        .line(52.2459984f, 36.8670006f)
        .bezier(52.3339996f, 36.8349991f, 52.4249992f, 36.8110008f, 52.5169983f, 36.7939987f)
        .bezier(53.4550018f, 36.6230011f, 54.3530006f, 37.2439995f, 54.5239983f, 38.1819992f)
        .line(54.7859993f, 39.6209984f)
        .bezier(55.1380005f, 39.6800003f, 55.4970016f, 39.7099991f, 55.8590012f, 39.7099991f)
        .bezier(56.2210007f, 39.7099991f, 56.5789986f, 39.6800003f, 56.9309998f, 39.6209984f)
        .line(57.1940002f, 38.1819992f)
        .bezier(57.2109985f, 38.0900002f, 57.2340012f, 37.9990005f, 57.2659988f, 37.9109993f)
        .bezier(57.5870018f, 37.0139999f, 58.5740013f, 36.5460014f, 59.4720001f, 36.8670006f)
        .line(60.848999f, 37.3590012f)
        .bezier(61.3069992f, 36.8040009f, 61.6710014f, 36.1769981f, 61.9230003f, 35.5019989f)
        .line(60.8069992f, 34.5540009f)
        .bezier(60.7360001f, 34.493f, 60.6689987f, 34.4269981f, 60.6080017f, 34.355999f)
        .bezier(59.9910011f, 33.6300011f, 60.0810013f, 32.5410004f, 60.8069992f, 31.9239998f)
        .line(61.9230003f, 30.9759998f)
        .bezier(61.6710014f, 30.3010006f, 61.3069992f, 29.6739998f, 60.848999f, 29.1189995f)
        .line(59.4720001f, 29.6119995f)
        .bezier(59.3839989f, 29.6439991f, 59.2929993f, 29.6669998f, 59.2010002f, 29.684f)
        .bezier(58.2639999f, 29.8549995f, 57.3650017f, 29.2339993f, 57.1940002f, 28.2970009f)
        .line(56.9309998f, 26.8570004f)
        .bezier(56.5789986f, 26.7980003f, 56.2210007f, 26.7679996f, 55.8590012f, 26.7679996f)
        .bezier(55.4970016f, 26.7679996f, 55.1380005f, 26.7980003f, 54.7859993f, 26.8570004f)
        .line(54.5239983f, 28.2959995f)
        .bezier(54.507f, 28.3880005f, 54.4830017f, 28.4799995f, 54.4510002f, 28.5680008f)
        .bezier(54.1300011f, 29.4650002f, 53.1430016f, 29.9330006f, 52.2459984f, 29.6119995f)
        .line(50.8689995f, 29.1189995f)
        .bezier(50.4099998f, 29.6739998f, 50.0470009f, 30.3010006f, 49.7949982f, 30.9759998f)
        .end(true)
        .build();
}

// The hub's outer circle.
PathGeometry hub_outer()
{
    return PathBuilder(FillRule::Nonzero)
        .begin(53.7019997f, 33.2389984f)
        .bezier(53.7019997f, 32.0480003f, 54.6679993f, 31.0820007f, 55.8590012f, 31.0820007f)
        .bezier(57.0499992f, 31.0820007f, 58.0159988f, 32.0480003f, 58.0159988f, 33.2389984f)
        .bezier(58.0159988f, 34.4300003f, 57.0499992f, 35.3959999f, 55.8590012f, 35.3959999f)
        .bezier(54.6679993f, 35.3959999f, 53.7019997f, 34.4300003f, 53.7019997f, 33.2389984f)
        .end(true)
        .build();
}

// The hub's inner circle.
PathGeometry hub_inner()
{
    return PathBuilder(FillRule::Nonzero)
        .begin(54.5649986f, 33.2389984f)
        .bezier(54.5649986f, 33.9539986f, 55.144001f, 34.5330009f, 55.8590012f, 34.5330009f)
        .bezier(56.5740013f, 34.5330009f, 57.1529999f, 33.9539986f, 57.1529999f, 33.2389984f)
        .bezier(57.1529999f, 32.5239983f, 56.5740013f, 31.9449997f, 55.8590012f, 31.9449997f)
        .bezier(55.144001f, 31.9449997f, 54.5649986f, 32.5239983f, 54.5649986f, 33.2389984f)
        .end(true)
        .build();
}

// The four paths as one D2D1_FILL_MODE_WINDING geometry group, shared by all three layers.
const PathGeometry& gear()
{
    static const PathGeometry g = []
    {
        const PathGeometry members[4] = {gear_outer(), gear_inner(), hub_outer(), hub_inner()};
        return PathGeometry::group(members, FillRule::Nonzero);
    }();
    return g;
}

// The gear's fixed transform in every layer, at the animated rotation.
Matrix gear_matrix(float rotation)
{
    const ShapeTransform t{.center   = {55.8590012f, 33.2389984f},
                           .scale    = {3.0f, 3.0f},
                           .rotation = rotation,
                           .offset   = {-31.8590012f, -9.23899841f}};
    return t.to_matrix();
}

} // namespace

Duration AnimatedSettingsVisualSource::duration() const
{
    return Duration{DurationMs};
}

const std::unordered_map<std::string, double>& AnimatedSettingsVisualSource::markers() const
{
    static const std::unordered_map<std::string, double> Markers{
        {"NormalToPointerOver_Start", 0.0},
        {"NormalToPointerOver_End", 0.0754166666666667},
        {"NormalToPressed_Start", 0.08375},
        {"NormalToPressed_End", 0.15875},
        {"PointerOverToNormal_Start", 0.167083333333333},
        {"PointerOverToNormal_End", 0.242083333333333},
        {"PointerOverToPressed_Start", 0.250416666666667},
        {"PointerOverToPressed_End", 0.325416666666667},
        {"PressedToNormal_Start", 0.33375},
        {"PressedToNormal_End", 0.65875},
        {"PressedToPointerOver_Start", 0.667083333333333},
        {"PressedToPointerOver_End", 0.992083333333333},
    };
    return Markers;
}

void AnimatedSettingsVisualSource::render_icon_frame(Renderer& renderer, const Rect& bounds, double progress, const Color& foreground) const
{
    const float   p = static_cast<float>(progress);
    const Painter painter(renderer, bounds, natural_size(), foreground);

    // Three layers drawn in this order, each 48x48, clipped to its square with an identity
    // transform, holding the one gear.

    // The PressedToPointerOver layer, hidden at first.
    if (animate_bool({{0.666666687f, true}}, p, false))
    {
        const float rotation = animate<float>({{0.0f, 0.0f, StepThenHold},
                                               {0.583333313f, 0.0f, HoldThenStep},
                                               {0.666666687f, -20.0f, Cubic0},
                                               {0.991666675f, 360.0f, Cubic1}},
                                              p,
                                              0.0f); // 0 to 360 degrees
        painter.push_clip(Rect{0.0f, 0.0f, 48.0f, 48.0f});
        painter.fill(gear(), gear_matrix(rotation));
        painter.pop_clip();
    }

    // The PressedToNormal layer, hidden at first.
    if (animate_bool({{0.333333343f, true}, {0.666666687f, false}}, p, false))
    {
        const float rotation = animate<float>({{0.0f, 0.0f, StepThenHold},
                                               {0.25f, 0.0f, HoldThenStep},
                                               {0.333333343f, -20.0f, Cubic0},
                                               {0.658333361f, 360.0f, Cubic1}},
                                              p,
                                              0.0f); // 0 to 360 degrees
        painter.push_clip(Rect{0.0f, 0.0f, 48.0f, 48.0f});
        painter.fill(gear(), gear_matrix(rotation));
        painter.pop_clip();
    }

    // The layer for NormalToPointerOver, NormalToPressed, PointerOverToNormal and
    // PointerOverToPressed.
    if (animate_bool({{0.333333343f, false}}, p, true))
    {
        const float rotation = animate<float>({{0.0f, 0.0f, HoldThenStep},
                                               {0.0833333358f, 0.0f, HoldThenStep},
                                               {0.158333331f, -20.0f, Cubic0},
                                               {0.166666672f, 0.0f, HoldThenStep},
                                               {0.25f, 0.0f, HoldThenStep},
                                               {0.324999988f, -20.0f, Cubic0}},
                                              p,
                                              0.0f); // 0 to -20 degrees
        painter.push_clip(Rect{0.0f, 0.0f, 48.0f, 48.0f});
        painter.fill(gear(), gear_matrix(rotation));
        painter.pop_clip();
    }
}

} // namespace ui
