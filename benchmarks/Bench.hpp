// SPDX-License-Identifier: MIT
// winui_bench's harness: a scenario runs a piece of work a number of times and records how long
// each run took, what the library counted (winui/testing/PerfCounters.hpp) and what it allocated.
#pragma once

#include "../tests/support/AllocationCounter.hpp"
#include "winui/testing/PerfCounters.hpp"

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace bench
{

struct Result
{
    std::string                                 scenario{};
    std::string                                 backend{}; // "headless" (deterministic measurer, TestRenderer) or "d2d"
    std::vector<double>                         samples_ms{};
    ui::testing::PerfCounters                   work{}; // summed over every run
    std::uint64_t                               allocations = 0;
    std::uint64_t                               bytes       = 0;
    std::vector<std::pair<std::string, double>> extra{}; // scenario-specific numbers (items alive, ...)
};

struct Options
{
    std::string filter;      // substring a scenario name must contain; empty runs all
    int         repeats = 5; // timed runs per scenario, after one warm-up
};

class Runner
{
public:
    explicit Runner(Options options) : options_(std::move(options))
    {
    }

    [[nodiscard]] bool wants(std::string_view scenario) const
    {
        return options_.filter.empty() || scenario.find(options_.filter) != std::string_view::npos;
    }

    // Runs `setup` and then `work` once to warm up, then `repeats` times timed. `setup` is not
    // timed or counted; it returns the state `work` runs on.
    template <typename Setup, typename Work>
    Result& run(std::string scenario, std::string backend, Setup&& setup, Work&& work)
    {
        Result result{.scenario = std::move(scenario), .backend = std::move(backend)};
        {
            auto state = setup();
            work(*state, result);
        }
        result.extra.clear();
        for (int i = 0; i < options_.repeats; ++i)
        {
            auto                                state = setup();
            const ui::testing::PerfCountScope   counts;
            const test_support::AllocationScope allocations;
            const auto                          start = std::chrono::steady_clock::now();
            work(*state, result);
            const auto end = std::chrono::steady_clock::now();
            result.samples_ms.push_back(std::chrono::duration<double, std::milli>(end - start).count());
            accumulate(result.work, counts.elapsed());
            result.allocations += allocations.allocations();
            result.bytes += allocations.bytes();
        }
        results_.push_back(std::move(result));
        return results_.back();
    }

    // A result measured by the scenario itself (a one-off number, such as bytes per control).
    void record(Result result)
    {
        results_.push_back(std::move(result));
    }

    [[nodiscard]] int repeats() const
    {
        return options_.repeats;
    }

    [[nodiscard]] const std::vector<Result>& results() const
    {
        return results_;
    }

private:
    static void accumulate(ui::testing::PerfCounters& sum, const ui::testing::PerfCounters& add);

    Options             options_;
    std::vector<Result> results_;
};

// The scenario groups, one per source file.
void controls(Runner& runner);
void lists(Runner& runner);
void gallery(Runner& runner);
void memory(Runner& runner);

} // namespace bench
