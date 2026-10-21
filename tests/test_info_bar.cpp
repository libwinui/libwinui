// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/status/InfoBar.hpp"
#include "winui/ui/themes/Palette.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

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

// Measures/arranges `bar` at a generous width so every part of the row fits unclipped.
void layout(InfoBarControl& bar, const TextMeasurer& measurer, const Theme& theme)
{
    const Size desired = bar.measure({600.0f, Unbounded}, measurer, theme);
    bar.arrange({0, 0, desired.width, desired.height});
}
} // namespace

TEST_CASE("A default-constructed InfoBar is closed, closable, icon-visible, and Informational", "[info-bar]")
{
    InfoBarControl bar;
    REQUIRE_FALSE(bar.is_open());
    REQUIRE(bar.is_closable());
    REQUIRE(bar.is_icon_visible());
    REQUIRE(bar.severity() == InfoBarSeverity::Informational);
    REQUIRE(bar.title().empty());
    REQUIRE(bar.message().empty());
}

TEST_CASE("The (title, message, severity, is_open) constructor sets every field", "[info-bar]")
{
    InfoBarControl bar{"Saved", "Your changes were saved.", InfoBarSeverity::Success, true};
    REQUIRE(bar.title() == "Saved");
    REQUIRE(bar.message() == "Your changes were saved.");
    REQUIRE(bar.severity() == InfoBarSeverity::Success);
    REQUIRE(bar.is_open());
}

TEST_CASE("A closed InfoBar measures to zero size and renders nothing", "[info-bar][layout][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBarControl bar{"Title", "Message"};
    layout(bar, m, theme);
    REQUIRE_THAT(bar.bounds().width, WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(bar.bounds().height, WithinAbs(0.0, 1e-6));

    TestRenderer r;
    bar.render(r, theme);
    REQUIRE(r.count() == 0);
}

TEST_CASE("An open InfoBar measures to the fixed 48px row height", "[info-bar][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBarControl bar{"Title", "Message", InfoBarSeverity::Informational, true};
    layout(bar, m, theme);
    REQUIRE_THAT(bar.bounds().height, WithinAbs(48.0, 1e-3)); // the bar's minimum height
    REQUIRE(bar.bounds().width > 0.0f);
}

TEST_CASE("Measured width grows when the icon or the close button is shown", "[info-bar][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBarControl baseline{"Title", "Message", InfoBarSeverity::Informational, true};
    baseline.set_icon_visible(false);
    baseline.set_closable(false);
    const Size baseline_size = baseline.measure({600.0f, Unbounded}, m, theme);

    InfoBarControl with_icon{"Title", "Message", InfoBarSeverity::Informational, true};
    with_icon.set_closable(false);
    const Size icon_size = with_icon.measure({600.0f, Unbounded}, m, theme);
    REQUIRE(icon_size.width > baseline_size.width);

    InfoBarControl with_close{"Title", "Message", InfoBarSeverity::Informational, true};
    with_close.set_icon_visible(false);
    const Size close_size = with_close.measure({600.0f, Unbounded}, m, theme);
    REQUIRE(close_size.width > baseline_size.width);
}

TEST_CASE("Setters no-op on an actual non-change and mark dirty otherwise", "[info-bar]")
{
    InfoBarControl bar{"Title", "Message"};
    ui::testing::clear_dirty(bar);

    bar.set_title("Title"); // unchanged
    REQUIRE_FALSE(bar.is_dirty());
    bar.set_message("Message"); // unchanged
    REQUIRE_FALSE(bar.is_dirty());
    bar.set_severity(InfoBarSeverity::Informational); // unchanged
    REQUIRE_FALSE(bar.is_dirty());

    bar.set_severity(InfoBarSeverity::Error);
    REQUIRE(bar.is_dirty());
    REQUIRE(bar.severity() == InfoBarSeverity::Error);
}

TEST_CASE("set_open no-ops when the value doesn't already change", "[info-bar]")
{
    InfoBarControl bar{"Title", "Message"}; // starts closed
    int            closing_calls = 0;
    bar.on_closing([&] { ++closing_calls; });

    bar.set_open(false); // already closed
    REQUIRE(closing_calls == 0);
    REQUIRE_FALSE(bar.is_open());
}

TEST_CASE("set_open(false) runs Closing then Closed with a Programmatic reason, and never fires on_close_button_click",
          "[info-bar][events]")
{
    InfoBarControl bar{"Title", "Message", InfoBarSeverity::Informational, true};

    std::vector<std::string> log;
    bar.on_close_button_click([&] { log.push_back("click"); });
    bar.on_closing(
        [&](const PopupClosingArgs& args)
        { log.push_back(args.reason == PopupCloseReason::Programmatic ? "closing(programmatic)" : "closing(button)"); });
    bar.on_closed(
        [&](PopupCloseReason reason)
        { log.push_back(reason == PopupCloseReason::Programmatic ? "closed(programmatic)" : "closed(button)"); });

    bar.set_open(false);

    REQUIRE_FALSE(bar.is_open());
    REQUIRE(log == std::vector<std::string>{"closing(programmatic)", "closed(programmatic)"});
}

TEST_CASE("Clicking the close button fires on_close_button_click, then Closing, then Closed, in that order",
          "[info-bar][events][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBarControl bar{"Title", "Message", InfoBarSeverity::Informational, true};
    layout(bar, m, theme);
    const Point at = bar.close_button_bounds().center();

    std::vector<std::string> log;
    bar.on_close_button_click([&] { log.push_back("click"); });
    bar.on_closing(
        [&](const PopupClosingArgs& args)
        {
            REQUIRE(args.reason == PopupCloseReason::CloseButton);
            log.push_back("closing");
        });
    bar.on_closed(
        [&](PopupCloseReason reason)
        {
            REQUIRE(reason == PopupCloseReason::CloseButton);
            log.push_back("closed");
        });

    bar.handle_pointer(down(at));
    bar.handle_pointer(up(at));

    REQUIRE_FALSE(bar.is_open());
    REQUIRE(log == std::vector<std::string>{"click", "closing", "closed"});
}

TEST_CASE("Setting cancel in on_closing cancels the close: is_open stays true and Closed never fires",
          "[info-bar][events]")
{
    InfoBarControl bar{"Title", "Message", InfoBarSeverity::Informational, true};

    bool closed_fired = false;
    bar.on_closing([] { return ui::Cancel; });
    bar.on_closed([&](PopupCloseReason) { closed_fired = true; });

    bar.set_open(false);

    REQUIRE(bar.is_open());
    REQUIRE_FALSE(closed_fired);
}

TEST_CASE("A non-closable InfoBar has an empty close button rect and joins nothing into the tab ring",
          "[info-bar][focus]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBarControl bar{"Title", "Message", InfoBarSeverity::Informational, true};
    bar.set_closable(false);
    layout(bar, m, theme);

    REQUIRE_THAT(bar.close_button_bounds().width, WithinAbs(0.0, 1e-6));

    std::vector<Element*> focusables;
    bar.collect_focusables(focusables);
    REQUIRE(focusables.empty());
}

TEST_CASE("An open, closable InfoBar's close button joins the tab ring; a closed one does not", "[info-bar][focus]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBarControl bar{"Title", "Message", InfoBarSeverity::Informational, true};
    layout(bar, m, theme);

    std::vector<Element*> open_focusables;
    bar.collect_focusables(open_focusables);
    REQUIRE(open_focusables.size() == 1);

    bar.set_open(false);
    std::vector<Element*> closed_focusables;
    bar.collect_focusables(closed_focusables);
    REQUIRE(closed_focusables.empty());
}

TEST_CASE("Render draws the severity icon as two overlaid glyphs only while .icon_visible is set", "[info-bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    InfoBarControl bar{"Title", "Message", InfoBarSeverity::Warning, true};
    bar.set_closable(false); // keep command counts free of Button's own internal render
    layout(bar, m, theme);

    TestRenderer with_icon;
    bar.render(with_icon, theme);
    REQUIRE(with_icon.count_of<FillRoundedRect>() == 1); // body fill only (no close button)
    REQUIRE(with_icon.count_of<StrokeRoundedRect>() == 1);
    REQUIRE(with_icon.count_of<DrawTextRun>() == 4); // 2 icon glyphs + title + message

    bar.set_icon_visible(false);
    layout(bar, m, theme);
    TestRenderer without_icon;
    bar.render(without_icon, theme);
    REQUIRE(without_icon.count_of<DrawTextRun>() == 2); // title + message only
}

TEST_CASE("Render strokes the border with card_stroke_default, not control_stroke_default", "[info-bar][render]")
{
    // The border is the card stroke, which differs from the control stroke in dark mode.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::fluent(Palette::fluent_dark());

    InfoBarControl bar{"Title", "Message", InfoBarSeverity::Informational, true};
    layout(bar, m, theme);

    TestRenderer r;
    bar.render(r, theme);
    const auto& stroke = std::get<StrokeRoundedRect>(r.commands[1]); // fill, then stroke
    REQUIRE(stroke.color == theme.palette.card_stroke_default);
    REQUIRE_FALSE(stroke.color == theme.palette.control_stroke_default);
}

// Only Theme::fluent() fills `theme.palette`, so reading it under another theme drew opaque black.
TEST_CASE("An InfoBar draws a visible bar under every theme, not a black rectangle", "[info_bar][render][regression]")
{
    HeadlessTextMeasurer measurer;

    for (ThemeVariant v :
         {ThemeVariant::Fluent2,
          ThemeVariant::Fluent,
          ThemeVariant::Mdl2,
          ThemeVariant::Metro,
          ThemeVariant::Aero,
          ThemeVariant::Luna,
          ThemeVariant::Windows9x,
          ThemeVariant::Windows3x,
          ThemeVariant::Windows1x})
    {
        const Theme    theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        InfoBarControl bar{"Title", "Message", InfoBarSeverity::Error, true};
        layout(bar, measurer, theme);

        TestRenderer renderer;
        bar.render(renderer, theme);

        REQUIRE(renderer.count() > 0);
        const auto& body = std::get<FillRoundedRect>(renderer.commands[0]);
        // The body is the first thing drawn and covers the whole control; black here is the bug.
        CHECK(body.bounds == bar.bounds());
        CHECK(body.color != Color{});
        // The title and the severity disc must both differ from the surface under them.
        CHECK(theme.colors().text != body.color);
        CHECK(theme.severity.critical.fill != body.color);
    }
}

TEST_CASE("Luna's InfoBar is a white banner with the era's brick-red severity disc", "[info_bar][render][retro][luna]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Luna}, OsThemeSnapshot{});

    InfoBarControl bar{"Title", "Message", InfoBarSeverity::Error, true};
    layout(bar, measurer, theme);

    TestRenderer renderer;
    bar.render(renderer, theme);

    CHECK(std::get<FillRoundedRect>(renderer.commands[0]).color == colors::White);
    CHECK(std::get<StrokeRoundedRect>(renderer.commands[1]).color == Color::from_argb(0xFF7F9DB9));

    // The disc is `[Window.CloseButton] FillColorHint`, the Close button's own brick red, the
    // part the 2001 Guidelines name as their example of "red = a high impact result".
    bool found_disc = false;
    for (const RenderCommand& c : renderer.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            if (t->color == Color::from_argb(0xFFE45D3D))
            {
                found_disc = true;
            }
        }
    }
    CHECK(found_disc);
}

namespace
{
const std::string
    LongMessage = "A long essential app message for your users to be informed of, acknowledge, or take action on.";

InfoBarControl open_bar(std::string title, std::string message)
{
    return InfoBarControl{std::move(title), std::move(message), InfoBarSeverity::Informational, true};
}

void layout_at(InfoBarControl& bar, float width, const TextMeasurer& measurer, const Theme& theme)
{
    const Size desired = bar.measure({width, Unbounded}, measurer, theme);
    bar.arrange({0.0f, 0.0f, width, desired.height});
}
} // namespace

TEST_CASE("Title and message that fit share one row at the horizontal margins", "[info-bar][layout]")
{
    HeadlessTextMeasurer m;
    InfoBarControl       bar = open_bar("Title", "Message");
    layout_at(bar, 600.0f, m, Theme::light());

    REQUIRE_FALSE(bar.is_vertical());
    REQUIRE_THAT(bar.bounds().height, WithinAbs(48.0, 1e-3));
    REQUIRE_THAT(bar.title_bounds().y, WithinAbs(14.0, 1e-3));   // the title's top margin in a row
    REQUIRE_THAT(bar.message_bounds().y, WithinAbs(14.0, 1e-3)); // the message's top margin in a row
    REQUIRE_THAT(bar.message_bounds().x, WithinAbs(bar.title_bounds().right() + 12.0, 1e-3));
    REQUIRE_THAT(bar.title_bounds().x, WithinAbs(16.0 + 16.0 + 14.0, 1e-3)); // padding, icon, icon margin
}

TEST_CASE("A message too long for the row stacks under the title, wraps, and grows the bar", "[info-bar][layout]")
{
    HeadlessTextMeasurer m;
    InfoBarControl       bar = open_bar("Title", LongMessage);
    layout_at(bar, 400.0f, m, Theme::light());

    REQUIRE(bar.is_vertical());
    REQUIRE_THAT(bar.title_bounds().y, WithinAbs(14.0, 1e-3)); // the stacked panel's top padding
    REQUIRE_THAT(bar.message_bounds().y, WithinAbs(bar.title_bounds().bottom() + 4.0, 1e-3));
    REQUIRE(bar.message_bounds().height > bar.title_bounds().height); // wrapped onto several lines
    REQUIRE(bar.message_bounds().right() <= bar.close_button_bounds().left());
    REQUIRE_THAT(bar.bounds().height, WithinAbs(bar.message_bounds().bottom() + 18.0, 1e-3));
    REQUIRE_THAT(bar.close_button_bounds().y, WithinAbs(5.0, 1e-3));
}

TEST_CASE("A lone title stacks, as a single item always does", "[info-bar][layout]")
{
    HeadlessTextMeasurer m;
    InfoBarControl       bar = open_bar("Title", "");
    layout_at(bar, 600.0f, m, Theme::light());

    REQUIRE(bar.is_vertical());
    REQUIRE_THAT(bar.bounds().height, WithinAbs(std::max(48.0f, 14.0f + bar.title_bounds().height + 18.0f), 1e-3));
}

TEST_CASE("The action button sits after the message and takes the tab stop before the close button",
          "[info-bar][layout][focus]")
{
    HeadlessTextMeasurer m;
    InfoBarControl       bar    = open_bar("Title", "Message");
    ButtonControl*       action = bar.set_action_button_content(std::make_unique<ButtonControl>("Action"));
    REQUIRE(bar.action_button_content() == action);
    layout_at(bar, 700.0f, m, Theme::light());

    REQUIRE_FALSE(bar.is_vertical());
    // The visible button sits at its margins; its focus ring draws outside them.
    REQUIRE_THAT(action->bounds().x, WithinAbs(bar.message_bounds().right() + 16.0, 1e-3));
    REQUIRE_THAT(action->bounds().y, WithinAbs(8.0, 1e-3)); // the action's top margin in a row

    std::vector<Element*> focusables;
    bar.collect_focusables(focusables);
    REQUIRE(focusables.size() == 2);
    REQUIRE(focusables[0] == action);

    bool clicked = false;
    action->on_click([&] { clicked = true; });
    bar.handle_pointer(down(action->bounds().center()));
    bar.handle_pointer(up(action->bounds().center()));
    REQUIRE(clicked);

    bar.clear_action_button_content();
    REQUIRE(bar.action_button_content() == nullptr);
    focusables.clear();
    bar.collect_focusables(focusables);
    REQUIRE(focusables.size() == 1);
}

TEST_CASE("A hyperlink action is pulled 12px left", "[info-bar][layout]")
{
    HeadlessTextMeasurer m;

    InfoBarControl button_bar = open_bar("Title", "Message");
    ButtonControl* plain      = button_bar.set_action_button_content(std::make_unique<ButtonControl>("Link"));
    layout_at(button_bar, 700.0f, m, Theme::light());

    InfoBarControl link_bar = open_bar("Title", "Message");
    ButtonControl* link     = link_bar.set_action_button_content(std::make_unique<ButtonControl>("Link"));
    link->set_variant(ButtonVariant::Hyperlink);
    layout_at(link_bar, 700.0f, m, Theme::light());

    REQUIRE_THAT(link->bounds().x, WithinAbs(plain->bounds().x - 12.0, 1e-3));
    REQUIRE_THAT(link->bounds().width, WithinAbs(plain->bounds().width, 1e-3));
}

TEST_CASE("Custom content takes a row of its own below the title and message, across the whole column",
          "[info-bar][layout]")
{
    HeadlessTextMeasurer m;
    InfoBarControl       bar   = open_bar("Title", "Message");
    ButtonControl*       inner = bar.set_content(std::make_unique<ButtonControl>("Inside"));
    REQUIRE(bar.content() == inner);
    REQUIRE(inner->parent() == &bar);
    layout_at(bar, 600.0f, m, Theme::light());

    // Title and message keep their row exactly as without content.
    REQUIRE_FALSE(bar.is_vertical());
    REQUIRE_THAT(bar.title_bounds().y, WithinAbs(14.0, 1e-3));
    REQUIRE_THAT(bar.title_bounds().x, WithinAbs(16.0 + 16.0 + 14.0, 1e-3));

    // The first row is the icon's and close button's 48; the content starts under it, at the
    // panel's left edge, and runs up to the close button's margin (no panel margin).
    REQUIRE_THAT(bar.content_bounds().y, WithinAbs(48.0, 1e-3));
    REQUIRE_THAT(bar.content_bounds().x, WithinAbs(bar.title_bounds().x, 1e-3));
    REQUIRE_THAT(bar.content_bounds().right(), WithinAbs(bar.close_button_bounds().left() - 5.0, 1e-3));
    REQUIRE(bar.content_bounds().height > 0.0f);
    REQUIRE_THAT(bar.bounds().height, WithinAbs(48.0 + bar.content_bounds().height, 1e-3));
    REQUIRE_THAT(bar.close_button_bounds().y, WithinAbs(5.0, 1e-3)); // the close button stays in row one

    bar.clear_content();
    REQUIRE(bar.content() == nullptr);
    layout_at(bar, 600.0f, m, Theme::light());
    REQUIRE_THAT(bar.bounds().height, WithinAbs(48.0, 1e-3));
    REQUIRE(bar.content_bounds().empty());
}

TEST_CASE("Custom content under a stacked title and message starts below the panel's bottom padding",
          "[info-bar][layout]")
{
    HeadlessTextMeasurer m;
    InfoBarControl       bar = open_bar("Title", LongMessage);
    (void)bar.set_content(std::make_unique<ButtonControl>("Inside"));
    layout_at(bar, 400.0f, m, Theme::light());

    REQUIRE(bar.is_vertical());
    REQUIRE_THAT(bar.content_bounds().y, WithinAbs(bar.message_bounds().bottom() + 18.0, 1e-3));
    REQUIRE_THAT(bar.bounds().height, WithinAbs(bar.content_bounds().bottom(), 1e-3));
}

TEST_CASE("With no title, message or action the content moves up into the first row, centered", "[info-bar][layout]")
{
    HeadlessTextMeasurer m;
    InfoBarControl       bar = open_bar("", "");
    (void)bar.set_content(std::make_unique<ButtonControl>("Inside"));
    layout_at(bar, 600.0f, m, Theme::light());

    const float height = bar.content_bounds().height;
    REQUIRE(height > 0.0f);
    REQUIRE(height < 48.0f);
    REQUIRE_THAT(bar.bounds().height, WithinAbs(48.0, 1e-3));
    REQUIRE_THAT(bar.content_bounds().y, WithinAbs((48.0 - height) / 2.0, 1e-3));

    // An action brings the banner back (a lone item, so it stacks with the vertical padding), and
    // the content returns to its own row under it.
    ButtonControl* action = bar.set_action_button_content(std::make_unique<ButtonControl>("Action"));
    layout_at(bar, 600.0f, m, Theme::light());
    REQUIRE(bar.is_vertical());
    REQUIRE_THAT(bar.content_bounds().y, WithinAbs(action->bounds().bottom() + 18.0, 1e-3));
    REQUIRE_THAT(bar.bounds().height, WithinAbs(bar.content_bounds().bottom(), 1e-3));
}

TEST_CASE("Custom content takes input and focus between the action and the close button, while open",
          "[info-bar][focus]")
{
    HeadlessTextMeasurer m;
    InfoBarControl       bar    = open_bar("Title", "Message");
    ButtonControl*       action = bar.set_action_button_content(std::make_unique<ButtonControl>("Action"));
    ButtonControl*       inner  = bar.set_content(std::make_unique<ButtonControl>("Inside"));
    layout_at(bar, 700.0f, m, Theme::light());

    std::vector<Element*> focusables;
    bar.collect_focusables(focusables);
    REQUIRE(focusables.size() == 3);
    CHECK(focusables[0] == action);
    CHECK(focusables[1] == inner);

    bool clicked = false;
    inner->on_click([&] { clicked = true; });
    bar.handle_pointer(down(inner->bounds().center()));
    bar.handle_pointer(up(inner->bounds().center()));
    CHECK(clicked);

    TestRenderer renderer;
    bar.render(renderer, Theme::light());
    const auto draws_inside = [&]
    {
        return std::ranges::any_of(renderer.commands,
                                   [](const RenderCommand& c)
                                   {
                                       const auto* t = std::get_if<DrawTextRun>(&c);
                                       return t != nullptr && t->text == "Inside";
                                   });
    };
    CHECK(draws_inside());

    bar.set_open(false);
    layout_at(bar, 700.0f, m, Theme::light());
    focusables.clear();
    bar.collect_focusables(focusables);
    CHECK(focusables.empty());
    CHECK(bar.content_bounds().empty());
    renderer.commands.clear();
    bar.render(renderer, Theme::light());
    CHECK_FALSE(draws_inside());
}


namespace
{
// Where a Button draws its plain label: centered in its bounds, as ButtonControl::render_override does.
Rect label_bounds(const ButtonControl& button, const TextMeasurer& measurer, const Theme& theme)
{
    const Rect b    = button.bounds();
    const Size text = measurer.measure(button.text(), theme.text_style);
    return Rect{b.x + (b.width - text.width) / 2.0f, b.y + (b.height - text.height) / 2.0f, text.width, text.height};
}
} // namespace

TEST_CASE("An action in the title's row is centered on the title, not pushed down by its focus-ring room",
          "[info-bar][layout][regression]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    for (const bool hyperlink : {false, true})
    {
        InfoBarControl bar    = open_bar("Title", "Message");
        ButtonControl* action = bar.set_action_button_content(std::make_unique<ButtonControl>("Informational link"));
        if (hyperlink)
        {
            action->set_variant(ButtonVariant::Hyperlink);
        }
        layout_at(bar, 900.0f, m, theme);
        REQUIRE_FALSE(bar.is_vertical());

        // In a row the action's top margin of 8 is the visible button's top; a 30px
        // button there has its center within a pixel of the 14-high text's.
        CHECK_THAT(action->bounds().y, WithinAbs(8.0, 1e-3));
        const float title_center  = bar.title_bounds().center().y;
        const float action_center = action->bounds().center().y;
        CHECK(std::abs(action_center - title_center) <= 1.0f);
        // The label's top lines up with the message's within a pixel.
        CHECK(std::abs(label_bounds(*action, m, theme).y - bar.message_bounds().y) <= 1.0f);
        // 16 after the message; a hyperlink 12 nearer.
        CHECK_THAT(action->bounds().x, WithinAbs(bar.message_bounds().right() + 16.0 - (hyperlink ? 12.0 : 0.0), 1e-3));
        CHECK_THAT(bar.bounds().height, WithinAbs(48.0, 1e-3));
    }
}

TEST_CASE("A stacked hyperlink action's label lines up with the message's left edge; a Button's edge does",
          "[info-bar][layout][regression]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    InfoBarControl button_bar = open_bar("Title", LongMessage);
    ButtonControl* button     = button_bar.set_action_button_content(std::make_unique<ButtonControl>("Action"));
    layout_at(button_bar, 400.0f, m, theme);
    REQUIRE(button_bar.is_vertical());
    // Stacked, the action sits 12 under the message, at the panel's left edge.
    CHECK_THAT(button->bounds().x, WithinAbs(button_bar.message_bounds().x, 1e-3));
    CHECK_THAT(button->bounds().y, WithinAbs(button_bar.message_bounds().bottom() + 12.0, 1e-3));
    CHECK_THAT(button_bar.bounds().height, WithinAbs(button->bounds().bottom() + 18.0, 1e-3));

    InfoBarControl link_bar = open_bar("Title", LongMessage);
    ButtonControl* link     = link_bar.set_action_button_content(std::make_unique<ButtonControl>("Informational link"));
    link->set_variant(ButtonVariant::Hyperlink);
    layout_at(link_bar, 400.0f, m, theme);
    REQUIRE(link_bar.is_vertical());
    // -12 margin, then the button's own 11 left padding: the label starts 1px left of the message.
    CHECK_THAT(link->bounds().x, WithinAbs(link_bar.message_bounds().x - 12.0, 1e-3));
    CHECK_THAT(label_bounds(*link, m, theme).x, WithinAbs(link_bar.message_bounds().x - 1.0, 1e-3));
    CHECK_THAT(link->bounds().y, WithinAbs(link_bar.message_bounds().bottom() + 12.0, 1e-3));
}

TEST_CASE("The close button is a visible 38px square 5 in from the corner, its glyph centered and its corner clickable",
          "[info-bar][layout][regression]")
{
    HeadlessTextMeasurer m;
    InfoBarControl       bar = open_bar("Title", "Message");
    layout_at(bar, 600.0f, m, Theme::light());

    std::vector<Element*> focusables;
    bar.collect_focusables(focusables);
    REQUIRE(focusables.size() == 1);
    auto* close = dynamic_cast<ButtonControl*>(focusables.back());
    REQUIRE(close != nullptr);

    // A 38 DIP close button with a 5 DIP margin, for the visible button;
    // its focus ring draws outside that square.
    const Rect b = close->bounds();
    CHECK_THAT(b.x, WithinAbs(600.0 - 5.0 - 38.0, 1e-3));
    CHECK_THAT(b.y, WithinAbs(5.0, 1e-3));
    CHECK_THAT(b.width, WithinAbs(38.0, 1e-3));
    CHECK_THAT(b.height, WithinAbs(38.0, 1e-3));
    CHECK(b == bar.close_button_bounds());

    REQUIRE(close->content() != nullptr);
    CHECK_THAT(close->content()->bounds().center().x, WithinAbs(b.center().x, 1e-3));
    CHECK_THAT(close->content()->bounds().center().y, WithinAbs(b.center().y, 1e-3));

    // A click one pixel inside the square's corner closes the bar.
    const Point corner{b.x + 1.0f, b.y + 1.0f};
    bar.handle_pointer(down(corner));
    bar.handle_pointer(up(corner));
    CHECK_FALSE(bar.is_open());
}
