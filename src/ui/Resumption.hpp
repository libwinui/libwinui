// SPDX-License-Identifier: MIT
// A suspended coroutine owned until it is resumed, shared by the awaitables that resume through
// Window::post().
#pragma once

#include <coroutine>
#include <utility>

namespace ui::detail
{

// Owns a suspended coroutine until it is run: called, it resumes it; dropped unrun (post() after
// close, a dialog that went without an answer), it destroys the frame, so nothing leaks and
// nothing resumes.
class Resumption
{
public:
    explicit Resumption(std::coroutine_handle<> coroutine) noexcept : coroutine_(coroutine)
    {
    }
    Resumption(Resumption&& other) noexcept : coroutine_(std::exchange(other.coroutine_, {}))
    {
    }
    Resumption& operator=(Resumption&& other) noexcept
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
    ~Resumption()
    {
        reset();
    }

    void operator()()
    {
        std::exchange(coroutine_, {}).resume();
    }

private:
    void reset() noexcept
    {
        if (coroutine_)
        {
            std::exchange(coroutine_, {}).destroy();
        }
    }

    std::coroutine_handle<> coroutine_;
};

} // namespace ui::detail
