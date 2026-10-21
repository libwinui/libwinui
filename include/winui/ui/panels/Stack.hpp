// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cassert>
#include <cstddef>

namespace ui
{

/// Lays its children out in a single column or row, `spacing` apart.
///
/// The panel owns its children (add() takes a `unique_ptr` and hands back a non-owning
/// observer). Each child is measured unbounded along the stack and gets the panel's full cross
/// extent, where its own alignment places it. The declarative forms are ui::VerticalStack and
/// ui::HorizontalStack.
///
/// Example:
/// @code
/// auto  stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// stack->set_spacing(12.0f);
/// ui::TextControl* label = stack->add(std::make_unique<ui::TextControl>("Clicks: 0"));
/// ui::ButtonControl*    click = stack->add(std::make_unique<ui::ButtonControl>("Click me"));
/// click->on_click([label, n = 0]() mutable { label->set_text(std::format("Clicks: {}", ++n)); });
/// window.set_content(std::move(stack)); // the window takes ownership of the whole subtree
///
/// // The same tree, declared:
/// ui::Property<int> clicks{0};
/// window.set_content(ui::VerticalStack{
///     .spacing  = 12,
///     .content  = {
///         ui::Text{ .text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); }) },
///         ui::Button{ .text = "Click me", .on_click = [&] { ++clicks; } },
///     },
/// });
/// @endcode
class StackControl final : public Element
{
public:
    /// An empty vertical panel, no spacing, no background.
    StackControl() = default;
    /// An empty panel stacking in `orientation`.
    explicit StackControl(Orientation orientation) : orientation_(orientation)
    {
    }

    /// Takes ownership of a child, appends it after the last one, and returns a non-owning
    /// observer to it. The panel is the sole owner, so the pointer is valid only while the child
    /// is in the panel. Invalidates layout.
    template <typename T>
    T* add(std::unique_ptr<T> child)
    {
        static_assert(std::is_base_of_v<Element, T>, "StackControl children must be Elements");
        assert(child != nullptr && "a StackControl child must not be null");
        T* observer = child.get();
        set_parent_of(*observer, this);
        children_.push_back(std::move(child));
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); // child set changed the panel's own size
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
    /// add() at `index` (clamped to the end) instead of after the last child.
    template <typename T>
    T* insert(std::size_t index, std::unique_ptr<T> child)
    {
        static_assert(std::is_base_of_v<Element, T>, "StackControl children must be Elements");
        assert(child != nullptr && "a StackControl child must not be null");
        T* observer = child.get();
        set_parent_of(*observer, this);
        children_.insert(children_.begin() + static_cast<std::ptrdiff_t>((std::min)(index, children_.size())),
                         std::move(child));
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// The children in stacking order (owned by the panel).
    [[nodiscard]] std::span<const std::unique_ptr<Element>> children() const
    {
        return children_;
    }
    /// Destroys `child` if it is one of this panel's. False when it is not.
    /// Any observer to it dangles afterwards. With an AddRemoveTransition in
    /// children_transitions(), the child leaves visibly. It drops out of layout, input and the
    /// tab ring at once, fades out where it was, and is destroyed when the fade has played.
    bool remove(const Element* child)
    {
        const auto it = std::find_if(children_.begin(),
                                     children_.end(),
                                     [child](const std::unique_ptr<Element>& c) { return c.get() == child; });
        if (it == children_.end())
        {
            return false;
        }
        retire_child(std::move(*it));
        children_.erase(it);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return true;
    }
    /// Takes `child` out of the panel alive and hands it back, for adding somewhere else. Focus
    /// inside it is cleared and it plays no exit transition. Null when it is not one of this
    /// panel's children.
    [[nodiscard]] std::unique_ptr<Element> orphan(const Element* child)
    {
        const auto it = std::find_if(children_.begin(),
                                     children_.end(),
                                     [child](const std::unique_ptr<Element>& c) { return c.get() == child; });
        if (it == children_.end())
        {
            return nullptr;
        }
        std::unique_ptr<Element> owned = std::move(*it);
        children_.erase(it);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return detail::orphan(std::move(owned));
    }
    /// Destroys every child, each one leaving as it does with remove().
    void clear()
    {
        for (std::unique_ptr<Element>& child : children_)
        {
            retire_child(std::move(child));
        }
        children_.clear();
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The stacking direction; Vertical by default.
    [[nodiscard]] Orientation orientation() const
    {
        return orientation_;
    }
    /// Changes the direction; invalidates layout.
    void set_orientation(Orientation o)
    {
        orientation_ = o;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The gap between neighboring children, in DIPs; 0 by default.
    [[nodiscard]] float spacing() const
    {
        return spacing_;
    }
    /// Changes the gap; invalidates layout.
    void set_spacing(float spacing)
    {
        spacing_ = spacing;
        mark_dirty(DirtyFlags::Layout);
    }

    /// The background, filled behind the children; nullopt (the default) draws nothing.
    /// A ThemeColor is a theme resource, re-read whenever it is drawn (ui/BackgroundBrush.hpp).
    [[nodiscard]] const std::optional<Color>& background() const
    {
        return background_.color();
    }
    /// The theme resource the background follows, when set_background(ThemeColor) set one.
    [[nodiscard]] ThemeColor background_resource() const
    {
        return background_.resource();
    }
    /// A fixed background color (nullopt clears it); redraws.
    void set_background(std::optional<Color> color)
    {
        background_.set(color);
        mark_dirty(DirtyFlags::Visual);
    }
    /// A theme-resource background, re-read on every theme change; redraws.
    void set_background(ThemeColor resource)
    {
        background_.set(resource);
        mark_dirty(DirtyFlags::Visual);
    }
    /// With a transition set, a later change of what the background draws (a new color, or a
    /// resource under a new theme) blends from the color on screen (Animation.hpp's
    /// BrushTransition). nullopt (the default) changes it at once.
    [[nodiscard]] const std::optional<BrushTransition>& background_transition() const
    {
        return background_.transition();
    }
    /// Sets or clears the transition used by later background changes.
    void set_background_transition(std::optional<BrushTransition> transition)
    {
        background_.set_transition(transition);
    }

    // ---- border and padding ----
    /// The outline color; nullopt (the default) draws no border even with a non-zero
    /// border_thickness(). Nullopt as well while a ThemeColor is set.
    [[nodiscard]] std::optional<Color> border_brush() const
    {
        return border_brush_.color();
    }
    /// The theme resource the border is drawn from, or null.
    [[nodiscard]] ThemeColor border_brush_resource() const
    {
        return border_brush_.resource();
    }
    /// A fixed outline color; replaces any resource. Redraws.
    void set_border_brush(std::optional<Color> color)
    {
        border_brush_ = color;
        mark_dirty(DirtyFlags::Visual);
    }
    /// An outline color read from the theme each time it is drawn; replaces any fixed color.
    /// Redraws.
    void set_border_brush(ThemeColor resource)
    {
        border_brush_ = resource;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The border width in DIPs per side (default zero); it insets the children like padding.
    [[nodiscard]] const Thickness& border_thickness() const
    {
        return border_thickness_;
    }
    /// Sets the border width; updates the layout and redraws.
    void set_border_thickness(const Thickness& thickness)
    {
        border_thickness_ = thickness;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    /// The corner radius in DIPs, uniform for all four corners (default 0, square). It rounds the
    /// background and the border; the children are not clipped.
    [[nodiscard]] float corner_radius() const
    {
        return corner_radius_;
    }
    /// Sets the corner radius; redraws.
    void set_corner_radius(float radius)
    {
        corner_radius_ = radius;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The room in DIPs between the border and the children (default zero).
    [[nodiscard]] const Thickness& padding() const
    {
        return padding_;
    }
    /// Sets the padding and updates the layout.
    void set_padding(const Thickness& padding)
    {
        padding_ = padding;
        mark_dirty(DirtyFlags::Layout);
    }

    /// How children are placed across the stack. With Stretch, the default, every
    /// child gets the panel's full cross extent and its own horizontal_alignment() /
    /// vertical_alignment() positions it within that. Leading, Center and Trailing place every
    /// child at its desired cross size, and a child's own alignment then has no effect.
    [[nodiscard]] Alignment child_alignment() const
    {
        return child_alignment_;
    }
    /// Changes child_alignment(); invalidates layout.
    void set_child_alignment(Alignment a)
    {
        child_alignment_ = a;
        mark_dirty(DirtyFlags::Layout);
    }

    /// True: measuring touches only the panel and its children.
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return true;
    }
    /// Lists the children in stacking order.
    void for_each_child(ChildVisitor visit) const override;

protected:
    /// Measures each child unbounded along the stack and bounded across it, caching the sizes;
    /// returns their sum along the stack (plus spacing) by the largest across.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places the children one after another from the cached sizes, aligned per child_alignment().
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Fills the background, then renders every child in order.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Advances the background and exit transitions, then ticks every child.
    void tick_override(Duration delta) override;


private:
    [[nodiscard]] bool vertical() const
    {
        return orientation_ == Orientation::Vertical;
    }

    std::vector<std::unique_ptr<Element>> children_;

    // Removed children still playing their exit (AddRemoveTransition).
    void                retire_child(std::unique_ptr<Element> child);
    [[nodiscard]] float spacing_total() const;

    // Border plus padding, taken off every side before the children are laid out.
    [[nodiscard]] Thickness content_inset() const
    {
        return Edges{.left   = border_thickness_.left + padding_.left,
                     .top    = border_thickness_.top + padding_.top,
                     .right  = border_thickness_.right + padding_.right,
                     .bottom = border_thickness_.bottom + padding_.bottom};
    }
    detail::ExitGhostsPtr ghosts_;
    Orientation           orientation_ = Orientation::Vertical;
    // The defaults: no spacing, and every child gets the full cross extent unless it sets its
    // own alignment.
    float     spacing_         = 0.0f;
    Alignment child_alignment_ = Alignment::Stretch;

    BackgroundBrush background_;
    Paint           border_brush_;
    Thickness       border_thickness_{};
    float           corner_radius_ = 0.0f;
    Thickness       padding_{};

    mutable std::vector<Size> measured_;
};

/// The control a `ui::VerticalStack` builds: a StackControl whose orientation is Vertical.
using VerticalStackControl = StackControl;
/// The control a `ui::HorizontalStack` builds: a StackControl whose orientation is Horizontal.
using HorizontalStackControl = StackControl;

/// A column of children. Builds a ui::StackControl with vertical orientation.
///
/// Children are measured with unbounded height and stacked top to bottom; `child_alignment`
/// decides their horizontal placement (a child's own `horizontal_alignment` still applies inside that).
///
/// Example:
/// @code
/// ui::Property<int> clicks{0};
///
/// window.set_content(ui::VerticalStack{
///     .spacing         = 12,
///     .child_alignment = ui::Alignment::Leading,
///     .margin          = ui::Thickness{24.0f},
///     .content         = {
///         ui::Text{.text = "Appearance", .role = ui::TextRole::Subtitle},
///         ui::Text{.text = ui::bind(clicks, [](int n) { return std::format("Clicks: {}", n); })},
///         ui::HorizontalStack{.spacing = 8, .content = {
///             ui::Button{.text = "Click me", .on_click = [&] { ++clicks; }},
///             ui::Button{.text = "Reset",    .on_click = [&] { clicks = 0; }},
///         }},
///     },
/// });
/// @endcode
struct VerticalStack
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<VerticalStack>` points at.
    using control_type = StackControl;

    // ---- properties ----
    /// Gap between adjacent children, DIPs.
    Binding<float> spacing{0.0f};
    /// Where children sit across the stack axis; Stretch (the default) sizes them to the panel.
    Binding<Alignment> child_alignment{Alignment::Stretch};
    /// Transitions every child runs: an EntranceTransition as
    /// each is first laid out, a RepositionTransition as layout moves it.
    Binding<TransitionList> children_transitions{};
    /// Background behind the children: a Color, or a ThemeColor read from the theme; empty (the
    /// default) draws nothing.
    Binding<Paint> background{};
    /// Animates a background change (Animation.hpp's BrushTransition).
    Binding<std::optional<BrushTransition>> background_transition{};
    /// Border color, a Color or a ThemeColor; empty (the default) draws no border.
    Binding<Paint> border_brush{};
    /// Border width per edge, DIPs; it insets the children like padding.
    Binding<Thickness> border_thickness{Thickness{}};
    /// Corner radius of the background and the border, DIPs.
    Binding<float> corner_radius{0.0f};
    /// Room between the border and the children, DIPs.
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
    ControlOut<VerticalStack> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The stacked children, top to bottom.
    SpecList content{};

    /// Consumes the spec and returns the built ui::StackControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

/// A row of children. Builds a ui::StackControl with horizontal orientation.
///
/// The mirror of ui::VerticalStack: children stack left to right and `child_alignment` places them
/// vertically.
///
/// Example:
/// @code
/// window.set_content(ui::HorizontalStack{
///     .spacing         = 8,
///     .child_alignment = ui::Alignment::Center,
///     .content         = {
///         ui::Icon{.source = ui::Symbol::Save},
///         ui::Text{.text = "Unsaved changes"},
///         ui::Button{.text = "Save", .on_click = [&] { save(); }},
///     },
/// });
/// @endcode
struct HorizontalStack
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<HorizontalStack>` points at.
    using control_type = StackControl;

    // ---- properties ----
    /// Gap between adjacent children, DIPs.
    Binding<float> spacing{0.0f};
    /// Where children sit across the stack axis; Stretch (the default) sizes them to the panel.
    Binding<Alignment> child_alignment{Alignment::Stretch};
    /// Transitions every child runs: an EntranceTransition as
    /// each is first laid out, a RepositionTransition as layout moves it.
    Binding<TransitionList> children_transitions{};
    /// Background behind the children: a Color, or a ThemeColor read from the theme; empty (the
    /// default) draws nothing.
    Binding<Paint> background{};
    /// Animates a background change (Animation.hpp's BrushTransition).
    Binding<std::optional<BrushTransition>> background_transition{};
    /// Border color, a Color or a ThemeColor; empty (the default) draws no border.
    Binding<Paint> border_brush{};
    /// Border width per edge, DIPs; it insets the children like padding.
    Binding<Thickness> border_thickness{Thickness{}};
    /// Corner radius of the background and the border, DIPs.
    Binding<float> corner_radius{0.0f};
    /// Room between the border and the children, DIPs.
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
    ControlOut<HorizontalStack> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The stacked children, left to right.
    SpecList content{};

    /// Consumes the spec and returns the built ui::StackControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
