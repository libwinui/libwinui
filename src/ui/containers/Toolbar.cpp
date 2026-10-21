// SPDX-License-Identifier: MIT
#include "winui/ui/containers/Toolbar.hpp"

#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace ui
{

namespace
{
constexpr float ToolbarSpacing           = 4.0f; // StackControl's own default is 0
constexpr float SeparatorWidth           = 9.0f;
constexpr float SeparatorLineInset       = 2.0f; // vertical inset so the etched-groove pair doesn't touch the edges
constexpr float FlatDividerVerticalInset = 8.0f; // the flat divider's top and bottom inset
} // namespace

// ---- SeparatorControl ----

Size SeparatorControl::measure_override(Size available, const MeasureContext& context) const
{
    // A control-sized height, not available.height: during an Auto-row measure that is the
    // whole remaining budget, and reporting it would balloon the Grid row hosting the toolbar.
    const float natural_height = context.measurer.measure("Mg", context.theme.text_style).height +
                                 context.theme.padding.vertical();
    return {SeparatorWidth, std::min(available.height, natural_height)};
}

void SeparatorControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    const float  x     = bounds().left() + bounds().width * 0.5f;
    if (theme.divider_visual == ThemeDividerVisual::FlatStroke)
    {
        // A 1px flat rule with rounded caps (a 0.5 corner radius), inset 8px top and bottom.
        renderer.fill_rounded_rect(Rect{x - 0.5f,
                                        bounds().top() + FlatDividerVerticalInset,
                                        1.0f,
                                        std::max(0.0f, bounds().height - 2.0f * FlatDividerVerticalInset)},
                                   0.5f,
                                   theme.colors().divider);
        return;
    }

    // The classic etched groove (EDGE_ETCHED): a dark line with a light one beside it. Only
    // Theme::fluent() opts into the flat rule above.
    renderer.stroke_line(Point{x - 0.5f, bounds().top() + SeparatorLineInset},
                         Point{x - 0.5f, bounds().bottom() - SeparatorLineInset},
                         theme.surfaces.separator_dark,
                         1.0f);
    renderer.stroke_line(Point{x + 0.5f, bounds().top() + SeparatorLineInset},
                         Point{x + 0.5f, bounds().bottom() - SeparatorLineInset},
                         theme.surfaces.separator_light,
                         1.0f);
}

// ---- ToolbarControl ----

ToolbarControl::ToolbarControl() : panel_(std::make_unique<StackControl>(Orientation::Horizontal))
{
    set_parent_of(*panel_, this);
    panel_->set_spacing(ToolbarSpacing);
}

void ToolbarControl::add_separator()
{
    panel_->add(make_separator());
}

std::unique_ptr<Element> ToolbarControl::make_separator()
{
    return std::make_unique<SeparatorControl>();
}

Size ToolbarControl::measure_override(Size available, const MeasureContext& context) const
{
    return panel_->measure(available, context);
}

void ToolbarControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    panel_->arrange(final_rect);
}

void ToolbarControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    panel_->render(renderer, context);
}

std::unique_ptr<ui::Element> Separator::build() &&
{
    auto node = std::make_unique<ui::SeparatorControl>();
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

std::unique_ptr<ui::Element> Toolbar::build() &&
{
    auto node = std::make_unique<ui::ToolbarControl>();
    apply(spacing, *node, [raw = node.get()](const float& v) { raw->set_spacing(v); });

    for (auto& child : content.nodes)
    {
        node->add(std::move(child));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
