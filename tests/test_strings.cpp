// SPDX-License-Identifier: MIT
#include "core/StringsDetail.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Strings.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestClipboard.hpp"
#include "winui/testing/TestContextMenuHost.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/media/PersonPicture.hpp"
#include "winui/ui/navigation/Pager.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/status/InfoBar.hpp"
#include "winui/ui/text/TextBox.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;

namespace
{
// Puts the English set back when a test ends, whatever it replaced.
struct StringsScope
{
    StringsScope()                               = default;
    StringsScope(const StringsScope&)            = delete;
    StringsScope& operator=(const StringsScope&) = delete;
    ~StringsScope()
    {
        set_strings(Strings{});
    }
};
} // namespace

TEST_CASE("fill puts each argument in its slot, in the pattern's order", "[strings]")
{
    CHECK(detail::fill("Page {0} of {1}", {"3", "10"}) == "Page 3 of 10");
    CHECK(detail::fill("{1} pages, page {0}", {"3", "10"}) == "10 pages, page 3");
    CHECK(detail::fill("{0}, {0}", {"again"}) == "again, again");
    CHECK(detail::fill("{{0}} is {0}", {"x"}) == "{0} is x");
    CHECK(detail::fill("100%", {}) == "100%");
}

TEST_CASE("fill writes a slot it has no argument for as it is, rather than throwing", "[strings]")
{
    CHECK(detail::fill("{0} and {1}", {"one"}) == "one and {1}");
    CHECK(detail::fill("{name} {0}", {"x"}) == "{name} x");
    CHECK(detail::fill("open { and }", {"x"}) == "open { and }");
    CHECK(detail::fill("{", {"x"}) == "{");
}

TEST_CASE("set_strings replaces the text and raises strings_changed", "[strings]")
{
    const StringsScope scope;
    int                raised = 0;
    const Subscription seen   = strings_changed().observe([&] { ++raised; });

    Strings german          = strings();
    german.toggle_switch.on = "Ein";
    set_strings(std::move(german));
    CHECK(strings().toggle_switch.on == "Ein");
    CHECK(strings().toggle_switch.off == "Off"); // what was not replaced stays
    CHECK(raised == 1);
}

TEST_CASE("Shortcut text takes its key names from the strings", "[strings]")
{
    const StringsScope scope;
    CHECK(shortcut_text(Ctrl + Shift + Key::Delete) == "Ctrl+Shift+Delete");

    Strings german    = strings();
    german.keys.ctrl  = "Strg";
    german.keys.shift = "Umschalt";
    german.keys.del   = "Entf";
    set_strings(std::move(german));
    CHECK(shortcut_text(Ctrl + Shift + Key::Delete) == "Strg+Umschalt+Entf");
    CHECK(shortcut_text(Ctrl + Key::S) == "Strg+S");
    CHECK(key_display_name(Key::Delete) == "Entf");
}

TEST_CASE("A standard command's text and description follow set_strings", "[strings][command]")
{
    const StringsScope scope;
    const Command      copy = standard_command(StandardCommand::Copy);
    CHECK(copy.current().text == "Copy");

    Strings german       = strings();
    german.commands.copy = {"Kopieren", "Die Auswahl kopieren"};
    set_strings(std::move(german));
    CHECK(copy.current().text == "Kopieren");
    CHECK(copy.current().description == "Die Auswahl kopieren");
    CHECK(standard_command(StandardCommand::Copy).current().text == "Kopieren");
}

TEST_CASE("A ToggleSwitch uses the strings' labels until it has its own", "[strings][toggle]")
{
    const StringsScope  scope;
    ToggleSwitchControl toggle;
    CHECK(toggle.checked_text() == "On");

    Strings german           = strings();
    german.toggle_switch.on  = "Ein";
    german.toggle_switch.off = "Aus";
    set_strings(std::move(german));
    CHECK(toggle.checked_text() == "Ein");
    CHECK(toggle.unchecked_text() == "Aus");

    toggle.set_checked_text("Working");
    CHECK(toggle.checked_text() == "Working");
    toggle.set_checked_text(std::nullopt);
    CHECK(toggle.checked_text() == "Ein");

    Control<ToggleSwitch>    own;
    std::unique_ptr<Element> tree = build(ToggleSwitch{.unchecked_text = "Idle", .control = own});
    CHECK(own->checked_text() == "Ein");
    CHECK(own->unchecked_text() == "Idle");
}

TEST_CASE("A Pager's names, tooltips and words follow set_strings", "[strings][pager]")
{
    const StringsScope scope;
    PagerControl       pager;
    pager.set_number_of_pages(5);
    pager.set_display_mode(PagerDisplayMode::ButtonPanel);
    CHECK(pager.first_button()->accessibility().name == "First page");
    REQUIRE(pager.button_panel_button(0) != nullptr);
    CHECK(pager.button_panel_button(0)->accessibility().name == "Page 1");

    Strings german           = strings();
    german.pager.first_page  = "Erste Seite";
    german.pager.prefix      = "Seite";
    german.pager.suffix      = "von";
    german.pager.page_number = "Seite {0}";
    set_strings(std::move(german));
    CHECK(pager.first_button()->accessibility().name == "Erste Seite");
    CHECK(pager.button_panel_button(0)->accessibility().name == "Seite 1");
    CHECK(pager.prefix_text() == "Seite");
    CHECK(pager.suffix_text() == "von");

    pager.set_prefix_text("Blatt");
    CHECK(pager.prefix_text() == "Blatt");
}

TEST_CASE("A TextBox's context menu takes its labels from the strings", "[strings][textbox]")
{
    const StringsScope  scope;
    TestClipboard       clipboard;
    TestContextMenuHost menu;
    set_clipboard(&clipboard);
    clipboard.set_text("pasted");
    TextBoxControl box;
    box.set_text("text");

    Strings german             = strings();
    german.commands.cut.text   = "Ausschneiden";
    german.commands.copy.text  = "Kopieren";
    german.commands.paste.text = "Einfügen";
    german.commands.select_all = {"Alles auswählen", ""};
    set_strings(std::move(german));
    box.handle_context_menu(std::nullopt, menu);
    CHECK(menu.last_texts() == std::vector<std::string>{"Ausschneiden", "Kopieren", "Einfügen", "", "Alles auswählen"});
}

TEST_CASE("PersonPicture names its badge count with the strings' function", "[strings][person-picture]")
{
    const StringsScope   scope;
    PersonPictureControl picture;
    picture.set_display_name("Ada");
    picture.set_badge_number(3);
    CHECK(picture.automation_name() == "Ada, 3 items");

    Strings german              = strings();
    german.person_picture.badge = [](std::size_t n) { return std::to_string(n) + (n == 1 ? " Element" : " Elemente"); };
    german.person_picture.with_badge = "{0} ({1})";
    set_strings(std::move(german));
    CHECK(picture.automation_name() == "Ada (3 Elemente)");
    picture.set_badge_number(1);
    CHECK(picture.automation_name() == "Ada (1 Element)");
}

TEST_CASE("An InfoBar's close button is named, and follows set_strings", "[strings][infobar]")
{
    const StringsScope scope;
    InfoBarControl     bar{"Title", "Message", InfoBarSeverity::Informational, true};
    ButtonControl*     close = nullptr;
    bar.for_each_child([&close](Element& child) { close = dynamic_cast<ButtonControl*>(&child); });
    REQUIRE(close != nullptr);
    CHECK(close->automation_name() == "Close");

    Strings german      = strings();
    german.common.close = "Schließen";
    set_strings(std::move(german));
    CHECK(close->automation_name() == "Schließen");
}

TEST_CASE("A window lays out again when the strings change", "[strings]")
{
    const StringsScope    scope;
    HeadlessUI            ui = create_headless();
    Control<ToggleSwitch> toggle;
    ui.window().set_content(ToggleSwitch{.horizontal_alignment = Alignment::Leading, .control = toggle});
    ui.layout(Size{400.0f, 300.0f});
    const float english = toggle->bounds().width;

    Strings longer           = strings();
    longer.toggle_switch.on  = "A much longer label while on";
    longer.toggle_switch.off = "A much longer label while off";
    set_strings(std::move(longer));
    CHECK(toggle->is_dirty());
    ui.layout(Size{400.0f, 300.0f});
    CHECK(toggle->bounds().width > english);
}
