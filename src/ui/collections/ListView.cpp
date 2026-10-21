// SPDX-License-Identifier: MIT
#include "winui/ui/collections/ListView.hpp"

#include "ViewSpecBuild.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"

#include <memory>
#include <utility>

#include <cstddef>

namespace ui
{

std::unique_ptr<ui::Element> detail::ListViewSpec::build() &&
{
    auto                 node = std::make_unique<ui::ListViewControl>();
    ui::ListViewControl* raw  = node.get();
    apply(item_height, *node, [raw](const std::optional<float>& v) { raw->set_item_height(v); });
    apply(items_updating_scroll_mode,
          *node,
          [raw](const ui::ItemsUpdatingScrollMode& v) { raw->set_items_updating_scroll_mode(v); });
    apply(items_panel_vertical_alignment, *node, [raw](const ui::Alignment& v) { raw->set_panel_vertical_alignment(v); });
    apply(item_container_margin, *node, [raw](const ui::Thickness& v) { raw->set_item_container_margin(v); });
    apply(item_content_horizontal_alignment,
          *node,
          [raw](const ui::Alignment& v) { raw->set_item_horizontal_content_alignment(v); });
    detail::build_list_base(*this, *node);

    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
