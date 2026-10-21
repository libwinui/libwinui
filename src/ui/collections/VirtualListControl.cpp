// SPDX-License-Identifier: MIT
#include "winui/ui/collections/VirtualListControl.hpp"

#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include <cmath>
#include <cstddef>

namespace ui
{

Size VirtualListControl::measure_override(Size available, const MeasureContext& context) const
{
    measurer_ = &context.measurer;
    theme_    = &context.theme;
    // An era bar takes a gutter of its own while the content overflows.
    scrollbar_.apply_theme(context.theme);
    right_gutter_ = scrollbar_.gutter() > 0.0f && content_height() > available.height ? scrollbar_.gutter() : 0.0f;
    return {
        std::isfinite(available.width) ? available.width : 0.0f,
        std::isfinite(available.height) ? available.height : content_height(),
    };
}

void VirtualListControl::arrange_override(const Rect&, const ArrangeContext&)
{
    reflow();
}

bool VirtualListControl::bar_shown() const
{
    return scrollbar_.gutter() == 0.0f || right_gutter_ > 0.0f;
}

void VirtualListControl::update_offsets() const
{
    if (!offsets_dirty_ && offsets_.size() == item_count_ + 1)
    {
        return;
    }
    offsets_.resize(item_count_ + 1);
    float y = 0.0f;
    for (std::size_t i = 0; i < item_count_; ++i)
    {
        offsets_[i] = y;
        y += i < heights_.size() && heights_[i] > 0.0f ? heights_[i] : item_height_;
    }
    offsets_[item_count_] = y;
    offsets_dirty_        = false;
}

float VirtualListControl::item_top(std::size_t index) const
{
    index = std::min(index, item_count_);
    if (!size_to_content_)
    {
        return leading_space_ + static_cast<float>(index) * item_height_;
    }
    update_offsets();
    return leading_space_ + offsets_[index];
}

float VirtualListControl::item_extent(std::size_t index) const
{
    if (size_to_content_ && index < heights_.size() && heights_[index] > 0.0f)
    {
        return heights_[index];
    }
    return item_height_;
}

std::size_t VirtualListControl::index_at(float y) const
{
    const float items_y = std::max(0.0f, y - leading_space_);
    if (!size_to_content_)
    {
        return item_height_ > 0.0f ? std::min(static_cast<std::size_t>(items_y / item_height_), item_count_) : item_count_;
    }
    update_offsets();
    // The last row starting at or above `items_y`.
    const auto it    = std::upper_bound(offsets_.begin(), offsets_.end() - 1, items_y);
    const auto index = static_cast<std::size_t>(it - offsets_.begin());
    return index == 0 ? 0 : (items_y >= offsets_[item_count_] ? item_count_ : index - 1);
}

void VirtualListControl::reflow()
{
    const Rect  viewport        = viewport_rect();
    const float total_height    = content_height();
    const float viewport_height = viewport.height;
    const float max_offset      = std::max(0.0f, total_height - viewport_height);
    scroll_offset_              = std::clamp(scroll_offset_, 0.0f, max_offset);

    // A row's own click handler can land here (a group header collapsing reflows the list), so the
    // old rows are retired rather than destroyed while that handler is still on the stack.
    for (std::unique_ptr<Element>& item : items_)
    {
        detail::retire(std::move(item));
    }
    items_.clear();
    ++generation_;
    first_visible_index_ = 0;

    if (item_count_ == 0 || !item_factory_ || item_height_ <= 0.0f || viewport_height <= 0.0f)
    {
        return;
    }
    if (size_to_content_)
    {
        reflow_sized_to_content(viewport);
        return;
    }

    // The items' own scroll position: the leading space (a header) scrolls away first.
    const float items_offset = scroll_offset_ - leading_space_;
    const auto  first        = static_cast<std::size_t>(std::max(0.0f, items_offset) / item_height_);
    const float last_y       = items_offset + viewport_height;
    std::size_t last = last_y <= 0.0f ? 0 : std::min(static_cast<std::size_t>(std::ceil(last_y / item_height_)), item_count_);
    first_visible_index_ = std::min(first, item_count_);
    last                 = std::max(last, first_visible_index_);

    items_.reserve(last - first_visible_index_);
    for (std::size_t i = first_visible_index_; i < last; ++i)
    {
        std::unique_ptr<Element> item = item_factory_(i);
        if (!item)
        {
            break;
        }
        set_parent_of(*item, this);

        const Rect item_rect{bounds().left(),
                             bounds().top() + leading_space_ + static_cast<float>(i) * item_height_ - scroll_offset_,
                             viewport.width,
                             item_height_};
        if (measurer_ && theme_)
        {
            // item_height_ wins over the answer, but measure() still runs so a container row gets
            // the measure-before-arrange contract Element::arrange() relies on.
            (void)item->measure(item_rect.size(), *measurer_, *theme_);
        }
        item->arrange(item_rect);
        items_.push_back(std::move(item));
    }
}

void VirtualListControl::reflow_sized_to_content(const Rect& viewport)
{
    // The heights hold for one width; a new one reflows every row's content.
    if (viewport.width != measured_width_)
    {
        heights_.clear();
        offsets_dirty_  = true;
        measured_width_ = viewport.width;
    }
    if (heights_.size() != item_count_)
    {
        heights_.resize(item_count_, 0.0f);
        offsets_dirty_ = true;
    }

    // Each row in view is measured and placed below the one before, so a row that turns out
    // taller than estimated pushes the rest down within this pass.
    first_visible_index_ = std::min(index_at(scroll_offset_), item_count_);
    float       top      = bounds().top() + item_top(first_visible_index_) - scroll_offset_;
    const float bottom   = bounds().top() + viewport.height;
    for (std::size_t i = first_visible_index_; i < item_count_ && top < bottom; ++i)
    {
        std::unique_ptr<Element> item = item_factory_(i);
        if (!item)
        {
            break;
        }
        set_parent_of(*item, this);
        float height = item_height_;
        if (measurer_ && theme_)
        {
            height = std::max(item_height_, item->measure(Size{viewport.width, Unbounded}, *measurer_, *theme_).height);
        }
        if (heights_[i] != height)
        {
            heights_[i]    = height;
            offsets_dirty_ = true;
        }
        item->arrange(Rect{bounds().left(), top, viewport.width, height});
        top += height;
        items_.push_back(std::move(item));
    }
}

void VirtualListControl::set_scroll_offset(float offset)
{
    scroll_offset_ = offset; // reflow() clamps to the current content range
    reflow();
    mark_dirty(DirtyFlags::Visual);
}

void VirtualListControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    renderer.push_clip(viewport_rect());
    for (const auto& item : items_)
    {
        item->render(renderer, context);
    }
    renderer.pop_clip();
    if (bar_shown())
    {
        scrollbar_.render(renderer, theme, bounds(), content_height(), scroll_offset_);
    }
}

bool VirtualListControl::pointer_override(const PointerEvent& e)
{
    if (e.action == PointerAction::Wheel)
    {
        if (bounds().contains(e.position) && !e.horizontal_wheel) // vertical-only list
        {
            scroll_offset_ += e.wheel_delta;
            reflow();
            mark_dirty(DirtyFlags::Visual);
            scrollbar_.notify_activity();
            if (scrolled_by_user_)
            {
                scrolled_by_user_();
            }
        }
        return false;
    }

    // The overlay scrollbar gets first refusal: rows span the full width, so a press on the band
    // would otherwise also reach the row behind it.
    const float content_length = content_height();
    float       offset         = scroll_offset_;
    if (bar_shown() && scrollbar_.handle_pointer(e, bounds(), content_length, offset))
    {
        if (offset != scroll_offset_)
        {
            set_scroll_offset(offset);
            if (scrolled_by_user_)
            {
                scrolled_by_user_();
            }
        }
        return true;
    }
    return false;
}

std::optional<Rect> VirtualListControl::children_hit_clip() const
{
    // Row coordinates can extend past the viewport; a row is hit only where it shows.
    return viewport_rect();
}

bool VirtualListControl::covers_children_at(Point p) const
{
    // The overlay scrollbar: rows span the full width, so a press on the band would otherwise
    // reach the row behind it.
    return bar_shown() && scrollbar_.band_bounds(bounds()).contains(p);
}

bool VirtualListControl::wheel_override(const PointerEvent& e)
{
    if (!bounds().contains(e.position))
    {
        return false;
    }
    // Scroll chaining (ScrollChainMode::Auto): a turn this list cannot act on, sideways or past
    // the end, is left to whatever scrolls around it.
    const float max_offset = std::max(0.0f, content_height() - viewport_rect().height);
    const bool  at_end     = e.wheel_delta > 0.0f ? scroll_offset_ >= max_offset : scroll_offset_ <= 0.0f;
    if (e.horizontal_wheel || at_end)
    {
        return false;
    }
    pointer_override(e);
    return true;
}

void VirtualListControl::for_each_child(ChildVisitor visit) const
{
    for (const auto& item : items_)
    {
        visit(*item);
    }
}

void VirtualListControl::tick_override(Duration delta)
{
    const bool scrollbar_animating = scrollbar_.update(delta);
    if (scrollbar_animating)
    {
        mark_dirty(DirtyFlags::Visual);
    }
    if (const std::optional<Duration> wait = scrollbar_.fade_wait();
        wait && bar_shown() && scrollbar_.scrollable(bounds(), content_height()))
    {
        request_tick_in(*wait); // a shown bar waiting to fade wakes the window once for it
    }
}

} // namespace ui
