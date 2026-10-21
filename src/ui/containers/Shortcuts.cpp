// SPDX-License-Identifier: MIT
#include "winui/ui/containers/Shortcuts.hpp"

#include "winui/ui/Element.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"

#include <memory>
#include <utility>

namespace ui
{

std::unique_ptr<Element> Shortcuts::build() &&
{
    auto node = std::make_unique<ContentHost>();
    for (Shortcut& shortcut : items.entries)
    {
        node->add_shortcut(std::move(shortcut));
    }
    if (content)
    {
        node->set_child(content.take());
    }
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
