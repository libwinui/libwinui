// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string>

namespace ui
{

/// What opens URIs and files with the apps the OS has registered for them. ui::launch_uri() and
/// ui::launch_file() go through it, and so do a hyperlink Button's and a Hyperlink inline's
/// `navigate_uri`.
///
/// There is one for the whole process: the platform's, or a double a test installs with
/// `ui::testing::set_launcher()`. App code calls the free functions and never needs the class.
/// It is public so a test double can derive from it.
class Launcher
{
public:
    virtual ~Launcher() = default;

    /// Opens `uri`, fire and forget, with its default handler (a browser for http/https, the
    /// mail client for mailto, ...). A URI nothing handles is dropped.
    virtual void launch_uri(const std::string& uri) = 0;

    /// Opens `file`, fire and forget, with the app registered for its type. A file that does not
    /// exist is dropped.
    virtual void launch_file(const std::filesystem::path& file) = 0;
};

/// Opens `uri` with whatever app the OS has registered for it, fire and forget. An empty URI,
/// or one nothing handles, does nothing.
///
/// Example:
/// @code
/// window.set_content(ui::Button{.text = "Help", .on_click = [] { ui::launch_uri("https://example.com/help"); }});
/// ui::launch_uri("mailto:support@example.com"); // opens the mail client
/// @endcode
void launch_uri(const std::string& uri);

/// Opens `file` with the app registered for its type, as a double-click in the file manager
/// does, fire and forget. An empty path, or one that does not exist, does nothing.
///
/// Example:
/// @code
/// if (auto path = ui::pick_save_file(window, {.suggested_name = "report.pdf"}))
/// {
///     save_document(*path);
///     ui::launch_file(*path); // shows it in the PDF viewer
/// }
/// @endcode
void launch_file(const std::filesystem::path& file);

namespace detail
{
// The launcher launch_uri() and launch_file() use in place of the platform's, or nullptr for the
// platform's own. ui::testing::set_launcher() is the public way to set it.
void                    install_launcher(Launcher* launcher);
[[nodiscard]] Launcher* installed_launcher();
} // namespace detail

} // namespace ui
