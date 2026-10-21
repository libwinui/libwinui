// SPDX-License-Identifier: MIT
#include "winui/ui/hosting/TitleBar.hpp"

#include "../WindowHost.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/ImageSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/media/Icon.hpp"
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

namespace ui
{

namespace
{
// TitleBar metrics.
constexpr float CompactBarHeight    = 32.0f; // the compact bar's height
constexpr float ExpandedHeight      = 48.0f; // the bar's height with a header or content
constexpr float TitleBarButtonWidth = 40.0f; // the back and pane-toggle buttons' width
// The buttons' 2px margin widens their column to TitleBarButtonWidth + 2 * CaptionButtonMargin. The
// button keeps its 40px width and is inset top and bottom as well.
constexpr float CaptionButtonMargin = 2.0f;
constexpr float ButtonColumnWidth   = TitleBarButtonWidth + 2.0f * CaptionButtonMargin;
constexpr float TitleBarIconSize    = 16.0f; // the icon's maximum width and height
constexpr float TitleBarIconGap     = 16.0f; // the gap right of the icon
constexpr float TitleBarTitleGap    = 8.0f;  // the gap right of the title
constexpr float SubtitleGap         = 16.0f; // the gap right of the subtitle
// The gap right of the content: it never runs into the right header.
constexpr float ContentGap = 16.0f;
// The gap left of the icon: always there, so the icon never sits on the edge. It narrows when
// exactly one of the back and pane-toggle buttons shows.
constexpr float LeftHeaderGap    = 14.0f; // no button or both
constexpr float LeftHeaderGapOne = 2.0f;  // one button
// The left padding. As the caption, both padding columns follow the window's title bar insets,
// set through set_caption_insets().
constexpr float LeftPadding = 2.0f;
// The minimum drag region: always somewhere to grab the window by. Only reserved as the caption,
// since an in-content bar drags nothing.
constexpr float MinDragRegionWidth = 48.0f;
// Outside a caption nothing sits right of the right header, so it keeps the gap the icon keeps from
// the left edge.
constexpr float RightPadding = LeftPadding + LeftHeaderGap;
// The opacity while the window is inactive.
constexpr float DeactivatedOpacity = 0.5f;

struct TitleBarPadding
{
    float left;
    float right; // the right padding column plus, as the caption, the min drag region
};

[[nodiscard]] TitleBarPadding padding_for(const CaptionInsets& insets)
{
    if (insets == CaptionInsets{})
    {
        return {LeftPadding, RightPadding};
    }
    return {insets.left, insets.right + MinDragRegionWidth};
}
} // namespace

TitleBarControl::TitleBarControl(std::string title, std::string subtitle) :
    title_(std::move(title)),
    subtitle_(std::move(subtitle)),
    back_button_(std::make_unique<ButtonControl>()),
    pane_toggle_button_(std::make_unique<ButtonControl>())
{
    set_parent_of(*back_button_, this);
    set_parent_of(*pane_toggle_button_, this);

    back_button_->set_variant(ButtonVariant::Subtle);
    pane_toggle_button_->set_variant(ButtonVariant::Subtle);
    // The buttons have no padding. The theme's text padding plus the focus-ring outset
    // would clip the icon inside the 40px button.
    back_button_->set_padding(Thickness{0.0f});
    pane_toggle_button_->set_padding(Thickness{0.0f});
    back_button_->set_content(
        std::make_unique<IconControl>(IconGlyph{glyphs::Back.codepoint, glyphs::Back.font_family, TitleBarIconSize}));
    pane_toggle_button_->set_content(std::make_unique<IconControl>(
        IconGlyph{glyphs::GlobalNavigationButton.codepoint, glyphs::GlobalNavigationButton.font_family, TitleBarIconSize}));

    back_button_->on_click(
        [this]
        {
            if (on_back_requested_)
            {
                on_back_requested_();
            }
        });
    pane_toggle_button_->on_click(
        [this]
        {
            if (on_pane_toggle_requested_)
            {
                on_pane_toggle_requested_();
            }
        });
}

Size TitleBarControl::measure_override(Size available, const MeasureContext& context) const
{
    // Compact unless any of the three arbitrary slots is present, regardless of whether icon/title/subtitle/buttons are shown.
    row_height_ = (content_ || left_header_ || right_header_) ? ExpandedHeight : CompactBarHeight;

    const TitleBarPadding padding = padding_for(caption_insets_);
    float                 x       = padding.left;

    const float button_height = std::max(0.0f, row_height_ - 2.0f * CaptionButtonMargin);
    if (back_button_visible_)
    {
        (void)back_button_->measure(Size{TitleBarButtonWidth, button_height}, context);
        x += ButtonColumnWidth;
    }
    if (pane_toggle_visible_)
    {
        (void)pane_toggle_button_->measure(Size{TitleBarButtonWidth, button_height}, context);
        x += ButtonColumnWidth;
    }

    left_header_w_ = 0.0f;
    if (left_header_)
    {
        const Size available_left{std::max(0.0f, available.width - x), row_height_};
        const Size desired_left = left_header_->measure(available_left, context);
        left_header_w_          = desired_left.width;
        left_header_h_          = std::min(desired_left.height, row_height_);
        x += left_header_w_;
    }
    x += back_button_visible_ == pane_toggle_visible_ ? LeftHeaderGap : LeftHeaderGapOne;

    if (icon_)
    {
        x += TitleBarIconSize + TitleBarIconGap;
    }

    // The title and the subtitle both use the Caption text style.
    title_w_ = 0.0f;
    if (!title_.empty())
    {
        title_w_ = context.measurer.measure(title_, context.theme.type.caption).width;
        x += title_w_ + TitleBarTitleGap;
    }

    subtitle_w_ = 0.0f;
    if (!subtitle_.empty())
    {
        subtitle_w_ = context.measurer.measure(subtitle_, context.theme.type.caption).width;
        x += subtitle_w_ + SubtitleGap;
    }

    right_header_w_ = 0.0f;
    if (right_header_)
    {
        const Size desired_right = right_header_->measure(Size{Unbounded, row_height_}, context);
        right_header_w_          = desired_right.width;
        right_header_h_          = std::min(desired_right.height, row_height_);
    }

    content_w_ = 0.0f;
    if (content_)
    {
        const Size desired_content = content_->measure(Size{Unbounded, row_height_}, context);
        content_w_                 = desired_content.width;
        content_h_                 = std::min(desired_content.height, row_height_);
    }

    const float gap           = content_ && right_header_ ? ContentGap : 0.0f;
    const float natural_width = x + content_w_ + gap + right_header_w_ + padding.right;
    Size        desired{std::isfinite(available.width) ? available.width : natural_width, row_height_};
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void TitleBarControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    const float top = final_rect.top();
    // The left header, the content and the right header are centered: a slot gets its desired
    // height, not the row's, so a TextBox there keeps its own height and caret.
    const auto            centered = [&](float height) { return top + std::round((row_height_ - height) * 0.5f); };
    const TitleBarPadding padding  = padding_for(caption_insets_);
    float                 x        = final_rect.left() + padding.left;

    const float button_height = std::max(0.0f, row_height_ - 2.0f * CaptionButtonMargin);
    if (back_button_visible_)
    {
        back_button_->arrange(Rect{x + CaptionButtonMargin, top + CaptionButtonMargin, TitleBarButtonWidth, button_height});
        x += ButtonColumnWidth;
    }
    if (pane_toggle_visible_)
    {
        pane_toggle_button_->arrange(
            Rect{x + CaptionButtonMargin, top + CaptionButtonMargin, TitleBarButtonWidth, button_height});
        x += ButtonColumnWidth;
    }
    if (left_header_)
    {
        left_header_->arrange(Rect{x, centered(left_header_h_), left_header_w_, left_header_h_});
        x += left_header_w_;
    }
    x += back_button_visible_ == pane_toggle_visible_ ? LeftHeaderGap : LeftHeaderGapOne;
    if (icon_)
    {
        icon_rect_ = Rect{x, top, TitleBarIconSize, row_height_};
        x += TitleBarIconSize + TitleBarIconGap;
    }
    // The right header keeps its place at the right end. When the bar is too narrow for everything,
    // the content gives up its room first, then the subtitle, then the title, each cut off.
    const float gap       = content_ && right_header_ ? ContentGap : 0.0f;
    const float right_x   = final_rect.right() - padding.right - right_header_w_;
    const float texts_end = right_x - gap;
    if (!title_.empty())
    {
        const float width = std::clamp(texts_end - x, 0.0f, title_w_);
        title_rect_       = Rect{x, top, width, row_height_};
        x += width + TitleBarTitleGap;
    }
    if (!subtitle_.empty())
    {
        const float width = std::clamp(texts_end - x, 0.0f, subtitle_w_);
        subtitle_rect_    = Rect{x, top, width, row_height_};
        x += width + SubtitleGap;
    }

    const float remaining = std::max(0.0f, texts_end - x);
    if (content_)
    {
        // The content's horizontal alignment: the content at its desired width unless it
        // stretches.
        const float wanted = std::min(content_w_, remaining);
        float       left   = x;
        float       width  = remaining;
        switch (content_alignment_)
        {
            case Alignment::Stretch:
                break;
            case Alignment::Leading:
                width = wanted;
                break;
            case Alignment::Center:
                width = wanted;
                left  = x + (remaining - wanted) * 0.5f;
                break;
            case Alignment::Trailing:
                width = wanted;
                left  = x + remaining - wanted;
                break;
        }
        content_->arrange(Rect{left, centered(content_h_), width, content_h_});
    }
    if (right_header_)
    {
        right_header_->arrange(Rect{right_x, centered(right_header_h_), right_header_w_, right_header_h_});
    }
}

namespace
{
// What takes a press on the element path under `p`. Element::title_bar_drag_region() decides when
// set. Otherwise an enabled control takes the press and anything else defers to the child under it.
HitRegion content_hit_region(const Element& element, Point p, bool parent_is_drag_region)
{
    if (element.visibility() != Visibility::Visible || !element.is_hit_test_visible())
    {
        return HitRegion::Caption;
    }
    bool drag_region = parent_is_drag_region;
    if (const std::optional<bool> own = element.title_bar_drag_region())
    {
        if (!*own)
        {
            return HitRegion::Client;
        }
        if (element.focusable())
        {
            return HitRegion::Caption;
        }
        drag_region = true;
    }
    if (!drag_region && element.focusable())
    {
        return HitRegion::Client;
    }
    std::vector<Element*> children;
    element.automation_children(children);
    for (auto it = children.rbegin(); it != children.rend(); ++it) // drawn last is on top
    {
        if (*it != nullptr && (*it)->bounds().contains(p))
        {
            return content_hit_region(**it, p, drag_region);
        }
    }
    return HitRegion::Caption;
}
} // namespace

HitRegion TitleBarControl::hit_region(Point p) const
{
    // What takes a press: the back button only while it can be clicked, the pane
    // toggle, and each header presenter whole.
    if (back_button_visible_ && back_button_->is_enabled() && back_button_->bounds().contains(p))
    {
        return HitRegion::Client;
    }
    if (pane_toggle_visible_ && pane_toggle_button_->bounds().contains(p))
    {
        return HitRegion::Client;
    }
    for (const Element* header : {left_header_.get(), right_header_.get()})
    {
        if (header != nullptr && header->visibility() == Visibility::Visible && header->bounds().contains(p))
        {
            return HitRegion::Client;
        }
    }
    if (content_ && content_->bounds().contains(p))
    {
        return content_hit_region(*content_, p, false);
    }
    return HitRegion::Caption;
}

void TitleBarControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    // One layer over everything rather than per part: at 0.5 it is both the slots' opacity and,
    // near enough, the tertiary text color over the primary one (see set_active()).
    const bool dimmed = !active_;
    if (dimmed)
    {
        renderer.push_opacity_layer(DeactivatedOpacity);
    }
    render_parts(renderer, theme);
    if (dimmed)
    {
        renderer.pop_opacity_layer();
    }
}

void TitleBarControl::render_parts(Renderer& renderer, const Theme& theme) const
{
    if (back_button_visible_)
    {
        back_button_->render(renderer, theme);
    }
    if (pane_toggle_visible_)
    {
        pane_toggle_button_->render(renderer, theme);
    }
    if (left_header_)
    {
        left_header_->render(renderer, theme);
    }

    icon_.draw_in_slot(renderer, icon_rect_, theme.colors().text, TitleBarIconSize, theme.icon_font_family);

    // The caption text style.
    TextStyle leading_style  = theme.type.caption;
    leading_style.horizontal = TextAlign::Leading;

    // Clipped: a bar too narrow for its texts cuts them off at the right header.
    if (!title_.empty() && title_rect_.width > 0.0f)
    {
        const ClipScope clip{renderer, title_rect_};
        renderer.draw_text(title_, title_rect_, theme.colors().text, leading_style);
    }
    if (!subtitle_.empty() && subtitle_rect_.width > 0.0f)
    {
        // The subtitle is in the secondary text color. Theme has no secondary-text token, so
        // colors.text is dimmed the way RatingControl and Slider do it.
        const ClipScope clip{renderer, subtitle_rect_};
        renderer.draw_text(subtitle_, subtitle_rect_, theme.colors().text.with_alpha(0.6f), leading_style);
    }

    if (content_)
    {
        content_->render(renderer, theme);
    }
    if (right_header_)
    {
        right_header_->render(renderer, theme);
    }
}

void TitleBarControl::set_caption_insets(std::optional<CaptionInsets> insets)
{
    caption_insets_set_ = insets;
    update_from_window();
}

void TitleBarControl::set_active(std::optional<bool> active)
{
    active_set_ = active;
    update_from_window();
}

void TitleBarControl::attached()
{
    Window* host = window();
    if (host == nullptr)
    {
        return;
    }
    followed_      = host;
    window_insets_ = host->caption_insets().observe([this](const CaptionInsets&) { update_from_window(); });
    window_active_ = host->active().observe([this](const bool&) { update_from_window(); });
    update_from_window();
}

void TitleBarControl::detached()
{
    followed_ = nullptr;
    window_insets_.reset();
    window_active_.reset();
    update_from_window();
}

void TitleBarControl::update_from_window()
{
    const Window* host = followed_;

    const CaptionInsets insets = caption_insets_set_.value_or(
        host != nullptr ? host->caption_insets().get() : CaptionInsets{});
    if (!(insets == caption_insets_))
    {
        caption_insets_ = insets;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
    const bool active = active_set_.value_or(host != nullptr ? host->active().get() : true);
    if (active != active_)
    {
        active_ = active;
        mark_dirty(DirtyFlags::Visual);
    }
}

void TitleBarControl::for_each_child(ChildVisitor visit) const
{
    if (back_button_visible_)
    {
        visit(*back_button_);
    }
    if (pane_toggle_visible_)
    {
        visit(*pane_toggle_button_);
    }
    for (Element* el : {left_header_.get(), content_.get(), right_header_.get()})
    {
        if (el != nullptr)
        {
            visit(*el);
        }
    }
}

std::unique_ptr<ui::Element> TitleBar::build() &&
{
    auto node = std::make_unique<ui::TitleBarControl>();

    apply(title, *node, [raw = node.get()](const std::string& v) { raw->set_title(v); });
    apply(subtitle, *node, [raw = node.get()](const std::string& v) { raw->set_subtitle(v); });
    apply(icon, *node, [raw = node.get()](const IconSource& v) { raw->set_icon(v); });
    apply(back_button_visible, *node, [raw = node.get()](const bool& v) { raw->set_back_button_visible(v); });
    apply(back_button_enabled, *node, [raw = node.get()](const bool& v) { raw->set_back_button_enabled(v); });
    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });
    apply(content_alignment, *node, [raw = node.get()](const Alignment& v) { raw->set_content_alignment(v); });
    apply(pane_toggle_button_visible,
          *node,
          [raw = node.get()](const bool& v) { raw->set_pane_toggle_button_visible(v); });
    apply(caption_insets,
          *node,
          [raw = node.get()](const std::optional<ui::CaptionInsets>& v) { raw->set_caption_insets(v); });
    apply(active, *node, [raw = node.get()](const std::optional<bool>& v) { raw->set_active(v); });

    if (on_back_requested)
    {
        node->on_back_requested(std::move(on_back_requested));
    }
    if (on_pane_toggle_requested)
    {
        node->on_pane_toggle_requested(std::move(on_pane_toggle_requested));
    }

    if (left_header_content)
    {
        node->set_left_header_content(left_header_content.take());
    }
    if (content)
    {
        node->set_content(content.take());
    }
    if (right_header_content)
    {
        node->set_right_header_content(right_header_content.take());
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
