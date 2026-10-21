// SPDX-License-Identifier: MIT
#include "winui/ui/navigation/Sections.hpp"

#include "../../render/Compositor.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
// Sections metrics.
constexpr float     TitleLeft              = 12.0f; // the title's padding 12,14,0,13
constexpr float     TitleTop               = 14.0f;
constexpr float     TitleBottom            = 13.0f;
constexpr float     SectionsTitleFontSize  = 14.0f; // the title, Bold
constexpr float     HeaderHeight           = 48.0f; // a header's height
constexpr float     SectionsHeaderPadding  = 12.0f; // a header's margin 12,0,12,0
constexpr float     SectionsHeaderFontSize = 24.0f; // a header's text, SemiLight
constexpr float     PipeHeight             = 3.0f;  // the selected header's bar: 3 tall, 2 above the bottom
constexpr float     PipeBottom             = 2.0f;
constexpr float     PipeRadius             = 1.5f;  // the bar's corner radius
constexpr float     SectionMargin          = 12.0f; // a section's margin 12,0,12,0
constexpr float     NavButtonWidth         = 20.0f; // the previous/next buttons: 20x36, margin 0,6,0,0
constexpr float     NavButtonHeight        = 36.0f;
constexpr float     NavButtonTop           = 6.0f;
constexpr IconGlyph PreviousGlyph{0xE76B, "Segoe Fluent Icons", 12.0f};
constexpr IconGlyph NextGlyph{0xE76C, "Segoe Fluent Icons", 12.0f};

// The opacities of the header and button colors: 0.4, 0.6 and 0.8 of the base color, and 0.8
// of the alt color.
constexpr float TextAlpha40 = 0.4f;
constexpr float TextAlpha60 = 0.6f;
constexpr float TextAlpha80 = 0.8f;

// The content's fly-out and fly-in.
constexpr double      FlyOutMs        = 83.0;
constexpr double      FlyOutOpacityMs = 67.0;
constexpr float       FlyOutDistance  = 7.0f;
constexpr double      FlyInMs         = 767.0;
constexpr double      FlyInOpacityMs  = 333.0;
constexpr float       FlyInDistance   = 20.0f;
constexpr CubicBezier FlyInSpline{0.1f, 0.9f, 0.2f, 1.0f};
} // namespace

// ---- items ----

void SectionsControl::add_item(SectionsControlItem item)
{
    if (item.content)
    {
        set_parent_of(*item.content, this);
    }
    items_.push_back(std::move(item));
    if (selected_index_ == detail::NoIndex)
    {
        selected_index_ = 0;
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

const std::string& SectionsControl::header(std::size_t index) const
{
    static const std::string empty;
    return index < items_.size() ? items_[index].header : empty;
}

void SectionsControl::set_selected_index(std::size_t index)
{
    if (items_.empty())
    {
        return;
    }
    select(std::min(index, items_.size() - 1), 0);
}

void SectionsControl::select(std::size_t index, int direction)
{
    if (index >= items_.size() || index == selected_index_)
    {
        return;
    }
    // Fly the old content out and the new one in, from the side the selection
    // moved to. Only with a frame clock; without one the new content simply shows.
    if (ticking_ && selected_index_ != detail::NoIndex)
    {
        leaving_index_ = selected_index_;
        direction_     = direction != 0 ? direction : (index > selected_index_ ? 1 : -1);
        clock_ms_      = 0.0;
    }
    selected_index_ = index;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(selected_index_);
    }
}

void SectionsControl::move(int delta)
{
    if (items_.empty())
    {
        return;
    }
    const std::size_t n    = items_.size();
    const std::size_t from = selected_index_ < n ? selected_index_ : 0;
    // Only the carousel wraps.
    const bool wrap = !static_headers_;
    if (delta > 0)
    {
        if (from + 1 < n)
        {
            select(from + 1, +1);
        }
        else if (wrap)
        {
            select(0, +1);
        }
    }
    else if (delta < 0)
    {
        if (from > 0)
        {
            select(from - 1, -1);
        }
        else if (wrap)
        {
            select(n - 1, -1);
        }
    }
}

void SectionsControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_header_ = pressed_header_ = detail::NoIndex;
        header_hovered_                   = false;
        nav_hovered_ = nav_pressed_ = 0;
    }
    mark_dirty(DirtyFlags::Visual);
}

void SectionsControl::focus_changed(bool /*focused*/)
{
    mark_dirty(DirtyFlags::Visual);
}

// ---- layout ----

TextStyle SectionsControl::header_style(const Theme& theme)
{
    TextStyle style  = theme.text_style;
    style.font_size  = SectionsHeaderFontSize;
    style.weight     = FontWeight::SemiLight;
    style.horizontal = TextAlign::Leading;
    return style;
}

Size SectionsControl::measure_override(Size available, const MeasureContext& context) const
{
    header_widths_.clear();
    float headers = 0.0f;
    for (const SectionsControlItem& item : items_)
    {
        const float w = std::ceil(context.measurer.measure(item.header, header_style(context.theme)).width);
        header_widths_.push_back(w);
        headers += w + 2.0f * SectionsHeaderPadding;
    }
    TextStyle title = context.theme.text_style;
    title.font_size = SectionsTitleFontSize;
    title.weight    = FontWeight::Bold;
    title_height_   = title_.empty() ? 0.0f : context.measurer.measure(title_, title).height;

    left_desired_  = left_header_ ? left_header_->measure(Size{Unbounded, HeaderHeight}, context) : Size{};
    right_desired_ = right_header_ ? right_header_->measure(Size{Unbounded, HeaderHeight}, context) : Size{};

    const float top = (title_.empty() ? 0.0f : TitleTop + title_height_ + TitleBottom) + HeaderHeight;
    const Size  offer{std::max(0.0f, available.width - 2.0f * SectionMargin), std::max(0.0f, available.height - top)};
    Size        content_desired{};
    if (Element* c = content(selected_index_); c != nullptr)
    {
        content_desired = c->measure(offer, context);
    }
    Size desired{std::max(left_desired_.width + headers + right_desired_.width, content_desired.width + 2.0f * SectionMargin),
                 top + content_desired.height};
    if (std::isfinite(available.width))
    {
        desired.width = available.width;
    }
    if (std::isfinite(available.height))
    {
        desired.height = available.height;
    }
    return desired;
}

void SectionsControl::arrange_override(const Rect& r, const ArrangeContext&)
{
    float y     = r.y;
    title_rect_ = Rect{};
    if (!title_.empty())
    {
        title_rect_ = Rect{r.x + TitleLeft, y + TitleTop, std::max(0.0f, r.width - TitleLeft), title_height_};
        y += TitleTop + title_height_ + TitleBottom;
    }
    if (left_header_)
    {
        left_header_->arrange(Rect{r.x, y, left_desired_.width, HeaderHeight});
    }
    if (right_header_)
    {
        right_header_->arrange(Rect{r.right() - right_desired_.width, y, right_desired_.width, HeaderHeight});
    }
    header_row_   = Rect{r.x + left_desired_.width,
                         y,
                         std::max(0.0f, r.width - left_desired_.width - right_desired_.width),
                         HeaderHeight};
    content_rect_ = Rect{r.x + SectionMargin,
                         y + HeaderHeight,
                         std::max(0.0f, r.width - 2.0f * SectionMargin),
                         std::max(0.0f, r.bottom() - y - HeaderHeight)};

    // The headers: in order when they fit (or the carousel is off), else starting from the
    // selected one and wrapping round.
    float total = 0.0f;
    for (float w : header_widths_)
    {
        total += w + 2.0f * SectionsHeaderPadding;
    }
    static_headers_ = total <= header_row_.width || !carousel_enabled_;
    slots_.clear();
    const std::size_t n = items_.size();
    if (n > 0 && selected_index_ < n)
    {
        if (static_headers_)
        {
            // Carousel off and too wide: keep the selected header in view.
            float x_sel = 0.0f;
            for (std::size_t i = 0; i < selected_index_; ++i)
            {
                x_sel += header_widths_[i] + 2.0f * SectionsHeaderPadding;
            }
            const float w_sel = header_widths_[selected_index_] + 2.0f * SectionsHeaderPadding;
            if (total <= header_row_.width)
            {
                static_scroll_ = 0.0f;
            }
            else if (x_sel < static_scroll_)
            {
                static_scroll_ = x_sel;
            }
            else if (x_sel + w_sel > static_scroll_ + header_row_.width)
            {
                static_scroll_ = x_sel + w_sel - header_row_.width;
            }
            float x = header_row_.x - static_scroll_;
            for (std::size_t i = 0; i < n; ++i)
            {
                const float w = header_widths_[i] + 2.0f * SectionsHeaderPadding;
                slots_.push_back(Slot{i, Rect{x, y, w, HeaderHeight}});
                x += w;
            }
        }
        else
        {
            float x = header_row_.x;
            for (std::size_t k = 0; k < n && x < header_row_.right(); ++k)
            {
                const std::size_t i = (selected_index_ + k) % n;
                const float       w = header_widths_[i] + 2.0f * SectionsHeaderPadding;
                slots_.push_back(Slot{i, Rect{x, y, w, HeaderHeight}});
                x += w;
            }
        }
    }

    for (std::size_t i = 0; i < n; ++i)
    {
        if (Element* c = content(i); c != nullptr && (i == selected_index_ || i == leaving_index_))
        {
            c->arrange(content_rect_);
        }
    }
}

std::optional<Rect> SectionsControl::header_bounds(std::size_t index) const
{
    for (const Slot& slot : slots_)
    {
        if (slot.item == index && slot.rect.right() > header_row_.x && slot.rect.x < header_row_.right())
        {
            return slot.rect;
        }
    }
    return std::nullopt;
}

std::optional<Rect> SectionsControl::previous_button_bounds() const
{
    // The navigation buttons show while the header row is hovered, and in
    // static mode only when there is somewhere to go.
    if (!header_hovered_ || items_.size() < 2 || (static_headers_ && selected_index_ == 0))
    {
        return std::nullopt;
    }
    return Rect{header_row_.x, header_row_.y + NavButtonTop, NavButtonWidth, NavButtonHeight};
}

std::optional<Rect> SectionsControl::next_button_bounds() const
{
    if (!header_hovered_ || items_.size() < 2 || (static_headers_ && selected_index_ + 1 >= items_.size()))
    {
        return std::nullopt;
    }
    return Rect{header_row_.right() - NavButtonWidth, header_row_.y + NavButtonTop, NavButtonWidth, NavButtonHeight};
}

std::size_t SectionsControl::header_at(Point p) const
{
    if (!header_row_.contains(p))
    {
        return detail::NoIndex;
    }
    for (const Slot& slot : slots_)
    {
        if (slot.rect.contains(p))
        {
            return slot.item;
        }
    }
    return detail::NoIndex;
}

// ---- render ----

void SectionsControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    const Color  text  = theme.colors().text;
    if (!title_.empty())
    {
        TextStyle style  = theme.text_style;
        style.font_size  = SectionsTitleFontSize;
        style.weight     = FontWeight::Bold;
        style.horizontal = TextAlign::Leading;
        renderer.draw_text(title_, title_rect_, is_enabled() ? text : theme.colors().text_disabled, style);
    }

    renderer.push_clip(header_row_);
    for (const Slot& slot : slots_)
    {
        const bool selected = slot.item == selected_index_;
        const bool hovered  = slot.item == hovered_header_;
        const bool pressed  = slot.item == pressed_header_ && hovered;
        // A header's text: 0.6 of the base color unselected, 1.0 selected, 0.8 for any hovered or
        // pressed header, 0.4 disabled.
        float alpha = selected ? 1.0f : TextAlpha60;
        if (hovered || pressed)
        {
            alpha = TextAlpha80;
        }
        const Color color = is_enabled() ? text.with_alpha(text.a * alpha) : text.with_alpha(text.a * TextAlpha40);
        const float w     = header_widths_[slot.item];
        renderer.draw_text(items_[slot.item].header,
                           Rect{slot.rect.x + SectionsHeaderPadding, slot.rect.y, w + 1.0f, slot.rect.height},
                           color,
                           header_style(theme));
        if (selected && is_enabled())
        {
            renderer.fill_rounded_rect(Rect{slot.rect.x + SectionsHeaderPadding,
                                            slot.rect.bottom() - PipeBottom - PipeHeight,
                                            w,
                                            PipeHeight},
                                       PipeRadius,
                                       theme.colors().accent);
        }
    }

    // The previous and next buttons.
    const auto nav = [&](const std::optional<Rect>& rect, int which, const IconGlyph& glyph)
    {
        if (!rect)
        {
            return;
        }
        const float alpha = nav_pressed_ == which && nav_hovered_ == which ? TextAlpha80
                            : nav_hovered_ == which                        ? TextAlpha60
                                                                           : TextAlpha40;
        renderer.fill_rounded_rect(*rect, 0.0f, text.with_alpha(text.a * alpha));
        draw_icon(renderer, glyph, *rect, theme.colors().background.with_alpha(TextAlpha80), glyph.size, theme.icon_font_family);
    };
    nav(previous_button_bounds(), -1, PreviousGlyph);
    nav(next_button_bounds(), +1, NextGlyph);
    renderer.pop_clip();

    if (left_header_)
    {
        left_header_->render(renderer, context);
    }
    if (right_header_)
    {
        right_header_->render(renderer, context);
    }

    // The content, flying out and in.
    if (clock_ms_ >= 0.0 && clock_ms_ < FlyOutMs && leaving_index_ != detail::NoIndex)
    {
        if (Element* old = content(leaving_index_); old != nullptr)
        {
            const float t       = static_cast<float>(clock_ms_ / FlyOutMs);
            const float opacity = 1.0f - std::min(1.0f, static_cast<float>(clock_ms_ / FlyOutOpacityMs));
            renderer.push_clip(Rect{bounds().x, content_rect_.y, bounds().width, content_rect_.height});
            renderer.push_opacity_layer(opacity);
            renderer.push_transform(1.0f, Point{-static_cast<float>(direction_) * FlyOutDistance * t, 0.0f});
            old->render(renderer, context);
            renderer.pop_transform();
            renderer.pop_opacity_layer();
            renderer.pop_clip();
        }
        return;
    }
    if (Element* c = content(selected_index_); c != nullptr)
    {
        if (clock_ms_ >= FlyOutMs)
        {
            const double in      = clock_ms_ - FlyOutMs;
            const float  moved   = easing::evaluate(FlyInSpline, in / FlyInMs);
            const float  opacity = easing::evaluate(FlyInSpline, in / FlyInOpacityMs);
            renderer.push_clip(Rect{bounds().x, content_rect_.y, bounds().width, content_rect_.height});
            renderer.push_opacity_layer(opacity);
            renderer.push_transform(1.0f, Point{static_cast<float>(direction_) * FlyInDistance * (1.0f - moved), 0.0f});
            c->render(renderer, context);
            renderer.pop_transform();
            renderer.pop_opacity_layer();
            renderer.pop_clip();
        }
        else
        {
            c->render(renderer, context);
        }
    }
}

// ---- input ----

bool SectionsControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    const bool in_row   = e.action != PointerAction::Leave && header_row_.contains(e.position);
    int        over_nav = 0;
    if (const auto prev = previous_button_bounds(); prev && prev->contains(e.position))
    {
        over_nav = -1;
    }
    else if (const auto next = next_button_bounds(); next && next->contains(e.position))
    {
        over_nav = +1;
    }
    const std::size_t over = over_nav != 0 ? detail::NoIndex : header_at(e.position);
    switch (e.action)
    {
        case PointerAction::Move:
            if (in_row != header_hovered_ || over != hovered_header_ || over_nav != nav_hovered_)
            {
                header_hovered_ = in_row;
                hovered_header_ = over;
                nav_hovered_    = over_nav;
                mark_dirty(DirtyFlags::Visual);
            }
            break;
        case PointerAction::Down:
            if (e.button == PointerButton::Left && in_row)
            {
                if (over_nav != 0)
                {
                    nav_pressed_ = over_nav;
                }
                else if (over != detail::NoIndex)
                {
                    pressed_header_ = over;
                    // A header after the selected one in the row is "next" (the carousel puts them
                    // all after it).
                    const Rect sel = header_bounds(selected_index_).value_or(Rect{});
                    const Rect hit = header_bounds(over).value_or(Rect{});
                    select(over, hit.x >= sel.x ? +1 : -1);
                }
                mark_dirty(DirtyFlags::Visual);
                return over_nav != 0 || over != detail::NoIndex;
            }
            break;
        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                if (nav_pressed_ != 0)
                {
                    const int which = nav_pressed_;
                    nav_pressed_    = 0;
                    if (which == over_nav)
                    {
                        move(which);
                    }
                    mark_dirty(DirtyFlags::Visual);
                    return true;
                }
                const bool held = pressed_header_ != detail::NoIndex;
                pressed_header_ = detail::NoIndex;
                mark_dirty(DirtyFlags::Visual);
                return held;
            }
            break;
        case PointerAction::Leave:
            header_hovered_ = false;
            hovered_header_ = pressed_header_ = detail::NoIndex;
            nav_hovered_ = nav_pressed_ = 0;
            mark_dirty(DirtyFlags::Visual);
            break;
        default:
            break;
    }
    return false;
}

void SectionsControl::hit_test_children(Point p, std::vector<Element*>& out) const
{
    for (Element* el : {right_header_.get(), left_header_.get()})
    {
        if (el != nullptr)
        {
            out.push_back(el);
        }
    }
    // The content only where it shows: while it slides in it reaches under the header row.
    if (Element* c = content(selected_index_); c != nullptr && content_rect_.contains(p))
    {
        out.push_back(c);
    }
}

bool SectionsControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, takes_arrow_key(e.key), false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused() || e.action != KeyAction::Down || items_.empty())
    {
        return handled;
    }
    switch (e.key)
    {
        case Key::ArrowLeft:
            move(-1);
            break;
        case Key::ArrowRight:
            move(+1);
            break;
        case Key::Home:
            select(0, -1);
            break;
        case Key::End:
            select(items_.size() - 1, +1);
            break;
        default:
            break;
    }
    return handled;
}

bool SectionsControl::handle_shortcut(const KeyEvent& e)
{
    // Ctrl+Tab / Ctrl+Shift+Tab and Ctrl+PageDown / Ctrl+PageUp, from anywhere
    // inside it.
    if (is_enabled() && e.modifiers.ctrl && !items_.empty())
    {
        if (e.key == Key::Tab)
        {
            move(e.modifiers.shift ? -1 : +1);
            return true;
        }
        if (e.key == Key::PageDown || e.key == Key::PageUp)
        {
            move(e.key == Key::PageDown ? +1 : -1);
            return true;
        }
    }
    return Element::handle_shortcut(e);
}

// ---- focus and plumbing ----

void SectionsControl::collect_focusables(std::vector<Element*>& out)
{
    if (left_header_)
    {
        left_header_->collect_focusables(out);
    }
    if (focusable())
    {
        out.push_back(this);
    }
    if (right_header_)
    {
        right_header_->collect_focusables(out);
    }
    if (Element* c = content(selected_index_); c != nullptr)
    {
        c->collect_focusables(out);
    }
}

void SectionsControl::for_each_child(ChildVisitor visit) const
{
    for (Element* el : {left_header_.get(), right_header_.get(), content(selected_index_)})
    {
        if (el != nullptr)
        {
            visit(*el);
        }
    }
}

void SectionsControl::clear_dirty_override(DirtyFlags flags)
{
    if (Element* leaving = content(leaving_index_); leaving != nullptr)
    {
        clear_dirty_of(*leaving, flags);
    }
}

void SectionsControl::tick_override(Duration delta)
{
    ticking_ = true;
    if (clock_ms_ >= 0.0)
    {
        clock_ms_ += animation_delta(delta).count();
        if (clock_ms_ >= FlyOutMs + FlyInMs)
        {
            clock_ms_      = -1.0;
            leaving_index_ = detail::NoIndex;
        }
        mark_dirty(DirtyFlags::Visual);
    }
}

std::unique_ptr<ui::Element> Sections::build() &&
{
    auto node = std::make_unique<ui::SectionsControl>();
    for (auto& item : items.entries)
    {
        node->add_item(ui::SectionsControlItem{std::move(item.header), std::move(item.node)});
    }
    if (left_header_content)
    {
        node->set_left_header_content(left_header_content.take());
    }
    if (right_header_content)
    {
        node->set_right_header_content(right_header_content.take());
    }
    apply(header_carousel_enabled, *node, [raw = node.get()](const bool& v) { raw->set_header_carousel_enabled(v); });

    ui::SectionsControl* raw = node.get();
    apply(title, *node, [raw](const std::string& value) { raw->set_title(value); });
    apply(selected_index, *node, [raw](const std::size_t& index) { raw->set_selected_index(index); });
    apply(enabled, *node, [raw](const bool& value) { raw->set_enabled(value); });

    if (auto changed = ui::write_back(selected_index, std::move(on_selection_changed)))
    {
        node->on_selection_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
