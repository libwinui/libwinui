// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ui
{

/// A draggable divider that resizes along one axis, between two Grid tracks or two panes.
///
/// Place it in a Grid track of its own with a track on either side: a column for a Vertical bar, a
/// row for a Horizontal one. It then resizes those two tracks within their min and max
/// (GridControl::resize_beside()). With an on_drag_delta() handler it resizes nothing and passes
/// each delta to the handler, for a layout of the caller's own. While it has focus, the arrow keys
/// along its axis resize too. It fills the available length along its axis and is thickness()
/// across it. The declarative form is ui::Splitter.
///
/// Example:
/// @code
/// using ui::operator""_fr;
/// using ui::operator""_dip;
///
/// window.set_content(ui::Grid{
///     .columns   = {ui::minmax(80_dip, 1_fr), 6_dip, 1_fr},
///     .min_width = 420.0f,
///     .content   = {
///         ui::Cell{.column = 0, .content = ui::Button{.text = "Left"}},
///         ui::Cell{.column = 1, .content = ui::Splitter{}}, // drag or arrow keys move the two columns
///         ui::Cell{.column = 2, .content = ui::Button{.text = "Right"}},
///     },
/// });
///
/// // A layout of your own: the handler gets the deltas and the splitter resizes nothing.
/// ui::Property<float> top_height{200.0f};
/// auto splitter = std::make_unique<ui::SplitterControl>(ui::Orientation::Horizontal);
/// splitter->on_drag_delta([&](float dy) { top_height = std::max(40.0f, top_height.get() + dy); });
/// @endcode
class SplitterControl final : public Element
{
public:
    /// A 6 DIP bar along `orientation`, focusable.
    explicit SplitterControl(Orientation orientation = Orientation::Vertical) : orientation_(orientation)
    {
    }

    /// The bar's axis, fixed at construction.
    [[nodiscard]] Orientation orientation() const
    {
        return orientation_;
    }

    /// The bar's size across its axis, in DIPs; 6 by default.
    [[nodiscard]] float thickness() const
    {
        return thickness_;
    }
    /// Changes the thickness and lays the bar out again.
    void set_thickness(float thickness)
    {
        thickness_ = thickness;
        mark_dirty(DirtyFlags::Layout);
    }

    /// Fired while the pointer drags or an arrow key steps, with the movement in DIPs since the
    /// previous call. The axis follows `orientation`: x for Vertical, y for Horizontal. Setting a
    /// handler turns off the resizing of the Grid tracks beside the bar.
    void on_drag_delta(Callback<void(float)> handler)
    {
        on_drag_delta_ = std::move(handler);
    }

    /// Whether a pointer drag is in progress: the button went down on the bar and is still held.
    [[nodiscard]] bool dragging() const
    {
        return dragging_;
    }
    /// Whether the pointer is over the bar. The bar draws the accent separator while hovered or
    /// dragged.
    [[nodiscard]] bool hovered() const
    {
        return hovered_;
    }


    // ---- keyboard resize ----
    /// Whether the bar takes focus; true by default. With focus, ArrowLeft/Right (Vertical) or
    /// ArrowUp/Down (Horizontal) resize by +/- keyboard_increment(), or four times that with
    /// Shift held. False leaves it out of the tab order.
    void set_focusable(bool focusable)
    {
        focusable_ = focusable;
        mark_dirty(DirtyFlags::Visual);
    }
    /// Whether the bar is a tab stop: set_focusable(), and only while enabled.
    [[nodiscard]] bool focusable() const override
    {
        return focusable_ && is_enabled();
    }
    /// Whether the bar has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// The arrow-key step, in DIPs; 8 by default. Set a different step, such as one grid unit,
    /// with set_keyboard_increment().
    [[nodiscard]] float keyboard_increment() const
    {
        return keyboard_increment_;
    }
    /// Sets the arrow-key step, in DIPs.
    void set_keyboard_increment(float increment)
    {
        keyboard_increment_ = increment;
    }

    // ---- automation ----
    /// The resize arrow along the drag axis: west-east for a vertical bar, north-south for a
    /// horizontal one.
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point) const override
    {
        if (!is_enabled())
        {
            return std::nullopt;
        }
        return orientation_ == Orientation::Vertical ? CursorShape::SizeWestEast : CursorShape::SizeNorthSouth;
    }
    /// Reported to screen readers as a Thumb with no value, because the splitter reports relative
    /// movement and has no bounded value of its own.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Thumb;
    }

protected:
    /// thickness() across the axis by the available length along it (thickness() when unbounded).
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// A 1 pixel divider at rest, and a 2 pixel accent separator while hovered or dragged.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Starts a drag on a press over the bar, reports each move's delta and ends on release.
    bool pointer_override(const PointerEvent& e) override;


private:
    // Only the arrows along the splitter's axis; the other two still move focus. Claimed so the
    // Window's directional focus navigation leaves them alone (Element::key_override).
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return vertical() ? (key == Key::ArrowLeft || key == Key::ArrowRight)
                          : (key == Key::ArrowUp || key == Key::ArrowDown);
    }
    [[nodiscard]] bool vertical() const
    {
        return orientation_ == Orientation::Vertical;
    }
    // Hands `delta` to on_drag_delta, or else to the Grid tracks beside the bar; returns how much
    // of it was applied (a track at its min or max takes less).
    float move_by(float delta);

    Orientation orientation_;
    float       thickness_ = 6.0f;

    bool  hovered_            = false;
    bool  dragging_           = false;
    bool  focusable_          = true;
    float keyboard_increment_ = 8.0f;
    Point drag_last_pos_{};

    Callback<void(float)> on_drag_delta_;

protected:
    void enabled_changed() override;
    /// Called by the Window as focus arrives or leaves; redraws.
    void focus_changed(bool /*focused*/) override
    {
        mark_dirty(DirtyFlags::Visual);
    }
    /// The arrow keys along the axis step by keyboard_increment(), four times as far with Shift.
    bool key_override(const KeyEvent& e) override;
};

/// A draggable divider. Builds a ui::SplitterControl.
///
/// In a Grid track of its own between two others it resizes those two tracks, by drag or by the
/// arrow keys, within their `ui::minmax()` limits. For a layout of your own, set `.on_drag_delta`.
/// The splitter then resizes nothing and passes on each delta in DIPs. Add the delta to
/// GridControl::column_size(), not to a neighbor's bounds(). Bounds are already shrunk by margins,
/// so feeding them back collapses the track on the first drag.
///
/// Example:
/// @code
/// using ui::operator""_dip;
/// using ui::operator""_fr;
///
/// window.set_content(ui::Grid{
///     .columns = {ui::minmax(120_dip, 200_dip), ui::AutoSize, 1_fr},
///     .content = {
///         ui::Cell{.column = 0, .content = ui::Text{.text = "Folders"}},
///         ui::Cell{.column = 1, .content = ui::Splitter{}},
///         ui::Cell{.column = 2, .content = ui::Text{.text = "Messages"}},
///     },
/// });
/// @endcode
struct Splitter
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Splitter>` points at.
    using control_type = SplitterControl;

    // ---- properties ----
    /// The bar's own axis. Vertical (the default) is a vertical bar dragged left and right to
    /// resize columns. Horizontal resizes rows.
    Orientation orientation = Orientation::Vertical;
    /// The bar's thickness across its axis, DIPs.
    Binding<float> thickness{6.0f};
    /// Whether the bar takes focus. With focus, the arrow keys along the drag axis resize as a
    /// drag does, four times as far with Shift. On by default.
    bool focusable = true;
    /// One arrow-key step, DIPs; four times as far with Shift.
    Binding<float> keyboard_increment{8.0f};

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
    Binding<bool>                     enabled{true}; ///< False ignores the pointer and keys.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The drag or key step along the drag axis since the previous call, DIPs. Positive is right
    /// or down. Setting it turns off the resizing of the Grid tracks beside the bar.
    Callback<void(float)> on_drag_delta{};
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
    ControlOut<Splitter> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::SplitterControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
