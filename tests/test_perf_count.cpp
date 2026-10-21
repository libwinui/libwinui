// SPDX-License-Identifier: MIT
// How much work the framework does for a change, in counts rather than times: layout passes,
// measures, allocations. Counts do not flake, so these run with every test run, and a change that
// makes the framework do more work for the same change fails here like any other regression.
#include "support/AllocationCounter.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/OffscreenCapture.hpp"
#include "winui/testing/PerfCounters.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/media/Image.hpp"
#include "winui/ui/media/Shape.hpp"
#include "winui/ui/navigation/NavigationShell.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;

namespace
{
constexpr Size Client{800.0f, 600.0f};

// A window holding `count` buttons in a vertical stack, laid out once.
struct ButtonWall
{
    explicit ButtonWall(int count)
    {
        auto stack = std::make_unique<StackControl>(Orientation::Vertical);
        for (int i = 0; i < count; ++i)
        {
            buttons.push_back(stack->add(std::make_unique<ButtonControl>("Button " + std::to_string(i))));
        }
        ui.window().set_content(std::move(stack));
        ui.layout(Client);
    }

    HeadlessUI                  ui = create_headless();
    std::vector<ButtonControl*> buttons;
};

bool covers(const Rect& outer, const Rect& inner)
{
    return outer.left() <= inner.left() && outer.top() <= inner.top() && outer.right() >= inner.right() &&
           outer.bottom() >= inner.bottom();
}

Point center(const Element& e)
{
    const Rect& b = e.bounds();
    return {b.x + b.width * 0.5f, b.y + b.height * 0.5f};
}
} // namespace

TEST_CASE("A layout with nothing changed measures and arranges nothing", "[perf-count]")
{
    ButtonWall           wall{1000};
    const PerfCountScope scope;
    wall.ui.layout(Client);
    const PerfCounters work = scope.elapsed();
    REQUIRE(work.layout_passes == 0);
    REQUIRE(work.measures == 0);
    REQUIRE(work.arranges == 0);
    REQUIRE(work.text_measurements == 0);
}

TEST_CASE("Hovering a button in a 1000-element window measures nothing", "[perf-count]")
{
    ButtonWall           wall{1000};
    const PerfCountScope scope;
    seams(wall.ui.window()).handle_pointer({center(*wall.buttons[10]), PointerButton::None, PointerAction::Move});
    wall.ui.layout(Client);
    const PerfCounters work = scope.elapsed();
    REQUIRE(work.layout_passes == 0);
    REQUIRE(work.measures == 0);
    REQUIRE(work.text_measurements == 0);
}

TEST_CASE("Changing a label's text measures only the ancestors whose size changed", "[perf-count]")
{
    ButtonWall wall{1000};
    auto*      stack = static_cast<StackControl*>(wall.ui.window().content());
    auto       cell  = std::make_unique<StackControl>(Orientation::Vertical);
    cell->set_width(200.0f);
    cell->set_height(40.0f);
    TextControl* label = cell->add(std::make_unique<TextControl>("Short"));
    stack->add(std::move(cell));
    wall.ui.layout(Client);

    const PerfCountScope scope;
    label->set_text("A good deal longer than before");
    wall.ui.layout(Client);
    const PerfCounters work = scope.elapsed();
    // The label, then its fixed-size cell, which kept its size; nothing above it.
    REQUIRE(work.layout_passes == 1);
    REQUIRE(work.measures == 2);
    REQUIRE(work.text_measurements == 1);
    REQUIRE(work.arranges == 2);
    REQUIRE(label->bounds().width > 0.0f);
}

TEST_CASE("A label that grows lays out its ancestors again, and the result is a full layout's", "[perf-count]")
{
    ButtonWall   wall{10};
    auto*        stack = static_cast<StackControl*>(wall.ui.window().content());
    TextControl* label = stack->add(std::make_unique<TextControl>("Short"));
    wall.ui.layout(Client);
    label->set_text("A good deal longer than before");
    wall.ui.layout(Client);
    const Rect incremental = label->bounds();
    const Rect last_button = wall.buttons.back()->bounds();

    // The same tree laid out from scratch puts everything in the same place.
    ButtonWall fresh{10};
    auto*      fresh_stack = static_cast<StackControl*>(fresh.ui.window().content());
    auto*      fresh_label = fresh_stack->add(std::make_unique<TextControl>("A good deal longer than before"));
    fresh.ui.layout(Client);
    REQUIRE(incremental == fresh_label->bounds());
    REQUIRE(last_button == fresh.buttons.back()->bounds());
}

TEST_CASE("A change inside a part its container does not list is laid out", "[perf-count]")
{
    // NavigationShell's page is not among the children for_each_child() lists.
    struct Page
    {
        explicit Page(float height)
        {
            auto nav     = std::make_unique<NavigationShellControl>();
            auto content = std::make_unique<StackControl>(Orientation::Vertical);
            label        = content->add(std::make_unique<TextControl>("Label"));
            label->set_height(height);
            below = content->add(std::make_unique<ButtonControl>("Below"));
            nav->set_content(std::move(content));
            ui.window().set_content(std::move(nav));
            ui.layout(Client);
        }
        HeadlessUI     ui = create_headless();
        TextControl*   label{};
        ButtonControl* below{};
    };
    Page page{20.0f};
    page.label->set_height(100.0f);
    page.ui.layout(Client);

    Page fresh{100.0f};
    REQUIRE(page.label->bounds() == fresh.label->bounds());
    REQUIRE(page.below->bounds() == fresh.below->bounds());
}

TEST_CASE("Changing an alignment arranges without measuring", "[perf-count]")
{
    ButtonWall wall{100};
    wall.buttons[5]->set_horizontal_alignment(Alignment::Center);
    wall.ui.layout(Client);
    const PerfCountScope scope;
    wall.buttons[5]->set_horizontal_alignment(Alignment::Trailing);
    wall.ui.layout(Client);
    const PerfCounters work = scope.elapsed();
    REQUIRE(work.measures == 0);
    REQUIRE(work.arranges == 1);
    ButtonWall fresh{100};
    fresh.buttons[5]->set_horizontal_alignment(Alignment::Trailing);
    fresh.ui.layout(Client);
    REQUIRE(wall.buttons[5]->bounds() == fresh.buttons[5]->bounds());
}

TEST_CASE("A layout pass builds no VisualTree until one is asked for", "[perf-count]")
{
    ButtonWall           wall{10};
    const PerfCountScope scope;
    wall.buttons[0]->set_text("Changed");
    wall.ui.layout(Client);
    REQUIRE(scope.elapsed().visual_tree_builds == 0);
    REQUIRE(wall.ui.window().visual_tree().size() == 11);
    REQUIRE(scope.elapsed().visual_tree_builds == 1);
}

TEST_CASE("The counters count the work of a first layout and a frame", "[perf-count]")
{
    const PerfCountScope scope;
    ButtonWall           wall{10};
    TestRenderer         renderer;
    wall.ui.window().render(renderer);
    const PerfCounters work = scope.elapsed();
    REQUIRE(work.layout_passes == 1);
    // The stack and its ten buttons, each measured and arranged once.
    REQUIRE(work.measures >= 11);
    REQUIRE(work.arranges >= 11);
    REQUIRE(work.text_measurements >= 10);
    REQUIRE(work.frames == 1);
    REQUIRE(work.renders >= 11);
}

TEST_CASE("The test binary counts heap allocations", "[perf-count]")
{
    const test_support::AllocationScope scope;
    auto                                owned = std::make_unique<int>(4);
    REQUIRE(scope.allocations() == 1);
    REQUIRE(scope.bytes() == sizeof(int));
}

TEST_CASE("A frame with nothing changed has no damage", "[perf-count][damage]")
{
    ButtonWall   wall{100};
    TestRenderer renderer;
    REQUIRE(wall.ui.window().take_damage().size() == 1); // the first frame is all of it
    wall.ui.window().render(renderer);
    wall.ui.layout(Client);
    REQUIRE(wall.ui.window().take_damage().empty());
}

TEST_CASE("Hovering a button damages the button and draws only near it", "[perf-count][damage]")
{
    ButtonWall   wall{1000};
    TestRenderer first;
    (void)wall.ui.window().take_damage(); // the host asks before every frame
    wall.ui.window().render(first);

    const Rect button = wall.buttons[10]->bounds();
    seams(wall.ui.window()).handle_pointer({center(*wall.buttons[10]), PointerButton::None, PointerAction::Move});
    wall.ui.layout(Client);
    const std::vector<Rect> damage = wall.ui.window().take_damage();
    REQUIRE(damage.size() == 1);
    REQUIRE(covers(damage[0], button));
    REQUIRE(damage[0].height < button.height + 16.0f);
    REQUIRE(damage[0].width < button.width + 16.0f);

    const PerfCountScope scope;
    TestRenderer         partial;
    wall.ui.window().render(partial, damage[0]);
    // The stack, the hovered button and the neighbors whose drawing touches it.
    REQUIRE(scope.elapsed().renders < 8);
    REQUIRE(scope.elapsed().renders >= 2);
    wall.ui.layout(Client);
    REQUIRE(wall.ui.window().take_damage().empty());
}

TEST_CASE("A label that grows damages where it and the elements it pushed drew and draw", "[perf-count][damage]")
{
    ButtonWall   wall{10};
    auto*        stack = static_cast<StackControl*>(wall.ui.window().content());
    TextControl* label = stack->insert(0, std::make_unique<TextControl>("Short"));
    wall.ui.layout(Client);
    TestRenderer first;
    (void)wall.ui.window().take_damage();
    wall.ui.window().render(first);
    const Rect last_before = wall.buttons.back()->bounds();

    label->set_text("Short\nand a second line");
    label->set_wrapping(TextWrapping::Wrap);
    wall.ui.layout(Client);
    const Rect              last_after = wall.buttons.back()->bounds();
    const std::vector<Rect> damage     = wall.ui.window().take_damage();
    REQUIRE(last_after.y > last_before.y);
    const auto covered = [&damage](const Rect& r)
    {
        for (const Rect& d : damage)
        {
            if (covers(d, r))
            {
                return true;
            }
        }
        return false;
    };
    REQUIRE(covered(last_before));
    REQUIRE(covered(last_after));
    REQUIRE(covered(label->bounds()));
}

TEST_CASE("A frame with nothing to repaint allocates nothing per element", "[perf-count][alloc]")
{
    // Whatever a frame allocates (an MSVC debug build allocates for every std::vector, even an
    // empty one) does not grow with the tree.
    const auto allocations_per_frame = [](int buttons)
    {
        ButtonWall   wall{buttons};
        TestRenderer first;
        (void)wall.ui.window().take_damage();
        wall.ui.window().render(first);
        (void)wall.ui.tick(16ms);
        wall.ui.layout(Client);
        (void)wall.ui.window().take_damage();

        const test_support::AllocationScope scope;
        (void)wall.ui.tick(16ms);
        wall.ui.layout(Client);
        REQUIRE(wall.ui.window().take_damage().empty());
        return scope.allocations();
    };
    const std::uint64_t small = allocations_per_frame(10);
    const std::uint64_t large = allocations_per_frame(1000);
    INFO("10 buttons: " << small << ", 1000 buttons: " << large);
    REQUIRE(large == small);
    REQUIRE(small <= 8);
}

TEST_CASE("A window with nothing going on wants no ticks", "[perf-count][tick]")
{
    ButtonWall wall{100};
    wall.ui.show(Client);
    wall.ui.layout(Client);
    (void)wall.ui.tick(16ms);
    REQUIRE_FALSE(wall.ui.window().wants_ticks());

    // A hover changes how a button draws: the tick that sees it is a busy one.
    seams(wall.ui.window()).handle_pointer({center(*wall.buttons[3]), PointerButton::None, PointerAction::Move});
    wall.buttons[3]->set_text("Hovered");
    (void)wall.ui.tick(16ms);
    REQUIRE(wall.ui.window().wants_ticks());
    wall.ui.layout(Client);
    (void)wall.ui.tick(16ms);
    REQUIRE_FALSE(wall.ui.window().wants_ticks());
}

TEST_CASE("A focused text box wakes its window for each blink and keeps it no busier", "[perf-count][tick]")
{
    HeadlessUI      ui  = create_headless();
    TextBoxControl* box = ui.window().set_content(std::make_unique<TextBoxControl>("Text"));
    ui.show(Client);
    ui.layout(Client);
    REQUIRE(ui.window().focus_element(box));
    (void)ui.tick(30ms); // the focus change: a busy tick, which starts the blink cycle
    ui.layout(Client);
    (void)ui.tick(0ms);
    REQUIRE_FALSE(ui.window().wants_ticks());
    REQUIRE(ui.window().next_wake() == Duration{TextBoxControl::CaretBlinkIntervalMs - 30.0});

    // Woken at each blink and at nothing in between, the window draws the caret and stays idle.
    int ticks = 0;
    for (int blink = 0; blink < 4; ++blink)
    {
        const std::optional<Duration> wait = ui.window().next_wake();
        REQUIRE(wait.has_value());
        const bool before = box->caret_visible();
        REQUIRE(ui.tick(Duration{*wait}));
        ++ticks;
        CHECK(box->caret_visible() != before);
        CHECK_FALSE(ui.window().wants_ticks());
        ui.layout(Client);
    }
    CHECK(ticks == 4);
    CHECK(ui.window().next_wake() == Duration{TextBoxControl::CaretBlinkIntervalMs});
}

TEST_CASE("A bitmap keeps its content id until its pixels change", "[perf-count]")
{
    Bitmap              a{4, 4, colors::White};
    const std::uint64_t id = a.content_id();
    REQUIRE(id != 0);
    REQUIRE(a.content_id() == id);

    const Bitmap copy = a; // same pixels, so the same upload serves both
    REQUIRE(copy.content_id() == id);

    a.set(0, 0, colors::Black);
    REQUIRE(a.content_id() != id);
    REQUIRE(copy.content_id() == id);
}

#ifdef _WIN32
TEST_CASE("Drawing an unchanged frame through Direct2D creates no text, brush or geometry", "[perf-count][pixel]")
{
    OffscreenCapture d2d;
    REQUIRE(d2d.ok());

    HeadlessUI ui    = create_headless();
    auto       stack = std::make_unique<StackControl>(Orientation::Vertical);
    for (int i = 0; i < 20; ++i)
    {
        stack->add(std::make_unique<ButtonControl>("Button " + std::to_string(i)));
    }
    stack->add(std::make_unique<CheckBoxControl>("Remember me", true));
    stack->add(std::make_unique<SliderControl>());
    stack->add(std::make_unique<TextBoxControl>("Some text"));
    stack->add(std::make_unique<ImageControl>(Bitmap{16, 16, colors::Black}));
    ui.window().set_content(std::move(stack));
    ui.window().layout(Client, d2d.measurer());

    (void)d2d.capture(ui.window(), Client);
    const PerfCountScope scope;
    (void)d2d.capture(ui.window(), Client);
    const PerfCounters work = scope.elapsed();
    REQUIRE(work.frames == 1);
    REQUIRE(work.text_formats_created == 0);
    REQUIRE(work.text_layouts_created == 0);
    REQUIRE(work.brushes_created == 0);
    REQUIRE(work.geometries_created == 0);
    REQUIRE(work.stroke_styles_created == 0);
    REQUIRE(work.bitmaps_created == 0);
}
TEST_CASE("Repainting only the damage gives what a full frame gives", "[perf-count][damage][pixel]")
{
    OffscreenCapture partial;
    OffscreenCapture reference;
    REQUIRE(partial.ok());
    const Theme theme = Theme::light();

    HeadlessUI ui = create_headless();
    ui.window().pin_theme(theme);
    auto                        stack = std::make_unique<StackControl>(Orientation::Vertical);
    std::vector<ButtonControl*> buttons;
    for (int i = 0; i < 6; ++i)
    {
        buttons.push_back(stack->add(std::make_unique<ButtonControl>("Button " + std::to_string(i))));
    }
    TextControl*     label = stack->add(std::make_unique<TextControl>("A label"));
    CheckBoxControl* check = stack->add(std::make_unique<CheckBoxControl>("Remember me", false));
    stack->add(std::make_unique<SliderControl>());
    stack->add(std::make_unique<TextBoxControl>("Some text"));
    buttons.push_back(stack->add(std::make_unique<ButtonControl>("Below")));
    ui.window().set_content(std::move(stack));

    const auto frame_matches = [&](const char* step)
    {
        ui.window().layout(Client, partial.measurer());
        const Bitmap changed = partial.capture_changes(ui.window(), Client, theme.colors().background);
        const Bitmap full    = reference.capture(ui.window(), Client, theme.colors().background);
        int          wrong   = 0;
        for (int y = 0; y < full.height(); ++y)
        {
            for (int x = 0; x < full.width(); ++x)
            {
                const Color a = changed.get(x, y);
                const Color b = full.get(x, y);
                if (std::abs(a.r - b.r) > 1.5f / 255.0f || std::abs(a.g - b.g) > 1.5f / 255.0f ||
                    std::abs(a.b - b.b) > 1.5f / 255.0f || std::abs(a.a - b.a) > 1.5f / 255.0f)
                {
                    ++wrong;
                }
            }
        }
        INFO(step);
        REQUIRE(wrong == 0);
    };

    frame_matches("first frame");
    seams(ui.window()).handle_pointer({center(*buttons[2]), PointerButton::None, PointerAction::Move});
    frame_matches("hover");
    seams(ui.window()).handle_pointer({center(*check), PointerButton::Left, PointerAction::Down});
    frame_matches("press");
    seams(ui.window()).handle_pointer({center(*check), PointerButton::Left, PointerAction::Up});
    frame_matches("click");
    label->set_text("A label that now takes two lines,\nso everything below it moves down");
    frame_matches("text that moves what is below");
    seams(ui.window()).handle_pointer({{1.0f, 590.0f}, PointerButton::None, PointerAction::Move});
    frame_matches("pointer gone");
}

TEST_CASE("Repainting only the damage blurs acrylic as a full frame does", "[perf-count][damage][pixel][acrylic]")
{
    OffscreenCapture partial;
    OffscreenCapture reference;
    REQUIRE(partial.ok());
    const Theme theme = Theme::light();

    // A red block half under an acrylic panel, and a button off to the side.
    HeadlessUI ui = create_headless();
    ui.window().pin_theme(theme);
    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    auto grid  = std::make_unique<GridControl>();
    grid->add_column(GridLength::fraction());
    grid->add_column(GridLength::fraction());
    grid->set_width(200.0f);
    grid->set_height(120.0f);
    auto red = std::make_unique<RectangleControl>();
    red->set_fill(Color::from_rgba(255, 0, 0));
    grid->add(std::move(red), 0, 0);
    auto              panel = std::make_unique<RectangleControl>();
    RectangleControl* glass = panel.get();
    AcrylicBrush      brush;
    brush.tint_opacity   = 0.3f;
    brush.fallback_color = colors::White;
    glass->set_fill(brush);
    grid->add(std::move(panel), 0, 0, 1, 2);
    stack->add(std::move(grid));
    ButtonControl* button = stack->add(std::make_unique<ButtonControl>("Beside"));
    ui.window().set_content(std::move(stack));

    const auto frame_matches = [&](const char* step)
    {
        ui.window().layout(Client, partial.measurer());
        const Bitmap changed = partial.capture_changes(ui.window(), Client, theme.colors().background);
        const Bitmap full    = reference.capture(ui.window(), Client, theme.colors().background);
        int          wrong   = 0;
        for (int y = 0; y < full.height(); ++y)
        {
            for (int x = 0; x < full.width(); ++x)
            {
                const Color a = changed.get(x, y);
                const Color b = full.get(x, y);
                if (std::abs(a.r - b.r) > 1.5f / 255.0f || std::abs(a.g - b.g) > 1.5f / 255.0f ||
                    std::abs(a.b - b.b) > 1.5f / 255.0f || std::abs(a.a - b.a) > 1.5f / 255.0f)
                {
                    ++wrong;
                }
            }
        }
        INFO(step);
        REQUIRE(wrong == 0);
    };

    frame_matches("first frame");
    brush.tint_opacity = 0.7f;
    glass->set_fill(brush);
    frame_matches("a new tint");
    seams(ui.window()).handle_pointer({center(*button), PointerButton::None, PointerAction::Move});
    frame_matches("hover beside it");
}

#endif
