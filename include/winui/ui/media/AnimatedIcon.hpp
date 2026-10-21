// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/AnimatedVisualSource.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/BackgroundBrush.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ui
{

/// What a new state does while an AnimatedIcon transition is still playing.
enum class AnimatedIconQueueBehavior
{
    Cut,             ///< a new state interrupts whatever is playing
    QueueOne,        ///< the default: queue it behind the running transition
    SpeedUpQueueOne, ///< queue it, and play the running and queued transitions 7x faster
};

/// An icon that animates between named visual states by playing segments of an
/// AnimatedIconSource. It draws a fallback icon when it has no source it can show.
///
/// Going from state A to B plays the source's segment between the `AToB_Start` and `AToB_End`
/// markers. Without those it looks for an `AToB` or `B` marker, then any marker named `ToB_End`.
/// A numeric state name is read as a progress value. A segment plays for
/// `duration * |to - from|`, and one shorter than 20 ms jumps straight to its end. A state set while
/// a transition plays waits or interrupts as queue_behavior() says. The icon scales the source's
/// natural size uniformly and is centered in its slot. App code or the owning control (such as
/// NavigationShell for its items) calls set_state(). The icon doesn't own the source.
///
/// Example:
/// @code
/// static const ui::AnimatedFindVisualSource find; // stateless; one instance serves every icon
/// ui::Control<ui::AnimatedIcon> icon;
///
/// window.set_content(ui::Button{
///     .width              = 75.0f,
///     .accessibility      = {.name = "Search"},
///     .on_pointer_entered = [&] { icon->set_state("PointerOver"); },
///     .on_pointer_exited  = [&] { icon->set_state("Normal"); },
///     .content            = ui::AnimatedIcon{.source   = &find,
///                                           .fallback = ui::symbol_glyph(ui::Symbol::Find),
///                                           .state    = std::string{"Normal"},
///                                           .control  = icon},
/// });
///
/// // Imperative form of the same icon:
/// auto plain = std::make_unique<ui::AnimatedIconControl>();
/// plain->set_source(&find);
/// plain->set_state("Normal");
/// @endcode
class AnimatedIconControl final : public Element
{
public:
    /// An icon with no source and no fallback: measures to nothing and draws nothing.
    AnimatedIconControl() = default;

    /// The animated source being shown, or null (then the fallback glyph is drawn, if any).
    [[nodiscard]] const AnimatedIconSource* source() const
    {
        return source_;
    }
    /// Sets the animated source. The icon doesn't own it, so it must outlive the icon. The current
    /// progress carries over to the new source. Updates layout and redraws.
    void set_source(const AnimatedIconSource* source)
    {
        source_ = source;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The icon drawn while there is no source that can be shown (none set, or its `available()`
    /// is false): a glyph at its own size, anything else fitted into the icon's box. Empty by
    /// default.
    [[nodiscard]] const IconSource& fallback() const
    {
        return fallback_;
    }
    /// Sets the fallback icon. Updates layout and redraws.
    void set_fallback(IconSource icon)
    {
        fallback_ = std::move(icon);
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// The icon's color: a fixed color or a theme resource. Empty, the default, uses the theme's
    /// text color.
    [[nodiscard]] const Paint& foreground() const
    {
        return foreground_;
    }
    /// Sets the color the source draws in. Empty goes back to the theme's text color. Redraws.
    void set_foreground(Paint color)
    {
        foreground_ = color;
        mark_dirty(DirtyFlags::Visual);
    }

    /// The state the icon was last asked for.
    [[nodiscard]] const std::string& state() const
    {
        return state_;
    }
    /// Moves the icon to `state`, playing the source's transition into it (see the class
    /// description). Setting the state it already has does nothing.
    void set_state(std::string state);

    /// The state the last transition went to, which the next one starts from.
    [[nodiscard]] const std::string& current_state() const
    {
        return current_state_;
    }

    /// The source's normalized progress (0..1) currently shown.
    [[nodiscard]] double progress() const
    {
        return progress_;
    }
    /// True while a transition segment is animating (tick_override() advances it).
    [[nodiscard]] bool is_playing() const
    {
        return playing_;
    }

    /// What a set_state() does while a transition plays. QueueOne by default.
    [[nodiscard]] AnimatedIconQueueBehavior queue_behavior() const
    {
        return queue_behavior_;
    }
    /// Sets the queue behavior for subsequent set_state() calls.
    void set_queue_behavior(AnimatedIconQueueBehavior behavior)
    {
        queue_behavior_ = behavior;
    }

    /// The segment name the last transition resolved to, for example "NormalToPointerOver". Meant
    /// for tests.
    [[nodiscard]] const std::string& last_segment() const
    {
        return last_segment_;
    }
    /// The start marker the last transition used (empty if it had none).
    [[nodiscard]] const std::string& last_segment_start() const
    {
        return last_segment_start_;
    }
    /// The end marker the last transition used (empty if it had none).
    [[nodiscard]] const std::string& last_segment_end() const
    {
        return last_segment_end_;
    }
    /// Draws the icon like render(), in `foreground` instead of its own color. For a control that
    /// owns the icon and colors it from its own visual state.
    void render_in(Renderer& renderer, const Theme& theme, const Color& foreground) const;

    /// Exposed to UI Automation as an image.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Image;
    }

protected:
    /// Uniform-scales the source's natural size into `available`, or measures the fallback glyph.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Draws the source's current frame, or the fallback, in foreground() or the theme text color.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Advances a playing segment by `delta`, then starts the next queued state when it ends.
    void tick_override(Duration delta) override;


private:
    [[nodiscard]] bool showing_source() const
    {
        return source_ != nullptr && source_->available();
    }
    void transition_and_update(const std::string& from, const std::string& to, double multiplier = 1.0);
    // `cleanup` updates the state and pops the queue once the transition ends, or is null. A
    // segment too short to animate runs it before completing.
    void transition(const std::string& from, const std::string& to, double multiplier, Action* cleanup = nullptr);
    void play_segment(std::optional<double> from, double to, double multiplier, Action* cleanup = nullptr);
    void on_animation_completed();

    const AnimatedIconSource* source_ = nullptr;
    IconSource                fallback_{};
    Paint                     foreground_{};
    std::string               state_{};

    std::string               current_state_{};
    std::string               previous_state_{};
    std::deque<std::string>   queued_{};
    AnimatedIconQueueBehavior queue_behavior_ = AnimatedIconQueueBehavior::QueueOne;
    bool                      speed_up_       = false;

    double progress_                = 0.0;
    double previous_segment_length_ = 1.0;
    bool   playing_                 = false;
    double from_                    = 0.0;
    double to_                      = 0.0;
    double duration_ms_             = 0.0;
    double elapsed_ms_              = 0.0;

    std::string last_segment_{};
    std::string last_segment_start_{};
    std::string last_segment_end_{};
};

/// An icon that plays a caller-supplied AnimatedIconSource as its state changes, and draws a
/// fallback icon when there is no source. Builds a ui::AnimatedIconControl.
///
/// Example:
/// @code
/// static const ui::AnimatedFindVisualSource find; // stateless; one instance serves every icon
/// ui::Property<std::string> state{"Normal"};
///
/// window.set_content(ui::Button{
///     .width              = 75.0f,
///     .accessibility      = {.name = "Search"},
///     .on_pointer_entered = [&] { state = "PointerOver"; },
///     .on_pointer_exited  = [&] { state = "Normal"; },
///     .content            = ui::AnimatedIcon{.source   = &find,
///                                            .fallback = ui::symbol_glyph(ui::Symbol::Find),
///                                            .state    = state},
/// });
/// @endcode
struct AnimatedIcon
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<AnimatedIcon>` points at.
    using control_type = AnimatedIconControl;

    // ---- properties ----
    /// The animation. Not owned, so it must outlive the built control. Null draws the fallback.
    Binding<const AnimatedIconSource*> source{};
    /// The icon drawn while there is no source: a Symbol, a glyph, a path or a picture.
    Binding<IconSource> fallback{};
    /// The visual state, such as "Normal", "PointerOver" or "Pressed". Each change plays the
    /// source's transition segment from the old state to the new one.
    Binding<std::string> state{};
    /// What a change of `.state` does while a transition still plays. QueueOne by default.
    Binding<AnimatedIconQueueBehavior> queue_behavior{AnimatedIconQueueBehavior::QueueOne};
    /// The icon's color: a fixed color or a theme resource. Unset uses the theme's text color.
    Binding<Paint> foreground{};

    // ---- appearance / layout ----
    Style                             style{};                ///< Named style; explicit fields win.
    Binding<std::optional<Thickness>> margin{};               ///< Outer margin, DIPs.
    Binding<std::optional<float>>     width{};                ///< Width, DIPs; unset is Auto.
    Binding<std::optional<float>>     height{};               ///< Height, DIPs; unset is Auto.
    Binding<std::optional<float>>     min_width{};            ///< Min width, DIPs; unset is none.
    Binding<std::optional<float>>     min_height{};           ///< Min height, DIPs; unset is none.
    Binding<std::optional<float>>     max_width{};            ///< Max width, DIPs; unset is no limit.
    Binding<std::optional<float>>     max_height{};           ///< Max height, DIPs; unset is no limit.
    Binding<std::optional<Alignment>> horizontal_alignment{}; ///< Horizontal alignment in its slot.
    Binding<std::optional<Alignment>> vertical_alignment{};   ///< Vertical alignment in its slot.
    Binding<bool>                     visible{true};          ///< Shown; false collapses it (no room, no input).
    Binding<std::string>              tool_tip{}; ///< Shown when the pointer rests on it (Element::set_tool_tip()).
    Accessibility                     accessibility{}; ///< Screen reader name and description, and a test id.

    // ---- behavior ----
    /// The pointer moved onto the control.
    Action on_pointer_entered{};
    /// The pointer left the control.
    Action on_pointer_exited{};
    /// Keyboard focus arrived on the control, or on anything inside it, from outside it. A
    /// control with nothing focusable in it never gets focus, and never raises this.
    Action on_got_focus{};
    /// Keyboard focus left the control and everything inside it.
    Action on_lost_focus{};
    /// A key pressed while focus is on the control or inside it, before the control's own
    /// handling; ui::Handled stops it there (Element::on_key_down()).
    Callback<Handling(const KeyEvent&)> on_key_down{};
    /// A wheel turn over the control, before the control scrolls; ui::Handled stops it there
    /// (Element::on_wheel()).
    Callback<Handling(const PointerEvent&)> on_wheel{};

    // ---- identity ----
    ControlOut<AnimatedIcon> control{}; ///< Out: the handle that receives the built control.

    /// Consumes the spec and returns the built ui::AnimatedIconControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
