// SPDX-License-Identifier: MIT
#include "ui/WindowHost.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/Describe.hpp"
#include "winui/render/IconGlyph.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Symbol.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/WindowStyle.hpp"
#include "winui/ui/containers/Border.hpp"
#include "winui/ui/containers/DensityScope.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/ComboBox.hpp"
#include "winui/ui/media/AnimatedVisuals.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/navigation/NavigationShell.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/TextBox.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{

Size measure(const Element& e, Size available, const Theme& theme = Theme::light())
{
    static HeadlessTextMeasurer measurer;
    return e.measure(available, measurer, theme);
}

void layout(Element& e, Size available, Rect slot)
{
    (void)measure(e, available);
    e.arrange(slot);
}

template <typename T>
std::vector<const T*> all_of(const TestRenderer& r)
{
    std::vector<const T*> out;
    for (const RenderCommand& c : r.commands)
    {
        if (const auto* x = std::get_if<T>(&c))
        {
            out.push_back(x);
        }
    }
    return out;
}

} // namespace

// ---- Icon ----

TEST_CASE("Icon: a glyph from its own font keeps it; a symbol-font glyph takes the theme's", "[icon][styles]")
{
    IconControl icon(IconGlyph{0xE790, "Segoe MDL2 Assets", 20.0f});
    CHECK(measure(icon, Size{Unbounded, Unbounded}) == Size{20.0f, 20.0f});
    icon.arrange(Rect{0.0f, 0.0f, 20.0f, 20.0f});
    TestRenderer r;
    icon.render(r, Theme::light());
    const auto runs = all_of<DrawTextRun>(r);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0]->style.font_family == "Segoe MDL2 Assets");
    CHECK(runs[0]->style.font_size == 20.0f);

    IconControl plain(IconGlyph{0xE790});
    layout(plain, Size{Unbounded, Unbounded}, Rect{0.0f, 0.0f, 12.0f, 12.0f});
    TestRenderer p;
    plain.render(p, Theme::light());
    CHECK(all_of<DrawTextRun>(p)[0]->style.font_family == std::string{Theme::light().icon_font_family});
}

TEST_CASE("Icon: size sets a glyph's font size and the square anything else fits into", "[icon][styles]")
{
    IconControl glyph(IconGlyph{0xE790});
    glyph.set_size(32.0f);
    CHECK(measure(glyph, Size{Unbounded, Unbounded}) == Size{32.0f, 32.0f});
    glyph.arrange(Rect{0.0f, 0.0f, 32.0f, 32.0f});
    TestRenderer g;
    glyph.render(g, Theme::light());
    CHECK(all_of<DrawTextRun>(g)[0]->style.font_size == 32.0f);

    IconControl picture(Bitmap{40, 20, colors::White});
    picture.set_size(10.0f);
    CHECK(measure(picture, Size{Unbounded, Unbounded}) == Size{10.0f, 10.0f});
    picture.arrange(Rect{0.0f, 0.0f, 10.0f, 10.0f});
    TestRenderer b;
    picture.render(b, Theme::light());
    const auto blits = all_of<DrawImageRect>(b);
    REQUIRE(blits.size() == 1);
    CHECK(blits[0]->bounds == Rect{0.0f, 2.5f, 10.0f, 5.0f});
}

TEST_CASE("Icon draws a Symbol's codepoint at 20 px", "[icon][styles]")
{
    CHECK(static_cast<char32_t>(Symbol::Accept) == 0xE10B);
    CHECK(static_cast<char32_t>(Symbol::Setting) == 0xE115);
    ui::Control<ui::Icon> icon;
    auto                  node = ui::build(ui::Icon{.source = Symbol::Find, .control = icon});
    REQUIRE(icon != nullptr);
    REQUIRE(icon->source().glyph() != nullptr);
    CHECK(icon->source().glyph()->codepoint == 0xE11A);
    CHECK(measure(*icon, Size{Unbounded, Unbounded}) == Size{20.0f, 20.0f});
}

TEST_CASE("Icon: a path measures to its far corner and fills in the foreground", "[icon][styles]")
{
    IconControl icon(IconSource::path(*PathGeometry::parse("F1 M 16,12 20,2L 20,16 1,16")));
    CHECK(measure(icon, Size{Unbounded, Unbounded}) == Size{20.0f, 16.0f});
    icon.arrange(Rect{10.0f, 10.0f, 20.0f, 16.0f});
    TestRenderer r;
    icon.render(r, Theme::light());
    const auto shapes = all_of<DrawGeometry>(r);
    REQUIRE(shapes.size() == 1);
    CHECK(shapes[0]->geometry.figures[0].start == Point{26.0f, 22.0f});
    CHECK(std::get<Color>(*shapes[0]->fill) == Theme::light().colors().text);

    icon.set_foreground(Color::from_rgba(255, 0, 0));
    TestRenderer red;
    icon.render(red, Theme::light());
    CHECK(std::get<Color>(*all_of<DrawGeometry>(red)[0]->fill) == Color::from_rgba(255, 0, 0));
}

TEST_CASE("Icon: a monochrome bitmap is a silhouette in the foreground, a plain one keeps its colors", "[icon][styles]")
{
    Bitmap picture(4, 2, Color::from_rgba(255, 0, 0));
    picture.set(0, 0, Color::from_rgba(0, 255, 0, 128)); // a half-transparent green pixel
    IconControl icon(IconSource::bitmap(picture));
    icon.set_foreground(Color::from_rgba(0, 0, 255));
    CHECK(measure(icon, Size{Unbounded, Unbounded}) == Size{4.0f, 2.0f});
    icon.arrange(Rect{0.0f, 0.0f, 40.0f, 20.0f});

    TestRenderer mono;
    icon.render(mono, Theme::light());
    const auto blits = all_of<DrawImageRect>(mono);
    REQUIRE(blits.size() == 1);
    CHECK(blits[0]->bounds == Rect{0.0f, 0.0f, 40.0f, 20.0f});

    icon.set_source(IconSource::bitmap(picture, false));
    TestRenderer color;
    icon.render(color, Theme::light());
    REQUIRE(all_of<DrawImageRect>(color).size() == 1);
    CHECK(all_of<DrawImageRect>(color)[0]->bounds == Rect{0.0f, 0.0f, 40.0f, 20.0f});
}

TEST_CASE("Icon takes an SVG picture and fits it Uniform", "[icon][styles]")
{
    IconControl icon(SvgImage{R"(<svg xmlns="http://www.w3.org/2000/svg" width="40" height="20"></svg>)"});
    layout(icon, Size{Unbounded, Unbounded}, Rect{0.0f, 0.0f, 40.0f, 40.0f});
    TestRenderer r;
    icon.render(r, Theme::light());
    const auto svgs = all_of<DrawSvg>(r);
    REQUIRE(svgs.size() == 1);
    CHECK(svgs[0]->bounds == Rect{0.0f, 10.0f, 40.0f, 20.0f});
}

// ---- NavigationShell animated icons ----

TEST_CASE("A NavigationShell item's AnimatedIcon follows the row's pointer state", "[navigation-view][animated-icon]")
{
    static const AnimatedSettingsVisualSource gear;
    ui::Control<ui::NavigationShell>          view;
    auto                                      node = ui::build(
        ui::NavigationShell{.pane_display_mode = NavigationShellPaneDisplayMode::Left,
                            .control           = view,
                            .items             = {
                                ui::NavItem{.text = "Game Settings", .icon = IconGlyph{0xE713}, .animated_icon = &gear}}});
    REQUIRE(view != nullptr);
    view->set_back_button_visible(AutoVisibility::Collapsed);
    view->set_pane_toggle_button_visible(false);
    view->set_settings_visible(false);
    layout(*view, Size{400.0f, 300.0f}, Rect{0.0f, 0.0f, 400.0f, 300.0f});

    // The row draws the animated visual (geometry), not the glyph, 16 px in its icon column.
    const auto icon_geometry = [&]
    {
        TestRenderer r;
        view->render(r, Theme::light());
        const auto shapes = all_of<DrawGeometry>(r);
        REQUIRE_FALSE(shapes.empty());
        return shapes.front()->geometry;
    };
    const PathGeometry rest = icon_geometry();
    CHECK(rest.bounds().width <= 16.5f);

    // Pressing the row moves the icon to Pressed: the gear turns (the press animation ends at -20
    // degrees; the stock visuals' hover transitions hold still).
    const Rect row = *view->item_bounds(0);
    view->handle_pointer({Point{row.x + 10.0f, row.y + 10.0f}, PointerButton::None, PointerAction::Move});
    view->handle_pointer({Point{row.x + 10.0f, row.y + 10.0f}, PointerButton::Left, PointerAction::Down});
    for (int i = 0; i < 10; ++i)
    {
        view->tick(50ms);
    }
    const PathGeometry pressed = icon_geometry();
    CHECK_FALSE(pressed == rest);

    // Settings has its own animated gear (AnimatedSettingsVisualSource) instead of a glyph.
    TestRenderer without_settings;
    view->render(without_settings, Theme::light());
    view->set_settings_visible(true);
    layout(*view, Size{400.0f, 300.0f}, Rect{0.0f, 0.0f, 400.0f, 300.0f});
    TestRenderer with_settings;
    view->render(with_settings, Theme::light());
    CHECK(with_settings.count_of<DrawGeometry>() > without_settings.count_of<DrawGeometry>());
    CHECK(with_settings.count_of<DrawTextRun>() == without_settings.count_of<DrawTextRun>() + 1); // its label only
}

// ---- shadow ----

TEST_CASE("A Border with a shadow draws the depth's shadow under itself", "[border][shadow][styles]")
{
    BorderControl border;
    border.set_background(colors::White);
    border.set_corner_radius(8.0f);
    border.set_width(200.0f);
    border.set_height(200.0f);
    layout(border, Size{Unbounded, Unbounded}, Rect{0.0f, 0.0f, 200.0f, 200.0f});

    TestRenderer none;
    border.render(none, Theme::light());
    CHECK(none.count() == 1); // just the background

    border.set_shadow(true);
    border.set_elevation(2.0f); // Control depth: casts nothing (below Z 4)
    TestRenderer flat;
    border.render(flat, Theme::light());
    CHECK(flat.count() == 1);

    border.set_elevation(32.0f);
    TestRenderer deep;
    border.render(deep, Theme::light());
    REQUIRE(deep.count() > 1);
    // The shadow comes first, growing past the border, and the background paints over it.
    const auto fills = all_of<FillRoundedRect>(deep);
    CHECK(fills.front()->bounds.width > 200.0f);
    CHECK(fills.back()->color == colors::White);
    CHECK(fills.back()->bounds == Rect{0.0f, 0.0f, 200.0f, 200.0f});
}

// ---- compact density ----

TEST_CASE("A DensityScope makes the text and combo controls under it compact", "[density][styles]")
{
    // Standard: a TextBox is at least 32 tall.
    TextBoxControl standard;
    CHECK(measure(standard, Size{200.0f, Unbounded}).height >= 32.0f);

    DensityScopeControl scope;
    TextBoxControl*     box    = scope.set_child(std::make_unique<TextBoxControl>());
    const Theme         theme  = Theme::light();
    const Theme&        inside = scope.scoped(theme);
    CHECK(inside.sizing.text_control_min_height == 24.0f);
    CHECK(inside.sizing.text_control_padding == Edges{.left = 2.0f, .top = 2.0f, .right = 6.0f, .bottom = 1.0f});
    (void)measure(scope, Size{200.0f, Unbounded});
    const float line = HeadlessTextMeasurer{}.measure("Ag", theme.text_style).height;
    CHECK_THAT(box->measure(Size{200.0f, Unbounded}, HeadlessTextMeasurer{}, inside).height,
               WithinAbs(std::max(24.0f, line + 3.0f), 1e-3));

    // ComboBox, read-only and editable: 12,1,0,3 of padding under a 24 floor.
    ComboBoxControl read_only(std::vector<std::string>{"One", "Two"});
    ComboBoxControl combo(std::vector<std::string>{"One", "Two"});
    combo.set_editable(true);
    const float compact_height = std::max(24.0f, HeadlessTextMeasurer{}.measure("", theme.text_style).height + 4.0f);
    CHECK_THAT(read_only.measure(Size{200.0f, Unbounded}, HeadlessTextMeasurer{}, inside).height,
               WithinAbs(compact_height, 1e-3));
    CHECK_THAT(combo.measure(Size{200.0f, Unbounded}, HeadlessTextMeasurer{}, inside).height,
               WithinAbs(compact_height, 1e-3));

    // Standard density keeps the theme itself.
    scope.set_density(Density::Standard);
    CHECK(&scope.scoped(theme) == &theme);
}

// At Standard density scoped() hands back its argument, which a temporary would leave dangling.
template <typename Base>
concept ScopesBase = requires(const DensityScopeControl& scope, Base&& base) { scope.scoped(std::forward<Base>(base)); };
static_assert(ScopesBase<const Theme&>);
static_assert(!ScopesBase<Theme>);

TEST_CASE("A DensityScope renders its child once", "[density][styles]")
{
    // render_override() has to call the base render_override(), not render(), or it recurses forever.
    ButtonControl alone{"Hello"};
    TestRenderer  expected;
    alone.render(expected);

    DensityScopeControl scope(Density::Standard);
    scope.set_child(std::make_unique<ButtonControl>("Hello"));
    TestRenderer r;
    scope.render(r);
    CHECK(r.count() == expected.count());
}

// ---- Button pointer events ----

TEST_CASE("Button raises pointer entered and exited as the pointer crosses it", "[button][input][styles]")
{
    ButtonControl button("Search");
    layout(button, Size{Unbounded, Unbounded}, Rect{0.0f, 0.0f, 80.0f, 32.0f});
    int entered = 0;
    int exited  = 0;
    button.on_pointer_entered([&] { ++entered; });
    button.on_pointer_exited([&] { ++exited; });

    button.handle_pointer({Point{10.0f, 10.0f}, PointerButton::None, PointerAction::Move});
    button.handle_pointer({Point{12.0f, 10.0f}, PointerButton::None, PointerAction::Move});
    CHECK(entered == 1);
    CHECK(exited == 0);
    button.handle_pointer({Point{200.0f, 10.0f}, PointerButton::None, PointerAction::Move});
    CHECK(exited == 1);
    button.handle_pointer({Point{10.0f, 10.0f}, PointerButton::None, PointerAction::Move});
    button.handle_pointer({Point{0.0f, 0.0f}, PointerButton::None, PointerAction::Leave});
    CHECK(entered == 2);
    CHECK(exited == 2);
}

// ---- backdrop ----

TEST_CASE("Window keeps the backdrop it was asked for, and hands it to the host", "[window][backdrop][styles]")
{
    class RecordingHost final : public WindowHost
    {
    public:
        void set_backdrop(Backdrop backdrop) override
        {
            last = backdrop;
        }
        Backdrop last = Backdrop::None;
    };

    Window window;
    CHECK(window.backdrop() == Backdrop::None);
    window.set_backdrop(Backdrop::MicaAlt); // no host yet: remembered
    CHECK(window.backdrop() == Backdrop::MicaAlt);

    RecordingHost host;
    seams(window).set_window_host(&host);
    window.set_backdrop(Backdrop::Acrylic);
    CHECK(host.last == Backdrop::Acrylic);
    seams(window).set_window_host(nullptr);
}
