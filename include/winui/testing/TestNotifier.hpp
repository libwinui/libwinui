// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/Notification.hpp"

#include <utility>
#include <vector>

#include <cstddef>

namespace ui::testing
{

/// Notifier test double: shows nothing, keeps every notification it was handed, and clicks one
/// on request.
///
/// Install it with set_notifier(); ui::notify() then reaches it. It uninstalls itself when
/// destroyed.
///
/// Example:
/// @code
/// ui::testing::TestNotifier notifier;
/// ui::testing::set_notifier(&notifier);
/// bool opened = false;
///
/// ui::notify({.title = "Export finished", .on_click = [&] { opened = true; }});
/// REQUIRE(notifier.shown.size() == 1);
/// REQUIRE(notifier.shown[0].title == "Export finished");
/// notifier.click(0);
/// REQUIRE(opened);
/// @endcode
class TestNotifier final : public Notifier
{
public:
    TestNotifier() = default;
    /// Uninstalls it if it is still the installed notifier.
    ~TestNotifier() override
    {
        if (ui::detail::installed_notifier() == this)
        {
            ui::detail::install_notifier(nullptr);
        }
    }
    TestNotifier(const TestNotifier&)            = delete;
    TestNotifier& operator=(const TestNotifier&) = delete;

    /// Appends `notification` to `shown`.
    void notify(Notification notification) override
    {
        shown.push_back(std::move(notification));
    }

    /// What a user's click on `shown[index]` does: runs its `on_click`, if it has one.
    void click(std::size_t index)
    {
        if (index < shown.size() && shown[index].on_click)
        {
            shown[index].on_click();
        }
    }

    /// Every notification shown so far, oldest first.
    std::vector<Notification> shown;
};

/// Makes ui::notify() use `notifier` for the whole process; nullptr goes back to the platform's.
/// Non-owning: `notifier` must stay alive while installed (a TestNotifier uninstalls itself when
/// destroyed).
///
/// Example:
/// @code
/// ui::testing::TestNotifier notifier;
/// ui::testing::set_notifier(&notifier);
/// ui::notify({.title = "Done"});
/// REQUIRE(notifier.shown.size() == 1);
/// @endcode
inline void set_notifier(Notifier* notifier)
{
    ui::detail::install_notifier(notifier);
}

} // namespace ui::testing
