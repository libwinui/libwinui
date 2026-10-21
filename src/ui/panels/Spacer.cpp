// SPDX-License-Identifier: MIT
#include "winui/ui/panels/Spacer.hpp"

#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"

#include <memory>

namespace ui
{

std::unique_ptr<ui::Element> Spacer::build() &&
{
    auto node = std::make_unique<ui::SpacerControl>();
    apply(weight, *node, [raw = node.get()](const float& v) { raw->set_weight(v); });
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
