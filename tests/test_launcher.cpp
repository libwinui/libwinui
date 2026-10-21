// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/render/Renderer.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestLauncher.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Command.hpp"
#include "winui/ui/Launcher.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/spec/Spec.hpp"
#include "winui/ui/text/Text.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;

namespace
{
const std::string Uri = "https://example.com/";

PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}

struct LinkWindow
{
    HeadlessTextMeasurer measurer;
    TestLauncher         launcher;
    Window               window;

    LinkWindow()
    {
        set_launcher(&launcher);
    }

    template <typename T>
    T* show(std::unique_ptr<T> content)
    {
        T* raw = window.set_content(std::move(content));
        window.layout({400.0f, 400.0f}, measurer);
        return raw;
    }

    void click(Point p)
    {
        ui::testing::seams(window).handle_pointer(down(p));
        ui::testing::seams(window).handle_pointer(up(p));
    }
};
} // namespace

TEST_CASE("A button's NavigateUri is launched after its click handler and command", "[launcher]")
{
    LinkWindow               w;
    std::vector<std::string> seen_by_handler;
    std::vector<std::string> seen_by_command;
    Command                  command{.execute = [&] { seen_by_command = w.launcher.launched; }};

    auto button = std::make_unique<ButtonControl>("Docs");
    button->set_variant(ButtonVariant::Hyperlink);
    button->set_navigate_uri(Uri);
    button->on_click([&] { seen_by_handler = w.launcher.launched; });
    button->set_command(command);
    ButtonControl* b = w.show(std::move(button));

    w.click(b->bounds().center());
    REQUIRE(w.launcher.launched == std::vector<std::string>{Uri});
    // The click handler, then the command, then the launch.
    REQUIRE(seen_by_handler.empty());
    REQUIRE(seen_by_command.empty());
}

TEST_CASE("Enter on a focused hyperlink button launches its URI", "[launcher]")
{
    LinkWindow w;
    auto       button = std::make_unique<ButtonControl>("Docs");
    button->set_navigate_uri(Uri);
    ButtonControl* b = w.show(std::move(button));
    REQUIRE(w.window.focus_element(b));

    ui::testing::seams(w.window).handle_key({Key::Enter, KeyAction::Down});
    ui::testing::seams(w.window).handle_key({Key::Enter, KeyAction::Up});
    REQUIRE(w.launcher.launched == std::vector<std::string>{Uri});
}

TEST_CASE("UI Automation's Invoke launches a hyperlink button's URI", "[launcher][automation]")
{
    LinkWindow w;
    auto       button = std::make_unique<ButtonControl>("Docs");
    button->set_navigate_uri(Uri);
    ButtonControl* b = w.show(std::move(button));

    REQUIRE(ui::testing::seams(w.window).handle_automation_invoke(*b));
    REQUIRE(w.launcher.launched == std::vector<std::string>{Uri});
}

TEST_CASE("A disabled button, or one without a URI, launches nothing", "[launcher]")
{
    LinkWindow     w;
    auto           button = std::make_unique<ButtonControl>("Docs");
    ButtonControl* b      = w.show(std::move(button));

    w.click(b->bounds().center());
    REQUIRE(w.launcher.launched.empty());

    b->set_navigate_uri(Uri);
    b->set_enabled(false);
    w.click(b->bounds().center());
    REQUIRE(w.launcher.launched.empty());
}

TEST_CASE("launch_uri() and launch_file() go to the installed launcher, empty ones nowhere", "[launcher]")
{
    TestLauncher launcher;
    set_launcher(&launcher);
    ui::launch_uri("");
    ui::launch_file({});
    REQUIRE(launcher.launched.empty());
    REQUIRE(launcher.launched_files.empty());

    ui::launch_uri(Uri);
    ui::launch_file("C:/Reports/report.pdf");
    REQUIRE(launcher.launched == std::vector<std::string>{Uri});
    REQUIRE(launcher.launched_files == std::vector<std::filesystem::path>{"C:/Reports/report.pdf"});
}

TEST_CASE("A test launcher uninstalls itself when it goes", "[launcher]")
{
    {
        TestLauncher launcher;
        set_launcher(&launcher);
        REQUIRE(ui::detail::installed_launcher() == &launcher);
    }
    REQUIRE(ui::detail::installed_launcher() == nullptr);

    // One that was replaced leaves the newer one alone.
    TestLauncher kept;
    {
        TestLauncher replaced;
        set_launcher(&replaced);
        set_launcher(&kept);
    }
    REQUIRE(ui::detail::installed_launcher() == &kept);
    set_launcher(nullptr);
}

TEST_CASE("A button outside a Window launches its URI when activated", "[launcher]")
{
    TestLauncher launcher;
    set_launcher(&launcher);
    ButtonControl button{"Docs"};
    button.set_navigate_uri(Uri);
    ui::testing::set_focused(button, true);
    button.handle_key({Key::Space, KeyAction::Down});
    button.handle_key({Key::Space, KeyAction::Up});
    REQUIRE(launcher.launched == std::vector<std::string>{Uri});
}

TEST_CASE("An inline with only a NavigateUri is a hyperlink that launches it", "[launcher][text]")
{
    LinkWindow w;
    int        clicks = 0;
    auto       block  = std::make_unique<TextControl>();
    block->set_inlines({{.text = "plain", .navigate_uri = Uri},
                        {.text = "both", .on_click = [&] { ++clicks; }, .navigate_uri = "mailto:someone@example.com"}});
    REQUIRE(!block->inlines().front().on_click);
    REQUIRE(block->inlines().front().is_link());
    TextControl* b = w.show(std::move(block));

    TestRenderer r;
    b->render(r, Theme::light());
    const auto* first = [&]() -> const DrawTextRun*
    {
        for (const auto& command : r.commands)
        {
            if (const auto* text = std::get_if<DrawTextRun>(&command))
            {
                return text;
            }
        }
        return nullptr;
    }();
    REQUIRE(first != nullptr);
    REQUIRE(first->style.underline);

    const Point on_first{b->bounds().x + 2.0f, b->bounds().y + 5.0f};
    w.click(on_first);
    REQUIRE(w.launcher.launched == std::vector<std::string>{Uri});
    REQUIRE(clicks == 0);
}

TEST_CASE("ui::Button's navigate_uri reaches the control", "[launcher][spec]")
{
    auto  node   = ui::build(ui::Button{.text = "Docs", .navigate_uri = Uri, .variant = ButtonVariant::Hyperlink});
    auto* button = dynamic_cast<ButtonControl*>(node.get());
    REQUIRE(button != nullptr);
    REQUIRE(button->navigate_uri() == Uri);
}
