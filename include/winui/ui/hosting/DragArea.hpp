// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ui
{

/// A wrapper whose content the window can be dragged by. It stands in for the caption of a
/// borderless window, or of content extended into the title bar, where a TitleBar doesn't fit,
/// such as a splash screen's whole face, a sidebar header or a custom toolbar strip.
///
/// A press on the area moves the window, a double-click maximizes it and a right-click opens the
/// system menu, except over a focusable control inside it: a label or icon drags, a button still
/// clicks. In a window with an ordinary frame it does nothing. Otherwise it passes everything
/// through to its content as ContentHost does, so wrapping a subtree changes nothing about how it
/// lays out or behaves.
///
/// Example:
/// @code
/// ui::Control<ui::Text> label;
/// ui::Control<ui::Button> cancel;
/// window.set_content(ui::DragArea{
///     .content = ui::VerticalStack{
///         .content = {
///             ui::Text{.text = "Loading...", .control = label},
///             ui::Button{.text = "Cancel", .on_click = [] { /* ... */ }, .control = cancel},
///         },
///     },
/// });
/// // window.hit_region_at(label->bounds().center())  == ui::HitRegion::Caption
/// // window.hit_region_at(cancel->bounds().center()) == ui::HitRegion::Client
/// @endcode
class DragAreaControl final : public ContentHost
{
public:
    /// False: the whole area takes the pointer, empty parts included, since they drag the window.
    [[nodiscard]] bool hit_test_transparent() const override
    {
        return false;
    }
    /// An empty area; give it content with ContentHost::set_child().
    DragAreaControl() = default;

    /// Caption everywhere inside the area, except Client along the path under `p` from the first
    /// focusable element down (that element takes the press).
    [[nodiscard]] HitRegion hit_region(Point p) const override
    {
        // Down the path under the pointer: the first focusable element on it takes the press.
        const Element* node = child();
        while (node != nullptr && node->bounds().contains(p))
        {
            if (node->focusable())
            {
                return HitRegion::Client;
            }
            std::vector<Element*> children;
            node->automation_children(children);
            const Element* next = nullptr;
            for (auto it = children.rbegin(); it != children.rend(); ++it)
            {
                if (*it != nullptr && (*it)->bounds().contains(p))
                {
                    next = *it;
                    break;
                }
            }
            node = next;
        }
        return HitRegion::Caption;
    }
};

/// Content the window can be dragged by, the caption stand-in for a borderless window
/// wherever a TitleBar is not the right shape. Builds a ui::DragAreaControl.
///
/// Empty parts of the area hit-test as the window caption; interactive descendants (buttons,
/// text boxes) stay ordinary client area, so they still take clicks.
///
/// Example:
/// @code
/// window.set_content(ui::DragArea{
///     .content = ui::VerticalStack{.spacing = 12, .content = {
///         ui::Text{.text = "Loading..."},                                  // drags the window
///         ui::Button{.text = "Cancel", .on_click = [&] { window.close(); }}, // still clickable
///     }},
/// });
/// @endcode
struct DragArea
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<DragArea>` points at.
    using control_type = DragAreaControl;

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
    ControlOut<DragArea> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// What the area shows; may be empty for a bare drag band.
    Spec content{};

    /// Consumes the spec and returns the built ui::DragAreaControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
