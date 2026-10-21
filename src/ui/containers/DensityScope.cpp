// SPDX-License-Identifier: MIT
#include "winui/ui/containers/DensityScope.hpp"

#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>

namespace ui
{

const Theme& DensityScopeControl::scoped(const Theme& base) const
{
    if (density_ == Density::Standard)
    {
        return base;
    }
    // Rebuilt per pass: the base theme can change under the window (a theme switch) and a copy
    // is cheaper than knowing when.
    compact_         = base;
    compact_->sizing = Theme::ControlSizing::compact();
    return *compact_;
}

Size DensityScopeControl::measure_override(Size available, const MeasureContext& context) const
{
    return ContentHost::measure_override(available, context.with_theme(scoped(context.theme)));
}

void DensityScopeControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    ContentHost::render_override(renderer, context.with_theme(scoped(context.theme)));
}

std::unique_ptr<ui::Element> DensityScope::build() &&
{
    auto                     node = std::make_unique<ui::DensityScopeControl>();
    ui::DensityScopeControl* raw  = node.get();
    apply(density, *node, [raw](const ui::Density& v) { raw->set_density(v); });
    if (content)
    {
        node->set_child(content.take());
    }
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
