// SPDX-License-Identifier: MIT
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestJumpListHost.hpp"
#include "winui/ui/JumpList.hpp"

#include <filesystem>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;

TEST_CASE("set_jump_list() and add_recent_file() reach the installed host", "[jump_list]")
{
    testing::TestJumpListHost jump_list;
    testing::set_jump_list_host(&jump_list);

    set_jump_list({
        JumpTask{.title = "New window", .arguments = "--new-window"},
        JumpTask{.title       = "New note",
                 .arguments   = "--new-note \"My notes\"",
                 .description = "Opens an empty note",
                 .icon        = {.resource = "#101"}},
    });
    REQUIRE(jump_list.tasks.size() == 2);
    CHECK(jump_list.tasks[0].title == "New window");
    CHECK(jump_list.tasks[1].arguments == "--new-note \"My notes\"");
    CHECK(jump_list.tasks[1].description == "Opens an empty note");
    CHECK(jump_list.tasks[1].icon.resource == "#101");

    set_jump_list({}); // takes the tasks away again
    CHECK(jump_list.tasks.empty());
    CHECK(jump_list.set_count == 2);

    add_recent_file("notes.txt");
    add_recent_file(std::filesystem::path{"report.pdf"});
    CHECK(jump_list.recent_files == std::vector<std::filesystem::path>{"notes.txt", "report.pdf"});
}

TEST_CASE("a TestJumpListHost uninstalls itself, and HeadlessUI installs one", "[jump_list]")
{
    {
        testing::TestJumpListHost jump_list;
        testing::set_jump_list_host(&jump_list);
        REQUIRE(detail::installed_jump_list_host() == &jump_list);
    }
    REQUIRE(detail::installed_jump_list_host() == nullptr);

    {
        testing::HeadlessUI ui;
        (void)ui.window();
        REQUIRE(ui.jump_list() != nullptr);
        set_jump_list({JumpTask{.title = "New window"}});
        CHECK(ui.jump_list()->tasks.size() == 1);
    }
    CHECK(detail::installed_jump_list_host() == nullptr);

    // A host the test installed first is left alone.
    testing::TestJumpListHost own;
    testing::set_jump_list_host(&own);
    testing::HeadlessUI ui;
    (void)ui.window();
    CHECK(ui.jump_list() == nullptr);
    add_recent_file("a.txt");
    CHECK(own.recent_files.size() == 1);
}
