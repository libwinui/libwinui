// SPDX-License-Identifier: MIT
#include "render/ControlGlyphs.hpp"
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/ToolTip.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/navigation/NavigationShell.hpp"
#include "winui/ui/status/InfoBadge.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
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

// NavigationShell interleaves row washes, labels and the indicator, so a fixed command index would be brittle.
FillRoundedRect fill_at(const TestRenderer& r, std::size_t n)
{
    for (const auto& c : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c); f && n-- == 0)
        {
            return *f;
        }
    }
    return {};
}

// Rows emit a chevron glyph before their label, so a label has no fixed command index.
const DrawTextRun* text_of(const TestRenderer& r, const std::string& text)
{
    for (const auto& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t && t->text == text)
        {
            return t;
        }
    }
    return nullptr;
}

// Two groups of two pages each, plus a trailing leaf. Pre-order indices:
//   0 Group A          3 Group B          6 Loose
//   1  A1              4  B1
//   2  A2              5  B2
// Both groups are headings (`selects_on_invoke == false`).
std::vector<NavEntry> grouped_items()
{
    const auto group = [](std::string label, std::string a, std::string b, bool expanded)
    {
        NavEntry entry{};
        entry.text              = std::move(label);
        entry.expanded          = expanded;
        entry.selects_on_invoke = false;
        entry.children.push_back(NavEntry{std::move(a)});
        entry.children.push_back(NavEntry{std::move(b)});
        return entry;
    };

    std::vector<NavEntry> items;
    items.push_back(group("Group A", "A1", "A2", /*expanded=*/true));
    items.push_back(group("Group B", "B1", "B2", /*expanded=*/false));
    items.push_back(NavEntry{"Loose"});
    return items;
}

void lay_out(NavigationShellControl& view, const TextMeasurer& m, const Theme& theme, Size size)
{
    (void)view.measure(size, m, theme);
    view.arrange({0, 0, size.width, size.height});
}

// A pane with no back or toggle button. The rows start PaneTop under the top:
// the pane's 1px border plus its content's 3px margin.
constexpr float PaneTop = 4.0f;
void            plain(NavigationShellControl& view)
{
    view.set_pane_display_mode(NavigationShellPaneDisplayMode::Left);
    view.set_back_button_visible(AutoVisibility::Collapsed);
    view.set_pane_toggle_button_visible(false);
    view.set_settings_visible(false);
}
} // namespace

TEST_CASE("Adding an item selects it automatically if none was selected", "[navigation-view]")
{
    NavigationShellControl view;
    REQUIRE(view.item_count() == 0);

    view.add_item("One", std::make_unique<TextControl>("content"));
    REQUIRE(view.item_count() == 1);
    REQUIRE(view.selected_index() == 0);

    view.add_item("Two", std::make_unique<TextControl>("content"));
    REQUIRE(view.item_count() == 2);
    REQUIRE(view.selected_index() == 0); // adding a second item doesn't move the selection
}

TEST_CASE("Only the selected item's content is measured", "[navigation-view]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TextStyle large_style{};
    large_style.font_size = 200.0f;

    auto small = std::make_unique<TextControl>("a"); // default 14pt style
    auto large = std::make_unique<TextControl>("a");
    large->set_text_style(large_style);

    NavigationShellControl view;
    plain(view);
    view.add_item("Small", std::move(small));
    view.add_item("Large", std::move(large));

    // pane_height (unbounded available): the rows (2 * 40) between the pane's 4px top and bottom
    // insets, plus the footer's 4px bottom margin.
    const float pane_height = PaneTop + static_cast<float>(view.item_count()) * view.item_row_height() + 4.0f + 4.0f;


    view.set_selected_index(0);
    Size desired = view.measure({300.0f, Unbounded}, m, theme);
    REQUIRE_THAT(desired.height, WithinAbs(pane_height, 1e-2)); // 92 > small content's 18.9

    // The content sits inside a 1px top border.
    view.set_selected_index(1);
    desired = view.measure({300.0f, Unbounded}, m, theme);
    REQUIRE_THAT(desired.height, WithinAbs(200.0f * HeadlessTextMeasurer::LineRatio + 1.0f, 1e-2));
}

TEST_CASE("Clicking a pane row selects that item", "[navigation-view][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    NavigationShellControl view;
    plain(view);
    view.add_item("One", std::make_unique<TextControl>("a"));
    view.add_item("Two", std::make_unique<TextControl>("b"));
    view.add_item("Three", std::make_unique<TextControl>("c"));
    (void)view.measure({400.0f, 300.0f}, m, theme);
    view.arrange({0, 0, 400, 300});

    // Row 1 ("Two") occupies y in [44, 84) at the default item_row_height() of 40.
    const Point p{10.0f, 50.0f};
    view.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    REQUIRE(view.selected_index() == 1);
}

TEST_CASE("set_selected_index clamps and fires on_selection_changed only on an actual change", "[navigation-view]")
{
    NavigationShellControl view;
    view.add_item("One", std::make_unique<TextControl>("a"));
    view.add_item("Two", std::make_unique<TextControl>("b"));
    view.add_item("Three", std::make_unique<TextControl>("c"));

    int         calls = 0;
    std::string last;
    view.on_selection_changed(
        [&](const std::string& key)
        {
            ++calls;
            last = key;
        });

    view.set_selected_index(0); // already selected: no-op
    REQUIRE(calls == 0);

    view.set_selected_index(2);
    REQUIRE(calls == 1);
    REQUIRE(last == "Three"); // an item without a key is keyed by its label

    view.set_selected_index(1000); // clamped to the last item
    REQUIRE(view.selected_index() == 2);
    REQUIRE(calls == 1); // already at the last item: no-op
}

TEST_CASE("With selection_follows_focus, arrow keys move the selected item, clamped (not wrapping), only while focused",
          "[navigation-view][input]")
{
    NavigationShellControl view;
    plain(view);
    view.set_selection_follows_focus(NavigationShellSelectionFollowsFocus::Enabled);
    view.add_item("One", std::make_unique<TextControl>("a"));
    view.add_item("Two", std::make_unique<TextControl>("b"));
    view.add_item("Three", std::make_unique<TextControl>("c"));

    view.handle_key(key_down(Key::ArrowDown)); // not focused: ignored
    REQUIRE(view.selected_index() == 0);

    ui::testing::set_focused(view, true);
    view.handle_key(key_down(Key::ArrowUp)); // already at 0: clamps, stays 0
    REQUIRE(view.selected_index() == 0);

    view.handle_key(key_down(Key::ArrowDown));
    REQUIRE(view.selected_index() == 1);
    view.handle_key(key_down(Key::ArrowDown));
    REQUIRE(view.selected_index() == 2);
    view.handle_key(key_down(Key::ArrowDown)); // clamped at the end
    REQUIRE(view.selected_index() == 2);

    view.handle_key(key_down(Key::ArrowUp));
    REQUIRE(view.selected_index() == 1);
}

TEST_CASE("collect_focusables yields NavigationShell itself plus only the selected item's descendants",
          "[navigation-view][focus]")
{
    NavigationShellControl view;
    plain(view);
    ButtonControl* b0 = view.add_item("One", std::make_unique<ButtonControl>("B0"));
    ButtonControl* b1 = view.add_item("Two", std::make_unique<ButtonControl>("B1"));

    view.set_selected_index(0);
    std::vector<Element*> out;
    view.collect_focusables(out);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == static_cast<Element*>(&view));
    REQUIRE(out[1] == static_cast<Element*>(b0));

    out.clear();
    view.set_selected_index(1);
    view.collect_focusables(out);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == static_cast<Element*>(&view));
    REQUIRE(out[1] == static_cast<Element*>(b1));
}

TEST_CASE("The items are one tab stop whenever there is an item to reach", "[navigation-view][focus]")
{
    // The menu items are a tab stop even when there is only one of them (an item can be
    // invoked whether or not it can move); with no item and no Settings there is nothing to stop on.
    NavigationShellControl view;
    plain(view);
    REQUIRE_FALSE(view.focusable());

    ButtonControl* only = view.add_item("Solo", std::make_unique<ButtonControl>("Only"));
    REQUIRE(view.focusable());
    std::vector<Element*> out;
    view.collect_focusables(out);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == static_cast<Element*>(&view));
    REQUIRE(out[1] == static_cast<Element*>(only));

    view.set_enabled(false);
    REQUIRE_FALSE(view.focusable());

    NavigationShellControl settings_only;
    settings_only.set_pane_display_mode(NavigationShellPaneDisplayMode::Left);
    REQUIRE(settings_only.focusable());
}

// ---- Fluent pane chrome ----

TEST_CASE("A selected pane item is a subtle wash plus an accent indicator, not an accent fill",
          "[navigation-view][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    NavigationShellControl view;
    plain(view);
    view.add_item("One", std::make_unique<TextControl>("a"));
    view.add_item("Two", std::make_unique<TextControl>("b"));
    (void)view.measure({400.0f, 200.0f}, m, theme);
    view.arrange({0, 0, 400, 200});

    TestRenderer r;
    view.render(r, theme);

    // The selected row's subtle hover wash and its indicator. An unselected row draws nothing.
    REQUIRE(r.count_of<FillRoundedRect>() == 2);
    const FillRoundedRect wash      = fill_at(r, 0);
    const FillRoundedRect indicator = fill_at(r, 1);
    REQUIRE(wash.color == theme.subtle_button_hover.background);     // the selected row
    REQUIRE_THAT(wash.radius, WithinAbs(theme.corner_radius, 1e-3)); // the control corner radius, not the overlay one

    REQUIRE(indicator.color == theme.accent_button_normal.background);
    REQUIRE_THAT(indicator.bounds.width, WithinAbs(3.0f, 1e-3));   // the indicator's width
    REQUIRE_THAT(indicator.bounds.height, WithinAbs(16.0f, 1e-3)); // height
    REQUIRE_THAT(indicator.radius, WithinAbs(2.0f, 1e-3));         // and radius
    // 4px in from the pane edge and centered in the 36px pill.
    REQUIRE_THAT(indicator.bounds.left(), WithinAbs(4.0f, 1e-3));
    REQUIRE_THAT(indicator.bounds.top(), WithinAbs(PaneTop + 2.0f + (36.0f - 16.0f) / 2.0f, 1e-3));
}

TEST_CASE("The selection indicator stretches between rows across a tick clock, then settles",
          "[navigation-view][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    NavigationShellControl view;
    plain(view);
    view.add_item("One", std::make_unique<TextControl>("a"));
    view.add_item("Two", std::make_unique<TextControl>("b"));
    (void)view.measure({400.0f, 200.0f}, m, theme);
    view.arrange({0, 0, 400, 200});

    const auto indicator_of = [&](const NavigationShellControl& v)
    {
        TestRenderer r;
        v.render(r, theme);
        for (const auto& c : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->color == theme.accent_button_normal.background)
            {
                return f->bounds;
            }
        }
        return Rect{};
    };

    // Without a frame clock the indicator lands at once, as with Expander's `ticking_`.
    view.set_selected_index(1);
    REQUIRE_THAT(indicator_of(view).height, WithinAbs(16.0f, 1e-3));

    view.set_selected_index(0);
    view.tick(0ms); // arms `ticking_`
    view.set_selected_index(1);

    view.tick(150ms);
    REQUIRE(indicator_of(view).height > 16.0f);
    view.tick(600ms);
    const Rect settled = indicator_of(view);
    REQUIRE_THAT(settled.height, WithinAbs(16.0f, 1e-3));
    REQUIRE_THAT(settled.top(), WithinAbs(PaneTop + 40.0f + 2.0f + (36.0f - 16.0f) / 2.0f, 1e-3));
}

TEST_CASE("Pressing a pane row deepens its wash and dims its label", "[navigation-view][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    NavigationShellControl view;
    plain(view);
    view.add_item("One", std::make_unique<TextControl>("a"));
    view.add_item("Two", std::make_unique<TextControl>("b"));
    (void)view.measure({400.0f, 200.0f}, m, theme);
    view.arrange({0, 0, 400, 200});

    // Hover then press row 1 (rows are 40 tall, from y = 4).
    const Point p{100.0f, 64.0f};
    view.handle_pointer({p, PointerButton::Left, PointerAction::Move});
    view.handle_pointer({p, PointerButton::Left, PointerAction::Down});

    TestRenderer r;
    view.render(r, theme);
    // A pressed selected row keeps the subtle hover wash, and the label drops to the
    // secondary text color.
    const FillRoundedRect wash = fill_at(r, 0);
    REQUIRE(wash.color == theme.subtle_button_hover.background);
    REQUIRE(text_of(r, "Two")->color == theme.palette.text_fill_secondary);

    view.handle_pointer({p, PointerButton::Left, PointerAction::Up});
    TestRenderer r2;
    view.render(r2, theme);
    REQUIRE(text_of(r2, "Two")->color == theme.colors().text);
}

// ---- Neither flat era draws a selection indicator (`ControlChrome::accent_selection_indicator`) ----

TEST_CASE("Metro's NavigationShell fills the selected row with the opaque accent, no indicator bar",
          "[navigation-view][render][metro]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = resolve_theme(ThemeRequest{.mode = ThemeMode::Dark, .variant = ThemeVariant::Metro},
                                               OsThemeSnapshot{});
    REQUIRE_FALSE(ui::detail::controls(theme).accent_selection_indicator);

    NavigationShellControl view;
    plain(view);
    view.add_item("One", std::make_unique<TextControl>("a"));
    view.add_item("Two", std::make_unique<TextControl>("b"));
    (void)view.measure({400.0f, 200.0f}, m, theme);
    view.arrange({0, 0, 400, 200});

    TestRenderer r;
    view.render(r, theme);

    REQUIRE(r.count_of<FillRoundedRect>() == 1);
    const FillRoundedRect row = fill_at(r, 0);

    // Metro names no `row_selection_fills`, so the row takes `colors.selection`, the opaque #FF4617B4.
    CHECK_FALSE(ui::detail::controls(theme).row_selection_fills.has_value());
    CHECK(row.color == Color::from_argb(0xFF4617B4));
    CHECK(row.bounds.width > 3.0f);
    CHECK(row.radius == 0.0f); // Windows 8.1 rounds no corners at all
}

TEST_CASE("MDL2's NavigationShell fills the selected row and draws no indicator bar", "[navigation-view][render][mdl2]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Mdl2}, OsThemeSnapshot{});
    REQUIRE_FALSE(ui::detail::controls(theme).accent_selection_indicator);

    NavigationShellControl view;
    plain(view);
    view.add_item("One", std::make_unique<TextControl>("a"));
    view.add_item("Two", std::make_unique<TextControl>("b"));
    (void)view.measure({400.0f, 200.0f}, m, theme);
    view.arrange({0, 0, 400, 200});

    TestRenderer r;
    view.render(r, theme);

    REQUIRE(r.count_of<FillRoundedRect>() == 1);
    const FillRoundedRect row = fill_at(r, 0);

    // The accent at 40 % in Light, as on an MDL2 ListView row.
    REQUIRE(ui::detail::controls(theme).row_selection_fills.has_value());
    CHECK(row.color == ui::detail::controls(theme).row_selection_fills->rest);
    CHECK(row.color.a < 1.0f);
    CHECK(row.bounds.width > 3.0f); // a row, not an indicator
    CHECK(row.radius == 0.0f);      // the era is square
}

TEST_CASE("Aero's pane paints its own surface and the selected row, and no indicator bar at all",
          "[navigation-view][render][aero]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    REQUIRE_FALSE(ui::detail::controls(theme).accent_selection_indicator);

    NavigationShellControl view;
    plain(view);
    view.add_item("One", std::make_unique<TextControl>("a"));
    view.add_item("Two", std::make_unique<TextControl>("b"));
    (void)view.measure({400.0f, 200.0f}, m, theme);
    view.arrange({0, 0, 400, 200});

    TestRenderer r;
    view.render(r, theme);

    // The pane's gradient goes out as its own command, so the one rounded fill is the selected row.
    REQUIRE(r.count_of<FillRoundedRect>() == 1);
    const FillRoundedRect row = fill_at(r, 0);

    // The era's flat #3399FF highlight at the pack's 2.75 radius. Aero has no control accent for a bar.
    CHECK(row.color == Color::from_argb(0xFF3399FF));
    CHECK(row.color == theme.colors().selection);
    CHECK(row.bounds.width > 3.0f); // a row, not a 2px bar
    CHECK(row.radius == theme.corner_radius);
}

// ---- Hierarchical items ----

TEST_CASE("Nested items share one pre-order index space; only uncollapsed ones get a row", "[navigation-view]")
{
    NavigationShellControl view;
    view.set_items(grouped_items());

    // Every item counts, collapsed or not. selected_index() speaks that index space.
    REQUIRE(view.item_count() == 7);
    REQUIRE(view.text_at(0) == "Group A");
    REQUIRE(view.text_at(2) == "A2");
    REQUIRE(view.text_at(5) == "B2");
    REQUIRE(view.text_at(6) == "Loose");

    // Group B is collapsed, so its two children draw no rows: A, A1, A2, B, Loose.
    REQUIRE(view.visible_row_count() == 5);
    REQUIRE(view.item_at_row(0) == 0);
    REQUIRE(view.item_at_row(3) == 3);
    REQUIRE(view.item_at_row(4) == 6);

    // Expanding B shifts rows, never indices.
    view.set_expanded(3, true);
    REQUIRE(view.item_count() == 7);
    REQUIRE(view.visible_row_count() == 7);
    REQUIRE(view.item_at_row(6) == 6);
    REQUIRE(view.text_at(6) == "Loose");
}

TEST_CASE("set_items selects the first selectable item, skipping headings", "[navigation-view]")
{
    NavigationShellControl view;
    view.set_items(grouped_items());
    REQUIRE(view.selected_index() == 1);

    view.set_selected_index(0);
    REQUIRE(view.selected_index() == 1);
    view.set_selected_index(3);
    REQUIRE(view.selected_index() == 1);
}

TEST_CASE("Clicking a group row toggles it without selecting it; clicking a page selects it", "[navigation-view][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    NavigationShellControl view;
    plain(view);
    view.set_items(grouped_items());
    lay_out(view, m, theme, {400.0f, 400.0f});

    int expanded_calls = 0;
    view.on_expanded_changed([&](const NavigationShellExpandedChangedArgs&) { ++expanded_calls; });

    // Row 3 is "Group B", collapsed. Click its body, well left of the chevron.
    view.handle_pointer({{40.0f, PaneTop + 3.0f * 40.0f + 20.0f}, PointerButton::Left, PointerAction::Down});
    REQUIRE(view.expanded(3));
    REQUIRE(expanded_calls == 1);
    REQUIRE(view.selected_index() == 1); // a heading never takes the selection

    // With B open, row 4 is "B1".
    view.handle_pointer({{40.0f, PaneTop + 4.0f * 40.0f + 20.0f}, PointerButton::Left, PointerAction::Down});
    REQUIRE(view.selected_index() == 4);
}

TEST_CASE("Clicking a group's chevron toggles it and nothing else", "[navigation-view][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    NavigationShellControl view;
    plain(view);
    std::vector<NavEntry> items;
    {
        NavEntry group{};
        group.text = "Group";
        group.children.push_back(NavEntry{"Child"});
        items.push_back(std::move(group));
        items.push_back(NavEntry{"Other"});
    }
    view.set_items(std::move(items));
    view.set_selected_index(2); // "Other", so a stray body click on the group would show up
    lay_out(view, m, theme, {400.0f, 400.0f});
    REQUIRE(view.selected_index() == 2);

    // The chevron box is the rightmost 40px of the pill, which is itself inset 4px
    // (the chevron's margin cancels the content's own right margin).
    const float pill_right = view.pane_width() - 4.0f;
    view.handle_pointer({{pill_right - 20.0f, PaneTop + 20.0f}, PointerButton::Left, PointerAction::Down});

    REQUIRE(view.expanded(0));
    REQUIRE(view.selected_index() == 2); // the chevron never invokes the item

    view.handle_pointer({{40.0f, PaneTop + 20.0f}, PointerButton::Left, PointerAction::Down});
    REQUIRE_FALSE(view.expanded(0));
    REQUIRE(view.selected_index() == 0);
}

TEST_CASE("A child row indents its label only - the pill and the indicator stay flush", "[navigation-view][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    NavigationShellControl view;
    plain(view);
    view.set_items(grouped_items());
    view.set_selected_index(2); // "A2", a child of the expanded Group A
    lay_out(view, m, theme, {400.0f, 400.0f});

    TestRenderer r;
    view.render(r, theme);

    // c_itemIndentation (31) on top of the pill's own 4px inset and
    // the 12px the label already sat at.
    const DrawTextRun* group_label = text_of(r, "Group A");
    const DrawTextRun* child_label = text_of(r, "A2");
    REQUIRE(group_label != nullptr);
    REQUIRE(child_label != nullptr);
    REQUIRE_THAT(group_label->bounds.left(), WithinAbs(4.0f + 12.0f, 1e-3));
    REQUIRE_THAT(child_label->bounds.left(), WithinAbs(4.0f + 31.0f + 12.0f, 1e-3));

    // Neither the pill nor the indicator follows the indent: the indent moves only the
    // row content, a sibling of both.
    const FillRoundedRect wash      = fill_at(r, 0);
    const FillRoundedRect indicator = fill_at(r, 1);
    REQUIRE(indicator.color == theme.accent_button_normal.background);
    REQUIRE_THAT(indicator.bounds.left(), WithinAbs(4.0f, 1e-3));
    REQUIRE_THAT(wash.bounds.left(), WithinAbs(4.0f, 1e-3));
}

TEST_CASE("A group row draws a chevron that flips when it opens", "[navigation-view][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    NavigationShellControl view;
    plain(view);
    view.set_items(grouped_items());
    lay_out(view, m, theme, {400.0f, 400.0f});

    TestRenderer r;
    view.render(r, theme);

    const std::string down = encode_utf8(glyphs::NavChevronCollapsed.codepoint);
    const std::string up   = encode_utf8(glyphs::NavChevronExpanded.codepoint);
    REQUIRE(text_of(r, up) != nullptr);   // Group A is open
    REQUIRE(text_of(r, down) != nullptr); // Group B is not

    // A leaf row has no chevron at all: two groups on screen, two glyphs.
    std::size_t chevrons = 0;
    for (const auto& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t && (t->text == up || t->text == down))
        {
            ++chevrons;
        }
    }
    REQUIRE(chevrons == 2);
}

TEST_CASE("Collapsing the selected page's group moves the indicator onto the group's own row",
          "[navigation-view][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    NavigationShellControl view;
    plain(view);
    view.set_items(grouped_items());
    view.set_selected_index(2); // "A2", row 2 while Group A is open
    lay_out(view, m, theme, {400.0f, 400.0f});

    const auto indicator_top = [&]
    {
        TestRenderer r;
        view.render(r, theme);
        for (const auto& c : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->color == theme.accent_button_normal.background)
            {
                return f->bounds.top();
            }
        }
        return -1.0f;
    };

    const float bar_inset = 2.0f + (36.0f - 16.0f) / 2.0f; // pill margin + centering inside the 36px pill
    REQUIRE_THAT(indicator_top(), WithinAbs(PaneTop + 2.0f * 40.0f + bar_inset, 1e-3));

    // The selection stays, but the bar climbs to row 0, the deepest visible ancestor.
    view.set_expanded(0, false);
    REQUIRE(view.selected_index() == 2);
    REQUIRE_THAT(indicator_top(), WithinAbs(PaneTop + bar_inset, 1e-3));

    // The group is still not selected, so the only fill left is the indicator.
    TestRenderer r;
    view.render(r, theme);
    REQUIRE(r.count_of<FillRoundedRect>() == 1);
}

TEST_CASE("Space and Left/Right expand and collapse the group under the keyboard cursor", "[navigation-view][input]")
{
    NavigationShellControl view;
    plain(view);
    view.set_items(grouped_items());
    ui::testing::set_focused(view, true);

    // The cursor starts on "A1". Up steps onto the heading without selecting it, which is the only
    // way a keyboard reaches a group.
    REQUIRE(view.selected_index() == 1);
    view.handle_key(key_down(Key::ArrowUp));
    REQUIRE(view.selected_index() == 1);

    view.handle_key(key_down(Key::Space)); // collapse Group A
    REQUIRE_FALSE(view.expanded(0));
    REQUIRE(view.visible_row_count() == 3); // A, B, Loose

    view.handle_key(key_down(Key::ArrowRight));
    REQUIRE(view.expanded(0));
    // Left/Right are only claimed from Window's directional focus movement when there is a
    // group to work on.
    REQUIRE(view.handle_key(key_down(Key::ArrowLeft)));
    REQUIRE_FALSE(view.expanded(0));
    NavigationShellControl flat;
    plain(flat);
    flat.add_item("One", std::make_unique<TextControl>("a"));
    flat.add_item("Two", std::make_unique<TextControl>("b"));
    ui::testing::set_focused(flat, true);
    REQUIRE(flat.handle_key({Key::ArrowDown, KeyAction::Up}));
    REQUIRE_FALSE(flat.handle_key({Key::ArrowLeft, KeyAction::Up}));
}

TEST_CASE("An overflowing pane clips, scrolls on the wheel, and follows the keyboard cursor", "[navigation-view][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    NavigationShellControl view;
    plain(view);
    std::vector<NavEntry> items;
    items.reserve(20);
    for (int i = 0; i < 20; ++i)
    {
        items.push_back(NavEntry{"Item " + std::to_string(i)});
    }
    view.set_items(std::move(items));

    // 20 rows x 40 = 800 DIPs of content in a ~140 tall viewport.
    lay_out(view, m, theme, {400.0f, 150.0f});
    const float viewport = view.bounds().height;
    REQUIRE(viewport < 800.0f);

    REQUIRE(view.handle_wheel({{40.0f, 40.0f}, PointerButton::None, PointerAction::Wheel, 100.0f}));
    REQUIRE_THAT(view.pane_scroll_offset(), WithinAbs(100.0f, 1e-3));

    view.handle_wheel({{40.0f, 40.0f}, PointerButton::None, PointerAction::Wheel, -1000.0f});
    REQUIRE_THAT(view.pane_scroll_offset(), WithinAbs(0.0f, 1e-3));

    // The menu's viewport is the pane less its 4px top inset, the footer's 4px margin and
    // the 4px bottom inset.
    const float menu_viewport = viewport - PaneTop - 4.0f - 4.0f;
    ui::testing::set_focused(view, true);
    for (int i = 0; i < 6; ++i)
    {
        view.handle_key(key_down(Key::ArrowDown));
    }
    REQUIRE(view.selected_index() == 0); // selection_follows_focus is off: the cursor moved, not the selection
    REQUIRE_THAT(view.pane_scroll_offset(), WithinAbs(7.0f * 40.0f - menu_viewport, 1e-3));
    view.handle_key(key_down(Key::Enter));
    REQUIRE(view.selected_index() == 6);
}

TEST_CASE("An item with no content of its own shows the view-level content", "[navigation-view]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    NavigationShellControl view;
    plain(view);
    view.set_items(grouped_items());
    // The content is one control the items share; they carry no pages of their own.
    ButtonControl* frame = view.set_content(std::make_unique<ButtonControl>("Frame"));

    std::vector<Element*> out;
    view.collect_focusables(out);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == static_cast<Element*>(&view));
    REQUIRE(out[1] == static_cast<Element*>(frame));

    lay_out(view, m, theme, {500.0f, 300.0f});
    REQUIRE(frame->bounds().left() >= view.bounds().left() + view.pane_width());

    std::vector<NavEntry> items;
    NavEntry              own{};
    own.text    = "Own";
    own.content = std::make_unique<ButtonControl>("Own page");
    items.push_back(std::move(own));
    items.push_back(NavEntry{"Borrowed"});
    view.set_items(std::move(items));

    out.clear();
    view.collect_focusables(out);
    REQUIRE(out.size() == 2);
    REQUIRE(out[1] != static_cast<Element*>(frame)); // the item's own content, not the frame

    view.set_selected_index(1);
    out.clear();
    view.collect_focusables(out);
    REQUIRE(out[1] == static_cast<Element*>(frame)); // falls back to the frame
}

// ---- The pane as a surface of its own ----

TEST_CASE("Aero's pane is a pale-blue surface with navy links, not black rows on the page",
          "[navigation-view][theme][retro][aero]")
{
    const Theme theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    REQUIRE(ui::detail::controls(theme).nav_pane.background.has_value());
    REQUIRE(ui::detail::controls(theme).nav_pane.text.has_value());
    CHECK(*ui::detail::controls(theme).nav_pane.text == Color::from_argb(0xFF151C55)); // `TaskLink`/`NavigationPaneLabel`, Windows 7
    CHECK(*ui::detail::controls(theme).nav_pane.text_hot == Color::from_argb(0xFF074AE5)); // its Hot/Pressed rung
    CHECK(ui::detail::controls(theme).nav_pane.background->stops.front().color == Color::from_argb(0xFFEDF2F9));
    CHECK(ui::detail::controls(theme).nav_pane.background->stops.back().color == Color::from_argb(0xFFEDF5FF));

    NavigationShellControl nav;
    std::vector<NavEntry>  items;
    NavEntry               first{};
    first.text = "Toolbar";
    NavEntry second{};
    second.text = "Menu";
    items.push_back(std::move(first));
    items.push_back(std::move(second));
    plain(nav);
    nav.set_items(std::move(items));
    nav.set_selected_index(0);
    nav.set_pane_width(280.0f);
    nav.arrange(Rect{0.0f, 0.0f, 600.0f, 300.0f});

    TestRenderer renderer;
    nav.render(renderer, theme);

    const auto* pane = std::get_if<FillRoundedRectGradient>(&renderer.commands.front());
    REQUIRE(pane != nullptr);
    CHECK(pane->bounds.width == 280.0f);
    CHECK(pane->bounds.height == 300.0f);

    // An unselected row's label is navy on the pane, not `colors.text` on the page.
    bool navy = false;
    for (const RenderCommand& c : renderer.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t != nullptr && t->text == "Menu")
        {
            navy = t->color == *ui::detail::controls(theme).nav_pane.text;
            CHECK(t->color != theme.colors().text);
        }
    }
    CHECK(navy);
}

TEST_CASE("A theme that names no pane surface keeps a transparent pane and colors.text rows", "[navigation-view][theme]")
{
    // `nav_pane` is Aero's alone. Fluent leaves the pane transparent because on Windows 11 the Mica
    // behind the window is the pane.
    for (ThemeVariant v :
         {ThemeVariant::Fluent2, ThemeVariant::Fluent, ThemeVariant::Metro, ThemeVariant::Luna, ThemeVariant::Windows9x})
    {
        const Theme theme = resolve_theme(ThemeRequest{.variant = v}, OsThemeSnapshot{});
        CHECK_FALSE(ui::detail::controls(theme).nav_pane.background.has_value());
        CHECK_FALSE(ui::detail::controls(theme).nav_pane.text.has_value());
        CHECK_FALSE(ui::detail::controls(theme).nav_pane.text_hot.has_value());
    }
}

TEST_CASE("An item's icon takes the 40px icon column, and its InfoBadge sits right of the label",
          "[navigation_view][render]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    std::vector<NavEntry> items;
    items.push_back(NavEntry{"Home"});
    NavEntry inbox{"Inbox"};
    inbox.icon       = IconGlyph{0xE715};
    inbox.info_badge = std::make_unique<InfoBadgeControl>(5);
    items.push_back(std::move(inbox));

    NavigationShellControl nav;
    plain(nav);
    nav.set_pane_width(300.0f);
    nav.set_items(std::move(items));
    (void)nav.measure({600.0f, 400.0f}, m, theme);
    nav.arrange({0.0f, 0.0f, 600.0f, 400.0f});

    TestRenderer r;
    nav.render(r, theme);
    const DrawTextRun* home  = text_of(r, "Home");
    const DrawTextRun* label = text_of(r, "Inbox");
    const DrawTextRun* icon  = text_of(r, encode_utf8(0xE715));
    const DrawTextRun* value = text_of(r, "5");
    REQUIRE(home != nullptr);
    REQUIRE(label != nullptr);
    REQUIRE(icon != nullptr);
    REQUIRE(value != nullptr);

    const float left = nav.bounds().x;
    REQUIRE_THAT(home->bounds.x, WithinAbs(left + 4.0 + 12.0, 1e-3)); // pill inset + collapsed icon column + margin
    REQUIRE_THAT(label->bounds.x, WithinAbs(left + 4.0 + 40.0 + 4.0, 1e-3));   // pill inset + icon column + margin
    REQUIRE_THAT(icon->bounds.center().x, WithinAbs(left + 4.0 + 20.0, 1e-3)); // centered in the icon column

    InfoBadgeControl* badge = nav.info_badge_at(1);
    REQUIRE(badge != nullptr);
    REQUIRE_THAT(badge->bounds().right(), WithinAbs(left + 300.0 - 4.0 - 14.0, 1e-3));
    REQUIRE(label->bounds.right() <= badge->bounds().left() - 8.0f + 1e-3f);

    nav.set_info_badge(1, nullptr);
    REQUIRE(nav.info_badge_at(1) == nullptr);
    TestRenderer without;
    nav.render(without, theme);
    REQUIRE(text_of(without, "5") == nullptr);
}

// ---- Display modes, pane chrome, footer items, the top bar and flyouts ----

namespace
{
constexpr IconGlyph Glyph{0xE80F};

std::vector<NavEntry> icon_items(std::initializer_list<const char*> labels)
{
    std::vector<NavEntry> items;
    for (const char* label : labels)
    {
        NavEntry e{label};
        e.icon = Glyph;
        items.push_back(std::move(e));
    }
    return items;
}

PointerEvent press(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent release(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}
void click(Element& e, Point p)
{
    e.handle_pointer(press(p));
    e.handle_pointer(release(p));
}
} // namespace

TEST_CASE("NavigationShell starts with its documented defaults", "[navigation-view]")
{
    NavigationShellControl view;
    CHECK(view.pane_display_mode() == NavigationShellPaneDisplayMode::Auto);
    CHECK(view.display_mode() == NavigationShellDisplayMode::Minimal); // until the first layout
    CHECK(view.is_pane_open());
    CHECK(view.pane_width() == 320.0f);         // open_pane_length
    CHECK(view.compact_pane_length() == 48.0f); // the compact pane length
    CHECK(view.compact_mode_threshold_width() == 641.0f);
    CHECK(view.expanded_mode_threshold_width() == 1008.0f);
    CHECK(view.back_button_visible() == AutoVisibility::Auto);
    CHECK_FALSE(view.is_back_enabled());
    CHECK(view.is_settings_visible());
    CHECK(view.is_pane_toggle_button_visible());
    CHECK(view.always_show_header());
    CHECK(view.selection_follows_focus() == NavigationShellSelectionFollowsFocus::Disabled);
    CHECK(view.text_at(NavigationShellControl::Settings) == "Settings");
}

TEST_CASE("Auto resolves Expanded, Compact and Minimal from the control's own width", "[navigation-view][layout]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();

    NavigationShellControl view;
    view.set_items(icon_items({"One", "Two"}));
    std::vector<NavigationShellDisplayMode> changes;
    view.on_display_mode_changed([&](NavigationShellDisplayMode mode) { changes.push_back(mode); });

    // Auto: Expanded from 1008, Minimal below 641, Compact between.
    lay_out(view, m, theme, {1100.0f, 500.0f});
    CHECK(view.display_mode() == NavigationShellDisplayMode::Expanded);
    CHECK(view.is_pane_open());
    CHECK_THAT(view.content_bounds().x - view.bounds().x, WithinAbs(320.0f, 1e-3));

    lay_out(view, m, theme, {800.0f, 500.0f});
    CHECK(view.display_mode() == NavigationShellDisplayMode::Compact);
    CHECK_FALSE(view.is_pane_open()); // Expanded -> Compact closes it
    CHECK_THAT(view.content_bounds().x - view.bounds().x, WithinAbs(48.0f, 1e-3));
    CHECK_THAT(view.pane_bounds().width, WithinAbs(48.0f, 1e-3));

    lay_out(view, m, theme, {500.0f, 500.0f});
    CHECK(view.display_mode() == NavigationShellDisplayMode::Minimal);
    CHECK_THAT(view.content_bounds().x, WithinAbs(view.bounds().x, 1e-3));
    CHECK(view.pane_bounds().width == 0.0f);

    lay_out(view, m, theme, {1100.0f, 500.0f});
    CHECK(view.is_pane_open()); // growing back into Expanded reopens it

    REQUIRE(changes.size() == 4);
    CHECK(changes[0] == NavigationShellDisplayMode::Expanded);
    CHECK(changes[1] == NavigationShellDisplayMode::Compact);
    CHECK(changes[2] == NavigationShellDisplayMode::Minimal);
    CHECK(changes[3] == NavigationShellDisplayMode::Expanded);
}

TEST_CASE("A pane that loads Compact or Minimal starts closed", "[navigation-view][layout]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_items(icon_items({"One"}));
    lay_out(view, m, theme, {800.0f, 400.0f});
    CHECK(view.display_mode() == NavigationShellDisplayMode::Compact);
    CHECK_FALSE(view.is_pane_open());
}

TEST_CASE("The back and toggle buttons stack at the pane's top-left, and the items start under them",
          "[navigation-view][layout]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_pane_display_mode(NavigationShellPaneDisplayMode::Left);
    view.set_items(icon_items({"One", "Two"}));
    lay_out(view, m, theme, {800.0f, 600.0f});
    const Point o{view.bounds().x, view.bounds().y};

    // The Auto layout: the button column's 0,4 margin, the back button 40x36 at 4,2, the
    // toggle button 40 further down at 4,2 inside its 48px width.
    const Rect back = view.back_button()->bounds();
    CHECK_THAT(back.x - o.x, WithinAbs(4.0f, 1e-3));
    CHECK_THAT(back.y - o.y, WithinAbs(6.0f, 1e-3));
    CHECK_THAT(back.width, WithinAbs(40.0f, 1e-3));
    CHECK_THAT(back.height, WithinAbs(36.0f, 1e-3));
    const Rect toggle = view.toggle_button()->bounds();
    CHECK_THAT(toggle.x - o.x, WithinAbs(4.0f, 1e-3));
    CHECK_THAT(toggle.y - o.y, WithinAbs(46.0f, 1e-3));
    CHECK_THAT(toggle.width, WithinAbs(40.0f, 1e-3));

    // The pane: 1 + 3 down, the back row (40) and the toggle row (40), then the items.
    const auto first = view.item_bounds(0);
    REQUIRE(first.has_value());
    CHECK_THAT(first->y - o.y, WithinAbs(86.0f, 1e-3));
    CHECK_THAT(first->x - o.x, WithinAbs(4.0f, 1e-3));
    CHECK_THAT(first->width, WithinAbs(320.0f - 8.0f, 1e-3));

    // back_enabled is false by default: the button shows, disabled.
    CHECK_FALSE(view.back_button()->enabled());
    int back_requests = 0;
    view.on_back_requested([&] { ++back_requests; });
    view.set_back_enabled(true);
    click(view, view.back_button()->bounds().center());
    CHECK(back_requests == 1);

    view.set_back_button_visible(AutoVisibility::Collapsed);
    lay_out(view, m, theme, {800.0f, 600.0f});
    CHECK_THAT(view.item_bounds(0)->y - o.y, WithinAbs(46.0f, 1e-3));
    CHECK_THAT(view.toggle_button()->bounds().y - o.y, WithinAbs(6.0f, 1e-3));
}

TEST_CASE("The toggle button closes an Expanded pane to its strip, and the close is remembered",
          "[navigation-view][input]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_items(icon_items({"One", "Two"}));
    lay_out(view, m, theme, {1100.0f, 500.0f});
    REQUIRE(view.is_pane_open());

    int closing = 0;
    int closed  = 0;
    view.on_pane_closing([&] { ++closing; });
    view.on_pane_closed([&] { ++closed; });
    click(view, view.toggle_button()->bounds().center());
    CHECK_FALSE(view.is_pane_open());
    CHECK(closing == 1);
    CHECK(closed == 1);
    lay_out(view, m, theme, {1100.0f, 500.0f});
    CHECK_THAT(view.content_bounds().x - view.bounds().x, WithinAbs(48.0f, 1e-3)); // CompactInline
    CHECK(view.display_mode() == NavigationShellDisplayMode::Expanded);

    // Closed by a resize: a resize that stays Expanded leaves it closed.
    lay_out(view, m, theme, {1200.0f, 500.0f});
    CHECK_FALSE(view.is_pane_open());

    click(view, view.toggle_button()->bounds().center());
    CHECK(view.is_pane_open());
}

TEST_CASE("A Compact pane opens over the content and light-dismisses", "[navigation-view][input]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_items(icon_items({"One", "Two"}));
    ButtonControl* page        = view.set_content(std::make_unique<ButtonControl>("Page"));
    int            page_clicks = 0;
    page->on_click([&] { ++page_clicks; });
    lay_out(view, m, theme, {800.0f, 500.0f});
    const float content_x = view.content_bounds().x;

    click(view, view.toggle_button()->bounds().center());
    REQUIRE(view.is_pane_open());
    lay_out(view, m, theme, {800.0f, 500.0f});
    // CompactOverlay: the pane lies over the content, which keeps its column.
    CHECK_THAT(view.content_bounds().x, WithinAbs(content_x, 1e-3));
    CHECK_THAT(view.pane_bounds().width, WithinAbs(320.0f, 1e-3));

    bool cancel = true;
    view.on_pane_closing([&] { return cancel ? ui::Cancel : ui::Proceed; });
    const Point outside{view.bounds().right() - 20.0f, view.bounds().bottom() - 20.0f};
    click(view, outside);
    CHECK(view.is_pane_open());
    CHECK(page_clicks == 0); // the content under the dismiss layer gets nothing

    cancel = false;
    click(view, outside);
    CHECK_FALSE(view.is_pane_open());

    click(view, view.toggle_button()->bounds().center());
    REQUIRE(view.is_pane_open());
    CHECK(view.handle_key({Key::Escape, KeyAction::Down}));
    CHECK_FALSE(view.is_pane_open());

    // Invoking an item closes an overlaying pane.
    click(view, view.toggle_button()->bounds().center());
    lay_out(view, m, theme, {800.0f, 500.0f});
    click(view, view.item_bounds(1)->center());
    CHECK(view.selected_index() == 1);
    CHECK_FALSE(view.is_pane_open());
}

TEST_CASE("Minimal puts the toggle button beside the back button and swaps in the close button while open",
          "[navigation-view][layout]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_pane_display_mode(NavigationShellPaneDisplayMode::LeftMinimal);
    view.set_items(icon_items({"One", "Two"}));
    view.set_header("Header");
    view.set_always_show_header(false); // ignored in Minimal
    lay_out(view, m, theme, {800.0f, 500.0f});
    const Point o{view.bounds().x, view.bounds().y};
    REQUIRE_FALSE(view.is_pane_open());

    // The LeftMinimal layout: back at 4, toggle at 40 (44 from the edge), same row.
    CHECK_THAT(view.back_button()->bounds().x - o.x, WithinAbs(4.0f, 1e-3));
    CHECK_THAT(view.toggle_button()->bounds().x - o.x, WithinAbs(44.0f, 1e-3));
    CHECK_THAT(view.toggle_button()->bounds().y - o.y, WithinAbs(6.0f, 1e-3));
    CHECK(view.pane_bounds().width == 0.0f);

    // The header shares the top with them: the buttons' 40 + 40 less 24, 44 down.
    TestRenderer r;
    view.render(r, theme);
    const DrawTextRun* header = text_of(r, "Header");
    REQUIRE(header != nullptr);
    CHECK_THAT(header->bounds.x - o.x, WithinAbs(56.0f, 1e-3));
    CHECK_THAT(header->bounds.y - o.y, WithinAbs(1.0f + 44.0f, 1e-3));

    // Open: the pane lies over the content from the left edge, and the back button gives way to
    // the close button.
    view.set_pane_open(true);
    lay_out(view, m, theme, {800.0f, 500.0f});
    CHECK_THAT(view.pane_bounds().width, WithinAbs(320.0f, 1e-3));
    CHECK(view.back_button()->bounds().width == 0.0f);
    CHECK(view.close_button()->bounds().width > 0.0f);
    click(view, view.close_button()->bounds().center());
    CHECK_FALSE(view.is_pane_open());
}

TEST_CASE("The header shows above the page when set, and always_show_header off hides it outside Minimal",
          "[navigation-view][layout]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    plain(view);
    view.set_items(icon_items({"One"}));
    ButtonControl* page = view.set_content(std::make_unique<ButtonControl>("Page"));
    lay_out(view, m, theme, {900.0f, 500.0f});
    const float no_header_top = page->bounds().y;

    view.set_header("This is Header Text");
    lay_out(view, m, theme, {900.0f, 500.0f});
    // The header's 56,44 margin, its content at least 36 tall.
    CHECK(page->bounds().y >= no_header_top + 44.0f + 36.0f - 1e-3f);
    TestRenderer r;
    view.render(r, theme);
    const DrawTextRun* header = text_of(r, "This is Header Text");
    REQUIRE(header != nullptr);
    CHECK_THAT(header->bounds.x - view.content_bounds().x, WithinAbs(1.0f + 56.0f, 1e-3));
    CHECK(header->style.font_size == theme.type.title.font_size);

    view.set_always_show_header(false);
    lay_out(view, m, theme, {900.0f, 500.0f});
    CHECK_THAT(page->bounds().y, WithinAbs(no_header_top, 1e-3));
}

TEST_CASE("Footer items and Settings sit at the bottom of the pane in the same selection model",
          "[navigation-view][layout][input]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    plain(view);
    view.set_settings_visible(true);
    view.set_items(icon_items({"Browse", "Track", "History"}));
    view.set_footer_items(icon_items({"Account", "Cart"}));
    lay_out(view, m, theme, {900.0f, 500.0f});

    // Pre-order: the menu items first, then the footer items.
    CHECK(view.item_count() == 5);
    CHECK(view.footer_item_count() == 2);
    CHECK(view.text_at(3) == "Account");

    // The footer items and Settings end at the pane's bottom (4px inset); Settings is last.
    const auto settings = view.item_bounds(NavigationShellControl::Settings);
    const auto cart     = view.item_bounds(4);
    REQUIRE(settings.has_value());
    REQUIRE(cart.has_value());
    CHECK_THAT(settings->bottom(), WithinAbs(view.bounds().bottom() - 4.0f - 2.0f, 1e-3));
    CHECK_THAT(cart->bottom() + 4.0f, WithinAbs(settings->y, 1e-3));

    click(view, cart->center());
    CHECK(view.selected_index() == 4);
    click(view, settings->center());
    CHECK(view.selected_index() == NavigationShellControl::Settings);
    CHECK(view.settings_selected());

    view.set_settings_visible(false);
    CHECK(view.selected_index() == std::nullopt);
}


TEST_CASE("Menu and footer items share a pane too short for both, with the separator between",
          "[navigation-view][layout]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    plain(view);
    view.set_items(icon_items({"1", "2", "3", "4", "5", "6", "7", "8"}));
    view.set_footer_items(icon_items({"A", "B"}));

    lay_out(view, m, theme, {900.0f, 600.0f});
    REQUIRE(view.item_bounds(7).has_value());
    TestRenderer roomy;
    view.render(roomy, theme);
    const auto divider_count = [&](const TestRenderer& r)
    {
        std::size_t n = 0;
        for (const auto& c : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&c);
                f && f->color == theme.colors().divider && f->bounds.height == 1.0f)
            {
                ++n;
            }
        }
        return n;
    };
    CHECK(divider_count(roomy) == 0);

    // 320 of menu and 80 of footer in a pane of about 250: the footer group fits in half, so the
    // menu gives way and scrolls.
    lay_out(view, m, theme, {900.0f, 260.0f});
    const auto a = view.item_bounds(8);
    const auto b = view.item_bounds(9);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK_THAT(b->bottom(), WithinAbs(view.bounds().bottom() - 4.0f - 2.0f, 1e-3));
    CHECK_FALSE(view.item_bounds(7).has_value()); // below the menu's viewport
    TestRenderer tight;
    view.render(tight, theme);
    CHECK(divider_count(tight) == 1);

    REQUIRE(view.handle_wheel({view.item_bounds(0)->center(), PointerButton::None, PointerAction::Wheel, 400.0f}));
    CHECK(view.pane_scroll_offset() > 0.0f);
    CHECK(view.item_bounds(7).has_value());
}

TEST_CASE("Header and separator items are drawn but never selected or given the cursor", "[navigation-view][input]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    plain(view);
    std::vector<NavEntry> items = icon_items({"Play"});
    NavEntry              header{"Actions"};
    header.kind = NavEntryKind::Header;
    items.push_back(std::move(header));
    items.push_back(std::move(icon_items({"Save"}).front()));
    NavEntry separator{};
    separator.kind = NavEntryKind::Separator;
    items.push_back(std::move(separator));
    items.push_back(std::move(icon_items({"Refresh"}).front()));
    view.set_items(std::move(items));
    lay_out(view, m, theme, {900.0f, 500.0f});

    // A header is 40 tall, a separator 1 + 3 + 4.
    const Rect play    = *view.item_bounds(0);
    const Rect save    = *view.item_bounds(2);
    const Rect refresh = *view.item_bounds(4);
    CHECK_THAT(save.y - play.y, WithinAbs(40.0f + 40.0f, 1e-3));
    CHECK_THAT(refresh.y - save.y, WithinAbs(40.0f + 8.0f, 1e-3));

    TestRenderer r;
    view.render(r, theme);
    const DrawTextRun* label = text_of(r, "Actions");
    REQUIRE(label != nullptr);
    CHECK(label->color == theme.palette.text_fill_secondary); // the header's text color
    CHECK(label->style.weight == FontWeight::SemiBold);
    CHECK_THAT(label->bounds.x - view.bounds().x, WithinAbs(16.0f, 1e-3)); // the header's 16 DIP inset

    click(view, Point{play.center().x, play.bottom() + 20.0f});
    CHECK(view.selected_index() == 0);
    ui::testing::set_focused(view, true);
    view.handle_key(key_down(Key::ArrowDown));
    view.handle_key(key_down(Key::Enter));
    CHECK(view.selected_index() == 2);
    view.handle_key(key_down(Key::ArrowDown));
    view.handle_key(key_down(Key::Space));
    CHECK(view.selected_index() == 4);
    view.set_selected_index(1);
    CHECK(view.selected_index() == 4);
}

TEST_CASE("Without selection_follows_focus the cursor moves and Enter or Space invokes", "[navigation-view][input]")
{
    NavigationShellControl view;
    plain(view);
    view.set_items(icon_items({"One", "Two", "Three"}));
    std::vector<std::string> invoked;
    view.on_item_invoked([&](const std::string& key) { invoked.push_back(key); });
    ui::testing::set_focused(view, true);

    view.handle_key(key_down(Key::ArrowDown));
    view.handle_key(key_down(Key::ArrowDown));
    CHECK(view.selected_index() == 0);
    view.handle_key(key_down(Key::Enter));
    CHECK(view.selected_index() == 2);
    // on_item_invoked also fires for the item that is already selected.
    view.handle_key(key_down(Key::Space));
    REQUIRE(invoked.size() == 2);
    CHECK(invoked[0] == "Three");
    CHECK(invoked[1] == "Three");

    view.handle_key(key_down(Key::Home));
    view.handle_key(key_down(Key::Enter));
    CHECK(view.selected_index() == 0);
}

TEST_CASE("A closed compact pane shows icons only, folds headers away and opens a group's children in a flyout",
          "[navigation-view][flyout]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_pane_display_mode(NavigationShellPaneDisplayMode::LeftCompact);
    view.set_back_button_visible(AutoVisibility::Collapsed);
    view.set_settings_visible(false);

    std::vector<NavEntry> items = icon_items({"Home"});
    NavEntry              account{"Account"};
    account.icon = Glyph;
    for (auto& child : icon_items({"Mail", "Calendar"}))
    {
        account.children.push_back(std::move(child));
    }
    items.push_back(std::move(account));
    view.set_items(std::move(items));
    lay_out(view, m, theme, {800.0f, 500.0f});
    REQUIRE_FALSE(view.is_pane_open());

    CHECK(view.visible_row_count() == 2);
    const Rect group = *view.item_bounds(1);
    CHECK_THAT(group.width, WithinAbs(40.0f, 1e-3));

    click(view, group.center());
    REQUIRE(view.flyout_open());
    REQUIRE(ui::testing::popup_open(view));
    CHECK(ui::testing::ElementAccess::popup_request(view)->placement == PopupPlacement::Right);
    Element* list = ui::testing::popup_content(view);
    REQUIRE(list != nullptr);
    REQUIRE(ui::testing::ElementAccess::place_popup(view, *list, Size{800.0f, 500.0f}, ui::MeasureContext{m, theme}));
    // To the right of the item (the 48px slot), top edges level, 4 up (the menu's -4 top margin).
    CHECK_THAT(list->bounds().x, WithinAbs(group.right() + 4.0f, 1e-3));
    CHECK_THAT(list->bounds().y, WithinAbs(group.y - 2.0f - 4.0f, 1e-3));
    CHECK(list->bounds().width >= 96.0f);

    // Rows start after the presenter's 1px frame and 2px padding.
    const Point calendar{list->bounds().center().x, list->bounds().y + 1.0f + 2.0f + 40.0f + 20.0f};
    list->handle_pointer(press(calendar));
    CHECK(view.selected_index() == 3);
    CHECK_FALSE(view.flyout_open());

    TestRenderer r;
    view.render(r, theme);
    bool on_group = false;
    for (const auto& c : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->color == theme.accent_button_normal.background)
        {
            on_group = f->bounds.center().y > group.y && f->bounds.center().y < group.bottom();
        }
    }
    CHECK(on_group);
}

TEST_CASE("A closed compact row's label is its tooltip", "[navigation-view][tooltip]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_pane_display_mode(NavigationShellPaneDisplayMode::LeftCompact);
    view.set_items(icon_items({"Home", "Mail"}));
    lay_out(view, m, theme, {800.0f, 500.0f});

    const Point over = view.item_bounds(1)->center();
    view.handle_pointer({over, PointerButton::None, PointerAction::Move});
    view.tick(0ms);
    view.tick(1000ms);

    std::vector<Element*> owners;
    ui::testing::ElementAccess::collect_live(view, owners);
    ToolTipControl* open = nullptr;
    for (Element* owner : owners)
    {
        if (auto* tip = dynamic_cast<ToolTipControl*>(owner); tip && ui::testing::popup_open(*tip))
        {
            open = tip;
        }
    }
    REQUIRE(open != nullptr);
    const auto* text = dynamic_cast<const TextControl*>(open->content());
    REQUIRE(text != nullptr);
    CHECK(text->text() == "Mail");

    // An open pane shows the labels themselves, so no tooltip.
    view.set_pane_open(true);
    lay_out(view, m, theme, {800.0f, 500.0f});
    view.handle_pointer({view.item_bounds(0)->center(), PointerButton::None, PointerAction::Move});
    view.tick(1000ms);
    owners.clear();
    ui::testing::ElementAccess::collect_live(view, owners);
    for (Element* owner : owners)
    {
        if (auto* tip = dynamic_cast<ToolTipControl*>(owner); tip && ui::testing::popup_open(*tip))
        {
            CHECK(dynamic_cast<const TextControl*>(tip->content())->text() != "Home");
        }
    }
}

TEST_CASE("pane_title rides in the toggle button while the pane is open", "[navigation-view][layout]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_pane_display_mode(NavigationShellPaneDisplayMode::Left);
    view.set_back_button_visible(AutoVisibility::Collapsed);
    view.set_items(icon_items({"One"}));
    view.set_pane_title("Pane Title");
    lay_out(view, m, theme, {900.0f, 500.0f});

    // The toggle button: the full pane width while open, less the 4,2 margins.
    CHECK_THAT(view.toggle_button()->bounds().width, WithinAbs(320.0f - 8.0f, 1e-3));
    TestRenderer r;
    view.render(r, theme);
    const DrawTextRun* title = text_of(r, "Pane Title");
    REQUIRE(title != nullptr);
    CHECK_THAT(title->bounds.x - view.toggle_button()->bounds().x, WithinAbs(40.0f + 4.0f, 1e-3));

    CHECK(view.toggle_button()->automation_name() == "Close Navigation");
    view.set_pane_open(false);
    CHECK(view.toggle_button()->automation_name() == "Open Navigation");
}

TEST_CASE("The AutoSuggestBox sits under the toggle button, and a search button stands in while closed compact",
          "[navigation-view][layout]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_pane_display_mode(NavigationShellPaneDisplayMode::LeftCompact);
    view.set_back_button_visible(AutoVisibility::Collapsed);
    view.set_items(icon_items({"One"}));
    ButtonControl* box = view.set_auto_suggest_box(std::make_unique<ButtonControl>("Search box"));
    lay_out(view, m, theme, {800.0f, 500.0f});
    const Point o{view.bounds().x, view.bounds().y};

    // Closed compact: the box is gone, the 40x36 search button in its 40px row.
    CHECK(box->bounds().width == 0.0f);
    const Rect search = view.search_button()->bounds();
    CHECK_THAT(search.x - o.x, WithinAbs(4.0f, 1e-3));
    CHECK_THAT(search.y - o.y, WithinAbs(4.0f + 40.0f + 2.0f, 1e-3));
    // The items follow the row and its 8px margin.
    CHECK_THAT(view.item_bounds(0)->y - o.y, WithinAbs(4.0f + 40.0f + 40.0f + 8.0f + 2.0f, 1e-3));

    // The search button opens the pane; the box then takes the row at its 16 DIP margin.
    click(view, search.center());
    REQUIRE(view.is_pane_open());
    lay_out(view, m, theme, {800.0f, 500.0f});
    CHECK_THAT(box->bounds().x - o.x, WithinAbs(16.0f, 1e-3));
    CHECK_THAT(box->bounds().width, WithinAbs(320.0f - 32.0f, 1e-3));
    CHECK(view.search_button()->bounds().width == 0.0f);
}

TEST_CASE("Opening an overlaying pane takes focus into it, and closing it gives focus back", "[navigation-view][focus]")
{
    HeadlessTextMeasurer    m;
    Window                  window;
    auto                    nav  = std::make_unique<NavigationShellControl>();
    NavigationShellControl* view = nav.get();
    view->set_pane_display_mode(NavigationShellPaneDisplayMode::LeftMinimal);
    view->set_items(icon_items({"One", "Two"}));
    ButtonControl* page = view->set_content(std::make_unique<ButtonControl>("Page"));
    window.set_content(std::move(nav));
    window.layout({700.0f, 400.0f}, m);

    window.focus_element(page);
    REQUIRE(window.focused() == page);
    view->set_pane_open(true);
    window.layout({700.0f, 400.0f}, m);
    CHECK(window.focused() == view); // the items are the pane's first stop

    ui::testing::seams(window).handle_key({Key::Tab, KeyAction::Down});
    CHECK(window.focused() == view);
    ui::testing::seams(window).handle_key({Key::Escape, KeyAction::Down});
    CHECK_FALSE(view->is_pane_open());
    window.layout({700.0f, 400.0f}, m);
    CHECK(window.focused() == page);
}

// ---- Top ----

TEST_CASE("Top lays the items out across a 48px bar, the back button first and Settings last",
          "[navigation-view][top][layout]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_pane_display_mode(NavigationShellPaneDisplayMode::Top);
    std::vector<NavEntry> items;
    items.push_back(NavEntry{"Menu Item1"});
    items.push_back(NavEntry{"Menu Item2"});
    view.set_items(std::move(items));
    lay_out(view, m, theme, {900.0f, 500.0f});
    const Point o{view.bounds().x, view.bounds().y};

    CHECK_FALSE(view.is_pane_open());
    CHECK(view.pane_bounds().width == 0.0f);
    CHECK_THAT(view.content_bounds().y - o.y, WithinAbs(48.0f, 1e-3));
    CHECK(view.toggle_button()->bounds().width == 0.0f);

    // The bar's 4,0 margin and the back button's 40px column, then the items: each 4 + 12 +
    // label + 12 + 4.
    const TextStyle label  = theme.text_style;
    const float     w1     = std::ceil(m.measure("Menu Item1", label).width) + 32.0f;
    const Rect      first  = *view.item_bounds(0);
    const Rect      second = *view.item_bounds(1);
    CHECK_THAT(first.x - o.x, WithinAbs(4.0f + 40.0f, 1e-3));
    CHECK_THAT(first.width, WithinAbs(w1, 1e-3));
    CHECK_THAT(second.x, WithinAbs(first.right(), 1e-3));
    CHECK_THAT(first.height, WithinAbs(48.0f, 1e-3));

    // Settings: a 40px icon at the right end.
    const Rect settings = *view.item_bounds(NavigationShellControl::Settings);
    CHECK_THAT(settings.right(), WithinAbs(view.bounds().right() - 4.0f, 1e-3));
    CHECK_THAT(settings.width, WithinAbs(40.0f, 1e-3));

    // The indicator is a 16x3 bar under the selected item, 4 above its pill's bottom.
    TestRenderer r;
    view.render(r, theme);
    const FillRoundedRect* bar = nullptr;
    for (const auto& c : r.commands)
    {
        if (const auto* f = std::get_if<FillRoundedRect>(&c); f && f->color == theme.accent_button_normal.background)
        {
            bar = f;
        }
    }
    REQUIRE(bar != nullptr);
    CHECK_THAT(bar->bounds.width, WithinAbs(16.0f, 1e-3));
    CHECK_THAT(bar->bounds.height, WithinAbs(3.0f, 1e-3));
    CHECK_THAT(bar->bounds.center().x, WithinAbs(first.center().x, 1e-3));
    CHECK_THAT(bar->bounds.bottom() - o.y, WithinAbs(2.0f + 44.0f - 4.0f, 1e-3));

    ui::testing::set_focused(view, true);
    view.handle_key(key_down(Key::ArrowRight));
    view.handle_key(key_down(Key::Enter));
    CHECK(view.selected_index() == 1);
    view.handle_key(key_down(Key::ArrowRight));
    view.handle_key(key_down(Key::Enter));
    CHECK(view.selected_index() == NavigationShellControl::Settings);
}

TEST_CASE("Top moves what does not fit behind the overflow button, but never the selected item",
          "[navigation-view][top][flyout]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_pane_display_mode(NavigationShellPaneDisplayMode::Top);
    view.set_back_button_visible(AutoVisibility::Collapsed);
    view.set_settings_visible(false);
    std::vector<NavEntry> items;
    items.reserve(8);
    for (int i = 0; i < 8; ++i)
    {
        items.push_back(NavEntry{"Category " + std::to_string(i)});
    }
    view.set_items(std::move(items));

    lay_out(view, m, theme, {2000.0f, 400.0f});
    CHECK_FALSE(view.overflow_button_bounds().has_value());
    CHECK_FALSE(view.in_overflow(7));

    lay_out(view, m, theme, {500.0f, 400.0f});
    REQUIRE(view.overflow_button_bounds().has_value());
    CHECK_FALSE(view.in_overflow(0));
    CHECK(view.in_overflow(7));
    CHECK_THAT(view.overflow_button_bounds()->width, WithinAbs(40.0f, 1e-3));

    click(view, view.overflow_button_bounds()->center());
    REQUIRE(view.flyout_open());
    Element* list = ui::testing::popup_content(view);
    REQUIRE(ui::testing::ElementAccess::place_popup(view, *list, Size{500.0f, 400.0f}, ui::MeasureContext{m, theme}));
    CHECK_THAT(list->bounds().right(), WithinAbs(view.overflow_button_bounds()->right(), 1e-3));
    CHECK_THAT(list->bounds().y, WithinAbs(view.overflow_button_bounds()->bottom(), 1e-3));

    // Picking an overflow item brings it into the bar at the expense of the items before it.
    const float last_row_y = list->bounds().bottom() - 1.0f - 2.0f - 18.0f;
    list->handle_pointer(press(Point{list->bounds().center().x, last_row_y}));
    CHECK(view.selected_index() == 7);
    CHECK_FALSE(view.flyout_open());
    lay_out(view, m, theme, {500.0f, 400.0f});
    CHECK_FALSE(view.in_overflow(7));
    CHECK(view.item_bounds(7).has_value());
}

TEST_CASE("Top opens a group's children in a flyout below it", "[navigation-view][top][flyout]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_pane_display_mode(NavigationShellPaneDisplayMode::Top);
    std::vector<NavEntry> items;
    items.push_back(NavEntry{"Home"});
    NavEntry docs{"Document options"};
    docs.selects_on_invoke = false;
    docs.children.push_back(NavEntry{"Create new"});
    docs.children.push_back(NavEntry{"Upload file"});
    items.push_back(std::move(docs));
    view.set_items(std::move(items));
    lay_out(view, m, theme, {900.0f, 400.0f});

    click(view, view.item_bounds(1)->center());
    CHECK(view.selected_index() == 0); // selects_on_invoke is false
    REQUIRE(view.flyout_open());
    CHECK(ui::testing::ElementAccess::popup_request(view)->placement == PopupPlacement::Below);
    Element* list = ui::testing::popup_content(view);
    REQUIRE(ui::testing::ElementAccess::place_popup(view, *list, Size{900.0f, 400.0f}, ui::MeasureContext{m, theme}));
    CHECK_THAT(list->bounds().x, WithinAbs(view.item_bounds(1)->x, 1e-3));
    // Overflow menu rows: 36 tall.
    list->handle_pointer(press(Point{list->bounds().center().x, list->bounds().y + 1.0f + 2.0f + 36.0f + 18.0f}));
    CHECK(view.selected_index() == 3);
    CHECK_FALSE(view.flyout_open());

    // Clicking the group again while open closes it (the menu's light dismiss).
    click(view, view.item_bounds(1)->center());
    REQUIRE(view.flyout_open());
    click(view, view.item_bounds(1)->center());
    CHECK_FALSE(view.flyout_open());
}

TEST_CASE("Switching pane_display_mode closes and opens the pane", "[navigation-view][layout]")
{
    HeadlessTextMeasurer   m;
    const Theme            theme = Theme::light();
    NavigationShellControl view;
    view.set_items(icon_items({"One"}));
    view.set_pane_display_mode(NavigationShellPaneDisplayMode::Left);
    lay_out(view, m, theme, {900.0f, 400.0f});
    REQUIRE(view.is_pane_open());

    view.set_pane_display_mode(NavigationShellPaneDisplayMode::LeftMinimal);
    CHECK_FALSE(view.is_pane_open());
    lay_out(view, m, theme, {900.0f, 400.0f});
    CHECK(view.display_mode() == NavigationShellDisplayMode::Minimal);

    view.set_pane_display_mode(NavigationShellPaneDisplayMode::Left);
    CHECK(view.is_pane_open()); // LeftMinimal -> Left opens it
    view.set_pane_display_mode(NavigationShellPaneDisplayMode::Top);
    CHECK_FALSE(view.is_pane_open());
    lay_out(view, m, theme, {900.0f, 400.0f});
    CHECK(view.display_mode() == NavigationShellDisplayMode::Minimal);
    view.set_pane_open(true);
    CHECK_FALSE(view.is_pane_open()); // the top bar has no pane
}

TEST_CASE("Fluent 2's content area is the layer fill inside a card-stroke edge, rounded at the top-left",
          "[navigation-view][render]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent2}, OsThemeSnapshot{});
    REQUIRE(theme.surfaces.nav_content.has_value());
    CHECK(*theme.surfaces.nav_content == Color::from_argb(0x80FFFFFF)); // the default layer fill, Light
    NavigationShellControl view;
    plain(view);
    view.set_items(icon_items({"One"}));
    lay_out(view, m, theme, {900.0f, 400.0f});
    TestRenderer r;
    view.render(r, theme);
    const auto* fill = std::get_if<FillRoundedRect>(&r.commands[1]); // after the content clip
    REQUIRE(fill != nullptr);
    CHECK(fill->color == *theme.surfaces.nav_content);
    CHECK(fill->radius == 8.0f);
    CHECK_THAT(fill->bounds.x, WithinAbs(view.content_bounds().x, 1e-3));

    const Theme fluent1 = resolve_theme(ThemeRequest{.variant = ThemeVariant::Fluent}, OsThemeSnapshot{});
    CHECK_FALSE(fluent1.surfaces.nav_content.has_value());
}

TEST_CASE("NavigationShell: a press on its page focuses the control there, one on the menu the view",
          "[navigation_view]")
{
    HeadlessUI ui;
    auto       nav = std::make_unique<NavigationShellControl>();
    nav->set_pane_display_mode(NavigationShellPaneDisplayMode::Left);
    nav->set_settings_visible(false);
    std::vector<NavEntry> items;
    NavEntry              entry{};
    entry.text = "Page";
    items.push_back(std::move(entry));
    nav->set_items(std::move(items));
    auto            box = std::make_unique<TextBoxControl>();
    TextBoxControl* raw = box.get();
    nav->set_content(std::move(box));
    NavigationShellControl* view = nav.get();
    ui.window().set_content(std::move(nav));
    ui.layout({800.0f, 600.0f});
    ui.window().focus_content();
    REQUIRE(ui.window().focused() != raw);

    // The view is before its page in the ring and covers it, but only the menu is its stop.
    const Rect  b = raw->bounds();
    const Point on_box{b.x + 10.0f, b.y + b.height / 2.0f};
    CHECK_FALSE(view->takes_focus_at(on_box));
    ui::testing::seams(ui.window()).handle_pointer({on_box, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({on_box, PointerButton::Left, PointerAction::Up});
    CHECK(ui.window().focused() == raw);

    bool menu_takes_focus = false;
    for (float y = view->bounds().y; y < view->bounds().bottom(); y += 4.0f)
    {
        menu_takes_focus = menu_takes_focus || view->takes_focus_at(Point{view->bounds().x + 20.0f, y});
    }
    CHECK(menu_takes_focus);
}

TEST_CASE("set_selected_index scrolls the pane to the item, already selected or not", "[navigation-view]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    NavigationShellControl view;
    plain(view);
    std::vector<NavEntry> items;
    for (int i = 0; i < 20; ++i)
    {
        items.push_back(NavEntry{"Item " + std::to_string(i)});
    }
    view.set_items(std::move(items));
    lay_out(view, m, theme, {400.0f, 150.0f});
    REQUIRE_THAT(view.pane_scroll_offset(), WithinAbs(0.0f, 1e-3));

    view.set_selected_index(19);
    lay_out(view, m, theme, {400.0f, 150.0f});
    const float at_end = view.pane_scroll_offset();
    CHECK(at_end > 0.0f);

    // Scrolled away by hand, then asked for the same item again.
    view.handle_wheel({{40.0f, 40.0f}, PointerButton::None, PointerAction::Wheel, -1000.0f});
    REQUIRE_THAT(view.pane_scroll_offset(), WithinAbs(0.0f, 1e-3));
    view.set_selected_index(19);
    lay_out(view, m, theme, {400.0f, 150.0f});
    CHECK_THAT(view.pane_scroll_offset(), WithinAbs(at_end, 1e-3));
}

TEST_CASE("Under Aero an overflowing pane gives its bar a gutter beside the rows", "[navigation-view][render][aero]")
{
    HeadlessTextMeasurer m;
    const Theme          aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});

    NavigationShellControl view;
    plain(view);
    std::vector<NavEntry> items;
    for (int i = 0; i < 20; ++i)
    {
        items.push_back(NavEntry{"Item " + std::to_string(i)});
    }
    view.set_items(std::move(items));
    lay_out(view, m, aero, {400.0f, 150.0f});

    TestRenderer r;
    view.render(r, aero);
    std::optional<Rect> track;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRectGradient>(&command);
            fill != nullptr && !fill->brush.stops.empty() && fill->brush.stops.front().color == Color::from_argb(0xFFE1E1E1))
        {
            track = fill->bounds;
        }
    }
    REQUIRE(track.has_value());
    CHECK(track->width == 17.0f);

    // A click in the gutter is the bar's: it pages the list and selects nothing.
    const std::optional<std::size_t> before = view.selected_index();
    view.handle_pointer({Point{track->center().x, track->bottom() - 20.0f}, PointerButton::Left, PointerAction::Down});
    view.handle_pointer({Point{track->center().x, track->bottom() - 20.0f}, PointerButton::Left, PointerAction::Up});
    CHECK(view.selected_index() == before);
    CHECK(view.pane_scroll_offset() > 0.0f);
}
