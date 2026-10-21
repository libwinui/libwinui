// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Brush.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Paint.hpp"
#include "winui/ui/collections/TableView.hpp"
#include "winui/ui/media/Shape.hpp"
#include "winui/ui/scrolling/Swipe.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <optional>
#include <variant>

#include <catch2/catch_test_macros.hpp>

using namespace ui;

namespace
{

constexpr Color Red   = Color::from_argb(0xFFFF0000);
constexpr Color Green = Color::from_argb(0xFF00FF00);
constexpr Color Blue  = Color::from_argb(0xFF0000FF);

const LinearGradientBrush Gradient{{GradientStop{0.0f, Red}, GradientStop{1.0f, Blue}}};

} // namespace

TEST_CASE("Paint holds a color, a brush, a theme color or nothing", "[paint]")
{
    const Theme theme = Theme::light();

    const Paint none;
    CHECK(none.empty());
    CHECK(Paint{std::nullopt}.empty());
    CHECK(Paint{std::optional<Color>{}}.empty());
    CHECK(Paint{std::optional<Brush>{}}.empty());
    CHECK_FALSE(none.resolve(theme).has_value());
    CHECK_FALSE(none.resolve_brush(theme).has_value());

    const Paint color = Red;
    CHECK(color.color() == Red);
    CHECK(color.resolve(theme) == Red);
    CHECK(color.resolve_brush(theme) == std::optional<Brush>{Brush{Red}});
    CHECK(Paint{std::optional<Color>{Red}} == color);
    CHECK(Paint{Brush{Red}} == color);

    const Paint themed = [](const Theme& t) { return t.colors().accent; };
    CHECK(themed.resource() != nullptr);
    CHECK_FALSE(themed.color().has_value());
    CHECK(themed.resolve(theme) == theme.colors().accent);
    CHECK(themed.resolve_brush(theme) == std::optional<Brush>{Brush{theme.colors().accent}});
}

TEST_CASE("A Paint holding a brush paints one color where one color is all a control draws", "[paint]")
{
    const Theme theme = Theme::light();

    const Paint gradient = Gradient;
    CHECK(gradient.brush() == std::optional<Brush>{Brush{Gradient}});
    CHECK(gradient.color() == Red); // the first stop
    CHECK(gradient.resolve(theme) == Red);
    CHECK(gradient.resolve_brush(theme) == std::optional<Brush>{Brush{Gradient}});

    const Paint radial = RadialGradientBrush{{GradientStop{0.0f, Blue}, GradientStop{1.0f, Red}}};
    CHECK(radial.color() == Blue);

    AcrylicBrush acrylic;
    acrylic.fallback_color = Green;
    CHECK(Paint{acrylic}.color() == Green);
}

TEST_CASE("A color field takes every kind of Paint as a value", "[paint][binding]")
{
    CHECK(Binding<Paint>{Red}.initial() == Paint{Red});
    CHECK(Binding<Paint>{Gradient}.initial() == Paint{Gradient});
    CHECK(Binding<Paint>{std::optional<Color>{Red}}.initial() == Paint{Red});
    CHECK(Binding<Paint>{std::nullopt}.initial().empty());

    Rectangle rect{.fill = colors::White, .stroke = Gradient};
    CHECK(rect.fill.initial() == Paint{colors::White});
    CHECK(rect.stroke.initial() == Paint{Gradient});

    TableView<int> table{.alternating_row_background = Red};
    CHECK(table.alternating_row_background.initial() == Paint{Red});

    SwipeItem item{.background = Red};
    CHECK(item.background == Paint{Red});
}

TEST_CASE("A binding of a color, a brush or an optional color fits a Paint field", "[paint][binding]")
{
    Property<Color> picked{Red};

    Binding<Paint> from_color = ui::bind(picked, [](Color c) { return c; });
    CHECK(from_color.initial() == Paint{Red});
    CHECK(from_color.reactive());

    Paint seen;
    auto  subscription = from_color.subscribe([&](Paint p) { seen = std::move(p); });
    picked             = Blue;
    CHECK(seen == Paint{Blue});

    Binding<Paint> plain = picked; // the property itself
    CHECK(plain.initial() == Paint{Blue});

    Property<std::optional<Color>> maybe{std::nullopt};
    Binding<Paint>                 from_optional = ui::bind(maybe);
    CHECK(from_optional.initial().empty());

    Property<bool> striped{false};
    Binding<Paint> from_brush = ui::bind(striped, [](bool on) { return on ? Brush{Gradient} : Brush{colors::White}; });
    CHECK(from_brush.initial() == Paint{colors::White});
    Paint brush_seen;
    auto  brush_subscription = from_brush.subscribe([&](Paint p) { brush_seen = std::move(p); });
    striped                  = true;
    CHECK(brush_seen == Paint{Gradient});

    Property<Paint> paint{Gradient};
    Rectangle       rect{.fill = paint};
    CHECK(rect.fill.initial() == Paint{Gradient});
}

TEST_CASE("A Shape paints a theme color under the theme it is drawn with", "[paint][shapes]")
{
    RectangleControl rect;
    rect.set_fill([](const Theme& t) { return t.colors().accent; });
    testing::HeadlessTextMeasurer measurer;
    (void)rect.measure(Size{20.0f, 20.0f}, measurer, Theme::default_light());
    rect.arrange(Rect{0.0f, 0.0f, 20.0f, 20.0f});

    const Theme           theme = Theme::light();
    testing::TestRenderer renderer;
    rect.render(renderer, theme);
    REQUIRE(renderer.count() == 1);
    const auto& cmd = std::get<DrawGeometry>(renderer.commands[0]);
    CHECK(cmd.fill == std::optional<Brush>{Brush{theme.colors().accent}});
    CHECK_FALSE(cmd.stroke.has_value());
}
