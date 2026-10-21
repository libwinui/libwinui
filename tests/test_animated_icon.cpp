// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/AnimatedVisualSource.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Symbol.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/media/AnimatedIcon.hpp"
#include "winui/ui/media/AnimatedVisuals.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
class FakeIconSource final : public AnimatedIconSource
{
public:
    explicit FakeIconSource(Size size = Size{20.0f, 20.0f}) : size_(size)
    {
        markers_["NormalToPointerOver_Start"]  = 0.0;
        markers_["NormalToPointerOver_End"]    = 0.25;
        markers_["PointerOverToNormal_Start"]  = 0.25;
        markers_["PointerOverToNormal_End"]    = 0.5;
        markers_["PointerOverToPressed_Start"] = 0.5;
        markers_["PointerOverToPressed_End"]   = 0.75;
        markers_["Checked"]                    = 0.9;
    }

    [[nodiscard]] Size natural_size() const override
    {
        return size_;
    }
    [[nodiscard]] Duration duration() const override
    {
        return 1000ms;
    }
    void render_frame(Renderer& renderer, const Rect& bounds, double progress) const override
    {
        render_icon_frame(renderer, bounds, progress, colors::Black);
    }
    void render_icon_frame(Renderer& renderer, const Rect& bounds, double progress, const Color& foreground) const override
    {
        last_progress_   = progress;
        last_foreground_ = foreground;
        renderer.fill_rounded_rect(bounds, 0.0f, foreground);
    }
    [[nodiscard]] const std::unordered_map<std::string, double>& markers() const override
    {
        return markers_;
    }
    [[nodiscard]] bool available() const override
    {
        return available_;
    }

    bool           available_     = true;
    mutable double last_progress_ = -1.0;
    mutable Color  last_foreground_{};

private:
    Size                                    size_;
    std::unordered_map<std::string, double> markers_;
};

} // namespace

TEST_CASE("A default-constructed AnimatedIcon has no source, no fallback, and draws nothing", "[animated-icon]")
{
    AnimatedIconControl icon;
    REQUIRE(icon.source() == nullptr);
    REQUIRE(icon.fallback().empty());
    REQUIRE(icon.state().empty());
    REQUIRE_THAT(icon.progress(), WithinAbs(0.0, 1e-9));

    HeadlessTextMeasurer measurer;
    const Theme          theme   = Theme::light();
    const Size           desired = icon.measure({Unbounded, Unbounded}, measurer, theme);
    REQUIRE(desired == Size{});

    icon.arrange(Rect{0, 0, 0, 0});
    TestRenderer renderer;
    icon.render(renderer, theme);
    REQUIRE(renderer.count() == 0);
}

TEST_CASE("Without a source that can be shown, AnimatedIcon draws its fallback glyph", "[animated-icon]")
{
    AnimatedIconControl icon;
    icon.set_fallback(symbol_glyph(Symbol::Find));

    HeadlessTextMeasurer measurer;
    const Theme          theme   = Theme::light();
    const Size           desired = icon.measure({Unbounded, Unbounded}, measurer, theme);
    REQUIRE_THAT(desired.width, WithinAbs(20.0, 1e-6)); // a symbol icon's 20 px
    icon.arrange(Rect{0, 0, desired.width, desired.height});
    TestRenderer renderer;
    icon.render(renderer, theme);
    REQUIRE(renderer.count_of<DrawTextRun>() == 1);

    FakeIconSource source;
    source.available_ = false;
    icon.set_source(&source);
    TestRenderer fallback;
    icon.render(fallback, theme);
    CHECK(fallback.count_of<DrawTextRun>() == 1);
    CHECK(fallback.count_of<FillRoundedRect>() == 0);
}

TEST_CASE("A transition with start and end markers plays that segment over its share of the duration",
          "[animated-icon][tick]")
{
    FakeIconSource      source;
    AnimatedIconControl icon;
    icon.set_source(&source);
    icon.set_state("Normal"); // from no state: no Normal marker, no "ToNormal_End"... but there is one
    // "PointerOverToNormal_End" contains "ToNormal_End": the first state cuts to it (0.5).
    CHECK_FALSE(icon.is_playing());
    CHECK_THAT(icon.progress(), WithinAbs(0.5, 1e-9));
    CHECK(icon.last_segment_end() == "PointerOverToNormal_End");

    icon.set_state("PointerOver"); // NormalToPointerOver_Start/_End: 0 -> 0.25 over 250 ms
    CHECK(icon.is_playing());
    CHECK(icon.current_state() == "PointerOver"); // the state machine moves on at once
    CHECK_THAT(icon.progress(), WithinAbs(0.0, 1e-9));
    icon.tick(125ms);
    CHECK_THAT(icon.progress(), WithinAbs(0.125, 1e-9));
    icon.tick(125ms);
    CHECK_THAT(icon.progress(), WithinAbs(0.25, 1e-9));
    CHECK_FALSE(icon.is_playing());
    CHECK(icon.last_segment() == "NormalToPointerOver");
}

TEST_CASE("A state set while a transition plays is queued behind it", "[animated-icon][tick]")
{
    FakeIconSource      source;
    AnimatedIconControl icon;
    icon.set_source(&source);
    icon.set_state("Normal");
    icon.set_state("PointerOver"); // playing 0 -> 0.25
    icon.set_state("Pressed");     // queued (QueueOne)
    CHECK(icon.current_state() == "PointerOver");
    icon.tick(250ms); // the first finishes and the queued one starts: 0.5 -> 0.75
    CHECK(icon.current_state() == "Pressed");
    CHECK(icon.is_playing());
    CHECK_THAT(icon.progress(), WithinAbs(0.5, 1e-9));
    icon.tick(250ms);
    CHECK_THAT(icon.progress(), WithinAbs(0.75, 1e-9));

    AnimatedIconControl cut;
    cut.set_queue_behavior(AnimatedIconQueueBehavior::Cut);
    cut.set_source(&source);
    cut.set_state("Normal");
    cut.set_state("PointerOver");
    cut.set_state("Checked"); // a plain state marker: cut straight to it
    CHECK(cut.current_state() == "Checked");
    CHECK_THAT(cut.progress(), WithinAbs(0.9, 1e-9));
}

TEST_CASE("A numeric state animates the progress to it, and an unknown one cuts to 0", "[animated-icon][tick]")
{
    FakeIconSource      source;
    AnimatedIconControl icon;
    icon.set_source(&source);
    icon.set_state("Checked"); // cut to 0.9
    icon.set_state("0.4");     // no markers: play to 0.4 over the previous segment's length (1)
    CHECK(icon.is_playing());
    icon.tick(1000ms);
    CHECK_THAT(icon.progress(), WithinAbs(0.4, 1e-9));
    icon.set_state("Bogus");
    CHECK_THAT(icon.progress(), WithinAbs(0.0, 1e-9));
    CHECK(icon.last_segment_end() == "0.0");
}

TEST_CASE("A segment shorter than 20 ms is a cut", "[animated-icon][tick]")
{
    class Quick final : public AnimatedIconSource
    {
    public:
        Size natural_size() const override
        {
            return Size{10.0f, 10.0f};
        }
        Duration duration() const override
        {
            return 50ms; // a 0.25 segment lasts 12.5 ms
        }
        void render_frame(Renderer&, const Rect&, double) const override
        {
        }
        const std::unordered_map<std::string, double>& markers() const override
        {
            static const std::unordered_map<std::string, double> m{{"AToB_Start", 0.0}, {"AToB_End", 0.25}};
            return m;
        }
    } quick;
    AnimatedIconControl icon;
    icon.set_source(&quick);
    icon.set_state("A");
    icon.set_state("B");
    CHECK_FALSE(icon.is_playing());
    CHECK_THAT(icon.progress(), WithinAbs(0.25, 1e-9));
}

TEST_CASE("AnimatedIcon sizes its visual Uniform and hands it its foreground", "[animated-icon][render]")
{
    FakeIconSource       source(Size{48.0f, 24.0f});
    AnimatedIconControl  icon;
    HeadlessTextMeasurer measurer;
    icon.set_source(&source);
    // Measure: unbounded keeps the natural size; one bounded axis scales the other.
    CHECK(icon.measure(Size{Unbounded, Unbounded}, measurer, Theme::default_light()) == Size{48.0f, 24.0f});
    CHECK(icon.measure(Size{24.0f, Unbounded}, measurer, Theme::default_light()) == Size{24.0f, 12.0f});
    CHECK(icon.measure(Size{96.0f, 30.0f}, measurer, Theme::default_light()) == Size{60.0f, 30.0f});

    // Arrange: the smaller ratio, centered.
    icon.arrange(Rect{0.0f, 0.0f, 48.0f, 48.0f});
    TestRenderer r;
    icon.render(r, Theme::light());
    const auto& fill = std::get<FillRoundedRect>(r.commands.at(0));
    CHECK(fill.bounds == Rect{0.0f, 12.0f, 48.0f, 24.0f});
    CHECK(source.last_foreground_ == Theme::light().colors().text);

    icon.set_foreground(Color::from_rgba(255, 0, 0));
    icon.render(r, Theme::light());
    CHECK(source.last_foreground_ == Color::from_rgba(255, 0, 0));
}

// ---- the stock visuals ----

TEST_CASE("The stock animated visuals draw in the foreground inside their bounds at every marker",
          "[animated-icon][animated-visuals]")
{
    const AnimatedBackVisualSource                   back;
    const AnimatedChevronDownSmallVisualSource       down;
    const AnimatedChevronRightDownSmallVisualSource  right_down;
    const AnimatedChevronUpDownSmallVisualSource     up_down;
    const AnimatedFindVisualSource                   find;
    const AnimatedGlobalNavigationButtonVisualSource hamburger;
    const AnimatedSettingsVisualSource               settings;
    const std::vector<const LottieIconSource*> all{&back, &down, &right_down, &up_down, &find, &hamburger, &settings};

    const Color red = Color::from_rgba(255, 0, 0);
    for (const LottieIconSource* source : all)
    {
        CHECK(source->natural_size() == Size{48.0f, 48.0f});
        CHECK(source->duration() > Duration{});
        REQUIRE_FALSE(source->markers().empty());
        std::vector<double> points{0.0};
        for (const auto& [name, value] : source->markers())
        {
            points.push_back(value);
        }
        for (double progress : points)
        {
            TestRenderer r;
            source->render_icon_frame(r, Rect{100.0f, 100.0f, 24.0f, 24.0f}, progress, red);
            std::size_t shapes = 0;
            for (const RenderCommand& c : r.commands)
            {
                if (const auto* g = std::get_if<DrawGeometry>(&c))
                {
                    ++shapes;
                    const Brush& paint = g->fill ? *g->fill : *g->stroke;
                    CHECK(std::get<Color>(paint) == red);
                    const Rect b = g->geometry.bounds();
                    CHECK(b.x >= 95.0f);
                    CHECK(b.y >= 95.0f);
                    CHECK(b.right() <= 129.0f);
                    CHECK(b.bottom() <= 129.0f);
                }
            }
            CHECK(shapes > 0);
        }
    }
}

TEST_CASE("AnimatedFindVisualSource shrinks its lens toward the pressed pose", "[animated-icon][animated-visuals]")
{
    const AnimatedFindVisualSource find;
    const auto                     lens_width = [&](double progress)
    {
        TestRenderer r;
        find.render_icon_frame(r, Rect{0.0f, 0.0f, 48.0f, 48.0f}, progress, colors::Black);
        for (const RenderCommand& c : r.commands)
        {
            if (const auto* g = std::get_if<DrawGeometry>(&c); g != nullptr && g->stroke)
            {
                return g->geometry.bounds().width;
            }
        }
        return 0.0f;
    };
    // At rest the lens is a 14.1875-radius circle scaled by 0.92286; the press animation ends at 0.8 of it.
    const float rest    = lens_width(0.0);
    const float pressed = lens_width(find.markers().at("NormalToPressed_End"));
    CHECK_THAT(rest, WithinAbs(2.0 * 14.1875 * 0.92286, 1e-2));
    CHECK_THAT(pressed, WithinAbs(rest * 0.8, 1e-2));
}

TEST_CASE("A queued state whose transition is too short to animate is played once, not forever", "[animated-icon][tick]")
{
    // A segment under 20 ms completes inside the transition, so the queue has to be popped
    // first or the same queued state replays until the stack runs out.
    static const AnimatedChevronDownSmallVisualSource chevron;
    AnimatedIconControl                               icon;
    icon.set_source(&chevron);
    icon.set_state("Normal");
    icon.set_state("PointerOver");
    icon.set_state("Pressed");
    REQUIRE(icon.is_playing());
    icon.set_state("PointerOver"); // queued behind the press
    icon.set_state("Normal");      // queued behind the release
    for (int i = 0; i < 100; ++i)
    {
        icon.tick(16ms);
    }
    CHECK_FALSE(icon.is_playing());
    CHECK(icon.current_state() == "Normal");
}
