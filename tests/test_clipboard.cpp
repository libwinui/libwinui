// SPDX-License-Identifier: MIT
#include "winui/core/Color.hpp"
#include "winui/core/Property.hpp"
#include "winui/render/Bitmap.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestClipboard.hpp"
#include "winui/ui/Clipboard.hpp"
#include "winui/ui/text/TextBox.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include "platform/windows/Win32Clipboard.hpp"
#endif

#include <catch2/catch_test_macros.hpp>

using namespace ui;
using namespace ui::testing;

TEST_CASE("ui::clipboard() is the installed clipboard, and a TestClipboard uninstalls itself", "[clipboard]")
{
    {
        TestClipboard clipboard;
        set_clipboard(&clipboard);
        REQUIRE(&ui::clipboard() == &clipboard);
        ui::clipboard().set_text("copied");
        REQUIRE(clipboard.text() == "copied");
    }
    REQUIRE(ui::detail::installed_clipboard() == nullptr);
}

TEST_CASE("HeadlessUI installs a TestClipboard unless the test installed one", "[clipboard]")
{
    {
        HeadlessUI ui;
        (void)ui.window();
        REQUIRE(ui.clipboard() != nullptr);
        REQUIRE(&ui::clipboard() == ui.clipboard());
    }
    REQUIRE(ui::detail::installed_clipboard() == nullptr);

    TestClipboard own;
    set_clipboard(&own);
    {
        HeadlessUI ui;
        (void)ui.window();
        REQUIRE(ui.clipboard() == nullptr);
        REQUIRE(&ui::clipboard() == &own);
    }
    REQUIRE(&ui::clipboard() == &own);
}

TEST_CASE("Ctrl+C in a HeadlessUI window lands on its TestClipboard", "[clipboard]")
{
    HeadlessUI               ui;
    ui::Control<ui::TextBox> box;
    ui.window().set_content(ui::TextBox{.text = "Portland", .control = box});
    ui.layout({400, 300});
    REQUIRE(ui.window().focus_element(box));
    seams(ui.window()).handle_key({Key::A, KeyAction::Down, {.ctrl = true}});
    seams(ui.window()).handle_key({Key::C, KeyAction::Down, {.ctrl = true}});
    REQUIRE(ui.clipboard()->text() == "Portland");
}

TEST_CASE("TestClipboard holds one content at a time, all of its parts together", "[clipboard]")
{
    TestClipboard clipboard;
    CHECK_FALSE(clipboard.contains(StandardDataFormat::Text));
    CHECK(clipboard.available_formats().empty());

    ClipboardContent content;
    content.text                = "hello";
    content.bitmap              = Bitmap{2, 2, colors::White};
    content.storage_items       = {"C:\\a.txt", "C:\\b.txt"};
    content.requested_operation = DataPackageOperation::Move;
    REQUIRE(clipboard.set_content(content));

    CHECK(clipboard.text() == "hello");
    REQUIRE(clipboard.bitmap().has_value());
    CHECK(clipboard.bitmap()->width() == 2);
    CHECK(clipboard.storage_items() == std::vector<std::filesystem::path>{"C:\\a.txt", "C:\\b.txt"});
    CHECK(clipboard.requested_operation() == DataPackageOperation::Move);
    CHECK(clipboard.contains(StandardDataFormat::Text));
    CHECK(clipboard.contains(StandardDataFormat::Bitmap));
    CHECK(clipboard.contains(StandardDataFormat::StorageItems));
    CHECK(clipboard.available_formats() ==
          std::vector<std::string>{"CF_UNICODETEXT", "PNG", "CF_DIBV5", "CF_HDROP", "Preferred DropEffect"});

    // Text replaces everything, as copying anything does.
    clipboard.set_text("only text");
    CHECK(clipboard.text() == "only text");
    CHECK_FALSE(clipboard.bitmap().has_value());
    CHECK(clipboard.storage_items().empty());
    CHECK(clipboard.requested_operation() == DataPackageOperation::None);

    clipboard.clear();
    CHECK_FALSE(clipboard.text().has_value());
    CHECK(clipboard.available_formats().empty());
}

TEST_CASE("An empty ClipboardContent is refused and leaves the clipboard alone", "[clipboard]")
{
    TestClipboard clipboard;
    clipboard.set_text("kept");
    CHECK_FALSE(clipboard.set_content(ClipboardContent{}));
    CHECK(clipboard.text() == "kept");
}

TEST_CASE("ClipboardContentOptions turned off add the history and cloud formats", "[clipboard]")
{
    TestClipboard    clipboard;
    ClipboardContent content;
    content.text = "secret";
    REQUIRE(clipboard.set_content(content, {.is_allowed_in_history = false, .is_roamable = false}));
    CHECK_FALSE(clipboard.options().is_allowed_in_history);
    CHECK_FALSE(clipboard.options().is_roamable);
    CHECK(clipboard.available_formats() ==
          std::vector<std::string>{"CF_UNICODETEXT", "CanIncludeInClipboardHistory", "CanUploadToCloudClipboard"});

    REQUIRE(clipboard.set_content(content));
    CHECK(clipboard.available_formats() == std::vector<std::string>{"CF_UNICODETEXT"});
}

TEST_CASE("content_changed() counts every change, and dropping the subscription stops it", "[clipboard]")
{
    TestClipboard clipboard;
    int           changes = 0;
    Subscription  watch   = clipboard.content_changed().observe([&changes](const std::uint64_t&) { ++changes; });

    clipboard.set_text("a");
    clipboard.set_text("a"); // the same text again is still a change of the clipboard
    clipboard.clear();
    CHECK(changes == 3);
    CHECK(clipboard.content_changed().get() == 3);

    watch.reset();
    clipboard.set_text("b");
    CHECK(changes == 3);
}

TEST_CASE("A content_changed() subscription outliving its clipboard is harmless", "[clipboard]")
{
    Subscription watch;
    {
        TestClipboard clipboard;
        watch = clipboard.content_changed().observe([](const std::uint64_t&) {});
    }
    watch.reset(); // the clipboard severed the edge as it died
    SUCCEED();
}

#ifdef _WIN32

TEST_CASE("A picture survives Win32Clipboard's CF_DIBV5 block and the .bmp WIC decodes it from", "[clipboard]")
{
    Bitmap picture{3, 2, colors::White};
    picture.set(0, 0, Color::from_rgba(255, 0, 0));
    picture.set(2, 1, Color::from_rgba(0, 0, 255));

    const std::vector<std::uint8_t> dib = win32::dib_from_image(picture);
    REQUIRE(dib.size() == 124 + 3 * 2 * 4); // BITMAPV5HEADER, then the rows

    const std::vector<std::uint8_t> bmp = win32::bmp_file_from_dib(dib);
    REQUIRE(bmp.size() == 14 + dib.size());
    CHECK(bmp[0] == 'B');
    CHECK(bmp[1] == 'M');

    const Bitmap decoded = Bitmap::decode(bmp);
    REQUIRE(decoded.width() == 3);
    REQUIRE(decoded.height() == 2);
    CHECK(compare(decoded, picture).max_channel_delta == 0);
}

TEST_CASE("bmp_file_from_dib() points past a BITMAPINFOHEADER's color masks and palette", "[clipboard]")
{
    // A 1x1 32 bpp BI_BITFIELDS DIB: 40-byte header, three masks, one pixel.
    std::vector<std::uint8_t> dib(40 + 12 + 4, 0);
    const auto                put32 = [&dib](std::size_t at, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
        {
            dib[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
        }
    };
    put32(0, 40);
    put32(4, 1);
    put32(8, 1);
    dib[12] = 1;  // planes
    dib[14] = 32; // bit count
    put32(16, 3); // BI_BITFIELDS
    const std::vector<std::uint8_t> bmp = win32::bmp_file_from_dib(dib);
    REQUIRE(bmp.size() == 14 + dib.size());
    const std::uint32_t offset = bmp[10] | (bmp[11] << 8) | (bmp[12] << 16) | (bmp[13] << 24);
    CHECK(offset == 14 + 40 + 12);

    // An 8 bpp one with no biClrUsed has a full 256-color palette.
    std::vector<std::uint8_t> indexed(40 + 256 * 4 + 4, 0);
    indexed[0]                                  = 40;
    indexed[4]                                  = 1;
    indexed[8]                                  = 1;
    indexed[12]                                 = 1;
    indexed[14]                                 = 8;
    const std::vector<std::uint8_t> indexed_bmp = win32::bmp_file_from_dib(indexed);
    const std::uint32_t indexed_offset          = indexed_bmp[10] | (indexed_bmp[11] << 8) | (indexed_bmp[12] << 16) |
                                                  (indexed_bmp[13] << 24);
    CHECK(indexed_offset == 14 + 40 + 256 * 4);

    CHECK(win32::bmp_file_from_dib(std::vector<std::uint8_t>(10, 0)).empty());
}

#endif
