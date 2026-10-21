// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Brush.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/TeachingTip.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <variant>
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

void click(Window& window, Element* e)
{
    ui::testing::seams(window).handle_pointer(down(e->bounds().center()));
    ui::testing::seams(window).handle_pointer(up(e->bounds().center()));
}

struct TipWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    Size                 client;
    ButtonControl*       target = nullptr;
    TeachingTipControl*  tip    = nullptr;

    explicit TipWindow(Size client_size = {800.0f, 600.0f}) : client(client_size)
    {
        auto stack = std::make_unique<StackControl>(Orientation::Vertical);
        target     = stack->add(std::make_unique<ButtonControl>("Show TeachingTip"));
        tip        = stack->add(std::make_unique<TeachingTipControl>());
        tip->set_title("This is the title");
        tip->set_subtitle("And this is the subtitle");
        tip->set_target(target);
        window.set_stretch_content(false); // laid out for the control at its desired size, centered
        window.set_content(std::move(stack));
        relayout();
    }

    void relayout()
    {
        window.layout(client, measurer);
    }

    [[nodiscard]] Rect card() const
    {
        return ui::testing::popup_content(*tip)->bounds();
    }
};
} // namespace

TEST_CASE("A TeachingTip takes no room in place and starts closed", "[teaching_tip]")
{
    HeadlessTextMeasurer measurer;
    TeachingTipControl   tip;
    CHECK_FALSE(tip.is_open());
    CHECK_FALSE(tip.light_dismiss());
    CHECK_FALSE(tip.placement().has_value());
    CHECK(tip.placement_alignment() == PopupPlacementAlignment::Center);
    const Size desired = tip.measure(Size{400.0f, 400.0f}, measurer, Theme::default_light());
    CHECK(desired.width == 0.0f);
    CHECK(desired.height == 0.0f);
}

TEST_CASE("The popup is 320 to 336 wide, tail strips included", "[teaching_tip][layout]")
{
    TipWindow w;
    w.tip->set_open(true);
    w.relayout();
    CHECK(w.card().width >= 320.0f);
    CHECK(w.card().width <= 336.0f);

    // Long text wraps inside the maximum instead of widening the card.
    const float one_line = w.card().height;
    w.tip->set_subtitle("A subtitle long enough that it cannot possibly fit on a single line of a teaching tip card");
    w.relayout();
    CHECK(w.card().width <= 336.0f);
    CHECK(w.card().height > one_line);
}

TEST_CASE("An unset placement places a targeted tip above its target, the tail at the target's center",
          "[teaching_tip][placement]")
{
    // A target low in the window leaves room above it; above is tried first.
    TipWindow w{{800.0f, 600.0f}};
    w.tip->set_open(true);
    w.relayout();
    REQUIRE(w.tip->effective_placement() == PopupPlacement::Above);

    // Above: the popup's bottom (tail tip included) on the target's top,
    // centered across it.
    const Rect t = w.target->bounds();
    CHECK_THAT(w.card().bottom(), WithinAbs(t.top(), 0.01));
    CHECK_THAT(w.card().center().x, WithinAbs(t.center().x, 0.5));

    // The tail: 20 wide, its tip on the popup's bottom edge.
    TestRenderer r;
    ui::testing::popup_content(*w.tip)->render(r, Theme::light());
    std::optional<FillPolygon>    tail;
    std::optional<StrokePolyline> edge;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* p = std::get_if<FillPolygon>(&c))
        {
            tail = *p;
        }
        if (const auto* p = std::get_if<StrokePolyline>(&c))
        {
            edge = *p;
        }
    }
    REQUIRE(tail);
    REQUIRE(tail->points.size() == 3);
    CHECK_THAT(tail->points[1].y, WithinAbs(w.card().bottom(), 0.01));
    CHECK_THAT(tail->points[1].x, WithinAbs(t.center().x, 0.5));
    CHECK_THAT(tail->points[2].x - tail->points[0].x, WithinAbs(20.0f, 0.01));
    REQUIRE(edge);
    CHECK(edge->points == tail->points);
    CHECK_FALSE(edge->closed); // the base is left open, inside the card
}

TEST_CASE("placement_margin is the gap on the side facing the target", "[teaching_tip][placement]")
{
    TipWindow w;
    w.tip->set_placement_margin(Thickness{20.0f});
    w.tip->set_open(true);
    w.relayout();
    CHECK_THAT(w.card().bottom(), WithinAbs(w.target->bounds().top() - 20.0f, 0.01));
}

TEST_CASE("A tip that does not fit on its preferred side falls back", "[teaching_tip][placement]")
{
    // The window's content is centered, so a short window leaves too little room above and
    // below the target for a card; a wide one leaves room at the sides.
    TipWindow w{{1200.0f, 150.0f}};
    w.tip->set_placement(PopupPlacement::Below);
    w.tip->set_open(true);
    w.relayout();
    const PopupPlacement p = w.tip->effective_placement();
    CHECK(p != PopupPlacement::Below);
    CHECK(p != PopupPlacement::Above);
    CHECK(w.tip->is_open());
}

TEST_CASE("A tip that fits nowhere does not open", "[teaching_tip][placement]")
{
    TipWindow                     w{{200.0f, 100.0f}};
    std::vector<PopupCloseReason> reasons;
    w.tip->on_closed([&](PopupCloseReason r) { reasons.push_back(r); });
    w.tip->set_open(true);
    w.relayout();
    CHECK_FALSE(w.tip->is_open());
    CHECK(reasons == std::vector{PopupCloseReason::Programmatic});
}

TEST_CASE("An untargeted tip sits 24 DIPs above the window's bottom edge, centered, with no tail",
          "[teaching_tip][placement]")
{
    TipWindow w;
    w.tip->set_target(nullptr);
    w.tip->set_open(true);
    w.relayout();
    CHECK_THAT(w.card().bottom(), WithinAbs(w.client.height - 24.0f, 0.01));
    CHECK_THAT(w.card().center().x, WithinAbs(w.client.width * 0.5f, 0.5));
    CHECK_FALSE(w.tip->tail_shown());

    TestRenderer r;
    ui::testing::popup_content(*w.tip)->render(r, Theme::light());
    CHECK(r.count_of<FillPolygon>() == 0);
}

TEST_CASE("The X shows without a close button and without light dismiss", "[teaching_tip]")
{
    TeachingTipControl tip;
    CHECK(tip.alternate_close_visible());
    tip.set_close_button_text("Close button");
    CHECK_FALSE(tip.alternate_close_visible());
    tip.set_close_button_text("");
    tip.set_light_dismiss(true);
    CHECK_FALSE(tip.alternate_close_visible());
}

TEST_CASE("The action button does not close the tip; the close button does", "[teaching_tip]")
{
    TipWindow w;
    w.tip->set_action_button_text("Action button");
    w.tip->set_close_button_text("Close button");
    int                           actions = 0;
    std::vector<PopupCloseReason> reasons;
    w.tip->on_action_button_click([&] { ++actions; });
    w.tip->on_closed([&](PopupCloseReason r) { reasons.push_back(r); });
    w.tip->set_open(true);
    w.relayout();

    // Both buttons share the row at equal width, 8 apart.
    const Rect a = w.tip->action_button()->bounds();
    const Rect c = w.tip->close_button()->bounds();
    CHECK_THAT(a.width, WithinAbs(c.width, 0.01));
    CHECK_THAT(c.x - a.right(), WithinAbs(8.0f, 0.01));

    click(w.window, w.tip->action_button());
    CHECK(actions == 1);
    CHECK(w.tip->is_open());

    click(w.window, w.tip->close_button());
    CHECK_FALSE(w.tip->is_open());
    CHECK(reasons == std::vector{PopupCloseReason::CloseButton});
}

TEST_CASE("The X closes the tip with reason CloseButton", "[teaching_tip]")
{
    TipWindow                     w;
    std::vector<PopupCloseReason> reasons;
    w.tip->on_closed([&](PopupCloseReason r) { reasons.push_back(r); });
    w.tip->set_open(true);
    w.relayout();
    const Rect x = w.tip->alternate_close_button()->bounds();
    CHECK_THAT(x.width, WithinAbs(32.0f, 0.01));
    click(w.window, w.tip->alternate_close_button());
    CHECK(reasons == std::vector{PopupCloseReason::CloseButton});
}

TEST_CASE("Without light dismiss an outside click and Escape leave the tip open", "[teaching_tip]")
{
    TipWindow w;
    w.tip->set_open(true);
    w.relayout();
    ui::testing::seams(w.window).handle_pointer(down(Point{1.0f, 1.0f}));
    ui::testing::seams(w.window).handle_pointer(up(Point{1.0f, 1.0f}));
    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    CHECK(w.tip->is_open());
}

TEST_CASE("Light dismiss: an outside click closes, the card takes the acrylic fill and focus", "[teaching_tip]")
{
    TipWindow w;
    w.tip->set_light_dismiss(true);
    w.tip->set_action_button_text("Action button");
    w.tip->set_close_button_text("Close button");
    std::vector<PopupCloseReason> reasons;
    w.tip->on_closed([&](PopupCloseReason r) { reasons.push_back(r); });
    w.tip->set_open(true);
    w.relayout();
    CHECK(w.window.focused() == w.tip->action_button()); // the first stop inside the tip

    // Dark, where the two differ: #2C2C2C acrylic fallback against #282828 (Light has #F9F9F9 for both).
    const Theme  theme = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark}, OsThemeSnapshot{});
    TestRenderer r;
    ui::testing::popup_content(*w.tip)->render(r, theme);
    const auto* fill = std::get_if<DrawGeometry>(&r.commands.front());
    REQUIRE(fill != nullptr);
    REQUIRE(fill->fill.has_value());
    const auto* acrylic = std::get_if<AcrylicBrush>(&*fill->fill);
    REQUIRE(acrylic != nullptr);
    CHECK(acrylic->fallback_color == theme.surfaces.menu);
    CHECK(acrylic->fallback_color != *theme.surfaces.teaching_tip);

    ui::testing::seams(w.window).handle_pointer(down(Point{1.0f, 1.0f}));
    CHECK_FALSE(w.tip->is_open());
    CHECK(reasons == std::vector{PopupCloseReason::LightDismiss});
}

TEST_CASE("on_closing can keep the tip open", "[teaching_tip]")
{
    TipWindow w;
    w.tip->on_closing([] { return ui::Cancel; });
    w.tip->set_open(true);
    w.relayout();
    click(w.window, w.tip->alternate_close_button());
    CHECK(w.tip->is_open());
}

TEST_CASE("Hero content goes away from the tail", "[teaching_tip][layout]")
{
    TipWindow    w;
    TextControl* hero = w.tip->set_hero_content(std::make_unique<TextControl>("Hero"));
    hero->set_min_height(40.0f);
    w.tip->set_placement(PopupPlacement::Above);
    w.tip->set_open(true);
    w.relayout();
    REQUIRE(w.tip->effective_placement() == PopupPlacement::Above);
    // Tail at the bottom: the hero across the top of the card, inside the strip and frame.
    CHECK_THAT(hero->bounds().top(), WithinAbs(w.card().top() + 9.0f, 0.01));

    w.tip->set_open(false);
    w.tip->set_placement(PopupPlacement::Below);
    w.tip->set_open(true);
    w.relayout();
    if (w.tip->effective_placement() == PopupPlacement::Below)
    {
        CHECK_THAT(hero->bounds().bottom(), WithinAbs(w.card().bottom() - 9.0f, 0.01));
    }
}

TEST_CASE("Placement fallback order puts the preferred family first", "[teaching_tip][placement]")
{
    // A window wide enough for any side, a target at its center: the preferred side wins.
    const std::pair<PopupPlacement, PopupPlacementAlignment> wanted[] = {
        {PopupPlacement::Left, PopupPlacementAlignment::Center},
        {PopupPlacement::Right, PopupPlacementAlignment::Center},
        {PopupPlacement::Below, PopupPlacementAlignment::Center},
        {PopupPlacement::Right, PopupPlacementAlignment::Leading},
        {PopupPlacement::Above, PopupPlacementAlignment::Trailing},
    };
    for (const auto& [placement, alignment] : wanted)
    {
        TipWindow w{{1400.0f, 1000.0f}};
        w.tip->set_placement(placement);
        w.tip->set_placement_alignment(alignment);
        w.tip->set_open(true);
        w.relayout();
        CHECK(w.tip->effective_placement() == placement);
        CHECK(w.tip->effective_placement_alignment() == alignment);
    }
}

TEST_CASE("ui::TeachingTip asks for its target as it opens, and on_opened follows is_open", "[teaching_tip][spec]")
{
    Property<bool>               open{false};
    int                          opened = 0;
    ui::Control<ui::Button>      show;
    ui::Control<ui::TeachingTip> tip;
    std::unique_ptr<Element>     tree = ui::build(ui::VerticalStack{
        .content =
            {
                ui::Button{.text = "Show", .control = show},
                ui::TeachingTip{.title     = "Title",
                                .is_open   = ui::bind(open),
                                .target    = show,
                                .on_opened = [&] { ++opened; },
                                .control   = tip},
            },
    });
    CHECK(tip->target() == show.get()); // linked as it is built
    CHECK(opened == 0);

    tip->set_open(true); // opened from the control: the property follows
    CHECK(open.get());
    CHECK(opened == 1);
    CHECK(tip->target() == show.get());
}

// The tip takes no room where it sits, but a Grid cell stretches it over the button it shares the
// cell with.
TEST_CASE("A button sharing a grid cell with a TeachingTip still takes the click", "[teaching-tip][input]")
{
    HeadlessUI ui;
    bool       clicked = false;
    ui.window().set_content(ui::build(ui::Grid{
        .columns              = {ui::AutoSize},
        .rows                 = {ui::AutoSize},
        .horizontal_alignment = ui::Alignment::Leading,
        .vertical_alignment   = ui::Alignment::Leading,
        .content =
            {ui::Cell{.row = 0, .column = 0, .content = ui::Button{.text = "Show", .on_click = [&] { clicked = true; }}},
             ui::Cell{.row = 0, .column = 0, .content = ui::TeachingTip{.title = "Title"}}},
    }));
    ui.layout(Size{400.0f, 300.0f});
    const Point at{20.0f, 10.0f};
    seams(ui.window()).handle_pointer({at, PointerButton::None, PointerAction::Move});
    seams(ui.window()).handle_pointer({at, PointerButton::Left, PointerAction::Down});
    seams(ui.window()).handle_pointer({at, PointerButton::Left, PointerAction::Up});
    CHECK(clicked);
}
