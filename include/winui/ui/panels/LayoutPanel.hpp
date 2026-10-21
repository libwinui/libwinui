// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/layouts/Layout.hpp"
#include "winui/ui/layouts/StackLayout.hpp"
#include "winui/ui/panels/Panel.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/LayoutSpec.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ui
{

/// A panel whose measure and arrange are delegated to an attachable Layout.
///
/// Use it for a fixed set of children with a layout that no built-in panel offers. That is a
/// custom NonVirtualizingLayout, or a StackLayoutEngine, TileLayoutEngine, WrapLayoutEngine or
/// LinedFlowLayoutEngine applied to ordinary children, where every child counts as realized.
/// Border and padding come off the room before the layout sees it. With no layout, every child
/// fills the whole content box, stacked on top of each other.
///
/// Example:
/// @code
/// // Declarative:
/// window.set_content(ui::LayoutPanel{
///     .layout   = ui::TileLayout{},
///     .padding  = ui::Thickness{12.0f},
///     .content  = {ui::Text{.text = "A"}, ui::Text{.text = "B"}, ui::Text{.text = "C"}},
/// });
///
/// // Imperative:
/// auto  panel = std::make_unique<ui::LayoutPanelControl>();
/// auto* stack = panel->set_layout(std::make_unique<ui::StackLayoutEngine>());
/// stack->set_spacing(8.0f);
/// panel->add(std::make_unique<ui::TextControl>("First"));
/// panel->add(std::make_unique<ui::TextControl>("Second"));
/// panel->set_padding(ui::Thickness{12.0f});
/// window.set_content(std::move(panel));
/// @endcode
class LayoutPanelControl final : public Panel, private LayoutOwner
{
public:
    /// An empty panel with no layout.
    LayoutPanelControl() = default;
    ~LayoutPanelControl() override;

    /// Takes ownership of `child`, appends it after the existing children, and returns a
    /// non-owning observer. Updates the layout.
    template <typename T>
    T* add(std::unique_ptr<T> child)
    {
        static_assert(std::is_base_of_v<Element, T>, "LayoutPanel children must be Elements");
        T* observer = child.get();
        if (adopt_child(std::move(child)) != nullptr)
        {
            desired_.emplace_back();
        }
        return observer;
    }
    /// The declarative overload: builds `spec` (or takes the node a ui::Spec holds) and adds it as
    /// add() does a control. Returns the built node as an Element, or nullptr for an empty
    /// ui::Spec, which adds nothing; a spec's `.control` field gives a typed observer.
    template <SpecLike S>
    Element* add(S&& spec)
    {
        auto node = std::forward<S>(spec).build();
        return node != nullptr ? add(std::move(node)) : nullptr;
    }

    /// Takes ownership of the layout, destroying any previous one, and returns an observer to it.
    /// nullptr leaves the panel with none.
    template <typename T>
    T* set_layout(std::unique_ptr<T> layout)
    {
        static_assert(std::is_base_of_v<Layout, T>, "a LayoutPanel's layout must be a Layout");
        T* observer = layout.get();
        replace_layout(std::move(layout));
        return observer;
    }
    /// The current layout (non-owning), or nullptr when the panel has none.
    [[nodiscard]] Layout* layout() const
    {
        return layout_.get();
    }

    using Panel::border_brush;
    using Panel::border_brush_resource;
    using Panel::border_thickness;
    using Panel::corner_radius;
    using Panel::padding;
    using Panel::set_border_brush;
    using Panel::set_border_thickness;
    using Panel::set_corner_radius;
    using Panel::set_padding;
    /// True without a layout, otherwise what the layout answers (Layout::parallel_measure_safe()).
    [[nodiscard]] bool parallel_measure_safe() const override;

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;


private:
    friend class PanelLayoutContext;
    friend class PanelVirtualizingContext;

    void replace_layout(std::unique_ptr<Layout> layout);
    void layout_invalidated(bool measure) override
    {
        mark_dirty(measure ? (DirtyFlags::Layout | DirtyFlags::Visual) : (DirtyFlags::Arrange | DirtyFlags::Visual));
    }

    std::unique_ptr<Layout>   layout_;
    mutable std::vector<Size> desired_; // parallel to children_
    // Kept from the last measure: a layout may measure again while arranging.
    mutable const TextMeasurer* measurer_ = nullptr;
    mutable const Theme*        theme_    = nullptr;
};

/// Children measured and arranged by a Layout object. Builds a ui::LayoutPanelControl. The panel
/// takes ownership of `.layout`; reach it again through `.control`'s layout().
///
/// The counterpart of ui::ForEach for hand-written children: every child is realized, and any
/// ui::Layout (a stock one or the app's own subclass) places them.
///
/// Example:
/// @code
/// auto grid = std::make_unique<ui::TileLayoutEngine>();
/// grid->set_min_row_spacing(8.0f);
/// grid->set_min_column_spacing(8.0f);
///
/// window.set_content(ui::LayoutPanel{
///     .layout   = std::move(grid),
///     .padding  = ui::Thickness{12.0f},
///     .content  = {
///         ui::Button{.text = "One"},
///         ui::Button{.text = "Two"},
///         ui::Button{.text = "Three"},
///     },
/// });
/// @endcode
struct LayoutPanel
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<LayoutPanel>` points at.
    using control_type = LayoutPanelControl;

    // ---- properties ----
    /// The layout that measures and arranges the children; the panel takes ownership.
    LayoutSpec layout{};
    /// Background: a Color, or a ThemeColor read from the theme; empty (the default) draws none.
    Binding<Paint> background{};
    /// Border color, a Color or a ThemeColor; empty (the default) draws no border.
    Binding<Paint> border_brush{};
    /// Border width per edge, DIPs.
    Binding<Thickness> border_thickness{};
    /// Corner radius, DIPs.
    Binding<float> corner_radius{0.0f};
    /// Padding between the border and the laid-out children, DIPs.
    Binding<Thickness> padding{Thickness{}};

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
    Binding<bool> enabled{true}; ///< False disables everything inside: no input, the disabled look, out of the tab ring.
    Accessibility accessibility{}; ///< Screen reader name and description, and a test id.

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
    ControlOut<LayoutPanel> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The children, in the order the layout sees them.
    SpecList content{};

    /// Consumes the spec and returns the built ui::LayoutPanelControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
