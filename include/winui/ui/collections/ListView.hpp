// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/ListViewBase.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/ItemsModel.hpp"
#include "winui/ui/spec/Spec.hpp"

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// A vertical, virtualized, selectable list of templated items.
///
/// Each item sits in a container with rounded chrome: a backplate for hover and press, a 3 pixel
/// accent selection indicator in Single and Extended mode, and a check box per item in Multiple
/// mode. Items, templates, selection, groups, header and footer, drag reordering and scrolling
/// come from ListViewBase.
///
/// Example:
/// @code
/// // Declarative: the items and how one row looks (plain strings need no `.item`).
/// std::vector<std::string> fruits{"Apple", "Banana", "Cherry"};
/// ui::Control<ui::ListView<std::string>> list;
/// window.set_content(ui::ListView<std::string>{
///     .items                = fruits,
///     .selection_mode       = ui::SelectionMode::Single,
///     .height               = 240.0f,
///     .on_selection_changed = [&](std::optional<std::string> fruit)
///     { std::println("selected {}", fruit ? *fruit : "nothing"); },
///     .control              = list,
///     .item                 = [](const std::string& fruit) -> ui::Spec {
///         return ui::Text{.text = fruit};
///     },
/// });
///
/// // Imperative:
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto names = std::make_unique<ui::ListViewControl>();
/// names->set_items({"One", "Two", "Three"});
/// names->set_item_click_enabled(true);
/// names->on_item_click([](std::size_t i) { std::println("clicked {}", i); });
/// stack->add(std::move(names));
/// @endcode
class ListViewControl final : public ListViewBase
{
public:
    /// An empty list in Single selection mode.
    ListViewControl();
};

namespace detail
{
// The index-based build behind ui::ListView<T> (below): the strings in `items`, or
// `item_count` items an `item_template` makes.
struct ListViewSpec
{
    /// The control this spec builds; what `ui::Control<ui::ListView<T>>` points at.
    using control_type = ListViewControl;

    // ---- properties ----
    /// A plain-text header above the first item, in the group headers' text style.
    /// `header_content` wins over it.
    Binding<std::string> header{};
    /// Plain-text items, one row each. Ignored when `item_template` is set.
    std::vector<std::string> items{};
    /// The selected item's index. Unset, the default, means nothing is selected, and a bound one
    /// can return to unset later (ui::ListViewControl::set_selected_index).
    TwoWayField<std::optional<std::size_t>> selected_index{};
    /// Every selected item, as ascending positions in the collection. Bind a
    /// `ui::Property<std::vector<std::size_t>>` to read and set the selection under Multiple or
    /// Extended. Two-way with ui::bind(). Left empty and unbound, `.selected_index` decides.
    Binding<std::vector<std::size_t>> selected_indices{};
    /// A fixed row height in DIPs. Unset (the default) sizes each row to its content, at least
    /// 40 DIPs.
    Binding<std::optional<float>> item_height{};
    /// Transitions every item container runs
    /// (ui::ListViewControl::set_item_transitions()).
    Binding<TransitionList> item_transitions{};
    /// With `item_template`: how many items the collection holds. A number, or
    /// `ui::item_count(collection)` to follow a ui::ObservableVector.
    ItemCount item_count{};
    /// Builds the content of item `index`. Wins over `items`.
    ItemTemplate item_template{};
    /// None, Single (the default), Multiple (check boxes) or Extended.
    Binding<SelectionMode> selection_mode{SelectionMode::Single};
    /// Whether clicks raise `on_item_click`, before they select. Setting `on_item_click` turns it
    /// on unless it is bound.
    Binding<bool> item_click_enabled{false};
    /// Whether Enter and double-clicks raise `on_item_invoked`. Setting `on_item_invoked` turns it
    /// on unless it is bound.
    Binding<bool> item_invoked_enabled{false};
    /// Whether items can be dragged out; see `on_drag_items_starting`.
    Binding<bool> can_drag_items{false};
    /// Whether dragging within the list moves the item; see `on_items_reordered`.
    Binding<bool> can_reorder_items{false};
    /// Whether the list accepts drops.
    Binding<bool> allow_drop{false};
    /// One group key per item, asked again on every collection change. Empty leaves the items
    /// ungrouped.
    Callback<std::vector<std::string>()> group_keys{};
    /// A group header's content, given the group's key. Unset shows the key.
    Callback<Spec(const std::string& key)> group_header_template{};
    /// Whether the current group's header stays pinned at the top while its items scroll under it.
    Binding<bool> sticky_group_headers{true};
    /// What stays in view as items arrive. KeepLastItemInView makes a chat-style log follow its end.
    Binding<ItemsUpdatingScrollMode> items_updating_scroll_mode{ItemsUpdatingScrollMode::KeepItemsInView};


    // ---- appearance / layout ----
    /// Background behind the rows; nullopt draws none.
    Binding<Paint> background{};
    /// The border color: a fixed color or a theme color.
    Binding<Paint> border_brush{};
    /// The border thickness, DIPs; zero (the default) draws no border.
    Binding<Thickness> border_thickness{};
    /// Where the rows sit vertically when they do not fill the list (Trailing: a short list sits at
    /// the bottom).
    Binding<Alignment> items_panel_vertical_alignment{Alignment::Stretch};
    /// Every item container's margin, and its content's horizontal alignment
    /// (`item_content_horizontal_alignment`).
    Binding<Thickness> item_container_margin{};
    Binding<Alignment> item_content_horizontal_alignment{Alignment::Stretch}; ///< See `item_container_margin`.
    Style              style{};                                               ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};                               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};                               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};                            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};                           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};                            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};                           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{};                 ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};                   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true}; ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// Called with the new selected index, unset when nothing is selected, when the user or
    /// set_selected_index() changes the selection. A change that comes from a binding, a write to
    /// `selected_index` or a change of `.items`, doesn't run it, and a bound `selected_index` still
    /// follows.
    Callback<void(std::optional<std::size_t>)> on_selection_changed{};
    /// Called with the clicked item, before the click selects. Turns `item_click_enabled` on.
    Callback<void(std::size_t)> on_item_click{};
    /// Called with the item on Enter or a double-click, after the gesture selects. With
    /// SelectionMode::None a click or Space also invokes. Turns `item_invoked_enabled` on.
    Callback<void(std::size_t)> on_item_invoked{};
    /// A drag of items starts. Fill in the drag package, or cancel.
    Callback<Decision(const DragItemsStartingArgs&)> on_drag_items_starting{};
    /// A drag of items ended, with the dragged items and what the drop did (None when refused).
    Callback<void(const DragItemsCompletedArgs&)> on_drag_items_completed{};
    /// A reorder moved item `from` to `to`. A string list has already moved it. With a template,
    /// move the model's data here.
    Callback<void(const ItemsReorderedArgs&)> on_items_reordered{};
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
    ControlOut<ListViewSpec> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// Content above the first item (`header_content`) and after the last (`footer_content`). Both
    /// scroll with the items.
    Spec header_content{};
    Spec footer_content{}; ///< See `header_content`.

    // ---- templates ----
    /// The context menu of item `index`. An empty list opens none.
    Callback<std::vector<MenuItem>(std::size_t index)> item_context_menu{};

    /// Consumes the spec and returns the built ui::ListViewControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};
} // namespace detail

/// A virtualized, selectable list of rows. Builds a ui::ListViewControl.
///
/// `.items` is the collection and `.item` is how one row looks. The handlers receive the item
/// they are about. Only rows in view are realized, so `.item` runs per visible row. An item type
/// that is text (`std::string`) needs no `.item`.
///
/// `.items` takes the same three sources as ui::ForEach, and the list follows the first two:
///
/// - A ui::ObservableVector<T>: every insert, removal and move reaches the list as that change,
///   and `.item` may take the item as `T&`.
/// - A `ui::Property<std::vector<T>>`: with `.key`, a reorder is followed as moves, so the
///   selection stays on its item through a sort or a filter. A change that needs very many moves,
///   such as a shuffle of several thousand items, arrives as one reset instead, which rebuilds
///   the rows and clears the selection. Without `.key`, the rows before and after an edit are
///   kept.
/// - A plain `std::vector<T>` or a braced list, copied once.
///
/// A reorder by the user (`can_reorder_items`) is written back. An ObservableVector is moved in
/// place, and a bound Property is assigned the reordered vector.
///
/// Name the item type, as in `ui::ListView<Contact>`. C++ deduces a spec's type only from the
/// field's own type, which a Property or an ObservableVector is not. The control works with
/// indices, so through `.control` an item is its position in the collection.
///
/// Example:
/// @code
/// struct Contact
/// {
///     int         id = 0;
///     std::string name;
///     std::string company;
///     friend bool operator==(const Contact&, const Contact&) = default;
/// };
/// ui::Property<std::vector<Contact>> contacts{{{1, "Ada Lovelace", "Analytical"}, {2, "Grace Hopper", "Navy"}}};
/// ui::Property<std::string>          status{};
///
/// window.set_content(ui::ListView<Contact>{
///     .items                = contacts,
///     .key                  = &Contact::id,
///     .height               = 400.0f,
///     .on_selection_changed = [&](std::optional<Contact> contact)
///     { status = contact ? "Selected " + contact->name : ""; },
///     .item                 = [](const Contact& contact) -> ui::Spec
///     {
///         return ui::VerticalStack{.content = {
///             ui::Text{.text = contact.name},
///             ui::Text{.text = contact.company, .role = ui::TextRole::Caption},
///         }};
///     },
/// });
///
/// // Plain text needs no template.
/// ui::ListView<std::string>{.items = {"Inbox", "Drafts", "Sent"}};
/// @endcode
template <typename T>
struct ListView
{
    /// The control this spec builds; what `ui::Control<ListView>` points at.
    using control_type = ListViewControl;

    // ---- properties ----
    /// A plain-text header above the first item, in the group headers' text style.
    /// `header_content` wins over it.
    Binding<std::string> header{};
    /// The collection. A ui::ObservableVector<T> or a `ui::Property<std::vector<T>>` is followed as
    /// it changes. A plain vector or a braced list is copied once.
    Binding<std::vector<T>> items{};
    /// What identifies an item when `.items` is bound to a property: a member pointer or a
    /// callable returning the key (see ui::ItemKey). Keys must be unique.
    ItemKey<std::type_identity_t<T>> key{};
    /// The selected item's index. Unset, the default, means nothing is selected, and a bound one
    /// can return to unset later (ui::ListViewControl::set_selected_index).
    TwoWayField<std::optional<std::size_t>> selected_index{};
    /// Every selected item, as ascending positions in the collection. Bind a
    /// `ui::Property<std::vector<std::size_t>>` to read and set the selection under Multiple or
    /// Extended. Two-way with ui::bind(). Left empty and unbound, `.selected_index` decides.
    Binding<std::vector<std::size_t>> selected_indices{};
    /// A fixed row height in DIPs. Unset (the default) sizes each row to its content, at least
    /// 40 DIPs.
    Binding<std::optional<float>> item_height{};
    /// Transitions every item container runs
    /// (ui::ListViewControl::set_item_transitions()).
    Binding<TransitionList> item_transitions{};
    /// None, Single (the default), Multiple (check boxes) or Extended.
    Binding<SelectionMode> selection_mode{SelectionMode::Single};
    /// Whether clicks raise `on_item_click`, before they select. Setting `on_item_click` turns it
    /// on unless it is bound.
    Binding<bool> item_click_enabled{false};
    /// Whether Enter and double-clicks raise `on_item_invoked`. Setting `on_item_invoked` turns it
    /// on unless it is bound.
    Binding<bool> item_invoked_enabled{false};
    /// Whether items can be dragged out; see `on_drag_items_starting`.
    Binding<bool> can_drag_items{false};
    /// Whether dragging within the list moves the item, in the list and in the collection.
    Binding<bool> can_reorder_items{false};
    /// Whether the list accepts drops.
    Binding<bool> allow_drop{false};
    /// Files the items under group headers by the key it returns for each item. The groups come in
    /// the order their keys first appear, and the items keep their order within a group, without
    /// sorting. Asked again on every collection change. Positions in the collection stay the
    /// indices.
    Callback<std::string(const std::type_identity_t<T>&)> group_by{};
    /// Whether the current group's header stays pinned at the top while its items scroll under it.
    Binding<bool> sticky_group_headers{true};
    /// What stays in view as items arrive. KeepLastItemInView makes a chat-style log follow its end.
    Binding<ItemsUpdatingScrollMode> items_updating_scroll_mode{ItemsUpdatingScrollMode::KeepItemsInView};


    // ---- appearance / layout ----
    /// Background behind the rows; nullopt draws none.
    Binding<Paint> background{};
    /// The border color: a fixed color or a theme color.
    Binding<Paint> border_brush{};
    /// The border thickness, DIPs; zero (the default) draws no border.
    Binding<Thickness> border_thickness{};
    /// Where the rows sit vertically when they do not fill the list (Trailing: a short list sits at
    /// the bottom).
    Binding<Alignment> items_panel_vertical_alignment{Alignment::Stretch};
    /// Every item container's margin, and its content's horizontal alignment
    /// (`item_content_horizontal_alignment`).
    Binding<Thickness> item_container_margin{};
    Binding<Alignment> item_content_horizontal_alignment{Alignment::Stretch}; ///< See `item_container_margin`.
    Style              style{};                                               ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};                               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};                               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};                            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};                           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};                            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};                           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{};                 ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};                   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true}; ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// Called with a copy of the first selected item, unset when nothing is selected (see
    /// ui::SelectedItem), when the user or set_selected_index() changes the selection. A change
    /// that comes from a binding, a write to `selected_index` or a change of `.items`, doesn't run
    /// it. The copy is safe to keep.
    Callback<void(SelectedItem<std::type_identity_t<T>>)> on_selection_changed{};
    /// Called with the clicked item, before the click selects. Turns `item_click_enabled` on. The
    /// reference is valid during the call. An item of a ui::ObservableVector stays at its address
    /// while it is in the collection.
    Callback<void(const std::type_identity_t<T>&)> on_item_click{};
    /// Called with the item on Enter or a double-click. With SelectionMode::None a click or Space
    /// also invokes. Turns `item_invoked_enabled` on. The reference is valid as for
    /// `on_item_click`.
    Callback<void(const std::type_identity_t<T>&)> on_item_invoked{};
    /// A drag of items starts. Fill in the drag package, or cancel. The args name the items by
    /// position.
    Callback<Decision(const DragItemsStartingArgs&)> on_drag_items_starting{};
    /// A drag of items ended, with the positions of the dragged items and what the drop did (None
    /// when refused).
    Callback<void(const DragItemsCompletedArgs&)> on_drag_items_completed{};
    /// A reorder moved the item at position `from` to `to`. The collection has already been moved.
    Callback<void(const ItemsReorderedArgs&)> on_items_reordered{};
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
    ControlOut<std::type_identity_t<ListView>> control{};

    // ---- children ----
    /// Content above the first row (`header_content`) and after the last (`footer_content`). Both
    /// scroll with the rows.
    Spec header_content{};
    Spec footer_content{}; ///< See `header_content`.
    /// How one row looks: a callable taking `const T&`, or `T&` to edit an item of a
    /// ui::ObservableVector. Left out, an item that is text is shown as that text. Any other item
    /// type needs one, and a debug build asserts without it.
    Callback<Spec(std::type_identity_t<T>&)> item{};
    /// A group header's content, given the group's key. Left out, the header shows the key as
    /// text. Needs `.group_by`.
    Callback<Spec(const std::string& key)> group_header_template{};
    /// The context menu of an item: a callable taking the item and returning the menu's entries.
    /// Called on a right-click on the item, or on Shift+F10 or the Menu key while it is current.
    /// The item is selected first. An empty list opens no menu. An item of a ui::ObservableVector
    /// stays where it is while it is in the collection, so an entry's `on_click` may keep a
    /// reference to it. Any other item is valid during the call only, so capture it by value.
    Callback<std::vector<MenuItem>(const std::type_identity_t<T>&)> item_context_menu{};

    /// Consumes the spec and returns the built ui::ListViewControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&
    {
        const bool            has_item = static_cast<bool>(item);
        detail::ItemsModel<T> model    = detail::items_model<T>(items, key);

        detail::ListViewSpec core;
        // A fixed list of strings keeps the control's own text rows.
        bool text_rows = false;
        if constexpr (std::same_as<T, std::string>)
        {
            text_rows = !has_item && !model.reactive;
            if (text_rows)
            {
                core.items = model.state->copy;
            }
        }
        if (!text_rows)
        {
            core.item_template = detail::indexed_template<T>(model.state, std::move(item));
            core.item_count    = std::move(model.count);
        }
        core.selected_index                    = std::move(selected_index);
        core.selected_indices                  = std::move(selected_indices);
        core.item_height                       = std::move(item_height);
        core.enabled                           = std::move(enabled);
        core.item_transitions                  = std::move(item_transitions);
        core.selection_mode                    = std::move(selection_mode);
        core.item_click_enabled                = std::move(item_click_enabled);
        core.item_invoked_enabled              = std::move(item_invoked_enabled);
        core.can_drag_items                    = std::move(can_drag_items);
        core.can_reorder_items                 = std::move(can_reorder_items);
        core.allow_drop                        = std::move(allow_drop);
        core.group_keys                        = detail::indexed_keys<T>(model.state, std::move(group_by));
        core.group_header_template             = std::move(group_header_template);
        core.sticky_group_headers              = std::move(sticky_group_headers);
        core.items_updating_scroll_mode        = std::move(items_updating_scroll_mode);
        core.background                        = std::move(background);
        core.border_brush                      = std::move(border_brush);
        core.border_thickness                  = std::move(border_thickness);
        core.items_panel_vertical_alignment    = std::move(items_panel_vertical_alignment);
        core.item_container_margin             = std::move(item_container_margin);
        core.item_content_horizontal_alignment = std::move(item_content_horizontal_alignment);
        detail::move_common(*this, core);

        core.on_selection_changed    = detail::indexed_selection<T>(model.state, std::move(on_selection_changed));
        core.on_item_click           = detail::indexed_handler<T>(model.state, std::move(on_item_click));
        core.on_item_invoked         = detail::indexed_handler<T>(model.state, std::move(on_item_invoked));
        core.on_drag_items_starting  = std::move(on_drag_items_starting);
        core.on_drag_items_completed = std::move(on_drag_items_completed);
        core.on_items_reordered =
            [state = model.state, handler = std::move(on_items_reordered)](const ItemsReorderedArgs& args) mutable
        {
            state->move(args.from, args.to);
            if (handler)
            {
                handler(args);
            }
        };
        core.header            = std::move(header);
        core.header_content    = std::move(header_content);
        core.footer_content    = std::move(footer_content);
        core.item_context_menu = detail::indexed_function<T>(model.state, std::move(item_context_menu));

        std::unique_ptr<Element> node = std::move(core).build();
        publish_control(control, static_cast<ListViewControl*>(node.get()));
        return node;
    }
};

} // namespace ui
