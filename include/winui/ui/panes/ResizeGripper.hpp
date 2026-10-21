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

/// A fixed-size corner drag handle that reports two-axis deltas while dragged.
///
/// It draws three diagonal rows of dots anchored to its bottom-right corner, shows the diagonal
/// resize cursor, and measures to a `size()` square whatever it is offered. It resizes nothing
/// itself. The app applies each (dx, dy) to whatever should grow, typically a Grid row and column
/// or an element's explicit size. It works with the mouse only and never takes focus. For a
/// single-axis pane splitter use `Splitter`. `ui::ResizeGripper` is the declarative form.
///
/// Example:
/// @code
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// ui::TextControl* box = stack->add(std::make_unique<ui::TextControl>("Resize me"));
/// box->set_width(200.0f);
/// box->set_height(120.0f);
///
/// ui::ResizeGripperControl* grip = stack->add(std::make_unique<ui::ResizeGripperControl>());
/// grip->set_horizontal_alignment(ui::Alignment::Trailing);
/// grip->on_drag_delta([box](ui::Point delta) {
///     box->set_width(std::max(40.0f, box->width().value_or(0.0f) + delta.x));
///     box->set_height(std::max(24.0f, box->height().value_or(0.0f) + delta.y));
/// });
/// window.set_content(std::move(stack));
/// @endcode
class ResizeGripperControl final : public Element
{
public:
    /// A 16 DIP gripper.
    ResizeGripperControl() = default;

    /// The square's edge length in DIPs; 16 by default.
    [[nodiscard]] float size() const
    {
        return size_;
    }
    /// Sets the edge length in DIPs and lays the gripper out again.
    void set_size(float size)
    {
        size_ = size;
        mark_dirty(DirtyFlags::Layout);
    }

    /// Fired while the pointer drags, with the (dx, dy) movement since the previous call. Works
    /// like SplitterControl::on_drag_delta, on both axes at once.
    void on_drag_delta(Callback<void(Point delta)> handler)
    {
        on_drag_delta_ = std::move(handler);
    }


    /// Whether a left-button drag is in progress. The dots draw at hover strength meanwhile.
    [[nodiscard]] bool dragging() const
    {
        return dragging_;
    }
    /// Whether the pointer is over the gripper.
    [[nodiscard]] bool hovered() const
    {
        return hovered_;
    }

    // ---- automation ----
    /// The diagonal resize arrow, or none while disabled.
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point) const override
    {
        if (!is_enabled())
        {
            return std::nullopt;
        }
        return CursorShape::SizeNorthwestSoutheast;
    }
    /// Reported to screen readers as a Thumb.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Thumb;
    }

protected:
    /// `size()` x `size()`, regardless of `available`. The gripper is never focusable and has no
    /// keyboard resize.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Draws a triangle of dots (rows of one, two and three) in the corner, in the text color at
    /// reduced alpha.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// A left press inside starts the drag. Each move then fires `on_drag_delta()` until the button
    /// is released.
    bool pointer_override(const PointerEvent& e) override;


private:
    float size_ = 16.0f;

    bool  hovered_  = false;
    bool  dragging_ = false;
    Point drag_last_pos_{};

    Callback<void(Point)> on_drag_delta_;

protected:
    void enabled_changed() override;
};

/// A fixed-size corner drag handle. Builds a ui::ResizeGripperControl. `.on_drag_delta` receives
/// the (dx, dy) movement since its previous call. A caller typically passes it on to
/// GridControl::set_row_height() and set_column_width() on a grid it holds through `.control`.
/// ui::Splitter is the single-axis version. It works with the mouse only and takes no focus.
///
/// Example:
/// @code
/// using ui::operator""_dip;
///
/// ui::Control<ui::Grid> grid;
///
/// window.set_content(ui::Grid{
///     .columns = {200_dip, ui::AutoSize},
///     .rows    = {120_dip, ui::AutoSize},
///     .control = grid,
///     .content = {
///         ui::Cell{.row = 0, .column = 0, .content = ui::Button{.text = "Resizable"}},
///         ui::Cell{.row     = 1,
///                  .column  = 1,
///                  .content = ui::ResizeGripper{.on_drag_delta = [&](ui::Point delta)
///                  {
///                      grid->set_column_width(0, std::max(40.0f, grid->column_size(0) + delta.x));
///                      grid->set_row_height(0, std::max(24.0f, grid->row_size(0) + delta.y));
///                  }}},
///     },
/// });
/// @endcode
struct ResizeGripper
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ResizeGripper>` points at.
    using control_type = ResizeGripperControl;

    // ---- properties ----
    /// The square's edge length, DIPs.
    Binding<float> size{16.0f};

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
    /// The (dx, dy) drag since the last call, DIPs; positive is right/down.
    Callback<void(Point)> on_drag_delta{};
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
    ControlOut<ResizeGripper> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::ResizeGripperControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
