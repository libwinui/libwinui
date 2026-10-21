// SPDX-License-Identifier: MIT
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestMessageBoxHost.hpp"
#include "winui/ui/MessageBox.hpp"
#include "winui/ui/Window.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ui;

TEST_CASE("message_box() reaches the installed host with its owner and returns its answer", "[message_box]")
{
    testing::TestMessageBoxHost boxes;
    testing::set_message_box_host(&boxes);
    boxes.answer = MessageBoxResult::Yes;

    Window                 window;
    const MessageBoxResult result = message_box(window,
                                                {.title   = "Notes",
                                                 .heading = "Save changes?",
                                                 .text    = "They are lost otherwise.",
                                                 .buttons = MessageBoxButtons::YesNoCancel,
                                                 .icon    = MessageBoxIcon::Warning});
    REQUIRE(result == MessageBoxResult::Yes);
    REQUIRE(boxes.shown.size() == 1);
    CHECK(boxes.shown[0].title == "Notes");
    CHECK(boxes.shown[0].heading == "Save changes?");
    CHECK(boxes.shown[0].text == "They are lost otherwise.");
    CHECK(boxes.shown[0].buttons == MessageBoxButtons::YesNoCancel);
    CHECK(boxes.shown[0].icon == MessageBoxIcon::Warning);
    CHECK(boxes.owners[0] == &window);

    boxes.answer = MessageBoxResult::Ok;
    REQUIRE(message_box({.text = "Done"}) == MessageBoxResult::Ok);
    REQUIRE(boxes.owners.size() == 2);
    CHECK(boxes.owners[1] == nullptr);
}

TEST_CASE("a TestMessageBoxHost uninstalls itself, and HeadlessUI installs one", "[message_box]")
{
    {
        testing::TestMessageBoxHost boxes;
        testing::set_message_box_host(&boxes);
        REQUIRE(detail::installed_message_box_host() == &boxes);
    }
    REQUIRE(detail::installed_message_box_host() == nullptr);

    testing::HeadlessUI ui;
    (void)ui.window();
    REQUIRE(ui.message_boxes() != nullptr);
    REQUIRE(detail::installed_message_box_host() == ui.message_boxes());
    (void)message_box(ui.window(), {.text = "Hello"});
    REQUIRE(ui.message_boxes()->shown.size() == 1);
}
