// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/CubicBezier.hpp"
#include "winui/render/RenderRecording.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/ui/Animation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ui
{

/// The default flight path. The element dips 80 DIPs downward and back during a 300 ms move with
/// (0.8,0)-(0.2,1) cubic Bézier easing. When its size does not otherwise change, it scales up by
/// 10 % at the lowest point.
///
/// Example:
/// @code
/// void prepare_flight(ui::Element& source)
/// {
///     ui::ConnectedAnimation& animation =
///         window.connected_animation_service().prepare_to_animate("ForwardConnectedAnimation", source);
///     animation.set_flight_path(ui::ArcFlightPath{.shadow = false});
/// }
/// @endcode
struct ArcFlightPath
{
    bool shadow = true; ///< A shadow under the element as it flies.
};
/// A straight, decelerating 200 ms move with (0.1,0.9)-(0.2,1) easing. Use it for back
/// navigation.
struct ShortFlightPath
{
};
/// A straight move with the service's default duration and easing
/// (ConnectedAnimationService::default_duration(), default_easing()).
struct StraightFlightPath
{
};
/// One of the three paths a connected animation flies.
using FlightPath = std::variant<ArcFlightPath, ShortFlightPath, StraightFlightPath>;

class ConnectedAnimationService;

/// The stage a ConnectedAnimation is in.
enum class ConnectedAnimationState
{
    Prepared, ///< snapshot taken, waiting for try_start()
    Started,  ///< try_start() called, waiting for the destination's layout
    Running,  ///< flying; the destination is hidden
    Complete, ///< played to the end; on_completed() has fired
    Canceled, ///< cancel()ed, replaced by a new prepare, or timed out
};

/// A connected animation: one element flying from where it was (the source, snapshotted
/// by ConnectedAnimationService::prepare_to_animate()) to where its counterpart is (the
/// destination, named by try_start()), usually across a page navigation.
///
/// The service owns it, and you reach it by reference or pointer. It lives until it completes or
/// is canceled. One that is never started times out two seconds after being prepared. While it
/// runs, a crossfade of the two snapshots moves from the source's rect to the destination's, and
/// by default scales with it. It is drawn above the tree and below popups. The destination stays
/// hidden until the flight lands.
///
/// Example:
/// @code
/// // On the list page, just before navigating away:
/// void leave_list(ui::ImageControl& thumbnail)
/// {
///     window.connected_animation_service().prepare_to_animate("ForwardConnectedAnimation", thumbnail);
/// }
///
/// // On the detail page, once its tree is in the window:
/// void arrive_at_detail(ui::ImageControl& detail_image, ui::TextControl& detail_title)
/// {
///     ui::ConnectedAnimationService& service = window.connected_animation_service();
///     if (ui::ConnectedAnimation* animation = service.animation("ForwardConnectedAnimation"))
///     {
///         animation->set_flight_path(ui::ShortFlightPath{});
///         animation->on_completed([] { /* the destination is showing again */ });
///         animation->try_start(detail_image, {&detail_title}); // the title flies in alongside
///     }
/// }
/// @endcode
class ConnectedAnimation
{
public:
    /// Non-copyable: the service owns every animation.
    ConnectedAnimation(const ConnectedAnimation&) = delete;
    /// Non-copyable: the service owns every animation.
    ConnectedAnimation& operator=(const ConnectedAnimation&) = delete;

    /// Animate into `destination`, and fly the `coordinated` elements in alongside it. They start
    /// from where they would sit relative to the source and fade in. The flight begins after the
    /// next layout, when the destination is snapshotted and hidden. Returns false and does nothing
    /// when the animation was already started, canceled or timed out, or when the destination is
    /// the source itself. A destination destroyed before that layout, or laid out with zero size,
    /// cancels the animation.
    bool try_start(Element& destination, std::vector<Element*> coordinated = {});
    /// Drop it, snapshot and all; a hidden destination shows again.
    void cancel();

    /// The flight path; nullopt (the default) is an ArcFlightPath.
    [[nodiscard]] const std::optional<FlightPath>& flight_path() const
    {
        return flight_path_;
    }
    /// Set before try_start(); a running flight keeps the path it began with.
    void set_flight_path(std::optional<FlightPath> path)
    {
        flight_path_ = path;
    }
    /// True (the default) scales the snapshots from the source's size to the destination's.
    /// False keeps their own sizes, so they only move, centered on each other.
    [[nodiscard]] bool is_scale_animation_enabled() const
    {
        return scale_enabled_;
    }
    /// Set before try_start().
    void set_scale_animation_enabled(bool enabled)
    {
        scale_enabled_ = enabled;
    }
    /// Fired when the animation has played (not when it is canceled or times out).
    /// Replaces any previous handler.
    void on_completed(Callback<void()> handler)
    {
        on_completed_ = std::move(handler);
    }

    /// The current state; Prepared right after prepare_to_animate().
    [[nodiscard]] ConnectedAnimationState state() const
    {
        return state_;
    }
    /// The source's rect when it was prepared (window coordinates).
    [[nodiscard]] const Rect& source_rect() const
    {
        return source_rect_;
    }
    /// The destination's rect once running (window coordinates); empty before that.
    [[nodiscard]] const Rect& destination_rect() const
    {
        return destination_rect_;
    }

private:
    friend class ConnectedAnimationService;
    explicit ConnectedAnimation(ConnectedAnimationService& service) : service_(&service)
    {
    }

    struct Flight
    {
        std::unique_ptr<ElementWatch> element; // the destination (or a coordinated element)
        RenderRecording               snapshot;
        Rect                          from{};
        Rect                          to{};
        bool                          coordinated = false;
    };

    void begin(const Theme& theme);                            // Started -> Running, after layout
    void finish(bool completed);                               // Running/any -> Complete/Canceled
    void render(Renderer& renderer, const Theme& theme) const; // the host visual

    ConnectedAnimationService*    service_;
    ConnectedAnimationState       state_ = ConnectedAnimationState::Prepared;
    RenderRecording               source_snapshot_;
    Rect                          source_rect_{};
    Rect                          destination_rect_{};
    std::vector<Flight>           flights_; // [0] the destination, then coordinated ones
    std::vector<Element*>         pending_coordinated_;
    std::unique_ptr<ElementWatch> pending_destination_;
    std::optional<FlightPath>     flight_path_;
    bool                          scale_enabled_ = true;
    double                        elapsed_       = 0.0; // since prepared, then since running
    double                        duration_      = 300.0;
    EasingFunction                easing_{};
    Callback<void()>              on_completed_;
};

/// The per-window registry of connected animations, keyed by name, and the layer that draws
/// them.
///
/// Each Window owns one (Window::connected_animation_service()) and runs and draws its
/// animations. An app only prepares animations and looks them up. Keys are strings the app
/// chooses, and both pages use the same key.
///
/// Example:
/// @code
/// // On the list page, just before navigating away:
/// void leave_list(ui::ImageControl& thumbnail)
/// {
///     window.connected_animation_service().prepare_to_animate("ForwardConnectedAnimation", thumbnail);
/// }
///
/// // On the detail page, once its tree is in the window:
/// void arrive_at_detail(ui::ImageControl& detail_image, ui::TextControl& detail_title)
/// {
///     ui::ConnectedAnimationService& service = window.connected_animation_service();
///     if (ui::ConnectedAnimation* animation = service.animation("ForwardConnectedAnimation"))
///     {
///         animation->set_flight_path(ui::ShortFlightPath{});
///         animation->on_completed([] { /* the destination is showing again */ });
///         animation->try_start(detail_image, {&detail_title}); // the title flies in alongside
///     }
/// }
/// @endcode
class ConnectedAnimationService
{
public:
    /// An empty service; a Window constructs its own.
    ConnectedAnimationService() = default;
    /// Non-copyable: animations point back at their service.
    ConnectedAnimationService(const ConnectedAnimationService&) = delete;
    /// Non-copyable: animations point back at their service.
    ConnectedAnimationService& operator=(const ConnectedAnimationService&) = delete;

    /// Snapshot `source` now, under `key`, and cancel any animation already prepared under it.
    /// The returned animation lives until it completes, is canceled or times out, and animation()
    /// finds it until then.
    ConnectedAnimation& prepare_to_animate(std::string key, Element& source);
    /// The animation prepared under `key` and not yet finished, or nullptr.
    [[nodiscard]] ConnectedAnimation* animation(const std::string& key) const;

    /// The duration ArcFlightPath and StraightFlightPath use (300 ms by default).
    [[nodiscard]] Duration default_duration() const
    {
        return default_duration_;
    }
    /// Applies to animations that start afterwards.
    void set_default_duration(Duration duration)
    {
        default_duration_ = duration;
    }
    /// The easing ArcFlightPath and StraightFlightPath use, a (0.8,0)-(0.2,1) cubic Bézier by default.
    [[nodiscard]] const EasingFunction& default_easing() const
    {
        return default_easing_;
    }
    /// Applies to animations that start afterwards.
    void set_default_easing(EasingFunction easing)
    {
        default_easing_ = easing;
    }

    /// Whether any animation is prepared or running, so something may be on screen.
    [[nodiscard]] bool active() const
    {
        return !animations_.empty();
    }

private:
    friend class ConnectedAnimation;
    friend class Window; // drives the four below

    // The theme snapshots are rendered with; not owned.
    void set_theme(const Theme* theme)
    {
        theme_ = theme;
    }
    // After every layout: started animations snapshot their destinations and begin.
    void after_layout();
    // Advance time; true while anything is on screen (the window keeps repainting).
    bool tick(Duration delta);
    // Draw every prepared and running animation.
    void render(Renderer& renderer) const;

    struct Slot
    {
        std::string                         key;
        std::unique_ptr<ConnectedAnimation> animation;
    };
    std::vector<Slot> animations_;
    const Theme*      theme_ = nullptr;
    Duration          default_duration_{300.0};
    EasingFunction    default_easing_ = CubicBezier{0.8f, 0.0f, 0.2f, 1.0f};
};

} // namespace ui
