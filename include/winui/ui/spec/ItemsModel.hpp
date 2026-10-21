// SPDX-License-Identifier: MIT
#pragma once

// What the views share: finished controls around a collection (ui::ListView, ui::TileView,
// ui::TableView), each typed on its item. `.items` is the collection, `.item` is
// how one item looks, and the handlers receive the item. The controls they build stay
// index-based; each view's header holds its index-based build (ui::detail) beside the typed spec,
// and what is here turns items into indices for them.

#include "winui/core/Callback.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"

#include <algorithm>
#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <cassert>
#include <cstddef>

namespace ui
{

/// An item template in a spec: the Spec for item `index`, which may pick a different look by the
/// item. Called only for the items a control creates rows for, and it may be called again for an
/// item later, so it indexes into a model the caller keeps alive for the life of the control.
using ItemTemplate = Callback<Spec(std::size_t index)>;

/// What a typed view's `on_selection_changed` receives: a copy of the selected item (the first one
/// when several are), or unset when nothing is selected. A copy is safe to keep. For an item type
/// that cannot be copied it is the selected index instead.
template <typename T>
using SelectedItem = std::conditional_t<std::copy_constructible<T>, std::optional<T>, std::optional<std::size_t>>;

namespace detail
{
// The collection behind a typed view, whichever of the three sources `.items` was given: an
// ObservableVector read in place, or a copy of a vector the view owns (a plain one, or the last
// value of a Property). Shared by the item template, the handlers and the change subscription of
// one built view, which all live and die with that view's node.
template <typename T>
struct ItemsState
{
    ObservableVector<T>*                    live = nullptr;
    std::vector<T>                          copy;
    typename Binding<std::vector<T>>::Write write;            // set for a two-way property
    bool                                    applying = false; // a reorder is being written back

    [[nodiscard]] std::size_t size() const
    {
        return live != nullptr ? live->size() : copy.size();
    }
    [[nodiscard]] T& at(std::size_t index)
    {
        return live != nullptr ? (*live)[index] : copy[index];
    }
    // The user reordered an item in the view: the collection follows. The view has already moved
    // its row, so the change this causes is not reported back to it.
    void move(std::size_t from, std::size_t to)
    {
        if (from >= size() || to >= size() || from == to)
        {
            return;
        }
        if (live != nullptr)
        {
            applying = true;
            live->move(from, to);
            applying = false;
            return;
        }
        if constexpr (std::movable<T>)
        {
            const auto begin = copy.begin();
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
            if (write)
            {
                write(copy); // the property now equals the copy, so the change it reports is empty
            }
        }
    }
};

template <typename T>
struct ItemsModel
{
    std::shared_ptr<ItemsState<T>> state;
    ItemCount                      count;    // what the index-based build follows
    bool                           reactive; // the source reports changes
};

// Consumes a view's `.items` and `.key`.
template <typename T>
[[nodiscard]] ItemsModel<T> items_model(Binding<std::vector<T>>& items, [[maybe_unused]] ItemKey<T>& key)
{
    auto state = std::make_shared<ItemsState<T>>();
    if (ObservableVector<T>* live = items.observable())
    {
        state->live = live;
        return ItemsModel<T>{state,
                             ItemCount{live->size(),
                                       [live, state](Callback<void(const CollectionChange&)> sink)
                                       {
                                           return live->observe(
                                               [state, sink = std::move(sink)](const CollectionChange& change) mutable
                                               {
                                                   if (!state->applying)
                                                   {
                                                       const BoundUpdate bound;
                                                       sink(change);
                                                   }
                                               });
                                       }},
                             true};
    }
    if constexpr (std::copy_constructible<T>)
    {
        state->copy = items.initial();
        if (items.reactive())
        {
            state->write              = items.take_writer();
            const std::size_t initial = state->copy.size();
            return ItemsModel<T>{state,
                                 ItemCount{initial,
                                           [source = std::move(items), identity = std::move(key), state](
                                               Callback<void(const CollectionChange&)> sink) mutable
                                           {
                                               return source.subscribe(
                                                   [state, identity = std::move(identity), sink = std::move(sink)](
                                                       std::vector<T> next) mutable
                                                   {
                                                       const BoundUpdate bound;
                                                       const std::vector<T> old = std::exchange(state->copy, std::move(next));
                                                       vector_changes<T>(old,
                                                                         state->copy,
                                                                         identity,
                                                                         [&sink](const CollectionChange& change)
                                                                         { sink(change); });
                                                   });
                                           }},
                                 true};
        }
    }
    return ItemsModel<T>{state, ItemCount{state->size()}, false};
}

// An item whose text is all there is to show: what a view without `.item` falls back to.
template <typename T>
concept TextItem = std::convertible_to<const T&, std::string_view>;

// The view's `.item` as the index-based template the control's build takes.
template <typename T>
[[nodiscard]] ItemTemplate indexed_template(std::shared_ptr<ItemsState<T>> state, Callback<Spec(T&)> item)
{
    if (!item)
    {
        if constexpr (TextItem<T>)
        {
            item = [](T& value) -> Spec { return Text{.text = std::string(std::string_view(value))}; };
        }
        else
        {
            // Nothing to show an item by. Asserted only once an item is asked for, since a view
            // may be built empty and handed its template through the control later.
            return [](std::size_t) -> Spec
            {
                assert(false && "a view of items that are not text needs an `.item` template");
                return {};
            };
        }
    }
    return [state = std::move(state), factory = std::move(item)](std::size_t index) mutable -> Spec
    {
        if (index >= state->size())
        {
            return {};
        }
        return factory(state->at(index));
    };
}

// A handler taking the item, as the handler taking its index that the control calls.
template <typename T>
[[nodiscard]] Callback<void(std::size_t)> indexed_handler(std::shared_ptr<ItemsState<T>> state, Callback<void(const T&)> handler)
{
    if (!handler)
    {
        return {};
    }
    return [state = std::move(state), handler = std::move(handler)](std::size_t index) mutable
    {
        if (index < state->size())
        {
            handler(state->at(index));
        }
    };
}

// `group_by` over every item of `state`, in item order, asked again on every collection change;
// empty when `group_by` is.
template <typename T>
[[nodiscard]] Callback<std::vector<std::string>()> indexed_keys(std::shared_ptr<ItemsState<T>>  state,
                                                                Callback<std::string(const T&)> group_by)
{
    if (!group_by)
    {
        return {};
    }
    return [state = std::move(state), group_by = std::move(group_by)]() mutable
    {
        std::vector<std::string> keys;
        keys.reserve(state->size());
        for (std::size_t i = 0; i < state->size(); ++i)
        {
            keys.push_back(group_by(state->at(i)));
        }
        return keys;
    };
}

// As above for a callable with a result: R{} for an index past the end.
template <typename T, typename R>
[[nodiscard]] Callback<R(std::size_t)> indexed_function(std::shared_ptr<ItemsState<T>> state, Callback<R(const T&)> fn)
{
    if (!fn)
    {
        return {};
    }
    return [state = std::move(state), fn = std::move(fn)](std::size_t index) mutable -> R
    { return index < state->size() ? fn(state->at(index)) : R{}; };
}

// As above for a selection: a copy of the item (or its index), unset when nothing is selected.
template <typename T>
[[nodiscard]] Callback<void(std::optional<std::size_t>)> indexed_selection(std::shared_ptr<ItemsState<T>>  state,
                                                                           Callback<void(SelectedItem<T>)> handler)
{
    if (!handler)
    {
        return {};
    }
    return [state = std::move(state), handler = std::move(handler)](std::optional<std::size_t> index) mutable
    {
        const bool valid = index && *index < state->size();
        if constexpr (std::copy_constructible<T>)
        {
            handler(valid ? SelectedItem<T>{state->at(*index)} : std::nullopt);
        }
        else
        {
            handler(valid ? index : std::nullopt);
        }
    };
}

// Hands the common fields of a typed spec to the index-based one that builds it.
template <typename From, typename To>
void move_common(From& from, To& to)
{
    to.style                = from.style;
    to.margin               = std::move(from.margin);
    to.width                = std::move(from.width);
    to.height               = std::move(from.height);
    to.min_width            = std::move(from.min_width);
    to.min_height           = std::move(from.min_height);
    to.max_width            = std::move(from.max_width);
    to.max_height           = std::move(from.max_height);
    to.horizontal_alignment = std::move(from.horizontal_alignment);
    to.vertical_alignment   = std::move(from.vertical_alignment);
    to.visible              = std::move(from.visible);
    to.tool_tip             = std::move(from.tool_tip);
    to.accessibility        = std::move(from.accessibility);
    to.on_pointer_entered   = std::move(from.on_pointer_entered);
    to.on_pointer_exited    = std::move(from.on_pointer_exited);
    to.on_got_focus         = std::move(from.on_got_focus);
    to.on_lost_focus        = std::move(from.on_lost_focus);
    to.on_key_down          = std::move(from.on_key_down);
    to.on_wheel             = std::move(from.on_wheel);
}
} // namespace detail

} // namespace ui
