// SPDX-License-Identifier: MIT
#include "winui/ui/input/SplitButton.hpp"

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
#include "winui/ui/Command.hpp"
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
// The size of each part.
constexpr float PartSize               = 35.0f;
constexpr float Divider                = 1.0f;
constexpr float SplitButtonChevronSize = 12.0f;
// The chevron sits 12 DIPs in from the secondary part's right edge.
constexpr float ChevronRightInset = 12.0f;
} // namespace

SplitButtonControl::SplitButtonControl() : SplitButtonControl(std::string{})
{
}

SplitButtonControl::SplitButtonControl(std::string text) : text_(std::move(text))
{
    static const AnimatedChevronDownSmallVisualSource ChevronVisual;
    chevron_ = std::make_unique<AnimatedIconControl>();
    chevron_->set_source(&ChevronVisual);
    chevron_->set_fallback(IconGlyph{0xE96E, "Segoe Fluent Icons", 8.0f});
    chevron_->set_state("Normal");
    set_parent_of(*chevron_, this);
}

SplitButtonControl::~SplitButtonControl() = default;

void SplitButtonControl::enabled_changed()
{
    if (!is_enabled())
    {
        primary_hovered_ = secondary_hovered_ = false;
        primary_down_ = secondary_down_ = key_down_ = false;
    }
    sync_chevron();
    mark_dirty(DirtyFlags::Visual);
}

void SplitButtonControl::set_command(CommandRef command, std::string parameter)
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

std::string SplitButtonControl::tool_tip() const
{
    std::string tip = Element::tool_tip();
    if (tip.empty() && command_ != nullptr)
    {
        tip = command_->current().description;
    }
    return tip;
}

void SplitButtonControl::command_changed()
{
    // The command disables the primary part only.
    command_enabled_ = command_ == nullptr || command_->current().enabled;
    if (!command_enabled_)
    {
        primary_hovered_ = primary_down_ = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

bool SplitButtonControl::is_flyout_open() const
{
    return flyout_ != nullptr && flyout_->is_open();
}

void SplitButtonControl::open_flyout()
{
    if (flyout_ == nullptr || !is_enabled())
    {
        return;
    }
    // Below the button, aligned to its leading edge.
    flyout_->set_placement(PopupPlacement::Below);
    flyout_->set_placement_alignment(PopupPlacementAlignment::Leading);
    flyout_->show();
    sync_chevron();
}

void SplitButtonControl::close_flyout()
{
    if (flyout_ != nullptr)
    {
        flyout_->hide();
    }
    sync_chevron();
}

SplitButtonState SplitButtonControl::visual_state() const
{
    // The visual state for the mouse; a held Space or Enter is the touch look.
    if (!is_enabled())
    {
        return SplitButtonState::Disabled;
    }
    if (is_flyout_open())
    {
        return SplitButtonState::FlyoutOpen;
    }
    if (key_down_)
    {
        return SplitButtonState::TouchPressed;
    }
    if (primary_down_ && primary_hovered_)
    {
        return SplitButtonState::PrimaryPressed;
    }
    if (primary_hovered_)
    {
        return SplitButtonState::PrimaryPointerOver;
    }
    if (secondary_down_ && secondary_hovered_)
    {
        return SplitButtonState::SecondaryPressed;
    }
    if (secondary_hovered_)
    {
        return SplitButtonState::SecondaryPointerOver;
    }
    return SplitButtonState::Normal;
}

Rect SplitButtonControl::primary_rect() const
{
    return Rect{bounds().x, bounds().y, std::max(0.0f, bounds().width - Divider - PartSize), bounds().height};
}

Rect SplitButtonControl::secondary_rect() const
{
    const float width = std::min(PartSize, bounds().width);
    return Rect{bounds().x + bounds().width - width, bounds().y, width, bounds().height};
}

Size SplitButtonControl::measure_override(Size available, const MeasureContext& context) const
{
    padding_ = padding_override_.value_or(context.theme.padding);
    Size content_size;
    if (content_)
    {
        const Size inner{
            std::max(0.0f, available.width - Divider - PartSize - padding_.horizontal()),
            std::max(0.0f, available.height - padding_.vertical()),
        };
        content_size = content_->measure(inner, context);
    }
    else
    {
        content_size = context.measurer.measure(text_, context.theme.text_style);
    }
    (void)chevron_->measure(Size{SplitButtonChevronSize, SplitButtonChevronSize}, context);

    const float primary = std::max(PartSize, content_size.width + padding_.horizontal());
    Size desired{primary + Divider + PartSize, std::max(content_size.height + padding_.vertical(), SplitButtonChevronSize)};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void SplitButtonControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    (void)final_rect; // bounds() is final_rect already
    if (content_)
    {
        content_->arrange(primary_rect().inset(padding_));
    }
    const Rect secondary = secondary_rect();
    chevron_->arrange(Rect{secondary.x + secondary.width - ChevronRightInset - SplitButtonChevronSize,
                           secondary.y + (secondary.height - SplitButtonChevronSize) * 0.5f,
                           SplitButtonChevronSize,
                           SplitButtonChevronSize});
}

ButtonVisual SplitButtonControl::part_visual(const Theme& theme, ButtonState part_state) const
{
    if (!checked())
    {
        return ButtonControl::style_visual(theme, ButtonVariant::Neutral, part_state);
    }
    // Checked takes the accent button's colors. A disabled button looks the same checked or not.
    if (part_state == ButtonState::Disabled)
    {
        return ButtonControl::style_visual(theme, ButtonVariant::Neutral, part_state);
    }
    const ButtonVisual accent   = ButtonControl::style_visual(theme, ButtonVariant::Accent, ButtonState::Normal);
    const ButtonVisual standard = ButtonControl::style_visual(theme, ButtonVariant::Neutral, ButtonState::Normal);
    if (accent.background == standard.background && accent.border == standard.border && accent.text == standard.text)
    {
        // An era with no accent button: checked is pushed in, as ToggleButton draws it there.
        return ButtonControl::style_visual(theme, ButtonVariant::Neutral, ButtonState::Pressed);
    }
    return ButtonControl::style_visual(theme, ButtonVariant::Accent, part_state);
}

void SplitButtonControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    // Each part's state, from the control's.
    ButtonState primary   = ButtonState::Normal;
    ButtonState secondary = ButtonState::Normal;
    const auto  state     = visual_state();
    switch (state)
    {
        case SplitButtonState::Disabled:
            primary = secondary = ButtonState::Disabled;
            break;
        case SplitButtonState::FlyoutOpen:
        case SplitButtonState::TouchPressed:
            primary = secondary = ButtonState::Pressed;
            break;
        case SplitButtonState::PrimaryPointerOver:
            primary = ButtonState::Hover;
            break;
        case SplitButtonState::PrimaryPressed:
            primary = ButtonState::Pressed;
            break;
        case SplitButtonState::SecondaryPointerOver:
            secondary = ButtonState::Hover;
            break;
        case SplitButtonState::SecondaryPressed:
            secondary = ButtonState::Pressed;
            break;
        case SplitButtonState::Normal:
            break;
    }
    // The primary part's own Disabled state, while the command is disabled.
    if (!command_enabled_)
    {
        primary = ButtonState::Disabled;
    }
    const ButtonVisual primary_visual   = part_visual(theme, primary);
    const ButtonVisual secondary_visual = part_visual(theme, secondary);
    const float        radius           = theme.resolved_button_corner_radius();

    // Each part is the whole rounded button clipped to its own columns, so the primary part keeps
    // its left corners and three outer borders (CornerRadius 4,0,0,4), the secondary its right ones.
    const Rect p = primary_rect();
    const Rect s = secondary_rect();
    renderer.push_clip(Rect{p.x, bounds().y, p.width + Divider, bounds().height});
    draw_control_chrome(renderer, bounds(), radius, primary_visual, theme.border_width);
    renderer.pop_clip();
    renderer.push_clip(s);
    draw_control_chrome(renderer, bounds(), radius, secondary_visual, theme.border_width);
    renderer.pop_clip();

    // The divider takes the default control stroke color, or the tertiary on-accent stroke color
    // when checked. Outside Fluent there are no such tokens, so the line takes the secondary part's
    // text color, as high contrast draws it.
    const bool  checked_look = checked() && state != SplitButtonState::Disabled;
    const Color divider      = !detail::has_fluent_palette(theme) ? secondary_visual.text
                               : checked_look                     ? theme.palette.control_stroke_on_accent_tertiary
                                                                  : theme.palette.control_stroke_default;
    renderer.fill_rounded_rect(Rect{p.x + p.width, bounds().y, Divider, bounds().height}, 0.0f, divider);

    if (content_)
    {
        content_->render(renderer, theme.with_content_colors(primary_visual.text));
    }
    else
    {
        TextStyle label_style = theme.text_style;
        label_style.effect    = primary_visual.text_effect;
        const Point offset    = primary == ButtonState::Pressed ? detail::controls(theme).pressed_content_offset
                                                                : Point{0.0f, 0.0f};
        renderer.draw_text(text_, Rect{p.x + offset.x, p.y + offset.y, p.width, p.height}, primary_visual.text, label_style);
    }

    // The secondary text color at rest, the button's text color on hover, the tertiary when
    // pressed, and the accent text when checked.
    Color chevron = theme.palette.text_fill_secondary;
    if (checked_look || secondary == ButtonState::Disabled || !detail::has_fluent_palette(theme))
    {
        chevron = secondary_visual.text; // and outside Fluent, where the grays are not filled in
    }
    else if (secondary == ButtonState::Hover)
    {
        chevron = secondary_visual.text;
    }
    else if (secondary == ButtonState::Pressed)
    {
        chevron = theme.palette.text_fill_tertiary;
    }
    if (theme.disclosure_visual == ThemeDisclosureVisual::ClassicArrow)
    {
        // The combo box's solid arrow in the button's text color, moving with the press.
        const Rect& at    = chevron_->bounds();
        const Point shift = secondary == ButtonState::Pressed ? detail::controls(theme).pressed_content_offset
                                                              : Point{0.0f, 0.0f};
        draw_classic_arrow(renderer,
                           Rect{at.x + shift.x, at.y + shift.y, at.width, at.height},
                           ArrowDirection::Down,
                           secondary_visual.text);
    }
    else
    {
        chevron_->render_in(renderer, theme, chevron);
    }
}

bool SplitButtonControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }
    const bool in_primary   = primary_rect().contains(e.position);
    const bool in_secondary = secondary_rect().contains(e.position);
    switch (e.action)
    {
        case PointerAction::Move:
            primary_hovered_   = in_primary && command_enabled_;
            secondary_hovered_ = in_secondary;
            break;

        case PointerAction::Down:
            if (e.button != PointerButton::Left)
            {
                break;
            }
            // A press anywhere on the button while its flyout is open is the flyout's light
            // dismiss: the flyout closes and the press goes no further.
            if ((in_primary || in_secondary) && is_flyout_open())
            {
                close_flyout();
                return true;
            }
            primary_down_      = in_primary && command_enabled_;
            secondary_down_    = in_secondary;
            primary_hovered_   = primary_down_;
            secondary_hovered_ = secondary_down_;
            sync_chevron();
            mark_dirty(DirtyFlags::Visual);
            return in_primary || in_secondary;

        case PointerAction::Up:
        {
            if (e.button != PointerButton::Left)
            {
                break;
            }
            const bool held            = primary_down_ || secondary_down_;
            const bool click_primary   = primary_down_ && in_primary && command_enabled_;
            const bool click_secondary = secondary_down_ && in_secondary;
            primary_down_ = secondary_down_ = false;
            primary_hovered_                = in_primary && command_enabled_;
            secondary_hovered_              = in_secondary;
            sync_chevron();
            mark_dirty(DirtyFlags::Visual);
            // Last: a click handler may tear the page down.
            if (click_secondary)
            {
                open_flyout();
            }
            else if (click_primary)
            {
                on_click_primary();
            }
            return held;
        }

        case PointerAction::Leave:
            primary_hovered_ = secondary_hovered_ = false;
            break;

        default:
            break;
    }
    sync_chevron();
    mark_dirty(DirtyFlags::Visual);
    return false;
}

bool SplitButtonControl::key_override(const KeyEvent& e)
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
    // Space and Enter press the primary part on key down and click it on key up.
    if (e.key == Key::Space || e.key == Key::Enter)
    {
        if (e.action == KeyAction::Down)
        {
            key_down_ = true;
            sync_chevron();
            mark_dirty(DirtyFlags::Visual);
            return handled;
        }
        if (!key_down_)
        {
            return handled;
        }
        key_down_ = false;
        sync_chevron();
        mark_dirty(DirtyFlags::Visual);
        on_click_primary(); // on_click, then the command (which checks whether it can run itself)
        return handled;
    }
    if (e.action == KeyAction::Up && ((e.key == Key::ArrowDown && e.modifiers.alt) || e.key == Key::F4))
    {
        open_flyout();
    }
    return handled;
}

void SplitButtonControl::focus_changed(bool /*focused*/)
{
    if (!is_focused())
    {
        key_down_ = false;
    }
    sync_chevron();
    mark_dirty(DirtyFlags::Visual);
}

void SplitButtonControl::tick_override(Duration delta)
{
    if (content_)
    {
        content_->tick(delta);
    }
    sync_chevron(); // the flyout opens and closes without telling the button
    chevron_->tick(delta);
}

bool SplitButtonControl::invoke()
{
    // An accessibility Invoke clicks the primary part.
    if (!primary_enabled())
    {
        return false;
    }
    on_click_primary();
    return true;
}

bool SplitButtonControl::set_expanded_state(bool expanded)
{
    if (!is_enabled() || flyout_ == nullptr)
    {
        return false;
    }
    if (expanded)
    {
        open_flyout();
    }
    else
    {
        close_flyout();
    }
    return true;
}

void SplitButtonControl::on_click_primary()
{
    // Copied out first: the click handler may tear the page down.
    const Command* const command   = command_;
    const std::string    parameter = command_parameter_;
    if (on_click_)
    {
        on_click_();
    }
    if (command != nullptr)
    {
        (*command)(parameter); // checks whether it can run, then runs
    }
}

void SplitButtonControl::sync_chevron()
{
    // The secondary button's own PointerOver/Pressed states drive AnimatedIcon.State.
    const char* state = "Normal";
    if (is_enabled() && (is_flyout_open() || key_down_ || (secondary_down_ && secondary_hovered_)))
    {
        state = "Pressed";
    }
    else if (is_enabled() && secondary_hovered_)
    {
        state = "PointerOver";
    }
    chevron_->set_state(state);
}

// ---- ToggleSplitButton ----

void ToggleSplitButtonControl::set_checked(bool checked)
{
    if (checked_ == checked)
    {
        return;
    }
    checked_ = checked;
    mark_dirty(DirtyFlags::Visual);
    if (on_checked_changed_)
    {
        on_checked_changed_(checked_);
    }
}

void ToggleSplitButtonControl::on_click_primary()
{
    set_checked(!checked_); // Toggle(), then the base's Click
    SplitButtonControl::on_click_primary();
}

bool ToggleSplitButtonControl::toggle_next()
{
    if (!is_enabled())
    {
        return false;
    }
    set_checked(!checked_);
    return true;
}

namespace
{
template <typename S>
void apply_split_button(S& spec, ui::SplitButtonControl& node)
{
    apply(spec.text, node, [raw = &node](const std::string& value) { raw->set_text(value); });
    apply(spec.enabled, node, [raw = &node](const bool& value) { raw->set_enabled(value); });
    if (spec.command)
    {
        node.set_command(spec.command, std::move(spec.command_parameter));
    }
    if (spec.on_click)
    {
        node.on_click(std::move(spec.on_click));
    }
    if (spec.padding.reactive() || spec.padding.initial())
    {
        apply(spec.padding, node, [raw = &node](const std::optional<ui::Thickness>& v) { raw->set_padding(v); });
    }
    if (spec.content)
    {
        node.set_content(spec.content.take());
    }
}
} // namespace

std::unique_ptr<ui::Element> SplitButton::build() &&
{
    auto node = std::make_unique<ui::SplitButtonControl>();
    apply_split_button(*this, *node);

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

std::unique_ptr<ui::Element> ToggleSplitButton::build() &&
{
    auto node = std::make_unique<ui::ToggleSplitButtonControl>();
    // The value first, the handler after it, so seeding `checked` raises nothing.
    apply(checked,
          *node,
          [raw = node.get()](const std::optional<bool>& value) { raw->set_checked(value.value_or(false)); });
    apply_split_button(*this, *node);
    if (auto changed = ui::write_back(checked, std::move(on_checked_changed)))
    {
        node->on_checked_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
