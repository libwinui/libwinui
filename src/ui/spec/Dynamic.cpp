// SPDX-License-Identifier: MIT
#include "winui/ui/spec/Dynamic.hpp"

#include "winui/core/Property.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"

#include <memory>
#include <utility>

namespace ui
{

std::unique_ptr<ui::Element> Lazy::build() &&
{
    auto             node = std::make_unique<ui::detail::DynamicHost>();
    ui::ContentHost* raw  = node.get();
    if (content_transitions.reactive() || !content_transitions.initial().empty())
    {
        ui::apply(content_transitions, *node, [raw](const ui::TransitionList& v) { raw->set_children_transitions(v); });
    }

    // The sink owns the factory and moves into the subscription, which the node owns.
    ui::apply(visible,
              *node,
              [raw, factory = std::move(content), drop = unload](const bool& shown) mutable
              {
                  if (shown && raw->child() == nullptr && factory)
                  {
                      Spec built = factory();
                      raw->set_child(std::move(built).build());
                  }
                  else if (!shown && drop)
                  {
                      raw->clear_child();
                  }
                  raw->set_visibility(shown ? ui::Visibility::Visible : ui::Visibility::Collapsed);
              });
    // Applied above, together with the build; apply_common() has nothing left to do for it.
    visible = ui::Binding<bool>{true};

    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
