// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/SvgImage.hpp"
#include "winui/render/Text.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ui
{

/// How a control's border is drawn: the Fluent fill alone, or a classic 2 px bevel.
///
/// Flat and Rounded draw no separate border; the control's fill is the whole look. Raised and
/// Sunken add the Windows 95/98/2000 light/dark bevel around the fill, drawn by
/// draw_bevel_border().
enum class BorderStyle
{
    Flat,    ///< Square-cornered fill, no border draw.
    Rounded, ///< Theme-rounded fill, no border draw; the Fluent default.
    Raised,  ///< Classic bevel of a control at rest (light top-left, dark bottom-right).
    Sunken,  ///< Classic bevel of a pressed or checked control (the colors swapped).
};

// ---- Abstract render commands -------------------------------------------------

/// Recorded form of Renderer::fill_rounded_rect(): a solid rounded-rect fill.
///
/// Example:
/// @code
/// // What Renderer::fill_rounded_rect() draws. In an element of your own:
/// class Panel : public ui::Element
/// {
///     void render_override(ui::Renderer& r, const ui::RenderContext& context) const override
///     {
///         const ui::Theme& theme = context.theme;
///         r.fill_rounded_rect(bounds(), 4.0f, theme.colors().background);
///     }
/// };
/// @endcode
struct FillRoundedRect
{
    Rect  bounds;        ///< The rect to fill, in DIPs.
    float radius = 0.0f; ///< Corner radius in DIPs; 0 is a plain rect.
    Color color;         ///< The fill color.

    friend bool operator==(const FillRoundedRect&, const FillRoundedRect&) = default;
};

/// Recorded form of Renderer::stroke_rounded_rect(): a solid outline of a rounded rect,
/// centered on `bounds`' edge (so half of `stroke_width` falls outside it).
struct StrokeRoundedRect
{
    Rect  bounds;              ///< The rect the stroke is centered on, in DIPs.
    float radius = 0.0f;       ///< Corner radius in DIPs; 0 is a plain rect.
    Color color;               ///< The stroke color.
    float stroke_width = 1.0f; ///< Pen width in DIPs.

    friend bool operator==(const StrokeRoundedRect&, const StrokeRoundedRect&) = default;
};

// GradientStop, GradientAxis and LinearGradientBrush live in render/Brush.hpp, with the other
// brushes a Shape can paint with.

/// A gradient fill of a rounded rect (radius 0 == a plain rect), along the brush's
/// GradientAxis: the XP/Vista counterpart to FillRoundedRect's flat color. Recorded form of
/// Renderer::fill_rounded_rect_gradient().
struct FillRoundedRectGradient
{
    Rect                bounds;        ///< The rect to fill, in DIPs.
    float               radius = 0.0f; ///< Corner radius in DIPs.
    LinearGradientBrush brush;         ///< Stops and axis, mapped across `bounds`.

    friend bool operator==(const FillRoundedRectGradient&, const FillRoundedRectGradient&) = default;
};

/// The stroke counterpart of FillRoundedRectGradient, used for Fluent's elevation border: a
/// linear gradient stroked around a rounded rect. The gradient spans `extent_px` device pixels
/// whatever the height of `bounds` (Fluent uses 3). A positive span starts at `bounds.top()`
/// and `brush`'s stops read top to bottom. A negative span starts at `bounds.bottom()` and the
/// stops read bottom to top, which is how an accent control's flipped border is drawn.
struct StrokeRoundedRectGradient
{
    Rect                bounds;              ///< The rect the stroke is centered on, in DIPs.
    float               radius = 0.0f;       ///< Corner radius in DIPs.
    LinearGradientBrush brush;               ///< Stops only; the axis is always vertical.
    float               extent_px    = 3.0f; ///< Signed gradient span in device pixels (see above).
    float               stroke_width = 1.0f; ///< Pen width in DIPs.

    friend bool operator==(const StrokeRoundedRectGradient&, const StrokeRoundedRectGradient&) = default;
};

/// A single straight stroked line segment. The classic raised and sunken bevels (a light edge
/// and a dark edge around a flat fill) are built from these: draw_bevel_border() draws four per
/// bevel ring.
struct StrokeLine
{
    Point from;                ///< Start point, in DIPs.
    Point to;                  ///< End point, in DIPs.
    Color color;               ///< The stroke color.
    float stroke_width = 1.0f; ///< Pen width in DIPs.

    friend bool operator==(const StrokeLine&, const StrokeLine&) = default;
};

/// A wavy line from `from` to `to`, the underline that marks a SpellChecker's misspellings.
/// It is a command of its own, so TestRenderer records one squiggle per word instead of many
/// line segments, and each backend draws its own curve.
/// The wave swings `amplitude` DIPs to either side of the from→to axis, one full period
/// every `wavelength` DIPs, starting on the axis.
struct StrokeWavyLine
{
    Point from;                ///< Start of the axis, in DIPs.
    Point to;                  ///< End of the axis, in DIPs.
    Color color;               ///< The stroke color.
    float stroke_width = 1.0f; ///< Pen width in DIPs.
    float amplitude    = 1.0f; ///< Peak distance from the axis, in DIPs.
    float wavelength   = 4.0f; ///< Length of one full period along the axis, in DIPs.

    friend bool operator==(const StrokeWavyLine&, const StrokeWavyLine&) = default;
};

/// A straight-edged polygon filled with one color, such as a menu's cascade arrow. The shape is
/// always closed; StrokePolyline outlines one.
///
/// Example:
/// @code
/// renderer.fill_polygon(ui::FillPolygon{
///     .points = {{0, 10}, {8, 0}, {16, 10}},
///     .fill   = ui::colors::White,
/// });
/// @endcode
struct FillPolygon
{
    std::vector<Point> points; ///< Vertices in DIPs, in drawing order.
    Color              fill;   ///< Interior color; transparent paints nothing.

    friend bool operator==(const FillPolygon&, const FillPolygon&) = default;
};

/// Straight lines through `points` in order, with the closing back()-to-front() edge only when
/// `closed`. TeachingTip's tail outlines its two slanted sides this way and leaves open the side
/// it shares with the tip's body.
///
/// Example:
/// @code
/// const std::vector<ui::Point> tail = {{0, 10}, {8, 0}, {16, 10}};
/// renderer.fill_polygon(ui::FillPolygon{.points = tail, .fill = ui::colors::White});
/// renderer.stroke_polyline(ui::StrokePolyline{
///     .points       = tail,
///     .color        = ui::Color::from_rgba(0, 0, 0, 40),
///     .stroke_width = 1.0f,
///     .closed       = false, // the two slanted sides only
/// });
/// @endcode
struct StrokePolyline
{
    std::vector<Point> points;               ///< Vertices in DIPs, in drawing order.
    Color              color;                ///< Line color; transparent draws nothing.
    float              stroke_width = 1.0f;  ///< Line width in DIPs; 0 draws nothing.
    bool               closed       = false; ///< Draw the closing back()-to-front() edge too.

    friend bool operator==(const StrokePolyline&, const StrokePolyline&) = default;
};

/// How an open end (or each dash end) of a stroke is finished.
enum class PenLineCap
{
    Flat,     ///< Ends exactly at the end point (the default).
    Square,   ///< Extends half the thickness past the end point, square.
    Round,    ///< A half-disc of the thickness past the end point.
    Triangle, ///< A triangle of half the thickness past the end point.
};

/// How a stroke turns a corner.
enum class PenLineJoin
{
    Miter, ///< Sharp point, cut back to a bevel past StrokeStyle::miter_limit (the default).
    Bevel, ///< The corner cut off flat.
    Round, ///< The corner rounded.
};

/// A Shape's stroke settings with their defaults. `dash_array` and `dash_offset` are in
/// multiples of `thickness`. An empty array is a solid line.
///
/// Example:
/// @code
/// auto line = std::make_unique<ui::LineControl>();
/// line->set_stroke(ui::colors::Black);
/// line->set_stroke_style(ui::StrokeStyle{
///     .start_cap  = ui::PenLineCap::Round,
///     .end_cap    = ui::PenLineCap::Round,
///     .dash_array = {2.0f, 1.0f}, // 2x thickness on, 1x off
/// });
/// line->set_stroke_thickness(4.0f); // thickness is owned by this setter
/// @endcode
struct StrokeStyle
{
    float              thickness   = 1.0f;               ///< Pen width in DIPs.
    PenLineCap         start_cap   = PenLineCap::Flat;   ///< Cap at a figure's first point.
    PenLineCap         end_cap     = PenLineCap::Flat;   ///< Cap at a figure's last point.
    PenLineCap         dash_cap    = PenLineCap::Flat;   ///< Cap at both ends of every dash.
    PenLineJoin        line_join   = PenLineJoin::Miter; ///< How corners are joined.
    float              miter_limit = 10.0f;              ///< Miter length cap, in half-thicknesses.
    std::vector<float> dash_array{};                     ///< On/off lengths in thicknesses; empty is solid.
    float              dash_offset = 0.0f;               ///< Start phase in the dash pattern, in thicknesses.

    friend bool operator==(const StrokeStyle&, const StrokeStyle&) = default;
};

/// A PathGeometry, filled and then stroked: what every Shape draws.
/// Either brush may be absent. A gradient's relatively mapped points span `brush_box` and its
/// absolute ones count from its top-left; a Shape passes its own layout rect, since
/// a Shape's brushes map to the element's top-left. An empty `brush_box` means the geometry's own
/// bounds.
///
/// An `AcrylicBrush` fill depends on drawing order: it blurs whatever was drawn under the
/// geometry before this command, so it must come after the content it sits on. Window::render()
/// draws popups last, so a popup's acrylic blurs the finished tree.
///
/// Example:
/// @code
/// renderer.draw_geometry(ui::DrawGeometry{
///     .geometry     = ui::PathGeometry::ellipse(ui::Point{50, 50}, 40, 40),
///     .fill         = ui::Brush{ui::colors::White},
///     .stroke       = ui::Brush{ui::Color::from_rgba(0, 120, 215)},
///     .stroke_style = {.thickness = 2.0f},
/// });
/// @endcode
struct DrawGeometry
{
    PathGeometry         geometry{};     ///< The figures to paint, in DIPs.
    std::optional<Brush> fill{};         ///< Interior paint; nullopt skips the fill.
    std::optional<Brush> stroke{};       ///< Outline paint; nullopt skips the stroke.
    StrokeStyle          stroke_style{}; ///< Pen for `stroke`; ignored when there is none.
    Rect                 brush_box{};    ///< Box gradients map to; empty uses the geometry's bounds.

    friend bool operator==(const DrawGeometry&, const DrawGeometry&) = default;
};

/// Recorded form of Renderer::draw_text(): a UTF-8 string laid out in `bounds` per `style`.
///
/// Example:
/// @code
/// // What Renderer::draw_text() draws. In an element of your own:
/// class Label : public ui::Element
/// {
///     void render_override(ui::Renderer& r, const ui::RenderContext& context) const override
///     {
///         const ui::Theme& theme = context.theme;
///         r.draw_text("Save", bounds(), theme.colors().text, theme.type.body);
///     }
/// };
/// @endcode
struct DrawTextRun
{
    std::string text;   ///< The UTF-8 text.
    Rect        bounds; ///< Layout box the text is aligned within, in DIPs.
    Color       color;  ///< Text color.
    TextStyle   style;  ///< Font, size, weight, alignment and wrapping.

    friend bool operator==(const DrawTextRun&, const DrawTextRun&) = default;
};

/// Restricts subsequent drawing to `bounds` until the matching PopClip. Pushes and pops must
/// balance within a single render() call. A container that clips, such as Scroll, pushes before
/// rendering its children and pops right after.
///
/// Example:
/// @code
/// void render_visible(ui::Renderer& renderer, const ui::RenderContext& context, const ui::Rect& viewport,
///                     const std::vector<ui::Element*>& visible_children)
/// {
///     renderer.push_clip(viewport);
///     for (const ui::Element* child : visible_children)
///     {
///         child->render(renderer, context);
///     }
///     renderer.pop_clip();
/// }
/// @endcode
struct PushClip
{
    Rect bounds; ///< The clip rect, in DIPs (transformed by any enclosing PushTransform).

    friend bool operator==(const PushClip&, const PushClip&) = default;
};

/// Ends the innermost PushClip, PushClipEllipse or PushClipRoundedRect.
struct PopClip
{
    friend bool operator==(const PopClip&, const PopClip&) = default;
};

/// Draws every call between this and the matching PopOpacityLayer as one layer at `opacity`.
/// Flyouts and dialogs fade in and out this way: Window::render() wraps a flyout's popup, or a
/// dialog's scrim and content together, in one layer at Element::transition_opacity().
///
/// At `opacity` 1, which is every Element that isn't fading, the layer must look like no layer,
/// and a backend may skip it. At 0 or below a backend need not draw the contents at all.
/// Pushes and pops must balance within a single render() call, like PushClip and PopClip, and
/// the two kinds nest inside each other freely.
///
/// Example:
/// @code
/// void render_faded(ui::Renderer& renderer, const ui::RenderContext& context, const ui::Element& popup_content)
/// {
///     renderer.push_opacity_layer(0.5f);
///     popup_content.render(renderer, context); // drawn at half opacity, as one layer
///     renderer.pop_opacity_layer();
/// }
/// @endcode
struct PushOpacityLayer
{
    float opacity = 1.0f; ///< Layer opacity, 0..1; 1 is a no-op.

    friend bool operator==(const PushOpacityLayer&, const PushOpacityLayer&) = default;
};

/// Ends the innermost PushOpacityLayer.
struct PopOpacityLayer
{
    friend bool operator==(const PopOpacityLayer&, const PopOpacityLayer&) = default;
};

/// An affine transform of everything drawn until the matching PopTransform: a point `p` lands at
/// `(p.x * scale + p.y * m21 + offset.x, p.x * m12 + p.y * scale_y + offset.y)` (a `Matrix` in
/// render/PathGeometry.hpp's row-vector convention), and nested pushes compose (the inner one
/// applies first). Scroll zooms this way: its content is laid out once at its natural size and
/// drawn magnified. `scale_y` differs from `scale` for ScaleToFit's `Stretch::Fill`. `m12` and
/// `m21` are non-zero only for a rotation, which comes from Element::set_rotation(). Clips pushed
/// inside a transform are transformed with it, and a rotated clip becomes its bounding box.
/// Pushes and pops balance within one render() call.
///
/// Example:
/// @code
/// void render_zoomed(ui::Renderer& renderer, const ui::RenderContext& context, const ui::Element& content)
/// {
///     renderer.push_transform(2.0f, ui::Point{10, 10}); // uniform 2x zoom, then offset
///     content.render(renderer, context);
///     renderer.pop_transform();
/// }
/// @endcode
struct PushTransform
{
    float scale = 1.0f;   ///< Horizontal scale (Matrix::m11); also the vertical one when uniform.
    Point offset{};       ///< Translation applied after scaling, in DIPs.
    float scale_y = 1.0f; ///< Vertical scale (Matrix::m22).
    float m12     = 0.0f; ///< Rotation/skew term; 0 unless the element is rotated.
    float m21     = 0.0f; ///< Rotation/skew term; 0 unless the element is rotated.

    /// The same transform as a Matrix.
    [[nodiscard]] Matrix matrix() const
    {
        return Matrix{scale, m12, m21, scale_y, offset.x, offset.y};
    }

    friend bool operator==(const PushTransform&, const PushTransform&) = default;
};

/// Ends the innermost PushTransform.
struct PopTransform
{
    friend bool operator==(const PopTransform&, const PopTransform&) = default;
};

/// An elliptical clip, such as PersonPicture's circular photo mask. It ends with the same PopClip
/// as a rect clip. A backend that pops each kind of clip differently tracks which kind it pushed.
///
/// Example:
/// @code
/// const ui::Bitmap photo = ui::Bitmap::read("avatar.jpg");
/// renderer.push_clip_ellipse(ui::Point{24, 24}, 24, 24);
/// renderer.draw_image(photo, ui::Rect{0, 0, 48, 48}, ui::Stretch::UniformToFill);
/// renderer.pop_clip();
/// @endcode
struct PushClipEllipse
{
    Point center;          ///< Center of the ellipse, in DIPs.
    float radius_x = 0.0f; ///< Horizontal radius, in DIPs.
    float radius_y = 0.0f; ///< Vertical radius, in DIPs.

    friend bool operator==(const PushClipEllipse&, const PushClipEllipse&) = default;
};

/// A rectangular clip with rounded corners (a card or a tile whose picture follows its rounded
/// edge). Paired with the same PopClip as every other clip.
///
/// Example:
/// @code
/// const ui::Bitmap photo = ui::Bitmap::read("cover.jpg");
/// renderer.push_clip_rounded_rect(ui::Rect{0, 0, 240, 160}, 8.0f);
/// renderer.draw_image(photo, ui::Rect{0, 0, 240, 160}, ui::Stretch::UniformToFill);
/// renderer.pop_clip();
/// @endcode
struct PushClipRoundedRect
{
    Rect  bounds;        ///< The rectangle, in DIPs.
    float radius = 0.0f; ///< The corner radius, in DIPs; 0 is a plain rect clip.

    friend bool operator==(const PushClipRoundedRect&, const PushClipRoundedRect&) = default;
};

/// A two-color stipple fill: the classic 50 % dither of a disabled glyph, a drag rubber band or
/// a scrollbar trough. Only one pattern exists. The enum leaves room for others, such as
/// hatches, without a breaking change.
enum class Pattern
{
    Checkerboard50, ///< Alternating fg/bg device pixels, the 50 % dither.
};

/// Recorded form of Renderer::fill_rect_pattern(): `bounds` filled with a Pattern in two
/// colors.
///
/// Example:
/// @code
/// // The Windows 95 scrollbar trough.
/// const ui::Rect trough{0, 16, 16, 200};
/// renderer.fill_rect_pattern(trough, ui::Pattern::Checkerboard50, ui::colors::White,
///                            ui::Color::from_rgba(192, 192, 192));
/// @endcode
struct FillRectPattern
{
    Rect    bounds;                            ///< The rect to fill, in DIPs.
    Pattern pattern = Pattern::Checkerboard50; ///< Which stipple.
    Color   fg;                                ///< The "on" pixels.
    Color   bg;                                ///< The "off" pixels.

    friend bool operator==(const FillRectPattern&, const FillRectPattern&) = default;
};

/// A nine-slice image blit: looks `image_name` up in whatever ImageThemePack the backend was
/// configured with and draws it stretched (corners verbatim, edges stretched along one axis,
/// center stretched along both) to fill `bounds`, per `slice_margins`. libwinui ships no
/// images itself. Without a matching image, or without a pack, which is the common case, the
/// call draws nothing and is no error, so a control can always ask for image chrome.
///
/// Example:
/// @code
/// renderer.draw_image_nine_slice("button.normal", ui::Rect{0, 0, 75, 23},
///                                ui::Thickness{3});
/// @endcode
struct DrawImageNineSlice
{
    std::string image_name;    ///< Key into the backend's ImageThemePack.
    Rect        bounds;        ///< Destination rect, in DIPs.
    Thickness   slice_margins; ///< Corner/edge sizes, in the source image's pixels.

    friend bool operator==(const DrawImageNineSlice&, const DrawImageNineSlice&) = default;
};

/// A Bitmap stretched into `bounds` per `stretch`, as drawn by Image, Icon and PersonPicture.
/// Renderer::draw_image() takes the Bitmap itself, while DrawImageNineSlice looks an image up by
/// name. The recorded command keeps `image_size` but not the pixels. A backend reads the pixels
/// during the call.
///
/// Example:
/// @code
/// const ui::Bitmap photo = ui::Bitmap::read("avatar.jpg", {.max_width = 96, .max_height = 96});
/// renderer.draw_image(photo, ui::Rect{0, 0, 48, 48}, ui::Stretch::UniformToFill);
/// @endcode
struct DrawImage
{
    Size    image_size;                 ///< Source pixel dimensions, for Stretch's aspect-ratio math.
    Rect    bounds;                     ///< Destination rect, in DIPs.
    Stretch stretch = Stretch::Uniform; ///< How the image is fitted into `bounds`.

    friend bool operator==(const DrawImage&, const DrawImage&) = default;
};

/// A `source` rectangle of a caller-supplied bitmap (in its pixels) drawn into `bounds`,
/// scaled on each axis to fill it. The Image element's stretch modes and nine-grid are built
/// from it: cropping (Stretch None or UniformToFill on a smaller element) is a smaller source
/// rect, and a nine-grid is nine of these. Like DrawImage, the recorded command keeps the
/// image's size but not its pixels.
///
/// Example:
/// @code
/// // Draw the top-left 16x16 tile of a sprite sheet at 2x.
/// const ui::Bitmap sheet = ui::Bitmap::read_png("sprites.png");
/// renderer.draw_image_rect(sheet, ui::Rect{0, 0, 16, 16}, ui::Rect{0, 0, 32, 32});
/// @endcode
struct DrawImageRect
{
    Size image_size; ///< Source pixel dimensions.
    Rect source;     ///< The part of the image drawn, in image pixels.
    Rect bounds;     ///< Destination rect, in DIPs.

    friend bool operator==(const DrawImageRect&, const DrawImageRect&) = default;
};

/// An SvgImage drawn into `bounds`: its viewport is its natural size (or `bounds`' size when
/// it has none), scaled on each axis to `bounds`. The recorded command keeps the natural size,
/// not the markup.
///
/// Example:
/// @code
/// const ui::SvgImage logo = ui::SvgImage::read("logo.svg"); // throws if unreadable
/// renderer.draw_svg(logo, ui::Rect{0, 0, 64, 64});
/// @endcode
struct DrawSvg
{
    Size natural_size; ///< The document's own size in DIPs; empty when it declares none.
    Rect bounds;       ///< Destination rect, in DIPs.

    friend bool operator==(const DrawSvg&, const DrawSvg&) = default;
};

/// A filled ellipse, given by its center and radii. A circle (radius_x == radius_y) given this
/// way has none of the rounding ambiguity of a square rect with a corner radius. The classic
/// circular radio-button bevel is built from these.
///
/// Example:
/// @code
/// renderer.fill_ellipse(ui::Point{10, 10}, 6, 6, ui::colors::White);
/// renderer.stroke_ellipse(ui::Point{10, 10}, 6, 6, ui::colors::Black, 1.0f);
/// @endcode
struct FillEllipse
{
    Point center;          ///< Center, in DIPs.
    float radius_x = 0.0f; ///< Horizontal radius, in DIPs.
    float radius_y = 0.0f; ///< Vertical radius, in DIPs.
    Color color;           ///< The fill color.

    friend bool operator==(const FillEllipse&, const FillEllipse&) = default;
};

/// Recorded form of Renderer::stroke_ellipse(): an ellipse outline centered on its edge.
struct StrokeEllipse
{
    Point center;              ///< Center, in DIPs.
    float radius_x = 0.0f;     ///< Horizontal radius, in DIPs.
    float radius_y = 0.0f;     ///< Vertical radius, in DIPs.
    Color color;               ///< The stroke color.
    float stroke_width = 1.0f; ///< Pen width in DIPs.

    friend bool operator==(const StrokeEllipse&, const StrokeEllipse&) = default;
};

/// Whether shapes are drawn with hard pixel edges or antialiased. The classic themes, Windows 95
/// through Vista/7, use hard edges to match the original look. The Modern and high-contrast
/// themes antialias.
enum class GeometryAA
{
    Aliased,     ///< Hard pixel edges.
    Antialiased, ///< Coverage-based edges (the default).
};

/// How text is antialiased, by era.
enum class TextAA
{
    Aliased,   ///< No smoothing (95/98/2000, the bitmap-font era).
    Grayscale, ///< Grayscale smoothing (XP, and the default).
    Subpixel,  ///< Subpixel smoothing (Vista onward, and every Modern and high-contrast theme).
};

/// The rasterization policy for the whole frame. Window::render() emits it once, at the start,
/// from Theme::rasterization. Because it is a command, TestRenderer records it and describe()
/// prints it, so a test can check that a theme asks for aliased rendering without comparing
/// pixels. The backend applies `snap_to_device_pixels` while drawing, by rounding each
/// primitive's coordinates to the nearest device pixel, so it stays correct under DPI scaling.
/// Layout is unaffected and stays resolution-independent.
///
/// Example:
/// @code
/// // A renderer of your own receives it first in every frame and keeps it for what follows.
/// class ExportRenderer : public ui::Renderer
/// {
/// public:
///     void set_rasterization(const ui::SetRasterization& policy) override
///     {
///         antialias_shapes_ = policy.geometry == ui::GeometryAA::Antialiased;
///         text_smoothing_   = policy.text;
///         snap_to_pixels_   = policy.snap_to_device_pixels;
///     }
///
/// private:
///     bool       antialias_shapes_ = true;
///     ui::TextAA text_smoothing_   = ui::TextAA::Grayscale;
///     bool       snap_to_pixels_   = false;
/// };
/// @endcode
struct SetRasterization
{
    GeometryAA geometry              = GeometryAA::Antialiased; ///< Edge policy for shapes and lines.
    TextAA     text                  = TextAA::Grayscale;       ///< Text smoothing policy.
    bool       snap_to_device_pixels = false;                   ///< Round primitives to device pixels.

    friend bool operator==(const SetRasterization&, const SetRasterization&) = default;
};

/// One recorded draw call: the data form of every Renderer method. TestRenderer::commands is a
/// vector of these, and describe() prints one line per command.
using RenderCommand = std::variant<
    FillRoundedRect,
    StrokeRoundedRect,
    DrawTextRun,
    PushClip,
    PopClip,
    FillRoundedRectGradient,
    StrokeLine,
    StrokeRoundedRectGradient,
    SetRasterization,
    FillEllipse,
    StrokeEllipse,
    FillRectPattern,
    DrawImageNineSlice,
    DrawImage,
    PushClipEllipse,
    PushOpacityLayer,
    PopOpacityLayer,
    StrokeWavyLine,
    FillPolygon,
    PushTransform,
    PopTransform,
    DrawImageRect,
    DrawSvg,
    DrawGeometry,
    StrokePolyline,
    PushClipRoundedRect>;

// ---- Renderer interface -------------------------------------------------------
/// The drawing surface every Element::render() draws through.
///
/// A control calls it from render_override(). The library implements it twice: one renderer draws
/// the window on screen, and TestRenderer records each call as a RenderCommand for unit tests and
/// describe(). All coordinates are DIPs in window space. Every push_* must be balanced by its
/// pop_* within the same render() call.
///
/// Example:
/// @code
/// class SwatchControl final : public ui::Element
/// {
///     ui::Color   color_ = ui::colors::White;
///     std::string label_ = "White";
///
///     // ... measure_override() ...
///     void render_override(ui::Renderer& r, const ui::RenderContext& context) const override
///     {
///         const ui::Theme& theme = context.theme;
///         r.fill_rounded_rect(bounds(), theme.corner_radius, color_);
///         r.stroke_rounded_rect(bounds(), theme.corner_radius, theme.colors().text, 1.0f);
///
///         // Clip and transform calls come in pairs around what they apply to; the scopes
///         // (ui::ClipScope, ...) pop at the end of the block.
///         r.push_clip(bounds());
///         r.draw_text(label_, bounds(), theme.colors().text, theme.type.body);
///         r.pop_clip();
///     }
/// };
/// @endcode
class Renderer
{
public:
    virtual ~Renderer() = default;

    /// Fills a rect with rounded corners (`radius` in DIPs, 0 for square) in a solid color.
    virtual void fill_rounded_rect(const Rect& bounds, float radius, const Color& color) = 0;

    /// Strokes a rounded rect's outline, the pen centered on `bounds`' edge.
    virtual void stroke_rounded_rect(const Rect& bounds, float radius, const Color& color, float stroke_width) = 0;

    /// Draws UTF-8 `text` laid out in `bounds` per `style` (font, alignment, wrapping). Text
    /// overflowing `bounds` is not trimmed, and not clipped unless a clip is pushed.
    virtual void draw_text(std::string_view text, const Rect& bounds, const Color& color, const TextStyle& style) = 0;

    /// Fills a rounded rect with a gradient along the brush's axis. The XP and Vista themes draw
    /// their chrome with it.
    virtual void fill_rounded_rect_gradient(const Rect& bounds, float radius, const LinearGradientBrush& brush) = 0;

    /// Strokes a rounded rect with a gradient spanning `extent_px` device pixels, for Fluent's
    /// elevation border. StrokeRoundedRectGradient explains the sign of `extent_px`.
    virtual void stroke_rounded_rect_gradient(const Rect&                bounds,
                                              float                      radius,
                                              const LinearGradientBrush& brush,
                                              float                      extent_px,
                                              float                      stroke_width) = 0;

    /// A straight line; also one edge of a classic raised/sunken bevel (see
    /// draw_bevel_border(), which draws a whole border out of these).
    virtual void stroke_line(Point from, Point to, const Color& color, float stroke_width) = 0;

    /// Draws the misspelling squiggle (see StrokeWavyLine).
    virtual void stroke_wavy_line(Point from, Point to, const Color& color, float stroke_width, float amplitude, float wavelength) = 0;

    /// Fills a closed polygon with one color (see FillPolygon).
    virtual void fill_polygon(const FillPolygon& polygon) = 0;
    /// Strokes straight lines through a list of points (see StrokePolyline).
    virtual void stroke_polyline(const StrokePolyline& polyline) = 0;

    /// Fills and strokes a PathGeometry (see DrawGeometry). A backend that cannot draw an
    /// AcrylicBrush draws its fallback color.
    virtual void draw_geometry(const DrawGeometry& geometry) = 0;

    /// Clips everything drawn until the matching pop_clip() to `bounds`, intersected with
    /// any enclosing clip.
    virtual void push_clip(const Rect& bounds) = 0;
    /// Ends the innermost push_clip(), push_clip_ellipse() or push_clip_rounded_rect().
    virtual void pop_clip() = 0;

    /// An elliptical clip, ended by the same pop_clip() as a rect clip (see PushClipEllipse).
    virtual void push_clip_ellipse(Point center, float radius_x, float radius_y) = 0;
    /// A clip to `bounds` with corners rounded by `radius`, ended by the same pop_clip() (see
    /// PushClipRoundedRect). A radius of 0 is the same clip as push_clip().
    virtual void push_clip_rounded_rect(const Rect& bounds, float radius) = 0;

    /// Fills an ellipse centered on `center`, radii in DIPs.
    virtual void fill_ellipse(Point center, float radius_x, float radius_y, const Color& color) = 0;
    /// Strokes an ellipse outline centered on `center`, radii in DIPs.
    virtual void stroke_ellipse(Point center, float radius_x, float radius_y, const Color& color, float stroke_width) = 0;

    /// Fills `bounds` with a two-color stipple (see Pattern).
    virtual void fill_rect_pattern(const Rect& bounds, Pattern pattern, const Color& fg, const Color& bg) = 0;

    /// Optional image-theme chrome (see ImageThemePack). A no-op wherever `image_name` isn't
    /// found in whatever pack the backend was configured with; ImageThemePack::empty() is the
    /// common case, and every backend must tolerate it. The default draws nothing, which is
    /// what a backend with no pack does.
    virtual void draw_image_nine_slice(const std::string& image_name, const Rect& bounds, const Thickness& slice_margins)
    {
        (void)image_name;
        (void)bounds;
        (void)slice_margins;
    }

    /// A caller-supplied bitmap, stretched into `bounds` per `stretch` (see DrawImage).
    /// `image` need only be valid for the duration of the call.
    virtual void draw_image(const Bitmap& image, const Rect& bounds, Stretch stretch) = 0;

    /// Draws the `source` part of `image` into `bounds` (see DrawImageRect). `source` is in the
    /// image's pixels and is clipped to them.
    virtual void draw_image_rect(const Bitmap& image, const Rect& source, const Rect& bounds) = 0;

    /// Draws `svg` into `bounds` (see DrawSvg). A backend that cannot draw SVG draws nothing.
    virtual void draw_svg(const SvgImage& svg, const Rect& bounds) = 0;

    /// Draws everything until the matching pop_opacity_layer() as one layer at `opacity`
    /// (0..1); see PushOpacityLayer.
    virtual void push_opacity_layer(float opacity) = 0;
    /// Ends the innermost push_opacity_layer().
    virtual void pop_opacity_layer() = 0;

    /// Transforms everything drawn until the matching pop_transform(); nested pushes compose,
    /// the inner one applying first (see PushTransform). What every push_transform() overload
    /// lands in: a backend implements this one, and callers use push_transform().
    virtual void push_matrix(const Matrix& transform) = 0;
    /// Transforms everything drawn until the matching pop_transform() by `transform`; see
    /// push_matrix().
    void push_transform(const Matrix& transform)
    {
        push_matrix(transform);
    }
    /// Scale by `scale_x`/`scale_y` about the origin, then translate by `offset`.
    void push_transform(float scale_x, float scale_y, Point offset)
    {
        push_matrix(Matrix{scale_x, 0.0f, 0.0f, scale_y, offset.x, offset.y});
    }
    /// Uniform scale by `scale`, then translate by `offset`.
    void push_transform(float scale, Point offset)
    {
        push_transform(scale, scale, offset);
    }
    /// Ends the innermost push_transform().
    virtual void pop_transform() = 0;

    /// Sets the theme's rasterization policy (see SetRasterization). Window::render() calls it
    /// once per frame, first, from the active Theme. A backend that ignores it draws everything
    /// antialiased, which matches the Modern look.
    virtual void set_rasterization(const SetRasterization& policy) = 0;

    /// Device pixels per DIP on the target, the DPI over 96: 1 unless the backend knows better.
    /// Everything else stays in DIPs; this is for the few classic marks that are drawn pixel by
    /// pixel, like the radio well (draw_classic_radio()) and the solid arrows (draw_classic_arrow()),
    /// which pick a mask per device size instead of scaling a 100 % one.
    [[nodiscard]] virtual float device_scale() const
    {
        return 1.0f;
    }
};

/// Pushes a clip when it is made and pops it when it goes out of scope, so an early return
/// cannot leave the clip pushed.
///
/// Example:
/// @code
/// class Caption : public ui::Element
/// {
///     std::string label_ = "A caption longer than its box";
///
///     void render_override(ui::Renderer& r, const ui::RenderContext& context) const override
///     {
///         const ui::Theme&    theme = context.theme;
///         const ui::ClipScope clip{r, bounds()};
///         r.draw_text(label_, bounds(), theme.colors().text, theme.type.body); // clipped to bounds()
///     }
/// };
/// @endcode
class [[nodiscard]] ClipScope
{
public:
    /// Clips to `bounds` (Renderer::push_clip()).
    ClipScope(Renderer& renderer, const Rect& bounds) : renderer_(renderer)
    {
        renderer_.push_clip(bounds);
    }
    /// Clips to an ellipse (Renderer::push_clip_ellipse()).
    ClipScope(Renderer& renderer, Point center, float radius_x, float radius_y) : renderer_(renderer)
    {
        renderer_.push_clip_ellipse(center, radius_x, radius_y);
    }
    /// Clips to a rect with rounded corners (Renderer::push_clip_rounded_rect()).
    ClipScope(Renderer& renderer, const Rect& bounds, float radius) : renderer_(renderer)
    {
        renderer_.push_clip_rounded_rect(bounds, radius);
    }
    /// Pops the clip.
    ~ClipScope()
    {
        renderer_.pop_clip();
    }
    ClipScope(const ClipScope&)            = delete;
    ClipScope& operator=(const ClipScope&) = delete;

private:
    Renderer& renderer_;
};

/// Pushes an opacity layer when it is made and pops it when it goes out of scope.
///
/// Example:
/// @code
/// class FadedSwatch : public ui::Element
/// {
///     ui::Color color_ = ui::colors::Black;
///
///     void render_override(ui::Renderer& r, const ui::RenderContext&) const override
///     {
///         const ui::OpacityLayerScope faded{r, 0.4f};
///         r.fill_rounded_rect(bounds(), 4.0f, color_); // drawn at 40 %
///     }
/// };
/// @endcode
class [[nodiscard]] OpacityLayerScope
{
public:
    /// Draws everything in the scope as one layer at `opacity` (Renderer::push_opacity_layer()).
    OpacityLayerScope(Renderer& renderer, float opacity) : renderer_(renderer)
    {
        renderer_.push_opacity_layer(opacity);
    }
    /// Pops the layer.
    ~OpacityLayerScope()
    {
        renderer_.pop_opacity_layer();
    }
    OpacityLayerScope(const OpacityLayerScope&)            = delete;
    OpacityLayerScope& operator=(const OpacityLayerScope&) = delete;

private:
    Renderer& renderer_;
};

/// Pushes a transform when it is made and pops it when it goes out of scope.
///
/// Example:
/// @code
/// class ZoomedSwatch : public ui::Element
/// {
///     ui::Color color_ = ui::colors::Black;
///
///     void render_override(ui::Renderer& r, const ui::RenderContext&) const override
///     {
///         const ui::TransformScope zoom{r, 2.0f, ui::Point{bounds().x, bounds().y}};
///         r.fill_rounded_rect(ui::Rect{0.0f, 0.0f, 8.0f, 8.0f}, 0.0f, color_); // 16 DIPs at the top left
///     }
/// };
/// @endcode
class [[nodiscard]] TransformScope
{
public:
    /// Transforms by `transform` (Renderer::push_transform()).
    TransformScope(Renderer& renderer, const Matrix& transform) : renderer_(renderer)
    {
        renderer_.push_transform(transform);
    }
    /// Scales by `scale_x`/`scale_y` about the origin, then translates by `offset`.
    TransformScope(Renderer& renderer, float scale_x, float scale_y, Point offset) : renderer_(renderer)
    {
        renderer_.push_transform(scale_x, scale_y, offset);
    }
    /// Scales uniformly by `scale`, then translates by `offset`.
    TransformScope(Renderer& renderer, float scale, Point offset) : renderer_(renderer)
    {
        renderer_.push_transform(scale, offset);
    }
    /// Pops the transform.
    ~TransformScope()
    {
        renderer_.pop_transform();
    }
    TransformScope(const TransformScope&)            = delete;
    TransformScope& operator=(const TransformScope&) = delete;

private:
    Renderer& renderer_;
};

} // namespace ui
