// SPDX-License-Identifier: MIT
#include "winui/ui/text/Inlines.hpp"

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Strings.hpp"
#include "winui/core/Version.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextAnalyzer.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/Launcher.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

std::vector<TextLayoutParagraph> to_layout_paragraphs(const std::vector<Paragraph>& paragraphs,
                                                      const TextStyle&              base,
                                                      int                           character_spacing,
                                                      std::vector<const Inline*>*   links)
{
    if (links)
    {
        links->clear();
    }
    std::size_t                      next_link = 0;
    std::vector<TextLayoutParagraph> out;
    out.reserve(paragraphs.size());
    for (const Paragraph& paragraph : paragraphs)
    {
        TextLayoutParagraph p;
        p.margin = paragraph.margin;
        for (const Inline& in : paragraph.inlines)
        {
            TextLayoutRun run;
            run.text  = in.text;
            run.style = base;
            if (in.font_family)
            {
                run.style.font_family = *in.font_family;
            }
            if (in.font_size)
            {
                run.style.font_size   = *in.font_size;
                run.style.line_height = 0.0f; // the ramp's line height is for the ramp's size
            }
            if (in.weight)
            {
                run.style.weight = *in.weight;
            }
            run.style.italic            = run.style.italic || in.italic;
            run.style.underline         = run.style.underline || in.underline || in.is_link();
            run.style.strikethrough     = run.style.strikethrough || in.strikethrough;
            run.style.character_spacing = character_spacing;
            run.color                   = in.color;
            if (in.is_link())
            {
                run.tag = next_link++;
                if (links)
                {
                    links->push_back(&in);
                }
            }
            p.runs.push_back(std::move(run));
        }
        out.push_back(std::move(p));
    }
    return out;
}

namespace
{
std::pair<std::size_t, std::size_t> word_at(const std::string& text, std::size_t index)
{
    return word_run(text, index, true);
}

std::pair<std::size_t, std::size_t> paragraph_at(const std::string& text, std::size_t index)
{
    std::size_t start = std::min(index, text.size());
    while (start > 0 && text[start - 1] != '\n')
    {
        --start;
    }
    std::size_t end = std::min(index, text.size());
    while (end < text.size() && text[end] != '\n')
    {
        ++end;
    }
    return {start, end};
}
} // namespace

bool TextInteraction::pointer(
    const PointerEvent&               e,
    const TextLayout&                 layout,
    Point                             origin,
    std::size_t                       first_line,
    const Rect&                       bounds,
    bool                              selectable,
    const std::vector<const Inline*>& links,
    bool                              sole_view)
{
    const float                      top   = first_line < layout.lines().size() ? layout.lines()[first_line].top : 0.0f;
    const Point                      local = {e.position.x - origin.x, e.position.y - origin.y + top};
    const bool                       inside = bounds.contains(e.position);
    const std::optional<std::size_t> link   = inside ? layout.tag_at(local) : std::nullopt;

    switch (e.action)
    {
        case PointerAction::Move:
        {
            bool changed = false;
            if (link != hover_link)
            {
                hover_link = link;
                changed    = true;
            }
            if (dragging && (inside || sole_view))
            {
                const std::size_t at = layout.hit_test(local);
                if (at != caret)
                {
                    caret   = at;
                    changed = true;
                }
            }
            return changed;
        }
        case PointerAction::Down:
            if (e.button != PointerButton::Left || !inside)
            {
                return false;
            }
            if (link)
            {
                pressed_link = link;
                return true;
            }
            if (!selectable)
            {
                return false;
            }
            {
                const std::size_t at = layout.hit_test(local);
                if (e.click_count >= 3)
                {
                    const auto [b, en] = paragraph_at(layout.text(), at);
                    select(b, en);
                }
                else if (e.click_count == 2)
                {
                    const auto [b, en] = word_at(layout.text(), at);
                    select(b, en);
                }
                else if (e.modifiers.shift)
                {
                    caret = at;
                }
                else
                {
                    select(at, at);
                }
                dragging = true;
            }
            return true;
        case PointerAction::Up:
        {
            if (e.button != PointerButton::Left)
            {
                return false;
            }
            dragging                                 = false;
            const std::optional<std::size_t> pressed = pressed_link;
            pressed_link.reset();
            if (pressed && pressed == link && *pressed < links.size())
            {
                // Copies: the handler may replace the very inlines it lives in.
                const Inline&               clicked = *links[*pressed];
                const std::function<void()> handler = clicked.on_click;
                std::string                 uri     = clicked.navigate_uri;
                if (handler)
                {
                    handler();
                }
                launch_uri(uri);
            }
            return pressed.has_value();
        }
        case PointerAction::Leave:
        {
            const bool changed = hover_link.has_value();
            hover_link.reset();
            return changed;
        }
        case PointerAction::Wheel:
            break;
    }
    return false;
}

bool TextInteraction::key(const KeyEvent& e, const TextLayout& layout)
{
    if (e.action != KeyAction::Down || !e.modifiers.ctrl)
    {
        return false;
    }
    if (e.key == Key::A)
    {
        select(0, layout.length());
        return true;
    }
    if (e.key == Key::C && has_selection())
    {
        clipboard().set_text(selected_text(layout));
        return true;
    }
    return false;
}

void TextInteraction::context_menu(Point anchor_point, const TextLayout& layout, ContextMenuHost& host)
{
    const std::vector<ContextMenuItem> items{
        ContextMenuItem{strings().commands.copy.text, has_selection(), false},
        ContextMenuItem{strings().commands.select_all.text, layout.length() > 0, false},
    };
    const std::optional<std::size_t> choice = host.show(anchor_point, items);
    if (choice == 0)
    {
        clipboard().set_text(selected_text(layout));
    }
    else if (choice == 1)
    {
        select(0, layout.length());
    }
}

std::string TextInteraction::selected_text(const TextLayout& layout) const
{
    const auto [b, e]       = range();
    const std::string& text = layout.text();
    if (b >= text.size())
    {
        return {};
    }
    std::string out;
    for (std::size_t i = b; i < std::min(e, text.size()); ++i)
    {
        if (text[i] == '\n')
        {
            out += "\r\n"; // the clipboard's line ending
        }
        else
        {
            out += text[i];
        }
    }
    return out;
}

void TextInteraction::paint(TextLayout::Paint& paint, const Theme& theme, bool show_selection, std::optional<Color> highlight) const
{
    theme_ = &theme;
    if (show_selection && has_selection())
    {
        const auto [b, e] = range();
        // The selection highlight defaults to the accent, and the selected text is the theme's
        // selection text.
        paint.selection = TextHighlight{b, e, highlight.value_or(theme.colors().selection), theme.colors().selection_text};
    }
    paint.tag_color = [self = this](std::size_t tag) -> std::optional<Color>
    {
        const Theme& t = *self->theme_;
        if (tag == self->pressed_link)
        {
            return t.surfaces.hyperlink_pressed.value_or(t.colors().link);
        }
        if (tag == self->hover_link)
        {
            return t.surfaces.hyperlink_hover.value_or(t.colors().link);
        }
        return t.colors().link;
    };
}

} // namespace ui
