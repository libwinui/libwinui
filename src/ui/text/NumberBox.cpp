// SPDX-License-Identifier: MIT
#include "winui/ui/text/NumberBox.hpp"

#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/NumberFormat.hpp"
#include "winui/render/Bevel.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/ContextMenuHost.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <cmath>

namespace ui
{

namespace
{
constexpr double RepeatInitialDelayMs      = 500.0;
constexpr double NumberBoxRepeatIntervalMs = 100.0; // every repeat after the first, while held

// A spin button's minimum width.
constexpr float SpinButtonWidth = 32.0f;

// The box's minimum width with Inline spin buttons. With any other placement the text field
// keeps TextBox's own minimum width.
constexpr float InlineMinWidth  = 120.0f;
constexpr float TextBoxMinWidth = 64.0f;

// Compact's popup indicator, popup offsets, padding, border and buttons.
constexpr char32_t PopupIndicatorGlyph   = 0xEC8F;
constexpr float    PopupIndicatorSize    = 12.0f;
constexpr float    PopupIndicatorMargin  = 8.0f;
constexpr float    PopupHorizontalOffset = -21.0f;
constexpr float    PopupVerticalOffset   = -27.0f;
constexpr float    PopupPadding          = 6.0f;
constexpr float    PopupBorder           = 1.0f;
constexpr float    PopupButtonSize       = 36.0f;
constexpr float    PopupButtonGap        = 4.0f;
constexpr float    PopupGlyphSize        = 16.0f;

// The displayed value is rounded to ten significant digits, so floating-point noise never shows.
constexpr int DisplayDigits = 10;

bool blank(const std::string& text)
{
    return text.find_first_not_of(" \t") == std::string::npos;
}

// A spin button's mark: the icon font's chevron, or in the classic packs the scroll arrow
// DrawFrameControl puts in an up-down control's halves, Marlett at the button's height less 2,
// moving with the press like a button label.
void draw_spin_glyph(Renderer& renderer, const Theme& theme, const Rect& r, ButtonState state, bool up, IconGlyph glyph, const Color& color)
{
    if (theme.disclosure_visual == ThemeDisclosureVisual::ClassicArrow)
    {
        const Point shift = state == ButtonState::Pressed ? detail::controls(theme).pressed_content_offset : Point{};
        const float depth = std::clamp(std::round((std::min(r.width, r.height) - 2.0f) * 4.0f / 13.0f), 2.0f, 4.0f);
        draw_classic_arrow(renderer,
                           Rect{r.x + shift.x, r.y + shift.y, r.width, r.height},
                           up ? ArrowDirection::Up : ArrowDirection::Down,
                           color,
                           depth);
        return;
    }
    draw_icon(renderer, glyph, r, color, 0.0f, theme.icon_font_family);
}
} // namespace

// ---- Compact's popup ----

// The content of Compact's up/down popup. It holds no state: presses go through the owner, so holding
// one repeats like an inline button.
class NumberBoxSpinPopup final : public Element
{
public:
    explicit NumberBoxSpinPopup(NumberBoxControl& owner) : owner_(owner)
    {
    }

    [[nodiscard]] Rect up_rect() const
    {
        const float inset = PopupBorder + PopupPadding;
        return Rect{bounds().x + inset, bounds().y + inset, PopupButtonSize, PopupButtonSize};
    }
    [[nodiscard]] Rect down_rect() const
    {
        const Rect up = up_rect();
        return Rect{up.x, up.bottom() + PopupButtonGap, PopupButtonSize, PopupButtonSize};
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        const float inset = 2.0f * (PopupBorder + PopupPadding);
        return {PopupButtonSize + inset, 2.0f * PopupButtonSize + PopupButtonGap + inset};
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme  = context.theme;
        const Color  stroke = theme.surfaces.surface_stroke.value_or(
            theme.surfaces.menu_border.value_or(theme.surfaces.field_border));
        fill_overlay_surface(renderer, theme, bounds(), theme.overlay_corner_radius, theme.colors().overlay);
        stroke_rounded_rect_inside(renderer, bounds(), theme.overlay_corner_radius, stroke, PopupBorder);
        draw_button(renderer, theme, up_rect(), owner_.popup_state(true), true);
        draw_button(renderer, theme, down_rect(), owner_.popup_state(false), false);
    }

    bool pointer_override(const PointerEvent& e) override
    {
        owner_.popup_pointer(e, up_rect(), down_rect());
        return false;
    }


private:
    // The popup's buttons use the subtle ramp, the same as the text field's inner buttons.
    static void draw_button(Renderer& renderer, const Theme& theme, const Rect& r, ButtonState state, bool up)
    {
        const ButtonVisual& v = theme.subtle_for_state(state);
        renderer.fill_rounded_rect(r, theme.corner_radius, v.background);
        IconGlyph glyph = up ? glyphs::ChevronUp : glyphs::ChevronDown;
        glyph.size      = PopupGlyphSize;
        draw_spin_glyph(renderer, theme, r, state, up, glyph, v.text);
    }

    NumberBoxControl& owner_;
};

NumberBoxControl::NumberBoxControl(std::optional<double> value, double min, double max) :
    text_box_(std::make_unique<TextBoxControl>()),
    min_(min),
    max_(std::max(min, max))
{
    set_parent_of(*text_box_, this);
    // NumberBox's own minimum covers the composite, so the inner TextBox drops its theme minimum.
    text_box_->set_min_width(0.0f);
    text_box_->set_uses_theme_min_height(false);
    if (value && !std::isnan(*value))
    {
        value_ = std::clamp(*value, min_, max_);
    }
    sync_text_from_value();
    // The default formatter is the user's regional one,
    // which the host learns after the tree is built: show the value in it once it is known.
    add_subscription(regional_number_format_changed().observe(
        [this]
        {
            if (!formatter_.symbols)
            {
                sync_text_from_value();
            }
        }));
}

NumberBoxControl::~NumberBoxControl() = default;

void NumberBoxControl::set_spin_button_placement(NumberBoxSpinButtonPlacementMode placement)
{
    if (placement_ == placement)
    {
        return;
    }
    placement_  = placement;
    hovered_up_ = pressed_up_ = hovered_down_ = pressed_down_ = false;
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    sync_popup();
}

float NumberBoxControl::spinner_width() const
{
    return inline_spinners() && !stacked_spinners() ? 2.0f * SpinButtonWidth : 0.0f;
}

float NumberBoxControl::indicator_width() const
{
    return compact_popup() ? PopupIndicatorSize + PopupIndicatorMargin : 0.0f;
}

void NumberBoxControl::set_range(double min, double max)
{
    min_ = min;
    max_ = std::max(min, max);
    mark_dirty(DirtyFlags::Visual);
    set_value(value_); // reclamp; syncs text and fires on_value_changed_ if that moves it
}

void NumberBoxControl::set_number_formatter(DecimalFormatter formatter)
{
    formatter_ = std::move(formatter);
    sync_text_from_value();
    mark_dirty(DirtyFlags::Visual);
}

void NumberBoxControl::set_value(std::optional<double> value)
{
    // Only a number is clamped, and only while invalid input is overwritten. A NaN
    // (0.0 / 0.0 from the app) is no number.
    if (value && std::isnan(*value))
    {
        value.reset();
    }
    if (value && validation_mode_ != NumberBoxValidationMode::Disabled)
    {
        value = std::clamp(*value, min_, max_);
    }
    const bool changed = value != value_;
    value_             = value;
    sync_text_from_value();
    mark_dirty(DirtyFlags::Visual);
    if (changed && on_value_changed_)
    {
        on_value_changed_(value_);
    }
}

const std::string& NumberBoxControl::text() const
{
    return text_box_->text();
}

void NumberBoxControl::set_text(std::string text)
{
    text_box_->set_text(std::move(text));
}

void NumberBoxControl::on_text_changed(Callback<void(const std::string&)> handler)
{
    text_box_->on_text_changed(std::move(handler));
}

void NumberBoxControl::enabled_changed()
{
    text_box_->set_enabled(is_enabled());
    if (!is_enabled())
    {
        hovered_up_ = pressed_up_ = hovered_down_ = pressed_down_ = false;
    }
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

void NumberBoxControl::focus_changed(bool /*focused*/)
{
    set_focused_of(*text_box_, is_focused());
    if (!is_focused())
    {
        // The text is validated when focus is lost.
        commit_text();
    }
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

void NumberBoxControl::sync_text_from_value()
{
    // No value is an empty box.
    text_box_->set_text(value_ ? formatter_.format(round_significant(*value_, DisplayDigits)) : std::string{});
}

void NumberBoxControl::commit_text()
{
    // Parse the text and validate it.
    const std::string& text = text_box_->text();
    if (blank(text))
    {
        set_value(std::nullopt);
        sync_text_from_value();
        return;
    }
    const std::optional<double> parsed = accepts_expression_ ? evaluate_expression(text, formatter_) : formatter_.parse(text);
    if (!parsed)
    {
        if (validation_mode_ == NumberBoxValidationMode::InvalidInputOverwritten)
        {
            sync_text_from_value();
        }
        return;
    }
    if (*parsed == value_)
    {
        sync_text_from_value(); // "1 + 2" becomes "3" even when the value already was 3
        return;
    }
    set_value(*parsed);
}

void NumberBoxControl::step_value(double delta)
{
    commit_text(); // start from whatever's currently on screen, not the last committed value
    if (!value_)
    {
        return;
    }
    double next = *value_ + delta;
    if (wrap_enabled_)
    {
        if (next > max_)
        {
            next = min_;
        }
        else if (next < min_)
        {
            next = max_;
        }
    }
    set_value(next);
}

Size NumberBoxControl::measure_override(Size available, const MeasureContext& context) const
{
    const Size header      = header_.measure(available.width, context);
    available              = ControlHeader::below(available, header);
    const Size description = description_.measure(available.width, context);
    available              = ControlHeader::below(available, description);

    const float line_height = context.measurer.measure("", context.theme.text_style).height;
    header_height_          = line_height + context.theme.padding.vertical();
    // Stacked spin buttons sit inside the text box's border, where its text never runs.
    stacked_width_       = detail::controls(context.theme).stacked_spinner_width.value_or(0.0f);
    spin_popup_          = detail::controls(context.theme).spin_button_popup;
    stacked_border_      = context.theme.border_width;
    const float trailing = stacked_spinners() ? stacked_width_ : 0.0f;
    if (text_box_->trailing_space() != trailing)
    {
        text_box_->set_trailing_space(trailing);
    }
    const float reserved = spinner_width() + indicator_width() + trailing;

    const Size text_available{
        std::max(0.0f, available.width - reserved),
        header_height_,
    };
    Size text_desired = text_box_->measure(text_available, context);
    if (!std::isfinite(available.width))
    {
        // Offered no width, a TextBox asks for a line's worth of room; the text field here asks
        // for its text and padding, and the minimum below does the rest.
        text_desired.width = context.measurer.measure(text_box_->text(), context.theme.text_style).width +
                             context.theme.padding.horizontal();
    }

    Size desired{std::max(text_desired.width + reserved, spinner_width() > 0.0f ? InlineMinWidth : TextBoxMinWidth),
                 header_height_};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return ControlHeader::above(ControlHeader::above(desired, header), description);
}

void NumberBoxControl::arrange_override(const Rect&, const ArrangeContext&)
{
    header_.arrange(*this);
    description_.arrange(*this);
    const Rect& final_rect = bounds(); // the control itself, below its header
    // Compact's indicator sits inside the text box's own border, so the box keeps the full
    // width and only the Inline spin columns take room from it.
    const Rect text_rect{final_rect.left(),
                         final_rect.top(),
                         std::max(0.0f, final_rect.width - spinner_width()),
                         final_rect.height};
    text_box_->arrange(text_rect);
    sync_popup(); // the theme decides in measure() whether Compact uses a popup
}

Rect NumberBoxControl::spinner_bounds() const
{
    return Rect{bounds().right() - spinner_width(), bounds().top(), spinner_width(), bounds().height};
}

Rect NumberBoxControl::up_bounds() const
{
    if (stacked_spinners())
    {
        // The upper half of the column inside the field's border.
        const float border = stacked_border_;
        const float height = std::max(0.0f, bounds().height - 2.0f * border);
        return Rect{bounds().right() - border - stacked_width_,
                    bounds().top() + border,
                    stacked_width_,
                    std::floor(height * 0.5f)};
    }
    return Rect{bounds().right() - 2.0f * SpinButtonWidth, bounds().top(), SpinButtonWidth, bounds().height};
}

Rect NumberBoxControl::down_bounds() const
{
    if (stacked_spinners())
    {
        const Rect  up     = up_bounds();
        const float bottom = bounds().bottom() - stacked_border_;
        return Rect{up.x, up.bottom(), up.width, std::max(0.0f, bottom - up.bottom())};
    }
    return Rect{bounds().right() - SpinButtonWidth, bounds().top(), SpinButtonWidth, bounds().height};
}

ButtonState NumberBoxControl::up_visual_state() const
{
    if (!can_step_up())
    {
        return ButtonState::Disabled;
    }
    if (pressed_up_ && hovered_up_)
    {
        return ButtonState::Pressed;
    }
    if (hovered_up_)
    {
        return ButtonState::Hover;
    }
    return ButtonState::Normal;
}

ButtonState NumberBoxControl::down_visual_state() const
{
    if (!can_step_down())
    {
        return ButtonState::Disabled;
    }
    if (pressed_down_ && hovered_down_)
    {
        return ButtonState::Pressed;
    }
    if (hovered_down_)
    {
        return ButtonState::Hover;
    }
    return ButtonState::Normal;
}

bool NumberBoxControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }

    // Only Inline has spin buttons in the box itself.
    const Rect up   = inline_spinners() ? up_bounds() : Rect{};
    const Rect down = inline_spinners() ? down_bounds() : Rect{};

    switch (e.action)
    {
        case PointerAction::Move:
            hovered_up_   = up.contains(e.position);
            hovered_down_ = down.contains(e.position);
            break;

        case PointerAction::Down:
            if (e.button == PointerButton::Left)
            {
                if (up.contains(e.position) && can_step_up())
                {
                    pressed_up_           = true;
                    hovered_up_           = true;
                    repeat_elapsed_ms_    = 0.0;
                    repeat_first_pending_ = true;
                    step_value(step_);
                }
                else if (down.contains(e.position) && can_step_down())
                {
                    pressed_down_         = true;
                    hovered_down_         = true;
                    repeat_elapsed_ms_    = 0.0;
                    repeat_first_pending_ = true;
                    step_value(-step_);
                }
            }
            break;

        case PointerAction::Up:
            if (e.button == PointerButton::Left)
            {
                pressed_up_   = false;
                pressed_down_ = false;
                hovered_up_   = up.contains(e.position);
                hovered_down_ = down.contains(e.position);
            }
            break;

        case PointerAction::Leave:
            hovered_up_ = hovered_down_ = pressed_up_ = pressed_down_ = false;
            break;

        case PointerAction::Wheel:
            break;
    }

    return (e.action == PointerAction::Down || e.action == PointerAction::Up) &&
           (up.contains(e.position) || down.contains(e.position));
}

bool NumberBoxControl::covers_children_at(Point p) const
{
    // The inline spin buttons sit inside the text box.
    return inline_spinners() && (up_bounds().contains(p) || down_bounds().contains(p));
}

bool NumberBoxControl::key_override(const KeyEvent& e)
{
    const bool handled = key_handled(e, takes_arrow_key(e.key), takes_enter_key(), takes_shortcut(e));
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || !is_focused())
    {
        return handled;
    }

    switch (e.key)
    {
        case Key::ArrowUp:
            if (e.action == KeyAction::Down && can_step_up())
            {
                step_value(step_);
            }
            return handled;

        case Key::ArrowDown:
            if (e.action == KeyAction::Down && can_step_down())
            {
                step_value(-step_);
            }
            return handled;

        case Key::PageUp:
            if (e.action == KeyAction::Down && can_step_up())
            {
                step_value(large_step_);
            }
            return handled;

        case Key::PageDown:
            if (e.action == KeyAction::Down && can_step_down())
            {
                step_value(-large_step_);
            }
            return handled;

        case Key::Enter:
            if (e.action == KeyAction::Down)
            {
                commit_text();
            }
            return handled;

        default:
            break;
    }

    text_box_->handle_key(e);
    return handled;
}

bool NumberBoxControl::takes_shortcut(const KeyEvent& e) const
{
    return text_box_ && text_box_->takes_shortcut(e);
}

bool NumberBoxControl::text_override(const TextInputEvent& text_event)
{
    const std::string_view text = text_event.text;
    if (!is_enabled() || !is_focused())
    {
        return true;
    }
    text_box_->handle_text(text);
    return true;
}

void NumberBoxControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    header_.render(renderer, theme, is_enabled());
    description_.render(renderer, theme, is_enabled());

    text_box_->render(renderer, context);

    if (compact_popup())
    {
        // The popup indicator: the secondary text color, which eras without a palette approximate
        // with the text color at the same 60% the rest of this codebase uses for it.
        const Color color = detail::controls(theme).scroll_button_chromeless ? theme.palette.text_fill_secondary
                                                                             : theme.colors().text.with_alpha(0.6f);
        const Rect  glyph{bounds().right() - PopupIndicatorMargin - PopupIndicatorSize,
                          bounds().top(),
                          PopupIndicatorSize,
                          bounds().height};
        draw_icon(renderer,
                  IconGlyph{PopupIndicatorGlyph, "Segoe Fluent Icons", PopupIndicatorSize},
                  glyph,
                  is_enabled() ? color : theme.colors().text_disabled,
                  0.0f,
                  theme.icon_font_family);
    }
    if (!inline_spinners())
    {
        return;
    }

    if (stacked_spinners())
    {
        // Two small button faces, each with the era's 7 x 4 triangle.
        const auto half = [&](const Rect& r, ButtonState state, bool up)
        {
            const ButtonVisual& v = theme.for_state(state);
            draw_control_chrome(renderer, r, 1.0f, v, theme.border_width, true, false);
            const float x = std::round(r.x + (r.width - 7.0f) * 0.5f);
            const float y = std::round(r.y + (r.height - 4.0f) * 0.5f);
            std::vector<Point> points = up ? std::vector<Point>{Point{x, y + 4.0f}, Point{x + 3.5f, y}, Point{x + 7.0f, y + 4.0f}}
                                           : std::vector<Point>{Point{x, y}, Point{x + 7.0f, y}, Point{x + 3.5f, y + 4.0f}};
            renderer.fill_polygon(FillPolygon{std::move(points), v.text});
        };
        half(up_bounds(), up_visual_state(), true);
        half(down_bounds(), down_visual_state(), false);
        return;
    }

    // Drawn as a push button, so each era's gloss, gradient or bevel shows on the spin buttons.
    const ButtonVisual& up_visual = theme.for_state(up_visual_state());
    const Rect          up        = up_bounds();
    draw_control_chrome(renderer, up, theme.corner_radius, up_visual, theme.border_width, true, false);
    draw_spin_glyph(renderer, theme, up, up_visual_state(), true, glyphs::ChevronUp, up_visual.text);

    const ButtonVisual& down_visual = theme.for_state(down_visual_state());
    const Rect          down        = down_bounds();
    draw_control_chrome(renderer, down, theme.corner_radius, down_visual, theme.border_width, true, false);
    draw_spin_glyph(renderer, theme, down, down_visual_state(), false, glyphs::ChevronDown, down_visual.text);
}

bool NumberBoxControl::wheel_override(const PointerEvent& e)
{
    if (!is_enabled() || !is_focused() || !bounds().contains(e.position) || e.horizontal_wheel || e.wheel_delta == 0.0f)
    {
        return false;
    }
    // A turn away from the user (a negative delta here) steps up.
    if (e.wheel_delta < 0.0f)
    {
        if (can_step_up())
        {
            step_value(step_);
        }
    }
    else if (can_step_down())
    {
        step_value(-step_);
    }
    return true;
}

void NumberBoxControl::sync_popup()
{
    if (!compact_popup() || !is_focused() || !is_enabled())
    {
        close_popup();
        return;
    }
    if (!popup_)
    {
        popup_ = std::make_unique<NumberBoxSpinPopup>(*this);
        set_parent_of(*popup_, this);
    }
    open_popup({
        .content       = popup_.get(),
        .light_dismiss = false, // a plain Popup: it stays open until focus leaves the box
        .place =
            [this](Element& content, Size, const MeasureContext& context)
        {
            // The popup sits in the spin column, which is the box's right edge while the inline
            // buttons are collapsed, moved by the popup's horizontal and vertical offsets.
            const Size size = content.measure({Unbounded, Unbounded}, context);
            content.arrange(
                Rect{bounds().right() + PopupHorizontalOffset, bounds().top() + PopupVerticalOffset, size.width, size.height});
            return true;
        },
    });
}

ButtonState NumberBoxControl::popup_state(bool up) const
{
    const bool can     = up ? can_step_up() : can_step_down();
    const bool hovered = up ? popup_hovered_up_ : popup_hovered_down_;
    const bool pressed = up ? pressed_up_ : pressed_down_;
    if (!can)
    {
        return ButtonState::Disabled;
    }
    if (pressed && hovered)
    {
        return ButtonState::Pressed;
    }
    return hovered ? ButtonState::Hover : ButtonState::Normal;
}

void NumberBoxControl::popup_pointer(const PointerEvent& e, const Rect& up, const Rect& down)
{
    if (!is_enabled())
    {
        return;
    }
    switch (e.action)
    {
        case PointerAction::Move:
            popup_hovered_up_   = up.contains(e.position);
            popup_hovered_down_ = down.contains(e.position);
            break;
        case PointerAction::Down:
            if (e.button != PointerButton::Left)
            {
                break;
            }
            if (up.contains(e.position) && can_step_up())
            {
                pressed_up_           = true;
                repeat_elapsed_ms_    = 0.0;
                repeat_first_pending_ = true;
                step_value(step_);
            }
            else if (down.contains(e.position) && can_step_down())
            {
                pressed_down_         = true;
                repeat_elapsed_ms_    = 0.0;
                repeat_first_pending_ = true;
                step_value(-step_);
            }
            break;
        case PointerAction::Up:
            pressed_up_ = pressed_down_ = false;
            break;
        case PointerAction::Leave:
            popup_hovered_up_ = popup_hovered_down_ = false;
            break;
        case PointerAction::Wheel:
            break;
    }
    mark_dirty(DirtyFlags::Visual);
}

void NumberBoxControl::tick_override(Duration delta)
{

    if (!pressed_up_ && !pressed_down_)
    {
        repeat_elapsed_ms_ = 0.0;
        return;
    }

    repeat_elapsed_ms_ += delta.count();
    const double threshold = repeat_first_pending_ ? RepeatInitialDelayMs : NumberBoxRepeatIntervalMs;
    if (repeat_elapsed_ms_ < threshold)
    {
        return;
    }
    repeat_elapsed_ms_    = 0.0;
    repeat_first_pending_ = false;

    if (pressed_up_ && can_step_up())
    {
        step_value(step_);
    }
    else if (pressed_down_ && can_step_down())
    {
        step_value(-step_);
    }
}

bool NumberBoxControl::handle_context_menu(std::optional<Point> position, ContextMenuHost& host)
{
    return text_box_->handle_context_menu(position, host);
}

std::unique_ptr<ui::Element> NumberBox::build() &&
{
    // The range goes to the constructor because set_range() re-clamps the current value; this way
    // the value below is clamped once, into the range the caller asked for.
    auto node = std::make_unique<ui::NumberBoxControl>(value.initial(), min.initial(), max.initial());
    apply(header, *node, [raw = node.get()](const std::string& v) { raw->set_header(v); });
    apply(description, *node, [raw = node.get()](const std::string& value) { raw->set_description(value); });
    apply(error, *node, [raw = node.get()](const std::string& value) { raw->set_error(value); });
    apply(step, *node, [raw = node.get()](const double& v) { raw->set_step(v); });
    apply(large_step, *node, [raw = node.get()](const double& v) { raw->set_large_step(v); });
    apply(spin_button_placement,
          *node,
          [raw = node.get()](const NumberBoxSpinButtonPlacementMode& v) { raw->set_spin_button_placement(v); });
    apply(accepts_expression, *node, [raw = node.get()](const bool& v) { raw->set_accepts_expression(v); });
    apply(validation_mode, *node, [raw = node.get()](const NumberBoxValidationMode& v) { raw->set_validation_mode(v); });
    apply(wrap_enabled, *node, [raw = node.get()](const bool& v) { raw->set_wrap_enabled(v); });
    if (number_formatter.reactive() || number_formatter.initial())
    {
        apply(number_formatter,
              *node,
              [raw = node.get()](const std::optional<DecimalFormatter>& v)
              {
                  if (v)
                  {
                      raw->set_number_formatter(*v);
                  }
              });
    }
    apply(placeholder_text, *node, [raw = node.get()](const std::string& v) { raw->set_placeholder_text(v); });

    // A value the control clamped to the range goes back to a two-way property, so it ends up
    // equal to the control: at build, and also when the control already held the clamped value.
    const auto write = ui::shared_writer(value);
    apply(value,
          *node,
          [raw = node.get(), write](const std::optional<double>& v)
          {
              raw->set_value(v);
              const std::optional<double> shown = raw->value();
              if (write && shown != v)
              {
                  (*write)(shown);
              }
          });
    detail::apply_range(min, max, *node);
    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });

    if (auto changed = ui::write_back(write, std::move(on_value_changed)))
    {
        node->on_value_changed(std::move(changed));
    }
    if (on_text_changed)
    {
        node->on_text_changed(std::move(on_text_changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
