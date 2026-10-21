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

/// Children placed at absolute left/top offsets (DIPs) from its top-left corner, drawn in
/// z-index order.
///
/// Every child is measured with unbounded room and arranged at its own desired size. The Canvas
/// itself asks for no room at all. It takes the size its width/height or its slot give it. It
/// does not clip, so a child placed past its edge still draws there. Children draw lowest
/// z-index first, ties in the order added, and the topmost child gets the wheel first.
/// The background comes from Panel::set_background().
///
/// Example:
/// @code
/// auto canvas = std::make_unique<ui::CanvasControl>();
/// canvas->set_width(140.0f);
/// canvas->set_height(140.0f);
/// canvas->set_background(ui::Color::from_rgba(128, 128, 128));
///
/// auto square = std::make_unique<ui::RectangleControl>();
/// square->set_width(40.0f);
/// square->set_height(40.0f);
/// square->set_fill(ui::Color::from_rgba(255, 0, 0));
/// ui::RectangleControl* red = canvas->add(std::move(square), 20.0f, 30.0f);
/// canvas->set_z_index(red, 5); // drawn above every child with a lower z-index
/// canvas->set_left(red, 60.0f); // moves it and updates the layout
/// window.set_content(std::move(canvas));
///
/// // Declaratively, with bindable placements:
/// ui::Property<float> left{20.0f};
/// window.set_content(ui::Canvas{
///     .width    = 140.0f,
///     .height   = 140.0f,
///     .content  = {
///         ui::CanvasItem{.left    = left,
///          .top     = 30.0f,
///          .content = ui::Rectangle{.fill = ui::colors::Black, .width = 40.0f, .height = 40.0f}},
///         ui::CanvasItem{.left = 60.0f, .top = 60.0f, .z_index = 1, .content = ui::Text{.text = "On top"}},
///     },
/// });
/// @endcode
class CanvasControl final : public Panel
{
public:
    /// An empty canvas; with no width/height set it takes whatever its slot gives it.
    CanvasControl() = default;

    /// Takes ownership of a child placed at (`left`, `top`), DIPs from the canvas' top-left, at
    /// `z_index`, and returns a non-owning observer, as StackControl::add() does. Updates the
    /// layout. A null `child` is ignored and returns nullptr.
    template <typename T>
    T* add(std::unique_ptr<T> child, float left = 0.0f, float top = 0.0f, int z_index = 0)
    {
        static_assert(std::is_base_of_v<Element, T>, "Canvas children must be Elements");
        T* observer = child.get();
        if (adopt_child(std::move(child)) != nullptr)
        {
            placements_.push_back(Placement{left, top, z_index});
        }
        return observer;
    }
    /// The declarative overload: builds `spec` (or takes the node a ui::Spec holds) and adds it as
    /// add() does a control. Returns the built node as an Element, or nullptr for an empty
    /// ui::Spec, which adds nothing; a spec's `.control` field gives a typed observer.
    template <SpecLike S>
    Element* add(S&& spec, float left = 0.0f, float top = 0.0f, int z_index = 0)
    {
        auto node = std::forward<S>(spec).build();
        return node != nullptr ? add(std::move(node), left, top, z_index) : nullptr;
    }

    /// Sets the left offset of `child`, DIPs, and updates the layout. Ignored for an element that
    /// is not a child of this canvas.
    void set_left(const Element* child, float left);
    /// Sets the top offset of `child`, DIPs, and updates the layout. Ignored for a non-child.
    void set_top(const Element* child, float top);
    /// Sets the z-index of `child`. A higher z-index draws later, on top, and gets input first.
    /// Redraws. Ignored for a non-child.
    void set_z_index(const Element* child, int z_index);
    /// The left offset of `child`, DIPs; 0 for an element that is not a child of this canvas.
    [[nodiscard]] float left(const Element* child) const;
    /// The top offset of `child`, DIPs; 0 for a non-child.
    [[nodiscard]] float top(const Element* child) const;
    /// The z-index of `child`; 0 for a non-child.
    [[nodiscard]] int z_index(const Element* child) const;

    /// True: measuring touches only the canvas and its children.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }

protected:
    /// The children sorted by z-index (stable, so ties keep insertion order), bottom first.
    [[nodiscard]] std::vector<Element*> render_order() const override;
    /// Measures every child with unbounded room and asks for zero itself.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places each child at its offset from `final_rect`'s top-left, at its desired size.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;


private:
    struct Placement
    {
        float left    = 0.0f;
        float top     = 0.0f;
        int   z_index = 0;
    };
    [[nodiscard]] Placement*       placement_of(const Element* child);
    [[nodiscard]] const Placement* placement_of(const Element* child) const;

    std::vector<Placement>    placements_; // parallel to children_
    mutable std::vector<Size> desired_;
};

/// One child of a Canvas with its left and top offsets and z-index, each bindable.
/// Written inline in a ui::Canvas's `.content`; see ui::Canvas for an example.
struct CanvasItem
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    Binding<float>                                    left{0.0f}; ///< Offset from the canvas's left, DIPs.
    Binding<float>                                    top{0.0f};  ///< Offset from the canvas's top, DIPs.
    Binding<int>                                      z_index{0}; ///< Higher draws on top.
    Spec                                              content{};  ///< The placed child.
};

/// A Canvas' children, built, in the same shape as CellList. Callers write it as a braced list of
/// `ui::CanvasItem{...}`.
class CanvasItemList
{
public:
    /// One built child with its position; the node is already owned by the list.
    struct Placed
    {
        Binding<float>           left;    ///< Left offset, possibly bound.
        Binding<float>           top;     ///< Top offset, possibly bound.
        Binding<int>             z_index; ///< Z-index, possibly bound.
        std::unique_ptr<Element> node;    ///< The built child.
    };

    /// An empty list.
    CanvasItemList() = default;
    /// Builds every item's content and takes ownership of it.
    template <typename... S>
        requires(sizeof...(S) > 0 && (std::same_as<S, CanvasItem> && ...))
    CanvasItemList(S&&... items) // NOLINT(google-explicit-constructor)
    {
        items_.reserve(sizeof...(S));
        (add(std::move(items)), ...);
    }
    CanvasItemList(const CanvasItemList&)            = delete;
    CanvasItemList& operator=(const CanvasItemList&) = delete;
    CanvasItemList(CanvasItemList&&)                 = default;
    CanvasItemList& operator=(CanvasItemList&&)      = default;

    /// The placed children, in declaration order.
    std::vector<Placed> items_;

    /// Builds `item`'s content and appends it, as one more entry of the braced list would.
    void add(CanvasItem item)
    {
        items_.push_back(Placed{std::move(item.left), std::move(item.top), std::move(item.z_index), item.content.take()});
    }
};

/// Children at absolute offsets. Builds a ui::CanvasControl.
///
/// A canvas does not size to its children; give it a `width`/`height` (or a stretching slot).
///
/// Example:
/// @code
/// ui::Property<float> x{0.0f};
///
/// window.set_content(ui::VerticalStack{.spacing = 8, .content = {
///     ui::Canvas{
///         .background = ui::Color::from_rgba(200, 200, 200),
///         .width      = 140.0f,
///         .height     = 140.0f,
///         .content    = {
///             ui::CanvasItem{.left = x, .top = 10.0f, .z_index = 2,
///              .content = ui::Border{.background = ui::colors::Black, .width = 40.0f, .height = 40.0f}},
///             ui::CanvasItem{.left = 20.0f, .top = 20.0f, .z_index = 1,
///              .content = ui::Border{.background = ui::colors::White, .width = 40.0f, .height = 40.0f}},
///         },
///     },
///     ui::Button{.text = "Move", .on_click = [&] { x += 10.0f; }},
/// }});
/// @endcode
struct Canvas
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Canvas>` points at.
    using control_type = CanvasControl;

    // ---- properties ----
    /// Background: a Color, or a ThemeColor read from the theme; empty (the default) draws none.
    Binding<Paint> background{};

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
    ControlOut<Canvas> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The positioned children; see ui::CanvasItem.
    CanvasItemList content{};

    /// Consumes the spec and returns the built ui::CanvasControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
