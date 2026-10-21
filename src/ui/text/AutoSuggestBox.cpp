// SPDX-License-Identifier: MIT
#include "winui/ui/text/AutoSuggestBox.hpp"

#include "../collections/DrawnListAutomation.hpp"
#include "../input/ListSlide.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Automation.hpp"
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
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

AutoSuggestBoxControl::AutoSuggestBoxControl() : AutoSuggestBoxControl(std::string{})
{
}

AutoSuggestBoxControl::AutoSuggestBoxControl(std::string text) :
    text_box_(std::make_unique<TextBoxControl>(text)),
    user_typed_text_(std::move(text))
{
    set_parent_of(*text_box_, this);
    text_box_->on_text_changed(
        [this](const std::string& new_text)
        {
            user_typed_text_ = new_text;
            active_index_    = detail::NoIndex;
            hover_index_     = detail::NoIndex;
            list_offset_     = 0.0f;
            if (on_text_changed_)
            {
                on_text_changed_(
                    AutoSuggestBoxTextChangedArgs{.text = new_text, .reason = AutoSuggestBoxTextChangeReason::UserInput});
            }
        });
}

namespace
{
// The query button: 32x28 with a 2,0,0,0 margin, then a 4 px right margin column. Its content is
// inset 1,3.
constexpr float     QueryButtonWidth  = 32.0f;
constexpr float     QueryButtonHeight = 28.0f;
constexpr float     QueryButtonGap    = 2.0f;
constexpr float     RightButtonMargin = 4.0f;
constexpr Thickness QueryButtonMargin = Symmetric{.horizontal = 1.0f, .vertical = 3.0f};
constexpr float     IconFontSize      = 12.0f;
} // namespace

void AutoSuggestBoxControl::set_query_icon(IconSource icon)
{
    query_icon_ = std::move(icon);
    text_box_->set_trailing_space(icon ? QueryButtonGap + QueryButtonWidth + RightButtonMargin : 0.0f);
    query_hovered_ = query_pressed_ = false;
    mark_dirty(DirtyFlags::Visual);
}

Rect AutoSuggestBoxControl::query_button_rect() const
{
    if (!query_icon_)
    {
        return {};
    }
    const Rect& box    = text_box_->bounds();
    const float height = std::min(QueryButtonHeight, box.height);
    return Rect{box.right() - RightButtonMargin - QueryButtonWidth,
                box.top() + (box.height - height) * 0.5f,
                QueryButtonWidth,
                height};
}

const std::string& AutoSuggestBoxControl::text() const
{
    return text_box_->text();
}

void AutoSuggestBoxControl::set_text(std::string text)
{
    text_box_->set_text(std::move(text));
}

void AutoSuggestBoxControl::set_suggestions(std::vector<std::string> suggestions)
{
    suggestions_  = std::move(suggestions);
    active_index_ = detail::NoIndex;
    hover_index_  = detail::NoIndex;
    list_offset_  = 0.0f;
    if (is_focused())
    {
        is_open_ = !suggestions_.empty();
    }
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

void AutoSuggestBoxControl::enabled_changed()
{
    text_box_->set_enabled(is_enabled());
    if (!is_enabled())
    {
        close_suggestion_list();
    }
    mark_dirty(DirtyFlags::Visual);
}

void AutoSuggestBoxControl::focus_changed(bool /*focused*/)
{
    set_focused_of(*text_box_, is_focused());
    if (is_focused())
    {
        // On getting focus, reopen with the previous suggestions when there is text,
        // without asking the app to filter again.
        if (!text_box_->text().empty() && !suggestions_.empty())
        {
            is_open_ = true;
        }
    }
    else
    {
        close_suggestion_list();
    }
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

void AutoSuggestBoxControl::close_suggestion_list()
{
    if (!is_open_)
    {
        return;
    }
    is_open_ = false;
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

void AutoSuggestBoxControl::preview_suggestion(std::size_t index)
{
    if (index >= suggestions_.size())
    {
        return;
    }
    active_index_ = index;
    scroll_list_to(index);
    text_box_->set_text(suggestions_[index]);
    mark_dirty(DirtyFlags::Visual);
    if (on_text_changed_)
    {
        on_text_changed_(AutoSuggestBoxTextChangedArgs{.text   = suggestions_[index],
                                                       .reason = AutoSuggestBoxTextChangeReason::Suggestion});
    }
}

namespace
{
// The tallest the suggestion list gets; longer lists scroll.
constexpr float SuggestionListMaxHeight = 374.0f;
} // namespace

float AutoSuggestBoxControl::list_viewport_height() const
{
    return std::min(row_height_ * static_cast<float>(suggestions_.size()), SuggestionListMaxHeight);
}

float AutoSuggestBoxControl::list_visible_height() const
{
    // The window may give the popup less than it asked for when the space below the box is short.
    return popup_ && popup_->bounds().height > 0.0f
               ? std::min(popup_->bounds().height, list_viewport_height())
               : list_viewport_height();
}

float AutoSuggestBoxControl::list_max_offset() const
{
    return std::max(0.0f, row_height_ * static_cast<float>(suggestions_.size()) - list_visible_height());
}

void AutoSuggestBoxControl::scroll_list_to(std::size_t index)
{
    const float top    = row_height_ * static_cast<float>(index);
    const float bottom = top + row_height_;
    if (top < list_offset_)
    {
        list_offset_ = top;
    }
    else if (bottom > list_offset_ + list_visible_height())
    {
        list_offset_ = bottom - list_visible_height();
    }
    list_offset_ = std::clamp(list_offset_, 0.0f, list_max_offset());
}

void AutoSuggestBoxControl::revert_to_typed_text()
{
    active_index_ = detail::NoIndex;
    text_box_->set_text(user_typed_text_);
    mark_dirty(DirtyFlags::Visual);
    if (on_text_changed_)
    {
        on_text_changed_(AutoSuggestBoxTextChangedArgs{.text   = user_typed_text_,
                                                       .reason = AutoSuggestBoxTextChangeReason::ProgrammaticChange});
    }
}

void AutoSuggestBoxControl::submit_query()
{
    const std::string query_text = text_box_->text();
    const std::size_t chosen     = active_index_;
    if (chosen != detail::NoIndex && on_suggestion_chosen_)
    {
        on_suggestion_chosen_(chosen);
    }
    close_suggestion_list();
    if (on_query_submitted_)
    {
        on_query_submitted_(AutoSuggestBoxQuerySubmittedArgs{.query_text        = query_text,
                                                             .chosen_suggestion = detail::optional_index(chosen)});
    }
}

// Moves the selected index by one. One step past either end lands on None
// (the typed text) instead of wrapping.
void AutoSuggestBoxControl::navigate(bool forward)
{
    if (suggestions_.empty())
    {
        return;
    }
    const std::size_t last = suggestions_.size() - 1;
    if (active_index_ == 0 && last == 0)
    {
        return; // the only item is already active
    }

    if (active_index_ == detail::NoIndex)
    {
        preview_suggestion(forward ? 0 : last);
    }
    else if (active_index_ == 0)
    {
        if (forward)
        {
            preview_suggestion(1);
        }
        else
        {
            revert_to_typed_text();
        }
    }
    else if (active_index_ == last)
    {
        if (forward)
        {
            revert_to_typed_text();
        }
        else
        {
            preview_suggestion(last - 1);
        }
    }
    else
    {
        preview_suggestion(forward ? active_index_ + 1 : active_index_ - 1);
    }
}

Size AutoSuggestBoxControl::measure_override(Size available, const MeasureContext& context) const
{
    const Size header      = header_.measure(available.width, context);
    available              = ControlHeader::below(available, header);
    const Size description = description_.measure(available.width, context);
    available              = ControlHeader::below(available, description);

    const float line_height = context.measurer.measure("", context.theme.text_style).height;
    row_height_             = line_height + context.theme.padding.vertical();
    // The minimum size is the text field's minimum width and height, which the TextBox already
    // applies.
    return ControlHeader::above(ControlHeader::above(text_box_->measure(available, context), header), description);
}

void AutoSuggestBoxControl::arrange_override(const Rect&, const ArrangeContext&)
{
    header_.arrange(*this);
    description_.arrange(*this);
    const Rect& final_rect = bounds(); // the control itself, below its header
    text_box_->arrange(final_rect);
}

bool AutoSuggestBoxControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    if (query_icon_)
    {
        const bool on_button = query_button_rect().contains(e.position);
        switch (e.action)
        {
            case PointerAction::Move:
                if (on_button != query_hovered_)
                {
                    query_hovered_ = on_button;
                    mark_dirty(DirtyFlags::Visual);
                }
                break;
            case PointerAction::Leave:
                query_hovered_ = false;
                mark_dirty(DirtyFlags::Visual);
                break;
            case PointerAction::Down:
                if (e.button == PointerButton::Left && on_button)
                {
                    query_pressed_ = true;
                    mark_dirty(DirtyFlags::Visual);
                    return true;
                }
                break;
            case PointerAction::Up:
                if (e.button == PointerButton::Left && query_pressed_)
                {
                    query_pressed_ = false;
                    mark_dirty(DirtyFlags::Visual);
                    if (on_button)
                    {
                        // The query button submits the typed text alone, no suggestion chosen.
                        active_index_ = detail::NoIndex;
                        submit_query();
                    }
                    return true;
                }
                break;
            case PointerAction::Wheel:
                break;
        }
    }
    return false;
}

bool AutoSuggestBoxControl::covers_children_at(Point p) const
{
    // The query button is drawn over the text field.
    return query_icon_ && query_button_rect().contains(p);
}

bool AutoSuggestBoxControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), takes_shortcut(e));
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused())
    {
        return handled;
    }

    if (e.action == KeyAction::Down)
    {
        switch (e.key)
        {
            case Key::Escape:
                revert_to_typed_text();
                close_suggestion_list();
                return handled;

            case Key::Enter:
                submit_query();
                return handled;

            case Key::ArrowDown:
            case Key::ArrowUp:
                if (is_open_)
                {
                    navigate(e.key == Key::ArrowDown);
                    return handled;
                }
                break;

            default:
                break;
        }
    }

    // Everything else keeps editing the text box, like ComboBox. An open list does not stop typing.
    text_box_->handle_key(e);
    return handled;
}

bool AutoSuggestBoxControl::takes_shortcut(const KeyEvent& e) const
{
    return text_box_ && text_box_->takes_shortcut(e);
}

bool AutoSuggestBoxControl::text_override(const TextInputEvent& text_event)
{
    const std::string_view text = text_event.text;
    if (!is_enabled() || !is_focused())
    {
        return true;
    }
    text_box_->handle_text(text);
    return true;
}

void AutoSuggestBoxControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    header_.render(renderer, theme, is_enabled());
    description_.render(renderer, theme, is_enabled());

    text_box_->render(renderer, context);
    if (query_icon_)
    {
        // The same fill and glyph colors as the TextBox's delete button.
        const ButtonState state  = !is_enabled()    ? ButtonState::Disabled
                                   : query_pressed_ ? ButtonState::Pressed
                                   : query_hovered_ ? ButtonState::Hover
                                                    : ButtonState::Normal;
        const Rect        button = query_button_rect().inset(QueryButtonMargin);
        const Color       fill   = theme.subtle_for_state(state).background;
        if (fill.a > 0.0f && is_enabled())
        {
            renderer.fill_rounded_rect(button, theme.corner_radius, fill);
        }
        const Color glyph = is_enabled() ? theme.colors().field_text.with_alpha(query_pressed_ ? 0.45f : 0.6f)
                                         : theme.button_disabled.text;
        query_icon_.draw_in_slot(renderer, button, glyph, IconFontSize, theme.icon_font_family);
    }
}

bool AutoSuggestBoxControl::handle_context_menu(std::optional<Point> position, ContextMenuHost& host)
{
    return text_box_->handle_context_menu(position, host);
}

// The floating suggestion list. Reads the owner's state live on every call, like ComboBoxPopup.
class AutoSuggestBoxPopup final : public Element
{
public:
    explicit AutoSuggestBoxPopup(AutoSuggestBoxControl* owner) : owner_(owner)
    {
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
    {
        // As wide as the box, however much room the window offers.
        return Size{detail::list_width(owner_->bounds().width, available.width), owner_->list_viewport_height()};
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        // Opaque backing first, like ComboBoxPopup and ComboBoxPopup, so Mica does not show through.
        fill_overlay_surface(renderer, theme, bounds(), theme.overlay_corner_radius, theme.colors().overlay);

        renderer.push_clip(bounds());
        for (std::size_t i = 0; i < owner_->suggestions_.size(); ++i)
        {
            const Rect row = row_bounds(i);
            if (row.bottom() <= bounds().top() || row.top() >= bounds().bottom())
            {
                continue; // scrolled out of the list
            }
            const bool hot = (i == owner_->hover_index_) || (i == owner_->active_index_);

            if (hot)
            {
                renderer.fill_rounded_rect(row, 0.0f, theme.surfaces.menu_highlight);
            }

            const Color text_color = hot ? theme.surfaces.menu_highlight_text : theme.surfaces.menu_text;
            TextStyle   item_style = theme.text_style;
            item_style.horizontal  = TextAlign::Leading;
            const Rect text_rect{row.left() + theme.padding.left,
                                 row.top(),
                                 std::max(0.0f, row.width - theme.padding.horizontal()),
                                 row.height};
            renderer.draw_text(owner_->suggestions_[i], text_rect, text_color, item_style);
        }
        renderer.pop_clip();
        // Metro's popups have a black stroke while its field_border is white, hence menu_border.
        stroke_rounded_rect_inside(renderer,
                                   bounds(),
                                   theme.overlay_corner_radius,
                                   theme.surfaces.menu_border.value_or(theme.surfaces.field_border),
                                   theme.border_width);
    }

    bool wheel_override(const PointerEvent& e) override
    {
        const float offset = std::clamp(owner_->list_offset_ + e.wheel_delta, 0.0f, owner_->list_max_offset());
        if (offset != owner_->list_offset_)
        {
            owner_->list_offset_ = offset;
            owner_->hover_index_ = hit_test(e.position);
            mark_dirty(DirtyFlags::Visual);
        }
        return true; // a turn over the list never scrolls the page behind it
    }

    bool pointer_override(const PointerEvent& e) override
    {
        switch (e.action)
        {
            case PointerAction::Move:
                owner_->hover_index_ = hit_test(e.position);
                break;

            case PointerAction::Up:
                if (e.button == PointerButton::Left)
                {
                    const std::size_t idx = hit_test(e.position);
                    if (idx != detail::NoIndex)
                    {
                        owner_->preview_suggestion(idx);
                        owner_->submit_query();
                    }
                }
                break;

            case PointerAction::Leave:
                owner_->hover_index_ = detail::NoIndex;
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
                    bounds().top() + owner_->row_height_ * static_cast<float>(index) - owner_->list_offset_,
                    bounds().width,
                    owner_->row_height_};
    }
    [[nodiscard]] std::size_t hit_test(Point p) const
    {
        if (!bounds().contains(p))
        {
            return detail::NoIndex;
        }
        for (std::size_t i = 0; i < owner_->suggestions_.size(); ++i)
        {
            if (row_bounds(i).contains(p))
            {
                return i;
            }
        }
        return detail::NoIndex;
    }

    AutoSuggestBoxControl* owner_;
};

// ---- automation ----
// The suggestions are drawn by the popup and have no Elements: a List node stands in for them
// while they show.

detail::DrawnListAutomation& AutoSuggestBoxControl::list_automation() const
{
    if (!list_automation_)
    {
        auto* self = const_cast<AutoSuggestBoxControl*>(this); // NOLINT(cppcoreguidelines-pro-type-const-cast): a cache
        list_automation_ = std::make_unique<
            detail::DrawnListAutomation>(self,
                                         detail::DrawnListAutomation::Source{
                                             .count = [self] { return self->suggestions_.size(); },
                                             .name  = [self](std::size_t i) { return self->suggestions_[i]; },
                                             .bounds = [self] { return self->popup_ ? self->popup_->bounds() : Rect{}; },
                                             .row =
                                                 [self](std::size_t i)
                                             {
                                                 const Rect list = self->popup_ ? self->popup_->bounds() : Rect{};
                                                 return Rect{list.left(),
                                                             list.top() + self->row_height_ * static_cast<float>(i) -
                                                                 self->list_offset_,
                                                             list.width,
                                                             self->row_height_};
                                             },
                                             .selected = [self](std::size_t i) { return self->active_index_ == i; },
                                             // Fills the box with the suggestion, as an arrow key onto it does.
                                             .select =
                                                 [self](std::size_t i)
                                             {
                                                 if (!self->is_enabled())
                                                 {
                                                     return false;
                                                 }
                                                 self->preview_suggestion(i);
                                                 return true;
                                             },
                                             // Chooses it, as a click does.
                                             .invoke =
                                                 [self](std::size_t i)
                                             {
                                                 if (!self->is_enabled())
                                                 {
                                                     return false;
                                                 }
                                                 self->preview_suggestion(i);
                                                 self->submit_query();
                                                 return true;
                                             },
                                         });
    }
    auto& list = static_cast<detail::DrawnListAutomation&>(*list_automation_);
    list.place();
    return list;
}

void AutoSuggestBoxControl::automation_children(std::vector<Element*>& out) const
{
    if (popup_open() && !suggestions_.empty())
    {
        out.push_back(&list_automation());
    }
}

Element* AutoSuggestBoxControl::automation_focus() const
{
    // The highlighted suggestion while the list shows; the box itself otherwise.
    if (popup_open() && active_index_ < suggestions_.size())
    {
        return list_automation().item(active_index_);
    }
    return nullptr;
}

void AutoSuggestBoxControl::sync_popup()
{
    if (!is_open_)
    {
        close_popup();
        return;
    }
    if (!popup_)
    {
        popup_ = std::make_unique<AutoSuggestBoxPopup>(this);
    }
    // Hung from the whole control. A light dismiss closes the list and keeps the text.
    open_popup({
        .content    = popup_.get(),
        .on_dismiss = [this](PopupCloseReason) { close_suggestion_list(); },
    });
}

std::unique_ptr<ui::Element> AutoSuggestBox::build() &&
{
    auto node = std::make_unique<ui::AutoSuggestBoxControl>();
    apply(header, *node, [raw = node.get()](const std::string& value) { raw->set_header(value); });
    apply(description, *node, [raw = node.get()](const std::string& value) { raw->set_description(value); });
    apply(error, *node, [raw = node.get()](const std::string& value) { raw->set_error(value); });
    apply(suggestions, *node, [raw = node.get()](const std::vector<std::string>& v) { raw->set_suggestions(v); });

    apply(text, *node, [raw = node.get()](const std::string& value) { raw->set_text(value); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply(placeholder_text, *node, [raw = node.get()](const std::string& value) { raw->set_placeholder_text(value); });
    apply(query_icon, *node, [raw = node.get()](const IconSource& v) { raw->set_query_icon(v); });

    // Two-way: the text of every change goes back into the property, then the spec's handler runs.
    Callback<void(const AutoSuggestBoxTextChangedArgs&)> changed = std::move(on_text_changed);
    if (auto write = text.take_writer())
    {
        changed = [write = std::move(write), handler = std::move(changed)](const AutoSuggestBoxTextChangedArgs& args) mutable
        {
            write(args.text);
            if (handler)
            {
                handler(args);
            }
        };
    }
    if (changed)
    {
        node->on_text_changed(std::move(changed));
    }
    if (on_suggestion_chosen)
    {
        node->on_suggestion_chosen(std::move(on_suggestion_chosen));
    }
    if (on_query_submitted)
    {
        node->on_query_submitted(std::move(on_query_submitted));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
