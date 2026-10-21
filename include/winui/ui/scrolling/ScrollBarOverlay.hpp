// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <optional>

namespace ui
{

/// The overlay scroll bar a scrolling Element paints over its own viewport. It is a part of
/// its owner, not an Element itself.
///
/// It holds only interaction and animation state. The owner passes the viewport, the content
/// length and the current offset into every call, and applies the offset handle_pointer() hands
/// back. At rest it is a thin thumb flush with the edge. Hovered or dragged, it widens and shows
/// a track and two step buttons. After 2 s without activity it fades away. Scroll and
/// VirtualList use it, and a custom scroller uses it the same way.
///
/// Example:
/// @code
/// class Scroller final : public ui::Element
/// {
/// public:
///     ui::Size measure_override(ui::Size available, const ui::MeasureContext&) const override
///     {
///         return available;
///     }
///     bool pointer_override(const ui::PointerEvent& e) override
///     {
///         float offset = offset_;
///         if (bar_.handle_pointer(e, bounds(), content_height_, offset))
///         {
///             offset_ = offset; // clamp and re-arrange here
///             mark_dirty(ui::DirtyFlags::Visual);
///             return true; // claimed: do not forward to the content
///         }
///         return false; // ... or forward to the content ...
///     }
///     void tick_override(ui::Duration delta) override
///     {
///         if (bar_.update(delta))
///         {
///             mark_dirty(ui::DirtyFlags::Visual);
///         }
///         if (const std::optional<ui::Duration> wait = bar_.fade_wait())
///         {
///             request_tick_in(*wait); // wake once for the fade
///         }
///     }
///     void render_override(ui::Renderer& renderer, const ui::RenderContext& context) const override
///     {
///         // ... render the clipped content first ...
///         bar_.render(renderer, theme, bounds(), content_height_, offset_);
///     }
///
/// private:
///     ui::ScrollBarOverlay bar_{ui::Orientation::Vertical};
///     float                   offset_         = 0.0f;
///     float                   content_height_ = 1000.0f;
/// };
/// @endcode
class ScrollBarOverlay
{
public:
    /// A bar for one axis, collapsed and idle.
    explicit ScrollBarOverlay(Orientation orientation = Orientation::Vertical) : orientation_(orientation)
    {
    }

    /// The axis given at construction.
    [[nodiscard]] Orientation orientation() const
    {
        return orientation_;
    }

    /// Whether there is anything to scroll at all, which decides whether the bar draws and takes
    /// input. This static form checks the vertical axis, and scrollable() below checks this bar's
    /// own axis.
    [[nodiscard]] static bool is_scrollable(const Rect& viewport, float content_length)
    {
        return content_length > viewport.height;
    }
    /// Whether `content_length` (DIPs along this bar's axis) exceeds the viewport along it.
    [[nodiscard]] bool scrollable(const Rect& viewport, float content_length) const
    {
        return content_length > (orientation_ == Orientation::Vertical ? viewport.height : viewport.width);
    }

    /// ScrollBarVisibility::Visible: draw the bar even with nothing to scroll. Its thumb then
    /// fills the track, and it takes no input. Off by default, which is Auto.
    [[nodiscard]] bool always_visible() const
    {
        return always_visible_;
    }
    /// The owner redraws. This class marks nothing dirty.
    void set_always_visible(bool always)
    {
        always_visible_ = always;
    }

    /// Takes the theme's kind of bar: this overlay, or a permanent bar, which takes room of its
    /// own at full thickness and never fades. The
    /// owner calls it in measure, where it has the theme, before it lays its content out.
    void apply_theme(const Theme& theme) const;
    /// The room the bar takes from its scroller's viewport while it shows, in DIPs: the era bar's
    /// thickness, or 0 for the overlay, which draws over the content.
    [[nodiscard]] float gutter() const
    {
        return gutter_;
    }

    /// The area that takes the bar's input: the rightmost, full-height, 12-DIP slice of the
    /// viewport, whether the bar is expanded or not. A click or hover must land in it to reach
    /// the bar. It spans the step buttons and the thumb and track between them. For a horizontal
    /// bar, it is the bottom row.
    [[nodiscard]] Rect band_bounds(const Rect& viewport) const;

    /// The up (top) step button, or the left one for a horizontal bar: a 12-DIP square at that
    /// end of the band, which scrolls back a small step. Both step buttons always take their
    /// space, however far the expand animation is, and the thumb's track stops short of them.
    /// Only their drawing fades with the animation.
    [[nodiscard]] Rect up_button_bounds(const Rect& viewport) const;
    /// The down (bottom) step button, or the right one for a horizontal bar; see
    /// up_button_bounds().
    [[nodiscard]] Rect down_button_bounds(const Rect& viewport) const;

    /// Where the thumb is drawn for `offset`, the DIPs scrolled into `content_length`. Its length
    /// is the viewport's share of the track between the step buttons, at least 30 DIPs, at the
    /// thickness the animation has reached.
    [[nodiscard]] Rect thumb_bounds(const Rect& viewport, float content_length, float offset) const;

    /// True while hovered or dragging: the expanded look (thicker thumb, visible track) instead of
    /// the collapsed one (thin thumb, no track).
    [[nodiscard]] bool expanded() const
    {
        return hovered_ || dragging_;
    }
    /// True while the thumb is being dragged.
    [[nodiscard]] bool dragging() const
    {
        return dragging_;
    }

    /// The owner passes every pointer event except wheel turns here first. Returns true if the
    /// bar took the event, and the owner must then not forward it to its content. When it
    /// returns true and `offset` differs from what the owner passed in, the owner applies it the
    /// way it applies a wheel turn, through its own offset setter, which clamps and arranges again.
    bool handle_pointer(const PointerEvent& e, const Rect& viewport, float content_length, float& offset);

    /// Advances the expand and collapse animation by `delta`. Call it once per frame from the
    /// owner's tick_override(). Returns true while an animation is still running. The owner then
    /// marks itself visually dirty so render() is called again.
    [[nodiscard]] bool update(Duration delta);

    /// How long until the bar starts to fade by itself. Set while it shows and waits out its 2 s
    /// without activity, and empty while hovered, dragged, fading or faded. The owner passes it to
    /// Element::request_tick_in() from tick_override(), so an otherwise idle window wakes once for
    /// the fade.
    [[nodiscard]] std::optional<Duration> fade_wait() const;

    /// Restarts the 2 s idle clock after which the bar fades, for activity handle_pointer() can't
    /// see, such as a wheel turn the owner scrolled with, whether or not the pointer is over the
    /// band. Scroll and VirtualList call it from their wheel handling. Hovering and dragging keep
    /// the clock reset by themselves.
    void notify_activity()
    {
        idle_seconds_ = 0.0f;
    }

    /// Draws nothing when !is_scrollable(viewport, content_length). Call it after the owner has
    /// drawn its clipped content. The bar draws on top, unclipped.
    void render(Renderer& renderer, const Theme& theme, const Rect& viewport, float content_length, float offset) const;

private:
    // Swaps x and y, so the horizontal bar can run the vertical one's code. Its own inverse.
    [[nodiscard]] Rect  to_axis(const Rect& r) const;
    [[nodiscard]] Point to_axis(Point p) const;

    // The vertical implementations everything above forwards to, in axis space.
    [[nodiscard]] Rect band_bounds_v(const Rect& viewport) const;
    [[nodiscard]] Rect up_button_bounds_v(const Rect& viewport) const;
    [[nodiscard]] Rect down_button_bounds_v(const Rect& viewport) const;
    [[nodiscard]] Rect thumb_bounds_v(const Rect& viewport, float content_length, float offset) const;

    [[nodiscard]] float thickness() const;
    [[nodiscard]] float button_length(const Rect& viewport) const;
    [[nodiscard]] float thumb_length(const Rect& viewport, float content_length) const;
    void                render_gutter(Renderer&    renderer,
                                      const Theme& theme,
                                      const Rect&  viewport,
                                      float        content_length,
                                      float        offset,
                                      bool         enabled) const;
    // The other eras' bars, on the same geometry (render_gutter() dispatches to them).
    void render_luna(Renderer&    renderer,
                     const Theme& theme,
                     const Rect&  viewport,
                     float        content_length,
                     float        offset,
                     bool         enabled) const;
    void render_classic(Renderer&    renderer,
                        const Theme& theme,
                        const Rect&  viewport,
                        float        content_length,
                        float        offset,
                        bool         enabled) const;
    // Where a held page click is, between the arrows and the thumb (axis space); empty for none.
    [[nodiscard]] Rect  pressed_page_rect(const Rect& viewport, float content_length, float offset) const;
    [[nodiscard]] float thumb_top(const Rect& viewport, float content_length, float offset, float length) const;
    [[nodiscard]] float animation_progress() const; // 0.0 = fully collapsed, 1.0 = fully expanded
    // viewport.height / StepRatio, the same ratio as AnnotatedScrollBar's automatic small change.
    [[nodiscard]] float       step_amount(const Rect& viewport) const;
    [[nodiscard]] ButtonState up_button_state() const;
    [[nodiscard]] ButtonState down_button_state() const;

    Orientation   orientation_    = Orientation::Vertical;
    bool          always_visible_ = false;
    mutable float gutter_    = 0.0f; // apply_theme(), from a const measure: the era bar's thickness, 0 for the overlay
    mutable float min_thumb_ = 0.0f; // apply_theme(): the era thumb's shortest length (0: half the thickness)
    mutable float fixed_thumb_ = 0.0f; // apply_theme(): an era's fixed scroll box length, 0 for a proportional one

    bool  hovered_              = false;
    bool  hovered_thumb_        = false;
    bool  dragging_             = false;
    int   pressed_page_         = 0; // a page click held before (-1) or after (+1) the thumb
    float drag_start_pointer_y_ = 0.0f;
    float drag_start_offset_    = 0.0f;

    bool hovered_up_ = false, pressed_up_ = false;
    bool hovered_down_ = false, pressed_down_ = false;

    // Reversing direction mid-animation eases on from wherever progress_ sits instead of snapping.
    float progress_        = 0.0f;
    bool  target_expanded_ = false;
    float state_time_      = 0.0f;
    float progress_start_  = 0.0f;

    // Idle auto-hide, independent of progress_: presence_ only moves while collapsed.
    float idle_seconds_ = 0.0f;
    float presence_     = 1.0f; // 0 = fully faded, 1 = visible
};

} // namespace ui
