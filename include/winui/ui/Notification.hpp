// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Callback.hpp"
#include "winui/core/Designated.hpp"

#include <string>

namespace ui
{

/// The picture a notification carries (Notification::icon).
enum class NotificationIcon
{
    None,    ///< No picture.
    Info,    ///< The system information icon.
    Warning, ///< The system warning icon.
    Error,   ///< The system error icon.
    App,     ///< The executable's icon, or the tray icon for TrayIcon::notify(). The default.
};

/// A desktop notification: what ui::notify() and TrayIcon::notify() show. Fill it with
/// designated initializers.
struct Notification
{
    LIBWINUI_NO_UNIQUE_ADDRESS detail::DesignatedOnly designated_only_{};
    /// The first line, in bold.
    std::string title{};
    /// The message under the title.
    std::string text{};
    /// The picture beside the text.
    NotificationIcon icon = NotificationIcon::App;
    /// The user clicked the notification while the app was running. It runs on the UI thread.
    Callback<void()> on_click{};
};

/// Shows a desktop notification, a toast in the corner of the screen that also goes to the
/// notification center. The app needs no tray icon and no installer. The first call registers
/// the program with the system under its own name, so the toast shows the executable's
/// description (or its file name) and its icon. The user's notification settings decide whether
/// it shows, and do-not-disturb holds it back. Fire and forget. A system without notifications
/// shows nothing.
///
/// A click reaches `on_click` while the app runs. Once the app has quit, clicking the entry in
/// the notification center only removes it.
///
/// Example:
/// @code
/// ui::notify({.title    = "Export finished",
///             .text     = "report.pdf was saved to Documents.",
///             .on_click = [&] { window.activate(); }});
/// @endcode
void notify(Notification notification);

/// What shows the notifications ui::notify() asks for.
///
/// There is one for the whole process: the platform's, or a double a test installs with
/// `ui::testing::set_notifier()`. App code calls ui::notify() and never needs the class. It is
/// public so a test double can derive from it.
class Notifier
{
public:
    virtual ~Notifier() = default;

    /// Shows `notification`.
    virtual void notify(Notification notification) = 0;
};

namespace detail
{
// The notifier notify() uses in place of the platform's, or nullptr for the platform's own.
// ui::testing::set_notifier() is the public way to set it.
void                    install_notifier(Notifier* notifier);
[[nodiscard]] Notifier* installed_notifier();
} // namespace detail

} // namespace ui
