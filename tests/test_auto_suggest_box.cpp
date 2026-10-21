// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Symbol.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestClipboard.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/text/AutoSuggestBox.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;

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
PointerEvent move(Point p)
{
    return {p, PointerButton::None, PointerAction::Move};
}

Point item_point(const AutoSuggestBoxControl& a, std::size_t index)
{
    const Rect  b     = a.bounds();
    const float row_h = b.height;
    return Point{b.center().x, b.bottom() + row_h * (static_cast<float>(index) + 0.5f)};
}

const Point Outside{-500.0f, -500.0f};

// Returns a unique_ptr because the constructor points text_box_ back at this, so a moved
// value would leave that back-pointer dangling.
std::unique_ptr<AutoSuggestBoxControl> make_box(std::string text = "")
{
    auto                 a = std::make_unique<AutoSuggestBoxControl>(std::move(text));
    HeadlessTextMeasurer measurer;
    const Size           size = a->measure({200.0f, Unbounded}, measurer, Theme::default_light());
    a->arrange({100.0f, 100.0f, size.width, size.height});
    return a;
}

// Tests call relayout() after the popup opens, because sync_popups() only runs inside layout().
struct AutoSuggestBoxWindow
{
    HeadlessTextMeasurer   measurer;
    Window                 window;
    AutoSuggestBoxControl* box;

    AutoSuggestBoxWindow()
    {
        auto owned = std::make_unique<AutoSuggestBoxControl>();
        box        = owned.get();
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

TEST_CASE("A new AutoSuggestBox starts closed, enabled, with empty text and no suggestions", "[auto_suggest_box]")
{
    AutoSuggestBoxControl a;
    REQUIRE(a.enabled());
    REQUIRE_FALSE(a.is_suggestion_list_open());
    REQUIRE(a.text().empty());
    REQUIRE(a.suggestions().empty());
}

TEST_CASE("Constructing with initial text seeds both text() and the Escape-revert baseline", "[auto_suggest_box]")
{
    auto a = make_box("Seattle");
    REQUIRE(a->text() == "Seattle");

    ui::testing::set_focused(*a, true);
    a->set_suggestions({"Seattle, WA", "Seaside, OR"});
    a->handle_key({Key::ArrowDown, KeyAction::Down}); // previews "Seattle, WA"
    REQUIRE(a->text() == "Seattle, WA");

    a->handle_key({Key::Escape, KeyAction::Down});
    REQUIRE(a->text() == "Seattle"); // reverts to the constructor's text, not a typed edit
}

TEST_CASE("Ctrl+V reaches the internal TextBox through AutoSuggestBox", "[auto_suggest_box][clipboard]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);

    TestClipboard clipboard;
    clipboard.set_text("Portland");
    ui::testing::set_clipboard(&clipboard);
    a->handle_key({Key::V, KeyAction::Down, {.ctrl = true}});

    REQUIRE(a->text() == "Portland");
}

TEST_CASE("Typing fires on_text_changed(UserInput) and updates text", "[auto_suggest_box][command]")
{
    auto                                                                a = make_box();
    std::vector<std::pair<std::string, AutoSuggestBoxTextChangeReason>> observed;
    a->on_text_changed([&](const AutoSuggestBoxTextChangedArgs& args) { observed.emplace_back(args.text, args.reason); });

    ui::testing::set_focused(*a, true);
    a->handle_text("H");
    a->handle_text("i");

    REQUIRE(a->text() == "Hi");
    REQUIRE(observed.size() == 2);
    REQUIRE(observed[0] == std::pair{std::string{"H"}, AutoSuggestBoxTextChangeReason::UserInput});
    REQUIRE(observed[1] == std::pair{std::string{"Hi"}, AutoSuggestBoxTextChangeReason::UserInput});
}

TEST_CASE("set_text() does not fire on_text_changed or touch suggestions", "[auto_suggest_box][command]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);
    a->set_suggestions({"One", "Two"});
    int fired = 0;
    a->on_text_changed([&] { ++fired; });

    a->set_text("Preset");

    REQUIRE(a->text() == "Preset");
    REQUIRE(fired == 0);
    REQUIRE(a->suggestions() == std::vector<std::string>{"One", "Two"});
}

TEST_CASE("set_suggestions() opens the popup only while focused and the list is non-empty", "[auto_suggest_box][state]")
{
    auto a = make_box("Se");

    a->set_suggestions({"Seattle", "Seaside"});
    REQUIRE_FALSE(a->is_suggestion_list_open());

    ui::testing::set_focused(*a, true);
    a->set_suggestions({"Seattle", "Seaside"});
    REQUIRE(a->is_suggestion_list_open());

    a->set_suggestions({});
    REQUIRE_FALSE(a->is_suggestion_list_open());
}

TEST_CASE("set_suggestions() while unfocused leaves prior visibility untouched", "[auto_suggest_box][state]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);
    a->set_suggestions({"One"});
    REQUIRE(a->is_suggestion_list_open());

    ui::testing::set_focused(*a, false);
    REQUIRE_FALSE(a->is_suggestion_list_open());

    a->set_suggestions({"One", "Two"});
    REQUIRE_FALSE(a->is_suggestion_list_open());
}

TEST_CASE("Refocusing reopens the popup if text and suggestions both remain", "[auto_suggest_box][state]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);
    a->handle_text("S");
    a->set_suggestions({"Seattle"});
    REQUIRE(a->is_suggestion_list_open());

    ui::testing::set_focused(*a, false);
    REQUIRE_FALSE(a->is_suggestion_list_open());

    ui::testing::set_focused(*a, true);
    REQUIRE(a->is_suggestion_list_open()); // reuses the still-populated suggestions_, no re-filter needed
}

TEST_CASE("Refocusing an empty text box does not reopen the popup", "[auto_suggest_box][state]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);
    a->set_suggestions({"Seattle"});
    a->set_text(""); // set_text() doesn't touch suggestions_, but text() is now empty again
    ui::testing::set_focused(*a, false);

    ui::testing::set_focused(*a, true);
    REQUIRE_FALSE(a->is_suggestion_list_open());
}

TEST_CASE("Arrow-key navigation previews a suggestion through on_text_changed, and Enter picks it",
          "[auto_suggest_box][command]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);
    a->handle_text("S");
    a->set_suggestions({"Seattle", "Seaside", "Sedona"});

    std::vector<std::size_t> chosen;
    a->on_suggestion_chosen([&](std::size_t i) { chosen.push_back(i); });
    std::vector<AutoSuggestBoxTextChangeReason> reasons;
    a->on_text_changed([&](const AutoSuggestBoxTextChangedArgs& args) { reasons.push_back(args.reason); });

    a->handle_key({Key::ArrowDown, KeyAction::Down});
    REQUIRE(a->text() == "Seattle");
    a->handle_key({Key::ArrowDown, KeyAction::Down});
    REQUIRE(a->text() == "Seaside");

    REQUIRE(chosen.empty()); // a highlight is not a pick
    REQUIRE(reasons == std::vector{AutoSuggestBoxTextChangeReason::Suggestion, AutoSuggestBoxTextChangeReason::Suggestion});
    REQUIRE(a->is_suggestion_list_open()); // arrow navigation alone never closes the list

    std::vector<std::string> order;
    a->on_suggestion_chosen([&](std::size_t i) { order.push_back("chosen:" + a->suggestions()[i]); });
    a->on_query_submitted(
        [&](const AutoSuggestBoxQuerySubmittedArgs& args) { order.push_back("submitted:" + args.query_text); });
    a->handle_key({Key::Enter, KeyAction::Down});
    REQUIRE(order == std::vector<std::string>{"chosen:Seaside", "submitted:Seaside"});
}

TEST_CASE("Enter with no suggestion highlighted submits the typed text and picks nothing", "[auto_suggest_box][command]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);
    a->handle_text("S");
    a->set_suggestions({"Seattle", "Seaside"});

    int                        chosen = 0;
    std::optional<std::size_t> submitted_index{42};
    a->on_suggestion_chosen([&](std::size_t) { ++chosen; });
    a->on_query_submitted([&](const AutoSuggestBoxQuerySubmittedArgs& args) { submitted_index = args.chosen_suggestion; });
    a->handle_key({Key::Enter, KeyAction::Down});

    REQUIRE(chosen == 0);
    REQUIRE(submitted_index == std::nullopt);
}

TEST_CASE("Arrow-key navigation past either end reverts to the user's typed text instead of wrapping",
          "[auto_suggest_box][command]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);
    a->handle_text("S");
    a->set_suggestions({"Seattle", "Seaside"});

    a->handle_key({Key::ArrowUp, KeyAction::Down}); // Up from unselected -> last item (index 1)
    REQUIRE(a->text() == "Seaside");
    a->handle_key({Key::ArrowUp, KeyAction::Down}); // Up from the last item steps toward index 0
    REQUIRE(a->text() == "Seattle");
    a->handle_key({Key::ArrowUp, KeyAction::Down}); // Up from index 0 reverts (no wrap to the last item)
    REQUIRE(a->text() == "S");
}

TEST_CASE("A single active suggestion ignores further arrow keys in that direction", "[auto_suggest_box][command]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);
    a->handle_text("S");
    a->set_suggestions({"Seattle"});

    a->handle_key({Key::ArrowDown, KeyAction::Down});
    REQUIRE(a->text() == "Seattle");

    a->handle_key({Key::ArrowDown, KeyAction::Down}); // still only one item -> no-op, no revert
    REQUIRE(a->text() == "Seattle");
}

TEST_CASE("Enter always submits the query, with or without a highlighted suggestion", "[auto_suggest_box][command]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);
    a->handle_text("S");

    std::vector<std::pair<std::string, std::optional<std::size_t>>> submitted;
    a->on_query_submitted([&](const AutoSuggestBoxQuerySubmittedArgs& args)
                          { submitted.emplace_back(args.query_text, args.chosen_suggestion); });

    a->handle_key({Key::Enter, KeyAction::Down});
    REQUIRE(submitted.size() == 1);
    REQUIRE(submitted[0] == std::pair<std::string, std::optional<std::size_t>>{"S", std::nullopt});

    a->set_suggestions({"Seattle", "Seaside"});
    a->handle_key({Key::ArrowDown, KeyAction::Down});
    a->handle_key({Key::Enter, KeyAction::Down});

    REQUIRE(submitted.size() == 2);
    REQUIRE(submitted[1] == std::pair<std::string, std::optional<std::size_t>>{"Seattle", 0});
    REQUIRE_FALSE(a->is_suggestion_list_open());
}

TEST_CASE("Escape reverts the text to what was typed and closes the popup", "[auto_suggest_box][command]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);
    a->handle_text("S");
    a->set_suggestions({"Seattle", "Seaside"});
    a->handle_key({Key::ArrowDown, KeyAction::Down});
    REQUIRE(a->text() == "Seattle");

    a->handle_key({Key::Escape, KeyAction::Down});

    REQUIRE_FALSE(a->is_suggestion_list_open());
    REQUIRE(a->text() == "S");
}

TEST_CASE("Typing while the popup is open keeps editing the text box", "[auto_suggest_box][command]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);
    a->handle_text("S");
    a->set_suggestions({"Seattle"});
    REQUIRE(a->is_suggestion_list_open());

    a->handle_text("e");

    REQUIRE(a->text() == "Se");
}

TEST_CASE("Clicking a suggestion fires on_suggestion_chosen, then on_query_submitted, and closes",
          "[auto_suggest_box][command][popup]")
{
    AutoSuggestBoxWindow w;
    ui::testing::set_focused(*w.box, true);
    w.box->handle_text("S");
    w.box->set_suggestions({"Seattle", "Seaside", "Sedona"});
    w.relayout(); // positions the popup now that is_suggestion_list_open() flipped

    std::vector<std::string> order;
    w.box->on_suggestion_chosen([&](std::size_t i) { order.push_back("chosen:" + w.box->suggestions()[i]); });
    w.box->on_query_submitted(
        [&](const AutoSuggestBoxQuerySubmittedArgs& args) { order.push_back("submitted:" + args.query_text); });

    const Point row1 = item_point(*w.box, 1); // "Seaside"
    ui::testing::seams(w.window).handle_pointer(down(row1));
    ui::testing::seams(w.window).handle_pointer(up(row1));

    REQUIRE(order == std::vector<std::string>{"chosen:Seaside", "submitted:Seaside"});
    REQUIRE_FALSE(w.box->is_suggestion_list_open());
    REQUIRE(w.box->text() == "Seaside");
}

TEST_CASE("A pointer-down outside the popup and its owner light-dismisses it", "[auto_suggest_box][popup]")
{
    AutoSuggestBoxWindow w;
    ui::testing::set_focused(*w.box, true);
    w.box->handle_text("S");
    w.box->set_suggestions({"Seattle"});
    w.relayout();
    REQUIRE(w.box->is_suggestion_list_open());

    ui::testing::seams(w.window).handle_pointer(down(Outside));
    REQUIRE_FALSE(w.box->is_suggestion_list_open());
}

TEST_CASE("Hovering a popup row does not fire callbacks or change the text", "[auto_suggest_box][popup]")
{
    AutoSuggestBoxWindow w;
    ui::testing::set_focused(*w.box, true);
    w.box->handle_text("S");
    w.box->set_suggestions({"Seattle", "Seaside"});
    w.relayout();

    int fired = 0;
    w.box->on_suggestion_chosen([&](std::size_t) { ++fired; });

    ui::testing::seams(w.window).handle_pointer(move(item_point(*w.box, 1)));

    REQUIRE(fired == 0);
    REQUIRE(w.box->text() == "S");
}

TEST_CASE("Disabled AutoSuggestBox ignores input and cannot be focused", "[auto_suggest_box][state]")
{
    auto a = make_box();
    ui::testing::set_focused(*a, true);
    a->handle_text("S");
    a->set_suggestions({"Seattle"});
    REQUIRE(a->is_suggestion_list_open());

    a->set_enabled(false);

    REQUIRE_FALSE(a->is_suggestion_list_open());
    REQUIRE_FALSE(a->focusable());
    a->handle_text("x"); // ignored: enabled_ gates handle_text too
    REQUIRE(a->text() == "S");
}

TEST_CASE("QueryIcon adds a QueryButton that submits the typed text alone", "[auto_suggest_box]")
{
    auto box = make_box("cat");
    box->set_query_icon(symbol_glyph(Symbol::Find));
    box->set_placeholder_text("Type a control name");
    REQUIRE(box->placeholder_text() == "Type a control name");
    const Rect button = box->query_button_rect();
    REQUIRE(button.width == 32.0f);
    REQUIRE(button.right() == box->bounds().right() - 4.0f);

    std::string                query;
    std::optional<std::size_t> chosen{0};
    box->on_query_submitted(
        [&](const AutoSuggestBoxQuerySubmittedArgs& args)
        {
            query  = args.query_text;
            chosen = args.chosen_suggestion;
        });
    box->handle_pointer(PointerEvent{button.center(), PointerButton::Left, PointerAction::Down});
    box->handle_pointer(PointerEvent{button.center(), PointerButton::Left, PointerAction::Up});
    REQUIRE(query == "cat");
    REQUIRE(chosen == std::nullopt);

    box->set_query_icon({});
    REQUIRE(box->query_button_rect().width == 0.0f);
}

TEST_CASE("A long suggestion list stops at its maximum height and scrolls to the keyboard's row",
          "[auto_suggest_box][popup]")
{
    AutoSuggestBoxWindow w;
    ui::testing::set_focused(*w.box, true);
    w.box->handle_text("S");
    std::vector<std::string> many;
    for (int i = 0; i < 40; ++i)
    {
        many.push_back("Suggestion " + std::to_string(i));
    }
    w.box->set_suggestions(many);
    w.relayout();
    const Rect list = ui::testing::popup_content(*w.box)->bounds();
    REQUIRE(list.height <= 374.0f);

    w.box->handle_key({Key::ArrowUp, KeyAction::Down}); // from the typed text to the last
    REQUIRE(w.box->text() == "Suggestion 39");

    std::vector<std::string> chosen;
    w.box->on_query_submitted([&](const AutoSuggestBoxQuerySubmittedArgs& args) { chosen.push_back(args.query_text); });
    const Point last_row{list.center().x, list.bottom() - 4.0f};
    ui::testing::seams(w.window).handle_pointer(down(last_row));
    ui::testing::seams(w.window).handle_pointer(up(last_row));
    REQUIRE(chosen == std::vector<std::string>{"Suggestion 39"});
}

TEST_CASE("A wheel turn over the suggestion list scrolls the list and not the page under it",
          "[auto_suggest_box][popup]")
{
    HeadlessTextMeasurer   measurer;
    Window                 window;
    auto                   page_owned = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl*          page       = page_owned.get();
    AutoSuggestBoxControl* box        = page->add(std::make_unique<AutoSuggestBoxControl>());
    for (int i = 0; i < 60; ++i)
    {
        page->add(std::make_unique<TextControl>("Line " + std::to_string(i)));
    }
    auto           view_owned = std::make_unique<ScrollControl>();
    ScrollControl* view       = view_owned.get();
    view->set_child(std::move(page_owned));
    window.set_content(std::move(view_owned));
    window.layout({400.0f, 400.0f}, measurer);
    REQUIRE(view->scrollable_height() > 0.0f);

    ui::testing::set_focused(*box, true);
    box->handle_text("S");
    std::vector<std::string> many;
    for (int i = 0; i < 40; ++i)
    {
        many.push_back("Suggestion " + std::to_string(i));
    }
    box->set_suggestions(many);
    window.layout({400.0f, 400.0f}, measurer);
    const Rect list = ui::testing::popup_content(*box)->bounds();
    REQUIRE(list.height > 0.0f);

    PointerEvent wheel{list.center(), PointerButton::None, PointerAction::Wheel};
    wheel.wheel_delta = 120.0f;
    ui::testing::seams(window).handle_pointer(wheel);
    REQUIRE(view->vertical_offset() == 0.0f);

    std::vector<std::string> chosen;
    box->on_query_submitted([&](const AutoSuggestBoxQuerySubmittedArgs& args) { chosen.push_back(args.query_text); });
    const Point first_row{list.center().x, list.top() + 4.0f};
    ui::testing::seams(window).handle_pointer(down(first_row));
    ui::testing::seams(window).handle_pointer(up(first_row));
    REQUIRE(chosen.size() == 1);
    REQUIRE(chosen.front() != "Suggestion 0");
}

TEST_CASE("The suggestion list is as wide as the box, not the room to the window's edge", "[auto_suggest_box][popup]")
{
    AutoSuggestBoxWindow w;
    ui::testing::set_focused(*w.box, true);
    w.box->handle_text("S");
    w.box->set_suggestions({"Seattle", "Seaside"});
    w.relayout();
    const Rect list = ui::testing::popup_content(*w.box)->bounds();
    REQUIRE(w.box->bounds().right() < 400.0f);
    REQUIRE(list.left() == w.box->bounds().left());
    REQUIRE(list.width == w.box->bounds().width);
}

TEST_CASE("Automation sees the open suggestions as a list a client can select from and choose",
          "[auto_suggest_box][automation][popup]")
{
    AutoSuggestBoxWindow w;
    CHECK(automation_children(*w.box).empty()); // closed: one node
    ui::testing::set_focused(*w.box, true);
    w.box->handle_text("S");
    w.box->set_suggestions({"Seattle", "Seaside", "Sedona"});
    w.relayout();

    const std::vector<Element*> top = automation_children(*w.box);
    REQUIRE(top.size() == 1);
    Element* list = top[0];
    REQUIRE(list->automation_role() == AutomationRole::List);
    REQUIRE(automation_parent(*list) == w.box);
    const std::vector<Element*> items = automation_children(*list);
    REQUIRE(items.size() == 3);
    CHECK(items[1]->automation_role() == AutomationRole::ListItem);
    CHECK(items[1]->automation_name() == "Seaside");
    REQUIRE(automation_parent(*items[1]) == list);
    CHECK(items[1]->bounds().contains(item_point(*w.box, 1)));

    // Selecting fills the box; invoking chooses, submits and closes.
    REQUIRE(ui::automation_providers(*items[2]).selection_item->select_item());
    CHECK(w.box->text() == "Sedona");
    CHECK(ui::automation_providers(*items[2]).selection_item->item_selected());

    std::vector<std::string> order;
    w.box->on_suggestion_chosen([&](std::size_t i) { order.push_back("chosen:" + w.box->suggestions()[i]); });
    REQUIRE(ui::automation_providers(*items[1]).invoke->invoke());
    CHECK(order == std::vector<std::string>{"chosen:Seaside"});
    CHECK(w.box->text() == "Seaside");
    CHECK_FALSE(w.box->is_suggestion_list_open());
}
