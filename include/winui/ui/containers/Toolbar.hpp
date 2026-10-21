// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ui
{

/// A horizontal strip of commands, usually Buttons, with optional separators between groups.
///
/// The items are laid out in a row with 4 DIPs of spacing, like a horizontal StackControl. The
/// toolbar itself never takes focus, and each item joins the tab order as in a StackControl. There
/// is no overflow handling, so a toolbar wider than its slot overflows like a StackControl. For a
/// command bar with an overflow menu, use ui::CommandBarControl.
///
/// Example:
/// @code
/// auto  toolbar = std::make_unique<ui::ToolbarControl>();
/// auto* cut     = toolbar->add(std::make_unique<ui::ButtonControl>("Cut"));
/// toolbar->add(std::make_unique<ui::ButtonControl>("Copy"));
/// toolbar->add_separator();
/// toolbar->add(std::make_unique<ui::ButtonControl>("Undo"));
/// cut->on_click([] { /* ... */ });
/// window.set_content(std::move(toolbar));
///
/// // Or declaratively:
/// window.set_content(ui::Toolbar{
///     .content = {
///         ui::Button{.text = "Cut", .on_click = [] { /* ... */ }},
///         ui::Button{.text = "Copy"},
///         ui::Separator{},
///         ui::Button{.text = "Undo"},
///     },
/// });
/// @endcode
class ToolbarControl final : public Element
{
public:
    /// An empty toolbar with 4 DIPs between items.
    ToolbarControl();

    /// Takes ownership of a child, appends it at the end and returns a non-owning observer to it,
    /// like StackControl::add().
    template <typename T>
    T* add(std::unique_ptr<T> child)
    {
        static_assert(std::is_base_of_v<Element, T>, "Toolbar children must be Elements");
        return panel_->add(std::move(child));
    }

    /// Appends a thin vertical divider line that takes no focus, the same element make_separator()
    /// returns.
    void add_separator();

    /// Makes the same separator as a standalone ui::SeparatorControl, for adding it yourself.
    /// In a declared child list, write `ui::Separator{}`.
    [[nodiscard]] static std::unique_ptr<Element> make_separator();

    /// The gap between adjacent items in DIPs, 4 by default.
    [[nodiscard]] float spacing() const
    {
        return panel_->spacing();
    }
    /// Changes the gap. Updates layout.
    void set_spacing(float spacing)
    {
        panel_->set_spacing(spacing);
    }
    /// Visits the internal StackControl that lays the items out.
    void for_each_child(ChildVisitor visit) const override
    {
        visit(*panel_);
    }

    // ---- automation ----
    /// Reports the toolbar's items and separators as its direct children. The internal
    /// StackControl that for_each_child() visits never appears in the automation tree.
    void automation_children(std::vector<Element*>& out) const override
    {
        for (const auto& child : panel_->children())
        {
            out.push_back(child.get());
        }
    }
    /// Exposed to UI Automation as a toolbar.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::ToolBar;
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    std::unique_ptr<StackControl> panel_; // internal layout, never exposed
};

/// A command strip. Builds a ui::ToolbarControl. Use ui::Separator{} between groups.
///
/// Example:
/// @code
/// window.set_content(ui::Toolbar{.content = {
///     ui::Button{.text = "Open", .on_click = [&] { open_file(); }},
///     ui::Separator{},
///     ui::Button{.text = "Save", .on_click = [&] { save_document(); }},
/// }});
/// @endcode
struct Toolbar
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Toolbar>` points at.
    using control_type = ToolbarControl;

    // ---- properties ----
    /// The gap between adjacent commands, DIPs.
    Binding<float> spacing{4.0f};

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
    ControlOut<Toolbar> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The commands, left to right.
    SpecList content{};

    /// Consumes the spec and returns the built ui::ToolbarControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

/// The divider a Toolbar draws between command groups. It takes no focus, and it is 9 DIPs wide
/// and a control's height. It draws an etched groove, a dark line beside a light one, or a 1 px
/// flat line where the theme draws dividers flat. ui::Separator builds it and
/// ui::ToolbarControl::add_separator() adds it.
class SeparatorControl final : public Element
{
public:
    /// A separator. It has no state.
    SeparatorControl() = default;

    /// Exposed to UI Automation as a separator.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Separator;
    }

protected:
    /// A fixed width, and the height of a line of body text plus the theme's padding. It doesn't
    /// take the available height, which would make an Auto-sized row holding the toolbar grow.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Draws the groove or the flat line, centered across the width.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
};

/// The divider a Toolbar draws between command groups. Builds a ui::SeparatorControl.
///
/// Example:
/// @code
/// ui::Toolbar{.content = {ui::Button{.text = "Cut"}, ui::Separator{}, ui::Button{.text = "Undo"}}}
/// @endcode
struct Separator
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Separator>` points at.
    using control_type = SeparatorControl;

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
    ControlOut<Separator> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::SeparatorControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
