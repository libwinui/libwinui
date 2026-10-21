// SPDX-License-Identifier: MIT
// The debug thread-affinity checks: a mutation of a window's tree, a window or an observed
// property from a thread that does not own it is reported; reads, post()ed work and detached
// elements are not.
#include "winui/core/ObservableVector.hpp"
#include "winui/core/Property.hpp"
#include "winui/core/ThreadCheck.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/Flyout.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/text/Text.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

using namespace ui;

#if LIBWINUI_THREAD_CHECKS

namespace
{
std::mutex                      recorded_mutex;
std::vector<ThreadCheckFailure> recorded;

void record(const ThreadCheckFailure& failure)
{
    const std::lock_guard lock{recorded_mutex};
    recorded.push_back(failure);
}

// Installs the recording handler for one test and puts the previous one back.
class Recorder
{
public:
    Recorder()
    {
        const std::lock_guard lock{recorded_mutex};
        recorded.clear();
        previous_ = set_thread_check_handler(&record);
    }
    ~Recorder()
    {
        set_thread_check_handler(previous_);
    }
    Recorder(const Recorder&)            = delete;
    Recorder& operator=(const Recorder&) = delete;

    [[nodiscard]] std::vector<ThreadCheckFailure> failures() const
    {
        const std::lock_guard lock{recorded_mutex};
        return recorded;
    }
    [[nodiscard]] bool any_call(std::string_view call) const
    {
        const std::vector<ThreadCheckFailure> all = failures();
        return std::ranges::any_of(all, [call](const ThreadCheckFailure& f) { return f.call == call; });
    }

private:
    ThreadCheckHandler previous_ = nullptr;
};

void on_worker(auto&& fn)
{
    std::thread{std::forward<decltype(fn)>(fn)}.join();
}
} // namespace

TEST_CASE("a setter on an element under a window, called from another thread, is reported by name", "[thread_check]")
{
    Recorder     recorder;
    auto         ui   = testing::create_headless();
    TextControl* text = ui.window().set_content(std::make_unique<TextControl>("a"));
    ui.layout({200.0f, 100.0f});

    std::thread::id worker;
    on_worker(
        [&]
        {
            worker = std::this_thread::get_id();
            text->set_text("b");
        });

    const std::vector<ThreadCheckFailure> failures = recorder.failures();
    REQUIRE_FALSE(failures.empty());
    CHECK(recorder.any_call("ui::TextControl::set_text"));
    CHECK(failures.front().thread == worker);
    CHECK(failures.front().owner == std::this_thread::get_id());
    CHECK(failures.front().message.find("ui::TextControl::set_text called on thread") != std::string::npos);
    CHECK(failures.front().message.find("but the tree belongs to thread") != std::string::npos);
}

TEST_CASE("setters that change nothing drawn are checked too", "[thread_check]")
{
    Recorder     recorder;
    auto         ui   = testing::create_headless();
    TextControl* text = ui.window().set_content(std::make_unique<TextControl>("a"));
    ui.layout({200.0f, 100.0f});

    on_worker(
        [&]
        {
            text->set_tool_tip("tip");
            text->on_pointer_entered([] {});
        });

    CHECK(recorder.any_call("ui::Element::set_tool_tip"));
    CHECK(recorder.any_call("ui::Element::on_pointer_entered"));
}

TEST_CASE("a setter deep in the tree is checked against the root's window", "[thread_check]")
{
    Recorder       recorder;
    auto           ui     = testing::create_headless();
    auto           stack  = std::make_unique<StackControl>(Orientation::Vertical);
    ButtonControl* button = stack->add(std::make_unique<StackControl>(Orientation::Horizontal))
                                ->add(std::make_unique<ButtonControl>("Ok"));
    ui.window().set_content(std::move(stack));

    on_worker([&] { button->set_margin(Thickness{4.0f}); });

    CHECK(recorder.any_call("ui::Element::set_margin"));
}

TEST_CASE("a setter on an open popup's parentless surface is checked against the window", "[thread_check]")
{
    // The flyout's content is its child, but the surface it floats in has no parent: only the
    // popup layer knows which window it belongs to.
    Recorder       recorder;
    auto           ui     = testing::create_headless();
    FlyoutControl* flyout = ui.window().set_content(std::make_unique<FlyoutControl>());
    flyout->set_content(std::make_unique<TextControl>("In the popup"));
    flyout->set_anchor(std::make_unique<ButtonControl>("Open"));
    flyout->show();
    ui.layout({400.0f, 400.0f});
    Element* surface = ui::testing::popup_content(*flyout);
    REQUIRE(surface != nullptr);
    REQUIRE(surface->parent() == nullptr);

    on_worker([&] { surface->set_margin(Thickness{2.0f}); });

    CHECK(recorder.any_call("ui::Element::set_margin"));
}

TEST_CASE("detached elements may be built and changed on any thread", "[thread_check]")
{
    Recorder                      recorder;
    auto                          ui = testing::create_headless();
    std::unique_ptr<StackControl> built;

    on_worker(
        [&]
        {
            auto stack = std::make_unique<StackControl>(Orientation::Vertical);
            stack->add(std::make_unique<TextControl>("a"))->set_text("b");
            stack->set_margin(Thickness{2.0f});
            built = std::move(stack);
        });
    ui.window().set_content(std::move(built));

    CHECK(recorder.failures().empty());
}

TEST_CASE("post()ed work runs on the UI thread and passes the check", "[thread_check]")
{
    Recorder     recorder;
    auto         ui   = testing::create_headless();
    Window&      w    = ui.window();
    TextControl* text = w.set_content(std::make_unique<TextControl>("a"));

    on_worker(
        [&]
        {
            // A whole tree built on the worker and handed over, and a setter on the live one.
            auto page = std::make_unique<StackControl>(Orientation::Vertical);
            page->add(std::make_unique<TextControl>("built"));
            w.post([text] { text->set_text("posted"); });
            w.post([&w, page = std::move(page)]() mutable { w.set_content(std::move(page)); });
        });
    CHECK(text->text() == "a");
    ui.tick(16ms);

    CHECK(recorder.failures().empty());
    CHECK(dynamic_cast<StackControl*>(w.content()) != nullptr);
}

TEST_CASE("measuring an attached tree on a worker is not reported", "[thread_check]")
{
    Recorder recorder;
    auto     ui    = testing::create_headless();
    auto     stack = std::make_unique<StackControl>(Orientation::Vertical);
    stack->add(std::make_unique<TextControl>("Some text"));
    stack->add(std::make_unique<ButtonControl>("Ok"));
    Element*    root  = ui.window().set_content(std::move(stack));
    const Theme theme = Theme::light();

    on_worker([&] { (void)root->measure(Size{300.0f, 200.0f}, ui.measurer(), theme); });

    CHECK(recorder.failures().empty());
}

TEST_CASE("Window members other than post() are checked", "[thread_check]")
{
    Recorder recorder;
    auto     ui = testing::create_headless();
    Window&  w  = ui.window();

    on_worker([&] { w.post([] {}); });
    CHECK(recorder.failures().empty());

    on_worker([&] { w.set_title("from a worker"); });
    CHECK(recorder.any_call("ui::Window::set_title"));

    on_worker([&] { w.set_content(std::make_unique<TextControl>("x")); });
    const std::vector<ThreadCheckFailure> failures = recorder.failures();
    const auto set_content = std::ranges::find(failures, std::string{"ui::Window::set_content"}, &ThreadCheckFailure::call);
    REQUIRE(set_content != failures.end());
    // set_content() reports the caller's line, not its own.
    CHECK(std::string_view{set_content->where.file_name()}.find("test_thread_check.cpp") != std::string_view::npos);
}

TEST_CASE("a property is checked once it has notified an observer", "[thread_check]")
{
    Recorder      recorder;
    Property<int> count{0};

    // Nobody observes it yet: any thread may write it.
    on_worker([&] { count.set(1); });
    CHECK(recorder.failures().empty());

    int          seen = 0;
    Subscription sub  = count.observe([&](const int& n) { seen = n; });
    count.set(2); // the first notifying write claims this thread
    CHECK(seen == 2);
    CHECK(recorder.failures().empty());

    // clang-format off
    on_worker([&] { count.set(3); }); const auto line = std::source_location::current().line();
    // clang-format on
    std::vector<ThreadCheckFailure> failures = recorder.failures();
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].call == "ui::Property::set");
    CHECK(failures[0].where.line() == line);
    CHECK(std::string_view{failures[0].where.file_name()}.find("test_thread_check.cpp") != std::string_view::npos);
    CHECK(failures[0].message.find("but the property belongs to thread") != std::string::npos);

    // update(), operator= and a new observer from the wrong thread too, each at the caller's line.
    on_worker([&] { count.update([](int& n) { ++n; }); });
    on_worker([&] { Subscription other = count.observe([](const int&) {}); });
    // clang-format off
    on_worker([&] { count = 10; }); const auto assign_line = std::source_location::current().line();
    // clang-format on
    failures = recorder.failures();
    REQUIRE(failures.size() == 4);
    CHECK(failures[1].call == "ui::Property::set");
    CHECK(failures[2].call.find("observe") != std::string::npos);
    CHECK(failures[3].call == "ui::Property::set");
    CHECK(failures[3].where.line() == assign_line);
    CHECK(std::string_view{failures[3].where.file_name()}.ends_with("test_thread_check.cpp"));
}

TEST_CASE("an element report names the property write that led to it", "[thread_check]")
{
    Recorder              recorder;
    auto                  ui = testing::create_headless();
    Property<std::string> title{"a"};
    TextControl*          text = ui.window().set_content(std::make_unique<TextControl>("a"));
    text->add_subscription(title.observe([text](const std::string& s) { text->set_text(s); }));

    // The worker's write is the property's first notifying one, so the property claims the
    // worker; the element it reaches belongs to the window, and says where the write was.
    on_worker([&] { title.set("b"); });

    const std::vector<ThreadCheckFailure> failures = recorder.failures();
    REQUIRE_FALSE(failures.empty());
    CHECK(failures.front().call == "ui::TextControl::set_text");
    CHECK(failures.front().message.find("reached from a ui::Property write at test_thread_check.cpp:") != std::string::npos);
}

TEST_CASE("an observable vector and a signal are checked like a property", "[thread_check]")
{
    Recorder              recorder;
    ObservableVector<int> items;
    Subscription          sub = items.observe([](const CollectionChange&) {});
    items.push_back(1); // claims this thread

    // clang-format off
    on_worker([&] { items.push_back(2); }); const auto line = std::source_location::current().line();
    // clang-format on
    std::vector<ThreadCheckFailure> failures = recorder.failures();
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].call == "ui::ObservableVector::push_back");
    CHECK(failures[0].where.line() == line); // the caller's line, not the library's

    Signal       changed;
    Subscription on_changed = changed.observe([] {});
    changed.raise();
    on_worker([&] { changed.raise(); });
    failures = recorder.failures();
    REQUIRE(failures.size() == 2);
    CHECK(failures[1].call == "ui::Signal::raise");
}

TEST_CASE("set_thread_check_handler() returns the handler it replaces", "[thread_check]")
{
    const ThreadCheckHandler previous   = set_thread_check_handler(&record);
    const bool               was_record = set_thread_check_handler(previous) == &record;
    CHECK(was_record);
}

#else

TEST_CASE("thread checks are compiled out of this build", "[thread_check]")
{
    STATIC_REQUIRE_FALSE(ThreadChecksEnabled);
}

#endif
