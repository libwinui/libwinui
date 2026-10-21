// SPDX-License-Identifier: MIT
#include "winui/ui/panels/Fill.hpp"

#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"

#include <memory>

namespace ui
{

std::unique_ptr<ui::Element> Fill::build() &&
{
    auto node = std::make_unique<ui::FillControl>();
    apply(weight, *node, [raw = node.get()](const float& v) { raw->set_weight(v); });
    if (content)
    {
        node->set_child(content.take());
    }
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
