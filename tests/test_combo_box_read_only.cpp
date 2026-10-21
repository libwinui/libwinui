// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/VisualStates.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;

namespace
{
PointerEvent move(Point p)
{
    return {p, PointerButton::None, PointerAction::Move};
}
PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}

// Hit points derive from the measured geometry, since the box height depends on the measurer.
// Returns a unique_ptr because the constructor points the inner TextBox back at the control, so a
// moved value would leave that back-pointer dangling.
std::unique_ptr<ComboBoxControl> make_read_only()
{
    auto d = std::make_unique<ComboBoxControl>(std::vector<std::string>{"Light", "Dark", "High contrast"}, 0);
    HeadlessTextMeasurer measurer;
    const Size           closed = d->measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    d->arrange({100.0f, 100.0f, closed.width, closed.height});
    return d;
}

Point header_point(const ComboBoxControl& d)
{
    return d.bounds().center();
}

Point item_point(const ComboBoxControl& d, std::size_t index)
{
    const Rect  b     = d.bounds();
    const float row_h = b.height;
    return Point{b.center().x, b.bottom() + row_h * (static_cast<float>(index) + 0.5f)};
}

const Point Outside{-500.0f, -500.0f};

// The popup only gets positioned by sync_popups() inside layout(), so tests call relayout() after
// a toggle, as a real host does on the next paint.
struct ReadOnlyWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    ComboBoxControl*     combo;

    explicit ReadOnlyWindow(std::vector<std::string> items)
    {
        auto owned = std::make_unique<ComboBoxControl>(std::move(items), 0);
        combo      = owned.get();
        window.set_stretch_content(false); // laid out for the control at its desired size, centered
        window.set_content(std::move(owned));
        window.layout({400.0f, 400.0f}, measurer);
    }

    void relayout()
    {
        window.layout({400.0f, 400.0f}, measurer);
    }
};
} // namespace

TEST_CASE("A new read-only ComboBox starts closed on item 0, Normal, and enabled", "[combo_box]")
{
    const auto       owned = make_read_only();
    ComboBoxControl& d     = *owned;
    REQUIRE(d.enabled());
    REQUIRE_FALSE(d.expanded());
    REQUIRE(d.selected_index() == 0);
    REQUIRE(d.text() == "Light");
    REQUIRE(ui::testing::visual_state(d) == ButtonState::Normal);
}

TEST_CASE("Constructing with a selected_index starts on that item", "[combo_box]")
{
    ComboBoxControl d{{"Light", "Dark"}, 1};
    REQUIRE(d.selected_index() == 1);
    REQUIRE(d.text() == "Dark");
}

TEST_CASE("Out-of-range selected_index at construction clamps to the last item", "[combo_box]")
{
    ComboBoxControl d{{"Light", "Dark"}, 99};
    REQUIRE(d.selected_index() == 1);
}

TEST_CASE("Clicking a read-only box anywhere toggles expanded", "[combo_box][command]")
{
    const auto       owned = make_read_only();
    ComboBoxControl& d     = *owned;
    const Point      h     = header_point(d);

    d.handle_pointer(down(h));
    d.handle_pointer(up(h));
    REQUIRE(d.expanded());

    d.handle_pointer(down(h));
    d.handle_pointer(up(h));
    REQUIRE_FALSE(d.expanded());
}

TEST_CASE("Clicking an item in a read-only box's popup selects it, fires the callbacks, and closes",
          "[combo_box][command][popup]")
{
    ReadOnlyWindow                          w{{"Light", "Dark", "High contrast"}};
    std::vector<std::optional<std::size_t>> observed;
    w.combo->on_selection_changed([&](std::optional<std::size_t> i) { observed.push_back(i); });

    const Point h = header_point(*w.combo);
    ui::testing::seams(w.window).handle_pointer(down(h));
    ui::testing::seams(w.window).handle_pointer(up(h));
    w.relayout();
    REQUIRE(w.combo->expanded());

    const Point row1 = item_point(*w.combo, 1); // "Dark"
    ui::testing::seams(w.window).handle_pointer(down(row1));
    ui::testing::seams(w.window).handle_pointer(up(row1));

    REQUIRE_FALSE(w.combo->expanded());
    REQUIRE(w.combo->selected_index() == 1);
    REQUIRE(w.combo->text() == "Dark");
    REQUIRE(observed == std::vector<std::optional<std::size_t>>{1});
}

TEST_CASE("A pointer-down outside a read-only box's popup light-dismisses it", "[combo_box][popup]")
{
    ReadOnlyWindow w{{"Light", "Dark", "High contrast"}};

    const Point h = header_point(*w.combo);
    ui::testing::seams(w.window).handle_pointer(down(h));
    ui::testing::seams(w.window).handle_pointer(up(h));
    w.relayout();
    REQUIRE(w.combo->expanded());

    ui::testing::seams(w.window).handle_pointer(down(Outside));
    REQUIRE_FALSE(w.combo->expanded());
}

TEST_CASE("Expanding a read-only ComboBox inside a StackControl does not reflow a sibling below it",
          "[combo_box][popup]")
{
    auto combo_owned = std::make_unique<ComboBoxControl>(std::vector<std::string>{"Light", "Dark", "High contrast"}, 0);
    ComboBoxControl* d            = combo_owned.get();
    auto             button_owned = std::make_unique<ButtonControl>("Below");
    ButtonControl*   btn          = button_owned.get();

    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->add(std::move(combo_owned));
    stack->add(std::move(button_owned));

    HeadlessTextMeasurer measurer;
    Window               window;
    window.set_content(std::move(stack));
    window.layout({400.0f, 400.0f}, measurer);
    const float button_top_before = btn->bounds().top();

    ui::testing::seams(window).handle_pointer(down(header_point(*d)));
    ui::testing::seams(window).handle_pointer(up(header_point(*d)));
    window.layout({400.0f, 400.0f}, measurer);

    REQUIRE(d->expanded());
    REQUIRE(btn->bounds().top() == button_top_before);
}

TEST_CASE("Re-selecting the current item does not fire on_selection_changed", "[combo_box][command]")
{
    const auto       owned = make_read_only();
    ComboBoxControl& d     = *owned;
    int              fired = 0;
    d.on_selection_changed([&](std::optional<std::size_t>) { ++fired; });

    d.set_selected_index(0);
    REQUIRE(fired == 0);

    d.set_selected_index(2);
    REQUIRE(fired == 1);
}

TEST_CASE("Release outside the read-only box cancels the open/close toggle", "[combo_box][command]")
{
    const auto       owned = make_read_only();
    ComboBoxControl& d     = *owned;

    d.handle_pointer(down(header_point(d)));
    d.handle_pointer(move(Outside));
    d.handle_pointer(up(Outside));

    REQUIRE_FALSE(d.expanded());
}

TEST_CASE("Space opens/closes while focused; Enter only commits while open", "[combo_box][command]")
{
    const auto       owned = make_read_only();
    ComboBoxControl& d     = *owned;
    ui::testing::set_focused(d, true);

    d.handle_key({Key::Enter, KeyAction::Down});
    d.handle_key({Key::Enter, KeyAction::Up});
    REQUIRE_FALSE(d.expanded());

    d.handle_key({Key::Space, KeyAction::Down});
    d.handle_key({Key::Space, KeyAction::Up});
    REQUIRE(d.expanded());

    d.handle_key({Key::ArrowDown, KeyAction::Down});
    d.handle_key({Key::Enter, KeyAction::Down});
    REQUIRE_FALSE(d.expanded());
    REQUIRE(d.selected_index() == 1);
}

TEST_CASE("Escape closes a read-only box's list without changing the selection", "[combo_box][command]")
{
    const auto       owned = make_read_only();
    ComboBoxControl& d     = *owned;
    ui::testing::set_focused(d, true);
    d.handle_key({Key::Space, KeyAction::Down});
    d.handle_key({Key::Space, KeyAction::Up});
    d.handle_key({Key::ArrowDown, KeyAction::Down});

    d.handle_key({Key::Escape, KeyAction::Down});

    REQUIRE_FALSE(d.expanded());
    REQUIRE(d.selected_index() == 0);
}

TEST_CASE("Arrow keys change selection directly while closed, clamped at the ends", "[combo_box][command]")
{
    const auto       owned = make_read_only();
    ComboBoxControl& d     = *owned;
    ui::testing::set_focused(d, true);

    d.handle_key({Key::ArrowUp, KeyAction::Down}); // already at 0, stays clamped
    REQUIRE(d.selected_index() == 0);

    d.handle_key({Key::ArrowDown, KeyAction::Down});
    d.handle_key({Key::ArrowDown, KeyAction::Down});
    REQUIRE(d.selected_index() == 2);

    d.handle_key({Key::ArrowDown, KeyAction::Down}); // already at the last item
    REQUIRE(d.selected_index() == 2);
}

TEST_CASE("Losing focus closes a read-only box's open list", "[combo_box][state]")
{
    const auto       owned = make_read_only();
    ComboBoxControl& d     = *owned;
    ui::testing::set_focused(d, true);
    d.handle_key({Key::Space, KeyAction::Down});
    d.handle_key({Key::Space, KeyAction::Up});
    REQUIRE(d.expanded());

    ui::testing::set_focused(d, false);
    REQUIRE_FALSE(d.expanded());
}

TEST_CASE("A disabled read-only ComboBox ignores input and cannot be focused", "[combo_box][state]")
{
    const auto       owned = make_read_only();
    ComboBoxControl& d     = *owned;
    d.set_enabled(false);

    d.handle_pointer(down(header_point(d)));
    d.handle_pointer(up(header_point(d)));
    REQUIRE_FALSE(d.expanded());
    REQUIRE_FALSE(d.focusable());
    REQUIRE(ui::testing::visual_state(d) == ButtonState::Disabled);
}

TEST_CASE("A read-only ComboBox with no items cannot be focused or expanded", "[combo_box][state]")
{
    ComboBoxControl d;
    REQUIRE_FALSE(d.focusable());
    REQUIRE(d.text().empty());

    ui::testing::set_focused(d, true);
    d.handle_key({Key::Space, KeyAction::Down});
    d.handle_key({Key::Space, KeyAction::Up});
    REQUIRE_FALSE(d.expanded());
}

// ---- MDL2 fills the selected popup row instead of marking it with a pill ------------------

TEST_CASE("MDL2's open read-only ComboBox fills its selected row and draws no pill", "[combo_box][popup][render][mdl2]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});
    REQUIRE(ui::detail::controls(theme).row_selection_fills.has_value());

    ReadOnlyWindow w{{"Light", "Dark", "System"}};
    w.window.pin_theme(theme);
    w.combo->set_selected_index(1);
    const Point h = header_point(*w.combo);
    ui::testing::seams(w.window).handle_pointer(down(h));
    ui::testing::seams(w.window).handle_pointer(up(h));
    w.relayout();
    REQUIRE(w.combo->expanded());

    TestRenderer r;
    w.window.render(r);

    // The era's pill is 3 wide (`draw_selection_pill`'s Width); MDL2 marks the row with a fill.
    std::vector<FillRoundedRect> selection_fills;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c))
        {
            CHECK(f->bounds.width != 3.0f);
            // Opening makes the selected item the hot one, so it may take the selected hover
            // rung rather than the resting one. Accept either.
            if (f->color == ui::detail::controls(theme).row_selection_fills->popup_selected ||
                f->color == ui::detail::controls(theme).row_selection_fills->popup_selected_hover)
            {
                selection_fills.push_back(*f);
            }
        }
    }
    // The selected row takes the era's accent rung, at 40 % in Light.
    REQUIRE(selection_fills.size() == 1);
    CHECK(selection_fills[0].color.a < 1.0f);
    CHECK(selection_fills[0].bounds.width > 3.0f);
}

TEST_CASE("Fluent 2's open read-only ComboBox still marks its selected row with a 3px pill",
          "[combo_box][popup][render]")
{
    const Theme theme = resolve_theme(ThemeRequest{}, OsThemeSnapshot{});
    REQUIRE_FALSE(ui::detail::controls(theme).row_selection_fills.has_value());

    ReadOnlyWindow w{{"Light", "Dark", "System"}};
    w.window.pin_theme(theme);
    w.combo->set_selected_index(1);
    const Point h = header_point(*w.combo);
    ui::testing::seams(w.window).handle_pointer(down(h));
    ui::testing::seams(w.window).handle_pointer(up(h));
    w.relayout();
    REQUIRE(w.combo->expanded());

    TestRenderer r;
    w.window.render(r);

    std::size_t pills = 0;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->bounds.width == 3.0f)
        {
            ++pills;
        }
    }
    CHECK(pills == 1);
}

TEST_CASE("A read-only ComboBox can start with nothing selected, showing its placeholder", "[combo_box][render]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    ComboBoxControl d{{"Transparent", "LightGray"}, std::nullopt};
    d.set_placeholder_text("Pick a color");
    REQUIRE(d.selected_index() == std::nullopt);
    REQUIRE(d.text().empty());

    const Size desired = d.measure({Unbounded, Unbounded}, m, theme);
    d.arrange({0.0f, 0.0f, desired.width, desired.height});
    TestRenderer r;
    d.render(r, theme);
    bool placeholder = false;
    for (const auto& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t != nullptr && t->text == "Pick a color")
        {
            placeholder = true;
            REQUIRE(t->color.a < theme.button_normal.text.a); // the secondary text color
        }
    }
    REQUIRE(placeholder);

    // Either arrow lands on the first item; clearing again reports None, once.
    std::size_t                fired = 0;
    std::optional<std::size_t> last{0};
    d.on_selection_changed(
        [&](std::optional<std::size_t> index)
        {
            ++fired;
            last = index;
        });
    ui::testing::set_focused(d, true);
    d.handle_key({Key::ArrowUp, KeyAction::Down});
    REQUIRE(d.selected_index() == 0);
    REQUIRE(fired == 1);
    d.set_selected_index(std::nullopt);
    REQUIRE(d.selected_index() == std::nullopt);
    REQUIRE(fired == 2);
    REQUIRE(last == std::nullopt);
    d.set_selected_index(std::nullopt);
    REQUIRE(fired == 2);
}

TEST_CASE("A read-only ComboBox starts with nothing selected, and set_items() clears the selection", "[combo_box]")
{
    ComboBoxControl empty;
    CHECK(empty.selected_index() == std::nullopt);
    CHECK(ComboBoxControl{{}, 0}.selected_index() == std::nullopt);
    CHECK(ComboBoxControl{{"A", "B"}}.selected_index() == std::nullopt);

    ComboBoxControl                         d{{"A", "B", "C"}, 2};
    std::vector<std::optional<std::size_t>> seen;
    d.on_selection_changed([&](std::optional<std::size_t> index) { seen.push_back(index); });

    d.set_items({"X", "Y"});
    CHECK(d.selected_index() == std::nullopt);
    CHECK(d.text().empty());
    CHECK(seen == std::vector<std::optional<std::size_t>>{std::nullopt});

    // Nothing was selected: nothing to report.
    d.set_items({"Z"});
    CHECK(seen.size() == 1);
}

TEST_CASE("ui::ComboBox shows its placeholder until something is picked", "[combo_box][spec]")
{
    ui::Control<ui::ComboBox> combo;
    std::unique_ptr<Element>  tree = ui::build(
        ui::ComboBox{.placeholder_text = "Pick one", .items = {"A", "B", "C"}, .control = combo});
    CHECK(combo->selected_index() == std::nullopt);
    CHECK(combo->text().empty());
}

TEST_CASE("A read-only ComboBox bound to a property writes None back when cleared", "[combo_box][spec]")
{
    ui::Property<std::optional<std::size_t>> index{1};
    ui::Control<ui::ComboBox>                combo;
    std::unique_ptr<Element>                 tree = ui::build(
        ui::ComboBox{.items = {"A", "B", "C"}, .selected_index = ui::bind(index), .control = combo});
    REQUIRE(combo->selected_index() == 1);

    combo->set_selected_index(std::nullopt);
    CHECK(index.get() == std::nullopt);

    combo->set_selected_index(2);
    combo->set_items({});
    CHECK(index.get() == std::nullopt);
}

TEST_CASE("An Aero drop-down list is one push button with a triangle, and its list is square and highlighted",
          "[combo_box][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    REQUIRE(ui::detail::controls(theme).dropdown_chrome.has_value());

    ComboBoxControl      d{{"Light", "Dark", "High contrast"}, 0};
    HeadlessTextMeasurer measurer;
    const Size           size = d.measure({Unbounded, Unbounded}, measurer, theme);
    d.arrange({0.0f, 0.0f, size.width, size.height});
    d.set_selected_index(1);

    const auto render = [&](const Element& e)
    {
        TestRenderer r;
        e.render(r, theme);
        return r;
    };

    TestRenderer closed = render(d);
    bool         face   = false;
    for (const RenderCommand& c : closed.commands)
    {
        if (const auto* g = std::get_if<FillRoundedRectGradient>(&c);
            g && g->brush == *theme.button_normal.background_gradient)
        {
            face = true;
        }
    }
    CHECK(face);
    REQUIRE(closed.count_of<FillPolygon>() == 1);
    for (const RenderCommand& c : closed.commands)
    {
        if (const auto* p = std::get_if<FillPolygon>(&c))
        {
            CHECK(p->points.size() == 3);
            CHECK(p->fill == colors::Black);
        }
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            CHECK(t->text == "Dark"); // the label only, no chevron glyph
        }
    }

    // Open: the selected row is highlighted in #3399FF until the pointer moves.
    ui::automation_providers(d).expand_collapse->set_expanded_state(true);
    TestRenderer open    = render(d);
    bool         pressed = false;
    for (const RenderCommand& c : open.commands)
    {
        if (const auto* g = std::get_if<FillRoundedRectGradient>(&c);
            g && g->brush == *theme.button_pressed.background_gradient)
        {
            pressed = true;
        }
    }
    CHECK(pressed);

    Element* popup = ui::testing::popup_content(d);
    REQUIRE(popup != nullptr);
    popup->arrange({0.0f, size.height, size.width, 60.0f});
    TestRenderer list        = render(*popup);
    bool         highlighted = false;
    for (const RenderCommand& c : list.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->color == Color::from_argb(0xFF3399FF))
        {
            highlighted = true;
        }
        if (const auto* t = std::get_if<DrawTextRun>(&c); t && t->text == "Dark")
        {
            CHECK(t->color == colors::White);
        }
        if (const auto* s = std::get_if<StrokeRoundedRect>(&c))
        {
            CHECK(s->radius == 0.0f);
            CHECK(s->color == Color::from_argb(0xFF646464));
        }
    }
    CHECK(highlighted);
}

TEST_CASE("A high-contrast read-only ComboBox strokes its outline", "[combo_box][render][high-contrast]")
{
    for (ThemeVariant v : {ThemeVariant::Fluent2, ThemeVariant::Fluent, ThemeVariant::Mdl2})
    {
        CAPTURE(static_cast<int>(v));
        const Theme theme = resolve_theme(ThemeRequest{.mode = ThemeMode::HighContrast, .variant = v}, OsThemeSnapshot{});
        ComboBoxControl      d{{"One", "Two"}, 0};
        HeadlessTextMeasurer measurer;
        const Size           size = d.measure({Unbounded, Unbounded}, measurer, theme);
        d.arrange({0.0f, 0.0f, size.width, size.height});
        TestRenderer r;
        d.render(r, theme);
        bool outline = false;
        for (const RenderCommand& c : r.commands)
        {
            if (const auto* s = std::get_if<StrokeRoundedRect>(&c); s && s->color == theme.button_normal.border)
            {
                outline = true;
            }
        }
        CHECK(outline);
    }
}

TEST_CASE("A classic drop-down list is a field with a raised button and a triangle", "[combo_box][render][retro]")
{
    for (const Theme& theme :
         {resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{}),
          resolve_theme(ThemeRequest{.mode = ThemeMode::HighContrast, .variant = ThemeVariant::Aero}, OsThemeSnapshot{}),
          resolve_theme(ThemeRequest{.mode = ThemeMode::HighContrast, .variant = ThemeVariant::Luna}, OsThemeSnapshot{})})
    {
        REQUIRE(ui::detail::controls(theme).dropdown_chrome.has_value());
        REQUIRE(ui::detail::controls(theme).dropdown_chrome->field_with_button);
        // And a focused classic field shows only its caret, not Fluent's 2px underline.
        CHECK(ui::detail::controls(theme).field_focus_border_thickness == Thickness{});

        ComboBoxControl      d{{"One", "Two"}, 0};
        HeadlessTextMeasurer measurer;
        const Size           size = d.measure({Unbounded, Unbounded}, measurer, theme);
        d.arrange({0.0f, 0.0f, size.width, size.height});
        TestRenderer r;
        d.render(r, theme);

        bool field = false;
        bool edge  = false;
        for (const RenderCommand& c : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->color == theme.colors().field)
            {
                field = true;
            }
            if (const auto* s = std::get_if<StrokeRoundedRect>(&c); s && s->color == theme.surfaces.field_border)
            {
                edge = true;
            }
            if (const auto* t = std::get_if<DrawTextRun>(&c))
            {
                CHECK(t->text == "One"); // no chevron glyph
            }
        }
        CHECK(field);
        CHECK(edge);
        // The 7 x 4 scroll arrow, one rect per pixel row: 7, 5, 3 and 1 wide.
        CHECK(r.count_of<FillPolygon>() == 0);
        std::vector<float> rows;
        for (const RenderCommand& c : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&c);
                f && f->color == ui::detail::controls(theme).dropdown_chrome->arrow)
            {
                rows.push_back(f->bounds.width);
            }
        }
        CHECK(rows == std::vector<float>{7.0f, 5.0f, 3.0f, 1.0f});
    }
}

TEST_CASE("An Aero drop-down list casts its shadow only once it has slid open", "[combo_box][popup][aero][animation]")
{
    // AnimateWindow shows the drop shadow only after the slide.
    const Theme          theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    ComboBoxControl      d{{"One", "Two"}, 0};
    HeadlessTextMeasurer measurer;
    const Size           size = d.measure({Unbounded, Unbounded}, measurer, theme);
    d.arrange({0.0f, 0.0f, size.width, size.height});

    CHECK_FALSE(ui::testing::popup_open(d));
    ui::automation_providers(d).expand_collapse->set_expanded_state(true);
    CHECK_FALSE(ui::testing::ElementAccess::popup_request(d)->chrome.shadow);
    d.tick(100ms);
    CHECK_FALSE(ui::testing::ElementAccess::popup_request(d)->chrome.shadow);
    d.tick(150ms);
    CHECK(ui::testing::ElementAccess::popup_request(d)->chrome.shadow);
}

TEST_CASE("Typing on a closed read-only box selects the next item starting with the letters", "[combo_box][search]")
{
    ComboBoxControl                         d{{"Apple", "Banana", "Blueberry", "Cherry", "black currant"}, 0};
    std::vector<std::optional<std::size_t>> seen;
    d.on_selection_changed([&](std::optional<std::size_t> i) { seen.push_back(i); });
    ui::testing::set_focused(d, true);

    d.handle_text("b");
    CHECK(d.selected_index() == 1); // Banana
    d.handle_text("l");
    CHECK(d.selected_index() == 2); // "bl": Blueberry, from the current item on
    d.handle_text("a");
    CHECK(d.selected_index() == 4); // "bla": black currant, case aside
    CHECK(seen == std::vector<std::optional<std::size_t>>{1, 2, 4});

    d.handle_text("z");
    CHECK(d.selected_index() == 4); // nothing starts with "blaz": the selection stays
}

TEST_CASE("A read-only box's search starts over a second after the last letter", "[combo_box][search]")
{
    ComboBoxControl d{{"Apple", "Banana", "Cherry"}, 0};
    ui::testing::set_focused(d, true);

    d.handle_text("c");
    CHECK(d.selected_index() == 2);
    d.tick(600ms);
    d.handle_text("h"); // "ch" still
    CHECK(d.selected_index() == 2);
    d.tick(1100ms);
    d.handle_text("a"); // a new search for "a"
    CHECK(d.selected_index() == 0);
}

TEST_CASE("The same letter again steps through the items starting with it", "[combo_box][search]")
{
    ComboBoxControl d{{"Red", "Green", "Gray", "Gold"}, 0};
    ui::testing::set_focused(d, true);

    d.handle_text("g");
    CHECK(d.selected_index() == 1);
    d.handle_text("g");
    CHECK(d.selected_index() == 2);
    d.handle_text("g");
    CHECK(d.selected_index() == 3);
    d.handle_text("g");
    CHECK(d.selected_index() == 1); // round to the first again
}

TEST_CASE("Typing on an open read-only list moves the highlight, and Enter picks it", "[combo_box][search]")
{
    ComboBoxControl          d{{"Apple", "Banana", "Cherry"}, 0};
    std::vector<std::size_t> chosen;
    d.on_item_chosen([&](std::size_t i) { chosen.push_back(i); });
    ui::testing::set_focused(d, true);
    d.handle_key({Key::F4, KeyAction::Down});
    REQUIRE(d.expanded());

    d.handle_text("c");
    CHECK(d.selected_index() == 0); // only highlighted while open
    d.handle_key({Key::Enter, KeyAction::Down});
    CHECK_FALSE(d.expanded());
    CHECK(d.selected_index() == 2);
    CHECK(chosen == std::vector<std::size_t>{2});
}

TEST_CASE("Space in the middle of a search is part of it; otherwise it opens the list", "[combo_box][search]")
{
    ComboBoxControl d{{"New York", "Newark", "New Delhi"}, std::nullopt};
    ui::testing::set_focused(d, true);

    d.handle_text("n");
    d.handle_text("e");
    d.handle_text("w");
    CHECK(d.selected_index() == 0);
    d.handle_key({Key::Space, KeyAction::Down});
    d.handle_text(" ");
    d.handle_key({Key::Space, KeyAction::Up});
    d.handle_text("d");
    CHECK_FALSE(d.expanded());
    CHECK(d.selected_index() == 2);

    d.tick(1500ms);
    d.handle_key({Key::Space, KeyAction::Down});
    d.handle_text(" ");
    d.handle_key({Key::Space, KeyAction::Up});
    CHECK(d.expanded());
    CHECK(d.selected_index() == 2);
}

TEST_CASE("F4 opens and closes a read-only box's list", "[combo_box][command]")
{
    ComboBoxControl d{{"One", "Two"}, 0};
    ui::testing::set_focused(d, true);
    d.handle_key({Key::F4, KeyAction::Down});
    CHECK(d.expanded());
    d.handle_key({Key::F4, KeyAction::Down});
    CHECK_FALSE(d.expanded());
}

TEST_CASE("A read-only box's text is its selection, and set_text() selects the matching item", "[combo_box]")
{
    ComboBoxControl d{{"Red", "Green"}, 1};
    CHECK(d.text() == "Green");
    d.set_text("Red");
    CHECK(d.selected_index() == 0);
    d.set_text("Purple"); // no such item
    CHECK(d.selected_index() == std::nullopt);
    CHECK(d.text().empty());
}

TEST_CASE("Switching a ComboBox to editable and back carries its value over", "[combo_box]")
{
    ComboBoxControl d{{"Red", "Green"}, 1};
    ui::testing::set_focused(d, true);
    d.set_editable(true);
    CHECK(d.text() == "Green");
    CHECK(d.selected_index() == 1);

    d.handle_text("s"); // "Greens"
    CHECK(d.selected_index() == std::nullopt);
    d.set_editable(false);
    CHECK(d.text().empty());

    d.set_selected_index(0);
    d.set_editable(true);
    CHECK(d.text() == "Red");
}

TEST_CASE("ui::ComboBox's bound text follows a read-only box's selection", "[combo_box][spec]")
{
    Property<std::string>     text{"B"};
    ui::Control<ui::ComboBox> combo;
    std::unique_ptr<Element>  tree = ui::build(
        ui::ComboBox{.items = {"A", "B", "C"}, .text = ui::bind(text), .control = combo});
    CHECK(combo->selected_index() == 1);

    ui::testing::set_focused(*combo, true);
    combo->handle_key({Key::ArrowDown, KeyAction::Down});
    CHECK(combo->selected_index() == 2);
    CHECK(text.get() == "C");
    text = "A";
    CHECK(combo->selected_index() == 0);
}
