// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace std::chrono_literals;

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

const Point Outside{-500.0f, -500.0f};

// sync_popups() positions the popup inside layout(), so tests call relayout() after input, as a
// real host does on the next paint.
struct FlyoutWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    FlyoutControl*       flyout;
    ButtonControl*       anchor;
    TextControl*         content;

    FlyoutWindow()
    {
        auto owned = std::make_unique<FlyoutControl>();
        flyout     = owned.get();
        anchor     = flyout->set_anchor(std::make_unique<ButtonControl>("Open"));
        content    = flyout->set_content(std::make_unique<TextControl>("Flyout content"));
        window.set_stretch_content(false); // laid out for the control at its desired size, centered
        window.set_content(std::move(owned));
        window.layout({400.0f, 400.0f}, measurer);
    }

    void relayout()
    {
        window.layout({400.0f, 400.0f}, measurer);
    }
};
} // namespace

TEST_CASE("Flyout content reads against the popup surface, not the page", "[flyout][popup][metro]")
{
    // Windows 8.1 draws every flyout surface light, so in a dark app the
    // content must read against the white popup surface, not the dark page.
    FlyoutWindow w;
    const Theme  theme = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                       OsThemeSnapshot{});

    REQUIRE(theme.surfaces.menu != theme.colors().background);
    REQUIRE(theme.surfaces.menu_text != theme.colors().text);

    w.flyout->show();
    w.relayout();
    TestRenderer r;
    ui::testing::popup_content(*w.flyout)->render(r, theme);

    REQUIRE(r.count_of<FillRoundedRect>() >= 1);
    const auto* fill = std::get_if<FillRoundedRect>(&r.commands[0]);
    REQUIRE(fill != nullptr);
    CHECK(fill->color == theme.surfaces.menu);

    const auto* stroke = std::get_if<StrokeRoundedRect>(&r.commands[1]);
    REQUIRE(stroke != nullptr);
    // `field_border` is white here and would vanish.
    CHECK(stroke->color == Color::from_argb(0xFF000000));
    CHECK(stroke->color != theme.surfaces.field_border);

    bool saw_text = false;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            saw_text = true;
            CHECK(t->color == theme.surfaces.menu_text);
            CHECK(t->color != theme.colors().text);
        }
    }
    CHECK(saw_text);
}

TEST_CASE("A theme whose popup matches its page passes its own Theme straight through", "[flyout][popup]")
{
    // Every Fluent-era theme has matching surfaces, so the substitution must change nothing there.
    FlyoutWindow w;
    const Theme  theme = resolve_theme(ThemeRequest{}, OsThemeSnapshot{});
    REQUIRE(theme.surfaces.menu_text == theme.colors().text);

    w.flyout->show();
    w.relayout();
    TestRenderer r;
    ui::testing::popup_content(*w.flyout)->render(r, theme);

    for (const RenderCommand& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            CHECK(t->color == theme.colors().text);
        }
    }
}

TEST_CASE("New Flyout starts closed", "[flyout]")
{
    FlyoutControl f;
    REQUIRE_FALSE(f.is_open());
    REQUIRE(f.anchor() == nullptr);
    REQUIRE(f.content() == nullptr);
}

TEST_CASE("show/hide/toggle flip is_open()", "[flyout]")
{
    FlyoutControl f;
    f.set_anchor(std::make_unique<ButtonControl>("Open"));

    f.show();
    REQUIRE(f.is_open());
    f.show(); // idempotent
    REQUIRE(f.is_open());

    f.hide();
    REQUIRE_FALSE(f.is_open());
    f.hide(); // idempotent
    REQUIRE_FALSE(f.is_open());

    f.toggle();
    REQUIRE(f.is_open());
    f.toggle();
    REQUIRE_FALSE(f.is_open());
}

TEST_CASE("Flyout's anchor fills the flyout's slot, and its click still fires", "[flyout]")
{
    FlyoutWindow w;
    int          clicks = 0;
    w.anchor->on_click([&] { ++clicks; });

    REQUIRE_THAT(w.anchor->bounds().left(), WithinAbs(w.flyout->bounds().left(), 0.5));
    REQUIRE_THAT(w.anchor->bounds().width, WithinAbs(w.flyout->bounds().width, 0.5));

    const Point center = w.flyout->bounds().center();
    ui::testing::seams(w.window).handle_pointer(down(center));
    ui::testing::seams(w.window).handle_pointer(up(center));

    REQUIRE(clicks == 1);
}

TEST_CASE("The popup request reflects the Flyout's own state", "[flyout][popup]")
{
    FlyoutWindow w;
    REQUIRE_FALSE(ui::testing::popup_open(*w.flyout));

    w.flyout->show();
    REQUIRE(ui::testing::popup_open(*w.flyout));
    REQUIRE(ui::testing::popup_content(*w.flyout) != nullptr);
    // Anchored to the visible anchor, grown by FlyoutMargin's 4 DIPs along the placement axis.
    const Rect anchor = w.anchor->bounds();
    REQUIRE(ui::testing::popup_anchor(*w.flyout) == Rect{anchor.x, anchor.y - 4.0f, anchor.width, anchor.height + 8.0f});
}

TEST_CASE("Clicking inside open popup content doesn't reach the anchor; outside light-dismisses", "[flyout][popup]")
{
    FlyoutWindow w;
    int          clicks = 0;
    w.anchor->on_click([&] { ++clicks; });

    w.flyout->show();
    w.relayout();
    REQUIRE(w.flyout->is_open());

    // A click inside the popup must not also land on the anchor underneath.
    const Point inside = w.content->bounds().center();
    ui::testing::seams(w.window).handle_pointer(down(inside));
    ui::testing::seams(w.window).handle_pointer(up(inside));
    REQUIRE(clicks == 0);
    REQUIRE(w.flyout->is_open());

    ui::testing::seams(w.window).handle_pointer(down(Outside));
    REQUIRE_FALSE(w.flyout->is_open());
}

TEST_CASE("A light dismiss (Escape) hides an open Flyout", "[flyout][popup]")
{
    FlyoutWindow w;
    w.flyout->show();
    w.relayout();

    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    REQUIRE_FALSE(w.flyout->is_open());
}

TEST_CASE("transition_opacity() fades in on show() and out on hide(), lagging the open popup",
          "[flyout][popup][animation]")
{
    FlyoutControl f;
    f.set_anchor(std::make_unique<ButtonControl>("Open"));
    f.set_content(std::make_unique<TextControl>("Body"));
    REQUIRE(f.transition_opacity() == 0.0f);
    REQUIRE_FALSE(ui::testing::popup_open(f));

    f.show();
    REQUIRE(f.transition_opacity() == 0.0f); // Opening just started
    REQUIRE(ui::testing::popup_open(f));

    f.tick(125ms); // halfway through the 250ms open fade
    REQUIRE_THAT(f.transition_opacity(), WithinAbs(0.5f, 0.01f));
    REQUIRE(f.is_open()); // is_open() doesn't wait for the fade to finish

    f.tick(125ms); // Opening -> Open
    REQUIRE(f.transition_opacity() == 1.0f);

    f.hide();
    REQUIRE_FALSE(f.is_open());              // is_open() goes false the instant hide() commits...
    REQUIRE(ui::testing::popup_open(f));     // ...but the popup stays open through the close fade
    REQUIRE(f.transition_opacity() == 1.0f); // Closing just started, still fully visible

    f.tick(125ms); // halfway through the 250ms close fade
    REQUIRE_THAT(f.transition_opacity(), WithinAbs(0.5f, 0.01f));
    REQUIRE(ui::testing::popup_open(f));

    f.tick(125ms); // Closing -> Closed
    REQUIRE(f.transition_opacity() == 0.0f);
    REQUIRE_FALSE(ui::testing::popup_open(f));
}

TEST_CASE("Placement Below flips to Above when there is no room below", "[flyout][popup][placement]")
{
    HeadlessTextMeasurer measurer;

    auto stack  = std::make_unique<StackControl>(Orientation::Vertical);
    auto spacer = std::make_unique<TextControl>();
    spacer->set_min_height(300.0f);
    stack->add(std::move(spacer));

    auto           owned_flyout = std::make_unique<FlyoutControl>();
    FlyoutControl* flyout       = owned_flyout.get();
    flyout->set_placement(PopupPlacement::Below);
    flyout->set_anchor(std::make_unique<ButtonControl>("Open"));
    flyout->set_content(std::make_unique<TextControl>("Flyout content"));
    stack->add(std::move(owned_flyout));

    Window window;
    window.set_stretch_content(false); // laid out for the control at its desired size, centered
    StackControl* root = window.set_content(std::move(stack));

    // The first pass learns the natural height; the second leaves almost no room below the anchor.
    window.layout({300.0f, 600.0f}, measurer);
    const Size client{300.0f, root->bounds().height + 2.0f};
    window.layout(client, measurer);

    flyout->show();
    window.layout(client, measurer);

    REQUIRE(flyout->is_open());
    REQUIRE(flyout->content() != nullptr);
    REQUIRE(flyout->content()->bounds().bottom() <= flyout->bounds().top() + 1.0f);
}

TEST_CASE("Popup content sizes to its own natural width, not clipped to the anchor's width", "[flyout][popup]")
{
    // Flyout content measures at its own natural width. Only ComboBox and Menu take the anchor's
    // width as a hint.
    HeadlessTextMeasurer measurer;
    const std::string    long_text = "A longer line of content than the anchor"; // under the 456 maximum width

    auto           owned  = std::make_unique<FlyoutControl>();
    FlyoutControl* flyout = owned.get();
    ButtonControl* anchor = flyout->set_anchor(std::make_unique<ButtonControl>("Open"));
    flyout->set_content(std::make_unique<TextControl>(long_text));

    Window window;
    window.set_stretch_content(false); // laid out for the control at its desired size, centered
    window.set_content(std::move(owned));
    window.layout({800.0f, 400.0f}, measurer);

    TextControl probe{long_text};
    const float natural_text_width = probe.measure(Size{Unbounded, Unbounded}, measurer, Theme::default_light()).width;
    REQUIRE(natural_text_width > anchor->bounds().width);

    flyout->show();
    window.layout({800.0f, 400.0f}, measurer);

    REQUIRE(flyout->is_open());
    REQUIRE(flyout->content()->bounds().width >= natural_text_width - 1.0f);
}

TEST_CASE("A Flyout opens below its anchor by default, centered on it, 4 DIPs off it", "[flyout][popup][placement]")
{
    // Below by default, centered, `FlyoutMargin` = 4.
    HeadlessTextMeasurer measurer;
    auto                 stack  = std::make_unique<StackControl>(Orientation::Vertical);
    auto                 spacer = std::make_unique<TextControl>();
    spacer->set_min_height(200.0f);
    stack->add(std::move(spacer));
    auto           owned  = std::make_unique<FlyoutControl>();
    FlyoutControl* flyout = owned.get();
    ButtonControl* anchor = flyout->set_anchor(std::make_unique<ButtonControl>("Open"));
    flyout->set_content(std::make_unique<TextControl>("Flyout content"));
    stack->add(std::move(owned));
    Window window;
    window.set_content(std::move(stack));
    window.layout({400.0f, 400.0f}, measurer);

    REQUIRE(flyout->placement() == PopupPlacement::Below);
    flyout->show();
    window.layout({400.0f, 400.0f}, measurer);

    const Rect surface = ui::testing::popup_content(*flyout)->bounds();
    CHECK_THAT(surface.top(), WithinAbs(anchor->bounds().bottom() + 4.0f, 0.01));
    CHECK_THAT(surface.center().x, WithinAbs(anchor->bounds().center().x, 0.5));
}

TEST_CASE("The flyout surface's padding, frame and size limits", "[flyout][popup]")
{
    FlyoutWindow w;
    w.flyout->show();
    w.relayout();

    // 16,15,16,17 of content padding inside the 1px border.
    const Rect surface = ui::testing::popup_content(*w.flyout)->bounds();
    const Rect content = w.content->bounds();
    CHECK_THAT(content.left() - surface.left(), WithinAbs(17.0f, 0.01));
    CHECK_THAT(content.top() - surface.top(), WithinAbs(16.0f, 0.01));

    // Wrapping content wraps at the 456 DIP maximum width rather than growing past it.
    HeadlessTextMeasurer measurer;
    auto                 owned = std::make_unique<FlyoutControl>();
    FlyoutControl*       wide  = owned.get();
    wide->set_anchor(std::make_unique<ButtonControl>("Open"));
    std::string words;
    for (int i = 0; i < 40; ++i)
    {
        words += "word ";
    }
    TextControl* text = wide->set_content(std::make_unique<TextControl>(words));
    text->set_wrapping(TextWrapping::Wrap);
    Window window;
    window.set_stretch_content(false); // laid out for the control at its desired size, centered
    window.set_content(std::move(owned));
    window.layout({1200.0f, 800.0f}, measurer);
    wide->show();
    window.layout({1200.0f, 800.0f}, measurer);
    CHECK(ui::testing::popup_content(*wide)->bounds().width <= 456.0f);
    CHECK(text->bounds().width <= 456.0f - 34.0f);
    CHECK(text->bounds().height > 30.0f); // more than one line
}

TEST_CASE("A Button anchor opens its flyout on click, and a second click closes it", "[flyout][popup]")
{
    // A Button with a flyout opens it after its click.
    FlyoutWindow w;
    int          clicks = 0;
    w.anchor->on_click([&] { ++clicks; });
    REQUIRE(w.anchor->flyout() == w.flyout);

    const Point center = w.anchor->bounds().center();
    ui::testing::seams(w.window).handle_pointer(down(center));
    ui::testing::seams(w.window).handle_pointer(up(center));
    CHECK(clicks == 1);
    CHECK(w.flyout->is_open());

    w.relayout();
    ui::testing::seams(w.window).handle_pointer(down(center));
    ui::testing::seams(w.window).handle_pointer(up(center));
    CHECK(clicks == 2);
    CHECK_FALSE(w.flyout->is_open());
}

TEST_CASE("An open Flyout takes the tab ring and gives focus back to its anchor", "[flyout][popup][focus]")
{
    // The standard show mode: focus moves into the flyout on open and back to what had it on
    // close.
    HeadlessTextMeasurer measurer;
    auto                 owned  = std::make_unique<FlyoutControl>();
    FlyoutControl*       flyout = owned.get();
    ButtonControl*       anchor = flyout->set_anchor(std::make_unique<ButtonControl>("Open"));
    auto                 panel  = std::make_unique<StackControl>(Orientation::Vertical);
    ButtonControl*       first  = panel->add(std::make_unique<ButtonControl>("One"));
    ButtonControl*       second = panel->add(std::make_unique<ButtonControl>("Two"));
    flyout->set_content(std::move(panel));
    Window window;
    window.set_content(std::move(owned));
    window.layout({400.0f, 400.0f}, measurer);

    ui::testing::seams(window).handle_pointer(down(anchor->bounds().center()));
    ui::testing::seams(window).handle_pointer(up(anchor->bounds().center()));
    REQUIRE(window.focused() == flyout); // the anchor's tab stop is the Flyout itself
    window.layout({400.0f, 400.0f}, measurer);
    CHECK(window.focused() == first);

    ui::testing::seams(window).handle_key({Key::Tab, KeyAction::Down});
    CHECK(window.focused() == second);
    ui::testing::seams(window).handle_key({Key::Tab, KeyAction::Down});
    CHECK(window.focused() == first); // Tab cycles inside the flyout

    ui::testing::seams(window).handle_key({Key::Escape, KeyAction::Down});
    REQUIRE_FALSE(flyout->is_open());
    window.layout({400.0f, 400.0f}, measurer);
    CHECK(window.focused() == flyout);
}

TEST_CASE("A button inside the flyout can close it", "[flyout][popup]")
{
    // An "Empty cart" confirmation: the button in the flyout calls Hide().
    HeadlessTextMeasurer measurer;
    auto                 owned   = std::make_unique<FlyoutControl>();
    FlyoutControl*       flyout  = owned.get();
    ButtonControl*       anchor  = flyout->set_anchor(std::make_unique<ButtonControl>("Empty cart"));
    ButtonControl*       confirm = flyout->set_content(std::make_unique<ButtonControl>("Yes, empty my cart"));
    confirm->on_click([flyout] { flyout->hide(); });
    Window window;
    window.set_content(std::move(owned));
    window.layout({400.0f, 400.0f}, measurer);
    ui::testing::seams(window).handle_pointer(down(anchor->bounds().center()));
    ui::testing::seams(window).handle_pointer(up(anchor->bounds().center()));
    window.layout({400.0f, 400.0f}, measurer);
    REQUIRE(flyout->is_open());

    ui::testing::seams(window).handle_pointer(down(confirm->bounds().center()));
    ui::testing::seams(window).handle_pointer(up(confirm->bounds().center()));
    CHECK_FALSE(flyout->is_open());
}

TEST_CASE("ui::Flyout's is_open opens and closes it, and a two-way bind follows a dismiss", "[flyout][spec][binding]")
{
    Property<bool>           open{false};
    int                      opened = 0;
    int                      closed = 0;
    ui::Control<ui::Flyout>  flyout;
    std::unique_ptr<Element> tree = ui::build(ui::Flyout{
        .is_open             = ui::bind(open),
        .placement_alignment = PopupPlacementAlignment::Leading,
        .show_mode           = FlyoutShowMode::Transient,
        .on_opened           = [&] { ++opened; },
        .on_closed           = [&](PopupCloseReason) { ++closed; },
        .control             = flyout,
        .anchor              = ui::Button{.text = "Open"},
        .content             = ui::Text{.text = "Flyout content"},
    });
    CHECK(flyout->placement_alignment() == PopupPlacementAlignment::Leading);
    CHECK(flyout->show_mode() == FlyoutShowMode::Transient);
    CHECK_FALSE(flyout->is_open());
    CHECK(opened == 0); // seeding the closed state raises nothing

    open = true;
    CHECK(flyout->is_open());
    CHECK(opened == 1);

    flyout->hide(); // a dismiss: the property follows once the close fade ends
    flyout->tick(1000ms);
    CHECK(closed == 1);
    CHECK_FALSE(open.get());

    flyout->show();
    CHECK(open.get());
    CHECK(opened == 2);
}

TEST_CASE("A Flyout reports why it closed, and on_closing can keep it open", "[flyout][popup]")
{
    FlyoutWindow                    w;
    std::vector<PopupCloseReason>   closing;
    std::optional<PopupCloseReason> closed;
    bool                            cancel = true;
    w.flyout->on_closing(
        [&](const PopupClosingArgs& args)
        {
            closing.push_back(args.reason);
            return cancel ? ui::Cancel : ui::Proceed;
        });
    w.flyout->on_closed([&](PopupCloseReason reason) { closed = reason; });

    w.flyout->show();
    w.relayout();
    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    REQUIRE(closing == std::vector{PopupCloseReason::LightDismiss});
    CHECK(w.flyout->is_open()); // canceled

    cancel = false;
    ui::testing::seams(w.window).handle_pointer(down(Outside));
    CHECK_FALSE(w.flyout->is_open());
    CHECK_FALSE(closed.has_value()); // not before the close fade ends
    w.flyout->tick(1000ms);
    CHECK(closed == PopupCloseReason::LightDismiss);

    w.flyout->show();
    w.flyout->hide();
    w.flyout->tick(1000ms);
    CHECK(closing.back() == PopupCloseReason::Programmatic);
    CHECK(closed == PopupCloseReason::Programmatic);
}

TEST_CASE("A Flyout without light dismiss ignores outside clicks and Escape", "[flyout][popup]")
{
    FlyoutWindow w;
    CHECK(w.flyout->light_dismiss());
    w.flyout->set_light_dismiss(false);
    w.flyout->show();
    w.relayout();

    ui::testing::seams(w.window).handle_pointer(down(Outside));
    ui::testing::seams(w.window).handle_pointer(up(Outside));
    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    CHECK(w.flyout->is_open());

    w.flyout->hide();
    CHECK_FALSE(w.flyout->is_open());
}

TEST_CASE("ui::Flyout puts a bound is_open back when on_closing cancels", "[flyout][spec][binding]")
{
    Property<bool>           open{true};
    ui::Control<ui::Flyout>  flyout;
    std::unique_ptr<Element> tree = ui::build(ui::Flyout{
        .is_open       = ui::bind(open),
        .light_dismiss = false,
        .on_closing    = [](const ui::PopupClosingArgs& args)
        { return args.reason == PopupCloseReason::Programmatic ? ui::Cancel : ui::Proceed; },
        .control = flyout,
        .anchor  = ui::Button{.text = "Open"},
        .content = ui::Text{.text = "Flyout content"},
    });
    REQUIRE(flyout->is_open());
    CHECK_FALSE(flyout->light_dismiss());

    open = false;
    CHECK(flyout->is_open());
    CHECK(open.get());
}

TEST_CASE("A Flyout opens on a click its anchor did not handle", "[flyout][popup]")
{
    HeadlessTextMeasurer measurer;
    auto                 stack  = std::make_unique<StackControl>(Orientation::Vertical);
    auto                 owned  = std::make_unique<FlyoutControl>();
    FlyoutControl*       flyout = owned.get();
    auto                 card   = std::make_unique<StackControl>(Orientation::Vertical);
    TextControl*         text   = card->add(std::make_unique<TextControl>("Details"));
    CheckBoxControl*     check  = card->add(std::make_unique<CheckBoxControl>("Pinned"));
    flyout->set_anchor(std::move(card));
    flyout->set_content(std::make_unique<TextControl>("More"));
    stack->add(std::move(owned));
    Window window;
    window.set_content(std::move(stack));
    window.layout({400.0f, 400.0f}, measurer);

    // The check box handles its own press: it toggles and the flyout stays shut.
    const Point on_check = check->bounds().center();
    ui::testing::seams(window).handle_pointer(down(on_check));
    ui::testing::seams(window).handle_pointer(up(on_check));
    CHECK(check->checked() == true);
    CHECK_FALSE(flyout->is_open());

    // Nothing handles a press on the text, so the flyout opens.
    const Point on_text = text->bounds().center();
    ui::testing::seams(window).handle_pointer(down(on_text));
    ui::testing::seams(window).handle_pointer(up(on_text));
    CHECK(flyout->is_open());

    // A press on the open content does not reopen or close it.
    window.layout({400.0f, 400.0f}, measurer);
    const Point on_content = flyout->content()->bounds().center();
    ui::testing::seams(window).handle_pointer(down(on_content));
    ui::testing::seams(window).handle_pointer(up(on_content));
    CHECK(flyout->is_open());
}

TEST_CASE("A disabled Button anchor does not open its Flyout", "[flyout][popup]")
{
    FlyoutWindow w;
    w.anchor->set_enabled(false);
    const Point center = w.anchor->bounds().center();
    ui::testing::seams(w.window).handle_pointer(down(center));
    ui::testing::seams(w.window).handle_pointer(up(center));
    CHECK_FALSE(w.flyout->is_open());
}
