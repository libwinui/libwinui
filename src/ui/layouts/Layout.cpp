// SPDX-License-Identifier: MIT
#include "winui/ui/layouts/Layout.hpp"

namespace ui
{

Layout::~Layout() = default;

void Layout::invalidate_measure()
{
    if (owner_ != nullptr)
    {
        owner_->layout_invalidated(true);
    }
}

void Layout::invalidate_arrange()
{
    if (owner_ != nullptr)
    {
        owner_->layout_invalidated(false);
    }
}

} // namespace ui
