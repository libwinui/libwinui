// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Describe.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/collections/ForEach.hpp"
#include "winui/ui/collections/ListView.hpp"
#include "winui/ui/collections/ListViewBase.hpp"
#include "winui/ui/collections/TableView.hpp"
#include "winui/ui/collections/TileView.hpp"
#include "winui/ui/collections/TreeView.hpp"
#include "winui/ui/containers/Border.hpp"
#include "winui/ui/containers/Expander.hpp"
#include "winui/ui/containers/ScaleToFit.hpp"
#include "winui/ui/dialogs/Dialog.hpp"
#include "winui/ui/dialogs/Popup.hpp"
#include "winui/ui/dialogs/TeachingTip.hpp"
#include "winui/ui/dialogs/ToolTip.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/input/ColorPicker.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/input/DropdownButton.hpp"
#include "winui/ui/input/RadioButtons.hpp"
#include "winui/ui/input/Rating.hpp"
#include "winui/ui/input/Slider.hpp"
#include "winui/ui/input/SplitButton.hpp"
#include "winui/ui/input/ToggleButton.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/layouts/Layout.hpp"
#include "winui/ui/layouts/LinedFlowLayout.hpp"
#include "winui/ui/layouts/StackLayout.hpp"
#include "winui/ui/layouts/TileLayout.hpp"
#include "winui/ui/layouts/WrapLayout.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/menus/AppBarButton.hpp"
#include "winui/ui/menus/MenuFlyout.hpp"
#include "winui/ui/navigation/Breadcrumb.hpp"
#include "winui/ui/navigation/NavigationFrame.hpp"
#include "winui/ui/navigation/NavigationShell.hpp"
#include "winui/ui/navigation/SelectorBar.hpp"
#include "winui/ui/navigation/Tabs.hpp"
#include "winui/ui/panels/Canvas.hpp"
#include "winui/ui/panels/Fill.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/panels/LayoutPanel.hpp"
#include "winui/ui/panels/RelativePanel.hpp"
#include "winui/ui/panels/Spacer.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/panels/TilePanel.hpp"
#include "winui/ui/panels/WrapPanel.hpp"
#include "winui/ui/panes/SidePane.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/spec/Common.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Dynamic.hpp"
#include "winui/ui/spec/LayoutSpec.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/status/InfoBadge.hpp"
#include "winui/ui/status/InfoBar.hpp"
#include "winui/ui/status/ProgressBar.hpp"
#include "winui/ui/status/ProgressRing.hpp"
#include "winui/ui/text/Inlines.hpp"
#include "winui/ui/text/NumberBox.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace std::chrono_literals;
using namespace ui;
using namespace ui::testing;

namespace
{
// Render a tree through the deterministic headless measurer and dump the command stream.
std::string describe_tree(Element& root, Size client = {400.0f, 300.0f})
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();
    (void)root.measure(client, measurer, theme);
    root.arrange(Rect{0.0f, 0.0f, client.width, client.height});

    TestRenderer renderer;
    root.render(renderer, theme);
    return describe(renderer.commands);
}
} // namespace

TEST_CASE("a spec-built tree renders identically to the hand-built one", "[spec]")
{
    // Hand-built.
    std::unique_ptr<Element> manual;
    {
        auto stack = std::make_unique<StackControl>(Orientation::Vertical);
        stack->set_spacing(12.0f);
        stack->add(std::make_unique<TextControl>("Clicks: 0"));
        stack->add(std::make_unique<ButtonControl>("Click me"));
        stack->add(std::make_unique<ButtonControl>("Reset"));
        manual = std::move(stack);
    }

    // Declared.
    std::unique_ptr<Element> declared = ui::build(ui::VerticalStack{
        .spacing = 12.0f,
        .content =
            {
                ui::Text{.text = "Clicks: 0"},
                ui::Button{.text = "Click me"},
                ui::Button{.text = "Reset"},
            },
    });

    REQUIRE(describe_tree(*declared) == describe_tree(*manual));
}

TEST_CASE("children are owned by their parent, with no shared ownership anywhere", "[spec]")
{
    ui::Control<ui::VerticalStack> panel;

    std::unique_ptr<Element> tree = ui::build(ui::VerticalStack{
        .control = panel,
        .content = {ui::Text{.text = "a"}, ui::Text{.text = "b"}, ui::Text{.text = "c"}},
    });

    REQUIRE(panel != nullptr);
    REQUIRE(panel->children().size() == 3);
    // Every child's parent link is wired the same way add() wires it, so dirty flags bubble.
    for (const auto& child : panel->children())
    {
        REQUIRE(child->parent() == panel);
    }
}

TEST_CASE("a Control handle reaches the typed control its spec built", "[spec]")
{
    ui::Control<ui::Text>   heading;
    ui::Control<ui::Button> reset;

    std::unique_ptr<Element> tree = ui::build(ui::VerticalStack{
        .content =
            {
                ui::Text{.text = "Clicks: 0", .control = heading},
                ui::Button{.text = "Reset", .control = reset},
            },
    });

    REQUIRE(heading != nullptr);
    REQUIRE(reset != nullptr);
    REQUIRE(heading->text() == "Clicks: 0");

    // The observer contract every container already provides: mutate the live control.
    reset->set_enabled(false);
    REQUIRE_FALSE(reset->enabled());
}

TEST_CASE("a Control handle is empty until build, optional through a pointer, and typed by its spec", "[spec]")
{
    static_assert(std::is_same_v<ui::Control<ui::Button>::Type, ui::ButtonControl>);
    static_assert(std::is_same_v<ui::Control<ui::VerticalStack>::Type, ui::StackControl>);
    static_assert(!std::is_constructible_v<ui::ControlOut<ui::Button>, ui::Control<ui::Text>&>);

    ui::Control<ui::Button> first;
    ui::Control<ui::Button> skipped;
    REQUIRE_FALSE(first);
    REQUIRE(first.get() == nullptr);

    const auto button = [&](bool wanted, ui::Control<ui::Button>& handle)
    { return ui::Button{.text = "OK", .control = wanted ? &handle : nullptr}; };
    std::unique_ptr<Element> tree = ui::build(ui::VerticalStack{.content = {button(true, first), button(false, skipped)}});

    REQUIRE(first);
    REQUIRE_FALSE(skipped);

    // The handle converts to the control's pointer where an API takes one.
    ui::ButtonControl* raw  = first;
    Element*           base = first;
    REQUIRE(raw == first.get());
    REQUIRE(base == raw);
    REQUIRE(&*first == raw);

    first = nullptr;
    REQUIRE_FALSE(first);
}

TEST_CASE("a spec's appearance fields follow a bound property", "[spec][binding]")
{
    Property<Alignment>                alignment{Alignment::Leading};
    Property<int>                      order{0};
    Property<std::optional<TextStyle>> style{std::nullopt};
    Control<VerticalStack>             stack;
    Control<AppBarButton>              button;
    Control<Text>                      text;

    auto stack_node  = VerticalStack{.child_alignment = bind(alignment), .control = stack}.build();
    auto button_node = AppBarButton{.dynamic_overflow_order = bind(order), .control = button}.build();
    auto text_node   = Text{.text = "Hi", .text_style = bind(style), .control = text}.build();
    CHECK(stack->child_alignment() == Alignment::Leading);
    CHECK(button->dynamic_overflow_order() == 0);

    alignment = Alignment::Center;
    order     = 3;
    style     = TextStyle{.font_size = 20.0f};
    CHECK(stack->child_alignment() == Alignment::Center);
    CHECK(button->dynamic_overflow_order() == 3);
    CHECK(text->text_style().font_size == 20.0f);
}

TEST_CASE("a color field takes a color, a theme resource or a bound optional color", "[spec][binding]")
{
    Property<std::optional<Color>> tint{Color{1.0f, 0.0f, 0.0f, 1.0f}};
    Control<Text>                  fixed;
    Control<Text>                  themed;
    Control<Text>                  bound;
    auto a = Text{.text = "a", .foreground = Color{0.0f, 0.0f, 1.0f, 1.0f}, .control = fixed}.build();
    auto b = Text{.text = "b", .foreground = [](const Theme& t) { return t.colors().text; }, .control = themed}.build();
    auto c = Text{.text = "c", .foreground = bind(tint), .control = bound}.build();

    CHECK(fixed->color() == Color{0.0f, 0.0f, 1.0f, 1.0f});
    CHECK(bound->color() == Color{1.0f, 0.0f, 0.0f, 1.0f});
    tint = Color{0.0f, 1.0f, 0.0f, 1.0f};
    CHECK(bound->color() == Color{0.0f, 1.0f, 0.0f, 1.0f});

    TestRenderer renderer;
    themed->render(renderer, Theme::default_light());
    CHECK(std::get<DrawTextRun>(renderer.commands.front()).color == Theme::default_light().colors().text);
}

TEST_CASE("an icon field follows a bound IconSource", "[spec][binding]")
{
    Property<IconSource>  icon{IconSource{glyphs::Accept}};
    Control<AppBarButton> button;
    auto                  node = AppBarButton{.text = "Save", .icon = bind(icon), .control = button}.build();
    CHECK(button->icon() == IconSource{glyphs::Accept});
    icon = IconSource{glyphs::Cancel};
    CHECK(button->icon() == IconSource{glyphs::Cancel});
    CHECK_FALSE(button->icon() == IconSource{glyphs::Accept});
}

TEST_CASE("bind() of a property is two-way on an editable field", "[spec][binding]")
{
    Property<float>               volume{20.0f};
    Property<std::string>         name{"Ada"};
    Property<bool>                muted{false};
    ui::Control<ui::Slider>       slider;
    ui::Control<ui::TextBox>      box;
    ui::Control<ui::ToggleSwitch> toggle;
    int                           handler_calls   = 0;
    float                         seen_by_handler = -1.0f;

    std::unique_ptr<Element> tree = ui::build(ui::VerticalStack{
        .content =
            {
                ui::Slider{.value = ui::bind(volume),
                           .on_value_changed =
                               [&](double v)
                           {
                               ++handler_calls;
                               seen_by_handler = volume.get(); // the property is written before the handler runs
                               (void)v;
                           },
                           .control = slider},
                ui::TextBox{.text = ui::bind(name), .control = box},
                ui::ToggleSwitch{.checked = ui::bind(muted), .control = toggle},
            },
    });

    // The control follows the property...
    volume = 35.0f;
    REQUIRE(slider->value() == 35.0f);
    // ...and what the user enters is written back, with no handler written for it.
    slider->set_value(60.0f);
    CHECK(volume.get() == 60.0f);
    CHECK(seen_by_handler == 60.0f);
    ui::testing::set_focused(*box, true);
    box->handle_text("!"); // typed at the caret, which starts at the front
    CHECK(name.get() == box->text());
    CHECK(name.get() != "Ada");
    toggle->set_checked(true);
    CHECK(muted.get());

    // The round trip ends at the equality gate: one change, one handler call.
    const int before = handler_calls;
    slider->set_value(80.0f);
    CHECK(handler_calls == before + 1);
}

TEST_CASE("a binding stays one-way with a conversion, a const property or bind_one_way()", "[spec][binding]")
{
    Property<float>         level{20.0f};
    const Property<float>&  read_only = level;
    ui::Control<ui::Slider> converted;
    ui::Control<ui::Slider> constant;
    ui::Control<ui::Slider> opted_out;

    static_assert(!std::is_constructible_v<Binding<float>, Property<float>&&>);
    CHECK(ui::bind(level).writable());
    CHECK_FALSE(ui::bind(read_only).writable());
    CHECK_FALSE(ui::bind_one_way(level).writable());
    CHECK_FALSE(ui::bind(level, [](float v) { return v * 2.0f; }).writable());

    std::unique_ptr<Element> tree = ui::build(ui::VerticalStack{
        .content =
            {
                ui::Slider{.value = ui::bind(level, [](float v) { return v; }), .control = converted},
                ui::Slider{.value = ui::bind(read_only), .control = constant},
                ui::Slider{.value = ui::bind_one_way(level), .control = opted_out},
            },
    });

    converted->set_value(70.0f);
    constant->set_value(71.0f);
    opted_out->set_value(72.0f);
    CHECK(level.get() == 20.0f);

    // Still bound the other way.
    level = 40.0f;
    CHECK(opted_out->value() == 40.0f);
}

TEST_CASE("an open/closed field bound two-way follows the control", "[spec][binding]")
{
    Property<bool>           open{true};
    ui::Control<ui::InfoBar> bar;
    int                      closed = 0;

    std::unique_ptr<Element> tree = ui::build(ui::InfoBar{
        .title     = "Saved",
        .is_open   = ui::bind(open),
        .on_closed = [&](PopupCloseReason) { ++closed; },
        .control   = bar,
    });

    bar->set_open(false); // what the close button does
    CHECK_FALSE(open.get());
    CHECK(closed == 1);

    open = true;
    CHECK(bar->is_open());
}

TEST_CASE("a close canceled by on_closing puts a two-way is_open back", "[spec][binding]")
{
    bool cancel_close = true;

    Property<bool>           bar_open{true};
    ui::Control<ui::InfoBar> bar;
    std::unique_ptr<Element> bar_tree = ui::build(ui::InfoBar{
        .title      = "Saved",
        .is_open    = ui::bind(bar_open),
        .on_closing = [&] { return cancel_close ? ui::Cancel : ui::Proceed; },
        .control    = bar,
    });
    REQUIRE(bar->is_open());
    bar_open = false;
    CHECK(bar->is_open());
    CHECK(bar_open.get());
    cancel_close = false;
    bar_open     = false;
    CHECK_FALSE(bar->is_open());
    CHECK_FALSE(bar_open.get());

    cancel_close = true;
    Property<bool>               tip_open{true};
    ui::Control<ui::TeachingTip> tip;
    std::unique_ptr<Element>     tip_tree = ui::build(ui::TeachingTip{
        .title      = "Tip",
        .is_open    = ui::bind(tip_open),
        .on_closing = [&] { return cancel_close ? ui::Cancel : ui::Proceed; },
        .control    = tip,
    });
    REQUIRE(tip->is_open());
    tip_open = false;
    CHECK(tip->is_open());
    CHECK(tip_open.get());
    cancel_close = false;
    tip_open     = false;
    CHECK_FALSE(tip->is_open());
    CHECK_FALSE(tip_open.get());
}

TEST_CASE("one Spec type is a component's result, a content field and a child-list entry", "[spec]")
{
    const auto component = [](std::string text) -> ui::Spec { return ui::Text{.text = std::move(text)}; };

    ui::Spec empty;
    REQUIRE_FALSE(empty);

    ui::Control<ui::VerticalStack> panel;
    std::unique_ptr<Element>       tree = ui::build(ui::VerticalStack{
        .control = panel,
        .content =
            {
                component("from a function"),
                ui::Button{.content = component("as content")},
                std::make_unique<ui::TextControl>("hand-built"),
            },
    });

    REQUIRE(panel->children().size() == 3);
    REQUIRE(ui::build(component("alone")) != nullptr);
    const std::unique_ptr<Element> nothing = ui::build(std::move(empty));
    REQUIRE(nothing == nullptr);
}

TEST_CASE("a container's content takes one spec without braces, a braced list or ui::content()", "[spec]")
{
    const auto component = [](std::string text) -> ui::Spec { return ui::Text{.text = std::move(text)}; };

    ui::Control<ui::VerticalStack> single;
    auto a = ui::build(ui::VerticalStack{.control = single, .content = ui::Text{.text = "only"}});
    REQUIRE(single->children().size() == 1);

    ui::Control<ui::HorizontalStack> from_component;
    auto b = ui::build(ui::HorizontalStack{.control = from_component, .content = component("from a function")});
    REQUIRE(from_component->children().size() == 1);

    ui::Control<ui::VerticalStack> hand_built;
    auto c = ui::build(ui::VerticalStack{.control = hand_built, .content = std::make_unique<ui::TextControl>("hand-built")});
    REQUIRE(hand_built->children().size() == 1);

    ui::Control<ui::VerticalStack> nothing;
    auto                           d = ui::build(ui::VerticalStack{.control = nothing, .content = ui::Spec{}});
    REQUIRE(nothing->children().empty());

    ui::Control<ui::VerticalStack> braced;
    auto e = ui::build(ui::VerticalStack{.control = braced, .content = {ui::Text{.text = "one"}}});
    REQUIRE(braced->children().size() == 1);

    ui::Control<ui::VerticalStack> variadic;
    auto                           f = ui::build(
        ui::VerticalStack{.control = variadic, .content = ui::content(ui::Text{.text = "a"}, ui::Text{.text = "b"})});
    REQUIRE(variadic->children().size() == 2);
}

TEST_CASE("a loop appends specs to a SpecList and to a panel", "[spec]")
{
    ui::SpecList rows;
    for (const char* name : {"a", "b", "c"})
    {
        rows.push_back(ui::Text{.text = name});
    }
    rows.push_back(ui::Spec{}); // an empty Spec adds nothing
    REQUIRE(rows.size() == 3);

    ui::Control<ui::VerticalStack> stack;
    auto                           root = ui::build(ui::VerticalStack{.control = stack, .content = std::move(rows)});
    REQUIRE(stack->children().size() == 3);

    ui::Control<ui::Text> added;
    Element*              node = stack->add(ui::Text{.text = "d", .control = added});
    REQUIRE(node == added.get());
    REQUIRE(node->parent() == stack.get());
    REQUIRE(stack->add(ui::Spec{}) == nullptr);
    REQUIRE(stack->children().size() == 4);

    ui::WrapPanelControl wrap;
    REQUIRE(wrap.add(ui::Text{.text = "x"}) != nullptr);
    ui::CanvasControl canvas;
    REQUIRE(canvas.add(ui::Text{.text = "x"}, 4.0f, 8.0f) != nullptr);
    ui::GridControl grid;
    REQUIRE(grid.add(ui::Text{.text = "x"}, 0, 0) != nullptr);
    REQUIRE(wrap.children().size() == 1);
    REQUIRE(canvas.children().size() == 1);
}

TEST_CASE("panel and container color fields take a Color or a theme resource", "[spec]")
{
    constexpr ui::ThemeColor           window = [](const Theme& t) { return t.colors().background; };
    ui::Property<std::optional<Color>> tint{colors::White};

    ui::Control<ui::VerticalStack> stack;
    ui::Control<ui::Grid>          grid;
    ui::Control<ui::Border>        border;
    ui::Control<ui::LayoutPanel>   panel;
    ui::Control<ui::Scroll>        scroll;
    ui::Control<ui::SidePane>      split;
    auto                           root = ui::build(
        ui::VerticalStack{.background = window,
                          .control    = stack,
                          .content    = {
                              ui::Grid{.background = colors::White, .control = grid},
                              ui::Border{.background = ui::bind(tint), .border_brush = window, .control = border},
                              ui::LayoutPanel{.background = window, .border_brush = colors::Black, .control = panel},
                              ui::Scroll{.background = window, .control = scroll},
                              ui::SidePane{.pane_background = window, .control = split},
                          }});
    CHECK(stack->background_resource() == window);
    CHECK(grid->background() == std::optional<Color>{colors::White});
    CHECK(border->background() == std::optional<Color>{colors::White});
    CHECK(border->border_brush_resource() == window);
    CHECK(panel->background_resource() == window);
    CHECK(panel->border_brush() == std::optional<Color>{colors::Black});
    CHECK(scroll->background_resource() == window);
    CHECK(split->pane_background_resource() == window);

    tint = colors::Black;
    CHECK(border->background() == std::optional<Color>{colors::Black});
    panel->set_border_brush(window);
    CHECK(panel->border_brush_resource() == window);
    CHECK_FALSE(panel->border_brush().has_value());
}

TEST_CASE("a loop appends cells and items to the slot lists", "[spec]")
{
    using namespace ui::literals;

    ui::CellList cells;
    for (std::size_t i = 0; i < 3; ++i)
    {
        cells.add(ui::Cell{.row = i, .content = ui::Text{.text = "row"}});
    }
    REQUIRE(cells.cells.size() == 3);
    ui::Control<ui::Grid> grid;
    auto                  root = ui::build(ui::Grid{.columns = {1_fr, 40_dip},
                                                    .rows    = {ui::AutoSize, ui::AutoSize, ui::AutoSize},
                                                    .control = grid,
                                                    .content = std::move(cells)});
    REQUIRE(grid->row_count() == 3);
    CHECK(grid->column(1) == ui::GridLength::dip(40.0f));

    ui::CanvasItemList items;
    items.add(ui::CanvasItem{.left = 4.0f, .content = ui::Text{.text = "a"}});
    ui::TilePanelItemList tiles;
    tiles.add(ui::TilePanelItem{.content = ui::Text{.text = "b"}});
    ui::RelativeItemList relative;
    relative.add(ui::RelativeItem{.name = "c", .content = ui::Text{.text = "c"}});
    CHECK(items.items_.size() == 1);
    CHECK(tiles.items_.size() == 1);
    CHECK(relative.items_.size() == 1);
}

TEST_CASE("a plain value initializes without subscribing; a binding subscribes", "[spec][binding]")
{
    ui::Control<ui::Text>    label;
    std::unique_ptr<Element> plain = ui::build(ui::Text{.text = "static", .control = label});
    REQUIRE(label->text() == "static");
    REQUIRE(label->subscription_count() == 0);

    Property<int>            clicks{0};
    ui::Control<ui::Text>    bound;
    std::unique_ptr<Element> reactive = ui::build(ui::Text{
        .text    = bind(clicks, [](int n) { return "Clicks: " + std::to_string(n); }),
        .control = bound,
    });

    REQUIRE(bound->text() == "Clicks: 0");
    REQUIRE(bound->subscription_count() == 1);

    clicks = 7;
    REQUIRE(bound->text() == "Clicks: 7");
}

TEST_CASE("a binding dies with the node that owns it", "[spec][binding]")
{
    Property<int> clicks{0};
    {
        std::unique_ptr<Element> tree = ui::build(ui::Text{
            .text = bind(clicks, [](int n) { return std::to_string(n); }),
        });
        REQUIRE(clicks.observer_count() == 1);
    }
    // The tree is gone; so is the subscription. Writing must not dispatch into freed memory.
    REQUIRE(clicks.observer_count() == 0);
    clicks = 1;
}

TEST_CASE("one model write updates every display of it", "[spec][binding]")
{
    // The value is written once and both displays derive from it, so they cannot disagree.
    Property<int>         count{0};
    ui::Control<ui::Text> a;
    ui::Control<ui::Text> b;

    std::unique_ptr<Element> tree = ui::build(ui::VerticalStack{
        .content =
            {
                ui::Text{.text = bind(count, [](int n) { return std::to_string(n); }), .control = a},
                ui::Text{.text = bind(count, [](int n) { return "total " + std::to_string(n); }), .control = b},
            },
    });

    count = 42;
    REQUIRE(a->text() == "42");
    REQUIRE(b->text() == "total 42");
}

TEST_CASE("handlers wired through a spec fire on the built control", "[spec]")
{
    int                     clicks = 0;
    ui::Control<ui::Button> button;

    std::unique_ptr<Element> tree = ui::build(ui::Button{
        .text     = "Click me",
        .on_click = [&] { ++clicks; },
        .control  = button,
    });

    ui::testing::set_focused(*button, true); // Enter/Space only activate a focused button
    button->handle_key(KeyEvent{Key::Space, KeyAction::Down});
    button->handle_key(KeyEvent{Key::Space, KeyAction::Up});
    REQUIRE(clicks == 1);
}

TEST_CASE("seeding an initial value never fires a change handler", "[spec]")
{
    // Handlers are installed AFTER values are applied, so a caller only ever hears about
    // changes they caused.
    int                       changes = 0;
    ui::Control<ui::CheckBox> box;

    std::unique_ptr<Element> tree = ui::build(ui::CheckBox{
        .text               = "On",
        .checked            = std::optional<bool>{true},
        .on_checked_changed = [&](std::optional<bool>) { ++changes; },
        .control            = box,
    });

    REQUIRE(box->checked() == true);
    REQUIRE(changes == 0);

    box->set_checked(false);
    REQUIRE(changes == 1);
}

TEST_CASE("a Command supplies both the action and its enablement", "[spec][command]")
{
    ui::Property<bool> dirty{false};
    int                saved = 0;
    ui::Command        save{
        .enabled = dirty,
        .execute = [&] { ++saved; },
    };

    ui::Control<ui::Button>  button;
    std::unique_ptr<Element> tree = ui::build(ui::Button{.text = "Save", .command = save, .control = button});

    REQUIRE_FALSE(button->is_enabled()); // the command's `enabled`

    dirty = true; // the button follows the bound field
    REQUIRE(button->is_enabled());
    ui::testing::set_focused(*button, true);
    button->handle_key(KeyEvent{Key::Enter, KeyAction::Down});
    button->handle_key(KeyEvent{Key::Enter, KeyAction::Up});
    REQUIRE(saved == 1);

    // The same command behind a different trigger runs the same code, and refuses when its
    // predicate says no.
    dirty = false;
    save();
    REQUIRE(saved == 1);
}

TEST_CASE("a component is a plain function returning a View", "[spec][view]")
{
    struct User
    {
        std::string name;
        std::string email;
    };

    auto user_row = [](const User& u) -> ui::Spec
    {
        return ui::HorizontalStack{
            .content =
                {
                    ui::Text{.text = u.name},
                    ui::Text{.text = u.email, .role = ui::TextRole::Caption},
                },
        };
    };

    const User alice{"Alice", "alice@example.com"};
    const User bob{"Bob", "bob@example.com"};

    ui::Control<ui::VerticalStack> root;
    std::unique_ptr<Element>       tree = ui::build(
        ui::VerticalStack{.control = root, .content = {user_row(alice), user_row(bob)}});

    REQUIRE(root->children().size() == 2);
}

TEST_CASE("an already-built subtree drops into a declared tree", "[spec]")
{
    // The imperative path stays first class: Child and View both accept a unique_ptr.
    auto hand_built = std::make_unique<StackControl>(Orientation::Horizontal);
    hand_built->add(std::make_unique<TextControl>("legacy"));

    ui::Control<ui::VerticalStack> root;
    std::unique_ptr<Element>       tree = ui::build(ui::VerticalStack{
        .control = root,
        .content = {ui::Text{.text = "declared"}, std::move(hand_built)},
    });

    REQUIRE(root->children().size() == 2);
}

TEST_CASE("styles compose at compile time and fill in what a field leaves unset", "[spec][style]")
{
    static_assert((ui::styles::Centered | ui::styles::Compact).horizontal_alignment == Alignment::Center);
    static_assert((ui::styles::Centered | ui::styles::Compact).margin->left == 2.0f);
    // Right-hand side wins where both set a field.
    static_assert((ui::styles::Compact | ui::styles::Spaced).margin->left == 8.0f);

    ui::Control<ui::Text>    styled;
    std::unique_ptr<Element> a = ui::build(ui::Text{.text = "x", .style = ui::styles::Spaced, .control = styled});
    REQUIRE(styled->margin().left == 8.0f);

    // An explicitly written field wins over the style.
    ui::Control<ui::Text>    overridden;
    std::unique_ptr<Element> b = ui::build(
        ui::Text{.text = "x", .style = ui::styles::Spaced, .margin = Thickness{16.0f}, .control = overridden});
    REQUIRE(overridden->margin().left == 16.0f);
}

TEST_CASE("the layout fields bind, a set value means what it says, and a binding beats the style", "[spec][style]")
{
    ui::Property<float>                    size{120.0f};
    ui::Property<std::optional<float>>     limit{};
    ui::Property<ui::Thickness>            gap{ui::Thickness{4.0f}};
    ui::Property<std::optional<Alignment>> align{Alignment::Center};
    const ui::Style                        wide{.width = 300.0f, .max_width = 50.0f};

    ui::Control<ui::Text>    text;
    std::unique_ptr<Element> a = ui::build(
        ui::Text{.text                 = "x",
                 .style                = wide,
                 .margin               = gap,
                 .width                = size,
                 .max_width            = limit,
                 .horizontal_alignment = align,
                 .control              = text});
    // Bound fields win over the style, also while the bound optional is empty.
    REQUIRE(text->width() == 120.0f);
    REQUIRE(text->max_width() == std::numeric_limits<float>::infinity());
    REQUIRE(text->margin().left == 4.0f);
    REQUIRE(text->horizontal_alignment() == Alignment::Center);

    size  = 0.0f; // zero wide, not Auto
    limit = 80.0f;
    gap   = ui::Symmetric{.horizontal = 2.0f, .vertical = 6.0f};
    align = std::nullopt; // back to the control's own default
    REQUIRE(text->width() == 0.0f);
    REQUIRE(text->max_width() == 80.0f);
    REQUIRE(text->margin().top == 6.0f);
    REQUIRE(text->horizontal_alignment() == Alignment::Stretch);

    // A constant: set wins over the style, unset lets the style fill in; braced values build in place.
    ui::Control<ui::Text>    styled;
    std::unique_ptr<Element> b = ui::build(
        ui::Text{.text = "x", .style = wide, .margin = ui::Symmetric{.horizontal = 4, .vertical = 8}, .width = 0, .control = styled});
    REQUIRE(styled->width() == 0.0f);
    REQUIRE(styled->max_width() == 50.0f);
    REQUIRE(styled->margin().top == 8.0f);

    ui::Control<ui::Text>    plain;
    std::unique_ptr<Element> c = ui::build(ui::Text{.text = "x", .control = plain});
    REQUIRE_FALSE(plain->width().has_value());
    REQUIRE(plain->min_width() == 0.0f);
}

TEST_CASE("accessibility lands on the built node", "[spec][a11y]")
{
    ui::Control<ui::Button>  button;
    std::unique_ptr<Element> tree = ui::build(ui::Button{
        .text          = "",
        .accessibility = {.name = "Save document", .description = "Saves the current document"},
        .control       = button,
    });

    REQUIRE(button->accessibility().name == "Save document");
    REQUIRE(button->accessibility().description == "Saves the current document");

    // The common case stays free: a labeled control needs no field and stores nothing.
    ui::Control<ui::Button>  plain;
    std::unique_ptr<Element> b = ui::build(ui::Button{.text = "Save", .control = plain});
    REQUIRE(plain->accessibility().empty());
}

TEST_CASE("grid tracks come from the unit literals", "[spec][grid]")
{
    using namespace ui;

    ui::Control<ui::Grid>    grid;
    std::unique_ptr<Element> tree = ui::build(ui::Grid{
        .columns = {120_dip, 1_fr},
        .rows    = {AutoSize, AutoSize},
        .control = grid,
        .content =
            {
                ui::Cell{.row = 0, .column = 0, .content = ui::Text{.text = "Name"}},
                ui::Cell{.row = 0, .column = 1, .content = ui::TextBox{}},
                ui::Cell{.row = 1, .column = 0, .content = ui::Text{.text = "Email"}},
                ui::Cell{.row = 1, .column = 1, .content = ui::TextBox{}},
            },
    });

    REQUIRE(grid->column_count() == 2);
    REQUIRE(grid->row_count() == 2);
    REQUIRE(grid->column(0).unit == GridUnit::Dip);
    REQUIRE(grid->column(0).value == 120.0f);
    REQUIRE(grid->column(1).unit == GridUnit::Fraction);
    REQUIRE(grid->row(0).unit == GridUnit::Auto);
}

TEST_CASE("a list of slots takes its entries written with their type, in order", "[spec][grid]")
{
    ui::CellList cells{
        ui::Cell{.row = 0, .content = ui::Text{.text = "a"}},
        ui::Cell{.row = 1, .content = ui::Text{.text = "b"}},
        ui::Cell{.row = 2, .content = ui::Text{.text = "c"}},
    };
    REQUIRE(cells.cells.size() == 3);
    for (std::size_t i = 0; i < 3; ++i)
    {
        CHECK(cells.cells[i].row.initial() == i);
        CHECK(dynamic_cast<TextControl&>(*cells.cells[i].node).text() == std::string(1, static_cast<char>('a' + i)));
    }

    // One entry, and none.
    ui::CellList one{ui::Cell{.column = 2, .content = ui::Text{}}};
    CHECK(one.cells.size() == 1);
    CHECK(one.cells[0].column.initial() == 2);
    CHECK(ui::CellList{}.cells.empty());

    ui::TabList tabs{ui::Tab{.header = "One"}, ui::Tab{.header = "Two"}};
    REQUIRE(tabs.entries.size() == 2);
    CHECK(tabs.entries[1].header.initial() == "Two");

    // A named slot is moved in, not copied.
    static_assert(!std::is_constructible_v<ui::CellList, ui::Cell&>);
    static_assert(std::is_constructible_v<ui::CellList, ui::Cell&&>);
}

TEST_CASE("Switch rebuilds its subtree when the property it watches changes", "[spec][dynamic]")
{
    enum class Page
    {
        Home,
        Settings,
    };

    Property<Page>               page{Page::Home};
    ui::Control<ui::Match<Page>> host;

    std::unique_ptr<Element> tree = ui::build(ui::Match<Page>{
        .value   = ui::bind(page),
        .control = host,
        .content = [](const Page& p) -> ui::Spec
        { return p == Page::Home ? ui::Spec{ui::Text{.text = "home"}} : ui::Spec{ui::Text{.text = "settings"}}; },
    });

    REQUIRE(host->child() != nullptr);
    REQUIRE(static_cast<TextControl*>(host->child())->text() == "home");

    page = Page::Settings;
    REQUIRE(static_cast<TextControl*>(host->child())->text() == "settings");

    // The equality gate means a redundant write rebuilds nothing.
    Element* before = host->child();
    page            = Page::Settings;
    REQUIRE(host->child() == before);
}

TEST_CASE("Match deduces its type from the value, and takes a plain value or a converted binding", "[spec][dynamic]")
{
    Property<int> count{0};

    // No type argument: `.value` decides it.
    auto deduced = ui::Match{.value   = ui::bind(count),
                             .content = [](const int& n) -> ui::Spec { return ui::Text{.text = std::to_string(n)}; }};
    static_assert(std::is_same_v<decltype(deduced), ui::Match<int>>);

    ui::Control<ui::Match<int>> host;
    deduced.control               = host;
    std::unique_ptr<Element> tree = ui::build(std::move(deduced));
    REQUIRE(static_cast<TextControl*>(host->child())->text() == "0");
    count = 7;
    REQUIRE(static_cast<TextControl*>(host->child())->text() == "7");

    // A binding with a conversion carries all Match needs to subscribe.
    ui::Control<ui::Match<bool>> flag;
    std::unique_ptr<Element>     parity = ui::build(ui::Match<bool>{
        .value   = ui::bind(count, [](int n) { return n % 2 == 0; }),
        .control = flag,
        .content = [](const bool& even) -> ui::Spec { return ui::Text{.text = even ? "even" : "odd"}; },
    });
    REQUIRE(static_cast<TextControl*>(flag->child())->text() == "odd");
    count = 8;
    REQUIRE(static_cast<TextControl*>(flag->child())->text() == "even");

    // A plain value builds once, and an empty Spec shows nothing.
    ui::Control<ui::Match<int>> fixed;
    std::unique_ptr<Element>    once = ui::build(
        ui::Match<int>{.value = 3, .control = fixed, .content = [](const int&) -> ui::Spec { return {}; }});
    REQUIRE(fixed->child() == nullptr);
}

namespace
{
struct Track
{
    std::string title;
    friend bool operator==(const Track&, const Track&) = default;
};

std::vector<Track> tracks(std::size_t count)
{
    std::vector<Track> all;
    all.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        all.push_back({"Track " + std::to_string(i)});
    }
    return all;
}

// A repeater inside a 200 x 100 scroll view, laid out.
struct Repeated
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();
    ScrollControl        view;

    explicit Repeated(ui::Spec content)
    {
        view.set_child(std::move(content).build());
        layout();
    }
    void layout()
    {
        (void)view.measure({200.0f, 100.0f}, measurer, theme);
        view.arrange(Rect{0.0f, 0.0f, 200.0f, 100.0f});
    }
};
} // namespace

TEST_CASE("ForEach builds every item by default and only what shows when virtualized", "[spec][dynamic][foreach]")
{
    Property<std::vector<Track>> model{tracks(200)};
    const auto row = [](const Track& t) -> ui::Spec { return ui::Text{.text = t.title, .height = 20.0f}; };

    ui::Control<ui::ForEach<Track>> all;
    ui::Control<ui::ForEach<Track>> some;

    // The type argument is deduced from `.items`, with a handle given too.
    auto deduced = ui::ForEach{.items = ui::bind(model), .control = all, .item = row};
    static_assert(std::is_same_v<decltype(deduced), ui::ForEach<Track>>);

    Repeated everything{std::move(deduced)};
    Repeated windowed{ui::ForEach<Track>{.items = ui::bind(model), .virtualize = true, .control = some, .item = row}};

    CHECK(all->item_count() == 200);
    CHECK(all->realized_count() == 200);
    CHECK(some->realized_count() > 0);
    CHECK(some->realized_count() < 200);

    // The rows follow the collection.
    model.update([](std::vector<Track>& v) { v.resize(3); });
    everything.layout();
    windowed.layout();
    CHECK(all->item_count() == 3);
    CHECK(all->realized_count() == 3);
    CHECK(some->realized_count() == 3);
    const auto* first = dynamic_cast<TextControl*>(all->try_get_element(0));
    REQUIRE(first != nullptr);
    CHECK(first->text() == "Track 0");
}

TEST_CASE("ForEach keeps a row's element across edits around it", "[spec][dynamic][foreach]")
{
    Property<std::vector<Track>>    model{tracks(4)}; // Track 0..3
    int                             built = 0;
    ui::Control<ui::ForEach<Track>> rows;
    Repeated                        host{ui::ForEach{
        .items   = ui::bind(model),
        .control = rows,
        .item    = [&built](const Track& t) -> ui::Spec
        {
            ++built;
            return ui::Text{.text = t.title, .height = 20.0f};
        },
    }};
    REQUIRE(built == 4);
    const Element* first = rows->try_get_element(0);
    const Element* last  = rows->try_get_element(3);

    // Append: the four rows stay, one is built.
    model.update([](std::vector<Track>& v) { v.push_back({"Track 4"}); });
    host.layout();
    CHECK(built == 5);
    CHECK(rows->try_get_element(0) == first);
    CHECK(rows->try_get_element(3) == last);

    // Remove from the middle: the row after it moves up and is still the same element.
    model.update([](std::vector<Track>& v) { v.erase(v.begin() + 1); });
    host.layout();
    CHECK(built == 5);
    CHECK(rows->item_count() == 4);
    CHECK(rows->try_get_element(0) == first);
    CHECK(rows->try_get_element(2) == last);

    // Insert at the front: everything shifts by one, nothing else is built.
    model.update([](std::vector<Track>& v) { v.insert(v.begin(), {"Intro"}); });
    host.layout();
    CHECK(built == 6);
    CHECK(rows->try_get_element(1) == first);

    // Change one item in place: only that row is built again.
    model.update([](std::vector<Track>& v) { v[1].title = "Renamed"; });
    host.layout();
    CHECK(built == 7);
    CHECK(static_cast<const TextControl*>(rows->try_get_element(1))->text() == "Renamed");
}

TEST_CASE("ForEach takes a plain vector and a layout value", "[spec][dynamic][foreach]")
{
    ui::Control<ui::ForEach<Track>> grid;
    Repeated                        host{ui::ForEach<Track>{
        .items   = tracks(6),
        .layout  = ui::TileLayout{.min_item_width = 90.0f, .min_item_height = 20.0f},
        .control = grid,
        .item = [](const Track& t) -> ui::Spec { return ui::Text{.text = t.title, .width = 90.0f, .height = 20.0f}; },
    }};

    REQUIRE(grid->realized_count() == 6);
    // Two 90 DIP cells fit across 200: the third item starts the second line.
    const Element* second = grid->try_get_element(1);
    const Element* third  = grid->try_get_element(2);
    REQUIRE(second != nullptr);
    REQUIRE(third != nullptr);
    CHECK(second->bounds().y == grid->try_get_element(0)->bounds().y);
    CHECK(third->bounds().y > second->bounds().y);
}

TEST_CASE("a layout is a value, or an object of the app's own", "[spec][layout]")
{
    static_assert(ui::LayoutLike<ui::StackLayout>);
    static_assert(ui::LayoutLike<ui::WrapLayout>);
    static_assert(ui::LayoutLike<ui::TileLayout>);
    static_assert(ui::LayoutLike<ui::LinedFlowLayout>);
    static_assert(!std::is_copy_constructible_v<ui::LayoutSpec>);

    ui::LayoutSpec none;
    CHECK_FALSE(none);

    ui::LayoutSpec value = ui::StackLayout{.orientation = ui::Orientation::Horizontal, .spacing = 8.0f};
    REQUIRE(value);
    std::unique_ptr<Layout> built = value.take();
    const auto*             stack = dynamic_cast<StackLayoutEngine*>(built.get());
    REQUIRE(stack != nullptr);
    CHECK(stack->orientation() == ui::Orientation::Horizontal);
    CHECK(stack->spacing() == 8.0f);

    // The object form, for a layout the app keeps a pointer to.
    auto              own    = std::make_unique<TileLayoutEngine>();
    TileLayoutEngine* raw    = own.get();
    ui::LayoutSpec    object = std::move(own);
    CHECK(object.take().get() == raw);
}

TEST_CASE("Window::set_content accepts a spec directly", "[spec]")
{
    ui::Control<ui::Button> button;
    Window                  window;
    HeadlessTextMeasurer    measurer;

    window.set_content(ui::VerticalStack{
        .spacing = 4.0f,
        .content = {ui::Text{.text = "Title"}, ui::Button{.text = "OK", .control = button}},
    });

    window.layout({400.0f, 300.0f}, measurer);
    window.focus_content();
    REQUIRE(window.focused() == button);
}

TEST_CASE("ui::Text's enabled binds through to the TextControl", "[spec][binding]")
{
    Property<bool>           on{true};
    ui::Control<ui::Text>    label;
    std::unique_ptr<Element> tree = ui::build(ui::Text{.text = "Name", .enabled = bind(on), .control = label});
    REQUIRE(label->enabled());
    on = false;
    CHECK_FALSE(label->enabled());

    ui::Control<ui::Text>    fixed;
    std::unique_ptr<Element> plain = ui::build(ui::Text{.text = "Name", .enabled = false, .control = fixed});
    CHECK_FALSE(fixed->enabled());
    CHECK(fixed->subscription_count() == 0);
}

TEST_CASE("ui::CheckBox's nullable checked and three_state bind through to the CheckBox", "[spec][binding]")
{
    Property<std::optional<bool>> value{std::nullopt};
    ui::Control<ui::CheckBox>     box;
    std::unique_ptr<Element>      tree = ui::build(
        ui::CheckBox{.text = "Select all", .checked = bind(value), .three_state = true, .control = box});
    REQUIRE_FALSE(box->checked().has_value());
    CHECK(box->three_state());
    value = true;
    CHECK(box->checked() == true);

    ui::Control<ui::CheckBox> plain;
    std::unique_ptr<Element>  fixed = ui::build(ui::CheckBox{.text = "Newsletter", .control = plain});
    CHECK(plain->checked() == false);
    CHECK_FALSE(plain->three_state());
}

TEST_CASE("ui::Rating's placeholder, clear-enabled and caption bind through to the RatingControl", "[spec][binding]")
{
    Property<double>         placeholder{2.5};
    Property<bool>           clearable{false};
    Property<std::string>    caption{"312 ratings"};
    ui::Control<ui::Rating>  rating;
    std::unique_ptr<Element> tree = ui::build(
        ui::Rating{.placeholder_value = bind(placeholder),
                   .clear_enabled     = bind(clearable),
                   .caption           = bind(caption),
                   .control           = rating});
    CHECK(rating->placeholder_value() == 2.5);
    CHECK_FALSE(rating->is_clear_enabled());
    CHECK(rating->caption() == "312 ratings");

    placeholder = 4.0;
    clearable   = true;
    caption     = "313 ratings";
    CHECK(rating->placeholder_value() == 4.0);
    CHECK(rating->is_clear_enabled());
    CHECK(rating->caption() == "313 ratings");

    ui::Control<ui::Rating>  plain;
    std::unique_ptr<Element> fixed = ui::build(
        ui::Rating{.placeholder_value = 3.0, .clear_enabled = false, .control = plain});
    CHECK(plain->placeholder_value() == 3.0);
    CHECK_FALSE(plain->is_clear_enabled());
    CHECK(plain->caption().empty());
    CHECK(plain->subscription_count() == 0);
}

TEST_CASE("ui::Slider's tick_frequency binds through to the Slider", "[spec][binding]")
{
    Property<float>          ticks{20.0f};
    ui::Control<ui::Slider>  slider;
    std::unique_ptr<Element> tree = ui::build(ui::Slider{.tick_frequency = bind(ticks), .control = slider});
    CHECK(slider->tick_frequency() == 20.0f);
    ticks = 10.0f;
    CHECK(slider->tick_frequency() == 10.0f);
}

// ---- The Basic input controls, as spec fields ----

TEST_CASE("ui::Slider's tick placement and snapping bind through to the Slider", "[spec][binding]")
{
    Property<ui::SliderTickPlacement> placement{ui::SliderTickPlacement::Outside};
    Property<ui::SliderSnapsTo>       snaps{ui::SliderSnapsTo::StepValues};
    ui::Control<ui::Slider>           slider;
    std::unique_ptr<Element>          tree = ui::build(
        ui::Slider{.tick_frequency = 20.0f,
                   .tick_placement = bind(placement),
                   .step_frequency = 10.0f,
                   .snaps_to       = bind(snaps),
                   .control        = slider});
    CHECK(slider->tick_placement() == ui::SliderTickPlacement::Outside);
    CHECK(slider->step_frequency() == 10.0f);
    CHECK(slider->snaps_to() == ui::SliderSnapsTo::StepValues);
    placement = ui::SliderTickPlacement::None;
    snaps     = ui::SliderSnapsTo::Ticks;
    CHECK(slider->tick_placement() == ui::SliderTickPlacement::None);
    CHECK(slider->snaps_to() == ui::SliderSnapsTo::Ticks);

    ui::Control<ui::Slider>  plain;
    std::unique_ptr<Element> fixed = ui::build(ui::Slider{.control = plain});
    CHECK(plain->tick_placement() == ui::SliderTickPlacement::Inline);
    CHECK(plain->step_frequency() == 1.0f);
}

TEST_CASE("ui::Rating's read_only binds through to the RatingControl", "[spec][binding]")
{
    Property<bool>           read_only{true};
    ui::Control<ui::Rating>  rating;
    std::unique_ptr<Element> tree = ui::build(ui::Rating{.read_only = bind(read_only), .control = rating});
    CHECK(rating->read_only());
    read_only = false;
    CHECK_FALSE(rating->read_only());
}

TEST_CASE("ui::ToggleSwitch's on and off content bind through to the ToggleSwitch", "[spec][binding]")
{
    Property<std::string>         on_label{"Working"};
    ui::Control<ui::ToggleSwitch> toggle;
    std::unique_ptr<Element>      tree = ui::build(
        ui::ToggleSwitch{.checked = true, .checked_text = bind(on_label), .unchecked_text = "Do work", .control = toggle});
    CHECK(toggle->checked_text() == "Working");
    on_label = "Busy";
    CHECK(toggle->checked_text() == "Busy");
    CHECK(toggle->unchecked_text() == "Do work");
}

TEST_CASE("ui::ComboBox's placeholder and text submission reach the ComboBox", "[spec][binding]")
{
    std::string               submitted;
    ui::Control<ui::ComboBox> combo;
    std::unique_ptr<Element>  tree = ui::build(
        ui::ComboBox{.placeholder_text  = "Size",
                     .items             = {"8", "9"},
                     .editable          = true,
                     .on_text_submitted = [&](const ui::ComboBoxTextSubmittedArgs& args) { submitted = args.text; },
                     .control           = combo});
    CHECK(combo->placeholder_text() == "Size");
    ui::testing::set_focused(*combo, true);
    combo->handle_text("4");
    combo->handle_text("2");
    combo->handle_key({Key::Enter, KeyAction::Down});
    CHECK(submitted == "42");
}

TEST_CASE("ui::Text's wrapping and ui::TextBox's placeholder reach their controls", "[spec][binding]")
{
    ui::Control<ui::Text>    label;
    std::unique_ptr<Element> text = ui::build(
        ui::Text{.text = "Long", .wrapping = TextWrapping::WrapWholeWords, .control = label});
    CHECK(label->wrapping() == TextWrapping::WrapWholeWords);

    ui::Control<ui::TextBox> box;
    std::unique_ptr<Element> field = ui::build(ui::TextBox{.placeholder_text = "Name", .control = box});
    CHECK(box->placeholder_text() == "Name");
}

TEST_CASE("ui::TextBox's MaxLength and CharacterCasing are bindable", "[spec][binding]")
{
    Property<std::size_t>     length{4};
    Property<CharacterCasing> casing{CharacterCasing::Upper};
    ui::Control<ui::TextBox>  box;
    std::unique_ptr<Element>  field = ui::build(
        ui::TextBox{.max_length = bind(length), .character_casing = bind(casing), .control = box});
    CHECK(box->max_length() == 4);
    CHECK(box->character_casing() == CharacterCasing::Upper);
    length = 0;
    casing = CharacterCasing::Lower;
    CHECK(box->max_length() == 0);
    CHECK(box->character_casing() == CharacterCasing::Lower);
}

TEST_CASE("ui::RadioButtons builds the group, seeding its selection without a callback", "[spec][binding]")
{
    Property<std::optional<std::size_t>> selected{1};
    Property<std::size_t>                columns{3};
    int                                  fired = 0;
    ui::Control<ui::RadioButtons>        group;
    std::unique_ptr<Element>             tree = ui::build(
        ui::RadioButtons{.header               = "Background",
                         .items                = {"Green", "Yellow", "White"},
                         .selected_index       = bind(selected),
                         .max_columns          = bind(columns),
                         .on_selection_changed = [&](std::optional<std::size_t>) { ++fired; },
                         .control              = group});
    CHECK(group->header() == "Background");
    CHECK(group->item_count() == 3);
    CHECK(group->selected_index() == 1);
    CHECK(group->max_columns() == 3);
    CHECK(fired == 0);

    selected = 2;
    CHECK(group->selected_index() == 2);
    CHECK(fired == 0); // a bound write isn't the user's change either
}

TEST_CASE("ui::ColorPicker's alpha and visibility switches bind through live", "[spec][binding]")
{
    Property<bool>               alpha{false};
    Property<bool>               more{true};
    ui::Control<ui::ColorPicker> picker;
    std::unique_ptr<Element>     tree = ui::build(
        ui::ColorPicker{.color               = Color::from_rgba(255, 0, 0).with_alpha(0.5f),
                        .alpha_enabled       = bind(alpha),
                        .spectrum_visible    = false,
                        .more_button_visible = bind(more),
                        .hex_input_visible   = false,
                        .control             = picker});
    CHECK_FALSE(picker->alpha_enabled());
    CHECK(picker->color().a == 1.0f);
    CHECK_FALSE(picker->spectrum_visible());
    CHECK(picker->more_button_visible());
    CHECK_FALSE(picker->hex_input_visible());
    CHECK(picker->color_channel_text_input_visible()); // the default

    alpha = true;
    more  = false;
    CHECK(picker->alpha_enabled());
    CHECK_FALSE(picker->more_button_visible());
}

TEST_CASE("ui::InfoBar binds severity, closable and icon visibility, and builds its action button", "[spec][binding]")
{
    Property<InfoBarSeverity> severity{InfoBarSeverity::Warning};
    Property<bool>            closable{true};
    Property<bool>            icon{false};
    ui::Control<ui::InfoBar>  bar;
    std::unique_ptr<Element>  tree = ui::build(
        ui::InfoBar{.title                 = "Title",
                    .is_open               = true,
                    .severity              = bind(severity),
                    .closable              = bind(closable),
                    .icon_visible          = bind(icon),
                    .control               = bar,
                    .action_button_content = ui::Button{.text = "Action"}});
    CHECK(bar->severity() == InfoBarSeverity::Warning);
    CHECK_FALSE(bar->is_icon_visible());
    REQUIRE(bar->action_button_content() != nullptr);
    CHECK(static_cast<ui::ButtonControl*>(bar->action_button_content())->text() == "Action");

    severity = InfoBarSeverity::Error;
    closable = false;
    icon     = true;
    CHECK(bar->severity() == InfoBarSeverity::Error);
    CHECK_FALSE(bar->is_closable());
    CHECK(bar->is_icon_visible());
}

TEST_CASE("ui::InfoBar builds its custom content as the bar's child", "[spec]")
{
    ui::Control<ui::InfoBar> bar;
    ui::Control<ui::Button>  inner;
    std::unique_ptr<Element> tree = ui::build(
        ui::InfoBar{.title = "Title", .is_open = true, .control = bar, .content = ui::Button{.text = "Inside", .control = inner}});
    REQUIRE(bar->content() != nullptr);
    CHECK(bar->content() == static_cast<Element*>(inner));
    CHECK(bar->content()->parent() == static_cast<Element*>(bar));
    CHECK(bar->action_button_content() == nullptr);
}

TEST_CASE("ui::InfoBadge binds severity, icon and background", "[spec][binding]")
{
    Property<InfoBadgeSeverity> severity{InfoBadgeSeverity::Success};
    ui::Control<ui::InfoBadge>  badge;
    std::unique_ptr<Element>    tree = ui::build(
        ui::InfoBadge{.severity   = bind(severity),
                      .icon       = glyphs::Cancel,
                      .background = Color::from_argb(0xFFC42B1C),
                      .control    = badge});
    CHECK(badge->severity() == InfoBadgeSeverity::Success);
    CHECK(badge->icon() == std::optional<IconGlyph>{glyphs::Cancel});
    CHECK(badge->background() == std::optional<Color>{Color::from_argb(0xFFC42B1C)});
    severity = InfoBadgeSeverity::Critical;
    CHECK(badge->severity() == InfoBadgeSeverity::Critical);
}

TEST_CASE("ui::ProgressBar's paused/error and ui::ProgressRing's background bind through", "[spec][binding]")
{
    Property<int>                state{1};
    ui::Control<ui::ProgressBar> bar;
    std::unique_ptr<Element> tree = ui::build(ui::ProgressBar{.show_paused = bind(state, [](int v) { return v == 1; }),
                                                              .show_error  = bind(state, [](int v) { return v == 2; }),
                                                              .control     = bar});
    CHECK(bar->show_paused());
    CHECK_FALSE(bar->show_error());
    state = 2;
    CHECK_FALSE(bar->show_paused());
    CHECK(bar->show_error());

    Property<std::optional<Color>> background{};
    ui::Control<ui::ProgressRing>  ring;
    std::unique_ptr<Element> ring_tree = ui::build(ui::ProgressRing{.background = bind(background), .control = ring});
    CHECK_FALSE(ring->background().has_value());
    background = std::optional<Color>{Color::from_argb(0xFFD3D3D3)};
    CHECK(ring->background() == std::optional<Color>{Color::from_argb(0xFFD3D3D3)});
}

TEST_CASE("ui::ToolTip carries the offsets, the placement rect and the control's own 800ms delay", "[spec]")
{
    ui::Control<ui::ToolTip> tip;
    std::unique_ptr<Element> tree = ui::build(
        ui::ToolTip{.vertical_offset = -80.0f,
                    .placement_rect  = Rect{0.0f, 0.0f, 400.0f, 266.0f},
                    .control         = tip,
                    .anchor          = ui::Text{.text = "Anchor"},
                    .content         = ui::Text{.text = "Tip"}});
    CHECK(tip->delay() == 800ms);
    CHECK(tip->horizontal_offset() == 20.0f);
    CHECK(tip->vertical_offset() == -80.0f);
    CHECK(tip->placement_rect() == std::optional<Rect>{Rect{0.0f, 0.0f, 400.0f, 266.0f}});
}

TEST_CASE("ui::ComboBox can start empty with a placeholder; ui::Button takes a padding", "[spec]")
{
    ui::Control<ui::ComboBox> dropdown;
    std::unique_ptr<Element>  tree = ui::build(
        ui::ComboBox{.placeholder_text = "Pick", .items = {"A", "B"}, .selected_index = std::nullopt, .control = dropdown});
    CHECK(dropdown->selected_index() == std::nullopt);
    CHECK(dropdown->placeholder_text() == "Pick");

    HeadlessTextMeasurer     measurer;
    std::unique_ptr<Element> plain  = ui::build(ui::Button{.text = "Text"});
    std::unique_ptr<Element> padded = ui::build(ui::Button{.text = "Text", .padding = Thickness{0.0f}});
    CHECK(padded->measure({Unbounded, Unbounded}, measurer, Theme::light()).width <
          plain->measure({Unbounded, Unbounded}, measurer, Theme::default_light()).width);
}

TEST_CASE("ui::NavItem's icon and InfoBadge reach the NavigationShell", "[spec]")
{
    ui::Control<ui::NavigationShell> nav;
    std::unique_ptr<Element>         tree = ui::build(ui::NavigationShell{
        .control = nav,
        .items   = {ui::NavItem{.text = "Home"},
                    ui::NavItem{.text = "Inbox", .icon = IconGlyph{0xE715}, .info_badge = ui::InfoBadge{.value = 5}}},
    });
    CHECK(nav->info_badge_at(0) == nullptr);
    REQUIRE(nav->info_badge_at(1) != nullptr);
    CHECK(nav->info_badge_at(1)->value() == 5);
}

TEST_CASE("Every spec's width, height, max_width and max_height reach the element; 0 leaves them unset", "[spec]")
{
    std::unique_ptr<Element> sized = ui::build(
        ui::Button{.text = "B", .width = 200.0f, .height = 60.0f, .max_width = 300.0f});
    CHECK(sized->width() == std::optional<float>{200.0f});
    CHECK(sized->height() == std::optional<float>{60.0f});
    CHECK(sized->max_width() == 300.0f);

    std::unique_ptr<Element> plain = ui::build(ui::Text{.text = "T"});
    CHECK_FALSE(plain->width().has_value());
    CHECK(plain->max_height() == std::numeric_limits<float>::infinity());

    ui::Style                wide{.width = 150.0f};
    std::unique_ptr<Element> styled = ui::build(ui::Text{.text = "T", .style = wide});
    CHECK(styled->width() == std::optional<float>{150.0f});
}

TEST_CASE("ui::Dialog carries the default button, each button's enabled binding and on_closing", "[spec]")
{
    Property<bool>           can_save{false};
    ui::Control<ui::Dialog>  dialog;
    DialogResult             seen = DialogResult::Primary;
    std::unique_ptr<Element> tree = ui::build(ui::Dialog{
        .title          = "Save your work?",
        .default_button = DialogDefaultButton::Close,
        .primary        = {.text = "Save", .enabled = bind(can_save)},
        .close          = {.text = "Cancel"},
        .on_closing     = [&seen](const DialogClosingArgs& args) { seen = args.result; },
        .control        = dialog,
    });
    CHECK(dialog->default_button() == DialogDefaultButton::Close);
    CHECK_FALSE(dialog->primary_button()->enabled());
    can_save = true;
    CHECK(dialog->primary_button()->enabled());
    dialog->hide();
    CHECK(seen == DialogResult::None);
}

TEST_CASE("ui::Popup binds is_open and the offsets; ui::TeachingTip its texts and placement", "[spec]")
{
    Property<bool>           open{false};
    Property<float>          offset{200.0f};
    ui::Control<ui::Popup>   popup;
    std::unique_ptr<Element> popup_tree = ui::build(
        ui::Popup{.is_open           = bind(open),
                  .horizontal_offset = bind(offset),
                  .control           = popup,
                  .content           = ui::Text{.text = "Simple Popup"}});
    CHECK_FALSE(popup->is_open());
    CHECK(popup->horizontal_offset() == 200.0f);
    open   = true;
    offset = -100.0f;
    CHECK(popup->is_open());
    CHECK(popup->horizontal_offset() == -100.0f);

    ui::Control<ui::TeachingTip> tip;
    std::unique_ptr<Element>     tip_tree = ui::build(
        ui::TeachingTip{.title              = "This is the title",
                        .subtitle           = "And this is the subtitle",
                        .action_button_text = "Action button",
                        .close_button_text  = "Close button",
                        .light_dismiss      = true,
                        .placement          = PopupPlacement::Below,
                        .placement_margin   = Thickness{20.0f},
                        .control            = tip});
    CHECK(tip->title() == "This is the title");
    CHECK(tip->subtitle() == "And this is the subtitle");
    CHECK(tip->action_button_text() == "Action button");
    CHECK(tip->light_dismiss());
    CHECK(tip->placement() == PopupPlacement::Below);
    CHECK(tip->placement_margin().left == 20.0f);
    CHECK_FALSE(tip->alternate_close_visible());
}

TEST_CASE("ui::Scroll binds its modes and bar visibilities; ui::NumberBox takes placement and LargeChange", "[spec]")
{
    Property<ScrollZoomMode>      zoom{ScrollZoomMode::Disabled};
    Property<ScrollBarVisibility> bar{ScrollBarVisibility::Auto};
    ui::Control<ui::Scroll>       view;
    int                           changes = 0;
    std::unique_ptr<Element>      tree    = ui::build(
        ui::Scroll{.max_zoom_factor                = 4.0f,
                   .content_orientation            = ScrollContentOrientation::None,
                   .zoom_mode                      = bind(zoom),
                   .vertical_scroll_bar_visibility = bind(bar),
                   .focusable                      = true,
                   .on_view_changed                = [&] { ++changes; },
                   .control                        = view,
                   .content                        = ui::Text{.text = "content"}});
    CHECK(view->content_orientation() == ScrollContentOrientation::None);
    CHECK(view->zoom_mode() == ScrollZoomMode::Disabled);
    CHECK(view->max_zoom_factor() == 4.0f);
    CHECK(view->focusable());
    zoom = ScrollZoomMode::Enabled;
    bar  = ScrollBarVisibility::Hidden;
    CHECK(view->zoom_mode() == ScrollZoomMode::Enabled);
    CHECK(view->vertical_scroll_bar_visibility() == ScrollBarVisibility::Hidden);

    ui::Control<ui::NumberBox> number;
    std::unique_ptr<Element>   box = ui::build(
        ui::NumberBox{.value                 = 3.0,
                      .large_step            = 100.0,
                      .spin_button_placement = NumberBoxSpinButtonPlacementMode::Inline,
                      .control               = number});
    CHECK(number->large_step() == 100.0);
    CHECK(number->spin_button_placement() == NumberBoxSpinButtonPlacementMode::Inline);
}

TEST_CASE("ui::Scroll's offsets and zoom factor bind two-way, jump on a write and clamp", "[spec]")
{
    Property<float>          across{0.0f};
    Property<float>          down{5000.0f}; // written before the first layout: lands clamped
    Property<float>          zoom{1.0f};
    ui::Control<ui::Scroll>  view;
    int                      changes = 0;
    std::unique_ptr<Element> tree    = ui::build(
        ui::Scroll{.horizontal_offset   = across,
                   .vertical_offset     = down,
                   .zoom_factor         = zoom,
                   .max_zoom_factor     = 4.0f,
                   .content_orientation = ScrollContentOrientation::Both,
                   .on_view_changed     = [&] { ++changes; },
                   .control             = view,
                   .content             = ui::Spacer{.width = 600.0f, .height = 1000.0f}});
    CHECK(view->vertical_offset() == 5000.0f); // not laid out yet

    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();
    (void)tree->measure({300.0f, 200.0f}, measurer, theme);
    tree->arrange(Rect{0.0f, 0.0f, 300.0f, 200.0f});
    CHECK(view->vertical_offset() == 800.0f);
    CHECK(down.get() == 800.0f); // the clamped value went back
    CHECK(changes > 0);

    // An app write jumps: no animation runs.
    down = 100.0f;
    CHECK(view->vertical_offset() == 100.0f);
    CHECK(view->state() == ScrollInteractionState::Idle);
    across = 50.0f;
    CHECK(view->horizontal_offset() == 50.0f);

    // A write past the end clamps, and the property follows.
    down = 900.0f;
    CHECK(view->vertical_offset() == 800.0f);
    CHECK(down.get() == 800.0f);

    // The view's own scrolling writes back.
    (void)view->scroll_to(20.0f, 30.0f, {.animation_mode = ScrollAnimationMode::Disabled});
    CHECK(across.get() == 20.0f);
    CHECK(down.get() == 30.0f);

    // A zoom write clamps to the zoom range; the offsets follow the zoom about the center.
    zoom = 10.0f;
    CHECK(view->zoom_factor() == 4.0f);
    CHECK(zoom.get() == 4.0f);
    CHECK(across.get() == view->horizontal_offset());
    CHECK(down.get() == view->vertical_offset());
}

TEST_CASE("ui::Grid binds its spacing, background and each cell's row and column", "[spec]")
{
    Property<std::size_t>    column{0};
    Property<float>          spacing{8.0f};
    ui::Control<ui::Grid>    grid;
    ui::Control<ui::Text>    label;
    std::unique_ptr<Element> tree = ui::build(ui::Grid{
        .columns        = {ui::operator""_dip(50ull), ui::operator""_dip(50ull)},
        .rows           = {ui::AutoSize},
        .column_spacing = bind(spacing),
        .background     = colors::Black,
        .control        = grid,
        .content = {ui::Cell{.row = 0, .column = bind(column), .content = ui::Text{.text = "red", .control = label}}},
    });
    CHECK(grid->column_spacing() == 8.0f);
    CHECK(grid->background() == std::optional<Color>{colors::Black});
    (void)describe_tree(*tree);
    const float before = label->bounds().x;
    column             = 1;
    spacing            = 4.0f;
    CHECK(grid->column_spacing() == 4.0f);
    (void)describe_tree(*tree);
    CHECK(label->bounds().x > before);
}

TEST_CASE("ui::VerticalStack binds its spacing; the Layout group's specs build their controls", "[spec]")
{
    Property<float>                spacing{2.0f};
    ui::Control<ui::VerticalStack> stack;
    std::unique_ptr<Element>       a = ui::build(ui::VerticalStack{.spacing = bind(spacing), .control = stack});
    spacing                          = 12.0f;
    CHECK(stack->spacing() == 12.0f);

    ui::Control<ui::Border> border;
    auto                    b = ui::build(ui::Border{.background       = colors::White,
                                                     .border_thickness = Thickness{2.0f},
                                                     .control          = border,
                                                     .content          = ui::Text{.text = "in a border"}});
    CHECK(border->background() == std::optional<Color>{colors::White});
    CHECK(border->child() != nullptr);

    Property<float>         left{10.0f};
    ui::Control<ui::Canvas> canvas;
    ui::Control<ui::Text>   item;
    auto                    c = ui::build(
        ui::Canvas{.control = canvas,
                   .content = {
                       ui::CanvasItem{.left = bind(left), .z_index = 3, .content = ui::Text{.text = "x", .control = item}}}});
    CHECK(canvas->left(item) == 10.0f);
    CHECK(canvas->z_index(item) == 3);
    left = 40.0f;
    CHECK(canvas->left(item) == 40.0f);

    ui::Control<ui::ScaleToFit> box;
    auto v = ui::build(ui::ScaleToFit{.stretch = Stretch::Fill, .control = box, .content = ui::Text{.text = "x"}});
    CHECK(box->stretch() == Stretch::Fill);

    ui::Control<ui::WrapPanel> wrap;
    auto                       w = ui::build(ui::WrapPanel{.item_spacing  = 8.0f,
                                                           .items_stretch = WrapPanelItemsStretch::Last,
                                                           .control       = wrap,
                                                           .content = {ui::Text{.text = "a"}, ui::Text{.text = "b"}}});
    CHECK(wrap->item_spacing() == 8.0f);
    CHECK(wrap->children().size() == 2);

    ui::Control<ui::TilePanel> vswg;
    ui::Control<ui::Text>      big;
    auto g = ui::build(ui::TilePanel{.item_width              = 44.0f,
                                     .maximum_rows_or_columns = 3,
                                     .control                 = vswg,
                                     .content = {ui::TilePanelItem{.row_span    = 2,
                                                                   .column_span = 2,
                                                                   .content = ui::Text{.text = "a", .control = big}}}});
    CHECK(vswg->item_width() == std::optional<float>{44.0f});
    CHECK_FALSE(vswg->item_height().has_value());
    CHECK(vswg->row_span(big) == 2);

    ui::Control<ui::RelativePanel> rel;
    ui::Control<ui::Text>          first;
    ui::Control<ui::Text>          second;
    auto                           r = ui::build(
        ui::RelativePanel{.control = rel,
                          .content = {
                              ui::RelativeItem{.right_of = "one", .content = ui::Text{.text = "b", .control = second}},
                              ui::RelativeItem{.name = "one", .content = ui::Text{.text = "a", .control = first}},
                          }});
    CHECK(rel->constraints(second).right_of == first); // named after itself, resolved anyway

    ui::Control<ui::LayoutPanel> lp;
    auto                         l = ui::build(
        ui::LayoutPanel{.layout = std::make_unique<StackLayoutEngine>(), .control = lp, .content = {ui::Text{.text = "a"}}});
    CHECK(dynamic_cast<StackLayoutEngine*>(lp->layout()) != nullptr);

    Property<bool>            open{false};
    ui::Control<ui::SidePane> split;
    auto                      s = ui::build(ui::SidePane{.is_pane_open     = bind(open),
                                                         .display_mode     = SidePaneDisplayMode::CompactInline,
                                                         .open_pane_length = std::nullopt,
                                                         .control          = split,
                                                         .pane             = ui::Text{.text = "pane"},
                                                         .content          = ui::Text{.text = "content"}});
    CHECK_FALSE(split->open_pane_length().has_value()); // unset is Auto
    CHECK(split->display_mode() == SidePaneDisplayMode::CompactInline);
    open = true;
    CHECK(split->is_pane_open());

    ui::Control<ui::Expander> expander;
    auto                      e = ui::build(ui::Expander{.expand_direction = ExpandDirection::Up,
                                                         .control          = expander,
                                                         .header_content   = ui::Text{.text = "centered"},
                                                         .content          = ui::Text{.text = "body"}});
    CHECK(expander->expand_direction() == ExpandDirection::Up);
    CHECK(expander->header_content() != nullptr);

    ui::Control<ui::Slider> slider;
    auto sl = ui::build(ui::Slider{.orientation = Orientation::Vertical, .direction_reversed = true, .control = slider});
    CHECK(slider->direction_reversed());
}

TEST_CASE("ui::NavigationShell carries the display mode, the pane's chrome and footer items, and binds the header",
          "[spec]")
{
    Property<std::string>            header{"Home"};
    Property<bool>                   settings{true};
    ui::Control<ui::NavigationShell> nav;
    ui::Control<ui::Button>          search;
    int                              invoked = 0;
    std::unique_ptr<Element>         tree    = ui::build(ui::NavigationShell{
        .header                  = bind(header),
        .pane_title              = "Pane Title",
        .pane_display_mode       = NavigationShellPaneDisplayMode::Top,
        .always_show_header      = false,
        .settings_visible        = bind(settings),
        .back_button_visible     = AutoVisibility::Collapsed,
        .selection_follows_focus = NavigationShellSelectionFollowsFocus::Enabled,
        .on_item_invoked         = [&](const std::string&) { ++invoked; },
        .control                 = nav,
        .items                   = {ui::NavItem{.text = "Play"},
                                    ui::NavItem{.text = "Actions", .kind = NavEntryKind::Header},
                                    ui::NavItem{.text = "Save", .tool_tip = "Save it"}},
        .footer_items            = {ui::NavItem{.text = "Account"}},
        .auto_suggest_box        = ui::Button{.text = "Search", .control = search},
    });
    REQUIRE(nav != nullptr);
    CHECK(nav->pane_display_mode() == NavigationShellPaneDisplayMode::Top);
    CHECK(nav->header() == "Home");
    CHECK_FALSE(nav->always_show_header());
    CHECK(nav->pane_title() == "Pane Title");
    CHECK(nav->back_button_visible() == AutoVisibility::Collapsed);
    CHECK(nav->selection_follows_focus() == NavigationShellSelectionFollowsFocus::Enabled);
    CHECK(nav->item_count() == 4);
    CHECK(nav->footer_item_count() == 1);
    CHECK(nav->auto_suggest_box() == search);
    CHECK(nav->pane_width() == 320.0f); // the open pane's width
    CHECK(nav->item_row_height() == 40.0f);

    header = "Sample Page 2";
    CHECK(nav->header() == "Sample Page 2");
    settings = false;
    CHECK_FALSE(nav->is_settings_visible());
    nav->set_selected_index(2);
    CHECK(nav->selected_index() == 2);
}

TEST_CASE("ui::Tabs carries tab icons, closability, the width modes and the strip's content", "[spec]")
{
    Property<TabsWidthMode>  mode{TabsWidthMode::Equal};
    ui::Control<ui::Tabs>    tabs;
    std::string              close;
    int                      adds = 0;
    std::unique_ptr<Element> tree = ui::build(ui::Tabs{
        .tab_width_mode            = bind(mode),
        .close_button_overlay_mode = TabsCloseButtonOverlayMode::OnPointerOver,
        .add_tab_button_visible    = false,
        .strip_background          = Color::from_argb(0xFF0078D4),
        .on_tab_close_requested    = [&](const std::string& key) { close = key; },
        .on_add_tab_button_click   = [&] { ++adds; },
        .control                   = tabs,
        .items =
            {
                ui::Tab{.header = "Home", .icon = IconGlyph{0xE80F}, .closable = false, .content = ui::Text{.text = "a"}},
                ui::Tab{.header = "Document", .content = ui::Text{.text = "b"}},
            },
        .tab_strip_header_content = ui::Text{.text = "Header"},
    });
    REQUIRE(tabs != nullptr);
    CHECK(tabs->tab_count() == 2);
    CHECK_FALSE(tabs->closable(0));
    CHECK(tabs->closable(1));
    CHECK(tabs->close_button_overlay_mode() == TabsCloseButtonOverlayMode::OnPointerOver);
    CHECK_FALSE(tabs->is_add_tab_button_visible());
    CHECK(tabs->strip_background().color() == Color::from_argb(0xFF0078D4));
    mode = TabsWidthMode::Compact;
    CHECK(tabs->tab_width_mode() == TabsWidthMode::Compact);
}

TEST_CASE("ui::NavigationShell selects by key, two-way, with Settings as \"settings\"", "[spec][navigation-view]")
{
    Property<std::string>            page;
    std::vector<std::string>         changes;
    ui::Control<ui::NavigationShell> nav;
    std::unique_ptr<Element>         tree = ui::build(ui::NavigationShell{
        .selected_key         = bind(page),
        .on_selection_changed = [&](const std::string& key) { changes.push_back(key); },
        .control              = nav,
        .items                = {ui::NavItem{.text = "Home", .key = "home"},
                                 ui::NavItem{.text = "Mail", .key = "mail"},
                                 ui::NavItem{.text = "Unkeyed"}},
        .footer_items         = {ui::NavItem{.text = "Account", .key = "account"}},
    });
    REQUIRE(nav != nullptr);

    // Empty leaves the first item selected, and the property learns its key.
    CHECK(nav->selected_key() == "home");
    CHECK(page.get() == "home");
    CHECK(changes.empty());

    page = "mail";
    CHECK(nav->selected_index() == 1);
    page = "Unkeyed"; // the text stands in for a missing key
    CHECK(nav->selected_index() == 2);
    page = "account";
    CHECK(nav->selected_key() == "account");
    page = "settings";
    CHECK(nav->settings_selected());
    CHECK(nav->selected_key() == NavigationShellControl::SettingsKey);

    // A key no item has changes nothing, and the property goes back to the selection.
    page = "nowhere";
    CHECK(nav->settings_selected());
    CHECK(page.get() == "settings");

    // The control's own change writes the property and reaches the handler with the key.
    changes.clear();
    nav->set_selected_index(1);
    CHECK(page.get() == "mail");
    CHECK(changes == std::vector<std::string>{"mail"});
}

TEST_CASE("ui::Tabs selects by key two-way, and a tab keeps its key when moved", "[spec][tabs]")
{
    Property<std::string>    current{"doc-2"};
    std::vector<std::string> closes;
    ui::Control<ui::Tabs>    tabs;
    std::unique_ptr<Element> tree = ui::build(ui::Tabs{
        .selected_key           = bind(current),
        .on_tab_close_requested = [&](const std::string& key) { closes.push_back(key); },
        .control                = tabs,
        .items                  = {ui::Tab{.header = "Home", .key = "home"},
                                   ui::Tab{.header = "Document", .key = "doc-1"},
                                   ui::Tab{.header = "Document", .key = "doc-2"}},
    });
    REQUIRE(tabs != nullptr);
    CHECK(tabs->selected_index() == 2);

    // Dragging the selected tab to the front moves its key and the selection with it.
    tabs->move_tab(2, 0);
    CHECK(tabs->key(0) == "doc-2");
    CHECK(tabs->selected_key() == "doc-2");
    CHECK(tabs->index_of("doc-1") == 2);

    tabs->set_selected_index(1);
    CHECK(current.get() == "home");
    current = "doc-1";
    CHECK(tabs->selected_index() == 2);

    // Keyless tabs take their header, numbered after the first.
    tabs->add_tab(ui::Tab{.header = "Untitled"});
    tabs->add_tab(ui::Tab{.header = "Untitled"});
    CHECK(tabs->key(3) == "Untitled");
    CHECK(tabs->key(4) == "Untitled (2)");

    tabs->remove_tab("doc-1");
    CHECK(tabs->index_of("doc-1") == std::nullopt);
    CHECK(tabs->tab_count() == 4);
    CHECK(current.get() == tabs->selected_key()); // the removal moved the selection on
}

TEST_CASE("A bound NavItem text and a bound Tab header follow their properties", "[spec][binding]")
{
    Property<std::string>            inbox{"Inbox"};
    Property<std::string>            nested{"Drafts"};
    ui::Control<ui::NavigationShell> nav;
    std::unique_ptr<Element>         shell = ui::build(ui::NavigationShell{
        .control      = nav,
        .items        = {ui::NavItem{.text = "Home", .key = "home"},
                         ui::NavItem{.text     = bind(inbox),
                                     .key      = "inbox",
                                     .children = {ui::NavItem{.text = bind(nested), .key = "drafts"}}}},
        .footer_items = {ui::NavItem{.text = "Account", .key = "account"}},
    });
    REQUIRE(nav != nullptr);
    CHECK(nav->text_at(1) == "Inbox");
    CHECK(nav->text_at(2) == "Drafts");
    inbox  = "Inbox (3)";
    nested = "Outbox";
    CHECK(nav->text_at(1) == "Inbox (3)");
    CHECK(nav->text_at(2) == "Outbox");
    CHECK(nav->text_at(3) == "Account");
    CHECK(nav->key_at(1) == "inbox");

    Property<std::string>    title{"Draft"};
    ui::Control<ui::Tabs>    tabs;
    std::unique_ptr<Element> view = ui::build(ui::Tabs{
        .control = tabs,
        .items   = {ui::Tab{.header = "Home"}, ui::Tab{.header = bind(title)}},
    });
    REQUIRE(tabs != nullptr);
    CHECK(tabs->header(1) == "Draft");
    tabs->move_tab(1, 0); // the binding follows the tab, by its key
    title = "Report";
    CHECK(tabs->header(0) == "Report");
    CHECK(tabs->key(0) == "Draft");
    CHECK(tabs->header(1) == "Home");

    Property<std::string> added{"New"};
    tabs->add_tab(ui::Tab{.header = bind(added)});
    added = "Renamed";
    CHECK(tabs->header(2) == "Renamed");
}

TEST_CASE("ui::SelectorBar passes its icons through", "[spec]")
{
    ui::Control<ui::SelectorBar> bar;
    std::unique_ptr<Element>     tree = ui::build(ui::SelectorBar{
        .items   = {{"Recent", IconGlyph{0xE823}}, {"Shared", IconGlyph{0xE72D}}},
        .control = bar,
    });
    REQUIRE(bar != nullptr);
    REQUIRE(bar->items().size() == 2);
    CHECK(bar->items()[1].icon.glyph()->codepoint == 0xE72D);
}

TEST_CASE("ui::ListView takes an item template, selection mode, groups and a bound flag", "[spec][collections]")
{
    Property<bool>                 reorder{false};
    ui::Control<ui::ListView<int>> list;
    std::unique_ptr<Element>       tree = ui::build(ui::ListView<int>{
        .items                = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
        .selection_mode       = SelectionMode::Multiple,
        .item_click_enabled   = true,
        .can_reorder_items    = bind(reorder),
        .group_by             = [](const int& i) { return i < 6 ? std::string{"First"} : std::string{"Second"}; },
        .sticky_group_headers = false,
        .border_brush         = [](const Theme& t) { return t.palette.control_strong_stroke_default; },
        .border_thickness     = Thickness{1.0f},
        .control              = list,
        .header_content       = ui::Text{.text = "Header"},
        .item                 = [](const int& i) -> ui::Spec { return ui::Text{.text = std::to_string(i)}; },
    });
    REQUIRE(list != nullptr);
    CHECK(list->item_count() == 12);
    CHECK(list->selection_mode() == SelectionMode::Multiple);
    CHECK(list->is_item_click_enabled());
    CHECK(list->group_count() == 2);
    CHECK(list->group_key(1) == "Second");
    CHECK_FALSE(list->sticky_group_headers());
    CHECK(list->header() != nullptr);
    CHECK_FALSE(list->can_reorder_items());
    reorder = true;
    CHECK(list->can_reorder_items());
}

TEST_CASE("ui::ListView's text header follows its binding; header_content wins over it", "[spec][collections]")
{
    Property<std::string>          title{"Recent files"};
    ui::Control<ui::ListView<int>> list;
    std::unique_ptr<Element>       tree = ui::build(ui::ListView<int>{
        .header  = ui::bind(title),
        .items   = {1, 2, 3},
        .control = list,
        .item    = [](const int& i) -> ui::Spec { return ui::Text{.text = std::to_string(i)}; },
    });
    REQUIRE(list != nullptr);
    CHECK(list->header_text() == "Recent files");
    REQUIRE(list->header() != nullptr);
    CHECK(list->header()->automation_name() == "Recent files");
    title = "";
    CHECK(list->header() == nullptr);

    ui::Control<ui::TileView<int>> tiles;
    std::unique_ptr<Element>       grid = ui::build(ui::TileView<int>{
        .header         = "Ignored",
        .items          = {1, 2},
        .control        = tiles,
        .header_content = ui::Text{.text = "Content"},
        .item           = [](const int& i) -> ui::Spec { return ui::Text{.text = std::to_string(i)}; },
    });
    REQUIRE(tiles != nullptr);
    CHECK(tiles->header_text().empty());
    CHECK(tiles->header() != nullptr);
}

TEST_CASE("a typed ListView and TileView keep their tool tip", "[spec][collections]")
{
    ui::Control<ui::ListView<int>> list;
    ui::Control<ui::TileView<int>> tiles;
    auto                           root = ui::build(ui::VerticalStack{
        .content = {
            ui::ListView<int>{.items    = {1, 2},
                              .tool_tip = "Numbers",
                              .control  = list,
                              .item     = [](const int&) -> ui::Spec { return ui::Text{.text = "x"}; }},
            ui::TileView<int>{.items    = {1, 2},
                              .tool_tip = "Tiles",
                              .control  = tiles,
                              .item     = [](const int&) -> ui::Spec { return ui::Text{.text = "y"}; }},
        }});
    REQUIRE(list != nullptr);
    REQUIRE(tiles != nullptr);
    CHECK(list->tool_tip() == "Numbers");
    CHECK(tiles->tool_tip() == "Tiles");
}

TEST_CASE("ui::TileView, ui::ForEach and ui::TreeView build what they describe", "[spec][collections]")
{
    ui::Control<ui::TileView<int>>      grid;
    ui::Control<ui::ForEach<int>>       repeater;
    ui::Control<ui::TreeView<TreeNode>> tree_view;
    auto                                root = ui::build(ui::VerticalStack{
        .content = {
            ui::TileView<int>{.items                   = {1, 2, 3, 4},
                              .selection_mode          = SelectionMode::Extended,
                              .item_invoked_enabled    = true,
                              .maximum_rows_or_columns = 3,
                              .control                 = grid,
                              .item                    = [](const int&) -> ui::Spec { return ui::Text{.text = "x"}; }},
            ui::ForEach<int>{.items      = {1, 2, 3, 4, 5},
                             .layout     = std::make_unique<TileLayoutEngine>(),
                             .virtualize = true,
                             .control    = repeater,
                             .item       = [](const int&) -> ui::Spec { return ui::Text{.text = "y"}; }},
            ui::TreeView<TreeNode>{.items = {TreeNode{.text = "Root", .children = {TreeNode{.text = "Leaf"}}, .expanded = true}},
                                   .selection_mode = TreeViewSelectionMode::Multiple,
                                   .control        = tree_view},
        }});
    REQUIRE(grid != nullptr);
    CHECK(grid->maximum_rows_or_columns() == 3);
    CHECK(grid->item_count() == 4);
    REQUIRE(repeater != nullptr);
    CHECK(dynamic_cast<TileLayoutEngine*>(repeater->layout()) != nullptr);
    CHECK(repeater->item_count() == 5);
    CHECK(grid->selection_mode() == SelectionMode::Extended);
    CHECK(grid->is_item_invoked_enabled());
    REQUIRE(tree_view != nullptr);
    CHECK(tree_view->selection_mode() == TreeViewSelectionMode::Multiple);
    CHECK(tree_view->visible_row_count() == 2);
}

TEST_CASE("ui::DropdownButton hangs from a menu; ui::SplitButton and ui::ToggleSplitButton build their parts",
          "[spec][split-button]")
{
    ui::Control<ui::DropdownButton> drop;
    ui::Control<ui::MenuFlyout>     menu;
    std::unique_ptr<Element>        tree = ui::build(ui::MenuFlyout{
        .items     = {{.text = "Send"}, {.text = "Reply"}},
        .placement = PopupPlacement::Below,
        .control   = menu,
        .anchor    = ui::DropdownButton{.text = "Email", .control = drop},
    });
    REQUIRE(drop != nullptr);
    REQUIRE(drop->text() == "Email");
    REQUIRE(drop->flyout() == menu);

    Property<bool>                     bullets{true};
    int                                changes = 0;
    int                                clicks  = 0;
    ui::Control<ui::ToggleSplitButton> toggle;
    std::unique_ptr<Element>           split = ui::build(ui::ToggleSplitButton{
        .text               = "Bullets",
        .checked            = bind(bullets),
        .on_click           = [&] { ++clicks; },
        .on_checked_changed = [&](bool) { ++changes; },
        .control            = toggle,
    });
    REQUIRE(toggle->checked());
    REQUIRE(changes == 0); // seeding the value raises nothing
    bullets = false;
    REQUIRE_FALSE(toggle->checked());
    REQUIRE(changes == 0); // nor does a bound write
    ui::testing::set_focused(*toggle, true);
    toggle->handle_key(KeyEvent{Key::Space, KeyAction::Down});
    toggle->handle_key(KeyEvent{Key::Space, KeyAction::Up});
    REQUIRE(toggle->checked());
    REQUIRE(clicks == 1);
    REQUIRE(bullets.get()); // two-way: the click wrote the property
    REQUIRE(changes == 1);  // and the user's change is reported

    // A Property<std::optional<bool>> binds two-way too; indeterminate shows unchecked.
    Property<std::optional<bool>>      mixed{std::nullopt};
    ui::Control<ui::ToggleSplitButton> mixed_toggle;
    std::unique_ptr<Element>           mixed_split = ui::build(
        ui::ToggleSplitButton{.text = "Mixed", .checked = mixed, .control = mixed_toggle});
    REQUIRE_FALSE(mixed_toggle->checked());
    ui::testing::set_focused(*mixed_toggle, true);
    mixed_toggle->handle_key(KeyEvent{Key::Space, KeyAction::Down});
    mixed_toggle->handle_key(KeyEvent{Key::Space, KeyAction::Up});
    REQUIRE(mixed.get() == std::optional<bool>{true});

    ui::Property<bool>           allowed{false};
    ui::Command                  apply{.enabled = allowed, .execute = [] {}};
    ui::Control<ui::SplitButton> plain;
    auto built = ui::build(ui::SplitButton{.text = "Apply", .command = apply, .control = plain});
    REQUIRE(plain->enabled());
    REQUIRE_FALSE(plain->primary_enabled());
    allowed = true;
    REQUIRE(plain->primary_enabled());
}

TEST_CASE("a nullable checked state is two-way: a click writes it back, then reports it", "[spec][binding]")
{
    Property<std::optional<bool>> model{false};
    Property<std::optional<bool>> pressed{false};
    std::optional<bool>           reported;
    std::optional<bool>           model_at_report;
    ui::Control<ui::CheckBox>     box;
    ui::Control<ui::ToggleButton> toggle;
    auto                          tree = ui::build(ui::VerticalStack{
        .content = {
            ui::CheckBox{.checked     = ui::bind(model),
                         .three_state = true,
                         .on_checked_changed =
                             [&](std::optional<bool> s)
                         {
                             reported        = s;
                             model_at_report = model.get();
                         },
                         .control = box},
            ui::ToggleButton{.checked = ui::bind(pressed), .control = toggle},
        }});
    REQUIRE(ui::automation_providers(*box).toggle->toggle_next());
    CHECK(reported == true);
    CHECK(model_at_report == true);
    CHECK(model.get() == true);
    REQUIRE(ui::automation_providers(*box).toggle->toggle_next()); // three-state: on to indeterminate
    CHECK(model.get() == std::nullopt);

    REQUIRE(ui::automation_providers(*toggle).toggle->toggle_next());
    CHECK(pressed.get() == true);

    model   = false; // and the model still drives both
    pressed = false;
    CHECK(box->checked() == false);
    CHECK(toggle->checked() == false);

    // A conversion or bind_one_way() stays one-way.
    Property<bool>                plain{false};
    ui::Control<ui::ToggleButton> one_way;
    auto                          built = ui::build(
        ui::ToggleButton{.checked = ui::bind(plain, [](bool on) { return std::optional<bool>{on}; }), .control = one_way});
    REQUIRE(ui::automation_providers(*one_way).toggle->toggle_next());
    CHECK(one_way->checked() == true);
    CHECK(plain.get() == false);
}

TEST_CASE("a ToggleButton runs its command after the click and follows its enabled state", "[spec][command]")
{
    ui::Property<bool>            allowed{false};
    std::string                   ran_with;
    ui::Command                   apply{.enabled = allowed, .execute = [&](std::string_view p) { ran_with = p; }};
    ui::Control<ui::ToggleButton> toggle;
    auto built = ui::build(ui::ToggleButton{.text = "Pin", .command = apply, .command_parameter = "pin", .control = toggle});
    REQUIRE_FALSE(toggle->is_enabled());
    allowed = true;
    REQUIRE(toggle->is_enabled());
    ui::testing::set_focused(*toggle, true);
    toggle->handle_key(KeyEvent{Key::Space, KeyAction::Down});
    toggle->handle_key(KeyEvent{Key::Space, KeyAction::Up});
    CHECK(toggle->checked() == true);
    CHECK(ran_with == "pin");
}

// A plain value converts into a Binding only when nothing is lost on the way. The refusal is a
// static_assert in Binding's converting constructor, which a trait cannot see, so the rule it
// asserts is checked here.
static_assert(!ui::detail::binding_value_converts<int*, bool>(), "a pointer is not a bool field's value");
static_assert(!ui::detail::binding_value_converts<const char*, bool>());
static_assert(!ui::detail::binding_value_converts<const char (&)[5], bool>());
static_assert(!ui::detail::binding_value_converts<int, bool>());
static_assert(!ui::detail::binding_value_converts<int*, std::optional<bool>>());
static_assert(!ui::detail::binding_value_converts<double, int>());
static_assert(!ui::detail::binding_value_converts<double, std::size_t>());
static_assert(!ui::detail::binding_value_converts<long long, int>());
static_assert(!ui::detail::binding_value_converts<std::size_t, int>());
static_assert(ui::detail::binding_value_converts<const char (&)[5], std::string>());
static_assert(std::is_convertible_v<int, Binding<float>>, "an int literal onto a float field");
static_assert(std::is_convertible_v<double, Binding<float>>);
static_assert(std::is_convertible_v<int, Binding<std::size_t>>, "a non-negative int onto an index");
static_assert(std::is_convertible_v<std::size_t, Binding<std::size_t>>);
static_assert(std::is_convertible_v<bool, Binding<std::optional<bool>>>);
static_assert(std::is_convertible_v<Color, Binding<std::optional<Color>>>);
static_assert(std::is_convertible_v<Binding<float>, Binding<double>>);
static_assert(std::is_convertible_v<const char*, Binding<std::string>>);

TEST_CASE("A Binding takes std::nullopt and int literals for an index", "[spec][binding]")
{
    const Binding<std::optional<std::size_t>> none  = std::nullopt;
    const Binding<std::optional<std::size_t>> third = 2;
    const Binding<float>                      width = 12;
    CHECK(none.initial() == std::nullopt);
    CHECK(third.initial() == 2);
    CHECK(width.initial() == 12.0f);
}

TEST_CASE("a handler with a payload also takes a lambda that takes nothing", "[spec][callback]")
{
    int                      calls   = 0;
    Callback<void(bool)>     toggled = [&] { ++calls; };
    Callback<void(int, int)> moved   = [&] { ++calls; };
    toggled(true);
    moved(1, 2);
    CHECK(calls == 2);

    Control<CheckBox> box;
    auto              node = CheckBox{.text = "Wrap", .on_checked_changed = [&] { ++calls; }, .control = box}.build();
    box->set_checked(true);
    CHECK(calls == 3);
}

TEST_CASE("Every spec's tool_tip reaches its control, and a bound one follows", "[spec][binding]")
{
    Property<std::string>    tip{"First"};
    ui::Control<ui::Text>    label;
    ui::Control<ui::Slider>  slider;
    std::unique_ptr<Element> tree = ui::build(ui::VerticalStack{
        .tool_tip = "Panel",
        .content =
            {
                ui::Text{.text = "Name", .tool_tip = ui::bind(tip), .control = label},
                ui::Slider{.tool_tip = "Volume", .control = slider},
            },
    });
    CHECK(tree->tool_tip() == "Panel");
    CHECK(label->tool_tip() == "First");
    CHECK(slider->tool_tip() == "Volume");
    tip = "Second";
    CHECK(label->tool_tip() == "Second");
}

TEST_CASE("choice lists, icons and pages follow their bindings", "[spec][binding]")
{
    Property<std::vector<std::string>> sizes{std::vector<std::string>{"S", "M"}};
    ui::Control<ui::ComboBox>          dropdown;
    auto                               built = ui::build(ui::ComboBox{.items = ui::bind(sizes), .control = dropdown});
    CHECK(dropdown->items() == std::vector<std::string>{"S", "M"});
    sizes = std::vector<std::string>{"S", "M", "L"};
    CHECK(dropdown->items().size() == 3);

    Property<std::vector<std::string>> trail{std::vector<std::string>{"Home", "Docs"}};
    ui::Control<ui::Breadcrumb>        crumbs;
    auto                               bar = ui::build(ui::Breadcrumb{.items = ui::bind(trail), .control = crumbs});
    trail                                  = std::vector<std::string>{"Home"};
    CHECK(crumbs->items() == std::vector<std::string>{"Home"});

    Property<ui::IconGlyph> glyph{ui::glyphs::More};
    ui::Control<ui::Icon>   icon;
    auto                    drawn = ui::build(ui::Icon{
        .source     = ui::bind(glyph, [](const ui::IconGlyph& g) { return ui::IconSource{g}; }),
        .foreground = [](const Theme& t) { return t.palette.text_fill_secondary; },
        .control    = icon,
    });
    glyph                         = ui::glyphs::Contact;
    CHECK(icon->source().glyph()->codepoint == ui::glyphs::Contact.codepoint);
    CHECK(icon->foreground().resource() != nullptr);

    ui::Control<ui::TextBox> box;
    auto                     field = ui::build(
        ui::TextBox{.foreground = [](const Theme& t) { return t.palette.text_fill_secondary; }, .control = box});
    CHECK(box->foreground().resource() != nullptr);
    box->set_foreground(colors::Black); // a fixed color still converts
    CHECK(box->foreground().color() == colors::Black);

    ui::NavigationFrameControl frame;
    REQUIRE(frame.navigate([] { return ui::Text{.text = "Home"}; }));
    REQUIRE(frame.content() != nullptr);
    CHECK(dynamic_cast<TextControl*>(frame.content()) != nullptr);

    TabsControl tabs;
    Element*    page = tabs.add_tab(ui::Tab{.header = "One", .content = ui::Text{.text = "1"}});
    CHECK(dynamic_cast<TextControl*>(page) != nullptr);
    tabs.insert_tab(0, ui::Tab{.header = "Zero", .closable = false});
    CHECK(tabs.tab_count() == 2);
}

TEST_CASE("one Binding copied into two specs keeps both controls on the property", "[spec][binding][copy]")
{
    Property<std::string>      title{"Untitled"};
    const Binding<std::string> shared = ui::bind(title);
    ui::Control<ui::Text>      first;
    ui::Control<ui::Text>      second;
    auto                       a = ui::build(ui::Text{.text = shared, .control = first});
    auto                       b = ui::build(ui::Text{.text = shared, .control = second});
    CHECK(first->text() == "Untitled");

    title = "Report";
    CHECK(first->text() == "Report");
    CHECK(second->text() == "Report");

    a.reset(); // the other control stays bound
    title = "Draft";
    CHECK(second->text() == "Draft");
}

TEST_CASE("Specs and list entries take designated initializers only", "[spec]")
{
    // The first member of each is a detail::DesignatedOnly nothing converts to, so a positional
    // `{"OK"}` stops there instead of filling whichever field comes first.
    STATIC_REQUIRE_FALSE(std::is_constructible_v<ui::Button, const char*>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<ui::TableViewColumn, const char*, float>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<ui::TreeNode, const char*>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<ui::MenuItem, const char*>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<ui::WindowOptions, const char*>);
    const ui::TableViewColumn column{.header = "Name", .width = 160.0f};
    CHECK(column.header == "Name");
}

TEST_CASE("A view names its item type and takes its collection as written", "[spec]")
{
    ui::Property<std::vector<std::string>> fruits{std::vector<std::string>{"Apple", "Pear"}};
    ObservableVector<std::string>          names{"Ada", "Grace"};
    ui::Control<ui::ListView<std::string>> from_property;
    ui::Control<ui::ListView<std::string>> from_collection;

    std::unique_ptr<Element> a = ui::build(ui::ListView<std::string>{.items = fruits, .control = from_property});
    std::unique_ptr<Element> b = ui::build(ui::ListView<std::string>{.items = names, .control = from_collection});
    REQUIRE(from_property->item_count() == 2);
    REQUIRE(from_collection->item_count() == 2);

    fruits = std::vector<std::string>{"Apple", "Pear", "Plum"};
    names.push_back("Alan");
    CHECK(from_property->item_count() == 3);
    CHECK(from_collection->item_count() == 3);
}

TEST_CASE("A handler returning an outcome takes a lambda that returns nothing, which gets the default",
          "[spec][callback]")
{
    int                  calls    = 0;
    Callback<Handling()> shortcut = [&] { ++calls; };
    CHECK(detail::handled(shortcut(), true));        // a target asked to act: handled
    CHECK_FALSE(detail::handled(shortcut(), false)); // an observer: the key goes on
    Callback<Handling()> passes = [] { return ui::NotHandled; };
    CHECK_FALSE(detail::handled(passes(), true));

    Callback<Decision(const PopupClosingArgs&)> closing = [&] { ++calls; };
    CHECK_FALSE(detail::canceled(closing(PopupClosingArgs{})));
    Callback<Decision(const PopupClosingArgs&)> keeps = [](const PopupClosingArgs& args)
    { return args.reason == PopupCloseReason::LightDismiss ? ui::Cancel : ui::Proceed; };
    CHECK(detail::canceled(keeps(PopupClosingArgs{.reason = PopupCloseReason::LightDismiss})));
    CHECK_FALSE(detail::canceled(keeps(PopupClosingArgs{})));
    CHECK(calls == 3);
}

TEST_CASE("A whole number fills a float field and a margin without a conversion warning", "[spec][binding]")
{
    // The test build treats warnings as errors: these lines compiling is half the check.
    ui::Control<ui::Text>    text;
    std::unique_ptr<Element> tree = ui::build(ui::Text{.text = "x", .margin = 24, .width = 300, .control = text});
    CHECK(text->margin() == Thickness{24.0f});
}

TEST_CASE("A whole number fills an optional float option field and setter", "[spec][binding]")
{
    // As above, these lines compiling under warnings-as-errors is half the check.
    const ui::Style      wide{.width = 300, .max_width = 50};
    const ui::TileLayout tiles{.min_item_width = 90, .min_item_height = std::nullopt};
    CHECK(wide.width == 300.0f);
    CHECK(tiles.min_item_width == 90);
    CHECK(tiles.min_item_height == std::nullopt);
    CHECK_FALSE(tiles.min_item_height);
    CHECK(tiles.min_item_width.value_or(0.0f) == 90.0f);

    ui::Control<ui::Text>    text;
    std::unique_ptr<Element> tree = ui::build(ui::Text{.text = "x", .style = wide, .control = text});
    CHECK(text->width() == 300.0f);
    text->set_width(120);
    CHECK(text->width() == 120.0f);
    text->set_width(std::nullopt);
    CHECK_FALSE(text->width());

    const std::optional<float> unset;
    CHECK(OptionalFloat{} == unset);
    CHECK(unset == OptionalFloat{});
    CHECK(OptionalFloat{2} != OptionalFloat{});
    CHECK(OptionalFloat{unset} == std::nullopt);
}

TEST_CASE("on_key_down sees a key before its control and after the focused ones inside", "[spec][input]")
{
    testing::HeadlessUI      ui;
    std::vector<std::string> seen;
    int                      clicks = 0;
    ui::Control<ui::Button>  button;
    ui.window().set_content(ui::VerticalStack{
        .on_key_down =
            [&](const KeyEvent& e)
        {
            seen.push_back("stack");
            return e.key == Key::Escape ? Handled : NotHandled;
        },
        .content = {ui::Button{.text     = "Go",
                               .on_click = [&] { ++clicks; },
                               .on_key_down =
                                   [&](const KeyEvent& e)
                               {
                                   seen.push_back("button");
                                   return e.key == Key::Space ? Handled : NotHandled; // keeps Space from the button
                               },
                               .control = button}},
    });
    ui.layout({200, 100});
    ui.window().focus_content();
    REQUIRE(ui.window().focused() == button);

    CHECK(testing::seams(ui.window()).handle_key({Key::Escape, KeyAction::Down}));
    CHECK(seen == std::vector<std::string>{"button", "stack"});

    seen.clear();
    (void)testing::seams(ui.window()).handle_key({Key::Space, KeyAction::Down});
    (void)testing::seams(ui.window()).handle_key({Key::Space, KeyAction::Up});
    CHECK(seen == std::vector<std::string>{"button"}); // the release is not a press
    CHECK(clicks == 0);
}

TEST_CASE("on_wheel sees a turn before its control scrolls, and can take it", "[spec][input]")
{
    testing::HeadlessUI     ui;
    int                     zooms = 0;
    ui::Control<ui::Scroll> scroll;
    ui.window().set_content(ui::Scroll{
        .height = 100,
        .on_wheel =
            [&](const PointerEvent& e)
        {
            if (!e.modifiers.ctrl)
            {
                return NotHandled;
            }
            ++zooms;
            return Handled;
        },
        .control = scroll,
        .content = ui::VerticalStack{.height = 1000},
    });
    ui.layout({200, 100});

    PointerEvent turn{.position = {50, 50}, .action = PointerAction::Wheel, .wheel_delta = 40};
    turn.modifiers.ctrl = true;
    testing::seams(ui.window()).handle_pointer(turn);
    CHECK(zooms == 1);
    CHECK(scroll->vertical_offset() == 0.0f);

    turn.modifiers.ctrl = false;
    testing::seams(ui.window()).handle_pointer(turn);
    CHECK(zooms == 1);
    CHECK(scroll->vertical_offset() > 0.0f);
}

TEST_CASE("focus() moves focus to a control or into a panel, and push_focus() gives it back", "[spec][focus]")
{
    testing::HeadlessUI            ui;
    ui::Control<ui::Button>        first;
    ui::Control<ui::Button>        second;
    ui::Control<ui::VerticalStack> view;
    Property<bool>                 view_shown{false};
    ui.window().set_content(ui::VerticalStack{
        .content = {ui::Button{.text = "One", .control = first},
                    ui::Button{.text = "Two", .control = second},
                    ui::VerticalStack{.visible = ui::bind(view_shown),
                                      .control = view,
                                      .content = {ui::Button{.text = "In the view"}}}},
    });
    ui.layout({200, 200});

    CHECK(second->focus());
    CHECK(ui.window().focused() == second);
    CHECK_FALSE(view->focus()); // hidden: nothing in it is a stop
    CHECK(ui.window().focused() == second);

    view_shown = true;
    CHECK(ui.window().push_focus(*view)); // a panel: its first stop gets focus
    CHECK(ui.window().focused() != second);
    CHECK(ui.window().focused()->parent() == view);
    CHECK(ui.window().pop_focus());
    CHECK(ui.window().focused() == second);
    CHECK_FALSE(ui.window().pop_focus()); // nothing left to give back

    ButtonControl orphan{"Not in a window"};
    CHECK_FALSE(orphan.focus());
}

TEST_CASE("A host with nothing visible in it lets the pointer through to what is behind", "[spec][input]")
{
    testing::HeadlessUI   ui;
    int                   clicks = 0;
    Property<bool>        overlay_shown{false};
    ui::Control<ui::Fill> overlay;
    ui.window().set_content(ui::Grid{
        .content = {ui::Cell{.content = ui::Button{.text = "Under", .on_click = [&] { ++clicks; }}},
                    ui::Cell{.content = ui::Fill{.control = overlay,
                                                 .content = ui::Text{.text = "Over", .visible = ui::bind(overlay_shown)}}}},
    });
    ui.layout({200, 100});
    const Point center = overlay->bounds().center();
    auto        click  = [&]
    {
        testing::seams(ui.window()).handle_pointer({center, PointerButton::Left, PointerAction::Down});
        testing::seams(ui.window()).handle_pointer({center, PointerButton::Left, PointerAction::Up});
    };

    click();
    CHECK(clicks == 1); // the collapsed text leaves the Fill empty

    overlay_shown = true;
    ui.layout({200, 100});
    click();
    CHECK(clicks == 1); // now the Fill and its text take it
}

TEST_CASE("The typed views keep their on_key_down and on_wheel", "[spec][collections][input]")
{
    int                            keys   = 0;
    int                            wheels = 0;
    ui::Control<ui::ListView<int>> list;
    ui::Control<ui::TileView<int>> tiles;
    std::unique_ptr<Element>       a = ui::build(ui::ListView<int>{
        .items       = {1, 2, 3},
        .on_key_down = [&](const KeyEvent&) { ++keys; },
        .on_wheel    = [&](const PointerEvent&) { ++wheels; },
        .control     = list,
        .item        = [](const int& i) -> ui::Spec { return ui::Text{.text = std::to_string(i)}; },
    });
    std::unique_ptr<Element>       b = ui::build(ui::TileView<int>{
        .items       = {1, 2, 3},
        .on_key_down = [&](const KeyEvent&) { ++keys; },
        .on_wheel    = [&](const PointerEvent&) { ++wheels; },
        .control     = tiles,
        .item        = [](const int& i) -> ui::Spec { return ui::Text{.text = std::to_string(i)}; },
    });

    const PointerEvent turn{.action = PointerAction::Wheel, .wheel_delta = 40};
    (void)list->handle_key({Key::Escape, KeyAction::Down});
    (void)list->raise_wheel(turn);
    (void)tiles->handle_key({Key::Escape, KeyAction::Down});
    (void)tiles->raise_wheel(turn);
    CHECK(keys == 2);
    CHECK(wheels == 2);
}

TEST_CASE("A spec's change handler reports the user's changes, not bound ones", "[spec][binding]")
{
    // A one-way binding: the box follows the property and the handler stays quiet.
    Property<bool>            agreed{false};
    std::vector<bool>         heard;
    ui::Control<ui::CheckBox> box;
    std::unique_ptr<Element>  tree = ui::build(ui::CheckBox{
        .text               = "Agree",
        .checked            = ui::bind(agreed, [](bool v) { return std::optional<bool>{v}; }),
        .on_checked_changed = [&](std::optional<bool> v) { heard.push_back(v.value_or(false)); },
        .control            = box,
    });
    agreed                         = true;
    CHECK(box->checked() == true);
    CHECK(heard.empty());
    box->set_checked(false); // code on the control itself is not a bound write
    CHECK(heard == std::vector<bool>{false});

    // New items clear the selection: the two-way property hears of it, the handler doesn't.
    Property<std::vector<std::string>>   sizes{std::vector<std::string>{"Small", "Large"}};
    Property<std::optional<std::size_t>> picked{std::size_t{1}};
    int                                  fired = 0;
    ui::Control<ui::ComboBox>            combo;
    std::unique_ptr<Element>             list = ui::build(ui::ComboBox{
        .items                = ui::bind(sizes),
        .selected_index       = ui::bind(picked),
        .on_selection_changed = [&](std::optional<std::size_t>) { ++fired; },
        .control              = combo,
    });
    sizes                                     = std::vector<std::string>{"S", "M", "L"};
    CHECK(combo->selected_index() == picked.get());
    CHECK(fired == 0);
    combo->set_selected_index(2);
    CHECK(picked.get() == 2);
    CHECK(fired == 1);
}

TEST_CASE("bind(source, to, from) is two-way through a conversion", "[spec][binding]")
{
    Property<bool>            any{false};
    int                       writes = 0;
    const Subscription        watch  = any.observe([&writes](bool) { ++writes; });
    ui::Control<ui::ComboBox> combo;
    std::unique_ptr<Element>  tree = ui::build(ui::ComboBox{
        .items          = {"All of these", "Any of these"},
        .selected_index = ui::bind(
            any,
            [](bool a) { return std::optional<std::size_t>{a ? std::size_t{1} : std::size_t{0}}; },
            [](const std::optional<std::size_t>& index) { return index == std::size_t{1}; }),
        .control = combo,
    });
    CHECK(combo->selected_index() == 0);

    any = true; // the model shows in the control
    CHECK(combo->selected_index() == 1);
    CHECK(writes == 1);

    combo->set_selected_index(0); // and the control's change reaches the model in its own type
    CHECK(any.get() == false);
    CHECK(writes == 2);
    combo->set_selected_index(std::nullopt); // `from` gives false again: the gate stops it
    CHECK(any.get() == false);
    CHECK(writes == 2);
}

TEST_CASE("A bound range clamps the value, and a two-way value follows", "[spec][binding]")
{
    Property<double>         low{0.0};
    Property<double>         high{100.0};
    Property<double>         level{80.0};
    int                      heard = 0;
    ui::Control<ui::Slider>  slider;
    std::unique_ptr<Element> a = ui::build(ui::Slider{
        .value            = ui::bind(level),
        .min              = ui::bind(low),
        .max              = ui::bind(high),
        .on_value_changed = [&heard](double) { ++heard; },
        .control          = slider,
    });
    high                       = 50.0;
    CHECK((slider->max)() == 50.0);
    CHECK(slider->value() == 50.0);
    CHECK(level.get() == 50.0); // the property follows the clamp
    CHECK(heard == 0);          // which isn't the user's change

    // A min moved past the max and back leaves the max as bound.
    low = 70.0;
    CHECK((slider->min)() == 70.0);
    low = 10.0;
    CHECK((slider->max)() == 50.0);

    Property<double>                top{10.0};
    Property<std::optional<double>> count{std::optional<double>{8.0}};
    ui::Control<ui::NumberBox>      box;
    std::unique_ptr<Element> b = ui::build(ui::NumberBox{.value = ui::bind(count), .max = ui::bind(top), .control = box});
    top = 5.0;
    CHECK(box->value() == 5.0);
    CHECK(count.get() == 5.0);

    Property<double>             total{200.0};
    ui::Control<ui::ProgressBar> bar;
    std::unique_ptr<Element>     c = ui::build(ui::ProgressBar{.value = 50.0, .max = ui::bind(total), .control = bar});
    total                          = 40.0;
    CHECK(bar->value() == 40.0);
}

namespace
{
struct TextChoice
{
    Property<std::string> text;
};
} // namespace

TEST_CASE("ui::bind_each() lists one Property of each item and follows both", "[spec][binding]")
{
    ObservableVector<TextChoice> choices;
    choices.emplace_back().text = "Small";
    ui::Control<ui::ComboBox> combo;
    std::unique_ptr<Element>  tree = ui::build(
        ui::ComboBox{.items = ui::bind_each(choices, &TextChoice::text), .control = combo});
    CHECK(combo->items() == std::vector<std::string>{"Small"});

    choices.emplace_back().text = "Large"; // a new item, and then its text
    CHECK(combo->items() == std::vector<std::string>{"Small", "Large"});
    choices[0].text = "Tiny"; // an edit inside an item
    CHECK(combo->items() == std::vector<std::string>{"Tiny", "Large"});
    choices.move(1, 0);
    CHECK(combo->items() == std::vector<std::string>{"Large", "Tiny"});
    choices.erase(0);
    choices[0].text = "Only"; // the removed item's edge is gone, the rest still follow
    CHECK(combo->items() == std::vector<std::string>{"Only"});
}
