// SPDX-License-Identifier: MIT
#include "winui/ui/ControlHeader.hpp"

#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>

namespace ui
{

TextStyle ControlHeader::style(const Theme& theme)
{
    // Every header wraps.
    TextStyle s  = theme.text_style;
    s.horizontal = TextAlign::Leading;
    s.vertical   = TextAlign::Leading;
    s.wrapping   = TextWrapping::Wrap;
    return s;
}

Size ControlHeader::measure(float available_width, const MeasureContext& context) const
{
    if (text_.empty())
    {
        height_ = 0.0f;
        return {};
    }
    const Size text = context.measurer.measure_wrapped(text_, style(context.theme), available_width);
    height_         = text.height;
    return Size{text.width, text.height + gap_};
}

void ControlHeader::arrange(Element& control)
{
    if (text_.empty())
    {
        rect_ = Rect{};
        return;
    }
    Rect&       bounds = control.bounds_;
    const float taken  = std::min(bounds.height, height_ + gap_);
    rect_              = Rect{bounds.left(), bounds.top(), bounds.width, std::min(bounds.height, height_)};
    bounds             = Rect{bounds.left(), bounds.top() + taken, bounds.width, bounds.height - taken};
}

void ControlHeader::render(Renderer& renderer, const Theme& theme, bool enabled) const
{
    if (text_.empty())
    {
        return;
    }
    // The primary text color, or the disabled text color when disabled,
    // the same pair every other label in this codebase reads.
    TextStyle s = style(theme);
    if (!enabled)
    {
        s.effect = theme.disabled_text_effect;
    }
    renderer.draw_text(text_, rect_, enabled ? theme.colors().text : theme.colors().text_disabled, s);
}

Size ControlDescription::measure(float available_width, const MeasureContext& context) const
{
    if (empty())
    {
        height_ = 0.0f;
        return {};
    }
    const Size text = context.measurer.measure_wrapped(shown(), ControlHeader::style(context.theme), available_width);
    height_         = text.height;
    return text;
}

void ControlDescription::arrange(Element& control)
{
    if (empty())
    {
        rect_ = Rect{};
        return;
    }
    Rect&       bounds = control.bounds_;
    const float taken  = std::min(bounds.height, height_);
    rect_              = Rect{bounds.left(), bounds.bottom() - taken, bounds.width, taken};
    bounds             = Rect{bounds.left(), bounds.top(), bounds.width, bounds.height - taken};
}

void ControlDescription::render(Renderer& renderer, const Theme& theme, bool enabled) const
{
    if (empty())
    {
        return;
    }
    // The description presenter's foreground is the page's medium text brush; an error is critical.
    TextStyle s = ControlHeader::style(theme);
    if (!enabled)
    {
        s.effect = theme.disabled_text_effect;
    }
    const Color color = !enabled      ? theme.colors().text_disabled
                        : has_error() ? theme.colors().critical
                                      : theme.palette.text_fill_secondary;
    renderer.draw_text(shown(), rect_, color, s);
}

} // namespace ui
