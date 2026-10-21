// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
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

/// A Renderer that keeps every call, with its full arguments, so it can be replayed into any
/// other Renderer later, any number of times: a resolution-independent snapshot of an element.
///
/// Unlike TestRenderer, which records commands as data for tests (without a bitmap's pixels or
/// an SVG's markup), this copies whatever each call needs to be drawn again. It is what a
/// ConnectedAnimation shows of its source after the source itself is gone.
///
/// Example:
/// @code
/// ui::Control<ui::Border> card; // filled through the card's `.control`
/// const float             fade = 0.6f;
///
/// // Take a snapshot of a card before it leaves the tree.
/// ui::RenderRecording snapshot;
/// card->render(snapshot);
///
/// // Later, in a custom control's render_override(), once per frame while the snapshot flies to
/// // its destination. The same recording replays as often as needed.
/// if (!snapshot.empty())
/// {
///     renderer.push_opacity_layer(fade);
///     renderer.push_transform(0.8f, ui::Point{40, 20});
///     snapshot.replay(renderer);
///     renderer.pop_transform();
///     renderer.pop_opacity_layer();
/// }
/// @endcode
class RenderRecording final : public Renderer
{
public:
    /// Draws everything recorded into `target`, in order.
    void replay(Renderer& target) const;
    /// How many calls were recorded.
    [[nodiscard]] std::size_t size() const
    {
        return calls_.size();
    }
    /// True when nothing has been recorded (or since clear()).
    [[nodiscard]] bool empty() const
    {
        return calls_.empty();
    }
    /// Drops every recorded call, for example to take a new snapshot of the same element.
    void clear()
    {
        calls_.clear();
    }

    // ---- Renderer ----
    /// @name Renderer
    /// Each call is appended to the recording (arguments copied, pixels and markup included);
    /// nothing is drawn until replay().
    ///@{
    void fill_rounded_rect(const Rect& bounds, float radius, const Color& color) override;
    void stroke_rounded_rect(const Rect& bounds, float radius, const Color& color, float stroke_width) override;
    void draw_text(std::string_view text, const Rect& bounds, const Color& color, const TextStyle& style) override;
    void fill_rounded_rect_gradient(const Rect& bounds, float radius, const LinearGradientBrush& brush) override;
    void stroke_rounded_rect_gradient(const Rect&                bounds,
                                      float                      radius,
                                      const LinearGradientBrush& brush,
                                      float                      extent_px,
                                      float                      stroke_width) override;
    void stroke_line(Point from, Point to, const Color& color, float stroke_width) override;
    void stroke_wavy_line(Point from, Point to, const Color& color, float stroke_width, float amplitude, float wavelength) override;
    void fill_polygon(const FillPolygon& polygon) override;
    void stroke_polyline(const StrokePolyline& polyline) override;
    void draw_geometry(const DrawGeometry& geometry) override;
    void push_clip(const Rect& bounds) override;
    void pop_clip() override;
    void push_clip_ellipse(Point center, float radius_x, float radius_y) override;
    void push_clip_rounded_rect(const Rect& bounds, float radius) override;
    void fill_ellipse(Point center, float radius_x, float radius_y, const Color& color) override;
    void stroke_ellipse(Point center, float radius_x, float radius_y, const Color& color, float stroke_width) override;
    void fill_rect_pattern(const Rect& bounds, Pattern pattern, const Color& fg, const Color& bg) override;
    void draw_image_nine_slice(const std::string& image_name, const Rect& bounds, const Thickness& slice_margins) override;
    void draw_image(const Bitmap& image, const Rect& bounds, Stretch stretch) override;
    void draw_image_rect(const Bitmap& image, const Rect& source, const Rect& bounds) override;
    void draw_svg(const SvgImage& svg, const Rect& bounds) override;
    void push_opacity_layer(float opacity) override;
    void pop_opacity_layer() override;
    void push_matrix(const Matrix& transform) override;
    void pop_transform() override;
    void set_rasterization(const SetRasterization& policy) override;
    ///@}

private:
    std::vector<Callback<void(Renderer&) const>> calls_;
};

} // namespace ui
