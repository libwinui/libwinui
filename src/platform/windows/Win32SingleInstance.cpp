// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "Win32SingleInstance.hpp"

#include "Win32MessageWindow.hpp"
#include "Win32String.hpp"
#include "winui/core/CommandLine.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <cstdint>

namespace ui::win32
{

namespace
{

// COPYDATASTRUCT::dwData of an activation, so a stray WM_COPYDATA is not taken for one.
constexpr ULONG_PTR ActivationTag = 0x4C57'4931; // "LWI1"

struct Claim
{
    HANDLE                             mutex = nullptr;
    std::wstring                       id;
    Callback<void(InstanceActivation)> on_activated{};
};

Claim& claim()
{
    static Claim state;
    return state;
}

// The mutex and the window title an `id` is known by; both stay inside the user's session.
std::wstring mutex_name(std::wstring_view id)
{
    return L"Local\\libwinui.instance." + std::wstring{id};
}
std::wstring window_title(std::wstring_view id)
{
    return L"libwinui.instance." + std::wstring{id};
}

// Backslashes are the one character a kernel object name can't hold.
std::wstring object_id(std::string_view id)
{
    std::wstring wide = widen(id);
    for (wchar_t& c : wide)
    {
        if (c == L'\\')
        {
            c = L'/';
        }
    }
    return wide;
}

} // namespace

std::string encode_activation(const InstanceActivation& activation)
{
    std::string data = narrow(activation.working_directory.wstring());
    data.push_back('\0');
    for (const std::string& argument : activation.arguments)
    {
        data += argument;
        data.push_back('\0');
    }
    return data;
}

std::optional<InstanceActivation> decode_activation(std::string_view data)
{
    if (data.empty() || data.back() != '\0')
    {
        return std::nullopt;
    }
    InstanceActivation activation;
    bool               first = true;
    while (!data.empty())
    {
        const std::size_t end = data.find('\0');
        const std::string part{data.substr(0, end)};
        data.remove_prefix(end + 1);
        if (first)
        {
            activation.working_directory = widen(part);
            first                        = false;
        }
        else
        {
            activation.arguments.push_back(part);
        }
    }
    return activation;
}

bool forward_to_instance(std::string_view id, const InstanceActivation& activation)
{
    const std::wstring title = window_title(object_id(id));
    const std::string  data  = encode_activation(activation);
    // The first instance may hold the mutex a moment before its window exists.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    for (;;)
    {
        if (const HWND target = FindWindowExW(HWND_MESSAGE, nullptr, MessageWindow::class_name(), title.c_str()))
        {
            DWORD process = 0;
            GetWindowThreadProcessId(target, &process);
            AllowSetForegroundWindow(process); // so it can bring its window to the front
            COPYDATASTRUCT message{};
            message.dwData       = ActivationTag;
            message.cbData       = static_cast<DWORD>(data.size());
            message.lpData       = const_cast<char*>(data.data());
            DWORD_PTR     answer = 0;
            const LRESULT sent   = SendMessageTimeoutW(target,
                                                       WM_COPYDATA,
                                                       0,
                                                       reinterpret_cast<LPARAM>(&message),
                                                       SMTO_ABORTIFHUNG,
                                                       5000,
                                                       &answer);
            return sent != 0 && answer != 0;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
}

void release_single_instance()
{
    Claim& state = claim();
    if (state.mutex != nullptr)
    {
        CloseHandle(state.mutex);
        state.mutex = nullptr;
    }
    state.id.clear();
    state.on_activated    = {};
    MessageWindow& window = MessageWindow::get();
    SetWindowTextW(window.hwnd(), L"");
    window.on_copy_data({});
}

} // namespace ui::win32

namespace ui
{

bool claim_single_instance(std::string_view id, Callback<void(InstanceActivation)> on_activated)
{
    using namespace win32;
    Claim& state = claim();
    if (state.mutex != nullptr)
    {
        return true;
    }
    const std::wstring name  = object_id(id);
    HANDLE             mutex = CreateMutexW(nullptr, FALSE, mutex_name(name).c_str());
    if (mutex == nullptr)
    {
        return true; // no way to tell; run rather than refuse to start
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(mutex);
        std::error_code error;
        (void)forward_to_instance(id, {command_line(), std::filesystem::current_path(error)});
        return false;
    }
    state.mutex        = mutex;
    state.id           = name;
    state.on_activated = std::move(on_activated);

    MessageWindow& window = MessageWindow::get();
    window.on_copy_data(
        [&window](const COPYDATASTRUCT& message)
        {
            if (message.dwData != ActivationTag || message.lpData == nullptr)
            {
                return false;
            }
            std::optional<InstanceActivation> activation = decode_activation(
                {static_cast<const char*>(message.lpData), message.cbData});
            if (!activation)
            {
                return false;
            }
            // The sender waits for this answer, so the app hears about it once it has returned.
            window.post(
                [activation = std::move(*activation)]() mutable
                {
                    if (Claim& current = claim(); current.on_activated)
                    {
                        current.on_activated(std::move(activation));
                    }
                });
            return true;
        });
    SetWindowTextW(window.hwnd(), window_title(name).c_str());
    return true;
}

} // namespace ui

#endif // _WIN32
