// SPDX-License-Identifier: MIT
#include "winui/ui/navigation/Pager.hpp"

#include "../LocalText.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/dialogs/ToolTip.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/NumberBox.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

namespace
{
// The first/previous/next/last buttons: 40x40, their four glyphs at a button's default 14px
// font size. Their names are also their tooltips.
constexpr float                   PagerNavButtonSize = 40.0f;
constexpr std::array<char32_t, 4> NavGlyphs{0xE892, 0xE76B, 0xE76C, 0xE893};
constexpr float                   NavGlyphSize = 14.0f;

const std::string& nav_name(std::size_t i)
{
    const Strings::PagerText& t = strings().pager;
    switch (i)
    {
        case 0:
            return t.first_page;
        case 1:
            return t.previous_page;
        case 2:
            return t.next_page;
        default:
            return t.last_page;
    }
}

// A page number button: at least 32 wide and 20 tall, with no padding of its own.
constexpr float NumberButtonMinWidth  = 32.0f;
constexpr float NumberButtonMinHeight = 20.0f;
// The number panel's spacing, and the 20 px ellipsis glyph between page numbers.
constexpr float NumberPanelSpacing = 5.0f;
constexpr float EllipsisSize       = 20.0f;
// The current page indicator: 2 tall, corner radius 1.
constexpr float IndicatorHeight      = 2.0f;
constexpr float PagerIndicatorRadius = 1.0f;

// Auto shows a combo box below this many pages, and an open-ended count lists pages in steps of 100.
constexpr int AutoDisplayModeThreshold = 10;
constexpr int InfiniteComboBoxItems    = 100;

// The number box's large change.
constexpr double NumberBoxLargeChange = 5.0;
} // namespace

PagerControl::PagerControl()
{
    const auto adopt = [this](std::size_t part, std::unique_ptr<Element> element)
    {
        set_parent_of(*element, this);
        parts_[part] = std::move(element);
    };

    for (std::size_t i = 0; i < 4; ++i)
    {
        auto button = std::make_unique<ButtonControl>();
        button->set_variant(ButtonVariant::Subtle);
        button->set_content(std::make_unique<IconControl>(IconGlyph{NavGlyphs[i], "Segoe Fluent Icons", NavGlyphSize}));
        button->set_padding(Thickness{0.0f});
        button->set_width(PagerNavButtonSize);
        button->set_height(PagerNavButtonSize);
        button->on_click(
            [this, i]
            {
                switch (i)
                {
                    case First:
                        clamp_and_select(0);
                        break;
                    case Previous:
                        clamp_and_select(selected_index_ - 1);
                        break;
                    case Next:
                        clamp_and_select(selected_index_ + 1);
                        break;
                    default:
                        clamp_and_select(number_of_pages_ - 1);
                        break;
                }
            });
        nav_buttons_[i] = button.get();

        auto tip = std::make_unique<ToolTipControl>();
        tip->set_anchor(std::move(button));
        tip->set_content(std::make_unique<TextControl>());
        nav_tips_[i] = tip.get();
        adopt(nav_part(i), std::move(tip));
    }

    auto prefix = std::make_unique<TextControl>(prefix_text());
    prefix->set_margin(Symmetric{.horizontal = 5.0f, .vertical = 0.0f});
    prefix_ = prefix.get();
    adopt(PartPrefix, std::move(prefix));

    auto number_box = std::make_unique<NumberBoxControl>(1.0, 1.0, 1.0);
    number_box->set_step(1.0);
    number_box->set_large_step(NumberBoxLargeChange);
    number_box->set_spin_button_placement(NumberBoxSpinButtonPlacementMode::Hidden);
    number_box->on_value_changed(
        [this](std::optional<double> value)
        {
            if (!syncing_ && value)
            {
                clamp_and_select(static_cast<int>(*value) - 1);
            }
        });
    number_box_ = number_box.get();
    adopt(PartNumber, std::move(number_box));

    auto combo = std::make_unique<ComboBoxControl>();
    combo->on_selection_changed(
        [this](std::optional<std::size_t> index)
        {
            if (!syncing_ && index)
            {
                clamp_and_select(static_cast<int>(*index));
            }
        });
    combo_box_ = combo.get();
    adopt(PartCombo, std::move(combo));

    auto suffix = std::make_unique<TextControl>(suffix_text());
    suffix->set_margin(Edges{.left = 5.0f});
    suffix_ = suffix.get();
    adopt(PartSuffix, std::move(suffix));

    auto total = std::make_unique<TextControl>();
    total->set_margin(Symmetric{.horizontal = 5.0f, .vertical = 0.0f});
    total_ = total.get();
    adopt(PartTotal, std::move(total));

    for (std::size_t slot = 0; slot < Slots; ++slot)
    {
        auto button = std::make_unique<ButtonControl>();
        button->set_variant(ButtonVariant::Subtle);
        button->set_padding(Thickness{0.0f});
        button->set_min_width(NumberButtonMinWidth);
        button->set_min_height(NumberButtonMinHeight);
        // The slot is re-labeled in place when the selection moves, so a button's own click
        // never destroys it.
        button->on_click(
            [this, slot]
            {
                if (slot_pages_[slot] > 0)
                {
                    clamp_and_select(slot_pages_[slot] - 1);
                }
            });
        slot_buttons_[slot] = button.get();
        adopt(PartSlots + 2 * slot, std::move(button));

        auto ellipsis = std::make_unique<IconControl>(IconGlyph{glyphs::More.codepoint, "Segoe Fluent Icons", EllipsisSize});
        slot_ellipses_[slot] = ellipsis.get();
        adopt(PartSlots + 2 * slot + 1, std::move(ellipsis));
        slot_pages_[slot] = -1;
    }

    // The names and tooltips follow set_strings(); refresh() takes the rest from strings().
    detail::follow_strings(*this,
                           [this]
                           {
                               for (std::size_t i = 0; i < 4; ++i)
                               {
                                   nav_buttons_[i]->set_accessibility({.name = nav_name(i)});
                                   if (auto* text = dynamic_cast<TextControl*>(nav_tips_[i]->content()))
                                   {
                                       text->set_text(nav_name(i));
                                   }
                               }
                               number_box_->set_accessibility({.name = strings().pager.page});
                               combo_box_->set_accessibility({.name = strings().pager.page});
                               refresh();
                           });
}

PagerControl::~PagerControl() = default;

ButtonControl* PagerControl::first_button() const
{
    return nav_buttons_[First];
}
ButtonControl* PagerControl::previous_button() const
{
    return nav_buttons_[Previous];
}
ButtonControl* PagerControl::next_button() const
{
    return nav_buttons_[Next];
}
ButtonControl* PagerControl::last_button() const
{
    return nav_buttons_[Last];
}

ButtonControl* PagerControl::button_panel_button(std::size_t slot) const
{
    return slot < Slots && slot_pages_[slot] > 0 && states_[PartSlots + 2 * slot] != PartState::Collapsed
               ? slot_buttons_[slot]
               : nullptr;
}

std::vector<int> PagerControl::button_panel_items() const
{
    std::vector<int> items;
    for (std::size_t slot = 0; slot < Slots; ++slot)
    {
        if (slot_pages_[slot] >= 0 && effective_display_mode() == PagerDisplayMode::ButtonPanel)
        {
            items.push_back(slot_pages_[slot]);
        }
    }
    return items;
}

// ---- properties ----

void PagerControl::set_number_of_pages(std::optional<std::size_t> count)
{
    const int pages = count ? static_cast<int>(std::min<std::size_t>(*count, std::numeric_limits<int>::max())) : -1;
    if (pages == number_of_pages_)
    {
        return;
    }
    number_of_pages_ = pages;
    if (number_of_pages_ > -1 && selected_index_ > std::max(0, number_of_pages_ - 1))
    {
        select(std::max(0, number_of_pages_ - 1));
        return;
    }
    refresh();
}

void PagerControl::clamp_and_select(int index)
{
    // Nothing past the last page (unless there are none), nothing
    // below the first.
    if (number_of_pages_ > 0 && index > number_of_pages_ - 1)
    {
        index = number_of_pages_ - 1;
    }
    if (index < 0)
    {
        index = 0;
    }
    if (index != selected_index_)
    {
        select(index);
    }
}

void PagerControl::select(int index)
{
    selected_index_ = index;
    refresh();
    if (on_selection_changed_)
    {
        on_selection_changed_(static_cast<std::size_t>(selected_index_));
    }
}

void PagerControl::set_display_mode(PagerDisplayMode mode)
{
    display_mode_ = mode;
    refresh();
}

PagerDisplayMode PagerControl::effective_display_mode() const
{
    if (display_mode_ != PagerDisplayMode::Auto)
    {
        return display_mode_;
    }
    // Auto: a combo box for a known count under the threshold, else a number box.
    return number_of_pages_ > -1 && number_of_pages_ < AutoDisplayModeThreshold
               ? PagerDisplayMode::ComboBox
               : PagerDisplayMode::NumberBox;
}

void PagerControl::set_button_visibility(std::size_t button, PagerButtonVisibility visibility)
{
    button_visibility_[button] = visibility;
    refresh();
}

void PagerControl::set_first_button_visibility(PagerButtonVisibility visibility)
{
    set_button_visibility(First, visibility);
}
void PagerControl::set_previous_button_visibility(PagerButtonVisibility visibility)
{
    set_button_visibility(Previous, visibility);
}
void PagerControl::set_next_button_visibility(PagerButtonVisibility visibility)
{
    set_button_visibility(Next, visibility);
}
void PagerControl::set_last_button_visibility(PagerButtonVisibility visibility)
{
    set_button_visibility(Last, visibility);
}

void PagerControl::set_button_panel_always_show_first_last_page_index(bool show)
{
    always_show_first_last_ = show;
    refresh();
}

void PagerControl::set_prefix_text(std::optional<std::string> text)
{
    prefix_text_ = std::move(text);
    refresh();
}

void PagerControl::set_suffix_text(std::optional<std::string> text)
{
    suffix_text_ = std::move(text);
    refresh();
}

void PagerControl::enabled_changed()
{
    refresh();
}

// ---- state ----

void PagerControl::set_slot(std::size_t slot, int page)
{
    slot_pages_[slot]                 = page;
    states_[PartSlots + 2 * slot]     = page > 0 ? PartState::Shown : PartState::Collapsed;
    states_[PartSlots + 2 * slot + 1] = page == 0 ? PartState::Shown : PartState::Collapsed;
    if (page > 0)
    {
        ButtonControl* button = slot_buttons_[slot];
        button->set_text(std::to_string(page));
        button->set_accessibility({.name = detail::fill(strings().pager.page_number, {std::to_string(page)})});
        button->set_enabled(is_enabled());
    }
}

void PagerControl::refresh_button_panel()
{
    // The number panel's four patterns. A page number of 0 is an ellipsis.
    std::vector<int> items;
    int              indicator = 0;
    const int        pages     = number_of_pages_;
    const int        selected  = selected_index_;
    if (pages < 0)
    {
        if (selected < 3)
        {
            items     = {1, 2, 3, 4, 5};
            indicator = selected;
        }
        else
        {
            items     = {1, 0, selected, selected + 1, selected + 2};
            indicator = 3;
        }
    }
    else if (pages < 8)
    {
        for (int page = 1; page <= pages; ++page)
        {
            items.push_back(page);
        }
        indicator = selected;
    }
    else if (selected < 4)
    {
        items = {1, 2, 3, 4, 5};
        if (always_show_first_last_)
        {
            items.push_back(0);
            items.push_back(pages);
        }
        indicator = selected;
    }
    else if (selected >= pages - 4)
    {
        if (always_show_first_last_)
        {
            items = {1, 0};
        }
        for (int page = pages - 4; page <= pages; ++page)
        {
            items.push_back(page);
        }
        indicator = always_show_first_last_ ? selected - pages + 7 : selected - pages + 5;
    }
    else
    {
        if (always_show_first_last_)
        {
            items = {1, 0};
        }
        items.push_back(selected);
        items.push_back(selected + 1);
        items.push_back(selected + 2);
        if (always_show_first_last_)
        {
            items.push_back(0);
            items.push_back(pages);
        }
        indicator = always_show_first_last_ ? 3 : 1;
    }

    for (std::size_t slot = 0; slot < Slots; ++slot)
    {
        set_slot(slot, slot < items.size() ? items[slot] : -1);
    }
    indicator_slot_ = indicator >= 0 && static_cast<std::size_t>(indicator) < items.size() ? indicator : -1;
}

void PagerControl::refresh()
{
    const PagerDisplayMode mode = effective_display_mode();
    const bool             box  = mode == PagerDisplayMode::NumberBox || mode == PagerDisplayMode::ComboBox;

    // ---- the selector ----
    states_[PartPrefix] = box ? PartState::Shown : PartState::Collapsed;
    states_[PartNumber] = mode == PagerDisplayMode::NumberBox ? PartState::Shown : PartState::Collapsed;
    states_[PartCombo]  = mode == PagerDisplayMode::ComboBox ? PartState::Shown : PartState::Collapsed;
    // An open-ended count collapses "of N".
    states_[PartSuffix] = box && !infinite() ? PartState::Shown : PartState::Collapsed;
    states_[PartTotal]  = box && !infinite() ? PartState::Shown : PartState::Collapsed;
    prefix_->set_text(prefix_text());
    suffix_->set_text(suffix_text());
    total_->set_text(std::to_string(number_of_pages_));

    syncing_ = true;
    number_box_->set_range(1.0,
                           infinite() ? std::numeric_limits<double>::infinity()
                                      : std::max(1.0, static_cast<double>(number_of_pages_)));
    number_box_->set_value(static_cast<double>(selected_index_) + 1.0);
    number_box_->set_enabled(is_enabled());
    // The combo box lists the page numbers, or the first hundred of an unknown count.
    const int combo_items = infinite() ? std::max(combo_items_, InfiniteComboBoxItems) : number_of_pages_;
    if (combo_items != combo_items_)
    {
        std::vector<std::string> items;
        items.reserve(static_cast<std::size_t>(std::max(0, combo_items)));
        for (int page = 1; page <= combo_items; ++page)
        {
            items.push_back(std::to_string(page));
        }
        combo_box_->set_items(std::move(items));
        combo_items_ = combo_items;
    }
    if (selected_index_ < combo_items_)
    {
        combo_box_->set_selected_index(static_cast<std::size_t>(selected_index_));
    }
    combo_box_->set_enabled(is_enabled());
    syncing_ = false;

    // ---- the button panel ----
    if (mode == PagerDisplayMode::ButtonPanel)
    {
        refresh_button_panel();
    }
    else
    {
        for (std::size_t slot = 0; slot < Slots; ++slot)
        {
            set_slot(slot, -1);
        }
        indicator_slot_ = -1;
    }

    // ---- First / Previous / Next / Last ----
    const int           selected = selected_index_;
    const int           pages    = number_of_pages_;
    std::array<bool, 4> enabled{true, true, true, true};
    if (selected == 0)
    {
        enabled = {false, false, true, true};
    }
    else if (selected >= pages - 1)
    {
        enabled = {true, true, pages <= -1, false};
    }
    const std::array<bool, 4> on_edge{selected == 0, selected == 0, selected == pages - 1, selected == pages - 1};
    for (std::size_t i = 0; i < 4; ++i)
    {
        PartState state = PartState::Shown;
        switch (button_visibility_[i])
        {
            case PagerButtonVisibility::Visible:
                break;
            case PagerButtonVisibility::Hidden:
                state = PartState::Collapsed;
                break;
            case PagerButtonVisibility::HiddenOnEdge:
                state = on_edge[i] ? PartState::Hidden : PartState::Shown;
                break;
        }
        if (i == Last && infinite())
        {
            state = PartState::Collapsed; // an open-ended count has no last page
        }
        states_[nav_part(i)] = state;
        nav_buttons_[i]->set_enabled(is_enabled() && enabled[i] && state == PartState::Shown);
    }

    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

// ---- layout ----

Size PagerControl::visible_size(std::size_t part) const
{
    return sizes_[part];
}

void PagerControl::place(std::size_t part, const Rect& visible)
{
    parts_[part]->arrange(visible);
}

Size PagerControl::measure_override(Size, const MeasureContext& context) const
{
    const bool panel = effective_display_mode() == PagerDisplayMode::ButtonPanel;
    row_height_      = 0.0f;
    for (std::size_t part = 0; part < PartCount; ++part)
    {
        if (states_[part] == PartState::Collapsed)
        {
            sizes_[part] = {};
            continue;
        }
        sizes_[part] = parts_[part]->measure({Unbounded, Unbounded}, context);
        row_height_  = std::max(row_height_, sizes_[part].height);
    }

    left_group_  = sizes_[PartFirst].width + sizes_[PartPrevious].width;
    right_group_ = sizes_[PartNext].width + sizes_[PartLast].width;
    center_      = 0.0f;
    if (panel)
    {
        int shown = 0;
        for (std::size_t part = PartSlots; part < PartNext; ++part)
        {
            if (states_[part] != PartState::Collapsed)
            {
                center_ += sizes_[part].width;
                ++shown;
            }
        }
        center_ += NumberPanelSpacing * static_cast<float>(std::max(0, shown - 1));
    }
    else
    {
        for (std::size_t part = PartPrefix; part <= PartTotal; ++part)
        {
            center_ += sizes_[part].width;
        }
    }
    // Star / auto / star columns keep the selector centered, so each side is as
    // wide as the wider of the two button groups.
    return {center_ + 2.0f * std::max(left_group_, right_group_), row_height_};
}

void PagerControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    const float star     = std::max(0.0f, (final_rect.width - center_) * 0.5f);
    const auto  centered = [&](std::size_t part, float x)
    {
        const Size s = sizes_[part];
        place(part, Rect{x, final_rect.y + (final_rect.height - s.height) * 0.5f, s.width, s.height});
        return x + s.width;
    };
    const auto collapse = [&](std::size_t part) { place(part, Rect{final_rect.x, final_rect.y, 0.0f, 0.0f}); };

    // The left pair is right-aligned in its column.
    float x = final_rect.x + star - sizes_[PartPrevious].width - sizes_[PartFirst].width;
    for (const std::size_t part : {PartFirst, PartPrevious})
    {
        states_[part] == PartState::Collapsed ? collapse(part) : void(x = centered(part, x));
    }

    x = final_rect.x + star;
    for (std::size_t part = PartPrefix; part <= PartTotal; ++part)
    {
        states_[part] == PartState::Collapsed ? collapse(part) : void(x = centered(part, x));
    }
    // The number panel's items stretch to the row's height (StackLayoutEngine's cross axis).
    bool first_item = true;
    for (std::size_t part = PartSlots; part < PartNext; ++part)
    {
        if (states_[part] == PartState::Collapsed)
        {
            collapse(part);
            continue;
        }
        if (!first_item)
        {
            x += NumberPanelSpacing;
        }
        first_item = false;
        place(part, Rect{x, final_rect.y, sizes_[part].width, final_rect.height});
        x += sizes_[part].width;
    }

    // The right pair is left-aligned in its column.
    x = final_rect.x + star + center_;
    for (const std::size_t part : {PartNext, PartLast})
    {
        states_[part] == PartState::Collapsed ? collapse(part) : void(x = centered(part, x));
    }
}

// ---- render / input ----

void PagerControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    for (std::size_t part = 0; part < PartCount; ++part)
    {
        if (states_[part] == PartState::Shown)
        {
            parts_[part]->render(renderer, context);
        }
    }
    // The current page indicator: an accent bar along the bottom of the row, as wide as
    // the selected page's button.
    if (indicator_slot_ >= 0)
    {
        const std::size_t part = PartSlots + 2 * static_cast<std::size_t>(indicator_slot_);
        if (states_[part] == PartState::Shown)
        {
            const Rect b = slot_buttons_[static_cast<std::size_t>(indicator_slot_)]->bounds();
            renderer.fill_rounded_rect(Rect{b.x, bounds().bottom() - IndicatorHeight, b.width, IndicatorHeight},
                                       PagerIndicatorRadius,
                                       theme.colors().accent);
        }
    }
}

void PagerControl::for_each_child(ChildVisitor visit) const
{
    for (std::size_t part = 0; part < PartCount; ++part)
    {
        if (states_[part] == PartState::Shown)
        {
            visit(*parts_[part]);
        }
    }
}

void PagerControl::clear_dirty_override(DirtyFlags flags)
{
    for (std::size_t part = 0; part < PartCount; ++part)
    {
        if (states_[part] != PartState::Shown)
        {
            clear_dirty_of(*parts_[part], flags);
        }
    }
}

std::unique_ptr<ui::Element> Pager::build() &&
{
    auto              node = std::make_unique<ui::PagerControl>();
    ui::PagerControl* raw  = node.get();
    apply(display_mode, *raw, [raw](const PagerDisplayMode& v) { raw->set_display_mode(v); });
    apply(first_button_visibility, *raw, [raw](const PagerButtonVisibility& v) { raw->set_first_button_visibility(v); });
    apply(previous_button_visibility,
          *raw,
          [raw](const PagerButtonVisibility& v) { raw->set_previous_button_visibility(v); });
    apply(next_button_visibility, *raw, [raw](const PagerButtonVisibility& v) { raw->set_next_button_visibility(v); });
    apply(last_button_visibility, *raw, [raw](const PagerButtonVisibility& v) { raw->set_last_button_visibility(v); });
    apply(button_panel_always_show_first_last_page_index,
          *raw,
          [raw](const bool& v) { raw->set_button_panel_always_show_first_last_page_index(v); });

    // Pages before the selection, so a selection past a default of zero pages is not clamped.
    apply(number_of_pages, *node, [raw](const std::optional<std::size_t>& v) { raw->set_number_of_pages(v); });
    apply(selected_index, *node, [raw](const std::size_t& v) { raw->set_selected_index(v); });
    apply(prefix_text, *node, [raw](const std::optional<std::string>& v) { raw->set_prefix_text(v); });
    apply(suffix_text, *node, [raw](const std::optional<std::string>& v) { raw->set_suffix_text(v); });
    apply(enabled, *node, [raw](const bool& v) { raw->set_enabled(v); });

    if (auto changed = ui::write_back(selected_index, std::move(on_selection_changed)))
    {
        node->on_selection_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
