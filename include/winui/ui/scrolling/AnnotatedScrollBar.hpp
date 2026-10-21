// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ui
{

class AnnotatedScrollBarDetailSurface; // defined in AnnotatedScrollBar.cpp

/// What caused an AnnotatedScrollBar scroll request. The top step button scrolls up, toward a
/// smaller offset, and the bottom one scrolls down.
enum class AnnotatedScrollBarScrollingEventKind
{
    Click,           ///< A click on the rail, Home or End, or an accessibility client setting the value.
    Drag,            ///< Dragging along the rail.
    IncrementButton, ///< The top step button or Up arrow: scrolls toward 0.
    DecrementButton, ///< The bottom step button or Down arrow: scrolls toward maximum().
};

/// What `on_scrolling` gets before a scroll request is raised: where it goes and what caused it.
/// The handler returns ui::Cancel to drop the request.
struct AnnotatedScrollBarScrollingArgs
{
    double                               offset = 0.0;                                         ///< The target offset.
    AnnotatedScrollBarScrollingEventKind kind   = AnnotatedScrollBarScrollingEventKind::Click; ///< What caused it.
};

/// A caption pinned beside a ui::AnnotatedScrollBar's rail at the position of a scroll offset.
/// Annotations that would overlap a previous one are hidden.
struct AnnotatedScrollBarAnnotation
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// Caption drawn right-aligned in the annotations column.
    std::string text;
    /// Scroll offset (DIPs of the target's content) the annotation marks.
    double scroll_offset = 0.0;

    /// Memberwise equality.
    friend bool operator==(const AnnotatedScrollBarAnnotation&, const AnnotatedScrollBarAnnotation&) = default;
};

/// A vertical scroll bar with a rail you click or drag to jump, up and down step buttons, and
/// offset annotations that stay visible and hide when they collide. It is for moving through
/// long grouped content, such as a photo timeline or an alphabetical list.
///
/// Attach it to a Scroll with set_target(). It then follows the view's range, viewport and
/// offset, scrolls it right away, and hides the view's own scroll bar while attached. For any
/// other scrollable, leave the target unset, call set_maximum(), set_viewport_size() and
/// set_value() yourself, and handle on_scroll_to_requested() and on_scroll_by_requested(). The
/// top step button is `AnnotatedScrollBarScrollingEventKind::IncrementButton` and scrolls up,
/// toward 0. on_detail_text_requested() supplies the floating tooltip shown over the rail while
/// hovering or dragging. The arrow keys step, and Home and End jump to the ends.
///
/// Example:
/// @code
/// const std::string                   long_text = "A text much taller than the window...";
/// ui::Control<ui::Scroll>             view;
/// ui::Control<ui::AnnotatedScrollBar> bar;
///
/// window.set_content(ui::HorizontalStack{
///     .content = {
///         ui::Scroll{.max_height = 500.0f,
///                    .control    = view,
///                    .content    = ui::Text{.text = long_text, .wrapping = ui::TextWrapping::Wrap}},
///         ui::AnnotatedScrollBar{
///             .annotations              = {{.text = "2024", .scroll_offset = 0.0},
///                                          {.text = "2025", .scroll_offset = 2400.0},
///                                          {.text = "2026", .scroll_offset = 5100.0}},
///             .max_height               = 500.0f,
///             .on_detail_text_requested = [](double offset) { return std::format("{:.0f} px", offset); },
///             .control                  = bar,
///         },
///     },
/// });
/// bar->set_target(view); // bar now drives and mirrors the Scroll
///
/// bar->on_scrolling([](const ui::AnnotatedScrollBarScrollingArgs& args) {
///     const bool far_drag = args.kind == ui::AnnotatedScrollBarScrollingEventKind::Drag && args.offset > 9000.0;
///     return far_drag ? ui::Cancel : ui::Proceed;
/// });
/// @endcode
class AnnotatedScrollBarControl final : public Element, private RangeValueProvider
{
public:
    /// An unattached bar with an empty range and no annotations.
    AnnotatedScrollBarControl() = default;

    // ---- range ----
    /// The largest scroll offset, in DIPs. The minimum is always 0, as for Scroll. With a target
    /// attached, the target's current scrollable_height().
    [[nodiscard]] double maximum() const;
    /// Sets the range for an unattached bar and redraws. Ignored while target() is attached,
    /// since maximum() then reads target()->scrollable_height().
    void set_maximum(double maximum);

    /// The visible extent in DIPs, which sizes the thumb and the automatic small change. With a
    /// target attached, the target's current height.
    [[nodiscard]] double viewport_size() const;
    /// Sets the viewport extent for an unattached bar and redraws. Ignored while target() is
    /// attached, since viewport_size() then reads target()->bounds().height.
    void set_viewport_size(double viewport_size);

    /// The current scroll offset in DIPs, 0 to maximum(). With a target attached, its current
    /// vertical_offset().
    [[nodiscard]] double value() const;
    /// Moves the thumb and annotations only, and raises no scroll request. Ignored while
    /// target() is attached, since value() then reads target()->vertical_offset().
    void set_value(double value);

    // ---- target: the built-in Scroll adapter ----
    /// The Scroll this bar drives, or null, in which case the manual range values apply.
    [[nodiscard]] ScrollControl* target() const
    {
        return target_;
    }
    /// Attaches the bar to `target`, which must outlive this control. The bar does not own it.
    /// Installs the bar's own on_scroll_to_requested() and on_scroll_by_requested() handlers,
    /// replacing any set before. While attached, the target's own scroll bar is hidden
    /// (ScrollControl::set_show_scrollbar(false)), and it comes back when the bar detaches or
    /// moves to another target. Pass nullptr to detach. That clears the two handlers and goes back
    /// to the manual maximum(), viewport_size() and value(). Set on_scroll_to_requested() and
    /// on_scroll_by_requested() again afterward to drive another kind of scrollable.
    void set_target(ScrollControl* target);

    // ---- annotations ----
    /// The annotations, in the order given.
    [[nodiscard]] const std::vector<AnnotatedScrollBarAnnotation>& annotations() const
    {
        return annotations_;
    }
    /// Replaces the annotations, then lays out again, since the annotations column fits the
    /// widest one, and redraws.
    void set_annotations(std::vector<AnnotatedScrollBarAnnotation> annotations);

    // ---- small change ----
    /// How far a step button or an arrow key moves value(), in DIPs. 0, the default, means
    /// automatic: an eighth of viewport_size().
    [[nodiscard]] double small_change() const
    {
        return small_change_;
    }
    /// Sets the step size in DIPs; negative values are taken as 0 (auto).
    void set_small_change(double small_change)
    {
        small_change_ = (std::max)(0.0, small_change);
    }


    // ---- events ----
    /// Raised before every scroll request, whether from a click, a drag, a step button, an arrow
    /// key, or Home or End. Returning ui::Cancel drops the request.
    void on_scrolling(Callback<Decision(const AnnotatedScrollBarScrollingArgs&)> handler)
    {
        on_scrolling_ = std::move(handler);
    }
    /// Text for the floating detail tooltip shown while hovering over or dragging the rail, given
    /// the offset under the pointer. No tooltip opens while this is unset.
    void on_detail_text_requested(Callback<std::string(double offset)> handler)
    {
        on_detail_text_requested_ = std::move(handler);
    }
    /// Request to scroll to `offset`, in DIPs. set_target() installs its own handler. Set it
    /// yourself to drive a scrollable that is not a Scroll.
    void on_scroll_to_requested(Callback<void(double offset)> handler)
    {
        on_scroll_to_requested_ = std::move(handler);
    }
    /// Low-level request to scroll by `delta` DIPs (step buttons, arrow keys); negative is up.
    void on_scroll_by_requested(Callback<void(double delta)> handler)
    {
        on_scroll_by_requested_ = std::move(handler);
    }

    // ---- focus ----
    /// Whether the bar has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// Focusable while enabled.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }

    // ---- automation ----
    /// Exposed to accessibility tools as a Slider, with a range value, since it shows and sets a
    /// bounded scroll value.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Slider;
    }

protected:
    // ---- layout ----
    /// The labels column, as wide as the widest label, plus the fixed-width rail column. The
    /// height is what's offered.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;

    // ---- input ----
    /// Clicks and drags on the rail jump, the step buttons step, and hovering over the rail asks
    /// for the detail text.
    bool pointer_override(const PointerEvent& e) override;

    // ---- render ----
    /// Draws the rail, thumb, step buttons, visible labels and focus ring.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    // Up/Down scroll the target; this bar has no horizontal axis. Claimed so Window's
    // directional focus navigation leaves them alone.
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowUp || key == Key::ArrowDown;
    }
    // The detail label: a popup above the rail position under the pointer while it shows
    // (hovering or dragging the rail with a handler set).
    void sync_popup();
    friend class AnnotatedScrollBarDetailSurface;

    [[nodiscard]] bool can_step_up() const
    {
        return is_enabled() && value() > 0.0;
    }
    [[nodiscard]] bool can_step_down() const
    {
        return is_enabled() && value() < maximum();
    }
    [[nodiscard]] ButtonState up_button_state() const;
    [[nodiscard]] ButtonState down_button_state() const;

    [[nodiscard]] Rect rail_column_bounds() const; // the fixed-width right column: buttons + rail + thumb
    [[nodiscard]] Rect up_button_bounds() const;
    [[nodiscard]] Rect down_button_bounds() const;
    [[nodiscard]] Rect rail_bounds() const; // click/drag area, between the two buttons
    [[nodiscard]] Rect labels_column_bounds() const;
    [[nodiscard]] Rect thumb_bounds() const;

    // (rail height - thumb height) / range * (1 - viewport / (viewport + range)). Positions
    // annotations only, not the thumb.
    [[nodiscard]] double label_offset_factor() const;
    [[nodiscard]] float  offset_to_rail_y(double offset) const; // labels only

    // Plain proportional travel over the whole free rail. The label factor left the thumb short
    // of the rail's end at maximum(), which reads as broken.
    [[nodiscard]] double thumb_travel_factor() const;
    [[nodiscard]] float  thumb_offset_to_rail_y(double offset) const; // thumb_bounds() / the detail label
    [[nodiscard]] double rail_y_to_offset(float y) const;             // pointer -> offset; matches the thumb 1:1

    [[nodiscard]] TextStyle         label_text_style(const Theme& theme) const;
    [[nodiscard]] std::vector<bool> visible_labels() const; // hides colliding and out-of-bounds annotations

    void step(AnnotatedScrollBarScrollingEventKind kind); // step buttons and arrow keys
    void scroll_to(double                               offset,
                   AnnotatedScrollBarScrollingEventKind kind); // click, drag, Home/End

    void request_detail_label(double offset);
    void close_detail_label();

    ScrollControl* target_ = nullptr;

    double maximum_       = 0.0;
    double viewport_size_ = 0.0;
    double value_         = 0.0;
    double small_change_  = 0.0;

    std::vector<AnnotatedScrollBarAnnotation> annotations_;
    // Cached by measure(): render() has no TextMeasurer, and visible_labels() needs each label's
    // height for its collision test.
    mutable std::vector<float> label_heights_;

    bool hovered_rail_ = false;
    bool dragging_     = false;
    bool hovered_up_ = false, pressed_up_ = false;
    bool hovered_down_ = false, pressed_down_ = false;
    // Last pointer-derived offset over the rail. Anchors the detail popup and, while dragging_,
    // is the live scroll target.
    double hover_offset_ = 0.0;

    Callback<Decision(const AnnotatedScrollBarScrollingArgs&)> on_scrolling_;
    Callback<std::string(double)>                              on_detail_text_requested_;
    Callback<void(double)>                                     on_scroll_to_requested_;
    Callback<void(double)>                                     on_scroll_by_requested_;

    bool                     detail_open_ = false;
    std::string              detail_text_;
    std::unique_ptr<Element> detail_surface_;

protected:
    void enabled_changed() override;
    /// Focus changes redraw the focus ring.
    void focus_changed(bool focused) override;
    /// Up and Down step by small_change(). Home and End jump to 0 and maximum().
    bool key_override(const KeyEvent& e) override;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.range_value = this;
    }
    // value().
    [[nodiscard]] double range_value() const override
    {
        return value();
    }
    // Always 0.
    [[nodiscard]] double range_minimum() const override
    {
        return 0.0;
    }
    // maximum().
    [[nodiscard]] double range_maximum() const override
    {
        return maximum();
    }
    // The small_change() setting (0 while automatic).
    [[nodiscard]] double range_small_change() const override
    {
        return small_change_;
    }
    // Scrolls to `value` as a Click would (on_scrolling() can cancel); false while disabled.
    bool set_range_value(double value) override
    {
        if (!is_enabled())
        {
            return false;
        }
        scroll_to(value, AnnotatedScrollBarScrollingEventKind::Click);
        return true;
    }
    [[nodiscard]] bool range_read_only() const override
    {
        return false;
    }
};

/// A vertical scroll bar with step buttons, a rail you click or drag to jump, and offset
/// annotations that hide when they collide. Builds a ui::AnnotatedScrollBarControl. There is no
/// `.target` field. Call `set_target()` on `.control` once both the bar and the
/// ui::ScrollControl exist.
///
/// Example:
/// @code
/// ui::Spec make_photo_grid(); // the app's own content
/// ui::Control<ui::Scroll>             view;
/// ui::Control<ui::AnnotatedScrollBar> bar;
///
/// window.set_content(ui::HorizontalStack{.content = {
///     ui::Scroll{.max_height = 500.0f, .control = view, .content = make_photo_grid()},
///     ui::AnnotatedScrollBar{
///         .annotations              = {{.text = "2024", .scroll_offset = 0.0},
///                                      {.text = "2025", .scroll_offset = 2400.0},
///                                      {.text = "2026", .scroll_offset = 5100.0}},
///         .max_height               = 500.0f,
///         .on_detail_text_requested = [](double offset) { return std::format("{:.0f} px", offset); },
///         .control                  = bar,
///     },
/// }});
/// bar->set_target(view); // the bar now drives and mirrors the Scroll
/// @endcode
struct AnnotatedScrollBar
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<AnnotatedScrollBar>` points at.
    using control_type = AnnotatedScrollBarControl;

    // ---- properties ----
    /// The current offset in DIPs, for an unattached bar. Moves the thumb only and raises no
    /// scroll request.
    Binding<double> value{0.0};
    /// The largest scroll offset in DIPs, for an unattached bar. The minimum is 0.
    Binding<double> maximum{0.0};
    /// How far a step button or arrow key moves, in DIPs. 0, the default, derives it from the
    /// viewport.
    Binding<double> small_change{0.0};
    /// The visible extent in DIPs, for an unattached bar. Sizes the thumb.
    Binding<double> viewport_size{0.0};
    /// Captions pinned beside the rail at scroll offsets. One that overlaps an earlier one hides.
    Binding<std::vector<AnnotatedScrollBarAnnotation>> annotations{};

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
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// Raised before every scroll request, with the target offset and what caused it. Return
    /// ui::Cancel to drop it.
    Callback<Decision(const AnnotatedScrollBarScrollingArgs&)> on_scrolling{};
    /// The tooltip text for the offset under the pointer. Unset shows no tooltip.
    Callback<std::string(double)> on_detail_text_requested{};
    /// Scroll to an absolute offset in DIPs. set_target() installs its own. Set it for another
    /// kind of scrollable.
    Callback<void(double)> on_scroll_to_requested{};
    /// Scroll by a delta in DIPs, negative is up. set_target() installs its own, too.
    Callback<void(double)> on_scroll_by_requested{};
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
    ControlOut<AnnotatedScrollBar> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::AnnotatedScrollBarControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
