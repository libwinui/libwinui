// SPDX-License-Identifier: MIT
#include "winui/ui/media/Icon.hpp"

#include "winui/core/Geometry.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>

namespace ui
{

Size IconControl::measure_override(Size available, const MeasureContext&) const
{
    if (source_.empty())
    {
        return Size{};
    }
    Size desired   = source_.slot_size(size_.value_or(0.0f));
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void IconControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    source_.draw_in_slot(renderer,
                         bounds(),
                         foreground_.resolve(theme).value_or(theme.colors().text),
                         size_.value_or(0.0f),
                         theme.icon_font_family);
}

std::unique_ptr<ui::Element> Icon::build() &&
{
    auto node = std::make_unique<ui::IconControl>();
    apply(source, *node, [raw = node.get()](const IconSource& v) { raw->set_source(v); });
    if (size.reactive() || size.initial())
    {
        apply(size, *node, [raw = node.get()](const std::optional<float>& v) { raw->set_size(v); });
    }
    apply(foreground, *node, [raw = node.get()](const Paint& v) { raw->set_foreground(v); });

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
