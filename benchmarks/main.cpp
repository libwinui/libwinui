// SPDX-License-Identifier: MIT
// winui_bench: the timings of Phase 8's scenarios, in a JSON file and a table on stdout. Not run
// by ctest; numbers only mean something from an optimized build (RelWithDebInfo or Release).
//
//     winui_bench [--filter <text>] [--repeats <n>] [--out <file.json>]
#include "Bench.hpp"

#include <algorithm>
#include <fstream>
#include <numeric>
#include <string>
#include <string_view>

#include <cstdio>
#include <cstdlib>

#ifndef LIBWINUI_BENCH_CONFIG
#define LIBWINUI_BENCH_CONFIG "unknown"
#endif

namespace bench
{

void Runner::accumulate(ui::testing::PerfCounters& sum, const ui::testing::PerfCounters& add)
{
    sum.layout_passes += add.layout_passes;
    sum.focus_order_builds += add.focus_order_builds;
    sum.visual_tree_builds += add.visual_tree_builds;
    sum.measures += add.measures;
    sum.arranges += add.arranges;
    sum.renders += add.renders;
    sum.text_measurements += add.text_measurements;
    sum.text_formats_created += add.text_formats_created;
    sum.text_layouts_created += add.text_layouts_created;
    sum.brushes_created += add.brushes_created;
    sum.geometries_created += add.geometries_created;
    sum.stroke_styles_created += add.stroke_styles_created;
    sum.bitmaps_created += add.bitmaps_created;
    sum.frames += add.frames;
    sum.ticks += add.ticks;
    sum.element_ticks += add.element_ticks;
    sum.timer_wakeups += add.timer_wakeups;
}

} // namespace bench

namespace
{

double median(std::vector<double> v)
{
    if (v.empty())
    {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) * 0.5;
}

std::string escape(std::string_view s)
{
    std::string out;
    for (const char c : s)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
        }
        out += c;
    }
    return out;
}

std::string json(const std::vector<bench::Result>& results)
{
    std::string out = "{\n  \"config\": \"" LIBWINUI_BENCH_CONFIG "\",\n  \"results\": [\n";
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const bench::Result& r    = results[i];
        const double         runs = static_cast<double>(std::max<std::size_t>(1, r.samples_ms.size()));
        const auto           per  = [&](std::uint64_t v) { return std::to_string(static_cast<double>(v) / runs); };
        out += "    {\"scenario\": \"" + escape(r.scenario) + "\", \"backend\": \"" + escape(r.backend) + "\"";
        if (!r.samples_ms.empty())
        {
            out += ", \"runs\": " + std::to_string(r.samples_ms.size());
            out += ", \"median_ms\": " + std::to_string(median(r.samples_ms));
            out += ", \"min_ms\": " + std::to_string(*std::min_element(r.samples_ms.begin(), r.samples_ms.end()));
            out += ", \"per_run\": {";
            out += "\"layout_passes\": " + per(r.work.layout_passes);
            out += ", \"measures\": " + per(r.work.measures);
            out += ", \"arranges\": " + per(r.work.arranges);
            out += ", \"renders\": " + per(r.work.renders);
            out += ", \"text_measurements\": " + per(r.work.text_measurements);
            out += ", \"text_formats_created\": " + per(r.work.text_formats_created);
            out += ", \"text_layouts_created\": " + per(r.work.text_layouts_created);
            out += ", \"brushes_created\": " + per(r.work.brushes_created);
            out += ", \"geometries_created\": " + per(r.work.geometries_created);
            out += ", \"frames\": " + per(r.work.frames);
            out += ", \"allocations\": " + per(r.allocations);
            out += ", \"allocated_bytes\": " + per(r.bytes);
            out += "}";
        }
        for (const auto& [name, value] : r.extra)
        {
            out += ", \"" + escape(name) + "\": " + std::to_string(value);
        }
        out += i + 1 < results.size() ? "},\n" : "}\n";
    }
    out += "  ]\n}\n";
    return out;
}

void table(const std::vector<bench::Result>& results)
{
    std::printf("%-44s %-8s %11s %11s %10s %10s %12s\n",
                "scenario",
                "backend",
                "median ms",
                "min ms",
                "measures",
                "layouts",
                "allocations");
    for (const bench::Result& r : results)
    {
        if (r.samples_ms.empty())
        {
            std::printf("%-44s %-8s", r.scenario.c_str(), r.backend.c_str());
            for (const auto& [name, value] : r.extra)
            {
                std::printf(" %s=%.1f", name.c_str(), value);
            }
            std::printf("\n");
            continue;
        }
        const double runs = static_cast<double>(r.samples_ms.size());
        std::printf("%-44s %-8s %11.3f %11.3f %10.0f %10.0f %12.0f\n",
                    r.scenario.c_str(),
                    r.backend.c_str(),
                    median(r.samples_ms),
                    *std::min_element(r.samples_ms.begin(), r.samples_ms.end()),
                    static_cast<double>(r.work.measures) / runs,
                    static_cast<double>(r.work.text_layouts_created) / runs,
                    static_cast<double>(r.allocations) / runs);
    }
}

} // namespace

int main(int argc, char** argv)
{
    bench::Options options;
    std::string    out_path;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--filter" && i + 1 < argc)
        {
            options.filter = argv[++i];
        }
        else if (arg == "--repeats" && i + 1 < argc)
        {
            options.repeats = std::max(1, std::atoi(argv[++i]));
        }
        else if (arg == "--out" && i + 1 < argc)
        {
            out_path = argv[++i];
        }
        else
        {
            std::fprintf(stderr, "usage: winui_bench [--filter <text>] [--repeats <n>] [--out <file.json>]\n");
            return 2;
        }
    }

    bench::Runner runner{options};
    bench::controls(runner);
    bench::lists(runner);
    bench::gallery(runner);
    bench::memory(runner);

    table(runner.results());
    const std::string text = json(runner.results());
    if (!out_path.empty())
    {
        std::ofstream{out_path, std::ios::binary} << text;
        std::printf("\nwrote %s\n", out_path.c_str());
    }
    return 0;
}
