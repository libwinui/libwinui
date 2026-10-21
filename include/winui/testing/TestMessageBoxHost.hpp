// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/MessageBox.hpp"

#include <vector>

namespace ui::testing
{

/// MessageBoxHost test double: shows nothing, records every box it was asked for and answers
/// with `answer`.
///
/// Install it with set_message_box_host(); ui::message_box() then reaches it. It uninstalls
/// itself when destroyed.
///
/// Example:
/// @code
/// ui::testing::TestMessageBoxHost boxes;
/// ui::testing::set_message_box_host(&boxes);
/// boxes.answer = ui::MessageBoxResult::No;
///
/// const ui::MessageBoxResult result =
///     ui::message_box({.text = "Save changes?", .buttons = ui::MessageBoxButtons::YesNo});
/// REQUIRE(result == ui::MessageBoxResult::No);
/// REQUIRE(boxes.shown.size() == 1);
/// REQUIRE(boxes.shown[0].text == "Save changes?");
/// @endcode
class TestMessageBoxHost final : public MessageBoxHost
{
public:
    TestMessageBoxHost() = default;
    /// Uninstalls it if it is still the installed host.
    ~TestMessageBoxHost() override
    {
        if (ui::detail::installed_message_box_host() == this)
        {
            ui::detail::install_message_box_host(nullptr);
        }
    }
    TestMessageBoxHost(const TestMessageBoxHost&)            = delete;
    TestMessageBoxHost& operator=(const TestMessageBoxHost&) = delete;

    /// Appends `options` to `shown` and `owner` to `owners`, and returns `answer`.
    MessageBoxResult show(const Window* owner, const MessageBoxOptions& options) override
    {
        shown.push_back(options);
        owners.push_back(owner);
        return answer;
    }

    /// What every box answers.
    MessageBoxResult answer = MessageBoxResult::Ok;
    /// Every box shown so far, oldest first.
    std::vector<MessageBoxOptions> shown;
    /// The owner of each box in `shown`, nullptr for a box without one.
    std::vector<const Window*> owners;
};

/// Makes ui::message_box() use `host` for the whole process; nullptr goes back to the platform's.
/// Non-owning: `host` must stay alive while installed (a TestMessageBoxHost uninstalls itself
/// when destroyed).
///
/// Example:
/// @code
/// ui::testing::TestMessageBoxHost boxes;
/// ui::testing::set_message_box_host(&boxes);
/// ui::message_box({.text = "Done"});
/// REQUIRE(boxes.shown.size() == 1);
/// @endcode
inline void set_message_box_host(MessageBoxHost* host)
{
    ui::detail::install_message_box_host(host);
}

} // namespace ui::testing
