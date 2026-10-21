// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/media/PersonPicture.hpp"
#include "winui/ui/themes/ClassicPalette.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
PersonPictureControl make(PersonPictureControl p, Size available = {Unbounded, Unbounded})
{
    HeadlessTextMeasurer measurer;
    const Size           desired = p.measure(available, measurer, Theme::default_light());
    p.arrange(Rect{0.0f, 0.0f, desired.width, desired.height});
    return p;
}
} // namespace

TEST_CASE("A default-constructed PersonPicture has a 96px diameter and no name/photo", "[person-picture]")
{
    PersonPictureControl p;
    REQUIRE(p.diameter() == 96.0f);
    REQUIRE(p.display_name().empty());
    REQUIRE(p.initials().empty());
    REQUIRE_FALSE(p.is_group());
    REQUIRE(p.profile_picture().empty());
    REQUIRE(p.effective_initials().empty());
}

TEST_CASE("Measured size is a square, clamped to the tighter available axis", "[person-picture][layout]")
{
    PersonPictureControl p = make(PersonPictureControl{}, Size{Unbounded, Unbounded});
    REQUIRE(p.bounds().width == 96.0f);
    REQUIRE(p.bounds().height == 96.0f);

    PersonPictureControl clamped = make(PersonPictureControl{}, Size{50.0f, Unbounded});
    REQUIRE(clamped.bounds().width == 50.0f);
    REQUIRE(clamped.bounds().height == 50.0f);
}

TEST_CASE("effective_initials derives from display_name when initials isn't set", "[person-picture]")
{
    PersonPictureControl single;
    single.set_display_name("Ada");
    REQUIRE(single.effective_initials() == "A");

    PersonPictureControl two;
    two.set_display_name("Ada Lovelace");
    REQUIRE(two.effective_initials() == "AL");

    PersonPictureControl three;
    three.set_display_name("Dr Ada Lovelace");
    REQUIRE(three.effective_initials() == "DL"); // first + last word only
}

TEST_CASE("Explicit initials() wins over a derived display_name", "[person-picture]")
{
    PersonPictureControl p;
    p.set_display_name("Ada Lovelace");
    p.set_initials("XY");
    REQUIRE(p.effective_initials() == "XY");
}

TEST_CASE("set_diameter floors at 1 and marks layout+visual dirty", "[person-picture]")
{
    PersonPictureControl p;
    p.set_diameter(-5.0f);
    REQUIRE(p.diameter() == 1.0f);
    REQUIRE(p.is_dirty(DirtyFlags::Layout));
    REQUIRE(p.is_dirty(DirtyFlags::Visual));
}

TEST_CASE(
    "With neither a photo, initials, nor .group, rendering draws the background ellipse pair plus "
    "one placeholder-glyph text run",
    "[person-picture][render]")
{
    PersonPictureControl p     = make(PersonPictureControl{});
    const Theme          theme = Theme::light();

    TestRenderer renderer;
    p.render(renderer, theme);

    REQUIRE(renderer.count() == 3);
    REQUIRE(renderer.count_of<FillEllipse>() == 1);
    REQUIRE(renderer.count_of<StrokeEllipse>() == 1);
    REQUIRE(renderer.count_of<DrawTextRun>() == 1); // the Contact placeholder glyph
}

TEST_CASE("With initials available, rendering draws the initials text instead of the placeholder glyph",
          "[person-picture][render]")
{
    PersonPictureControl spec;
    spec.set_display_name("Ada Lovelace");
    PersonPictureControl p     = make(std::move(spec));
    const Theme          theme = Theme::light();

    TestRenderer renderer;
    p.render(renderer, theme);

    REQUIRE(renderer.count() == 3);
    const auto& text = std::get<DrawTextRun>(renderer.commands[2]);
    REQUIRE(text.text == "AL");
}

TEST_CASE("With a profile picture set, rendering clips to the circle and draws the photo", "[person-picture][render]")
{
    PersonPictureControl spec;
    spec.set_profile_picture(Bitmap(40, 40, colors::Black));
    PersonPictureControl p     = make(std::move(spec));
    const Theme          theme = Theme::light();

    TestRenderer renderer;
    p.render(renderer, theme);

    REQUIRE(renderer.count() == 5); // fill + stroke + PushClipEllipse + DrawImage + PopClip
    REQUIRE(renderer.count_of<PushClipEllipse>() == 1);
    REQUIRE(renderer.count_of<DrawImage>() == 1);
    REQUIRE(renderer.count_of<PopClip>() == 1);
    const auto& img = std::get<DrawImage>(renderer.commands[3]);
    REQUIRE(img.stretch == Stretch::UniformToFill);
}

TEST_CASE("A group picture wins over an already-set profile picture and initials", "[person-picture][render]")
{
    PersonPictureControl spec;
    spec.set_display_name("Ada Lovelace");
    spec.set_profile_picture(Bitmap(10, 10, colors::Black));
    spec.set_group(true);
    PersonPictureControl p     = make(std::move(spec));
    const Theme          theme = Theme::light();

    TestRenderer renderer;
    p.render(renderer, theme);

    // Background pair plus the People glyph, no image commands.
    REQUIRE(renderer.count() == 3);
    REQUIRE(renderer.count_of<DrawImage>() == 0);
    REQUIRE(renderer.count_of<DrawTextRun>() == 1);
}

TEST_CASE("automation_role() is Text", "[person-picture][automation]")
{
    PersonPictureControl p;
    REQUIRE(p.automation_role() == AutomationRole::Text);
}

TEST_CASE("The placeholder ellipse uses PersonPicture's own dedicated fill/stroke tokens under Theme::fluent()",
          "[person-picture][render][theme]")
{
    // The ellipse is filled with the quaternary alt fill and stroked
    // with the card stroke. Only Theme::fluent() sets person_picture_dedicated_fill.
    PersonPictureControl p     = make(PersonPictureControl{});
    const Theme          theme = Theme::light();
    REQUIRE(ui::detail::controls(theme).person_picture_dedicated_fill);

    TestRenderer renderer;
    p.render(renderer, theme);

    const auto& fill   = std::get<FillEllipse>(renderer.commands[0]);
    const auto& stroke = std::get<StrokeEllipse>(renderer.commands[1]);
    REQUIRE(fill.color == theme.palette.control_alt_fill_quaternary);
    REQUIRE(stroke.color == theme.palette.card_stroke_default);
    REQUIRE_FALSE(fill.color == theme.button_normal.background);
}

TEST_CASE("A non-Fluent theme keeps drawing the placeholder ellipse with button_normal chrome",
          "[person-picture][render][theme]")
{
    // Retro resolvers leave theme.palette empty, so reading it unconditionally would paint
    // transparent chrome.
    PersonPictureControl p     = make(PersonPictureControl{});
    const Theme          theme = Theme::classic(ClassicPalette::windows_standard(), false, "MS Sans Serif");
    REQUIRE_FALSE(ui::detail::controls(theme).person_picture_dedicated_fill);

    TestRenderer renderer;
    p.render(renderer, theme);

    const auto& fill   = std::get<FillEllipse>(renderer.commands[0]);
    const auto& stroke = std::get<StrokeEllipse>(renderer.commands[1]);
    REQUIRE(fill.color == theme.button_normal.background);
    REQUIRE(stroke.color == theme.button_normal.border);
}

TEST_CASE("initials_from_display_name picks a name's initials", "[person-picture]")
{
    REQUIRE(PersonPictureControl::initials_from_display_name("jane doe") == "JD");
    REQUIRE(PersonPictureControl::initials_from_display_name("Jane Doe  (Work)") == "JD"); // trailing brackets dropped
    REQUIRE(PersonPictureControl::initials_from_display_name("   Jane   ") == "J");
    REQUIRE(PersonPictureControl::initials_from_display_name("\"Jane\"  'Doe'") == "JD"); // leading punctuation skipped
    REQUIRE(PersonPictureControl::initials_from_display_name("e\u0301mile zola") == "E\u0301Z"); // combining mark kept
    REQUIRE(PersonPictureControl::initials_from_display_name("\u041E\u043B\u044F \u041F") == "\u041E\u041F"); // Cyrillic
    REQUIRE(PersonPictureControl::initials_from_display_name("\u5F20\u4F1F").empty()); // CJK: symbolic, no initials
    REQUIRE(PersonPictureControl::initials_from_display_name("\u0645\u062D\u0645\u062F").empty()); // Arabic: glyph
    REQUIRE(PersonPictureControl::initials_from_display_name("").empty());
}

TEST_CASE("An explicit Height sizes the picture round, and both take the smaller", "[person-picture][layout]")
{
    PersonPictureControl spec;
    spec.set_height(300.0f);
    PersonPictureControl tall = make(std::move(spec));
    REQUIRE(tall.bounds().width == 300.0f);
    REQUIRE(tall.bounds().height == 300.0f);

    PersonPictureControl both;
    both.set_width(80.0f);
    both.set_height(120.0f);
    REQUIRE(both.effective_diameter() == 80.0f);
}

TEST_CASE("The badge shows the number while there is one, else the icon", "[person-picture][badge]")
{
    PersonPictureControl p;
    REQUIRE_FALSE(p.has_badge());
    p.set_badge_icon(IconGlyph{0xE8BE});
    REQUIRE(p.has_badge());
    REQUIRE(p.badge_content().empty());
    p.set_badge_number(7);
    REQUIRE(p.badge_content() == "7");
    p.set_badge_number(100);
    REQUIRE(p.badge_content() == "99+");
    p.set_badge_number(0);
    p.set_badge_icon(Bitmap(4, 4, colors::Black));
    REQUIRE(p.badge_content().empty());
    REQUIRE(p.has_badge());
}

TEST_CASE("A badge too narrow for its number widens to the left into a pill", "[person-picture][badge]")
{
    PersonPictureControl spec;
    spec.set_badge_number(100);
    PersonPictureControl p     = make(std::move(spec));
    const Rect           badge = p.badge_bounds();
    CHECK(badge.height == 48.0f);
    CHECK(badge.width > badge.height);
    CHECK(badge.right() == 100.0f); // the same right edge as the circle
    CHECK(badge.top() == -4.0f);

    TestRenderer renderer;
    p.render(renderer, Theme::light());
    CHECK(renderer.count_of<FillEllipse>() == 1); // the picture only
    CHECK(renderer.count_of<FillRoundedRect>() == 1);
}

TEST_CASE("The badge is half the picture in its top-right corner, 4 DIPs out", "[person-picture][badge]")
{
    PersonPictureControl spec;
    spec.set_badge_number(3);
    PersonPictureControl p = make(std::move(spec));
    REQUIRE(p.badge_bounds() == Rect{52.0f, -4.0f, 48.0f, 48.0f});

    TestRenderer renderer;
    p.render(renderer, Theme::light());
    REQUIRE(renderer.count_of<FillEllipse>() == 2); // the picture, then the badge
    const auto& text = std::get<DrawTextRun>(renderer.commands.back());
    REQUIRE(text.text == "3");
    REQUIRE(text.color == Theme::light().palette.text_on_accent_primary);
    REQUIRE_THAT(text.style.font_size, WithinAbs(48.0f * 0.6f, 1e-4));
}

TEST_CASE("The automation name is the name, then the badge count", "[person-picture][automation]")
{
    PersonPictureControl p;
    REQUIRE(p.automation_name() == "Person");
    p.set_initials("SB");
    REQUIRE(p.automation_name() == "SB");
    p.set_display_name("Jane Doe");
    REQUIRE(p.automation_name() == "Jane Doe");
    p.set_badge_number(1);
    REQUIRE(p.automation_name() == "Jane Doe, 1 item");
    p.set_badge_number(5);
    REQUIRE(p.automation_name() == "Jane Doe, 5 items");
    p.set_badge_text("messages");
    REQUIRE(p.automation_name() == "Jane Doe, 5 messages");
    p.set_badge_number(0);
    p.set_badge_icon(IconGlyph{0xE8BE});
    REQUIRE(p.automation_name() == "Jane Doe, messages");
    p.set_badge_text("");
    REQUIRE(p.automation_name() == "Jane Doe, icon");
    p.set_group(true);
    REQUIRE(p.automation_name() == "Group, icon");
}
