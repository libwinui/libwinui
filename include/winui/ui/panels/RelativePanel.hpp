// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
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

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ui
{

/// The placement constraints of one child of a ui::RelativePanelControl. A sibling reference is an
/// observer of another child of the same panel; nullptr leaves that constraint unset.
struct RelativePanelConstraints
{
    const Element* left_of                            = nullptr; ///< Right edge against this sibling's left edge.
    const Element* above                              = nullptr; ///< Bottom edge against this sibling's top edge.
    const Element* right_of                           = nullptr; ///< Left edge against this sibling's right edge.
    const Element* below                              = nullptr; ///< Top edge against this sibling's bottom edge.
    const Element* align_horizontal_center_with       = nullptr; ///< Horizontal center on this sibling's.
    const Element* align_vertical_center_with         = nullptr; ///< Vertical center on this sibling's.
    const Element* align_left_with                    = nullptr; ///< Left edge in line with this sibling's.
    const Element* align_top_with                     = nullptr; ///< Top edge in line with this sibling's.
    const Element* align_right_with                   = nullptr; ///< Right edge in line with this sibling's.
    const Element* align_bottom_with                  = nullptr; ///< Bottom edge in line with this sibling's.
    bool           align_left_with_panel              = false;   ///< Left edge on the panel's content edge.
    bool           align_top_with_panel               = false;   ///< Top edge on the panel's content edge.
    bool           align_right_with_panel             = false;   ///< Right edge on the panel's content edge.
    bool           align_bottom_with_panel            = false;   ///< Bottom edge on the panel's content edge.
    bool           align_horizontal_center_with_panel = false;   ///< Centered horizontally in the panel.
    bool           align_vertical_center_with_panel   = false;   ///< Centered vertically in the panel.
};

/// Lays out children relative to each other and to the panel's edges.
///
/// Each child carries a ui::RelativePanelConstraints (sixteen constraints). A child is measured
/// in the room its constraints leave it, then pinned: stretched between two anchored edges,
/// placed against one, centered, or in the top-left corner when nothing is set. On the same side,
/// a panel alignment wins over a sibling alignment, which wins over left_of/right_of. Siblings are
/// resolved before the children that refer to them, whatever order they were added in. A
/// constraint that would make a cycle, or that names a non-child, is ignored. The panel asks for
/// the widest and tallest dependency chain plus its border and padding. `ui::RelativePanel` is the
/// declarative form, referring to siblings by name.
///
/// Example:
/// @code
/// auto panel = std::make_unique<ui::RelativePanelControl>();
/// panel->set_padding(ui::Thickness{12.0f});
/// auto* title  = panel->add(std::make_unique<ui::TextControl>("Title"));
/// auto* ok     = panel->add(std::make_unique<ui::ButtonControl>("OK"), {.align_right_with_panel = true});
/// auto* cancel = panel->add(std::make_unique<ui::ButtonControl>("Cancel"), {.left_of = ok});
/// panel->add(std::make_unique<ui::TextControl>("Body"), {.below = title});
/// panel->set_constraints(ok, {.below = title, .align_right_with_panel = true}); // re-pin later
/// cancel->on_click([] { /* ... */ });
/// window.set_content(std::move(panel));
///
/// // Declaratively:
/// auto node = ui::build(ui::RelativePanel{.content = {
///     ui::RelativeItem{.name = "a", .content = ui::Text{.text = "A"}},
///     ui::RelativeItem{.right_of = "a", .content = ui::Text{.text = "B"}},
/// }});
/// @endcode
class RelativePanelControl final : public Panel
{
public:
    /// An empty panel with no border or padding.
    RelativePanelControl() = default;

    /// Takes ownership of `child` with its `constraints` and returns a non-owning observer.
    /// Constraints may name siblings added later (set them with `set_constraints()`).
    template <typename T>
    T* add(std::unique_ptr<T> child, RelativePanelConstraints constraints)
    {
        static_assert(std::is_base_of_v<Element, T>, "RelativePanel children must be Elements");
        T* observer = child.get();
        if (adopt_child(std::move(child)) != nullptr)
        {
            constraints_.push_back(constraints);
        }
        return observer;
    }
    /// The declarative overload: builds `spec` (or takes the node a ui::Spec holds) and adds it as
    /// add() does a control. Returns the built node as an Element, or nullptr for an empty
    /// ui::Spec, which adds nothing; a spec's `.control` field gives a typed observer.
    template <SpecLike S>
    Element* add(S&& spec, RelativePanelConstraints constraints)
    {
        auto node = std::forward<S>(spec).build();
        return node != nullptr ? add(std::move(node), constraints) : nullptr;
    }
    /// add() with no constraints set.
    template <typename T>
    T* add(std::unique_ptr<T> child)
    {
        return add(std::move(child), RelativePanelConstraints{});
    }
    /// The declarative add() with no constraints set.
    template <SpecLike S>
    Element* add(S&& spec)
    {
        return add(std::forward<S>(spec), RelativePanelConstraints{});
    }

    /// Replaces a child's constraints; an element that is not a child is ignored.
    void set_constraints(const Element* child, const RelativePanelConstraints& constraints);
    /// A child's constraints; all unset for an element that is not a child.
    [[nodiscard]] RelativePanelConstraints constraints(const Element* child) const;

    using Panel::border_brush;
    using Panel::border_brush_resource;
    using Panel::border_thickness;
    using Panel::corner_radius;
    using Panel::padding;
    using Panel::set_border_brush;
    using Panel::set_border_thickness;
    using Panel::set_corner_radius;
    using Panel::set_padding;
    /// True: measuring touches only the panel and its children. The children are measured in
    /// dependency order on one thread.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

protected:
    /// Resolves the dependency graph, measures each child in the room its constraints leave,
    /// and returns the widest and tallest chain plus border and padding.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Pins every child per its constraints inside the border and padding.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;


private:
    // One child's resolved references and layout state.
    struct Node
    {
        Element*                        element                      = nullptr;
        const RelativePanelConstraints* c                            = nullptr;
        Node*                           left_of                      = nullptr;
        Node*                           above                        = nullptr;
        Node*                           right_of                     = nullptr;
        Node*                           below                        = nullptr;
        Node*                           align_horizontal_center_with = nullptr;
        Node*                           align_vertical_center_with   = nullptr;
        Node*                           align_left_with              = nullptr;
        Node*                           align_top_with               = nullptr;
        Node*                           align_right_with             = nullptr;
        Node*                           align_bottom_with            = nullptr;
        Size                            desired{};
        Rect                            measure_rect{};
        Rect                            arrange_rect{};
        bool                            pending               = false;
        bool                            measured              = false;
        bool                            arranged_horizontally = false;
        bool                            arranged_vertically   = false;
        bool                            horizontal_leaf       = true;
        bool                            vertical_leaf         = true;

        [[nodiscard]] bool left_anchored() const;
        [[nodiscard]] bool top_anchored() const;
        [[nodiscard]] bool right_anchored() const;
        [[nodiscard]] bool bottom_anchored() const;
        [[nodiscard]] bool horizontal_center_anchored() const;
        [[nodiscard]] bool vertical_center_anchored() const;
    };

    void               generate_graph() const;
    void               measure_node(Node* node, Size available, const MeasureContext& context) const;
    void               arrange_node_horizontally(Node* node, Size final_size) const;
    void               arrange_node_vertically(Node* node, Size final_size) const;
    static void        measure_rect_horizontally(const Node* node, Size available, float& x, float& width);
    static void        measure_rect_vertically(const Node* node, Size available, float& y, float& height);
    static void        arrange_rect_horizontally(const Node* node, float& x, float& width);
    static void        arrange_rect_vertically(const Node* node, float& y, float& height);
    [[nodiscard]] Size calculate_desired_size() const;
    void               accumulate_positive_width(const Node* node, float x) const;
    void               accumulate_negative_width(const Node* node, float x) const;
    void               accumulate_positive_height(const Node* node, float y) const;
    void               accumulate_negative_height(const Node* node, float y) const;

    std::vector<RelativePanelConstraints> constraints_; // parallel to children_

    mutable std::vector<Node> nodes_;
    mutable Size              resolved_for_{};
    mutable float             min_x_      = 0.0f;
    mutable float             max_x_      = 0.0f;
    mutable float             min_y_      = 0.0f;
    mutable float             max_y_      = 0.0f;
    mutable bool              min_capped_ = false;
    mutable bool              max_capped_ = false;
};

/// One child of a RelativePanel. `name` is what a sibling's constraints refer to it by. A
/// constraint must name a sibling in the same panel. A name that matches none is a bug, caught by
/// an assert in a debug build and otherwise left unset. Each string field is the
/// ui::RelativePanelConstraints member of the same name; see ui::RelativePanel for an example.
struct RelativeItem
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    std::string                                       name{};     ///< This child's name, for siblings to refer to.
    std::string                                       left_of{};  ///< Sit left of the named sibling.
    std::string                                       above{};    ///< Sit above the named sibling.
    std::string                                       right_of{}; ///< Sit right of the named sibling.
    std::string                                       below{};    ///< Sit below the named sibling.
    std::string align_horizontal_center_with{};                   ///< Share the named sibling's horizontal center.
    std::string align_vertical_center_with{};                     ///< Share the named sibling's vertical center.
    std::string align_left_with{};                                ///< Align left edges with the named sibling.
    std::string align_top_with{};                                 ///< Align top edges with the named sibling.
    std::string align_right_with{};                               ///< Align right edges with the named sibling.
    std::string align_bottom_with{};                              ///< Align bottom edges with the named sibling.
    bool        align_left_with_panel              = false;       ///< Pin to the panel's left edge.
    bool        align_top_with_panel               = false;       ///< Pin to the panel's top edge.
    bool        align_right_with_panel             = false;       ///< Pin to the panel's right edge.
    bool        align_bottom_with_panel            = false;       ///< Pin to the panel's bottom edge.
    bool        align_horizontal_center_with_panel = false;       ///< Center horizontally in the panel.
    bool        align_vertical_center_with_panel   = false;       ///< Center vertically in the panel.
    Spec        content{};                                        ///< The placed child.
};

/// A RelativePanel's children, built; the names are resolved when the panel is. Callers write it
/// as a braced list of `ui::RelativeItem{...}`.
class RelativeItemList
{
public:
    /// One built child with its constraints; the node is already owned by the list.
    struct Placed
    {
        RelativeItem             names; ///< The constraints; its content is already taken.
        std::unique_ptr<Element> node;  ///< The built child.
    };

    /// An empty list.
    RelativeItemList() = default;
    /// Builds every item's content and takes ownership of it, keeping the constraints.
    template <typename... S>
        requires(sizeof...(S) > 0 && (std::same_as<S, RelativeItem> && ...))
    RelativeItemList(S&&... items) // NOLINT(google-explicit-constructor)
    {
        items_.reserve(sizeof...(S));
        (add(std::move(items)), ...);
    }
    RelativeItemList(const RelativeItemList&)            = delete;
    RelativeItemList& operator=(const RelativeItemList&) = delete;
    RelativeItemList(RelativeItemList&&)                 = default;
    RelativeItemList& operator=(RelativeItemList&&)      = default;

    /// The placed children, in declaration order.
    std::vector<Placed> items_;

    /// Builds `item`'s content and appends it, as one more entry of the braced list would.
    void add(RelativeItem item)
    {
        Placed placed{};
        placed.node                                     = item.content.take();
        placed.names.name                               = item.name;
        placed.names.left_of                            = item.left_of;
        placed.names.above                              = item.above;
        placed.names.right_of                           = item.right_of;
        placed.names.below                              = item.below;
        placed.names.align_horizontal_center_with       = item.align_horizontal_center_with;
        placed.names.align_vertical_center_with         = item.align_vertical_center_with;
        placed.names.align_left_with                    = item.align_left_with;
        placed.names.align_top_with                     = item.align_top_with;
        placed.names.align_right_with                   = item.align_right_with;
        placed.names.align_bottom_with                  = item.align_bottom_with;
        placed.names.align_left_with_panel              = item.align_left_with_panel;
        placed.names.align_top_with_panel               = item.align_top_with_panel;
        placed.names.align_right_with_panel             = item.align_right_with_panel;
        placed.names.align_bottom_with_panel            = item.align_bottom_with_panel;
        placed.names.align_horizontal_center_with_panel = item.align_horizontal_center_with_panel;
        placed.names.align_vertical_center_with_panel   = item.align_vertical_center_with_panel;
        items_.push_back(std::move(placed));
    }
};

/// Children placed relative to each other and to the panel. Builds a ui::RelativePanelControl.
///
/// Example:
/// @code
/// window.set_content(ui::RelativePanel{
///     .padding  = ui::Thickness{12.0f},
///     .width    = 280.0f,
///     .content  = {
///         ui::RelativeItem{.name                  = "image",
///                          .align_left_with_panel = true,
///                          .align_top_with_panel  = true,
///                          .content = ui::Image{.source = ui::Bitmap::read("cover.jpg"), .width = 80.0f}},
///         ui::RelativeItem{.name = "title", .right_of = "image", .align_top_with = "image",
///                          .content = ui::Text{.text = "Title", .role = ui::TextRole::BodyStrong}},
///         ui::RelativeItem{.right_of = "image", .below = "title", .content = ui::Text{.text = "Subtitle"}},
///     },
/// });
/// @endcode
struct RelativePanel
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<RelativePanel>` points at.
    using control_type = RelativePanelControl;

    // ---- properties ----
    /// Background: a Color, or a ThemeColor read from the theme; empty (the default) draws none.
    Binding<Paint> background{};
    /// Border color, a Color or a ThemeColor; empty (the default) draws no border.
    Binding<Paint> border_brush{};
    /// Border width per edge, DIPs.
    Binding<Thickness> border_thickness{};
    /// Corner radius, DIPs.
    Binding<float> corner_radius{0.0f};
    /// Padding between the border and the children, DIPs.
    Binding<Thickness> padding{};

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
    ControlOut<RelativePanel> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The constrained children; see ui::RelativeItem.
    RelativeItemList content{};

    /// Consumes the spec and returns the built ui::RelativePanelControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
