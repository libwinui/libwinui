// SPDX-License-Identifier: MIT
#include "winui/ui/navigation/Breadcrumb.hpp"

#include "../FocusRingPlacement.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
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
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
constexpr float BreadcrumbHoverOverlayAlpha = 0.10f; // non-Fluent fallback wash, see render()
constexpr float PressedOverlayAlpha         = 0.15f; // non-Fluent fallback wash, see render()

// The chevron's 12 DIP font size plus its 2,0 padding. An approximation of the
// glyph's advance, like Expander's chevron_box.
constexpr float     ChevronWidth = 16.0f;
constexpr IconGlyph ItemChevron{0xE76C, "Segoe Fluent Icons", 12.0f};
constexpr IconGlyph EllipsisChevron{0xE974, "Segoe Fluent Icons", 12.0f};

// An item button's padding is 1,3. The last item has the same, so the current
// location is padded like the others.
constexpr Thickness BreadcrumbItemPadding = Symmetric{.horizontal = 1.0f, .vertical = 3.0f};

// Both item kinds set a line height of 20: an item's row is 20 plus BreadcrumbItemPadding's 6,
// independent of what the measurer reports (read as a floor, as a line height is).
constexpr float LineHeight = 20.0f;

// The ellipsis: the 0xE712 glyph at the item's font size with a padding of 3, inside the
// button's own 1,3.
constexpr IconGlyph EllipsisGlyph{0xE712, "Segoe Fluent Icons", 14.0f};
constexpr float     EllipsisPadding = 3.0f;

// The item button's focus visual margin of -3, the ring's outward reach around an item.
constexpr float ItemFocusMargin = 3.0f;

// The ellipsis drop-down's items: padding 11,7,11,9, margin 5,3, in a flyout surface padded 0,2
// with a 1 px border, at least 40 tall and at most a flyout's maximum width (456) wide.
constexpr Thickness DropPadding               = Edges{.left = 11.0f, .top = 7.0f, .right = 11.0f, .bottom = 9.0f};
constexpr Thickness DropMargin                = Symmetric{.horizontal = 5.0f, .vertical = 3.0f};
constexpr float     BreadcrumbFlyoutPadding   = 2.0f;
constexpr float     BreadcrumbFlyoutBorder    = 1.0f;
constexpr float     BreadcrumbFlyoutMinHeight = 40.0f;
constexpr float     BreadcrumbFlyoutMaxWidth  = 456.0f;
} // namespace

// ---- the ellipsis flyout's rows ----

class BreadcrumbFlyoutList final : public Element
{
public:
    explicit BreadcrumbFlyoutList(BreadcrumbControl& owner) : owner_(owner)
    {
    }

    // The folded items, nearest-first: the reverse of their order in the trail.
    [[nodiscard]] std::vector<std::size_t> rows() const
    {
        std::vector<std::size_t> out;
        for (std::size_t i = owner_.first_shown_; i > 0; --i)
        {
            out.push_back(i - 1);
        }
        return out;
    }

    [[nodiscard]] Rect row_rect(std::size_t row) const
    {
        return Rect{bounds().x + BreadcrumbFlyoutBorder,
                    bounds().y + BreadcrumbFlyoutBorder + BreadcrumbFlyoutPadding + static_cast<float>(row) * row_height_,
                    std::max(0.0f, bounds().width - 2.0f * BreadcrumbFlyoutBorder),
                    row_height_};
    }

    std::size_t hovered = detail::NoIndex;
    std::size_t pressed = detail::NoIndex;

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext& context) const override
    {
        row_height_      = 0.0f;
        float      width = 0.0f;
        const auto rs    = rows();
        for (std::size_t i : rs)
        {
            const Size s = context.measurer.measure(owner_.items_[i], context.theme.text_style);
            width        = std::max(width, std::ceil(s.width) + DropPadding.horizontal() + DropMargin.horizontal());
            row_height_  = std::max(row_height_, s.height + DropPadding.vertical() + DropMargin.vertical());
        }
        const float chrome = 2.0f * (BreadcrumbFlyoutBorder + BreadcrumbFlyoutPadding);
        return Size{std::min(width + 2.0f * BreadcrumbFlyoutBorder, BreadcrumbFlyoutMaxWidth),
                    std::max(BreadcrumbFlyoutMinHeight, row_height_ * static_cast<float>(rs.size()) + chrome)};
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        // The menu surface (the acrylic's fallback color) inside the flyout stroke, at the
        // overlay corner radius.
        const Color fill   = theme.surfaces.menu;
        const Color stroke = theme.surfaces.menu_border.value_or(theme.surfaces.field_border);
        fill_overlay_surface(renderer, theme, bounds(), theme.overlay_corner_radius, fill);
        stroke_rounded_rect_inside(renderer, bounds(), theme.overlay_corner_radius, stroke, theme.border_width);
        const auto rs = rows();
        renderer.push_clip(bounds());
        for (std::size_t r = 0; r < rs.size(); ++r)
        {
            const Rect root  = row_rect(r).inset(DropMargin);
            const bool hot   = r == hovered || (owner_.flyout_cursor_ == rs[r]);
            const bool down  = r == pressed && r == hovered;
            Color      color = owner_.is_enabled() ? theme.surfaces.menu_text : theme.colors().text_disabled;
            if (hot || down)
            {
                // A drop-down row's hover and pressed backgrounds.
                const Color bg = detail::controls(theme).chromeless_text_states
                                     ? (down ? theme.palette.subtle_fill_tertiary : theme.palette.subtle_fill_secondary)
                                     : theme.surfaces.menu_highlight;
                renderer.fill_rounded_rect(root, theme.corner_radius, bg);
                if (!detail::controls(theme).chromeless_text_states)
                {
                    color = theme.surfaces.menu_highlight_text;
                }
            }
            TextStyle style  = theme.text_style;
            style.horizontal = TextAlign::Leading;
            renderer.draw_text(owner_.items_[rs[r]], root.inset(DropPadding), color, style);
        }
        renderer.pop_clip();
    }

    bool pointer_override(const PointerEvent& e) override
    {
        const auto  rs  = rows();
        std::size_t hit = detail::NoIndex;
        for (std::size_t r = 0; r < rs.size(); ++r)
        {
            if (row_rect(r).contains(e.position))
            {
                hit = r;
            }
        }
        switch (e.action)
        {
            case PointerAction::Move:
                hovered = hit;
                mark_dirty(DirtyFlags::Visual);
                break;
            case PointerAction::Down:
                if (e.button == PointerButton::Left)
                {
                    pressed = hit;
                }
                break;
            case PointerAction::Up:
                if (e.button == PointerButton::Left)
                {
                    const std::size_t was = pressed;
                    pressed               = detail::NoIndex;
                    if (was != detail::NoIndex && was == hit)
                    {
                        const std::size_t item = rs[hit];
                        owner_.set_flyout_open(false);
                        owner_.activate(item);
                    }
                }
                break;
            case PointerAction::Leave:
                hovered = pressed = detail::NoIndex;
                mark_dirty(DirtyFlags::Visual);
                break;
            default:
                break;
        }
        return false;
    }


private:
    BreadcrumbControl& owner_;
    mutable float      row_height_ = 0.0f;
};

// ---- construction ----

BreadcrumbControl::BreadcrumbControl() : flyout_(std::make_unique<BreadcrumbFlyoutList>(*this))
{
    set_parent_of(*flyout_, this);
}

BreadcrumbControl::BreadcrumbControl(std::vector<std::string> items) : BreadcrumbControl()
{
    items_ = std::move(items);
}

BreadcrumbControl::~BreadcrumbControl() = default;

void BreadcrumbControl::set_items(std::vector<std::string> items)
{
    items_              = std::move(items);
    hovered_index_      = detail::NoIndex;
    pointer_down_index_ = detail::NoIndex;
    focused_item_index_ = detail::NoIndex;
    flyout_cursor_      = detail::NoIndex;
    key_down_           = false;
    flyout_open_        = false;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void BreadcrumbControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_index_      = detail::NoIndex;
        pointer_down_index_ = detail::NoIndex;
        key_down_           = false;
        flyout_open_        = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

void BreadcrumbControl::focus_changed(bool /*focused*/)
{
    if (is_focused())
    {
        // The highlight starts at the first stop each time focus arrives, like SelectorBar.
        const auto s        = stops();
        focused_item_index_ = s.empty() ? detail::NoIndex : s.front();
    }
    else
    {
        focused_item_index_ = detail::NoIndex;
        key_down_           = false; // losing focus cancels a keyboard press (Button's own rule)
    }
    mark_dirty(DirtyFlags::Visual);
}

void BreadcrumbControl::set_flyout_open(bool open)
{
    open = open && first_shown_ > 0 && is_enabled();
    if (open == flyout_open_)
    {
        return;
    }
    flyout_open_     = open;
    flyout_cursor_   = detail::NoIndex;
    flyout_->hovered = flyout_->pressed = detail::NoIndex;
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

void BreadcrumbControl::activate(std::size_t index)
{
    if (index == Ellipsis)
    {
        set_flyout_open(!flyout_open_);
        return;
    }
    if (is_enabled() && is_clickable(index) && on_item_click_)
    {
        on_item_click_(index);
    }
}

std::vector<std::size_t> BreadcrumbControl::stops() const
{
    std::vector<std::size_t> out;
    if (first_shown_ > 0)
    {
        out.push_back(Ellipsis);
    }
    for (std::size_t i = first_shown_; i < items_.size(); ++i)
    {
        if (is_clickable(i))
        {
            out.push_back(i);
        }
    }
    return out;
}

// ---- layout ----

Size BreadcrumbControl::measure_override(Size available, const MeasureContext& context) const
{
    item_widths_.clear();
    item_widths_.reserve(items_.size());

    float line_height = context.measurer.measure("Ag", context.theme.text_style).height;
    float total_width = 0.0f;
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const Size  s = context.measurer.measure(items_[i], context.theme.text_style);
        const float w = s.width + BreadcrumbItemPadding.horizontal();
        item_widths_.push_back(w);
        total_width += w;
        line_height = std::max(line_height, s.height);
        if (i + 1 < items_.size())
        {
            total_width += ChevronWidth;
        }
    }
    row_height_     = std::max(line_height, LineHeight) + BreadcrumbItemPadding.vertical();
    ellipsis_width_ = BreadcrumbItemPadding.horizontal() + 2.0f * EllipsisPadding + EllipsisGlyph.size;

    Size desired{total_width, row_height_};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void BreadcrumbControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    // The breadcrumb layout: when the whole
    // trail does not fit, as many items as fit from the last one backwards follow the ellipsis.
    const std::size_t n     = items_.size();
    const auto        width = [this, n](std::size_t i) { return item_widths_[i] + (i + 1 < n ? ChevronWidth : 0.0f); };
    float             total = 0.0f;
    for (std::size_t i = 0; i < n && i < item_widths_.size(); ++i)
    {
        total += width(i);
    }
    const std::size_t before = first_shown_;
    first_shown_             = 0;
    if (n > 1 && item_widths_.size() == n && total > final_rect.width)
    {
        float accum  = width(n - 1) + ellipsis_width_ + ChevronWidth;
        first_shown_ = n - 1;
        for (std::size_t i = n - 1; i-- > 0;)
        {
            if (accum + width(i) > final_rect.width)
            {
                break;
            }
            accum += width(i);
            first_shown_ = i;
        }
        first_shown_ = std::max<std::size_t>(first_shown_, 1);
    }
    if (first_shown_ != before)
    {
        if (first_shown_ == 0)
        {
            flyout_open_ = false;
        }
        if (focused_item_index_ != detail::NoIndex && focused_item_index_ != Ellipsis && focused_item_index_ < first_shown_)
        {
            focused_item_index_ = first_shown_ > 0 ? Ellipsis : focused_item_index_;
        }
    }
    sync_popup();
}

std::optional<Rect> BreadcrumbControl::ellipsis_bounds() const
{
    if (first_shown_ == 0)
    {
        return std::nullopt;
    }
    return Rect{bounds().left(), bounds().top(), ellipsis_width_, row_height_};
}

std::optional<Rect> BreadcrumbControl::item_bounds(std::size_t index) const
{
    if (index >= items_.size() || index < first_shown_ || index >= item_widths_.size())
    {
        return std::nullopt;
    }
    float x = bounds().left() + (first_shown_ > 0 ? ellipsis_width_ + ChevronWidth : 0.0f);
    for (std::size_t i = first_shown_; i < index; ++i)
    {
        x += item_widths_[i] + ChevronWidth;
    }
    return Rect{x, bounds().top(), item_widths_[index], row_height_};
}

std::size_t BreadcrumbControl::hit_test(Point p) const
{
    if (!bounds().contains(p))
    {
        return detail::NoIndex;
    }
    if (const auto ellipsis = ellipsis_bounds(); ellipsis && ellipsis->contains(p))
    {
        return Ellipsis;
    }
    for (std::size_t i = first_shown_; i < items_.size(); ++i)
    {
        if (const auto r = item_bounds(i); r && is_clickable(i) && r->contains(p))
        {
            return i;
        }
    }
    return detail::NoIndex;
}

// ---- the flyout ----

void BreadcrumbControl::sync_popup()
{
    if (!flyout_open_ || first_shown_ == 0)
    {
        close_popup();
        return;
    }
    open_popup({
        .content     = flyout_.get(),
        .anchor_rect = Rect{0.0f, 0.0f, ellipsis_width_, row_height_}, // the ellipsis item
        .on_dismiss  = [this](PopupCloseReason) { set_flyout_open(false); },
    });
}

// ---- render ----

void BreadcrumbControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme      = context.theme;
    const Color  rest_color = is_enabled() ? theme.colors().text : theme.colors().text_disabled;

    const auto item_state = [&](std::size_t i)
    {
        const bool clickable = i == Ellipsis || is_clickable(i);
        const bool pressed   = clickable && is_enabled() &&
                               ((i == pointer_down_index_ && i == hovered_index_) ||
                                (i == focused_item_index_ && key_down_) || (i == Ellipsis && flyout_open_));
        const bool hovered   = clickable && is_enabled() && i == hovered_index_;
        return std::pair{hovered, pressed};
    };
    const auto text_color_for = [&](bool hovered, bool pressed, const Rect& rect)
    {
        Color text_color = rest_color;
        if (detail::controls(theme).chromeless_text_states)
        {
            // No wash: an item only changes its text color (primary, secondary, tertiary,
            // disabled) and keeps a transparent background.
            const ButtonState state = !is_enabled() ? ButtonState::Disabled
                                      : pressed     ? ButtonState::Pressed
                                      : hovered     ? ButtonState::Hover
                                                    : ButtonState::Normal;
            text_color              = detail::text_fill_for_state(theme, state);
        }
        else if (pressed)
        {
            renderer.fill_rounded_rect(rect, 0.0f, theme.colors().control_fill_pressed.with_alpha(PressedOverlayAlpha));
        }
        else if (hovered)
        {
            renderer.fill_rounded_rect(rect, 0.0f, theme.colors().control_fill_hover.with_alpha(BreadcrumbHoverOverlayAlpha));
        }
        return text_color;
    };
    if (const auto ellipsis = ellipsis_bounds())
    {
        const auto [hovered, pressed] = item_state(Ellipsis);
        const Color color             = text_color_for(hovered, pressed, *ellipsis);
        draw_icon(renderer, EllipsisGlyph, *ellipsis, color, EllipsisGlyph.size, theme.icon_font_family);
        // The chevron takes the rest text color once, so a chevron does
        // not follow its neighbor's hover or press color.
        draw_disclosure(renderer,
                        theme,
                        EllipsisChevron,
                        ArrowDirection::Right,
                        Rect{ellipsis->right(), bounds().top(), ChevronWidth, row_height_},
                        rest_color,
                        EllipsisChevron.size);
    }

    for (std::size_t i = first_shown_; i < items_.size(); ++i)
    {
        const auto item_rect = item_bounds(i);
        if (!item_rect)
        {
            continue;
        }
        const auto [hovered, pressed] = item_state(i);
        const Color text_color        = text_color_for(hovered, pressed, *item_rect);
        renderer.draw_text(items_[i], *item_rect, text_color, theme.text_style);
        if (i + 1 < items_.size())
        {
            // The classic packs draw a menu's cascade arrow, the era's "goes on to the right".
            draw_disclosure(renderer,
                            theme,
                            ItemChevron,
                            ArrowDirection::Right,
                            Rect{item_rect->right(), bounds().top(), ChevronWidth, row_height_},
                            rest_color,
                            ItemChevron.size);
        }
    }
}

Rect BreadcrumbControl::focus_ring_rect(const Theme& theme) const
{
    // The item button is the focus target, at a focus visual margin of -3: the ring stands 3 DIPs
    // out around the item and reaches into the chevron slots either side of it.
    std::optional<Rect> item;
    if (focused_item_index_ == Ellipsis)
    {
        item = ellipsis_bounds();
    }
    else if (is_clickable(focused_item_index_))
    {
        item = item_bounds(focused_item_index_);
    }
    return item ? detail::focus_ring_seed(*item, Thickness{ItemFocusMargin}, theme.focus_ring_style()) : Rect{};
}

// ---- input ----

bool BreadcrumbControl::pointer_override(const PointerEvent& e)
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
                pointer_down_index_ = hit_test(e.position);
                hovered_index_      = pointer_down_index_;
                handled             = pointer_down_index_ != detail::NoIndex;
            }
            break;

        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                const std::size_t under    = hit_test(e.position);
                const std::size_t was_down = pointer_down_index_;
                handled                    = was_down != detail::NoIndex;
                pointer_down_index_        = detail::NoIndex;
                hovered_index_             = under;
                if (was_down != detail::NoIndex && was_down == under)
                {
                    activate(was_down);
                }
            }
            break;

        case PointerAction::Leave:
            hovered_index_ = detail::NoIndex;
            mark_dirty(DirtyFlags::Visual);
            break;

        default:
            break;
    }
    return handled;
}

bool BreadcrumbControl::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused())
    {
        return handled;
    }

    // The open flyout's items: Up/Down walk them, Enter/Space pick one.
    if (flyout_open_ && e.action == KeyAction::Down)
    {
        const auto rows = flyout_->rows();
        if ((e.key == Key::ArrowDown || e.key == Key::ArrowUp) && !rows.empty())
        {
            auto it = std::find(rows.begin(), rows.end(), flyout_cursor_);
            if (it == rows.end())
            {
                flyout_cursor_ = e.key == Key::ArrowDown ? rows.front() : rows.back();
            }
            else if (e.key == Key::ArrowDown && it + 1 != rows.end())
            {
                flyout_cursor_ = *(it + 1);
            }
            else if (e.key == Key::ArrowUp && it != rows.begin())
            {
                flyout_cursor_ = *(it - 1);
            }
            mark_dirty(DirtyFlags::Visual);
            return handled;
        }
        if ((e.key == Key::Enter || e.key == Key::Space) && flyout_cursor_ != detail::NoIndex)
        {
            const std::size_t item = flyout_cursor_;
            set_flyout_open(false);
            activate(item);
            return handled;
        }
    }

    if (e.key == Key::ArrowLeft || e.key == Key::ArrowRight)
    {
        const auto s = stops();
        if (e.action != KeyAction::Down || s.empty())
        {
            return handled;
        }
        auto it = std::find(s.begin(), s.end(), focused_item_index_);
        if (it == s.end())
        {
            focused_item_index_ = s.front();
        }
        else if (e.key == Key::ArrowLeft && it != s.begin())
        {
            focused_item_index_ = *(it - 1);
        }
        else if (e.key == Key::ArrowRight && it + 1 != s.end())
        {
            focused_item_index_ = *(it + 1);
        }
        mark_dirty(DirtyFlags::Visual);
        return handled;
    }

    const bool activation_key = (e.key == Key::Enter || e.key == Key::Space);
    if (!activation_key || focused_item_index_ == detail::NoIndex)
    {
        return handled;
    }

    if (e.action == KeyAction::Down)
    {
        key_down_ = true;
        mark_dirty(DirtyFlags::Visual);
    }
    else
    { // Up
        if (key_down_)
        {
            key_down_ = false;
            activate(focused_item_index_);
        }
    }
    return handled;
}

std::unique_ptr<ui::Element> Breadcrumb::build() &&
{
    auto node = std::make_unique<ui::BreadcrumbControl>(items.initial());
    if (items.reactive())
    {
        node->add_subscription(items.subscribe(
            [raw = node.get()](std::vector<std::string> v)
            {
                const detail::BoundUpdate bound; // a selection the new items clear isn't the user's change
                raw->set_items(std::move(v));
            }));
    }

    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });

    if (on_item_click)
    {
        node->on_item_click(std::move(on_item_click));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
