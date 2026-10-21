// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/containers/Toolbar.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/ClassicPalette.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE("Toolbar measure reflects its children, like a horizontal StackControl", "[toolbar][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ToolbarControl toolbar;
    toolbar.set_spacing(4.0f);
    ButtonControl* a = toolbar.add(std::make_unique<ButtonControl>("A"));
    ButtonControl* b = toolbar.add(std::make_unique<ButtonControl>("B"));

    const Size desired = toolbar.measure({Unbounded, Unbounded}, m, theme);
    const Size a_size  = a->measure({Unbounded, Unbounded}, m, theme);
    const Size b_size  = b->measure({Unbounded, Unbounded}, m, theme);

    REQUIRE_THAT(desired.width, WithinAbs(a_size.width + b_size.width + toolbar.spacing(), 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(std::max(a_size.height, b_size.height), 1e-3));
}

TEST_CASE("add_separator() renders Fluent's flat single divider under the default theme and takes up layout width",
          "[toolbar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light(); // Fluent: ThemeDividerVisual::FlatStroke

    ToolbarControl toolbar;
    toolbar.add(std::make_unique<ButtonControl>("A"));
    toolbar.add_separator();
    toolbar.add(std::make_unique<ButtonControl>("B"));

    const Size desired = toolbar.measure({Unbounded, Unbounded}, m, theme);
    toolbar.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    toolbar.render(r, theme);
    // The separator is one flat 1px rule. The classic themes draw the
    // etched pair instead (next test).
    REQUIRE(r.count_of<StrokeLine>() == 0);
    REQUIRE(r.count_of<FillRoundedRect>() >= 1);
}

TEST_CASE("add_separator() renders the classic etched-groove line pair under a retro Win32 theme", "[toolbar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::classic(ClassicPalette::windows_standard(),
                                                /*hot_track=*/false,
                                                "MS Sans Serif"); // ThemeDividerVisual::EtchedGroove default

    // The separator alone: classic Buttons stroke their own bevels, which would muddy the count.
    std::unique_ptr<Element> separator = ToolbarControl::make_separator();
    separator->arrange({0, 0, 9, 24});

    TestRenderer r;
    separator->render(r, theme);
    // The classic EDGE_ETCHED pair: a dark line, then a light one.
    REQUIRE(r.count_of<StrokeLine>() == 2);
    REQUIRE(r.count_of<FillRoundedRect>() == 0);
}

TEST_CASE("Toolbar forwards input and focus collection to its children", "[toolbar][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ToolbarControl toolbar;
    ButtonControl* a       = toolbar.add(std::make_unique<ButtonControl>("A"));
    int            clicked = 0;
    a->on_click([&] { ++clicked; });

    const Size desired = toolbar.measure({Unbounded, Unbounded}, m, theme);
    toolbar.arrange({0, 0, desired.width, desired.height});

    const Point p{a->bounds().center()};
    toolbar.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    toolbar.handle_pointer({p, PointerButton::Left, PointerAction::Up});
    REQUIRE(clicked == 1);

    std::vector<Element*> focusables;
    toolbar.collect_focusables(focusables);
    REQUIRE(focusables.size() == 1);
    REQUIRE(focusables[0] == a);
}

TEST_CASE("Toolbar itself is never focusable - only its children are", "[toolbar][state]")
{
    ToolbarControl toolbar;
    toolbar.add(std::make_unique<ButtonControl>("A"));
    REQUIRE_FALSE(toolbar.focusable());
}

TEST_CASE("A separator doesn't balloon toolbar height when measured with a large available height", "[toolbar][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    ToolbarControl toolbar;
    toolbar.add(std::make_unique<ButtonControl>("A"));
    toolbar.add_separator();
    toolbar.add(std::make_unique<ButtonControl>("B"));

    const Size desired_small = toolbar.measure({Unbounded, Unbounded}, m, theme);
    const Size desired_large = toolbar.measure({1000.0f, 4000.0f}, m, theme);

    REQUIRE_THAT(desired_large.height, WithinAbs(desired_small.height, 1e-3));
}

TEST_CASE("ui::Separator takes the common fields and publishes its control", "[toolbar][spec]")
{
    ui::Control<ui::Separator> separator;
    ui::Control<ui::Toolbar>   toolbar;
    std::unique_ptr<Element>   node = ui::build(
        ui::Toolbar{.control = toolbar,
                    .content = {
                        ui::Button{.text = "Cut"},
                        ui::Separator{.margin        = Symmetric{.horizontal = 4.0f, .vertical = 0.0f},
                                      .accessibility = {.name = "Edit group"},
                                      .control       = separator},
                        ui::Button{.text = "Undo"},
                    }});
    REQUIRE(separator);
    CHECK(separator->margin() == Symmetric{.horizontal = 4.0f, .vertical = 0.0f});
    CHECK(separator->accessibility().name == "Edit group");
    CHECK(separator->automation_role() == AutomationRole::Separator);
    CHECK(separator->parent() != nullptr);

    ui::Control<ui::Separator> hidden;
    std::unique_ptr<Element>   alone = ui::build(ui::Separator{.visible = false, .control = hidden});
    CHECK(hidden->visibility() == Visibility::Collapsed);
}
