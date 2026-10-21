// SPDX-License-Identifier: MIT
#pragma once

#include "../render/Compositor.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Duration.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/RenderRecording.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Animation.hpp"

#include <utility>
#include <variant>
#include <vector>

#include <cstddef>

namespace ui::testing
{

// Recorded Compositor::sync_visual() call: the overlay visual `id` was (re)placed at `bounds`
// (DIPs) as a rounded rect of `corner_radius` filled with `color`.
struct SyncVisual
{
    // Overlay visual the call targeted.
    VisualId id;
    // Final, non-animated bounds in DIPs.
    Rect bounds;
    // Corner radius in DIPs; 0 is a square rect.
    float corner_radius = 0.0f;
    // Fill of the overlay (usually a theme hover/press tint).
    Color color;

    // Memberwise equality, so a whole command can be compared in one `REQUIRE`.
    friend bool operator==(const SyncVisual&, const SyncVisual&) = default;
};

// Recorded Compositor::animate_opacity() call: a linear fade of visual `id` from `from` to `to`
// (both 0..1) over `duration`.
struct AnimateOpacity
{
    // Overlay visual being faded.
    VisualId id;
    // Start opacity, 0..1.
    float from = 0.0f;
    // End opacity, 0..1.
    float to = 0.0f;
    // Length of the fade.
    Duration duration{};

    // Memberwise equality.
    friend bool operator==(const AnimateOpacity&, const AnimateOpacity&) = default;
};

// Recorded Compositor::remove_visual() call: visual `id` was dropped.
struct RemoveVisual
{
    // Visual that was removed.
    VisualId id;

    // Memberwise equality.
    friend bool operator==(const RemoveVisual&, const RemoveVisual&) = default;
};

// Recorded Compositor::sync_layer() call: layer `id` was created or moved to `placement`.
struct SyncLayer
{
    // Layer the call targeted.
    VisualId id;
    // Where the layer sits, window-client DIPs, with its clip.
    detail::LayerPlacement placement;

    // Memberwise equality.
    friend bool operator==(const SyncLayer&, const SyncLayer&) = default;
};

// Recorded Compositor::paint_layer() call: layer `id` got new content.
struct PaintLayer
{
    // Layer the call targeted.
    VisualId id;
    // The content, replayed into a TestRenderer: (0,0) is the layer's top-left.
    std::vector<RenderCommand> commands;
};

// Recorded Compositor::animate_layer() call.
struct AnimateLayer
{
    // Layer being animated.
    VisualId id;
    // The animation, as the caller described it.
    LayerAnimation animation;
};

// Recorded Compositor::stop_layer_animations() call.
struct StopLayerAnimations
{
    // Layer whose animations stopped.
    VisualId id;

    // Memberwise equality.
    friend bool operator==(const StopLayerAnimations&, const StopLayerAnimations&) = default;
};

// Recorded Compositor::remove_layer() call.
struct RemoveLayer
{
    // Layer that was removed.
    VisualId id;

    // Memberwise equality.
    friend bool operator==(const RemoveLayer&, const RemoveLayer&) = default;
};

// One recorded Compositor call; inspect with `std::get_if` / `std::holds_alternative`. The
// variant grows with the Compositor, so a visit needs a catch-all case.
using CompositionCommand = std::
    variant<SyncVisual, AnimateOpacity, RemoveVisual, SyncLayer, PaintLayer, AnimateLayer, StopLayerAnimations, RemoveLayer>;

// Compositor test double that records every call, in order, instead of driving
// DirectComposition.
//
// It is to Compositor what TestRenderer is to Renderer: hand it to
// ElementAccess::sync_composition() (or WindowSeams::set_compositor()) and assert on commands()
// afterwards. Nothing is animated: each call just appends one CompositionCommand, and the clock
// (now()) moves only when advance() moves it. It supports layers unless told otherwise.
//
// Example:
// @code
// ui::ButtonControl button{"Hello"};
// button.arrange({100, 100, 100, 40});
// ui::testing::TestCompositor compositor;
// ui::Theme          theme = ui::Theme::light();
//
// button.handle_pointer({{150, 120}, ui::PointerButton::None, ui::PointerAction::Move});
// ui::testing::ElementAccess::sync_composition(button, compositor, theme); // hover starts: sync + fade in
//
// const ui::testing::AnimateOpacity* fade = nullptr;
// for (const auto& cmd : compositor.commands())
//     if (const auto* a = std::get_if<ui::testing::AnimateOpacity>(&cmd))
//         fade = a;
// REQUIRE(fade != nullptr);
// REQUIRE(fade->from == 0.0f);
// REQUIRE(fade->to > 0.0f);
// @endcode
class TestCompositor final : public detail::Compositor
{
public:
    // Appends a SyncVisual.
    void sync_visual(VisualId id, const Rect& bounds, float corner_radius, const Color& color) override
    {
        commands_.push_back(SyncVisual{id, bounds, corner_radius, color});
    }

    // Appends an AnimateOpacity.
    void animate_opacity(VisualId id, float from, float to, Duration duration) override
    {
        commands_.push_back(AnimateOpacity{id, from, to, duration});
    }

    // Appends a RemoveVisual.
    void remove_visual(VisualId id) override
    {
        commands_.push_back(RemoveVisual{id});
    }

    // True unless set_supports_layers(false) said otherwise.
    [[nodiscard]] bool supports_layers() override
    {
        return supports_layers_;
    }
    // Makes the compositor one without layers, so Window draws everything into its frame.
    void set_supports_layers(bool supports)
    {
        supports_layers_ = supports;
    }

    // Appends a SyncLayer.
    void sync_layer(VisualId id, const detail::LayerPlacement& placement) override
    {
        commands_.push_back(SyncLayer{id, placement});
    }

    // Appends a PaintLayer with the content replayed into a TestRenderer.
    void paint_layer(VisualId id, const RenderRecording& content) override
    {
        TestRenderer replayed;
        content.replay(replayed);
        commands_.push_back(PaintLayer{id, std::move(replayed.commands)});
    }

    // Appends an AnimateLayer.
    void animate_layer(VisualId id, const LayerAnimation& animation) override
    {
        commands_.push_back(AnimateLayer{id, animation});
    }

    // Appends a StopLayerAnimations.
    void stop_layer_animations(VisualId id) override
    {
        commands_.push_back(StopLayerAnimations{id});
    }

    // Appends a RemoveLayer.
    void remove_layer(VisualId id) override
    {
        commands_.push_back(RemoveLayer{id});
    }

    // The clock advance() moves; zero at first.
    [[nodiscard]] Duration now() const override
    {
        return now_;
    }
    // Moves the clock on by `time`, as if the compositor had run for that long.
    void advance(Duration time)
    {
        now_ += time;
    }

    // How many recorded calls are an `T` (one of the CompositionCommand alternatives).
    template <typename T>
    [[nodiscard]] std::size_t count_of() const
    {
        std::size_t n = 0;
        for (const CompositionCommand& command : commands_)
        {
            n += std::holds_alternative<T>(command) ? 1 : 0;
        }
        return n;
    }

    // Every call recorded so far, oldest first. Never cleared; construct a fresh
    // TestCompositor per scenario.
    [[nodiscard]] const std::vector<CompositionCommand>& commands() const
    {
        return commands_;
    }
    // Number of recorded calls (`commands().size()`).
    [[nodiscard]] std::size_t count() const
    {
        return commands_.size();
    }

private:
    std::vector<CompositionCommand> commands_;
    bool                            supports_layers_ = true;
    Duration                        now_{};
};

} // namespace ui::testing
