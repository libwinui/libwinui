// SPDX-License-Identifier: MIT
#include "winui/ui/input/RadioButtons.hpp"

#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
// RadioButtons metrics.
constexpr float ColumnSpacing = 7.0f; // between columns
constexpr float RowSpacing    = 8.0f; // between rows
} // namespace

RadioButtonsControl::RadioButtonsControl(std::vector<std::string> items, std::optional<std::size_t> selected_index)
{
    set_items(std::move(items));
    set_selected_index(selected_index);
}

void RadioButtonsControl::set_header(std::string header)
{
    if (header_.set_text(std::move(header)))
    {
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void RadioButtonsControl::set_items(std::vector<std::string> items)
{
    // Clear first, so a handler fired by it still sees the old items it might look up.
    set_selected_index(std::nullopt);

    items_.clear();
    items_.reserve(items.size());
    for (std::string& text : items)
    {
        auto button = std::make_unique<RadioButtonControl>(std::move(text));
        set_parent_of(*button, this);
        button->set_owner(this);
        button->set_enabled(is_enabled());
        items_.push_back(std::move(button));
    }
    focus_index_ = is_focused() ? first_enabled() : NoSlot;
    sync_item_focus();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void RadioButtonsControl::set_selected_index(std::optional<std::size_t> requested)
{
    std::size_t index = detail::index_or_none(requested);
    if (index >= items_.size())
    {
        index = detail::NoIndex;
    }
    if (index == selected_index_)
    {
        return;
    }
    if (selected_index_ != detail::NoIndex)
    {
        items_[selected_index_]->set_checked(false);
    }
    selected_index_ = index;
    if (selected_index_ != detail::NoIndex)
    {
        items_[selected_index_]->set_checked(true);
    }
    mark_dirty(DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(detail::optional_index(selected_index_));
    }
}

std::string RadioButtonsControl::selected_item() const
{
    return selected_index_ == detail::NoIndex ? std::string{} : items_[selected_index_]->text();
}

void RadioButtonsControl::set_max_columns(std::size_t columns)
{
    max_columns_ = std::max<std::size_t>(1, columns);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void RadioButtonsControl::enabled_changed()
{
    for (const auto& item : items_)
    {
        item->set_enabled(is_enabled());
    }
    mark_dirty(DirtyFlags::Visual);
}

void RadioButtonsControl::select(RadioButtonControl* button)
{
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        if (items_[i].get() == button)
        {
            move_focus(static_cast<int>(i), true);
            return;
        }
    }
}

void RadioButtonsControl::move_focus(int index, bool select_it)
{
    focus_index_ = index;
    sync_item_focus();
    if (select_it)
    {
        set_selected_index(index == NoSlot ? std::nullopt : std::optional<std::size_t>{static_cast<std::size_t>(index)});
    }
    mark_dirty(DirtyFlags::Visual);
}

int RadioButtonsControl::first_enabled() const
{
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        if (items_[i]->is_enabled())
        {
            return static_cast<int>(i);
        }
    }
    return NoSlot;
}

void RadioButtonsControl::sync_item_focus()
{
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        set_focused_of(*items_[i], is_focused() && static_cast<int>(i) == focus_index_);
    }
}

// ---- layout ----

Size RadioButtonsControl::measure_override(Size available, const MeasureContext& context) const
{
    const Size header = header_.measure(available.width, context);
    available         = ControlHeader::below(available, header);

    // The column-major uniform grid measures every item against the whole
    // offered size, the largest one sizing every cell.
    measured_.clear();
    cell_ = {};
    for (const auto& item : items_)
    {
        const Size size = item->measure(Size{available.width, Unbounded}, context);
        measured_.push_back(size);
        cell_.width  = std::max(cell_.width, size.width);
        cell_.height = std::max(cell_.height, size.height);
    }

    // Columns: as many as asked for and the items fill, but only as many as fit the
    // offered width (an unbounded width fits them all), and never fewer than one.
    const int count     = static_cast<int>(items_.size());
    const int requested = static_cast<int>(std::min(max_columns_, items_.size()));
    columns_            = std::max(1, requested);
    const float extra   = ColumnSpacing + cell_.width;
    if (std::isfinite(available.width) && extra >= std::numeric_limits<float>::epsilon())
    {
        const float fits = std::max(0.0f, std::floor((available.width - cell_.width) / extra)) + 1.0f;
        columns_         = std::max(1, static_cast<int>(std::min(static_cast<float>(requested), fits)));
    }

    Size grid{};
    if (count > 0)
    {
        const int rows = (count + columns_ - 1) / columns_;
        grid.width     = cell_.width * static_cast<float>(columns_) + ColumnSpacing * static_cast<float>(columns_ - 1);
        grid.height    = cell_.height * static_cast<float>(rows) + RowSpacing * static_cast<float>(rows - 1);
    }

    Size desired   = grid;
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return ControlHeader::above(desired, header);
}

void RadioButtonsControl::arrange_override(const Rect&, const ArrangeContext&)
{
    header_.arrange(*this);
    const int count = static_cast<int>(items_.size());
    if (count == 0 || measured_.size() != items_.size())
    {
        return;
    }

    // The column-major uniform grid arranges by this rule: columns fill top to
    // bottom, and when the count does not divide evenly the leftmost columns take one extra.
    // measure_override() keeps columns_ within the count, so per_column is at least one already.
    const int   columns       = std::clamp(columns_, 1, count);
    const int   per_column    = std::max(1, count / columns);
    const int   extra_columns = count % columns;
    const float top           = bounds().top();
    float       x             = bounds().left();
    float       y             = top;
    int         column        = 0;
    for (int index = 0; index < count; ++index)
    {
        const Size& size = measured_[static_cast<std::size_t>(index)];
        items_[static_cast<std::size_t>(index)]->arrange(Rect{x, y, size.width, size.height});

        const bool last_in_column = column < extra_columns
                                        ? index % (per_column + 1) == per_column
                                        : (index - extra_columns * (per_column + 1)) % per_column == per_column - 1;
        if (last_in_column)
        {
            x += cell_.width + ColumnSpacing;
            y = top;
            ++column;
        }
        else
        {
            y += cell_.height + RowSpacing;
        }
    }
}

// ---- focus ----

void RadioButtonsControl::focus_changed(bool /*focused*/)
{
    if (is_focused())
    {
        // Focus arriving from outside goes to the selected item.
        const bool selected_usable = selected_index_ != detail::NoIndex && items_[selected_index_]->is_enabled();
        focus_index_               = selected_usable ? static_cast<int>(selected_index_) : first_enabled();
    }
    sync_item_focus();
    mark_dirty(DirtyFlags::Visual);
}

bool RadioButtonsControl::focusable() const
{
    return is_enabled() && first_enabled() != NoSlot;
}

// ---- input ----

bool RadioButtonsControl::pointer_override(const PointerEvent& e)
{
    if (e.action == PointerAction::Down && e.button == PointerButton::Left)
    {
        // The pressed item gets the keyboard's attention, so Space and the arrows carry on from
        // where the pointer was rather than from wherever focus last was.
        for (std::size_t i = 0; i < items_.size(); ++i)
        {
            if (items_[i]->is_enabled() && items_[i]->bounds().contains(e.position))
            {
                focus_index_ = static_cast<int>(i);
                sync_item_focus();
                break;
            }
        }
    }
    return false;
}

int RadioButtonsControl::arrow_target(Key key) const
{
    if (focus_index_ == NoSlot)
    {
        return NoSlot;
    }
    const int count = static_cast<int>(items_.size());
    switch (key)
    {
        case Key::ArrowUp:
            for (int i = focus_index_ - 1; i >= 0; --i)
            {
                if (items_[static_cast<std::size_t>(i)]->is_enabled())
                {
                    return i;
                }
            }
            return NoSlot;

        case Key::ArrowDown:
            for (int i = focus_index_ + 1; i < count; ++i)
            {
                if (items_[static_cast<std::size_t>(i)]->is_enabled())
                {
                    return i;
                }
            }
            return NoSlot;

        case Key::ArrowLeft:
        case Key::ArrowRight:
        {
            // Directional focus confined to the group: the nearest item in the arrow's direction,
            // searched among the group's items only. Columns compare by left edge, since each item keeps its own width.
            const Rect& from    = items_[static_cast<std::size_t>(focus_index_)]->bounds();
            const float cy      = from.top() + from.height * 0.5f;
            int         best    = NoSlot;
            float       best_dx = 0.0f, best_dy = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                const RadioButtonControl& candidate = *items_[static_cast<std::size_t>(i)];
                if (i == focus_index_ || !candidate.is_enabled())
                {
                    continue;
                }
                const Rect& r  = candidate.bounds();
                const float dx = r.left() - from.left();
                if (key == Key::ArrowLeft ? dx > -0.5f : dx < 0.5f)
                {
                    continue;
                }
                const float adx = std::abs(dx);
                const float ady = std::abs(r.top() + r.height * 0.5f - cy);
                if (best == NoSlot || adx < best_dx - 0.5f || (std::abs(adx - best_dx) <= 0.5f && ady < best_dy))
                {
                    best    = i;
                    best_dx = adx;
                    best_dy = ady;
                }
            }
            return best;
        }

        default:
            return NoSlot;
    }
}

bool RadioButtonsControl::takes_arrow_key(Key key) const
{
    return is_focused() && arrow_target(key) != NoSlot;
}

bool RadioButtonsControl::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, takes_arrow_key(e.key), false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused() || focus_index_ == NoSlot)
    {
        return handled;
    }
    if (e.key == Key::ArrowUp || e.key == Key::ArrowDown || e.key == Key::ArrowLeft || e.key == Key::ArrowRight)
    {
        if (e.action == KeyAction::Down)
        {
            if (const int target = arrow_target(e.key); target != NoSlot)
            {
                // Selection follows focus unless Ctrl is held.
                move_focus(target, !e.modifiers.ctrl);
            }
        }
        return handled;
    }
    items_[static_cast<std::size_t>(focus_index_)]->handle_key(e);
    return handled;
}

// ---- render ----

void RadioButtonsControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    header_.render(renderer, theme, is_enabled());
    for (const auto& item : items_)
    {
        item->render(renderer, context);
    }
}

// ---- forwarding ----

std::unique_ptr<ui::Element> RadioButtons::build() &&
{
    // Items and the first selection go to the constructor, so the initial check is in place
    // before anything is bound and no handler sees it.
    auto node = std::make_unique<ui::RadioButtonsControl>(items.initial(), selected_index.initial());
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
    apply(selected_index,
          *node,
          [raw = node.get()](const std::optional<std::size_t>& value) { raw->set_selected_index(value); });
    apply(max_columns, *node, [raw = node.get()](const std::size_t& value) { raw->set_max_columns(value); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });

    if (auto changed = ui::write_back(selected_index, std::move(on_selection_changed)))
    {
        node->on_selection_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

Rect RadioButtonsControl::focus_ring_rect(const Theme& theme) const
{
    const RadioButtonControl* focused = focus_index_ != NoSlot ? item(static_cast<std::size_t>(focus_index_)) : nullptr;
    return focused != nullptr ? focused->focus_ring_rect(theme) : Rect{};
}

float RadioButtonsControl::focus_ring_corner_radius(const Theme& theme) const
{
    const RadioButtonControl* focused = focus_index_ != NoSlot ? item(static_cast<std::size_t>(focus_index_)) : nullptr;
    return focused != nullptr ? focused->focus_ring_corner_radius(theme) : theme.corner_radius;
}

} // namespace ui
