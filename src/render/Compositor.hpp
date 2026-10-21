// SPDX-License-Identifier: MIT
// The seam between a control's visual state and the system compositor, private to libwinui: the
// Win32 backend implements it with DirectComposition and the tests with TestCompositor.
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Duration.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/CubicBezier.hpp"
#include "winui/ui/Animation.hpp"

#include <optional>
#include <vector>

#include <cstdint>

namespace ui
{

class RenderRecording;

// A property of a compositor layer (Compositor::sync_layer()) that a LayerAnimation drives.
enum class LayerProperty : std::uint8_t
{
    OffsetX    = 0, // Horizontal translation in DIPs, on top of where the layer is placed.
    OffsetY    = 1, // Vertical translation in DIPs.
    ScaleX     = 2, // Horizontal scale factor around LayerAnimation::center.
    ScaleY     = 3, // Vertical scale factor around LayerAnimation::center.
    Rotation   = 4, // Rotation in degrees, clockwise, around LayerAnimation::center.
    Opacity    = 5, // Opacity, 0..1.
    ClipLeft   = 6, // Left edge of the layer's own animated clip, in DIPs from the layer's left.
    ClipTop    = 7, // Top edge of the animated clip, in DIPs from the layer's top.
    ClipRight  = 8, // Right edge of the animated clip, in DIPs from the layer's left.
    ClipBottom = 9, // Bottom edge of the animated clip, in DIPs from the layer's top.
};

// A key frame animation of one layer property, run by the compositor without the UI thread, once
// or forever (Compositor::animate_layer()). The property starts at the first frame's value, which
// a first frame at time 0 sets, and holds the last frame's value after it. A Discrete frame makes
// a jump.
//
// value_at() reads the same animation on the CPU, for a control that draws the motion itself when
// there is no compositor.
//
// Example:
// @code
// // A spinner: a full clockwise turn every 1.5 s, around the middle of a 32 DIP layer.
// const ui::LayerAnimation spin{.property    = ui::LayerProperty::Rotation,
//                               .key_frames  = {ui::KeyFrame::linear(0ms, 0.0f), ui::KeyFrame::linear(1500ms, 360.0f)},
//                               .center      = ui::Point{16.0f, 16.0f}};
// compositor.animate_layer(layer, spin);
// @endcode
struct LayerAnimation
{
    // The property animated.
    LayerProperty property = LayerProperty::OffsetX;
    // The frames, sorted by time.
    std::vector<KeyFrame> key_frames{};
    // The length of one cycle; 0 uses the last key time.
    Duration duration{};
    // How far into its first cycle the animation starts, so one started again continues where
    // the last one was.
    Duration phase{};
    // Repeat forever (the default), or play once and hold the last value.
    bool repeat = true;
    // The point that scale and rotation keep in place, in DIPs from the layer's top-left.
    Point center{};

    // The cycle length: `duration`, or the last key time when that is 0.
    [[nodiscard]] Duration cycle() const
    {
        return duration > Duration{} ? duration : key_frames_duration(key_frames);
    }

    // The property's value `elapsed` after the animation started, `phase` included.
    [[nodiscard]] float value_at(Duration elapsed) const
    {
        if (key_frames.empty())
        {
            return 0.0f;
        }
        const double length = cycle().count();
        double       t      = (elapsed + phase).count();
        if (repeat && length > 0.0)
        {
            t -= length * static_cast<double>(static_cast<long long>(t / length));
            t = t < 0.0 ? t + length : t;
        }
        return sample_key_frames(key_frames, key_frames.front().value, Duration{t});
    }
};

namespace detail
{

// Where a compositor layer sits. `bounds` is the area its content is drawn for and `clip` is the
// part of the window it may show in, whatever its animations do to it. An element describes its
// layers in its own coordinates (bounds()'s space), and the Window hands them to the compositor
// in window-client DIPs, with the clip of every ancestor applied.
//
// Example:
// @code
// // A bar that slides inside its track: drawn at the track's left end, clipped to the track.
// const ui::detail::LayerPlacement bar{.bounds = ui::Rect{track.x, track.y, 40.0f, track.height}, .clip = track};
// @endcode
struct LayerPlacement
{
    // The area the layer's content is drawn for, before any animation moves it.
    Rect bounds{};
    // What of the layer shows. nullopt is no clip of the layer's own.
    std::optional<Rect> clip{};

    // Memberwise equality.
    friend bool operator==(const LayerPlacement&, const LayerPlacement&) = default;
};

// The seam between a control's visual state and the system compositor: a state-transition
// animation (a hover fade) is handed off as a property animation that runs
// without per-frame UI-thread work, and a continuous one (a spinner) runs on a layer of its own.
//
// UI code calls this the same way it calls Renderer, except calls land on state *transitions*
// rather than every frame: Window calls Element::sync_composition() after layout and input,
// and a control compares its current state with the one it last composed. Each overlay visual
// is keyed by a VisualId (normally the element's own visual_id()). The host installs an
// implementation with Window::set_compositor() (DirectComposition on Win32); non-owning, and
// it may be absent.
//
// Example:
// @code
// void MyButton::sync_composition_override(ui::detail::Compositor& compositor, const ui::Theme& theme)
// {
//     const bool now_hot = hovered_ || pressed_;
//     // Geometry may change without the state changing; keep the overlay current regardless.
//     compositor.sync_visual(visual_id(), bounds(), theme.corner_radius, ui::colors::White);
//     if (now_hot == composed_hot_)
//     {
//         return; // only spend an animation on an actual flip
//     }
//     compositor.animate_opacity(visual_id(), composed_hot_ ? 0.1f : 0.0f, now_hot ? 0.1f : 0.0f,
//                                theme.anim_faster);
//     composed_hot_ = now_hot;
// }
// @endcode
class Compositor
{
public:
    virtual ~Compositor() = default;

    // (Re)establishes the overlay visual for `id` as a filled rounded rect covering
    // `bounds` in `color`, so a subsequent animate_opacity() has something to fade.
    // Idempotent: safe to call whenever geometry might have changed (e.g. after
    // layout), even if it hasn't. `bounds`' position is always applied immediately
    // (non-animated).
    virtual void sync_visual(VisualId id, const Rect& bounds, float corner_radius, const Color& color) = 0;

    // Animates the overlay's opacity from `from` to `to` over `duration`, run by the
    // compositor rather than the calling thread. Always linear, so there is no easing parameter
    // (see CubicBezier above).
    virtual void animate_opacity(VisualId id, float from, float to, Duration duration) = 0;

    // Drops the overlay visual (e.g. the owning control is being destroyed, or its wash would
    // show over something that covers the control).
    virtual void remove_visual(VisualId id) = 0;

    // ---- layers ----
    // A layer is a visual with content of its own: part of an element's drawing that the
    // compositor shows and animates instead of the window's frame. Window places, paints and
    // removes them (Element::layer_count()); the element only starts their animations. Layers sit
    // above the window's frame and below the overlay visuals. Every call below does nothing by
    // default, for a compositor without them.

    // Whether this compositor shows layers. False by default, and then Window draws everything
    // into its frame.
    [[nodiscard]] virtual bool supports_layers()
    {
        return false;
    }

    // Creates layer `id` or moves it to `placement` (window-client DIPs, `clip` always set). Its
    // running animations carry on.
    virtual void sync_layer(VisualId id, const LayerPlacement& placement)
    {
        (void)id;
        (void)placement;
    }

    // Replaces layer `id`'s content with `content`, drawn with (0,0) at the top-left of the
    // placement's bounds. Called again only when the content changes.
    virtual void paint_layer(VisualId id, const RenderRecording& content)
    {
        (void)id;
        (void)content;
    }

    // Runs `animation` on layer `id`, replacing any animation of the same property.
    virtual void animate_layer(VisualId id, const LayerAnimation& animation)
    {
        (void)id;
        (void)animation;
    }

    // Stops every animation on layer `id` and puts its properties back at rest.
    virtual void stop_layer_animations(VisualId id)
    {
        (void)id;
    }

    // Drops layer `id` with its animations.
    virtual void remove_layer(VisualId id)
    {
        (void)id;
    }

    // The compositor's clock, the time animations run on. Window times how long a layer ran with
    // it, so a control that takes its drawing back continues where the compositor was. Zero by
    // default.
    [[nodiscard]] virtual Duration now() const
    {
        return {};
    }
};

} // namespace detail

} // namespace ui
