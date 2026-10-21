// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"

#include <algorithm>
#include <memory>
#include <string>

namespace ui
{

/// A child of a stack that is given the room the other children leave. Built by ui::Fill.
///
/// A StackControl measures its children unbounded along its axis. A fill is measured last, with
/// the room left over as its bound, so a scrolling child inside it (a Scroll, a ListView)
/// gets a real height and scrolls. Several fills, and the stack's spacers, share the room by
/// their weight. Where the stack itself has no bound along its axis (inside a Scroll) there
/// is nothing to share, and a fill is as big as its content. Outside a stack it is a plain
/// one-child container.
///
/// Example:
/// @code
/// auto  page = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// page->add(std::make_unique<ui::TextControl>("Inbox"));
/// auto* fill = page->add(std::make_unique<ui::FillControl>());
/// fill->set_child(std::make_unique<ui::ScrollControl>());
/// page->add(std::make_unique<ui::TextControl>("3 unread"));
/// @endcode
class FillControl final : public ContentHost
{
public:
    /// An empty fill of weight 1.
    FillControl() = default;

    /// This fill's share of the leftover room, relative to the other fills and spacers in the
    /// same stack.
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

private:
    float weight_ = 1.0f;
};

/// Gives one child of a stack the room the other children leave. Builds a ui::FillControl.
///
/// In a VerticalStack of a header, a list and a footer, putting the list in a Fill keeps the
/// footer at the bottom and lets the list scroll in between.
///
/// A stack gives each child what it asks for along its axis, with no limit, so a list in a plain
/// stack grows as long as its items and never scrolls. A Fill is measured with the room the
/// other children leave. Several fills share that room by `.weight`, together with the
/// stack's spacers. A ui::Spacer is a Fill with nothing in it. In a stack that has no limit
/// itself (one inside a Scroll) a Fill is measured as big as its content, and takes what
/// room the stack is arranged with beyond that.
///
/// Example:
/// @code
/// ui::Property<std::vector<std::string>> messages;
/// ui::Property<int>                      unread{0};
///
/// window.set_content(ui::VerticalStack{
///     .spacing = 8,
///     .content = {
///         ui::Text{.text = "Inbox", .role = ui::TextRole::Subtitle},
///         ui::Fill{.content = ui::ListView<std::string>{.items = messages}},
///         ui::Text{.text = ui::bind(unread, [](int n) { return std::format("{} unread", n); })},
///     },
/// });
/// @endcode
struct Fill
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ui::Fill>` points at.
    using control_type = FillControl;

    // ---- properties ----
    /// This fill's share of the leftover room, relative to the other fills and spacers in the
    /// same stack.
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
    ControlOut<Fill> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// What gets the room.
    Spec content{};

    /// Consumes the spec and returns the built ui::FillControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
