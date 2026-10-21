// SPDX-License-Identifier: MIT
// The thread checks compiled out (LIBWINUI_THREAD_CHECKS=0, what a Release build gets): the core
// headers are used from several threads, and the program is linked without the library, so any
// call left into the check machinery would fail to link. A separate program, not a file of
// winui_tests: Property is laid out differently with the checks, and one program never mixes
// the two.
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/ThreadCheck.hpp"

#include <thread>

#include <cstdio>

static_assert(!ui::ThreadChecksEnabled, "this program is built with LIBWINUI_THREAD_CHECKS=0");

int main()
{
    ui::Property<int> count{0};
    int               seen = 0;
    ui::Subscription  sub  = count.observe([&](const int& n) { seen = n; });
    count.set(1);
    std::thread{[&] { count.set(2); }}.join(); // would be reported with the checks
    count.update([](int& n) { ++n; });
    count = 4;

    ui::ObservableVector<int> items;
    ui::Subscription          items_sub = items.observe([](const ui::CollectionChange&) {});
    items.push_back(1);
    std::thread{[&] { items.push_back(2); }}.join();

    ui::Signal       changed;
    ui::Subscription changed_sub = changed.observe([] {});
    changed.raise();
    std::thread{[&] { changed.raise(); }}.join();

    if (seen != 4 || items.size() != 2)
    {
        std::puts("thread_checks_off: unexpected state");
        return 1;
    }
    return 0;
}
