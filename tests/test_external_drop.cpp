// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/DragDrop.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Text.hpp"

#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ui;

namespace
{
DataPackage dropped_files()
{
    DataPackage package;
    package.files               = {std::filesystem::path{"C:/photos/a.jpg"}, std::filesystem::path{"C:/photos/b.jpg"}};
    package.requested_operation = DataPackageOperation::Copy;
    return package;
}
} // namespace

TEST_CASE("files dropped from another app reach the target under the drop point", "[drag][external-drop]")
{
    testing::HeadlessUI ui    = testing::create_headless();
    auto                stack = std::make_unique<StackControl>(Orientation::Vertical);
    auto*               zone  = stack->add(std::make_unique<StackControl>(Orientation::Vertical));
    auto*               other = stack->add(std::make_unique<TextControl>("Not a target"));
    zone->set_height(100.0f);
    zone->set_allow_drop(true);

    std::vector<std::string>           events;
    std::vector<std::filesystem::path> received;
    zone->on_drag_enter([&] { events.emplace_back("enter"); });
    zone->on_drag_over(
        [&](const DragEventArgs& e)
        {
            events.emplace_back("over");
            return DragResponse{.operation = e.data.files.empty() ? DataPackageOperation::None : DataPackageOperation::Copy};
        });
    zone->on_drop(
        [&](const DragEventArgs& e)
        {
            events.emplace_back("drop");
            received = e.data.files;
            CHECK(e.data.source == nullptr);
        });
    zone->on_drag_leave([&] { events.emplace_back("leave"); });
    ui.window().set_content(std::move(stack));
    ui.layout({400.0f, 300.0f});

    Window& w = ui.window();
    CHECK(testing::seams(w).handle_external_drop(dropped_files(), zone->bounds().center()) == DataPackageOperation::Copy);
    CHECK(events == std::vector<std::string>{"enter", "over", "drop"});
    CHECK(received == dropped_files().files);
    CHECK_FALSE(DragDrop::active());

    // Where nothing accepts drops, nothing hears it.
    events.clear();
    CHECK(testing::seams(w).handle_external_drop(dropped_files(), other->bounds().center()) == DataPackageOperation::None);
    CHECK(events.empty());

    // A target that refuses hears the drag leave again.
    DataPackage text_only;
    text_only.text = "not files";
    CHECK(testing::seams(w).handle_external_drop(text_only, zone->bounds().center()) == DataPackageOperation::None);
    CHECK(events == std::vector<std::string>{"enter", "over", "leave"});
}

TEST_CASE("a drag is asked of the drop targets from the innermost up until one accepts", "[drag][external-drop]")
{
    testing::HeadlessUI ui    = testing::create_headless();
    auto                outer = std::make_unique<StackControl>(Orientation::Vertical);
    auto*               inner = outer->add(std::make_unique<StackControl>(Orientation::Vertical));
    inner->set_height(100.0f);
    outer->set_allow_drop(true);
    inner->set_allow_drop(true);

    std::vector<std::string> events;
    inner->on_drag_enter([&] { events.emplace_back("inner enter"); });
    inner->on_drag_over(
        [&](const DragEventArgs& e)
        {
            events.emplace_back("inner over");
            return DragResponse{.operation = e.data.files.empty() ? DataPackageOperation::None : DataPackageOperation::Copy};
        });
    inner->on_drag_leave([&] { events.emplace_back("inner leave"); });
    inner->on_drop([&] { events.emplace_back("inner drop"); });
    outer->on_drag_enter([&] { events.emplace_back("outer enter"); });
    outer->on_drag_over(
        [&]
        {
            events.emplace_back("outer over");
            return DragResponse{.operation = DataPackageOperation::Move, .caption = "Move here"};
        });
    outer->on_drag_leave([&] { events.emplace_back("outer leave"); });
    outer->on_drop([&] { events.emplace_back("outer drop"); });
    ui.window().set_content(std::move(outer));
    ui.layout({400.0f, 300.0f});
    Window& w = ui.window();

    // Files: the inner one accepts, so the outer one is never asked.
    CHECK(testing::seams(w).handle_external_drop(dropped_files(), inner->bounds().center()) == DataPackageOperation::Copy);
    CHECK(events == std::vector<std::string>{"inner enter", "inner over", "inner drop"});

    // Text: the inner one refuses and hears it leave; the outer one takes it.
    events.clear();
    DataPackage text;
    text.text = "hello";
    CHECK(testing::seams(w).handle_external_drop(text, inner->bounds().center()) == DataPackageOperation::Move);
    CHECK(
        events ==
        std::vector<std::string>{"inner enter", "inner over", "outer enter", "outer over", "inner leave", "outer drop"});
}

TEST_CASE("a drag from another app is answered while it moves over the window", "[drag][external-drop]")
{
    testing::HeadlessUI ui    = testing::create_headless();
    auto                stack = std::make_unique<StackControl>(Orientation::Vertical);
    auto*               zone  = stack->add(std::make_unique<StackControl>(Orientation::Vertical));
    auto*               other = stack->add(std::make_unique<TextControl>("Not a target"));
    zone->set_height(100.0f);
    zone->set_allow_drop(true);

    std::vector<std::string> events;
    zone->on_drag_enter([&] { events.emplace_back("enter"); });
    zone->on_drag_over(
        [&](const DragEventArgs& e)
        {
            events.emplace_back("over");
            return DragResponse{.operation = e.data.requested_operation,
                                .caption   = std::format("Add {} file(s)", e.data.files.size())};
        });
    zone->on_drag_leave([&] { events.emplace_back("leave"); });
    zone->on_drop([&] { events.emplace_back("drop"); });
    ui.window().set_content(std::move(stack));
    ui.layout({400.0f, 300.0f});
    auto seams = testing::seams(ui.window());

    // Over the zone: accepted, with the caption, and the window has a drag running.
    const DragResponse entered = seams.handle_external_drag_enter(dropped_files(), zone->bounds().center());
    CHECK(entered.operation == DataPackageOperation::Copy);
    CHECK(entered.caption == "Add 2 file(s)");
    CHECK(DragDrop::active());

    // Shift held: the package asks for a move, and the zone answers it.
    CHECK(seams.handle_external_drag_over(zone->bounds().center(), DataPackageOperation::Move).operation ==
          DataPackageOperation::Move);

    // Off the zone: refused, and the zone hears it leave; back on it: entered again.
    CHECK(seams.handle_external_drag_over(other->bounds().center()).operation == DataPackageOperation::None);
    CHECK(seams.handle_external_drag_over(zone->bounds().center()).operation == DataPackageOperation::Move);
    CHECK(events == std::vector<std::string>{"enter", "over", "over", "leave", "enter", "over"});

    // Dropped where it last moved: no new question, and the drag is over.
    events.clear();
    CHECK(seams.handle_external_drag_drop(zone->bounds().center()) == DataPackageOperation::Move);
    CHECK(events == std::vector<std::string>{"drop"});
    CHECK_FALSE(DragDrop::active());

    // One that leaves the window again: the zone hears it, nothing is dropped.
    events.clear();
    (void)seams.handle_external_drag_enter(dropped_files(), zone->bounds().center());
    seams.handle_external_drag_leave();
    CHECK(events == std::vector<std::string>{"enter", "over", "leave"});
    CHECK_FALSE(DragDrop::active());
    CHECK(seams.handle_external_drag_drop(zone->bounds().center()) == DataPackageOperation::None);
}

TEST_CASE("external_drag() is true while a drag from another app is over the window", "[drag][external-drop]")
{
    testing::HeadlessUI ui;
    Window&             w = ui.window();
    w.set_content(std::make_unique<TextControl>("Drop here"));
    ui.layout({200, 100});

    std::vector<bool> seen;
    Subscription      s = w.external_drag().observe([&](bool over) { seen.push_back(over); });
    CHECK_FALSE(w.external_drag().get());
    CHECK(w.external_drag_data() == nullptr);

    (void)testing::seams(w).handle_external_drag_enter(dropped_files(), {10, 10});
    CHECK(w.external_drag().get());
    REQUIRE(w.external_drag_data() != nullptr);
    CHECK_FALSE(w.external_drag_data()->files.empty());
    (void)testing::seams(w).handle_external_drag_over({150, 50});
    testing::seams(w).handle_external_drag_leave();
    CHECK_FALSE(w.external_drag().get());
    CHECK(w.external_drag_data() == nullptr);

    (void)testing::seams(w).handle_external_drop(dropped_files(), {10, 10}); // a drop ends it too
    CHECK_FALSE(w.external_drag().get());
    CHECK(seen == std::vector<bool>{true, false, true, false});
}
