// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/VisualStates.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;

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

ToggleSwitchControl make_toggle()
{
    ToggleSwitchControl t;
    t.arrange({100, 100, 120, 24});
    return t;
}
const Point Inside{110, 110};
const Point Outside{10, 10};
} // namespace

TEST_CASE("New toggle switch is off, Normal, and enabled", "[toggle]")
{
    ToggleSwitchControl t = make_toggle();
    REQUIRE(t.enabled());
    REQUIRE_FALSE(t.checked());
    REQUIRE(ui::testing::visual_state(t) == ButtonState::Normal);
}

TEST_CASE("Constructing with on=true starts on", "[toggle]")
{
    ToggleSwitchControl t{true};
    REQUIRE(t.checked());
}

TEST_CASE("Click toggles on/off and fires on_checked_changed", "[toggle][command]")
{
    ToggleSwitchControl t = make_toggle();
    std::vector<bool>   observed;
    t.on_checked_changed([&](bool v) { observed.push_back(v); });

    t.handle_pointer(down(Inside));
    t.handle_pointer(up(Inside));
    REQUIRE(t.checked());
    REQUIRE(observed == std::vector<bool>{true});

    t.handle_pointer(down(Inside));
    t.handle_pointer(up(Inside));
    REQUIRE_FALSE(t.checked());
    REQUIRE(observed == (std::vector<bool>{true, false}));
}

TEST_CASE("Release outside cancels the toggle", "[toggle][command]")
{
    ToggleSwitchControl t       = make_toggle();
    int                 toggles = 0;
    t.on_checked_changed([&](bool) { ++toggles; });

    t.handle_pointer(down(Inside));
    t.handle_pointer(move(Outside));
    t.handle_pointer(up(Outside));

    REQUIRE(toggles == 0);
}

TEST_CASE("Space toggles while focused; Enter does not", "[toggle][command]")
{
    ToggleSwitchControl t = make_toggle();
    ui::testing::set_focused(t, true);

    t.handle_key({Key::Enter, KeyAction::Down});
    t.handle_key({Key::Enter, KeyAction::Up});
    REQUIRE_FALSE(t.checked());

    t.handle_key({Key::Space, KeyAction::Down});
    REQUIRE(t.is_pressed());
    t.handle_key({Key::Space, KeyAction::Up});
    REQUIRE(t.checked());
}

TEST_CASE("Disabled toggle switch ignores input", "[toggle][state]")
{
    ToggleSwitchControl t = make_toggle();
    t.set_enabled(false);

    t.handle_pointer(down(Inside));
    t.handle_pointer(up(Inside));
    ui::testing::set_focused(t, true);
    t.handle_key({Key::Space, KeyAction::Down});
    t.handle_key({Key::Space, KeyAction::Up});

    REQUIRE_FALSE(t.checked());
    REQUIRE(ui::testing::visual_state(t) == ButtonState::Disabled);
}

namespace
{
std::string drawn_label(const ToggleSwitchControl& toggle)
{
    TestRenderer renderer;
    toggle.render(renderer, Theme::light());
    for (const RenderCommand& command : renderer.commands)
    {
        if (const auto* text = std::get_if<DrawTextRun>(&command))
        {
            return text->text;
        }
    }
    return {};
}
} // namespace

TEST_CASE("A ToggleSwitch says On and Off by default", "[toggle][content]")
{
    ToggleSwitchControl toggle;
    toggle.arrange({0, 0, 200, 24});
    CHECK(toggle.checked_text() == "On");
    CHECK(toggle.unchecked_text() == "Off");
    CHECK(drawn_label(toggle) == "Off");
    toggle.set_checked(true);
    CHECK(drawn_label(toggle) == "On");
    CHECK(toggle.automation_name() == "On"); // no header: the label showing names it

    toggle.set_checked_text("");
    toggle.set_unchecked_text("");
    CHECK(drawn_label(toggle).empty());
}

TEST_CASE("checked_text and unchecked_text label each state", "[toggle][content]")
{
    ToggleSwitchControl toggle;
    toggle.set_checked_text("Working");
    toggle.set_unchecked_text("Do work");
    toggle.arrange({0, 0, 200, 24});

    CHECK(drawn_label(toggle) == "Do work");
    toggle.set_checked(true);
    CHECK(drawn_label(toggle) == "Working");
}

TEST_CASE("A ToggleSwitch is as wide as its longer label in either state", "[toggle][content][layout]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();

    // Long enough to clear the switch's minimum width, so the label is what sizes it.
    ToggleSwitchControl toggle;
    toggle.set_checked_text("On");
    toggle.set_unchecked_text("Switched off for the night");
    const Size off = toggle.measure({Unbounded, Unbounded}, measurer, theme);
    REQUIRE(off.width > 154.0f);
    toggle.set_checked(true);
    CHECK(toggle.measure({Unbounded, Unbounded}, measurer, theme).width == off.width);
}

TEST_CASE("A ToggleSwitch header sits above the track and names the switch", "[toggle][header]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();

    ToggleSwitchControl plain;
    const Size          bare = plain.measure({Unbounded, Unbounded}, measurer, theme);

    ToggleSwitchControl headed;
    headed.set_header("Toggle work");
    const Size  size = headed.measure({Unbounded, Unbounded}, measurer, theme);
    const float line = measurer.measure("Toggle work", theme.text_style).height;
    CHECK(size.height == bare.height + line + 4.0f); // the 4 DIP gap under the header
    plain.arrange({0, 0, bare.width, bare.height});
    headed.arrange({0, 0, size.width, size.height});
    // bounds() is the switch itself, below its header; both sit inside the same focus-ring band.
    CHECK(headed.bounds().y == plain.bounds().y + line + 4.0f);
    CHECK(headed.automation_name() == "Toggle work");

    TestRenderer renderer;
    headed.render(renderer, theme);
    const auto& first = std::get<DrawTextRun>(renderer.commands.front());
    CHECK(first.text == "Toggle work");
    CHECK(first.bounds.y == plain.bounds().y);

    headed.set_enabled(false);
    TestRenderer grayed;
    headed.render(grayed, theme);
    CHECK(std::get<DrawTextRun>(grayed.commands.front()).color == theme.colors().text_disabled);
}

TEST_CASE("An icon can stand in for either state's text", "[toggle][content]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();

    ToggleSwitchControl toggle;
    toggle.set_unchecked_icon(glyphs::ChevronDown);
    toggle.set_checked_icon(glyphs::ChevronUp);
    toggle.set_min_width(0.0f);
    const Size size = toggle.measure({Unbounded, Unbounded}, measurer, theme);
    // Track, spacing, and one glyph: the "On"/"Off" text is not measured while an icon replaces it.
    CHECK(size.width == ui::detail::controls(theme).toggle_switch_track_size.width +
                            ui::detail::controls(theme).toggle_content_spacing + glyphs::ChevronDown.size);
    toggle.arrange({0, 0, size.width, size.height});

    const auto drawn = [&]
    {
        TestRenderer renderer;
        toggle.render(renderer, theme);
        std::vector<DrawTextRun> runs;
        for (const RenderCommand& command : renderer.commands)
        {
            if (const auto* run = std::get_if<DrawTextRun>(&command))
            {
                runs.push_back(*run);
            }
        }
        return runs;
    };
    auto runs = drawn();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].text == encode_utf8(glyphs::ChevronDown.codepoint));
    CHECK(runs[0].style.font_family == theme.icon_font_family);

    toggle.set_checked(true);
    runs = drawn();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].text == encode_utf8(glyphs::ChevronUp.codepoint));
    CHECK(toggle.automation_name() == "On"); // the text still names the state

    ui::Control<ui::ToggleSwitch> built;
    std::unique_ptr<Element> tree = ui::build(ui::ToggleSwitch{.checked_icon = glyphs::ChevronUp, .control = built});
    CHECK(built->checked_icon() == glyphs::ChevronUp);
    CHECK(built->unchecked_icon().empty());
}

TEST_CASE("An Aero ToggleSwitch is a text-field track with a push-button knob that slides", "[toggle][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    REQUIRE(ui::detail::controls(theme).toggle_switch_shape == ToggleSwitchShape::ButtonKnob);

    ToggleSwitchControl t;
    t.arrange({0.0f, 0.0f, 160.0f, 22.0f});

    // The knob is the only thing drawn with the button's face gradient; returns its rect.
    const auto knob = [&](const ButtonVisual& face)
    {
        TestRenderer r;
        t.render(r, theme);
        std::vector<Rect> rects;
        for (const RenderCommand& c : r.commands)
        {
            if (const auto* g = std::get_if<FillRoundedRectGradient>(&c); g && g->brush == *face.background_gradient)
            {
                rects.push_back(g->bounds);
            }
        }
        REQUIRE(rects.size() == 1);
        return rects.front();
    };

    // Off: a 32x16 pill 3px in from the left of the 56x22 track, over a white track.
    CHECK(knob(theme.button_normal) == Rect{3.0f, 3.0f, 32.0f, 16.0f});
    t.handle_pointer(move({10.0f, 10.0f}));
    CHECK(knob(theme.button_hover).x == 3.0f);
    t.handle_pointer({Point{-10.0f, -10.0f}, PointerButton::None, PointerAction::Leave});

    // With no host ticking, a toggle lands at the far end at once: 56 - 3 - 32 = 21.
    t.set_checked(true);
    CHECK(knob(theme.button_normal).x == 21.0f);

    // Once ticked it slides: part way after 50 ms, eased (past the linear 20 %), home by 250 ms.
    t.tick(0ms);
    t.set_checked(false);
    t.tick(50ms);
    const float mid = knob(theme.button_normal).x;
    CHECK(mid < 21.0f);
    CHECK(mid > 3.0f);
    CHECK(mid < 21.0f - 18.0f * 0.2f);
    t.tick(200ms);
    CHECK(knob(theme.button_normal).x == 3.0f);

    // The on track is filled `#C1DFF6` and ringed in the field's focus-blue gradient.
    t.set_checked(true);
    TestRenderer r;
    t.render(r, theme);
    bool on_track = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->bounds == Rect{0.0f, 0.0f, 56.0f, 22.0f})
        {
            on_track = f->color == Color::from_argb(0xFFC1DFF6);
        }
    }
    CHECK(on_track);
}
