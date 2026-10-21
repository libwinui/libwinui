// SPDX-License-Identifier: MIT
#include "winui/ui/input/Rating.hpp"

#include "../../render/ControlGlyphs.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
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
#include <optional>
#include <string>
#include <utility>

#include <cmath>

namespace ui
{

namespace
{
constexpr float StarSize    = 16.0f; // not 32, which is a 2x glyph-metrics workaround
constexpr float ItemSpacing = 8.0f;  // between stars

// The caption sits 4 DIPs after the stars, in 12 px text.
constexpr float CaptionGap      = 4.0f;
constexpr float CaptionFontSize = 12.0f;

// A set value is clamped to [1, max_rating].
std::optional<double> coerce_value(std::optional<double> raw, int max_rating)
{
    if (!raw)
    {
        return std::nullopt;
    }
    return std::clamp(*raw, 1.0, static_cast<double>(max_rating));
}

// A placeholder is clamped too, but 0 and fractional [0,1) values stay,
// since a placeholder is a display hint, not a committed rating.
std::optional<double> coerce_placeholder(std::optional<double> raw, int max_rating)
{
    if (!raw)
    {
        return std::nullopt;
    }
    return std::clamp(*raw, 0.0, static_cast<double>(std::max(1, max_rating)));
}
} // namespace

namespace
{
// The star count, in the int the layout arithmetic uses: at least 1.
int clamp_stars(std::size_t stars)
{
    return static_cast<int>(std::clamp<std::size_t>(stars, 1, 1000));
}
} // namespace

RatingControl::RatingControl(std::size_t max_rating, std::optional<double> value) :
    max_rating_(clamp_stars(max_rating)),
    value_(coerce_value(value, max_rating_))
{
}

void RatingControl::set_max_rating(std::size_t max_rating)
{
    max_rating_ = clamp_stars(max_rating);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    set_value(value_);
    set_placeholder_value(placeholder_value_);
}

void RatingControl::set_value(std::optional<double> value)
{
    const std::optional<double> coerced = coerce_value(value, max_rating_);
    if (coerced == value_)
    {
        return;
    }
    value_ = coerced;
    mark_dirty(DirtyFlags::Visual);
    if (on_value_changed_)
    {
        on_value_changed_(value_);
    }
}

void RatingControl::set_placeholder_value(std::optional<double> value)
{
    const std::optional<double> coerced = coerce_placeholder(value, max_rating_);
    if (coerced == placeholder_value_)
    {
        return;
    }
    placeholder_value_ = coerced;
    mark_dirty(DirtyFlags::Visual);
}

void RatingControl::set_initial_set_value(int value)
{
    initial_set_value_ = std::max(1, value);
}

void RatingControl::set_caption(std::string caption)
{
    if (caption == caption_)
    {
        return;
    }
    caption_ = std::move(caption);
    mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
}

void RatingControl::enabled_changed()
{
    if (!is_enabled())
    {
        hovered_      = false;
        pointer_down_ = false;
        preview_count_.reset();
    }
    mark_dirty(DirtyFlags::Visual);
}

void RatingControl::set_read_only(bool read_only)
{
    if (read_only_ == read_only)
    {
        return;
    }
    read_only_ = read_only;
    if (read_only_)
    {
        hovered_      = false;
        pointer_down_ = false;
        preview_count_.reset();
    }
    mark_dirty(DirtyFlags::Visual);
}

void RatingControl::focus_changed(bool /*focused*/)
{
    mark_dirty(DirtyFlags::Visual);
}

float RatingControl::content_width() const
{
    return static_cast<float>(max_rating_) * StarSize + static_cast<float>(max_rating_ - 1) * ItemSpacing;
}

Rect RatingControl::star_bounds(int index) const
{
    const float x = bounds().left() + static_cast<float>(index) * (StarSize + ItemSpacing);
    const float y = bounds().top() + (bounds().height - StarSize) * 0.5f;
    return Rect{x, y, StarSize, StarSize};
}

double RatingControl::position_to_star_count(Point p) const
{
    const float width = content_width();
    if (width <= 0.0f)
    {
        return 0.0;
    }
    const double fraction = static_cast<double>((p.x - bounds().left()) / width);
    return std::ceil(fraction * static_cast<double>(max_rating_));
}

void RatingControl::set_rating_to(double new_rating, bool originated_from_mouse)
{
    const std::optional<double> old_value = value_;
    const double                rating    = std::clamp(new_rating, 0.0, static_cast<double>(max_rating_));

    // No rating and a computed rating of zero is a no-op.
    if (!(value_ || rating != 0.0))
    {
        return;
    }

    if (!is_clear_enabled_ && rating <= 0.0)
    {
        set_value(1.0);
    }
    else if (rating == old_value && is_clear_enabled_ &&
             (rating != static_cast<double>(max_rating_) || originated_from_mouse))
    {
        set_value(std::nullopt);
    }
    else if (rating > 0.0)
    {
        set_value(rating);
    }
    else
    {
        set_value(std::nullopt);
    }
}

void RatingControl::change_rating_by(double delta, bool originated_from_mouse)
{
    if (delta == 0.0)
    {
        return;
    }

    double rating;
    if (value_)
    {
        const double old = *value_;
        if (old != std::trunc(old))
        {
            // Drop a programmatically set fraction before stepping.
            rating = (delta == -1.0) ? std::trunc(old) : std::trunc(old) + delta;
        }
        else
        {
            rating = old + delta;
        }
    }
    else
    {
        rating = static_cast<double>(initial_set_value_);
    }

    set_rating_to(rating, originated_from_mouse);
}

void RatingControl::set_header(std::string header)
{
    if (header_.set_text(std::move(header)))
    {
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }
}

void RatingControl::arrange_override(const Rect& /*final_rect*/, const ArrangeContext& /*context*/)
{
    header_.arrange(*this);
}

Size RatingControl::measure_override(Size outer, const MeasureContext& context) const
{
    const Size header    = header_.measure(outer.width, context);
    const Size available = ControlHeader::below(outer, header);
    Size       desired{content_width(), StarSize};
    if (!caption_.empty())
    {
        TextStyle caption_style = context.theme.text_style;
        caption_style.font_size = CaptionFontSize;
        const Size caption_size = context.measurer.measure(caption_, caption_style);
        desired.width += CaptionGap + caption_size.width;
        desired.height = std::max(desired.height, caption_size.height);
    }
    desired.width  = std::min(desired.width, available.width);
    desired.height = std::min(desired.height, available.height);
    return ControlHeader::above(desired, header);
}

bool RatingControl::pointer_override(const PointerEvent& e)
{
    if (!is_enabled() || read_only_)
    {
        return false;
    }

    bool handled = false;
    switch (e.action)
    {
        case PointerAction::Move:
            hovered_ = bounds().contains(e.position);
            if (hovered_ || pointer_down_)
            {
                // The preview updates on every move and keeps doing so outside the bounds
                // while dragging (pointer capture), which is how a drag off the left edge clears.
                preview_count_ = position_to_star_count(e.position);
            }
            else
            {
                preview_count_.reset();
            }
            break;

        case PointerAction::Down:
            if (e.button == PointerButton::Left && bounds().contains(e.position))
            {
                pointer_down_  = true;
                hovered_       = true;
                handled        = true;
                preview_count_ = position_to_star_count(e.position);
            }
            break;

        case PointerAction::Up:
            if (e.button == PointerButton::Left && pointer_down_)
            {
                // Commits only on release, unlike Slider.
                set_rating_to(preview_count_.value_or(0.0), true);
                handled        = true;
                pointer_down_  = false;
                hovered_       = bounds().contains(e.position);
                preview_count_ = hovered_ ? std::optional<double>{position_to_star_count(e.position)} : std::nullopt;
            }
            break;

        case PointerAction::Leave:
            hovered_ = false;
            if (!pointer_down_)
            {
                preview_count_.reset();
            }
            break;

        case PointerAction::Wheel:
            break;
    }
    return handled;
}

bool RatingControl::key_override(const KeyEvent& e)
{

    const bool handled = key_handled(e, takes_arrow_key(e.key), false, false);
    if (!is_focused())
    {
        return false; // a key bubbling up from inside: this control reads only its own
    }
    if (!is_enabled() || read_only_ || !is_focused() || e.action != KeyAction::Down)
    {
        return handled;
    }

    switch (e.key)
    {
        // Up/Right increase, Down/Left decrease, as in Slider and PipsPager (Up/Down map to
        // Right/Left before flow direction applies).
        case Key::ArrowUp:
        case Key::ArrowRight:
            change_rating_by(1.0, false);
            break;

        case Key::ArrowDown:
        case Key::ArrowLeft:
            change_rating_by(-1.0, false);
            break;

        case Key::Home:
            set_rating_to(0.0, false);
            break;

        case Key::End:
            set_rating_to(static_cast<double>(max_rating_), false);
            break;

        default:
            break;
    }
    return handled;
}

void RatingControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    const Theme& theme = context.theme;
    header_.render(renderer, theme, is_enabled());
    // The outline star has one color in every state, disabled included.
    const Color outline_color = theme.colors().text.with_alpha(0.6f);
    for (int i = 0; i < max_rating_; ++i)
    {
        draw_icon(renderer, glyphs::RatingStarOutline, star_bounds(i), outline_color, StarSize, theme.icon_font_family);
    }

    // A live preview wins over value(), which wins
    // over placeholder_value().
    double fill_count;
    Color  fill_color;
    if (preview_count_.has_value())
    {
        fill_count = std::clamp(*preview_count_, 0.0, static_cast<double>(max_rating_));
        // Accent once a value exists, else the placeholder color, which is the outline's.
        fill_color = value_ ? theme.colors().accent : theme.colors().text.with_alpha(0.6f);
    }
    else if (value_)
    {
        fill_count = *value_;
        fill_color = is_enabled() ? theme.colors().accent : theme.colors().text_disabled;
    }
    else if (placeholder_value_)
    {
        fill_count = *placeholder_value_;
        fill_color = is_enabled() ? theme.colors().text : theme.colors().text_disabled;
    }
    else
    {
        fill_count = 0.0;
        fill_color = theme.colors().text; // unused: the loop below never runs when fill_count == 0
    }

    for (int i = 0; i < max_rating_ && static_cast<double>(i) < fill_count; ++i)
    {
        const Rect   cell      = star_bounds(i);
        const double remaining = fill_count - static_cast<double>(i);
        if (remaining >= 1.0)
        {
            draw_icon(renderer, glyphs::RatingStar, cell, fill_color, StarSize, theme.icon_font_family);
        }
        else
        {
            // A fractional trailing star, only reachable through a programmatic value: clip it.
            const Rect clip{cell.left(), cell.top(), cell.width * static_cast<float>(remaining), cell.height};
            renderer.push_clip(clip);
            draw_icon(renderer, glyphs::RatingStar, cell, fill_color, StarSize, theme.icon_font_family);
            renderer.pop_clip();
        }
    }

    if (!caption_.empty())
    {
        const float stars_width  = content_width();
        const float caption_left = bounds().left() + stars_width + CaptionGap;
        const Rect  caption_rect{caption_left,
                                 bounds().top(),
                                 std::max(0.0f, bounds().width - stars_width - CaptionGap),
                                 bounds().height};
        TextStyle   caption_style = theme.text_style;
        caption_style.font_size   = CaptionFontSize;
        caption_style.horizontal  = TextAlign::Leading;
        // The secondary text color, derived from colors.text.
        renderer.draw_text(caption_, caption_rect, theme.colors().text.with_alpha(0.6f), caption_style);
    }
}

std::unique_ptr<ui::Element> Rating::build() &&
{
    auto node = std::make_unique<ui::RatingControl>(max_rating.initial(), value.initial());
    apply(header, *node, [raw = node.get()](const std::string& v) { raw->set_header(v); });
    apply(max_rating, *node, [raw = node.get()](const std::size_t& v) { raw->set_max_rating(v); });
    apply(initial_set_value, *node, [raw = node.get()](const int& v) { raw->set_initial_set_value(v); });

    // A value the control coerced (rounded or clamped) goes back to a two-way property, so it ends up
    // equal to the control: also when the control already held the coerced value, and at build.
    // Written directly, not through on_value_changed, since the control's value may not change.
    const auto write = ui::shared_writer(value);
    apply(value,
          *node,
          [raw = node.get(), write](const std::optional<double>& v)
          {
              raw->set_value(v);
              if (write && raw->value() != v)
              {
                  (*write)(raw->value());
              }
          });
    apply(placeholder_value, *node, [raw = node.get()](const std::optional<double>& v) { raw->set_placeholder_value(v); });
    apply(clear_enabled, *node, [raw = node.get()](const bool& v) { raw->set_clear_enabled(v); });
    apply(enabled, *node, [raw = node.get()](const bool& v) { raw->set_enabled(v); });
    apply(caption, *node, [raw = node.get()](const std::string& v) { raw->set_caption(v); });
    apply(read_only, *node, [raw = node.get()](const bool& v) { raw->set_read_only(v); });

    if (auto changed = ui::write_back(write, std::move(on_value_changed)))
    {
        node->on_value_changed(std::move(changed));
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
