// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestClipboard.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/VisualStates.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;

namespace
{
PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}

Point chevron_point(const ComboBoxControl& c)
{
    const Rect b = c.bounds();
    return Point{b.right() - b.height * 0.5f, b.center().y};
}

Point text_point(const ComboBoxControl& c)
{
    const Rect b = c.bounds();
    return Point{b.left() + 4.0f, b.center().y};
}

Point item_point(const ComboBoxControl& c, std::size_t index)
{
    const Rect  b     = c.bounds();
    const float row_h = b.height;
    return Point{b.center().x, b.bottom() + row_h * (static_cast<float>(index) + 0.5f)};
}

const Point Outside{-500.0f, -500.0f};

// Returns a unique_ptr because the constructor points text_box_ back at this, so a moved
// value would leave that back-pointer dangling.
std::unique_ptr<ComboBoxControl> make_combo_box()
{
    auto c = std::make_unique<ComboBoxControl>(std::vector<std::string>{"Light", "Dark", "High contrast"});
    c->set_editable(true);
    HeadlessTextMeasurer measurer;
    const Size           closed = c->measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    c->arrange({100.0f, 100.0f, closed.width, closed.height});
    return c;
}

// Tests call relayout() after toggling expanded_, because sync_popups() only runs inside layout().
struct ComboBoxWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    ComboBoxControl*     combo;

    explicit ComboBoxWindow(std::vector<std::string> items)
    {
        auto owned = std::make_unique<ComboBoxControl>(std::move(items));
        owned->set_editable(true);
        combo = owned.get();
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

TEST_CASE("A new ComboBox starts read-only, closed, enabled, and with empty text", "[combo_box]")
{
    ComboBoxControl c;
    REQUIRE_FALSE(c.editable());
    REQUIRE(c.enabled());
    REQUIRE_FALSE(c.expanded());
    REQUIRE(c.text().empty());
    REQUIRE(c.items().empty());
}

TEST_CASE("An editable ComboBox keeps text that is none of its items", "[combo_box]")
{
    ComboBoxControl c{{"Light", "Dark"}};
    c.set_editable(true);
    c.set_text("Custom");
    REQUIRE(c.items() == std::vector<std::string>{"Light", "Dark"});
    REQUIRE(c.text() == "Custom");
}

TEST_CASE("An empty editable ComboBox is still focusable, unlike a read-only one", "[combo_box][state]")
{
    ComboBoxControl c;
    REQUIRE_FALSE(c.focusable());
    c.set_editable(true);
    REQUIRE(c.focusable());
}

TEST_CASE("Typing while focused edits the text and fires on_text_changed", "[combo_box][command]")
{
    auto                     c = make_combo_box();
    std::vector<std::string> observed;
    c->on_text_changed([&](const std::string& t) { observed.push_back(t); });

    ui::testing::set_focused(*c, true);
    c->handle_text("O");
    c->handle_text("k");

    REQUIRE(c->text() == "Ok");
    REQUIRE(observed == std::vector<std::string>{"O", "Ok"});
}

TEST_CASE("Ctrl+V reaches the internal TextBox through ComboBox", "[combo_box][clipboard]")
{
    auto c = make_combo_box();
    ui::testing::set_focused(*c, true);

    TestClipboard clipboard;
    clipboard.set_text("Dark");
    ui::testing::set_clipboard(&clipboard);
    c->handle_key({Key::V, KeyAction::Down, {.ctrl = true}});

    REQUIRE(c->text() == "Dark");
}

TEST_CASE("set_text() does not fire on_text_changed", "[combo_box][command]")
{
    auto c     = make_combo_box();
    int  fired = 0;
    c->on_text_changed([&](const std::string&) { ++fired; });

    c->set_text("Preset");

    REQUIRE(c->text() == "Preset");
    REQUIRE(fired == 0);
}

TEST_CASE("set_items() replaces the item list without touching the current text", "[combo_box][state]")
{
    auto c = make_combo_box();
    c->set_text("Dark");

    c->set_items({"One", "Two"});

    REQUIRE(c->items() == std::vector<std::string>{"One", "Two"});
    REQUIRE(c->text() == "Dark");
}

TEST_CASE("Clicking the chevron toggles expanded", "[combo_box][command]")
{
    auto        c       = make_combo_box();
    const Point chevron = chevron_point(*c);

    c->handle_pointer(down(chevron));
    c->handle_pointer(up(chevron));
    REQUIRE(c->expanded());

    c->handle_pointer(down(chevron));
    c->handle_pointer(up(chevron));
    REQUIRE_FALSE(c->expanded());
}

TEST_CASE("Clicking the text region does not toggle expanded", "[combo_box][command]")
{
    auto c = make_combo_box();

    c->handle_pointer(down(text_point(*c)));
    c->handle_pointer(up(text_point(*c)));

    REQUIRE_FALSE(c->expanded());
}

TEST_CASE("Clicking an item in the floating popup selects it, fires the callback, and closes",
          "[combo_box][command][popup]")
{
    ComboBoxWindow           w{{"Light", "Dark", "High contrast"}};
    std::vector<std::size_t> observed;
    w.combo->on_item_chosen([&](std::size_t i) { observed.push_back(i); });

    const Point chevron = chevron_point(*w.combo);
    ui::testing::seams(w.window).handle_pointer(down(chevron));
    ui::testing::seams(w.window).handle_pointer(up(chevron));
    w.relayout(); // positions the popup now that expanded_ flipped
    REQUIRE(w.combo->expanded());

    const Point row1 = item_point(*w.combo, 1); // "Dark"
    ui::testing::seams(w.window).handle_pointer(down(row1));
    ui::testing::seams(w.window).handle_pointer(up(row1));

    REQUIRE_FALSE(w.combo->expanded());
    REQUIRE(w.combo->text() == "Dark");
    REQUIRE(observed == std::vector<std::size_t>{1});
}

TEST_CASE("Picking an item writes a bound text back, as typing does", "[combo_box][spec][binding]")
{
    Property<std::string>     theme_name{"Light"};
    std::vector<std::size_t>  picked;
    ui::Control<ui::ComboBox> combo;
    HeadlessTextMeasurer      measurer;
    Window                    window;
    window.set_stretch_content(false);
    window.set_content(ui::ComboBox{.items          = {"Light", "Dark", "High contrast"},
                                    .text           = ui::bind(theme_name),
                                    .editable       = true,
                                    .on_item_chosen = [&](std::size_t i) { picked.push_back(i); },
                                    .control        = combo});
    window.layout({400.0f, 400.0f}, measurer);

    const Point chevron = chevron_point(*combo);
    ui::testing::seams(window).handle_pointer(down(chevron));
    ui::testing::seams(window).handle_pointer(up(chevron));
    window.layout({400.0f, 400.0f}, measurer);
    REQUIRE(combo->expanded());

    const Point row1 = item_point(*combo, 1);
    ui::testing::seams(window).handle_pointer(down(row1));
    ui::testing::seams(window).handle_pointer(up(row1));

    CHECK(combo->text() == "Dark");
    CHECK(theme_name.get() == "Dark"); // the property follows the pick, not only typed text
    CHECK(picked == std::vector<std::size_t>{1});
}

TEST_CASE("Re-picking the item already shown still fires on_item_chosen", "[combo_box][command][popup]")
{
    // There is no selected index to de-dupe against: the text may have been typed by hand, so
    // picking the same item again is deliberate.
    ComboBoxWindow w{{"Light", "Dark"}};
    w.combo->set_text("Light");
    int fired = 0;
    w.combo->on_item_chosen([&](std::size_t) { ++fired; });

    const Point chevron = chevron_point(*w.combo);
    ui::testing::seams(w.window).handle_pointer(down(chevron));
    ui::testing::seams(w.window).handle_pointer(up(chevron));
    w.relayout();

    const Point row0 = item_point(*w.combo, 0); // "Light", already the current text
    ui::testing::seams(w.window).handle_pointer(down(row0));
    ui::testing::seams(w.window).handle_pointer(up(row0));

    REQUIRE(fired == 1);
}

TEST_CASE("A pointer-down outside the popup and its owner light-dismisses it", "[combo_box][popup]")
{
    ComboBoxWindow w{{"Light", "Dark", "High contrast"}};

    const Point chevron = chevron_point(*w.combo);
    ui::testing::seams(w.window).handle_pointer(down(chevron));
    ui::testing::seams(w.window).handle_pointer(up(chevron));
    w.relayout();
    REQUIRE(w.combo->expanded());

    ui::testing::seams(w.window).handle_pointer(down(Outside));
    REQUIRE_FALSE(w.combo->expanded());
}

TEST_CASE("Expanding a ComboBox inside a StackControl does not reflow a sibling below it", "[combo_box][popup]")
{
    auto combo_owned   = std::make_unique<ComboBoxControl>(std::vector<std::string>{"Light", "Dark", "High contrast"});
    ComboBoxControl* c = combo_owned.get();
    c->set_editable(true);
    auto           btn_owned = std::make_unique<ButtonControl>("Below");
    ButtonControl* btn       = btn_owned.get();

    auto stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->add(std::move(combo_owned));
    stack->add(std::move(btn_owned));

    HeadlessTextMeasurer measurer;
    Window               window;
    window.set_content(std::move(stack));
    window.layout({400.0f, 400.0f}, measurer);
    const float button_top_before = btn->bounds().top();

    ui::testing::seams(window).handle_pointer(down(chevron_point(*c)));
    ui::testing::seams(window).handle_pointer(up(chevron_point(*c)));
    window.layout({400.0f, 400.0f}, measurer);

    REQUIRE(c->expanded());
    REQUIRE(btn->bounds().top() == button_top_before);
}

TEST_CASE("F4 opens and closes the popup while focused", "[combo_box][command]")
{
    auto c = make_combo_box();
    ui::testing::set_focused(*c, true);

    c->handle_key({Key::F4, KeyAction::Down});
    REQUIRE(c->expanded());

    c->handle_key({Key::F4, KeyAction::Down});
    REQUIRE_FALSE(c->expanded());
}

TEST_CASE("Arrow keys navigate the open popup; Enter commits the highlighted item and closes", "[combo_box][command]")
{
    auto c = make_combo_box();
    ui::testing::set_focused(*c, true);
    c->handle_key({Key::F4, KeyAction::Down});

    c->handle_key({Key::ArrowDown, KeyAction::Down}); // highlights index 0 ("Light")
    c->handle_key({Key::ArrowDown, KeyAction::Down}); // highlights index 1 ("Dark")
    c->handle_key({Key::Enter, KeyAction::Down});

    REQUIRE_FALSE(c->expanded());
    REQUIRE(c->text() == "Dark");
}

TEST_CASE("Escape closes an open popup without changing the text", "[combo_box][command]")
{
    auto c = make_combo_box();
    ui::testing::set_focused(*c, true);
    c->handle_key({Key::F4, KeyAction::Down});
    c->handle_key({Key::ArrowDown, KeyAction::Down});

    c->handle_key({Key::Escape, KeyAction::Down});

    REQUIRE_FALSE(c->expanded());
    REQUIRE(c->text().empty());
}

TEST_CASE("Typing while the popup is open keeps editing the text box", "[combo_box][command]")
{
    auto c = make_combo_box();
    ui::testing::set_focused(*c, true);
    c->handle_key({Key::F4, KeyAction::Down});

    c->handle_text("X");

    REQUIRE(c->expanded());
    REQUIRE(c->text() == "X");
}

TEST_CASE("Losing focus closes an open popup", "[combo_box][state]")
{
    auto c = make_combo_box();
    ui::testing::set_focused(*c, true);
    c->handle_key({Key::F4, KeyAction::Down});
    REQUIRE(c->expanded());

    ui::testing::set_focused(*c, false);
    REQUIRE_FALSE(c->expanded());
}

TEST_CASE("Disabled ComboBox ignores input and cannot be focused", "[combo_box][state]")
{
    auto c = make_combo_box();
    c->set_enabled(false);

    c->handle_pointer(down(chevron_point(*c)));
    c->handle_pointer(up(chevron_point(*c)));
    REQUIRE_FALSE(c->expanded());
    REQUIRE_FALSE(c->focusable());
    REQUIRE(ui::testing::visual_state(*c) == ButtonState::Disabled);
}

TEST_CASE("An empty item list cannot be expanded, but stays a usable text box", "[combo_box][state]")
{
    ComboBoxControl c;
    c.set_editable(true);
    ui::testing::set_focused(c, true);

    c.handle_key({Key::F4, KeyAction::Down});
    REQUIRE_FALSE(c.expanded());

    c.handle_text("Y");
    REQUIRE(c.text() == "Y");
}

// ---- committing typed text ----

namespace
{
void type(ComboBoxControl& c, const std::string& text)
{
    // One code point a keystroke, as WM_CHAR delivers them.
    for (std::size_t i = 0; i < text.size(); i = next_code_point(text, i))
    {
        c.handle_text(std::string_view{text}.substr(i, next_code_point(text, i) - i));
    }
}
void press(ComboBoxControl& c, Key key)
{
    c.handle_key({key, KeyAction::Down});
    c.handle_key({key, KeyAction::Up});
}

std::vector<std::string> drawn_text(const ComboBoxControl& c)
{
    const Theme  theme = Theme::light();
    TestRenderer renderer;
    c.render(renderer, theme);
    std::vector<std::string> out;
    for (const RenderCommand& command : renderer.commands)
    {
        if (const auto* run = std::get_if<DrawTextRun>(&command); run && run->style.font_family != theme.icon_font_family)
        {
            out.push_back(run->text);
        }
    }
    return out;
}
} // namespace

TEST_CASE("Enter submits typed text that matches no item, once per new value", "[combo_box][submit]")
{
    auto                     c = make_combo_box();
    std::vector<std::string> submitted;
    c->on_text_submitted([&](const ComboBoxTextSubmittedArgs& args) { submitted.push_back(args.text); });
    ui::testing::set_focused(*c, true);

    type(*c, "Sepia");
    CHECK(submitted.empty()); // typing alone submits nothing
    press(*c, Key::Enter);
    CHECK(submitted == std::vector<std::string>{"Sepia"});

    // Accepted, so it is the committed text now: Enter again has nothing new to submit.
    press(*c, Key::Enter);
    CHECK(submitted.size() == 1);
}

TEST_CASE("Committing an item's exact text picks that item instead of submitting", "[combo_box][submit]")
{
    auto                     c         = make_combo_box();
    int                      submitted = 0;
    std::vector<std::size_t> picked;
    c->on_text_submitted([&] { ++submitted; });
    c->on_item_chosen([&](std::size_t index) { picked.push_back(index); });
    ui::testing::set_focused(*c, true);

    type(*c, "Dark");
    press(*c, Key::Enter);
    CHECK(submitted == 0);
    CHECK(picked == std::vector<std::size_t>{1});
}

TEST_CASE("Losing focus commits like Enter; blank text is not a submission", "[combo_box][submit]")
{
    auto c         = make_combo_box();
    int  submitted = 0;
    c->on_text_submitted([&] { ++submitted; });

    ui::testing::set_focused(*c, true);
    type(*c, "    ");
    ui::testing::set_focused(*c, false);
    CHECK(submitted == 0);

    ui::testing::set_focused(*c, true);
    type(*c, "x");
    ui::testing::set_focused(*c, false); // Tab away: Window never shows the control the Tab itself
    CHECK(submitted == 1);
}

TEST_CASE("A handler that rejects the text keeps the committed value, and Escape reverts to it", "[combo_box][submit]")
{
    auto c = make_combo_box();
    c->set_text("12");
    c->on_text_submitted(
        [&](const ComboBoxTextSubmittedArgs& args)
        {
            if (args.text.find_first_not_of("0123456789") != std::string::npos)
            {
                c->set_text("12"); // rejected: back to the last good value
                return ui::Cancel;
            }
            return ui::Proceed;
        });
    ui::testing::set_focused(*c, true);

    type(*c, "abc"); // appended to "12"
    press(*c, Key::Enter);
    CHECK(c->text() == "12");

    type(*c, "5");
    CHECK(c->text() == "125");
    press(*c, Key::Escape);
    CHECK(c->text() == "12");
}

TEST_CASE("A ComboBox shows its placeholder only while its text is empty", "[combo_box][render]")
{
    auto c = make_combo_box();
    c->set_placeholder_text("Pick a color");
    CHECK(c->placeholder_text() == "Pick a color");
    CHECK(drawn_text(*c) == std::vector<std::string>{"Pick a color"});

    c->set_text("Dark");
    CHECK(drawn_text(*c) == std::vector<std::string>{"Dark"});
    CHECK(c->text() == "Dark"); // the placeholder never becomes the text
}

TEST_CASE("An Aero combo box is a text field with a flush drop button and a triangle", "[combobox][aero]")
{
    const Theme     theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    ComboBoxControl c{{"Light", "Dark"}};
    c.set_editable(true);
    c.set_text("Custom");
    HeadlessTextMeasurer measurer;
    const Size           size = c.measure({160.0f, Unbounded}, measurer, theme);
    c.arrange({0.0f, 0.0f, 160.0f, size.height});

    TestRenderer r;
    c.render(r, theme);
    // The field's border is the text box's rest gradient at radius 1, and the arrow a solid
    // triangle in the 17px drop button, whose resting face is the ring alone.
    bool field = false;
    for (const RenderCommand& cmd : r.commands)
    {
        if (const auto* s = std::get_if<StrokeRoundedRectGradient>(&cmd);
            s && s->brush == ui::detail::controls(theme).field_border_states->rest)
        {
            field = true;
        }
        if (const auto* g = std::get_if<FillRoundedRectGradient>(&cmd))
        {
            CHECK(g->brush != *theme.button_normal.background_gradient);
        }
    }
    CHECK(field);
    REQUIRE(r.count_of<FillPolygon>() == 1);
    for (const RenderCommand& cmd : r.commands)
    {
        if (const auto* p = std::get_if<FillPolygon>(&cmd))
        {
            CHECK(p->points[0].x >= 160.0f - 17.0f);
        }
    }
}

TEST_CASE("Only the eras with an accent mark the selected item with a pill", "[combo_box][popup][era]")
{
    // The pill is a 3 px wide accent bar at the selected row's left edge.
    const auto pill_count = [](ThemeVariant variant)
    {
        HeadlessUI       ui;
        ComboBoxControl* combo = nullptr;
        auto owned = std::make_unique<ComboBoxControl>(std::vector<std::string>{"Light", "Dark", "Auto"}, 1);
        combo      = owned.get();
        ui.window().set_theme(ThemeRequest{.variant = variant, .animations = AnimationPreference::Off});
        ui.window().set_stretch_content(false);
        ui.window().set_content(std::move(owned));
        ui.layout({400.0f, 400.0f});
        REQUIRE(combo->focus());
        seams(ui.window()).handle_key(KeyEvent{Key::F4, KeyAction::Down});
        ui.layout({400.0f, 400.0f});
        Element* list = popup_content(*combo);
        REQUIRE(list != nullptr);
        TestRenderer r;
        list->render(r, ui.window().theme());
        int pills = 0;
        for (const RenderCommand& command : r.commands)
        {
            if (const auto* fill = std::get_if<FillRoundedRect>(&command);
                fill && fill->bounds.width == 3.0f && fill->color == ui.window().theme().colors().accent)
            {
                ++pills;
            }
        }
        return pills;
    };
    CHECK(pill_count(ThemeVariant::Fluent2) == 1);
    CHECK(pill_count(ThemeVariant::Luna) == 0);
    CHECK(pill_count(ThemeVariant::Metro) == 0);
}

TEST_CASE("selected_index follows the item the text matches", "[combo_box][selection]")
{
    ComboBoxControl c{{"Red", "Green", "Blue"}};
    c.set_editable(true);
    std::vector<std::optional<std::size_t>> seen;
    c.on_selection_changed([&](std::optional<std::size_t> index) { seen.push_back(index); });
    CHECK(c.selected_index() == std::nullopt);

    ui::testing::set_focused(c, true);
    for (const char* letter : {"G", "r", "e", "e", "n"})
    {
        c.handle_text(letter);
    }
    CHECK(c.selected_index() == 1);
    c.handle_text("s");
    CHECK(c.selected_index() == std::nullopt);
    CHECK(seen == std::vector<std::optional<std::size_t>>{1, std::nullopt});

    c.set_selected_index(2);
    CHECK(c.text() == "Blue");
    c.set_selected_index(7); // past the list
    CHECK(c.text() == "Blue");
    c.set_items({"Blue", "Red"});
    CHECK(c.selected_index() == 0);
    c.set_selected_index(std::nullopt);
    CHECK(c.text().empty());
    CHECK(seen == std::vector<std::optional<std::size_t>>{1, std::nullopt, 2, 0, std::nullopt});
}

TEST_CASE("ui::ComboBox's selected_index is two-way and keeps a bound text in step", "[combo_box][selection][spec]")
{
    Property<std::optional<std::size_t>> index{1};
    Property<std::string>                text{"Green"};
    ui::Control<ui::ComboBox>            combo;
    std::unique_ptr<Element>             tree = ui::build(
        ui::ComboBox{.items          = {"Red", "Green", "Blue"},
                     .selected_index = ui::bind(index),
                     .text           = ui::bind(text),
                     .editable       = true,
                     .control        = combo});
    CHECK(combo->text() == "Green");

    index = 2;
    CHECK(combo->text() == "Blue");
    CHECK(text.get() == "Blue");

    ui::testing::set_focused(*combo, true);
    combo->handle_text("!");
    CHECK(index.get() == std::nullopt);
    CHECK(text.get() == "Blue!");
}

TEST_CASE("ui::ComboBox without a selected_index leaves its text alone", "[combo_box][selection][spec]")
{
    ui::Control<ui::ComboBox> combo;
    std::unique_ptr<Element> tree = ui::build(ui::ComboBox{.items = {"Red", "Green"}, .text = "Red", .control = combo});
    CHECK(combo->text() == "Red");
    CHECK(combo->selected_index() == 0);
}

namespace
{
// The inner TextBox an editable box shows its text, caret and selection in.
const TextBoxControl& inner_text_box(const ComboBoxControl& c)
{
    const TextBoxControl* box = nullptr;
    c.for_each_child([&box](Element& child) { box = dynamic_cast<const TextBoxControl*>(&child); });
    REQUIRE(box != nullptr);
    return *box;
}
} // namespace

TEST_CASE("Typing at the end completes the text to the first item it starts, the rest selected", "[combo_box][complete]")
{
    ComboBoxControl c{{"Red", "Green", "Gray"}};
    c.set_editable(true);
    std::vector<std::string>                observed;
    std::vector<std::optional<std::size_t>> seen;
    c.on_text_changed([&](const std::string& t) { observed.push_back(t); });
    c.on_selection_changed([&](std::optional<std::size_t> i) { seen.push_back(i); });
    ui::testing::set_focused(c, true);

    c.handle_text("g");
    CHECK(c.text() == "Green"); // the item's own spelling
    CHECK(inner_text_box(c).selection_range() == std::pair<std::size_t, std::size_t>{1, 5});
    c.handle_text("r"); // replaces the completed part, and completes again
    CHECK(c.text() == "Green");
    c.handle_text("a");
    CHECK(c.text() == "Gray");
    CHECK(inner_text_box(c).selection_range() == std::pair<std::size_t, std::size_t>{3, 4});
    // The app hears of each edit once, completed.
    CHECK(observed == std::vector<std::string>{"Green", "Green", "Gray"});
    CHECK(seen == std::vector<std::optional<std::size_t>>{1, 2});
}

TEST_CASE("Backspace and typing inside the text never complete", "[combo_box][complete]")
{
    ComboBoxControl c{{"Green"}};
    c.set_editable(true);
    ui::testing::set_focused(c, true);

    c.handle_text("g");
    REQUIRE(c.text() == "Green");
    c.handle_key({Key::Backspace, KeyAction::Down}); // deletes the completed part
    CHECK(c.text() == "G");                          // in the item's spelling
    CHECK(c.selected_index() == std::nullopt);

    c.handle_key({Key::Home, KeyAction::Down});
    c.handle_text("x"); // not at the end
    CHECK(c.text() == "xG");
}
