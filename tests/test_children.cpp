// SPDX-License-Identifier: MIT
// Element::for_each_child(): a container that lists its children once gets every tree-wide walk
// (ticks, focus, dirty clearing, popups, automation, the visual tree) without forwarding any.
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;

namespace
{
// Two children side by side, and nothing else: the only walk it overrides is for_each_child().
class Pair final : public Element
{
public:
    Pair(std::unique_ptr<Element> left, std::unique_ptr<Element> right) :
        left_(std::move(left)),
        right_(std::move(right))
    {
        set_parent_of(*left_, this);
        set_parent_of(*right_, this);
    }

    void for_each_child(ChildVisitor visit) const override
    {
        visit(*left_);
        visit(*right_);
    }

protected:
    Size measure_override(Size available, const MeasureContext& context) const override
    {
        const Size half{available.width / 2.0f, available.height};
        const Size l = left_->measure(half, context);
        const Size r = right_->measure(half, context);
        return {l.width + r.width, std::max(l.height, r.height)};
    }
    void arrange_override(const Rect& final_rect, const ArrangeContext&) override
    {
        const float half = final_rect.width / 2.0f;
        left_->arrange(Rect{final_rect.x, final_rect.y, half, final_rect.height});
        right_->arrange(Rect{final_rect.x + half, final_rect.y, half, final_rect.height});
    }
    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        left_->render(renderer, context);
        right_->render(renderer, context);
    }
    bool pointer_override(const PointerEvent& e) override
    {
        left_->handle_pointer(e);
        right_->handle_pointer(e);
        return false;
    }

private:
    std::unique_ptr<Element> left_;
    std::unique_ptr<Element> right_;
};

// A leaf that counts its ticks.
class Ticker final : public Element
{
public:
    int ticks = 0;

protected:
    Size measure_override(Size, const MeasureContext&) const override
    {
        return {10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

    void tick_override(Duration) override
    {
        ++ticks;
    }
};

PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
} // namespace

TEST_CASE("A container that only lists its children has them ticked", "[children][tick]")
{
    HeadlessUI ui     = create_headless();
    auto       left   = std::make_unique<Ticker>();
    auto       right  = std::make_unique<Ticker>();
    Ticker*    first  = left.get();
    Ticker*    second = right.get();
    ui.window().set_content(std::make_unique<Pair>(std::move(left), std::move(right)));
    ui.layout({400, 300});

    ui.tick(16ms);
    ui.tick(16ms);
    REQUIRE(first->ticks == 2);
    REQUIRE(second->ticks == 2);
}

TEST_CASE("Tab reaches the focusable children of a container that only lists them", "[children][focus]")
{
    HeadlessUI     ui    = create_headless();
    auto           left  = ui.button("One");
    auto           right = ui.button("Two");
    ButtonControl* one   = left.get();
    ButtonControl* two   = right.get();
    ui.window().set_content(std::make_unique<Pair>(std::move(left), std::move(right)));
    ui.layout({400, 300});

    Window& win = ui.window();
    win.focus_content();
    REQUIRE(win.focused() == one);
    seams(win).handle_key({Key::Tab, KeyAction::Down});
    REQUIRE(win.focused() == two);
    seams(win).handle_key({Key::Tab, KeyAction::Down});
    REQUIRE(win.focused() == one); // the ring wraps
}

TEST_CASE("clear_dirty() clears the children of a container that only lists them", "[children][dirty]")
{
    auto         left  = std::make_unique<TextControl>("Left");
    auto         right = std::make_unique<TextControl>("Right");
    TextControl* label = right.get();
    Pair         pair{std::move(left), std::move(right)};
    REQUIRE(label->is_dirty()); // a new element starts dirty

    ui::testing::clear_dirty(pair);
    REQUIRE_FALSE(pair.is_dirty());
    REQUIRE_FALSE(label->is_dirty());

    // Cleared all the way down, so the next change bubbles up again.
    label->set_text("Changed");
    REQUIRE(label->is_dirty(DirtyFlags::Layout));
    REQUIRE(pair.is_dirty(DirtyFlags::Layout));
}

TEST_CASE("A child's flyout is found through a container that only lists its children", "[children][popup]")
{
    HeadlessUI     ui     = create_headless();
    auto           owned  = std::make_unique<FlyoutControl>();
    FlyoutControl* flyout = owned.get();
    ButtonControl* anchor = flyout->set_anchor(std::make_unique<ButtonControl>("Open"));
    flyout->set_content(std::make_unique<TextControl>("Flyout content"));
    ui.window().set_content(std::make_unique<Pair>(ui.text("Label"), std::move(owned)));
    ui.layout({400, 300});

    std::vector<Element*> owners;
    ui::testing::ElementAccess::collect_live(*ui.window().content(), owners);
    REQUIRE(std::find(owners.begin(), owners.end(), flyout) != owners.end());

    // The window drives it as a popup: it lays it out above the tree and light-dismisses it on a
    // press outside, which only an owner it found can get.
    flyout->show();
    ui.layout({400, 300});
    REQUIRE(flyout->is_open());
    REQUIRE(anchor->bounds().width > 0.0f);
    seams(ui.window()).handle_pointer(down({-500.0f, -500.0f}));
    REQUIRE_FALSE(flyout->is_open());
}

TEST_CASE("Automation lists the children of a container that only lists them", "[children][automation]")
{
    auto         left  = std::make_unique<TextControl>("Left");
    auto         right = std::make_unique<ButtonControl>("Right");
    TextControl* text  = left.get();
    Element*     btn   = right.get();
    Pair         pair{std::move(left), std::move(right)};

    REQUIRE(automation_children(pair) == std::vector<Element*>{text, btn});
    REQUIRE(automation_parent(*btn) == &pair);
}

TEST_CASE("The visual tree holds the children of a container that only lists them", "[children][visual-tree]")
{
    Pair       pair{std::make_unique<TextControl>("Left"), std::make_unique<TextControl>("Right")};
    VisualTree tree;
    pair.build_visual_tree(tree, InvalidVisualId);
    REQUIRE(tree.size() == 3); // the pair, then its two children under it
}

namespace
{
// A leaf that scrolls: it takes every wheel turn offered to it.
class Scroller final : public Element
{
public:
    int turns = 0;

protected:
    Size measure_override(Size, const MeasureContext&) const override
    {
        return {10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
    bool wheel_override(const PointerEvent&) override
    {
        ++turns;
        return true;
    }
};

// A leaf that does not scroll, counting what reaches its pointer_override().
class WheelWatcher final : public Element
{
public:
    int wheels = 0;

protected:
    Size measure_override(Size, const MeasureContext&) const override
    {
        return {10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
    bool pointer_override(const PointerEvent& e) override
    {
        if (e.action == PointerAction::Wheel)
        {
            ++wheels;
        }
        return false;
    }
};

// A caption strip: a press on it drags the window.
class CaptionLeaf final : public Element
{
public:
    [[nodiscard]] HitRegion hit_region(Point) const override
    {
        return HitRegion::Caption;
    }

protected:
    Size measure_override(Size, const MeasureContext&) const override
    {
        return {10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
};

// One automation node over its child: it lists the child for the walks but hides it from UIA.
class Composite final : public Element
{
public:
    explicit Composite(std::unique_ptr<Element> child) : child_(std::move(child))
    {
        set_parent_of(*child_, this);
    }
    void for_each_child(ChildVisitor visit) const override
    {
        visit(*child_);
    }
    void automation_children(std::vector<Element*>&) const override
    {
    }

protected:
    Size measure_override(Size available, const MeasureContext& context) const override
    {
        return child_->measure(available, context);
    }
    void arrange_override(const Rect& final_rect, const ArrangeContext&) override
    {
        child_->arrange(final_rect);
    }
    void render_override(Renderer& renderer, const RenderContext& context) const override
    {
        child_->render(renderer, context);
    }

private:
    std::unique_ptr<Element> child_;
};

PointerEvent wheel(Point p)
{
    PointerEvent e{p, PointerButton::None, PointerAction::Wheel};
    e.wheel_delta = -120.0f;
    return e;
}
} // namespace

TEST_CASE("The wheel reaches the child under it in a container that only lists its children", "[children][wheel]")
{
    HeadlessUI    ui      = create_headless();
    auto          left    = std::make_unique<Scroller>();
    auto          right   = std::make_unique<WheelWatcher>();
    Scroller*     scrolls = left.get();
    WheelWatcher* watches = right.get();
    ui.window().set_content(std::make_unique<Pair>(std::move(left), std::move(right)));
    ui.layout({400, 300});
    Element& pair = *ui.window().content();

    // Over the scrolling child: it takes the turn and the container reports it consumed, so an
    // enclosing Scroll would not scroll as well.
    REQUIRE(pair.handle_wheel(wheel(scrolls->bounds().center())));
    REQUIRE(scrolls->turns == 1);
    REQUIRE(watches->wheels == 0);

    // Over the leaf that does not scroll: it gets the turn as a pointer event, and nobody took it.
    REQUIRE_FALSE(pair.handle_wheel(wheel(watches->bounds().center())));
    REQUIRE(watches->wheels == 1);
    REQUIRE(scrolls->turns == 1);
}

TEST_CASE("Caption hit testing sees a child hidden from automation", "[children][hit-region]")
{
    HeadlessUI ui = create_headless();
    ui.window().set_content(std::make_unique<Composite>(std::make_unique<CaptionLeaf>()));
    ui.layout({400, 300});
    REQUIRE(ui.window().content()->hit_region({200.0f, 150.0f}) == HitRegion::Caption);
}

TEST_CASE("Parenting a dirty child marks the new parent chain", "[children][dirty]")
{
    Pair pair{std::make_unique<TextControl>("Left"), std::make_unique<TextControl>("Right")};
    ui::testing::clear_dirty(pair);
    REQUIRE_FALSE(pair.is_dirty());

    std::unique_ptr<Element> orphan = std::make_unique<TextControl>("New"); // a new element starts dirty
    REQUIRE(adopt(pair, orphan) == orphan.get());
    REQUIRE(pair.is_dirty(DirtyFlags::Layout));
    detail::TreeAccess::set_parent(*orphan, nullptr); // the pair does not own it
}

TEST_CASE("A moved element gets a visual id of its own and keeps its watches", "[children][move]")
{
    Ticker         source;
    const VisualId id = source.visual_id();
    ElementWatch   watch{&source};

    Ticker moved{std::move(source)};
    REQUIRE(moved.visual_id() != id);
    REQUIRE(moved.visual_id() != InvalidVisualId);
    REQUIRE(watch.get() == &moved);
    REQUIRE(moved.is_dirty(DirtyFlags::Layout));
}

TEST_CASE("measure() defaults its theme as render() does", "[children][measure]")
{
    const HeadlessTextMeasurer measurer;
    TextControl                text{"Measured on its own"};
    const Size                 defaulted = text.measure({400.0f, 300.0f}, measurer);
    REQUIRE(defaulted == text.measure({400.0f, 300.0f}, measurer, Theme::default_light()));
    REQUIRE(defaulted.width > 0.0f);
}
