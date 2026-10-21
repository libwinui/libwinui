// SPDX-License-Identifier: MIT
#include "winui/core/ThreadCheck.hpp"

#include <atomic>
#include <sstream>
#include <string>
#include <string_view>

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ui
{

namespace
{
std::atomic<ThreadCheckHandler> installed_handler{nullptr};

thread_local const std::source_location* current_write = nullptr;

// "void __cdecl ui::TextControl::set_text(std::string)" -> "ui::TextControl::set_text": the
// name in front of the parameter list, without the return type and calling convention.
std::string qualified_name(std::string_view signature)
{
    int         depth = 0;
    std::size_t start = 0;
    std::size_t end   = signature.size();
    for (std::size_t i = 0; i < signature.size(); ++i)
    {
        const char c = signature[i];
        if (c == '<')
        {
            ++depth;
        }
        else if (c == '>' && depth > 0)
        {
            --depth;
        }
        else if (depth == 0 && c == ' ')
        {
            start = i + 1;
        }
        else if (depth == 0 && c == '(')
        {
            end = i;
            break;
        }
    }
    if (end <= start)
    {
        return std::string{signature}; // "operator ()" and friends: show it whole
    }
    return std::string{signature.substr(start, end - start)};
}

std::string_view file_name(const std::source_location& where)
{
    const std::string_view path = where.file_name();
    const std::size_t      cut  = path.find_last_of("/\\");
    return cut == std::string_view::npos ? path : path.substr(cut + 1);
}

std::string location(const std::source_location& where)
{
    std::ostringstream out;
    out << file_name(where) << ':' << where.line();
    return out.str();
}
} // namespace

ThreadCheckHandler set_thread_check_handler(ThreadCheckHandler handler) noexcept
{
    return installed_handler.exchange(handler);
}

void default_thread_check_handler(const ThreadCheckFailure& failure)
{
    const std::string line = "libwinui: " + failure.message + "\n";
    std::fputs(line.c_str(), stderr);
    std::fflush(stderr);
#ifdef _WIN32
    OutputDebugStringA(line.c_str());
    if (IsDebuggerPresent())
    {
        DebugBreak();
        return;
    }
#endif
    std::abort();
}

namespace detail
{

void report_thread_violation(std::string_view call, std::string_view subject, std::thread::id owner, std::source_location where)
{
    ThreadCheckFailure failure;
    failure.call   = call.empty() ? qualified_name(where.function_name()) : std::string{call};
    failure.where  = where;
    failure.thread = std::this_thread::get_id();
    failure.owner  = owner;

    std::ostringstream out;
    out << failure.call << " called on thread " << failure.thread << ", but " << subject << " belongs to thread " << owner;
    if (where.line() == 0)
    {
        // No location given: the debugger's stack has the caller.
    }
    else if (call.empty())
    {
        out << " (" << location(where) << ')';
    }
    else
    {
        out << " (from " << qualified_name(where.function_name()) << " at " << location(where) << ')';
    }
    if (current_write != nullptr && current_write != &where && current_write->line() != 0)
    {
        out << "; reached from a ui::Property write at " << location(*current_write);
    }
    failure.message = out.str();

    const ThreadCheckHandler handler = installed_handler.load();
    (handler != nullptr ? handler : &default_thread_check_handler)(failure);
}

WriteScope::WriteScope(const std::source_location& where) noexcept : outer_(current_write)
{
    current_write = &where;
}

WriteScope::~WriteScope()
{
    current_write = outer_;
}

} // namespace detail

} // namespace ui
