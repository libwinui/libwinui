// SPDX-License-Identifier: MIT
#include "winui/ui/containers/GroupBox.hpp"

#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include <cmath>

namespace ui
{

namespace
{
// The header over the card: BodyStrong, then 10 DIPs (a Windows 11 settings section header's 6 below
// plus the 4 between cards), then the card with 16 DIPs of padding inside its 1 DIP stroke, at
// the overlay corner radius like every top-level card.
constexpr float GroupBoxHeaderSpacing = 10.0f;
constexpr float GroupBoxCardPadding   = 16.0f;

TextStyle group_box_header_style(const Theme& theme)
{
    if (detail::controls(theme).era_group_box)
    {
        TextStyle style  = theme.text_style;
        style.horizontal = TextAlign::Leading;
        return style;
    }
    TextStyle style = theme.type.for_role(TextRole::BodyStrong);
    if (!theme.type.body_strong)
    {
        style.weight = FontWeight::SemiBold;
    }
    style.horizontal = TextAlign::Leading;
    return style;
}
} // namespace

GroupBoxControl::Metrics GroupBoxControl::metrics(const TextMeasurer& measurer, const Theme& theme) const
{
    Metrics    m;
    const Size header = header_.empty() ? Size{} : measurer.measure(header_, group_box_header_style(theme));
    m.header_width    = header.width;
    m.header_height   = header.height;
    if (const auto& era = detail::controls(theme).era_group_box)
    {
        // The frame's top runs through the header's middle; with no header it is the top edge.
        m.padding = era->padding;
        if (header_.empty())
        {
            m.padding.top = era->padding.bottom;
        }
        else
        {
            m.frame_top   = std::max(0.0f, std::floor(header.height / 2.0f) - era->frame_raise);
            m.padding.top = std::max(era->padding.top, header.height + 2.0f);
        }
        return m;
    }
    m.frame_top = header_.empty() ? 0.0f : header.height + GroupBoxHeaderSpacing;
    m.padding   = Edges{.left   = GroupBoxCardPadding,
                        .top    = m.frame_top + GroupBoxCardPadding,
                        .right  = GroupBoxCardPadding,
                        .bottom = GroupBoxCardPadding};
    return m;
}

Size GroupBoxControl::measure_override(Size available, const MeasureContext& context) const
{
    metrics_           = metrics(context.measurer, context.theme);
    const Thickness& p = metrics_.padding;
    const Size inner{std::max(0.0f, available.width - p.horizontal()), std::max(0.0f, available.height - p.vertical())};
    const Size child = ContentHost::measure_override(inner, context);

    // The header is never clipped by a narrow content: an era's starts in from the corner and
    // keeps its gap on the right.
    float header_room = metrics_.header_width;
    if (const auto& era = detail::controls(context.theme).era_group_box; era && !header_.empty())
    {
        header_room += era->header_x + era->header_gap + 1.0f;
    }
    return Size{std::max(child.width + p.horizontal(), header_room), child.height + p.vertical()};
}

void GroupBoxControl::arrange_override(const Rect& final_rect, const ArrangeContext& context)
{
    const Thickness& p = metrics_.padding;
    ContentHost::arrange_override(Rect{final_rect.x + p.left,
                                       final_rect.y + p.top,
                                       std::max(0.0f, final_rect.width - p.horizontal()),
                                       std::max(0.0f, final_rect.height - p.vertical())},
                                  context);
}

void GroupBoxControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme&    theme = context.theme;
    const TextStyle style = group_box_header_style(theme);
    const Rect      frame{bounds().x,
                          bounds().y + metrics_.frame_top,
                          bounds().width,
                          std::max(0.0f, bounds().height - metrics_.frame_top)};

    if (const auto& era = detail::controls(theme).era_group_box)
    {
        // The frame, drawn in up to three clipped passes so the header's gap stays open: left of
        // the gap, right of it, and below the top edge in between.
        const auto stroke = [&]
        {
            if (era->frame_light)
            {
                // EDGE_ETCHED: the light ring one pixel down and right, the dark one over it.
                const Rect ring{frame.x, frame.y, std::max(0.0f, frame.width - 1.0f), std::max(0.0f, frame.height - 1.0f)};
                stroke_rounded_rect_inside(renderer,
                                           Rect{ring.x + 1.0f, ring.y + 1.0f, ring.width, ring.height},
                                           era->radius,
                                           *era->frame_light,
                                           1.0f);
                stroke_rounded_rect_inside(renderer, ring, era->radius, era->frame, 1.0f);
            }
            else
            {
                stroke_rounded_rect_inside(renderer, frame, era->radius, era->frame, 1.0f);
            }
        };
        if (header_.empty())
        {
            stroke();
        }
        else
        {
            const float label_x = bounds().x + era->header_x;
            const float label_width = std::clamp(metrics_.header_width, 0.0f, std::max(0.0f, bounds().right() - label_x));
            const float gap_left  = label_x - era->header_gap;
            const float gap_right = std::min(bounds().right(), label_x + label_width + era->header_gap);
            renderer.push_clip(Rect{frame.x, frame.y, gap_left - frame.x, frame.height});
            stroke();
            renderer.pop_clip();
            renderer.push_clip(Rect{gap_right, frame.y, std::max(0.0f, frame.right() - gap_right), frame.height});
            stroke();
            renderer.pop_clip();
            renderer.push_clip(Rect{gap_left, frame.y + 2.0f, gap_right - gap_left, std::max(0.0f, frame.height - 2.0f)});
            stroke();
            renderer.pop_clip();
            renderer.draw_text(header_, Rect{label_x, bounds().y, label_width, metrics_.header_height}, era->header, style);
        }
    }
    else
    {
        if (!header_.empty())
        {
            renderer.draw_text(header_,
                               Rect{bounds().x, bounds().y, bounds().width, metrics_.header_height},
                               theme.colors().text,
                               style);
        }
        // A card in the card tokens; a theme without them strokes its divider color.
        if (detail::controls(theme).card_surfaces)
        {
            draw_border(renderer,
                        frame,
                        theme.palette.card_background_default,
                        theme.palette.card_stroke_default,
                        Thickness{1.0f},
                        theme.overlay_corner_radius);
        }
        else
        {
            draw_border(renderer, frame, std::nullopt, theme.colors().divider, Thickness{1.0f}, theme.overlay_corner_radius);
        }
    }
    ContentHost::render_override(renderer, context);
}

std::unique_ptr<ui::Element> GroupBox::build() &&
{
    auto                 node = std::make_unique<ui::GroupBoxControl>();
    ui::GroupBoxControl* raw  = node.get();
    apply(header, *node, [raw](const std::string& v) { raw->set_header(v); });
    if (content)
    {
        node->set_child(content.take());
    }
    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
