// SPDX-License-Identifier: MIT
#include "winui/ui/status/InfoBar.hpp"

#include "../../render/ControlGlyphs.hpp"
#include "../LocalText.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
constexpr float InfoBarHeightFloor = 48.0f; // the bar's minimum height
constexpr float ContentPaddingLeft = 16.0f; // the bar's left padding
constexpr float InfoBarIconSize    = 16.0f; // the severity icon's size
constexpr float IconMarginTop      = 16.0f; // the icon's 0,16,14,16 margin
constexpr float IconGapRight       = 14.0f;
constexpr float PanelRightMargin   = 16.0f; // the panel's right margin
constexpr float CloseButtonSize    = 38.0f; // the close button, 38 square
constexpr float CloseButtonMargin  = 5.0f;  // the close button's margin
constexpr float CloseButtonGlyph   = 16.0f; // the close glyph's size
constexpr float BarTitleFontSize   = 14.0f; // the title's font size
constexpr float MessageFontSize    = 14.0f; // the message's font size

// The panel's paddings per orientation and each item's two margins (left and top only: every
// right and bottom margin is 0, and the horizontal padding is 0).
constexpr float VerticalPaddingTop    = 14.0f; // the vertical panel's 0,14,0,18 padding
constexpr float VerticalPaddingBottom = 18.0f;
constexpr Point TitleHorizontalMargin{0.0f, 14.0f};    // the title's margin in a row
constexpr Point MessageHorizontalMargin{12.0f, 14.0f}; // the message's margin in a row
constexpr Point ActionHorizontalMargin{16.0f, 8.0f};   // the action's margin in a row
constexpr float TitleVerticalMarginTop   = 14.0f;      // the title's margin in a column
constexpr float MessageVerticalMarginTop = 4.0f;       // the message's margin in a column
constexpr float ActionVerticalMarginTop  = 12.0f;      // the action's margin in a column
constexpr float HyperlinkMarginLeft      = -12.0f;     // a hyperlink action's left margin

TextStyle title_style(const Theme& theme)
{
    TextStyle style  = theme.text_style;
    style.horizontal = TextAlign::Leading;
    style.vertical   = TextAlign::Leading;
    style.font_size  = BarTitleFontSize;
    style.weight     = FontWeight::SemiBold;
    style.wrapping   = TextWrapping::WrapWholeWords;
    return style;
}

TextStyle message_style(const Theme& theme)
{
    TextStyle style  = theme.text_style;
    style.horizontal = TextAlign::Leading;
    style.vertical   = TextAlign::Leading;
    style.font_size  = MessageFontSize;
    style.wrapping   = TextWrapping::WrapWholeWords;
    return style;
}

Size measure_bar_text(const TextMeasurer& measurer, const std::string& text, const TextStyle& style, float max_width)
{
    if (text.empty())
    {
        return {};
    }
    return std::isfinite(max_width) ? measurer.measure_wrapped(text, style, max_width) : measurer.measure(text, style);
}

bool has_size(const Size& size)
{
    return size.width != 0.0f && size.height != 0.0f; // the panel skips a zero-sized child
}

// `Theme::severity`, not `theme.palette`: only Theme::fluent() fills the palette, so every other
// theme would draw an opaque black bar.
const Theme::SeverityVisual& severity_visual(const Theme& theme, InfoBarSeverity severity)
{
    switch (severity)
    {
        case InfoBarSeverity::Success:
            return theme.severity.success;
        case InfoBarSeverity::Warning:
            return theme.severity.caution;
        case InfoBarSeverity::Error:
            return theme.severity.critical;
        case InfoBarSeverity::Informational:
            break;
    }
    return theme.severity.informational;
}

Color icon_background(const Theme& theme, InfoBarSeverity severity)
{
    return severity_visual(theme, severity).fill;
}

Color body_background(const Theme& theme, InfoBarSeverity severity)
{
    return severity_visual(theme, severity).background;
}

const IconGlyph& severity_glyph(InfoBarSeverity severity)
{
    switch (severity)
    {
        case InfoBarSeverity::Success:
            return glyphs::InfoBarSuccess;
        case InfoBarSeverity::Warning:
            return glyphs::InfoBarWarning;
        case InfoBarSeverity::Error:
            return glyphs::InfoBarError;
        case InfoBarSeverity::Informational:
            break;
    }
    return glyphs::InfoBarInformational;
}
} // namespace

InfoBarControl::InfoBarControl() : close_button_(std::make_unique<ButtonControl>())
{
    set_parent_of(*close_button_, this);
    close_button_->set_variant(ButtonVariant::Subtle);
    // The close button has no padding of its own: text-button padding would clip the 16px glyph
    // inside the 38px button.
    close_button_->set_padding(Thickness{0.0f});
    close_button_->set_content(
        std::make_unique<IconControl>(IconGlyph{glyphs::Cancel.codepoint, glyphs::Cancel.font_family, CloseButtonGlyph}));
    close_button_->on_click([this] { close(PopupCloseReason::CloseButton); });
    detail::follow_strings(*this,
                           [this] { close_button_->set_accessibility(Accessibility{.name = strings().common.close}); });
}

InfoBarControl::InfoBarControl(std::string title, std::string message, InfoBarSeverity severity, bool is_open) :
    InfoBarControl()
{
    title_    = std::move(title);
    message_  = std::move(message);
    severity_ = severity;
    is_open_  = is_open;
}

void InfoBarControl::set_title(std::string title)
{
    if (title == title_)
    {
        return;
    }
    title_ = std::move(title);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void InfoBarControl::set_message(std::string message)
{
    if (message == message_)
    {
        return;
    }
    message_ = std::move(message);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void InfoBarControl::set_severity(InfoBarSeverity severity)
{
    if (severity == severity_)
    {
        return;
    }
    severity_ = severity;
    mark_dirty(DirtyFlags::Visual);
}

void InfoBarControl::set_icon_visible(bool visible)
{
    if (visible == is_icon_visible_)
    {
        return;
    }
    is_icon_visible_ = visible;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void InfoBarControl::set_closable(bool closable)
{
    if (closable == is_closable_)
    {
        return;
    }
    is_closable_ = closable;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void InfoBarControl::enabled_changed()
{
    close_button_->set_enabled(is_enabled());
    mark_dirty(DirtyFlags::Visual);
}

void InfoBarControl::set_open(bool is_open)
{
    if (is_open == is_open_)
    {
        return;
    }
    if (is_open)
    {
        is_open_ = true;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
        return;
    }
    close(PopupCloseReason::Programmatic);
}

void InfoBarControl::close(PopupCloseReason reason)
{
    if (!is_open_)
    {
        return;
    }
    if (reason == PopupCloseReason::CloseButton && on_close_button_click_)
    {
        on_close_button_click_();
    }
    PopupClosingArgs args{.reason = reason};
    bool             canceled = false;
    if (on_closing_)
    {
        canceled = detail::canceled(on_closing_(args));
    }
    if (canceled)
    {
        return; // is_open_ is left untouched
    }
    is_open_ = false;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (on_closed_)
    {
        on_closed_(reason);
    }
}

void InfoBarControl::clear_action_button_content()
{
    if (!action_)
    {
        return;
    }
    action_.reset();
    action_rect_ = Rect{};
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void InfoBarControl::clear_content()
{
    if (!content_)
    {
        return;
    }
    content_.reset();
    content_rect_ = Rect{};
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

Size InfoBarControl::measure_override(Size available, const MeasureContext& context) const
{
    if (!is_open_)
    {
        title_size_ = message_size_ = action_size_ = content_size_ = Size{};
        first_row_height_                                          = 0.0f;
        return Size{0.0f, 0.0f};
    }

    const float fixed_left  = ContentPaddingLeft + (is_icon_visible_ ? InfoBarIconSize + IconGapRight : 0.0f);
    const float close_width = is_closable_ ? CloseButtonSize + 2.0f * CloseButtonMargin : 0.0f;
    // The panel and the content share a star column, offered whatever the fixed columns leave;
    // the panel keeps the right 16 of it clear, the content does not.
    const float column_available = std::max(1.0f, available.width - fixed_left - close_width);
    const float panel_available  = std::max(1.0f, column_available - PanelRightMargin);

    title_size_        = measure_bar_text(context.measurer, title_, title_style(context.theme), panel_available);
    message_size_      = measure_bar_text(context.measurer, message_, message_style(context.theme), panel_available);
    action_size_       = Size{};
    const auto* button = dynamic_cast<const ButtonControl*>(action_.get());
    action_hyperlink_  = button != nullptr && button->variant() == ButtonVariant::Hyperlink;
    if (action_)
    {
        action_size_ = action_->measure(Size{panel_available, Unbounded}, context);
        if (action_hyperlink_)
        {
            action_size_.width = std::max(0.0f, action_size_.width + HyperlinkMarginLeft);
        }
    }

    // The content panel's measure.
    struct Item
    {
        Size  size;
        Point horizontal_margin;
        float vertical_margin_top;
    };
    const Item items[]     = {{title_size_, TitleHorizontalMargin, TitleVerticalMarginTop},
                              {message_size_, MessageHorizontalMargin, MessageVerticalMarginTop},
                              {action_size_, ActionHorizontalMargin, ActionVerticalMarginTop}};
    float      total_width = 0.0f, total_height = 0.0f, widest = 0.0f, tallest = 0.0f, tallest_in_horizontal = 0.0f;
    int        count = 0;
    for (const Item& item : items)
    {
        if (!has_size(item.size))
        {
            continue;
        }
        total_width += item.size.width + (count > 0 ? item.horizontal_margin.x : 0.0f);
        total_height += item.size.height + (count > 0 ? item.vertical_margin_top : 0.0f);
        widest                = std::max(widest, item.size.width);
        tallest               = std::max(tallest, item.size.height);
        tallest_in_horizontal = std::max(tallest_in_horizontal, item.size.height + item.horizontal_margin.y);
        ++count;
    }
    vertical_        = count == 1 || total_width > panel_available || tallest_in_horizontal > InfoBarHeightFloor;
    const Size panel = vertical_ ? Size{widest, total_height + VerticalPaddingTop + VerticalPaddingBottom}
                                 : Size{total_width, tallest};

    if (close_button_active())
    {
        (void)close_button_->measure(Size{CloseButtonSize, CloseButtonSize}, context);
    }

    content_size_ = content_ ? content_->measure(Size{column_available, Unbounded}, context) : Size{};

    // The layout grid: the icon (16 + 16 above and below), the panel and the close button
    // (38 + 5 above and below) share the first row; the content takes a second row, or moves into
    // the first when there is no title, message or action. A row of
    // title and message counts with its 14 top margin, so content never overlaps it.
    has_banner_           = !title_.empty() || !message_.empty() || action_ != nullptr;
    const float icon_row  = is_icon_visible_ ? IconMarginTop * 2.0f + InfoBarIconSize : 0.0f;
    const float close_row = is_closable_ ? CloseButtonSize + 2.0f * CloseButtonMargin : 0.0f;
    const float panel_row = vertical_ ? panel.height : tallest_in_horizontal;
    first_row_height_     = std::max({icon_row, close_row, panel_row, has_banner_ ? 0.0f : content_size_.height});
    const float rows      = first_row_height_ + (has_banner_ ? content_size_.height : 0.0f);
    const float column    = std::max(panel.width + PanelRightMargin, content_size_.width);

    Size desired{fixed_left + column + close_width, std::max(InfoBarHeightFloor, rows)};
    desired.width = std::min(desired.width, available.width);
    return desired;
}

void InfoBarControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    if (!is_open_)
    {
        icon_rect_ = title_rect_ = message_rect_ = action_rect_ = content_rect_ = close_button_rect_ = Rect{};
        return;
    }

    float x = final_rect.left() + ContentPaddingLeft;
    if (is_icon_visible_)
    {
        icon_rect_ = Rect{x, final_rect.top() + IconMarginTop, InfoBarIconSize, InfoBarIconSize};
        x += InfoBarIconSize + IconGapRight;
    }
    else
    {
        icon_rect_ = Rect{};
    }

    float panel_right = final_rect.right() - PanelRightMargin;
    if (close_button_active())
    {
        close_button_rect_ = Rect{final_rect.right() - CloseButtonMargin - CloseButtonSize,
                                  final_rect.top() + CloseButtonMargin,
                                  CloseButtonSize,
                                  CloseButtonSize};
        close_button_->arrange(close_button_rect_);
        panel_right = close_button_rect_.left() - CloseButtonMargin - PanelRightMargin;
    }
    else
    {
        close_button_rect_ = Rect{};
    }

    // The content panel's arrange. Each item keeps its measured size; in a row the last child
    // (the action slot) also gets whatever width is left.
    Rect* const rects[]    = {&title_rect_, &message_rect_, &action_rect_};
    const Size  sizes[]    = {title_size_, message_size_, action_size_};
    const Point h_margin[] = {TitleHorizontalMargin, MessageHorizontalMargin, ActionHorizontalMargin};
    const float v_margin[] = {TitleVerticalMarginTop, MessageVerticalMarginTop, ActionVerticalMarginTop};
    bool        previous   = false;
    float       cursor     = vertical_ ? final_rect.top() + VerticalPaddingTop : x;
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (!has_size(sizes[i]))
        {
            *rects[i] = Rect{};
            continue;
        }
        if (vertical_)
        {
            cursor += previous ? v_margin[i] : 0.0f;
            *rects[i] = Rect{x, cursor, sizes[i].width, sizes[i].height};
            cursor += sizes[i].height;
        }
        else
        {
            cursor += previous ? h_margin[i].x : 0.0f;
            const float width = i == 2 ? std::max(sizes[i].width, panel_right - cursor) : sizes[i].width;
            *rects[i]         = Rect{cursor, final_rect.top() + h_margin[i].y, width, sizes[i].height};
            cursor += sizes[i].width;
        }
        previous = true;
    }

    if (action_active())
    {
        // The action keeps its own width (its slot does not stretch a Button), shifted left by
        // the hyperlink margin when it has one.
        const float shift = action_hyperlink_ ? HyperlinkMarginLeft : 0.0f;
        action_->arrange(Rect{action_rect_.x + shift, action_rect_.y, action_size_.width - shift, action_rect_.height});
    }

    if (content_active())
    {
        // The whole star column, without the panel's right margin. In its own row the slot is as
        // tall as the content; moved up into the first row it is centered there.
        const float column_width = std::max(0.0f, panel_right + PanelRightMargin - x);
        const float top          = has_banner_ ? final_rect.top() + first_row_height_
                                               : final_rect.top() + (first_row_height_ - content_size_.height) * 0.5f;
        content_rect_            = Rect{x, top, column_width, content_size_.height};
        content_->arrange(content_rect_);
    }
    else
    {
        content_rect_ = Rect{};
    }
}

bool InfoBarControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, false, false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    for_each_child([&](Element& child) { child.handle_key(e); });
    return handled;
}

void InfoBarControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    if (!is_open_)
    {
        return;
    }

    renderer.fill_rounded_rect(bounds(), theme.corner_radius, body_background(theme, severity_));
    // The border is the card stroke.
    stroke_rounded_rect_inside(renderer, bounds(), theme.corner_radius, theme.severity.stroke, theme.border_width);

    if (is_icon_visible_)
    {
        draw_icon(renderer, glyphs::CircleFill, icon_rect_, icon_background(theme, severity_), InfoBarIconSize, theme.icon_font_family);
        draw_icon(renderer, severity_glyph(severity_), icon_rect_, theme.severity.text_on_fill, InfoBarIconSize, theme.icon_font_family);
    }

    if (!title_.empty())
    {
        renderer.draw_text(title_, title_rect_, theme.colors().text, title_style(theme));
    }
    if (!message_.empty())
    {
        renderer.draw_text(message_, message_rect_, theme.colors().text, message_style(theme));
    }
    if (action_active())
    {
        action_->render(renderer, context);
    }
    if (content_active())
    {
        content_->render(renderer, context);
    }

    if (close_button_active())
    {
        close_button_->render(renderer, context);
    }
}

std::unique_ptr<ui::Element> InfoBar::build() &&
{
    auto node = std::make_unique<ui::InfoBarControl>();

    apply(severity, *node, [raw = node.get()](const ui::InfoBarSeverity& value) { raw->set_severity(value); });
    apply(closable, *node, [raw = node.get()](const bool& value) { raw->set_closable(value); });
    apply(icon_visible, *node, [raw = node.get()](const bool& value) { raw->set_icon_visible(value); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply(title, *node, [raw = node.get()](const std::string& value) { raw->set_title(value); });
    apply(message, *node, [raw = node.get()](const std::string& value) { raw->set_message(value); });
    if (auto action = action_button_content.take())
    {
        node->set_action_button_content(std::move(action));
    }
    if (auto custom = content.take())
    {
        node->set_content(std::move(custom));
    }

    // Handlers before is_open: opening fires nothing but closing does, so `.is_open = bind(visible)`
    // raises on_closed the first time the binding flips back.
    if (on_close_button_click)
    {
        node->on_close_button_click(std::move(on_close_button_click));
    }
    if (on_closing)
    {
        node->on_closing(std::move(on_closing));
    }
    // A two-way `.is_open` follows the control: the user opening or closing it writes the property.
    const auto is_open_writer = ui::shared_writer(is_open);
    if (auto handler = ui::write_back_value<bool>(is_open_writer, false, std::move(on_closed)))
    {
        node->on_closed(std::move(handler));
    }

    apply(is_open,
          *node,
          [raw = node.get(), is_open_writer](const bool& value)
          {
              raw->set_open(value);
              // on_closing canceled the close the property asked for: put the property back.
              if (raw->is_open() != value && is_open_writer)
              {
                  (*is_open_writer)(raw->is_open());
              }
          });

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
