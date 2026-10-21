// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
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
#include <string>
#include <utility>

namespace ui
{

/// A header over a group of related controls, with the group framed.
///
/// It takes no input and no focus of its own, so Tab moves straight to the controls inside. It
/// groups only visually. Radio buttons inside still need a RadioGroup or a RadioButtons control to
/// be exclusive.
///
/// Example:
/// @code
/// auto box = std::make_unique<ui::GroupBoxControl>("Appearance");
/// box->set_child(std::make_unique<ui::CheckBoxControl>("Show the status bar"));
/// window.set_content(std::move(box));
///
/// // Declaratively:
/// window.set_content(ui::GroupBox{
///     .header  = "Appearance",
///     .content = ui::CheckBox{.text = "Show the status bar"},
/// });
/// @endcode
class GroupBoxControl final : public ContentHost
{
public:
    /// Returns false, so the group box is hit anywhere in its bounds, even where it draws nothing.
    [[nodiscard]] bool hit_test_transparent() const override
    {
        return false;
    }
    /// A group box with no header and no content.
    GroupBoxControl() = default;
    /// A group box headed `header`.
    explicit GroupBoxControl(std::string header) : header_(std::move(header))
    {
    }

    /// The header text. Empty draws no header, and the frame or card starts at the top.
    [[nodiscard]] const std::string& header() const
    {
        return header_;
    }
    /// Sets the header text. Updates layout and redraws.
    void set_header(std::string header)
    {
        header_ = std::move(header);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The header names the group when accessibility().name doesn't.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return header_;
    }

    /// Returns true, since measuring touches only the header text and the child.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

protected:
    /// The child's desired size plus the header and the theme's frame or card insets.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Gives the child the rect inside the frame or card.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws the header and the frame or card, then the child.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    // What measure resolved from the theme, for arrange and render.
    struct Metrics
    {
        float     header_height = 0.0f; // the header's line, 0 without a header
        float     header_width  = 0.0f;
        float     frame_top     = 0.0f; // where the frame or card starts, from the top
        Thickness padding{};            // the content's insets, top measured from the top
    };
    [[nodiscard]] Metrics metrics(const TextMeasurer& measurer, const Theme& theme) const;

    std::string     header_;
    mutable Metrics metrics_{};
};

/// A header over a group of related controls, with the group framed. Builds a ui::GroupBoxControl.
///
/// Example:
/// @code
/// window.set_content(ui::GroupBox{
///     .header  = "Appearance",
///     .content = ui::VerticalStack{
///         .spacing = 8,
///         .content = {
///             ui::CheckBox{.text = "Show the status bar"},
///             ui::CheckBox{.text = "Show line numbers"},
///         },
///     },
/// });
/// @endcode
struct GroupBox
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<GroupBox>` points at.
    using control_type = GroupBoxControl;

    // ---- properties ----
    /// The header text. Empty draws no header.
    Binding<std::string> header{};

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
    ControlOut<GroupBox> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The grouped content.
    Spec content{};

    /// Consumes the spec and returns the built ui::GroupBoxControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
