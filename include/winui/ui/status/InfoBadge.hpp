// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

namespace ui
{

/// The severity a badge takes its background color from.
enum class InfoBadgeSeverity
{
    Default,       ///< the accent fill
    Attention,     ///< the attention fill
    Informational, ///< the solid neutral fill
    Success,       ///< the success fill
    Caution,       ///< the caution fill
    Critical,      ///< the critical fill
};

/// A small non-interactive badge showing a number, an icon or a plain dot, typically over a
/// navigation item or a button to flag new or pending items.
///
/// What it shows follows value() and icon(): a value shows the number; otherwise the icon if one
/// is set; otherwise a dot. The fill comes from severity() (the accent by default) unless
/// a local background() overrides it. The badge is squared whenever it would be narrower than tall,
/// so a one-digit value is a circle, and its corners are always fully rounded.
///
/// Example:
/// @code
/// // Declarative: an unread count bound to state, and a critical icon badge.
/// ui::Property<int> unread{5};
/// window.set_content(ui::HorizontalStack{
///     .spacing  = 8.0f,
///     .content  = {
///         ui::InfoBadge{.value = unread},
///         ui::InfoBadge{
///             .severity = ui::InfoBadgeSeverity::Critical,
///             .icon     = ui::InfoBadgeControl::severity_glyph(ui::InfoBadgeSeverity::Critical),
///         },
///     },
/// });
///
/// // Imperative:
/// auto  stack = std::make_unique<ui::StackControl>(ui::Orientation::Horizontal);
/// auto* badge = stack->add(std::make_unique<ui::InfoBadgeControl>(3));
/// badge->set_severity(ui::InfoBadgeSeverity::Attention);
/// badge->set_value(std::nullopt); // back to a dot
/// @endcode
class InfoBadgeControl final : public Element
{
public:
    /// A dot badge in the Default severity.
    InfoBadgeControl() = default;
    /// A badge showing `value` (clamped to >= 0).
    explicit InfoBadgeControl(int value) : value_((std::max)(0, value))
    {
    }

    /// The number shown; unset (the default) shows the icon, or a dot when there is none.
    [[nodiscard]] std::optional<int> value() const
    {
        return value_;
    }
    /// Sets the number (clamped to >= 0) or clears it. Does nothing if the clamped value is the
    /// current one.
    void set_value(std::optional<int> value);

    /// Which severity color the fill comes from; Default (the accent) by default.
    [[nodiscard]] InfoBadgeSeverity severity() const
    {
        return severity_;
    }
    /// Sets the severity; redraws.
    void set_severity(InfoBadgeSeverity severity);

    /// The icon shown while value() is unset, sized to the badge: a glyph's code point and font
    /// at the badge's glyph size, or a path or picture fitted into the same square.
    [[nodiscard]] const IconSource& icon() const
    {
        return icon_;
    }
    /// Sets the icon; an empty source shows the plain dot while value() is unset.
    void set_icon(IconSource icon);

    /// A local fixed background, overriding the severity's color. nullopt when there is none or
    /// a theme resource is set.
    [[nodiscard]] const std::optional<Color>& background() const
    {
        return background_;
    }
    /// The local background as a theme resource, or nullptr.
    [[nodiscard]] ThemeColor background_resource() const
    {
        return background_resource_;
    }
    /// Sets or clears the local background and clears a theme-resource one; nullopt goes back to
    /// the severity's color. Redraws.
    void set_background(std::optional<Color> background);
    /// Sets the local background to a theme resource, re-read under every theme, and clears a
    /// fixed one. Redraws.
    void set_background(ThemeColor resource);

    /// The icon a severity shows: glyphs::InfoBadgeAttention for Attention, glyphs::InfoBarInformational
    /// for Informational, and glyphs::AcceptSymbol, glyphs::Important and glyphs::Cancel for
    /// Success, Caution and Critical. `Default` has no icon of its own and gets Attention's.
    [[nodiscard]] static IconGlyph severity_glyph(InfoBadgeSeverity severity);

    /// True: measuring reads only the badge's own state, the measurer and the theme.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

    // ---- automation ----
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Custom;
    }
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return value_ ? std::to_string(*value_) : std::string{};
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    std::optional<int>   value_{};
    InfoBadgeSeverity    severity_ = InfoBadgeSeverity::Default;
    IconSource           icon_{};
    std::optional<Color> background_{};
    ThemeColor           background_resource_ = nullptr;
};

/// A small count, icon or dot badge. Builds a ui::InfoBadgeControl.
///
/// Example:
/// @code
/// ui::Property<int> unread{3};
///
/// window.set_content(ui::HorizontalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::InfoBadge{.value    = unread},
///         ui::InfoBadge{.severity = ui::InfoBadgeSeverity::Critical,
///                       .icon     = ui::InfoBadgeControl::severity_glyph(ui::InfoBadgeSeverity::Critical)},
///         ui::InfoBadge{}, // a plain dot in the accent color
///     },
/// });
/// @endcode
struct InfoBadge
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<InfoBadge>` points at.
    using control_type = InfoBadgeControl;

    // ---- properties ----
    /// The number; unset renders the icon, or the dot form when there is none.
    Binding<std::optional<int>> value{};
    /// The severity the background color comes from.
    Binding<InfoBadgeSeverity> severity{InfoBadgeSeverity::Default};
    /// The icon shown while `value` is unset: a Symbol, a glyph, a path or a picture; empty shows the plain dot.
    Binding<IconSource> icon{};
    /// A local background over the severity's color: a fixed color or a theme resource, which
    /// follows theme switches. Unset keeps the severity's.
    Binding<Paint> background{};

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
    ControlOut<InfoBadge> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::InfoBadgeControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
