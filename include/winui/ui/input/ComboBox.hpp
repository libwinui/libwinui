// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/ControlHeader.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{
namespace detail
{
class DrawnListAutomation; // src/ui/collections/DrawnListAutomation.hpp
}

class ComboBoxPopup; // defined in ComboBox.cpp

/// What a ComboBox's `on_text_submitted` gets: the typed text.
struct ComboBoxTextSubmittedArgs
{
    std::string text; ///< The committed text, which matches no item.
};

/// A combo box: a closed box showing one item of a list of strings, and a chevron that opens the
/// list as a popup floating above the rest of the tree, so opening it never reflows the layout.
///
/// By default the box is read-only and the value is always one of the items (or nothing, showing
/// the placeholder). A click, Space, F4 or Alt+Down opens the list. While open, hovering or
/// Up/Down moves the highlight, and a click or Enter picks it and closes the list; Escape or a
/// click outside closes it unchanged. While closed, Up/Down change the selection directly. Typing
/// jumps to the next item starting with the typed letters; letters typed within a second of each
/// other add to the search.
///
/// set_editable(true) turns the box into a text field the user can type any value into. Typing at
/// the end of the text completes it to the first item it starts, with the completed part selected
/// so the next letter replaces it. Enter or losing focus commits the text: text equal to an item
/// picks it, other non-blank changed text raises on_text_submitted(). Escape reverts to the last
/// committed text. Only F4 or Alt+Down open the list (Space is an ordinary character), and the
/// list always shows every item (AutoSuggestBox filters as you type). The control stays
/// one tab stop; typing and caret movement go to an internal TextBox, which draws its own caret
/// and selection.
///
/// Example:
/// @code
/// // Declarative: ui::ComboBox builds a ui::ComboBoxControl.
/// ui::Property<std::optional<std::size_t>> size{1};
/// window.set_content(ui::VerticalStack{
///     .spacing  = 8,
///     .content  = {
///         ui::ComboBox{
///             .header         = "Size",
///             .items          = {"Small", "Medium", "Large"},
///             .selected_index = size,
///         },
///         ui::Text{.text = ui::bind(size, [](std::optional<std::size_t> i) { return std::format("Index {}", i.value_or(0)); })},
///     },
/// });
///
/// // Imperative and editable: any text goes, the items are suggestions.
/// auto combo = std::make_unique<ui::ComboBoxControl>(std::vector<std::string>{"Red", "Green", "Blue"});
/// combo->set_editable(true);
/// combo->set_header("Colors");
/// combo->set_placeholder_text("Pick a color");
/// combo->on_item_chosen([](std::size_t index) { /* index into items() */ });
/// combo->on_text_submitted([](const ui::ComboBoxTextSubmittedArgs& args) {
///     return args.text.size() > 20 ? ui::Cancel : ui::Proceed; // too long: not committed
/// });
/// window.set_content(std::move(combo));
/// @endcode
class ComboBoxControl final : public Element, private ExpandCollapseProvider, private ValueProvider
{
public:
    /// An empty, read-only combo box (not focusable until it has items).
    ComboBoxControl();
    /// A read-only combo box offering `items`. `selected_index` is clamped to the list; unset (the
    /// default) starts with nothing selected, showing the placeholder. Sets a 64 DIP minimum width.
    explicit ComboBoxControl(std::vector<std::string> items, std::optional<std::size_t> selected_index = std::nullopt);

    // ---- mode ----
    /// Whether the user can type a value of their own. False (the default) only picks items.
    [[nodiscard]] bool editable() const
    {
        return editable_;
    }
    /// Switches between the read-only box and the text field. The text carries over: turning
    /// editing off keeps the selection, and drops typed text that matches no item. Updates the layout.
    void set_editable(bool editable);

    // ---- items ----
    /// The list's choices (UTF-8), in display order.
    [[nodiscard]] const std::vector<std::string>& items() const
    {
        return items_;
    }
    /// Replaces the item list and closes the popup. A read-only box clears its selection, firing
    /// on_selection_changed() when something was selected; an editable one keeps its text, and
    /// the selection follows it to the new list.
    void set_items(std::vector<std::string> items);

    /// Index into items() of the selected item: the item the text matches exactly, when editable.
    /// Unset when nothing is selected.
    [[nodiscard]] std::optional<std::size_t> selected_index() const
    {
        return detail::optional_index(selected_);
    }
    /// Shows item `index`, as picking it would, without raising on_item_chosen(). Read-only, the
    /// index is clamped to the list; editable, an index past the list is ignored. Unset clears the
    /// selection (and, editable, the text when it shows an item).
    void set_selected_index(std::optional<std::size_t> index);

    // ---- text ----
    /// The text in the box (UTF-8): the selected item's, or empty with nothing selected; when
    /// editable, whatever was typed or picked.
    [[nodiscard]] const std::string& text() const;
    /// Read-only, selects the item equal to `text`, or nothing when none is. Editable, replaces
    /// the text and moves the caret to the end without firing on_text_changed(), which is for
    /// user edits only. The new text is also the committed text Escape reverts to.
    void set_text(std::string text);

    /// The hint shown while nothing is selected or the text is empty; empty by default.
    [[nodiscard]] const std::string& placeholder_text() const
    {
        return placeholder_text_;
    }
    /// Sets the hint ("Pick a color"). Updates the layout, since a read-only box fits its width to it.
    void set_placeholder_text(std::string text);

    /// The label above the control. Empty by default.
    [[nodiscard]] const std::string& header() const
    {
        return header_.text();
    }
    /// Sets the header. Empty draws none and takes no room. A change updates the layout.
    void set_header(std::string header)
    {
        if (header_.set_text(std::move(header)))
        {
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
    /// The secondary text under the box; empty (the default) draws none.
    [[nodiscard]] const std::string& description() const
    {
        return description_.text();
    }
    /// Sets the description. Empty draws none and takes no room. A change updates the layout.
    void set_description(std::string description)
    {
        if (description_.set_text(std::move(description)))
        {
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
    /// What is wrong with the value, shown in the description's place in the critical color,
    /// with the border in the same color; empty (the default) shows the description.
    [[nodiscard]] const std::string& error() const
    {
        return description_.error();
    }
    /// Sets the error. A change updates the layout and redraws.
    void set_error(std::string error)
    {
        if (description_.set_error(std::move(error)))
        {
            text_box_->set_invalid_border(description_.has_error());
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
    /// The description, or the error while one is set, is the help text a screen reader offers
    /// when accessibility().description is empty.
    [[nodiscard]] std::string automation_help_text_fallback() const override
    {
        return description_.shown();
    }
    /// Invalid while an error is set.
    [[nodiscard]] bool automation_invalid() const override
    {
        return description_.has_error();
    }
    /// The header names the control when accessibility().name does not.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return header_.text();
    }


    /// Fires when the selection changes: an item picked, Up/Down or typing while closed,
    /// text typed or set that matches another item or none, set_items() or set_selected_index().
    /// Unset when it was cleared; not for a write that lands on the current item.
    void on_selection_changed(Callback<void(std::optional<std::size_t>)> handler)
    {
        on_selection_changed_ = std::move(handler);
    }

    /// Fires when the user picks an item from the open list (a click, or Enter on the highlighted
    /// item), even if the box already showed it, and when an editable box commits typed text equal
    /// to an item. Receives the index into items(); the box already shows the item when it fires.
    void on_item_chosen(Callback<void(std::size_t)> handler)
    {
        on_item_chosen_ = std::move(handler);
    }

    /// Editable only: fires with the new text after any user-driven text edit (typing, Backspace,
    /// Delete, UI Automation's SetValue), once completion has filled it in. Not fired after
    /// set_text() or when an item is picked (that is on_item_chosen()). Replaces any previous
    /// handler.
    void on_text_changed(Callback<void(const std::string&)> handler)
    {
        on_text_changed_ = std::move(handler);
    }

    /// Editable only: fires when typed text matching no item is committed (see the class comment).
    /// Return ui::Cancel to reject the text, typically after putting the last good value back with
    /// set_text(); the text is then not committed.
    void on_text_submitted(Callback<Decision(const ComboBoxTextSubmittedArgs&)> handler)
    {
        on_text_submitted_ = std::move(handler);
    }

    /// Whether the item list is open. Opened by the user or through UI Automation's
    /// ExpandCollapse; never opens while items() is empty, and there is no public setter.
    [[nodiscard]] bool expanded() const
    {
        return expanded_;
    }

    // ---- focus ----
    /// Whether the control (and so, editable, its inner TextBox) holds keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// True while enabled and holding at least one item; an editable box needs no items, since
    /// it is still a free-text field.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled() && (editable_ || !items_.empty());
    }

protected:
    friend struct testing::VisualStates; // the tests read the state a control draws in
    /// The Normal/Hover/Pressed/Disabled state the control draws in: the whole closed box when
    /// read-only, the chevron button when editable.
    [[nodiscard]] ButtonState visual_state() const;

    // ---- layout ----
    /// Desired width fits the widest item, and the placeholder (read-only) or the inner TextBox's
    /// own desired width (editable), so the box doesn't reflow as the selection changes. Desired
    /// height is the header plus one row, open or closed, since the list floats as a popup.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Places the header, and when editable the inner TextBox beside the chevron column.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    /// A click on the box (read-only) or on the chevron (editable) toggles the list; anything
    /// else in an editable box goes to the TextBox. Clicks on the open list reach the popup.
    bool pointer_override(const PointerEvent& e) override;
    // ---- render ----
    /// Draws the header, the box with its text (or placeholder) and the chevron. The open list is
    /// a popup the window draws.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Ticks the inner TextBox, runs the list's slide-open, where the theme has one
    /// (Theme::popup_motion.list), and times out the typed search.
    void tick_override(Duration delta) override;


public:
    // ---- the internal TextBox ----
    /// Lists the internal TextBox while editable, so it is updated, animated and drawn along
    /// with the combo box.
    void for_each_child(ChildVisitor visit) const override
    {
        if (editable_)
        {
            visit(*text_box_);
        }
    }
    /// One tab stop: the internal TextBox is part of this control, not a stop of its own.
    void collect_focusables(std::vector<Element*>& out) override
    {
        if (focusable())
        {
            out.push_back(this);
        }
    }
    /// The internal TextBox is part of this node and not listed. While the list is open, a List
    /// node of the items, each a ListItem a client can select.
    void automation_children(std::vector<Element*>& out) const override;
    /// The highlighted item while the list is open, so a screen reader reads it as the arrow keys
    /// move.
    [[nodiscard]] Element* automation_focus() const override;
    /// Editable: the inner TextBox's context menu, Cut/Copy/Paste/Select All only, with no
    /// spelling section. Read-only: none.
    bool handle_context_menu(std::optional<Point> position, ContextMenuHost& host) override;

    // ---- automation ----
    /// Editable: the text field's cursor over the text field (an I-beam, an arrow over its buttons).
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point p) const override
    {
        return editable_ && text_box_->bounds().contains(p) ? text_box_->cursor_at(p) : std::nullopt;
    }
    /// A ComboBox, with its text as the Value pattern (read-only unless editable) and
    /// ExpandCollapse for open and close.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::ComboBox;
    }
    /// Editable: the inner text box's caret.
    [[nodiscard]] std::optional<Rect> caret_bounds() const override
    {
        return editable_ ? text_box_->caret_bounds() : std::nullopt;
    }

private:
    // The inner TextBox's editing and clipboard keys (Element::key_override()).
    [[nodiscard]] bool takes_shortcut(const KeyEvent& e) const;
    // Up/Down step the selection, and when editable Left/Right reach the inner TextBox caret.
    // Claimed so Window's directional focus navigation leaves them alone
    // (Element::key_override).
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowUp || key == Key::ArrowDown ||
               (editable_ && (key == Key::ArrowLeft || key == Key::ArrowRight));
    }
    // Enter opens, commits or submits (Element::key_override()).
    [[nodiscard]] bool takes_enter_key() const
    {
        return true;
    }
    // This control alone: the internal TextBox is not live on its own.
    void collect_live(std::vector<Element*>& out) override
    {
        out.push_back(this);
    }
    friend class ComboBoxPopup; // needs the list state and choose()

    // The box the list hangs from: the control less its header and description, one row tall
    // when read-only.
    [[nodiscard]] Rect box_bounds() const;
    [[nodiscard]] Rect chevron_bounds() const;
    [[nodiscard]] bool box_pressed() const;
    [[nodiscard]] std::size_t find_text_index() const; // index of the item matching the TextBox exactly, or detail::NoIndex

    // Opens, updates or closes the list's popup to match expanded_ and the slide.
    void sync_popup();
    void set_expanded(bool expanded);
    void select(std::size_t index); // read-only: clamps, marks dirty, fires on_selection_changed_ on an actual change
    void choose(std::size_t index); // the user picked `index` from the list: shows it, closes, fires on_item_chosen_
    void sync_selection();          // editable: follows selected_ to the text, firing on_selection_changed_
    void commit_text();             // editable: Enter or focus loss
    void search(std::string_view typed); // read-only: jumps to the next item starting with the typed search
    void complete();                     // editable: completes the text to the first item it starts

    void key_read_only(const KeyEvent& e);
    void key_editable(const KeyEvent& e);
    void render_read_only(Renderer& renderer, const Theme& theme) const;
    void render_editable(Renderer& renderer, const RenderContext& context) const;

    std::unique_ptr<TextBoxControl> text_box_;
    std::vector<std::string>        items_;
    std::string                     placeholder_text_;
    std::string                     committed_text_; // editable: what Escape reverts to
    std::size_t                     selected_ = detail::NoIndex;
    bool                            editable_ = false;
    bool                            expanded_ = false;
    ControlHeader                   header_{8.0f}; // 8 DIPs between the header and the box
    ControlDescription              description_;

    // Input flags on the box (read-only) or the chevron (editable), mirroring Button's state machine.
    bool hovered_      = false;
    bool pointer_down_ = false;
    bool key_down_     = false; // Space held on a read-only box

    // An edit from typing waits in text_override() for completion before the app hears of it.
    bool typing_ = false;

    // hover (mouse) or highlight (keyboard) target while expanded; detail::NoIndex when neither
    // applies. Seeded to the current selection when the list opens.
    std::size_t active_item_ = detail::NoIndex;

    // The read-only box's typed search, and the time since its last letter.
    std::string search_;
    double      search_idle_ms_ = 0.0;

    // Set by measure_override(); read by box_bounds(), chevron_bounds(), render() and ComboBoxPopup.
    mutable float header_height_ = 0.0f;
    mutable float row_height_    = 0.0f;
    mutable float chevron_width_ = 0.0f;

    // The list's slide-open (src/ui/input/ListSlide.hpp): time since it opened, and the theme's motion
    // as measure() last saw it, for tick_override().
    double                     list_slide_ms_ = 1.0e9;
    mutable Theme::PopupMotion list_motion_{};

    // Built on the first open and kept. It holds no per-open state and reads everything live from
    // this control.
    std::unique_ptr<Element> popup_;
    // The open list as automation sees it, made on first use.
    [[nodiscard]] detail::DrawnListAutomation& list_automation() const;
    mutable std::unique_ptr<Element>           list_automation_; // a detail::DrawnListAutomation

    Callback<void(std::size_t)>                          on_item_chosen_;
    Callback<Decision(const ComboBoxTextSubmittedArgs&)> on_text_submitted_;
    Callback<void(const std::string&)>                   on_text_changed_;
    Callback<void(std::optional<std::size_t>)>           on_selection_changed_;

protected:
    void enabled_changed() override;
    /// Called by Window; mirrored into the inner TextBox. Losing focus closes the list, and an
    /// editable box commits its typed text.
    void focus_changed(bool focused) override;
    /// F4 or Alt+Down/Up toggles the list; Up/Down/Enter/Escape drive it while open. Read-only,
    /// Space toggles it and Up/Down step the selection while closed. Editable, Enter commits and
    /// Escape reverts while closed, and everything else edits the text.
    bool key_override(const KeyEvent& e) override;
    /// Typed text searches the items (read-only) or goes to the inner TextBox (editable).
    bool text_override(const TextInputEvent& text_event) override;

    /// Read-only: around the box; inside the classic field, round the selected text, while the
    /// list is closed.
    [[nodiscard]] Rect  focus_ring_rect(const Theme& theme) const override;
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override;
    /// Read-only only: an editable box shows focus with its accent border.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return !editable_;
    }

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.expand_collapse = this;
        out.value           = this;
    }
    // expanded().
    [[nodiscard]] bool expanded_state() const override
    {
        return expanded_;
    }
    // Opens or closes the list when enabled; false and nothing happens when disabled.
    bool set_expanded_state(bool expand) override
    {
        if (!is_enabled())
        {
            return false;
        }
        set_expanded(expand);
        return true;
    }
    // text().
    [[nodiscard]] std::string text_value() const override
    {
        return text();
    }
    // Writable only while editable and enabled.
    [[nodiscard]] bool text_value_read_only() const override
    {
        return !editable_ || !is_enabled();
    }
    // Sets the text through the inner TextBox's own Value pattern, so on_text_changed() fires
    // as for a user edit. False when read-only or disabled.
    bool set_text_value(std::string_view value) override
    {
        if (!editable_ || !is_enabled())
        {
            return false;
        }
        return detail::TreeAccess::automation_providers(*text_box_).value->set_text_value(value);
    }
};

/// A combo box: pick one of a list, or with `editable` type any value. Builds a ui::ComboBoxControl.
///
/// `items` is applied before `text` and `selected_index`, so a bound index always lands inside
/// the list. When both are bound, `text` follows the selection.
///
/// Example:
/// @code
/// ui::Property<std::optional<std::size_t>> size{1};
/// ui::Property<std::string>                color{"Blue"};
///
/// window.set_content(ui::VerticalStack{
///     .spacing = 8,
///     .content = {
///         ui::ComboBox{
///             .header         = "Size",
///             .items          = {"Small", "Medium", "Large"},
///             .selected_index = size,
///             .width          = 200.0f,
///         },
///         ui::ComboBox{
///             .header            = "Colors",
///             .placeholder_text  = "Pick a color",
///             .items             = {"Red", "Green", "Blue"},
///             .text              = color,
///             .editable          = true,
///             .width             = 200.0f,
///             .on_text_submitted = [](const ui::ComboBoxTextSubmittedArgs&) { return ui::Proceed; }, // free text
///         },
///     },
/// });
/// @endcode
struct ComboBox
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<ComboBox>` points at.
    using control_type = ComboBoxControl;

    // ---- properties ----
    /// The label above the control. Empty draws none.
    Binding<std::string> header{};
    /// Secondary text under the box, such as what to enter or why; empty draws none. Screen
    /// readers offer it as help text.
    Binding<std::string> description{};
    /// What is wrong with the value: non-empty shows it in the description's place in the critical
    /// color and draws the border in that color, and screen readers report the field invalid.
    /// The field shows exactly what it is given, so bind it to a check of the value and decide
    /// there when it shows (after the first submit, say).
    Binding<std::string> error{};
    /// The hint shown while nothing is selected or the text is empty ("Pick a color").
    Binding<std::string> placeholder_text{};
    /// The choices, in display order. Bound, a new list replaces them and closes the list, as
    /// ui::ComboBoxControl::set_items() does.
    Binding<std::vector<std::string>> items{};
    /// The selected item; unset (the default) selects nothing and shows `placeholder_text`. Two-way
    /// with ui::bind(): picking, stepping or (editable) typing an item writes its index, other text
    /// writes unset.
    TwoWayField<std::optional<std::size_t>> selected_index{};
    /// The text in the box: the selected item's, or when `editable` whatever was typed. Two-way
    /// with ui::bind().
    TwoWayField<std::string> text{};
    /// Whether the user can type a value of their own; false (the default) only picks items.
    Binding<bool> editable{false};

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
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// Editable: fires with the new text after each user edit (typing, Backspace, Delete).
    Callback<void(const std::string&)> on_text_changed{};
    /// Fires with the index of the item the user picked from the list, even if the box already
    /// showed it.
    Callback<void(std::size_t)> on_item_chosen{};
    /// Fires with the new index when the user changes the selection, unset when it was cleared. A
    /// change that comes from a binding, a write to `selected_index` or new `items`, doesn't run
    /// it.
    Callback<void(std::optional<std::size_t>)> on_selection_changed{};
    /// Editable: typed text matching no item, committed by Enter or focus loss. Return ui::Cancel to
    /// reject it (after putting the last good value back with set_text() through `.control`).
    Callback<Decision(const ComboBoxTextSubmittedArgs&)> on_text_submitted{};
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
    ControlOut<ComboBox> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::ComboBoxControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
