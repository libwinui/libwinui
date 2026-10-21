// SPDX-License-Identifier: MIT
// Every control that takes an icon draws every kind of IconSource: a symbol, a glyph, a path, a
// monochrome bitmap and a picture, each inside the room the control gives a glyph.
#include "winui/core/Geometry.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/render/IconSource.hpp"
#include "winui/render/PathGeometry.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/render/Symbol.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/TeachingTip.hpp"
#include "winui/ui/hosting/TitleBar.hpp"
#include "winui/ui/input/ToggleSwitch.hpp"
#include "winui/ui/media/AnimatedIcon.hpp"
#include "winui/ui/media/Icon.hpp"
#include "winui/ui/media/InkToolbar.hpp"
#include "winui/ui/media/PersonPicture.hpp"
#include "winui/ui/menus/AppBarButton.hpp"
#include "winui/ui/navigation/NavigationShell.hpp"
#include "winui/ui/navigation/SelectorBar.hpp"
#include "winui/ui/navigation/Tabs.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/status/InfoBadge.hpp"
#include "winui/ui/text/AutoSuggestBox.hpp"

#include <algorithm>
#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

namespace
{

constexpr char32_t SymbolCode = static_cast<char32_t>(Symbol::Find);

// Renders a window whose content `make` builds around `icon`, laid out at 800x600.
using Scene = std::function<std::unique_ptr<Element>(IconSource icon, Window& window)>;

TestRenderer render(const Scene& make, IconSource icon)
{
    HeadlessTextMeasurer measurer;
    Window               window;
    window.set_stretch_content(false);
    window.set_content(make(std::move(icon), window));
    window.layout(Size{800.0f, 600.0f}, measurer);
    window.layout(Size{800.0f, 600.0f}, measurer); // a popup opened in the first pass lays out now
    TestRenderer r;
    window.render(r);
    return r;
}

template <typename T>
std::vector<Rect> boxes(const TestRenderer& r)
{
    std::vector<Rect> out;
    for (const auto& c : r.commands)
    {
        if (const auto* d = std::get_if<T>(&c))
        {
            if constexpr (std::same_as<T, DrawGeometry>)
            {
                out.push_back(d->geometry.bounds());
            }
            else
            {
                out.push_back(d->bounds);
            }
        }
    }
    return out;
}

bool inside(const Rect& r, const Rect& slot)
{
    constexpr float Slack = 0.5f;
    return r.left() >= slot.left() - Slack && r.top() >= slot.top() - Slack && r.right() <= slot.right() + Slack &&
           r.bottom() <= slot.bottom() + Slack && r.width > 0.0f && r.height > 0.0f;
}

// The slot: the square around the symbol's glyph run. Every other kind must land in it, as a
// command the control did not draw without an icon.
template <typename T>
void check_kind(const Scene& make, const IconSource& icon, const Rect& slot, const char* kind)
{
    INFO(kind);
    const std::vector<Rect> without = boxes<T>(render(make, IconSource{}));
    const std::vector<Rect> with    = boxes<T>(render(make, icon));
    REQUIRE(with.size() > without.size());
    bool found = false;
    for (const Rect& box : with)
    {
        found = found || (inside(box, slot) && std::find(without.begin(), without.end(), box) == without.end());
    }
    CHECK(found);
}

void check_slot(const Scene& make)
{
    const TestRenderer symbol = render(make, Symbol::Find);
    const DrawTextRun* run    = nullptr;
    for (const auto& c : symbol.commands)
    {
        if (const auto* d = std::get_if<DrawTextRun>(&c); d != nullptr && d->text == encode_utf8(SymbolCode))
        {
            run = d;
        }
    }
    REQUIRE(run != nullptr);
    // The square on the glyph's longer side: a glyph fitted by its measured box is narrower than
    // the room its control gives it.
    const float side = std::max(run->bounds.width, run->bounds.height);
    const Rect  slot{run->bounds.center().x - side * 0.5f, run->bounds.center().y - side * 0.5f, side, side};

    const TestRenderer glyph = render(make, IconGlyph{0xE8A5});
    bool               drawn = false;
    for (const auto& c : glyph.commands)
    {
        if (const auto* d = std::get_if<DrawTextRun>(&c); d != nullptr && d->text == encode_utf8(0xE8A5))
        {
            drawn = inside(d->bounds, slot) || d->bounds == slot;
        }
    }
    CHECK(drawn);

    check_kind<DrawGeometry>(make,
                             IconSource::path(PathGeometry::rectangle(Rect{0, 0, 10, 10}, 0.0f, 0.0f)),
                             slot,
                             "path");
    check_kind<DrawImageRect>(make, IconSource::bitmap(Bitmap{8, 8, colors::Black}), slot, "bitmap");
    check_kind<DrawImageRect>(make, Bitmap{8, 4, colors::Black}, slot, "picture");
}

} // namespace

TEST_CASE("Every icon slot draws a symbol, a glyph, a path, a bitmap and a picture", "[icon][slots]")
{
    SECTION("Icon")
    {
        check_slot([](IconSource icon, Window&) { return ui::build(ui::Icon{.source = std::move(icon), .size = 20.0f}); });
    }
    SECTION("AppBarButton")
    {
        check_slot([](IconSource icon, Window&)
                   { return std::make_unique<AppBarButtonControl>("Save", std::move(icon)); });
    }
    SECTION("TitleBar")
    {
        check_slot([](IconSource icon, Window&)
                   { return ui::build(ui::TitleBar{.title = "App", .icon = std::move(icon)}); });
    }
    SECTION("InfoBadge")
    {
        check_slot([](IconSource icon, Window&) { return ui::build(ui::InfoBadge{.icon = std::move(icon)}); });
    }
    SECTION("ToggleSwitch")
    {
        check_slot([](IconSource icon, Window&)
                   { return ui::build(ui::ToggleSwitch{.checked = true, .checked_icon = std::move(icon)}); });
    }
    SECTION("AutoSuggestBox")
    {
        check_slot([](IconSource icon, Window&)
                   { return ui::build(ui::AutoSuggestBox{.query_icon = std::move(icon), .width = 300.0f}); });
    }
    SECTION("SelectorBar")
    {
        check_slot([](IconSource icon, Window&)
                   { return ui::build(ui::SelectorBar{.items = {{"Recent", std::move(icon)}, "Shared"}}); });
    }
    SECTION("PersonPicture badge")
    {
        check_slot([](IconSource icon, Window&)
                   { return ui::build(ui::PersonPicture{.display_name = "Jane Doe", .badge_icon = std::move(icon)}); });
    }
    SECTION("AnimatedIcon fallback")
    {
        check_slot([](IconSource icon, Window&)
                   { return ui::build(ui::AnimatedIcon{.fallback = std::move(icon), .width = 20.0f, .height = 20.0f}); });
    }
    SECTION("Tabs tab")
    {
        check_slot(
            [](IconSource icon, Window&)
            {
                auto view = std::make_unique<TabsControl>();
                view->add_tab(Tab{.header = "Document", .icon = std::move(icon)});
                return view;
            });
    }
    SECTION("NavItem")
    {
        check_slot(
            [](IconSource icon, Window&)
            {
                return ui::build(ui::NavigationShell{.pane_display_mode = NavigationShellPaneDisplayMode::Left,
                                                     .settings_visible  = false,
                                                     .items = {ui::NavItem{.text = "Home", .icon = std::move(icon)}}});
            });
    }
    SECTION("InkToolbar custom tool")
    {
        check_slot(
            [](IconSource icon, Window&)
            {
                auto toolbar = std::make_unique<InkToolbarControl>();
                (void)toolbar->add_custom_tool({.name = "Lasso", .icon = std::move(icon)});
                return toolbar;
            });
    }
    SECTION("TeachingTip")
    {
        check_slot(
            [](IconSource icon, Window&)
            {
                auto  stack = std::make_unique<StackControl>(Orientation::Vertical);
                auto* tip   = stack->add(ui::build(ui::TeachingTip{.title = "Saved", .icon = std::move(icon)}));
                static_cast<TeachingTipControl*>(tip)->set_open(true);
                return stack;
            });
    }
}
