// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/panels/Canvas.hpp"
#include "winui/ui/text/Text.hpp"

#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

namespace
{
// A custom control that floats a list under itself, the way an extending-tier author writes one.
class ListOwner final : public Element
{
public:
    std::vector<PopupCloseReason> dismissals;
    bool                          close_on_dismiss = true;
    const Element*                anchor           = nullptr;
    std::optional<Rect>           anchor_rect{};

    void show()
    {
        open_popup({
            .content     = &list_,
            .anchor      = anchor,
            .anchor_rect = anchor_rect,
            .placement   = PopupPlacement::Below,
            .on_dismiss =
                [this](PopupCloseReason reason)
            {
                dismissals.push_back(reason);
                if (close_on_dismiss)
                {
                    close_popup();
                }
            },
        });
    }
    void hide()
    {
        close_popup();
    }
    [[nodiscard]] bool is_open() const
    {
        return popup_open();
    }
    [[nodiscard]] const TextControl& list() const
    {
        return list_;
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {100.0f, 20.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

private:
    TextControl list_{"list"};
};

PointerEvent press(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}

[[nodiscard]] int drawn_lists(Window& window)
{
    TestRenderer renderer;
    window.render(renderer);
    int count = 0;
    for (const RenderCommand& c : renderer.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t != nullptr && t->text == "list")
        {
            ++count;
        }
    }
    return count;
}

struct Fixture
{
    HeadlessUI     ui     = create_headless();
    CanvasControl* canvas = ui.window().set_content(std::make_unique<CanvasControl>());
    ListOwner*     owner  = canvas->add(std::make_unique<ListOwner>(), 50.0f, 40.0f);

    Fixture()
    {
        ui.layout({400, 300});
    }
};
} // namespace

TEST_CASE("open_popup() floats the content under its owner, and close_popup() takes it away", "[popup][open-popup]")
{
    Fixture f;
    CHECK(drawn_lists(f.ui.window()) == 0);

    f.owner->show();
    REQUIRE(f.owner->is_open());
    f.ui.layout({400, 300});
    const Rect list = f.owner->list().bounds();
    CHECK(list.top() == f.owner->bounds().bottom());
    CHECK(list.left() == f.owner->bounds().left());
    CHECK(drawn_lists(f.ui.window()) == 1);

    f.owner->hide();
    REQUIRE_FALSE(f.owner->is_open());
    f.ui.layout({400, 300});
    CHECK(drawn_lists(f.ui.window()) == 0);
}

TEST_CASE("Escape and a press outside call on_dismiss, and the owner decides", "[popup][open-popup]")
{
    Fixture f;
    f.owner->show();
    f.ui.layout({400, 300});

    // The owner keeps it open: the window only asks.
    f.owner->close_on_dismiss = false;
    seams(f.ui.window()).handle_key({Key::Escape, KeyAction::Down});
    REQUIRE(f.owner->dismissals == std::vector{PopupCloseReason::LightDismiss});
    CHECK(f.owner->is_open());

    // A press inside the popup is the popup's own.
    seams(f.ui.window()).handle_pointer(press(f.owner->list().bounds().center()));
    CHECK(f.owner->dismissals.size() == 1);

    f.owner->close_on_dismiss = true;
    seams(f.ui.window()).handle_pointer(press({390, 290}));
    CHECK(f.owner->dismissals.size() == 2);
    CHECK_FALSE(f.owner->is_open());
}

TEST_CASE("A popup without on_dismiss closes by itself on a light dismiss", "[popup][open-popup]")
{
    class Bare final : public Element
    {
    public:
        void show()
        {
            open_popup({.content = &list_});
        }
        [[nodiscard]] bool is_open() const
        {
            return popup_open();
        }

    protected:
        [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
        {
            return {10.0f, 10.0f};
        }
        void render_override(Renderer&, const RenderContext&) const override
        {
        }

    private:
        TextControl list_{"list"};
    };
    HeadlessUI ui   = create_headless();
    Bare*      bare = ui.window().set_content(std::make_unique<Bare>());
    ui.layout({200, 200});
    bare->show();
    ui.layout({200, 200});
    seams(ui.window()).handle_key({Key::Escape, KeyAction::Down});
    CHECK_FALSE(bare->is_open());
}

TEST_CASE("Destroying the owner closes its popup", "[popup][open-popup]")
{
    Fixture f;
    f.owner->show();
    f.ui.layout({400, 300});
    REQUIRE(drawn_lists(f.ui.window()) == 1);

    f.ui.window().set_content(std::make_unique<CanvasControl>()); // the owner goes with the old tree
    f.ui.layout({400, 300});
    CHECK(drawn_lists(f.ui.window()) == 0);
}

TEST_CASE("The popup hangs from the request's anchor element and the rect inside it", "[popup][open-popup]")
{
    Fixture    f;
    ListOwner* other = f.canvas->add(std::make_unique<ListOwner>(), 200.0f, 100.0f);
    f.ui.layout({400, 300});

    f.owner->anchor      = other;
    f.owner->anchor_rect = Rect{10.0f, 5.0f, 30.0f, 10.0f};
    f.owner->show();
    f.ui.layout({400, 300});
    CHECK(popup_anchor(*f.owner) == Rect{210.0f, 105.0f, 30.0f, 10.0f});
    CHECK(f.owner->list().bounds().top() == 115.0f);
    CHECK(f.owner->list().bounds().left() == 210.0f);
}

TEST_CASE("Sending the same request again asks for no layout", "[popup][open-popup]")
{
    Fixture f;
    f.owner->show();
    f.ui.layout({400, 300});
    clear_dirty(*f.owner);
    f.owner->show(); // the callbacks are new, everything else is the same
    CHECK_FALSE(f.owner->is_dirty(DirtyFlags::Layout));
    f.owner->anchor_rect = Rect{0.0f, 0.0f, 50.0f, 10.0f};
    f.owner->show();
    CHECK(f.owner->is_dirty(DirtyFlags::Layout));
}
