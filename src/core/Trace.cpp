// SPDX-License-Identifier: MIT
#include "Trace.hpp"

#if LIBWINUI_ENABLE_TRACING && defined(_WIN32)

#include <TraceLoggingProvider.h>
#include <windows.h>

namespace
{
// The GUID ETW derives from the name "libwinui", so `*libwinui` finds it.
TRACELOGGING_DEFINE_PROVIDER(provider, "libwinui", (0xabddcda8, 0x4aec, 0x5a60, 0x8d, 0x55, 0x8a, 0x57, 0xfb, 0xaa, 0x38, 0x12));

// Registered on first use and never unregistered: the OS drops the registration with the process,
// and a static destructor could run after a span on another thread.
bool registered() noexcept
{
    static const bool ok = SUCCEEDED(TraceLoggingRegister(provider));
    return ok;
}

std::int64_t frequency() noexcept
{
    static const std::int64_t f = []
    {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value.QuadPart;
    }();
    return f;
}
} // namespace

namespace ui::detail
{

std::int64_t trace_ticks() noexcept
{
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

void trace_span_start(const char* name) noexcept
{
    if (registered())
    {
        TraceLoggingWrite(provider,
                          "SpanStart",
                          TraceLoggingOpcode(WINEVENT_OPCODE_START),
                          TraceLoggingString(name, "Name"));
    }
}

void trace_span_stop(const char* name, std::int64_t start_ticks) noexcept
{
    if (registered())
    {
        const std::int64_t micros = (trace_ticks() - start_ticks) * 1'000'000 / frequency();
        TraceLoggingWrite(provider,
                          "SpanStop",
                          TraceLoggingOpcode(WINEVENT_OPCODE_STOP),
                          TraceLoggingString(name, "Name"),
                          TraceLoggingInt64(micros, "DurationUs"));
    }
}

void trace_mark(const char* name) noexcept
{
    if (registered())
    {
        TraceLoggingWrite(provider, "Mark", TraceLoggingString(name, "Name"));
    }
}

} // namespace ui::detail

#endif
