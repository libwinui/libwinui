// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/scrolling/ScrollBarOverlay.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

/// A list that scrolls itself and creates Elements only for the rows in view.
///
/// It asks its item factory for a fresh Element per visible index and drops rows as they scroll
/// out, so a 100,000-item list holds about a dozen live controls. A Scroll, by contrast, keeps its
/// whole child and clips it. The visible rows are created again on every arrange and scroll step,
/// so keep per-row state in the caller's data, not in the row. The list scrolls with the wheel and
/// an overlay scroll bar. TableView's body is one. All rows share item_height() unless
/// set_size_items_to_content() lets each row be as tall as it measures. It has no spec. For a list
/// in a declared tree use ui::ListView. Build this one by hand when rows of one height, an index
/// and a factory are all a list needs.
///
/// Example:
/// @code
/// std::vector<std::string> names(1000, "Row"); // outlives the window
///
/// auto list = std::make_unique<ui::VirtualListControl>();
/// list->set_item_height(36.0f);
/// list->set_item_factory([&names](std::size_t i) -> std::unique_ptr<ui::Element> {
///     return std::make_unique<ui::TextControl>(names[i]);
/// });
/// list->set_item_count(names.size());
/// ui::VirtualListControl* observer = window.set_content(std::move(list));
/// auto top_button = std::make_unique<ui::ButtonControl>("Top");
/// top_button->on_click([observer] { observer->set_scroll_offset(0.0f); }); // back to the top
/// @endcode
class VirtualListControl final : public Element, private ScrollProvider
{
public:
    /// Builds the row for item `index`. The list takes ownership and destroys the row once it
    /// scrolls out of view.
    using ItemFactory = Callback<std::unique_ptr<Element>(std::size_t index)>;

    /// An empty list: no factory, no items, 32-DIP rows.
    VirtualListControl() = default;

    /// Sets the factory that returns the Element for any index in [0, item_count()). It is called
    /// for every visible row on each arrange and scroll step, so keep it cheap. Returning nullptr
    /// ends the realized rows there, for a source that shrank before set_item_count() caught up.
    /// That row and the ones after it stay empty. Replacing the factory rebuilds the rows on the
    /// next layout.
    void set_item_factory(ItemFactory factory)
    {
        item_factory_ = std::move(factory);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// How many items the list scrolls over; 0 by default.
    [[nodiscard]] std::size_t item_count() const
    {
        return item_count_;
    }
    /// Changes the item count and lays the list out again. The scroll offset is clamped again on
    /// the next arrange. Forgets the heights the rows measured.
    void set_item_count(std::size_t count)
    {
        item_count_ = count;
        forget_item_heights();
    }

    /// Every row's height in DIPs; 32 by default. Each row is arranged to exactly this height,
    /// whatever it measures. When the rows size to their content, it is the least height of a row
    /// and the estimate for a row not yet seen.
    [[nodiscard]] float item_height() const
    {
        return item_height_;
    }
    /// Changes the row height, lays the list out again and forgets the heights the rows measured.
    void set_item_height(float height)
    {
        item_height_ = height;
        forget_item_heights();
    }

    /// Whether each row is measured with unbounded height and is as tall as it asks for, at least
    /// item_height(). Off by default. A row keeps the height it measured while it is out of view,
    /// so scrolling back finds it where it was. Rows never seen count as item_height().
    [[nodiscard]] bool size_items_to_content() const
    {
        return size_to_content_;
    }
    /// Turns content-sized rows on or off and lays the list out again.
    void set_size_items_to_content(bool on)
    {
        if (on != size_to_content_)
        {
            size_to_content_ = on;
            forget_item_heights();
        }
    }
    /// Forgets every height the rows measured and lays the list out again. Call it after a change
    /// that reflows the rows' content, such as a new column width. The list tracks its own width
    /// changes.
    void forget_item_heights()
    {
        heights_.clear();
        offsets_dirty_ = true;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Where item `index` starts, in DIPs from the top of the content, leading space included.
    [[nodiscard]] float item_top(std::size_t index) const;
    /// Item `index`'s height. That is what it measured while the rows size to their content, and
    /// item_height() otherwise.
    [[nodiscard]] float item_extent(std::size_t index) const;
    /// The item at `y` DIPs from the top of the content. Returns item_count() past the last item
    /// and 0 over the leading space.
    [[nodiscard]] std::size_t index_at(float y) const;

    /// Room before the first item that scrolls with the content, where a ListView puts its header.
    /// The items start this far down the content, and the content is this much taller.
    [[nodiscard]] float leading_space() const
    {
        return leading_space_;
    }
    /// Sets the leading space in DIPs, clamped to >= 0. Lays the list out again only on a change.
    void set_leading_space(float space)
    {
        space = (std::max)(0.0f, space);
        if (space != leading_space_)
        {
            leading_space_ = space;
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
    /// The content's height: the leading space and every item.
    [[nodiscard]] float content_height() const
    {
        return item_top(item_count_);
    }

    /// How far the content is scrolled, in DIPs from the top (0 is the top).
    [[nodiscard]] float scroll_offset() const
    {
        return scroll_offset_;
    }
    /// Sets the offset directly, for example to scroll to the top. Clamps it to the current
    /// content range and rebuilds the realized rows at once.
    void set_scroll_offset(float offset);

    /// The width the vertical bar takes from the rows, decided by measure(). It is the bar's
    /// gutter under a theme whose bar takes room of its own, while the content overflows. It is 0
    /// for the overlay bar, which draws over the rows.
    [[nodiscard]] float right_gutter() const
    {
        return right_gutter_;
    }
    /// Where the rows are shown: the bounds minus the right gutter.
    [[nodiscard]] Rect viewport_rect() const
    {
        return Rect{bounds().x, bounds().y, (std::max)(0.0f, bounds().width - right_gutter_), bounds().height};
    }

    /// Element::scroll_info(), the same as a Scroll's. It can be pulled a tenth of its viewport
    /// past either end.
    [[nodiscard]] std::optional<ScrollInfo> scroll_info() const override
    {
        const Size viewport = viewport_rect().size();
        return ScrollInfo{Point{0.0f, scroll_offset_},
                          viewport,
                          Size{viewport.width, content_height()},
                          1.0f,
                          Size{viewport.width * 0.1f, viewport.height * 0.1f},
                          nullptr};
    }

    /// The item index of the first realized row. The realized items are [first_visible_index(),
    /// first_visible_index() + visible_count()). visible_item() takes an offset into that range.
    /// Meant for inspection and testing, not for driving the list from outside.
    [[nodiscard]] std::size_t first_visible_index() const
    {
        return first_visible_index_;
    }
    /// How many rows are realized right now: the rows at least partly in view.
    [[nodiscard]] std::size_t visible_count() const
    {
        return items_.size();
    }
    /// The `i`th realized row; 0 is the row of first_visible_index(). Throws std::out_of_range
    /// past visible_count(). The list owns the row and destroys it when it scrolls out of view.
    [[nodiscard]] Element* visible_item(std::size_t i) const
    {
        return items_.at(i).get();
    }
    /// The realized rows, top first. Only these can take focus. Focus does not scroll a row into
    /// view.
    void for_each_child(ChildVisitor visit) const override;

    // ---- automation ----
    /// Reported to screen readers as a scrollable List without children, because only the
    /// visible rows exist as Elements.
    void automation_children(std::vector<Element*>& out) const override
    {
        (void)out;
    }
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::List;
    }

protected:
    [[nodiscard]] Size                measure_override(Size available, const MeasureContext& context) const override;
    void                              arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    void                              render_override(Renderer& renderer, const RenderContext& context) const override;
    bool                              pointer_override(const PointerEvent& e) override;
    bool                              wheel_override(const PointerEvent& e) override;
    [[nodiscard]] std::optional<Rect> children_hit_clip() const override;
    [[nodiscard]] bool                covers_children_at(Point p) const override;
    /// Fades the scroll bar, then ticks the rows.
    void tick_override(Duration delta) override;


private:
    friend class TableViewControl;
    // Run after a wheel turn or a scroll bar drag moved the list (TableView's pinned header).
    Callback<void()> scrolled_by_user_;

    // Clamps scroll_offset_ and rebuilds `items_` for the visible range.
    void reflow();
    // reflow() for content-sized rows.
    void reflow_sized_to_content(const Rect& viewport);
    // Whether the bar is drawn and takes input: an era bar while it has its gutter, the overlay
    // whenever it has anything to scroll (it checks that itself).
    [[nodiscard]] bool bar_shown() const;
    // Content-sized rows: brings offsets_ up to date with heights_.
    void update_offsets() const;

    ItemFactory   item_factory_;
    std::size_t   item_count_    = 0;
    float         item_height_   = 32.0f;
    float         scroll_offset_ = 0.0f;
    float         leading_space_ = 0.0f;
    mutable float right_gutter_  = 0.0f; // measure(): the era bar's thickness while it shows

    // Content-sized rows: each row's measured height (0 while unseen), the running sum of the
    // heights by index (offsets_[i] is where row i starts, past the leading space), and the width
    // they were measured at.
    bool                       size_to_content_ = false;
    std::vector<float>         heights_;
    mutable std::vector<float> offsets_;
    mutable bool               offsets_dirty_  = true;
    float                      measured_width_ = -1.0f;

    std::vector<std::unique_ptr<Element>> items_;
    std::size_t                           first_visible_index_ = 0;
    std::size_t                           generation_          = 0; // bumped each time reflow() replaces items_

    // Cached by measure() so reflow() can measure new rows from the wheel handler, outside a layout
    // pass. Relies on the host-owned measurer outliving the list.
    mutable const TextMeasurer* measurer_ = nullptr;
    mutable const Theme*        theme_    = nullptr;

    ScrollBarOverlay scrollbar_;

private:
    // ---- automation providers ----
    void automation_providers(AutomationProviders& out) override
    {
        out.scroll = this;
    }
    // -1: the list does not scroll horizontally.
    [[nodiscard]] float scroll_horizontal_percent() const override
    {
        return -1.0f;
    }
    // The scroll position as 0..100 of the scrollable range; 0 when nothing scrolls.
    [[nodiscard]] float scroll_vertical_percent() const override
    {
        const float max_offset = (std::max)(0.0f, content_height() - viewport_rect().height);
        return max_offset > 0.0f ? (scroll_offset_ / max_offset) * 100.0f : 0.0f;
    }
    // Scrolls to `vertical` percent (0..100) of the range; a negative value leaves it alone.
    // The horizontal value is ignored.
    bool set_scroll_percent(float /*horizontal*/, float vertical) override
    {
        if (vertical < 0.0f)
        {
            return true;
        }
        const float max_offset = (std::max)(0.0f, content_height() - viewport_rect().height);
        set_scroll_offset(vertical / 100.0f * max_offset);
        return true;
    }
};

} // namespace ui
