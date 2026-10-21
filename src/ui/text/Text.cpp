// SPDX-License-Identifier: MIT
#include "winui/ui/text/Text.hpp"

#include "LinkAutomation.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextAnalyzer.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Launcher.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/Inlines.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

// ---- automation ----

void TextControl::automation_children(std::vector<Element*>& out) const
{
    // The hyperlinks, read live from this control.
    const detail::LinkAutomation::Source source{
        .links        = [this]() -> const std::vector<const Inline*>& { return links_; },
        .layout       = [this]() -> const TextLayout& { return layout_; },
        .origin       = [this] { return Point{bounds().x, bounds().y}; },
        .shown_height = detail::whole_layout,
        .enabled      = [this] { return is_enabled(); },
    };
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the nodes are a cache
    detail::list_link_automation(const_cast<TextControl&>(*this), link_automation_, source, out);
}

void TextControl::place_link_automation() const
{
    detail::place_link_automation(link_automation_);
}

TextStyle TextControl::resolved_style(const Theme& theme) const
{
    TextStyle s = style_;
    if (!style_explicit_)
    {
        s = theme.type.for_role(role_);
        if (role_ == TextRole::BodyStrong && !theme.type.body_strong)
        {
            s.weight = FontWeight::SemiBold;
        }
        s.horizontal = style_.horizontal; // TextControl's own alignment, not the theme's
        s.vertical   = style_.vertical;
    }
    if (wrapping_ != TextWrapping::NoWrap)
    {
        s.wrapping = wrapping_;
    }
    if (character_spacing_ != 0)
    {
        s.character_spacing = character_spacing_;
    }
    switch (alignment_)
    {
        case TextAlignment::Center:
            s.horizontal = TextAlign::Center;
            break;
        case TextAlignment::Right:
            s.horizontal = TextAlign::Trailing;
            break;
        case TextAlignment::Left:
        case TextAlignment::Justify:
            break;
    }
    return s;
}

void TextControl::set_inlines(std::vector<Inline> inlines)
{
    inlines_ = std::move(inlines);
    text_.clear();
    for (const Inline& in : inlines_)
    {
        text_ += in.text;
    }
    interaction_.select(0, 0);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TextControl::set_selection_enabled(bool enabled)
{
    if (selection_enabled_ == enabled)
    {
        return;
    }
    selection_enabled_ = enabled;
    interaction_.select(0, 0);
    interaction_.dragging = false;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void TextControl::select_all()
{
    interaction_.select(0, text_.size());
    mark_dirty(DirtyFlags::Visual);
}

Size TextControl::measure_override(Size available, const MeasureContext& context) const
{
    const TextStyle style = resolved_style(context.theme);
    Size            desired{};
    if (uses_layout())
    {
        measurer_ = &context.measurer;
        std::vector<Paragraph> paragraphs(1);
        if (inlines_.empty())
        {
            paragraphs[0].inlines.push_back(Inline{.text = text_});
        }
        else
        {
            paragraphs[0].inlines = inlines_;
        }
        layout_.set_content(to_layout_paragraphs(paragraphs, style, character_spacing_, nullptr));
        links_.clear();
        for (const Inline& in : inlines_)
        {
            if (in.is_link())
            {
                links_.push_back(&in);
            }
        }
        layout_options_            = TextLayoutOptions{};
        layout_options_.wrapping   = style.wrapping;
        layout_options_.alignment  = alignment_;
        layout_options_.base_style = style;
        layout_options_.max_width  = style.wrapping != TextWrapping::NoWrap && std::isfinite(available.width)
                                         ? available.width
                                         : 0.0f;
        layout_.layout(layout_options_, context.measurer);
        desired = layout_.size();
    }
    else
    {
        measurer_       = &context.measurer;
        measured_style_ = style;
        // Wrapping needs a width to wrap at; an unbounded one keeps the run on a single line.
        desired = style.wrapping != TextWrapping::NoWrap && std::isfinite(available.width)
                      ? context.measurer.measure_wrapped(text_, style, available.width)
                      : context.measurer.measure(text_, style);
    }
    // Never demand more than offered on a bounded axis: an unwrapped run is clipped, and a
    // WrapWholeWords word longer than the line overflows the same way.
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void TextControl::relayout(float width) const
{
    if (!measurer_)
    {
        return;
    }
    // Wrapping breaks at the arranged width, and every alignment but Left places lines in it.
    if (layout_options_.wrapping == TextWrapping::NoWrap && alignment_ == TextAlignment::Left)
    {
        return;
    }
    if (layout_options_.max_width == width)
    {
        return;
    }
    layout_options_.max_width = width;
    layout_.layout(layout_options_, *measurer_);
}

void TextControl::arrange_override(const Rect&, const ArrangeContext&)
{
    if (uses_layout())
    {
        relayout(bounds().width);
        place_link_automation();
        return;
    }
    trim_to_bounds();
}

void TextControl::trim_to_bounds()
{
    const bool was = trimmed_;
    trimmed_       = false;
    trimmed_text_.clear();
    if (trimming_ != TextTrimming::None && measurer_ != nullptr && !text_.empty())
    {
        const TextStyle& style = measured_style_;
        const float      w     = bounds().width;
        const float      h     = bounds().height;
        const auto       fits  = [&](std::string_view s)
        {
            const Size d = style.wrapping != TextWrapping::NoWrap && w > 0.0f
                               ? measurer_->measure_wrapped(s, style, w)
                               : measurer_->measure(s, style);
            return d.width <= w + 0.5f && d.height <= h + 0.5f;
        };
        if (!fits(text_))
        {
            trimmed_ = true;
            if (trimming_ != TextTrimming::Clip)
            {
                // The longest start of the text that still fits with the ellipsis after it, cut
                // between clusters.
                std::vector<std::size_t> stops{0};
                for (std::size_t i = 0; i < text_.size();)
                {
                    i = text_analyzer().next_cluster(text_, i);
                    stops.push_back(i);
                }
                std::size_t lo = 0;
                std::size_t hi = stops.size() - 1;
                while (lo < hi)
                {
                    const std::size_t mid = (lo + hi + 1) / 2;
                    if (fits(text_.substr(0, stops[mid]) + "…"))
                    {
                        lo = mid;
                    }
                    else
                    {
                        hi = mid - 1;
                    }
                }
                std::size_t cut = stops[lo];
                if (trimming_ == TextTrimming::WordEllipsis)
                {
                    // Back to the end of the last whole word.
                    const std::size_t space = text_.find_last_of(" \t\n", cut);
                    if (space != std::string::npos && space > 0 && cut < text_.size() && text_[cut] != ' ')
                    {
                        cut = space;
                    }
                }
                while (cut > 0 && (text_[cut - 1] == ' ' || text_[cut - 1] == '\n'))
                {
                    --cut;
                }
                trimmed_text_ = text_.substr(0, cut) + "…";
            }
        }
    }
    if (trimmed_ != was && on_trimmed_changed_)
    {
        on_trimmed_changed_(trimmed_);
    }
}

void TextControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (uses_layout())
    {
        TextLayout::Paint paint;
        paint.foreground = !is_enabled() ? theme.colors().text_disabled
                           : color_resource_ != nullptr
                               ? color_resource_(theme)
                               : color_.value_or(theme.type.color_for_role(role_).value_or(theme.colors().text));
        interaction_.paint(paint, theme, is_focused() && selection_enabled_, selection_color_.resolve(theme));
        const Size size = layout_.size();
        const bool clip = size.width > bounds().width + 0.5f || size.height > bounds().height + 0.5f;
        if (clip)
        {
            renderer.push_clip(bounds());
        }
        layout_.render(renderer, Point{bounds().x, bounds().y}, paint);
        if (clip)
        {
            renderer.pop_clip();
        }
        return;
    }

    // An explicit set_color() wins, then the theme's per-role color (Aero's `#003399` main
    // instruction), then the page's text color.
    const std::string& shown = trimmed_ && trimming_ != TextTrimming::Clip ? trimmed_text_ : text_;
    const bool         clip  = trimmed_ && trimming_ == TextTrimming::Clip;
    if (clip)
    {
        renderer.push_clip(bounds());
    }
    if (!is_enabled())
    {
        TextStyle style = resolved_style(theme);
        style.effect    = theme.disabled_text_effect;
        renderer.draw_text(shown, bounds(), theme.colors().text_disabled, style);
    }
    else
    {
        const Color color = color_resource_ != nullptr
                                ? color_resource_(theme)
                                : color_.value_or(theme.type.color_for_role(role_).value_or(theme.colors().text));
        renderer.draw_text(shown, bounds(), color, resolved_style(theme));
    }
    if (clip)
    {
        renderer.pop_clip();
    }
}

bool TextControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled() || !uses_layout())
    {
        return false;
    }
    const bool held = interaction_.pressed_link.has_value() || interaction_.dragging;
    if (interaction_.pointer(e, layout_, Point{bounds().x, bounds().y}, 0, bounds(), selection_enabled_, links_))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    return held || interaction_.pressed_link.has_value() || interaction_.dragging;
}

bool TextControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, false, false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (is_enabled() && selection_enabled_ && interaction_.key(e, layout_))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    return handled;
}

bool TextControl::handle_context_menu(std::optional<Point> position, ContextMenuHost& host)
{
    if (!is_enabled() || !selection_enabled_)
    {
        return false;
    }
    interaction_.context_menu(position.value_or(Point{bounds().left(), bounds().bottom()}), layout_, host);
    mark_dirty(DirtyFlags::Visual);
    return true;
}

std::optional<CursorShape> TextControl::cursor_at(Point) const
{
    // Hand over a hyperlink, else IBeam when selectable, else no opinion.
    if (interaction_.hover_link)
    {
        return CursorShape::Hand;
    }
    if (selection_enabled_)
    {
        return CursorShape::IBeam;
    }
    return std::nullopt;
}

std::unique_ptr<ui::Element> Text::build() &&
{
    auto node = std::make_unique<ui::TextControl>();

    apply(text, *node, [raw = node.get()](const std::string& value) { raw->set_text(value); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });

    // An explicit `.text_style` wins over `.role`.
    apply(role, *node, [raw = node.get()](const TextRole& v) { raw->set_role(v); });
    apply(text_style,
          *node,
          [raw = node.get()](const std::optional<TextStyle>& v)
          {
              if (v)
              {
                  raw->set_text_style(*v);
              }
          });
    if (wrapping.reactive() || wrapping.initial() != ui::TextWrapping::NoWrap)
    {
        apply(wrapping, *node, [raw = node.get()](const ui::TextWrapping& v) { raw->set_wrapping(v); });
    }
    if (inlines.reactive() || !inlines.initial().empty())
    {
        apply(inlines, *node, [raw = node.get()](const std::vector<Inline>& v) { raw->set_inlines(v); });
    }
    apply(text_alignment, *node, [raw = node.get()](const ui::TextAlignment& value) { raw->set_text_alignment(value); });
    if (character_spacing.reactive() || character_spacing.initial() != 0)
    {
        apply(character_spacing, *node, [raw = node.get()](const int& v) { raw->set_character_spacing(v); });
    }
    apply(selection_enabled, *node, [raw = node.get()](const bool& value) { raw->set_selection_enabled(value); });
    if (selection_highlight_color.reactive() || !selection_highlight_color.initial().empty())
    {
        apply(selection_highlight_color,
              *node,
              [raw = node.get()](const Paint& v)
              { ui::detail::set_paint(v, [raw](auto c) { raw->set_selection_highlight_color(c); }); });
    }
    if (foreground.reactive() || !foreground.initial().empty())
    {
        apply(foreground,
              *node,
              [raw = node.get()](const Paint& v)
              {
                  raw->set_foreground(v.resource());
                  if (v.color())
                  {
                      raw->set_color(*v.color());
                  }
              });
    }
    if (trimming.reactive() || trimming.initial() != ui::TextTrimming::None)
    {
        apply(trimming, *node, [raw = node.get()](const ui::TextTrimming& v) { raw->set_trimming(v); });
    }
    if (on_text_trimmed_changed)
    {
        node->on_text_trimmed_changed(std::move(on_text_trimmed_changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
