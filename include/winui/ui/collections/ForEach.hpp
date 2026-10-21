// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/layouts/Layout.hpp"
#include "winui/ui/layouts/StackLayout.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/LayoutSpec.hpp"
#include "winui/ui/spec/Spec.hpp"

#include <concepts>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ui
{

/// What `on_element_prepared` gets: an element about to show item `index`.
struct ElementPreparedArgs
{
    Element&    element; ///< The realized element.
    std::size_t index;   ///< The item it shows.
};

/// What `on_element_index_changed` gets: an element whose item moved.
struct ElementIndexChangedArgs
{
    Element&    element;   ///< The realized element.
    std::size_t old_index; ///< Where its item was.
    std::size_t new_index; ///< Where its item is now.
};

/// A repeater: a collection laid out by an attachable Layout that realizes only the items in view.
///
/// It has no chrome, selection or scrolling of its own. Put it in a Scroll and it asks its item
/// template only for the items its layout places in the viewport and a cache buffer around it, and
/// destroys the rest as they scroll away. The collection is an index range (set_item_count()) that
/// the template turns into elements. The repeater owns every realized element. Report collection
/// changes with the notify_*() calls so realized elements stay with their items. ListView and
/// TileView are built on it. Use it directly for a custom collection without chrome.
///
/// Example:
/// @code
/// // Declarative: 500 cards in a wrap layout, inside a scroller.
/// std::vector<std::string> names;
/// for (int i = 0; i < 500; ++i)
///     names.push_back(std::format("Card {}", i));
/// window.set_content(ui::Scroll{
///     .height  = 320.0f,
///     .content = ui::ForEach<std::string>{
///         .items      = names,
///         .layout     = ui::WrapLayout{},
///         .virtualize = true,
///         .item       = [](const std::string& name) -> ui::Spec {
///             return ui::Text{.text = name, .width = 96.0f, .height = 64.0f};
///         },
///     },
/// });
///
/// // Imperative:
/// std::vector<std::string> rows{"First", "Second", "Third"};
/// auto  scroll   = std::make_unique<ui::ScrollControl>();
/// auto* repeater = scroll->set_child(std::make_unique<ui::ForEachControl>());
/// repeater->set_layout(std::make_unique<ui::StackLayoutEngine>())->set_spacing(4.0f);
/// repeater->set_item_template([&rows](std::size_t i) { return std::make_unique<ui::TextControl>(rows[i]); });
/// repeater->set_item_count(rows.size());
/// repeater->on_element_prepared([](const ui::ElementPreparedArgs& args) { std::println("realized {}", args.index); });
///
/// rows.insert(rows.begin(), "New first row");
/// repeater->notify_inserted(0);
/// @endcode
class ForEachControl final : public Element, private LayoutOwner
{
public:
    /// Makes the element for item `index` and may choose a different look per item. Called when the
    /// item is realized. The repeater owns what it returns.
    using ItemTemplate = Callback<std::unique_ptr<Element>(std::size_t index)>;

    /// An empty repeater with a default StackLayoutEngine and cache lengths of 2.
    ForEachControl();
    ~ForEachControl() override;

    ForEachControl(const ForEachControl&)            = delete;
    ForEachControl& operator=(const ForEachControl&) = delete;

    // ---- items ----
    /// Replaces the template. Every realized element is cleared and made again.
    void set_item_template(ItemTemplate item_template);
    /// Whether an item template is set. Without one nothing is realized.
    [[nodiscard]] bool has_item_template() const
    {
        return static_cast<bool>(item_template_);
    }
    /// How many items the collection holds.
    [[nodiscard]] std::size_t item_count() const
    {
        return item_count_;
    }
    /// Replaces the collection with `count` new items (a reset).
    void set_item_count(std::size_t count);

    /// Reports that `count` items were inserted at `index`.
    void notify_inserted(std::size_t index, std::size_t count = 1);
    /// Reports that `count` items were removed from `index`. With an AddRemoveTransition among
    /// the children_transitions(), each removed element fades out in place before it is destroyed.
    void notify_removed(std::size_t index, std::size_t count = 1);
    /// Reports that `count` items at `index` were replaced. Their elements are made again.
    void notify_replaced(std::size_t index, std::size_t count = 1);
    /// Reports that one item moved from `from` to `to`, its position after the move. Its element
    /// moves with it.
    void notify_moved(std::size_t from, std::size_t to);
    /// Reports any other change: the whole collection is new, with the same count.
    void notify_reset();

    // ---- layout ----
    /// Takes ownership of the layout and returns an observer. Replaces the previous layout and
    /// clears every realized element. nullptr restores the default StackLayoutEngine.
    template <typename T>
    T* set_layout(std::unique_ptr<T> layout)
    {
        static_assert(std::is_base_of_v<Layout, T>, "a ForEach's layout must be a Layout");
        T* observer = layout.get();
        replace_layout(std::move(layout));
        return observer;
    }
    /// The current layout, non-owning and never nullptr.
    [[nodiscard]] Layout* layout() const
    {
        return layout_.get();
    }

    // ---- appearance ----
    /// Background behind the items, as a fixed color; nullopt (the default) draws none. Redraws.
    void set_background(std::optional<Color> color)
    {
        background_.set(color);
        mark_dirty(DirtyFlags::Visual);
    }
    /// Background as a theme color, read again when the theme changes. Redraws.
    void set_background(ThemeColor resource)
    {
        background_.set(resource);
        mark_dirty(DirtyFlags::Visual);
    }

    // ---- virtualization ----
    /// How many viewports' worth of items to keep realized beyond the one in view, split evenly
    /// before and after it. 2 by default, one on each side. 0 realizes only what shows.
    [[nodiscard]] float horizontal_cache_length() const
    {
        return horizontal_cache_length_;
    }
    /// Sets the horizontal cache length and lays the repeater out again.
    void set_horizontal_cache_length(float length);
    /// The vertical counterpart of horizontal_cache_length().
    [[nodiscard]] float vertical_cache_length() const
    {
        return vertical_cache_length_;
    }
    /// Sets the vertical cache length and lays the repeater out again.
    void set_vertical_cache_length(float length);

    // ---- elements ----
    /// The realized element for item `index` (non-owning), or nullptr.
    [[nodiscard]] Element* try_get_element(std::size_t index) const;
    /// The item a realized element stands for. Unset when `element` is not one of the repeater's
    /// realized elements.
    [[nodiscard]] std::optional<std::size_t> element_index(const Element& element) const;
    /// Realizes item `index` now, laid out where the layout puts it. The repeater owns the element.
    /// Returns nullptr only for an index out of range or a repeater with no template.
    Element* get_or_create_element(std::size_t index);
    /// How many items are realized.
    [[nodiscard]] std::size_t realized_count() const
    {
        return realized_.size();
    }
    /// The realized items' indices, ascending.
    [[nodiscard]] std::vector<std::size_t> realized_indices() const;

    // ---- events ----
    /// An element was made for item `index`, before it is measured.
    void on_element_prepared(Callback<void(const ElementPreparedArgs&)> handler)
    {
        on_element_prepared_ = std::move(handler);
    }
    /// An element is about to be destroyed because it scrolled out, its item was removed, or the
    /// collection was reset.
    void on_element_clearing(Callback<void(Element&)> handler)
    {
        on_element_clearing_ = std::move(handler);
    }
    /// A collection change moved an element's item, with the old and the new index.
    void on_element_index_changed(Callback<void(const ElementIndexChangedArgs&)> handler)
    {
        on_element_index_changed_ = std::move(handler);
    }

    /// The layout's extent in layout coordinates, covering every item, realized or estimated.
    [[nodiscard]] Rect layout_extent() const
    {
        return extent_;
    }
    /// The realization rect of the last layout pass, in layout coordinates.
    [[nodiscard]] Rect realization_rect() const
    {
        return realization_;
    }
    /// The realized elements, in item order. An item scrolled out of the realization rect has no
    /// element to list.
    void for_each_child(ChildVisitor visit) const override;
    /// Only the realized elements that have been arranged. One realized but not yet placed has no
    /// usable bounds.
    void                         automation_children(std::vector<Element*>& out) const override;
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Group;
    }

protected:
    // ---- Element ----
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Advances the background and exit transitions, then ticks a snapshot of the realized
    /// elements. A child's handler may change the collection. An element recycled or cleared on the
    /// way is skipped.
    void tick_override(Duration delta) override;


private:
    friend class RepeaterLayoutContext;
    friend class RepeaterNonVirtualContext;

    struct Entry
    {
        std::unique_ptr<Element> element;
        Size                     desired{};
        Size                     measured_in{-1.0f, -1.0f};
        std::uint64_t            theme_generation = 0;
        bool                     used             = false; // asked for in this measure pass
        bool                     suppressed       = false; // the layout recycles it itself
        bool                     arranged         = false; // placed by the layout in this pass
    };

    void layout_invalidated(bool measure) override
    {
        (void)measure;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    void replace_layout(std::unique_ptr<Layout> layout);

    Size run_measure(Size available);
    Size measure_pass(Size available); // run_measure()'s body
    void run_arrange();
    // A collection change told while the repeater measures or runs a template (a template that
    // edits its own collection): the layout cannot take it mid-pass, so it is replayed afterwards,
    // at the end of the measure pass, before the next change, or on the next tick.
    enum class ChangeKind
    {
        Inserted,
        Removed,
        Replaced,
        Moved,
        Reset,
    };
    struct DeferredChange
    {
        ChangeKind  kind;
        std::size_t first;
        std::size_t second;
    };
    std::vector<DeferredChange> deferred_changes_;
    int                         busy_ = 0; // run_measure() or an item template is running
    [[nodiscard]] bool          defer_change(ChangeKind kind, std::size_t first = 0, std::size_t second = 0);
    void                        replay_deferred_changes();
    void                        update_window();
    Element&                    realize(std::size_t index, ElementRealizationOptions options);
    void                        clear_element(std::size_t index, bool leaving = false);
    void                        clear_all();
    void                        shift_entries(std::size_t from, std::ptrdiff_t delta);
    void                        notify_layout(const ItemsChange& change);
    Size                        measure_element(Element& element, Size available);
    [[nodiscard]] Entry*        entry_of(const Element& element);
    void                        flush_graveyard();

    ItemTemplate            item_template_;
    std::size_t             item_count_ = 0;
    std::unique_ptr<Layout> layout_;
    bool                    layout_initialized_ = false;
    BackgroundBrush         background_;
    float                   horizontal_cache_length_ = 2.0f;
    float                   vertical_cache_length_   = 2.0f;

    std::map<std::size_t, Entry>                    realized_;
    std::unordered_map<const Element*, std::size_t> index_of_;
    std::vector<std::unique_ptr<Element>>           graveyard_; // cleared elements, until the next flush
    detail::ExitGhostsPtr                           ghosts_;    // removed elements playing their exit
    int                                             dispatching_ = 0;

    // Layout state.
    Point                      layout_origin_{};
    Rect                       extent_{};
    Rect                       visible_{};
    Rect                       realization_{};
    Rect                       measured_realization_{};
    Size                       last_available_{};
    Size                       last_desired_{};
    bool                       measured_         = false;
    bool                       relayout_pending_ = false;
    std::optional<std::size_t> recommended_anchor_{};

    mutable const TextMeasurer* measurer_ = nullptr;
    mutable const Theme*        theme_    = nullptr;

    Callback<void(const ElementPreparedArgs&)>     on_element_prepared_;
    Callback<void(Element&)>                       on_element_clearing_;
    Callback<void(const ElementIndexChangedArgs&)> on_element_index_changed_;
};

namespace detail
{
// Forwards one change of a collection to the repeater.
void apply_collection_change(ForEachControl& control, const CollectionChange& change);
} // namespace detail

/// A repeater: turns a collection into children, and nothing else. Builds a ui::ForEachControl.
///
/// `.items` is the collection, `.item` is how one of them looks, and `.layout` is how they are
/// arranged, a vertical stack when left out. It has no selection, keyboard navigation or scrolling
/// of its own. For those, use a view such as ui::ListView, ui::TileView or ui::TableView. To
/// scroll a ForEach, put it in a ui::Scroll. To mix fixed and generated children, put the ForEach
/// in an ordinary stack next to the fixed ones.
///
/// By default every item is built and stays built, wherever the ForEach sits. A row keeps its own
/// state, such as a text box's text, for as long as its item is in the collection. Set
/// `.virtualize = true` for long collections inside a ui::Scroll. Then only the rows in and near
/// view exist, and a row's state is lost when it scrolls away, so keep that state in the item.
///
/// `.items` takes one of three sources, and they differ in how a change reaches the rows:
///
/// - A ui::ObservableVector<T> (`.items = todos`), for a collection the app edits in place. Each
///   insert builds one row and each removal destroys one. A move keeps the row's element and
///   moves it, and a replacement builds that one row again. `.item` receives the item as `T&`. It
///   may keep that reference and bind to the item's own Property members, because an item stays
///   at its address for as long as it is in the collection.
/// - A `ui::Property<std::vector<T>>` (`.items = photos`), for a collection the app assigns as a
///   whole, for example after loading, filtering or sorting it. With `.key`, the old and the new
///   keys are compared. A row whose key is still there keeps its element and state wherever it
///   moved to, and is built again only when its item compares unequal. A change that needs very
///   many moves, such as a shuffle of several thousand items, arrives as one reset instead, which
///   builds every row again. Without `.key`, the rows before and after the edit are kept and those
///   in between are built again. That covers appending, inserting or removing a run of items, but
///   not a reorder. The rows read a copy of the collection the control owns, so `.item` must not
///   keep the reference it is given.
/// - A plain `std::vector<T>`, copied once, for a fixed collection.
///
/// Name the item type, as in `ui::ForEach<Photo>`. C++ deduces a spec's type only from the field's
/// own type, which a Property or an ObservableVector is not.
///
/// Lifetime: `.item` is stored on the built control and called for the life of the tree, so it
/// must not capture anything shorter-lived than the window.
///
/// Example:
/// @code
/// struct Photo
/// {
///     int         id = 0;
///     std::string title;
///     friend bool operator==(const Photo&, const Photo&) = default;
/// };
/// ui::Property<std::vector<Photo>> photos{{{1, "Lake"}, {2, "Bridge"}, {3, "Harbor"}}};
///
/// window.set_content(ui::Scroll{.content = ui::ForEach<Photo>{
///     .items  = photos,
///     .key    = &Photo::id,
///     .layout = ui::TileLayout{.min_item_width = 160.0f, .min_row_spacing = 8.0f, .min_column_spacing = 8.0f},
///     .item   = [](const Photo& p) -> ui::Spec { return ui::Text{.text = p.title}; },
/// }});
///
/// std::vector<Photo> sorted = photos.get();
/// std::ranges::sort(sorted, {}, &Photo::title);
/// photos = sorted; // the rows move, none is built again
/// @endcode
template <typename T>
struct ForEach
{
    /// The control this spec builds; what `ui::Control<ForEach>` points at.
    using control_type = ForEachControl;

    // ---- properties ----
    /// The collection. A ui::ObservableVector<T> or a `ui::Property<std::vector<T>>` is followed
    /// as it changes. A plain vector is copied once.
    Binding<std::vector<T>> items{};


    /// What identifies an item when `.items` is bound to a `ui::Property<std::vector<T>>`: a
    /// member pointer or a callable returning the key. With it, a row follows its item through
    /// any reorder. Keys must be unique. A collection that repeats one is rebuilt as a whole, and
    /// a debug build asserts. Not needed with a ui::ObservableVector.
    ItemKey<std::type_identity_t<T>> key{};

    /// How the items are arranged; a vertical stack when left out.
    LayoutSpec layout{};

    /// When false, every item is built and keeps its state. When true, only the rows in and near
    /// view are built, for a long collection inside a ui::Scroll. A row's state is then lost when
    /// it scrolls away.
    bool virtualize = false;

    /// With `virtualize`: how many viewports' worth of rows to keep built beyond the one in view,
    /// split evenly before and after it. 2, one on each side, by default; 0 builds only what shows.
    Binding<float> horizontal_cache_length{2.0f};
    Binding<float> vertical_cache_length{2.0f}; ///< See `horizontal_cache_length`.

    // ---- appearance / layout ----
    /// Transitions every row runs. With a ui::AddRemoveTransition an inserted row grows
    /// in, a removed one shrinks away and the rows after it slide into place.
    TransitionList item_transitions{};
    /// Background behind the rows: a fixed color, or a theme color that follows theme switches.
    /// Unset draws none.
    Binding<Paint>                    background{};
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
    /// A row was built for the item at this position, before it is measured. With `virtualize`
    /// that happens as rows scroll into view.
    Callback<void(const ElementPreparedArgs&)> on_element_prepared{};
    /// A row is about to be destroyed because it scrolled out, its item was removed, or the
    /// collection was reset.
    Callback<void(Element&)> on_element_clearing{};
    /// A change of the collection moved a row's item, with the old and the new position.
    Callback<void(const ElementIndexChangedArgs&)> on_element_index_changed{};
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
    /// Out: the handle that receives the built control. Its type uses std::type_identity_t, so
    /// passing a handle does not take part in deducing the type argument.
    ControlOut<std::type_identity_t<ForEach>> control{};
    /// How one item looks: a callable taking `const T&`, or `T&` to edit an item of a
    /// ui::ObservableVector. Its type uses std::type_identity_t, so `.items` alone decides T when
    /// the type argument is left out.
    Callback<Spec(std::type_identity_t<T>&)> item{};

    /// Consumes the spec and returns the built ui::ForEachControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&
    {
        // In viewports: far more than any collection is long.
        constexpr float Everything = 1.0e6f;

        auto            node = std::make_unique<ForEachControl>();
        ForEachControl* raw  = node.get();

        std::unique_ptr<Layout> arrangement = layout.take();
        if (!arrangement)
        {
            arrangement = StackLayout{}.build();
        }
        if (!virtualize)
        {
            // Everything realized: a stack is told so, any other layout gets a window so large that
            // every item falls inside it.
            if (auto* stack = dynamic_cast<StackLayoutEngine*>(arrangement.get()))
            {
                stack->set_virtualization_enabled(false);
            }
            node->set_horizontal_cache_length(Everything);
            node->set_vertical_cache_length(Everything);
        }
        else
        {
            apply(horizontal_cache_length,
                  *node,
                  [raw = node.get()](const float& v) { raw->set_horizontal_cache_length(v); });
            apply(vertical_cache_length, *node, [raw = node.get()](const float& v) { raw->set_vertical_cache_length(v); });
        }
        if (background.reactive() || !background.initial().empty())
        {
            apply(background,
                  *node,
                  [raw = node.get()](const Paint& v) { detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });
        }
        // Before the template is set, so the rows of the first pass are reported too.
        if (on_element_prepared)
        {
            node->on_element_prepared(std::move(on_element_prepared));
        }
        if (on_element_clearing)
        {
            node->on_element_clearing(std::move(on_element_clearing));
        }
        if (on_element_index_changed)
        {
            node->on_element_index_changed(std::move(on_element_index_changed));
        }
        node->set_layout(std::move(arrangement));
        if (!item_transitions.empty())
        {
            node->set_children_transitions(std::move(item_transitions));
        }

        if (item && items.observable() != nullptr)
        {
            // The rows read the app's collection in place. Its last change, on destruction, is a
            // reset to empty, so the template is never asked for an item of a collection that is gone.
            ObservableVector<T>* live = items.observable();
            node->set_item_template(
                [live, factory = std::move(item)](std::size_t index) mutable -> std::unique_ptr<Element>
                {
                    if (index >= live->size())
                    {
                        return nullptr;
                    }
                    Spec built = factory((*live)[index]);
                    return std::move(built).build();
                });
            node->set_item_count(live->size());
            node->add_subscription(
                live->observe([raw](const CollectionChange& change) { detail::apply_collection_change(*raw, change); }));
        }
        else if (item)
        {
            // An item type that cannot be copied only comes in an ObservableVector.
            if constexpr (std::copy_constructible<T>)
            {
                // The rows read a copy of the collection the node owns: the template closure holds it, the
                // subscription below only refreshes it.
                auto            owned = std::make_unique<std::vector<T>>(items.initial());
                std::vector<T>* data  = owned.get();

                node->set_item_template(
                    [rows = std::move(owned), factory = std::move(item)](std::size_t index) mutable -> std::unique_ptr<Element>
                    {
                        if (index >= rows->size())
                        {
                            return nullptr;
                        }
                        Spec built = factory((*rows)[index]);
                        return std::move(built).build();
                    });
                node->set_item_count(data->size());
                if (items.reactive())
                {
                    node->add_subscription(items.subscribe(
                        [raw, data, identity = std::move(key)](std::vector<T> next) mutable
                        {
                            // Nothing is realized until the next layout pass, so the copy is replaced
                            // first and the changes are reported after.
                            const std::vector<T> old = std::exchange(*data, std::move(next));
                            detail::vector_changes<T>(old,
                                                      *data,
                                                      identity,
                                                      [raw](const CollectionChange& change)
                                                      { detail::apply_collection_change(*raw, change); });
                        }));
                }
            }
        }

        apply_common(*this, *node);
        publish_control(control, raw);
        return node;
    }
};

} // namespace ui
