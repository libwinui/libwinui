// SPDX-License-Identifier: MIT
// A handler may replace or remove the subtree it sits in: the window keeps that subtree alive until
// the input that ran the handler has been handed all the way through it.
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Dynamic.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"

#include <memory>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

namespace
{
struct Stage
{
    HeadlessTextMeasurer measurer;
    Window               window;

    void layout()
    {
        window.layout({400.0f, 400.0f}, measurer);
    }
    void click(const Element* target)
    {
        const Point at = target->bounds().center();
        seams(window).handle_pointer({at, PointerButton::Left, PointerAction::Down});
        seams(window).handle_pointer({at, PointerButton::Left, PointerAction::Up});
    }
};
} // namespace

TEST_CASE("a button may replace the window content it sits in", "[window][lifetime]")
{
    Stage                   stage;
    Property<std::string>   label{"first"}; // observed by the first page only
    ui::Control<ui::Button> next;
    ElementWatch            first_page;
    bool                    alive_in_handler = false;
    bool                    bound_in_handler = true;

    stage.window.set_content(ui::VerticalStack{
        .content =
            {
                ui::Button{.text = "Next",
                           .on_click =
                               [&]
                           {
                               stage.window.set_content(ui::Text{.text = "second"});
                               alive_in_handler = first_page.get() != nullptr;
                               bound_in_handler = label.observer_count() != 0;
                           },
                           .control = next},
                // After the button, so the stack still has a child to hand the event to.
                ui::Text{.text = ui::bind(label)},
            },
    });
    first_page.reset(stage.window.content());
    stage.layout();
    REQUIRE(label.observer_count() == 1);

    stage.click(next);
    CHECK(alive_in_handler);            // the old page outlived its own handler
    CHECK_FALSE(bound_in_handler);      // but stopped following its bindings at once
    CHECK(first_page.get() == nullptr); // and went once the click had been dispatched
    CHECK(label.observer_count() == 0);
    stage.layout();
    CHECK(dynamic_cast<TextControl*>(stage.window.content()) != nullptr);
}

TEST_CASE("a button may switch the Match page it sits on", "[window][lifetime][dynamic]")
{
    Stage                   stage;
    Property<int>           page{0};
    Property<std::string>   label{"first"};
    ui::Control<ui::Button> next;
    ElementWatch            button;
    bool                    alive_in_handler = false;

    stage.window.set_content(ui::Match{
        .value   = ui::bind(page),
        .content = [&](const int& p) -> ui::Spec
        {
            if (p != 0)
            {
                return ui::Text{.text = "second"};
            }
            return ui::VerticalStack{
                .content =
                    {
                        ui::Button{.text = "Next",
                                   .on_click =
                                       [&]
                                   {
                                       page             = 1;
                                       alive_in_handler = button.get() != nullptr;
                                   },
                                   .control = next},
                        ui::Text{.text = ui::bind(label)},
                    },
            };
        },
    });
    button.reset(next);
    stage.layout();

    stage.click(next);
    CHECK(alive_in_handler);
    CHECK(button.get() == nullptr);
    CHECK(label.observer_count() == 0);
}

// A page switched away from must not see the state the next page was built for, like old table
// columns reading a new table's shorter rows.
TEST_CASE("a page switched away from in a handler no longer follows its bindings", "[window][lifetime][dynamic]")
{
    Stage                   stage;
    Property<int>           page{0};
    Property<std::size_t>   columns{3};
    ui::Control<ui::Button> next;
    ui::Control<ui::Text>   second;
    int                     old_page_updates = 0;

    stage.window.set_content(ui::Match{
        .value   = ui::bind(page),
        .content = [&](const int& p) -> ui::Spec
        {
            if (p != 0)
            {
                return ui::Text{.text    = ui::bind(columns, [](std::size_t n) { return std::to_string(n); }),
                                .control = second};
            }
            return ui::VerticalStack{
                .content =
                    {
                        ui::Button{.text = "Next",
                                   .on_click =
                                       [&]
                                   {
                                       page    = 1;
                                       columns = 1; // what only the new page can show
                                   },
                                   .control = next},
                        ui::Text{.text = ui::bind(columns,
                                                  [&](std::size_t n)
                                                  {
                                                      ++old_page_updates;
                                                      return std::to_string(n);
                                                  })},
                    },
            };
        },
    });
    stage.layout();
    REQUIRE(old_page_updates == 1);

    stage.click(next);
    CHECK(old_page_updates == 1);
    stage.layout();
    REQUIRE(second);
    CHECK(second->text() == "1");
}

// The binding that switched the page may belong to the old page and still be running.
TEST_CASE("a page may be switched away from by one of its own bindings", "[window][lifetime][dynamic]")
{
    Stage                   stage;
    Property<int>           page{0};
    Property<int>           clicks{0};
    ui::Control<ui::Button> next;
    std::string             seen;

    stage.window.set_content(ui::Match{
        .value   = ui::bind(page),
        .content = [&](const int& p) -> ui::Spec
        {
            if (p != 0)
            {
                return ui::Text{.text = "second"};
            }
            return ui::VerticalStack{
                .content =
                    {
                        ui::Button{.text = "Next", .on_click = [&] { clicks = clicks.get() + 1; }, .control = next},
                        ui::Text{.text = ui::bind(clicks,
                                                  [&, tag = std::string{"captured"}](int n)
                                                  {
                                                      if (n > 0)
                                                      {
                                                          page = 1; // retires this binding's own element
                                                          seen = tag;
                                                      }
                                                      return std::to_string(n);
                                                  })},
                    },
            };
        },
    });
    stage.layout();

    stage.click(next);
    CHECK(seen == "captured"); // its captures outlived the switch
    CHECK(clicks.observer_count() == 0);
}

TEST_CASE("a row may remove itself from its stack", "[window][lifetime]")
{
    Stage                 stage;
    Property<std::string> label{"row"};

    auto           stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl*  list  = stack.get();
    auto           row   = std::make_unique<StackControl>(Orientation::Horizontal);
    StackControl*  line  = row.get();
    ButtonControl* drop  = row->add(std::make_unique<ButtonControl>("Remove"));
    row->add(ui::build(ui::Text{.text = ui::bind(label)}));
    stack->add(std::move(row));
    stack->add(std::make_unique<TextControl>("after"));
    stage.window.set_content(std::move(stack));
    stage.layout();

    ElementWatch row_watch{line};
    bool         alive_in_handler = false;
    drop->on_click(
        [&]
        {
            CHECK(list->remove(line));
            alive_in_handler = row_watch.get() != nullptr;
            list->add(std::make_unique<TextControl>("added")); // grows the vector being walked
        });

    stage.click(drop);
    CHECK(alive_in_handler);
    CHECK(row_watch.get() == nullptr);
    CHECK(label.observer_count() == 0);
    CHECK(list->children().size() == 2);
}

TEST_CASE("outside a dispatch a replaced child is destroyed at once", "[lifetime]")
{
    Property<std::string> label{"x"};
    ContentHost           host;
    host.set_child(ui::build(ui::Text{.text = ui::bind(label)}));
    REQUIRE(label.observer_count() == 1);
    host.set_child(std::make_unique<TextControl>("other"));
    CHECK(label.observer_count() == 0);
}

TEST_CASE("a modal may be replaced from its own handler, and empty content is allowed", "[window][lifetime]")
{
    Stage stage;
    stage.window.set_content(ui::Button{.text = "Background"});
    stage.layout();

    ButtonControl* first  = stage.window.show_modal(std::make_unique<ButtonControl>("First"));
    Element*       second = nullptr;
    first->on_click(
        [&]
        {
            // The confirm chain: this one closes, the next one opens.
            stage.window.close_modal();
            second = stage.window.show_modal(std::make_unique<ButtonControl>("Second"));
        });
    stage.layout();

    stage.click(first);
    REQUIRE(stage.window.has_modal());
    CHECK(stage.window.modal() == second); // the earlier close did not take the new one with it

    stage.window.close_modal();
    CHECK_FALSE(stage.window.has_modal());

    stage.window.set_content(std::unique_ptr<Element>{});
    CHECK(stage.window.content() == nullptr);
    stage.layout();
}

TEST_CASE("Tab right after a click that removed a stop skips the dead stop", "[window][lifetime][focus]")
{
    Stage          stage;
    auto           stack  = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl*  host   = stack.get();
    ButtonControl* remove = host->add(std::make_unique<ButtonControl>("Remove the next one"));
    ButtonControl* doomed = host->add(std::make_unique<ButtonControl>("Doomed"));
    remove->on_click([host, doomed] { host->remove(doomed); });
    stage.window.set_content(std::move(stack));
    stage.layout();

    stage.click(remove);
    REQUIRE(host->children().size() == 1);
    // No layout() in between: the ring still had the removed button in it.
    seams(stage.window).handle_key({Key::Tab, KeyAction::Down});
    CHECK(stage.window.focused() == remove); // wrapped around the one stop left
}

TEST_CASE("Tab after the focused button removed itself goes on from where it was", "[window][lifetime][focus]")
{
    Stage          stage;
    auto           stack = std::make_unique<StackControl>(Orientation::Vertical);
    StackControl*  host  = stack.get();
    ButtonControl* first = host->add(std::make_unique<ButtonControl>("First"));
    auto           row   = std::make_unique<StackControl>(Orientation::Horizontal);
    StackControl*  line  = row.get();
    ButtonControl* drop  = row->add(std::make_unique<ButtonControl>("Remove"));
    host->add(std::move(row));
    ButtonControl* second = host->add(std::make_unique<ButtonControl>("Second"));
    ButtonControl* third  = host->add(std::make_unique<ButtonControl>("Third"));
    drop->on_click([host, line] { host->remove(line); });
    stage.window.set_content(std::move(stack));
    stage.layout();

    SECTION("Tab")
    {
        stage.click(drop);
        REQUIRE(stage.window.focused() == nullptr);
        seams(stage.window).handle_key({Key::Tab, KeyAction::Down});
        CHECK(stage.window.focused() == second);
        seams(stage.window).handle_key({Key::Tab, KeyAction::Down});
        CHECK(stage.window.focused() == third);
    }
    SECTION("Shift+Tab, after a layout")
    {
        stage.click(drop);
        stage.layout();
        seams(stage.window).handle_key({Key::Tab, KeyAction::Down, {.shift = true}});
        CHECK(stage.window.focused() == first);
    }
}
