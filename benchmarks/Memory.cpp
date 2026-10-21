// SPDX-License-Identifier: MIT
// What a control costs in memory (its object and what it allocates), and what a frame in which
// nothing changed still does.
#include "Bench.hpp"
#include "Fixture.hpp"

#include <winui/testing.hpp>
#include <winui/winui.hpp>

#include <memory>
#include <string>
#include <vector>

namespace bench
{

namespace
{

constexpr int Instances = 1000;

template <typename Control, typename Make>
void bytes_per_control(Runner& runner, const char* control, Make make)
{
    std::vector<std::unique_ptr<Control>> controls;
    controls.reserve(Instances);
    const test_support::AllocationScope scope;
    for (int i = 0; i < Instances; ++i)
    {
        controls.push_back(make(i));
    }
    runner.record({.scenario = std::string{"memory/"} + control,
                   .backend  = "-",
                   .extra    = {{"sizeof", static_cast<double>(sizeof(Control))},
                                {"bytes_per_control", static_cast<double>(scope.bytes()) / Instances},
                                {"allocations_per_control", static_cast<double>(scope.allocations()) / Instances}}});
}

struct Still
{
    explicit Still(Backend backend) : fixture(backend)
    {
        auto stack = std::make_unique<ui::StackControl>(ui::Orientation::Vertical);
        for (int i = 0; i < Instances; ++i)
        {
            stack->add(std::make_unique<ui::ButtonControl>("Button " + std::to_string(i)));
        }
        fixture.window().set_content(std::move(stack));
        fixture.frame();
        fixture.frame();
    }
    Fixture fixture;
};

} // namespace

void memory(Runner& runner)
{
    if (runner.wants("memory/"))
    {
        bytes_per_control<ui::TextControl>(runner,
                                           "TextControl",
                                           [](int i)
                                           { return std::make_unique<ui::TextControl>("Label " + std::to_string(i)); });
        bytes_per_control<ui::ButtonControl>(runner,
                                             "ButtonControl",
                                             [](int i)
                                             {
                                                 return std::make_unique<ui::ButtonControl>("Button " + std::to_string(i));
                                             });
        bytes_per_control<ui::CheckBoxControl>(runner,
                                               "CheckBoxControl",
                                               [](int i)
                                               {
                                                   return std::make_unique<ui::CheckBoxControl>(
                                                       "Option " + std::to_string(i));
                                               });
        bytes_per_control<ui::TextBoxControl>(runner,
                                              "TextBoxControl",
                                              [](int i)
                                              {
                                                  return std::make_unique<ui::TextBoxControl>("Text " + std::to_string(i));
                                              });
        bytes_per_control<ui::SliderControl>(runner,
                                             "SliderControl",
                                             [](int) { return std::make_unique<ui::SliderControl>(); });
        bytes_per_control<ui::StackControl>(runner,
                                            "StackControl",
                                            [](int)
                                            { return std::make_unique<ui::StackControl>(ui::Orientation::Vertical); });
    }
    for (const Backend backend : {Backend::Headless, Backend::D2D})
    {
        if (runner.wants("frame/unchanged"))
        {
            runner.run(
                "frame/unchanged, 1000 buttons, 10 frames",
                name(backend),
                [backend] { return std::make_unique<Still>(backend); },
                [](Still& s, Result&)
                {
                    for (int i = 0; i < 10; ++i)
                    {
                        s.fixture.frame();
                    }
                });
        }
    }
}

} // namespace bench
