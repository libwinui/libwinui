// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "platform/windows/ShellDialogs.hpp"
#include "winui/ui/Pickers.hpp"

#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using ui::win32::FileDialogFilter;
using FileTypeChoice = ui::FileType;
using ui::win32::picker_file_types;

namespace
{

bool same(const std::vector<FileDialogFilter>& a, const std::vector<FileDialogFilter>& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].name != b[i].name || a[i].pattern != b[i].pattern)
        {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("No file types at all is one All Files filter, selected", "[storage_pickers]")
{
    const auto types = picker_file_types({}, {});
    CHECK(same(types.filters, {{"All Files", "*"}}));
    CHECK(types.initial_index == 0);
}

TEST_CASE("A file type filter lists each extension, then their union selected", "[storage_pickers]")
{
    const std::vector<std::string> filter{".jpg", ".png"};
    const auto                     types = picker_file_types(filter, {});
    CHECK(same(types.filters, {{"*.jpg", "*.jpg"}, {"*.png", "*.png"}, {"All Files", "*.jpg;*.png"}}));
    CHECK(types.initial_index == 2);

    const auto first = picker_file_types(filter, {}, 0);
    CHECK(first.initial_index == 0);
}

TEST_CASE("A lone \"*\" filter is just All Files", "[storage_pickers]")
{
    const std::vector<std::string> filter{"*"};
    const auto                     types = picker_file_types(filter, {}, -1, "Alle Dateien");
    CHECK(same(types.filters, {{"Alle Dateien", "*"}}));
    CHECK(types.initial_index == 0);
}

TEST_CASE("File type choices win over the filter, with no All Files added", "[storage_pickers]")
{
    const std::vector<std::string>    filter{".txt"};
    const std::vector<FileTypeChoice> choices{{.name = "Text Files", .extensions = {".txt"}},
                                              {.name = "Images", .extensions = {".jpg", ".png"}},
                                              {.name = "Text Files", .extensions = {".txt", ".md"}}};
    const auto                        types = picker_file_types(filter, choices);
    // The repeated name replaced the first entry's extensions in its place.
    CHECK(same(types.filters, {{"Text Files", "*.txt;*.md"}, {"Images", "*.jpg;*.png"}}));
    CHECK(types.initial_index == -1);
    CHECK(picker_file_types({}, choices, 1).initial_index == 1);
}

TEST_CASE("Malformed file types and indices throw", "[storage_pickers]")
{
    for (const char* bad : {"txt", "", ".t.xt", ".t*", ".?", "*.txt"})
    {
        const std::vector<std::string> filter{bad};
        CHECK_THROWS_AS(picker_file_types(filter, {}), std::invalid_argument);
    }
    const std::vector<FileTypeChoice> choices{{.name = "Text", .extensions = {"txt"}}};
    CHECK_THROWS_AS(picker_file_types({}, choices), std::invalid_argument);

    const std::vector<std::string> filter{".txt"};
    CHECK_THROWS_AS(picker_file_types(filter, {}, -2), std::invalid_argument);
    CHECK_THROWS_AS(picker_file_types(filter, {}, 2), std::invalid_argument); // .txt and All Files only
    CHECK_NOTHROW(picker_file_types(filter, {}, 1));
}

#endif
