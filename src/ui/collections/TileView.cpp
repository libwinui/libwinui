// SPDX-License-Identifier: MIT
#include "winui/ui/collections/TileView.hpp"

#include "ViewSpecBuild.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/layouts/Layout.hpp"
#include "winui/ui/layouts/TileLayout.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"

#include <memory>
#include <optional>
#include <utility>

#include <cstddef>

namespace ui
{

std::unique_ptr<ui::Element> detail::TileViewSpec::build() &&
{
    auto                 node = std::make_unique<ui::TileViewControl>();
    ui::TileViewControl* raw  = node.get();
    // A tile layout given in `.layout` keeps its own orientation and cap unless the spec sets them.
    std::optional<ui::Orientation> layout_orientation;
    const bool                     has_layout = static_cast<bool>(layout);
    if (has_layout)
    {
        std::unique_ptr<ui::Layout> engine = layout.take();
        if (const auto* tile = dynamic_cast<const ui::TileLayoutEngine*>(engine.get()))
        {
            layout_orientation = tile->orientation();
        }
        node->set_layout(std::move(engine));
    }
    apply(orientation,
          *node,
          [raw, fallback = layout_orientation.value_or(ui::Orientation::Horizontal)](
              const std::optional<ui::Orientation>& v) { raw->set_orientation(v.value_or(fallback)); });
    if (!has_layout || maximum_rows_or_columns.reactive() || maximum_rows_or_columns.initial() != 0)
    {
        apply(maximum_rows_or_columns, *node, [raw](const std::size_t& v) { raw->set_maximum_rows_or_columns(v); });
    }
    apply(item_container_margin,
          *node,
          [raw](const std::optional<ui::Thickness>& v)
          {
              if (v)
              {
                  raw->set_item_container_margin(*v);
              }
          });
    detail::build_list_base(*this, *node);

    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
