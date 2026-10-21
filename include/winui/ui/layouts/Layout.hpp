// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/core/Version.hpp"

#include <limits>
#include <optional>

#include <cstddef>

namespace ui
{

/// Where the items of a line go in the room left over along it (a LinedFlowLayout's or
/// TileLayout's ItemsJustification, a WrapLayout's line alignment).
enum class ItemsJustification
{
    Start,        ///< Packed at the start (the default).
    Center,       ///< Packed in the middle.
    End,          ///< Packed at the end.
    SpaceAround,  ///< Equal room around each item, so half as much at the ends.
    SpaceBetween, ///< First and last item at the ends, equal gaps between the rest.
    SpaceEvenly,  ///< Equal gaps between the items and at both ends.
};

class Element;

/// What a layout's host does when the layout asks to be redone.
///
/// LayoutPanelControl and ForEachControl, the two hosts a Layout attaches to, implement it by
/// measuring or arranging themselves again. A layout reaches it through Layout::invalidate_measure()
/// and Layout::invalidate_arrange().
///
/// Example:
/// @code
/// // A custom layout: a property change redoes the host only while one is attached.
/// class GapLayout : public ui::NonVirtualizingLayout
/// {
/// public:
///     void set_gap(float gap)
///     {
///         gap_ = gap;
///         if (owner() != nullptr)
///         {
///             invalidate_measure(); // the host's layout_invalidated(true)
///         }
///     }
///
/// private:
///     float gap_ = 0.0f;
/// };
/// @endcode
class LayoutOwner
{
public:
    LayoutOwner()                              = default;
    LayoutOwner(const LayoutOwner&)            = delete;
    LayoutOwner& operator=(const LayoutOwner&) = delete;

    /// The layout's properties changed. Measure and arrange again when `measure` is true, else only
    /// arrange.
    virtual void layout_invalidated(bool measure) = 0;

protected:
    ~LayoutOwner() = default;
};

/// What kind of change it was.
enum class ItemsChangeAction
{
    Add,     ///< `new_count` items inserted at `new_index`
    Remove,  ///< `old_count` items removed from `old_index`
    Replace, ///< `old_count` items at `old_index` replaced by `new_count` new ones
    Move,    ///< `old_count` items moved from `old_index` to `new_index`
    Reset,   ///< anything else: the whole collection is new
};

/// A change to a virtualizing layout's collection, as the host hands it to
/// VirtualizingLayout::on_items_changed().
///
/// Hosts build it from their notify_*() calls, such as ForEachControl::notify_inserted(). A layout
/// that caches per-item state uses it to shift or drop that state.
///
/// Example:
/// @code
/// // A layout that caches each item's height.
/// class CachedRowLayout : public ui::VirtualizingLayout
/// {
/// public:
///     void on_items_changed(ui::VirtualizingLayoutContext&, const ui::ItemsChange& change) override
///     {
///         if (change.action == ui::ItemsChangeAction::Add)
///         {
///             const auto at = heights_.begin() + static_cast<std::ptrdiff_t>(change.new_index);
///             heights_.insert(at, change.new_count, 0.0f);
///         }
///         else
///         {
///             heights_.clear();
///         }
///         invalidate_measure();
///     }
///
/// private:
///     std::vector<float> heights_;
/// };
/// @endcode
struct ItemsChange
{
    /// The kind of change. The ItemsChangeAction says which index and count fields apply.
    ItemsChangeAction action = ItemsChangeAction::Reset;
    /// The first affected item before the change (Remove, Replace, Move).
    std::size_t old_index = 0;
    /// How many items were removed, replaced or moved.
    std::size_t old_count = 0;
    /// The first affected item after the change (Add, Replace, Move).
    std::size_t new_index = 0;
    /// How many items were inserted or replaced in.
    std::size_t new_count = 0;
};

/// The base of every attachable layout. Not copyable: it belongs to one host.
///
/// Derive from NonVirtualizingLayout or VirtualizingLayout, not from this class. A host takes the
/// layout by `std::unique_ptr` (LayoutPanelControl::set_layout(), ForEachControl::set_layout()) and
/// becomes its owner(). The layout keeps its per-host state in its own members and calls
/// invalidate_measure() when one of its properties changes.
///
/// Example:
/// @code
/// auto  panel  = std::make_unique<ui::LayoutPanelControl>();
/// auto* layout = panel->set_layout(std::make_unique<ui::StackLayoutEngine>());
/// layout->set_spacing(8.0f); // the layout invalidates the panel
/// @endcode
class Layout
{
public:
    virtual ~Layout();

    Layout(const Layout&)            = delete;
    Layout& operator=(const Layout&) = delete;

    /// The host this layout is attached to (non-owning), or nullptr.
    [[nodiscard]] LayoutOwner* owner() const
    {
        return owner_;
    }

    /// Whether the host may be measured on a layout worker thread (ParallelLayout) with this
    /// layout. Return true only when measure() touches nothing but the layout's own state and
    /// the context it is given. It must not realize or recycle an element, read the viewport or
    /// invalidate anything. False by default. The built-in layouts virtualize and return false.
    [[nodiscard]] virtual bool parallel_measure_safe() const
    {
        return false;
    }

protected:
    /// Measures and arranges the host again. Call it after a property of the layout changed.
    /// Does nothing while detached.
    void invalidate_measure();
    /// Arranges the host again without measuring. Does nothing while detached.
    void invalidate_arrange();

private:
    // Only the two kinds a host knows how to drive derive from Layout; a layout derives from
    // NonVirtualizingLayout or VirtualizingLayout, so deriving from Layout itself fails to compile.
    Layout() = default;
    friend class NonVirtualizingLayout;
    friend class VirtualizingLayout;

    // Attach to / detach from a host, as the two hosts take and give up a layout.
    void set_owner(LayoutOwner* owner)
    {
        owner_ = owner;
    }
    friend class LayoutPanelControl;
    friend class ForEachControl;

    LayoutOwner* owner_ = nullptr;
};

/// The children of a panel, as a non-virtualizing layout sees them.
///
/// The host implements it. A layout calls it only from its measure() and arrange(). Children are
/// addressed by index, in the host's child order, and all of them exist up front.
///
/// Example:
/// @code
/// class ColumnLayout : public ui::NonVirtualizingLayout
/// {
/// public:
///     ui::Size measure(ui::NonVirtualizingLayoutContext& context, ui::Size available) override
///     {
///         ui::Size total{};
///         for (std::size_t i = 0; i < context.item_count(); ++i)
///         {
///             const ui::Size d = context.measure(i, {available.width, ui::Unbounded});
///             total.width = std::max(total.width, d.width);
///             total.height += d.height;
///         }
///         return total;
///     }
/// };
/// @endcode
class NonVirtualizingLayoutContext
{
public:
    virtual ~NonVirtualizingLayoutContext() = default;

    /// How many children the host has.
    [[nodiscard]] virtual std::size_t item_count() const = 0;

    /// Measures child `index` in `available` (infinity on an unbounded axis) and returns its
    /// desired size. desired_size() returns the same size until the next measure.
    virtual Size measure(std::size_t index, Size available) = 0;
    /// The desired size child `index` reported at its last measure().
    [[nodiscard]] virtual Size desired_size(std::size_t index) const = 0;

    /// Places child `index`, in coordinates relative to the host's content box.
    virtual void arrange(std::size_t index, const Rect& rect) = 0;
};

/// A layout that measures and arranges every child of the host.
///
/// Derive a custom layout for a fixed set of children from it and give it to a LayoutPanel. Each
/// pass sees all children through the context. Keep any state between measure() and arrange() in
/// members.
///
/// Example:
/// @code
/// // Every child in one column, each as tall as it wants.
/// class ColumnLayout final : public ui::NonVirtualizingLayout
/// {
/// public:
///     ui::Size measure(ui::NonVirtualizingLayoutContext& context, ui::Size available) override
///     {
///         ui::Size total{};
///         for (std::size_t i = 0; i < context.item_count(); ++i)
///         {
///             const ui::Size d = context.measure(i, {available.width, ui::Unbounded});
///             total.width = std::max(total.width, d.width);
///             total.height += d.height;
///         }
///         return total;
///     }
///     ui::Size arrange(ui::NonVirtualizingLayoutContext& context, ui::Size final_size) override
///     {
///         float y = 0.0f;
///         for (std::size_t i = 0; i < context.item_count(); ++i)
///         {
///             const float h = context.desired_size(i).height;
///             context.arrange(i, ui::Rect{0.0f, y, final_size.width, h});
///             y += h;
///         }
///         return final_size;
///     }
/// };
///
/// window.set_content(ui::LayoutPanel{
///     .layout   = std::make_unique<ColumnLayout>(),
///     .content  = {ui::Text{.text = "One"}, ui::Text{.text = "Two"}},
/// });
/// @endcode
class NonVirtualizingLayout : public Layout
{
public:
    /// Returns the size the children need within `available` (infinity on an unbounded axis). It
    /// must measure every child it will arrange.
    [[nodiscard]] virtual Size measure(NonVirtualizingLayoutContext& context, Size available) = 0;
    /// Places every child within `final_size` and returns the size used.
    virtual Size arrange(NonVirtualizingLayoutContext& context, Size final_size) = 0;
};

/// How get_or_create_element_at() hands an element out.
/// Combine with `|`, test with has_flag().
enum class ElementRealizationOptions : unsigned
{
    /// Realize it normally; the host recycles it after a pass that did not ask for it.
    None = 0,
    /// Realize it even outside the collection's realized range, where the host would otherwise
    /// refuse. Use it to measure a representative item.
    ForceCreate = 1,
    /// The layout keeps track of it and recycles it itself. The host does not clear it at the end
    /// of a pass that did not ask for it.
    SuppressAutoRecycle = 2,
};
/// Whether `set` contains `flag`.
[[nodiscard]] constexpr bool has_flag(ElementRealizationOptions set, ElementRealizationOptions flag)
{
    return (static_cast<unsigned>(set) & static_cast<unsigned>(flag)) != 0;
}
/// The union of two option sets.
[[nodiscard]] constexpr ElementRealizationOptions operator|(ElementRealizationOptions a, ElementRealizationOptions b)
{
    return static_cast<ElementRealizationOptions>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

/// A collection, as a virtualizing layout sees it.
///
/// The host implements it: ForEachControl, or LayoutPanelControl, where every child counts as
/// realized. A layout calls it only from its measure() and arrange(). It asks only for the items
/// that fall in realization_rect(), measures and arranges them through the context, and lets the
/// host recycle the rest.
///
/// Example:
/// @code
/// // Realizes and measures the rows, 32 high, that fall in the realization rect.
/// void measure_rows(ui::VirtualizingLayoutContext& context, ui::Size available)
/// {
///     const ui::Rect view  = context.realization_rect();
///     const auto     first = static_cast<std::size_t>(std::max(0.0f, view.y) / 32.0f);
///     for (std::size_t i = first; i < context.item_count() && static_cast<float>(i) * 32.0f < view.bottom(); ++i)
///     {
///         ui::Element& e = context.get_or_create_element_at(i);
///         (void)context.measure(e, {available.width, ui::Unbounded});
///     }
/// }
/// @endcode
class VirtualizingLayoutContext
{
public:
    virtual ~VirtualizingLayoutContext() = default;

    /// How many items the collection holds.
    [[nodiscard]] virtual std::size_t item_count() const = 0;

    /// The element for item `index`, realized from the host's item template if it is not already.
    /// Asking for it keeps it realized through this measure pass. The host owns the element.
    virtual Element& get_or_create_element_at(std::size_t index, ElementRealizationOptions options) = 0;
    /// get_or_create_element_at() with ElementRealizationOptions::None.
    Element& get_or_create_element_at(std::size_t index)
    {
        return get_or_create_element_at(index, ElementRealizationOptions::None);
    }
    /// Lets the host clear an element now instead of at the end of the pass.
    virtual void recycle_element(Element& element) = 0;
    /// The item an element was realized for; unset when it is not one of this collection's.
    [[nodiscard]] virtual std::optional<std::size_t> index_of(const Element& element) const = 0;

    /// The part of the layout worth realizing: the viewport plus the cache buffer around it, in
    /// layout coordinates. Unbounded (a huge rect) when the host has nothing to scroll it.
    [[nodiscard]] virtual Rect realization_rect() const = 0;
    /// The viewport alone.
    [[nodiscard]] virtual Rect visible_rect() const = 0;
    /// The item to lay out first after a long scroll jump, such as a bring-into-view target, if any.
    [[nodiscard]] virtual std::optional<std::size_t> recommended_anchor_index() const
    {
        return std::nullopt;
    }

    /// Where the layout's origin sits in the host's content box. A layout that places items at
    /// negative positions moves it.
    [[nodiscard]] virtual Point layout_origin() const = 0;
    /// Moves the layout origin; see layout_origin().
    virtual void set_layout_origin(Point origin) = 0;

    /// Measures an element in `available` (infinity on an unbounded axis) and returns its desired
    /// size, which desired_size() keeps.
    virtual Size measure(Element& element, Size available) = 0;
    /// The desired size `element` reported at its last measure() through this context.
    [[nodiscard]] virtual Size desired_size(const Element& element) const = 0;
    /// Places an element in the host's content box, in layout coordinates minus layout_origin().
    virtual void arrange(Element& element, const Rect& rect) = 0;
};

/// A layout that measures and arranges the part of a collection that is in view.
///
/// Derive a custom layout from it and give it to a ForEach, which can then virtualize, or to a
/// LayoutPanel, where every child is in view. measure() realizes only the items inside
/// VirtualizingLayoutContext::realization_rect() and returns the extent of the whole collection.
/// arrange() places the items measure() realized.
///
/// Example:
/// @code
/// // Fixed-height rows; only the rows in the realization rect are realized.
/// class RowLayout final : public ui::VirtualizingLayout
/// {
/// public:
///     ui::Size measure(ui::VirtualizingLayoutContext& context, ui::Size available) override
///     {
///         const ui::Rect view  = context.realization_rect();
///         const std::size_t count = context.item_count();
///         first_ = std::min(count, static_cast<std::size_t>(std::max(0.0f, view.y) / Row));
///         last_  = std::min(count, static_cast<std::size_t>(std::max(0.0f, view.bottom()) / Row) + 1);
///         for (std::size_t i = first_; i < last_; ++i)
///         {
///             (void)context.measure(context.get_or_create_element_at(i), {available.width, Row});
///         }
///         return {std::isfinite(available.width) ? available.width : 0.0f, static_cast<float>(count) * Row};
///     }
///     ui::Size arrange(ui::VirtualizingLayoutContext& context, ui::Size final_size) override
///     {
///         for (std::size_t i = first_; i < last_; ++i)
///         {
///             context.arrange(context.get_or_create_element_at(i),
///                             ui::Rect{0.0f, static_cast<float>(i) * Row, final_size.width, Row});
///         }
///         return final_size;
///     }
///
/// private:
///     static constexpr float Row = 32.0f;
///     std::size_t            first_ = 0;
///     std::size_t            last_  = 0;
/// };
///
/// auto repeater = std::make_unique<ui::ForEachControl>();
/// repeater->set_layout(std::make_unique<RowLayout>());
/// @endcode
class VirtualizingLayout : public Layout
{
public:
    /// Called when a host starts using this layout. uninitialize_for_context() is called when it
    /// stops. The default does nothing.
    virtual void initialize_for_context(VirtualizingLayoutContext& context)
    {
        (void)context;
    }
    /// Called when the host stops using this layout. Drop per-host state here.
    virtual void uninitialize_for_context(VirtualizingLayoutContext& context)
    {
        (void)context;
    }

    /// Returns the size of the whole collection laid out within `available` (infinity on an
    /// unbounded axis). Realizes and measures only the items in the context's realization rect.
    [[nodiscard]] virtual Size measure(VirtualizingLayoutContext& context, Size available) = 0;
    /// Places the items realized by the last measure() within `final_size` and returns the size used.
    virtual Size arrange(VirtualizingLayoutContext& context, Size final_size) = 0;

    /// Called when the collection changed. The default calls invalidate_measure(), which lays out
    /// everything again.
    virtual void on_items_changed(VirtualizingLayoutContext& context, const ItemsChange& change)
    {
        (void)context;
        (void)change;
        invalidate_measure();
    }
};

/// A rect that stands for no limit. A host with nothing to scroll it reports it as its realization
/// rect. It is huge but finite, because infinite bounds would make its right edge NaN.
[[nodiscard]] inline Rect unbounded_rect()
{
    constexpr float Huge = (std::numeric_limits<float>::max)() / 4.0f;
    return Rect{-Huge, -Huge, 2.0f * Huge, 2.0f * Huge};
}
/// Whether a realization rect is unbounded along either axis, as in a non-virtualizing context.
[[nodiscard]] inline bool is_unbounded(const Rect& rect)
{
    constexpr float Limit = (std::numeric_limits<float>::max)() / 16.0f;
    return rect.width >= Limit || rect.height >= Limit;
}

} // namespace ui
