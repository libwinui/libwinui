// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Text.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/panels/Grid.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

TEST_CASE("Margin insets an element within the rect its parent affords it", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::dip(200.0f));
    grid.add_row(GridLength::dip(100.0f));

    TextControl* text = grid.add(std::make_unique<TextControl>("Hi"), 0, 0);
    text->set_margin(Symmetric{.horizontal = 10.0f, .vertical = 20.0f});

    (void)grid.measure({Unbounded, Unbounded}, m, theme);
    grid.arrange({0, 0, 200.0f, 100.0f});

    // Default alignment is Stretch, so the element fills the cell minus its margin.
    REQUIRE_THAT(text->bounds().left(), WithinAbs(10.0, 0.5));
    REQUIRE_THAT(text->bounds().top(), WithinAbs(20.0, 0.5));
    REQUIRE_THAT(text->bounds().width, WithinAbs(180.0, 0.5));
    REQUIRE_THAT(text->bounds().height, WithinAbs(60.0, 0.5));
}

TEST_CASE("MinWidth/MinHeight clamp desired size upward when natural content is smaller", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TextControl text{"Hi"}; // natural size is well under 120x32 at the default 14pt style
    text.set_min_width(120.0f);
    text.set_min_height(32.0f);

    const Size desired = text.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE_THAT(desired.width, WithinAbs(120.0, 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(32.0, 1e-3));
}

TEST_CASE("MinWidth/MinHeight have no effect when natural content is already larger", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TextControl text{"A much longer line of text than the minimum"};
    text.set_min_width(10.0f);
    text.set_min_height(1.0f);

    const Size natural = TextControl{"A much longer line of text than the minimum"}.measure({Unbounded, Unbounded}, m, theme);
    const Size desired = text.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE_THAT(desired.width, WithinAbs(natural.width, 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(natural.height, 1e-3));
}

namespace
{
TextControl* arrange_in_cell(GridControl& grid, const HeadlessTextMeasurer& m, const Theme& theme, Alignment h, Alignment v)
{
    grid.add_column(GridLength::dip(200.0f));
    grid.add_row(GridLength::dip(100.0f));
    TextControl* text = grid.add(std::make_unique<TextControl>("Hi"), 0, 0);
    text->set_horizontal_alignment(h);
    text->set_vertical_alignment(v);
    (void)grid.measure({Unbounded, Unbounded}, m, theme);
    grid.arrange({0, 0, 200.0f, 100.0f});
    return text;
}
} // namespace

TEST_CASE("Alignment::Stretch fills the full cell on both axes (the default)", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    GridControl          grid;

    TextControl* text = arrange_in_cell(grid, m, theme, Alignment::Stretch, Alignment::Stretch);
    REQUIRE_THAT(text->bounds().left(), WithinAbs(0.0, 0.5));
    REQUIRE_THAT(text->bounds().top(), WithinAbs(0.0, 0.5));
    REQUIRE_THAT(text->bounds().width, WithinAbs(200.0, 0.5));
    REQUIRE_THAT(text->bounds().height, WithinAbs(100.0, 0.5));
}

TEST_CASE("Alignment::Leading sizes to desired and pins to the cell's origin edge", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    GridControl          grid;

    const Size   desired = TextControl{"Hi"}.measure({Unbounded, Unbounded}, m, theme);
    TextControl* text    = arrange_in_cell(grid, m, theme, Alignment::Leading, Alignment::Leading);

    REQUIRE_THAT(text->bounds().left(), WithinAbs(0.0, 0.5));
    REQUIRE_THAT(text->bounds().top(), WithinAbs(0.0, 0.5));
    REQUIRE_THAT(text->bounds().width, WithinAbs(desired.width, 0.5));
    REQUIRE_THAT(text->bounds().height, WithinAbs(desired.height, 0.5));
}

TEST_CASE("Alignment::Trailing sizes to desired and pins to the cell's far edge", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    GridControl          grid;

    const Size   desired = TextControl{"Hi"}.measure({Unbounded, Unbounded}, m, theme);
    TextControl* text    = arrange_in_cell(grid, m, theme, Alignment::Trailing, Alignment::Trailing);

    REQUIRE_THAT(text->bounds().left(), WithinAbs(200.0 - desired.width, 0.5));
    REQUIRE_THAT(text->bounds().top(), WithinAbs(100.0 - desired.height, 0.5));
    REQUIRE_THAT(text->bounds().width, WithinAbs(desired.width, 0.5));
    REQUIRE_THAT(text->bounds().height, WithinAbs(desired.height, 0.5));
}

TEST_CASE("Alignment::Center sizes to desired and centers within the cell", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();
    GridControl          grid;

    const Size   desired = TextControl{"Hi"}.measure({Unbounded, Unbounded}, m, theme);
    TextControl* text    = arrange_in_cell(grid, m, theme, Alignment::Center, Alignment::Center);

    REQUIRE_THAT(text->bounds().left(), WithinAbs((200.0 - desired.width) * 0.5, 0.5));
    REQUIRE_THAT(text->bounds().top(), WithinAbs((100.0 - desired.height) * 0.5, 0.5));
    REQUIRE_THAT(text->bounds().width, WithinAbs(desired.width, 0.5));
    REQUIRE_THAT(text->bounds().height, WithinAbs(desired.height, 0.5));
}

TEST_CASE("Width/Height fix the desired size whatever the content wants", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TextControl text{"A label much wider than forty pixels"};
    text.set_width(40.0f);
    text.set_height(12.0f);
    const Size desired = text.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE_THAT(desired.width, WithinAbs(40.0, 1e-3));
    REQUIRE_THAT(desired.height, WithinAbs(12.0, 1e-3));

    text.set_width(std::nullopt);
    REQUIRE(text.measure({Unbounded, Unbounded}, m, theme).width > 40.0f);
}

TEST_CASE("min_width wins over width, and width over max_width", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TextControl text{"Hi"};
    text.set_width(50.0f);
    text.set_max_width(30.0f);
    REQUIRE_THAT(text.measure({Unbounded, Unbounded}, m, theme).width, WithinAbs(30.0, 1e-3));
    text.set_min_width(80.0f);
    REQUIRE_THAT(text.measure({Unbounded, Unbounded}, m, theme).width, WithinAbs(80.0, 1e-3));
}

TEST_CASE("max_width bounds an unbounded offer, so wrapping text wraps there", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    TextControl text{"This is some text that is too long and will wrap at the max width"};
    text.set_wrapping(TextWrapping::WrapWholeWords);
    const Size one_line = text.measure({Unbounded, Unbounded}, m, theme);

    text.set_max_width(120.0f);
    const Size wrapped = text.measure({Unbounded, Unbounded}, m, theme);
    REQUIRE(wrapped.width <= 120.0f + 1e-3f);
    REQUIRE(wrapped.height > one_line.height);
}

TEST_CASE("A stretched element with a Width is centered in a wider slot", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    GridControl grid;
    grid.add_column(GridLength::dip(200.0f));
    grid.add_row(GridLength::dip(100.0f));
    TextControl* text = grid.add(std::make_unique<TextControl>("Hi"), 0, 0);
    text->set_width(60.0f);
    text->set_max_height(40.0f);

    (void)grid.measure({Unbounded, Unbounded}, m, theme);
    grid.arrange({0, 0, 200.0f, 100.0f});

    REQUIRE_THAT(text->bounds().width, WithinAbs(60.0, 1e-3));
    REQUIRE_THAT(text->bounds().left(), WithinAbs(70.0, 1e-3));
    REQUIRE_THAT(text->bounds().height, WithinAbs(40.0, 1e-3));
    REQUIRE_THAT(text->bounds().top(), WithinAbs(30.0, 1e-3));

    text->set_horizontal_alignment(Alignment::Leading);
    (void)grid.measure({Unbounded, Unbounded}, m, theme);
    grid.arrange({0, 0, 200.0f, 100.0f});
    REQUIRE_THAT(text->bounds().left(), WithinAbs(0.0, 1e-3));
}

namespace
{
// Asks for a fixed size and remembers every room measure_override() was offered.
class OfferProbe final : public Element
{
public:
    mutable std::vector<Size> offers;

protected:
    [[nodiscard]] Size measure_override(Size available, const MeasureContext&) const override
    {
        offers.push_back(available);
        return Size{40.0f, 20.0f};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }
};
} // namespace

TEST_CASE("A margin that eats a bounded offer leaves no room, not unbounded room", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    OfferProbe probe;
    probe.set_margin(Thickness{10.0f});
    const Size desired = probe.measure({15.0f, Unbounded}, m, theme);

    REQUIRE(probe.offers.size() == 1);
    CHECK(probe.offers.back().width == 0.0f);
    CHECK(probe.offers.back().height == Unbounded);
    // The answer is the probe's own size plus the margin; clipping it is the parent's call.
    CHECK_THAT(desired.width, WithinAbs(60.0, 1e-3));
}

TEST_CASE("An unbounded axis stays unbounded through a margin", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    OfferProbe probe;
    probe.set_margin(Symmetric{.horizontal = 10.0f, .vertical = 5.0f});
    const Size desired = probe.measure({Unbounded, Unbounded}, m, theme);

    REQUIRE(probe.offers.size() == 1);
    CHECK(probe.offers.back().width == Unbounded);
    CHECK(probe.offers.back().height == Unbounded);
    CHECK_THAT(desired.width, WithinAbs(60.0, 1e-3));
    CHECK_THAT(desired.height, WithinAbs(30.0, 1e-3));
}

TEST_CASE("measure_override never sees a negative or NaN axis", "[sizing][layout]")
{
    HeadlessTextMeasurer m;
    Theme                theme = Theme::light();

    OfferProbe probe;
    probe.set_margin(Thickness{10.0f});
    (void)probe.measure({-50.0f, 5.0f}, m, theme);
    (void)probe.measure({std::numeric_limits<float>::quiet_NaN(), 100.0f}, m, theme);
    probe.set_max_width(30.0f);
    (void)probe.measure({Unbounded, 0.0f}, m, theme);

    REQUIRE(probe.offers.size() == 3);
    for (const Size& offer : probe.offers)
    {
        CHECK(offer.width >= 0.0f);
        CHECK(offer.height >= 0.0f);
    }
    CHECK(probe.offers[0].width == 0.0f);
    CHECK(probe.offers[1].width == 0.0f);
    CHECK(probe.offers[2].width == 30.0f); // an unbounded offer with a finite max is offered the max
    CHECK(probe.offers[2].height == 0.0f);
}
