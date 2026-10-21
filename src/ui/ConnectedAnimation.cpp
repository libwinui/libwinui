// SPDX-License-Identifier: MIT
#include "winui/ui/ConnectedAnimation.hpp"

#include "../render/Compositor.hpp"
#include "winui/core/Callback.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/OverlayShadow.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/RenderRecording.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ui
{

namespace
{

// The ConnectedAnimation timeout: a prepared animation nobody starts goes after this long.
constexpr double TimeoutMs = 2000.0;

// ArcFlightPath: at two thirds of the way the element dips 80 DIPs
// down and 32 toward the viewer, and grows 10 % when its size does not change otherwise.
constexpr float       DippedY     = 80.0f;
constexpr float       DippedZ     = 32.0f;
constexpr float       DippedScale = 1.1f;
constexpr float       PeakTime    = 0.66f;
constexpr CubicBezier DipIn{0.35f, 0.0f, 0.55f, 1.0f};
constexpr CubicBezier DipOut{0.45f, 0.0f, 0.55f, 1.0f};

// ShortFlightPath.
constexpr double      DirectDurationMs = 200.0;
constexpr CubicBezier DirectEasing{0.1f, 0.9f, 0.2f, 1.0f};

// How far into the dip the element is at linear time `t` (0 at rest, 1 at the peak).
float dip(float t)
{
    if (t <= PeakTime)
    {
        return ease(DipIn, t / PeakTime);
    }
    return 1.0f - ease(DipOut, (t - PeakTime) / (1.0f - PeakTime));
}

Rect lerp_rect(const Rect& a, const Rect& b, float t)
{
    return Rect{a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.width + (b.width - a.width) * t,
                a.height + (b.height - a.height) * t};
}

// The transform that draws what was recorded at `from` into `to`.
Matrix map_rect(const Rect& from, const Rect& to)
{
    const float sx = from.width > 0.0f ? to.width / from.width : 1.0f;
    const float sy = from.height > 0.0f ? to.height / from.height : 1.0f;
    return Matrix::translation(-from.x, -from.y) * Matrix::scale(sx, sy) * Matrix::translation(to.x, to.y);
}

void replay_into(Renderer& renderer, const RenderRecording& snapshot, const Rect& recorded, const Rect& shown, float opacity)
{
    if (opacity <= 0.0f || snapshot.empty())
    {
        return;
    }
    const Matrix m = map_rect(recorded, shown);
    renderer.push_transform(m);
    renderer.push_opacity_layer(std::min(opacity, 1.0f));
    snapshot.replay(renderer);
    renderer.pop_opacity_layer();
    renderer.pop_transform();
}

} // namespace

// ---- ConnectedAnimation ----

bool ConnectedAnimation::try_start(Element& destination, std::vector<Element*> coordinated)
{
    if (state_ != ConnectedAnimationState::Prepared)
    {
        return false;
    }
    pending_destination_ = std::make_unique<ElementWatch>(&destination);
    pending_coordinated_ = std::move(coordinated);
    state_               = ConnectedAnimationState::Started;
    elapsed_             = 0.0;
    return true;
}

void ConnectedAnimation::cancel()
{
    finish(false);
}

void ConnectedAnimation::begin(const Theme& theme)
{
    Element* destination = pending_destination_ ? pending_destination_->get() : nullptr;
    if (destination == nullptr)
    {
        finish(false); // the destination went before it was ever laid out
        return;
    }
    destination_rect_ = destination->bounds();
    if (destination_rect_.width <= 0.0f || destination_rect_.height <= 0.0f)
    {
        finish(false); // nothing to see: a zero-size one is canceled
        return;
    }

    duration_ = service_->default_duration().count();
    easing_   = service_->default_easing();
    if (flight_path_ && std::holds_alternative<ShortFlightPath>(*flight_path_))
    {
        duration_ = DirectDurationMs;
        easing_   = DirectEasing;
    }

    const auto take = [&](Element& element, const Rect& from, bool coordinated)
    {
        Flight flight;
        flight.element = std::make_unique<ElementWatch>(&element);
        element.render(flight.snapshot, theme);
        flight.from        = from;
        flight.to          = element.bounds();
        flight.coordinated = coordinated;
        TransitionTargetState hidden{};
        hidden.opacity = 0.0f;
        element.set_transition_target(hidden);
        flights_.push_back(std::move(flight));
    };
    take(*destination, source_rect_, false);

    // Coordinated elements start where they would sit if they had the same layout relation to the
    // source as they have to the destination.
    for (Element* element : pending_coordinated_)
    {
        if (element == nullptr || element == destination)
        {
            continue;
        }
        const Rect dest = element->bounds();
        Rect       from = dest;
        if (scale_enabled_)
        {
            const float sx = source_rect_.width / destination_rect_.width;
            const float sy = source_rect_.height / destination_rect_.height;
            from           = Rect{source_rect_.x + (dest.x - destination_rect_.x) * sx,
                                  source_rect_.y + (dest.y - destination_rect_.y) * sy,
                                  dest.width * sx,
                                  dest.height * sy};
        }
        else
        {
            from.x = source_rect_.x + (dest.x - destination_rect_.x) + (source_rect_.width - destination_rect_.width) * 0.5f;
            from.y = source_rect_.y + (dest.y - destination_rect_.y) +
                     (source_rect_.height - destination_rect_.height) * 0.5f;
        }
        take(*element, from, true);
    }
    pending_coordinated_.clear();
    pending_destination_.reset();

    state_   = ConnectedAnimationState::Running;
    elapsed_ = 0.0;
}

void ConnectedAnimation::finish(bool completed)
{
    if (state_ == ConnectedAnimationState::Complete || state_ == ConnectedAnimationState::Canceled)
    {
        return;
    }
    for (Flight& f : flights_)
    {
        if (Element* element = f.element ? f.element->get() : nullptr)
        {
            element->set_transition_target(TransitionTargetState{});
        }
    }
    flights_.clear();
    pending_destination_.reset();
    pending_coordinated_.clear();
    source_snapshot_.clear();
    state_ = completed ? ConnectedAnimationState::Complete : ConnectedAnimationState::Canceled;
}

void ConnectedAnimation::render(Renderer& renderer, const Theme& theme) const
{
    if (state_ == ConnectedAnimationState::Prepared || state_ == ConnectedAnimationState::Started)
    {
        // The hand-off: the source stays where it was while the page under it changes.
        replay_into(renderer, source_snapshot_, source_rect_, source_rect_, 1.0f);
        return;
    }
    if (state_ != ConnectedAnimationState::Running)
    {
        return;
    }

    const float t = duration_ > 0.0 ? static_cast<float>(std::min(1.0, elapsed_ / duration_)) : 1.0f;
    const float e = ease(easing_, t);

    const bool  arc    = !flight_path_ || std::holds_alternative<ArcFlightPath>(*flight_path_);
    const bool  shadow = arc && (!flight_path_ || std::get<ArcFlightPath>(*flight_path_).shadow);
    const float d      = arc ? dip(t) : 0.0f;

    for (const Flight& f : flights_)
    {
        Rect shown;
        if (scale_enabled_)
        {
            shown = lerp_rect(f.from, f.to, e);
        }
        else
        {
            const Point from_c = f.from.center();
            const Point to_c   = f.to.center();
            const Point c{from_c.x + (to_c.x - from_c.x) * e, from_c.y + (to_c.y - from_c.y) * e};
            shown = Rect{c.x - f.to.width * 0.5f, c.y - f.to.height * 0.5f, f.to.width, f.to.height};
        }
        if (arc)
        {
            shown.y += DippedY * d;
            // The grow only when nothing else scales the element (its own size does not change).
            if (f.from.width == f.to.width && f.from.height == f.to.height)
            {
                const float s = 1.0f + (DippedScale - 1.0f) * d;
                shown.width *= s;
                shown.height *= s;
            }
        }
        if (shadow && d > 0.0f)
        {
            draw_drop_shadow(renderer, shown, 0.0f, ShadowRecipe::for_elevation(DippedZ * d, theme.dark));
        }
        if (f.coordinated)
        {
            // No source snapshot: the element fades in as it moves (the host visual's opacity 0 -> 1).
            replay_into(renderer, f.snapshot, f.to, shown, e);
            continue;
        }
        const Rect source_shown = scale_enabled_ ? shown
                                                 : Rect{shown.center().x - source_rect_.width * 0.5f,
                                                        shown.center().y - source_rect_.height * 0.5f,
                                                        source_rect_.width,
                                                        source_rect_.height};
        replay_into(renderer, source_snapshot_, source_rect_, source_shown, 1.0f - e);
        replay_into(renderer, f.snapshot, f.to, shown, e);
    }
}

// ---- ConnectedAnimationService ----

ConnectedAnimation& ConnectedAnimationService::prepare_to_animate(std::string key, Element& source)
{
    for (Slot& slot : animations_)
    {
        if (slot.key == key)
        {
            slot.animation->finish(false);
        }
    }
    std::erase_if(animations_, [&key](const Slot& slot) { return slot.key == key; });

    auto animation          = std::unique_ptr<ConnectedAnimation>(new ConnectedAnimation(*this));
    animation->source_rect_ = source.bounds();
    source.render(animation->source_snapshot_, theme_ != nullptr ? *theme_ : Theme::default_light());
    ConnectedAnimation& observer = *animation;
    animations_.push_back(Slot{std::move(key), std::move(animation)});
    return observer;
}

ConnectedAnimation* ConnectedAnimationService::animation(const std::string& key) const
{
    for (const Slot& slot : animations_)
    {
        if (slot.key == key && (slot.animation->state_ == ConnectedAnimationState::Prepared ||
                                slot.animation->state_ == ConnectedAnimationState::Started ||
                                slot.animation->state_ == ConnectedAnimationState::Running))
        {
            return slot.animation.get();
        }
    }
    return nullptr;
}

void ConnectedAnimationService::after_layout()
{
    const Theme& theme = theme_ != nullptr ? *theme_ : Theme::default_light();
    for (Slot& slot : animations_)
    {
        if (slot.animation->state_ == ConnectedAnimationState::Started)
        {
            slot.animation->begin(theme);
        }
    }
}

bool ConnectedAnimationService::tick(Duration delta)
{
    std::vector<Callback<void()>> completed;
    for (Slot& slot : animations_)
    {
        ConnectedAnimation& a = *slot.animation;
        a.elapsed_ += delta.count();
        switch (a.state_)
        {
            case ConnectedAnimationState::Prepared:
            case ConnectedAnimationState::Started:
                if (a.elapsed_ >= TimeoutMs)
                {
                    a.finish(false);
                }
                break;
            case ConnectedAnimationState::Running:
                if (a.elapsed_ >= a.duration_ || !animations_enabled())
                {
                    a.finish(true);
                    if (a.on_completed_)
                    {
                        completed.push_back(std::move(a.on_completed_));
                        a.on_completed_ = nullptr;
                    }
                }
                break;
            default:
                break;
        }
    }
    const bool was_active = !animations_.empty();
    std::erase_if(animations_,
                  [](const Slot& slot)
                  {
                      return slot.animation->state_ == ConnectedAnimationState::Complete ||
                             slot.animation->state_ == ConnectedAnimationState::Canceled;
                  });
    for (Callback<void()>& done : completed)
    {
        done();
    }
    return was_active;
}

void ConnectedAnimationService::render(Renderer& renderer) const
{
    const Theme& theme = theme_ != nullptr ? *theme_ : Theme::default_light();
    for (const Slot& slot : animations_)
    {
        slot.animation->render(renderer, theme);
    }
}

} // namespace ui
