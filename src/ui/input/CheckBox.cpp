// SPDX-License-Identifier: MIT
#include "winui/ui/input/CheckBox.hpp"

#include "../../render/Compositor.hpp"
#include "../FocusRingPlacement.hpp"
#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Chrome.hpp"
#include "winui/render/FocusRing.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <array>
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
constexpr float CheckBoxHotOverlayOpacity = 0.16f;
constexpr float BoxCornerRadius           = 4.0f; // small rounded square, distinct from RadioButton's circle

// The tick of the animated accept icon: a 48x48 canvas that fills the box, with the stroke drawn
// around its center scaled by 0.7 and then 1.05.
constexpr float                CheckCanvas = 48.0f;
constexpr float                CheckScale  = 0.7f * 1.05f;
constexpr std::array<Point, 3> CheckPoints{{{-15.172f, 0.016f}, {-5.0f, 10.188f}, {15.337f, -10.337f}}};
// The icon strokes at 4 and sits a canvas unit lower. These two are measured against the Accept
// glyph the box shows at rest instead, so the last frame of a draw-in is that glyph.
constexpr float CheckStroke = 2.94f;
constexpr float CheckDrop   = 0.6f;

// The stroke's end runs 0 -> 1 over 19 frames at 60 fps (9 from a box still held down), its
// start over 4 when the tick erases.
constexpr double      DrawInMs        = 19000.0 / 60.0;
constexpr double      DrawInPressedMs = 9000.0 / 60.0;
constexpr double      EraseMs         = 4000.0 / 60.0;
constexpr CubicBezier DrawInCurve{0.55f, 0.0f, 0.0f, 1.0f};
constexpr CubicBezier EraseCurve{0.167f, 0.167f, 0.833f, 0.833f};

// Strokes the stretch of the tick from `from` to `to`, each a fraction of its length.
void draw_check(Renderer& renderer, const Rect& box, const Color& color, float from, float to)
{
    const float          unit   = box.width / CheckCanvas;
    const Point          center = box.center();
    std::array<Point, 3> p{};
    for (std::size_t i = 0; i < p.size(); ++i)
    {
        p[i] = Point{center.x + CheckPoints[i].x * CheckScale * unit,
                     center.y + (CheckPoints[i].y * CheckScale + CheckDrop) * unit};
    }
    const float first = std::hypot(p[1].x - p[0].x, p[1].y - p[0].y);
    const float total = first + std::hypot(p[2].x - p[1].x, p[2].y - p[1].y);
    const float start = from * total;
    const float end   = to * total;
    if (end - start <= 0.0f)
    {
        return;
    }
    const auto at = [&](float d)
    {
        const auto mix = [](Point a, Point b, float t) { return Point{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; };
        return d <= first ? mix(p[0], p[1], d / first) : mix(p[1], p[2], (d - first) / (total - first));
    };
    std::vector<Point> points{at(start)};
    if (start < first && end > first)
    {
        points.push_back(p[1]);
    }
    points.push_back(at(end));
    renderer.draw_geometry(
        DrawGeometry{.geometry     = PathGeometry::polyline(points, /*closed=*/false),
                     .stroke       = Brush{color},
                     .stroke_style = {.thickness = CheckStroke * CheckScale * unit,
                                      .start_cap = PenLineCap::Round,
                                      .end_cap   = PenLineCap::Round,
                                      .line_join = PenLineJoin::Round}});
}

// Square for retro (corner_radius 0), rounded otherwise. Luna sets toggle_box_radius because its
// button is rounded but its check box is square.
float check_box_radius(const Theme& theme)
{
    return detail::controls(theme).toggle_box_radius >= 0.0f
               ? detail::controls(theme).toggle_box_radius
               : (theme.corner_radius > 0.0f ? BoxCornerRadius : 0.0f);
}
} // namespace

CheckBoxControl::CheckBoxControl(std::string text, std::optional<bool> checked) :
    text_(std::move(text)),
    checked_(checked)
{
    // The control's minimum width and height.
    set_min_width(120.0f);
    set_min_height(32.0f);
}

void CheckBoxControl::set_checked(std::optional<bool> checked)
{
    if (checked_ == checked)
    {
        return;
    }
    const std::optional<bool> was = checked_;
    checked_                      = checked;
    // Only between checked and unchecked: an indeterminate dash comes and goes at once.
    check_ms_ = -1.0;
    if (ticking_ && animations_enabled() && was.has_value() && checked.has_value())
    {
        check_erasing_ = !*checked;
        check_len_ms_  = check_erasing_ ? EraseMs : is_pressed() ? DrawInPressedMs : DrawInMs;
        check_ms_      = 0.0;
    }
    mark_dirty(DirtyFlags::Visual);
    if (on_checked_changed_)
    {
        on_checked_changed_(checked_);
    }
}

void CheckBoxControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_      = false;
        pointer_down_ = false;
        key_down_     = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

void CheckBoxControl::focus_changed(bool /*focused*/)
{
    if (!is_focused())
    {
        key_down_ = false;
    }
    mark_dirty(DirtyFlags::Visual);
}

Rect CheckBoxControl::box_bounds(const Theme& theme) const
{
    const float box = detail::controls(theme).toggle_box_size;
    return Rect{bounds().left(), bounds().top() + (bounds().height - box) * 0.5f, box, box};
}

Size CheckBoxControl::measure_override(Size available, const MeasureContext& context) const
{
    Size desired{detail::controls(context.theme).toggle_box_size, detail::controls(context.theme).toggle_box_size};
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

bool CheckBoxControl::is_pressed() const
{
    if (!is_enabled())
    {
        return false;
    }
    return (pointer_down_ && hovered_) || key_down_;
}

ButtonState CheckBoxControl::visual_state() const
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

bool CheckBoxControl::pointer_override(const PointerEvent& e)
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

bool CheckBoxControl::key_override(const KeyEvent& e)
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

void CheckBoxControl::toggle()
{
    if (!is_enabled())
    {
        return;
    }
    // Three-state: unchecked, checked, indeterminate, unchecked. Two-state: a click checks an
    // unchecked or indeterminate box ("Select all" over a partial selection) and clears a checked one.
    if (checked_ == true)
    {
        set_checked(three_state_ ? std::nullopt : std::optional<bool>{false});
    }
    else
    {
        set_checked(!(three_state_ && !checked_.has_value()));
    }
}

bool CheckBoxControl::hot() const
{
    return is_enabled() && (hovered_ || is_pressed());
}

void CheckBoxControl::sync_composition_override(detail::Compositor& compositor, const Theme& theme)
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
    const bool now_hot = hot();
    compositor.sync_visual(visual_id(), box_bounds(theme), BoxCornerRadius, colors::White);
    if (now_hot == composed_hot_)
    {
        return;
    }
    compositor.animate_opacity(visual_id(),
                               composed_hot_ ? CheckBoxHotOverlayOpacity : 0.0f,
                               now_hot ? CheckBoxHotOverlayOpacity : 0.0f,
                               theme.anim_faster);
    composed_hot_ = now_hot;
}

std::optional<std::pair<float, float>> CheckBoxControl::check_motion() const
{
    if (check_ms_ < 0.0 || !animations_enabled())
    {
        return std::nullopt;
    }
    const double t = std::clamp(check_ms_ / check_len_ms_, 0.0, 1.0);
    if (check_erasing_)
    {
        return std::pair{easing::evaluate(EraseCurve, t), 1.0f};
    }
    return std::pair{0.0f, easing::evaluate(DrawInCurve, t)};
}

void CheckBoxControl::tick_override(Duration delta)
{
    ticking_ = true;
    if (check_ms_ >= 0.0)
    {
        check_ms_ += animation_delta(delta).count();
        if (check_ms_ >= check_len_ms_)
        {
            check_ms_ = -1.0;
        }
        mark_dirty(DirtyFlags::Visual);
    }
}

void CheckBoxControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme      = context.theme;
    const Rect   box        = box_bounds(theme);
    const float  box_radius = check_box_radius(theme);

    // Always filled: the retro well is sunken, and Fluent's unchecked box uses the recessed
    // ControlAltFill ramp. Unlike Button, a flat v.border outline stands in when there is no bevel.
    const float glyph_size = detail::controls(theme).toggle_box_size * 0.7f;
    if (checked_)
    {
        const ButtonVisual& v = detail::controls(theme).check_box_box.for_state(visual_state(), *checked_);
        draw_control_chrome(renderer,
                            box,
                            box_radius,
                            v,
                            theme.border_width,
                            /*draw_flat_border_fallback=*/true,
                            /*use_border_gradient=*/false);
        const std::optional<std::pair<float, float>> motion = detail::controls(theme).check_mark_draws_in && theme.animations_enabled
                                                                  ? check_motion()
                                                                  : std::nullopt;
        if (*checked_ && motion)
        {
            draw_check(renderer, box, v.text, motion->first, motion->second);
        }
        else if (*checked_)
        {
            // `theme.check_glyph` is the tick in every era but Windows 1.x-3.x, whose check box
            // carries an X instead (Theme.hpp).
            draw_icon(renderer, detail::controls(theme).check_glyph, box, v.text, glyph_size, theme.icon_font_family);
        }
        else if (motion)
        {
            // The box is already the unchecked one; the tick erases in its checked color.
            draw_check(renderer,
                       box,
                       detail::controls(theme).check_box_box.for_state(visual_state(), true).text,
                       motion->first,
                       motion->second);
        }
    }
    else
    {
        // Indeterminate (`ControlChrome::check_box_indeterminate`): the same box chrome, then whichever of
        // a dither, a glyph or a square the era marks a mixed value with.
        const IndeterminateVisuals& set = detail::controls(theme).check_box_indeterminate;
        const IndeterminateVisual&  iv  = set.for_state(visual_state());
        draw_control_chrome(renderer,
                            box,
                            box_radius,
                            iv.box,
                            theme.border_width,
                            /*draw_flat_border_fallback=*/true,
                            /*use_border_gradient=*/false);
        if (iv.dither)
        {
            // Inside the edge, so the bevel or outline stays solid: a classic well is two rings of
            // `border_width`, a flat outline one ring of its own width.
            const bool bevel = iv.box.border_style == BorderStyle::Raised || iv.box.border_style == BorderStyle::Sunken;
            const float edge = bevel ? static_cast<float>(iv.box.bevel_rings) * theme.border_width
                                     : iv.box.resolved_border_width(theme.border_width);
            const Rect  inside = box.inset(Thickness{edge});
            if (inside.width > 0.0f && inside.height > 0.0f)
            {
                renderer.fill_rect_pattern(inside, Pattern::Checkerboard50, *iv.dither, iv.box.background);
            }
        }
        switch (set.kind)
        {
            case IndeterminateMark::Glyph:
                draw_icon(renderer, set.glyph, box, iv.box.text, glyph_size, theme.icon_font_family);
                break;
            case IndeterminateMark::Square:
            {
                const float side = std::min(set.square_size, box.width);
                const Rect square{box.left() + (box.width - side) * 0.5f, box.top() + (box.height - side) * 0.5f, side, side};
                if (iv.mark)
                {
                    draw_control_chrome(renderer, square, 0.0f, *iv.mark, theme.border_width, /*draw_flat_border_fallback=*/false);
                }
                else
                {
                    renderer.fill_rounded_rect(square, 0.0f, iv.box.text);
                }
                break;
            }
            case IndeterminateMark::None:
                break;
        }
    }

    if (!text_.empty())
    {
        const Rect label{box.right() + detail::controls(theme).toggle_content_spacing,
                         bounds().top(),
                         std::max(0.0f, bounds().width - box.width - detail::controls(theme).toggle_content_spacing),
                         bounds().height};
        TextStyle  label_style = theme.text_style;
        label_style.horizontal = TextAlign::Leading;
        // A disabled label takes the disabled text color. The classic eras also engrave
        // the label (`Theme::disabled_text_effect`), never the tick.
        if (!is_enabled())
        {
            label_style.effect = theme.disabled_text_effect;
        }
        renderer.draw_text(text_, label, is_enabled() ? theme.colors().text : theme.colors().text_disabled, label_style);
    }
    if (detail::TreeAccess::visuals_covered(*this) && hot())
    {
        renderer.fill_rounded_rect(box_bounds(theme), BoxCornerRadius, colors::White.with_alpha(CheckBoxHotOverlayOpacity));
    }
}

Rect CheckBoxControl::focus_ring_rect(const Theme& theme) const
{
    const Rect box = box_bounds(theme);
    if (theme.focus_visual == FocusVisual::ClassicDotted)
    {
        // The dotted rect surrounds the label, as in Windows, 1 inside it (draw_focus_ring()
        // insets 4). Without a label it frames the box.
        const float spacing = detail::controls(theme).toggle_content_spacing;
        const Rect  label   = text_.empty() ? box
                                            : Rect{box.right() + spacing,
                                                   bounds().top(),
                                                   std::max(0.0f, bounds().width - box.width - spacing),
                                                   bounds().height};
        return label.inflate(3.0f);
    }
    if (theme.focus_visual == FocusVisual::MetroDashedPair)
    {
        return box; // Metro frames the box (a fixed 27x27 focus rect), not the label
    }
    return detail::toggle_content_ring(bounds(), desired_content_size(), theme);
}

float CheckBoxControl::focus_ring_corner_radius(const Theme& theme) const
{
    return theme.focus_visual == FocusVisual::MetroDashedPair ? check_box_radius(theme) : theme.corner_radius;
}

std::unique_ptr<ui::Element> CheckBox::build() &&
{
    auto node = std::make_unique<ui::CheckBoxControl>();

    apply(text, *node, [raw = node.get()](const std::string& value) { raw->set_text(value); });
    apply(checked, *node, [raw = node.get()](const std::optional<bool>& value) { raw->set_checked(value); });
    assert((!three_state || !checked.writes_bool()) &&
           "three_state needs `checked` bound to a Property<std::optional<bool>>: a click can reach "
           "indeterminate, which a Property<bool> cannot hold");
    node->set_three_state(three_state);
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
