// SPDX-License-Identifier: MIT
#include "winui/ui/hosting/ContentHost.hpp"

#include "../ExitGhosts.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <string_view>
#include <utility>

namespace ui
{

Size ContentHost::measure_override(Size available, const MeasureContext& context) const
{
    return child_ ? child_->measure(available, context) : Size{};
}

void ContentHost::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    if (child_)
    {
        child_->arrange(final_rect);
    }
}

void ContentHost::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (child_)
    {
        child_->render(renderer, context);
    }
    if (ghosts_)
    {
        ghosts_->render(renderer, theme, bounds().origin());
    }
}

void ContentHost::retire_child()
{
    if (child_ && find_transition<ContentTransition>(children_transitions()) != nullptr)
    {
        detail::ensure(ghosts_).add(std::move(child_), bounds().origin(), transition_storyboards::content_out());
    }
    // Not destroyed here: the child may be the one whose handler replaced it.
    detail::retire(std::move(child_));
}

void ContentHost::tick_override(Duration delta)
{
    if (ghosts_ && ghosts_->advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
}

} // namespace ui
