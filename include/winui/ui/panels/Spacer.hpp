// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string>

namespace ui
{

/// A gap in a stack that takes the room the other children leave. Built by ui::Spacer.
///
/// A StackControl gives its children what they ask for along its axis and leaves the rest of its
/// slot empty at the end. A spacer takes that rest instead, so the children after it are pushed
/// to the far edge: a toolbar with its last button on the right is a horizontal stack with a
/// spacer in front of that button. Several spacers share the room by their weight, together
/// with the stack's fills (FillControl). Outside a stack a spacer is an empty element of its
/// minimum size.
///
/// Example:
/// @code
/// auto  bar    = std::make_unique<ui::StackControl>(ui::Orientation::Horizontal);
/// bar->add(std::make_unique<ui::ButtonControl>("Back"));
/// bar->add(std::make_unique<ui::SpacerControl>());
/// bar->add(std::make_unique<ui::ButtonControl>("Next"));
/// @endcode
class SpacerControl final : public Element
{
public:
    /// A spacer of weight 1.
    SpacerControl() = default;

    /// This spacer's share of the leftover room, relative to the other spacers in the same stack.
    [[nodiscard]] float weight() const
    {
        return weight_;
    }
    /// Sets the share; negative is treated as 0. Updates the layout.
    void set_weight(float weight)
    {
        weight_ = (std::max)(0.0f, weight);
        mark_dirty(DirtyFlags::Layout);
    }

    /// True: measuring reads nothing at all.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

protected:
    /// Nothing of its own: a spacer is as small as its minimum size until a stack stretches it.
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    /// Draws nothing.
    void render_override(Renderer&, const RenderContext&) const override
    {
    }


private:
    float weight_ = 1.0f;
};

/// A flexible gap in a stack: it takes the room the other children leave along the stack, which
/// pushes whatever comes after it to the far edge. Builds a ui::SpacerControl.
///
/// A stack gives each child what it asks for and leaves the rest of its slot empty at the end.
/// Put a Spacer where that empty room should be instead. With several spacers the room is
/// shared by `.weight`, together with the stack's ui::Fill children. Set a minimum gap with
/// `.min_width`, or `.min_height` in a vertical stack. The stack needs more room than its
/// children ask for, or there is nothing to share. A stack sized to its content has none.
///
/// Example:
/// @code
/// // A toolbar: two buttons on the left, one on the right.
/// window.set_content(ui::HorizontalStack{
///     .spacing         = 8,
///     .child_alignment = ui::Alignment::Center,
///     .content         = {
///         ui::Button{.text = "New"},
///         ui::Button{.text = "Open"},
///         ui::Spacer{},
///         ui::Button{.text = "Settings"},
///     },
/// });
/// @endcode
struct Spacer
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ui::Spacer>` points at.
    using control_type = SpacerControl;

    // ---- properties ----
    /// This spacer's share of the leftover room, relative to the other spacers in the same stack.
    Binding<float> weight{1.0f};

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
    ControlOut<Spacer> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::SpacerControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
