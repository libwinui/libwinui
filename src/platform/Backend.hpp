// SPDX-License-Identifier: MIT
#pragma once

namespace ui
{
class Clipboard;
class Launcher;
class Window;
class Notifier;
class JumpListHost;
struct MessageBoxOptions;
struct PrintJob;
enum class PrintResult;
enum class MessageBoxResult;
} // namespace ui

// What the portable Window asks of whichever platform backend is compiled in.
namespace ui::detail
{

// Creates the OS window for `window` from its options and live properties, shown unless
// `visible` is false, and activated when shown unless `activate` is false. Does nothing and
// returns true if this backend already hosts it; false when it could not be created or there
// is no backend.
bool create_native_window(Window& window, bool visible, bool activate = true);

// Destroys the OS window this backend made for `window`, if any, and forgets the Window.
// ~Window() calls it, so a Window that dies while shown takes its OS window with it.
void destroy_native_window(Window& window);

// Links the OS spell checker into the app. TextBox and RichEditBox call it when they are made, so
// an app without either leaves the checker out; a window creates it when a control first asks.
void link_spell_checker();

// The platform's clipboard, made on the first call and kept for the life of the process. A
// backend without one returns a clipboard that lives in memory.
Clipboard& platform_clipboard();

// The platform's launcher, made on the first call. A backend without one returns a launcher that
// opens nothing.
Launcher& platform_launcher();

// Shows the OS message box over `owner` (nullptr: none) and returns the answer. A backend without
// one returns the answer that changes nothing: Cancel, No when there is no Cancel, else Ok.
MessageBoxResult platform_message_box(const Window* owner, const MessageBoxOptions& options);

// Shows the print dialog over `owner` (nullptr: none), or uses job.printer, and prints the pages
// chosen. A backend without printing returns Failed.
PrintResult platform_print(const Window* owner, PrintJob& job);

// The platform's desktop notifications, made on the first call. A backend without them returns a
// notifier that shows nothing.
Notifier& platform_notifier();

// The platform's jump list, made on the first call. A backend without one returns a host that
// keeps nothing.
JumpListHost& platform_jump_list_host();

} // namespace ui::detail
