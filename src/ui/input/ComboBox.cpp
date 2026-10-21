// SPDX-License-Identifier: MIT
#include "winui/ui/input/ComboBox.hpp"

#include "../../render/DropdownChrome.hpp"
#include "../collections/DrawnListAutomation.hpp"
#include "../themes/ControlChrome.hpp"
#include "ListSlide.hpp"
#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
// The selection pill: a 3x16 accent bar with 1.5 corner radius, 1 px in from the row's left edge.
// It marks the selected item, in place of a check mark.

// Letters typed further apart than this start a new search.
constexpr double ComboSearchTimeoutMs = 1000.0;

// ASCII letters compare without case; other bytes compare as they are.
char fold_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool starts_with_folded(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() &&
           std::equal(prefix.begin(),
                      prefix.end(),
                      text.begin(),
                      [](char a, char b) { return fold_ascii(a) == fold_ascii(b); });
}

// Where the classic field's selected run sits: one pixel in from the field left of the button.
Rect classic_combo_field(const Theme& theme, const detail::ControlChrome::DropdownChrome& era, const Rect& box)
{
    const float edge  = theme.border_width;
    const float right = box.right() - edge - era.arrow_column;
    return Rect{box.left() + edge,
                box.top() + edge,
                std::max(0.0f, right - box.left() - edge),
                std::max(0.0f, box.height - 2.0f * edge)}
        .inset(Thickness{1.0f});
}

} // namespace

ComboBoxControl::ComboBoxControl() : ComboBoxControl(std::vector<std::string>{})
{
}

ComboBoxControl::ComboBoxControl(std::vector<std::string> items, std::optional<std::size_t> selected_index) :
    text_box_(std::make_unique<TextBoxControl>()),
    items_(std::move(items))
{
    if (!items_.empty() && selected_index)
    {
        selected_ = std::min(*selected_index, items_.size() - 1);
    }
    set_parent_of(*text_box_, this);
    // One background and border span text and chevron, so the internal TextBox only draws
    // text, selection and caret.
    text_box_->set_chromeless(true);
    // The minimum width is for the whole control. Zero the TextBox's own 64 px minimum so it
    // does not add to the control's minimum below.
    text_box_->set_min_width(0.0f);
    text_box_->set_uses_theme_min_height(false);
    // The editable box has no delete button.
    text_box_->set_delete_button_enabled(false);
    // The minimum width of the whole control, background and chevron included.
    set_min_width(64.0f);
    // Every user edit may match another item: the selection follows before the app hears of it.
    // Typed text waits for text_override(), which completes it first.
    text_box_->on_text_changed(
        [this](const std::string& text)
        {
            if (typing_)
            {
                return;
            }
            sync_selection();
            if (on_text_changed_)
            {
                on_text_changed_(text);
            }
        });
}

void ComboBoxControl::set_editable(bool editable)
{
    if (editable == editable_)
    {
        return;
    }
    set_expanded(false);
    if (editable)
    {
        // The field starts out showing the selection, which is also what Escape comes back to.
        committed_text_ = selected_ < items_.size() ? items_[selected_] : std::string{};
        text_box_->set_text(committed_text_);
        editable_ = true;
        set_focused_of(*text_box_, is_focused());
    }
    else
    {
        set_focused_of(*text_box_, false);
        editable_ = false;
        search_.clear();
    }
    hovered_      = false;
    pointer_down_ = false;
    key_down_     = false;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void ComboBoxControl::set_items(std::vector<std::string> items)
{
    const bool had_selection = selected_ != detail::NoIndex;
    items_                   = std::move(items);
    active_item_             = detail::NoIndex;
    set_expanded(false);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (editable_)
    {
        sync_selection();
        return;
    }
    selected_ = detail::NoIndex;
    // The selection is gone: a handler that reads the item, or a bound index, has to hear about it.
    if (had_selection && on_selection_changed_)
    {
        on_selection_changed_(std::nullopt);
    }
}

const std::string& ComboBoxControl::text() const
{
    if (editable_)
    {
        return text_box_->text();
    }
    static const std::string Empty;
    return selected_ < items_.size() ? items_[selected_] : Empty;
}

void ComboBoxControl::set_text(std::string text)
{
    if (!editable_)
    {
        const auto match = std::ranges::find(items_, text);
        set_selected_index(match == items_.end()
                               ? std::nullopt
                               : std::optional<std::size_t>{static_cast<std::size_t>(match - items_.begin())});
        return;
    }
    committed_text_ = text;
    text_box_->set_text(std::move(text));
    sync_selection();
}

void ComboBoxControl::set_selected_index(std::optional<std::size_t> index)
{
    if (editable_)
    {
        if (index ? *index >= items_.size() : selected_ == detail::NoIndex)
        {
            return; // past the list, or already showing no item
        }
        set_text(index ? items_[*index] : std::string{});
        return;
    }
    if (!index)
    {
        if (selected_ != detail::NoIndex)
        {
            selected_ = detail::NoIndex;
            mark_dirty(DirtyFlags::Visual);
            if (on_selection_changed_)
            {
                on_selection_changed_(std::nullopt);
            }
        }
        return;
    }
    if (!items_.empty())
    {
        select(std::min(*index, items_.size() - 1));
    }
}

void ComboBoxControl::set_placeholder_text(std::string text)
{
    if (text == placeholder_text_)
    {
        return;
    }
    placeholder_text_ = text;
    text_box_->set_placeholder_text(std::move(text));
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void ComboBoxControl::sync_selection()
{
    const std::size_t index = find_text_index();
    if (index == selected_)
    {
        return;
    }
    selected_ = index;
    mark_dirty(DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(detail::optional_index(selected_));
    }
}

std::size_t ComboBoxControl::find_text_index() const
{
    const auto match = std::ranges::find(items_, text_box_->text());
    return match == items_.end() ? detail::NoIndex : static_cast<std::size_t>(match - items_.begin());
}

void ComboBoxControl::select(std::size_t index)
{
    if (index >= items_.size() || index == selected_)
    {
        return;
    }
    selected_ = index;
    // The box's width already accounts for every item's width (measure() takes the max), so a
    // selection change never needs a re-measure: Visual is enough.
    mark_dirty(DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(detail::optional_index(selected_));
    }
}

void ComboBoxControl::choose(std::size_t index)
{
    if (index >= items_.size())
    {
        return;
    }
    if (editable_)
    {
        committed_text_ = items_[index];
        text_box_->set_text(items_[index]);
        set_expanded(false);
        sync_selection();
    }
    else
    {
        select(index);
        set_expanded(false);
    }
    if (on_item_chosen_)
    {
        on_item_chosen_(index);
    }
}

void ComboBoxControl::commit_text()
{
    const std::string text = text_box_->text();
    if (text == committed_text_)
    {
        return;
    }
    if (const std::size_t index = find_text_index(); index != detail::NoIndex)
    {
        committed_text_ = text;
        if (on_item_chosen_)
        {
            on_item_chosen_(index);
        }
        return;
    }
    // Nothing but spaces is not a submission, and leaves the committed text
    // alone for Escape to come back to.
    if (text.find_first_not_of(' ') == std::string::npos)
    {
        return;
    }
    ComboBoxTextSubmittedArgs args{.text = text};
    bool                      canceled = false;
    if (on_text_submitted_)
    {
        canceled = detail::canceled(on_text_submitted_(args));
    }
    if (!canceled)
    {
        committed_text_ = text_box_->text();
    }
}

void ComboBoxControl::search(std::string_view typed)
{
    // Space opens the list (key_override()) unless it continues a search; control characters
    // never search.
    const bool fresh = search_.empty() || search_idle_ms_ > ComboSearchTimeoutMs;
    if (items_.empty() || typed.empty() || static_cast<unsigned char>(typed.front()) < 0x20 ||
        static_cast<unsigned char>(typed.front()) == 0x7F || (typed == " " && fresh))
    {
        return;
    }
    if (fresh)
    {
        search_.clear();
    }
    search_ += typed;
    search_idle_ms_ = 0.0;
    request_tick(); // tick_override() times the search out

    const std::size_t count     = items_.size();
    const std::size_t base      = (expanded_ && active_item_ != detail::NoIndex) ? active_item_ : selected_;
    const auto        find_from = [&](std::size_t start, std::string_view prefix)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t index = (start + i) % count;
            std::string_view  item  = items_[index];
            item.remove_prefix(std::min(item.find_first_not_of(' '), item.size()));
            if (starts_with_folded(item, prefix))
            {
                return index;
            }
        }
        return detail::NoIndex;
    };
    // A new search starts after the current item, so the same letter again moves on; a longer
    // one stays on the current item while it still matches.
    const std::size_t after = base == detail::NoIndex ? 0 : (base + 1) % count;
    std::size_t       found = find_from(fresh ? after : (base == detail::NoIndex ? 0 : base), search_);
    // The same letter typed again and again steps through the items starting with it.
    if (found == detail::NoIndex && search_.size() > 1 &&
        std::ranges::all_of(search_, [this](char c) { return fold_ascii(c) == fold_ascii(search_.front()); }))
    {
        found = find_from(after, std::string_view{search_}.substr(0, 1));
    }
    if (found == detail::NoIndex)
    {
        return;
    }
    if (expanded_)
    {
        active_item_ = found;
        mark_dirty(DirtyFlags::Visual);
    }
    else
    {
        select(found);
    }
}

void ComboBoxControl::complete()
{
    const std::string& typed = text_box_->text();
    if (typed.empty() || text_box_->caret() != typed.size() || text_box_->has_selection())
    {
        return; // only typing at the end of the text completes it
    }
    for (const std::string& item : items_)
    {
        if (item.size() >= typed.size() && item != typed && starts_with_folded(item, typed))
        {
            // The item's own spelling, with the part the user did not type selected, so the next
            // letter replaces it.
            const std::size_t typed_size = typed.size();
            text_box_->set_text(item);
            text_box_->selection_anchor_ = typed_size;
            text_box_->caret_            = item.size();
            return;
        }
    }
}

void ComboBoxControl::enabled_changed()
{
    text_box_->set_enabled(is_enabled());
    if (!is_enabled())
    {
        hovered_      = false;
        pointer_down_ = false;
        key_down_     = false;
        set_expanded(false);
    }
    mark_dirty(DirtyFlags::Visual);
}

void ComboBoxControl::focus_changed(bool focused)
{
    if (editable_)
    {
        set_focused_of(*text_box_, is_focused());
    }
    if (!is_focused())
    {
        key_down_ = false;
        search_.clear();
        // Losing focus (e.g. Tab) closes an open list too, so tabbing away leaves no stale popup.
        set_expanded(false);
    }
    if (!focused && editable_)
    {
        // Losing focus commits. Tab lands here too, since Window consumes it before any control
        // sees it.
        commit_text();
    }
    mark_dirty(DirtyFlags::Visual);
}

void ComboBoxControl::set_expanded(bool expanded)
{
    if (expanded == expanded_ || (expanded && items_.empty()))
    {
        return;
    }
    expanded_    = expanded;
    active_item_ = expanded_ ? selected_ : detail::NoIndex;
    if (expanded_)
    {
        list_slide_ms_ = 0.0; // Theme::popup_motion.list: the slide-open starts over
    }
    // Visual only: the popup floats, so the container around the ComboBox never reflows.
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

Size ComboBoxControl::measure_override(Size available, const MeasureContext& context) const
{
    list_motion_           = context.theme.popup_motion; // for tick_override(), which has no Theme
    const Size header      = header_.measure(available.width, context);
    available              = ControlHeader::below(available, header);
    const Size description = description_.measure(available.width, context);
    available              = ControlHeader::below(available, description);

    const auto& era            = detail::controls(context.theme).dropdown_chrome;
    float       max_item_width = 0.0f;
    float       line_height    = context.measurer.measure("", context.theme.text_style).height;
    for (const auto& item : items_)
    {
        const Size s   = context.measurer.measure(item, context.theme.text_style);
        max_item_width = std::max(max_item_width, s.width);
        // An editable box's row is the TextBox's; a read-only one grows with a tall item.
        if (!editable_)
        {
            line_height = std::max(line_height, s.height);
        }
    }

    // The box's padding and minimum height, when the theme's density sets them.
    header_height_ = std::max(line_height + context.theme.sizing.combo_box_padding.value_or(context.theme.padding).vertical(),
                              context.theme.sizing.combo_box_min_height.value_or(0.0f));
    // Consumed by ComboBoxPopup, not by this measure.
    row_height_ = line_height + (era ? era->list_item_padding : context.theme.padding).vertical();
    // The chevron gutter is a square the height of the box, or, where the era draws a triangle,
    // a column a scroll bar wide (inside the read-only button's own 2px edge).
    chevron_width_ = era ? era->arrow_column + (editable_ ? 0.0f : 2.0f) : header_height_;

    Size desired{0.0f, header_height_};
    if (editable_)
    {
        const Size text_available{std::max(0.0f, available.width - chevron_width_), header_height_};
        const Size text_desired = text_box_->measure(text_available, context);
        desired.width = std::max(text_desired.width, max_item_width + context.theme.padding.horizontal()) + chevron_width_;
    }
    else
    {
        if (!placeholder_text_.empty())
        {
            max_item_width = std::max(max_item_width,
                                      context.measurer.measure(placeholder_text_, context.theme.text_style).width);
        }
        desired.width = max_item_width + context.theme.padding.horizontal() + chevron_width_;
    }
    // The list floats as a popup, so a container never reflows when it opens or closes.
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return ControlHeader::above(ControlHeader::above(desired, header), description);
}

void ComboBoxControl::arrange_override(const Rect&, const ArrangeContext&)
{
    header_.arrange(*this);
    description_.arrange(*this);
    if (editable_)
    {
        const Rect& box = bounds(); // the control itself, below its header
        text_box_->arrange(Rect{box.left(), box.top(), std::max(0.0f, box.width - chevron_width_), box.height});
    }
    if (expanded_)
    {
        sync_popup(); // the list hangs from the box, which may have moved
    }
}

Rect ComboBoxControl::box_bounds() const
{
    return editable_ ? bounds() : Rect{bounds().left(), bounds().top(), bounds().width, header_height_};
}

Rect ComboBoxControl::chevron_bounds() const
{
    const Rect box = box_bounds();
    return Rect{box.right() - chevron_width_, box.top(), chevron_width_, box.height};
}

bool ComboBoxControl::box_pressed() const
{
    return is_enabled() && ((pointer_down_ && hovered_) || key_down_);
}

ButtonState ComboBoxControl::visual_state() const
{
    if (!is_enabled())
    {
        return ButtonState::Disabled;
    }
    if (box_pressed())
    {
        return ButtonState::Pressed;
    }
    // An editable box's background border shares one PointerOver state across text and chevron,
    // so hovering the TextBox counts too.
    if (hovered_ || (editable_ && text_box_->hovered()))
    {
        return ButtonState::Hover;
    }
    return ButtonState::Normal;
}

bool ComboBoxControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }

    // The whole box toggles a read-only list, the chevron an editable one; item hover and click
    // reach ComboBoxPopup through the overlay layer.
    const Rect target = editable_ ? chevron_bounds() : box_bounds();

    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
            hovered_ = target.contains(e.position);
            break;

        case PointerAction::Down:
            if (e.button == PointerButton::Left && target.contains(e.position))
            {
                pointer_down_ = true;
                hovered_      = true;
                handled       = true;
            }
            break;

        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                const bool was_down = pointer_down_;
                handled             = was_down;
                pointer_down_       = false;
                hovered_            = target.contains(e.position);
                if (was_down && hovered_)
                {
                    set_expanded(!expanded_);
                }
            }
            break;

        case PointerAction::Leave:
            hovered_      = false;
            pointer_down_ = false;
            break;

        case PointerAction::Wheel:
            break;
    }
    return handled;
}

bool ComboBoxControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), takes_shortcut(e));
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled())
    {
        return handled;
    }

    // F4 toggles the list, and so do Alt+Down and Alt+Up (open when closed, closed when open),
    // without moving the selection.
    if (e.key == Key::F4 || (e.modifiers.alt && (e.key == Key::ArrowDown || e.key == Key::ArrowUp)))
    {
        if (e.action == KeyAction::Down)
        {
            set_expanded(!expanded_);
        }
        return handled;
    }

    if (editable_)
    {
        key_editable(e);
    }
    else
    {
        key_read_only(e);
    }
    return handled;
}

void ComboBoxControl::key_read_only(const KeyEvent& e)
{
    if (items_.empty())
    {
        return;
    }
    switch (e.key)
    {
        // Space only: Enter is reserved for a dialog's default button, as for CheckBox. Space in
        // the middle of a typed search is part of it (text_override()).
        case Key::Space:
            if (!search_.empty() && search_idle_ms_ <= ComboSearchTimeoutMs)
            {
                break;
            }
            if (e.action == KeyAction::Down)
            {
                key_down_ = true;
            }
            else if (key_down_)
            {
                key_down_ = false;
                set_expanded(!expanded_);
            }
            break;

        case Key::Enter:
            if (e.action == KeyAction::Down && expanded_)
            {
                if (active_item_ != detail::NoIndex)
                {
                    choose(active_item_);
                }
                else
                {
                    set_expanded(false);
                }
            }
            break;

        case Key::Escape:
            if (e.action == KeyAction::Down)
            {
                set_expanded(false);
            }
            break;

        case Key::ArrowDown:
        case Key::ArrowUp:
            if (e.action == KeyAction::Down)
            {
                search_.clear();
                const bool step_up = (e.key == Key::ArrowUp);
                if (expanded_)
                {
                    const std::size_t base = (active_item_ == detail::NoIndex) ? selected_ : active_item_;
                    // With nothing selected either arrow lands on the first item.
                    active_item_ = base == detail::NoIndex ? 0
                                   : step_up               ? (base == 0 ? 0 : base - 1)
                                                           : std::min(base + 1, items_.size() - 1);
                    mark_dirty(DirtyFlags::Visual);
                }
                else
                {
                    select(selected_ == detail::NoIndex ? 0
                           : step_up                    ? (selected_ == 0 ? 0 : selected_ - 1)
                                                        : std::min(selected_ + 1, items_.size() - 1));
                }
            }
            break;

        default:
            break;
    }
}

void ComboBoxControl::key_editable(const KeyEvent& e)
{
    if (expanded_)
    {
        switch (e.key)
        {
            case Key::Escape:
                if (e.action == KeyAction::Down)
                {
                    set_expanded(false);
                }
                return;

            case Key::Enter:
                if (e.action == KeyAction::Down)
                {
                    if (active_item_ != detail::NoIndex)
                    {
                        choose(active_item_);
                    }
                    else
                    {
                        set_expanded(false);
                        commit_text();
                    }
                }
                return;

            case Key::ArrowDown:
            case Key::ArrowUp:
                if (e.action == KeyAction::Down && !items_.empty())
                {
                    // With no current match, the first arrow press highlights item 0 in either
                    // direction, like an ordinary list box.
                    if (active_item_ == detail::NoIndex)
                    {
                        active_item_ = 0;
                    }
                    else if (e.key == Key::ArrowUp)
                    {
                        active_item_ = active_item_ == 0 ? 0 : active_item_ - 1;
                    }
                    else
                    {
                        active_item_ = std::min(active_item_ + 1, items_.size() - 1);
                    }
                    mark_dirty(DirtyFlags::Visual);
                }
                return;

            default:
                break; // typing keeps editing the text box even while the list is open
        }
    }
    else if (e.key == Key::Enter || e.key == Key::Escape)
    {
        // Closed: Enter commits the typed text and Escape takes it back to the committed one.
        // Neither reaches the TextBox, whose only use for them is its own on_submit.
        if (e.action == KeyAction::Down)
        {
            if (e.key == Key::Enter)
            {
                commit_text();
            }
            else if (text_box_->text() != committed_text_)
            {
                text_box_->set_text(committed_text_);
                sync_selection();
            }
        }
        return;
    }

    text_box_->handle_key(e);
}

bool ComboBoxControl::takes_shortcut(const KeyEvent& e) const
{
    return editable_ && text_box_->takes_shortcut(e);
}

bool ComboBoxControl::text_override(const TextInputEvent& text_event)
{
    if (!is_enabled() || !is_focused())
    {
        return true;
    }
    if (!editable_)
    {
        search(text_event.text);
        return true;
    }
    // Typing keeps editing the text box, even while the list is open. The app hears of the edit
    // once completion has filled it in.
    const std::string before = text_box_->text();
    typing_                  = true;
    text_box_->handle_text(text_event.text);
    typing_ = false;
    if (text_box_->text() == before)
    {
        return true;
    }
    complete();
    sync_selection();
    if (expanded_ && selected_ != detail::NoIndex)
    {
        active_item_ = selected_;
    }
    if (on_text_changed_)
    {
        on_text_changed_(text_box_->text());
    }
    return true;
}

void ComboBoxControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    header_.render(renderer, theme, is_enabled());
    description_.render(renderer, theme, is_enabled());
    if (editable_)
    {
        render_editable(renderer, context);
    }
    else
    {
        render_read_only(renderer, theme);
    }
}

void ComboBoxControl::render_read_only(Renderer& renderer, const Theme& theme) const
{
    const detail::ControlChrome::DropdownChrome* era           = detail::controls(theme).dropdown_chrome
                                                                     ? &*detail::controls(theme).dropdown_chrome
                                                                     : nullptr;
    const bool                                   has_selection = selected_ < items_.size();
    if (era && era->field_with_button)
    {
        draw_classic_combo_field(renderer,
                                 theme,
                                 *era,
                                 ClassicComboState{box_bounds(),
                                                   is_enabled(),
                                                   expanded_,
                                                   is_focused(),
                                                   focus_visible(),
                                                   has_selection ? std::string_view{items_[selected_]}
                                                                 : std::string_view{placeholder_text_},
                                                   !has_selection});
        return;
    }
    // Aero's drop-down list is one push button, and it stays pressed while its list is open
    // (`ControlChrome::DropdownChrome`).
    const ButtonVisual& box_visual = (era && expanded_ && is_enabled()) ? theme.button_pressed
                                                                        : theme.for_state(visual_state());
    const Rect          box        = box_bounds();
    const float         text_left  = era ? era->text_inset : theme.padding.left;

    if (era)
    {
        draw_control_chrome(renderer, box, theme.resolved_button_corner_radius(), box_visual, theme.border_width);
    }
    else
    {
        // Fluent's box has no stroke of its own, except where a visual asks for one
        // (`ButtonVisual::draw_border`): high contrast and Windows 8's dark theme need the outline.
        draw_control_chrome(renderer,
                            box,
                            theme.corner_radius,
                            box_visual,
                            theme.border_width,
                            /*draw_flat_border_fallback=*/false,
                            /*use_border_gradient=*/false);
    }
    if (description_.has_error() && is_enabled())
    {
        const float radius = era ? theme.resolved_button_corner_radius() : theme.corner_radius;
        stroke_rounded_rect_inside(renderer, box, radius, theme.colors().critical, theme.border_width);
    }

    const Rect label_rect{box.left() + text_left, box.top(), std::max(0.0f, box.width - text_left - chevron_width_), box.height};
    TextStyle label_style  = theme.text_style;
    label_style.horizontal = TextAlign::Leading;
    if (has_selection)
    {
        renderer.draw_text(items_[selected_], label_rect, box_visual.text, label_style);
    }
    else if (!placeholder_text_.empty())
    {
        // The placeholder takes the secondary text color: 60% of the box's text color, as
        // TextBox's placeholder uses, works for every pack.
        const Color hint = is_enabled() ? box_visual.text.with_alpha(0.6f) : box_visual.text;
        renderer.draw_text(placeholder_text_, label_rect, hint, label_style);
    }

    const Rect chevron_rect = chevron_bounds();
    if (era)
    {
        // The arrow column ends 2px inside the button's right edge (`ButtonChrome` shrinks its
        // child by 2 per side).
        draw_dropdown_arrow(renderer, *era, chevron_rect.inset(Edges{.right = 2.0f}), is_enabled());
    }
    else
    {
        draw_icon(renderer,
                  expanded_ ? glyphs::ChevronUp : glyphs::ChevronDown,
                  chevron_rect,
                  box_visual.text,
                  0.0f,
                  theme.icon_font_family);
    }
}

void ComboBoxControl::render_editable(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (detail::controls(theme).dropdown_chrome)
    {
        // Aero: the field turns focus-blue while focused or open, with a square drop button flush
        // in its right edge that lights like a push button on hover and press.
        const detail::ControlChrome::DropdownChrome& era    = *detail::controls(theme).dropdown_chrome;
        const float                                  radius = detail::resolved_field_corner_radius(theme);
        Color                                        fill   = theme.colors().field;
        if (!is_enabled() && detail::controls(theme).field_fills)
        {
            fill = detail::controls(theme).field_fills->disabled;
        }
        renderer.fill_rounded_rect(bounds(), radius, fill);
        const bool hovered = hovered_ || text_box_->hovered();
        if (!is_enabled() || !detail::controls(theme).field_border_states)
        {
            stroke_rounded_rect_inside(renderer,
                                       bounds(),
                                       radius,
                                       is_enabled() ? theme.surfaces.field_border : theme.button_disabled.border,
                                       theme.border_width);
        }
        else
        {
            const detail::ControlChrome::FieldBorderStates& states = *detail::controls(theme).field_border_states;
            const LinearGradientBrush&                      brush  = (is_focused() || expanded_) ? states.focused
                                                                     : hovered                   ? states.hover
                                                                                                 : states.rest;
            stroke_rounded_rect_gradient_inside(renderer, bounds(), radius, brush, states.extent_px, theme.border_width);
        }
        if (description_.has_error() && is_enabled())
        {
            stroke_rounded_rect_inside(renderer, bounds(), radius, theme.colors().critical, theme.border_width);
        }

        text_box_->render(renderer, context);

        if (era.field_with_button)
        {
            // The classic COMBOBOX: a raised push button inside the field's edge in every state,
            // sunk while pressed or open, its arrow moving with the press like a button label.
            const float       edge = theme.border_width;
            const Rect        button{bounds().right() - edge - era.arrow_column,
                                     bounds().top() + edge,
                                     era.arrow_column,
                                     std::max(0.0f, bounds().height - 2.0f * edge)};
            const ButtonState state = (is_enabled() && expanded_) ? ButtonState::Pressed : visual_state();
            draw_control_chrome(renderer, button, 0.0f, theme.for_state(state), theme.border_width);
            const Point shift = state == ButtonState::Pressed ? detail::controls(theme).pressed_content_offset
                                                              : Point{0.0f, 0.0f};
            const Rect  arrow{button.x + shift.x, button.y + shift.y, button.width, button.height};
            draw_dropdown_arrow(renderer, era, arrow, is_enabled());
            return;
        }

        const Rect button = chevron_bounds();
        if (is_enabled())
        {
            const ButtonState state = expanded_ ? ButtonState::Pressed : visual_state();
            ButtonVisual      face  = theme.for_state(state);
            if (state == ButtonState::Normal)
            {
                // No background and no border: the ring alone.
                face.background_gradient.reset();
                face.background  = colors::Transparent;
                face.draw_border = false;
            }
            draw_control_chrome(renderer, button, 0.0f, face, theme.border_width);
        }
        draw_dropdown_arrow(renderer, era, button, is_enabled());
        return;
    }

    // One background and border across text and chevron gutter, a single Background
    // Border. Field colors rather than a ButtonVisual, since the box reads as an edit field.
    Color border = theme.surfaces.field_border;
    if (!is_enabled())
    {
        border = theme.button_disabled.border;
    }
    else if (hovered_ || text_box_->hovered())
    {
        border = theme.button_hover.border;
    }
    if (is_focused() && is_enabled())
    {
        // The whole border turns accent on focus. There is
        // no separate underline like TextBox's.
        border = theme.colors().focus;
    }
    if (description_.has_error() && is_enabled())
    {
        border = theme.colors().critical; // the `error` is set
    }
    // An open ComboBox's field is an accent tint in Windows 10, focused or not, so an open
    // ComboBox washes its field regardless of focus (`ControlChrome::combo_open_fill`).
    const Color field_fill = (expanded_ && detail::controls(theme).combo_open_fill)
                                 ? *detail::controls(theme).combo_open_fill
                                 : theme.colors().field;
    renderer.fill_rounded_rect(bounds(), theme.corner_radius, field_fill);
    stroke_rounded_rect_inside(renderer, bounds(), theme.corner_radius, border, theme.border_width);

    text_box_->render(renderer, context);

    const ButtonVisual& chevron_visual = theme.for_state(visual_state());
    draw_icon(renderer,
              expanded_ ? glyphs::ChevronUp : glyphs::ChevronDown,
              chevron_bounds(),
              chevron_visual.text,
              0.0f,
              theme.icon_font_family);
}

Rect ComboBoxControl::focus_ring_rect(const Theme& theme) const
{
    const auto& era = detail::controls(theme).dropdown_chrome;
    if (era && era->field_with_button)
    {
        return expanded_ ? Rect{} : classic_combo_field(theme, *era, box_bounds());
    }
    return box_bounds();
}

float ComboBoxControl::focus_ring_corner_radius(const Theme& theme) const
{
    const auto& era = detail::controls(theme).dropdown_chrome;
    return era && era->field_with_button ? 0.0f : theme.corner_radius;
}

void ComboBoxControl::tick_override(Duration delta)
{
    if (expanded_ && detail::tick_list_slide(list_motion_, list_slide_ms_, delta.count()))
    {
        mark_dirty(DirtyFlags::Visual); // keep the frames coming while the list slides open
    }
    if (expanded_)
    {
        sync_popup(); // the shadow comes once the slide has finished
    }
    if (!search_.empty())
    {
        search_idle_ms_ += delta.count();
        if (search_idle_ms_ > ComboSearchTimeoutMs)
        {
            search_.clear();
        }
        else
        {
            request_tick_in(Duration{ComboSearchTimeoutMs - search_idle_ms_});
        }
    }
}

bool ComboBoxControl::handle_context_menu(std::optional<Point> position, ContextMenuHost& host)
{
    return editable_ && text_box_->handle_context_menu(position, host);
}

// The floating item list. Reads items, selection and hover live from `owner_`; `bounds()` is where
// Window::sync_popups() last arranged it, so no coordinate translation is needed.
class ComboBoxPopup final : public Element
{
public:
    explicit ComboBoxPopup(ComboBoxControl* owner) : owner_(owner)
    {
    }

    void render_list(Renderer& renderer, const Theme& theme) const
    {
        // The popup floats over the window with nothing opaque behind it. Under Mica the root
        // fill is transparent, so a backing goes under the translucent row fills.
        if (detail::controls(theme).dropdown_chrome)
        {
            const std::size_t lit = owner_->active_item_ != detail::NoIndex ? owner_->active_item_ : owner_->selected_;
            draw_dropdown_list(renderer,
                               theme,
                               *detail::controls(theme).dropdown_chrome,
                               bounds(),
                               owner_->items_,
                               owner_->row_height_,
                               lit);
            return;
        }
        fill_overlay_surface(renderer, theme, bounds(), theme.overlay_corner_radius, theme.colors().overlay);

        for (std::size_t i = 0; i < owner_->items_.size(); ++i)
        {
            const Rect row = row_bounds(i);
            const bool hot = (i == owner_->active_item_);

            if (hot)
            {
                renderer.fill_rounded_rect(row, 0.0f, theme.surfaces.menu_highlight);
            }

            const Color text_color = hot ? theme.surfaces.menu_highlight_text : theme.surfaces.menu_text;
            if (i == owner_->selected_)
            {
                // The selected item's background fills the whole row in the Windows 10 era, a rung
                // fainter than a ListView row's. The pill is the Windows 11 cue.
                if (detail::controls(theme).row_selection_fills)
                {
                    // The selected row's fill, at rest and on hover: this ladder
                    // deepens on hover (0.4 -> 0.6 Light) where a ListView row's fades.
                    renderer.fill_rounded_rect(row,
                                               0.0f,
                                               hot ? detail::controls(theme).row_selection_fills->popup_selected_hover
                                                   : detail::controls(theme).row_selection_fills->popup_selected);
                }
                else if (detail::controls(theme).accent_selection_indicator)
                {
                    detail::draw_selection_pill(renderer, row, theme.colors().accent);
                }
            }

            TextStyle item_style  = theme.text_style;
            item_style.horizontal = TextAlign::Leading;
            const Rect text_rect{row.left() + theme.padding.left,
                                 row.top(),
                                 std::max(0.0f, row.width - theme.padding.horizontal()),
                                 row.height};
            renderer.draw_text(owner_->items_[i], text_rect, text_color, item_style);
        }
        const ButtonVisual& owner_visual = theme.for_state(owner_->visual_state());
        // `menu_border` when the theme names one: Metro's popups have a black stroke while its
        // `field_border` is white.
        stroke_rounded_rect_inside(renderer,
                                   bounds(),
                                   theme.overlay_corner_radius,
                                   theme.surfaces.menu_border.value_or(owner_visual.border),
                                   theme.border_width);
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
    {
        // As wide as the box, however much room the window offers; one row per item.
        return Size{detail::list_width(owner_->box_bounds().width, available.width),
                    owner_->row_height_ * static_cast<float>(owner_->items_.size())};
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        // The list hangs below the box unless there was no room, and slides out of that edge
        // where the era animates it (Theme::popup_motion.list).
        const bool above = bounds().top() < owner_->box_bounds().top();
        const bool slid  = detail::push_list_slide(renderer, theme, owner_->list_slide_ms_, bounds(), above);
        render_list(renderer, theme);
        if (slid)
        {
            detail::pop_list_slide(renderer);
        }
    }

    bool pointer_override(const PointerEvent& e) override
    {
        switch (e.action)
        {
            case PointerAction::Move:
                owner_->active_item_ = hit_test(e.position);
                break;

            case PointerAction::Up:
                if (e.button == PointerButton::Left)
                {
                    const std::size_t idx = hit_test(e.position);
                    if (idx != detail::NoIndex)
                    {
                        owner_->choose(idx);
                    }
                }
                break;

            case PointerAction::Leave:
                owner_->active_item_ = detail::NoIndex;
                break;

            default:
                break;
        }
        return false;
    }


private:
    [[nodiscard]] Rect row_bounds(std::size_t index) const
    {
        return Rect{bounds().left(),
                    bounds().top() + owner_->row_height_ * static_cast<float>(index),
                    bounds().width,
                    owner_->row_height_};
    }
    [[nodiscard]] std::size_t hit_test(Point p) const
    {
        for (std::size_t i = 0; i < owner_->items_.size(); ++i)
        {
            if (row_bounds(i).contains(p))
            {
                return i;
            }
        }
        return detail::NoIndex;
    }

    ComboBoxControl* owner_;
};

// ---- automation ----
// The items are drawn by the popup and have no Elements: a List node stands in for them while the
// list is open.

detail::DrawnListAutomation& ComboBoxControl::list_automation() const
{
    if (!list_automation_)
    {
        auto* self       = const_cast<ComboBoxControl*>(this); // NOLINT(cppcoreguidelines-pro-type-const-cast): a cache
        list_automation_ = std::make_unique<
            detail::DrawnListAutomation>(self,
                                         detail::DrawnListAutomation::Source{
                                             .count = [self] { return self->items_.size(); },
                                             .name  = [self](std::size_t i) { return self->items_[i]; },
                                             .bounds = [self] { return self->popup_ ? self->popup_->bounds() : Rect{}; },
                                             .row =
                                                 [self](std::size_t i)
                                             {
                                                 const Rect list = self->popup_ ? self->popup_->bounds() : Rect{};
                                                 return Rect{list.left(),
                                                             list.top() + self->row_height_ * static_cast<float>(i),
                                                             list.width,
                                                             self->row_height_};
                                             },
                                             .selected = [self](std::size_t i) { return self->selected_ == i; },
                                             // Picks the item, as a click on it does.
                                             .select =
                                                 [self](std::size_t i)
                                             {
                                                 if (!self->is_enabled() || !self->expanded_)
                                                 {
                                                     return false;
                                                 }
                                                 self->choose(i);
                                                 return true;
                                             },
                                             .invoke = {},
                                         });
    }
    auto& list = static_cast<detail::DrawnListAutomation&>(*list_automation_);
    list.place();
    return list;
}

void ComboBoxControl::automation_children(std::vector<Element*>& out) const
{
    if (expanded_ && !items_.empty())
    {
        out.push_back(&list_automation());
    }
}

Element* ComboBoxControl::automation_focus() const
{
    if (expanded_ && active_item_ < items_.size())
    {
        return list_automation().item(active_item_);
    }
    return nullptr;
}

void ComboBoxControl::sync_popup()
{
    if (!expanded_)
    {
        close_popup();
        return;
    }
    if (!popup_)
    {
        popup_ = std::make_unique<ComboBoxPopup>(this);
    }
    PopupRequest request{
        .content = popup_.get(),
        // AnimateWindow shows CS_DROPSHADOW only once the slide has finished. Drawn from the first
        // frame, the shadow would stand at the list's final size while the list slides in over it.
        .chrome     = {.shadow = !detail::list_slide_running(list_motion_, list_slide_ms_)},
        .on_dismiss = [this](PopupCloseReason) { set_expanded(false); }, // nothing picked
    };
    if (!editable_)
    {
        // The read-only box, below the header label; an editable list hangs from the whole field.
        request.anchor_rect = Rect{0.0f, 0.0f, bounds().width, header_height_};
    }
    open_popup(std::move(request));
}

std::unique_ptr<ui::Element> ComboBox::build() &&
{
    auto node = std::make_unique<ui::ComboBoxControl>(items.initial());
    if (items.reactive())
    {
        // New items clear the selection; a bound index is then shown again as it stands, so one
        // that follows the same list (ui::bind_choice()) keeps its item whichever hears first.
        Binding<std::optional<std::size_t>> index = selected_index;
        node->add_subscription(items.subscribe(
            [raw = node.get(), index = std::move(index)](std::vector<std::string> v)
            {
                const detail::BoundUpdate bound; // a selection the new items clear isn't the user's change
                raw->set_items(std::move(v));
                if (index.reactive())
                {
                    raw->set_selected_index(index.current());
                }
            }));
    }
    apply(header, *node, [raw = node.get()](const std::string& value) { raw->set_header(value); });
    apply(description, *node, [raw = node.get()](const std::string& value) { raw->set_description(value); });
    apply(error, *node, [raw = node.get()](const std::string& value) { raw->set_error(value); });
    // Before the text and the index, which mean different things in the two modes.
    apply(editable, *node, [raw = node.get()](const bool& value) { raw->set_editable(value); });

    if (text.reactive() || !text.initial().empty())
    {
        apply(text, *node, [raw = node.get()](const std::string& value) { raw->set_text(value); });
    }
    const auto text_writer = ui::shared_writer(text);
    // A bound index shows its item; the text a two-way `.text` holds follows it. Left unset, the
    // index does not touch the text.
    if (selected_index.reactive() || selected_index.initial())
    {
        apply(selected_index,
              *node,
              [raw = node.get(), text_writer](const std::optional<std::size_t>& index)
              {
                  const std::string before = raw->text();
                  raw->set_selected_index(index);
                  if (text_writer && raw->text() != before)
                  {
                      (*text_writer)(raw->text());
                  }
              });
    }
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply(placeholder_text, *node, [raw = node.get()](const std::string& value) { raw->set_placeholder_text(value); });

    // A read-only box's text is its selection, so a bound `.text` hears of every selection change;
    // an editable one's is written as it is typed or picked, below.
    auto index_changed = ui::write_back(selected_index, std::move(on_selection_changed));
    if (index_changed || text_writer)
    {
        node->on_selection_changed(
            [handler = std::move(index_changed), text_writer, raw = node.get()](std::optional<std::size_t> index) mutable
            {
                if (text_writer && !raw->editable())
                {
                    (*text_writer)(raw->text());
                }
                if (handler)
                {
                    handler(index);
                }
            });
    }
    // The text has two ways to change by the user's hand, typing and picking an item, and a bound
    // property hears of both. Picking does not raise on_text_changed, so it writes here.
    if (text_writer)
    {
        node->on_text_changed(
            [text_writer, handler = std::move(on_text_changed)](const std::string& value) mutable
            {
                (*text_writer)(value);
                if (handler)
                {
                    handler(value);
                }
            });
        node->on_item_chosen(
            [text_writer, raw = node.get(), handler = std::move(on_item_chosen)](std::size_t index) mutable
            {
                (*text_writer)(raw->text());
                if (handler)
                {
                    handler(index);
                }
            });
    }
    else
    {
        if (on_text_changed)
        {
            node->on_text_changed(std::move(on_text_changed));
        }
        if (on_item_chosen)
        {
            node->on_item_chosen(std::move(on_item_chosen));
        }
    }
    if (on_text_submitted)
    {
        node->on_text_submitted(std::move(on_text_submitted));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
