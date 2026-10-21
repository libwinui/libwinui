// SPDX-License-Identifier: MIT
#include "testing/TestCompositor.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/themes/ClassicPalette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <optional>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;

namespace
{
PointerEvent move(Point p)
{
    return {p, PointerButton::None, PointerAction::Move};
}
PointerEvent leave()
{
    return {{}, PointerButton::None, PointerAction::Leave};
}

std::vector<AnimateOpacity> opacity_animations(const TestCompositor& c)
{
    std::vector<AnimateOpacity> out;
    for (const auto& cmd : c.commands())
    {
        if (const auto* a = std::get_if<AnimateOpacity>(&cmd))
        {
            out.push_back(*a);
        }
    }
    return out;
}
} // namespace

TEST_CASE("Button hover fades a compositor overlay in and out", "[composition][button]")
{
    ButtonControl b{"Hello"};
    b.arrange({100, 100, 100, 40});
    TestCompositor compositor;
    Theme          theme = Theme::light();

    ui::testing::ElementAccess::sync_composition(b, compositor, theme); // Normal -> Normal: no transition yet
    REQUIRE(opacity_animations(compositor).empty());

    b.handle_pointer(move({150, 120})); // now hot
    ui::testing::ElementAccess::sync_composition(b, compositor, theme);
    auto anims = opacity_animations(compositor);
    REQUIRE(anims.size() == 1);
    REQUIRE(anims[0].from == 0.0f);
    REQUIRE(anims[0].to > 0.0f);

    b.handle_pointer(leave()); // no longer hot
    ui::testing::ElementAccess::sync_composition(b, compositor, theme);
    anims = opacity_animations(compositor);
    REQUIRE(anims.size() == 2);
    REQUIRE(anims[1].from == anims[0].to);
    REQUIRE(anims[1].to == 0.0f);
}

TEST_CASE("Repeated sync_composition calls while hotness is unchanged don't re-animate", "[composition][button]")
{
    ButtonControl b{"Hello"};
    b.arrange({100, 100, 100, 40});
    TestCompositor compositor;
    Theme          theme = Theme::light();

    b.handle_pointer(move({150, 120}));
    ui::testing::ElementAccess::sync_composition(b, compositor, theme);
    REQUIRE(opacity_animations(compositor).size() == 1);

    b.handle_pointer(move({151, 121}));
    ui::testing::ElementAccess::sync_composition(b, compositor, theme);
    REQUIRE(opacity_animations(compositor).size() == 1);
}

TEST_CASE("Pressing a hovered button stays hot; releasing while still hovered stays hot too", "[composition][button]")
{
    ButtonControl b{"Hello"};
    b.arrange({100, 100, 100, 40});
    TestCompositor compositor;
    Theme          theme = Theme::light();

    b.handle_pointer(move({150, 120}));
    ui::testing::ElementAccess::sync_composition(b, compositor, theme); // hover fade-in

    b.handle_pointer({{150, 120}, PointerButton::Left, PointerAction::Down});
    ui::testing::ElementAccess::sync_composition(b, compositor, theme);
    b.handle_pointer({{150, 120}, PointerButton::Left, PointerAction::Up});
    ui::testing::ElementAccess::sync_composition(b, compositor, theme);

    REQUIRE(opacity_animations(compositor).size() == 1);
}

// ---- Metro: press is a hard flip, so the hover overlay must not also fire on it ----

TEST_CASE("Under a Fluent theme, a keyboard press with no prior hover still fades the overlay in",
          "[composition][button][metro]")
{
    ButtonControl b{"Hello"};
    b.arrange({100, 100, 100, 40});
    ui::testing::set_focused(b, true);
    TestCompositor compositor;
    Theme          theme = Theme::light(); // press_state_flip == false

    b.handle_key({Key::Space, KeyAction::Down}); // pressed via keyboard, never hovered
    ui::testing::ElementAccess::sync_composition(b, compositor, theme);

    REQUIRE(opacity_animations(compositor).size() == 1);
}

TEST_CASE("Under Metro, a keyboard press with no prior hover does not fade the overlay in",
          "[composition][button][metro]")
{
    ButtonControl b{"Hello"};
    b.arrange({100, 100, 100, 40});
    ui::testing::set_focused(b, true);
    TestCompositor compositor;
    const Theme    theme = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                         OsThemeSnapshot{}); // press_state_flip == true

    b.handle_key({Key::Space, KeyAction::Down}); // pressed via keyboard, never hovered
    ui::testing::ElementAccess::sync_composition(b, compositor, theme);

    REQUIRE(opacity_animations(compositor).empty());
}

TEST_CASE("Under Metro, hover alone still fades the overlay in", "[composition][button][metro]")
{
    ButtonControl b{"Hello"};
    b.arrange({100, 100, 100, 40});
    TestCompositor compositor;
    const Theme    theme = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                         OsThemeSnapshot{});

    b.handle_pointer(move({150, 120})); // hovered, not pressed
    ui::testing::ElementAccess::sync_composition(b, compositor, theme);

    REQUIRE(opacity_animations(compositor).size() == 1);
}

TEST_CASE("CheckBox hover fades a compositor overlay in and out", "[composition][checkbox]")
{
    CheckBoxControl c{"Remember me"};
    c.arrange({100, 100, 140, 24});
    TestCompositor compositor;
    Theme          theme = Theme::light();

    c.handle_pointer(move({110, 110}));
    ui::testing::ElementAccess::sync_composition(c, compositor, theme);
    auto anims = opacity_animations(compositor);
    REQUIRE(anims.size() == 1);
    REQUIRE(anims[0].to > 0.0f);
}

TEST_CASE("RadioButton hover fades a compositor overlay in and out", "[composition][radio]")
{
    RadioButtonControl r{"Option"};
    r.arrange({100, 100, 120, 24});
    TestCompositor compositor;
    Theme          theme = Theme::light();

    r.handle_pointer(move({110, 110}));
    ui::testing::ElementAccess::sync_composition(r, compositor, theme);
    auto anims = opacity_animations(compositor);
    REQUIRE(anims.size() == 1);
    REQUIRE(anims[0].to > 0.0f);
}

TEST_CASE("ToggleSwitch hover fades a compositor overlay in and out", "[composition][toggle]")
{
    ToggleSwitchControl t;
    t.arrange({100, 100, 120, 24});
    TestCompositor compositor;
    Theme          theme = Theme::light();

    t.handle_pointer(move({110, 110}));
    ui::testing::ElementAccess::sync_composition(t, compositor, theme);
    auto anims = opacity_animations(compositor);
    REQUIRE(anims.size() == 1);
    REQUIRE(anims[0].to > 0.0f);
}

namespace
{
// The last rounded fill narrower than `max_width`: the radio dot or the toggle knob.
Rect last_small_fill(const Element& e, const Theme& theme, float max_width)
{
    TestRenderer        renderer;
    std::optional<Rect> found;
    e.render(renderer, theme);
    for (const auto& cmd : renderer.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&cmd); f && f->bounds.width < max_width)
        {
            found = f->bounds;
        }
    }
    REQUIRE(found);
    return *found;
}

// The radius of the last ellipse drawn: the slider thumb's inner circle.
float last_ellipse_radius(const Element& e, const Theme& theme)
{
    TestRenderer         renderer;
    std::optional<float> found;
    e.render(renderer, theme);
    for (const auto& cmd : renderer.commands)
    {
        if (const auto* f = std::get_if<FillEllipse>(&cmd))
        {
            found = f->radius_x;
        }
    }
    REQUIRE(found);
    return *found;
}

constexpr float SmallPart = 24.0f; // narrower than any track, wider than any dot or knob
} // namespace

TEST_CASE("ToggleSwitch slides its knob in the frame on toggle", "[composition][toggle]")
{
    ToggleSwitchControl t;
    t.arrange({100, 100, 120, 24});
    ToggleSwitchControl on{true};
    on.arrange({100, 100, 120, 24});
    TestCompositor compositor;
    Theme          theme    = Theme::light();
    const Rect     off_knob = last_small_fill(t, theme, SmallPart);
    const Rect     on_knob  = last_small_fill(on, theme, SmallPart);
    REQUIRE(on_knob.left() > off_knob.left());

    ui::testing::ElementAccess::sync_composition(t, compositor, theme); // the first sync only records
    t.set_checked(true);
    ui::testing::ElementAccess::sync_composition(t, compositor, theme);
    CHECK(last_small_fill(t, theme, SmallPart) == off_knob); // starts where it was drawn

    t.tick(Duration{16.0});
    t.tick(Duration{40.0});
    const Rect moving = last_small_fill(t, theme, SmallPart);
    CHECK(moving.left() > off_knob.left());
    CHECK(moving.left() < on_knob.left());

    t.tick(theme.anim_fast);
    CHECK(last_small_fill(t, theme, SmallPart) == on_knob);
}

TEST_CASE("ToggleSwitch grows its knob in the frame on hover", "[composition][toggle]")
{
    ToggleSwitchControl t;
    t.arrange({100, 100, 120, 24});
    ToggleSwitchControl hovered;
    hovered.arrange({100, 100, 120, 24});
    hovered.handle_pointer(move({110, 110}));
    TestCompositor compositor;
    Theme          theme = Theme::light();
    const Rect     rest  = last_small_fill(t, theme, SmallPart);
    const Rect     hover = last_small_fill(hovered, theme, SmallPart);
    REQUIRE(hover.width > rest.width);

    ui::testing::ElementAccess::sync_composition(t, compositor, theme);
    t.handle_pointer(move({110, 110}));
    ui::testing::ElementAccess::sync_composition(t, compositor, theme);
    t.tick(Duration{16.0});
    t.tick(Duration{20.0});
    const float growing = last_small_fill(t, theme, SmallPart).width;
    CHECK(growing > rest.width);
    CHECK(growing < hover.width);

    t.tick(theme.anim_fast);
    CHECK(last_small_fill(t, theme, SmallPart) == hover);
}

TEST_CASE("A ToggleSwitch with no compositor draws its knob where its state puts it", "[composition][toggle]")
{
    ToggleSwitchControl t;
    t.arrange({100, 100, 120, 24});
    ToggleSwitchControl on{true};
    on.arrange({100, 100, 120, 24});
    Theme theme = Theme::light();

    t.set_checked(true);
    t.tick(Duration{16.0});
    CHECK(last_small_fill(t, theme, SmallPart) == last_small_fill(on, theme, SmallPart));
}

TEST_CASE("RadioButton grows its checked dot in the frame on hover", "[composition][radio]")
{
    RadioButtonControl r{"Option", /*checked=*/true};
    r.arrange({100, 100, 120, 24});
    RadioButtonControl hovered{"Option", /*checked=*/true};
    hovered.arrange({100, 100, 120, 24});
    hovered.handle_pointer(move({110, 110}));
    TestCompositor compositor;
    Theme          theme = Theme::light();
    const float    rest  = last_small_fill(r, theme, SmallPart).width;
    const float    hover = last_small_fill(hovered, theme, SmallPart).width;
    REQUIRE(hover > rest);

    ui::testing::ElementAccess::sync_composition(r, compositor, theme);
    r.handle_pointer(move({110, 110}));
    ui::testing::ElementAccess::sync_composition(r, compositor, theme);
    CHECK(last_small_fill(r, theme, SmallPart).width == rest);

    r.tick(Duration{16.0});
    r.tick(Duration{40.0});
    const float growing = last_small_fill(r, theme, SmallPart).width;
    CHECK(growing > rest);
    CHECK(growing < hover);

    r.tick(theme.anim_fast);
    CHECK(last_small_fill(r, theme, SmallPart).width == hover);
}

TEST_CASE("A freshly checked RadioButton dot appears at its size", "[composition][radio]")
{
    RadioButtonControl r{"Option"};
    r.arrange({100, 100, 120, 24});
    RadioButtonControl hovered{"Option", /*checked=*/true};
    hovered.arrange({100, 100, 120, 24});
    hovered.handle_pointer(move({110, 110}));
    TestCompositor compositor;
    Theme          theme = Theme::light();

    r.handle_pointer(move({110, 110}));
    ui::testing::ElementAccess::sync_composition(r, compositor, theme);
    r.set_checked(true);
    ui::testing::ElementAccess::sync_composition(r, compositor, theme);
    r.tick(Duration{16.0});
    CHECK(last_small_fill(r, theme, SmallPart).width == last_small_fill(hovered, theme, SmallPart).width);
}

TEST_CASE("Slider grows its inner circle in the frame on hover, and shrinks it faster leaving", "[composition][slider]")
{
    SliderControl s;
    s.arrange({100, 100, 200, 22});
    SliderControl hovered;
    hovered.arrange({100, 100, 200, 22});
    hovered.handle_pointer(move({150, 111}));
    TestCompositor compositor;
    Theme          theme = Theme::light();
    const float    rest  = last_ellipse_radius(s, theme);
    const float    hover = last_ellipse_radius(hovered, theme);
    REQUIRE(hover > rest);
    REQUIRE(theme.anim_normal > theme.anim_fast);

    ui::testing::ElementAccess::sync_composition(s, compositor, theme);
    s.handle_pointer(move({150, 111}));
    ui::testing::ElementAccess::sync_composition(s, compositor, theme);
    s.tick(Duration{16.0});
    s.tick(theme.anim_fast);
    // Entering Hover takes anim_normal, so anim_fast into it is still growing.
    const float growing = last_ellipse_radius(s, theme);
    CHECK(growing > rest);
    CHECK(growing < hover);
    s.tick(theme.anim_normal);
    CHECK(last_ellipse_radius(s, theme) == hover);

    // Entering Normal takes anim_fast.
    s.handle_pointer(leave());
    ui::testing::ElementAccess::sync_composition(s, compositor, theme);
    s.tick(Duration{16.0});
    s.tick(theme.anim_fast);
    CHECK(last_ellipse_radius(s, theme) == rest);
}

TEST_CASE("A retro Slider thumb composes nothing", "[composition][slider]")
{
    SliderControl s;
    s.arrange({100, 100, 200, 22});
    TestCompositor compositor;
    const Theme    theme = Theme::classic(ClassicPalette::windows_standard(), /*hot_track=*/false, "MS Sans Serif");

    ui::testing::ElementAccess::sync_composition(s, compositor, theme);
    s.handle_pointer(move({150, 111}));
    ui::testing::ElementAccess::sync_composition(s, compositor, theme);

    REQUIRE(compositor.commands().empty());
}

TEST_CASE("Window forwards composition sync to its content after pointer/layout events", "[composition][window]")
{
    HeadlessUI     ui;
    ButtonControl* button = ui.window().set_content(ui.button("Hello"));
    TestCompositor compositor;
    seams(ui.window()).set_compositor(&compositor);
    ui.layout({400, 300});

    const std::size_t after_layout = compositor.count();
    REQUIRE(after_layout > 0); // layout() itself syncs geometry

    ui::testing::seams(ui.window()).handle_pointer(move(button->bounds().center()));
    const auto anims = opacity_animations(compositor);
    REQUIRE(anims.size() == 1);
    REQUIRE(anims[0].to > 0.0f);
}

TEST_CASE("A modal on a window with no content gets the pointer and its composition", "[composition][window]")
{
    HeadlessUI     ui;
    TestCompositor compositor;
    seams(ui.window()).set_compositor(&compositor);
    int            clicks = 0;
    ButtonControl* button = ui.window().show_modal(ui.button("OK"));
    button->on_click([&] { ++clicks; });
    ui.layout({400, 300});

    const Point center = button->bounds().center();
    ui::testing::seams(ui.window()).handle_pointer(move(center));
    const auto anims = opacity_animations(compositor);
    REQUIRE(anims.size() == 1); // the hover reached the modal's button through sync_composition()
    CHECK(anims[0].to > 0.0f);

    ui::testing::seams(ui.window()).handle_pointer({center, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({center, PointerButton::Left, PointerAction::Up});
    CHECK(clicks == 1);
}
