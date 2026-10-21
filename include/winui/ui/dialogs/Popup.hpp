// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace ui
{

/// Floats one child above the tree, at an offset from where the Popup sits.
///
/// The Popup itself takes no space in layout and draws no background, border or shadow. While it
/// is open, the window measures the child at its natural size and places its top-left corner at
/// the Popup's position plus the offsets. A child running past the window edge is clipped and
/// stays where it is. Light dismiss is off by default, so the popup stays open until
/// `set_open(false)` and clicks outside reach the page. With light dismiss on, a click outside or
/// Escape closes it, and it holds keyboard focus while open and gives it back on close. In a spec
/// tree it is built by `ui::Popup`, usually with `is_open` bound to a Property.
///
/// Example:
/// @code
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// ui::ButtonControl* show  = stack->add(std::make_unique<ui::ButtonControl>("Show popup"));
/// ui::PopupControl*  popup = stack->add(std::make_unique<ui::PopupControl>());
/// ui::ButtonControl* close = popup->set_child(std::make_unique<ui::ButtonControl>("Close"));
/// popup->set_horizontal_offset(200.0f);
/// popup->set_light_dismiss(true);
/// popup->set_child_transitions({ui::PopupTransition{}});
/// show->on_click([popup] { popup->set_open(true); });
/// close->on_click([popup] { popup->set_open(false); });
/// popup->on_closed([] { /* closed by the button, a click outside or Escape */ });
/// window.set_content(std::move(stack));
///
/// // Declaratively, with the open state in a Property:
/// ui::Property<bool> open{false};
/// std::unique_ptr<ui::Element> node = ui::build(ui::Popup{
///     .is_open       = open, // two-way: a light dismiss writes false into `open`
///     .light_dismiss = true,
///     .content       = ui::Text{.text = "Simple Popup"},
/// }); // add `node` to any panel
/// @endcode
class PopupControl final : public Element
{
public:
    /// A closed popup with no child, zero offsets and light dismiss off. Not hit-test visible: it
    /// draws nothing where it sits, so a press there goes to what is under it.
    PopupControl();

    /// Takes ownership of the floated child and returns a non-owning observer to it. The child is
    /// not part of the main tree. The window lays it out and draws it while the popup is open.
    template <typename T>
    T* set_child(std::unique_ptr<T> child)
    {
        static_assert(std::is_base_of_v<Element, T>, "Popup child must be an Element");
        T* observer = child.get();
        set_parent_of(*observer, this);
        child_ = std::move(child);
        mark_dirty(DirtyFlags::Visual);
        sync_popup();
        return observer;
    }
    /// The floated child (non-owning), or nullptr.
    [[nodiscard]] Element* child() const
    {
        return child_.get();
    }

    /// Whether the popup is open. Opening raises on_opened. Closing raises on_closed, whether by
    /// set_open(), a click outside or Escape. Setting the value it already has raises nothing.
    [[nodiscard]] bool is_open() const
    {
        return open_;
    }
    /// Opens or closes the popup. With child transitions, the child stays on screen after
    /// closing until its close transition has played (see `closing()`).
    void set_open(bool open);

    /// The child's offset from the Popup's own position, in DIPs. 0 by default.
    [[nodiscard]] float horizontal_offset() const
    {
        return horizontal_offset_;
    }
    /// Sets the horizontal offset. The child moves on the next layout.
    void set_horizontal_offset(float offset)
    {
        horizontal_offset_ = offset;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The child's vertical offset from the Popup's position, in DIPs; 0 by default.
    [[nodiscard]] float vertical_offset() const
    {
        return vertical_offset_;
    }
    /// Sets the vertical offset. The child moves on the next layout.
    void set_vertical_offset(float offset)
    {
        vertical_offset_ = offset;
        mark_dirty(DirtyFlags::Visual);
    }

    /// Whether light dismiss is on; false by default. On, a click outside the child or Escape
    /// closes the popup, and it holds keyboard focus while open.
    [[nodiscard]] bool light_dismiss() const
    {
        return light_dismiss_;
    }
    /// Turns light dismiss on or off. Takes effect at once, on an open popup too.
    void set_light_dismiss(bool enabled)
    {
        light_dismiss_ = enabled;
        sync_popup();
    }

    /// Fired after the popup opens. Replaces any previous handler.
    void on_opened(Callback<void()> handler)
    {
        on_opened_ = std::move(handler);
    }
    /// Fired after the popup closes, whether by `set_open(false)`, a click outside or Escape.
    /// Reset state bound to `is_open()` here. Replaces any previous handler.
    void on_closed(Callback<void()> handler)
    {
        on_closed_ = std::move(handler);
    }

    /// The transitions the child plays; empty by default. A PopupTransition slides the child
    /// in from its offset and fades it in on open, and fades it out on close. A
    /// ContentTransition also plays both ways, and an EntranceTransition on open only.
    [[nodiscard]] const TransitionList& child_transitions() const
    {
        return child_transitions_;
    }
    /// Sets the child's transitions. Applies from the next open or close.
    void set_child_transitions(TransitionList transitions)
    {
        child_transitions_ = std::move(transitions);
    }
    /// Whether the child is still on screen after closing, playing its close transition.
    [[nodiscard]] bool closing() const
    {
        return closing_;
    }

    // ---- automation ----
    /// Screen readers see the Popup as an empty group where it sits. They see the child only
    /// while the popup is open.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Group;
    }

protected:
    // ---- Element: nothing in layout, nothing drawn in place ----
    /// Always zero: the Popup takes no space in layout.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    void               clear_dirty_override(DirtyFlags flags) override;
    void               tick_override(Duration delta) override;


private:
    // Opens, updates or closes the popup to match open_, closing_, the child and light dismiss.
    void sync_popup();

    std::unique_ptr<Element> child_;
    bool                     open_              = false;
    bool                     light_dismiss_     = false;
    float                    horizontal_offset_ = 0.0f;
    float                    vertical_offset_   = 0.0f;
    bool                     closing_           = false;
    Callback<void()>         on_opened_;
    Callback<void()>         on_closed_;
    TransitionList           child_transitions_;
};

/// One child floated above the tree at an offset from where the popup sits, with no chrome of
/// its own. Builds a ui::PopupControl, which takes no room in layout.
///
/// Example:
/// @code
/// ui::Property<bool> open{false};
///
/// window.set_content(ui::VerticalStack{ .content = {
///     ui::Button{ .text = "Show Popup", .on_click = [&] { open = true; } },
///     ui::Popup{ .is_open           = open,
///                .horizontal_offset = 200,
///                .light_dismiss     = true,
///                .content           = ui::Text{ .text = "Simple Popup" } },
/// }});
/// @endcode
struct Popup
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Popup>` points at.
    using control_type = PopupControl;

    // ---- properties ----
    /// Whether the child is showing.
    TwoWayField<bool> is_open{false};
    /// The child's horizontal offset from the popup's own position, in DIPs.
    Binding<float> horizontal_offset{0.0f};
    /// The child's vertical offset from the popup's own position, in DIPs.
    Binding<float> vertical_offset{0.0f};
    /// Whether an outside click or Escape closes it.
    Binding<bool> light_dismiss{false};
    /// The transitions the child plays (ui::PopupControl::set_child_transitions()). A
    /// PopupTransition animates the child opening and closing.
    Binding<TransitionList> child_transitions{};

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
    /// Raised once the child is showing.
    Action on_opened{};
    /// Raised however it closed, including a click outside or Escape. A two-way bound `is_open`
    /// has been written by then.
    Action on_closed{};
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
    ControlOut<Popup> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The floated content; it draws its own background, if any.
    Spec content{};

    /// Consumes the spec and returns the built ui::PopupControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
