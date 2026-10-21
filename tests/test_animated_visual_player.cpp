// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/AnimatedVisualSource.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/media/AnimatedVisualPlayer.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/panels/Spacer.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
class FakeVisualSource final : public AnimatedVisualSource
{
public:
    FakeVisualSource(Size size, double duration_ms) : size_(size), duration_ms_(duration_ms)
    {
    }

    [[nodiscard]] Size natural_size() const override
    {
        return size_;
    }
    [[nodiscard]] Duration duration() const override
    {
        return Duration{duration_ms_};
    }
    void render_frame(Renderer& renderer, const Rect& bounds, double progress) const override
    {
        last_bounds_   = bounds;
        last_progress_ = progress;
        renderer.fill_rounded_rect(bounds, 0.0f, colors::Black);
    }

    [[nodiscard]] const Rect& last_bounds() const
    {
        return last_bounds_;
    }
    [[nodiscard]] double last_progress() const
    {
        return last_progress_;
    }

private:
    Size   size_;
    double duration_ms_;

    mutable Rect   last_bounds_{};
    mutable double last_progress_ = -1.0;
};
} // namespace

TEST_CASE("A default-constructed AnimatedVisualPlayer has no source and plays nothing", "[animated-visual-player]")
{
    AnimatedVisualPlayerControl player;
    REQUIRE(player.source() == nullptr);
    REQUIRE(player.auto_play());
    REQUIRE_THAT(player.playback_rate(), WithinAbs(1.0, 1e-9));
    REQUIRE(player.stretch() == Stretch::Uniform);
    REQUIRE_FALSE(player.is_playing());
    REQUIRE_THAT(player.progress(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(player.duration().count(), WithinAbs(0.0, 1e-9));

    HeadlessTextMeasurer measurer;
    const Theme          theme   = Theme::light();
    const Size           desired = player.measure({Unbounded, Unbounded}, measurer, theme);
    REQUIRE(desired.width == 0.0f);
    REQUIRE(desired.height == 0.0f);

    player.arrange(Rect{0, 0, 0, 0});
    TestRenderer renderer;
    player.render(renderer, theme);
    REQUIRE(renderer.count() == 0);
}

TEST_CASE("set_source with auto_play starts looped playback from progress 0 immediately", "[animated-visual-player]")
{
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_source(&source);

    REQUIRE(player.source() == &source);
    REQUIRE(player.is_playing());
    REQUIRE_THAT(player.progress(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(player.duration().count(), WithinAbs(1000.0, 1e-9));
}

TEST_CASE("auto_play off leaves a freshly attached source paused at progress 0", "[animated-visual-player]")
{
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_auto_play(false);
    player.set_source(&source);

    REQUIRE_FALSE(player.is_playing());
    REQUIRE_THAT(player.progress(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("on_tick advances progress by delta/duration and wraps round on overrun", "[animated-visual-player][tick]")
{
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player; // auto_play defaults to true: playing [0,1], looped

    player.set_source(&source);
    player.tick(500ms);
    REQUIRE_THAT(player.progress(), WithinAbs(0.5, 1e-6));
    REQUIRE(player.is_playing());

    player.tick(600ms); // 0.5 + 0.6 = 1.1: the looping animation carries on at 0.1
    REQUIRE_THAT(player.progress(), WithinAbs(0.1, 1e-6));
    REQUIRE(player.is_playing());
}

TEST_CASE("play(from, to, looped=false) stops at to_progress without wrapping", "[animated-visual-player][tick]")
{
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_auto_play(false);
    player.set_source(&source);

    player.play(0.25, 0.75, /*looped=*/false);
    REQUIRE(player.is_playing());
    REQUIRE_THAT(player.progress(), WithinAbs(0.25, 1e-9));

    player.tick(500ms); // half the full [0,1] duration -> +0.5 progress
    REQUIRE_THAT(player.progress(), WithinAbs(0.75, 1e-6));
    REQUIRE_FALSE(player.is_playing()); // landed on to_progress, non-looped: stops
}

TEST_CASE("pause/resume hold the clock without ending the play; stop rewinds to 0", "[animated-visual-player]")
{
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_source(&source); // playing, looped [0,1]

    player.tick(300ms);
    REQUIRE_THAT(player.progress(), WithinAbs(0.3, 1e-6));

    player.pause();
    REQUIRE(player.is_playing()); // pausing does not end the play
    REQUIRE(player.is_paused());
    player.tick(300ms); // paused: no further advance
    REQUIRE_THAT(player.progress(), WithinAbs(0.3, 1e-6));

    player.resume();
    REQUIRE(player.is_playing());
    player.tick(200ms);
    REQUIRE_THAT(player.progress(), WithinAbs(0.5, 1e-6));

    player.stop();
    REQUIRE_FALSE(player.is_playing());
    REQUIRE_THAT(player.progress(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("stop() rewinds to 0 wherever the play started", "[animated-visual-player]")
{
    // stop() rewinds to the play's from-progress, which nothing ever moves off 0.
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_auto_play(false);
    player.set_source(&source);

    player.play(0.4, 0.9, /*looped=*/false);
    player.tick(200ms);
    REQUIRE(player.is_playing());

    player.stop();
    REQUIRE_FALSE(player.is_playing());
    REQUIRE_THAT(player.progress(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("pause/resume/stop are no-ops before the first play()", "[animated-visual-player]")
{
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_auto_play(false);
    player.set_source(&source); // never played: auto_play is off

    player.pause();
    REQUIRE_FALSE(player.is_playing());
    player.resume();
    REQUIRE_FALSE(player.is_playing()); // resume() didn't start anything either
    player.stop();
    REQUIRE_THAT(player.progress(), WithinAbs(0.0, 1e-9)); // unchanged, not force-reset
}

TEST_CASE("set_progress jumps and completes whatever was playing", "[animated-visual-player]")
{
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_source(&source); // auto_play: playing
    REQUIRE(player.is_playing());

    player.set_progress(0.42);
    REQUIRE_THAT(player.progress(), WithinAbs(0.42, 1e-9));
    REQUIRE_FALSE(player.is_playing());
    player.tick(100ms);
    REQUIRE_THAT(player.progress(), WithinAbs(0.42, 1e-9));

    player.set_progress(5.0); // clamped
    REQUIRE_THAT(player.progress(), WithinAbs(1.0, 1e-9));
}

TEST_CASE("A negative playback rate runs the play backwards from its end", "[animated-visual-player][tick]")
{
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_auto_play(false);
    player.set_source(&source);

    player.set_playback_rate(-1.0);
    player.play(0.0, 1.0, /*looped=*/false);
    REQUIRE_THAT(player.progress(), WithinAbs(1.0, 1e-9)); // a reversed play starts at progress 1
    player.tick(250ms);
    REQUIRE_THAT(player.progress(), WithinAbs(0.75, 1e-6));

    player.set_playback_rate(2.0); // live: turns round and speeds up
    player.tick(100ms);
    REQUIRE_THAT(player.progress(), WithinAbs(0.95, 1e-6));

    player.set_playback_rate(-1.0);
    player.tick(Duration{2'000.0}); // runs out at the start: completes on `from`
    REQUIRE_FALSE(player.is_playing());
    REQUIRE_THAT(player.progress(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("A looped reverse play wraps from the start to the end", "[animated-visual-player][tick]")
{
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_auto_play(false);
    player.set_source(&source);
    player.set_playback_rate(-1.0);
    player.play(0.0, 1.0, /*looped=*/true);
    player.tick(Duration{1'100.0});
    REQUIRE(player.is_playing());
    REQUIRE_THAT(player.progress(), WithinAbs(0.9, 1e-6));
}

TEST_CASE("from > to plays through the end and on from the start", "[animated-visual-player][tick]")
{
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_auto_play(false);
    player.set_source(&source);
    player.play(0.8, 0.2, /*looped=*/false); // 0.4 of the duration: 400 ms
    player.tick(100ms);
    REQUIRE_THAT(player.progress(), WithinAbs(0.9, 1e-6));
    player.tick(200ms);
    REQUIRE_THAT(player.progress(), WithinAbs(0.1, 1e-6));
    player.tick(100ms);
    REQUIRE_FALSE(player.is_playing());
    REQUIRE_THAT(player.progress(), WithinAbs(0.2, 1e-9));
}

TEST_CASE("play() adjusts a backwards range and skips plays under 20 ms", "[animated-visual-player]")
{
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_auto_play(false);
    player.set_source(&source);

    player.play(0.5, 0.0, /*looped=*/false); // to 0 from 0.5 plays to 1
    player.tick(500ms);
    REQUIRE_THAT(player.progress(), WithinAbs(1.0, 1e-6));

    player.play(0.5, 0.51, /*looped=*/false); // 10 ms: not animated, jumps to `from`
    REQUIRE_FALSE(player.is_playing());
    REQUIRE_THAT(player.progress(), WithinAbs(0.5, 1e-9));
}

TEST_CASE("Turning auto_play on starts a loaded, idle player", "[animated-visual-player]")
{
    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_auto_play(false);
    player.set_source(&source);
    REQUIRE_FALSE(player.is_playing());
    player.set_auto_play(true);
    REQUIRE(player.is_playing());
}

TEST_CASE("Measure follows the stretch rules", "[animated-visual-player][layout]")
{
    FakeVisualSource            source(Size{100.0f, 50.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_source(&source);
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();

    Size desired = player.measure({400.0f, 400.0f}, measurer, theme);
    REQUIRE_THAT(desired.width, WithinAbs(400.0f, 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(200.0f, 1e-3));
    desired = player.measure({Unbounded, 100.0f}, measurer, theme);
    REQUIRE_THAT(desired.width, WithinAbs(200.0f, 1e-3));
    player.set_stretch(Stretch::Fill);
    desired = player.measure({300.0f, 70.0f}, measurer, theme);
    REQUIRE(desired == Size{300.0f, 70.0f});
    player.set_stretch(Stretch::None);
    desired = player.measure({300.0f, 30.0f}, measurer, theme);
    REQUIRE(desired == Size{100.0f, 30.0f});
}

TEST_CASE("UniformToFill is clipped to the player; None sits top-left", "[animated-visual-player][render]")
{
    FakeVisualSource            source(Size{100.0f, 50.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_source(&source);
    player.set_stretch(Stretch::UniformToFill);
    player.arrange(Rect{0.0f, 0.0f, 50.0f, 50.0f});
    TestRenderer renderer;
    player.render(renderer, Theme::light());
    REQUIRE(renderer.count_of<PushClip>() == 1);
    REQUIRE(source.last_bounds() == Rect{-25.0f, 0.0f, 100.0f, 50.0f});

    player.set_stretch(Stretch::None);
    player.arrange(Rect{10.0f, 10.0f, 200.0f, 200.0f});
    renderer.clear();
    player.render(renderer, Theme::light());
    REQUIRE(source.last_bounds() == Rect{10.0f, 10.0f, 100.0f, 50.0f});
}

namespace
{
class UnavailableSource final : public AnimatedVisualSource
{
public:
    [[nodiscard]] Size natural_size() const override
    {
        return {10.0f, 10.0f};
    }
    [[nodiscard]] Duration duration() const override
    {
        return 1000ms;
    }
    void render_frame(Renderer&, const Rect&, double) const override
    {
    }
    [[nodiscard]] bool available() const override
    {
        return false;
    }
};
} // namespace

TEST_CASE("An unavailable source shows the fallback content instead", "[animated-visual-player][fallback]")
{
    UnavailableSource           broken;
    FakeVisualSource            working(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    Element* fallback = player.set_fallback_content(std::make_unique<IconControl>(Bitmap(20, 20, colors::Black)));
    REQUIRE(player.fallback_content() == fallback);

    std::vector<Element*> children;
    player.automation_children(children);
    REQUIRE(children.empty()); // hidden fallback content is not in the tree

    player.set_source(&broken);
    REQUIRE(player.is_fallen_back());
    REQUIRE_FALSE(player.is_animated_visual_loaded());
    REQUIRE_FALSE(player.is_playing());
    HeadlessTextMeasurer measurer;
    REQUIRE(player.measure({Unbounded, Unbounded}, measurer, Theme::default_light()) == Size{20.0f, 20.0f});
    player.automation_children(children);
    REQUIRE(children.size() == 1);

    player.set_source(&working);
    REQUIRE_FALSE(player.is_fallen_back());
    REQUIRE(player.is_playing());
    REQUIRE(player.fallback_content() == fallback);
}

TEST_CASE("render fits the source's natural size into bounds per stretch and forwards progress",
          "[animated-visual-player][render]")
{
    FakeVisualSource            source(Size{10.0f, 20.0f}, 1000.0);
    AnimatedVisualPlayerControl player;
    player.set_auto_play(false);
    player.set_source(&source);
    player.set_stretch(Stretch::Uniform);
    player.set_progress(0.33);
    player.arrange(Rect{0.0f, 0.0f, 40.0f, 40.0f});

    TestRenderer renderer;
    player.render(renderer, Theme::light());

    const Rect expected = fit_stretch(Rect{0.0f, 0.0f, 40.0f, 40.0f}, Size{10.0f, 20.0f}, Stretch::Uniform);
    REQUIRE_THAT(source.last_bounds().x, WithinAbs(expected.x, 1e-4));
    REQUIRE_THAT(source.last_bounds().width, WithinAbs(expected.width, 1e-4));
    REQUIRE_THAT(source.last_bounds().height, WithinAbs(expected.height, 1e-4));
    REQUIRE_THAT(source.last_progress(), WithinAbs(0.33, 1e-9));
    REQUIRE(renderer.count() == 1);
}

TEST_CASE("on_tick is a no-op without a source or while paused", "[animated-visual-player][tick]")
{
    AnimatedVisualPlayerControl no_source;
    ui::testing::clear_dirty(no_source);
    no_source.tick(500ms);
    REQUIRE_FALSE(no_source.is_dirty());

    FakeVisualSource            source(Size{32.0f, 32.0f}, 1000.0);
    AnimatedVisualPlayerControl paused;
    paused.set_auto_play(false);
    paused.set_source(&source);
    ui::testing::clear_dirty(paused);
    paused.tick(500ms);
    REQUIRE_FALSE(paused.is_dirty());
}

// ---- off screen ----

namespace
{
constexpr Size Client{400.0f, 300.0f};

// A player below a spacer as tall as the window, so it starts out of view; the spacer's height
// brings it back.
struct OffScreenPlayer
{
    HeadlessUI                   ui     = create_headless();
    SpacerControl*               spacer = nullptr;
    AnimatedVisualPlayerControl* player = nullptr;

    OffScreenPlayer(const AnimatedVisualSource& source, bool auto_play)
    {
        auto stack = std::make_unique<StackControl>(Orientation::Vertical);
        spacer     = stack->add(std::make_unique<SpacerControl>());
        spacer->set_height(1000.0f);
        player = stack->add(std::make_unique<AnimatedVisualPlayerControl>());
        player->set_height(40.0f);
        player->set_auto_play(auto_play);
        player->set_source(&source);
        ui.window().set_content(std::move(stack));
        ui.show(Client);
        frame();
    }

    // What a host does for one frame: lay out, then the measuring pass that finds what is on screen.
    void frame()
    {
        ui.layout(Client);
        (void)ui.window().take_damage();
    }

    void scroll_into_view()
    {
        spacer->set_height(0.0f);
        frame();
    }
};
} // namespace

TEST_CASE("A looped play keeps real time off screen without ticking, and shows it when back",
          "[animated-visual-player][tick][screen]")
{
    FakeVisualSource source(Size{32.0f, 32.0f}, 1000.0);
    OffScreenPlayer  s{source, true};
    REQUIRE_FALSE(s.player->on_screen());
    REQUIRE(s.player->is_playing());

    // Ticked only by something else, it neither draws nor asks for ticks of its own.
    for (int i = 0; i < 10; ++i)
    {
        (void)s.ui.tick(350ms);
        REQUIRE_FALSE(s.player->is_dirty());
        REQUIRE_FALSE(s.ui.window().wants_ticks());
        REQUIRE_FALSE(s.ui.window().next_wake().has_value());
    }
    REQUIRE_THAT(s.player->progress(), WithinAbs(0.5, 1e-6)); // 3.5 loops in, wrapped

    s.scroll_into_view();
    REQUIRE(s.player->on_screen());
    REQUIRE(s.player->is_dirty()); // the progress it reached away is drawn
    REQUIRE_THAT(s.ui.window().next_wake().value_or(Duration{-1.0}).count(), WithinAbs(0.0, 1e-6));
    (void)s.ui.tick(100ms);
    REQUIRE_THAT(s.player->progress(), WithinAbs(0.6, 1e-6));
    REQUIRE(s.ui.window().wants_ticks()); // on screen the play animates as before
}

TEST_CASE("A play that ends off screen completes on time, on one wake", "[animated-visual-player][tick][screen]")
{
    FakeVisualSource source(Size{32.0f, 32.0f}, 1000.0);
    OffScreenPlayer  s{source, false};
    s.player->set_playback_rate(2.0);
    s.player->play(0.0, 1.0, /*looped=*/false);
    s.frame(); // the play() draws once, a change like any setter's

    (void)s.ui.tick(100ms); // 0.2 in at twice the speed: 400 ms to go
    REQUIRE_THAT(s.player->progress(), WithinAbs(0.2, 1e-6));
    REQUIRE_FALSE(s.ui.window().wants_ticks());
    REQUIRE_THAT(s.ui.window().next_wake().value_or(Duration{-1.0}).count(), WithinAbs(400.0, 1e-6));

    (void)s.ui.tick(399ms);
    REQUIRE(s.player->is_playing());
    REQUIRE_FALSE(s.ui.window().wants_ticks());
    REQUIRE_THAT(s.ui.window().next_wake().value_or(Duration{-1.0}).count(), WithinAbs(1.0, 1e-6));
    (void)s.ui.tick(s.ui.window().next_wake().value_or(1ms));
    REQUIRE_FALSE(s.player->is_playing());
    REQUIRE(s.player->progress() == 1.0);
    REQUIRE_FALSE(s.player->is_dirty());
    REQUIRE_FALSE(s.ui.window().wants_ticks());
    REQUIRE_FALSE(s.ui.window().next_wake().has_value());

    s.scroll_into_view();
    REQUIRE(s.player->is_dirty());                        // drawn at its end
    REQUIRE_FALSE(s.ui.window().next_wake().has_value()); // nothing left to play
}

TEST_CASE("Off screen, a paused play waits for nothing and a reversed one ends at its start",
          "[animated-visual-player][tick][screen]")
{
    FakeVisualSource source(Size{32.0f, 32.0f}, 1000.0);
    OffScreenPlayer  s{source, false};
    s.player->set_playback_rate(-1.0);
    s.player->play(0.0, 1.0, /*looped=*/false); // starts at its end, runs to 0
    s.frame();

    (void)s.ui.tick(300ms);
    REQUIRE_THAT(s.player->progress(), WithinAbs(0.7, 1e-6));
    REQUIRE_THAT(s.ui.window().next_wake().value_or(Duration{-1.0}).count(), WithinAbs(700.0, 1e-6));

    s.player->pause();
    (void)s.ui.tick(5000ms);
    REQUIRE_THAT(s.player->progress(), WithinAbs(0.7, 1e-6));
    REQUIRE_FALSE(s.ui.window().next_wake().has_value());

    s.player->resume();
    (void)s.ui.tick(0ms);
    REQUIRE_THAT(s.ui.window().next_wake().value_or(Duration{-1.0}).count(), WithinAbs(700.0, 1e-6));
    (void)s.ui.tick(700ms);
    REQUIRE_FALSE(s.player->is_playing());
    REQUIRE(s.player->progress() == 0.0);
}
