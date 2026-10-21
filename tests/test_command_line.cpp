// SPDX-License-Identifier: MIT
#include "winui/core/CommandLine.hpp"
#include "winui/core/Utf8.hpp"
#include "winui/core/Utf8Path.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace ui;

TEST_CASE("to_utf16 and from_utf16 round-trip every plane", "[utf8]")
{
    const std::u16string text = u"Dürrenhauser 漢字 \U0001F600";
    const std::string    utf8 = "D\xC3\xBCrrenhauser \xE6\xBC\xA2\xE5\xAD\x97 \xF0\x9F\x98\x80";

    REQUIRE("Dürrenhauser 漢字 \U0001F600" == utf8); // /utf-8 is on: a literal is UTF-8
    REQUIRE(from_utf16(text) == utf8);
    REQUIRE(to_utf16(utf8) == text);
    REQUIRE(from_utf16(u"").empty());
    REQUIRE(to_utf16("").empty());
}

TEST_CASE("Malformed input becomes U+FFFD and decoding carries on", "[utf8]")
{
    REQUIRE(sanitize_utf8("a\xC3") == "a�");           // truncated at the end
    REQUIRE(sanitize_utf8("a\xC3z") == "a�z");         // truncated, then more text
    REQUIRE(sanitize_utf8("\x80") == "�");             // stray continuation byte
    REQUIRE(sanitize_utf8("\xC0\xAF") == "�");         // overlong '/'
    REQUIRE(sanitize_utf8("\xED\xA0\x80") == "�");     // an encoded surrogate
    REQUIRE(sanitize_utf8("\xF4\x90\x80\x80") == "�"); // beyond U+10FFFF
    REQUIRE(sanitize_utf8("\xFF") == "�");             // never valid
    REQUIRE(to_utf16("\xFF") == u"�");
    REQUIRE(sanitize_utf8("ok �") == "ok �"); // a spelled-out U+FFFD is fine

    REQUIRE(is_valid_utf8("Dürrenhauser \U0001F600"));
    REQUIRE_FALSE(is_valid_utf8("a\xC3"));
    REQUIRE_FALSE(is_valid_utf8("\xED\xA0\x80"));

    // An unpaired surrogate in UTF-16.
    const std::u16string lone(1, static_cast<char16_t>(0xD800));
    REQUIRE(from_utf16(lone) == "\xEF\xBF\xBD");
}

TEST_CASE("Code-point stepping lands on boundaries", "[utf8]")
{
    const std::string text = "aä漢\U0001F600"; // 1 + 2 + 3 + 4 bytes
    REQUIRE(text.size() == 10);
    REQUIRE(code_point_count(text) == 4);

    std::vector<std::size_t> forward;
    for (std::size_t i = 0; i < text.size(); i = next_code_point(text, i))
    {
        forward.push_back(i);
    }
    REQUIRE(forward == std::vector<std::size_t>{0, 1, 3, 6});
    REQUIRE(next_code_point(text, 10) == 10);

    REQUIRE(previous_code_point(text, 10) == 6);
    REQUIRE(previous_code_point(text, 6) == 3);
    REQUIRE(previous_code_point(text, 3) == 1);
    REQUIRE(previous_code_point(text, 1) == 0);
    REQUIRE(previous_code_point(text, 0) == 0);

    REQUIRE(is_code_point_boundary(text, 3));
    REQUIRE_FALSE(is_code_point_boundary(text, 4));
    REQUIRE(floor_code_point(text, 8) == 6);

    REQUIRE(decode_utf8(text, 6).code_point == 0x1F600);
    REQUIRE(decode_utf8(text, 6).length == 4);
    REQUIRE(encode_utf8(0x1F600) == "\U0001F600");
    REQUIRE(encode_utf8(0xD800) == "�");

    std::vector<char32_t> cps;
    for (const char32_t cp : code_points(text))
    {
        cps.push_back(cp);
    }
    REQUIRE(cps == std::vector<char32_t>{'a', 0xE4, 0x6F22, 0x1F600});

    // A stray continuation byte is a code point of its own, both ways.
    const std::string bad = "a\x80z";
    REQUIRE(next_code_point(bad, 1) == 2);
    REQUIRE(previous_code_point(bad, 2) == 1);
}

TEST_CASE("A non-ASCII path round-trips through path_from_utf8", "[utf8]")
{
    const std::string           name = "Dürrenhauser-漢字 \U0001F600.txt";
    const std::filesystem::path path = path_from_utf8(name);
    REQUIRE(path.filename().u8string() == u8"Dürrenhauser-漢字 \U0001F600.txt");
    REQUIRE(path_to_utf8(path) == name);
}

TEST_CASE("command_line lists the program first", "[utf8][command_line]")
{
    const std::vector<std::string> args = command_line();
    REQUIRE_FALSE(args.empty());
    REQUIRE_FALSE(args.front().empty());
    for (const std::string& arg : args)
    {
        REQUIRE(is_valid_utf8(arg));
    }
}
