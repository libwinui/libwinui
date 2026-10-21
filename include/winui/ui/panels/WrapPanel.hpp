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
#include "winui/ui/panels/Panel.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
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

/// Whether a WrapPanel stretches its last child.
enum class WrapPanelItemsStretch
{
    None, ///< every child keeps its desired length along the line (the default)
    Last, ///< the panel's last child is widened to the end of its line
};

/// A panel that places its children one after another along a line and starts a new line when
/// the next child would cross the far edge. Use it for tag chips, a toolbar of variable-width
/// buttons, or a gallery of cards that reflows with the window.
///
/// Along the orientation children sit item_spacing() apart; lines sit line_spacing() apart,
/// each as tall as its tallest child, and every child in a line is arranged at the line's full
/// height. The panel asks for its widest line by the sum of its lines, plus the padding. A
/// first child wider than the whole panel does not push an empty line in front of itself.
/// Built declaratively by `ui::WrapPanel`.
///
/// Example:
/// @code
/// // Declarative:
/// window.set_content(ui::WrapPanel{
///     .item_spacing = 8.0f,
///     .line_spacing = 8.0f,
///     .padding      = ui::Thickness{12.0f},
///     .content      = {
///         ui::Button{.text = "Alpha"},
///         ui::Button{.text = "Beta"},
///         ui::Button{.text = "A much longer gamma"},
///         ui::Button{.text = "Delta"},
///     },
/// });
///
/// // Imperative:
/// auto panel = std::make_unique<ui::WrapPanelControl>();
/// panel->set_orientation(ui::Orientation::Horizontal);
/// panel->set_item_spacing(8.0f);
/// panel->set_line_spacing(4.0f);
/// panel->set_items_stretch(ui::WrapPanelItemsStretch::Last);
/// for (const char* tag : {"red", "green", "blue", "cyan", "magenta"})
/// {
///     panel->add(std::make_unique<ui::ButtonControl>(tag));
/// }
/// window.set_content(std::move(panel));
/// @endcode
class WrapPanelControl final : public Panel
{
public:
    /// An empty horizontal panel with no spacing, no padding and no stretch.
    WrapPanelControl() = default;

    /// Appends `child`, taking ownership, and returns a non-owning observer to it. Children are
    /// placed in the order added.
    template <typename T>
    T* add(std::unique_ptr<T> child)
    {
        static_assert(std::is_base_of_v<Element, T>, "WrapPanel children must be Elements");
        T* observer = child.get();
        adopt_child(std::move(child));
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

    /// The flow direction: Horizontal (rows, the default) or Vertical (columns).
    [[nodiscard]] Orientation orientation() const
    {
        return orientation_;
    }
    /// Changes the flow direction; invalidates layout.
    void set_orientation(Orientation orientation)
    {
        orientation_ = orientation;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The gap between two children in a line, in DIPs. Default 0.
    [[nodiscard]] float item_spacing() const
    {
        return item_spacing_;
    }
    /// Sets the gap between children; invalidates layout.
    void set_item_spacing(float spacing)
    {
        item_spacing_ = spacing;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The gap between two lines, in DIPs. Default 0.
    [[nodiscard]] float line_spacing() const
    {
        return line_spacing_;
    }
    /// Sets the gap between lines; invalidates layout.
    void set_line_spacing(float spacing)
    {
        line_spacing_ = spacing;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Whether the last child is widened to the end of its line. Default None.
    [[nodiscard]] WrapPanelItemsStretch items_stretch() const
    {
        return items_stretch_;
    }
    /// Sets whether the last child is widened; invalidates layout.
    void set_items_stretch(WrapPanelItemsStretch stretch)
    {
        items_stretch_ = stretch;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
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

    /// True: measuring touches only the panel and its children.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

protected:
    /// Lays the children out in lines against `available` (less the padding) and returns the
    /// widest line by the sum of the lines, plus the padding.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Wraps against `final_rect`'s size and places each child in its slot, every child as
    /// tall as its line and the last one stretched when items_stretch() is Last.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;


private:
    // One child's slot in "UV" space: U along the orientation, V across it.
    struct UvRect
    {
        float u      = 0.0f;
        float v      = 0.0f;
        float u_size = 0.0f;
        float v_size = 0.0f;
    };
    struct Line
    {
        std::vector<UvRect> items;
        float               v_size = 0.0f; // the tallest child in the line
    };

    /// Lays the children out in lines against `extent` (U, V; infinity unbounded) and
    /// returns the size the panel needs.
    [[nodiscard]] Size update_rows(Size extent) const;

    Orientation           orientation_   = Orientation::Horizontal;
    float                 item_spacing_  = 0.0f;
    float                 line_spacing_  = 0.0f;
    WrapPanelItemsStretch items_stretch_ = WrapPanelItemsStretch::None;

    mutable std::vector<Size> desired_;
    mutable std::vector<Line> lines_;
    mutable Size              panel_desired_{};
};

/// Children in lines that wrap. Builds a ui::WrapPanelControl.
///
/// Children keep their desired size; a line breaks when the next child would not fit.
///
/// Example:
/// @code
/// ui::Spec chip(const char* text) { return ui::Button{.text = text}; }
///
/// window.set_content(ui::WrapPanel{
///     .item_spacing = 8,
///     .line_spacing = 8,
///     .padding      = ui::Thickness{12.0f},
///     .content      = {chip("Alpha"), chip("Beta item"), chip("Gamma"), chip("Delta item")},
/// });
/// @endcode
struct WrapPanel
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<WrapPanel>` points at.
    using control_type = WrapPanelControl;

    // ---- properties ----
    /// Horizontal (the default) fills rows and wraps downward; Vertical fills columns.
    Binding<Orientation> orientation{Orientation::Horizontal};
    /// Gap between neighbors along a line, DIPs.
    Binding<float> item_spacing{0.0f};
    /// Gap between lines, DIPs.
    Binding<float> line_spacing{0.0f};
    /// None keeps every child's length; Last widens the last child to the end of its line.
    Binding<WrapPanelItemsStretch> items_stretch{WrapPanelItemsStretch::None};
    /// Inset between the panel's edge and the children, DIPs.
    Binding<Thickness> padding{Thickness{}};
    /// Background: a Color, or a ThemeColor read from the theme; empty (the default) draws none.
    Binding<Paint> background{};
    /// Border color, a Color or a ThemeColor; empty (the default) draws no border.
    Binding<Paint> border_brush{};
    /// Border width per edge, DIPs; it insets the children like padding.
    Binding<Thickness> border_thickness{Thickness{}};
    /// Corner radius of the background and the border, DIPs.
    Binding<float> corner_radius{0.0f};

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
    ControlOut<WrapPanel> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The wrapped children, in reading order.
    SpecList content{};

    /// Consumes the spec and returns the built ui::WrapPanelControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
