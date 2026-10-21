// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Paint.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cstdlib>

namespace ui
{

/// The base of the shapes. A shape is one geometry, filled with fill() and outlined with
/// stroke(), drawn with one Renderer::draw_geometry() call.
///
/// It can't be constructed on its own. Use Rectangle, Ellipse, Line, Polyline, Polygon or Path, or
/// their ui:: specs. Everything here is shared by all six. Shapes take no input and no focus.
///
/// Example:
/// @code
/// auto shape = std::make_unique<ui::EllipseControl>();
/// shape->set_fill(ui::colors::White);
/// shape->set_stroke(ui::colors::Black);
/// shape->set_stroke_thickness(2.0f);
/// shape->set_stretch(ui::Stretch::Uniform); // a circle, whatever the slot's aspect
/// shape->set_width(100.0f);
/// shape->set_height(60.0f);
/// window.set_content(std::move(shape)); // drawn as one geometry: fill and stroke together
/// @endcode
class Shape : public Element
{
public:
    /// The interior paint. Empty, the default, paints no interior.
    [[nodiscard]] const Paint& fill() const
    {
        return fill_;
    }
    /// Sets the interior paint: a Color, a gradient, an AcrylicBrush or a ThemeColor. Redraws.
    void set_fill(Paint paint)
    {
        fill_ = std::move(paint);
        mark_dirty(DirtyFlags::Visual);
    }

    /// The outline paint. Empty, the default, draws no outline and makes `stroke_thickness()`
    /// count as zero for layout.
    [[nodiscard]] const Paint& stroke() const
    {
        return stroke_;
    }
    /// Sets the outline paint. Updates layout and redraws, since whether there is a stroke changes
    /// how much room the shape takes.
    void set_stroke(Paint paint)
    {
        stroke_ = std::move(paint);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The outline width, 1 by default. This is the `thickness` of stroke_style(). The other
    /// stroke settings, such as caps, join, miter limit and dashes, are in that StrokeStyle.
    [[nodiscard]] float stroke_thickness() const
    {
        return style_.thickness;
    }
    /// Sets the outline width in DIPs. Updates layout and redraws, since the room the stroke takes
    /// changes with it.
    void set_stroke_thickness(float thickness)
    {
        style_.thickness = thickness;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The whole pen, stroke_thickness() included.
    [[nodiscard]] const StrokeStyle& stroke_style() const
    {
        return style_;
    }
    /// Sets everything but the thickness, which set_stroke_thickness() sets. Redraws.
    void set_stroke_style(const StrokeStyle& style)
    {
        const float thickness = style_.thickness;
        style_                = style;
        style_.thickness      = thickness;
        mark_dirty(DirtyFlags::Visual);
    }

    /// How the geometry fills the layout rect. Fill by default for Rectangle and Ellipse, None for
    /// the rest. For Rectangle and Ellipse, Uniform and UniformToFill make the outline square and
    /// None collapses it. For the other shapes it scales the geometry's bounds into the rect, less
    /// the stroke.
    [[nodiscard]] Stretch stretch() const
    {
        return stretch_;
    }
    /// Sets the stretch. Updates layout and redraws.
    void set_stretch(Stretch stretch)
    {
        stretch_ = stretch;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The geometry as drawn, in window coordinates, as render() passes it to the renderer. Valid
    /// after arrange().
    [[nodiscard]] PathGeometry rendered_geometry() const;

protected:
    /// For the concrete shapes. `stretch` is their default Stretch.
    explicit Shape(Stretch stretch) : stretch_(stretch)
    {
    }

    /// The thickness layout reserves: the absolute stroke thickness with a stroke, 0 without.
    [[nodiscard]] float effective_stroke_thickness() const
    {
        return !stroke_.empty() ? std::abs(style_.thickness) : 0.0f;
    }

    /// For Line, Polyline, Polygon and Path: the geometry in local coordinates, before any stretch.
    /// Rectangle and Ellipse override outline_geometry() instead and never call this.
    [[nodiscard]] virtual PathGeometry natural_geometry() const
    {
        return {};
    }

    /// Whether stretch() scales the geometry. False for Rectangle and Ellipse, which build their
    /// geometry at the layout size themselves.
    [[nodiscard]] virtual bool stretches_geometry() const
    {
        return true;
    }

    /// For Rectangle and Ellipse: the geometry, in local coordinates, for an element laid out at
    /// `size`.
    [[nodiscard]] virtual PathGeometry outline_geometry(Size /*size*/) const
    {
        return {};
    }

    /// The layout rect inset by half the stroke, then made square or collapsed as the stretch
    /// says. With `check_degenerate`, which Rectangle sets, the full rect is kept when the stroke is
    /// as thick as a side.
    [[nodiscard]] Rect outline_rect(Size size, bool check_degenerate) const;

    /// The natural geometry's size under stretch(). With Stretch::None that is its bottom-right
    /// corner plus half the stroke, otherwise its bounds scaled into `available`. Rectangle and
    /// Ellipse override this.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Draws the geometry once with fill(), stroke() and stroke_style(). Draws nothing when both
    /// paints are unset or the shape was laid out with no area.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    Paint       fill_{};
    Paint       stroke_{};
    StrokeStyle style_{};
    Stretch     stretch_ = Stretch::None;
};

/// A rectangle, optionally with elliptically rounded corners. Stretch defaults to Fill.
///
/// Its geometry is built from its layout rect, inset by half the stroke, so without a width and
/// height it fills the slot its parent gives it. The declarative form is ui::Rectangle.
///
/// Example:
/// @code
/// auto panel = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* card = panel->add(std::make_unique<ui::RectangleControl>());
/// card->set_fill(ui::Color::from_rgba(70, 130, 180));
/// card->set_stroke(ui::colors::Black);
/// card->set_stroke_thickness(2.0f);
/// card->set_radius_x(8.0f);
/// card->set_radius_y(8.0f);
/// card->set_width(120.0f);
/// card->set_height(80.0f);
/// window.set_content(std::move(panel));
/// @endcode
class RectangleControl final : public Shape
{
public:
    /// A rectangle with Stretch::Fill and no fill or stroke. Set a fill() or stroke() to see it.
    RectangleControl() : Shape(Stretch::Fill)
    {
    }

    /// The corners' horizontal radius in DIPs. 0, the default, gives square corners.
    [[nodiscard]] float radius_x() const
    {
        return radius_x_;
    }
    /// Sets the horizontal corner radius. Redraws.
    void set_radius_x(float radius)
    {
        radius_x_ = radius;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The corners' vertical radius in DIPs. 0, the default, gives square corners.
    [[nodiscard]] float radius_y() const
    {
        return radius_y_;
    }
    /// Sets the vertical corner radius. Redraws.
    void set_radius_y(float radius)
    {
        radius_y_ = radius;
        mark_dirty(DirtyFlags::Visual);
    }

protected:
    [[nodiscard]] bool stretches_geometry() const override
    {
        return false;
    }
    [[nodiscard]] PathGeometry outline_geometry(Size size) const override;

    /// No size of its own, so an explicit width and height apply. Under UniformToFill it asks
    /// for a square covering `available`.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;


private:
    float radius_x_ = 0.0f;
    float radius_y_ = 0.0f;
};

/// The ellipse inscribed in the outline rect. Stretch defaults to Fill.
///
/// Like Rectangle, it takes its size from layout. Stretch::Uniform makes it a circle, placed at the
/// rect's top-left. The declarative form is ui::Ellipse.
///
/// Example:
/// @code
/// auto panel = std::make_unique<ui::StackControl>(ui::Orientation::Horizontal);
/// auto* dot  = panel->add(std::make_unique<ui::EllipseControl>());
/// dot->set_fill(ui::colors::White);
/// dot->set_stroke(ui::colors::Black);
/// dot->set_stroke_thickness(2.0f);
/// dot->set_width(48.0f);
/// dot->set_height(48.0f);
/// window.set_content(std::move(panel));
/// @endcode
class EllipseControl final : public Shape
{
public:
    /// An ellipse with Stretch::Fill and no fill or stroke.
    EllipseControl() : Shape(Stretch::Fill)
    {
    }

protected:
    [[nodiscard]] bool stretches_geometry() const override
    {
        return false;
    }
    [[nodiscard]] PathGeometry outline_geometry(Size size) const override;

    /// Like Rectangle's: no size of its own, except a covering square under UniformToFill.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
};

/// A line from (x1, y1) to (x2, y2) in local coordinates.
///
/// A line has no interior, so only stroke() draws. With the default Stretch::None it asks for
/// room up to its farther end plus half the stroke. The declarative form is ui::Line.
///
/// Example:
/// @code
/// auto panel = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* rule = panel->add(std::make_unique<ui::LineControl>(ui::Point{0.0f, 0.0f},
///                                                       ui::Point{200.0f, 0.0f}));
/// rule->set_stroke(ui::colors::Black);
/// rule->set_stroke_thickness(2.0f);
/// rule->set_x2(300.0f); // stretch it later
/// window.set_content(std::move(panel));
/// @endcode
class LineControl final : public Shape
{
public:
    /// A line from (0, 0) to (0, 0). Invisible until its ends and a stroke are set.
    LineControl() : Shape(Stretch::None)
    {
    }
    /// A line from `from` to `to`, in DIPs of local coordinates.
    LineControl(Point from, Point to) : Shape(Stretch::None), from_(from), to_(to)
    {
    }

    /// The start point, (x1, y1).
    [[nodiscard]] Point from() const
    {
        return from_;
    }
    /// The end point, (x2, y2).
    [[nodiscard]] Point to() const
    {
        return to_;
    }
    /// Sets the start point's x. Each of the four setters updates layout and redraws.
    void set_x1(float v);
    /// Sets the start point's y.
    void set_y1(float v);
    /// Sets the end point's x.
    void set_x2(float v);
    /// Sets the end point's y.
    void set_y2(float v);

protected:
    [[nodiscard]] PathGeometry natural_geometry() const override
    {
        return PathGeometry::line(from_, to_);
    }

private:
    Point from_{};
    Point to_{};
};

/// Straight segments through a list of points. The fill rule, EvenOdd by default, decides what
/// counts as inside.
///
/// A Polyline is open: its stroke stops at the last point. Polygon, which derives from it,
/// closes the last point back to the first. The declarative form is ui::Polyline.
///
/// Example:
/// @code
/// auto panel = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* zigzag = panel->add(std::make_unique<ui::PolylineControl>(std::vector<ui::Point>{
///     {10.0f, 200.0f}, {60.0f, 140.0f}, {130.0f, 140.0f}, {180.0f, 200.0f}}));
/// zigzag->set_stroke(ui::colors::Black);
/// zigzag->set_stroke_thickness(4.0f);
/// window.set_content(std::move(panel));
/// @endcode
class PolylineControl : public Shape
{
public:
    /// An empty polyline: draws nothing until it has points.
    PolylineControl() : Shape(Stretch::None)
    {
    }
    /// A polyline through `points`, in local DIPs.
    explicit PolylineControl(std::vector<Point> points) : Shape(Stretch::None), points_(std::move(points))
    {
    }

    /// The points, in local coordinates.
    [[nodiscard]] const std::vector<Point>& points() const
    {
        return points_;
    }
    /// Replaces every point. Updates layout and redraws.
    void set_points(std::vector<Point> points)
    {
        points_ = std::move(points);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// Which enclosed areas the fill paints where segments cross.
    [[nodiscard]] FillRule fill_rule() const
    {
        return fill_rule_;
    }
    /// Sets the fill rule. Redraws.
    void set_fill_rule(FillRule rule)
    {
        fill_rule_ = rule;
        mark_dirty(DirtyFlags::Visual);
    }

protected:
    /// Whether the figure closes back to its first point. False here, true for Polygon.
    [[nodiscard]] virtual bool closed() const
    {
        return false;
    }
    [[nodiscard]] PathGeometry natural_geometry() const override
    {
        return PathGeometry::polyline(points_, closed(), fill_rule_);
    }

private:
    std::vector<Point> points_{};
    FillRule           fill_rule_ = FillRule::EvenOdd;
};

/// A Polyline whose last point joins back to its first, so the stroke goes all the way around.
/// The declarative form is ui::Polygon.
///
/// Example:
/// @code
/// auto panel    = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* triangle = panel->add(std::make_unique<ui::PolygonControl>(std::vector<ui::Point>{
///     {10.0f, 110.0f}, {60.0f, 10.0f}, {110.0f, 110.0f}}));
/// triangle->set_fill(ui::colors::White);
/// triangle->set_stroke(ui::colors::Black);
/// triangle->set_fill_rule(ui::FillRule::Nonzero);
/// window.set_content(std::move(panel));
/// @endcode
class PolygonControl final : public PolylineControl
{
public:
    /// An empty polygon: draws nothing until it has points.
    PolygonControl() = default;
    /// A polygon through `points`, in local DIPs, closed back to the first.
    explicit PolygonControl(std::vector<Point> points) : PolylineControl(std::move(points))
    {
    }

protected:
    [[nodiscard]] bool closed() const override
    {
        return true;
    }
};

/// Any geometry, parsed from markup with PathGeometry::parse() or built with the PathGeometry
/// builders and PathGeometry::group().
///
/// Stretch::None, the default, draws the geometry where its coordinates put it. Any other Stretch
/// scales its bounds into the layout rect. The declarative form is ui::Path.
///
/// Example:
/// @code
/// auto panel = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// if (auto arrow = ui::PathGeometry::parse("F1 M 20,20L 24,10L 24,24L 5,24")) // nullopt if malformed
/// {
///     auto* icon = panel->add(std::make_unique<ui::PathControl>(std::move(*arrow)));
///     icon->set_fill(ui::colors::Black);
///     icon->set_stretch(ui::Stretch::Uniform);
///     icon->set_width(32.0f);
///     icon->set_height(32.0f);
/// }
/// window.set_content(std::move(panel));
/// @endcode
class PathControl final : public Shape
{
public:
    /// An empty path: draws nothing until it has data().
    PathControl() : Shape(Stretch::None)
    {
    }
    /// A path drawing `data`, in local coordinates.
    explicit PathControl(PathGeometry data) : Shape(Stretch::None), data_(std::move(data))
    {
    }

    /// The geometry, in local coordinates before any stretch.
    [[nodiscard]] const PathGeometry& data() const
    {
        return data_;
    }
    /// Replaces the geometry. Updates layout and redraws.
    void set_data(PathGeometry data)
    {
        data_ = std::move(data);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

protected:
    [[nodiscard]] PathGeometry natural_geometry() const override
    {
        return data_;
    }

private:
    PathGeometry data_{};
};

/// A rectangle, optionally with rounded corners. Builds a ui::RectangleControl.
///
/// It has no natural size. Give it a `.width` and `.height`, or let the parent stretch it.
///
/// Example:
/// @code
/// ui::Property<ui::Color> swatch{ui::Color::from_argb(0xFF0078D4)};
///
/// window.set_content(ui::HorizontalStack{
///     .spacing  = 12,
///     .content  = {
///         ui::Rectangle{ .fill = ui::colors::Black, .width = 100, .height = 60 },
///         ui::Rectangle{ .fill             = swatch, // follows the property
///                        .stroke           = ui::colors::Black,
///                        .stroke_thickness = 2.0f,
///                        .radius_x         = 8.0f,
///                        .radius_y         = 8.0f,
///                        .width            = 100,
///                        .height           = 60 },
///     },
/// });
/// @endcode
struct Rectangle
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Rectangle>` points at.
    using control_type = RectangleControl;

    // ---- properties ----
    /// The interior paint: a Color, gradient, acrylic or ThemeColor. Empty paints none.
    Binding<Paint> fill{};
    /// The outline paint. Empty draws no outline and reserves no room for one.
    Binding<Paint> stroke{};
    /// The outline width, in DIPs.
    Binding<float> stroke_thickness{1.0f};
    /// Caps, join, miter limit and dashes. Its `thickness` is ignored for `.stroke_thickness`.
    Binding<StrokeStyle> stroke_style{};
    /// How the shape fills its layout rect. Fill by default.
    Binding<Stretch> stretch{Stretch::Fill};
    /// Horizontal corner radius in DIPs, clamped to half the width. 0 gives square corners.
    Binding<float> radius_x{0.0f};
    /// Vertical corner radius in DIPs, clamped to half the height. 0 gives square corners.
    Binding<float> radius_y{0.0f};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<Rectangle> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::RectangleControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

/// The ellipse inscribed in the element's rect. Builds a ui::EllipseControl.
///
/// Example:
/// @code
/// window.set_content(ui::Ellipse{
///     .fill                 = ui::Color::from_argb(0xFFFF00FF),
///     .stroke               = ui::colors::Black,
///     .stroke_thickness     = 2.0f,
///     .width                = 152,
///     .height               = 152,
///     .horizontal_alignment = ui::Alignment::Center,
///     .vertical_alignment   = ui::Alignment::Center,
/// });
/// @endcode
struct Ellipse
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Ellipse>` points at.
    using control_type = EllipseControl;

    // ---- properties ----
    /// The interior paint: a Color, gradient, acrylic or ThemeColor. Empty paints none.
    Binding<Paint> fill{};
    /// The outline paint. Empty draws no outline and reserves no room for one.
    Binding<Paint> stroke{};
    /// The outline width, in DIPs.
    Binding<float> stroke_thickness{1.0f};
    /// Caps, join, miter limit and dashes. Its `thickness` is ignored for `.stroke_thickness`.
    Binding<StrokeStyle> stroke_style{};
    /// How the shape fills its layout rect. Fill by default.
    Binding<Stretch> stretch{Stretch::Fill};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<Ellipse> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::EllipseControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

/// A straight line from (x1, y1) to (x2, y2). Builds a ui::LineControl.
///
/// A line has no interior, so there is no `.fill`. Without a `.stroke` it draws nothing.
///
/// Example:
/// @code
/// ui::Property<float> end_x{200.0f};
///
/// window.set_content(ui::Line{
///     .x1               = 0.0f,
///     .y1               = 50.0f,
///     .x2               = end_x,
///     .y2               = 50.0f,
///     .stroke           = ui::Color::from_argb(0xFF4682B4),
///     .stroke_thickness = 4.0f,
/// });
/// end_x = 300.0f; // the line follows
/// @endcode
struct Line
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Line>` points at.
    using control_type = LineControl;

    // ---- properties ----
    /// Start point x, in local DIPs.
    Binding<float> x1{0.0f};
    /// Start point y, in local DIPs.
    Binding<float> y1{0.0f};
    /// End point x, in local DIPs.
    Binding<float> x2{0.0f};
    /// End point y, in local DIPs.
    Binding<float> y2{0.0f};
    /// The outline paint. Empty draws no outline and reserves no room for one.
    Binding<Paint> stroke{};
    /// The outline width, in DIPs.
    Binding<float> stroke_thickness{1.0f};
    /// Caps, join, miter limit and dashes. Its `thickness` is ignored for `.stroke_thickness`.
    Binding<StrokeStyle> stroke_style{};
    /// How the geometry scales into the layout rect. None, the default, draws it where it is.
    Binding<Stretch> stretch{Stretch::None};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<Line> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::LineControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

/// Connected straight lines through `points`, left open. Builds a ui::PolylineControl.
///
/// Example:
/// @code
/// window.set_content(ui::Polyline{
///     .points           = std::vector<ui::Point>{{10.0f, 100.0f}, {60.0f, 40.0f}, {200.0f, 40.0f}, {250.0f, 100.0f}},
///     .stroke           = ui::colors::Black,
///     .stroke_thickness = 4.0f,
/// });
/// @endcode
struct Polyline
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Polyline>` points at.
    using control_type = PolylineControl;

    // ---- properties ----
    /// The vertices, in local DIPs.
    Binding<std::vector<Point>> points{};
    /// Which enclosed regions `.fill` paints where the outline crosses itself.
    Binding<FillRule> fill_rule{FillRule::EvenOdd};
    /// The interior paint: a Color, gradient, acrylic or ThemeColor. Empty paints none.
    Binding<Paint> fill{};
    /// The outline paint. Empty draws no outline and reserves no room for one.
    Binding<Paint> stroke{};
    /// The outline width, in DIPs.
    Binding<float> stroke_thickness{1.0f};
    /// Caps, join, miter limit and dashes. Its `thickness` is ignored for `.stroke_thickness`.
    Binding<StrokeStyle> stroke_style{};
    /// How the geometry scales into the layout rect. None, the default, draws it where it is.
    Binding<Stretch> stretch{Stretch::None};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<Polyline> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::PolylineControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

/// Connected straight lines through `points`, closed back to the first point. Builds a
/// ui::PolygonControl.
///
/// Example:
/// @code
/// window.set_content(ui::Polygon{
///     .points           = std::vector<ui::Point>{{10.0f, 100.0f}, {60.0f, 40.0f}, {200.0f, 40.0f}, {250.0f, 100.0f}},
///     .fill             = ui::Color::from_argb(0xFF4682B4),
///     .stroke           = ui::colors::Black,
///     .stroke_thickness = 2.0f,
/// });
/// @endcode
struct Polygon
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Polygon>` points at.
    using control_type = PolygonControl;

    // ---- properties ----
    /// The vertices, in local DIPs.
    Binding<std::vector<Point>> points{};
    /// Which enclosed regions `.fill` paints where the outline crosses itself.
    Binding<FillRule> fill_rule{FillRule::EvenOdd};
    /// The interior paint: a Color, gradient, acrylic or ThemeColor. Empty paints none.
    Binding<Paint> fill{};
    /// The outline paint. Empty draws no outline and reserves no room for one.
    Binding<Paint> stroke{};
    /// The outline width, in DIPs.
    Binding<float> stroke_thickness{1.0f};
    /// Caps, join, miter limit and dashes. Its `thickness` is ignored for `.stroke_thickness`.
    Binding<StrokeStyle> stroke_style{};
    /// How the geometry scales into the layout rect. None, the default, draws it where it is.
    Binding<Stretch> stretch{Stretch::None};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<Polygon> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::PolygonControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

/// Any geometry, with `data` from PathGeometry::parse() or the PathGeometry builders. Builds a
/// ui::PathControl.
///
/// PathGeometry::parse() returns nullopt for markup that doesn't parse. A literal known to be
/// valid is usually dereferenced directly.
///
/// Example:
/// @code
/// window.set_content(ui::Path{
///     .data             = *ui::PathGeometry::parse("M 10,100 C 100,25 300,250 400,75 H 200"),
///     .stroke           = ui::Color::from_argb(0xFFB8860B),
///     .stroke_thickness = 3.0f,
/// });
/// @endcode
struct Path
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Path>` points at.
    using control_type = PathControl;

    // ---- properties ----
    /// The figures to draw, in local DIPs: `*PathGeometry::parse("M 0,0 L 10,10")` or a PathGeometry builder.
    Binding<PathGeometry> data{};
    /// The interior paint: a Color, gradient, acrylic or ThemeColor. Empty paints none.
    Binding<Paint> fill{};
    /// The outline paint. Empty draws no outline and reserves no room for one.
    Binding<Paint> stroke{};
    /// The outline width, in DIPs.
    Binding<float> stroke_thickness{1.0f};
    /// Caps, join, miter limit and dashes. Its `thickness` is ignored for `.stroke_thickness`.
    Binding<StrokeStyle> stroke_style{};
    /// How the geometry scales into the layout rect. None, the default, draws it where it is.
    Binding<Stretch> stretch{Stretch::None};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<Path> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::PathControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
