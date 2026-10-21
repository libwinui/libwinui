// SPDX-License-Identifier: MIT
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestNotifier.hpp"
#include "winui/ui/Notification.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ui;

TEST_CASE("notify() reaches the installed notifier, and a click runs on_click", "[notification]")
{
    testing::TestNotifier notifier;
    testing::set_notifier(&notifier);
    int clicks = 0;

    notify(
        {.title = "Export finished", .text = "report.pdf", .icon = NotificationIcon::Info, .on_click = [&] { ++clicks; }});
    REQUIRE(notifier.shown.size() == 1);
    CHECK(notifier.shown[0].title == "Export finished");
    CHECK(notifier.shown[0].text == "report.pdf");
    CHECK(notifier.shown[0].icon == NotificationIcon::Info);

    notifier.click(0);
    CHECK(clicks == 1);
    notify({.title = "No handler"});
    notifier.click(1); // nothing to run
    notifier.click(7); // out of range
    CHECK(clicks == 1);
    CHECK(Notification{}.icon == NotificationIcon::App);
}

TEST_CASE("a TestNotifier uninstalls itself, and HeadlessUI installs one", "[notification]")
{
    {
        testing::TestNotifier notifier;
        testing::set_notifier(&notifier);
        REQUIRE(detail::installed_notifier() == &notifier);
    }
    REQUIRE(detail::installed_notifier() == nullptr);

    testing::HeadlessUI ui;
    (void)ui.window();
    REQUIRE(ui.notifier() != nullptr);
    notify({.title = "Hello"});
    REQUIRE(ui.notifier()->shown.size() == 1);
}
