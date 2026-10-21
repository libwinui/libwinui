// SPDX-License-Identifier: MIT
#include "winui/ui/panes/TwoPane.hpp"

#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>

#include <cmath>

namespace ui
{

namespace
{

// GridControl::resolve_tracks()'s Pixels/Auto/Star algorithm for exactly two tracks and no spacing, so
// TwoPane does not depend on Grid.
struct TrackPair
{
    float a = 0.0f;
    float b = 0.0f;
};

TrackPair resolve_two_tracks(GridLength len_a, GridLength len_b, float extent, float auto_a, float auto_b)
{
    const auto fixed = [](GridLength len, float auto_desired) -> std::optional<float>
    {
        switch (len.unit)
        {
            case GridUnit::Dip:
                return std::max(0.0f, len.value);
            case GridUnit::Auto:
                return std::max(0.0f, auto_desired);
            case GridUnit::Fraction:
                return std::nullopt;
        }
        return std::nullopt;
    };

    const std::optional<float> fixed_a = fixed(len_a, auto_a);
    const std::optional<float> fixed_b = fixed(len_b, auto_b);

    const float fixed_total = fixed_a.value_or(0.0f) + fixed_b.value_or(0.0f);
    const float star_total  = (len_a.unit == GridUnit::Fraction ? len_a.value : 0.0f) +
                              (len_b.unit == GridUnit::Fraction ? len_b.value : 0.0f);
    const float remaining   = std::max(0.0f, extent - fixed_total);

    TrackPair result;
    result.a = fixed_a.value_or(star_total > 0.0f ? remaining * (len_a.value / star_total) : 0.0f);
    result.b = fixed_b.value_or(star_total > 0.0f ? remaining * (len_b.value / star_total) : 0.0f);
    return result;
}

} // namespace

TwoPaneMode TwoPaneControl::mode() const
{
    switch (resolved_)
    {
        case Configuration::LeftRight:
        case Configuration::RightLeft:
            return TwoPaneMode::Wide;
        case Configuration::TopBottom:
        case Configuration::BottomTop:
            return TwoPaneMode::Tall;
        default:
            return TwoPaneMode::SinglePane;
    }
}

bool TwoPaneControl::pane1_visible() const
{
    return resolved_ != Configuration::Pane2Only;
}

bool TwoPaneControl::pane2_visible() const
{
    return resolved_ != Configuration::Pane1Only;
}

Size TwoPaneControl::measure_override(Size available, const MeasureContext& context) const
{
    // Picks the mode from the room. An unbounded axis never picks a wide or
    // tall mode.
    resolved_ = (pane_priority_ == TwoPanePriority::Pane1) ? Configuration::Pane1Only : Configuration::Pane2Only;

    if (std::isfinite(available.width) && available.width > min_wide_mode_width_ &&
        wide_mode_configuration_ != TwoPaneWideModeConfiguration::SinglePane)
    {
        resolved_ = (wide_mode_configuration_ == TwoPaneWideModeConfiguration::LeftRight)
                        ? Configuration::LeftRight
                        : Configuration::RightLeft;
    }
    else if (std::isfinite(available.height) && available.height > min_tall_mode_height_ &&
             tall_mode_configuration_ != TwoPaneTallModeConfiguration::SinglePane)
    {
        resolved_ = (tall_mode_configuration_ == TwoPaneTallModeConfiguration::TopBottom)
                        ? Configuration::TopBottom
                        : Configuration::BottomTop;
    }

    if (resolved_ == Configuration::Pane1Only || resolved_ == Configuration::Pane2Only)
    {
        pane1_size_    = 0.0f;
        pane2_size_    = 0.0f;
        Element* shown = (resolved_ == Configuration::Pane1Only) ? pane1_.get() : pane2_.get();
        return shown ? shown->measure(available, context) : Size{};
    }

    const bool horizontal = resolved_ == Configuration::LeftRight || resolved_ == Configuration::RightLeft;

    const Size pane1_desired = pane1_ ? pane1_->measure(available, context) : Size{};
    const Size pane2_desired = pane2_ ? pane2_->measure(available, context) : Size{};

    const float     extent = horizontal ? available.width : available.height;
    const TrackPair sizes  = resolve_two_tracks(pane1_length_,
                                                pane2_length_,
                                                extent,
                                                horizontal ? pane1_desired.width : pane1_desired.height,
                                                horizontal ? pane2_desired.width : pane2_desired.height);
    pane1_size_            = sizes.a;
    pane2_size_            = sizes.b;

    const float cross_available = horizontal ? available.height : available.width;
    const float cross_desired   = std::max(horizontal ? pane1_desired.height : pane1_desired.width,
                                           horizontal ? pane2_desired.height : pane2_desired.width);
    const float cross           = cross_available > 0.0f ? cross_available : cross_desired;

    return horizontal ? Size{extent, cross} : Size{cross, extent};
}

void TwoPaneControl::arrange_override(const Rect& final_rect, const ArrangeContext&)
{
    switch (resolved_)
    {
        case Configuration::Pane1Only:
            if (pane1_)
            {
                pane1_->arrange(final_rect);
            }
            break;

        case Configuration::Pane2Only:
            if (pane2_)
            {
                pane2_->arrange(final_rect);
            }
            break;

        case Configuration::LeftRight:
        case Configuration::RightLeft:
        {
            const bool     leading       = resolved_ == Configuration::LeftRight;
            Element* const leading_pane  = leading ? pane1_.get() : pane2_.get();
            Element* const trailing_pane = leading ? pane2_.get() : pane1_.get();
            const float    leading_size  = leading ? pane1_size_ : pane2_size_;
            const float    trailing_size = leading ? pane2_size_ : pane1_size_;
            if (leading_pane)
            {
                leading_pane->arrange(Rect{final_rect.left(), final_rect.top(), leading_size, final_rect.height});
            }
            if (trailing_pane)
            {
                trailing_pane->arrange(
                    Rect{final_rect.left() + leading_size, final_rect.top(), trailing_size, final_rect.height});
            }
            break;
        }

        case Configuration::TopBottom:
        case Configuration::BottomTop:
        {
            const bool     leading       = resolved_ == Configuration::TopBottom;
            Element* const leading_pane  = leading ? pane1_.get() : pane2_.get();
            Element* const trailing_pane = leading ? pane2_.get() : pane1_.get();
            const float    leading_size  = leading ? pane1_size_ : pane2_size_;
            const float    trailing_size = leading ? pane2_size_ : pane1_size_;
            if (leading_pane)
            {
                leading_pane->arrange(Rect{final_rect.left(), final_rect.top(), final_rect.width, leading_size});
            }
            if (trailing_pane)
            {
                trailing_pane->arrange(
                    Rect{final_rect.left(), final_rect.top() + leading_size, final_rect.width, trailing_size});
            }
            break;
        }
    }
}

void TwoPaneControl::render_override(Renderer& renderer, const RenderContext& context) const
{
    if (pane1_visible() && pane1_)
    {
        pane1_->render(renderer, context);
    }
    if (pane2_visible() && pane2_)
    {
        pane2_->render(renderer, context);
    }
}

std::unique_ptr<ui::Element> TwoPane::build() &&
{
    auto node = std::make_unique<ui::TwoPaneControl>();
    apply(pane1_length, *node, [raw = node.get()](const GridLength& v) { raw->set_pane1_length(v); });
    apply(pane2_length, *node, [raw = node.get()](const GridLength& v) { raw->set_pane2_length(v); });
    apply(pane_priority, *node, [raw = node.get()](const TwoPanePriority& v) { raw->set_pane_priority(v); });
    apply(wide_mode_configuration,
          *node,
          [raw = node.get()](const TwoPaneWideModeConfiguration& v) { raw->set_wide_mode_configuration(v); });
    apply(tall_mode_configuration,
          *node,
          [raw = node.get()](const TwoPaneTallModeConfiguration& v) { raw->set_tall_mode_configuration(v); });
    apply(min_wide_mode_width, *node, [raw = node.get()](const float& v) { raw->set_min_wide_mode_width(v); });
    apply(min_tall_mode_height, *node, [raw = node.get()](const float& v) { raw->set_min_tall_mode_height(v); });

    if (pane1)
    {
        node->set_pane1(pane1.take());
    }
    if (pane2)
    {
        node->set_pane2(pane2.take());
    }

    apply_common(*this, *node);
    ui::publish_control(control, node.get());
    return node;
}

} // namespace ui
