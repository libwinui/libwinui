// SPDX-License-Identifier: MIT
// A suspended coroutine owned until it is resumed, shared by the awaitables that resume through
// Window::post().
#pragma once

#ifndef LIBWINUI_MODULE
#include <coroutine>
#include <utility>
#endif

namespace ui::detail
{

// Owns a suspended coroutine until it is run: called, it resumes it; dropped unrun (post() after
// close, a dialog that went without an answer), it destroys the frame, so nothing leaks and
// nothing resumes.
class Resumption
{
public:
    inline explicit Resumption(std::coroutine_handle<> coroutine) noexcept : coroutine_(coroutine)
    {
    }
    inline Resumption(Resumption&& other) noexcept : coroutine_(std::exchange(other.coroutine_, {}))
    {
    }
    inline Resumption& operator=(Resumption&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            coroutine_ = std::exchange(other.coroutine_, {});
        }
        return *this;
    }
    Resumption(const Resumption&)            = delete;
    Resumption& operator=(const Resumption&) = delete;
    inline ~Resumption()
    {
        reset();
    }

    inline void operator()()
    {
        std::exchange(coroutine_, {}).resume();
    }

private:
    inline void reset() noexcept
    {
        if (coroutine_)
        {
            std::exchange(coroutine_, {}).destroy();
        }
    }

    std::coroutine_handle<> coroutine_;
};

} // namespace ui::detail
