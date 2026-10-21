// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Duration.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Version.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/CubicBezier.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/scrolling/ScrollBarOverlay.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ui
{

/// Which axes a Scroll's content is free to grow along.
enum class ScrollContentOrientation
{
    Vertical,   ///< Width fixed to the viewport, height free (the default).
    Horizontal, ///< Height fixed to the viewport, width free.
    None,       ///< Both fixed: the content fits the viewport, and zooming shows more.
    Both,       ///< Both free.
};

/// Whether the user can scroll along an axis.
enum class ScrollMode
{
    Enabled,  ///< The wheel, the scroll bar and the keys scroll this axis.
    Disabled, ///< User input leaves this axis alone. scroll_to() and the other calls still move it.
    Auto,     ///< Enabled while the axis has something to scroll, or while zooming is enabled.
};

/// Whether the user can zoom (Ctrl+wheel).
enum class ScrollZoomMode
{
    Enabled,  ///< Ctrl+wheel zooms, 10 % a notch.
    Disabled, ///< The default. zoom_to() and zoom_by() still work.
};

/// Whether a wheel turn this view cannot use, because it is already at the end the turn points
/// toward, goes on to the scroller around it. Auto and Always differ only for touch input, which
/// the library does not handle, so both pass the turn on. Never keeps the turn, and the page
/// behind does not move.
enum class ScrollChainMode
{
    Auto,   ///< Pass the turn on (the default).
    Always, ///< Pass the turn on, the same as Auto without touch.
    Never,  ///< Keep the turn.
};

/// When a scroll bar shows.
enum class ScrollBarVisibility
{
    Auto,    ///< Shown while its axis is scrollable and its scroll mode enabled.
    Visible, ///< Always shown.
    Hidden,  ///< Never shown.
};

/// Whether a programmatic view change animates.
enum class ScrollAnimationMode
{
    Disabled, ///< Jump straight to the target.
    Enabled,  ///< Animate over a duration that depends on the distance.
    Auto,     ///< Always animates. The system animation setting is not checked.
};

/// What is moving the view right now (ScrollControl::state()).
enum class ScrollInteractionState
{
    Idle,        ///< Nothing.
    Interaction, ///< A scroll bar thumb is being dragged.
    Inertia,     ///< An add_scroll_velocity() is coasting.
    Animating,   ///< A scroll_to(), scroll_by(), zoom_to() or zoom_by() is animating.
};

/// How ScrollControl::scroll_to() and scroll_by() move. Snap points are not supported.
///
/// Example:
/// @code
/// ui::Control<ui::Scroll> view; // a Scroll's .control
///
/// // Back to the top at once, no animation.
/// view->scroll_to(0.0f, 0.0f, {.animation_mode = ui::ScrollAnimationMode::Disabled});
/// @endcode
struct ScrollOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// Whether the scroll animates. Auto animates.
    ScrollAnimationMode animation_mode = ScrollAnimationMode::Auto;
};

/// How ScrollControl::zoom_to() and zoom_by() change the zoom.
struct ScrollZoomOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// Whether the zoom animates. Auto animates.
    ScrollAnimationMode animation_mode = ScrollAnimationMode::Auto;
};

/// How a key frame eases in from the one before it: a cubic Bézier curve, or a step easing when
/// `steps` is positive. A step easing jumps in `steps` equal steps, the last of them at the key
/// frame itself.
struct KeyFrameEasing
{
    /// The curve, used while `steps` is 0. Fluent's fast-out-slow-in by default.
    CubicBezier curve = easing::ControlFastOutSlowIn;
    /// A positive count makes this a step easing instead of `curve`.
    int steps = 0;

    /// The eased progress (0..1) at linear progress `x` (0..1) between the two key frames.
    [[nodiscard]] float evaluate(double x) const;

    friend bool operator==(const KeyFrameEasing&, const KeyFrameEasing&) = default;
};

/// One key frame of a scroll animation: at `progress`, 0 to 1 of the duration, the view is at
/// `offsets`. The offsets are horizontal and vertical, in zoomed DIPs, and are clamped to the
/// scrollable range as the animation plays.
struct ScrollKeyFrame
{
    /// Where in the animation the frame sits, 0..1 of the duration.
    float progress = 1.0f;
    /// The (horizontal, vertical) offsets the view reaches at `progress`.
    Point offsets{};
    /// How the view eases in from the previous key frame.
    KeyFrameEasing easing{};

    friend bool operator==(const ScrollKeyFrame&, const ScrollKeyFrame&) = default;
};

/// The key-frame animation a Scroll builds for an animated scroll. By default it is one key
/// frame at 1.0 on the target, over a duration that depends on the distance. The start at
/// progress 0 is implicit. Key frames play in `progress` order.
struct ScrollAnimation
{
    /// The whole animation's length.
    Duration duration{};
    /// The frames after the implicit start.
    std::vector<ScrollKeyFrame> key_frames;
};

/// The arguments of ScrollControl::on_scroll_animation_starting(). The handler may edit or
/// replace `animation`. `start_position` and `end_position` are the offsets the view moves
/// between.
///
/// Example:
/// @code
/// ui::Control<ui::Scroll> view; // a Scroll's .control
/// view->on_scroll_animation_starting([](ui::ScrollAnimationStarting& args) {
///     args.animation.duration   = 600ms; // slower than the distance-based default
///     args.animation.key_frames = {
///         {.progress = 0.5f, .offsets = args.start_position},  // hold, then
///         {.progress = 1.0f, .offsets = args.end_position},    // move
///     };
/// });
/// @endcode
struct ScrollAnimationStarting
{
    /// The animation about to play. The handler may change it.
    ScrollAnimation animation;
    /// The offsets the view starts from.
    Point start_position{};
    /// The offsets the request targets.
    Point end_position{};
    /// The id scroll_to()/scroll_by() returned for this request.
    int correlation_id = 0;
};

/// One key frame of a zoom animation. It carries a zoom factor, applied about the
/// animation's `center_point`.
struct ZoomKeyFrame
{
    /// Where in the animation the frame sits, 0..1 of the duration.
    float progress = 1.0f;
    /// The zoom factor reached at `progress`.
    float zoom_factor = 1.0f;
    /// How the zoom eases in from the previous key frame.
    KeyFrameEasing easing{};

    friend bool operator==(const ZoomKeyFrame&, const ZoomKeyFrame&) = default;
};

/// The key-frame animation a Scroll builds for an animated zoom. By default it is one key frame
/// at 1.0 on the target factor, over 250 ms per whole zoom step, kept within 50 to 1000 ms.
struct ZoomAnimation
{
    /// The whole animation's length.
    Duration duration{};
    /// The frames after the implicit start, in `progress` order.
    std::vector<ZoomKeyFrame> key_frames;
};

/// The arguments of ScrollControl::on_zoom_animation_starting(). The handler may edit or
/// replace `animation`, as in ScrollAnimationStarting.
struct ZoomAnimationStarting
{
    /// The animation about to play. The handler may change it.
    ZoomAnimation animation;
    /// The zoom factor the view starts from.
    float start_zoom_factor = 1.0f;
    /// The zoom factor the request targets.
    float end_zoom_factor = 1.0f;
    /// The point kept still while zooming, in DIPs relative to the viewport.
    Point center_point{};
    /// The id zoom_to()/zoom_by() returned for this request.
    int correlation_id = 0;
};

/// A viewport that scrolls and zooms one child, with overlay scroll bars.
///
/// The child is measured without a limit along the scrolling axes (content_orientation()) and
/// drawn clipped and offset. Offsets are in zoomed DIPs, from 0 to scrollable_width() and
/// scrollable_height(). The wheel scrolls, Ctrl+wheel zooms while zoom_mode() allows it, the
/// bars drag and step, and with set_focusable() the keys scroll too. scroll_to(), scroll_by(),
/// zoom_to(), zoom_by() and add_scroll_velocity() move it from code, animated by default. Each
/// returns a correlation id that its completed event reports. The declarative form is
/// ui::Scroll.
///
/// Example:
/// @code
/// auto view = std::make_unique<ui::ScrollControl>();
/// view->set_height(300.0f);
/// view->set_zoom_mode(ui::ScrollZoomMode::Enabled);
/// auto* list = view->set_child(std::make_unique<ui::StackControl>(ui::Orientation::Vertical));
/// for (int i = 0; i < 100; ++i)
/// {
///     list->add(std::make_unique<ui::TextControl>(std::format("Row {}", i)));
/// }
/// view->on_view_changed([v = view.get()] { std::println("at {}", v->vertical_offset()); });
/// view->on_scroll_completed([](int id) { std::println("scroll {} done", id); });
/// ui::ScrollControl* observer = view.get();
/// window.set_content(std::move(view));
///
/// observer->scroll_to(0.0f, 500.0f); // animated; lands after the first layout if called before it
/// @endcode
class ScrollControl final : public Element, private ScrollProvider
{
public:
    /// The default min_zoom_factor().
    static constexpr float DefaultMinZoomFactor = 0.1f;
    /// The default max_zoom_factor().
    static constexpr float DefaultMaxZoomFactor = 10.0f;

    /// An empty view: vertical content orientation, Auto modes and bars, zoom disabled.
    ScrollControl() = default;

    /// Takes ownership of the scrollable content and returns a non-owning pointer to it, as
    /// StackControl::add does. Replaces any previous child.
    template <typename T>
    T* set_child(std::unique_ptr<T> child)
    {
        static_assert(std::is_base_of_v<Element, T>, "Scroll content must be an Element");
        T* observer = child.get();
        if (observer) // null clears
        {
            set_parent_of(*observer, this);
        }
        child_ = std::move(child);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The content (non-owning), or nullptr before set_child().
    [[nodiscard]] Element* child() const
    {
        return child_.get();
    }

    // ---- configuration ----
    /// Which axes the child is free to grow along. Vertical by default.
    [[nodiscard]] ScrollContentOrientation content_orientation() const
    {
        return content_orientation_;
    }
    /// Lays the view out again.
    void set_content_orientation(ScrollContentOrientation orientation);

    /// Whether user input scrolls horizontally. Auto by default.
    [[nodiscard]] ScrollMode horizontal_scroll_mode() const
    {
        return horizontal_scroll_mode_;
    }
    /// Takes effect on the next input. Scrolling from code ignores it.
    void set_horizontal_scroll_mode(ScrollMode mode);
    /// Whether user input scrolls vertically. Auto by default.
    [[nodiscard]] ScrollMode vertical_scroll_mode() const
    {
        return vertical_scroll_mode_;
    }
    /// Takes effect on the next input. Scrolling from code ignores it.
    void set_vertical_scroll_mode(ScrollMode mode);
    /// The horizontal scroll mode with Auto resolved, so never Auto.
    [[nodiscard]] ScrollMode computed_horizontal_scroll_mode() const;
    /// The vertical counterpart of computed_horizontal_scroll_mode().
    [[nodiscard]] ScrollMode computed_vertical_scroll_mode() const;

    /// Whether Ctrl+wheel zooms. Disabled by default.
    [[nodiscard]] ScrollZoomMode zoom_mode() const
    {
        return zoom_mode_;
    }
    /// Also changes what the Auto scroll modes resolve to.
    void set_zoom_mode(ScrollZoomMode mode);

    /// Whether dragging the content with the left button moves the view, as a hand tool does. A
    /// pen or a finger counts as the left button. Off by default. Only a press that nothing
    /// inside handles starts a pan, so the content's buttons and boxes keep their clicks, and
    /// only the axes the user may scroll (computed_horizontal_scroll_mode(),
    /// computed_vertical_scroll_mode()) move. While there is something to pan, the pointer shows
    /// the move cursor over the content.
    [[nodiscard]] bool pan_on_drag() const
    {
        return pan_on_drag_;
    }
    /// Takes effect on the next press. Turning it off during a drag lets the drag finish.
    void set_pan_on_drag(bool pan);
    /// The move cursor over the content while pan_on_drag() has something to pan.
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point p) const override;


    /// Whether a wheel turn this view can't use goes on to the scroller around it, per axis
    /// (ScrollChainMode). Auto by default: a list scrolled to its bottom passes the wheel on to
    /// the page it sits in.
    [[nodiscard]] ScrollChainMode horizontal_scroll_chain_mode() const
    {
        return horizontal_scroll_chain_mode_;
    }
    /// Takes effect on the next wheel turn.
    void set_horizontal_scroll_chain_mode(ScrollChainMode mode)
    {
        horizontal_scroll_chain_mode_ = mode;
    }
    /// The vertical axis's chain mode; see horizontal_scroll_chain_mode().
    [[nodiscard]] ScrollChainMode vertical_scroll_chain_mode() const
    {
        return vertical_scroll_chain_mode_;
    }
    /// Takes effect on the next wheel turn.
    void set_vertical_scroll_chain_mode(ScrollChainMode mode)
    {
        vertical_scroll_chain_mode_ = mode;
    }

    /// When the horizontal bar shows. Auto by default.
    [[nodiscard]] ScrollBarVisibility horizontal_scroll_bar_visibility() const
    {
        return horizontal_scroll_bar_visibility_;
    }
    /// Redraws. The bars lie over the content, so the layout stays the same.
    void set_horizontal_scroll_bar_visibility(ScrollBarVisibility visibility);
    /// When the vertical bar shows. Auto by default.
    [[nodiscard]] ScrollBarVisibility vertical_scroll_bar_visibility() const
    {
        return vertical_scroll_bar_visibility_;
    }
    /// Redraws. The bars lie over the content, so the layout stays the same.
    void set_vertical_scroll_bar_visibility(ScrollBarVisibility visibility);
    /// Whether the horizontal bar shows right now, with Auto resolved against whether the axis
    /// can scroll and its scroll mode.
    [[nodiscard]] bool computed_horizontal_scroll_bar_visible() const;
    /// Whether the vertical bar shows right now, as computed_horizontal_scroll_bar_visible().
    [[nodiscard]] bool computed_vertical_scroll_bar_visible() const;

    /// The smallest zoom factor. DefaultMinZoomFactor (0.1) by default.
    [[nodiscard]] float min_zoom_factor() const
    {
        return min_zoom_factor_;
    }
    /// Clamps the current zoom factor into the new range.
    void set_min_zoom_factor(float value);
    /// The largest zoom factor. DefaultMaxZoomFactor (10) by default.
    [[nodiscard]] float max_zoom_factor() const
    {
        return max_zoom_factor_;
    }
    /// Clamps the current zoom factor into the new range.
    void set_max_zoom_factor(float value);

    /// The fixed color painted behind the content across the viewport. nullopt, the default,
    /// paints nothing.
    [[nodiscard]] std::optional<Color> background() const
    {
        return background_.color();
    }
    /// The theme resource the background is drawn from, or null.
    [[nodiscard]] ThemeColor background_resource() const
    {
        return background_.resource();
    }
    /// A fixed background. Replaces any theme resource and redraws.
    void set_background(std::optional<Color> color)
    {
        background_ = color;
        mark_dirty(DirtyFlags::Visual);
    }
    /// A background read from the theme each time it is drawn. Replaces any fixed color.
    void set_background(ThemeColor resource)
    {
        background_ = resource;
        mark_dirty(DirtyFlags::Visual);
    }

    /// Whether the Scroll itself takes focus, ahead of its content in the tab order, and scrolls
    /// with the keyboard. False by default. focusable() reads it and is false while disabled.
    /// Lays the view out again, since the focus ring needs room.
    void set_focusable(bool focusable)
    {
        focusable_ = focusable;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    // ---- view ----
    /// How far the view is scrolled right, in zoomed DIPs, in [0, scrollable_width()].
    [[nodiscard]] float horizontal_offset() const
    {
        return horizontal_offset_;
    }
    /// How far the view is scrolled down, in zoomed DIPs, in [0, scrollable_height()].
    [[nodiscard]] float vertical_offset() const
    {
        return vertical_offset_;
    }
    /// The zoom factor, within [min_zoom_factor(), max_zoom_factor()]. 1 by default.
    [[nodiscard]] float zoom_factor() const
    {
        return zoom_factor_;
    }
    /// The content's arranged width, unzoomed.
    [[nodiscard]] float extent_width() const
    {
        return extent_.width;
    }
    /// The content's arranged height, unzoomed.
    [[nodiscard]] float extent_height() const
    {
        return extent_.height;
    }
    /// The view's own arranged width in DIPs, less the room a theme's permanent vertical scroll
    /// bar takes while it shows. The overlay bar takes none.
    [[nodiscard]] float viewport_width() const
    {
        return viewport_rect().width;
    }
    /// The view's own arranged height in DIPs, less the room an era's horizontal bar takes.
    [[nodiscard]] float viewport_height() const
    {
        return viewport_rect().height;
    }
    /// max(0, extent_width() * zoom_factor() - viewport_width()), the largest
    /// horizontal_offset().
    [[nodiscard]] float scrollable_width() const
    {
        return (std::max)(0.0f, extent_.width * zoom_factor_ - viewport_width());
    }
    /// The largest vertical_offset(), as scrollable_width().
    [[nodiscard]] float scrollable_height() const
    {
        return (std::max)(0.0f, extent_.height * zoom_factor_ - viewport_height());
    }
    /// Element::scroll_info(), whose content can be pulled a whole viewport past either end.
    [[nodiscard]] std::optional<ScrollInfo> scroll_info() const override
    {
        return ScrollInfo{Point{horizontal_offset_, vertical_offset_},
                          viewport_rect().size(),
                          extent_,
                          zoom_factor_,
                          viewport_rect().size(),
                          child_.get()};
    }
    /// What is moving the view: nothing, a thumb drag, inertia or an animation.
    [[nodiscard]] ScrollInteractionState state() const
    {
        return state_;
    }

    /// Moves the view to the given offsets, in zoomed DIPs clamped to the scrollable range,
    /// animated unless `options` say otherwise. Stops any running animation or inertia and
    /// completes its request.
    ///
    /// Each of these calls returns the request's correlation id, which the matching completed
    /// event reports. A request made before the first layout waits until then, so a view set up
    /// right after it is built lands against the real viewport.
    int scroll_to(float horizontal_offset, float vertical_offset, ScrollOptions options = {});
    /// scroll_to() the current offsets plus the deltas.
    int scroll_by(float horizontal_delta, float vertical_delta, ScrollOptions options = {});
    /// Starts an inertial coast at `offsets_velocity` DIPs per second, or adds to the velocity of
    /// one already coasting. `inertia_decay_rate` is per axis, in [0, 1]. nullopt is 0.95, and 0
    /// keeps a constant speed.
    int add_scroll_velocity(Point offsets_velocity, std::optional<Point> inertia_decay_rate = std::nullopt);
    /// Changes the zoom factor, clamped to the zoom range, about `center_point`, which is relative
    /// to the viewport. nullopt is the viewport's center.
    int zoom_to(float zoom_factor, std::optional<Point> center_point = std::nullopt, ScrollZoomOptions options = {});
    /// zoom_to() the current factor plus `zoom_factor_delta`.
    int zoom_by(float zoom_factor_delta, std::optional<Point> center_point = std::nullopt, ScrollZoomOptions options = {});

    /// Whether this Scroll draws its own vertical scroll bar and lets it take input. True by
    /// default. AnnotatedScrollBarControl::set_target() sets it to false while attached, since
    /// that bar replaces the built-in one.
    [[nodiscard]] bool show_scrollbar() const
    {
        return show_scrollbar_;
    }
    /// Redraws when it changes.
    void set_show_scrollbar(bool show)
    {
        if (show_scrollbar_ == show)
        {
            return;
        }
        show_scrollbar_ = show;
        mark_dirty(DirtyFlags::Visual);
    }

    // ---- events (one handler each; setting another replaces it) ----
    /// An offset or the zoom factor changed.
    void on_view_changed(Callback<void()> handler)
    {
        on_view_changed_ = std::move(handler);
    }
    /// The content's arranged size changed.
    void on_extent_changed(Callback<void()> handler)
    {
        on_extent_changed_ = std::move(handler);
    }
    /// state() changed; read it in the handler.
    void on_state_changed(Callback<void()> handler)
    {
        on_state_changed_ = std::move(handler);
    }
    /// A scroll_to() or scroll_by() finished, with the request's correlation id. A request
    /// replaced by a later one completes when it is replaced.
    void on_scroll_completed(Callback<void(int correlation_id)> handler)
    {
        on_scroll_completed_ = std::move(handler);
    }
    /// A zoom_to() or zoom_by() finished or was replaced, with its correlation id.
    void on_zoom_completed(Callback<void(int correlation_id)> handler)
    {
        on_zoom_completed_ = std::move(handler);
    }
    /// Edit `args.animation` before an animated scroll plays, to change the duration or the path.
    void on_scroll_animation_starting(Callback<void(ScrollAnimationStarting& args)> handler)
    {
        on_scroll_animation_starting_ = std::move(handler);
    }
    /// Edit `args.animation` before an animated zoom plays.
    void on_zoom_animation_starting(Callback<void(ZoomAnimationStarting& args)> handler)
    {
        on_zoom_animation_starting_ = std::move(handler);
    }
    /// True: measuring touches only the content, the view's own sizes and its scroll bars' sizes.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }
    /// Only after set_focusable(true), and while enabled.
    [[nodiscard]] bool focusable() const override
    {
        return focusable_ && is_enabled();
    }
    /// Whether the view itself (not its content) holds keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// Visits the content, if any. The scroll bars are drawn by the view and are not children.
    /// Tab reaches the view itself first when it is a tab stop, then what is focusable in the
    /// content.
    void for_each_child(ChildVisitor visit) const override
    {
        if (child_)
        {
            visit(*child_);
        }
    }
    /// Scrolls a newly focused descendant into the viewport (Element::bring_into_view), so Tab
    /// never moves focus to a part of the content that is scrolled out of view.
    Rect reveal_child(const Rect& area, const BringIntoViewOptions& options) override;

    // ---- automation ----
    /// Reports the Pane role to accessibility tools.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Pane;
    }

protected:
    // ---- Element ----
    /// Measures the child without a limit along the axes content_orientation() leaves free. The
    /// view itself takes all it is offered, and the child's size only on an axis offered 0.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Derives the extent, clamps the offsets, arranges the child at them and runs any request
    /// held for the first layout.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// The background, the child clipped to the viewport and zoomed, then the scroll bars.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Scroll bar drags and steps first; everything else goes to the child, mapped through the
    /// offsets and the zoom.
    bool pointer_override(const PointerEvent& e) override;
    /// Scrolls at once, or zooms with Ctrl. Returns false when there was nothing to scroll that
    /// way, so a scroller around it may take the turn, depending on the chain mode.
    bool                              wheel_override(const PointerEvent& e) override;
    [[nodiscard]] Point               map_to_children(Point p) const override;
    [[nodiscard]] std::optional<Rect> children_hit_clip() const override;
    [[nodiscard]] bool                covers_children_at(Point p) const override;
    /// Advances the scroll bars' fades, any scroll or zoom animation and any inertia, then ticks
    /// the content.
    void tick_override(Duration delta) override;


private:
    // Claims an arrow key while focused and able to scroll that way; otherwise it moves focus.
    [[nodiscard]] bool takes_arrow_key(Key key) const;
    // The library's own controls that move the view at once, inside their own call, with no
    // view-change request.
    friend class ListViewBase;
    friend class AnnotatedScrollBarControl;
    friend struct Scroll;

    // Sets the vertical offset at once (no animation), clamped to the current content range.
    void jump_to_vertical_offset(float offset);
    // Sets the horizontal offset at once (no animation), clamped to the current content range.
    void jump_to_horizontal_offset(float offset);
    // Sets the zoom factor at once (no animation), clamped to the zoom range, about the
    // viewport's center.
    void jump_to_zoom_factor(float zoom_factor);

    // A request waiting for the first layout (see scroll_to()).
    struct Deferred
    {
        enum class Kind
        {
            ScrollTo,
            ScrollBy,
            Velocity,
            ZoomTo,
            ZoomBy,
        };
        Kind                 kind;
        int                  id;
        Point                value;     // offsets, deltas or velocity; zoom in x
        std::optional<Point> extra;     // decay rate or center point
        ScrollAnimationMode  animation; // scroll/zoom options
    };

    struct OffsetsAnimation
    {
        ScrollAnimation animation;
        Point           start;
        double          elapsed_ms = 0.0;
        int             id         = 0;
    };
    struct ZoomAnimationState
    {
        ZoomAnimation animation;
        float         start_zoom = 1.0f;
        Point         center{};
        double        elapsed_ms = 0.0;
        int           id         = 0;
    };
    struct Inertia
    {
        Point velocity{};
        Point decay{};
        int   id = 0;
    };

    [[nodiscard]] bool scrollable_horizontally() const
    {
        return scrollable_width() > 0.0f;
    }
    [[nodiscard]] bool scrollable_vertically() const
    {
        return scrollable_height() > 0.0f;
    }
    [[nodiscard]] bool user_can_scroll_horizontally() const
    {
        return computed_horizontal_scroll_mode() == ScrollMode::Enabled && scrollable_horizontally();
    }
    [[nodiscard]] bool user_can_scroll_vertically() const
    {
        return computed_vertical_scroll_mode() == ScrollMode::Enabled && scrollable_vertically();
    }

    // Where the content's (0,0) sits inside the viewport when it is smaller than it, per axis.
    [[nodiscard]] Point alignment_offset() const;
    // Content coordinates (the child's arranged space) <-> window coordinates.
    [[nodiscard]] Point        content_to_view(Point p) const;
    [[nodiscard]] Point        view_to_content(Point p) const;
    [[nodiscard]] PointerEvent to_content(const PointerEvent& e) const;

    // The two bars' viewports: each stops short of the other's corner when both show.
    [[nodiscard]] Rect vertical_bar_viewport() const;
    [[nodiscard]] Rect horizontal_bar_viewport() const;

    // Clamp both offsets, arrange the child, and report a view change if anything moved.
    void reflow();
    // Apply offsets (and optionally a zoom factor) directly: the one place the view changes.
    void set_view(float horizontal, float vertical, OptionalFloat zoom = std::nullopt);
    void set_state(ScrollInteractionState state);

    int next_id()
    {
        return ++last_id_;
    }
    // Stop whatever animation or inertia is running, completing its request.
    void interrupt();
    void start_scroll(int id, Point target, ScrollAnimationMode mode);
    void start_zoom(int id, float target, std::optional<Point> center, ScrollAnimationMode mode);
    void start_velocity(int id, Point velocity, std::optional<Point> decay);
    void run(const Deferred& request);

    // A wheel turn over the viewport: scroll, or zoom with Ctrl. False when this view had
    // nothing to do with it, so an enclosing scroller may take it.
    bool wheel(const PointerEvent& e);

    // Scrolls for a key press; returns whether the key scrolled.
    bool               scroll_for_key(Key key);
    [[nodiscard]] bool can_scroll(Key key) const;

    std::unique_ptr<Element> child_;

    ScrollContentOrientation content_orientation_              = ScrollContentOrientation::Vertical;
    ScrollMode               horizontal_scroll_mode_           = ScrollMode::Auto;
    ScrollMode               vertical_scroll_mode_             = ScrollMode::Auto;
    ScrollZoomMode           zoom_mode_                        = ScrollZoomMode::Disabled;
    ScrollChainMode          horizontal_scroll_chain_mode_     = ScrollChainMode::Auto;
    ScrollChainMode          vertical_scroll_chain_mode_       = ScrollChainMode::Auto;
    ScrollBarVisibility      horizontal_scroll_bar_visibility_ = ScrollBarVisibility::Auto;
    ScrollBarVisibility      vertical_scroll_bar_visibility_   = ScrollBarVisibility::Auto;
    float                    min_zoom_factor_                  = DefaultMinZoomFactor;
    float                    max_zoom_factor_                  = DefaultMaxZoomFactor;
    Paint                    background_;
    bool                     focusable_   = false;
    bool                     pan_on_drag_ = false;

    // A pan_on_drag() drag in progress: where it was pressed and the offsets then, in this
    // control's layout coordinates, and the axes it moves.
    struct Pan
    {
        Point start;
        Point offsets;
        bool  horizontal = false;
        bool  vertical   = false;
    };
    std::optional<Pan> pan_;
    // The axes a pan would move now: (horizontal, vertical).
    [[nodiscard]] std::pair<bool, bool> pannable_axes() const;

    float horizontal_offset_ = 0.0f;
    float vertical_offset_   = 0.0f;
    float zoom_factor_       = 1.0f;

    // The child's desired size from the most recent measure(), and the extent arrange()
    // derived from it (stretched to the viewport where the child stretches).
    mutable Size child_measured_{};
    // The gutters an era's permanent scroll bars take from the viewport, decided in measure; 0
    // under the overlay bars and while a bar does not show.
    mutable float vertical_gutter_   = 0.0f;
    mutable float horizontal_gutter_ = 0.0f;
    mutable Size  gutter_room_{}; // the room the gutters were decided in
    // The part of bounds() the content shows in: all of it, less the era bars' gutters.
    [[nodiscard]] Rect viewport_rect() const
    {
        return Rect{bounds().x,
                    bounds().y,
                    (std::max)(0.0f, bounds().width - vertical_gutter_),
                    (std::max)(0.0f, bounds().height - horizontal_gutter_)};
    }
    Size extent_{};
    bool laid_out_ = false;

    ScrollBarOverlay vertical_bar_{Orientation::Vertical};
    ScrollBarOverlay horizontal_bar_{Orientation::Horizontal};
    bool             show_scrollbar_ = true;

    ScrollInteractionState            state_   = ScrollInteractionState::Idle;
    int                               last_id_ = 0;
    std::vector<Deferred>             deferred_;
    std::optional<OffsetsAnimation>   offsets_animation_;
    std::optional<ZoomAnimationState> zoom_animation_;
    std::optional<Inertia>            inertia_;

    Callback<void()>                         on_view_changed_;
    Callback<void()>                         on_extent_changed_;
    Callback<void()>                         on_state_changed_;
    Callback<void(int)>                      on_scroll_completed_;
    Callback<void(int)>                      on_zoom_completed_;
    Callback<void(ScrollAnimationStarting&)> on_scroll_animation_starting_;
    Callback<void(ZoomAnimationStarting&)>   on_zoom_animation_starting_;

protected:
    void enabled_changed() override;
    /// While focused, the arrow keys scroll 15 % of the viewport, Page Up and Page Down a whole
    /// viewport, and Home and End go to either end.
    bool key_override(const KeyEvent& e) override;
    /// Called by Window as focus moves; redraws.
    void focus_changed(bool focused) override;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.scroll = this;
    }
    // horizontal_offset() as a percentage of scrollable_width(); -1 with nothing to scroll.
    [[nodiscard]] float scroll_horizontal_percent() const override
    {
        const float range = scrollable_width();
        return range > 0.0f ? horizontal_offset_ / range * 100.0f : -1.0f;
    }
    // vertical_offset() as a percentage of scrollable_height(); 0 with nothing to scroll.
    [[nodiscard]] float scroll_vertical_percent() const override
    {
        const float range = scrollable_height();
        return range > 0.0f ? (vertical_offset_ / range) * 100.0f : 0.0f;
    }
    // Scrolls to the given percentages at once, with no animation; a negative percentage
    // leaves that axis alone.
    bool set_scroll_percent(float horizontal, float vertical) override;
};

/// A scrollable, zoomable viewport around one child. Builds a ui::ScrollControl. The modes and
/// bar visibilities can be bound.
///
/// Give it a bounded size, such as a `height`, a star grid row or a window, or it grows to its
/// content and has nothing to scroll. `horizontal_offset`, `vertical_offset` and `zoom_factor`
/// bind two-way: the user's scrolling writes them, and writing them moves the view at once.
/// Animated scrolling goes through `.control`: scroll_to(), scroll_by(), zoom_to().
///
/// Example:
/// @code
/// const std::string       long_text = "A text much taller than the window...";
/// ui::Property<float>     position{0.0f};
/// ui::Control<ui::Scroll> scroller;
///
/// window.set_content(ui::VerticalStack{.content = {
///     ui::Button{.text = "Back to top", .on_click = [&] { scroller->scroll_to(0.0f, 0.0f); }},
///     ui::Text{.text = ui::bind(position, [](float y) { return std::format("{:.0f} DIPs down", y); })},
///     ui::Scroll{
///         .vertical_offset = position, // follows the user's scrolling; writing it jumps
///         .zoom_mode       = ui::ScrollZoomMode::Enabled,
///         .height          = 400.0f,
///         .control         = scroller,
///         .content         = ui::Text{.text = long_text, .wrapping = ui::TextWrapping::Wrap},
///     },
/// }});
/// @endcode
struct Scroll
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Scroll>` points at.
    using control_type = ScrollControl;

    // ---- properties ----
    /// How far the view is scrolled right, in zoomed DIPs. Two-way: the user's scrolling writes
    /// it, and a write moves the view at once, where scroll_to() animates. Clamped to the
    /// scrollable range, and the clamped value is written back. A write before the first layout
    /// lands once the content has a size.
    TwoWayField<float> horizontal_offset{0.0f};
    /// How far the view is scrolled down, in zoomed DIPs. Two-way, as `horizontal_offset`.
    TwoWayField<float> vertical_offset{0.0f};
    /// The zoom factor. Two-way, as `horizontal_offset`. A write zooms at once about the
    /// viewport's center, clamped to the zoom range below, where zoom_to() animates.
    TwoWayField<float> zoom_factor{1.0f};
    /// The smallest zoom the user can reach.
    Binding<float> min_zoom_factor{ScrollControl::DefaultMinZoomFactor};
    /// The largest zoom the user can reach.
    Binding<float> max_zoom_factor{ScrollControl::DefaultMaxZoomFactor};
    /// The axis the content is free to grow along (Vertical, Horizontal, Both or None). The other
    /// axis is measured at the viewport's size.
    Binding<ScrollContentOrientation> content_orientation{ScrollContentOrientation::Vertical};
    /// Whether user input may scroll this axis.
    Binding<ScrollMode> horizontal_scroll_mode{ScrollMode::Auto};
    /// Whether user input may scroll this axis.
    Binding<ScrollMode> vertical_scroll_mode{ScrollMode::Auto};
    /// Enabled lets Ctrl+wheel zoom, 10 % a notch, between the zoom factors.
    Binding<ScrollZoomMode> zoom_mode{ScrollZoomMode::Disabled};
    /// Auto shows the bar only when there is something to scroll.
    Binding<ScrollBarVisibility> horizontal_scroll_bar_visibility{ScrollBarVisibility::Auto};
    /// Auto shows the bar only when there is something to scroll.
    Binding<ScrollBarVisibility> vertical_scroll_bar_visibility{ScrollBarVisibility::Auto};
    /// The viewport itself takes focus, for keyboard scrolling of content with nothing focusable in
    /// it. Off by default.
    bool focusable = false;
    /// Whether a wheel turn goes on to the scroller around it (ui::ScrollChainMode). Auto passes
    /// on a turn this view is at the end of, and Never keeps it.
    Binding<ScrollChainMode> horizontal_scroll_chain_mode{ScrollChainMode::Auto};
    Binding<ScrollChainMode> vertical_scroll_chain_mode{ScrollChainMode::Auto}; ///< As above.
    /// Background behind the content: a Color, or a ThemeColor read from the theme. Empty, the
    /// default, draws none.
    Binding<Paint> background{};
    /// Dragging the content with the left button moves the view (ScrollControl::pan_on_drag()).
    /// Off by default.
    Binding<bool> pan_on_drag{false};

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
    Binding<bool>                     enabled{true}; ///< False takes no scrolling input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// An offset or the zoom factor changed.
    Callback<void()> on_view_changed{};
    /// A scroll finished, with the correlation id scroll_to()/scroll_by() returned.
    Callback<void(int)> on_scroll_completed{};
    /// A zoom finished, with the correlation id zoom_to()/zoom_by() returned.
    Callback<void(int)> on_zoom_completed{};
    /// Edit the args' animation to change its duration or path.
    Callback<void(ScrollAnimationStarting&)> on_scroll_animation_starting{};
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
    ControlOut<Scroll> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The scrolled content.
    Spec content{};

    /// Consumes the spec and returns the built ui::ScrollControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
