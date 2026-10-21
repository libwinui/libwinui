// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/containers/Expander.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

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
PointerEvent leave()
{
    return {{}, PointerButton::None, PointerAction::Leave};
}
KeyEvent key_down(Key k)
{
    return {k, KeyAction::Down};
}
KeyEvent key_up(Key k)
{
    return {k, KeyAction::Up};
}

ExpanderControl make_expander(bool expanded = false)
{
    ExpanderControl      e{"More details", expanded};
    HeadlessTextMeasurer measurer;
    const Size           desired = e.measure({200.0f, Unbounded}, measurer, Theme::default_light());
    e.arrange({100.0f, 100.0f, desired.width, desired.height});
    return e;
}
} // namespace

TEST_CASE("New expander is collapsed, Normal, and enabled", "[expander]")
{
    ExpanderControl e = make_expander();
    REQUIRE(e.enabled());
    REQUIRE_FALSE(e.expanded());
    REQUIRE_FALSE(e.focused());
    REQUIRE_FALSE(e.is_pressed());
}

TEST_CASE("Constructing with expanded=true starts expanded", "[expander]")
{
    ExpanderControl e{"Already open", true};
    REQUIRE(e.expanded());
}

TEST_CASE("Collapsed height is the header alone; expanding grows it by the content's size", "[expander][layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    ExpanderControl collapsed{"Section"};
    const Size      collapsed_size = collapsed.measure({300.0f, Unbounded}, measurer, theme);
    collapsed.arrange({0, 0, collapsed_size.width, collapsed_size.height});

    ExpanderControl expanded{"Section", true};
    expanded.set_content(std::make_unique<TextControl>("Body text"));
    const Size expanded_size = expanded.measure({300.0f, Unbounded}, measurer, theme);

    REQUIRE(expanded_size.height > collapsed_size.height);
    REQUIRE_THAT(collapsed_size.height, WithinAbs(static_cast<double>(collapsed.header_bounds().height), 1e-3));
}

TEST_CASE("content_padding insets the opened band and defaults to 16px", "[expander][layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    ExpanderControl wide{"Section", true};
    REQUIRE_THAT(wide.content_padding().left, WithinAbs(16.0, 1e-3));
    wide.set_content(std::make_unique<TextControl>("Body text"));
    const Size wide_size = wide.measure({300.0f, Unbounded}, measurer, theme);
    wide.arrange({0, 0, wide_size.width, wide_size.height});
    const Rect wide_content = wide.content()->bounds();

    ExpanderControl tight{"Section", true};
    tight.set_content_padding(Edges{.left = 4.0f, .right = 4.0f, .bottom = 4.0f});
    tight.set_content(std::make_unique<TextControl>("Body text"));
    const Size tight_size = tight.measure({300.0f, Unbounded}, measurer, theme);
    tight.arrange({0, 0, tight_size.width, tight_size.height});
    const Rect tight_content = tight.content()->bounds();

    // Less padding: the content sits further left and the control is no taller, despite the
    // the 48 DIP minimum header height.
    REQUIRE(tight_content.left() < wide_content.left());
    REQUIRE(tight_size.height <= wide_size.height);
}

TEST_CASE("Content set while collapsed doesn't affect measured size until expanded", "[expander][layout]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    ExpanderControl e{"Section"};
    e.set_content(std::make_unique<TextControl>("Body text"));
    const Size collapsed_size = e.measure({300.0f, Unbounded}, measurer, theme);

    e.set_expanded(true);
    const Size expanded_size = e.measure({300.0f, Unbounded}, measurer, theme);

    REQUIRE(expanded_size.height > collapsed_size.height);
}

TEST_CASE("Hover state follows the pointer within the header only", "[expander][state]")
{
    ExpanderControl e             = make_expander();
    const Point     inside_header = e.header_bounds().center();

    e.handle_pointer(move(inside_header));
    REQUIRE(e.is_pressed() == false);

    e.handle_pointer(down(inside_header));
    REQUIRE(e.is_pressed());

    e.handle_pointer(move({0, 0}));
    REQUIRE_FALSE(e.is_pressed()); // moved outside the header while still "down"

    e.handle_pointer(leave());
}

TEST_CASE("Clicking the header toggles expanded and fires on_expanded_changed", "[expander][command]")
{
    ExpanderControl   e = make_expander();
    std::vector<bool> observed;
    e.on_expanded_changed([&](bool v) { observed.push_back(v); });

    const Point inside_header = e.header_bounds().center();
    e.handle_pointer(down(inside_header));
    e.handle_pointer(up(inside_header));
    REQUIRE(e.expanded());
    REQUIRE(observed == std::vector<bool>{true});

    HeadlessTextMeasurer measurer;
    const Size           desired = e.measure({200.0f, Unbounded}, measurer, Theme::default_light());
    e.arrange({100.0f, 100.0f, desired.width, desired.height});
    e.handle_pointer(down(e.header_bounds().center()));
    e.handle_pointer(up(e.header_bounds().center()));
    REQUIRE_FALSE(e.expanded());
    REQUIRE(observed == (std::vector<bool>{true, false}));
}

TEST_CASE("Release outside the header cancels the toggle", "[expander][command]")
{
    ExpanderControl e       = make_expander();
    int             toggles = 0;
    e.on_expanded_changed([&](bool) { ++toggles; });

    e.handle_pointer(down(e.header_bounds().center()));
    e.handle_pointer(up({0, 0}));

    REQUIRE(toggles == 0);
    REQUIRE_FALSE(e.expanded());
}

TEST_CASE("Keyboard activation requires focus and uses Space only, not Enter", "[expander][command]")
{
    ExpanderControl e = make_expander();

    e.handle_key(key_down(Key::Space));
    e.handle_key(key_up(Key::Space));
    REQUIRE_FALSE(e.expanded()); // not focused yet

    ui::testing::set_focused(e, true);
    e.handle_key(key_down(Key::Enter));
    e.handle_key(key_up(Key::Enter));
    REQUIRE_FALSE(e.expanded()); // Enter does not activate an Expander

    e.handle_key(key_down(Key::Space));
    REQUIRE(e.is_pressed());
    e.handle_key(key_up(Key::Space));
    REQUIRE(e.expanded());
}

TEST_CASE("Losing focus cancels an in-progress keyboard press", "[expander][state]")
{
    ExpanderControl e = make_expander();
    ui::testing::set_focused(e, true);
    e.handle_key(key_down(Key::Space));
    REQUIRE(e.is_pressed());

    ui::testing::set_focused(e, false);
    REQUIRE_FALSE(e.is_pressed());
    e.handle_key(key_up(Key::Space)); // arrives after focus loss
    REQUIRE_FALSE(e.expanded());
}

TEST_CASE("Disabled expander ignores input", "[expander][state]")
{
    ExpanderControl e = make_expander();
    e.set_enabled(false);

    e.handle_pointer(down(e.header_bounds().center()));
    e.handle_pointer(up(e.header_bounds().center()));
    ui::testing::set_focused(e, true);
    e.handle_key(key_down(Key::Space));
    e.handle_key(key_up(Key::Space));

    REQUIRE_FALSE(e.expanded());
}

TEST_CASE("set_expanded() sets state directly and fires the callback only on a real change", "[expander]")
{
    ExpanderControl e     = make_expander();
    int             fired = 0;
    e.on_expanded_changed([&](bool) { ++fired; });

    e.set_expanded(true);
    REQUIRE(e.expanded());
    REQUIRE(fired == 1);

    e.set_expanded(true); // no change
    REQUIRE(fired == 1);
}

TEST_CASE("A focusable content control only joins the tab ring while expanded", "[expander][focus]")
{
    HeadlessTextMeasurer measurer;

    auto             expander_owned = std::make_unique<ExpanderControl>("Section");
    ExpanderControl* expander       = expander_owned.get();
    ButtonControl*   inner          = expander->set_content(std::make_unique<ButtonControl>("Inner"));

    Window window;
    window.set_content(std::move(expander_owned));
    window.layout({300, 100}, measurer);

    std::vector<Element*> focusables;
    window.content()->collect_focusables(focusables);
    REQUIRE(std::find(focusables.begin(), focusables.end(), static_cast<Element*>(inner)) == focusables.end());
    REQUIRE(std::find(focusables.begin(), focusables.end(), static_cast<Element*>(expander)) != focusables.end());

    expander->set_expanded(true);
    window.layout({300, 100}, measurer);

    focusables.clear();
    window.content()->collect_focusables(focusables);
    REQUIRE(std::find(focusables.begin(), focusables.end(), static_cast<Element*>(inner)) != focusables.end());
}

TEST_CASE("Render draws the header always, and content plus a separator only while expanded", "[expander][render]")
{
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    ExpanderControl e{"Section"};
    e.set_content(std::make_unique<TextControl>("Body"));

    {
        const Size desired = e.measure({300.0f, Unbounded}, measurer, theme);
        e.arrange({0, 0, desired.width, desired.height});
        TestRenderer r;
        e.render(r, theme);
        REQUIRE(r.count_of<DrawTextRun>() == 2); // header label + chevron glyph
    }

    e.set_expanded(true);
    {
        const Size desired = e.measure({300.0f, Unbounded}, measurer, theme);
        e.arrange({0, 0, desired.width, desired.height});
        TestRenderer r;
        e.render(r, theme);
        REQUIRE(r.count_of<DrawTextRun>() == 3);
    }
}

TEST_CASE("Header reserves its minimum height and width even with a short label", "[expander][layout]")
{
    // A 48 DIP minimum height and a 96 DIP minimum width.
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    ExpanderControl e{"x"};
    const Size      desired = e.measure({Unbounded, Unbounded}, measurer, theme);

    // Both minimums apply.
    REQUIRE_THAT(desired.width, WithinAbs(96.0, 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(48.0, 1e-3));

    e.arrange({0, 0, desired.width, desired.height});
    REQUIRE_THAT(e.header_bounds().height, WithinAbs(48.0, 1e-3));
}

TEST_CASE("The hover/press wash covers the chevron button only, not the whole header", "[expander][render]")
{
    // The hover and press states animate the chevron button only; the
    // header's own background stays at its rest fill.
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    ExpanderControl e{"Section"};
    const Size      desired = e.measure({300.0f, Unbounded}, measurer, theme);
    e.arrange({0, 0, desired.width, desired.height});

    e.handle_pointer(move(e.header_bounds().center()));

    TestRenderer r;
    e.render(r, theme);

    const Rect header = e.header_bounds();
    bool       found  = false;
    for (const auto& cmd : r.commands)
    {
        const auto* fill = std::get_if<FillRoundedRect>(&cmd);
        if (fill == nullptr || fill->color != theme.subtle_button_hover.background)
        {
            continue;
        }
        found = true;
        REQUIRE_THAT(fill->bounds.width, WithinAbs(32.0, 1e-3)); // the chevron button's size
        REQUIRE_THAT(fill->bounds.height, WithinAbs(32.0, 1e-3));
        // The chevron button's 8dip right inset.
        REQUIRE_THAT(fill->bounds.right(), WithinAbs(static_cast<double>(header.right()) - 8.0, 1e-3));
    }
    REQUIRE(found);
}

TEST_CASE("Expanding slides the content into a clipped band over 333ms", "[expander][render][animation]")
{
    // Expanding and collapsing: the box is full height at once (it shows or hides at
    // time 0) and the content slides in over 333ms, out over 167ms.
    HeadlessTextMeasurer measurer;
    Theme                theme = Theme::light();

    ExpanderControl e{"Section"};
    e.set_content(std::make_unique<TextControl>("Body"));

    const auto relayout = [&]
    {
        const Size desired = e.measure({300.0f, Unbounded}, measurer, theme);
        e.arrange({0, 0, desired.width, desired.height});
    };

    relayout();
    e.tick(16ms); // a host with a frame clock: from here the slide really animates
    e.set_expanded(true);
    relayout();

    REQUIRE_THAT(e.slide_progress(), WithinAbs(0.0, 1e-3));
    REQUIRE(e.content_bounds().height > 0.0f);
    const float band_height = e.content_bounds().height;

    e.tick(166ms);
    const float midway = e.slide_progress();
    REQUIRE(midway > 0.0f);
    REQUIRE(midway < 1.0f);
    REQUIRE_THAT(e.content_bounds().height, WithinAbs(band_height, 1e-3)); // the box never resizes

    e.tick(400ms);
    REQUIRE_THAT(e.slide_progress(), WithinAbs(1.0, 1e-3));

    // The band is clipped, so a half-slid child never paints over the header.
    TestRenderer r;
    e.render(r, theme);
    REQUIRE(r.count_of<PushClip>() >= 1);
    REQUIRE(r.count_of<PushClip>() == r.count_of<PopClip>());

    e.set_expanded(false);
    e.tick(100ms);
    REQUIRE(e.slide_progress() < 1.0f);
    e.tick(100ms); // 200ms total: past the shorter 167ms collapse, but not the 333ms open
    REQUIRE_THAT(e.slide_progress(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("Open and close run their own easing curve, not one curve read both ways", "[expander][animation]")
{
    // Expanding eases along the spline (0,0,0,1) and collapsing along (1,1,0,1). Reading one curve backwards for
    // the close makes it sit still and then snap.
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();

    ExpanderControl e{"Section"};
    e.set_content(std::make_unique<TextControl>("Body"));
    const Size desired = e.measure({300.0f, Unbounded}, measurer, theme);
    e.arrange({0, 0, desired.width, desired.height});
    e.tick(16ms); // a host with a frame clock, so the slide really animates

    // Expanding, half of its 333ms: y = 3x^(2/3) - 2x at x = 0.5.
    e.set_expanded(true);
    e.tick(Duration{0.5 * 333.0});
    REQUIRE_THAT(e.slide_progress(), WithinAbs(0.89, 0.02));

    e.tick(333ms);
    REQUIRE_THAT(e.slide_progress(), WithinAbs(1.0, 1e-3));

    // Collapsing, a tenth of its 167ms: about 10% of the way hidden already.
    e.set_expanded(false);
    e.tick(Duration{0.1 * 167.0});
    const float tenth = 1.0f - e.slide_progress();
    REQUIRE(tenth > 0.05f);
    REQUIRE(tenth < 0.20f);

    // ...and nearly home by half way.
    e.tick(Duration{0.4 * 167.0});
    REQUIRE_THAT(1.0f - e.slide_progress(), WithinAbs(0.875, 0.03));

    e.tick(167ms);
    REQUIRE_THAT(e.slide_progress(), WithinAbs(0.0, 1e-3));
}

TEST_CASE("A header click never reaches content parked behind the header", "[expander][input]")
{
    // Mid-slide the content sits partly behind the header, so "inside bounds()" is not "visible".
    // A header press must not reach it.
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();

    ExpanderControl e{"Section", /*expanded=*/true};
    auto            body    = std::make_unique<ButtonControl>("Inside");
    ButtonControl*  content = e.set_content(std::move(body));

    const auto relayout = [&]
    {
        const Size desired = e.measure({300.0f, Unbounded}, measurer, theme);
        e.arrange({0, 0, desired.width, desired.height});
    };
    relayout();
    e.tick(16ms);

    int clicks = 0;
    content->on_click([&clicks] { ++clicks; });

    e.set_expanded(false); // starts the 167ms collapse; content is still live and sliding up
    e.tick(160ms);
    relayout();
    REQUIRE(content->bounds().top() < e.header_bounds().bottom()); // genuinely parked under it

    const Point header_point{e.header_bounds().left() + 4.0f, e.header_bounds().top() + 4.0f};
    e.handle_pointer(down(header_point));
    e.handle_pointer(up(header_point));

    CHECK(clicks == 0);
    CHECK(e.expanded()); // the header click did what it was for: it reopened the section
}

TEST_CASE("Without a frame clock the slide settles immediately", "[expander][animation]")
{
    // Headless layout tests never tick, so the content must be where it ends up.
    ExpanderControl e{"Section"};
    e.set_content(std::make_unique<TextControl>("Body"));
    e.set_expanded(true);
    REQUIRE_THAT(e.slide_progress(), WithinAbs(1.0, 1e-3));
}

TEST_CASE("ExpandDirection Up puts the content above the header and turns the chevron round", "[expander]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();
    ExpanderControl      e("Header", true);
    e.set_content(std::make_unique<ButtonControl>("Body"));
    e.set_expand_direction(ExpandDirection::Up);
    const Size d = e.measure(Size{300.0f, Unbounded}, measurer, theme);
    e.arrange(Rect{0.0f, 0.0f, 300.0f, d.height});
    CHECK(e.content_bounds().y < e.header_bounds().y);
    CHECK_THAT(e.header_bounds().bottom(), WithinAbs(e.bounds().bottom(), 1e-3));

    auto chevron = [&e, &theme]
    {
        TestRenderer r;
        e.render(r, theme);
        for (const auto& c : r.commands)
        {
            if (const auto* t = std::get_if<DrawTextRun>(&c); t != nullptr && code_point_count(t->text) == 1)
            {
                return t->text;
            }
        }
        return std::string{};
    };
    // Expanded upward, the chevron points down (the up chevron, turned); collapsed, up.
    CHECK(chevron() == encode_utf8(glyphs::ChevronDown.codepoint));
    e.set_expanded(false);
    CHECK(chevron() == encode_utf8(glyphs::ChevronUp.codepoint));
}

TEST_CASE("Header content replaces the header text and sits in the header row", "[expander]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();
    ExpanderControl      e;
    ButtonControl*       header = e.set_header_content(std::make_unique<ButtonControl>("Centered"));
    header->set_horizontal_alignment(Alignment::Center);
    const Size d = e.measure(Size{300.0f, Unbounded}, measurer, theme);
    e.arrange(Rect{0.0f, 0.0f, 300.0f, d.height});
    CHECK(e.header_bounds().contains(header->bounds().center()));
    CHECK(header->bounds().center().x > 100.0f); // centered in the row, not at its start
}

namespace
{
void click(HeadlessUI& ui, Point p)
{
    ui::testing::seams(ui.window()).handle_pointer({p, PointerButton::Left, PointerAction::Down});
    ui::testing::seams(ui.window()).handle_pointer({p, PointerButton::Left, PointerAction::Up});
}
} // namespace

TEST_CASE("Expander: a press in its content focuses the control there, one on the header the expander",
          "[expander][focus]")
{
    HeadlessUI       ui;
    auto             expander = std::make_unique<ExpanderControl>();
    ExpanderControl* view     = expander.get();
    view->set_header("Details");
    TextBoxControl* box = view->set_content(std::make_unique<TextBoxControl>());
    view->set_expanded(true);
    ui.window().set_content(std::move(expander));
    ui.layout({800.0f, 600.0f});
    ui.layout({800.0f, 600.0f}); // settle the expand animation's first frame

    const Point on_box{box->bounds().x + 10.0f, box->bounds().center().y};
    REQUIRE(box->bounds().height > 0.0f);
    CHECK_FALSE(view->takes_focus_at(on_box));
    click(ui, on_box);
    CHECK(ui.window().focused() == box);

    CHECK(view->takes_focus_at(view->header_bounds().center()));
}

// The era has no expander; its header takes the drop-down's solid arrow, turning up once open.
TEST_CASE("A classic Expander marks its header with the solid arrow, not a chevron glyph", "[expander][render][retro]")
{
    for (ThemeVariant variant : {ThemeVariant::Windows9x, ThemeVariant::Windows3x})
    {
        CAPTURE(static_cast<int>(variant));
        const Theme theme = resolve_theme(ThemeRequest{.variant = variant}, OsThemeSnapshot{});
        REQUIRE(theme.disclosure_visual == ThemeDisclosureVisual::ClassicArrow);
        for (bool expanded : {false, true})
        {
            ExpanderControl      e{"More details", expanded};
            HeadlessTextMeasurer measurer;
            const Size           desired = e.measure({200.0f, Unbounded}, measurer, theme);
            e.arrange({0.0f, 0.0f, desired.width, desired.height});
            TestRenderer renderer;
            e.render(renderer, theme);

            // No icon-font text: the only text is the header.
            for (const auto& cmd : renderer.commands)
            {
                if (const auto* t = std::get_if<DrawTextRun>(&cmd))
                {
                    CHECK(t->text == "More details");
                }
            }
            // The arrow's rows in the header text color, top to bottom: widest first when it
            // points down.
            std::vector<std::pair<float, float>> rows;
            for (const auto& cmd : renderer.commands)
            {
                if (const auto* f = std::get_if<FillRoundedRect>(&cmd);
                    f && f->color == theme.colors().text && f->bounds.height == 1.0f)
                {
                    rows.emplace_back(f->bounds.top(), f->bounds.width);
                }
            }
            std::sort(rows.begin(), rows.end());
            std::vector<float> widths;
            for (const auto& row : rows)
            {
                widths.push_back(row.second);
            }
            CHECK(widths ==
                  (expanded ? std::vector<float>{1.0f, 3.0f, 5.0f, 7.0f} : std::vector<float>{7.0f, 5.0f, 3.0f, 1.0f}));
        }
    }
    // Fluent keeps its chevron.
    CHECK(Theme::default_light().disclosure_visual == ThemeDisclosureVisual::IconChevron);
}

TEST_CASE("An era without cards divides header and content with its own divider, inside the border",
          "[expander][render][era]")
{
    const Theme          theme = resolve_theme(ThemeRequest{.variant = ThemeVariant::Windows9x}, OsThemeSnapshot{});
    HeadlessTextMeasurer measurer;
    ExpanderControl      e{"Section", true};
    e.set_content(std::make_unique<TextControl>("Body"));
    const Size size = e.measure({200.0f, Unbounded}, measurer, theme);
    e.arrange({0.0f, 0.0f, size.width, size.height});

    TestRenderer r;
    e.render(r, RenderContext{theme});
    std::vector<FillRoundedRect> lines;
    for (const RenderCommand& command : r.commands)
    {
        // The divider spans the card; the bevel's own one-pixel edges sit at its top and bottom.
        if (const auto* fill = std::get_if<FillRoundedRect>(&command);
            fill && fill->bounds.height == 1.0f && fill->bounds.width > size.width * 0.5f && fill->bounds.y > 2.0f &&
            fill->bounds.bottom() < size.height - 2.0f)
        {
            lines.push_back(*fill);
        }
    }
    // The etched groove: a dark line over a light one, neither crossing the card's bevel.
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].color == theme.surfaces.separator_dark);
    CHECK(lines[1].color == theme.surfaces.separator_light);
    CHECK(lines[1].bounds.y == lines[0].bounds.y + 1.0f);
    for (const FillRoundedRect& line : lines)
    {
        CHECK(line.bounds.x > 0.0f);
        CHECK(line.bounds.right() < size.width);
    }
}
