// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Version.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE("Button desired size = measured text + padding", "[layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    ButtonControl        b{"Hello"};

    const Size desired = b.measure({Unbounded, Unbounded}, m, theme);
    const Size text    = m.measure("Hello", theme.text_style);

    REQUIRE_THAT(desired.width, WithinAbs(text.width + theme.padding.horizontal(), 1e-4));
    REQUIRE_THAT(desired.height, WithinAbs(text.height + theme.padding.vertical(), 1e-4));
}

TEST_CASE("Headless UI centers content in the client area", "[layout][headless]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_stretch_content(false); // laid out for the control at its desired size, centered

    ButtonControl* button = ui.window().set_content(ui.button("Hello"));
    ui.layout({800, 600});

    const Rect  bounds = button->bounds();
    const Point client_center{400, 300};

    REQUIRE_THAT(bounds.center().x, WithinAbs(client_center.x, 0.5));
    REQUIRE_THAT(bounds.center().y, WithinAbs(client_center.y, 0.5));

    REQUIRE(bounds.width < 800.0f);
    REQUIRE(bounds.height < 600.0f);
}

TEST_CASE("Re-layout at a new size re-centers", "[layout][headless]")
{
    HeadlessUI     ui     = create_headless();
    ButtonControl* button = ui.window().set_content(ui.button("Hello"));

    ui.layout({400, 400});
    REQUIRE_THAT(button->bounds().center().x, WithinAbs(200.0, 0.5));

    ui.layout({1000, 200});
    REQUIRE_THAT(button->bounds().center().x, WithinAbs(500.0, 0.5));
    REQUIRE_THAT(button->bounds().center().y, WithinAbs(100.0, 0.5));
}

TEST_CASE("Input hit-testing uses laid-out bounds", "[layout][headless]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_stretch_content(false); // laid out for the control at its desired size, centered
    ButtonControl* button = ui.window().set_content(ui.button("Hello"));
    ui.layout({800, 600});

    int clicks = 0;
    button->on_click([&] { ++clicks; });

    const Point center = button->bounds().center();
    ui::testing::seams(ui.window()).handle_pointer({center, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({center, PointerButton::Left, PointerAction::Up});
    REQUIRE(clicks == 1);

    ui::testing::seams(ui.window()).handle_pointer({{5, 5}, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({{5, 5}, PointerButton::Left, PointerAction::Up});
    REQUIRE(clicks == 1);
}

TEST_CASE("version_string is reported", "[core]")
{
    REQUIRE(std::string{version_string()} == "0.1.0");
}
