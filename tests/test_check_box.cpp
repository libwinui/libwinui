// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/VisualStates.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cmath>

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
PointerEvent leave()
{
    return {{}, PointerButton::None, PointerAction::Leave};
}
KeyEvent key_down(Key k)
{
    return {k, KeyAction::Down};
}
KeyEvent key_up(Key k)
{
    return {k, KeyAction::Up};
}

CheckBoxControl make_check_box()
{
    CheckBoxControl c{"Remember me"};
    c.arrange({100, 100, 140, 24});
    return c;
}
const Point Inside{110, 110};
const Point Outside{10, 10};
} // namespace

TEST_CASE("New checkbox is unchecked, Normal, and enabled", "[checkbox]")
{
    CheckBoxControl c = make_check_box();
    REQUIRE(c.enabled());
    REQUIRE(c.checked() == false);
    REQUIRE_FALSE(c.focused());
    REQUIRE(ui::testing::visual_state(c) == ButtonState::Normal);
}

TEST_CASE("Constructing with checked=true starts checked", "[checkbox]")
{
    CheckBoxControl c{"On by default", true};
    REQUIRE(c.checked() == true);
}

TEST_CASE("Hover state follows the pointer", "[checkbox][state]")
{
    CheckBoxControl c = make_check_box();

    c.handle_pointer(move(Inside));
    REQUIRE(ui::testing::visual_state(c) == ButtonState::Hover);

    c.handle_pointer(move(Outside));
    REQUIRE(ui::testing::visual_state(c) == ButtonState::Normal);

    c.handle_pointer(move(Inside));
    c.handle_pointer(leave());
    REQUIRE(ui::testing::visual_state(c) == ButtonState::Normal);
}

TEST_CASE("Click toggles checked and fires on_checked_changed", "[checkbox][command]")
{
    CheckBoxControl                  c = make_check_box();
    std::vector<std::optional<bool>> observed;
    c.on_checked_changed([&](std::optional<bool> v) { observed.push_back(v); });

    c.handle_pointer(move(Inside));
    c.handle_pointer(down(Inside));
    REQUIRE(ui::testing::visual_state(c) == ButtonState::Pressed);

    c.handle_pointer(up(Inside));
    REQUIRE(c.checked() == true);
    REQUIRE(observed == std::vector<std::optional<bool>>{true});

    c.handle_pointer(down(Inside));
    c.handle_pointer(up(Inside));
    REQUIRE(c.checked() == false);
    REQUIRE(observed == (std::vector<std::optional<bool>>{true, false}));
}

TEST_CASE("Release outside the checkbox cancels the toggle", "[checkbox][command]")
{
    CheckBoxControl c       = make_check_box();
    int             toggles = 0;
    c.on_checked_changed([&](std::optional<bool>) { ++toggles; });

    c.handle_pointer(down(Inside));
    c.handle_pointer(move(Outside));
    REQUIRE(ui::testing::visual_state(c) != ButtonState::Pressed);
    c.handle_pointer(up(Outside));

    REQUIRE(toggles == 0);
    REQUIRE(c.checked() == false);
}

TEST_CASE("Keyboard activation requires focus and uses Space only, not Enter", "[checkbox][command]")
{
    CheckBoxControl c = make_check_box();

    c.handle_key(key_down(Key::Space));
    c.handle_key(key_up(Key::Space));
    REQUIRE(c.checked() == false);

    ui::testing::set_focused(c, true);
    c.handle_key(key_down(Key::Enter));
    c.handle_key(key_up(Key::Enter));
    REQUIRE(c.checked() == false); // Enter does not activate a checkbox

    c.handle_key(key_down(Key::Space));
    REQUIRE(ui::testing::visual_state(c) == ButtonState::Pressed);
    c.handle_key(key_up(Key::Space));
    REQUIRE(c.checked() == true);
}

TEST_CASE("Losing focus cancels an in-progress keyboard press", "[checkbox][state]")
{
    CheckBoxControl c = make_check_box();
    ui::testing::set_focused(c, true);
    c.handle_key(key_down(Key::Space));
    REQUIRE(c.is_pressed());

    ui::testing::set_focused(c, false);
    REQUIRE_FALSE(c.is_pressed());
    c.handle_key(key_up(Key::Space)); // arrives after focus loss
    REQUIRE(c.checked() == false);
}

TEST_CASE("Disabled checkbox ignores input and reports Disabled", "[checkbox][state]")
{
    CheckBoxControl c = make_check_box();
    c.set_enabled(false);

    c.handle_pointer(move(Inside));
    c.handle_pointer(down(Inside));
    c.handle_pointer(up(Inside));
    ui::testing::set_focused(c, true);
    c.handle_key(key_down(Key::Space));
    c.handle_key(key_up(Key::Space));

    REQUIRE(c.checked() == false);
    REQUIRE(ui::testing::visual_state(c) == ButtonState::Disabled);
}

TEST_CASE("set_checked() sets state directly and fires the callback only on a real change", "[checkbox]")
{
    CheckBoxControl c     = make_check_box();
    int             fired = 0;
    c.on_checked_changed([&](std::optional<bool>) { ++fired; });

    c.set_checked(true);
    REQUIRE(c.checked() == true);
    REQUIRE(fired == 1);

    c.set_checked(true); // no change
    REQUIRE(fired == 1);
}

// ---- the third value ----

TEST_CASE("A three-state checkbox cycles unchecked, checked, indeterminate", "[checkbox][command]")
{
    CheckBoxControl c = make_check_box();
    c.set_three_state(true);
    std::vector<std::optional<bool>> observed;
    c.on_checked_changed([&](std::optional<bool> v) { observed.push_back(v); });

    auto click = [&]
    {
        c.handle_pointer(down(Inside));
        c.handle_pointer(up(Inside));
    };
    click();
    REQUIRE(c.checked() == true);
    click();
    REQUIRE_FALSE(c.checked().has_value());
    click();
    REQUIRE(c.checked() == false);
    click();
    REQUIRE(c.checked() == true);
    REQUIRE(observed == (std::vector<std::optional<bool>>{true, std::nullopt, false, true}));
}

TEST_CASE("Without three_state a click never reaches indeterminate, but code can set it", "[checkbox][command]")
{
    CheckBoxControl c = make_check_box();
    REQUIRE_FALSE(c.three_state());
    int fired = 0;
    c.on_checked_changed([&](std::optional<bool>) { ++fired; });

    c.set_checked(true);
    c.handle_pointer(down(Inside));
    c.handle_pointer(up(Inside));
    REQUIRE(c.checked() == false); // checked clears, as a two-state box does

    c.set_checked(std::nullopt);
    REQUIRE_FALSE(c.checked().has_value());
    REQUIRE(fired == 3);
    c.set_checked(std::nullopt); // no change
    REQUIRE(fired == 3);

    // A two-state box checks from indeterminate, as "Select all" over a partial selection does.
    ui::testing::set_focused(c, true);
    c.handle_key(key_down(Key::Space));
    c.handle_key(key_up(Key::Space));
    REQUIRE(c.checked() == true);
}

TEST_CASE("Indeterminate is reported to UI Automation, and Toggle() follows the click cycle", "[checkbox][automation]")
{
    CheckBoxControl c{"Select all", std::nullopt};
    REQUIRE(ui::automation_providers(c).toggle->toggle_value() == ToggleState::Indeterminate);

    c.set_three_state(true);
    REQUIRE(ui::automation_providers(c).toggle->toggle_next());
    REQUIRE(ui::automation_providers(c).toggle->toggle_value() == ToggleState::Off);
    REQUIRE(ui::automation_providers(c).toggle->toggle_next());
    REQUIRE(ui::automation_providers(c).toggle->toggle_value() == ToggleState::On);
    REQUIRE(ui::automation_providers(c).toggle->toggle_next());
    REQUIRE(ui::automation_providers(c).toggle->toggle_value() == ToggleState::Indeterminate);
}

TEST_CASE("A focused CheckBox rings its box and label together, not its whole slot", "[checkbox][focus]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();
    CheckBoxControl      box{"Remember me"};
    const Size           desired = box.measure({Unbounded, Unbounded}, measurer, theme);
    // Stretched across a slot much wider than its content.
    box.arrange(Rect{20.0f, 20.0f, 400.0f, desired.height});
    ui::testing::set_focused(box, true);
    ui::testing::set_focus_visible(box, true);

    TestRenderer renderer;
    ui::testing::render_with_focus_ring(box, renderer, theme);
    const StrokeRoundedRect* ring = nullptr;
    for (const RenderCommand& command : renderer.commands)
    {
        if (const auto* stroke = std::get_if<StrokeRoundedRect>(&command); stroke != nullptr && stroke->stroke_width == 2.0f)
        {
            ring = stroke;
        }
    }
    REQUIRE(ring != nullptr);
    // The outer ring's path is 6 outside the content at the sides (7 less half its width) and
    // 2 above and below.
    const float content = std::min(box.bounds().width, desired.width);
    CHECK(ring->bounds.x == box.bounds().x - 6.0f);
    CHECK(ring->bounds.y == box.bounds().y - 2.0f);
    CHECK(ring->bounds.height == box.bounds().height + 4.0f);
    CHECK(ring->bounds.width < 400.0f); // the label's width, not the slot's
    CHECK(ring->bounds.width == content + 12.0f);
}

// ---- the check mark's motion ----

namespace
{
// What the box shows of its tick: the stroked length of one being drawn in or erased, and
// whether the resting glyph is drawn.
struct Mark
{
    float stroked = 0.0f;
    bool  glyph   = false;
};

Mark mark_of(const CheckBoxControl& c, const Theme& theme)
{
    TestRenderer r;
    c.render(r, theme);
    Mark m;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* g = std::get_if<DrawGeometry>(&command))
        {
            for (const PathFigure& figure : g->geometry.figures)
            {
                Point at = figure.start;
                for (const PathSegment& segment : figure.segments)
                {
                    m.stroked += std::hypot(segment.end().x - at.x, segment.end().y - at.y);
                    at = segment.end();
                }
            }
        }
        else if (const auto* t = std::get_if<DrawTextRun>(&command); t && t->text != c.text())
        {
            m.glyph = true;
        }
    }
    return m;
}

void click(CheckBoxControl& c)
{
    c.handle_pointer(down(Inside));
    c.handle_pointer(up(Inside));
}

// The whole tick in a 20 DIP box: its 48-unit canvas scaled to the box, then by 0.7 and 1.05.
const float TickLength = (std::hypot(10.172f, 10.172f) + std::hypot(20.337f, 20.525f)) * 20.0f / 48.0f * 0.735f;
} // namespace

TEST_CASE("A Fluent CheckBox draws its tick in over about 317 ms", "[checkbox][animation]")
{
    const Theme     theme = Theme::light();
    CheckBoxControl c     = make_check_box();
    c.tick(0ms); // in a live tree

    click(c);
    REQUIRE(c.checked() == true);
    CHECK(mark_of(c, theme).stroked == 0.0f); // nothing yet, and no resting glyph either
    CHECK_FALSE(mark_of(c, theme).glyph);

    c.tick(50ms);
    const float early = mark_of(c, theme).stroked;
    CHECK(early > 0.0f);
    CHECK(early < TickLength * 0.5f);

    c.tick(110ms);
    const float half = mark_of(c, theme).stroked;
    CHECK(half > early);
    CHECK(half < TickLength - 0.01f);
    CHECK_FALSE(mark_of(c, theme).glyph);

    c.tick(160ms); // 320 ms in: done, and the box rests on its glyph
    CHECK(mark_of(c, theme).stroked == 0.0f);
    CHECK(mark_of(c, theme).glyph);
}

TEST_CASE("Checked while still held down, the tick draws in over 150 ms", "[checkbox][animation]")
{
    const Theme     theme = Theme::light();
    CheckBoxControl c     = make_check_box();
    c.tick(0ms);
    c.handle_pointer(down(Inside));
    c.set_checked(true);
    c.tick(100ms);
    CHECK(mark_of(c, theme).stroked > 0.0f);
    c.tick(55ms);
    CHECK(mark_of(c, theme).glyph);
}

TEST_CASE("A Fluent CheckBox erases its tick within about 67 ms", "[checkbox][animation]")
{
    const Theme     theme = Theme::light();
    CheckBoxControl c{"Remember me", true};
    c.arrange({100, 100, 140, 24});
    c.tick(0ms);

    click(c);
    REQUIRE(c.checked() == false);
    CHECK(mark_of(c, theme).stroked > TickLength - 0.01f); // erasing from the start of the stroke
    c.tick(30ms);
    const Mark part = mark_of(c, theme);
    CHECK(part.stroked > 0.0f);
    CHECK(part.stroked < TickLength * 0.75f);
    CHECK_FALSE(part.glyph);
    c.tick(40ms);
    CHECK(mark_of(c, theme).stroked == 0.0f);
    CHECK_FALSE(mark_of(c, theme).glyph);
}

TEST_CASE("Indeterminate comes and goes without motion", "[checkbox][animation]")
{
    const Theme     theme = Theme::light();
    CheckBoxControl c{"Remember me", true};
    c.set_three_state(true);
    c.arrange({100, 100, 140, 24});
    c.tick(0ms);

    click(c); // checked -> indeterminate
    REQUIRE_FALSE(c.checked().has_value());
    CHECK(mark_of(c, theme).stroked == 0.0f);
    c.tick(16ms);
    CHECK(mark_of(c, theme).stroked == 0.0f);

    click(c); // indeterminate -> unchecked
    REQUIRE(c.checked() == false);
    CHECK(mark_of(c, theme).stroked == 0.0f);
}

TEST_CASE("The tick lands at once outside a live tree, under the other eras, and with animations off",
          "[checkbox][animation]")
{
    SECTION("never ticked")
    {
        CheckBoxControl c = make_check_box();
        click(c);
        CHECK(mark_of(c, Theme::light()).glyph);
    }
    SECTION("an era whose mark simply appears")
    {
        const Theme aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
        REQUIRE_FALSE(ui::detail::controls(aero).check_mark_draws_in);
        CheckBoxControl c = make_check_box();
        c.tick(0ms);
        click(c);
        CHECK(mark_of(c, aero).stroked == 0.0f);
        CHECK(mark_of(c, aero).glyph);
    }
    SECTION("animations off")
    {
        HeadlessTextMeasurer measurer;
        Window               window;
        window.set_theme(ThemeRequest{.animations = AnimationPreference::Off});
        auto owned = std::make_unique<CheckBoxControl>("Remember me");
        owned->set_horizontal_alignment(Alignment::Leading);
        owned->set_vertical_alignment(Alignment::Leading);
        CheckBoxControl* c = window.set_content(std::move(owned));
        window.layout({400.0f, 300.0f}, measurer);
        window.tick(16ms);

        const Point center = c->bounds().center();
        seams(window).handle_pointer(down(center));
        seams(window).handle_pointer(up(center));
        REQUIRE(c->checked() == true);
        CHECK(mark_of(*c, window.theme()).stroked == 0.0f);
        CHECK(mark_of(*c, window.theme()).glyph);
    }
}
