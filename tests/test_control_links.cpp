// SPDX-License-Identifier: MIT
// Links between controls made through their handles (ControlLink, ElementLink).
#include "winui/ui/Element.hpp"
#include "winui/ui/dialogs/TeachingTip.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/media/InkCanvas.hpp"
#include "winui/ui/media/InkToolbar.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/RichText.hpp"

#include <memory>

#include <catch2/catch_test_macros.hpp>

using namespace ui;

TEST_CASE("An InkToolbar links its canvas in either build order", "[link]")
{
    SECTION("canvas built after the toolbar")
    {
        ui::Control<ui::InkCanvas>  canvas;
        ui::Control<ui::InkToolbar> toolbar;
        std::unique_ptr<Element>    tree = ui::build(ui::VerticalStack{
            .content = {
                ui::InkToolbar{.target = canvas, .control = toolbar},
                ui::InkCanvas{.control = canvas},
            }});
        CHECK(toolbar->target_ink_canvas() == canvas.get());
    }
    SECTION("canvas built before the toolbar")
    {
        ui::Control<ui::InkCanvas>  canvas;
        ui::Control<ui::InkToolbar> toolbar;
        std::unique_ptr<Element>    tree = ui::build(ui::VerticalStack{
            .content = {
                ui::InkCanvas{.control = canvas},
                ui::InkToolbar{.target = canvas, .control = toolbar},
            }});
        CHECK(toolbar->target_ink_canvas() == canvas.get());
    }
    SECTION("a destroyed canvas is let go of, and a new one is linked")
    {
        ui::Control<ui::InkCanvas>  canvas;
        ui::Control<ui::InkToolbar> toolbar;
        std::unique_ptr<Element>    bar = ui::build(ui::InkToolbar{.target = canvas, .control = toolbar});
        std::unique_ptr<Element>    one = ui::build(ui::InkCanvas{.control = canvas});
        REQUIRE(toolbar->target_ink_canvas() == canvas.get());
        one.reset();
        CHECK(toolbar->target_ink_canvas() == nullptr);
        std::unique_ptr<Element> two = ui::build(ui::InkCanvas{.control = canvas});
        CHECK(toolbar->target_ink_canvas() == two.get());
    }
    SECTION("a toolbar destroyed before its canvas is built is not called")
    {
        ui::Control<ui::InkCanvas> canvas;
        std::unique_ptr<Element>   bar = ui::build(ui::InkToolbar{.target = canvas});
        bar.reset();
        std::unique_ptr<Element> one = ui::build(ui::InkCanvas{.control = canvas});
        CHECK(canvas.get() == one.get());
    }
}

TEST_CASE("A RichText links its overflow through a handle", "[link]")
{
    ui::Control<ui::RichText>         text;
    ui::Control<ui::RichTextOverflow> more;
    ui::Control<ui::RichTextOverflow> rest;
    std::unique_ptr<Element>          tree = ui::build(ui::HorizontalStack{
        .content = {
            ui::RichText{.overflow = more, .control = text},
            ui::RichTextOverflow{.overflow = rest, .control = more},
            ui::RichTextOverflow{.control = rest},
        }});
    CHECK(text->overflow_target() == more.get());
    CHECK(more->overflow_target() == rest.get());
    CHECK(rest->content_source() == text.get());
}

TEST_CASE("A TeachingTip points at any control's handle, built before or after it", "[link]")
{
    ui::Control<ui::Button>      before;
    ui::Control<ui::Button>      after;
    ui::Control<ui::TeachingTip> first;
    ui::Control<ui::TeachingTip> second;
    std::unique_ptr<Element>     tree = ui::build(ui::VerticalStack{
        .content = {
            ui::Button{.text = "Before", .control = before},
            ui::TeachingTip{.title = "One", .target = before, .control = first},
            ui::TeachingTip{.title = "Two", .target = after, .control = second},
            ui::Button{.text = "After", .control = after},
        }});
    CHECK(first->target() == before.get());
    CHECK(second->target() == after.get());
}
