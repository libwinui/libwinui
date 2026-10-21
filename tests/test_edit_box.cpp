// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "edit_box.hpp"
#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/NativeHost.hpp"
#include "winui/render/Text.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestNativeHost.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/ui/themes/Theme.hpp"

#include <utility>
#include <windows.h>

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace ui;
using namespace ui::testing;
using native_controls::EditBox;
using native_controls::EditBoxOptions;

namespace
{
// Outlives every box synced with it: a native control's destructor calls back into its last host.
TestNativeHost& shared_host()
{
    static TestNativeHost host;
    return host;
}

std::uint32_t created_style(EditBox& box)
{
    box.arrange({0.0f, 0.0f, 100.0f, 30.0f});
    box.sync_native(shared_host());
    return shared_host().find(box.visual_id())->desc.style;
}
} // namespace

TEST_CASE("EditBox's options pick the EDIT styles it is created with", "[edit_box][native_control]")
{
    SECTION("the default: several lines, wrapped, a vertical scroll bar")
    {
        EditBox box{Size{200.0f, 80.0f}, "text"};
        box.arrange({0.0f, 0.0f, 200.0f, 80.0f});
        box.sync_native(shared_host());
        const TestNativeHost::Entry* entry = shared_host().find(box.visual_id());
        REQUIRE(entry != nullptr);
        CHECK(entry->desc.class_name == "EDIT");
        CHECK(entry->desc.initial_text == "text");
        const std::uint32_t style = entry->desc.style;
        CHECK((style & ES_MULTILINE) != 0);
        CHECK((style & ES_WANTRETURN) != 0);
        CHECK((style & WS_VSCROLL) != 0);
        CHECK((style & ES_AUTOHSCROLL) == 0); // a multi-line EDIT wraps unless told to scroll
        CHECK((style & WS_HSCROLL) == 0);
    }
    SECTION("one line scrolls sideways")
    {
        EditBox             box{Size{200.0f, 0.0f}, "", EditBoxOptions{.accepts_return = false}};
        const std::uint32_t style = created_style(box);
        CHECK((style & ES_MULTILINE) == 0);
        CHECK((style & ES_AUTOHSCROLL) != 0);
    }
    SECTION("NoWrap scrolls several lines sideways")
    {
        EditBox             box{Size{200.0f, 80.0f}, "", EditBoxOptions{.wrapping = TextWrapping::NoWrap}};
        const std::uint32_t style = created_style(box);
        CHECK((style & ES_MULTILINE) != 0);
        CHECK((style & ES_AUTOHSCROLL) != 0);
        CHECK((style & WS_HSCROLL) != 0);
    }
    SECTION("alignment")
    {
        EditBox center{Size{200.0f, 0.0f}, "", EditBoxOptions{.alignment = TextAlignment::Center}};
        EditBox right{Size{200.0f, 0.0f}, "", EditBoxOptions{.alignment = TextAlignment::Right}};
        EditBox justify{Size{200.0f, 0.0f}, "", EditBoxOptions{.alignment = TextAlignment::Justify}};
        CHECK((created_style(center) & ES_CENTER) != 0);
        CHECK((created_style(right) & ES_RIGHT) != 0);
        CHECK((created_style(justify) & (ES_CENTER | ES_RIGHT)) == 0);
    }
    SECTION("a password box is one line, whatever accepts_return says")
    {
        EditBox box{Size{200.0f, 0.0f}, "", EditBoxOptions{.accepts_return = true, .password = true}};
        CHECK_FALSE(box.options().accepts_return);
        const std::uint32_t style = created_style(box);
        CHECK((style & ES_PASSWORD) != 0);
        CHECK((style & ES_MULTILINE) == 0);
    }
}

TEST_CASE("EditBox's header sits above the box, and bounds() is the box", "[edit_box][native_control]")
{
    HeadlessTextMeasurer measurer;
    const Theme          theme = Theme::light();
    EditBox              box{Size{200.0f, 30.0f}, "", EditBoxOptions{.accepts_return = false}};
    const Size           bare = box.measure({500.0f, 500.0f}, measurer, theme);
    CHECK(bare == Size{200.0f, 30.0f});

    box.set_header("Enter your name:");
    const Size with_header = box.measure({500.0f, 500.0f}, measurer, theme);
    CHECK(with_header.width == 200.0f);
    CHECK(with_header.height > 30.0f + 8.0f); // the header's text plus its gap to the box

    box.arrange({10.0f, 20.0f, with_header.width, with_header.height});
    CHECK(box.bounds().y > 20.0f);
    CHECK(std::abs(box.bounds().height - 30.0f) < 1e-3f);

    box.sync_native(shared_host());
    CHECK(shared_host().find(box.visual_id())->bounds == box.bounds());

    // The framework draws the header; the box itself is the window's to paint.
    TestRenderer renderer;
    box.render(renderer, theme);
    CHECK(renderer.count() > 0);
}

TEST_CASE("EditBox with no height asks for one line until its font says otherwise", "[edit_box][native_control]")
{
    EditBox box{Size{200.0f, 0.0f}, "", EditBoxOptions{.accepts_return = false}};
    CHECK(box.native_size() == Size{200.0f, 23.0f});
}

TEST_CASE("EditBox keeps its properties until its window exists", "[edit_box][native_control]")
{
    EditBox box{Size{200.0f, 0.0f}, "first", EditBoxOptions{.accepts_return = false}};
    CHECK(box.text() == "first");
    box.set_text("second");
    CHECK(box.text() == "second");

    box.set_placeholder_text("Name");
    box.set_read_only(true);
    box.set_enabled(false);
    box.set_max_length(-3);
    box.set_character_casing(CharacterCasing::Upper);
    box.set_number_only(true);
    box.set_password_char('#');
    box.set_password_revealed(true);
    CHECK(box.placeholder_text() == "Name");
    CHECK(box.read_only());
    CHECK_FALSE(box.enabled());
    CHECK(box.max_length() == 0);
    CHECK(box.character_casing() == CharacterCasing::Upper);
    CHECK(box.number_only());
    CHECK(box.password_char() == '#');
    CHECK(box.password_revealed());
    CHECK(box.selection() == std::pair<std::size_t, std::size_t>{0, 0});
}

TEST_CASE("EditBox answers WM_CTLCOLOR* only when it has colors of its own", "[edit_box][native_control]")
{
    EditBox            box{Size{200.0f, 0.0f}};
    NativeNotification edit{WM_CTLCOLOREDIT, 0, 0};
    CHECK_FALSE(box.handle_native_notification(edit)); // light theme, nothing set: the system's colors

    NativeNotification other{WM_COMMAND, 0, 0};
    box.set_foreground(colors::Black);
    CHECK_FALSE(box.handle_native_notification(other));
}

#endif // _WIN32
