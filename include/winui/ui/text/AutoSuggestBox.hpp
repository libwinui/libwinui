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
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
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

class AutoSuggestBoxPopup; // defined in AutoSuggestBox.cpp

/// Why the text box's text changed. Lets an `on_text_changed` handler tell the user's typing,
/// which is worth filtering suggestions for, apart from the text this control writes itself.
enum class AutoSuggestBoxTextChangeReason
{
    UserInput,          ///< The user typed, deleted or pasted text.
    ProgrammaticChange, ///< Escape, or an arrow key past either end of the list, restored the typed text.
    Suggestion,         ///< A suggestion was put in the box, by an arrow key or a click.
};

/// What an AutoSuggestBox's `on_text_changed` gets: the text and what changed it.
struct AutoSuggestBoxTextChangedArgs
{
    const std::string&             text;   ///< The text now in the box.
    AutoSuggestBoxTextChangeReason reason; ///< UserInput is the one to update the suggestions on.
};

/// What `on_query_submitted` gets: the text, and the suggestion it came from, if any.
struct AutoSuggestBoxQuerySubmittedArgs
{
    const std::string&         query_text;        ///< The submitted text.
    std::optional<std::size_t> chosen_suggestion; ///< The suggestion's index; unset for typed text.
};

/// A text box with a floating list of suggestions the app supplies as the user types,
/// for search boxes and type-ahead pickers.
///
/// The control never filters. Handle on_text_changed(), and when the reason is
/// AutoSuggestBoxTextChangeReason::UserInput, compute the matches and pass them to
/// set_suggestions(). The popup opens whenever the box is focused and the list is non-empty.
///
/// Up and Down preview suggestions in the text. Stepping past either end restores what the user
/// typed. A row click, or Enter on a highlighted row, fires on_suggestion_chosen(). Enter or a row
/// click fires on_query_submitted(). Escape restores the typed text and closes the list. An
/// optional query button (set_query_icon()) submits the typed text as a query.
///
/// Example:
/// @code
/// static const std::vector<std::string> Cities{"Amsterdam", "Berlin", "Bern", "Zurich"};
///
/// auto box = std::make_unique<ui::AutoSuggestBoxControl>();
/// box->set_header("City");
/// box->set_placeholder_text("Search cities");
/// box->set_query_icon(ui::symbol_glyph(ui::Symbol::Find));
/// box->on_text_changed([b = box.get()](const ui::AutoSuggestBoxTextChangedArgs& args) {
///     if (args.reason != ui::AutoSuggestBoxTextChangeReason::UserInput)
///         return; // our own preview/revert writes; nothing to re-filter
///     std::vector<std::string> matches;
///     for (const std::string& city : Cities)
///         if (!args.text.empty() && city.starts_with(args.text))
///             matches.push_back(city);
///     b->set_suggestions(std::move(matches));
/// });
/// box->on_query_submitted([](const ui::AutoSuggestBoxQuerySubmittedArgs& args) {
///     // args.chosen_suggestion is unset for a free-typed query
/// });
/// window.set_content(std::move(box));
/// @endcode
class AutoSuggestBoxControl final : public Element, private ValueProvider
{
public:
    /// An empty box with no suggestions, header or query button.
    AutoSuggestBoxControl();
    /// A box starting with `text`, which is also what Escape reverts to until the user types.
    explicit AutoSuggestBoxControl(std::string text);

    // ---- text ----
    /// The text box's current text (UTF-8), including a previewed suggestion.
    [[nodiscard]] const std::string& text() const;
    /// Replaces the text and moves the caret to the end. Doesn't fire on_text_changed() or change
    /// the suggestion list. It also doesn't change the text Escape restores, which only the user's
    /// typing sets.
    void set_text(std::string text);

    // ---- suggestions (filtered by the app) ----
    /// The list last passed to set_suggestions(), shown as-is in the popup.
    [[nodiscard]] const std::vector<std::string>& suggestions() const
    {
        return suggestions_;
    }
    /// Replaces the shown suggestion list and clears the hover and keyboard highlight. Opens the
    /// popup when the control is focused and the new list is non-empty. Otherwise
    /// is_suggestion_list_open() stays as it was.
    void set_suggestions(std::vector<std::string> suggestions);

    /// Whether the suggestion popup is showing.
    [[nodiscard]] bool is_suggestion_list_open() const
    {
        return is_open_;
    }

    /// The query button's icon; empty for no button.
    [[nodiscard]] const IconSource& query_icon() const
    {
        return query_icon_;
    }
    /// Shows a query button with this icon (such as `Symbol::Find`); a click on it submits the
    /// typed text with no suggestion chosen. An empty source, the default, has no button.
    void set_query_icon(IconSource icon);
    /// The query button's rect in window DIPs, valid after arrange. Empty without a query icon.
    [[nodiscard]] Rect query_button_rect() const;

    /// The hint shown while the text is empty.
    [[nodiscard]] const std::string& placeholder_text() const
    {
        return text_box_->placeholder_text();
    }
    /// Sets the hint shown while the text is empty.
    void set_placeholder_text(std::string text)
    {
        text_box_->set_placeholder_text(std::move(text));
        mark_dirty(DirtyFlags::Visual);
    }

    /// The header text above the box; empty for none.
    [[nodiscard]] const std::string& header() const
    {
        return header_.text();
    }
    /// The label above the control. Empty (the default) draws none and takes no room.
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
    /// Sets the description. Empty draws none and takes no room.
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
    /// Sets the error. Empty shows the description again.
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


    /// Fired after every change to the text, with the new text and the reason
    /// (AutoSuggestBoxTextChangeReason). set_text() doesn't fire it. When the reason is UserInput,
    /// filter the suggestions here and call set_suggestions().
    void on_text_changed(Callback<void(const AutoSuggestBoxTextChangedArgs&)> handler)
    {
        on_text_changed_ = std::move(handler);
    }
    /// Fires with the suggestion's index when the user picks it with a row click, or with Enter
    /// while an arrow key has it highlighted. A highlight alone doesn't fire it. That shows in
    /// on_text_changed() with the reason AutoSuggestBoxTextChangeReason::Suggestion. Fires right
    /// before on_query_submitted(), with the suggestion already in the box.
    void on_suggestion_chosen(Callback<void(std::size_t)> handler)
    {
        on_suggestion_chosen_ = std::move(handler);
    }
    /// Fired on Enter, a query button click, or after on_suggestion_chosen() for a row click. It
    /// fires even with nothing highlighted, for a typed query with no chosen index. The popup
    /// closes afterward.
    void on_query_submitted(Callback<void(const AutoSuggestBoxQuerySubmittedArgs&)> handler)
    {
        on_query_submitted_ = std::move(handler);
    }

    // ---- focus ----
    /// Whether the box has keyboard focus.
    [[nodiscard]] bool focused() const
    {
        return is_focused();
    }
    /// Focusable while enabled.
    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }

    // ---- the internal TextBox ----
    /// Visits the internal TextBox.
    void for_each_child(ChildVisitor visit) const override
    {
        visit(*text_box_);
    }
    /// One tab stop: the internal TextBox is part of this control, not a stop of its own.
    void collect_focusables(std::vector<Element*>& out) override
    {
        if (focusable())
        {
            out.push_back(this);
        }
    }
    /// The internal TextBox is part of this control and not listed. While the suggestions show,
    /// lists them as a list of items. Selecting an item fills the box, and invoking it chooses it.
    void automation_children(std::vector<Element*>& out) const override;
    /// The highlighted suggestion while the list shows, so a screen reader reads it as the arrow
    /// keys move.
    [[nodiscard]] Element* automation_focus() const override;
    /// Shows Cut, Copy, Paste and Select All, with no spelling suggestions.
    bool handle_context_menu(std::optional<Point> position, ContextMenuHost& host) override;

    // ---- automation ----
    // A value only, delegated to text_box_. There is no expand or collapse to expose, because the
    // popup shows whenever the box is focused and has suggestions.
    /// The text field's cursor over the text field: an I-beam, or an arrow over its buttons.
    [[nodiscard]] std::optional<CursorShape> cursor_at(Point p) const override
    {
        return text_box_->bounds().contains(p) ? text_box_->cursor_at(p) : std::nullopt;
    }
    /// Always AutomationRole::Edit.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Edit;
    }
    /// The inner text box's caret.
    [[nodiscard]] std::optional<Rect> caret_bounds() const override
    {
        return text_box_->caret_bounds();
    }

protected:
    // ---- layout ----
    /// Sizes exactly as the internal TextBox does, plus the header. The popup floats and does
    /// not add to the measured size.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Arranges the internal TextBox below the header.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;

    // ---- input ----
    /// Routes clicks to the text box, its delete button or the query button.
    bool               pointer_override(const PointerEvent& e) override;
    [[nodiscard]] bool covers_children_at(Point p) const override;

    // ---- render ----
    /// Draws the header, the text box and the query button.
    void render_override(Renderer& renderer, const RenderContext& context) const override;


private:
    // The inner TextBox's editing and clipboard keys (Element::key_override()).
    [[nodiscard]] bool takes_shortcut(const KeyEvent& e) const;
    // Left/Right reach the inner TextBox caret, and Up/Down walk the suggestion list whenever
    // it is open. Claimed so Window's directional focus navigation leaves them alone.
    [[nodiscard]] bool takes_arrow_key(Key key) const
    {
        return key == Key::ArrowUp || key == Key::ArrowDown || key == Key::ArrowLeft || key == Key::ArrowRight;
    }
    // Enter commits the suggestion or the query (Element::key_override()).
    [[nodiscard]] bool takes_enter_key() const
    {
        return true;
    }
    // Opens or closes the suggestion list's popup to match is_open_.
    void sync_popup();
    // This control alone: the internal TextBox is not live on its own.
    void collect_live(std::vector<Element*>& out) override
    {
        out.push_back(this);
    }
    friend class AutoSuggestBoxPopup; // reads the suggestion state and calls back into the box
    // The suggestions as automation sees them, made on first use and placed over the list.
    [[nodiscard]] detail::DrawnListAutomation& list_automation() const;

    void preview_suggestion(std::size_t index); // writes suggestions_[index] into the text box; fires on_text_changed_
    void revert_to_typed_text();                // restores user_typed_text_; fires on_text_changed_(ProgrammaticChange)
    void submit_query();         // on_suggestion_chosen_ for active_index_, if any, then closes; on_query_submitted_
    void navigate(bool forward); // one arrow-key step through suggestions_, no wrap-around
    void close_suggestion_list();

    std::unique_ptr<TextBoxControl> text_box_;
    std::vector<std::string>        suggestions_;
    std::string                     user_typed_text_; // last text the user typed, which Escape restores
    bool                            is_open_ = false;
    ControlHeader                   header_{8.0f}; // 8 DIPs between the header and the field
    ControlDescription              description_;
    IconSource                      query_icon_;
    bool                            query_hovered_ = false;
    bool                            query_pressed_ = false;

    // Only the keyboard index previews text and is what Enter picks. The hover index only
    // highlights its row.
    std::size_t active_index_ = detail::NoIndex;
    std::size_t hover_index_  = detail::NoIndex;

    // Set by measure_override(), read by AutoSuggestBoxPopup.
    mutable float row_height_ = 0.0f;
    // How far the suggestion list is scrolled. The list has a maximum height and scrolls the rest,
    // by the wheel or to keep the keyboard's row in view.
    float               list_offset_ = 0.0f;
    [[nodiscard]] float list_viewport_height() const;
    [[nodiscard]] float list_visible_height() const;
    [[nodiscard]] float list_max_offset() const;
    void                scroll_list_to(std::size_t index);

    std::unique_ptr<Element>         popup_;
    mutable std::unique_ptr<Element> list_automation_; // a detail::DrawnListAutomation

    Callback<void(const AutoSuggestBoxTextChangedArgs&)>    on_text_changed_;
    Callback<void(std::size_t)>                             on_suggestion_chosen_;
    Callback<void(const AutoSuggestBoxQuerySubmittedArgs&)> on_query_submitted_;

protected:
    void enabled_changed() override;
    /// Focus is forwarded to the internal TextBox; losing it closes the popup.
    void focus_changed(bool focused) override;
    /// Up/Down walk the suggestions, Enter submits, Escape reverts; other keys edit the text.
    bool key_override(const KeyEvent& e) override;
    /// Typed text goes to the internal TextBox and fires on_text_changed() with UserInput.
    bool text_override(const TextInputEvent& text_event) override;

protected:
    /// None: the field shows focus with its accent border.
    [[nodiscard]] bool draws_focus_ring() const override
    {
        return false;
    }

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.value = this;
    }
    // text().
    [[nodiscard]] std::string text_value() const override
    {
        return text();
    }
    // Read-only while disabled.
    [[nodiscard]] bool text_value_read_only() const override
    {
        return !is_enabled();
    }
    // Sets the text through the internal TextBox; false while disabled.
    bool set_text_value(std::string_view value) override
    {
        if (!is_enabled())
        {
            return false;
        }
        return detail::TreeAccess::automation_providers(*text_box_).value->set_text_value(value);
    }
};

/// A search field with a live suggestion list. Builds a ui::AutoSuggestBoxControl.
///
/// Usually `.suggestions` is bound to a property, `.on_text_changed` recomputes it as the user
/// types, and `.on_query_submitted` runs the search.
///
/// Example:
/// @code
/// const std::vector<std::string>         titles{"Inbox", "Invoices", "Settings"};
/// ui::Property<std::vector<std::string>> matches;
///
/// window.set_content(ui::AutoSuggestBox{
///     .placeholder_text = "Search",
///     .suggestions      = matches,
///     .query_icon       = ui::symbol_glyph(ui::Symbol::Find),
///     .width            = 300.0f,
///     .on_text_changed  =
///         [&](const ui::AutoSuggestBoxTextChangedArgs& args)
///     {
///         if (args.reason == ui::AutoSuggestBoxTextChangeReason::UserInput)
///         {
///             std::vector<std::string> found;
///             for (const std::string& title : titles)
///             {
///                 if (title.starts_with(args.text))
///                 {
///                     found.push_back(title);
///                 }
///             }
///             matches = std::move(found);
///         }
///     },
///     .on_query_submitted = [](const ui::AutoSuggestBoxQuerySubmittedArgs& args)
///     { std::println("Search for {}", args.query_text); },
/// });
/// @endcode
struct AutoSuggestBox
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<AutoSuggestBox>` points at.
    using control_type = AutoSuggestBoxControl;

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
    /// The hint shown while the text is empty.
    Binding<std::string> placeholder_text{};
    /// The query text in the box.
    TwoWayField<std::string> text{};
    /// The suggestion list. Bind it with ui::bind() and assign the property from
    /// `.on_text_changed` to update it as the user types.
    Binding<std::vector<std::string>> suggestions{};
    /// The query button's icon: a Symbol, a glyph, a path or a picture; empty has no button.
    Binding<IconSource> query_icon{};

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
    /// The new text and why it changed. Recompute suggestions only for UserInput.
    Callback<void(const AutoSuggestBoxTextChangedArgs&)> on_text_changed{};
    /// The user picked a suggestion: a row click, or Enter on the highlighted row.
    Callback<void(std::size_t)> on_suggestion_chosen{};
    /// Enter, a suggestion click or the query button. The chosen index is unset for typed text.
    Callback<void(const AutoSuggestBoxQuerySubmittedArgs&)> on_query_submitted{};
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
    ControlOut<AutoSuggestBox> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::AutoSuggestBoxControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
