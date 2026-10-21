// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/media/AnimatedIcon.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace ui
{

class FlyoutBase; // ui/dialogs/Flyout.hpp

/// The visual state a SplitButton draws in. A checked ToggleSplitButton draws the same states in
/// the accent colors.
enum class SplitButtonState
{
    Normal,               ///< At rest.
    Disabled,             ///< is_enabled() is false.
    FlyoutOpen,           ///< The flyout is open: both parts drawn pressed.
    TouchPressed,         ///< Space or Enter held: both parts pressed.
    PrimaryPointerOver,   ///< The pointer is over the primary part.
    PrimaryPressed,       ///< The primary part is pressed.
    SecondaryPointerOver, ///< The pointer is over the chevron part.
    SecondaryPressed,     ///< The chevron part is pressed.
};

/// A button with a second, chevron part that opens a flyout.
///
/// The primary part raises on_click() and runs the command; the 35 DIP secondary part at the
/// right (or Alt+Down / F4) opens the flyout below the button. As for every flyout anchor, the
/// flyout owns the split button: make it the flyout's anchor and the flyout wires itself in
/// through set_flyout(). The declarative form is ui::SplitButton, usually as a ui::Flyout's
/// `.anchor`.
///
/// Example:
/// @code
/// ui::Property<ui::Color> current{ui::colors::Black};
/// ui::Property<ui::Color> ink{ui::colors::Black};
///
/// window.set_content(ui::Flyout{
///     .anchor  = ui::SplitButton{ .text     = "Choose color",
///                                 .on_click = [&] { ink = current.get(); } },
///     .content = ui::HorizontalStack{ .spacing = 4, .content = {
///         ui::Button{ .text = "Red",  .on_click = [&] { current = ui::Color::from_argb(0xFFFF0000); } },
///         ui::Button{ .text = "Blue", .on_click = [&] { current = ui::Color::from_argb(0xFF0000FF); } },
///     }},
/// });
///
/// // Imperatively:
/// auto  flyout = std::make_unique<ui::FlyoutControl>();
/// auto* split  = flyout->set_anchor(std::make_unique<ui::SplitButtonControl>("Choose color"));
/// flyout->set_content(std::make_unique<ui::TextControl>("Pick a color"));
/// split->on_click([&] { ink = current.get(); });
/// @endcode
class SplitButtonControl : public Element, protected InvokeProvider, protected ExpandCollapseProvider
{
public:
    /// An empty split button: no text or content, no flyout.
    SplitButtonControl();
    /// A split button whose primary part shows `text`.
    explicit SplitButtonControl(std::string text);
    ~SplitButtonControl() override;

    // ---- content ----
    /// The primary part's text, used while there is no content().
    [[nodiscard]] const std::string& text() const
    {
        return text_;
    }
    /// Changes the text, which is also the accessible name when none is set. The button is
    /// measured again and redrawn.
    void set_text(std::string text)
    {
        text_ = std::move(text);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The primary part's content, in place of text(). Returns the observer.
    template <typename T>
    T* set_content(std::unique_ptr<T> content)
    {
        static_assert(std::is_base_of_v<Element, T>, "SplitButton content must be an Element");
        T* observer = content.get();
        set_parent_of(*observer, this);
        content_ = std::move(content);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The primary part's content (owned by the button), or null.
    [[nodiscard]] Element* content() const
    {
        return content_.get();
    }
    /// Padding around the primary part's content; nullopt keeps the theme's button padding.
    void set_padding(std::optional<Thickness> padding)
    {
        padding_override_ = padding;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Whether the primary part takes a click: enabled, and the command (if any) is too.
    [[nodiscard]] bool primary_enabled() const
    {
        return is_enabled() && command_enabled_;
    }

    // ---- behavior ----
    /// The primary part was clicked (or Space/Enter released on the control).
    void on_click(Callback<void()> handler)
    {
        on_click_ = std::move(handler);
    }
    /// A command and its parameter, run after on_click. The primary part is disabled while the
    /// command is, following it as it changes, and without a tooltip of its own the button shows
    /// the command's description. A destroyed command leaves the button as if it had none.
    void set_command(CommandRef command, std::string parameter = {});
    /// The command set_command() installed, or null.
    [[nodiscard]] const Command* command() const
    {
        return command_.get();
    }
    /// The tooltip set on the button, else its command's description.
    [[nodiscard]] std::string tool_tip() const override;

    /// The flyout the chevron opens. Set for you when the split button becomes a flyout's anchor.
    void set_flyout(FlyoutBase* flyout)
    {
        flyout_ = flyout;
    }
    /// The flyout the chevron part opens (non-owning; it owns this button), or null.
    [[nodiscard]] FlyoutBase* flyout() const
    {
        return flyout_;
    }
    /// Whether the flyout is showing.
    [[nodiscard]] bool is_flyout_open() const;
    /// Shows the flyout below the button, left edges lined up.
    void open_flyout();
    /// Hides the flyout if it is open.
    void close_flyout();

    // ---- state ----

protected:
    friend struct testing::VisualStates; // the tests read the state a control draws in
    /// The visual state the button is drawn in, derived from hover, press, key and flyout state.
    [[nodiscard]] SplitButtonState visual_state() const;

    // ---- Element ----
    /// The primary part (content or text plus padding, at least 35 DIPs wide), the 1 DIP divider and
    /// the 35 DIP chevron part side by side.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Splits `final_rect` into the two parts and arranges the content in the primary one.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws each part in its own state's colors, the divider, the content and the chevron.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Tracks hover and press per part; a click on the primary part clicks, on the chevron opens.
    bool pointer_override(const PointerEvent& e) override;
    /// Ticks the content and the chevron's animation.
    void tick_override(Duration delta) override;


public:
    /// A toggle split button's checked state; always false for a plain SplitButton.
    [[nodiscard]] virtual bool checked() const
    {
        return false;
    }
    /// The primary part, without the divider, from the last arrange.
    [[nodiscard]] Rect primary_rect() const;
    /// The chevron part, 35 DIPs wide at the right, from the last arrange.
    [[nodiscard]] Rect secondary_rect() const;

    /// The chevron (an AnimatedIcon).
    [[nodiscard]] AnimatedIconControl* chevron() const
    {
        return chevron_.get();
    }
    /// Whether the control has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// One tab stop for both parts, while enabled.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }

    // ---- automation ----
    /// A SplitButton.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::SplitButton;
    }
    /// The text, when no accessible name is set.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return text_;
    }

protected:
    /// Raises on_click, then runs the command. ToggleSplitButtonControl toggles first.
    virtual void on_click_primary();

private:
    // Enter clicks the primary part, so it is not offered to an open dialog as unclaimed.
    [[nodiscard]] bool takes_enter_key() const
    {
        return is_enabled();
    }
    void command_changed();
    void sync_chevron();
    /// The colors the part in `part_state` draws with: the standard or, checked, the accent
    /// button's.
    [[nodiscard]] ButtonVisual part_visual(const Theme& theme, ButtonState part_state) const;

    std::string                          text_;
    std::unique_ptr<Element>             content_;
    std::unique_ptr<AnimatedIconControl> chevron_;
    std::optional<Thickness>             padding_override_{};
    mutable Thickness                    padding_{};

    bool command_enabled_ = true;

    bool primary_hovered_   = false;
    bool secondary_hovered_ = false;
    bool primary_down_      = false;
    bool secondary_down_    = false;
    bool key_down_          = false; // Space or Enter held

    Callback<void()> on_click_;
    CommandRef       command_;
    std::string      command_parameter_;
    Subscription     command_changed_;
    FlyoutBase*      flyout_ = nullptr; // the flyout owns this button

protected:
    void enabled_changed() override;
    /// Space/Enter click the primary part on key up; Alt+Down and F4 open the flyout.
    bool key_override(const KeyEvent& e) override;
    /// Called by the Window as focus arrives or leaves; redraws.
    void focus_changed(bool focused) override;

protected:
    /// The ring follows the button's corner radius.
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override
    {
        return theme.resolved_button_corner_radius();
    }

protected:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.invoke          = this;
        out.expand_collapse = this;
    }

private:
    // Clicks the primary part; false when it is disabled.
    bool invoke() override;
    // Whether the flyout is open.
    [[nodiscard]] bool expanded_state() const override
    {
        return is_flyout_open();
    }
    // Opens or closes the flyout; false when disabled or there is none.
    bool set_expanded_state(bool expanded) override;
};

/// A SplitButton whose primary part also toggles checked(). Checked, both
/// parts draw in the accent colors. The declarative form is ui::ToggleSplitButton.
///
/// Example:
/// @code
/// ui::Property<bool> bullets{false};
/// ui::Property<int>  bullet_style{0};
///
/// window.set_content(ui::Flyout{
///     .anchor  = ui::ToggleSplitButton{ .text    = "Bullets",
///                                       .checked = bullets }, // two-way
///     .content = ui::VerticalStack{ .content = {
///         ui::Button{ .text = "Round", .on_click = [&] { bullet_style = 0; } },
///         ui::Button{ .text = "Square", .on_click = [&] { bullet_style = 1; } },
///     }},
/// });
/// @endcode
class ToggleSplitButtonControl final : public SplitButtonControl, private ToggleProvider
{
public:
    /// Unchecked, with no text.
    ToggleSplitButtonControl() = default;
    /// Unchecked, the primary part showing `text`.
    explicit ToggleSplitButtonControl(std::string text) : SplitButtonControl(std::move(text))
    {
    }

    /// Whether the button is checked.
    [[nodiscard]] bool checked() const override
    {
        return checked_;
    }
    /// Sets the checked state; fires on_checked_changed() only when the value changes.
    void set_checked(bool checked);
    /// Fires with the new state after a click or set_checked() changes it.
    void on_checked_changed(Callback<void(bool)> handler)
    {
        on_checked_changed_ = std::move(handler);
    }


protected:
    /// Toggles, then raises the base's on_click and runs the command.
    void on_click_primary() override;

private:
    bool                 checked_ = false;
    Callback<void(bool)> on_checked_changed_;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.toggle          = this;
        out.expand_collapse = this;
    }
    // On while checked, Off otherwise.
    [[nodiscard]] ToggleState toggle_value() const override
    {
        return checked_ ? ToggleState::On : ToggleState::Off;
    }
    // Toggles checked() without raising on_click; false when disabled.
    bool toggle_next() override;
};

/// A button with a second part that opens a flyout. Builds a ui::SplitButtonControl.
///
/// The primary part raises `.on_click` and runs `.command`; the secondary part (the chevron)
/// opens the flyout the split button is the `.anchor` of, below it and left-aligned.
///
/// Example:
/// @code
/// ui::Property<std::string> current{"Red"};
/// ui::Property<std::string> ink{"Black"};
///
/// window.set_content(ui::Flyout{
///     .anchor  = ui::SplitButton{.text     = "Choose color",
///                                .on_click = [&] { ink = current.get(); }},
///     .content = ui::VerticalStack{.content = {
///         ui::Button{.text = "Red", .on_click = [&] { current = "Red"; }},
///         ui::Button{.text = "Blue", .on_click = [&] { current = "Blue"; }},
///     }},
/// });
/// @endcode
struct SplitButton
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<SplitButton>` points at.
    using control_type = SplitButtonControl;

    // ---- properties ----
    /// The primary part's label, ignored while `.content` is set.
    Binding<std::string> text{};
    /// Padding around the primary part's content; nullopt keeps the theme's button padding.
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
    /// Run after `.on_click`; while it is disabled the primary part is too (the secondary part
    /// still opens the flyout).
    CommandRef command{};
    /// Handed to `.command` when it runs.
    std::string command_parameter{};
    /// A click on the primary part. The chevron raises nothing, it opens the flyout.
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
    ControlOut<SplitButton> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The primary part's content, instead of `.text`.
    Spec content{};

    /// Consumes the spec and returns the built ui::SplitButtonControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

/// A split button whose primary part also toggles `checked`. Builds a ui::ToggleSplitButtonControl.
///
/// The chevron opens the flyout it is the `.anchor` of, as ui::SplitButton's does. `.checked`
/// is two-way with a `Property<bool>` or a `Property<std::optional<bool>>`: the button shows the
/// property and a click on the primary part writes it back, before `.on_checked_changed` runs.
///
/// Example:
/// @code
/// ui::Property<bool> bullets{false};
///
/// window.set_content(ui::MenuFlyout{
///     .items   = {{.text = "Bullets"}, {.text = "Numbers"}},
///     .anchor  = ui::ToggleSplitButton{
///         .text                  = "List",
///         .checked               = bullets,
///     },
/// });
/// @endcode
struct ToggleSplitButton
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ToggleSplitButton>` points at.
    using control_type = ToggleSplitButtonControl;

    // ---- properties ----
    /// The primary part's label, ignored while `.content` is set.
    Binding<std::string> text{};
    /// Whether the button is checked; std::nullopt shows unchecked, since the button has no third
    /// state. Seeding it raises no `.on_checked_changed`. Two-way with a `Property<bool>` or a
    /// `Property<std::optional<bool>>`. See ui::CheckedField.
    CheckedField checked{false};
    /// Padding around the primary part's content; nullopt keeps the theme's button padding.
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
    /// As ui::SplitButton's: run after `.on_click`; the primary part disabled while it is.
    CommandRef command{};
    /// Handed to `.command` when it runs.
    std::string command_parameter{};
    /// A click on the primary part.
    Action on_click{};
    /// The checked state changed: a click on the primary part, or UI Automation's Toggle. A bound
    /// value doesn't run it.
    Callback<void(bool)> on_checked_changed{};
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
    ControlOut<ToggleSplitButton> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The primary part's content, instead of `.text`.
    Spec content{};

    /// Consumes the spec and returns the built ui::ToggleSplitButtonControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
