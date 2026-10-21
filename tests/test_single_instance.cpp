// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include "platform/windows/Win32SingleInstance.hpp"
#include "winui/ui/SingleInstance.hpp"

#include <chrono>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

#include <catch2/catch_test_macros.hpp>

using namespace ui;

namespace
{
// Pumps this thread's messages until `done` or a second has passed.
template <typename Done>
void pump_until(Done done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    while (!done() && std::chrono::steady_clock::now() < deadline)
    {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
}
} // namespace

TEST_CASE("an activation round-trips through its encoding", "[single_instance]")
{
    const InstanceActivation sent{{"notes.exe", "--new", "Grüße.txt", ""}, std::filesystem::path{L"C:\\Users\\Ada"}};
    const std::optional<InstanceActivation> received = win32::decode_activation(win32::encode_activation(sent));
    REQUIRE(received);
    CHECK(received->arguments == sent.arguments);
    CHECK(received->working_directory == sent.working_directory);
    CHECK_FALSE(win32::decode_activation("no terminator"));
}

TEST_CASE("a second start hands its command line to the instance holding the claim", "[single_instance]")
{
    const std::string                 id = std::format("libwinui.tests.{}", GetCurrentProcessId());
    std::optional<InstanceActivation> heard;
    REQUIRE(claim_single_instance(id, [&](InstanceActivation activation) { heard = std::move(activation); }));
    CHECK(claim_single_instance(id, {})); // the holder claims again: still true

    // A second process would do this from claim_single_instance(); here another thread plays it.
    bool        forwarded = false;
    std::thread second([&] { forwarded = win32::forward_to_instance(id, {{"notes.exe", "todo.txt"}, L"D:\\work"}); });
    pump_until([&] { return heard.has_value(); });
    second.join();
    CHECK(forwarded);

    REQUIRE(heard);
    CHECK(heard->arguments == std::vector<std::string>{"notes.exe", "todo.txt"});
    CHECK(heard->working_directory == std::filesystem::path{L"D:\\work"});

    win32::release_single_instance();
    CHECK(claim_single_instance(id, {})); // released: free to claim again
    win32::release_single_instance();
}
#endif
