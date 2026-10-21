// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/AnimatedVisualSource.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/ui/Accessibility.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panels/Panel.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>

namespace ui
{

/// Plays a caller-supplied AnimatedVisualSource by advancing its progress over time and drawing
/// each frame with the source's render_frame().
///
/// With auto_play() on (the default), a newly loaded source starts looping from 0 to 1 at once.
/// Otherwise the app calls play(), pause(), resume(), stop() or set_progress(). A play from `from`
/// to `to` lasts `duration() * (to - from)`, scaled by playback_rate() while it runs. A negative
/// rate plays backwards. The source is fitted into the player's bounds as stretch() says. When the
/// source can't be shown (its `available()` is false), the fallback content takes its place. The
/// player doesn't own the source, so it must outlive the player.
///
/// A play keeps running in real time while the player is off screen, scrolled out of view or
/// clipped away, but nothing is drawn and the window isn't kept busy for it. Back on screen, the
/// player shows the progress the play has reached by then, with a looped play wrapped around. A
/// play that ends off screen still completes on time.
///
/// Example:
/// @code
/// // A source drawing a circle that grows over one second.
/// struct Pulse final : ui::AnimatedVisualSource
/// {
///     ui::Size natural_size() const override { return {48.0f, 48.0f}; }
///     ui::Duration duration() const override { return ui::Duration{1000.0}; }
///     void render_frame(ui::Renderer& r, const ui::Rect& b, double progress) const override
///     {
///         const float radius = static_cast<float>(progress) * b.width / 2.0f;
///         r.fill_ellipse(b.center(), radius, radius, ui::colors::Black);
///     }
/// };
/// static const Pulse pulse; // outlives every player showing it
///
/// ui::Control<ui::AnimatedVisualPlayer> player;
/// window.set_content(ui::VerticalStack{
///     .content = {
///         ui::AnimatedVisualPlayer{.source = &pulse, .auto_play = false, .width = 96.0f,
///                                  .height = 96.0f, .control = player,
///                                  .fallback_content = ui::Text{.text = "(no animation)"}},
///         ui::Button{.text = "Play once", .on_click = [&] { player->play(0.0, 1.0, false); }},
///         ui::Button{.text = "Pause", .on_click = [&] { player->pause(); }},
///     },
/// });
/// @endcode
class AnimatedVisualPlayerControl final : public Panel
{
public:
    /// A player with no source: auto-play on, rate 1, Uniform stretch.
    AnimatedVisualPlayerControl() = default;

    /// The source being played, or null.
    [[nodiscard]] const AnimatedVisualSource* source() const
    {
        return source_;
    }
    /// Sets the source to play. The player doesn't own it, so the source must outlive the player.
    /// Replacing the source stops whatever was playing, and auto-play then starts on the new one.
    /// nullptr unloads it. Shows or hides the fallback content as the source's `available()` says.
    void set_source(const AnimatedVisualSource* source);

    /// A source is set, available and has a size.
    [[nodiscard]] bool is_animated_visual_loaded() const
    {
        return loaded_;
    }

    /// Sets what shows instead when the source can't be shown. Takes ownership, returns a
    /// non-owning observer and replaces any earlier fallback.
    Element* set_fallback_content(std::unique_ptr<Element> content);
    /// The fallback content (whether or not it is showing), or null if none was set.
    [[nodiscard]] Element* fallback_content() const
    {
        return fallen_back_ ? (children_.empty() ? nullptr : children_.front().get()) : fallback_.get();
    }
    /// Whether the fallback content is what is showing.
    [[nodiscard]] bool is_fallen_back() const
    {
        return fallen_back_;
    }

    /// Whether a loaded source starts looping 0..1 on its own. Default true.
    [[nodiscard]] bool auto_play() const
    {
        return auto_play_;
    }
    /// Turns auto-play on or off. Turning it on while a source is loaded and nothing plays starts
    /// the looping play at once.
    void set_auto_play(bool auto_play);

    /// How fast plays run, 1 by default. A negative rate plays backwards.
    [[nodiscard]] double playback_rate() const
    {
        return playback_rate_;
    }
    /// Sets the playback rate. A running play speeds up, slows down or reverses at once.
    void set_playback_rate(double rate)
    {
        playback_rate_ = rate;
    }

    /// How the source's natural size fits the player's bounds. Uniform by default.
    [[nodiscard]] Stretch stretch() const
    {
        return stretch_;
    }
    /// Sets the stretch. Updates layout and redraws.
    void set_stretch(Stretch stretch)
    {
        stretch_ = stretch;
        mark_dirty(DirtyFlags::Layout | DirtyFlags::Visual);
    }

    /// Whether a play has started and not completed. A paused play counts as playing.
    [[nodiscard]] bool is_playing() const
    {
        return playing_;
    }
    /// Whether the current play is paused.
    [[nodiscard]] bool is_paused() const
    {
        return playing_ && paused_;
    }
    /// The current position in the animation, from 0 to 1.
    [[nodiscard]] double progress() const
    {
        return progress_;
    }
    /// 0 until a source is loaded, then the source's duration().
    [[nodiscard]] Duration duration() const;

    /// Completes any play in progress, rewinding it, then plays from `from_progress` to
    /// `to_progress`, repeating forever when `looped` is true. Without a loaded source the play
    /// waits and doesn't start, and setting a source cancels it.
    void play(double from_progress, double to_progress, bool looped);
    /// Pauses the current play where it is. Does nothing when nothing is playing.
    void pause();
    /// Continues a paused play. Does nothing when nothing is playing.
    void resume();
    /// Completes the current play and rewinds progress() to 0. Does nothing when nothing is playing.
    void stop();
    /// Completes any play and jumps to `progress`, clamped to [0,1].
    void set_progress(double progress);
    /// When the player comes back on screen, draws the progress the play reached while away.
    void on_screen_changed(bool on_screen) override;

    // ---- automation ----
    /// Exposed to UI Automation as an image.
    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Image;
    }

protected:
    /// Fits the source's natural size into `available` as stretch() says, or measures the fallback.
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override;
    /// Arranges the fallback content when it is showing.
    void arrange_override(const Rect& final_rect, const ArrangeContext& context) override;
    /// Draws the source's frame at progress(), clipped for UniformToFill, or draws the fallback.
    void render_override(Renderer& renderer, const RenderContext& context) const override;
    /// Advances the current play by `delta * playback_rate()`, and completes or loops it at its
    /// ends. Off screen it draws nothing and keeps the window idle, and a play that ends there
    /// still completes on time.
    void tick_override(Duration delta) override;


private:
    void                 show_fallback(bool fallen_back);
    void                 start_play();
    void                 complete_play();
    [[nodiscard]] double progress_at(double time) const;

    const AnimatedVisualSource* source_ = nullptr;
    std::unique_ptr<Element>    fallback_; // the fallback content while it is not showing
    bool                        loaded_      = false;
    bool                        fallen_back_ = false;

    bool    auto_play_     = true;
    double  playback_rate_ = 1.0;
    Stretch stretch_       = Stretch::Uniform;

    // What is playing now. `pending_` is a play requested before a source was loaded, `playing_`
    // one that has started.
    bool   pending_       = false;
    bool   playing_       = false;
    bool   paused_        = false;
    bool   looped_        = false;
    double from_progress_ = 0.0;
    double to_progress_   = 1.0;
    double play_ms_       = 0.0; // the play's length at rate 1
    double time_          = 0.0; // position in the play, 0 at its start and 1 at its end
    double progress_      = 0.0;
};

/// A player for a caller-supplied AnimatedVisualSource (render/AnimatedVisualSource.hpp). Builds a
/// ui::AnimatedVisualPlayerControl.
///
/// `source` is a non-owning pointer, so the AnimatedVisualSource must outlive the built control.
/// Changing the source, through a binding or set_source() on the control, stops the player.
/// `fallback_content` shows instead when the source can't be shown.
///
/// Example:
/// @code
/// extern const ui::AnimatedVisualSource& logo; // the app's own subclass, alive as long as the window
/// ui::Control<ui::AnimatedVisualPlayer> player;
///
/// window.set_content(ui::VerticalStack{.spacing = 8, .content = {
///     ui::AnimatedVisualPlayer{.source           = &logo,
///                              .auto_play        = false,
///                              .width            = 400.0f,
///                              .height           = 400.0f,
///                              .control          = player,
///                              .fallback_content = ui::Text{.text = "Animation unavailable"}},
///     ui::HorizontalStack{.spacing = 8, .content = {
///         ui::Button{.text = "Play",  .on_click = [&] { player->play(0.0, 1.0, true); }},
///         ui::Button{.text = "Pause", .on_click = [&] { player->pause(); }},
///         ui::Button{.text = "Stop",  .on_click = [&] { player->stop(); }},
///     }},
/// }});
/// @endcode
struct AnimatedVisualPlayer
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The control this spec builds; what `ui::Control<AnimatedVisualPlayer>` points at.
    using control_type = AnimatedVisualPlayerControl;

    // ---- properties ----
    /// The animation. Not owned, so it must outlive the built control.
    Binding<const AnimatedVisualSource*> source{};
    /// Whether the whole animation loops as soon as the source loads.
    Binding<bool> auto_play{true};
    /// Playback speed. 1 is normal speed, and a negative rate plays backwards.
    Binding<double> playback_rate{1.0};
    /// How the animation fills the player's box.
    Binding<Stretch> stretch{Stretch::Uniform};

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
    ControlOut<AnimatedVisualPlayer> control{}; ///< Out: the handle that receives the built control.

    // ---- children ----
    /// Shown instead of the animation when the source can't be shown.
    Spec fallback_content{};

    /// Consumes the spec and returns the built ui::AnimatedVisualPlayerControl; the caller owns it.
    [[nodiscard]] std::unique_ptr<Element> build() &&;
};

} // namespace ui
