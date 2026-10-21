// SPDX-License-Identifier: MIT
#include "ui/themes/ControlChrome.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Index.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/VisualStates.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/navigation/PipsPager.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
KeyEvent key_down(Key k)
{
    return {k, KeyAction::Down};
}
} // namespace

TEST_CASE("A default-constructed PipsPager has infinite pages", "[pips-pager]")
{
    PipsPagerControl pager;
    REQUIRE(pager.number_of_pages() == std::nullopt);
    REQUIRE(pager.infinite());
    REQUIRE(pager.selected_index() == 0);
    REQUIRE(pager.max_visible_pips() == 5);
    REQUIRE(pager.orientation() == Orientation::Horizontal);
    REQUIRE(pager.previous_button_visibility() == PipsPagerButtonVisibility::Collapsed);
    REQUIRE(pager.next_button_visibility() == PipsPagerButtonVisibility::Collapsed);
    REQUIRE(pager.wrap_mode() == PipsPagerWrapMode::None);
    REQUIRE(pager.focusable());
}

TEST_CASE("Construction clamps an out-of-range initial selection, except with an open-ended count", "[pips-pager]")
{
    PipsPagerControl pager{5, 100};
    REQUIRE(pager.selected_index() == 4);

    PipsPagerControl open{std::nullopt, 7};
    REQUIRE(open.number_of_pages() == std::nullopt);
    REQUIRE(open.selected_index() == 7); // no upper bound
}

TEST_CASE("set_number_of_pages re-clamps the selection, firing the callback only if that moves it", "[pips-pager]")
{
    PipsPagerControl pager{5, 4};

    int                        calls = 0;
    std::optional<std::size_t> last;
    pager.on_selection_changed(
        [&](std::size_t i)
        {
            ++calls;
            last = i;
        });

    pager.set_number_of_pages(10); // selection (4) is still valid: no reclamp needed
    REQUIRE(calls == 0);

    pager.set_number_of_pages(3); // clamps 4 -> 2
    REQUIRE(pager.selected_index() == 2);
    REQUIRE(calls == 1);
    REQUIRE(last == 2);

    pager.set_number_of_pages(0);
    REQUIRE(pager.selected_index() == 0);
    REQUIRE(calls == 2);
    REQUIRE(last == 0);
}

TEST_CASE("set_selected_index clamps and no-ops on an actual non-change", "[pips-pager]")
{
    PipsPagerControl pager{3};

    int calls = 0;
    pager.on_selection_changed([&](std::size_t) { ++calls; });

    pager.set_selected_index(0); // already there: no-op
    REQUIRE(calls == 0);

    pager.set_selected_index(2);
    REQUIRE(pager.selected_index() == 2);
    REQUIRE(calls == 1);

    pager.set_selected_index(1000); // clamps to 2, already there: no-op
    REQUIRE(pager.selected_index() == 2);
    REQUIRE(calls == 1);
}

TEST_CASE("Selection alone never changes measured size; page count/max_visible_pips/orientation/buttons do",
          "[pips-pager][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    PipsPagerControl pager{10};
    const Size       first = pager.measure({500.0f, Unbounded}, m, theme);
    pager.set_selected_index(9);
    const Size second = pager.measure({500.0f, Unbounded}, m, theme);
    REQUIRE_THAT(first.width, WithinAbs(static_cast<double>(second.width), 1e-3));
    REQUIRE_THAT(first.height, WithinAbs(static_cast<double>(second.height), 1e-3));

    PipsPagerControl vertical{10};
    vertical.set_orientation(Orientation::Vertical);
    const Size v = vertical.measure({Unbounded, 500.0f}, m, theme);
    REQUIRE_THAT(v.width, WithinAbs(static_cast<double>(first.height), 1e-3));
    REQUIRE_THAT(v.height, WithinAbs(static_cast<double>(first.width), 1e-3));

    // A reserved nav button adds one slot even while hidden. VisibleOnPointerOver reserves like
    // Visible, so the layout never depends on hover.
    PipsPagerControl with_prev{10};
    with_prev.set_previous_button_visibility(PipsPagerButtonVisibility::VisibleOnPointerOver);
    const Size prev_size = with_prev.measure({500.0f, Unbounded}, m, theme);
    REQUIRE(prev_size.width > first.width);

    PipsPagerControl with_both{10};
    with_both.set_previous_button_visibility(PipsPagerButtonVisibility::Visible);
    with_both.set_next_button_visibility(PipsPagerButtonVisibility::VisibleOnPointerOver);
    const Size  both_size = with_both.measure({500.0f, Unbounded}, m, theme);
    const float per_slot  = prev_size.width - first.width;
    REQUIRE_THAT(both_size.width - prev_size.width, WithinAbs(static_cast<double>(per_slot), 1e-3));

    PipsPagerControl fewer{10};
    fewer.set_max_visible_pips(2);
    const Size fewer_size = fewer.measure({500.0f, Unbounded}, m, theme);
    REQUIRE(fewer_size.width < first.width);
}

TEST_CASE("The visible pip window is centered on the selection and clamped at both ends", "[pips-pager][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    PipsPagerControl pager{20}; // max_visible_pips defaults to 5
    const Size       desired = pager.measure({1000.0f, Unbounded}, m, theme);
    pager.arrange({0, 0, desired.width, desired.height});

    constexpr int VisibleCount = 5;
    const float   slot_width   = desired.width / static_cast<float>(VisibleCount);
    auto          click_slot   = [&](int slot)
    {
        const Point p{slot_width * (static_cast<float>(slot) + 0.5f), desired.height * 0.5f};
        pager.handle_pointer({p, PointerButton::Left, PointerAction::Down});
    };

    // Low end: window is [0,4], clamped rather than centered.
    click_slot(0);
    REQUIRE(pager.selected_index() == 0);

    // Centered: selecting page 10 centers the window on it -> [8,12].
    pager.set_selected_index(10);
    click_slot(0);
    REQUIRE(pager.selected_index() == 8);
    pager.set_selected_index(10);
    click_slot(4);
    REQUIRE(pager.selected_index() == 12);

    // High end: window is [15,19], clamped rather than centered.
    pager.set_selected_index(19);
    click_slot(4);
    REQUIRE(pager.selected_index() == 19);
    pager.set_selected_index(19);
    click_slot(0);
    REQUIRE(pager.selected_index() == 15);
}

TEST_CASE("Nav buttons step the selection and hide at their own end of the range", "[pips-pager][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    PipsPagerControl pager{3, 0};
    pager.set_previous_button_visibility(PipsPagerButtonVisibility::Visible);
    pager.set_next_button_visibility(PipsPagerButtonVisibility::Visible);
    const Size desired = pager.measure({1000.0f, Unbounded}, m, theme);
    pager.arrange({0, 0, desired.width, desired.height});

    // At page 0 Previous is Hidden and Disabled,
    // its slot kept.
    REQUIRE_FALSE(pager.previous_button_visible());
    REQUIRE(ui::testing::previous_button_visual_state(pager) == ButtonState::Disabled);
    REQUIRE(pager.next_button_visible());
    REQUIRE(ui::testing::next_button_visual_state(pager) != ButtonState::Disabled);

    // Nav buttons are 24x24, not a 12px pip slot.
    constexpr float NavButtonSize = 24.0f;
    const Point     prev_center{NavButtonSize * 0.5f, desired.height * 0.5f};
    const Point     next_center{desired.width - NavButtonSize * 0.5f, desired.height * 0.5f};

    pager.handle_pointer({prev_center, PointerButton::Left, PointerAction::Down});
    REQUIRE(pager.selected_index() == 0); // hidden: no-op

    pager.handle_pointer({next_center, PointerButton::Left, PointerAction::Down});
    REQUIRE(pager.selected_index() == 1);
    pager.handle_pointer({next_center, PointerButton::Left, PointerAction::Down});
    REQUIRE(pager.selected_index() == 2);
    REQUIRE_FALSE(pager.next_button_visible()); // now at the last page
    REQUIRE(ui::testing::next_button_visual_state(pager) == ButtonState::Disabled);
    REQUIRE(pager.previous_button_visible());

    pager.handle_pointer({next_center, PointerButton::Left, PointerAction::Down});
    REQUIRE(pager.selected_index() == 2); // hidden: no-op
}

TEST_CASE("PipsPagerWrapMode::Wrap lets nav buttons cycle past both ends", "[pips-pager][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    PipsPagerControl pager{3, 0};
    pager.set_wrap_mode(PipsPagerWrapMode::Wrap);
    pager.set_previous_button_visibility(PipsPagerButtonVisibility::Visible);
    pager.set_next_button_visibility(PipsPagerButtonVisibility::Visible);
    const Size desired = pager.measure({1000.0f, Unbounded}, m, theme);
    pager.arrange({0, 0, desired.width, desired.height});

    REQUIRE(pager.previous_button_visible()); // wrap keeps it available even at page 0
    REQUIRE(ui::testing::previous_button_visual_state(pager) != ButtonState::Disabled);

    constexpr float NavButtonSize = 24.0f;
    const Point     prev_center{NavButtonSize * 0.5f, desired.height * 0.5f};
    const Point     next_center{desired.width - NavButtonSize * 0.5f, desired.height * 0.5f};

    pager.handle_pointer({prev_center, PointerButton::Left, PointerAction::Down});
    REQUIRE(pager.selected_index() == 2); // wrapped from 0 to the last page

    pager.handle_pointer({next_center, PointerButton::Left, PointerAction::Down});
    REQUIRE(pager.selected_index() == 0); // wrapped from the last page back to 0
}

TEST_CASE("VisibleOnPointerOver shows/hits a nav button only while hovered or focused", "[pips-pager][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    PipsPagerControl pager{3, 1};
    pager.set_next_button_visibility(PipsPagerButtonVisibility::VisibleOnPointerOver);
    const Size desired = pager.measure({1000.0f, Unbounded}, m, theme);
    pager.arrange({0, 0, desired.width, desired.height});

    constexpr float NavButtonSize = 24.0f;
    const Point     next_center{desired.width - NavButtonSize * 0.5f, desired.height * 0.5f};

    // Hidden: the reserved slot is not hit-tested and holds no pip, so the click hits nothing.
    pager.handle_pointer({next_center, PointerButton::Left, PointerAction::Down});
    REQUIRE(pager.selected_index() == 1);

    ui::testing::set_focused(pager, true); // focus alone reveals it
    pager.handle_pointer({next_center, PointerButton::Left, PointerAction::Down});
    REQUIRE(pager.selected_index() == 2);
}

TEST_CASE("Arrow keys move the selection along the main axis, clamped by default, only while focused",
          "[pips-pager][input]")
{
    PipsPagerControl pager{3};

    pager.handle_key(key_down(Key::ArrowRight)); // not focused: ignored
    REQUIRE(pager.selected_index() == 0);

    ui::testing::set_focused(pager, true);
    pager.handle_key(key_down(Key::ArrowLeft)); // already at 0: clamps, stays 0
    REQUIRE(pager.selected_index() == 0);
    pager.handle_key(key_down(Key::ArrowRight));
    REQUIRE(pager.selected_index() == 1);
    pager.handle_key(key_down(Key::ArrowRight));
    REQUIRE(pager.selected_index() == 2);
    pager.handle_key(key_down(Key::ArrowRight)); // clamped at the end
    REQUIRE(pager.selected_index() == 2);

    pager.set_orientation(Orientation::Vertical);
    pager.set_selected_index(0);
    pager.handle_key(key_down(Key::ArrowRight)); // wrong axis now: ignored
    REQUIRE(pager.selected_index() == 0);
    pager.handle_key(key_down(Key::ArrowDown));
    REQUIRE(pager.selected_index() == 1);
}

TEST_CASE("Home/End jump to the first/last page while focused", "[pips-pager][input]")
{
    PipsPagerControl pager{5, 2};
    ui::testing::set_focused(pager, true);

    pager.handle_key(key_down(Key::End));
    REQUIRE(pager.selected_index() == 4);
    pager.handle_key(key_down(Key::Home));
    REQUIRE(pager.selected_index() == 0);
}

TEST_CASE("A pager with 0 or 1 pages is not focusable and ignores arrow keys", "[pips-pager][focus]")
{
    PipsPagerControl empty{0};
    REQUIRE_FALSE(empty.focusable());

    PipsPagerControl single{1};
    REQUIRE_FALSE(single.focusable());
    ui::testing::set_focused(single, true); // set_focused itself has no focusable() guard, matching SelectorBar
    single.handle_key(key_down(Key::ArrowRight));
    REQUIRE(single.selected_index() == 0);

    PipsPagerControl pager{2};
    REQUIRE(pager.focusable());
    pager.set_enabled(false);
    REQUIRE_FALSE(pager.focusable());
}

TEST_CASE("Render draws one ellipse per visible pip, and rect chrome only for shown nav buttons", "[pips-pager][render]")
{
    HeadlessTextMeasurer m;
    // A bordered era: Fluent's nav buttons are chromeless (see the next test).
    Theme theme                                               = Theme::light();
    ui::detail::edit_controls(theme).scroll_button_chromeless = false;

    PipsPagerControl pager{3, 1};
    const Size       desired = pager.measure({1000.0f, Unbounded}, m, theme);
    pager.arrange({0, 0, desired.width, desired.height});

    TestRenderer r;
    pager.render(r, theme);
    REQUIRE(r.count_of<FillEllipse>() == 3);     // one dot per visible pip, selected or not
    REQUIRE(r.count_of<FillRoundedRect>() == 0); // no hover wash, no nav buttons (Collapsed by default)

    pager.set_previous_button_visibility(PipsPagerButtonVisibility::Visible);
    pager.set_next_button_visibility(PipsPagerButtonVisibility::Visible);
    const Size desired2 = pager.measure({1000.0f, Unbounded}, m, theme); // Layout-dirty: re-measure/arrange
    pager.arrange({0, 0, desired2.width, desired2.height});

    TestRenderer r2;
    pager.render(r2, theme);
    REQUIRE(r2.count_of<FillEllipse>() == 3);
    REQUIRE(r2.count_of<FillRoundedRect>() == 2); // one background rect per shown nav button
}

TEST_CASE("Disabled PipsPager ignores pointer and keyboard input", "[pips-pager][state]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    PipsPagerControl pager{3};
    const Size       desired = pager.measure({1000.0f, Unbounded}, m, theme);
    pager.arrange({0, 0, desired.width, desired.height});
    ui::testing::set_focused(pager, true);
    pager.set_enabled(false);

    const float slot_width = desired.width / 3.0f;
    pager.handle_pointer({{slot_width * 2.5f, desired.height * 0.5f}, PointerButton::Left, PointerAction::Down});
    pager.handle_key(key_down(Key::ArrowRight));

    REQUIRE(pager.selected_index() == 0);
}

TEST_CASE("Fluent nav buttons are glyph-only, and point along the pager's axis", "[pips-pager][render]")
{
    HeadlessTextMeasurer m;
    const Theme          theme = Theme::light(); // Fluent: the nav buttons' background is transparent

    PipsPagerControl pager{3, 1};
    pager.set_previous_button_visibility(PipsPagerButtonVisibility::Visible);
    pager.set_next_button_visibility(PipsPagerButtonVisibility::Visible);
    const Size desired = pager.measure({1000.0f, Unbounded}, m, theme);
    pager.arrange({0, 0, desired.width, desired.height});

    const auto glyphs_drawn = [&]
    {
        TestRenderer r;
        pager.render(r, theme);
        REQUIRE(r.count_of<FillRoundedRect>() == 0);
        std::vector<std::string> texts;
        for (const RenderCommand& c : r.commands)
        {
            if (const auto* t = std::get_if<DrawTextRun>(&c))
            {
                texts.push_back(t->text);
            }
        }
        return texts;
    };

    // A horizontal pager rotates the 0xEDDB/0xEDDC buttons by -90 degrees.
    const std::vector<std::string> horizontal = glyphs_drawn();
    REQUIRE(horizontal == std::vector<std::string>{encode_utf8(0xEDD9), encode_utf8(0xEDDA)});

    pager.set_orientation(Orientation::Vertical);
    const Size vertical_size = pager.measure({Unbounded, 1000.0f}, m, theme);
    pager.arrange({0, 0, vertical_size.width, vertical_size.height});
    const std::vector<std::string> vertical = glyphs_drawn();
    REQUIRE(vertical == std::vector<std::string>{encode_utf8(0xEDDB), encode_utf8(0xEDDC)});
}

TEST_CASE("VisibleOnPointerOver hides an available button until the pointer is over the pager", "[pips-pager][input]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    PipsPagerControl pager{5, 2};
    pager.set_previous_button_visibility(PipsPagerButtonVisibility::VisibleOnPointerOver);
    const Size desired = pager.measure({1000.0f, Unbounded}, m, theme);
    pager.arrange({0, 0, desired.width, desired.height});
    REQUIRE_FALSE(pager.previous_button_visible());

    pager.handle_pointer({{desired.width * 0.5f, desired.height * 0.5f}, PointerButton::None, PointerAction::Move});
    REQUIRE(pager.previous_button_visible());

    pager.handle_pointer({{-10.0f, -10.0f}, PointerButton::None, PointerAction::Leave});
    REQUIRE_FALSE(pager.previous_button_visible());

    // At its edge it stays hidden even under the pointer.
    pager.set_selected_index(0);
    pager.handle_pointer({{desired.width * 0.5f, desired.height * 0.5f}, PointerButton::None, PointerAction::Move});
    REQUIRE_FALSE(pager.previous_button_visible());
}

TEST_CASE("An infinite pager grows its pips as the selection reaches the last one", "[pips-pager][infinite]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    PipsPagerControl pager; // -1 pages, 5 visible pips
    pager.set_next_button_visibility(PipsPagerButtonVisibility::Visible);
    pager.set_previous_button_visibility(PipsPagerButtonVisibility::Visible);
    const Size desired = pager.measure({1000.0f, Unbounded}, m, theme);
    pager.arrange({0, 0, desired.width, desired.height});

    REQUIRE(ui::automation_providers(pager).range_value->range_maximum() == 4.0); // max_visible_pips pips to start
    REQUIRE(pager.next_button_visible());                                         // never hidden at an edge
    REQUIRE_FALSE(pager.previous_button_visible());                               // and Previous never wraps

    ui::testing::set_focused(pager, true);
    for (int i = 0; i < 4; ++i)
    {
        pager.handle_key(key_down(Key::ArrowRight));
    }
    REQUIRE(pager.selected_index() == 4);
    REQUIRE(ui::automation_providers(pager).range_value->range_maximum() == 5.0); // reaching the last pip adds one past it

    pager.handle_key(key_down(Key::ArrowRight));
    REQUIRE(pager.selected_index() == 5);
    REQUIRE(ui::automation_providers(pager).range_value->range_maximum() == 6.0);

    pager.handle_key(key_down(Key::End)); // no last page to jump to
    REQUIRE(pager.selected_index() == 5);

    pager.set_selected_index(40);
    REQUIRE(pager.selected_index() == 40);
    REQUIRE(ui::automation_providers(pager).range_value->range_maximum() == 40.0); // a jump fills up to the selection only

    REQUIRE(pager.focusable());
    const Size again = pager.measure({1000.0f, Unbounded}, m, theme);
    REQUIRE_THAT(again.width, WithinAbs(static_cast<double>(desired.width), 1e-3));
}
