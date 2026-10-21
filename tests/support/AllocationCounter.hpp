// SPDX-License-Identifier: MIT
// The test binary replaces the global operator new with one that counts (AllocationCounter.cpp),
// so a [perf-count] test can say how many heap allocations a piece of work made.
#pragma once

#include <cstdint>

namespace test_support
{

// Allocations made through operator new on any thread since the process started.
[[nodiscard]] std::uint64_t allocation_count() noexcept;
// Bytes those allocations asked for.
[[nodiscard]] std::uint64_t allocated_bytes() noexcept;

// The allocations made between construction and allocations() / bytes().
class AllocationScope
{
public:
    AllocationScope() noexcept : count_(allocation_count()), bytes_(allocated_bytes())
    {
    }

    [[nodiscard]] std::uint64_t allocations() const noexcept
    {
        return allocation_count() - count_;
    }

    [[nodiscard]] std::uint64_t bytes() const noexcept
    {
        return allocated_bytes() - bytes_;
    }

private:
    std::uint64_t count_;
    std::uint64_t bytes_;
};

} // namespace test_support
