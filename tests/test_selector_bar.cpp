// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/navigation/SelectorBar.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
KeyEvent key_down(Key k)
{
    return {k, KeyAction::Down};
}

// A Fluent SelectorBar fills only the selected item's pill, so the stream needs no index.
FillRoundedRect only_fill(const TestRenderer& r)
{
    for (const auto& c : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c))
        {
            return *f;
        }
    }
    return {};
}
} // namespace

TEST_CASE("A default-constructed SelectorBar has no items and is not focusable", "[selector-bar]")
{
    SelectorBarControl bar;
    REQUIRE(bar.items().empty());
    REQUIRE(bar.selected_index() == 0);
    REQUIRE(bar.selected_text().empty());
    REQUIRE_FALSE(bar.focusable());
}

TEST_CASE("Constructing with items selects index 0 by default, clamped if out of range", "[selector-bar]")
{
    SelectorBarControl bar{{"Day", "Week", "Month"}};
    REQUIRE(bar.items().size() == 3);
    REQUIRE(bar.selected_index() == 0);
    REQUIRE(bar.selected_text() == "Day");

    SelectorBarControl clamped{{"Day", "Week"}, 10};
    REQUIRE(clamped.selected_index() == 1);
}

TEST_CASE("set_items replaces the list and resets selection to 0", "[selector-bar]")
{
    SelectorBarControl bar{{"Day", "Week"}, 1};
    REQUIRE(bar.selected_index() == 1);

    bar.set_items({"Q1", "Q2", "Q3"});
    REQUIRE(bar.items().size() == 3);
    REQUIRE(bar.selected_index() == 0);
    REQUIRE(bar.selected_text() == "Q1");
}

TEST_CASE("set_selected_index clamps and fires on_selection_changed only on an actual change", "[selector-bar]")
{
    SelectorBarControl bar{{"One", "Two", "Three"}};

    int         calls = 0;
    std::size_t last  = static_cast<std::size_t>(-1);
    bar.on_selection_changed(
        [&](std::size_t i)
        {
            ++calls;
            last = i;
        });

    bar.set_selected_index(0); // already selected: no-op
    REQUIRE(calls == 0);

    bar.set_selected_index(2);
    REQUIRE(calls == 1);
    REQUIRE(last == 2);

    bar.set_selected_index(1000); // clamped to the last item
    REQUIRE(bar.selected_index() == 2);
    REQUIRE(calls == 1); // already at the last item: no-op
}

TEST_CASE("Only the widest row height changes measured size; selection alone does not", "[selector-bar][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    SelectorBarControl bar{{"Day", "Week", "Month"}};
    const Size         first = bar.measure({300.0f, Unbounded}, m, theme);

    bar.set_selected_index(2);
    const Size second = bar.measure({300.0f, Unbounded}, m, theme);

    REQUIRE_THAT(first.height, WithinAbs(static_cast<double>(second.height), 1e-3));
    REQUIRE_THAT(first.width, WithinAbs(static_cast<double>(second.width), 1e-3));
}

TEST_CASE("Clicking an item selects it", "[selector-bar][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    // Single-letter items all measure the same, so "A" spans [0, w) and "B" spans [w, 2w).
    SelectorBarControl bar{{"A", "B"}};
    const Size         desired = bar.measure({300.0f, 200.0f}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    const float w = desired.width / 2.0f;
    const Point p{w + 5.0f, 5.0f}; // inside item "B"
    bar.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    REQUIRE(bar.selected_index() == 1);
}

TEST_CASE("A click outside every item's bounds is ignored", "[selector-bar][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    SelectorBarControl bar{{"A", "B"}};
    const Size         desired = bar.measure({300.0f, 200.0f}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    bar.handle_pointer({{5.0f, 500.0f}, PointerButton::Left, PointerAction::Down}); // well below the row
    REQUIRE(bar.selected_index() == 0);
}

TEST_CASE("Arrow keys move the selection, clamped (not wrapping), only while focused", "[selector-bar][input]")
{
    SelectorBarControl bar{{"One", "Two", "Three"}};

    bar.handle_key(key_down(Key::ArrowRight)); // not focused: ignored
    REQUIRE(bar.selected_index() == 0);

    ui::testing::set_focused(bar, true);
    bar.handle_key(key_down(Key::ArrowLeft)); // already at 0: clamps, stays 0
    REQUIRE(bar.selected_index() == 0);

    bar.handle_key(key_down(Key::ArrowRight));
    REQUIRE(bar.selected_index() == 1);
    bar.handle_key(key_down(Key::ArrowRight));
    REQUIRE(bar.selected_index() == 2);
    bar.handle_key(key_down(Key::ArrowRight)); // clamped at the end
    REQUIRE(bar.selected_index() == 2);

    bar.handle_key(key_down(Key::ArrowLeft));
    REQUIRE(bar.selected_index() == 1);
}

TEST_CASE("A single-item SelectorBar is not focusable and ignores arrow keys", "[selector-bar][focus]")
{
    SelectorBarControl bar{{"Solo"}};
    REQUIRE_FALSE(bar.focusable());

    ui::testing::set_focused(bar, true); // set_focused itself has no focusable() guard, matching Tabs
    bar.handle_key(key_down(Key::ArrowRight));
    REQUIRE(bar.selected_index() == 0);

    bar.set_items({"One", "Two"});
    REQUIRE(bar.focusable());

    bar.set_enabled(false);
    REQUIRE_FALSE(bar.focusable());
}

TEST_CASE("Render draws one text run per item plus exactly one pill for the selected item", "[selector-bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    SelectorBarControl bar{{"Day", "Week", "Month"}, 1};
    const Size         desired = bar.measure({300.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    bar.render(r, theme);
    REQUIRE(r.count_of<DrawTextRun>() == 3);
    REQUIRE(r.count_of<FillRoundedRect>() == 1); // only the selected item's pill, no hover
}

TEST_CASE("Disabled SelectorBar ignores pointer and keyboard input", "[selector-bar][state]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    SelectorBarControl bar{{"A", "B"}};
    const Size         desired = bar.measure({300.0f, 200.0f}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});
    ui::testing::set_focused(bar, true);
    bar.set_enabled(false);

    const float w = desired.width / 2.0f;
    bar.handle_pointer({{w + 5.0f, 5.0f}, PointerButton::Left, PointerAction::Down});
    bar.handle_key(key_down(Key::ArrowRight));

    REQUIRE(bar.selected_index() == 0);
}

// ---- Pill geometry, grow-in, and the text-only state ramp ----

TEST_CASE("The selection pill is a short centered dash, not a full-width bar", "[selector-bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    SelectorBarControl bar{{"Day", "Week"}, 0};
    const Size         desired = bar.measure({400.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    bar.render(r, theme);
    REQUIRE(r.count_of<FillRoundedRect>() == 1); // the pill, and nothing else
    const FillRoundedRect pill = only_fill(r);

    // The 4px pill times its settled scale of 4.
    REQUIRE_THAT(pill.bounds.width, WithinAbs(16.0f, 1e-3));
    REQUIRE_THAT(pill.bounds.height, WithinAbs(3.0f, 1e-3)); // the pill's height
    REQUIRE(pill.color == theme.accent_button_normal.background);

    // The pill is centered in the item and scales around its own center.
    const float item_w = m.measure("Day", theme.text_style).width + 24.0f;
    REQUIRE(pill.bounds.width < item_w); // emphatically not the item's own width
    REQUIRE_THAT(pill.bounds.left() + pill.bounds.width / 2.0f, WithinAbs(item_w / 2.0f, 1e-2));
}

TEST_CASE("Selecting grows the pill in over a tick clock; without one it lands settled", "[selector-bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    SelectorBarControl bar{{"Day", "Week"}, 0};
    const Size         desired = bar.measure({400.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    const auto pill_width = [&]
    {
        TestRenderer r;
        bar.render(r, theme);
        return only_fill(r).bounds.width;
    };
    // The grow-in sections on the pill's own center, so this never moves while the width does.
    const auto pill_center = [&]
    {
        TestRenderer r;
        bar.render(r, theme);
        const Rect b = only_fill(r).bounds;
        return b.left() + b.width / 2.0f;
    };

    bar.set_selected_index(1);
    REQUIRE_THAT(pill_width(), WithinAbs(16.0f, 1e-3)); // no frame clock, settled immediately

    bar.tick(0ms); // arms `ticking_`
    bar.set_selected_index(0);
    REQUIRE_THAT(pill_width(), WithinAbs(4.0f, 1e-3)); // the pill's width, unscaled
    const float center = pill_center();
    bar.tick(80ms);
    const float mid = pill_width();
    REQUIRE(mid > 4.0f);
    REQUIRE(mid < 16.0f);
    REQUIRE_THAT(pill_center(), WithinAbs(center, 1e-2));
    bar.tick(200ms); // past the pill's 167ms scale animation
    REQUIRE_THAT(pill_width(), WithinAbs(16.0f, 1e-3));
    REQUIRE_THAT(pill_center(), WithinAbs(center, 1e-2));
}

TEST_CASE("Hover and press recolor an item's label instead of filling it", "[selector-bar][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    SelectorBarControl bar{{"Day", "Week"}, 0};
    const Size         desired = bar.measure({400.0f, 200.0f}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    const Point over_second{desired.width * 0.75f, 5.0f};
    bar.handle_pointer({over_second, PointerButton::Left, PointerAction::Move});

    const auto label_color = [&](const SelectorBarControl& b, const std::string& text)
    {
        TestRenderer r;
        b.render(r, theme);
        for (const auto& c : r.commands)
        {
            if (const auto* t = std::get_if<DrawTextRun>(&c); t && t->text == text)
            {
                return t->color;
            }
        }
        return Color{};
    };

    TestRenderer r;
    bar.render(r, theme);
    // Still one fill, the selected pill. Hover paints nothing.
    REQUIRE(r.count_of<FillRoundedRect>() == 1);
    REQUIRE(label_color(bar, "Week") == theme.palette.text_fill_secondary);
    REQUIRE(label_color(bar, "Day") == theme.palette.text_fill_primary);

    bar.handle_pointer({over_second, PointerButton::Left, PointerAction::Down});
    TestRenderer r2;
    bar.render(r2, theme);
    REQUIRE(r2.count_of<FillRoundedRect>() == 1);
    // Item 1 is now the selected one, and it is pressed: the tertiary text color.
    REQUIRE(label_color(bar, "Week") == theme.palette.text_fill_tertiary);
}

// ---- Neither flat era draws a selection indicator (`ControlChrome::accent_selection_indicator`) ----
// MDL2 marks selection with a row fill or the label color, and Metro's 8.1 styles have no
// CornerRadius at all, so neither era gets the Fluent pill.

TEST_CASE("MDL2's SelectorBar draws no pill and marks selection in the label instead", "[selector-bar][render][mdl2]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});
    REQUIRE_FALSE(ui::detail::controls(theme).accent_selection_indicator);

    SelectorBarControl bar{{"Day", "Week", "Month"}, 1};
    const Size         desired = bar.measure({300.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    bar.render(r, theme);

    // Three labels and nothing else: no pill, no hover wash.
    REQUIRE(r.count_of<DrawTextRun>() == 3);
    REQUIRE(r.count_of<FillRoundedRect>() == 0);

    // A header: text_60 (an absolute 0.6 alpha) unselected, text_100 selected. That
    // equals 60 % of colors.text here only because MDL2's is opaque.
    std::vector<Color> label_colors;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            label_colors.push_back(t->color);
        }
    }
    REQUIRE(label_colors.size() == 3);
    CHECK(label_colors[1] == theme.colors().text); // selected: full
    CHECK(label_colors[0] == theme.colors().text.with_alpha(0.6f));
    CHECK(label_colors[2] == label_colors[0]);
    CHECK(label_colors[0] != label_colors[1]);
}

TEST_CASE("Metro's SelectorBar draws no pill and dims to the era's own secondary rung", "[selector-bar][render][metro]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                               OsThemeSnapshot{});
    REQUIRE_FALSE(ui::detail::controls(theme).accent_selection_indicator);

    SelectorBarControl bar{{"Day", "Week", "Month"}, 1};
    const Size         desired = bar.measure({300.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    bar.render(r, theme);

    REQUIRE(r.count_of<DrawTextRun>() == 3);
    REQUIRE(r.count_of<FillRoundedRect>() == 0);

    std::vector<Color> label_colors;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            label_colors.push_back(t->color);
        }
    }
    REQUIRE(label_colors.size() == 3);
    CHECK(label_colors[1] == theme.colors().text);

    // Set to #99FFFFFF, not scaled: Metro's colors.text is #DEFFFFFF, so scaling it by 0.6 would
    // land on ~0x85.
    CHECK(label_colors[0] == Color::from_argb(0x99FFFFFF));
    CHECK(label_colors[0] != theme.colors().text.with_alpha(theme.colors().text.a * 0.6f));
    CHECK(label_colors[2] == label_colors[0]);
}

TEST_CASE("Fluent 2's SelectorBar still draws its pill", "[selector-bar][render]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = resolve_theme(ThemeRequest{}, OsThemeSnapshot{});
    REQUIRE(ui::detail::controls(theme).accent_selection_indicator);

    SelectorBarControl bar{{"Day", "Week", "Month"}, 1};
    const Size         desired = bar.measure({300.0f, Unbounded}, m, theme);
    bar.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    bar.render(r, theme);
    CHECK(r.count_of<FillRoundedRect>() == 1);
}

TEST_CASE("An item's icon comes before its text at 0.8 of a 20px symbol, 8 apart", "[selector-bar][render]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    SelectorBarControl   bar({{"Recent", IconGlyph{0xE823}}, "Shared"});
    (void)bar.measure({600.0f, Unbounded}, m, theme);
    bar.arrange({0.0f, 0.0f, 600.0f, 60.0f});

    TestRenderer r;
    bar.render(r, theme);
    const DrawTextRun* icon   = nullptr;
    const DrawTextRun* recent = nullptr;
    const DrawTextRun* shared = nullptr;
    for (const auto& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            if (t->text == encode_utf8(0xE823))
            {
                icon = t;
            }
            else if (t->text == "Recent")
            {
                recent = t;
            }
            else if (t->text == "Shared")
            {
                shared = t;
            }
        }
    }
    REQUIRE(icon != nullptr);
    REQUIRE(recent != nullptr);
    REQUIRE(shared != nullptr);
    CHECK_THAT(icon->style.font_size, WithinAbs(16.0f, 1e-3));
    // SelectorBarItemPadding's 12, the 20px box's -2 margin: centered 12 - 2 + 10 = 20 in.
    CHECK_THAT(icon->bounds.center().x - bar.bounds().x, WithinAbs(20.0f, 1e-3));
    // The text keeps its padding and moves over by the icon's 16 plus the 8 spacing.
    const float text_width = m.measure("Recent", theme.text_style).width;
    CHECK_THAT(recent->bounds.width, WithinAbs(text_width + 24.0f, 1e-3));
    CHECK_THAT(recent->bounds.x - bar.bounds().x, WithinAbs(24.0f, 1e-3));
    // The second item starts after the first's icon, text and padding.
    CHECK_THAT(shared->bounds.x - bar.bounds().x, WithinAbs(24.0f + text_width + 24.0f, 1e-3));
}
