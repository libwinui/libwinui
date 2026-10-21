// SPDX-License-Identifier: MIT
#include "winui/ui/input/Button.hpp"

#include "../../render/Compositor.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Launcher.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
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
// A soft white wash over the button's own fill, faded in and out by the compositor.
constexpr float ButtonHotOverlayOpacity = 0.16f;

// Theme::for_state() only covers the neutral set. The other styles are picked here, since only
// Button has styles.
ButtonVisual select_visual(const Theme& theme, ButtonVariant style, ButtonState state)
{
    switch (style)
    {
        case ButtonVariant::Hyperlink:
        {
            // Hyperlink: the subtle button's backgrounds and borders with
            // AccentTextFillColor text (colors.link and its state overrides).
            ButtonVisual visual = select_visual(theme, ButtonVariant::Subtle, state);
            switch (state)
            {
                case ButtonState::Hover:
                    visual.text = theme.surfaces.hyperlink_hover.value_or(theme.colors().link);
                    break;
                case ButtonState::Pressed:
                    visual.text = theme.surfaces.hyperlink_pressed.value_or(theme.colors().link);
                    break;
                case ButtonState::Disabled:
                    visual.text = theme.surfaces.hyperlink_disabled.value_or(visual.text);
                    break;
                case ButtonState::Normal:
                    visual.text = theme.colors().link;
                    break;
            }
            return visual;
        }
        case ButtonVariant::Accent:
            switch (state)
            {
                case ButtonState::Hover:
                    return theme.accent_button_hover;
                case ButtonState::Pressed:
                    return theme.accent_button_pressed;
                case ButtonState::Disabled:
                    return theme.accent_button_disabled;
                case ButtonState::Normal:
                    break;
            }
            return theme.accent_button_normal;

        case ButtonVariant::Subtle:
            switch (state)
            {
                case ButtonState::Hover:
                    return theme.subtle_button_hover;
                case ButtonState::Pressed:
                    return theme.subtle_button_pressed;
                case ButtonState::Disabled:
                    return theme.subtle_button_disabled;
                case ButtonState::Normal:
                    break;
            }
            return theme.subtle_button_normal;

        case ButtonVariant::Neutral:
            break;
    }
    return theme.for_state(state);
}
} // namespace

ButtonControl::ButtonControl(std::string text) : text_(std::move(text))
{
}

ButtonVisual ButtonControl::style_visual(const Theme& theme, ButtonVariant style, ButtonState state)
{
    return select_visual(theme, style, state);
}

ButtonVisual ButtonControl::current_visual(const Theme& theme) const
{
    return select_visual(theme, variant_, visual_state());
}

void ButtonControl::enabled_changed()
{
    if (!is_enabled())
    {
        // Disabled controls drop all transient interaction state.
        hovered_      = false;
        pointer_down_ = false;
        key_down_     = false;
    }
}

void ButtonControl::focus_changed(bool /*focused*/)
{
    if (!is_focused())
    {
        key_down_ = false; // losing focus cancels a keyboard press
    }
    mark_dirty(DirtyFlags::Visual); // focus ring appears/disappears; size is unaffected
}

Size ButtonControl::measure_override(Size available, const MeasureContext& context) const
{
    content_padding_ = padding_override_.value_or(context.theme.padding); // cached for arrange_override(), which has no Theme

    Size content_size;
    if (content_)
    {
        const Size inner_available{
            std::max(0.0f, available.width - content_padding_.horizontal() - trailing_reserve_),
            std::max(0.0f, available.height - content_padding_.vertical()),
        };
        content_size = content_->measure(inner_available, context);
    }
    else
    {
        content_size = context.measurer.measure(text_, context.theme.text_style);
    }

    Size desired{content_size.width + content_padding_.horizontal() + trailing_reserve_,
                 content_size.height + content_padding_.vertical()};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void ButtonControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    if (content_)
    {
        Rect inner  = final_rect.inset(content_padding_);
        inner.width = std::max(0.0f, inner.width - trailing_reserve_);
        content_->arrange(inner);
    }
}

bool ButtonControl::is_pressed() const
{
    if (!is_enabled())
    {
        return false;
    }
    // Mouse press shows "pressed" only while the pointer is still over the control;
    // a held keyboard activation always shows pressed.
    return (pointer_down_ && hovered_) || key_down_;
}

ButtonState ButtonControl::visual_state() const
{
    if (!is_enabled())
    {
        return ButtonState::Disabled;
    }
    if (is_pressed())
    {
        return ButtonState::Pressed;
    }
    if (hovered_)
    {
        return ButtonState::Hover;
    }
    return ButtonState::Normal;
}

bool ButtonControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    return handle_pointer_state(e);
}

bool ButtonControl::handle_pointer_state(const PointerEvent& e)
{
    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
            hovered_ = bounds().contains(e.position);
            break;

        case PointerAction::Down:
            if (e.button == PointerButton::Left && bounds().contains(e.position))
            {
                pointer_down_ = true;
                hovered_      = true;
                handled       = true;
            }
            break;

        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                const bool inside   = bounds().contains(e.position);
                const bool was_down = pointer_down_;
                pointer_down_       = false;
                hovered_            = inside;
                handled             = was_down;
                if (was_down && inside)
                {
                    activate();
                }
            }
            break;

        case PointerAction::Leave:
            hovered_ = false;
            break;
        case PointerAction::Wheel:
            break;
    }
    return handled;
}

bool ButtonControl::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, false, takes_enter_key(), false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused())
    {
        return handled;
    }
    const bool activation_key = (e.key == Key::Enter || e.key == Key::Space);
    if (!activation_key)
    {
        return handled;
    }

    if (e.action == KeyAction::Down)
    {
        key_down_ = true;
    }
    else
    { // Up
        if (key_down_)
        {
            key_down_ = false;
            activate();
        }
    }
    return handled;
}

void ButtonControl::activate()
{
    if (!is_enabled())
    {
        return;
    }
    // Opens the associated flyout. The flyout goes before the click handler
    // so nothing touches `this` after a handler that may tear the page down.
    if (flyout_ != nullptr)
    {
        flyout_->toggle();
    }
    on_activated();
    // Copied first, because the handler may tear the page down. The click handler runs first,
    // then the command, then the URI opens.
    const Command* const command   = command_;
    const std::string    parameter = command_ != nullptr ? command_parameter_ : std::string{};
    std::string          uri       = navigate_uri_;
    if (on_click_)
    {
        on_click_();
    }
    if (command != nullptr)
    {
        (*command)(parameter);
    }
    launch_uri(uri);
}

void ButtonControl::set_command(CommandRef command, std::string parameter)
{
    command_changed_.reset();
    command_           = command;
    command_parameter_ = std::move(parameter);
    if (command_ != nullptr)
    {
        command_changed_ = command_->observe([this] { command_changed(); });
    }
    command_changed();
}

bool ButtonControl::handle_global_shortcut(const KeyEvent& e)
{
    if (command_ == nullptr || !is_enabled() || !command_->current().shortcut.matches(e))
    {
        return false;
    }
    activate(); // last: a handler may tear the page down
    return true;
}

std::string ButtonControl::tool_tip() const
{
    std::string tip = Element::tool_tip();
    if (tip.empty() && command_ != nullptr)
    {
        tip = command_->current().description;
    }
    return tip;
}

void ButtonControl::command_changed()
{
    // A derived button may show the command's label, icon or shortcut, so any change relayouts.
    set_enabled_gate(command_ == nullptr || command_->current().enabled);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

bool ButtonControl::hot() const
{
    return is_enabled() && (hovered_ || is_pressed());
}

bool ButtonControl::washed(const Theme& theme) const
{
    // Metro's press is already a color inversion in the ButtonVisual, so the wash only tracks
    // hover there.
    return detail::controls(theme).press_state_flip ? (is_enabled() && hovered_ && !is_pressed()) : hot();
}

void ButtonControl::sync_composition_override(detail::Compositor& compositor, const Theme& theme)
{
    detail::TreeAccess::set_composes_visuals(*this);
    // Under a popup, a clip or a transform the compositor's wash would show over what hides the
    // control, so render_override() draws it instead.
    if (detail::TreeAccess::visuals_covered(*this))
    {
        compositor.remove_visual(visual_id());
        composed_hot_ = false;
        return;
    }
    const bool now_hot = washed(theme);
    // Geometry can change (layout) without hotness changing; keep the overlay's bounds
    // current regardless, but only spend an animation on an actual hot/not-hot flip.
    compositor.sync_visual(visual_id(), bounds(), theme.resolved_button_corner_radius(), colors::White);
    if (now_hot == composed_hot_)
    {
        return;
    }
    compositor.animate_opacity(visual_id(),
                               composed_hot_ ? ButtonHotOverlayOpacity : 0.0f,
                               now_hot ? ButtonHotOverlayOpacity : 0.0f,
                               theme.anim_faster);
    composed_hot_ = now_hot;
}

void ButtonControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    ButtonVisual v     = current_visual(theme);
    // Control.Background/Foreground set on the button: the template binds them in its Normal state,
    // and every other state's setter replaces them with the theme's.
    if (visual_state() == ButtonState::Normal)
    {
        if (background_resource_ != nullptr)
        {
            v.background = background_resource_(theme);
            v.background_gradient.reset();
        }
        else if (background_override_)
        {
            v.background = *background_override_;
            v.background_gradient.reset();
        }
        if (foreground_resource_ != nullptr)
        {
            v.text = foreground_resource_(theme);
        }
        else if (foreground_override_)
        {
            v.text = *foreground_override_;
        }
    }

    // Never the flat-border fallback, only a bevel or the elevation gradient. The corner radius
    // differs only in Windows 3.x, where the push button is the one rounded surface.
    draw_control_chrome(renderer, bounds(), theme.resolved_button_corner_radius(), v, theme.border_width);

    if (content_)
    {
        // Content drawn in the default text color follows the button's state, like the plain label.
        content_->render(renderer, theme.with_content_colors(v.text));
    }
    else
    {
        TextStyle label_style = theme.text_style;
        label_style.effect    = v.text_effect;
        // Classic packs shift a pressed label one pixel down and right as the bevel inverts
        // (ControlChrome::pressed_content_offset); zero, and so a no-op, in every other era.
        const Point offset = visual_state() == ButtonState::Pressed ? detail::controls(theme).pressed_content_offset
                                                                    : Point{0.0f, 0.0f};
        const Rect  label{bounds().x + offset.x,
                          bounds().y + offset.y,
                          std::max(0.0f, bounds().width - trailing_reserve_),
                          bounds().height};
        renderer.draw_text(text_, label, v.text, label_style);
    }
    if (detail::TreeAccess::visuals_covered(*this) && washed(theme))
    {
        renderer.fill_rounded_rect(bounds(),
                                   theme.resolved_button_corner_radius(),
                                   colors::White.with_alpha(ButtonHotOverlayOpacity));
    }
}

std::unique_ptr<ui::Element> Button::build() &&
{
    auto node = std::make_unique<ui::ButtonControl>();
    apply(variant, *node, [raw = node.get()](const ButtonVariant& v) { raw->set_variant(v); });

    apply(text, *node, [raw = node.get()](const std::string& value) { raw->set_text(value); });

    // `.enabled` and the command's `enabled` both have to allow a click, and the button follows
    // either as it changes.
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    if (command)
    {
        node->set_command(command, std::move(command_parameter));
    }
    if (on_click)
    {
        node->on_click(std::move(on_click));
    }
    if (background.reactive() || !background.initial().empty())
    {
        apply(background,
              *node,
              [raw = node.get()](const Paint& v) { ui::detail::set_paint(v, [raw](auto c) { raw->set_background(c); }); });
    }
    if (foreground.reactive() || !foreground.initial().empty())
    {
        apply(foreground,
              *node,
              [raw = node.get()](const Paint& v)
              {
                  if (v.resource() != nullptr)
                  {
                      raw->set_foreground(v.resource());
                  }
                  else
                  {
                      raw->set_foreground(v.color());
                  }
              });
    }
    if (navigate_uri.reactive() || !navigate_uri.initial().empty())
    {
        apply(navigate_uri, *node, [raw = node.get()](const std::string& v) { raw->set_navigate_uri(v); });
    }
    if (on_context_requested)
    {
        node->on_context_requested(std::move(on_context_requested));
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
