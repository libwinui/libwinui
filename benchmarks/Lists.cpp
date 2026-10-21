// SPDX-License-Identifier: MIT
// Behind the claim that a virtualized list costs what its viewport costs, whatever the item count:
// every list shape, first layout and a scroll step, at 100, 100'000 and 1'000'000 items. Then a
// 100'000-item ListView scrolled in a window and drawn, through both backends.
#include "Bench.hpp"
#include "Fixture.hpp"

#include <winui/testing.hpp>
#include <winui/winui.hpp>

#include <functional>
#include <memory>
#include <string>

namespace bench
{

namespace
{

constexpr ui::Size Viewport{400.0f, 600.0f};
constexpr int      ScrollSteps = 400;
constexpr float    RowHeight   = 40.0f;

std::unique_ptr<ui::Element> row(std::size_t index, std::size_t& built)
{
    ++built;
    auto text = std::make_unique<ui::TextControl>("Item " + std::to_string(index));
    text->set_height(RowHeight);
    return text;
}

// One list, its scroll step and how many rows it has alive, behind one interface.
struct List
{
    ui::testing::HeadlessTextMeasurer measurer;
    ui::Theme                         theme = ui::Theme::light();
    std::unique_ptr<ui::Element>      root;
    std::function<void(int)>          scroll;
    std::function<std::size_t()>      alive;
    std::size_t                       built = 0;

    void lay_out()
    {
        (void)root->measure(Viewport, measurer, theme);
        root->arrange(ui::Rect{0.0f, 0.0f, Viewport.width, Viewport.height});
    }
};

std::size_t jump(int step, std::size_t count)
{
    // Jumps of a few screens, so every step realizes a fresh set of rows.
    return (static_cast<std::size_t>(step) * 53) % count;
}

std::unique_ptr<List> list_view_base(std::unique_ptr<ui::ListViewBase> view, std::size_t count)
{
    auto  list = std::make_unique<List>();
    auto* raw  = view.get();
    raw->set_item_template([l = list.get()](std::size_t i) { return row(i, l->built); });
    raw->set_item_count(count);
    list->scroll = [raw, count](int step)
    { raw->scroll_into_view(jump(step, count), ui::ScrollIntoViewAlignment::Leading); };
    list->alive = [raw] { return raw->repeater()->realized_count(); };
    list->root  = std::move(view);
    return list;
}

struct Shape
{
    const char*                                       name;
    std::function<std::unique_ptr<List>(std::size_t)> make;
};

const Shape Shapes[] = {
    {"ListView", [](std::size_t n) { return list_view_base(std::make_unique<ui::ListViewControl>(), n); }},
    {"ListView, fixed rows",
     [](std::size_t n)
     {
         auto view = std::make_unique<ui::ListViewControl>();
         view->set_item_height(RowHeight);
         return list_view_base(std::move(view), n);
     }},
    {"TileView", [](std::size_t n) { return list_view_base(std::make_unique<ui::TileViewControl>(), n); }},
    {"TileView, wrap layout",
     [](std::size_t n)
     {
         auto view = std::make_unique<ui::TileViewControl>();
         view->set_layout(std::make_unique<ui::WrapLayoutEngine>());
         return list_view_base(std::move(view), n);
     }},
    {"ForEach, virtualized",
     [](std::size_t n)
     {
         auto  list     = std::make_unique<List>();
         auto  view     = std::make_unique<ui::ScrollControl>();
         auto* raw      = view.get();
         auto* repeater = raw->set_child(std::make_unique<ui::ForEachControl>());
         repeater->set_item_template([l = list.get()](std::size_t i) { return row(i, l->built); });
         repeater->set_item_count(n);
         list->scroll = [raw, n](int step)
         {
             raw->scroll_to(raw->horizontal_offset(),
                            static_cast<float>(jump(step, n)) * RowHeight,
                            {.animation_mode = ui::ScrollAnimationMode::Disabled});
         };
         list->alive = [repeater] { return repeater->realized_count(); };
         list->root  = std::move(view);
         return list;
     }},
    {"VirtualList",
     [](std::size_t n)
     {
         auto  list = std::make_unique<List>();
         auto  view = std::make_unique<ui::VirtualListControl>();
         auto* raw  = view.get();
         raw->set_item_height(RowHeight);
         raw->set_item_factory([l = list.get()](std::size_t i) { return row(i, l->built); });
         raw->set_item_count(n);
         list->scroll = [raw, n](int step) { raw->set_scroll_offset(static_cast<float>(jump(step, n)) * RowHeight); };
         list->alive  = [raw] { return raw->visible_count(); };
         list->root   = std::move(view);
         return list;
     }},
};

struct ScrolledWindow
{
    static constexpr std::size_t Count = 100'000;

    explicit ScrolledWindow(Backend backend) : fixture(backend, Viewport)
    {
        list = fixture.window().set_content(std::make_unique<ui::ListViewControl>());
        list->set_item_template(
            [](std::size_t i) { return std::make_unique<ui::TextControl>("Item " + std::to_string(i)); });
        list->set_item_count(Count);
        fixture.frame();
    }
    Fixture              fixture;
    ui::ListViewControl* list = nullptr;
};

} // namespace

void lists(Runner& runner)
{
    for (const Shape& shape : Shapes)
    {
        for (const std::size_t count : {std::size_t{100}, std::size_t{100'000}, std::size_t{1'000'000}})
        {
            const std::string prefix = std::string{"lists/"} + shape.name + ", " + std::to_string(count) + " items";
            if (runner.wants(prefix + ": first layout"))
            {
                runner.run(
                    prefix + ": first layout",
                    "headless",
                    [&shape, count] { return shape.make(count); },
                    [](List& l, Result& r)
                    {
                        l.lay_out();
                        r.extra = {{"rows_alive", static_cast<double>(l.alive())}};
                    });
            }
            if (runner.wants(prefix + ": 400 scroll steps"))
            {
                runner.run(
                    prefix + ": 400 scroll steps",
                    "headless",
                    [&shape, count]
                    {
                        auto l = shape.make(count);
                        l->lay_out();
                        l->built = 0;
                        return l;
                    },
                    [](List& l, Result& r)
                    {
                        for (int step = 1; step <= ScrollSteps; ++step)
                        {
                            l.scroll(step);
                            l.lay_out();
                        }
                        r.extra = {{"rows_alive", static_cast<double>(l.alive())},
                                   {"rows_built", static_cast<double>(l.built)}};
                    });
            }
        }
    }

    for (const Backend backend : {Backend::Headless, Backend::D2D})
    {
        if (runner.wants("lists/window"))
        {
            runner.run(
                "lists/window: scroll a 100'000-item ListView 100 times",
                name(backend),
                [backend] { return std::make_unique<ScrolledWindow>(backend); },
                [](ScrolledWindow& w, Result&)
                {
                    for (int step = 1; step <= 100; ++step)
                    {
                        w.list->scroll_into_view(jump(step, ScrolledWindow::Count), ui::ScrollIntoViewAlignment::Leading);
                        w.fixture.frame();
                    }
                });
        }
    }
}

} // namespace bench
