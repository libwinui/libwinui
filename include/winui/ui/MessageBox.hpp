// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Designated.hpp"

#include <string>

namespace ui
{

class Window;

/// The buttons a message box offers (MessageBoxOptions::buttons).
enum class MessageBoxButtons
{
    Ok,          ///< OK. The default.
    OkCancel,    ///< OK and Cancel.
    YesNo,       ///< Yes and No. The box has no close button, so the user has to choose.
    YesNoCancel, ///< Yes, No and Cancel.
    RetryCancel, ///< Retry and Cancel.
};

/// The icon beside a message box's text (MessageBoxOptions::icon).
enum class MessageBoxIcon
{
    None,        ///< No icon. The default.
    Information, ///< The system information icon.
    Warning,     ///< The system warning icon.
    Error,       ///< The system error icon.
};

/// The button the user chose, what ui::message_box() returns. Closing the box with Escape or its
/// close button is Cancel, or Ok on a box whose only button is OK.
enum class MessageBoxResult
{
    Ok,
    Cancel,
    Yes,
    No,
    Retry,
};

/// What ui::message_box() shows. Fill it with designated initializers.
struct MessageBoxOptions
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The box's window title. Empty: the program's file name.
    std::string title{};
    /// A short line in large text above `text`, the question or the problem in a few words.
    /// Empty: none.
    std::string heading{};
    /// The message. Line breaks are kept.
    std::string text{};
    /// The buttons offered.
    MessageBoxButtons buttons = MessageBoxButtons::Ok;
    /// The icon beside the text.
    MessageBoxIcon icon = MessageBoxIcon::None;
};

/// Shows the operating system's own message box over `owner` and waits until the user answers.
/// The owner is disabled meanwhile, and the app's other windows keep painting. The OS draws the
/// box in its own style, whatever theme the app uses. For a box in the app's theme, use a
/// ui::Dialog.
///
/// Example:
/// @code
/// const ui::MessageBoxResult answer = ui::message_box(window,
///     {.title   = "Notes",
///      .heading = "Save changes to todo.txt?",
///      .text    = "Your changes are lost if you don't save them.",
///      .buttons = ui::MessageBoxButtons::YesNoCancel,
///      .icon    = ui::MessageBoxIcon::Warning});
/// if (answer == ui::MessageBoxResult::Yes)
/// {
///     save();
/// }
/// @endcode
MessageBoxResult message_box(const Window& owner, const MessageBoxOptions& options);

/// ui::message_box() without an owner window, for a message before any window exists (a failed
/// start-up) or after the last one closed. The box gets a taskbar button of its own.
///
/// Example:
/// @code
/// int start(bool config_loaded)
/// {
///     if (!config_loaded)
///     {
///         ui::message_box({.title = "Notes",
///                          .text  = "The settings file is damaged.",
///                          .icon  = ui::MessageBoxIcon::Error});
///         return 1;
///     }
///     return 0;
/// }
/// @endcode
MessageBoxResult message_box(const MessageBoxOptions& options);

/// What shows the message boxes ui::message_box() asks for.
///
/// There is one for the whole process: the platform's, or a double a test installs with
/// `ui::testing::set_message_box_host()`. App code calls ui::message_box() and never needs the
/// class. It is public so a test double can derive from it.
class MessageBoxHost
{
public:
    virtual ~MessageBoxHost() = default;

    /// Shows `options` over `owner` (nullptr: no owner) and returns the user's answer.
    virtual MessageBoxResult show(const Window* owner, const MessageBoxOptions& options) = 0;
};

namespace detail
{
// The host message_box() uses in place of the platform's, or nullptr for the platform's own.
// ui::testing::set_message_box_host() is the public way to set it.
void                          install_message_box_host(MessageBoxHost* host);
[[nodiscard]] MessageBoxHost* installed_message_box_host();
} // namespace detail

} // namespace ui
