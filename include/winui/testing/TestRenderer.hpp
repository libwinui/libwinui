// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/SvgImage.hpp"
#include "winui/render/Text.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <cstddef>

namespace ui
{
class Window;
}

namespace ui::testing
{

/// Renderer test double that records every draw call as a RenderCommand instead of painting.
///
/// This is the headless testing boundary: any Element can render() into it without a native
/// window, a GPU or a desktop session, and the resulting command stream is deterministic. Each override
/// appends exactly one command of the matching alternative; images and SVGs are recorded by
/// their natural size only, never their pixels. render/Describe.hpp turns the stream into the
/// text used by the semantic golden files.
///
/// Example:
/// @code
/// ui::ButtonControl       button{"Hello"};
/// ui::testing::TestRenderer renderer;
/// button.render(renderer); // Theme::default_light() by default
///
/// REQUIRE(renderer.count() == 3); // background, elevation border, label
/// REQUIRE(std::holds_alternative<ui::FillRoundedRect>(renderer.commands[0]));
/// REQUIRE(renderer.count_of<ui::DrawTextRun>() == 1);
/// REQUIRE(std::get<ui::DrawTextRun>(renderer.commands[2]).text == "Hello");
///
/// renderer.clear(); // reuse for the next frame
/// @endcode
class TestRenderer final : public Renderer
{
public:
    // Out of line, with every recording member: the command vector's members are compiled once in
    // TestRenderer.cpp, not in each test that includes this header.
    TestRenderer();
    TestRenderer(const TestRenderer& other);
    TestRenderer(TestRenderer&& other) noexcept;
    TestRenderer& operator=(const TestRenderer& other);
    TestRenderer& operator=(TestRenderer&& other) noexcept;
    ~TestRenderer() override;

    /// Every recorded command, in draw order. Public so tests can index and `std::get` directly.
    std::vector<RenderCommand> commands;

    /// Records a FillRoundedRect.
    void fill_rounded_rect(const Rect& bounds, float radius, const Color& color) override;

    /// Records a StrokeRoundedRect.
    void stroke_rounded_rect(const Rect& bounds, float radius, const Color& color, float stroke_width) override;

    /// Records a DrawTextRun, copying `text` into an owned string.
    void draw_text(std::string_view text, const Rect& bounds, const Color& color, const TextStyle& style) override;

    /// Records a FillRoundedRectGradient.
    void fill_rounded_rect_gradient(const Rect& bounds, float radius, const LinearGradientBrush& brush) override;

    /// Records a StrokeRoundedRectGradient.
    void stroke_rounded_rect_gradient(const Rect&                bounds,
                                      float                      radius,
                                      const LinearGradientBrush& brush,
                                      float                      extent_px,
                                      float                      stroke_width) override;

    /// Records a StrokeLine.
    void stroke_line(Point from, Point to, const Color& color, float stroke_width) override;

    /// Records a StrokeWavyLine (the spelling squiggle).
    void stroke_wavy_line(Point from, Point to, const Color& color, float stroke_width, float amplitude, float wavelength) override;

    /// Records the FillPolygon as-is.
    void fill_polygon(const FillPolygon& polygon) override;
    /// Records the StrokePolyline as-is.
    void stroke_polyline(const StrokePolyline& polyline) override;

    /// Records the DrawGeometry as-is.
    void draw_geometry(const DrawGeometry& geometry) override;

    /// Records a PushClip.
    void push_clip(const Rect& bounds) override;

    /// Records a PopClip.
    void pop_clip() override;

    /// Records a PushClipEllipse.
    void push_clip_ellipse(Point center, float radius_x, float radius_y) override;

    /// Records a PushClipRoundedRect.
    void push_clip_rounded_rect(const Rect& bounds, float radius) override;

    /// Records a PushOpacityLayer.
    void push_opacity_layer(float opacity) override;

    /// Records a PopOpacityLayer.
    void pop_opacity_layer() override;

    /// Records a PushTransform decomposed as (m11, offset {dx, dy}, m22, m12, m21).
    void push_matrix(const Matrix& m) override;

    /// Records a PopTransform.
    void pop_transform() override;

    /// Records the SetRasterization policy as-is.
    void set_rasterization(const SetRasterization& policy) override;

    /// What device_scale() reports, 1 by default: set it to test a mark drawn per device pixel at
    /// another DPI (1.5 for 144 DPI).
    float scale = 1.0f;
    /// Returns `scale`.
    [[nodiscard]] float device_scale() const override
    {
        return scale;
    }

    /// Records a FillEllipse.
    void fill_ellipse(Point center, float radius_x, float radius_y, const Color& color) override;

    /// Records a StrokeEllipse.
    void stroke_ellipse(Point center, float radius_x, float radius_y, const Color& color, float stroke_width) override;

    /// Records a FillRectPattern.
    void fill_rect_pattern(const Rect& bounds, Pattern pattern, const Color& fg, const Color& bg) override;

    /// Records a DrawImageNineSlice by image name.
    void draw_image_nine_slice(const std::string& image_name, const Rect& bounds, const Thickness& slice_margins) override;

    /// Records a DrawImage carrying the image's pixel size, not its pixels.
    void draw_image(const Bitmap& image, const Rect& bounds, Stretch stretch) override;

    /// Records a DrawImageRect carrying the image's pixel size and source rect.
    void draw_image_rect(const Bitmap& image, const Rect& source, const Rect& bounds) override;

    /// Records a DrawSvg carrying the document's natural size.
    void draw_svg(const SvgImage& svg, const Rect& bounds) override;

    /// Drops every recorded command, e.g. between two render() passes.
    void clear();
    /// Number of recorded commands (`commands.size()`).
    [[nodiscard]] std::size_t count() const
    {
        return commands.size();
    }

    /// How many recorded commands hold alternative `T` (e.g. `count_of<DrawTextRun>()`).
    template <typename T>
    [[nodiscard]] std::size_t count_of() const
    {
        std::size_t n = 0;
        for (const auto& c : commands)
        {
            if (std::holds_alternative<T>(c))
            {
                ++n;
            }
        }
        return n;
    }
};

/// Renders `window` through a TestRenderer and prints the result with ui::describe(): the semantic
/// golden of a whole window. Lay the window out first; the dump starts with the theme's
/// SetRasterization line.
///
/// Example:
/// @code
/// // What the window draws right now, as text, to diff against an earlier frame.
/// const std::string frame = ui::testing::describe(window);
/// std::println("{}", frame);
/// @endcode
[[nodiscard]] std::string describe(const Window& window);

} // namespace ui::testing
