// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/dialogs/TeachingTip.hpp"
#include "winui/ui/hosting/TitleBar.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/menus/CommandBar.hpp"
#include "winui/ui/panels/Spacer.hpp"
#include "winui/ui/panes/ResizeGripper.hpp"
#include "winui/ui/panes/Splitter.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/scrolling/Swipe.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/status/InfoBar.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

TEST_CASE("Every input-taking container spec takes enabled", "[spec][enabled]")
{
    Control<Splitter>      splitter;
    Control<ResizeGripper> gripper;
    Control<InfoBar>       info_bar;
    Control<TeachingTip>   tip;
    Control<Swipe>         swipe;
    Control<CommandBar>    command_bar;
    Control<Scroll>        scroll_view;
    Control<TitleBar>      title_bar;

    std::vector<std::unique_ptr<Element>> built;
    built.push_back(Splitter{.enabled = false, .control = splitter}.build());
    built.push_back(ResizeGripper{.enabled = false, .control = gripper}.build());
    built.push_back(InfoBar{.enabled = false, .control = info_bar}.build());
    built.push_back(TeachingTip{.enabled = false, .control = tip}.build());
    built.push_back(Swipe{.enabled = false, .control = swipe}.build());
    built.push_back(CommandBar{.enabled = false, .control = command_bar}.build());
    built.push_back(Scroll{.enabled = false, .control = scroll_view}.build());
    built.push_back(TitleBar{.enabled = false, .control = title_bar}.build());

    CHECK_FALSE(splitter->enabled());
    CHECK_FALSE(gripper->enabled());
    CHECK_FALSE(info_bar->enabled());
    CHECK_FALSE(tip->enabled());
    CHECK_FALSE(swipe->enabled());
    CHECK_FALSE(command_bar->enabled());
    CHECK_FALSE(scroll_view->enabled());
    CHECK_FALSE(title_bar->enabled());
}

TEST_CASE("A disabled Splitter ignores drags and keys and leaves the tab ring", "[splitter][enabled]")
{
    SplitterControl splitter{Orientation::Vertical};
    splitter.arrange({100, 0, 6, 200});
    splitter.set_focusable(true);
    int deltas = 0;
    splitter.on_drag_delta([&](float) { ++deltas; });

    splitter.set_enabled(false);
    CHECK_FALSE(splitter.focusable());
    CHECK_FALSE(splitter.cursor_at({103, 50}).has_value());
    splitter.handle_pointer({{103, 50}, PointerButton::Left, PointerAction::Down});
    splitter.handle_pointer({{120, 50}, PointerButton::None, PointerAction::Move});
    CHECK_FALSE(splitter.dragging());
    CHECK(deltas == 0);

    splitter.set_enabled(true);
    CHECK(splitter.focusable());
    splitter.handle_pointer({{103, 50}, PointerButton::Left, PointerAction::Down});
    splitter.handle_pointer({{120, 50}, PointerButton::None, PointerAction::Move});
    CHECK(deltas == 1);
}

TEST_CASE("A disabled Scroll leaves the wheel to whatever is around it", "[scroll-view][enabled]")
{
    HeadlessTextMeasurer m;
    Control<Scroll>      view;
    auto                 node = Scroll{.control = view, .content = Spacer{.height = 1000}}.build();
    (void)node->measure({300, 200}, m, Theme::default_light());
    node->arrange({0, 0, 300, 200});

    const PointerEvent turn{{10, 10}, PointerButton::None, PointerAction::Wheel, 40.0f};
    view->set_enabled(false);
    CHECK_FALSE(node->handle_wheel(turn));
    CHECK(view->vertical_offset() == 0.0f);

    view->set_enabled(true);
    CHECK(node->handle_wheel(turn));
    CHECK(view->vertical_offset() > 0.0f);
}

TEST_CASE("A disabled TitleBar keeps its buttons disabled", "[title-bar][enabled]")
{
    TitleBarControl bar;
    bar.set_back_button_visible(true);
    bar.set_pane_toggle_button_visible(true);
    const auto buttons = [&bar]
    {
        std::vector<ButtonControl*> out;
        bar.for_each_child(
            [&out](Element& child)
            {
                if (auto* button = dynamic_cast<ButtonControl*>(&child))
                {
                    out.push_back(button);
                }
            });
        return out;
    };
    REQUIRE(buttons().size() == 2);

    bar.set_enabled(false);
    bar.set_back_button_enabled(true);
    CHECK(bar.is_back_button_enabled());
    for (ButtonControl* button : buttons())
    {
        CHECK_FALSE(button->enabled());
    }

    bar.set_enabled(true);
    for (ButtonControl* button : buttons())
    {
        CHECK(button->enabled());
    }
}
