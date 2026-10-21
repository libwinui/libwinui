// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ui
{

/// What a second start of the program hands to the one already running
/// (ui::claim_single_instance()).
struct InstanceActivation
{
    /// The second start's arguments, UTF-8, the program first, as ui::command_line() returns
    /// them.
    std::vector<std::string> arguments;
    /// The second start's working directory, which a relative path among `arguments` is relative
    /// to.
    std::filesystem::path working_directory;
};

/// Makes this process the one instance of the program named `id` for the user's session.
///
/// The first process to claim `id` gets true and keeps the claim until it exits. A later start
/// that claims the same `id` hands its command line to the first, which receives it through
/// `on_activated` on its UI thread, and gets false: it should exit without opening a window. The
/// running instance is allowed to bring its window to the front from `on_activated`.
///
/// `id` names the program, not the executable: pick something unique to the app, such as
/// "com.example.notes". Call it from the UI thread, before the first window opens. A process
/// that already holds the claim gets true again.
///
/// Example:
/// @code
/// int main()
/// {
///     ui::Window window{{.title = "Notes"}};
///     if (!ui::claim_single_instance("com.example.notes",
///                                    [&window](ui::InstanceActivation started)
///                                    {
///                                        if (started.arguments.size() > 1)
///                                        {
///                                            open_document(started.working_directory /
///                                                          ui::path_from_utf8(started.arguments[1]));
///                                        }
///                                        window.activate();
///                                    }))
///     {
///         return 0; // the running Notes opens the file
///     }
///     return ui::run(window);
/// }
/// @endcode
[[nodiscard]] bool claim_single_instance(std::string_view id, Callback<void(InstanceActivation)> on_activated);

} // namespace ui
