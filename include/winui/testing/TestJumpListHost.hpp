// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/JumpList.hpp"

#include <filesystem>
#include <utility>
#include <vector>

namespace ui::testing
{

/// JumpListHost test double: touches no jump list, and keeps the tasks it was handed and every
/// recent file.
///
/// Install it with set_jump_list_host(); ui::set_jump_list() and ui::add_recent_file() then reach
/// it. It uninstalls itself when destroyed.
///
/// Example:
/// @code
/// ui::testing::TestJumpListHost jump_list;
/// ui::testing::set_jump_list_host(&jump_list);
///
/// ui::set_jump_list({ui::JumpTask{.title = "New window", .arguments = "--new-window"}});
/// ui::add_recent_file("notes.txt");
/// REQUIRE(jump_list.tasks.size() == 1);
/// REQUIRE(jump_list.recent_files.size() == 1);
/// @endcode
class TestJumpListHost final : public JumpListHost
{
public:
    TestJumpListHost() = default;
    /// Uninstalls it if it is still the installed host.
    ~TestJumpListHost() override
    {
        if (ui::detail::installed_jump_list_host() == this)
        {
            ui::detail::install_jump_list_host(nullptr);
        }
    }
    TestJumpListHost(const TestJumpListHost&)            = delete;
    TestJumpListHost& operator=(const TestJumpListHost&) = delete;

    /// Replaces `tasks` and counts the call.
    void set_tasks(std::vector<JumpTask> new_tasks) override
    {
        tasks = std::move(new_tasks);
        ++set_count;
    }
    /// Appends `path` to `recent_files`.
    void add_recent_file(const std::filesystem::path& path) override
    {
        recent_files.push_back(path);
    }

    /// The tasks of the last ui::set_jump_list().
    std::vector<JumpTask> tasks;
    /// Every file ui::add_recent_file() was given, oldest first.
    std::vector<std::filesystem::path> recent_files;
    /// How many times ui::set_jump_list() was called.
    int set_count = 0;
};

/// Makes ui::set_jump_list() and ui::add_recent_file() use `host` for the whole process; nullptr
/// goes back to the platform's. Non-owning: `host` must stay alive while installed (a
/// TestJumpListHost uninstalls itself when destroyed).
///
/// Example:
/// @code
/// ui::testing::TestJumpListHost jump_list;
/// ui::testing::set_jump_list_host(&jump_list);
/// ui::set_jump_list({});
/// REQUIRE(jump_list.set_count == 1);
/// @endcode
inline void set_jump_list_host(JumpListHost* host)
{
    ui::detail::install_jump_list_host(host);
}

} // namespace ui::testing
