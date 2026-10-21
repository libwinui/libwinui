// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/core/NumberFormat.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestClipboard.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/VisualStates.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/NumberBox.hpp"
#include "winui/ui/themes/Theme.hpp"
#include "winui/ui/themes/ThemeSource.hpp"

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

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

// The spin buttons are two 32px columns at the right edge (each at least 32 wide),
// up left of down.
Point up_point(const NumberBoxControl& n)
{
    const Rect b = n.bounds();
    return Point{b.right() - 48.0f, b.center().y};
}
Point down_point(const NumberBoxControl& n)
{
    const Rect b = n.bounds();
    return Point{b.right() - 16.0f, b.center().y};
}

// Heap-allocated because the constructor parents its TextBox to `this`, so a by-value move
// would leave that pointer dangling.
std::unique_ptr<NumberBoxControl> make_number_box(double value = 0.0,
                                                  double min   = -std::numeric_limits<double>::infinity(),
                                                  double max   = std::numeric_limits<double>::infinity())
{
    auto n = std::make_unique<NumberBoxControl>(value, min, max);
    // The default placement (Hidden) draws no spin buttons.
    n->set_spin_button_placement(NumberBoxSpinButtonPlacementMode::Inline);
    HeadlessTextMeasurer measurer;
    const Size           sized = n->measure({Unbounded, Unbounded}, measurer, Theme::default_light());
    n->arrange({100.0f, 100.0f, sized.width, sized.height});
    return n;
}
} // namespace

TEST_CASE("A new NumberBox defaults to no value (empty), an unbounded range, enabled, and focusable", "[number_box]")
{
    NumberBoxControl n;
    REQUIRE(n.enabled());
    REQUIRE(n.focusable());
    REQUIRE_FALSE(n.value().has_value());
    REQUIRE(n.text().empty());
}

TEST_CASE("Constructing with an explicit value clamps it into [min, max]", "[number_box]")
{
    NumberBoxControl under{-5.0, 0.0, 10.0};
    REQUIRE_THAT(under.value().value(), WithinAbs(0.0, 1e-9));
    REQUIRE(under.text() == "0");

    NumberBoxControl over{50.0, 0.0, 10.0};
    REQUIRE_THAT(over.value().value(), WithinAbs(10.0, 1e-9));
    REQUIRE(over.text() == "10");
}

TEST_CASE("set_value clamps, reformats the text, and fires on_value_changed only when it actually moves",
          "[number_box][command]")
{
    NumberBoxControl    n{5.0, 0.0, 10.0};
    std::vector<double> observed;
    n.on_value_changed([&](std::optional<double> v) { observed.push_back(v.value()); });

    n.set_value(7.5);
    REQUIRE_THAT(n.value().value(), WithinAbs(7.5, 1e-9));
    REQUIRE(n.text() == "7.5");

    n.set_value(50.0); // clamped to max
    REQUIRE_THAT(n.value().value(), WithinAbs(10.0, 1e-9));

    n.set_value(10.0); // unchanged -> no callback, but text still resynced
    REQUIRE(observed == std::vector<double>{7.5, 10.0});
}

TEST_CASE("set_range re-clamps the current value", "[number_box][command]")
{
    NumberBoxControl n{8.0, 0.0, 10.0};

    std::vector<double> observed;
    n.on_value_changed([&](std::optional<double> v) { observed.push_back(v.value()); });

    n.set_range(0.0, 5.0);
    REQUIRE_THAT(n.value().value(), WithinAbs(5.0, 1e-9));
    REQUIRE(observed == std::vector<double>{5.0});
}

TEST_CASE("Ctrl+V reaches the internal TextBox through NumberBox", "[number_box][clipboard]")
{
    auto n = make_number_box(0.0, 0.0, 100.0);
    ui::testing::set_focused(*n, true);
    n->set_text("");

    TestClipboard clipboard;
    clipboard.set_text("42");
    ui::testing::set_clipboard(&clipboard);
    n->handle_key({Key::V, KeyAction::Down, {.ctrl = true}});

    REQUIRE(n->text() == "42"); // pasted text is uncommitted until Enter, same as typing
}

TEST_CASE("Typing a valid number then pressing Enter commits it as the new value", "[number_box][command]")
{
    auto n = make_number_box(0.0, 0.0, 100.0);
    ui::testing::set_focused(*n, true);
    n->set_text("42");

    n->handle_key({Key::Enter, KeyAction::Down});

    REQUIRE_THAT(n->value().value(), WithinAbs(42.0, 1e-9));
    REQUIRE(n->text() == "42");
}

TEST_CASE("Typing an unparsable value then pressing Enter reverts the text without changing value",
          "[number_box][command]")
{
    auto n = make_number_box(3.0, 0.0, 100.0);
    ui::testing::set_focused(*n, true);
    n->set_text("not a number");
    int fired = 0;
    n->on_value_changed([&](std::optional<double>) { ++fired; });

    n->handle_key({Key::Enter, KeyAction::Down});

    REQUIRE_THAT(n->value().value(), WithinAbs(3.0, 1e-9));
    REQUIRE(n->text() == "3");
    REQUIRE(fired == 0);
}

TEST_CASE("Losing focus commits pending typed text, same as Enter", "[number_box][state]")
{
    auto n = make_number_box(0.0, 0.0, 100.0);
    ui::testing::set_focused(*n, true);
    n->set_text("17");

    ui::testing::set_focused(*n, false);

    REQUIRE_THAT(n->value().value(), WithinAbs(17.0, 1e-9));
}

TEST_CASE("Clicking the up/down spin buttons steps the value and updates the text", "[number_box][command]")
{
    auto n = make_number_box(5.0, 0.0, 10.0);
    n->set_step(2.0);

    n->handle_pointer(down(up_point(*n)));
    n->handle_pointer(up(up_point(*n)));
    REQUIRE_THAT(n->value().value(), WithinAbs(7.0, 1e-9));
    REQUIRE(n->text() == "7");

    n->handle_pointer(down(down_point(*n)));
    n->handle_pointer(up(down_point(*n)));
    REQUIRE_THAT(n->value().value(), WithinAbs(5.0, 1e-9));
}

TEST_CASE("Spin buttons auto-disable at their own end of the range", "[number_box][state]")
{
    auto n = make_number_box(10.0, 0.0, 10.0); // starts at max
    REQUIRE(ui::testing::up_visual_state(*n) == ButtonState::Disabled);
    REQUIRE(ui::testing::down_visual_state(*n) != ButtonState::Disabled);

    n->handle_pointer(down(down_point(*n)));
    n->handle_pointer(up(down_point(*n)));
    for (int i = 0; i < 9; ++i) // step down to min (default step 1)
    {
        n->handle_pointer(down(down_point(*n)));
        n->handle_pointer(up(down_point(*n)));
    }
    REQUIRE_THAT(n->value().value(), WithinAbs(0.0, 1e-9));
    REQUIRE(ui::testing::down_visual_state(*n) == ButtonState::Disabled);
}

TEST_CASE("ArrowUp/ArrowDown step the value while focused; ignored while not focused", "[number_box][command]")
{
    auto n = make_number_box(5.0, 0.0, 10.0);
    n->set_step(2.0);

    n->handle_key({Key::ArrowUp, KeyAction::Down}); // not focused yet
    REQUIRE_THAT(n->value().value(), WithinAbs(5.0, 1e-9));

    ui::testing::set_focused(*n, true);
    n->handle_key({Key::ArrowUp, KeyAction::Down});
    REQUIRE_THAT(n->value().value(), WithinAbs(7.0, 1e-9));

    n->handle_key({Key::ArrowDown, KeyAction::Down});
    n->handle_key({Key::ArrowDown, KeyAction::Down});
    REQUIRE_THAT(n->value().value(), WithinAbs(3.0, 1e-9));
}

TEST_CASE("A step starts from whatever is currently typed, not the last committed value", "[number_box][command]")
{
    auto n = make_number_box(0.0, 0.0, 100.0);
    ui::testing::set_focused(*n, true);
    n->set_text("7"); // typed, not yet committed

    n->handle_key({Key::ArrowUp, KeyAction::Down});

    REQUIRE_THAT(n->value().value(), WithinAbs(8.0, 1e-9));
}

TEST_CASE("Holding a spin button down repeats the step after a delay, then at a faster interval",
          "[number_box][command][tick]")
{
    auto n = make_number_box(0.0, 0.0, 100.0);
    n->set_step(1.0);

    n->handle_pointer(down(up_point(*n))); // immediate first step
    REQUIRE_THAT(n->value().value(), WithinAbs(1.0, 1e-9));

    n->tick(400ms); // short of the initial repeat delay
    REQUIRE_THAT(n->value().value(), WithinAbs(1.0, 1e-9));

    n->tick(200ms); // crosses the initial delay -> one repeat
    REQUIRE_THAT(n->value().value(), WithinAbs(2.0, 1e-9));

    n->tick(100ms); // crosses the faster steady interval -> another repeat
    REQUIRE_THAT(n->value().value(), WithinAbs(3.0, 1e-9));

    n->handle_pointer(up(up_point(*n)));
    n->tick(1000ms); // released -> no further repeats
    REQUIRE_THAT(n->value().value(), WithinAbs(3.0, 1e-9));
}

TEST_CASE("set_step(0) disables both spin-button and arrow-key stepping", "[number_box][command]")
{
    auto n = make_number_box(5.0, 0.0, 10.0);
    n->set_step(0.0);
    ui::testing::set_focused(*n, true);

    n->handle_key({Key::ArrowUp, KeyAction::Down});
    n->handle_pointer(down(up_point(*n)));
    n->handle_pointer(up(up_point(*n)));

    REQUIRE_THAT(n->value().value(), WithinAbs(5.0, 1e-9));
}

TEST_CASE("Disabled NumberBox ignores pointer and keyboard input", "[number_box][state]")
{
    auto n = make_number_box(5.0, 0.0, 10.0);
    n->set_enabled(false);

    n->handle_pointer(down(up_point(*n)));
    n->handle_pointer(up(up_point(*n)));
    ui::testing::set_focused(*n, true);
    n->handle_key({Key::ArrowUp, KeyAction::Down});

    REQUIRE_THAT(n->value().value(), WithinAbs(5.0, 1e-9));
    REQUIRE_FALSE(n->focusable());
    REQUIRE(ui::testing::up_visual_state(*n) == ButtonState::Disabled);
    REQUIRE(ui::testing::down_visual_state(*n) == ButtonState::Disabled);
}

TEST_CASE("NumberBoxSpinButtonPlacementMode is Hidden by default: no spin columns, TextBox's minimum width",
          "[number_box][placement]")
{
    auto                 n = std::make_unique<NumberBoxControl>(5.0, 0.0, 10.0);
    HeadlessTextMeasurer m;
    REQUIRE(n->spin_button_placement() == NumberBoxSpinButtonPlacementMode::Hidden);
    const Size hidden = n->measure({Unbounded, Unbounded}, m, Theme::default_light());
    REQUIRE_THAT(hidden.width, WithinAbs(64.0, 1e-3)); // the text field's minimum width

    n->set_spin_button_placement(NumberBoxSpinButtonPlacementMode::Inline);
    const Size inline_size = n->measure({Unbounded, Unbounded}, m, Theme::default_light());
    REQUIRE(inline_size.width >= 120.0f); // the minimum width, only with inline spin buttons

    n->set_spin_button_placement(NumberBoxSpinButtonPlacementMode::Hidden);
    const Size size = n->measure({Unbounded, Unbounded}, m, Theme::default_light());
    n->arrange({0, 0, 200, size.height});
    n->handle_pointer(down({190, size.height * 0.5f}));
    n->handle_pointer(up({190, size.height * 0.5f}));
    REQUIRE_THAT(n->value().value(), WithinAbs(5.0, 1e-9));
}

TEST_CASE("PageUp/PageDown step by large_step; the wheel steps while focused", "[number_box][command]")
{
    auto n = make_number_box(50.0, 0.0, 100.0);
    REQUIRE_THAT(n->large_step(), WithinAbs(10.0, 1e-9)); // the default
    ui::testing::set_focused(*n, true);
    n->handle_key({Key::PageUp, KeyAction::Down});
    REQUIRE_THAT(n->value().value(), WithinAbs(60.0, 1e-9));
    n->set_large_step(25.0);
    n->handle_key({Key::PageDown, KeyAction::Down});
    REQUIRE_THAT(n->value().value(), WithinAbs(35.0, 1e-9));

    const Point inside = n->bounds().center();
    REQUIRE(n->handle_wheel({inside, PointerButton::None, PointerAction::Wheel, -48.0f})); // away from the user: up
    REQUIRE_THAT(n->value().value(), WithinAbs(36.0, 1e-9));
    REQUIRE(n->handle_wheel({inside, PointerButton::None, PointerAction::Wheel, 48.0f}));
    REQUIRE_THAT(n->value().value(), WithinAbs(35.0, 1e-9));

    ui::testing::set_focused(*n, false);
    REQUIRE_FALSE(n->handle_wheel({inside, PointerButton::None, PointerAction::Wheel, 48.0f})); // unfocused: scrolls the page
    REQUIRE_THAT(n->value().value(), WithinAbs(35.0, 1e-9));
}

TEST_CASE("Compact opens a spin-button popup while focused, beside the box's top-right corner",
          "[number_box][placement]")
{
    auto                 n = std::make_unique<NumberBoxControl>(5.0, 0.0, 10.0);
    HeadlessTextMeasurer m;
    n->set_spin_button_placement(NumberBoxSpinButtonPlacementMode::Compact);
    const Size size = n->measure({Unbounded, Unbounded}, m, Theme::default_light());
    n->arrange({100, 100, size.width, size.height});

    REQUIRE_FALSE(ui::testing::popup_open(*n));
    ui::testing::set_focused(*n, true);
    REQUIRE(ui::testing::popup_open(*n));
    REQUIRE_FALSE(ui::testing::ElementAccess::popup_request(*n)->light_dismiss);

    Element* popup = ui::testing::popup_content(*n);
    REQUIRE(ui::testing::ElementAccess::place_popup(*n, *popup, {800, 600}, ui::MeasureContext{m, Theme::light()}));
    // The popup sits -21 across and -27 down; 36px buttons 4 apart in 6px of
    // padding inside a 1px border.
    REQUIRE(popup->bounds() == Rect{100 + size.width - 21, 73, 50, 90});

    const Point up_button{popup->bounds().x + 25, popup->bounds().y + 25};
    popup->handle_pointer({up_button, PointerButton::None, PointerAction::Move});
    popup->handle_pointer(down(up_button));
    REQUIRE_THAT(n->value().value(), WithinAbs(6.0, 1e-9));
    n->tick(600ms);
    REQUIRE_THAT(n->value().value(), WithinAbs(7.0, 1e-9));
    popup->handle_pointer(up(up_button));
    n->tick(600ms);
    REQUIRE_THAT(n->value().value(), WithinAbs(7.0, 1e-9));

    const Point down_button{popup->bounds().x + 25, popup->bounds().y + 65};
    popup->handle_pointer(down(down_button));
    popup->handle_pointer(up(down_button));
    REQUIRE_THAT(n->value().value(), WithinAbs(6.0, 1e-9));

    ui::testing::set_focused(*n, false);
    REQUIRE_FALSE(ui::testing::popup_open(*n));
}

// ---- no value, accepts_expression, number_formatter, validation, wrapping ----

namespace
{
void commit(NumberBoxControl& n, const std::string& text)
{
    ui::testing::set_focused(n, true);
    n.set_text(text);
    n.handle_key(KeyEvent{Key::Enter, KeyAction::Down});
}
} // namespace

TEST_CASE("Clearing a NumberBox commits no value, and no value disables stepping", "[number_box]")
{
    NumberBoxControl      n{5.0};
    std::optional<double> seen{0.0};
    n.on_value_changed([&](std::optional<double> v) { seen = v; });
    commit(n, "    ");
    REQUIRE_FALSE(n.value().has_value());
    REQUIRE_FALSE(seen.has_value());
    n.handle_key(KeyEvent{Key::ArrowUp, KeyAction::Down});
    REQUIRE_FALSE(n.value().has_value());

    n.set_value(std::numeric_limits<double>::quiet_NaN()); // a NaN from the app is no number too
    REQUIRE_FALSE(n.value().has_value());
    REQUIRE(n.text().empty());
    n.set_placeholder_text("1 + 2^2");
    REQUIRE(n.placeholder_text() == "1 + 2^2");
}

TEST_CASE("accepts_expression evaluates what is typed, with precedence and parentheses", "[number_box]")
{
    NumberBoxControl n;
    n.set_accepts_expression(true);
    commit(n, "1 + 2^2");
    REQUIRE(n.value() == 5.0);
    REQUIRE(n.text() == "5");
    commit(n, "(1 + 2) * -3");
    REQUIRE(n.value() == -9.0);
    commit(n, "2 ^ 3 ^ 2"); // left-associative
    REQUIRE(n.value() == 64.0);
    commit(n, "1 +");
    REQUIRE(n.value() == 64.0); // invalid: the text goes back to the value
    REQUIRE(n.text() == "64");

    NumberBoxControl plain;
    commit(plain, "1 + 2");
    REQUIRE_FALSE(plain.value().has_value());
}

TEST_CASE("evaluate_expression divides, rejects malformed input and handles signs", "[number_box]")
{
    const DecimalFormatter f;
    REQUIRE(evaluate_expression("10 / 4", f) == 2.5);
    REQUIRE(std::isnan(*evaluate_expression("1 / 0", f)));
    REQUIRE_FALSE(evaluate_expression("(1 + 2", f).has_value());
    REQUIRE_FALSE(evaluate_expression("1 2", f).has_value());
    REQUIRE(evaluate_expression("-2 * -2", f) == 4.0);
}

TEST_CASE("A DecimalFormatter pads digits and rounds by increment", "[number_box]")
{
    DecimalFormatter dollars;
    dollars.integer_digits  = 1;
    dollars.fraction_digits = 2;
    dollars.rounder         = IncrementNumberRounder{0.25, RoundingAlgorithm::RoundHalfUp};
    REQUIRE(dollars.format(3.14) == "3.25");
    REQUIRE(dollars.format(3.0) == "3.00");
    REQUIRE(dollars.format(0.1) == "0.00");
    REQUIRE(dollars.format(-0.1) == "0.00"); // no signed zero

    DecimalFormatter zoom;
    zoom.integer_digits  = 2;
    zoom.fraction_digits = 1;
    zoom.rounder         = IncrementNumberRounder{0.1, RoundingAlgorithm::RoundHalfUp};
    REQUIRE(zoom.format(4.0) == "04.0");
    REQUIRE(zoom.format(0.25) == "00.3");

    DecimalFormatter grouped;
    grouped.is_grouped = true;
    REQUIRE(grouped.format(1234567.0) == "1,234,567");
    REQUIRE(grouped.parse(" 1,234 ") == 1234.0);
    REQUIRE_FALSE(DecimalFormatter{}.parse("12x").has_value());

    REQUIRE(IncrementNumberRounder{1.0, RoundingAlgorithm::RoundHalfToEven}.round(2.5) == 2.0);
    REQUIRE(IncrementNumberRounder{1.0, RoundingAlgorithm::RoundHalfToOdd}.round(2.5) == 3.0);
    REQUIRE(IncrementNumberRounder{1.0, RoundingAlgorithm::RoundHalfDown}.round(2.5) == 2.0);
    REQUIRE(IncrementNumberRounder{1.0, RoundingAlgorithm::RoundTowardsZero}.round(-2.7) == -2.0);
    REQUIRE(IncrementNumberRounder{1.0, RoundingAlgorithm::RoundAwayFromZero}.round(-2.2) == -3.0);
}

TEST_CASE("NumberBox formats through its number_formatter and hides floating-point noise", "[number_box]")
{
    NumberBoxControl n{0.1 + 0.2};
    REQUIRE(n.text() == "0.3");

    DecimalFormatter dollars;
    dollars.fraction_digits = 2;
    dollars.rounder         = IncrementNumberRounder{0.25, RoundingAlgorithm::RoundHalfUp};
    n.set_number_formatter(dollars);
    REQUIRE(n.text() == "0.25");
    commit(n, "3.14");
    REQUIRE(n.text() == "3.25");
    REQUIRE(n.value() == 3.14); // the rounder shapes the text, not the value
}

TEST_CASE("validation_mode Disabled keeps out-of-range values, and wrapping steps round", "[number_box]")
{
    NumberBoxControl n{5.0, 0.0, 10.0};
    n.set_validation_mode(NumberBoxValidationMode::Disabled);
    commit(n, "42");
    REQUIRE(n.value() == 42.0);
    commit(n, "nonsense");
    REQUIRE(n.text() == "nonsense");

    NumberBoxControl w{10.0, 0.0, 10.0};
    w.set_wrap_enabled(true);
    ui::testing::set_focused(w, true);
    w.handle_key(KeyEvent{Key::ArrowUp, KeyAction::Down});
    REQUIRE(w.value() == 0.0);
    w.handle_key(KeyEvent{Key::ArrowDown, KeyAction::Down});
    REQUIRE(w.value() == 10.0);
}

namespace
{
// Sets the regional format for one test and puts the invariant one back after it.
struct RegionalScope
{
    explicit RegionalScope(NumberFormatSymbols symbols)
    {
        set_regional_number_format(std::move(symbols));
    }
    ~RegionalScope()
    {
        set_regional_number_format(NumberFormatSymbols::invariant());
    }
    RegionalScope(const RegionalScope&)            = delete;
    RegionalScope& operator=(const RegionalScope&) = delete;
};

NumberFormatSymbols german()
{
    return {.decimal_separator = ",", .grouping_separator = "."};
}
} // namespace

TEST_CASE("A DecimalFormatter without symbols writes and reads the regional format", "[number_box][locale]")
{
    DecimalFormatter f{.fraction_digits = 2, .is_grouped = true};
    REQUIRE(f.format(1234567.5) == "1,234,567.50"); // invariant until a host says otherwise
    {
        RegionalScope de{german()};
        REQUIRE(f.format(1234567.5) == "1.234.567,50");
        REQUIRE(f.parse("1.234,5") == 1234.5);
        REQUIRE(f.parse("1,5") == 1.5);
        REQUIRE_FALSE(f.parse("1,5,0").has_value());                  // one decimal separator at most
        REQUIRE_FALSE(DecimalFormatter{}.parse("1.234").has_value()); // not grouped: '.' means nothing

        const DecimalFormatter pinned{.symbols = NumberFormatSymbols::invariant()};
        REQUIRE(pinned.format(1.5) == "1.5");
        REQUIRE(pinned.parse("1.5") == 1.5);
    }
    REQUIRE(f.format(1.5) == "1.50");
}

TEST_CASE("Grouping follows the locale's group sizes and multi-character separators", "[number_box][locale]")
{
    const std::string narrow_nbsp = encode_utf8(0x202F);
    DecimalFormatter  f{.is_grouped = true};
    f.symbols = NumberFormatSymbols{.decimal_separator = ",", .grouping_separator = narrow_nbsp}; // fr-FR
    REQUIRE(f.format(1234567.0) == "1" + narrow_nbsp + "234" + narrow_nbsp + "567");
    REQUIRE(f.parse("1" + narrow_nbsp + "234,5") == 1234.5);

    f.symbols = NumberFormatSymbols{.group_sizes = {3, 2}}; // hi-IN: 12,34,567
    REQUIRE(f.format(1234567.0) == "12,34,567");
    f.symbols = NumberFormatSymbols{.group_sizes = {3}, .repeat_last_group = false}; // LOCALE_SGROUPING "3"
    REQUIRE(f.format(1234567.0) == "1234,567");
    REQUIRE(f.format(-1234.0) == "-1,234");
}

TEST_CASE("A NumberBox on the default formatter follows the regional format; expressions read it too",
          "[number_box][locale]")
{
    NumberBoxControl box{1.5};
    REQUIRE(box.text() == "1.5");
    {
        RegionalScope de{german()};
        REQUIRE(box.text() == "1,5"); // re-shown when the host learns the user's locale

        box.set_accepts_expression(true);
        ui::testing::set_focused(box, true);
        box.set_text("1,5 + 2");
        box.handle_key({Key::Enter, KeyAction::Down});
        REQUIRE_THAT(box.value().value(), WithinAbs(3.5, 1e-9));
        REQUIRE(box.text() == "3,5");

        box.set_number_formatter(DecimalFormatter{.symbols = NumberFormatSymbols::invariant()});
        REQUIRE(box.text() == "3.5");
    }
    REQUIRE(box.text() == "3.5");
}

TEST_CASE("Aero stacks the inline spin buttons inside the field's right edge", "[number_box][placement][aero]")
{
    const Theme          aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    auto                 n    = std::make_unique<NumberBoxControl>(5.0, 0.0, 10.0);
    HeadlessTextMeasurer m;
    n->set_spin_button_placement(NumberBoxSpinButtonPlacementMode::Inline);
    const Size size = n->measure({200, Unbounded}, m, aero);
    n->arrange({0, 0, 200, size.height});
    const Rect b = n->bounds();

    // The 17 wide column sits just inside the border: its upper half steps up, its lower down.
    const float column = b.right() - 1.0f - 8.5f;
    n->handle_pointer(down({column, b.top() + b.height * 0.25f}));
    n->handle_pointer(up({column, b.top() + b.height * 0.25f}));
    REQUIRE_THAT(n->value().value(), WithinAbs(6.0, 1e-9));
    n->handle_pointer(down({column, b.top() + b.height * 0.75f}));
    n->handle_pointer(up({column, b.top() + b.height * 0.75f}));
    REQUIRE_THAT(n->value().value(), WithinAbs(5.0, 1e-9));
    // Where Fluent's up column would be, the field takes the click.
    n->handle_pointer(down({b.right() - 50.0f, b.center().y}));
    n->handle_pointer(up({b.right() - 50.0f, b.center().y}));
    REQUIRE_THAT(n->value().value(), WithinAbs(5.0, 1e-9));

    // Each half is a button face with a 7 x 4 triangle.
    TestRenderer r;
    n->render(r, aero);
    int triangles = 0;
    for (const RenderCommand& command : r.commands)
    {
        if (const auto* polygon = std::get_if<FillPolygon>(&command); polygon != nullptr && polygon->points.size() == 3)
        {
            ++triangles;
            CHECK(polygon->points[0].x >= b.right() - 18.0f);
        }
    }
    CHECK(triangles == 2);
}

TEST_CASE("Without a spin-button popup, Compact shows the inline buttons", "[number_box][placement][aero]")
{
    const Theme          aero = resolve_theme(ThemeRequest{.variant = ThemeVariant::Aero}, OsThemeSnapshot{});
    auto                 n    = std::make_unique<NumberBoxControl>(5.0, 0.0, 10.0);
    HeadlessTextMeasurer m;
    n->set_spin_button_placement(NumberBoxSpinButtonPlacementMode::Compact);
    const Size size = n->measure({200, Unbounded}, m, aero);
    n->arrange({0, 0, 200, size.height});
    ui::testing::set_focused(*n, true);
    CHECK_FALSE(ui::testing::popup_open(*n));

    const Rect  b      = n->bounds();
    const float column = b.right() - 1.0f - 8.5f;
    n->handle_pointer(down({column, b.top() + b.height * 0.25f}));
    n->handle_pointer(up({column, b.top() + b.height * 0.25f}));
    REQUIRE_THAT(n->value().value(), WithinAbs(6.0, 1e-9));
    CHECK(n->spin_button_placement() == NumberBoxSpinButtonPlacementMode::Compact);
}

TEST_CASE("A two-way value the NumberBox clamps is written back, at build and when already clamped",
          "[number_box][spec][binding]")
{
    Property<std::optional<double>> v{150.0};
    int                             handled = 0;
    ui::Control<ui::NumberBox>      box;
    std::unique_ptr<Element>        node = ui::build(
        ui::NumberBox{.value            = ui::bind(v),
                      .min              = 0.0,
                      .max              = 100.0,
                      .on_value_changed = [&](std::optional<double>) { ++handled; },
                      .control          = box});
    CHECK(box->value() == 100.0);
    CHECK(v.get() == 100.0); // the build-time clamp went back
    CHECK(handled == 0);     // seeding never runs the spec's handler

    v = 250.0; // the control already holds 100: nothing changes there
    CHECK(box->value() == 100.0);
    CHECK(v.get() == 100.0);

    v = -3.0; // a real change, clamped
    CHECK(box->value() == 0.0);
    CHECK(v.get() == 0.0);

    v = std::nullopt; // the empty box is not clamped and not written
    CHECK_FALSE(box->value().has_value());
    CHECK_FALSE(v.get().has_value());
}
