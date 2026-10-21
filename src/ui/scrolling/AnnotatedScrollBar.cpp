// SPDX-License-Identifier: MIT
#include "winui/ui/scrolling/AnnotatedScrollBar.hpp"

#include "../themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Version.hpp"
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
#include <utility>
#include <vector>

#include <cstddef>

namespace ui
{

namespace
{
// AnnotatedScrollBar's sizes.
constexpr float RailWidth                       = 30.0f; // the thumb's width, also the step buttons' width
constexpr float ThumbHeight                     = 3.0f;  // the thumb's height
constexpr float ThumbCornerRadius               = 1.5f;  // the thumb's corner radius
constexpr float AnnotatedScrollBarButtonHeight  = 16.0f; // the step buttons' minimum height and width
constexpr float LabelsGridMinWidth              = 44.0f;
constexpr float AnnotatedScrollBarDefaultLength = 200.0f; // Slider's nominal length

constexpr float DetailMaxWidth  = 360.0f; // the detail card's maximum width
constexpr float DetailMinHeight = 40.0f;  // the detail card's minimum height
// The detail card reuses ToolTip's padding.
constexpr Thickness DetailPadding = Symmetric{.horizontal = 8.0f, .vertical = 6.0f};

// The viewport's share that one small change scrolls when no small change is set.
constexpr double DefaultViewportToSmallChangeRatio = 8.0;
} // namespace

// Reads the owner's detail text live, like ToolTipSurface does, so a later hover move cannot leave
// it dangling.
class AnnotatedScrollBarDetailSurface final : public Element
{
public:
    explicit AnnotatedScrollBarDetailSurface(AnnotatedScrollBarControl* owner) : owner_(owner)
    {
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext& context) const override
    {
        TextStyle style   = context.theme.text_style;
        style.horizontal  = TextAlign::Center;
        style.vertical    = TextAlign::Center;
        const float max_w = std::max(0.0f, DetailMaxWidth - DetailPadding.horizontal());
        Size        text  = context.measurer.measure(owner_->detail_text_, style);
        return {std::min(text.width, max_w) + DetailPadding.horizontal(),
                std::max(text.height + DetailPadding.vertical(), DetailMinHeight)};
    }

    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        const Theme& theme = context.theme;
        renderer.fill_rounded_rect(bounds(), theme.overlay_corner_radius, theme.surfaces.tooltip);
        stroke_rounded_rect_inside(renderer, bounds(), theme.overlay_corner_radius, theme.surfaces.field_border, theme.border_width);

        TextStyle style  = theme.text_style;
        style.horizontal = TextAlign::Center;
        style.vertical   = TextAlign::Center;
        renderer.draw_text(owner_->detail_text_, bounds().inset(DetailPadding), theme.surfaces.tooltip_text, style);
    }


private:
    AnnotatedScrollBarControl* owner_; // non-owning; the owner builds this lazily and outlives it
};

double AnnotatedScrollBarControl::maximum() const
{
    return target_ ? static_cast<double>(target_->scrollable_height()) : maximum_;
}

void AnnotatedScrollBarControl::set_maximum(double maximum)
{
    maximum_ = std::max(0.0, maximum);
    mark_dirty(DirtyFlags::Visual);
}

double AnnotatedScrollBarControl::viewport_size() const
{
    return target_ ? static_cast<double>(target_->bounds().height) : viewport_size_;
}

void AnnotatedScrollBarControl::set_viewport_size(double viewport_size)
{
    viewport_size_ = std::max(0.0, viewport_size);
    mark_dirty(DirtyFlags::Visual);
}

double AnnotatedScrollBarControl::value() const
{
    return target_ ? static_cast<double>(target_->vertical_offset()) : value_;
}

void AnnotatedScrollBarControl::set_value(double value)
{
    value_ = value; // no clamp
    mark_dirty(DirtyFlags::Visual);
}

void AnnotatedScrollBarControl::set_target(ScrollControl* target)
{
    if (target_ && target_ != target)
    {
        // set_target() hid the target's own scrollbar. Restore it before letting go.
        target_->set_show_scrollbar(true);
    }
    target_ = target;
    if (target_)
    {
        // A vertical scroll controller replaces the built-in scrollbar instead of
        // drawing next to it, so hide the target's own overlay.
        target_->set_show_scrollbar(false);
        // At once, with no view-change request, as a drag on the target's own scrollbar does.
        on_scroll_to_requested_ = [this](double offset) { target_->jump_to_vertical_offset(static_cast<float>(offset)); };
        on_scroll_by_requested_ = [this](double delta)
        { target_->jump_to_vertical_offset(target_->vertical_offset() + static_cast<float>(delta)); };
    }
    else
    {
        // The handlers set_target() installed capture `this` and would scroll a null target_.
        on_scroll_to_requested_ = nullptr;
        on_scroll_by_requested_ = nullptr;
    }
    mark_dirty(DirtyFlags::Visual);
}

void AnnotatedScrollBarControl::set_annotations(std::vector<AnnotatedScrollBarAnnotation> annotations)
{
    annotations_ = std::move(annotations);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void AnnotatedScrollBarControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_up_ = hovered_down_ = hovered_rail_ = pressed_up_ = pressed_down_ = dragging_ = false;
        close_detail_label();
    }
    mark_dirty(DirtyFlags::Visual);
}

void AnnotatedScrollBarControl::focus_changed(bool /*focused*/)
{
    mark_dirty(DirtyFlags::Visual);
}

ButtonState AnnotatedScrollBarControl::up_button_state() const
{
    if (!can_step_up())
    {
        return ButtonState::Disabled;
    }
    if (pressed_up_ && hovered_up_)
    {
        return ButtonState::Pressed;
    }
    return hovered_up_ ? ButtonState::Hover : ButtonState::Normal;
}

ButtonState AnnotatedScrollBarControl::down_button_state() const
{
    if (!can_step_down())
    {
        return ButtonState::Disabled;
    }
    if (pressed_down_ && hovered_down_)
    {
        return ButtonState::Pressed;
    }
    return hovered_down_ ? ButtonState::Hover : ButtonState::Normal;
}

Rect AnnotatedScrollBarControl::rail_column_bounds() const
{
    return Rect{bounds().right() - RailWidth, bounds().top(), RailWidth, bounds().height};
}

Rect AnnotatedScrollBarControl::up_button_bounds() const
{
    const Rect col = rail_column_bounds();
    return Rect{col.left(), col.top(), col.width, AnnotatedScrollBarButtonHeight};
}

Rect AnnotatedScrollBarControl::down_button_bounds() const
{
    const Rect col = rail_column_bounds();
    return Rect{col.left(), col.bottom() - AnnotatedScrollBarButtonHeight, col.width, AnnotatedScrollBarButtonHeight};
}

Rect AnnotatedScrollBarControl::rail_bounds() const
{
    const Rect  col    = rail_column_bounds();
    const float top    = col.top() + AnnotatedScrollBarButtonHeight;
    const float bottom = col.bottom() - AnnotatedScrollBarButtonHeight;
    return Rect{col.left(), top, col.width, std::max(0.0f, bottom - top)};
}

Rect AnnotatedScrollBarControl::labels_column_bounds() const
{
    const Rect rail = rail_bounds();
    return Rect{bounds().left(), rail.top(), std::max(0.0f, rail_column_bounds().left() - bounds().left()), rail.height};
}

double AnnotatedScrollBarControl::label_offset_factor() const
{
    const float  rail_height = rail_bounds().height;
    const double range       = std::max(1.0, maximum()); // the range floors at 1
    const double viewport    = viewport_size();
    return (static_cast<double>(rail_height) - ThumbHeight) / range * (1.0 - viewport / (viewport + range));
}

float AnnotatedScrollBarControl::offset_to_rail_y(double offset) const
{
    return rail_bounds().top() + static_cast<float>(offset * label_offset_factor());
}

double AnnotatedScrollBarControl::thumb_travel_factor() const
{
    const float  rail_height = rail_bounds().height;
    const double range       = maximum();
    if (range <= 0.0)
    {
        return 0.0;
    }
    return (static_cast<double>(rail_height) - ThumbHeight) / range;
}

float AnnotatedScrollBarControl::thumb_offset_to_rail_y(double offset) const
{
    return rail_bounds().top() + static_cast<float>(offset * thumb_travel_factor());
}

double AnnotatedScrollBarControl::rail_y_to_offset(float y) const
{
    const double factor = thumb_travel_factor();
    if (factor <= 0.0)
    {
        return 0.0;
    }
    const Rect  rail    = rail_bounds();
    const float local_y = std::clamp(y - rail.top(), 0.0f, rail.height);
    return std::clamp(static_cast<double>(local_y) / factor, 0.0, maximum());
}

Rect AnnotatedScrollBarControl::thumb_bounds() const
{
    const Rect col = rail_column_bounds();
    return Rect{col.left(), thumb_offset_to_rail_y(value()), col.width, ThumbHeight};
}

TextStyle AnnotatedScrollBarControl::label_text_style(const Theme& theme) const
{
    TextStyle style  = theme.text_style;
    style.horizontal = TextAlign::Trailing;
    style.vertical   = TextAlign::Leading;
    return style;
}

// Collapses colliding and out-of-bounds labels: bottom-up, hide labels off the rail or
// overlapping the last kept one. Label 0 stays unless its only collision is the last label.
std::vector<bool> AnnotatedScrollBarControl::visible_labels() const
{
    std::vector<bool> visible(annotations_.size(), true);
    const float       rail_height = rail_bounds().height;
    if (annotations_.empty() || rail_height <= 0.0f)
    {
        return visible;
    }

    double      previous_top   = -1.0;
    std::size_t previous_index = annotations_.size();
    for (std::size_t i = annotations_.size(); i-- > 0;)
    {
        const double top    = annotations_[i].scroll_offset * label_offset_factor();
        const float  height = i < label_heights_.size() ? label_heights_[i] : 0.0f;
        const double bottom = top + height;

        const bool out_of_bounds = top < 0.0 || bottom > rail_height;
        const bool colliding     = previous_top >= 0.0 && previous_top <= bottom;

        if (out_of_bounds)
        {
            visible[i] = false;
        }
        else if (colliding)
        {
            std::size_t to_collapse = i;
            if (i == 0 && previous_index < annotations_.size() - 1)
            {
                to_collapse = previous_index;
            }
            visible[to_collapse] = false;
        }
        else
        {
            previous_top   = top;
            previous_index = i;
        }
    }
    return visible;
}

Size AnnotatedScrollBarControl::measure_override(Size available, const MeasureContext& context) const
{
    label_heights_.clear();
    float           label_column_width = 0.0f;
    const TextStyle style              = label_text_style(context.theme);
    for (const AnnotatedScrollBarAnnotation& label : annotations_)
    {
        const Size size = context.measurer.measure(label.text, style);
        label_heights_.push_back(size.height);
        label_column_width = std::max(label_column_width, size.width);
    }
    if (!annotations_.empty())
    {
        label_column_width = std::max(label_column_width, LabelsGridMinWidth);
    }

    Size desired{label_column_width + RailWidth, AnnotatedScrollBarDefaultLength};
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return desired;
}

void AnnotatedScrollBarControl::step(AnnotatedScrollBarScrollingEventKind kind)
{
    const double small = small_change_ > 0.0 ? small_change_ : (viewport_size() / DefaultViewportToSmallChangeRatio);
    // The sign flips: IncrementButton is the top button and scrolls toward Minimum.
    const double delta  = kind == AnnotatedScrollBarScrollingEventKind::IncrementButton ? -small : small;
    const double offset = std::clamp(value() + delta, 0.0, maximum());

    AnnotatedScrollBarScrollingArgs args{.offset = offset, .kind = kind};
    bool                            canceled = false;
    if (on_scrolling_)
    {
        canceled = detail::canceled(on_scrolling_(args));
    }
    if (canceled)
    {
        return;
    }
    if (on_scroll_by_requested_)
    {
        on_scroll_by_requested_(delta);
    }
    mark_dirty(DirtyFlags::Visual);
}

void AnnotatedScrollBarControl::scroll_to(double offset, AnnotatedScrollBarScrollingEventKind kind)
{
    offset = std::clamp(offset, 0.0, maximum());
    AnnotatedScrollBarScrollingArgs args{.offset = offset, .kind = kind};
    bool                            canceled = false;
    if (on_scrolling_)
    {
        canceled = detail::canceled(on_scrolling_(args));
    }
    if (canceled)
    {
        return;
    }
    if (on_scroll_to_requested_)
    {
        on_scroll_to_requested_(offset);
    }
    mark_dirty(DirtyFlags::Visual);
}

void AnnotatedScrollBarControl::request_detail_label(double offset)
{
    if (!on_detail_text_requested_)
    {
        close_detail_label();
        return;
    }
    // Clamped above at maximum plus viewport. rail_y_to_offset() already floors at 0.
    detail_text_ = on_detail_text_requested_(std::min(offset, maximum() + viewport_size()));
    detail_open_ = true;
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

void AnnotatedScrollBarControl::close_detail_label()
{
    if (!detail_open_)
    {
        return;
    }
    detail_open_ = false;
    mark_dirty(DirtyFlags::Visual);
    sync_popup();
}

void AnnotatedScrollBarControl::sync_popup()
{
    if (!detail_open_ || !on_detail_text_requested_)
    {
        close_popup();
        return;
    }
    if (!detail_surface_)
    {
        detail_surface_ = std::make_unique<AnnotatedScrollBarDetailSurface>(this);
    }
    // The label follows the thumb's mapping, not the labels', and opens above that point.
    const Rect  col = rail_column_bounds();
    const float y   = thumb_offset_to_rail_y(hover_offset_);
    open_popup({
        .content     = detail_surface_.get(),
        .anchor_rect = Rect{col.left() - bounds().x, y - bounds().y, col.width, 0.0f},
        .placement   = PopupPlacement::Above,
        .on_dismiss  = [this](PopupCloseReason) { close_detail_label(); },
    });
}

bool AnnotatedScrollBarControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled())
    {
        return false;
    }

    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
        {
            hovered_up_          = up_button_bounds().contains(e.position);
            hovered_down_        = down_button_bounds().contains(e.position);
            const bool over_rail = rail_bounds().contains(e.position);
            hovered_rail_        = over_rail;

            if (over_rail || dragging_)
            {
                hover_offset_ = rail_y_to_offset(e.position.y);
                request_detail_label(hover_offset_);
            }
            else if (!dragging_)
            {
                close_detail_label();
            }

            if (dragging_)
            {
                scroll_to(hover_offset_, AnnotatedScrollBarScrollingEventKind::Drag);
            }
            break;
        }

        case PointerAction::Down:
            if (e.button != PointerButton::Left)
            {
                break;
            }
            if (up_button_bounds().contains(e.position))
            {
                pressed_up_ = hovered_up_ = true;
                handled                   = true;
                if (can_step_up())
                {
                    step(AnnotatedScrollBarScrollingEventKind::IncrementButton);
                }
            }
            else if (down_button_bounds().contains(e.position))
            {
                pressed_down_ = hovered_down_ = true;
                handled                       = true;
                if (can_step_down())
                {
                    step(AnnotatedScrollBarScrollingEventKind::DecrementButton);
                }
            }
            else if (rail_bounds().contains(e.position))
            {
                dragging_     = true;
                handled       = true;
                hover_offset_ = rail_y_to_offset(e.position.y);
                request_detail_label(hover_offset_);
                scroll_to(hover_offset_, AnnotatedScrollBarScrollingEventKind::Click);
            }
            break;

        case PointerAction::Up:
            if (e.button != PointerButton::Left)
            {
                break;
            }
            handled     = pressed_up_ || pressed_down_ || dragging_;
            pressed_up_ = pressed_down_ = false;
            if (dragging_)
            {
                dragging_ = false;
                if (!rail_bounds().contains(e.position))
                {
                    close_detail_label();
                }
            }
            break;

        case PointerAction::Leave:
            hovered_up_ = hovered_down_ = hovered_rail_ = false;
            if (!dragging_)
            {
                close_detail_label();
            }
            break;

        case PointerAction::Wheel:
            break;
    }
    return handled;
}

bool AnnotatedScrollBarControl::key_override(const KeyEvent& e)
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

    switch (e.key)
    {
        case Key::ArrowUp:
            if (can_step_up())
            {
                step(AnnotatedScrollBarScrollingEventKind::IncrementButton);
            }
            break;
        case Key::ArrowDown:
            if (can_step_down())
            {
                step(AnnotatedScrollBarScrollingEventKind::DecrementButton);
            }
            break;
        case Key::Home:
            scroll_to(0.0, AnnotatedScrollBarScrollingEventKind::Click);
            break;
        case Key::End:
            scroll_to(maximum(), AnnotatedScrollBarScrollingEventKind::Click);
            break;
        default:
            break;
    }
    return handled;
}

namespace
{
// The step button glyph: primary, secondary, tertiary or disabled text by state. Only read
// when scroll_button_chromeless.
Color scroll_button_glyph_color(const Theme& theme, ButtonState state)
{
    switch (state)
    {
        case ButtonState::Hover:
            return theme.palette.text_fill_secondary;
        case ButtonState::Pressed:
            return theme.palette.text_fill_tertiary;
        case ButtonState::Disabled:
            return theme.palette.text_fill_disabled;
        case ButtonState::Normal:
            break;
    }
    return theme.palette.text_fill_primary;
}

// The chromeless step button when theme.scroll_button_chromeless is set, otherwise the
// bordered ButtonVisual look.
void draw_scroll_button(Renderer& renderer, const Theme& theme, const Rect& bounds, ButtonState state, const IconGlyph& glyph)
{
    if (detail::controls(theme).scroll_button_chromeless)
    {
        draw_icon(renderer, glyph, bounds, scroll_button_glyph_color(theme, state), 0.0f, theme.icon_font_family);
        return;
    }
    const ButtonVisual& v = theme.for_state(state);
    renderer.fill_rounded_rect(bounds, theme.corner_radius, v.background);
    stroke_rounded_rect_inside(renderer, bounds, theme.corner_radius, v.border, theme.border_width);
    draw_icon(renderer, glyph, bounds, v.text, 0.0f, theme.icon_font_family);
}
} // namespace

void AnnotatedScrollBarControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    draw_scroll_button(renderer, theme, up_button_bounds(), up_button_state(), glyphs::ScrollUp);
    draw_scroll_button(renderer, theme, down_button_bounds(), down_button_state(), glyphs::ScrollDown);

    // The rail track is only there for the retro themes, which need their COLOR_SCROLLBAR groove.
    renderer.fill_rounded_rect(rail_bounds(), 0.0f, theme.surfaces.scrollbar_track);

    const Rect label_col = labels_column_bounds();
    if (label_col.width > 0.0f && !annotations_.empty())
    {
        const TextStyle         style      = label_text_style(theme);
        const Color             text_color = is_enabled() ? theme.colors().text : theme.colors().text_disabled;
        const std::vector<bool> visible    = visible_labels();
        for (std::size_t i = 0; i < annotations_.size(); ++i)
        {
            if (!visible[i])
            {
                continue;
            }
            const float top    = offset_to_rail_y(annotations_[i].scroll_offset);
            const float height = i < label_heights_.size() ? label_heights_[i] : theme.text_style.font_size;
            renderer.draw_text(annotations_[i].text, Rect{label_col.left(), top, label_col.width, height}, text_color, style);
        }
    }

    renderer.fill_rounded_rect(thumb_bounds(),
                               ThumbCornerRadius,
                               is_enabled() ? theme.surfaces.scrollbar_thumb : theme.colors().text_disabled);
}

std::unique_ptr<ui::Element> AnnotatedScrollBar::build() &&
{
    auto node = std::make_unique<ui::AnnotatedScrollBarControl>();
    apply(maximum, *node, [raw = node.get()](const double& v) { raw->set_maximum(v); });
    apply(viewport_size, *node, [raw = node.get()](const double& v) { raw->set_viewport_size(v); });
    apply(value, *node, [raw = node.get()](const double& v) { raw->set_value(v); });
    apply(small_change, *node, [raw = node.get()](const double& v) { raw->set_small_change(v); });
    apply(annotations,
          *node,
          [raw = node.get()](const std::vector<AnnotatedScrollBarAnnotation>& v) { raw->set_annotations(v); });

    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });

    if (on_scrolling)
    {
        node->on_scrolling(std::move(on_scrolling));
    }
    if (on_detail_text_requested)
    {
        node->on_detail_text_requested(std::move(on_detail_text_requested));
    }
    if (on_scroll_to_requested)
    {
        node->on_scroll_to_requested(std::move(on_scroll_to_requested));
    }
    if (on_scroll_by_requested)
    {
        node->on_scroll_by_requested(std::move(on_scroll_by_requested));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
