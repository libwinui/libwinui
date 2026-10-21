// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/ControlHeader.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// A headed group of mutually exclusive string options, as one element.
///
/// The control owns one `RadioButton` per item (`RadioGroup` coordinates loose buttons
/// instead), lays them out column-major in up to `max_columns()` equal-sized columns, and
/// selects by index. The group is a single tab stop; focus lands on the selected (or first
/// enabled) item. Up/Down move to the previous/next enabled item and select it, Left/Right
/// move between columns, Ctrl+arrow moves focus without selecting (Space then selects), and
/// an arrow with nowhere to go inside the group moves focus out of it. `ui::RadioButtons` is
/// the declarative form.
///
/// Example:
/// @code
/// ui::Property<std::string> message{"Nothing selected"};
///
/// window.set_content(ui::VerticalStack{
///     .spacing  = 12,
///     .content  = {
///         ui::RadioButtons{
///             .header               = "Options:",
///             .items                = {"Option 1", "Option 2", "Option 3"},
///             .max_columns          = 2,
///             .on_selection_changed = [&message](std::optional<std::size_t> index) {
///                 message = index ? std::format("You selected Option {}", *index + 1) : "";
///             },
///         },
///         ui::Text{.text = message},
///     },
/// });
///
/// // Imperatively:
/// auto colors = std::make_unique<ui::RadioButtonsControl>(
///     std::vector<std::string>{"Red", "Green", "Blue"}, /*selected_index=*/0);
/// colors->set_header("Color");
/// colors->item(2)->set_enabled(false); // disable a single option
/// colors->on_selection_changed([](std::optional<std::size_t> index) { /* unset when cleared */ });
/// @endcode
class RadioButtonsControl final : public Element
{
public:
    /// An empty group with no header and nothing selected.
    RadioButtonsControl() = default;
    /// One RadioButton per string, in order, with `selected_index` checked (unset for none;
    /// no callback fires).
    explicit RadioButtonsControl(std::vector<std::string> items, std::optional<std::size_t> selected_index = std::nullopt);

    // ---- content ----
    /// The header above the items; also the automation name when none is set.
    [[nodiscard]] const std::string& header() const
    {
        return header_.text();
    }
    /// The label above the options. Empty draws none and reserves no room for one.
    void set_header(std::string header);

    /// The number of options.
    [[nodiscard]] std::size_t item_count() const
    {
        return items_.size();
    }
    /// The button for item `index`, owned by this control; nullptr past the end.
    [[nodiscard]] RadioButtonControl* item(std::size_t index) const
    {
        return index < items_.size() ? items_[index].get() : nullptr;
    }
    /// Replaces every item and clears the selection (firing on_selection_changed if there was
    /// one). The new buttons take this control's enabled state.
    void set_items(std::vector<std::string> items);

    // ---- selection ----
    /// The checked item; unset when none is.
    [[nodiscard]] std::optional<std::size_t> selected_index() const
    {
        return detail::optional_index(selected_index_);
    }
    /// Checks that item and unchecks the one before. Unset, or anything outside
    /// [0, item_count()), clears the selection. No-op, and no callback, when it does not change.
    void set_selected_index(std::optional<std::size_t> index);
    /// The selected item's text, or an empty string.
    [[nodiscard]] std::string selected_item() const;

    /// Fired once per change with the new index (unset when cleared), whether
    /// a click, an arrow key, `set_items()` or `set_selected_index()` caused it. Replaces any
    /// previous handler.
    void on_selection_changed(Callback<void(std::optional<std::size_t>)> handler)
    {
        on_selection_changed_ = std::move(handler);
    }

    // ---- layout ----
    /// The most columns items are laid out in; 1 by default. Fewer are used when the
    /// offered width cannot hold them.
    [[nodiscard]] std::size_t max_columns() const
    {
        return max_columns_;
    }
    /// Clamped to >= 1.
    void set_max_columns(std::size_t columns);


    // ---- focus ----
    /// Whether the group has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// The item that has the keyboard's attention while the group is focused; unset otherwise.
    [[nodiscard]] std::optional<std::size_t> focused_index() const
    {
        if (!is_focused() || focus_index_ == NoSlot)
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(focus_index_);
    }
    /// A tab stop while enabled with at least one enabled item.
    [[nodiscard]] bool focusable() const override;

    // ---- the owned items ----
    /// The items, in order.
    void for_each_child(ChildVisitor visit) const override
    {
        for (const auto& item : items_)
        {
            visit(*item);
        }
    }
    /// Lists only the group, as one tab stop. The arrow keys move between the items.
    void collect_focusables(std::vector<Element*>& out) override
    {
        if (focusable())
        {
            out.push_back(this);
        }
    }

    // ---- automation ----
    /// Group; each item is its own SelectionItem.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Group;
    }
    /// The header, when no `Accessibility::name` is set.
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return header_.text();
    }

protected:
    /// The header plus a column-major grid of cells, each the size of the largest item, with 7 DIP
    /// column and 8 DIP row spacing.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    void               arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    /// Routes clicks to the owned buttons.
    bool pointer_override(const PointerEvent& e) override;

    // ---- render ----
    /// Draws the header and every item.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    // Only the arrows that have an item to move to: the rest leave the group.
    [[nodiscard]] bool takes_arrow_key(Key key) const;
    // ui::detail::NoIndex in the int form the index arithmetic below uses.
    static constexpr int NoSlot = -1;
    friend class RadioButtonControl; // RadioButtonControl::activate() reports a click or Space through select()

    void select(RadioButtonControl* button);
    void move_focus(int index, bool select_it);
    // The item an arrow key would move to from focus_index_, or NoSlot when it would leave.
    [[nodiscard]] int arrow_target(Key key) const;
    [[nodiscard]] int first_enabled() const;
    void              sync_item_focus();

    ControlHeader                                    header_{8.0f}; // 8 DIPs between the header and the items
    std::vector<std::unique_ptr<RadioButtonControl>> items_;
    std::size_t                                      selected_index_ = detail::NoIndex;
    std::size_t                                      max_columns_    = 1;
    int                                              focus_index_    = NoSlot;

    mutable Size              cell_{}; // the largest item, every cell's size
    mutable int               columns_ = 1;
    mutable std::vector<Size> measured_; // each item's own desired size, which is what it is arranged at

    Callback<void(std::optional<std::size_t>)> on_selection_changed_;

protected:
    void enabled_changed() override;
    void focus_changed(bool focused) override;
    /// Arrow keys move the focused item (and select it unless Ctrl is held); Space selects it.
    bool key_override(const KeyEvent& e) override;

protected:
    /// Around the focused item, as that item would ring itself.
    [[nodiscard]] Rect  focus_ring_rect(const Theme& theme) const override;
    [[nodiscard]] float focus_ring_corner_radius(const Theme& theme) const override;
};

/// A headed group of mutually exclusive options, one tab stop, selected by index. Builds a
/// ui::RadioButtonsControl.
///
/// The initial selection is in place before any handler is installed, so it raises nothing.
///
/// Example:
/// @code
/// ui::Property<std::optional<std::size_t>> background{0};
///
/// window.set_content(ui::RadioButtons{
///     .header               = "Background",
///     .items                = {"Green", "Yellow", "White"},
///     .selected_index       = background,
///     .max_columns          = 3,
/// });
/// @endcode
struct RadioButtons
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<RadioButtons>` points at.
    using control_type = RadioButtonsControl;

    // ---- properties ----
    /// The label above the group. Empty draws none.
    Binding<std::string> header{};
    /// One radio button per string, in order. Bound, a new list replaces the buttons and clears
    /// the selection.
    Binding<std::vector<std::string>> items{};
    /// Unset (the default) selects nothing.
    TwoWayField<std::optional<std::size_t>> selected_index{};
    /// The most columns (at least 1). Items fill a column top to bottom before starting the next.
    Binding<std::size_t> max_columns{1};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named Style; explicit fields below win over it.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Fixed width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Fixed height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Minimum width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Minimum height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Maximum width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Maximum height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment within the parent's slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment within the parent's slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{};    ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Binding<bool>                     enabled{true}; ///< False grays it out and takes no input.
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The newly selected index (unset when cleared), after a click or Space. A bound value doesn't
    /// run it.
    Callback<void(std::optional<std::size_t>)> on_selection_changed{};
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
    ControlOut<RadioButtons> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::RadioButtonsControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
