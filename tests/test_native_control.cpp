// SPDX-License-Identifier: MIT
#include "winui/core/DirtyFlags.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/core/VisualId.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/SpellChecker.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestNativeHost.hpp"
#include "winui/testing/TestSpellChecker.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/hosting/ContentHost.hpp"
#include "winui/ui/hosting/NativeControl.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/scrolling/Scroll.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using namespace ui;
using namespace ui::testing;

namespace
{
NativeControlDesc make_desc()
{
    NativeControlDesc desc;
    desc.class_name   = "EDIT";
    desc.initial_text = "hello";
    return desc;
}
} // namespace

TEST_CASE("NativeControl measures to its fixed size regardless of available space", "[native_control]")
{
    NativeControl control{make_desc(), Size{120.0f, 24.0f}};
    Theme         theme = Theme::light();

    REQUIRE(control.measure({Unbounded, Unbounded}, HeadlessTextMeasurer{}, theme) == Size{120.0f, 24.0f});
    REQUIRE(control.measure({1000, 1000}, HeadlessTextMeasurer{}, theme) == Size{120.0f, 24.0f});
}

TEST_CASE("sync_native creates the control once and only moves it when bounds actually changed", "[native_control]")
{
    // `host` must outlive `control`: its destructor calls back into the host it was last synced with.
    TestNativeHost host;
    NativeControl  control{make_desc(), Size{120.0f, 24.0f}};

    control.arrange({10, 10, 120, 24});
    control.sync_native(host);

    const auto* entry = host.find(control.visual_id());
    REQUIRE(entry != nullptr);
    REQUIRE(entry->created);
    REQUIRE(entry->create_count == 1);
    REQUIRE(entry->move_count == 1); // the initial creation also positions it
    REQUIRE(entry->bounds == Rect{10, 10, 120, 24});
    REQUIRE(entry->desc.class_name == "EDIT");

    control.sync_native(host);
    entry = host.find(control.visual_id());
    REQUIRE(entry->create_count == 1);
    REQUIRE(entry->move_count == 1);

    control.arrange({20, 10, 120, 24});
    control.sync_native(host);
    entry = host.find(control.visual_id());
    REQUIRE(entry->create_count == 1);
    REQUIRE(entry->move_count == 2);
    REQUIRE(entry->bounds == Rect{20, 10, 120, 24});
}

TEST_CASE("a NativeControl outlives its host, and gets a new window from the next one", "[native_control]")
{
    // A closed window: its host goes, the tree the app owns stays.
    NativeControl control{make_desc(), Size{120.0f, 24.0f}};
    control.arrange({10, 10, 120, 24});
    {
        TestNativeHost closed;
        control.sync_native(closed);
        REQUIRE(closed.find(control.visual_id()) != nullptr);
    }
    CHECK(control.native_handle() == nullptr);

    // Shown again: created afresh rather than taken for still there.
    TestNativeHost reopened;
    control.sync_native(reopened);
    const auto* entry = reopened.find(control.visual_id());
    REQUIRE(entry != nullptr);
    CHECK(entry->create_count == 1);
}

TEST_CASE("sync_native registers the control as its entry's owner", "[native_control]")
{
    TestNativeHost host;
    NativeControl  control{make_desc(), Size{120.0f, 24.0f}};

    control.arrange(Rect{0.0f, 0.0f, 120.0f, 24.0f});
    control.sync_native(host);

    const TestNativeHost::Entry* entry = host.find(control.visual_id());
    REQUIRE(entry != nullptr);
    REQUIRE(entry->owner == &control);
}

TEST_CASE("Destroying a NativeControl after sync_native tears down its native host entry", "[native_control]")
{
    TestNativeHost host;
    {
        NativeControl control{make_desc(), Size{120.0f, 24.0f}};
        control.arrange({0, 0, 120, 24});
        control.sync_native(host);
        REQUIRE(host.count() == 1);
    }
    REQUIRE(host.count() == 0);
}

TEST_CASE("A NativeControl never synced has no native host entry to destroy", "[native_control]")
{
    TestNativeHost host;
    {
        NativeControl control{make_desc(), Size{120.0f, 24.0f}};
        (void)control;
    }
    REQUIRE(host.count() == 0);
}

TEST_CASE("Containers recurse sync_native into their children", "[native_control][stack_panel]")
{
    // `host` must outlive `panel`, whose NativeControl child calls back into it on destruction.
    TestNativeHost host;
    StackControl   panel{Orientation::Vertical};
    NativeControl* control = panel.add(std::make_unique<NativeControl>(make_desc(), Size{100.0f, 20.0f}));

    const Size desired = panel.measure({200, 200}, HeadlessTextMeasurer{}, Theme::default_light());
    panel.arrange({0, 0, desired.width, desired.height});

    panel.sync_native(host);

    REQUIRE(host.count() == 1);
    REQUIRE(host.find(control->visual_id()) != nullptr);
}

namespace
{
// TestNativeHost is final and has no real handle to hand back, so the creation hook needs a small
// host that fabricates one.
class HandleHost final : public NativeHost
{
public:
    NativeHandle handle = reinterpret_cast<NativeHandle>(static_cast<std::uintptr_t>(0xBEEF));
    bool         exists = false;
    int          moves  = 0;
    Rect         bounds{};

    void ensure_created(VisualId, const NativeControlDesc&) override
    {
        exists = true;
    }
    void set_bounds(VisualId, const Rect& rect) override
    {
        bounds = rect;
        ++moves;
    }
    void set_visible(VisualId, bool) override
    {
    }
    void destroy(VisualId) override
    {
        exists = false;
    }
    [[nodiscard]] NativeHandle native_handle(VisualId) const override
    {
        return exists ? handle : nullptr;
    }
};

class HookedControl final : public NativeControl
{
public:
    using NativeControl::NativeControl;

    int          created_calls  = 0;
    NativeHandle created_handle = nullptr;
    Rect         bounds_at_call{};

protected:
    void on_native_created(NativeHandle handle) override
    {
        ++created_calls;
        created_handle = handle;
        bounds_at_call = bounds();
    }
};
} // namespace

TEST_CASE("on_native_created fires once, with the real handle, after the first move", "[native_control]")
{
    HandleHost    host;
    HookedControl control{make_desc(), Size{120.0f, 24.0f}};

    control.arrange({10, 10, 120, 24});
    control.sync_native(host);

    REQUIRE(control.created_calls == 1);
    REQUIRE(control.created_handle == host.handle);
    // The hook runs after set_bounds(), so a control that sizes itself off its client rect sees the
    // real one, not the 0x0 CreateWindowExW started it at.
    REQUIRE(control.bounds_at_call == Rect{10, 10, 120, 24});
    REQUIRE(host.moves == 1);

    control.sync_native(host);
    control.arrange({20, 10, 120, 24});
    control.sync_native(host);
    REQUIRE(control.created_calls == 1);
    REQUIRE(host.moves == 2);
}

TEST_CASE("on_native_created is skipped when the host produced no handle", "[native_control]")
{
    // A failed CreateWindowExW: the entry exists, but there is no handle to configure.
    TestNativeHost host;
    HookedControl  control{make_desc(), Size{120.0f, 24.0f}};

    control.arrange({0, 0, 120, 24});
    control.sync_native(host);

    REQUIRE(host.find(control.visual_id()) != nullptr);
    REQUIRE(control.created_calls == 0);
}


namespace
{
// A NativeControl gets its spell checker through the host, not through focus.
class SpellAwareControl final : public NativeControl
{
public:
    using NativeControl::NativeControl;

    int           spell_syncs   = 0;
    SpellChecker* last_checker  = nullptr;
    int           content_notes = 0;

    void sync_spell_checker(SpellChecker& checker) override
    {
        ++spell_syncs;
        last_checker = &checker;
    }
    void on_native_content_changed() override
    {
        ++content_notes;
    }
};
} // namespace

TEST_CASE("A host carrying a spell checker hands it to every native control it syncs", "[native_control][spell]")
{
    TestNativeHost    host;
    TestSpellChecker  checker;
    SpellAwareControl control{make_desc(), Size{120.0f, 24.0f}};

    control.arrange({0, 0, 120, 24});
    control.sync_native(host);
    REQUIRE(control.spell_syncs == 0); // nothing published yet

    host.set_spell_checker(&checker);
    control.sync_native(host);
    REQUIRE(control.spell_syncs == 1);
    REQUIRE(control.last_checker == &checker);

    // Handed over on every pass. Only the control knows whether that means a re-check.
    control.sync_native(host);
    REQUIRE(control.spell_syncs == 2);

    host.set_spell_checker(nullptr);
    control.sync_native(host);
    REQUIRE(control.spell_syncs == 2);
}

TEST_CASE("Window publishes its spell checker to the native host", "[native_control][spell][window]")
{
    TestNativeHost   host;
    TestSpellChecker checker;
    Window           window;
    seams(window).set_native_host(&host);
    seams(window).set_spell_checker(&checker);

    auto               panel   = std::make_unique<StackControl>(Orientation::Vertical);
    SpellAwareControl* control = panel->add(std::make_unique<SpellAwareControl>(make_desc(), Size{100.0f, 20.0f}));
    window.set_content(std::move(panel));
    window.layout({200.0f, 100.0f}, HeadlessTextMeasurer{});

    REQUIRE(control->spell_syncs > 0);
    REQUIRE(control->last_checker == &checker);
}

TEST_CASE("A native control is told when its own content changed", "[native_control][spell][window]")
{
    TestNativeHost   host;
    TestSpellChecker checker;
    Window           window;
    seams(window).set_native_host(&host);
    seams(window).set_spell_checker(&checker);

    auto               panel   = std::make_unique<StackControl>(Orientation::Vertical);
    SpellAwareControl* control = panel->add(std::make_unique<SpellAwareControl>(make_desc(), Size{100.0f, 20.0f}));
    window.set_content(std::move(panel));
    window.layout({200.0f, 100.0f}, HeadlessTextMeasurer{});

    const int syncs_before = control->spell_syncs;
    ui::testing::seams(window).handle_native_content_changed(*control);
    REQUIRE(control->content_notes == 1);
    REQUIRE(control->spell_syncs > syncs_before);
}

// ---- clipping, hiding and notifications ----

namespace
{
struct Scrolled
{
    // `host` first, so it outlives the controls.
    TestNativeHost                 host;
    std::unique_ptr<ScrollControl> view = std::make_unique<ScrollControl>();
    NativeControl*                 top  = nullptr; // 100x50, first
    NativeControl*                 tall = nullptr; // 100x500, under it

    Scrolled()
    {
        auto* panel = view->set_child(std::make_unique<StackControl>(Orientation::Vertical));
        panel->set_horizontal_alignment(Alignment::Leading);
        panel->set_vertical_alignment(Alignment::Leading);
        top  = panel->add(std::make_unique<NativeControl>(make_desc(), Size{100.0f, 50.0f}));
        tall = panel->add(std::make_unique<NativeControl>(make_desc(), Size{100.0f, 500.0f}));
        top->set_horizontal_alignment(Alignment::Leading);
        tall->set_horizontal_alignment(Alignment::Leading);
        (void)view->measure({300.0f, 200.0f}, HeadlessTextMeasurer{}, Theme::default_light());
        view->arrange({0.0f, 0.0f, 300.0f, 200.0f});
        view->sync_native(host);
    }
};
} // namespace

TEST_CASE("A native control is clipped to what its scrolling ancestors leave in view", "[native_control][scroll-view]")
{
    Scrolled                     s;
    const TestNativeHost::Entry* top  = s.host.find(s.top->visual_id());
    const TestNativeHost::Entry* tall = s.host.find(s.tall->visual_id());
    REQUIRE(top != nullptr);
    REQUIRE(tall != nullptr);

    CHECK_FALSE(top->clip.has_value());
    CHECK(top->visible);
    REQUIRE(tall->clip.has_value());
    CHECK(*tall->clip == Rect{0.0f, 50.0f, 100.0f, 150.0f});
    CHECK(s.tall->visible_rect() == Rect{0.0f, 50.0f, 100.0f, 150.0f});

    s.view->scroll_to(0.0, 100.0, {.animation_mode = ScrollAnimationMode::Disabled});
    s.view->sync_native(s.host);
    top  = s.host.find(s.top->visual_id());
    tall = s.host.find(s.tall->visual_id());
    CHECK_FALSE(top->visible);
    CHECK(s.top->visible_rect().width == 0.0f);
    REQUIRE(tall->clip.has_value());
    CHECK(*tall->clip == Rect{0.0f, 0.0f, 100.0f, 200.0f});
    CHECK(tall->bounds == Rect{0.0f, -50.0f, 100.0f, 500.0f});

    s.view->scroll_to(0.0, 0.0, {.animation_mode = ScrollAnimationMode::Disabled});
    s.view->sync_native(s.host);
    top = s.host.find(s.top->visual_id());
    CHECK(top->visible);
    CHECK_FALSE(top->clip.has_value());
}

TEST_CASE("A clip is only re-sent when it or the bounds change", "[native_control][scroll-view]")
{
    Scrolled  s;
    const int clips = s.host.find(s.tall->visual_id())->clip_count;
    const int shows = s.host.find(s.top->visual_id())->visible_count;
    s.view->sync_native(s.host);
    s.view->sync_native(s.host);
    CHECK(s.host.find(s.tall->visual_id())->clip_count == clips);
    CHECK(s.host.find(s.top->visual_id())->visible_count == shows);
}

TEST_CASE("A Collapsed native control, or one under a Collapsed ancestor, is hidden", "[native_control]")
{
    TestNativeHost host;
    StackControl   panel{Orientation::Vertical};
    NativeControl* control = panel.add(std::make_unique<NativeControl>(make_desc(), Size{100.0f, 20.0f}));
    (void)panel.measure({200.0f, 200.0f}, HeadlessTextMeasurer{}, Theme::default_light());
    panel.arrange({0.0f, 0.0f, 200.0f, 200.0f});
    panel.sync_native(host);
    REQUIRE(host.find(control->visual_id())->visible);

    // The ancestor collapses without re-arranging its children: their bounds stay as they were.
    panel.set_visibility(Visibility::Collapsed);
    panel.sync_native(host);
    CHECK_FALSE(host.find(control->visual_id())->visible);
    CHECK_FALSE(host.presented(control->visual_id()));

    panel.set_visibility(Visibility::Visible);
    panel.sync_native(host);
    CHECK(host.find(control->visual_id())->visible);

    control->set_visibility(Visibility::Collapsed);
    panel.sync_native(host);
    CHECK_FALSE(host.find(control->visual_id())->visible);
}

namespace
{
// A container that can stop recursing into its child, as Tabs and NavigationShell do for the
// entries they are not showing.
class Gate final : public ContentHost
{
public:
    bool open = true;

    // A real container marks itself when it switches what it shows; a layout with nothing marked
    // does nothing.
    void set_open(bool value)
    {
        open = value;
        mark_dirty(DirtyFlags::Visual);
    }

    void sync_native(NativeHost& host) override
    {
        if (open)
        {
            ContentHost::sync_native(host);
        }
    }
};
} // namespace

TEST_CASE("Window's sync pass hides a native control the walk no longer reaches", "[native_control][window]")
{
    TestNativeHost host;
    Window         window;
    seams(window).set_native_host(&host);

    auto           gate    = std::make_unique<Gate>();
    NativeControl* control = gate->set_child(std::make_unique<NativeControl>(make_desc(), Size{100.0f, 20.0f}));
    Gate*          raw     = window.set_content(std::move(gate));
    window.layout({200.0f, 100.0f}, HeadlessTextMeasurer{});
    REQUIRE(host.find(control->visual_id())->visible);

    raw->set_open(false);
    window.layout({200.0f, 100.0f}, HeadlessTextMeasurer{});
    CHECK_FALSE(host.find(control->visual_id())->visible);

    raw->set_open(true);
    window.layout({200.0f, 100.0f}, HeadlessTextMeasurer{});
    CHECK(host.find(control->visual_id())->visible);
}

TEST_CASE("set_native_size re-measures the control", "[native_control]")
{
    NativeControl control{make_desc(), Size{120.0f, 24.0f}};
    const Theme   theme = Theme::light();
    (void)control.measure({500.0f, 500.0f}, HeadlessTextMeasurer{}, theme);

    control.set_native_size(Size{200.0f, 160.0f});
    CHECK(control.native_size() == Size{200.0f, 160.0f});
    CHECK(control.measure({500.0f, 500.0f}, HeadlessTextMeasurer{}, theme) == Size{200.0f, 160.0f});
}

namespace
{
class NotifiedControl final : public NativeControl
{
public:
    using NativeControl::NativeControl;

    std::uint32_t last_message = 0;

    bool handle_native_notification(NativeNotification& notification) override
    {
        last_message = notification.message;
        if (notification.message != 0x004E) // WM_NOTIFY
        {
            return false;
        }
        notification.result = 42;
        return true;
    }
};
} // namespace

TEST_CASE("Window hands a native notification to the control it names", "[native_control][window]")
{
    TestNativeHost host;
    Window         window;
    seams(window).set_native_host(&host);
    auto             panel   = std::make_unique<StackControl>(Orientation::Vertical);
    NotifiedControl* control = panel->add(std::make_unique<NotifiedControl>(make_desc(), Size{100.0f, 20.0f}));
    window.set_content(std::move(panel));
    window.layout({200.0f, 100.0f}, HeadlessTextMeasurer{});

    NativeNotification handled{0x004E, 1, 2};
    CHECK(ui::testing::seams(window).handle_native_notification(*control, handled));
    CHECK(handled.result == 42);

    NativeNotification declined{0x0111, 1, 2}; // WM_COMMAND
    CHECK_FALSE(ui::testing::seams(window).handle_native_notification(*control, declined));
    CHECK(control->last_message == 0x0111);
}
