// SPDX-License-Identifier: MIT
#include "winui/ui/containers/Expander.hpp"

#include "../../render/Compositor.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ui
{

namespace
{
// Expander metrics, applied to every theme: an Expander has no pre-Fluent counterpart.
constexpr float     HeaderMinHeight          = 48.0f;                               // the header's height
constexpr float     ExpanderMinWidth         = 96.0f;                               // a flyout's minimum width
constexpr Thickness ExpanderHeaderInset      = Edges{.left = 16.0f};                // before the header content
constexpr float     ChevronButtonSize        = 32.0f;                               // the chevron button's square
constexpr Thickness ChevronMargin            = Edges{.left = 20.0f, .right = 8.0f}; // around the chevron button
constexpr float     ExpanderChevronGlyphSize = 12.0f;                               // the chevron glyph
// The content padding is a settable member.

// The chevron sits in the header Grid's Auto column, so its footprint is the button plus both
// margins.
constexpr float ExpanderChevronColumnWidth = ChevronMargin.left + ChevronButtonSize + ChevronMargin.right;

// The expand and collapse durations. Neither matches a Theme::anim_* rung, and
// tick_override() has no Theme anyway.
constexpr double ExpanderExpandMs   = 333.0;
constexpr double ExpanderCollapseMs = 167.0;

// Each direction has its own curve. Expanding is a hard decelerate, collapsing starts at the
// pace the content already had. One shared ease-out makes the close lurch.
constexpr CubicBezier ExpandCurve = easing::ControlFastOutSlowIn;
constexpr CubicBezier CollapseCurve{1.0f, 1.0f, 0.0f, 1.0f};

// Renderer has one uniform corner radius, where the header and content corners are rounded
// separately: draw one radius over-tall and clip the overhang away.
[[nodiscard]] Rect overhang(const Rect& rect, float radius, bool round_top, bool round_bottom)
{
    const float top    = round_top ? rect.top() : rect.top() - radius;
    const float bottom = round_bottom ? rect.bottom() : rect.bottom() + radius;
    return Rect{rect.left(), top, rect.width, bottom - top};
}

void fill_partial_rounded_rect(Renderer& r, const Rect& rect, float radius, const Color& color, bool round_top, bool round_bottom)
{
    if (radius <= 0.0f || (round_top && round_bottom))
    {
        r.fill_rounded_rect(rect, radius, color);
        return;
    }
    r.push_clip(rect);
    r.fill_rounded_rect(overhang(rect, radius, round_top, round_bottom), radius, color);
    r.pop_clip();
}

void stroke_partial_rounded_rect(Renderer& r, const Rect& rect, float radius, const Color& color, float width, bool round_top, bool round_bottom)
{
    if (radius <= 0.0f || (round_top && round_bottom))
    {
        stroke_rounded_rect_inside(r, rect, radius, color, width);
        return;
    }
    r.push_clip(rect);
    stroke_rounded_rect_inside(r, overhang(rect, radius, round_top, round_bottom), radius, color, width);
    r.pop_clip();
}
} // namespace

ExpanderControl::ExpanderControl(std::string header, bool expanded) : header_(std::move(header)), expanded_(expanded)
{
    // A flyout's minimum width and the header's height.
    set_min_width(ExpanderMinWidth);
    set_min_height(HeaderMinHeight);
    slide_ = expanded_ ? 1.0 : 0.0;
}

float ExpanderControl::slide_progress() const
{
    // `slide_` is openness, so a collapse runs its curve mirrored. The curve follows the current
    // direction: an interrupted collapse re-expands on the expand curve.
    if (expanded_)
    {
        return easing::evaluate(ExpandCurve, slide_);
    }
    return 1.0f - easing::evaluate(CollapseCurve, 1.0 - slide_);
}

bool ExpanderControl::content_live() const
{
    return content_ != nullptr && (expanded_ || slide_ > 0.0);
}

void ExpanderControl::set_expanded(bool expanded)
{
    if (expanded_ == expanded)
    {
        return;
    }
    expanded_ = expanded;
    if (!ticking_)
    {
        slide_ = expanded_ ? 1.0 : 0.0; // no frame clock: settle immediately
    }
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    if (on_expanded_changed_)
    {
        on_expanded_changed_(expanded_);
    }
}

void ExpanderControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_      = false;
        pointer_down_ = false;
        key_down_     = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

void ExpanderControl::focus_changed(bool /*focused*/)
{
    if (!is_focused())
    {
        key_down_ = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

void ExpanderControl::collect_focusables(std::vector<Element*>& out)
{
    if (focusable())
    {
        out.push_back(this);
    }
    // A scope's content belongs to its inner ring only. In the outer ring too, it would be
    // reachable without entering the scope.
    if (content_live() && !focus_scope())
    {
        content_->collect_focusables(out);
    }
}

void ExpanderControl::collect_scope_focusables(std::vector<Element*>& out)
{
    if (content_live())
    {
        content_->collect_focusables(out);
    }
}

Size ExpanderControl::measure_override(Size available, const MeasureContext& context) const
{
    // ExpanderHeaderInset is horizontal-only. The row is sized by the header's HeaderMinHeight (48),
    // with the 32px chevron button the tallest part inside it.
    const float line_height  = context.measurer.measure("", context.theme.text_style).height;
    float       label_width  = 0.0f;
    float       label_height = line_height;
    if (header_content_)
    {
        const float room = std::max(0.0f, available.width - ExpanderHeaderInset.left - ExpanderChevronColumnWidth);
        const Size  d    = header_content_->measure(Size{room, Unbounded}, context);
        label_width      = d.width;
        label_height     = d.height;
    }
    else
    {
        label_width = context.measurer.measure(header_, context.theme.text_style).width;
    }
    header_height_ = std::max({HeaderMinHeight, label_height, ChevronButtonSize});

    const float header_width = ExpanderHeaderInset.left + label_width + ExpanderChevronColumnWidth;

    float content_width = 0.0f;
    content_height_     = 0.0f;
    if (content_live())
    {
        const Size inner_available{
            std::max(0.0f, available.width - content_padding_.horizontal()),
            Unbounded,
        };
        const Size content_measured = content_->measure(inner_available, context);
        content_width               = content_measured.width + content_padding_.horizontal();
        // The opened band has the same 48px floor the header does.
        content_height_ = std::max(HeaderMinHeight, content_measured.height + content_padding_.vertical());
    }

    Size desired{std::max(header_width, content_width), header_height_ + content_height_};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void ExpanderControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    const float band = std::min(content_height_, std::max(0.0f, final_rect.height - header_height_));
    if (expand_direction_ == ExpandDirection::Up)
    {
        // Up swaps the rows: the content on top, the header below.
        content_rect_ = Rect{final_rect.left(), final_rect.top(), final_rect.width, band};
        header_rect_  = Rect{final_rect.left(), final_rect.top() + band, final_rect.width, header_height_};
    }
    else
    {
        header_rect_  = Rect{final_rect.left(), final_rect.top(), final_rect.width, header_height_};
        content_rect_ = Rect{final_rect.left(), final_rect.top() + header_height_, final_rect.width, band};
    }
    if (header_content_)
    {
        header_content_->arrange(
            Rect{header_rect_.left() + ExpanderHeaderInset.left,
                 header_rect_.top(),
                 std::max(0.0f, header_rect_.width - ExpanderHeaderInset.left - ExpanderChevronColumnWidth),
                 header_rect_.height});
    }
    place_content();
}

void ExpanderControl::place_content()
{
    if (!content_live() || content_height_ <= 0.0f)
    {
        return;
    }
    // The band stays put and the content slides into it from behind the header: expanding down
    // it moves from minus the content height to 0, expanding up from plus the content height.
    const float slide  = (1.0f - slide_progress()) * content_height_;
    const float offset = expand_direction_ == ExpandDirection::Up ? slide : -slide;
    const Rect  band{content_rect_.left(), content_rect_.top() + offset, content_rect_.width, content_height_};
    content_->arrange(band.inset(content_padding_));
}

bool ExpanderControl::is_pressed() const
{
    if (!is_enabled())
    {
        return false;
    }
    return (pointer_down_ && hovered_) || key_down_;
}

bool ExpanderControl::hot() const
{
    return is_enabled() && (hovered_ || is_pressed());
}

bool ExpanderControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }

    const bool in_header = header_rect_.contains(e.position);

    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
            hovered_ = in_header;
            break;

        case PointerAction::Down:
            if (e.button == PointerButton::Left && in_header)
            {
                pointer_down_ = true;
                hovered_      = true;
                handled       = true;
            }
            break;

        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                const bool was_down = pointer_down_;
                pointer_down_       = false;
                hovered_            = in_header;
                handled             = was_down;
                if (was_down && in_header)
                {
                    set_expanded(!expanded_);
                }
            }
            break;

        case PointerAction::Leave:
            hovered_ = false;
            break;

        default:
            break;
    }

    return handled;
}

void ExpanderControl::hit_test_children(Point p, std::vector<Element*>& out) const
{
    // The content only through its drawn band: while it slides, content_ reaches up under the
    // header.
    if (content_live() && content_rect_.contains(p))
    {
        out.push_back(content_.get());
    }
}

bool ExpanderControl::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, false, false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    // Space only: Enter steps into the expanded body as a focus scope, which Window handles
    // before this. Space toggles either way.
    if (!is_enabled() || !is_focused() || e.key != Key::Space)
    {
        return handled;
    }

    if (e.action == KeyAction::Down)
    {
        key_down_ = true;
    }
    else
    {
        if (key_down_)
        {
            key_down_ = false;
            set_expanded(!expanded_);
        }
    }
    return handled;
}

void ExpanderControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme          = context.theme;
    const bool   open           = content_live() && content_rect_.height > 0.0f;
    const bool   up             = expand_direction_ == ExpandDirection::Up;
    const float  radius         = theme.corner_radius;
    const Color  header_text    = is_enabled() ? theme.colors().text : theme.colors().text_disabled;
    const Rect   chevron_button = Rect{header_rect_.right() - ChevronMargin.right - ChevronButtonSize,
                                       header_rect_.top() + (header_rect_.height - ChevronButtonSize) * 0.5f,
                                       ChevronButtonSize,
                                       ChevronButtonSize};

    if (detail::controls(theme).card_surfaces)
    {
        // The content's BorderThickness is 1,0,1,1 (Up: 1,1,1,0): the header's own border
        // already draws the shared line.
        fill_partial_rounded_rect(renderer, header_rect_, radius, theme.palette.card_background_default, !open || !up, !open || up);
        if (open)
        {
            fill_partial_rounded_rect(renderer, content_rect_, radius, theme.palette.card_background_secondary, up, !up);
        }
    }
    else
    {
        // No card tokens for this theme (ControlChrome::card_surfaces): a uniform button_normal card.
        draw_control_chrome(renderer, bounds(), radius, theme.button_normal, theme.border_width);
    }

    // The hover and press wash sits on the 32x32 chevron button, not the header. Drawn as chrome so
    // a classic theme's subtle visual shows its bevel instead of a flat fill that disappears.
    if (is_enabled() && is_pressed())
    {
        draw_control_chrome(renderer, chevron_button, radius, theme.subtle_button_pressed, theme.border_width);
    }
    else if (is_enabled() && hovered_)
    {
        draw_control_chrome(renderer, chevron_button, radius, theme.subtle_button_hover, theme.border_width);
    }

    if (detail::controls(theme).card_surfaces)
    {
        stroke_partial_rounded_rect(renderer,
                                    header_rect_,
                                    radius,
                                    theme.palette.card_stroke_default,
                                    theme.border_width,
                                    !open || !up,
                                    !open || up);
        if (open)
        {
            // A 1,0,1,1 border expanding down, 1,1,1,0 expanding up:
            // no edge against the header, whose own border draws that line.
            stroke_partial_rounded_rect(renderer,
                                        content_rect_,
                                        radius,
                                        theme.palette.card_stroke_default,
                                        theme.border_width,
                                        up,
                                        !up);
        }
    }

    // A down chevron when expanding down, an up chevron when expanding up,
    // each turned round while checked (expanded). The classic packs have no expander, so they
    // take the drop-down's solid arrow, pointing the same way.
    const bool points_up = expanded_ != up;
    draw_disclosure(renderer,
                    theme,
                    points_up ? glyphs::ChevronUp : glyphs::ChevronDown,
                    points_up ? ArrowDirection::Up : ArrowDirection::Down,
                    chevron_button,
                    header_text,
                    ExpanderChevronGlyphSize);

    TextStyle header_style  = theme.text_style;
    header_style.horizontal = TextAlign::Leading;
    const Rect header_text_rect{header_rect_.left() + ExpanderHeaderInset.left,
                                header_rect_.top(),
                                std::max(0.0f, header_rect_.width - ExpanderHeaderInset.left - ExpanderChevronColumnWidth),
                                header_rect_.height};
    if (header_content_)
    {
        header_content_->render(renderer, context);
    }
    else
    {
        renderer.draw_text(header_, header_text_rect, header_text, header_style);
    }

    if (open)
    {
        // Whatever overhangs the band is cut off, so it never paints over the header.
        renderer.push_clip(content_rect_);
        content_->render(renderer, context);
        renderer.pop_clip();

        if (!detail::controls(theme).card_surfaces)
        {
            // The era's divider between header and content, inside the card's border so it never
            // crosses the bevel: an etched groove (dark, then light), or one flat line.
            const ButtonVisual& card = theme.button_normal;
            const bool  bevel = card.border_style == BorderStyle::Raised || card.border_style == BorderStyle::Sunken;
            const float inset = card.resolved_border_width(theme.border_width) *
                                static_cast<float>(bevel ? std::max(1, card.bevel_rings) : 1);
            const float edge  = up ? header_rect_.top() : header_rect_.bottom();
            const float left  = bounds().left() + inset;
            const float width = std::max(0.0f, bounds().width - 2.0f * inset);
            if (theme.divider_visual == ThemeDividerVisual::FlatStroke)
            {
                renderer.fill_rounded_rect(Rect{left, up ? edge - 1.0f : edge, width, 1.0f}, 0.0f, theme.colors().divider);
            }
            else
            {
                renderer.fill_rounded_rect(Rect{left, edge - 1.0f, width, 1.0f}, 0.0f, theme.surfaces.separator_dark);
                renderer.fill_rounded_rect(Rect{left, edge, width, 1.0f}, 0.0f, theme.surfaces.separator_light);
            }
        }
    }
}

void ExpanderControl::clear_dirty_override(DirtyFlags flags)
{
    if (header_content_)
    {
        clear_dirty_of(*header_content_, flags);
    }
}

void ExpanderControl::tick_override(Duration delta)
{
    ticking_ = true;

    const double target = expanded_ ? 1.0 : 0.0;
    if (slide_ != target)
    {
        const double step = animation_delta(delta).count() / (expanded_ ? ExpanderExpandMs : ExpanderCollapseMs);
        slide_            = expanded_ ? std::min(1.0, slide_ + step) : std::max(0.0, slide_ - step);
        place_content();
        // A finished collapse retires the content region, which is a size change. Before that the
        // content only moves within a fixed band.
        mark_dirty(slide_ > 0.0 ? (DirtyFlags::Visual) : (DirtyFlags::Layout | DirtyFlags::Visual));
    }
}

std::unique_ptr<ui::Element> Expander::build() &&
{
    auto node = std::make_unique<ui::ExpanderControl>();
    apply(content_padding, *node, [raw = node.get()](const Thickness& v) { raw->set_content_padding(v); });

    apply(header, *node, [raw = node.get()](const std::string& value) { raw->set_header(value); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply(expand_direction,
          *node,
          [raw = node.get()](const ui::ExpandDirection& value) { raw->set_expand_direction(value); });
    if (header_content)
    {
        node->set_header_content(header_content.take());
    }

    if (content)
    {
        node->set_content(content.take());
    }

    // After set_content: expanding before there is content to reveal measures an empty
    // body, and after it the first layout pass sees the real thing.
    apply(expanded, *node, [raw = node.get()](const bool& value) { raw->set_expanded(value); });

    if (auto changed = ui::write_back(expanded, std::move(on_expanded_changed)))
    {
        node->on_expanded_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
