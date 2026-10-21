// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// Shows one item at a time, filling the whole view, and flips between them with previous/next
/// buttons, the mouse wheel or the keyboard.
///
/// Items are any elements, owned by the view (add_item()). They sit side by side along the flip
/// axis, each the size of the view. Only the items in view are drawn or get input, and Tab
/// reaches only the selected item's focusable content. The previous/next buttons appear while the
/// mouse moves over the view and for 3 s after, or once keyboard focus arrives. A button never
/// shows on a side with no item beyond it. One wheel turn flips one item. Along the flip axis the
/// arrow keys and PageUp/PageDown step one item, and Home/End jump to the ends. A step to a
/// neighbor slides over 300 ms, and longer jumps land at once. For a page indicator, pair it with
/// a PipsPager bound to the same index.
///
/// Example:
/// @code
/// // Declarative: the pages are children, each filling the view.
/// ui::Property<std::size_t> page{0};
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::Carousel{
///             .selected_index = page,
///             .height         = 270.0f,
///             .max_width      = 400.0f,
///             .content        = {
///                 ui::Image{.source = ui::Bitmap::read("cliff.jpg")},
///                 ui::Image{.source = ui::Bitmap::read("grapes.jpg")},
///                 ui::Text{.text = "The end", .horizontal_alignment = ui::Alignment::Center},
///             },
///         },
///         ui::PipsPager{.selected_index = page, .number_of_pages = 3}, // two-way
///     },
/// });
///
/// // Imperative.
/// auto  stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto* flip  = stack->add(std::make_unique<ui::CarouselControl>());
/// flip->set_height(200.0f);
/// flip->add_item(std::make_unique<ui::TextControl>("First")); // becomes the selection
/// flip->add_item(std::make_unique<ui::TextControl>("Second"));
/// flip->on_selection_changed([&page](std::size_t i) { page = i; });
/// flip->set_selected_index(1); // slides once laid out; fires the handler
/// @endcode
class CarouselControl final : public Element, private SelectionProvider
{
public:
    /// An empty view: no items, nothing selected, not focusable.
    CarouselControl() = default;

    // ---- items ----
    /// Append an item, taking ownership; returns a non-owning observer.
    /// The first item added becomes the selection (without firing on_selection_changed()).
    template <typename T>
    T* add_item(std::unique_ptr<T> item)
    {
        static_assert(std::is_base_of_v<Element, T>, "Carousel items must be Elements");
        T* observer = item.get();
        set_parent_of(*observer, this);
        items_.push_back(std::move(item));
        if (selected_index_ < 0)
        {
            selected_index_ = 0;
            position_       = 0.0f;
        }
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return observer;
    }
    /// How many items the view holds.
    [[nodiscard]] std::size_t item_count() const
    {
        return items_.size();
    }
    /// The item at `index` (owned by the view), or nullptr past the end.
    [[nodiscard]] Element* item(std::size_t index) const
    {
        return index < items_.size() ? items_[index].get() : nullptr;
    }

    /// The index of the item in view; unset with no items.
    [[nodiscard]] std::optional<std::size_t> selected_index() const
    {
        if (selected_index_ < 0)
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(selected_index_);
    }
    /// Selects the item at `index`, clamped to the items there are. A change fires
    /// on_selection_changed. A step to a neighbor slides (use_animations()), and anything else
    /// lands at once. Ignored with no items, since a carousel with items always shows one.
    void set_selected_index(std::size_t index)
    {
        select_clamped(static_cast<int>((std::min<std::size_t>)(index, (std::numeric_limits<int>::max)())));
    }

    /// The flip axis; Horizontal by default.
    [[nodiscard]] Orientation orientation() const
    {
        return orientation_;
    }
    /// Changes the flip axis and which arrow keys flip; updates the layout.
    void set_orientation(Orientation orientation);

    /// Whether a one-item flip slides. On by default.
    [[nodiscard]] bool use_animations() const
    {
        return use_animations_;
    }
    /// Turns the one-item slide on or off for later flips.
    void set_use_animations(bool animate)
    {
        use_animations_ = animate;
    }

    /// The fill behind the items: a fixed color or a theme resource. Empty, the default, is the
    /// window's background color.
    void set_background(Paint color)
    {
        background_ = color;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The border color around the items (none by default), drawn over their edges.
    void set_border_brush(Paint color)
    {
        border_brush_ = color;
        mark_dirty(DirtyFlags::Visual);
    }
    /// The border's thickness per side, DIPs (0 by default). Drawn over the items, so it does not
    /// shrink them; redraws only.
    void set_border_thickness(const Thickness& thickness)
    {
        border_thickness_ = thickness;
        mark_dirty(DirtyFlags::Visual);
    }


    /// Called with the new index whenever the selected item changes, by a button, the wheel, a key
    /// or set_selected_index(). One handler; setting another replaces it.
    void on_selection_changed(Callback<void(std::size_t)> handler)
    {
        on_selection_changed_ = std::move(handler);
    }

    /// Whether the previous/next buttons are showing right now (see the class comment).
    [[nodiscard]] bool previous_button_visible() const;
    /// As previous_button_visible(), for the next button.
    [[nodiscard]] bool next_button_visible() const;
    /// Where the previous button sits (window DIPs): 16x38 centered 1 DIP inside the left edge, or
    /// 38x16 at the top when vertical. Valid after arrange(), shown or not.
    [[nodiscard]] Rect previous_button_bounds() const;
    /// As previous_button_bounds(), at the right (or bottom) edge.
    [[nodiscard]] Rect next_button_bounds() const;
    /// Focusable while enabled and holding at least one item.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled() && !items_.empty();
    }
    /// Whether the view itself holds keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// Every item, in order.
    void for_each_child(ChildVisitor visit) const override
    {
        for (const auto& item : items_)
        {
            visit(*item);
        }
    }
    /// The view itself, then the selected item's focusable descendants. Tab cannot reach the
    /// other items.
    void collect_focusables(std::vector<Element*>& out) override;

    // ---- automation ----
    /// A List.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::List;
    }

protected:
    // ---- Element ----
    /// Takes the whole available size on a bounded axis; on an unbounded one, the largest item's.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Gives every item the view's size and places them along the flip axis at the current
    /// position.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws the background, the items in view (clipped), the border, the buttons while shown and
    /// the focus ring, which sits just inside the bounds.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Mouse movement shows the buttons; a click on a button flips. Other input reaches the items
    /// in view.
    bool pointer_override(const PointerEvent& e) override;
    /// An item in view that scrolls gets the turn first. Otherwise each turn flips one item, and a
    /// repeat in the same direction within 200 ms is ignored. Returns false (not handled) at the
    /// first or last item and for Ctrl+wheel, so an outer scroller can react.
    bool                              wheel_override(const PointerEvent& e) override;
    void                              hit_test_children(Point p, std::vector<Element*>& out) const override;
    [[nodiscard]] bool                covers_children_at(Point p) const override;
    [[nodiscard]] std::optional<Rect> children_hit_clip() const override
    {
        return bounds();
    }
    /// Advances the slide and the buttons' show timer, then ticks the items.
    void tick_override(Duration delta) override;


private:
    // The two arrows along the flip axis; the cross-axis ones are left to move focus.
    [[nodiscard]] bool takes_arrow_key(Key key) const;
    // The selected item's live elements only, unlike for_each_child(): an item out of view must
    // not open a popup.
    void               collect_live(std::vector<Element*>& out) override;
    void               select_clamped(int index);
    [[nodiscard]] bool horizontal() const
    {
        return orientation_ == Orientation::Horizontal;
    }
    [[nodiscard]] float extent() const // one item's length along the flip axis
    {
        return horizontal() ? bounds().width : bounds().height;
    }
    // Items the view overlaps at the current position.
    [[nodiscard]] std::pair<std::size_t, std::size_t> visible_range() const;
    void                                              place_items();
    bool                                              move(int delta); // flips by `delta` items
    void                                              show_buttons();  // restarts the buttons' fade-out timer

    std::vector<std::unique_ptr<Element>> items_;
    int                                   selected_index_ = -1;
    Orientation                           orientation_    = Orientation::Horizontal;
    bool                                  use_animations_ = true;
    Paint                                 background_;
    Paint                                 border_brush_;
    Thickness                             border_thickness_{};

    // The view's position in items: selected_index_ when settled, fractional while sliding.
    float  position_         = 0.0f;
    float  slide_from_       = 0.0f;
    double slide_elapsed_    = -1.0; // < 0: not sliding
    double buttons_left_ms_  = 0.0;  // how much longer the buttons show without more movement
    bool   show_buttons_     = false;
    bool   keep_buttons_     = false; // the pointer is over one of them
    bool   hovered_previous_ = false, pressed_previous_ = false;
    bool   hovered_next_ = false, pressed_next_ = false;

    // The wheel's debounce: time since the last turn, and that turn's direction.
    double since_wheel_ms_ = 1e9;
    float  last_wheel_     = 0.0f;

    Callback<void(std::size_t)> on_selection_changed_;

protected:
    void enabled_changed() override;
    /// Arrows along the flip axis step, Home/End jump to the ends, PageUp/PageDown step one item.
    bool key_override(const KeyEvent& e) override;
    /// Called by Window; gaining keyboard focus (focus_visible()) shows the buttons. Redraws.
    void focus_changed(bool focused) override;

protected:
    /// Inside the view, which fills its slot edge to edge.
    [[nodiscard]] bool focus_ring_inside() const override
    {
        return true;
    }

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.selection = this;
    }
    // The selected item.
    void selected_items(std::vector<Element*>& out) const override
    {
        if (selected_index_ >= 0 && static_cast<std::size_t>(selected_index_) < items_.size())
        {
            out.push_back(items_[static_cast<std::size_t>(selected_index_)].get());
        }
    }
    // True: a flip view with items always shows one of them.
    [[nodiscard]] bool selection_required() const override
    {
        return true;
    }
    [[nodiscard]] bool can_select_multiple() const override
    {
        return false;
    }
};

/// One item at a time, flipped with arrows, the wheel or the keys. Builds a ui::CarouselControl. Its
/// items are child elements, and each fills the whole view.
///
/// Example:
/// @code
/// ui::Property<std::size_t> page{0};
///
/// window.set_content(ui::Carousel{
///     .selected_index = page,
///     .height         = 270.0f,
///     .max_width      = 400.0f,
///     .content        = {
///         ui::Image{.source = ui::Bitmap::read("cliff.jpg")},
///         ui::Image{.source = ui::Bitmap::read("grapes.jpg")},
///         ui::Text{.text = "The end", .horizontal_alignment = ui::Alignment::Center},
///     },
/// });
/// @endcode
struct Carousel
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<Carousel>` points at.
    using control_type = CarouselControl;

    // ---- properties ----
    /// The index of the item in view.
    TwoWayField<std::size_t> selected_index{0};
    /// Horizontal (the default) flips left/right; Vertical flips up/down.
    Binding<Orientation> orientation{Orientation::Horizontal};
    Binding<bool>        use_animations{true}; ///< One-item flips slide.
    /// The fill behind the items: a fixed color or a theme resource; unset is the window's
    /// background color.
    Binding<Paint> background{};

    // ---- appearance / layout ----
    /// A border drawn over the items' edges: a fixed color or a theme resource; unset draws none.
    Binding<Paint>                    border_brush{};
    Binding<Thickness>                border_thickness{};     ///< See `border_brush`.
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
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// Called with the index of the item now in view. A bound value doesn't run it.
    Callback<void(std::size_t)> on_selection_changed{};
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
    ControlOut<Carousel> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// The pages, in flip order; each fills the whole view.
    SpecList content{};

    /// Consumes the spec and returns the built ui::CarouselControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
