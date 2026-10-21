// SPDX-License-Identifier: MIT
#pragma once

#include <source_location>
#include <string>
#include <string_view>
#include <thread>

// ---- the switch ----
// 1 compiles the thread-affinity checks in, 0 compiles them out. Unset, it follows the build
// type: on without NDEBUG, off with it. The CMake option LIBWINUI_THREAD_CHECKS (AUTO, ON, OFF) sets
// it for the library and everything that links it. It never changes a class layout: the owning
// thread ids are stored either way (one std::thread::id per Window, Element and observed
// source) and only the checks are switched, so a consumer built with a different NDEBUG than
// the library still agrees with it on every object it shares.
#ifndef LIBWINUI_THREAD_CHECKS
#ifdef NDEBUG
#define LIBWINUI_THREAD_CHECKS 0
#else
#define LIBWINUI_THREAD_CHECKS 1
#endif
#endif

namespace ui
{

/// Whether this build checks thread affinity (see set_thread_check_handler()); LIBWINUI_THREAD_CHECKS
/// as a constant.
constexpr bool ThreadChecksEnabled = LIBWINUI_THREAD_CHECKS != 0;

/// One wrong-thread call, handed to the thread check handler.
struct ThreadCheckFailure
{
    /// The whole report, e.g. "ui::TextControl::set_text called on thread 1234, but the tree
    /// belongs to thread 5678 (Text.cpp:41)".
    std::string message;
    /// The offending call, e.g. "ui::TextControl::set_text" or "ui::Property::set".
    std::string call;
    /// Where it was made: inside the setter for an element, at the caller for Property::set().
    std::source_location where;
    /// The thread that made it.
    std::thread::id thread;
    /// The thread the tree or property belongs to.
    std::thread::id owner;
};

/// What set_thread_check_handler() installs: called on the offending thread, before the call it
/// reports goes ahead.
using ThreadCheckHandler = void (*)(const ThreadCheckFailure&);

/// Replace what a failed thread check does; nullptr restores the default. Returns the handler
/// it replaced (nullptr for the default). Safe from any thread.
///
/// In a build with checks (LIBWINUI_THREAD_CHECKS, on by default without NDEBUG), a mutation from
/// a thread that does not own what it mutates is reported: an element setter (anything that
/// invalidates through Element::mark_dirty()) on an element attached to a Window, from another
/// thread than the one that constructed the Window; a member of Window other than post(); a
/// write to a Property, ObservableVector or Signal that has notified observers before, from
/// another thread than its first notifying write, and a new observer from such a thread.
/// Reading is never checked, so measuring on worker threads is fine. A detached element (built
/// but not yet under a Window) is unchecked, so a tree may be built on a worker and handed
/// over with Window::post().
///
/// The default handler writes the message to stderr and the debugger output, then breaks into
/// the debugger when one is attached and aborts otherwise, as a failed assert does: the call is
/// a data race, and what it corrupts shows up far from here.
///
/// Example:
/// @code
/// // A test that would rather count than stop.
/// static std::atomic<int> failures{0};
/// ui::set_thread_check_handler([](const ui::ThreadCheckFailure& f) {
///     std::fprintf(stderr, "%s\n", f.message.c_str());
///     ++failures;
/// });
/// @endcode
ThreadCheckHandler set_thread_check_handler(ThreadCheckHandler handler) noexcept;

/// What a failed check does unless set_thread_check_handler() says otherwise: print the message,
/// then break into an attached debugger or abort. Public so a custom handler can fall back to it.
void default_thread_check_handler(const ThreadCheckFailure& failure);

namespace detail
{
// Builds the ThreadCheckFailure and hands it to the installed handler. `call` is a name to show;
// empty takes it from where.function_name(). `subject` is "the tree" or "the property".
void report_thread_violation(std::string_view call, std::string_view subject, std::thread::id owner, std::source_location where);

// While one exists on this thread, element reports name the Property write that led to them.
class WriteScope
{
public:
    explicit WriteScope(const std::source_location& where) noexcept;
    ~WriteScope();
    WriteScope(const WriteScope&)            = delete;
    WriteScope& operator=(const WriteScope&) = delete;

private:
    const std::source_location* outer_;
};
} // namespace detail

} // namespace ui
