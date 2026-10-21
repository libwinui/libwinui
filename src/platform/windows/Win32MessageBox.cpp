// SPDX-License-Identifier: MIT
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Backend.hpp"
#include "ProcAddress.hpp"
#include "Win32String.hpp"
#include "winui/ui/MessageBox.hpp"
#include "winui/ui/Window.hpp"

// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on

#include <string>

namespace ui::detail
{

namespace
{

using TaskDialogIndirectFn = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);

// TaskDialogIndirect lives in version 6 of the common controls, which a process only gets
// through a manifest. An executable without one borrows shell32's (resource 124, which asks for
// version 6) for the length of the call.
class CommonControls6
{
public:
    CommonControls6()
    {
        wchar_t system[MAX_PATH]{};
        if (GetSystemDirectoryW(system, MAX_PATH) == 0)
        {
            return;
        }
        ACTCTXW context{};
        context.cbSize              = sizeof(context);
        context.dwFlags             = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID;
        context.lpSource            = L"shell32.dll";
        context.lpAssemblyDirectory = system;
        context.lpResourceName      = MAKEINTRESOURCEW(124);
        context_                    = CreateActCtxW(&context);
        if (context_ != INVALID_HANDLE_VALUE && !ActivateActCtx(context_, &cookie_))
        {
            cookie_ = 0;
        }
        if (HMODULE module = LoadLibraryW(L"comctl32.dll"))
        {
            module_      = module;
            task_dialog_ = win32::proc_address<TaskDialogIndirectFn>(module, "TaskDialogIndirect");
        }
    }
    ~CommonControls6()
    {
        if (module_ != nullptr)
        {
            FreeLibrary(module_);
        }
        if (cookie_ != 0)
        {
            DeactivateActCtx(0, cookie_);
        }
        if (context_ != INVALID_HANDLE_VALUE)
        {
            ReleaseActCtx(context_);
        }
    }
    CommonControls6(const CommonControls6&)            = delete;
    CommonControls6& operator=(const CommonControls6&) = delete;

    [[nodiscard]] TaskDialogIndirectFn task_dialog() const
    {
        return task_dialog_;
    }

private:
    HANDLE               context_     = INVALID_HANDLE_VALUE;
    ULONG_PTR            cookie_      = 0;
    HMODULE              module_      = nullptr;
    TaskDialogIndirectFn task_dialog_ = nullptr;
};

MessageBoxResult to_result(int id, MessageBoxButtons buttons)
{
    switch (id)
    {
        case IDYES:
            return MessageBoxResult::Yes;
        case IDNO:
            return MessageBoxResult::No;
        case IDRETRY:
            return MessageBoxResult::Retry;
        case IDOK:
            return MessageBoxResult::Ok;
        default:
            // Escape or the close button on a box whose only button is OK answers OK.
            return buttons == MessageBoxButtons::Ok ? MessageBoxResult::Ok : MessageBoxResult::Cancel;
    }
}

std::wstring program_name()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring name = path;
    if (const auto slash = name.find_last_of(L"\\/"); slash != std::wstring::npos)
    {
        name.erase(0, slash + 1);
    }
    if (const auto dot = name.find_last_of(L'.'); dot != std::wstring::npos)
    {
        name.erase(dot);
    }
    return name;
}

// The plain MessageBox, for a system where the task dialog cannot be loaded. It has no heading,
// so the heading becomes the first paragraph.
MessageBoxResult show_plain(HWND owner, const std::wstring& title, const MessageBoxOptions& options)
{
    std::wstring text = win32::widen(options.text);
    if (!options.heading.empty())
    {
        text = win32::widen(options.heading) + (text.empty() ? L"" : L"\n\n" + text);
    }
    UINT type = MB_OK;
    switch (options.buttons)
    {
        case MessageBoxButtons::Ok:
            type = MB_OK;
            break;
        case MessageBoxButtons::OkCancel:
            type = MB_OKCANCEL;
            break;
        case MessageBoxButtons::YesNo:
            type = MB_YESNO;
            break;
        case MessageBoxButtons::YesNoCancel:
            type = MB_YESNOCANCEL;
            break;
        case MessageBoxButtons::RetryCancel:
            type = MB_RETRYCANCEL;
            break;
    }
    switch (options.icon)
    {
        case MessageBoxIcon::None:
            break;
        case MessageBoxIcon::Information:
            type |= MB_ICONINFORMATION;
            break;
        case MessageBoxIcon::Warning:
            type |= MB_ICONWARNING;
            break;
        case MessageBoxIcon::Error:
            type |= MB_ICONERROR;
            break;
    }
    return to_result(MessageBoxW(owner, text.c_str(), title.c_str(), type), options.buttons);
}

} // namespace

MessageBoxResult platform_message_box(const Window* owner, const MessageBoxOptions& options)
{
    const HWND         owner_window = owner != nullptr ? static_cast<HWND>(owner->native_handle()) : nullptr;
    const std::wstring title        = options.title.empty() ? program_name() : win32::widen(options.title);
    const std::wstring heading      = win32::widen(options.heading);
    const std::wstring text         = win32::widen(options.text);

    const CommonControls6 controls;
    if (controls.task_dialog() == nullptr)
    {
        return show_plain(owner_window, title, options);
    }

    TASKDIALOGCONFIG config{};
    config.cbSize             = sizeof(config);
    config.hwndParent         = owner_window;
    config.dwFlags            = TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle     = title.c_str();
    config.pszMainInstruction = heading.empty() ? nullptr : heading.c_str();
    config.pszContent         = text.empty() ? nullptr : text.c_str();
    switch (options.buttons)
    {
        case MessageBoxButtons::Ok:
            config.dwCommonButtons = TDCBF_OK_BUTTON;
            config.dwFlags |= TDF_ALLOW_DIALOG_CANCELLATION;
            break;
        case MessageBoxButtons::OkCancel:
            config.dwCommonButtons = TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON;
            break;
        case MessageBoxButtons::YesNo:
            config.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
            break;
        case MessageBoxButtons::YesNoCancel:
            config.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON | TDCBF_CANCEL_BUTTON;
            break;
        case MessageBoxButtons::RetryCancel:
            config.dwCommonButtons = TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON;
            break;
    }
    switch (options.icon)
    {
        case MessageBoxIcon::None:
            break;
        case MessageBoxIcon::Information:
            config.pszMainIcon = TD_INFORMATION_ICON;
            break;
        case MessageBoxIcon::Warning:
            config.pszMainIcon = TD_WARNING_ICON;
            break;
        case MessageBoxIcon::Error:
            config.pszMainIcon = TD_ERROR_ICON;
            break;
    }
    int button = 0;
    if (FAILED(controls.task_dialog()(&config, &button, nullptr, nullptr)))
    {
        return show_plain(owner_window, title, options);
    }
    return to_result(button, options.buttons);
}

} // namespace ui::detail

#endif // _WIN32
