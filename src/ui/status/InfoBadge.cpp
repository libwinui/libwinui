// SPDX-License-Identifier: MIT
#include "winui/ui/status/InfoBadge.hpp"

#include "../../render/ControlGlyphs.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

namespace ui
{

namespace
{
constexpr float     MinSize        = 4.0f;                                               // the minimum width and height
constexpr float     BadgeMaxHeight = 16.0f;                                              // the maximum height
constexpr float     ValueFontSize  = 11.0f;                                              // the value's font size
constexpr Thickness ValueMargin    = Edges{.left = 4.0f, .right = 4.0f, .bottom = 2.0f}; // the value's margin
// The icon kinds: a 4 margin, or the glyph icons' padding plus margin. Both leave an 8x8 box
// inside the 16px maximum height, and the glyph is scaled to fill it.
constexpr float IconInset         = 4.0f;
constexpr float InfoBadgeIconSize = BadgeMaxHeight - 2.0f * IconInset;

TextStyle value_style(const Theme& theme)
{
    TextStyle style = theme.text_style;
    style.font_size = ValueFontSize;
    return style;
}

Color badge_background(const Theme& theme, InfoBadgeSeverity severity)
{
    switch (severity)
    {
        case InfoBadgeSeverity::Attention:
            // Attention is the accent color. `informational.fill` is where every
            // era keeps its accent-strength status mark (Theme::Severities).
            return theme.colors().informational;
        case InfoBadgeSeverity::Informational:
            return theme.severity.neutral;
        case InfoBadgeSeverity::Success:
            return theme.colors().success;
        case InfoBadgeSeverity::Caution:
            return theme.colors().caution;
        case InfoBadgeSeverity::Critical:
            return theme.colors().critical;
        case InfoBadgeSeverity::Default:
            break;
    }
    return theme.colors().accent; // the default background is the accent fill
}

Color badge_foreground(const Theme& theme, InfoBadgeSeverity severity)
{
    // The default foreground is the text on accent. The severity set carries
    // each era's color for a mark drawn on one of its fills.
    return severity == InfoBadgeSeverity::Default ? theme.colors().text_on_accent : theme.severity.text_on_fill;
}
} // namespace

IconGlyph InfoBadgeControl::severity_glyph(InfoBadgeSeverity severity)
{
    switch (severity)
    {
        case InfoBadgeSeverity::Informational:
            return glyphs::InfoBarInformational;
        case InfoBadgeSeverity::Success:
            return glyphs::AcceptSymbol;
        case InfoBadgeSeverity::Caution:
            return glyphs::Important;
        case InfoBadgeSeverity::Critical:
            return glyphs::Cancel;
        case InfoBadgeSeverity::Default:
        case InfoBadgeSeverity::Attention:
            break;
    }
    return glyphs::InfoBadgeAttention;
}

void InfoBadgeControl::set_value(std::optional<int> value)
{
    const std::optional<int> clamped = value ? std::optional<int>{std::max(0, *value)} : std::nullopt;
    if (clamped == value_)
    {
        return;
    }
    value_ = clamped;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void InfoBadgeControl::set_severity(InfoBadgeSeverity severity)
{
    if (severity == severity_)
    {
        return;
    }
    severity_ = severity;
    mark_dirty(DirtyFlags::Visual);
}

void InfoBadgeControl::set_icon(IconSource icon)
{
    if (icon == icon_)
    {
        return;
    }
    icon_ = std::move(icon);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void InfoBadgeControl::set_background(std::optional<Color> background)
{
    if (background == background_ && background_resource_ == nullptr)
    {
        return;
    }
    background_          = background;
    background_resource_ = nullptr;
    mark_dirty(DirtyFlags::Visual);
}

void InfoBadgeControl::set_background(ThemeColor resource)
{
    if (resource == background_resource_ && !background_)
    {
        return;
    }
    background_          = std::nullopt;
    background_resource_ = resource;
    mark_dirty(DirtyFlags::Visual);
}

Size InfoBadgeControl::measure_override(Size available, const MeasureContext& context) const
{
    Size desired;
    if (value_)
    {
        const Size text_size = context.measurer.measure(std::to_string(*value_), value_style(context.theme));
        desired              = Size{std::max(MinSize, text_size.width + ValueMargin.horizontal()),
                                    std::clamp(text_size.height + ValueMargin.vertical(), MinSize, BadgeMaxHeight)};
    }
    else if (icon_)
    {
        desired = Size{BadgeMaxHeight, BadgeMaxHeight};
    }
    else
    {
        desired = Size{MinSize, MinSize};
    }
    // Never narrower than tall.
    desired.width  = std::max(desired.width, desired.height);
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void InfoBadgeControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme     = context.theme;
    const float  radius    = bounds().height * 0.5f;
    const Color background = background_resource_ != nullptr ? background_resource_(theme)
                                                             : background_.value_or(badge_background(theme, severity_));
    renderer.fill_rounded_rect(bounds(), radius, background);

    const Color foreground = badge_foreground(theme, severity_);
    if (value_)
    {
        renderer.draw_text(std::to_string(*value_), bounds().inset(ValueMargin), foreground, value_style(theme));
    }
    else if (icon_)
    {
        const Rect box{bounds().center().x - InfoBadgeIconSize * 0.5f,
                       bounds().center().y - InfoBadgeIconSize * 0.5f,
                       InfoBadgeIconSize,
                       InfoBadgeIconSize};
        icon_.draw_in_slot(renderer, box, foreground, InfoBadgeIconSize, theme.icon_font_family);
    }
}

std::unique_ptr<ui::Element> InfoBadge::build() &&
{
    auto node = std::make_unique<ui::InfoBadgeControl>();

    apply(value, *node, [raw = node.get()](const std::optional<int>& v) { raw->set_value(v); });
    apply(severity, *node, [raw = node.get()](const ui::InfoBadgeSeverity& v) { raw->set_severity(v); });
    apply(icon, *node, [raw = node.get()](const IconSource& v) { raw->set_icon(v); });
    apply(background,
          *node,
          [raw = node.get()](const ui::Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
