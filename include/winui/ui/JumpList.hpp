// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"
#include "winui/ui/WindowShell.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ui
{

/// One entry in the Tasks section of the app's jump list (ui::set_jump_list()): clicking it
/// starts the app again with `arguments` on its command line. Fill it with designated
/// initializers.
struct JumpTask
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The entry's text.
    std::string title{};
    /// The command line the new start gets, as ui::command_line() then returns it. Quote an
    /// argument that has spaces.
    std::string arguments{};
    /// The tooltip on the entry; empty shows none.
    std::string description{};
    /// The picture beside the entry; empty uses the executable's own icon.
    WindowIcon icon{};

    friend bool operator==(const JumpTask&, const JumpTask&) = default;
};

/// Sets the app's jump list, the menu the system shows on a right-click on the app's taskbar
/// button or its entry in Start. `tasks` go in its Tasks section, in this order, above the Recent
/// section that ui::add_recent_file() fills. An empty list removes the tasks.
///
/// A task starts the app again. That second start usually hands its arguments to the instance
/// already running through ui::claim_single_instance() and exits. The system stores the list,
/// and it stays after the app quits until the next call replaces it. The list belongs to the
/// app's taskbar button. When the app has set its own app identity, it is that identity's list,
/// which the app's notifications also use. Otherwise it is the executable's. A system without
/// jump lists ignores the call.
///
/// Example:
/// @code
/// ui::set_jump_list({
///     ui::JumpTask{.title = "New window", .arguments = "--new-window"},
///     ui::JumpTask{.title = "New note", .arguments = "--new-note", .description = "Opens an empty note"},
/// });
/// @endcode
void set_jump_list(std::vector<JumpTask> tasks);

/// Records that the app opened the file at `path`, for the Recent section of its jump list and
/// the system's list of recent files. The jump list shows it only for a file type the app is
/// registered to open. Fire and forget. A system without the list ignores it.
///
/// Example:
/// @code
/// if (const std::optional<std::filesystem::path> file = ui::pick_file(window))
/// {
///     open_document(*file);
///     ui::add_recent_file(*file);
/// }
/// @endcode
void add_recent_file(const std::filesystem::path& path);

/// What keeps the app's jump list for ui::set_jump_list() and ui::add_recent_file().
///
/// There is one for the whole process: the platform's, or a double a test installs with
/// `ui::testing::set_jump_list_host()`. App code calls the two free functions and never needs the
/// class. It is public so a test double can derive from it.
class JumpListHost
{
public:
    virtual ~JumpListHost() = default;

    /// Replaces the Tasks section with `tasks`.
    virtual void set_tasks(std::vector<JumpTask> tasks) = 0;
    /// Adds `path` to the recent files.
    virtual void add_recent_file(const std::filesystem::path& path) = 0;
};

namespace detail
{
// The host set_jump_list() and add_recent_file() use in place of the platform's, or nullptr for
// the platform's own. ui::testing::set_jump_list_host() is the public way to set it.
void                        install_jump_list_host(JumpListHost* host);
[[nodiscard]] JumpListHost* installed_jump_list_host();
} // namespace detail

} // namespace ui
