// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/core/OptionalFloat.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/ListViewBase.hpp"
#include "winui/ui/layouts/Layout.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/ItemsModel.hpp"
#include "winui/ui/spec/LayoutSpec.hpp"
#include "winui/ui/spec/Spec.hpp"

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

class TileLayoutEngine;

/// Templated items in wrapping rows of equal cells, virtualized and selectable.
///
/// Every cell is the size of the first item's container, or set_item_width() and
/// set_item_height_cell(). A line holds as many cells as fit, or maximum_rows_or_columns(). Each
/// item sits in a container with a backplate for hover and press, a 2 pixel accent frame when
/// selected, and a check box in the top-right corner in Multiple mode. Items, templates,
/// selection, drag reordering and scrolling come from ListViewBase. When grouped
/// (set_group_keys()), each group header takes a line of its own and its cells start on the next.
///
/// Example:
/// @code
/// // Declarative: a 3-column picker of 96x96 tiles.
/// std::vector<std::string> names{"Red", "Green", "Blue", "Cyan", "Magenta", "Yellow"};
/// window.set_content(ui::TileView<std::string>{
///     .items                   = names,
///     .maximum_rows_or_columns = 3,
///     .on_item_click           = [](const std::string& name) { std::println("picked {}", name); },
///     .item                    = [](const std::string& name) -> ui::Spec {
///         return ui::Text{.text = name, .width = 96.0f, .height = 96.0f};
///     },
/// });
///
/// // Imperative:
/// auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
/// auto grid  = std::make_unique<ui::TileViewControl>();
/// grid->set_item_template([](std::size_t i) { return std::make_unique<ui::TextControl>(std::to_string(i)); });
/// grid->set_item_count(12);
/// grid->set_item_width(100.0f);
/// grid->set_item_height_cell(64.0f);
/// grid->set_selection_mode(ui::SelectionMode::Multiple);
/// stack->add(std::move(grid));
/// @endcode
class TileViewControl final : public ListViewBase
{
public:
    /// An empty grid in Single selection mode, horizontal orientation.
    TileViewControl();

    /// Replaces the layout that places the cells and returns an observer. The default is equal
    /// cells (a TileLayoutEngine). Use a WrapLayoutEngine for cells of their own width, or a
    /// LinedFlowLayoutEngine for a photo wall. nullptr restores the default. With a layout that is
    /// not a TileLayoutEngine, the cell setters below have no effect, and the arrow keys find the
    /// item above or below by where the items are.
    template <typename T>
    T* set_layout(std::unique_ptr<T> layout)
    {
        static_assert(std::is_base_of_v<Layout, T>, "a TileView's layout must be a Layout");
        T* observer = layout.get();
        replace_layout(std::move(layout));
        return observer;
    }

    /// The most cells per row (per column when Vertical); 0 (the default) is as many as fit.
    [[nodiscard]] std::size_t maximum_rows_or_columns() const;
    /// Caps the cells per row (per column when Vertical) and lays the view out again.
    void set_maximum_rows_or_columns(std::size_t max);
    /// The fill direction; Horizontal by default.
    [[nodiscard]] Orientation orientation() const
    {
        return orientation_;
    }
    /// Changes the fill direction, and the scroll axis with it, and lays the view out again.
    void set_orientation(Orientation orientation);
    /// Sets the cell width in DIPs. nullopt, the default, takes the first item's width.
    void set_item_width(OptionalFloat width);
    /// Sets the cell height in DIPs. nullopt, the default, takes the first item's height. It has
    /// its own name because ListViewBase::set_item_height() sets the list container height.
    void set_item_height_cell(OptionalFloat height);

private:
    void replace_layout(std::unique_ptr<Layout> layout);

    TileLayoutEngine* grid_        = nullptr; // the layout, while it is a tile layout
    Orientation       orientation_ = Orientation::Horizontal;
};

namespace detail
{
// The index-based build behind ui::TileView<T> (below).
struct TileViewSpec
{
    /// The control this spec builds; what `ui::Control<ui::TileView<T>>` points at.
    using control_type = TileViewControl;

    // ---- properties ----
    /// A plain-text header above the first cell, in the group headers' text style.
    /// `header_content` wins over it.
    Binding<std::string> header{};
    /// Plain-text items, one cell each. Ignored when `item_template` is set.
    std::vector<std::string> items{};
    /// The selected item's index. Unset, the default, means nothing is selected.
    TwoWayField<std::optional<std::size_t>> selected_index{};
    /// Every selected item, as ascending positions in the collection. Bind a
    /// `ui::Property<std::vector<std::size_t>>` to read and set the selection under Multiple or
    /// Extended. Two-way with ui::bind(). Left empty and unbound, `.selected_index` decides.
    Binding<std::vector<std::size_t>> selected_indices{};
    /// Transitions every item container runs, as ui::ListView's.
    Binding<TransitionList> item_transitions{};
    /// With `item_template`: how many items the collection holds. A number, or
    /// `ui::item_count(collection)` to follow a ui::ObservableVector.
    ItemCount item_count{};
    /// Builds the content of item `index`. Wins over `items`.
    ItemTemplate item_template{};
    /// The selection mode, as ui::ListView's.
    Binding<SelectionMode> selection_mode{SelectionMode::Single};
    /// Whether clicks raise `on_item_click`, before they select. Setting `on_item_click` turns it
    /// on unless it is bound.
    Binding<bool> item_click_enabled{false};
    /// Whether Enter and double-clicks raise `on_item_invoked`. Setting `on_item_invoked` turns it
    /// on unless it is bound.
    Binding<bool> item_invoked_enabled{false};
    /// Whether items can be dragged out.
    Binding<bool> can_drag_items{false};
    /// Whether dragging within the grid moves the item.
    Binding<bool> can_reorder_items{false};
    /// Whether the grid accepts drops.
    Binding<bool> allow_drop{false};
    /// One group key per item, asked again on every collection change. Empty leaves the items
    /// ungrouped.
    Callback<std::vector<std::string>()> group_keys{};
    /// A group header's content, given the group's key. Unset shows the key.
    Callback<Spec(const std::string& key)> group_header_template{};
    /// Whether the current group's header stays pinned at the top while its cells scroll under it.
    Binding<bool> sticky_group_headers{true};
    /// The most cells in a line before wrapping; 0 is as many as fit. With a `layout`, 0 keeps the
    /// layout's own cap.
    Binding<std::size_t> maximum_rows_or_columns{0};
    /// The layout that places the cells. A tile layout of equal cells when left out.
    LayoutSpec layout{};
    /// Horizontal fills rows and scrolls vertically. Unset takes the orientation of a tile
    /// `layout`, else Horizontal.
    Binding<std::optional<Orientation>> orientation{};


    // ---- appearance / layout ----
    /// Background behind the cells; nullopt draws none.
    Binding<Paint> background{};
    /// The border color: a fixed color, or a theme color read again on a theme change.
    Binding<Paint> border_brush{};
    /// The border thickness, DIPs; zero (the default) draws no border.
    Binding<Thickness> border_thickness{};
    /// Every item container's margin. nullopt keeps the default 0,0,4,4.
    Binding<std::optional<Thickness>> item_container_margin{std::nullopt};
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
    /// Called with the new selected index, unset when nothing is selected, when the selection
    /// changes. A change that comes from a binding, a write to `selected_index` or a change of
    /// `.items`, doesn't run it.
    Callback<void(std::optional<std::size_t>)> on_selection_changed{};
    /// Called with the clicked item. Turns `item_click_enabled` on.
    Callback<void(std::size_t)> on_item_click{};
    /// Called with the item on Enter or a double-click, after the gesture selects. With
    /// SelectionMode::None a click or Space also invokes. Turns `item_invoked_enabled` on.
    Callback<void(std::size_t)> on_item_invoked{};
    /// A drag of items starts. Fill in the drag package, or cancel.
    Callback<Decision(const DragItemsStartingArgs&)> on_drag_items_starting{};
    /// A drag of items ended, with the dragged items and what the drop did.
    Callback<void(const DragItemsCompletedArgs&)> on_drag_items_completed{};
    /// A reorder moved item `from` to `to`. With a template, move the model's data here.
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
    ControlOut<TileViewSpec> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// Content above the first cell (`header_content`) and after the last (`footer_content`). Both
    /// scroll with the cells.
    Spec header_content{};
    Spec footer_content{}; ///< See `header_content`.

    // ---- templates ----
    /// The context menu of item `index`. An empty list opens none.
    Callback<std::vector<MenuItem>(std::size_t index)> item_context_menu{};

    /// Consumes the spec and returns the built ui::TileViewControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};
} // namespace detail

/// Templated items in rows of equal cells. Builds a ui::TileViewControl.
///
/// It shares ListViewBase with ui::ListView. `.items`, `.item` and `.key` take the same sources,
/// and selection, click, drag and reorder work the same way. The cells wrap. `.layout` swaps the
/// equal cells for another arrangement and keeps everything else.
///
/// Example:
/// @code
/// std::vector<ui::Color>  swatches{ui::Color::from_rgb(0xE81123), ui::Color::from_rgb(0x0078D4),
///                                  ui::Color::from_rgb(0x107C10), ui::Color::from_rgb(0xFFB900)};
/// ui::Property<ui::Color> picked{ui::colors::Black};
///
/// window.set_content(ui::TileView<ui::Color>{
///     .items                   = swatches,
///     .maximum_rows_or_columns = 4,
///     .on_item_click           = [&](const ui::Color& color) { picked = color; },
///     .item                    = [](const ui::Color& color) -> ui::Spec
///     { return ui::Border{.background = color, .width = 40.0f, .height = 40.0f}; },
/// });
/// @endcode
template <typename T>
struct TileView
{
    /// The control this spec builds; what `ui::Control<TileView>` points at.
    using control_type = TileViewControl;

    // ---- properties ----
    /// A plain-text header above the first cell, in the group headers' text style.
    /// `header_content` wins over it.
    Binding<std::string> header{};
    /// The collection: a ui::ObservableVector<T>, a `ui::Property<std::vector<T>>`, a plain vector
    /// or a braced list.
    Binding<std::vector<T>> items{};
    /// What identifies an item when `.items` is bound to a property; see ui::ListView.
    ItemKey<std::type_identity_t<T>> key{};
    /// The selected item's index. Unset, the default, means nothing is selected.
    TwoWayField<std::optional<std::size_t>> selected_index{};
    /// Every selected item, as ascending positions in the collection. Bind a
    /// `ui::Property<std::vector<std::size_t>>` to read and set the selection under Multiple or
    /// Extended. Two-way with ui::bind(). Left empty and unbound, `.selected_index` decides.
    Binding<std::vector<std::size_t>> selected_indices{};
    /// Transitions every item container runs
    /// (ui::TileViewControl::set_item_transitions()).
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
    /// Whether dragging within the view moves the item, in the view and in the collection.
    Binding<bool> can_reorder_items{false};
    /// Whether the view accepts drops.
    Binding<bool> allow_drop{false};
    /// Files the items under group headers by the key it returns for each item. The groups come in
    /// the order their keys first appear, and the items keep their order within a group, without
    /// sorting. Each header takes a line of its own, and the group's cells start on the next. Asked
    /// again on every collection change. Positions in the collection stay the indices.
    Callback<std::string(const std::type_identity_t<T>&)> group_by{};
    /// Whether the current group's header stays pinned at the top while its cells scroll under it.
    Binding<bool> sticky_group_headers{true};
    /// The most cells in a line before wrapping; 0 is no limit.
    /// With a `ui::TileLayout` in `layout`, 0 keeps the layout's own `maximum_rows_or_columns`
    /// and anything else overrides it.
    Binding<std::size_t> maximum_rows_or_columns{0};
    /// How the cells are placed: equal cells when left out, `ui::WrapLayout{}` for cells of their
    /// own width, `ui::LinedFlowLayout{}` for a photo wall. With a layout other than a tile layout,
    /// `maximum_rows_or_columns` and `orientation` have no effect.
    LayoutSpec layout{};
    /// The direction cells are laid in before they wrap, which also sets the scroll axis. Unset
    /// takes the orientation of a `ui::TileLayout` in `layout`, else Horizontal. When set, it
    /// overrides the layout's.
    Binding<std::optional<Orientation>> orientation{};


    // ---- appearance / layout ----
    /// Background behind the cells; nullopt draws none.
    Binding<Paint> background{};
    /// The border color: a fixed color, or a theme color read again on a theme change.
    Binding<Paint> border_brush{};
    /// The border thickness, DIPs; zero (the default) draws no border.
    Binding<Thickness> border_thickness{};
    /// Every item container's margin. nullopt keeps the default 0,0,4,4.
    Binding<std::optional<Thickness>> item_container_margin{std::nullopt};
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
    /// Called with a copy of the first selected item, unset when nothing is selected (see
    /// ui::SelectedItem), when the selection changes. A change that comes from a binding, a write
    /// to `selected_index` or a change of `.items`, doesn't run it. The copy is safe to keep.
    Callback<void(SelectedItem<std::type_identity_t<T>>)> on_selection_changed{};
    /// Called with the clicked item. Turns `item_click_enabled` on. The reference is valid during
    /// the call. An item of a ui::ObservableVector stays at its address while it is in the
    /// collection.
    Callback<void(const std::type_identity_t<T>&)> on_item_click{};
    /// Called with the item on Enter or a double-click. With SelectionMode::None a click or Space
    /// also invokes. Turns `item_invoked_enabled` on. The reference is valid as for
    /// `on_item_click`.
    Callback<void(const std::type_identity_t<T>&)> on_item_invoked{};
    /// A drag of items starts. Fill in the drag package, or cancel. The args name the items by
    /// position.
    Callback<Decision(const DragItemsStartingArgs&)> on_drag_items_starting{};
    /// A drag of items ended, with the positions of the dragged items and what the drop did.
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
    ControlOut<std::type_identity_t<TileView>> control{};

    // ---- children ----
    /// Content above the first cell (`header_content`) and after the last (`footer_content`). Both
    /// scroll with the cells.
    Spec header_content{};
    Spec footer_content{}; ///< See `header_content`.
    /// How one cell looks: a callable taking `const T&`, or `T&` to edit an item of a
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

    /// Consumes the spec and returns the built ui::TileViewControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&
    {
        const bool            has_item = static_cast<bool>(item);
        detail::ItemsModel<T> model    = detail::items_model<T>(items, key);

        detail::TileViewSpec core;
        // A fixed list of strings keeps the control's own text cells.
        bool text_cells = false;
        if constexpr (std::same_as<T, std::string>)
        {
            text_cells = !has_item && !model.reactive;
            if (text_cells)
            {
                core.items = model.state->copy;
            }
        }
        if (!text_cells)
        {
            core.item_template = detail::indexed_template<T>(model.state, std::move(item));
            core.item_count    = std::move(model.count);
        }
        core.selected_index          = std::move(selected_index);
        core.selected_indices        = std::move(selected_indices);
        core.enabled                 = std::move(enabled);
        core.item_transitions        = std::move(item_transitions);
        core.selection_mode          = std::move(selection_mode);
        core.item_click_enabled      = std::move(item_click_enabled);
        core.item_invoked_enabled    = std::move(item_invoked_enabled);
        core.can_drag_items          = std::move(can_drag_items);
        core.can_reorder_items       = std::move(can_reorder_items);
        core.allow_drop              = std::move(allow_drop);
        core.group_keys              = detail::indexed_keys<T>(model.state, std::move(group_by));
        core.group_header_template   = std::move(group_header_template);
        core.sticky_group_headers    = std::move(sticky_group_headers);
        core.maximum_rows_or_columns = std::move(maximum_rows_or_columns);
        core.layout                  = std::move(layout);
        core.orientation             = std::move(orientation);
        core.background              = std::move(background);
        core.border_brush            = std::move(border_brush);
        core.border_thickness        = std::move(border_thickness);
        core.item_container_margin   = std::move(item_container_margin);
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
        publish_control(control, static_cast<TileViewControl*>(node.get()));
        return node;
    }
};

} // namespace ui
