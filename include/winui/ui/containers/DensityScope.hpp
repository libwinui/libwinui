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
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>

namespace ui
{

/// The density a DensityScope applies.
enum class Density
{
    Standard, ///< The theme's own control sizing. The scope changes nothing.
    Compact,  ///< Theme::ControlSizing::compact(), with smaller minimum heights and paddings.
};

/// A wrapper that measures and draws its one child at compact or standard control density, which
/// applies the compact sizing to one subtree.
///
/// The child sees a copy of the theme whose `sizing` is Theme::ControlSizing::compact(). TextBox,
/// the controls built on it and ComboBox shrink, and everything else keeps its size.
/// Density::Standard passes the theme through unchanged. Popups opened from inside the scope are
/// drawn in the window's own theme, at standard density. Input, focus and ticks pass through to
/// the child.
///
/// Example:
/// @code
/// // Declarative: a compact form, switchable at run time through a Property.
/// ui::Property<ui::Density> density{ui::Density::Compact};
/// window.set_content(ui::DensityScope{
///     .density = density,
///     .content = ui::VerticalStack{
///         .spacing  = 8,
///         .content  = {
///             ui::TextBox{.header = "Name"},
///             ui::ComboBox{.items = {"Red", "Green", "Blue"}},
///         },
///     },
/// });
///
/// // Imperative: the scope owns its child through ContentHost::set_child().
/// auto scope = std::make_unique<ui::DensityScopeControl>(ui::Density::Compact);
/// ui::TextBoxControl* box = scope->set_child(std::make_unique<ui::TextBoxControl>());
/// const ui::Theme& inside = scope->scoped(ui::Theme::default_light());
/// // inside.sizing.text_control_min_height == 24, down from 32
/// @endcode
class DensityScopeControl final : public ContentHost
{
public:
    /// A compact scope with no child yet.
    DensityScopeControl() = default;
    /// A scope at `density`, with no child yet.
    explicit DensityScopeControl(Density density) : density_(density)
    {
    }

    /// The density applied to the child. Density::Compact by default.
    [[nodiscard]] Density density() const
    {
        return density_;
    }
    /// Changes the density. Updates layout and redraws, since every control underneath measures
    /// again against the new sizing.
    void set_density(Density density)
    {
        density_ = density;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The theme the subtree sees. At Density::Standard that is `base` itself, otherwise a copy of
    /// it at compact density, kept in the scope and valid until the next call.
    [[nodiscard]] const Theme& scoped(const Theme& base) const;
    /// Deleted, since at Density::Standard the result is `base` itself, which a temporary would
    /// leave dangling.
    const Theme& scoped(const Theme&& base) const = delete;

protected:
    /// Measures the child against scoped(theme).
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Draws the child against scoped(theme).
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    Density                      density_ = Density::Compact;
    mutable std::optional<Theme> compact_{};
};

/// Shows its child at compact or standard control density, which applies the compact control
/// sizes to a whole subtree. Builds a ui::DensityScopeControl.
///
/// Example:
/// @code
/// ui::Property<bool> compact{true};
///
/// window.set_content(ui::DensityScope{
///     .density = ui::bind(compact, [](bool c) { return c ? ui::Density::Compact : ui::Density::Standard; }),
///     .content = ui::VerticalStack{.spacing = 8, .content = {
///         ui::TextBox{.placeholder_text = "Name"},
///         ui::ComboBox{.items = {"One", "Two"}},
///         ui::Button{.text = "Submit"},
///     }},
/// });
/// @endcode
struct DensityScope
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<DensityScope>` points at.
    using control_type = DensityScopeControl;

    // ---- properties ----
    /// Compact, the default, shrinks control heights and paddings. Standard changes nothing.
    Binding<Density> density{Density::Compact};

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
    ControlOut<DensityScope> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The subtree the density applies to.
    Spec content{};

    /// Consumes the spec and returns the built ui::DensityScopeControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
