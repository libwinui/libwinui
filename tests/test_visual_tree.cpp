// SPDX-License-Identifier: MIT
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/VisualTree.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Text.hpp"

#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;

TEST_CASE("Every Element gets a distinct, non-zero VisualId", "[visual-tree]")
{
    ButtonControl a{"A"};
    ButtonControl b{"B"};
    REQUIRE(a.visual_id() != InvalidVisualId);
    REQUIRE(b.visual_id() != InvalidVisualId);
    REQUIRE(a.visual_id() != b.visual_id());
}

TEST_CASE("A fresh Element starts fully dirty", "[visual-tree]")
{
    ButtonControl b{"A"};
    REQUIRE(b.is_dirty());
    REQUIRE(b.is_dirty(DirtyFlags::Layout));
}

TEST_CASE("VisualId is stable across repeated layout passes", "[visual-tree][headless]")
{
    HeadlessUI     ui     = create_headless();
    ButtonControl* button = ui.window().set_content(ui.button("Hello"));
    ui.layout({800, 600});
    const VisualId id_after_first = button->visual_id();

    ui.layout({400, 300});
    REQUIRE(button->visual_id() == id_after_first);
}

TEST_CASE("Window::layout() builds a VisualTree mirroring the logical tree", "[visual-tree][headless]")
{
    HeadlessUI ui = create_headless();

    auto           label_owned = ui.text("Title");
    auto           save_owned  = ui.button("Save");
    TextControl*   label       = label_owned.get();
    ButtonControl* save        = save_owned.get();
    StackControl*  stack = ui.window().set_content(ui.vertical_stack(std::move(label_owned), std::move(save_owned)));
    ui.layout({800, 600});

    const VisualTree& tree = ui.window().visual_tree();
    REQUIRE(tree.size() == 3); // stack + label + button

    std::size_t stack_slot = 0, label_slot = 0, save_slot = 0;
    REQUIRE(tree.find(stack->visual_id(), stack_slot));
    REQUIRE(tree.find(label->visual_id(), label_slot));
    REQUIRE(tree.find(save->visual_id(), save_slot));

    REQUIRE(tree.parent_at(stack_slot) == InvalidVisualId);
    REQUIRE(tree.parent_at(label_slot) == stack->visual_id());
    REQUIRE(tree.parent_at(save_slot) == stack->visual_id());

    // Children are linked first_child -> ... -> next_sibling in add() order.
    REQUIRE(tree.first_child_at(stack_slot) == label->visual_id());
    REQUIRE(tree.next_sibling_at(label_slot) == save->visual_id());
    REQUIRE(tree.next_sibling_at(save_slot) == InvalidVisualId);

    REQUIRE(tree.bounds_at(label_slot) == label->bounds());
    REQUIRE(tree.bounds_at(save_slot) == save->bounds());
}

namespace
{
// Owns a part it does not list in for_each_child(), and adds it to the snapshot itself.
class WithPart final : public Element
{
public:
    WithPart()
    {
        set_parent_of(*part_, this);
    }
    [[nodiscard]] const Element& part() const
    {
        return *part_;
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {10.0f, 10.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
    void build_visual_tree_override(VisualTree& tree) const override
    {
        part_->build_visual_tree(tree, visual_id());
    }

private:
    std::unique_ptr<TextControl> part_ = std::make_unique<TextControl>("part");
};
} // namespace

TEST_CASE("build_visual_tree() adds the listed children, then the parts the hook adds", "[visual-tree]")
{
    auto         stack = std::make_unique<StackControl>(Orientation::Vertical);
    TextControl* label = stack->add(std::make_unique<TextControl>("Title"));
    WithPart*    owner = stack->add(std::make_unique<WithPart>());
    VisualTree   tree;
    stack->build_visual_tree(tree, InvalidVisualId);
    REQUIRE(tree.size() == 4);
    std::size_t slot = 0;
    REQUIRE(tree.find(label->visual_id(), slot));
    REQUIRE(tree.find(owner->part().visual_id(), slot));
    CHECK(tree.parent_at(slot) == owner->visual_id());
}

TEST_CASE("Window::layout() skips remeasure when nothing is layout-dirty", "[invalidation][headless]")
{
    HeadlessUI     ui     = create_headless();
    ButtonControl* button = ui.window().set_content(ui.button("Hello"));

    ui.layout({800, 600});
    REQUIRE(ui.window().layout_pass_count() == 1);
    REQUIRE_FALSE(button->is_dirty());

    // Same size, nothing changed: no work.
    ui.layout({800, 600});
    REQUIRE(ui.window().layout_pass_count() == 1);

    // A size change always forces a pass, dirty or not.
    ui.layout({400, 300});
    REQUIRE(ui.window().layout_pass_count() == 2);

    // Back to a stable size: settles into a no-op again.
    ui.layout({400, 300});
    REQUIRE(ui.window().layout_pass_count() == 2);

    // A text change marks Layout dirty, so the next layout() redoes the pass even at the same size.
    button->set_text("Hello, world!");
    REQUIRE(button->is_dirty(DirtyFlags::Layout));
    ui.layout({400, 300});
    REQUIRE(ui.window().layout_pass_count() == 3);
    REQUIRE_FALSE(button->is_dirty());
}

TEST_CASE("A Visual-only change does not force a re-measure, but the tab ring still updates", "[invalidation][headless]")
{
    HeadlessUI     ui    = create_headless();
    auto           a_own = ui.button("A");
    auto           b_own = ui.button("B");
    ButtonControl* a     = a_own.get();
    ButtonControl* b     = b_own.get();
    ui.window().set_content(ui.vertical_stack(std::move(a_own), std::move(b_own)));
    ui.layout({400, 300});
    REQUIRE(ui.window().layout_pass_count() == 1);

    b->set_enabled(false); // visual-only: doesn't affect measured size
    REQUIRE(b->is_dirty(DirtyFlags::Visual));
    REQUIRE_FALSE(b->is_dirty(DirtyFlags::Layout));

    ui.layout({400, 300}); // same size, nothing layout-dirty: still a no-op pass
    REQUIRE(ui.window().layout_pass_count() == 1);
    // The skipped pass still clears the Visual flag. Left set, tick() would keep reporting the
    // tree dirty and the host would repaint until something forced a layout.
    REQUIRE_FALSE(b->is_dirty());
    REQUIRE_FALSE(ui.window().tick(16ms));

    // The focus ring is rebuilt on every pass, so b is skipped.
    Window& win = ui.window();
    win.focus_content();
    REQUIRE(win.focused() == a);
    ui::testing::seams(win).handle_key({Key::Tab, KeyAction::Down});
    REQUIRE(win.focused() == a); // wraps back to a; b (disabled) is the only other control
}
