// SPDX-License-Identifier: MIT
#include "winui/ui/input/DropdownButton.hpp"

#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/media/AnimatedIcon.hpp"
#include "winui/ui/media/AnimatedVisuals.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace ui
{

namespace
{
// The chevron's AnimatedIcon is 12x12 with an 8 left margin.
constexpr float DropdownButtonChevronSize   = 12.0f;
constexpr float DropdownButtonChevronMargin = 8.0f;
} // namespace

DropdownButtonControl::DropdownButtonControl() : DropdownButtonControl(std::string{})
{
}

DropdownButtonControl::DropdownButtonControl(std::string text) : ButtonControl(std::move(text))
{
    static const AnimatedChevronDownSmallVisualSource ChevronVisual;
    chevron_ = std::make_unique<AnimatedIconControl>();
    chevron_->set_source(&ChevronVisual);
    // The ChevronDownSmall glyph (0xE96E) at 8px.
    chevron_->set_fallback(IconGlyph{0xE96E, "Segoe Fluent Icons", 8.0f});
    chevron_->set_state("Normal");
    set_parent_of(*chevron_, this);
    set_trailing_reserve(DropdownButtonChevronMargin + DropdownButtonChevronSize);
}

DropdownButtonControl::~DropdownButtonControl() = default;

bool DropdownButtonControl::is_flyout_open() const
{
    return flyout() != nullptr && flyout()->is_open();
}

Size DropdownButtonControl::measure_override(Size available, const MeasureContext& context) const
{
    Size desired = ButtonControl::measure_override(available, context);
    (void)chevron_->measure(Size{DropdownButtonChevronSize, DropdownButtonChevronSize}, context);
    desired.height = std::max(desired.height, DropdownButtonChevronSize + padding().vertical());
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void DropdownButtonControl::arrange_override(const Rect& final_rect, const ArrangeContext& context)
{
    ButtonControl::arrange_override(final_rect, context);
    // At the right edge inside the button's padding, centered vertically.
    const Rect inner = final_rect.inset(padding());
    chevron_->arrange(Rect{inner.x + inner.width - DropdownButtonChevronSize,
                           inner.y + (inner.height - DropdownButtonChevronSize) * 0.5f,
                           DropdownButtonChevronSize,
                           DropdownButtonChevronSize});
}

void DropdownButtonControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    ButtonControl::render_override(renderer, context);
    // The secondary text color, tertiary on hover and press, the label's own color when disabled.
    Color color = theme.palette.text_fill_secondary;
    switch (visual_state())
    {
        case ButtonState::Hover:
        case ButtonState::Pressed:
            color = theme.palette.text_fill_tertiary;
            break;
        case ButtonState::Disabled:
            color = current_visual(theme).text;
            break;
        case ButtonState::Normal:
            break;
    }
    if (variant() == ButtonVariant::Accent || !detail::has_fluent_palette(theme))
    {
        // The secondary grays are for a neutral Fluent face; anywhere else, the label's color.
        color = current_visual(theme).text;
    }
    if (theme.disclosure_visual == ThemeDisclosureVisual::ClassicArrow)
    {
        // The combo box's solid arrow in the label's color, moving with the press like the label.
        const Rect& at    = chevron_->bounds();
        const Point shift = visual_state() == ButtonState::Pressed ? detail::controls(theme).pressed_content_offset
                                                                   : Point{0.0f, 0.0f};
        draw_classic_arrow(renderer,
                           Rect{at.x + shift.x, at.y + shift.y, at.width, at.height},
                           ArrowDirection::Down,
                           current_visual(theme).text);
        return;
    }
    chevron_->render_in(renderer, theme, color);
}

bool DropdownButtonControl::pointer_override(const PointerEvent& e)
{
    const bool handled = ButtonControl::pointer_override(e);
    sync_chevron();
    return handled;
}

bool DropdownButtonControl::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, false, takes_enter_key(), false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    ButtonControl::key_override(e);
    sync_chevron();
    return handled;
}

void DropdownButtonControl::tick_override(Duration delta)
{
    ButtonControl::tick_override(delta);
    sync_chevron(); // catches what changes the state without an event: set_enabled(), focus loss
    chevron_->tick(delta);
}

bool DropdownButtonControl::set_expanded_state(bool expanded)
{
    FlyoutBase* const flyout = this->flyout();
    if (flyout == nullptr || !is_enabled())
    {
        return false;
    }
    // Shows the flyout at this button, or hides it.
    if (expanded)
    {
        flyout->show();
    }
    else
    {
        flyout->hide();
    }
    return true;
}

void DropdownButtonControl::sync_chevron()
{
    // The icon's state follows the button's: PointerOver and Pressed, Normal otherwise
    // (Disabled included).
    switch (visual_state())
    {
        case ButtonState::Hover:
            chevron_->set_state("PointerOver");
            break;
        case ButtonState::Pressed:
            chevron_->set_state("Pressed");
            break;
        case ButtonState::Normal:
        case ButtonState::Disabled:
            chevron_->set_state("Normal");
            break;
    }
}

std::unique_ptr<ui::Element> DropdownButton::build() &&
{
    auto node = std::make_unique<ui::DropdownButtonControl>();
    apply(variant, *node, [raw = node.get()](const ButtonVariant& v) { raw->set_variant(v); });
    apply(text, *node, [raw = node.get()](const std::string& value) { raw->set_text(value); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    if (command)
    {
        node->set_command(command, std::move(command_parameter));
    }
    if (on_click)
    {
        node->on_click(std::move(on_click));
    }
    if (padding.reactive() || padding.initial())
    {
        apply(padding, *node, [raw = node.get()](const std::optional<Thickness>& v) { raw->set_padding(v); });
    }
    if (content)
    {
        node->set_content(content.take());
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
