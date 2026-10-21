// SPDX-License-Identifier: MIT
// The index-based view builds' shared steps: ListView and TileView.
#pragma once

#include "winui/core/ObservableVector.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/ListViewBase.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/ItemsModel.hpp"

#include <memory>
#include <utility>

#include <cstddef>

namespace ui
{

namespace detail
{

// A spec's item template (a Spec per index) as the element factory the controls take.
inline ui::ListViewBase::ItemTemplate element_template(ItemTemplate item_template)
{
    if (!item_template)
    {
        return {};
    }
    return [t = std::move(item_template)](std::size_t index) mutable -> std::unique_ptr<ui::Element>
    { return t(index).build(); };
}

// Sets the item count and, when it follows an ObservableVector, forwards each change to the
// control's own notifications, so realized rows stay with their items.
template <typename C>
void follow_item_count(ui::ItemCount& count, C& node, ui::Callback<void()> after = {})
{
    node.set_item_count(count.initial());
    if (!count.reactive())
    {
        return;
    }
    C* raw = &node;
    node.add_subscription(count.subscribe(
        [raw, after = std::move(after)](const ui::CollectionChange& change) mutable
        {
            if constexpr (requires { raw->notify_inserted(std::size_t{}, std::size_t{}); })
            {
                switch (change.kind)
                {
                    case ui::CollectionChangeKind::Inserted:
                        raw->notify_inserted(change.index, change.count);
                        break;
                    case ui::CollectionChangeKind::Removed:
                        raw->notify_removed(change.index, change.count);
                        break;
                    case ui::CollectionChangeKind::Replaced:
                        raw->notify_replaced(change.index, change.count);
                        break;
                    case ui::CollectionChangeKind::Moved:
                        if constexpr (requires { raw->notify_moved(std::size_t{}, std::size_t{}); })
                        {
                            raw->notify_moved(change.index, change.to);
                        }
                        else
                        {
                            raw->notify_removed(change.index, 1);
                            raw->notify_inserted(change.to, 1);
                        }
                        break;
                    case ui::CollectionChangeKind::Reset:
                        raw->set_item_count(change.size);
                        break;
                }
            }
            else
            {
                raw->set_item_count(change.size); // fixed-height rows: the count is all there is to know
            }
            if (after)
            {
                after();
            }
        }));
}

// What ListView and TileView share (ListViewBase), in build()'s order: values, then bindings, then
// handlers.
template <typename S>
void build_list_base(S& spec, ui::ListViewBase& node)
{
    ui::ListViewBase* raw = &node;
    apply(spec.item_transitions, node, [raw = &node](const ui::TransitionList& v) { raw->set_item_transitions(v); });
    // The group keys are read again after the control has followed each collection change.
    ui::Callback<void()> regroup;
    if (spec.group_keys)
    {
        node.set_group_keys(spec.group_keys());
        regroup = [raw, keys = std::move(spec.group_keys)]() mutable { raw->set_group_keys(keys()); };
    }
    if (spec.group_header_template)
    {
        node.set_group_header_template([raw, t = std::move(spec.group_header_template)](std::size_t group) mutable
                                       { return t(raw->group_key(group)).build(); });
    }
    apply(spec.sticky_group_headers, node, [raw](const bool& v) { raw->set_sticky_group_headers(v); });
    if (spec.item_template)
    {
        node.set_item_template(element_template(std::move(spec.item_template)));
        follow_item_count(spec.item_count, node, std::move(regroup));
    }
    else
    {
        node.set_items(std::move(spec.items));
    }
    apply(spec.selection_mode, node, [raw](const ui::SelectionMode& v) { raw->set_selection_mode(v); });
    apply(spec.selected_index, node, [raw](const std::optional<std::size_t>& index) { raw->set_selected_index(index); });
    if (spec.selected_indices.reactive() || !spec.selected_indices.initial().empty())
    {
        apply(spec.selected_indices,
              node,
              [raw](const std::vector<std::size_t>& indices) { raw->set_selected_items(indices); });
    }
    // One writer for both: a selection change, and the selected item's index shifting under an
    // insert, removal or move (written back without running the spec's handler).
    const auto write_selected = ui::shared_writer(spec.selected_index);
    const auto write_indices  = ui::shared_writer(spec.selected_indices);
    if (write_selected || write_indices)
    {
        node.on_selected_index_shifted(
            [raw, write_selected, write_indices](std::optional<std::size_t> index)
            {
                if (write_selected)
                {
                    (*write_selected)(index);
                }
                if (write_indices)
                {
                    (*write_indices)(raw->selected_items());
                }
            });
    }
    apply(spec.enabled, node, [raw](const bool& value) { raw->set_enabled(value); });
    // A handler with nothing to switch it on would never fire: a constant false flag turns on with
    // its handler. A bound flag keeps the say.
    if (spec.on_item_click && !spec.item_click_enabled.reactive())
    {
        spec.item_click_enabled = true;
    }
    if (spec.on_item_invoked && !spec.item_invoked_enabled.reactive())
    {
        spec.item_invoked_enabled = true;
    }
    apply(spec.item_click_enabled, node, [raw](const bool& v) { raw->set_item_click_enabled(v); });
    apply(spec.item_invoked_enabled, node, [raw](const bool& v) { raw->set_item_invoked_enabled(v); });
    apply(spec.can_drag_items, node, [raw](const bool& v) { raw->set_can_drag_items(v); });
    apply(spec.can_reorder_items, node, [raw](const bool& v) { raw->set_can_reorder_items(v); });
    apply(spec.allow_drop, node, [raw](const bool& v) { raw->set_allow_drop(v); });
    if (spec.background.reactive() || !spec.background.initial().empty())
    {
        apply(spec.background,
              node,
              [raw](const ui::Paint& v)
              {
                  if (v.resource() != nullptr)
                  {
                      raw->set_background(v.resource());
                  }
                  else
                  {
                      raw->set_background(v.color());
                  }
              });
    }
    apply(spec.border_brush,
          node,
          [raw](const ui::Paint& v)
          {
              if (v.resource() != nullptr)
              {
                  raw->set_border_brush(v.resource());
              }
              else
              {
                  raw->set_border_brush(v.color());
              }
          });
    apply(spec.border_thickness, node, [raw](const ui::Thickness& v) { raw->set_border_thickness(v); });
    if (auto header = spec.header_content.take())
    {
        node.set_header(std::move(header));
    }
    else if (spec.header.reactive() || !spec.header.initial().empty())
    {
        apply(spec.header, node, [raw](const std::string& v) { raw->set_header_text(v); });
    }
    if (auto footer = spec.footer_content.take())
    {
        node.set_footer(std::move(footer));
    }
    if (spec.item_context_menu)
    {
        node.set_item_context_menu(std::move(spec.item_context_menu));
    }

    auto changed = ui::write_back(write_selected, std::move(spec.on_selection_changed));
    if (write_indices)
    {
        changed = [raw, write_indices, inner = std::move(changed)](std::optional<std::size_t> index) mutable
        {
            (*write_indices)(raw->selected_items());
            if (inner)
            {
                inner(index);
            }
        };
    }
    if (changed)
    {
        node.on_selection_changed(std::move(changed));
    }
    if (spec.on_item_click)
    {
        node.on_item_click(std::move(spec.on_item_click));
    }
    if (spec.on_item_invoked)
    {
        node.on_item_invoked(std::move(spec.on_item_invoked));
    }
    if (spec.on_drag_items_starting)
    {
        node.on_drag_items_starting(std::move(spec.on_drag_items_starting));
    }
    if (spec.on_drag_items_completed)
    {
        node.on_drag_items_completed(std::move(spec.on_drag_items_completed));
    }
    if (spec.on_items_reordered)
    {
        node.on_items_reordered(std::move(spec.on_items_reordered));
    }
}

} // namespace detail

} // namespace ui
