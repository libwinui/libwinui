// SPDX-License-Identifier: MIT
#include "winui/ui/navigation/PipsPager.hpp"

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
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

#include <cstddef>

namespace ui
{

namespace
{
// A pip's slot in a horizontal pager, swapped for Orientation::Vertical.
constexpr float PipLength    = 12.0f; // along the main axis
constexpr float PipThickness = 24.0f; // across the main axis

// The previous and next buttons: a 24x24 square in either orientation.
constexpr float PipsPagerNavButtonSize = 24.0f;

// The dot sizes. The selected dot is drawn bigger,
// never in another color.
constexpr float NormalDotDiameter   = 4.0f;
constexpr float SelectedDotDiameter = 6.0f;

// The pressed glyph shrinks to this.
constexpr float PressedGlyphScale = 0.875f;

// Under Fluent (ControlChrome::scroll_button_chromeless) background
// and border stay transparent and only the glyph changes. Other eras keep a bordered button.
void draw_nav_button(Renderer& renderer, const Theme& theme, const Rect& bounds, ButtonState state, const IconGlyph& glyph)
{
    const float size = state == ButtonState::Pressed ? glyph.size * PressedGlyphScale : 0.0f;
    if (detail::controls(theme).scroll_button_chromeless)
    {
        Color color = theme.surfaces.scrollbar_thumb;
        if (state == ButtonState::Hover || state == ButtonState::Pressed)
        {
            color = theme.palette.text_fill_secondary;
        }
        else if (state == ButtonState::Disabled)
        {
            color = theme.colors().text_disabled;
        }
        draw_icon(renderer, glyph, bounds, color, size, theme.icon_font_family);
        return;
    }
    const ButtonVisual& v = theme.for_state(state);
    renderer.fill_rounded_rect(bounds, theme.corner_radius, v.background);
    stroke_rounded_rect_inside(renderer, bounds, theme.corner_radius, v.border, theme.border_width);
    draw_icon(renderer, glyph, bounds, v.text, size, theme.icon_font_family);
}
// A page count in the int the index arithmetic uses: -1 for an open-ended count.
int pages_as_int(std::optional<std::size_t> pages)
{
    return pages ? static_cast<int>(std::min<std::size_t>(*pages, std::numeric_limits<int>::max())) : -1;
}
} // namespace

PipsPagerControl::PipsPagerControl(std::optional<std::size_t> number_of_pages, std::size_t selected_index) :
    number_of_pages_(pages_as_int(number_of_pages))
{
    const int index = static_cast<int>(std::min<std::size_t>(selected_index, std::numeric_limits<int>::max()));
    if (infinite())
    {
        selected_index_ = index;
    }
    else
    {
        selected_index_ = number_of_pages_ == 0 ? 0 : std::clamp(index, 0, number_of_pages_ - 1);
    }
    grow_infinite_items();
}

void PipsPagerControl::grow_infinite_items()
{
    if (!infinite())
    {
        return;
    }
    const int minimum = std::max(selected_index_ + 1, std::max(0, max_visible_pips_));
    if (minimum > infinite_items_)
    {
        infinite_items_ = minimum;
    }
    else if (selected_index_ == infinite_items_ - 1)
    {
        ++infinite_items_;
    }
}

void PipsPagerControl::set_number_of_pages(std::optional<std::size_t> number_of_pages)
{
    number_of_pages_ = pages_as_int(number_of_pages);
    infinite_items_  = 0;
    grow_infinite_items();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    clamp_and_select(selected_index_);
}

void PipsPagerControl::clamp_and_select(int index)
{
    if (infinite())
    {
        select(std::max(0, index));
        return;
    }
    select(number_of_pages_ == 0 ? 0 : std::clamp(index, 0, number_of_pages_ - 1));
}

void PipsPagerControl::set_max_visible_pips(std::size_t max_visible_pips)
{
    max_visible_pips_ = static_cast<int>(std::min<std::size_t>(max_visible_pips, std::numeric_limits<int>::max()));
    grow_infinite_items();
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void PipsPagerControl::set_orientation(Orientation orientation)
{
    if (orientation_ == orientation)
    {
        return;
    }
    orientation_ = orientation;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void PipsPagerControl::set_previous_button_visibility(PipsPagerButtonVisibility visibility)
{
    if (previous_button_visibility_ == visibility)
    {
        return;
    }
    previous_button_visibility_ = visibility;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual); // Collapsed<->non-Collapsed changes the reserved slot
}

void PipsPagerControl::set_next_button_visibility(PipsPagerButtonVisibility visibility)
{
    if (next_button_visibility_ == visibility)
    {
        return;
    }
    next_button_visibility_ = visibility;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void PipsPagerControl::enabled_changed()
{
    if (!is_enabled())
    {
        pointer_over_       = false;
        hovered_page_index_ = NoSlot;
        hovered_previous_ = pressed_previous_ = hovered_next_ = pressed_next_ = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

void PipsPagerControl::focus_changed(bool /*focused*/)
{
    mark_dirty(DirtyFlags::Visual);
}

void PipsPagerControl::select(int index)
{
    if (index == selected_index_)
    {
        return;
    }
    selected_index_ = index;
    grow_infinite_items();
    // The visible window moves with the selection but keeps its length, so no Layout change.
    mark_dirty(DirtyFlags::Visual);
    if (on_selection_changed_)
    {
        on_selection_changed_(static_cast<std::size_t>(selected_index_));
    }
}

int PipsPagerControl::visible_count() const
{
    if (page_count() <= 0 || max_visible_pips_ <= 0)
    {
        return 0;
    }
    return std::min(max_visible_pips_, page_count());
}

int PipsPagerControl::first_visible_index() const
{
    const int vc = visible_count();
    if (vc == 0)
    {
        return 0;
    }
    return std::clamp(selected_index_ - vc / 2, 0, page_count() - vc);
}

// Previous hides on the first page and Next on the last, unless
// wrapping around more than one page.
bool PipsPagerControl::previous_available() const
{
    if (number_of_pages_ == 0 || max_visible_pips_ <= 0)
    {
        return false;
    }
    return selected_index_ != 0 || (wrap_mode_ == PipsPagerWrapMode::Wrap && number_of_pages_ > 1);
}

bool PipsPagerControl::next_available() const
{
    if (number_of_pages_ == 0 || max_visible_pips_ <= 0)
    {
        return false;
    }
    return selected_index_ != number_of_pages_ - 1 || (wrap_mode_ == PipsPagerWrapMode::Wrap && number_of_pages_ > 1);
}

// A click has nothing to do with 0 or 1 pages, and an infinite pager never wraps back from
// the start.
bool PipsPagerControl::can_go_previous() const
{
    if (!is_enabled() || number_of_pages_ == 0 || number_of_pages_ == 1)
    {
        return false;
    }
    return selected_index_ > 0 || (wrap_mode_ == PipsPagerWrapMode::Wrap && !infinite());
}

bool PipsPagerControl::can_go_next() const
{
    if (!is_enabled() || number_of_pages_ == 0 || number_of_pages_ == 1)
    {
        return false;
    }
    return infinite() || wrap_mode_ == PipsPagerWrapMode::Wrap || selected_index_ < number_of_pages_ - 1;
}

void PipsPagerControl::go_previous()
{
    if (!can_go_previous())
    {
        return;
    }
    select(selected_index_ > 0 ? selected_index_ - 1 : number_of_pages_ - 1);
}

void PipsPagerControl::go_next()
{
    if (!can_go_next())
    {
        return;
    }
    select(infinite() || selected_index_ < number_of_pages_ - 1 ? selected_index_ + 1 : 0);
}

bool PipsPagerControl::previous_button_shown() const
{
    if (!previous_available())
    {
        return false;
    }
    switch (previous_button_visibility_)
    {
        case PipsPagerButtonVisibility::Collapsed:
            return false;
        case PipsPagerButtonVisibility::Visible:
            return true;
        case PipsPagerButtonVisibility::VisibleOnPointerOver:
            return pointer_over_ || is_focused();
    }
    return false;
}

bool PipsPagerControl::next_button_shown() const
{
    if (!next_available())
    {
        return false;
    }
    switch (next_button_visibility_)
    {
        case PipsPagerButtonVisibility::Collapsed:
            return false;
        case PipsPagerButtonVisibility::Visible:
            return true;
        case PipsPagerButtonVisibility::VisibleOnPointerOver:
            return pointer_over_ || is_focused();
    }
    return false;
}

ButtonState PipsPagerControl::previous_button_visual_state() const
{
    if (!previous_available() || !can_go_previous())
    {
        return ButtonState::Disabled;
    }
    if (pressed_previous_ && hovered_previous_)
    {
        return ButtonState::Pressed;
    }
    if (hovered_previous_)
    {
        return ButtonState::Hover;
    }
    return ButtonState::Normal;
}

ButtonState PipsPagerControl::next_button_visual_state() const
{
    if (!next_available() || !can_go_next())
    {
        return ButtonState::Disabled;
    }
    if (pressed_next_ && hovered_next_)
    {
        return ButtonState::Pressed;
    }
    if (hovered_next_)
    {
        return ButtonState::Hover;
    }
    return ButtonState::Normal;
}

Rect PipsPagerControl::previous_button_bounds() const
{
    return Rect{bounds().left(), bounds().top(), PipsPagerNavButtonSize, PipsPagerNavButtonSize};
}

Rect PipsPagerControl::next_button_bounds() const
{
    const float offset = (previous_button_reserved() ? PipsPagerNavButtonSize : 0.0f) +
                         static_cast<float>(visible_count()) * PipLength;
    return horizontal() ? Rect{bounds().left() + offset, bounds().top(), PipsPagerNavButtonSize, PipsPagerNavButtonSize}
                        : Rect{bounds().left(), bounds().top() + offset, PipsPagerNavButtonSize, PipsPagerNavButtonSize};
}

Rect PipsPagerControl::pip_bounds(int visible_slot) const
{
    const float offset = (previous_button_reserved() ? PipsPagerNavButtonSize : 0.0f) +
                         static_cast<float>(visible_slot) * PipLength;
    return horizontal() ? Rect{bounds().left() + offset, bounds().top(), PipLength, PipThickness}
                        : Rect{bounds().left(), bounds().top() + offset, PipThickness, PipLength};
}

int PipsPagerControl::pip_hit_test(Point p) const
{
    const int vc = visible_count();
    for (int slot = 0; slot < vc; ++slot)
    {
        if (pip_bounds(slot).contains(p))
        {
            return first_visible_index() + slot;
        }
    }
    return NoSlot;
}

Size PipsPagerControl::measure_override(Size available, const MeasureContext&) const
{
    float main_axis = static_cast<float>(visible_count()) * PipLength;
    if (previous_button_reserved())
    {
        main_axis += PipsPagerNavButtonSize;
    }
    if (next_button_reserved())
    {
        main_axis += PipsPagerNavButtonSize;
    }

    Size desired   = horizontal() ? Size{main_axis, PipThickness} : Size{PipThickness, main_axis};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void PipsPagerControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme     = context.theme;
    const int    vc        = visible_count();
    const int    first     = first_visible_index();
    const Color  dot_color = is_enabled() ? theme.colors().text : theme.colors().text_disabled;
    // A hovered and a pressed dot share the secondary text color, so a pressed pip needs no
    // tracking of its own.
    const Color hover_dot_color = theme.colors().text.with_alpha(0.6f);

    for (int slot = 0; slot < vc; ++slot)
    {
        const int  page = first + slot;
        const Rect cell = pip_bounds(slot);

        const bool  hovered  = is_enabled() && page == hovered_page_index_ && page != selected_index_;
        const Color color    = hovered ? hover_dot_color : dot_color;
        const float diameter = page == selected_index_ ? SelectedDotDiameter : NormalDotDiameter;
        renderer.fill_ellipse(cell.center(), diameter * 0.5f, diameter * 0.5f, color);
    }

    const IconGlyph previous_glyph = horizontal() ? glyphs::ScrollLeft : glyphs::ScrollUp;
    const IconGlyph next_glyph     = horizontal() ? glyphs::ScrollRight : glyphs::ScrollDown;
    if (previous_button_shown())
    {
        draw_nav_button(renderer, theme, previous_button_bounds(), previous_button_visual_state(), previous_glyph);
    }
    if (next_button_shown())
    {
        draw_nav_button(renderer, theme, next_button_bounds(), next_button_visual_state(), next_glyph);
    }
}

bool PipsPagerControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }

    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
            pointer_over_       = bounds().contains(e.position);
            hovered_page_index_ = pip_hit_test(e.position);
            hovered_previous_   = previous_button_reserved() && previous_button_bounds().contains(e.position);
            hovered_next_       = next_button_reserved() && next_button_bounds().contains(e.position);
            break;

        case PointerAction::Down:
            if (e.button != PointerButton::Left)
            {
                break;
            }
            if (previous_button_shown() && previous_button_bounds().contains(e.position))
            {
                pressed_previous_ = true;
                hovered_previous_ = true;
                handled           = true;
                go_previous();
            }
            else if (next_button_shown() && next_button_bounds().contains(e.position))
            {
                pressed_next_ = true;
                hovered_next_ = true;
                handled       = true;
                go_next();
            }
            else
            {
                const int page = pip_hit_test(e.position);
                if (page != NoSlot)
                {
                    handled = true;
                    clamp_and_select(page);
                }
            }
            break;

        case PointerAction::Up:
            if (e.button != PointerButton::Left)
            {
                break;
            }
            handled           = pressed_previous_ || pressed_next_ || pip_hit_test(e.position) != NoSlot;
            pressed_previous_ = false;
            pressed_next_     = false;
            hovered_previous_ = previous_button_reserved() && previous_button_bounds().contains(e.position);
            hovered_next_     = next_button_reserved() && next_button_bounds().contains(e.position);
            break;

        case PointerAction::Leave:
            pointer_over_       = false;
            hovered_page_index_ = NoSlot;
            hovered_previous_ = hovered_next_ = pressed_previous_ = pressed_next_ = false;
            break;

        case PointerAction::Wheel:
            break;
    }
    return handled;
}

bool PipsPagerControl::key_override(const KeyEvent& e)
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

    const Key decrement = horizontal() ? Key::ArrowLeft : Key::ArrowUp;
    const Key increment = horizontal() ? Key::ArrowRight : Key::ArrowDown;

    if (e.key == decrement)
    {
        go_previous();
    }
    else if (e.key == increment)
    {
        go_next();
    }
    else if (e.key == Key::Home)
    {
        if (number_of_pages_ > 0)
        {
            select(0);
        }
    }
    else if (e.key == Key::End)
    {
        if (number_of_pages_ > 0) // an infinite pager has no last page
        {
            select(number_of_pages_ - 1);
        }
    }
    return handled;
}

std::unique_ptr<ui::Element> PipsPager::build() &&
{
    auto node = std::make_unique<ui::PipsPagerControl>(number_of_pages.initial(), selected_index.initial());
    apply(number_of_pages,
          *node,
          [raw = node.get()](const std::optional<std::size_t>& v) { raw->set_number_of_pages(v); });
    apply(max_visible_pips, *node, [raw = node.get()](const std::size_t& v) { raw->set_max_visible_pips(v); });
    ui::PipsPagerControl* raw = node.get();
    apply(orientation, *node, [raw](const ui::Orientation& v) { raw->set_orientation(v); });
    apply(previous_button_visibility,
          *node,
          [raw](const ui::PipsPagerButtonVisibility& v) { raw->set_previous_button_visibility(v); });
    apply(next_button_visibility,
          *node,
          [raw](const ui::PipsPagerButtonVisibility& v) { raw->set_next_button_visibility(v); });
    apply(wrap_mode, *node, [raw](const ui::PipsPagerWrapMode& v) { raw->set_wrap_mode(v); });

    apply(selected_index, *node, [raw = node.get()](const std::size_t& v) { raw->set_selected_index(v); });
    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });

    if (auto changed = ui::write_back(selected_index, std::move(on_selection_changed)))
    {
        node->on_selection_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
