// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/media/AnimatedIcon.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>

namespace ui
{

/// A Button with a trailing chevron that says a click opens a menu or flyout.
///
/// It adds no behavior of its own to Button: the flyout is whichever FlyoutBase (a MenuFlyout, a
/// Flyout) has it as its anchor, and a click opens that flyout as it would for any button. It
/// adds a 12x12 animated chevron 8 DIPs right of the content, which dips on a press, and the
/// ExpandCollapse automation pattern, expanded while the flyout is open. All of
/// Button's API (text, content, style, on_click, command) applies.
///
/// Example:
/// @code
/// // Declarative: the flyout owns its anchor.
/// window.set_content(ui::MenuFlyout{
///     .items     = {{.text = "Send"}, {.text = "Reply"}, {.text = "Reply All", .on_click = [] { /* ... */ }}},
///     .placement = ui::PopupPlacement::Below,
///     .placement_alignment = ui::PopupPlacementAlignment::Leading,
///     .anchor              = ui::DropdownButton{.text = "Email"},
/// });
///
/// // Imperative: the same tree built by hand.
/// auto  stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* menu  = stack->add(std::make_unique<ui::MenuFlyoutControl>());
/// menu->set_items({{.text = "Send"}, {.text = "Reply"}, {.text = "Reply All"}});
/// menu->set_placement(ui::PopupPlacement::Below);
/// menu->set_placement_alignment(ui::PopupPlacementAlignment::Leading);
/// ui::DropdownButtonControl* email = menu->set_anchor(std::make_unique<ui::DropdownButtonControl>("Email"));
/// @endcode
class DropdownButtonControl : public ButtonControl, protected ExpandCollapseProvider
{
public:
    /// A button with no text; give it content through Button's setters.
    DropdownButtonControl();
    /// A button labeled `text`, with the chevron after it.
    explicit DropdownButtonControl(std::string text);
    ~DropdownButtonControl() override;

    /// Whether the flyout the button opens is showing.
    /// False when no flyout has this button as its anchor.
    [[nodiscard]] bool is_flyout_open() const;

    /// The chevron (an AnimatedIcon), for an app that wants another source. Owned by the
    /// button. Its state ("Normal", "PointerOver" or "Pressed") follows the button's.
    [[nodiscard]] AnimatedIconControl* chevron() const
    {
        return chevron_.get();
    }

    // ---- Element ----
    /// The same answer as ButtonControl's, since the chevron measures only its glyph.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return ButtonControl::parallel_measure_safe();
    }

    // ---- automation ----

protected:
    /// Button's size, at least as tall as the chevron plus the padding.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places the content as Button does and the chevron at the right edge of the padded area,
    /// centered vertically.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws the button, then the chevron in the theme's secondary text color at rest and its
    /// tertiary one under the pointer and while pressed. Disabled, on an Accent button and in
    /// themes without those colors, the chevron takes the label's color.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Button's pointer handling, then moves the chevron to the new visual state.
    bool pointer_override(const PointerEvent& e) override;
    /// Advances Button's animations and the chevron's, catching state changes without an event
    /// (set_enabled(), focus loss).
    void tick_override(Duration delta) override;


private:
    /// Sets the chevron's animation state from the button's visual state.
    void sync_chevron();

    std::unique_ptr<AnimatedIconControl> chevron_;

protected:
    /// Button's keys (Enter or Space click on key up), then moves the chevron to the new state.
    bool key_override(const KeyEvent& e) override;

protected:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.invoke          = this;
        out.expand_collapse = this;
    }

private:
    // Expanded while the flyout is open (is_flyout_open()).
    [[nodiscard]] bool expanded_state() const override
    {
        return is_flyout_open();
    }
    // Shows or hides the flyout; false (and nothing done) when there is no flyout or the button is
    // disabled.
    bool set_expanded_state(bool expanded) override;
};

/// A button with a chevron that says it opens a flyout. Builds a ui::DropdownButtonControl.
///
/// The button does not hold the flyout itself: it is the `.anchor` of a ui::MenuFlyout or
/// ui::Flyout, and a click opens that.
///
/// Example:
/// @code
/// window.set_content(ui::MenuFlyout{
///     .items     = {{.text = "Send"}, {.text = "Reply"}, {.text = "Reply All"}},
///     .placement = ui::PopupPlacement::Below,
///     .placement_alignment = ui::PopupPlacementAlignment::Leading,
///     .anchor              = ui::DropdownButton{.text = "Email"},
/// });
/// @endcode
struct DropdownButton
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<DropdownButton>` points at.
    using control_type = DropdownButtonControl;

    // ---- properties ----
    /// The label before the chevron, ignored while `.content` is set.
    Binding<std::string> text{};
    /// The button look, as ui::Button's.
    Binding<ButtonVariant> variant{ButtonVariant::Neutral};
    /// Padding around the label or content; nullopt keeps the theme's button padding.
    Binding<std::optional<Thickness>> padding{};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named Style; explicit fields below win over it.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Fixed width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Fixed height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Minimum width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Minimum height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Maximum width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Maximum height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment within the parent's slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment within the parent's slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    /// Shown when the pointer rests on it. Unset: the command's description.
    Binding<std::string> tool_tip{};
    Binding<bool>        enabled{true};   ///< False grays it out and takes no input.
    Accessibility        accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// As ui::Button's: run after `.on_click`, and the button enabled only while it is.
    CommandRef command{};
    /// Handed to `.command` when it runs.
    std::string command_parameter{};
    /// A click; `.command` runs after it.
    Action on_click{};
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
    ControlOut<DropdownButton> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// Arbitrary content (an icon, say) before the chevron, instead of `.text`.
    Spec content{};

    /// Consumes the spec and returns the built ui::DropdownButtonControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
