// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ui
{

/// One sampled point of an ink stroke: where it is, in the canvas's own DIPs (0,0 is the canvas's
/// top-left corner), and how hard the pen pressed there.
///
/// Example:
/// @code
/// ui::InkStroke line;
/// line.points = {{.position = {10, 10}}, {.position = {90, 40}, .pressure = 0.9f}};
/// @endcode
struct InkPoint
{
    Point position{};      ///< Canvas DIPs.
    float pressure = 0.5f; ///< 0 to 1, the pen's PointerEvent::pressure. A mouse always reports 0.5.

    friend bool operator==(const InkPoint&, const InkPoint&) = default;
};

/// The shape of the nib a stroke is drawn with.
enum class InkPenTip
{
    Circle,    ///< A round nib, elliptical when its width and height differ. Draws smooth, even lines.
    Rectangle, ///< A flat, axis-aligned nib, like a highlighter's chisel tip.
};

/// Which family of ink a stroke's attributes describe.
enum class InkDrawingAttributesKind
{
    Default, ///< Pen and highlighter ink.
    Pencil,  ///< Pencil ink: lighter, with a grain of specks, at `pencil_opacity`.
};

/// How a stroke looks. The defaults are a black, round, 2 DIP pen.
///
/// `size` is the nib's width and height. A pressure-sensitive stroke scales it at each point: a
/// quarter of the size at no pressure, the full size at 0.5 and 1.75 times the size at full
/// pressure. `ignore_pressure` keeps every point at the full size. A highlighter is drawn at half
/// opacity and under the ordinary ink, so writing stays legible over it.
///
/// Example:
/// @code
/// auto canvas = std::make_unique<ui::InkCanvasControl>();
/// ui::InkDrawingAttributes marker{
///     .color               = ui::Color::from_rgba(255, 230, 0),
///     .size                = {6.0f, 18.0f},
///     .pen_tip             = ui::InkPenTip::Rectangle,
///     .draw_as_highlighter = true,
/// };
/// canvas->set_drawing_attributes(marker);
/// @endcode
struct InkDrawingAttributes
{
    Color                    color               = colors::Black;     ///< The ink's color.
    Size                     size                = {2.0f, 2.0f};      ///< The nib's width and height, DIPs.
    InkPenTip                pen_tip             = InkPenTip::Circle; ///< The nib's shape.
    bool                     draw_as_highlighter = false;             ///< Translucent, drawn under other ink.
    bool                     ignore_pressure     = false;             ///< Every point at the full size.
    bool                     fit_to_curve        = true;              ///< Smooths the points into Bézier curves.
    InkDrawingAttributesKind kind                = InkDrawingAttributesKind::Default; ///< Pen or pencil ink.
    float                    pencil_opacity      = 1.0f; ///< Opacity from 0 to 1, for pencil ink only.

    /// The defaults with `kind` set to Pencil.
    [[nodiscard]] static InkDrawingAttributes for_pencil()
    {
        return InkDrawingAttributes{.kind = InkDrawingAttributesKind::Pencil};
    }

    friend bool operator==(const InkDrawingAttributes&, const InkDrawingAttributes&) = default;
};

/// One stroke of ink: its points, in drawing order, and how it looks.
///
/// A stroke is a plain value. A ui::InkStrokeContainer gives it an `id` when it is added.
///
/// Example:
/// @code
/// auto          canvas = std::make_unique<ui::InkCanvasControl>();
/// ui::InkStroke tick{.points = {{.position = {20, 50}}, {.position = {40, 70}}, {.position = {80, 20}}}};
/// tick.attributes.color = ui::Color::from_rgba(0, 128, 85);
/// canvas->strokes().add_stroke(std::move(tick));
/// @endcode
struct InkStroke
{
    std::vector<InkPoint> points{};     ///< The sampled points, in canvas DIPs.
    InkDrawingAttributes  attributes{}; ///< Color, size, nib and the rest.
    std::uint32_t         id = 0;       ///< Unique within its container. 0 for a stroke in no container.
    /// Whether the stroke is selected. A selected stroke is drawn hollow in its own color, and the
    /// container's move_selected() and delete_selected() act on it.
    bool selected = false;

    /// The area the stroke paints: its points' bounds grown by half the nib at its widest. An
    /// empty rect at the origin for a stroke with no points.
    [[nodiscard]] Rect bounding_rect() const;

    friend bool operator==(const InkStroke&, const InkStroke&) = default;
};

class InkCanvasControl;

/// The strokes on a ui::InkCanvasControl. Read them, add, remove, clear or replace them all, and
/// select, move and delete some of them.
///
/// Every stroke that goes in gets a fresh `id` in place of the one it came with, so a set saved
/// with strokes() and handed back to replace() comes back renumbered. Every change, including a
/// change of selection, redraws the canvas and raises InkCanvasControl::on_strokes_changed(). Each
/// canvas owns one container, reached through InkCanvasControl::strokes().
///
/// Selection is the basis for a lasso. The canvas reports the pointer input it doesn't turn into
/// ink through InkCanvasControl::on_unprocessed_input(). The app collects the outline, passes it
/// to select_with_polyline(), then moves or deletes the selected strokes.
///
/// Example:
/// @code
/// // Keep a copy, clear the canvas, and put the drawing back later.
/// auto canvas = std::make_unique<ui::InkCanvasControl>();
/// std::vector<ui::InkStroke> saved{canvas->strokes().strokes().begin(), canvas->strokes().strokes().end()};
/// canvas->strokes().clear();
/// canvas->strokes().replace(std::move(saved));
/// @endcode
class InkStrokeContainer
{
public:
    InkStrokeContainer()                                     = default;
    InkStrokeContainer(const InkStrokeContainer&)            = delete;
    InkStrokeContainer& operator=(const InkStrokeContainer&) = delete;

    /// Every stroke, oldest first.
    [[nodiscard]] std::span<const InkStroke> strokes() const
    {
        return strokes_;
    }
    /// The number of strokes.
    [[nodiscard]] std::size_t size() const
    {
        return strokes_.size();
    }
    /// True when there are no strokes.
    [[nodiscard]] bool empty() const
    {
        return strokes_.empty();
    }
    /// The stroke with `id`, or nullptr. The pointer lasts until the next change.
    [[nodiscard]] const InkStroke* stroke_by_id(std::uint32_t id) const;

    /// Appends `stroke` on top of the others and returns the id it was given. A stroke with no
    /// points is dropped and 0 is returned.
    std::uint32_t add_stroke(InkStroke stroke);
    /// Appends each stroke in order, as one change.
    void add_strokes(std::vector<InkStroke> strokes);
    /// Removes the stroke with `id`. Returns false when there is none.
    bool remove_stroke(std::uint32_t id);
    /// Removes every stroke. Does nothing, and raises no change, when there are none.
    void clear();
    /// Removes every stroke and adds `strokes` in their place, as one change. Use it to load a
    /// saved drawing.
    void replace(std::vector<InkStroke> strokes);

    /// The union of every stroke's bounding_rect(). Empty when there are none.
    [[nodiscard]] Rect bounding_rect() const;

    // ---- selection ----
    /// Selects the strokes lying wholly inside the outline `points` and deselects the rest. The
    /// outline is closed back to its first point. Returns the selection's bounding rect, empty when
    /// nothing is inside. Pass it the outline of a lasso drawn over the canvas.
    Rect select_with_polyline(std::span<const Point> points);
    /// Selects the strokes the line from `from` to `to` crosses and deselects the rest. Returns the
    /// selection's bounding rect, empty when it crosses none.
    Rect select_with_line(Point from, Point to);
    /// Selects or deselects the stroke with `id`. Returns false when there is none.
    bool set_selected(std::uint32_t id, bool selected);
    /// Deselects every stroke.
    void clear_selection();
    /// Whether any stroke is selected.
    [[nodiscard]] bool has_selection() const;
    /// The union of the selected strokes' bounding_rect(). Empty when none is selected.
    [[nodiscard]] Rect selection_bounding_rect() const;
    /// Moves every selected stroke by `translation` DIPs. Returns the selection's new bounding rect.
    Rect move_selected(Point translation);
    /// Removes every selected stroke. Returns the rect they covered, empty when none was selected.
    Rect delete_selected();

private:
    friend class InkCanvasControl;

    void        changed();
    InkStroke&  push(InkStroke stroke);
    std::size_t erase_where_id(std::uint32_t id);

    std::vector<InkStroke> strokes_;
    std::uint32_t          next_id_ = 1;
    std::uint64_t          version_ = 0;  // bumped on every change; the canvas's geometry cache key
    Callback<void()>       on_changed_{}; // the owning canvas
};

/// What pointer input on a ui::InkCanvasControl does.
enum class InkCanvasInputMode
{
    None,    ///< Draws and erases nothing. Pointer input goes to on_unprocessed_input().
    Inking,  ///< The default. A drag draws a stroke with drawing_attributes(), and a pen's eraser erases.
    Erasing, ///< A left-button drag removes every whole stroke it touches (the stroke eraser).
};

/// What a right-button drag on a ui::InkCanvasControl does. A pen pressed with its barrel button
/// held counts as the right button.
enum class InkCanvasRightDragAction
{
    LeaveUnprocessed, ///< The default. The drag isn't inked and goes to on_unprocessed_input().
    AllowProcessing,  ///< Inks or erases as a left-button drag does.
};

/// The ruler on a ui::InkCanvasControl, a straight edge to draw along.
///
/// While it shows, a press on its body drags it and the wheel over it turns it. A stroke started on
/// or close outside one of its long edges is drawn straight along that edge, outside it, wherever
/// the pointer goes. Its scale is in centimeters, at 96 DIPs to the inch, and it shows its angle.
///
/// Example:
/// @code
/// auto canvas = std::make_unique<ui::InkCanvasControl>();
/// canvas->set_ruler({.visible = true, .angle = 30.0f}); // centered on the canvas, turned 30 degrees
/// @endcode
struct InkRuler
{
    bool                 visible = false;     ///< Shown on the canvas.
    std::optional<Point> center{};            ///< Its middle, canvas DIPs. nullopt is the canvas's center.
    float                angle      = 0.0f;   ///< Degrees clockwise from horizontal.
    float                length     = 600.0f; ///< Its length, DIPs.
    float                width      = 80.0f;  ///< Its width across, DIPs.
    bool                 tick_marks = true;   ///< The centimeter scale.

    friend bool operator==(const InkRuler&, const InkRuler&) = default;
};

/// The protractor on a ui::InkCanvasControl, a circle to draw arcs along.
///
/// While it shows, a press on its body drags it and the wheel over it turns its scale. A stroke
/// started on or close outside its edge is drawn around the circle, outside it.
///
/// Example:
/// @code
/// auto canvas = std::make_unique<ui::InkCanvasControl>();
/// canvas->set_protractor({.visible = true, .center = ui::Point{200, 150}, .radius = 120.0f});
/// @endcode
struct InkProtractor
{
    bool                 visible = false;        ///< Shown on the canvas.
    std::optional<Point> center{};               ///< Its center, canvas DIPs. nullopt is the canvas's center.
    float                angle         = 0.0f;   ///< How far the scale is turned clockwise, degrees.
    float                radius        = 160.0f; ///< Its radius, DIPs.
    bool                 tick_marks    = true;   ///< A degree scale around the edge.
    bool                 center_marker = true;   ///< A cross at the center.
    bool                 angle_readout = true;   ///< A readout of its angle under the center.

    friend bool operator==(const InkProtractor&, const InkProtractor&) = default;
};

/// What the eraser removes.
enum class InkCanvasEraseMode
{
    Stroke, ///< The default. Every whole stroke the eraser touches.
    Point,  ///< Only the ink under the eraser. A stroke it crosses splits in two.
};

/// A surface the user draws ink on with the pointer.
///
/// In Inking mode a press starts a stroke, every move adds a point and the release commits it to
/// strokes(), raising on_strokes_collected(). A press without a move leaves a dot. The stroke is
/// drawn as it grows, each point at the pen's pressure. A mouse always reports 0.5. In Erasing
/// mode the drag removes each stroke it passes over and raises on_strokes_erased(). A pen's eraser
/// end or eraser button does the same in Inking mode, unless set_eraser_input_enabled(false) turns
/// it off. With `fit_to_curve`, strokes are smoothed into Bézier curves. Highlighter strokes are
/// drawn first and translucent, the rest on top, all clipped to the canvas. The canvas has no
/// background of its own, so put it in a ui::BorderControl for paper.
///
/// Only the devices in input_device_types() draw. By default these are the pen and the mouse,
/// which leaves touch to whatever scrolls the canvas. A right-button drag, including a pen with its
/// barrel button held, draws nothing and goes to on_unprocessed_input() (see
/// set_right_drag_action()). So does every press, move and release in None mode. Use that input
/// to build a lasso or another tool of the app's own. A ruler and a protractor, shown with
/// set_ruler() and set_protractor(), lie over the ink to draw straight lines and arcs along. With
/// set_erase_mode(InkCanvasEraseMode::Point) the eraser removes only the ink under it,
/// eraser_size() wide, and leaves the rest of a stroke it crosses as new strokes. strokes() is the
/// data to save, in whatever format the app likes. `ui::InkCanvas` is the declarative form, and
/// ui::InkToolbarControl is a ready-made set of tools for it.
///
/// Example:
/// @code
/// auto  owned  = std::make_unique<ui::InkCanvasControl>();
/// auto* canvas = owned.get();
/// canvas->set_drawing_attributes({.color = ui::Color::from_rgba(230, 27, 27), .size = {4.0f, 4.0f}});
/// canvas->on_strokes_collected([](std::span<const ui::InkStroke> added)
///                              { std::println("{} new stroke(s)", added.size()); });
/// window.set_content(std::move(owned));
///
/// canvas->set_input_mode(ui::InkCanvasInputMode::Erasing); // the next drag erases
/// canvas->strokes().clear();                                // or wipe it all
/// @endcode
class InkCanvasControl final : public Element
{
public:
    /// An empty canvas in Inking mode with the default drawing attributes.
    InkCanvasControl();
    ~InkCanvasControl() override;

    // ---- strokes ----
    /// The committed strokes.
    [[nodiscard]] InkStrokeContainer& strokes()
    {
        return strokes_;
    }
    /// The committed strokes, read-only.
    [[nodiscard]] const InkStrokeContainer& strokes() const
    {
        return strokes_;
    }
    /// The stroke being drawn right now, or nullptr between strokes. Not yet in strokes().
    [[nodiscard]] const InkStroke* wet_stroke() const
    {
        return wet_ ? &*wet_ : nullptr;
    }

    // ---- input ----
    /// What the next stroke is drawn with.
    [[nodiscard]] const InkDrawingAttributes& drawing_attributes() const
    {
        return attributes_;
    }
    /// Sets what the next stroke is drawn with. Strokes already drawn keep their own attributes.
    void set_drawing_attributes(const InkDrawingAttributes& attributes);

    /// What pointer input does. Inking by default.
    [[nodiscard]] InkCanvasInputMode input_mode() const
    {
        return mode_;
    }
    /// Switches what the next press does. A stroke in progress is committed first.
    void set_input_mode(InkCanvasInputMode mode);

    /// The devices whose presses draw or erase. Pen | Mouse by default.
    [[nodiscard]] PointerDeviceTypes input_device_types() const
    {
        return devices_;
    }
    /// Sets the devices that draw or erase. Takes effect from the next press. A stroke in progress
    /// is finished with the device that started it.
    void set_input_device_types(PointerDeviceTypes devices)
    {
        devices_ = devices;
    }

    /// Whether a pen's eraser erases in Inking mode. True by default. When off, the eraser end
    /// draws nothing.
    [[nodiscard]] bool eraser_input_enabled() const
    {
        return eraser_input_;
    }
    /// Turns the pen's eraser on or off. Takes effect from the next press.
    void set_eraser_input_enabled(bool enabled)
    {
        eraser_input_ = enabled;
    }

    /// What the eraser removes: whole strokes, the default, or only the ink under it.
    [[nodiscard]] InkCanvasEraseMode erase_mode() const
    {
        return erase_mode_;
    }
    /// Sets what the eraser removes. Takes effect from the next eraser move.
    void set_erase_mode(InkCanvasEraseMode mode)
    {
        erase_mode_ = mode;
    }
    /// The point eraser's width in DIPs, 8 by default.
    [[nodiscard]] float eraser_size() const
    {
        return eraser_size_;
    }
    /// Sets the point eraser's width, at least 1 DIP.
    void set_eraser_size(float size)
    {
        eraser_size_ = (std::max)(1.0f, size);
    }

    /// Whether a right-button drag inks or goes to on_unprocessed_input(). LeaveUnprocessed by
    /// default.
    [[nodiscard]] InkCanvasRightDragAction right_drag_action() const
    {
        return right_drag_;
    }
    /// Sets what a right-button drag does. Takes effect from the next press.
    void set_right_drag_action(InkCanvasRightDragAction action)
    {
        right_drag_ = action;
    }

    // ---- stencils ----
    /// The ruler. Hidden by default.
    [[nodiscard]] const InkRuler& ruler() const
    {
        return ruler_;
    }
    /// Shows, hides, moves or turns the ruler. Redraws.
    void set_ruler(const InkRuler& ruler);
    /// The protractor. Hidden by default.
    [[nodiscard]] const InkProtractor& protractor() const
    {
        return protractor_;
    }
    /// Shows, hides, moves or turns the protractor. Redraws.
    void set_protractor(const InkProtractor& protractor);
    /// Raised when the user drags or turns the ruler or the protractor.
    void on_stencils_changed(Callback<void()> handler)
    {
        on_stencils_changed_ = std::move(handler);
    }

    // ---- lasso ----
    /// The outline set_lasso_outline() draws. Empty by default.
    [[nodiscard]] std::span<const Point> lasso_outline() const
    {
        return lasso_;
    }
    /// Draws `outline`, in canvas DIPs, as a dashed line over the ink and the stencils. Use it to
    /// show the trail of a drag that selects strokes with
    /// InkStrokeContainer::select_with_polyline(). An empty outline hides it. Redraws.
    void set_lasso_outline(std::vector<Point> outline);


    /// Raised with the stroke the user drew, when it is committed to strokes().
    void on_strokes_collected(Callback<void(std::span<const InkStroke>)> handler)
    {
        on_strokes_collected_ = std::move(handler);
    }
    /// Raised with the strokes the eraser removed, after they are gone. The point eraser reports
    /// the strokes it cut, and what is left of them stays as new strokes.
    void on_strokes_erased(Callback<void(std::span<const InkStroke>)> handler)
    {
        on_strokes_erased_ = std::move(handler);
    }
    /// Raised after any change to strokes(): drawn, erased or changed through the container.
    void on_strokes_changed(Callback<void()> handler)
    {
        on_strokes_changed_ = std::move(handler);
    }
    /// Raised for pointer input the canvas doesn't turn into ink, with the position in canvas DIPs.
    /// In None mode that is every press, move and release over the canvas, plus the moves of a drag
    /// that left it. In the other modes it is a right-button drag, from press to release, when the
    /// right-drag action is InkCanvasRightDragAction::LeaveUnprocessed. A Leave event ends a drag
    /// the window lost.
    void on_unprocessed_input(Callback<void(const PointerEvent&)> handler)
    {
        on_unprocessed_input_ = std::move(handler);
    }

    // ---- automation ----
    /// Exposed to UI Automation as a pane.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Pane;
    }

protected:
    // ---- Element ----
    /// Returns no size of its own. A canvas is as large as its slot, or its width and height, make it.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Draws the highlighter strokes, the other strokes, the wet stroke, the stencils and the lasso
    /// outline, clipped to the bounds.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Turns the ruler or the protractor under the pointer. Anywhere else the wheel isn't consumed.
    bool wheel_override(const PointerEvent& e) override;
    /// Left-button drags (pen tip or finger) from input_device_types() draw or erase, as
    /// input_mode() and the pen's eraser say. Input that isn't inked goes to on_unprocessed_input().
    bool pointer_override(const PointerEvent& e) override;


private:
    [[nodiscard]] InkPoint to_local(const PointerEvent& e) const;
    void                   commit_wet();
    void                   erase_along(Point from, Point to);
    void                   raise_unprocessed(const PointerEvent& e);
    [[nodiscard]] Point    ruler_center() const;
    [[nodiscard]] Point    protractor_center() const;
    // The point a stroke started on a stencil's edge is drawn at.
    [[nodiscard]] InkPoint snapped(InkPoint p) const;

    InkStrokeContainer       strokes_;
    InkDrawingAttributes     attributes_{};
    InkCanvasInputMode       mode_         = InkCanvasInputMode::Inking;
    PointerDeviceTypes       devices_      = PointerDeviceTypes::Pen | PointerDeviceTypes::Mouse;
    bool                     eraser_input_ = true;
    InkCanvasEraseMode       erase_mode_   = InkCanvasEraseMode::Stroke;
    float                    eraser_size_  = 8.0f;
    InkCanvasRightDragAction right_drag_   = InkCanvasRightDragAction::LeaveUnprocessed;
    InkRuler                 ruler_{};
    InkProtractor            protractor_{};
    // The stencil a press is dragging (0 none, 1 the ruler, 2 the protractor) and where it was.
    int   stencil_drag_ = 0;
    Point stencil_last_{};
    // The edge the stroke being drawn snaps to: 0 none, else the ruler's side (-1, 1) or 2 for the
    // protractor.
    int                snap_ = 0;
    std::vector<Point> lasso_{};

    std::optional<InkStroke> wet_{};
    // The button of the drag being inked or erased, and of one left unprocessed (None for none).
    PointerButton inking_button_      = PointerButton::None;
    PointerButton unprocessed_button_ = PointerButton::None;
    bool          erasing_            = false;
    Point         last_erase_{};

    // The dry strokes as draw commands, rebuilt when strokes_.version_ moves on.
    mutable std::vector<DrawGeometry> dry_cache_{};
    mutable std::uint64_t             dry_version_ = ~std::uint64_t{0};

    Callback<void(std::span<const InkStroke>)> on_strokes_collected_;
    Callback<void(std::span<const InkStroke>)> on_strokes_erased_;
    Callback<void()>                           on_strokes_changed_;
    Callback<void(const PointerEvent&)>        on_unprocessed_input_;
    Callback<void()>                           on_stencils_changed_;

protected:
    void enabled_changed() override;
};

/// A surface the user draws ink on. Builds a ui::InkCanvasControl, whose description says what the
/// pointer does in each mode.
///
/// `strokes` is the drawing it starts with. Read and change the strokes later through the
/// control's InkCanvasControl::strokes(). The canvas has no background of its own, so it usually
/// sits in a ui::Border.
///
/// Example:
/// @code
/// ui::Control<ui::InkCanvas> canvas;
/// ui::Property<std::size_t>  count{0};
///
/// window.set_content(ui::VerticalStack{.spacing = 8, .content = {
///     ui::Text{.text = ui::bind(count, [](std::size_t n) { return std::format("{} stroke(s)", n); })},
///     ui::Border{
///         .background       = ui::colors::White,
///         .border_brush     = ui::Color::from_rgba(0, 0, 0, 40),
///         .border_thickness = ui::Thickness{1.0f},
///         .height           = 280.0f,
///         .content          = ui::InkCanvas{
///             .drawing_attributes = ui::InkDrawingAttributes{.color = ui::Color::from_rgba(0, 77, 230),
///                                                            .size  = {4.0f, 4.0f}},
///             .accessibility      = {.name = "Drawing canvas"},
///             .on_strokes_changed = [&] { count = canvas->strokes().size(); },
///             .control            = canvas,
///         },
///     },
///     ui::Button{.text = "Clear", .on_click = [&] { canvas->strokes().clear(); }},
/// }});
/// @endcode
struct InkCanvas
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<InkCanvas>` points at.
    using control_type = InkCanvasControl;

    // ---- properties ----
    /// The attributes the next stroke is drawn with.
    Binding<InkDrawingAttributes> drawing_attributes{};
    /// What a left-button drag does: draw (the default), erase whole strokes, or nothing.
    Binding<InkCanvasInputMode> input_mode{InkCanvasInputMode::Inking};
    /// The devices whose presses draw or erase. The pen and the mouse by default.
    Binding<PointerDeviceTypes> input_device_types{PointerDeviceTypes::Pen | PointerDeviceTypes::Mouse};
    /// Whether a pen's eraser end erases in Inking mode.
    Binding<bool> eraser_input_enabled{true};
    /// What the eraser removes: whole strokes, the default, or only the ink under it.
    Binding<InkCanvasEraseMode> erase_mode{InkCanvasEraseMode::Stroke};
    /// The point eraser's width, DIPs.
    Binding<float> eraser_size{8.0f};
    /// Whether a right-button drag inks or goes to `on_unprocessed_input`, the default.
    Binding<InkCanvasRightDragAction> right_drag_action{InkCanvasRightDragAction::LeaveUnprocessed};
    /// The ruler. Hidden by default.
    Binding<InkRuler> ruler{};
    /// The protractor. Hidden by default.
    Binding<InkProtractor> protractor{};
    /// The strokes the canvas starts with. Each gets a fresh id.
    std::vector<InkStroke> strokes{};

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
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False takes no ink but still shows it.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The stroke the user drew, when it is committed.
    Callback<void(std::span<const InkStroke>)> on_strokes_collected{};
    /// The strokes the eraser removed.
    Callback<void(std::span<const InkStroke>)> on_strokes_erased{};
    /// Any change to the strokes: drawn, erased, or changed through the container.
    Callback<void()> on_strokes_changed{};
    /// The pointer input the canvas doesn't turn into ink, in canvas DIPs.
    Callback<void(const PointerEvent&)> on_unprocessed_input{};
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
    ControlOut<InkCanvas> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::InkCanvasControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
