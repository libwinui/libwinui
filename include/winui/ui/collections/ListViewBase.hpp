// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/render/RenderRecording.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/DragDrop.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/ForEach.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ui
{

namespace detail
{
class ItemSelection;
}

class ListItemContainer;

/// Where ListViewBase::scroll_into_view() puts the item.
enum class ScrollIntoViewAlignment
{
    Default, ///< The least scroll that shows the item.
    Leading, ///< The item at the top of the viewport.
};

/// How a list keeps its scroll position while items are inserted and removed.
enum class ItemsUpdatingScrollMode
{
    KeepItemsInView,    ///< The first item in view stays in view (the default).
    KeepScrollOffset,   ///< The scroll offset stays put and the items move under it.
    KeepLastItemInView, ///< A list scrolled to its end stays at its end as items arrive.
};

/// What a ListView drag carries in DataPackage::data: the list it came from and the items dragged.
///
/// A drop target reads it back with `std::any_cast<ListViewDragPayload>(&e.data.data)`.
///
/// Example:
/// @code
/// void accept_drops(ui::ListViewControl* left, ui::ListViewControl* right)
/// {
///     right->on_drop([=](const ui::DragEventArgs& e) {
///         if (const auto* payload = std::any_cast<ui::ListViewDragPayload>(&e.data.data);
///             payload != nullptr && payload->source == left)
///         {
///             right->insert_item(right->insertion_index_at(e.position), left->items()[payload->items.front()]);
///             left->remove_item(payload->items.front());
///         }
///     });
/// }
/// @endcode
struct ListViewDragPayload
{
    /// The list the drag started from (non-owning).
    const Element* source = nullptr;
    /// The indices of the dragged items, in index order.
    std::vector<std::size_t> items;
};

/// The arguments of ListViewBase::on_drag_items_starting().
///
/// The package already carries a ListViewDragPayload in `data.data`. A handler may add text for
/// other drop targets, replace the payload, or return ui::Cancel to stop the drag.
///
/// Example:
/// @code
/// auto list = std::make_unique<ui::ListViewControl>();
/// list->set_items({"Apple", "Banana", "Cherry"});
/// list->on_drag_items_starting([&](const ui::DragItemsStartingArgs& e) {
///     if (e.items.size() > 1)
///         return ui::Cancel; // single-item drags only
///     e.data.text = list->items()[e.items.front()];
///     return ui::Proceed;
/// });
/// @endcode
struct DragItemsStartingArgs
{
    /// The indices of the items about to be dragged.
    std::vector<std::size_t> items;
    /// The package the drag will carry. Fill in its text or data.
    DataPackage& data;
};

/// What `on_drag_items_completed` gets once a drag out of a ListView or TileView ends.
struct DragItemsCompletedArgs
{
    std::vector<std::size_t> items;                               ///< The indices of the dragged items.
    DataPackageOperation     result = DataPackageOperation::None; ///< What the drop did; None when refused.
};

/// What `on_items_reordered` gets when a drop inside the list moved item `from` to `to`. Both are
/// positions in the collection after the move.
struct ItemsReorderedArgs
{
    std::size_t from = 0; ///< Where the item was.
    std::size_t to   = 0; ///< Where it is now.
};

/// The shared base of ListView and TileView: a scrolling, virtualized, selectable collection of
/// templated items. Each item sits in an item container that draws the hover, press and selection
/// chrome.
///
/// The items are either plain strings (set_items(), shown as text by the default template) or an
/// index range that an item template turns into content (set_item_template() and
/// set_item_count()). Only the items in and near the viewport are realized, through the
/// ForEachControl inside the Scroll (repeater(), scroll_view()). Report changes to the collection
/// with insert_item() and remove_item() for strings, or the notify_*() calls for a template's
/// collection. The selection then follows its items and the item container transitions play. The
/// selection mode decides which gestures select. Groups, a header and footer, item click, drag
/// reordering and saving the scroll position are all optional. The class cannot be constructed
/// directly. Create a ListView or a TileView.
///
/// The list is one tab stop. The arrow keys move the focused item. In Extended mode, and in Single
/// mode with single_selection_follows_focus(), the selection moves with it. Ctrl+A selects
/// everything in Multiple and Extended mode, or clears the selection when everything is already
/// selected.
///
/// Example:
/// @code
/// auto  list = std::make_unique<ui::ListViewControl>();
/// list->set_items({"Apple", "Banana", "Cherry"});
/// list->set_selection_mode(ui::SelectionMode::Extended);
/// list->set_height(240.0f);
/// list->on_selection_changed([](std::optional<std::size_t> index)
///                            { std::println("first selected: {}", index.value_or(0)); });
///
/// // A template's collection instead of strings:
/// struct Contact
/// {
///     std::string name;
/// };
/// std::vector<Contact> contacts{{"Ada"}, {"Grace"}, {"Linus"}};
/// list->set_item_template([&contacts](std::size_t i) {
///     return std::make_unique<ui::TextControl>(contacts[i].name);
/// });
/// list->set_item_count(contacts.size());
///
/// window.set_content(std::move(list));
/// @endcode
class ListViewBase : public Element, protected SelectionProvider, protected ValueProvider
{
public:
    /// Makes the content for item `index` and may choose a different look per item. Called when an
    /// item is realized. The list owns what it returns.
    using ItemTemplate = Callback<std::unique_ptr<Element>(std::size_t index)>;
    /// Makes a group header's content, for group index `group` (group_key() names it).
    using GroupHeaderTemplate = Callback<std::unique_ptr<Element>(std::size_t group)>;

    ~ListViewBase() override;

    ListViewBase(const ListViewBase&)            = delete;
    ListViewBase& operator=(const ListViewBase&) = delete;
    ListViewBase(ListViewBase&&)                 = delete;
    ListViewBase& operator=(ListViewBase&&)      = delete;

    // ---- items: strings ----
    /// The string items. Empty when the items come from an item template.
    [[nodiscard]] const std::vector<std::string>& items() const
    {
        return items_;
    }
    /// Replaces the items with a new list of strings. Nothing is selected afterwards.
    /// on_selection_changed() fires when something was selected before.
    void set_items(std::vector<std::string> items);
    /// Inserts `item` before `index`. The selection stays on its items, and an
    /// AddRemoveTransition among the item container transitions plays.
    void insert_item(std::size_t index, std::string item);
    /// Appends `item` at the end.
    void add_item(std::string item)
    {
        insert_item(items_.size(), std::move(item));
    }
    /// Removes the item at `index`, dropping it from the selection.
    void remove_item(std::size_t index);
    /// Moves the item at `from` to `to`, keeping its selection state.
    void move_item(std::size_t from, std::size_t to);

    // ---- items: a template's collection ----
    /// Sets the content template and rebuilds the realized items. nullptr, the default, shows each
    /// string of items() as text.
    void set_item_template(ItemTemplate item_template);
    /// How many items the list shows: items().size() for strings, else the set_item_count() value.
    [[nodiscard]] std::size_t item_count() const
    {
        return count_;
    }
    /// Sets a collection of `count` items that only the template knows. Counts as a reset and
    /// clears items().
    void set_item_count(std::size_t count);
    /// Reports that `count` items were inserted at `index` in the template's collection.
    void notify_inserted(std::size_t index, std::size_t count = 1);
    /// Reports that `count` items were removed at `index`. They leave the selection.
    void notify_removed(std::size_t index, std::size_t count = 1);
    /// Reports that `count` items at `index` changed in place. The template builds their content
    /// again.
    void notify_replaced(std::size_t index, std::size_t count = 1);
    /// Reports that the item at `from` moved to `to`. The selection moves with it.
    void notify_moved(std::size_t from, std::size_t to);
    /// Reports that the whole collection changed. Realized items are rebuilt and a
    /// ContentTransition among the item container transitions plays. Clears the selection,
    /// and fires on_selection_changed when there was one.
    void notify_reset();
    /// The item that `inside`, any element inside an item, belongs to. A handler inside a template,
    /// such as a context menu's Delete, uses it to find its item after inserts and removals moved
    /// it. Unset when `inside` is not in a realized item.
    [[nodiscard]] std::optional<std::size_t> index_from_element(const Element& inside) const;

    // ---- groups ----
    /// Files the items under group headers, one key per item in item order. The groups appear in
    /// the order their keys first appear, and the items keep their order within a group, so items
    /// with the same key need not be next to each other. An empty vector ungroups. An item past the
    /// end of `keys` goes under an empty key. Indices stay positions in the collection. Only the
    /// order on screen, which the arrow keys and Shift ranges follow, comes from the groups. In a
    /// TileView with a tile or stack layout, each header takes a line of its own.
    void set_group_keys(std::vector<std::string> keys);
    /// The keys set_group_keys() was given.
    [[nodiscard]] const std::vector<std::string>& group_keys() const
    {
        return group_keys_;
    }
    /// How many groups the keys make; 0 when ungrouped.
    [[nodiscard]] std::size_t group_count() const
    {
        return group_names_.size();
    }
    /// Group `group`'s key, as its header shows it. Empty past group_count().
    [[nodiscard]] const std::string& group_key(std::size_t group) const;
    /// Sets the group header's content. nullptr shows the group's key as 20px text over a 1px
    /// divider.
    void set_group_header_template(GroupHeaderTemplate header_template);
    /// Whether group headers are sticky; on by default. The current group's header stays pinned to
    /// the top of the viewport until the next header pushes it up.
    [[nodiscard]] bool sticky_group_headers() const
    {
        return sticky_headers_;
    }
    /// Turns sticky group headers on or off.
    void set_sticky_group_headers(bool sticky);

    // ---- header / footer ----
    /// Sets content above the first item that scrolls with the items. Takes ownership, replaces
    /// any previous header, header_text() included, and returns a non-owning observer of the new
    /// one.
    template <typename T>
    T* set_header(std::unique_ptr<T> header)
    {
        T* observer = header.get();
        header_text_.clear();
        replace_header(std::move(header), true);
        return observer;
    }
    /// Sets a plain-text header above the first item, in the group headers' text style, that
    /// scrolls with the items. Replaces any previous header. An empty text removes a text header.
    void set_header_text(std::string text);
    /// The text set_header_text() shows. Empty while there is none or the header is content.
    [[nodiscard]] const std::string& header_text() const
    {
        return header_text_;
    }
    /// The current header, non-owning, or nullptr.
    [[nodiscard]] Element* header() const
    {
        return header_;
    }
    /// Sets content after the last item that scrolls with the items. Takes ownership and returns a
    /// non-owning observer.
    template <typename T>
    T* set_footer(std::unique_ptr<T> footer)
    {
        T* observer = footer.get();
        replace_header(std::move(footer), false);
        return observer;
    }
    /// The current footer, non-owning, or nullptr.
    [[nodiscard]] Element* footer() const
    {
        return footer_;
    }

    // ---- selection ----
    /// The selection mode; Single by default.
    [[nodiscard]] SelectionMode selection_mode() const
    {
        return selection_mode_;
    }
    /// Sets the selection mode. None clears the selection, Single keeps one at most, and Multiple
    /// shows a check box in each item.
    void set_selection_mode(SelectionMode mode);
    /// The first selected item. Unset when nothing is selected.
    [[nodiscard]] std::optional<std::size_t> selected_index() const;
    /// Selects `index` alone, clamped to the items, and scrolls it into view. Unset deselects all.
    /// No callback fires when nothing changes.
    void set_selected_index(std::optional<std::size_t> index);
    /// The selected items, in index order.
    [[nodiscard]] std::vector<std::size_t> selected_items() const;
    /// Replaces the selection with the items at `indices`, in any order. Indices past the end are
    /// dropped, Single mode keeps the lowest one, and None selects nothing. Fires
    /// on_selection_changed() once, and only when the selection changed.
    void set_selected_items(std::vector<std::size_t> indices);
    /// Whether the item at `index` is selected.
    [[nodiscard]] bool is_selected(std::size_t index) const;
    /// Adds an item to the selection, in Multiple and Extended mode.
    void select(std::size_t index);
    /// Removes the item at `index` from the selection.
    void deselect(std::size_t index);
    /// Selects every item in Multiple and Extended mode, as Ctrl+A does.
    void select_all();
    /// Clears the selection.
    void deselect_all();
    /// Whether moving keyboard focus with the arrows also selects in Single mode; on by
    /// default.
    [[nodiscard]] bool single_selection_follows_focus() const
    {
        return selection_follows_focus_;
    }
    /// Turns selection-follows-focus on or off.
    void set_single_selection_follows_focus(bool follows)
    {
        selection_follows_focus_ = follows;
    }
    /// Called with selected_index() after the change. Fires for user gestures and programmatic
    /// changes, and only when the selection changed. Replaces any previous handler.
    void on_selection_changed(Callback<void(std::optional<std::size_t>)> handler)
    {
        on_selection_changed_ = std::move(handler);
    }
    /// Called with the new index when the selected item stayed selected but an insert, removal or
    /// move before it changed selected_index(). It is not a selection change, so
    /// on_selection_changed does not fire. A two-way `.selected_index` uses it to follow the item.
    /// Replaces any previous handler.
    void on_selected_index_shifted(Callback<void(std::optional<std::size_t>)> handler)
    {
        on_selected_index_shifted_ = std::move(handler);
    }

    // ---- item click ----
    /// Whether a click raises on_item_click(); off by default.
    [[nodiscard]] bool is_item_click_enabled() const
    {
        return item_click_enabled_;
    }
    /// Turns on_item_click() on or off.
    void set_item_click_enabled(bool enabled)
    {
        item_click_enabled_ = enabled;
    }
    /// Called with the clicked item, before the click selects, while is_item_click_enabled().
    /// Replaces any previous handler.
    void on_item_click(Callback<void(std::size_t)> handler)
    {
        on_item_click_ = std::move(handler);
    }

    // ---- item invoked ----
    /// Whether on_item_invoked() is raised; off by default.
    [[nodiscard]] bool is_item_invoked_enabled() const
    {
        return item_invoked_enabled_;
    }
    /// Turns on_item_invoked() on or off.
    void set_item_invoked_enabled(bool enabled)
    {
        item_invoked_enabled_ = enabled;
    }
    /// Called with the item on Enter or a double-click, and under SelectionMode::None also a
    /// click or Space, while is_item_invoked_enabled(). Raised after the gesture selects.
    /// Replaces any previous handler.
    void on_item_invoked(Callback<void(std::size_t)> handler)
    {
        on_item_invoked_ = std::move(handler);
    }
    /// Sets the context menu of item `index`. It is asked for on a right-click on the item, or on
    /// Shift+F10 or the Menu key while the item is the current one. The item is selected first,
    /// unless it already is or the selection mode is None. An empty list opens no menu. Replaces
    /// any previous one.
    void set_item_context_menu(Callback<std::vector<MenuItem>(std::size_t index)> menu)
    {
        item_context_menu_ = std::move(menu);
    }

    // ---- drag and drop ----
    /// Whether an item dragged past the threshold starts a drag; off by default.
    [[nodiscard]] bool can_drag_items() const
    {
        return can_drag_items_;
    }
    /// Turns item dragging on or off.
    void set_can_drag_items(bool can)
    {
        can_drag_items_ = can;
    }
    /// Whether a drag inside the list moves the item; off by default. Needs can_drag_items() and
    /// Element::allow_drop().
    [[nodiscard]] bool can_reorder_items() const
    {
        return can_reorder_items_;
    }
    /// Turns reordering on or off.
    void set_can_reorder_items(bool can)
    {
        can_reorder_items_ = can;
    }
    /// A drag of items starts. Fill in the package's text or data, or cancel.
    void on_drag_items_starting(Callback<Decision(const DragItemsStartingArgs&)> handler)
    {
        on_drag_items_starting_ = std::move(handler);
    }
    /// A drag of items ended, with the items dragged and what the drop did (None when it was
    /// refused or canceled).
    void on_drag_items_completed(Callback<void(const DragItemsCompletedArgs&)> handler)
    {
        on_drag_items_completed_ = std::move(handler);
    }
    /// A drop inside the list moved item `from` to `to`, both positions in the collection after the
    /// move. A string list has already moved it. For a template's collection, move the data here.
    /// The list has already been told through notify_moved().
    void on_items_reordered(Callback<void(const ItemsReorderedArgs&)> handler)
    {
        on_items_reordered_ = std::move(handler);
    }
    /// Where a drop at `position`, in window coordinates, would insert: the index of the item it
    /// would go before. Past the middle of an item counts as after it.
    [[nodiscard]] std::size_t insertion_index_at(Point position) const;

    // ---- scrolling ----
    /// Scrolls so item `index` shows, by the least amount (Default) or to the top (Leading).
    void scroll_into_view(std::size_t index, ScrollIntoViewAlignment alignment = ScrollIntoViewAlignment::Default);
    /// The vertical scroll offset, DIPs. Horizontal for a vertical TileView.
    [[nodiscard]] float scroll_offset() const;
    /// The inner Scroll, owned by the list.
    [[nodiscard]] ScrollControl* scroll_view() const
    {
        return scroll_;
    }
    /// The inner ForEachControl that realizes the item containers, owned by the list. Its
    /// try_get_element() reaches a realized container by its index, counting group headers.
    [[nodiscard]] ForEachControl* repeater() const
    {
        return repeater_;
    }
    /// The key of the first item in view, from `key_of`, and how far it is scrolled past the top,
    /// as "key|offset".
    [[nodiscard]] std::string relative_scroll_position(Callback<std::string(std::size_t)> key_of) const;
    /// Scrolls back to what relative_scroll_position() described. `index_of` finds the item for a
    /// key and returns unset when there is none. Returns whether the item was found.
    bool set_relative_scroll_position(const std::string&                                       position,
                                      Callback<std::optional<std::size_t>(const std::string&)> index_of);
    /// How the scroll position reacts to inserts and removes; KeepItemsInView by default.
    [[nodiscard]] ItemsUpdatingScrollMode items_updating_scroll_mode() const
    {
        return updating_mode_;
    }
    /// Sets the scroll mode. KeepLastItemInView makes a chat-style log follow its end.
    void set_items_updating_scroll_mode(ItemsUpdatingScrollMode mode)
    {
        updating_mode_ = mode;
    }

    // ---- items panel and containers ----
    /// Where the items sit vertically. Trailing puts a list shorter than its viewport at the
    /// bottom, as in a message log.
    void set_panel_vertical_alignment(Alignment alignment);
    /// Sets the margin around every item container, DIPs, and lays the items out again.
    void set_item_container_margin(const Thickness& margin);
    /// The margin around each item container.
    [[nodiscard]] const Thickness& item_container_margin() const
    {
        return container_margin_;
    }
    /// How content sits horizontally in its container. Stretch in a ListView, Center in a
    /// TileView by default.
    void set_item_horizontal_content_alignment(Alignment alignment);
    /// A fixed container height, DIPs. Unset (the default) sizes each container to its content,
    /// at least 40 DIPs in a ListView.
    [[nodiscard]] std::optional<float> item_height() const
    {
        return item_height_;
    }
    /// Sets the fixed container height, or clears it to size containers to their content. Lays the
    /// items out again.
    void set_item_height(OptionalFloat height);

    // ---- appearance ----
    /// Background behind the items, as a fixed color. nullopt is transparent.
    void set_background(std::optional<Color> color);
    /// Background as a theme color, read again when the theme changes.
    void set_background(ThemeColor resource);
    /// The default template's text color. nullopt uses the theme's text color.
    void set_foreground(std::optional<Color> color);
    /// Foreground as a theme color.
    void set_foreground(ThemeColor resource);
    /// The border color. nullopt draws no border.
    void set_border_brush(std::optional<Color> color);
    /// The border color as a theme color.
    void set_border_brush(ThemeColor resource);
    /// The border width on each side, DIPs. 0, the default, draws none.
    void set_border_thickness(const Thickness& thickness);
    /// Padding inside the border, around the scrolling area.
    void set_padding(const Thickness& padding);

    /// The item container transitions; empty by default. AddRemoveTransition plays on insert
    /// and removal, ContentTransition for a new collection, EntranceTransition when items
    /// first appear and RepositionTransition when they move.
    [[nodiscard]] const TransitionList& item_transitions() const
    {
        return item_transitions_;
    }
    /// Sets the item container transitions. They apply to changes reported after the call.
    void set_item_transitions(TransitionList transitions)
    {
        item_transitions_ = std::move(transitions);
    }


    // ---- focus ----
    /// Whether the list has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// The item keyboard focus is on. Unset when there is none.
    [[nodiscard]] std::optional<std::size_t> current_item() const
    {
        return current_;
    }
    /// Moves the keyboard focus cue to item `index` and scrolls it into view, leaving the selection
    /// as it is. An index past the end does nothing. The arrow keys go on from there.
    void set_current_item(std::size_t index);
    /// The item Tab steps through while the list is entered as a focus scope. Unset otherwise.
    [[nodiscard]] std::optional<std::size_t> focus_index() const
    {
        return detail::optional_index(focus_index_);
    }
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled() && count_ > 0;
    }
    /// The item the keyboard is on, so a screen reader reads it as the arrow keys move.
    [[nodiscard]] Element* automation_focus() const override;
    void                   collect_focusables(std::vector<Element*>& out) override;
    /// Lists the scroll view that holds the header, the items and the footer, and an open item
    /// context menu. The pinned group header is a drawn copy of a header already in the list and
    /// is not listed.
    void for_each_child(ChildVisitor visit) const override
    {
        visit(*scroll_);
        if (item_menu_ && detail::TreeAccess::popup_request(*item_menu_) != nullptr)
        {
            visit(*item_menu_);
        }
    }
    bool                                    raise_context_requested(std::optional<Point> position) override;
    [[nodiscard]] std::optional<ScrollInfo> scroll_info() const override;

    // ---- drag and drop (as a drop target and a source) ----
    void         drag_enter(const DragEventArgs& e) override;
    DragResponse drag_over(const DragEventArgs& e) override;
    void         drag_leave(const DragEventArgs& e) override;
    void         drop(const DragEventArgs& e) override;
    void         drag_completed(DataPackageOperation result) override;

    // ---- automation ----
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::List;
    }

protected:
    // ---- Element ----
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void               render_override(Renderer& renderer, const RenderContext& context) const override;
    bool               pointer_override(const PointerEvent& e) override;
    [[nodiscard]] bool covers_children_at(Point p) const override;
    void               clear_dirty_override(DirtyFlags flags) override;
    void               tick_override(Duration delta) override;
    void               collect_scope_focusables(std::vector<Element*>& out) override;
    bool               scope_step(int delta) override;
    void               set_scope_entered(bool entered) override;


private:
    [[nodiscard]] bool takes_shortcut(const KeyEvent& e) const
    {
        return e.modifiers.ctrl && e.key == Key::A;
    }
    [[nodiscard]] bool takes_arrow_key(Key key) const;
    [[nodiscard]] bool takes_enter_key() const
    {
        return true;
    }
    Callback<std::vector<MenuItem>(std::size_t)> item_context_menu_;
    std::unique_ptr<MenuFlyoutControl>           item_menu_; // made on the first item menu
    friend class ListItemContainer;
    friend class ListViewControl; // the only two kinds
    friend class TileViewControl;

    // Which derived control this is: the items panel and item chrome differ.
    enum class Kind
    {
        List, // a ListView: a StackLayoutEngine of list item containers
        Grid, // a TileView: a TileLayoutEngine of tile item containers
    };
    // Builds the scroll view, repeater and layout for `kind`.
    explicit ListViewBase(Kind kind);

    // ---- flat index space (items and group headers) ----
    [[nodiscard]] std::size_t                flat_count() const;
    [[nodiscard]] std::optional<std::size_t> flat_to_item(std::size_t flat) const;
    [[nodiscard]] std::optional<std::size_t> flat_to_group(std::size_t flat) const;
    [[nodiscard]] std::size_t                item_to_flat(std::size_t item) const;
    [[nodiscard]] ListItemContainer*         container_for_item(std::size_t item) const;
    std::unique_ptr<Element>                 make_container(std::size_t flat);
    void                                     reset_repeater();
    void                                     invalidate_containers();

    void replace_header(std::unique_ptr<Element> element, bool header);
    void set_count(std::size_t count);

    // ---- selection ----
    void primary_gesture(std::size_t item, bool shift);
    void secondary_gesture(std::size_t item, bool shift);
    void keyboard_moved(std::size_t item, bool ctrl, bool shift);
    void selection_changed();
    void item_pressed(std::size_t item);
    void item_released(std::size_t item, bool ctrl, bool shift, int click_count);
    void invoke_item(std::size_t item);
    // An accessibility Invoke on an item: raises on_item_click and on_item_invoked, as Enter does.
    void invoke_from_automation(std::size_t item);

    // ---- groups ----
    [[nodiscard]] bool grouped() const
    {
        return !group_keys_.empty();
    }
    // Rebuilds the display order from the keys and the item count.
    void regroup();
    // The item shown at display position `rank`, and the display position of `item`.
    [[nodiscard]] std::size_t item_at_rank(std::size_t rank) const
    {
        return grouped() && rank < order_.size() ? order_[rank] : rank;
    }
    [[nodiscard]] std::size_t rank_of(std::size_t item) const
    {
        return grouped() && item < rank_.size() ? rank_[item] : item;
    }
    // The group display position `rank` falls in.
    [[nodiscard]] std::size_t group_of_rank(std::size_t rank) const;
    // Sets every item between `a` and `b` in display order, both included.
    bool set_span(detail::ItemSelection& s, std::size_t a, std::size_t b, bool selected);
    // Hands the group sizes to a tile layout, which puts each header on a line of its own.
    void push_group_sizes();

    // ---- navigation ----
    void                                     set_current(std::size_t item);
    [[nodiscard]] std::optional<std::size_t> neighbor(std::size_t from, Key key);
    // The part in view of the item the keyboard is on, or nullopt when there is none.
    [[nodiscard]] std::optional<Rect> ring_item_rect() const;

    // ---- drag ----
    void start_item_drag(std::size_t item, Point pointer);
    void update_reorder(std::optional<std::size_t> insert_before, float slot);
    // Takes the gap away: the items slide back with `animate`, else jump back at once.
    void clear_reorder(bool animate = false);
    // The room a reorder drag opens: the dragged item's cell along the view's axis.
    [[nodiscard]] float reorder_slot() const;
    // Scrolls a view a drag is held near the start or the end of; true when it moved.
    bool                                     drag_scroll(Duration delta);
    [[nodiscard]] std::optional<std::size_t> dragged_item() const;
    // insertion_index_at() in a TileView: the line the point is level with, then the place along it.
    [[nodiscard]] std::size_t tile_insertion_index_at(Point position) const;
    // Where the cell of `item` is in a TileView, worked out from the realized cells, for an item
    // with no container: off screen, or one past the last. Nullopt until two line starts are realized.
    [[nodiscard]] std::optional<Point> tile_slot_position(std::size_t item) const;

    // ---- transitions ----
    void open_batch(bool reset);
    // Calls on_selected_index_shifted when selected_index() differs from `was_selected`.
    void report_index_shift(std::optional<std::size_t> was_selected);
    void resolve_batch();
    void add_ghost(std::size_t item);

    // ---- sticky headers ----
    void update_sticky_header();

    Kind kind_;

    std::vector<std::string> items_;
    std::size_t              count_ = 0;
    ItemTemplate             item_template_;
    std::vector<std::string> group_keys_;
    GroupHeaderTemplate      group_header_template_;
    bool                     sticky_headers_ = true;
    // Derived by regroup(): each group's key, size, first display position and header's flat
    // index; the items in display order, and each item's display position.
    std::vector<std::string> group_names_;
    std::vector<std::size_t> group_sizes_;
    std::vector<std::size_t> group_start_;
    std::vector<std::size_t> group_flat_;
    std::vector<std::size_t> order_;
    std::vector<std::size_t> rank_;

    std::unique_ptr<ScrollControl> scroll_owner_;
    ScrollControl*                 scroll_   = nullptr;
    StackControl*                  content_  = nullptr;
    ForEachControl*                repeater_ = nullptr;
    Element*                       header_   = nullptr;
    std::string                    header_text_{};
    Element*                       footer_ = nullptr;

    std::unique_ptr<detail::ItemSelection> selection_;
    SelectionMode                          selection_mode_          = SelectionMode::Single;
    bool                                   selection_follows_focus_ = true;
    bool                                   item_click_enabled_      = false;
    bool                                   item_invoked_enabled_    = false;
    bool                                   can_drag_items_          = false;
    bool                                   can_reorder_items_       = false;
    ItemsUpdatingScrollMode                updating_mode_           = ItemsUpdatingScrollMode::KeepItemsInView;
    bool                                   keep_last_pending_       = false;

    Thickness            container_margin_{};
    Alignment            content_h_align_;
    std::optional<float> item_height_;

    BackgroundBrush background_;
    BackgroundBrush foreground_;
    BackgroundBrush border_brush_;
    Thickness       border_thickness_{};
    Thickness       padding_{};

    std::optional<std::size_t> current_;
    std::size_t                focus_index_   = detail::NoIndex;
    bool                       scope_entered_ = false;

    // A press on an item that may become a drag.
    std::optional<std::size_t> press_item_;
    Point                      press_point_{};
    std::vector<std::size_t>   dragging_;
    std::optional<std::size_t> gap_before_; // where a reorder drag would put the item
    std::optional<Point>       drag_point_; // where an accepted drag over the list last was
    mutable const Theme*       last_theme_ = nullptr;

    // ---- item container transitions ----
    TransitionList             item_transitions_;
    std::vector<std::uint64_t> ids_; // per item, stable across moves
    std::uint64_t              next_id_ = 1;
    struct Batch
    {
        bool                                     open  = false;
        bool                                     reset = false;
        std::unordered_map<std::uint64_t, Point> before; // realized items' positions, repeater-relative
    };
    Batch batch_;
    bool  entered_ = false; // items have been shown (Entrance plays once)
    struct Ghost
    {
        RenderRecording      recording;
        Point                offset{}; // repeater-relative position when it went
        Rect                 recorded_at{};
        TransitionStoryboard storyboard;
        Duration             elapsed{};
    };
    std::vector<Ghost> ghosts_;

    // ---- sticky header ----
    std::unique_ptr<Element>    sticky_;
    std::optional<std::size_t>  sticky_group_;
    float                       sticky_y_      = 0.0f;
    float                       sticky_h_      = 0.0f;
    mutable const TextMeasurer* last_measurer_ = nullptr;

    Callback<void(std::optional<std::size_t>)>       on_selection_changed_;
    Callback<void(std::optional<std::size_t>)>       on_selected_index_shifted_;
    Callback<void(std::size_t)>                      on_item_click_;
    Callback<void(std::size_t)>                      on_item_invoked_;
    Callback<Decision(const DragItemsStartingArgs&)> on_drag_items_starting_;
    Callback<void(const DragItemsCompletedArgs&)>    on_drag_items_completed_;
    Callback<void(const ItemsReorderedArgs&)>        on_items_reordered_;

protected:
    void enabled_changed() override;
    bool key_override(const KeyEvent& e) override;
    void focus_changed(bool focused) override;

protected:
    /// Inside the item the keyboard is on, cut to the part of it in view. Around the whole list
    /// when that item is out of view.
    [[nodiscard]] Rect  focus_ring_rect(const Theme& theme) const override;
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override;
    [[nodiscard]] bool  focus_ring_inside() const override;

protected:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.selection = this;
        out.value     = this;
    }

private:
    // The selected item's text, for a list of strings.
    [[nodiscard]] std::string text_value() const override;
    // The realized containers of the selected items.
    void selected_items(std::vector<Element*>& out) const override;
    // True in Multiple and Extended mode.
    [[nodiscard]] bool can_select_multiple() const override
    {
        return selection_mode_ == SelectionMode::Multiple || selection_mode_ == SelectionMode::Extended;
    }
    [[nodiscard]] bool selection_required() const override
    {
        return false;
    }
    [[nodiscard]] bool text_value_read_only() const override
    {
        return true;
    }
    bool set_text_value(std::string_view /*text*/) override
    {
        return false;
    }
};

} // namespace ui
