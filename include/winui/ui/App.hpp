// SPDX-License-Identifier: MIT
#pragma once

#include "winui/ui/Window.hpp"

namespace ui
{

/// Show `window` and run the message loop until it is closed; returns the exit code.
///
/// The window is the app's main window. It is created from its WindowOptions if it has no OS
/// window yet, and the loop also runs every other window the app shows (Window::show()) while it
/// is open. Closing the main window ends the loop and closes whatever else is still open. run()
/// only borrows the window: it is an object the app owns, usually a local in `main()`, and it is
/// still a valid, closed Window when run() returns. Not re-entrant: one run() at a time.
///
/// Returns -1 when the window could not be created, or on a platform with no backend.
///
/// Example:
/// @code
/// int main()
/// {
///     ui::Window window{{.title = "Demo", .size = {400, 300}}};
///     ui::Property<int> clicks{0};
///     window.set_content(ui::Button{.text = "Click", .on_click = [&] { ++clicks; }});
///     return ui::run(window);
/// }
/// @endcode
int run(Window& window);

/// Run the message loop with no main window. Every window the app shows runs on it. Closing the
/// last one does not end the loop, only quit() does. Returns quit()'s exit code, and closes
/// whatever is still open on the way out. For apps whose entry point is not a window, such as a
/// tray icon. Not re-entrant.
///
/// Example:
/// @code
/// int main()
/// {
///     ui::Window tool{{.title = "Tool", .size = {300, 200}}};
///     tool.set_content(ui::Button{.text = "Quit", .on_click = [] { ui::quit(); }});
///     tool.show();
///     return ui::run(); // closing the window does not end the loop, quit() does
/// }
/// @endcode
int run();

/// Shows `window` and hands it to the message loop, which destroys it between two messages once it
/// has closed. To get rid of it, call `window.close()` from anywhere, its own handlers included.
/// For an app's secondary windows, such as a tool window, an inspector or a document, that should
/// go away when closed.
///
/// Returns the window, so you can set it up further. To reach it later, keep `window.ref()`. Its
/// get() returns null once the window is gone, and its is_open() tells bound UI when it closes.
/// UI thread only.
///
/// Example:
/// @code
/// auto about = std::make_unique<ui::Window>(ui::WindowOptions{.title = "About", .size = {320, 200}});
/// ui::Window& w = *about;
/// about->set_content(ui::Button{.text = "Close", .on_click = [&w] { w.close(); }});
/// ui::open_window(std::move(about));
/// @endcode
Window& open_window(std::unique_ptr<Window> window);

/// End the running loop (either run()) with `exit_code`. Posted, so the handler calling it returns
/// first. UI thread only.
void quit(int exit_code = 0);

} // namespace ui
