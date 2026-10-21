// SPDX-License-Identifier: MIT
#include "ui/LayoutSnapshot.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Scheduler.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/containers/Border.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/DropdownButton.hpp"
#include "winui/ui/input/RadioButton.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/media/Image.hpp"
#include "winui/ui/menus/AppBarButton.hpp"
#include "winui/ui/panels/Canvas.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/LayoutPanel.hpp"
#include "winui/ui/panels/RelativePanel.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/panels/TilePanel.hpp"
#include "winui/ui/panels/WrapPanel.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/spec/Dynamic.hpp"
#include "winui/ui/status/ProgressBar.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstddef>

#ifdef _WIN32
#include "winui/render/Text.hpp"
#include "winui/testing/OffscreenCapture.hpp"
#include "winui/ui/text/Inlines.hpp"
#endif

using namespace ui;
using namespace ui::testing;

namespace
{
std::vector<std::unique_ptr<TextControl>> make_text_blocks(std::size_t n)
{
    std::vector<std::unique_ptr<TextControl>> blocks;
    blocks.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        blocks.push_back(std::make_unique<TextControl>("Item " + std::to_string(i)));
    }
    return blocks;
}

// Records the thread it was measured on; parallel-safe or not as told.
class Probe final : public Element
{
public:
    Probe(std::string text, bool safe, bool throws = false) : text_(std::move(text)), safe_(safe), throws_(throws)
    {
    }
    [[nodiscard]] bool parallel_measure_safe() const override
    {
        return safe_;
    }
    [[nodiscard]] std::thread::id measured_on() const
    {
        return thread_;
    }

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override
    {
        thread_ = std::this_thread::get_id();
        if (throws_)
        {
            throw std::runtime_error("probe");
        }
        return context.measurer.measure_wrapped(text_, context.theme.text_style, available.width);
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }


private:
    std::string             text_;
    bool                    safe_;
    bool                    throws_;
    mutable std::thread::id thread_{};
};

const std::string Paragraph = "A paragraph long enough to wrap over several lines at most widths.";

// A settings-page-like tree: sections of label/field grids, wrapped paragraphs, a wrap panel of
// buttons and a canvas, plus parts that must stay on the UI thread (a button with content, an
// unsafe probe). About 40 elements per section.
std::unique_ptr<Element> make_form(std::size_t sections)
{
    auto root = std::make_unique<StackControl>(Orientation::Vertical);
    root->set_spacing(8.0f);
    for (std::size_t s = 0; s < sections; ++s)
    {
        root->add(std::make_unique<TextControl>("Section " + std::to_string(s)));

        auto grid = std::make_unique<GridControl>();
        grid->add_column(GridLength::auto_());
        grid->add_column(GridLength::fraction());
        for (std::size_t r = 0; r < 8; ++r)
        {
            grid->add_row(GridLength::auto_());
            grid->add(std::make_unique<TextControl>("Field " + std::to_string(s * 8 + r)), r, 0);
            switch (r % 8)
            {
                case 0:
                    grid->add(std::make_unique<TextBoxControl>("Value " + std::to_string(r)), r, 1);
                    break;
                case 1:
                    grid->add(std::make_unique<CheckBoxControl>("Enabled", true), r, 1);
                    break;
                case 2:
                    grid->add(std::make_unique<SliderControl>(), r, 1);
                    break;
                case 3:
                    grid->add(std::make_unique<ToggleSwitchControl>(true), r, 1);
                    break;
                case 4:
                    grid->add(std::make_unique<RadioButtonControl>("Option", true), r, 1);
                    break;
                case 5:
                    grid->add(std::make_unique<ProgressBarControl>(40.0), r, 1);
                    break;
                default:
                {
                    auto text = std::make_unique<TextControl>(Paragraph);
                    text->set_wrapping(TextWrapping::Wrap);
                    grid->add(std::move(text), r, 1);
                    break;
                }
            }
        }
        root->add(std::move(grid));

        auto border = std::make_unique<BorderControl>();
        auto inner  = std::make_unique<StackControl>(Orientation::Vertical);
        for (std::size_t i = 0; i < 4; ++i)
        {
            auto text = std::make_unique<TextControl>(Paragraph + " " + std::to_string(i));
            text->set_wrapping(TextWrapping::WrapWholeWords);
            inner->add(std::move(text));
        }
        border->set_child(std::move(inner));
        root->add(std::move(border));

        auto wrap = std::make_unique<WrapPanelControl>();
        for (std::size_t i = 0; i < 10; ++i)
        {
            wrap->add(std::make_unique<ButtonControl>("Action " + std::to_string(i)));
        }
        root->add(std::move(wrap));

        auto canvas = std::make_unique<CanvasControl>();
        for (std::size_t i = 0; i < 4; ++i)
        {
            canvas->add(std::make_unique<TextControl>("Pinned " + std::to_string(i)), 10.0f * static_cast<float>(i), 4.0f);
        }
        root->add(std::move(canvas));

        if (s % 4 == 1)
        {
            auto with_content = std::make_unique<ButtonControl>();
            with_content->set_content(std::make_unique<TextControl>("Content button"));
            root->add(std::move(with_content));
            root->add(std::make_unique<Probe>("Unsafe probe", false));
            root->add(ui::build(ui::Lazy{.content = [] { return ui::Spec{ui::Text{.text = "Lazy page"}}; }}));
        }
    }
    return root;
}

// One of each container and leaf that may be measured on a worker beyond the four splitting
// panels, each holding probes, plus `rows` rows of the leaves.
std::unique_ptr<Element> make_mixed(std::size_t rows)
{
    auto root = std::make_unique<StackControl>(Orientation::Vertical);

    auto* tiles = root->add(std::make_unique<TilePanelControl>());
    for (std::size_t i = 0; i < 24; ++i)
    {
        tiles->add(std::make_unique<Probe>("Tile " + std::to_string(i), true), 1, i % 3 == 0 ? 2 : 1);
    }

    auto*  relative = root->add(std::make_unique<RelativePanelControl>());
    Probe* anchor   = relative->add(std::make_unique<Probe>("Anchor", true), {.align_left_with_panel = true});
    Probe* right    = relative->add(std::make_unique<Probe>("Right of the anchor", true), {.right_of = anchor});
    relative->add(std::make_unique<Probe>("Below both", true), {.below = right, .align_left_with = anchor});

    auto* scroll = root->add(std::make_unique<ScrollControl>());
    scroll->set_height(120.0f);
    auto* scrolled = scroll->set_child(std::make_unique<StackControl>(Orientation::Vertical));
    for (std::size_t i = 0; i < 16; ++i)
    {
        scrolled->add(std::make_unique<Probe>(Paragraph, true));
    }

    auto* layered = root->add(std::make_unique<LayoutPanelControl>());
    for (std::size_t i = 0; i < 8; ++i)
    {
        layered->add(std::make_unique<Probe>("Layer " + std::to_string(i), true));
    }

    root->add(ui::build(ui::Match<int>{.value = 1, .content = [](int) { return ui::Spec{ui::Text{.text = "Page"}}; }}));
    root->add(ui::build(ui::Match<int>{.value = 2,
                                       .keep  = true,
                                       .content = [](int n) { return ui::Spec{ui::Text{.text = std::to_string(n)}}; }}));
    root->add(ui::build(ui::Lazy{.content = [] { return ui::Spec{ui::Text{.text = "Lazy page"}}; }}));

    for (std::size_t r = 0; r < rows; ++r)
    {
        auto* row = root->add(std::make_unique<StackControl>(Orientation::Horizontal));
        row->add(std::make_unique<ImageControl>(ImageSource{Bitmap{32, 24}}));
        row->add(std::make_unique<IconControl>(Bitmap{16, 16}));
        row->add(std::make_unique<IconControl>(IconSource::bitmap(Bitmap{20, 20})));
        row->add(std::make_unique<IconControl>(IconSource::path(PathGeometry::rectangle(Rect{0, 0, 12, 12}, 2.0f, 2.0f))));
        row->add(std::make_unique<DropdownButtonControl>("More"));
        row->add(std::make_unique<AppBarButtonControl>("Save", IconGlyph{0xE74E}));
        row->add(std::make_unique<Probe>("Row " + std::to_string(r), true));
    }
    return root;
}

// Every element's bounds, depth first.
void collect_bounds(const Element& element, std::vector<Rect>& out)
{
    out.push_back(element.bounds());
    element.for_each_child([&](Element& child) { collect_bounds(child, out); });
}

std::vector<Rect> lay_out(std::unique_ptr<Element> tree, ParallelLayout options, Size client)
{
    HeadlessUI ui = create_headless();
    ui.window().set_parallel_layout(options);
    Element* root = ui.window().set_content(std::move(tree));
    ui.layout(client);
    std::vector<Rect> bounds;
    collect_bounds(*root, bounds);
    return bounds;
}

template <typename Fn>
void each_probe(const Element& element, Fn&& fn)
{
    if (const auto* probe = dynamic_cast<const Probe*>(&element))
    {
        fn(*probe);
    }
    element.for_each_child([&](Element& child) { each_probe(child, fn); });
}
} // namespace

TEST_CASE("measure_parallel matches sequential measure() for every element", "[parallel-layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    auto                  blocks = make_text_blocks(257); // deliberately not a multiple of any worker count
    std::vector<Element*> elements;
    elements.reserve(blocks.size());
    for (auto& b : blocks)
    {
        elements.push_back(b.get());
    }

    std::vector<Size> expected;
    expected.reserve(elements.size());
    for (Element* e : elements)
    {
        expected.push_back(e->measure({Unbounded, Unbounded}, measurer, theme));
    }

    Scheduler         scheduler{4};
    LayoutSnapshot    snapshot{elements, {Unbounded, Unbounded}, &measurer, &theme};
    std::vector<Size> actual = measure_parallel(snapshot, scheduler);

    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        REQUIRE(actual[i] == expected[i]);
    }
}

TEST_CASE("measure_parallel works with the deterministic Inline scheduler", "[parallel-layout][deterministic]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    auto                  blocks = make_text_blocks(10);
    std::vector<Element*> elements;
    elements.reserve(blocks.size());
    for (auto& b : blocks)
    {
        elements.push_back(b.get());
    }

    Scheduler         scheduler{Scheduler::Inline{}};
    LayoutSnapshot    snapshot{elements, {Unbounded, Unbounded}, &measurer, &theme};
    std::vector<Size> actual = measure_parallel(snapshot, scheduler);

    REQUIRE(actual.size() == elements.size());
    for (std::size_t i = 0; i < elements.size(); ++i)
    {
        REQUIRE(actual[i] == elements[i]->measure({Unbounded, Unbounded}, measurer, theme));
    }
}

TEST_CASE("measure_parallel handles an empty batch", "[parallel-layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();
    Scheduler            scheduler;

    std::vector<Element*> elements;
    LayoutSnapshot        snapshot{elements, {Unbounded, Unbounded}, &measurer, &theme};
    std::vector<Size>     actual = measure_parallel(snapshot, scheduler);

    REQUIRE(actual.empty());
}

// Hidden ([.]): the speedup depends on the machine, so this informs rather than gates.
// Run it with "[benchmark]".
TEST_CASE("measure_parallel on 100,000 synthetic elements", "[.][benchmark]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    auto                  blocks = make_text_blocks(100'000);
    std::vector<Element*> elements;
    elements.reserve(blocks.size());
    for (auto& b : blocks)
    {
        elements.push_back(b.get());
    }
    LayoutSnapshot snapshot{elements, {Unbounded, Unbounded}, &measurer, &theme};

    BENCHMARK("sequential")
    {
        Scheduler inline_scheduler{Scheduler::Inline{}};
        return measure_parallel(snapshot, inline_scheduler);
    };

    BENCHMARK("scheduler (hardware_concurrency)")
    {
        Scheduler scheduler;
        return measure_parallel(snapshot, scheduler);
    };
}

TEST_CASE("Parallel layout is on by default, above 256 elements, and can be switched off", "[parallel-layout]")
{
    Window window;
    CHECK(window.parallel_layout().enabled);
    CHECK(window.parallel_layout().min_elements == 256);
    CHECK(window.parallel_layout().scheduler == nullptr);
    CHECK_FALSE(parallel_measure_active());

    HeadlessUI ui    = create_headless();
    const auto self  = std::this_thread::get_id();
    auto*      stack = ui.window().set_content(std::make_unique<StackControl>(Orientation::Vertical));
    for (int i = 0; i < 200; ++i)
    {
        stack->add(std::make_unique<Probe>("Probe", true));
    }
    ui.layout({400, 300});
    each_probe(*stack, [&](const Probe& p) { CHECK(p.measured_on() == self); });

    for (int i = 0; i < 200; ++i)
    {
        stack->add(std::make_unique<Probe>("Probe", true));
    }
    ui.layout({400, 300});
    std::set<std::thread::id> threads;
    each_probe(*stack, [&](const Probe& p) { threads.insert(p.measured_on()); });
    CHECK(threads.contains(self));
    CHECK(threads.size() > 1); // layout_scheduler() has a worker even on a single core
    CHECK_FALSE(parallel_measure_active());

    ui.window().set_parallel_layout({.enabled = false});
    ui.layout({401, 300});
    each_probe(*stack, [&](const Probe& p) { CHECK(p.measured_on() == self); });
}

TEST_CASE("A parallel Window layout gives exactly the sequential bounds", "[parallel-layout]")
{
    constexpr std::size_t Sections = 24;
    Scheduler             workers{4};
    Scheduler             inline_scheduler{Scheduler::Inline{}};

    for (const Size client : {Size{640, 480}, Size{333, 900}, Size{1200, 200}})
    {
        const std::vector<Rect> expected = lay_out(make_form(Sections), {.enabled = false}, client);
        REQUIRE(expected.size() > 900);

        CHECK(lay_out(make_form(Sections), {.enabled = true, .min_elements = 1, .scheduler = &workers}, client) == expected);
        CHECK(lay_out(make_form(Sections), {.enabled = true, .min_elements = 64, .scheduler = &workers}, client) ==
              expected);
        CHECK(lay_out(make_form(Sections), {.enabled = true, .min_elements = 1, .scheduler = &inline_scheduler}, client) ==
              expected);
    }
}

TEST_CASE("Repeated parallel passes over one tree stay identical to sequential ones", "[parallel-layout]")
{
    // The same tree, re-laid out at many widths on real workers: a data race in a measure path
    // shows up here (and under the sanitizers) as a mismatch or a crash.
    Scheduler  workers{4};
    HeadlessUI parallel_ui   = create_headless();
    HeadlessUI sequential_ui = create_headless();
    parallel_ui.window().set_parallel_layout({.enabled = true, .min_elements = 8, .scheduler = &workers});
    sequential_ui.window().set_parallel_layout({.enabled = false});
    Element* parallel_root   = parallel_ui.window().set_content(make_form(12));
    Element* sequential_root = sequential_ui.window().set_content(make_form(12));

    for (int pass = 0; pass < 40; ++pass)
    {
        const Size client{300.0f + 17.0f * static_cast<float>(pass), 600.0f};
        parallel_ui.layout(client);
        sequential_ui.layout(client);
        std::vector<Rect> a;
        std::vector<Rect> b;
        collect_bounds(*parallel_root, a);
        collect_bounds(*sequential_root, b);
        REQUIRE(a == b);
    }
}

TEST_CASE("Subtrees that are not parallel-safe are measured on the UI thread", "[parallel-layout]")
{
    Scheduler           workers{4};
    HeadlessUI          ui    = create_headless();
    const auto          self  = std::this_thread::get_id();
    StackControl*       stack = ui.window().set_content(std::make_unique<StackControl>(Orientation::Vertical));
    std::vector<Probe*> unsafe;
    std::vector<Probe*> safe;
    for (int i = 0; i < 64; ++i)
    {
        if (i % 8 == 3)
        {
            unsafe.push_back(stack->add(std::make_unique<Probe>("Unsafe", false)));
        }
        else
        {
            safe.push_back(stack->add(std::make_unique<Probe>("Safe", true)));
        }
    }
    // A safe container holding one unsafe element is unsafe as a whole, its safe children too.
    auto*  mixed        = stack->add(std::make_unique<StackControl>(Orientation::Vertical));
    Probe* mixed_safe   = mixed->add(std::make_unique<Probe>("Safe inside", true));
    Probe* mixed_unsafe = mixed->add(std::make_unique<Probe>("Unsafe inside", false));

    ui.window().set_parallel_layout({.enabled = true, .min_elements = 1, .scheduler = &workers});
    ui.layout({400, 300});

    for (const Probe* p : unsafe)
    {
        CHECK(p->measured_on() == self);
    }
    CHECK(mixed_safe->measured_on() == self);
    CHECK(mixed_unsafe->measured_on() == self);
    std::set<std::thread::id> threads;
    for (const Probe* p : safe)
    {
        threads.insert(p->measured_on());
    }
    CHECK(threads.size() > 1); // the calling thread took one share, the workers the others
    CHECK(threads.contains(self));
}

TEST_CASE("Parallel layout stays on the UI thread below the size threshold", "[parallel-layout]")
{
    Scheduler     workers{4};
    HeadlessUI    ui    = create_headless();
    const auto    self  = std::this_thread::get_id();
    StackControl* stack = ui.window().set_content(std::make_unique<StackControl>(Orientation::Vertical));
    for (int i = 0; i < 40; ++i)
    {
        stack->add(std::make_unique<Probe>("Probe", true));
    }

    // A batch of 40 probes: under a threshold of 64 it stays put, over 32 it splits.
    ui.window().set_parallel_layout({.enabled = true, .min_elements = 64, .scheduler = &workers});
    ui.layout({400, 300});
    each_probe(*stack, [&](const Probe& p) { CHECK(p.measured_on() == self); });

    ui.window().set_parallel_layout({.enabled = true, .min_elements = 32, .scheduler = &workers});
    ui.layout({401, 300});
    std::set<std::thread::id> threads;
    each_probe(*stack, [&](const Probe& p) { threads.insert(p.measured_on()); });
    CHECK(threads.size() > 1);
}

TEST_CASE("A child large enough to split its own children is not split across", "[parallel-layout]")
{
    // A header and one big list: splitting the outer pair would leave threads idle, so the outer
    // stack measures in order and the list spreads its children.
    Scheduler     workers{4};
    HeadlessUI    ui     = create_headless();
    const auto    self   = std::this_thread::get_id();
    StackControl* outer  = ui.window().set_content(std::make_unique<StackControl>(Orientation::Vertical));
    Probe*        header = outer->add(std::make_unique<Probe>("Header", true));
    StackControl* list   = outer->add(std::make_unique<StackControl>(Orientation::Vertical));
    for (int i = 0; i < 100; ++i)
    {
        list->add(std::make_unique<Probe>("Row", true));
    }
    ui.window().set_parallel_layout({.enabled = true, .min_elements = 50, .scheduler = &workers});
    ui.layout({400, 300});

    CHECK(header->measured_on() == self);
    std::set<std::thread::id> threads;
    each_probe(*list, [&](const Probe& p) { threads.insert(p.measured_on()); });
    CHECK(threads.size() > 1);
}

TEST_CASE("A container measured on a worker measures its children there too", "[parallel-layout]")
{
    Scheduler                  workers{4};
    HeadlessUI                 ui    = create_headless();
    StackControl*              outer = ui.window().set_content(std::make_unique<StackControl>(Orientation::Vertical));
    std::vector<StackControl*> groups;
    for (int g = 0; g < 16; ++g)
    {
        StackControl* group = outer->add(std::make_unique<StackControl>(Orientation::Horizontal));
        for (int i = 0; i < 8; ++i)
        {
            group->add(std::make_unique<Probe>("Cell", true));
        }
        groups.push_back(group);
    }
    ui.window().set_parallel_layout({.enabled = true, .min_elements = 16, .scheduler = &workers});
    ui.layout({400, 300});

    for (const StackControl* group : groups)
    {
        std::set<std::thread::id> threads;
        each_probe(*group, [&](const Probe& p) { threads.insert(p.measured_on()); });
        CHECK(threads.size() == 1);
    }
    CHECK_FALSE(parallel_measure_active());
}

TEST_CASE("An exception thrown while measuring on a worker reaches the layout caller", "[parallel-layout]")
{
    Scheduler     workers{4};
    HeadlessUI    ui    = create_headless();
    StackControl* stack = ui.window().set_content(std::make_unique<StackControl>(Orientation::Vertical));
    for (int i = 0; i < 63; ++i)
    {
        stack->add(std::make_unique<Probe>("Probe", true));
    }
    stack->add(std::make_unique<Probe>("Throws", true, true)); // the last share, which a worker takes
    ui.window().set_parallel_layout({.enabled = true, .min_elements = 1, .scheduler = &workers});
    CHECK_THROWS_AS(ui.layout({400, 300}), std::runtime_error);
    CHECK_FALSE(parallel_measure_active());
}

TEST_CASE("Tile, relative, scroll, layout and dynamic hosts, images and icons go to workers", "[parallel-layout]")
{
    Scheduler workers{4};
    for (const Size client : {Size{640, 480}, Size{240, 900}})
    {
        const std::vector<Rect> expected = lay_out(make_mixed(40), {.enabled = false}, client);
        CHECK(lay_out(make_mixed(40), {.enabled = true, .min_elements = 1, .scheduler = &workers}, client) == expected);
    }

    HeadlessUI ui   = create_headless();
    Element*   root = ui.window().set_content(make_mixed(40));
    ui.window().set_parallel_layout({.enabled = true, .min_elements = 1, .scheduler = &workers});
    ui.layout({640, 480});

    // Every probe is safe, so the whole tree is: no element was left to the UI thread.
    std::set<std::thread::id> threads;
    each_probe(*root, [&](const Probe& p) { threads.insert(p.measured_on()); });
    CHECK(threads.size() > 1);
    std::size_t unsafe = 0;
    const auto  count  = [&](this const auto& self, const Element& e) -> void
    {
        unsafe += e.parallel_measure_safe() ? 0 : 1;
        e.for_each_child([&](Element& child) { self(child); });
    };
    count(*root);
    CHECK(unsafe == 0);
}

TEST_CASE("A layout panel is parallel-safe only when its layout says so", "[parallel-layout]")
{
    LayoutPanelControl panel;
    CHECK(panel.parallel_measure_safe());
    panel.set_layout(std::make_unique<StackLayoutEngine>());
    CHECK_FALSE(panel.parallel_measure_safe());
}

TEST_CASE("measure_batch outside a parallel layout measures in order on the calling thread", "[parallel-layout]")
{
    HeadlessTextMeasurer        measurer;
    const Theme                 theme = Theme::light();
    Probe                       a{"One", true};
    Probe                       b{"Two words", false};
    std::vector<MeasureRequest> batch{{&a, {Unbounded, Unbounded}}, {&b, {40.0f, Unbounded}}};
    measure_batch(batch, {measurer, theme});
    CHECK(batch[0].desired == a.measure({Unbounded, Unbounded}, measurer, theme));
    CHECK(batch[1].desired == b.measure({40.0f, Unbounded}, measurer, theme));
    CHECK(a.measured_on() == std::this_thread::get_id());
}

#ifdef _WIN32
TEST_CASE("Parallel layout with the DirectWrite measurer gives exactly the sequential bounds",
          "[parallel-layout][pixel]")
{
    // DirectWrite text measurement on several threads at once, through the real measurer.
    OffscreenCapture d2d;
    REQUIRE(d2d.ok());
    Scheduler workers{4};
    for (const Size client : {Size{640, 480}, Size{333, 900}})
    {
        Window sequential;
        Window parallel;
        sequential.set_parallel_layout({.enabled = false});
        parallel.set_parallel_layout({.enabled = true, .min_elements = 16, .scheduler = &workers});
        Element* a = sequential.set_content(make_form(8));
        Element* b = parallel.set_content(make_form(8));
        sequential.layout(client, d2d.measurer());
        parallel.layout(client, d2d.measurer());
        std::vector<Rect> expected;
        std::vector<Rect> actual;
        collect_bounds(*a, expected);
        collect_bounds(*b, actual);
        CHECK(actual == expected);
    }
}
#endif

// Hidden ([.]): timings depend on the machine. Run with "[benchmark]", best in Release.
TEST_CASE("Window layout of a form, sequential vs parallel", "[.][benchmark]")
{
    // 60 sections are about 2'500 elements, 12 about 500, 6 about 250 (just under the default
    // threshold, so there the parallel run pays only for the checks).
    const std::size_t Sections = GENERATE(60u, 12u, 6u);
    INFO("sections: " << Sections);
    const auto bench = [Sections](const char* name, const TextMeasurer& measurer, ParallelLayout options)
    {
        Window window;
        window.set_parallel_layout(options);
        window.set_content(make_form(Sections));
        bool wide = false;
        BENCHMARK(name)
        {
            wide = !wide; // a new width every run, so each one is a full pass
            window.layout(wide ? Size{800, 600} : Size{799, 600}, measurer);
            return window.layout_pass_count();
        };
    };

    HeadlessTextMeasurer headless;
    bench("headless measurer, sequential", headless, {.enabled = false});
    bench("headless measurer, parallel", headless, {.enabled = true});
#ifdef _WIN32
    OffscreenCapture d2d;
    REQUIRE(d2d.ok());
    bench("DirectWrite measurer, sequential", d2d.measurer(), {.enabled = false});
    bench("DirectWrite measurer, parallel", d2d.measurer(), {.enabled = true});
#endif
}
