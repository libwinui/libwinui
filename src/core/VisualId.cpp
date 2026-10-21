// SPDX-License-Identifier: MIT
#include "winui/core/VisualId.hpp"

#include <atomic>

namespace ui
{

VisualId allocate_visual_id()
{
    static std::atomic<VisualId> next{InvalidVisualId + 1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

} // namespace ui
