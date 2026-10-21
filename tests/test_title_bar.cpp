// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Symbol.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/hosting/TitleBar.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/media/PersonPicture.hpp"
#include "winui/ui/text/AutoSuggestBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;

namespace
{
std::vector<Element*> focusables(TitleBarControl& bar)
{
    std::vector<Element*> out;
    bar.collect_focusables(out);
    return out;
}
} // namespace

TEST_CASE("A default-constructed TitleBar is empty, compact-height, and has no focusables", "[title_bar]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TitleBarControl bar;
    REQUIRE(bar.title().empty());
    REQUIRE(bar.subtitle().empty());
    REQUIRE(bar.icon().empty());
    REQUIRE_FALSE(bar.is_back_button_visible());
    REQUIRE_FALSE(bar.is_pane_toggle_button_visible());
    REQUIRE(focusables(bar).empty());

    REQUIRE(bar.measure({Unbounded, Unbounded}, m, theme).height == 32.0f); // the compact height
}

TEST_CASE("Setting a title/subtitle grows the measured width", "[title_bar]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TitleBarControl bar;
    const float     empty_width = bar.measure({Unbounded, Unbounded}, m, theme).width;

    bar.set_title("My App");
    const float with_title_width = bar.measure({Unbounded, Unbounded}, m, theme).width;
    REQUIRE(with_title_width > empty_width);

    bar.set_subtitle("Preview");
    const float with_subtitle_width = bar.measure({Unbounded, Unbounded}, m, theme).width;
    REQUIRE(with_subtitle_width > with_title_width);
}

TEST_CASE("A present left header, content, or right header switches TitleBar to the expanded height", "[title_bar]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TitleBarControl bar;
    REQUIRE(bar.measure({Unbounded, Unbounded}, m, theme).height == 32.0f);

    bar.set_content(std::make_unique<ButtonControl>("Search"));
    REQUIRE(bar.measure({Unbounded, Unbounded}, m, theme).height == 48.0f); // the taller height with content
    REQUIRE(bar.content() != nullptr);
}

TEST_CASE("back_button_visible/pane_toggle_button_visible add and remove focusables", "[title_bar][focus]")
{
    TitleBarControl bar;
    REQUIRE(focusables(bar).empty());

    bar.set_back_button_visible(true);
    REQUIRE(focusables(bar).size() == 1);

    bar.set_pane_toggle_button_visible(true);
    REQUIRE(focusables(bar).size() == 2);

    bar.set_back_button_visible(false);
    REQUIRE(focusables(bar).size() == 1);
}

TEST_CASE("is_back_button_enabled(false) removes the back button from the tab order without hiding it",
          "[title_bar][focus]")
{
    TitleBarControl bar;
    bar.set_back_button_visible(true);
    REQUIRE(focusables(bar).size() == 1);

    bar.set_back_button_enabled(false);
    REQUIRE(focusables(bar).empty());      // hidden from Tab, per ButtonControl::focusable() == enabled_
    REQUIRE(bar.is_back_button_visible()); // still drawn, just not interactable
}

TEST_CASE("Clicking the back button fires on_back_requested", "[title_bar][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TitleBarControl bar;
    bar.set_back_button_visible(true);
    const Size desired = bar.measure({300.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    int calls = 0;
    bar.on_back_requested([&] { ++calls; });

    const Rect  back_bounds = focusables(bar)[0]->bounds();
    const Point p{back_bounds.left() + back_bounds.width * 0.5f, back_bounds.top() + back_bounds.height * 0.5f};
    bar.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    bar.handle_pointer({p, PointerButton::Left, PointerAction::Up});

    REQUIRE(calls == 1);
}

TEST_CASE("Clicking the pane-toggle button fires on_pane_toggle_requested", "[title_bar][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TitleBarControl bar;
    bar.set_pane_toggle_button_visible(true);
    const Size desired = bar.measure({300.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    int calls = 0;
    bar.on_pane_toggle_requested([&] { ++calls; });

    const Rect  toggle_bounds = focusables(bar)[0]->bounds();
    const Point p{toggle_bounds.left() + toggle_bounds.width * 0.5f, toggle_bounds.top() + toggle_bounds.height * 0.5f};
    bar.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    bar.handle_pointer({p, PointerButton::Left, PointerAction::Up});

    REQUIRE(calls == 1);
}

TEST_CASE("collect_focusables recurses into the left header, content, and right header slots", "[title_bar][focus]")
{
    TitleBarControl bar;
    REQUIRE(focusables(bar).empty());

    ButtonControl* left = bar.set_left_header_content(std::make_unique<ButtonControl>("Left"));
    REQUIRE(focusables(bar) == std::vector<Element*>{left});

    ButtonControl* center = bar.set_content(std::make_unique<ButtonControl>("Center"));
    REQUIRE(focusables(bar) == std::vector<Element*>{left, center});

    ButtonControl* right = bar.set_right_header_content(std::make_unique<ButtonControl>("Right"));
    REQUIRE(focusables(bar) == std::vector<Element*>{left, center, right});
}

TEST_CASE("Title/subtitle measure at the Caption type-ramp role (12px), not the 14px body role", "[title_bar][layout]")
{
    // The title and the subtitle both use the Caption text style.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    REQUIRE_FALSE(theme.type.caption.font_size == theme.text_style.font_size);

    TitleBarControl bar{"My App"};
    const float     measured_width = bar.measure({Unbounded, Unbounded}, m, theme).width;

    const float caption_only_width = m.measure("My App", theme.type.caption).width;
    const float body_only_width    = m.measure("My App", theme.text_style).width;
    REQUIRE_FALSE(caption_only_width == body_only_width);

    // The width also holds the padding constants, so compare the delta against an empty bar.
    TitleBarControl empty;
    const float     empty_width = empty.measure({Unbounded, Unbounded}, m, theme).width;
    constexpr float TitleGap    = 8.0f; // the title's right inset (TitleBar.cpp)
    REQUIRE_THAT(measured_width - empty_width, Catch::Matchers::WithinAbs(caption_only_width + TitleGap, 1e-3));
}

TEST_CASE("Back and pane-toggle buttons sit inset by ButtonMargin within a 44px column, after a 2px left padding",
          "[title_bar][layout]")
{
    // Each button's 2 DIP margin grows its column to 40 + 2*2, and
    // a 2 DIP left padding offsets the whole row.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TitleBarControl bar;
    bar.set_back_button_visible(true);
    bar.set_pane_toggle_button_visible(true);
    const Size desired = bar.measure({300.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});


    const auto fs = focusables(bar);
    REQUIRE(fs.size() == 2);
    REQUIRE_THAT(fs[0]->bounds().left(), Catch::Matchers::WithinAbs(4.0, 1e-3)); // 2 (left pad) + 2 (margin)
    REQUIRE_THAT(fs[0]->bounds().width, Catch::Matchers::WithinAbs(40.0, 1e-3));
    REQUIRE_THAT(fs[1]->bounds().left(), Catch::Matchers::WithinAbs(48.0, 1e-3)); // 2 + 44 (column) + 2
}

TEST_CASE("Render draws the title and subtitle text only when set", "[title_bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TitleBarControl bar{"My App", "Preview"};
    const Size      desired = bar.measure({300.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    bar.render(r, theme);

    bool found_title    = false;
    bool found_subtitle = false;
    for (const auto& c : r.commands)
    {
        if (const auto* run = std::get_if<DrawTextRun>(&c))
        {
            found_title |= run->text == "My App";
            found_subtitle |= run->text == "Preview";
        }
    }
    REQUIRE(found_title);
    REQUIRE(found_subtitle);
}

TEST_CASE("Content is centered at its own width by default, and fills the space when stretched", "[title_bar][layout]")
{
    // The content is centered by default.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TitleBarControl bar{"App"};
    ButtonControl*  content = bar.set_content(std::make_unique<ButtonControl>("Search"));
    REQUIRE(bar.content_alignment() == Alignment::Center);
    (void)bar.measure({600.0f, Unbounded}, m, theme);
    bar.arrange({0.0f, 0.0f, 600.0f, 48.0f});
    const Rect centered = content->bounds();
    CHECK(centered.width < 200.0f);

    bar.set_content_alignment(Alignment::Stretch);
    (void)bar.measure({600.0f, Unbounded}, m, theme);
    bar.arrange({0.0f, 0.0f, 600.0f, 48.0f});
    const Rect stretched = content->bounds();
    CHECK(stretched.width > centered.width + 100.0f);
    CHECK_THAT(stretched.center().x, Catch::Matchers::WithinAbs(centered.center().x, 0.5));
}

TEST_CASE("Stretched content stops 16 DIPs short of the right header", "[title_bar][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TitleBarControl bar{"App"};
    bar.set_content_alignment(Alignment::Stretch);
    ButtonControl* content = bar.set_content(std::make_unique<ButtonControl>("Search"));
    ButtonControl* person  = bar.set_right_header_content(std::make_unique<ButtonControl>("JD"));
    (void)bar.measure({600.0f, Unbounded}, m, theme);
    bar.arrange({0.0f, 0.0f, 600.0f, 48.0f});

    CHECK_THAT(person->bounds().left() - content->bounds().right(), Catch::Matchers::WithinAbs(16.0, 0.5));
}

TEST_CASE("Outside a caption the right header keeps 16 DIPs from the right edge", "[title_bar][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TitleBarControl bar{"App"};
    ButtonControl*  person = bar.set_right_header_content(std::make_unique<ButtonControl>("JD"));
    (void)bar.measure({600.0f, Unbounded}, m, theme);
    bar.arrange({0.0f, 0.0f, 600.0f, 48.0f});

    CHECK_THAT(600.0f - person->bounds().right(), Catch::Matchers::WithinAbs(16.0, 0.5));
}

TEST_CASE("An image icon source draws in the 16x16 icon box, and replaces a glyph", "[title_bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TitleBarControl bar{"App"};
    bar.set_icon(Bitmap{32, 32, colors::White});
    REQUIRE(bar.icon().glyph() == nullptr);

    const Size desired = bar.measure({400.0f, Unbounded}, m, theme);
    bar.arrange({0.0f, 0.0f, 400.0f, desired.height});

    TestRenderer r;
    bar.render(r, theme);
    const DrawImageRect* image = nullptr;
    for (const auto& c : r.commands)
    {
        if (const auto* d = std::get_if<DrawImageRect>(&c))
        {
            image = d;
        }
    }
    REQUIRE(image != nullptr);
    CHECK_THAT(image->bounds.width, Catch::Matchers::WithinAbs(16.0, 1e-3));
    CHECK_THAT(image->bounds.height, Catch::Matchers::WithinAbs(16.0, 1e-3));
    CHECK_THAT(image->bounds.center().y, Catch::Matchers::WithinAbs(desired.height * 0.5, 1e-3));

    bar.set_icon(IconGlyph{0xE700});
    CHECK(bar.icon().glyph() != nullptr);
}

TEST_CASE("The icon keeps the left-header padding column, narrowed when only one button shows", "[title_bar][layout]")
{
    // The left-header padding column is 14px, or 2px when exactly one of the back button
    // and the pane toggle shows.
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    const auto icon_left = [&](bool back, bool pane)
    {
        TitleBarControl bar{"App"};
        bar.set_icon(Bitmap{16, 16, colors::White});
        bar.set_back_button_visible(back);
        bar.set_pane_toggle_button_visible(pane);
        const Size desired = bar.measure({400.0f, Unbounded}, m, theme);
        bar.arrange({0.0f, 0.0f, 400.0f, desired.height});
        TestRenderer r;
        bar.render(r, theme);
        for (const auto& c : r.commands)
        {
            if (const auto* d = std::get_if<DrawImageRect>(&c))
            {
                return d->bounds.left();
            }
        }
        return -1.0f;
    };

    CHECK_THAT(icon_left(false, false), Catch::Matchers::WithinAbs(2.0 + 14.0, 1e-3));
    CHECK_THAT(icon_left(true, false), Catch::Matchers::WithinAbs(2.0 + 44.0 + 2.0, 1e-3));
    CHECK_THAT(icon_left(false, true), Catch::Matchers::WithinAbs(2.0 + 44.0 + 2.0, 1e-3));
    CHECK_THAT(icon_left(true, true), Catch::Matchers::WithinAbs(2.0 + 88.0 + 14.0, 1e-3));
}

TEST_CASE("TitleBar centers its content and headers at their desired height", "[title_bar]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TitleBarControl bar;
    ButtonControl*  left    = bar.set_left_header_content(std::make_unique<ButtonControl>("Left"));
    ButtonControl*  content = bar.set_content(std::make_unique<ButtonControl>("Search"));
    ButtonControl*  right   = bar.set_right_header_content(std::make_unique<ButtonControl>("Right"));
    const Size      desired = bar.measure({800.0f, Unbounded}, m, theme);
    bar.arrange({0.0f, 10.0f, 800.0f, desired.height});

    for (const ButtonControl* slot : {left, content, right})
    {
        CHECK(slot->bounds().height < desired.height);
        CHECK_THAT(slot->bounds().center().y, Catch::Matchers::WithinAbs(10.0f + desired.height * 0.5f, 0.51));
    }
}

TEST_CASE("The right header stays inside the bar at every width", "[title_bar][layout]")
{
    for (float width = 200.0f; width <= 1200.0f; width += 7.0f)
    {
        HeadlessTextMeasurer      m;
        Window                    window;
        ui::Control<ui::TitleBar> bar;
        window.set_content(ui::TitleBar{
            .title                = "libwinui Gallery",
            .subtitle             = "Preview",
            .content_alignment    = Alignment::Stretch,
            .control              = bar,
            .content              = ui::AutoSuggestBox{.placeholder_text   = "Search...",
                                                       .query_icon         = ui::symbol_glyph(ui::Symbol::Find, 16.0f),
                                                       .max_width          = 580.0f,
                                                       .vertical_alignment = Alignment::Center},
            .right_header_content = ui::PersonPicture{.initials           = "JD",
                                                      .width              = 30.0f,
                                                      .height             = 30.0f,
                                                      .vertical_alignment = Alignment::Center},
        });
        window.layout({width, 200.0f}, m);
        const Element* person = bar->right_header_content();
        const Element* search = bar->content();
        INFO("width " << width << " bar " << bar->bounds().right() << " search " << search->bounds().left() << ".."
                      << search->bounds().right() << " person " << person->bounds().left() << ".."
                      << person->bounds().right());
        CHECK(person->bounds().right() <= bar->bounds().right());
        CHECK(search->bounds().right() <= person->bounds().left());
    }
}
