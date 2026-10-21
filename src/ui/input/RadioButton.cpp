// SPDX-License-Identifier: MIT
#include "winui/ui/input/RadioButton.hpp"

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
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/RadioButtons.hpp"
#include "winui/ui/input/RadioGroup.hpp"
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
constexpr float RadioButtonHotOverlayOpacity = 0.16f;

// The classic eras' sunken well (Theme::classic()), which draw_classic_radio() draws whole.
bool is_classic_well(const ButtonVisual& v)
{
    return v.border_style == BorderStyle::Raised || v.border_style == BorderStyle::Sunken;
}
} // namespace

RadioButtonControl::GroupLink::~GroupLink()
{
    if (group != nullptr)
    {
        group->remove(member);
    }
}

RadioButtonControl::RadioButtonControl(std::string text, bool checked) : text_(std::move(text)), checked_(checked)
{
    // A minimum width of 120 and no minimum height.
    set_min_width(120.0f);
}

void RadioButtonControl::set_checked(bool checked)
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

void RadioButtonControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_      = false;
        pointer_down_ = false;
        key_down_     = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

void RadioButtonControl::focus_changed(bool /*focused*/)
{
    if (!is_focused())
    {
        key_down_ = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

Rect RadioButtonControl::dot_bounds(const Theme& theme) const
{
    const float box = detail::controls(theme).resolved_radio_box_size();
    return Rect{bounds().left(), bounds().top() + (bounds().height - box) * 0.5f, box, box};
}

Size RadioButtonControl::measure_override(Size available, const MeasureContext& context) const
{
    Size desired{detail::controls(context.theme).resolved_radio_box_size(),
                 detail::controls(context.theme).resolved_radio_box_size()};
    if (!text_.empty())
    {
        const Size text_size = context.measurer.measure(text_, context.theme.text_style);
        desired.width += detail::controls(context.theme).toggle_content_spacing + text_size.width;
        desired.height = std::max(desired.height, text_size.height);
    }
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

bool RadioButtonControl::is_pressed() const
{
    if (!is_enabled())
    {
        return false;
    }
    return (pointer_down_ && hovered_) || key_down_;
}

ButtonState RadioButtonControl::visual_state() const
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

bool RadioButtonControl::pointer_override(const PointerEvent& e)
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

bool RadioButtonControl::key_override(const KeyEvent& e)
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
            activate();
        }
    }
    return handled;
}

void RadioButtonControl::activate()
{
    if (!is_enabled())
    {
        return;
    }
    if (owner_)
    {
        owner_->select(this);
    }
    else if (group_.group)
    {
        group_.group->select(this);
    }
    else if (!checked_)
    {
        set_checked(true);
    }
}

bool RadioButtonControl::hot() const
{
    return is_enabled() && (hovered_ || is_pressed());
}

void RadioButtonControl::sync_composition_override(detail::Compositor& compositor, const Theme& theme)
{
    // The classic well has no hover state, and draws its dot itself, per device pixel.
    const bool classic = is_classic_well(detail::controls(theme).radio_ring.normal.off);
    const bool now_hot = hot() && !classic;
    const Rect ring    = dot_bounds(theme);
    detail::TreeAccess::set_composes_visuals(*this);
    // Under a popup, a clip or a transform the compositor's wash would show over what hides the
    // control, so render_override() draws it instead.
    if (detail::TreeAccess::visuals_covered(*this))
    {
        compositor.remove_visual(visual_id());
        composed_hot_ = false;
    }
    else
    {
        compositor.sync_visual(visual_id(), ring, ring.width * 0.5f, colors::White);
    }
    if (!detail::TreeAccess::visuals_covered(*this) && now_hot != composed_hot_)
    {
        compositor.animate_opacity(visual_id(),
                                   composed_hot_ ? RadioButtonHotOverlayOpacity : 0.0f,
                                   now_hot ? RadioButtonHotOverlayOpacity : 0.0f,
                                   theme.anim_faster);
        composed_hot_ = now_hot;
    }

    // render() draws the dot at the tween's size. A gradient dot (`ButtonVisual::mark_gradient`)
    // and the classic one never resize.
    const ButtonVisual& shown = detail::controls(theme).radio_ring.for_state(visual_state(), checked_);
    if (checked_ && !shown.mark_gradient && !classic)
    {
        const float size = dot_diameter(theme);
        dot_tween_.retarget(Rect{0.0f, 0.0f, size, size}, theme.anim_fast, theme.anim_fast_out_slow_in);
    }
    else
    {
        dot_tween_.reset();
    }
}

void RadioButtonControl::tick_override(Duration delta)
{
    if (dot_tween_.advance(delta))
    {
        mark_dirty(DirtyFlags::Visual);
    }
}

float RadioButtonControl::dot_diameter(const Theme& theme) const
{
    switch (visual_state())
    {
        case ButtonState::Hover:
            return detail::controls(theme).radio_dot.hover;
        case ButtonState::Pressed:
            return detail::controls(theme).radio_dot.pressed;
        case ButtonState::Normal:
        case ButtonState::Disabled:
            break;
    }
    return detail::controls(theme).radio_dot.rest;
}

void RadioButtonControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme&        theme       = context.theme;
    const ButtonVisual& v           = detail::controls(theme).radio_ring.for_state(visual_state(), checked_);
    const Rect          ring        = dot_bounds(theme);
    const float         ring_radius = ring.width * 0.5f; // square bounds + radius==w/2 == a circle
    // A classic radio is the sunken field border made round, drawn per device pixel with its dot.
    const bool is_bevel = is_classic_well(v);

    if (is_bevel)
    {
        draw_classic_radio(renderer,
                           ring,
                           v.bevel_light,
                           v.bevel_highlight,
                           v.bevel_shadow,
                           v.bevel_dark_shadow,
                           v.background,
                           checked_ ? std::optional<Color>{v.text} : std::nullopt);
    }
    else
    {
        // The ring is a filled disc under its stroke in both check states.
        // A hollow ring has a transparent background, a strokeless one (Metro dark) a zero width.
        if (v.background_gradient)
        {
            renderer.fill_rounded_rect_gradient(ring, ring_radius, *v.background_gradient);
        }
        else
        {
            renderer.fill_rounded_rect(ring, ring_radius, v.background);
        }
        // Aero's rim and inner fill (Theme.hpp's `InsetWell`), round here where the check box's
        // are square.
        if (v.inset_well)
        {
            const Rect rim = ring.inset(Thickness{v.inset_well->inset});
            if (rim.width > 0.0f && rim.height > 0.0f)
            {
                renderer.fill_rounded_rect_gradient(rim, rim.width * 0.5f, v.inset_well->rim);
                const Rect fill = rim.inset(Thickness{1.0f});
                if (v.inset_well->fill && fill.width > 0.0f)
                {
                    renderer.fill_rounded_rect_gradient(fill, fill.width * 0.5f, *v.inset_well->fill);
                }
            }
        }
        const float width = v.resolved_border_width(theme.border_width);
        if (width > 0.0f)
        {
            stroke_rounded_rect_inside(renderer, ring, ring_radius, v.border, width);
        }
    }

    if (v.mark_gradient && !is_bevel)
    {
        // Aero's dot: the radial fill over the whole disc, then its ring 1px inside the edge. The
        // brush maps to the disc inside that ring.
        const Rect  dot = ring.inset(Thickness{(ring.width - detail::controls(theme).radio_dot.rest) * 0.5f});
        const float r   = dot.width * 0.5f;
        const Point c{dot.left() + r, dot.top() + r};
        renderer.draw_geometry(DrawGeometry{PathGeometry::ellipse(c, r, r),
                                            Brush{*v.mark_gradient},
                                            std::nullopt,
                                            StrokeStyle{},
                                            dot.inset(Thickness{0.5f})});
        if (v.mark_stroke)
        {
            stroke_rounded_rect_inside(renderer, dot, r, *v.mark_stroke, 1.0f);
        }
    }
    else if (checked_ && !is_bevel)
    {
        // Eased between the state sizes while the tween runs.
        const float size  = dot_diameter(theme);
        const float dot_d = dot_tween_.current(Rect{0.0f, 0.0f, size, size}).width;
        const Rect  dot   = ring.inset(Thickness{(ring.width - dot_d) * 0.5f});
        // The dot is the visual's `text`: the primary text-on-accent color on an accent disc in Fluent,
        // and draw_classic_radio() above draws COLOR_WINDOWTEXT on the classic well.
        renderer.fill_rounded_rect(dot, dot.width * 0.5f, v.text);
        // Fluent 2's lift on the dot: a 1 epx ring inside it. The extent is the stroke path's
        // height, which turns StrokeRoundedRectGradient's absolute span into RelativeToBoundingBox.
        if (v.mark_border_gradient)
        {
            constexpr float MarkStroke  = 1.0f; // the ring's stroke width
            const float     path_height = dot.height - MarkStroke;
            if (path_height > 0.0f)
            {
                stroke_rounded_rect_gradient_inside(renderer, dot, dot.width * 0.5f, *v.mark_border_gradient, path_height, MarkStroke);
            }
        }
    }

    if (!text_.empty())
    {
        const Rect label{ring.right() + detail::controls(theme).toggle_content_spacing,
                         bounds().top(),
                         std::max(0.0f, bounds().width - ring.width - detail::controls(theme).toggle_content_spacing),
                         bounds().height};
        TextStyle  label_style = theme.text_style;
        label_style.horizontal = TextAlign::Leading;
        // Engraved in the classic eras (`Theme::disabled_text_effect`); the dot above is not.
        if (!is_enabled())
        {
            label_style.effect = theme.disabled_text_effect;
        }
        renderer.draw_text(text_, label, is_enabled() ? theme.colors().text : theme.colors().text_disabled, label_style);
    }
    if (detail::TreeAccess::visuals_covered(*this) && hot() &&
        !is_classic_well(detail::controls(theme).radio_ring.normal.off))
    {
        renderer.fill_rounded_rect(ring, ring.width * 0.5f, colors::White.with_alpha(RadioButtonHotOverlayOpacity));
    }
}

Rect RadioButtonControl::focus_ring_rect(const Theme& theme) const
{
    if (theme.focus_visual == FocusVisual::MetroDashedPair)
    {
        return dot_bounds(theme);
    }
    return detail::toggle_content_ring(bounds(), desired_content_size(), theme);
}

float RadioButtonControl::focus_ring_corner_radius(const Theme& theme) const
{
    return theme.focus_visual == FocusVisual::MetroDashedPair ? dot_bounds(theme).width * 0.5f : theme.corner_radius;
}

std::unique_ptr<ui::Element> RadioButton::build() &&
{
    auto node = std::make_unique<ui::RadioButtonControl>();

    apply(text, *node, [raw = node.get()](const std::string& value) { raw->set_text(value); });
    node->selection_group_ = checked.selection_group();
    apply(checked, *node, [raw = node.get()](const bool& value) { raw->set_checked(value); });
    apply(enabled, *node, [raw = node.get()](const bool& value) { raw->set_enabled(value); });

    if (auto changed = ui::write_back(checked, std::move(on_checked_changed)))
    {
        node->on_checked_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
