// SPDX-License-Identifier: MIT
#include "winui/ui/navigation/Carousel.hpp"

#include "../../render/Compositor.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
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
#include <optional>
#include <utility>
#include <vector>

#include <cmath>
#include <cstddef>

namespace ui
{

namespace
{
// Carousel timings.
constexpr double ButtonsShowDurationMs = 3000.0; // how long the buttons stay after the last cue
constexpr double WheelDelayMs          = 200.0;  // the quiet time before a wheel turn flips again

// The navigation buttons: 16x38 (38x16 vertical), a margin of 1, an 8 DIP arrow glyph that
// shrinks to 0.875 while pressed.
constexpr float ButtonThickness      = 16.0f;
constexpr float ButtonLength         = 38.0f;
constexpr float CarouselButtonMargin = 1.0f;
constexpr float PressedScale         = 0.875f;

// The slide: 300 ms on ControlFastOutSlowIn.
constexpr double SlideMs = 300.0;
} // namespace

// ---- selection ----

void CarouselControl::select_clamped(int index)
{
    if (items_.empty())
    {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(items_.size()) - 1);
    if (index == selected_index_)
    {
        return;
    }
    const int previous = selected_index_;
    selected_index_    = index;
    // Only a step to a neighbor animates. Anything else lands at once.
    if (use_animations_ && std::abs(index - previous) == 1 && !bounds().empty())
    {
        slide_from_    = position_;
        slide_elapsed_ = 0.0;
    }
    else
    {
        position_      = static_cast<float>(index);
        slide_elapsed_ = -1.0;
        place_items();
    }
    mark_dirty(DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(static_cast<std::size_t>(selected_index_));
    }
}

bool CarouselControl::move(int delta)
{
    const int before = selected_index_;
    select_clamped(selected_index_ + delta);
    return selected_index_ != before;
}

void CarouselControl::set_orientation(Orientation orientation)
{
    orientation_ = orientation;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void CarouselControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_previous_ = pressed_previous_ = hovered_next_ = pressed_next_ = false;
        show_buttons_                                                         = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

void CarouselControl::focus_changed(bool /*focused*/)
{
    // Keyboard focus shows the buttons.
    if (is_focused() && focus_visible())
    {
        show_buttons();
    }
    mark_dirty(DirtyFlags::Visual);
}

void CarouselControl::show_buttons()
{
    show_buttons_    = true;
    buttons_left_ms_ = ButtonsShowDurationMs;
    mark_dirty(DirtyFlags::Visual);
}

// ---- buttons ----

bool CarouselControl::previous_button_visible() const
{
    // Nothing before the first item, and never with one item or none.
    return is_enabled() && show_buttons_ && items_.size() > 1 && selected_index_ > 0;
}

bool CarouselControl::next_button_visible() const
{
    return is_enabled() && show_buttons_ && items_.size() > 1 && selected_index_ < static_cast<int>(items_.size()) - 1;
}

Rect CarouselControl::previous_button_bounds() const
{
    if (horizontal())
    {
        return Rect{bounds().x + CarouselButtonMargin, bounds().center().y - ButtonLength * 0.5f, ButtonThickness, ButtonLength};
    }
    return Rect{bounds().center().x - ButtonLength * 0.5f, bounds().y + CarouselButtonMargin, ButtonLength, ButtonThickness};
}

Rect CarouselControl::next_button_bounds() const
{
    if (horizontal())
    {
        return Rect{bounds().right() - CarouselButtonMargin - ButtonThickness,
                    bounds().center().y - ButtonLength * 0.5f,
                    ButtonThickness,
                    ButtonLength};
    }
    return Rect{bounds().center().x - ButtonLength * 0.5f,
                bounds().bottom() - CarouselButtonMargin - ButtonThickness,
                ButtonLength,
                ButtonThickness};
}

// ---- layout ----

Size CarouselControl::measure_override(Size available, const MeasureContext& context) const
{
    // Every item is offered the whole view (Carousel sizes its items to itself).
    Size largest{};
    for (const auto& item : items_)
    {
        const Size s = item->measure(available, context);
        largest      = {std::max(largest.width, s.width), std::max(largest.height, s.height)};
    }
    return {std::isfinite(available.width) ? available.width : largest.width,
            std::isfinite(available.height) ? available.height : largest.height};
}

void CarouselControl::arrange_override(const Rect&, const ArrangeContext&)
{
    place_items();
}

std::pair<std::size_t, std::size_t> CarouselControl::visible_range() const
{
    if (items_.empty())
    {
        return {0, 0};
    }
    const auto first = static_cast<std::size_t>(std::max(0.0f, std::floor(position_)));
    const auto last  = std::min(items_.size(), static_cast<std::size_t>(std::max(0.0f, std::ceil(position_))) + 1);
    return {std::min(first, items_.size()), last};
}

void CarouselControl::place_items()
{
    const float length = extent();
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const float offset = (static_cast<float>(i) - position_) * length;
        items_[i]->arrange(horizontal() ? Rect{bounds().x + offset, bounds().y, bounds().width, bounds().height}
                                        : Rect{bounds().x, bounds().y + offset, bounds().width, bounds().height});
    }
}

// ---- render ----

void CarouselControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    // The background is the window's own base color.
    renderer.fill_rounded_rect(bounds(), theme.corner_radius, background_.resolve(theme).value_or(theme.colors().background));

    renderer.push_clip(bounds());
    const auto [first, last] = visible_range();
    for (std::size_t i = first; i < last; ++i)
    {
        items_[i]->render(renderer, context);
    }
    renderer.pop_clip();
    if (const std::optional<Color> border = border_brush_.resolve(theme))
    {
        draw_border(renderer, bounds(), std::nullopt, border, border_thickness_, theme.corner_radius);
    }

    const auto draw_button = [&](const Rect& r, bool hovered, bool pressed, IconGlyph glyph)
    {
        // A button's background is the in-app acrylic in every state. Only the arrow answers
        // to hover and press.
        fill_overlay_surface(renderer, theme, r, theme.corner_radius, theme.colors().overlay);
        Color arrow = theme.surfaces.scrollbar_thumb;
        if (hovered || pressed)
        {
            arrow = detail::controls(theme).scroll_button_chromeless
                        ? theme.palette.text_fill_secondary
                        : theme.colors().text;
        }
        const float size = pressed && hovered ? glyph.size * PressedScale : 0.0f;
        draw_icon(renderer, glyph, r, arrow, size, theme.icon_font_family);
    };
    if (previous_button_visible())
    {
        draw_button(previous_button_bounds(),
                    hovered_previous_,
                    pressed_previous_,
                    horizontal() ? glyphs::ScrollLeft : glyphs::ScrollUp);
    }
    if (next_button_visible())
    {
        draw_button(next_button_bounds(), hovered_next_, pressed_next_, horizontal() ? glyphs::ScrollRight : glyphs::ScrollDown);
    }
}

// ---- input ----

bool CarouselControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    if (e.action == PointerAction::Wheel)
    {
        (void)wheel_override(e);
        return false;
    }

    const bool over_previous = previous_button_visible() && previous_button_bounds().contains(e.position);
    const bool over_next     = next_button_visible() && next_button_bounds().contains(e.position);
    switch (e.action)
    {
        case PointerAction::Move:
            if (bounds().contains(e.position))
            {
                show_buttons(); // a mouse moving over the view shows them
            }
            hovered_previous_ = over_previous;
            hovered_next_     = over_next;
            keep_buttons_     = over_previous || over_next; // the buttons stay while hovered
            break;
        case PointerAction::Down:
            if (e.button == PointerButton::Left && (over_previous || over_next))
            {
                pressed_previous_ = over_previous;
                pressed_next_     = over_next;
                mark_dirty(DirtyFlags::Visual);
                return false; // the button has it, not the item under it
            }
            break;
        case PointerAction::Up:
            if (pressed_previous_ || pressed_next_)
            {
                // A Button clicks on release, over the button it was pressed on.
                const bool previous = pressed_previous_ && over_previous;
                const bool next     = pressed_next_ && over_next;
                pressed_previous_ = pressed_next_ = false;
                if (previous)
                {
                    move(-1);
                }
                else if (next)
                {
                    move(1);
                }
                mark_dirty(DirtyFlags::Visual);
                return false;
            }
            break;
        case PointerAction::Leave:
            hovered_previous_ = hovered_next_ = pressed_previous_ = pressed_next_ = false;
            keep_buttons_                                                         = false;
            break;
        case PointerAction::Wheel:
            break;
    }
    mark_dirty(DirtyFlags::Visual);
    return (e.action == PointerAction::Down || e.action == PointerAction::Up) && (over_previous || over_next);
}

void CarouselControl::hit_test_children(Point /*p*/, std::vector<Element*>& out) const
{
    // Only the items in view; the one drawn last is on top.
    const auto [first, last] = visible_range();
    for (std::size_t i = last; i-- > first;)
    {
        out.push_back(items_[i].get());
    }
}

bool CarouselControl::covers_children_at(Point p) const
{
    // The previous and next buttons sit over the items.
    return (previous_button_visible() && previous_button_bounds().contains(p)) ||
           (next_button_visible() && next_button_bounds().contains(p));
}

bool CarouselControl::wheel_override(const PointerEvent& e)
{
    if (!is_enabled() || !bounds().contains(e.position))
    {
        return false;
    }
    // An item that scrolls had the turn first; it reaches the Carousel only unhandled.
    if (e.modifiers.ctrl || e.wheel_delta == 0.0f)
    {
        return false;
    }

    // A wheel turn flips on a change of direction or 200 ms after the last turn. Every
    // turn restarts the clock, so a touchpad flick flips once; a turn at an end stays unhandled.
    const float direction = e.wheel_delta > 0.0f ? 1.0f : -1.0f;
    const bool  can_flip  = direction != last_wheel_ || since_wheel_ms_ > WheelDelayMs;
    since_wheel_ms_       = 0.0;
    if (!can_flip)
    {
        return true;
    }
    if (move(direction > 0.0f ? 1 : -1))
    {
        last_wheel_ = direction;
        return true;
    }
    return false;
}

bool CarouselControl::takes_arrow_key(Key key) const
{
    return horizontal() ? (key == Key::ArrowLeft || key == Key::ArrowRight)
                        : (key == Key::ArrowUp || key == Key::ArrowDown);
}

bool CarouselControl::key_override(const KeyEvent& e)
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
    const Key back    = horizontal() ? Key::ArrowLeft : Key::ArrowUp;
    const Key forward = horizontal() ? Key::ArrowRight : Key::ArrowDown;
    if (e.key == back || e.key == Key::PageUp)
    {
        move(-1);
    }
    else if (e.key == forward || e.key == Key::PageDown)
    {
        move(1);
    }
    else if (e.key == Key::Home)
    {
        select_clamped(0);
    }
    else if (e.key == Key::End)
    {
        select_clamped(static_cast<int>(items_.size()) - 1);
    }
    return handled;
}

// ---- time ----

void CarouselControl::tick_override(Duration delta)
{
    since_wheel_ms_ += delta.count();

    if (show_buttons_ && !keep_buttons_)
    {
        buttons_left_ms_ -= delta.count();
        if (buttons_left_ms_ <= 0.0)
        {
            show_buttons_ = false; // the quiet time is up: the buttons go
            mark_dirty(DirtyFlags::Visual);
        }
        else
        {
            request_tick_in(Duration{buttons_left_ms_}); // one wake when the three quiet seconds are up
        }
    }

    if (slide_elapsed_ >= 0.0)
    {
        slide_elapsed_ += animation_delta(delta).count();
        const double t      = std::min(1.0, slide_elapsed_ / SlideMs);
        const float  eased  = easing::evaluate(easing::ControlFastOutSlowIn, t);
        const float  target = static_cast<float>(selected_index_);
        position_           = slide_from_ + (target - slide_from_) * eased;
        if (t >= 1.0)
        {
            position_      = target;
            slide_elapsed_ = -1.0;
        }
        place_items();
        mark_dirty(DirtyFlags::Visual);
    }
}

// ---- tree ----

void CarouselControl::collect_focusables(std::vector<Element*>& out)
{
    if (focusable())
    {
        out.push_back(this);
    }
    // Only the item on show has reachable content.
    if (selected_index_ >= 0)
    {
        items_[static_cast<std::size_t>(selected_index_)]->collect_focusables(out);
    }
}

void CarouselControl::collect_live(std::vector<Element*>& out)
{
    if (selected_index_ >= 0)
    {
        detail::TreeAccess::collect_live(*items_[static_cast<std::size_t>(selected_index_)], out);
    }
}

std::unique_ptr<ui::Element> Carousel::build() &&
{
    auto                 node = std::make_unique<ui::CarouselControl>();
    ui::CarouselControl* raw  = node.get();
    for (auto& item : content.nodes)
    {
        node->add_item(std::move(item));
    }
    apply(orientation, *node, [raw = node.get()](const Orientation& v) { raw->set_orientation(v); });
    apply(use_animations, *node, [raw = node.get()](const bool& v) { raw->set_use_animations(v); });
    apply(background, *node, [raw = node.get()](const Paint& v) { raw->set_background(v); });
    apply(border_brush, *node, [raw = node.get()](const Paint& v) { raw->set_border_brush(v); });
    apply(border_thickness, *node, [raw = node.get()](const Thickness& v) { raw->set_border_thickness(v); });

    apply(selected_index, *node, [raw](const std::size_t& v) { raw->set_selected_index(v); });
    apply(enabled, *node, [raw](const bool& v) { raw->set_enabled(v); });

    if (auto changed = ui::write_back(selected_index, std::move(on_selection_changed)))
    {
        node->on_selection_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, raw);
    return node;
}

} // namespace ui
