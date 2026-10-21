// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/VisualStates.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
PointerEvent move(Point p)
{
    return {p, PointerButton::None, PointerAction::Move};
}
PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}
} // namespace

TEST_CASE("Slider measure respects orientation and fills the cross axis", "[slider][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    // A 32 DIP minimum floors the cross axis.
    const float cross_axis_core = std::max(ui::detail::controls(theme).slider_thumb.outer, 32.0f);

    SliderControl horizontal{Orientation::Horizontal};
    const Size    h_desired = horizontal.measure({Unbounded, 400}, m, theme);
    REQUIRE_THAT(h_desired.height, WithinAbs(cross_axis_core, 1e-3));
    REQUIRE_THAT(h_desired.width, WithinAbs(200.0, 1e-3)); // unconstrained -> nominal default length

    SliderControl vertical{Orientation::Vertical};
    const Size    v_desired = vertical.measure({500, Unbounded}, m, theme);
    REQUIRE_THAT(v_desired.width, WithinAbs(cross_axis_core, 1e-3));
    REQUIRE_THAT(v_desired.height, WithinAbs(200.0, 1e-3));
}

TEST_CASE("New Slider defaults to a 0..100 range at 0, Normal, and enabled", "[slider]")
{
    SliderControl s;
    REQUIRE(s.enabled());
    REQUIRE_FALSE(s.focused());
    REQUIRE_THAT(s.min(), WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(s.max(), WithinAbs(100.0, 1e-6));
    REQUIRE_THAT(s.value(), WithinAbs(0.0, 1e-6));
    REQUIRE(ui::testing::visual_state(s) == ButtonState::Normal);
    REQUIRE(s.focusable());
}

TEST_CASE("Constructing with an explicit value clamps it into [min, max]", "[slider]")
{
    SliderControl under{Orientation::Horizontal, 0.0f, 10.0f, -5.0f};
    REQUIRE_THAT(under.value(), WithinAbs(0.0, 1e-6));

    SliderControl over{Orientation::Horizontal, 0.0f, 10.0f, 50.0f};
    REQUIRE_THAT(over.value(), WithinAbs(10.0, 1e-6));
}

TEST_CASE("set_value clamps and fires on_value_changed only when the value actually moves", "[slider][command]")
{
    SliderControl       s{Orientation::Horizontal, 0.0f, 10.0f, 5.0f};
    std::vector<double> observed;
    s.on_value_changed([&](double v) { observed.push_back(v); });

    s.set_value(7.0f);
    REQUIRE_THAT(s.value(), WithinAbs(7.0, 1e-6));

    s.set_value(50.0f); // clamped to max
    REQUIRE_THAT(s.value(), WithinAbs(10.0, 1e-6));

    s.set_value(10.0f); // unchanged -> no callback
    REQUIRE(observed == (std::vector<double>{7.0, 10.0}));
}

TEST_CASE("set_range re-clamps the current value", "[slider][command]")
{
    SliderControl s{Orientation::Horizontal, 0.0f, 10.0f, 8.0f};

    std::vector<double> observed;
    s.on_value_changed([&](double v) { observed.push_back(v); });

    s.set_range(0.0f, 5.0f);
    REQUIRE_THAT(s.value(), WithinAbs(5.0, 1e-6));
    REQUIRE(observed == std::vector<double>{5.0});
}

TEST_CASE("Clicking the horizontal track jumps the value to that position", "[slider][input]")
{
    SliderControl s{Orientation::Horizontal, 0.0f, 100.0f, 0.0f};
    s.arrange({0, 0, 200, 20});

    s.handle_pointer(down({50, 10}));
    REQUIRE(s.dragging());
    REQUIRE_THAT(s.value(), WithinAbs(25.0, 1e-3)); // 50/200 of [0,100]

    s.handle_pointer(move({150, 10}));
    REQUIRE_THAT(s.value(), WithinAbs(75.0, 1e-3));

    s.handle_pointer(up({150, 10}));
    REQUIRE_FALSE(s.dragging());

    s.handle_pointer(move({0, 10}));
    REQUIRE_THAT(s.value(), WithinAbs(75.0, 1e-3));
}

TEST_CASE("A vertical Slider maps min to the bottom and max to the top", "[slider][input]")
{
    SliderControl s{Orientation::Vertical, 0.0f, 100.0f, 0.0f};
    s.arrange({0, 0, 20, 200});

    s.handle_pointer(down({10, 150})); // near the bottom
    REQUIRE_THAT(s.value(), WithinAbs(25.0, 1e-3));

    s.handle_pointer(move({10, 50})); // near the top
    REQUIRE_THAT(s.value(), WithinAbs(75.0, 1e-3));
}

TEST_CASE("A drag started outside the slider's bounds does not start dragging", "[slider][input]")
{
    SliderControl s;
    s.arrange({0, 0, 200, 20});

    s.handle_pointer(down({500, 500}));
    REQUIRE_FALSE(s.dragging());
    REQUIRE_THAT(s.value(), WithinAbs(0.0, 1e-6));
}

TEST_CASE("Arrow keys step the value while focused; Home/End jump to min/max", "[slider][command]")
{
    SliderControl s{Orientation::Horizontal, 0.0f, 10.0f, 5.0f};
    s.set_step(2.0f);
    ui::testing::set_focused(s, true);

    s.handle_key({Key::ArrowRight, KeyAction::Down});
    REQUIRE_THAT(s.value(), WithinAbs(7.0, 1e-6));

    s.handle_key({Key::ArrowUp, KeyAction::Down});
    REQUIRE_THAT(s.value(), WithinAbs(9.0, 1e-6));

    s.handle_key({Key::ArrowLeft, KeyAction::Down});
    REQUIRE_THAT(s.value(), WithinAbs(7.0, 1e-6));

    s.handle_key({Key::ArrowDown, KeyAction::Down});
    REQUIRE_THAT(s.value(), WithinAbs(5.0, 1e-6));

    s.handle_key({Key::End, KeyAction::Down});
    REQUIRE_THAT(s.value(), WithinAbs(10.0, 1e-6));

    s.handle_key({Key::Home, KeyAction::Down});
    REQUIRE_THAT(s.value(), WithinAbs(0.0, 1e-6));
}

TEST_CASE("Arrow keys are ignored while not focused", "[slider][focus]")
{
    SliderControl s{Orientation::Horizontal, 0.0f, 10.0f, 5.0f};
    s.handle_key({Key::ArrowRight, KeyAction::Down});
    REQUIRE_THAT(s.value(), WithinAbs(5.0, 1e-6));
}

TEST_CASE("Disabled Slider ignores pointer and keyboard input", "[slider][state]")
{
    SliderControl s{Orientation::Horizontal, 0.0f, 10.0f, 5.0f};
    s.arrange({0, 0, 200, 20});
    s.set_enabled(false);
    ui::testing::set_focused(s, true);

    s.handle_pointer(down({50, 10}));
    s.handle_key({Key::ArrowRight, KeyAction::Down});

    REQUIRE_THAT(s.value(), WithinAbs(5.0, 1e-6));
    REQUIRE_FALSE(s.dragging());
    REQUIRE_FALSE(s.focusable());
    REQUIRE(ui::testing::visual_state(s) == ButtonState::Disabled);
}

// ---- The two thumb constructions (`SliderThumbShape`) ----------------------------------
// Fluent draws an outer disc plus an accent inner circle that grows and shrinks. MDL2 draws one
// 8x24 accent rectangle at CornerRadius="4", the only CornerRadius in the 1507 default styles.

namespace
{
SliderControl laid_out(Orientation orientation, Size size)
{
    SliderControl slider{orientation, 0.0f, 100.0f, 50.0f};
    slider.arrange(Rect{0.0f, 0.0f, size.width, size.height});
    return slider;
}

std::vector<FillRoundedRect> rounded_fills(const TestRenderer& r)
{
    std::vector<FillRoundedRect> out;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c))
        {
            out.push_back(*f);
        }
    }
    return out;
}

std::size_t ellipse_count(const TestRenderer& r)
{
    std::size_t n = 0;
    for (const RenderCommand& c : r.commands)
    {
        if (std::holds_alternative<FillEllipse>(c) || std::holds_alternative<StrokeEllipse>(c))
        {
            ++n;
        }
    }
    return n;
}
} // namespace

TEST_CASE("Fluent's Slider thumb is drawn as two concentric circles", "[slider][render][theme]")
{
    const Theme theme = resolve_theme(ThemeRequest{}, OsThemeSnapshot{});
    REQUIRE(ui::detail::controls(theme).slider_thumb.shape == SliderThumbShape::ConcentricCircles);

    SliderControl slider = laid_out(Orientation::Horizontal, Size{200.0f, 40.0f});
    TestRenderer  renderer;
    slider.render(renderer, theme);

    // The opaque outer disc (fill + stroke) and the accent inner circle: three ellipse
    // commands, and no rounded-rect thumb anywhere.
    CHECK(ellipse_count(renderer) == 3);
}

TEST_CASE("MDL2's Slider thumb is drawn as one 8x24 rounded rectangle at radius 4", "[slider][render][theme][mdl2]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});
    REQUIRE(ui::detail::controls(theme).slider_thumb.shape == SliderThumbShape::RoundedRect);

    SliderControl slider = laid_out(Orientation::Horizontal, Size{200.0f, 40.0f});
    TestRenderer  renderer;
    slider.render(renderer, theme);

    // No disc and no stroke: neither is drawn, rather than drawn transparent.
    CHECK(ellipse_count(renderer) == 0);

    // One fill carries the thumb's radius, 8x24 on a horizontal slider. The
    // track and its value fill use the track's own radius.
    const std::vector<FillRoundedRect> fills = rounded_fills(renderer);
    std::vector<FillRoundedRect>       thumbs;
    for (const FillRoundedRect& f : fills)
    {
        if (f.radius == ui::detail::controls(theme).slider_thumb.radius)
        {
            thumbs.push_back(f);
        }
    }
    REQUIRE(thumbs.size() == 1);
    CHECK(thumbs[0].radius == 4.0f);
    CHECK(thumbs[0].bounds.width == 8.0f);
    CHECK(thumbs[0].bounds.height == 24.0f);
    // Filled with the plain accent at rest.
    REQUIRE(ui::detail::controls(theme).slider_fills.has_value());
    CHECK(thumbs[0].color == ui::detail::controls(theme).slider_fills->thumb);
}

// ---- Luna ---------------------------------------------------------------------------------
// Luna's horizontal trackbar thumb (five 11x21 state cells) and its vertical transpose.
TEST_CASE("Luna's Slider thumb is an 11x21 block with an accent band at each end", "[slider][render][theme][luna]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});
    REQUIRE(ui::detail::controls(theme).slider_thumb.shape == SliderThumbShape::RoundedRect);
    REQUIRE(ui::detail::controls(theme).slider_chrome.has_value());

    SliderControl slider = laid_out(Orientation::Horizontal, Size{200.0f, 40.0f});
    TestRenderer  renderer;
    slider.render(renderer, theme);

    CHECK(ellipse_count(renderer) == 0); // not a Fluent disc

    // The body: the only fill carrying the thumb's own 1px corner cut, 11 across the track's
    // direction by 21 across it, in `[TrackBar.Thumb] FillColorHint`.
    std::vector<FillRoundedRect> thumbs;
    for (const FillRoundedRect& f : rounded_fills(renderer))
    {
        if (f.radius == ui::detail::controls(theme).slider_thumb.radius)
        {
            thumbs.push_back(f);
        }
    }
    REQUIRE(thumbs.size() == 1);
    CHECK(thumbs[0].bounds.width == 11.0f);
    CHECK(thumbs[0].bounds.height == 21.0f);
    CHECK(thumbs[0].color == Color::from_argb(0xFFF3F3EF));

    // The two bands, rows 1-2 and 18-19 of the cell: 9 x 2 each, inset 1, green and lit from
    // above. They sit *on* the body, so they are square-cornered fills rather than part of it.
    std::vector<FillRoundedRect> bands;
    for (const FillRoundedRect& f : rounded_fills(renderer))
    {
        if (f.radius == 0.0f && f.bounds.height == ui::detail::controls(theme).slider_chrome->band_extent)
        {
            bands.push_back(f);
        }
    }
    REQUIRE(bands.size() == 2);
    CHECK(bands[0].bounds.width == 9.0f);
    CHECK(bands[0].bounds.top() == thumbs[0].bounds.top() + 1.0f);
    CHECK(bands[0].color == Color::from_argb(0xFF48CB46));
    CHECK(bands[1].bounds.top() == thumbs[0].bounds.top() + 18.0f);
    CHECK(bands[1].color == Color::from_argb(0xFF1FAE1D));

    // Two 1px bevels, the thumb's raised and the track's sunken, eight lines each.
    CHECK(renderer.count_of<StrokeLine>() == 16);
}

TEST_CASE("A hovered Luna slider turns its bands amber and leaves the body alone", "[slider][render][theme][luna]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    SliderControl slider = laid_out(Orientation::Horizontal, Size{200.0f, 40.0f});
    slider.handle_pointer(PointerEvent{{100.0f, 20.0f}, PointerButton::None, PointerAction::Move});

    TestRenderer renderer;
    slider.render(renderer, theme);

    std::vector<FillRoundedRect> bands;
    Color                        body{};
    for (const FillRoundedRect& f : rounded_fills(renderer))
    {
        if (f.radius == 0.0f && f.bounds.height == ui::detail::controls(theme).slider_chrome->band_extent)
        {
            bands.push_back(f);
        }
        else if (f.radius == ui::detail::controls(theme).slider_thumb.radius)
        {
            body = f.color;
        }
    }
    REQUIRE(bands.size() == 2);
    // The trackbar thumb is one of the seven parts where Luna's signature amber really appears.
    CHECK(bands[0].color == Color::from_argb(0xFFFAC158));
    CHECK(bands[1].color == Color::from_argb(0xFFE2A330));
    // The body and the thumb's size do not move: all five cells are the same size and the same
    // `#F3F3EF`, so only the bands carry the state.
    CHECK(body == Color::from_argb(0xFFF3F3EF));
    CHECK(ui::detail::controls(theme).slider_thumb.hover == ui::detail::controls(theme).slider_thumb.rest);
}

TEST_CASE("A Luna slider fills no part of its track", "[slider][render][theme][luna]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});
    CHECK_FALSE(ui::detail::controls(theme).slider_value_fill);

    SliderControl slider = laid_out(Orientation::Horizontal, Size{200.0f, 40.0f});
    TestRenderer  renderer;
    slider.render(renderer, theme);

    // [TrackBar.Track] imagecount = 1: one uniform groove, so one fill carries the track's radius.
    const float track_radius = ui::detail::controls(theme).slider_track_thickness * 0.5f;
    std::size_t at_track     = 0;
    for (const FillRoundedRect& f : rounded_fills(renderer))
    {
        if (f.radius == track_radius)
        {
            ++at_track;
            CHECK(f.color == Color::from_argb(0xFFF2F1E9));
        }
    }
    CHECK(at_track == 1);

    // Fluent, by contrast, does fill it.
    const Theme   fluent   = resolve_theme(ThemeRequest{}, OsThemeSnapshot{});
    SliderControl f_slider = laid_out(Orientation::Horizontal, Size{200.0f, 40.0f});
    TestRenderer  f_renderer;
    f_slider.render(f_renderer, fluent);
    CHECK(ui::detail::controls(fluent).slider_value_fill);
}

TEST_CASE("Luna's vertical Slider thumb mirrors to 21x11 with the bands on its ends", "[slider][render][theme][luna]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    SliderControl slider = laid_out(Orientation::Vertical, Size{40.0f, 200.0f});
    TestRenderer  renderer;
    slider.render(renderer, theme);

    std::vector<FillRoundedRect> thumbs;
    std::vector<FillRoundedRect> bands;
    for (const FillRoundedRect& f : rounded_fills(renderer))
    {
        if (f.radius == ui::detail::controls(theme).slider_thumb.radius)
        {
            thumbs.push_back(f);
        }
        else if (f.radius == 0.0f && f.bounds.width == ui::detail::controls(theme).slider_chrome->band_extent)
        {
            bands.push_back(f);
        }
    }
    REQUIRE(thumbs.size() == 1);
    // The long axis is *across* the track in both orientations, so the cell transposes whole.
    CHECK(thumbs[0].bounds.width == 21.0f);
    CHECK(thumbs[0].bounds.height == 11.0f);
    REQUIRE(bands.size() == 2);
    CHECK(bands[0].bounds.height == 9.0f);
    CHECK(bands[0].bounds.left() == thumbs[0].bounds.left() + 1.0f);
    CHECK(bands[1].bounds.left() == thumbs[0].bounds.left() + 18.0f);
}

TEST_CASE("MDL2's vertical Slider thumb mirrors to 24x8", "[slider][render][theme][mdl2]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});

    SliderControl slider = laid_out(Orientation::Vertical, Size{40.0f, 200.0f});

    TestRenderer renderer;
    slider.render(renderer, theme);

    std::vector<FillRoundedRect> thumbs;
    for (const FillRoundedRect& f : rounded_fills(renderer))
    {
        if (f.radius == ui::detail::controls(theme).slider_thumb.radius)
        {
            thumbs.push_back(f);
        }
    }
    REQUIRE(thumbs.size() == 1);
    CHECK(thumbs[0].bounds.width == 24.0f);
    CHECK(thumbs[0].bounds.height == 8.0f);
}

// ---------------------------------------------------------------------------------------
// The classic TRACKBAR_CLASS look from the original Windows 95 comctl32.
// ---------------------------------------------------------------------------------------

namespace
{
const Theme& retro()
{
    static const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    return theme;
}

// A horizontal classic slider in 160x32. 32 is 3px more than it asks for, on purpose, so
// render_classic()'s centering gets exercised too.
std::vector<RenderCommand> retro_commands(float value, bool enabled = true, float tick_frequency = 0.0f)
{
    SliderControl        slider{Orientation::Horizontal, 0.0f, 100.0f, value};
    HeadlessTextMeasurer measurer;
    slider.set_tick_frequency(tick_frequency);
    slider.set_enabled(enabled);
    (void)slider.measure(Size{160.0f, 32.0f}, measurer, retro());
    slider.arrange({0.0f, 0.0f, 160.0f, 32.0f});

    TestRenderer renderer;
    slider.render(renderer, retro());
    return renderer.commands;
}

// Every 1px-tall solid fill on row `y`, left to right: how the thumb's point and the ticks draw.
std::vector<FillRoundedRect> row_fills(const std::vector<RenderCommand>& commands, float y)
{
    std::vector<FillRoundedRect> out;
    for (const auto& cmd : commands)
    {
        const auto* fill = std::get_if<FillRoundedRect>(&cmd);
        if (fill && fill->bounds.y == y && fill->bounds.height == 1.0f)
        {
            out.push_back(*fill);
        }
    }
    return out;
}

// The full-width horizontal edge line at `y`, if there is one.
std::optional<Color> edge_at(const std::vector<RenderCommand>& commands, float y)
{
    for (const auto& cmd : commands)
    {
        const auto* line = std::get_if<StrokeLine>(&cmd);
        if (line && line->from.y == line->to.y && line->from.y == y)
        {
            return line->color;
        }
    }
    return std::nullopt;
}
} // namespace

TEST_CASE("A Windows9x Slider draws a sunken channel, not a Fluent track", "[slider][retro]")
{
    // DrawEdge(EDGE_SUNKEN, BF_RECT) over a rect two rings thick: all bevel, no interior. Under
    // 95/98 the rows are BTNSHADOW, 3DDKSHADOW, 3DLIGHT, BTNHIGHLIGHT.
    const auto commands = retro_commands(50.0f);

    // 32px of room for a 29px control leaves 3px of slack, 1px of which goes above: the channel
    // sits at 1 + `channel_offset`.
    CHECK(edge_at(commands, 10.5f) == Color::from_argb(0xFF808080));
    CHECK(edge_at(commands, 11.5f) == colors::Black);
    CHECK(edge_at(commands, 12.5f) == Color::from_argb(0xFFDFDFDF));
    CHECK(edge_at(commands, 13.5f) == colors::White);

    for (const auto& cmd : commands)
    {
        const auto* line = std::get_if<StrokeLine>(&cmd);
        if (line && line->from.y == 10.5f)
        {
            CHECK(line->from.x == 8.0f);
            CHECK(line->to.x == 152.0f);
        }
    }

    // A classic groove has no value fill. The thumb position is the only cue.
    CHECK_FALSE(ui::detail::controls(retro()).slider_value_fill);
}

TEST_CASE("A Windows9x Slider's thumb comes to a point", "[slider][retro]")
{
    // comctl32 fills a pentagon with a lit diagonal down one flank and a shadowed one down the
    // other. Aliased, each row loses one pixel at both ends, the outermost carrying the ramp.
    const auto commands = retro_commands(50.0f);

    // The flat-ended part is 11 wide and `uThumbLen - point` = 16 tall, and nothing draws an
    // edge along the side the point grows out of.
    const FillRoundedRect* body = nullptr;
    for (const auto& cmd : commands)
    {
        const auto* fill = std::get_if<FillRoundedRect>(&cmd);
        if (fill && fill->bounds.width == 11.0f)
        {
            body = fill;
        }
    }
    REQUIRE(body != nullptr);
    CHECK(body->bounds.height == 16.0f);
    CHECK(body->color == Color::from_argb(0xFFC0C0C0)); // BTNFACE, not COLOR_WINDOW
    CHECK(body->bounds.y == 3.0f);                      // 1px of slack + `thumb_margin`

    // Five rows of point below it, 9 / 7 / 5 / 3 / 1 wide.
    const float top = body->bounds.bottom();
    for (int k = 0; k < 5; ++k)
    {
        const auto fills = row_fills(commands, top + static_cast<float>(k));
        REQUIRE_FALSE(fills.empty());
        CHECK(fills.front().bounds.width == 9.0f - static_cast<float>(k) * 2.0f);
        // The shadow goes down last, so it owns the single pixel at the tip.
        CHECK(fills.back().color == colors::Black);
        CHECK(fills.front().bounds.x == body->bounds.x + static_cast<float>(k) + 1.0f);
    }

    const auto tip = row_fills(commands, top + 4.0f);
    CHECK(tip.front().bounds.width == 1.0f);
    CHECK(tip.front().bounds.x == body->bounds.x + 5.0f);
}

TEST_CASE("A Windows9x Slider's thumb travels inside its channel", "[slider][retro]")
{
    // Unlike Fluent's, a trackbar thumb never overhangs the groove. measure_override() caches the
    // inset and both drawing and hit-testing read it, so the two must agree.
    HeadlessTextMeasurer measurer;
    SliderControl        slider{Orientation::Horizontal, 0.0f, 100.0f, 0.0f};
    (void)slider.measure(Size{160.0f, 32.0f}, measurer, retro());
    slider.arrange({0.0f, 0.0f, 160.0f, 32.0f});

    const auto thumb_left = [&](float value)
    {
        slider.set_value(value);
        TestRenderer renderer;
        slider.render(renderer, retro());
        for (const auto& cmd : renderer.commands)
        {
            const auto* fill = std::get_if<FillRoundedRect>(&cmd);
            if (fill && fill->bounds.width == 11.0f)
            {
                return fill->bounds.x;
            }
        }
        FAIL("no thumb body");
        return 0.0f;
    };

    // At both extremes the thumb sits exactly on the channel's end, neither overhanging it nor
    // stopping short: channel_inset .. control width - channel_inset - thumb width.
    CHECK(thumb_left(0.0f) == 8.0f);
    CHECK(thumb_left(100.0f) == 141.0f);

    // And a click lands where the thumb is drawn, not `channel_inset + 5.5` px away from it.
    slider.handle_pointer(down({8.0f + 5.5f, 16.0f}));
    CHECK_THAT(slider.value(), WithinAbs(0.0, 0.01));
    slider.handle_pointer(move({141.0f + 5.5f, 16.0f}));
    CHECK_THAT(slider.value(), WithinAbs(100.0, 0.01));
    slider.handle_pointer(up({141.0f + 5.5f, 16.0f}));
}

TEST_CASE("A stock trackbar draws two tick marks; TBM_SETTICFREQ draws the rest", "[slider][retro]")
{
    // tick_frequency() == 0 still draws the two end ticks, a pixel longer than interior ones
    // (comctl32's TIC_EDGE handling).
    const float tick_row = 26.0f; // 1px of slack + thumb_margin + uThumbLen + tick_gap

    const auto ticks = [&](float frequency)
    {
        std::vector<FillRoundedRect> out;
        for (const auto& cmd : retro_commands(50.0f, true, frequency))
        {
            const auto* fill = std::get_if<FillRoundedRect>(&cmd);
            if (fill && fill->bounds.y == tick_row)
            {
                out.push_back(*fill);
            }
        }
        return out;
    };

    const auto stock = ticks(0.0f);
    REQUIRE(stock.size() == 2);
    CHECK(stock[0].bounds.width == 1.0f);
    CHECK(stock[0].bounds.height == 4.0f);
    CHECK(stock[1].bounds.height == 4.0f);
    CHECK(stock[0].color == colors::Black); // COLOR_3DDKSHADOW
    // They mark the two ends of the *travel*, so they line up with where the thumb can get to.
    CHECK(stock[0].bounds.x == 13.0f);
    CHECK(stock[1].bounds.x == 146.0f);

    // Every tenth unit across 0-100 is nine interior ticks plus the two ends, and the interior
    // ones are the shorter kind.
    const auto every_ten = ticks(10.0f);
    CHECK(every_ten.size() == 11);
    CHECK(every_ten[2].bounds.height == 3.0f);

    // A frequency finer than the control is wide is capped rather than trusted, so this emits a
    // bounded number of commands instead of one per value unit.
    CHECK(ticks(0.0001f).size() <= 514);
}

TEST_CASE("A Windows9x Slider dithers its thumb instead of restyling the bevel", "[slider][retro]")
{
    // Disabled, and in this era dragging, swaps the face for a 50% dither of 3DFACE over
    // 3DHILIGHT. The bevel stays raised; the dither is the only state feedback.
    const auto patterns = [](const std::vector<RenderCommand>& commands)
    {
        std::vector<FillRectPattern> out;
        for (const auto& cmd : commands)
        {
            if (const auto* fill = std::get_if<FillRectPattern>(&cmd))
            {
                out.push_back(*fill);
            }
        }
        return out;
    };

    CHECK(patterns(retro_commands(50.0f)).empty());

    const auto disabled = patterns(retro_commands(50.0f, /*enabled=*/false));
    REQUIRE(disabled.size() == 6); // the flat-ended part plus one per row of the point
    CHECK(disabled.front().pattern == Pattern::Checkerboard50);
    CHECK(disabled.front().fg == Color::from_argb(0xFFC0C0C0));
    CHECK(disabled.front().bg == colors::White);
    // A point row swaps the pair on an odd diagonal, or the checker restarts per row and breaks
    // into stripes. On the stock 11x21 thumb every row is odd.
    for (std::size_t i = 1; i < disabled.size(); ++i)
    {
        CHECK(disabled[i].fg == disabled.front().bg);
        CHECK(disabled[i].bg == disabled.front().fg);
    }

    // And the bevel is unchanged: still a raised top edge in BTNHIGHLIGHT, not a sunken one.
    CHECK(edge_at(retro_commands(50.0f, /*enabled=*/false), 3.5f) == colors::White);
}

TEST_CASE("A vertical Windows9x Slider is the same control turned on its side", "[slider][retro]")
{
    // The stock vertical style is TBS_RIGHT: point and ticks on the right, value increasing upward.
    SliderControl        slider{Orientation::Vertical, 0.0f, 100.0f, 50.0f};
    HeadlessTextMeasurer measurer;
    (void)slider.measure(Size{32.0f, 160.0f}, measurer, retro());
    slider.arrange({0.0f, 0.0f, 32.0f, 160.0f});

    TestRenderer renderer;
    slider.render(renderer, retro());

    // The channel runs *down* the control, 4 wide and inset 8 from top and bottom.
    const StrokeLine* channel_edge = nullptr;
    for (const auto& cmd : renderer.commands)
    {
        const auto* line = std::get_if<StrokeLine>(&cmd);
        if (line != nullptr && line->from.x == line->to.x && line->from.x == 10.5f)
        {
            channel_edge = line;
            break;
        }
    }
    REQUIRE(channel_edge != nullptr);
    CHECK(channel_edge->color == Color::from_argb(0xFF808080)); // BTNSHADOW on the near side
    CHECK(channel_edge->from.y == 8.0f);
    CHECK(channel_edge->to.y == 152.0f);

    // And the thumb is 21 across by 11 along, i.e. wider than it is tall.
    const FillRoundedRect* body = nullptr;
    for (const auto& cmd : renderer.commands)
    {
        const auto* fill = std::get_if<FillRoundedRect>(&cmd);
        if (fill != nullptr && fill->bounds.height == 11.0f)
        {
            body = fill;
        }
    }
    REQUIRE(body != nullptr);
    CHECK(body->bounds.width == 16.0f); // the flat-ended part; the other 5 are the point
    CHECK(body->bounds.x == 3.0f);      // 1px of slack + thumb_margin, from the *left*

    // The point grows to the right, so each of its rows is a *column* one pixel shorter at both
    // ends than the last.
    const FillRoundedRect* first_point_row = nullptr;
    for (const auto& cmd : renderer.commands)
    {
        const auto* fill = std::get_if<FillRoundedRect>(&cmd);
        if (fill != nullptr && fill->bounds.width == 1.0f && fill->bounds.height == 9.0f)
        {
            first_point_row = fill;
            break;
        }
    }
    REQUIRE(first_point_row != nullptr);
    CHECK(first_point_row->bounds.x == body->bounds.right());
}

// ---- Snapping and the Fluent tick rows ----------------------------------------------------
// step_frequency and snaps_to round a drag to the closest step, and
// the tick rows draw tick_frequency marks in every Fluent era.

TEST_CASE("A drag snaps to step_frequency multiples, whole values by default", "[slider][input][snap]")
{
    SliderControl s{Orientation::Horizontal, 0.0f, 100.0f, 0.0f};
    s.arrange({0, 0, 200, 20});
    REQUIRE(s.step_frequency() == 1.0f); // the step_frequency default

    s.handle_pointer(down({47, 10})); // 23.5 -> 23, the tie going down
    CHECK_THAT(s.value(), WithinAbs(23.0, 1e-3));
    s.handle_pointer(up({47, 10}));

    s.set_step_frequency(0.0f); // continuous
    s.handle_pointer(down({47, 10}));
    CHECK_THAT(s.value(), WithinAbs(23.5, 1e-3));
    s.handle_pointer(up({47, 10}));

    s.set_step_frequency(10.0f);
    s.handle_pointer(down({47, 10})); // 23.5 -> 20
    CHECK_THAT(s.value(), WithinAbs(20.0, 1e-3));
    s.handle_pointer(move({52, 10})); // 26 -> 30
    CHECK_THAT(s.value(), WithinAbs(30.0, 1e-3));
    s.handle_pointer(move({50, 10})); // 25, halfway: the lower step wins
    CHECK_THAT(s.value(), WithinAbs(20.0, 1e-3));
    s.handle_pointer(up({50, 10}));

    // Keyboard stepping is step(), not step_frequency().
    s.set_step(1.0f);
    ui::testing::set_focused(s, true);
    s.handle_key({Key::ArrowRight, KeyAction::Down});
    CHECK_THAT(s.value(), WithinAbs(21.0, 1e-3));
}

TEST_CASE("Snapping counts multiples from zero and clamps them into the range", "[slider][input][snap]")
{
    SliderControl s{Orientation::Horizontal, 5.0f, 95.0f, 50.0f};
    s.arrange({0, 0, 90, 20}); // one value unit per pixel
    s.set_step_frequency(10.0f);

    s.handle_pointer(down({88.5f, 10})); // 93.5: 100 clamps to 95, which is nearer than 90
    CHECK_THAT(s.value(), WithinAbs(95.0, 1e-3));
    s.handle_pointer(move({1, 10})); // 6: 0 clamps to 5, which is nearer than 10
    CHECK_THAT(s.value(), WithinAbs(5.0, 1e-3));
    s.handle_pointer(move({13, 10})); // 18 -> 20, not 5 + 10 = 15
    CHECK_THAT(s.value(), WithinAbs(20.0, 1e-3));
}

TEST_CASE("SliderSnapsTo::Ticks snaps a drag to ticks and steps the arrow keys by one tick", "[slider][input][snap]")
{
    SliderControl s{Orientation::Horizontal, 0.0f, 90.0f, 0.0f};
    s.arrange({0, 0, 90, 20});
    s.set_step_frequency(10.0f);
    s.set_tick_frequency(20.0f);
    s.set_snaps_to(SliderSnapsTo::Ticks);

    s.handle_pointer(down({33, 10})); // 33 -> 40, not the step_frequency's 30
    CHECK_THAT(s.value(), WithinAbs(40.0, 1e-3));
    s.handle_pointer(up({33, 10}));

    ui::testing::set_focused(s, true);
    s.handle_key({Key::ArrowRight, KeyAction::Down}); // by the tick, not by step()
    CHECK_THAT(s.value(), WithinAbs(60.0, 1e-3));

    // 90 is not a tick, so stepping down from it lands on the last tick below, not on 70.
    s.handle_key({Key::End, KeyAction::Down});
    s.handle_key({Key::ArrowLeft, KeyAction::Down});
    CHECK_THAT(s.value(), WithinAbs(80.0, 1e-3));
    s.handle_key({Key::ArrowLeft, KeyAction::Down});
    CHECK_THAT(s.value(), WithinAbs(60.0, 1e-3));

    // With no tick frequency there is nothing to snap to, and the step values take over again.
    s.set_tick_frequency(0.0f);
    s.handle_pointer(down({33, 10}));
    CHECK_THAT(s.value(), WithinAbs(30.0, 1e-3));
    s.handle_pointer(up({33, 10}));
}

namespace
{
// Every one-pixel mark a Fluent-look slider (Theme::light()) draws across its travel axis.
std::vector<FillRoundedRect> tick_marks(SliderTickPlacement placement,
                                        float               frequency,
                                        Orientation         orientation = Orientation::Horizontal)
{
    const bool    horizontal = orientation == Orientation::Horizontal;
    SliderControl slider{orientation, 0.0f, 100.0f, 50.0f};
    slider.set_tick_frequency(frequency);
    slider.set_tick_placement(placement);
    slider.arrange(horizontal ? Rect{0, 0, 200, 32} : Rect{0, 0, 32, 200});

    TestRenderer renderer;
    slider.render(renderer, Theme::light());
    std::vector<FillRoundedRect> out;
    for (const FillRoundedRect& fill : rounded_fills(renderer))
    {
        if ((horizontal ? fill.bounds.width : fill.bounds.height) == 1.0f)
        {
            out.push_back(fill);
        }
    }
    return out;
}
} // namespace

TEST_CASE("A Fluent Slider draws its ticks where tick_placement puts them", "[slider][render][ticks]")
{
    // 0-100 every 20 across 200px: six ticks, one under the thumb's center at each multiple.
    const auto outside = tick_marks(SliderTickPlacement::Outside, 20.0f);
    REQUIRE(outside.size() == 12);
    const auto above = tick_marks(SliderTickPlacement::TopLeft, 20.0f);
    REQUIRE(above.size() == 6);
    CHECK(above[0].bounds.x == -0.5f);
    CHECK(above[1].bounds.x == 39.5f);
    CHECK(above[5].bounds.x == 199.5f);
    // 4px tall tick marks held 4px off the 4px track centered in 32.
    CHECK(above[0].bounds.y == 6.0f);
    CHECK(above[0].bounds.height == 4.0f);
    const auto below = tick_marks(SliderTickPlacement::BottomRight, 20.0f);
    REQUIRE(below.size() == 6);
    CHECK(below[0].bounds.y == 22.0f);

    // Inline notches cross the track itself, in the field color rather than the track's.
    const auto inline_ticks = tick_marks(SliderTickPlacement::Inline, 20.0f);
    REQUIRE(inline_ticks.size() == 6);
    CHECK(inline_ticks[0].bounds.y == 14.0f);
    CHECK(inline_ticks[0].bounds.height == 4.0f);
    CHECK(inline_ticks[0].color == Theme::light().colors().background);

    CHECK(tick_marks(SliderTickPlacement::None, 20.0f).empty());
    CHECK(tick_marks(SliderTickPlacement::Outside, 0.0f).empty()); // nothing is drawn at 0
}

TEST_CASE("Fluent ticks skip a partial last interval and thin out below a 20px gap", "[slider][render][ticks]")
{
    // Every 30 over 0-100 is three whole intervals: ticks at 0, 30, 60 and 90, none at 100.
    const auto partial = tick_marks(SliderTickPlacement::TopLeft, 30.0f);
    REQUIRE(partial.size() == 4);
    CHECK(partial[3].bounds.x == 179.5f);

    // Every 1 is 2px apart across 200px; the tick row keeps every tenth so they sit 20px apart.
    const auto dense = tick_marks(SliderTickPlacement::TopLeft, 1.0f);
    REQUIRE(dense.size() == 11);
    CHECK(dense[1].bounds.x == 19.5f);

    // A vertical slider starts at the bottom, and TopLeft puts its row on the left.
    const auto vertical = tick_marks(SliderTickPlacement::TopLeft, 20.0f, Orientation::Vertical);
    REQUIRE(vertical.size() == 6);
    CHECK(vertical[0].bounds.y == 199.5f);
    CHECK(vertical[0].bounds.x == 6.0f);
    CHECK(vertical[0].bounds.width == 4.0f);
}

TEST_CASE("SliderTickPlacement::None takes the classic trackbar's ticks away, end ticks included",
          "[slider][retro][ticks]")
{
    SliderControl        slider{Orientation::Horizontal, 0.0f, 100.0f, 50.0f};
    HeadlessTextMeasurer measurer;
    slider.set_tick_frequency(10.0f);
    (void)slider.measure(Size{160.0f, 32.0f}, measurer, retro());
    slider.arrange({0.0f, 0.0f, 160.0f, 32.0f});

    const auto tick_count = [&]
    {
        TestRenderer renderer;
        slider.render(renderer, retro());
        std::size_t n = 0;
        for (const FillRoundedRect& fill : rounded_fills(renderer))
        {
            n += fill.bounds.y == 26.0f ? 1 : 0; // the tick row, see the stock-trackbar test
        }
        return n;
    };
    CHECK(tick_count() == 11);
    slider.set_tick_placement(SliderTickPlacement::None);
    CHECK(tick_count() == 0);
}

TEST_CASE("direction_reversed puts min at the far end for the thumb, a click and the keys", "[slider]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();

    SliderControl h(Orientation::Horizontal, 0.0f, 100.0f, 0.0f);
    h.set_direction_reversed(true);
    (void)h.measure(Size{200.0f, Unbounded}, measurer, theme);
    h.arrange(Rect{0.0f, 0.0f, 200.0f, 32.0f});
    const Rect b = h.bounds();

    auto thumb_x = [](const TestRenderer& rr)
    {
        for (const auto& c : rr.commands)
        {
            if (const auto* e = std::get_if<FillEllipse>(&c))
            {
                return e->center.x;
            }
        }
        return -1.0f;
    };
    // At min the thumb sits at the right end.
    TestRenderer r;
    h.render(r, theme);
    h.set_value(100.0f);
    TestRenderer r_max;
    h.render(r_max, theme);
    CHECK(thumb_x(r) > thumb_x(r_max));
    CHECK(thumb_x(r_max) >= 0.0f);

    // A click near the left end is near max.
    h.handle_pointer(down(Point{b.x + 5.0f, b.center().y}));
    h.handle_pointer(up(Point{b.x + 5.0f, b.center().y}));
    CHECK(h.value() > 90.0f);

    // The keys: Left and Down increase, Right and Up decrease.
    h.set_value(50.0f);
    ui::testing::set_focused(h, true);
    h.handle_key({Key::ArrowLeft, KeyAction::Down});
    CHECK(h.value() > 50.0f);
    h.handle_key({Key::ArrowUp, KeyAction::Down});
    h.handle_key({Key::ArrowUp, KeyAction::Down});
    CHECK(h.value() < 50.0f);
}

TEST_CASE("An Aero slider is a glossy block on a groove, pointed toward one-sided ticks", "[slider][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    REQUIRE(ui::detail::controls(theme).slider_thumb.shape == SliderThumbShape::ButtonBlock);

    SliderControl        slider{Orientation::Horizontal, 0.0f, 100.0f, 0.0f};
    HeadlessTextMeasurer measurer;
    (void)slider.measure({200.0f, Unbounded}, measurer, theme);
    slider.arrange({0.0f, 0.0f, 200.0f, 32.0f});

    const auto render = [&]
    {
        TestRenderer r;
        slider.render(r, theme);
        return r;
    };

    // At its minimum the block's center is 5 + 5.5 in: the track's inset plus half the block.
    TestRenderer r     = render();
    bool         block = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* g = std::get_if<FillRoundedRectGradient>(&c);
            g && g->brush == *ui::detail::controls(theme).slider_block->thumb_rest.background_gradient)
        {
            CHECK(g->bounds == Rect{5.0f, 5.0f, 11.0f, 22.0f});
            block = true;
        }
        // No circles, and no accent value fill: the groove is `#E7EAEA` from end to end.
        CHECK_FALSE(std::holds_alternative<FillEllipse>(c));
        if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->bounds.height == 2.0f)
        {
            CHECK(f->color == Color::from_argb(0xFFE7EAEA));
            CHECK(f->bounds.width == 188.0f); // 200 less the 5px insets and the 1px border each end
        }
    }
    CHECK(block);

    // Keyboard focus shows the hover face.
    ui::testing::set_focused(slider, true);
    bool hover_face = false;
    for (const RenderCommand& c : render().commands)
    {
        if (const auto* g = std::get_if<FillRoundedRectGradient>(&c);
            g && g->brush == *ui::detail::controls(theme).slider_block->thumb_hover.background_gradient)
        {
            hover_face = true;
        }
    }
    CHECK(hover_face);
    ui::testing::set_focused(slider, false);

    // Ticks below: `#C4C4C4` marks, and the block becomes a pointer (a filled polygon) aimed at them.
    slider.set_tick_frequency(20.0f); // far enough apart that none is thinned out
    slider.set_tick_placement(SliderTickPlacement::BottomRight);
    TestRenderer ticked = render();
    int          ticks  = 0;
    for (const RenderCommand& c : ticked.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->color == Color::from_argb(0xFFC4C4C4))
        {
            CHECK(f->bounds.top() == 27.0f); // just past the thumb's 22px row
            ++ticks;
        }
    }
    CHECK(ticks == 6);
    CHECK(ticked.count_of<DrawGeometry>() == 3); // face, white ring, outline
}

TEST_CASE("A two-way value the slider clamps is written back, at build and when already clamped",
          "[slider][spec][binding]")
{
    Property<double>         v{150.0};
    int                      handled = 0;
    ui::Control<ui::Slider>  slider;
    std::unique_ptr<Element> node = ui::build(
        ui::Slider{.value = ui::bind(v), .min = 0.0, .max = 100.0, .on_value_changed = [&](double) { ++handled; }, .control = slider});
    CHECK(slider->value() == 100.0);
    CHECK(v.get() == 100.0); // the build-time clamp went back
    CHECK(handled == 0);     // seeding never runs the spec's handler

    v = 250.0; // the control already holds 100: nothing changes there
    CHECK(slider->value() == 100.0);
    CHECK(v.get() == 100.0);
    CHECK(handled == 0);

    v = 40.0;
    CHECK(slider->value() == 40.0);
    v = -3.0; // a real change, clamped
    CHECK(slider->value() == 0.0);
    CHECK(v.get() == 0.0);
}

// ---- the focus visual ----

namespace
{
const StrokeRoundedRect* outer_ring(const TestRenderer& renderer)
{
    const StrokeRoundedRect* ring = nullptr;
    for (const RenderCommand& command : renderer.commands)
    {
        if (const auto* stroke = std::get_if<StrokeRoundedRect>(&command); stroke != nullptr && stroke->stroke_width == 2.0f)
        {
            ring = stroke;
        }
    }
    return ring;
}
} // namespace

TEST_CASE("A focused Fluent Slider rings its whole track container, 7 out at the sides", "[slider][focus]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    SliderControl s{Orientation::Horizontal};
    (void)s.measure({Unbounded, Unbounded}, m, theme);
    s.arrange(Rect{50.0f, 50.0f, 300.0f, 60.0f});
    ui::testing::set_focused(s, true);
    ui::testing::set_focus_visible(s, true);

    TestRenderer renderer;
    ui::testing::render_with_focus_ring(s, renderer, theme);
    const StrokeRoundedRect* ring = outer_ring(renderer);
    REQUIRE(ring != nullptr);
    // The container is the 32 DIP row through the thumb's center, as long as the slider (the thumb
    // stays inside it). The outer ring's 2 DIP path is centered 1 inside its outer edge: 6 out at
    // the sides, 1 in at the top and bottom.
    const Rect b = s.bounds();
    CHECK_THAT(ring->bounds.x, WithinAbs(b.x - 6.0f, 1e-3));
    CHECK_THAT(ring->bounds.width, WithinAbs(b.width + 12.0f, 1e-3));
    CHECK_THAT(ring->bounds.y, WithinAbs(b.y + b.height * 0.5f - 16.0f + 1.0f, 1e-3));
    CHECK_THAT(ring->bounds.height, WithinAbs(30.0f, 1e-3));
}

TEST_CASE("A vertical Slider's ring goes 7 out across the track and flush along it", "[slider][focus]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    SliderControl s{Orientation::Vertical};
    (void)s.measure({Unbounded, Unbounded}, m, theme);
    s.arrange(Rect{50.0f, 50.0f, 60.0f, 300.0f});
    ui::testing::set_focused(s, true);
    ui::testing::set_focus_visible(s, true);

    TestRenderer renderer;
    ui::testing::render_with_focus_ring(s, renderer, theme);
    const StrokeRoundedRect* ring = outer_ring(renderer);
    REQUIRE(ring != nullptr);
    const Rect b = s.bounds();
    // The 32 DIP column through the thumb's center, the ring's path 6 out from it.
    CHECK_THAT(ring->bounds.x, WithinAbs(b.x + b.width * 0.5f - 16.0f - 6.0f, 1e-3));
    CHECK_THAT(ring->bounds.width, WithinAbs(32.0f + 12.0f, 1e-3));
    CHECK_THAT(ring->bounds.y, WithinAbs(b.y + 1.0f, 1e-3));
    CHECK_THAT(ring->bounds.height, WithinAbs(b.height - 2.0f, 1e-3));
}
