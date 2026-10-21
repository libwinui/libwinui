// SPDX-License-Identifier: MIT
#include "winui/ui/input/ToggleSwitch.hpp"

#include "../../render/Compositor.hpp"
#include "../FocusRingPlacement.hpp"
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
#include <optional>
#include <string>
#include <utility>

namespace ui
{

namespace
{
constexpr float ToggleSwitchHotOverlayOpacity = 0.16f;
constexpr float ToggleSwitchIconSize          = 16.0f; // the room a path or picture gets beside the switch

// `ButtonKnob`'s slide: 250 ms quintic ease-out, from the third-party Aero theme the knob comes
// from. A literal because tick_override() has no Theme to read.
constexpr double ButtonKnobSlideMs = 250.0;

[[nodiscard]] float ease_out_quint(double x)
{
    const double u = 1.0 - std::clamp(x, 0.0, 1.0);
    return static_cast<float>(1.0 - u * u * u * u * u);
}

// In Fluent the off knob takes the secondary text color. The retro, Metro and high contrast packs
// draw it in the track outline's color; `toggle_knob_off_foreground` picks.
[[nodiscard]] Color knob_fill(const Theme& theme, const ButtonVisual& v, bool on)
{
    return (on || detail::controls(theme).toggle_knob_off_foreground) ? v.text : v.border;
}
} // namespace

ToggleSwitchControl::ToggleSwitchControl() : ToggleSwitchControl(false)
{
}

ToggleSwitchControl::ToggleSwitchControl(bool on) : on_(on), composed_on_(on), slide_(on ? 1.0 : 0.0)
{
    // The control's minimum width.
    set_min_width(154.0f);
}

void ToggleSwitchControl::set_checked(bool on)
{
    if (on_ == on)
    {
        return;
    }
    on_ = on;
    mark_dirty(DirtyFlags::Visual);
    if (on_checked_changed_)
    {
        on_checked_changed_(on_);
    }
}

void ToggleSwitchControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_      = false;
        pointer_down_ = false;
        key_down_     = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

void ToggleSwitchControl::focus_changed(bool /*focused*/)
{
    if (!is_focused())
    {
        key_down_ = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

Rect ToggleSwitchControl::track_bounds(const Theme& theme) const
{
    const Size sz = detail::controls(theme).toggle_switch_track_size;
    return Rect{bounds().left(), bounds().top() + (bounds().height - sz.height) * 0.5f, sz.width, sz.height};
}

Rect ToggleSwitchControl::knob_bounds(const Theme& theme, bool on, float knob_d) const
{
    const Rect  track     = track_bounds(theme);
    const float rest_d    = detail::controls(theme).toggle_knob.rest;
    const float container = (track.height - rest_d) * 0.5f;
    const float center_x  = track.left() + container + rest_d * 0.5f +
                            (on ? detail::controls(theme).toggle_knob.travel : 0.0f);
    const float center_y  = track.top() + container + rest_d * 0.5f;
    return Rect{center_x - knob_d * 0.5f, center_y - knob_d * 0.5f, knob_d, knob_d};
}

Rect ToggleSwitchControl::knob_bounds_pressed(const Theme& theme, bool on) const
{
    const Rect  track           = track_bounds(theme);
    const float container_width = track.height;
    const float container_left  = track.left() + (on ? detail::controls(theme).toggle_knob.travel : 0.0f);
    const Size  sz              = detail::controls(theme).toggle_knob.pressed;
    const float left            = on ? container_left : container_left + (container_width - sz.width);
    const float top             = track.top() + (track.height - sz.height) * 0.5f;
    return Rect{left, top, sz.width, sz.height};
}

Rect ToggleSwitchControl::knob_shape_bounds(const Theme& theme, bool on, ButtonState state) const
{
    if (state == ButtonState::Pressed)
    {
        return knob_bounds_pressed(theme, on);
    }
    return knob_bounds(theme,
                       on,
                       state == ButtonState::Hover ? detail::controls(theme).toggle_knob.hover
                                                   : detail::controls(theme).toggle_knob.rest);
}

Rect ToggleSwitchControl::button_knob_bounds(const Theme& theme, float position) const
{
    const Rect  track  = track_bounds(theme);
    const Size  knob   = detail::controls(theme).toggle_button_knob_size;
    const float inset  = std::max(0.0f, (track.height - knob.height) * 0.5f);
    const float travel = std::max(0.0f, track.width - 2.0f * inset - knob.width);
    return Rect{track.left() + inset + travel * position, track.top() + inset, knob.width, knob.height};
}

float ToggleSwitchControl::button_knob_position() const
{
    if (!ticking_)
    {
        return on_ ? 1.0f : 0.0f;
    }
    // Eased toward whichever end it is heading for, so a knob turned around mid-slide
    // decelerates into the new end rather than the old one.
    return on_ ? ease_out_quint(slide_) : 1.0f - ease_out_quint(1.0 - slide_);
}

// The Aero knob slides here, through render(), and not as a compositor overlay: its fill is a
// gradient and a compositor overlay is one color.
void ToggleSwitchControl::tick_override(Duration delta)
{
    if (knob_tween_.advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
    const double target = on_ ? 1.0 : 0.0;
    if (!ticking_)
    {
        // Until now the knob was drawn at its resting end, whatever slide_ held; start from there.
        slide_   = target;
        ticking_ = true;
    }
    if (slide_ != target)
    {
        const double step = animation_delta(delta).count() / ButtonKnobSlideMs;
        slide_            = on_ ? std::min(1.0, slide_ + step) : std::max(0.0, slide_ - step);
        mark_dirty(DirtyFlags::Visual);
    }
}

Size ToggleSwitchControl::measure_override(Size available, const MeasureContext& context) const
{
    const Size header = header_.measure(available.width, context);
    available         = ControlHeader::below(available, header);

    Size desired = detail::controls(context.theme).toggle_switch_track_size;
    // Both labels, whichever is showing, so the width does not jump on a toggle.
    const Size on_label  = label_extent(true, context);
    const Size off_label = label_extent(false, context);
    const Size label{std::max(on_label.width, off_label.width), std::max(on_label.height, off_label.height)};
    if (label.width > 0.0f)
    {
        desired.width += detail::controls(context.theme).toggle_content_spacing + label.width;
        desired.height = std::max(desired.height, label.height);
    }
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return ControlHeader::above(desired, header);
}

Size ToggleSwitchControl::label_extent(bool on, const MeasureContext& context) const
{
    if (const IconSource& icon = on ? checked_icon_ : unchecked_icon_)
    {
        const float size = icon.extent(ToggleSwitchIconSize);
        return Size{size, size};
    }
    const std::string& text = on ? checked_text() : unchecked_text();
    return text.empty() ? Size{} : context.measurer.measure(text, context.theme.text_style);
}

void ToggleSwitchControl::arrange_override(const Rect&, const ArrangeContext&)
{
    header_.arrange(*this);
}

bool ToggleSwitchControl::is_pressed() const
{
    if (!is_enabled())
    {
        return false;
    }
    return (pointer_down_ && hovered_) || key_down_;
}

ButtonState ToggleSwitchControl::visual_state() const
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

bool ToggleSwitchControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }

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
                    toggle();
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

bool ToggleSwitchControl::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, false, false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
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
            toggle();
        }
    }
    return handled;
}

void ToggleSwitchControl::toggle()
{
    if (is_enabled())
    {
        set_checked(!on_);
    }
}

bool ToggleSwitchControl::hot() const
{
    return is_enabled() && (hovered_ || is_pressed());
}

void ToggleSwitchControl::sync_composition_override(detail::Compositor& compositor, const Theme& theme)
{
    const bool now_hot = hot();
    const Rect track   = track_bounds(theme);
    if (detail::controls(theme).toggle_switch_shape == ToggleSwitchShape::ButtonKnob)
    {
        // render() draws both the hover (it is the knob's and track's own hover visuals) and the
        // slide (tick_override()), so the wash stays empty.
        compositor.sync_visual(visual_id(), track, 0.0f, colors::Transparent);
        knob_tween_.reset();
        composed_on_  = on_;
        composed_hot_ = now_hot;
        return;
    }
    detail::TreeAccess::set_composes_visuals(*this);
    // Under a popup, a clip or a transform the compositor's wash would show over what hides the
    // control, so render_override() draws it instead.
    if (detail::TreeAccess::visuals_covered(*this))
    {
        compositor.remove_visual(visual_id());
    }
    else
    {
        compositor.sync_visual(visual_id(), track, track.height * 0.5f, colors::White);
    }
    if (!detail::TreeAccess::visuals_covered(*this) && now_hot != composed_hot_)
    {
        compositor.animate_opacity(visual_id(),
                                   composed_hot_ ? ToggleSwitchHotOverlayOpacity : 0.0f,
                                   now_hot ? ToggleSwitchHotOverlayOpacity : 0.0f,
                                   theme.anim_faster);
    }

    // render() draws the knob where the tween has it. One change can need a slide and a resize
    // together (releasing a click both un-presses and toggles); the slide's duration covers both.
    const ButtonVisual& v        = detail::controls(theme).toggle_track.for_state(visual_state(), on_);
    const bool          is_bevel = v.border_style == BorderStyle::Raised || v.border_style == BorderStyle::Sunken;
    const Rect          knob     = knob_shape_bounds(theme, on_, is_bevel ? ButtonState::Normal : visual_state());
    knob_tween_.retarget(Rect{knob.left() - track.left(), knob.top() - track.top(), knob.width, knob.height},
                         on_ != composed_on_ ? theme.anim_fast : theme.anim_faster,
                         theme.anim_fast_out_slow_in);

    composed_on_  = on_;
    composed_hot_ = now_hot && !detail::TreeAccess::visuals_covered(*this); // a removed wash shows nothing
}

void ToggleSwitchControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    header_.render(renderer, theme, is_enabled());

    const ButtonVisual& v     = detail::controls(theme).toggle_track.for_state(visual_state(), on_);
    const Rect          track = track_bounds(theme);
    // Windows had no ToggleSwitch before Fluent. The classic eras draw a latched toolbar-button
    // well instead, raised when off and sunken when on.
    const bool  classic      = detail::controls(theme).toggle_switch_shape == ToggleSwitchShape::ClassicWell;
    const float track_radius = classic ? 0.0f : track.height * detail::controls(theme).toggle_switch_track_radius_ratio;

    if (detail::controls(theme).toggle_switch_shape == ToggleSwitchShape::ButtonKnob)
    {
        // Aero: a text-field track and a push-button knob in the switch's own state, so the knob
        // lights blue on hover and shades in on press.
        draw_control_chrome(renderer, track, track_radius, v, theme.border_width, /*draw_flat_border_fallback=*/true);
        // Less the pressed inner shadow: its two bands are rectangles, and on a knob this small
        // they poke out past the rounded ends.
        ButtonVisual knob_visual = theme.for_state(visual_state());
        knob_visual.inner_shadow.reset();
        const Rect knob = button_knob_bounds(theme, button_knob_position());
        draw_control_chrome(renderer, knob, knob.height * 0.5f, knob_visual, theme.border_width);
    }
    else if (classic)
    {
        const BorderStyle style = on_ ? BorderStyle::Sunken : BorderStyle::Raised;
        renderer.fill_rounded_rect(track, track_radius, v.background);
        // The same two edge grammars render/Chrome.hpp dispatches between, and for the same
        // reason: Windows 3.x's is a different construction, not this one with stops removed.
        if (v.bevel_frame)
        {
            draw_win3x_border(renderer, track, style, *v.bevel_frame, v.bevel_highlight, v.bevel_shadow, theme.border_width);
        }
        else
        {
            // `bevel_rings` is honored, not defaulted to 2: Windows1x gives this well a single
            // COLOR_WINDOWFRAME ring, the era's whole edge vocabulary.
            draw_bevel_border(renderer,
                              track,
                              style,
                              v.bevel_light,
                              v.bevel_highlight,
                              v.bevel_shadow,
                              v.bevel_dark_shadow,
                              theme.border_width,
                              BevelSides::All,
                              v.bevel_soft,
                              v.bevel_rings);
        }
    }
    else
    {
        // The off track is always filled, with the recessed control fill. Hover shifts that
        // fill; a wash over an unfilled outline would not show against a light page.
        renderer.fill_rounded_rect(track, track_radius, v.background);
        stroke_rounded_rect_inside(renderer, track, track_radius, v.border, v.resolved_border_width(theme.border_width));
    }

    // Knob size and travel are their own metrics, not derived from the track height. The classic
    // well has no hover growth or press stretch.
    if (detail::controls(theme).toggle_switch_shape != ToggleSwitchShape::ButtonKnob)
    {
        const Rect target = classic ? knob_bounds(theme, on_, detail::controls(theme).toggle_knob.rest)
                                    : knob_shape_bounds(theme, on_, visual_state());
        const Rect eased  = knob_tween_.current(
            Rect{target.left() - track.left(), target.top() - track.top(), target.width, target.height});
        const Rect thumb{track.left() + eased.left(), track.top() + eased.top(), eased.width, eased.height};
        const float thumb_radius = classic ? 0.0f : thumb.height * detail::controls(theme).toggle_switch_track_radius_ratio;
        renderer.fill_rounded_rect(thumb, thumb_radius, knob_fill(theme, v, on_));
    }

    if (const IconSource& icon = on_ ? checked_icon_ : unchecked_icon_)
    {
        // Leading and vertically centered, where the text would start, in the text's color.
        const float size = icon.extent(ToggleSwitchIconSize);
        const Rect  icon_rect{track.right() + detail::controls(theme).toggle_content_spacing,
                              bounds().top() + (bounds().height - size) * 0.5f,
                              size,
                              size};
        icon.draw_in_slot(renderer,
                          icon_rect,
                          is_enabled() ? theme.colors().text : theme.colors().text_disabled,
                          size,
                          theme.icon_font_family);
    }
    else if (const std::string& text = current_text(); !text.empty())
    {
        const Rect label_rect{track.right() + detail::controls(theme).toggle_content_spacing,
                              bounds().top(),
                              std::max(0.0f, bounds().width - track.width - detail::controls(theme).toggle_content_spacing),
                              bounds().height};
        TextStyle label_style  = theme.text_style;
        label_style.horizontal = TextAlign::Leading;
        // Engraved in the classic eras (`Theme::disabled_text_effect`); the knob above is not.
        if (!is_enabled())
        {
            label_style.effect = theme.disabled_text_effect;
        }
        renderer.draw_text(text, label_rect, is_enabled() ? theme.colors().text : theme.colors().text_disabled, label_style);
    }
    if (detail::TreeAccess::visuals_covered(*this) && hot() &&
        detail::controls(theme).toggle_switch_shape != ToggleSwitchShape::ButtonKnob)
    {
        renderer.fill_rounded_rect(track, track.height * 0.5f, colors::White.with_alpha(ToggleSwitchHotOverlayOpacity));
    }
}

Rect ToggleSwitchControl::focus_ring_rect(const Theme& theme) const
{
    if (theme.focus_visual == FocusVisual::MetroDashedPair)
    {
        return track_bounds(theme);
    }
    return detail::toggle_content_ring(bounds(), desired_content_size(), theme);
}

float ToggleSwitchControl::focus_ring_corner_radius(const Theme& theme) const
{
    if (theme.focus_visual != FocusVisual::MetroDashedPair)
    {
        return theme.corner_radius;
    }
    const bool classic = detail::controls(theme).toggle_switch_shape == ToggleSwitchShape::ClassicWell;
    return classic ? 0.0f : track_bounds(theme).height * detail::controls(theme).toggle_switch_track_radius_ratio;
}

std::unique_ptr<ui::Element> ToggleSwitch::build() &&
{
    auto node = std::make_unique<ui::ToggleSwitchControl>();

    apply(header, *node, [raw = node.get()](const std::string& value) { raw->set_header(value); });
    apply(checked, *node, [raw = node.get()](const bool& value) { raw->set_checked(value); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });
    apply(checked_text,
          *node,
          [raw = node.get()](const std::optional<std::string>& value) { raw->set_checked_text(value); });
    apply(unchecked_text,
          *node,
          [raw = node.get()](const std::optional<std::string>& value) { raw->set_unchecked_text(value); });
    apply(checked_icon, *node, [raw = node.get()](const IconSource& v) { raw->set_checked_icon(v); });
    apply(unchecked_icon, *node, [raw = node.get()](const IconSource& v) { raw->set_unchecked_icon(v); });

    if (auto changed = ui::write_back(checked, std::move(on_checked_changed)))
    {
        node->on_checked_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
