// SPDX-License-Identifier: MIT
#include "winui/ui/navigation/SelectorBar.hpp"

#include "../FocusRingPlacement.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
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
constexpr float SelectorBarHoverOverlayAlpha = 0.10f; // the same header-hover wash alpha as Tabs

// An item's padding is 12,10,12,7. The bottom side is short because the pill
// lives inside it.
constexpr Thickness SelectorBarItemPadding = Edges{.left = 12.0f, .top = 10.0f, .right = 12.0f, .bottom = 7.0f};

// The strip's padding of 0,4, set on the item row: above and below the row only.
constexpr float StripPaddingV = 4.0f;

// The pill: 4x3, centered under the item. An elliptical 0.5x1 radius becomes one uniform
// radius, the taller one.
constexpr float PillWidth  = 4.0f;
constexpr float PillHeight = 3.0f;
constexpr float PillRadius = 1.0f;

// Selecting an item scales the pill's width 1 -> 4 and its opacity 0 -> 1 over 167 ms on the
// curve 0,0,0,1. Its origin is 0.5,0.5, so the pill grows about its center.
constexpr float  PillScale  = 4.0f;
constexpr double PillAnimMs = 167.0;

// The item's icon: a 20px symbol icon at margin (-2,0), scaled 0.8 about its center, then
// a spacing of 8 before the text.
constexpr float IconBox     = 20.0f;
constexpr float IconMargin  = -2.0f;
constexpr float IconScale   = 0.8f;
constexpr float IconSpacing = 8.0f;
constexpr float IconColumn  = IconBox + 2.0f * IconMargin + IconSpacing;

// The curve 0,0,0,1 has x1 == x2 == 0, so x(t) = t^3 and y(t) = 3t^2 - 2t^3, solved in closed form.
[[nodiscard]] float pill_ease(double x)
{
    x              = std::clamp(x, 0.0, 1.0);
    const double t = std::cbrt(x);
    return static_cast<float>(3.0 * t * t - 2.0 * t * t * t);
}
} // namespace

SelectorBarControl::SelectorBarControl(std::vector<SelectorBarItem> items, std::size_t selected_index) :
    items_(std::move(items))
{
    if (!items_.empty())
    {
        selected_index_ = std::min(selected_index, items_.size() - 1);
    }
}

void SelectorBarControl::set_items(std::vector<SelectorBarItem> items)
{
    items_          = std::move(items);
    selected_index_ = 0;
    hovered_index_  = detail::NoIndex;
    pressed_index_  = detail::NoIndex;
    pill_animating_ = false; // a fresh list is not a selection change, nothing grows in
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void SelectorBarControl::set_selected_index(std::size_t index)
{
    if (items_.empty())
    {
        return;
    }
    select(std::min(index, items_.size() - 1));
}

const std::string& SelectorBarControl::selected_text() const
{
    static const std::string Empty;
    return selected_index_ < items_.size() ? items_[selected_index_].text : Empty;
}

void SelectorBarControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_index_ = detail::NoIndex;
        pressed_index_ = detail::NoIndex;
    }
    mark_dirty(DirtyFlags::Visual);
}

void SelectorBarControl::focus_changed(bool /*focused*/)
{
    mark_dirty(DirtyFlags::Visual);
}

void SelectorBarControl::select(std::size_t index)
{
    if (index >= items_.size() || index == selected_index_)
    {
        return;
    }
    selected_index_ = index;
    // With no frame clock the pill lands fully grown.
    pill_animating_ = ticking_;
    pill_anim_ms_   = 0.0;
    // No item's width depends on selection, so Visual is enough.
    mark_dirty(DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(selected_index_);
    }
}

std::size_t SelectorBarControl::hit_test(Point p) const
{
    if (!bounds().contains(p))
    {
        return detail::NoIndex;
    }
    float x = bounds().left();
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const float w = i < item_widths_.size() ? item_widths_[i] : 0.0f;
        if (p.x >= x && p.x < x + w)
        {
            return i;
        }
        x += w;
    }
    return detail::NoIndex;
}

Size SelectorBarControl::measure_override(Size available, const MeasureContext& context) const
{
    item_widths_.clear();
    item_widths_.reserve(items_.size());

    float line_height = context.measurer.measure("Ag", context.theme.text_style).height;
    float total_width = 0.0f;
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const Size  s    = context.measurer.measure(items_[i].text, context.theme.text_style);
        const bool  icon = !items_[i].icon.empty();
        const float w    = s.width + SelectorBarItemPadding.horizontal() + (icon ? IconColumn : 0.0f);
        item_widths_.push_back(w);
        total_width += w;
        line_height = std::max(line_height, icon ? std::max(s.height, IconBox) : s.height);
    }
    // The pill sits inside the item's short bottom padding, so only the strip's 0,4 is added.
    row_height_ = line_height + SelectorBarItemPadding.vertical() + 2.0f * StripPaddingV;

    Size desired{total_width, row_height_};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void SelectorBarControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme      = context.theme;
    const Color  pill_color = is_enabled() ? theme.colors().accent : theme.accent_button_disabled.background;

    const float progress   = pill_animating_ ? pill_ease(pill_anim_ms_ / PillAnimMs) : 1.0f;
    const float pill_width = PillWidth * (1.0f + (PillScale - 1.0f) * progress);

    float x = bounds().left();
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const float w        = i < item_widths_.size() ? item_widths_[i] : 0.0f;
        const bool  selected = (i == selected_index_);
        const bool  hovered  = (i == hovered_index_);
        const bool  pressed  = (i == pressed_index_ && hovered);
        const Rect  item_rect{x, bounds().top(), w, row_height_};

        Color text_color = is_enabled() ? theme.colors().text : theme.colors().text_disabled;
        if (is_enabled() && !detail::controls(theme).accent_selection_indicator && !selected)
        {
            // Without a pill the cue moves into the label, as for the Sections headers under MDL2
            // and Metro. 60% is absolute: both eras' secondary foreground is 0x99 alpha.
            text_color = theme.colors().text.with_alpha(0.6f);
        }
        if (detail::controls(theme).chromeless_text_states)
        {
            // An item's background stays transparent in every state. The hover/press cue lives in
            // the label color.
            const ButtonState state = !is_enabled() ? ButtonState::Disabled
                                      : pressed     ? ButtonState::Pressed
                                      : hovered     ? ButtonState::Hover
                                                    : ButtonState::Normal;
            text_color              = detail::text_fill_for_state(theme, state);
        }
        else if (!selected && is_enabled() && hovered)
        {
            renderer.fill_rounded_rect(item_rect,
                                       0.0f,
                                       theme.colors().control_fill_hover.with_alpha(SelectorBarHoverOverlayAlpha));
        }

        const float inner_top    = bounds().top() + StripPaddingV;
        const float inner_height = std::max(0.0f, row_height_ - 2.0f * StripPaddingV);
        float       text_left    = x;
        float       text_width   = w;
        if (const IconSource& icon = items_[i].icon)
        {
            // The icon's 20px box at the padding's left edge, pulled in by its -2 margin; the
            // label follows after the spacing, the pair centered in the padded item.
            const float size = IconBox * IconScale;
            const float cx   = x + SelectorBarItemPadding.left + IconMargin + IconBox * 0.5f;
            const float cy   = inner_top + SelectorBarItemPadding.top +
                               (inner_height - SelectorBarItemPadding.vertical()) * 0.5f;
            icon.draw_in_slot(renderer, Rect{cx - size * 0.5f, cy - size * 0.5f, size, size}, text_color, size, theme.icon_font_family);
            text_left += IconColumn;
            text_width -= IconColumn;
        }
        const Rect text_rect{text_left, inner_top, std::max(0.0f, text_width), inner_height};
        renderer.draw_text(items_[i].text, text_rect, text_color, theme.text_style);

        if (selected && detail::controls(theme).accent_selection_indicator)
        {
            // Centered on the scaled width per the 0.5,0.5 origin; growing from the left edge would
            // settle the pill 6px right of center.
            const Rect pill_rect{x + (w - pill_width) * 0.5f,
                                 bounds().top() + row_height_ - StripPaddingV - PillHeight,
                                 pill_width,
                                 PillHeight};
            renderer.fill_rounded_rect(pill_rect, PillRadius, pill_color.with_alpha(pill_color.a * progress));
        }

        x += w;
    }
}

Rect SelectorBarControl::focus_ring_rect(const Theme& theme) const
{
    return detail::focus_ring_seed(bounds(), Thickness{2.0f}, theme.focus_ring_style());
}

void SelectorBarControl::tick_override(Duration delta)
{
    ticking_ = true;
    if (pill_animating_)
    {
        pill_anim_ms_ += animation_delta(delta).count();
        if (pill_anim_ms_ >= PillAnimMs)
        {
            pill_animating_ = false;
        }
        mark_dirty(DirtyFlags::Visual);
    }
}

bool SelectorBarControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }

    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
            hovered_index_ = hit_test(e.position);
            mark_dirty(DirtyFlags::Visual);
            break;

        case PointerAction::Down:
            if (e.button == PointerButton::Left)
            {
                const std::size_t idx = hit_test(e.position);
                if (idx != detail::NoIndex)
                {
                    // A pressed item, selected or not, drops the label to the tertiary text color.
                    // Selection lands on Down, so this lasts until Up.
                    pressed_index_ = idx;
                    handled        = true;
                    select(idx);
                }
            }
            break;

        case PointerAction::Up:
            if (e.button == PointerButton::Left && pressed_index_ != detail::NoIndex)
            {
                pressed_index_ = detail::NoIndex;
                handled        = true;
                mark_dirty(DirtyFlags::Visual);
            }
            break;

        case PointerAction::Leave:
            hovered_index_ = detail::NoIndex;
            pressed_index_ = detail::NoIndex;
            mark_dirty(DirtyFlags::Visual);
            break;

        default:
            break;
    }
    return handled;
}

bool SelectorBarControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, takes_arrow_key(e.key), false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused() || e.action != KeyAction::Down)
    {
        return handled;
    }

    switch (e.key)
    {
        case Key::ArrowLeft:
            if (selected_index_ > 0)
            {
                select(selected_index_ - 1);
            }
            break;
        case Key::ArrowRight:
            if (selected_index_ + 1 < items_.size())
            {
                select(selected_index_ + 1);
            }
            break;
        default:
            break;
    }
    return handled;
}

std::unique_ptr<ui::Element> SelectorBar::build() &&
{
    auto node = std::make_unique<ui::SelectorBarControl>(items.initial());
    if (items.reactive())
    {
        // New items clear the selection; a bound index is then shown again as it stands, so one
        // that follows the same list (ui::bind_choice()) keeps its item whichever hears first.
        Binding<std::size_t> index = selected_index;
        node->add_subscription(items.subscribe(
            [raw = node.get(), index = std::move(index)](std::vector<SelectorBarItem> v)
            {
                const detail::BoundUpdate bound; // a selection the new items clear isn't the user's change
                raw->set_items(std::move(v));
                if (index.reactive())
                {
                    raw->set_selected_index(index.current());
                }
            }));
    }

    apply(selected_index, *node, [raw = node.get()](const std::size_t& index) { raw->set_selected_index(index); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });

    if (auto changed = ui::write_back(selected_index, std::move(on_selection_changed)))
    {
        node->on_selection_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
