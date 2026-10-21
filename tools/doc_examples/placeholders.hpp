// SPDX-License-Identifier: MIT
// What the doc examples (tools/doc_examples.cpp) may use without declaring it: what an app or a
// custom control usually has at hand, and the app's own functions the examples call. Declarations
// only; the examples are compiled, never linked or run.
#pragma once

#include <winui/testing.hpp>
#include <winui/winui.hpp>

#include <print>

// The test framework's checks, for the testing tier's examples.
#define REQUIRE(...)       static_cast<void>(__VA_ARGS__)
#define CHECK(...)         static_cast<void>(__VA_ARGS__)
#define REQUIRE_FALSE(...) static_cast<void>(__VA_ARGS__)
#define CHECK_FALSE(...)   static_cast<void>(__VA_ARGS__)

namespace doc_placeholders
{
using namespace std::chrono_literals;
using ui::operator""_fr;

// The app's main window.
extern ui::Window& window;
// What a custom control's render_override() and measure_override() are handed.
extern ui::Renderer&           renderer;
extern const ui::Theme&        theme;
extern const ui::TextMeasurer& measurer;

// The app's own actions, which the examples call from handlers.
void open_file();
void save();
void save_document();
void save_document(const std::filesystem::path& path);
void open_document(const std::filesystem::path& path);
void quit();
void restart_app();
void open_settings();
void remove_item(std::string_view item);
void reload_list();
void crunch();
std::string read_file(const std::string& path);
std::string fetch_status();
} // namespace doc_placeholders
