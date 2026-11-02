// SPDX-License-Identifier: MIT
#include "winui/ui/hosting/DragArea.hpp"

#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"

#ifndef LIBWINUI_MODULE
#include <memory>
#endif

namespace ui
{

std::unique_ptr<ui::Element> DragArea::build() &&
{
    auto node = std::make_unique<ui::DragAreaControl>();
    if (content)
    {
        node->set_child(content.take());
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
