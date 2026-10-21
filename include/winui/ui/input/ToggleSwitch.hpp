// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Strings.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/ControlHeader.hpp"
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

/// A boolean on/off switch: a pill-shaped track with a sliding thumb, an optional header above
/// it and a label beside it that follows the state.
///
/// Use it for a setting that takes effect immediately (a CheckBox is for a choice confirmed
/// later). A click released over the control, or Space on key-up while focused, flips checked() and
/// fires on_checked_changed(). The control reserves the width of the wider of its two labels, so
/// flipping it never reflows its row. With animations running, the thumb slides and grows on
/// hover. Otherwise it snaps.
///
/// Example:
/// @code
/// ui::Property<bool> wifi{true};
///
/// auto  stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* sw    = stack->add(std::make_unique<ui::ToggleSwitchControl>(true));
/// sw->set_header("Wi-Fi");
/// sw->on_checked_changed([&](bool on) { wifi = on; });
///
/// // Or declaratively, kept in step with the Property:
/// stack->add(ui::ToggleSwitch{
///     .header             = "Airplane mode",
///     .checked            = ui::bind(wifi, [](bool w) { return !w; }),
///     .checked_text       = "Flying",
///     .unchecked_text     = "Grounded",
///     .on_checked_changed = [&](bool on) { wifi = !on; },
/// }.build());
/// window.set_content(std::move(stack));
/// @endcode
class ToggleSwitchControl final : public Element, private ToggleProvider
{

    // ---- composition ----
    void sync_composition_override(detail::Compositor& compositor, const Theme& theme) override;

public:
    /// An enabled switch, off, with the "On"/"Off" labels and no header.
    ToggleSwitchControl();
    /// A switch starting in `on`; the initial state never fires on_checked_changed() or animates.
    explicit ToggleSwitchControl(bool on);

    // ---- content ----
    /// The header text; empty by default. See set_header().
    [[nodiscard]] const std::string& header() const
    {
        return header_.text();
    }
    /// The label above the switch. Empty (the default) draws none. A change updates the layout.
    void set_header(std::string header)
    {
        if (header_.set_text(std::move(header)))
        {
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }

    /// The label beside the track while on: the control's own, else strings().toggle_switch.on
    /// ("On").
    [[nodiscard]] const std::string& checked_text() const
    {
        return checked_text_ ? *checked_text_ : strings().toggle_switch.on;
    }
    /// Replaces the on label, or with nullopt goes back to strings().toggle_switch.on. Updates the
    /// layout, since the control is as wide as its wider label.
    void set_checked_text(std::optional<std::string> text)
    {
        checked_text_ = std::move(text);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The label beside the track while off: the control's own, else strings().toggle_switch.off
    /// ("Off").
    [[nodiscard]] const std::string& unchecked_text() const
    {
        return unchecked_text_ ? *unchecked_text_ : strings().toggle_switch.off;
    }
    /// Replaces the off label, or with nullopt goes back to strings().toggle_switch.off. Updates
    /// the layout, as set_checked_text() does.
    void set_unchecked_text(std::optional<std::string> text)
    {
        unchecked_text_ = std::move(text);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// An icon shown beside the track while on, in place of checked_text(). Unset (the
    /// default) shows the text; with an icon, checked_text() still names the state for UI
    /// Automation.
    [[nodiscard]] const IconSource& checked_icon() const
    {
        return checked_icon_;
    }
    /// Sets the on icon, or clears it with an empty source. Updates the layout.
    void set_checked_icon(IconSource icon)
    {
        checked_icon_ = std::move(icon);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// An icon shown beside the track while off, in place of unchecked_text().
    [[nodiscard]] const IconSource& unchecked_icon() const
    {
        return unchecked_icon_;
    }
    /// Sets the off icon, or clears it with an empty source. Updates the layout.
    void set_unchecked_icon(IconSource icon)
    {
        unchecked_icon_ = std::move(icon);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Whether the switch is on. Off by default.
    [[nodiscard]] bool checked() const
    {
        return on_;
    }
    /// Changes the state and redraws. Does nothing when unchanged. Otherwise fires
    /// on_checked_changed(), also when called from code.
    void set_checked(bool on);


    /// Invoked with the new state whenever checked() changes (click, Space, UI Automation
    /// or set_checked()). Replaces any previous handler.
    void on_checked_changed(Callback<void(bool)> handler)
    {
        on_checked_changed_ = std::move(handler);
    }

    // ---- layout ----
    /// True: measuring reads only the switch's own state, the measurer and the theme.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

    // ---- focus ----
    /// Whether the switch holds keyboard focus (and so draws its focus ring around the track).
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// Only an enabled switch joins the tab ring.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }

    // ---- derived visual state ----

protected:
    friend struct testing::VisualStates; // the tests read the state a control draws in
    /// Disabled, Pressed, Hover or Normal, in that priority; picks the track and thumb colors.
    [[nodiscard]] ButtonState visual_state() const;
    [[nodiscard]] Size        measure_override(Size available, const MeasureContext& context) const override;
    void                      arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    bool pointer_override(const PointerEvent& e) override;

    // ---- render ----
    void render_override(Renderer& renderer, const RenderContext& context) const override;

    // ---- time ----
    /// Advances the knob's slide and resize.
    void tick_override(Duration delta) override;


public:
    /// True while the pointer is held down over the switch or Space is held; never while
    /// disabled.
    [[nodiscard]] bool is_pressed() const;

    // ---- automation ----
    /// Role Button; UI Automation has no dedicated toggle switch role.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Button;
    }
    /// The header when there is one, since it names the switch; otherwise the state's text.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return header_.empty() ? current_text() : header_.text();
    }

private:
    // The text for the current state, whether or not an icon is showing in its place.
    [[nodiscard]] const std::string& current_text() const
    {
        return on_ ? checked_text() : unchecked_text();
    }
    // What sits beside the track in one state: that state's icon at its glyph size, else its
    // text as measured, else nothing.
    [[nodiscard]] Size label_extent(bool on, const MeasureContext& context) const;

    void               toggle();
    [[nodiscard]] bool hot() const;
    [[nodiscard]] Rect track_bounds(const Theme& theme) const;
    // Centered in a container sized off the rest diameter, so the hover growth expands from a
    // fixed center, as in a fixed 20x20 knob container.
    [[nodiscard]] Rect knob_bounds(const Theme& theme, bool on, float knob_d) const;
    // The pressed stretch: anchored at the near edge of the knob's slot and reaching toward
    // the far edge, the direction it would travel next.
    [[nodiscard]] Rect knob_bounds_pressed(const Theme& theme, bool on) const;
    // The one place render() and sync_composition() both take the knob shape from, so the CPU
    // and compositor geometry never diverge.
    [[nodiscard]] Rect knob_shape_bounds(const Theme& theme, bool on, ButtonState state) const;
    // `ButtonKnob`'s knob at `position`, 0 fully off and 1 fully on.
    [[nodiscard]] Rect button_knob_bounds(const Theme& theme, float position) const;
    // Where that knob is drawn now: the eased slide while a host ticks, else the resting end.
    [[nodiscard]] float button_knob_position() const;

    ControlHeader              header_{4.0f};   // 4 DIPs between the header and the switch
    std::optional<std::string> checked_text_;   // unset: strings().toggle_switch.on
    std::optional<std::string> unchecked_text_; // unset: strings().toggle_switch.off
    IconSource                 checked_icon_;
    IconSource                 unchecked_icon_;
    bool                       on_ = false;

    bool hovered_      = false;
    bool pointer_down_ = false;
    bool key_down_     = false;

    bool composed_hot_ = false;

    // The knob's rect relative to the track's top-left, eased as it slides and resizes; its first
    // target only records, so a switch built already on does not slide in from nowhere.
    detail::RectTween knob_tween_;
    // What the last sync saw, so a slide gets the slide's duration and a resize the shorter one.
    bool composed_on_ = false;

    // 0 (off) to 1 (on), linear in time and eased when drawn. It only moves once a host ticks, so
    // a host with no clock gets the resting position at once.
    double slide_   = 0.0;
    bool   ticking_ = false;

    Callback<void(bool)> on_checked_changed_;

protected:
    void enabled_changed() override;
    void focus_changed(bool focused) override;
    bool key_override(const KeyEvent& e) override;

protected:
    /// Around switch and label, or the switch alone, as the theme places it.
    [[nodiscard]] Rect  focus_ring_rect(const Theme& theme) const override;
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.toggle = this;
    }
    // On or Off; a ToggleSwitch is never indeterminate.
    [[nodiscard]] ToggleState toggle_value() const override
    {
        return on_ ? ToggleState::On : ToggleState::Off;
    }
    // Flips the state through set_checked(), firing on_checked_changed(). Returns false while disabled.
    bool toggle_next() override
    {
        if (!is_enabled())
        {
            return false;
        }
        set_checked(!on_);
        return true;
    }
};

/// A boolean on/off switch, with a header above it and a label beside it that follows the state.
/// Builds a ui::ToggleSwitchControl.
///
/// Example:
/// @code
/// ui::Property<bool> wifi{true};
///
/// window.set_content(ui::ToggleSwitch{
///     .header            = "Wi-Fi",
///     .checked           = wifi,
///     .checked_text      = "Connected",
///     .unchecked_text    = "Off",
/// });
/// @endcode
struct ToggleSwitch
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ToggleSwitch>` points at.
    using control_type = ToggleSwitchControl;

    // ---- properties ----
    /// The label above the control. Empty draws none.
    Binding<std::string> header{};
    /// On or off. Seeding it raises no `.on_checked_changed`.
    TwoWayField<bool> checked{false};
    /// The label beside the track while on; unset uses strings().toggle_switch.on ("On"). The
    /// same text in both `checked_text` and `unchecked_text` makes a label that does not change
    /// with the state. Both empty shows no label.
    Binding<std::optional<std::string>> checked_text{};
    /// The label while off; unset uses strings().toggle_switch.off ("Off").
    Binding<std::optional<std::string>> unchecked_text{};
    /// An icon in place of the on state's text, which then only names that state for UI
    /// Automation.
    Binding<IconSource> checked_icon{};
    /// The off state's icon, as `checked_icon`.
    Binding<IconSource> unchecked_icon{};

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
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The new state, whenever the user changes it. A bound value doesn't run it.
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
    ControlOut<ToggleSwitch> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::ToggleSwitchControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
