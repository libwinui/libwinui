// SPDX-License-Identifier: MIT
// TraceLogging spans, so Windows Performance Analyzer shows what a frame spent its time on without
// a debugger. Compiled in only with LIBWINUI_ENABLE_TRACING (the CMake option of that name); otherwise
// LIBWINUI_TRACE_SPAN expands to nothing.
//
// The provider is "libwinui" (abddcda8-4aec-5a60-8d55-8a57fbaa3812, the GUID ETW derives from the
// name), so a recording needs nothing more than its name:
//
//     tracelog -start winui -guid *libwinui -f winui.etl
//     tracelog -stop winui
//
// Every span is a SpanStart and a SpanStop event carrying the span's name, the stop also its
// duration in microseconds; WPA's Generic Events table groups them by the Name field.
#pragma once

#if LIBWINUI_ENABLE_TRACING && defined(_WIN32)

#include <cstdint>

namespace ui::detail
{

void trace_span_start(const char* name) noexcept;
void trace_span_stop(const char* name, std::int64_t start_ticks) noexcept;
// A single point in time (first frame, first idle).
void                       trace_mark(const char* name) noexcept;
[[nodiscard]] std::int64_t trace_ticks() noexcept;

class TraceSpan
{
public:
    explicit TraceSpan(const char* name) noexcept : name_(name), start_(trace_ticks())
    {
        trace_span_start(name_);
    }
    ~TraceSpan()
    {
        trace_span_stop(name_, start_);
    }
    TraceSpan(const TraceSpan&)            = delete;
    TraceSpan& operator=(const TraceSpan&) = delete;

private:
    const char*  name_;
    std::int64_t start_;
};

} // namespace ui::detail

#define LIBWINUI_TRACE_CONCAT_(a, b) a##b
#define LIBWINUI_TRACE_CONCAT(a, b)  LIBWINUI_TRACE_CONCAT_(a, b)
#define LIBWINUI_TRACE_SPAN(name)                                                    \
    const ::ui::detail::TraceSpan LIBWINUI_TRACE_CONCAT(winui_trace_span_, __LINE__) \
    {                                                                                \
        name                                                                         \
    }
#define LIBWINUI_TRACE_MARK(name) ::ui::detail::trace_mark(name)

#else

#define LIBWINUI_TRACE_SPAN(name) static_cast<void>(0)
#define LIBWINUI_TRACE_MARK(name) static_cast<void>(0)

#endif
