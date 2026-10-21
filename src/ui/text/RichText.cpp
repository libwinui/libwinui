// SPDX-License-Identifier: MIT
#include "winui/ui/text/RichText.hpp"

#include "LinkAutomation.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/Inlines.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

// ---- RichText -------------------------------------------------------------------

RichTextControl::~RichTextControl()
{
    if (overflow_target_)
    {
        overflow_target_->detach();
    }
}

void RichTextControl::set_blocks(std::vector<Paragraph> blocks)
{
    blocks_ = std::move(blocks);
    links_.clear();
    interaction_ = TextInteraction{};
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    mark_chain_dirty();
}

std::string RichTextControl::plain_text() const
{
    std::string text;
    for (std::size_t p = 0; p < blocks_.size(); ++p)
    {
        if (p > 0)
        {
            text += '\n';
        }
        for (const Inline& in : blocks_[p].inlines)
        {
            text += in.text;
        }
    }
    return text;
}

void RichTextControl::set_text_style(TextStyle style)
{
    style_          = std::move(style);
    style_explicit_ = true;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    mark_chain_dirty();
}

void RichTextControl::set_wrapping(TextWrapping wrapping)
{
    wrapping_ = wrapping;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    mark_chain_dirty();
}

void RichTextControl::set_text_alignment(TextAlignment alignment)
{
    alignment_ = alignment;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    mark_chain_dirty();
}

void RichTextControl::set_character_spacing(int spacing)
{
    character_spacing_ = spacing;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    mark_chain_dirty();
}

void RichTextControl::set_selection_enabled(bool enabled)
{
    if (selection_enabled_ == enabled)
    {
        return;
    }
    selection_enabled_ = enabled;
    interaction_.select(0, 0);
    interaction_.dragging = false;
    mark_chain_dirty();
}

void RichTextControl::select_all()
{
    interaction_.select(0, plain_text().size());
    mark_chain_dirty();
}

void RichTextControl::set_overflow_target(RichTextOverflowControl* target)
{
    if (overflow_target_ == target)
    {
        return;
    }
    if (overflow_target_)
    {
        overflow_target_->detach();
    }
    overflow_target_ = target;
    if (target)
    {
        target->attach(this, nullptr);
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void RichTextControl::mark_chain_dirty()
{
    mark_dirty(DirtyFlags::Visual);
    for (RichTextOverflowControl* o = overflow_target_; o != nullptr; o = o->next_)
    {
        o->mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

bool RichTextControl::chain_focused() const
{
    if (is_focused())
    {
        return true;
    }
    for (const RichTextOverflowControl* o = overflow_target_; o != nullptr; o = o->next_)
    {
        if (o->is_focused())
        {
            return true;
        }
    }
    return false;
}

void RichTextControl::place_lines(float height) const
{
    const std::size_t count = layout_.lines().size();
    shown_                  = overflow_target_ && height > 0.0f ? layout_.fitting_lines(height) : count;
    overflow_start_         = shown_ < count ? layout_.lines()[shown_].begin : ui::detail::NoIndex;
}

Size RichTextControl::measure_override(Size available, const MeasureContext& context) const
{
    measurer_ = &context.measurer;
    theme_    = &context.theme;

    // RichText's font is the body text's unless the block sets its own.
    TextStyle base  = style_explicit_ ? style_ : context.theme.type.for_role(TextRole::Body);
    base.horizontal = TextAlign::Leading;
    base.wrapping   = TextWrapping::NoWrap;
    layout_.set_content(to_layout_paragraphs(blocks_, base, character_spacing_, &links_));

    layout_options_            = TextLayoutOptions{};
    layout_options_.max_width  = std::isfinite(available.width) ? available.width : 0.0f;
    layout_options_.wrapping   = wrapping_;
    layout_options_.alignment  = alignment_;
    layout_options_.base_style = base;
    layout_.layout(layout_options_, context.measurer);
    place_lines(available.height);

    Size desired = layout_.size();
    if (overflow_start_ != ui::detail::NoIndex)
    {
        desired.height = layout_.height_of(shown_);
    }
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void RichTextControl::automation_children(std::vector<Element*>& out) const
{
    // The hyperlinks in the lines this block draws; an overflow container has the rest.
    const detail::LinkAutomation::Source source{
        .links        = [this]() -> const std::vector<const Inline*>& { return links_; },
        .layout       = [this]() -> const TextLayout& { return layout_; },
        .origin       = [this] { return Point{bounds().x, bounds().y}; },
        .shown_height = [this]
        { return overflow_target_ != nullptr ? layout_.height_of(shown_) : detail::whole_layout(); },
        .enabled = [this] { return is_enabled(); },
    };
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the nodes are a cache
    detail::list_link_automation(const_cast<RichTextControl&>(*this), link_automation_, source, out);
}

void RichTextControl::arrange_override(const Rect&, const ArrangeContext&)
{
    if (!measurer_)
    {
        return;
    }
    const std::size_t before = overflow_start_;
    if (layout_options_.max_width != bounds().width &&
        (layout_options_.wrapping != TextWrapping::NoWrap || alignment_ != TextAlignment::Left))
    {
        layout_options_.max_width = bounds().width;
        layout_.layout(layout_options_, *measurer_);
    }
    place_lines(bounds().height);
    if (overflow_start_ != before)
    {
        mark_chain_dirty(); // the overflow picks up somewhere else now
    }
    detail::place_link_automation(link_automation_);
}

void RichTextControl::paint(TextLayout::Paint& paint, const Theme& theme) const
{
    paint.foreground = foreground_.resolve(theme).value_or(theme.colors().text);
    interaction_.paint(paint, theme, selection_enabled_ && chain_focused(), selection_color_.resolve(theme));
}

void RichTextControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (layout_.lines().empty())
    {
        return;
    }
    std::vector<TextHighlight> highlights;
    for (const TextHighlighter& h : highlighters_)
    {
        for (const TextHighlighter::Range& r : h.ranges)
        {
            highlights.push_back(TextHighlight{r.start, r.start + r.length, h.background, h.foreground});
        }
    }
    TextLayout::Paint p;
    paint(p, theme);
    p.highlights = highlights;
    p.last_line  = shown_;

    const Size size = layout_.size();
    const bool clip = size.width > bounds().width + 0.5f || layout_.height_of(shown_) > bounds().height + 0.5f;
    if (clip)
    {
        renderer.push_clip(bounds());
    }
    layout_.render(renderer, Point{bounds().x, bounds().y}, p);
    if (clip)
    {
        renderer.pop_clip();
    }
}

bool RichTextControl::pointer_override(const PointerEvent& e)
{
    const bool held = interaction_.pressed_link.has_value() || interaction_.dragging;
    if (interaction_.pointer(e, layout_, Point{bounds().x, bounds().y}, 0, bounds(), selection_enabled_, links_, overflow_target_ == nullptr))
    {
        mark_chain_dirty();
    }
    return held || interaction_.pressed_link.has_value() || interaction_.dragging;
}

bool RichTextControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, false, false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (selection_enabled_ && interaction_.key(e, layout_))
    {
        mark_chain_dirty();
    }
    return handled;
}

bool RichTextControl::handle_context_menu(std::optional<Point> position, ContextMenuHost& host)
{
    if (!selection_enabled_)
    {
        return false;
    }
    interaction_.context_menu(position.value_or(Point{bounds().left(), bounds().bottom()}), layout_, host);
    mark_chain_dirty();
    return true;
}

// ---- RichTextOverflow -----------------------------------------------------------------

RichTextOverflowControl::~RichTextOverflowControl()
{
    if (next_)
    {
        next_->detach();
    }
    if (previous_)
    {
        previous_->next_ = nullptr;
    }
    else if (master_ && master_->overflow_target_ == this)
    {
        master_->overflow_target_ = nullptr;
    }
}

void RichTextOverflowControl::attach(RichTextControl* master, RichTextOverflowControl* previous)
{
    master_   = master;
    previous_ = previous;
    if (next_)
    {
        next_->attach(master, this);
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void RichTextOverflowControl::detach()
{
    master_   = nullptr;
    previous_ = nullptr;
    start_    = ui::detail::NoIndex;
    layout_.set_content({});
    if (next_)
    {
        RichTextOverflowControl* next = next_;
        next->detach();
        next->previous_ = this; // still linked to this one; only the content source went away
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void RichTextOverflowControl::set_overflow_target(RichTextOverflowControl* target)
{
    if (next_ == target)
    {
        return;
    }
    if (next_)
    {
        next_->detach();
        next_->previous_ = nullptr;
    }
    next_ = target;
    if (target)
    {
        if (master_)
        {
            target->attach(master_, this);
        }
        else
        {
            target->previous_ = this;
        }
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

std::size_t RichTextOverflowControl::previous_overflow_start() const
{
    if (!master_)
    {
        return ui::detail::NoIndex;
    }
    return previous_ ? previous_->overflow_start_ : master_->overflow_start_;
}

void RichTextOverflowControl::place_lines(float height) const
{
    const std::size_t count = layout_.lines().size();
    shown_                  = next_ && height > 0.0f ? layout_.fitting_lines(height) : count;
    overflow_start_         = shown_ < count ? layout_.lines()[shown_].begin : ui::detail::NoIndex;
}

Size RichTextOverflowControl::measure_override(Size available, const MeasureContext& context) const
{
    start_ = previous_overflow_start();
    if (start_ == ui::detail::NoIndex)
    {
        layout_.set_content({});
        shown_          = 0;
        overflow_start_ = ui::detail::NoIndex;
        return {};
    }
    layout_.set_content(master_->layout_.content());
    TextLayoutOptions options = master_->layout_options_;
    options.max_width         = std::isfinite(available.width) ? available.width : 0.0f;
    layout_.layout(options, context.measurer, start_);
    place_lines(available.height);

    Size desired = layout_.size();
    if (overflow_start_ != ui::detail::NoIndex)
    {
        desired.height = layout_.height_of(shown_);
    }
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void RichTextOverflowControl::arrange_override(const Rect&, const ArrangeContext&)
{
    if (!master_ || start_ == ui::detail::NoIndex || !master_->measurer_)
    {
        return;
    }
    TextLayoutOptions options = master_->layout_options_;
    options.max_width         = bounds().width;
    layout_.layout(options, *master_->measurer_, start_);
    const std::size_t before = overflow_start_;
    place_lines(bounds().height);
    if (overflow_start_ != before)
    {
        for (RichTextOverflowControl* o = next_; o != nullptr; o = o->next_)
        {
            o->mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        }
    }
}

void RichTextOverflowControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (!master_ || start_ == ui::detail::NoIndex || layout_.lines().empty())
    {
        return;
    }
    std::vector<TextHighlight> highlights;
    for (const TextHighlighter& h : master_->highlighters_)
    {
        for (const TextHighlighter::Range& r : h.ranges)
        {
            highlights.push_back(TextHighlight{r.start, r.start + r.length, h.background, h.foreground});
        }
    }
    TextLayout::Paint p;
    master_->paint(p, theme);
    p.highlights = highlights;
    p.last_line  = shown_;

    const bool clip = layout_.size().width > bounds().width + 0.5f || layout_.height_of(shown_) > bounds().height + 0.5f;
    if (clip)
    {
        renderer.push_clip(bounds());
    }
    layout_.render(renderer, Point{bounds().x, bounds().y}, p);
    if (clip)
    {
        renderer.pop_clip();
    }
}

bool RichTextOverflowControl::pointer_override(const PointerEvent& e)
{
    if (!master_ || start_ == ui::detail::NoIndex)
    {
        return false;
    }
    TextInteraction& interaction = master_->interaction_;
    const bool       held        = interaction.pressed_link.has_value() || interaction.dragging;
    if (interaction.pointer(e, layout_, Point{bounds().x, bounds().y}, 0, bounds(), master_->selection_enabled_, master_->links_, false))
    {
        master_->mark_chain_dirty();
    }
    return held || interaction.pressed_link.has_value() || interaction.dragging;
}

bool RichTextOverflowControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, false, false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (master_)
    {
        master_->handle_key(e);
    }
    return handled;
}

bool RichTextOverflowControl::handle_context_menu(std::optional<Point> position, ContextMenuHost& host)
{
    if (!master_)
    {
        return false;
    }
    return master_->handle_context_menu(position.value_or(Point{bounds().left(), bounds().bottom()}), host);
}

std::optional<CursorShape> RichTextControl::cursor_at(Point) const
{
    // IBeam while selectable (the default), Hand over a hyperlink.
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

std::optional<CursorShape> RichTextOverflowControl::cursor_at(Point p) const
{
    // An overflow mirrors its master's cursor.
    return master_ != nullptr ? master_->cursor_at(p) : std::nullopt;
}

std::unique_ptr<ui::Element> RichText::build() &&
{
    auto node = std::make_unique<ui::RichTextControl>();

    apply(blocks, *node, [raw = node.get()](const std::vector<Paragraph>& v) { raw->set_blocks(v); });
    apply(text_style,
          *node,
          [raw = node.get()](const std::optional<TextStyle>& v)
          {
              if (v)
              {
                  raw->set_text_style(*v);
              }
          });
    apply(wrapping, *node, [raw = node.get()](const ui::TextWrapping& v) { raw->set_wrapping(v); });
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
    apply(text_highlighters,
          *node,
          [raw = node.get()](const std::vector<ui::TextHighlighter>& value) { raw->set_text_highlighters(value); });
    apply(foreground, *node, [raw = node.get()](const Paint& v) { raw->set_foreground(v); });

    overflow.resolve(*node, [raw = node.get()](RichTextOverflowControl* next) { raw->set_overflow_target(next); });
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

std::unique_ptr<ui::Element> RichTextOverflow::build() &&
{
    auto node = std::make_unique<ui::RichTextOverflowControl>();
    overflow.resolve(*node, [raw = node.get()](RichTextOverflowControl* next) { raw->set_overflow_target(next); });
    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
