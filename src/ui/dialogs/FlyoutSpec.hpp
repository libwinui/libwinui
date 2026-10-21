// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Property.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/spec/Common.hpp"

#include <utility>

namespace ui::detail
{

// The Flyout, MenuFlyout and CommandBarFlyout specs share this open-state block: `.is_open` shows
// and hides the flyout, and a two-way bind follows it, written before the spec's own on_opened/
// on_closed run. A false that on_closing cancels is put back to true.
inline void apply_flyout_open(FlyoutBase&                                  node,
                              Binding<bool>&                               is_open,
                              Binding<bool>&                               light_dismiss,
                              Action&                                      on_opened,
                              Callback<Decision(const PopupClosingArgs&)>& on_closing,
                              Callback<void(PopupCloseReason)>&            on_closed)
{
    apply(light_dismiss, node, [raw = &node](const bool& value) { raw->set_light_dismiss(value); });
    const auto writer = ui::shared_writer(is_open);
    apply(is_open,
          node,
          [raw = &node, writer](const bool& open)
          {
              if (open)
              {
                  raw->show();
              }
              else
              {
                  raw->hide();
              }
              if (raw->is_open() != open && writer)
              {
                  (*writer)(raw->is_open());
              }
          });
    if (auto handler = ui::write_back_value<bool>(writer, true, std::move(on_opened)))
    {
        node.on_opened(std::move(handler));
    }
    if (on_closing)
    {
        node.on_closing(std::move(on_closing));
    }
    if (auto handler = ui::write_back_value<bool>(writer, false, std::move(on_closed)))
    {
        node.on_closed(std::move(handler));
    }
}

} // namespace ui::detail
