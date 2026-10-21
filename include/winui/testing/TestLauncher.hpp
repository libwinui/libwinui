// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/Launcher.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ui::testing
{

/// Launcher test double: launches nothing and records every URI and file it was handed.
///
/// Install it with set_launcher(); ui::launch_uri(), ui::launch_file() and a hyperlink Button's or
/// a Hyperlink inline's `navigate_uri` then reach it. It uninstalls itself when destroyed.
///
/// Example:
/// @code
/// ui::testing::HeadlessTextMeasurer measurer;
/// ui::testing::TestLauncher         launcher;
/// ui::testing::set_launcher(&launcher);
/// ui::Window window;
///
/// auto button = std::make_unique<ui::ButtonControl>("Docs");
/// button->set_variant(ui::ButtonVariant::Hyperlink);
/// button->set_navigate_uri("https://example.com/docs");
/// ui::ButtonControl* b = window.set_content(std::move(button));
/// window.layout({400.0f, 400.0f}, measurer);
///
/// const ui::Point p = b->bounds().center();
/// ui::testing::seams(window).handle_pointer({p, ui::PointerButton::Left, ui::PointerAction::Down});
/// ui::testing::seams(window).handle_pointer({p, ui::PointerButton::Left, ui::PointerAction::Up});
/// REQUIRE(launcher.launched == std::vector<std::string>{"https://example.com/docs"});
/// @endcode
class TestLauncher final : public Launcher
{
public:
    TestLauncher() = default;
    /// Uninstalls it if it is still the installed launcher.
    ~TestLauncher() override
    {
        if (ui::detail::installed_launcher() == this)
        {
            ui::detail::install_launcher(nullptr);
        }
    }
    TestLauncher(const TestLauncher&)            = delete;
    TestLauncher& operator=(const TestLauncher&) = delete;

    /// Appends `uri` to `launched`.
    void launch_uri(const std::string& uri) override
    {
        launched.push_back(uri);
    }

    /// Appends `file` to `launched_files`.
    void launch_file(const std::filesystem::path& file) override
    {
        launched_files.push_back(file);
    }

    /// Every URI launched so far, oldest first.
    std::vector<std::string> launched;
    /// Every file launched so far, oldest first.
    std::vector<std::filesystem::path> launched_files;
};

/// Makes ui::launch_uri(), ui::launch_file() and the controls that open links use `launcher`
/// for the whole process; nullptr goes back to the platform's. Non-owning: `launcher` must stay
/// alive while installed (a TestLauncher uninstalls itself when destroyed).
///
/// Example:
/// @code
/// ui::testing::TestLauncher launcher;
/// ui::testing::set_launcher(&launcher);
/// ui::launch_uri("https://example.com");
/// REQUIRE(launcher.launched == std::vector<std::string>{"https://example.com"});
/// @endcode
inline void set_launcher(Launcher* launcher)
{
    ui::detail::install_launcher(launcher);
}

} // namespace ui::testing
