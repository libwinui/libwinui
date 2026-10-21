// SPDX-License-Identifier: MIT
#include "winui/ui/collections/ForEach.hpp"

#include "../ExitGhosts.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/layouts/Layout.hpp"
#include "winui/ui/layouts/StackLayout.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{

// Where an element the layout did not place is parked.
constexpr float Parked = -10000.0f;

// A realization rect that moved less than this is the same one.
bool same_rect(const Rect& a, const Rect& b)
{
    constexpr float Epsilon = 0.01f;
    return std::abs(a.x - b.x) <= Epsilon && std::abs(a.y - b.y) <= Epsilon && std::abs(a.width - b.width) <= Epsilon &&
           std::abs(a.height - b.height) <= Epsilon;
}

} // namespace

// ---- contexts ----

class RepeaterLayoutContext final : public VirtualizingLayoutContext
{
public:
    explicit RepeaterLayoutContext(ForEachControl& owner) : owner_(owner)
    {
    }

    [[nodiscard]] std::size_t item_count() const override
    {
        return owner_.item_count_;
    }
    Element& get_or_create_element_at(std::size_t index, ElementRealizationOptions options) override
    {
        return owner_.realize(index, options);
    }
    void recycle_element(Element& element) override
    {
        if (const std::optional<std::size_t> index = owner_.element_index(element))
        {
            ForEachControl::Entry& entry = owner_.realized_.at(*index);
            if (entry.element->has_focus_within())
            {
                // Pinned: kept, and no longer the layout's to recycle.
                entry.suppressed = false;
                entry.used       = false;
                return;
            }
            owner_.clear_element(*index);
        }
    }
    [[nodiscard]] std::optional<std::size_t> index_of(const Element& element) const override
    {
        return owner_.element_index(element);
    }
    [[nodiscard]] Rect realization_rect() const override
    {
        return owner_.realization_;
    }
    [[nodiscard]] Rect visible_rect() const override
    {
        return owner_.visible_;
    }
    [[nodiscard]] std::optional<std::size_t> recommended_anchor_index() const override
    {
        return owner_.recommended_anchor_;
    }
    [[nodiscard]] Point layout_origin() const override
    {
        return owner_.layout_origin_;
    }
    void set_layout_origin(Point origin) override
    {
        owner_.layout_origin_ = origin;
    }
    Size measure(Element& element, Size available) override
    {
        return owner_.measure_element(element, available);
    }
    [[nodiscard]] Size desired_size(const Element& element) const override
    {
        const ForEachControl::Entry* entry = owner_.entry_of(element);
        return entry != nullptr ? entry->desired : Size{};
    }
    void arrange(Element& element, const Rect& rect) override
    {
        element.arrange(Rect{owner_.bounds().x + rect.x, owner_.bounds().y + rect.y, rect.width, rect.height});
        if (ForEachControl::Entry* entry = owner_.entry_of(element))
        {
            entry->arranged = true;
        }
    }

private:
    ForEachControl& owner_;
};

// Every item realized, by index, for a NonVirtualizingLayout.
class RepeaterNonVirtualContext final : public NonVirtualizingLayoutContext
{
public:
    explicit RepeaterNonVirtualContext(ForEachControl& owner) : owner_(owner)
    {
    }

    [[nodiscard]] std::size_t item_count() const override
    {
        return owner_.item_count_;
    }
    Size measure(std::size_t index, Size available) override
    {
        return owner_.measure_element(owner_.realize(index, ElementRealizationOptions::None), available);
    }
    [[nodiscard]] Size desired_size(std::size_t index) const override
    {
        const auto it = owner_.realized_.find(index);
        return it != owner_.realized_.end() ? it->second.desired : Size{};
    }
    void arrange(std::size_t index, const Rect& rect) override
    {
        const auto it = owner_.realized_.find(index);
        if (it != owner_.realized_.end())
        {
            it->second.element->arrange(
                Rect{owner_.bounds().x + rect.x, owner_.bounds().y + rect.y, rect.width, rect.height});
            it->second.arranged = true;
        }
    }

private:
    ForEachControl& owner_;
};

// ---- ForEach ----

ForEachControl::ForEachControl()
{
    replace_layout(nullptr); // the default StackLayoutEngine
}

ForEachControl::~ForEachControl()
{
    if (layout_)
    {
        layout_->set_owner(nullptr);
    }
}

void ForEachControl::replace_layout(std::unique_ptr<Layout> layout)
{
    if (!layout)
    {
        layout = std::make_unique<StackLayoutEngine>(); // ForEach's default
    }
    if (layout_)
    {
        if (auto* virtualizing = dynamic_cast<VirtualizingLayout*>(layout_.get()); virtualizing && layout_initialized_)
        {
            RepeaterLayoutContext context(*this);
            virtualizing->uninitialize_for_context(context);
        }
        layout_->set_owner(nullptr);
    }
    // A new layout starts from nothing realized.
    clear_all();
    layout_             = std::move(layout);
    layout_initialized_ = false;
    layout_origin_      = Point{};
    layout_->set_owner(this);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void ForEachControl::set_item_template(ItemTemplate item_template)
{
    item_template_ = std::move(item_template);
    notify_reset();
}

void ForEachControl::set_item_count(std::size_t count)
{
    item_count_ = count;
    notify_reset();
}

void ForEachControl::set_horizontal_cache_length(float length)
{
    horizontal_cache_length_ = std::max(0.0f, std::isfinite(length) ? length : 0.0f);
    mark_dirty(DirtyFlags::Layout);
}

void ForEachControl::set_vertical_cache_length(float length)
{
    vertical_cache_length_ = std::max(0.0f, std::isfinite(length) ? length : 0.0f);
    mark_dirty(DirtyFlags::Layout);
}

// ---- collection changes ----

void ForEachControl::notify_layout(const ItemsChange& change)
{
    if (auto* virtualizing = dynamic_cast<VirtualizingLayout*>(layout_.get()))
    {
        RepeaterLayoutContext context(*this);
        virtualizing->on_items_changed(context, change);
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void ForEachControl::shift_entries(std::size_t from, std::ptrdiff_t delta)
{
    // Re-key every realized item at or after `from`, telling on_element_index_changed.
    std::vector<std::pair<std::size_t, Entry>> moved;
    for (auto it = realized_.lower_bound(from); it != realized_.end();)
    {
        moved.emplace_back(it->first, std::move(it->second));
        it = realized_.erase(it);
    }
    for (auto& [old_index, entry] : moved)
    {
        const std::size_t new_index    = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(old_index) + delta);
        index_of_[entry.element.get()] = new_index;
        Element& element               = *entry.element;
        realized_.emplace(new_index, std::move(entry));
        if (on_element_index_changed_)
        {
            on_element_index_changed_(
                ElementIndexChangedArgs{.element = element, .old_index = old_index, .new_index = new_index});
        }
    }
}

void ForEachControl::notify_inserted(std::size_t index, std::size_t count)
{
    if (defer_change(ChangeKind::Inserted, index, count))
    {
        return;
    }
    if (count == 0)
    {
        return;
    }
    index = std::min(index, item_count_);
    item_count_ += count;
    shift_entries(index, static_cast<std::ptrdiff_t>(count));
    notify_layout(ItemsChange{ItemsChangeAction::Add, 0, 0, index, count});
}

void ForEachControl::notify_removed(std::size_t index, std::size_t count)
{
    if (defer_change(ChangeKind::Removed, index, count))
    {
        return;
    }
    if (index >= item_count_ || count == 0)
    {
        return;
    }
    count = std::min(count, item_count_ - index);
    // The layout forgets the removed items first; their elements go after.
    std::vector<std::size_t> removed;
    for (auto it = realized_.lower_bound(index); it != realized_.end() && it->first < index + count; ++it)
    {
        removed.push_back(it->first);
    }
    item_count_ -= count; // the source has already changed when it reports the removal
    notify_layout(ItemsChange{ItemsChangeAction::Remove, index, count, 0, 0});
    // The delete half of AddRemoveTransition: each removed element shrinks away where it was.
    const bool leaving = find_transition<AddRemoveTransition>(children_transitions()) != nullptr;
    for (std::size_t i : removed)
    {
        clear_element(i, leaving);
    }
    shift_entries(index + count, -static_cast<std::ptrdiff_t>(count));
}

void ForEachControl::notify_replaced(std::size_t index, std::size_t count)
{
    if (defer_change(ChangeKind::Replaced, index, count))
    {
        return;
    }
    if (index >= item_count_ || count == 0)
    {
        return;
    }
    count = std::min(count, item_count_ - index);
    notify_layout(ItemsChange{ItemsChangeAction::Replace, index, count, index, count});
    for (std::size_t i = index; i < index + count; ++i)
    {
        clear_element(i);
    }
}

void ForEachControl::notify_moved(std::size_t from, std::size_t to)
{
    if (defer_change(ChangeKind::Moved, from, to))
    {
        return;
    }
    if (from >= item_count_ || to >= item_count_ || from == to)
    {
        return;
    }
    notify_layout(ItemsChange{ItemsChangeAction::Move, from, 1, to, 1});
    // The moved item keeps its element, and with it the element's state: taken out, the items
    // between closed up and opened again at `to`, then put back there.
    Entry      moved;
    const auto it = realized_.find(from);
    if (it != realized_.end())
    {
        moved = std::move(it->second);
        realized_.erase(it);
    }
    shift_entries(from + 1, -1);
    shift_entries(to, 1);
    if (moved.element)
    {
        Element& element    = *moved.element;
        index_of_[&element] = to;
        realized_.emplace(to, std::move(moved));
        if (on_element_index_changed_)
        {
            on_element_index_changed_(ElementIndexChangedArgs{.element = element, .old_index = from, .new_index = to});
        }
    }
}

bool ForEachControl::defer_change(ChangeKind kind, std::size_t first, std::size_t second)
{
    if (busy_ == 0)
    {
        replay_deferred_changes(); // queued ones first, so the order holds
        return false;
    }
    deferred_changes_.push_back(DeferredChange{kind, first, second});
    return true;
}

void ForEachControl::replay_deferred_changes()
{
    if (busy_ != 0 || deferred_changes_.empty())
    {
        return;
    }
    // Taken first: a change replayed here is told straight away, and one a later template makes
    // is queued afresh.
    const std::vector<DeferredChange> changes = std::exchange(deferred_changes_, {});
    for (const DeferredChange& change : changes)
    {
        switch (change.kind)
        {
            case ChangeKind::Inserted:
                notify_inserted(change.first, change.second);
                break;
            case ChangeKind::Removed:
                notify_removed(change.first, change.second);
                break;
            case ChangeKind::Replaced:
                notify_replaced(change.first, change.second);
                break;
            case ChangeKind::Moved:
                notify_moved(change.first, change.second);
                break;
            case ChangeKind::Reset:
                notify_reset();
                break;
        }
    }
    relayout_pending_ = true; // the pass that queued them laid out the old collection
}

void ForEachControl::notify_reset()
{
    if (defer_change(ChangeKind::Reset))
    {
        return;
    }
    notify_layout(ItemsChange{ItemsChangeAction::Reset, 0, 0, 0, item_count_});
    clear_all();
}

// ---- elements ----

Element* ForEachControl::try_get_element(std::size_t index) const
{
    const auto it = realized_.find(index);
    return it != realized_.end() ? it->second.element.get() : nullptr;
}

std::optional<std::size_t> ForEachControl::element_index(const Element& element) const
{
    const auto it = index_of_.find(&element);
    return it != index_of_.end() ? std::optional<std::size_t>{it->second} : std::nullopt;
}

std::vector<std::size_t> ForEachControl::realized_indices() const
{
    std::vector<std::size_t> out;
    out.reserve(realized_.size());
    for (const auto& [index, entry] : realized_)
    {
        out.push_back(index);
    }
    return out;
}

ForEachControl::Entry* ForEachControl::entry_of(const Element& element)
{
    const auto it = index_of_.find(&element);
    if (it == index_of_.end())
    {
        return nullptr;
    }
    const auto entry = realized_.find(it->second);
    return entry != realized_.end() ? &entry->second : nullptr;
}

Element& ForEachControl::realize(std::size_t index, ElementRealizationOptions options)
{
    auto it = realized_.find(index);
    if (it == realized_.end())
    {
        std::unique_ptr<Element> element;
        if (item_template_)
        {
            ++busy_;
            element = item_template_(index);
            --busy_;
        }
        if (!element)
        {
            // A template that made nothing still has to answer: an empty stand-in.
            class Empty final : public Element
            {
                [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
                {
                    return {};
                }
                void render_override(Renderer&, const RenderContext&) const override
                {
                }
            };
            element = std::make_unique<Empty>();
        }
        set_parent_of(*element, this);
        Entry entry;
        entry.element = std::move(element);
        Element* raw  = entry.element.get();
        // A template that edited the collection (notify_inserted() from inside it) may have moved
        // another realized item onto `index` meanwhile. That one stands for the item now there, so
        // it stays, and the element just made goes without ever being indexed or announced.
        const auto [at, inserted] = realized_.try_emplace(index, std::move(entry));
        it                        = at;
        if (inserted)
        {
            index_of_[raw] = index;
            mark_dirty(DirtyFlags::Visual);
            if (on_element_prepared_)
            {
                on_element_prepared_(ElementPreparedArgs{.element = *raw, .index = index});
            }
        }
        else
        {
            detail::retire(std::move(entry.element));
            mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
    it->second.used = true;
    if (has_flag(options, ElementRealizationOptions::SuppressAutoRecycle))
    {
        it->second.suppressed = true;
    }
    return *it->second.element;
}

void ForEachControl::clear_element(std::size_t index, bool leaving)
{
    const auto it = realized_.find(index);
    if (it == realized_.end())
    {
        return;
    }
    std::unique_ptr<Element> element  = std::move(it->second.element);
    const bool               arranged = it->second.arranged;
    realized_.erase(it);
    index_of_.erase(element.get());
    if (on_element_clearing_)
    {
        on_element_clearing_(*element);
    }
    mark_dirty(DirtyFlags::Visual);
    if (leaving && arranged)
    {
        detail::ensure(ghosts_).add(std::move(element), bounds().origin(), transition_storyboards::remove()); // seen out first
        note_child_deleted();
    }
    else
    {
        // Destroyed at the next layout, tick or pointer pass rather than here: the element may be
        // the one whose handler caused this (a row's own Remove button, by pointer or by key).
        set_parent_of(*element, nullptr); // so a tick walking the children skips it
        graveyard_.push_back(std::move(element));
    }
}

void ForEachControl::clear_all()
{
    while (!realized_.empty())
    {
        clear_element(realized_.begin()->first);
    }
}

void ForEachControl::flush_graveyard()
{
    if (dispatching_ == 0)
    {
        graveyard_.clear();
    }
}

Size ForEachControl::measure_element(Element& element, Size available)
{
    Entry* entry = entry_of(element);
    if (measurer_ == nullptr || theme_ == nullptr)
    {
        return entry != nullptr ? entry->desired : Size{};
    }
    // A clean element measured with the same room under the same theme gives the same answer, which
    // lets a scroll re-run the layout without re-measuring everything realized.
    if (entry != nullptr && !element.is_dirty(DirtyFlags::Layout) && entry->measured_in == available &&
        entry->theme_generation == theme_->generation())
    {
        return entry->desired;
    }
    const Size desired = element.measure(available, *measurer_, *theme_);
    if (entry != nullptr)
    {
        entry->desired          = desired;
        entry->measured_in      = available;
        entry->theme_generation = theme_->generation();
    }
    return desired;
}

Element* ForEachControl::get_or_create_element(std::size_t index)
{
    if (index >= item_count_ || !item_template_)
    {
        return nullptr;
    }
    if (Element* existing = try_get_element(index); existing != nullptr && measured_)
    {
        return existing;
    }
    // Lay out again with the item as the anchor (the layout's recommended_anchor_index()), so it is realized where it
    // belongs whatever is in view.
    recommended_anchor_ = index;
    if (measured_ && measurer_ != nullptr)
    {
        (void)run_measure(last_available_);
        run_arrange();
        relayout_pending_ = true;
    }
    Element* element = try_get_element(index);
    if (element == nullptr)
    {
        element = &realize(index, ElementRealizationOptions::None);
    }
    recommended_anchor_.reset();
    return element;
}

// ---- layout ----

void ForEachControl::update_window()
{
    const Rect viewport = effective_viewport();
    if (is_unbounded(viewport))
    {
        visible_     = unbounded_rect();
        realization_ = unbounded_rect();
        return;
    }
    // Into the repeater's own coordinates, then the layout's (whose origin may be negative).
    const Rect local{viewport.x - bounds().x + layout_origin_.x,
                     viewport.y - bounds().y + layout_origin_.y,
                     viewport.width,
                     viewport.height};
    visible_          = local;
    const float buf_h = horizontal_cache_length_ * local.width / 2.0f;
    const float buf_v = vertical_cache_length_ * local.height / 2.0f;
    realization_      = Rect{local.x - buf_h, local.y - buf_v, local.width + 2.0f * buf_h, local.height + 2.0f * buf_v};
}

Size ForEachControl::run_measure(Size available)
{
    ++busy_;
    const Size desired = measure_pass(available);
    --busy_;
    replay_deferred_changes();
    return desired;
}

Size ForEachControl::measure_pass(Size available)
{
    update_window();
    for (auto& [index, entry] : realized_)
    {
        entry.used = false;
    }
    Size desired{};
    if (item_template_ && item_count_ > 0)
    {
        const Size room = available;
        if (auto* virtualizing = dynamic_cast<VirtualizingLayout*>(layout_.get()))
        {
            RepeaterLayoutContext context(*this);
            if (!layout_initialized_)
            {
                virtualizing->initialize_for_context(context);
                layout_initialized_ = true;
            }
            desired = virtualizing->measure(context, room);
        }
        else if (auto* nonvirtual = dynamic_cast<NonVirtualizingLayout*>(layout_.get()))
        {
            RepeaterNonVirtualContext context(*this);
            layout_origin_ = Point{};
            desired        = nonvirtual->measure(context, room);
        }
    }
    // Whatever the layout did not ask for goes, unless it keeps it itself or it has focus.
    std::vector<std::size_t> unused;
    for (auto& [index, entry] : realized_)
    {
        if (!entry.used && !entry.suppressed && !entry.element->has_focus_within())
        {
            unused.push_back(index);
        }
    }
    for (std::size_t index : unused)
    {
        clear_element(index);
    }
    desired.width         = std::isfinite(desired.width) ? std::max(0.0f, desired.width) : 0.0f;
    desired.height        = std::isfinite(desired.height) ? std::max(0.0f, desired.height) : 0.0f;
    extent_               = Rect{layout_origin_.x, layout_origin_.y, desired.width, desired.height};
    measured_realization_ = realization_;
    last_available_       = available;
    last_desired_         = desired;
    measured_             = true;
    return desired;
}

Size ForEachControl::measure_override(Size available, const MeasureContext& context) const
{
    measurer_ = &context.measurer;
    theme_    = &context.theme;
    return const_cast<ForEachControl*>(this)->run_measure(available);
}

void ForEachControl::run_arrange()
{
    for (auto& [index, entry] : realized_)
    {
        entry.arranged = false;
    }
    const Size final_size = bounds().size();
    if (auto* virtualizing = dynamic_cast<VirtualizingLayout*>(layout_.get()); virtualizing && item_template_)
    {
        RepeaterLayoutContext context(*this);
        (void)virtualizing->arrange(context, final_size);
    }
    else if (auto* nonvirtual = dynamic_cast<NonVirtualizingLayout*>(layout_.get()); nonvirtual && item_template_)
    {
        RepeaterNonVirtualContext context(*this);
        (void)nonvirtual->arrange(context, final_size);
    }
    // Elements realized but not placed (a pinned focused element) are parked out of sight.
    for (auto& [index, entry] : realized_)
    {
        if (!entry.arranged)
        {
            entry.element->arrange(Rect{Parked - entry.desired.width, Parked - entry.desired.height, 0.0f, 0.0f});
        }
    }
}

void ForEachControl::arrange_override(const Rect&, const ArrangeContext&)
{
    // A scroll arranges without measuring: when the viewport moved, the layout runs again from here
    // (a moved viewport invalidates the measure), and a size it changed asks
    // for a full pass next tick.
    if (measured_ && measurer_ != nullptr)
    {
        update_window();
        if (!same_rect(realization_, measured_realization_))
        {
            const Size before = last_desired_;
            (void)run_measure(last_available_);
            if (!(last_desired_ == before))
            {
                relayout_pending_ = true;
            }
        }
    }
    run_arrange();
    flush_graveyard();
}

// ---- render / input ----

void ForEachControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (const std::optional<Color> background = background_.resolve(theme); background && background->a > 0.0f)
    {
        renderer.fill_rounded_rect(bounds(), 0.0f, *background);
    }
    for (const auto& [index, entry] : realized_)
    {
        if (entry.arranged)
        {
            entry.element->render(renderer, context);
        }
    }
    if (ghosts_)
    {
        ghosts_->render(renderer, theme, bounds().origin());
    }
}

void ForEachControl::for_each_child(ChildVisitor visit) const
{
    for (const auto& [index, entry] : realized_)
    {
        visit(*entry.element);
    }
}

void ForEachControl::tick_override(Duration delta)
{
    replay_deferred_changes(); // a template run outside a measure pass (get_or_create_element())

    if (relayout_pending_)
    {
        relayout_pending_ = false;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    if (background_.advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    if (ghosts_ && ghosts_->advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    flush_graveyard();
}

void ForEachControl::automation_children(std::vector<Element*>& out) const
{
    for (const auto& [index, entry] : realized_)
    {
        if (entry.arranged)
        {
            out.push_back(entry.element.get());
        }
    }
}

namespace detail
{

void apply_collection_change(ui::ForEachControl& control, const ui::CollectionChange& change)
{
    switch (change.kind)
    {
        case ui::CollectionChangeKind::Inserted:
            control.notify_inserted(change.index, change.count);
            break;
        case ui::CollectionChangeKind::Removed:
            control.notify_removed(change.index, change.count);
            break;
        case ui::CollectionChangeKind::Replaced:
            control.notify_replaced(change.index, change.count);
            break;
        case ui::CollectionChangeKind::Moved:
            control.notify_moved(change.index, change.to);
            break;
        case ui::CollectionChangeKind::Reset:
            control.set_item_count(change.size);
            break;
    }
}

} // namespace detail

} // namespace ui
