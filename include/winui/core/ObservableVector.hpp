// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Property.hpp"

#include <algorithm>
#include <concepts>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <source_location>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cassert>
#include <cstddef>

namespace ui
{

/// What happened to a collection, as reported by ui::ObservableVector.
enum class CollectionChangeKind
{
    Inserted, ///< `count` items were inserted at `index`.
    Removed,  ///< `count` items were removed, starting at `index`.
    Replaced, ///< `count` items starting at `index` were replaced by new ones.
    Moved,    ///< The item at `index` moved to `to`; both are positions in the collection.
    Reset,    ///< Everything changed at once: the collection was cleared or assigned.
};

/// One change of a ui::ObservableVector, delivered to its observers after the collection has
/// already changed.
struct CollectionChange
{
    CollectionChangeKind kind  = CollectionChangeKind::Reset; ///< What happened.
    std::size_t          index = 0;                           ///< Where it happened; unused for a Reset.
    std::size_t          count = 0;                           ///< How many items it covers; 1 for a move.
    std::size_t          to    = 0;                           ///< For a move, the item's new position.
    std::size_t          size  = 0;                           ///< The collection's size after the change.
};

/// A collection that reports each edit: what was inserted, removed, replaced or moved, and where.
///
/// It is the collection counterpart of ui::Property. A Property holding a `std::vector` can only
/// say "changed"; an ObservableVector says what changed, so a ui::ForEach or a list view bound to
/// it builds one row for an inserted item, destroys one for a removed item and leaves every other
/// row, and its state, alone.
///
/// Each item lives at its own address, which stays the same for as long as the item is in the
/// collection, whatever is inserted or removed around it. A row may therefore keep a reference to
/// its item and bind to the item's own Property members. Positions do shift, so a row should not
/// keep its index; index_of() finds an item's current position.
///
/// Observers are held the same way a Property holds them: the collection or the observer may be
/// destroyed first. Destroying the collection reports a Reset to empty, so a tree still showing
/// it shows nothing afterwards. Non-copyable and non-movable; UI-thread only.
///
/// Example:
/// @code
/// struct Todo
/// {
///     explicit Todo(std::string text) : title{std::move(text)} {}
///     ui::Property<std::string> title;
///     ui::Property<bool>        done{false};
/// };
/// ui::ObservableVector<Todo> todos;
///
/// window.set_content(ui::VerticalStack{.content = {
///     ui::Button{.text = "Add", .on_click = [&] { todos.emplace_back("New task"); }},
///     ui::ForEach<Todo>{
///         .items = todos,
///         .item  = [&todos](Todo& todo) -> ui::Spec
///         {
///             return ui::HorizontalStack{.spacing = 8, .content = {
///                 ui::ToggleSwitch{.checked = todo.done},
///                 ui::Text{.text = todo.title},
///                 ui::Button{.text = "Remove", .on_click = [&todos, &todo] { todos.remove(todo); }},
///             }};
///         },
///     },
/// }});
/// @endcode
template <typename T>
class ObservableVector final : public PropertyBase
{
    using Storage = std::vector<std::unique_ptr<T>>;

    template <bool Const>
    class Iterator
    {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = T;
        using difference_type   = std::ptrdiff_t;
        using reference         = std::conditional_t<Const, const T&, T&>;
        using pointer           = std::conditional_t<Const, const T*, T*>;

        Iterator() = default;
        explicit Iterator(typename Storage::const_iterator at) : at_(std::move(at))
        {
        }
        /// An iterator converts to a const iterator.
        template <bool Other>
            requires(Const && !Other)
        Iterator(Iterator<Other> other) : at_(other.at_) // NOLINT(google-explicit-constructor)
        {
        }

        [[nodiscard]] reference operator*() const
        {
            return **at_;
        }
        [[nodiscard]] pointer operator->() const
        {
            return at_->get();
        }
        [[nodiscard]] reference operator[](difference_type n) const
        {
            return *at_[n];
        }
        Iterator& operator++()
        {
            ++at_;
            return *this;
        }
        Iterator operator++(int)
        {
            Iterator before = *this;
            ++at_;
            return before;
        }
        Iterator& operator--()
        {
            --at_;
            return *this;
        }
        Iterator operator--(int)
        {
            Iterator before = *this;
            --at_;
            return before;
        }
        Iterator& operator+=(difference_type n)
        {
            at_ += n;
            return *this;
        }
        Iterator& operator-=(difference_type n)
        {
            at_ -= n;
            return *this;
        }
        [[nodiscard]] friend Iterator operator+(Iterator it, difference_type n)
        {
            return it += n;
        }
        [[nodiscard]] friend Iterator operator+(difference_type n, Iterator it)
        {
            return it += n;
        }
        [[nodiscard]] friend Iterator operator-(Iterator it, difference_type n)
        {
            return it -= n;
        }
        [[nodiscard]] friend difference_type operator-(const Iterator& a, const Iterator& b)
        {
            return a.at_ - b.at_;
        }
        [[nodiscard]] friend bool operator==(const Iterator& a, const Iterator& b)
        {
            return a.at_ == b.at_;
        }
        [[nodiscard]] friend auto operator<=>(const Iterator& a, const Iterator& b)
        {
            return a.at_ <=> b.at_;
        }

    private:
        template <bool>
        friend class Iterator;

        typename Storage::const_iterator at_{};
    };

public:
    /// The item type.
    using value_type = T;
    /// Iterates the items as `T&`, in order.
    using iterator = Iterator<false>;
    /// Iterates the items as `const T&`, in order.
    using const_iterator = Iterator<true>;

    /// An empty collection.
    ObservableVector() = default;
    /// Holds a copy of each of `items`; no observer exists yet, so nothing is notified.
    ObservableVector(std::initializer_list<T> items)
        requires std::copy_constructible<T>
    {
        items_.reserve(items.size());
        for (const T& item : items)
        {
            items_.push_back(std::make_unique<T>(item));
        }
        count_.set(items_.size());
    }
    /// Reports a Reset to empty, then leaves every remaining Subscription dead.
    ~ObservableVector()
    {
        clear();
    }

    // ---- reading ----
    /// How many items the collection holds.
    [[nodiscard]] std::size_t size() const noexcept
    {
        return items_.size();
    }
    /// True when the collection holds no items.
    [[nodiscard]] bool empty() const noexcept
    {
        return items_.empty();
    }
    /// How many items the collection holds, as a Property, so a binding follows it: an empty-state
    /// text that shows while there is nothing, a count in a heading. It changes after the
    /// collection's own observers were told.
    ///
    /// Example:
    /// @code
    /// ui::ObservableVector<std::string> files;
    /// window.set_content(ui::Text{
    ///     .text    = "No files yet",
    ///     .visible = ui::bind(files.count(), [](std::size_t n) { return n == 0; }),
    /// });
    /// @endcode
    [[nodiscard]] const Property<std::size_t>& count() const noexcept
    {
        return count_;
    }
    /// The item at `index`, which must be less than size(). The reference stays valid until that
    /// item is removed or replaced.
    [[nodiscard]] T& operator[](std::size_t index)
    {
        assert(index < items_.size());
        return *items_[index];
    }
    /// The item at `index`, which must be less than size().
    [[nodiscard]] const T& operator[](std::size_t index) const
    {
        assert(index < items_.size());
        return *items_[index];
    }
    /// The first item; the collection must not be empty.
    [[nodiscard]] T& front()
    {
        assert(!items_.empty());
        return *items_.front();
    }
    /// The first item; the collection must not be empty.
    [[nodiscard]] const T& front() const
    {
        assert(!items_.empty());
        return *items_.front();
    }
    /// The last item; the collection must not be empty.
    [[nodiscard]] T& back()
    {
        assert(!items_.empty());
        return *items_.back();
    }
    /// The last item; the collection must not be empty.
    [[nodiscard]] const T& back() const
    {
        assert(!items_.empty());
        return *items_.back();
    }
    /// The position of `item`, found by its address; unset when it is not an item of this
    /// collection.
    [[nodiscard]] std::optional<std::size_t> index_of(const T& item) const
    {
        const auto it = std::find_if(items_.begin(),
                                     items_.end(),
                                     [&item](const std::unique_ptr<T>& held) { return held.get() == &item; });
        if (it == items_.end())
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(it - items_.begin());
    }

    /// The first item, for iterating as `T&`.
    [[nodiscard]] iterator begin() noexcept
    {
        return iterator{items_.begin()};
    }
    /// One past the last item.
    [[nodiscard]] iterator end() noexcept
    {
        return iterator{items_.end()};
    }
    /// The first item, for iterating as `const T&`.
    [[nodiscard]] const_iterator begin() const noexcept
    {
        return const_iterator{items_.begin()};
    }
    /// One past the last item.
    [[nodiscard]] const_iterator end() const noexcept
    {
        return const_iterator{items_.end()};
    }

    // ---- editing: each call notifies the observers once, synchronously, before it returns ----
    // The trailing `where` of the editing members is the caller's line, which a thread check
    // names; leave it defaulted.

    /// Appends `item` and returns it in its place.
    T& push_back(T item, std::source_location where = std::source_location::current())
        requires std::move_constructible<T>
    {
        return emplace_at("ui::ObservableVector::push_back", where, items_.size(), std::move(item));
    }
    /// Appends an item constructed in place from `args`, for an item type that cannot be moved
    /// (one holding a Property), and returns it.
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    T& emplace_back(Args&&... args)
    {
        return emplace(items_.size(), std::forward<Args>(args)...);
    }
    /// Inserts `item` before position `index` (clamped to the end) and returns it in its place.
    T& insert(std::size_t index, T item, std::source_location where = std::source_location::current())
        requires std::move_constructible<T>
    {
        return emplace_at("ui::ObservableVector::insert", where, index, std::move(item));
    }
    /// Inserts an item constructed in place from `args` before position `index` (clamped to the
    /// end) and returns it.
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    T& emplace(std::size_t index, Args&&... args)
    {
        return emplace_at("ui::ObservableVector::emplace", std::source_location::current(), index, std::forward<Args>(args)...);
    }

    /// Removes `count` items starting at `index`; a range reaching past the end is cut short.
    /// The items are destroyed after the observers have been told.
    void erase(std::size_t index, std::size_t count = 1, std::source_location where = std::source_location::current())
    {
        if (index >= items_.size() || count == 0)
        {
            return;
        }
        count            = (std::min)(count, items_.size() - index);
        const auto first = items_.begin() + static_cast<std::ptrdiff_t>(index);
        const auto last  = first + static_cast<std::ptrdiff_t>(count);
        // Kept alive until every observer has dropped what it built for them.
        Storage doomed{std::make_move_iterator(first), std::make_move_iterator(last)};
        items_.erase(first, last);
        notify({CollectionChangeKind::Removed, index, count, 0, items_.size()}, "ui::ObservableVector::erase", where);
        retire(doomed);
    }
    /// Removes `item`, found by its address. False when it is not an item of this collection.
    bool remove(const T& item, std::source_location where = std::source_location::current())
    {
        const std::optional<std::size_t> index = index_of(item);
        if (!index)
        {
            return false;
        }
        erase(*index, 1, where);
        return true;
    }
    /// Removes every item for which `predicate` answers true, and returns how many went. Each
    /// run of neighboring matches is reported as one Removed, from the back of the collection
    /// to the front. `predicate` is asked about every item before the first one goes.
    template <typename Predicate>
        requires std::predicate<Predicate&, const T&>
    std::size_t erase_if(Predicate predicate, std::source_location where = std::source_location::current())
    {
        std::vector<const T*> doomed;
        for (const std::unique_ptr<T>& item : items_)
        {
            if (predicate(static_cast<const T&>(*item)))
            {
                doomed.push_back(item.get());
            }
        }
        // Found again by address for each run, because an observer may edit the collection
        // while it is told about the one before.
        std::size_t removed = 0;
        for (std::size_t last = doomed.size(); last > 0;)
        {
            const std::optional<std::size_t> found = index_of(*doomed[last - 1]);
            --last;
            if (!found)
            {
                continue;
            }
            const std::size_t end   = *found;
            std::size_t       first = end;
            while (last > 0 && first > 0 && items_[first - 1].get() == doomed[last - 1])
            {
                --first;
                --last;
            }
            const std::size_t count = end - first + 1;
            erase(first, count, where);
            removed += count;
        }
        return removed;
    }
    /// Replaces the item at `index` with `item`; whatever was built for the old one is built
    /// again. For an item that holds Properties, set those instead: nothing needs rebuilding.
    T& replace(std::size_t index, T item, std::source_location where = std::source_location::current())
        requires std::move_constructible<T>
    {
        assert(index < items_.size());
        std::unique_ptr<T> old   = std::exchange(items_[index], std::make_unique<T>(std::move(item)));
        T&                 fresh = *items_[index];
        notify({CollectionChangeKind::Replaced, index, 1, 0, items_.size()}, "ui::ObservableVector::replace", where);
        Storage doomed;
        doomed.push_back(std::move(old));
        retire(doomed);
        return fresh;
    }
    /// Moves the item at `from` to position `to`, both less than size(). The item keeps its
    /// address, and what was built for it stays as it is.
    void move(std::size_t from, std::size_t to, std::source_location where = std::source_location::current())
    {
        assert(from < items_.size() && to < items_.size());
        if (from == to || from >= items_.size() || to >= items_.size())
        {
            return;
        }
        const auto begin = items_.begin();
        if (from < to)
        {
            std::rotate(begin + static_cast<std::ptrdiff_t>(from),
                        begin + static_cast<std::ptrdiff_t>(from) + 1,
                        begin + static_cast<std::ptrdiff_t>(to) + 1);
        }
        else
        {
            std::rotate(begin + static_cast<std::ptrdiff_t>(to),
                        begin + static_cast<std::ptrdiff_t>(from),
                        begin + static_cast<std::ptrdiff_t>(from) + 1);
        }
        notify({CollectionChangeKind::Moved, from, 1, to, items_.size()}, "ui::ObservableVector::move", where);
    }
    /// Removes every item, reported as one Reset.
    void clear(std::source_location where = std::source_location::current())
    {
        if (items_.empty())
        {
            return;
        }
        Storage doomed = std::exchange(items_, {});
        notify({CollectionChangeKind::Reset, 0, 0, 0, 0}, "ui::ObservableVector::clear", where);
        retire(doomed);
    }
    /// Replaces the whole content with a copy of each of `items`, reported as one Reset.
    /// Nothing is reported when the collection was empty and stays empty.
    template <typename Range>
        requires requires(const Range& range) {
            { *std::begin(range) } -> std::convertible_to<T>;
        }
    void assign(const Range& items, std::source_location where = std::source_location::current())
    {
        Storage next;
        for (const auto& item : items)
        {
            next.push_back(std::make_unique<T>(item));
        }
        if (next.empty() && items_.empty())
        {
            return;
        }
        Storage doomed = std::exchange(items_, std::move(next));
        notify({CollectionChangeKind::Reset, 0, 0, 0, items_.size()}, "ui::ObservableVector::assign", where);
        retire(doomed);
    }
    /// Replaces the whole content with a copy of each of `items`, reported as one Reset.
    void assign(std::initializer_list<T> items, std::source_location where = std::source_location::current())
        requires std::copy_constructible<T>
    {
        assign<std::initializer_list<T>>(items, where);
    }

    /// Runs `edits`, which edit this collection, and reports everything they did as one Reset when
    /// they return, instead of one change per edit. For a bulk edit (an undo step, a sort, a
    /// reload) whose rows are cheaper to build again once than to follow edit by edit. A batch
    /// inside a batch reports once, when the outermost returns. Items removed or replaced during
    /// the batch are destroyed after the Reset was reported, so nothing built for them dangles.
    /// Nothing is reported when `edits` changed nothing.
    ///
    /// Example:
    /// @code
    /// ui::ObservableVector<std::string> pages{"Intro", "Setup", "Usage", "Cover"};
    /// pages.batch([&] {
    ///     std::string moved = pages[3];
    ///     pages.erase(3);
    ///     pages.insert(0, std::move(moved));
    /// });
    /// @endcode
    template <typename Edits>
        requires std::invocable<Edits&>
    void batch(Edits edits, std::source_location where = std::source_location::current())
    {
        struct Scope
        {
            ObservableVector&           owner;
            const std::source_location& where;
            ~Scope()
            {
                if (--owner.batch_depth_ == 0)
                {
                    owner.end_batch(where);
                }
            }
        };
        ++batch_depth_;
        const Scope scope{*this, where};
        edits();
    }

    // ---- observing ----
    /// Observe changes. The returned Subscription must be kept alive for `fn` to keep firing; an
    /// Element keeps it with Element::add_subscription(). `fn` runs after the collection has
    /// changed, and is not called for the current content.
    [[nodiscard]] Subscription observe(Callback<void(const CollectionChange&)> fn) const
    {
        return attach([handler = std::move(fn)](const void* change) mutable
                      { handler(*static_cast<const CollectionChange*>(change)); });
    }

private:
    template <typename... Args>
    T& emplace_at(std::string_view call, const std::source_location& where, std::size_t index, Args&&... args)
    {
        index     = (std::min)(index, items_.size());
        auto at   = items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(index),
                                  std::make_unique<T>(std::forward<Args>(args)...));
        T&   item = **at;
        // The reference outlives the notification unless an observer removes the item again.
        notify({CollectionChangeKind::Inserted, index, 1, 0, items_.size()}, call, where);
        return item;
    }

    // `where` is the caller's line, for the thread check (Property.hpp's check_write(); after the
    // edit, which is close enough for a report).
    void notify(const CollectionChange& change, std::string_view call, const std::source_location& where)
    {
#if LIBWINUI_THREAD_CHECKS
        check_write(call, where);
#else
        (void)call;
        (void)where;
#endif
        if (batch_depth_ > 0)
        {
            batch_changed_ = true; // end_batch() reports it
            return;
        }
        dispatch(&change);
        count_.set(items_.size());
    }

    // Items an edit took out: destroyed now, or once the batch they were taken out in reported.
    void retire(Storage& doomed)
    {
        if (batch_depth_ == 0)
        {
            return; // `doomed` goes when the caller returns
        }
        for (std::unique_ptr<T>& item : doomed)
        {
            retired_.push_back(std::move(item));
        }
    }

    void end_batch(const std::source_location& where)
    {
        if (std::exchange(batch_changed_, false))
        {
            notify({CollectionChangeKind::Reset, 0, 0, 0, items_.size()}, "ui::ObservableVector::batch", where);
        }
        Storage gone = std::exchange(retired_, {});
    }

    Storage               items_;
    Property<std::size_t> count_{0};
    Storage               retired_; // taken out during a batch, see retire()
    int                   batch_depth_   = 0;
    bool                  batch_changed_ = false;
};

/// Binding of a collection to an `.items` field: the field follows every insert, removal,
/// replacement and move, row by row. ui::ForEach then passes each item to `.item` as `T&`.
///
/// A field that reads the collection in place (ui::ForEach, the views) never copies it. On a field
/// that only takes a whole vector (a shape's `.points`), the binding delivers a copy of the
/// collection when the field is built and on every change, which needs a copyable item type.
template <typename T>
[[nodiscard]] Binding<std::vector<T>> bind(ObservableVector<T>& source)
{
    using Items = std::vector<T>;
    if constexpr (std::copy_constructible<T>)
    {
        return Binding<Items>{&source,
                              [](const ObservableVector<T>& from, Items& into) { into.assign(from.begin(), from.end()); },
                              [&source](Callback<void(Items)> sink, Items* now)
                              {
                                  if (now != nullptr)
                                  {
                                      now->assign(source.begin(), source.end());
                                  }
                                  return sink ? source.observe([&source, sink = std::move(sink)](const CollectionChange&) mutable
                                                               { sink(Items(source.begin(), source.end())); })
                                              : Subscription{};
                              }};
    }
    else
    {
        return Binding<Items>{&source, nullptr, {}};
    }
}
/// Binding of a collection the caller may only read. The field gets a copy of the whole
/// collection when it is built and again after every change, as from a `Property<std::vector<T>>`;
/// it never writes to the collection, so a view built from it does not reorder it by drag.
template <typename T>
    requires std::copy_constructible<T>
[[nodiscard]] Binding<std::vector<T>> bind(const ObservableVector<T>& source)
{
    using Items = std::vector<T>;
    return Binding<Items>{Items(source.begin(), source.end()),
                          [&source](Callback<void(Items)> sink, Items* now)
                          {
                              if (now != nullptr)
                              {
                                  now->assign(source.begin(), source.end());
                              }
                              return sink ? source.observe([&source, sink = std::move(sink)](const CollectionChange&) mutable
                                                           { sink(Items(source.begin(), source.end())); })
                                          : Subscription{};
                          }};
}
/// Deleted: a binding keeps a reference to its source, so a temporary collection cannot be bound.
template <typename T>
Binding<std::vector<T>> bind(ObservableVector<T>&& source) = delete;
/// Deleted: a binding keeps a reference to its source, so a temporary collection cannot be bound.
template <typename T>
Binding<std::vector<T>> bind(const ObservableVector<T>&& source) = delete;

/// One-way binding of a list made from one Property of each item: the values of `member` in
/// collection order. It follows both the collection (an item added, removed or moved) and each
/// item's Property, so a list of choices whose texts the user edits stays a plain
/// `std::vector` for a control's `.items`.
///
/// Example:
/// @code
/// struct Choice
/// {
///     ui::Property<std::string> text;
/// };
/// ui::ObservableVector<Choice> choices;
/// choices.emplace_back().text = "Small";
/// window.set_content(ui::ComboBox{.items = ui::bind_each(choices, &Choice::text)});
/// choices.emplace_back().text = "Large"; // the drop-down lists both
/// choices[0].text             = "Tiny";  // and follows the edit
/// @endcode
template <typename T, typename U>
    requires std::copy_constructible<U>
[[nodiscard]] Binding<std::vector<U>> bind_each(const ObservableVector<T>& items, Property<U> T::* member)
{
    using Values      = std::vector<U>;
    const auto values = [&items, member]
    {
        Values out;
        out.reserve(items.size());
        for (const T& item : items)
        {
            out.push_back((item.*member).get());
        }
        return out;
    };
    return Binding<Values>{values(),
                           [&items, member, values](Callback<void(Values)> sink, Values* now)
                           {
                               if (now != nullptr)
                               {
                                   *now = values();
                               }
                               if (!sink)
                               {
                                   return Subscription{};
                               }
                               // The collection's edge owns the state, the items' edges with it, so
                               // dropping the returned subscription drops them all.
                               struct State
                               {
                                   Callback<void(Values)>    sink;
                                   std::vector<Subscription> each;
                               };
                               auto       state  = std::make_shared<State>(State{.sink = std::move(sink), .each = {}});
                               State*     raw    = state.get();
                               const auto follow = [&items, member, values, raw]
                               {
                                   raw->each.clear();
                                   for (const T& item : items)
                                   {
                                       raw->each.push_back(
                                           (item.*member).observe([raw, values](const U&) { raw->sink(values()); }));
                                   }
                               };
                               follow();
                               return items.observe(
                                   [state, follow, values](const CollectionChange&) mutable
                                   {
                                       follow();
                                       state->sink(values());
                                   });
                           }};
}
/// Deleted: the binding keeps a reference to the collection, so a temporary cannot be bound.
template <typename T, typename U>
Binding<std::vector<U>> bind_each(const ObservableVector<T>&& items, Property<U> T::* member) = delete;

/// How many items an index-based control shows, and the changes that lead from one count to the
/// next. The typed views hand one to their controls, and an app uses one to drive a control by
/// hand. It is either a plain number or ui::item_count() of a ui::ObservableVector or a
/// `ui::Property<std::vector<T>>`. The second keeps the view in step with the collection, so no
/// change has to be announced by hand.
///
/// Example:
/// @code
/// ui::ObservableVector<std::string> names{"Ada", "Grace"};
///
/// auto list = std::make_unique<ui::VirtualListControl>();
/// list->set_item_factory([&names](std::size_t i) { return std::make_unique<ui::TextControl>(names[i]); });
///
/// ui::ItemCount count = ui::item_count(names);
/// list->set_item_count(count.initial());
/// list->add_subscription(count.subscribe(
///     [raw = list.get()](const ui::CollectionChange& change) { raw->set_item_count(change.size); }));
///
/// names.push_back("Edsger"); // the list shows a third row
/// @endcode
class ItemCount
{
public:
    /// Subscribes a sink to the collection's changes.
    using Attach = Callback<Subscription(Callback<void(const CollectionChange&)>)>;

    /// No items.
    ItemCount() = default;
    /// A fixed number of items; a later change goes through the control's set_item_count(). A
    /// negative number is no items.
    template <std::integral N>
        requires(!std::same_as<N, bool>)
    ItemCount(N count) // NOLINT(google-explicit-constructor)
        :
        count_(std::cmp_less(count, 0) ? std::size_t{0} : static_cast<std::size_t>(count))
    {
    }
    /// The live form, made by ui::item_count().
    ItemCount(std::size_t count, Attach attach) : count_(count), attach_(std::move(attach))
    {
    }

    /// The number of items when the spec is built.
    [[nodiscard]] std::size_t initial() const noexcept
    {
        return count_;
    }
    /// True when the count follows a collection.
    [[nodiscard]] bool reactive() const noexcept
    {
        return static_cast<bool>(attach_);
    }
    /// Starts following the collection; only valid when reactive().
    [[nodiscard]] Subscription subscribe(Callback<void(const CollectionChange&)> sink)
    {
        assert(attach_ && "ItemCount::subscribe() needs a reactive() count");
        return attach_ ? attach_(std::move(sink)) : Subscription{};
    }

private:
    std::size_t count_ = 0;
    Attach      attach_{};
};

/// The size of `source`, kept current: what an index-based collection spec's `.item_count` takes
/// to follow a ui::ObservableVector. See ui::ItemCount.
template <typename T>
[[nodiscard]] ItemCount item_count(const ObservableVector<T>& source)
{
    return ItemCount{source.size(),
                     [&source](Callback<void(const CollectionChange&)> sink) { return source.observe(std::move(sink)); }};
}
/// Deleted: the count keeps a reference to its source, so a temporary collection cannot be used.
template <typename T>
ItemCount item_count(const ObservableVector<T>&& source) = delete;

/// What makes two items of a collection "the same item" when the collection is assigned as a
/// whole: the type of ui::ForEach's `.key` field, and what ui::item_count() takes for a view.
///
/// Made from a pointer to a data member (`.key = &Photo::id`) or from any callable returning the
/// key (`.key = [](const Photo& p) { return p.path; }`). The key type needs `==` and `std::hash`.
/// Keys must be unique within the collection.
template <typename T>
class ItemKey
{
public:
    /// No key: items are matched by position and value.
    ItemKey() = default;

    /// A key read from each item by `key`: a member pointer or a callable.
    template <typename F>
        requires std::invocable<const F&, const T&> && std::copy_constructible<F> &&
                     std::equality_comparable<std::remove_cvref_t<std::invoke_result_t<const F&, const T&>>> &&
                     requires(const std::remove_cvref_t<std::invoke_result_t<const F&, const T&>>& k) {
                         std::hash<std::remove_cvref_t<std::invoke_result_t<const F&, const T&>>>{}(k);
                     }
    ItemKey(F key) : // NOLINT(google-explicit-constructor)
        hash_(
            [key](const T& item)
            {
                using KeyType = std::remove_cvref_t<std::invoke_result_t<const F&, const T&>>;
                return std::hash<KeyType>{}(std::invoke(key, item));
            }),
        same_([key](const T& a, const T& b) { return std::invoke(key, a) == std::invoke(key, b); })
    {
    }

    /// True when a key is set.
    [[nodiscard]] explicit operator bool() const
    {
        return static_cast<bool>(hash_);
    }

    /// Matches `next` against `old` by key. For each item of `next`, `source` receives the
    /// position of the item of `old` with the same key, unset for a new one, and `changed`
    /// whether a matched item differs in value. False when `old` holds a key twice, or when two
    /// items of `next` match the same item of `old`. A key repeated only among the new items of
    /// `next` is not detected.
    [[nodiscard]] bool match(const std::vector<T>&                    old,
                             const std::vector<T>&                    next,
                             std::vector<std::optional<std::size_t>>& source,
                             std::vector<bool>&                       changed)
    {
        std::unordered_multimap<std::size_t, std::size_t> by_hash;
        by_hash.reserve(old.size());
        for (std::size_t i = 0; i < old.size(); ++i)
        {
            const std::size_t hash   = hash_(old[i]);
            const auto [first, last] = by_hash.equal_range(hash);
            for (auto it = first; it != last; ++it)
            {
                if (same_(old[it->second], old[i]))
                {
                    return false;
                }
            }
            by_hash.emplace(hash, i);
        }

        std::vector<bool> taken(old.size(), false);
        source.assign(next.size(), std::nullopt);
        changed = std::vector<bool>(next.size(), false);
        for (std::size_t i = 0; i < next.size(); ++i)
        {
            const auto [first, last] = by_hash.equal_range(hash_(next[i]));
            for (auto it = first; it != last; ++it)
            {
                if (!same_(old[it->second], next[i]))
                {
                    continue;
                }
                if (taken[it->second])
                {
                    return false;
                }
                taken[it->second] = true;
                source[i]         = it->second;
                if constexpr (std::equality_comparable<T>)
                {
                    changed[i] = !(old[it->second] == next[i]);
                }
                else
                {
                    changed[i] = true;
                }
                break;
            }
        }
        return true;
    }

private:
    Callback<std::size_t(const T&)>    hash_{};
    Callback<bool(const T&, const T&)> same_{};
};

namespace detail
{
// Reports how a collection of `old_size` items became the one described by `source` (per new
// item, its old position or NoIndex) and `changed` (per new item, whether what was built
// for it has to be built again): removals, then the fewest moves that put the surviving items in
// their new order, then insertions and replacements. A reorder too large to follow that way is
// reported as a reset.
void keyed_changes(std::size_t                             old_size,
                   std::span<const std::size_t>            source,
                   const std::vector<bool>&                changed,
                   Callback<void(const CollectionChange&)> sink);

// Reports how `old` became `next`. With a key, items are matched by it, so a reorder is reported
// as moves. Without one, the common head and tail are kept and what lies between is reported as
// removed, inserted or replaced.
template <typename T>
void vector_changes(const std::vector<T>&                   old,
                    const std::vector<T>&                   next,
                    ItemKey<T>&                             key,
                    Callback<void(const CollectionChange&)> sink)
{
    if (key)
    {
        std::vector<std::optional<std::size_t>> matched;
        std::vector<bool>                       changed;
        const bool                              unique = key.match(old, next, matched, changed);
        assert(unique && "a key must be unique within its collection");
        if (unique)
        {
            std::vector<std::size_t> source(matched.size());
            std::ranges::transform(matched, source.begin(), index_or_none);
            keyed_changes(old.size(), source, changed, std::move(sink));
        }
        else
        {
            sink({CollectionChangeKind::Reset, 0, 0, 0, next.size()});
        }
        return;
    }
    if constexpr (std::equality_comparable<T>)
    {
        const std::size_t before = old.size();
        const std::size_t after  = next.size();
        std::size_t       head   = 0;
        while (head < before && head < after && old[head] == next[head])
        {
            ++head;
        }
        std::size_t tail = 0;
        while (tail < before - head && tail < after - head && old[before - 1 - tail] == next[after - 1 - tail])
        {
            ++tail;
        }
        const std::size_t removed  = before - head - tail;
        const std::size_t inserted = after - head - tail;
        if (removed == inserted)
        {
            if (removed != 0)
            {
                sink({CollectionChangeKind::Replaced, head, removed, 0, after});
            }
            return;
        }
        if (removed != 0)
        {
            sink({CollectionChangeKind::Removed, head, removed, 0, before - removed});
        }
        if (inserted != 0)
        {
            sink({CollectionChangeKind::Inserted, head, inserted, 0, after});
        }
    }
    else
    {
        sink({CollectionChangeKind::Reset, 0, 0, 0, next.size()});
    }
}

// What item_count() of a property subscribes: remembers the last vector and reports each new one
// as the changes that lead to it.
template <typename T, typename MakeKey>
[[nodiscard]] ItemCount count_of_vector(const Property<std::vector<T>>& source, MakeKey make_key)
{
    return ItemCount{source.get().size(),
                     [&source, make_key](Callback<void(const CollectionChange&)> sink)
                     {
                         return source.observe(
                             [previous = source.get(), identity = make_key(), sink = std::move(sink)](
                                 const std::vector<T>& next) mutable
                             {
                                 const std::vector<T> old = std::exchange(previous, next);
                                 vector_changes<T>(old,
                                                   previous,
                                                   identity,
                                                   [&sink](const CollectionChange& change) { sink(change); });
                             });
                     }};
}
} // namespace detail

/// The size of the vector in `source`, kept current, with each change of the property reported
/// as what happened to the items: what an index-based collection spec's `.item_count` takes to
/// follow a `ui::Property<std::vector<T>>` the app assigns as a whole. The typed views
/// (ui::ListView, ui::TileView, ui::TableView) do the same from `.items` and `.key`.
///
/// `key` says which item is which (a member pointer or a callable, see ui::ItemKey). An item whose
/// key is still there is reported as moved, not as removed and inserted, so its row stays with it
/// through a sort or a filter. Keys must be unique. The item template reads the property
/// by index.
///
/// Example:
/// @code
/// struct Contact
/// {
///     int         id = 0;
///     std::string name;
///     friend bool operator==(const Contact&, const Contact&) = default;
/// };
/// ui::Property<std::vector<Contact>> contacts{std::vector<Contact>{{1, "Grace"}, {2, "Ada"}}};
///
/// ui::ItemCount count = ui::item_count(contacts, &Contact::id);
/// ui::Subscription following = count.subscribe([&](const ui::CollectionChange& /*change*/) {
///     // Forward to an index-based control: notify_inserted(), notify_removed(), notify_moved() ...
/// });
///
/// std::vector<Contact> sorted = contacts.get();
/// std::ranges::sort(sorted, {}, &Contact::name);
/// contacts = std::move(sorted); // reported as moves
/// @endcode
template <typename T, typename K>
    requires std::constructible_from<ItemKey<T>, K> && std::copy_constructible<K>
[[nodiscard]] ItemCount item_count(const Property<std::vector<T>>& source, K key)
{
    return detail::count_of_vector(source, [key] { return ItemKey<T>{key}; });
}
/// As above without a key: the items before and after an edit are kept and those in between are
/// reported as removed, inserted or replaced. That covers appending and removing; a reorder
/// replaces nearly everything.
template <typename T>
[[nodiscard]] ItemCount item_count(const Property<std::vector<T>>& source)
{
    return detail::count_of_vector(source, [] { return ItemKey<T>{}; });
}
/// Deleted: the count keeps a reference to its source, so a temporary property cannot be used.
template <typename T, typename... K>
ItemCount item_count(const Property<std::vector<T>>&& source, K... key) = delete;

} // namespace ui
