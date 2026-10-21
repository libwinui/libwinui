// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/navigation/Tabs.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <memory>
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

void lay_out(Element& view, const TextMeasurer& m, const Theme& theme, Size size)
{
    (void)view.measure(size, m, theme);
    view.arrange({0, 0, size.width, size.height});
}
} // namespace

TEST_CASE("Adding a tab selects it automatically if none was selected", "[tabs]")
{
    TabsControl view;
    REQUIRE(view.tab_count() == 0);

    view.add_tab("One", std::make_unique<TextControl>("content"));
    REQUIRE(view.tab_count() == 1);
    REQUIRE(view.selected_index() == 0);

    view.add_tab("Two", std::make_unique<TextControl>("content"));
    REQUIRE(view.tab_count() == 2);
    REQUIRE(view.selected_index() == 0); // adding a second tab doesn't move the selection
}

TEST_CASE("Only the selected tab's content is measured", "[tabs]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TextStyle small_style{}; // font_size 14 default
    TextStyle large_style{};
    large_style.font_size = 40.0f;

    auto small = std::make_unique<TextControl>("A");
    small->set_text_style(small_style);
    auto large = std::make_unique<TextControl>("A");
    large->set_text_style(large_style);

    TabsControl view;
    view.add_tab("Small", std::move(small));
    view.add_tab("Large", std::move(large));

    // item height = max(the 32 minimum, line height 18.9 + 2 * 3 padding) = 32, and the
    // row adds the 8px top padding.
    const float item_height = std::max(32.0f, 14.0f * HeadlessTextMeasurer::LineRatio + 6.0f);
    const float row_height  = 8.0f + item_height;

    view.set_selected_index(0);
    Size desired = view.measure({300.0f, Unbounded}, m, theme);
    REQUIRE_THAT(desired.height, WithinAbs(row_height + 14.0f * HeadlessTextMeasurer::LineRatio, 1e-2));

    view.set_selected_index(1);
    desired = view.measure({300.0f, Unbounded}, m, theme);
    REQUIRE_THAT(desired.height, WithinAbs(row_height + 40.0f * HeadlessTextMeasurer::LineRatio, 1e-2));
}

TEST_CASE("Clicking a header selects that tab", "[tabs][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    // Single-letter headers land on the 100px minimum tab width: "A" spans [0, 100) and
    // "B" [100, 200). Pills sit 8px below the strip's top.
    TabsControl view;
    view.set_add_tab_button_visible(true);
    view.add_tab("A", std::make_unique<TextControl>("a"));
    view.add_tab("B", std::make_unique<TextControl>("b"));
    (void)view.measure({300.0f, 200.0f}, m, theme);
    view.arrange({0, 0, 300, 200});

    const Point p{150.0f, 20.0f}; // inside header "B"'s [100, 200) range, below the top gap
    view.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    REQUIRE(view.selected_index() == 1);
}

TEST_CASE("set_selected_index clamps and fires on_selection_changed only on an actual change", "[tabs]")
{
    TabsControl view;
    view.add_tab("One", std::make_unique<TextControl>("a"));
    view.add_tab("Two", std::make_unique<TextControl>("b"));
    view.add_tab("Three", std::make_unique<TextControl>("c"));

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
    REQUIRE(last == "Three"); // a tab without a key is keyed by its header

    view.set_selected_index(1000); // clamped to the last tab
    REQUIRE(view.selected_index() == 2);
    REQUIRE(calls == 1); // already at the last tab: no-op
}

TEST_CASE("Arrow keys move the focus between tabs, clamped, and Enter or Space selects", "[tabs][input]")
{
    TabsControl view;
    view.add_tab("One", std::make_unique<TextControl>("a"));
    view.add_tab("Two", std::make_unique<TextControl>("b"));
    view.add_tab("Three", std::make_unique<TextControl>("c"));

    view.handle_key(key_down(Key::ArrowRight)); // not focused: ignored
    view.handle_key(key_down(Key::Enter));
    REQUIRE(view.selected_index() == 0);

    // Selection does not follow focus: the arrows move the focus only.
    ui::testing::set_focused(view, true);
    view.handle_key(key_down(Key::ArrowLeft)); // already at 0: clamps
    view.handle_key(key_down(Key::ArrowRight));
    REQUIRE(view.selected_index() == 0);
    view.handle_key(key_down(Key::Enter));
    REQUIRE(view.selected_index() == 1);
    view.handle_key(key_down(Key::ArrowRight));
    view.handle_key(key_down(Key::ArrowRight)); // clamped at the end
    view.handle_key(key_down(Key::Space));
    REQUIRE(view.selected_index() == 2);
}

TEST_CASE("collect_focusables yields Tabs itself plus only the selected tab's descendants", "[tabs][focus]")
{
    TabsControl view;
    view.set_add_tab_button_visible(false);
    ButtonControl* b0 = view.add_tab("One", std::make_unique<ButtonControl>("B0"));
    ButtonControl* b1 = view.add_tab("Two", std::make_unique<ButtonControl>("B1"));

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

TEST_CASE("The tabs are a tab stop whenever there is one, and the content follows", "[tabs][focus]")
{
    TabsControl view;
    view.set_add_tab_button_visible(false);
    REQUIRE_FALSE(view.focusable());
    ButtonControl* only = view.add_tab("Solo", std::make_unique<ButtonControl>("Only"));
    REQUIRE(view.focusable());

    std::vector<Element*> out;
    view.collect_focusables(out);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0] == static_cast<Element*>(&view));
    REQUIRE(out[1] == static_cast<Element*>(only));

    view.set_enabled(false);
    REQUIRE_FALSE(view.focusable());
}

// ---- Tab-strip chrome ----

TEST_CASE("Tab headers render at 12pt and go SemiBold once selected", "[tabs][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TabsControl view;
    view.add_tab("First", std::make_unique<TextControl>("a"));
    view.add_tab("Second", std::make_unique<TextControl>("b"));
    (void)view.measure({400.0f, 200.0f}, m, theme);
    view.arrange({0, 0, 400, 200});

    TestRenderer r;
    view.render(r, theme);

    std::vector<DrawTextRun> headers;
    for (const auto& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c); t && (t->text == "First" || t->text == "Second"))
        {
            headers.push_back(*t);
        }
    }
    REQUIRE(headers.size() == 2);
    // The selected tab draws last, over its neighbors.
    const DrawTextRun& second = headers[0];
    const DrawTextRun& first  = headers[1];
    REQUIRE(first.text == "First");
    REQUIRE_THAT(first.style.font_size, WithinAbs(12.0f, 1e-3)); // the header's font size
    REQUIRE(first.style.weight == FontWeight::SemiBold);         // selected
    REQUIRE(second.style.weight == FontWeight::Regular);
    REQUIRE(first.color == theme.palette.text_fill_primary);
    REQUIRE(second.color == theme.palette.text_fill_secondary);
}

TEST_CASE("Equal tabs share the strip, clamped to the minimum and maximum tab width", "[tabs][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TabsControl view;
    view.set_add_tab_button_visible(true);
    view.add_tab("A", std::make_unique<TextControl>("a"));
    lay_out(view, m, theme, {800.0f, 200.0f});
    // One tab in a wide strip stops at the 240 maximum.
    CHECK_THAT(view.tab_bounds(0)->width, WithinAbs(240.0f, 1e-3));

    // The tab width: what the header column (2 minimum) and the add button (3 + 32) leave, less
    // the list's 4px ends and 1px padding, shared out.
    view.add_tab("B", std::make_unique<TextControl>("b"));
    lay_out(view, m, theme, {400.0f, 200.0f});
    const float room = view.bounds().width - 2.0f - 35.0f;
    CHECK_THAT(view.tab_bounds(0)->width, WithinAbs((room - 9.0f) / 2.0f, 1e-3));
    CHECK_THAT(view.tab_bounds(0)->x - view.bounds().x, WithinAbs(2.0f + 1.0f + 4.0f, 1e-3));
    CHECK_THAT(view.tab_bounds(1)->x, WithinAbs(view.tab_bounds(0)->right(), 1e-3));
    view.handle_pointer({view.tab_bounds(1)->center(), PointerButton::Left, PointerAction::Down});
    REQUIRE(view.selected_index() == 1);

    // Many tabs bottom out at 100 and scroll.
    for (int i = 0; i < 10; ++i)
    {
        view.add_tab("More", std::make_unique<TextControl>("m"));
    }
    lay_out(view, m, theme, {400.0f, 200.0f});
    CHECK_THAT(view.tab_bounds(0)->width, WithinAbs(100.0f, 1e-3));
    CHECK(view.scroll_decrease_bounds().has_value());
    CHECK(view.scroll_increase_bounds().has_value());
}

TEST_CASE("The strip rule runs under every tab and the separator skips the selection", "[tabs][render]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TabsControl view;
    view.add_tab("One", std::make_unique<TextControl>("a"));
    view.add_tab("Two", std::make_unique<TextControl>("b"));
    view.add_tab("Three", std::make_unique<TextControl>("c"));
    (void)view.measure({400.0f, 200.0f}, m, theme);
    view.arrange({0, 0, 400, 200});

    // Matched on geometry, not color: the divider and card strokes are
    // both 0x0F000000 in Fluent Light. A separator is the 1px-wide one.
    const auto separators = [&](const TabsControl& v)
    {
        TestRenderer r;
        v.render(r, theme);
        std::size_t n = 0;
        for (const auto& c : r.commands)
        {
            if (const auto* f = std::get_if<FillRoundedRect>(&c);
                f && f->color == theme.colors().divider && f->bounds.width == 1.0f)
            {
                ++n;
            }
        }
        return n;
    };

    // Tab 0 is selected: its own separator is hidden, tabs 1 and 2 keep theirs.
    REQUIRE(separators(view) == 2);

    // Tab 1 selected: its own separator goes, and so does tab 0's, the tab immediately left of
    // the selection.
    view.set_selected_index(1);
    REQUIRE(separators(view) == 1);

    TestRenderer r;
    view.render(r, theme);
    // The rule along the strip's bottom: card-stroke
    // hairlines on either side of the selected tab and its 4px flares, drawn first.
    const auto& left  = std::get<FillRoundedRect>(r.commands[0]);
    const auto& right = std::get<FillRoundedRect>(r.commands[1]);
    const Rect  tab   = *view.tab_bounds(1);
    REQUIRE(left.color == theme.palette.card_stroke_default);
    REQUIRE_THAT(left.bounds.height, WithinAbs(1.0f, 1e-3));
    CHECK_THAT(left.bounds.x, WithinAbs(view.bounds().x, 1e-3));
    CHECK_THAT(left.bounds.right(), WithinAbs(tab.x - 4.0f, 1e-3));
    CHECK_THAT(right.bounds.x, WithinAbs(tab.right() + 4.0f, 1e-3));
    CHECK_THAT(right.bounds.right(), WithinAbs(view.bounds().right(), 1e-3));
    CHECK_THAT(left.bounds.bottom(), WithinAbs(tab.bottom(), 1e-3));

    // The selected tab is one path: flaring 4px past its slot, filled with
    // the tertiary solid background, outlined with the card stroke, open at the bottom.
    const FillPolygon*    shape   = nullptr;
    const StrokePolyline* outline = nullptr;
    for (const auto& c : r.commands)
    {
        if (const auto* p = std::get_if<FillPolygon>(&c))
        {
            shape = p;
        }
        if (const auto* p = std::get_if<StrokePolyline>(&c))
        {
            outline = p;
        }
    }
    REQUIRE(shape != nullptr);
    REQUIRE(outline != nullptr);
    CHECK(shape->fill == theme.palette.solid_background_tertiary);
    CHECK(outline->color == theme.palette.card_stroke_default);
    CHECK(outline->points == shape->points);
    CHECK_FALSE(outline->closed);
    CHECK_THAT(shape->points.front().x, WithinAbs(tab.x - 4.0f, 1e-3));
    CHECK_THAT(shape->points.front().y, WithinAbs(tab.bottom(), 1e-3));
    CHECK_THAT(shape->points.back().x, WithinAbs(tab.right() + 4.0f, 1e-3));
}

// ---- Tabs as an app drives it: closing, adding, the width modes, scrolling, keyboard
// shortcuts, reordering and the strip's own content ----

namespace
{
void click(Element& e, Point p, PointerButton button = PointerButton::Left)
{
    e.handle_pointer({p, button, PointerAction::Down});
    e.handle_pointer({p, button, PointerAction::Up});
}

Tab document(const char* header, bool closable = true)
{
    return Tab{.header   = header,
               .icon     = IconGlyph{0xE8A5},
               .closable = closable,
               .content  = std::make_unique<TextControl>(header)};
}
} // namespace

TEST_CASE("Tabs starts with its documented defaults", "[tabs]")
{
    TabsControl view;
    CHECK(view.tab_width_mode() == TabsWidthMode::Equal);
    CHECK(view.close_button_overlay_mode() == TabsCloseButtonOverlayMode::Auto);
    CHECK_FALSE(view.is_add_tab_button_visible()); // no add handler yet
    CHECK(view.can_reorder_tabs());
    CHECK(view.strip_background().empty());
}

TEST_CASE("The add button shows once there is an add handler, unless set explicitly", "[tabs]")
{
    TabsControl view;
    view.on_add_tab_button_click([] {});
    CHECK(view.is_add_tab_button_visible());
    view.set_add_tab_button_visible(false);
    CHECK_FALSE(view.is_add_tab_button_visible());
    view.set_add_tab_button_visible(std::nullopt);
    CHECK(view.is_add_tab_button_visible());

    TabsControl shown;
    shown.set_add_tab_button_visible(true);
    CHECK(shown.is_add_tab_button_visible());
}

TEST_CASE("Without a close handler a close button removes its tab and selects the neighbor", "[tabs][input]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    TabsControl          view;
    for (const char* h : {"Document 0", "Document 1", "Document 2"})
    {
        view.add_tab(document(h));
    }
    std::vector<std::string> selected;
    view.on_selection_changed([&](const std::string& key) { selected.push_back(key); });
    view.set_selected_index(1);
    lay_out(view, m, theme, {800.0f, 300.0f});

    click(view, view.close_button_bounds(1)->center());
    CHECK(view.tab_count() == 2);
    CHECK(view.header(1) == "Document 2");
    CHECK(view.selected_key() == "Document 2");
    CHECK(selected.back() == "Document 2");

    // With a handler, the handler decides: this one keeps every tab.
    std::vector<std::string> requests;
    view.on_tab_close_requested([&](const std::string& key) { requests.push_back(key); });
    lay_out(view, m, theme, {800.0f, 300.0f});
    click(view, view.close_button_bounds(0)->center());
    CHECK(requests == std::vector<std::string>{"Document 0"});
    CHECK(view.tab_count() == 2);
}

TEST_CASE("The close button asks to close; the app removes, and the selection moves on", "[tabs][input]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    TabsControl          view;
    for (const char* h : {"Document 0", "Document 1", "Document 2"})
    {
        view.add_tab(document(h));
    }
    std::vector<std::string> requests;
    view.on_tab_close_requested(
        [&](const std::string& key)
        {
            requests.push_back(key);
            view.remove_tab(key);
        });
    view.set_selected_index(1);
    lay_out(view, m, theme, {800.0f, 300.0f});

    // With a close button: the 32x24 button 1 + 4 in from the tab's right.
    const Rect tab   = *view.tab_bounds(1);
    const Rect close = *view.close_button_bounds(1);
    CHECK_THAT(close.right(), WithinAbs(tab.right() - 1.0f - 4.0f, 1e-3));
    CHECK_THAT(close.width, WithinAbs(32.0f, 1e-3));
    CHECK_THAT(close.height, WithinAbs(24.0f, 1e-3));

    // Removing the selected tab selects the one that takes its index.
    click(view, close.center());
    REQUIRE(requests.size() == 1);
    CHECK(requests[0] == "Document 1");
    CHECK(view.tab_count() == 2);
    CHECK(view.selected_index() == 1);
    CHECK(view.header(1) == "Document 2");

    // A middle click closes too; the last tab's removal selects the new last.
    lay_out(view, m, theme, {800.0f, 300.0f});
    click(view, view.tab_bounds(1)->center(), PointerButton::Middle);
    CHECK(view.tab_count() == 1);
    CHECK(view.selected_index() == 0);

    // A tab that is not closable has no button and ignores the requests.
    view.add_tab(document("Pinned", false));
    lay_out(view, m, theme, {800.0f, 300.0f});
    CHECK_FALSE(view.close_button_bounds(1).has_value());
    click(view, view.tab_bounds(1)->center(), PointerButton::Middle);
    CHECK(view.tab_count() == 2);
}

TEST_CASE("OnPointerOver shows the close button on the selected and the hovered tab only", "[tabs][render]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    TabsControl          view;
    for (const char* h : {"Home", "Tab 2 Has Longer Text", "Third Tab"})
    {
        view.add_tab(document(h));
    }
    view.set_close_button_overlay_mode(TabsCloseButtonOverlayMode::OnPointerOver);
    lay_out(view, m, theme, {800.0f, 300.0f});
    CHECK(view.close_button_bounds(0).has_value());
    CHECK_FALSE(view.close_button_bounds(1).has_value());
    view.handle_pointer({view.tab_bounds(2)->center(), PointerButton::None, PointerAction::Move});
    CHECK(view.close_button_bounds(2).has_value());
    CHECK_FALSE(view.close_button_bounds(1).has_value());

    view.set_close_button_overlay_mode(TabsCloseButtonOverlayMode::Always);
    CHECK(view.close_button_bounds(1).has_value());
}

TEST_CASE("The add button sits after the tabs and raises on_add_tab_button_click", "[tabs][input]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    TabsControl          view;
    view.add_tab(document("Document 0"));
    int adds = 0;
    view.on_add_tab_button_click(
        [&]
        {
            ++adds;
            view.add_tab(document("Document 1"));
        });
    lay_out(view, m, theme, {800.0f, 300.0f});

    // The add button's 3,0,0,3 padding: 3 after the list's end, 3 above the strip's bottom.
    const Rect add = view.add_button()->bounds();
    CHECK_THAT(add.width, WithinAbs(32.0f, 1e-3));
    CHECK_THAT(add.height, WithinAbs(24.0f, 1e-3));
    CHECK_THAT(add.x, WithinAbs(view.tab_bounds(0)->right() + 4.0f + 3.0f, 1e-3));
    CHECK_THAT(add.bottom(), WithinAbs(view.strip_bounds().bottom() - 3.0f, 1e-3));

    click(view, add.center());
    CHECK(adds == 1);
    CHECK(view.tab_count() == 2);
    CHECK(view.selected_index() == 0); // adding does not move the selection

    view.set_add_tab_button_visible(false);
    lay_out(view, m, theme, {800.0f, 300.0f});
    CHECK(view.add_button()->bounds().width == 0.0f);
}

TEST_CASE("SizeToContent sizes each tab to its content, Compact shrinks the unselected ones to their icon",
          "[tabs][layout]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    TabsControl          view;
    view.add_tab(document("Home", false));
    view.add_tab(document("Tab 2 Has Longer Text", false));
    view.set_tab_width_mode(TabsWidthMode::SizeToContent);
    lay_out(view, m, theme, {900.0f, 300.0f});

    TextStyle label = theme.text_style;
    label.font_size = 12.0f;
    TextStyle bold  = label;
    bold.weight     = FontWeight::SemiBold;
    // The border (1 + 1), padding without a close button (8 + 8), the icon and its 10px gap.
    const float home = 2.0f + 16.0f + 16.0f + 10.0f + std::ceil(m.measure("Home", bold).width);
    const float tab2 = 2.0f + 16.0f + 16.0f + 10.0f + std::ceil(m.measure("Tab 2 Has Longer Text", label).width);
    CHECK_THAT(view.tab_bounds(0)->width, WithinAbs(home, 1e-3));
    CHECK_THAT(view.tab_bounds(1)->width, WithinAbs(tab2, 1e-3));

    // Compact: the unselected tab is its 16px icon column alone.
    view.set_tab_width_mode(TabsWidthMode::Compact);
    lay_out(view, m, theme, {900.0f, 300.0f});
    CHECK_THAT(view.tab_bounds(1)->width, WithinAbs(2.0f + 16.0f + 16.0f, 1e-3));
    CHECK_THAT(view.tab_bounds(0)->width, WithinAbs(home, 1e-3));
    TestRenderer r;
    view.render(r, theme);
    for (const auto& c : r.commands)
    {
        if (const auto* t = std::get_if<DrawTextRun>(&c))
        {
            CHECK(t->text != "Tab 2 Has Longer Text");
        }
    }
}

TEST_CASE("Overflowing tabs scroll with the strip's buttons and the wheel, and selection brings a tab into view",
          "[tabs][input]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    TabsControl          view;
    for (int i = 0; i < 12; ++i)
    {
        view.add_tab(document("Document"));
    }
    lay_out(view, m, theme, {600.0f, 300.0f});
    REQUIRE(view.scroll_increase_bounds().has_value());
    CHECK(view.scroll_offset() == 0.0f);

    // a fixed distance per click.
    click(view, view.scroll_increase_bounds()->center());
    CHECK_THAT(view.scroll_offset(), WithinAbs(50.0f, 1e-3));
    click(view, view.scroll_decrease_bounds()->center());
    CHECK(view.scroll_offset() == 0.0f);

    // Held, the scroll button repeats after its 50ms delay, every 100ms.
    view.handle_pointer({view.scroll_increase_bounds()->center(), PointerButton::None, PointerAction::Move});
    view.handle_pointer({view.scroll_increase_bounds()->center(), PointerButton::Left, PointerAction::Down});
    view.tick(60ms);
    view.tick(100ms);
    CHECK_THAT(view.scroll_offset(), WithinAbs(150.0f, 1e-3));
    view.handle_pointer({view.scroll_increase_bounds()->center(), PointerButton::Left, PointerAction::Up});

    CHECK(view.handle_wheel({view.strip_bounds().center(), PointerButton::None, PointerAction::Wheel, 30.0f}));
    CHECK_THAT(view.scroll_offset(), WithinAbs(180.0f, 1e-3));

    view.set_selected_index(11);
    CHECK(view.tab_bounds(11).has_value());
    view.set_selected_index(0);
    CHECK_FALSE(view.scroll_offset() > 0.0f);
}

TEST_CASE("A tab strip with nothing to scroll leaves the wheel to the page around it", "[tabs][input]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    TabsControl          view;
    view.add_tab(document("Document"));
    lay_out(view, m, theme, {600.0f, 300.0f});
    REQUIRE_FALSE(view.scroll_increase_bounds().has_value());

    // The Scroll around the tabs cannot scroll, so the wheel chains on (a page scrolls).
    CHECK_FALSE(view.handle_wheel({view.strip_bounds().center(), PointerButton::None, PointerAction::Wheel, 30.0f}));
    CHECK(view.scroll_offset() == 0.0f);
}

TEST_CASE("Ctrl+Tab and Ctrl+Shift+Tab cycle the selection and Ctrl+F4 asks to close, from anywhere inside",
          "[tabs][input][shortcut]")
{
    HeadlessTextMeasurer m;
    Window               window;
    auto                 tabs = std::make_unique<TabsControl>();
    TabsControl*         view = tabs.get();
    ButtonControl*       page = view->add_tab("One", std::make_unique<ButtonControl>("Page"));
    view->add_tab("Two", std::make_unique<TextControl>("b"));
    view->add_tab("Three", std::make_unique<TextControl>("c"));
    std::vector<std::string> closes;
    view->on_tab_close_requested([&](const std::string& key) { closes.push_back(key); });
    window.set_content(std::move(tabs));
    window.layout({600.0f, 300.0f}, m);

    // Focus in the selected tab's content, not on the strip.
    window.focus_element(page);
    REQUIRE(window.focused() == page);
    ui::testing::seams(window).handle_key({Key::Tab, KeyAction::Down, {.ctrl = true}});
    CHECK(view->selected_index() == 1);
    ui::testing::seams(window).handle_key({Key::Tab, KeyAction::Down, {.ctrl = true, .shift = true}});
    ui::testing::seams(window).handle_key({Key::Tab, KeyAction::Down, {.ctrl = true, .shift = true}});
    CHECK(view->selected_index() == 2); // wrapped
    ui::testing::seams(window).handle_key({Key::F4, KeyAction::Down, {.ctrl = true}});
    REQUIRE(closes.size() == 1);
    CHECK(closes[0] == "Three");

    // An app's own shortcut (Ctrl+T here) works the same way.
    int added = 0;
    view->add_shortcut({.keys = Ctrl + Key::T, .on_pressed = [&] { ++added; }});
    window.focus_element(view);
    ui::testing::seams(window).handle_key({Key::T, KeyAction::Down, {.ctrl = true}});
    CHECK(added == 1);
}

TEST_CASE("Dragging a tab reorders the strip, and the selection and its key travel with it", "[tabs][input]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    TabsControl          view;
    for (const char* h : {"A", "B", "C"})
    {
        Tab item = document(h);
        item.key = std::string{"key-"} + h;
        view.add_tab(std::move(item));
    }
    int changes = 0;
    view.on_tabs_changed([&] { ++changes; });
    lay_out(view, m, theme, {800.0f, 300.0f});

    const Point start = view.tab_bounds(0)->center();
    const Point end   = Point{view.tab_bounds(2)->center().x + 5.0f, start.y};
    view.handle_pointer({start, PointerButton::Left, PointerAction::Down});
    view.handle_pointer({end, PointerButton::Left, PointerAction::Move});
    view.handle_pointer({end, PointerButton::Left, PointerAction::Up});
    CHECK(view.header(2) == "A");
    CHECK(view.header(0) == "B");
    CHECK(view.key(2) == "key-A");
    CHECK(view.key(0) == "key-B");
    CHECK(view.selected_index() == 2);
    CHECK(view.selected_key() == "key-A");
    CHECK(changes >= 1);

    // With can_reorder_tabs off a drag does nothing.
    view.set_can_reorder_tabs(false);
    lay_out(view, m, theme, {800.0f, 300.0f});
    view.handle_pointer({view.tab_bounds(0)->center(), PointerButton::Left, PointerAction::Down});
    view.handle_pointer({Point{view.tab_bounds(2)->center().x + 5.0f, start.y}, PointerButton::Left, PointerAction::Move});
    CHECK(view.header(0) == "B");
}

TEST_CASE("Closing a tab with the pointer in the strip holds the Equal width until it leaves", "[tabs][layout]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    TabsControl          view;
    for (int i = 0; i < 6; ++i)
    {
        view.add_tab(document("Document"));
    }
    view.on_tab_close_requested([&](const std::string& key) { view.remove_tab(key); });
    lay_out(view, m, theme, {800.0f, 300.0f});
    const float before = view.tab_bounds(0)->width;
    REQUIRE(before < 240.0f);

    const Point close = view.close_button_bounds(2)->center();
    view.handle_pointer({close, PointerButton::None, PointerAction::Move});
    click(view, close);
    lay_out(view, m, theme, {800.0f, 300.0f});
    CHECK_THAT(view.tab_bounds(0)->width, WithinAbs(before, 1e-3));

    view.handle_pointer({Point{close.x, view.bounds().bottom() - 5.0f}, PointerButton::None, PointerAction::Move});
    lay_out(view, m, theme, {800.0f, 300.0f});
    CHECK(view.tab_bounds(0)->width > before);
}

TEST_CASE("The strip header and footer take the strip's ends, and the background fills the strip", "[tabs][layout]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light();
    TabsControl          view;
    view.add_tab(document("Document 0"));
    view.set_tab_width_mode(TabsWidthMode::SizeToContent);
    TextControl* header = view.set_tab_strip_header_content(std::make_unique<TextControl>("TabStripHeader Content"));
    TextControl* footer = view.set_tab_strip_footer_content(std::make_unique<TextControl>("TabStripFooter Content"));
    view.set_strip_background(Color::from_argb(0xFF99CCFF));
    lay_out(view, m, theme, {900.0f, 300.0f});

    CHECK_THAT(header->bounds().x, WithinAbs(view.bounds().x, 1e-3));
    CHECK_THAT(view.tab_bounds(0)->x, WithinAbs(header->bounds().right() + 1.0f + 4.0f, 1e-3));
    CHECK(footer->bounds().x >= view.add_button()->bounds().right());

    TestRenderer r;
    view.render(r, theme);
    const auto& fill = std::get<FillRoundedRect>(r.commands[0]);
    CHECK(fill.color == Color::from_argb(0xFF99CCFF));
    CHECK(fill.bounds == view.strip_bounds());
}

namespace
{
void click_at(HeadlessUI& ui, Point p)
{
    ui::testing::seams(ui.window()).handle_pointer({p, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({p, PointerButton::Left, PointerAction::Up});
}
} // namespace

TEST_CASE("Tabs: a press in a tab's content focuses the control there, one on a tab the view", "[tabview][focus]")
{
    HeadlessUI      ui;
    auto            tabs = std::make_unique<TabsControl>();
    TabsControl*    view = tabs.get();
    TextBoxControl* box  = view->add_tab("Document", std::make_unique<TextBoxControl>());
    (void)view->add_tab("Other", std::make_unique<TextControl>("Other"));
    ui.window().set_content(std::move(tabs));
    ui.layout({800.0f, 600.0f});

    // The view is one stop before its content in the ring and covers it, but only the tabs are its.
    const Point on_box{box->bounds().x + 10.0f, box->bounds().center().y};
    CHECK_FALSE(view->takes_focus_at(on_box));
    click_at(ui, on_box);
    CHECK(ui.window().focused() == box);

    click_at(ui, view->tab_bounds(0)->center());
    CHECK(ui.window().focused() == view);

    // Empty strip space is nobody's stop: focus stays where it was.
    const Rect  strip = view->strip_bounds();
    const Point empty{strip.right() - 4.0f, strip.center().y};
    CHECK_FALSE(view->takes_focus_at(empty));
}

TEST_CASE("Under Aero the tabs are the era's faces over a framed page the selected one merges into",
          "[tabs][render][aero]")
{
    HeadlessTextMeasurer m;
    const Theme          aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    TabsControl          view;
    TextControl*         first = view.add_tab("One", std::make_unique<TextControl>("content"));
    view.add_tab("Two", std::make_unique<TextControl>("content"));
    lay_out(view, m, aero, {400.0f, 300.0f});

    // The content sits inside the page's 1 DIP border and 4 DIPs of padding.
    const Rect b = view.bounds();
    CHECK(first->bounds().x == b.x + 5.0f);

    TestRenderer r;
    view.render(r, aero);
    bool page = false;
    bool face = false;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&command))
        {
            page = page || (fill->color == Color::from_argb(0xFFF9F9F9) && fill->bounds.width == b.width);
        }
        if (const auto* fill = std::get_if<FillRoundedRectGradient>(&command))
        {
            face = face || fill->brush.stops.front().color == Color::from_argb(0xFFF3F3F3);
        }
    }
    CHECK(page);
    CHECK(face); // the unselected tab
}

TEST_CASE("Automation sees a tab item per tab, with a close button on a closable one", "[tabs][automation]")
{
    HeadlessTextMeasurer m;
    TabsControl          view;
    view.add_tab("One", std::make_unique<TextControl>("first"));
    view.add_tab("Two", std::make_unique<TextControl>("second"));
    view.set_closable(1, false);
    lay_out(view, m, Theme::light(), {400.0f, 300.0f});

    REQUIRE(describe_automation(view) ==
            "Tab [Selection Value=\"One\" (ReadOnly)]\n"
            "  TabItem \"One\" [SelectionItem=Selected]\n"
            "    Button \"Close tab\" [Invoke]\n"
            "  TabItem \"Two\" [SelectionItem=Unselected]\n"
            "  Text \"first\"\n");

    const std::vector<Element*> top = automation_children(view);
    REQUIRE(automation_parent(*top[1]) == &view);
    REQUIRE(top[1]->bounds() == *view.tab_bounds(1));
    std::vector<Element*> selected;
    ui::automation_providers(view).selection->selected_items(selected);
    REQUIRE(selected == std::vector<Element*>{top[0]});

    REQUIRE(ui::automation_providers(*top[1]).selection_item->select_item());
    CHECK(view.selected_index() == 1);

    // Invoking the close button asks to close the tab, as a click does.
    std::string asked;
    view.on_tab_close_requested([&](const std::string& key) { asked = key; });
    Element* close = automation_children(*top[0]).front();
    REQUIRE(automation_parent(*close) == top[0]);
    REQUIRE(ui::automation_providers(*close).invoke->invoke());
    CHECK(asked == view.key(0));
}
