// SPDX-License-Identifier: MIT
// The gallery's pages as the complex windows of the bench: each page built and laid out once, then
// the heaviest few resized through twenty widths, as dragging a window edge does.
#include "gallery.hpp"

#include "Bench.hpp"
#include "Fixture.hpp"

#include <winui/testing.hpp>
#include <winui/winui.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bench
{

namespace
{

std::size_t element_count(const ui::Element& e)
{
    std::size_t n = 1;
    e.for_each_child([&n](ui::Element& child) { n += element_count(child); });
    return n;
}

// A page in a window, the way the gallery shows one: inside a scroll view.
struct PageWindow
{
    PageWindow(Backend backend, const gallery::Page& page) :
        fixture(backend, {1180.0f, 820.0f}),
        context{status, fixture.window(), theme_request, [](std::string_view) {}}
    {
        fixture.window().set_content(ui::Scroll{.content = page.make(context)});
        fixture.frame();
    }
    ui::Property<std::string> status{""};
    ui::ThemeRequest          theme_request{};
    Fixture                   fixture;
    gallery::Context          context;
};

std::vector<const gallery::Page*> all_pages()
{
    std::vector<const gallery::Page*> pages;
    for (const gallery::Group& group : gallery::groups())
    {
        for (const gallery::Page& page : group.pages)
        {
            pages.push_back(&page);
        }
    }
    return pages;
}

} // namespace

void gallery(Runner& runner)
{
    if (!runner.wants("gallery/"))
    {
        return;
    }
    // Which pages are heaviest, by the elements a laid-out page holds.
    std::vector<std::pair<std::size_t, const gallery::Page*>> sizes;
    for (const gallery::Page* page : all_pages())
    {
        PageWindow w{Backend::Headless, *page};
        sizes.emplace_back(element_count(*w.fixture.ui.window().content()), page);
    }
    std::sort(sizes.begin(), sizes.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    if (runner.wants("gallery/build"))
    {
        runner.run(
            "gallery/build and lay out every page",
            "headless",
            [] { return std::make_unique<int>(0); },
            [](int&, Result&)
            {
                for (const gallery::Page* page : all_pages())
                {
                    const PageWindow w{Backend::Headless, *page};
                }
            });
    }

    for (std::size_t i = 0; i < std::min<std::size_t>(3, sizes.size()); ++i)
    {
        const gallery::Page& page     = *sizes[i].second;
        const std::string    scenario = std::string{"gallery/resize "} + page.name + " through 20 widths";
        if (!runner.wants(scenario))
        {
            continue;
        }
        for (const Backend backend : {Backend::Headless, Backend::D2D})
        {
            Result& result = runner.run(
                scenario,
                name(backend),
                [backend, &page] { return std::make_unique<PageWindow>(backend, page); },
                [](PageWindow& w, Result&)
                {
                    for (int step = 0; step < 20; ++step)
                    {
                        w.fixture.client = {600.0f + static_cast<float>(step) * 40.0f, 820.0f};
                        w.fixture.frame();
                    }
                });
            result.extra = {{"elements", static_cast<double>(sizes[i].first)}};
        }
    }
}

} // namespace bench
