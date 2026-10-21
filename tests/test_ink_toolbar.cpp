// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/media/InkCanvas.hpp"
#include "winui/ui/media/InkToolbar.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

namespace
{
PointerEvent tool_down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent tool_up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}

// A toolbar over a canvas in a headless window.
struct InkWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    InkToolbarControl*   toolbar = nullptr;
    InkCanvasControl*    canvas  = nullptr;

    InkWindow()
    {
        auto stack = std::make_unique<StackControl>(Orientation::Vertical);
        toolbar    = stack->add(std::make_unique<InkToolbarControl>());
        canvas     = stack->add(std::make_unique<InkCanvasControl>());
        canvas->set_height(300.0f);
        toolbar->set_horizontal_alignment(Alignment::Leading);
        toolbar->set_target_ink_canvas(canvas);
        window.set_content(std::move(stack));
        relayout();
    }

    void relayout()
    {
        window.layout({600.0f, 500.0f}, measurer);
    }

    void click(InkToolbarTool tool)
    {
        const Point c = toolbar->tool_button(tool)->bounds().center();
        seams(window).handle_pointer(tool_down(c));
        seams(window).handle_pointer(tool_up(c));
        relayout();
    }
};

SliderControl* width_slider(InkToolbarToolButtonControl& pen)
{
    auto* flyout = dynamic_cast<FlyoutControl*>(pen.parent());
    auto* panel  = flyout != nullptr ? dynamic_cast<StackControl*>(flyout->content()) : nullptr;
    if (panel == nullptr)
    {
        return nullptr;
    }
    for (const auto& child : panel->children())
    {
        if (auto* slider = dynamic_cast<SliderControl*>(child.get()))
        {
            return slider;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("A new InkToolbarControl has the four tools with the ballpoint pen active", "[ink-toolbar]")
{
    InkToolbarControl toolbar;
    REQUIRE(toolbar.active_tool() == InkToolbarTool::BallpointPen);
    for (const InkToolbarTool tool : InkToolbarControl::AllTools)
    {
        REQUIRE(toolbar.tool_button(tool) != nullptr);
        REQUIRE(toolbar.tool_visible(tool));
    }
    REQUIRE(toolbar.tool_button(InkToolbarTool::BallpointPen)->checked());
    REQUIRE_FALSE(toolbar.tool_button(InkToolbarTool::Pencil)->checked());
    REQUIRE(toolbar.ink_drawing_attributes() == InkDrawingAttributes{});
    REQUIRE(toolbar.orientation() == Orientation::Horizontal);
    REQUIRE(toolbar.button_flyout_placement() == InkToolbarButtonFlyoutPlacement::Auto);
    REQUIRE(toolbar.target_ink_canvas() == nullptr);
    REQUIRE(toolbar.automation_role() == AutomationRole::ToolBar);
}

TEST_CASE("Each pen starts with its palette, width range and attributes", "[ink-toolbar]")
{
    InkToolbarControl toolbar;
    const auto*       pen = toolbar.tool_button(InkToolbarTool::BallpointPen);
    REQUIRE(pen->palette().size() == 30);
    REQUIRE(pen->selected_color() == colors::Black);
    REQUIRE(pen->min_stroke_width() == 1.0f);
    REQUIRE(pen->max_stroke_width() == 24.0f);
    REQUIRE(pen->selected_stroke_width() == 2.0f);

    const auto* pencil = toolbar.tool_button(InkToolbarTool::Pencil);
    REQUIRE(pencil->selected_stroke_width() == 4.0f);
    REQUIRE(pencil->drawing_attributes().kind == InkDrawingAttributesKind::Pencil);
    REQUIRE(pencil->drawing_attributes().size == Size{4.0f, 4.0f});

    const auto* marker = toolbar.tool_button(InkToolbarTool::Highlighter);
    REQUIRE(marker->palette().size() == 6);
    REQUIRE(marker->selected_color() == Color::from_rgba(255, 230, 0));
    REQUIRE(marker->min_stroke_width() == 12.0f);
    REQUIRE(marker->max_stroke_width() == 64.0f);
    REQUIRE(marker->selected_stroke_width() == 16.0f);
    const InkDrawingAttributes chisel = marker->drawing_attributes();
    REQUIRE(chisel.pen_tip == InkPenTip::Rectangle);
    REQUIRE(chisel.draw_as_highlighter);
    REQUIRE(chisel.size == Size{16.0f / 3.0f, 16.0f});

    const auto* eraser = toolbar.tool_button(InkToolbarTool::Eraser);
    REQUIRE_FALSE(eraser->is_pen());
    REQUIRE(eraser->clear_all_visible());
    REQUIRE(eraser->automation_role() == AutomationRole::Custom);
    REQUIRE(pen->automation_role() == AutomationRole::Button);
}

TEST_CASE("Selecting a tool sets the canvas up for it and raises the events", "[ink-toolbar]")
{
    InkCanvasControl  canvas;
    InkToolbarControl toolbar;
    toolbar.set_target_ink_canvas(&canvas);

    std::vector<InkToolbarTool>       tools;
    std::vector<InkDrawingAttributes> attrs;
    toolbar.on_active_tool_changed([&](InkToolbarTool t) { tools.push_back(t); });
    toolbar.on_ink_drawing_attributes_changed([&](const InkDrawingAttributes& a) { attrs.push_back(a); });

    toolbar.set_active_tool(InkToolbarTool::Highlighter);
    REQUIRE(toolbar.active_tool() == InkToolbarTool::Highlighter);
    REQUIRE(toolbar.tool_button(InkToolbarTool::Highlighter)->checked());
    REQUIRE_FALSE(toolbar.tool_button(InkToolbarTool::BallpointPen)->checked());
    REQUIRE(canvas.drawing_attributes().draw_as_highlighter);
    REQUIRE(canvas.input_mode() == InkCanvasInputMode::Inking);
    REQUIRE(tools == std::vector{InkToolbarTool::Highlighter});
    REQUIRE(attrs.size() == 1);

    toolbar.set_active_tool(InkToolbarTool::Highlighter); // already active: nothing
    REQUIRE(tools.size() == 1);

    toolbar.set_active_tool(InkToolbarTool::Eraser);
    REQUIRE(canvas.input_mode() == InkCanvasInputMode::Erasing);
    REQUIRE(attrs.size() == 1); // the eraser keeps the last pen's attributes
    REQUIRE(toolbar.ink_drawing_attributes().draw_as_highlighter);

    toolbar.set_active_tool(InkToolbarTool::BallpointPen);
    REQUIRE(canvas.input_mode() == InkCanvasInputMode::Inking);
    REQUIRE(canvas.drawing_attributes() == InkDrawingAttributes{});
    REQUIRE(tools.size() == 3);
    REQUIRE(attrs.size() == 2);

    // Detached, the toolbar leaves the canvas alone.
    toolbar.set_target_ink_canvas(nullptr);
    REQUIRE(toolbar.target_ink_canvas() == nullptr);
    toolbar.set_active_tool(InkToolbarTool::Eraser);
    REQUIRE(canvas.input_mode() == InkCanvasInputMode::Inking);
}

TEST_CASE("Changing the active pen's color or width updates the canvas; another pen's does not", "[ink-toolbar]")
{
    InkCanvasControl  canvas;
    InkToolbarControl toolbar;
    toolbar.set_target_ink_canvas(&canvas);
    int changes = 0;
    toolbar.on_ink_drawing_attributes_changed([&](const InkDrawingAttributes&) { ++changes; });

    auto* pen = toolbar.tool_button(InkToolbarTool::BallpointPen);
    pen->set_selected_brush_index(7);
    REQUIRE(canvas.drawing_attributes().color == Color::from_rgba(0xE6, 0x1B, 0x1B));
    pen->set_selected_stroke_width(100.0f); // clamped to the 24 maximum
    REQUIRE(pen->selected_stroke_width() == 24.0f);
    REQUIRE(canvas.drawing_attributes().size == Size{24.0f, 24.0f});
    REQUIRE(changes == 2);

    toolbar.tool_button(InkToolbarTool::Pencil)->set_selected_brush_index(3);
    REQUIRE(changes == 2);
    REQUIRE(canvas.drawing_attributes().color == Color::from_rgba(0xE6, 0x1B, 0x1B));

    pen->set_selected_brush_index(99); // clamped to the last color
    REQUIRE(pen->selected_brush_index() == 29);
}

TEST_CASE("Clicking a tool selects it, clicking it again opens its flyout", "[ink-toolbar]")
{
    InkWindow w;
    w.click(InkToolbarTool::Pencil);
    REQUIRE(w.toolbar->active_tool() == InkToolbarTool::Pencil);
    REQUIRE(w.canvas->drawing_attributes().kind == InkDrawingAttributesKind::Pencil);
    REQUIRE_FALSE(w.toolbar->open_flyout_tool().has_value());
    REQUIRE(w.toolbar->tool_button(InkToolbarTool::Pencil)->extension_glyph_shown());

    w.click(InkToolbarTool::Pencil);
    REQUIRE(w.toolbar->open_flyout_tool() == InkToolbarTool::Pencil);
    REQUIRE_FALSE(w.toolbar->tool_button(InkToolbarTool::Pencil)->extension_glyph_shown());
    REQUIRE(ui::automation_providers(*w.toolbar->tool_button(InkToolbarTool::Pencil)).expand_collapse->expanded_state());

    w.click(InkToolbarTool::Pencil);
    REQUIRE_FALSE(w.toolbar->open_flyout_tool().has_value());
}

TEST_CASE("The pen flyout's slider sets the pen's width", "[ink-toolbar]")
{
    InkWindow w;
    auto*     marker = w.toolbar->tool_button(InkToolbarTool::Highlighter);
    w.toolbar->set_active_tool(InkToolbarTool::Highlighter);
    REQUIRE(w.toolbar->open_flyout(InkToolbarTool::Highlighter));

    SliderControl* slider = width_slider(*marker);
    REQUIRE(slider != nullptr);
    REQUIRE(slider->min() == 12.0);
    REQUIRE(slider->max() == 64.0);
    REQUIRE(slider->value() == 16.0);

    slider->set_value(30.0);
    REQUIRE(marker->selected_stroke_width() == 30.0f);
    REQUIRE(w.canvas->drawing_attributes().size == Size{10.0f, 30.0f});

    // Opening again shows a width set from code meanwhile.
    w.toolbar->close_flyout();
    marker->set_selected_stroke_width(20.0f);
    REQUIRE(w.toolbar->open_flyout(InkToolbarTool::Highlighter));
    REQUIRE(slider->value() == 20.0);
}

TEST_CASE("Erase all clears the canvas; without it the eraser's flyout keeps its two erasers", "[ink-toolbar]")
{
    InkWindow w;
    w.canvas->strokes().add_stroke(InkStroke{.points = {{.position = {1, 1}}, {.position = {9, 9}}}});
    int erased = 0;
    w.toolbar->on_erase_all_clicked([&] { ++erased; });

    w.click(InkToolbarTool::Eraser);
    REQUIRE(w.canvas->input_mode() == InkCanvasInputMode::Erasing);
    REQUIRE(w.toolbar->open_flyout(InkToolbarTool::Eraser));
    w.toolbar->erase_all();
    REQUIRE(w.canvas->strokes().empty());
    REQUIRE(erased == 1);

    w.toolbar->close_flyout();
    auto* eraser = w.toolbar->tool_button(InkToolbarTool::Eraser);
    eraser->set_clear_all_visible(false);
    REQUIRE(w.toolbar->open_flyout(InkToolbarTool::Eraser));
    REQUIRE(w.toolbar->open_flyout_tool() == InkToolbarTool::Eraser);
}

TEST_CASE("The eraser's flyout switches the canvas between the stroke and the point eraser", "[ink-toolbar]")
{
    InkWindow w;
    w.click(InkToolbarTool::Eraser);
    REQUIRE(w.toolbar->open_flyout(InkToolbarTool::Eraser));

    // The flyout's items, in order: Stroke eraser, Point eraser, Erase all ink.
    auto* flyout = dynamic_cast<FlyoutControl*>(w.toolbar->tool_button(InkToolbarTool::Eraser)->parent());
    REQUIRE(flyout != nullptr);
    auto* panel = dynamic_cast<StackControl*>(flyout->content());
    REQUIRE(panel != nullptr);
    REQUIRE(panel->children().size() == 3);

    REQUIRE(ui::automation_providers(*panel->children()[1]).invoke->invoke()); // Point eraser
    CHECK(w.canvas->erase_mode() == InkCanvasEraseMode::Point);
    CHECK_FALSE(w.toolbar->open_flyout_tool().has_value()); // picking one closes the flyout

    REQUIRE(w.toolbar->open_flyout(InkToolbarTool::Eraser));
    REQUIRE(ui::automation_providers(*panel->children()[0]).invoke->invoke()); // Stroke eraser
    CHECK(w.canvas->erase_mode() == InkCanvasEraseMode::Stroke);
}

TEST_CASE("The toolbar is one tab stop; the arrows move along it and Enter acts", "[ink-toolbar]")
{
    InkCanvasControl  canvas;
    InkToolbarControl toolbar;
    toolbar.set_target_ink_canvas(&canvas);

    std::vector<Element*> ring;
    toolbar.collect_focusables(ring);
    REQUIRE(ring == std::vector<Element*>{&toolbar});

    ui::testing::set_focused(toolbar, true);
    REQUIRE(toolbar.focused_button() == toolbar.tool_button(InkToolbarTool::BallpointPen));
    REQUIRE_FALSE(toolbar.handle_key({Key::ArrowDown, KeyAction::Down})); // across the axis: left to focus
    REQUIRE(toolbar.focused_button() == toolbar.tool_button(InkToolbarTool::BallpointPen));

    REQUIRE(toolbar.handle_key({Key::ArrowRight, KeyAction::Down}));
    toolbar.handle_key({Key::ArrowRight, KeyAction::Down});
    REQUIRE(toolbar.focused_button() == toolbar.tool_button(InkToolbarTool::Highlighter));
    toolbar.handle_key({Key::End, KeyAction::Down});
    REQUIRE(toolbar.focused_button() == toolbar.tool_button(InkToolbarTool::Eraser));
    toolbar.handle_key({Key::ArrowRight, KeyAction::Down}); // stays at the end
    REQUIRE(toolbar.focused_button() == toolbar.tool_button(InkToolbarTool::Eraser));

    toolbar.handle_key({Key::Enter, KeyAction::Down});
    REQUIRE(toolbar.active_tool() == InkToolbarTool::BallpointPen); // acts on release
    toolbar.handle_key({Key::Enter, KeyAction::Up});
    REQUIRE(toolbar.active_tool() == InkToolbarTool::Eraser);
    REQUIRE(canvas.input_mode() == InkCanvasInputMode::Erasing);

    // A hidden tool is skipped.
    toolbar.set_tool_visible(InkToolbarTool::Pencil, false);
    toolbar.handle_key({Key::Home, KeyAction::Down});
    toolbar.handle_key({Key::ArrowRight, KeyAction::Down});
    REQUIRE(toolbar.focused_button() == toolbar.tool_button(InkToolbarTool::Highlighter));

    toolbar.set_orientation(Orientation::Vertical);
    REQUIRE(toolbar.handle_key({Key::ArrowDown, KeyAction::Down}));
}

TEST_CASE("The toolbar is 40 DIPs a tool inside a 2 DIP margin, and hidden tools take no room", "[ink-toolbar]")
{
    HeadlessTextMeasurer m;
    InkToolbarControl    toolbar;
    REQUIRE(toolbar.measure({1000.0f, 1000.0f}, m, Theme::default_light()) == Size{164.0f, 44.0f});

    toolbar.set_tool_visible(InkToolbarTool::Pencil, false);
    REQUIRE_FALSE(toolbar.tool_visible(InkToolbarTool::Pencil));
    REQUIRE(toolbar.measure({1000.0f, 1000.0f}, m, Theme::default_light()) == Size{124.0f, 44.0f});

    toolbar.set_orientation(Orientation::Vertical);
    REQUIRE(toolbar.measure({1000.0f, 1000.0f}, m, Theme::default_light()) == Size{44.0f, 124.0f});

    InkToolbarControl pens_only{
        std::vector{InkToolbarTool::Highlighter, InkToolbarTool::BallpointPen, InkToolbarTool::Highlighter}};
    REQUIRE(pens_only.tool_button(InkToolbarTool::Eraser) == nullptr);
    REQUIRE(pens_only.active_tool() == InkToolbarTool::Highlighter);
    REQUIRE(pens_only.measure({1000.0f, 1000.0f}, m, Theme::default_light()) == Size{84.0f, 44.0f});
}

TEST_CASE("A tool button draws its state, color glyph, glyph and chevron", "[ink-toolbar]")
{
    HeadlessTextMeasurer m;
    InkToolbarControl    toolbar;
    (void)toolbar.measure({1000.0f, 1000.0f}, m, Theme::default_light());
    toolbar.arrange({0.0f, 0.0f, 164.0f, 44.0f});

    auto* pen = toolbar.tool_button(InkToolbarTool::BallpointPen);
    REQUIRE(pen->bounds() == Rect{4.0f, 4.0f, 36.0f, 36.0f});

    TestRenderer r;
    pen->render(r);
    // background, the color glyph, the pen glyph and the chevron of the active tool
    REQUIRE(r.count() == 4);
    REQUIRE(std::get<FillRoundedRect>(r.commands[0]).color ==
            Theme::default_light().subtle_for_state(ButtonState::Hover).background);
    REQUIRE(std::get<DrawTextRun>(r.commands[1]).color == colors::Black);
    REQUIRE(std::get<DrawTextRun>(r.commands[3]).style.font_size == 10.0f);

    r.clear();
    toolbar.tool_button(InkToolbarTool::Eraser)->render(r);
    REQUIRE(r.count() == 2); // background and glyph: not active, no color
}

TEST_CASE("A tool button speaks its name and selection", "[ink-toolbar]")
{
    InkToolbarControl toolbar;
    REQUIRE(toolbar.tool_button(InkToolbarTool::BallpointPen)->automation_name() == "Ballpoint pen, selected");
    REQUIRE(toolbar.tool_button(InkToolbarTool::Eraser)->automation_name() == "Eraser");
    REQUIRE(toolbar.tool_button(InkToolbarTool::Pencil)->tool_tip() == "Pencil");

    REQUIRE(ui::automation_providers(*toolbar.tool_button(InkToolbarTool::Pencil)).selection_item->select_item());
    REQUIRE(toolbar.active_tool() == InkToolbarTool::Pencil);
    REQUIRE(ui::automation_providers(*toolbar.tool_button(InkToolbarTool::Pencil)).selection_item->item_selected());

    std::vector<Element*> children;
    toolbar.automation_children(children);
    REQUIRE(children.size() == 4);
}

TEST_CASE("A disabled toolbar takes no clicks and is no tab stop", "[ink-toolbar]")
{
    InkWindow w;
    w.toolbar->set_enabled(false);
    w.click(InkToolbarTool::Highlighter);
    REQUIRE(w.toolbar->active_tool() == InkToolbarTool::BallpointPen);
    REQUIRE_FALSE(w.toolbar->focusable());
}

TEST_CASE("A toolbar without a canvas keeps its state and hands it over when one is set later", "[ink-toolbar]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    auto                 stack   = std::make_unique<StackControl>(Orientation::Vertical);
    auto*                toolbar = stack->add(std::make_unique<InkToolbarControl>());
    auto*                canvas  = stack->add(std::make_unique<InkCanvasControl>());
    canvas->set_height(200.0f);
    window.set_content(std::move(stack));
    window.layout({600.0f, 400.0f}, measurer);

    // No target: the tools still work, and erase_all() has nothing to clear.
    REQUIRE(toolbar->target_ink_canvas() == nullptr);
    toolbar->set_active_tool(InkToolbarTool::Highlighter);
    toolbar->tool_button(InkToolbarTool::Highlighter)->set_selected_stroke_width(30.0f);
    toolbar->erase_all();
    REQUIRE(canvas->drawing_attributes() == InkDrawingAttributes{});

    // Set after the first layout: the canvas takes the active pen's attributes at once.
    toolbar->set_target_ink_canvas(canvas);
    REQUIRE(toolbar->target_ink_canvas() == canvas);
    REQUIRE(canvas->drawing_attributes() == toolbar->ink_drawing_attributes());
    REQUIRE(canvas->drawing_attributes().draw_as_highlighter);
    REQUIRE(canvas->input_mode() == InkCanvasInputMode::Inking);
    toolbar->set_active_tool(InkToolbarTool::Eraser);
    REQUIRE(canvas->input_mode() == InkCanvasInputMode::Erasing);
}

TEST_CASE("A toolbar lets go of a canvas destroyed before it", "[ink-toolbar]")
{
    InkToolbarControl toolbar;
    {
        InkCanvasControl canvas;
        toolbar.set_target_ink_canvas(&canvas);
        REQUIRE(toolbar.target_ink_canvas() == &canvas);
    }
    REQUIRE(toolbar.target_ink_canvas() == nullptr);
    toolbar.set_active_tool(InkToolbarTool::Pencil); // nothing to dangle into
    toolbar.erase_all();
    REQUIRE(toolbar.active_tool() == InkToolbarTool::Pencil);
}

TEST_CASE("ui::InkToolbar is pointed at its canvas through .control and binds the active tool both ways",
          "[ink-toolbar][spec]")
{
    HeadlessTextMeasurer     measurer;
    Window                   window;
    Control<InkCanvas>       canvas;
    Control<InkToolbar>      toolbar;
    Property<InkToolbarTool> tool{InkToolbarTool::Eraser};
    int                      changes = 0;

    window.set_content(VerticalStack{
        .content = {
            InkToolbar{
                .active_tool            = bind(tool),
                .horizontal_alignment   = Alignment::Leading,
                .on_active_tool_changed = [&](InkToolbarTool) { ++changes; },
                .control                = toolbar,
            },
            InkCanvas{.height = 200.0f, .control = canvas},
        }});
    REQUIRE(toolbar->active_tool() == InkToolbarTool::Eraser);
    REQUIRE(changes == 0);
    REQUIRE(toolbar->target_ink_canvas() == nullptr);

    toolbar->set_target_ink_canvas(canvas);
    window.layout({600.0f, 400.0f}, measurer);
    REQUIRE(toolbar->target_ink_canvas() == canvas.get());
    REQUIRE(canvas->input_mode() == InkCanvasInputMode::Erasing);

    tool = InkToolbarTool::Highlighter;
    REQUIRE(toolbar->active_tool() == InkToolbarTool::Highlighter);
    REQUIRE(canvas->drawing_attributes().draw_as_highlighter);

    const Point c = toolbar->tool_button(InkToolbarTool::Pencil)->bounds().center();
    seams(window).handle_pointer(tool_down(c));
    seams(window).handle_pointer(tool_up(c));
    REQUIRE(tool.get() == InkToolbarTool::Pencil);
    REQUIRE(changes == 1); // the click; the bound write to Highlighter isn't reported
}

TEST_CASE("The eraser's flyout is as wide as its items, not as its button", "[ink-toolbar][popup]")
{
    InkWindow w;
    w.click(InkToolbarTool::Eraser);
    REQUIRE(w.toolbar->open_flyout(InkToolbarTool::Eraser));
    w.relayout();

    auto* eraser = w.toolbar->tool_button(InkToolbarTool::Eraser);
    auto* flyout = dynamic_cast<FlyoutControl*>(eraser->parent());
    REQUIRE(flyout != nullptr);
    auto* panel = dynamic_cast<StackControl*>(flyout->content());
    REQUIRE(panel != nullptr);
    CHECK(ui::testing::popup_content(*flyout)->bounds().width > eraser->bounds().width);
    for (const auto& item : panel->children())
    {
        CHECK(item->bounds().right() <= panel->bounds().right() + 0.5f);
    }
}

TEST_CASE("A custom pen draws with the attributes it makes from the picked color and width", "[ink-toolbar][custom]")
{
    InkWindow                    w;
    InkToolbarToolButtonControl* pen = w.toolbar->add_custom_pen(
        {.name = "Calligraphy pen",
         .icon = IconGlyph{0xEDFB},
         .pen  = [](Color color, float width)
         { return InkDrawingAttributes{.color = color, .size = {width / 3.0f, width}, .pen_tip = InkPenTip::Rectangle}; }});
    w.relayout();
    REQUIRE(pen->tool() == InkToolbarTool::CustomPen);
    REQUIRE(pen->is_pen());
    REQUIRE(pen->tool_tip() == "Calligraphy pen");
    REQUIRE(pen->bounds().left() > w.toolbar->tool_button(InkToolbarTool::Eraser)->bounds().right()); // after the tools

    const Point c = pen->bounds().center();
    seams(w.window).handle_pointer(tool_down(c));
    seams(w.window).handle_pointer(tool_up(c));
    w.relayout();
    REQUIRE(w.toolbar->active_tool() == InkToolbarTool::CustomPen);
    REQUIRE(w.toolbar->active_tool_button() == pen);
    REQUIRE(w.canvas->input_mode() == InkCanvasInputMode::Inking);
    REQUIRE(w.canvas->drawing_attributes().pen_tip == InkPenTip::Rectangle);
    REQUIRE(w.canvas->drawing_attributes().size == Size{2.0f / 3.0f, 2.0f});

    // Its flyout is the pens' color and size flyout, and a new width reaches the canvas.
    REQUIRE(width_slider(*pen) != nullptr);
    pen->set_selected_stroke_width(12.0f);
    REQUIRE(w.canvas->drawing_attributes().size == Size{4.0f, 12.0f});
}

TEST_CASE("A custom tool turns inking off so the app handles the canvas's input", "[ink-toolbar][custom]")
{
    InkWindow                    w;
    InkToolbarToolButtonControl* lasso = w.toolbar->add_custom_tool({.name = "Lasso", .icon = IconGlyph{0xEF20}});
    w.relayout();
    REQUIRE(lasso->tool() == InkToolbarTool::CustomTool);
    REQUIRE_FALSE(lasso->is_pen());
    REQUIRE(dynamic_cast<FlyoutControl*>(lasso->parent()) == nullptr); // nothing to configure

    w.toolbar->set_active_tool_button(lasso);
    REQUIRE(w.toolbar->active_tool() == InkToolbarTool::CustomTool);
    REQUIRE(w.canvas->input_mode() == InkCanvasInputMode::None);
    REQUIRE(lasso->automation_name() == "Lasso, selected");

    w.click(InkToolbarTool::BallpointPen);
    REQUIRE(w.canvas->input_mode() == InkCanvasInputMode::Inking);

    // With configuration content, a click on the active tool opens it.
    InkToolbarToolButtonControl* stamp = w.toolbar->add_custom_tool(
        {.name = "Stamp", .icon = IconGlyph{0xE734}, .configuration = [] { return Spec{Text{.text = "Stamp options"}}; }});
    w.relayout();
    w.toolbar->set_active_tool_button(stamp);
    REQUIRE(ui::automation_providers(*stamp).expand_collapse->set_expanded_state(true));
    REQUIRE(ui::automation_providers(*stamp).expand_collapse->expanded_state());
}

TEST_CASE("A custom toggle sits after the tools and flips on a click or the keyboard", "[ink-toolbar][custom]")
{
    InkWindow                      w;
    std::vector<bool>              changes;
    InkToolbarToggleButtonControl* grid = w.toolbar->add_custom_toggle(
        {.name = "Grid", .icon = IconGlyph{0xE80A}, .on_checked_changed = [&](bool on) { changes.push_back(on); }});
    w.relayout();
    REQUIRE(w.toolbar->toggle_buttons() == std::vector<InkToolbarToggleButtonControl*>{grid});
    REQUIRE(grid->bounds().left() > w.toolbar->tool_button(InkToolbarTool::Eraser)->bounds().right());

    const Point c = grid->bounds().center();
    seams(w.window).handle_pointer(tool_down(c));
    seams(w.window).handle_pointer(tool_up(c));
    REQUIRE(grid->checked());
    REQUIRE(w.toolbar->active_tool() == InkToolbarTool::BallpointPen); // a toggle is not a tool
    REQUIRE(ui::automation_providers(*grid).toggle->toggle_value() == ToggleState::On);

    // End puts the keyboard cursor on the toggle, and Space flips it back.
    ui::testing::set_focused(*w.toolbar, true);
    w.toolbar->handle_key(KeyEvent{Key::End, KeyAction::Down});
    REQUIRE(w.toolbar->focused_button() == nullptr);
    w.toolbar->handle_key(KeyEvent{Key::Space, KeyAction::Up});
    REQUIRE_FALSE(grid->checked());
    REQUIRE(changes == std::vector<bool>{true, false});

    std::vector<Element*> children;
    w.toolbar->automation_children(children);
    REQUIRE(children.back() == grid);
}

TEST_CASE("ui::InkToolbar builds its custom pens, tools and toggles in order", "[ink-toolbar][custom][spec]")
{
    Control<InkToolbar>      toolbar;
    std::unique_ptr<Element> built = build(InkToolbar{
        .tools          = {InkToolbarTool::BallpointPen, InkToolbarTool::Eraser},
        .custom_pens    = {{.name = "Marker", .icon = IconGlyph{0xE76D}}},
        .custom_tools   = {{.name = "Lasso", .icon = IconGlyph{0xEF20}}},
        .custom_toggles = {{.name = "Grid", .icon = IconGlyph{0xE80A}}},
        .control        = toolbar,
    });
    std::vector<Element*>    children;
    toolbar->automation_children(children);
    REQUIRE(children.size() == 5);
    REQUIRE(children[3]->automation_name() == "Lasso"); // a custom tool with nothing to configure has no flyout
    REQUIRE(children[4] == toolbar->toggle_buttons().front());
    toolbar->set_active_tool(InkToolbarTool::CustomPen);
    REQUIRE(toolbar->active_tool_button()->tool_tip() == "Marker");
}

TEST_CASE("The stencil button shows the ruler, and its flyout switches to the protractor or hides it",
          "[ink-toolbar][stencil]")
{
    InkWindow                                           w;
    std::vector<std::pair<bool, InkToolbarStencilKind>> changes;
    w.toolbar->on_stencil_button_checked_changed(
        [&](const InkToolbarStencilChangedArgs& args) { changes.emplace_back(args.checked, args.kind); });
    InkToolbarStencilButtonControl* stencils = w.toolbar->add_stencil_button();
    REQUIRE(w.toolbar->add_stencil_button() == stencils); // one only
    w.relayout();
    REQUIRE(stencils->bounds().left() > w.toolbar->tool_button(InkToolbarTool::Eraser)->bounds().right());
    REQUIRE_FALSE(w.canvas->ruler().visible);

    const auto click = [&]
    {
        const Point c = stencils->bounds().center();
        seams(w.window).handle_pointer(tool_down(c));
        seams(w.window).handle_pointer(tool_up(c));
        w.relayout();
    };

    // Off: a click shows the ruler; the tools stay as they were.
    click();
    REQUIRE(w.toolbar->stencil_button_checked());
    REQUIRE(w.canvas->ruler().visible);
    REQUIRE_FALSE(w.canvas->protractor().visible);
    REQUIRE(w.toolbar->active_tool() == InkToolbarTool::BallpointPen);
    REQUIRE(stencils->extension_glyph_shown());

    // On: a click opens the flyout, whose Protractor item swaps the stencils.
    click();
    REQUIRE(ui::automation_providers(*stencils).expand_collapse->expanded_state());
    auto* flyout = dynamic_cast<FlyoutControl*>(stencils->parent());
    auto* items  = flyout != nullptr ? dynamic_cast<StackControl*>(flyout->content()) : nullptr;
    REQUIRE(items != nullptr);
    REQUIRE(items->children().size() == 2);
    Element* protractor = items->children()[1].get();
    REQUIRE(protractor->automation_name().starts_with("Protractor"));
    REQUIRE(ui::automation_providers(*protractor).invoke->invoke());
    REQUIRE(stencils->selected_stencil() == InkToolbarStencilKind::Protractor);
    REQUIRE_FALSE(w.canvas->ruler().visible);
    REQUIRE(w.canvas->protractor().visible);
    REQUIRE_FALSE(ui::automation_providers(*stencils).expand_collapse->expanded_state());

    // Picking the one showing hides it and turns the button off.
    REQUIRE(ui::automation_providers(*stencils).expand_collapse->set_expanded_state(true));
    REQUIRE(ui::automation_providers(*protractor).invoke->invoke());
    REQUIRE_FALSE(w.toolbar->stencil_button_checked());
    REQUIRE_FALSE(w.canvas->protractor().visible);
    REQUIRE(changes.size() == 2); // on and off; switching stencils is neither
    REQUIRE(changes.back() == std::pair{false, InkToolbarStencilKind::Protractor});
}

TEST_CASE("With one stencil on offer the stencil button is a plain toggle", "[ink-toolbar][stencil]")
{
    InkWindow                       w;
    InkToolbarStencilButtonControl* stencils = w.toolbar->add_stencil_button();
    stencils->set_ruler_item_visible(false);
    w.relayout();
    REQUIRE(stencils->selected_stencil() == InkToolbarStencilKind::Protractor);
    REQUIRE(ui::automation_providers(*stencils).toggle->toggle_next());
    REQUIRE(w.canvas->protractor().visible);
    REQUIRE_FALSE(stencils->extension_glyph_shown());
    REQUIRE_FALSE(ui::automation_providers(*stencils).expand_collapse->set_expanded_state(true));
    REQUIRE(ui::automation_providers(*stencils).toggle->toggle_next());
    REQUIRE_FALSE(w.canvas->protractor().visible);

    // The keyboard reaches it after the tools.
    ui::testing::set_focused(*w.toolbar, true);
    w.toolbar->handle_key(KeyEvent{Key::End, KeyAction::Down});
    w.toolbar->handle_key(KeyEvent{Key::Space, KeyAction::Up});
    REQUIRE(w.toolbar->stencil_button_checked());
}
