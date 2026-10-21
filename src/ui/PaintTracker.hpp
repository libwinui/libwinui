// SPDX-License-Identifier: MIT
// How Window repaints only what changed. Every frame Window::take_damage() draws the whole tree
// into a PaintTracker that only measures: for each element it hashes the commands the element
// itself issues (and the transform, clip and opacity they are drawn under) and works out the area
// they cover in window-client DIPs. An element whose hash differs from the last frame's damages
// where it drew then and where it draws now; one that drew other children, or children in another
// order, damages its whole subtree's old and new area. Nothing relies on a control marking itself
// dirty: a hover a control only keeps in a member shows up as a different command.
//
// Window::render(renderer, clip) then draws through a culling PaintTracker, which hands every
// command on to the real renderer and lets Element::render() skip a subtree the measuring pass
// found to draw nowhere near the clip.
//
// The measuring pass also probes the elements that want compositor layers (Element::layer_count()):
// it records each layer's content, where the element is drawn and under what clip, and whether
// anything drawn after the element covers its layers. Window::sync_layers() reads the probes once
// the pass is done and decides which layers run on the compositor.
//
// It also lists the elements that track whether they are on screen (Element::set_screen_tracking()),
// so Window::sync_screen() can compare where each was drawn with the client area. An element
// that does not track costs one flag test.
#pragma once

#include "../render/Compositor.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/RenderRecording.hpp"
#include "winui/render/Renderer.hpp"

#include <memory>
#include <optional>
#include <vector>

#include <cstdint>

namespace ui
{
class Element;
class ElementWatch;
struct Theme;
} // namespace ui

namespace ui::detail
{

// The smallest rect holding both; an empty rect holds nothing.
[[nodiscard]] Rect united(const Rect& a, const Rect& b);

// One per element being drawn by a measuring PaintTracker, innermost last; the first stands for
// the window itself.
struct PaintLevel
{
    std::uint64_t own      = 0; // hash of its own commands
    std::uint64_t children = 0; // hash of the children it drew, in order
    Rect          own_area{};   // where its own commands draw
    Rect          area{};       // where its subtree draws
};

// One layer of a probed element: where the element puts it (its own coordinates), its content
// with (0,0) at the top-left of the placement's bounds, and that content's hash.
struct ProbedLayer
{
    detail::LayerPlacement           placement;
    std::unique_ptr<RenderRecording> content;
    std::uint64_t                    hash = 0;
};

// An element that wanted layers in this frame, or that had them running and wants none now.
struct LayerProbe
{
    const Element*           element = nullptr;
    Matrix                   transform{}; // the element's coordinates to window-client DIPs
    std::optional<Rect>      clip;        // its ancestors' clip, window-client DIPs
    float                    opacity = 1.0f;
    Rect                     area{};           // where its layers can show, window-client DIPs
    bool                     open     = true;  // still inside the element: its own drawing is below
    bool                     occluded = false; // something drawn after it covers `area`
    std::vector<ProbedLayer> layers;
};

// The layers of one element running on the compositor, kept from frame to frame.
struct ComposedLayers
{
    ComposedLayers();
    ~ComposedLayers();
    ComposedLayers(ComposedLayers&&) noexcept;
    ComposedLayers& operator=(ComposedLayers&&) noexcept;

    std::unique_ptr<ElementWatch>       element;
    VisualId                            key = InvalidVisualId; // the element's visual_id()
    std::vector<VisualId>               ids;                   // one per layer
    std::vector<std::uint64_t>          hashes;                // the content each layer was painted with
    std::vector<detail::LayerPlacement> placements;            // where each was synced, window-client DIPs
    Duration                            since{};               // compositor time the run started at
    bool                                shown = false;         // the layers exist on the compositor
    std::uint32_t                       frame = 0;             // the last frame the element was probed in
};

// An element whose compositor visuals were showing in the last frame.
struct ComposedVisuals
{
    std::unique_ptr<ElementWatch> element;
    VisualId                      key = InvalidVisualId; // the element's visual_id()
};

// A measuring PaintTracker's stacks, kept by the Window from frame to frame so a frame allocates
// nothing for them.
struct PaintScratch
{
    std::vector<PaintLevel>          stack;
    std::vector<Matrix>              matrices;
    std::vector<std::optional<Rect>> clips; // window space, each cut to the ones around it
    std::vector<float>               opacities;
    std::vector<Rect>                acrylic;
    std::vector<Rect>                damage;
    std::vector<LayerProbe>          layers;  // this frame's probes, in drawing order
    std::vector<LayerProbe>          visuals; // this frame's elements with compositor visuals, no layers
    std::vector<const Element*>      screen;  // this frame's drawn elements that track the screen

    // Not touched by the PaintTracker: the tracking elements the last frame found on screen.
    std::vector<std::unique_ptr<ElementWatch>> on_screen;

    // Not touched by the PaintTracker: what Window::sync_layers() keeps running, and on which
    // compositor.
    std::vector<ComposedLayers> composed;
    detail::Compositor*         compositor = nullptr;

    // Not touched by the PaintTracker: the elements Window::sync_covered_visuals() last left with
    // their compositor visuals showing.
    std::vector<ComposedVisuals> showing;
};

class PaintTracker final : public Renderer
{
public:
    // Measures: draws nothing, records each element's hash and areas, and appends to
    // `scratch.damage` what changed since frame `frame` - 1.
    PaintTracker(std::uint32_t frame, PaintScratch& scratch);
    // Culls: draws through `inner`, skipping elements measured in `frame` to draw outside `clip`.
    PaintTracker(Renderer& inner, std::uint32_t frame, const Rect& clip);
    ~PaintTracker() override;
    PaintTracker(const PaintTracker&)            = delete;
    PaintTracker& operator=(const PaintTracker&) = delete;

    // The tracker `renderer` is, if it is the one active on this thread.
    [[nodiscard]] static PaintTracker* active(const Renderer& renderer) noexcept;

    [[nodiscard]] bool culling() const
    {
        return inner_ != nullptr;
    }
    [[nodiscard]] std::uint32_t frame() const
    {
        return frame_;
    }
    [[nodiscard]] const Rect& clip() const
    {
        return clip_;
    }

    // Element::render() brackets each element's drawing with these.
    void begin_element(const Element& e);
    void end_element(const Element& e);

    // Measuring: records `count` layers of `e` (drawn under `theme`, with `own` and `own_opacity`
    // being its visual transform and opacity), right after begin_element(). A count of 0 records
    // an element whose layers ran and are no longer wanted.
    void probe_layers(const Element& e, const Theme& theme, const Matrix& own, float own_opacity, std::size_t count);
    // Records where `e`'s compositor visuals show (its bounds), to find out whether anything covers them.
    void probe_visuals(const Element& e, const Matrix& own, float own_opacity);
    // Whether drawing covers the probes. Off while the window draws the focus rings, which go
    // around an element and over its edge without hiding it.
    void set_covering(bool covering)
    {
        covering_ = covering;
    }

    // What was drawn outside every element (popup shadows, the modal's scrim, a drag visual):
    // its hash and area, for Window to compare with the last frame's.
    [[nodiscard]] std::uint64_t window_hash() const
    {
        return s_->stack.front().own;
    }
    [[nodiscard]] const Rect& window_area() const
    {
        return s_->stack.front().own_area;
    }
    // Where in-app acrylic draws, grown by how far its blur reads: a change anywhere in one of
    // these changes the acrylic over it.
    [[nodiscard]] const std::vector<Rect>& acrylic_areas() const
    {
        return s_->acrylic;
    }

    // ---- Renderer ----
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
    void                draw_image(const Bitmap& image, const Rect& bounds, Stretch stretch) override;
    void                draw_image_rect(const Bitmap& image, const Rect& source, const Rect& bounds) override;
    void                draw_svg(const SvgImage& svg, const Rect& bounds) override;
    void                push_opacity_layer(float opacity) override;
    void                pop_opacity_layer() override;
    void                push_matrix(const Matrix& transform) override;
    void                pop_transform() override;
    void                set_rasterization(const SetRasterization& policy) override;
    [[nodiscard]] float device_scale() const override;

private:
    bool covering_ = true; // set_covering()
    // Starts a command: its kind and its parameters go into the innermost element's hash.
    void hash(std::uint64_t value);
    void hash_bytes(const void* data, std::size_t size);
    template <typename T>
    void hash_value(const T& value)
    {
        hash_bytes(&value, sizeof(T));
    }
    // Covers `local` (in the current transform's space), grown for anti-aliasing and clipped to
    // the current clip.
    void cover(const Rect& local);
    // Hashes `points` and covers their bounding box grown by `outset`.
    void cover_points(const std::vector<Point>& points, float outset);

    Renderer*     inner_ = nullptr;
    std::uint32_t frame_ = 0;
    Rect          clip_{};
    PaintScratch* s_             = nullptr; // measuring only
    std::uint64_t rasterization_ = 0;
    PaintTracker* outer_;
};

} // namespace ui::detail
