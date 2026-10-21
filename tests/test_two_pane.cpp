// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panes/TwoPane.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}
} // namespace

TEST_CASE("A new TwoPane is not focusable and has no panes", "[two-pane-view]")
{
    TwoPaneControl view;
    REQUIRE_FALSE(view.focusable());
    REQUIRE(view.pane1() == nullptr);
    REQUIRE(view.pane2() == nullptr);
}

TEST_CASE("Below both thresholds resolves SinglePane, showing the priority pane", "[two-pane-view][layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    TwoPaneControl view;
    ButtonControl* pane1 = view.set_pane1(std::make_unique<ButtonControl>("One"));
    ButtonControl* pane2 = view.set_pane2(std::make_unique<ButtonControl>("Two"));

    const Size desired = view.measure({300.0f, 300.0f}, measurer, theme);
    REQUIRE(view.mode() == TwoPaneMode::SinglePane);

    const Size pane1_alone = pane1->measure({300.0f, 300.0f}, measurer, theme);
    REQUIRE_THAT(desired.width, WithinAbs(pane1_alone.width, 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(pane1_alone.height, 1e-3));

    view.arrange({0, 0, desired.width, desired.height});
    REQUIRE_THAT(pane1->bounds().width, WithinAbs(desired.width, 1e-3));
    (void)pane2;
}

TEST_CASE("PanePriority::Pane2 shows pane2 in SinglePane mode", "[two-pane-view][layout]")
{
    TwoPaneControl view;
    view.set_pane_priority(TwoPanePriority::Pane2);
    view.set_pane1(std::make_unique<ButtonControl>("One"));
    ButtonControl* pane2 = view.set_pane2(std::make_unique<ButtonControl>("Two"));

    HeadlessTextMeasurer measurer;
    Theme                theme   = Theme::light();
    const Size           desired = view.measure({300.0f, 300.0f}, measurer, theme);
    view.arrange({0, 0, desired.width, desired.height});

    REQUIRE(view.mode() == TwoPaneMode::SinglePane);
    REQUIRE_THAT(pane2->bounds().width, WithinAbs(desired.width, 1e-3));
}

TEST_CASE("Width past MinWideModeWidth resolves Wide and splits LeftRight by default", "[two-pane-view][layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    TwoPaneControl view;
    view.set_pane1_length(GridLength::dip(200.0f));
    ButtonControl* pane1 = view.set_pane1(std::make_unique<ButtonControl>("One"));
    ButtonControl* pane2 = view.set_pane2(std::make_unique<ButtonControl>("Two"));

    const Size desired = view.measure({800.0f, 300.0f}, measurer, theme);
    REQUIRE(view.mode() == TwoPaneMode::Wide);
    REQUIRE_THAT(desired.width, WithinAbs(800.0, 1e-3));

    view.arrange({0, 0, desired.width, desired.height});
    REQUIRE_THAT(pane1->bounds().left(), WithinAbs(0.0f, 1e-3));
    REQUIRE_THAT(pane1->bounds().width, WithinAbs(200.0, 1e-3));
    REQUIRE_THAT(pane2->bounds().left(), WithinAbs(200.0, 1e-3));
    REQUIRE_THAT(pane2->bounds().width, WithinAbs(600.0, 1e-3)); // pane 2's one-fraction default absorbs the remainder
}

TEST_CASE("WideModeConfiguration::RightLeft puts pane2 on the left", "[two-pane-view][layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    TwoPaneControl view;
    view.set_wide_mode_configuration(TwoPaneWideModeConfiguration::RightLeft);
    view.set_pane2_length(GridLength::dip(150.0f));
    ButtonControl* pane1 = view.set_pane1(std::make_unique<ButtonControl>("One"));
    ButtonControl* pane2 = view.set_pane2(std::make_unique<ButtonControl>("Two"));

    const Size desired = view.measure({800.0f, 300.0f}, measurer, theme);
    view.arrange({0, 0, desired.width, desired.height});

    REQUIRE_THAT(pane2->bounds().left(), WithinAbs(0.0f, 1e-3));
    REQUIRE_THAT(pane2->bounds().width, WithinAbs(150.0, 1e-3));
    REQUIRE_THAT(pane1->bounds().left(), WithinAbs(150.0, 1e-3));
}

TEST_CASE("Height past MinTallModeHeight (width too narrow for Wide) resolves Tall, TopBottom by default",
          "[two-pane-view][layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    TwoPaneControl view;
    view.set_pane1_length(GridLength::dip(100.0f));
    ButtonControl* pane1 = view.set_pane1(std::make_unique<ButtonControl>("One"));
    ButtonControl* pane2 = view.set_pane2(std::make_unique<ButtonControl>("Two"));

    const Size desired = view.measure({300.0f, 800.0f}, measurer, theme);
    REQUIRE(view.mode() == TwoPaneMode::Tall);

    view.arrange({0, 0, desired.width, desired.height});
    REQUIRE_THAT(pane1->bounds().top(), WithinAbs(0.0f, 1e-3));
    REQUIRE_THAT(pane1->bounds().height, WithinAbs(100.0, 1e-3));
    REQUIRE_THAT(pane2->bounds().top(), WithinAbs(100.0, 1e-3));
    REQUIRE_THAT(pane2->bounds().height, WithinAbs(700.0, 1e-3));
}

TEST_CASE("TallModeConfiguration::BottomTop puts pane2 on top", "[two-pane-view][layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    TwoPaneControl view;
    view.set_tall_mode_configuration(TwoPaneTallModeConfiguration::BottomTop);
    view.set_pane2_length(GridLength::dip(120.0f));
    ButtonControl* pane1 = view.set_pane1(std::make_unique<ButtonControl>("One"));
    ButtonControl* pane2 = view.set_pane2(std::make_unique<ButtonControl>("Two"));

    const Size desired = view.measure({300.0f, 800.0f}, measurer, theme);
    view.arrange({0, 0, desired.width, desired.height});

    REQUIRE_THAT(pane2->bounds().top(), WithinAbs(0.0f, 1e-3));
    REQUIRE_THAT(pane2->bounds().height, WithinAbs(120.0, 1e-3));
    REQUIRE_THAT(pane1->bounds().top(), WithinAbs(120.0, 1e-3));
}

TEST_CASE("SinglePane on both configurations stays SinglePane regardless of size", "[two-pane-view][layout]")
{
    TwoPaneControl view;
    view.set_wide_mode_configuration(TwoPaneWideModeConfiguration::SinglePane);
    view.set_tall_mode_configuration(TwoPaneTallModeConfiguration::SinglePane);
    view.set_pane1(std::make_unique<ButtonControl>("One"));
    view.set_pane2(std::make_unique<ButtonControl>("Two"));

    HeadlessTextMeasurer measurer;
    (void)view.measure({800.0f, 800.0f}, measurer, Theme::default_light());
    REQUIRE(view.mode() == TwoPaneMode::SinglePane);
}

TEST_CASE("An unbounded available falls back to SinglePane", "[two-pane-view][layout]")
{
    TwoPaneControl view;
    view.set_pane1(std::make_unique<ButtonControl>("One"));
    view.set_pane2(std::make_unique<ButtonControl>("Two"));

    HeadlessTextMeasurer measurer;
    (void)view.measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    REQUIRE(view.mode() == TwoPaneMode::SinglePane);
}

TEST_CASE("Auto pane length sizes to the pane's own desired extent", "[two-pane-view][layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    TwoPaneControl view; // pane1_length defaults to Auto
    ButtonControl* pane1 = view.set_pane1(std::make_unique<ButtonControl>("A rather long label"));
    ButtonControl* pane2 = view.set_pane2(std::make_unique<ButtonControl>("Two"));

    const Size pane1_natural = pane1->measure({800.0f, 300.0f}, measurer, theme);

    const Size desired = view.measure({800.0f, 300.0f}, measurer, theme);
    view.arrange({0, 0, desired.width, desired.height});

    REQUIRE_THAT(pane1->bounds().width, WithinAbs(pane1_natural.width, 1e-3));
    REQUIRE_THAT(pane2->bounds().width, WithinAbs(800.0 - pane1_natural.width, 1e-3)); // pane 2's fraction absorbs the rest
}

TEST_CASE("Only the visible pane(s) join focusables, render, and receive pointer events", "[two-pane-view][focus]")
{
    HeadlessTextMeasurer measurer;

    auto            view_owned = std::make_unique<TwoPaneControl>();
    TwoPaneControl* view       = view_owned.get();
    view->set_pane1_length(GridLength::dip(100.0f));
    ButtonControl* pane1 = view->set_pane1(std::make_unique<ButtonControl>("One"));
    ButtonControl* pane2 = view->set_pane2(std::make_unique<ButtonControl>("Two"));

    Window window;
    window.set_content(std::move(view_owned));

    // Narrow: SinglePane, Pane1 shown.
    window.layout({300, 300}, measurer);
    std::vector<Element*> focusables;
    window.content()->collect_focusables(focusables);
    REQUIRE(std::find(focusables.begin(), focusables.end(), static_cast<Element*>(pane1)) != focusables.end());
    REQUIRE(std::find(focusables.begin(), focusables.end(), static_cast<Element*>(pane2)) == focusables.end());

    TestRenderer r1;
    view->render(r1, Theme::light());
    REQUIRE(r1.count() > 0);

    int pane2_clicks = 0;
    pane2->on_click([&] { ++pane2_clicks; });
    view->handle_pointer(down(pane2->bounds().center()));
    view->handle_pointer(up(pane2->bounds().center()));
    REQUIRE(pane2_clicks == 0); // pane2 isn't shown, so it never sees the click

    // Wide: both panes shown.
    window.layout({800, 300}, measurer);
    focusables.clear();
    window.content()->collect_focusables(focusables);
    REQUIRE(std::find(focusables.begin(), focusables.end(), static_cast<Element*>(pane1)) != focusables.end());
    REQUIRE(std::find(focusables.begin(), focusables.end(), static_cast<Element*>(pane2)) != focusables.end());

    view->handle_pointer(down(pane2->bounds().center()));
    view->handle_pointer(up(pane2->bounds().center()));
    REQUIRE(pane2_clicks == 1);
}
