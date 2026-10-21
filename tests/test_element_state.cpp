// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/containers/ScaleToFit.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Canvas.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/Panel.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

namespace
{
// Counts the state hooks Element runs.
class Probe : public Element
{
public:
    int enabled_changes = 0;
    int focus_changes   = 0;
    int attaches        = 0;
    int detaches        = 0;

    [[nodiscard]] bool focusable() const override
    {
        return is_enabled();
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {40.0f, 20.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
    void enabled_changed() override
    {
        ++enabled_changes;
    }
    void focus_changed(bool) override
    {
        ++focus_changes;
    }
    void attached() override
    {
        ++attaches;
    }
    void detached() override
    {
        ++detaches;
    }
};
} // namespace

TEST_CASE("disabling a panel disables everything inside it", "[element][enabled]")
{
    HeadlessUI                     ui = create_headless();
    ui::Control<ui::Button>        button;
    ui::Control<ui::VerticalStack> stack;
    ui.window().set_content(ui::VerticalStack{
        .control = stack,
        .content = {ui::Button{.text = "Save", .control = button}},
    });
    ui.layout({400, 300});
    CHECK(button->is_enabled());
    CHECK(ui.window().focus_element(button));

    stack->set_enabled(false);
    CHECK(button->enabled()); // its own flag is untouched
    CHECK_FALSE(button->is_enabled());
    ui.layout({400, 300});
    CHECK_FALSE(ui.window().focus_element(button));

    stack->set_enabled(true);
    CHECK(button->is_enabled());
}

TEST_CASE("a panel spec's .enabled binds", "[element][enabled][spec]")
{
    Property<bool>           on{false};
    ui::Control<ui::Button>  button;
    std::unique_ptr<Element> tree = ui::build(ui::HorizontalStack{
        .enabled = on,
        .content = {ui::Button{.text = "Go", .control = button}},
    });
    CHECK_FALSE(button->is_enabled());
    on = true;
    CHECK(button->is_enabled());
}

TEST_CASE("enabled_changed runs where the effective state flips, and only there", "[element][enabled]")
{
    StackControl stack{Orientation::Vertical};
    Probe*       inner = stack.add(std::make_unique<Probe>());
    Probe*       off   = stack.add(std::make_unique<Probe>());
    off->set_enabled(false);
    const int off_before = off->enabled_changes;

    stack.set_enabled(false);
    CHECK(inner->enabled_changes == 1);
    CHECK(off->enabled_changes == off_before); // it was off already
    stack.set_enabled(true);
    CHECK(inner->enabled_changes == 2);
    CHECK_FALSE(off->is_enabled());

    // Moving into a disabled parent flips it too.
    StackControl disabled{Orientation::Vertical};
    disabled.set_enabled(false);
    Probe* moved = disabled.add(std::make_unique<Probe>());
    CHECK_FALSE(moved->is_enabled());
    CHECK(moved->enabled_changes == 1);
}

TEST_CASE("is_focused follows the window, and focus_changed runs once per change", "[element][focus]")
{
    HeadlessUI ui    = create_headless();
    Probe*     probe = ui.window().set_content(std::make_unique<Probe>());
    ui.layout({400, 300});
    CHECK_FALSE(probe->is_focused());
    CHECK(ui.window().focus_element(probe));
    CHECK(probe->is_focused());
    CHECK(probe->focus_changes == 1);
    ui.window().blur();
    CHECK_FALSE(probe->is_focused());
    CHECK(probe->focus_changes == 2);
}

TEST_CASE("window() finds the window, and attached runs as a tree joins it", "[element][window]")
{
    auto   stack = std::make_unique<StackControl>(Orientation::Vertical);
    Probe* probe = stack->add(std::make_unique<Probe>());
    CHECK(probe->window() == nullptr);
    CHECK(probe->attaches == 0);

    HeadlessUI    ui   = create_headless();
    StackControl* root = ui.window().set_content(std::move(stack));
    CHECK(probe->window() == &ui.window());
    CHECK(probe->attaches == 1);

    Probe* late = root->add(std::make_unique<Probe>());
    CHECK(late->window() == &ui.window());
    CHECK(late->attaches == 1);
}

TEST_CASE("orphan() takes a child out alive, unfocused and out of the window", "[element][orphan]")
{
    HeadlessUI    ui    = create_headless();
    StackControl* root  = ui.window().set_content(std::make_unique<StackControl>(Orientation::Vertical));
    Probe*        first = root->add(std::make_unique<Probe>());
    Probe*        moved = root->add(std::make_unique<Probe>());
    ui.layout({400, 300});
    REQUIRE(ui.window().focus_element(moved));

    std::unique_ptr<Element> owned = root->orphan(moved);
    REQUIRE(owned.get() == moved);
    CHECK(root->children().size() == 1);
    CHECK(moved->parent() == nullptr);
    CHECK(moved->window() == nullptr);
    CHECK(moved->detaches == 1);
    CHECK_FALSE(moved->is_focused());
    CHECK(ui.window().focused() == nullptr);
    CHECK(root->orphan(moved) == nullptr); // no longer a child

    // It goes back in elsewhere and takes focus again.
    root->add(std::move(owned));
    ui.layout({400, 300});
    CHECK(moved->window() == &ui.window());
    CHECK(ui.window().focus_element(moved));
    CHECK(first->window() == &ui.window());
}

TEST_CASE("Panel, Grid and ContentHost hand an orphaned child back", "[element][orphan]")
{
    auto   canvas = std::make_unique<CanvasControl>();
    Probe* a      = canvas->add(std::make_unique<Probe>(), 0.0f, 0.0f);
    CHECK(canvas->orphan(a).get() == a);
    CHECK(canvas->children().empty());

    auto   grid = std::make_unique<GridControl>();
    Probe* b    = grid->add(std::make_unique<Probe>(), 0, 0);
    CHECK(grid->orphan(b).get() == b);
    CHECK(grid->orphan(b) == nullptr);

    auto                     host = std::make_unique<ContentHost>();
    Probe*                   c    = host->set_child(std::make_unique<Probe>());
    std::unique_ptr<Element> back = host->orphan_child();
    CHECK(back.get() == c);
    CHECK(host->child() == nullptr);
    CHECK(c->parent() == nullptr);
}

namespace
{
// A container that counts the keys and text that bubble up to it.
class KeySink : public Panel
{
public:
    template <typename T>
    T* add(std::unique_ptr<T> child)
    {
        return static_cast<T*>(adopt_child(std::move(child)));
    }
    int  keys      = 0;
    int  texts     = 0;
    bool take_keys = true;

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext& context) const override
    {
        Size total{};
        heights_.clear();
        for (const auto& child : children())
        {
            const Size s = child->measure(available, context);
            total        = {std::max(total.width, s.width), total.height + s.height};
            heights_.push_back(s.height);
        }
        return total;
    }
    void arrange_override(const Rect& rect, const ArrangeContext&) override
    {
        float       y = rect.y;
        std::size_t i = 0;
        for (const auto& child : children())
        {
            const float h = i < heights_.size() ? heights_[i] : 0.0f;
            child->arrange({rect.x, y, rect.width, h});
            y += h;
            ++i;
        }
    }
    mutable std::vector<float> heights_;
    bool                       key_override(const KeyEvent&) override
    {
        ++keys;
        return take_keys;
    }
    bool text_override(const TextInputEvent&) override
    {
        ++texts;
        return true;
    }
};
} // namespace

TEST_CASE("a key the focused element leaves alone bubbles to its parents", "[element][keys]")
{
    HeadlessUI ui    = create_headless();
    KeySink*   sink  = ui.window().set_content(std::make_unique<KeySink>());
    Probe*     probe = sink->add(std::make_unique<Probe>());
    ui.layout({400, 300});
    REQUIRE(ui.window().focus_element(probe));

    CHECK(seams(ui.window()).handle_key({Key::Enter, KeyAction::Down}));
    CHECK(sink->keys == 1);
    seams(ui.window()).handle_text("a");
    CHECK(sink->texts == 1);

    // Handled nowhere, an arrow moves focus instead.
    sink->take_keys = false;
    Probe* below    = sink->add(std::make_unique<Probe>());
    ui.layout({400, 300});
    CHECK(seams(ui.window()).handle_key({Key::ArrowDown, KeyAction::Down}));
    CHECK(sink->keys == 2);
    CHECK(ui.window().focused() == below);
}

TEST_CASE("the window draws a focusable element's ring over the tree, with no room kept for it",
          "[element][focus][focus-ring]")
{
    Probe                probe;
    const Theme          theme = Theme::fluent(Palette::fluent_light());
    HeadlessTextMeasurer measurer;
    const Size           size = probe.measure({400, 300}, MeasureContext{measurer, theme});
    CHECK(size.width == 40.0f); // layout never depends on focus
    probe.arrange({0, 0, size.width, size.height});

    TestRenderer unfocused;
    ui::testing::render_with_focus_ring(probe, unfocused, theme);
    ui::testing::set_focused(probe, true);
    TestRenderer drawn_alone;
    probe.render(drawn_alone, theme);
    CHECK(drawn_alone.count() == unfocused.count()); // the element itself draws no ring
    TestRenderer focused;
    ui::testing::render_with_focus_ring(probe, focused, theme);
    CHECK(focused.count() > unfocused.count());

    // A click gives focus without the ring.
    ui::testing::set_focus_visible(probe, false);
    TestRenderer clicked;
    ui::testing::render_with_focus_ring(probe, clicked, theme);
    CHECK(clicked.count() == unfocused.count());
}

namespace
{
// Logs the pointer events it gets; handles a press when told to.
class PointerLog : public Element
{
public:
    explicit PointerLog(std::vector<std::string>& log, std::string name, bool handles = false) :
        log_(log),
        name_(std::move(name)),
        handles_(handles)
    {
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {40.0f, 40.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
    bool pointer_override(const PointerEvent& e) override
    {
        static constexpr const char* Names[] = {"move", "down", "up", "leave", "wheel"};
        log_.push_back(name_ + ":" + Names[static_cast<int>(e.action)]);
        return handles_ && (e.action == PointerAction::Down || e.action == PointerAction::Up);
    }

private:
    std::vector<std::string>& log_;
    std::string               name_;
    bool                      handles_;
};

PointerEvent at(PointerAction action, Point p)
{
    return {p,
            action == PointerAction::Move || action == PointerAction::Leave ? PointerButton::None : PointerButton::Left,
            action};
}
} // namespace

TEST_CASE("only the topmost of two overlapping siblings gets a press", "[element][pointer]")
{
    std::vector<std::string> log;
    CanvasControl            canvas;
    canvas.add(std::make_unique<PointerLog>(log, "below"), 0.0f, 0.0f);
    canvas.add(std::make_unique<PointerLog>(log, "above"), 20.0f, 20.0f);
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();
    (void)canvas.measure({200, 200}, MeasureContext{measurer, theme});
    canvas.arrange({0, 0, 200, 200});

    canvas.handle_pointer(at(PointerAction::Down, {30, 30}));
    CHECK(log == std::vector<std::string>{"above:down"});
}

TEST_CASE("a handled press stops bubbling, and captures the pointer until the release", "[element][pointer]")
{
    std::vector<std::string> log;
    auto                     outer = std::make_unique<KeySink>();
    KeySink&                 sink  = *outer;
    sink.add(std::make_unique<PointerLog>(log, "a", true));
    sink.add(std::make_unique<PointerLog>(log, "b"));
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();
    (void)sink.measure({200, 200}, MeasureContext{measurer, theme});
    sink.arrange({0, 0, 200, 200});

    sink.handle_pointer(at(PointerAction::Down, {10, 10})); // on a
    sink.handle_pointer(at(PointerAction::Move, {10, 50})); // over b, a holds the capture
    sink.handle_pointer(at(PointerAction::Up, {10, 50}));   // the release goes to a, then a leaves
    sink.handle_pointer(at(PointerAction::Move, {10, 55})); // b now
    CHECK(log == std::vector<std::string>{"a:down", "a:move", "a:up", "a:leave", "b:move"});
}

namespace
{
// A PointerLog container that also records the presses something inside it handled.
class HandledLog final : public KeySink
{
public:
    explicit HandledLog(std::vector<std::string>& log) : log_(log)
    {
    }

protected:
    bool pointer_override(const PointerEvent& e) override
    {
        if (e.action == PointerAction::Down)
        {
            log_.push_back("row:down");
        }
        return false;
    }
    void handled_pointer_override(const PointerEvent& e) override
    {
        if (e.action == PointerAction::Down)
        {
            log_.push_back("row:handled down");
        }
    }

private:
    std::vector<std::string>& log_;
};
} // namespace

TEST_CASE("a container sees a press that an element inside it handled through handled_pointer_override()",
          "[element][pointer]")
{
    std::vector<std::string> log;
    HandledLog               row{log};
    row.add(std::make_unique<PointerLog>(log, "a", true));
    row.add(std::make_unique<PointerLog>(log, "b"));
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();
    (void)row.measure({200, 200}, MeasureContext{measurer, theme});
    row.arrange({0, 0, 200, 200});

    CHECK(row.handle_pointer(at(PointerAction::Down, {10, 10}))); // a handles it
    row.handle_pointer(at(PointerAction::Up, {10, 10}));
    CHECK(row.handle_pointer(at(PointerAction::Down, {10, 50})) == false); // b does not
    row.handle_pointer(at(PointerAction::Up, {10, 50}));
    CHECK(std::ranges::count(log, std::string{"row:handled down"}) == 1);
    CHECK(std::ranges::count(log, std::string{"row:down"}) == 1);
    CHECK(log.front() == "a:down");
    CHECK(log[1] == "row:handled down");
}

TEST_CASE("pointer entered and exited follow the element under the pointer", "[element][pointer]")
{
    std::vector<std::string> log;
    auto                     outer = std::make_unique<KeySink>();
    KeySink&                 sink  = *outer;
    Element*                 a     = sink.add(std::make_unique<PointerLog>(log, "a"));
    Element*                 b     = sink.add(std::make_unique<PointerLog>(log, "b"));
    int                      a_in = 0, a_out = 0, b_in = 0;
    a->on_pointer_entered([&] { ++a_in; });
    a->on_pointer_exited([&] { ++a_out; });
    b->on_pointer_entered([&] { ++b_in; });
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();
    (void)sink.measure({200, 200}, MeasureContext{measurer, theme});
    sink.arrange({0, 0, 200, 200});

    sink.handle_pointer(at(PointerAction::Move, {10, 10}));
    sink.handle_pointer(at(PointerAction::Move, {12, 12}));
    CHECK(a_in == 1);
    sink.handle_pointer(at(PointerAction::Move, {10, 50}));
    CHECK(a_out == 1);
    CHECK(b_in == 1);
    CHECK(std::ranges::count(log, std::string{"a:leave"}) == 1);
}

TEST_CASE("a press inside a ScaleToFit reaches the child where it is drawn", "[element][pointer]")
{
    std::vector<std::string> log;
    ScaleToFitControl        box;
    box.set_child(std::make_unique<PointerLog>(log, "child"));
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();
    (void)box.measure({160, 160}, MeasureContext{measurer, theme});
    box.arrange({0, 0, 160, 160}); // the 40x40 child drawn 4x, over the whole box

    box.handle_pointer(at(PointerAction::Down, {150, 150}));
    CHECK(log == std::vector<std::string>{"child:down"});
    box.handle_pointer(at(PointerAction::Down, {170, 150})); // outside: not hit, and the child is left
    CHECK(log == std::vector<std::string>{"child:down", "child:leave"});
}
